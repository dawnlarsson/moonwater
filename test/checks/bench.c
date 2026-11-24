/*
        test/checks/bench.c: the benchmarks: the floor, numbers and bases, and pixels and memory.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

/*
        The benchmarks, which are sections too.

            gcc ... -DBENCH_floor test/checks.c        the floor
            gcc ... -DBENCH_grep_count test/checks.c   counting matches

        sh test/run bench builds these and sh test/run never does. One
        section each, with the two pieces they share -- the timing scaffold
        and the scalar references -- reached through the SHARED_ names
        above, where an #include line would stand.

        Six are not in test/run's catalogue, because nothing on this side of
        the machine can run them: BENCH_exec, BENCH_network_spawn,
        BENCH_shell_document and BENCH_tiny are built by `build spark` and run
        inside the booted image, BENCH_startup_empty is the host-kernel floor
        they subtract, and BENCH_one is one function at one length for
        test/insns, which picks which with -DWHICH -DLENGTH -DROUNDS.
*/

#ifdef BENCH_floor
/*
        Every routine in lib.c, against the speed of the machine.

        The lower-bound candidate is a loop that moves the unavoidable traffic
        and computes nothing.  A semantic routine's useful question is how
        much slower it remains.  This is an empirical bound, not a declaration:
        when the real routine beats it, the report says the bound is unresolved
        instead of printing an impossible efficiency or pretending victory.

        Traffic matters and is measured rather than multiplied.  Two streams
        can overlap in the load/store machinery, and reads and writes are not
        symmetric.  Multiplying a one-stream time by a byte count produced
        impossible results above 100 percent.  Copy, fill and paired-read each
        have their own assembly floor which performs the same traffic shape.

        The number to distrust is a small one: at four bytes a call costs more
        than the work, so the column says what the routine costs to reach, not
        how fast it runs. The large sizes are the ones that mean anything about
        the loop.

            sh test/run bench floor

        A native run is hardware evidence.  A foreign run under qemu remains
        useful for instruction-shape comparison, but is labelled as emulated
        by the dispatcher and must not be quoted as an architectural floor.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))

static p8 one[1 << 22] __attribute__((aligned(64)));
static p8 two[1 << 22] __attribute__((aligned(64)));
static p8 out[1 << 22] __attribute__((aligned(64)));

static positive sizes[] = {3,   8,   15,  16,  17,  31,  32,   33,  63,  64,  65,
                           127, 128, 129, 255, 256, 257, 512,  1024, 2048, 4096, 65536, 1048576};

/*
        The floor, in assembly, for the same reason the routines are.

        Two earlier versions of this were wrong in opposite directions and both
        looked plausible. Stepping a cache line at a time moves the same
        traffic but issues a sixty fourth of the loads, so in cache it beats
        anything that must look at every byte and every routine read as slack
        that was not there. Writing it in C with an asm body and a memory
        clobber made the compiler reload each turn, so the floor measured
        itself and every routine came out above it.

        A ceiling has to be built the way the thing under it is built. This
        reads every byte at the widest load the machine has, accumulates into
        registers nothing ever looks at, and is written out by hand so no
        compiler decides how to schedule it.
*/
__asm__(
    ".text\n"
    ASM_FUNC(floor_ticks)
#if X64
    "lfence\n   rdtsc\n   shl $32, %rdx\n   or %rdx, %rax\n   lfence\n"
#elif ARM64
    "isb\n   mrs x0, cntvct_el0\n   isb\n"
#else
    "fence iorw, iorw\n   rdtime a0\n   fence iorw, iorw\n"
#endif
    ASM_RET
    ASM_END(floor_ticks)
    ASM_FUNC(floor_read)
#if X64
    "xor %eax, %eax\n   test %rsi, %rsi\n   jz 9f\n"
    "cmpb $0, cpu_has_avx2(%rip)\n   je 5f\n"
    "cmpb $0, cpu_has_avx512(%rip)\n   jne 7f\n"
    "1:  cmp $128, %rsi\n   jb 4f\n"
    "vmovdqu 0(%rdi), %ymm0\n   vmovdqu 32(%rdi), %ymm1\n"
    "vmovdqu 64(%rdi), %ymm2\n   vmovdqu 96(%rdi), %ymm3\n"
    "add $128, %rdi\n   sub $128, %rsi\n   jmp 1b\n"
    "7:  cmp $256, %rsi\n   jb 4f\n"
    "vmovdqu64 0(%rdi), %zmm0\n   vmovdqu64 64(%rdi), %zmm1\n"
    "vmovdqu64 128(%rdi), %zmm2\n   vmovdqu64 192(%rdi), %zmm3\n"
    "add $256, %rdi\n   sub $256, %rsi\n   jmp 7b\n"
    "4:  vzeroupper\n"
    "5:  test %rsi, %rsi\n   jz 9f\n"
    "6:  add (%rdi), %rax\n   add $8, %rdi\n   sub $8, %rsi\n   jg 6b\n"
    "9:  \n"
#elif ARM64
    "mov x2, #0\n   cbz x1, 9f\n"
    "1:  cmp x1, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x0]\n   ldp q2, q3, [x0, #32]\n"
    "add x0, x0, #64\n   sub x1, x1, #64\n   b 1b\n"
    "5:  cbz x1, 9f\n"
    "6:  ldr x3, [x0]\n   add x2, x2, x3\n   add x0, x0, #8\n"
    "subs x1, x1, #8\n   b.hi 6b\n"
    "9:  mov x0, x2\n"
#else
    "li a2, 0\n   beqz a1, 9f\n"
    "1:  li a3, 8\n   bltu a1, a3, 9f\n"
    "ld a4, 0(a0)\n   add a2, a2, a4\n   addi a0, a0, 8\n   addi a1, a1, -8\n   j 1b\n"
    "9:  mv a0, a2\n"
#endif
    ASM_RET
    ASM_END(floor_read)
);

positive floor_read(address_any block, positive size);
positive floor_ticks(void);

__asm__(
    ".text\n"
    ASM_FUNC(floor_read_two)
#if X64
    "xor %eax, %eax\n   test %rdx, %rdx\n   jz 9f\n"
    "cmpb $0, cpu_has_avx2(%rip)\n   je 5f\n"
    "cmpb $0, cpu_has_avx512(%rip)\n   jne 7f\n"
    "1:  cmp $128, %rdx\n   jb 4f\n"
    "vmovdqu 0(%rdi), %ymm0\n   vmovdqu 32(%rdi), %ymm1\n"
    "vmovdqu 64(%rdi), %ymm2\n   vmovdqu 96(%rdi), %ymm3\n"
    "vmovdqu 0(%rsi), %ymm4\n   vmovdqu 32(%rsi), %ymm5\n"
    "vmovdqu 64(%rsi), %ymm6\n   vmovdqu 96(%rsi), %ymm7\n"
    "add $128, %rdi\n   add $128, %rsi\n   sub $128, %rdx\n   jmp 1b\n"
    "7:  cmp $256, %rdx\n   jb 4f\n"
    "vmovdqu64 0(%rdi), %zmm0\n   vmovdqu64 64(%rdi), %zmm1\n"
    "vmovdqu64 128(%rdi), %zmm2\n   vmovdqu64 192(%rdi), %zmm3\n"
    "vmovdqu64 0(%rsi), %zmm4\n   vmovdqu64 64(%rsi), %zmm5\n"
    "vmovdqu64 128(%rsi), %zmm6\n   vmovdqu64 192(%rsi), %zmm7\n"
    "add $256, %rdi\n   add $256, %rsi\n   sub $256, %rdx\n   jmp 7b\n"
    "4:  vzeroupper\n"
    "5:  test %rdx, %rdx\n   jz 9f\n"
    "6:  add (%rdi), %rax\n   add (%rsi), %rax\n"
    "add $8, %rdi\n   add $8, %rsi\n   sub $8, %rdx\n"
    "jg 6b\n"
    "9:  \n"
#elif ARM64
    "mov x3, #0\n   cbz x2, 9f\n"
    "1:  cmp x2, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x0]\n   ldp q2, q3, [x0, #32]\n"
    "ldp q4, q5, [x1]\n   ldp q6, q7, [x1, #32]\n"
    "add x0, x0, #64\n   add x1, x1, #64\n"
    "sub x2, x2, #64\n   b 1b\n"
    "5:  cbz x2, 9f\n"
    "6:  ldr x4, [x0], #8\n   ldr x5, [x1], #8\n"
    "add x3, x3, x4\n   add x3, x3, x5\n"
    "subs x2, x2, #8\n   b.hi 6b\n"
    "9:  mov x0, x3\n"
#else
    "li a3, 0\n   beqz a2, 9f\n"
    "1:  li a4, 32\n   bltu a2, a4, 5f\n"
    "ld a4, 0(a0)\n   ld a5, 8(a0)\n   ld a6, 16(a0)\n   ld a7, 24(a0)\n"
    "ld t0, 0(a1)\n   ld t1, 8(a1)\n   ld t2, 16(a1)\n   ld t3, 24(a1)\n"
    "addi a0, a0, 32\n   addi a1, a1, 32\n   addi a2, a2, -32\n   j 1b\n"
    "5:  beqz a2, 9f\n"
    "6:  ld a4, 0(a0)\n   ld a5, 0(a1)\n"
    "add a3, a3, a4\n   add a3, a3, a5\n"
    "addi a0, a0, 8\n   addi a1, a1, 8\n   addi a2, a2, -8\n   bgtz a2, 6b\n"
    "9:  mv a0, a3\n"
#endif
    ASM_RET
    ASM_END(floor_read_two)
    ASM_FUNC(floor_copy)
#if X64
    "xor %eax, %eax\n   test %rdx, %rdx\n   jz 9f\n"
    "cmpb $0, cpu_has_avx2(%rip)\n   je 5f\n"
    "cmpb $0, cpu_has_avx512(%rip)\n   jne 7f\n"
    "1:  cmp $128, %rdx\n   jb 4f\n"
    "vmovdqu 0(%rsi), %ymm0\n   vmovdqu 32(%rsi), %ymm1\n"
    "vmovdqu 64(%rsi), %ymm2\n   vmovdqu 96(%rsi), %ymm3\n"
    "vmovdqu %ymm0, 0(%rdi)\n   vmovdqu %ymm1, 32(%rdi)\n"
    "vmovdqu %ymm2, 64(%rdi)\n   vmovdqu %ymm3, 96(%rdi)\n"
    "add $128, %rdi\n   add $128, %rsi\n   sub $128, %rdx\n   jmp 1b\n"
    "7:  cmp $256, %rdx\n   jb 4f\n"
    "vmovdqu64 0(%rsi), %zmm0\n   vmovdqu64 64(%rsi), %zmm1\n"
    "vmovdqu64 128(%rsi), %zmm2\n   vmovdqu64 192(%rsi), %zmm3\n"
    "vmovdqu64 %zmm0, 0(%rdi)\n   vmovdqu64 %zmm1, 64(%rdi)\n"
    "vmovdqu64 %zmm2, 128(%rdi)\n   vmovdqu64 %zmm3, 192(%rdi)\n"
    "add $256, %rdi\n   add $256, %rsi\n   sub $256, %rdx\n   jmp 7b\n"
    "4:  vzeroupper\n"
    "5:  test %rdx, %rdx\n   jz 9f\n"
    "6:  mov (%rsi), %rax\n   mov %rax, (%rdi)\n"
    "add $8, %rdi\n   add $8, %rsi\n   sub $8, %rdx\n"
    "jg 6b\n"
    "9:  \n"
#elif ARM64
    "cbz x2, 9f\n"
    "1:  cmp x2, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x1]\n   ldp q2, q3, [x1, #32]\n"
    "stp q0, q1, [x0]\n   stp q2, q3, [x0, #32]\n"
    "add x0, x0, #64\n   add x1, x1, #64\n"
    "sub x2, x2, #64\n   b 1b\n"
    "5:  cbz x2, 9f\n"
    "6:  ldr x3, [x1], #8\n   str x3, [x0], #8\n"
    "subs x2, x2, #8\n   b.hi 6b\n"
    "9:  mov x0, #0\n"
#else
    "beqz a2, 9f\n"
    "1:  li a3, 32\n   bltu a2, a3, 5f\n"
    "ld a3, 0(a1)\n   ld a4, 8(a1)\n   ld a5, 16(a1)\n   ld a6, 24(a1)\n"
    "sd a3, 0(a0)\n   sd a4, 8(a0)\n   sd a5, 16(a0)\n   sd a6, 24(a0)\n"
    "addi a0, a0, 32\n   addi a1, a1, 32\n   addi a2, a2, -32\n   j 1b\n"
    "5:  beqz a2, 9f\n"
    "6:  ld a3, 0(a1)\n   sd a3, 0(a0)\n"
    "addi a0, a0, 8\n   addi a1, a1, 8\n   addi a2, a2, -8\n   bgtz a2, 6b\n"
    "9:  li a0, 0\n"
#endif
    ASM_RET
    ASM_END(floor_copy)
    ASM_FUNC(floor_fill)
#if X64
    "xor %eax, %eax\n   test %rsi, %rsi\n   jz 9f\n"
    "cmpb $0, cpu_has_avx2(%rip)\n   je 5f\n"
    "cmpb $0, cpu_has_avx512(%rip)\n   jne 7f\n"
    "vpxor %ymm0, %ymm0, %ymm0\n"
    "1:  cmp $128, %rsi\n   jb 4f\n"
    "vmovdqu %ymm0, 0(%rdi)\n   vmovdqu %ymm0, 32(%rdi)\n"
    "vmovdqu %ymm0, 64(%rdi)\n   vmovdqu %ymm0, 96(%rdi)\n"
    "add $128, %rdi\n   sub $128, %rsi\n   jmp 1b\n"
    "7:  vpxord %zmm0, %zmm0, %zmm0\n"
    "8:  cmp $256, %rsi\n   jb 4f\n"
    "vmovdqu64 %zmm0, 0(%rdi)\n   vmovdqu64 %zmm0, 64(%rdi)\n"
    "vmovdqu64 %zmm0, 128(%rdi)\n   vmovdqu64 %zmm0, 192(%rdi)\n"
    "add $256, %rdi\n   sub $256, %rsi\n   jmp 8b\n"
    "4:  vzeroupper\n"
    "5:  test %rsi, %rsi\n   jz 9f\n"
    "6:  mov %rax, (%rdi)\n   add $8, %rdi\n   sub $8, %rsi\n"
    "jg 6b\n"
    "9:  \n"
#elif ARM64
    "movi v0.16b, #0\n   cbz x1, 9f\n"
    "1:  cmp x1, #64\n   b.lo 5f\n"
    "stp q0, q0, [x0]\n   stp q0, q0, [x0, #32]\n"
    "add x0, x0, #64\n   sub x1, x1, #64\n   b 1b\n"
    "5:  cbz x1, 9f\n"
    "6:  str xzr, [x0], #8\n   subs x1, x1, #8\n   b.hi 6b\n"
    "9:  mov x0, #0\n"
#else
    "beqz a1, 9f\n"
    "1:  li a2, 32\n   bltu a1, a2, 5f\n"
    "sd zero, 0(a0)\n   sd zero, 8(a0)\n"
    "sd zero, 16(a0)\n   sd zero, 24(a0)\n"
    "addi a0, a0, 32\n   addi a1, a1, -32\n   j 1b\n"
    "5:  beqz a1, 9f\n"
    "6:  sd zero, 0(a0)\n   addi a0, a0, 8\n"
    "addi a1, a1, -8\n   bgtz a1, 6b\n"
    "9:  li a0, 0\n"
#endif
    ASM_RET
    ASM_END(floor_fill)
);

positive floor_read_two(address_any one, address_any two, positive size);
positive floor_copy(address_any out, address_any in, positive size);
positive floor_fill(address_any out, positive size);

static positive floor_one(p8 address_to p, positive n)
{
        return floor_read(p, n);
}

static positive floor_two(p8 address_to a, p8 address_to b, positive n)
{
        return floor_read_two(a, b, n);
}

static volatile positive sink_global;

/*
        The bodies add what the routine answered into a variable called sink,
        and each timed run declares its own: a register, not the volatile it
        used to be. A volatile read-modify-write per call is a store and a
        reload that the next call waits on, about five cycles on a 9950X, and
        it sat under every row: a call that returned at once measured 4.0
        ticks and the whole of a sixteen byte copy 5.0, so the small sizes
        could not show a difference smaller than that. The empty asm with a
        memory clobber after each call is what stops the compiler from
        hoisting a call whose arguments do not change out of the loop; it
        costs nothing at run time.
*/
#define TIMED_ONCE(rounds, body)                                              \
        ({                                                                    \
                positive sink = 0;                                            \
                p64 s = floor_ticks();                                        \
                for (b32 k = 0; k < (rounds); k++)                            \
                {                                                             \
                        body;                                                 \
                        __asm__ volatile("" ::: "memory");                    \
                }                                                             \
                p64 e = floor_ticks();                                        \
                sink_global += sink;                                          \
                e - s;                                                        \
        })

/*
        The same, with a size that changes every call. A routine that picks a
        rung by the size is fast at a size that stays put because the branch
        is learned, and a program that copies names and lines does not stay
        put: a copy of one to sixty four bytes at random was 21 ticks against
        4 at one size before the masked window tier. N_ is this call's size,
        lo plus the bits of a generator the predictor cannot follow.
*/
#define MIXED_ONCE(rounds, lo, mask, body)                                    \
        ({                                                                    \
                positive sink = 0;                                            \
                positive x_ = 0x9e3779b97f4a7c15ull;                          \
                p64 s = floor_ticks();                                        \
                for (b32 k = 0; k < (rounds); k++)                            \
                {                                                             \
                        x_ = x_ * 6364136223846793005ull + 1442695040888963407ull; \
                        positive N_ = (lo) + ((x_ >> 40) & (mask));           \
                        body;                                                 \
                        __asm__ volatile("" ::: "memory");                    \
                }                                                             \
                p64 e = floor_ticks();                                        \
                sink_global += sink;                                          \
                e - s;                                                        \
        })

static fn row(string_address name, positive size, positive ratio)
{
        // State the paired median as remaining cost: zero percent over is the
        // lower bound.  If the candidate wins, the baseline was not a floor.
        positive over = ratio > 10000 ? ratio - 10000 : 0;

        string_format(log, "  %s", name);
        for (positive i = string_length(name); i < 24; i++)
                string_format(log, " ");
        string_format(log, "%p", size);
        for (positive i = 0; i < 9; i++) string_format(log, " ");
        if (ratio < 10000)
                string_format(log, "unresolved: candidate beat baseline\n");
        else
                string_format(log, "%p.%p%% slower\n", over / 100,
                              over % 100);
}

#define PAIRED_MIXED(name, lo, mask, floor_body, candidate_body)              \
        do {                                                                  \
                positive ratios[5];                                           \
                for (positive trial = 0; trial < 5; trial++)                  \
                {                                                             \
                        p64 floor_time;                                        \
                        p64 candidate_time;                                    \
                        BENCH_ALTERNATE(trial, floor_time,                    \
                                        MIXED_ONCE(1 << 20, lo, mask, floor_body), \
                                        candidate_time,                       \
                                        MIXED_ONCE(1 << 20, lo, mask, candidate_body)); \
                        ratios[trial] = (positive)(candidate_time * 10000 /   \
                                                   (floor_time ? floor_time : 1)); \
                }                                                             \
                order(ratios, 5);                                             \
                row(name, (lo) + (mask) / 2, ratios[2]);                      \
        } while (0)

#define PAIRED_ROW(name, size, rounds, floor_body, candidate_body)            \
        do {                                                                  \
                positive ratios[5];                                           \
                for (positive trial = 0; trial < 5; trial++)                  \
                {                                                             \
                        p64 floor_time;                                        \
                        p64 candidate_time;                                    \
                        BENCH_ALTERNATE(trial, floor_time,                    \
                                        TIMED_ONCE(rounds, floor_body),       \
                                        candidate_time,                       \
                                        TIMED_ONCE(rounds, candidate_body));  \
                        ratios[trial] = (positive)(candidate_time * 10000 /   \
                                                   (floor_time ? floor_time : 1)); \
                }                                                             \
                order(ratios, 5);                                             \
                row(name, size, ratios[2]);                                   \
        } while (0)

b32 main(void)
{
        for (positive i = 0; i < sizeof(one); i++)
        {
                one[i] = (p8)(i % 251 + 1);
                two[i] = one[i];
        }

        moonwater_cpu_detect();

        /*
                The kernel shape: a kernel build has no vector body, so what
                it runs is the word at a time path under every routine here,
                and the floor for that path is the scalar loop. Built with
                -DFLOOR_NARROW this measures exactly that, natively, which a
                kernel cannot do for itself.
        */
#ifdef FLOOR_NARROW
        cpu_has_avx2 = 0;
        cpu_has_avx512 = 0;
        string_format(log, "  (narrow bodies against the scalar floor: the kernel shape)\n");
#endif

        string_format(log, "  routine                 size     gap to traffic lower bound\n");
        string_format(log, "  -----------------------------------------------------\n");

        for (positive z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++)
        {
                positive n = sizes[z];
                b32 rounds = (b32)((1 << 24) / n) + 1;

                PAIRED_ROW("memory_fill", n, rounds,
                           sink += floor_fill(out, n), memory_fill(out, 7, n));
                PAIRED_ROW("memory_copy", n, rounds,
                           sink += floor_copy(out, one, n),
                           memory_copy(out, one, n));
                PAIRED_ROW("memory_copy_apart", n, rounds,
                           sink += floor_copy(out, one, n),
                           memory_copy_apart(out, one, n));
                PAIRED_ROW("memory_count", n, rounds,
                           sink += floor_one(one, n),
                           sink += memory_count(one, n, 7));
                PAIRED_ROW("memory_first_of", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)memory_first_of(one, 0, n));
                PAIRED_ROW("memory_compare", n, rounds,
                           sink += floor_two(one, two, n),
                           sink += (positive)memory_compare(one, two, n));

                one[n - 1] = 0;
                PAIRED_ROW("string_length", n, rounds,
                           sink += floor_one(one, n),
                           sink += string_length(one));
                PAIRED_ROW("string_first_of", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)string_first_of(one, 0));
                PAIRED_ROW("string_last_of_or_end", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)string_last_of_or_end(one, 3));
                two[n - 1] = 0;
                PAIRED_ROW("string_compare", n, rounds,
                           sink += floor_two(one, two, n),
                           sink += (positive)string_compare(one, two));
                PAIRED_ROW("string_copy", n, rounds,
                           sink += floor_copy(out, one, n),
                           string_copy(out, one));
                PAIRED_ROW("string_copy_max", n, rounds,
                           sink += floor_copy(out, one, n),
                           string_copy_max(out, one, n));
                PAIRED_ROW("string_length_max", n, rounds,
                           sink += floor_one(one, n),
                           sink += string_length_max(one, n));
                PAIRED_ROW("string_compare_max", n, rounds,
                           sink += floor_two(one, two, n),
                           sink += (positive)string_compare_max(one, two, n));
                PAIRED_ROW("memory_common_prefix", n, rounds,
                           sink += floor_two(one, two, n),
                           sink += memory_common_prefix(one, two, n));
                PAIRED_ROW("memory_last_of", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)memory_last_of(one, 0, n));
                PAIRED_ROW("memory_sum_bytes", n, rounds,
                           sink += floor_one(one, n),
                           sink += memory_sum_bytes(one, n));
                PAIRED_ROW("memory_hash_33", n, rounds,
                           sink += floor_one(one, n),
                           sink += memory_hash_33(one, n));
                PAIRED_ROW("hash_xxh64", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)hash_xxh64(one, n, 0));
                memory_copy_apart(out, one, n);
                PAIRED_ROW("memory_reverse", n, rounds,
                           sink += floor_copy(out, one, n),
                           memory_reverse(out, n));
                memory_copy_apart(out, one, n);
                PAIRED_ROW("memory_frob", n, rounds,
                           sink += floor_copy(out, one, n),
                           memory_frob(out, n));
                memory_copy_apart(out, one, n);
                PAIRED_ROW("memory_to_lower_ascii", n, rounds,
                           sink += floor_copy(out, one, n),
                           memory_to_lower_ascii(out, n));
                PAIRED_ROW("memory_exchange_apart", n, rounds,
                           sink += floor_copy(out, one, n),
                           memory_exchange_apart(out, one, n));
                PAIRED_ROW("memory_span_byte", n, rounds,
                           sink += floor_one(one, n),
                           sink += memory_span_byte(one, 1, n));
                one[n] = 0;
                two[0] = 'z';
                two[1] = 0;
                PAIRED_ROW("string_search", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)string_search(one, two));
                PAIRED_ROW("memory_search", n, rounds,
                           sink += floor_one(one, n),
                           sink += (positive)memory_search(one, n, two, 1));
                one[n] = (p8)(n % 251 + 1);
                one[n - 1] = (p8)((n - 1) % 251 + 1);
                two[n - 1] = one[n - 1];
                two[0] = one[0];
                two[1] = one[1];

                string_format(log, "\n");
        }

        /*
                Sizes drawn at random. The size column is the middle of the
                range. The strings come from a pool of 256, one picked at
                random a call, each of a length of its own.
        */
        static const struct { positive lo; positive mask; } spans[] = {
            {1, 15}, {1, 63}, {16, 127}, {64, 511}};
        static p8 pool[256 * 64] __attribute__((aligned(64)));
        static p8 pool_length[256];

        string_format(log, "\n  sizes at random, a new one every call\n");
        for (positive i = 0; i < sizeof(one); i++) { one[i] = (p8)(i % 251 + 1); two[i] = one[i]; }
        for (positive z = 0; z < sizeof(spans) / sizeof(spans[0]); z++)
        {
                positive lo = spans[z].lo;
                positive mask = spans[z].mask;

                PAIRED_MIXED("memory_fill mixed", lo, mask,
                             sink += floor_fill(out, N_), memory_fill(out, 7, N_));
                PAIRED_MIXED("memory_copy mixed", lo, mask,
                             sink += floor_copy(out, one, N_), memory_copy(out, one, N_));
                PAIRED_MIXED("memory_copy_apart mixed", lo, mask,
                             sink += floor_copy(out, one, N_), memory_copy_apart(out, one, N_));
                PAIRED_MIXED("memory_compare mixed", lo, mask,
                             sink += floor_two(one, two, N_),
                             sink += (positive)memory_compare(one, two, N_));
                PAIRED_MIXED("memory_first_of mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += (positive)memory_first_of(one, 0, N_));
                PAIRED_MIXED("memory_last_of mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += (positive)memory_last_of(one, 0, N_));
                PAIRED_MIXED("memory_count mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += memory_count(one, N_, 7));
                PAIRED_MIXED("memory_common_prefix mixed", lo, mask,
                             sink += floor_two(one, two, N_),
                             sink += memory_common_prefix(one, two, N_));
                PAIRED_MIXED("memory_sum_bytes mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += memory_sum_bytes(one, N_));
                PAIRED_MIXED("memory_hash_33 mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += memory_hash_33(one, N_));
                PAIRED_MIXED("hash_xxh64 mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += (positive)hash_xxh64(one, N_, 0));
                PAIRED_MIXED("hash_crc32 mixed", lo, mask,
                             sink += floor_one(one, N_),
                             sink += hash_crc32(0, one, N_));
                PAIRED_MIXED("memory_to_lower_ascii mixed", lo, mask,
                             sink += floor_copy(out, one, N_),
                             memory_to_lower_ascii(out, N_));
                PAIRED_MIXED("memory_into_hex mixed", lo, mask,
                             sink += floor_copy(out, one, N_),
                             sink += memory_into_hex(out, one, N_));
        }
        for (positive z = 0; z < 3; z++)
        {
                positive longest = z == 0 ? 15 : z == 1 ? 31 : 62;
                positive state = 88172645463325252ull + z;
                string_address labels[] = {"1-16", "1-32", "1-62"};

                for (positive i = 0; i < 256; i++)
                {
                        XORSHIFT64(state);
                        positive length = 1 + state % longest;

                        for (positive j = 0; j < length; j++)
                                pool[i * 64 + j] = (p8)((i + j) % 251 + 1);
                        pool[i * 64 + length] = end;
                        pool_length[i] = (p8)length;
                }
                string_format(log, "  strings of %s bytes from a pool of 256\n", labels[z]);
#define POOL_AT(x) (pool + 64 * ((x >> 33) & 255))
#define POOL_LENGTH(x) (pool_length[(x >> 33) & 255] + 1)
                PAIRED_MIXED("string_length mixed", 1, 0,
                             sink += floor_one(POOL_AT(x_), POOL_LENGTH(x_)),
                             sink += string_length(POOL_AT(x_)));
                PAIRED_MIXED("string_copy mixed", 1, 0,
                             sink += floor_copy(out, POOL_AT(x_), POOL_LENGTH(x_)),
                             string_copy(out, POOL_AT(x_)));
                PAIRED_MIXED("string_compare mixed", 1, 0,
                             sink += floor_two(POOL_AT(x_), POOL_AT(x_), POOL_LENGTH(x_)),
                             sink += (positive)string_compare(POOL_AT(x_), POOL_AT(x_)));
                PAIRED_MIXED("string_first_of mixed", 1, 0,
                             sink += floor_one(POOL_AT(x_), POOL_LENGTH(x_)),
                             sink += (positive)string_first_of(POOL_AT(x_), 'z'));
#undef POOL_AT
#undef POOL_LENGTH
        }

        log_flush();
        return 0;
}
#endif /* BENCH_floor */

#ifdef BENCH_baseline
#include "../../src/lib.util.c"

/*
        The assembly against the C byte loop it replaced.

        lib.c's string_length, string_compare and string_first_of forward
        to assembly on x86_64, arm64 and riscv64 and to a byte loop anywhere
        else. This runs both halves side by side at the lengths the choice
        actually turns on, so "a word at a time is faster" is a number rather
        than a belief.

        Foreign targets run under qemu, and a qemu tick is the emulator's work
        and not the machine's. What it measures honestly is how much less work
        there is to do; what it cannot tell you is what a real board would say.

        Built and run by `sh test/run bench baseline`, which is the same
        freestanding link the checks in this file get.
*/

#define SHARED_bench_reference
#include "../checks.c"
#undef SHARED_bench_reference

static positive lengths[] = {4, 8, 16, 32, 64, 256, 4096};

#define LENGTH_COUNT (sizeof(lengths) / sizeof(lengths[0]))
#define ROUNDS 65536

static p8 subject[8192];
static p8 mirror[8192];

// Somewhere the answers have to go that the optimiser cannot argue away.
static volatile positive sink;

fn line(string_address name, positive size, p64 byte_ticks, p64 word_ticks)
{
        string_format(log, "  %s %p bytes: byte %p  word %p\n",
                      name, size, (positive)byte_ticks, (positive)word_ticks);
}

b32 main()
{
        string_format(log, "assembly against the byte loop, %p calls each\n",
                      (positive)ROUNDS);

        for (positive e = 0; e < LENGTH_COUNT; e++)
        {
                positive size = lengths[e];

                for (positive i = 0; i < size; i++)
                        subject[i] = (p8)('a' + i % 26);

                subject[size] = 0;
                memory_copy_apart(mirror, subject, size + 1);

                p64 start = get_cpu_time();

                for (positive r = 0; r < ROUNDS; r++)
                        sink += reference_length(subject);

                p64 middle = get_cpu_time();

                for (positive r = 0; r < ROUNDS; r++)
                        sink += string_length(subject);

                p64 finish = get_cpu_time();

                line((string_address)"string_length   ", size,
                     middle - start, finish - middle);

                start = get_cpu_time();

                for (positive r = 0; r < ROUNDS; r++)
                        sink += (positive)reference_compare(subject, mirror);

                middle = get_cpu_time();

                for (positive r = 0; r < ROUNDS; r++)
                        sink += (positive)string_compare(subject, mirror);

                finish = get_cpu_time();

                line((string_address)"string_compare  ", size,
                     middle - start, finish - middle);

                // A character that is not there, so the whole string is walked
                // rather than however far the first 'q' happens to be.
                start = get_cpu_time();

                for (positive r = 0; r < ROUNDS; r++)
                        sink += (positive)reference_first_of(subject, '#');

                middle = get_cpu_time();

                for (positive r = 0; r < ROUNDS; r++)
                        sink += (positive)string_first_of(subject, '#');

                finish = get_cpu_time();

                line((string_address)"string_first_of ", size,
                     middle - start, finish - middle);
        }

        log_flush();

        return 0;
}
#endif /* BENCH_baseline */

#ifdef BENCH_numbers
/*
        Decimal conversion at the call shapes the library actually exposes.

        The reference is the scalar divide-by-ten/scratch/copy loop that the
        callers used before positive_into folded it.  The assembly column is
        positive_into itself.  positive_to_string is shown separately because
        its writer call is part of that API and therefore not directly
        comparable with a buffer writer, but it is the existing chunked
        implementation against which a new shared digit core must be checked.

        A native run reports hardware ticks.  A qemu run reports emulator
        work: useful as a regression ratio, never as a hardware-floor claim.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define VALUE_COUNT 64
#define ROUNDS (1u << 17)
#define TRIES 7

static p8 output[64];
static volatile positive sink;

NOT_INLINED positive scalar_into(p8 address_to into, positive value)
{
        p8 scratch[24];
        positive have = 0;

        do
        {
                scratch[have++] = (p8)('0' + value % 10);
                value /= 10;
        } while (value);

        for (positive i = 0; i < have; i++)
                into[i] = scratch[have - i - 1];

        return have;
}

NOT_INLINED fn discard_writer(address_any data, positive length)
{
        volatile p8 address_to bytes = (volatile p8 address_to)data;

        sink += length + bytes[0] + bytes[length - 1];
}

static positive small_values[VALUE_COUNT];
static positive middle_values[VALUE_COUNT];
static positive mixed_values[VALUE_COUNT];
static positive wide_values[VALUE_COUNT];

static fn make_values()
{
        positive state = 0x9e3779b97f4a7c15ull;

        for (positive i = 0; i < VALUE_COUNT; i++)
        {
                small_values[i] = i < 32 ? i : (i * 313u) % 10000u;

                positive middle_base = 10000;
                for (positive d = 0; d < i % 3; d++)
                        middle_base *= 10;
                middle_values[i] = middle_base + (i * 7919u) % middle_base;

                positive digits = i % 20 + 1;
                positive base = 1;
                for (positive d = 1; d < digits; d++)
                        base *= 10;
                mixed_values[i] = base + (i * 7919u) % base;

                XORSHIFT64(state);
                wide_values[i] = state | (1ull << 63);
        }

        /* Exact boundaries and both ends keep branch wins honest. */
        small_values[0] = 0;
        small_values[1] = 9;
        small_values[2] = 10;
        small_values[3] = 99;
        small_values[4] = 100;
        small_values[5] = 9999;
        middle_values[0] = 10000;
        middle_values[1] = 99999;
        middle_values[2] = 100000;
        middle_values[3] = 999999;
        middle_values[4] = 1000000;
        middle_values[5] = 9999999;
        mixed_values[0] = 99999999ull;
        mixed_values[1] = 100000000ull;
        mixed_values[2] = 9999999999999999ull;
        mixed_values[3] = 10000000000000000ull;
        wide_values[0] = positive_max;
        wide_values[1] = 10000000000000000000ull;
}

BENCH_BEST_ONCE(scalar_run, (positive address_to values), TRIES, ROUNDS, r,
                sink += scalar_into(output, values[r & (VALUE_COUNT - 1)]))

BENCH_BEST_ONCE(assembly_run, (positive address_to values), TRIES, ROUNDS, r,
                sink += positive_into(output,
                                      values[r & (VALUE_COUNT - 1)]))

BENCH_BEST_ONCE(writer_run, (positive address_to values), TRIES, ROUNDS, r,
                positive_to_string(discard_writer,
                                   values[r & (VALUE_COUNT - 1)]))

static fn row(string_address name, positive address_to values)
{
        p64 scalar = scalar_run(values);
        p64 assembly = assembly_run(values);
        p64 writer = writer_run(values);

        string_format(log, "  %s: scalar %p  into %p  chunked-writer %p"
                           "  into/scalar %p%%\n",
                      name, (positive)scalar, (positive)assembly,
                      (positive)writer,
                      (positive)(assembly * 100 / (scalar ? scalar : 1)));
}

b32 main()
{
        make_values();
        moonwater_cpu_detect();

        string_format(log, "decimal conversion, best of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        row((string_address)"small 1-4 digit", small_values);
        row((string_address)"middle 5-7 digit", middle_values);
        row((string_address)"mixed 1-20 digit", mixed_values);
        row((string_address)"wide 19-20 digit", wide_values);
        log_flush();
        return 0;
}
#endif /* BENCH_numbers */

#ifdef BENCH_bases
/*
        Buffer conversion in non-decimal bases.

        The reference is printf_render's former C body: a runtime-base divide
        loop into a scratch buffer, followed by a backwards copy. The assembly
        is positive_into_base. Decimal has its own deeper benchmark in
        bench_numbers.c; this one measures every power-of-two lane from two
        through thirty two and the generic base-36 lane.

        Native runs are hardware ticks. Qemu runs are emulator work and only
        useful as regression ratios, never as hardware-floor measurements.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define VALUE_COUNT 64
#define ROUNDS (1u << 17)
#define TRIES 7

static p8 output[64];
static positive values[VALUE_COUNT];
static volatile positive sink;

NOT_INLINED positive scalar_base(p8 address_to into, positive value,
                                 positive base, p8 upper)
{
        p8 scratch[64];
        positive have = 0;

        do
        {
                positive digit = value % base;

                scratch[have++] =
                    (p8)(digit < 10 ? '0' + digit
                                    : (upper ? 'A' : 'a') + digit - 10);
                value /= base;
        } while (value);

        for (positive i = 0; i < have; i++)
                into[i] = scratch[have - i - 1];

        return have;
}

static fn make_values()
{
        positive state = 0x9e3779b97f4a7c15ull;

        for (positive i = 0; i < VALUE_COUNT; i++)
        {
                XORSHIFT64(state);
                values[i] = state;
        }

        values[0] = 0;
        values[1] = 7;
        values[2] = 8;
        values[3] = 15;
        values[4] = 16;
        values[5] = 07777;
        values[6] = ~(positive)0;
}

BENCH_BEST_ONCE(scalar_run, (positive base, p8 upper), TRIES, ROUNDS, r,
                sink += scalar_base(output,
                                    values[r & (VALUE_COUNT - 1)],
                                    base, upper))

BENCH_BEST_ONCE(assembly_run, (positive base, p8 upper), TRIES, ROUNDS, r,
                sink += positive_into_base(
                    output, values[r & (VALUE_COUNT - 1)], base, upper))

static fn row(string_address name, positive base, p8 upper)
{
        p64 scalar = scalar_run(base, upper);
        p64 assembly = assembly_run(base, upper);

        string_format(log, "  %s: scalar %p  assembly %p  assembly/scalar %p%%\n",
                      name, (positive)scalar, (positive)assembly,
                      (positive)(assembly * 100 / (scalar ? scalar : 1)));
}

b32 main()
{
        make_values();
        moonwater_cpu_detect();

        string_format(log, "base conversion, best of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        row((string_address)"binary", 2, false);
        row((string_address)"base 4", 4, false);
        row((string_address)"octal", 8, false);
        row((string_address)"hexadecimal lower", 16, false);
        row((string_address)"hexadecimal upper", 16, true);
        row((string_address)"base 32 lower", 32, false);
        row((string_address)"base 32 upper", 32, true);
        row((string_address)"base 36 lower", 36, false);
        log_flush();
        return 0;
}
#endif /* BENCH_bases */

#ifdef BENCH_hex
/* Bulk hex output: the former checksum loop versus the shared ASM encoder. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 24)

static p8 input[MAXIMUM];
static p8 former_output[MAXIMUM * 2];
static p8 assembly_output[MAXIMUM * 2];
static volatile positive sink;

__attribute__((noinline, noclone)) static positive former_hex(
    p8 address_to destination, p8 address_to source, positive size)
{
        static const p8 digits[] = "0123456789abcdef";
        for (positive at = 0; at < size; at++)
        {
                destination[at * 2] = digits[source[at] >> 4];
                destination[at * 2 + 1] = digits[source[at] & 15];
        }
        return size * 2;
}

static p64 run(p8 assembly, positive size, positive rounds)
{
        p64 start = get_cpu_time();
        for (positive at = 0; at < rounds; at++)
                sink += assembly ? memory_into_hex(assembly_output, input, size) :
                                   former_hex(former_output, input, size);
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive sizes[] = {1, 8, 15, 16, 17, 32, 64, 4096, MAXIMUM};
        for (positive at = 0; at < MAXIMUM; at++)
                input[at] = (p8)(at * 197 + (at >> 8));
        for (positive row = 0; row < sizeof(sizes) / sizeof(sizes[0]); row++)
        {
                positive size = sizes[row];
                positive ratios[TRIES];
                positive rounds = TARGET_BYTES / size;
                if (rounds > (1u << 20))
                        rounds = 1u << 20;
                if (former_hex(former_output, input, size) !=
                    memory_into_hex(assembly_output, input, size) ||
                    memory_compare(former_output, assembly_output, size * 2))
                        return 1;
                BENCH_PAIRED_RUNS(ratios, former, assembly, size, rounds);
                string_format(log, "memory_into_hex %p bytes: paired median ASM/C %p.%p%%\n",
                              size, ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_hex */
#ifdef BENCH_hex_decode
/* Hexadecimal decode at digest lengths: the former C digit loop versus the
   shared memory_from_hex_exact body, over valid digits, which is what a check
   file holds. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define HEX_CASES 4096

static char text[HEX_CASES][129];
static p8 former_out[HEX_CASES][64];
static p8 assembly_out[HEX_CASES][64];
static volatile positive sink;

__attribute__((noinline, noclone)) static p8 former_decode(
    address_any into, string_address text, positive bytes)
{
        p8 *out = (p8 *)into;
        for (positive at = 0; at < bytes; at++)
        {
                positive high = digit_known(string_get(text + at * 2), 16);
                positive low;

                if (high >= 16)
                        return false;
                low = digit_known(string_get(text + at * 2 + 1), 16);
                if (low >= 16)
                        return false;
                out[at] = (p8)(high << 4 | low);
        }
        return true;
}

static p64 run(p8 assembly, positive bytes, positive rounds)
{
        p64 start = get_cpu_time();
        for (positive r = 0; r < rounds; r++)
                for (positive c = 0; c < HEX_CASES; c++)
                        sink += assembly ?
                                memory_from_hex(assembly_out[c], (string_address)text[c], bytes) :
                                former_decode(former_out[c], (string_address)text[c], bytes);
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive sizes[] = {16, 20, 32, 64};
        static const char digits[] = "0123456789abcdefABCDEF";
        p64 seed = 0x9e3779b97f4a7c15ULL;

        for (positive c = 0; c < HEX_CASES; c++)
                for (positive i = 0; i < 128; i++)
                {
                        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
                        text[c][i] = digits[(seed >> 33) % 22];
                }
        for (positive row = 0; row < sizeof(sizes) / sizeof(sizes[0]); row++)
        {
                positive bytes = sizes[row];
                positive ratios[TRIES];
                positive rounds = 2000;

                BENCH_PAIRED_RUNS(ratios, former, assembly, bytes, rounds);
                string_format(log, "memory_from_hex %p bytes: paired median C/ASM %p.%p\n",
                              bytes, ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_hex_decode */

#ifdef BENCH_translate_rect
/* memory_translate_u32_rect against the loop it replaced in src/canvas/font.c,
   at the shapes a terminal cell has at 1080p (9 by 19), 4K (16 by 35) and
   8K (31 by 70), and a strip of 256 by 64; on x86_64 for both bodies. The
   cells come out of a 4096-byte-wide atlas into a 3840-pixel-wide frame, as
   the engine's do. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

static p32 table[512];
static p8 source[4096 * 72];
static p32 target[3840 * 72];

__attribute__((noinline, noclone)) static fn former_translate(
    p32 address_to into, positive into_stride, const p8 address_to from,
    positive from_stride, positive width, positive height, const p32 address_to tones)
{
        for (positive r = 0; r < height; r++, into += into_stride, from += from_stride)
                for (positive i = 0; i < width; i++)
                        into[i] = tones[from[i]];
}

//      Sixty four cells side by side a turn, as a terminal row draws them.
static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
                for (positive cell = 0; cell < 64; cell++)
                {
                        p32 address_to into = target + cell * width % (3840 - width);
                        const p8 address_to from = source + cell * 61 % (4096 - width);

                        if (assembly)
                                memory_translate_u32_rect(into, 3840, from, 4096, width, height, table);
                        else
                                former_translate(into, 3840, from, 4096, width, height, table);
                }
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[][2] = {{9, 19}, {16, 35}, {31, 70}, {256, 64}};
        p64 seed = 0x9e3779b97f4a7c15ULL;

        for (positive i = 0; i < sizeof(source); i++)
        {
                XORSHIFT64(seed);
                source[i] = (p8)seed;
        }
        for (p32 i = 0; i < 256; i++)
        {
                table[i] = 0x101820 + i * 0x010101;
                for (p32 k = 0; k < 4; k++)
                        ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
        }
#if X64
        p8 planes = cpu_has_avx512_vbmi;
        for (p32 tier = 0; tier < (planes ? 2u : 1u); tier++)
        {
                cpu_has_avx512_vbmi = tier ? 0 : planes;
#endif
                for (positive row = 0; row < sizeof(shapes) / sizeof(shapes[0]); row++)
                {
                        positive width = shapes[row][0], height = shapes[row][1];
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (width * height * 64) + 1;

                        static p32 former_out[3840 * 72];

                        memory_fill_u32(target, sizeof(target) / 4, 0);
                        memory_fill_u32(former_out, sizeof(former_out) / 4, 0);
                        memory_translate_u32_rect(target + 7, 3840, source + 5, 4096, width, height, table);
                        former_translate(former_out + 7, 3840, source + 5, 4096, width, height, table);
                        if (memory_compare(target, former_out, sizeof(target)))
                                return 1;
                        BENCH_PAIRED_RUNS(ratios, former, assembly, width, height, rounds);
                        string_format(log, "memory_translate_u32_rect %p by %p%s: paired median ASM/C %p.",
                                      width, height,
#if X64
                                      cpu_has_avx512_vbmi ? (string_address) " planes" : (string_address) " narrow",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx512_vbmi = planes;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_translate_rect */

#ifdef BENCH_average_ahead
/* memory_average_ahead against the byte loop it is, over the compositor's
   glass: 640 pixels by 40 rows of four bytes, in cache, a pass across at a
   pixel and its doubles and down at a row and its doubles; on x86_64 the
   three bodies. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define GLASS (640 * 4 * 40)

static p8 glass[GLASS + 4 * 2560], glass_c[GLASS + 4 * 2560];

__attribute__((noinline, noclone)) static fn former_average(p8 address_to data, positive count, positive distance)
{
        for (positive i = 0; i < count; i++)
                data[i] = (p8)((data[i] + data[i + distance] + 1) >> 1);
}

static p64 run(p8 assembly, positive distance, positive rounds)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
                if (assembly)
                        memory_average_ahead(glass, GLASS, distance);
                else
                        former_average(glass_c, GLASS, distance);
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive distances[] = {4, 8, 16, 32, 2560, 5120, 10240};
        p64 seed = 0x243f6a8885a308d3ULL;

        for (positive i = 0; i < sizeof(glass); i++)
        {
                XORSHIFT64(seed);
                glass[i] = glass_c[i] = (p8)(seed >> 32);
        }
#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
                if ((tier == 0 && !wide512) || (tier == 1 && !wide256))
                        continue;
#endif
                for (positive row = 0; row < sizeof(distances) / sizeof(distances[0]); row++)
                {
                        positive ratios[TRIES];

                        memory_average_ahead(glass, GLASS, distances[row]);
                        former_average(glass_c, GLASS, distances[row]);
                        if (memory_compare(glass, glass_c, sizeof(glass)))
                                return 1;
                        BENCH_PAIRED_RUNS(ratios, former, assembly, distances[row], 200);
                        string_format(log, "memory_average_ahead %p bytes ahead%s: paired median ASM/C %p.", distances[row],
#if X64
                                      cpu_has_avx512 ? (string_address) " avx512" : cpu_has_avx2 ? (string_address) " avx2"
                                                                                                    : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_average_ahead */

#ifdef BENCH_glass
/* The glass's three routines against the byte loops they are, over a glass
   window of 640 by 400 (in cache): a level halved, the small picture
   doubled, and the picture laid over its ground with its grain; on x86_64
   with AVX2 and without. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define GW 640
#define GH 400

static p32 scene[(GW + 2) * (GH + 2)], into_asm[GW * GH], into_c[GW * GH], picture[GW * GH], grain[128];

static p32 bench_mean(p32 a, p32 b)
{
        p32 out = 0;

        for (p32 shift = 0; shift < 32; shift += 8)
                out |= (((a >> shift & 255) + (b >> shift & 255) + 1) >> 1) << shift;
        return out;
}

static p32 bench_low(p32 a, p32 b)
{
        p32 out = 0;

        for (p32 shift = 0; shift < 32; shift += 8)
                out |= (((a >> shift & 255) + (b >> shift & 255)) >> 1) << shift;
        return out;
}

__attribute__((noinline, noclone)) static fn former_halve(p32 address_to into)
{
        for (positive y = 0; y < GH / 2; y++)
                for (positive x = 0; x < GW / 2; x++)
                {
                        const p32 address_to at = scene + 2 * y * GW + 2 * x;

                        into[y * (GW / 2) + x] = bench_low(bench_mean(at[0], at[GW]), bench_mean(at[1], at[GW + 1]));
                }
}

__attribute__((noinline, noclone)) static fn former_double(p32 address_to into)
{
        positive pitch = GW / 2 + 2;
        const p32 address_to from = scene + pitch + 1;

        for (positive y = 0; y < GH / 2; y++)
                for (positive side = 0; side < 2; side++)
                {
                        const p32 address_to c = from + y * pitch, address_to n = side ? c + pitch : c - pitch;
                        p32 address_to out = into + (2 * y + side) * GW;

                        for (positive x = 0; x < GW / 2; x++)
                        {
                                p32 v = bench_low(c[x], bench_mean(c[x], n[x]));
                                p32 l = bench_low(c[x - 1], bench_mean(c[x - 1], n[x - 1]));
                                p32 r = bench_low(c[x + 1], bench_mean(c[x + 1], n[x + 1]));

                                out[2 * x] = bench_low(v, bench_mean(v, l));
                                out[2 * x + 1] = bench_low(v, bench_mean(v, r));
                        }
                }
}

__attribute__((noinline, noclone)) static fn former_over(p32 address_to into)
{
        for (positive i = 0; i < GW * GH; i++)
        {
                p32 p = picture[i], rest = 255 - (p >> 24), out = 0;

                for (p32 shift = 0; shift < 32; shift += 8)
                {
                        b32 c = (b32)(scene[i] >> shift & 255) + (b32)(grain[i & 15] >> shift & 255);

                        c = c > 255 ? 255 : c;
                        c -= (b32)(grain[64 + (i & 15)] >> shift & 255);
                        c = c < 0 ? 0 : c;
                        p32 t = (p32)c * rest + 128, v = (p >> shift & 255) + ((t + (t >> 8)) >> 8);

                        out |= (v > 255 ? 255 : v) << shift;
                }
                into[i] = out;
        }
}

static p64 run(p8 assembly, positive which, positive rounds)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
                if (which == 0)
                        assembly ? memory_halve_u32(into_asm, GW / 2, scene, GW, GW / 2, GH / 2) : former_halve(into_c);
                else if (which == 1)
                        assembly ? memory_double_u32(into_asm, GW, scene + GW / 2 + 3, GW / 2 + 2, GW / 2, GH / 2)
                                 : former_double(into_c);
                else
                        assembly ? memory_glass_over_u32(into_asm, picture, scene, GW * GH, grain) : former_over(into_c);
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const char address_to names[3] = {"memory_halve_u32", "memory_double_u32", "memory_glass_over_u32"};
        p64 seed = 0x13198a2e03707344ULL;

        for (positive i = 0; i < sizeof(scene) / 4; i++)
        {
                XORSHIFT64(seed);
                scene[i] = (p32)(seed >> 32);
        }
        for (positive i = 0; i < GW * GH; i++)
        {
                XORSHIFT64(seed);
                picture[i] = (p32)(seed >> 40) & 0x3f3f3f3fu;
        }
        for (positive i = 0; i < 128; i++)
                grain[i] = (p32)(i * 0x01010101u) & 0x03030303u;
#if X64
        p8 wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier ? 0 : wide256;
                if (tier == 0 && !wide256)
                        continue;
#endif
                for (positive which = 0; which < 3; which++)
                {
                        positive ratios[TRIES];

                        run(true, which, 1);
                        run(false, which, 1);
                        if (memory_compare(into_asm, into_c, which ? sizeof(into_c) : sizeof(into_c) / 4))
                                return 1;
                        BENCH_PAIRED_RUNS(ratios, former, assembly, which, 20);
                        string_format(log, "%s%s: paired median ASM/C %p.", (string_address)names[which],
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx2 = wide256;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_glass */

#ifdef BENCH_matrix
/* gemm_f32 at the trainer's shapes against the compiler: GEMM written as C
   for the vectoriser -- rows four at a time, then k, the j loop vectorised --
   and built -O3 for the tier being timed (x86-64-v4 with 512-bit vectors for
   the AVX-512 body, x86-64-v3 for AVX2 + FMA, the baseline for SSE2; riscv64's
   with V for the V body). Paired medians of ASM/C, each side's best ticks a
   call, and the assembly's flop a tick. Natively the shapes are the training
   calls themselves (8192 rows); emulated, their M is cut to 64, which is a
   ratio worth reading and an absolute that is not. The OpenBLAS bar, which
   this freestanding build cannot link, is in lib.c's notes on gemm_f32.

   One shape on one tier alone, for perf stat:

       bench.matrix.x86_64 SHAPE-TIER     SHAPE 0..17 as the table below,
                                          TIER 512, 256 or 128 (128: SSE2;
                                          one tier only off x86_64)
       bench.matrix.x86_64 nothing        the same program doing none */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#if X64
#define MATRIX_C_WIDE __attribute__((optimize("O3"), target("arch=x86-64-v4,prefer-vector-width=512")))
#define MATRIX_C_YMM __attribute__((optimize("O3"), target("arch=x86-64-v3")))
#elif RISCV64
#define MATRIX_C_WIDE __attribute__((optimize("O3"), target("arch=+v")))
#define MATRIX_C_YMM __attribute__((optimize("O3")))
#else
#define MATRIX_C_WIDE __attribute__((optimize("O3")))
#define MATRIX_C_YMM __attribute__((optimize("O3")))
#endif
#define MATRIX_C_BASE __attribute__((optimize("O3")))

//      C = beta C + A Bk, Bk K x N: four rows of C at a time against a row
//      of Bk, which the vectoriser turns into vector FMAs along j.
#define MATRIX_C_GEMM(name, attributes)                                                                  \
        attributes static fn name(f32 *restrict C, const f32 *restrict A, const f32 *restrict Bk,        \
                                  positive M, positive N, positive K, f32 beta)                          \
        {                                                                                                \
                for (positive i = 0; i < M; i += 4)                                                      \
                {                                                                                        \
                        positive rows = M - i < 4 ? M - i : 4;                                           \
                                                                                                         \
                        for (positive r = 0; r < rows; r++)                                              \
                                for (positive j = 0; j < N; j++)                                         \
                                        C[(i + r) * N + j] = beta == 0 ? 0 : beta * C[(i + r) * N + j];  \
                        if (rows < 4)                                                                    \
                        {                                                                                \
                                for (positive r = 0; r < rows; r++)                                      \
                                        for (positive k = 0; k < K; k++)                                 \
                                        {                                                                \
                                                f32 a = A[(i + r) * K + k];                              \
                                                                                                         \
                                                for (positive j = 0; j < N; j++)                         \
                                                        C[(i + r) * N + j] += a * Bk[k * N + j];         \
                                        }                                                                \
                                continue;                                                                \
                        }                                                                                \
                        f32 *restrict c0 = C + i * N, *restrict c1 = c0 + N, *restrict c2 = c1 + N;      \
                        f32 *restrict c3 = c2 + N;                                                       \
                                                                                                         \
                        for (positive k = 0; k < K; k++)                                                 \
                        {                                                                                \
                                f32 a0 = A[i * K + k], a1 = A[(i + 1) * K + k], a2 = A[(i + 2) * K + k]; \
                                f32 a3 = A[(i + 3) * K + k];                                             \
                                const f32 *restrict b = Bk + k * N;                                      \
                                                                                                         \
                                for (positive j = 0; j < N; j++)                                         \
                                {                                                                        \
                                        c0[j] += a0 * b[j];                                              \
                                        c1[j] += a1 * b[j];                                              \
                                        c2[j] += a2 * b[j];                                              \
                                        c3[j] += a3 * b[j];                                              \
                                }                                                                        \
                        }                                                                                \
                }                                                                                        \
        }
MATRIX_C_GEMM(matrix_c_wide, MATRIX_C_WIDE)
MATRIX_C_GEMM(matrix_c_ymm, MATRIX_C_YMM)
MATRIX_C_GEMM(matrix_c_base, MATRIX_C_BASE)

typedef fn (*matrix_c_call)(f32 *restrict, const f32 *restrict, const f32 *restrict, positive, positive, positive,
                            f32);

static const struct
{
        string_address what;
        positive M, N, K;
        p32 trans;
} matrix_shapes[] = {
        {"qkv forward", 8192, 768, 256, 1},       {"attention out", 8192, 256, 256, 1},
        {"MLP gate+up", 8192, 1364, 256, 1},      {"MLP gate+up", 8192, 682, 256, 1},
        {"MLP down", 8192, 256, 682, 1},          {"dX from qkv", 8192, 256, 768, 0},
        {"dX from MLP", 8192, 256, 1364, 0},      {"dX from down", 8192, 682, 256, 0},
        {"dX from attention", 8192, 256, 256, 0}, {"dW qkv", 768, 256, 8192, 0},
        {"dW up", 1364, 256, 8192, 0},            {"dW down", 256, 682, 8192, 0},
        {"dW attention", 256, 256, 8192, 0},      {"attention Q K^T", 256, 256, 32, 1},
        {"attention P V", 256, 32, 256, 0},       {"Muon 256x256x682", 256, 256, 682, 1},
        {"Muon 256x682x256", 256, 682, 256, 0},   {"Muon 256x256x256", 256, 256, 256, 0},
};
#define MATRIX_SHAPES (sizeof(matrix_shapes) / sizeof(matrix_shapes[0]))

static f32 *matrix_a, *matrix_b, *matrix_bk, *matrix_c;
static matrix_c_call matrix_former;

//      M and K as timed: the training call natively; emulated, at most 64
//      rows and 1024 deep
static positive matrix_rows(positive s)
{
#if X64
        return matrix_shapes[s].M;
#else
        return matrix_shapes[s].M > 64 ? 64 : matrix_shapes[s].M;
#endif
}

static positive matrix_depth(positive s)
{
#if X64
        return matrix_shapes[s].K;
#else
        return matrix_shapes[s].K > 1024 ? 1024 : matrix_shapes[s].K;
#endif
}

static fn matrix_prepare(positive s)
{
        positive N = matrix_shapes[s].N, K = matrix_depth(s);

        //      the C side reads op(B) as K x N; for trans_b that is B laid out
        //      afresh, outside the clock
        for (positive k = 0; k < K; k++)
                for (positive j = 0; j < N; j++)
                        matrix_bk[k * N + j] = matrix_shapes[s].trans ? matrix_b[j * K + k] : matrix_b[k * N + j];
}

static p64 run(p8 assembly, positive s, positive reps)
{
        positive M = matrix_rows(s), N = matrix_shapes[s].N, K = matrix_depth(s);
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                if (assembly)
                        gemm_f32(matrix_c, matrix_a, matrix_b, M, N, K, matrix_shapes[s].trans, 0.0f);
                else
                        matrix_former(matrix_c, matrix_a, matrix_bk, M, N, K, 0.0f);
        return get_cpu_time() - start;
}

static fn matrix_hundredths(positive value)
{
        string_format(log, "%p.", value / 100);
        positive_to_padded(log, value % 100, 2, '0', 0);
}

static fn matrix_tier(string_address tier)
{
        for (positive s = 0; s < MATRIX_SHAPES; s++)
        {
                positive M = matrix_rows(s), N = matrix_shapes[s].N, K = matrix_depth(s);
                positive flops = 2 * M * N * K, tries = flops > 1000000000 ? 3 : 7;
                positive reps = flops > 100000000 ? 1 : 100000000 / (flops ? flops : 1), ratios[7];
                p64 best_c = ~(p64)0, best_a = ~(p64)0;

                matrix_prepare(s);
                for (positive trial = 0; trial < tries; trial++)
                {
                        p64 former, assembly;

                        BENCH_ALTERNATE(trial, former, run(false, s, reps), assembly, run(true, s, reps));
                        ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                        best_c = former < best_c ? former : best_c;
                        best_a = assembly < best_a ? assembly : best_a;
                }
                order(ratios, tries);
                string_format(log, "gemm_f32 %s %p x %p x %p%s, %s: paired median ASM/C ", matrix_shapes[s].what, M, N, K,
                              matrix_shapes[s].trans ? (string_address) " (B^T)" : (string_address) "", tier);
                matrix_hundredths(ratios[tries / 2]);
                string_format(log, "%%; best ticks a call ASM %p, C %p; ASM flop a tick ", best_a / reps, best_c / reps);
                matrix_hundredths((positive)((p64)flops * reps * 100 / (best_a ? best_a : 1)));
                log("\n", 1);
        }
}

//      SHAPE-TIER alone, for perf stat
static positive matrix_named_shape;

static fn matrix_named_run(void)
{
        run(true, matrix_named_shape, 1);
}

static fn matrix_named_nothing(void)
{
}

static fn matrix_force(positive tier)
{
#if X64
        cpu_has_avx512 = tier == 512 ? cpu_has_avx512 : 0;
        cpu_has_avx2 = tier >= 256 ? cpu_has_avx2 : 0;
        cpu_has_fma = tier >= 256 ? cpu_has_fma : 0;
#else
        (void)tier;
#endif
}

static bench_work matrix_named(string_address name)
{
        BENCH_NAME(name, "nothing", matrix_named_nothing);
        positive shape = 0, tier = 0, at = 0;

        for (; name[at] >= '0' && name[at] <= '9'; at++)
                shape = shape * 10 + (positive)(name[at] - '0');
        if (!at || name[at] != '-' || shape >= MATRIX_SHAPES)
                return null;
        for (at++; name[at] >= '0' && name[at] <= '9'; at++)
                tier = tier * 10 + (positive)(name[at] - '0');
        if (name[at] || (tier != 512 && tier != 256 && tier != 128))
                return null;
        matrix_named_shape = shape;
        matrix_force(tier);
        return matrix_named_run;
}

b32 main(void)
{
        positive floats = 8192 * 1364;

        matrix_a = memory(floats * 4), matrix_b = memory(floats * 4), matrix_bk = memory(floats * 4);
        matrix_c = memory(floats * 4);
        if (!matrix_a || !matrix_b || !matrix_bk || !matrix_c)
                return 1;
        p64 state = 0x9e3779b97f4a7c15ull;
        for (positive i = 0; i < floats; i++)
        {
                XORSHIFT64(state);
                matrix_a[i] = (f32)((b64)(state >> 40) - (1 << 23)) * (1.0f / (f32)(1 << 23));
                matrix_b[i] = (f32)((b64)(state & 0xffffff) - (1 << 23)) * (1.0f / (f32)(1 << 23));
                matrix_c[i] = 0.0f;
        }
        BENCH_RUN_NAMED(matrix_named);
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;

        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                cpu_has_fma = tier < 2 ? fused : 0;
                if ((tier == 0 && !wide512) || (tier == 1 && !(wide2 && fused)))
                        continue;
                matrix_former = tier == 0 ? matrix_c_wide : tier == 1 ? matrix_c_ymm : matrix_c_base;
                matrix_tier(tier == 0 ? "avx512" : tier == 1 ? "avx2 fma" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2, cpu_has_fma = fused;
#elif RISCV64
        cpu_has_vector = 0;
        gemm_f32(matrix_c, matrix_a, matrix_b, 1, 1, 1, 0, 0.0f);
        p8 vector = cpu_has_vector;

        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
                matrix_former = tier == 0 ? matrix_c_wide : matrix_c_base;
                matrix_tier(tier == 0 ? "V" : "no V");
        }
        cpu_has_vector = vector;
#else
        matrix_former = matrix_c_base;
        matrix_tier("neon");
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_matrix */

#ifdef BENCH_blend_rect
/* memory_blend_u32_rect against the loop it replaced in font_blend: glyphs
   of UI text at 13, 24 and 48 px (8 by 13, 14 by 24, 28 by 48) and a 4K
   heading (120 by 200), each row two strokes of ink in air, laid on a
   ground in a 3840-pixel frame that stays in cache; on x86_64 both bodies.
   The rows overlap in the glyph array, so each shape has its own strokes. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

static p32 table[513];
static p8 glyph[256 * 200];
static p32 frame[3840 * 220], frame_c[3840 * 220];

__attribute__((noinline, noclone)) static positive former_blend(
    p32 address_to into, positive stride, const p8 address_to from, positive from_stride,
    positive width, positive height, const p32 address_to tones)
{
        for (positive r = 0; r < height; r++)
                for (positive c = 0; c < width; c++)
                {
                        p32 a = from[r * from_stride + c];

                        if (a)
                        {
                                if ((into[r * stride + c] & 0xffffff) != tones[512])
                                        return r * width + c;
                                into[r * stride + c] = tones[a];
                        }
                }
        return width * height;
}

//      Sixteen glyphs along a line, each laid on ground the turn before it.
static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        p64 took = 0;

        for (positive round = 0; round < rounds; round++)
        {
                for (positive r = 0; r < height; r++)
                        memory_fill_u32(frame + r * 3840, 16 * width, 0x203040);
                p64 start = get_cpu_time();
                for (positive g = 0; g < 16; g++)
                        if (assembly)
                                memory_blend_u32_rect(frame + g * width, 3840, glyph, 256, width, height, table);
                        else
                                former_blend(frame + g * width, 3840, glyph, 256, width, height, table);
                took += get_cpu_time() - start;
        }
        return took;
}

b32 main(void)
{
        static const positive shapes[][2] = {{8, 13}, {14, 24}, {28, 48}, {120, 200}};
        p64 seed = 0x9e3779b97f4a7c15ULL;

        //      Each row two strokes, a fifth of the row wide each, at random
        //      places: runs of ink and air as a glyph's rows have them.
        for (positive r = 0; r < 200; r++)
                for (positive k = 0; k < 2; k++)
                {
                        XORSHIFT64(seed);
                        for (positive w = 0; w < 4; w++)
                        {
                                positive width = w == 0 ? 8 : w == 1 ? 14 : w == 2 ? 28 : 120;
                                positive at = (positive)(seed >> (8 * w)) % width, run = width / 5 + 1;

                                for (positive c = at; c < at + run && c < width; c++)
                                        glyph[r * 256 + c] = (p8)(seed >> 40 | 1);
                        }
                }
        for (p32 i = 0; i < 256; i++)
        {
                table[i] = 0x203040 + i * 0x010101;
                for (p32 k = 0; k < 4; k++)
                        ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
        }
        table[512] = 0x203040;
#if X64
        p8 planes = cpu_has_avx512_vbmi;
        for (p32 tier = 0; tier < (planes ? 2u : 1u); tier++)
        {
                cpu_has_avx512_vbmi = tier ? 0 : planes;
#endif
                for (positive row = 0; row < sizeof(shapes) / sizeof(shapes[0]); row++)
                {
                        positive width = shapes[row][0], height = shapes[row][1];
                        positive ratios[TRIES];
                        positive rounds = 400000 / (width * height * 16) + 1;

                        memory_fill_u32(frame, sizeof(frame) / 4, 0x203040);
                        memory_fill_u32(frame_c, sizeof(frame_c) / 4, 0x203040);
                        if (memory_blend_u32_rect(frame, 3840, glyph, 256, width, height, table) !=
                                former_blend(frame_c, 3840, glyph, 256, width, height, table) ||
                            memory_compare(frame, frame_c, sizeof(frame)))
                                return 1;
                        /* memory_blend_u32_rect_baseline: the baseline body writes the fast path's words on these glyphs */
                        memory_fill_u32(frame_c, sizeof(frame_c) / 4, 0x203040);
                        memory_blend_u32_rect_baseline(frame_c, 3840, glyph, 256, width, height, table);
                        if (memory_compare(frame, frame_c, sizeof(frame)))
                                return 1;
                        BENCH_PAIRED_RUNS(ratios, former, assembly, width, height, rounds);
                        string_format(log, "memory_blend_u32_rect %p by %p%s: paired median ASM/C %p.", width, height,
#if X64
                                      cpu_has_avx512_vbmi ? (string_address) " planes" : (string_address) " narrow",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx512_vbmi = planes;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_blend_rect */

#ifdef BENCH_mix_rect
/* memory_mix_u32_rect against the loop it replaced in font_blend on an
   unknown ground: glyphs of UI text at 13, 24 and 48 px (8 by 13, 14 by 24,
   28 by 48) and a 4K heading (120 by 200), each row two strokes of ink in
   air, laid on a ground in a 3840-pixel frame that stays in cache, for dark
   text (weight 0) and light text (weight 32). The former loop is font_mix. */
#include "../../src/lib.util.c"
#include "../../src/canvas/font.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

static font_engine engine;
static p8 atlas[1024 * 1024];
static p8 glyph[256 * 200];
static p32 frame[3840 * 220], frame_c[3840 * 220];
static const p32 ink = 0x202020;
static p32 weight;

__attribute__((noinline, noclone)) static positive former_mix(p32 address_to into, positive stride,
                                                              const p8 address_to from, positive from_stride,
                                                              positive width, positive height)
{
        for (positive r = 0; r < height; r++)
                for (positive c = 0; c < width; c++)
                {
                        p32 cover = from[r * from_stride + c];

                        if (cover)
                                into[r * stride + c] = font_mix(&engine, into[r * stride + c], ink, engine.ramp[cover]);
                }
        return width * height;
}

//      Sixteen glyphs along a line, each laid on ground the turn before it.
static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        p64 took = 0;

        for (positive round = 0; round < rounds; round++)
        {
                for (positive r = 0; r < height; r++)
                        memory_fill_u32(frame + r * 3840, 16 * width, 0x203040);
                p64 start = get_cpu_time();
                for (positive g = 0; g < 16; g++)
                        if (assembly)
                                memory_mix_u32_rect(frame + g * width, 3840, glyph, 256, width, height, ink, weight,
                                                    engine.ramp, font_to_linear, font_srgb_bytes);
                        else
                                former_mix(frame + g * width, 3840, glyph, 256, width, height);
                took += get_cpu_time() - start;
        }
        return took;
}

b32 main(void)
{
        static const positive shapes[][2] = {{8, 13}, {14, 24}, {28, 48}, {120, 200}};
        static const p32 weights[] = {0, 32};
        p64 seed = 0x9e3779b97f4a7c15ULL;

        //      Each row two strokes, a fifth of the row wide each, at random
        //      places: runs of ink and air as a glyph's rows have them.
        for (positive r = 0; r < 200; r++)
                for (positive k = 0; k < 2; k++)
                {
                        XORSHIFT64(seed);
                        for (positive w = 0; w < 4; w++)
                        {
                                positive width = w == 0 ? 8 : w == 1 ? 14 : w == 2 ? 28 : 120;
                                positive at = (positive)(seed >> (8 * w)) % width, run = width / 5 + 1;

                                for (positive c = at; c < at + run && c < width; c++)
                                        glyph[r * 256 + c] = (p8)(seed >> 40 | 1);
                        }
                }
        font_start(&engine, atlas, 1024, 1024);
        font_ramp(&engine, ink, FONT_UNKNOWN, 64);
        for (positive i = 0; i < sizeof(weights) / sizeof(weights[0]); i++)
        {
                weight = engine.plain = weights[i];
                for (positive row = 0; row < sizeof(shapes) / sizeof(shapes[0]); row++)
                {
                        positive width = shapes[row][0], height = shapes[row][1];
                        positive ratios[TRIES];
                        positive rounds = 400000 / (width * height * 16) + 1;

                        memory_fill_u32(frame, sizeof(frame) / 4, 0x203040);
                        memory_fill_u32(frame_c, sizeof(frame_c) / 4, 0x203040);
                        memory_mix_u32_rect(frame, 3840, glyph, 256, width, height, ink, weight, engine.ramp,
                                            font_to_linear, font_srgb_bytes);
                        former_mix(frame_c, 3840, glyph, 256, width, height);
                        if (memory_compare(frame, frame_c, sizeof(frame)))
                                return 1;
                        BENCH_PAIRED_RUNS(ratios, former, assembly, width, height, rounds);
                        string_format(log, "memory_mix_u32_rect %p by %p weight %p: paired median ASM/C %p.", width,
                                      height, (positive)weight, ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
        log_flush();
        return 0;
}
#endif /* BENCH_mix_rect */

#ifdef BENCH_fill_rect
/* memory_fill_u32_rect against the loop it replaced in font_cell, a
   memory_fill_u32 a row: blank terminal cells at 1080p and 4K (9 by 19, 16
   by 35), a sliver (3 by 10), a blank run of a line (240 by 35) and a
   panel (1000 by 20), in a 3840-pixel frame; on x86_64 with AVX-512 and
   without. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

static p32 frame[3840 * 40];

__attribute__((noinline, noclone)) static fn former_fill(p32 address_to into, positive stride, positive width,
                                                         positive height, p32 value)
{
        for (positive r = 0; r < height; r++)
                memory_fill_u32(into + r * stride, width, value);
}

static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
                for (positive cell = 0; cell < 16; cell++)
                {
                        p32 address_to into = frame + cell * width % (3840 - width);

                        if (assembly)
                                memory_fill_u32_rect(into, 3840, width, height, 0x101820 + (p32)round);
                        else
                                former_fill(into, 3840, width, height, 0x101820 + (p32)round);
                }
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[][2] = {{3, 10}, {9, 19}, {16, 35}, {240, 35}, {1000, 20}};
#if X64
        p8 wide = cpu_has_avx512;
        for (p32 tier = 0; tier < (wide ? 2u : 1u); tier++)
        {
                cpu_has_avx512 = tier ? 0 : wide;
#endif
                for (positive row = 0; row < sizeof(shapes) / sizeof(shapes[0]); row++)
                {
                        positive width = shapes[row][0], height = shapes[row][1];
                        positive ratios[TRIES];
                        positive rounds = 2000000 / (width * height * 16) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, width, height, rounds);
                        string_format(log, "memory_fill_u32_rect %p by %p%s: paired median ASM/C %p.", width, height,
#if X64
                                      cpu_has_avx512 ? (string_address) " avx512" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx512 = wide;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_fill_rect */

#ifdef BENCH_over_tone
/*
        memory_over_tone_u32 against the loop it replaced in ui_kit_run and ui_fill: ui_over, one tone laid over each
        pixel of a run, at runs of 8, 64 and 1024 pixels, in the machine's own tier of each body (x86_64 with what the
        CPU has, arm64 and riscv64 as they are built). The minimum of nine paired runs, in thousandths of the C loop.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

static p32 over_frame[4096];

__attribute__((noinline, noclone)) static fn former_over(p32 address_to into, positive count, p32 tone)
{
        for (positive i = 0; i < count; i++)
                into[i] = over_tone_reference(tone, into[i]);
}

static p64 run_over(p8 assembly, positive count, positive rounds)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
                for (positive row = 0; row < 16; row++)
                {
                        p32 address_to into = over_frame + row * count % (4096 - count);

                        if (assembly)
                                memory_over_tone_u32(into, count, 0x80402010u + (p32)round);
                        else
                                former_over(into, count, 0x80402010u + (p32)round);
                }
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[] = {8, 64, 1024};

        for (positive k = 0; k < sizeof(shapes) / sizeof(shapes[0]); k++)
        {
                positive count = shapes[k], rounds = 400000 / count + 1;
                p64 best_asm = ~(p64)0, best_c = ~(p64)0;

                for (positive t = 0; t < TRIES; t++)
                {
                        p64 a, c;

                        for (positive i = 0; i < 4096; i++)
                                over_frame[i] = 0x203040u + i * 2654435761u;
                        a = run_over(true, count, rounds);
                        for (positive i = 0; i < 4096; i++)
                                over_frame[i] = 0x203040u + i * 2654435761u;
                        c = run_over(false, count, rounds);
                        if (a < best_asm)
                                best_asm = a;
                        if (c < best_c)
                                best_c = c;
                }
                string_format(log, "memory_over_tone_u32 %p pixels: ASM/C %p per mille\n", count,
                              (positive)(best_asm * 1000 / best_c));
        }
        log_flush();
        return 0;
}
#endif /* BENCH_over_tone */

#ifdef BENCH_train
/* The training and inference block against the compiler: the C loop each
   routine replaces, built -O3 for the tier being timed (x86-64-v4 with 512-bit
   vectors for the AVX-512 bodies, x86-64-v3 for AVX2 + FMA, the baseline for
   SSE2; riscv64's with V for the V bodies), at sizes that live in L1, L2 and,
   natively, L3: fused_residual_add_f32, f32_to_bf16, bf16_to_f32 and
   fast_argmax_f32 over 2048, 65536 and 2097152 elements, and fast_argmax_f32
   again against the most the compiler makes of it, the fast-math two passes
   (vectorised, and blind to NaN). Paired medians of ASM/C and each side's
   best ticks a call; a name on the command line runs one shape alone for
   perf stat. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

#if X64
#define TRAIN_C_WIDE __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v4,prefer-vector-width=512")))
#define TRAIN_C_YMM __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3")))
#elif RISCV64
#define TRAIN_C_WIDE __attribute__((noinline, noclone, optimize("O3"), target("arch=+v")))
#define TRAIN_C_YMM __attribute__((noinline, noclone, optimize("O3")))
#else
#define TRAIN_C_WIDE __attribute__((noinline, noclone, optimize("O3")))
#define TRAIN_C_YMM __attribute__((noinline, noclone, optimize("O3")))
#endif
#define TRAIN_C_BASE __attribute__((noinline, noclone, optimize("O3")))

//      The C each routine replaces: the residual add, and the bf16 definition
//      both ways.
#define TRAIN_C_LOOPS(tag, attributes)                                                                    \
        attributes static fn train_c_residual_##tag(f32 *restrict x, const f32 *restrict h, positive count, \
                                                    f32 alpha)                                            \
        {                                                                                                 \
                for (positive i = 0; i < count; i++)                                                      \
                        x[i] = x[i] + alpha * h[i];                                                       \
        }                                                                                                 \
        attributes static fn train_c_to_bf16_##tag(p16 *restrict to, const p32 *restrict from, positive count) \
        {                                                                                                 \
                for (positive i = 0; i < count; i++)                                                      \
                {                                                                                         \
                        p32 v = from[i];                                                                  \
                        to[i] = (v & 0x7f800000u) == 0          ? (p16)(v >> 16 & 0x8000u)                \
                                : (v & 0x7fffffffu) > 0x7f800000u ? (p16)(v >> 16 | 0x40u)                \
                                                                  : (p16)((v + 0x7fffu + (v >> 16 & 1u)) >> 16); \
                }                                                                                         \
        }                                                                                                 \
        attributes static fn train_c_from_bf16_##tag(p32 *restrict to, const p16 *restrict from, positive count) \
        {                                                                                                 \
                for (positive i = 0; i < count; i++)                                                      \
                        to[i] = (p32)from[i] << 16;                                                       \
        }                                                                                                 \
        attributes static positive train_c_argmax_##tag(const f32 *x, positive count)                     \
        {                                                                                                 \
                positive best = 0;                                                                        \
                f32 value = count ? x[0] : 0;                                                             \
                for (positive i = 1; i < count && value == value; i++)                                    \
                        if (x[i] > value || x[i] != x[i])                                                 \
                                value = x[i], best = i;                                                   \
                return best;                                                                              \
        }
TRAIN_C_LOOPS(wide, TRAIN_C_WIDE)
TRAIN_C_LOOPS(ymm, TRAIN_C_YMM)
TRAIN_C_LOOPS(base, TRAIN_C_BASE)

//      The most the compiler makes of argmax, a second bar: the maximum under
//      fast-math (vectorised, and blind to NaN, so not the routine's
//      contract), then a counted search for the first equal to it.
#if X64
#define TRAIN_C_FAST_WIDE __attribute__((noinline, noclone, optimize("O3,fast-math"), target("arch=x86-64-v4,prefer-vector-width=512")))
#define TRAIN_C_FAST_YMM __attribute__((noinline, noclone, optimize("O3,fast-math"), target("arch=x86-64-v3")))
#elif RISCV64
#define TRAIN_C_FAST_WIDE __attribute__((noinline, noclone, optimize("O3,fast-math"), target("arch=+v")))
#define TRAIN_C_FAST_YMM __attribute__((noinline, noclone, optimize("O3,fast-math")))
#else
#define TRAIN_C_FAST_WIDE __attribute__((noinline, noclone, optimize("O3,fast-math")))
#define TRAIN_C_FAST_YMM __attribute__((noinline, noclone, optimize("O3,fast-math")))
#endif
#define TRAIN_C_FAST_BASE __attribute__((noinline, noclone, optimize("O3,fast-math")))
#define TRAIN_C_FAST(tag, attributes)                                                                     \
        attributes static positive train_c_fast_argmax_##tag(const f32 *x, positive count)                \
        {                                                                                                 \
                f32 m = x[0];                                                                             \
                for (positive i = 1; i < count; i++)                                                      \
                        m = x[i] > m ? x[i] : m;                                                          \
                for (positive i = 0; i < count; i++)                                                      \
                        if (x[i] == m)                                                                    \
                                return i;                                                                 \
                return 0;                                                                                 \
        }
TRAIN_C_FAST(wide, TRAIN_C_FAST_WIDE)
TRAIN_C_FAST(ymm, TRAIN_C_FAST_YMM)
TRAIN_C_FAST(base, TRAIN_C_FAST_BASE)

#define TRAIN_FLOATS 2097152
static f32 *train_x, *train_h;
static p16 *train_b;
static p32 train_tier;

static p64 train_residual(p8 assembly, positive count, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                if (assembly)
                        fused_residual_add_f32(train_x, train_h, count, 1e-7f);
                else if (train_tier == 0)
                        train_c_residual_wide(train_x, train_h, count, 1e-7f);
                else if (train_tier == 1)
                        train_c_residual_ymm(train_x, train_h, count, 1e-7f);
                else
                        train_c_residual_base(train_x, train_h, count, 1e-7f);
        return get_cpu_time() - start;
}

static p64 train_to_bf16(p8 assembly, positive count, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                if (assembly)
                        f32_to_bf16(train_b, train_x, count);
                else if (train_tier == 0)
                        train_c_to_bf16_wide(train_b, (const p32 *)train_x, count);
                else if (train_tier == 1)
                        train_c_to_bf16_ymm(train_b, (const p32 *)train_x, count);
                else
                        train_c_to_bf16_base(train_b, (const p32 *)train_x, count);
        return get_cpu_time() - start;
}

static p64 train_from_bf16(p8 assembly, positive count, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                if (assembly)
                        bf16_to_f32(train_h, train_b, count);
                else if (train_tier == 0)
                        train_c_from_bf16_wide((p32 *)train_h, train_b, count);
                else if (train_tier == 1)
                        train_c_from_bf16_ymm((p32 *)train_h, train_b, count);
                else
                        train_c_from_bf16_base((p32 *)train_h, train_b, count);
        return get_cpu_time() - start;
}

static volatile positive train_sink;

static p64 train_argmax(p8 assembly, positive count, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                if (assembly)
                        train_sink += fast_argmax_f32(train_x, count);
                else if (train_tier == 0)
                        train_sink += train_c_argmax_wide(train_x, count);
                else if (train_tier == 1)
                        train_sink += train_c_argmax_ymm(train_x, count);
                else
                        train_sink += train_c_argmax_base(train_x, count);
        return get_cpu_time() - start;
}

static p64 train_argmax_fast(p8 assembly, positive count, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                if (assembly)
                        train_sink += fast_argmax_f32(train_x, count);
                else if (train_tier == 0)
                        train_sink += train_c_fast_argmax_wide(train_x, count);
                else if (train_tier == 1)
                        train_sink += train_c_fast_argmax_ymm(train_x, count);
                else
                        train_sink += train_c_fast_argmax_base(train_x, count);
        return get_cpu_time() - start;
}

typedef p64 (*train_run)(p8 assembly, positive count, positive reps);

static fn train_hundredths(positive value)
{
        string_format(log, "%p.", value / 100);
        positive_to_padded(log, value % 100, 2, '0', 0);
}

//      One routine over its sizes on the tier set now: a paired median of
//      ASM/C and the best ticks a call of each side.
static fn train_shapes(string_address name, train_run run, string_address tier)
{
#if X64
        static const positive counts[] = {2048, 65536, 2097152};
#else
        static const positive counts[] = {2048, 65536};
#endif
        for (positive c = 0; c < sizeof(counts) / sizeof(counts[0]); c++)
        {
                positive count = counts[c], ratios[TRIES];
#if X64
                positive reps = (1u << 25) / count + 1;
#else
                positive reps = (1u << 17) / count + 1;
#endif
                p64 best_c = ~(p64)0, best_a = ~(p64)0;

                for (positive trial = 0; trial < TRIES; trial++)
                {
                        p64 former, assembly;

                        BENCH_ALTERNATE(trial, former, run(false, count, reps), assembly, run(true, count, reps));
                        ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                        best_c = former < best_c ? former : best_c;
                        best_a = assembly < best_a ? assembly : best_a;
                }
                order(ratios, TRIES);
                string_format(log, "%s %p, %s: paired median ASM/C ", name, count, tier);
                train_hundredths(ratios[TRIES / 2]);
                string_format(log, "%%; best ticks a call ASM ");
                train_hundredths((positive)(best_a * 100 / reps));
                string_format(log, ", C ");
                train_hundredths((positive)(best_c * 100 / reps));
                log("\n", 1);
        }
}

static fn train_all(string_address tier)
{
        train_shapes("fused_residual_add_f32", train_residual, tier);
        train_shapes("f32_to_bf16", train_to_bf16, tier);
        train_shapes("bf16_to_f32", train_from_bf16, tier);
        train_shapes("fast_argmax_f32", train_argmax, tier);
        train_shapes("fast_argmax_f32 against fast-math C", train_argmax_fast, tier);
}

//      One shape on one tier, alone, for perf stat; "nothing" is the same
//      program doing none, the baseline to subtract. Tier 0 is AVX-512 (with
//      BF16 where the machine has it), 1 AVX2 + FMA, 2 SSE2.
#if X64
static fn train_force(p32 tier)
{
        static p8 wide512 = 3, wide2, fused, bf16;

        if (wide512 == 3)
        {
                f32_to_bf16(train_b, train_x, 1);
                wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma, bf16 = cpu_has_bf16;
        }
        cpu_has_avx512 = tier == 0 ? wide512 : 0;
        cpu_has_avx2 = tier < 2 ? wide2 : 0;
        cpu_has_fma = tier < 2 ? fused : 0;
        cpu_has_bf16 = bf16;
        train_tier = tier;
}
#else
static fn train_force(p32 tier)
{
        (void)tier;
        train_tier = 2;
}
#endif
#define TRAIN_NAMED(id, run, count, tier, assembly)                                                      \
        static fn train_named_##id(void)                                                                 \
        {                                                                                                \
                train_force(tier);                                                                       \
                run(assembly, count, (1u << 28) / (count));                                              \
        }
TRAIN_NAMED(residual_2048_zmm, train_residual, 2048, 0, true)
TRAIN_NAMED(residual_2048_zmm_c, train_residual, 2048, 0, false)
TRAIN_NAMED(residual_2048_ymm, train_residual, 2048, 1, true)
TRAIN_NAMED(residual_2048_ymm_c, train_residual, 2048, 1, false)
TRAIN_NAMED(residual_2048_xmm, train_residual, 2048, 2, true)
TRAIN_NAMED(residual_2048_xmm_c, train_residual, 2048, 2, false)
TRAIN_NAMED(residual_2097152_zmm, train_residual, 2097152, 0, true)
TRAIN_NAMED(residual_2097152_zmm_c, train_residual, 2097152, 0, false)
TRAIN_NAMED(to_bf16_2048_zmm, train_to_bf16, 2048, 0, true)
TRAIN_NAMED(to_bf16_2048_zmm_c, train_to_bf16, 2048, 0, false)
TRAIN_NAMED(from_bf16_2048_zmm, train_from_bf16, 2048, 0, true)
TRAIN_NAMED(from_bf16_2048_zmm_c, train_from_bf16, 2048, 0, false)
TRAIN_NAMED(argmax_2048_zmm, train_argmax, 2048, 0, true)
TRAIN_NAMED(argmax_2048_zmm_c, train_argmax, 2048, 0, false)
TRAIN_NAMED(argmax_2048_ymm, train_argmax, 2048, 1, true)
TRAIN_NAMED(argmax_2048_ymm_c, train_argmax, 2048, 1, false)

static fn train_named_nothing(void)
{
}

static bench_work train_named(string_address name)
{
        BENCH_NAME(name, "nothing", train_named_nothing);
        BENCH_NAME(name, "residual-2048-zmm", train_named_residual_2048_zmm);
        BENCH_NAME(name, "residual-2048-zmm-c", train_named_residual_2048_zmm_c);
        BENCH_NAME(name, "residual-2048-ymm", train_named_residual_2048_ymm);
        BENCH_NAME(name, "residual-2048-ymm-c", train_named_residual_2048_ymm_c);
        BENCH_NAME(name, "residual-2048-xmm", train_named_residual_2048_xmm);
        BENCH_NAME(name, "residual-2048-xmm-c", train_named_residual_2048_xmm_c);
        BENCH_NAME(name, "residual-2097152-zmm", train_named_residual_2097152_zmm);
        BENCH_NAME(name, "residual-2097152-zmm-c", train_named_residual_2097152_zmm_c);
        BENCH_NAME(name, "to-bf16-2048-zmm", train_named_to_bf16_2048_zmm);
        BENCH_NAME(name, "to-bf16-2048-zmm-c", train_named_to_bf16_2048_zmm_c);
        BENCH_NAME(name, "from-bf16-2048-zmm", train_named_from_bf16_2048_zmm);
        BENCH_NAME(name, "from-bf16-2048-zmm-c", train_named_from_bf16_2048_zmm_c);
        BENCH_NAME(name, "argmax-2048-zmm", train_named_argmax_2048_zmm);
        BENCH_NAME(name, "argmax-2048-zmm-c", train_named_argmax_2048_zmm_c);
        BENCH_NAME(name, "argmax-2048-ymm", train_named_argmax_2048_ymm);
        BENCH_NAME(name, "argmax-2048-ymm-c", train_named_argmax_2048_ymm_c);
        return null;
}

b32 main(void)
{
        train_x = memory(TRAIN_FLOATS * 4), train_h = memory((TRAIN_FLOATS + 512) * 4), train_b = memory(TRAIN_FLOATS * 2);
        if (!train_x || !train_h || !train_b)
                return 1;
        train_h += 480;
        p64 state = 0x243f6a8885a308d3ull;
        for (positive i = 0; i < TRAIN_FLOATS; i++)
        {
                XORSHIFT64(state);
                train_x[i] = 1.0f / (f32)(1 + (i & 1023)), train_h[i] = 1e-3f * (f32)(i & 255);
                train_b[i] = (p16)(state >> 24);
        }
        BENCH_RUN_NAMED(train_named);
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;

        f32_to_bf16(train_b, train_x, 1);
        for (p32 tier = 0; tier < 3; tier++)
        {
                if ((tier == 0 && !wide512) || (tier == 1 && !(wide2 && fused)))
                        continue;
                train_force(tier);
                train_all(tier == 0 ? (cpu_has_bf16 == 2 ? "avx512 bf16" : "avx512") : tier == 1 ? "avx2 fma" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2, cpu_has_fma = fused;
#elif RISCV64
        cpu_has_vector = 0;
        fused_residual_add_f32(train_x, train_h, 1, 1e-7f);
        p8 vector = cpu_has_vector;

        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
                train_tier = tier == 0 ? 0 : 2;
                train_all(tier == 0 ? "V" : "no V");
        }
        cpu_has_vector = vector;
#else
        train_tier = 2;
        train_all("neon");
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_train */

#ifdef BENCH_audiolib
/* The audio routines of lib.c against the C loops of the sound server's mixing core they replaced (src/audio/mix.c), over the
   shapes a mixer's period has them in: resample_polyphase2_f32 at 44.1, 22.05, 96 and 192 kHz into 48 kHz, 256 frames a call as
   the sink's period is, and 16 frames, with three tables rotating so that the rows come from L2 as a mixer's do. Two bars: the C
   as the service is built (-O2, the baseline instruction set) and the most a compiler makes of the same loop at -O3 for the tier
   (x86-64-v3, riscv64's V). Paired medians of ASM/C and each side's best ticks an output frame; a name on the command line
   runs one shape alone for perf stat. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define SHARED_audio_loops
#include "../checks.c"
#undef SHARED_audio_loops

#define TRIES 9

#if X64
AUDIO_C_POLYPHASE2(al_c_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3"))))
#elif RISCV64
AUDIO_C_POLYPHASE2(al_c_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
#else
AUDIO_C_POLYPHASE2(al_c_wide, __attribute__((noinline, noclone, optimize("O3"))))
#endif
AUDIO_C_POLYPHASE2(al_c_base, __attribute__((noinline, noclone, optimize("O2"))))
AUDIO_C_POLYPHASE2(al_c_o3, __attribute__((noinline, noclone, optimize("O3"))))

//      The gain loop the same three ways.
#if X64
AUDIO_C_SCALE(al_s_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3"))))
AUDIO_C_SCALE(al_s_zmm, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v4,prefer-vector-width=512"))))
#elif RISCV64
AUDIO_C_SCALE(al_s_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
AUDIO_C_SCALE(al_s_zmm, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
#else
AUDIO_C_SCALE(al_s_wide, __attribute__((noinline, noclone, optimize("O3"))))
AUDIO_C_SCALE(al_s_zmm, __attribute__((noinline, noclone, optimize("O3"))))
#endif
AUDIO_C_SCALE(al_s_base, __attribute__((noinline, noclone, optimize("O2"))))
AUDIO_C_SCALE(al_s_o3, __attribute__((noinline, noclone, optimize("O3"))))

//      And the sink's stages.
#if X64
AUDIO_C_ENCODE(al_e_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3"))))
AUDIO_C_CLIP(al_k_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3"))))
#elif RISCV64
AUDIO_C_ENCODE(al_e_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
AUDIO_C_CLIP(al_k_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
#else
AUDIO_C_ENCODE(al_e_wide, __attribute__((noinline, noclone, optimize("O3"))))
AUDIO_C_CLIP(al_k_wide, __attribute__((noinline, noclone, optimize("O3"))))
#endif
AUDIO_C_ENCODE(al_e_base, __attribute__((noinline, noclone, optimize("O2"))))
AUDIO_C_CLIP(al_k_base, __attribute__((noinline, noclone, optimize("O2"))))
AUDIO_C_ENCODE(al_e_o3, __attribute__((noinline, noclone, optimize("O3"))))
AUDIO_C_CLIP(al_k_o3, __attribute__((noinline, noclone, optimize("O3"))))

//      The remix to two.
#if X64
AUDIO_C_MIX2(al_m_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3"))))
#elif RISCV64
AUDIO_C_MIX2(al_m_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
#else
AUDIO_C_MIX2(al_m_wide, __attribute__((noinline, noclone, optimize("O3"))))
#endif
AUDIO_C_MIX2(al_m_base, __attribute__((noinline, noclone, optimize("O2"))))
AUDIO_C_MIX2(al_m_o3, __attribute__((noinline, noclone, optimize("O3"))))

//      The decode loop the same ways.
#if X64
AUDIO_C_DECODE(al_d_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=x86-64-v3"))))
#elif RISCV64
AUDIO_C_DECODE(al_d_wide, __attribute__((noinline, noclone, optimize("O3"), target("arch=+v"))))
#else
AUDIO_C_DECODE(al_d_wide, __attribute__((noinline, noclone, optimize("O3"))))
#endif
AUDIO_C_DECODE(al_d_base, __attribute__((noinline, noclone, optimize("O2"))))
AUDIO_C_DECODE(al_d_o3, __attribute__((noinline, noclone, optimize("O3"))))

typedef struct
{
        string_address name;
        positive taps, phases, count, voices;
        p64 step;
} al_shape;

static const al_shape al_shapes[] = {
    {"44.1 kHz into 48 kHz, 64 taps", 64, 512, 256, 3, 3946001203ull},
    {"22.05 kHz into 48 kHz, 64 taps", 64, 512, 256, 3, 1973000601ull},
    {"96 kHz into 48 kHz, 128 taps", 128, 256, 256, 3, 8589934592ull},
    {"192 kHz into 48 kHz, 256 taps", 256, 128, 256, 2, 17179869184ull},
    {"44.1 kHz into 48 kHz, 16 frames", 64, 512, 16, 3, 3946001203ull},
    // The taps of the design of 2026-11-04 (mix.c: 128 a ratio of one or less, 256 at 2:1, 512 at 4:1; cutoff 0.95 of the lower Nyquist rate).
    {"44.1 kHz into 48 kHz, 128 taps", 128, 512, 256, 3, 3946001203ull},
    {"96 kHz into 48 kHz, 256 taps", 256, 256, 256, 3, 8589934592ull},
    {"192 kHz into 48 kHz, 512 taps", 512, 128, 256, 2, 17179869184ull},
};

#define AL_VOICES 3
#define AL_INPUT 16384
static f32 *al_table[AL_VOICES], *al_in[AL_VOICES][2], *al_out[2];
static p8 al_wide = true; // the tier under test has the wider instruction set the -O3 bar is built for
static p32 al_bar; // 0 the C as the service is built, 1 -O3 for the wide tier (x86-64-v3, V), 2 -O3 for the baseline

static p64 al_run(p8 assembly, positive shape, positive reps)
{
        const al_shape *s = al_shapes + shape;
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
        {
                positive v = r % s->voices;
                p64 pos = (p64)(r * 2654435761u) & 0xffffffffu;

                if (assembly)
                        resample_polyphase2_f32(al_out[0], al_out[1], al_in[v][0], al_in[v][1], al_table[v], s->taps, s->phases, pos, s->step, s->count);
                else if (al_bar == 1)
                        al_c_wide(al_out[0], al_out[1], al_in[v][0], al_in[v][1], al_table[v], s->taps, s->phases, pos, s->step, s->count);
                else if (al_bar == 2)
                        al_c_o3(al_out[0], al_out[1], al_in[v][0], al_in[v][1], al_table[v], s->taps, s->phases, pos, s->step, s->count);
                else
                        al_c_base(al_out[0], al_out[1], al_in[v][0], al_in[v][1], al_table[v], s->taps, s->phases, pos, s->step, s->count);
        }
        return get_cpu_time() - start;
}

static fn al_hundredths(positive value)
{
        string_format(log, "%p.", value / 100);
        positive_to_padded(log, value % 100, 2, '0', 0);
}

/*
        The gain loop: a block of 256 floats added to a bus (what a voice does twice a period), 512 of them, 64, and 16, and 256
        stored (the master gain), on arrays on a cache line and, for the last row, four bytes off one.
*/
typedef struct
{
        string_address name;
        positive count, add, offset;
} al_scale_shape;

static const al_scale_shape al_scale_shapes[] = {
    {"256 floats added", 256, 1, 0},  {"256 floats stored", 256, 0, 0}, {"512 floats added", 512, 1, 0},
    {"64 floats added", 64, 1, 0},    {"16 floats added", 16, 1, 0},     {"256 floats added, 4 bytes off a line", 256, 1, 1},
};
static f32 *al_scale_bus, *al_scale_in;
static p32 al_scale_bar; // 0 the C as built, 1 -O3 for the tier, 2 -O3 for the baseline, 3 -O3 at 512 bits

static volatile f32 al_scale_sink;

static p64 al_scale_run(p8 assembly, positive shape, positive reps)
{
        const al_scale_shape *s = al_scale_shapes + shape;
        p64 start = get_cpu_time();
        f32 peak = 0;

        for (positive r = 0; r < reps; r++)
        {
                f32 *dst = al_scale_bus + s->offset + (r & 3) * 1024, *x = al_scale_in + s->offset + (r & 3) * 1024;

                if (assembly)
                        peak += s->add ? f32_scale_add_peak(dst, x, s->count, 0.999f) : f32_scale_peak(dst, x, s->count, 0.999f);
                else if (al_scale_bar == 1)
                        peak += al_s_wide(dst, x, s->count, 0.999f, (int)s->add);
                else if (al_scale_bar == 2)
                        peak += al_s_o3(dst, x, s->count, 0.999f, (int)s->add);
                else if (al_scale_bar == 3)
                        peak += al_s_zmm(dst, x, s->count, 0.999f, (int)s->add);
                else
                        peak += al_s_base(dst, x, s->count, 0.999f, (int)s->add);
        }
        al_scale_sink = peak;
        return get_cpu_time() - start;
}

//      One shape on the tier set now, against the bar set now.
static fn al_shapes_run(string_address tier)
{
        for (positive shape = 0; shape < sizeof(al_shapes) / sizeof(al_shapes[0]); shape++)
                for (p32 bar = 0; bar < 2; bar++)
                {
                        al_bar = bar == 0 ? 0 : al_wide ? 1 : 2;
                        positive reps = (1u << 22) / al_shapes[shape].count / (al_shapes[shape].taps / 64) + 1, ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, al_run(false, shape, reps), assembly, al_run(true, shape, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "resample_polyphase2_f32 %s, %s against C %s: paired median ASM/C ", al_shapes[shape].name, tier,
                                      bar ? "-O3" : "-O2 as built");
                        al_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a frame ASM ");
                        al_hundredths((positive)(best_a * 100 / (reps * al_shapes[shape].count)));
                        string_format(log, ", C ");
                        al_hundredths((positive)(best_c * 100 / (reps * al_shapes[shape].count)));
                        log("\n", 1);
                }
}

/*
        The rows-only routine against the weighted one it takes the place of for a ratio of two rates that reduces to p / q: the
        same taps, the same step, the rows kernel over the q rows of the ratio's table and the weighted kernel over the 512 phases
        a table has now, three tables rotating as in the weighted rows above.
*/
typedef struct
{
        string_address name;
        positive taps, phases, count, voices;
        p64 step;
} al_rows_shape;

static const al_rows_shape al_rows_shapes[] = {
    {"44.1 kHz into 48 kHz, 64 taps, 160 rows", 64, 160, 256, 3, (147ull << 32) / 160 + 1},
    {"22.05 kHz into 48 kHz, 64 taps, 320 rows", 64, 320, 256, 3, (147ull << 32) / 320 + 1},
    {"32 kHz into 48 kHz, 64 taps, 3 rows", 64, 3, 256, 3, (2ull << 32) / 3 + 1},
    {"96 kHz into 48 kHz, 128 taps, 1 row", 128, 1, 256, 3, 2ull << 32},
    {"192 kHz into 48 kHz, 256 taps, 1 row", 256, 1, 256, 2, 4ull << 32},
    {"44.1 kHz into 48 kHz, 16 frames", 64, 160, 16, 3, (147ull << 32) / 160 + 1},
    {"inputs always on a line (16 inputs a frame), 64 taps, 1 row", 64, 1, 256, 3, 16ull << 32},
    {"inputs never on a line (17 inputs a frame), 64 taps, 1 row", 64, 1, 256, 3, 17ull << 32},
};

static volatile p64 al_rows_sink;

static p64 al_rows_run(p8 rows, positive shape, positive reps)
{
        const al_rows_shape *s = al_rows_shapes + shape;
        p64 start = get_cpu_time(), walked = 0;

        for (positive r = 0; r < reps; r++)
        {
                positive v = r % s->voices;
                p64 pos = (p64)(r * 2654435761u) & 0xffffffffu;

                if (rows)
                        walked += resample_polyphase2_f32_rows(al_out[0], al_out[1], al_in[v][0], al_in[v][1], al_table[v], s->taps, s->phases, pos, s->step, s->count);
                else
                        resample_polyphase2_f32(al_out[0], al_out[1], al_in[v][0], al_in[v][1], al_table[v], s->taps, 512, pos, s->step, s->count);
        }
        al_rows_sink = walked;
        return get_cpu_time() - start;
}

static fn al_rows_bench(string_address tier)
{
        for (positive shape = 0; shape < sizeof(al_rows_shapes) / sizeof(al_rows_shapes[0]); shape++)
        {
                positive reps = (1u << 22) / al_rows_shapes[shape].count / (al_rows_shapes[shape].taps / 64) + 1, ratios[TRIES];
                p64 best_w = ~(p64)0, best_r = ~(p64)0;

                for (positive trial = 0; trial < TRIES; trial++)
                {
                        p64 weighted, rows;

                        BENCH_ALTERNATE(trial, weighted, al_rows_run(false, shape, reps), rows, al_rows_run(true, shape, reps));
                        ratios[trial] = (positive)(rows * 10000 / (weighted ? weighted : 1));
                        best_w = weighted < best_w ? weighted : best_w;
                        best_r = rows < best_r ? rows : best_r;
                }
                order(ratios, TRIES);
                string_format(log, "resample_polyphase2_f32_rows %s, %s against the weighted routine: paired median rows/weighted ", al_rows_shapes[shape].name, tier);
                al_hundredths(ratios[TRIES / 2]);
                string_format(log, "%%; best ticks a frame rows ");
                al_hundredths((positive)(best_r * 100 / (reps * al_rows_shapes[shape].count)));
                string_format(log, ", weighted ");
                al_hundredths((positive)(best_w * 100 / (reps * al_rows_shapes[shape].count)));
                log("\n", 1);
        }
}

/*
        The decode: 256 frames of stereo, of mono and of six channels (the voice's block, a period), and 16 frames of stereo, in
        each of the three formats, from a source at 4096 bytes on and so off a line by a few bytes (a ring's bytes are where
        the program's last write ended).
*/
typedef struct
{
        string_address name;
        positive format, frames, channels;
} al_decode_shape;

static const al_decode_shape al_decode_shapes[] = {
    {"s16 stereo 256 frames", 0, 256, 2}, {"s16 mono 256 frames", 0, 256, 1}, {"s16 six channels 256 frames", 0, 256, 6}, {"s16 stereo 16 frames", 0, 16, 2},
    {"s32 stereo 256 frames", 1, 256, 2}, {"s32 mono 256 frames", 1, 256, 1}, {"s32 six channels 256 frames", 1, 256, 6}, {"s32 stereo 16 frames", 1, 16, 2},
    {"f32 stereo 256 frames", 2, 256, 2}, {"f32 mono 256 frames", 2, 256, 1}, {"f32 six channels 256 frames", 2, 256, 6}, {"f32 stereo 16 frames", 2, 16, 2},
};
static p8 *al_decode_in;
static f32 *al_decode_out[8];
static p32 al_decode_bar; // 0 the C as built, 1 -O3 for the tier, 2 -O3 for the baseline

static p64 al_decode_run(p8 assembly, positive shape, positive reps)
{
        const al_decode_shape *s = al_decode_shapes + shape;
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
        {
                const p8 *src = al_decode_in + (r & 3) * 8192 + 4 * (r & 1);

                if (assembly)
                {
                        if (s->format == 0)
                                f32_planes_from_s16(al_decode_out, src, s->frames, s->channels);
                        else if (s->format == 1)
                                f32_planes_from_s32(al_decode_out, src, s->frames, s->channels);
                        else
                                f32_planes_from_f32(al_decode_out, src, s->frames, s->channels);
                }
                else if (al_decode_bar == 1)
                        al_d_wide((int)s->format, src, s->frames, s->channels, al_decode_out);
                else if (al_decode_bar == 2)
                        al_d_o3((int)s->format, src, s->frames, s->channels, al_decode_out);
                else
                        al_d_base((int)s->format, src, s->frames, s->channels, al_decode_out);
        }
        return get_cpu_time() - start;
}

static fn al_decode_rows(string_address tier, p8 wide)
{
        for (positive shape = 0; shape < sizeof(al_decode_shapes) / sizeof(al_decode_shapes[0]); shape++)
                for (p32 bar = 0; bar < 2; bar++)
                {
                        positive reps = (1u << 22) / (al_decode_shapes[shape].frames * al_decode_shapes[shape].channels) + 1, ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        al_decode_bar = bar == 0 ? 0 : wide ? 1 : 2;
                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, al_decode_run(false, shape, reps), assembly, al_decode_run(true, shape, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "f32_planes_from_%s %s, %s against C %s: paired median ASM/C ",
                                      al_decode_shapes[shape].format == 0 ? "s16" : al_decode_shapes[shape].format == 1 ? "s32" : "f32", al_decode_shapes[shape].name,
                                      tier, bar == 0 ? "-O2 as built" : "-O3");
                        al_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a call ASM ");
                        al_hundredths((positive)(best_a * 100 / reps));
                        string_format(log, ", C ");
                        al_hundredths((positive)(best_c * 100 / reps));
                        log("\n", 1);
                }
}

/*
        The sink's output: 256 frames of the bus (inside full scale, as a mixer's is) into each format of a card, and 16 frames,
        and the clip over 256 frames with none past full scale and with one in eight past it.
*/
typedef struct
{
        string_address name;
        positive format, frames; // format 0 to 3 encode, 4 clip with none past, 5 clip with one frame in eight
} al_encode_shape;

static const al_encode_shape al_encode_shapes[] = {
    {"s16 stereo 256 frames", 0, 256}, {"s32 stereo 256 frames", 1, 256}, {"s24_32 stereo 256 frames", 2, 256}, {"f32 stereo 256 frames", 3, 256},
    {"s16 stereo 16 frames", 0, 16},   {"clip 256 frames, none past full scale", 4, 256}, {"clip 256 frames, one in eight past", 5, 256},
};
static f32 *al_encode_left, *al_encode_right, *al_encode_keep_left, *al_encode_keep_right;
static p8 *al_encode_out;
static p32 al_encode_bar;

static p64 al_encode_run(p8 assembly, positive shape, positive reps)
{
        const al_encode_shape *s = al_encode_shapes + shape;
        p64 start = get_cpu_time();
        positive sink = 0;

        for (positive r = 0; r < reps; r++)
        {
                if (s->format < 4)
                {
                        if (assembly)
                        {
                                if (s->format == 0)
                                        f32_pair_to_s16(al_encode_out, al_encode_left, al_encode_right, s->frames);
                                else if (s->format == 1)
                                        f32_pair_to_s32(al_encode_out, al_encode_left, al_encode_right, s->frames);
                                else if (s->format == 2)
                                        f32_pair_to_s24_32(al_encode_out, al_encode_left, al_encode_right, s->frames);
                                else
                                        f32_pair_to_f32(al_encode_out, al_encode_left, al_encode_right, s->frames);
                        }
                        else if (al_encode_bar == 1)
                                al_e_wide((int)s->format, al_encode_out, s->frames, al_encode_left, al_encode_right);
                        else if (al_encode_bar == 2)
                                al_e_o3((int)s->format, al_encode_out, s->frames, al_encode_left, al_encode_right);
                        else
                                al_e_base((int)s->format, al_encode_out, s->frames, al_encode_left, al_encode_right);
                }
                else
                {
                        f32 *l = s->format == 4 ? al_encode_left : al_encode_keep_left, *rr = s->format == 4 ? al_encode_right : al_encode_keep_right;

                        //      The planes come back as they were (and so are timed with what puts them back, the same on both sides).
                        if (s->format == 5)
                                for (positive i = 0; i < s->frames; i++)
                                        al_encode_keep_left[i] = (i & 7) == 3 ? 1.5f : al_encode_left[i], al_encode_keep_right[i] = al_encode_right[i];
                        if (assembly)
                                sink += f32_pair_clip(l, rr, s->frames);
                        else if (al_encode_bar == 1)
                                sink += al_k_wide(l, rr, s->frames);
                        else if (al_encode_bar == 2)
                                sink += al_k_o3(l, rr, s->frames);
                        else
                                sink += al_k_base(l, rr, s->frames);
                }
        }
        al_scale_sink = (f32)sink;
        return get_cpu_time() - start;
}

/*
        The remix to two of a voice that is not stereo: 256 frames of mono, of three channels and of 5.1.
*/
static const positive al_mix2_channels[] = {1, 3, 6};
static f32 *al_mix2_rows, *al_mix2_out0, *al_mix2_out1;
static f32 al_mix2_matrix[16] = {0.7071f, 0.5f, 0.25f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.7071f, 0.25f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f};
static p32 al_mix2_bar;

static p64 al_mix2_run(p8 assembly, positive shape, positive reps)
{
        positive channels = al_mix2_channels[shape];
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
        {
                if (assembly)
                        f32_rows_mix2(al_mix2_out0, al_mix2_out1, al_mix2_rows, 512, al_mix2_matrix, 256, channels);
                else if (al_mix2_bar == 1)
                        al_m_wide(al_mix2_out0, al_mix2_out1, al_mix2_rows, 512, al_mix2_matrix, 256, channels);
                else if (al_mix2_bar == 2)
                        al_m_o3(al_mix2_out0, al_mix2_out1, al_mix2_rows, 512, al_mix2_matrix, 256, channels);
                else
                        al_m_base(al_mix2_out0, al_mix2_out1, al_mix2_rows, 512, al_mix2_matrix, 256, channels);
        }
        return get_cpu_time() - start;
}

/*
        The formats the decoders do not take: 512 frames of stereo and of six channels in each, pcm_widen and the library's decoder
        (mix.c's audio_decode, a pass of 4096 lanes at a time) against the loops mix.c had, built as the service is (-O2). Kind 9
        here is the big-endian float, which the widen reads as kind 4 and the float decoder takes.
*/
static float al_law[2][256];
static const unsigned al_old_width[10] = {1, 1, 1, 2, 4, 3, 3, 4, 4, 4};
static const int al_new_kind[10] = {0, -1, -1, 1, 2, 3, 4, 5, 6, 2};
static unsigned al_wide_lanes[4096];

static unsigned al_swap(unsigned x)
{
        return (x >> 24) | ((x >> 8) & 0xff00u) | ((x << 8) & 0xff0000u) | (x << 24);
}

static float al_sane(float x)
{
        x = x > 16.0f ? 16.0f : x;
        x = x < -16.0f ? -16.0f : x;
        return x == x ? x : 0.0f;
}

__attribute__((noinline, noclone, optimize("O2"))) static void al_old_decode(int format, const unsigned char *src, long frames, long channels, float *const *dst)
{
        long bytes = al_old_width[format], stride = bytes * channels;

        for (long c = 0; c < channels; c++)
        {
                const unsigned char *p = src + c * bytes;
                float *d = dst[c];

                switch (format)
                {
                case 0:
                        for (long i = 0; i < frames; i++)
                                d[i] = (float)((int)p[i * stride] - 128) * (1.0f / 128.0f);
                        break;
                case 1:
                        for (long i = 0; i < frames; i++)
                                d[i] = al_law[0][p[i * stride]];
                        break;
                case 2:
                        for (long i = 0; i < frames; i++)
                                d[i] = al_law[1][p[i * stride]];
                        break;
                case 3:
                        for (long i = 0; i < frames; i++)
                                d[i] = (float)(short)((p[i * stride] << 8) | p[i * stride + 1]) * (1.0f / 32768.0f);
                        break;
                case 9:
                        for (long i = 0; i < frames; i++)
                        {
                                unsigned bits;
                                float x;

                                __builtin_memcpy(&bits, p + i * stride, 4);
                                bits = al_swap(bits);
                                __builtin_memcpy(&x, &bits, 4);
                                d[i] = al_sane(x);
                        }
                        break;
                case 4:
                        for (long i = 0; i < frames; i++)
                        {
                                unsigned bits;

                                __builtin_memcpy(&bits, p + i * stride, 4);
                                d[i] = (float)(int)al_swap(bits) * (1.0f / 2147483648.0f);
                        }
                        break;
                case 5:
                        for (long i = 0; i < frames; i++)
                        {
                                const unsigned char *q = p + i * stride;

                                d[i] = (float)((int)((unsigned)q[0] << 8 | (unsigned)q[1] << 16 | (unsigned)q[2] << 24) >> 8) * (1.0f / 8388608.0f);
                        }
                        break;
                case 6:
                        for (long i = 0; i < frames; i++)
                        {
                                const unsigned char *q = p + i * stride;

                                d[i] = (float)((int)((unsigned)q[2] << 8 | (unsigned)q[1] << 16 | (unsigned)q[0] << 24) >> 8) * (1.0f / 8388608.0f);
                        }
                        break;
                default:
                        for (long i = 0; i < frames; i++)
                        {
                                unsigned bits;

                                __builtin_memcpy(&bits, p + i * stride, 4);
                                if (format == 8)
                                        bits = al_swap(bits);
                                d[i] = (float)((int)(bits << 8) >> 8) * (1.0f / 8388608.0f);
                        }
                        break;
                }
        }
}

static void al_new_decode(int format, const unsigned char *src, long frames, long channels, float *const *dst)
{
        long bytes = al_old_width[format], done = 0;

        if (format == 1 || format == 2)
        {
                f32_planes_from_law(dst, src, (unsigned long)frames, (unsigned long)channels, format == 2);
                return;
        }
        while (done < frames)
        {
                long take = 4096 / channels, c;
                float *planes[8];

                if (take > frames - done)
                        take = frames - done;
                for (c = 0; c < channels; c++)
                        planes[c] = dst[c] + done;
                pcm_widen(al_wide_lanes, src + done * channels * bytes, (unsigned long)(take * channels), (unsigned long)al_new_kind[format]);
                if (format == 9)
                        f32_planes_from_f32(planes, (const unsigned char *)al_wide_lanes, (unsigned long)take, (unsigned long)channels);
                else
                        f32_planes_from_s32(planes, (const unsigned char *)al_wide_lanes, (unsigned long)take, (unsigned long)channels);
                done += take;
        }
}

static const char *const al_widen_names[10] = {"u8", "A-law", "mu-law", "s16 big-endian", "s32 big-endian", "s24 little-endian", "s24 big-endian", "s24_32 little-endian", "s24_32 big-endian", "f32 big-endian"};
static int al_widen_format;
static long al_widen_channels;

static p64 al_widen_run(p8 assembly, positive shape, positive reps)
{
        float *planes[8];
        p64 start = get_cpu_time();

        (void)shape;
        for (int c = 0; c < 8; c++)
                planes[c] = al_decode_out[c];
        for (positive r = 0; r < reps; r++)
        {
                const unsigned char *src = al_decode_in + (r & 3) * 8192 + 4 * (r & 1);

                if (assembly)
                        al_new_decode(al_widen_format, src, 512, al_widen_channels, planes);
                else
                        al_old_decode(al_widen_format, src, 512, al_widen_channels, planes);
        }
        return get_cpu_time() - start;
}

static fn al_widen_bench(string_address tier)
{
        for (int byte = 0; byte < 256; byte++)
        {
                int a = byte ^ 0x55, t, segment, u = ~byte & 0xff, value;

                t = (a & 0x0f) << 4;
                segment = (a & 0x70) >> 4;
                if (segment == 0)
                        t += 8;
                else
                {
                        t += 0x108;
                        if (segment > 1)
                                t <<= segment - 1;
                }
                al_law[0][byte] = (float)((a & 0x80) ? t : -t) * (1.0f / 32768.0f);
                t = ((u & 0x0f) << 3) + 0x84;
                t <<= (u & 0x70) >> 4;
                value = (u & 0x80) ? 0x84 - t : t - 0x84;
                al_law[1][byte] = (float)value * (1.0f / 32768.0f);
        }
        for (int format = 0; format < 10; format++)
                for (long channels = 2; channels <= 6; channels += 4)
                {
                        positive reps = (1u << 20) / 512 / (positive)channels * 2 + 1, ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        al_widen_format = format;
                        al_widen_channels = channels;
                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, al_widen_run(false, 0, reps), assembly, al_widen_run(true, 0, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "decode of %s, %p channels of 512 frames, %s against C -O2 as built: paired median ASM/C ", al_widen_names[format], (positive)channels, tier);
                        al_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a call ASM ");
                        al_hundredths((positive)(best_a * 100 / reps));
                        string_format(log, ", C ");
                        al_hundredths((positive)(best_c * 100 / reps));
                        log("\n", 1);
                }
}

static fn al_mix2_rows_run(string_address tier, p8 wide)
{
        for (positive shape = 0; shape < 3; shape++)
                for (p32 bar = 0; bar < 2; bar++)
                {
                        positive reps = (1u << 20) / 256 / al_mix2_channels[shape] + 1, ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        al_mix2_bar = bar == 0 ? 0 : wide ? 1 : 2;
                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, al_mix2_run(false, shape, reps), assembly, al_mix2_run(true, shape, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "f32_rows_mix2 %p channels of 256 frames, %s against C %s: paired median ASM/C ", al_mix2_channels[shape], tier,
                                      bar == 0 ? "-O2 as built" : "-O3");
                        al_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a call ASM ");
                        al_hundredths((positive)(best_a * 100 / reps));
                        string_format(log, ", C ");
                        al_hundredths((positive)(best_c * 100 / reps));
                        log("\n", 1);
                }
}

static fn al_encode_rows(string_address tier, p8 wide)
{
        for (positive shape = 0; shape < sizeof(al_encode_shapes) / sizeof(al_encode_shapes[0]); shape++)
                for (p32 bar = 0; bar < 2; bar++)
                {
                        positive reps = (1u << 22) / al_encode_shapes[shape].frames + 1, ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        al_encode_bar = bar == 0 ? 0 : wide ? 1 : 2;
                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, al_encode_run(false, shape, reps), assembly, al_encode_run(true, shape, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "%s %s, %s against C %s: paired median ASM/C ", al_encode_shapes[shape].format < 4 ? "f32_pair_to_" : "f32_pair_clip",
                                      al_encode_shapes[shape].name, tier, bar == 0 ? "-O2 as built" : "-O3");
                        al_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a call ASM ");
                        al_hundredths((positive)(best_a * 100 / reps));
                        string_format(log, ", C ");
                        al_hundredths((positive)(best_c * 100 / reps));
                        log("\n", 1);
                }
}

//      The gain loop's rows on the tier set now. bars: the C as built, -O3 for the tier (x86-64-v3, V), and, on a machine
//      with AVX-512 on its AVX-512 tier, -O3 at 512 bits.
static p8 al_has512;

/*
        The card's 24 bits in three bytes, stereo, 512 frames: the pair into 24-in-32 lanes and pcm_pack24 against the loop mix.c had
        (a rounded integer a sample, three bytes stored one at a time), built as the service is (-O2).
*/
static unsigned al_pack_lanes[1024];

__attribute__((noinline, noclone, optimize("O2"))) static void al_pack_old(unsigned char *dst, const float *left, const float *right, long frames)
{
        for (long i = 0; i < frames; i++)
                for (long c = 0; c < 2; c++)
                {
                        float v = c == 0 ? left[i] : right[i];
                        double d = (double)v * 8388608.0, r;
                        long long bits;
                        int x;
                        unsigned char *q = dst + (i * 2 + c) * 3;

                        if (v != v)
                                x = 0;
                        else
                        {
                                d = d > 8388607.0 ? 8388607.0 : d;
                                d = d < -8388608.0 ? -8388608.0 : d;
                                r = d + 6755399441055744.0;
                                __builtin_memcpy(&bits, &r, 8);
                                x = (int)(bits & 0xffffffffll);
                        }
                        q[0] = (unsigned char)x;
                        q[1] = (unsigned char)(x >> 8);
                        q[2] = (unsigned char)(x >> 16);
                }
}

static p64 al_pack_run(p8 assembly, positive shape, positive reps)
{
        p64 start = get_cpu_time();

        (void)shape;
        for (positive r = 0; r < reps; r++)
        {
                if (assembly)
                {
                        f32_pair_to_s24_32((unsigned char *)al_pack_lanes, al_encode_left, al_encode_right, 512);
                        pcm_pack24((unsigned char *)al_encode_out, al_pack_lanes, 1024);
                }
                else
                        al_pack_old((unsigned char *)al_encode_out, al_encode_left, al_encode_right, 512);
        }
        return get_cpu_time() - start;
}

static fn al_pack_bench(string_address tier)
{
        positive reps = (1u << 20) / 512 + 1, ratios[TRIES];
        p64 best_c = ~(p64)0, best_a = ~(p64)0;

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 former, assembly;

                BENCH_ALTERNATE(trial, former, al_pack_run(false, 0, reps), assembly, al_pack_run(true, 0, reps));
                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                best_c = former < best_c ? former : best_c;
                best_a = assembly < best_a ? assembly : best_a;
        }
        order(ratios, TRIES);
        string_format(log, "encode of 24 bits in three bytes, 2 channels of 512 frames, %s against C -O2 as built: paired median ASM/C ", tier);
        al_hundredths(ratios[TRIES / 2]);
        string_format(log, "%%; best ticks a call ASM ");
        al_hundredths((positive)(best_a * 100 / reps));
        string_format(log, ", C ");
        al_hundredths((positive)(best_c * 100 / reps));
        log("\n", 1);
}

/*
        The pulse service's five loops (memory_text_clean, memory_key_printable, memory_reverse_units, memory_s16_to_u8,
        memory_window_peaks), each against the C of SHARED_audio_loops (the loops pulse.c has) at the sizes the service calls them with:
        a name of 32 and of 120 bytes of ASCII and of 64 bytes with UTF-8 in it, a key of 16 and 63 bytes, a block of 512 frames of two
        channels in 2, 3 and 4 byte units, the 16-bit block to 8 bits, and a peak window of 1024 frames and of 7 over 512 frames.
*/
static unsigned char al_pulse_from[2048] __attribute__((aligned(64))), al_pulse_to[2048] __attribute__((aligned(64)));
static unsigned char al_pulse_work[2048] __attribute__((aligned(64)));
static float al_pulse_left[1024] __attribute__((aligned(64))), al_pulse_right[1024] __attribute__((aligned(64)));
static float al_pulse_out0[1024] __attribute__((aligned(64))), al_pulse_out1[1024] __attribute__((aligned(64)));
static float al_pulse_state[2];
static long al_pulse_count_c;
static positive al_pulse_count_a;

static const struct
{
        string_address name;
        positive kind, size, extra;
} al_pulse_shapes[] = {
    {"memory_text_clean, a name of 32 bytes of ASCII", 0, 32, 0},
    {"memory_text_clean, a name of 120 bytes of ASCII", 0, 120, 0},
    {"memory_text_clean, a name of 64 bytes with UTF-8 in it", 0, 64, 1},
    {"memory_key_printable, a key of 16 bytes", 1, 16, 0},
    {"memory_key_printable, a key of 63 bytes", 1, 63, 0},
    {"memory_reverse_units, 1024 units of 2 bytes", 2, 1024, 2},
    {"memory_reverse_units, 1024 units of 3 bytes", 2, 1024, 3},
    {"memory_reverse_units, 1024 units of 4 bytes", 2, 1024, 4},
    {"memory_s16_to_u8, 1024 samples", 3, 1024, 0},
    {"memory_window_peaks, 512 frames, a window of 1024", 4, 512, 1024},
    {"memory_window_peaks, 512 frames, a window of 7", 4, 512, 7},
};

__attribute__((noinline, noclone, optimize("O2"))) static long al_pulse_old(positive shape)
{
        positive kind = al_pulse_shapes[shape].kind, size = al_pulse_shapes[shape].size, extra = al_pulse_shapes[shape].extra;

        if (kind == 0)
                return audio_c_text_clean(al_pulse_to, al_pulse_from + (extra ? 1024 : 0), (long)size);
        if (kind == 1)
                return audio_c_key_printable(al_pulse_from + 1536, (long)size);
        if (kind == 2)
        {
                audio_c_reverse_units(al_pulse_work, (long)size, (long)extra);
                return 0;
        }
        if (kind == 3)
        {
                audio_c_s16_to_u8(al_pulse_to, al_pulse_from, (long)size);
                return 0;
        }
        return audio_c_window_peaks(al_pulse_out0, al_pulse_out1, al_pulse_left, al_pulse_right, (long)size, al_pulse_state, &al_pulse_count_c, (long)extra, 0);
}

static long al_pulse_new(positive shape)
{
        positive kind = al_pulse_shapes[shape].kind, size = al_pulse_shapes[shape].size, extra = al_pulse_shapes[shape].extra;

        if (kind == 0)
                return (long)memory_text_clean(al_pulse_to, al_pulse_from + (extra ? 1024 : 0), size);
        if (kind == 1)
                return (long)memory_key_printable(al_pulse_from + 1536, size);
        if (kind == 2)
        {
                memory_reverse_units(al_pulse_work, size, extra);
                return 0;
        }
        if (kind == 3)
        {
                memory_s16_to_u8(al_pulse_to, al_pulse_from, size);
                return 0;
        }
        return (long)memory_window_peaks(al_pulse_out0, al_pulse_out1, al_pulse_left, al_pulse_right, size, al_pulse_state, &al_pulse_count_a, extra, 0);
}

static p64 al_pulse_run(p8 assembly, positive shape, positive reps)
{
        p64 start = get_cpu_time();
        long sink = 0;

        for (positive r = 0; r < reps; r++)
                sink += assembly ? al_pulse_new(shape) : al_pulse_old(shape);
        al_pulse_count_c += sink & 1;
        return get_cpu_time() - start;
}

static fn al_pulse_bench(string_address tier)
{
        positive state = 88172645463325252ull;

        for (positive i = 0; i < 2048; i++)
        {
                XORSHIFT64(state);
                al_pulse_from[i] = (unsigned char)(0x20 + (state >> 33) % 0x5f);
                al_pulse_work[i] = (unsigned char)(state >> 41);
        }
        // a name with UTF-8 at 1024: the ASCII, and a two byte, a three byte and a four byte character every twelve bytes
        for (positive i = 0; i < 64; i++)
                al_pulse_from[1024 + i] = al_pulse_from[i];
        for (positive i = 0; i + 12 < 64; i += 12)
        {
                unsigned char *q = al_pulse_from + 1024 + i;

                q[4] = 0xc3, q[5] = 0xa9;
                q[6] = 0xe2, q[7] = 0x82, q[8] = 0xac;
                q[9] = 0xf0, q[10] = 0x9f, q[11] = 0x98, q[12] = 0x80;
        }
        // a key: 0x21 to 0x7e
        for (positive i = 0; i < 64; i++)
                al_pulse_from[1536 + i] = (unsigned char)(0x21 + al_pulse_from[i] % 0x5e);
        for (positive i = 0; i < 1024; i++)
        {
                XORSHIFT64(state);
                al_pulse_left[i] = (f32)(int)(state >> 40) / 8388608.0f - 1.0f;
                al_pulse_right[i] = (f32)(int)(state >> 16 & 0xffffff) / 8388608.0f - 1.0f;
        }
        for (positive shape = 0; shape < sizeof(al_pulse_shapes) / sizeof(al_pulse_shapes[0]); shape++)
        {
                positive reps = (1u << 24) / (al_pulse_shapes[shape].size * (al_pulse_shapes[shape].kind >= 2 ? 3 : 1) + 64) + 1, ratios[TRIES];
                p64 best_c = ~(p64)0, best_a = ~(p64)0;

                for (positive trial = 0; trial < TRIES; trial++)
                {
                        p64 former, assembly;

                        BENCH_ALTERNATE(trial, former, al_pulse_run(false, shape, reps), assembly, al_pulse_run(true, shape, reps));
                        ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                        best_c = former < best_c ? former : best_c;
                        best_a = assembly < best_a ? assembly : best_a;
                }
                order(ratios, TRIES);
                string_format(log, "%s, %s against C -O2 as built: paired median ASM/C ", al_pulse_shapes[shape].name, tier);
                al_hundredths(ratios[TRIES / 2]);
                string_format(log, "%%; best ticks a call ASM ");
                al_hundredths((positive)(best_a * 100 / reps));
                string_format(log, ", C ");
                al_hundredths((positive)(best_c * 100 / reps));
                log("\n", 1);
        }
}

static fn al_scale_rows(string_address tier, p8 wide, p8 has512_tier)
{
        for (positive shape = 0; shape < sizeof(al_scale_shapes) / sizeof(al_scale_shapes[0]); shape++)
                for (p32 bar = 0; bar < 3; bar++)
                {
                        positive reps = (1u << 24) / al_scale_shapes[shape].count + 1, ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        if (bar == 2 && !has512_tier)
                                continue;
                        al_scale_bar = bar == 0 ? 0 : bar == 1 ? (wide ? 1 : 2) : 3;
                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, al_scale_run(false, shape, reps), assembly, al_scale_run(true, shape, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "f32_scale%s_peak %s, %s against C %s: paired median ASM/C ", al_scale_shapes[shape].add ? "_add" : "",
                                      al_scale_shapes[shape].name, tier, bar == 0 ? "-O2 as built" : bar == 1 ? "-O3" : "-O3 at 512 bits");
                        al_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a call ASM ");
                        al_hundredths((positive)(best_a * 100 / reps));
                        string_format(log, ", C ");
                        al_hundredths((positive)(best_c * 100 / reps));
                        log("\n", 1);
                }
}

#if X64
static fn al_force(p32 tier)
{
        static p8 wide2 = 3, fused;

        if (wide2 == 3)
                wide2 = cpu_has_avx2, fused = cpu_has_fma, al_has512 = cpu_has_avx512;
        cpu_has_avx2 = tier <= 1 ? wide2 : 0;
        cpu_has_fma = tier <= 1 ? fused : 0;
        cpu_has_avx512 = tier == 0 ? al_has512 : 0;
        al_wide = tier <= 1;
}
#else
static fn al_force(p32 tier)
{
        (void)tier;
}
#endif

#define AL_NAMED(id, shape, tier, assembly, bar)                                                         \
        static fn al_named_##id(void)                                                                    \
        {                                                                                                \
                al_force(tier);                                                                          \
                al_bar = bar;                                                                            \
                al_run(assembly, shape, (1u << 24) / al_shapes[shape].count);                             \
        }
AL_NAMED(poly_44k_ymm, 0, 1, true, 0)
AL_NAMED(poly_44k_ymm_c, 0, 1, false, 0)
AL_NAMED(poly_44k_xmm, 0, 2, true, 0)
AL_NAMED(poly_44k_xmm_c, 0, 2, false, 0)
AL_NAMED(poly_96k_ymm, 2, 1, true, 0)
AL_NAMED(poly_96k_ymm_c, 2, 1, false, 0)

static fn al_named_nothing(void)
{
}

static bench_work al_named(string_address name)
{
        BENCH_NAME(name, "nothing", al_named_nothing);
        BENCH_NAME(name, "poly-44k-ymm", al_named_poly_44k_ymm);
        BENCH_NAME(name, "poly-44k-ymm-c", al_named_poly_44k_ymm_c);
        BENCH_NAME(name, "poly-44k-xmm", al_named_poly_44k_xmm);
        BENCH_NAME(name, "poly-44k-xmm-c", al_named_poly_44k_xmm_c);
        BENCH_NAME(name, "poly-96k-ymm", al_named_poly_96k_ymm);
        BENCH_NAME(name, "poly-96k-ymm-c", al_named_poly_96k_ymm_c);
        return null;
}

b32 main(void)
{
        p64 state = 0x243f6a8885a308d3ull;

        al_out[0] = memory(1024 * 4), al_out[1] = memory(1024 * 4);
        for (positive v = 0; v < AL_VOICES; v++)
        {
                al_table[v] = memory((513 * 512 + 64) * 4), al_in[v][0] = memory(AL_INPUT * 4), al_in[v][1] = memory(AL_INPUT * 4);
                if (!al_table[v] || !al_in[v][0] || !al_in[v][1] || !al_out[0] || !al_out[1])
                        return 1;
                for (positive i = 0; i < 513 * 512; i++)
                {
                        XORSHIFT64(state);
                        al_table[v][i] = (f32)(int)(state >> 40) / 8388608.0f * 0.05f - 0.025f;
                }
                for (positive i = 0; i < AL_INPUT; i++)
                {
                        XORSHIFT64(state);
                        al_in[v][0][i] = (f32)(int)(state >> 40) / 8388608.0f - 1.0f;
                        al_in[v][1][i] = (f32)(int)(state >> 20 & 0xffffff) / 8388608.0f - 1.0f;
                }
        }
        al_scale_bus = memory((4 * 1024 + 64) * 4), al_scale_in = memory((4 * 1024 + 64) * 4);
        al_mix2_rows = memory(8 * 512 * 4), al_mix2_out0 = memory(1024 * 4), al_mix2_out1 = memory(1024 * 4);
        if (!al_mix2_rows || !al_mix2_out0 || !al_mix2_out1)
                return 1;
        for (positive i = 0; i < 8 * 512; i++)
        {
                XORSHIFT64(state);
                al_mix2_rows[i] = (f32)(int)(state >> 40) / 8388608.0f - 1.0f;
        }
        al_encode_left = memory(1024 * 4), al_encode_right = memory(1024 * 4), al_encode_keep_left = memory(1024 * 4), al_encode_keep_right = memory(1024 * 4);
        al_encode_out = memory(1024 * 8);
        if (!al_encode_left || !al_encode_right || !al_encode_keep_left || !al_encode_keep_right || !al_encode_out)
                return 1;
        for (positive i = 0; i < 1024; i++)
        {
                XORSHIFT64(state);
                al_encode_left[i] = (f32)(int)(state >> 40) / 8388608.0f - 1.0f;
                al_encode_right[i] = (f32)(int)(state >> 16 & 0xffffff) / 8388608.0f - 1.0f;
        }
        al_decode_in = memory(4 * 8192 + 64);
        if (!al_scale_bus || !al_scale_in || !al_decode_in)
                return 1;
        for (positive i = 0; i < 4 * 8192; i++)
        {
                XORSHIFT64(state);
                al_decode_in[i] = (p8)(state >> 24);
        }
        for (positive c = 0; c < 8; c++)
        {
                al_decode_out[c] = memory(4096 * 4);
                if (!al_decode_out[c])
                        return 1;
        }
        for (positive i = 0; i < 4 * 1024 + 64; i++)
        {
                XORSHIFT64(state);
                al_scale_in[i] = (f32)(int)(state >> 40) / 8388608.0f - 1.0f;
                al_scale_bus[i] = (f32)(int)(state >> 16 & 0xffffff) / 8388608.0f - 1.0f;
        }
        BENCH_RUN_NAMED(al_named);
#if X64
        p8 wide2 = cpu_has_avx2, fused = cpu_has_fma, wide512 = cpu_has_avx512;

        al_has512 = wide512;
        for (p32 tier = 0; tier < 3; tier++)
        {
                if ((tier == 0 && !wide512) || (tier == 1 && !(wide2 && fused)))
                        continue;
                al_force(tier);
                if (tier)
                {
                        al_shapes_run(tier == 1 ? "avx2 fma" : "sse2");
                        al_rows_bench(tier == 1 ? "avx2 fma" : "sse2");
                        al_widen_bench(tier == 1 ? "avx2" : "sse2");
                        al_pack_bench(tier == 1 ? "avx2" : "sse2");
                        al_pulse_bench(tier == 1 ? "avx2" : "sse2");
                }
                al_scale_rows(tier == 0 ? "avx512" : tier == 1 ? "avx2" : "sse2", tier <= 1, tier == 0);
                if (tier)
                {
                        al_decode_rows(tier == 1 ? "avx2" : "sse2", tier == 1);
                        al_encode_rows(tier == 1 ? "avx2" : "sse2", tier == 1);
                        al_mix2_rows_run(tier == 1 ? "avx2" : "sse2", tier == 1);
                }
        }
        cpu_has_avx2 = wide2, cpu_has_fma = fused, cpu_has_avx512 = wide512;
#elif RISCV64
        cpu_has_vector = 0;
        resample_polyphase2_f32(al_out[0], al_out[1], al_in[0][0], al_in[0][1], al_table[0], 64, 1, 0, 0, 1);
        p8 vector = cpu_has_vector;

        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
                al_shapes_run(tier == 0 ? "V" : "no V");
                al_rows_bench(tier == 0 ? "V" : "no V");
                al_widen_bench(tier == 0 ? "V" : "no V");
                al_pack_bench(tier == 0 ? "V" : "no V");
                al_pulse_bench(tier == 0 ? "V" : "no V");
                al_scale_rows(tier == 0 ? "V" : "no V", true, false);
                al_decode_rows(tier == 0 ? "V" : "no V", true);
                al_encode_rows(tier == 0 ? "V" : "no V", true);
                al_mix2_rows_run(tier == 0 ? "V" : "no V", true);
        }
        cpu_has_vector = vector;
#else
        al_shapes_run("neon");
        al_rows_bench("neon");
        al_widen_bench("neon");
        al_pack_bench("neon");
        al_pulse_bench("neon");
        al_scale_rows("neon", true, false);
        al_decode_rows("neon", true);
        al_encode_rows("neon", true);
        al_mix2_rows_run("neon", true);
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_audiolib */

#ifdef BENCH_dbus
/*
        The session bus's engine (src/dbus/dbus.c) with lib.c under it, as the sessionbus tool has it, in ticks: what the
        bus does with a message and no socket (the socket's are test/dbus_bench.c's). The engine's rows: a ReadOne the bus
        answers itself, one at a time and 64 in one read; a call from one client to another and its answer (two messages
        routed); a signal to N clients whose rule it matches; the portal's SettingChanged to N listeners; a message of a
        dictionary of 32 strings parsed whole. Then the loops: each C loop the engine has against the lib.c routine that
        would take its place, at the sizes a bus sees (names of 10 to 40 bytes, bodies to 4 KiB).

            sh test/run bench dbus           (or by hand: the freestanding flags of bench_target, -DBENCH_dbus)
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure
#include "../../src/dbus/dbus.c"

#define BD_TRIES 9

static volatile positive bd_sink;

static void *bd_alloc(void *context, long bytes)
{
        long got = (long)system_call_6(syscall(mmap), 0, (positive)bytes, 3, 0x22, (positive)-1, 0);

        (void)context;
        return got < 0 && got > -4096 ? 0 : (void *)got;
}

static void bd_release(void *context, void *block, long bytes)
{
        (void)context;
        system_call_2(syscall(munmap), (positive)block, (positive)bytes);
}

static long bd_now(void *context)
{
        (void)context;
        return 1;
}

static void bd_random(void *context, unsigned char *bytes, long count)
{
        (void)context;
        for (long at = 0; at < count; at++)
                bytes[at] = (unsigned char)(at * 37 + 11);
}

static const struct dbus_host bd_host = {0, bd_alloc, bd_release, bd_now, bd_random, 0, 0, 0, 0};
static string_address bd_only; // a row to run alone (call, readone, setting), or every row

static p8 bd_want(string_address row)
{
        return !bd_only[0] || string_equals(bd_only, "engine") || string_equals(bd_only, row);
}
static unsigned long bd_serial;

static void bd_feed(long slot, const unsigned char *bytes, long n)
{
        while (n > 0)
        {
                long room, take;
                unsigned char *to = dbus_in_room(slot, &room);

                if (!to || room <= 0)
                        return;
                take = n < room ? n : room;
                memory_copy(to, bytes, (positive)take);
                dbus_in_done(slot, take);
                bytes += take;
                n -= take;
        }
}

static void bd_drain(long slot)
{
        const unsigned char *bytes;

        dbus_sent(slot, dbus_out(slot, &bytes));
}

// A message of up to two strings, or with the body the caller writes after (body 0 to end it here); its length in into.
static long bd_message(unsigned char *into, long room, unsigned type, unsigned flags, const char *path, const char *interface, const char *member,
                       const char *destination, const char *signature, const char *a, const char *b, unsigned long reply)
{
        struct dbus_out w = {into, 0, room, 0};
        struct dbus_fields f = {path, interface, member, "", destination, "", signature, reply, reply != 0};
        long body;

        dbus_o_header(&w, type, flags, ++bd_serial, &f);
        body = w.length;
        if (a)
                dbus_o_string(&w, a);
        if (b)
                dbus_o_string(&w, b);
        dbus_o_end(&w, body);
        return w.bad ? -1 : w.length;
}

static long bd_connect(void)
{
        static const unsigned char auth[] = "\0AUTH EXTERNAL\r\nDATA\r\nBEGIN\r\n";
        unsigned char hello[256];
        long slot = dbus_connect(1, 0), n;

        bd_feed(slot, auth, sizeof auth - 1);
        n = bd_message(hello, sizeof hello, DBUS_CALL, 0, "/org/freedesktop/DBus", DBUS_NAME_BUS, "Hello", DBUS_NAME_BUS, "", 0, 0, 0);
        bd_feed(slot, hello, n);
        bd_drain(slot);
        return slot;
}

static void bd_rule(long slot, const char *rule)
{
        unsigned char call[1024];
        long n = bd_message(call, sizeof call, DBUS_CALL, 0, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", DBUS_NAME_BUS, "s", rule, 0, 0);

        bd_feed(slot, call, n);
        bd_drain(slot);
}

// RequestName of a name, no flags; its answer read and dropped.
static void bd_request(long slot, const char *name)
{
        unsigned char call[256];
        struct dbus_out w = {call, 0, sizeof call, 0};
        struct dbus_fields f = {"/org/freedesktop/DBus", DBUS_NAME_BUS, "RequestName", "", DBUS_NAME_BUS, "", "su", 0, 0};
        long body;

        dbus_o_header(&w, DBUS_CALL, 0, ++bd_serial, &f);
        body = w.length;
        dbus_o_string(&w, name);
        dbus_o_u32(&w, 0);
        dbus_o_end(&w, body);
        bd_feed(slot, call, w.length);
        bd_drain(slot);
}

static unsigned char bd_one[512], bd_many[64 * 512], bd_echo[512], bd_answer[512], bd_signal[512], bd_dict[8192];
static long bd_one_n, bd_many_n, bd_echo_n, bd_answer_n, bd_signal_n, bd_dict_n, bd_a, bd_b, bd_listeners[100], bd_count;

static fn bd_readone(void)
{
        for (int k = 0; k < 1000; k++)
        {
                bd_feed(bd_a, bd_one, bd_one_n);
                bd_drain(bd_a);
        }
}

static fn bd_pipelined(void)
{
        for (int k = 0; k < 100; k++)
        {
                bd_feed(bd_a, bd_many, bd_many_n);
                bd_drain(bd_a);
        }
}

static fn bd_call(void)
{
        for (int k = 0; k < 1000; k++)
        {
                bd_feed(bd_a, bd_echo, bd_echo_n);
                bd_drain(bd_b);
                bd_feed(bd_b, bd_answer, bd_answer_n);
                bd_drain(bd_a);
        }
}

static fn bd_broadcast(void)
{
        for (int k = 0; k < 100; k++)
        {
                bd_feed(bd_a, bd_signal, bd_signal_n);
                for (long l = 0; l < bd_count; l++)
                        bd_drain(bd_listeners[l]);
        }
}

static fn bd_setting(void)
{
        for (int k = 0; k < 100; k++)
        {
                dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1 + (k & 1), 0, 0);
                for (long l = 0; l < bd_count; l++)
                        bd_drain(bd_listeners[l]);
        }
}

static fn bd_parse(void)
{
        struct dbus_message m;

        for (int k = 0; k < 1000; k++)
                bd_sink += (positive)dbus_parse(bd_dict, bd_dict_n, &m);
}

static fn bd_engine(void)
{
        long reply;

        bd_count = bd_many_n = 0; // a run of its own each time: its own listeners and its own batch
        dbus_start(&bd_host);
        bd_a = bd_connect();
        bd_b = bd_connect();
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1, 0, 0);
        bd_one_n = bd_message(bd_one, sizeof bd_one, DBUS_CALL, 0, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadOne", DBUS_NAME_PORTAL, "ss",
                              "org.freedesktop.appearance", "color-scheme", 0);
        for (int k = 0; k < 64; k++)
                bd_many_n += bd_message(bd_many + bd_many_n, (long)sizeof bd_many - bd_many_n, DBUS_CALL, 0, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings",
                                        "ReadOne", DBUS_NAME_PORTAL, "ss", "org.freedesktop.appearance", "color-scheme", 0);
        bd_request(bd_b, "org.example.Echo");
        bd_echo_n = bd_message(bd_echo, sizeof bd_echo, DBUS_CALL, 0, "/org/example", "org.example.Echo", "Echo", "org.example.Echo", "s", "a question", 0, 0);
        reply = (long)bd_serial;
        bd_answer_n = bd_message(bd_answer, sizeof bd_answer, DBUS_RETURN, DBUS_FLAG_NO_REPLY, "", "", "", dbus.client[bd_a]->unique, "s", "an answer", 0,
                                 (unsigned long)reply);
        bd_signal_n = bd_message(bd_signal, sizeof bd_signal, DBUS_SIGNAL, 0, "/org/example", "org.example.Signals", "Changed", "", "s", "payload", 0, 0);
        {
                struct dbus_out w = {bd_dict, 0, sizeof bd_dict, 0};
                struct dbus_fields f = {"/org/example", "org.example.Dict", "Big", "", "", "", "a{sv}", 0, 0};
                long body, length_at, first;
                char key[16];

                dbus_o_header(&w, DBUS_SIGNAL, 0, ++bd_serial, &f);
                body = w.length;
                dbus_o_u32(&w, 0);
                length_at = w.length - 4;
                dbus_o_pad(&w, 8);
                first = w.length;
                for (int k = 0; k < 32; k++)
                {
                        dbus_o_pad(&w, 8);
                        key[0] = 'k', key[1] = (char)('a' + k % 26), key[2] = (char)('0' + k / 26), key[3] = 0;
                        dbus_o_string(&w, key);
                        dbus_o_sig(&w, "s");
                        dbus_o_string(&w, "a value of a setting, as text, some forty bytes");
                }
                dbus_o_patch32(&w, length_at, (unsigned long)(w.length - first));
                dbus_o_end(&w, body);
                bd_dict_n = w.length;
        }
        if (bd_want("readone"))
        {
                bench_report("ReadOne, one at a time  ", bd_readone, BD_TRIES, 1000, "call");
                bench_report("ReadOne, 64 in one read ", bd_pipelined, BD_TRIES, 6400, "call");
        }
        if (bd_want("call"))
                bench_report("a call to a client, and its answer", bd_call, BD_TRIES, 1000, "call and answer");
        if (bd_want("parse"))
                bench_report("a dict of 32 strings parsed", bd_parse, BD_TRIES, 1000 * (positive)bd_dict_n, "byte");
        for (int sizes = 0; sizes < 3 && bd_want("setting"); sizes++)
        {
                long want = sizes == 0 ? 1 : sizes == 1 ? 10 : 100;

                while (bd_count < want)
                {
                        bd_listeners[bd_count] = bd_connect();
                        bd_rule(bd_listeners[bd_count], "type='signal',interface='org.example.Signals'");
                        bd_rule(bd_listeners[bd_count], "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Settings',member='SettingChanged'");
                        bd_count++;
                }
                string_format(log, "  %p listeners:\n", (positive)want);
                bench_report("  a signal to them      ", bd_broadcast, BD_TRIES, 100, "signal");
                bench_report("  SettingChanged to them", bd_setting, BD_TRIES, 100, "change");
        }
        if (string_equals(bd_only, "full")) // a run of its own: after the listeners the table has room for 26 clients more
        {
                // the names as many as one uid's clients may own (96 of 8, or what the table takes), then a call to a name owned
                // after all of them: what every lookup costs once a client has filled the table
                long names = 0;

                for (long k = 0; k < 96; k++)
                {
                        long slot = bd_connect();

                        for (int n = 0; n < DBUS_NAMES_PER; n++)
                        {
                                char name[40] = "org.example.F000.N0";

                                name[13] = (char)('0' + k / 100), name[14] = (char)('0' + k / 10 % 10), name[15] = (char)('0' + k % 10);
                                name[18] = (char)('0' + n);
                                bd_request(slot, name);
                                names += dbus_name_find(name) != 0;
                        }
                }
                bd_request(bd_b, "org.example.Late");
                bd_echo_n = bd_message(bd_echo, sizeof bd_echo, DBUS_CALL, 0, "/org/example", "org.example.Echo", "Echo", "org.example.Late", "s", "a question", 0,
                                       0);
                reply = (long)bd_serial;
                bd_answer_n = bd_message(bd_answer, sizeof bd_answer, DBUS_RETURN, DBUS_FLAG_NO_REPLY, "", "", "", dbus.client[bd_a]->unique, "s", "an answer", 0,
                                         (unsigned long)reply);
                string_format(log, "  %p names in the table:\n", (positive)names + 2);
                bench_report("  a call to the last name, and its answer", bd_call, BD_TRIES, 1000, "call and answer");
        }
        dbus_stop();
}

/* ---- the loops ---- */

static p8 bd_bytes[8192] __attribute__((aligned(64)));
static p8 bd_into[8192] __attribute__((aligned(64)));
static const char *bd_names[] = {"org.freedesktop.portal.Settings", "org.freedesktop.portal.Desktop", "org.freedesktop.appearance", "color-scheme",
                                 "SettingChanged", "/org/freedesktop/portal/desktop", ":1.42", "org.gnome.desktop.interface"};

// The engine's C as it was before lib.c took these (dbus_bytes and dbus_utf8_walk are still its own below the thresholds).
__attribute__((noinline)) static long bd_former_length(const char *s)
{
        long n = 0;

        while (s[n])
                n++;
        return n;
}

__attribute__((noinline)) static int bd_former_equal(const char *a, const char *b)
{
        long n = 0;

        while (a[n] && a[n] == b[n])
                n++;
        return a[n] == b[n];
}

static p64 bd_run_copy(p8 library, positive n, positive rounds)
{
        p64 start = get_cpu_time();

        while (rounds--)
        {
                if (library)
                        memory_copy(bd_into + (rounds & 7), bd_bytes + (rounds & 3), n);
                else
                        dbus_bytes(bd_into + (rounds & 7), bd_bytes + (rounds & 3), (long)n);
                bd_sink += bd_into[n / 2];
        }
        return get_cpu_time() - start;
}

static p64 bd_run_utf8(p8 library, positive n, positive rounds)
{
        p64 start = get_cpu_time();

        while (rounds--)
        {
                if (library)
                        bd_sink += memory_utf8_valid_span(bd_bytes, n).x == n && string_length((const_string)bd_bytes) == n;
                else
                        bd_sink += (positive)dbus_utf8_walk(bd_bytes, (long)n);
        }
        return get_cpu_time() - start;
}

static p64 bd_run_length(p8 library, positive which, positive rounds)
{
        p64 start = get_cpu_time();

        while (rounds--)
        {
                const char *s = bd_names[(which + rounds) & 7];

                bd_sink += library ? string_length((const_string)s) : (positive)bd_former_length(s);
        }
        return get_cpu_time() - start;
}

static p64 bd_run_equal(p8 library, positive which, positive rounds)
{
        p64 start = get_cpu_time();

        while (rounds--)
        {
                const char *a = bd_names[(which + rounds) & 7], *b = bd_names[(which + rounds + (rounds & 1)) & 7];

                bd_sink += library ? string_compare((string_address)a, (string_address)b) == 0 : (positive)bd_former_equal(a, b);
        }
        return get_cpu_time() - start;
}

/*
        The engine's answers with lib.c under it, held to its C: dbus_utf8 (lib.c's validator from DBUS_UTF8_LIBRARY bytes)
        against dbus_utf8_walk over texts of 0 to 80 bytes, most valid and of every length of sequence, some with a byte
        changed (a bad lead, a cut, an overlong form, a surrogate, past U+10FFFF, a NUL inside); dbus_copy (memory_copy
        from DBUS_COPY_LIBRARY) moving a block down onto itself, as the queue does; dbus_length and dbus_equal against the
        former loops. The hosted CHECK_dbus has only the C, so this is where the library's side of each is proved.
*/
static positive bd_agree(void)
{
        static const unsigned long codes[] = {0x41, 0x7f, 0x80, 0x7ff, 0x800, 0xfffd, 0xffff, 0x10000, 0x10ffff, 0x1f600, 0xe9, 0x4e2d};
        unsigned long state = 0x9e3779b97f4a7c15ul;
        positive wrong = 0, cases = 0;
        p8 text[96];

        for (positive round = 0; round < 200000; round++)
        {
                positive n = 0, want;

                state ^= state << 13, state ^= state >> 7, state ^= state << 17;
                want = (positive)(state % 80);
                while (n < want)
                {
                        unsigned long code;

                        state ^= state << 13, state ^= state >> 7, state ^= state << 17;
                        code = state % 3 ? 0x20 + state % 95 : codes[(state >> 8) % array_count(codes)];
                        if (code < 0x80)
                                text[n++] = (p8)code;
                        else if (code < 0x800 && n + 2 <= want)
                                text[n++] = (p8)(0xc0 | code >> 6), text[n++] = (p8)(0x80 | (code & 63));
                        else if (code < 0x10000 && n + 3 <= want)
                                text[n++] = (p8)(0xe0 | code >> 12), text[n++] = (p8)(0x80 | ((code >> 6) & 63)), text[n++] = (p8)(0x80 | (code & 63));
                        else if (n + 4 <= want)
                                text[n++] = (p8)(0xf0 | code >> 18), text[n++] = (p8)(0x80 | ((code >> 12) & 63)), text[n++] = (p8)(0x80 | ((code >> 6) & 63)),
                                text[n++] = (p8)(0x80 | (code & 63));
                        else
                                text[n++] = 'z';
                }
                if (n && state % 4 == 0)
                {
                        static const p8 bad[] = {0x80, 0xc0, 0xc1, 0xe0, 0xed, 0xf4, 0xf5, 0xff, 0x00, 0xa0, 0x90, 0xbf};

                        text[(state >> 20) % n] = bad[(state >> 40) % array_count(bad)];
                }
                text[n] = 0;
                cases++;
                wrong += dbus_utf8(text, (long)n) != dbus_utf8_walk(text, (long)n);
        }
        for (positive n = 0; n < 4096; n += 1 + n / 3)
        {
                for (positive k = 0; k < n + 64; k++)
                        bd_bytes[k] = (p8)(k * 7 + n);
                dbus_copy(bd_bytes, bd_bytes + 64, (long)n);
                for (positive k = 0; k < n; k++)
                        wrong += bd_bytes[k] != (p8)((k + 64) * 7 + n);
                cases++;
        }
        for (positive k = 0; k < 64; k++)
        {
                const char *a = bd_names[k & 7], *b = bd_names[(k >> 3) & 7];

                wrong += dbus_length(a) != bd_former_length(a) || dbus_equal(a, b) != bd_former_equal(a, b);
                cases++;
        }
        string_format(log, "  the engine's library side against its C: %p cases, %p that differ\n", cases, wrong);
        return wrong;
}

static fn bd_loop(string_address name, p64 (*one)(p8, positive, positive), positive n, positive rounds)
{
        p64 best_c = ~(p64)0, best_library = ~(p64)0;

        for (positive trial = 0; trial < BD_TRIES; trial++)
        {
                p64 c, library;

                if (trial & 1)
                        library = one(true, n, rounds), c = one(false, n, rounds);
                else
                        c = one(false, n, rounds), library = one(true, n, rounds);
                if (c < best_c)
                        best_c = c;
                if (library < best_library)
                        best_library = library;
        }
        string_format(log, "  %s %p:  C %p  lib.c %p ticks/1000  lib.c/C %p%%\n", name, n, best_c * 1000 / rounds, best_library * 1000 / rounds,
                      best_library * 100 / (best_c ? best_c : 1));
}

static fn bd_loops(void)
{
        static const positive copies[] = {16, 40, 100, 160, 200, 256, 300, 1100, 4096}, texts[] = {1, 4, 6, 8, 10, 12, 16, 26, 40, 96, 400};

        for (positive k = 0; k < sizeof bd_bytes; k++)
                bd_bytes[k] = (p8)('a' + k % 26);
        for (positive k = 0; k < array_count(copies); k++)
                bd_loop("copy  ", bd_run_copy, copies[k], 100000);
        for (positive k = 0; k < array_count(texts); k++)
        {
                bd_bytes[texts[k]] = 0;
                bd_loop("utf8  ", bd_run_utf8, texts[k], 100000);
                bd_bytes[texts[k]] = (p8)('a' + texts[k] % 26);
        }
        bd_loop("length", bd_run_length, 0, 100000);
        bd_loop("equal ", bd_run_equal, 0, 100000);
}

// With an argument, `engine` or `loops`: that part alone (a profile of the engine is perf record of `engine`).
b32 main(void)
{
        string_address only = program_argument_count() > 1 ? program_argument(1) : (string_address) "";

        bd_only = only;

        // the library side against its C first, but for a row timed alone (whose counts it would only dilute)
        if ((!only[0] || string_equals(only, "loops") || string_equals(only, "engine")) && bd_agree())
        {
                log_flush();
                return 1;
        }
        if (!string_equals(only, "loops"))
        {
                string_format(log, "the engine (ticks):\n");
                for (int again = 0; again < (only[0] ? 20 : 1); again++)
                        bd_engine();
        }
        if (!only[0] || string_equals(only, "loops"))
        {
                string_format(log, "the loops (C against lib.c):\n");
                bd_loops();
        }
        log_flush();
        return 0;
}
#endif /* BENCH_dbus */

#ifdef BENCH_fill_sizes
/* memory_fill and memory_zero from 1 to 512 bytes, in the run time size the routine gets from a call it cannot fold, against the
   compiler's expansion of the same size as a literal (inline) and against the library's own name given the literal (which
   lib.util.c routes to the expansion to KNOWN_FILL_MAX), and a sweep of random sizes over three ranges. Ticks a call, the least of nine
   runs of 2^20 calls, the buffers rotating over four and two offsets. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define REPS (1u << 20)
#define TRIES 9

static p8 buffers[4][4096] __attribute__((aligned(64)));

typedef fn (*call_function)(p8 *);

#define SIZES(X) X(1) X(2) X(3) X(5) X(8) X(12) X(16) X(24) X(32) X(48) X(64) X(96) X(128) X(136) X(160) X(192) X(224) X(256) X(384) X(512)
#define INLINE_FN(N) __attribute__((noinline, noclone)) static fn inl_##N(p8 *d) { __builtin_memset(d, 0, N); }
SIZES(INLINE_FN)
#define KNOWN_FN(N) __attribute__((noinline, noclone)) static fn kn_##N(p8 *d) { memory_zero(d, N); }
SIZES(KNOWN_FN)

static positive want;
static fn lib_zero_call(p8 *d) { memory_zero(d, want); }
static fn lib_fill_call(p8 *d) { memory_fill(d, 0x5a, want); }

static p64 run(call_function f, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                f(buffers[r & 3] + ((r >> 2) & 1) * 64);
        return get_cpu_time() - start;
}

static fn show(positive n, string_address what, p64 ticks)
{
        string_format(log, "%p bytes %s: ", n, what);
        positive_to_padded(log, (positive)(ticks * 100 / REPS) / 100, 3, ' ', 0);
        string_format(log, ".");
        positive_to_padded(log, (positive)(ticks * 100 / REPS) % 100, 2, '0', 0);
        string_format(log, " ticks\n");
}

#define ONE(N)                                                                                                                      \
        {                                                                                                                           \
                p64 best_inline = ~(p64)0, best_zero = ~(p64)0, best_fill = ~(p64)0, best_known = ~(p64)0;                          \
                want = N;                                                                                                           \
                for (positive t = 0; t < TRIES; t++)                                                                                \
                {                                                                                                                   \
                        p64 a = run(inl_##N, REPS), b = run(lib_zero_call, REPS), c = run(lib_fill_call, REPS), k = run(kn_##N, REPS); \
                        best_known = k < best_known ? k : best_known;                                                               \
                        best_inline = a < best_inline ? a : best_inline;                                                            \
                        best_zero = b < best_zero ? b : best_zero;                                                                  \
                        best_fill = c < best_fill ? c : best_fill;                                                                  \
                }                                                                                                                   \
                show(N, "inline", best_inline);                                                                                     \
                show(N, "memory_zero", best_zero);                                                                                  \
                show(N, "memory_fill", best_fill);                                                                                  \
                show(N, "memory_zero(literal)", best_known);                                                                        \
        }

static positive sweep_state = 88172645463325252ull;
static positive sweep_low, sweep_span;
static fn sweep_zero_call(p8 *d)
{
        sweep_state ^= sweep_state << 13;
        sweep_state ^= sweep_state >> 7;
        sweep_state ^= sweep_state << 17;
        memory_zero(d, sweep_low + sweep_state % sweep_span);
}
static fn sweep_report(string_address what, positive low, positive span)
{
        p64 best_zero = ~(p64)0;

        sweep_low = low;
        sweep_span = span;
        for (positive t = 0; t < TRIES; t++)
        {
                p64 b = run(sweep_zero_call, REPS);

                best_zero = b < best_zero ? b : best_zero;
        }
        show(low, what, best_zero);
}

b32 main(void)
{
        sweep_report("sweep memory_zero 129..256", 129, 128);
        sweep_report("sweep memory_zero 1..512", 1, 512);
        sweep_report("sweep memory_zero 33..128", 33, 96);
        SIZES(ONE)
        log_flush();
        return 0;
}
#endif /* BENCH_fill_sizes */

#ifdef BENCH_small_sizes
/* The small sizes of memory_copy, string_length and memory_utf8_valid_span against the C loops a caller writes when the library loses
   (the session bus's engine kept its own below 300, 8 and never called string_length): memory_copy at 1 to 512 bytes against a byte
   loop on pointers the compiler knows nothing about (what dbus_bytes is) and the same loop on restrict pointers, which gcc turns into
   sixteen bytes a turn; string_length of 0 to 100 against the byte loop; memory_utf8_valid_span of 1 to 40 bytes against the character
   walk of the engine. Ticks a call, the least of nine runs of 2^20 calls, the offsets rotating (eight for the destination, four for the
   source), the two buffers a half page apart so the 4 KiB aliasing of a load behind a store is not what is timed. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define REPS (1u << 20)
#define TRIES 9

static p8 sm_a[8192] __attribute__((aligned(64))), sm_b[8192] __attribute__((aligned(64)));
static p8 sm_text[8][256] __attribute__((aligned(64)));
static positive sm_sink;

__attribute__((noinline, noclone)) static fn sm_copy_bytes(void *to, const void *from, positive n)
{
        p8 *d = (p8 *)to;
        const p8 *s = (const p8 *)from;

        for (positive at = 0; at < n; at++)
                d[at] = s[at];
}

__attribute__((noinline, noclone)) static fn sm_copy_vector(void *__restrict to, const void *__restrict from, positive n)
{
        p8 *d = (p8 *)to;
        const p8 *s = (const p8 *)from;

        for (positive at = 0; at < n; at++)
                d[at] = s[at];
}

__attribute__((noinline, noclone)) static positive sm_length_bytes(const char *s)
{
        positive n = 0;

        while (s[n])
                n++;
        return n;
}

__attribute__((noinline, noclone)) static int sm_utf8_walk(const unsigned char *s, long n)
{
        for (long at = 0; at < n;)
        {
                unsigned char c = s[at];
                long more = c < 0x80 ? 0 : c >= 0xc2 && c < 0xe0 ? 1 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xf0 && c < 0xf5 ? 3 : -1;
                unsigned long code;

                if (!c || more < 0 || at + more >= n + (more == 0))
                        return 0;
                if (!more)
                {
                        at++;
                        continue;
                }
                code = more == 1 ? c & 0x1fu : more == 2 ? c & 0x0fu : c & 0x07u;
                for (long k = 1; k <= more; k++)
                {
                        if ((s[at + k] & 0xc0) != 0x80)
                                return 0;
                        code = code << 6 | (s[at + k] & 0x3fu);
                }
                if ((more == 2 && (code < 0x800 || (code >= 0xd800 && code < 0xe000))) || (more == 3 && (code < 0x10000 || code > 0x10ffff)))
                        return 0;
                at += more + 1;
        }
        return 1;
}

#define SM_LOOP(name, statement)                                                                    \
        __attribute__((noinline, noclone)) static p64 name(positive n)                                \
        {                                                                                           \
                p64 start = get_cpu_time();                                                         \
                                                                                                    \
                for (positive r = 0; r < REPS; r++)                                                 \
                {                                                                                   \
                        p8 *to = sm_b + 1536 + (r & 7), *from = sm_a + 512 + (r & 3);              \
                        const char *text = (const char *)sm_text[r & 7] + (r & 7);                 \
                        (void)to, (void)from, (void)text;                                           \
                        statement;                                                                  \
                }                                                                                   \
                return get_cpu_time() - start;                                                      \
        }
SM_LOOP(sm_copy_lib, memory_copy(to, from, n))
SM_LOOP(sm_copy_apart, memory_copy_apart(to, from, n))
SM_LOOP(sm_copy_c, sm_copy_bytes(to, from, n))
SM_LOOP(sm_copy_v, sm_copy_vector(to, from, n))
SM_LOOP(sm_length_lib, sm_sink += string_length(text))
SM_LOOP(sm_length_c, sm_sink += sm_length_bytes(text))
SM_LOOP(sm_utf8_lib, sm_sink += memory_utf8_valid_span((address_any)text, n).x == n)
SM_LOOP(sm_utf8_c, sm_sink += (positive)sm_utf8_walk((const unsigned char *)text, (long)n))

static fn sm_show(string_address what, positive n, p64 ticks)
{
        string_format(log, "%s %p: ", what, n);
        positive_to_padded(log, (positive)(ticks * 100 / REPS) / 100, 3, ' ', 0);
        string_format(log, ".");
        positive_to_padded(log, (positive)(ticks * 100 / REPS) % 100, 2, '0', 0);
        string_format(log, " ticks");
}

typedef p64 (*sm_function)(positive);

static fn sm_row(string_address what, positive n, sm_function lib, sm_function first, sm_function second)
{
        p64 best_lib = ~(p64)0, best_first = ~(p64)0, best_second = ~(p64)0;

        for (positive t = 0; t < TRIES; t++)
        {
                p64 a = lib(n), b = first(n), c = second ? second(n) : 0;

                best_lib = a < best_lib ? a : best_lib;
                best_first = b < best_first ? b : best_first;
                best_second = second && c < best_second ? c : best_second;
        }
        sm_show(what, n, best_lib);
        string_format(log, " (lib), ");
        positive_to_padded(log, (positive)(best_first * 100 / REPS) / 100, 3, ' ', 0);
        string_format(log, ".");
        positive_to_padded(log, (positive)(best_first * 100 / REPS) % 100, 2, '0', 0);
        string_format(log, " (C)");
        if (second)
        {
                string_format(log, ", ");
                positive_to_padded(log, (positive)(best_second * 100 / REPS) / 100, 3, ' ', 0);
                string_format(log, ".");
                positive_to_padded(log, (positive)(best_second * 100 / REPS) % 100, 2, '0', 0);
                string_format(log, " (C, restrict)");
        }
        string_format(log, ", lib/C %p%%\n", (positive)(best_lib * 100 / (best_first ? best_first : 1)));
}

static fn sm_texts(positive n, positive multibyte)
{
        for (positive k = 0; k < 8; k++)
        {
                p8 *line = sm_text[k] + k;

                for (positive at = 0; at < 200; at++)
                        line[at] = (p8)('a' + at % 26);
                if (multibyte && n >= 4)
                        line[n - 3] = 0xc3, line[n - 2] = 0xa9, line[n - 1] = 'z';
                line[n] = 0;
        }
}

b32 main(void)
{
        static const positive copies[] = {1, 2, 3, 4, 5, 7, 8, 9, 12, 15, 16, 17, 24, 31, 32, 33, 40, 48, 63, 64, 65, 100, 128, 129, 160, 192,
                                          200, 255, 256, 257, 300, 384, 512, 1100, 4096};
        static const positive lengths[] = {0, 1, 2, 3, 4, 5, 8, 12, 16, 20, 24, 31, 40, 64, 100};
        static const positive texts[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 26, 40, 96};

        for (positive k = 0; k < sizeof sm_a; k++)
                sm_a[k] = (p8)(k * 7 + 3);
        for (positive k = 0; k < array_count(copies); k++)
                sm_row("copy", copies[k], sm_copy_lib, sm_copy_c, sm_copy_v);
        for (positive k = 0; k < array_count(copies); k++)
                if (copies[k] <= 512)
                        sm_row("copy_apart", copies[k], sm_copy_apart, sm_copy_c, sm_copy_v);
        for (positive k = 0; k < array_count(lengths); k++)
        {
                sm_texts(lengths[k], 0);
                sm_row("string_length", lengths[k], sm_length_lib, sm_length_c, 0);
        }
        for (positive k = 0; k < array_count(texts); k++)
        {
                sm_texts(texts[k], 0);
                sm_row("utf8 ascii", texts[k], sm_utf8_lib, sm_utf8_c, 0);
        }
        for (positive k = 0; k < array_count(texts); k++)
                if (texts[k] >= 4)
                {
                        sm_texts(texts[k], 1);
                        sm_row("utf8 e-acute last", texts[k], sm_utf8_lib, sm_utf8_c, 0);
                }
        log_flush();
        return 0;
}
#endif /* BENCH_small_sizes */

#ifdef BENCH_translate_planes
/* memory_translate_planes_u32_rect against the C loop it replaced in
   src/canvas/font.c (font_translate_lcd): a strip of a terminal row as
   font_cells lays it (1220 by 15) and cells (9 by 19, 16 by 35), into a
   3840-pixel frame, with a grey table and a colour one; on x86_64 with VBMI
   and without. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

static p32 frame[3840 * 40];
static p8 strip[4 * 1300 * 40];
static p32 table[512];

__attribute__((noinline, noclone)) static fn former_planes(p32 address_to into, positive stride, const p8 address_to from,
                                                           positive from_stride, positive width, positive height,
                                                           const p32 address_to pixel)
{
        const p8 address_to planes = (const p8 address_to)(pixel + 256);

        for (positive r = 0; r < height; r++, into += stride, from += from_stride)
                for (positive c = 0; c < width; c++)
                        into[c] = planes[from[4 * c]] | (p32)planes[256 + from[4 * c + 1]] << 8 |
                                  (p32)planes[512 + from[4 * c + 2]] << 16;
}

static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
                if (assembly)
                        memory_translate_planes_u32_rect(frame + round % 7, 3840, strip, 4 * width + 16, width, height, table);
                else
                        former_planes(frame + round % 7, 3840, strip, 4 * width + 16, width, height, table);
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[][2] = {{9, 19}, {16, 35}, {1220, 15}};
        p64 state = 0x2d6b1f0e93a7c845ull;

        for (positive i = 0; i < sizeof(strip); i++)
        {
                XORSHIFT64(state);
                strip[i] = (p8)(state % 3 == 0 ? 0 : state % 3 == 1 ? 255 : state >> 8);
        }
        for (p32 grey = 0; grey < 2; grey++)
        {
                for (p32 i = 0; i < 256; i++)
                {
                        p32 g = grey ? 0x10 + i * 0xd8 / 255 : 0;

                        table[i] = grey ? g * 0x010101 : (0x20 + i / 2) << 16 | (0x30 + i / 3) << 8 | (0x10 + i * 3 / 4);
                        for (p32 k = 0; k < 4; k++)
                                ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
                }
#if X64
                p8 wide = cpu_has_avx512_vbmi;
                for (p32 tier = 0; tier < (wide ? 2u : 1u); tier++)
                {
                        cpu_has_avx512_vbmi = tier ? 0 : wide;
#endif
                        for (positive row = 0; row < sizeof(shapes) / sizeof(shapes[0]); row++)
                        {
                                positive width = shapes[row][0], height = shapes[row][1];
                                positive ratios[TRIES];
                                positive rounds = 4000000 / (width * height) + 1;

                                BENCH_PAIRED_RUNS(ratios, former, assembly, width, height, rounds);
                                string_format(log, "memory_translate_planes_u32_rect %p by %p, %s%s: paired median ASM/C %p.",
                                              width, height, grey ? (string_address) "grey" : (string_address) "colour",
#if X64
                                              cpu_has_avx512_vbmi ? (string_address) " vbmi" : (string_address) " scalar",
#else
                                              (string_address) "",
#endif
                                              ratios[TRIES / 2] / 100);
                                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                                log("%\n", 2);
                        }
#if X64
                }
                cpu_has_avx512_vbmi = wide;
#endif
        }
        log_flush();
        return 0;
}
#endif /* BENCH_translate_planes */

#ifdef BENCH_copy_changed
/* memory_copy_changed_u32 against the same chunks compared and copied in C
   and against a plain copy of every row, over a 1220-pixel window's rows
   (the terminal of the desk bench) in a 3840-pixel frame: rows with no
   change, one changed chunk a row and every word changed; on x86_64 with
   AVX-512, AVX2 and SSE2. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define WIDE 1220
#define TALL 64

static p32 into[3840 * TALL], mirror[3840 * TALL], from[3840 * TALL];

__attribute__((noinline, noclone)) static positive former_changed(p32 address_to to, p32 address_to seen,
                                                                const p32 address_to source, positive count)
{
        positive stored = 0;

        for (positive at = 0; at < count; at += 16)
        {
                positive words = count - at < 16 ? count - at : 16;

                if (!memory_compare(seen + at, source + at, words * 4))
                        continue;
                memory_copy(seen + at, source + at, words * 4);
                memory_copy(to + at, source + at, words * 4);
                stored++;
        }
        return stored;
}

// The frame's change: none, the word at a row's middle, every word; mirror made the frame before it.
static fn prepare(positive pattern, positive round)
{
        for (positive row = 0; row < TALL; row++)
                for (positive x = 0; x < WIDE; x++)
                {
                        p32 was = (p32)(row * 7 + x);

                        mirror[row * 3840 + x] = was;
                        from[row * 3840 + x] = pattern == 2 || (pattern == 1 && x == WIDE / 2) ? was ^ (p32)(round + 1) : was;
                }
}

static p64 run(positive side, positive pattern, positive rounds)
{
        p64 took = 0;

        for (positive round = 0; round < rounds; round++)
        {
                prepare(pattern, round);
                p64 start = get_cpu_time();

                for (positive row = 0; row < TALL; row++)
                        if (side == 0)
                                former_changed(into + row * 3840, mirror + row * 3840, from + row * 3840, WIDE);
                        else if (side == 1)
                                memory_copy_changed_u32(into + row * 3840, mirror + row * 3840, from + row * 3840, WIDE);
                        else
                                memory_copy(into + row * 3840, from + row * 3840, WIDE * 4);
                took += get_cpu_time() - start;
        }
        return took;
}

b32 main(void)
{
        static const string_address names[] = {"no change", "one chunk a row", "every word"};
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                if ((tier == 0 && !wide512) || (tier == 1 && !wide2))
                        continue;
#endif
                for (positive pattern = 0; pattern < 3; pattern++)
                {
                        positive ratios[TRIES], plain[TRIES];

                        BENCH_PAIRED(ratios, former, run(0, pattern, 200), assembly, run(1, pattern, 200));
                        BENCH_PAIRED(plain, copy, run(2, pattern, 200), assembly, run(1, pattern, 200));
                        string_format(log, "memory_copy_changed_u32 %p by %p, %s%s: paired median ASM/C %p.", (positive)WIDE,
                                      (positive)TALL, names[pattern],
#if X64
                                      cpu_has_avx512 ? (string_address) " avx512" : cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        string_format(log, "%%, ASM/copy %p.", plain[TRIES / 2] / 100);
                        positive_to_padded(log, plain[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide2;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_copy_changed */

#ifdef BENCH_first_differs
/* memory_first_differs_u32 against the loop ui_run_in had: a row of scratch paper, a word that is not paper at the
   end of a run of the length given, the run from the start of the row. The kit's text gives runs of one to three
   words most of the time (a stroke's edge) and runs of tens to hundreds across a row's paper; the lengths here cover
   both. On x86_64 with AVX2 and without, riscv64 with V and without, and arm64 NEON. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define PAPER 0xffffffu
#define WORDS 4096

static unsigned int row[WORDS];
static volatile positive sink;

__attribute__((noinline, noclone)) static positive former_first(const unsigned int address_to words, positive count, unsigned int value)
{
        positive at = 0;

        while (at < count && words[at] == value)
                at++;
        return at;
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive length, positive rounds)
{
        positive answers = 0;
        p64 start;

        row[length] = PAPER ^ 0x10000u;
        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                const unsigned int address_to words = row + round % 8;
                positive count = length + 8;

                if (assembly)
                        answers += memory_first_differs_u32(words, count, PAPER) + round;
                else
                        answers += former_first(words, count, PAPER) + round;
        }
        sink = answers;
        row[length] = PAPER;
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive lengths[] = {1, 3, 12, 60, 300};
        static const string_address names[] = {"one run", "three words", "twelve words", "sixty words", "three hundred words"};

        for (positive at = 0; at < WORDS; at++)
                row[at] = PAPER;
#if X64
        p8 wide2 = cpu_has_avx2;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier == 0 ? wide2 : 0;
                if (tier == 0 && !wide2)
                        continue;
#elif RISCV64
        p8 vector = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                for (positive at = 0; at < sizeof(lengths) / sizeof(lengths[0]); at++)
                {
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (lengths[at] + 8) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, lengths[at], rounds);
                        string_format(log, "memory_first_differs_u32 %s, %s: paired median ASM/C %p.", names[at],
#if X64
                                      cpu_has_avx2 ? (string_address) "avx2" : (string_address) "sse2",
#elif RISCV64
                                      cpu_has_vector == 2 ? (string_address) "vector" : (string_address) "scalar",
#else
                                      (string_address) "neon",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
#if X64
        cpu_has_avx2 = wide2;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_first_differs */

#ifdef BENCH_saturate
/* memory_saturate_u32 against the C compositor_vivid made of each pixel: a row of backdrop boosted into another, runs of
   one, three, twelve, sixty and three hundred pixels, on each tier the machine has. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define WORDS 4096

static p32 backdrop[WORDS], boosted[WORDS];
static volatile positive sink;

__attribute__((noinline, noclone)) static p32 former_boost(p32 pixel)
{
        int r = (int)(pixel >> 16 & 255), g = (int)(pixel >> 8 & 255), b = (int)(pixel & 255);
        int grey = (r * 77 + g * 150 + b * 29) >> 8;

        r += (r - grey) >> 1;
        g += (g - grey) >> 1;
        b += (b - grey) >> 1;
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        return (p32)(r << 16 | g << 8 | b);
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive length, positive rounds)
{
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                const p32 address_to from = backdrop + round % 8;
                p32 address_to into = boosted + round % 8;

                if (assembly)
                        memory_saturate_u32(into, from, length);
                else
                        for (positive at = 0; at < length; at++)
                                into[at] = former_boost(from[at]);
        }
        sink = boosted[length - 1];
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive lengths[] = {1, 3, 12, 60, 300};
        static const string_address names[] = {"one pixel", "three pixels", "twelve pixels", "sixty pixels", "three hundred pixels"};

        for (positive at = 0; at < WORDS; at++)
                backdrop[at] = (p32)at * 2654435761u;
#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
                if ((tier == 0 && !wide512) || (tier == 1 && !wide256))
                        continue;
#elif RISCV64
        p8 vector = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                for (positive at = 0; at < sizeof(lengths) / sizeof(lengths[0]); at++)
                {
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (lengths[at] + 8) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, lengths[at], rounds);
                        string_format(log, "memory_saturate_u32 %s, %s: paired median ASM/C %p.", names[at],
#if X64
                                      cpu_has_avx512 ? (string_address) "avx512" : cpu_has_avx2 ? (string_address) "avx2" : (string_address) "scalar",
#elif RISCV64
                                      cpu_has_vector == 2 ? (string_address) "vector" : (string_address) "scalar",
#else
                                      (string_address) "neon",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
#if X64
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_saturate */

#ifdef BENCH_sum_swap
/* memory_sum_swap_u32 against the word loop compositor_glass_pass ran (the glass sums of a row of blocks, each
   swapped in), runs of one, three, twelve, sixty and three hundred blocks: the body the machine has, on each tier. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define WORDS 4096

static p32 now_row[WORDS], old_row[WORDS];
static unsigned long long sums_row[3];
static volatile positive sink;

/* The loop as a call of its own, as the compositor makes it: the sums are in memory between rows, not in registers. */
__attribute__((noinline, noclone)) static void former_sum_swap(p64 address_to sums, p32 address_to into, const p32 address_to from,
                                                              positive length)
{
        for (positive at = 0; at < length; at++)
        {
                sums[0] += from[at] >> 16 & 255;
                sums[0] -= into[at] >> 16 & 255;
                sums[1] += from[at] >> 8 & 255;
                sums[1] -= into[at] >> 8 & 255;
                sums[2] += from[at] & 255;
                sums[2] -= into[at] & 255;
                into[at] = from[at];
        }
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive length, positive rounds)
{
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                const p32 address_to from = now_row + round % 8;
                p32 address_to into = old_row + round % 8;

                if (assembly)
                        memory_sum_swap_u32((p64 address_to)sums_row, into, from, length);
                else
                        former_sum_swap((p64 address_to)sums_row, into, from, length);
        }
        sink = (positive)sums_row[0];
        return get_cpu_time() - start;
}

b32 main()
{
        static const positive lengths[] = {1, 3, 12, 60, 300};
        static const string_address names[] = {"one block", "three blocks", "twelve blocks", "sixty blocks", "three hundred blocks"};

        for (positive at = 0; at < WORDS; at++)
                now_row[at] = (p32)at * 2654435761u, old_row[at] = (p32)at * 40503u;
        for (positive at = 0; at < sizeof(lengths) / sizeof(lengths[0]); at++)
        {
                positive ratios[TRIES];
                positive rounds = 4000000 / (lengths[at] + 8) + 1;

                BENCH_PAIRED_RUNS(ratios, former, assembly, lengths[at], rounds);
                string_format(log, "memory_sum_swap_u32 %s: paired median ASM/C %p.", names[at],
                              ratios[TRIES / 2] / 100);
                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                log("%\n", 2);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_sum_swap */

#ifdef BENCH_invert
/* memory_invert_u32 against the word loop compositor_frost ran (the complement of a row, word by word), runs of one,
   three, twelve, sixty and three hundred words, each run a call of its own, as the frost makes them. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define WORDS 4096

static p32 source_row[WORDS], into_row[WORDS];
static volatile positive sink;

__attribute__((noinline, noclone)) static void former_invert(p32 address_to into, const p32 address_to from, positive length)
{
        for (positive at = 0; at < length; at++)
                into[at] = ~from[at];
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive length, positive rounds)
{
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                const p32 address_to from = source_row + round % 8;
                p32 address_to into = into_row + round % 8;

                if (assembly)
                        memory_invert_u32(into, from, length);
                else
                        former_invert(into, from, length);
        }
        sink = into_row[0];
        return get_cpu_time() - start;
}

b32 main()
{
        static const positive lengths[] = {1, 3, 12, 60, 300};
        static const string_address names[] = {"one word", "three words", "twelve words", "sixty words", "three hundred words"};

        for (positive at = 0; at < WORDS; at++)
                source_row[at] = (p32)at * 2654435761u;
        for (positive at = 0; at < sizeof(lengths) / sizeof(lengths[0]); at++)
        {
                positive ratios[TRIES];
                positive rounds = 4000000 / (lengths[at] + 8) + 1;

                BENCH_PAIRED_RUNS(ratios, former, assembly, lengths[at], rounds);
                string_format(log, "memory_invert_u32 %s: paired median ASM/C %p.", names[at], ratios[TRIES / 2] / 100);
                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                log("%\n", 2);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_invert */

#ifdef BENCH_copy_strip
/* memory_copy_strip_16 against the loop font_strip16 was everywhere but x86-64 (a 16-byte load and store a row, the
   compiler's): a cell's coverage laid into a text run's strip, one row to forty, the strip's stride and an atlas's,
   the addresses at the odd places a cell width puts them. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

typedef p8 strip_word __attribute__((vector_size(16)));
static p8 strip_from[64 * 512 + 64], strip_into[64 * 512 + 64];
static volatile positive sink;

__attribute__((noinline, noclone)) static void former_strip(p8 address_to into, positive stride, const p8 address_to source,
                                                            positive source_stride, positive rows)
{
        for (positive r = 0; r < rows; r++, into += stride, source += source_stride)
                memory_store_unaligned(strip_word, into, memory_load_unaligned(strip_word, source));
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive rows, positive rounds)
{
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                const p8 address_to from = strip_from + 3 + round % 8;
                p8 address_to into = strip_into + 5 + (round % 8) * 9;

                if (assembly)
                        memory_copy_strip_16(into, 496, from, 512, rows);
                else
                        former_strip(into, 496, from, 512, rows);
        }
        sink = strip_into[5];
        return get_cpu_time() - start;
}

b32 main()
{
        static const positive rows[] = {1, 2, 4, 8, 20, 40};
        static const string_address names[] = {"one row", "two rows", "four rows", "eight rows", "twenty rows", "forty rows"};

        for (positive at = 0; at < sizeof(strip_from); at++)
                strip_from[at] = (p8)(at * 131);
        for (positive at = 0; at < sizeof(rows) / sizeof(rows[0]); at++)
        {
                positive ratios[TRIES];
                positive rounds = 4000000 / (rows[at] + 4) + 1;

                BENCH_PAIRED_RUNS(ratios, former, assembly, rows[at], rounds);
                string_format(log, "memory_copy_strip_16 %s: paired median ASM/C %p.", names[at], ratios[TRIES / 2] / 100);
                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                log("%\n", 2);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_copy_strip */

#ifdef BENCH_tint
/* memory_tint_u32 against the C the compositor's glass tone was: a row of boosted pixels tinted toward the light side
   with the glass's cap and floor, runs of one, three, twelve, sixty and three hundred pixels, on each tier. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define WORDS 4096

static p32 boosted[WORDS], tinted[WORDS];
static volatile positive sink;

__attribute__((noinline, noclone)) static p32 former_tone(p32 pixel)
{
        p32 mixed = 0;

        for (p32 shift = 0; shift < 24; shift += 8)
                mixed |= (((pixel >> shift & 255) * (256 - 96) + 255 * 96 + 128) >> 8) << shift;
        {
                p32 r = mixed >> 16 & 255, g = mixed >> 8 & 255, b = mixed & 255;
                p32 most = r > g ? r : g, least = r < g ? r : g;

                most = most > b ? most : b;
                least = least < b ? least : b;
                if (least < 0xb4)
                {
                        r = 255 - (255 - r) * (255 - 0xb4) / (255 - least);
                        g = 255 - (255 - g) * (255 - 0xb4) / (255 - least);
                        b = 255 - (255 - b) * (255 - 0xb4) / (255 - least);
                }
                return r << 16 | g << 8 | b;
        }
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive length, positive rounds)
{
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                const p32 address_to from = boosted + round % 8;
                p32 address_to into = tinted + round % 8;

                if (assembly)
                        memory_tint_u32(into, from, length, 0xffffffu, 96, 255, 0xb4);
                else
                        for (positive at = 0; at < length; at++)
                                into[at] = former_tone(from[at]);
        }
        sink = tinted[length - 1];
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive lengths[] = {1, 3, 12, 60, 300};
        static const string_address names[] = {"one pixel", "three pixels", "twelve pixels", "sixty pixels", "three hundred pixels"};

        for (positive at = 0; at < WORDS; at++)
                boosted[at] = (p32)at * 2654435761u;
#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
                if ((tier == 0 && !wide512) || (tier == 1 && !wide256))
                        continue;
#elif RISCV64
        p8 vector = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                for (positive at = 0; at < sizeof(lengths) / sizeof(lengths[0]); at++)
                {
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (lengths[at] + 8) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, lengths[at], rounds);
                        string_format(log, "memory_tint_u32 %s, %s: paired median ASM/C %p.", names[at],
#if X64
                                      cpu_has_avx512 ? (string_address) "avx512" : cpu_has_avx2 ? (string_address) "avx2" : (string_address) "scalar",
#elif RISCV64
                                      cpu_has_vector == 2 ? (string_address) "vector" : (string_address) "scalar",
#else
                                      (string_address) "neon",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
#if X64
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_tint */

#ifdef BENCH_mix
/* memory_mix_u32_row against the pixel loop the round corners had: compositor_mix of each pixel whose cover is between
   0 and 255, the ink whole at 255 and the pixel kept at 0, over runs of one, eight, twelve, sixty-four and three hundred
   pixels with covers of zero, 255 and between. On x86_64 (SSE2), riscv64 with V and without, and arm64 NEON. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define WORDS 4096

static p32 mixed[WORDS], inks[WORDS];
static p8 covers[WORDS];
static volatile positive sink;

/* compositor_mix, as src/canvas/compositor.c has it: the pixel loop's per-pixel mix. */
__attribute__((noinline, noclone)) static unsigned int compositor_mix(unsigned int ground, unsigned int ink, unsigned int cover)
{
        const unsigned long long low = 0xff00ff00ffull, one = 0x100010001ull;
        unsigned long long g, i, x;

        if (cover == 0)
                return ground & 0xffffffu;
        if (cover >= 255)
                return ink & 0xffffffu;
        g = (ground & 0xffu) | (unsigned long long)(ground & 0xff00u) << 8 | (unsigned long long)(ground & 0xff0000u) << 16;
        i = (ink & 0xffu) | (unsigned long long)(ink & 0xff00u) << 8 | (unsigned long long)(ink & 0xff0000u) << 16;
        x = g * (255 - cover) + i * cover + 127 * one;
        x += one + ((x >> 8) & low);
        x = (x >> 8) & low;
        return (unsigned int)((x & 0xff) | (x >> 8 & 0xff00) | (x >> 16 & 0xff0000));
}

__attribute__((noinline, noclone)) static void former_mix(p32 address_to into, const p32 address_to ink,
                                                          const p8 address_to cover, positive length)
{
        for (positive at = 0; at < length; at++)
                if (cover[at])
                        into[at] = cover[at] == 255 ? ink[at] : compositor_mix(into[at], ink[at], cover[at]);
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive length, positive rounds)
{
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                positive offset = (round * 7) % 2048;

                if (assembly)
                        memory_mix_u32_row(mixed + offset, inks + offset, covers + offset, length);
                else
                        former_mix(mixed + offset, inks + offset, covers + offset, length);
        }
        sink = mixed[length - 1];
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive lengths[] = {1, 8, 12, 64, 300};
        static const string_address names[] = {"one pixel", "eight pixels", "twelve pixels", "sixty-four pixels",
                                               "three hundred pixels"};
        p32 seed = 12345;

        for (positive at = 0; at < WORDS; at++)
        {
                seed = seed * 1103515245u + 12345u;
                mixed[at] = seed >> 3;
                seed = seed * 1103515245u + 12345u;
                inks[at] = seed >> 3;
                seed = seed * 1103515245u + 12345u;
                covers[at] = (p8)(seed >> 16);
                if ((seed >> 8) % 4 == 0)
                        covers[at] = 0;
                else if ((seed >> 8) % 4 == 1)
                        covers[at] = 255;
        }
#if RISCV64
        p8 vector = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                for (positive at = 0; at < sizeof(lengths) / sizeof(lengths[0]); at++)
                {
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (lengths[at] + 8) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, lengths[at], rounds);
                        string_format(log, "memory_mix_u32_row %s, %s: paired median ASM/C %p.", names[at],
#if X64
                                      (string_address) "sse2",
#elif RISCV64
                                      cpu_has_vector == 2 ? (string_address) "vector" : (string_address) "scalar",
#else
                                      (string_address) "neon",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
#if RISCV64
        cpu_has_vector = vector;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_mix */

#ifdef BENCH_copy_rect
/* memory_copy_rect_u32 against the row loop compositor_blit had: a rectangle copied a row at a time with memory_copy.
   The shapes are the compositor's: a window's rows, 1160 words wide with rows 2560 words apart, a sliver of damage a
   few dozen words wide, a band and a row narrower than the wide body. On x86_64 with AVX-512 and without, riscv64 with
   V and without, and arm64 NEON. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define STRIDE 2560
#define ROWS 64

static unsigned int into_plane[STRIDE * ROWS];
static unsigned int from_plane[STRIDE * ROWS];
static volatile positive sink;

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        positive answers = 0;
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                unsigned int address_to into = into_plane + round % 8;
                const unsigned int address_to from = from_plane + 3 + round % 8;

                if (assembly)
                        memory_copy_rect_u32(into, STRIDE, from, STRIDE, width, height);
                else
                        for (positive row = 0; row < height; row++)
                                memory_copy(into + row * STRIDE, from + row * STRIDE, width * 4);
                answers += into[width - 1] + round;
        }
        sink = answers;
        return get_cpu_time() - start;
}

/* memory_copy_rect_stream_u32 against memory_copy_rect_u32 on ordinary cached memory, where it has nothing to win (its
   streaming loads are for a GPU's write-combined buffer, which this file cannot make): a control that it does not lose. */
__attribute__((noinline, noclone)) static p64 run_stream(p8 stream, positive width, positive height, positive rounds)
{
        positive answers = 0;
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                unsigned int address_to into = into_plane + round % 8;
                const unsigned int address_to from = from_plane + 3 + round % 8;

                if (stream)
                        memory_copy_rect_stream_u32(into, STRIDE, from, STRIDE, width, height);
                else
                        memory_copy_rect_u32(into, STRIDE, from, STRIDE, width, height);
                answers += into[width - 1] + round;
        }
        sink = answers;
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[][2] = {{1160, 64}, {40, 32}, {300, 16}, {96, 64}};
        static const string_address names[] = {"a window of 1160 words", "a sliver of 40 words", "a band of 300 words",
                                               "a row of 96 words"};

        for (positive at = 0; at < STRIDE * ROWS; at++)
                from_plane[at] = (unsigned int)at * 2654435761u;
#if X64
        p8 wide512 = cpu_has_avx512;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                if (tier == 0 && !wide512)
                        continue;
#elif RISCV64
        p8 vector = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                for (positive at = 0; at < sizeof(shapes) / sizeof(shapes[0]); at++)
                {
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (shapes[at][0] * shapes[at][1] / 16 + 64) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, shapes[at][0], shapes[at][1], rounds);
                        string_format(log, "memory_copy_rect_u32 %s: paired median ASM/C %p.", names[at],
#if X64
                                      cpu_has_avx512 ? (string_address) "avx512" : (string_address) "avx2 or sse2",
#elif RISCV64
                                      cpu_has_vector == 2 ? (string_address) "vector" : (string_address) "scalar",
#else
                                      (string_address) "neon",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
#if X64
        cpu_has_avx512 = wide512;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        for (positive at = 0; at < sizeof(shapes) / sizeof(shapes[0]); at++)
        {
                positive ratios[TRIES];
                positive rounds = 4000000 / (shapes[at][0] * shapes[at][1] / 16 + 64) + 1;

                BENCH_PAIRED(ratios, former, run_stream(false, shapes[at][0], shapes[at][1], rounds), assembly,
                             run_stream(true, shapes[at][0], shapes[at][1], rounds));
                string_format(log, "memory_copy_rect_stream_u32 %s, ordinary memory: paired median stream/plain %p.", names[at],
                              (string_address) "", ratios[TRIES / 2] / 100);
                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                log("%\n", 2);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_copy_rect */

#ifdef BENCH_transpose
/* memory_transpose_u32 against the tile loop windows_rotate_u32 had for a quarter turn: each tile of a panel is a
   block of rows by columns, the picture's column read down its rows and written across the panel's, with a
   picture 1024 words a row. The shapes are the full-width tiles the caller sends, sixteen columns and four to sixteen
   rows; the edges of a picture stay on the loop. On x86_64 with SSE2, arm64 with NEON, and scalar on riscv64. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define STRIDE 1024
#define ROWS 1024

static unsigned int turn_into[STRIDE * ROWS];
static unsigned int turn_from[STRIDE * ROWS];
static volatile positive sink;

/* the loop windows_rotate_u32 wrote: row r of the tile is the picture's column, read down the picture's rows */
__attribute__((noinline, noclone)) static void turn_c_loop(unsigned int address_to into, const unsigned int address_to from,
                                                           positive rows, positive cols)
{
        for (positive row = 0; row < rows; row++)
        {
                const unsigned int address_to source = from + row;
                unsigned int address_to to = into + row * STRIDE;

                for (positive px = 0; px < cols; px++, source -= STRIDE)
                        to[px] = *source;
        }
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive rows, positive cols, positive rounds)
{
        positive answers = 0;
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                unsigned int address_to into = turn_into + (round % 8) * (ROWS / 2) + (round % 8);
                const unsigned int address_to from = turn_from + (ROWS - 1 - round % 8) * STRIDE + 3 + round % 8;

                if (assembly)
                        memory_transpose_u32(into, STRIDE, from, -(long)STRIDE, 1, rows, cols);
                else
                        turn_c_loop(into, from, rows, cols);
                answers += into[cols - 1] + round;
        }
        sink = answers;
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[][2] = {{16, 16}, {12, 16}, {8, 16}, {4, 16}};
        static const string_address names[] = {"a 16 by 16 tile", "a 12 by 16 tile", "an 8 by 16 tile",
                                               "a 4 by 16 tile"};

        for (positive at = 0; at < STRIDE * ROWS; at++)
                turn_from[at] = (unsigned int)at * 2654435761u;
        for (positive at = 0; at < sizeof(shapes) / sizeof(shapes[0]); at++)
        {
                positive ratios[TRIES];
                positive rounds = 4000000 / (shapes[at][0] * shapes[at][1] / 16 + 64) + 1;

                BENCH_PAIRED_RUNS(ratios, former, assembly, shapes[at][0], shapes[at][1], rounds);
                string_format(log, "memory_transpose_u32 %s: paired median ASM/C %p.", names[at],
#if X64
                              (string_address) "sse2",
#elif RISCV64
                              (string_address) "scalar",
#else
                              (string_address) "neon",
#endif
                              ratios[TRIES / 2] / 100);
                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                log("%\n", 2);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_transpose */

#ifdef BENCH_runs
/* memory_runs_u32 against the C loop it is written out as (runs_reference in the CHECK_font section): a picture read for its
   stretches of one colour, a chunk of 16 words at a time. The shapes are what the compositor reads once a picture has
   been composed three times: a window's rows 1160 words wide with rows 2560 apart, all one colour, as a blank terminal
   is; lines of text, each chunk of 16 a noise chunk or a flat one at a third; and noise, where no chunk is flat and
   every row ends at the first word that differs. On x86_64 with AVX-512 and without, riscv64 and arm64 (one body each). */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define STRIDE 2560
#define ROWS 64

static unsigned int plane[STRIDE * ROWS];
static unsigned char rows_out[ROWS * 128 + 64];
static volatile positive sink;

__attribute__((noinline, noclone)) static void runs_c(unsigned char address_to rows, const unsigned int address_to from,
                                                      positive from_stride, positive width, positive height)
{
        positive chunks = width / 16 > 4095 ? 4095 : width / 16;

        for (positive y = 0; y < height; y++)
        {
                unsigned char address_to row = rows + y * 128;
                const unsigned int address_to line = from + y * from_stride;
                positive count = 0, start = 0;
                p8 open = false;
                unsigned int colour = 0;

                for (positive c = 0; c <= chunks; c++)
                {
                        p8 flat = false;

                        if (c < chunks)
                        {
                                flat = true;
                                for (positive k = 1; k < 16; k++)
                                        flat = flat && line[c * 16 + k] == line[c * 16];
                        }
                        if (flat && open && line[c * 16] == colour)
                                continue;
                        if (open && c - start >= 2 && count < 15)
                        {
                                unsigned short address_to run = (unsigned short address_to)(row + 8 + count * 8);

                                run[0] = (unsigned short)(start * 16);
                                run[1] = (unsigned short)(c * 16);
                                *(unsigned int address_to)(row + 8 + count * 8 + 4) = colour;
                                count++;
                        }
                        open = flat;
                        if (flat)
                        {
                                start = c;
                                colour = line[c * 16];
                        }
                }
                *(unsigned int address_to)row = (unsigned int)count;
                *(unsigned int address_to)(row + 4) = 0;
        }
}

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        positive answers = 0;
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                if (assembly)
                        memory_runs_u32(rows_out, plane + round % 8, STRIDE, width, height);
                else
                        runs_c(rows_out, plane + round % 8, STRIDE, width, height);
                answers += rows_out[0] + round;
        }
        sink = answers;
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const string_address names[] = {"a flat window of 1160 words", "lines of text 1160 words wide", "noise 1160 words wide"};
        p64 seed = 0x2545f4914f6cdd1dull;

        for (positive shape = 0; shape < 3; shape++)
        {
                for (positive y = 0; y < ROWS; y++)
                        for (positive x = 0; x < STRIDE; x++)
                        {
                                XORSHIFT64(seed);
                                plane[y * STRIDE + x] = shape == 0 ? 0x00203040u : shape == 1 && (x / 16 + y / 19) % 3 == 0 ? 0x00203040u : (unsigned int)(seed >> 8);
                        }
                {
#if X64
                p8 wide512 = cpu_has_avx512;
                for (positive tier = 0; tier < 2; tier++)
                {
                        cpu_has_avx512 = tier == 0 ? wide512 : 0;
                        if (tier == 0 && !wide512)
                                continue;
#else
                for (positive tier = 0; tier < 1; tier++)
                {
#endif
                        positive ratios[TRIES];
                        positive rounds = 2000000 / (1160 * ROWS / 16 + 64) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, 1160, ROWS, rounds);
                        string_format(log, "memory_runs_u32 %s, %s: paired median ASM/C %p.", names[shape],
#if X64
                                      cpu_has_avx512 ? (string_address) "avx512" : (string_address) "scalar",
#elif RISCV64
                                      (string_address) "scalar",
#else
                                      (string_address) "neon",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
                cpu_has_avx512 = wide512;
#endif
                }
        }
        log_flush();
        return 0;
}
#endif /* BENCH_runs */

#ifdef BENCH_resample
/* memory_resample_u32_rect against resample_reference, the walk compositor_blit_scaled made: a landing window's picture,
   1000 by 600 words, drawn at 96%, 98% and 100% of its size, a whole rectangle a frame. On x86_64 with AVX-512, with
   AVX2 alone and with neither; on arm64 and riscv64 the scalar walk. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define PICTURE_WIDTH 1000
#define PICTURE_HEIGHT 600

static unsigned int picture[PICTURE_WIDTH * PICTURE_HEIGHT];
static unsigned int drawn[1024 * 640];
static volatile positive sink;

__attribute__((noinline, noclone)) static p64 run(p8 assembly, positive width, positive height, positive rounds)
{
        positive answers = 0;
        p64 start;

        start = get_cpu_time();
        for (positive round = 0; round < rounds; round++)
        {
                unsigned int address_to into = drawn + round % 8;

                if (assembly)
                        memory_resample_u32_rect(into, 1024, width, height, picture, PICTURE_WIDTH, PICTURE_WIDTH,
                                                 PICTURE_HEIGHT, width, height, 0, 0);
                else
                        resample_reference(into, 1024, width, height, picture, PICTURE_WIDTH, PICTURE_WIDTH, PICTURE_HEIGHT,
                                           width, height, 0, 0);
                answers += into[width - 1] + round;
        }
        sink = answers;
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive shapes[][2] = {{960, 576}, {980, 588}, {1000, 600}};
        static const string_address names[] = {"a landing at 96%", "a landing at 98%", "a landing at 100%"};

        for (positive at = 0; at < PICTURE_WIDTH * PICTURE_HEIGHT; at++)
                picture[at] = (unsigned int)at * 2654435761u;
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;
        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide2;
                if (tier == 0 && !wide512)
                        continue;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                for (positive at = 0; at < sizeof(shapes) / sizeof(shapes[0]); at++)
                {
                        positive ratios[TRIES];
                        positive rounds = 4000000 / (shapes[at][0] * shapes[at][1] / 16 + 64) + 1;

                        BENCH_PAIRED_RUNS(ratios, former, assembly, shapes[at][0], shapes[at][1], rounds);
                        string_format(log, "memory_resample_u32_rect %s: paired median ASM/C %p.", names[at],
#if X64
                                      cpu_has_avx512 ? (string_address) "avx512" : cpu_has_avx2 ? (string_address) "avx2" : (string_address) "scalar",
#else
                                      (string_address) "scalar",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
        }
#if X64
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide2;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_resample */

