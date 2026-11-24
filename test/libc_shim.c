/*
        libc_shim: src/shim/libc.c's names held against the C library they stand in front of, and timed against it.

        Built in a bowl against that bowl's C library (glibc in arch, musl in alpine), with no preload:
          cc -O1 -o libc_shim test/libc_shim.c -ldl
          ./libc_shim /path/libmoonwater-c.so [bench]

        The shim is opened RTLD_LOCAL, so the program's own memcpy stays the C library's, and each name is called through a
        pointer on both sides. Every input sits at the end of a page whose next page is unmapped, or at the start of a page
        whose previous one is, so a body that reads past either end faults. Every byte around a destination is a canary.
        Results: a pointer is compared as an offset into its buffer, a comparison by its sign (bcmp by zero or not), and the
        destination's bytes and the canaries on both sides are compared whole.

        The checked names are tried in a child each, with a destination one byte too small: the child must end by SIGABRT.

        bench: cycles a call (rdtsc around 4096 calls, the best of 64 rounds, the loop alone subtracted) for the hot names at
        the lengths browsers and games make, both sides through the same pointer call.

        bench compare [OLD.so|-] [NAME...]: the compare, copy and search names, at fixed lengths and at lengths drawn at
        random, against the C library and against a second build of the shim (bench_compare below says how).
*/
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 4096ul
#define SPAN (16 * PAGE)

static unsigned long failures, checks;
static uint64_t seed = 0x9e3779b97f4a7c15ull;

static uint64_t next(void)
{
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
}

/* A region of SPAN bytes with an unmapped page on each side. */
static unsigned char *region(void)
{
        unsigned char *all = mmap(0, SPAN + 2 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (all == MAP_FAILED) {
                perror("mmap");
                exit(2);
        }
        mprotect(all, PAGE, PROT_NONE);
        mprotect(all + PAGE + SPAN, PAGE, PROT_NONE);
        return all + PAGE;
}

static void fail(const char *name, const char *what, size_t a, size_t b, size_t c)
{
        if (failures < 60)
                printf("FAIL %s: %s (%zu %zu %zu)\n", name, what, a, b, c);
        failures++;
}

/* Where an input of length n starts: against the end of the region, against the start, or in the middle at offset align. */
static size_t place(size_t n, int where, size_t align)
{
        if (where == 0)
                return SPAN - n;
        if (where == 1)
                return align;
        return (SPAN / 2 + align) % (SPAN - n);
}

typedef void *(*copy_fn)(void *, const void *, size_t);
typedef void *(*fill_fn)(void *, int, size_t);
typedef int (*cmp_fn)(const void *, const void *, size_t);
typedef void *(*find_fn)(const void *, int, size_t);
typedef void *(*mem_fn)(const void *, size_t, const void *, size_t);
typedef void *(*ccpy_fn)(void *, const void *, int, size_t);
typedef void (*zero_fn)(void *, size_t);
typedef size_t (*len_fn)(const char *);
typedef size_t (*nlen_fn)(const char *, size_t);
typedef int (*scmp_fn)(const char *, const char *);
typedef int (*sncmp_fn)(const char *, const char *, size_t);
typedef char *(*schr_fn)(const char *, int);
typedef char *(*scpy_fn)(char *, const char *);
typedef char *(*sstr_fn)(const char *, const char *);
typedef size_t (*sspn_fn)(const char *, const char *);
typedef char *(*spbrk_fn)(const char *, const char *);

static unsigned char *src, *dst_a, *dst_b, *src2;

static int sign(int v) { return (v > 0) - (v < 0); }

static void fill_random(unsigned char *p, size_t n, unsigned alphabet)
{
        for (size_t i = 0; i < n; i++) {
                unsigned v = (unsigned)next();
                p[i] = alphabet ? (unsigned char)(1 + v % alphabet) : (unsigned char)v;
        }
}

static size_t lengths[1024];
static size_t length_count;

static void make_lengths(void)
{
        for (size_t n = 0; n <= 320; n++)
                lengths[length_count++] = n;
        static const size_t more[] = {383, 384, 385, 447, 448, 449, 511, 512, 513, 767, 768, 1023, 1024, 1025, 2047, 2048, 2049,
                                      3000, 4095, 4096, 4097, 8191, 8192, 8193, 12000, 16384, 30000, 65535 - 2 * PAGE};
        for (size_t i = 0; i < sizeof more / sizeof *more; i++)
                if (more[i] <= SPAN - 64)
                        lengths[length_count++] = more[i];
}

/* memcpy-shaped: both destinations from the same canary, then compared whole. */
static void check_copy(const char *name, copy_fn ours, copy_fn theirs, int returns_end)
{
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                for (int where = 0; where < 3; where++)
                        for (size_t align = 0; align < (n < 512 ? 64 : 8); align += (n < 512 ? 1 : 3)) {
                                size_t s = place(n, where, align), d = place(n, (where + 1) % 3, (align * 7) % 64);
                                fill_random(src + s, n, 0);
                                memset(dst_a, 0xa5, SPAN);
                                memset(dst_b, 0xa5, SPAN);
                                void *ra = ours(dst_a + d, src + s, n);
                                void *rb = theirs(dst_b + d, src + s, n);
                                checks++;
                                if ((unsigned char *)ra - dst_a != (unsigned char *)rb - dst_b)
                                        fail(name, "return", n, s, d);
                                if (returns_end ? (unsigned char *)ra != dst_a + d + n : (unsigned char *)ra != dst_a + d)
                                        fail(name, "return value", n, s, d);
                                if (memcmp(dst_a, dst_b, SPAN))
                                        fail(name, "bytes", n, s, d);
                        }
        }
}

/* memmove inside one buffer, both directions and every overlap distance near the length. */
static void check_move(const char *name, copy_fn ours, copy_fn theirs)
{
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                if (n > SPAN / 2 - 512)
                        continue;
                for (long shift = -130; shift <= 130; shift += (n < 512 ? 1 : 13)) {
                        size_t base = SPAN / 4;
                        size_t s = base + 200, d = (size_t)((long)s + shift);
                        fill_random(dst_a, SPAN, 0);
                        memcpy(dst_b, dst_a, SPAN);
                        void *ra = ours(dst_a + d, dst_a + s, n);
                        void *rb = theirs(dst_b + d, dst_b + s, n);
                        checks++;
                        if ((unsigned char *)ra - dst_a != (unsigned char *)rb - dst_b)
                                fail(name, "return", n, s, d);
                        if (memcmp(dst_a, dst_b, SPAN))
                                fail(name, "bytes", n, s, d);
                }
        }
}

static void check_fill(fill_fn ours, fill_fn theirs)
{
        static const int values[] = {0, 1, 0x7f, 0x80, 0xff, 0x1ff, -1, -128, 0x12345678};
        for (size_t li = 0; li < length_count; li++)
                for (int where = 0; where < 3; where++)
                        for (size_t align = 0; align < 64; align += (lengths[li] < 512 ? 1 : 7)) {
                                size_t n = lengths[li], d = place(n, where, align);
                                int v = values[next() % (sizeof values / sizeof *values)];
                                memset(dst_a, 0x5a, SPAN);
                                memset(dst_b, 0x5a, SPAN);
                                void *ra = ours(dst_a + d, v, n);
                                void *rb = theirs(dst_b + d, v, n);
                                checks++;
                                if ((unsigned char *)ra - dst_a != (unsigned char *)rb - dst_b)
                                        fail("memset", "return", n, d, (size_t)v);
                                if (memcmp(dst_a, dst_b, SPAN))
                                        fail("memset", "bytes", n, d, (size_t)v);
                        }
}

static void check_zero(zero_fn ours)
{
        for (size_t li = 0; li < length_count; li++)
                for (size_t align = 0; align < 64; align += 5) {
                        size_t n = lengths[li], d = place(n, (int)(align % 3), align);
                        memset(dst_a, 0x5a, SPAN);
                        memset(dst_b, 0x5a, SPAN);
                        ours(dst_a + d, n);
                        memset(dst_b + d, 0, n);
                        checks++;
                        if (memcmp(dst_a, dst_b, SPAN))
                                fail("bzero", "bytes", n, d, 0);
                }
}

/* memcmp and bcmp: equal inputs, then one byte changed at every position (both signs, and the top bit). */
static void check_compare(const char *name, cmp_fn ours, cmp_fn theirs, int zero_only)
{
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                for (int where = 0; where < 3; where++) {
                        size_t a = place(n, where, (n * 3) % 64), b = place(n, (where + 2) % 3, (n * 5) % 64);
                        fill_random(src + a, n, 0);
                        memcpy(src2 + b, src + a, n);
                        checks++;
                        if (sign(ours(src + a, src2 + b, n)) != sign(theirs(src + a, src2 + b, n)))
                                fail(name, "equal", n, a, b);
                        size_t step = n < 320 ? 1 : n / 97 + 1;
                        for (size_t at = 0; at < n; at += step) {
                                unsigned char keep = src2[b + at];
                                src2[b + at] = (unsigned char)(keep ^ (at & 1 ? 0x80 : 0x01));
                                int x = ours(src + a, src2 + b, n), y = theirs(src + a, src2 + b, n);
                                checks++;
                                if (zero_only ? ((x == 0) != (y == 0)) : sign(x) != sign(y))
                                        fail(name, "differ", n, at, (size_t)keep);
                                x = ours(src2 + b, src + a, n), y = theirs(src2 + b, src + a, n);
                                if (zero_only ? ((x == 0) != (y == 0)) : sign(x) != sign(y))
                                        fail(name, "differ swapped", n, at, (size_t)keep);
                                src2[b + at] = keep;
                        }
                }
        }
}

/* Where an input of n bytes sits against a page edge, j bytes in from it: 0 its last byte j before the end of the region (the
   page after unmapped), 1 its first j after the region's start (the page before unmapped), 2 ending j before an edge inside
   the region, 3 starting j after one, 4 across one, 5 in the middle of a page. */
static size_t edge_place(size_t n, int k, size_t j)
{
        switch (k) {
        case 0: return SPAN - n - j;
        case 1: return j;
        case 2: return 5 * PAGE - n - j;
        case 3: return 7 * PAGE + j;
        case 4: return 9 * PAGE - n / 2 - j;
        default: return 11 * PAGE + 1000 + j;
        }
}

/* memcmp and bcmp at every length to 300 with each input against every kind of page edge, at every offset from it to 72
   for the two that have an unmapped page beside them: equal, then one byte changed first, last and in the middle. */
static void check_compare_edges(const char *name, cmp_fn ours, int zero_only)
{
        for (size_t n = 0; n <= 300; n += (n < 140 ? 1 : 7))
                for (int ka = 0; ka < 6; ka++)
                        for (size_t ja = 0; ja < 72; ja += (ka < 2 ? 1 : 11))
                                for (int kb = 0; kb < 6; kb++) {
                                        size_t a = edge_place(n, ka, ja), b = edge_place(n, kb, (ja * 7 + (size_t)kb * 13) % 72);
                                        fill_random(src + a, n, 0);
                                        memcpy(src2 + b, src + a, n);
                                        for (int d = 0; d < 4; d++) {
                                                size_t at = d == 1 ? 0 : d == 2 ? n - 1 : n / 2;
                                                if (d && !n)
                                                        break;
                                                if (d)
                                                        src2[b + at] ^= (unsigned char)(d == 3 ? 0x80 : 0x01);
                                                int x = ours(src + a, src2 + b, n), y = memcmp(src + a, src2 + b, n);
                                                checks++;
                                                if (zero_only ? ((x == 0) != (y == 0)) : sign(x) != sign(y))
                                                        fail(name, "against a page edge", n, a, b);
                                                x = ours(src2 + b, src + a, n), y = memcmp(src2 + b, src + a, n);
                                                if (zero_only ? ((x == 0) != (y == 0)) : sign(x) != sign(y))
                                                        fail(name, "against a page edge, swapped", n, a, b);
                                                if (d)
                                                        src2[b + at] ^= (unsigned char)(d == 3 ? 0x80 : 0x01);
                                        }
                                }
}

/* strcmp and strncmp the same way: strings of every length to 300 whose terminator sits against every kind of page edge,
   equal, one byte changed first, last and in the middle, and the second ended early; strncmp under bounds around the length.
   Then strncmp of arrays with no terminator at all, n bytes ending against the unmapped page, which may be read to n only. */
static void check_string_compare_edges(scmp_fn ours, sncmp_fn ours_n)
{
        for (size_t n = 0; n <= 300; n += (n < 140 ? 1 : 7))
                for (int ka = 0; ka < 6; ka++)
                        for (size_t ja = 0; ja < 72; ja += (ka < 2 ? 3 : 11))
                                for (int kb = 0; kb < 6; kb++) {
                                        size_t a = edge_place(n + 1, ka, ja), b = edge_place(n + 1, kb, (ja * 7 + (size_t)kb * 13) % 72);
                                        fill_random(src + a, n, 255);
                                        src[a + n] = 0;
                                        memcpy(src2 + b, src + a, n + 1);
                                        const char *x = (const char *)src + a, *y = (const char *)src2 + b;
                                        for (int d = 0; d < 5; d++) {
                                                size_t at = d == 1 ? 0 : d == 2 ? n - 1 : n / 2;
                                                if (d && !n)
                                                        break;
                                                unsigned char keep = src2[b + at];
                                                if (d == 4)
                                                        src2[b + at] = 0;
                                                else if (d)
                                                        src2[b + at] = (unsigned char)(keep == 0xff ? 1 : keep + 1);
                                                checks++;
                                                if (sign(ours(x, y)) != sign(strcmp(x, y)) || sign(ours(y, x)) != sign(strcmp(y, x)))
                                                        fail("strcmp", "against a page edge", n, a, b);
                                                if (d != 1 && d != 3) {
                                                        size_t bounds[] = {0, 1, n / 2, n, n + 1, n + 64, (size_t)-1};
                                                        for (int k = 0; k < 7; k++)
                                                                if (sign(ours_n(x, y, bounds[k])) != sign(strncmp(x, y, bounds[k])) ||
                                                                    sign(ours_n(y, x, bounds[k])) != sign(strncmp(y, x, bounds[k])))
                                                                        fail("strncmp", "against a page edge", n, bounds[k], (size_t)d);
                                                }
                                                if (d)
                                                        src2[b + at] = keep;
                                        }
                                }
        for (size_t n = 1; n <= 320; n++)
                for (int k = 0; k < 4; k++) {
                        size_t a = k & 1 ? 0 : SPAN - n, b = k & 2 ? 0 : SPAN - n;
                        fill_random(src + a, n, 255);
                        memcpy(src2 + b, src + a, n);
                        checks++;
                        if (ours_n((char *)src + a, (char *)src2 + b, n) != 0)
                                fail("strncmp", "no terminator, equal", n, a, b);
                        unsigned char keep = src2[b + n - 1];
                        src2[b + n - 1] = (unsigned char)(keep == 0xff ? 1 : keep + 1);
                        if (sign(ours_n((char *)src + a, (char *)src2 + b, n)) != sign(strncmp((char *)src + a, (char *)src2 + b, n)))
                                fail("strncmp", "no terminator, the last byte differs", n, a, b);
                        src2[b + n - 1] = keep;
                }
}

/* memchr and memrchr: the byte absent, then placed once at each position (and its value given with high bits set). */
static void check_find(const char *name, find_fn ours, find_fn theirs)
{
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                for (int where = 0; where < 3; where++) {
                        size_t a = place(n, where, (n * 11) % 64);
                        fill_random(src + a, n, 200);   // bytes 1..200
                        int want = 0xee;
                        void *x = ours(src + a, want, n), *y = theirs(src + a, want, n);
                        checks++;
                        if (x != y)
                                fail(name, "absent", n, a, 0);
                        size_t step = n < 320 ? 1 : n / 61 + 1;
                        for (size_t at = 0; at < n; at += step) {
                                src[a + at] = 0xee;
                                if (at + 7 < n && (at & 2))
                                        src[a + at + 7] = 0xee;     // a second one, after
                                int asked = (at & 1) ? (int)0xffffffee : 0x1ee;
                                x = ours(src + a, asked, n), y = theirs(src + a, asked, n);
                                checks++;
                                if (x != y)
                                        fail(name, "present", n, at, a);
                                src[a + at] = (unsigned char)(1 + at % 200);
                                if (at + 7 < n && (at & 2))
                                        src[a + at + 7] = (unsigned char)(1 + (at + 7) % 200);
                        }
                }
        }
}

/* A string of length n ending at the guard page's edge (its terminator the last mapped byte), or starting at a page start. */
static char *string_at(unsigned char *buffer, size_t n, int where, size_t align, unsigned alphabet)
{
        size_t at = where == 0 ? SPAN - n - 1 : where == 1 ? align : (SPAN / 3 + align);
        fill_random(buffer + at, n, alphabet);
        buffer[at + n] = 0;
        return (char *)buffer + at;
}

static void check_strings(void *shim)
{
        len_fn strlen_ours = (len_fn)dlsym(shim, "strlen");
        nlen_fn strnlen_ours = (nlen_fn)dlsym(shim, "strnlen");
        scmp_fn strcmp_ours = (scmp_fn)dlsym(shim, "strcmp");
        sncmp_fn strncmp_ours = (sncmp_fn)dlsym(shim, "strncmp");
        schr_fn strchr_ours = (schr_fn)dlsym(shim, "strchr"), strrchr_ours = (schr_fn)dlsym(shim, "strrchr"),
                strchrnul_ours = (schr_fn)dlsym(shim, "strchrnul");
        scpy_fn strcpy_ours = (scpy_fn)dlsym(shim, "strcpy"), stpcpy_ours = (scpy_fn)dlsym(shim, "stpcpy"),
                strcat_ours = (scpy_fn)dlsym(shim, "strcat");
        sstr_fn strstr_ours = (sstr_fn)dlsym(shim, "strstr");
        sspn_fn strspn_ours = (sspn_fn)dlsym(shim, "strspn"), strcspn_ours = (sspn_fn)dlsym(shim, "strcspn");
        spbrk_fn strpbrk_ours = (spbrk_fn)dlsym(shim, "strpbrk");
        scmp_fn strcmp_theirs = strcmp;
        sncmp_fn strncmp_theirs = strncmp;
        len_fn strlen_theirs = strlen;
        nlen_fn strnlen_theirs = strnlen;
        schr_fn strchr_theirs = (schr_fn)strchr, strrchr_theirs = (schr_fn)strrchr, strchrnul_theirs = (schr_fn)strchrnul;
        sstr_fn strstr_theirs = (sstr_fn)strstr;

        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                if (n > SPAN / 3)
                        continue;
                for (int where = 0; where < 3; where++)
                        for (size_t align = 0; align < 64; align += (n < 320 ? 1 : 9)) {
                                char *s = string_at(src, n, where, align, 255);
                                checks++;
                                if (strlen_ours(s) != strlen_theirs(s))
                                        fail("strlen", "length", n, (size_t)where, align);
                                size_t limits[] = {0, 1, n / 2, n, n + 1, n + 64, (size_t)-1};
                                for (int k = 0; k < 7; k++)
                                        if (strnlen_ours(s, limits[k]) != strnlen_theirs(s, limits[k]))
                                                fail("strnlen", "length", n, limits[k], align);
                                /* no terminator at all: n bytes whose last is the last mapped one, the bound n, n - 1 and
                                   n - 64; a read past the bound faults */
                                if (where == 0 && n) {
                                        unsigned char *unended = src2 + SPAN - n;
                                        fill_random(unended, n, 255);
                                        for (size_t cut = 0; cut < 128 && cut < n; cut += cut < 2 ? 1 : 63) {
                                                checks++;
                                                if (strnlen_ours((char *)unended + cut, n - cut) != n - cut)
                                                        fail("strnlen", "unended", n, cut, align);
                                        }
                                }
                                /* strchr, strrchr, strchrnul: absent, the terminator, then each position */
                                char *t = string_at(src2, n, (where + 1) % 3, (align * 3) % 64, 200);
                                for (int c = 0; c < 3; c++) {
                                        int asked = c == 0 ? 0xee : c == 1 ? 0 : 0x1ee;
                                        if (strchr_ours(t, asked) != strchr_theirs(t, asked))
                                                fail("strchr", "absent or end", n, (size_t)asked, align);
                                        if (strrchr_ours(t, asked) != strrchr_theirs(t, asked))
                                                fail("strrchr", "absent or end", n, (size_t)asked, align);
                                        if (strchrnul_ours(t, asked) != strchrnul_theirs(t, asked))
                                                fail("strchrnul", "absent or end", n, (size_t)asked, align);
                                }
                                if (n < 320 || align == 0) {
                                        size_t step = n < 320 ? 1 : n / 37 + 1;
                                        for (size_t at = 0; at < n; at += step) {
                                                char keep = t[at];
                                                t[at] = (char)0xee;
                                                if (at + 3 < n)
                                                        t[at + 3] = (char)0xee;
                                                if (strchr_ours(t, 0xee) != strchr_theirs(t, 0xee))
                                                        fail("strchr", "present", n, at, align);
                                                if (strrchr_ours(t, 0xee) != strrchr_theirs(t, 0xee))
                                                        fail("strrchr", "present", n, at, align);
                                                if (strchrnul_ours(t, 0xee) != strchrnul_theirs(t, 0xee))
                                                        fail("strchrnul", "present", n, at, align);
                                                t[at] = keep;
                                                if (at + 3 < n)
                                                        t[at + 3] = (char)(1 + (at + 3) % 200);
                                        }
                                }
                                /* strcmp, strncmp: equal, a prefix, one byte changed */
                                char *u = string_at(src2, n, (where + 2) % 3, (align * 5) % 64, 255);
                                memcpy(u, s, n);
                                if (sign(strcmp_ours(s, u)) != sign(strcmp_theirs(s, u)))
                                        fail("strcmp", "equal", n, (size_t)where, align);
                                for (int k = 0; k < 7; k++)
                                        if (sign(strncmp_ours(s, u, limits[k])) != sign(strncmp_theirs(s, u, limits[k])))
                                                fail("strncmp", "equal", n, limits[k], align);
                                if (n) {
                                        size_t at = (size_t)next() % n;
                                        unsigned char keep = (unsigned char)u[at];
                                        u[at] = (char)(keep == 0xff ? 0x7f : keep == 1 ? 0x80 : keep + 1);
                                        if (sign(strcmp_ours(s, u)) != sign(strcmp_theirs(s, u)))
                                                fail("strcmp", "differ", n, at, align);
                                        if (sign(strcmp_ours(u, s)) != sign(strcmp_theirs(u, s)))
                                                fail("strcmp", "differ swapped", n, at, align);
                                        for (int k = 0; k < 7; k++)
                                                if (sign(strncmp_ours(s, u, limits[k])) != sign(strncmp_theirs(s, u, limits[k])))
                                                        fail("strncmp", "differ", n, at, limits[k]);
                                        u[at] = 0;      // a prefix of s
                                        if (sign(strcmp_ours(s, u)) != sign(strcmp_theirs(s, u)) ||
                                            sign(strcmp_ours(u, s)) != sign(strcmp_theirs(u, s)))
                                                fail("strcmp", "prefix", n, at, align);
                                        u[at] = (char)keep;
                                }
                                /* strcpy, stpcpy, strcat: into canaried destinations */
                                if (n < SPAN / 4) {
                                        size_t d = (where == 0) ? SPAN - n - 1 : (align * 7) % 64;
                                        memset(dst_a, 0xa5, SPAN);
                                        memset(dst_b, 0xa5, SPAN);
                                        char *ra = strcpy_ours((char *)dst_a + d, s), *rb = strcpy((char *)dst_b + d, s);
                                        if (ra - (char *)dst_a != rb - (char *)dst_b || memcmp(dst_a, dst_b, SPAN))
                                                fail("strcpy", "copy", n, d, align);
                                        memset(dst_a, 0xa5, SPAN);
                                        memset(dst_b, 0xa5, SPAN);
                                        ra = stpcpy_ours((char *)dst_a + d, s), rb = stpcpy((char *)dst_b + d, s);
                                        if (ra - (char *)dst_a != rb - (char *)dst_b || memcmp(dst_a, dst_b, SPAN))
                                                fail("stpcpy", "copy", n, d, align);
                                        size_t head = n / 3, d2 = (align * 3) % 64;
                                        if (d2 + head + n + 1 < SPAN) {
                                                memset(dst_a, 0xa5, SPAN);
                                                memset(dst_a + d2, 'h', head);
                                                dst_a[d2 + head] = 0;
                                                memcpy(dst_b, dst_a, SPAN);
                                                ra = strcat_ours((char *)dst_a + d2, s), rb = strcat((char *)dst_b + d2, s);
                                                if (ra - (char *)dst_a != rb - (char *)dst_b || memcmp(dst_a, dst_b, SPAN))
                                                        fail("strcat", "append", n, d2, head);
                                        }
                                }
                                checks += 20;
                        }
        }

        /* strstr: needles from the haystack itself, absent ones, the empty one, and repetitive shapes */
        for (int round = 0; round < 6000; round++) {
                size_t n = (size_t)next() % (round < 3000 ? 200 : 6000);
                unsigned alphabet = (unsigned)(round % 4 == 0 ? 2 : round % 4 == 1 ? 4 : round % 4 == 2 ? 26 : 255);
                char *h = string_at(src, n, round % 3, (size_t)round % 64, alphabet);
                size_t m = (size_t)next() % (round & 1 ? 70 : 12);       // across the needle sizes the bodies split at
                char needle_room[80];
                char *needle;
                if (round % 5 == 0 || n == 0) {
                        fill_random((unsigned char *)needle_room, m, alphabet);
                        needle_room[m] = 0;
                        needle = needle_room;
                } else {
                        size_t at = (size_t)next() % n;
                        if (m > n - at)
                                m = n - at;
                        memcpy(needle_room, h + at, m);
                        needle_room[m] = 0;
                        if (round % 7 == 0 && m)
                                needle_room[m - 1] ^= 1;
                        needle = needle_room;
                }
                /* the needle also against the page edge in src2 */
                char *nd = string_at(src2, strlen(needle), round % 2 ? 0 : 1, 0, 1);
                memcpy(nd, needle, strlen(needle));
                checks++;
                if (strstr_ours(h, nd) != strstr_theirs(h, nd))
                        fail("strstr", "position", n, strlen(needle), (size_t)round);
        }

        /* strspn, strcspn, strpbrk: sets of every size from the same bytes */
        for (int round = 0; round < 6000; round++) {
                size_t n = (size_t)next() % (round < 3000 ? 100 : 3000);
                char *h = string_at(src, n, round % 3, (size_t)round % 64, (unsigned)(round % 3 == 0 ? 3 : 40));
                char set_room[300];
                size_t k = (size_t)next() % (round % 2 ? 6 : 120);
                for (size_t i = 0; i < k; i++)
                        set_room[i] = (char)(1 + next() % (round % 3 == 0 ? 3 : 254));
                set_room[k] = 0;
                checks++;
                if (strspn_ours(h, set_room) != strspn(h, set_room))
                        fail("strspn", "span", n, k, (size_t)round);
                if (strcspn_ours(h, set_room) != strcspn(h, set_room))
                        fail("strcspn", "span", n, k, (size_t)round);
                if (strpbrk_ours(h, set_room) != strpbrk(h, set_room))
                        fail("strpbrk", "position", n, k, (size_t)round);
        }
}

static void check_memmem(mem_fn ours)
{
        for (int round = 0; round < 20000; round++) {
                size_t n = (size_t)next() % (round < 10000 ? 300 : 9000);
                unsigned alphabet = (unsigned)(round % 3 == 0 ? 2 : round % 3 == 1 ? 5 : 0);
                size_t a = place(n, round % 3, (size_t)round % 64);
                fill_random(src + a, n, alphabet);
                size_t m = (size_t)next() % (round & 1 ? 70 : 12);       // across the needle sizes the bodies split at
                size_t b;
                if (round % 4 == 0 || m > n) {
                        b = place(m, (round + 1) % 3, 7);
                        fill_random(src2 + b, m, alphabet);
                } else {
                        size_t at = (size_t)next() % (n - m + 1);
                        b = place(m, (round + 1) % 3, 7);
                        memcpy(src2 + b, src + a + at, m);
                        if (round % 9 == 0 && m)
                                src2[b + m - 1] ^= 1;
                }
                checks++;
                if (ours(src + a, n, src2 + b, m) != memmem(src + a, n, src2 + b, m))
                        fail("memmem", "position", n, m, (size_t)round);
        }
}

static void check_memccpy(ccpy_fn ours)
{
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                for (int round = 0; round < (n < 320 ? 6 : 2); round++) {
                        size_t s = place(n, round % 3, (size_t)round * 9), d = place(n, (round + 1) % 3, (size_t)round * 5);
                        fill_random(src + s, n, 200);
                        if (n && round % 2)
                                src[s + (size_t)next() % n] = 0xee;
                        memset(dst_a, 0xa5, SPAN);
                        memset(dst_b, 0xa5, SPAN);
                        void *ra = ours(dst_a + d, src + s, (round & 2) ? 0x1ee : 0xee, n);
                        void *rb = memccpy(dst_b + d, src + s, (round & 2) ? 0x1ee : 0xee, n);
                        checks++;
                        if ((ra == 0) != (rb == 0) || (ra && (unsigned char *)ra - dst_a != (unsigned char *)rb - dst_b))
                                fail("memccpy", "return", n, s, d);
                        if (memcmp(dst_a, dst_b, SPAN))
                                fail("memccpy", "bytes", n, s, d);
                }
        }
}

typedef char *(*sncpy_fn)(char *, const char *, size_t);
typedef size_t (*slcpy_fn)(char *, const char *, size_t);
/* strcpy, stpcpy, strcat, strncpy and stpncpy with the source's terminator against every kind of page edge, at every length
   to 300, into a destination that ends on the unmapped page or sits after it, canaries all round; strcat onto heads of a few
   lengths; the bounded two under bounds around the length, and with sources that have no terminator inside the bound, n bytes
   ending against the unmapped page. */
static void check_copy_edges(void *shim)
{
        scpy_fn cpy[3] = {(scpy_fn)dlsym(shim, "strcpy"), (scpy_fn)dlsym(shim, "stpcpy"), (scpy_fn)dlsym(shim, "strcat")};
        scpy_fn cpy_c[3] = {strcpy, stpcpy, strcat};
        const char *cpy_name[3] = {"strcpy", "stpcpy", "strcat"};
        sncpy_fn ncpy[2] = {(sncpy_fn)dlsym(shim, "strncpy"), (sncpy_fn)dlsym(shim, "stpncpy")};
        sncpy_fn ncpy_c[2] = {strncpy, stpncpy};
        const char *ncpy_name[2] = {"strncpy", "stpncpy"};
        for (size_t n = 0; n <= 300; n += (n < 140 ? 1 : 7))
                for (int ka = 0; ka < 6; ka++)
                        for (size_t ja = 0; ja < 72; ja += (ka < 2 ? 5 : 23)) {
                                size_t a = edge_place(n + 1, ka, ja);
                                fill_random(src + a, n, 255);
                                src[a + n] = 0;
                                const char *x = (const char *)src + a;
                                for (int kd = 0; kd < 3; kd++) {
                                        size_t head = kd == 2 ? (ja * 3 + n) % 150 : 0;
                                        size_t d = kd == 0 ? SPAN - n - 1 - head : kd == 1 ? (ja * 7) % 64 : PAGE * 3 - head - (n / 3);
                                        for (int k = 0; k < 3; k++) {
                                                memset(dst_a, 0xa5, SPAN);
                                                if (k == 2) {
                                                        memset(dst_a + d, 'h', head);
                                                        dst_a[d + head] = 0;
                                                } else if (head)
                                                        continue;
                                                memcpy(dst_b, dst_a, SPAN);
                                                char *ra = cpy[k]((char *)dst_a + d, x), *rb = cpy_c[k]((char *)dst_b + d, x);
                                                checks++;
                                                if (ra - (char *)dst_a != rb - (char *)dst_b || memcmp(dst_a, dst_b, SPAN))
                                                        fail(cpy_name[k], "against a page edge", n, a, d);
                                        }
                                        size_t bounds[] = {0, 1, n / 2, n, n + 1, n + 63, n + 64, n + 65, 2 * n + 70};
                                        for (int b = 0; b < 9; b++) {
                                                size_t bound = bounds[b], dd = kd == 0 ? SPAN - bound : kd == 1 ? (ja * 7) % 64 : PAGE * 3 - bound / 2;
                                                if (bound > SPAN - 64 || (kd == 2 && bound / 2 > PAGE * 3))
                                                        continue;
                                                for (int k = 0; k < 2; k++) {
                                                        memset(dst_a, 0xa5, SPAN);
                                                        memset(dst_b, 0xa5, SPAN);
                                                        char *ra = ncpy[k]((char *)dst_a + dd, x, bound), *rb = ncpy_c[k]((char *)dst_b + dd, x, bound);
                                                        checks++;
                                                        if (ra - (char *)dst_a != rb - (char *)dst_b || memcmp(dst_a, dst_b, SPAN))
                                                                fail(ncpy_name[k], "against a page edge", n, bound, dd);
                                                }
                                        }
                                }
                        }
        /* no terminator inside the bound: n bytes that end on the unmapped page, the bound n and less */
        for (size_t n = 1; n <= 320; n++)
                for (int k = 0; k < 2; k++) {
                        size_t a = SPAN - n;
                        fill_random(src + a, n, 255);
                        size_t bounds[] = {n, n - n / 3, n > 64 ? n - 64 : n};
                        for (int b = 0; b < 3; b++) {
                                memset(dst_a, 0xa5, SPAN);
                                memset(dst_b, 0xa5, SPAN);
                                char *ra = ncpy[k]((char *)dst_a + 5, (char *)src + a, bounds[b]);
                                char *rb = ncpy_c[k]((char *)dst_b + 5, (char *)src + a, bounds[b]);
                                checks++;
                                if (ra - (char *)dst_a != rb - (char *)dst_b || memcmp(dst_a, dst_b, SPAN))
                                        fail(ncpy_name[k], "no terminator", n, bounds[b], 0);
                        }
                }
}

typedef char *(*ssep_fn)(char **, const char *);
typedef char *(*stok_fn)(char *, const char *);
typedef char *(*stokr_fn)(char *, const char *, char **);
typedef void (*bcopy_fn)(const void *, void *, size_t);
typedef void *(*frob_fn)(void *, size_t);
typedef uint32_t *(*wset_fn)(uint32_t *, uint32_t, size_t);
typedef uint32_t *(*wcpy_fn)(uint32_t *, const uint32_t *, size_t);
typedef void *(*rawchr_fn)(const void *, int);

/* The C library's own copy of a name, or null where it has none (musl has no memfrob, no wmempcpy). */
static void *theirs_of(const char *name) { return dlsym(RTLD_DEFAULT, name); }

static void same_bytes(const char *name, const char *what, size_t a, size_t b)
{
        checks++;
        if (memcmp(dst_a, dst_b, SPAN))
                fail(name, what, a, b, 0);
}

/* The names that are a second label on one already held above, and the small routines, each against the C library's. */
static void check_more(void *shim)
{
        schr_fn index_ours = (schr_fn)dlsym(shim, "index"), rindex_ours = (schr_fn)dlsym(shim, "rindex");
        for (int round = 0; round < 4000; round++) {
                size_t n = (size_t)next() % (round < 2000 ? 130 : 3000);
                char *s = string_at(src, n, round % 3, (size_t)round % 64, 30);
                int c = round % 4 == 0 ? 0 : round % 4 == 1 ? 0xee : (int)(1 + next() % 30);
                checks++;
                if (index_ours(s, c) != strchr(s, c))
                        fail("index", "position", n, (size_t)c, (size_t)round);
                if (rindex_ours(s, c) != strrchr(s, c))
                        fail("rindex", "position", n, (size_t)c, (size_t)round);
        }

        cmp_fn eq = (cmp_fn)dlsym(shim, "__memcmpeq");
        check_compare("__memcmpeq", eq, memcmp, 1);

        zero_fn zero_names[] = {(zero_fn)dlsym(shim, "explicit_bzero"), (zero_fn)dlsym(shim, "__bzero")};
        for (int k = 0; k < 2; k++)
                for (size_t li = 0; li < length_count; li += 3) {
                        size_t n = lengths[li], d = place(n, (int)(li % 3), li % 64);
                        memset(dst_a, 0x5a, SPAN);
                        memset(dst_b, 0x5a, SPAN);
                        zero_names[k](dst_a + d, n);
                        memset(dst_b + d, 0, n);
                        same_bytes(k ? "__bzero" : "explicit_bzero", "bytes", n, d);
                }
        check_fill((fill_fn)dlsym(shim, "memset_explicit"), memset);

        /* rawmemchr: the byte at every place, and as the very last byte before the unmapped page */
        rawchr_fn raw[] = {(rawchr_fn)dlsym(shim, "rawmemchr"), (rawchr_fn)dlsym(shim, "__rawmemchr")};
        for (int k = 0; k < 2; k++)
                for (size_t li = 0; li < length_count; li++) {
                        size_t n = lengths[li] + 1;
                        if (n > SPAN / 2)
                                continue;
                        for (int where = 0; where < 3; where++) {
                                size_t a = place(n, where, (n * 13) % 64);
                                fill_random(src + a, n, 200);
                                src[a + n - 1] = 0xee;
                                size_t at = n > 1 ? (size_t)next() % n : 0;
                                if (where != 0)
                                        src[a + at] = 0xee;
                                int asked = (n & 1) ? 0xee : (int)0xffffffee;
                                checks++;
                                if (raw[k](src + a, asked) != memchr(src + a, 0xee, n))
                                        fail(k ? "__rawmemchr" : "rawmemchr", "position", n, at, a);
                        }
                }

        /* strncpy, stpncpy, strncat, strlcpy, strlcat: every bound against every length near it */
        sncpy_fn ncpy[] = {(sncpy_fn)dlsym(shim, "strncpy"), (sncpy_fn)dlsym(shim, "stpncpy"), (sncpy_fn)dlsym(shim, "__stpncpy"),
                           (sncpy_fn)dlsym(shim, "strncat")};
        sncpy_fn ncpy_c[] = {strncpy, stpncpy, stpncpy, strncat};
        const char *ncpy_name[] = {"strncpy", "stpncpy", "__stpncpy", "strncat"};
        slcpy_fn lcpy[] = {(slcpy_fn)dlsym(shim, "strlcpy"), (slcpy_fn)dlsym(shim, "strlcat")};
        slcpy_fn lcpy_c[] = {(slcpy_fn)theirs_of("strlcpy"), (slcpy_fn)theirs_of("strlcat")};
        for (int round = 0; round < 12000; round++) {
                size_t n = (size_t)next() % (round < 8000 ? 140 : 3000);
                char *s = string_at(src, n, round % 3, (size_t)round % 64, 255);
                size_t bound = round % 3 == 0 ? n : round % 3 == 1 ? (size_t)next() % (n + 70) : n + (size_t)next() % 4;
                size_t head = (round & 4) ? (size_t)next() % 40 : 0;
                size_t d = (round % 5 == 0 && bound + head + 1 < SPAN) ? SPAN - bound - head - 1 : (size_t)round % 64;
                for (int k = 0; k < 4; k++) {
                        memset(dst_a, 0xa5, SPAN);
                        if (k == 3) {
                                memset(dst_a + d, 'h', head);
                                dst_a[d + head] = 0;
                        }
                        memcpy(dst_b, dst_a, SPAN);
                        if (k == 3 && d + head + bound + 1 >= SPAN)
                                continue;
                        if (k != 3 && d + bound > SPAN)
                                continue;
                        char *ra = ncpy[k]((char *)dst_a + d, s, bound), *rb = ncpy_c[k]((char *)dst_b + d, s, bound);
                        checks++;
                        if (ra - (char *)dst_a != rb - (char *)dst_b)
                                fail(ncpy_name[k], "return", n, bound, d);
                        same_bytes(ncpy_name[k], "bytes", n, bound);
                }
                for (int k = 0; k < 2; k++) {
                        if (!lcpy_c[k] || d + head + bound + 1 >= SPAN)
                                continue;
                        memset(dst_a, 0xa5, SPAN);
                        if (k == 1) {
                                memset(dst_a + d, 'h', head);
                                dst_a[d + head] = 0;
                        }
                        memcpy(dst_b, dst_a, SPAN);
                        size_t x = lcpy[k]((char *)dst_a + d, s, bound), y = lcpy_c[k]((char *)dst_b + d, s, bound);
                        checks++;
                        if (x != y)
                                fail(k ? "strlcat" : "strlcpy", "return", n, bound, head);
                        same_bytes(k ? "strlcat" : "strlcpy", "bytes", n, bound);
                }
        }

        /* strsep, strtok, strtok_r, __strtok_r: the same text split by both, every token's place and the text after */
        ssep_fn sep = (ssep_fn)dlsym(shim, "strsep");
        stok_fn tok = (stok_fn)dlsym(shim, "strtok");
        stokr_fn tokr[] = {(stokr_fn)dlsym(shim, "strtok_r"), (stokr_fn)dlsym(shim, "__strtok_r")};
        for (int round = 0; round < 6000; round++) {
                size_t n = (size_t)next() % (round < 4000 ? 90 : 2000);
                char *s = string_at(src, n, round % 3, (size_t)round % 64, 6);
                char set[8];
                size_t k = 1 + (size_t)next() % 3;
                for (size_t i = 0; i < k; i++)
                        set[i] = (char)(1 + next() % 6);
                set[k] = round % 17 == 0 ? (set[0] = 0) : 0;
                size_t at = (size_t)round % 64;
                for (int form = 0; form < 4; form++) {
                        memcpy(dst_a + at, s, n + 1);
                        memcpy(dst_b + at, s, n + 1);
                        char *pa = (char *)dst_a + at, *pb = (char *)dst_b + at;
                        char *qa = 0, *qb = 0;
                        for (int step = 0; step < 4000; step++) {
                                char *ra, *rb;
                                if (form == 0)
                                        ra = sep(&pa, set), rb = strsep(&pb, set);
                                else if (form == 1)
                                        ra = tok(step ? 0 : pa, set), rb = strtok(step ? 0 : pb, set);
                                else
                                        ra = tokr[form - 2](step ? 0 : pa, set, &qa), rb = strtok_r(step ? 0 : pb, set, &qb);
                                checks++;
                                if ((ra == 0) != (rb == 0) || (ra && ra - (char *)dst_a != rb - (char *)dst_b))
                                        fail(form == 0 ? "strsep" : form == 1 ? "strtok" : form == 2 ? "strtok_r" : "__strtok_r",
                                             "token", n, (size_t)step, (size_t)round);
                                if (form == 0 && ((pa == 0) != (pb == 0) || (pa && pa - (char *)dst_a != pb - (char *)dst_b)))
                                        fail("strsep", "rest", n, (size_t)step, (size_t)round);
                                if (!ra || !rb)
                                        break;
                        }
                        same_bytes(form == 0 ? "strsep" : form == 1 ? "strtok" : "strtok_r", "text after", n, (size_t)round);
                }
        }

        /* memfrob: forty two in every byte, twice is nothing, the start handed back */
        frob_fn frob = (frob_fn)dlsym(shim, "memfrob");
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li], d = place(n, (int)(li % 3), li % 64);
                fill_random(dst_a, SPAN, 0);
                memcpy(dst_b, dst_a, SPAN);
                for (size_t i = 0; i < n; i++)
                        dst_b[d + i] ^= 42;
                checks++;
                if (frob(dst_a + d, n) != dst_a + d)
                        fail("memfrob", "return", n, d, 0);
                same_bytes("memfrob", "bytes", n, d);
        }

        /* bcopy: memmove with its arguments the other way round */
        bcopy_fn bc = (bcopy_fn)dlsym(shim, "bcopy");
        for (size_t li = 0; li < length_count; li += 2)
                for (long shift = -70; shift <= 70; shift += 7) {
                        size_t n = lengths[li];
                        if (n > SPAN / 2)
                                continue;
                        size_t s = SPAN / 4, d = (size_t)((long)s + shift);
                        fill_random(dst_a, SPAN, 0);
                        memcpy(dst_b, dst_a, SPAN);
                        bc(dst_a + s, dst_a + d, n);
                        memmove(dst_b + d, dst_b + s, n);
                        same_bytes("bcopy", "bytes", n, d);
                }

        /* the wide ones: counts of four byte units */
        wset_fn wset = (wset_fn)dlsym(shim, "wmemset"), wset_c = (wset_fn)theirs_of("wmemset");
        wcpy_fn wcpy[] = {(wcpy_fn)dlsym(shim, "wmemcpy"), (wcpy_fn)dlsym(shim, "wmemmove"), (wcpy_fn)dlsym(shim, "wmempcpy")};
        wcpy_fn wcpy_c[] = {(wcpy_fn)theirs_of("wmemcpy"), (wcpy_fn)theirs_of("wmemmove"), (wcpy_fn)theirs_of("wmempcpy")};
        for (size_t li = 0; li < length_count; li++) {
                size_t n = lengths[li];
                if (n * 4 + 64 > SPAN / 2)
                        continue;
                size_t d = (li % 2) ? SPAN - n * 4 : (li % 16) * 4;
                uint32_t v = (uint32_t)next();
                memset(dst_a, 0x5a, SPAN);
                memset(dst_b, 0x5a, SPAN);
                uint32_t *ra = wset((uint32_t *)(dst_a + d), v, n), *rb = wset_c((uint32_t *)(dst_b + d), v, n);
                checks++;
                if ((unsigned char *)ra - dst_a != (unsigned char *)rb - dst_b)
                        fail("wmemset", "return", n, d, 0);
                same_bytes("wmemset", "bytes", n, d);
                for (int k = 0; k < 3; k++) {
                        size_t s = (li % 3) * 4 + 64, t = (k == 1) ? s + (li % 5) * 4 : d;
                        fill_random(src, SPAN, 0);
                        memset(dst_a, 0x5a, SPAN);
                        memset(dst_b, 0x5a, SPAN);
                        if (k == 1) {
                                memcpy(dst_a, src, SPAN);
                                memcpy(dst_b, src, SPAN);
                                t = (li & 1) ? s + 4 * (li % 7) : s - 4 * (li % 7 < s / 4 ? li % 7 : 0);
                        }
                        const uint32_t *from_a = k == 1 ? (uint32_t *)(dst_a + s) : (uint32_t *)(src + s);
                        const uint32_t *from_b = k == 1 ? (uint32_t *)(dst_b + s) : (uint32_t *)(src + s);
                        if (t + n * 4 > SPAN)
                                continue;
                        uint32_t *xa = wcpy[k]((uint32_t *)(dst_a + t), from_a, n), *xb;
                        if (wcpy_c[k])
                                xb = wcpy_c[k]((uint32_t *)(dst_b + t), from_b, n);
                        else {
                                memcpy(dst_b + t, from_b, n * 4);
                                xb = (uint32_t *)(dst_b + t) + n;
                        }
                        checks++;
                        if ((unsigned char *)xa - dst_a != (unsigned char *)xb - dst_b)
                                fail(k == 0 ? "wmemcpy" : k == 1 ? "wmemmove" : "wmempcpy", "return", n, t, s);
                        same_bytes(k == 0 ? "wmemcpy" : k == 1 ? "wmemmove" : "wmempcpy", "bytes", n, t);
                }
        }

        /* wmemset on a destination that is not a whole word (wchar_t promises one; a caller can still hand this over):
           every store has to keep the word's phase, so the aligned stores of the wide rungs must not be reached. Held to
           a word at a time and not to the C library: glibc's own loses the phase there from 128 bytes. */
        for (size_t n = 0; n < 300; n++)
                for (size_t off = 1; off < 4; off++) {
                        size_t d = 64 + off + (n % 16) * 4;
                        uint32_t v = (uint32_t)next();
                        memset(dst_a, 0x5a, SPAN);
                        memset(dst_b, 0x5a, SPAN);
                        uint32_t *ra = wset((uint32_t *)(dst_a + d), v, n);
                        for (size_t i = 0; i < n; i++)
                                memcpy(dst_b + d + 4 * i, &v, 4);
                        checks++;
                        if ((unsigned char *)ra != dst_a + d)
                                fail("wmemset off a word", "return", n, d, 0);
                        same_bytes("wmemset off a word", "bytes", n, d);
                }

        /* ffs and the absolute values, over the bits one at a time and at random */
        int (*ffs_ours)(int) = (int (*)(int))dlsym(shim, "ffs");
        int (*ffsl_ours[2])(long) = {(int (*)(long))dlsym(shim, "ffsl"), (int (*)(long))dlsym(shim, "ffsll")};
        int (*abs_ours)(int) = (int (*)(int))dlsym(shim, "abs");
        long (*labs_ours[3])(long) = {(long (*)(long))dlsym(shim, "labs"), (long (*)(long))dlsym(shim, "llabs"),
                                      (long (*)(long))dlsym(shim, "imaxabs")};
        int (*ffs_c)(int) = (int (*)(int))theirs_of("ffs");
        int (*ffsl_c)(long) = (int (*)(long))theirs_of("ffsl");
        for (int round = 0; round < 70000; round++) {
                uint64_t w = round < 64 ? 1ull << round : round < 128 ? ~0ull << (round - 64) : next() >> (next() % 64);
                if (round == 200)
                        w = 0;
                checks++;
                if (ffs_ours((int)w) != ffs_c((int)w))
                        fail("ffs", "bit", (size_t)w, 0, 0);
                for (int k = 0; k < 2; k++)
                        if (ffsl_ours[k]((long)w) != ffsl_c((long)w))
                                fail(k ? "ffsll" : "ffsl", "bit", (size_t)w, 0, 0);
                if ((int)w != (int)0x80000000 && abs_ours((int)w) != abs((int)w))
                        fail("abs", "value", (size_t)w, 0, 0);
                for (int k = 0; k < 3; k++)
                        if ((long)w != (long)0x8000000000000000ull && labs_ours[k]((long)w) != labs((long)w))
                                fail(k == 0 ? "labs" : k == 1 ? "llabs" : "imaxabs", "value", (size_t)w, 0, 0);
        }
}

/*
        Each checked string copy at the room that is exactly enough (it must match the unchecked one) and one byte or one
        unit less (it must end by SIGABRT), for lengths around a page.
*/
static int ends_by_abort(void (*body)(void *, size_t), void *shim, size_t n)
{
        fflush(stdout);
        pid_t child = fork();
        if (child == 0) {
                int quiet = open("/dev/null", 1);
                dup2(quiet, 2);
                body(shim, n);
                _exit(0);
        }
        int status = 0;
        waitpid(child, &status, 0);
        return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static size_t chk_room_less;    // 1: one less than enough; 0: exactly enough
static char *chk_source;

static void chk_strcpy(void *shim, size_t n)
{
        ((char *(*)(char *, const char *, size_t))dlsym(shim, "__strcpy_chk"))((char *)dst_a, chk_source, n + 1 - chk_room_less);
}
static void chk_stpcpy(void *shim, size_t n)
{
        ((char *(*)(char *, const char *, size_t))dlsym(shim, "__stpcpy_chk"))((char *)dst_a, chk_source, n + 1 - chk_room_less);
}
static void chk_strcat(void *shim, size_t n)
{
        strcpy((char *)dst_a, "head");
        ((char *(*)(char *, const char *, size_t))dlsym(shim, "__strcat_chk"))((char *)dst_a, chk_source, 4 + n + 1 - chk_room_less);
}
static void chk_strncat(void *shim, size_t n)
{
        strcpy((char *)dst_a, "head");
        size_t k = n / 2;       // n of the source's n bytes appended
        ((char *(*)(char *, const char *, size_t, size_t))dlsym(shim, "__strncat_chk"))((char *)dst_a, chk_source, k,
                                                                                       4 + k + 1 - chk_room_less);
}
static void chk_strncpy(void *shim, size_t n)
{
        ((char *(*)(char *, const char *, size_t, size_t))dlsym(shim, "__strncpy_chk"))((char *)dst_a, chk_source, n + 9,
                                                                                       n + 9 - chk_room_less);
}
static void chk_stpncpy(void *shim, size_t n)
{
        ((char *(*)(char *, const char *, size_t, size_t))dlsym(shim, "__stpncpy_chk"))((char *)dst_a, chk_source, n + 9,
                                                                                       n + 9 - chk_room_less);
}
static void chk_strlcpy(void *shim, size_t n)
{
        ((size_t(*)(char *, const char *, size_t, size_t))dlsym(shim, "__strlcpy_chk"))((char *)dst_a, chk_source, n / 2 + 1,
                                                                                       n / 2 + 1 - chk_room_less);
}
static void chk_strlcat(void *shim, size_t n)
{
        strcpy((char *)dst_a, "head");
        ((size_t(*)(char *, const char *, size_t, size_t))dlsym(shim, "__strlcat_chk"))((char *)dst_a, chk_source, n / 2 + 6,
                                                                                       n / 2 + 6 - chk_room_less);
}
static void chk_wmemset(void *shim, size_t n)
{
        ((uint32_t * (*)(uint32_t *, uint32_t, size_t, size_t)) dlsym(shim, "__wmemset_chk"))((uint32_t *)dst_a, 7, n, n - chk_room_less);
}
static void chk_wmemcpy(void *shim, size_t n)
{
        ((uint32_t * (*)(uint32_t *, const uint32_t *, size_t, size_t)) dlsym(shim, "__wmemcpy_chk"))((uint32_t *)dst_a,
                                                                                                     (uint32_t *)src, n,
                                                                                                     n - chk_room_less);
}
static void chk_wmemmove(void *shim, size_t n)
{
        ((uint32_t * (*)(uint32_t *, const uint32_t *, size_t, size_t)) dlsym(shim, "__wmemmove_chk"))((uint32_t *)dst_a,
                                                                                                      (uint32_t *)src, n,
                                                                                                      n - chk_room_less);
}
static void chk_wmempcpy(void *shim, size_t n)
{
        ((uint32_t * (*)(uint32_t *, const uint32_t *, size_t, size_t)) dlsym(shim, "__wmempcpy_chk"))((uint32_t *)dst_a,
                                                                                                      (uint32_t *)src, n,
                                                                                                      n - chk_room_less);
}
static void chk_explicit_bzero(void *shim, size_t n)
{
        ((void (*)(void *, size_t, size_t))dlsym(shim, "__explicit_bzero_chk"))(dst_a, n, n - chk_room_less);
}

static void check_checked(void *shim)
{
        static const struct {
                const char *name;
                void (*body)(void *, size_t);
        } cases[] = {{"__strcpy_chk", chk_strcpy},   {"__stpcpy_chk", chk_stpcpy},     {"__strcat_chk", chk_strcat},
                     {"__strncat_chk", chk_strncat}, {"__strncpy_chk", chk_strncpy},   {"__stpncpy_chk", chk_stpncpy},
                     {"__strlcpy_chk", chk_strlcpy}, {"__strlcat_chk", chk_strlcat},   {"__wmemset_chk", chk_wmemset},
                     {"__wmemcpy_chk", chk_wmemcpy}, {"__wmemmove_chk", chk_wmemmove}, {"__wmempcpy_chk", chk_wmempcpy},
                     {"__explicit_bzero_chk", chk_explicit_bzero}};
        static const size_t sizes[] = {1, 2, 7, 16, 31, 64, 200, 4000};
        for (size_t c = 0; c < sizeof cases / sizeof *cases; c++)
                for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
                        size_t n = sizes[i];
                        chk_source = string_at(src2, n, 1, 3, 200);
                        chk_room_less = 1;
                        checks++;
                        if (!ends_by_abort(cases[c].body, shim, n))
                                fail(cases[c].name, "one less than enough did not end by SIGABRT", n, 0, 0);
                        chk_room_less = 0;
                        checks++;
                        if (ends_by_abort(cases[c].body, shim, n))
                                fail(cases[c].name, "exactly enough ended the program", n, 0, 0);
                }
        /* the checked copies' results, at exactly enough, against the unchecked C library's */
        for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
                size_t n = sizes[i];
                char *s = string_at(src2, n, 0, 0, 200);
                memset(dst_a, 0xa5, SPAN);
                memset(dst_b, 0xa5, SPAN);
                char *ra = ((char *(*)(char *, const char *, size_t))dlsym(shim, "__strcpy_chk"))((char *)dst_a + 5, s, n + 1);
                char *rb = strcpy((char *)dst_b + 5, s);
                if (ra - (char *)dst_a != rb - (char *)dst_b)
                        fail("__strcpy_chk", "return", n, 0, 0);
                same_bytes("__strcpy_chk", "bytes", n, 0);
                ra = ((char *(*)(char *, const char *, size_t))dlsym(shim, "__stpcpy_chk"))((char *)dst_a + 5, s, n + 1);
                rb = stpcpy((char *)dst_b + 5, s);
                if (ra - (char *)dst_a != rb - (char *)dst_b)
                        fail("__stpcpy_chk", "return", n, 0, 0);
                same_bytes("__stpcpy_chk", "bytes", n, 0);
                strcpy((char *)dst_a + 5, "head");
                strcpy((char *)dst_b + 5, "head");
                ra = ((char *(*)(char *, const char *, size_t))dlsym(shim, "__strcat_chk"))((char *)dst_a + 5, s, n + 5);
                rb = strcat((char *)dst_b + 5, s);
                if (ra - (char *)dst_a != rb - (char *)dst_b)
                        fail("__strcat_chk", "return", n, 0, 0);
                same_bytes("__strcat_chk", "bytes", n, 0);
                ra = ((char *(*)(char *, const char *, size_t, size_t))dlsym(shim, "__strncat_chk"))((char *)dst_a + 5, s, n / 2,
                                                                                                   n + 5 + n / 2);
                rb = strncat((char *)dst_b + 5, s, n / 2);
                if (ra - (char *)dst_a != rb - (char *)dst_b)
                        fail("__strncat_chk", "return", n, 0, 0);
                same_bytes("__strncat_chk", "bytes", n, 0);
                checks += 8;
        }
}

static void check_overflow(void *shim, const char *name)
{
        fflush(stdout);
        pid_t child = fork();
        if (child == 0) {
                int quiet = open("/dev/null", 1);
                dup2(quiet, 2);
                unsigned char room[32];
                if (!strcmp(name, "__memset_chk"))
                        ((void *(*)(void *, int, size_t, size_t))dlsym(shim, name))(room, 0, 33, 32);
                else
                        ((void *(*)(void *, const void *, size_t, size_t))dlsym(shim, name))(room, src, 33, 32);
                _exit(0);
        }
        int status = 0;
        waitpid(child, &status, 0);
        checks++;
        if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT)
                fail(name, "did not end by SIGABRT", (size_t)status, 0, 0);
        /* and the room that is enough */
        unsigned char room[64];
        memset(room, 1, sizeof room);
        if (!strcmp(name, "__memset_chk")) {
                void *r = ((void *(*)(void *, int, size_t, size_t))dlsym(shim, name))(room, 7, 64, 64);
                if (r != room || room[0] != 7 || room[63] != 7)
                        fail(name, "fits", 0, 0, 0);
        } else {
                void *r = ((void *(*)(void *, const void *, size_t, size_t))dlsym(shim, name))(room, src, 64, 64);
                void *want = strcmp(name, "__mempcpy_chk") ? (void *)room : (void *)(room + 64);
                if (r != want || memcmp(room, src, 64))
                        fail(name, "fits", 0, 0, 0);
        }
}

static inline uint64_t ticks(void)
{
        unsigned lo, hi;
        __asm__ volatile("lfence\n rdtsc\n lfence" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
}

static void *volatile sink;

static double time_copy(copy_fn f, size_t n, size_t soff, size_t doff)
{
        uint64_t best = ~0ull;
        for (int round = 0; round < 64; round++) {
                uint64_t t0 = ticks();
                for (int i = 0; i < 4096; i++)
                        sink = f(dst_a + doff, src + soff, n);
                uint64_t t = ticks() - t0;
                if (t < best)
                        best = t;
        }
        return (double)best / 4096;
}

static double time_fill(fill_fn f, size_t n)
{
        uint64_t best = ~0ull;
        for (int round = 0; round < 64; round++) {
                uint64_t t0 = ticks();
                for (int i = 0; i < 4096; i++)
                        sink = f(dst_a + 3, 0, n);
                uint64_t t = ticks() - t0;
                if (t < best)
                        best = t;
        }
        return (double)best / 4096;
}

static double time_compare(cmp_fn f, size_t n)
{
        uint64_t best = ~0ull;
        volatile int r;
        for (int round = 0; round < 64; round++) {
                uint64_t t0 = ticks();
                for (int i = 0; i < 4096; i++)
                        r = f(src + 5, src2 + 5, n);
                uint64_t t = ticks() - t0;
                if (t < best)
                        best = t;
        }
        (void)r;
        return (double)best / 4096;
}

static double time_length(len_fn f, const char *s)
{
        uint64_t best = ~0ull;
        volatile size_t r;
        for (int round = 0; round < 64; round++) {
                uint64_t t0 = ticks();
                for (int i = 0; i < 4096; i++)
                        r = f(s);
                uint64_t t = ticks() - t0;
                if (t < best)
                        best = t;
        }
        (void)r;
        return (double)best / 4096;
}

static double time_find(find_fn f, size_t n)
{
        uint64_t best = ~0ull;
        for (int round = 0; round < 64; round++) {
                uint64_t t0 = ticks();
                for (int i = 0; i < 4096; i++)
                        sink = f(src + 1, 0xee, n);
                uint64_t t = ticks() - t0;
                if (t < best)
                        best = t;
        }
        return (double)best / 4096;
}

/*
        copy: the copies and fills (memcpy, memmove both ways over itself, memset, bzero, mempcpy, wmemset, wmemcpy), ticks a
        call, the C library's against every shim named after "copy" (this tree's, a baseline's, builds capped to a lower tier),
        each side timed in turn at every row and the row taken twice, so a drift of the machine reaches every column alike.

        fixed: one length a row, 1 B to 64 MiB, the same call over and over, so every branch in a body is learned.
        o<N>: the length drawn at every call from the octave [N, 2N), uniform, so the rung is known and the length is not.
        r512, r8192: the length drawn log-uniform from 1 to 512 and from 1 to 8192, so no rung can be learned.
        The lengths are drawn once (4M of them, the same on every run) and read in order from an array by a cursor that
        never goes back, so the work between calls is a load and an increment, and no side meets a stretch of lengths a
        side before it trained the predictor on: a pool of 65536 read in overlapping windows was learned, and the side
        timed first in a pass took 13.5 ticks a call at r512 where the same bytes timed later took 20.

        Where the bytes sit: the source one byte into a page and the destination seven bytes and half a page further on
        (apart), the same with the destination seven bytes after the source modulo a page (near, the bench rows' placing,
        which glibc copies backwards for 4K aliasing), the source 37 bytes below the destination in one buffer (up, the copy
        that has to run backwards when it overlaps) and 37 above it (down). The fills start three bytes into a page.
        Ticks are rdtsc's, the best of the rounds, the sides taking turns a round at a time, nothing subtracted: every side
        pays the same loop around the call. The core clock is reported against them (a chain of dependent adds), since
        ticks are the TSC's and not the core's.
*/
enum { CB_COPY, CB_NEAR, CB_UP, CB_DOWN, CB_SET, CB_ZERO, CB_PCPY, CB_WSET, CB_WCPY };
static const struct copy_case {
        const char *name, *symbol;
        int shape;
} copy_cases[] = {
        {"memcpy", "memcpy", CB_COPY},       {"memcpy-near", "memcpy", CB_NEAR}, {"memmove-up", "memmove", CB_UP},
        {"memmove-down", "memmove", CB_DOWN}, {"memset", "memset", CB_SET},      {"bzero", "bzero", CB_ZERO},
        {"mempcpy", "mempcpy", CB_PCPY},     {"wmemset", "wmemset", CB_WSET},    {"wmemcpy", "wmemcpy", CB_WCPY},
};
#define COPY_ORDER (1u << 22)   // more lengths than a predictor can learn, used once each before they come round again
#define COPY_BIG ((size_t)64 << 20)
static uint32_t *copy_len;
static unsigned copy_cursor;
static unsigned char *copy_src, *copy_dst;

/* How many calls a round, and how many rounds, so that a round is long enough to time and the row short enough to run. */
static void copy_budget(size_t n, int *calls, int *rounds)
{
        size_t c = n <= 512 ? 4096 : (size_t)4096 * 512 / n;
        *calls = c < 2 ? 2 : (int)c;
        *rounds = n <= ((size_t)1 << 20) ? 64 : n <= ((size_t)8 << 20) ? 16 : 6;
}

/* One round of calls on one side: ticks for all of them. */
static uint64_t copy_round(const struct copy_case *c, void *f, int calls)
{
        unsigned char *s = copy_src + 1, *d = copy_dst + 7 + PAGE / 2;
        switch (c->shape) {
        case CB_NEAR: d = copy_dst + 8; break;
        case CB_UP: s = copy_src + 64, d = s + 37; break;
        case CB_DOWN: d = copy_src + 64, s = d + 37; break;
        case CB_SET: case CB_ZERO: d = copy_dst + 3; break;
        case CB_WSET: case CB_WCPY: s = copy_src + 4, d = copy_dst + 8 + PAGE / 2; break;
        }
        unsigned k = copy_cursor;
        copy_cursor += (unsigned)calls;
        uint64_t t0 = ticks();
        switch (c->shape) {
        case CB_SET:
                for (int i = 0; i < calls; i++)
                        sink = ((fill_fn)f)(d, 0x5c, copy_len[k++ % COPY_ORDER]);
                break;
        case CB_ZERO:
                for (int i = 0; i < calls; i++) {
                        ((zero_fn)f)(d, copy_len[k++ % COPY_ORDER]);
                        sink = d;
                }
                break;
        case CB_WSET:
                for (int i = 0; i < calls; i++)
                        sink = ((wset_fn)f)((uint32_t *)d, 0x5c5c5c5c, (copy_len[k++ % COPY_ORDER] + 3) / 4);
                break;
        case CB_WCPY:
                for (int i = 0; i < calls; i++)
                        sink = ((wcpy_fn)f)((uint32_t *)d, (const uint32_t *)s, (copy_len[k++ % COPY_ORDER] + 3) / 4);
                break;
        default:
                for (int i = 0; i < calls; i++)
                        sink = ((copy_fn)f)(d, s, copy_len[k++ % COPY_ORDER]);
                break;
        }
        return ticks() - t0;
}

/* The best round of a side, the sides taking turns a round at a time, so whatever the machine does meanwhile (its clock,
   a neighbour on the other thread of the core) reaches every column alike: taking each side's rounds in one run let a
   burst of load land on one column (glibc's own body read 0.57 to 0.89 of itself from one run to the next). */
static void copy_times(const struct copy_case *c, void **sides, int count, int calls, int rounds, double *t)
{
        uint64_t best[16];
        for (int s = 0; s < count; s++)
                best[s] = ~0ull;
        for (int round = 0; round < rounds; round++)
                for (int i = 0; i < count; i++) {
                        int s = (i + round) % count;    // who goes first turns too
                        uint64_t v = copy_round(c, sides[s], calls);
                        if (v < best[s])
                                best[s] = v;
                }
        for (int s = 0; s < count; s++)
                t[s] = (double)best[s] / calls;
}

static void copy_row(const struct copy_case *c, void **sides, int count, const char *mode, size_t size)
{
        double t[16];
        int calls, rounds;
        size_t total = 0;
        for (unsigned i = 0; i < COPY_ORDER; i++)
                total += copy_len[i];
        copy_budget(total / COPY_ORDER, &calls, &rounds);       // the mean length's
        copy_times(c, sides, count, calls, 2 * rounds, t);
        printf("%-13s %-6s %9zu %9.1f", c->name, mode, size, t[0]);
        for (int s = 1; s < count; s++)
                printf(" %9.1f %5.2f", t[s], t[0] / t[s]);
        printf("\n");
}

/* log2 and 2^x to a part in ten thousand, which is all a draw of lengths needs (and no libm to link). */
static double copy_log2(double v)
{
        double e = 0;
        while (v >= 2)
                v /= 2, e++;
        double z = (v - 1) / (v + 1), z2 = z * z;      // ln v = 2 atanh z
        return e + 2 * z * (1 + z2 / 3 + z2 * z2 / 5 + z2 * z2 * z2 / 7) / 0.6931471805599453;
}

static double copy_exp2(double x)
{
        double whole = 1;
        while (x >= 1)
                x -= 1, whole *= 2;
        return whole * (1 + x * (0.6931471805599453 + x * (0.2402265069591007 + x * (0.0555041086648216 + x * 0.0096181291076285))));
}

/* Core cycles a tick: a chain of 4 dependent adds a turn is 4 cycles a turn on anything this runs on. */
static double copy_clock(void)
{
        double best = 1e30;
        for (int r = 0; r < 16; r++) {
                uint64_t t0 = ticks();
                __asm__ volatile("mov $250000, %%ecx\n 1: add $1, %%rax\n add $1, %%rax\n add $1, %%rax\n add $1, %%rax\n dec %%ecx\n jnz 1b"
                                 ::: "rax", "rcx", "cc");
                double v = (double)(ticks() - t0);
                if (v < best)
                        best = v;
        }
        return 1000000.0 / best;
}

static void bench_copy(void **shims, int shim_count, const char *only)
{
        static const size_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 12, 15, 16, 17, 24, 31, 32, 33, 48, 63, 64, 65, 80, 96, 112, 127, 128,
                                       129, 160, 192, 224, 255, 256, 257, 320, 384, 448, 511, 512, 513, 640, 768, 1024, 1536,
                                       2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 65536, 131072, 262144, 524288,
                                       (size_t)1 << 20, (size_t)2 << 20, (size_t)4 << 20, (size_t)8 << 20, (size_t)16 << 20,
                                       (size_t)24 << 20, (size_t)32 << 20, (size_t)64 << 20};
        size_t room = 2 * (COPY_BIG + 2 * PAGE);
        unsigned char *all = mmap(0, room, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (all == MAP_FAILED) {
                perror("mmap");
                exit(2);
        }
        copy_src = all, copy_dst = all + COPY_BIG + 2 * PAGE;
        fill_random(copy_src, COPY_BIG + 2 * PAGE, 0);
        copy_len = mmap(0, COPY_ORDER * sizeof *copy_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(copy_dst, 1, COPY_BIG + 2 * PAGE);
        size_t largest = (size_t)-1;
        const char *cap = getenv("LIBC_SHIM_COPY_MAX");
        if (cap)
                largest = (size_t)strtoull(cap, 0, 0);
        printf("core cycles a tick %.3f; columns: the C library's ticks a call, then each shim's and the C library's over it (above 1.00 the shim wins)\n", copy_clock());
        for (size_t ci = 0; ci < sizeof copy_cases / sizeof *copy_cases; ci++) {
                const struct copy_case *c = &copy_cases[ci];
                if (only && strcmp(only, c->name) && strcmp(only, c->symbol))
                        continue;
                void *sides[16];
                sides[0] = dlsym(RTLD_DEFAULT, c->symbol);
                for (int s = 0; s < shim_count; s++)
                        sides[s + 1] = dlsym(shims[s], c->symbol);
                for (size_t si = 0; si < sizeof sizes / sizeof *sizes; si++) {
                        size_t n = sizes[si];
                        if (n > largest || ((c->shape == CB_UP || c->shape == CB_DOWN) && n > COPY_BIG - 256))
                                continue;
                        if (si == 0) {  // a case's first row is timed twice and the first kept from no one: whichever
                                        // side went first there came out a quarter quicker, at every size
                                for (unsigned i = 0; i < COPY_ORDER; i++)
                                        copy_len[i] = (uint32_t)(n + 7);
                                double t[16];
                                copy_times(c, sides, shim_count + 1, 4096, 16, t);
                        }
                        for (unsigned i = 0; i < COPY_ORDER; i++)
                                copy_len[i] = (uint32_t)n;
                        copy_row(c, sides, shim_count + 1, "fixed", n);
                }
                uint64_t keep = seed;
                for (size_t lo = 1; lo <= 16384 && lo <= largest; lo *= 2) {
                        seed = 0x2545f4914f6cdd1dull + lo;      // the same lengths on every side and every run
                        for (unsigned i = 0; i < COPY_ORDER; i++)
                                copy_len[i] = (uint32_t)(lo + next() % lo);
                        char mode[16];
                        snprintf(mode, sizeof mode, "o%zu", lo);
                        copy_row(c, sides, shim_count + 1, mode, 2 * lo - 1);
                }
                static const size_t ranges[] = {512, 8192};
                for (int r = 0; r < 2; r++) {
                        seed = 0x9e3779b97f4a7c15ull + ranges[r];
                        double bits = copy_log2((double)ranges[r] + 1);
                        for (unsigned i = 0; i < COPY_ORDER; i++) {
                                size_t n = (size_t)copy_exp2((double)(next() >> 11) / 9007199254740992.0 * bits);
                                copy_len[i] = (uint32_t)(n < 1 ? 1 : n > ranges[r] ? ranges[r] : n);
                        }
                        char mode[16];
                        snprintf(mode, sizeof mode, "r%zu", ranges[r]);
                        copy_row(c, sides, shim_count + 1, mode, ranges[r]);
                }
                seed = keep;
        }
        munmap(all, room);
}

static void bench(void *shim)
{
        static const size_t sizes[] = {1, 3, 8, 15, 16, 24, 32, 48, 64, 96, 128, 200, 256, 512, 1024, 4096, 16384, 32768};
        copy_fn cpy = (copy_fn)dlsym(shim, "memcpy"), mov = (copy_fn)dlsym(shim, "memmove");
        fill_fn set = (fill_fn)dlsym(shim, "memset");
        cmp_fn cmp = (cmp_fn)dlsym(shim, "memcmp");
        len_fn len = (len_fn)dlsym(shim, "strlen");
        find_fn chr = (find_fn)dlsym(shim, "memchr");
        copy_fn cpy_c = memcpy, mov_c = memmove;
        fill_fn set_c = memset;
        cmp_fn cmp_c = memcmp;
        len_fn len_c = strlen;
        find_fn chr_c = (find_fn)memchr;
        printf("ticks a call, shim/libc (lower is better; ratio = libc/shim)\n");
        printf("%6s %17s %17s %17s %17s %17s %17s\n", "size", "memcpy", "memmove", "memset", "memcmp(equal)", "strlen", "memchr(absent)");
        memset(src, 'x', SPAN);
        memset(src2, 'x', SPAN);
        fill_random(src, SPAN, 200);
        memcpy(src2, src, SPAN);
        for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
                size_t n = sizes[i];
                double a = time_copy(cpy, n, 1, 7), b = time_copy(cpy_c, n, 1, 7);
                double c = time_copy(mov, n, 9, 3), d = time_copy(mov_c, n, 9, 3);
                double e = time_fill(set, n), f = time_fill(set_c, n);
                double g = time_compare(cmp, n), h = time_compare(cmp_c, n);
                char saved = (char)src[2 + n];
                src[2 + n] = 0;
                double k = time_length(len, (const char *)src + 2), l = time_length(len_c, (const char *)src + 2);
                src[2 + n] = (unsigned char)saved;
                double m = time_find(chr, n), o = time_find(chr_c, n);
                printf("%6zu %6.1f/%5.1f %4.2f %6.1f/%5.1f %4.2f %6.1f/%5.1f %4.2f %6.1f/%5.1f %4.2f %6.1f/%5.1f %4.2f %6.1f/%5.1f %4.2f\n", n,
                       a, b, b / a, c, d, d / c, e, f, f / e, g, h, h / g, k, l, l / k, m, o, o / m);
        }
}

/*
        bench compare [OLD.so|-] [NAME...]: the compare, copy and search family -- memcmp, strcmp, strncmp, strcpy,
        stpcpy, stpncpy, strcat, strstr, memmem -- in ticks a call, the best of 32 rounds. Fixed lengths from 1 B to
        64 KiB, with the difference or the match at the start, the middle and the end where a routine has one; then
        lengths drawn log-uniform over 1-512 and over 1-8192 (an octave at random, then a length in it), in an order the
        branch predictor cannot learn: 65536 lengths, read from where the last round stopped. Every string of n bytes ends
        at the same place, so its start moves with n and so does its alignment, as a program's do. OLD.so, a second build
        of the shim, is timed in the same rounds, interleaved, so a change is read against what it replaced on the same
        core in the same minute. NAME limits the run to the routines named. Each case is called once on every side first
        and the answers held against each other: a bench that times different work says so.
*/
#define BC_END (80 * 1024ul + 1347)             // where every string ends: mid-page, off any line
#define BC_SPAN (BC_END + 72 * 1024ul)
#define BC_SLOT (136 * 1024ul + 5 * 64 + 8)     // strcat's destinations: eight, so no call reads the byte the last wrote, at eight offsets in their pages, so no load waits on the store before it to another (4K aliasing)
#define BC_SLOT_END (66 * 1024ul)
#define BC_ROUNDS 32
#define BC_POOL 65536u

static unsigned char *bc_a, *bc_b, *bc_c, *bc_e, *bc_t, *bc_d, *bc_slots;
static char bc_needle[64];
static size_t bc_pool[2][BC_POOL];
static double bc_best[3];
static int bc_sides;
static const char *const bc_side_name[3] = {"new", "old", "libc"};

static unsigned char *bc_region(size_t bytes)
{
        unsigned char *p = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
                perror("mmap");
                exit(2);
        }
        return p;
}

/* calls a round: enough work to time, few enough that 64 KiB finishes */
static unsigned bc_calls(size_t n) { return n <= 256 ? 1024 : 256; }

/* Times CALL (which sees the side's pointer as f and the length as n) on every side, interleaved round by round.
   N is evaluated each call; base is where in the pool this round starts. */
#define BC_RUN(fns, calls_, N, CALL)                                                                    \
        do {                                                                                            \
                unsigned calls = (calls_);                                                              \
                for (int s_ = 0; s_ < bc_sides; s_++)                                                   \
                        bc_best[s_] = 1e30;                                                             \
                for (unsigned r_ = 0; r_ < BC_ROUNDS; r_++)                                             \
                        for (int t_ = 0; t_ < bc_sides; t_++) {                                         \
                                int s_ = (int)((t_ + r_) % (unsigned)bc_sides); /* each side first in turn */ \
                                __typeof__((fns)[0]) f = (fns)[s_];                                     \
                                unsigned base = r_ * calls;                                             \
                                uintptr_t acc_ = 0;                                                     \
                                uint64_t t0_ = ticks();                                                 \
                                for (unsigned i = 0; i < calls; i++) {                                  \
                                        size_t n = (N);                                                 \
                                        (void)n;                                                        \
                                        acc_ += (uintptr_t)(CALL);                                      \
                                }                                                                       \
                                uint64_t t_ = ticks() - t0_;                                            \
                                __asm__ volatile("" ::"r"(acc_));                                       \
                                (void)base;                                                             \
                                if ((double)t_ / calls < bc_best[s_])                                   \
                                        bc_best[s_] = (double)t_ / calls;                               \
                        }                                                                               \
        } while (0)

static void bc_head(const char *what)
{
        printf("\n%s\n%8s", what, "size");
        for (int s = 0; s < bc_sides; s++)
                printf(" %8s", bc_side_name[s]);
        printf("  %8s", "libc/new");
        if (bc_sides == 3)
                printf(" %8s %8s", "libc/old", "old/new");
        printf("\n");
}

static void bc_row(const char *size)
{
        double libc = bc_best[bc_sides - 1];
        printf("%8s", size);
        for (int s = 0; s < bc_sides; s++)
                printf(" %8.1f", bc_best[s]);
        printf("  %8.2f", libc / bc_best[0]);
        if (bc_sides == 3)
                printf(" %8.2f %8.2f", libc / bc_best[1], bc_best[1] / bc_best[0]);
        printf("%s\n", libc / bc_best[0] < 0.995 ? "  <" : "");
}

static void bc_row_n(size_t n)
{
        char room[16];
        snprintf(room, sizeof room, "%zu", n);
        bc_row(room);
}

static void bc_disagree(const char *name, const char *what, size_t n)
{
        printf("  BENCH DISAGREES: %s %s at %zu\n", name, what, n);
}

static const size_t bc_sizes[] = {1,   2,   3,   4,   7,   8,    12,   16,   24,   31,    32,    33,    48,   63,  64,
                                  65,  96,  128, 200, 256, 384,  512,  1024, 2048, 4096,  8192,  16384, 32768, 65536};
#define BC_SIZES (sizeof bc_sizes / sizeof *bc_sizes)
#define BC_RANDOM(k) (bc_pool[k][(base + i) & (BC_POOL - 1)])

static int bc_wanted(int argc, char **argv, const char *name)
{
        if (argc < 1)
                return 1;
        for (int i = 0; i < argc; i++)
                if (!strcmp(argv[i], name))
                        return 1;
        return 0;
}

/* where a fixed-size case puts its one difference: start, middle, end; -1 none */
static long bc_at(int where, size_t n) { return where == 0 ? -1 : where == 1 ? 0 : where == 2 ? (long)(n / 2) : (long)n - 1; }
static const char *const bc_where[4] = {"equal", "differ at the start", "differ in the middle", "differ at the end"};

static void bc_memcmp(cmp_fn *fns)
{
        for (int where = 0; where < 4; where++) {
                char title[64];
                snprintf(title, sizeof title, "memcmp, %s", bc_where[where]);
                bc_head(title);
                for (size_t k = 0; k < BC_SIZES; k++) {
                        size_t n0 = bc_sizes[k];
                        unsigned char *a = bc_a + BC_END - n0, *b = bc_b + BC_END - n0;
                        long at = bc_at(where, n0);
                        if (at >= 0)
                                b[at] ^= 0x40;
                        int want = sign(fns[bc_sides - 1](a, b, n0));
                        for (int s = 0; s < bc_sides - 1; s++)
                                if (sign(fns[s](a, b, n0)) != want)
                                        bc_disagree("memcmp", bc_side_name[s], n0);
                        BC_RUN(fns, bc_calls(n0), n0, f(a, b, n));
                        if (at >= 0)
                                b[at] ^= 0x40;
                        bc_row_n(n0);
                }
        }
        static const char *const mode[2] = {"r512", "r8192"};
        /* the slow way some bodies take: an input whose last byte is the last of its page (the next one mapped) */
        bc_head("memcmp, equal, both ending where a page ends");
        size_t edge = BC_END - 1347 + 4096;
        for (size_t k = 0; k < BC_SIZES && bc_sizes[k] <= 256; k++) {
                size_t n0 = bc_sizes[k];
                BC_RUN(fns, bc_calls(n0), n0, f(bc_a + edge - n, bc_e + edge - n, n));
                bc_row_n(n0);
        }
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(bc_a + edge - n, bc_e + edge - n, n));
                bc_row(mode[k]);
        }
        bc_head("memcmp, random lengths: equal (start moves with n)");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(bc_a + BC_END - n, bc_b + BC_END - n, n));
                bc_row(mode[k]);
        }
        bc_head("memcmp, random lengths: equal (start fixed at +5)");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(bc_a + 5, bc_b + 5, n));
                bc_row(mode[k]);
        }
        bc_head("memcmp, random lengths: differ at the end");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(bc_a + BC_END - n, bc_c + BC_END - n, n));
                bc_row(mode[k]);
        }
}

static void bc_strcmp(scmp_fn *fns)
{
        for (int where = 0; where < 4; where++) {
                char title[64];
                snprintf(title, sizeof title, "strcmp, %s", bc_where[where]);
                bc_head(title);
                for (size_t k = 0; k < BC_SIZES; k++) {
                        size_t n0 = bc_sizes[k];
                        const char *a = (char *)bc_a + BC_END - n0;
                        char *b = (char *)bc_b + BC_END - n0;
                        long at = bc_at(where, n0);
                        if (at >= 0)
                                b[at] ^= 0x40;
                        int want = sign(fns[bc_sides - 1](a, b));
                        for (int s = 0; s < bc_sides - 1; s++)
                                if (sign(fns[s](a, b)) != want)
                                        bc_disagree("strcmp", bc_side_name[s], n0);
                        BC_RUN(fns, bc_calls(n0), n0, f(a, b));
                        if (at >= 0)
                                b[at] ^= 0x40;
                        bc_row_n(n0);
                }
        }
        static const char *const mode[2] = {"r512", "r8192"};
        bc_head("strcmp, equal, both ending where a page ends");
        size_t edge = BC_END - 1347 + 4096;
        bc_a[edge] = 0, bc_e[edge] = 0;
        for (size_t k = 0; k < BC_SIZES && bc_sizes[k] <= 256; k++) {
                size_t n0 = bc_sizes[k];
                if (fns[0]((char *)bc_a + edge - n0, (char *)bc_e + edge - n0))
                        bc_disagree("strcmp", "page end", n0);
                BC_RUN(fns, bc_calls(n0), n0, f((char *)bc_a + edge - n, (char *)bc_e + edge - n));
                bc_row_n(n0);
        }
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f((char *)bc_a + edge - n, (char *)bc_e + edge - n));
                bc_row(mode[k]);
        }
        bc_a[edge] = bc_e[edge] = 1;
        bc_head("strcmp, random lengths: equal");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f((char *)bc_a + BC_END - n, (char *)bc_b + BC_END - n));
                bc_row(mode[k]);
        }
        bc_head("strcmp, random lengths: differ at the end");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f((char *)bc_a + BC_END - n, (char *)bc_c + BC_END - n));
                bc_row(mode[k]);
        }
}

static void bc_strncmp(sncmp_fn *fns)
{
        for (int where = 0; where < 4; where++) {
                char title[80];
                snprintf(title, sizeof title, "strncmp, bound n, strings of n, %s", bc_where[where]);
                bc_head(title);
                for (size_t k = 0; k < BC_SIZES; k++) {
                        size_t n0 = bc_sizes[k];
                        const char *a = (char *)bc_a + BC_END - n0;
                        char *b = (char *)bc_b + BC_END - n0;
                        long at = bc_at(where, n0);
                        if (at >= 0)
                                b[at] ^= 0x40;
                        int want = sign(fns[bc_sides - 1](a, b, n0));
                        for (int s = 0; s < bc_sides - 1; s++)
                                if (sign(fns[s](a, b, n0)) != want)
                                        bc_disagree("strncmp", bc_side_name[s], n0);
                        BC_RUN(fns, bc_calls(n0), n0, f(a, b, n));
                        if (at >= 0)
                                b[at] ^= 0x40;
                        bc_row_n(n0);
                }
        }
        bc_head("strncmp, bound 2n + 1, strings of n, equal (the terminator decides)");
        for (size_t k = 0; k < BC_SIZES; k++) {
                size_t n0 = bc_sizes[k];
                const char *a = (char *)bc_a + BC_END - n0, *b = (char *)bc_b + BC_END - n0;
                BC_RUN(fns, bc_calls(n0), 2 * n0 + 1, f(a, b, n));
                bc_row_n(n0);
        }
        static const char *const mode[2] = {"r512", "r8192"};
        bc_head("strncmp, random lengths: bound n, equal");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f((char *)bc_a + BC_END - n, (char *)bc_b + BC_END - n, n));
                bc_row(mode[k]);
        }
        bc_head("strncmp, random lengths: bound n, differ at the end");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f((char *)bc_a + BC_END - n, (char *)bc_c + BC_END - n, n));
                bc_row(mode[k]);
        }
}

/* strcpy and stpcpy: a string of n into a destination 3 bytes past a line */
static void bc_strcpy(const char *name, scpy_fn *fns)
{
        char title[64];
        snprintf(title, sizeof title, "%s, a string of n", name);
        bc_head(title);
        char *d = (char *)bc_d + 3;
        for (size_t k = 0; k < BC_SIZES; k++) {
                size_t n0 = bc_sizes[k];
                const char *s = (char *)bc_a + BC_END - n0;
                char *want = 0;
                for (int side = bc_sides - 1; side >= 0; side--) {
                        memset(d, 0x5a, n0 + 2);
                        char *r = fns[side](d, s);
                        if (side == bc_sides - 1)
                                want = r;
                        else if (r != want)
                                bc_disagree(name, bc_side_name[side], n0);
                        if (memcmp(d, s, n0 + 1) || d[n0 + 1] != 0x5a)
                                bc_disagree(name, bc_side_name[side], n0);
                }
                BC_RUN(fns, bc_calls(n0), n0, f(d, (char *)bc_a + BC_END - n));
                bc_row_n(n0);
        }
        static const char *const mode[2] = {"r512", "r8192"};
        snprintf(title, sizeof title, "%s, random lengths", name);
        bc_head(title);
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(d, (char *)bc_a + BC_END - n));
                bc_row(mode[k]);
        }
}

static void bc_stpncpy(sncpy_fn *fns)
{
        char *d = (char *)bc_d + 3;
        for (int pad = 0; pad < 2; pad++) {
                bc_head(pad ? "stpncpy, bound n, a string of n/2 (half padding)" : "stpncpy, bound n, a string of n (no terminator)");
                for (size_t k = 0; k < BC_SIZES; k++) {
                        size_t n0 = bc_sizes[k], len = pad ? n0 / 2 : n0;
                        const char *s = (char *)bc_a + BC_END - len;
                        char *want = 0;
                        for (int side = bc_sides - 1; side >= 0; side--) {
                                memset(d, 0x5a, n0 + 2);
                                char *r = fns[side](d, s, n0);
                                if (side == bc_sides - 1)
                                        want = r;
                                else if (r != want)
                                        bc_disagree("stpncpy", bc_side_name[side], n0);
                                if (memcmp(d, s, len) || d[n0] != 0x5a)
                                        bc_disagree("stpncpy bytes", bc_side_name[side], n0);
                        }
                        BC_RUN(fns, bc_calls(n0), n0, f(d, (char *)bc_a + BC_END - (pad ? n / 2 : n), n));
                        bc_row_n(n0);
                }
        }
        static const char *const mode[2] = {"r512", "r8192"};
        bc_head("stpncpy, random lengths: bound n, a string of n");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(d, (char *)bc_a + BC_END - n, n));
                bc_row(mode[k]);
        }
        bc_head("stpncpy, random lengths: bound n, a string of n/2");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(d, (char *)bc_a + BC_END - n / 2, n));
                bc_row(mode[k]);
        }
}

/* strcat: a destination string of n and a source of n; the destination's terminator put back after each call, in
   one of eight slots so no call reads a byte the one before it wrote */
static char *bc_cat(scpy_fn f, size_t n, unsigned i)
{
        char *slot = (char *)bc_slots + (i & 7) * BC_SLOT;
        char *r = f(slot + BC_SLOT_END - n, (char *)bc_a + BC_END - n);
        slot[BC_SLOT_END] = 0;
        return r;
}

static void bc_strcat(scpy_fn *fns)
{
        bc_head("strcat, a string of n onto a string of n");
        for (size_t k = 0; k < BC_SIZES; k++) {
                size_t n0 = bc_sizes[k];
                for (int side = 0; side < bc_sides; side++) {
                        char *slot = (char *)bc_slots;
                        char *r = fns[side](slot + BC_SLOT_END - n0, (char *)bc_a + BC_END - n0);
                        if (r != slot + BC_SLOT_END - n0 || memcmp(slot + BC_SLOT_END, bc_a + BC_END - n0, n0 + 1))
                                bc_disagree("strcat", bc_side_name[side], n0);
                        slot[BC_SLOT_END] = 0;
                }
                BC_RUN(fns, bc_calls(n0), n0, bc_cat(f, n, i));
                bc_row_n(n0);
        }
        static const char *const mode[2] = {"r512", "r8192"};
        bc_head("strcat, random lengths");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), bc_cat(f, n, i));
                bc_row(mode[k]);
        }
}

/* strstr and memmem: a haystack of n letters, a needle of 8 letters absent from it, then planted at the start, in the
   middle and at the end */
static const char *const bc_find_where[4] = {"absent", "at the start", "in the middle", "at the end"};
#define BC_NEEDLE 8

static void bc_plant(size_t n, int where, int undo)
{
        static unsigned char keep[BC_NEEDLE];
        if (where == 0 || n < BC_NEEDLE)
                return;
        unsigned char *h = bc_t + BC_END - n;
        size_t at = where == 1 ? 0 : where == 2 ? (n - BC_NEEDLE) / 2 : n - BC_NEEDLE;
        if (undo)
                memcpy(h + at, keep, BC_NEEDLE);
        else
                memcpy(keep, h + at, BC_NEEDLE), memcpy(h + at, bc_needle, BC_NEEDLE);
}

static void bc_strstr(sstr_fn *fns)
{
        for (int where = 0; where < 4; where++) {
                char title[80];
                snprintf(title, sizeof title, "strstr, a haystack of n letters, a needle of %d, %s", BC_NEEDLE, bc_find_where[where]);
                bc_head(title);
                for (size_t k = 0; k < BC_SIZES; k++) {
                        size_t n0 = bc_sizes[k];
                        if (where && n0 < BC_NEEDLE)
                                continue;
                        bc_plant(n0, where, 0);
                        const char *h = (char *)bc_t + BC_END - n0;
                        char *want = fns[bc_sides - 1](h, bc_needle);
                        for (int s = 0; s < bc_sides - 1; s++)
                                if (fns[s](h, bc_needle) != want)
                                        bc_disagree("strstr", bc_side_name[s], n0);
                        BC_RUN(fns, bc_calls(n0), n0, f((char *)bc_t + BC_END - n, bc_needle));
                        bc_plant(n0, where, 1);
                        bc_row_n(n0);
                }
        }
        static const char *const mode[2] = {"r512", "r8192"};
        bc_head("strstr, random lengths: absent");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f((char *)bc_t + BC_END - n, bc_needle));
                bc_row(mode[k]);
        }
}

static void bc_memmem(mem_fn *fns)
{
        for (int where = 0; where < 4; where++) {
                char title[80];
                snprintf(title, sizeof title, "memmem, a haystack of n letters, a needle of %d, %s", BC_NEEDLE, bc_find_where[where]);
                bc_head(title);
                for (size_t k = 0; k < BC_SIZES; k++) {
                        size_t n0 = bc_sizes[k];
                        if (where && n0 < BC_NEEDLE)
                                continue;
                        bc_plant(n0, where, 0);
                        const void *h = bc_t + BC_END - n0;
                        void *want = fns[bc_sides - 1](h, n0, bc_needle, BC_NEEDLE);
                        for (int s = 0; s < bc_sides - 1; s++)
                                if (fns[s](h, n0, bc_needle, BC_NEEDLE) != want)
                                        bc_disagree("memmem", bc_side_name[s], n0);
                        BC_RUN(fns, bc_calls(n0), n0, f(bc_t + BC_END - n, n, bc_needle, BC_NEEDLE));
                        bc_plant(n0, where, 1);
                        bc_row_n(n0);
                }
        }
        static const char *const mode[2] = {"r512", "r8192"};
        bc_head("memmem, random lengths: absent");
        for (int k = 0; k < 2; k++) {
                BC_RUN(fns, 1024, BC_RANDOM(k), f(bc_t + BC_END - n, n, bc_needle, BC_NEEDLE));
                bc_row(mode[k]);
        }
}

/* The floor under every row: the same loop around a call that does nothing. */
__asm__(".text\n.globl bc_null\n.type bc_null, @function\nbc_null: xor %eax, %eax\n ret\n.size bc_null, .-bc_null\n");
int bc_null(const void *, const void *, size_t);

static int bench_compare(void *shim, int argc, char **argv)
{
        void *old = 0;
        if (argc > 0 && strcmp(argv[0], "-")) {
                old = dlopen(argv[0], RTLD_NOW | RTLD_LOCAL);
                if (!old || old == shim) {
                        fprintf(stderr, "dlopen %s: %s\n", argv[0], old ? "the same library as the new one" : dlerror());
                        return 2;
                }
        }
        if (argc > 0)
                argc--, argv++;
        bc_sides = old ? 3 : 2;
        void *libs[2] = {shim, old};
        /* b and c sit 7 and 13 bytes on from a line where a sits on one, so no two streams share an alignment */
        bc_a = bc_region(BC_SPAN), bc_b = bc_region(BC_SPAN) + 7, bc_c = bc_region(BC_SPAN) + 13, bc_t = bc_region(BC_SPAN);
        bc_d = bc_region(BC_SPAN), bc_slots = bc_region(8 * BC_SLOT);
        /* a, b and c the same bytes (1 to 200) but c's last before the end; every string ends at BC_END */
        fill_random(bc_a, BC_SPAN - 64, 200);
        memcpy(bc_b, bc_a, BC_SPAN - 64);
        memcpy(bc_c, bc_a, BC_SPAN - 64);
        bc_e = bc_region(BC_SPAN);      // a again, on a's alignment: the inputs that end where a page does
        memcpy(bc_e, bc_a, BC_SPAN);
        bc_a[BC_END] = bc_b[BC_END] = bc_c[BC_END] = 0;
        bc_c[BC_END - 1] ^= 0x40;
        for (size_t i = 0; i < BC_SPAN; i++)
                bc_t[i] = (unsigned char)('a' + next() % 26);
        bc_t[BC_END] = 0;
        for (int s = 0; s < 8; s++) {
                fill_random(bc_slots + s * BC_SLOT, BC_SLOT, 200);
                bc_slots[s * BC_SLOT + BC_SLOT_END] = 0;
        }
        /* a needle of letters that is nowhere in the haystack */
        for (;;) {
                for (int i = 0; i < BC_NEEDLE; i++)
                        bc_needle[i] = (char)('a' + next() % 26);
                bc_needle[BC_NEEDLE] = 0;
                if (!memmem(bc_t, BC_SPAN - 64, bc_needle, BC_NEEDLE))
                        break;
        }
        for (int k = 0; k < 2; k++) {
                unsigned octaves = k ? 13 : 9;
                for (unsigned i = 0; i < BC_POOL; i++) {
                        unsigned o = (unsigned)(next() % octaves);
                        bc_pool[k][i] = ((size_t)1 << o) + (size_t)(next() % ((size_t)1 << o));
                }
        }
        printf("ticks a call, the best of %d rounds; %s; libc/new below 1.00 is a loss (marked <)\n", BC_ROUNDS,
               old ? "new = the shim, old = OLD.so" : "new = the shim");
#define BC_FNS(type, name, libc_fn)                                                     \
        type name[3];                                                                   \
        for (int s_ = 0; s_ < bc_sides - 1; s_++)                                       \
                name[s_] = (type)dlsym(libs[s_], #libc_fn);                             \
        name[bc_sides - 1] = (type)libc_fn
        {
                cmp_fn fns[3] = {bc_null, bc_null, bc_null};
                bc_head("the loop around a call that does nothing");
                BC_RUN(fns, 1024, 8, f(bc_a, bc_b, n));
                bc_row("null");
        }
        if (bc_wanted(argc, argv, "memcmp")) {
                BC_FNS(cmp_fn, fns, memcmp);
                bc_memcmp(fns);
        }
        if (bc_wanted(argc, argv, "strcmp")) {
                BC_FNS(scmp_fn, fns, strcmp);
                bc_strcmp(fns);
        }
        if (bc_wanted(argc, argv, "strncmp")) {
                BC_FNS(sncmp_fn, fns, strncmp);
                bc_strncmp(fns);
        }
        if (bc_wanted(argc, argv, "strcpy")) {
                BC_FNS(scpy_fn, fns, strcpy);
                bc_strcpy("strcpy", fns);
        }
        if (bc_wanted(argc, argv, "stpcpy")) {
                BC_FNS(scpy_fn, fns, stpcpy);
                bc_strcpy("stpcpy", fns);
        }
        if (bc_wanted(argc, argv, "stpncpy")) {
                BC_FNS(sncpy_fn, fns, stpncpy);
                bc_stpncpy(fns);
        }
        if (bc_wanted(argc, argv, "strcat")) {
                BC_FNS(scpy_fn, fns, strcat);
                bc_strcat(fns);
        }
        if (bc_wanted(argc, argv, "strstr")) {
                BC_FNS(sstr_fn, fns, strstr);
                bc_strstr(fns);
        }
        if (bc_wanted(argc, argv, "memmem")) {
                BC_FNS(mem_fn, fns, memmem);
                bc_memmem(fns);
        }
        return 0;
}

/*
        hunt: the byte hunts (strlen, strnlen, memchr, memrchr, strchr, strchrnul, strrchr, rawmemchr), ticks a call, the C
        library's against every shim named after "hunt" (this tree's, a baseline's, one built capped to a lower tier), each
        measured in turn at each row so that a drift of the machine reaches every column alike.

        fixed: one length a row, 1 B to 64 KiB, the same call over and over, so every branch in the body is learned. The string
        starts 35 bytes into a page, off every vector boundary. "hit" puts the byte at the row's length less one (memrchr: that
        far from the end of a 64 KiB count; memchr: with a count to the end of the buffer), "absent" ends the call at the length
        (the count, or the terminator).

        random: the length drawn log-uniform from 1 to 512 and from 1 to 8192 at every call. A pool of strings is laid end to
        end, each with its own length and so its own alignment, and visited in an order drawn once (65536 picks) and read as an
        array, so the predictor meets a length it cannot know and the only work between calls is two loads.

        The cost is core cycles in user mode (hunt_now), the best of 64 rounds and of three passes over every side, nothing
        subtracted: both sides pay the same loop around the call.
*/
enum { HUNT_STRING, HUNT_CHR_HIT, HUNT_RLAST, HUNT_MEMR_HIT };
enum { SHAPE_LEN, SHAPE_NLEN, SHAPE_FIND, SHAPE_CHR };
static const struct hunt_case {
        const char *name, *symbol;
        int layout, shape, big_count;
} hunt_cases[] = {
        {"strlen", "strlen", HUNT_STRING, SHAPE_LEN, 0},
        {"strnlen ends first", "strnlen", HUNT_STRING, SHAPE_NLEN, 1},
        {"strnlen bound first", "strnlen", HUNT_CHR_HIT, SHAPE_NLEN, 0},
        {"memchr absent", "memchr", HUNT_STRING, SHAPE_FIND, 0},
        {"memchr hit", "memchr", HUNT_CHR_HIT, SHAPE_FIND, 1},
        {"memrchr absent", "memrchr", HUNT_STRING, SHAPE_FIND, 0},
        {"memrchr hit", "memrchr", HUNT_MEMR_HIT, SHAPE_FIND, 1},
        {"strchr absent", "strchr", HUNT_STRING, SHAPE_CHR, 0},
        {"strchr hit", "strchr", HUNT_CHR_HIT, SHAPE_CHR, 0},
        {"strchrnul absent", "strchrnul", HUNT_STRING, SHAPE_CHR, 0},
        {"strchrnul hit", "strchrnul", HUNT_CHR_HIT, SHAPE_CHR, 0},
        {"strrchr absent", "strrchr", HUNT_STRING, SHAPE_CHR, 0},
        {"strrchr hit last", "strrchr", HUNT_RLAST, SHAPE_CHR, 0},
        {"rawmemchr", "rawmemchr", HUNT_CHR_HIT, SHAPE_CHR, 0},
};
#define HUNT_ORDER 65536u
#define HUNT_ROOM (4u << 20)
static unsigned hunt_order[HUNT_ORDER];
static const char *hunt_ptr[4096];
static size_t hunt_arg[4096], hunt_len[4096];
static volatile size_t hunt_sink;

/* A byte that is none of the three the layouts mean something by (0, the hunted 0xee). */
static unsigned char hunt_plain(void) { return (unsigned char)(1 + next() % 200); }

/*
        Lay out count strings of the lengths in hunt_len at buffer (past its first 64 bytes), end to end, the way layout says,
        and fill hunt_ptr and hunt_arg for the case.
*/
static void hunt_lay(unsigned char *buffer, size_t room, const struct hunt_case *c, size_t count, size_t first)
{
        unsigned char *p = buffer + first, *end = buffer + room - 64;
        /* Fresh bytes over all that the strings take and a page more: every answer is inside a string's own bytes, so what
           an earlier layout left further on is never reached by one (bench_hunt filled the whole buffer once). */
        size_t used = first + PAGE;
        for (size_t j = 0; j < count; j++)
                used += hunt_len[j] + 1;
        for (unsigned char *q = buffer; q < buffer + used && q < end; q++)
                *q = hunt_plain();
        for (size_t j = 0; j < count; j++) {
                size_t n = hunt_len[j];
                hunt_ptr[j] = (const char *)p;
                switch (c->layout) {
                case HUNT_STRING:  p[n] = 0; p += n + 1; break;                 // n bytes, then the end
                case HUNT_CHR_HIT: p[n - 1] = 0xee; p += n; break;              // the byte at n - 1, no end until the buffer's
                case HUNT_RLAST:   p[n - 1] = 0xee; p[n] = 0; p += n + 1; break;
                case HUNT_MEMR_HIT: p[0] = 0xee; p += n; break;                 // the byte at each start: n back from the next
                }
        }
        *end = 0;
        for (size_t j = 0; j < count; j++) {
                size_t n = hunt_len[j];
                if (c->layout == HUNT_MEMR_HIT) {
                        hunt_arg[j] = (size_t)((const unsigned char *)hunt_ptr[j] + n - buffer);
                        hunt_ptr[j] = (const char *)buffer;
                } else
                        hunt_arg[j] = c->big_count ? (c->shape == SHAPE_NLEN ? (size_t)1 << 30 : (size_t)(end - (const unsigned char *)hunt_ptr[j])) : n;
        }
}

/*
        Core cycles in user mode, from the processor's own counter (perf_event_open), so a clock that boosts or drops while
        the machine is shared changes nothing; ticks of the time stamp counter where the counter cannot be had.
*/
static int hunt_counter = -2;

static uint64_t hunt_now(void)
{
        if (hunt_counter == -2) {
                struct {
                        uint32_t type, size;
                        uint64_t config, period, sample_type, read_format, flags, rest[12];
                } attr = {0, sizeof attr, 0};   // PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES
                attr.size = 112;
                attr.flags = (1ull << 5) | (1ull << 6); // exclude_kernel, exclude_hv
                hunt_counter = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
                printf("hunt: %s\n", hunt_counter >= 0 ? "core cycles in user mode" : "time stamp ticks (no cycle counter)");
        }
        if (hunt_counter < 0)
                return ticks();
        uint64_t v = 0;
        if (read(hunt_counter, &v, sizeof v) != sizeof v)
                return 0;
        return v;
}

static double hunt_time(const struct hunt_case *c, void *f, int calls)
{
        uint64_t best = ~0ull;
        unsigned k = 0;
        for (int round = 0; round < 64; round++) {
                uint64_t t0 = hunt_now();
                switch (c->shape) {
                case SHAPE_LEN:
                        for (int i = 0; i < calls; i++) {
                                unsigned j = hunt_order[k++ % HUNT_ORDER];
                                hunt_sink = ((len_fn)f)(hunt_ptr[j]);
                        }
                        break;
                case SHAPE_NLEN:
                        for (int i = 0; i < calls; i++) {
                                unsigned j = hunt_order[k++ % HUNT_ORDER];
                                hunt_sink = ((nlen_fn)f)(hunt_ptr[j], hunt_arg[j]);
                        }
                        break;
                case SHAPE_FIND:
                        for (int i = 0; i < calls; i++) {
                                unsigned j = hunt_order[k++ % HUNT_ORDER];
                                hunt_sink = (size_t)((find_fn)f)(hunt_ptr[j], 0xee, hunt_arg[j]);
                        }
                        break;
                case SHAPE_CHR:
                        for (int i = 0; i < calls; i++) {
                                unsigned j = hunt_order[k++ % HUNT_ORDER];
                                hunt_sink = (size_t)((schr_fn)f)(hunt_ptr[j], 0xee);
                        }
                        break;
                }
                uint64_t t = hunt_now() - t0;
                if (t < best)
                        best = t;
        }
        return (double)best / calls;
}

/* What one call answers, the same on every side or the row is not worth its numbers. */
static size_t hunt_answer(const struct hunt_case *c, void *f, size_t j)
{
        switch (c->shape) {
        case SHAPE_LEN: return ((len_fn)f)(hunt_ptr[j]);
        case SHAPE_NLEN: return ((nlen_fn)f)(hunt_ptr[j], hunt_arg[j]);
        case SHAPE_FIND: return (size_t)((find_fn)f)(hunt_ptr[j], 0xee, hunt_arg[j]);
        default: return (size_t)((schr_fn)f)(hunt_ptr[j], 0xee);
        }
}

static void hunt_row(const struct hunt_case *c, void **sides, int count, const char *mode, size_t size, int calls, size_t pool)
{
        double t[16];
        for (size_t j = 0; j < pool; j++)
                for (int s = 1; s < count; s++)
                        if (hunt_answer(c, sides[s], j) != hunt_answer(c, sides[0], j)) {
                                printf("hunt: WRONG %s %s %zu: side %d answers %zu, the C library %zu\n", mode, c->name, hunt_len[j], s,
                                       hunt_answer(c, sides[s], j), hunt_answer(c, sides[0], j));
                                failures++;
                                return;
                        }
        for (int s = 0; s < count; s++)
                t[s] = 1e30;
        for (int pass = 0; pass < 3; pass++)
                for (int s = 0; s < count; s++) {
                        double v = hunt_time(c, sides[s], calls);
                        if (v < t[s])
                                t[s] = v;
                }
        printf("%-6s %-20s %6zu %8.1f", mode, c->name, size, t[0]);
        for (int s = 1; s < count; s++)
                printf(" %8.1f %5.2f", t[s], t[0] / t[s]);
        printf("\n");
}

static void bench_hunt(void **shims, int shim_count, const char *only)
{
        static const size_t sizes[] = {1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 31, 32, 40, 48, 56, 63, 64, 80, 96, 112, 128, 160, 192,
                                       224, 256, 320, 384, 448, 512, 640, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288,
                                       16384, 32768, 65536};
        unsigned char *all = mmap(0, HUNT_ROOM + 2 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (all == MAP_FAILED) {
                perror("mmap");
                exit(2);
        }
        unsigned char *buffer = all + PAGE;
        mprotect(all, PAGE, PROT_NONE);
        mprotect(buffer + HUNT_ROOM, PAGE, PROT_NONE);
        for (size_t i = 0; i < HUNT_ROOM; i++)
                buffer[i] = hunt_plain();
        printf("a call's cost; columns: the C library, then each shim and the C library's cost over its (above 1.00 the shim wins)\n");
        for (size_t ci = 0; ci < sizeof hunt_cases / sizeof *hunt_cases; ci++) {
                const struct hunt_case *c = &hunt_cases[ci];
                if (only && strcmp(only, c->symbol))
                        continue;
                void *sides[16];
                if (!(sides[0] = dlsym(RTLD_DEFAULT, c->symbol)))
                        continue;       // musl has no rawmemchr
                for (int s = 0; s < shim_count; s++)
                        sides[s + 1] = dlsym(shims[s], c->symbol);
                for (size_t si = 0; si < sizeof sizes / sizeof *sizes; si++) {
                        size_t n = sizes[si];
                        memset(hunt_order, 0, sizeof hunt_order);
                        hunt_len[0] = n;
                        hunt_lay(buffer, HUNT_ROOM, c, 1, (c->layout == HUNT_MEMR_HIT ? 65536 - n : 0) + PAGE + 35);
                        int calls = n <= 256 ? 4096 : (int)(4096 * 256 / n) < 16 ? 16 : (int)(4096 * 256 / n);
                        hunt_row(c, sides, shim_count + 1, "fixed", n, calls, 1);
                }
                static const size_t ranges[] = {512, 8192};
                for (int r = 0; r < 2; r++) {
                        size_t pool = ranges[r] == 512 ? 256 : 128;
                        uint64_t keep = seed;
                        seed = 0x2545f4914f6cdd1dull + ranges[r];       // the same lengths and order on every side and run
                        /* log-uniform: a power of two drawn evenly, then a place drawn evenly inside its octave */
                        unsigned octaves = ranges[r] == 512 ? 9 : 13;
                        for (size_t j = 0; j < pool; j++) {
                                uint64_t x = next() % ((uint64_t)octaves << 16);
                                size_t n = (size_t)(((1ull << (x >> 16)) * (65536 + (x & 0xffff))) >> 16);
                                hunt_len[j] = n < 1 ? 1 : n > ranges[r] ? ranges[r] : n;
                        }
                        for (unsigned i = 0; i < HUNT_ORDER; i++)
                                hunt_order[i] = (unsigned)(next() % pool);
                        seed = keep;
                        hunt_lay(buffer, HUNT_ROOM, c, pool, 64);
                        char mode[16];
                        snprintf(mode, sizeof mode, "r%zu", ranges[r]);
                        hunt_row(c, sides, shim_count + 1, mode, ranges[r], ranges[r] == 512 ? 4096 : 1024, pool);
                }
        }
        munmap(all, HUNT_ROOM + 2 * PAGE);
}

int main(int argc, char **argv)
{
        if (argc < 2) {
                fprintf(stderr, "libc_shim SHIM.so [bench [compare [OLD.so|-] [NAME...]] | hunt [OTHER.so ...] | copy [OTHER.so ...]]\n");
                return 2;
        }
        setvbuf(stdout, 0, _IONBF, 0);  // what was said before a crash is kept
        int trace = getenv("LIBC_SHIM_TRACE") != 0;
        void *shim = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!shim) {
                fprintf(stderr, "dlopen: %s\n", dlerror());
                return 2;
        }
        src = region(), src2 = region(), dst_a = region(), dst_b = region();
        if (argc > 3 && !strcmp(argv[2], "bench") && !strcmp(argv[3], "compare"))
                return bench_compare(shim, argc - 4, argv + 4);
        if (argc > 2 && !strcmp(argv[2], "bench")) {
                bench(shim);
                return 0;
        }
        if (argc > 2 && (!strcmp(argv[2], "hunt") || !strcmp(argv[2], "copy"))) {
                /* libc_shim SHIM.so hunt [OTHER.so ...]: LIBC_SHIM_ONLY=strlen keeps the rows of one name;
                   libc_shim SHIM.so copy [OTHER.so ...]: LIBC_SHIM_COPY_ONLY=NAME keeps one case */
                void *shims[15] = {shim};
                int count = 1;
                for (int i = 3; i < argc && count < 15; i++)
                        if (!(shims[count++] = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL))) {
                                fprintf(stderr, "dlopen: %s\n", dlerror());
                                return 2;
                        }
                if (!strcmp(argv[2], "copy")) {
                        const char *only = getenv("LIBC_SHIM_COPY_ONLY");
                        bench_copy(shims, count, only && *only ? only : 0);
                        return 0;
                }
                const char *only = getenv("LIBC_SHIM_ONLY");
                bench_hunt(shims, count, only && *only ? only : 0);
                return failures != 0;
        }
        make_lengths();
        /* every name src/shim/libc.map gives (test/libc_shim.sh also holds the library's exports to the map) */
        static const char *names[] = {"memcpy", "memmove", "memset", "memset_explicit", "memcmp", "bcmp", "__memcmpeq",
                                      "memchr", "memrchr", "rawmemchr", "__rawmemchr", "memmem", "memccpy", "memfrob",
                                      "mempcpy", "__mempcpy", "bzero", "__bzero", "explicit_bzero", "bcopy", "strlen",
                                      "strnlen", "__strnlen", "strcmp", "strncmp", "strchr", "index", "strchrnul", "strrchr",
                                      "rindex", "strcpy", "stpcpy", "__stpcpy", "strncpy", "stpncpy", "__stpncpy", "strcat",
                                      "strncat", "strlcpy", "strlcat", "strstr", "strspn", "strcspn", "strpbrk", "strsep",
                                      "strtok", "strtok_r", "__strtok_r", "wmemset", "wmemcpy", "wmemmove", "wmempcpy",
                                      "ffs", "ffsl", "ffsll", "abs", "labs", "llabs", "imaxabs", "__memcpy_chk",
                                      "__memmove_chk", "__mempcpy_chk", "__memset_chk", "__explicit_bzero_chk",
                                      "__strcpy_chk", "__stpcpy_chk", "__strcat_chk", "__strncat_chk", "__strncpy_chk",
                                      "__stpncpy_chk", "__strlcpy_chk", "__strlcat_chk", "__wmemset_chk", "__wmemcpy_chk",
                                      "__wmemmove_chk", "__wmempcpy_chk"};
        for (size_t i = 0; i < sizeof names / sizeof *names; i++)
                if (!dlsym(shim, names[i]))
                        fail(names[i], "not exported", 0, 0, 0);
        if (trace)
                fprintf(stderr, "%s\n", "check_copy");
        check_copy("memcpy", (copy_fn)dlsym(shim, "memcpy"), memcpy, 0);
        if (trace)
                fprintf(stderr, "%s\n", "check_copy");
        check_copy("memmove (apart)", (copy_fn)dlsym(shim, "memmove"), memmove, 0);
        if (trace)
                fprintf(stderr, "%s\n", "check_copy");
        check_copy("mempcpy", (copy_fn)dlsym(shim, "mempcpy"), mempcpy, 1);
        if (trace)
                fprintf(stderr, "%s\n", "check_copy");
        check_copy("__mempcpy", (copy_fn)dlsym(shim, "__mempcpy"), mempcpy, 1);
        if (trace)
                fprintf(stderr, "%s\n", "check_move");
        check_move("memmove", (copy_fn)dlsym(shim, "memmove"), memmove);
        /* memcpy keeps glibc's promise of overlap (src/shim/libc.c, LIBRARY_MEMCPY_OVERLAPS) */
        if (trace)
                fprintf(stderr, "%s\n", "check_move");
        check_move("memcpy onto itself", (copy_fn)dlsym(shim, "memcpy"), memmove);
        if (trace)
                fprintf(stderr, "%s\n", "check_fill");
        check_fill((fill_fn)dlsym(shim, "memset"), memset);
        if (trace)
                fprintf(stderr, "%s\n", "check_zero");
        check_zero((zero_fn)dlsym(shim, "bzero"));
        if (trace)
                fprintf(stderr, "%s\n", "check_compare");
        check_compare("memcmp", (cmp_fn)dlsym(shim, "memcmp"), memcmp, 0);
        if (trace)
                fprintf(stderr, "%s\n", "check_compare");
        check_compare("bcmp", (cmp_fn)dlsym(shim, "bcmp"), memcmp, 1);
        if (trace)
                fprintf(stderr, "%s\n", "check_compare_edges");
        check_compare_edges("memcmp", (cmp_fn)dlsym(shim, "memcmp"), 0);
        check_compare_edges("bcmp", (cmp_fn)dlsym(shim, "bcmp"), 1);
        if (trace)
                fprintf(stderr, "%s\n", "check_find");
        check_find("memchr", (find_fn)dlsym(shim, "memchr"), (find_fn)memchr);
        if (trace)
                fprintf(stderr, "%s\n", "check_find");
        check_find("memrchr", (find_fn)dlsym(shim, "memrchr"), (find_fn)memrchr);
        if (trace)
                fprintf(stderr, "%s\n", "check_memmem");
        check_memmem((mem_fn)dlsym(shim, "memmem"));
        if (trace)
                fprintf(stderr, "%s\n", "check_memccpy");
        check_memccpy((ccpy_fn)dlsym(shim, "memccpy"));
        if (trace)
                fprintf(stderr, "%s\n", "check_strings");
        check_strings(shim);
        if (trace)
                fprintf(stderr, "%s\n", "check_string_compare_edges");
        check_string_compare_edges((scmp_fn)dlsym(shim, "strcmp"), (sncmp_fn)dlsym(shim, "strncmp"));
        if (trace)
                fprintf(stderr, "%s\n", "check_copy_edges");
        check_copy_edges(shim);
        if (trace)
                fprintf(stderr, "%s\n", "check_more");
        check_more(shim);
        if (trace)
                fprintf(stderr, "%s\n", "check_checked");
        check_checked(shim);
        if (trace)
                fprintf(stderr, "%s\n", "check_overflow");
        check_overflow(shim, "__memcpy_chk");
        if (trace)
                fprintf(stderr, "%s\n", "check_overflow");
        check_overflow(shim, "__memmove_chk");
        if (trace)
                fprintf(stderr, "%s\n", "check_overflow");
        check_overflow(shim, "__memset_chk");
        if (trace)
                fprintf(stderr, "%s\n", "check_overflow");
        check_overflow(shim, "__mempcpy_chk");
        printf("%s: %lu checks, %lu failures\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures != 0;
}
