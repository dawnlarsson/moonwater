/*
        test/checks/coverage.c: the hook a coverage build calls at each basic block.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef COVERAGE_hook
/*
        The other half of `sh test/run coverage`: built on its own, without
        instrumentation, and linked into every freestanding binary a coverage
        run makes, whose every basic block starts with a call here.

        What it records is where it was called from, a byte per address of
        text, in a file test/run made the size of any image (sparse, so only
        the pages a run touches take room) and named at compile time -- a
        freestanding binary has no environment reader that runs before its
        first block. The file is mapped shared, so every process the binary
        becomes writes into the same record: forked children, the utilities
        the farm execs under a hundred names, threads. Nothing needs flushing
        at exit, which a process killed by a timeout never reaches.

        A process that cannot open it (a bowl's root, a sandbox without the
        path) records nothing and runs on; the report counts a binary whose
        record is empty as never having run, never as covered.

        COVERAGE_CREATE is the image's variant (kernel/profile/coverage):
        nothing makes the record before the guest's PID 1 runs, so the first
        process to reach a block creates it and every one sizes it -- the
        same size, so the race between two first processes is harmless.
*/
#if !defined(__x86_64__)
#error "coverage records are taken on x86_64"
#endif
#ifndef COVERAGE_PATH
#error "COVERAGE_PATH names the record"
#endif

#define COVERAGE_BASE 0x400000ul
#define COVERAGE_ROOM (64ul << 20)

static unsigned char *coverage_map;
static int coverage_tried;

static long coverage_system(long number, long a, long b, long c, long d, long e, long f)
{
        register long r10 __asm__("r10") = d;
        register long r8 __asm__("r8") = e;
        register long r9 __asm__("r9") = f;
        long result;

        __asm__ volatile("syscall"
                         : "=a"(result)
                         : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
        return result;
}

void __sanitizer_cov_trace_pc(void)
{
        unsigned long at = (unsigned long)__builtin_return_address(0) - COVERAGE_BASE;
        unsigned char *map = coverage_map;

        if (!map)
        {
                long handle, mapped;

                if (__atomic_exchange_n(&coverage_tried, 1, __ATOMIC_RELAXED))
                        return;
#ifdef COVERAGE_CREATE
                handle = coverage_system(2, (long)COVERAGE_PATH, 02 | 0100 | 02000000, 0600,
                                         0, 0, 0);
                if (handle >= 0 && coverage_system(77, handle, (long)COVERAGE_ROOM, 0, 0,
                                                   0, 0) < 0)
                {
                        coverage_system(3, handle, 0, 0, 0, 0, 0);
                        return;
                }
#else
                handle = coverage_system(2, (long)COVERAGE_PATH, 02 | 02000000, 0, 0, 0, 0);
#ifdef COVERAGE_FALLBACK
                /* A process in a chroot (the moonwater command's sandbox)
                   cannot see the record's path; the sandbox binds the
                   coverage directory at /moonwater-coverage instead, a place
                   no verb it runs empties. */
                if (handle < 0)
                        handle = coverage_system(2, (long)COVERAGE_FALLBACK, 02 | 02000000, 0, 0,
                                                 0, 0);
#endif
#endif
                if (handle < 0)
                        return;
                mapped = coverage_system(9, 0, (long)COVERAGE_ROOM, 3, 1, handle, 0);
                coverage_system(3, handle, 0, 0, 0, 0, 0);
                if ((unsigned long)mapped > -4096ul)
                        return;
                coverage_map = map = (unsigned char *)mapped;
        }

        if (at < COVERAGE_ROOM)
                map[at] = 1;
}
#endif /* COVERAGE_hook */

