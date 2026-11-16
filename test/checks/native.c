/*
        test/checks/native.c: the stdio buffer and the native routines built against generated headers.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_stdio_buffer
#include <stdio.h>
#include <stdio_ext.h>

/* libstdbuf runs before main.  Reading glibc's public stdio inspection
   surface here proves that the launcher changed the streams themselves,
   rather than merely publishing plausible environment variables. */
int main(void)
{
        printf("%d:%zu %d:%zu %d:%zu\n",
               __flbf(stdin) != 0, __fbufsize(stdin),
               __flbf(stdout) != 0, __fbufsize(stdout),
               __flbf(stderr) != 0, __fbufsize(stderr));
        return 0;
}
#endif /* CHECK_stdio_buffer */

#ifdef CHECK_native_byte_hunts
/*
        The byte hunts, run on this machine.

        Built by sh test/run native, which lifts the arm64 bodies out of
        lib.c first. Both shapes that broke them before the fix are here,
        plus a sweep of every alignment and length; see FINDING for what the
        two shapes are and why random data does not find them.
*/
#include "lifted.h"

#define NATIVE_SEED 0x2545F4914F6CDD1Dul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

char *string_first_of(const char *, int);
char *string_first_of_or_end(const char *, int);
char *string_first_of_max(const char *, u64, int);
char *string_find(const char *, const char *);

static char *r_find(const char *h, const char *n)
{
        if (!*n) return 0;
        for (; *h; h++) {
                const char *a = h, *b = n;
                while (*a && *b && *a == *b) { a++; b++; }
                if (!*b) return (char *)h;
        }
        return 0;
}

int main(void)
{
        static u8 a[4096], nee[64];
        u64 checks = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, bt = 0;

        // The extractor checks the marked source payload with SHA-256.  This
        // second, deliberately cheap hash checks that the Mach-O object the
        // native assembler and linker produced has those same 256 bytes.
        u64 table_hash = 0xcbf29ce484222325ul;
        for (u64 i = 0; i < sizeof(byte_commonness); i++)
                table_hash = (table_hash ^ byte_commonness[i]) * 0x100000001b3ul;
        checks++;
        if (table_hash != 0x4e8034ba88cd1ac9ul) bt++;

        // The two adjacencies a borrow out of the prefix can forge.
        for (u64 off = 1; off < 8; off++)
                for (int c = 1; c < 256; c++) {
                        for (u64 i = 0; i < off+40; i++) a[i] = 0x55;
                        a[off-1] = (u8)c; a[off] = (u8)(c+1);
                        a[off+20] = (u8)c; a[off+30] = 0;
                        checks += 3;
                        if (string_first_of((char*)a+off,c)        != r_first((char*)a+off,c)) b1++;
                        if (string_first_of_or_end((char*)a+off,c) != r_or_end((char*)a+off,c)) b2++;
                        if (string_first_of_max((char*)a+off,40,c) != r_max((char*)a+off,40,c)) b3++;

                        for (u64 i = 0; i < off+40; i++) a[i] = 0x55;
                        a[off-1] = 0x00; a[off] = 0x01;
                        a[off+20] = (u8)c; a[off+30] = 0;
                        checks += 3;
                        if (string_first_of((char*)a+off,c)        != r_first((char*)a+off,c)) b1++;
                        if (string_first_of_or_end((char*)a+off,c) != r_or_end((char*)a+off,c)) b2++;
                        if (string_first_of_max((char*)a+off,40,c) != r_max((char*)a+off,40,c)) b3++;
                }

        // Every alignment and length, over an alphabet small enough that
        // partial matches and adjacent byte values are common.
        for (u64 t = 0; t < 300000; t++) {
                u64 off = next()%64, len = next()%120 + 1;
                int c = (int)(next()%255) + 1;
                for (u64 i = 0; i < off+len+16; i++) a[i] = (u8)(next()%6) + 1;
                a[off+len] = 0;
                checks += 3;
                if (string_first_of((char*)a+off,c)         != r_first((char*)a+off,c)) b1++;
                if (string_first_of_or_end((char*)a+off,c)  != r_or_end((char*)a+off,c)) b2++;
                if (string_first_of_max((char*)a+off,len,c) != r_max((char*)a+off,len,c)) b3++;
        }

        // string_find, where a failed candidate must not eat the starts inside it.
        for (u64 t = 0; t < 200000; t++) {
                u64 off = next()%32, hl = next()%80 + 1, nl = next()%5 + 1;
                for (u64 i = 0; i < off+hl; i++) a[i] = (u8)(next()%4) + 'a';
                a[off+hl] = 0;
                for (u64 i = 0; i < nl; i++) nee[i] = (u8)(next()%4) + 'a';
                nee[nl] = 0;
                checks++;
                if (string_find((char*)a+off,(char*)nee) != r_find((char*)a+off,(char*)nee)) b4++;
        }

        printf("arm64 byte hunts: %lu checks | table %lu | first_of %lu | or_end %lu | max %lu | find %lu\n",
               checks, bt, b1, b2, b3, b4);
        return (bt||b1||b2||b3||b4) != 0;
}
#endif /* CHECK_native_byte_hunts */

#ifdef CHECK_native_bounded
/*
        The four routines arm64 did not have, run on this machine.

        string_last_of_or_end, string_compare_max, string_length_max and
        memory_first_of were written for this block rather than lifted from
        anywhere, so every one of them is new code and none of it has ever
        run before. The references are what libc means by strrchr,
        strncmp, strnlen and memchr.
*/
#include "lifted.h"

#define NATIVE_SEED 0x9E3779B97F4A7C15ul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

char *string_last_of_or_end(const char *, int);
int   string_compare_max(const char *, const char *, u64);
u64   string_length_max(const char *, u64);
void *memory_first_of(const void *, int, u64);

static int r_ncmp(const char *a, const char *b, u64 n)
{
        for (u64 i = 0; i < n; i++) {
                u8 x = (u8)a[i], y = (u8)b[i];
                if (x != y) return (int)x - (int)y;
                if (!x) return 0;
        }
        return 0;
}

static int sign(int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; }

int main(void)
{
        static u8 a[4096], b[4096];
        u64 checks = 0, e1 = 0, e2 = 0, e3 = 0, e4 = 0;

        // Every alignment, every length across the word boundaries, and an
        // alphabet small enough that repeats and adjacent values are common.
        for (u64 t = 0; t < 300000; t++) {
                u64 off = next()%64, len = next()%140;
                int c = (int)(next()%255) + 1;

                for (u64 i = 0; i < off+len+16; i++) a[i] = (u8)(next()%5) + 1;
                a[off+len] = 0;

                checks += 3;
                if (string_last_of_or_end((char*)a+off,c) != r_last_or_end((char*)a+off,c)) e1++;
                if (string_length_max((char*)a+off,len)   != r_nlen((char*)a+off,len)) e3++;
                if (memory_first_of(a+off,c,len)          != r_mchr(a+off,c,len)) e4++;

        }

        // string_compare_max wants a pair, built so ties and differences are
        // both common: copy, then poke one byte.
        for (u64 t = 0; t < 300000; t++) {
                u64 ao = next()%32, bo = next()%32, len = next()%80 + 1;
                for (u64 i = 0; i < len+8; i++) a[ao+i] = (u8)(next()%4) + 'a';
                a[ao+len] = 0;
                for (u64 i = 0; i < len+8; i++) b[bo+i] = a[ao+i];
                if (next() & 3) b[bo + next()%(len+1)] = (u8)(next()%4) + 'a';
                u64 n = next()%(len+4);
                checks++;
                if (sign(string_compare_max((char*)a+ao,(char*)b+bo,n))
                    != sign(r_ncmp((char*)a+ao,(char*)b+bo,n))) e2++;
        }

        // the ends: nothing to look at, and a hit in the very last byte
        for (u64 len = 0; len < 40; len++)
                for (u64 off = 0; off < 16; off++) {
                        for (u64 i = 0; i < len+8; i++) a[off+i] = 0xAA;
                        if (len) a[off+len-1] = 0x5A;
                        a[off+len] = 0x5A;              // just past: must not be seen
                        checks += 2;
                        if (memory_first_of(a+off,0x5A,len) != r_mchr(a+off,0x5A,len)) e4++;
                        a[off+len] = 0;
                        if (string_length_max((char*)a+off,len) != r_nlen((char*)a+off,len)) e3++;
                }

        printf("arm64 bounded: %lu checks | last_of_or_end %lu | compare_max %lu | length_max %lu | first_of %lu\n",
               checks, e1, e2, e3, e4);
        return (e1||e2||e3||e4) != 0;
}
#endif /* CHECK_native_bounded */

#ifdef CHECK_native_riscv_bits
/*
        The two things riscv64 has to do by hand, checked here.

        Base RV64I has no count-trailing-zeros and no count-leading-zeros, so
        the new routines in the riscv block build both out of shifts. This is
        that arithmetic on its own, against a plain loop, over every input it
        can be handed: one flag bit per byte, at bit 8k+7.
*/
typedef unsigned long u64;
int printf(const char *, ...);

#define ONES  0x0101010101010101ul
#define SEVEN 0x7f7f7f7f7f7f7f7ful

// isolate the lowest flag, then count the bytes under it
static u64 lowest_byte(u64 flags)
{
        u64 t = flags & (~flags + 1);            // sub t,zero,x / and
        t = t - 1;                               // addi -1
        t = t & ONES;                            // and t0
        t = t * ONES;                            // mul t0
        t = t >> 56;                             // srli 56
        return t - 1;                            // addi -1
}

// smear down, isolate the highest, then the same count
static u64 highest_byte(u64 flags)
{
        u64 x = flags;
        x |= x >> 1;  x |= x >> 2;  x |= x >> 4;
        x |= x >> 8;  x |= x >> 16; x |= x >> 32;
        u64 t = (x >> 1) + 1;                    // srli 1 / addi 1
        t = t - 1;
        t = t & ONES;
        t = t * ONES;
        t = t >> 56;
        return t - 1;
}

// the exact per byte zero test, which must not carry between bytes
static u64 zero_flags(u64 v)
{
        return ~(((v & SEVEN) + SEVEN) | v | SEVEN);
}

int main(void)
{
        u64 checks = 0, bad_lo = 0, bad_hi = 0, bad_zero = 0;

        // every non-empty subset of the eight flag positions
        for (unsigned m = 1; m < 256; m++) {
                u64 flags = 0;
                int lo = -1, hi = -1;
                for (int k = 0; k < 8; k++)
                        if (m & (1u << k)) {
                                flags |= 1ul << (8*k + 7);
                                if (lo < 0) lo = k;
                                hi = k;
                        }
                checks += 2;
                if ((long)lowest_byte(flags)  != lo) bad_lo++;
                if ((long)highest_byte(flags) != hi) bad_hi++;
        }

        // the zero test against a byte by byte answer, including the
        // adjacencies the cheap test gets wrong
        for (u64 t = 0; t < 400000; t++) {
                static u64 seed = 0x2545F4914F6CDD1Dul;
                seed ^= seed<<13; seed ^= seed>>7; seed ^= seed<<17;
                u64 v = seed;
                if (t < 2000) v &= 0x0101010101010101ul * (t % 3);  // lots of zero bytes
                u64 want = 0;
                for (int k = 0; k < 8; k++)
                        if (((v >> (8*k)) & 0xff) == 0) want |= 1ul << (8*k + 7);
                checks++;
                if (zero_flags(v) != want) bad_zero++;
        }

        printf("riscv64 bit arithmetic: %lu checks | lowest %lu | highest %lu | zero test %lu\n",
               checks, bad_lo, bad_hi, bad_zero);
        return (bad_lo || bad_hi || bad_zero) != 0;
}
#endif /* CHECK_native_riscv_bits */

#ifdef CHECK_native_bulk
/*
        memset, memcpy, memmove, memfrob and the string copies, run on this machine.

        The verify lane runs the arm64 block under qemu, which proves the
        answers but not that they came out of an arm64 core. These seven have
        an aligned NEON loop with the head and the last chunk held in q
        registers across it, and the alignment step moves the source by one of
        sixty four values -- so the sweep here is size crossed with destination
        offset crossed with source offset, and the overlap distances that put a
        memmove's read on either side of its own writes.

        memory_copy branches to memory_copy_apart by name rather than carrying a
        second copy of the forward direction. Darwin spells that symbol with a
        leading underscore and extract.py writes the underscored one, so the
        plain name is set to the same address here.
*/
__asm__(".globl memory_copy_apart\n.set memory_copy_apart, _memory_copy_apart\n");
#include "bulk.h"

typedef unsigned char u8;
typedef unsigned long u64;

void *memory_fill(void *, int, u64);
void memory_exchange_apart(void *, void *, u64);
void *memory_frob(void *, u64);
void *memory_frob_8_8(void *, u64);
void *memory_copy_apart(void *, const void *, u64);
void *memory_copy(void *, const void *, u64);
char *string_copy(char *, const char *);
char *string_copy_max(char *, const char *, u64);
char *string_cut(char *, int);
void string_replace_all(char *, int, int);

int printf(const char *, ...);

#define ROOM 2600

static u8 got[ROOM], want[ROOM], from[ROOM];
static u64 checks, bad;

// The references: what the C in lib.c did before the assembly.
static void r_fill(u8 *d, int v, u64 n)
{
        while (n--)
                *d++ = (u8)v;
}

static void r_copy(u8 *d, const u8 *s, u64 n)
{
        if (d > s && d < s + n) {
                d += n - 1;
                s += n - 1;
                while (n--)
                        *d-- = *s--;
        } else
                while (n--)
                        *d++ = *s++;
}

static void r_exchange(u8 *a, u8 *b, u64 n)
{
        if (a == b)
                return;

        while (n--) {
                u8 held = *a;
                *a++ = *b;
                *b++ = held;
        }
}

static void *r_frob(u8 *p, u64 n)
{
        u8 *answer = p;

        while (n--)
                *p++ ^= 42;

        return answer;
}

static u64 r_len(const char *s)
{
        const char *p = s;
        while (*p)
                p++;
        return (u64)(p - s);
}

static char *r_first(char *s, int c)
{
        for (; *s; s++)
                if (*s == (char)c)
                        return s;
        return c ? 0 : s;
}

static void whole(const char *what, u64 size, u64 off)
{
        checks++;

        for (u64 i = 0; i < ROOM; i++)
                if (got[i] != want[i]) {
                        if (bad++ < 8)
                                printf("  FAIL %s size %lu off %lu: byte %lu is %u want %u\n",
                                       what, size, off, i, got[i], want[i]);
                        return;
                }
}

static const long gaps[] = {-1200, -513, -257, -129, -65, -64, -33, -32, -9, -8,
                            -3, -1, 1, 3, 8, 9, 32, 33, 64, 65, 129, 257, 513, 1200};

static void memory(void)
{
        for (u64 i = 0; i < ROOM; i++)
                from[i] = (u8)(i * 13 + 5);

        for (u64 size = 0; size <= 700; size++)
                for (u64 off = 0; off < 72; off++) {
                        if (off + size + 200 > ROOM)
                                continue;

                        r_fill(got, 0xA5, ROOM);
                        r_fill(want, 0xA5, ROOM);
                        memory_fill(got + off, (int)(size + 3), size);
                        r_fill(want + off, (int)(size + 3), size);
                        whole("memory_fill", size, off);

                        for (u64 so = 0; so < 9; so++) {
                                r_fill(got, 0xA5, ROOM);
                                r_fill(want, 0xA5, ROOM);
                                memory_copy_apart(got + off, from + so, size);
                                r_copy(want + off, from + so, size);
                                whole("memory_copy_apart", size, off);
                        }

                        for (u64 g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++) {
                                long gap = gaps[g];
                                u64 base = 1200 + off;

                                if ((long)base + gap < 0)
                                        continue;
                                if (base + size + 40 > ROOM)
                                        continue;
                                if (base + gap + size + 40 > ROOM)
                                        continue;

                                r_copy(got, from, ROOM);
                                r_copy(want, from, ROOM);
                                memory_copy(got + base + gap, got + base, size);
                                r_copy(want + base + gap, want + base, size);
                                whole("memory_copy", size, off);
                        }
                }
}

static void exchange_one(u64 left, u64 right, u64 size)
{
        r_fill(got, 0xA5, ROOM);
        r_fill(want, 0xA5, ROOM);

        for (u64 i = 0; i < size; i++) {
                got[left + i] = want[left + i] = (u8)(i * 37 + left);
                got[right + i] = want[right + i] = (u8)(i * 73 + right);
        }

        memory_exchange_apart(got + left, got + right, size);
        r_exchange(want + left, want + right, size);
        whole("memory_exchange_apart", size, left);
}

static void exchange(void)
{
        static const u64 large[] = {129, 255, 256, 257, 511, 512, 513, 700, 1000};

        memory_exchange_apart(0, 0, 0);
        memory_exchange_apart(0, 0, 37);

        r_fill(got, 0xA5, ROOM);
        r_fill(want, 0xA5, ROOM);
        memory_exchange_apart(got + 31, got + 31, 1000);
        whole("memory_exchange_apart equal", 1000, 31);

        for (u64 size = 0; size <= 128; size++)
                for (u64 left = 0; left < 16; left++)
                        for (u64 right = 0; right < 16; right++)
                                exchange_one(32 + left, 1400 + right, size);

        for (u64 width = 0; width < sizeof(large) / sizeof(large[0]); width++)
                for (u64 left = 0; left < 16; left++)
                        for (u64 right = 0; right < 16; right++)
                                exchange_one(32 + left, 1400 + right,
                                             large[width]);
}

static void frob(void)
{
        static const u64 widths[] = {
                0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512,
                700, 1000, 2048,
        };

        checks++;
        if (memory_frob(0, 0) != 0)
                bad++;

        for (u64 width = 0; width < sizeof(widths) / sizeof(widths[0]); width++)
                for (u64 off = 0; off < 32; off++) {
                        u64 size = widths[width];
                        void *answer;

                        for (u64 i = 0; i < ROOM; i++)
                                got[i] = want[i] = (u8)(i * 37 + size + off);

                        answer = memory_frob(got + 64 + off, size);
                        r_frob(want + 64 + off, size);
                        checks++;
                        if (answer != got + 64 + off)
                                bad++;
                        whole("memory_frob", size, off);

                        memory_frob(got + 64 + off, size);
                        r_frob(want + 64 + off, size);
                        whole("memory_frob twice", size, off);
                }

        /* The size class at eight bytes, every offset: it must answer what the
           generic answers. */
        for (u64 off = 0; off < 32; off++) {
                void *answer;

                for (u64 i = 0; i < ROOM; i++)
                        got[i] = want[i] = (u8)(i * 37 + 8 + off);

                answer = memory_frob_8_8(got + 64 + off, 8);
                r_frob(want + 64 + off, 8);
                checks++;
                if (answer != got + 64 + off)
                        bad++;
                whole("memory_frob_8_8", 8, off);
        }
}

static u8 subject[ROOM], spare[ROOM], mirror[ROOM];

static void strings(void)
{
        for (u64 size = 0; size <= 700; size++)
                for (u64 off = 0; off < 72; off++) {
                        if (off + size + 200 > ROOM)
                                continue;

                        char *text = (char *)subject + off;

                        for (u64 i = 0; i < size; i++)
                                text[i] = (char)('a' + (i * 7 + off) % 4);

                        text[size] = 0;

                        // string_copy, into a poisoned buffer so a byte written
                        // past the terminator it copied is caught
                        for (u64 d = 0; d < 3; d++) {
                                r_fill(spare, 0xA5, ROOM);
                                string_copy((char *)spare + d, text);
                                checks++;
                                if (r_len((char *)spare + d) != size)
                                        bad++;
                                checks++;
                                for (u64 i = 0; i < size; i++)
                                        if (spare[d + i] != (u8)text[i]) { bad++; break; }
                                checks++;
                                for (u64 i = d + size + 1; i < ROOM; i++)
                                        if (spare[i] != 0xA5) { bad++; break; }
                        }

                        // string_copy_max at every bound around the length
                        for (u64 b = 0; b < 6; b++) {
                                u64 limit = size + 2 > b ? size + 2 - b : 0;

                                if (limit + 8 > ROOM)
                                        continue;

                                r_fill(spare, 0xA5, ROOM);
                                string_copy_max((char *)spare, text, limit);

                                u64 kept = size < limit ? size : limit;

                                checks++;
                                for (u64 i = 0; i < kept; i++)
                                        if (spare[i] != (u8)text[i]) { bad++; break; }

                                checks++;
                                if (size < limit && spare[kept] != 0)
                                        bad++;

                                checks++;
                                for (u64 i = size < limit ? kept + 1 : limit; i < ROOM; i++)
                                        if (spare[i] != 0xA5) { bad++; break; }
                        }

                        // string_cut and string_replace_all write into the
                        // string, so each gets its own copy -- at the same
                        // offset, because both walk to a block boundary before
                        // they widen and an always-aligned string never enters
                        // that walk
                        for (u64 c = 0; c < 2; c++) {
                                int symbol = c ? 'b' : 'q';
                                char *mine = (char *)spare + off;

                                r_fill(spare, 0xA5, ROOM);
                                for (u64 i = 0; i <= size; i++)
                                        mine[i] = text[i];

                                char *where = r_first(mine, symbol);
                                u64 at = where ? (u64)(where - mine) : 0;
                                char follow = where ? mine[at + 1] : 0;

                                char *answer = string_cut(mine, symbol);

                                checks++;
                                if (answer != ((!where || follow == 0) ? 0 : mine + at + 1))
                                        bad++;

                                checks++;
                                if (where && mine[at] != 0)
                                        bad++;

                                checks++;
                                for (u64 i = 0; i < ROOM; i++) {
                                        if (i >= off && i <= off + size)
                                                continue;
                                        if (spare[i] != 0xA5) { bad++; break; }
                                }

                                r_fill(spare, 0xA5, ROOM);
                                r_fill(mirror, 0xA5, ROOM);
                                for (u64 i = 0; i <= size; i++)
                                        mine[i] = mirror[off + i] = (u8)text[i];
                                for (u64 i = 0; i < size; i++)
                                        if (mirror[off + i] == (u8)symbol)
                                                mirror[off + i] = 'z';

                                string_replace_all(mine, symbol, 'z');

                                checks++;
                                for (u64 i = 0; i < ROOM; i++)
                                        if (spare[i] != mirror[i]) { bad++; break; }
                        }
                }
}

int main(void)
{
        memory();
        exchange();
        frob();
        strings();

        printf("arm64 bulk: %lu checks | failures %lu\n", checks, bad);
        return bad != 0;
}
#endif /* CHECK_native_bulk */

#ifdef CHECK_native_reverse
/* ARM64 memory_reverse lifted verbatim from lib.c and run on the host. */
#include "reverse.h"

#define NATIVE_SEED 0x2545f4914f6cdd1dull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

void *memory_reverse(void *, u64);

#define MEDIUM (1u << 15)
#define LARGE ((1u << 20) + 128)

static u8 got[MEDIUM], want[MEDIUM];
static u8 large_got[LARGE], large_want[LARGE];
static u64 checks, bad;
static void fill(u8 *at, u8 value, u64 size)
{
        while (size--)
                *at++ = value;
}

static void reference(u8 *at, u64 size)
{
        u8 *end = at + size;

        while (at < end) {
                u8 value;

                end--;
                if (at >= end)
                        break;

                value = *at;
                *at++ = *end;
                *end = value;
        }
}

static void prepare(u64 extent, u64 offset, u64 size)
{
        fill(got, 0xa5, extent);
        fill(want, 0xa5, extent);

        for (u64 i = 0; i < size; i++)
                got[offset + i] = want[offset + i] = (u8)next();
}

static void same(const char *name, const u8 *left, const u8 *right, u64 size)
{
        checks++;

        for (u64 i = 0; i < size; i++)
                if (left[i] != right[i]) {
                        if (bad++ < 8)
                                printf("  FAIL %s: byte %lu is %u want %u\n",
                                       name, i, left[i], right[i]);
                        return;
                }
}

static void short_matrix(void)
{
        checks++;
        if (memory_reverse(0, 0) != 0)
                bad++;

        for (u64 size = 0; size <= 1024; size++)
                for (u64 residue = 0; residue < 64; residue++) {
                        u64 offset = 64 + residue;
                        u64 extent = offset + size + 64;

                        prepare(extent, offset, size);

                        checks++;
                        if (memory_reverse(got + offset, size) != got + offset)
                                bad++;

                        reference(want + offset, size);
                        same("small sizes/residues", got, want, extent);

                        memory_reverse(got + offset, size);
                        reference(want + offset, size);
                        same("double reverse", got, want, extent);
                }
}

static void random_matrix(void)
{
        for (u64 turn = 0; turn < 1024; turn++) {
                u64 size = next() % 16385;
                u64 offset = 33 + (next() & 63);
                u64 extent = offset + size + 71;

                prepare(extent, offset, size);
                memory_reverse(got + offset, size);
                reference(want + offset, size);
                same("random length/content", got, want, extent);
        }
}

static void page_edges(void)
{
        static const u64 sizes[] = {
            0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33,
            63, 64, 65, 255, 256, 257, 4095, 4096, 4097,
        };
        u64 base = (u64)(void *)got;
        u64 edge = ((base + 4095) & ~(u64)4095) - base + 8192;

        for (u64 c = 0; c < sizeof(sizes) / sizeof(sizes[0]); c++) {
                u64 size = sizes[c];
                u64 offset = edge - size;
                u64 extent = edge + 64;

                prepare(extent, offset, size);
                memory_reverse(got + offset, size);
                reference(want + offset, size);
                same("page-edge end", got, want, extent);
        }
}

static void megabyte(void)
{
        for (u64 i = 0; i < LARGE; i++) {
                u8 value = (u8)next();

                large_got[i] = large_want[i] = value;
        }

        memory_reverse(large_got + 37, 1u << 20);
        reference(large_want + 37, 1u << 20);
        same("one megabyte", large_got, large_want, LARGE);

        memory_reverse(large_got + 37, 1u << 20);
        reference(large_want + 37, 1u << 20);
        same("one megabyte double", large_got, large_want, LARGE);
}

int main(void)
{
        short_matrix();
        random_matrix();
        page_edges();
        megabyte();

        printf("arm64 memory_reverse: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_reverse */

#ifdef CHECK_native_words
/* ARM64 memory_count_words lifted verbatim from lib.c and run on the host: every
   size to 700 at every residue of a 64-byte line, both incoming states, every
   byte value at the lanes either side of the wide turn's blocks, long random
   counts split anywhere, and counts that end against an unmapped page. */
#include "words.h"
#include <sys/mman.h>
#include <unistd.h>

#define NATIVE_SEED 0x9e3779b97f4a7c15ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct
{
        u64 x, y;
} words_pair;

words_pair memory_count_words(const void *, u64, int);

static u8 room[(1u << 16) + 256];
static u64 checks, bad;

static words_pair reference(const u8 *at, u64 size, int inside)
{
        words_pair answer = {0, inside ? 1 : 0};

        for (u64 i = 0; i < size; i++) {
                int word = !(at[i] == 32 || (at[i] >= 9 && at[i] <= 13));

                if (word && !answer.y)
                        answer.x++;
                answer.y = (u64)word;
        }
        return answer;
}

static void same(const char *name, const u8 *at, u64 size, int inside)
{
        words_pair want = reference(at, size, inside), got = memory_count_words(at, size, inside);

        checks++;
        if (got.x != want.x || got.y != want.y)
                if (bad++ < 8)
                        printf("  FAIL %s: size %lu inside %d: %lu/%lu want %lu/%lu\n", name, size, inside,
                               got.x, got.y, want.x, want.y);
}

static u8 byte(void)
{
        static const u8 alphabet[] = {0, 8, 9, 10, 11, 12, 13, 14, 31, 32, 33, 73, 77, 96, 127, 128, 137, 160, 'a', ' '};
        u64 r = next();

        return (r & 3) ? alphabet[(r >> 8) % sizeof(alphabet)] : (u8)(r >> 16);
}

int main(void)
{
        for (u64 size = 0; size <= 700; size++)
                for (u64 residue = 0; residue < 64; residue += (size < 300 ? 1 : 7)) {
                        for (u64 i = 0; i < size; i++)
                                room[residue + i] = byte();
                        same("sizes and residues", room + residue, size, 0);
                        same("sizes and residues", room + residue, size, 1);
                }

        static const u64 lanes[] = {0, 1, 15, 16, 63, 64, 127, 128, 255, 256, 299};
        for (u64 value = 0; value < 256; value++)
                for (u64 k = 0; k < sizeof(lanes) / sizeof(lanes[0]); k++)
                        for (u64 background = 0; background < 2; background++) {
                                for (u64 i = 0; i < 300; i++)
                                        room[i] = background ? 'x' : ' ';
                                room[lanes[k]] = (u8)value;
                                same("every byte at the edges", room, 300, 0);
                                same("every byte at the edges", room, 300, 1);
                        }

        for (u64 turn = 0; turn < 300; turn++) {
                u64 size = next() % (1u << 16), split = next() % (size + 1);

                for (u64 i = 0; i < size; i++)
                        room[i] = byte();
                words_pair want = reference(room, size, (int)(turn & 1));
                words_pair first = memory_count_words(room, split, (int)(turn & 1));
                words_pair rest = memory_count_words(room + split, size - split, (int)first.y);

                checks++;
                if (first.x + rest.x != want.x || rest.y != want.y)
                        if (bad++ < 8)
                                printf("  FAIL split: size %lu at %lu\n", size, split);
        }

        long page = sysconf(_SC_PAGESIZE);
        u8 *pages = mmap(0, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (pages == MAP_FAILED || mprotect(pages + page, (size_t)page, PROT_NONE))
                bad++;
        else
                for (u64 size = 0; size <= 1100 && size <= (u64)page; size++) {
                        u8 *at = pages + page - size;

                        for (u64 i = 0; i < size; i++)
                                at[i] = byte();
                        same("against an unmapped page", at, size, 0);
                        same("against an unmapped page", at, size, 1);
                }

        printf("arm64 memory_count_words: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_words */

#ifdef CHECK_native_squeeze
/* ARM64 memory_squeeze_bytes lifted verbatim from lib.c and run on the host:
   every size to 300 and every residue against a reference, then the
   former C loop tr ran against the body over a megabyte of words. */
#include "squeeze.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64 memory_squeeze_bytes(void *, u64, const void *, u64);

#define TEXT (1u << 20)

static u8 got[1024], want[1024], table[256];
static u8 source[TEXT], block[TEXT];
static u64 checks, bad;

static u64 reference(u8 *at, u64 size, const u8 *marks, u64 previous)
{
        u64 kept = 0;

        for (u64 i = 0; i < size; i++) {
                u8 value = at[i];

                if (!(marks[value] && value == previous))
                        at[kept++] = value;
                previous = value;
        }
        return kept;
}

// The loop tr -s ran before: a byte at a time, the run after a marked byte
// skipped by a span.
__attribute__((noinline)) static u64 former(u8 *at, u64 left, const u8 *squeezed)
{
        u64 kept = 0;
        int last = -1;

        for (u64 c = 0; c < left;) {
                u8 character = at[c++];

                if (!squeezed[character] || character != last)
                        at[kept++] = character;
                last = character;
                if (squeezed[character])
                        while (c < left && at[c] == character)
                                c++;
        }
        return kept;
}

static void matrix(void)
{
        static const u8 alphabet[4] = {'a', 'b', ' ', 0xf1};

        for (u64 density = 0; density < 3; density++)
                for (u64 size = 0; size <= 300; size++)
                        for (u64 residue = 0; residue < 16; residue++) {
                                u64 offset = 64 + residue;
                                u64 previous = size % 3 == 0 ? 256 : size % 3 == 1 ? 'a' : 'z';

                                for (u64 i = 0; i < 256; i++)
                                        table[i] = density == 2 ? (u8)(next() & 1)
                                                   : density == 1 ? i == 'a' || i == ' '
                                                                  : 1;
                                for (u64 i = 0; i < sizeof(got); i++)
                                        got[i] = want[i] = next() % 5 ? alphabet[next() & 3] : (u8)next();

                                u64 kept = reference(want + offset, size, table, previous);
                                u64 answer = memory_squeeze_bytes(got + offset, size, table, previous);

                                checks++;
                                if (answer != kept) {
                                        if (bad++ < 8)
                                                printf("  FAIL size %lu residue %lu: kept %lu want %lu\n",
                                                       size, residue, answer, kept);
                                        continue;
                                }
                                checks++;
                                for (u64 i = 0; i < offset + kept; i++)
                                        if (got[i] != want[i]) {
                                                if (bad++ < 8)
                                                        printf("  FAIL size %lu residue %lu byte %lu\n",
                                                               size, residue, i);
                                                break;
                                        }
                                checks++;
                                for (u64 i = offset + size; i < sizeof(got); i++)
                                        if (got[i] != want[i]) {
                                                if (bad++ < 8)
                                                        printf("  FAIL size %lu residue %lu past the block\n",
                                                               size, residue);
                                                break;
                                        }
                        }
}

static void timing(void)
{
        static const char *names[3] = {"tr -s ' '", "tr -s a-z", "every byte"};

        for (u64 i = 0; i < TEXT; i++) {
                u64 pick = next() % 40;

                source[i] = pick < 30 ? (u8)('a' + pick % 26) : pick < 38 ? ' ' : '\n';
        }

        for (u64 set = 0; set < 3; set++) {
                u64 best_former = ~0ul, best_body = ~0ul, kept_former = 0, kept_body = 0;

                for (u64 i = 0; i < 256; i++)
                        table[i] = set == 2 || (set == 1 && i >= 'a' && i <= 'z') || (set == 0 && i == ' ');
                for (u64 trial = 0; trial < 7; trial++) {
                        u64 start, took;

                        __builtin_memcpy(block, source, TEXT);
                        start = ticks();
                        kept_former = former(block, TEXT, table);
                        took = ticks() - start;
                        best_former = took < best_former ? took : best_former;

                        __builtin_memcpy(block, source, TEXT);
                        start = ticks();
                        kept_body = memory_squeeze_bytes(block, TEXT, table, 256);
                        took = ticks() - start;
                        best_body = took < best_body ? took : best_body;
                }
                checks++;
                if (kept_former != kept_body)
                        bad++;
                printf("  %s, one megabyte: former C %lu ticks, assembly %lu, asm/C %lu%%\n",
                       names[set], best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        }
}

int main(void)
{
        matrix();
        timing();
        printf("arm64 memory_squeeze_bytes: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_squeeze */

#ifdef CHECK_native_offsets
/* ARM64 memory_offsets_of_either lifted verbatim from lib.c and run on the
   host: sizes, residues, limits and resumption against a byte loop, then
   the byte loop against the body over a read of comma-separated rows. */
#include "offsets.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef unsigned int u32;
u64 memory_offsets_of_either(u32 *, const void *, u64, u8, u8, u64);
u64 memory_offsets_between(u32 *, const void *, u64, u8, u8, u64);
u64 memory_offsets_outside(u32 *, const void *, u64, u8, u8, u64);
u64 memory_offsets_in_set(u32 *, const void *, u64, const u8 *, u64);

#define READ (1u << 16)

static u8 bytes[READ + 64];
static u32 got[READ], want[READ];
static u64 checks, bad;

__attribute__((noinline)) static u64 former(u32 *out, const u8 *at, u64 size, u8 a, u8 b)
{
        u64 count = 0;

        for (u64 i = 0; i < size; i++)
                if (at[i] == a || at[i] == b)
                        out[count++] = (u32)i;
        return count;
}

static void fill_rows(void)
{
        for (u64 i = 0; i < sizeof(bytes); i++) {
                u64 pick = next() % 8;

                bytes[i] = pick == 0 ? ',' : pick == 1 && next() % 4 == 0 ? '\n'
                                                                   : (u8)('a' + next() % 26);
        }
}

static void matrix(void)
{
        static const u64 limits[] = {1, 2, 7, 16, 17, 64, 100000};

        fill_rows();
        for (u64 size = 0; size <= 300; size++)
                for (u64 residue = 0; residue < 16; residue += 3)
                        for (u64 l = 0; l < sizeof(limits) / sizeof(limits[0]); l++) {
                                const u8 *at = bytes + residue;
                                u64 count = former(want, at, size, ',', '\n');
                                u64 have = 0, from = 0;
                                int ok = 1;

                                for (;;) {
                                        u64 answer = memory_offsets_of_either(got, at + from, size - from,
                                                                              ',', '\n', limits[l]);
                                        if (answer > limits[l]) { ok = 0; break; }
                                        for (u64 i = 0; i < answer; i++)
                                                if (have + i >= count || got[i] + from != want[have + i])
                                                        ok = 0;
                                        have += answer;
                                        if (answer < limits[l] || !ok)
                                                break;
                                        from += got[answer - 1] + 1;
                                }
                                checks++;
                                if (!ok || have != count) {
                                        if (bad++ < 8)
                                                printf("  FAIL size %lu residue %lu limit %lu\n",
                                                       size, residue, limits[l]);
                                }
                        }
}

static void timing(void)
{
        u64 best_former = ~0ul, best_body = ~0ul, a = 0, b = 0;

        fill_rows();
        for (u64 trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 64; r++)
                        a = former(want, bytes, READ, ',', '\n');
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 64; r++)
                        b = memory_offsets_of_either(got, bytes, READ, ',', '\n', READ);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (a != b)
                bad++;
        printf("  64 reads of 64 KB: former C %lu ticks, assembly %lu, asm/C %lu%%\n",
               best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
}

static void ranges(void)
{
        static const u8 bounds[][2] = {{0x20, 0x7e}, {8, 10}, {0, 0}, {0, 0xff}};
        u64 best_former = ~0ul, best_body = ~0ul, a = 0, b = 0;

        for (u64 i = 0; i < sizeof(bytes); i++)
                bytes[i] = next() % 4 ? (u8)('a' + next() % 40) : (u8)next();
        for (u64 r = 0; r < 4; r++)
                for (u64 outside = 0; outside < 2; outside++)
                        for (u64 size = 0; size <= 200; size++) {
                                u8 low = bounds[r][0], high = bounds[r][1];
                                u64 count = 0, answer;

                                for (u64 i = 0; i < size; i++)
                                        if ((bytes[i] >= low && bytes[i] <= high) != (outside != 0))
                                                want[count++] = (u32)i;
                                answer = outside ? memory_offsets_outside(got, bytes, size, low, high, 100000)
                                                 : memory_offsets_between(got, bytes, size, low, high, 100000);
                                checks++;
                                if (answer != count) {
                                        if (bad++ < 8)
                                                printf("  FAIL range %lu outside %lu size %lu\n", r, outside, size);
                                        continue;
                                }
                                for (u64 i = 0; i < count; i++)
                                        if (got[i] != want[i]) {
                                                bad++;
                                                break;
                                        }
                        }
        for (u64 trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 64; r++) {
                        u64 count = 0;
                        for (u64 i = 0; i < READ; i++)
                                if (bytes[i] < 0x20 || bytes[i] > 0x7e)
                                        want[count++] = (u32)i;
                        a = count;
                }
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 64; r++)
                        b = memory_offsets_outside(got, bytes, READ, 0x20, 0x7e, READ);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (a != b)
                bad++;
        printf("  outside printable, 64 reads of 64 KB: former C %lu ticks, assembly %lu, asm/C %lu%%\n",
               best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
}

static void sets(void)
{
        static u8 table[256];
        u64 best_former = ~0ul, best_body = ~0ul, a = 0, b = 0;

        for (u64 t = 0; t < 3; t++) {
                for (u64 i = 0; i < 256; i++)
                        table[i] = t == 0 ? i == '\n' : t == 1 ? (i < 9 || (i > 10 && i < 32) || i > 126)
                                                        : (u8)(next() & 1);
                for (u64 size = 0; size <= 200; size++) {
                        u64 count = 0, answer;

                        for (u64 i = 0; i < size; i++)
                                if (table[bytes[i]])
                                        want[count++] = (u32)i;
                        answer = memory_offsets_in_set(got, bytes, size, table, 100000);
                        checks++;
                        if (answer != count) {
                                if (bad++ < 8)
                                        printf("  FAIL set %lu size %lu\n", t, size);
                                continue;
                        }
                        for (u64 i = 0; i < count; i++)
                                if (got[i] != want[i]) {
                                        bad++;
                                        break;
                                }
                }
        }
        for (u64 i = 0; i < 256; i++)
                table[i] = i < 9 || (i > 10 && i < 32) || i > 126;
        for (u64 trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 64; r++) {
                        u64 count = 0;
                        for (u64 i = 0; i < READ; i++)
                                if (table[bytes[i]])
                                        want[count++] = (u32)i;
                        a = count;
                }
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 64; r++)
                        b = memory_offsets_in_set(got, bytes, READ, table, READ);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (a != b)
                bad++;
        printf("  in a table set, 64 reads of 64 KB: former C %lu ticks, assembly %lu, asm/C %lu%%\n",
               best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
}

int main(void)
{
        matrix();
        timing();
        ranges();
        sets();
        printf("arm64 memory_offsets_of_either: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_offsets */

#ifdef CHECK_native_hash
/* ARM64 string_hash_33_length lifted verbatim from lib.c and run on the
   host: every length to 300 at every start in a word, bytes after the
   terminator that are not zero, the last bytes of a page; then the call at
   the lengths names are against a C loop, per call, dependent. */
#include "hash.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct { u64 x, y; } pair;
pair string_hash_33_length(const char *);
void *mmap(void *, u64, int, int, int, long);
int mprotect(void *, u64, int);

static u64 checks, bad;

static u64 model(const u8 *text, u64 length)
{
        u64 hash = 5381;

        for (u64 i = 0; i < length; i++)
                hash = hash * 33 + text[i];
        return hash;
}

__attribute__((noinline)) static pair former(const char *text)
{
        pair answer = {5381, 0};

        while (text[answer.y])
                answer.x = answer.x * 33 + (u8)text[answer.y++];
        return answer;
}

int main(void)
{
        static u8 room[1024];
        u8 *pages = mmap(0, 32768, 3, 0x1002, -1, 0);

        for (u64 offset = 0; offset < 16; offset++)
                for (u64 length = 0; length <= 300; length++) {
                        u8 *text = room + 64 + offset;

                        for (u64 i = 0; i < length + 16; i++)
                                text[i] = (u8)(next() % 255 + 1);
                        text[length] = 0;
                        pair got = string_hash_33_length((const char *)text);
                        checks++;
                        if (got.x != model(text, length) || got.y != length) {
                                if (bad++ < 8)
                                        printf("  FAIL offset %lu length %lu\n", offset, length);
                        }
                }
        if (pages != (u8 *)-1) {
                u8 *edge = pages + 16384;
                mprotect(edge, 16384, 0);
                for (u64 length = 0; length <= 100; length++) {
                        u8 *text = edge - length - 1;
                        for (u64 i = 0; i < length; i++)
                                text[i] = (u8)(i % 250 + 1);
                        text[length] = 0;
                        pair got = string_hash_33_length((const char *)text);
                        checks++;
                        if (got.x != model(text, length) || got.y != length)
                                bad++;
                }
        }

        static const u64 lengths[] = {1, 3, 5, 7, 9, 12, 16, 24, 32, 64};
        for (u64 l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
                u64 length = lengths[l], best_former = ~0ul, best_body = ~0ul;
                char *text = (char *)room + 128 + (l % 5);
                volatile u64 zero = 0;
                u64 k = 0;

                for (u64 i = 0; i < length; i++)
                        text[i] = (char)('a' + i % 26);
                text[length] = 0;
                for (int t = 0; t < 7; t++) {
                        u64 start = ticks();
                        for (int r = 0; r < 100000; r++)
                                k = former(text + (k & zero)).x;
                        u64 took = ticks() - start;
                        best_former = took < best_former ? took : best_former;
                        start = ticks();
                        for (int r = 0; r < 100000; r++)
                                k = string_hash_33_length(text + (k & zero)).x;
                        took = ticks() - start;
                        best_body = took < best_body ? took : best_body;
                }
                printf("  length %2lu: former C %lu ticks, assembly %lu, asm/C %lu%%\n", length,
                       best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        }
        printf("arm64 string_hash_33_length: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_hash */

#ifdef CHECK_native_join
/* ARM64 text_blank_field lifted verbatim from src/sh/text.c: every start in
   records of blanks and words at every alignment against the byte walk join
   used to run, the last bytes of a page, then that walk against the body over
   a read of join-shaped records. */
#include "join.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct { u64 x, y; } pair;
pair text_blank_field(const u8 *, u64, u64, u64);
void *mmap(void *, u64, int, int, int, long);
int mprotect(void *, u64, int);

static u64 checks, bad;

static int blank(u8 byte) { return byte == ' ' || byte == '\t' || byte == '\n'; }

__attribute__((noinline)) static pair former(const u8 *bytes, u64 length, u64 at)
{
        pair answer;

        while (at < length && blank(bytes[at]))
                at++;
        answer.x = at;
        while (at < length && !blank(bytes[at]))
                at++;
        answer.y = at;
        return answer;
}

static void fill(u8 *at, u64 size)
{
        static const u8 pieces[] = {' ', '\t', '\n', 'a', 'b', 0xff, 0x01, 'z'};

        for (u64 i = 0; i < size; i++)
                at[i] = next() % 3 ? pieces[3 + next() % 5] : pieces[next() % 3];
}

int main(void)
{
        static u8 room[4096];
        u8 *pages = mmap(0, 32768, 3, 0x1002, -1, 0);

        fill(room, sizeof(room));
        for (u64 length = 0; length <= 200; length++)
                for (u64 offset = 0; offset < 16; offset++)
                        for (u64 at = 0; at <= length; at += 1 + at / 8) {
                                const u8 *bytes = room + 64 + offset;
                                pair want = former(bytes, length, at);
                                pair got = text_blank_field(bytes, length, at, '\n');
                                u64 wx = want.x, wy = want.x == length ? length : want.y;

                                checks++;
                                if (got.x != wx || (wx != length && got.y != wy)) {
                                        if (bad++ < 8)
                                                printf("  FAIL length %lu at %lu: %lu,%lu want %lu,%lu\n",
                                                       length, at, got.x, got.y, wx, wy);
                                }
                        }
        if (pages != (u8 *)-1) {
                u8 *edge = pages + 16384;
                mprotect(edge, 16384, 0);
                for (u64 length = 0; length <= 64; length++) {
                        u8 *bytes = edge - length;
                        fill(bytes, length);
                        for (u64 at = 0; at <= length; at++) {
                                pair want = former(bytes, length, at);
                                pair got = text_blank_field(bytes, length, at, '\n');
                                checks++;
                                if (got.x != want.x || (want.x != length && got.y != want.y))
                                        bad++;
                        }
                }
        }

        // Records of a key and a few words, walked field by field as join does.
        static u8 text[1 << 16];
        u64 used = 0;
        while (used + 80 < sizeof(text)) {
                u64 words = 1 + next() % 6;
                for (u64 w = 0; w < words; w++) {
                        u64 size = 1 + next() % 9;
                        for (u64 i = 0; i < size; i++)
                                text[used++] = (u8)('a' + next() % 26);
                        text[used++] = ' ';
                }
                text[used - 1] = '\n';
        }
        u64 best_former = ~0ul, best_body = ~0ul, fields_former = 0, fields_body = 0;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks(), fields = 0;
                for (int r = 0; r < 16; r++)
                        for (u64 at = 0; at < used;) {
                                pair field = former(text, used, at);
                                if (field.x == used)
                                        break;
                                at = field.y;
                                fields++;
                        }
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                fields_former = fields;
                start = ticks();
                fields = 0;
                for (int r = 0; r < 16; r++)
                        for (u64 at = 0; at < used;) {
                                pair field = text_blank_field(text, used, at, '\n');
                                if (field.x == used)
                                        break;
                                at = field.y;
                                fields++;
                        }
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
                fields_body = fields;
        }
        checks++;
        if (fields_former != fields_body)
                bad++;
        printf("  fields of join records: former C %lu ticks, assembly %lu, asm/C %lu%%\n",
               best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 text_blank_field: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_join */

#ifdef CHECK_native_keyword
/* ARM64 parse_keyword_of lifted verbatim from src/sh/parse.c, against the
   length switch it replaced, over tokens drawn the way the parser meets
   them: the forty commonest words of 1.6 million calls made reading five
   configure scripts, test/run and two ltmain.sh, each as often as it came,
   the rest as words of their lengths, then both timed over that stream. */
#include "keyword.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct { int kind, op, joined; const char *text; u64 length; void *trace; int forced, line; } keyword_token;

int parse_keyword_of(const keyword_token *);
void *memcpy(void *, const void *, u64);

static u64 checks, bad;

static inline unsigned l16(const char *p) { unsigned short v; memcpy(&v, p, 2); return v; }
static inline unsigned l32(const char *p) { unsigned v; memcpy(&v, p, 4); return v; }
#define W2(a, b) ((unsigned)(unsigned char)(a) | (unsigned)(unsigned char)(b) << 8)
#define W4(a, b, c, d) (W2(a, b) | W2(c, d) << 16)

__attribute__((noinline)) static int former(const keyword_token *token)
{
        const char *text;

        if (token->kind != 1)
                return 0;
        text = token->text;
        switch (token->length) {
        case 1: return text[0] == '!' ? 19 : text[0] == '{' ? 9 : text[0] == '}' ? 17 : 0;
        case 2:
                switch (l16(text)) {
                case W2('i', 'f'): return 1; case W2('f', 'i'): return 13;
                case W2('d', 'o'): return 14; case W2('i', 'n'): return 18;
                case W2(']', ']'): return 20; default: return 0;
                }
        case 3: return l16(text) == W2('f', 'o') && text[2] == 'r' ? 4 : 0;
        case 4:
                switch (l32(text)) {
                case W4('t', 'h', 'e', 'n'): return 10; case W4('e', 'l', 's', 'e'): return 11;
                case W4('e', 'l', 'i', 'f'): return 12; case W4('d', 'o', 'n', 'e'): return 15;
                case W4('c', 'a', 's', 'e'): return 5; case W4('e', 's', 'a', 'c'): return 16;
                case W4('t', 'i', 'm', 'e'): return 7; default: return 0;
                }
        case 5:
                if (l32(text) == W4('w', 'h', 'i', 'l') && text[4] == 'e') return 2;
                if (l32(text) == W4('u', 'n', 't', 'i') && text[4] == 'l') return 3;
                return 0;
        case 6:
                if (l32(text) == W4('s', 'e', 'l', 'e') && l16(text + 4) == W2('c', 't')) return 6;
                if (l32(text) == W4('c', 'o', 'p', 'r') && l16(text + 4) == W2('o', 'c')) return 8;
                return 0;
        default: return 0;
        }
}

/* Thousands of calls in the trace: a word, its length, how often. */
static const struct { const char *word; u64 length, weight; } common[] = {
        {"if", 2, 376}, {"then", 4, 185}, {"case", 4, 155}, {"else", 4, 84}, {"{", 1, 64},
        {"printf", 6, 62}, {"test", 4, 56}, {"for", 3, 55}, {"fi", 2, 41}, {":", 1, 26},
        {"}", 1, 20}, {"func_mod", 14, 13}, {"ac_prev=", 14, 12}, {"eval", 4, 10},
        {"rm", 2, 9}, {"ac_prev=", 18, 8}, {"while", 5, 8}, {"cat", 3, 8}, {"ac_prev=", 19, 8},
        {"6", 1, 7}, {"echo", 4, 7}, {"$as_nop", 7, 7}, {"done", 4, 6}, {"ac_prev=", 15, 6},
        {"\"%s\\n\"", 6, 6}, {"break", 5, 4}, {"do", 2, 4}, {"%s", 2, 4}, {"ac_fn_c_", 19, 3},
        {"ac_prev=", 22, 3}, {"as_dir=.", 9, 3}, {"elif", 4, 3}, {"same", 4, 3},
        {"func_app", 11, 3}, {"ac_prev=", 21, 3}, {"ac_dashd", 15, 2}, {"bindir=$", 17, 2},
        {"build_al", 22, 2}, {"cache_fi", 21, 2}, {"cache_fi", 23, 2},
};
/* The rest, by length (twelve and longer as twelve), and tokens not words. */
static const u64 other[13] = {0, 7, 6, 4, 8, 6, 3, 3, 6, 6, 11, 10, 242};
#define NONWORD 5
#define STREAM 65536

int main(void)
{
        static keyword_token stream[STREAM];
        static char arena[STREAM * 32 + 64];
        char *at = arena;
        u64 total = NONWORD;

        for (u64 i = 0; i < sizeof(common) / sizeof(common[0]); i++)
                total += common[i].weight;
        for (u64 i = 0; i < 13; i++)
                total += other[i];

        for (u64 i = 0; i < STREAM; i++) {
                u64 pick = next() % total, length = 0;
                int kind = 1;

                for (u64 c = 0; c < 32; c++)
                        at[c] = "abcdefghijklmnopqrstuvwxyz_=$\"{"[next() % 31];
                for (u64 c = 0; c < sizeof(common) / sizeof(common[0]) && !length; c++) {
                        if (pick < common[c].weight) {
                                u64 have = 0;
                                while (common[c].word[have])
                                        have++;
                                memcpy(at, common[c].word, have);
                                length = common[c].length;
                        } else
                                pick -= common[c].weight;
                }
                for (u64 l = 1; l < 13 && !length; l++) {
                        if (pick < other[l])
                                length = l == 12 ? 12 + next() % 12 : l;
                        else
                                pick -= other[l];
                }
                if (!length)
                        kind = 2, length = 1;
                stream[i] = (keyword_token){kind, 0, 0, at, length, 0, 0, 1};
                at += length + 1 + next() % 3;
        }

        for (u64 i = 0; i < STREAM; i++) {
                int want = former(stream + i), got = parse_keyword_of(stream + i);

                checks++;
                if (want != got && bad++ < 8)
                        printf("  FAIL token %lu length %lu: %d want %d\n", i, stream[i].length, got, want);
        }

        u64 best_former = ~0ul, best_body = ~0ul, sink = 0, sink_body = 0;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 20; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink += former(stream + i);
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 20; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink_body += parse_keyword_of(stream + i);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != sink_body)
                bad++;
        printf("  %d tokens drawn as the parser meets them, 20 times: length switch %lu ticks, assembly %lu, asm/C %lu%%\n",
               STREAM, best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 parse_keyword_of: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_keyword */

#ifdef CHECK_native_envprobe
/* ARM64 env_find_hashed_span_probe lifted verbatim from src/sh/builtin.c,
   with the C check of the name found last in front of it as the shell has
   it, against the C probe it replaced calling memory_compare lifted from
   lib.c: a table the size make's configure builds, 972 variables in 2048
   slots, and lookups drawn from the 60,241 that configure made, its
   sixty four commonest names as often as they came and a third of them the
   name looked up just before, then both timed over that stream. */
#include "envprobe.h"
#include "floodlight_lib.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct { const char *text; u64 hash, name_length, value_length; unsigned char owned, permanent, declared, attributes; int array; } envprobe_variable;
typedef struct { u64 hash, length, index_plus_one; } envprobe_slot;
typedef struct { u64 generation, hit_generation, hit_hash, hit_length, hit_index; } envprobe_lookup;

envprobe_variable *shell_vars;
u64 shell_var_count;
envprobe_lookup env_lookup;
envprobe_slot *env_index;
u64 env_index_slots;

u64 env_find_hashed_span_probe(const char *, u64, u64);
int memory_compare(const void *, const void *, u64);
void *memcpy(void *, const void *, u64);

static u64 checks, bad;
static envprobe_lookup former_lookup;

__attribute__((noinline, cold)) static u64 former_probe(const char *name, u64 length, u64 hash)
{
        u64 at = hash & (env_index_slots - 1);

        for (u64 probes = 0; probes < env_index_slots; probes++) {
                envprobe_slot *slot = env_index + at;

                if (!slot->index_plus_one)
                        return shell_var_count;
                if (slot->index_plus_one != ~0ul && slot->hash == hash && slot->length == length) {
                        u64 index = slot->index_plus_one - 1;

                        if (index < shell_var_count && !memory_compare(shell_vars[index].text, name, length)) {
                                if (!former_lookup.generation)
                                        former_lookup.generation = 1;
                                former_lookup.hit_generation = former_lookup.generation;
                                former_lookup.hit_hash = hash;
                                former_lookup.hit_length = length;
                                former_lookup.hit_index = index;
                                return index;
                        }
                }
                at = (at + 1) & (env_index_slots - 1);
        }
        return shell_var_count;
}

static inline int same(const char *held, const char *name, u64 length)
{
        if (length == 1)
                return held[0] == name[0];
        for (u64 at = 0; at < length; at++)
                if (held[at] != name[at])
                        return 0;
        return 1;
}

/* The check of the name found last, the shell's C, in front of each probe. */
#define FIND(lookup, probe)                                                     \
        __attribute__((noinline)) static u64 find_##probe(const char *name, u64 length, u64 hash) \
        {                                                                       \
                if (lookup.hit_generation && lookup.hit_generation == lookup.generation && \
                    lookup.hit_hash == hash && lookup.hit_length == length &&    \
                    lookup.hit_index < shell_var_count &&                        \
                    same(shell_vars[lookup.hit_index].text, name, length))       \
                        return lookup.hit_index;                                 \
                return probe(name, length, hash);                                \
        }
FIND(former_lookup, former_probe)
FIND(env_lookup, env_find_hashed_span_probe)

static const struct { const char *name; u64 weight; } common[] = {
        {"IFS", 6453},
        {"ac_val", 5370},
        {"as_lineno", 2942},
        {"ac_var", 2154},
        {"LINENO", 2128},
        {"as_lineno_stack", 1994},
        {"as_nl", 1663},
        {"PATH", 1589},
        {"PIPESTATUS", 1329},
        {"ac_try_echo", 1215},
        {"presentlang", 960},
        {"desiredlang", 901},
        {"CPPFLAGS", 896},
        {"as_me", 862},
        {"ac_status", 752},
        {"CFLAGS", 717},
        {"ac_ext", 717},
        {"ac_retval", 608},
        {"LDFLAGS", 606},
        {"ac_try", 553},
        {"HOME", 456},
        {"XDG_RUNTIME_DIR", 456},
        {"ac_res", 443},
        {"ac_exeext", 407},
        {"CC", 391},
        {"SHELL", 367},
        {"ac_objext", 352},
        {"LOGNAME", 344},
        {"USER", 344},
        {"SHLVL", 344},
        {"TMPDIR", 344},
        {"LANG", 343},
        {"OPTIND", 343},
        {"as_nop", 278},
        {"ac_compile", 274},
        {"LIBS", 259},
        {"PWD", 248},
        {"as_dir", 234},
        {"useit", 212},
        {"lang", 210},
        {"ac_link", 194},
        {"ac_delim", 180},
        {"LC_ALL", 165},
        {"ac_cache", 165},
        {"ac_header", 137},
        {"OLDPWD", 136},
        {"ac_executable_extension", 136},
        {"ac_c_werror_flag", 133},
        {"DUALCASE", 131},
        {"LANGUAGE", 131},
        {"ac_file", 126},
        {"CONFIG_SHELL", 125},
        {"INST_LINGUAS", 125},
        {"POFILES", 124},
        {"UPDATEPOFILES", 124},
        {"DUMMYPOFILES", 124},
        {"GMOFILES", 124},
        {"CATALOGS", 124},
        {"desiredlanguages", 121},
        {"XDG_SESSION_TYPE", 120},
        {"MOTD_SHOWN", 120},
        {"SSH_CONNECTION", 120},
        {"XDG_SESSION_CLASS", 120},
        {"XDG_SESSION_ID", 120},
};

static u64 hash_of(const char *p, u64 n)
{
        u64 h = 5381;

        for (u64 i = 0; i < n; i++)
                h = h * 33 + (unsigned char)p[i];
        return h;
}

#define STREAM 65536

int main(void)
{
        static envprobe_variable variables[1024];
        static envprobe_slot slots[2048];
        static char texts[1024][40], queries[STREAM][24];
        static u64 lengths[STREAM], hashes[STREAM];
        u64 count = 0, total = 0;

        for (u64 i = 0; i < sizeof(common) / sizeof(common[0]); i++, count++) {
                u64 length = 0;

                while (common[i].name[length])
                        length++;
                memcpy(texts[count], common[i].name, length);
                texts[count][length] = '=';
                variables[count] = (envprobe_variable){texts[count], hash_of(texts[count], length), length};
                total += common[i].weight;
        }
        for (; count < 972; count++) {
                u64 length = 4 + next() % 20;

                for (u64 c = 0; c < length; c++)
                        texts[count][c] = "abcdefghijklmnop_ACDEFLS"[next() % 24];
                texts[count][length] = '=';
                variables[count] = (envprobe_variable){texts[count], hash_of(texts[count], length), length};
        }
        shell_vars = variables;
        shell_var_count = count;
        env_index = slots;
        env_index_slots = 2048;
        for (u64 i = 0; i < count; i++) {
                u64 at = variables[i].hash & 2047;

                while (slots[at].index_plus_one)
                        at = (at + 1) & 2047;
                slots[at] = (envprobe_slot){variables[i].hash, variables[i].name_length, i + 1};
        }

        for (u64 i = 0; i < STREAM; i++) {
                u64 pick = next() % (total + total / 3), length = 0;
                const char *from = 0;

                if (i && pick >= total) {
                        memcpy(queries[i], queries[i - 1], 24);
                        lengths[i] = lengths[i - 1];
                        hashes[i] = hashes[i - 1];
                        continue;
                }
                for (u64 c = 0; c < sizeof(common) / sizeof(common[0]) && !from; c++) {
                        if (pick < common[c].weight)
                                from = common[c].name;
                        else
                                pick -= common[c].weight;
                }
                if (!from)
                        from = common[0].name;
                while (from[length])
                        length++;
                memcpy(queries[i], from, length);
                queries[i][length] = '=';
                lengths[i] = length;
                hashes[i] = hash_of(queries[i], length);
        }

        env_lookup.generation = former_lookup.generation = 1;
        for (u64 i = 0; i < STREAM; i++) {
                u64 want = find_former_probe(queries[i], lengths[i], hashes[i]);
                u64 got = find_env_find_hashed_span_probe(queries[i], lengths[i], hashes[i]);

                checks++;
                if ((want != got || memory_compare(&former_lookup, &env_lookup, sizeof(env_lookup))) && bad++ < 8)
                        printf("  FAIL query %lu length %lu: %lu want %lu\n", i, lengths[i], got, want);
        }

        u64 best_former = ~0ul, best_body = ~0ul, sink = 0, sink_body = 0;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 10; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink += find_former_probe(queries[i], lengths[i], hashes[i]);
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 10; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink_body += find_env_find_hashed_span_probe(queries[i], lengths[i], hashes[i]);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != sink_body)
                bad++;
        printf("  %d lookups drawn as configure makes them, 10 times: C probe %lu ticks, assembly %lu, asm/C %lu%%\n",
               STREAM, best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 env_find_hashed_span_probe: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_envprobe */

#ifdef CHECK_native_funcslot
/* ARM64 exec_function_slot lifted verbatim from src/sh/exec.c, against the
   walk it replaced calling memory_compare lifted from lib.c: a table the size
   libtool's ltmain.sh defines, 118 functions, and lookups shaped like the
   755 that ltmain.sh --help and a --mode=compile made -- eval, test, unset
   and the rest, one in five a function -- then both timed over that stream. */
#include "funcslot.h"
#include "floodlight_lib.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct { unsigned char *name; u64 name_room, name_hash, name_length; int body; u64 active;
                 unsigned char readonly, exported; unsigned char *environment; u64 environment_room;
                 unsigned char environment_valid, special_kind; } funcslot_function;
typedef struct { u64 x, y; } funcslot_named;

funcslot_function *exec_functions;
u64 exec_function_count;
u64 exec_function_recent = ~0ul;

u64 exec_function_slot(const char *, funcslot_named);
int memory_compare(const void *, const void *, u64);
void *memcpy(void *, const void *, u64);

static u64 checks, bad, former_recent = ~0ul;

__attribute__((noinline)) static u64 former(const char *name, funcslot_named named)
{
#define MATCHES(index) (exec_functions[index].name_hash == named.x && \
                        exec_functions[index].name_length == named.y && \
                        !memory_compare(exec_functions[index].name, name, named.y))
        if (former_recent < exec_function_count && exec_functions[former_recent].body && MATCHES(former_recent))
                return former_recent;
        for (u64 index = 0; index < exec_function_count; index++) {
                if (index == former_recent || !exec_functions[index].body)
                        continue;
                if (MATCHES(index)) {
                        former_recent = index;
                        return index;
                }
        }
        return ~0ul;
#undef MATCHES
}

static u64 hash_of(const char *p, u64 n)
{
        u64 h = 5381;

        for (u64 i = 0; i < n; i++)
                h = h * 33 + (unsigned char)p[i];
        return h;
}

static const struct { const char *name; u64 weight; } commands[] = {
        {"eval", 232}, {"test", 166}, {"unset", 50}, {"shift", 36}, {"continue", 32}, {"set", 17},
        {"printf", 12}, {"case", 8}, {"exit", 6}, {"return", 6}, {"echo", 5}, {":", 5},
};

#define STREAM 65536

int main(void)
{
        static funcslot_function functions[118];
        static char names[118][32], queries[STREAM][32];
        static funcslot_named named[STREAM];
        u64 total = 0;

        for (u64 i = 0; i < 118; i++) {
                u64 length = 5 + next() % 14;

                memcpy(names[i], "func_", 5);
                for (u64 c = 5; c < length; c++)
                        names[i][c] = "abcdefghilmnoprstu_"[next() % 19];
                functions[i] = (funcslot_function){(unsigned char *)names[i], 32,
                                                   hash_of(names[i], length), length, 1};
        }
        exec_functions = functions;
        exec_function_count = 118;
        for (u64 i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
                total += commands[i].weight;
        for (u64 i = 0; i < STREAM; i++) {
                const char *from;
                u64 length = 0;

                if (next() % 5 == 0) {
                        from = names[next() % 118];
                } else {
                        u64 pick = next() % total, c = 0;

                        while (pick >= commands[c].weight)
                                pick -= commands[c++].weight;
                        from = commands[c].name;
                }
                while (from[length])
                        length++;
                memcpy(queries[i], from, length + 1);
                named[i] = (funcslot_named){hash_of(queries[i], length), length};
        }

        for (u64 i = 0; i < STREAM; i++) {
                u64 want = former(queries[i], named[i]), got = exec_function_slot(queries[i], named[i]);

                checks++;
                if ((want != got || former_recent != exec_function_recent) && bad++ < 8)
                        printf("  FAIL query %lu: %lu want %lu\n", i, got, want);
        }

        u64 best_former = ~0ul, best_body = ~0ul, sink = 0, sink_body = 0;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (u64 i = 0; i < STREAM; i++)
                        sink += former(queries[i], named[i]);
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (u64 i = 0; i < STREAM; i++)
                        sink_body += exec_function_slot(queries[i], named[i]);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != sink_body)
                bad++;
        printf("  %d lookups against 118 functions, as ltmain.sh makes them: C walk %lu ticks, assembly %lu, asm/C %lu%%\n",
               STREAM, best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 exec_function_slot: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_funcslot */

#ifdef CHECK_native_assignment
/* ARM64 shell_assignment_kind lifted verbatim from src/sh/shell.c, against
   the C it replaced calling string_span lifted from lib.c, over words drawn
   the way the parser meets them: the commonest of 1.36 million words met
   reading five configure scripts, test/run and two ltmain.sh, as often as
   they came, and the rest made in its proportions -- over half beginning
   with a byte no name can, a tenth assignments -- then both timed. */
#include "assignment.h"
#include "assignment_lib.h"
#include "wordkind.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

unsigned char shell_assignment_kind(const char *, u64 *);
unsigned char parse_word_kind(const char *, u64, u64 *);
u64 string_span(const char *, const unsigned char *);
u64 string_span_max(const char *, u64, const unsigned char *);
void *memcpy(void *, const void *, u64);
unsigned char string_set_name[256], expand_literal_set[256];

static u64 checks, bad;

unsigned char shell_assignment_subscript(const char *word, u64 length, u64 *name_length)
{
        const char *at = word + length + 1, *stop = 0;

        for (; *at; at++)
                if (*at == ']' && (at[1] == '=' || (at[1] == '+' && at[2] == '='))) {
                        stop = at + 1;
                        break;
                }
        if (stop && stop - (word + length) > 2)
                length = (u64)(stop - word);
        if (name_length)
                *name_length = length;
        if (word[length] == '=')
                return 1;
        return word[length] == '+' && word[length + 1] == '=' ? 2 : 0;
}

__attribute__((noinline)) static unsigned char former(const char *word, u64 *name_length)
{
        u64 length = string_span(word, string_set_name);

        if (!length || (word[0] >= '0' && word[0] <= '9')) {
                if (name_length)
                        *name_length = length;
                return 0;
        }
        if (word[length] == '[')
                return shell_assignment_subscript(word, length, name_length);
        if (name_length)
                *name_length = length;
        if (word[length] == '=')
                return 1;
        return word[length] == '+' && word[length + 1] == '=' ? 2 : 0;
}

/* What the parser asked each word before parse_word_kind: the assignment
   question, in assembly, then whether expansion leaves it alone. */
__attribute__((noinline)) static _Bool literal(const char *word, u64 length)
{
        return string_span_max(word, length, expand_literal_set) == length || (length == 1 && word[0] == '[');
}

unsigned char parse_word_kind_subscript(const char *text, u64 length, u64 *name_length)
{
        return shell_assignment_kind(text, name_length) | (literal(text, length) ? 4 : 0);
}

__attribute__((noinline)) static unsigned char two_questions(const char *text, u64 length, u64 *name_length)
{
        return shell_assignment_kind(text, name_length) | (literal(text, length) ? 4 : 0);
}

static const struct { const char *word; u64 weight; } common[] = {
        {"e", 567},
        {"printf", 358},
        {"test", 303},
        {"\"%s\\n\"", 271},
        {"=", 146},
        {"fi", 141},
        {"*", 140},
        {":", 139},
        {"if", 93},
        {"%s", 85},
        {"\"$host_os\"", 77},
        {"6", 74},
        {"else", 73},
        {"}", 67},
        {"yes", 62},
        {"\"$LINENO\"", 59},
        {"-f", 58},
        {"eval", 57},
        {"then", 56},
        {"rm", 55},
        {"mingw*", 52},
        {"confdefs.h", 50},
        {"''", 50},
        {"$host_os", 45},
        {"as_dir", 43},
        {"conftest.$ac_ext", 42},
        {"{", 42},
        {"func_mode_link", 41},
        {"-n", 41},
        {"conftest.$ac_objext", 41},
        {"conftest.beam", 40},
        {"core", 40},
        {"windows*", 40},
        {"-z", 40},
        {"cat", 39},
        {"echo", 38},
        {"-", 33},
        {"$as_nop", 33},
        {"*yes", 32},
        {"xyes", 31},
        {"darwin*", 31},
        {"\"(cached) \"", 31},
        {"conftest.err", 30},
        {"1", 30},
        {"$PATH", 29},
        {"no", 29},
};

#define STREAM 65536

int main(void)
{
        static char words[STREAM][40];
        u64 total = 0;

        for (int c = 0; c < 256; c++)
                string_set_name[c] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                     (c >= '0' && c <= '9') || c == '_';
        for (u64 i = 0; i < sizeof(common) / sizeof(common[0]); i++)
                total += common[i].weight;
        for (u64 i = 0; i < STREAM; i++) {
                char *word = words[i];
                u64 pick = next() % (total * 100 / 29);

                if (pick < total) {
                        u64 c = 0, length = 0;

                        while (pick >= common[c].weight)
                                pick -= common[c++].weight;
                        while (common[c].word[length])
                                length++;
                        memcpy(word, common[c].word, length + 1);
                        continue;
                }
                u64 kind = next() % 100, length = 1 + next() % 16, at = 0;

                if (kind < 55)
                        word[at++] = "\"$-*'.{"[next() % 7];
                for (; at < length; at++)
                        word[at] = "abcdefgh_ACDLNOS0123$."[next() % 22];
                if (kind >= 85) {
                        word[at++] = '=';
                        word[at++] = '$';
                        word[at++] = 'x';
                }
                word[at] = 0;
        }

        for (u64 i = 0; i < STREAM; i++) {
                u64 want_length = 7, got_length = 7;
                unsigned char want = former(words[i], &want_length);
                unsigned char got = shell_assignment_kind(words[i], &got_length);

                checks++;
                if ((want != got || want_length != got_length) && bad++ < 8)
                        printf("  FAIL %s: %d/%lu want %d/%lu\n", words[i], got, got_length, want, want_length);
        }

        u64 best_former = ~0ul, best_body = ~0ul, sink = 0, sink_body = 0, length;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 10; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink += former(words[i], &length) + length;
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 10; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink_body += shell_assignment_kind(words[i], &length) + length;
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != sink_body)
                bad++;
        printf("  %d words drawn as the parser meets them, 10 times: C with string_span %lu ticks, assembly %lu, asm/C %lu%%\n",
               STREAM, best_former, best_body, best_body * 100 / (best_former ? best_former : 1));

        /* parse_word_kind: both questions in one walk, against the two calls. */
        static u64 lengths[STREAM];
        for (int c = 0; c < 256; c++)
                expand_literal_set[c] = c != 0;
        for (const char *at = "'\"\\$`*?[{~<>("; *at; at++)
                expand_literal_set[(unsigned char)*at] = 0;
        for (u64 i = 0; i < STREAM; i++) {
                u64 want_length = 7, got_length = 7;

                while (words[i][lengths[i]])
                        lengths[i]++;
                unsigned char want = two_questions(words[i], lengths[i], &want_length);
                unsigned char got = parse_word_kind(words[i], lengths[i], &got_length);

                checks++;
                if ((want != got || want_length != got_length) && bad++ < 8)
                        printf("  FAIL kind %s: %d/%lu want %d/%lu\n", words[i], got, got_length, want, want_length);
        }
        best_former = best_body = ~0ul;
        sink = sink_body = 0;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 10; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink += two_questions(words[i], lengths[i], &length) + length;
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 10; r++)
                        for (u64 i = 0; i < STREAM; i++)
                                sink_body += parse_word_kind(words[i], lengths[i], &length) + length;
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != sink_body)
                bad++;
        printf("  the same words, 10 times: two calls %lu ticks, parse_word_kind %lu, asm/C %lu%%\n",
               best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 shell_assignment_kind and parse_word_kind: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_assignment */

#ifdef CHECK_native_arguments
/* ARM64 argument_option_take lifted verbatim from src/lib.util.c, against
   the four C functions it folds -- argument_next, argument_option_short,
   argument_option_long and argument_value, with string_first_of and
   string_compare_max lifted from lib.c -- over whole option loops as tools
   run them: ls -la dir, ls -l --color=auto dir, ls -1 --sort=time -r a b,
   ls --almost-all -h against ls's own table of 49 rows, and wc -l file,
   wc -lwc a b, wc --lines f against wc's, every call compared and then
   both timed. */
#include "arguments.h"
#include "arguments_lib.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef struct { u64 argc; char **argv; u64 at; char *letters, *word; unsigned char operands_only, long_option;
                 char *attached; u64 name_length; } argument_cursor;
typedef struct { const char *name; unsigned char letter, mode; unsigned short selection; } argument_option;
typedef struct { char *value; unsigned short selection; unsigned char letter, mode, optional; } argument_match;
enum { ARGUMENT_END, ARGUMENT_OPERAND = 256, ARGUMENT_LONG, ARGUMENT_UNKNOWN = -1, ARGUMENT_MISSING = -2,
       ARGUMENT_UNEXPECTED = -3 };

int argument_option_take(argument_cursor *, const argument_option *, _Bool, argument_match *);
char *string_first_of(const char *, int);
int string_compare_max(const char *, const char *, u64);
int memory_compare(const void *, const void *, u64);
u64 strlen(const char *);

static u64 checks, bad;

static int argument_next(argument_cursor *cursor)
{
        for (;;) {
                if (cursor->letters && *cursor->letters)
                        return (unsigned char)*cursor->letters++;
                cursor->letters = 0;
                cursor->attached = 0;
                cursor->long_option = 0;
                if (cursor->at >= cursor->argc)
                        return ARGUMENT_END;
                cursor->word = cursor->argv[cursor->at++];
                if (cursor->operands_only || cursor->word[0] != '-' || !cursor->word[1])
                        return ARGUMENT_OPERAND;
                if (cursor->word[1] != '-') {
                        cursor->letters = cursor->word + 1;
                        continue;
                }
                if (!cursor->word[2]) {
                        cursor->operands_only = 1;
                        continue;
                }
                char *name = cursor->word + 2, *mark = string_first_of(name, '=');
                cursor->long_option = 1;
                cursor->name_length = mark ? (u64)(mark - name) : strlen(name);
                cursor->attached = mark ? mark + 1 : 0;
                return ARGUMENT_LONG;
        }
}

static char *argument_value(argument_cursor *cursor, _Bool required)
{
        if (cursor->long_option) {
                if (cursor->attached)
                        return cursor->attached;
        } else if (cursor->letters && *cursor->letters) {
                char *value = cursor->letters;
                cursor->letters = 0;
                return value;
        }
        return required && cursor->at < cursor->argc ? cursor->argv[cursor->at++] : 0;
}

static const argument_option *argument_option_short(const argument_option *options, unsigned char letter)
{
        for (; options && options->name; options++)
                if (options->letter == letter || (!options->letter && string_first_of(options->name, letter)))
                        return options;
        return 0;
}

static const argument_option *argument_option_long(const argument_option *options, const char *name,
                                                   u64 length, _Bool prefix)
{
        const argument_option *candidate = 0;

        if (!length)
                return 0;
        for (; options && options->name; options++) {
                if (!options->letter || string_compare_max(options->name, name, length))
                        continue;
                if (!options->name[length])
                        return options;
                if (prefix) {
                        if (candidate)
                                return 0;
                        candidate = options;
                }
        }
        return candidate;
}

__attribute__((noinline)) static int former(argument_cursor *cursor, const argument_option *options, _Bool prefix,
                                            argument_match *match)
{
        int token = argument_next(cursor);

        *match = (argument_match){0};
        if (token == ARGUMENT_END || token == ARGUMENT_OPERAND) {
                match->value = cursor->word;
                return token;
        }
        const argument_option *option = cursor->long_option
            ? argument_option_long(options, cursor->word + 2, cursor->name_length, prefix)
            : argument_option_short(options, (unsigned char)token);
        match->letter = cursor->long_option ? (option ? option->letter : 0) : (unsigned char)token;
        if (!option || (!cursor->long_option && (option->mode & 4)))
                return ARGUMENT_UNKNOWN;
        match->mode = option->mode;
        match->selection = option->selection;
        match->optional = (option->mode & 2) || (cursor->long_option && (option->mode & 8));
        _Bool required = (option->mode & 1) && !match->optional;
        if (cursor->attached && !match->optional && !required)
                return ARGUMENT_UNEXPECTED;
        if (required || match->optional) {
                match->value = argument_value(cursor, required);
                if (required && !match->value)
                        return ARGUMENT_MISSING;
        }
        return match->letter;
}

static const argument_option options_ls[] = {
    {"all", 'a', 0, 7},
    {"almost-all", 'A', 0, 7},
    {"author", '8', 4},
    {"escape", 'b', 0, 7},
    {"block-size", '7', 1 | 4, 7},
    {"ignore-backups", 'B'},
    {"color", 'K', 8},
    {"directory", 'd'},
    {"dired", 'D', 0, 7},
    {"classify", 'E', 8 | 4},
    {"file-type", 'j', 4, 7},
    {"format", 'J', 1 | 4, 7},
    {"full-time", 'M', 4, 7},
    {"group-directories-first", 'O', 4},
    {"no-group", 'G'},
    {"human-readable", 'h', 0, 7},
    {"si", 'P', 4, 7},
    {"dereference-command-line", 'H', 0, 7},
    {"dereference-command-line-symlink-to-dir", 'V', 4, 7},
    {"hide", 'W', 1 | 4},
    {"hyperlink", 'y', 8 | 4},
    {"indicator-style", 'Y', 1 | 4, 7},
    {"inode", 'i'},
    {"ignore", 'I', 1},
    {"kibibytes", 'k'},
    {"dereference", 'L', 0, 7},
    {"numeric-uid-gid", 'n', 0, 7},
    {"literal", 'N', 0, 7},
    {"hide-control-chars", 'q', 0, 7},
    {"show-control-chars", '2', 4, 7},
    {"quote-name", 'Q', 0, 7},
    {"quoting-style", 'z', 1 | 4, 7},
    {"reverse", 'r'},
    {"recursive", 'R'},
    {"size", 's'},
    {"sort", '3', 1 | 4, 7},
    {"time", '4', 1 | 4, 7},
    {"time-style", '5', 1 | 4, 7},
    {"tabsize", 'T', 1},
    {"width", 'w', 1},
    {"context", 'Z'},
    {"zero", '6', 4, 7},
    {"cu", 0, 0, 7},
    {"Cglmox", 0, 0, 7},
    {"f", 0, 0, 7},
    {"Fp", 0, 0, 7},
    {"StUvX", 0, 0, 7},
    {"1", 0},
    {0},
};
static const argument_option options_wc[] = {
    {"bytes", 'c'}, {"chars", 'm'}, {"lines", 'l'}, {"files0-from", 'F', 5}, {"max-line-length", 'L'},
    {"words", 'w'}, {"total", 'T', 5}, {0},
};

static char *ls1[] = {"-la", "dir"}, *ls2[] = {"-l", "--color=auto", "dir"},
            *ls3[] = {"-1", "--sort=time", "-r", "a", "b"}, *ls4[] = {"--almost-all", "-h"},
            *wc1[] = {"-l", "file"}, *wc2[] = {"-lwc", "a", "b"}, *wc3[] = {"--lines", "f"};
static const struct { char **argv; u64 argc; const argument_option *table; } runs[] = {
        {ls1, 2, options_ls}, {ls2, 3, options_ls}, {ls3, 5, options_ls}, {ls4, 2, options_ls},
        {wc1, 2, options_wc}, {wc2, 3, options_wc}, {wc3, 2, options_wc},
};

static u64 loop(int body)
{
        u64 sum = 0;

        for (u64 r = 0; r < sizeof(runs) / sizeof(runs[0]); r++) {
                argument_cursor cursor = {runs[r].argc, runs[r].argv};
                argument_match match;
                int got;

                do {
                        got = body ? argument_option_take(&cursor, runs[r].table, 1, &match)
                                   : former(&cursor, runs[r].table, 1, &match);
                        sum += (u64)got + (u64)match.value;
                } while (got != ARGUMENT_END);
        }
        return sum;
}

int main(void)
{
        for (u64 r = 0; r < sizeof(runs) / sizeof(runs[0]); r++) {
                argument_cursor want = {runs[r].argc, runs[r].argv}, got = want;

                for (int call = 0; call < 16; call++) {
                        argument_match a, b;
                        int x = former(&want, runs[r].table, 1, &a), y = argument_option_take(&got, runs[r].table, 1, &b);

                        checks++;
                        if ((x != y || memory_compare(&want, &got, sizeof(want)) || a.value != b.value ||
                             a.letter != b.letter || a.mode != b.mode || a.optional != b.optional ||
                             a.selection != b.selection) && bad++ < 8)
                                printf("  FAIL run %lu call %d: %d want %d\n", r, call, y, x);
                        if (x == ARGUMENT_END)
                                break;
                }
        }

        u64 best_former = ~0ul, best_body = ~0ul, sink = 0, sink_body = 0;
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 20000; r++)
                        sink += loop(0);
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 20000; r++)
                        sink_body += loop(1);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != sink_body)
                bad++;
        printf("  seven option loops of ls and wc, 20000 times: C %lu ticks, assembly %lu, asm/C %lu%%\n",
               best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 argument_option_take: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_arguments */

#ifdef CHECK_native_floodlight
/* ARM64 floodlight_status_clear lifted verbatim from src/sh/builtin.c,
   against the line walk it replaced -- memory_first_of to each newline and
   memory_compare on the two names, both lifted from lib.c -- over a real
   /proc/self/status and random edits of it at every alignment, then both
   timed over that file, which every applet reads once as it starts. */
#include "floodlight.h"
#include "floodlight_lib.h"

#define NATIVE_SEED 0x243f6a8885a308d3ull
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

int floodlight_status_clear(const u8 *, u64);
void *memory_first_of(const void *, int, u64);
int memory_compare(const void *, const void *, u64);
void *memcpy(void *, const void *, u64);
void *memmove(void *, const void *, u64);

static u64 checks, bad;

static const char status[] =
        "Name:\tcat\n"
        "Umask:\t0022\n"
        "State:\tR (running)\n"
        "Tgid:\t2112676\n"
        "Ngid:\t0\n"
        "Pid:\t2112676\n"
        "PPid:\t2112672\n"
        "TracerPid:\t0\n"
        "Uid:\t1000\t1000\t1000\t1000\n"
        "Gid:\t1000\t1000\t1000\t1000\n"
        "FDSize:\t64\n"
        "Groups:\t98 963 983 985 988 989 992 995 998 1000 \n"
        "NStgid:\t2112676\n"
        "NSpid:\t2112676\n"
        "NSpgid:\t2112676\n"
        "NSsid:\t2112676\n"
        "Kthread:\t0\n"
        "VmPeak:\t    3244 kB\n"
        "VmSize:\t    3244 kB\n"
        "VmLck:\t       0 kB\n"
        "VmPin:\t       0 kB\n"
        "VmHWM:\t    1804 kB\n"
        "VmRSS:\t    1804 kB\n"
        "RssAnon:\t     100 kB\n"
        "RssFile:\t    1704 kB\n"
        "RssShmem:\t       0 kB\n"
        "VmData:\t     468 kB\n"
        "VmStk:\t     132 kB\n"
        "VmExe:\t      28 kB\n"
        "VmLib:\t    1704 kB\n"
        "VmPTE:\t      44 kB\n"
        "VmSwap:\t       0 kB\n"
        "HugetlbPages:\t       0 kB\n"
        "CoreDumping:\t0\n"
        "THP_enabled:\t1\n"
        "untag_mask:\t0xffffffffffffffff\n"
        "Threads:\t1\n"
        "SigQ:\t2/239201\n"
        "SigPnd:\t0000000000000000\n"
        "ShdPnd:\t0000000000000000\n"
        "SigBlk:\t0000000000000000\n"
        "SigIgn:\t0000000000000000\n"
        "SigCgt:\t0000000000000000\n"
        "CapInh:\t0000000000000000\n"
        "CapPrm:\t0000000000000000\n"
        "CapEff:\t0000000000000000\n"
        "CapBnd:\t000001ffffffffff\n"
        "CapAmb:\t0000000000000000\n"
        "NoNewPrivs:\t0\n"
        "Seccomp:\t0\n"
        "Seccomp_filters:\t0\n"
        "Speculation_Store_Bypass:\tthread vulnerable\n"
        "SpeculationIndirectBranch:\tconditional enabled\n"
        "Cpus_allowed:\tffffffff\n"
        "Cpus_allowed_list:\t0-31\n"
        "Mems_allowed:\t00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n"
        "Mems_allowed_list:\t0\n"
        "voluntary_ctxt_switches:\t0\n"
        "nonvoluntary_ctxt_switches:\t0\n"
        "x86_Thread_features:\t\n"
        "x86_Thread_features_locked:\t\n";

__attribute__((noinline)) static int former(const u8 *text, u64 length)
{
        int mode = 0, count = 0;
        u64 at = 0;

        while (at < length) {
                const u8 *line = text + at, *stop = memory_first_of(line, '\n', length - at);
                u64 used, name, value;
                int *seen;

                if (!stop)
                        return 0;
                used = (u64)(stop - line);
                at += used + 1;
                if (used >= 8 && !memory_compare(line, "Seccomp:", 8))
                        name = 8, seen = &mode;
                else if (used >= 16 && !memory_compare(line, "Seccomp_filters:", 16))
                        name = 16, seen = &count;
                else
                        continue;
                if (used > 32 || *seen)
                        return 0;
                value = name;
                while (value < used && (line[value] == ' ' || line[value] == '\t'))
                        value++;
                if (value + 1 != used || line[value] != '0')
                        return 0;
                *seen = 1;
        }
        return mode && count;
}

int main(void)
{
        static u8 text[4096], room[4096 + 64];
        static const u8 edits[] = "\nSeccomp:_filters0 \tx";
        u64 length = sizeof(status) - 1, valid = 0;

        for (int round = 0; round < 4000; round++) {
                u64 used = length;

                memcpy(text, status, length);
                for (u64 e = 0, n = next() % 4; e < n; e++) {
                        u64 at = next() % used;
                        u8 byte = edits[next() % (sizeof(edits) - 1)];

                        switch (next() % 3) {
                        case 0: text[at] = byte; break;
                        case 1: memmove(text + at + 1, text + at, used - at); text[at] = byte; used++; break;
                        default: memmove(text + at, text + at + 1, used - at - 1); used--; break;
                        }
                }
                for (u64 offset = 0; offset < 32; offset += 1 + round % 7) {
                        memcpy(room + offset, text, used);
                        int want = former(room + offset, used), got = floodlight_status_clear(room + offset, used);

                        checks++;
                        valid += want;
                        if (want != got && bad++ < 8)
                                printf("  FAIL round %d offset %lu length %lu: %d want %d\n",
                                       round, offset, used, got, want);
                }
        }
        checks++;
        if (valid < 1000)
                bad++;

        u64 best_former = ~0ul, best_body = ~0ul, sink = 0;
        memcpy(room + 8, status, length);
        for (int trial = 0; trial < 9; trial++) {
                u64 start = ticks();
                for (int r = 0; r < 20000; r++)
                        sink += former(room + 8, length);
                u64 took = ticks() - start;
                best_former = took < best_former ? took : best_former;
                start = ticks();
                for (int r = 0; r < 20000; r++)
                        sink += floodlight_status_clear(room + 8, length);
                took = ticks() - start;
                best_body = took < best_body ? took : best_body;
        }
        checks++;
        if (sink != 9 * 2 * 20000)
                bad++;
        printf("  a %lu-byte status, 20000 times: line walk %lu ticks, assembly %lu, asm/C %lu%%\n",
               length, best_former, best_body, best_body * 100 / (best_former ? best_former : 1));
        printf("arm64 floodlight_status_clear: %lu checks | %lu failures\n", checks, bad);
        return bad ? 1 : 0;
}
#endif /* CHECK_native_floodlight */

#ifdef CHECK_native_series
/* Exact production ARM64 decimal-record loop, checked against libc output. */
#include "series.h"
#include <stdio.h>
#include <string.h>

unsigned long memory_decimal_series(unsigned char *, unsigned long,
                                    unsigned long, unsigned long,
                                    unsigned long, long);

int main(void)
{
        static const long steps[] = {0, 1, -1, 3, -3, 25, -25, 999, -999};
        unsigned char actual[2048], expected[2048];
        unsigned long checks = 0;
        for (unsigned width = 1; width <= 7; width++)
        {
                long limit = 1;
                for (unsigned i = 0; i < width; i++) limit *= 10;
                for (unsigned s = 0; s < sizeof(steps) / sizeof(steps[0]); s++)
                        for (unsigned offset = 0; offset < 16; offset++)
                                for (unsigned room = width + 3; room <= 1024; room += 17)
                                {
                                        long value = steps[s] < 0 ? limit - 1 : 0;
                                        unsigned length = width + 3, used = 0;
                                        memset(actual, 0xa5, sizeof(actual));
                                        memset(expected, 0xa5, sizeof(expected));
                                        char record[32];
                                        while (room - used >= length && value >= 0 && value < limit)
                                        {
                                                snprintf(record, sizeof(record), "[%0*ld]\n", width, value);
                                                memcpy(expected + offset + used, record, length);
                                                used += length;
                                                value += steps[s];
                                        }
                                        memcpy(actual + offset, expected + offset, length);
                                        unsigned long got = memory_decimal_series(actual + offset,
                                            room, length, 1, width + 1, steps[s]);
                                        checks++;
                                        if (got != used || memcmp(actual + offset, expected + offset, used))
                                                return printf("series mismatch width=%u step=%ld room=%u\n",
                                                              width, steps[s], room), 1;
                                        for (unsigned i = 0; i < sizeof(actual); i++)
                                                if ((i < offset || i >= offset + room) && actual[i] != 0xa5)
                                                        return printf("series bounds\n"), 1;
                                }
        }
        printf("native decimal series: %lu checks, 0 failures\n", checks);
        return 0;
}
#endif /* CHECK_native_series */

#ifdef CHECK_native_wide
/*
        The hunts at the lengths a vector body reaches, run on this machine.

        byte_hunts.c and bounded.c between them cover six routines at lengths
        up to a hundred and forty, and neither one names string_length or
        string_last_of at all: the arm64 lane has never executed either. Both
        of those grew a wide body, and a wide body that nothing long enough
        ever reaches passes every check while being wrong in every lane.

        So this is the same eight routines the differential suite carries,
        every alignment into a sixty four byte block, lengths across 32, 64,
        128, 256 and 320, and the hunted byte walked through every position
        including the last one inside a bound and the first one past it.
*/
#include "wide.h"

#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64   string_length(const char *);
u64   string_length_max(const char *, u64);
char *string_first_of(const char *, int);
char *string_first_of_or_end(const char *, int);
char *string_first_of_max(const char *, u64, int);
char *string_last_of(const char *, int);
char *string_last_of_or_end(const char *, int);
void *memory_first_of(const void *, int, u64);

static u64 r_len(const char *s)
{
        const char *p = s;
        while (*p) p++;
        return (u64)(p - s);
}

// Nothing for the terminator, which is the whole difference between this and
// strrchr; the library says so where it defines the two.
static char *r_last(const char *s, int c)
{
        const char *best = 0;
        if (!(char)c) return 0;
        for (; *s; s++) if (*s == (char)c) best = s;
        return (char *)best;
}

static u64 checks;
static u64 e_len, e_nlen, e_first, e_or_end, e_max, e_last, e_last_end, e_mchr;

static void one(const char *text, u64 size, int c)
{
        u64 bounds[6];

        checks += 5;
        if (string_length(text)            != r_len(text))          e_len++;
        if (string_first_of(text,c)        != r_first(text,c))      e_first++;
        if (string_first_of_or_end(text,c) != r_or_end(text,c))     e_or_end++;
        if (string_last_of(text,c)         != r_last(text,c))       e_last++;
        if (string_last_of_or_end(text,c)  != r_last_or_end(text,c)) e_last_end++;

        bounds[0] = 0; bounds[1] = 1; bounds[2] = size/2;
        bounds[3] = size; bounds[4] = size+1; bounds[5] = size+40;

        for (u64 i = 0; i < 6; i++) {
                u64 n = bounds[i];
                checks += 3;
                if (string_first_of_max(text,n,c) != r_max(text,n,c))   e_max++;
                if (memory_first_of(text,c,n)     != r_mchr(text,c,n))  e_mchr++;
                if (string_length_max(text,n)     != r_nlen(text,n))    e_nlen++;
        }
}

static const u64 sizes[] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,23,24,25,
        30,31,32,33,34,35,39,40,47,48,55,56,62,63,64,65,66,67,70,79,80,
        95,96,97,111,112,120,126,127,128,129,130,131,143,144,159,160,
        175,176,191,192,193,200,223,224,240,252,254,255,256,257,258,260,
        280,288,300,310,318,319,320};

#define SIZE_COUNT (sizeof(sizes)/sizeof(sizes[0]))

static const u64 offsets[] = {0,1,7,8,15,16,17,31,32,33,63};

#define OFFSET_COUNT (sizeof(offsets)/sizeof(offsets[0]))

static u8 field[1024] __attribute__((aligned(64)));

int main(void)
{
        char *base = (char *)field;

        // Every alignment a sixty four byte loop can begin on, against every
        // length in the ladder, with the byte where an edge that is off by
        // one gets it wrong.
        for (u64 off = 0; off < 72; off++)
                for (u64 s = 0; s < SIZE_COUNT; s++) {
                        u64 size = sizes[s];
                        char *text = base + off;

                        if (off + size + 48 >= sizeof(field)) continue;

                        for (u64 i = 0; i < sizeof(field); i++) field[i] = 'a';
                        text[size] = 0;

                        one(text, size, 'z');           // nowhere
                        one(text, size, 'a');           // everywhere
                        one(text, size, 0);             // the terminator

                        if (size) {
                                text[size-1] = 'z';
                                one(text, size, 'z');   // the last byte in
                                text[size-1] = 'a';
                        }

                        text[size+1] = 'z';
                        one(text, size, 'z');           // past the terminator
                        text[size+1] = 'a';

                        text[size/2] = (char)0xff;
                        one(text, size, 0xff);          // a high byte
                        text[size/2] = 'a';
                }

        // The byte at every position in turn, which is what a body that finds
        // the right vector and the wrong lane fails and nothing else does.
        for (u64 o = 0; o < OFFSET_COUNT; o++)
                for (u64 s = 0; s < SIZE_COUNT; s++) {
                        u64 off = offsets[o], size = sizes[s];
                        char *text = base + off;

                        if (off + size + 48 >= sizeof(field)) continue;

                        for (u64 i = 0; i < sizeof(field); i++) field[i] = 'a';
                        text[size] = 0;

                        for (u64 where = 0; where <= size + 2; where++) {
                                if (where == size) continue;
                                text[where] = 'z';
                                one(text, size, 'z');
                                text[where] = 'a';
                        }
                }

        printf("arm64 wide: %lu checks | length %lu | length_max %lu | first_of %lu"
               " | or_end %lu | first_max %lu | last_of %lu | last_or_end %lu | memchr %lu\n",
               checks, e_len, e_nlen, e_first, e_or_end, e_max, e_last, e_last_end, e_mchr);

        return (e_len||e_nlen||e_first||e_or_end||e_max||e_last||e_last_end||e_mchr) != 0;
}
#endif /* CHECK_native_wide */

#ifdef CHECK_native_bases
/*
        positive_into_base on native arm64 silicon.

        Correctness is already crossed with every architecture by verify.c;
        this lifted case establishes that the exact arm64 body also executes
        here, then times it against printf_render's former runtime-base C loop.
*/
#include "bases.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64 positive_into_base(u8 *, u64, u64, u8);

static u64 reference(u8 *into, u64 value, u64 base, u8 upper)
{
        u8 scratch[64];
        u64 have = 0;

        do {
                u64 digit = value % base;
                scratch[have++] = (u8)(digit < 10 ? '0' + digit
                                                     : (upper ? 'A' : 'a') + digit - 10);
                value /= base;
        } while (value);

        for (u64 i = 0; i < have; i++) into[i] = scratch[have - i - 1];
        return have;
}

// The lifted routine has a decimal tail relocation even though this case
// never takes it. Give the linker the exact no-terminator contract it expects.
u64 positive_into(u8 *into, u64 value)
{
        return reference(into, value, 10, 0);
}

static u8 got_room[96], want_room[96];

static int correctness(void)
{
        u64 checks = 0, bad = 0;

        for (u64 base = 2; base <= 36; base++)
                for (u8 upper = 0; upper <= 1; upper++)
                        for (u64 r = 0; r < 4096; r++) {
                                u64 value = r < 16 ? (u64[]){0,1,7,8,9,10,15,16,
                                                           63,64,255,256,4095,4096,
                                                           ~0ul-1,~0ul}[r] : next();
                                u64 off = (value + r + base) & 7;
                                for (u64 i = 0; i < sizeof(got_room); i++)
                                        got_room[i] = want_room[i] = 0xa5;

                                u64 gl = positive_into_base(got_room + 8 + off,
                                                            value, base, upper);
                                u64 wl = reference(want_room + 8 + off,
                                                   value, base, upper);
                                checks++;
                                if (gl != wl) { bad++; continue; }
                                for (u64 i = 0; i < sizeof(got_room); i++)
                                        if (got_room[i] != want_room[i]) {
                                                bad++;
                                                break;
                                        }
                        }

        printf("arm64 base correctness: %lu checks | %lu failures\n", checks, bad);
        return bad != 0;
}

#define COUNT 64
#define ROUNDS (1ul << 17)
#define TRIES 7

static u64 values[COUNT];
static u8 output[64];
static volatile u64 sink;

static __attribute__((noinline, noclone)) u64 old_runtime_base(u8 *into, u64 value,
                                                       u64 base, u8 upper)
{
        return reference(into, value, base, upper);
}

static u64 run_old(u64 base, u8 upper)
{
        u64 best = ~0ul;
        for (u64 t = 0; t < TRIES; t++) {
                u64 start = ticks();
                for (u64 r = 0; r < ROUNDS; r++)
                        sink += old_runtime_base(output, values[r & (COUNT - 1)],
                                                 base, upper);
                u64 took = ticks() - start;
                if (took < best) best = took;
        }
        return best;
}

static u64 run_asm(u64 base, u8 upper)
{
        u64 best = ~0ul;
        for (u64 t = 0; t < TRIES; t++) {
                u64 start = ticks();
                for (u64 r = 0; r < ROUNDS; r++)
                        sink += positive_into_base(output,
                                                   values[r & (COUNT - 1)],
                                                   base, upper);
                u64 took = ticks() - start;
                if (took < best) best = took;
        }
        return best;
}

static void row(const char *name, u64 base, u8 upper)
{
        u64 old = run_old(base, upper), assembly = run_asm(base, upper);
        printf("  %-18s old %lu  assembly %lu  assembly/old %lu%%\n",
               name, old, assembly, assembly * 100 / old);
}

int main(void)
{
        int bad = correctness();

        for (u64 i = 0; i < COUNT; i++) values[i] = next();
        values[0] = 0; values[1] = 7; values[2] = 8; values[3] = 15;
        values[4] = 16; values[5] = 07777; values[6] = ~0ul;

        printf("arm64 native base conversion, best of %d, %lu calls\n", TRIES, ROUNDS);
        row("binary", 2, 0);
        row("base 4", 4, 0);
        row("octal", 8, 0);
        row("hexadecimal lower", 16, 0);
        row("hexadecimal upper", 16, 1);
        row("base 32 lower", 32, 0);
        row("base 32 upper", 32, 1);
        row("base 36 lower", 36, 0);
        return bad;
}
#endif /* CHECK_native_bases */

#ifdef CHECK_native_base_field
#include <stdint.h>
#include <stdio.h>

typedef unsigned long positive;
typedef long bipolar;
typedef unsigned char p8;

static p8 output[2048];
static positive used, calls, lengths[1024];

static void capture(void *data, positive length)
{
        p8 *bytes = data;
        lengths[calls++] = length;
        for (positive i = 0; i < length; i++)
                output[used++] = bytes[i];
}

positive positive_into(p8 *out, positive value)
{
        p8 reverse[24];
        positive n = 0;
        do { reverse[n++] = (p8)('0' + value % 10); value /= 10; } while (value);
        for (positive i = 0; i < n; i++) out[i] = reverse[n - i - 1];
        return n;
}

#include "base_field.h"

extern void positive_to_base_field(void (*)(void *, positive), positive,
                                   positive, positive, bipolar, positive);
extern void writer_fill(void (*)(void *, positive), positive, p8);

int main(void)
{
        for (positive base = 2; base <= 36; base++)
                for (positive flags = 0; flags < 8; flags++)
                {
                        used = calls = 0;
                        positive style = '+' | ((positive)'0' << 8) |
                            ((positive)'x' << 16) | ((positive)2 << 24) |
                            ((flags & 1) << 26) | (((flags >> 1) & 1) << 27) |
                            (((flags >> 2) & 1) << 28);
                        positive_to_base_field(capture, 0xfffffffffffffffful,
                                               base, 80, flags & 1 ? 25 : -1,
                                               style);
                        if (!used || !calls || used < 25)
                                return 1;
                        for (positive i = 0; i < calls; i++)
                                if (!lengths[i])
                                        return 2;
                }

        used = calls = 0;
        writer_fill(capture, 257, 0xa5);
        if (used != 257 || calls != 257)
                return 3;
        for (positive i = 0; i < 257; i++)
                if (output[i] != 0xa5 || lengths[i] != 1)
                        return 4;

        puts("  arm64 native base field: correct");
        return 0;
}
#endif /* CHECK_native_base_field */

#ifdef CHECK_native_human
/* Exact lifted ARM64 compact binary-size formatter on native Apple silicon. */
#include "human.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64 positive_into_human_1024_string(u8 *, u64);
void positive_to_human_1024(void (*)(void *, u64), u64);

// The two relocations the lifted formatter reaches on its decimal paths.
__attribute__((noinline, noclone)) u64 positive_into(u8 *into, u64 value)
{
        return reference_into(into, value);
}
__attribute__((noinline, noclone)) u64 positive_into_string(u8 *into, u64 value)
{
        u64 length = reference_into(into, value);
        into[length] = 0;
        return length;
}
void positive_to_string(void (*write)(void *, u64), u64 value)
{
        u8 digits[24];
        u64 length = reference_into(digits, value);
        write(digits, length);
}

static __attribute__((noinline, noclone)) u64 former_human(u8 *into, u64 value)
{
        static u8 units[] = "BKMGTPE";
        u64 divisor = 1, unit = 0;

        while (value / divisor >= 1024 && unit < 6) {
                divisor *= 1024;
                unit++;
        }

        if (!unit) return positive_into_string(into, value);

        u64 quotient = value / divisor;
        u64 remainder = value % divisor;
        u64 length;

        if (quotient >= 10)
                length = reference_into(into, quotient + (remainder != 0));
        else {
                u64 fraction = (remainder * 10 + divisor - 1) / divisor;
                if (fraction == 10) { quotient++; fraction = 0; }
                if (quotient >= 10)
                        length = reference_into(into, quotient);
                else {
                        length = reference_into(into, quotient);
                        into[length++] = '.';
                        length += reference_into(into + length, fraction);
                }
        }

        into[length++] = units[unit];
        into[length] = 0;
        return length;
}

static u8 written[8];
static u64 written_length;
static u64 writer_calls;
static u64 call_lengths[4];

static void capture(void *data, u64 length)
{
        u8 *bytes = data;
        if (writer_calls < 4) call_lengths[writer_calls] = length;
        writer_calls++;
        for (u64 i = 0; i < length && written_length < sizeof(written); i++)
                written[written_length++] = bytes[i];
}

static int one(u64 value, u64 offset)
{
        u8 got[48], want[48];
        for (u64 i = 0; i < sizeof(got); i++) got[i] = want[i] = 0xa5;

        u64 gl = positive_into_human_1024_string(got + 16 + offset, value);
        u64 wl = former_human(want + 16 + offset, value);

        if (gl != wl) return 1;
        for (u64 i = 0; i < sizeof(got); i++) if (got[i] != want[i]) return 1;

        written_length = writer_calls = 0;
        positive_to_human_1024(capture, value);
        if (written_length != wl) return 1;
        for (u64 i = 0; i < wl; i++) if (written[i] != want[16 + offset + i]) return 1;

        int fractional = wl == 4 && want[17 + offset] == '.';
        int scaled = want[16 + offset + wl - 1] > '9';
        u64 expected = fractional ? 4 : (scaled ? 2 : 1);
        if (writer_calls != expected) return 1;
        if (fractional) {
                for (u64 i = 0; i < 4; i++) if (call_lengths[i] != 1) return 1;
        } else if (scaled) {
                if (call_lengths[0] != wl - 1 || call_lengths[1] != 1) return 1;
        } else if (call_lengths[0] != wl) return 1;
        return 0;
}

static int correctness(void)
{
        u64 checks = 0, bad = 0;

        for (u64 value = 0; value <= 65535; value++) {
                bad += one(value, value & 15);
                checks++;
        }

        u64 divisor = 1024;
        for (u64 unit = 1; unit <= 6; unit++) {
                u64 values[] = {
                    divisor - 1, divisor, divisor + 1,
                    9 * divisor - 1, 9 * divisor, 9 * divisor + 1,
                    10 * divisor - 1, 10 * divisor, 10 * divisor + 1,
                };
                for (u64 i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
                        bad += one(values[i], (i + unit) & 15);
                        checks++;
                }
                if (divisor <= (~0ul) / 1024) divisor *= 1024;
        }

        divisor = 1ul << 60;
        u64 tops[] = {15 * divisor, 15 * divisor + 1, ~0ul - 1, ~0ul};
        for (u64 i = 0; i < sizeof(tops) / sizeof(tops[0]); i++) {
                bad += one(tops[i], i);
                checks++;
        }

        for (u64 i = 0; i < 100000; i++) {
                bad += one(next(), i & 15);
                checks++;
        }

        printf("arm64 compact human: %lu checks | %lu failures\n", checks, bad);
        return bad != 0;
}

#define COUNT 64
#define ROUNDS (1ul << 18)
#define TRIES 7

static u64 values[4][COUNT];
static u8 output[8];
static volatile u64 sink;

static u64 run_once(u64 shape, int assembly)
{
        u64 start = ticks();
        for (u64 r = 0; r < ROUNDS; r++) {
                u64 value = values[shape][r & (COUNT - 1)];
                u64 length = assembly ? positive_into_human_1024_string(output, value)
                                      : former_human(output, value);
                sink += length + output[0] + output[length - 1];
        }
        return ticks() - start;
}

static void row(const char *name, u64 shape)
{
        u64 old = ~0ul, assembly = ~0ul;
        for (u64 t = 0; t < TRIES; t++) {
                u64 got_old, got_assembly;
                if (t & 1) {
                        got_assembly = run_once(shape, 1);
                        got_old = run_once(shape, 0);
                } else {
                        got_old = run_once(shape, 0);
                        got_assembly = run_once(shape, 1);
                }
                if (got_old < old) old = got_old;
                if (got_assembly < assembly) assembly = got_assembly;
        }
        printf("  %-12s old %lu  assembly %lu  assembly/old %lu%%\n",
               name, old, assembly, assembly * 100 / old);
}

int main(void)
{
        int bad = correctness();

        for (u64 i = 0; i < COUNT; i++) {
                u64 random = next();
                values[0][i] = random & 1023;
                values[1][i] = 1024 + random % (8 * 1024);
                values[2][i] = 10 * 1024 + random % (1014 * 1024);
                values[3][i] = random;
        }

        printf("arm64 native compact human, best of %d, %lu calls\n",
               TRIES, ROUNDS);
        row("plain", 0);
        row("fractional", 1);
        row("integer", 2);
        row("mixed u64", 3);
        return bad;
}
#endif /* CHECK_native_human */

#ifdef CHECK_native_human_nearest
/* Exact lifted ARM64 dd human formatter and wait decoder on Apple silicon. */
#include "human_nearest.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64 positive_into_human_nearest_string(u8 *, u64, u8);
int wait_status_code(u64);

static __attribute__((noinline, noclone)) u64 former_human(u8 *into, u64 n, u8 binary)
{
        u64 base = binary ? 1024 : 1000;
        u64 amount = n, tenths = 0, rounding = 0, exponent = 0;
        static u8 letters[] = {0, 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q'};
        u64 used = 0, fraction = 0;
        u8 point = 0;

        if (base <= amount) {
                do {
                        u64 ten = (amount % base) * 10 + tenths;
                        u64 two = (ten % base) * 2 + (rounding >> 1);
                        amount /= base;
                        tenths = ten / base;
                        rounding = two < base ? ((two + rounding) != 0)
                                              : 2 + (base < two + rounding);
                        exponent++;
                } while (base <= amount && exponent < 10);

                if (amount < 10) {
                        if (2 < rounding + (tenths & 1)) {
                                tenths++;
                                rounding = 0;
                                if (tenths == 10) { amount++; tenths = 0; }
                        }
                        if (amount < 10) {
                                point = 1;
                                fraction = tenths;
                                tenths = rounding = 0;
                        }
                }
        }

        if (5 < tenths + (0 < rounding + (amount & 1))) {
                amount++;
                if (amount == base && exponent < 10) {
                        exponent++;
                        point = 1;
                        fraction = 0;
                        amount = 1;
                }
        }

        used += reference_into(into + used, amount);
        if (point) { into[used++] = '.'; into[used++] = (u8)('0' + fraction); }
        into[used++] = ' ';
        if (exponent) into[used++] = !binary && exponent == 1 ? 'k' : letters[exponent];
        if (binary && exponent) into[used++] = 'i';
        into[used++] = 'B';
        into[used] = 0;
        return used;
}

static int one(u64 value, u8 binary, u64 offset, u8 guard)
{
        u8 got[64], want[64];
        for (u64 i = 0; i < sizeof(got); i++) got[i] = want[i] = guard;

        u64 gl = positive_into_human_nearest_string(got + 16 + (offset & 15),
                                                     value, binary);
        u64 wl = former_human(want + 16 + (offset & 15), value, binary);

        if (gl != wl || wl > 8) return 1;
        for (u64 i = 0; i < sizeof(got); i++) if (got[i] != want[i]) return 1;
        return 0;
}

static int correctness(void)
{
        static u8 guards[] = {0, 0x55, 0xff};
        static u64 fixed[] = {
            0, 1, 9, 10, 99, 999, 1000, 1023, 1024, 9999,
            999499, 999500, 999501, 999999, 1000000,
            1023 * 1024, 1024 * 1024 - 1, 1024 * 1024,
            ~0ul - 1, ~0ul,
        };
        u64 checks = 0, bad = 0;

        for (u64 binary = 0; binary < 2; binary++)
                for (u64 value = 0; value <= 65535; value++) {
                        bad += one(value, (u8)binary, value & 15,
                                   guards[(value >> 4) % sizeof(guards)]);
                        checks++;
                }

        for (u64 binary = 0; binary < 2; binary++)
                for (u64 offset = 0; offset < 16; offset++)
                        for (u64 g = 0; g < sizeof(guards); g++)
                                for (u64 i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
                                        bad += one(fixed[i], (u8)binary, offset, guards[g]);
                                        checks++;
                                }

        for (u64 binary = 0; binary < 2; binary++) {
                u64 base = binary ? 1024 : 1000, power = 1, serial = 0;
                for (u64 unit = 1; unit <= 6; unit++) {
                        if (power > ~0ul / base) break;
                        power *= base;
                        for (u64 delta = 0; delta <= 4096; delta++) {
                                if (power >= delta) {
                                        bad += one(power - delta, (u8)binary, serial & 15,
                                                   guards[serial % sizeof(guards)]);
                                        serial++; checks++;
                                }
                                if (power <= ~0ul - delta) {
                                        bad += one(power + delta, (u8)binary, serial & 15,
                                                   guards[serial % sizeof(guards)]);
                                        serial++; checks++;
                                }
                        }
                        u64 half = power / 2;
                        for (u64 q = 1; q <= 20; q++)
                                if (q <= (~0ul - half) / power) {
                                        u64 tie = q * power + half;
                                        bad += one(tie - 1, (u8)binary, serial++ & 15, 0xa5);
                                        bad += one(tie, (u8)binary, serial++ & 15, 0x5a);
                                        bad += one(tie + 1, (u8)binary, serial++ & 15, 0xff);
                                        checks += 3;
                                }
                }
        }

        for (u64 binary = 0; binary < 2; binary++)
                for (u64 i = 0; i < 65536; i++) {
                        bad += one(next(), (u8)binary, i & 15,
                                   guards[(i >> 4) % sizeof(guards)]);
                        checks++;
                }

        for (u64 raw = 0; raw <= 65535; raw++) {
                u64 signal = raw & 0x7f;
                int want = signal ? (int)(128 + signal) : (int)((raw >> 8) & 0xff);
                if (wait_status_code(raw) != want) bad++;
                checks++;
        }

        printf("arm64 nearest human/wait: %lu checks | %lu failures\n", checks, bad);
        return bad != 0;
}

#define COUNT 64
#define ROUNDS (1ul << 18)
#define TRIES 7

static u64 values[2][COUNT];
static u8 output[9];
static volatile u64 sink;

static u64 run_once(u8 binary, int assembly)
{
        u64 start = ticks();
        for (u64 r = 0; r < ROUNDS; r++) {
                u64 value = values[binary][r & (COUNT - 1)];
                u64 length = assembly
                    ? positive_into_human_nearest_string(output, value, binary)
                    : former_human(output, value, binary);
                sink += length + output[0] + output[length - 1];
        }
        return ticks() - start;
}

static void row(const char *name, u8 binary)
{
        u64 old = ~0ul, assembly = ~0ul;
        for (u64 t = 0; t < TRIES; t++) {
                u64 got_old, got_assembly;
                if (t & 1) {
                        got_assembly = run_once(binary, 1);
                        got_old = run_once(binary, 0);
                } else {
                        got_old = run_once(binary, 0);
                        got_assembly = run_once(binary, 1);
                }
                if (got_old < old) old = got_old;
                if (got_assembly < assembly) assembly = got_assembly;
        }
        printf("  %-10s old %lu  assembly %lu  assembly/old %lu%%\n",
               name, old, assembly, assembly * 100 / old);
}

int main(void)
{
        int bad = correctness();

        for (u64 i = 0; i < COUNT; i++) {
                values[0][i] = next();
                values[1][i] = next();
        }
        values[0][0] = 999999;
        values[0][1] = ~0ul;
        values[1][0] = 1023 * 1024;
        values[1][1] = ~0ul;

        printf("arm64 native nearest human, best of %d, %lu calls\n", TRIES, ROUNDS);
        row("SI mixed", 0);
        row("IEC mixed", 1);
        return bad;
}
#endif /* CHECK_native_human_nearest */

#ifdef CHECK_native_fields
/* Exact lifted ARM64 contiguous-field leaves on native Apple ARM silicon. */
#include "fields.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64 positive_into_padded(u8 *, u64, u64, u8);
u64 positive_into_pair(u8 *, u64);

// Satisfy the general-path relocation in the lifted body. The benchmark's
// bounded width-six/nine calls take their direct lanes before reaching it.
u64 positive_into(u8 *into, u64 value) { return reference_into(into, value); }

static int correctness(void)
{
        u8 got[128], want[128];
        u64 checks = 0, bad = 0;

        for (u64 value = 0; value < 100; value++)
                for (u64 off = 0; off < 8; off++) {
                        for (u64 i = 0; i < sizeof(got); i++) got[i] = want[i] = 0xa5;
                        u64 length = positive_into_pair(got + 16 + off, value);
                        want[16 + off] = (u8)('0' + value / 10);
                        want[17 + off] = (u8)('0' + value % 10);
                        checks++;
                        if (length != 2) { bad++; continue; }
                        for (u64 i = 0; i < sizeof(got); i++)
                                if (got[i] != want[i]) { bad++; break; }
                }

        for (u64 r = 0; r < 100000; r++) {
                u64 width = r & 1 ? 6 : 9;
                u64 limit = width == 6 ? 1000000 : 1000000000;
                u64 value = next() % limit;
                u64 off = r & 7;
                for (u64 i = 0; i < sizeof(got); i++) got[i] = want[i] = 0x5a;
                u8 digits[24];
                u64 dl = reference_into(digits, value), pad = width - dl;
                for (u64 i = 0; i < pad; i++) want[16 + off + i] = '0';
                for (u64 i = 0; i < dl; i++) want[16 + off + pad + i] = digits[i];
                u64 length = positive_into_padded(got + 16 + off, value, width, '0');
                checks++;
                if (length != width) { bad++; continue; }
                for (u64 i = 0; i < sizeof(got); i++)
                        if (got[i] != want[i]) { bad++; break; }
        }

        // Any pad at every width the direct lane takes, on both sides of
        // every power of ten and at random.
        static const u8 pads[3] = {' ', '#', '0'};
        for (u64 r = 0; r < 200000; r++) {
                u64 width = 2 + r % 8, power = 1;
                for (u64 k = 0; k < width; k++) power *= 10;
                u64 value = r < 1000 ? power / (1 + r % 3) - 1 + r % 3 : next() % (power + power / 8);
                u8 pad = pads[r % 3];
                u64 off = r & 7;
                for (u64 i = 0; i < sizeof(got); i++) got[i] = want[i] = 0x5a;
                u8 digits[24];
                u64 dl = reference_into(digits, value), fill = width > dl ? width - dl : 0;
                for (u64 i = 0; i < fill; i++) want[16 + off + i] = pad;
                for (u64 i = 0; i < dl; i++) want[16 + off + fill + i] = digits[i];
                u64 length = positive_into_padded(got + 16 + off, value, width, pad);
                checks++;
                if (length != fill + dl) { bad++; continue; }
                for (u64 i = 0; i < sizeof(got); i++)
                        if (got[i] != want[i]) { bad++; break; }
        }

        printf("arm64 contiguous fields: %lu checks | %lu failures\n", checks, bad);
        return bad != 0;
}

#define COUNT 64
#define ROUNDS (1ul << 17)
#define TRIES 7

static u64 values[COUNT];
static u8 output[24];
static volatile u64 sink;

static __attribute__((noinline, noclone)) u64 former_pair(u8 *into, u64 value)
{
        into[0] = (u8)('0' + (value / 10) % 10);
        into[1] = (u8)('0' + value % 10);
        return 2;
}

static __attribute__((noinline, noclone)) u64 former_scaled(u8 *into, u64 value, u64 scale)
{
        u64 length = 0;
        while (scale) {
                into[length++] = (u8)('0' + (value / scale) % 10);
                scale /= 10;
        }
        return length;
}

// The general path's shape the space-padded lane replaced: the digits at the
// front, moved to the end, the front filled.
static __attribute__((noinline, noclone)) u64 former_spaced(u8 *into, u64 value, u64 width)
{
        u64 length = reference_into(into, value);
        if (length < width) {
                u64 shift = width - length;
                for (u64 i = length; i--;)
                        into[shift + i] = into[i];
                for (u64 i = 0; i < shift; i++)
                        into[i] = ' ';
                length = width;
        }
        return length;
}

static u64 run_spaced(int assembly)
{
        u64 start = ticks();
        for (u64 r = 0; r < ROUNDS; r++) {
                u64 value = values[r & (COUNT - 1)] % 1000000;
                u64 length = assembly ? positive_into_padded(output, value, 6, ' ')
                                      : former_spaced(output, value, 6);
                sink += length + output[0] + output[5];
        }
        return ticks() - start;
}

static u64 run_once(u64 width, int assembly)
{
        u64 start = ticks();
        for (u64 r = 0; r < ROUNDS; r++) {
                u64 value = values[r & (COUNT - 1)];
                u64 length;
                if (width == 2) value %= 100;
                else if (width == 6) value %= 1000000;
                else value %= 1000000000;
                if (assembly)
                        length = width == 2 ? positive_into_pair(output, value)
                                            : positive_into_padded(output, value, width, '0');
                else
                        length = width == 2 ? former_pair(output, value)
                                            : former_scaled(output, value,
                                                  width == 6 ? 100000 : 100000000);
                sink += length + output[0] + output[width - 1];
        }
        return ticks() - start;
}

static void row(const char *name, u64 width)
{
        u64 old = ~0ul, assembly = ~0ul;
        for (u64 t = 0; t < TRIES; t++) {
                u64 got_old, got_assembly;
                if (t & 1) {
                        got_assembly = run_once(width, 1);
                        got_old = run_once(width, 0);
                } else {
                        got_old = run_once(width, 0);
                        got_assembly = run_once(width, 1);
                }
                if (got_old < old) old = got_old;
                if (got_assembly < assembly) assembly = got_assembly;
        }
        printf("  %-8s old %lu  assembly %lu  assembly/old %lu%%\n",
               name, old, assembly, assembly * 100 / old);
}

int main(void)
{
        int bad = correctness();
        for (u64 i = 0; i < COUNT; i++) values[i] = next();
        printf("arm64 native contiguous decimal, best of %d, %lu calls\n", TRIES, ROUNDS);
        row("width 2", 2); row("width 6", 6); row("width 9", 9);
        {
                u64 old = ~0ul, assembly = ~0ul;
                for (u64 t = 0; t < TRIES; t++) {
                        u64 o = run_spaced(0), a = run_spaced(1);
                        if (o < old) old = o;
                        if (a < assembly) assembly = a;
                }
                printf("  %-8s old %lu  assembly %lu  assembly/old %lu%%\n",
                       "space 6", old, assembly, assembly * 100 / old);
        }
        return bad;
}
#endif /* CHECK_native_fields */

#ifdef CHECK_native_train
/*
        The arm64 training and inference bodies lifted out of lib.c, run on
        this machine through the walks CHECK_train runs (the generator, the
        references and the walks are with it, after CHECK_modulo), then timed
        against the C each replaces as this machine's compiler builds it. The
        lift is the same bodies byte for byte, with Darwin's symbol spelling,
        so the hardware here judges what the qemu run on the box judges.
        Darwin does not answer an ID register read at EL0, so the feature byte
        the bf16 body asks is set here: 2 for the BFCVTN body this machine
        has, then 1 for the integer one.
*/
void fused_residual_add_f32(float *, const float *, unsigned long long, float);
void f32_to_bf16(unsigned short *, const float *, unsigned long long);
void bf16_to_f32(float *, const unsigned short *, unsigned long long);
unsigned long long fast_argmax_f32(const float *, unsigned long long);
unsigned char cpu_has_bf16 = 2;

static unsigned long long train_ticks(void)
{
        unsigned long long value;

        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
        return value;
}

//      A run of floats whose end is the start of a page that faults; Apple's
//      pages are 16 KiB.
static float *train_native_guard(unsigned long long most)
{
        unsigned long long span = ((most + 64) * 4 + 16383) / 16384 * 16384;
        unsigned char *pages = mmap(0, span + 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

        if (pages == MAP_FAILED || mprotect(pages + span, 16384, PROT_NONE) != 0)
                return 0;
        return (float *)(pages + span);
}

//      A run of floats whose start is just after a page that faults.
static float *train_native_front(unsigned long long most)
{
        unsigned long long span = (most * 4 + 16383) / 16384 * 16384;
        unsigned char *pages = mmap(0, span + 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

        if (pages == MAP_FAILED || mprotect(pages, 16384, PROT_NONE) != 0)
                return 0;
        return (float *)(pages + 16384);
}

__attribute__((noinline)) static void train_former_residual(float *restrict x, const float *restrict h, unsigned long long n,
                                                            float alpha)
{
        for (unsigned long long i = 0; i < n; i++)
                x[i] = x[i] + alpha * h[i];
}

__attribute__((noinline)) static void train_former_to_bf16(unsigned short *restrict to, const float *restrict from,
                                                           unsigned long long n)
{
        for (unsigned long long i = 0; i < n; i++)
                to[i] = train_bf16(train_bits(from[i]));
}

__attribute__((noinline)) static void train_former_from_bf16(float *restrict to, const unsigned short *restrict from,
                                                             unsigned long long n)
{
        for (unsigned long long i = 0; i < n; i++)
                ((unsigned *)to)[i] = (unsigned)from[i] << 16;
}

//      What a C programmer writes for greedy decoding: the first NaN, or the
//      first index of the maximum.
__attribute__((noinline)) static unsigned long long train_former_argmax(const float *x, unsigned long long n)
{
        unsigned long long best = 0;
        float value = n ? x[0] : 0;

        for (unsigned long long i = 1; i < n && value == value; i++)
                if (x[i] > value || x[i] != x[i])
                        value = x[i], best = i;
        return best;
}

static volatile unsigned long long train_sink;

//      Best of eleven alternated batches of each side, in ticks.
#define TRAIN_PAIR(label, n, reps, former, assembly)                                                      \
        do                                                                                                \
        {                                                                                                 \
                unsigned long long best[2] = {~0ull, ~0ull};                                              \
                for (int t = 0; t < 11; t++)                                                              \
                        for (int side0 = 0; side0 < 2; side0++)                                           \
                        {                                                                                 \
                                int side = t & 1 ? 1 - side0 : side0;                                     \
                                unsigned long long start = train_ticks();                                 \
                                for (unsigned long long r = 0; r < (reps); r++)                           \
                                        if (side)                                                         \
                                                assembly;                                                 \
                                        else                                                              \
                                                former;                                                   \
                                unsigned long long took = train_ticks() - start;                          \
                                best[side] = took < best[side] ? took : best[side];                       \
                        }                                                                                 \
                printf("  %-28s %8llu: C %llu ticks, assembly %llu, asm/C %llu%%\n", label, (unsigned long long)(n), \
                       best[0], best[1], best[1] * 100 / (best[0] ? best[0] : 1));                        \
        } while (0)

//      L1, L2 and beyond, then the residual add with both arrays a float off
//      16 bytes.
static void train_time(float *x, float *h)
{
        static const unsigned long long sizes[] = {1000, 2048, 8192, 65536, 2097152};
        unsigned short *b = (unsigned short *)h;

        for (unsigned long long s = 0; s < 10; s++)
        {
                unsigned long long n = sizes[s % 5], reps = (1ull << 25) / n;
                float *xo = x + (s >= 5), *ho = h + (s >= 5);

                TRAIN_PAIR(s >= 5 ? "residual add, +4 B" : "residual add", n, reps, train_former_residual(xo, ho, n, 1e-7f),
                           fused_residual_add_f32(xo, ho, n, 1e-7f));
        }
        for (unsigned long long s = 0; s < 5; s++)
        {
                unsigned long long n = sizes[s], reps = (1ull << 25) / n;

                cpu_has_bf16 = 2;
                TRAIN_PAIR("f32_to_bf16, bfcvtn", n, reps, train_former_to_bf16(b, x, n), f32_to_bf16(b, x, n));
                cpu_has_bf16 = 1;
                TRAIN_PAIR("f32_to_bf16, integer", n, reps, train_former_to_bf16(b, x, n), f32_to_bf16(b, x, n));
                TRAIN_PAIR("bf16_to_f32", n, reps, train_former_from_bf16(x, b, n), bf16_to_f32(x, b, n));
        }
        cpu_has_bf16 = 2;
        static const unsigned long long vocabularies[] = {2048, 32000, 50257, 128256};

        for (unsigned long long s = 0; s < 4; s++)
        {
                unsigned long long n = vocabularies[s], reps = (1ull << 24) / n;

                for (unsigned long long i = 0; i < n; i++)
                        x[i] = (float)((i * 2654435761u) >> 8 & 0xffff) * 1e-4f;
                x[(n * 7) / 11] = 100.0f;
                TRAIN_PAIR("fast_argmax_f32", n, reps, train_sink += train_former_argmax(x, n), train_sink += fast_argmax_f32(x, n));
        }
}

int main(void)
{
        float *x_end = train_native_guard(TRAIN_MOST), *h_end = train_native_guard(TRAIN_MOST);
        train_faults faults = {0, 0, 0, 0, 0, 0};
        train_bf16_faults bfcvtn = {0, 0, 0, 0, 0, 0}, integer = {0, 0, 0, 0, 0, 0};
        train_argmax_faults argmax = {0, 0, 0, 0, 0, 0};
        float *front = train_native_front(TRAIN_MOST);

        if (!x_end || !h_end)
        {
                printf("arm64 train: 1 checks | 1 failures\n");
                return 1;
        }
        train_residual_cases(fused_residual_add_f32, x_end, h_end, 1, &faults);
        cpu_has_bf16 = 2;
        train_bf16_cases(f32_to_bf16, bf16_to_f32, x_end, (unsigned short *)h_end, &bfcvtn);
        cpu_has_bf16 = 1;
        train_bf16_cases(f32_to_bf16, bf16_to_f32, x_end, (unsigned short *)h_end, &integer);
        cpu_has_bf16 = 2;
        if (front)
                train_argmax_cases(fast_argmax_f32, x_end, front, &argmax);
        unsigned long long bad = faults.values + faults.bounded + faults.outside + faults.kept + faults.aliased + faults.integers;

        bad += bfcvtn.narrowed + bfcvtn.widened + bfcvtn.kept + bfcvtn.outside + bfcvtn.strata + bfcvtn.every;
        bad += integer.narrowed + integer.widened + integer.kept + integer.outside + integer.strata + integer.every;
        bad += !front + argmax.random + argmax.planted + argmax.ties + argmax.zeros + argmax.nans + argmax.infinities;
        for (unsigned long long i = 0; i < TRAIN_MOST; i++)
                x_end[(long long)i - TRAIN_MOST - 64] = 1.0f / (float)(1 + (i & 1023)),
                h_end[(long long)i - TRAIN_MOST - 64] = 1e-3f * (float)(i & 255);
        train_time(x_end - TRAIN_MOST - 64, h_end - TRAIN_MOST - 64);
        printf("arm64 train: 24 checks | residual values %llu | bounded %llu | outside %llu | kept %llu | aliased %llu | "
               "integers %llu | bfcvtn %llu %llu %llu %llu %llu %llu | integer %llu %llu %llu %llu %llu %llu | "
               "argmax %llu %llu %llu %llu %llu %llu\n",
               faults.values, faults.bounded, faults.outside, faults.kept, faults.aliased, faults.integers, bfcvtn.narrowed,
               bfcvtn.widened, bfcvtn.kept, bfcvtn.outside, bfcvtn.strata, bfcvtn.every, integer.narrowed, integer.widened,
               integer.kept, integer.outside, integer.strata, integer.every, argmax.random, argmax.planted, argmax.ties,
               argmax.zeros, argmax.nans, argmax.infinities + !front);
        return bad != 0;
}
#endif /* CHECK_native_train */

#ifdef CHECK_native_input_bases
/* Exact lifted bounded input-base leaves on native Apple ARM silicon. */
#include "input_bases.h"

#define SHARED_native
#include "../checks.c"
#undef SHARED_native

u64 string_digits_max(const u8 *, u64, u64 *);
u64 string_digits_octal_max(const u8 *, u64, u64 *);
u64 string_digits_octal_escape_max(const u8 *, u64, u64 *);
u64 string_digits_hexadecimal_max(const u8 *, u64, u64 *);
u64 string_digits_hexadecimal_escape_max(const u8 *, u64, u64 *);
u64 string_digits_base_max(const u8 *, u64, u64, u64 *);

#define SUBJECTS 64
#define ROUNDS (1ul << 20)
#define TRIES 11

typedef u64 (*fixed_parser)(const u8 *, u64, u64 *);
typedef u64 (*base_parser)(const u8 *, u64, u64, u64 *);

static u8 subjects[SUBJECTS][96];
static u64 limits[SUBJECTS];
static volatile u64 sink;

static __attribute__((noinline, noclone)) u64 scalar_octal(const u8 *source, u64 bound,
                                                   u64 *used)
{
        u64 value = 0, at = 0;
        while (at < bound && source[at] >= '0' && source[at] <= '7') {
                value = (value << 3) + source[at] - '0';
                at++;
        }
        if (used) *used = at;
        return value;
}

static __attribute__((noinline, noclone)) u64 scalar_hex(const u8 *source, u64 bound,
                                                 u64 *used)
{
        u64 value = 0, at = 0;
        while (at < bound) {
                u8 c = source[at];
                u64 digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                else break;
                value = (value << 4) + digit;
                at++;
        }
        if (used) *used = at;
        return value;
}

static __attribute__((noinline, noclone)) u64 scalar_base(const u8 *source, u64 bound,
                                                  u64 base, u64 *used)
{
        u64 value = 0, at = 0;
        while (at < bound) {
                u8 c = source[at];
                u64 digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
                else break;
                if (digit >= base) break;
                value = value * base + digit;
                at++;
        }
        if (used) *used = at;
        return value;
}

static void make_subjects(u64 base, u64 maximum)
{
        for (u64 which = 0; which < SUBJECTS; which++) {
                u64 length = 1 + which % maximum;
                for (u64 at = 0; at < length; at++) {
                        u64 digit = (which * 13 + at * 17) % base;
                        subjects[which][at] = (u8)(digit < 10 ? '0' + digit
                            : ((at & 1) ? 'A' : 'a') + digit - 10);
                }
                subjects[which][length] = '@';
                limits[which] = length + 1;
        }
}

static __attribute__((noinline, noclone)) u64 run_fixed(fixed_parser parser)
{
        u64 start = ticks();
        for (u64 round = 0; round < ROUNDS; round++) {
                u64 which = round & (SUBJECTS - 1), used;
                u64 value = parser(subjects[which], limits[which], &used);
                sink += value + used;
        }
        return ticks() - start;
}

static __attribute__((noinline, noclone)) u64 run_base(base_parser parser, u64 base)
{
        u64 start = ticks();
        for (u64 round = 0; round < ROUNDS; round++) {
                u64 which = round & (SUBJECTS - 1), used;
                u64 value = parser(subjects[which], limits[which], base, &used);
                sink += value + used;
        }
        return ticks() - start;
}

static u64 median(u64 values[TRIES])
{
        for (u64 at = 1; at < TRIES; at++) {
                u64 value = values[at], before = at;
                while (before && values[before - 1] > value) {
                        values[before] = values[before - 1];
                        before--;
                }
                values[before] = value;
        }
        return values[TRIES / 2];
}

static void fixed_row(const char *name, fixed_parser scalar,
                      fixed_parser assembly, u64 base, u64 maximum)
{
        u64 old[TRIES], floor[TRIES];
        make_subjects(base, maximum);
        for (u64 trial = 0; trial < TRIES; trial++) {
                if (trial & 1) {
                        floor[trial] = run_fixed(assembly);
                        old[trial] = run_fixed(scalar);
                } else {
                        old[trial] = run_fixed(scalar);
                        floor[trial] = run_fixed(assembly);
                }
        }
        u64 scalar_ticks = median(old), assembly_ticks = median(floor);
        printf("  %-18s scalar %lu  assembly %lu  assembly/scalar %lu%%\n",
               name, scalar_ticks, assembly_ticks,
               assembly_ticks * 100 / scalar_ticks);
}

static void base_row(void)
{
        u64 old[TRIES], floor[TRIES];
        make_subjects(36, 13);
        for (u64 trial = 0; trial < TRIES; trial++) {
                if (trial & 1) {
                        floor[trial] = run_base(string_digits_base_max, 36);
                        old[trial] = run_base(scalar_base, 36);
                } else {
                        old[trial] = run_base(scalar_base, 36);
                        floor[trial] = run_base(string_digits_base_max, 36);
                }
        }
        u64 scalar_ticks = median(old), assembly_ticks = median(floor);
        printf("  %-18s scalar %lu  assembly %lu  assembly/scalar %lu%%\n",
               "base 36, 1-13", scalar_ticks, assembly_ticks,
               assembly_ticks * 100 / scalar_ticks);
}

int main(void)
{
        u64 used = 99;
        int bad = 0;
        bad += string_digits_octal_max((const u8 *)"777@", 4, &used) != 511 || used != 3;
        bad += string_digits_octal_escape_max((const u8 *)"777@", 4,
                                               &used) != 511 || used != 3;
        bad += string_digits_hexadecimal_max((const u8 *)"aF@", 3, &used) != 175 || used != 2;
        bad += string_digits_hexadecimal_escape_max((const u8 *)"aF@", 3,
                                                     &used) != 175 || used != 2;
        bad += string_digits_base_max((const u8 *)"Z@", 2, 36, &used) != 35 || used != 1;
        bad += string_digits_octal_max(0, 0, 0) != 0;

        printf("arm64 native bounded input bases: %s\n", bad ? "FAILED" : "correct");
        fixed_row("octal escape, 1-3", scalar_octal,
                  string_digits_octal_escape_max, 8, 3);
        fixed_row("octal word, 1-22", scalar_octal,
                  string_digits_octal_max, 8, 22);
        fixed_row("hex escape, 1-2", scalar_hex,
                  string_digits_hexadecimal_escape_max, 16, 2);
        fixed_row("hex word, 1-16", scalar_hex,
                  string_digits_hexadecimal_max, 16, 16);
        base_row();
        return bad;
}
#endif /* CHECK_native_input_bases */

#ifdef CHECK_native_network
/* Exact lifted ARM64 unaligned wire accessors on native Apple ARM silicon. */
#include "network.h"

#define NATIVE_SEED 0x9e3779b97f4a7c15ul
#define SHARED_native
#include "../checks.c"
#undef SHARED_native

typedef unsigned short u16;
typedef unsigned int u32;

u16 network_load_16(u8 *);
u32 network_load_32(u8 *);
void network_store_16(u8 *, u16);
void network_store_32(u8 *, u32);

int main(void)
{
        u8 got[32], want[32];
        u64 checks = 0, bad = 0;

        for (u32 value = 0; value <= 0xffff; value++)
                for (u64 offset = 0; offset < 16; offset++)
                {
                        for (u64 i = 0; i < sizeof got; i++)
                                got[i] = want[i] = 0xa5;

                        want[offset] = (u8)(value >> 8);
                        want[offset + 1] = (u8)value;
                        network_store_16(got + offset, (u16)value);

                        checks++;
                        if (network_load_16(got + offset) != value)
                        {
                                bad++;
                                continue;
                        }

                        for (u64 i = 0; i < sizeof got; i++)
                                if (got[i] != want[i])
                                {
                                        bad++;
                                        break;
                                }
                }

        for (u64 round = 0; round < 1000000; round++)
        {
                u32 value = next();
                u64 offset = round & 15;

                for (u64 i = 0; i < sizeof got; i++)
                        got[i] = want[i] = 0x5a;

                want[offset] = (u8)(value >> 24);
                want[offset + 1] = (u8)(value >> 16);
                want[offset + 2] = (u8)(value >> 8);
                want[offset + 3] = (u8)value;
                network_store_32(got + offset, value);

                checks++;
                if (network_load_32(got + offset) != value)
                {
                        bad++;
                        continue;
                }

                for (u64 i = 0; i < sizeof got; i++)
                        if (got[i] != want[i])
                        {
                                bad++;
                                break;
                        }
        }

        printf("arm64 network fields: %lu checks | %lu failures\n", checks, bad);
        return bad != 0;
}
#endif /* CHECK_native_network */

#ifdef CHECK_native_tensor
/*
        The arm64 tensor bodies lifted out of lib.c, run on this machine
        through the properties CHECK_tensor walks (the generator, the model
        and the walks are with it, ahead of CHECK_font). The lift is the
        same bodies byte for byte, with Darwin's symbol spelling, so the
        hardware here judges what the qemu run on the box judges.
*/
void swiglu_forward_f32(float *, const float *, const float *, unsigned long long);
float rmsnorm_forward_f32(float *, const float *, const float *, unsigned long long, float);
void rmsnorm_backward_f32(float *, float *, const float *, const float *, const float *, unsigned long long, float);

int main(void)
{
        float *region[5];
        unsigned long long checks = 0, span = (TENSOR_GUARD_FLOATS * 4 + 16383) / 16384 * 16384;

        //      Each region ends against a page that faults; Apple's pages are
        //      16 KiB, so the span is a whole number of them.
        for (int at = 0; at < 5; at++)
        {
                unsigned char *pages = mmap(0, span + 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

                if (pages == MAP_FAILED || mprotect(pages + span, 16384, PROT_NONE) != 0)
                {
                        printf("arm64 tensor: 1 checks | 1 failures\n");
                        return 1;
                }
                region[at] = (float *)(pages + span) - TENSOR_GUARD_FLOATS;
        }

        tensor_faults faults = {0, 0, 0, 0};

        tensor_swiglu_forward_cases(swiglu_forward_f32, region, &faults);
        checks += 4;
        unsigned long long grid = tensor_swiglu_forward_grid(swiglu_forward_f32, region);
        unsigned long long sweep = tensor_swiglu_forward_sweep(swiglu_forward_f32, region, 16411);

        checks += 2;

        tensor_faults norm = {0, 0, 0, 0}, back = {0, 0, 0, 0};

        tensor_rmsnorm_forward_cases(rmsnorm_forward_f32, region, &norm);
        int edges = tensor_rmsnorm_edges(rmsnorm_forward_f32, region);

        tensor_rmsnorm_backward_cases(rmsnorm_backward_f32, region, region + 3, &back);
        checks += 9;
        printf("  neon: swiglu_forward_f32 worst %.3f eps, rmsnorm_forward_f32 %.3f, rmsnorm_backward_f32 %.3f\n",
               tensor_worst, tensor_rms_worst, tensor_rmsb_worst);
        printf("arm64 tensor: %llu checks | values %llu | outside %llu | aliased %llu | nans %llu | grid %llu | sweep %llu"
               " | rmsnorm %llu %llu %llu %llu %s | backward %llu %llu %llu %llu\n",
               checks, faults.values, faults.outside, faults.aliased, faults.nans, grid, sweep, norm.values, norm.outside,
               norm.aliased, norm.nans, edges ? "edges" : "EDGES", back.values, back.outside, back.aliased, back.nans);
        return faults.values || faults.outside || faults.aliased || faults.nans || grid || sweep || norm.values ||
               norm.outside || norm.aliased || norm.nans || !edges || back.values || back.outside || back.aliased ||
               back.nans;
}
#endif /* CHECK_native_tensor */

#ifdef CHECK_native_reserve
/* Exact lifted ARM64 movable-store leaves, with native allocation beneath. */
#include "reserve.h"

typedef unsigned char u8;
typedef unsigned long u64;

void *malloc(u64);
void free(void *);
int printf(const char *, ...);

static int allocation_fails;

void *memory(u64 size)
{
        return allocation_fails ? (void *)-12 : malloc(size);
}

void memory_free(void *at, u64 size)
{
        (void)size;
        if (at)
                free(at);
}

void *memory_copy(void *to, const void *from, u64 size)
{
        u8 *out = to;
        const u8 *in = from;

        for (u64 i = 0; i < size; i++)
                out[i] = in[i];

        return to;
}

int memory_reserve(void **, u64 *, u64, u64, u64, u64);
void memory_release(void **, u64 *, u64 *, u64);
u64 memory_growth(u64, u64, u64);

#define CHECK(condition) do { checks++; if (!(condition)) bad++; } while (0)

int main(void)
{
        void *held = 0;
        u64 have = 0, used = 0;
        u64 checks = 0, bad = 0;

        CHECK(memory_growth(64, 64, 16) == 64);
        CHECK(memory_growth(0, 1, 64) == 64);
        CHECK(memory_growth(64, 65, 16) == 128);
        CHECK(memory_growth(0, 1, 0) == 0);
        CHECK(memory_growth(1ul << 63, ~0ul, 1) == 0);

        CHECK(memory_reserve(&held, &have, used, 1, sizeof(u64), 64));
        CHECK(held != 0);
        CHECK(have == 64);

        u64 *words = held;
        for (used = 0; used < have; used++)
                words[used] = used ^ 0x55aa55aa55aa55aaul;

        void *first = held;
        CHECK(memory_reserve(&held, &have, used, 64, sizeof(u64), 64));
        CHECK(held == first);
        CHECK(have == 64);

        CHECK(memory_reserve(&held, &have, used, 65, sizeof(u64), 64));
        CHECK(held != 0);
        CHECK(have == 128);

        words = held;
        for (u64 i = 0; i < used; i++)
                CHECK(words[i] == (i ^ 0x55aa55aa55aa55aaul));

        memory_release(&held, &have, &used, sizeof(u64));
        CHECK(held == 0);
        CHECK(have == 0);
        CHECK(used == 0);

        CHECK(!memory_reserve(&held, &have, 0, 1, 0, 64));
        CHECK(!memory_reserve(&held, &have, 0, ~0ul, 1, 1ul << 63));
        CHECK(!memory_reserve(&held, &have, 0, 2, ~0ul, 2));
        CHECK(held == 0 && have == 0);

        allocation_fails = 1;
        CHECK(!memory_reserve(&held, &have, 0, 1, 1, 64));
        CHECK(held == 0 && have == 0);

        printf("arm64 memory reserve: %lu checks | %lu failures\n", checks, bad);
        return bad != 0;
}
#endif /* CHECK_native_reserve */

#ifdef CHECK_native_matrix
/*
        gemm_f32's arm64 body lifted out of lib.c, run on this machine
        through the properties CHECK_matrix walks (the generator, the judge
        and the walks are with it, after CHECK_math), the trainer's shapes at
        full size included. The lift is the same bodies byte for byte, with
        Darwin's symbol spelling, so the hardware here judges what the qemu
        run on the box judges.
*/
void gemm_f32(float *, const float *, const float *, unsigned long long, unsigned long long, unsigned long long,
              unsigned, float);

int main(void)
{
        matrix_faults faults = {0, 0, 0, 0, 0, 0};

        matrix_sweep(gemm_f32, &faults, 1);
        matrix_nans(gemm_f32, &faults);
        matrix_edges(gemm_f32, &faults);
        matrix_narrow(gemm_f32, (matrix_wide_call *)(void *)gemm_f32, &faults);
        printf("  neon: gemm_f32 %llu cases, worst %.3f eps S\n", faults.cases, matrix_worst);
        printf("arm64 matrix: 5 checks | values %llu | outside %llu | nans %llu | edges %llu | narrow %llu\n",
               faults.values, faults.outside, faults.nans, faults.edges, faults.narrow);
        return faults.values || faults.outside || faults.nans || faults.edges || faults.narrow;
}
#endif /* CHECK_native_matrix */


