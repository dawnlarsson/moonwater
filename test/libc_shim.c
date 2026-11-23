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
                size_t m = (size_t)next() % 40;
                char needle_room[64];
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
                size_t m = (size_t)next() % 48;
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

int main(int argc, char **argv)
{
        if (argc < 2) {
                fprintf(stderr, "libc_shim SHIM.so [bench]\n");
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
        if (argc > 2 && !strcmp(argv[2], "bench")) {
                bench(shim);
                return 0;
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
