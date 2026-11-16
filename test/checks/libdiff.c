/*
        test/checks/libdiff.c: the library's memory routines held to C (libdiff).

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_libdiff
/*
        The library's memory routines against plain C references, over every
        size class and every alignment the contract allows, and seeded fuzz.

        test/run runs this as the libdiff lane, once for each compiler it is
        asked for. By hand, with the library built by the same compiler:

            cc -O2 -c src/lib.c -o lib.o
            cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
               -DCHECK_libdiff test/checks.c lib.o -Wl,--gc-sections -o libdiff   (ELF)
            cc ... -Wl,-dead_strip -o libdiff                                     (Mac)

        The library's side is the assembly in lib.c, called through the
        prototypes its callers use. The reference is C written from the
        contract and nothing else: a byte loop in the direction the contract
        needs, with no call into the library and no libc copy. Both sides run
        on the same window of memory, whose end is a PROT_NONE guard page: the
        operands end exactly on the guard, so a read or write past the last
        byte an operand names faults rather than passing. Afterwards the whole
        window is compared with the reference's copy, so a write before the
        first operand byte, or to a byte the contract did not name, is a
        difference. The reference runs on a malloc'd copy sized to the window,
        so the sanitizers see every access the reference makes.

        A row is one routine: its kind (how its operands are placed), its
        contract flags, and two wrappers of one shape, one calling the library
        and one the reference. To add a routine: declare its prototype below,
        write its two wrappers, and add one line to libdiff_rows. The driver
        does not change.

        Kinds:
          COPY      dst and src. The overlap sweep runs only when the row's
                    overlap flag says the contract allows it (the memmove family).
          FILL      dst and a value; n counts elements of unit bytes.
          CLEAR     dst only, no value (memory_zero).
          UNTIL     memccpy: dst, src, a byte, with that byte at every position
                    in turn and absent.
          EXCHANGE  two disjoint ranges, swapped.

        Flags: terminator writes one byte at dst + n (the _end forms);
        zero_no_deref says n = 0 touches neither pointer, so it is also run
        with both pointers on the guard; align is the dst alignment step the
        contract needs (8 for the aligned 64-bit fill). Sizes are 0 to 256 in
        full, then the tier boundaries and the sizes around them, to 100000.

        Every run is one check. The last line is "N checks, M failures".
*/

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The library's routines, spelled the way their callers spell them. */
void *memory_copy(void *destination, const void *source, unsigned long size);
void *memory_copy_apart(void *destination, const void *source, unsigned long size);
unsigned char *memory_copy_end(unsigned char *destination, const void *source, unsigned long size);
unsigned char *memory_copy_apart_end(unsigned char *destination, const void *source,
                                     unsigned long size);
void memory_copy_source_first(const void *source, void *destination, unsigned long size);
void *memory_copy_until(void *destination, const void *source, int value, unsigned long size);
void memory_exchange_apart(void *left, void *right, unsigned long size);
void memory_zero(void *destination, unsigned long size);
void *memory_fill(void *destination, int value, unsigned long size);
void memory_fill_u32(void *destination, unsigned long count, unsigned int value);
void memory_fill_u64_aligned(void *destination, unsigned long count, unsigned long value);
void *memory_fill_32(void *destination, unsigned int value, unsigned long count);
void *memory_fill_64(void *destination, unsigned long value, unsigned long count);

#define LIBDIFF_USABLE (1L << 20)  /* bytes between the window's start and the guard */
#define LIBDIFF_PAD 256L           /* canary room before the operands, and slack */

enum { LIBDIFF_COPY, LIBDIFF_FILL, LIBDIFF_CLEAR, LIBDIFF_UNTIL, LIBDIFF_EXCHANGE };
enum {
        LIBDIFF_ONE,        /* one operand, dst */
        LIBDIFF_APART_A,    /* src wholly below dst */
        LIBDIFF_APART_B,    /* dst wholly below src */
        LIBDIFF_OVERLAP,    /* dst = src + delta, the two overlapping */
        LIBDIFF_NODEREF     /* n = 0 with both pointers on the guard */
};

struct libdiff_args {
        long dst, src, n;      /* offsets from the window's first byte; n in elements */
        unsigned long value;
        long pos;              /* UNTIL: the offset within src of the byte, or -1 */
};

typedef long (*libdiff_op)(unsigned char *base, const struct libdiff_args *a);

struct libdiff_row {
        const char *name;
        int kind;
        int overlap;
        int terminator;
        int unit;
        int align;
        int zero_no_deref;
        libdiff_op library;
        libdiff_op reference;
};

static unsigned char *libdiff_end;
static unsigned char *libdiff_pattern;
static unsigned long long libdiff_seed = 0x9e3779b97f4a7c15ull;
static volatile sig_atomic_t libdiff_armed;
static sigjmp_buf libdiff_jump;
static long libdiff_checks;
static long libdiff_failures;
static long libdiff_row_failures;

static unsigned long long libdiff_random(void)
{
        XORSHIFT64(libdiff_seed);
        return libdiff_seed;
}

static void libdiff_trap(int signal_number)
{
        if (libdiff_armed)
                siglongjmp(libdiff_jump, signal_number);
        _exit(3);
}

/* A pointer's offset from base, or -1 for the null answer memccpy gives. */
static long libdiff_at(unsigned char *base, void *pointer)
{
        return pointer ? (long)((unsigned char *)pointer - base) : -1;
}

/* memmove, as a byte loop in the direction that is safe. */
static void libdiff_move(unsigned char *destination, const unsigned char *source, unsigned long n)
{
        if ((uintptr_t)destination < (uintptr_t)source)
                for (unsigned long i = 0; i < n; i++)
                        destination[i] = source[i];
        else
                for (unsigned long i = n; i-- > 0;)
                        destination[i] = source[i];
}

/* Library and reference wrappers: one shape, base plus offsets in, an answer out. */

static long library_copy(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_copy(b + a->dst, b + a->src, (unsigned long)a->n));
}

static long reference_copy(unsigned char *b, const struct libdiff_args *a)
{
        libdiff_move(b + a->dst, b + a->src, (unsigned long)a->n);
        return a->dst;
}

static long library_copy_apart(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_copy_apart(b + a->dst, b + a->src, (unsigned long)a->n));
}

static long library_copy_end(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_copy_end(b + a->dst, b + a->src, (unsigned long)a->n));
}

static long library_copy_apart_end(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_copy_apart_end(b + a->dst, b + a->src, (unsigned long)a->n));
}

static long reference_copy_end(unsigned char *b, const struct libdiff_args *a)
{
        libdiff_move(b + a->dst, b + a->src, (unsigned long)a->n);
        b[a->dst + a->n] = 0;
        return a->dst + a->n;
}

static long library_copy_source_first(unsigned char *b, const struct libdiff_args *a)
{
        memory_copy_source_first(b + a->src, b + a->dst, (unsigned long)a->n);
        return 0;
}

static long reference_copy_source_first(unsigned char *b, const struct libdiff_args *a)
{
        libdiff_move(b + a->dst, b + a->src, (unsigned long)a->n);
        return 0;
}

static long library_copy_until(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_copy_until(b + a->dst, b + a->src, (int)a->value,
                                               (unsigned long)a->n));
}

/* memccpy: copy through the first byte equal to value, and answer just past it. */
static long reference_copy_until(unsigned char *b, const struct libdiff_args *a)
{
        for (long i = 0; i < a->n; i++) {
                b[a->dst + i] = b[a->src + i];
                if (b[a->src + i] == (unsigned char)a->value)
                        return a->dst + i + 1;
        }
        return -1;
}

static long library_exchange(unsigned char *b, const struct libdiff_args *a)
{
        memory_exchange_apart(b + a->dst, b + a->src, (unsigned long)a->n);
        return 0;
}

static long reference_exchange(unsigned char *b, const struct libdiff_args *a)
{
        for (long i = 0; i < a->n; i++) {
                unsigned char t = b[a->dst + i];
                b[a->dst + i] = b[a->src + i];
                b[a->src + i] = t;
        }
        return 0;
}

static long library_zero(unsigned char *b, const struct libdiff_args *a)
{
        memory_zero(b + a->dst, (unsigned long)a->n);
        return 0;
}

static long reference_zero(unsigned char *b, const struct libdiff_args *a)
{
        for (long i = 0; i < a->n; i++)
                b[a->dst + i] = 0;
        return 0;
}

static long library_fill(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_fill(b + a->dst, (int)a->value, (unsigned long)a->n));
}

static long reference_fill(unsigned char *b, const struct libdiff_args *a)
{
        for (long i = 0; i < a->n; i++)
                b[a->dst + i] = (unsigned char)a->value;
        return a->dst;
}

static long library_fill_u32(unsigned char *b, const struct libdiff_args *a)
{
        memory_fill_u32(b + a->dst, (unsigned long)a->n, (unsigned int)a->value);
        return 0;
}

static long reference_fill_u32(unsigned char *b, const struct libdiff_args *a)
{
        unsigned int v = (unsigned int)a->value;
        for (long i = 0; i < a->n; i++)
                memcpy(b + a->dst + 4 * i, &v, 4);
        return 0;
}

static long library_fill_u64_aligned(unsigned char *b, const struct libdiff_args *a)
{
        memory_fill_u64_aligned(b + a->dst, (unsigned long)a->n, a->value);
        return 0;
}

static long reference_fill_u64(unsigned char *b, const struct libdiff_args *a)
{
        unsigned long v = a->value;
        for (long i = 0; i < a->n; i++)
                memcpy(b + a->dst + 8 * i, &v, 8);
        return 0;
}

/* memset32 and memset64 answer the destination; the void forms above do not. */
static long reference_fill_32(unsigned char *b, const struct libdiff_args *a)
{
        reference_fill_u32(b, a);
        return a->dst;
}

static long reference_fill_64(unsigned char *b, const struct libdiff_args *a)
{
        reference_fill_u64(b, a);
        return a->dst;
}

static long library_fill_32(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_fill_32(b + a->dst, (unsigned int)a->value,
                                            (unsigned long)a->n));
}

static long library_fill_64(unsigned char *b, const struct libdiff_args *a)
{
        return libdiff_at(b, memory_fill_64(b + a->dst, a->value, (unsigned long)a->n));
}

/*
        The rows. The fields after the name are kind, overlap, terminator,
        unit, align, zero_no_deref, then the library's wrapper and the
        reference's.
*/
static const struct libdiff_row libdiff_rows[] = {
        {"memory_copy",              LIBDIFF_COPY,     1, 0, 1, 1, 1, library_copy,              reference_copy},
        {"memory_copy_apart",        LIBDIFF_COPY,     0, 0, 1, 1, 1, library_copy_apart,        reference_copy},
        {"memory_copy_end",          LIBDIFF_COPY,     1, 1, 1, 1, 0, library_copy_end,          reference_copy_end},
        {"memory_copy_apart_end",    LIBDIFF_COPY,     0, 1, 1, 1, 0, library_copy_apart_end,    reference_copy_end},
        {"memory_copy_source_first", LIBDIFF_COPY,     1, 0, 1, 1, 1, library_copy_source_first, reference_copy_source_first},
        {"memory_copy_until",        LIBDIFF_UNTIL,    0, 0, 1, 1, 1, library_copy_until,        reference_copy_until},
        {"memory_exchange_apart",    LIBDIFF_EXCHANGE, 0, 0, 1, 1, 1, library_exchange,         reference_exchange},
        {"memory_zero",              LIBDIFF_CLEAR,    0, 0, 1, 1, 1, library_zero,             reference_zero},
        {"memory_fill",              LIBDIFF_FILL,     0, 0, 1, 1, 1, library_fill,             reference_fill},
        {"memory_fill_u32",          LIBDIFF_FILL,     0, 0, 4, 1, 1, library_fill_u32,         reference_fill_u32},
        {"memory_fill_u64_aligned",  LIBDIFF_FILL,     0, 0, 8, 8, 1, library_fill_u64_aligned, reference_fill_u64},
        {"memory_fill_32",           LIBDIFF_FILL,     0, 0, 4, 1, 1, library_fill_32,         reference_fill_32},
        {"memory_fill_64",           LIBDIFF_FILL,     0, 0, 8, 1, 1, library_fill_64,         reference_fill_64},
};

static const unsigned long libdiff_fill_values[] = {
        0, 1, 0x7f, 0x80, 0xff, 0x0123456789abcdefull, 0x8000000000000001ull, ~0ul,
};

/*
        The largest start at or below limit - length whose offset is congruent
        to want modulo 64. want is an offset's residue (libdiff_residue), and
        the window ends on a page boundary, so an offset's residue is its
        address's residue less the window's length.
*/
static long libdiff_place(long limit, long length, long want)
{
        long start = limit - length;
        return start - (((start - want) % 64) + 64) % 64;
}

/* The offset residue that gives address residue a in a window of length w. */
static long libdiff_residue(long address_residue, long window)
{
        return (((address_residue + window) % 64) + 64) % 64;
}

static void libdiff_report(const struct libdiff_row *row, long n, int mode, long a_dst,
                           long a_src, long delta, unsigned long value, long pos,
                           const char *what, long at, long got, long want)
{
        libdiff_row_failures++;
        if (libdiff_row_failures > 12)
                return;
        printf("FAIL %s n=%ld mode=%d dst_align=%ld src_align=%ld delta=%ld value=%#lx pos=%ld: %s",
               row->name, n, mode, a_dst, a_src, delta, value, pos, what);
        if (at >= 0)
                printf(" at window byte %ld (got %#x want %#x)", at, (unsigned)got, (unsigned)want);
        printf("\n");
}

/*
        One check: place the operands, run the reference on a copy and the
        library on the window, and compare. a_dst and a_src are the addresses'
        residues mod 64 that the operands are asked to have; OVERLAP takes
        a_dst and the delta, dst - src, and derives the source. Returns 1 when
        the check failed.
*/
static int libdiff_one(const struct libdiff_row *row, long n, int mode, long a_dst,
                       long a_src, long delta, unsigned long value, long pos)
{
        long nb = n * row->unit;
        long term = row->terminator;
        long total, w = 0, dst = 0, src = 0, at;
        long library_answer = 0, reference_answer = 0;
        struct libdiff_args a;
        unsigned char *base, *mirror;

        switch (mode) {
        case LIBDIFF_ONE:
                w = nb + term + LIBDIFF_PAD;
                dst = libdiff_place(w, nb + term, libdiff_residue(a_dst, w));
                src = dst;
                break;
        case LIBDIFF_APART_A:
                w = 2 * (nb + term) + LIBDIFF_PAD;
                dst = libdiff_place(w, nb + term, libdiff_residue(a_dst, w));
                src = libdiff_place(dst, nb, libdiff_residue(a_src, w));
                break;
        case LIBDIFF_APART_B:
                w = 2 * (nb + term) + LIBDIFF_PAD;
                src = libdiff_place(w, nb, libdiff_residue(a_src, w));
                dst = libdiff_place(src, nb + term, libdiff_residue(a_dst, w));
                break;
        case LIBDIFF_OVERLAP: {
                long magnitude = delta < 0 ? -delta : delta;
                long lower_residue = delta >= 0 ? (((a_dst - delta) % 64) + 64) % 64 : a_dst;
                long lower;
                total = nb + magnitude + term;
                w = total + LIBDIFF_PAD;
                lower = libdiff_place(w, total, libdiff_residue(lower_residue, w));
                if (delta >= 0) {
                        src = lower;
                        dst = lower + delta;
                } else {
                        dst = lower;
                        src = lower - delta;
                }
                break;
        }
        case LIBDIFF_NODEREF:
                w = LIBDIFF_PAD;
                dst = w;
                src = w;
                break;
        }

        a.dst = dst;
        a.src = src;
        a.n = n;
        a.value = value;
        a.pos = pos;

        base = libdiff_end - w;
        memcpy(base, libdiff_pattern + (libdiff_random() & 4095), (size_t)w);
        if (row->kind == LIBDIFF_UNTIL) {
                for (long i = 0; i < nb; i++)
                        if (base[src + i] == (unsigned char)value)
                                base[src + i] = (unsigned char)(value + 1);
                if (pos >= 0)
                        base[src + pos] = (unsigned char)value;
        }

        mirror = malloc((size_t)w);
        if (!mirror)
                return 0;
        memcpy(mirror, base, (size_t)w);
        reference_answer = row->reference(mirror, &a);

        libdiff_checks++;
        libdiff_armed = 1;
        if (sigsetjmp(libdiff_jump, 1)) {
                libdiff_armed = 0;
                libdiff_report(row, n, mode, a_dst, a_src, delta, value, pos,
                               "faulted on the guard", -1, 0, 0);
                free(mirror);
                libdiff_failures++;
                return 1;
        }
        library_answer = row->library(base, &a);
        libdiff_armed = 0;

        for (at = 0; at < w && base[at] == mirror[at]; at++)
                ;
        if (at < w) {
                libdiff_report(row, n, mode, a_dst, a_src, delta, value, pos, "window differs",
                               at, base[at], mirror[at]);
        } else if (library_answer != reference_answer) {
                libdiff_report(row, n, mode, a_dst, a_src, delta, value, pos, "answer differs",
                               -1, library_answer, reference_answer);
        } else {
                free(mirror);
                return 0;
        }
        free(mirror);
        libdiff_failures++;
        return 1;
}

/* Overlap deltas: the small ones at every residue, and the sizes themselves. */
static const long libdiff_small_deltas[] = {
        1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257,
};

static long libdiff_deltas(long n, long *deltas)
{
        long count = 0;
        long around[3] = { n - 1, n, n + 1 };
        deltas[count++] = 0;
        for (unsigned long i = 0; i < sizeof libdiff_small_deltas / sizeof libdiff_small_deltas[0]; i++) {
                deltas[count++] = libdiff_small_deltas[i];
                deltas[count++] = -libdiff_small_deltas[i];
        }
        for (int i = 0; i < 3; i++)
                if (around[i] > 0) {
                        deltas[count++] = around[i];
                        deltas[count++] = -around[i];
                }
        return count;
}

/*
        Every placement the row's kind names, at one size: all 64 destination
        residues against all 64 source residues, in both apart orders, for the
        copies and the swap; the byte at every position for memccpy; the
        overlap deltas at every destination residue for the memmove family.
*/
static void libdiff_sweep(const struct libdiff_row *row, long n)
{
        long deltas[64];
        long count;

        if (row->kind == LIBDIFF_FILL) {
                for (unsigned long v = 0; v < sizeof libdiff_fill_values / sizeof libdiff_fill_values[0]; v++)
                        for (long a = 0; a < 64; a += row->align)
                                libdiff_one(row, n, LIBDIFF_ONE, a, 0, 0, libdiff_fill_values[v], -1);
                return;
        }
        if (row->kind == LIBDIFF_CLEAR) {
                for (long a = 0; a < 64; a += row->align)
                        libdiff_one(row, n, LIBDIFF_ONE, a, 0, 0, 0, -1);
                if (row->zero_no_deref && n == 0)
                        libdiff_one(row, n, LIBDIFF_NODEREF, 0, 0, 0, 0, -1);
                return;
        }

        for (long a_dst = 0; a_dst < 64; a_dst += row->align)
                for (long a_src = 0; a_src < 64; a_src++) {
                        if (row->kind == LIBDIFF_UNTIL) {
                                long positions[4] = { -1, 0, n / 2, n - 1 };
                                for (int p = 0; p < 4; p++) {
                                        if (positions[p] >= n)
                                                continue;
                                        libdiff_one(row, n, LIBDIFF_APART_A, a_dst, a_src, 0, 0x0a, positions[p]);
                                        libdiff_one(row, n, LIBDIFF_APART_B, a_dst, a_src, 0, 0x0a, positions[p]);
                                }
                                continue;
                        }
                        libdiff_one(row, n, LIBDIFF_APART_A, a_dst, a_src, 0, 0, -1);
                        libdiff_one(row, n, LIBDIFF_APART_B, a_dst, a_src, 0, 0, -1);
                }

        if (row->overlap) {
                count = libdiff_deltas(n, deltas);
                for (long a_dst = 0; a_dst < 64; a_dst += row->align)
                        for (long k = 0; k < count; k++)
                                libdiff_one(row, n, LIBDIFF_OVERLAP, a_dst, 0, deltas[k], 0, -1);
        }
        if (row->zero_no_deref && n == 0)
                libdiff_one(row, n, LIBDIFF_NODEREF, 0, 0, 0, 0, -1);
}

/* Seeded random operands: one check per iteration, every kind of placement. */
static void libdiff_fuzz(const struct libdiff_row *row, long iterations)
{
        for (long k = 0; k < iterations; k++) {
                unsigned long r = libdiff_random();
                unsigned long s = libdiff_random();
                long n;
                long a_dst = (long)(s % (64 / row->align)) * row->align;
                long a_src = (long)(libdiff_random() % 64);
                unsigned long value = 0;
                long pos = -1;
                long delta = 0;
                int mode;

                if (r % 100 < 70)
                        n = (long)(s % 257);
                else if (r % 100 < 95)
                        n = (long)(s % 4097);
                else
                        n = (long)(s % 100001);

                if (row->kind == LIBDIFF_FILL) {
                        mode = LIBDIFF_ONE;
                        value = (r & 1) ? libdiff_fill_values[(r >> 1) % 8] : libdiff_random();
                } else if (row->kind == LIBDIFF_CLEAR) {
                        mode = LIBDIFF_ONE;
                } else if (row->kind == LIBDIFF_UNTIL) {
                        mode = (r & 2) ? LIBDIFF_APART_A : LIBDIFF_APART_B;
                        value = (r >> 8) & 0xff;
                        pos = (n > 0 && (r & 4)) ? (long)(libdiff_random() % (unsigned long)n) : -1;
                } else if (row->overlap && (r & 8)) {
                        mode = LIBDIFF_OVERLAP;
                        delta = (r & 16)
                                ? (long)(libdiff_random() % 129) - 64
                                : (long)(libdiff_random() % (2 * (unsigned long)(n + 8) + 1))
                                  - (long)(n + 8);
                } else {
                        mode = (r & 32) ? LIBDIFF_APART_A : LIBDIFF_APART_B;
                }
                if (row->kind == LIBDIFF_EXCHANGE)
                        mode = LIBDIFF_APART_A;
                if (n == 0 && row->zero_no_deref && (r & 64))
                        mode = LIBDIFF_NODEREF;
                if (mode == LIBDIFF_NODEREF)
                        n = 0;
                libdiff_one(row, n, mode, a_dst, a_src, delta, value, pos);
        }
}

int main(void)
{
        static const long tiers[] = {
                257, 300, 319, 320, 383, 384, 385, 447, 448, 449, 500, 511, 512, 513,
                640, 767, 768, 769, 1000, 1023, 1024, 1025, 1536, 2047, 2048, 2049,
                3000, 4095, 4096, 4097, 8191, 8192, 8193, 16383, 16384, 16385,
                32767, 32768, 32769, 65535, 65536, 65537, 100000,
        };
        unsigned char *map;
        long fuzz = 20000;

        if (getenv("LIBDIFF_FUZZ"))
                fuzz = atol(getenv("LIBDIFF_FUZZ"));

        map = mmap(NULL, LIBDIFF_USABLE + 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED)
                return 2;
        if (mprotect(map + LIBDIFF_USABLE, 4096, PROT_NONE))
                return 2;
        libdiff_end = map + LIBDIFF_USABLE;
        libdiff_pattern = malloc(LIBDIFF_USABLE + 4096);
        if (!libdiff_pattern)
                return 2;
        for (unsigned long i = 0; i < LIBDIFF_USABLE + 4096; i++)
                libdiff_pattern[i] = (unsigned char)libdiff_random();
        signal(SIGSEGV, libdiff_trap);
        signal(SIGBUS, libdiff_trap);

        for (unsigned long r = 0; r < sizeof libdiff_rows / sizeof libdiff_rows[0]; r++) {
                const struct libdiff_row *row = &libdiff_rows[r];
                long checks_before = libdiff_checks;

                if (getenv("LIBDIFF_ROW") && strcmp(getenv("LIBDIFF_ROW"), row->name) != 0)
                        continue;
                long failures_before = libdiff_failures;

                libdiff_row_failures = 0;
                for (long n = 0; n <= 256; n++)
                        libdiff_sweep(row, n);
                for (unsigned long t = 0; t < sizeof tiers / sizeof tiers[0]; t++)
                        libdiff_sweep(row, tiers[t]);
                libdiff_fuzz(row, fuzz);
                printf("libdiff %s: %ld checks, %ld failures\n", row->name,
                       libdiff_checks - checks_before, libdiff_failures - failures_before);
        }

        printf("%ld checks, %ld failures\n", libdiff_checks, libdiff_failures);
        return libdiff_failures != 0;
}
#endif /* CHECK_libdiff */
