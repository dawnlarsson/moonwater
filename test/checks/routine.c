/*
        test/checks/routine.c: the routine driver's benchmark.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef BENCH_routine
/*
        One lib.c routine at a list of sizes, interleaved against named references.

        Hosted, linked against libc, because the references are the host's own
        functions and the compiler's own output for the naive C loop. The
        routine itself is lib.c's assembly, from its own object. Built and run
        by test/run, which refuses to run it with no routine named:

            sh test/run bench routine -- --list
            sh test/run bench routine -- memory_copy --sizes 1,64,4096

        Or by hand, the same two commands on either machine:

            Mac (clang, arm64):
                clang -O2 -c src/lib.c -o $OBJ
                clang -O2 -fno-builtin -DBENCH_routine test/checks.c $OBJ -o $BIN
            Box (gcc, x86-64), in a directory of its own on the box:
                gcc -O2 -c src/lib.c -o $OBJ
                objcopy --redefine-sym _start=moonwater_start $OBJ
                gcc -O2 -fno-builtin -DBENCH_routine test/checks.c $OBJ -o $BIN

            $BIN [options] routine...      (a word without -- is a routine name)

            --list                 the routines this driver has rows for
            --sizes A,B,...        byte sizes; K and M suffixes (default: the grid below)
            --offset D,S           destination and source offsets, 0 to 63 (default 0,0)
            --tries N              repetitions, at least 15 (default 15); best and median reported
            --target-ms MS         each timed batch lasts at least this long (default 4)
            --tie PCT              two candidates within PCT percent tie (default 2)
            --fuzz N               random sizes and offsets checked before timing (default 512)
            --check                correctness only, no timing and no lock: every offset
                                   pair 0..63 by 0..63 at sizes to 512, eight offsets above
            --lock PATH            the bench lock directory (default below, or env
                                   MOONWATER_BENCH_LOCK)

        Correctness comes first and is untimed. Every candidate is run on fresh
        buffers beside the trusted one, the candidate named "c", and its
        result, its destination bytes past the count and its source bytes are
        compared with the trusted run's. A mismatch is printed and nothing is
        timed. Then the bench lock is taken: mkdir of the lock directory, which
        is released with rmdir at exit and on INT, TERM and HUP. Another holder
        is waited for, and a lock older than ten minutes is stale and removed.
        The same lock is used by every agent on the machine, so two timed runs
        never share a core.

        Timing. Each candidate is called through a function pointer, out of
        line, the way a caller calls it. A batch of calls is sized to last
        --target-ms, and the candidates run round robin, in a rotation that
        changes each repetition, so none always follows another's warm cache.
        The reported number is the best of the repetitions, with the median
        beside it. Buffers are reused, so the timed data is cache resident
        at small sizes and at the level its size lands in above that.

        The harness candidate is an empty call that returns its count. It is
        the cost of the call and nothing else, and every routine's time is
        read against it: "call floor" means the routine is no slower than the
        call that reaches it, so no further gain is measurable through this
        path.

        Candidates per row. "ours" is the lib.c routine being measured; "libc"
        is the host's function for the same job, where one exists; "c" is the
        naive C loop, compiled -O2 in this file by the compiler under test,
        and is the trusted reference. The gcc build may turn the copy loop into
        a call to memcpy, which then is what "c" measures there. Per size, the
        verdict is WON when ours is faster than every other candidate by more
        than the tie, LOST when it is slower than the fastest by more than the
        tie, and TIED otherwise. The routine's verdict is LOST if any size is
        lost, WON if every size is won, and TIED otherwise.

        Adding a routine: write one wrapper per candidate, each with the
        routine_call signature and noinline, add an extern for the lib.c
        symbol, and add one row to routine_rows. Keep one row per routine.

        Foreign targets are not timed here. A qemu tick is the emulator's work,
        not the machine's, so this driver runs natively or not at all.
*/

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#define ROUTINE_LOCK_DEFAULT "/private/tmp/claude-501/-Users-alve-moonwater/bench.lock"
#define ROUTINE_LIBC "/usr/lib/libSystem.B.dylib"
#else
#define ROUTINE_LOCK_DEFAULT "/home/dawn/mw-bench.lock"
#define ROUTINE_LIBC "libc.so.6"
#endif

/*
        lib.c exports its own memcpy, strlen, sleep and exit, so a call to any
        of those by name in this link reaches the library's, not the host's.
        The host's memcpy and strlen are taken from the host's library by
        dlsym, and the driver's own string compare and integer parse replace
        the rest, so apart from the routines under test it calls no name lib.c
        defines. Its lock is released by main on every way out, not by exit(),
        for the same reason.
*/
typedef unsigned char routine_byte;
typedef unsigned long routine_size;

static int routine_same(const char *a, const char *b)
{
        while (*a && *a == *b)
        {
                a++;
                b++;
        }
        return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int routine_int(const char *text)
{
        return (int)strtol(text, 0, 10);
}

static void *(*routine_libc_memcpy)(void *, const void *, size_t);
static size_t (*routine_libc_strlen)(const char *);

static int routine_libc_open(void)
{
        void *handle = dlopen(ROUTINE_LIBC, RTLD_NOW);

        if (!handle)
                return 0;
        routine_libc_memcpy = (void *(*)(void *, const void *, size_t))dlsym(handle, "memcpy");
        routine_libc_strlen = (size_t(*)(const char *))dlsym(handle, "strlen");
        return routine_libc_memcpy && routine_libc_strlen;
}

/* The lib.c entry points this driver times. The object supplies them. */
extern void memory_copy(routine_byte *into, const routine_byte *from, routine_size count);
extern routine_size string_length(const routine_byte *source);
extern routine_size string_copy_bounded(routine_byte *into, const routine_byte *from, routine_size capacity);
extern routine_size string_copy_bounded_3_16(routine_byte *into, const routine_byte *from, routine_size capacity);
extern void moonwater_cpu_detect(void);

/* lib.c's logging and buffered writers reach these two, which on some
   targets (Mach-O arm64 among them) are not in the object. The routines timed
   here never write, so a weak stand-in is linked only where nothing better is,
   and it stops the run if anything does call it. */
/* The asm labels name the symbols the assembly branches to, which on Mach-O
   carry no leading underscore. */
__attribute__((weak)) routine_size routine_writer_stub(routine_size fd, const routine_byte *data, routine_size count)
        __asm__("system_write_all");
__attribute__((weak)) routine_byte routine_writer_flag __asm__("writer_stderr_failed");

__attribute__((weak)) routine_size routine_writer_stub(routine_size fd, const routine_byte *data, routine_size count)
{
        (void)fd;
        (void)data;
        (void)count;
        abort();
}

/* What every candidate is called as: a destination, a source, and a count.
   The result is what the routine returns, or 0 where it returns nothing. */
typedef routine_size (*routine_call)(routine_byte *dst, const routine_byte *src, routine_size n);

struct routine_candidate
{
        const char *name;
        routine_call run;
};

struct routine_row
{
        const char *name;
        const char *shape;
        struct routine_candidate candidate[6]; /* NULL name ends the list */
        routine_size lo, hi;                   /* a size class: the bytes it covers, both ends in; 0, 0 is every size */
};

/* Whether a size belongs to the row: a size class's row is timed and checked
   only inside its class, the generic row at every size. */
static int routine_in(const struct routine_row *row, routine_size n)
{
        return row->hi == 0 || (n >= row->lo && n <= row->hi);
}

#define ROUTINE_CALL static __attribute__((noinline)) routine_size
#define ROUTINE_GUARD 64
#define ROUTINE_MAX (16UL << 20)
#define ROUTINE_PAD 256
#define ROUTINE_TRIES_MAX 256
#define ROUTINE_CANDIDATES 8

/* Four arenas: 0 and 1 are the timed buffers and the trusted run's, 2 and 3
   the candidate's in the correctness check. */
static routine_byte routine_arena[4][ROUTINE_MAX + ROUTINE_PAD] __attribute__((aligned(64)));
static volatile routine_size routine_sink;

ROUTINE_CALL memory_copy_ours(routine_byte *d, const routine_byte *s, routine_size n)
{
        memory_copy(d, s, n);
        return 0;
}

ROUTINE_CALL memory_copy_libc(routine_byte *d, const routine_byte *s, routine_size n)
{
        routine_libc_memcpy(d, s, n);
        return 0;
}

ROUTINE_CALL memory_copy_c(routine_byte *d, const routine_byte *s, routine_size n)
{
        for (routine_size i = 0; i < n; i++)
                d[i] = s[i];
        return 0;
}

ROUTINE_CALL string_length_ours(routine_byte *d, const routine_byte *s, routine_size n)
{
        (void)d;
        (void)n;
        return string_length(s);
}

ROUTINE_CALL string_length_libc(routine_byte *d, const routine_byte *s, routine_size n)
{
        (void)d;
        (void)n;
        return routine_libc_strlen((const char *)s);
}

ROUTINE_CALL string_length_c(routine_byte *d, const routine_byte *s, routine_size n)
{
        routine_size i = 0;
        (void)d;
        (void)n;
        while (s[i])
                i++;
        return i;
}

/* memory_frob and its size classes: XOR with 42 in place. The wrappers return
   0 so the result compare cannot differ on arena addresses. */
extern void *memory_frob(void *block, routine_size size);
extern void *memory_frob_8_8(void *block, routine_size size);

ROUTINE_CALL memory_frob_generic(routine_byte *d, const routine_byte *s, routine_size n)
{
        (void)s;
        memory_frob(d, n);
        return 0;
}

ROUTINE_CALL memory_frob_8_8_ours(routine_byte *d, const routine_byte *s, routine_size n)
{
        (void)s;
        memory_frob_8_8(d, n);
        return 0;
}

ROUTINE_CALL memory_frob_c(routine_byte *d, const routine_byte *s, routine_size n)
{
        (void)s;
        for (routine_size i = 0; i < n; i++)
                d[i] ^= 42;
        return 0;
}

ROUTINE_CALL harness_empty(routine_byte *d, const routine_byte *s, routine_size n)
{
        (void)d;
        (void)s;
        return n;
}

/* string_copy_bounded: n is the capacity. The source is the driver's string of
   length n, so every timed call copies n - 1 bytes and terminates (the
   truncating case). "c" is the naive strlcpy, which the compiler under test
   builds; it is the trusted reference. */
ROUTINE_CALL string_copy_bounded_class(routine_byte *d, const routine_byte *s, routine_size n)
{
        return string_copy_bounded_3_16(d, s, n);
}

ROUTINE_CALL string_copy_bounded_ours(routine_byte *d, const routine_byte *s, routine_size n)
{
        return string_copy_bounded(d, s, n);
}

ROUTINE_CALL string_copy_bounded_c(routine_byte *d, const routine_byte *s, routine_size n)
{
        routine_size len = 0;

        while (s[len])
                len++;
        if (n)
        {
                routine_size k = len < n - 1 ? len : n - 1;

                for (routine_size i = 0; i < k; i++)
                        d[i] = s[i];
                d[k] = 0;
        }
        return len;
}

/* The driver's own check fills every source to exactly its size, so it never
   reaches a source shorter than the capacity. This sweep does: every source
   length from 0 to 40 (short, equal and longer than the capacity), every
   capacity from 0 to 20, and every destination and source offset pair from
   0 to 63. Each run is compared with the C reference on the result, the bytes
   of the destination through a guard past the capacity, and the source. */
static int routine_copy_sweep(void)
{
        static routine_byte src0[256], src1[256], dst0[256], dst1[256];
        int bad = 0;
        long runs = 0;

        for (routine_size od = 0; od < 64; od++)
                for (routine_size os = 0; os < 64; os++)
                        for (routine_size len = 0; len <= 40; len++)
                                for (routine_size cap = 0; cap <= 16; cap++)
                                {
                                        routine_size want, got;

                                        for (routine_size i = 0; i < 256; i++)
                                        {
                                                src0[i] = (routine_byte)(1 + (i * 37u + len) % 255u);
                                                dst0[i] = (routine_byte)(i * 131u + 0x5a);
                                                src1[i] = src0[i];
                                                dst1[i] = dst0[i];
                                        }
                                        src0[os + len] = 0;
                                        src1[os + len] = 0;
                                        want = string_copy_bounded_c(dst0 + od, src0 + os, cap);
                                        got = string_copy_bounded_3_16(dst1 + od, src1 + os, cap);
                                        runs++;
                                        if (want != got)
                                        {
                                                if (bad++ < 8)
                                                        printf("SWEEP len=%lu cap=%lu off=%lu,%lu: result %lu, trusted %lu\n",
                                                               len, cap, od, os, got, want);
                                                continue;
                                        }
                                        for (routine_size i = 0; i < 256; i++)
                                                if (dst0[i] != dst1[i])
                                                {
                                                        if (bad++ < 8)
                                                                printf("SWEEP len=%lu cap=%lu off=%lu,%lu: destination byte %lu is %02x, trusted %02x\n",
                                                                       len, cap, od, os, i, dst1[i], dst0[i]);
                                                        break;
                                                }
                                        for (routine_size i = 0; i < 256; i++)
                                                if (src0[i] != src1[i])
                                                {
                                                        if (bad++ < 8)
                                                                printf("SWEEP len=%lu cap=%lu off=%lu,%lu: source byte %lu changed\n",
                                                                       len, cap, od, os, i);
                                                        break;
                                                }
                                }
        printf("sweep string_copy_bounded_3_16 %ld runs, %d mismatched\n", runs, bad);
        return bad != 0;
}

/* f32_pair_to_s16 (lib.c): the sink's interleaved 16-bit stereo from two float planes. The driver's size is bytes, and a
   frame is 8 of them (left and right, a float each), so the source it fills is exactly the two planes and frames = bytes / 8.
   The destination is 4 bytes a frame. The reference is the C the mixer made of it (audio_c_encode format 0), held out of line
   as the candidates are, so every candidate pays one call and the same wrapper depth. */
extern void f32_pair_to_s16(unsigned char *dst, const float *left, const float *right, unsigned long count);
extern void f32_pair_to_s16_5_7(unsigned char *dst, const float *left, const float *right, unsigned long count);
extern void f32_pair_to_s16_3_3(unsigned char *dst, const float *left, const float *right, unsigned long count);
extern void f32_pair_to_s16_1_2(unsigned char *dst, const float *left, const float *right, unsigned long count);

static int routine_round16(float v)
{
        float r;
        int bits;

        if (v != v)
                return 0;
        v *= 32768.0f;
        v = v > 32767.0f ? 32767.0f : v;
        v = v < -32768.0f ? -32768.0f : v;
        r = v + 12582912.0f;
        __builtin_memcpy(&bits, &r, 4);
        return (bits & 0x7fffff) - 0x400000;
}

static __attribute__((noinline)) void f32_pair_to_s16_cref(routine_byte *d, const float *left, const float *right, routine_size n)
{
        for (routine_size i = 0; i < n; i++)
        {
                short x0 = (short)routine_round16(left[i]), x1 = (short)routine_round16(right[i]);

                __builtin_memcpy(d + i * 4, &x0, 2);
                __builtin_memcpy(d + i * 4 + 2, &x1, 2);
        }
}

ROUTINE_CALL f32_pair_to_s16_asm(routine_byte *d, const routine_byte *s, routine_size bytes)
{
        const float *left = (const float *)s;
        routine_size n = bytes >> 3;

        f32_pair_to_s16(d, left, left + n, n);
        return 0;
}

ROUTINE_CALL f32_pair_to_s16_5_7_asm(routine_byte *d, const routine_byte *s, routine_size bytes)
{
        const float *left = (const float *)s;
        routine_size n = bytes >> 3;

        f32_pair_to_s16_5_7(d, left, left + n, n);
        return 0;
}

ROUTINE_CALL f32_pair_to_s16_3_3_asm(routine_byte *d, const routine_byte *s, routine_size bytes)
{
        const float *left = (const float *)s;
        routine_size n = bytes >> 3;

        f32_pair_to_s16_3_3(d, left, left + n, n);
        return 0;
}

ROUTINE_CALL f32_pair_to_s16_1_2_asm(routine_byte *d, const routine_byte *s, routine_size bytes)
{
        const float *left = (const float *)s;
        routine_size n = bytes >> 3;

        f32_pair_to_s16_1_2(d, left, left + n, n);
        return 0;
}

ROUTINE_CALL f32_pair_to_s16_c(routine_byte *d, const routine_byte *s, routine_size bytes)
{
        const float *left = (const float *)s;
        routine_size n = bytes >> 3;

        f32_pair_to_s16_cref(d, left, left + n, n);
        return 0;
}

/* One row per routine. The names are the lib.c symbols. */
static const struct routine_row routine_rows[] = {
        {"memory_copy", "copy",
         {{"ours", memory_copy_ours}, {"libc", memory_copy_libc}, {"c", memory_copy_c}, {0, 0}}},
        {"string_length", "scan",
         {{"ours", string_length_ours}, {"libc", string_length_libc}, {"c", string_length_c}, {0, 0}}},
        {"string_copy_bounded_3_16", "copy",
         {{"ours", string_copy_bounded_class}, {"generic", string_copy_bounded_ours}, {"c", string_copy_bounded_c}, {0, 0}}},
        {"f32_pair_to_s16", "bytes8",
         {{"ours", f32_pair_to_s16_asm}, {"c", f32_pair_to_s16_c}, {0, 0}}},
        {"f32_pair_to_s16_5_7", "bytes8",
         {{"ours", f32_pair_to_s16_5_7_asm}, {"generic", f32_pair_to_s16_asm}, {"c", f32_pair_to_s16_c}, {0, 0}}, 40, 63},
        {"f32_pair_to_s16_1_2", "bytes8",
         {{"ours", f32_pair_to_s16_1_2_asm}, {"generic", f32_pair_to_s16_asm}, {"c", f32_pair_to_s16_c}, {0, 0}}, 8, 23},
        {"f32_pair_to_s16_3_3", "bytes8",
         {{"ours", f32_pair_to_s16_3_3_asm}, {"generic", f32_pair_to_s16_asm}, {"c", f32_pair_to_s16_c}, {0, 0}}, 24, 31},
        {"memory_frob", "frob",
         {{"ours", memory_frob_generic}, {"c", memory_frob_c}, {0, 0}}},
        {"memory_frob_8_8", "frob",
         {{"ours", memory_frob_8_8_ours}, {"generic", memory_frob_generic}, {"c", memory_frob_c}, {0, 0}}, 8, 8},
};

static const routine_size routine_default_sizes[] = {
        0,      1,      2,       3,       4,       5,       7,        8,       9,       15,      16,
        17,     31,     32,      33,      63,      64,      65,       127,     128,     129,     255,
        256,    257,    511,     512,     513,     1024,    4096,     16384,   65536,   262144,  1048576,
};

static double routine_now(void)
{
        struct timespec t;

        clock_gettime(CLOCK_MONOTONIC, &t);
        return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

/* The bench lock. A directory, so mkdir is the atomic test-and-set. */
static const char *routine_lock_path;
static volatile sig_atomic_t routine_lock_held;

static void routine_unlock(void)
{
        if (routine_lock_held)
        {
                routine_lock_held = 0;
                rmdir(routine_lock_path);
        }
}

static void routine_signalled(int sig)
{
        routine_unlock();
        _exit(128 + sig);
}

/* Returns 0 with the lock held, or 2 with a message. The lock is released by
   routine_unlock, which main calls on every way out, and by the signal
   handlers, so nothing relies on exit() running an atexit list. */
static int routine_lock_take(const char *path)
{
        int said = 0;
        struct timespec pause = {2, 0};

        routine_lock_path = path;
        for (;;)
        {
                struct stat st;

                if (mkdir(path, 0700) == 0)
                        break;
                if (errno != EEXIST)
                {
                        perror(path);
                        return 2;
                }
                if (stat(path, &st) == 0 && time(0) - st.st_mtime > 600)
                {
                        fprintf(stderr, "removing the stale bench lock %s\n", path);
                        rmdir(path);
                        continue;
                }
                if (!said)
                {
                        fprintf(stderr, "waiting for the bench lock %s\n", path);
                        said = 1;
                }
                nanosleep(&pause, 0);
        }
        routine_lock_held = 1;
        signal(SIGINT, routine_signalled);
        signal(SIGTERM, routine_signalled);
        signal(SIGHUP, routine_signalled);
        return 0;
}

/* Fill the span a run may touch with a pattern that depends on the seed.
   The source is never zero below its count and is zero at it, so the same
   buffers serve copies and string lengths. */
static void routine_prepare(routine_size n, routine_size off_d, routine_size off_s, unsigned long seed,
                            routine_byte *dst_arena, routine_byte *src_arena)
{
        routine_size dst_span = off_d + n + ROUTINE_GUARD;
        routine_size src_span = off_s + n + 1;

        for (routine_size i = 0; i < dst_span; i++)
                dst_arena[i] = (routine_byte)(i * 131u + seed * 7u + 0x5a);
        for (routine_size i = 0; i < src_span; i++)
                src_arena[i] = (routine_byte)(1 + (i * 37u + seed * 11u) % 255u);
        src_arena[off_s + n] = 0;
}

static void routine_batch_total(routine_call run, routine_byte *d, const routine_byte *s, routine_size n,
                               routine_size reps, double *ns)
{
        routine_size acc = 0;
        double start = routine_now();

        for (routine_size i = 0; i < reps; i++)
                acc += run(d, s, n);
        *ns = routine_now() - start;
        routine_sink += acc;
}

static routine_size routine_reps_for(routine_call run, routine_byte *d, const routine_byte *s, routine_size n,
                                     double target_ns)
{
        routine_size reps = 1;

        for (;;)
        {
                double took = 0;

                routine_batch_total(run, d, s, n, reps, &took);
                if (took >= target_ns || reps >= ((routine_size)1 << 40))
                        return reps;
                {
                        double grow = took > 0 ? target_ns * 1.1 / took : 16.0;

                        if (grow > 16.0)
                                grow = 16.0;
                        if (grow < 2.0)
                                grow = 2.0;
                        reps = (routine_size)((double)reps * grow) + 1;
                }
        }
}

static int routine_double_order(const void *a, const void *b)
{
        double x = *(const double *)a;
        double y = *(const double *)b;

        return (x > y) - (x < y);
}

/* Best and median ns per call of each candidate at one size, the candidates
   interleaved round robin with the start moved each repetition. */
static void routine_measure(const struct routine_candidate *cand, int count, routine_size n, routine_size off_d,
                            routine_size off_s, int tries, double target_ns, double *best, double *median)
{
        static double sample[ROUTINE_CANDIDATES][ROUTINE_TRIES_MAX];
        routine_size reps[ROUTINE_CANDIDATES];
        routine_byte *d = routine_arena[0] + off_d;
        const routine_byte *s = routine_arena[1] + off_s;

        routine_prepare(n, off_d, off_s, 0x1234, routine_arena[0], routine_arena[1]);
        for (int k = 0; k < count; k++)
                reps[k] = routine_reps_for(cand[k].run, d, s, n, target_ns);

        for (int r = 0; r < tries; r++)
                for (int j = 0; j < count; j++)
                {
                        int k = (r + j) % count;
                        double took = 0;

                        routine_batch_total(cand[k].run, d, s, n, reps[k], &took);
                        sample[k][r] = took / (double)reps[k];
                }

        for (int k = 0; k < count; k++)
        {
                double *row = sample[k];

                qsort(row, (size_t)tries, sizeof row[0], routine_double_order);
                best[k] = row[0];
                median[k] = row[tries / 2];
        }
}

/* Correctness: the trusted candidate and one other, on fresh buffers from the
   same pattern, compared on the result, the destination through its guard and
   the source. Returns the number of differing runs (0 or 1). */
static int routine_shown;

#define ROUTINE_SHOWN 16

static int routine_check_one(const struct routine_candidate *trusted, const struct routine_candidate *cand,
                             routine_size n, routine_size off_d, routine_size off_s, unsigned long seed)
{
        routine_byte *d0 = routine_arena[0], *s0 = routine_arena[1];
        routine_byte *d1 = routine_arena[2], *s1 = routine_arena[3];
        routine_size want, got, dst_span = off_d + n + ROUTINE_GUARD, src_span = off_s + n + 1;

        routine_prepare(n, off_d, off_s, seed, d0, s0);
        routine_prepare(n, off_d, off_s, seed, d1, s1);
        want = trusted->run(d0 + off_d, s0 + off_s, n);
        got = cand->run(d1 + off_d, s1 + off_s, n);

        if (want != got)
        {
                if (routine_shown++ < ROUTINE_SHOWN)
                        printf("MISMATCH %s n=%lu off=%lu,%lu seed=%lu: result %lu, trusted %lu\n", cand->name, n,
                               off_d, off_s, seed, got, want);
                return 1;
        }
        for (routine_size i = 0; i < dst_span; i++)
                if (d0[i] != d1[i])
                {
                        if (routine_shown++ < ROUTINE_SHOWN)
                                printf("MISMATCH %s n=%lu off=%lu,%lu seed=%lu: destination byte %lu is %02x, trusted %02x\n",
                                       cand->name, n, off_d, off_s, seed, i, d1[i], d0[i]);
                        return 1;
                }
        for (routine_size i = 0; i < src_span; i++)
                if (s0[i] != s1[i])
                {
                        if (routine_shown++ < ROUTINE_SHOWN)
                                printf("MISMATCH %s n=%lu off=%lu,%lu seed=%lu: source byte %lu was changed\n",
                                       cand->name, n, off_d, off_s, seed, i);
                        return 1;
                }
        return 0;
}

static int routine_check_row(const struct routine_row *row, const routine_size *sizes, int size_count,
                             int full, int fuzz, routine_size off_d, routine_size off_s)
{
        const struct routine_candidate *trusted = 0;
        int swept = 0;

        if (full && !routine_same(row->name, "string_copy_bounded_3_16"))
                swept = routine_copy_sweep();
        int bad = 0, runs = 0;
        static const routine_size eight[8] = {0, 1, 3, 7, 8, 15, 31, 63};
        unsigned long state = 88172645463325252ul;

        for (int i = 0; row->candidate[i].name; i++)
                if (!routine_same(row->candidate[i].name, "c"))
                        trusted = &row->candidate[i];
        if (!trusted)
        {
                printf("%s: no trusted candidate named c\n", row->name);
                return 1;
        }

        /* --check: every offset pair at sizes to 512, eight offsets above.
           Otherwise the one pair being timed, at every size in the list. */
        for (int z = 0; z < size_count; z++)
        {
                routine_size n = sizes[z];
                int wide = full && n <= 512;

                if (!routine_in(row, n))
                        continue;
                routine_size span = full ? (wide ? 64 : 8) : 1;

                for (int i = 0; row->candidate[i].name; i++)
                {
                        if (row->candidate[i].run == trusted->run)
                                continue;
                        for (routine_size a = 0; a < span; a++)
                                for (routine_size b = 0; b < span; b++)
                                {
                                        routine_size od = !full ? off_d : (wide ? a : eight[a]);
                                        routine_size os = !full ? off_s : (wide ? b : eight[b]);

                                        runs++;
                                        bad += routine_check_one(trusted, &row->candidate[i], n, od, os, 1 + a + 64 * b);
                                }
                }
        }

        for (int f = 0; f < fuzz; f++)
        {
                routine_size n, od, os;

                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                n = row->hi ? row->lo + (routine_size)(state % (row->hi - row->lo + 1)) : (routine_size)(state % 70001);
                od = (routine_size)((state >> 20) % 64);
                os = (routine_size)((state >> 32) % 64);
                for (int i = 0; row->candidate[i].name; i++)
                {
                        if (row->candidate[i].run == trusted->run)
                                continue;
                        runs++;
                        bad += routine_check_one(trusted, &row->candidate[i], n, od, os, 1000 + (unsigned long)f);
                }
        }

        printf("check %-14s %d runs, %d mismatched\n", row->name, runs, bad);
        return bad != 0 || swept;
}

static const struct routine_row *routine_find(const char *name)
{
        for (size_t i = 0; i < sizeof routine_rows / sizeof routine_rows[0]; i++)
                if (!routine_same(routine_rows[i].name, name))
                        return &routine_rows[i];
        return 0;
}

static int routine_parse_sizes(const char *text, routine_size *out, int max)
{
        int count = 0;

        while (*text)
        {
                char *end;
                unsigned long v = strtoul(text, &end, 10);

                if (end == text)
                        return -1;
                if (*end == 'K' || *end == 'k')
                {
                        v <<= 10;
                        end++;
                }
                else if (*end == 'M' || *end == 'm')
                {
                        v <<= 20;
                        end++;
                }
                if (count >= max || v > ROUTINE_MAX - 2 * ROUTINE_GUARD)
                        return -1;
                out[count++] = v;
                if (*end == ',')
                        end++;
                else if (*end)
                        return -1;
                text = end;
        }
        return count;
}

static void routine_usage(void)
{
        fprintf(stderr, "usage: routine [--list] [--sizes A,B] [--offset D,S] [--tries N] [--target-ms MS]\n"
                        "               [--tie PCT] [--fuzz N] [--check] [--lock PATH] routine...\n");
}

#define ROUTINE_SIZES_MAX 128

/* lib.c's feature bytes are set by moonwater_cpu_detect, and the startup
   assembly calls that before main in a program. A hosted build is started by
   the C library instead, so nothing sets the bytes, every routine that asks
   one takes its narrowest path, and a timing is of the wrong body. This
   driver calls the detector first of all, before any timed or checked call,
   and prints what it found so each run says what it measured. The asm labels
   name the symbols the assembly uses, with no leading underscore on Mach-O. */
extern void moonwater_cpu_detect(void);
#if !defined(__APPLE__)
extern unsigned char routine_cpu_aes __asm__("cpu_has_aes");
extern unsigned char routine_cpu_pclmul __asm__("cpu_has_pclmul");
#endif
#if defined(__x86_64__)
extern unsigned char routine_cpu_fma __asm__("cpu_has_fma");
extern unsigned char routine_cpu_vaes __asm__("cpu_has_vaes");
extern unsigned char routine_cpu_vpclmul __asm__("cpu_has_vpclmul");
extern unsigned char routine_cpu_avx2 __asm__("cpu_has_avx2");
extern unsigned char routine_cpu_avx512 __asm__("cpu_has_avx512");
extern unsigned char routine_cpu_vbmi __asm__("cpu_has_avx512_vbmi");
#endif

static int routine_main(int argc, char **argv)
{
        routine_size sizes[ROUTINE_SIZES_MAX];

        /* Before any timing: the generic routines pick their bodies from what this processor has. */
        moonwater_cpu_detect();

        moonwater_cpu_detect();
        int size_count = (int)(sizeof routine_default_sizes / sizeof routine_default_sizes[0]);
        routine_size off_d = 0, off_s = 0;
        int tries = 15, fuzz = 512, check_only = 0, list_only = 0, names = 0;
        double target_ms = 4.0, tie = 2.0;
        const char *lock = getenv("MOONWATER_BENCH_LOCK");
        const char *selected[16];
        struct utsname un;

        if (!routine_libc_open())
        {
                fprintf(stderr, "routine: cannot reach the host memcpy and strlen through %s\n", ROUTINE_LIBC);
                return 2;
        }

        for (int i = 0; i < size_count; i++)
                sizes[i] = routine_default_sizes[i];
        if (!lock)
                lock = ROUTINE_LOCK_DEFAULT;

        for (int i = 1; i < argc; i++)
        {
                const char *a = argv[i];

                if (!routine_same(a, "--list"))
                        list_only = 1;
                else if (!routine_same(a, "--check"))
                        check_only = 1;
                else if (!routine_same(a, "--sizes") && i + 1 < argc)
                {
                        size_count = routine_parse_sizes(argv[++i], sizes, ROUTINE_SIZES_MAX);
                        if (size_count <= 0)
                        {
                                fprintf(stderr, "routine: bad --sizes\n");
                                return 2;
                        }
                }
                else if (!routine_same(a, "--offset") && i + 1 < argc)
                {
                        if (sscanf(argv[++i], "%lu,%lu", &off_d, &off_s) != 2 || off_d > 63 || off_s > 63)
                        {
                                fprintf(stderr, "routine: --offset is D,S with both 0 to 63\n");
                                return 2;
                        }
                }
                else if (!routine_same(a, "--tries") && i + 1 < argc)
                {
                        tries = routine_int(argv[++i]);
                        if (tries < 15 || tries > ROUTINE_TRIES_MAX)
                        {
                                fprintf(stderr, "routine: --tries is at least 15 and at most %d\n", ROUTINE_TRIES_MAX);
                                return 2;
                        }
                }
                else if (!routine_same(a, "--target-ms") && i + 1 < argc)
                        target_ms = atof(argv[++i]);
                else if (!routine_same(a, "--tie") && i + 1 < argc)
                        tie = atof(argv[++i]);
                else if (!routine_same(a, "--fuzz") && i + 1 < argc)
                        fuzz = routine_int(argv[++i]);
                else if (!routine_same(a, "--lock") && i + 1 < argc)
                        lock = argv[++i];
                else if (a[0] == '-' && a[1] == '-')
                {
                        routine_usage();
                        return 2;
                }
                else if (names < 16)
                        selected[names++] = a;
                else
                {
                        fprintf(stderr, "routine: at most 16 routines at once\n");
                        return 2;
                }
        }

        if (list_only)
        {
                for (size_t i = 0; i < sizeof routine_rows / sizeof routine_rows[0]; i++)
                {
                        printf("%-16s %-6s candidates:", routine_rows[i].name, routine_rows[i].shape);
                        for (int k = 0; routine_rows[i].candidate[k].name; k++)
                                printf(" %s", routine_rows[i].candidate[k].name);
                        printf("\n");
                }
                return 0;
        }
        if (!names)
        {
                routine_usage();
                return 2;
        }

        for (int i = 0; i < names; i++)
                if (!routine_find(selected[i]))
                {
                        fprintf(stderr, "routine: no row called %s (--list)\n", selected[i]);
                        return 2;
                }

        /* Correctness first, untimed, with or without the lock: a candidate
           that is wrong is not timed. */
        {
                int bad = 0;

                for (int i = 0; i < names; i++)
                        bad |= routine_check_row(routine_find(selected[i]), sizes, size_count, check_only, fuzz,
                                                 off_d, off_s);
                if (bad)
                {
                        printf("correctness failed: nothing timed\n");
                        return 1;
                }
                if (check_only)
                {
                        printf("correctness ok\n");
                        return 0;
                }
        }

        uname(&un);
        if (routine_lock_take(lock))
                return 2;

        printf("arch %s, %s, %s; offset %lu,%lu, tries %d, target %.1f ms, tie %.1f%%\n", un.machine, un.sysname,
               un.nodename, off_d, off_s, tries, target_ms, tie);
        printf("compiler: %s\n", __VERSION__);
#if defined(__x86_64__)
        printf("cpu bytes: aes %d pclmul %d fma %d vaes %d vpclmul %d avx2 %d avx512 %d avx512_vbmi %d\n",
               routine_cpu_aes, routine_cpu_pclmul, routine_cpu_fma, routine_cpu_vaes, routine_cpu_vpclmul,
               routine_cpu_avx2, routine_cpu_avx512, routine_cpu_vbmi);
#elif !defined(__APPLE__)
        printf("cpu bytes: aes %d pclmul %d\n", routine_cpu_aes, routine_cpu_pclmul);
#endif

        for (int i = 0; i < names; i++)
        {
                const struct routine_row *row = routine_find(selected[i]);
                struct routine_candidate cand[ROUTINE_CANDIDATES];
                int count = 0, ours = -1, harness = -1, all_won = 1, any_lost = 0;
                double best[ROUTINE_CANDIDATES], median[ROUTINE_CANDIDATES];

                for (int k = 0; row->candidate[k].name; k++)
                {
                        cand[count] = row->candidate[k];
                        if (!routine_same(cand[count].name, "ours"))
                                ours = count;
                        count++;
                }
                cand[count].name = "harness";
                cand[count].run = harness_empty;
                harness = count;
                count++;
                if (ours < 0)
                {
                        fprintf(stderr, "%s: no candidate named ours\n", row->name);
                        return 2;
                }

                printf("\n%s (%s)\n%12s", row->name, row->shape, "bytes");
                for (int k = 0; k < count; k++)
                        printf(" %10s", cand[k].name);
                printf(" %10s %10s  verdict\n", "ours ns/B", "ours med");

                for (int z = 0; z < size_count; z++)
                {
                        routine_size n = sizes[z];
                        const char *word;

                        if (!routine_in(row, n))
                                continue;
                        double ref = 0;
                        const char *ref_name = "";
                        double floor_ns = 0;

                        if (n + off_d + ROUTINE_GUARD > ROUTINE_MAX || n + off_s + 1 > ROUTINE_MAX)
                                continue;
                        routine_measure(cand, count, n, off_d, off_s, tries, target_ms * 1e6, best, median);

                        floor_ns = best[harness];
                        for (int k = 0; k < count; k++)
                                if (k != ours && k != harness && (!ref_name[0] || best[k] < ref))
                                {
                                        ref = best[k];
                                        ref_name = cand[k].name;
                                }

                        if (!ref_name[0])
                                word = "NOT MEASURED";
                        else if (best[ours] < ref * (1 - tie / 100))
                                word = "won";
                        else if (best[ours] <= ref * (1 + tie / 100))
                                word = "tied";
                        else
                        {
                                word = "lost";
                                any_lost = 1;
                        }
                        if (word[0] != 'w')
                                all_won = 0;

                        printf("%12lu", n);
                        for (int k = 0; k < count; k++)
                                printf(" %10.3f", best[k]);
                        if (n)
                                printf(" %10.5f %10.3f", best[ours] / (double)n, median[ours]);
                        else
                                printf(" %10s %10.3f", "-", median[ours]);
                        printf("  %s", word);
                        if (ref_name[0] && word[0] == 'l')
                                printf(" to %s", ref_name);
                        if (best[ours] <= floor_ns * (1 + tie / 100))
                                printf(" (call floor)");
                        printf("\n");
                }

                printf("verdict %s: %s\n", row->name,
                       any_lost ? "LOST" : (all_won ? "WON" : "TIED"));
        }

        printf("sink %lu\n", (unsigned long)routine_sink);
        return 0;
}

/* The lock is released on every way out of the run. */
int main(int argc, char **argv)
{
        int status = routine_main(argc, argv);

        routine_unlock();
        return status;
}
#endif /* BENCH_routine */

