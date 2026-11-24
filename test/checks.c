/*
        Every C check and every C benchmark in the tree, in one file.

            gcc ... -DCHECK_verify test/checks.c              a check
            gcc ... -DCHECK_spool_reference test/checks.c     its glibc twin
            gcc ... -DCHECK_allocator -DALLOCATOR_REFERENCE test/checks.c
            gcc ... -DBENCH_floor test/checks.c               a benchmark

        A section is one whole program, between #ifdef CHECK_<name> and its
        #endif. test/run picks one by defining that name and nothing else in
        this file is compiled. The benchmarks are sections of their own under
        BENCH_<name>, at the end of the file; `sh test/run bench` builds those and
        `sh test/run` builds none of them. The reference variants --
        the same body linked against glibc so the trace it prints can be
        diffed against ours -- are sections of their own, named _reference,
        except the allocator, which was always one file built twice and keeps
        its ALLOCATOR_REFERENCE switch. CHECK_format_standalone is
        CHECK_format with FORMAT_STANDALONE set.

        Pieces are shared between sections: the literal-size lists, the counters every counted
        test reports through, the named-case walk, the case tables the
        freestanding and glibc variants both run, the stream and spool bodies,
        the terminal fixture the term and edit harnesses in test/run link, and
        the timing scaffold and scalar references the benchmarks were written
        against. Each appears once, below, under SHARED_<name>, and a section reaches
        it where its #include line stood by defining that name and including
        this file again:

            #define SHARED_counted
            #include "checks.c"
            #undef SHARED_counted

        That is the whole of what the #if chain that opens the file is for:
        with a SHARED_ name defined, the file is that one piece and nothing
        else. The chain is ordered innermost first -- named_cases includes
        counted -- so a nested read finds its own branch before the one that
        asked for it.

        Three sections are hand-run rather than picked by test/run: the
        hardware floor (BENCH_hardware_floor), the libc comparison
        (BENCH_libc_audit) and the inlining probe (CHECK_library_inline).
        Each was a file of its own until it was folded in here and each
        carries its own compile recipe above it, because each is built
        against something test/run does not link.
*/

/*
        CHECK_gpu_model9 and CHECK_gpu_model_kit9 are CHECK_gpu_model and
        CHECK_gpu_model_kit with GPU_MODEL_GFX9 set: the model of the card a
        gfx9 chip, and gpu.c's kernels and packets gfx9's (test/checks/gpu_model.c,
        test/gpu_model/card.c). They stand ahead of the chain, since every
        piece that asks for one of the two sections is theirs too.
*/
#if defined(CHECK_gpu_model9) && !defined(CHECK_gpu_model)
#define CHECK_gpu_model
#define GPU_MODEL_GFX9
#endif
#if defined(CHECK_gpu_model_kit9) && !defined(CHECK_gpu_model_kit)
#define CHECK_gpu_model_kit
#define GPU_MODEL_GFX9
#endif

/*
        The one macro every section may use, so it stands ahead of the chain:
        BENCH_ALTERNATE times two runs in the order a trial asks for. An even
        trial takes first then second, an odd one second then first, so
        neither side always inherits the warm cache or the far end of a
        drifting clock.
*/
#define BENCH_ALTERNATE(trial, first, first_run, second, second_run)          \
        do {                                                                  \
                if ((trial) & 1)                                              \
                {                                                             \
                        (second) = (second_run);                              \
                        (first) = (first_run);                                \
                }                                                             \
                else                                                          \
                {                                                             \
                        (first) = (first_run);                                \
                        (second) = (second_run);                              \
                }                                                             \
        } while (0)

/*
        The xorshift step every seeded check draws with, as a statement:
        XORSHIFT64(seed) is Marsaglia's 13, 7, 17 on a 64-bit state and
        XORSHIFT32(seed) his 13, 17, 5 on a 32-bit one. The draw is the state
        afterwards; the macro is the same three shifts each check used to
        spell out, so every seed produces the sequence it always did.
*/
#define XORSHIFT64(state) \
        ((state) ^= (state) << 13, (state) ^= (state) >> 7, (state) ^= (state) << 17)
#define XORSHIFT32(state) \
        ((state) ^= (state) << 13, (state) ^= (state) >> 17, (state) ^= (state) << 5)

/*
        VECTOR_FIRST_CALL(yes), when yes holds, makes the call after it the
        first a riscv64 process makes: it forgets cpu_has_vector, so a body
        that asks for V when it does not know yet calls cpu_vector_detect and
        comes back from it with what the detect left in its registers. A
        guard-page walk's first-call tier says yes before every call of such a
        body. On the other machines it is nothing. A tier that forces a body
        (cpu_has_vector = 1) never says yes, or the detect would undo it.
*/
#if defined(__riscv) && __riscv_xlen == 64
#define VECTOR_FIRST_CALL(yes) ((yes) ? (void)(cpu_has_vector = 0) : (void)0)
#else
#define VECTOR_FIRST_CALL(yes) ((void)0)
#endif

/*
        BENCH_PAIRED fills ratios[] with one measurement per trial and sorts
        it, so ratios[TRIES / 2] is the paired median: each trial takes one
        (the baseline) and two (what is under test) in the order BENCH_ALTERNATE
        gives, and records two * 10000 / one, ten thousand being parity.
        BENCH_PAIRED_RUNS is the common case, one run(bool assembly, ...) that
        the flag turns from the former body into the assembly. The section
        supplies TRIES and order().
*/
#define BENCH_PAIRED(ratios, one, one_run, two, two_run)                      \
        do {                                                                  \
                for (positive trial = 0; trial < TRIES; trial++)              \
                {                                                             \
                        p64 one, two;                                         \
                        BENCH_ALTERNATE(trial, one, one_run, two, two_run);   \
                        (ratios)[trial] =                                     \
                            (positive)((two) * 10000 / ((one) ? (one) : 1));  \
                }                                                             \
                order(ratios, TRIES);                                         \
        } while (0)
#define BENCH_PAIRED_RUNS(ratios, former, assembly, ...)                      \
        BENCH_PAIRED(ratios, former, run(false, __VA_ARGS__), assembly,       \
                     run(true, __VA_ARGS__))

/*
        BENCH_BEST_PAIRED is the best-of form of the same alternation: over
        TRIES trials it keeps the fastest time of each side, the two best
        variables being ones the section has already set to its maximum.
*/
#define BENCH_BEST_PAIRED(first_best, first_run, second_best, second_run)     \
        do {                                                                  \
                for (positive trial = 0; trial < TRIES; trial++)              \
                {                                                             \
                        p64 first_took, second_took;                          \
                        BENCH_ALTERNATE(trial, first_took, first_run,         \
                                        second_took, second_run);             \
                        if (first_took < (first_best))                        \
                                (first_best) = first_took;                    \
                        if (second_took < (second_best))                      \
                                (second_best) = second_took;                  \
                }                                                             \
        } while (0)

/*
        BENCH_ONCE defines one timed run: name(params) does count iterations
        of the body with index counting them and answers the CPU ticks they
        took. The body is a statement without its closing semicolon, or a
        block. A benchmark's paired sides are each one of these.
*/
#define BENCH_ONCE(name, params, count, index, ...)                           \
        static p64 name params                                                \
        {                                                                     \
                p64 bench_start = get_cpu_time();                             \
                                                                              \
                for (positive index = 0; index < (count); index++)            \
                        __VA_ARGS__;                                          \
                                                                              \
                return get_cpu_time() - bench_start;                          \
        }

/*
        The command line of a benchmark that can run one piece of its work on
        its own (for perf or a trace): BENCH_NAME is one entry of the lookup a
        section's named function is made of, and BENCH_RUN_NAMED runs what an
        argument names and leaves, or leaves with 2 when it names nothing.
*/
#define BENCH_NAME(name, literal, work)                                       \
        if (string_compare(name, (string_address)literal) == 0)               \
                return work
#define BENCH_RUN_NAMED(named)                                                \
        do {                                                                  \
                if (program_argument_count() > 1)                             \
                {                                                             \
                        bench_work work = named(program_argument(1));         \
                                                                              \
                        if (is_null(work))                                    \
                                return 2;                                     \
                                                                              \
                        work();                                               \
                        return 0;                                             \
                }                                                             \
        } while (0)

/*
        HARDWARE_WORK is one body of the hosted floor benchmarks: rounds calls
        of an expression whose answer goes into sink, after any setup the body
        needs. BENCH_hardware_floor and BENCH_libc_audit are each a table of
        these.
*/
#define HARDWARE_WORK(name, setup, expression)                                \
        static void name(unsigned long size, unsigned long rounds)            \
        {                                                                     \
                unsigned long i;                                              \
                setup                                                         \
                for (i = 0; i < rounds; i++)                                  \
                        sink += expression;                                   \
        }

/*
        BENCH_BEST_ONCE defines a timed run that keeps the best of tries
        repetitions of BENCH_ONCE's loop, the run least disturbed by whatever
        else the machine was doing.
*/
#define BENCH_BEST_ONCE(name, params, tries, count, index, ...)               \
        static p64 name params                                                \
        {                                                                     \
                p64 best = positive_max;                                      \
                                                                              \
                for (positive attempt = 0; attempt < (tries); attempt++)      \
                {                                                             \
                        p64 bench_start = get_cpu_time();                     \
                        for (positive index = 0; index < (count); index++)    \
                                __VA_ARGS__;                                  \
                        p64 took = get_cpu_time() - bench_start;              \
                        if (took < best)                                      \
                                best = took;                                  \
                }                                                             \
                                                                              \
                return best;                                                  \
        }

#if defined(SHARED_sizes)
/* Literal arguments exercise compiler-owned specializations. The scan checks
   keep their twenty-size function bands to bound compiler resource use. */
#define EACH_SIZE_0_19(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)
#define EACH_SIZE_20_39(X) \
    X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) \
    X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39)
#define EACH_SIZE_40_59(X) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) \
    X(50) X(51) X(52) X(53) X(54) X(55) X(56) X(57) X(58) X(59)
#define EACH_SIZE_60_79(X) \
    X(60) X(61) X(62) X(63) X(64) X(65) X(66) X(67) X(68) X(69) \
    X(70) X(71) X(72) X(73) X(74) X(75) X(76) X(77) X(78) X(79)
#define EACH_SIZE_80_99(X) \
    X(80) X(81) X(82) X(83) X(84) X(85) X(86) X(87) X(88) X(89) \
    X(90) X(91) X(92) X(93) X(94) X(95) X(96) X(97) X(98) X(99)
#define EACH_SIZE_100_119(X) \
    X(100) X(101) X(102) X(103) X(104) X(105) X(106) X(107) X(108) X(109) \
    X(110) X(111) X(112) X(113) X(114) X(115) X(116) X(117) X(118) X(119)
#define EACH_SIZE_120_139(X) \
    X(120) X(121) X(122) X(123) X(124) X(125) X(126) X(127) X(128) X(129) \
    X(130) X(131) X(132) X(133) X(134) X(135) X(136) X(137) X(138) X(139)
#define EACH_SIZE_140_159(X) \
    X(140) X(141) X(142) X(143) X(144) X(145) X(146) X(147) X(148) X(149) \
    X(150) X(151) X(152) X(153) X(154) X(155) X(156) X(157) X(158) X(159)
#define EACH_SIZE_160_160(X) \
    X(160)
#define EACH_SIZE(X) \
    EACH_SIZE_0_19(X) \
    EACH_SIZE_20_39(X) \
    EACH_SIZE_40_59(X) \
    EACH_SIZE_60_79(X) \
    EACH_SIZE_80_99(X) \
    EACH_SIZE_100_119(X) \
    EACH_SIZE_120_139(X) \
    EACH_SIZE_140_159(X) \
    EACH_SIZE_160_160(X)

#elif defined(SHARED_counted)
/*
        What every counted test in this directory does the same way.

        A counted test asks a list of questions, keeps two numbers -- how many
        it asked and how many disagreed -- prints one line the runner greps
        for, and leaves with one when anything disagreed. Twenty one files in
        here were each carrying their own copy of exactly that: the same two
        counters, the same eight line macro, the same three line ending. The
        drift you get from writing one thing twenty one times had already
        arrived -- two spellings of the macro's braces, four spellings of the
        label in front of the count, two spellings of the same return -- so
        this is the one copy and those are gone.

        The counters and the ending are what all twenty one share. check is
        what most of them ask with; the rest have a typed helper of their own
        -- same, judge, fail -- that counts the same two numbers and says more
        about the disagreement than a name can, and they keep it. Including
        this file is a claim about the counters and the ending, not a promise
        to ask through check.

        check is a macro and not a function, which is not a style choice.
        Two things depend on it:

        The name is pasted into the format string rather than passed as an
        argument, so the whole message is one literal the compiler assembles
        and a test that never fails never builds a string at runtime.

        The condition is evaluated where it is written rather than as an
        argument. bool is p8 here, so a condition that answers a pointer or a
        wide word would be narrowed to its low byte on the way into a call --
        and a non-null pointer whose low byte happens to be zero would come
        out false. Written as a macro, !(condition) is the C the author meant.

        A failure is flushed where it is said: left in the buffer, it went
        into every child forked after it, and each child that wrote and
        flushed anything printed the parent's FAIL again -- one failed row of
        storage_io's ARP probe came out 227 times, as many as the forks after
        it that said a word.
*/

static positive checks;
static positive failures;

#define check(name, condition)                                          \
        do {                                                            \
                checks++;                                               \
                if (!(condition)) {                                     \
                        failures++;                                     \
                        string_format(log, "  FAIL " name "\n");        \
                        log_flush();                                    \
                }                                                       \
        } while (0)

/*
        The last thing a counted test does, and the answer main returns.

        label is what goes in front of the count for the lanes that name
        themselves there -- "bounded: ", a bare newline -- and null for the
        ones that do not. The flush is here because the count line is the
        only thing the runner reads, and a test that computed it and left it
        in the buffer would look like a test that never ran.
*/
static b32 test_report(string_address label)
{
        string_format(log, "%s%p checks, %p failures\n",
                      is_null(label) ? (string_address) "" : label,
                      checks, failures);
        log_flush();

        return failures ? 1 : 0;
}

#elif defined(SHARED_named_cases)
/*
        The named-case shape, which is the other way a test here is written.

        A counted test asks its questions inline. A named-case test is a list
        of named functions that each answer true or false, walked in order,
        with the name printed as it starts and PASSED or FAILED printed after
        it. Four files -- standard, stdlib, math, leaving -- were each
        carrying their own copy of the same three macros, the same two type
        declarations and the same twenty line walk.

        counted.inc comes in below, and is where checks, failures and
        test_report come from: a case that ran is a check and a case that
        failed is a failure, so the two shapes count the same two numbers and
        three of these four suites end with the same line a counted test ends
        with. Nothing here needs its check macro, but a suite is free to ask
        through it.

        test(name) opens one. fail(condition) is how a case gives up: it
        returns false the moment something is not what it should be, so what
        gets reported is the first disagreement rather than the last.
        case(name) puts one in the table and spells the name out of the token,
        so the printed name and the called function cannot drift apart. A
        table ends with a zeroed entry.

        result is kept on the entry rather than only counted because one of
        the four walks the table a second time afterwards to write an HTML
        report out of it.

        What follows the walk stays with each suite, because the four of them
        deliberately do not end the same way: one leaves through exit rather
        than a return, because it is the family that knows the difference; one
        writes its verdict and deliberately never flushes, because the bytes
        arriving at all is the claim that file exists to make; and one says
        passed and failed where the others say checks and failures.
*/

#define SHARED_counted
#include "checks.c"
#undef SHARED_counted

#define test(test_name) p8 test_##test_name(void)
#define case(test_name)                        \
        {                                      \
                (string_address)#test_name,    \
                test_##test_name               \
        }
#define fail(condition)   \
        if (!(condition)) \
        return false

typedef p8(address_to test_function)(void);

typedef struct
{
        string_address name;
        test_function function;
        p8 result;
} test_case;

/*
        Runs a table in order and leaves the two counters holding how many
        ran and how many failed, which is what every one of the four ending
        lines is written out of.
*/
static fn test_cases_walk(test_case address_to walk)
{
        while (walk->name)
        {
                log_direct(walk->name, string_length(walk->name));

                walk->result = walk->function();

                checks++;

                if (walk->result)
                        log_direct(str(" PASSED\n"));
                else
                {
                        failures++;
                        log_direct(str(" ----- FAILED\n"));
                }

                walk++;
        }
}

#elif defined(SHARED_declare_cases)
/*
        Every name lib.c gives a standard spelling to, called by that
        spelling.

        This file is the point of the whole exercise and it is deliberately
        plain C. It declares no types of its own, casts nothing, and reaches
        for no house name: it says memcpy and strlen and sqrt the way a
        program ported into this distribution would say them. That is what
        makes it a test of the declarations rather than of the routines.

        It is compiled twice from two files that share nothing but this one.
        CHECK_declare includes src/lib.util.c and gets every name
        below from the declarations in src/lib.c and the ASM_ALIAS
        symbols beside them.
        CHECK_declare_reference includes the host's <string.h>, <ctype.h>,
        <stdlib.h>, <strings.h>, <math.h> and <setjmp.h> and gets them from
        glibc. If a declaration in declare.c disagreed with the one in a real
        header by so much as a const, the second build would not compile, and
        the second build compiling is the assertion that matters most here.
        The two are then supposed to print the same verdict.

        The wrapper supplies three things and this file assumes nothing
        else, and prints the verdict itself afterwards:

            declare_check(name, condition)   counts, and names the failure
            declare_jump_state               jump_state here, jmp_buf there
            DECLARE_FREESTANDING             defined only in the first build

        DECLARE_FREESTANDING appears once for the identity checks, which ask
        whether a standard name and a house name are one address -- a question
        glibc has no opinion about -- and once to pin string_copy_max's
        deliberately unpadded house contract beside strncpy's padded one.
*/

static char declare_left[64];
static char declare_right[64];

static int declare_offset(const void *found, const void *base)
{
        if (found == 0)
                return -1;

        return (int)((const char *)found - (const char *)base);
}

/*
        Two addresses compared where the linker settled them and not where
        the compiler guessed. The reason is at the identity checks below.
*/
#define declare_same(name, one, two)                                          \
        do {                                                                  \
                void *volatile first = (void *)(one);                         \
                void *volatile second = (void *)(two);                        \
                                                                              \
                declare_check(name, first == second);                         \
        } while (0)

static int declare_sign(int value)
{
        if (value < 0)
                return -1;

        return value > 0;
}

/*
        The C locale, written out.

        Every ctype answer below is compared against this rather than against
        a table of expected bits, because the point is that both libraries
        implement the same definition and a table would only prove they match
        each other. isascii and toascii are not C's at all -- they are the
        pre-standard pair POSIX kept -- and their definition is the mask.
*/
static int declare_is_upper(int c) { return c >= 'A' && c <= 'Z'; }
static int declare_is_lower(int c) { return c >= 'a' && c <= 'z'; }
static int declare_is_digit(int c) { return c >= '0' && c <= '9'; }
static int declare_is_alpha(int c) { return declare_is_upper(c) || declare_is_lower(c); }
static int declare_is_alnum(int c) { return declare_is_alpha(c) || declare_is_digit(c); }
static int declare_is_space(int c)
{
        return c == ' ' || (c >= '\t' && c <= '\r');
}
static int declare_is_print(int c) { return c >= ' ' && c <= '~'; }
static int declare_is_graph(int c) { return c > ' ' && c <= '~'; }
static int declare_is_cntrl(int c) { return (c >= 0 && c < ' ') || c == 0x7f; }
static int declare_is_punct(int c) { return declare_is_graph(c) && !declare_is_alnum(c); }
static int declare_is_blank(int c) { return c == ' ' || c == '\t'; }
static int declare_is_hex(int c)
{
        return declare_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static void declare_cases(void)
{
        char *found;
        void *block;
        char *stop;
        int index;

        /*
                The byte hunts, and what each answers when the byte is not
                there. strchr and memchr say null; strchrnul says the
                terminator's address, which is the only reason it exists.
                strrchr is string_last_of_or_end, whose name is about the one
                case where the byte hunted for IS the terminator -- there the
                answer is the end of the string, which is what C says too.
        */
        declare_check("strchr hit", declare_offset(strchr("hello", 'l'), "hello") == 2);
        declare_check("strchr miss", strchr("hello", 'z') == 0);
        declare_check("strchr terminator",
                      declare_offset(strchr("hello", 0), "hello") == 5);
        declare_check("strrchr hit", declare_offset(strrchr("hello", 'l'), "hello") == 3);
        declare_check("strrchr miss", strrchr("hello", 'z') == 0);
        declare_check("strrchr terminator",
                      declare_offset(strrchr("hello", 0), "hello") == 5);
        declare_check("strchrnul hit",
                      declare_offset(strchrnul("hello", 'l'), "hello") == 2);
        declare_check("strchrnul miss",
                      declare_offset(strchrnul("hello", 'z'), "hello") == 5);

        /*
                The character argument is an int and the routine reads its
                low byte, which is what C means by "converted to unsigned
                char". 0x16c is 'l' with a ninth bit set and -152 is 'h' sign
                extended; both have to find their byte.
        */
        declare_check("strchr high bits",
                      declare_offset(strchr("hello", 0x16c), "hello") == 2);
        declare_check("strchr negative",
                      declare_offset(strchr("hello", -152), "hello") == 0);
        declare_check("memchr hit", declare_offset(memchr("hello", 'l', 5), "hello") == 2);
        declare_check("memchr miss", memchr("hello", 'z', 5) == 0);
        declare_check("memchr high bits",
                      declare_offset(memchr("hello", 0x16c, 5), "hello") == 2);
        declare_check("memchr no bytes", memchr("hello", 'h', 0) == 0);
        declare_check("memrchr hit",
                      declare_offset(memrchr("hello", 'l', 5), "hello") == 3);
        declare_check("memrchr miss", memrchr("hello", 'z', 5) == 0);
        declare_check("memrchr high bits",
                      declare_offset(memrchr("hello", 0x16c, 5), "hello") == 3);

        /*
                The comparisons, and the sign each hands back. C says the
                bytes are compared as unsigned char, so 0x80 is above 0x01
                and not below it, which is the one way a comparison written
                with a signed char is wrong on every input with the top bit
                set.
        */
        declare_check("memcmp below", declare_sign(memcmp("\x01", "\x02", 1)) == -1);
        declare_check("memcmp above", declare_sign(memcmp("\x02", "\x01", 1)) == 1);
        declare_check("memcmp equal", memcmp("\x02", "\x02", 1) == 0);
        declare_check("memcmp unsigned", declare_sign(memcmp("\x80", "\x01", 1)) == 1);
        declare_check("memcmp no bytes", memcmp("\x80", "\x01", 0) == 0);
        declare_check("strcmp unsigned", declare_sign(strcmp("\x80", "\x01")) == 1);
        declare_check("strcmp prefix", declare_sign(strcmp("ab", "abc")) == -1);
        declare_check("strcmp equal", strcmp("abc", "abc") == 0);
        declare_check("strncmp bound", strncmp("abZ", "abY", 2) == 0);
        declare_check("strncmp past", declare_sign(strncmp("abZ", "abY", 3)) == 1);
        declare_check("strncmp no bytes", strncmp("a", "b", 0) == 0);
        declare_check("bcmp differs", bcmp("\x01", "\x02", 1) != 0);
        declare_check("bcmp same", bcmp("\x02", "\x02", 1) == 0);
        declare_check("strcasecmp equal", strcasecmp("AbC", "aBc") == 0);
        declare_check("strcasecmp below", declare_sign(strcasecmp("abc", "abd")) == -1);
        declare_check("strncasecmp bound", strncasecmp("AbZ", "aBY", 2) == 0);

        /*
                The lengths and the spans.
        */
        declare_check("strlen", strlen("hello") == 5);
        declare_check("strlen empty", strlen("") == 0);
        declare_check("strnlen under", strnlen("hello", 3) == 3);
        declare_check("strnlen over", strnlen("hello", 9) == 5);
        declare_check("strnlen none", strnlen("hello", 0) == 0);
        declare_check("strspn", strspn("abcXY", "cba") == 3);
        declare_check("strspn none", strspn("Xabc", "abc") == 0);
        declare_check("strspn empty set", strspn("abc", "") == 0);
        declare_check("strcspn", strcspn("abcXY", "YX") == 3);
        declare_check("strcspn none", strcspn("abc", "XY") == 3);
        declare_check("strcspn empty set", strcspn("abc", "") == 3);
        declare_check("strpbrk hit", declare_offset(strpbrk("abcXY", "YX"), "abcXY") == 3);
        declare_check("strpbrk miss", strpbrk("abc", "XY") == 0);
        declare_check("strpbrk empty set", strpbrk("abc", "") == 0);

        /*
                The searches. An empty needle is found at the front, which is
                what C says for strstr and what POSIX says for memmem, and a
                needle longer than the haystack is not found at all.
        */
        declare_check("strstr hit", declare_offset(strstr("abcdef", "cd"), "abcdef") == 2);
        declare_check("strstr miss", strstr("abcdef", "cz") == 0);
        declare_check("strstr empty", declare_offset(strstr("abcdef", ""), "abcdef") == 0);
        declare_check("memmem hit",
                      declare_offset(memmem("abcdef", 6, "cd", 2), "abcdef") == 2);
        declare_check("memmem miss", memmem("abcdef", 6, "cz", 2) == 0);
        declare_check("memmem empty",
                      declare_offset(memmem("abcdef", 6, "", 0), "abcdef") == 0);
        declare_check("memmem too long", memmem("abc", 3, "abcd", 4) == 0);
        declare_check("strcasestr hit",
                      declare_offset(strcasestr("abCDef", "cd"), "abCDef") == 2);
        declare_check("strcasestr miss", strcasestr("abcdef", "cz") == 0);

        /*
                The block moves. memset takes an int and keeps its low byte,
                so 0x141 fills with 'A' and -1 fills with 0xff.
        */
        memset(declare_left, 'X', sizeof declare_left);
        memset(declare_left, 0x141, 4);
        declare_check("memset low byte",
                      declare_left[0] == 'A' && declare_left[3] == 'A' &&
                              declare_left[4] == 'X');
        memset(declare_left, -1, 4);
        declare_check("memset negative",
                      (unsigned char)declare_left[0] == 0xff &&
                              declare_left[4] == 'X');
        block = memset(declare_left, 'X', sizeof declare_left);
        declare_check("memset returns destination", block == declare_left);

        memset(declare_left, 'X', sizeof declare_left);
        bzero(declare_left, 3);
        declare_check("bzero", declare_left[0] == 0 && declare_left[2] == 0 &&
                                       declare_left[3] == 'X');

        memset(declare_left, 'X', sizeof declare_left);
        block = memcpy(declare_left, "abcdefgh", 8);
        declare_check("memcpy", memcmp(declare_left, "abcdefgh", 8) == 0);
        declare_check("memcpy returns destination", block == declare_left);

        /*
                memmove and memcpy are one routine here, so the overlap has
                to be right in both directions or the alias is a lie. This is
                the check that says which way the guarantee runs.
        */
        memset(declare_left, 'X', sizeof declare_left);
        memcpy(declare_left, "abcdefgh", 8);
        block = memmove(declare_left + 2, declare_left, 8);
        declare_check("memmove forward overlap",
                      memcmp(declare_left, "ababcdefgh", 10) == 0);
        declare_check("memmove returns destination", block == declare_left + 2);
        memset(declare_left, 'X', sizeof declare_left);
        memcpy(declare_left + 2, "abcdefgh", 8);
        memmove(declare_left, declare_left + 2, 8);
        declare_check("memmove backward overlap",
                      memcmp(declare_left, "abcdefgh", 8) == 0);

        memset(declare_left, 'X', sizeof declare_left);
        bcopy("abcd", declare_left, 4);
        declare_check("bcopy source first",
                      memcmp(declare_left, "abcdX", 5) == 0);

        /*
                memccpy stops after the byte it was given, and answers with
                the address one past where that byte landed, or null when it
                never turned up.
        */
        memset(declare_left, 'X', sizeof declare_left);
        block = memccpy(declare_left, "abcdef", 'c', 6);
        declare_check("memccpy stops", memcmp(declare_left, "abcXX", 5) == 0);
        declare_check("memccpy returns past", declare_offset(block, declare_left) == 3);
        memset(declare_left, 'X', sizeof declare_left);
        block = memccpy(declare_left, "abcdef", 'z', 6);
        declare_check("memccpy copies all", memcmp(declare_left, "abcdef", 6) == 0);
        declare_check("memccpy misses", block == 0);
        memset(declare_left, 'X', sizeof declare_left);
        block = memccpy(declare_left, "abcdef", 0x163, 6);
        declare_check("memccpy low byte", declare_offset(block, declare_left) == 3);

        /*
                The copies that terminate, and the two that answer with the
                end rather than the start.
        */
        memset(declare_left, 'X', sizeof declare_left);
        found = strcpy(declare_left, "abc");
        declare_check("strcpy", memcmp(declare_left, "abc\0X", 5) == 0);
        declare_check("strcpy returns destination", found == declare_left);
        memset(declare_left, 'X', sizeof declare_left);
        found = stpcpy(declare_left, "abc");
        declare_check("stpcpy", memcmp(declare_left, "abc\0X", 5) == 0);
        declare_check("stpcpy returns end", declare_offset(found, declare_left) == 3);

        memset(declare_left, 'X', sizeof declare_left);
        found = strncpy(declare_left, "abcdefgh", 4);
        declare_check("strncpy full writes no terminator",
                      memcmp(declare_left, "abcdX", 5) == 0);
        declare_check("strncpy returns destination", found == declare_left);

        /* strncpy pads; the house copy keeps its intentionally shorter rule. */
        memset(declare_left, 'X', sizeof declare_left);
        strncpy(declare_left, "abc", 8);
        declare_check("strncpy short pads with zeroes",
                      memcmp(declare_left, "abc\0\0\0\0\0", 8) == 0);
#ifdef DECLARE_FREESTANDING
        memset(declare_left, 'X', sizeof declare_left);
        string_copy_max((p8 address_to)declare_left,
                        (string_address)"abc", 8);
        declare_check("string_copy_max short pads nothing",
                      memcmp(declare_left, "abc\0XXXX", 8) == 0);
#endif

        /* stpncpy shares the padding body and returns the first zero. */
        memset(declare_left, 'X', sizeof declare_left);
        found = stpncpy(declare_left, "abc", 8);
        declare_check("stpncpy short pads",
                      memcmp(declare_left, "abc\0\0\0\0\0X", 9) == 0);
        declare_check("stpncpy returns first zero",
                      declare_offset(found, declare_left) == 3);
        memset(declare_left, 'X', sizeof declare_left);
        found = stpncpy(declare_left, "abcdefgh", 4);
        declare_check("stpncpy full", memcmp(declare_left, "abcdX", 5) == 0);
        declare_check("stpncpy full returns bound",
                      declare_offset(found, declare_left) == 4);

        memset(declare_left, 'X', sizeof declare_left);
        strcpy(declare_left, "ab");
        found = strcat(declare_left, "cd");
        declare_check("strcat", memcmp(declare_left, "abcd\0X", 6) == 0);
        declare_check("strcat returns destination", found == declare_left);
        memset(declare_left, 'X', sizeof declare_left);
        strcpy(declare_left, "ab");
        found = strncat(declare_left, "cdef", 2);
        declare_check("strncat bound", memcmp(declare_left, "abcd\0X", 6) == 0);
        declare_check("strncat returns destination", found == declare_left);
        memset(declare_left, 'X', sizeof declare_left);
        strcpy(declare_left, "ab");
        strncat(declare_left, "cd", 8);
        declare_check("strncat under bound", memcmp(declare_left, "abcd\0X", 6) == 0);

        /*
                The bounded pair BSD added and glibc took in 2.38. Both
                answer with the length the source would have needed.
        */
        memset(declare_left, 'X', sizeof declare_left);
        declare_check("strlcpy returns source length",
                      strlcpy(declare_left, "abcdef", 4) == 6);
        declare_check("strlcpy truncates",
                      memcmp(declare_left, "abc\0X", 5) == 0);
        memset(declare_left, 'X', sizeof declare_left);
        strcpy(declare_left, "ab");
        declare_check("strlcat returns wanted length",
                      strlcat(declare_left, "cdef", 4) == 6);
        declare_check("strlcat truncates", memcmp(declare_left, "abc\0X", 5) == 0);

        /*
                The duplications, which allocate, and the joke that is its
                own inverse.
        */
        found = strdup("copied");
        declare_check("strdup", found != 0 && strcmp(found, "copied") == 0);
        found = strndup("copied", 3);
        declare_check("strndup", found != 0 && strcmp(found, "cop") == 0);
        memcpy(declare_left, "abcd", 5);
        memfrob(declare_left, 4);
        declare_check("memfrob", memcmp(declare_left, "abcd", 4) != 0);
        memfrob(declare_left, 4);
        declare_check("memfrob twice", memcmp(declare_left, "abcd\0", 5) == 0);

        /*
                The three that cut a string up, and the state each keeps.
        */
        strcpy(declare_left, "a,b,,c");
        found = strtok(declare_left, ",");
        declare_check("strtok first", found != 0 && strcmp(found, "a") == 0);
        found = strtok(0, ",");
        declare_check("strtok second", found != 0 && strcmp(found, "b") == 0);
        found = strtok(0, ",");
        declare_check("strtok skips empty", found != 0 && strcmp(found, "c") == 0);
        declare_check("strtok end", strtok(0, ",") == 0);

        strcpy(declare_left, "x:y");
        {
                char *held = 0;

                found = strtok_r(declare_left, ":", &held);
                declare_check("strtok_r first", found != 0 && strcmp(found, "x") == 0);
                found = strtok_r(0, ":", &held);
                declare_check("strtok_r second", found != 0 && strcmp(found, "y") == 0);
                declare_check("strtok_r end", strtok_r(0, ":", &held) == 0);
        }

        strcpy(declare_left, "a,b,,c");
        {
                char *held = declare_left;

                found = strsep(&held, ",");
                declare_check("strsep first", found != 0 && strcmp(found, "a") == 0);
                found = strsep(&held, ",");
                declare_check("strsep second", found != 0 && strcmp(found, "b") == 0);
                found = strsep(&held, ",");
                declare_check("strsep keeps empty", found != 0 && found[0] == 0);
                found = strsep(&held, ",");
                declare_check("strsep last", found != 0 && strcmp(found, "c") == 0);
                declare_check("strsep end", strsep(&held, ",") == 0);
        }

        /*
                <ctype.h> over its whole domain, which is EOF and every
                unsigned char, against the C locale written out above. The
                argument is an int for exactly this reason: -1 has to be
                sayable and has to answer no to all twelve.
        */
        for (index = -1; index < 256; index++) {
                declare_check("isalnum", !!isalnum(index) == declare_is_alnum(index));
                declare_check("isalpha", !!isalpha(index) == declare_is_alpha(index));
                declare_check("isblank", !!isblank(index) == declare_is_blank(index));
                declare_check("iscntrl", !!iscntrl(index) == declare_is_cntrl(index));
                declare_check("isdigit", !!isdigit(index) == declare_is_digit(index));
                declare_check("isgraph", !!isgraph(index) == declare_is_graph(index));
                declare_check("islower", !!islower(index) == declare_is_lower(index));
                declare_check("isprint", !!isprint(index) == declare_is_print(index));
                declare_check("ispunct", !!ispunct(index) == declare_is_punct(index));
                declare_check("isspace", !!isspace(index) == declare_is_space(index));
                declare_check("isupper", !!isupper(index) == declare_is_upper(index));
                declare_check("isxdigit", !!isxdigit(index) == declare_is_hex(index));
                declare_check("tolower",
                              tolower(index) ==
                                      (declare_is_upper(index) ? index + 32 : index));
                declare_check("toupper",
                              toupper(index) ==
                                      (declare_is_lower(index) ? index - 32 : index));
                declare_check("isascii", !!isascii(index) == (index >= 0 && index < 128));
                declare_check("toascii", toascii(index) == (index & 0x7f));
        }

        /*
                The conversions. atoi skips leading space, takes a sign, and
                stops at the first byte that is not a digit; strtol says
                where it stopped and takes a base, with zero meaning "read
                the prefix".
        */
        declare_check("atoi", atoi("  -42xyz") == -42);
        declare_check("atoi nothing", atoi("xyz") == 0);
        declare_check("atoi plus", atoi("+17") == 17);
        declare_check("atol", atol("-9007199254740993") == -9007199254740993L);
        declare_check("atoll", atoll("9007199254740993") == 9007199254740993LL);

        declare_check("strtol", strtol("  -42xyz", &stop, 10) == -42);
        declare_check("strtol stopped", stop != 0 && strcmp(stop, "xyz") == 0);
        declare_check("strtol hex", strtol("0x1f", &stop, 16) == 31);
        declare_check("strtol hex stopped", stop != 0 && *stop == 0);
        declare_check("strtol base zero hex", strtol("0x1f", &stop, 0) == 31);
        declare_check("strtol base zero octal", strtol("017", &stop, 0) == 15);
        declare_check("strtol base thirty six", strtol("zz", &stop, 36) == 1295);
        declare_check("strtol nothing", strtol("nope", &stop, 10) == 0);
        declare_check("strtol nothing stopped",
                      stop != 0 && strcmp(stop, "nope") == 0);
        declare_check("strtoll", strtoll("-9223372036854775807", &stop, 10) ==
                                         -9223372036854775807LL);
        declare_check("strtoul", strtoul("4294967296", &stop, 10) == 4294967296UL);
        declare_check("strtoull", strtoull("ff", &stop, 16) == 255ULL);

        declare_check("abs", abs(-7) == 7 && abs(7) == 7);
        declare_check("labs", labs(-9007199254740993L) == 9007199254740993L);
        declare_check("llabs", llabs(-9007199254740993LL) == 9007199254740993LL);

        /*
                ffs counts from one and answers zero for a word with nothing
                in it, which is the one bit routine in the library whose
                index is not zero based.
        */
        declare_check("ffs none", ffs(0) == 0);
        declare_check("ffs first", ffs(1) == 1);
        declare_check("ffs ninth", ffs(0x100) == 9);
        declare_check("ffs all", ffs(-1) == 1);
        declare_check("ffsl none", ffsl(0) == 0);
        declare_check("ffsl wide", ffsl(0x100000000L) == 33);
        declare_check("ffsll none", ffsll(0) == 0);
        declare_check("ffsll wide", ffsll(0x100000000LL) == 33);

        /*
                The roundings, which differ from each other only on a tie and
                only on a negative, so the table is halves.
        */
        declare_check("trunc", trunc(2.5) == 2.0 && trunc(-2.5) == -2.0);
        declare_check("floor", floor(2.5) == 2.0 && floor(-2.5) == -3.0);
        declare_check("floor whole", floor(-1.5) == -2.0 && floor(1.0) == 1.0);
        declare_check("ceil", ceil(2.5) == 3.0 && ceil(-2.5) == -2.0);
        declare_check("round away", round(2.5) == 3.0 && round(-2.5) == -3.0);
        declare_check("round half", round(0.5) == 1.0 && round(-0.5) == -1.0);
        declare_check("nearbyint even", nearbyint(2.5) == 2.0 && nearbyint(1.5) == 2.0);
        declare_check("rint even", rint(2.5) == 2.0 && rint(3.5) == 4.0);
        declare_check("trunc large", trunc(1e300) == 1e300);
        declare_check("floor large", floor(-1e300) == -1e300);

        declare_check("sqrt", sqrt(144.0) == 12.0 && sqrt(0.25) == 0.5);
        declare_check("sqrt zero", sqrt(0.0) == 0.0);
        declare_check("fabs", fabs(-3.7) == 3.7 && fabs(3.7) == 3.7);
        declare_check("copysign", copysign(3.0, -1.0) == -3.0 &&
                                          copysign(-3.0, 1.0) == 3.0);
        declare_check("fmin", fmin(2.0, 3.0) == 2.0 && fmin(-2.0, -3.0) == -3.0);
        declare_check("fmax", fmax(2.0, 3.0) == 3.0 && fmax(-2.0, -3.0) == -2.0);
        declare_check("fdim", fdim(5.0, 3.0) == 2.0 && fdim(3.0, 5.0) == 0.0);
        declare_check("fma", fma(2.0, 3.0, 1.0) == 7.0);
        {
                /*
                        fma is one rounding and not two, which is the whole
                        of why it exists. 0.1 times ten is a hair above one
                        and the plain product rounds it down to exactly one,
                        so the plain expression is zero and the fused one is
                        the hair. The operands are volatile because otherwise
                        the compiler works it out itself and the test
                        measures gcc. The hair is exactly two to the minus
                        fifty fourth, so the answer is written as a hex
                        float and not as a tolerance. Comparing against the
                        plain product instead would measure the contraction
                        setting rather than fma: arm64 and riscv64 fuse
                        `a * b - c` into one instruction at -O2 by default,
                        so the plain expression is not plain there.
                */
                volatile double tenth = 0.1;
                volatile double ten = 10.0;

                declare_check("fma rounds once",
                              fma(tenth, ten, -1.0) == 0x1p-54);
        }

        declare_check("sqrtf", sqrtf(144.0f) == 12.0f);
        declare_check("fabsf", fabsf(-3.5f) == 3.5f);
        declare_check("truncf", truncf(-2.5f) == -2.0f);
        declare_check("floorf", floorf(-2.5f) == -3.0f);
        declare_check("ceilf", ceilf(-2.5f) == -2.0f);
        declare_check("roundf", roundf(-2.5f) == -3.0f);
        declare_check("copysignf", copysignf(3.0f, -1.0f) == -3.0f);
        declare_check("fminf", fminf(2.0f, 3.0f) == 2.0f);
        declare_check("fmaxf", fmaxf(2.0f, 3.0f) == 3.0f);

        /*
                The non-local jump, whose buffer is the one type in this file
                the two builds cannot spell the same way: jump_state here is
                thirty two slots and glibc's jmp_buf is a struct. Both decay
                to a pointer at the call, so only the name differs.
        */
        {
                declare_jump_state mark;
                volatile int arrivals = 0;
                volatile int answer;

                answer = setjmp(mark);
                arrivals++;

                if (answer == 0)
                        longjmp(mark, 7);

                declare_check("setjmp first arrival is zero", arrivals == 2);
                declare_check("longjmp value arrives", answer == 7);
        }
        {
                declare_jump_state mark;
                volatile int answer;

                answer = _setjmp(mark);

                if (answer == 0)
                        _longjmp(mark, 0);

                declare_check("longjmp zero becomes one", answer == 1);
        }

#ifdef DECLARE_FREESTANDING
        /*
                And the thing no real library has an opinion about: that
                the standard name and the prose name are one address rather
                than two functions that happen to agree. This is what
                ASM_ALIAS promises, what a forwarding wrapper would quietly
                not deliver, and the reason declare.c is prototypes and never
                macros -- a macro has no address to take.

                Each goes through declare_same, whose two volatile locals are
                not decoration. gcc is entitled to assume that two functions
                it has separate declarations for are two different objects,
                and it folds the comparison to false without ever asking the
                linker. It does that for strdup and string_duplicate, which
                ARE one address -- nm says 40abb5 for both -- because
                string_duplicate has a body in this translation unit and the
                .set that renames it is inside a string literal the compiler
                does not read. Storing each address through volatile puts the
                question to the link, where the answer is.

                Which is worth knowing beyond this test: ASM_ALIAS over a C
                body is invisible to the compiler in a way
                __attribute__((alias)) -- what allocator.c uses for malloc --
                is not.
        */
        /*
                memcpy and memmove are no longer one address.

                They were one routine until the kernel audit measured what the
                shared body cost: memory_copy tests for overlap first, and
                above thirty two bytes hands the work to the same rep movsb
                the architecture's own memcpy uses -- so both ran the same
                instruction and ours paid six extra to reach it, a flat 0.90x
                from thirty three bytes to sixteen megabytes.

                memcpy is memory_copy_apart now, which is what C already says
                about it; memmove keeps memory_copy, the body that promises
                overlap. The behavioural check below, which slides a block
                over itself both ways, is what proves memmove kept that
                promise. This pair only records that the two names stopped
                being one routine.
        */
        declare_same("memcpy is memory_copy_apart", memcpy, memory_copy_apart);
        declare_same("memmove is memory_copy", memmove, memory_copy);
        declare_same("memset is memory_fill", memset, memory_fill);
        declare_same("memcmp is memory_compare", memcmp, memory_compare);
        declare_same("bcmp is memory_compare", bcmp, memory_compare);
        declare_same("strlen is string_length", strlen, string_length);
        declare_same("strcmp is string_compare", strcmp, string_compare);
        declare_same("strchr is string_first_of", strchr, string_first_of);
        declare_same("strstr is string_search", strstr, string_search);
        declare_same("toupper is byte_to_upper", toupper, byte_to_upper);
        declare_same("isdigit is byte_is_digit", isdigit, byte_is_digit);
        declare_same("sqrt is square_root", sqrt, square_root);
        declare_same("fabs is absolute", fabs, absolute);
        declare_same("floor is decimal_floor", floor, decimal_floor);
        declare_same("labs is llabs", labs, llabs);
        declare_same("atol is atoll", atol, atoll);
        declare_same("setjmp is _setjmp", setjmp, _setjmp);
        declare_same("setjmp is jump_mark", setjmp, jump_mark);
        declare_same("strdup is string_duplicate", strdup, string_duplicate);
        declare_same("strtok is string_token", strtok, string_token);
        declare_same("__memcpy is memcpy", __memcpy, memcpy);
#endif
}

#elif defined(SHARED_signal_cases)
/*
        Experimental C standard library

        The cases for <signal.h>, and for the half of setjmp that was missing

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_TEST_SIGNAL_CASES
#define STANDARD_MODERN_C_TEST_SIGNAL_CASES

/*
        One list of cases, two libraries under it, exactly as
        CHECK_strings arranges the same thing for <string.h>.

        CHECK_signal builds this against the library, freestanding, on
        x86_64, arm64 and riscv64. CHECK_signal_reference builds the same
        lines against the host's glibc. Both print the same verdict, and an
        expectation written here that only this library satisfies fails the
        reference build rather than quietly agreeing with a wrong
        implementation.

        Everything below is reached through sig_ names -- sig_set for a set of
        signals, sig_action for a disposition, one macro per routine -- so
        nothing here mentions sigprocmask or signal_mask_change and nothing
        here knows which library it is testing.

        The two places the libraries genuinely differ are kept OUT of this
        file and are checked separately in each host, because a shared case
        can only ask a question both can answer:

          signals 32 and 33. glibc reserves them for thread cancellation and
          identity changes and leaves them out of a full set; this library has
          no threads and fills all sixty four. So no case here asks what a
          full set contains at 32 or 33.

          SA_RESTORER on the way back. glibc hands the caller the restorer bit
          it set itself, so a disposition read back on x86_64 has 0x14000000
          where arm64 and riscv64 have 0x10000000. This library clears it, and
          answers 0x10000000 on all three. So the cases compare only the flags
          a caller is allowed to set, through sig_flags_asked.

        Signals 61 to 64 are also avoided throughout. qemu-user reserves the
        top few real-time signals for its own use and a mask set through it
        does not read back with them, which is an emulator artefact and not a
        disagreement about libraries.
*/

/*
        What the handlers write down.

        File scope and volatile, not locals. A handler runs between two
        arbitrary instructions of whatever it interrupted, so anything it
        touches has to be a real store to a real address rather than a value
        the compiler is entitled to keep in a register across the raise.
*/
static volatile int sig_first_ran;
static volatile int sig_second_ran;
static volatile int sig_second_seen_inside_first;
static volatile int sig_alarm_ran;
static volatile int sig_suspend_ran;
static volatile int sig_ignored_ran;

static sig_jump_state sig_place;

static void sig_note_first(int number)
{
        sig_first_ran = number;
}

/*
        Raises the second signal from inside the first, then looks to see
        whether it ran. That look is the whole of the sa_mask case: if the
        mask went where the kernel reads masks, the second handler cannot have
        run yet and the answer is zero.
*/
static void sig_note_first_and_raise(int number)
{
        sig_first_ran = number;
        sig_raise(SIGUSR2);
        sig_second_seen_inside_first = sig_second_ran;
}

static void sig_note_second(int number)
{
        sig_second_ran = number;
}

static void sig_note_alarm(int number)
{
        sig_alarm_ran = number;
}

static void sig_note_suspend(int number)
{
        sig_suspend_ran = number;
}

static void sig_note_ignored(int number)
{
        sig_ignored_ran = number;
}

//      Leaves through the mark rather than returning, which is the case
//      sigsetjmp's second argument exists for.
static void sig_leave_from_handler(int number)
{
        sig_first_ran = number;
        sig_jump_to_mark(sig_place, 1);
}

/*
        Every case starts from a known disposition and a known mask, so that
        one failing case cannot make the next one fail for a reason that is
        not its own.

        The mask is SET to empty rather than having the three signals used
        here unblocked out of it. Unblocking a list is enough today and stops
        being enough the moment a case is added that blocks a fourth signal --
        and the way that failure presents is the NEXT case hanging in
        sigsuspend or losing a raise, which reads as a bug in whatever was
        added last rather than in this function.
*/
static void sig_case_reset(void)
{
        sig_set none;

        sig_first_ran = 0;
        sig_second_ran = 0;
        sig_second_seen_inside_first = 0;
        sig_alarm_ran = 0;
        sig_suspend_ran = 0;
        sig_ignored_ran = 0;

        sig_set_empty(&none);
        sig_mask_change(SIG_SETMASK, &none, 0);

        sig_handle(SIGUSR1, SIG_DFL);
        sig_handle(SIGUSR2, SIG_DFL);
        sig_handle(SIGALRM, SIG_DFL);
        sig_alarm(0);
}

/*
        The five set operations, which are the only routines here that touch
        no kernel at all.

        The bounds are the interesting part. One and sixty four are signals;
        zero and sixty five are not, and both ends answer EINVAL. That is not
        obvious from the numbering -- sixty four signals numbered from one
        means the highest valid number equals the count, and an off by one in
        either direction is a set operation that silently touches the wrong
        bit or refuses a real signal.
*/
static void sig_case_set_operations(void)
{
        sig_set set;
        int number;
        int missing = 0;

        sig_set_empty(&set);

        for (number = 1; number <= 31; number++)
                if (sig_set_has(&set, number) != 0)
                        missing++;

        check("an empty set holds nothing from 1 to 31", missing == 0);

        sig_set_fill(&set);

        missing = 0;

        for (number = 1; number <= 31; number++)
                if (sig_set_has(&set, number) != 1)
                        missing++;

        for (number = 34; number <= 60; number++)
                if (sig_set_has(&set, number) != 1)
                        missing++;

        check("a full set holds every standard and queued signal",
              missing == 0);

        sig_set_empty(&set);

        check("adding one signal", sig_set_add(&set, SIGUSR1) == 0);
        check("the added signal is a member", sig_set_has(&set, SIGUSR1) == 1);
        check("no other signal came with it",
              sig_set_has(&set, SIGUSR2) == 0);
        check("removing it", sig_set_remove(&set, SIGUSR1) == 0);
        check("and then it is not a member",
              sig_set_has(&set, SIGUSR1) == 0);

        check("signal one is a signal", sig_set_add(&set, 1) == 0);
        check("and reads back", sig_set_has(&set, 1) == 1);
        check("signal sixty four is a signal", sig_set_add(&set, 64) == 0);
        check("and reads back", sig_set_has(&set, 64) == 1);

        sig_errno = 0;
        check("zero is not a signal to add", sig_set_add(&set, 0) == -1);
        check("and says why", sig_errno == EINVAL);

        sig_errno = 0;
        check("sixty five is not a signal to add",
              sig_set_add(&set, 65) == -1);
        check("and says why", sig_errno == EINVAL);

        sig_errno = 0;
        check("nor to remove", sig_set_remove(&set, 0) == -1);
        check("and says why", sig_errno == EINVAL);

        sig_errno = 0;
        check("nor to ask about", sig_set_has(&set, 65) == -1);
        check("and says why", sig_errno == EINVAL);

        sig_errno = 0;
        check("a negative number is not a signal",
              sig_set_add(&set, -1) == -1);
        check("and says why", sig_errno == EINVAL);
}

/*
        A handler that runs, and -- the part that is not free -- returns.

        Returning from a signal handler is a system call the program never
        wrote: the kernel put a return address on the frame and something has
        to be at it that asks for the interrupted registers back. If that
        address is wrong the process dies here rather than reaching the line
        below, so every check after the raise is also a check that the return
        path exists.
*/
static void sig_case_handler_returns(void)
{
        sig_case_reset();

        check("installing a handler",
              sig_handle(SIGUSR1, sig_note_first) != SIG_ERR);
        check("raising it", sig_raise(SIGUSR1) == 0);
        check("the handler ran", sig_first_ran == SIGUSR1);

        sig_case_reset();
}

/*
        The disposition round trip, which is what signal returns.

        Three installs and three answers: the default first, then the handler
        that replaced it, then SIG_IGN that replaced that. A libc that returns
        the NEW disposition, or that returns zero always, passes a test that
        only checks the handler ran.
*/
static void sig_case_disposition(void)
{
        sig_handler previous;

        sig_case_reset();

        previous = sig_handle(SIGUSR1, sig_note_first);
        check("the first install reports the default", previous == SIG_DFL);

        previous = sig_handle(SIGUSR1, SIG_IGN);
        check("the second reports the handler", previous == sig_note_first);

        sig_ignored_ran = 0;
        check("raising an ignored signal succeeds", sig_raise(SIGUSR1) == 0);
        check("and nothing ran", sig_first_ran == 0);

        previous = sig_handle(SIGUSR1, SIG_DFL);
        check("and the third reports SIG_IGN", previous == SIG_IGN);

        sig_case_reset();
}

/*
        sigaction, read back field by field.

        The mask is the field this is really about. It is also the field that
        a single four-word structure gets wrong on exactly one of the three
        machines, because riscv64's kernel struct sigaction has no restorer
        slot and its mask therefore sits eight bytes earlier than on x86_64
        and arm64. A read back that finds the mask where the caller put it is
        the cheapest proof that both layouts are right.
*/
static void sig_case_action_round_trip(void)
{
        sig_action wanted;
        sig_action previous;

        sig_case_reset();

        sig_zero(&wanted, sizeof wanted);
        sig_zero(&previous, sizeof previous);

        sig_handler_of(wanted) = sig_note_first;
        sig_flags_of(wanted) = SA_RESTART;
        sig_set_empty(&sig_mask_of(wanted));
        sig_set_add(&sig_mask_of(wanted), SIGUSR2);
        sig_set_add(&sig_mask_of(wanted), SIGALRM);

        check("installing a full disposition",
              sig_action_change(SIGUSR1, &wanted, 0) == 0);
        check("reading it back",
              sig_action_change(SIGUSR1, 0, &previous) == 0);

        check("the handler came back",
              sig_handler_of(previous) == sig_note_first);
        check("the flags a caller set came back",
              sig_flags_asked(previous) == SA_RESTART);
        check("the mask came back",
              sig_set_has(&sig_mask_of(previous), SIGUSR2) == 1);
        check("and the rest of the mask came back",
              sig_set_has(&sig_mask_of(previous), SIGALRM) == 1);
        check("and nothing else got into it",
              sig_set_has(&sig_mask_of(previous), SIGINT) == 0);

        check("null for both is a legal enquiry about nothing",
              sig_action_change(SIGUSR1, 0, 0) == 0);

        sig_case_reset();
}

/*
        The mask a disposition carries, which is the case this whole family
        was written around.

        SIGUSR1's handler is installed with SIGUSR2 in its mask, and the
        handler raises SIGUSR2 and then looks. If the mask arrived, the second
        handler cannot have run: the kernel is holding SIGUSR2 back until the
        first handler returns. If the mask did not arrive -- if it was written
        into a restorer slot the kernel does not read as a mask -- the second
        handler runs immediately, nested inside the first, and the flag it
        sets is already there when the first looks.

        Both outcomes leave a program that works. Only one of them is what the
        caller asked for, and the difference is invisible to any test that
        checks only that handlers run.
*/
static void sig_case_action_mask(void)
{
        sig_action wanted;

        sig_case_reset();

        sig_zero(&wanted, sizeof wanted);
        sig_handler_of(wanted) = sig_note_second;
        sig_flags_of(wanted) = SA_RESTART;
        sig_set_empty(&sig_mask_of(wanted));
        check("the second signal gets a handler",
              sig_action_change(SIGUSR2, &wanted, 0) == 0);

        sig_zero(&wanted, sizeof wanted);
        sig_handler_of(wanted) = sig_note_first_and_raise;
        sig_flags_of(wanted) = SA_RESTART;
        sig_set_empty(&sig_mask_of(wanted));
        sig_set_add(&sig_mask_of(wanted), SIGUSR2);
        check("the first gets one that blocks the second",
              sig_action_change(SIGUSR1, &wanted, 0) == 0);

        check("raising the first", sig_raise(SIGUSR1) == 0);

        check("the first handler ran", sig_first_ran == SIGUSR1);
        check("the second was held back while it did",
              sig_second_seen_inside_first == 0);
        check("and arrived once the first returned",
              sig_second_ran == SIGUSR2);

        sig_case_reset();
}

/*
        A blocked signal, which does not go away and does not arrive.

        Three separate claims and each one is a way for a mask to be wrong:
        the handler has not run, sigpending says the signal is waiting, and
        unblocking delivers it before sigprocmask returns rather than at some
        later point.
*/
static void sig_case_blocked_stays_pending(void)
{
        sig_set block;
        sig_set pending;

        sig_case_reset();

        check("a handler for the signal about to be blocked",
              sig_handle(SIGUSR2, sig_note_second) != SIG_ERR);

        sig_set_empty(&block);
        sig_set_add(&block, SIGUSR2);

        check("blocking it", sig_mask_change(SIG_BLOCK, &block, 0) == 0);
        check("raising it while blocked", sig_raise(SIGUSR2) == 0);
        check("nothing ran", sig_second_ran == 0);

        sig_set_empty(&pending);
        check("asking what is pending", sig_mask_pending(&pending) == 0);
        check("the blocked signal is pending",
              sig_set_has(&pending, SIGUSR2) == 1);
        check("and nothing else is", sig_set_has(&pending, SIGUSR1) == 0);

        check("unblocking it", sig_mask_change(SIG_UNBLOCK, &block, 0) == 0);
        check("delivered it", sig_second_ran == SIGUSR2);

        sig_set_empty(&pending);
        check("asking again", sig_mask_pending(&pending) == 0);
        check("and nothing is pending now",
              sig_set_has(&pending, SIGUSR2) == 0);

        sig_case_reset();
}

/*
        The three things how can be, and the one thing it cannot.

        SIG_SETMASK is the one worth watching: it replaces rather than adds,
        so a library that implemented all three as an or would pass the first
        two cases and fail this one.
*/
static void sig_case_mask_change(void)
{
        sig_set one;
        sig_set two;
        sig_set had;
        sig_set none;

        sig_case_reset();

        sig_set_empty(&none);
        sig_set_empty(&one);
        sig_set_add(&one, SIGUSR1);
        sig_set_empty(&two);
        sig_set_add(&two, SIGUSR2);

        check("starting from nothing blocked",
              sig_mask_change(SIG_SETMASK, &none, 0) == 0);

        check("blocking one", sig_mask_change(SIG_BLOCK, &one, &had) == 0);
        check("and being told what was blocked before",
              sig_set_has(&had, SIGUSR1) == 0);

        check("blocking a second", sig_mask_change(SIG_BLOCK, &two, &had) == 0);
        check("the first is still blocked", sig_set_has(&had, SIGUSR1) == 1);

        check("reading the mask without changing it",
              sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("both are blocked",
              sig_set_has(&had, SIGUSR1) == 1 &&
                      sig_set_has(&had, SIGUSR2) == 1);

        check("setting replaces rather than adds",
              sig_mask_change(SIG_SETMASK, &two, &had) == 0);
        check("reading it back", sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("only the one set is blocked",
              sig_set_has(&had, SIGUSR2) == 1 &&
                      sig_set_has(&had, SIGUSR1) == 0);

        check("unblocking", sig_mask_change(SIG_UNBLOCK, &two, 0) == 0);
        check("reading it back", sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("nothing is blocked", sig_set_has(&had, SIGUSR2) == 0);

        sig_errno = 0;
        check("a how that is not one of the three fails",
              sig_mask_change(77, &one, 0) == -1);
        check("and says why", sig_errno == EINVAL);
        check("and changed nothing",
              sig_mask_change(SIG_BLOCK, 0, &had) == 0 &&
                      sig_set_has(&had, SIGUSR1) == 0);

        sig_case_reset();
}

/*
        sigsuspend, whose success is spelled -1 and EINTR.

        The signal is blocked, raised so that it is pending, and then the mask
        handed to sigsuspend leaves it out -- so the pending signal is
        delivered the moment the new mask takes effect, the handler runs, and
        sigsuspend comes back having restored the old mask. The last check is
        that restoration: the signal is blocked again afterwards, because it
        was blocked before.
*/
static void sig_case_suspend(void)
{
        sig_set block;
        sig_set open;
        sig_set had;

        sig_case_reset();

        check("a handler", sig_handle(SIGUSR1, sig_note_suspend) != SIG_ERR);

        sig_set_empty(&block);
        sig_set_add(&block, SIGUSR1);
        sig_set_empty(&open);

        check("blocking it", sig_mask_change(SIG_SETMASK, &block, 0) == 0);
        check("raising it while blocked", sig_raise(SIGUSR1) == 0);
        check("nothing has run", sig_suspend_ran == 0);

        sig_errno = 0;
        check("suspending with it unblocked returns",
              sig_mask_suspend(&open) == -1);
        check("interrupted, which is the only way out", sig_errno == EINTR);
        check("and the handler ran", sig_suspend_ran == SIGUSR1);

        check("reading the mask back", sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("suspend put the old mask back",
              sig_set_has(&had, SIGUSR1) == 1);

        sig_set_empty(&open);
        check("clearing up", sig_mask_change(SIG_SETMASK, &open, 0) == 0);

        sig_case_reset();
}

/*
        alarm, which is a timer on two of the three machines and a system call
        on the other, and pause, which is a system call on one and a suspend
        on the other two.

        A whole second of real waiting, and it is not skippable: proving that
        SIGALRM arrives means letting it arrive. pause is what waits, and its
        return is the second claim -- it too answers -1 and EINTR and never
        anything else.

        The remaining-seconds answer is checked by arming for two seconds and
        immediately cancelling. glibc rounds the leftover up to whole seconds,
        so an alarm set a microsecond ago has two seconds left rather than
        one, and rounding the other way would report zero for an alarm that
        was certainly armed.
*/
static void sig_case_alarm_and_pause(void)
{
        unsigned int left;

        sig_case_reset();

        check("cancelling an alarm nobody set", sig_alarm(0) == 0);

        left = sig_alarm(2);
        check("arming one reports no previous", left == 0);

        left = sig_alarm(0);
        check("cancelling it reports what was left, rounded up", left == 2);

        check("a handler for it",
              sig_handle(SIGALRM, sig_note_alarm) != SIG_ERR);

        left = sig_alarm(1);
        check("arming again", left == 0);

        sig_errno = 0;
        check("pause waits and then reports being interrupted",
              sig_wait() == -1);
        check("and says why", sig_errno == EINTR);
        check("the alarm arrived", sig_alarm_ran == SIGALRM);

        sig_case_reset();
}

/*
        The mask across a mark and a jump, which is the whole reason
        sigsetjmp is a different routine from setjmp.

        Twice, because the second argument is what decides it. With a one, the
        mask in force at the mark is written down and put back by the jump, so
        the unblocking done in between is undone. With a zero, nothing is
        written down and the unblocking survives -- which is setjmp's own
        behaviour and is what this library's setjmp has always done.

        A library that saved the mask unconditionally passes the first half
        and fails the second; one that ignored the argument passes the second
        and fails the first. Neither half alone says anything.
*/
static void sig_case_jump_mask(void)
{
        sig_set one;
        sig_set had;
        volatile int arrivals;

        sig_case_reset();

        sig_set_empty(&one);
        sig_set_add(&one, SIGUSR1);
        sig_set_empty(&had);

        check("blocking a signal before the mark",
              sig_mask_change(SIG_SETMASK, &one, 0) == 0);

        arrivals = 0;

        if (sig_jump_mark(sig_place, 1) == 0)
        {
                arrivals = 1;
                sig_mask_change(SIG_UNBLOCK, &one, 0);
                sig_jump_to_mark(sig_place, 3);
                check("the jump did not return", 0);
        }

        check("the mark was reached twice", arrivals == 1);
        check("reading the mask after the jump",
              sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("asking for the mask put it back",
              sig_set_has(&had, SIGUSR1) == 1);

        check("blocking again", sig_mask_change(SIG_SETMASK, &one, 0) == 0);

        arrivals = 0;

        if (sig_jump_mark(sig_place, 0) == 0)
        {
                arrivals = 1;
                sig_mask_change(SIG_UNBLOCK, &one, 0);
                sig_jump_to_mark(sig_place, 4);
                check("the jump did not return", 0);
        }

        check("the mark was reached twice", arrivals == 1);
        check("reading the mask after the jump",
              sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("not asking left it alone", sig_set_has(&had, SIGUSR1) == 0);

        sig_case_reset();
}

//      The value the jump carries, and the one value it may not carry.
static void sig_case_jump_value(void)
{
        volatile int arrivals;
        int landed;

        sig_case_reset();

        arrivals = 0;
        landed = sig_jump_mark(sig_place, 1);

        if (landed == 0)
        {
                arrivals = 1;
                sig_jump_to_mark(sig_place, 42);
        }

        check("the mark answered zero the first time", arrivals == 1);
        check("and the jump's own value the second", landed == 42);

        arrivals = 0;
        landed = sig_jump_mark(sig_place, 0);

        if (landed == 0)
        {
                arrivals = 1;
                sig_jump_to_mark(sig_place, 0);
        }

        check("the mark answered zero the first time", arrivals == 1);
        check("a jump asking for zero arrives as one", landed == 1);

        sig_case_reset();
}

/*
        Leaving a handler by jumping out of it, which is the shape every
        program that recovers from SIGSEGV or times out a read is written in.

        The kernel blocks the signal being delivered for as long as its
        handler runs, and a handler that jumps out instead of returning never
        reaches the return that would unblock it. So a plain longjmp out of a
        SIGUSR1 handler leaves SIGUSR1 blocked for the rest of the process,
        and the second SIGUSR1 never arrives -- silently, once, much later.

        That is the bug sigsetjmp exists to not have, and the last two checks
        are the whole of it: after jumping out, the signal is not blocked, and
        raising it again works.
*/
static void sig_case_jump_from_handler(void)
{
        sig_set had;
        volatile int arrivals;
        int landed;

        sig_case_reset();

        check("a handler that leaves rather than returns",
              sig_handle(SIGUSR1, sig_leave_from_handler) != SIG_ERR);

        arrivals = 0;
        landed = sig_jump_mark(sig_place, 1);

        if (landed == 0)
        {
                arrivals = 1;
                sig_raise(SIGUSR1);
                check("the handler did not come back", 0);
        }

        check("the mark was reached twice", arrivals == 1);
        check("with the value the handler jumped with", landed == 1);
        check("and the handler had run", sig_first_ran == SIGUSR1);

        check("reading the mask", sig_mask_change(SIG_BLOCK, 0, &had) == 0);
        check("the signal is not still blocked",
              sig_set_has(&had, SIGUSR1) == 0);

        sig_first_ran = 0;
        check("a handler that returns this time",
              sig_handle(SIGUSR1, sig_note_first) != SIG_ERR);
        check("raising it again", sig_raise(SIGUSR1) == 0);
        check("and it arrives", sig_first_ran == SIGUSR1);

        sig_case_reset();
}

static void sig_cases_all(void)
{
        sig_case_set_operations();
        sig_case_handler_returns();
        sig_case_disposition();
        sig_case_action_round_trip();
        sig_case_action_mask();
        sig_case_blocked_stays_pending();
        sig_case_mask_change();
        sig_case_suspend();
        sig_case_alarm_and_pause();
        sig_case_jump_mask();
        sig_case_jump_value();
        sig_case_jump_from_handler();
}

#endif // STANDARD_MODERN_C_TEST_SIGNAL_CASES

#elif defined(SHARED_strings_cases)
/*
        Experimental C standard library

        The cases for the rest of <string.h>, and for setjmp

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_TEST_STRINGS_CASES
#define STANDARD_MODERN_C_TEST_STRINGS_CASES

/*
        One list of cases, two libraries under it.

        CHECK_strings builds this against the library, freestanding, on all
        three machines. CHECK_strings_reference builds the same lines
        against the host's glibc. Both print the same verdict, and the point
        of the arrangement is that an expectation written here has to be true
        of glibc as well -- a wrong expectation fails the reference build
        rather than quietly agreeing with a wrong implementation, which is
        exactly the failure a hand-written expected-value table cannot catch.

        The two hosts supply everything below through text_ names: text_string
        for a string pointer, text_byte for one of its bytes, text_size for a
        length, TX() to make a literal one of them, and one macro per routine.
        Nothing here mentions strdup or string_duplicate, so nothing here
        knows which library it is testing.

        Cases that glibc cannot be asked -- the null start strtok_r rejects
        instead of faulting -- are marked where they appear and are the only
        places the two builds are not answering the identical question.
*/

/*
        strdup, whose only two questions are whether the bytes arrived and
        whether they arrived somewhere else.
*/
static void text_case_duplicate(void)
{
        text_byte source[] = "abcdef";
        text_string one = text_duplicate(TX("hello"));
        text_string two = text_duplicate(TX(""));
        text_string three = text_duplicate(source);

        check("strdup content", one != 0 && text_equal(one, TX("hello")));
        check("strdup length", one != 0 && text_length(one) == 5);
        check("strdup empty length", two != 0 && text_length(two) == 0);
        check("strdup empty terminator", two != 0 && two[0] == 0);
        check("strdup is a copy", three != 0 && three != (text_string)source);
        check("strdup copy content",
              three != 0 && text_equal(three, TX("abcdef")));
        check("strdup copy is writable", three != 0 && (three[0] = 'z') == 'z');
        check("strdup copy did not disturb its source", source[0] == 'a');
}

/*
        strndup, and the one byte that is not in the bound.

        The block is the bound plus a terminator, so strndup(s, 3) on "hello"
        is four bytes and not three. The last case has a source that is
        exactly its own array with no bytes after it, so a bound equal to the
        length proves nothing is read past the terminator.
*/
static void text_case_duplicate_max(void)
{
        text_byte source[] = "hello";
        text_byte tight[4] = {'a', 'b', 'c', 0};
        text_string a = text_duplicate_max(source, 3);
        text_string b = text_duplicate_max(source, 5);
        text_string c = text_duplicate_max(source, 40);
        text_string d = text_duplicate_max(source, 0);
        text_string e = text_duplicate_max(tight, 3);

        check("strndup short bound", a != 0 && text_equal(a, TX("hel")));
        check("strndup short length", a != 0 && text_length(a) == 3);
        check("strndup exact bound", b != 0 && text_equal(b, TX("hello")));
        check("strndup bound past the end", c != 0 && text_equal(c, TX("hello")));
        check("strndup zero bound", d != 0 && text_length(d) == 0);
        check("strndup zero bound terminator", d != 0 && d[0] == 0);
        check("strndup stops at the terminator", e != 0 && text_equal(e, TX("abc")));
}

/*
        strtok_r: runs of delimiters are one delimiter, and the input is
        destroyed as it goes.

        The last group is the reason strtok_r exists at all -- two walks
        interleaved, each carrying its own place, neither seeing the other.
*/
static void text_case_token_next(void)
{
        text_byte one[] = "a,b,,c";
        text_byte two[] = "  hello   world  ";
        text_byte three[] = ",,,";
        text_byte four[] = "";
        text_byte five[] = "abc";
        text_byte six[] = "a;b,c";
        text_byte left[] = "1 2 3";
        text_byte right[] = "x y z";
        text_string place = 0;
        text_string left_place = 0;
        text_string right_place = 0;
        text_string got;

        got = text_token_next(one, TX(","), &place);
        check("strtok_r first token", got != 0 && text_equal(got, TX("a")));
        got = text_token_next(0, TX(","), &place);
        check("strtok_r second token", got != 0 && text_equal(got, TX("b")));
        got = text_token_next(0, TX(","), &place);
        check("strtok_r skips the empty field", got != 0 && text_equal(got, TX("c")));
        got = text_token_next(0, TX(","), &place);
        check("strtok_r exhausted", got == 0);
        got = text_token_next(0, TX(","), &place);
        check("strtok_r stays exhausted", got == 0);
        check("strtok_r terminated the first token", one[1] == 0);
        check("strtok_r terminated the second token", one[3] == 0);
        check("strtok_r left the skipped delimiter alone", one[4] == ',');

        place = 0;
        got = text_token_next(two, TX(" "), &place);
        check("strtok_r skips leading blanks", got != 0 && text_equal(got, TX("hello")));
        got = text_token_next(0, TX(" "), &place);
        check("strtok_r crosses a run of blanks", got != 0 && text_equal(got, TX("world")));
        got = text_token_next(0, TX(" "), &place);
        check("strtok_r ends on trailing blanks", got == 0);

        place = 0;
        got = text_token_next(three, TX(","), &place);
        check("strtok_r all delimiters", got == 0);

        place = 0;
        got = text_token_next(four, TX(","), &place);
        check("strtok_r empty input", got == 0);

        place = 0;
        got = text_token_next(five, TX(""), &place);
        check("strtok_r empty delimiters", got != 0 && text_equal(got, TX("abc")));
        got = text_token_next(0, TX(""), &place);
        check("strtok_r empty delimiters once only", got == 0);

        place = 0;
        got = text_token_next(six, TX(",;"), &place);
        check("strtok_r two delimiters first", got != 0 && text_equal(got, TX("a")));
        got = text_token_next(0, TX(",;"), &place);
        check("strtok_r two delimiters second", got != 0 && text_equal(got, TX("b")));
        got = text_token_next(0, TX(",;"), &place);
        check("strtok_r two delimiters third", got != 0 && text_equal(got, TX("c")));
        got = text_token_next(0, TX(",;"), &place);
        check("strtok_r two delimiters done", got == 0);

        got = text_token_next(left, TX(" "), &left_place);
        check("strtok_r interleaved left first", got != 0 && text_equal(got, TX("1")));
        got = text_token_next(right, TX(" "), &right_place);
        check("strtok_r interleaved right first", got != 0 && text_equal(got, TX("x")));
        got = text_token_next(0, TX(" "), &left_place);
        check("strtok_r interleaved left second", got != 0 && text_equal(got, TX("2")));
        got = text_token_next(0, TX(" "), &right_place);
        check("strtok_r interleaved right second", got != 0 && text_equal(got, TX("y")));
        got = text_token_next(0, TX(" "), &left_place);
        check("strtok_r interleaved left third", got != 0 && text_equal(got, TX("3")));
        got = text_token_next(0, TX(" "), &right_place);
        check("strtok_r interleaved right third", got != 0 && text_equal(got, TX("z")));
        got = text_token_next(0, TX(" "), &left_place);
        check("strtok_r interleaved left done", got == 0);
        got = text_token_next(0, TX(" "), &right_place);
        check("strtok_r interleaved right done", got == 0);
}

/*
        strtok, which is strtok_r with one place shared by the whole program.
*/
static void text_case_token(void)
{
        text_byte one[] = "one:two::three";
        text_string got;

        got = text_token(one, TX(":"));
        check("strtok first", got != 0 && text_equal(got, TX("one")));
        got = text_token(0, TX(":"));
        check("strtok second", got != 0 && text_equal(got, TX("two")));
        got = text_token(0, TX(":"));
        check("strtok skips the empty field", got != 0 && text_equal(got, TX("three")));
        got = text_token(0, TX(":"));
        check("strtok exhausted", got == 0);
}

/*
        strsep, which keeps every field strtok throws away.

        The same input as the first strtok_r group, so the two answers can be
        read against each other: four fields here where there were three
        tokens there, and the difference is the one between them.
*/
static void text_case_split(void)
{
        text_byte one[] = "a,b,,c";
        text_byte empty[] = "";
        text_byte plain[] = "abc";
        text_byte leading[] = ",a";
        text_byte trailing[] = "a,";
        text_string walk;
        text_string got;

        walk = one;
        got = text_split_next(&walk, TX(","));
        check("strsep first field", got != 0 && text_equal(got, TX("a")));
        got = text_split_next(&walk, TX(","));
        check("strsep second field", got != 0 && text_equal(got, TX("b")));
        got = text_split_next(&walk, TX(","));
        check("strsep keeps the empty field", got != 0 && text_equal(got, TX("")));
        got = text_split_next(&walk, TX(","));
        check("strsep last field", got != 0 && text_equal(got, TX("c")));
        check("strsep holder is null at the end", walk == 0);
        got = text_split_next(&walk, TX(","));
        check("strsep past the end", got == 0);

        walk = empty;
        got = text_split_next(&walk, TX(","));
        check("strsep empty input is one empty field",
              got != 0 && text_equal(got, TX("")));
        check("strsep empty input ends the walk", walk == 0);
        got = text_split_next(&walk, TX(","));
        check("strsep empty input past the end", got == 0);

        walk = plain;
        got = text_split_next(&walk, TX(","));
        check("strsep with no delimiter", got != 0 && text_equal(got, TX("abc")));
        check("strsep with no delimiter ends the walk", walk == 0);

        walk = plain;
        got = text_split_next(&walk, TX(""));
        check("strsep empty delimiters", got != 0 && text_equal(got, TX("abc")));
        check("strsep empty delimiters ends the walk", walk == 0);

        walk = leading;
        got = text_split_next(&walk, TX(","));
        check("strsep leading delimiter is an empty field",
              got != 0 && text_equal(got, TX("")));
        got = text_split_next(&walk, TX(","));
        check("strsep after a leading delimiter",
              got != 0 && text_equal(got, TX("a")));
        check("strsep leading delimiter ends the walk", walk == 0);

        walk = trailing;
        got = text_split_next(&walk, TX(","));
        check("strsep before a trailing delimiter",
              got != 0 && text_equal(got, TX("a")));
        got = text_split_next(&walk, TX(","));
        check("strsep trailing delimiter is an empty field",
              got != 0 && text_equal(got, TX("")));
        check("strsep trailing delimiter ends the walk", walk == 0);
}

/*
        strcasestr, including the long haystack and the four byte needle that
        together reach the anchored path rather than the byte loop.
*/
static void text_case_search_folded(void)
{
        text_byte hay[] = "Hello World";
        text_byte overlap[] = "aaaB";
        text_byte brackets[] = "Z_[]";
        text_byte long_hay[] =
            "the quick brown fox jumps over the lazy dog and then some "
            "more filler so this is well past sixty four bytes HERE";
        text_string got;

        got = text_search_folded(hay, TX("world"));
        check("strcasestr folds the needle", got == hay + 6);
        got = text_search_folded(hay, TX("WORLD"));
        check("strcasestr folds the haystack", got == hay + 6);
        got = text_search_folded(hay, TX("hello world"));
        check("strcasestr whole haystack", got == hay);
        got = text_search_folded(hay, TX(""));
        check("strcasestr empty needle is the front", got == hay);
        got = text_search_folded(hay, TX("xyz"));
        check("strcasestr absent needle", got == 0);
        got = text_search_folded(hay, TX("Hello World and more"));
        check("strcasestr needle longer than haystack", got == 0);

        got = text_search_folded(overlap, TX("aab"));
        check("strcasestr overlapping candidate", got == overlap + 1);

        got = text_search_folded(brackets, TX("z_"));
        check("strcasestr underscore is itself", got == brackets);
        got = text_search_folded(brackets, TX("z["));
        check("strcasestr bracket is not a letter", got == 0);

        got = text_search_folded(long_hay, TX("here"));
        check("strcasestr long haystack",
              got == long_hay + text_length(long_hay) - 4);
        got = text_search_folded(long_hay, TX("QUICK"));
        check("strcasestr long haystack early hit", got == long_hay + 4);
        got = text_search_folded(long_hay, TX("zebra"));
        check("strcasestr long haystack miss", got == 0);
}

/*
        strlcpy: the answer is what the source wanted, and the buffer always
        ends inside itself.

        Every case fills the buffer with a guard byte first, so a routine that
        wrote one byte too many is caught by the guard rather than by luck.
*/
static void text_case_copy_bounded(void)
{
        text_byte buffer[16];
        text_size answer;

        text_fill(buffer, 0x7e, sizeof buffer);
        answer = text_copy_bounded(buffer, TX("hello"), 10);
        check("strlcpy answer is the source length", answer == 5);
        check("strlcpy content", text_equal(buffer, TX("hello")));
        check("strlcpy wrote no further", buffer[6] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        answer = text_copy_bounded(buffer, TX("hello"), 6);
        check("strlcpy exact fit answer", answer == 5);
        check("strlcpy exact fit content", text_equal(buffer, TX("hello")));
        check("strlcpy exact fit wrote no further", buffer[6] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        answer = text_copy_bounded(buffer, TX("hello"), 3);
        check("strlcpy truncated answer", answer == 5);
        check("strlcpy truncated content", text_equal(buffer, TX("he")));
        check("strlcpy truncated wrote no further", buffer[3] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        answer = text_copy_bounded(buffer, TX("hello"), 1);
        check("strlcpy capacity one answer", answer == 5);
        check("strlcpy capacity one is empty", buffer[0] == 0);
        check("strlcpy capacity one wrote no further", buffer[1] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        answer = text_copy_bounded(buffer, TX("hello"), 0);
        check("strlcpy capacity zero answer", answer == 5);
        check("strlcpy capacity zero writes nothing", buffer[0] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        answer = text_copy_bounded(buffer, TX(""), 4);
        check("strlcpy empty source answer", answer == 0);
        check("strlcpy empty source content", buffer[0] == 0);
        check("strlcpy empty source wrote no further", buffer[1] == 0x7e);
}

/*
        strlcat, and the destination that holds no terminator inside the
        capacity -- which is where an implementation either notices or walks
        off the end of a buffer.
*/
static void text_case_append_bounded(void)
{
        text_byte buffer[16];
        text_size answer;

        text_fill(buffer, 0x7e, sizeof buffer);
        buffer[0] = 'a';
        buffer[1] = 'b';
        buffer[2] = 0;
        answer = text_append_bounded(buffer, TX("cd"), 10);
        check("strlcat answer is the whole length", answer == 4);
        check("strlcat content", text_equal(buffer, TX("abcd")));
        check("strlcat wrote no further", buffer[5] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        buffer[0] = 'a';
        buffer[1] = 'b';
        buffer[2] = 0;
        answer = text_append_bounded(buffer, TX("cdef"), 5);
        check("strlcat truncated answer", answer == 6);
        check("strlcat truncated content", text_equal(buffer, TX("abcd")));
        check("strlcat truncated wrote no further", buffer[5] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        buffer[0] = 'a';
        buffer[1] = 'b';
        buffer[2] = 0;
        answer = text_append_bounded(buffer, TX("cd"), 3);
        check("strlcat no room answer", answer == 4);
        check("strlcat no room content", text_equal(buffer, TX("ab")));
        check("strlcat no room wrote no further", buffer[3] == 0x7e);

        text_fill(buffer, 0x7e, sizeof buffer);
        buffer[0] = 'a';
        buffer[1] = 'b';
        buffer[2] = 0;
        answer = text_append_bounded(buffer, TX("cd"), 2);
        check("strlcat capacity equals the held length", answer == 4);
        check("strlcat capacity equals held writes nothing",
              text_equal(buffer, TX("ab")));

        text_fill(buffer, 0x7e, sizeof buffer);
        buffer[0] = 'a';
        buffer[1] = 'b';
        buffer[2] = 0;
        answer = text_append_bounded(buffer, TX("cd"), 0);
        check("strlcat capacity zero answer", answer == 2);
        check("strlcat capacity zero writes nothing",
              text_equal(buffer, TX("ab")));

        text_fill(buffer, 'x', sizeof buffer);
        answer = text_append_bounded(buffer, TX("y"), 4);
        check("strlcat unterminated destination answer", answer == 5);
        check("strlcat unterminated destination untouched",
              buffer[0] == 'x' && buffer[3] == 'x' && buffer[4] == 'x');

        text_fill(buffer, 0x7e, sizeof buffer);
        buffer[0] = 0;
        answer = text_append_bounded(buffer, TX("cd"), 8);
        check("strlcat onto an empty destination answer", answer == 2);
        check("strlcat onto an empty destination content",
              text_equal(buffer, TX("cd")));

        /* Every truncation point, including a destination with no bounded
           terminator; compare all bytes so padding and overruns cannot hide. */
        for (text_size held = 0; held < 16; held++)
                for (text_size length = 0; length < 16; length++)
                        for (text_size capacity = 0; capacity <= 16; capacity++)
                        {
                                text_byte source[16], expected[16];
                                text_size seen = held < capacity ? held : capacity;
                                text_size fits = seen < capacity ? capacity - seen - 1 : 0;
                                if (fits > length) fits = length;
                                text_fill(source, 's', length);
                                source[length] = 0;
                                text_fill(buffer, 'd', sizeof buffer);
                                text_fill(expected, 'd', sizeof expected);
                                buffer[held] = expected[held] = 0;
                                if (seen < capacity)
                                {
                                        text_fill(expected + held, 's', fits);
                                        expected[held + fits] = 0;
                                }
                                answer = text_append_bounded(buffer, source, capacity);
                                check("strlcat capacity matrix length", answer == seen + length);
                                int intact = 1;
                                for (text_size at = 0; at < sizeof buffer; at++)
                                        intact &= buffer[at] == expected[at];
                                check("strlcat capacity matrix bytes", intact);
                        }
}

/*
        memfrob, whose whole specification is that doing it twice undoes it.
*/
static void text_case_frob(void)
{
        text_byte block[6] = "hello";

        text_frob(block, 5);
        check("memfrob first byte", block[0] == (text_byte)('h' ^ 42));
        check("memfrob last byte", block[4] == (text_byte)('o' ^ 42));
        check("memfrob left the terminator", block[5] == 0);
        text_frob(block, 5);
        check("memfrob is its own inverse", text_equal(block, TX("hello")));

        text_frob(block, 0);
        check("memfrob of nothing", text_equal(block, TX("hello")));
}

/*
        The non-local exit.

        text_case_jump_level_one through three make three real frames between
        the mark and the jump, and each adds to a volatile counter so the
        depth reached is checked rather than assumed. Nothing tail calls: the
        statement after each nested call is dead but it is what stops the
        compiler from turning three frames into none.

        text_case_scramble_jump is the deepest step and it is assembly,
        because the thing being tested is that longjmp puts the callee-saved
        registers back, and a register the C chain never touched proves
        nothing. It writes a pattern into every one of them and then jumps.
*/
text_jump_state text_case_state;
static volatile unsigned long long text_case_depth;
static volatile double text_case_seed_decimal = 0.5;
static volatile unsigned long long text_case_seed_whole = 0x0102030405060708ULL;

void text_case_scramble_jump(text_jump_state state, int value);
void text_case_jump_level_one(int value);
unsigned long long text_case_register_probe(void);

void text_case_jump_level_three(int value)
{
        text_case_depth += 4;
        text_case_scramble_jump(text_case_state, value);
        text_case_depth += 1000;
}

void text_case_jump_level_two(int value)
{
        text_case_depth += 2;
        text_case_jump_level_three(value);
        text_case_depth += 1000;
}

void text_case_jump_level_one(int value)
{
        text_case_depth += 1;
        text_case_jump_level_two(value);
        text_case_depth += 1000;
}

/*
        Eight doubles and eight integers, each derived from a volatile read so
        the compiler cannot fold them away, live across the mark and read
        after it.

        On arm64 that is what asks for d8 through d15, and on riscv64 for fs0
        through fs11 -- the halves of the save list a byte-only test never
        reaches. Where the compiler spills them to the stack instead the sums
        still have to come back, which tests the saved stack pointer. Neither
        value is written between the mark and the jump, so both are defined
        after the second arrival rather than merely likely.
*/
static double text_case_jump_decimals(void)
{
        volatile int came;
        double base = text_case_seed_decimal;
        double a = base + 1.0;
        double b = base + 2.0;
        double c = base + 3.0;
        double d = base + 4.0;
        double e = base + 5.0;
        double f = base + 6.0;
        double g = base + 7.0;
        double h = base + 8.0;

        came = text_jump_mark(text_case_state);

        if (came == 0)
                text_case_jump_level_one(5);

        return a + b + c + d + e + f + g + h;
}

static unsigned long long text_case_jump_wholes(void)
{
        volatile int came;
        unsigned long long base = text_case_seed_whole;
        unsigned long long a = base + 1;
        unsigned long long b = base + 2;
        unsigned long long c = base + 3;
        unsigned long long d = base + 4;
        unsigned long long e = base + 5;
        unsigned long long f = base + 6;
        unsigned long long g = base + 7;
        unsigned long long h = base + 8;

        came = text_jump_mark(text_case_state);

        if (came == 0)
                text_case_jump_level_one(6);

        return a + b + c + d + e + f + g + h;
}

static void text_case_jump(void)
{
        volatile int came;

        text_case_depth = 0;
        came = text_jump_mark(text_case_state);

        if (came == 0)
                text_case_jump_level_one(7);

        check("setjmp answers seven where longjmp said seven", came == 7);
        check("longjmp crossed three frames", text_case_depth == 7);

        text_case_depth = 0;
        came = text_jump_mark(text_case_state);

        if (came == 0)
                text_case_jump_level_one(0);

        check("longjmp of zero arrives as one", came == 1);
        check("longjmp of zero crossed three frames", text_case_depth == 7);

        text_case_depth = 0;
        came = text_jump_mark(text_case_state);

        if (came == 0)
                text_case_jump_level_one(-3);

        check("longjmp carries a negative value", came == -3);

        check("doubles survive the jump", text_case_jump_decimals() == 40.0);
        check("integers survive the jump",
              text_case_jump_wholes() == 8 * 0x0102030405060708ULL + 36);

        check("every callee-saved register comes back",
              text_case_register_probe() == 0);
}

/*
        The list, so that the two builds cannot run different ones.

        Both hosts used to write this out for themselves, which is the one
        place in the arrangement where they could drift: a case added here and
        called from only one main would be checked against one library and
        silently not the other, and the two verdict lines would still look
        comparable, because both are the number of cases that ran.
*/
static void text_case_all(void)
{
        text_case_duplicate();
        text_case_duplicate_max();
        text_case_token_next();
        text_case_token();
        text_case_split();
        text_case_search_folded();
        text_case_copy_bounded();
        text_case_append_bounded();
        text_case_frob();
        text_case_jump();
}


/*
        The two assembly helpers, and why a C test cannot be either of them.

        text_case_scramble_jump writes a pattern into every callee-saved
        register the ABI names and then jumps. That is the half of the test
        that makes the other half mean anything: if the registers still held
        what the marking frame put there, a longjmp that restored none of them
        would pass.

        text_case_register_probe is the marking frame, written in assembly for
        the same reason. It saves the caller's registers the way any function
        must, fills every one of them with a known value, marks, and -- on the
        arrival that comes back through the jump -- exclusive-ors each against
        the value it should still hold and ors the differences together. Zero
        means every register on that machine's list came back. C cannot ask
        that question about x19 or s7; it can only hope the register allocator
        happened to use one.

        The values are sixteen bit constants on the two machines whose move
        immediate is sixteen bits wide, so no constant pool is needed and the
        whole probe is position independent by construction. The doubles are
        set and read as bit patterns through the general registers rather than
        compared as floating point, because what is being tested is whether
        sixty four bits came back, not whether two numbers are equal.

        Both call setjmp and longjmp by their libc names, which is what lets
        this same block test glibc in the reference build.
*/
#if defined(__x86_64__)

__asm__(
    ".text\n"
    ".globl text_case_scramble_jump\n"
    "text_case_scramble_jump:\n"
    "   movabs $0xd1d1d1d1d1d1d1d1, %rbx\n"
    "   movabs $0xd2d2d2d2d2d2d2d2, %rbp\n"
    "   movabs $0xd3d3d3d3d3d3d3d3, %r12\n"
    "   movabs $0xd4d4d4d4d4d4d4d4, %r13\n"
    "   movabs $0xd5d5d5d5d5d5d5d5, %r14\n"
    "   movabs $0xd6d6d6d6d6d6d6d6, %r15\n"
    "   jmp longjmp\n"

    ".globl text_case_register_probe\n"
    "text_case_register_probe:\n"
    "   push %rbx\n   push %rbp\n   push %r12\n"
    "   push %r13\n   push %r14\n   push %r15\n"
    "   sub $8, %rsp   # six pushes and the return address leave this odd\n"
    "   movabs $0xb1b1b1b1b1b1b1b1, %rbx\n"
    "   movabs $0xb2b2b2b2b2b2b2b2, %rbp\n"
    "   movabs $0xb3b3b3b3b3b3b3b3, %r12\n"
    "   movabs $0xb4b4b4b4b4b4b4b4, %r13\n"
    "   movabs $0xb5b5b5b5b5b5b5b5, %r14\n"
    "   movabs $0xb6b6b6b6b6b6b6b6, %r15\n"
    "   lea text_case_state(%rip), %rdi\n"
    "   call setjmp\n"
    "   test %eax, %eax\n"
    "   jnz 2f\n"
    "   mov $9, %edi\n"
    "   call text_case_jump_level_one   # three frames, then back to here\n"
    "2: xor %eax, %eax\n"
    "   movabs $0xb1b1b1b1b1b1b1b1, %rcx\n   xor %rbx, %rcx\n   or %rcx, %rax\n"
    "   movabs $0xb2b2b2b2b2b2b2b2, %rcx\n   xor %rbp, %rcx\n   or %rcx, %rax\n"
    "   movabs $0xb3b3b3b3b3b3b3b3, %rcx\n   xor %r12, %rcx\n   or %rcx, %rax\n"
    "   movabs $0xb4b4b4b4b4b4b4b4, %rcx\n   xor %r13, %rcx\n   or %rcx, %rax\n"
    "   movabs $0xb5b5b5b5b5b5b5b5, %rcx\n   xor %r14, %rcx\n   or %rcx, %rax\n"
    "   movabs $0xb6b6b6b6b6b6b6b6, %rcx\n   xor %r15, %rcx\n   or %rcx, %rax\n"
    "   add $8, %rsp\n"
    "   pop %r15\n   pop %r14\n   pop %r13\n"
    "   pop %r12\n   pop %rbp\n   pop %rbx\n"
    "   ret\n");

#elif defined(__aarch64__)

__asm__(
    ".text\n"
    ".globl text_case_scramble_jump\n"
    "text_case_scramble_jump:\n"
    "   mov x19, #0xdd01\n   mov x20, #0xdd02\n   mov x21, #0xdd03\n"
    "   mov x22, #0xdd04\n   mov x23, #0xdd05\n   mov x24, #0xdd06\n"
    "   mov x25, #0xdd07\n   mov x26, #0xdd08\n   mov x27, #0xdd09\n"
    "   mov x28, #0xdd0a\n   mov x29, #0xdd0b\n"
    "   mov x2, #0xee01\n   fmov d8,  x2\n"
    "   mov x2, #0xee02\n   fmov d9,  x2\n"
    "   mov x2, #0xee03\n   fmov d10, x2\n"
    "   mov x2, #0xee04\n   fmov d11, x2\n"
    "   mov x2, #0xee05\n   fmov d12, x2\n"
    "   mov x2, #0xee06\n   fmov d13, x2\n"
    "   mov x2, #0xee07\n   fmov d14, x2\n"
    "   mov x2, #0xee08\n   fmov d15, x2\n"
    "   b longjmp\n"

    ".globl text_case_register_probe\n"
    "text_case_register_probe:\n"
    "   stp x29, x30, [sp, #-176]!\n"
    "   mov x29, sp\n"
    "   stp x19, x20, [sp, #16]\n"
    "   stp x21, x22, [sp, #32]\n"
    "   stp x23, x24, [sp, #48]\n"
    "   stp x25, x26, [sp, #64]\n"
    "   stp x27, x28, [sp, #80]\n"
    "   stp d8,  d9,  [sp, #96]\n"
    "   stp d10, d11, [sp, #112]\n"
    "   stp d12, d13, [sp, #128]\n"
    "   stp d14, d15, [sp, #144]\n"
    "   mov x19, #0x1901\n   mov x20, #0x2002\n   mov x21, #0x2103\n"
    "   mov x22, #0x2204\n   mov x23, #0x2305\n   mov x24, #0x2406\n"
    "   mov x25, #0x2507\n   mov x26, #0x2608\n   mov x27, #0x2709\n"
    "   mov x28, #0x280a\n"
    "   mov x2, #0x1108\n   fmov d8,  x2\n"
    "   mov x2, #0x1209\n   fmov d9,  x2\n"
    "   mov x2, #0x130a\n   fmov d10, x2\n"
    "   mov x2, #0x140b\n   fmov d11, x2\n"
    "   mov x2, #0x150c\n   fmov d12, x2\n"
    "   mov x2, #0x160d\n   fmov d13, x2\n"
    "   mov x2, #0x170e\n   fmov d14, x2\n"
    "   mov x2, #0x180f\n   fmov d15, x2\n"
    "   adrp x0, text_case_state\n"
    "   add x0, x0, :lo12:text_case_state\n"
    "   bl setjmp\n"
    "   cbnz w0, 2f\n"
    "   mov w0, #9\n"
    "   bl text_case_jump_level_one   // three frames, then back to here\n"
    "2: mov x0, xzr\n"
    "   mov x1, #0x1901\n   eor x1, x19, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2002\n   eor x1, x20, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2103\n   eor x1, x21, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2204\n   eor x1, x22, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2305\n   eor x1, x23, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2406\n   eor x1, x24, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2507\n   eor x1, x25, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2608\n   eor x1, x26, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x2709\n   eor x1, x27, x1\n   orr x0, x0, x1\n"
    "   mov x1, #0x280a\n   eor x1, x28, x1\n   orr x0, x0, x1\n"
    "   mov x1, sp\n   eor x1, x29, x1   // the frame pointer, against the stack\n"
    "   orr x0, x0, x1\n"
    "   fmov x1, d8\n    mov x2, #0x1108\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d9\n    mov x2, #0x1209\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d10\n   mov x2, #0x130a\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d11\n   mov x2, #0x140b\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d12\n   mov x2, #0x150c\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d13\n   mov x2, #0x160d\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d14\n   mov x2, #0x170e\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   fmov x1, d15\n   mov x2, #0x180f\n   eor x1, x1, x2\n   orr x0, x0, x1\n"
    "   ldp x19, x20, [sp, #16]\n"
    "   ldp x21, x22, [sp, #32]\n"
    "   ldp x23, x24, [sp, #48]\n"
    "   ldp x25, x26, [sp, #64]\n"
    "   ldp x27, x28, [sp, #80]\n"
    "   ldp d8,  d9,  [sp, #96]\n"
    "   ldp d10, d11, [sp, #112]\n"
    "   ldp d12, d13, [sp, #128]\n"
    "   ldp d14, d15, [sp, #144]\n"
    "   ldp x29, x30, [sp], #176\n"
    "   ret\n");

#elif defined(__riscv)

__asm__(
    ".text\n"
    ".globl text_case_scramble_jump\n"
    "text_case_scramble_jump:\n"
    "   li s0, 0xd001\n   li s1, 0xd002\n   li s2, 0xd003\n   li s3, 0xd004\n"
    "   li s4, 0xd005\n   li s5, 0xd006\n   li s6, 0xd007\n   li s7, 0xd008\n"
    "   li s8, 0xd009\n   li s9, 0xd00a\n   li s10, 0xd00b\n   li s11, 0xd00c\n"
    "   li t0, 0xe001\n   fmv.d.x fs0, t0\n"
    "   li t0, 0xe002\n   fmv.d.x fs1, t0\n"
    "   li t0, 0xe003\n   fmv.d.x fs2, t0\n"
    "   li t0, 0xe004\n   fmv.d.x fs3, t0\n"
    "   li t0, 0xe005\n   fmv.d.x fs4, t0\n"
    "   li t0, 0xe006\n   fmv.d.x fs5, t0\n"
    "   li t0, 0xe007\n   fmv.d.x fs6, t0\n"
    "   li t0, 0xe008\n   fmv.d.x fs7, t0\n"
    "   li t0, 0xe009\n   fmv.d.x fs8, t0\n"
    "   li t0, 0xe00a\n   fmv.d.x fs9, t0\n"
    "   li t0, 0xe00b\n   fmv.d.x fs10, t0\n"
    "   li t0, 0xe00c\n   fmv.d.x fs11, t0\n"
    "   tail longjmp\n"

    ".globl text_case_register_probe\n"
    "text_case_register_probe:\n"
    "   addi sp, sp, -224\n"
    "   sd ra, 216(sp)\n"
    "   sd s0, 0(sp)\n     sd s1, 8(sp)\n     sd s2, 16(sp)\n"
    "   sd s3, 24(sp)\n    sd s4, 32(sp)\n    sd s5, 40(sp)\n"
    "   sd s6, 48(sp)\n    sd s7, 56(sp)\n    sd s8, 64(sp)\n"
    "   sd s9, 72(sp)\n    sd s10, 80(sp)\n   sd s11, 88(sp)\n"
    "   fsd fs0, 96(sp)\n  fsd fs1, 104(sp)\n fsd fs2, 112(sp)\n"
    "   fsd fs3, 120(sp)\n fsd fs4, 128(sp)\n fsd fs5, 136(sp)\n"
    "   fsd fs6, 144(sp)\n fsd fs7, 152(sp)\n fsd fs8, 160(sp)\n"
    "   fsd fs9, 168(sp)\n fsd fs10, 176(sp)\n fsd fs11, 184(sp)\n"
    "   li s0, 0x1001\n   li s1, 0x1002\n   li s2, 0x1003\n   li s3, 0x1004\n"
    "   li s4, 0x1005\n   li s5, 0x1006\n   li s6, 0x1007\n   li s7, 0x1008\n"
    "   li s8, 0x1009\n   li s9, 0x100a\n   li s10, 0x100b\n   li s11, 0x100c\n"
    "   li t0, 0x2001\n   fmv.d.x fs0, t0\n"
    "   li t0, 0x2002\n   fmv.d.x fs1, t0\n"
    "   li t0, 0x2003\n   fmv.d.x fs2, t0\n"
    "   li t0, 0x2004\n   fmv.d.x fs3, t0\n"
    "   li t0, 0x2005\n   fmv.d.x fs4, t0\n"
    "   li t0, 0x2006\n   fmv.d.x fs5, t0\n"
    "   li t0, 0x2007\n   fmv.d.x fs6, t0\n"
    "   li t0, 0x2008\n   fmv.d.x fs7, t0\n"
    "   li t0, 0x2009\n   fmv.d.x fs8, t0\n"
    "   li t0, 0x200a\n   fmv.d.x fs9, t0\n"
    "   li t0, 0x200b\n   fmv.d.x fs10, t0\n"
    "   li t0, 0x200c\n   fmv.d.x fs11, t0\n"
    "   lla a0, text_case_state\n"
    "   call setjmp\n"
    "   bnez a0, 2f\n"
    "   li a0, 9\n"
    "   call text_case_jump_level_one   # three frames, then back to here\n"
    "2: li a0, 0\n"
    "   li t0, 0x1001\n   xor t0, s0, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1002\n   xor t0, s1, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1003\n   xor t0, s2, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1004\n   xor t0, s3, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1005\n   xor t0, s4, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1006\n   xor t0, s5, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1007\n   xor t0, s6, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1008\n   xor t0, s7, t0\n    or a0, a0, t0\n"
    "   li t0, 0x1009\n   xor t0, s8, t0\n    or a0, a0, t0\n"
    "   li t0, 0x100a\n   xor t0, s9, t0\n    or a0, a0, t0\n"
    "   li t0, 0x100b\n   xor t0, s10, t0\n   or a0, a0, t0\n"
    "   li t0, 0x100c\n   xor t0, s11, t0\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs0\n    li t1, 0x2001\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs1\n    li t1, 0x2002\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs2\n    li t1, 0x2003\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs3\n    li t1, 0x2004\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs4\n    li t1, 0x2005\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs5\n    li t1, 0x2006\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs6\n    li t1, 0x2007\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs7\n    li t1, 0x2008\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs8\n    li t1, 0x2009\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs9\n    li t1, 0x200a\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs10\n   li t1, 0x200b\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   fmv.x.d t0, fs11\n   li t1, 0x200c\n   xor t0, t0, t1\n   or a0, a0, t0\n"
    "   ld ra, 216(sp)\n"
    "   ld s0, 0(sp)\n     ld s1, 8(sp)\n     ld s2, 16(sp)\n"
    "   ld s3, 24(sp)\n    ld s4, 32(sp)\n    ld s5, 40(sp)\n"
    "   ld s6, 48(sp)\n    ld s7, 56(sp)\n    ld s8, 64(sp)\n"
    "   ld s9, 72(sp)\n    ld s10, 80(sp)\n   ld s11, 88(sp)\n"
    "   fld fs0, 96(sp)\n  fld fs1, 104(sp)\n fld fs2, 112(sp)\n"
    "   fld fs3, 120(sp)\n fld fs4, 128(sp)\n fld fs5, 136(sp)\n"
    "   fld fs6, 144(sp)\n fld fs7, 152(sp)\n fld fs8, 160(sp)\n"
    "   fld fs9, 168(sp)\n fld fs10, 176(sp)\n fld fs11, 184(sp)\n"
    "   addi sp, sp, 224\n"
    "   ret\n");

#else
#error "the register probe needs a body for this architecture"
#endif

#endif // STANDARD_MODERN_C_TEST_STRINGS_CASES

#elif defined(SHARED_format_differential_body)
/*
        Experimental C standard library

        printf's decimal floats, generated, written once and compiled twice

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        The pinned floats in CHECK_format are the corners somebody thought of.
        This is the rest of the surface. A case is drawn from its index alone,
        so a range of indices is the same cases on any machine and in any
        order, and one digest of every byte snprintf wrote and every length it
        answered is printed for each block of 65536 indices.
        CHECK_format_differential puts this tree's snprintf underneath and
        CHECK_format_differential_reference the machine's glibc; the format
        lane of test/run compares the digests on x86_64, arm64 and riscv64 and
        runs a block that disagrees again with one line per case.

        Neither library is asked to make a number. The doubles come from
        integers: bit patterns, integer conversions, one correctly rounded
        division, products of exact powers of ten, and exponent edits. IEEE
        arithmetic gives each of those exactly one answer, and none of them is
        a multiply followed by an add a compiler could fuse into an operation
        that answers differently on another machine.

        The families are where float formatting goes wrong: any pattern at
        all, NaNs and infinities with it; every exponent; subnormals with
        short mantissas; integers of every width; exact decimal ties -- an odd
        integer over a power of two, whose last decimal place is a five --
        with the precision that rounds exactly there; decimal looking
        quotients; the neighbours of both a few units away in the last place;
        powers of two; doubles near every power of ten; runs of nines that
        carry; and the ends of the range. Each case then draws one of
        %f %F %e %E %g %G, any of the five flags in a rotated order, a width
        or none, and a precision or none, most of them below forty six and a
        few of them up to eleven hundred.
*/

typedef unsigned long long fd_word;

#define FD_SIGN (1ull << 63)
#define FD_MANTISSA ((1ull << 52) - 1)

static const fd_word fd_tens[20] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull,
    10000000ull, 100000000ull, 1000000000ull, 10000000000ull,
    100000000000ull, 1000000000000ull, 10000000000000ull,
    100000000000000ull, 1000000000000000ull, 10000000000000000ull,
    100000000000000000ull, 1000000000000000000ull,
    10000000000000000000ull};

static const double fd_exact_tens[23] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

static fd_word fd_next(fd_word *state)
{
        fd_word z;

        *state += 0x9E3779B97F4A7C15ull;
        z = *state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
}

static fd_word fd_bits(double value)
{
        union
        {
                double value;
                fd_word bits;
        } view;

        view.value = value;
        return view.bits;
}

static double fd_double(fd_word bits)
{
        union
        {
                double value;
                fd_word bits;
        } view;

        view.bits = bits;
        return view.value;
}

//      A normal double times two to the minus places, by its exponent field.
static fd_word fd_scale_down(fd_word bits, fd_word places)
{
        fd_word raw = (bits >> 52) & 0x7ff;

        if (raw <= places)
                return bits;

        return (bits & ~(0x7ffull << 52)) | ((raw - places) << 52);
}

static fd_word fd_nudge(fd_word *state, fd_word bits, fd_word reach)
{
        return bits + fd_next(state) % (2 * reach + 1) - reach;
}

//      Some double near ten to a power anywhere in the range: a product of
//      exact powers, each step correctly rounded, so a few units from the
//      nearest and the same few units on every machine.
static fd_word fd_near_ten(fd_word *state)
{
        long power = (long)(fd_next(state) % 632) - 323;
        double value = 1.0;

        while (power >= 22)
        {
                value = value * 1e22;
                power -= 22;
        }

        while (power <= -22)
        {
                value = value / 1e22;
                power += 22;
        }

        if (power >= 0)
                value = value * fd_exact_tens[power];
        else
                value = value / fd_exact_tens[-power];

        return fd_bits(value);
}

static fd_word fd_tie(fd_word a, fd_word b, long *hint)
{
        fd_word places = 1 + b % 64;
        fd_word odd = ((a >> (23 + (b >> 8) % 41)) << 1) | 1;

        *hint = (long)places - 1;
        return fd_scale_down(fd_bits((double)odd), places);
}

static fd_word fd_quotient(fd_word a, fd_word b, long *hint)
{
        fd_word digits = 1 + b % 17;
        fd_word places = (b >> 8) % 23;

        *hint = (long)places - 1 + (long)((b >> 16) % 3);
        return fd_bits((double)(a % fd_tens[digits]) / fd_exact_tens[places]);
}

static fd_word fd_value(fd_word *state, long *hint)
{
        static const fd_word ends[12] = {
            0x7FEFFFFFFFFFFFFFull, 0x0010000000000000ull,
            0x000FFFFFFFFFFFFFull, 0x0000000000000001ull,
            0x0000000000000000ull, 0x7FF0000000000000ull,
            0x7FF8000000000000ull, 0x7FF0000000000001ull,
            0x7FFFFFFFFFFFFFFFull, 0x3FF0000000000000ull,
            0x3FEFFFFFFFFFFFFFull, 0x3FB999999999999Aull};
        fd_word sign = fd_next(state) & FD_SIGN;
        fd_word pick = fd_next(state);
        fd_word a = fd_next(state);
        fd_word b = fd_next(state);
        fd_word bits;

        *hint = -1;

        switch (pick % 16)
        {
        case 0:
                return a;
        case 1:
        case 2:
                return sign | (a & FD_MANTISSA) | ((b % 2047) << 52);
        case 3:
                return sign | (a >> (12 + b % 52));
        case 4:
                return sign | fd_bits((double)(a >> (b % 64)));
        case 5:
        case 6:
                return sign | fd_tie(a, b, hint);
        case 7:
        case 8:
                return sign | fd_quotient(a, b, hint);
        case 9:
                return fd_nudge(state, sign | fd_tie(a, b, hint), 2);
        case 10:
                return fd_nudge(state, sign | fd_quotient(a, b, hint), 3);
        case 11:
                return sign | ((b & 1) ? (a % 2047) << 52
                                       : (fd_word)1 << (a % 52));
        case 12:
                *hint = (long)(b % 20);
                return sign | fd_nudge(state, fd_near_ten(state), 2);
        case 13:
        {
                fd_word digits = 1 + b % 17;
                fd_word places = (b >> 8) % 23;

                *hint = (long)digits - 2 + (long)((b >> 16) % 3);
                bits = fd_bits((double)(fd_tens[digits] - 1) /
                               fd_exact_tens[places]);
                return fd_nudge(state, sign | bits, 1);
        }
        case 14:
                bits = ends[a % 12];

                if ((b & 1) && ((bits >> 52) & 0x7ff) != 0x7ff && bits > 2)
                        bits = fd_nudge(state, bits, 2);

                return sign | bits;
        default:
                return sign | (a & FD_MANTISSA) |
                       (((b & 1) ? (b >> 1) % 64 : 2046 - (b >> 1) % 64)
                        << 52);
        }
}

static long fd_decimal(char *into, fd_word value)
{
        char scratch[24];
        long length = 0;
        long at = 0;

        do
        {
                scratch[length++] = (char)('0' + value % 10);
                value /= 10;
        } while (value);

        while (length)
                into[at++] = scratch[--length];

        return at;
}

static long fd_hex(char *into, fd_word value)
{
        long at;

        for (at = 0; at < 16; at++)
                into[at] = "0123456789abcdef"[(value >> (60 - 4 * at)) & 15];

        return 16;
}

static fd_word fd_parse(const char *text)
{
        fd_word value = 0;

        while (*text >= '0' && *text <= '9')
                value = value * 10 + (fd_word)(*text++ - '0');

        return value;
}

typedef struct
{
        char flags[6];
        long width;      // -1 for none
        long precision;  // -1 for none, -2 for a bare point
        char conversion;
        char length[3];  // the grid's length modifier, empty for the floats
        char width_star; // the grid's * and .*, whose values are arguments
        char precision_star;
        int width_argument;
        int precision_argument;
} fd_spec;

static void fd_render(const fd_spec *spec, char *format)
{
        long at = 0;
        long i;

        format[at++] = '%';

        for (i = 0; spec->flags[i]; i++)
                format[at++] = spec->flags[i];

        if (spec->width_star)
                format[at++] = '*';
        else if (spec->width >= 0)
                at += fd_decimal(format + at, (fd_word)spec->width);

        if (spec->precision_star)
        {
                format[at++] = '.';
                format[at++] = '*';
        }
        else if (spec->precision == -2)
        {
                format[at++] = '.';
        }
        else if (spec->precision >= 0)
        {
                format[at++] = '.';
                at += fd_decimal(format + at, (fd_word)spec->precision);
        }

        for (i = 0; spec->length[i]; i++)
                format[at++] = spec->length[i];

        format[at++] = spec->conversion;
        format[at] = 0;
}

//      The specification and the double of one case, from its index.
static fd_word fd_case(fd_word index, fd_spec *spec)
{
        static const char flags[5] = {'-', '+', ' ', '#', '0'};
        fd_word state = index * 0xD1B54A32D192ED03ull + 0x8CB92BA72F3D8DD7ull;
        long hint;
        fd_word bits = fd_value(&state, &hint);
        fd_word a = fd_next(&state);
        fd_word b = fd_next(&state);
        long at = 0;
        long i;

        spec->length[0] = 0;
        spec->width_star = spec->precision_star = 0;

        for (i = 0; i < 5; i++)
                if (((a >> (i * 2)) & 3) == 0)
                        spec->flags[at++] = flags[(i + b % 5) % 5];

        spec->flags[at] = 0;
        spec->width = ((a >> 10) & 1) ? 1 + (long)((a >> 11) % 40) : -1;

        if (hint >= 0 && hint < 1100 && ((a >> 20) & 1))
        {
                spec->precision = hint;
        }
        else
        {
                fd_word roll = (a >> 21) % 100;
                fd_word spread = b >> 8;

                if (roll < 8)
                        spec->precision = -1;
                else if (roll < 12)
                        spec->precision = -2;
                else if (roll < 72)
                        spec->precision = (long)(spread % 21);
                else if (roll < 90)
                        spec->precision = 21 + (long)(spread % 25);
                else if (roll < 97)
                        spec->precision = 46 + (long)(spread % 355);
                else
                        spec->precision = 401 + (long)(spread % 700);
        }

        spec->conversion = "fFeEgG"[(b >> 40) % 6];
        return bits;
}

/*
        The one glibc bug the reference is corrected for, which src/lib.util.c
        names beside format_run. When %g with the # flag first settles on
        the fixed shape with no places left after the point, and rounding then
        carries into a new power of ten, glibc switches to the exponent shape
        but keeps the fixed shape's count of zero places: "%#.6g" of
        999999.9999 is "1.e+06" where C asks for "1.00000e+06". The two agree
        whenever the precision is one. So on the reference side, and only
        there, a %#g answer with a point directly before its exponent and
        more than one significant digit asked for is written again as %#e at
        one place fewer, which is the answer C gives and which glibc's %e
        gets right. Applied to this tree's side it could hide the same bug
        arriving here, so it is not.
*/
/*
        The grid's arguments: a double, a 64 bit integer the length modifier
        narrows, a string or a pointer, and the values the stars take.
*/
typedef struct
{
        char kind; // d i u(unsigned int) l(the length modifier's) s p c
        double real;
        long long whole;
        const char *text;
} fd_argument;

static int fd_call(char *out, long room, const char *format,
                   const fd_spec *spec, const fd_argument *value)
{
        int w = spec->width_argument;
        int p = spec->precision_argument;
        int stars = (spec->width_star ? 2 : 0) | (spec->precision_star ? 1 : 0);

#define FD_CALL(v)                                                            \
        (stars == 3 ? snprintf(out, room, format, w, p, v)                    \
         : stars == 2 ? snprintf(out, room, format, w, v)                     \
         : stars == 1 ? snprintf(out, room, format, p, v)                     \
                      : snprintf(out, room, format, v))

        switch (value->kind)
        {
        case 'i':
                return FD_CALL((int)value->whole);
        case 'u':
                return FD_CALL((unsigned)value->whole);
        case 'l':
                if (spec->length[0] == 'h' || !spec->length[0])
                        return FD_CALL((int)value->whole);
                return FD_CALL(value->whole);
        case 's':
                return FD_CALL(value->text);
        case 'p':
                return FD_CALL((void *)(unsigned long)value->whole);
        case 'c':
                return FD_CALL((int)value->whole);
        default:
                return FD_CALL(value->real);
        }
#undef FD_CALL
}

/*
        The one glibc bug the reference is corrected for, which src/lib.util.c
        names beside format_run. When %g with the # flag first settles on
        the fixed shape with no places left after the point, and rounding then
        carries into a new power of ten, glibc switches to the exponent shape
        but keeps the fixed shape's count of zero places: "%#.6g" of
        999999.9999 is "1.e+06" where C asks for "1.00000e+06". The two agree
        whenever the precision is one. So on the reference side, and only
        there, a %#g answer with a point directly before its exponent and
        more than one significant digit asked for is written again as %#e at
        one place fewer, which is the answer C gives and which glibc's %e
        gets right. Applied to this tree's side it could hide the same bug
        arriving here, so it is not.
*/
static int fd_format_argument(char *out, long room, const fd_spec *spec,
                              const fd_argument *value)
{
        char format[48];
        int answer;

        fd_render(spec, format);
        answer = fd_call(out, room, format, spec, value);

#ifdef FD_REFERENCE
        if ((spec->conversion == 'g' || spec->conversion == 'G') &&
            answer > 0 && answer < room)
        {
                long precision = spec->precision_star
                                     ? (spec->precision_argument < 0
                                            ? -1
                                            : spec->precision_argument)
                                     : spec->precision;
                long significant = precision == -1 ? 6
                                   : precision <= 0 ? 1
                                                    : precision;
                int alternate = 0;
                int point = 0;
                long i;

                for (i = 0; spec->flags[i]; i++)
                        alternate |= spec->flags[i] == '#';

                for (i = 0; i + 1 < answer; i++)
                        point |= out[i] == '.' && (out[i + 1] | 0x20) == 'e';

                if (alternate && point && significant > 1)
                {
                        fd_spec standard = *spec;

                        standard.conversion = spec->conversion == 'g' ? 'e' : 'E';
                        standard.precision_star = 0;
                        standard.precision = significant - 1;
                        fd_render(&standard, format);
                        answer = fd_call(out, room, format, &standard, value);
                }
        }
#endif

        return answer;
}

static int fd_format(char *out, long room, fd_spec *spec, double value)
{
        fd_argument argument;

        argument.kind = 'd';
        argument.real = value;
        return fd_format_argument(out, room, spec, &argument);
}

/*
        The grid: every flag spelling against every width, every precision,
        every conversion and every value of its kind, walked in full rather
        than drawn. It replaced some nine hundred cases CHECK_format pinned
        one line each from a glibc run, and every one of those is a point on
        it: the same format, the same arguments, now compared with the
        machine's glibc instead of with a copy of its answer.

        A flag spelling is one of the 32 subsets of "-+ #0" in that order, or
        one of the ten pairs the other way round. A width or precision is
        none, a literal, a bare point, or a star whose argument is on the
        list, negative ones included. Each kind carries its own axes.
*/
#define FD_STAR (1L << 20)
#define FD_END (1L << 30)

static const long fd_int_widths[] = {-1, 1, 5, 8, 10, 12, 20, FD_STAR - 20,
                                     FD_STAR - 8, FD_STAR - 5, FD_STAR,
                                     FD_STAR + 5, FD_END};
static const long fd_int_precisions[] = {-1, -2, 0, 1, 3, 4, 8, 20,
                                         FD_STAR - 3, FD_STAR - 1, FD_STAR,
                                         FD_STAR + 2, FD_STAR + 6, FD_END};
static const long fd_real_widths[] = {-1, 10, 15, 20, 25, FD_STAR - 8,
                                      FD_STAR + 1, FD_STAR + 20, FD_END};
static const long fd_real_precisions[] = {
    -1, -2, 0, 1, 2, 3, 5, 6, 7, 10, 11, 12, 13, 17, 20, 30, 40, 55, 56, 60,
    FD_STAR - 1, FD_STAR + 6, FD_END};
static const long fd_text_widths[] = {-1, 1, 5, 10, 20, FD_STAR - 8, FD_STAR,
                                      FD_STAR + 6, FD_END};
static const long fd_text_precisions[] = {-1, -2, 0, 1, 3, 6, 10, FD_STAR - 1,
                                          FD_STAR + 6, FD_END};
static const long fd_length_widths[] = {-1, 20, FD_END};
static const long fd_no_precision[] = {-1, FD_END};
static const long fd_length_precisions[] = {-1, 20, FD_END};

static const long long fd_ints[] = {0, 1, 7, 42, -42, -1, 1234, 12345,
                                    2147483647, -2147483647 - 1};
static const long long fd_unsigneds[] = {0, 1, 7, 8, 42, 255, 300, 4294967295u};
static const long long fd_wides[] = {0, 300, 70000, -1234567890123LL,
                                     123456789, 0xdeadbeefcafeLL,
                                     -9223372036854775807LL - 1,
                                     9223372036854775807LL, -1};
static const long long fd_pointers[] = {0, 1, 0xdeadbeef};
static const long long fd_characters[] = {'a', ' ', '%'};
static const char *const fd_texts[] = {"", "a", "hello", "abcdefgh", 0};
static const double fd_reals[] = {
    0.0, -0.0, 1.0, -1.0, 2.0, 1.5, -2.5, 2.5, 3.5, 4.5, 5.5, 0.5, 0.1, 0.2,
    0.3, 0.05, 0.15, 0.25, 0.35, 0.45, 0.375, 1.4375, 1e-1, 1e-2, 1e-3, 1e-4,
    1e-5, 1e-6, 1e-10, 1e-100, 1e-300, 1e-310, 1e-320, 5e-320, -5e-320,
    5e-324, 4.9406564584124654e-324, 1e1, 1e2, 1e5, 1e15, 1e16, 1e17, 1e20,
    1e100, 1e300, 1e308, 100000.0, 1000000.0, 9999999.0, 9.999, 999999.5,
    999999.95, 999999.4999999, 0.0001, 0.00001, 0.000123456,
    0.049999999999999996, 0.25000000000000006, 0.9999999999,
    0.9999999999999999, 1.0 / 3.0, 2.0 / 3.0, 1.0000000000000002,
    1.0000000298023224, 1.9999999999999991, 1.9999999999999998,
    1.1125369292536007e-308, 2.2250738585072009e-308, 2.2250738585072014e-308,
    2.225073858507201e-308, 1.7976931348623157e308, 123456789.123456789,
    1234567890123456789.0, 2.718281828459045, 3.14159, 3.14159265358979,
    3.141592653589793, __builtin_inf(), -__builtin_inf(), __builtin_nan(""),
    -__builtin_nan("")};

typedef struct
{
        char kind;
        const char *conversions;
        long flag_spellings; // 32 in order, or 42 with the pairs reversed
        const char *const *lengths;
        const long *widths;
        const long *precisions;
        long values;
} fd_kind;

static const char *const fd_no_length[] = {"", 0};
static const char *const fd_every_length[] = {"hh", "h", "", "l", "ll",
                                              "j", "z", "t", 0};

static const fd_kind fd_kinds[] = {
    {'i', "di", 42, fd_no_length, fd_int_widths, fd_int_precisions,
     sizeof(fd_ints) / sizeof(fd_ints[0])},
    {'u', "uoxXbB", 42, fd_no_length, fd_int_widths, fd_int_precisions,
     sizeof(fd_unsigneds) / sizeof(fd_unsigneds[0])},
    {'l', "diuoxX", 16, fd_every_length, fd_length_widths, fd_length_precisions,
     sizeof(fd_wides) / sizeof(fd_wides[0])},
    {'s', "s", 2, fd_no_length, fd_text_widths, fd_text_precisions,
     sizeof(fd_texts) / sizeof(fd_texts[0])},
    {'c', "c", 2, fd_no_length, fd_text_widths, fd_no_precision,
     sizeof(fd_characters) / sizeof(fd_characters[0])},
    {'p', "p", 2, fd_no_length, fd_text_widths, fd_no_precision,
     sizeof(fd_pointers) / sizeof(fd_pointers[0])},
    {'d', "fFeEgGaA", 32, fd_no_length, fd_real_widths, fd_real_precisions,
     sizeof(fd_reals) / sizeof(fd_reals[0])},
};

static long fd_count_of(const long *list)
{
        long n = 0;

        while (list[n] != FD_END)
                n++;

        return n;
}

static long fd_strings(const char *const *list)
{
        long n = 0;

        while (list[n])
                n++;

        return n;
}

static fd_word fd_kind_size(const fd_kind *kind)
{
        fd_word conversions = 0;

        while (kind->conversions[conversions])
                conversions++;

        return (fd_word)kind->flag_spellings * (fd_word)fd_strings(kind->lengths) *
               (fd_word)fd_count_of(kind->widths) *
               (fd_word)fd_count_of(kind->precisions) * conversions *
               (fd_word)kind->values;
}

static fd_word fd_grid_size(void)
{
        fd_word total = 0;
        unsigned long k;

        for (k = 0; k < sizeof(fd_kinds) / sizeof(fd_kinds[0]); k++)
                total += fd_kind_size(&fd_kinds[k]);

        return total;
}

//      The spelling of flag set n: the subset of "-+ #0" whose bits n names,
//      in that order for n below 32, and the ten pairs reversed after it.
static void fd_flag_spelling(long n, char *into)
{
        static const char flags[5] = {'-', '+', ' ', '#', '0'};
        long at = 0;
        long i;
        long j;

        if (n < 32)
        {
                for (i = 0; i < 5; i++)
                        if (n & (1L << i))
                                into[at++] = flags[i];
        }
        else
        {
                n -= 32;

                for (i = 0; i < 5; i++)
                        for (j = i + 1; j < 5; j++)
                                if (n-- == 0)
                                {
                                        into[at++] = flags[j];
                                        into[at++] = flags[i];
                                }
        }

        into[at] = 0;
}

static void fd_axis(long code, long *plain, char *star, int *argument)
{
        *star = code >= FD_STAR / 2;
        *argument = *star ? (int)(code - FD_STAR) : 0;
        *plain = *star ? -1 : code;
}

//      Grid case index, as a specification and its argument.
static void fd_grid_case(fd_word index, fd_spec *spec, fd_argument *value)
{
        const fd_kind *kind = fd_kinds;
        fd_word size;
        long pick;
        long i;

        while ((size = fd_kind_size(kind)) <= index)
        {
                index -= size;
                kind++;
        }

        pick = (long)(index % (fd_word)kind->values);
        index /= (fd_word)kind->values;

        for (i = 0; kind->conversions[i]; i++)
                ;
        spec->conversion = kind->conversions[index % (fd_word)i];
        index /= (fd_word)i;

        i = fd_count_of(kind->precisions);
        fd_axis(kind->precisions[index % (fd_word)i], &spec->precision,
                &spec->precision_star, &spec->precision_argument);
        index /= (fd_word)i;

        i = fd_count_of(kind->widths);
        fd_axis(kind->widths[index % (fd_word)i], &spec->width,
                &spec->width_star, &spec->width_argument);
        index /= (fd_word)i;

        i = fd_strings(kind->lengths);
        {
                const char *length = kind->lengths[index % (fd_word)i];

                spec->length[0] = length[0];
                spec->length[1] = length[0] ? length[1] : 0;
                spec->length[2] = 0;
        }
        index /= (fd_word)i;

        fd_flag_spelling((long)index, spec->flags);

        value->kind = kind->kind;
        value->real = 0;
        value->whole = 0;
        value->text = 0;

        switch (kind->kind)
        {
        case 'i':
                value->whole = fd_ints[pick];
                break;
        case 'u':
                value->whole = fd_unsigneds[pick];
                break;
        case 'l':
                value->whole = fd_wides[pick];
                break;
        case 's':
                value->text = fd_texts[pick];
                break;
        case 'c':
                value->whole = fd_characters[pick];
                break;
        case 'p':
                value->whole = fd_pointers[pick];
                break;
        default:
                value->real = fd_reals[pick];
        }
}

static long fd_signed(char *into, long long value)
{
        long at = 0;

        if (value < 0)
                into[at++] = '-';

        return at + fd_decimal(into + at, value < 0 ? 0 - (fd_word)value
                                                    : (fd_word)value);
}

//      The arguments of one case as a detail line shows them: w= and p= for
//      the stars, then the value -- a double's bits, an integer in decimal,
//      a string in quotes or null.
static long fd_describe(char *into, const fd_spec *spec,
                        const fd_argument *value)
{
        long at = 0;
        long i;

        if (spec->width_star)
        {
                into[at++] = 'w';
                into[at++] = '=';
                at += fd_signed(into + at, spec->width_argument);
                into[at++] = ' ';
        }

        if (spec->precision_star)
        {
                into[at++] = 'p';
                into[at++] = '=';
                at += fd_signed(into + at, spec->precision_argument);
                into[at++] = ' ';
        }

        if (value->kind == 'd')
                return at + fd_hex(into + at, fd_bits(value->real));

        if (value->kind == 's')
        {
                if (!value->text)
                        return at + (into[at] = 'n', into[at + 1] = 'u',
                                     into[at + 2] = 'l', into[at + 3] = 'l', 4);

                into[at++] = '"';

                for (i = 0; value->text[i]; i++)
                        into[at++] = value->text[i];

                into[at++] = '"';
                return at;
        }

        return at + fd_signed(into + at, value->whole);
}

//      grid is zero for the drawn floats and one for the grid, whose digest
//      lines say grid rather than block and whose run ends by saying how many
//      cases it has.
static void format_differential_run(fd_word first, fd_word count, int detail,
                                    int grid)
{
        static char out[2048];
        static char line[2600];
        char format[48];
        fd_spec spec;
        fd_argument value;
        fd_word hash = 0xCBF29CE484222325ull;
        fd_word index;
        const char *label = grid ? "grid " : "block ";

        if (grid && first + count > fd_grid_size())
                count = first < fd_grid_size() ? fd_grid_size() - first : 0;

        for (index = first; index < first + count; index++)
        {
                int answer;

                if (grid)
                {
                        fd_grid_case(index, &spec, &value);
                }
                else
                {
                        value.kind = 'd';
                        value.real = fd_double(fd_case(index, &spec));
                }

                answer = fd_format_argument(out, sizeof(out), &spec, &value);
                fd_render(&spec, format);
                long length = answer < 0                   ? 0
                              : answer >= (int)sizeof(out) ? (long)sizeof(out) - 1
                                                           : answer;
                long at;
                long i;

                if (detail)
                {
                        at = fd_decimal(line, index);
                        line[at++] = ' ';

                        for (i = 0; format[i]; i++)
                                line[at++] = format[i];

                        line[at++] = ' ';
                        at += fd_describe(line + at, &spec, &value);
                        line[at++] = ' ';

                        if (answer < 0)
                                line[at++] = '-';

                        at += fd_decimal(line + at,
                                         (fd_word)(answer < 0 ? -(long)answer
                                                              : answer));
                        line[at++] = ' ';
                        line[at++] = '[';

                        for (i = 0; i < length; i++)
                                line[at++] = out[i];

                        line[at++] = ']';
                        line[at++] = '\n';
                        line[at] = 0;
                        fd_text(line);
                        continue;
                }

                for (i = 0; i < length; i++)
                        hash = (hash ^ (unsigned char)out[i]) * 0x100000001B3ull;

                hash = (hash ^ (fd_word)(unsigned)answer) * 0x100000001B3ull;

                if ((index + 1) % 65536 == 0 || index + 1 == first + count)
                {
                        at = 0;

                        for (i = 0; label[i]; i++)
                                line[at++] = label[i];

                        at += fd_decimal(line + at, index / 65536);
                        line[at++] = ' ';
                        at += fd_hex(line + at, hash);
                        line[at++] = '\n';
                        line[at] = 0;
                        fd_text(line);
                        hash = 0xCBF29CE484222325ull;
                }
        }

        if (grid && !detail)
        {
                {
                        long at = 0;
                        const char *said = "cases ";

                        for (; said[at]; at++)
                                line[at] = said[at];

                        at += fd_decimal(line + at, count);
                        line[at++] = '\n';
                        line[at] = 0;
                        fd_text(line);
                }
        }
}

//      first, count, and "detail" for a line per case instead of digests;
//      a first argument of "grid" walks the grid instead, from the next
//      two arguments (all of it when they are absent).
static void format_differential_main(const char *first, const char *count,
                                     const char *detail, const char *more)
{
        if (first[0] == 'g')
        {
                format_differential_run(fd_parse(count),
                                        detail[0] ? fd_parse(detail)
                                                  : fd_grid_size(),
                                        more[0] == 'd', 1);
                return;
        }

        format_differential_run(fd_parse(first),
                                count[0] ? fd_parse(count) : 65536,
                                detail[0] == 'd', 0);
}

#elif defined(SHARED_scan_differential_body)
/*
        Experimental C standard library

        scanf, generated, written once and compiled twice

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        A grid of formats against inputs, walked in full. CHECK_scan pinned a
        sample of a glibc run one case to a line; every one of those is a point
        here, now compared with the machine's glibc on all three machines.

        Three shapes. A number: one of d i u o x X p at every length modifier,
        or one of f e g E F G a A plain and with l, at a width or none, then
        %n, against every numeric input -- stored into a sixty four bit slot
        filled with 0xA5, so a conversion that writes too many or too few
        bytes shows in the bytes. Text: %s, %c or a scan set at a width or
        none, then %n, against every text input, into a room filled with
        0x7F. And formats that assign nothing, whose answer is the whole case.
        %n starts at minus one, so a format that failed before reaching it
        says so.
*/
typedef unsigned long long sd_word;

static const char *const sd_numbers[] = {
    "42", "-42", "+42", "  42", "42abc", "", "  ", "abc", "-", "+", "- 5",
    "+-5", "0", "007", "-0", "-4", "017", "019", "0x1f", "0X1F", "0x", "0xz",
    "-0x10", "+0", "0G", "0b101", "0b", "0b2", "-0b1", "9223372036854775807",
    "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
    "99999999999999999999", "-99999999999999999999",
    "0000000000000000000000005", "2147483648", "-2147483649", "-1", "-2",
    "+7", "08", "0x0", "7", "8", "1f", "00", "0x1G", "x1", "-0x1", "abcdef",
    "ABCDEF", "18446744073709551615", "18446744073709551616",
    "0xFFFFFFFFFFFFFFFF", "0x10000000000000000", "ff", "300", "70000",
    "1ffff", "1.5", "-1.5", "+1.5", ".5", "5.", "2.5", "0.25", "1e2", "1E2",
    "1.5e2xyz", "1e", "1e+", "1e-", ".", "-.", "inf", "-inf", "INF",
    "infinity", "INFINITY", "infinit", "infz", "in", "2inf", "0x1p3",
    "0X1P-2", "0x1", "0x1p", "0x.8p1", "0x.", "3.14159", "1.5e", "-2.5",
    "3.140000", "nan", "0x1234", "1234", "(nil)", "(NIL)x", "(ni", "-0x5",
    "0x123456"};
static const char *const sd_texts[] = {
    "hello", "  hello", "hello world", "", "   ", "\t\nab", "abc", "  abc",
    "ab", "a", "abcd", "dabc", "dab", "abcZ", "ABCa", "]]a", "ab]c", "-ab",
    "a-b", "z-ab", "abz", "abcde", "123a", "1234", "  a", "ab c", "abc]d",
    "a^b", "ab^", "abZ9", "19afg"};
static const char *const sd_quiet_inputs[] = {"", "y", "x", "abc", "ab", " ",
                                              "a b", "a", " a", "%", " %",
                                              "12"};
static const char *const sd_quiet_formats[] = {
    "x", "abc", "a b", " a", "", " ", "%%", "%*d", "%*s", "%*c", "%*[ab]",
    "%y", "%", "%[abc", "%[]", "%[^]"};
static const char *const sd_sets[] = {
    "s", "c", "[abc]", "[^abc]", "[a-z]", "[^a-z]", "[]]", "[^]]", "[-a]",
    "[a-]", "[z-a]", "[a-c-e]", "[0-9]", "[ ]", "[^ ]", "[]abc]", "[a^]",
    "[^^]", "[A-Za-z]", "[0-9a-f]"};
static const char *const sd_lengths[] = {"hh", "h", "", "l", "ll", "j", "z", "t"};
static const long sd_widths[] = {-1, 0, 1, 2, 3, 4, 5, 8, 10, 20};

#define SD_COUNT(list) ((sd_word)(sizeof(list) / sizeof((list)[0])))
#define SD_INTEGERS (7 * 8)  // d i u o x X at eight lengths, and p at one
#define SD_REALS (8 * 2)

static sd_word sd_number_cases(void)
{
        return (SD_INTEGERS + SD_REALS) * SD_COUNT(sd_widths) *
               SD_COUNT(sd_numbers);
}

static sd_word sd_text_cases(void)
{
        return SD_COUNT(sd_sets) * SD_COUNT(sd_widths) * SD_COUNT(sd_texts);
}

static sd_word sd_grid_size(void)
{
        return sd_number_cases() + sd_text_cases() +
               SD_COUNT(sd_quiet_formats) * SD_COUNT(sd_quiet_inputs);
}

static long sd_decimal(char *into, sd_word value)
{
        char scratch[24];
        long length = 0;
        long at = 0;

        do
        {
                scratch[length++] = (char)('0' + value % 10);
                value /= 10;
        } while (value);

        while (length)
                into[at++] = scratch[--length];

        return at;
}

static long sd_hex(char *into, const unsigned char *bytes, long count)
{
        long at;

        for (at = 0; at < count; at++)
        {
                into[2 * at] = "0123456789abcdef"[bytes[at] >> 4];
                into[2 * at + 1] = "0123456789abcdef"[bytes[at] & 15];
        }

        return 2 * count;
}

static long sd_copy(char *into, const char *text)
{
        long at = 0;

        while (text[at])
        {
                into[at] = text[at];
                at++;
        }

        return at;
}

static sd_word sd_parse(const char *text)
{
        sd_word value = 0;

        while (*text >= '0' && *text <= '9')
                value = value * 10 + (sd_word)(*text++ - '0');

        return value;
}

//      One case from its index: the format, the input, and the shape --
//      'v' for a number, 't' for text, 'q' for a format that assigns nothing.
static char sd_case(sd_word index, char *format, const char **input)
{
        long at = 0;

        if (index < sd_number_cases())
        {
                sd_word pick = index % SD_COUNT(sd_numbers);
                sd_word spec = index / SD_COUNT(sd_numbers);
                long width = sd_widths[spec % SD_COUNT(sd_widths)];
                sd_word kind = spec / SD_COUNT(sd_widths);

                *input = sd_numbers[pick];
                format[at++] = '%';

                if (width >= 0)
                        at += sd_decimal(format + at, (sd_word)width);

                if (kind < SD_INTEGERS - 8)
                {
                        at += sd_copy(format + at, sd_lengths[kind % 8]);
                        format[at++] = "diuoxX"[kind / 8];
                }
                else if (kind < SD_INTEGERS)
                {
                        format[at++] = 'p';
                }
                else
                {
                        kind -= SD_INTEGERS;

                        if (kind & 1)
                                format[at++] = 'l';

                        format[at++] = "feEgGaAF"[kind / 2];
                }

                at += sd_copy(format + at, "%n");
                format[at] = 0;
                return 'v';
        }

        index -= sd_number_cases();

        if (index < sd_text_cases())
        {
                sd_word pick = index % SD_COUNT(sd_texts);
                sd_word spec = index / SD_COUNT(sd_texts);
                long width = sd_widths[spec % SD_COUNT(sd_widths)];

                *input = sd_texts[pick];
                format[at++] = '%';

                if (width >= 0)
                        at += sd_decimal(format + at, (sd_word)width);

                at += sd_copy(format + at,
                              sd_sets[spec / SD_COUNT(sd_widths)]);
                at += sd_copy(format + at, "%n");
                format[at] = 0;
                return 't';
        }

        index -= sd_text_cases();
        *input = sd_quiet_inputs[index % SD_COUNT(sd_quiet_inputs)];
        at += sd_copy(format, sd_quiet_formats[index / SD_COUNT(sd_quiet_inputs)]);
        format[at] = 0;
        return 'q';
}

//      first, count and "detail": a digest per 65536 cases of every answer,
//      every %n and every byte stored, or a line per case; the digests end
//      with how many cases there were.
static void scan_differential_main(const char *first_text,
                                   const char *count_text, const char *detail)
{
        static char line[512];
        static unsigned char room[64];
        char format[48];
        sd_word hash = 0xCBF29CE484222325ull;
        sd_word first = first_text[0] ? sd_parse(first_text) : 0;
        sd_word count = count_text[0] ? sd_parse(count_text) : sd_grid_size();
        sd_word index;
        long at;
        long i;

        if (first > sd_grid_size())
                first = sd_grid_size();

        if (count > sd_grid_size() - first)
                count = sd_grid_size() - first;

        for (index = first; index < first + count; index++)
        {
                const char *input;
                char shape = sd_case(index, format, &input);
                sd_word slot = 0xA5A5A5A5A5A5A5A5ull;
                int used = -1;
                int answer;
                long stored = 0;
                const unsigned char *bytes = room;

                for (i = 0; i < (long)sizeof(room); i++)
                        room[i] = 0x7F;

                if (shape == 'v')
                {
                        answer = sscanf(input, format, &slot, &used);
                        bytes = (const unsigned char *)&slot;
                        stored = 8;
                }
                else if (shape == 't')
                {
                        answer = sscanf(input, format, room, &used);
                        stored = sizeof(room);
                }
                else
                {
                        answer = sscanf(input, format);
                }

                if (detail[0] == 'd')
                {
                        at = sd_decimal(line, index);
                        line[at++] = ' ';
                        line[at++] = '[';
                        at += sd_copy(line + at, format);
                        line[at++] = ']';
                        line[at++] = ' ';
                        line[at++] = '[';

                        for (i = 0; input[i]; i++)
                        {
                                unsigned char c = (unsigned char)input[i];

                                if (c < ' ')
                                {
                                        line[at++] = '\\';
                                        line[at++] = 'x';
                                        at += sd_hex(line + at, &c, 1);
                                }
                                else
                                {
                                        line[at++] = (char)c;
                                }
                        }

                        line[at++] = ']';
                        line[at++] = ' ';

                        if (answer < 0)
                                line[at++] = '-';

                        at += sd_decimal(line + at, (sd_word)(answer < 0 ? -answer
                                                                         : answer));
                        line[at++] = ' ';

                        if (used < 0)
                                line[at++] = '-';

                        at += sd_decimal(line + at, (sd_word)(used < 0 ? -used : used));
                        line[at++] = ' ';
                        at += sd_hex(line + at, bytes, stored > 16 ? 16 : stored);
                        line[at++] = '\n';
                        line[at] = 0;
                        sd_text(line);
                        continue;
                }

                hash = (hash ^ (sd_word)(unsigned)answer) * 0x100000001B3ull;
                hash = (hash ^ (sd_word)(unsigned)used) * 0x100000001B3ull;

                for (i = 0; i < stored; i++)
                        hash = (hash ^ bytes[i]) * 0x100000001B3ull;

                if ((index + 1) % 65536 == 0 || index + 1 == first + count)
                {
                        at = sd_copy(line, "block ");
                        at += sd_decimal(line + at, index / 65536);
                        line[at++] = ' ';

                        for (i = 0; i < 8; i++)
                                room[i] = (unsigned char)(hash >> (56 - 8 * i));

                        at += sd_hex(line + at, room, 8);
                        line[at++] = '\n';
                        line[at] = 0;
                        sd_text(line);
                        hash = 0xCBF29CE484222325ull;
                }
        }

        if (detail[0] != 'd')
        {
                at = sd_copy(line, "cases ");
                at += sd_decimal(line + at, count);
                line[at++] = '\n';
                line[at] = 0;
                sd_text(line);
        }
}

#elif defined(SHARED_math_differential_body)
/*
        Experimental C standard library

        <math.h>, generated, against glibc's long double routines

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        CHECK_math_differential_reference writes, for every routine, every
        argument in the lists below -- each pair of the two-argument lists --
        and 8192 more drawn over the routine's domain, with the answer glibc's
        long double routine gives rounded back to a double: an eighty bit
        reference with eleven bits more than the answer it judges. The lists
        are every argument CHECK_math once pinned one table row at a time.
        CHECK_math_differential reads those lines, every field hexadecimal,
        on its standard input, answers each with this tree's routine and
        counts every answer more than the routine's allowance of units in
        the last place away: two for the transcendentals, which is what the
        family claims, and none for fmod and remainder, whose answers are
        exact.
*/
typedef unsigned long long md_word;

typedef struct
{
        const char *name;
        int arity;
        int allowance;
        char domain;
} md_routine;

static const md_routine md_routines[] = {
    {"sine", 1, 2, 't'}, {"cosine", 1, 2, 't'}, {"tangent", 1, 2, 't'},
    {"exponential", 1, 2, 'e'}, {"exponential_two", 1, 2, '2'},
    {"exponential_minus_one", 1, 2, 'e'}, {"logarithm", 1, 2, 'p'},
    {"logarithm_two", 1, 2, 'p'}, {"logarithm_ten", 1, 2, 'p'},
    {"arc_sine", 1, 2, 'u'}, {"arc_cosine", 1, 2, 'u'}, {"arc_tangent", 1, 2, 'g'},
    {"hyperbolic_sine", 1, 2, 'h'}, {"hyperbolic_cosine", 1, 2, 'h'},
    {"hyperbolic_tangent", 1, 2, 'g'}, {"cube_root", 1, 2, 'g'},
    {"power", 2, 2, 'w'}, {"arc_tangent_two", 2, 2, 'g'}, {"hypotenuse", 2, 2, 'g'},
    {"decimal_modulo", 2, 0, 'g'}, {"decimal_remainder", 2, 0, 'g'}};

#define MD_ROUTINES (sizeof(md_routines) / sizeof(md_routines[0]))

#ifdef MD_REFERENCE
static const md_word md_ones[] = {
    0x00000000000007e8ull, 0x3e45798ee2308c3aull, 0x3fb999999999999aull,
    0x3fe0000000000000ull, 0x3ff0000000000000ull, 0x3ff8000000000000ull,
    0x4000000000000000ull, 0x4008000000000000ull, 0x4024000000000000ull,
    0x4059000000000000ull, 0x412e848000000000ull, 0x430c6bf526340000ull,
    0x4480f0cf064dd592ull, 0x54b249ad2594c37dull, 0x7e37e43c8800759cull,
    0xc087480000000000ull, 0xc059000000000000ull, 0xbff0000000000000ull,
    0xbfd0000000000000ull, 0x3bc79ca10c924223ull, 0x3fd0000000000000ull,
    0x4004000000000000ull, 0x407f400000000000ull, 0x4086280000000000ull,
    0x0000000000000001ull, 0x01a56e1fc2f8f359ull, 0x3fefffffca501acbull,
    0x3ff000001ad7f29bull, 0x4376345785d8a000ull, 0x7fefffffffffffffull,
    0xbfefff2e48e8a71eull, 0xbfe8000000000000ull, 0xbfe0000000000000ull,
    0x0000000000000000ull, 0x3fe8000000000000ull, 0x3fefff2e48e8a71eull,
    0xc03e000000000000ull, 0xbddb7cdfd9d7bdbbull, 0x3ddb7cdfd9d7bdbbull,
    0x4036800000000000ull, 0x4085e00000000000ull, 0x4086300000000000ull,
    0x80000000000007e8ull, 0xbe45798ee2308c3aull, 0xbfb999999999999aull,
    0xbff8000000000000ull, 0xc000000000000000ull, 0xc008000000000000ull,
    0xc024000000000000ull, 0xc12e848000000000ull, 0xc30c6bf526340000ull,
    0xc480f0cf064dd592ull, 0xd4b249ad2594c37dull, 0xfe37e43c8800759cull,};
static const md_word md_firsts[] = {
    0x3fe0000000000000ull, 0x3ff8000000000000ull, 0x4000000000000000ull,
    0x4008000000000000ull, 0x4024000000000000ull, 0x54b249ad2594c37dull,
    0x2b2bff2ee48e0530ull, 0x3fe60755b7093d7full, 0xfe37e43c8800759cull,
    0xbff0000000000000ull, 0x81a56e1fc2f8f359ull, 0x0000000000000000ull,
    0x01a56e1fc2f8f359ull, 0x3ff0000000000000ull, 0x7e37e43c8800759cull,
    0x1eb67e9c127b6e74ull, 0x6126c2d4256ffcc3ull, 0x401e000000000000ull,
    0x0000000000000001ull,};
static const md_word md_seconds[] = {
    0xc08f440000000000ull, 0xc008000000000000ull, 0x3fe0000000000000ull,
    0x4004000000000000ull, 0x4008000000000000ull, 0x408f440000000000ull,
    0x408fff3333333333ull, 0xc090c80000000000ull, 0xfe37e43c8800759cull,
    0xbff0000000000000ull, 0x81a56e1fc2f8f359ull, 0x0000000000000000ull,
    0x01a56e1fc2f8f359ull, 0x3ff0000000000000ull, 0x7e37e43c8800759cull,
    0x1eb67e9c127b6e74ull, 0x6126c2d4256ffcc3ull, 0x54b249ad2594c37dull,
    0x401e000000000000ull, 0x0000000000000001ull,};

#define MD_COUNT(list) ((long)(sizeof(list) / sizeof((list)[0])))
#define MD_DRAWN 8192

static md_word md_next(md_word *state)
{
        md_word z = (*state += 0x9E3779B97F4A7C15ull);

        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
}

static double md_double(md_word bits)
{
        union
        {
                double value;
                md_word bits;
        } view;

        view.bits = bits;
        return view.value;
}

//      A double of the routine's domain: a stretch of the line where the
//      domain is short, and otherwise a random mantissa at a random binary
//      exponent of the range -- the trigonometry's within 2^70, power's
//      base within 2^20, the rest over the whole format.
static double md_draw(md_word *state, char domain)
{
        md_word a = md_next(state);
        md_word b = md_next(state);
        double unit = (double)(a >> 11) / 9007199254740992.0;
        md_word sign = (b & 1) << 63;
        md_word mantissa = a & ((1ull << 52) - 1);
        long exponent;

        switch (domain)
        {
        case 'u':
                return b & 1 ? -unit : unit;
        case 'e':
                return unit * 1454.0 - 745.0;
        case '2':
                return unit * 2099.0 - 1075.0;
        case 'h':
                return unit * 1420.0 - 710.0;
        case 't':
                exponent = (long)((b >> 8) % 140) - 70;
                break;
        case 'w':
                exponent = (long)((b >> 8) % 40) - 20;
                break;
        default:
                exponent = (long)((b >> 8) % 2100) - 1075;
        }

        if (domain == 'p')
                sign = 0;

        if (exponent <= -1023)
                return md_double(sign | mantissa);

        return md_double(sign | (md_word)(exponent + 1023) << 52 | mantissa);
}

static long md_cases(const md_routine *routine)
{
        return (routine->arity == 1 ? MD_COUNT(md_ones)
                                    : MD_COUNT(md_firsts) * MD_COUNT(md_seconds)) +
               MD_DRAWN;
}

static void md_case(long routine, long index, double *x, double *y)
{
        const md_routine *which = &md_routines[routine];
        long listed = md_cases(which) - MD_DRAWN;
        md_word state = 0x5eed000000000000ull ^ (md_word)routine << 32 ^ (md_word)index;

        *y = 0;

        if (index < listed)
        {
                if (which->arity == 1)
                {
                        *x = md_double(md_ones[index]);
                        return;
                }

                *x = md_double(md_firsts[index / MD_COUNT(md_seconds)]);
                *y = md_double(md_seconds[index % MD_COUNT(md_seconds)]);
                return;
        }

        *x = md_draw(&state, which->domain);

        if (which->arity == 2)
                *y = which->domain != 'w'        ? md_draw(&state, 'g')
                     : (md_next(&state) & 3) == 0 ? (double)((long)(md_next(&state) % 129) - 64)
                                                  : md_draw(&state, 'u') * 80.0;
}
#endif
#elif defined(SHARED_spool_body) || defined(SHARED_stream_body) || \
      defined(SHARED_stream_buffering_body) || defined(SHARED_stream_standard_body)
/* What each body measures a string with, so that neither side's C library is asked. */
static long body_length(const char *text)
{
        long n = 0;
        while (text[n]) n++;
        return n;
}

#if defined(SHARED_spool_body)
/*
        Experimental C standard library

        the rest of <stdio.h>, written once and compiled twice

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        One body, two implementations under it, which is the shape
        CHECK_stream_standard established and the reason it exists is the
        same here: glibc is on the machine, glibc defines what these functions
        answer, and a popen that is merely self-consistent is worth nothing.

        CHECK_spool puts this tree's entries underneath;
        CHECK_spool_reference puts the machine's glibc underneath. Both
        print the same trace on a correct implementation and the spool lane
        of test/run diffs them, on x86_64, arm64 and riscv64.

        WHAT IS TRACED AND WHAT IS NOT

        Names are not traced. A temporary name is random by design, so the two
        runs cannot agree on one and a diff of them would fail every time. So
        for everything in the temporary family the trace carries properties:
        how long the name is, what it starts with, whether the file exists,
        whether two draws differ, what mode the file was created with, what
        the errno is when the template is malformed. Those are the things a
        caller depends on and they are identical between any two correct
        implementations.

        Everything else is traced literally, including errno after each
        failure, because an implementation that fails with the wrong errno has
        failed differently from glibc and that is worth catching.

        Two things the wrapper supplies, because they cannot be written once:
        trace_number, which prints through whichever output layer that side
        has; and body_file_mode, which needs a stat structure whose fields are
        spelled differently on the two sides.

        WORK is the directory the driver was told to work in, as a string
        literal, so both runs write into their own tree.
*/

static void body_join(char *into, const char *leaf)
{
        const char *base = WORK;
        long at = 0;
        long i = 0;

        while (base[at])
        {
                into[at] = base[at];
                at++;
        }

        into[at] = '/';
        at++;

        while (leaf[i])
        {
                into[at] = leaf[i];
                at++;
                i++;
        }

        into[at] = 0;
}

static void body_make(const char *path, const char *content)
{
        FILE *f = fopen(path, "w");

        if (!f)
        {
                trace_number("make: fopen failed", 1);
                return;
        }

        fwrite(content, 1, (unsigned long)body_length(content), f);
        fclose(f);
}

static long body_exists(const char *path)
{
        return access(path, F_OK) == 0;
}

/*
        remove, which is the smallest of these and has four distinct answers.

        A regular file goes through unlink. A directory goes through rmdir,
        which is the half C leaves implementation-defined and POSIX requires.
        A missing path is ENOENT from unlink. A non-empty directory is the
        interesting one: unlink answers EISDIR, the fallback runs, and rmdir
        answers ENOTEMPTY -- so the errno a caller sees comes from the second
        call and not the first, and that is what is being pinned here.
*/
static void body_remove(void)
{
        char file[512];
        char folder[512];
        char inside[512];
        char missing[512];

        body_join(file, "removable");
        body_join(folder, "removable_folder");
        body_join(missing, "no_such_thing");

        body_make(file, "bytes");
        trace_number("remove file", remove(file));
        trace_number("remove file: exists after", body_exists(file));

        errno = 0;
        trace_number("remove missing", remove(missing));
        trace_number("remove missing: errno", errno);

        mkdir(folder, 0700);
        trace_number("remove empty folder", remove(folder));
        trace_number("remove empty folder: exists after", body_exists(folder));

        mkdir(folder, 0700);
        body_join(inside, "removable_folder/occupant");
        body_make(inside, "bytes");

        errno = 0;
        trace_number("remove full folder", remove(folder));
        trace_number("remove full folder: errno", errno);
        trace_number("remove full folder: exists after", body_exists(folder));

        remove(inside);
        remove(folder);
}

/*
        mkstemp and its relatives, traced by property.

        The four things a caller actually relies on: a descriptor comes back,
        the file is there, it is private to the owner, and two calls do not
        collide. Then the two
        refusals -- a template with no marks and a template too short -- with
        their errno, because a caller that checks for EINVAL is entitled to
        get it.
*/
static long body_same_text(const char *first, const char *second)
{
        long at = 0;

        while (first[at] && first[at] == second[at])
                at++;

        return first[at] == second[at];
}

static void body_temporary(void)
{
        char first[512];
        char second[512];
        char broken[512];
        int one;
        int two;

        body_join(first, "stemXXXXXX");
        body_join(second, "stemXXXXXX");

        one = mkstemp(first);
        two = mkstemp(second);

        trace_number("mkstemp: first is a descriptor", one >= 0);
        trace_number("mkstemp: second is a descriptor", two >= 0);
        trace_number("mkstemp: length unchanged",
                     body_length(first) == body_length(second));
        trace_number("mkstemp: names differ", !body_same_text(first, second));
        trace_number("mkstemp: first exists", body_exists(first));
        trace_number("mkstemp: mode", body_file_mode(first));

        if (one >= 0)
                close(one);

        if (two >= 0)
                close(two);

        remove(first);
        remove(second);

        body_join(broken, "nomarks");
        errno = 0;
        trace_number("mkstemp: no marks", mkstemp(broken));
        trace_number("mkstemp: no marks errno", errno);

        {
                char tiny[4];

                tiny[0] = 'X';
                tiny[1] = 'X';
                tiny[2] = 'X';
                tiny[3] = 0;

                errno = 0;
                trace_number("mkstemp: too short", mkstemp(tiny));
                trace_number("mkstemp: too short errno", errno);
        }
}

/*
        tmpnam, whose only checkable promises are that the name is shorter
        than L_tmpnam, that it does not exist at the moment it is handed over,
        and that two calls into caller buffers do not collide.
*/
static void body_temporary_name(void)
{
        char first[L_tmpnam + 1];
        char second[L_tmpnam + 1];
        const char *shared = tmpnam(0);

        tmpnam(first);
        tmpnam(second);

        trace_number("tmpnam: static answer given", shared != 0);
        trace_number("tmpnam: fits L_tmpnam", body_length(first) < L_tmpnam);
        trace_number("tmpnam: does not exist", !body_exists(first));
        trace_number("tmpnam: two draws differ",
                     !body_same_text(first, second));
        trace_number("tmpnam: absolute", first[0] == '/');
}

/*
        tmpfile, which has a name nowhere and must still behave like a file
        opened "w+": write, rewind, read the same bytes back, and be gone when
        it closes.
*/
static void body_temporary_stream(void)
{
        FILE *f = tmpfile();
        char back[8];
        long got;

        if (!f)
        {
                trace_number("tmpfile: opened", 0);
                return;
        }

        trace_number("tmpfile: opened", 1);
        trace_number("tmpfile: written",
                     (long)fwrite("abcdef", 1, 6, f));
        trace_number("tmpfile: tell after write", (long)ftell(f));

        rewind(f);

        got = (long)fread(back, 1, 6, f);
        back[got < 0 ? 0 : got] = 0;

        trace_number("tmpfile: read back", got);
        trace_number("tmpfile: bytes agree", body_same_text(back, "abcdef"));
        trace_number("tmpfile: closed", fclose(f) == 0);
}

/*
        fgetpos and fsetpos, which are ftell and fseek wearing an opaque type.

        Three positions and a round trip through each, because the failure
        this catches is an fpos_t that records nothing and an fsetpos that
        silently seeks to zero -- which passes any test that only ever marks
        the start.
*/
static void body_position(void)
{
        char path[512];
        FILE *f;
        fpos_t mark;
        fpos_t start;

        body_join(path, "positions");
        body_make(path, "0123456789");

        f = fopen(path, "r");

        if (!f)
        {
                trace_number("fgetpos: opened", 0);
                return;
        }

        trace_number("fgetpos: at start", fgetpos(f, &start) == 0);

        fseek(f, 4, SEEK_SET);
        trace_number("fgetpos: at four", fgetpos(f, &mark) == 0);
        trace_number("fgetpos: tell agrees", (long)ftell(f));

        fseek(f, 0, SEEK_END);
        trace_number("fgetpos: at end tell", (long)ftell(f));

        trace_number("fsetpos: back to four", fsetpos(f, &mark) == 0);
        trace_number("fsetpos: tell after", (long)ftell(f));
        trace_number("fsetpos: byte there", fgetc(f));

        trace_number("fsetpos: back to start", fsetpos(f, &start) == 0);
        trace_number("fsetpos: byte there", fgetc(f));

        fclose(f);
        remove(path);
}

/*
        popen and pclose, which is what this family is for.

        Six questions. Does a reading pipeline deliver the child's bytes. Does
        pclose hand back a raw wait status rather than an exit code -- the
        trace prints the raw number, so an implementation that returns 3 where
        glibc returns 768 fails here and nowhere else. Does a writing pipeline
        reach the child's standard input, which is checked by having the child
        write a file this process then reads. Does a command the shell cannot
        find come back 127, which is also the answer when there is no shell at
        all, since both are an execve that failed. Does an invalid mode
        refuse. And does a second pclose on a closed stream refuse rather than
        wait on a pid that is no longer ours.
*/
static void body_pipeline(void)
{
        char path[512];
        char line[128];
        FILE *f;
        int status;

        f = popen("printf 'alpha\\nbeta\\n'", "r");

        if (!f)
        {
                trace_number("popen r: opened", 0);
                return;
        }

        trace_number("popen r: opened", 1);
        trace_number("popen r: first line got",
                     fgets(line, (int)sizeof(line), f) != 0);
        trace_number("popen r: first line is alpha",
                     body_same_text(line, "alpha\n"));
        trace_number("popen r: second line got",
                     fgets(line, (int)sizeof(line), f) != 0);
        trace_number("popen r: second line is beta",
                     body_same_text(line, "beta\n"));
        trace_number("popen r: third line absent",
                     fgets(line, (int)sizeof(line), f) == 0);
        trace_number("popen r: eof", feof(f) != 0);
        trace_number("popen r: pclose raw", (long)pclose(f));

        f = popen("exit 3", "r");
        status = pclose(f);
        trace_number("popen: exit 3 raw", (long)status);

        f = popen("exit 0", "r");
        trace_number("popen: exit 0 raw", (long)pclose(f));

        f = popen("kill -TERM $$", "r");
        trace_number("popen: killed raw", (long)pclose(f));

        f = popen("no_such_command_anywhere_xyz 2>/dev/null", "r");
        trace_number("popen: missing command raw", (long)pclose(f));

        body_join(path, "written_by_child");
        {
                char command[600];
                long at = 0;
                const char *lead = "cat > ";

                while (lead[at])
                {
                        command[at] = lead[at];
                        at++;
                }

                {
                        long i = 0;

                        while (path[i])
                        {
                                command[at] = path[i];
                                at++;
                                i++;
                        }

                        command[at] = 0;
                }

                f = popen(command, "w");
        }

        if (!f)
        {
                trace_number("popen w: opened", 0);
                return;
        }

        trace_number("popen w: opened", 1);
        trace_number("popen w: written", (long)fwrite("gamma\n", 1, 6, f));
        trace_number("popen w: pclose raw", (long)pclose(f));
        trace_number("popen w: child wrote the file", body_exists(path));

        f = fopen(path, "r");

        if (f)
        {
                line[0] = 0;
                fgets(line, (int)sizeof(line), f);
                trace_number("popen w: bytes arrived",
                             body_same_text(line, "gamma\n"));
                fclose(f);
        }

        remove(path);

        errno = 0;
        trace_number("popen: bad mode", popen("true", "q") != 0);
        trace_number("popen: bad mode errno", errno);

        //      A null command is deliberately not asked here. glibc does
        //      not check for it -- the pointer goes straight into the argv it
        //      hands execve -- so the reference would not answer a question,
        //      it would misbehave. This tree refuses it with EINVAL, which is
        //      checked in the freestanding half of the lane where there is no
        //      reference to disagree with.
}

/*
        The small entries, which are traced for effect rather than for a
        return value.

        setlinebuf and setbuffer are policy changes with nothing to report, so
        what is checked is that output written after them still arrives and in
        order. __fpurge is checked by the only thing that distinguishes it
        from fflush: bytes written before it must NOT arrive.
*/
static void body_buffering(void)
{
        char path[512];
        FILE *f;
        char back[64];
        long got;

        body_join(path, "buffered");

        f = fopen(path, "w");
        setlinebuf(f);
        fwrite("line\n", 1, 5, f);
        fclose(f);

        f = fopen(path, "r");
        got = (long)fread(back, 1, 63, f);
        back[got < 0 ? 0 : got] = 0;
        fclose(f);

        trace_number("setlinebuf: bytes arrived", body_same_text(back, "line\n"));

        f = fopen(path, "w");
        {
                static char own[64];

                setbuffer(f, own, 64);
                fwrite("held\n", 1, 5, f);
                fclose(f);
        }

        f = fopen(path, "r");
        got = (long)fread(back, 1, 63, f);
        back[got < 0 ? 0 : got] = 0;
        fclose(f);

        trace_number("setbuffer: bytes arrived", body_same_text(back, "held\n"));

        f = fopen(path, "w");
        fwrite("dropped", 1, 7, f);
        __fpurge(f);
        fwrite("kept", 1, 4, f);
        fclose(f);

        f = fopen(path, "r");
        got = (long)fread(back, 1, 63, f);
        back[got < 0 ? 0 : got] = 0;
        fclose(f);

        trace_number("__fpurge: dropped the first write",
                     body_same_text(back, "kept"));

        remove(path);
}

/*
        fmemopen, read mode only, which is the only mode this tree defines.

        The reference has all of glibc's modes; the trace only ever asks for
        "r", so both sides answer the same thing and the divergence stays
        where it is written down rather than showing up as a failure here.
*/
static void body_memory_stream(void)
{
        static char source[] = "one\ntwo\nthree\n";
        FILE *f = fmemopen(source, sizeof(source) - 1, "r");
        char line[64];

        if (!f)
        {
                trace_number("fmemopen: opened", 0);
                return;
        }

        trace_number("fmemopen: opened", 1);

        fgets(line, (int)sizeof(line), f);
        trace_number("fmemopen: first line", body_same_text(line, "one\n"));

        fgets(line, (int)sizeof(line), f);
        trace_number("fmemopen: second line", body_same_text(line, "two\n"));

        trace_number("fmemopen: tell", (long)ftell(f));

        fseek(f, 0, SEEK_SET);
        fgets(line, (int)sizeof(line), f);
        trace_number("fmemopen: rewound", body_same_text(line, "one\n"));

        trace_number("fmemopen: closed", fclose(f) == 0);
}

/*
        fmemopen across the sizes its first buffer is cut at.

        A memory stream here is a memfd whose first buffer's worth is placed
        straight into the stream's buffer when it is opened, with the kernel
        offset left just past it. So the sizes that matter are the ones either
        side of that buffer -- a byte, just under, at and just over it, a page,
        and several buffers -- and each is pushed back at the start, read to
        the end a byte at a time with ftell looked at on the way, sought back
        into the middle and read again.
*/
static void body_memory_sizes(void)
{
        static const long sizes[] = {1, 63, 4087, 4088, 4089, 4095, 4096, 4097, 12001};
        static char data[12001];
        static unsigned char again[64];
        unsigned long i;
        long at;

        for (at = 0; at < 12001; at++)
                data[at] = (char)('a' + (at * 5) % 26);

        for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        {
                long size = sizes[i];
                FILE *f = fmemopen(data, (unsigned long)size, "r");
                long count = 0;
                long sum = 0;
                unsigned long got;
                int c;

                trace_number("fmemopen sizes: size", size);
                trace_number("fmemopen sizes: opened", f != 0);

                if (!f)
                        continue;

                trace_number("fmemopen sizes: tell at start", ftell(f));
                trace_number("fmemopen sizes: ungetc at start", (long)ungetc('!', f));
                trace_number("fmemopen sizes: getc after ungetc", (long)fgetc(f));

                while ((c = fgetc(f)) != EOF)
                {
                        count++;
                        sum = (sum * 31 + c) % 1000000007L;

                        if (count % 1024 == 1 || count == size - 1)
                                trace_number("fmemopen sizes: tell", ftell(f));
                }

                trace_number("fmemopen sizes: bytes", count);
                trace_number("fmemopen sizes: sum", sum);
                trace_number("fmemopen sizes: feof", (long)feof(f));
                trace_number("fmemopen sizes: tell at end", ftell(f));
                trace_number("fmemopen sizes: seek middle", (long)fseek(f, size / 2, SEEK_SET));
                got = fread(again, 1, sizeof(again), f);
                sum = 0;

                for (at = 0; at < (long)got; at++)
                        sum = (sum * 31 + again[at]) % 1000000007L;

                trace_number("fmemopen sizes: fread", (long)got);
                trace_number("fmemopen sizes: fread sum", sum);
                trace_number("fmemopen sizes: tell after fread", ftell(f));
                trace_number("fmemopen sizes: fclose", (long)fclose(f));
        }
}

/*
        The unlocked spellings, which must answer exactly what the locked ones
        do. On glibc they are a different code path with the lock removed; here
        they are the same function. Either way the answers agree or one of the
        two is wrong.
*/
static void body_unlocked(void)
{
        char path[512];
        FILE *f;

        body_join(path, "unlocked");

        f = fopen(path, "w");
        putc_unlocked('a', f);
        fputc_unlocked('b', f);
        fputs_unlocked("cd", f);
        fwrite_unlocked("ef", 1, 2, f);
        trace_number("unlocked: fileno is a descriptor", fileno_unlocked(f) >= 0);
        trace_number("unlocked: flush", fflush_unlocked(f) == 0);
        fclose(f);

        f = fopen(path, "r");
        trace_number("unlocked: getc", getc_unlocked(f));
        trace_number("unlocked: fgetc", fgetc_unlocked(f));

        {
                char rest[8];

                trace_number("unlocked: fread",
                             (long)fread_unlocked(rest, 1, 4, f));
                rest[4] = 0;
                trace_number("unlocked: bytes agree",
                             body_same_text(rest, "cdef"));
        }

        trace_number("unlocked: at end", getc_unlocked(f) == EOF);
        trace_number("unlocked: eof", feof_unlocked(f) != 0);
        trace_number("unlocked: error", ferror_unlocked(f) != 0);

        clearerr_unlocked(f);
        trace_number("unlocked: eof cleared", feof_unlocked(f) != 0);

        fclose(f);
        remove(path);

        flockfile(stdout);
        trace_number("flockfile: try while held", ftrylockfile(stdout) == 0);
        funlockfile(stdout);
}

static void trace_body(void)
{
        body_remove();
        body_temporary();
        body_temporary_name();
        body_temporary_stream();
        body_position();
        body_pipeline();
        body_buffering();
        body_memory_stream();
        body_memory_sizes();
        body_unlocked();
}

#elif defined(SHARED_stream_body)
/*
        Experimental C standard library

        what a FILE does, written once and compiled twice

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        One body, two implementations under it.

        CHECK_stream puts this tree's streams underneath;
        CHECK_stream_reference puts the machine's glibc underneath. Both
        print the same trace on a correct implementation and the stream lane
        of test/run diffs them, on x86_64, arm64 and riscv64.

        The trace is the test, not the bytes. Every classic hand-written stdio
        bug leaves the byte stream correct: feof set one call early still hands
        back every byte, fread returning bytes rather than items still copies
        the right memory, an ftell that models the kernel offset instead of
        asking it still answers correctly until somebody uses fileno. So after
        every single call this prints the return value, ftell, feof and ferror,
        and then the files the run produced are compared byte for byte as well.

        WORK is the directory the driver was told to work in, as a string
        literal, so both runs write to their own and the two trees are diffed
        afterwards.
*/

/* A fresh native subprocess exhausts allocation, never the parent or emulator. */
static int body_allocation_failure(void)
{
        int descriptor = open("/dev/null", 1, 0);
        if (descriptor < 0 || !body_limit_memory()) return 2;
        __typeof__(&malloc) volatile take = &malloc;
        while (take(sizeof(FILE))) {}
        errno = 0;
        FILE *f = fdopen(descriptor, "a");
        int saved = errno;
        int flags = fcntl(descriptor, 3, 0UL);
        int failed = f != 0 || saved != ENOMEM || flags < 0;
        if (f) fclose(f); else close(descriptor);
        descriptor = dup(1);
        close(descriptor);
        errno = 0;
        f = fopen("/dev/null", "r");
        saved = errno;
        if (f) fclose(f);
        int next = dup(1);
        close(next);
        return failed || f != 0 || saved != ENOMEM || next != descriptor;
}

static void trace_run(const char *label, const unsigned char *data, long length)
{
        long i;
        trace_number(label, length);
        for (i = 0; i < length; i++)
                trace_number("    byte", (long)data[i]);
}

static void trace_state(const char *label, FILE *f)
{
        trace_number(label, 0);
        trace_number("    ftell", (long)ftell(f));
        trace_number("    feof", (long)feof(f));
        trace_number("    ferror", (long)ferror(f));
}

static long body_file_size(const char *path)
{
        FILE *f = fopen(path, "r");
        long size;

        if (!f)
                return -1;

        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
        return size;
}

static void body_make(const char *path, const char *content)
{
        FILE *f = fopen(path, "w");
        long length = body_length(content);
        trace_number("make: fwrite items", (long)fwrite(content, 1, (unsigned long)length, f));
        trace_number("make: fclose", (long)fclose(f));
        trace_number("make: size", body_file_size(path));
}

/*      Reading three bytes out of a three byte file, one at a time.

        The point of this block is the pair of feof readings around each
        getc. A stream sitting on the last byte has not seen the end; the
        end arrives on the call after the last byte comes out, and an
        implementation that sets the indicator when its refill came back
        short instead of empty says so one call early. */
static void body_getc_to_the_end(void)
{
        FILE *f = fopen(WORK "/three.txt", "r");
        int i;

        trace_number("getc-to-end: opened", f != 0);

        for (i = 0; i < 5; i++)
        {
                trace_number("getc-to-end: feof before", (long)feof(f));
                trace_number("getc-to-end: getc", (long)fgetc(f));
                trace_number("getc-to-end: feof after", (long)feof(f));
                trace_number("getc-to-end: ferror", (long)ferror(f));
                trace_number("getc-to-end: ftell", (long)ftell(f));
        }

        trace_number("getc-to-end: fclose", (long)fclose(f));
}

/*      fread returns items, and a partial item at the end is not one. */
static void body_fread_items(void)
{
        unsigned char buffer[64];
        FILE *f = fopen(WORK "/three.txt", "r");
        unsigned long got;

        got = fread(buffer, 4, 2, f);
        trace_number("fread-items: eight wanted from three", (long)got);
        trace_state("fread-items: after", f);
        trace_run("fread-items: buffer", buffer, 3);

        got = fread(buffer, 1, 10, f);
        trace_number("fread-items: again at the end", (long)got);
        trace_state("fread-items: after again", f);
        fclose(f);

        f = fopen(WORK "/three.txt", "r");
        trace_number("fread-items: size zero", (long)fread(buffer, 0, 5, f));
        trace_state("fread-items: after size zero", f);
        trace_number("fread-items: count zero", (long)fread(buffer, 5, 0, f));
        trace_state("fread-items: after count zero", f);
        trace_number("fread-items: then getc", (long)fgetc(f));

        got = fread(buffer, 1, 1, f);
        trace_number("fread-items: one byte", (long)got);
        trace_run("fread-items: one byte value", buffer, 1);
        trace_state("fread-items: after one byte", f);
        fclose(f);
}

/*      A large fread, which takes the path that reads straight into the
        caller's memory rather than through the buffer. */
static void body_fread_large(void)
{
        static unsigned char buffer[20000];
        FILE *f = fopen(WORK "/large.txt", "r");
        unsigned long got = fread(buffer, 1, sizeof(buffer), f);
        long i;
        long sum = 0;

        trace_number("fread-large: got", (long)got);
        trace_state("fread-large: after", f);

        for (i = 0; i < (long)got; i++)
                sum += buffer[i];

        trace_number("fread-large: checksum", sum);
        trace_number("fread-large: at the end now", (long)fgetc(f));
        trace_state("fread-large: after the end", f);
        fclose(f);
}

/*      ungetc before anything has been read, which is where a design that
        pushes back into the read buffer has nowhere to put the byte. */
static void body_ungetc_at_the_start(void)
{
        FILE *f = fopen(WORK "/three.txt", "r");

        trace_state("ungetc-start: fresh", f);
        trace_number("ungetc-start: ungetc", (long)ungetc('X', f));
        trace_state("ungetc-start: after ungetc", f);
        trace_number("ungetc-start: getc", (long)fgetc(f));
        trace_state("ungetc-start: after getc", f);
        trace_number("ungetc-start: getc again", (long)fgetc(f));
        trace_state("ungetc-start: after getc again", f);
        trace_number("ungetc-start: refused EOF", (long)ungetc(EOF, f));
        fclose(f);
}

/*      ungetc after the end has been seen has to clear the indicator, or the
        byte the stream is holding can never come back out. */
static void body_ungetc_at_the_end(void)
{
        FILE *f = fopen(WORK "/three.txt", "r");

        while (fgetc(f) != EOF)
                ;

        trace_state("ungetc-end: at the end", f);
        trace_number("ungetc-end: ungetc", (long)ungetc('Z', f));
        trace_state("ungetc-end: after ungetc", f);
        trace_number("ungetc-end: getc", (long)fgetc(f));
        trace_state("ungetc-end: after getc", f);
        trace_number("ungetc-end: getc again", (long)fgetc(f));
        trace_state("ungetc-end: after getc again", f);
        fclose(f);
}

/*      Seeking with a partly consumed buffer behind us: a relative seek has
        to be measured from where the caller is, not from where the kernel
        was left after a refill. */
static void body_seeking(void)
{
        FILE *f = fopen(WORK "/lines.txt", "r");
        long size = body_file_size(WORK "/lines.txt");

        trace_number("seek: file size", size);
        fgetc(f);
        fgetc(f);
        fgetc(f);
        trace_state("seek: three bytes in", f);

        trace_number("seek: cur +2", (long)fseek(f, 2, SEEK_CUR));
        trace_state("seek: after cur +2", f);
        trace_number("seek: getc", (long)fgetc(f));

        trace_number("seek: cur -1", (long)fseek(f, -1, SEEK_CUR));
        trace_state("seek: after cur -1", f);
        trace_number("seek: getc", (long)fgetc(f));

        trace_number("seek: end", (long)fseek(f, 0, SEEK_END));
        trace_state("seek: after end", f);
        trace_number("seek: getc at end", (long)fgetc(f));
        trace_state("seek: end seen", f);

        trace_number("seek: set 1 clears eof", (long)fseek(f, 1, SEEK_SET));
        trace_state("seek: after set 1", f);
        trace_number("seek: getc", (long)fgetc(f));

        trace_number("seek: end -3", (long)fseek(f, -3, SEEK_END));
        trace_state("seek: after end -3", f);
        trace_number("seek: getc", (long)fgetc(f));

        trace_number("seek: after ungetc, seek", (long)ungetc('Q', f));
        trace_number("seek: set 0", (long)fseek(f, 0, SEEK_SET));
        trace_state("seek: after set 0", f);
        trace_number("seek: getc", (long)fgetc(f));
        fclose(f);
}

/*      rewind clears both indicators and fseek clears only one. */
static void body_rewind(void)
{
        FILE *f = fopen(WORK "/three.txt", "r");

        while (fgetc(f) != EOF)
                ;

        trace_state("rewind: at the end", f);
        rewind(f);
        trace_state("rewind: after rewind", f);
        trace_number("rewind: getc", (long)fgetc(f));
        fclose(f);
}

/*      An update stream: bytes written and then read back through the same
        FILE, with the seek in between doing the flushing. */
static void body_update_stream(void)
{
        unsigned char buffer[64];
        FILE *f = fopen(WORK "/update.txt", "w+");
        unsigned long got;

        trace_number("update: fwrite", (long)fwrite("0123456789", 1, 10, f));
        trace_state("update: after write", f);
        trace_number("update: size before seek", body_file_size(WORK "/update.txt"));

        trace_number("update: seek 0", (long)fseek(f, 0, SEEK_SET));
        trace_number("update: size after seek", body_file_size(WORK "/update.txt"));
        trace_state("update: at the start", f);

        got = fread(buffer, 1, 10, f);
        trace_number("update: fread", (long)got);
        trace_run("update: bytes", buffer, (long)got);
        trace_state("update: after read", f);
        trace_number("update: getc past the end", (long)fgetc(f));
        trace_state("update: end seen", f);

        trace_number("update: seek 3", (long)fseek(f, 3, SEEK_SET));
        trace_number("update: putc", (long)fputc('X', f));
        trace_state("update: after putc", f);
        trace_number("update: seek 0 again", (long)fseek(f, 0, SEEK_SET));
        got = fread(buffer, 1, 10, f);
        trace_run("update: bytes again", buffer, (long)got);
        trace_number("update: fclose", (long)fclose(f));
        trace_number("update: final size", body_file_size(WORK "/update.txt"));
}

/*      fgets: the terminator is always written, null means nothing at all was
        read, and a line longer than the buffer comes out in pieces. */
static void body_fgets(void)
{
        char buffer[8];
        FILE *f = fopen(WORK "/lines.txt", "r");
        char *result;

        while ((result = fgets(buffer, 8, f)) != 0)
        {
                trace_run("fgets 8: line", (const unsigned char *)buffer,
                          body_length(buffer));
                trace_state("fgets 8: after", f);
        }

        trace_number("fgets 8: null returned", 1);
        trace_state("fgets 8: at the end", f);
        fclose(f);

        f = fopen(WORK "/lines.txt", "r");

        while ((result = fgets(buffer, 3, f)) != 0)
                trace_run("fgets 3: piece", (const unsigned char *)buffer,
                          body_length(buffer));

        trace_state("fgets 3: at the end", f);
        fclose(f);

        f = fopen(WORK "/lines.txt", "r");
        buffer[0] = '!';
        result = fgets(buffer, 1, f);
        trace_number("fgets 1: returned something", result != 0);
        trace_run("fgets 1: content", (const unsigned char *)buffer,
                  body_length(buffer));
        trace_state("fgets 1: after", f);
        trace_number("fgets 1: getc", (long)fgetc(f));
        fclose(f);
}

/*      getline and getdelim, including the file whose last line has no
        newline on it -- the one every line reader gets wrong. */
static void body_getline(void)
{
        char *line = 0;
        unsigned long capacity = 0;
        long got;
        FILE *f = fopen(WORK "/lines.txt", "r");

        while ((got = getline(&line, &capacity, f)) != -1)
        {
                trace_number("getline: length", got);
                trace_run("getline: bytes", (const unsigned char *)line, got);
                trace_state("getline: after", f);
        }

        trace_number("getline: minus one", got);
        trace_state("getline: at the end", f);
        fclose(f);

        f = fopen(WORK "/colons.txt", "r");

        while ((got = getdelim(&line, &capacity, ':', f)) != -1)
        {
                trace_number("getdelim: length", got);
                trace_run("getdelim: bytes", (const unsigned char *)line, got);
        }

        trace_state("getdelim: at the end", f);
        fclose(f);
        free(line);

        line = 0;
        capacity = 0;
        f = fopen(WORK "/large.txt", "r");
        got = getline(&line, &capacity, f);
        trace_number("getline: one long line", got);
        trace_number("getline: first byte", line ? (long)(unsigned char)line[0] : -2);
        trace_number("getline: last byte",
                     (got > 0) ? (long)(unsigned char)line[got - 1] : -2);
        trace_state("getline: after the long line", f);
        fclose(f);
        free(line);
}

/*      Buffering policy, observed rather than asserted: the size of the file
        on disk while the stream still has bytes in hand is the only way to
        see which of the three modes is in force. Under an emulator with a
        pipe on the far end nothing is a terminal, so the line buffered case
        is reached through setvbuf rather than by asking isatty. */
static void body_buffering(void)
{
        FILE *f;

        f = fopen(WORK "/buffered.txt", "w");
        setvbuf(f, 0, _IOFBF, 4096);
        fwrite("hello", 1, 5, f);
        trace_number("full: size while held", body_file_size(WORK "/buffered.txt"));
        fwrite("\n", 1, 1, f);
        trace_number("full: size after newline", body_file_size(WORK "/buffered.txt"));
        fflush(f);
        trace_number("full: size after flush", body_file_size(WORK "/buffered.txt"));
        fclose(f);

        f = fopen(WORK "/line.txt", "w");
        setvbuf(f, 0, _IOLBF, 4096);
        fwrite("hello", 1, 5, f);
        trace_number("line: size while held", body_file_size(WORK "/line.txt"));
        fwrite("\n", 1, 1, f);
        trace_number("line: size after newline", body_file_size(WORK "/line.txt"));
        fwrite("tail", 1, 4, f);
        trace_number("line: size after tail", body_file_size(WORK "/line.txt"));
        trace_number("line: fclose", (long)fclose(f));
        trace_number("line: final size", body_file_size(WORK "/line.txt"));

        f = fopen(WORK "/none.txt", "w");
        setvbuf(f, 0, _IONBF, 0);
        fwrite("hello", 1, 5, f);
        trace_number("none: size while held", body_file_size(WORK "/none.txt"));
        fputc('!', f);
        trace_number("none: size after putc", body_file_size(WORK "/none.txt"));
        trace_state("none: state", f);
        fclose(f);

        f = fopen(WORK "/reader.txt", "r");
        setvbuf(f, 0, _IONBF, 0);
        trace_number("none: getc", (long)fgetc(f));
        trace_state("none: after getc", f);
        trace_number("none: getc", (long)fgetc(f));
        trace_state("none: after second getc", f);
        fclose(f);
}

/* Invalid whence retains pushback; an unseekable pipe retains unread data. */
static void body_failed_seek(void)
{
        for (int piped = 0; piped < 2; piped++)
        {
                int ends[2];
                FILE *f;
                if (piped)
                {
                        if (pipe(ends)) return;
                        write(ends[1], "abc", 3);
                        close(ends[1]);
                        f = fdopen(ends[0], "r");
                }
                else
                        f = fopen(WORK "/three.txt", "r");
                trace_number("failed-seek: first", fgetc(f));
                if (!piped)
                        trace_number("failed-seek: pushback", ungetc('Z', f));
                errno = 0;
                trace_number("failed-seek: rejected", fseek(f, 0, piped ? SEEK_SET : 999) != 0);
                trace_number("failed-seek: errno", errno);
                trace_number("failed-seek: next byte retained", fgetc(f));
                trace_number("failed-seek: unread byte retained", fgetc(f));
                fclose(f);
        }
}

static void body_descriptor_errors(void)
{
        errno = 0;
        FILE *f = fopen(WORK "/missing", "r");
        trace_number("open-error: rejected", f == 0);
        trace_number("open-error: errno", errno);
        errno = 0;
        f = fdopen(-1, "r");
        trace_number("adopt-error: rejected", f == 0);
        trace_number("adopt-error: errno", errno);
        f = fopen(WORK "/three.txt", "r");
        int descriptor = dup(fileno(f));
        fclose(f);
        errno = 0;
        f = fdopen(descriptor, "w");
        trace_number("adopt-access: rejected", f == 0);
        trace_number("adopt-access: errno", errno);
        if (f) fclose(f); else close(descriptor);
        f = fopen(WORK, "r");
        errno = 0;
        trace_number("read-error: byte", fgetc(f));
        trace_number("read-error: errno", errno);
        fclose(f);
}

static void body_append(void)
{
        FILE *f = fopen(WORK "/append.txt", "w");

        fwrite("one\n", 1, 4, f);
        fclose(f);

        f = fopen(WORK "/adopt-append.txt", "w+");
        fwrite("abc", 1, 3, f);
        fflush(f);
        int descriptor = dup(fileno(f));
        fclose(f);
        f = fdopen(descriptor, "a");
        trace_number("adopt-append: seek start", fseek(f, 0, SEEK_SET));
        trace_number("adopt-append: write", fputc('X', f));
        trace_number("adopt-append: close", fclose(f));
        trace_number("adopt-append: size", body_file_size(WORK "/adopt-append.txt"));

        f = fopen(WORK "/append.txt", "a");
        trace_state("append: freshly opened", f);
        fwrite("two\n", 1, 4, f);
        trace_state("append: after write", f);
        fflush(f);
        trace_state("append: after flush", f);
        fclose(f);
        trace_number("append: size", body_file_size(WORK "/append.txt"));

        f = fopen(WORK "/append.txt", "a+");
        trace_number("append-update: initial position", ftell(f));
        trace_number("append-update: initial read", fgetc(f));
        fclose(f);

        f = fopen(WORK "/append.txt", "r");
        {
                unsigned char buffer[32];
                unsigned long got = fread(buffer, 1, sizeof(buffer), f);
                trace_run("append: content", buffer, (long)got);
        }
        fclose(f);
}

/*      fputs, fputc, fwrite item counts, fileno, freopen and fflush(null). */
static void body_odds_and_ends(void)
{
        FILE *f = fopen(WORK "/odds.txt", "w");
        int descriptor;

        trace_number("odds: fputs non negative", fputs("first\n", f) >= 0);
        trace_number("odds: fputc", (long)fputc('!', f));
        trace_number("odds: fwrite items", (long)fwrite("abcdefghijkl", 3, 4, f));
        trace_number("odds: fwrite zero size", (long)fwrite("x", 0, 4, f));
        trace_number("odds: fwrite zero count", (long)fwrite("x", 4, 0, f));
        descriptor = fileno(f);
        trace_number("odds: fileno is a descriptor", descriptor >= 0);
        trace_number("odds: flush all", (long)fflush(0));
        trace_number("odds: size after flush all", body_file_size(WORK "/odds.txt"));

        f = freopen(WORK "/odds2.txt", "w", f);
        trace_number("odds: freopen gave a stream", f != 0);
        trace_number("odds: fputs after freopen", fputs("second\n", f) >= 0);
        trace_number("odds: fclose", (long)fclose(f));
        trace_number("odds: first file size", body_file_size(WORK "/odds.txt"));
        trace_number("odds: second file size", body_file_size(WORK "/odds2.txt"));

        f = fopen(WORK "/odds.txt", "r");
        {
                unsigned char buffer[64];
                unsigned long got = fread(buffer, 1, sizeof(buffer), f);
                trace_run("odds: first content", buffer, (long)got);
        }
        fclose(f);
}

/*      Everything a program is allowed to hand these routines that is not
        the ordinary case: a path that does not exist, a mode that is not a
        mode, a write to a stream opened for reading. Both sides run all of
        it, and both sides agree, which is worth more than the guess that
        glibc would be undefined here -- it is not, and the trace says so. */
static void body_refusals(void)
{
        FILE *f = fopen(WORK "/nowhere/at/all.txt", "r");
        trace_number("refusals: missing file", f == 0);

        f = fopen(WORK "/three.txt", "q");
        trace_number("refusals: bad mode", f == 0);

        f = fopen(WORK "/three.txt", "r");
        trace_number("refusals: write to a read stream",
                     (long)fwrite("x", 1, 1, f));
        trace_number("refusals: ferror set", (long)ferror(f));
        clearerr(f);
        trace_number("refusals: cleared", (long)ferror(f));
        trace_number("refusals: setvbuf bad mode", (long)setvbuf(f, 0, 77, 0));
        fclose(f);

        {
                unsigned char scratch[4];
                f = fopen(WORK "/writeonly.txt", "w");
                trace_number("refusals: read from a write stream",
                             (long)fread(scratch, 1, 1, f));
                fclose(f);
        }
}

/*      /dev/full accepts an open and refuses every write, which is the only
        way to reach the failure paths without a full disk. What has to come
        out of it: a short item count, the error indicator set, EOF from
        fflush and EOF from fclose. */
static void body_write_failure(void)
{
        static char block[9000];
        FILE *f = fopen("/dev/full", "w");
        long i;

        for (i = 0; i < (long)sizeof(block); i++)
                block[i] = 'z';

        trace_number("full-device: opened", f != 0);

        if (!f)
                return;

        errno = 0;
        trace_number("full-device: fwrite items", (long)fwrite(block, 1, 9000, f));
        trace_number("full-device: fwrite errno", errno);
        trace_number("full-device: ferror", (long)ferror(f));
        trace_number("full-device: fflush", (long)fflush(f));
        trace_number("full-device: ferror after flush", (long)ferror(f));
        trace_number("full-device: feof", (long)feof(f));
        clearerr(f);
        trace_number("full-device: cleared", (long)ferror(f));
        trace_number("full-device: fputc", (long)fputc('q', f));
        errno = 0;
        trace_number("full-device: fclose", (long)fclose(f));
        trace_number("full-device: fclose errno", errno);
}

/*
        Every entry that stages bytes, walked over the edges its fast paths
        decide.

        The resident paths -- putc into a buffer already holding bytes, a run
        that fits with a byte to spare, a printf piece copied straight into
        the buffer, a pad filled in place, a whole formatted line staged for
        an unbuffered stream -- each decide a boundary the general path used
        to decide for them: exactly full, one byte short, a newline at the
        start, in the middle, at the end or nowhere, a width wider than the
        window or than the stage. So the walk is over buffer sizes either side
        of every width the copies are cut at, in all three buffering
        policies, with a seeded run of writes through every entry. After each
        one the answer, the position and the error indicator are traced, and
        the size of the file where every correct implementation must agree on
        it: an unbuffered stream has written everything by the time the call
        returns. The files the walk leaves behind are compared byte for byte
        by the lane with every other file here.

        What glibc cannot be asked -- whether a buffer was left full, whether
        a newline is still waiting on a line buffered stream, whether waiting
        bytes carry this process's stamp, whether the open list still links
        up -- the wrapper asks through body_stream_invariants, and a broken
        invariant is a trace line glibc never prints.
*/
#ifndef body_stream_invariants
#define body_stream_invariants(f) ((void)(f))
#endif

static unsigned long body_walk_state;

static unsigned long body_walk(unsigned long bound)
{
        body_walk_state = body_walk_state * 6364136223846793005UL +
                          1442695040888963407UL;
        return bound ? (body_walk_state >> 33) % bound : 0;
}

//      Letters, with newlines nowhere, first, in the middle, last, or at both
//      ends, as the walk says.
static long body_walk_text(char *into, unsigned long room)
{
        long n = (long)body_walk(room);
        long i;

        for (i = 0; i < n; i++)
                into[i] = (char)('a' + (i * 7 + n) % 23);

        switch (body_walk(5))
        {
        case 1: if (n) into[0] = '\n'; break;
        case 2: if (n) into[n / 2] = '\n'; break;
        case 3: if (n) into[n - 1] = '\n'; break;
        case 4: if (n) { into[0] = '\n'; into[n - 1] = '\n'; } break;
        default: break;
        }

        into[n] = 0;
        return n;
}

static void body_walk_write(FILE *f, long size, int unbuffered)
{
        static char text[3 * 4096 + 72];
        static char other[48];
        unsigned long reach = unbuffered ? 3100 : (unsigned long)(3 * size + 70);
        long n;
        int byte;

        if (reach > sizeof(text) - 1)
                reach = sizeof(text) - 1;

        n = body_walk_text(text, reach);
        byte = body_walk(4) == 0 ? '\n' : 'a' + (int)body_walk(26);

        body_walk_text(other, 40);

        switch (body_walk(12))
        {
        case 0: trace_number("walk: fwrite", (long)fwrite(text, 1, (unsigned long)n, f)); break;
        case 1: trace_number("walk: fwrite items", (long)fwrite(text, 3, (unsigned long)n / 3, f)); break;
        case 2: trace_number("walk: fputc", (long)fputc(byte, f)); break;
        case 3: trace_number("walk: putc", (long)putc(byte, f)); break;
        case 4: trace_number("walk: fputs", (long)(fputs(text, f) >= 0)); break;
        case 5: trace_number("walk: printf text", (long)fprintf(f, "%s", text)); break;
        case 6: trace_number("walk: printf padded", (long)fprintf(f, "%*d|%s", (int)body_walk(reach), (int)n, other)); break;
        case 7: trace_number("walk: printf two lines", (long)fprintf(f, "%s\n%s", other, text)); break;
        case 8: trace_number("walk: printf left", (long)fprintf(f, "%-*s:%c", (int)body_walk(reach), other, byte)); break;
        case 9: trace_number("walk: printf precision", (long)fprintf(f, "%.*s%%%x%c", (int)body_walk((unsigned long)n + 1), text, (unsigned)n, byte)); break;
        case 10: trace_number("walk: fflush", (long)fflush(f)); break;
        default: trace_number("walk: printf nothing", (long)fprintf(f, "%s", "")); break;
        }
}

static void body_generated_writes(void)
{
        static const long sizes[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                                     63, 64, 65, 200, 1023, 1024, 1025, 4096};
        static const int modes[] = {_IOFBF, _IOLBF, _IONBF};
        static char buffer[4096];
        char path[256];
        unsigned long m;
        unsigned long i;

        for (m = 0; m < 3; m++)
                for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
                {
                        int unbuffered = modes[m] == _IONBF;
                        int steps = unbuffered ? 120 : 48;
                        FILE *f;

                        if (unbuffered && i > 1)
                                break;

                        snprintf(path, sizeof(path), WORK "/walk-%lu-%ld.txt", m, sizes[i]);
                        f = fopen(path, "w");
                        body_walk_state = 0x5eedUL + m * 131 + i;
                        trace_number("walk: size class", sizes[i]);
                        trace_number("walk: setvbuf", (long)setvbuf(f, (i & 1) ? buffer : 0, modes[m], (unsigned long)sizes[i]));

                        while (steps--)
                        {
                                body_walk_write(f, sizes[i], unbuffered);
                                trace_number("walk: ftell", (long)ftell(f));
                                trace_number("walk: ferror", (long)ferror(f));

                                if (unbuffered)
                                        trace_number("walk: size", body_file_size(path));

                                body_stream_invariants(f);
                        }

                        trace_number("walk: fclose", (long)fclose(f));
                        trace_number("walk: final size", body_file_size(path));
                }
}

/*
        The same writes on an update stream, turned round the way C allows: a
        positioning call between writing and reading and between reading and
        writing, with getc, fread and an ungetc of a byte the file does not
        hold in the reading half.
*/
static void body_generated_updates(void)
{
        static const long sizes[] = {1, 8, 17, 64, 4096};
        static const int modes[] = {_IOFBF, _IOLBF, _IONBF};
        static unsigned char got[512];
        char path[256];
        unsigned long m;
        unsigned long i;

        for (m = 0; m < 3; m++)
                for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
                {
                        FILE *f;
                        int phase;

                        snprintf(path, sizeof(path), WORK "/update-walk-%lu-%lu.txt", m, i);
                        f = fopen(path, "w+");
                        body_walk_state = 0xabcdUL + m * 977 + i;
                        trace_number("update-walk: setvbuf", (long)setvbuf(f, 0, modes[m], (unsigned long)sizes[i]));

                        for (phase = 0; phase < 10; phase++)
                        {
                                int step;

                                for (step = 0; step < 5; step++)
                                {
                                        body_walk_write(f, sizes[i], 0);
                                        trace_number("update-walk: ftell after write", (long)ftell(f));
                                        body_stream_invariants(f);
                                }

                                trace_number("update-walk: seek", (long)fseek(f, (long)body_walk((unsigned long)ftell(f) + 1), SEEK_SET));

                                for (step = 0; step < 5; step++)
                                {
                                        unsigned long want = body_walk(3 * (unsigned long)sizes[i] + 20);
                                        unsigned long have;
                                        unsigned long sum = 0;
                                        unsigned long j;
                                        int c;

                                        if (want > sizeof(got))
                                                want = sizeof(got);

                                        switch (body_walk(3))
                                        {
                                        case 0:
                                                have = fread(got, 1, want, f);
                                                for (j = 0; j < have; j++)
                                                        sum = (sum * 31 + got[j]) % 1000000007UL;
                                                trace_number("update-walk: fread", (long)have);
                                                trace_number("update-walk: bytes", (long)sum);
                                                break;
                                        case 1:
                                                trace_number("update-walk: fgetc", (long)fgetc(f));
                                                break;
                                        default:
                                                c = fgetc(f);
                                                trace_number("update-walk: fgetc for ungetc", (long)c);
                                                if (c != EOF)
                                                        trace_number("update-walk: ungetc", (long)ungetc(c ^ 1, f));
                                                break;
                                        }

                                        trace_number("update-walk: ftell after read", (long)ftell(f));
                                        trace_number("update-walk: feof", (long)feof(f));
                                        body_stream_invariants(f);
                                }

                                trace_number("update-walk: seek back", (long)fseek(f, 0, body_walk(2) ? SEEK_CUR : SEEK_END));
                        }

                        trace_number("update-walk: fclose", (long)fclose(f));
                        trace_number("update-walk: final size", body_file_size(path));
                }
}

/*
        Many streams open at once, closed out of order.

        fclose unlinks a stream from the list fflush(null) and exit walk, and
        the orders that matter are the ones where the stream closed is not
        the newest: every third from the oldest, the newest half newest
        first, and what is left from the middle out. A flush of everything
        after each pass is what would trip over a list the closes broke, and
        every file must still hold what was written to it.
*/
static void body_many_streams(void)
{
        static FILE *streams[240];
        char path[256];
        int count = (int)(sizeof(streams) / sizeof(streams[0]));
        int i;

        for (i = 0; i < count; i++)
        {
                snprintf(path, sizeof(path), WORK "/many-%03d.txt", i);
                streams[i] = fopen(path, "w");

                if (!streams[i])
                {
                        trace_number("many: open failed at", i);
                        return;
                }

                fprintf(streams[i], "stream %d\n", i);
        }

        for (i = 0; i < count; i += 3)
        {
                trace_number("many: close oldest first", (long)fclose(streams[i]));
                streams[i] = 0;
        }

        body_stream_invariants(streams[1]);
        trace_number("many: flush all", (long)fflush(0));

        for (i = count - 1; i >= 0; i--)
                if (streams[i])
                        fprintf(streams[i], "again %d\n", i);

        for (i = count - 1; i >= count / 2; i--)
                if (streams[i])
                {
                        trace_number("many: close newest first", (long)fclose(streams[i]));
                        streams[i] = 0;
                }

        body_stream_invariants(streams[1]);
        trace_number("many: flush all again", (long)fflush(0));

        for (i = 0; i < count / 2; i++)
        {
                int at = count / 4 + ((i & 1) ? -(i + 1) / 2 : i / 2);

                if (at >= 0 && at < count && streams[at])
                {
                        fprintf(streams[at], "last %d\n", at);
                        trace_number("many: close middle out", (long)fclose(streams[at]));
                        streams[at] = 0;
                }
        }

        for (i = 0; i < count; i++)
                if (streams[i])
                        trace_number("many: close the rest", (long)fclose(streams[i]));

        trace_number("many: flush nothing left", (long)fflush(0));
}

static void trace_body(void)
{
        body_make(WORK "/three.txt", "abc");
        body_make(WORK "/lines.txt", "alpha\nbeta\n\ngamma");
        body_make(WORK "/colons.txt", "a:bb:ccc:");
        body_make(WORK "/reader.txt", "xy");

        {
                FILE *f = fopen(WORK "/large.txt", "w");
                long i;

                for (i = 0; i < 12000; i++)
                        fputc((int)('A' + (i % 26)), f);

                trace_number("large: fclose", (long)fclose(f));
                trace_number("large: size", body_file_size(WORK "/large.txt"));
        }

        body_getc_to_the_end();
        body_fread_items();
        body_fread_large();
        body_ungetc_at_the_start();
        body_ungetc_at_the_end();
        body_seeking();
        body_failed_seek();
        body_descriptor_errors();
        body_rewind();
        body_update_stream();
        body_fgets();
        body_getline();
        body_buffering();
        body_append();
        body_odds_and_ends();
        body_refusals();
        body_write_failure();
        body_generated_writes();
        body_generated_updates();
        body_many_streams();
}

#elif defined(SHARED_stream_buffering_body)
/*
        Experimental C standard library

        which buffering policy is in force, seen from outside the process

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        A terminal makes a stream line buffered and a file makes it block
        buffered, and that decision is invisible from inside the program: both
        produce the same bytes in the end. It is visible from outside, in the
        middle -- after the newline and before the flush a line buffered
        stream has already written and a block buffered one has not -- so the
        program stops there for long enough to be looked at.

        The stream lane of test/run runs this with standard output on a file and
        expects nothing to have arrived at the pause, then runs it again under
        a pseudo terminal and expects the first line to have arrived. The same
        two runs are made against glibc, which is what says the expectation is
        the standard's and not this file's.

        body_pause is the driver's, because the way to sleep is the one thing
        here that has no name in common between a freestanding build and a
        hosted one.
*/
static void trace_body(void)
{
        fputs("one", stdout);
        fputs("two\n", stdout);
        body_pause();
        fputs("three", stdout);
        fflush(stdout);
        body_pause();
}

#elif defined(SHARED_stream_standard_body)
/*
        Experimental C standard library

        the three standard streams, a pipe, and a terminal

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        Compiled twice like stream_body.c, and run two ways by
        the stream lane of test/run: with a regular file on standard input and a
        regular file on standard output, and then with a pipe on both. The
        pipe run is the one that matters -- a pipe cannot seek, so ftell has
        to answer minus one rather than a number it made up, and ungetc has to
        work with nowhere to put the byte but its own pushback.

        The trace goes to descriptor three, which the runner opens, so that
        what the program writes to standard output and standard error stays
        payload and is compared on its own.
*/

static void trace_body(void)
{
        char *line = 0;
        unsigned long capacity = 0;
        long got;
        long lines = 0;

        trace_number("isatty 0", isatty(0));
        trace_number("isatty 1", isatty(1));
        trace_number("isatty 2", isatty(2));
        trace_number("fileno stdin", fileno(stdin));
        trace_number("fileno stdout", fileno(stdout));
        trace_number("fileno stderr", fileno(stderr));
        trace_number("ftell stdin before", (long)ftell(stdin));
        trace_number("feof stdin before", (long)feof(stdin));

        trace_number("ungetc onto stdin", (long)ungetc('>', stdin));
        trace_number("getc it back", (long)fgetc(stdin));

        while ((got = getline(&line, &capacity, stdin)) != -1)
        {
                lines++;
                trace_number("line length", got);
                fputs("out: ", stdout);
                fwrite(line, 1, (unsigned long)got, stdout);

                if (got > 0 && line[got - 1] != '\n')
                        fputc('\n', stdout);
        }

        trace_number("lines", lines);
        trace_number("feof stdin after", (long)feof(stdin));
        trace_number("ferror stdin after", (long)ferror(stdin));
        trace_number("getc past the end", (long)fgetc(stdin));
        trace_number("feof still", (long)feof(stdin));
        clearerr(stdin);
        trace_number("feof cleared", (long)feof(stdin));

        fputs("err: diagnostic\n", stderr);
        trace_number("stderr ferror", (long)ferror(stderr));

        trace_number("fflush all", (long)fflush(0));
        trace_number("ftell stdout", (long)ftell(stdout));
        trace_number("stdout ferror", (long)ferror(stdout));

        fputs("out: done\n", stdout);
        trace_number("fflush stdout", (long)fflush(stdout));
        trace_number("length of the last line seen", line ? body_length(line) : -2);

        /*
                stderr formats a whole line before it writes, and stdout's
                puts stages a line and its newline together. The bytes and
                their order are what is compared: a line made of several
                pieces, widths either side of the stage a formatted line is
                built in, text wider than the stage, an empty format, and
                puts lines of every length up to and past stdout's buffer.
        */
        {
                static char wide[5001];
                int i;

                for (i = 0; i < 5000; i++)
                        wide[i] = (char)('A' + i % 26);

                wide[5000] = 0;

                trace_number("stderr pieces", (long)fprintf(stderr, "%s: %s\n", "prog", "message"));

                for (i = 1015; i <= 1030; i++)
                        trace_number("stderr across the stage", (long)fprintf(stderr, "%*d|%s\n", i, i, "tail"));

                trace_number("stderr wide text", (long)fprintf(stderr, "[%s]\n", wide));
                trace_number("stderr wide pad", (long)fprintf(stderr, "%3000s|%-2100s|\n", "x", "y"));
                trace_number("stderr nothing", (long)fprintf(stderr, "%s", ""));
                trace_number("stderr putc", (long)fputc('!', stderr));
                trace_number("stderr fputs", (long)(fputs("\n", stderr) >= 0));

                for (i = 0; i < 90; i++)
                        trace_number("puts", (long)(puts(wide + (i * 4099) % 5001) >= 0));

                trace_number("fflush after puts", (long)fflush(stdout));
        }
}

#endif
#elif defined(SHARED_terminal_fixture)
#define GRID_STRIDE 256
#define GRID_HISTORY 64
#define GRID_LINES \
        (WINDOW_PIXELS + GRID_HISTORY * GRID_STRIDE * \
                             (positive)sizeof(struct window_cell))

static p8 page[GRID_LINES + GRID_HISTORY * 4];
static p8 out[TERMINAL_FIXTURE_OUTPUT];
static positive out_length;

static fn say_byte(unsigned int c)
{
        if (out_length < sizeof(out))
                out[out_length++] = (p8)c;
}

static fn tell(const char *text)
{
        while (*text)
                say_byte((p8)*text++);
}

static fn say_number(unsigned int value)
{
        if (value >= 10)
                say_number(value / 10);

        say_byte('0' + value % 10);
}

static fn say_hex(unsigned int value, unsigned int digits)
{
        while (digits--)
                say_byte("0123456789abcdef"[(value >> (digits * 4)) & 15]);
}

static unsigned int number(string_address text)
{
        unsigned int value = 0;

        while (*text >= '0' && *text <= '9')
                value = value * 10 + (unsigned int)(*text++ - '0');

        return value;
}

static bipolar next_byte(string_address text, positive *at)
{
        unsigned int c = text[*at];

        if (!c)
                return -1;

        (*at)++;

        if (c != '\\')
                return (bipolar)c;

        c = text[*at];

        if (!c)
                return '\\';

        (*at)++;

        switch (c)
        {
        case 'e': return 27;
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'b': return 8;
#ifdef TERMINAL_FIXTURE_BELL
        case 'a': return 7;
#endif
        case '0': return 0;
        case 'x':
        {
                unsigned int value = 0;

                for (int i = 0; i < 2; i++)
                {
                        unsigned int digit = text[*at];

                        if (digit >= '0' && digit <= '9')
                                value = value * 16 + digit - '0';
                        else if (digit >= 'a' && digit <= 'f')
                                value = value * 16 + digit - 'a' + 10;
                        else if (digit >= 'A' && digit <= 'F')
                                value = value * 16 + digit - 'A' + 10;
                        else
                                break;

                        (*at)++;
                }

                return (bipolar)value;
        }
        }

        return (bipolar)c;
}

static fn say_row(unsigned int row)
{
        struct window_cell *cells = row_cells(row);
        unsigned int length = *row_length(row);

        say_byte('[');

        for (unsigned int column = 0; column < length; column++)
        {
                unsigned int character = cells[column].character;

                // The second cell of a double-width character is the same one.
                if (cells[column].flags & WINDOW_CELL_WIDE_RIGHT)
                        continue;

                if (character >= ' ' && character < 127)
                        say_byte(character);
                else
                {
                        say_byte('<');
                        say_hex(character, character > 0xfffff
                                               ? 6
                                               : character > 0xffff ? 5 : 4);
                        say_byte('>');
                }
        }

        tell("]\n");
}

// The cell a "row,column" argument names.
static struct window_cell *fixture_cell(string_address text)
{
        positive at = 0;

        while (text[at] && text[at] != ',')
                at++;

        return row_cells(number(text)) + number(text + at + 1);
}

static fn say_attribute(string_address text)
{
        struct window_cell *cell = fixture_cell(text);

        say_number(cell->ink);
        say_byte(',');
        say_number(cell->paper);
        say_byte('\n');
}

/* The style and shape bits of a cell. Which of its colours a program set, and whether the defaults trade places, are the colours'
   records and say nothing of the style: they are left out. */
static fn say_flags(string_address text)
{
        say_number(fixture_cell(text)->flags & ~(WINDOW_CELL_INK_SET | WINDOW_CELL_PAPER_SET | WINDOW_CELL_SWAP));
        say_byte('\n');
}

static fn terminal_fixture_start(unsigned int columns, unsigned int rows)
{
        window = (struct window *)page;
        window->stride = GRID_STRIDE;
        window->history = GRID_HISTORY;
        window->lines = GRID_LINES;
        window->head = GRID_HISTORY + rows;
        window->columns = columns;
        window->rows = rows;
        window->max_columns = GRID_STRIDE;
        window->max_rows = GRID_HISTORY;

        //      Clamped as grid_take clamps: a grid wider than its lines are
        //      apart runs each row into the next, a state no window reaches.
        COLUMNS = columns < GRID_STRIDE ? columns : GRID_STRIDE;
        ROWS = rows;
        full_reset();
}
#elif defined(SHARED_bench_measure)
/*
        The timing scaffold shared by the small standalone benchmarks.

        A benchmark supplies only the work it means to measure.  Trial
        preparation, best-of selection, fixed-point reporting and the tiny
        insertion sort used by paired medians live here so their policy cannot
        drift between callers.
*/

#ifndef DAWNING_BENCH_MEASURE_C
#define DAWNING_BENCH_MEASURE_C

typedef fn (*bench_work)(void);

/* Optional state reset performed immediately before the clock starts. */
static bench_work bench_prepare;

static p64 bench_best(bench_work work, positive tries)
{
        p64 best = 0;

        for (positive which = 0; which < tries; which++)
        {
                p64 started;
                p64 elapsed;

                if (bench_prepare)
                        bench_prepare();

                started = get_cpu_time();
                work();
                elapsed = get_cpu_time() - started;

                if (!which || elapsed < best)
                        best = elapsed;
        }

        return best;
}

static fn bench_report(string_address name, bench_work work, positive tries,
                       positive units, string_address unit)
{
        p64 ticks = bench_best(work, tries);
        positive scaled = (positive)(ticks * 100 / units);
        p8 fraction[3];

        positive_into_padded(fraction, scaled % 100, 2, '0');
        fraction[2] = end;
        string_format(log, "  %s  %p.%s ticks/%s\n", name, scaled / 100,
                      fraction, unit);
}

/*
        One paired trial's two runs, in the order that trial asks for: an odd
        trial measures the assembly first and an even one the C, so neither
        side always inherits the warm cache or the far end of a drifting
        clock. The benchmark's own run(bool assembly, ...) does the timing;
        everything after the flag is the same for both sides and is written
        once here.
*/
#define BENCH_BOTH_ORDERS(trial, assembly, former, ...)                       \
        BENCH_ALTERNATE(trial, former, run(false, __VA_ARGS__), assembly,     \
                        run(true, __VA_ARGS__))

static fn order(positive address_to values, positive count)
{
        for (positive i = 1; i < count; i++)
        {
                positive value = values[i];
                positive at = i;

                while (at && values[at - 1] > value)
                {
                        values[at] = values[at - 1];
                        at--;
                }

                values[at] = value;
        }
}

#ifdef BENCH_PAIRED_INDEXED
typedef positive (*bench_indexed_work)(positive);

BENCH_ONCE(bench_indexed_once, (bench_indexed_work run), ROUNDS, round,
           sink += run(round & (SUBJECTS - 1)))

static positive bench_paired_median(bench_indexed_work one,
                                    bench_indexed_work two)
{
        positive samples[TRIES];

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 first;
                p64 second;

                if (trial & 1)
                {
                        second = bench_indexed_once(two);
                        first = bench_indexed_once(one);
                }
                else
                {
                        first = bench_indexed_once(one);
                        second = bench_indexed_once(two);
                }

                samples[trial] =
                    (positive)(second * 10000 / (first ? first : 1));
        }

        order(samples, TRIES);
        return samples[TRIES / 2];
}
#endif

#endif
#elif defined(SHARED_bench_reference)
/*
        Scalar references for the assembly string benchmarks.

        These stay out of line on purpose.  The public assembly is reached by
        a call too; inlining only this side would measure call elimination at
        short lengths instead of the byte loop the routines replaced.
*/

#ifndef DAWNING_BENCH_REFERENCE_C
#define DAWNING_BENCH_REFERENCE_C

#define BENCH_NOT_INLINED __attribute__((noinline, noclone))

BENCH_NOT_INLINED positive reference_length(string_address source)
{
        string_address step = source;

        while (string_get(step))
                step++;

        return step - source;
}

BENCH_NOT_INLINED b32 reference_compare(string_address source,
                                        string_address input)
{
        while (string_get(source) && string_get(input))
        {
                if string_not (source, address_to input)
                        break;

                source++;
                input++;
        }

        return string_get(source) - string_get(input);
}

BENCH_NOT_INLINED string_address reference_first_of(string_address source,
                                                     p8 character)
{
        while (string_get(source))
        {
                if string_is (source, character)
                        return source;

                source++;
        }

        return character ? null : source;
}

#undef BENCH_NOT_INLINED
#endif
#elif defined(SHARED_random_filter)
/*
        A seccomp filter that answers getrandom with -EAGAIN when its flags
        are first or second, and lets every other system call through. It tells the entropy policies of
        DHCP and SNTP apart without replacing their production helpers.
        -1 when this kernel or emulator cannot install a filter.
*/
typedef struct
{
        p16 code;
        p8 yes;
        p8 no;
        p32 value;
} random_filter_instruction;

typedef struct
{
        p16 length;
        random_filter_instruction address_to instructions;
} random_filter_program;

static bipolar random_filter(p32 first, p32 second)
{
        random_filter_instruction instructions[] = {
            {0x20, 0, 0, 0},
            {0x15, 0, 4, syscall(getrandom)},
            {0x20, 0, 0, 32},
            {0x15, 1, 0, first},
            {0x15, 0, 1, second},
            {0x06, 0, 0, 0x0005000b},
            {0x06, 0, 0, 0x7fff0000},
        };
        random_filter_program program = {
            array_count(instructions), instructions};

        if (system_call_5(syscall(prctl), 38, 1, 0, 0, 0) < 0)
                return -1;
        return system_call_3(syscall(seccomp), 1, 0,
                             (positive)address_of program);
}
#elif defined(SHARED_montgomery_reference)
/*
        The generic Montgomery multiply and square crypto.c ran in C for the
        group orders and RSA, kept here as it was written -- the schoolbook
        product or the doubled triangle, word-by-word reduction, a masked
        final subtraction and the volatile wipes -- so CHECK_net can hold
        the assembly to it and BENCH_montgomery can time the two against
        each other. Include it after crypto.c.
*/

#ifndef DAWNING_MONTGOMERY_REFERENCE_C
#define DAWNING_MONTGOMERY_REFERENCE_C

#define MONTGOMERY_REFERENCE_LIMBS 64

typedef unsigned __int128 montgomery_reference_wide;

static fn montgomery_reference_forget(address_any secret, positive length)
{
        volatile p8 address_to at = secret;

        while (length)
        {
                *at++ = 0;
                length--;
        }
}

static p64 montgomery_reference_subtract(p64 address_to d,
                                         const p64 address_to a,
                                         const p64 address_to b, positive n)
{
        montgomery_reference_wide borrow = 0;

        for (positive i = 0; i < n; i++)
        {
                montgomery_reference_wide value =
                    (montgomery_reference_wide)a[i] - b[i] - borrow;

                d[i] = (p64)value;
                borrow = (value >> 64) & 1;
        }

        return (p64)borrow;
}

static fn montgomery_reference_select(p64 address_to d, const p64 address_to a,
                                      const p64 address_to b, positive n,
                                      p64 choose_b)
{
        p64 mask = 0 - choose_b;

        for (positive i = 0; i < n; i++)
                d[i] = (a[i] & ~mask) | (b[i] & mask);

        montgomery_reference_forget(address_of mask, sizeof mask);
}

static fn montgomery_reference_reduce(p64 address_to d, p64 address_to t,
                                      const p64 address_to m, p64 inverse,
                                      positive n)
{
        p64 reduced[MONTGOMERY_REFERENCE_LIMBS];
        p64 top = 0;
        p64 borrow;

        for (positive i = 0; i < n; i++)
        {
                p64 q = t[i] * inverse;
                montgomery_reference_wide carry = 0;

                for (positive j = 0; j < n; j++)
                {
                        carry += (montgomery_reference_wide)t[i + j] +
                                 (montgomery_reference_wide)q * m[j];
                        t[i + j] = (p64)carry;
                        carry >>= 64;
                }
                carry += (montgomery_reference_wide)t[i + n] + top;
                t[i + n] = (p64)carry;
                top = (p64)(carry >> 64);
        }

        borrow = montgomery_reference_subtract(reduced, t + n, m, n);
        montgomery_reference_select(d, t + n, reduced, n, top | (borrow ^ 1));
        montgomery_reference_forget(reduced, n * 8);
        montgomery_reference_forget(address_of top, sizeof top);
        montgomery_reference_forget(address_of borrow, sizeof borrow);
}

static fn montgomery_reference_multiply(p64 address_to d,
                                        const p64 address_to a,
                                        const p64 address_to b,
                                        const p64 address_to m, p64 inverse,
                                        positive n)
{
        p64 t[MONTGOMERY_REFERENCE_LIMBS * 2];

        if (n > MONTGOMERY_REFERENCE_LIMBS)
                return;

        memory_fill(t, 0, n * 2 * 8);
        for (positive i = 0; i < n; i++)
        {
                montgomery_reference_wide carry = 0;

                for (positive j = 0; j < n; j++)
                {
                        carry += (montgomery_reference_wide)t[i + j] +
                                 (montgomery_reference_wide)a[i] * b[j];
                        t[i + j] = (p64)carry;
                        carry >>= 64;
                }
                t[i + n] = (p64)carry;
        }

        montgomery_reference_reduce(d, t, m, inverse, n);
        montgomery_reference_forget(t, n * 2 * 8);
}

static fn montgomery_reference_square(p64 address_to d, const p64 address_to a,
                                      const p64 address_to m, p64 inverse,
                                      positive n)
{
        p64 t[MONTGOMERY_REFERENCE_LIMBS * 2];
        montgomery_reference_wide carry = 0;

        if (n > MONTGOMERY_REFERENCE_LIMBS)
                return;

        memory_fill(t, 0, n * 2 * 8);
        for (positive i = 0; i < n; i++)
        {
                carry = 0;
                for (positive j = i + 1; j < n; j++)
                {
                        carry += (montgomery_reference_wide)t[i + j] +
                                 (montgomery_reference_wide)a[i] * a[j];
                        t[i + j] = (p64)carry;
                        carry >>= 64;
                }
                t[i + n] = (p64)carry;
        }

        carry = 0;
        for (positive i = 0; i < n * 2; i++)
        {
                p64 limb = t[i];

                t[i] = (limb << 1) | (p64)carry;
                carry = limb >> 63;
        }

        carry = 0;
        for (positive i = 0; i < n; i++)
        {
                montgomery_reference_wide square =
                    (montgomery_reference_wide)a[i] * a[i];

                carry += (montgomery_reference_wide)t[i * 2] + (p64)square;
                t[i * 2] = (p64)carry;
                carry >>= 64;
                carry += (montgomery_reference_wide)t[i * 2 + 1] +
                         (p64)(square >> 64);
                t[i * 2 + 1] = (p64)carry;
                carry >>= 64;
        }

        montgomery_reference_reduce(d, t, m, inverse, n);
        montgomery_reference_forget(t, n * 2 * 8);
        montgomery_reference_forget(address_of carry, sizeof carry);
}

#endif
#elif defined(SHARED_unicode_width_reference)
/*
        The two display-width functions unicode_width answers for, as their
        files had them: src/canvas/term.c's character_width, Unicode 16 range
        tables with the ideograph shortcut in front of them, and
        src/sh/text.c's wc_width, glibc 2.44's wcwidth with nought for what
        iswprint refuses. The terminal has since taken glibc's width where
        glibc prints what Unicode 16 does not, so its zero ranges no longer
        hold U+0600-0605, U+06DD, U+070F, U+0890-0891, U+08E2, U+FFF9-FFFB,
        U+110BD, U+110CD or U+13430-1343F, and its wide ranges take in
        U+3248-324F, U+16FF2-16FF6, U+187F8-187FF, U+18D09-18D1E,
        U+18D80-18DF2, U+1F6D8, U+1FA8A, U+1FA8E, U+1FAC8, U+1FACD, U+1FAEA
        and U+1FAEF. Otherwise only the names are changed, and the tables alone,
        without the shortcut, are unicode_width_reference_tables -- what the
        term lane's widths verb has always held character_width to.

        unicode_width_reference_table lays unicode_width_tab out from them:
        for every 128 code points a byte naming a block, padded to eight; a
        block of eight 16-bit leaf offsets; a leaf of eight bytes holding
        sixteen nibbles, the terminal's width low and wc's high. Blocks and
        leaves are kept once each, in the order they are first met, and the
        code points U+E0000-E01FF follow U+3FFFF.
*/
#ifndef DAWNING_UNICODE_WIDTH_REFERENCE_C
#define DAWNING_UNICODE_WIDTH_REFERENCE_C

/*
        How many columns a character takes.

        Programs count columns with wcwidth, and a terminal that disagrees puts
        everything after an ideograph, an emoji or a combining accent a column
        away from where the program meant it. The ranges are Unicode 16's: East
        Asian Wide and Fullwidth take two, marks and format characters take
        none, and everything below U+0300 takes one.
*/
struct unicode_width_reference_range
{
        unsigned int first, last;
};

static const struct unicode_width_reference_range unicode_width_reference_zero[] = {
    {0x00300, 0x0036f}, {0x00483, 0x00489}, {0x00591, 0x005bd}, {0x005bf, 0x005bf},
    {0x005c1, 0x005c2}, {0x005c4, 0x005c5}, {0x005c7, 0x005c7}, {0x00610, 0x0061a},
    {0x0061c, 0x0061c}, {0x0064b, 0x0065f}, {0x00670, 0x00670}, {0x006d6, 0x006dc},
    {0x006df, 0x006e4}, {0x006e7, 0x006e8}, {0x006ea, 0x006ed}, {0x00711, 0x00711},
    {0x00730, 0x0074a}, {0x007a6, 0x007b0}, {0x007eb, 0x007f3}, {0x007fd, 0x007fd},
    {0x00816, 0x00819}, {0x0081b, 0x00823}, {0x00825, 0x00827}, {0x00829, 0x0082d},
    {0x00859, 0x0085b}, {0x00897, 0x0089f}, {0x008ca, 0x008e1}, {0x008e3, 0x00902},
    {0x0093a, 0x0093a}, {0x0093c, 0x0093c}, {0x00941, 0x00948}, {0x0094d, 0x0094d},
    {0x00951, 0x00957}, {0x00962, 0x00963}, {0x00981, 0x00981}, {0x009bc, 0x009bc},
    {0x009c1, 0x009c4}, {0x009cd, 0x009cd}, {0x009e2, 0x009e3}, {0x009fe, 0x009fe},
    {0x00a01, 0x00a02}, {0x00a3c, 0x00a3c}, {0x00a41, 0x00a42}, {0x00a47, 0x00a48},
    {0x00a4b, 0x00a4d}, {0x00a51, 0x00a51}, {0x00a70, 0x00a71}, {0x00a75, 0x00a75},
    {0x00a81, 0x00a82}, {0x00abc, 0x00abc}, {0x00ac1, 0x00ac5}, {0x00ac7, 0x00ac8},
    {0x00acd, 0x00acd}, {0x00ae2, 0x00ae3}, {0x00afa, 0x00aff}, {0x00b01, 0x00b01},
    {0x00b3c, 0x00b3c}, {0x00b3f, 0x00b3f}, {0x00b41, 0x00b44}, {0x00b4d, 0x00b4d},
    {0x00b55, 0x00b56}, {0x00b62, 0x00b63}, {0x00b82, 0x00b82}, {0x00bc0, 0x00bc0},
    {0x00bcd, 0x00bcd}, {0x00c00, 0x00c00}, {0x00c04, 0x00c04}, {0x00c3c, 0x00c3c},
    {0x00c3e, 0x00c40}, {0x00c46, 0x00c48}, {0x00c4a, 0x00c4d}, {0x00c55, 0x00c56},
    {0x00c62, 0x00c63}, {0x00c81, 0x00c81}, {0x00cbc, 0x00cbc}, {0x00cbf, 0x00cbf},
    {0x00cc6, 0x00cc6}, {0x00ccc, 0x00ccd}, {0x00ce2, 0x00ce3}, {0x00d00, 0x00d01},
    {0x00d3b, 0x00d3c}, {0x00d41, 0x00d44}, {0x00d4d, 0x00d4d}, {0x00d62, 0x00d63},
    {0x00d81, 0x00d81}, {0x00dca, 0x00dca}, {0x00dd2, 0x00dd4}, {0x00dd6, 0x00dd6},
    {0x00e31, 0x00e31}, {0x00e34, 0x00e3a}, {0x00e47, 0x00e4e}, {0x00eb1, 0x00eb1},
    {0x00eb4, 0x00ebc}, {0x00ec8, 0x00ece}, {0x00f18, 0x00f19}, {0x00f35, 0x00f35},
    {0x00f37, 0x00f37}, {0x00f39, 0x00f39}, {0x00f71, 0x00f7e}, {0x00f80, 0x00f84},
    {0x00f86, 0x00f87}, {0x00f8d, 0x00f97}, {0x00f99, 0x00fbc}, {0x00fc6, 0x00fc6},
    {0x0102d, 0x01030}, {0x01032, 0x01037}, {0x01039, 0x0103a}, {0x0103d, 0x0103e},
    {0x01058, 0x01059}, {0x0105e, 0x01060}, {0x01071, 0x01074}, {0x01082, 0x01082},
    {0x01085, 0x01086}, {0x0108d, 0x0108d}, {0x0109d, 0x0109d}, {0x01160, 0x011ff},
    {0x0135d, 0x0135f}, {0x01712, 0x01714}, {0x01732, 0x01733}, {0x01752, 0x01753},
    {0x01772, 0x01773}, {0x017b4, 0x017b5}, {0x017b7, 0x017bd}, {0x017c6, 0x017c6},
    {0x017c9, 0x017d3}, {0x017dd, 0x017dd}, {0x0180b, 0x0180f}, {0x01885, 0x01886},
    {0x018a9, 0x018a9}, {0x01920, 0x01922}, {0x01927, 0x01928}, {0x01932, 0x01932},
    {0x01939, 0x0193b}, {0x01a17, 0x01a18}, {0x01a1b, 0x01a1b}, {0x01a56, 0x01a56},
    {0x01a58, 0x01a5e}, {0x01a60, 0x01a60}, {0x01a62, 0x01a62}, {0x01a65, 0x01a6c},
    {0x01a73, 0x01a7c}, {0x01a7f, 0x01a7f}, {0x01ab0, 0x01ace}, {0x01b00, 0x01b03},
    {0x01b34, 0x01b34}, {0x01b36, 0x01b3a}, {0x01b3c, 0x01b3c}, {0x01b42, 0x01b42},
    {0x01b6b, 0x01b73}, {0x01b80, 0x01b81}, {0x01ba2, 0x01ba5}, {0x01ba8, 0x01ba9},
    {0x01bab, 0x01bad}, {0x01be6, 0x01be6}, {0x01be8, 0x01be9}, {0x01bed, 0x01bed},
    {0x01bef, 0x01bf1}, {0x01c2c, 0x01c33}, {0x01c36, 0x01c37}, {0x01cd0, 0x01cd2},
    {0x01cd4, 0x01ce0}, {0x01ce2, 0x01ce8}, {0x01ced, 0x01ced}, {0x01cf4, 0x01cf4},
    {0x01cf8, 0x01cf9}, {0x01dc0, 0x01dff}, {0x0200b, 0x0200f}, {0x0202a, 0x0202e},
    {0x02060, 0x02064}, {0x02066, 0x0206f}, {0x020d0, 0x020f0}, {0x02cef, 0x02cf1},
    {0x02d7f, 0x02d7f}, {0x02de0, 0x02dff}, {0x0302a, 0x0302d}, {0x03099, 0x0309a},
    {0x0a66f, 0x0a672}, {0x0a674, 0x0a67d}, {0x0a69e, 0x0a69f}, {0x0a6f0, 0x0a6f1},
    {0x0a802, 0x0a802}, {0x0a806, 0x0a806}, {0x0a80b, 0x0a80b}, {0x0a825, 0x0a826},
    {0x0a82c, 0x0a82c}, {0x0a8c4, 0x0a8c5}, {0x0a8e0, 0x0a8f1}, {0x0a8ff, 0x0a8ff},
    {0x0a926, 0x0a92d}, {0x0a947, 0x0a951}, {0x0a980, 0x0a982}, {0x0a9b3, 0x0a9b3},
    {0x0a9b6, 0x0a9b9}, {0x0a9bc, 0x0a9bd}, {0x0a9e5, 0x0a9e5}, {0x0aa29, 0x0aa2e},
    {0x0aa31, 0x0aa32}, {0x0aa35, 0x0aa36}, {0x0aa43, 0x0aa43}, {0x0aa4c, 0x0aa4c},
    {0x0aa7c, 0x0aa7c}, {0x0aab0, 0x0aab0}, {0x0aab2, 0x0aab4}, {0x0aab7, 0x0aab8},
    {0x0aabe, 0x0aabf}, {0x0aac1, 0x0aac1}, {0x0aaec, 0x0aaed}, {0x0aaf6, 0x0aaf6},
    {0x0abe5, 0x0abe5}, {0x0abe8, 0x0abe8}, {0x0abed, 0x0abed}, {0x0fb1e, 0x0fb1e},
    {0x0fe00, 0x0fe0f}, {0x0fe20, 0x0fe2f}, {0x0feff, 0x0feff}, {0x101fd, 0x101fd},
    {0x102e0, 0x102e0}, {0x10376, 0x1037a}, {0x10a01, 0x10a03}, {0x10a05, 0x10a06},
    {0x10a0c, 0x10a0f}, {0x10a38, 0x10a3a}, {0x10a3f, 0x10a3f}, {0x10ae5, 0x10ae6},
    {0x10d24, 0x10d27}, {0x10d69, 0x10d6d}, {0x10eab, 0x10eac}, {0x10efc, 0x10eff},
    {0x10f46, 0x10f50}, {0x10f82, 0x10f85}, {0x11001, 0x11001}, {0x11038, 0x11046},
    {0x11070, 0x11070}, {0x11073, 0x11074}, {0x1107f, 0x11081}, {0x110b3, 0x110b6},
    {0x110b9, 0x110ba}, {0x110c2, 0x110c2}, {0x11100, 0x11102}, {0x11127, 0x1112b},
    {0x1112d, 0x11134}, {0x11173, 0x11173}, {0x11180, 0x11181}, {0x111b6, 0x111be},
    {0x111c9, 0x111cc}, {0x111cf, 0x111cf}, {0x1122f, 0x11231}, {0x11234, 0x11234},
    {0x11236, 0x11237}, {0x1123e, 0x1123e}, {0x11241, 0x11241}, {0x112df, 0x112df},
    {0x112e3, 0x112ea}, {0x11300, 0x11301}, {0x1133b, 0x1133c}, {0x11340, 0x11340},
    {0x11366, 0x1136c}, {0x11370, 0x11374}, {0x113bb, 0x113c0}, {0x113ce, 0x113ce},
    {0x113d0, 0x113d0}, {0x113d2, 0x113d2}, {0x113e1, 0x113e2}, {0x11438, 0x1143f},
    {0x11442, 0x11444}, {0x11446, 0x11446}, {0x1145e, 0x1145e}, {0x114b3, 0x114b8},
    {0x114ba, 0x114ba}, {0x114bf, 0x114c0}, {0x114c2, 0x114c3}, {0x115b2, 0x115b5},
    {0x115bc, 0x115bd}, {0x115bf, 0x115c0}, {0x115dc, 0x115dd}, {0x11633, 0x1163a},
    {0x1163d, 0x1163d}, {0x1163f, 0x11640}, {0x116ab, 0x116ab}, {0x116ad, 0x116ad},
    {0x116b0, 0x116b5}, {0x116b7, 0x116b7}, {0x1171d, 0x1171d}, {0x1171f, 0x1171f},
    {0x11722, 0x11725}, {0x11727, 0x1172b}, {0x1182f, 0x11837}, {0x11839, 0x1183a},
    {0x1193b, 0x1193c}, {0x1193e, 0x1193e}, {0x11943, 0x11943}, {0x119d4, 0x119d7},
    {0x119da, 0x119db}, {0x119e0, 0x119e0}, {0x11a01, 0x11a0a}, {0x11a33, 0x11a38},
    {0x11a3b, 0x11a3e}, {0x11a47, 0x11a47}, {0x11a51, 0x11a56}, {0x11a59, 0x11a5b},
    {0x11a8a, 0x11a96}, {0x11a98, 0x11a99}, {0x11c30, 0x11c36}, {0x11c38, 0x11c3d},
    {0x11c3f, 0x11c3f}, {0x11c92, 0x11ca7}, {0x11caa, 0x11cb0}, {0x11cb2, 0x11cb3},
    {0x11cb5, 0x11cb6}, {0x11d31, 0x11d36}, {0x11d3a, 0x11d3a}, {0x11d3c, 0x11d3d},
    {0x11d3f, 0x11d45}, {0x11d47, 0x11d47}, {0x11d90, 0x11d91}, {0x11d95, 0x11d95},
    {0x11d97, 0x11d97}, {0x11ef3, 0x11ef4}, {0x11f00, 0x11f01}, {0x11f36, 0x11f3a},
    {0x11f40, 0x11f40}, {0x11f42, 0x11f42}, {0x11f5a, 0x11f5a}, {0x13440, 0x13440},
    {0x13447, 0x13455}, {0x1611e, 0x16129}, {0x1612d, 0x1612f}, {0x16af0, 0x16af4},
    {0x16b30, 0x16b36}, {0x16f4f, 0x16f4f}, {0x16f8f, 0x16f92}, {0x16fe4, 0x16fe4},
    {0x1bc9d, 0x1bc9e}, {0x1bca0, 0x1bca3}, {0x1cf00, 0x1cf2d}, {0x1cf30, 0x1cf46},
    {0x1d167, 0x1d169}, {0x1d173, 0x1d182}, {0x1d185, 0x1d18b}, {0x1d1aa, 0x1d1ad},
    {0x1d242, 0x1d244}, {0x1da00, 0x1da36}, {0x1da3b, 0x1da6c}, {0x1da75, 0x1da75},
    {0x1da84, 0x1da84}, {0x1da9b, 0x1da9f}, {0x1daa1, 0x1daaf}, {0x1e000, 0x1e006},
    {0x1e008, 0x1e018}, {0x1e01b, 0x1e021}, {0x1e023, 0x1e024}, {0x1e026, 0x1e02a},
    {0x1e08f, 0x1e08f}, {0x1e130, 0x1e136}, {0x1e2ae, 0x1e2ae}, {0x1e2ec, 0x1e2ef},
    {0x1e4ec, 0x1e4ef}, {0x1e5ee, 0x1e5ef}, {0x1e8d0, 0x1e8d6}, {0x1e944, 0x1e94a},
    {0xe0001, 0xe0001}, {0xe0020, 0xe007f}, {0xe0100, 0xe01ef},
};
static const struct unicode_width_reference_range unicode_width_reference_wide[] = {
    {0x01100, 0x0115f}, {0x0231a, 0x0231b}, {0x02329, 0x0232a}, {0x023e9, 0x023ec},
    {0x023f0, 0x023f0}, {0x023f3, 0x023f3}, {0x025fd, 0x025fe}, {0x02614, 0x02615},
    {0x02630, 0x02637}, {0x02648, 0x02653}, {0x0267f, 0x0267f}, {0x0268a, 0x0268f},
    {0x02693, 0x02693}, {0x026a1, 0x026a1}, {0x026aa, 0x026ab}, {0x026bd, 0x026be},
    {0x026c4, 0x026c5}, {0x026ce, 0x026ce}, {0x026d4, 0x026d4}, {0x026ea, 0x026ea},
    {0x026f2, 0x026f3}, {0x026f5, 0x026f5}, {0x026fa, 0x026fa}, {0x026fd, 0x026fd},
    {0x02705, 0x02705}, {0x0270a, 0x0270b}, {0x02728, 0x02728}, {0x0274c, 0x0274c},
    {0x0274e, 0x0274e}, {0x02753, 0x02755}, {0x02757, 0x02757}, {0x02795, 0x02797},
    {0x027b0, 0x027b0}, {0x027bf, 0x027bf}, {0x02b1b, 0x02b1c}, {0x02b50, 0x02b50},
    {0x02b55, 0x02b55}, {0x02e80, 0x02e99}, {0x02e9b, 0x02ef3}, {0x02f00, 0x02fd5},
    {0x02ff0, 0x03029}, {0x0302e, 0x0303e}, {0x03041, 0x03096}, {0x0309b, 0x030ff},
    {0x03105, 0x0312f}, {0x03131, 0x0318e}, {0x03190, 0x031e5}, {0x031ef, 0x0321e},
    {0x03220, 0x0a48c}, {0x0a490, 0x0a4c6}, {0x0a960, 0x0a97c}, {0x0ac00, 0x0d7a3},
    {0x0f900, 0x0faff}, {0x0fe10, 0x0fe19}, {0x0fe30, 0x0fe52}, {0x0fe54, 0x0fe66},
    {0x0fe68, 0x0fe6b}, {0x0ff01, 0x0ff60}, {0x0ffe0, 0x0ffe6}, {0x16fe0, 0x16fe3},
    {0x16ff0, 0x16ff6}, {0x17000, 0x18cd5}, {0x18cff, 0x18d1e}, {0x18d80, 0x18df2},
    {0x1aff0, 0x1aff3}, {0x1aff5, 0x1affb}, {0x1affd, 0x1affe}, {0x1b000, 0x1b122},
    {0x1b132, 0x1b132}, {0x1b150, 0x1b152}, {0x1b155, 0x1b155}, {0x1b164, 0x1b167},
    {0x1b170, 0x1b2fb}, {0x1d300, 0x1d356}, {0x1d360, 0x1d376}, {0x1f004, 0x1f004},
    {0x1f0cf, 0x1f0cf}, {0x1f18e, 0x1f18e}, {0x1f191, 0x1f19a}, {0x1f200, 0x1f202},
    {0x1f210, 0x1f23b}, {0x1f240, 0x1f248}, {0x1f250, 0x1f251}, {0x1f260, 0x1f265},
    {0x1f300, 0x1f320}, {0x1f32d, 0x1f335}, {0x1f337, 0x1f37c}, {0x1f37e, 0x1f393},
    {0x1f3a0, 0x1f3ca}, {0x1f3cf, 0x1f3d3}, {0x1f3e0, 0x1f3f0}, {0x1f3f4, 0x1f3f4},
    {0x1f3f8, 0x1f43e}, {0x1f440, 0x1f440}, {0x1f442, 0x1f4fc}, {0x1f4ff, 0x1f53d},
    {0x1f54b, 0x1f54e}, {0x1f550, 0x1f567}, {0x1f57a, 0x1f57a}, {0x1f595, 0x1f596},
    {0x1f5a4, 0x1f5a4}, {0x1f5fb, 0x1f64f}, {0x1f680, 0x1f6c5}, {0x1f6cc, 0x1f6cc},
    {0x1f6d0, 0x1f6d2}, {0x1f6d5, 0x1f6d8}, {0x1f6dc, 0x1f6df}, {0x1f6eb, 0x1f6ec},
    {0x1f6f4, 0x1f6fc}, {0x1f7e0, 0x1f7eb}, {0x1f7f0, 0x1f7f0}, {0x1f90c, 0x1f93a},
    {0x1f93c, 0x1f945}, {0x1f947, 0x1f9ff}, {0x1fa70, 0x1fa7c}, {0x1fa80, 0x1fa8a},
    {0x1fa8e, 0x1fac6}, {0x1fac8, 0x1fac8}, {0x1facd, 0x1fadc}, {0x1fadf, 0x1faea},
    {0x1faef, 0x1faf8}, {0x20000, 0x2fffd}, {0x30000, 0x3fffd},
};

static PURE b32 unicode_width_reference_in(const struct unicode_width_reference_range address_to ranges,
                             positive count, unsigned int c)
{
        positive low = 0, high = count;

        while (low < high)
        {
                positive middle = (low + high) / 2;

                if (ranges[middle].last < c)
                        low = middle + 1;
                else
                        high = middle;
        }

        return low < count && ranges[low].first <= c;
}

static PURE unsigned int unicode_width_reference_terminal(unsigned int c)
{
        if (c < 0x300)
                return 1;

        /*
                The CJK ideographs and the Hangul syllables are most of the
                wide characters a terminal is sent, and neither block holds a
                mark, so they are answered before the two searches -- behind
                one compare, so box drawing and every script below them pay
                no more than that. The term lane checks every character
                against the tables alone.
        */
        if (c >= 0x3250 && (c <= 0xa48c || c - 0xac00u <= 0xd7a3 - 0xac00))
                return 2;

        if (unicode_width_reference_in(unicode_width_reference_zero, array_count(unicode_width_reference_zero), c))
                return 0;

        return unicode_width_reference_in(unicode_width_reference_wide, array_count(unicode_width_reference_wide), c)
                   ? 2
                   : 1;
}

static PURE unsigned int unicode_width_reference_tables(unsigned int c)
{
        return c < 0x300 ? 1
               : unicode_width_reference_in(unicode_width_reference_zero,
                                            array_count(unicode_width_reference_zero), c) ? 0
               : unicode_width_reference_in(unicode_width_reference_wide,
                                            array_count(unicode_width_reference_wide), c) ? 2
                                                                                          : 1;
}

/*
        The display width of every code point past ASCII that is not one
        column wide, as wc -L measures it in a UTF-8 locale: glibc's wcwidth
        for a printable character and nought for anything iswprint refuses,
        unassigned code points included. Generated from glibc 2.44 on the
        reference machine; each row packs first << 32 | last << 2 | width,
        sorted, so a binary search on the first code point finds a row.
*/
#define UNICODE_WIDTH_REFERENCE_ROWS 1044
static const p64 unicode_width_reference_rows[UNICODE_WIDTH_REFERENCE_ROWS] = {
    0x800000027c, 0x30000000dbc, 0x37800000de4, 0x38000000e0c, 0x38b00000e2c,
    0x38d00000e34, 0x3a200000e88, 0x48300001224, 0x530000014c0, 0x55700001560,
    0x58b00001630, 0x590000016f4, 0x5bf000016fc, 0x5c100001708, 0x5c400001714,
    0x5c70000173c, 0x5eb000017b8, 0x5f5000017fc, 0x61000001868, 0x61c00001870,
    0x64b0000197c, 0x670000019c0, 0x6d600001b70, 0x6df00001b90, 0x6e700001ba0,
    0x6ea00001bb4, 0x70e00001c38, 0x71100001c44, 0x73000001d30, 0x7a600001ec0,
    0x7b200001efc, 0x7eb00001fcc, 0x7fb00001ff4, 0x81600002064, 0x81b0000208c,
    0x8250000209c, 0x829000020bc, 0x83f000020fc, 0x85900002174, 0x85f0000217c,
    0x86b000021bc, 0x8920000227c, 0x8ca00002384, 0x8e300002408, 0x93a000024e8,
    0x93c000024f0, 0x94100002520, 0x94d00002534, 0x9510000255c, 0x9620000258c,
    0x98100002604, 0x98400002610, 0x98d00002638, 0x99100002648, 0x9a9000026a4,
    0x9b1000026c4, 0x9b3000026d4, 0x9ba000026f0, 0x9c100002718, 0x9c900002728,
    0x9cd00002734, 0x9cf00002758, 0x9d80000276c, 0x9de00002778, 0x9e200002794,
    0x9fe00002808, 0xa0400002810, 0xa0b00002838, 0xa1100002848, 0xa29000028a4,
    0xa31000028c4, 0xa34000028d0, 0xa37000028dc, 0xa3a000028f4, 0xa4100002960,
    0xa5d00002974, 0xa5f00002994, 0xa70000029c4, 0xa75000029d4, 0xa7700002a08,
    0xa8400002a10, 0xa8e00002a38, 0xa9200002a48, 0xaa900002aa4, 0xab100002ac4,
    0xab400002ad0, 0xaba00002af0, 0xac100002b20, 0xaca00002b28, 0xacd00002b3c,
    0xad100002b7c, 0xae200002b94, 0xaf200002be0, 0xafa00002c04, 0xb0400002c10,
    0xb0d00002c38, 0xb1100002c48, 0xb2900002ca4, 0xb3100002cc4, 0xb3400002cd0,
    0xb3a00002cf0, 0xb3f00002cfc, 0xb4100002d18, 0xb4900002d28, 0xb4d00002d58,
    0xb5800002d6c, 0xb5e00002d78, 0xb6200002d94, 0xb7800002e08, 0xb8400002e10,
    0xb8b00002e34, 0xb9100002e44, 0xb9600002e60, 0xb9b00002e6c, 0xb9d00002e74,
    0xba000002e88, 0xba500002e9c, 0xbab00002eb4, 0xbba00002ef4, 0xbc000002f00,
    0xbc300002f14, 0xbc900002f24, 0xbcd00002f3c, 0xbd100002f58, 0xbd800002f94,
    0xbfb00003000, 0xc0400003010, 0xc0d00003034, 0xc1100003044, 0xc29000030a4,
    0xc3a000030f0, 0xc3e00003100, 0xc450000315c, 0xc5b0000316c, 0xc5e0000317c,
    0xc6200003194, 0xc70000031d8, 0xc8100003204, 0xc8d00003234, 0xc9100003244,
    0xca9000032a4, 0xcb4000032d0, 0xcba000032f0, 0xcbf000032fc, 0xcc500003318,
    0xcc900003324, 0xccc00003350, 0xcd70000336c, 0xcdf0000337c, 0xce200003394,
    0xcf0000033c0, 0xcf400003404, 0xd0d00003434, 0xd1100003444, 0xd3b000034f0,
    0xd4100003514, 0xd4900003524, 0xd4d00003534, 0xd500000354c, 0xd6200003594,
    0xd8000003604, 0xd8400003610, 0xd9700003664, 0xdb2000036c8, 0xdbc000036f0,
    0xdbe000036fc, 0xdc700003738, 0xdd20000375c, 0xde000003794, 0xdf0000037c4,
    0xdf500003800, 0xe31000038c4, 0xe34000038f8, 0xe4700003938, 0xe5c00003a00,
    0xe8300003a0c, 0xe8500003a14, 0xe8b00003a2c, 0xea400003a90, 0xea600003a98,
    0xeb100003ac4, 0xeb400003af0, 0xebe00003afc, 0xec500003b14, 0xec700003b3c,
    0xeda00003b6c, 0xee000003bfc, 0xf1800003c64, 0xf3500003cd4, 0xf3700003cdc,
    0xf3900003ce4, 0xf4800003d20, 0xf6d00003df8, 0xf8000003e10, 0xf8600003e1c,
    0xf8d00003ef4, 0xfc600003f18, 0xfcd00003f34, 0xfdb00003ffc, 0x102d000040c0,
    0x1032000040dc, 0x1039000040e8, 0x103d000040f8, 0x105800004164, 0x105e00004180,
    0x1071000041d0, 0x108200004208, 0x108500004218, 0x108d00004234, 0x109d00004274,
    0x10c600004318, 0x10c800004330, 0x10ce0000433c, 0x11000000457e, 0x1160000047fc,
    0x124900004924, 0x124e0000493c, 0x12570000495c, 0x125900004964, 0x125e0000497c,
    0x128900004a24, 0x128e00004a3c, 0x12b100004ac4, 0x12b600004adc, 0x12bf00004afc,
    0x12c100004b04, 0x12c600004b1c, 0x12d700004b5c, 0x131100004c44, 0x131600004c5c,
    0x135b00004d7c, 0x137d00004dfc, 0x139a00004e7c, 0x13f600004fdc, 0x13fe00004ffc,
    0x169d00005a7c, 0x16f900005bfc, 0x171200005c50, 0x171600005c78, 0x173200005ccc,
    0x173700005cfc, 0x175200005d7c, 0x176d00005db4, 0x177100005dfc, 0x17b400005ed4,
    0x17b700005ef4, 0x17c600005f18, 0x17c900005f4c, 0x17dd00005f7c, 0x17ea00005fbc,
    0x17fa00005ffc, 0x180b0000603c, 0x181a0000607c, 0x1879000061fc, 0x188500006218,
    0x18a9000062a4, 0x18ab000062bc, 0x18f6000063fc, 0x191f00006488, 0x1927000064a0,
    0x192c000064bc, 0x1932000064c8, 0x1939000064fc, 0x19410000650c, 0x196e000065bc,
    0x1975000065fc, 0x19ac000066bc, 0x19ca0000673c, 0x19db00006774, 0x1a1700006860,
    0x1a1b00006874, 0x1a5600006958, 0x1a5800006980, 0x1a6200006988, 0x1a65000069b0,
    0x1a73000069fc, 0x1a8a00006a3c, 0x1a9a00006a7c, 0x1aae00006c0c, 0x1b3400006cd0,
    0x1b3600006ce8, 0x1b3c00006cf0, 0x1b4200006d08, 0x1b4d00006d34, 0x1b6b00006dcc,
    0x1b8000006e04, 0x1ba200006e94, 0x1ba800006ea4, 0x1bab00006eb4, 0x1be600006f98,
    0x1be800006fa4, 0x1bed00006fb4, 0x1bef00006fc4, 0x1bf400006fec, 0x1c2c000070cc,
    0x1c36000070e8, 0x1c4a00007130, 0x1c8b0000723c, 0x1cbb000072f0, 0x1cc800007348,
    0x1cd400007380, 0x1ce2000073a0, 0x1ced000073b4, 0x1cf4000073d0, 0x1cf8000073e4,
    0x1cfb000073fc, 0x1dc0000077fc, 0x1f1600007c5c, 0x1f1e00007c7c, 0x1f4600007d1c,
    0x1f4e00007d3c, 0x1f5800007d60, 0x1f5a00007d68, 0x1f5c00007d70, 0x1f5e00007d78,
    0x1f7e00007dfc, 0x1fb500007ed4, 0x1fc500007f14, 0x1fd400007f54, 0x1fdc00007f70,
    0x1ff000007fc4, 0x1ff500007fd4, 0x1fff00007ffc, 0x200b0000803c, 0x2028000080b8,
    0x2060000081bc, 0x2072000081cc, 0x208f0000823c, 0x209d0000827c, 0x20c2000083fc,
    0x218c0000863c, 0x231a00008c6e, 0x232900008caa, 0x23e900008fb2, 0x23f000008fc2,
    0x23f300008fce, 0x242a000090fc, 0x244b0000917c, 0x25fd000097fa, 0x261400009856,
    0x2630000098de, 0x26480000994e, 0x267f000099fe, 0x268a00009a3e, 0x269300009a4e,
    0x26a100009a86, 0x26aa00009aae, 0x26bd00009afa, 0x26c400009b16, 0x26ce00009b3a,
    0x26d400009b52, 0x26ea00009baa, 0x26f200009bce, 0x26f500009bd6, 0x26fa00009bea,
    0x26fd00009bf6, 0x270500009c16, 0x270a00009c2e, 0x272800009ca2, 0x274c00009d32,
    0x274e00009d3a, 0x275300009d56, 0x275700009d5e, 0x279500009e5e, 0x27b000009ec2,
    0x27bf00009efe, 0x2b1b0000ac72, 0x2b500000ad42, 0x2b550000ad56, 0x2b740000add4,
    0x2cef0000b3c4, 0x2cf40000b3e0, 0x2d260000b498, 0x2d280000b4b0, 0x2d2e0000b4bc,
    0x2d680000b5b8, 0x2d710000b5fc, 0x2d970000b67c, 0x2da70000b69c, 0x2daf0000b6bc,
    0x2db70000b6dc, 0x2dbf0000b6fc, 0x2dc70000b71c, 0x2dcf0000b73c, 0x2dd70000b75c,
    0x2ddf0000b7fc, 0x2e5e0000b9fc, 0x2e800000ba66, 0x2e9a0000ba68, 0x2e9b0000bbce,
    0x2ef40000bbfc, 0x2f000000bf56, 0x2fd60000bfbc, 0x2ff00000c0a6, 0x302a0000c0b4,
    0x302e0000c0fa, 0x30400000c100, 0x30410000c25a, 0x30970000c268, 0x309b0000c3fe,
    0x31000000c410, 0x31050000c4be, 0x31300000c4c0, 0x31310000c58e, 0x31640000c590,
    0x31650000c63a, 0x318f0000c63c, 0x31900000c796, 0x31e60000c7b8, 0x31ef0000c87a,
    0x321f0000c87c, 0x322000029232, 0xa48d0002923c, 0xa4900002931a, 0xa4c70002933c,
    0xa62c000298fc, 0xa66f000299c8, 0xa674000299f4, 0xa69e00029a7c, 0xa6f000029bc4,
    0xa6f800029bfc, 0xa7dd00029fc0, 0xa8020002a008, 0xa8060002a018, 0xa80b0002a02c,
    0xa8250002a098, 0xa82c0002a0bc, 0xa83a0002a0fc, 0xa8780002a1fc, 0xa8c40002a334,
    0xa8da0002a3c4, 0xa8ff0002a3fc, 0xa9260002a4b4, 0xa9470002a544, 0xa9540002a578,
    0xa9600002a5f2, 0xa97d0002a608, 0xa9b30002a6cc, 0xa9b60002a6e4, 0xa9bc0002a6f4,
    0xa9ce0002a738, 0xa9da0002a774, 0xa9e50002a794, 0xa9ff0002a7fc, 0xaa290002a8b8,
    0xaa310002a8c8, 0xaa350002a8fc, 0xaa430002a90c, 0xaa4c0002a930, 0xaa4e0002a93c,
    0xaa5a0002a96c, 0xaa7c0002a9f0, 0xaab00002aac0, 0xaab20002aad0, 0xaab70002aae0,
    0xaabe0002aafc, 0xaac10002ab04, 0xaac30002ab68, 0xaaec0002abb4, 0xaaf60002ac00,
    0xab070002ac20, 0xab0f0002ac40, 0xab170002ac7c, 0xab270002ac9c, 0xab2f0002acbc,
    0xab6c0002adbc, 0xabe50002af94, 0xabe80002afa0, 0xabed0002afbc, 0xabfa0002affc,
    0xac0000035e8e, 0xd7a400035ffc, 0xf9000003e9b6, 0xfa6e0003e9bc, 0xfa700003eb66,
    0xfada0003ebfc, 0xfb070003ec48, 0xfb180003ec70, 0xfb1e0003ec78, 0xfb370003ecdc,
    0xfb3d0003ecf4, 0xfb3f0003ecfc, 0xfb420003ed08, 0xfb450003ed14, 0xfdd00003f7bc,
    0xfe000003f83c, 0xfe100003f866, 0xfe1a0003f8bc, 0xfe300003f94a, 0xfe530003f94c,
    0xfe540003f99a, 0xfe670003f99c, 0xfe680003f9ae, 0xfe6c0003f9bc, 0xfe750003f9d4,
    0xfefd0003fc00, 0xff010003fd82, 0xffa00003fe80, 0xffbf0003ff04, 0xffc80003ff24,
    0xffd00003ff44, 0xffd80003ff64, 0xffdd0003ff7c, 0xffe00003ff9a, 0xffe70003ff9c,
    0xffef0003ffe0, 0xfffe0003fffc, 0x1000c00040030, 0x100270004009c, 0x1003b000400ec,
    0x1003e000400f8, 0x1004e0004013c, 0x1005e000401fc, 0x100fb000403fc, 0x1010300040418,
    0x10134000404d8, 0x1018f0004063c, 0x1019d0004067c, 0x101a10004073c, 0x101fd000409fc,
    0x1029d00040a7c, 0x102d100040b80, 0x102fc00040bfc, 0x1032400040cb0, 0x1034b00040d3c,
    0x1037600040dfc, 0x1039e00040e78, 0x103c400040f1c, 0x103d600040ffc, 0x1049e0004127c,
    0x104aa000412bc, 0x104d40004135c, 0x104fc000413fc, 0x10528000414bc, 0x10564000415b8,
    0x1057b000415ec, 0x1058b0004162c, 0x105930004164c, 0x1059600041658, 0x105a200041688,
    0x105b2000416c8, 0x105ba000416e8, 0x105bd000416fc, 0x105f4000417fc, 0x1073700041cfc,
    0x1075600041d7c, 0x1076800041dfc, 0x1078600041e18, 0x107b100041ec4, 0x107bb00041ffc,
    0x108060004201c, 0x1080900042024, 0x10836000420d8, 0x10839000420ec, 0x1083d000420f8,
    0x1085600042158, 0x1089f00042298, 0x108b00004237c, 0x108f3000423cc, 0x108f6000423e8,
    0x1091c00042478, 0x1093a000424f8, 0x1095a000425fc, 0x109b8000426ec, 0x109d000042744,
    0x10a010004283c, 0x10a1400042850, 0x10a1800042860, 0x10a36000428fc, 0x10a490004293c,
    0x10a590004297c, 0x10aa000042afc, 0x10ae500042ba8, 0x10af700042bfc, 0x10b3600042ce0,
    0x10b5600042d5c, 0x10b7300042ddc, 0x10b9200042e60, 0x10b9d00042ea0, 0x10bb000042ffc,
    0x10c49000431fc, 0x10cb3000432fc, 0x10cf3000433e4, 0x10d24000434bc, 0x10d3a000434fc,
    0x10d66000435b4, 0x10d8600043634, 0x10d900004397c, 0x10e7f000439fc, 0x10eaa00043ab0,
    0x10eae00043abc, 0x10eb200043b04, 0x10ec800043b3c, 0x10ed900043bfc, 0x10f2800043cbc,
    0x10f4600043d40, 0x10f5a00043dbc, 0x10f8200043e14, 0x10f8a00043ebc, 0x10fcc00043f7c,
    0x10ff700043ffc, 0x1100100044004, 0x1103800044118, 0x1104e00044144, 0x11070000441c0,
    0x11073000441d0, 0x1107600044204, 0x110b3000442d8, 0x110b9000442e8, 0x110c200044330,
    0x110ce0004433c, 0x110e9000443bc, 0x110fa00044408, 0x11127000444ac, 0x1112d000444d4,
    0x111480004453c, 0x11173000445cc, 0x1117700044604, 0x111b6000446f8, 0x111c900044730,
    0x111cf0004473c, 0x111e000044780, 0x111f5000447fc, 0x1121200044848, 0x1122f000448c4,
    0x11234000448d0, 0x11236000448dc, 0x1123e000448f8, 0x11241000449fc, 0x1128700044a1c,
    0x1128900044a24, 0x1128e00044a38, 0x1129e00044a78, 0x112aa00044abc, 0x112df00044b7c,
    0x112e300044bbc, 0x112fa00044c04, 0x1130400044c10, 0x1130d00044c38, 0x1131100044c48,
    0x1132900044ca4, 0x1133100044cc4, 0x1133400044cd0, 0x1133a00044cf0, 0x1134000044d00,
    0x1134500044d18, 0x1134900044d28, 0x1134e00044d3c, 0x1135100044d58, 0x1135800044d70,
    0x1136400044dfc, 0x1138a00044e28, 0x1138c00044e34, 0x1138f00044e3c, 0x113b600044ed8,
    0x113bb00044f04, 0x113c300044f10, 0x113c600044f18, 0x113cb00044f2c, 0x113ce00044f38,
    0x113d000044f40, 0x113d200044f48, 0x113d600044f58, 0x113d900044ffc, 0x11438000450fc,
    0x1144200045110, 0x1144600045118, 0x1145c00045170, 0x1145e00045178, 0x11462000451fc,
    0x114b3000452e0, 0x114ba000452e8, 0x114bf00045300, 0x114c20004530c, 0x114c80004533c,
    0x114da000455fc, 0x115b2000456dc, 0x115bc000456f4, 0x115bf00045700, 0x115dc000457fc,
    0x11633000458e8, 0x1163d000458f4, 0x1163f00045900, 0x116450004593c, 0x1165a0004597c,
    0x1166d000459fc, 0x116ab00045aac, 0x116ad00045ab4, 0x116b000045ad4, 0x116b700045adc,
    0x116ba00045afc, 0x116ca00045b3c, 0x116e400045bfc, 0x1171b00045c74, 0x1171f00045c7c,
    0x1172200045c94, 0x1172700045cbc, 0x1174700045ffc, 0x1182f000460dc, 0x11839000460e8,
    0x1183c0004627c, 0x118f3000463f8, 0x1190700046420, 0x1190a0004642c, 0x1191400046450,
    0x119170004645c, 0x11936000464d8, 0x11939000464f0, 0x1193e000464f8, 0x119430004650c,
    0x119470004653c, 0x1195a0004667c, 0x119a8000466a4, 0x119d40004676c, 0x119e000046780,
    0x119e5000467fc, 0x11a0100046828, 0x11a33000468e0, 0x11a3b000468f8, 0x11a470004693c,
    0x11a5100046958, 0x11a590004696c, 0x11a8a00046a58, 0x11a9800046a64, 0x11aa300046abc,
    0x11af900046bfc, 0x11b0a00046d80, 0x11b6200046d90, 0x11b6600046d98, 0x11b6800046efc,
    0x11be200046fbc, 0x11bfa00046ffc, 0x11c0900047024, 0x11c30000470f4, 0x11c3f000470fc,
    0x11c460004713c, 0x11c6d000471bc, 0x11c90000472a0, 0x11caa000472c0, 0x11cb2000472cc,
    0x11cb5000473fc, 0x11d070004741c, 0x11d0a00047428, 0x11d3100047514, 0x11d470004753c,
    0x11d5a0004757c, 0x11d6600047598, 0x11d69000475a4, 0x11d8f00047648, 0x11d9500047654,
    0x11d970004765c, 0x11d990004767c, 0x11daa000476bc, 0x11ddc0004777c, 0x11dea00047b7c,
    0x11ef300047bd0, 0x11ef900047c04, 0x11f1100047c44, 0x11f3600047cf4, 0x11f4000047d00,
    0x11f4200047d08, 0x11f5a00047ebc, 0x11fb100047efc, 0x11ff200047ff8, 0x1239a00048ffc,
    0x1246f000491bc, 0x12475000491fc, 0x125440004be3c, 0x12ff30004bffc, 0x134400004d100,
    0x134470004d17c, 0x143fb00050ffc, 0x14647000583fc, 0x1611e000584a4, 0x1612d000584bc,
    0x1613a00059ffc, 0x16a390005a8fc, 0x16a5f0005a97c, 0x16a6a0005a9b4, 0x16abf0005aafc,
    0x16aca0005ab3c, 0x16aee0005abd0, 0x16af60005abfc, 0x16b300005acd8, 0x16b460005ad3c,
    0x16b5a0005ad68, 0x16b620005ad88, 0x16b780005adf0, 0x16b900005b4fc, 0x16d7a0005b8fc,
    0x16e9b0005ba7c, 0x16eb90005bae8, 0x16ed40005bbfc, 0x16f4b0005bd3c, 0x16f880005be48,
    0x16fa00005bf7c, 0x16fe00005bf8e, 0x16fe40005bfbc, 0x16ff00005bfda, 0x16ff70005bffc,
    0x1700000063356, 0x18cd6000633f8, 0x18cff0006347a, 0x18d1f000635fc, 0x18d80000637ca,
    0x18df30006bfbc, 0x1aff00006bfce, 0x1aff40006bfd0, 0x1aff50006bfee, 0x1affc0006bff0,
    0x1affd0006bffa, 0x1afff0006bffc, 0x1b0000006c48a, 0x1b1230006c4c4, 0x1b1320006c4ca,
    0x1b1330006c53c, 0x1b1500006c54a, 0x1b1530006c550, 0x1b1550006c556, 0x1b1560006c58c,
    0x1b1640006c59e, 0x1b1680006c5bc, 0x1b1700006cbee, 0x1b2fc0006effc, 0x1bc6b0006f1bc,
    0x1bc7d0006f1fc, 0x1bc890006f23c, 0x1bc9a0006f26c, 0x1bc9d0006f278, 0x1bca000072ffc,
    0x1ccfd000733fc, 0x1ceb400073ae4, 0x1ced100073b7c, 0x1cef100073d3c, 0x1cfc400073ffc,
    0x1d0f6000743fc, 0x1d127000744a0, 0x1d167000745a4, 0x1d17300074608, 0x1d1850007462c,
    0x1d1aa000746b4, 0x1d1eb000747fc, 0x1d24200074910, 0x1d24600074afc, 0x1d2d400074b7c,
    0x1d2f400074bfc, 0x1d30000074d5a, 0x1d35700074d7c, 0x1d36000074dda, 0x1d37900074ffc,
    0x1d45500075154, 0x1d49d00075274, 0x1d4a000075284, 0x1d4a300075290, 0x1d4a7000752a0,
    0x1d4ad000752b4, 0x1d4ba000752e8, 0x1d4bc000752f0, 0x1d4c400075310, 0x1d50600075418,
    0x1d50b00075430, 0x1d51500075454, 0x1d51d00075474, 0x1d53a000754e8, 0x1d53f000754fc,
    0x1d54500075514, 0x1d54700075524, 0x1d55100075544, 0x1d6a600075a9c, 0x1d7cc00075f34,
    0x1da00000768d8, 0x1da3b000769b0, 0x1da75000769d4, 0x1da8400076a10, 0x1da8c00077bfc,
    0x1df1f00077c90, 0x1df2b000780bc, 0x1e06e000783fc, 0x1e12d000784d8, 0x1e13e000784fc,
    0x1e14a00078534, 0x1e15000078a3c, 0x1e2ae00078afc, 0x1e2ec00078bbc, 0x1e2fa00078bf8,
    0x1e3000007933c, 0x1e4ec000793bc, 0x1e4fa0007973c, 0x1e5ee000797bc, 0x1e5fb000797f8,
    0x1e60000079afc, 0x1e6df00079b7c, 0x1e6e300079b8c, 0x1e6e600079b98, 0x1e6ee00079bbc,
    0x1e6f500079bf4, 0x1e70000079f7c, 0x1e7e700079f9c, 0x1e7ec00079fb0, 0x1e7ef00079fbc,
    0x1e7ff00079ffc, 0x1e8c50007a318, 0x1e8d00007a3fc, 0x1e9440007a528, 0x1e94c0007a53c,
    0x1e95a0007a574, 0x1e9600007b1c0, 0x1ecb50007b400, 0x1ed3e0007b7fc, 0x1ee040007b810,
    0x1ee200007b880, 0x1ee230007b88c, 0x1ee250007b898, 0x1ee280007b8a0, 0x1ee330007b8cc,
    0x1ee380007b8e0, 0x1ee3a0007b8e8, 0x1ee3c0007b904, 0x1ee430007b918, 0x1ee480007b920,
    0x1ee4a0007b928, 0x1ee4c0007b930, 0x1ee500007b940, 0x1ee530007b94c, 0x1ee550007b958,
    0x1ee580007b960, 0x1ee5a0007b968, 0x1ee5c0007b970, 0x1ee5e0007b978, 0x1ee600007b980,
    0x1ee630007b98c, 0x1ee650007b998, 0x1ee6b0007b9ac, 0x1ee730007b9cc, 0x1ee780007b9e0,
    0x1ee7d0007b9f4, 0x1ee7f0007b9fc, 0x1ee8a0007ba28, 0x1ee9c0007ba80, 0x1eea40007ba90,
    0x1eeaa0007baa8, 0x1eebc0007bbbc, 0x1eef20007bffc, 0x1f0040007c012, 0x1f02c0007c0bc,
    0x1f0940007c27c, 0x1f0af0007c2c0, 0x1f0c00007c300, 0x1f0cf0007c33e, 0x1f0d00007c340,
    0x1f0f60007c3fc, 0x1f18e0007c63a, 0x1f1910007c66a, 0x1f1ae0007c794, 0x1f2000007c80a,
    0x1f2030007c83c, 0x1f2100007c8ee, 0x1f23c0007c8fc, 0x1f2400007c922, 0x1f2490007c93c,
    0x1f2500007c946, 0x1f2520007c97c, 0x1f2600007c996, 0x1f2660007cbfc, 0x1f3000007cc82,
    0x1f32d0007ccd6, 0x1f3370007cdf2, 0x1f37e0007ce4e, 0x1f3a00007cf2a, 0x1f3cf0007cf4e,
    0x1f3e00007cfc2, 0x1f3f40007cfd2, 0x1f3f80007d0fa, 0x1f4400007d102, 0x1f4420007d3f2,
    0x1f4ff0007d4f6, 0x1f54b0007d53a, 0x1f5500007d59e, 0x1f57a0007d5ea, 0x1f5950007d65a,
    0x1f5a40007d692, 0x1f5fb0007d93e, 0x1f6800007db16, 0x1f6cc0007db32, 0x1f6d00007db4a,
    0x1f6d50007db62, 0x1f6d90007db6c, 0x1f6dc0007db7e, 0x1f6eb0007dbb2, 0x1f6ed0007dbbc,
    0x1f6f40007dbf2, 0x1f6fd0007dbfc, 0x1f7da0007df7c, 0x1f7e00007dfae, 0x1f7ec0007dfbc,
    0x1f7f00007dfc2, 0x1f7f10007dffc, 0x1f80c0007e03c, 0x1f8480007e13c, 0x1f85a0007e17c,
    0x1f8880007e23c, 0x1f8ae0007e2bc, 0x1f8bc0007e2fc, 0x1f8c20007e33c, 0x1f8d90007e3fc,
    0x1f90c0007e4ea, 0x1f93c0007e516, 0x1f9470007e7fe, 0x1fa580007e97c, 0x1fa6e0007e9bc,
    0x1fa700007e9f2, 0x1fa7d0007e9fc, 0x1fa800007ea2a, 0x1fa8b0007ea34, 0x1fa8e0007eb1a,
    0x1fac70007eb1c, 0x1fac80007eb22, 0x1fac90007eb30, 0x1facd0007eb72, 0x1fadd0007eb78,
    0x1fadf0007ebaa, 0x1faeb0007ebb8, 0x1faef0007ebe2, 0x1faf90007ebfc, 0x1fb930007ee4c,
    0x1fbfb0007fffc, 0x20000000a9b7e, 0x2a6e0000a9bfc, 0x2a700000ae076, 0x2b81e000ae07c,
    0x2b820000b3ab6, 0x2ceae000b3abc, 0x2ceb0000baf82, 0x2ebe1000bafbc, 0x2ebf0000bb976,
    0x2ee5e000bdffc, 0x2f800000be876, 0x2fa1e000bfffc, 0x30000000c4d2a, 0x3134b000c4d3c,
    0x31350000cd1e6, 0x3347a003bfffc, 0xffffe003ffffc, 0x10fffe0043fffc,
};

static positive unicode_width_reference_wcwidth(p32 code)
{
        positive low = 0;
        positive high = UNICODE_WIDTH_REFERENCE_ROWS;

        while (low < high)
        {
                positive middle = (low + high) / 2;

                if ((p32)(unicode_width_reference_rows[middle] >> 32) <= code)
                        low = middle + 1;
                else
                        high = middle;
        }

        if (!low)
                return 1;

        p64 row = unicode_width_reference_rows[low - 1];

        return code <= (p32)((row >> 2) & 0x3fffffff) ? (positive)(row & 3) : 1;
}

#define UNICODE_WIDTH_REFERENCE_SPAN 0x40200u
#define UNICODE_WIDTH_REFERENCE_BLOCKS 256
#define UNICODE_WIDTH_REFERENCE_LEAVES 512

static PURE p32 unicode_width_reference_code(positive index)
{
        return index < 0x40000 ? (p32)index : (p32)(index - 0x40000 + 0xe0000);
}

/* The table, into room bytes at out. Answers its size, with where the
   blocks and the leaves start, or 0 when it does not fit. */
static positive unicode_width_reference_table(p8 address_to out, positive room,
                                              positive address_to blocks_at,
                                              positive address_to leaves_at)
{
        static p8 leaves[UNICODE_WIDTH_REFERENCE_LEAVES * 8];
        static p8 blocks[UNICODE_WIDTH_REFERENCE_BLOCKS * 16];
        positive leaf_count = 0;
        positive block_count = 0;
        positive stages = UNICODE_WIDTH_REFERENCE_SPAN / 128;
        positive size = (stages + 7) & ~(positive)7;

        if (room < size)
                return 0;

        for (positive stage = 0; stage < stages; stage++)
        {
                p8 block[16];
                positive at;

                for (positive part = 0; part < 8; part++)
                {
                        p8 leaf[8];

                        for (positive k = 0; k < 16; k++)
                        {
                                p32 code = unicode_width_reference_code(
                                    stage * 128 + part * 16 + k);
                                p8 nibble = (p8)(unicode_width_reference_terminal(code) |
                                                 unicode_width_reference_wcwidth(code) << 2);

                                if (k & 1)
                                        leaf[k / 2] |= (p8)(nibble << 4);
                                else
                                        leaf[k / 2] = nibble;
                        }

                        for (at = 0; at < leaf_count; at++)
                                if (!memory_compare(leaves + at * 8, leaf, 8))
                                        break;

                        if (at == leaf_count)
                        {
                                if (leaf_count == UNICODE_WIDTH_REFERENCE_LEAVES)
                                        return 0;
                                memory_copy(leaves + at * 8, leaf, 8);
                                leaf_count++;
                        }

                        block[part * 2] = (p8)(at * 8);
                        block[part * 2 + 1] = (p8)(at * 8 >> 8);
                }

                for (at = 0; at < block_count; at++)
                        if (!memory_compare(blocks + at * 16, block, 16))
                                break;

                if (at == block_count)
                {
                        if (block_count == UNICODE_WIDTH_REFERENCE_BLOCKS)
                                return 0;
                        memory_copy(blocks + at * 16, block, 16);
                        block_count++;
                }

                out[stage] = (p8)at;
        }

        if (size + block_count * 16 + leaf_count * 8 > room)
                return 0;

        for (positive pad = stages; pad < size; pad++)
                out[pad] = 0;

        address_to blocks_at = size;
        memory_copy(out + size, blocks, block_count * 16);
        size += block_count * 16;
        address_to leaves_at = size;
        memory_copy(out + size, leaves, leaf_count * 8);
        return size + leaf_count * 8;
}

#endif
#elif defined(SHARED_native)
/*
        What the native sections share: the references the hunts are held
        to -- strchr, strchrnul, a bounded strchr, strnlen, memchr, and
        strrchr that also finds the terminator -- the decimal digits the
        lifted formatters' relocations reach, the counter the timings read,
        and one xorshift. A section that draws from next defines NATIVE_SEED
        as its starting state first, so it draws the cases it always did.
*/
typedef unsigned char u8;
typedef unsigned long u64;

int printf(const char *, ...);

static char *r_first(const char *s, int c)
{
        for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}

static char *r_or_end(const char *s, int c)
{
        while (*s && *s != (char)c) s++;
        return (char *)s;
}

static char *r_max(const char *s, u64 n, int c)
{
        for (u64 i = 0; i < n; i++) {
                if (s[i] == (char)c) return (char *)(s + i);
                if (!s[i]) return 0;
        }
        return 0;
}

static u64 r_nlen(const char *s, u64 n)
{
        u64 i = 0;
        while (i < n && s[i]) i++;
        return i;
}

static void *r_mchr(const void *s, int c, u64 n)
{
        const u8 *p = s;
        for (u64 i = 0; i < n; i++) if (p[i] == (u8)c) return (void *)(p + i);
        return 0;
}

static char *r_last_or_end(const char *s, int c)
{
        const char *best = 0;
        for (;; s++) {
                if (*s == (char)c) best = s;
                if (!*s) return (char *)best;
        }
}

static u64 reference_into(u8 *into, u64 value)
{
        u8 reverse[24];
        u64 length = 0;

        do {
                reverse[length++] = (u8)('0' + value % 10);
                value /= 10;
        } while (value);

        for (u64 i = 0; i < length; i++) into[i] = reverse[length - i - 1];
        return length;
}

static u64 ticks(void)
{
        u64 value;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
        return value;
}

#ifdef NATIVE_SEED
static u64 seed = NATIVE_SEED;

static u64 next(void)
{
        XORSHIFT64(seed);
        return seed;
}
#endif

#elif defined(SHARED_x25519_reference)
/*
        RFC 7748 X25519 as crypto.c ran it in C before lib.c's x25519: five
        51-bit limbs, field reduce, freeze and the Montgomery step after the
        public-domain 64-bit curve25519-donna, the volatile wipes included.
        Kept whole so BENCH_montgomery can time the assembly against it at
        crypto_x25519, its caller, and crypto_fuzz can run it where the
        assembly cannot be lifted (MemorySanitizer, a host that is not
        x86_64). Include it after crypto.c.
*/
#ifndef DAWNING_X25519_REFERENCE_C
#define DAWNING_X25519_REFERENCE_C

typedef unsigned __int128 x25519_reference_wide;

//      crypto.c's crypto_forget as it stood.
static fn x25519_reference_forget(address_any secret, positive length)
{
        memory_fill(secret, 0, length);
        __asm__ __volatile__("" : : "r"(secret) : "memory");
}

typedef p64 x25519_reference_fe[5];

static p64 x25519_reference_load64(p8 address_to in)
{
        return (p64)in[0] | ((p64)in[1] << 8) | ((p64)in[2] << 16) |
               ((p64)in[3] << 24) | ((p64)in[4] << 32) | ((p64)in[5] << 40) |
               ((p64)in[6] << 48) | ((p64)in[7] << 56);
}

static fn x25519_reference_store64(p8 address_to out, p64 in)
{
        out[0] = (p8)in;
        out[1] = (p8)(in >> 8);
        out[2] = (p8)(in >> 16);
        out[3] = (p8)(in >> 24);
        out[4] = (p8)(in >> 32);
        out[5] = (p8)(in >> 40);
        out[6] = (p8)(in >> 48);
        out[7] = (p8)(in >> 56);
}

static fn x25519_reference_copy(x25519_reference_fe o, x25519_reference_fe a)
{
        o[0] = a[0];
        o[1] = a[1];
        o[2] = a[2];
        o[3] = a[3];
        o[4] = a[4];
}

static fn x25519_reference_load(x25519_reference_fe out, p8 address_to in)
{
        out[0] = x25519_reference_load64(in) & 0x7ffffffffffffull;
        out[1] = (x25519_reference_load64(in + 6) >> 3) & 0x7ffffffffffffull;
        out[2] = (x25519_reference_load64(in + 12) >> 6) & 0x7ffffffffffffull;
        out[3] = (x25519_reference_load64(in + 19) >> 1) & 0x7ffffffffffffull;
        out[4] = (x25519_reference_load64(in + 24) >> 12) & 0x7ffffffffffffull;
}

/*
        One pass of the 51 bit carry chain.

        Each limb hands what will not fit to the one above it. wrap folds the
        carry leaving the top limb back into the bottom one at the weight the
        prime gives it -- 2^255 is 19 -- and is how the chain stays inside
        the field. The pass that finishes a store is the one exception: there
        the carry out of the top is the answer to the conditional
        subtraction, and dropping it is what performs the subtraction.
*/
static fn x25519_reference_carry(x25519_reference_wide address_to t, p8 wrap)
{
        for (positive i = 0; i < 4; i++)
        {
                t[i + 1] += t[i] >> 51;
                t[i] &= 0x7ffffffffffffull;
        }

        if (wrap)
                t[0] += 19 * (t[4] >> 51);
        t[4] &= 0x7ffffffffffffull;
}

static fn x25519_reference_store(p8 address_to out, x25519_reference_fe in)
{
        x25519_reference_wide t[5];

        for (positive i = 0; i < 5; i++)
                t[i] = in[i];

        x25519_reference_carry(t, true);
        x25519_reference_carry(t, true);

        //      Adding 19 and carrying turns a value in [p, 2p) into one in
        //      [0, p) with the top limb's bit set, which the constants below
        //      then clear; a value already below p is unchanged by the pair.
        t[0] += 19;
        x25519_reference_carry(t, true);

        t[0] += 0x8000000000000ull - 19;
        for (positive i = 1; i < 5; i++)
                t[i] += 0x8000000000000ull - 1;
        x25519_reference_carry(t, false);

        x25519_reference_store64(out, (p64)(t[0] | (t[1] << 51)));
        x25519_reference_store64(out + 8, (p64)((t[1] >> 13) | (t[2] << 38)));
        x25519_reference_store64(out + 16, (p64)((t[2] >> 26) | (t[3] << 25)));
        x25519_reference_store64(out + 24, (p64)((t[3] >> 39) | (t[4] << 12)));
        x25519_reference_forget(t, sizeof t);
}

static fn x25519_reference_sum(x25519_reference_fe o, x25519_reference_fe in)
{
        o[0] += in[0];
        o[1] += in[1];
        o[2] += in[2];
        o[3] += in[3];
        o[4] += in[4];
}

static fn x25519_reference_diff(x25519_reference_fe o, x25519_reference_fe in)
{
        o[0] = in[0] + 0x3fffffffffff68ull - o[0];
        o[1] = in[1] + 0x3ffffffffffff8ull - o[1];
        o[2] = in[2] + 0x3ffffffffffff8ull - o[2];
        o[3] = in[3] + 0x3ffffffffffff8ull - o[3];
        o[4] = in[4] + 0x3ffffffffffff8ull - o[4];
}

static fn x25519_reference_mul(x25519_reference_fe o, x25519_reference_fe in2,
                            x25519_reference_fe in)
{
        x25519_reference_wide t[5];
        p64 r0, r1, r2, r3, r4, s0, s1, s2, s3, s4, c;

        r0 = in[0];
        r1 = in[1];
        r2 = in[2];
        r3 = in[3];
        r4 = in[4];
        s0 = in2[0];
        s1 = in2[1];
        s2 = in2[2];
        s3 = in2[3];
        s4 = in2[4];

        t[0] = (x25519_reference_wide)r0 * s0;
        t[1] = (x25519_reference_wide)r0 * s1 + (x25519_reference_wide)r1 * s0;
        t[2] = (x25519_reference_wide)r0 * s2 + (x25519_reference_wide)r2 * s0 + (x25519_reference_wide)r1 * s1;
        t[3] = (x25519_reference_wide)r0 * s3 + (x25519_reference_wide)r3 * s0 + (x25519_reference_wide)r1 * s2 +
               (x25519_reference_wide)r2 * s1;
        t[4] = (x25519_reference_wide)r0 * s4 + (x25519_reference_wide)r4 * s0 + (x25519_reference_wide)r3 * s1 +
               (x25519_reference_wide)r1 * s3 + (x25519_reference_wide)r2 * s2;

        //      Limbs stay below 2^55 (a difference is at most 2^52 + 8p's
        //      2^54 limb), so nineteen of one fits a word and each wrapped
        //      product is one 64 by 64 multiply rather than a 128 by 64.
        r1 *= 19;
        r2 *= 19;
        r3 *= 19;
        r4 *= 19;
        t[0] += (x25519_reference_wide)r4 * s1 + (x25519_reference_wide)r1 * s4 +
                (x25519_reference_wide)r2 * s3 + (x25519_reference_wide)r3 * s2;
        t[1] += (x25519_reference_wide)r4 * s2 + (x25519_reference_wide)r2 * s4 +
                (x25519_reference_wide)r3 * s3;
        t[2] += (x25519_reference_wide)r4 * s3 + (x25519_reference_wide)r3 * s4;
        t[3] += (x25519_reference_wide)r4 * s4;

        r0 = (p64)t[0] & 0x7ffffffffffffull;
        c = (p64)(t[0] >> 51);
        t[1] += c;
        r1 = (p64)t[1] & 0x7ffffffffffffull;
        c = (p64)(t[1] >> 51);
        t[2] += c;
        r2 = (p64)t[2] & 0x7ffffffffffffull;
        c = (p64)(t[2] >> 51);
        t[3] += c;
        r3 = (p64)t[3] & 0x7ffffffffffffull;
        c = (p64)(t[3] >> 51);
        t[4] += c;
        r4 = (p64)t[4] & 0x7ffffffffffffull;
        c = (p64)(t[4] >> 51);
        r0 += c * 19;
        c = r0 >> 51;
        r0 &= 0x7ffffffffffffull;
        r1 += c;
        c = r1 >> 51;
        r1 &= 0x7ffffffffffffull;
        r2 += c;

        o[0] = r0;
        o[1] = r1;
        o[2] = r2;
        o[3] = r3;
        o[4] = r4;
        x25519_reference_forget(t, sizeof t);
}

/* o = a^(2^n), n at least one: each square makes each cross product once
   and doubles it, fifteen multiplies where x25519_reference_mul makes
   twenty five, with the same carry chain. */
static fn x25519_reference_sqr_n(x25519_reference_fe o, x25519_reference_fe a, positive n)
{
        x25519_reference_wide t[5];
        p64 r0 = a[0], r1 = a[1], r2 = a[2], r3 = a[3], r4 = a[4];

        while (n--)
        {
                p64 d0 = r0 * 2;
                p64 d1 = r1 * 2;
                p64 d2 = r2 * 2 * 19;
                p64 d419 = r4 * 19;
                p64 d4 = d419 * 2;
                p64 c;

                t[0] = (x25519_reference_wide)r0 * r0 + (x25519_reference_wide)d4 * r1 +
                       (x25519_reference_wide)d2 * r3;
                t[1] = (x25519_reference_wide)d0 * r1 + (x25519_reference_wide)d4 * r2 +
                       (x25519_reference_wide)r3 * (r3 * 19);
                t[2] = (x25519_reference_wide)d0 * r2 + (x25519_reference_wide)r1 * r1 +
                       (x25519_reference_wide)d4 * r3;
                t[3] = (x25519_reference_wide)d0 * r3 + (x25519_reference_wide)d1 * r2 +
                       (x25519_reference_wide)r4 * d419;
                t[4] = (x25519_reference_wide)d0 * r4 + (x25519_reference_wide)d1 * r3 +
                       (x25519_reference_wide)r2 * r2;

                t[1] += (p64)(t[0] >> 51);
                r0 = (p64)t[0] & 0x7ffffffffffffull;
                t[2] += (p64)(t[1] >> 51);
                r1 = (p64)t[1] & 0x7ffffffffffffull;
                t[3] += (p64)(t[2] >> 51);
                r2 = (p64)t[2] & 0x7ffffffffffffull;
                t[4] += (p64)(t[3] >> 51);
                r3 = (p64)t[3] & 0x7ffffffffffffull;
                r4 = (p64)t[4] & 0x7ffffffffffffull;
                r0 += 19 * (p64)(t[4] >> 51);
                c = r0 >> 51;
                r0 &= 0x7ffffffffffffull;
                r1 += c;
                c = r1 >> 51;
                r1 &= 0x7ffffffffffffull;
                r2 += c;
        }

        o[0] = r0;
        o[1] = r1;
        o[2] = r2;
        o[3] = r3;
        o[4] = r4;
        x25519_reference_forget(t, sizeof t);
}

static fn x25519_reference_mul121665(x25519_reference_fe o, x25519_reference_fe a)
{
        x25519_reference_wide w;

        w = (x25519_reference_wide)a[0] * 121665;
        o[0] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (x25519_reference_wide)a[1] * 121665;
        o[1] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (x25519_reference_wide)a[2] * 121665;
        o[2] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (x25519_reference_wide)a[3] * 121665;
        o[3] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (x25519_reference_wide)a[4] * 121665;
        o[4] = (p64)w & 0x7ffffffffffffull;
        o[0] += 19 * (p64)(w >> 51);
        x25519_reference_forget(address_of w, sizeof w);
}

static fn x25519_reference_invert(x25519_reference_fe o, x25519_reference_fe z)
{
        x25519_reference_fe a, t0, b, c;

        x25519_reference_sqr_n(a, z, 1);
        x25519_reference_sqr_n(t0, a, 2);
        x25519_reference_mul(b, t0, z);
        x25519_reference_mul(a, b, a);
        x25519_reference_sqr_n(t0, a, 1);
        x25519_reference_mul(b, t0, b);
        x25519_reference_sqr_n(t0, b, 5);
        x25519_reference_mul(b, t0, b);
        x25519_reference_sqr_n(t0, b, 10);
        x25519_reference_mul(c, t0, b);
        x25519_reference_sqr_n(t0, c, 20);
        x25519_reference_mul(t0, t0, c);
        x25519_reference_sqr_n(t0, t0, 10);
        x25519_reference_mul(b, t0, b);
        x25519_reference_sqr_n(t0, b, 50);
        x25519_reference_mul(c, t0, b);
        x25519_reference_sqr_n(t0, c, 100);
        x25519_reference_mul(t0, t0, c);
        x25519_reference_sqr_n(t0, t0, 50);
        x25519_reference_mul(t0, t0, b);
        x25519_reference_sqr_n(t0, t0, 5);
        x25519_reference_mul(o, t0, a);

        x25519_reference_forget(a, sizeof a);
        x25519_reference_forget(t0, sizeof t0);
        x25519_reference_forget(b, sizeof b);
        x25519_reference_forget(c, sizeof c);
}

static fn x25519_reference_cswap(x25519_reference_fe a, x25519_reference_fe b, p64 swap)
{
        positive i;

        swap = 0 - swap;
        for (i = 0; i < 5; i++)
        {
                p64 t = swap & (a[i] ^ b[i]);
                a[i] ^= t;
                b[i] ^= t;
        }
}

static fn x25519_reference(p8 address_to out, const p8 address_to scalar,
                             const p8 address_to u)
{
        //      Everything the ladder derives from the scalar, wiped as one.
        struct
        {
                p8 e[32];
                x25519_reference_fe x1, x2, z2, x3, z3;
                x25519_reference_fe a, b, c, d, aa, bb, ee, da, cb, t;
                p64 bit;
                p64 swap;
        } w;
        positive i;

        memory_copy(w.e, scalar, 32);
        w.swap = 0;
        w.e[0] &= 248;
        w.e[31] &= 127;
        w.e[31] |= 64;

        x25519_reference_load(w.x1, (p8 address_to)u);
        memory_fill(w.x2, 0, sizeof(w.x2));
        w.x2[0] = 1;
        memory_fill(w.z2, 0, sizeof(w.z2));
        x25519_reference_copy(w.x3, w.x1);
        memory_fill(w.z3, 0, sizeof(w.z3));
        w.z3[0] = 1;

        for (i = 254; i < 256; i--)
        {
                w.bit = (w.e[i >> 3] >> (i & 7)) & 1;
                w.swap ^= w.bit;
                x25519_reference_cswap(w.x2, w.x3, w.swap);
                x25519_reference_cswap(w.z2, w.z3, w.swap);
                w.swap = w.bit;

                x25519_reference_copy(w.a, w.x2);
                x25519_reference_sum(w.a, w.z2);
                x25519_reference_copy(w.b, w.z2);
                x25519_reference_diff(w.b, w.x2);
                x25519_reference_copy(w.c, w.x3);
                x25519_reference_sum(w.c, w.z3);
                x25519_reference_copy(w.d, w.z3);
                x25519_reference_diff(w.d, w.x3);

                x25519_reference_mul(w.da, w.d, w.a);
                x25519_reference_mul(w.cb, w.c, w.b);
                x25519_reference_sqr_n(w.aa, w.a, 1);
                x25519_reference_sqr_n(w.bb, w.b, 1);

                x25519_reference_copy(w.t, w.da);
                x25519_reference_sum(w.t, w.cb);
                x25519_reference_sqr_n(w.x3, w.t, 1);

                x25519_reference_copy(w.t, w.cb);
                x25519_reference_diff(w.t, w.da);
                x25519_reference_sqr_n(w.t, w.t, 1);
                x25519_reference_mul(w.z3, w.x1, w.t);

                x25519_reference_mul(w.x2, w.aa, w.bb);

                x25519_reference_copy(w.ee, w.bb);
                x25519_reference_diff(w.ee, w.aa);
                x25519_reference_mul121665(w.t, w.ee);
                x25519_reference_sum(w.t, w.aa);
                x25519_reference_mul(w.z2, w.ee, w.t);
        }

        x25519_reference_cswap(w.x2, w.x3, w.swap);
        x25519_reference_cswap(w.z2, w.z3, w.swap);
        x25519_reference_invert(w.z2, w.z2);
        x25519_reference_mul(w.x2, w.x2, w.z2);
        x25519_reference_store(out, w.x2);

        x25519_reference_forget(address_of w, sizeof w);
}

#endif
#elif defined(SHARED_number_stream)
/*
        The float conversions over generated text, written once for two sides.

        CHECK_number_differential compiles this over this tree's strtod and
        strtof and CHECK_number_differential_reference over the machine's
        glibc. Both walk the same seeded cases in blocks of 65,536 and print
        one line a block, a hash over the bits, the end offset and the errno
        of both conversions, and the numbers lane diffs the two. Asked for one
        block by name a program prints every case of that block instead, so a
        block that disagrees names its inputs on the next run. A block is
        seeded by its index alone, so any one of them can be run by itself and
        a long sweep can be cut into ranges that run side by side.

        CHECK_number draws from the same generator to hold
        string_to_decimal_short to the general path, which needs no glibc.

        THE FAMILIES, AND WHAT EACH ONE IS FOR

        Random decimals: one to forty digits and now and then a thousand, a
        point anywhere or nowhere, an exponent from -350 to 320 or none,
        signs, space in front and a stray byte behind. Most of what a program
        writes, and most of what lands in the estimating tier.

        Clinger's window: significands up to and around two to the fifty third
        and powers of ten within twenty five, which is the edge of what
        string_to_decimal_short accepts and of the exact tier behind it.

        Midpoints: the exact decimal of the point halfway between a random
        double or float and the next one up, subnormals and the top of the
        range included, then cut short, cut short and bumped a unit, pushed a
        unit below, padded with zeros past the eight hundred and thirty two
        digits the register holds, or given a one past its last digit.
        Nothing else reaches the slow tier as often, and a tie is the one
        place rounding can be wrong while every other answer is right.

        Exact floats: the decimal expansion of a random double or float, and
        of the smallest subnormal, the largest subnormal, the smallest normal,
        the largest finite value and the tininess threshold, with the same
        variations. That is where errno is decided.

        Zero runs: leading zeros, zeros after a point, zeros inside the
        significand, and runs long enough for the register and the point to
        clamp.

        Hexadecimal floats and the malformed: 0x with and without digits,
        points and binary exponents, and strings put together from the pieces
        a parser stumbles on -- signs, points, e, x, p and the names.

        Nothing here calls a C library. What a side supplies is NS_STRTOD,
        NS_STRTOF, NS_ERRNO and ns_emit, defined before the include; a section
        that only wants the generator defines none of them.
*/
typedef unsigned long long ns_u64;
typedef unsigned int ns_u32;

static ns_u64 ns_state;

static ns_u64 ns_next(void)
{
        ns_u64 mixed = (ns_state += 0x9e3779b97f4a7c15ULL);

        mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
        return mixed ^ (mixed >> 31);
}

static ns_u64 ns_below(ns_u64 bound)
{
        return ns_next() % bound;
}

#define NS_TEXT 4096
#define NS_DIGITS 2048
#define NS_LIMBS 200
#define NS_BLOCK 65536

static char ns_text[NS_TEXT];
static int ns_at;
static char ns_digit[NS_DIGITS];
static int ns_count;
static ns_u32 ns_limb[NS_LIMBS];
static int ns_used;

static void ns_put(char byte)
{
        if (ns_at < NS_TEXT - 1)
                ns_text[ns_at++] = byte;
}

static void ns_puts(const char *piece)
{
        while (*piece)
                ns_put(*piece++);
}

static void ns_zeros(long long count)
{
        while (count-- > 0)
                ns_put('0');
}

static void ns_number(ns_u64 value)
{
        char held[24];
        int count = 0;

        do
        {
                held[count++] = (char)('0' + value % 10);
                value /= 10;
        } while (value);

        while (count)
                ns_put(held[--count]);
}

//      An optional sign, and now and then the space strtod skips.
static void ns_front(void)
{
        ns_u64 pick = ns_below(100);

        if (pick < 3)
                ns_puts(pick == 0 ? " " : pick == 1 ? "\t" : "\n\v ");

        pick = ns_below(100);
        if (pick < 20)
                ns_put('-');
        else if (pick < 25)
                ns_put('+');
}

//      An exponent in any spelling: either letter, a sign or none, and
//      sometimes zeros in front of its digits.
static void ns_exponent(long long value)
{
        ns_put(ns_below(2) ? 'e' : 'E');

        if (value < 0)
                ns_put('-');
        else if (ns_below(3) == 0)
                ns_put('+');

        if (ns_below(8) == 0)
                ns_zeros(1 + (long long)ns_below(4));

        ns_number(value < 0 ? (ns_u64)-value : (ns_u64)value);
}

//      And now and then a byte behind the number, which may or may not turn
//      out to be part of it.
static void ns_back(void)
{
        static const char tails[] = "xXeE.+-p9 _";

        if (ns_below(10) == 0)
                ns_put(tails[ns_below(sizeof tails - 1)]);
}

//      digits times ten to the power, spelled one of the ways a program
//      spells a number: an exponent after the digits, a point after the
//      first digit or anywhere, or the point written out with no exponent.
static void ns_render(long long power)
{
        ns_u64 style = ns_below(8);
        int index;

        if (style < 2)
        {
                for (index = 0; index < ns_count; index++)
                        ns_put(ns_digit[index]);

                if (power != 0 || ns_below(4) == 0)
                        ns_exponent(power);
                return;
        }

        if (style < 6)
        {
                int place = style < 4 ? 1 : (int)ns_below((ns_u64)ns_count + 1);

                if (place > ns_count)
                        place = ns_count;

                for (index = 0; index < place; index++)
                        ns_put(ns_digit[index]);

                ns_put('.');

                for (; index < ns_count; index++)
                        ns_put(ns_digit[index]);

                power += ns_count - place;

                if (power != 0 || ns_below(4) == 0)
                        ns_exponent(power);
                return;
        }

        if (power >= 0 && power <= 40)
        {
                for (index = 0; index < ns_count; index++)
                        ns_put(ns_digit[index]);

                ns_zeros(power);

                if (ns_below(2))
                        ns_put('.');
                return;
        }

        if (power < 0 && power + ns_count > -40)
        {
                long long lead = power + ns_count;

                if (lead <= 0)
                {
                        ns_put('0');
                        ns_put('.');
                        ns_zeros(-lead);
                        index = 0;
                }
                else
                {
                        for (index = 0; index < lead; index++)
                                ns_put(ns_digit[index]);

                        ns_put('.');
                }

                for (; index < ns_count; index++)
                        ns_put(ns_digit[index]);
                return;
        }

        ns_put('0');
        ns_put('.');

        for (index = 0; index < ns_count; index++)
                ns_put(ns_digit[index]);

        ns_exponent(power + ns_count);
}

//      A big number in limbs of a billion, which is all the exact decimal of
//      a float needs: a multiply by a small factor and a spelling.
static void ns_big(ns_u64 value)
{
        ns_used = 0;

        while (value)
        {
                ns_limb[ns_used++] = (ns_u32)(value % 1000000000);
                value /= 1000000000;
        }
}

static void ns_scale(ns_u32 factor)
{
        ns_u64 carry = 0;
        int index;

        for (index = 0; index < ns_used; index++)
        {
                ns_u64 product = (ns_u64)ns_limb[index] * factor + carry;

                ns_limb[index] = (ns_u32)(product % 1000000000);
                carry = product / 1000000000;
        }

        while (carry && ns_used < NS_LIMBS)
        {
                ns_limb[ns_used++] = (ns_u32)(carry % 1000000000);
                carry /= 1000000000;
        }
}

static void ns_spell(void)
{
        char held[10];
        int count = 0;
        int index;
        ns_u32 top;

        ns_count = 0;

        if (ns_used == 0)
        {
                ns_digit[ns_count++] = '0';
                return;
        }

        for (top = ns_limb[ns_used - 1]; top; top /= 10)
                held[count++] = (char)('0' + top % 10);

        while (count)
                ns_digit[ns_count++] = held[--count];

        for (index = ns_used - 2; index >= 0; index--)
        {
                ns_u32 limb = ns_limb[index];
                int place;

                for (place = 8; place >= 0; place--)
                {
                        ns_digit[ns_count + place] = (char)('0' + limb % 10);
                        limb /= 10;
                }

                ns_count += 9;
        }
}

//      The exact decimal of significand times two to the power, into
//      ns_digit, answering the power of ten the digits stand at.
static long long ns_exact(ns_u64 significand, int power)
{
        ns_big(significand);

        if (power >= 0)
        {
                while (power > 0)
                {
                        int step = power > 29 ? 29 : power;

                        ns_scale((ns_u32)1 << step);
                        power -= step;
                }

                ns_spell();
                return 0;
        }

        {
                int left = -power;

                while (left > 0)
                {
                        int step = left > 12 ? 12 : left;
                        ns_u32 five = 1;
                        int count;

                        for (count = 0; count < step; count++)
                                five *= 5;

                        ns_scale(five);
                        left -= step;
                }
        }

        ns_spell();
        return power;
}

//      The variations every exact decimal is put through before spelling.
static void ns_variant(long long power)
{
        ns_u64 pick = ns_below(10);
        int index;

        if ((pick == 1 || pick == 2) && ns_count > 1)
        {
                int keep = 1 + (int)ns_below((ns_u64)ns_count - 1);

                power += ns_count - keep;
                ns_count = keep;

                if (pick == 2)
                {
                        index = ns_count - 1;

                        while (index >= 0 && ns_digit[index] == '9')
                        {
                                ns_digit[index] = '0';
                                index--;
                        }

                        if (index >= 0)
                                ns_digit[index]++;
                        else
                        {
                                for (index = ns_count; index > 0; index--)
                                        ns_digit[index] = ns_digit[index - 1];

                                ns_digit[0] = '1';
                                ns_count++;
                        }
                }
        }
        else if (pick == 3)
        {
                int nines = 1 + (int)ns_below(30);

                index = ns_count - 1;

                while (index >= 0 && ns_digit[index] == '0')
                {
                        ns_digit[index] = '9';
                        index--;
                }

                if (index >= 0)
                        ns_digit[index]--;

                while (nines-- > 0 && ns_count < NS_DIGITS)
                {
                        ns_digit[ns_count++] = '9';
                        power--;
                }
        }
        else if (pick == 4 || pick == 5)
        {
                int length = 700 + (int)ns_below(250);

                while (ns_count < length)
                {
                        ns_digit[ns_count++] = '0';
                        power--;
                }

                if (pick == 5)
                {
                        ns_digit[ns_count++] = '1';
                        power--;
                }
        }
        else if (pick == 6 && ns_count > 17)
        {
                power += ns_count - 17;
                ns_count = 17;
        }
        else if (pick == 7 && ns_count < NS_DIGITS)
        {
                ns_digit[ns_count++] = '1';
                power--;
        }

        ns_front();
        ns_render(power);
        ns_back();
}

//      A random double or float, a fifth of them subnormal and a tenth each
//      at the bottom and the top of the normal range.
static ns_u64 ns_float(int narrow, int *power)
{
        ns_u64 pick = ns_below(10);
        int places = narrow ? 23 : 52;
        int top = narrow ? 254 : 2046;
        int bias = narrow ? 150 : 1075;
        ns_u64 fraction = ns_next() & ((1ULL << places) - 1);
        int field;

        if (ns_below(8) == 0)
                fraction = ns_below(2) ? (1ULL << places) - 1 : ns_below(4);

        if (pick < 2)
                field = 0;
        else if (pick < 3)
                field = 1 + (int)ns_below(40);
        else if (pick < 4)
                field = top - (int)ns_below(40);
        else
                field = 1 + (int)ns_below((ns_u64)top);

        *power = field ? field - bias : 1 - bias;
        return field ? fraction | (1ULL << places) : fraction;
}

static void ns_midpoint(void)
{
        int power;
        ns_u64 significand = ns_float(ns_below(4) == 0, &power);

        ns_variant(ns_exact(2 * significand + 1, power - 1));
}

static void ns_exact_float(void)
{
        static const struct
        {
                ns_u64 significand;
                int power;
        } edges[] = {
                {1, -1074}, {0xfffffffffffffULL, -1074}, {1ULL << 52, -1074},
                {0x1fffffffffffffULL, 971}, {0x3fffffffffffffULL, -1076},
                {1, -1075}, {3, -1076}, {0x3fffffffffffffULL, 970},
                {1, -149}, {0x7fffffULL, -149}, {1ULL << 23, -149},
                {0xffffffULL, 104}, {0x1ffffffULL, -151}, {1, -150},
                {0x1ffffffULL, 103},
        };
        int power;
        ns_u64 significand;

        if (ns_below(3) == 0)
        {
                ns_u64 which = ns_below(sizeof edges / sizeof edges[0]);

                significand = edges[which].significand;
                power = edges[which].power;
        }
        else
                significand = ns_float(ns_below(4) == 0, &power);

        if (significand == 0)
                significand = 1;

        ns_variant(ns_exact(significand, power));
}

static void ns_decimal(void)
{
        ns_u64 pick = ns_below(20);
        int count = pick == 0 ? 100 + (int)ns_below(900)
                  : pick < 7 ? 1 + (int)ns_below(40)
                             : 1 + (int)ns_below(19);
        long long power;
        int index;

        for (index = 0; index < count; index++)
                ns_digit[index] = (char)('0' + ns_below(10));

        ns_count = count;

        if (ns_below(3) == 0)
                power = -(long long)ns_below((ns_u64)count + 1);
        else
        {
                //      One draw at a time: two in an expression are drawn in
                //      whichever order the compiler picks, and gcc plain and
                //      gcc with the undefined-behaviour checks picked two.
                power = (long long)ns_below(671) - 350;
                power -= ns_below(2) ? count : 0;
        }

        ns_front();
        ns_render(power);
        ns_back();
}

static void ns_window(void)
{
        ns_u64 pick = ns_below(5);
        ns_u64 value;
        char held[24];
        int count = 0;
        int index;

        if (pick == 0)
                value = 9007199254740992ULL - 4 + ns_below(9);
        else if (pick == 1)
        {
                value = ns_next();
                value >>= 11 + ns_below(53);
        }
        else if (pick == 2)
                value = ns_below(1000000);
        else if (pick == 3)
                value = 999999999999999ULL + ns_below(3);
        else
        {
                value = ns_next();
                value >>= ns_below(64);
        }

        do
        {
                held[count++] = (char)('0' + value % 10);
                value /= 10;
        } while (value);

        for (index = 0; index < count; index++)
                ns_digit[index] = held[count - 1 - index];

        ns_count = count;

        if (ns_below(6) == 0)
                for (index = (int)ns_below(4); index > 0; index--)
                        ns_digit[ns_count++] = '0';

        ns_front();
        {
                long long power = (long long)ns_below(51) - 25;

                power -= ns_below(2) ? ns_count : 0;
                ns_render(power);
        }
        ns_back();
}

static void ns_zero_runs(void)
{
        static const int runs[] = {0, 1, 2, 7, 19, 20, 40, 300, 831, 832, 833, 1200};
        int count = 1 + (int)ns_below(20);

        ns_front();
        ns_zeros(runs[ns_below(sizeof runs / sizeof runs[0])]);

        if (ns_below(2))
        {
                ns_put('.');
                ns_zeros(runs[ns_below(sizeof runs / sizeof runs[0])]);
        }

        ns_put((char)('1' + ns_below(9)));
        ns_zeros(runs[ns_below(8)]);

        while (count-- > 0)
                ns_put((char)('0' + ns_below(10)));

        if (ns_below(3) == 0)
                ns_zeros(runs[ns_below(sizeof runs / sizeof runs[0])]);

        if (ns_below(2))
                ns_exponent((long long)ns_below(800) - 400);

        ns_back();
}

static void ns_hexadecimal(void)
{
        static const char digits[] = "0123456789abcdefABCDEF";
        int count = (int)ns_below(20);
        int point = ns_below(3) == 0 ? -1 : (int)ns_below((ns_u64)count + 1);
        int index;

        ns_front();
        ns_put('0');
        ns_put(ns_below(2) ? 'x' : 'X');

        for (index = 0; index < count; index++)
        {
                if (index == point)
                        ns_put('.');

                ns_put(digits[ns_below(sizeof digits - 1)]);
        }

        if (point == count)
                ns_put('.');

        if (ns_below(4) != 0)
        {
                ns_put(ns_below(2) ? 'p' : 'P');

                if (ns_below(3) == 0)
                        ns_put(ns_below(2) ? '-' : '+');

                if (ns_below(8) != 0)
                        ns_number(ns_below(1200));
        }

        ns_back();
}

static void ns_malformed(void)
{
        static const char *pieces[] = {
                "", " ", "\t", "\v", "\f", "\r", "\n", "+", "-", ".", "..", "e",
                "E", "e+", "e-", "x", "X", "p", "p+", "0", "1", "9", "00", "5.",
                ".5", "-.", "+.", "1e", "0x", "0x.", "0x1", "inf", "INF",
                "infinity", "InFiNiTy", "infin", "in", "i", "nan", "NAN", "na",
                "n", "nan(", "nan()", "nan(12)", "nan(0x1f)", "nan(abc_9)", "(",
                ")", "_", "a"};
        int count = 1 + (int)ns_below(6);

        while (count-- > 0)
                ns_puts(pieces[ns_below(sizeof pieces / sizeof pieces[0])]);
}

/*
        The listed inputs, which block 0 takes before any drawn case: every
        string CHECK_number once pinned glibc 2.44's strtod and strtof answers
        for -- the boundaries, the subnormals, the classic hard cases, the
        hexadecimal corners and the malformed strings whose end pointer has
        to come back at the original text.
*/
static const char *const ns_listed[] = {
    "0", "-0", "0.0", "-0.0", "1", "-1", "1.5", "-1.5", "0.1", "-0.1", "0.2",
    "0.3", "3.14159265358979323846",
    "2.718281828459045235360287471352662497757", "1e0", "1e1", "1e-1", "1e10",
    "1e-10", "1e22", "1e23", "1e-22", "1e-23", "1e100", "1e-100", "1e300",
    "1e-300", "1e308", "1e-308", "1e309", "1e-309", "1e-320", "1e-323",
    "1e-324", "1e-325", "1e400", "1e-400", "4.9406564584124654e-324",
    "4.9406564584124655e-324", "2.4703282292062327e-324",
    "2.4703282292062328e-324", "2.2250738585072011e-308",
    "2.2250738585072012e-308", "2.2250738585072013e-308",
    "2.2250738585072014e-308", "1.7976931348623157e308",
    "1.7976931348623158e308", "1.7976931348623159e308", "9007199254740992",
    "9007199254740993", "9007199254740994", "9007199254740995",
    "18446744073709551615", "18446744073709551616",
    "340282366920938463463374607431768211456", "123456789012345678901234567890",
    "0.000000000000000000000000000001",
    "1234567890123456789012345678901234567890e-40", "7.8459735791271921e65",
    "3.571e266", "3.08984926168550152811e-32", "8.98846567431158e307",
    "1.00000000000000005",
    "0.5000000000000000166533453693773481063544750213623046875",
    "3.518437208883201171875e13", "62.5364939768271845828", "8.10109172351e-33",
    "1.50000000000000011102230246251565404236316680908203125",
    "9007199254740991.4999999999999999999999999999999999", "5e-324",
    "7.4109846876186981626485318930233205854758970392148714663837852375101326090531312779794975454245398856969484704316857659638998506553390969459816219401617281718945106978546710679176872575177347315553307795408549809608457500958111373034747658096871009590975442271004757307809711118935784838675653998783503015228055934046593739791790738723868299395818481660169122019456499931289798411362062484498678713572180352209017023903285791732520220528974020802906854021606612375549983402671300035812486479041385743401875520901590172592547146296175134159774938718574737870961645638908718119841271673056017045493004705269590165763776884908267986972573366521765567941072508764337560846003984904972149117463085539556354188641513168478436313080237596295773983001708984374999e-324",
    "5e-324", "inf", "-inf", "INFINITY", "-INFINITY", "InF", "infi", "nan",
    "-nan", "NaN", "nan(1)", "nan(0x7)", "nan()", "nan(zz)", "0x1p0", "0x1p1",
    "-0x1p-1", "0x1.8p3", "0x1.fffffffffffffp1023", "0x1.fffffffffffff8p0",
    "0x1.0000000000001p0", "0x1p-1074", "0x1p-1075", "0x1p-1076", "0x1p1024",
    "0x0p0", "-0x0p0", "0x.8p1", "0x8.p-1", "0X1P+2", "0xabcdefp-20",
    "0x1.00000000000008p0", "0x1.00000000000018p0", "0x435p-1073", "0x", "0X",
    "0xp3", "0x.p1", "0x1", "0x1.8", "", " ", "+", "-", ".", "e5", ".e3",
    "+.e-3", "1e", "1e+", "1e-", "1.5e", "1.5e+x", "  \t\n\r\f\v12.5xyz", "--1",
    "+-1", ".5", "5.", "+.5", "-.5", "00000000000000000000000000000000000001",
    "0.00000000000000000000000000000000000001",
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001",
    "1.000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001",
    "1e1000000000", "1e-1000000000", "1e2147483647", "1e-2147483647",
    "1e99999999999999999999", "1e-99999999999999999999",
    "2.2250738585072012e-308", "1e-45", "1.4e-45", "3.4028235e38",
    "3.4028236e38", "1.1754944e-38", "1.1754942e-38", "5.877472e-39",
    "16777217", "16777216", "33554433", "2.49113510e7", "3.99578460e7",
    "1.19209289550781250e-7",
};

#define NS_LISTED (sizeof ns_listed / sizeof ns_listed[0])

//      One case into ns_text.
static void ns_case(void)
{
        ns_u64 family = ns_below(100);

        ns_at = 0;

        if (family < 22)
                ns_decimal();
        else if (family < 36)
                ns_window();
        else if (family < 58)
                ns_midpoint();
        else if (family < 70)
                ns_exact_float();
        else if (family < 80)
                ns_zero_runs();
        else if (family < 90)
                ns_hexadecimal();
        else
                ns_malformed();

        ns_text[ns_at] = 0;
}

//      Each block starts from its own index, so it can run alone.
static void ns_seed(ns_u64 block)
{
        ns_state = 0x6a09e667f3bcc909ULL ^ (block * 0xbb67ae8584caa73bULL);
}

static ns_u64 ns_parse(const char *piece)
{
        ns_u64 value = 0;

        while (*piece >= '0' && *piece <= '9')
                value = value * 10 + (ns_u64)(*piece++ - '0');

        return value;
}

#ifdef NS_STRTOD
static int ns_hex(char *into, ns_u64 value, int places)
{
        int place;

        for (place = 0; place < places; place++)
                into[place] = "0123456789abcdef"[(value >> ((places - 1 - place) * 4)) & 15];

        return places;
}

static int ns_decimal_into(char *into, ns_u64 value)
{
        char held[24];
        int count = 0;
        int at = 0;

        do
        {
                held[count++] = (char)('0' + value % 10);
                value /= 10;
        } while (value);

        while (count)
                into[at++] = held[--count];

        return at;
}

static ns_u64 ns_fold(ns_u64 hash, ns_u64 value)
{
        return (hash ^ value) * 0x100000001b3ULL;
}

static ns_u64 ns_block(ns_u64 block, int verbose)
{
        ns_u64 hash = 0xcbf29ce484222325ULL;
        ns_u64 index;

        ns_seed(block);

        for (index = 0; index < NS_BLOCK; index++)
        {
                union
                {
                        double value;
                        ns_u64 bits;
                } wide;
                union
                {
                        float value;
                        ns_u32 bits;
                } narrow;
                char *stop_at;
                ns_u64 wide_end;
                ns_u64 narrow_end;
                ns_u64 wide_errno;
                ns_u64 narrow_errno;

                if (block == 0 && index < NS_LISTED)
                {
                        ns_at = 0;
                        ns_puts(ns_listed[index]);
                        ns_text[ns_at] = 0;
                }
                else
                {
                        ns_case();
                }

                NS_ERRNO = 0;
                wide.bits = 0;
                wide.value = NS_STRTOD(ns_text, &stop_at);
                wide_errno = (ns_u64)NS_ERRNO;
                wide_end = (ns_u64)(stop_at - ns_text);

                NS_ERRNO = 0;
                narrow.value = NS_STRTOF(ns_text, &stop_at);
                narrow_errno = (ns_u64)NS_ERRNO;
                narrow_end = (ns_u64)(stop_at - ns_text);

                hash = ns_fold(hash, wide.bits);
                hash = ns_fold(hash, wide_end << 16 | wide_errno);
                hash = ns_fold(hash, (ns_u64)narrow.bits << 32 |
                                             narrow_end << 16 | narrow_errno);

                if (verbose)
                {
                        char line[512];
                        int at = 0;
                        int read;

                        at += ns_decimal_into(line + at, index);
                        line[at++] = ' ';
                        at += ns_hex(line + at, wide.bits, 16);
                        line[at++] = ' ';
                        at += ns_decimal_into(line + at, wide_end);
                        line[at++] = ' ';
                        at += ns_decimal_into(line + at, wide_errno);
                        line[at++] = ' ';
                        at += ns_hex(line + at, narrow.bits, 8);
                        line[at++] = ' ';
                        at += ns_decimal_into(line + at, narrow_end);
                        line[at++] = ' ';
                        at += ns_decimal_into(line + at, narrow_errno);
                        line[at++] = ' ';
                        line[at++] = '[';

                        for (read = 0; ns_text[read] && read < 96; read++)
                        {
                                char byte = ns_text[read];

                                if (byte > ' ' && byte < 127 && byte != '\\')
                                        line[at++] = byte;
                                else
                                {
                                        line[at++] = '\\';
                                        at += ns_hex(line + at, (ns_u64)(unsigned char)byte, 2);
                                }
                        }

                        if (ns_text[read])
                        {
                                line[at++] = '.';
                                line[at++] = '.';
                                at += ns_decimal_into(line + at, (ns_u64)ns_at);
                        }

                        line[at++] = ']';
                        line[at] = 0;
                        ns_emit(line);
                }
        }

        return hash;
}

//      Blocks first through first + count - 1, one hash line each; or, when
//      verbose names a block, every case of that one block and its hash.
static void ns_run(ns_u64 first, ns_u64 count, ns_u64 verbose, int chosen)
{
        ns_u64 block;

        for (block = first; block < first + count; block++)
        {
                char line[64];
                int at = 0;
                ns_u64 hash;

                if (chosen && block != verbose)
                        continue;

                hash = ns_block(block, chosen);

                line[at++] = 'b';
                line[at++] = 'l';
                line[at++] = 'o';
                line[at++] = 'c';
                line[at++] = 'k';
                line[at++] = ' ';
                at += ns_decimal_into(line + at, block);
                line[at++] = ' ';
                at += ns_hex(line + at, hash, 16);
                line[at] = 0;
                ns_emit(line);
        }
}
#endif

#elif defined(SHARED_error_body)
/*
        The transcript CHECK_error and CHECK_error_reference both print.

        One body, so the two cannot drift: it is written against the names
        the library gives errno and the POSIX wrappers, and each half says
        what those names are -- the library's own under CHECK_error, glibc's
        under CHECK_error_reference -- and how a line and a flush are made:
        error_trace takes the library's format (%b a bipolar, %p a positive,
        %s a string), error_flush is the flush perror's ordering needs, and
        error_sync is sync as an answer. Everything printed with the
        TRANSCRIPT prefix is the contract and must not be reordered.

        error_test_transcript is the whole of it, in the order both print it.
*/
#define ERROR_TEST_DIRECTORY "/tmp/dawning-error-test"
#define ERROR_TEST_FILE "/tmp/dawning-error-test/one"
#define ERROR_TEST_LINK "/tmp/dawning-error-test/link"
#define ERROR_TEST_ABSENT "/tmp/dawning-error-test/absent"

/*
        A buffer shown byte for byte, so a terminator that is or is not there
        is visible in the diff.

        The buffer is filled with '@' before the call and this prints two
        bytes past what was asked for, so a routine that wrote one byte too
        many shows an overwritten '@' rather than nothing at all. A zero byte
        prints as '|', which is what glibc's side prints for it too.
*/
static fn error_test_shown(p8 address_to into, p8 address_to buffer,
                           positive count)
{
        positive at;

        for (at = 0; at < count; at++)
                into[at] = buffer[at] == 0 ? '|' : buffer[at];

        into[count] = end;
}

static fn error_test_strerror_r_line(b32 number, positive size)
{
        p8 buffer[128];
        p8 shown[136];
        b32 answer;

        memory_fill(buffer, '@', sizeof buffer);
        answer = strerror_r(number, buffer, size);
        error_test_shown(shown, buffer, size + 2 < 128 ? size + 2 : 128);

        error_trace("TRANSCRIPT strerror_r %b %p rc=%b [%s]\n",
                      (bipolar)number, size, (bipolar)answer, shown);
}

static fn error_test_messages(void)
{
        b32 number;

        for (number = 0; number <= 140; number++)
                error_trace("TRANSCRIPT strerror %b [%s]\n",
                              (bipolar)number, strerror(number));

        error_trace("TRANSCRIPT strerror %b [%s]\n", (bipolar)-1,
                      strerror(-1));
        error_trace("TRANSCRIPT strerror %b [%s]\n", (bipolar)-7,
                      strerror(-7));
        error_trace("TRANSCRIPT strerror %b [%s]\n", (bipolar)1000,
                      strerror(1000));

        //      Every code the table knows and a few either side of it, at
        //      every size from nothing to past the longest message: the
        //      bytes written, the terminator, what is left alone, and the
        //      answer -- EINVAL beating ERANGE in the holes of the numbering.
        for (number = -8; number <= 141; number++)
                for (positive size = 0; size <= 64; size++)
                        error_test_strerror_r_line(number == 141 ? 1000 : number, size);
}

static fn error_test_perror(void)
{
        error_flush();
        errno = ENOENT;
        perror((string_address) "TRANSCRIPT perror named");

        error_flush();
        errno = ENOENT;
        perror((string_address) "");

        error_flush();
        errno = ENOENT;
        perror(null);

        error_flush();
        errno = 41;
        perror((string_address) "TRANSCRIPT perror unknown");

        error_flush();
        errno = 0;
        perror((string_address) "TRANSCRIPT perror success");
}

//      Result and errno on one line, which is the whole of what a wrapper
//      promises and the whole of what the reference prints.
static fn error_test_said(string_address what, bipolar result)
{
        error_trace("TRANSCRIPT call %s -> %b errno %b\n", what, result,
                      (bipolar)errno);
}

static fn error_test_wrappers(void)
{
        p8 room[64];
        p8 small[4];
        b32 pair[2];
        b32 handle;
        error_stat one;
        bipolar got;

        //      A clean slate, without reporting whether the cleaning worked:
        //      a previous run may have left nothing behind.
        unlink((string_address)ERROR_TEST_LINK);
        unlink((string_address)ERROR_TEST_FILE);
        rmdir((string_address)ERROR_TEST_DIRECTORY);

        errno = 0;
        error_test_said((string_address) "mkdir",
                        mkdir((string_address)ERROR_TEST_DIRECTORY, 0755));

        errno = 0;
        error_test_said((string_address) "mkdir-again",
                        mkdir((string_address)ERROR_TEST_DIRECTORY, 0755));

        errno = 0;
        handle = open((string_address)ERROR_TEST_FILE,
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        error_test_said((string_address) "open-create", (bipolar)(handle >= 0));

        errno = 0;
        error_test_said((string_address) "write",
                        write(handle, (address_any) "abcdefgh", 8));

        errno = 0;
        error_test_said((string_address) "lseek-end",
                        lseek(handle, 0, SEEK_END));

        errno = 0;
        error_test_said((string_address) "read-on-write-only",
                        read(handle, room, 4));

        errno = 0;
        error_test_said((string_address) "close", close(handle));

        errno = 0;
        error_test_said((string_address) "close-twice", close(handle));

        errno = 0;
        error_test_said((string_address) "open-missing",
                        open((string_address)ERROR_TEST_ABSENT, O_RDONLY));

        errno = 0;
        error_test_said((string_address) "open-exclusive-existing",
                        open((string_address)ERROR_TEST_FILE,
                             O_WRONLY | O_CREAT | O_EXCL, 0644));

        errno = 0;
        error_test_said((string_address) "open-directory-for-write",
                        open((string_address)ERROR_TEST_DIRECTORY,
                             O_WRONLY));

        errno = 0;
        error_test_said((string_address) "stat",
                        stat((string_address)ERROR_TEST_FILE, &one));

        error_trace("TRANSCRIPT stat size %b regular %b directory %b\n",
                      (bipolar)one.st_size,
                      (bipolar)(S_ISREG(one.st_mode) ? 1 : 0),
                      (bipolar)(S_ISDIR(one.st_mode) ? 1 : 0));

        error_trace("TRANSCRIPT stat permission %p links %b\n",
                      (positive)(one.st_mode & 07777),
                      (bipolar)one.st_nlink);

        errno = 0;
        error_test_said((string_address) "stat-missing",
                        stat((string_address)ERROR_TEST_ABSENT, &one));

        errno = 0;
        error_test_said((string_address) "stat-directory",
                        stat((string_address)ERROR_TEST_DIRECTORY, &one));

        error_trace("TRANSCRIPT stat directory-bit %b\n",
                      (bipolar)(S_ISDIR(one.st_mode) ? 1 : 0));

        errno = 0;
        error_test_said((string_address) "symlink",
                        symlink((string_address) "one",
                                (string_address)ERROR_TEST_LINK));

        errno = 0;
        memory_fill(room, '@', sizeof room);
        got = readlink((string_address)ERROR_TEST_LINK, room, sizeof room);
        error_test_said((string_address) "readlink", got);

        errno = 0;
        error_test_said((string_address) "readlink-not-a-link",
                        readlink((string_address)ERROR_TEST_FILE, room,
                                 sizeof room));

        errno = 0;
        error_test_said((string_address) "lstat-is-link",
                        lstat((string_address)ERROR_TEST_LINK, &one));

        error_trace("TRANSCRIPT lstat link-bit %b\n",
                      (bipolar)(S_ISLNK(one.st_mode) ? 1 : 0));

        errno = 0;
        error_test_said((string_address) "access-existing",
                        access((string_address)ERROR_TEST_FILE, F_OK));

        errno = 0;
        error_test_said((string_address) "access-missing",
                        access((string_address)ERROR_TEST_ABSENT, F_OK));

        errno = 0;
        error_test_said((string_address) "rmdir-not-empty",
                        rmdir((string_address)ERROR_TEST_DIRECTORY));

        errno = 0;
        error_test_said((string_address) "unlink-missing",
                        unlink((string_address)ERROR_TEST_ABSENT));

        errno = 0;
        error_test_said((string_address) "rename",
                        rename((string_address)ERROR_TEST_FILE,
                               (string_address)ERROR_TEST_ABSENT));

        errno = 0;
        error_test_said((string_address) "rename-back",
                        rename((string_address)ERROR_TEST_ABSENT,
                               (string_address)ERROR_TEST_FILE));

        errno = 0;
        error_test_said((string_address) "rename-missing",
                        rename((string_address)ERROR_TEST_ABSENT,
                               (string_address)ERROR_TEST_LINK));

        errno = 0;
        error_test_said((string_address) "pipe", pipe(pair));

        errno = 0;
        error_test_said((string_address) "lseek-on-pipe",
                        lseek(pair[0], 0, SEEK_SET));

        errno = 0;
        error_test_said((string_address) "isatty-on-pipe",
                        (bipolar)isatty(pair[0]));

        errno = 0;
        error_test_said((string_address) "isatty-bad-handle",
                        (bipolar)isatty(999));

        errno = 0;
        error_test_said((string_address) "dup2-onto-self",
                        dup2(pair[0], pair[0]));

        errno = 0;
        error_test_said((string_address) "dup2-closed-onto-self",
                        dup2(900, 900));

        errno = 0;
        error_test_said((string_address) "close-pipe-read", close(pair[0]));

        errno = 0;
        error_test_said((string_address) "close-pipe-write", close(pair[1]));

        errno = 0;
        error_test_said((string_address) "write-bad-handle",
                        write(999, (address_any) "x", 1));

        errno = 0;
        error_test_said((string_address) "getcwd-too-small",
                        (bipolar)(getcwd(small, sizeof small) == null));

        errno = 0;
        error_test_said((string_address) "getpid-positive",
                        (bipolar)(getpid() > 0));

        errno = 0;
        error_test_said((string_address) "chmod",
                        chmod((string_address)ERROR_TEST_FILE, 0600));

        errno = 0;
        error_test_said((string_address) "stat-after-chmod",
                        stat((string_address)ERROR_TEST_FILE, &one));

        error_trace("TRANSCRIPT stat permission %p\n",
                      (positive)(one.st_mode & 07777));

        errno = 0;
        error_test_said((string_address) "chmod-missing",
                        chmod((string_address)ERROR_TEST_ABSENT, 0600));

        errno = 0;
        error_test_said((string_address) "unlink-link",
                        unlink((string_address)ERROR_TEST_LINK));

        errno = 0;
        error_test_said((string_address) "unlink",
                        unlink((string_address)ERROR_TEST_FILE));

        errno = 0;
        error_test_said((string_address) "rmdir",
                        rmdir((string_address)ERROR_TEST_DIRECTORY));
}


#define ERROR_MORE_DIRECTORY "/tmp/dawning-error-more"
#define ERROR_MORE_FILE "/tmp/dawning-error-more/two"
#define ERROR_MORE_ABSENT "/tmp/dawning-error-more/absent/deeper"

/*
        The wrappers the first walk never reached, each on the path that
        proves its arguments went to the kernel in the right order.

        An error path is the cheap way to do that and not a weak one. A
        transposed argument in a five argument call does not fail to compile
        and does not fail to run -- it produces a different errno, and a
        differential against glibc reads that errno. fchownat with its path
        and its owner swapped answers EFAULT where the right order answers
        ENOENT; an *at call handed a descriptor where the flags belong answers
        EBADF where the right order answers ENOENT. Both show up here as a
        line that differs.

        A descriptor of -1 is the shape for everything that takes one, a path
        under a directory that does not exist is the shape for everything that
        takes a path, and -1 as the directory of an *at call with a relative
        name is the shape that pins which argument is the directory.

        fork, waitpid and _exit get a real round trip instead, because a wrong
        clone flag word does not come back as an error -- it comes back as a
        child that is a thread sharing this stack, and the way that shows is
        that the parent never sees SIGCHLD and never reaps anything.
*/
static fn error_test_more(void)
{
        p8 room[64];
        b32 pair[2];
        b32 handle;
        b32 copy;
        b32 status;
        b32 child;
        p32 mask;
        error_stat one;
        struct stat tagged;
        address_any region;

        rmdir((string_address)ERROR_MORE_DIRECTORY);

        errno = 0;
        error_test_said((string_address) "more-mkdir",
                        mkdir((string_address)ERROR_MORE_DIRECTORY, 0755));

        errno = 0;
        handle = creat((string_address)ERROR_MORE_FILE, 0644);
        error_test_said((string_address) "creat", (bipolar)(handle >= 0));

        errno = 0;
        error_test_said((string_address) "creat-missing-directory",
                        creat((string_address)ERROR_MORE_ABSENT, 0644));

        errno = 0;
        error_test_said((string_address) "pwrite",
                        pwrite(handle, (address_any) "abcdefgh", 8, 0));

        errno = 0;
        memory_fill(room, '@', sizeof room);
        error_test_said((string_address) "pread",
                        pread(handle, room, 4, 2));

        room[4] = end;
        error_trace("TRANSCRIPT pread saw [%s]\n", room);

        errno = 0;
        error_test_said((string_address) "pread-bad-handle",
                        pread(-1, room, 4, 0));

        errno = 0;
        error_test_said((string_address) "pwrite-bad-handle",
                        pwrite(-1, (address_any) "x", 1, 0));

        errno = 0;
        error_test_said((string_address) "fstat", fstat(handle, &one));

        error_trace("TRANSCRIPT fstat size %b regular %b\n",
                      (bipolar)one.st_size,
                      (bipolar)(S_ISREG(one.st_mode) ? 1 : 0));

        //      The tag and the typedef are meant to be the same type, and
        //      nothing else in this file writes the tag, so it is written
        //      once here or the promise is never compiled.
        errno = 0;
        error_test_said((string_address) "fstat-through-the-tag",
                        fstat(handle, &tagged));

        error_trace("TRANSCRIPT tagged size %b\n",
                      (bipolar)tagged.st_size);

        errno = 0;
        error_test_said((string_address) "fstat-bad-handle", fstat(-1, &one));

        errno = 0;
        error_test_said((string_address) "fchmod", fchmod(handle, 0600));

        errno = 0;
        error_test_said((string_address) "fchmod-bad-handle",
                        fchmod(-1, 0600));

        errno = 0;
        error_test_said((string_address) "fchown-no-change",
                        fchown(handle, (p32)-1, (p32)-1));

        errno = 0;
        error_test_said((string_address) "fchown-bad-handle",
                        fchown(-1, (p32)-1, (p32)-1));

        errno = 0;
        error_test_said((string_address) "ftruncate", ftruncate(handle, 4));

        errno = 0;
        error_test_said((string_address) "ftruncate-bad-handle",
                        ftruncate(-1, 4));

        errno = 0;
        error_test_said((string_address) "fsync", fsync(handle));

        errno = 0;
        error_test_said((string_address) "fsync-bad-handle", fsync(-1));

        errno = 0;
        error_test_said((string_address) "fdatasync", fdatasync(handle));

        errno = 0;
        error_test_said((string_address) "fdatasync-bad-handle",
                        fdatasync(-1));

        errno = 0;
        error_test_said((string_address) "isatty-on-file",
                        (bipolar)isatty(handle));

        errno = 0;
        error_test_said((string_address) "fcntl-getfd",
                        fcntl(handle, 1, (positive)0));

        errno = 0;
        error_test_said((string_address) "fcntl-bad-handle",
                        fcntl(-1, 1, (positive)0));

        errno = 0;
        error_test_said((string_address) "ioctl-bad-handle",
                        ioctl(-1, 0x5401, (positive)room));

        errno = 0;
        error_test_said((string_address) "close-more", close(handle));

        //      Truncate by name, which is the one path-taking size call.
        errno = 0;
        error_test_said((string_address) "truncate",
                        truncate((string_address)ERROR_MORE_FILE, 2));

        errno = 0;
        error_test_said((string_address) "truncate-missing",
                        truncate((string_address)ERROR_MORE_ABSENT, 0));

        //      The *at forms, first with the working directory and then with
        //      a descriptor that is not one, which is what says that the
        //      first argument is the directory and not something else.
        errno = 0;
        handle = openat(AT_FDCWD, (string_address)ERROR_MORE_FILE, O_RDONLY);
        error_test_said((string_address) "openat", (bipolar)(handle >= 0));

        errno = 0;
        error_test_said((string_address) "close-openat", close(handle));

        errno = 0;
        error_test_said((string_address) "openat-bad-directory",
                        openat(-1, (string_address) "relative-name",
                               O_RDONLY));

        errno = 0;
        error_test_said((string_address) "fstatat",
                        fstatat(AT_FDCWD, (string_address)ERROR_MORE_FILE,
                                &one, 0));

        errno = 0;
        error_test_said((string_address) "fstatat-bad-directory",
                        fstatat(-1, (string_address) "relative-name", &one,
                                0));

        errno = 0;
        error_test_said((string_address) "faccessat",
                        faccessat(AT_FDCWD, (string_address)ERROR_MORE_FILE,
                                  F_OK, 0));

        errno = 0;
        error_test_said((string_address) "faccessat-bad-directory",
                        faccessat(-1, (string_address) "relative-name", F_OK,
                                  0));

        errno = 0;
        error_test_said((string_address) "mkdirat-bad-directory",
                        mkdirat(-1, (string_address) "relative-name", 0755));

        errno = 0;
        error_test_said((string_address) "unlinkat-bad-directory",
                        unlinkat(-1, (string_address) "relative-name", 0));

        errno = 0;
        error_test_said((string_address) "readlinkat-bad-directory",
                        readlinkat(-1, (string_address) "relative-name", room,
                                   sizeof room));

        errno = 0;
        error_test_said((string_address) "renameat-missing",
                        renameat2(AT_FDCWD,
                                  (string_address)ERROR_MORE_ABSENT,
                                  AT_FDCWD,
                                  (string_address)ERROR_MORE_ABSENT, 0));

        //      link and the two ownership calls by name, whose only reachable
        //      error without being root is a path that is not there.
        errno = 0;
        error_test_said((string_address) "link-missing",
                        link((string_address)ERROR_MORE_ABSENT,
                             (string_address)ERROR_MORE_ABSENT));

        errno = 0;
        error_test_said((string_address) "link",
                        link((string_address)ERROR_MORE_FILE,
                             (string_address) "/tmp/dawning-error-more/three"));

        errno = 0;
        error_test_said((string_address) "unlink-link-two",
                        unlink((string_address) "/tmp/dawning-error-more/three"));

        errno = 0;
        error_test_said((string_address) "chown-no-change",
                        chown((string_address)ERROR_MORE_FILE, (p32)-1,
                              (p32)-1));

        errno = 0;
        error_test_said((string_address) "chown-missing",
                        chown((string_address)ERROR_MORE_ABSENT, (p32)-1,
                              (p32)-1));

        errno = 0;
        error_test_said((string_address) "lchown-no-change",
                        lchown((string_address)ERROR_MORE_FILE, (p32)-1,
                               (p32)-1));

        errno = 0;
        error_test_said((string_address) "lchown-missing",
                        lchown((string_address)ERROR_MORE_ABSENT, (p32)-1,
                               (p32)-1));

        //      Descriptors: dup, dup3 and pipe2, and the equal-handle case
        //      dup2 has to keep away from dup3.
        errno = 0;
        copy = dup(1);
        error_test_said((string_address) "dup", (bipolar)(copy >= 3));

        errno = 0;
        error_test_said((string_address) "close-dup", close(copy));

        errno = 0;
        error_test_said((string_address) "dup-bad-handle", dup(-1));

        errno = 0;
        error_test_said((string_address) "dup3", dup3(1, 7, 0));

        errno = 0;
        error_test_said((string_address) "close-dup3", close(7));

        errno = 0;
        error_test_said((string_address) "dup3-bad-handle", dup3(-1, 7, 0));

        errno = 0;
        error_test_said((string_address) "pipe2", pipe2(pair, 0));

        errno = 0;
        error_test_said((string_address) "close-pipe2-read", close(pair[0]));

        errno = 0;
        error_test_said((string_address) "close-pipe2-write", close(pair[1]));

        //      chdir and fchdir, put back immediately so nothing after this
        //      depends on where it is standing.
        errno = 0;
        error_test_said((string_address) "chdir",
                        chdir((string_address)ERROR_MORE_DIRECTORY));

        errno = 0;
        error_test_said((string_address) "chdir-back",
                        chdir((string_address) "/tmp"));

        errno = 0;
        error_test_said((string_address) "chdir-missing",
                        chdir((string_address)ERROR_MORE_ABSENT));

        errno = 0;
        error_test_said((string_address) "fchdir-bad-handle", fchdir(-1));

        //      umask, which cannot fail and is checked by what it gives back.
        umask(0022);
        mask = umask(0077);
        error_trace("TRANSCRIPT umask previous %p\n", (positive)mask);
        umask(mask);

        //      The mapping calls, whose failure is a pointer and not a minus
        //      one, so they are the only users of the third translation.
        errno = 0;
        region = mmap(null, 4096, FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                      FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS, -1, 0);
        error_test_said((string_address) "mmap",
                        (bipolar)(region != address_bad));

        //      Written to before it is unmapped, because a mapping that is
        //      not really there fails here rather than in the munmap.
        memory_fill(region, 0x5a, 4096);

        errno = 0;
        error_test_said((string_address) "mprotect",
                        mprotect(region, 4096, FILE_PROTECT_READ));

        errno = 0;
        error_test_said((string_address) "munmap", munmap(region, 4096));

        errno = 0;
        error_test_said((string_address) "mmap-zero-length",
                        (bipolar)(mmap(null, 0, FILE_PROTECT_READ,
                                       FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS,
                                       -1, 0) == address_bad));

        errno = 0;
        error_test_said((string_address) "munmap-zero-length",
                        munmap(null, 0));

        //      The identity calls, which cannot fail and are compared as
        //      values because both programs run as the same user.
        error_trace("TRANSCRIPT identity ppid-positive %b uid-is-euid %b"
                           " gid-is-egid %b\n",
                      (bipolar)(getppid() > 0),
                      (bipolar)(getuid() == geteuid()),
                      (bipolar)(getgid() == getegid()));

        errno = 0;
        error_test_said((string_address) "kill-probe-self",
                        kill(getpid(), 0));

        errno = 0;
        error_test_said((string_address) "kill-bad-signal", kill(getpid(),
                                                                9999));

        errno = 0;
        error_test_said((string_address) "execve-missing",
                        execve((string_address) "/nonexistent-dawning-execve",
                               null, null));

        errno = 0;
        error_test_said((string_address) "sync", error_sync());

        /*
                A child, and the three calls that only mean anything together.

                The log is flushed first because the child inherits whatever
                is still in the buffer, and although _exit does not flush it,
                a child that ever did would print this run's output twice.
        */
        error_flush();

        errno = 0;
        child = fork();

        if (child == 0)
                _exit(7);

        status = 0;
        errno = 0;
        error_test_said((string_address) "fork-then-waitpid",
                        (bipolar)(waitpid(child, &status, 0) == child));

        error_trace("TRANSCRIPT child status %b\n", (bipolar)status);

        error_flush();

        errno = 0;
        child = fork();

        if (child == 0)
                _Exit(3);

        status = 0;
        errno = 0;
        error_test_said((string_address) "fork-then-wait",
                        (bipolar)(wait(&status) == child));

        error_trace("TRANSCRIPT second child status %b\n",
                      (bipolar)status);

        errno = 0;
        error_test_said((string_address) "waitpid-no-children",
                        waitpid(-1, &status, 0));

        errno = 0;
        error_test_said((string_address) "wait4-no-children",
                        wait4(-1, &status, 0, null));

        errno = 0;
        error_test_said((string_address) "more-unlink",
                        unlink((string_address)ERROR_MORE_FILE));

        errno = 0;
        error_test_said((string_address) "more-rmdir",
                        rmdir((string_address)ERROR_MORE_DIRECTORY));
}

//      Both halves of the transcript have to arrive on one descriptor in one
//      order, and perror writes to two.
static fn error_test_transcript(void)
{
        dup2(1, 2);

        error_test_messages();
        error_test_perror();
        error_test_wrappers();
        error_test_more();
}

#elif defined(SHARED_hash_reference)
/*
        The hash block cores as the textbooks write them, with none of the
        assembly's tricks: no renamed registers, no carried terms, no schedule
        ring, no extension instructions. CHECK_checksum_crc checks the cores
        against these and BENCH_hardware_floor times the cores against them, so the C
        floor a hash is beaten by is the C floor it was proved against.
*/
static p32 hash_reference_rol(p32 x, positive s)
{
        return (x << s) | (x >> ((32 - s) & 31));
}

static p64 hash_reference_ror(p64 x, positive s)
{
        return (x >> s) | (x << ((64 - s) & 63));
}

static p32 hash_reference_le32(const p8 address_to p)
{
        return (p32)p[0] | (p32)p[1] << 8 | (p32)p[2] << 16 | (p32)p[3] << 24;
}

static p32 hash_reference_be32(const p8 address_to p)
{
        return (p32)p[0] << 24 | (p32)p[1] << 16 | (p32)p[2] << 8 | (p32)p[3];
}

static fn hash_reference_md5(p32 address_to st, const p8 address_to d, positive n)
{
        static const p8 shift[4][4] = {{7, 12, 17, 22}, {5, 9, 14, 20},
                                       {4, 11, 16, 23}, {6, 10, 15, 21}};
        static const p32 k[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
        };

        for (; n; n--, d += 64)
        {
                p32 x[16];
                for (positive i = 0; i < 16; i++)
                        x[i] = hash_reference_le32(d + 4 * i);
                p32 a = st[0], b = st[1], c = st[2], e = st[3];
                for (positive i = 0; i < 64; i++)
                {
                        p32 f;
                        positive g;
                        if (i < 16) { f = (b & c) | (~b & e); g = i; }
                        else if (i < 32) { f = (e & b) | (~e & c); g = (5 * i + 1) & 15; }
                        else if (i < 48) { f = b ^ c ^ e; g = (3 * i + 5) & 15; }
                        else { f = c ^ (b | ~e); g = (7 * i) & 15; }
                        p32 t = e;
                        e = c;
                        c = b;
                        b = b + hash_reference_rol(a + f + k[i] + x[g], shift[i / 16][i % 4]);
                        a = t;
                }
                st[0] += a; st[1] += b; st[2] += c; st[3] += e;
        }
}

static fn hash_reference_sha1(p32 address_to st, const p8 address_to d, positive n)
{
        for (; n; n--, d += 64)
        {
                p32 w[80];
                for (positive i = 0; i < 16; i++)
                        w[i] = hash_reference_be32(d + 4 * i);
                for (positive i = 16; i < 80; i++)
                        w[i] = hash_reference_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                p32 a = st[0], b = st[1], c = st[2], e = st[3], h = st[4];
                for (positive i = 0; i < 80; i++)
                {
                        p32 f, k;
                        if (i < 20) { f = (b & c) | (~b & e); k = 0x5a827999; }
                        else if (i < 40) { f = b ^ c ^ e; k = 0x6ed9eba1; }
                        else if (i < 60) { f = (b & c) | (b & e) | (c & e); k = 0x8f1bbcdc; }
                        else { f = b ^ c ^ e; k = 0xca62c1d6; }
                        p32 t = hash_reference_rol(a, 5) + f + h + k + w[i];
                        h = e;
                        e = c;
                        c = hash_reference_rol(b, 30);
                        b = a;
                        a = t;
                }
                st[0] += a; st[1] += b; st[2] += c; st[3] += e; st[4] += h;
        }
}

static fn hash_reference_sha256(p32 address_to st, const p8 address_to d, positive n)
{
        static const p32 k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };

        for (; n; n--, d += 64)
        {
                p32 w[64];
                for (positive i = 0; i < 16; i++)
                        w[i] = hash_reference_be32(d + 4 * i);
                for (positive i = 16; i < 64; i++)
                        w[i] = w[i - 16] + w[i - 7] +
                               (hash_reference_rol(w[i - 15], 25) ^ hash_reference_rol(w[i - 15], 14) ^ (w[i - 15] >> 3)) +
                               (hash_reference_rol(w[i - 2], 15) ^ hash_reference_rol(w[i - 2], 13) ^ (w[i - 2] >> 10));
                p32 v[8];
                for (positive i = 0; i < 8; i++)
                        v[i] = st[i];
                for (positive i = 0; i < 64; i++)
                {
                        p32 t1 = v[7] + (hash_reference_rol(v[4], 26) ^ hash_reference_rol(v[4], 21) ^
                                         hash_reference_rol(v[4], 7)) +
                                 ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
                        p32 t2 = (hash_reference_rol(v[0], 30) ^ hash_reference_rol(v[0], 19) ^
                                  hash_reference_rol(v[0], 10)) +
                                 ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
                        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
                        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
                }
                for (positive i = 0; i < 8; i++)
                        st[i] += v[i];
        }
}

static const p64 hash_reference_iv512[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};

static fn hash_reference_sha512(p64 address_to st, const p8 address_to d, positive n)
{
        static const p64 k[80] = {
            0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
            0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
            0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
            0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
            0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
            0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
            0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
            0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
            0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
            0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
            0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
            0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
            0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
            0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
            0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
            0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
            0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
            0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
            0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
            0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
        };

        for (; n; n--, d += 128)
        {
                p64 w[80];
                for (positive i = 0; i < 16; i++)
                        w[i] = (p64)hash_reference_be32(d + 8 * i) << 32 | hash_reference_be32(d + 8 * i + 4);
                for (positive i = 16; i < 80; i++)
                        w[i] = w[i - 16] + w[i - 7] +
                               (hash_reference_ror(w[i - 15], 1) ^ hash_reference_ror(w[i - 15], 8) ^ (w[i - 15] >> 7)) +
                               (hash_reference_ror(w[i - 2], 19) ^ hash_reference_ror(w[i - 2], 61) ^ (w[i - 2] >> 6));
                p64 v[8];
                for (positive i = 0; i < 8; i++)
                        v[i] = st[i];
                for (positive i = 0; i < 80; i++)
                {
                        p64 t1 = v[7] + (hash_reference_ror(v[4], 14) ^ hash_reference_ror(v[4], 18) ^
                                         hash_reference_ror(v[4], 41)) +
                                 ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
                        p64 t2 = (hash_reference_ror(v[0], 28) ^ hash_reference_ror(v[0], 34) ^
                                  hash_reference_ror(v[0], 39)) +
                                 ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
                        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
                        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
                }
                for (positive i = 0; i < 8; i++)
                        st[i] += v[i];
        }
}

static fn hash_reference_blake2b(p64 address_to st, const p8 address_to d, positive n,
                                 positive tail)
{
        static const p8 sigma[12][16] = {
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
            {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
            {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
            {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
            {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
            {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
            {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
            {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
            {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
            {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
            {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
        };
        static const p8 lanes[8][4] = {{0, 4, 8, 12}, {1, 5, 9, 13}, {2, 6, 10, 14}, {3, 7, 11, 15},
                                       {0, 5, 10, 15}, {1, 6, 11, 12}, {2, 7, 8, 13}, {3, 4, 9, 14}};

        for (; n; n--, d += 128)
        {
                p64 add = n == 1 ? tail : 128;
                p64 m[16], v[16];

                st[8] += add;
                if (st[8] < add)
                        st[9]++;
                for (positive i = 0; i < 16; i++)
                        m[i] = (p64)hash_reference_le32(d + 8 * i) |
                               (p64)hash_reference_le32(d + 8 * i + 4) << 32;
                for (positive i = 0; i < 8; i++)
                {
                        v[i] = st[i];
                        v[i + 8] = hash_reference_iv512[i];
                }
                v[12] ^= st[8];
                v[13] ^= st[9];
                v[14] ^= n == 1 ? st[10] : 0;
                for (positive r = 0; r < 12; r++)
                        for (positive g = 0; g < 8; g++)
                        {
                                const p8 address_to l = lanes[g];
                                v[l[0]] += v[l[1]] + m[sigma[r][2 * g]];
                                v[l[3]] = hash_reference_ror(v[l[3]] ^ v[l[0]], 32);
                                v[l[2]] += v[l[3]];
                                v[l[1]] = hash_reference_ror(v[l[1]] ^ v[l[2]], 24);
                                v[l[0]] += v[l[1]] + m[sigma[r][2 * g + 1]];
                                v[l[3]] = hash_reference_ror(v[l[3]] ^ v[l[0]], 16);
                                v[l[2]] += v[l[3]];
                                v[l[1]] = hash_reference_ror(v[l[1]] ^ v[l[2]], 63);
                        }
                for (positive i = 0; i < 8; i++)
                        st[i] ^= v[i] ^ v[i + 8];
        }
}

#elif defined(SHARED_filter_run)
/* Classic BPF as seccomp runs it, over one call: the client filter's checks
   (CHECK_net) and the Wi-Fi station's (CHECK_bowl). */
static p32 net_filter_run(const net_filter address_to filter, p32 arch,
                          p32 number, const p64 address_to arguments)
{
        p8 data[64];
        p32 accumulator = 0;

        memory_fill(data, 0, sizeof data);
        memory_copy(data, address_of number, 4);
        memory_copy(data + 4, address_of arch, 4);
        memory_copy(data + 16, arguments, 6 * sizeof(p64));
        for (positive at = 0; at < filter->used && at < NET_FILTER_STEPS; at++)
        {
                net_filter_step step = filter->steps[at];

                switch (step.code)
                {
                case NET_FILTER_LOAD:
                        if (step.value > sizeof data - 4)
                                return 0;
                        memory_copy(address_of accumulator, data + step.value, 4);
                        break;
                case NET_FILTER_AND:
                        accumulator &= step.value;
                        break;
                case NET_FILTER_ALWAYS:
                        at += step.value;
                        break;
                case NET_FILTER_EQUAL:
                        at += accumulator == step.value ? step.jump_true
                                                        : step.jump_false;
                        break;
                case NET_FILTER_ANY_BIT:
                        at += accumulator & step.value ? step.jump_true
                                                       : step.jump_false;
                        break;
                case NET_FILTER_RETURN:
                        return step.value;
                default:
                        return 0;
                }
        }
        //      Off the end: the kernel refuses such a program at install.
        return 0;
}

#elif defined(SHARED_audio_loops)
/*
        The loops of src/audio/mix.c that lib.c's audio routines took over, as the C they were, written once for the
        two programs that need them: CHECK_audio_mix, which is hosted and has no library and so supplies a routine of
        the library's name that calls these, and CHECK_audiolib, which holds the library's assembly to them. They
        are the definitions; the assembly is checked against them to the bound each routine's comment in lib.c states,
        and the one body that keeps their order (the SSE2 resampler) to the bit.
*/
#define AUDIO_C_UNUSED __attribute__((unused))

// The polyphase resampler's core: the row between the two nearest phases (a multiply and an add, as the machine without a
// fused multiply-add does them), then each plane's dot product in eight lanes folded as a tree.
#define AUDIO_C_POLYPHASE2(name, attributes)                                                                                            \
        attributes static AUDIO_C_UNUSED void name(float *out0, float *out1, const float *b0, const float *b1, const float *h,            \
                                                   unsigned long taps, unsigned long phases, unsigned long pos, unsigned long step,       \
                                                   unsigned long made)                                                                    \
        {                                                                                                                                 \
                for (unsigned long m = 0; m < made; m++, pos += step)                                                                    \
                {                                                                                                                         \
                        unsigned long index = pos >> 32, scaled = (pos & 0xffffffffu) * phases, phase = scaled >> 32;                     \
                        float f = (float)(scaled & 0xffffffffu) * (1.0f / 4294967296.0f);                                                  \
                        const float *a = h + phase * taps, *b = a + taps;                                                                  \
                        const float *x0 = b0 + index, *x1 = b1 + index;                                                                    \
                        float coefficient[1024], s0[8] = {0}, s1[8] = {0};                                                                  \
                                                                                                                                          \
                        for (unsigned long k = 0; k < taps; k++)                                                                           \
                                coefficient[k] = a[k] + f * (b[k] - a[k]);                                                                 \
                        for (unsigned long k = 0; k < taps; k += 8)                                                                        \
                                for (int j = 0; j < 8; j++)                                                                                \
                                {                                                                                                         \
                                        s0[j] += coefficient[k + j] * x0[k + j];                                                           \
                                        s1[j] += coefficient[k + j] * x1[k + j];                                                           \
                                }                                                                                                         \
                        out0[m] = ((s0[0] + s0[4]) + (s0[1] + s0[5])) + ((s0[2] + s0[6]) + (s0[3] + s0[7]));                              \
                        out1[m] = ((s1[0] + s1[4]) + (s1[1] + s1[5])) + ((s1[2] + s1[6]) + (s1[3] + s1[7]));                              \
                }                                                                                                                         \
        }
AUDIO_C_POLYPHASE2(audio_c_polyphase2, )

// The rows-only resampler: the row (frac * phases) >> 32 of the table, with no weight between two rows, each plane's dot product
// in eight lanes folded as a tree; the answer is the pos the walk ends on.
#define AUDIO_C_POLYPHASE2_ROWS(name, attributes)                                                                                       \
        attributes static AUDIO_C_UNUSED unsigned long name(float *out0, float *out1, const float *b0, const float *b1, const float *h,  \
                                                            unsigned long taps, unsigned long phases, unsigned long pos,                 \
                                                            unsigned long step, unsigned long made)                                      \
        {                                                                                                                                 \
                for (unsigned long m = 0; m < made; m++, pos += step)                                                                    \
                {                                                                                                                         \
                        unsigned long index = pos >> 32, phase = ((pos & 0xffffffffu) * phases) >> 32;                                    \
                        const float *a = h + phase * taps, *x0 = b0 + index, *x1 = b1 + index;                                             \
                        float s0[8] = {0}, s1[8] = {0};                                                                                    \
                                                                                                                                          \
                        for (unsigned long k = 0; k < taps; k += 8)                                                                        \
                                for (int j = 0; j < 8; j++)                                                                                \
                                {                                                                                                         \
                                        s0[j] += a[k + j] * x0[k + j];                                                                     \
                                        s1[j] += a[k + j] * x1[k + j];                                                                     \
                                }                                                                                                         \
                        out0[m] = ((s0[0] + s0[4]) + (s0[1] + s0[5])) + ((s0[2] + s0[6]) + (s0[3] + s0[7]));                              \
                        out1[m] = ((s1[0] + s1[4]) + (s1[1] + s1[5])) + ((s1[2] + s1[6]) + (s1[3] + s1[7]));                              \
                }                                                                                                                         \
                return pos;                                                                                                               \
        }
AUDIO_C_POLYPHASE2_ROWS(audio_c_polyphase2_rows, )

// A gain held over a block: y = x * gain rounded, then added to dst (add) or stored, the product and the sum rounded apart on
// every machine (a compiler may not fuse them: the library's routines do not), and the greatest |y| the block had, a NaN left out.
#if defined(__GNUC__) && !defined(__clang__)
#define AUDIO_C_EXACT __attribute__((optimize("fp-contract=off")))
#else
#define AUDIO_C_EXACT
#endif
#define AUDIO_C_SCALE(name, attributes)                                                                                    \
        attributes static AUDIO_C_UNUSED float name(float *dst, const float *x, unsigned long count, float gain, int add)    \
        {                                                                                                                   \
                float peak = 0;                                                                                             \
                                                                                                                            \
                for (unsigned long i = 0; i < count; i++)                                                                   \
                {                                                                                                           \
                        float y = x[i] * gain, a = y < 0 ? -y : y;                                                          \
                                                                                                                            \
                        peak = a > peak ? a : peak;                                                                         \
                        dst[i] = add ? dst[i] + y : y;                                                                      \
                }                                                                                                           \
                return peak;                                                                                                \
        }
AUDIO_C_SCALE(audio_c_scale, AUDIO_C_EXACT)

// A program's float is not trusted: a NaN is nothing, the rest is held to 16.
static AUDIO_C_UNUSED float audio_c_sane(float x)
{
        x = x > 16.0f ? 16.0f : x;
        x = x < -16.0f ? -16.0f : x;
        return x == x ? x : 0.0f;
}

// Interleaved frames of one of three formats (0 signed 16 bit, 1 signed 32 bit, 2 float, all little endian) to a plane a channel.
#define AUDIO_C_DECODE(name, attributes)                                                                                         \
        attributes static AUDIO_C_UNUSED void name(int format, const unsigned char *src, unsigned long frames, unsigned long channels, \
                                                   float *const *planes)                                                         \
        {                                                                                                                        \
                unsigned long bytes = format == 0 ? 2 : 4, stride = bytes * channels;                                            \
                                                                                                                                 \
                for (unsigned long c = 0; c < channels; c++)                                                                     \
                        for (unsigned long i = 0; i < frames; i++)                                                               \
                        {                                                                                                        \
                                const unsigned char *p = src + c * bytes + i * stride;                                           \
                                                                                                                                 \
                                if (format == 0)                                                                                 \
                                {                                                                                                \
                                        short x;                                                                                 \
                                                                                                                                 \
                                        __builtin_memcpy(&x, p, 2);                                                              \
                                        planes[c][i] = (float)x * (1.0f / 32768.0f);                                             \
                                }                                                                                                \
                                else if (format == 1)                                                                            \
                                {                                                                                                \
                                        int x;                                                                                   \
                                                                                                                                 \
                                        __builtin_memcpy(&x, p, 4);                                                              \
                                        planes[c][i] = (float)x * (1.0f / 2147483648.0f);                                        \
                                }                                                                                                \
                                else                                                                                             \
                                {                                                                                                \
                                        float x;                                                                                 \
                                                                                                                                 \
                                        __builtin_memcpy(&x, p, 4);                                                              \
                                        planes[c][i] = audio_c_sane(x);                                                          \
                                }                                                                                                \
                        }                                                                                                        \
        }
AUDIO_C_DECODE(audio_c_decode, AUDIO_C_EXACT)

// The sink's last two stages: a frame with a sample past full scale is counted and both its samples held to it (a NaN is past
// nothing and stays), and the pair becomes a card's frames: a sample rounded to the nearest integer, a tie to the even one, and
// held to the format's range, a NaN being zero (0 signed 16 bit, 1 signed 32 bit, 2 24 bits in 32, 3 float).
#define AUDIO_C_CLIP(name, attributes)                                                                                      \
        attributes static AUDIO_C_UNUSED unsigned long name(float *l, float *r, unsigned long count)                                 \
        {                                                                                                                         \
                unsigned long clips = 0;                                                                                          \
                                                                                                                                  \
                for (unsigned long i = 0; i < count; i++)                                                                          \
                {                                                                                                                 \
                        float a = l[i], b = r[i];                                                                                  \
                                                                                                                                  \
                        if (a > 1.0f || a < -1.0f || b > 1.0f || b < -1.0f)                                                        \
                        {                                                                                                         \
                                clips++;                                                                                          \
                                l[i] = a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a;                                                    \
                                r[i] = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;                                                    \
                        }                                                                                                         \
                }                                                                                                                 \
                return clips;                                                                                                     \
        }
AUDIO_C_CLIP(audio_c_clip, )

// A block of channels to two: one chain of sums a frame in channel order from nothing, each term a product rounded and added rounded.
#define AUDIO_C_MIX2(name, attributes)                                                                                          \
        attributes static AUDIO_C_UNUSED void name(float *out0, float *out1, const float *rows, unsigned long stride, const float *matrix, \
                                                   unsigned long count, unsigned long channels)                                  \
        {                                                                                                                       \
                for (unsigned long i = 0; i < count; i++)                                                                       \
                {                                                                                                               \
                        float l = 0, r = 0;                                                                                     \
                                                                                                                                \
                        for (unsigned long c = 0; c < channels; c++)                                                            \
                        {                                                                                                       \
                                l += matrix[c] * rows[c * stride + i];                                                          \
                                r += matrix[8 + c] * rows[c * stride + i];                                                      \
                        }                                                                                                       \
                        out0[i] = l;                                                                                            \
                        out1[i] = r;                                                                                            \
                }                                                                                                               \
        }
AUDIO_C_MIX2(audio_c_mix2, AUDIO_C_EXACT)

static AUDIO_C_UNUSED int audio_c_round16(float v)
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

static AUDIO_C_UNUSED int audio_c_round_wide(float v, double scale, double high)
{
        double d = (double)v * scale, r;
        long long bits;

        if (v != v)
                return 0;
        d = d > high ? high : d;
        d = d < -high - 1.0 ? -high - 1.0 : d;
        r = d + 6755399441055744.0;
        __builtin_memcpy(&bits, &r, 8);
        return (int)(bits & 0xffffffffll);
}

#define AUDIO_C_ENCODE(name, attributes)                                                                                    \
        attributes static AUDIO_C_UNUSED void name(int format, unsigned char *dst, unsigned long frames, const float *l, const float *r) \
        {                                                                                                                         \
                for (unsigned long i = 0; i < frames; i++)                                                                         \
                        for (int c = 0; c < 2; c++)                                                                                \
                        {                                                                                                         \
                                float v = c ? r[i] : l[i];                                                                         \
                                unsigned char *q = dst + (i * 2 + (unsigned long)c) * (format == 0 ? 2 : 4);                       \
                                                                                                                                  \
                                if (format == 0)                                                                                  \
                                {                                                                                                 \
                                        short x = (short)audio_c_round16(v);                                                       \
                                                                                                                                  \
                                        __builtin_memcpy(q, &x, 2);                                                               \
                                }                                                                                                 \
                                else if (format == 3)                                                                              \
                                {                                                                                                 \
                                        float x = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;                                         \
                                                                                                                                  \
                                        __builtin_memcpy(q, &x, 4);                                                               \
                                }                                                                                                 \
                                else                                                                                              \
                                {                                                                                                 \
                                        int x = format == 1 ? audio_c_round_wide(v, 2147483648.0, 2147483647.0) : audio_c_round_wide(v, 8388608.0, 8388607.0); \
                                                                                                                                  \
                                        __builtin_memcpy(q, &x, 4);                                                               \
                                }                                                                                                 \
                        }                                                                                                         \
        }
AUDIO_C_ENCODE(audio_c_encode, )

// The lane pcm_widen makes of one sample of the nine kinds it knows (see its prototype in lib.c), and a run of them.
static AUDIO_C_UNUSED unsigned audio_c_widen_one(unsigned kind, const unsigned char *p)
{
        unsigned little = 0, big = 0;

        if (kind == 2 || kind >= 5)
        {
                little = (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
                big = (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | (unsigned)p[3];
        }
        switch (kind)
        {
        case 0:
                return (unsigned)(p[0] ^ 0x80) << 24;
        case 1:
                return ((unsigned)p[0] << 8 | (unsigned)p[1]) << 16;
        case 2:
                return big;
        case 3:
                return (unsigned)p[0] << 8 | (unsigned)p[1] << 16 | (unsigned)p[2] << 24;
        case 4:
                return (unsigned)p[2] << 8 | (unsigned)p[1] << 16 | (unsigned)p[0] << 24;
        case 5:
                return little << 8;
        default:
                return big << 8;
        }
}

// pcm_widen: kind 0 unsigned 8 bits, 1 signed 16 big-endian, 2 a 32-bit word big-endian, 3 and 4 signed 24 bits in three bytes little- and
// big-endian, 5 and 6 the low 24 bits of a 32-bit word little- and big-endian; a kind past 6 is nothing.
static AUDIO_C_UNUSED void audio_c_widen(unsigned *dst, const unsigned char *src, unsigned long samples, unsigned long kind)
{
        static const unsigned char width[7] = {1, 2, 4, 3, 3, 4, 4};

        if (kind > 6)
                return;
        for (unsigned long i = 0; i < samples; i++)
                dst[i] = audio_c_widen_one((unsigned)kind, src + i * width[kind]);
}

// G.711: the float a byte of A-law (law 0) or mu-law (law 1) is, the table mix.c kept made by arithmetic.
static AUDIO_C_UNUSED float audio_c_law_one(unsigned char byte, unsigned long law)
{
        int a = byte ^ 0x55, t, segment, u = ~byte & 0xff, value;

        if (law == 0)
        {
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
                return (float)((a & 0x80) ? t : -t) * (1.0f / 32768.0f);
        }
        t = ((u & 0x0f) << 3) + 0x84;
        t <<= (u & 0x70) >> 4;
        value = (u & 0x80) ? 0x84 - t : t - 0x84;
        return (float)value * (1.0f / 32768.0f);
}

static AUDIO_C_UNUSED void audio_c_law(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels, unsigned long law)
{
        for (unsigned long c = 0; c < channels; c++)
                for (unsigned long i = 0; i < frames; i++)
                        planes[c][i] = audio_c_law_one(src[i * channels + c], law);
}

// The three low bytes of each of lanes 32-bit words, little-endian, one after the other.
static AUDIO_C_UNUSED void audio_c_pack24(unsigned char *dst, const unsigned *src, unsigned long lanes)
{
        for (unsigned long i = 0; i < lanes; i++)
        {
                dst[3 * i] = (unsigned char)src[i];
                dst[3 * i + 1] = (unsigned char)(src[i] >> 8);
                dst[3 * i + 2] = (unsigned char)(src[i] >> 16);
        }
}

//      A hosted check has no library: it takes this C for the routines of the library's names, which mix.c calls.
#ifdef AUDIO_C_AS_LIBRARY
// The library's memory names as mix.c calls them, for a hosted build: the C library's own copy and fill (a shim of the test, which
// is not the code being measured).
#ifndef CHECK_audio_service // the service's section has its own memory_copy
void *memory_copy_apart(void *destination, const void *source, unsigned long size)
{
        return memcpy(destination, source, size);
}

void *memory_copy(void *destination, const void *source, unsigned long size)
{
        return memmove(destination, source, size);
}

void *memory_fill(void *destination, unsigned char value, unsigned long size)
{
        return memset(destination, value, size);
}
#endif

/* the helpers below are defined after their first use here (lib.c's five pulse loops, 22a16648f): declared first */
static long audio_c_text_clean(unsigned char *to, const unsigned char *from, long length);
static int audio_c_key_printable(const unsigned char *from, long length);
static void audio_c_reverse_units(unsigned char *dst, long count, long width);
static void audio_c_s16_to_u8(unsigned char *dst, const unsigned char *src, long count);
static long audio_c_window_peaks(float *out0, float *out1, const float *left, const float *right, long frames, float *state,
                                 long *count, long window, int mono);

void pcm_widen(unsigned int *dst, const unsigned char *src, unsigned long samples, unsigned long kind)
{
        audio_c_widen(dst, src, samples, kind);
}

void pcm_pack24(unsigned char *dst, const unsigned *src, unsigned long lanes)
{
        audio_c_pack24(dst, src, lanes);
}

void f32_planes_from_law(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels, unsigned long law)
{
        audio_c_law(planes, src, frames, channels, law);
}

long memory_text_clean(unsigned char *to, const unsigned char *from, long length)
{
        return audio_c_text_clean(to, from, length);
}

int memory_key_printable(const unsigned char *from, long length)
{
        return audio_c_key_printable(from, length);
}

void memory_reverse_units(unsigned char *dst, long count, long width)
{
        audio_c_reverse_units(dst, count, width);
}

void memory_s16_to_u8(unsigned char *dst, const unsigned char *src, long count)
{
        audio_c_s16_to_u8(dst, src, count);
}

long memory_window_peaks(float *out0, float *out1, const float *left, const float *right, long frames, float *state, long *count, long window, long mono)
{
        return audio_c_window_peaks(out0, out1, left, right, frames, state, count, window, (int)mono);
}

void resample_polyphase2_f32(float *out0, float *out1, const float *x0, const float *x1, const float *table, unsigned long taps,
                             unsigned long phases, unsigned long pos, unsigned long step, unsigned long count)
{
        audio_c_polyphase2(out0, out1, x0, x1, table, taps, phases, pos, step, count);
}

unsigned long resample_polyphase2_f32_rows(float *out0, float *out1, const float *x0, const float *x1, const float *table,
                                           unsigned long taps, unsigned long phases, unsigned long pos, unsigned long step,
                                           unsigned long count)
{
        return audio_c_polyphase2_rows(out0, out1, x0, x1, table, taps, phases, pos, step, count);
}

float f32_scale_peak(float *dst, const float *x, unsigned long count, float gain)
{
        return audio_c_scale(dst, x, count, gain, 0);
}

float f32_scale_add_peak(float *dst, const float *x, unsigned long count, float gain)
{
        return audio_c_scale(dst, x, count, gain, 1);
}

void f32_planes_from_s16(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels)
{
        audio_c_decode(0, src, frames, channels, planes);
}

void f32_planes_from_s32(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels)
{
        audio_c_decode(1, src, frames, channels, planes);
}

void f32_planes_from_f32(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels)
{
        audio_c_decode(2, src, frames, channels, planes);
}

void f32_pair_to_s16(unsigned char *dst, const float *left, const float *right, unsigned long count)
{
        audio_c_encode(0, dst, count, left, right);
}

void f32_pair_to_s32(unsigned char *dst, const float *left, const float *right, unsigned long count)
{
        audio_c_encode(1, dst, count, left, right);
}

void f32_pair_to_s24_32(unsigned char *dst, const float *left, const float *right, unsigned long count)
{
        audio_c_encode(2, dst, count, left, right);
}

void f32_pair_to_f32(unsigned char *dst, const float *left, const float *right, unsigned long count)
{
        audio_c_encode(3, dst, count, left, right);
}

unsigned long f32_pair_clip(float *left, float *right, unsigned long count)
{
        return audio_c_clip(left, right, count);
}

void f32_rows_mix2(float *out0, float *out1, const float *rows, unsigned long stride, const float *matrix, unsigned long count, unsigned long channels)
{
        audio_c_mix2(out0, out1, rows, stride, matrix, count, channels);
}
#endif

/*
        The specifications of four loops of src/audio/pulse.c, each the loop the engine runs, moved here in the same meaning:
        the text a name is shown as, the bytes a property key may have, the reversal of each sample of a big-endian format,
        the 16-bit to 8-bit narrowing of the U8 format, and the greatest magnitude of each window of a peak stream. A library
        body that takes one of them is held to it by CHECK_audiolib, and the engine's rows in CHECK_audio_pulse hold the
        engine to it. Each is written so that a body may run on the arguments as they are: a count of zero does nothing, a
        pointer may be the same as another where the comment says so.
*/

// The text of a name: bytes from[0..length) up to the first NUL, each shown as itself when it is printable ASCII
// (0x20 to 0x7e) or as the whole of a valid UTF-8 sequence (C2..DF, E0..EF, F0..F4 with their continuation bytes, and the
// forms that are not overlong, not surrogates, not C1 controls and not past U+10FFFF); every other byte is shown as '?'.
// Each output is no longer than the input bytes it stands for, so to may be from. Answers the count written. Length may
// be zero. (The engine's cap on output is the caller's: pulse_props_text is given a room.)
static AUDIO_C_UNUSED long audio_c_text_clean(unsigned char *to, const unsigned char *from, long length)
{
        long n = 0;

        for (long at = 0; at < length && from[at]; at++)
        {
                unsigned char c = from[at];
                long extra = c >= 0xf0 && c < 0xf5 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 ? 1 : 0;

                if (c >= 0x20 && c < 0x7f)
                        to[n++] = c;
                else if (c >= 0xc2 && c < 0xf5 && at + extra < length)
                {
                        int good = 1;

                        for (long k = 1; k <= extra; k++)
                                good &= (from[at + k] & 0xc0) == 0x80;
                        if (good && extra == 1 && c == 0xc2 && from[at + 1] < 0xa0)
                                good = 0;
                        if (good && extra == 2 && c == 0xe0 && from[at + 1] < 0xa0)
                                good = 0;
                        if (good && extra == 2 && c == 0xed && from[at + 1] >= 0xa0)
                                good = 0;
                        if (good && extra == 3 && c == 0xf0 && from[at + 1] < 0x90)
                                good = 0;
                        if (good && extra == 3 && c == 0xf4 && from[at + 1] >= 0x90)
                                good = 0;
                        if (good)
                        {
                                for (long k = 0; k <= extra; k++)
                                        to[n++] = from[at + k];
                                at += extra;
                        }
                        else
                                to[n++] = '?';
                }
                else
                        to[n++] = '?';
        }
        return n;
}

// A property key: one to 63 bytes, each printable ASCII and not the space (0x21 to 0x7e). Answers 1 or 0.
static AUDIO_C_UNUSED int audio_c_key_printable(const unsigned char *from, long length)
{
        if (length < 1 || length >= 64)
                return 0;
        for (long i = 0; i < length; i++)
                if (from[i] <= ' ' || from[i] >= 127)
                        return 0;
        return 1;
}

// Each of count units of width bytes (2, 3 or 4) reversed in place: the big-endian form of the little-endian samples.
static AUDIO_C_UNUSED void audio_c_reverse_units(unsigned char *dst, long count, long width)
{
        for (long i = 0; i < count; i++)
                for (long k = 0; k < width / 2; k++)
                {
                        unsigned char t = dst[i * width + k];

                        dst[i * width + k] = dst[i * width + width - 1 - k];
                        dst[i * width + width - 1 - k] = t;
                }
}

// count little-endian 16-bit samples at src (two bytes each) as unsigned 8-bit samples: the sample rounded to the top byte
// (plus 128, arithmetic shift), clipped to 127 above, and biased by 128. dst may be src or lie below it: each pair is read
// before any byte that would overwrite it is written.
static AUDIO_C_UNUSED void audio_c_s16_to_u8(unsigned char *dst, const unsigned char *src, long count)
{
        for (long i = 0; i < count; i++)
        {
                int v = (short)(src[2 * i] | src[2 * i + 1] << 8);

                v = (v + 128) >> 8;
                dst[i] = (unsigned char)((v > 127 ? 127 : v) + 128);
        }
}

// The window of a peak stream over a block of frames: state[0] and state[1] hold the greatest magnitude of the window so
// far on each side, *count the frames of it so far, and window (at least 1) its length. A window that completes writes
// one frame to out0 and out1 (a mono stream's both sides are the greater of the two) and clears the state. Answers the
// frames written. The magnitude is x < 0 ? -x : x, so a NaN stays NaN and never becomes a peak.
static AUDIO_C_UNUSED long audio_c_window_peaks(float *out0, float *out1, const float *left, const float *right, long frames, float *state,
                                                long *count, long window, int mono)
{
        long made = 0;

        for (long i = 0; i < frames; i++)
        {
                float a = left[i] < 0 ? -left[i] : left[i], b = right[i] < 0 ? -right[i] : right[i];

                state[0] = a > state[0] ? a : state[0];
                state[1] = b > state[1] ? b : state[1];
                if (++*count >= window)
                {
                        out0[made] = state[0];
                        out1[made] = state[1];
                        if (mono)
                                out0[made] = out1[made] = state[0] > state[1] ? state[0] : state[1];
                        made++;
                        state[0] = state[1] = 0;
                        *count = 0;
                }
        }
        return made;
}

#else /* the sections */

#include "checks/standard.c" // the standard's own library: the C library's names and what each does
#include "checks/verify.c" // the library's assembly bodies held to their C, at every size and alignment
#include "checks/memory.c" // the memory routines at literal sizes: exact copies and sets, needles, bounded and exact scans, prefixes, case, the text and shell assembly, hashes, bases, folds, the allocator, reserve and UTF-8
#include "checks/numbers.c" // hex, codecs, fixed point, tables, errors, strings, stdlib, leaving, numbers, clocks, math, matrices, modulo, the trainer, the ranged routines and the environment
#include "checks/stdio.c" // format and scan, streams and spools, and their glibc twins
#include "checks/process.c" // processes, signals, locks and sockets
#include "checks/net.c" // the network stack, and the crypto vectors it is held to
#include "checks/waterlink.c" // the waterlink and its service
#include "checks/bowl.c" // bowls
#include "checks/compress.c" // tar, zstd, gzip and xz
#include "checks/shell.c" // the shell's reuse, its path stage, the builtins' regressions, slurp, the wait retry and the writer's buffer
#include "checks/storage.c" // storage's I/O and its formats
#include "checks/installer.c" // the installer: its drives, its plan, its steps and its write path on a file
#include "checks/machine.c" // the machine, snapshots, sensors and the browser
#include "checks/tensor.c" // the tensor routines
#include "checks/enhance.c" // the picture network routines and the wallpaper enhancer
#include "checks/jpeg.c" // the JPEG arithmetic: the inverse DCTs, colour conversion and upsampling, against their definitions
#include "checks/font.c" // the font engine and shaping

#if defined(CHECK_terminal) || defined(CHECK_kit_frames) || defined(CHECK_gpu_kit) || defined(CHECK_draw_list) || defined(CHECK_gpu_model_kit) || defined(CHECK_doc)
#include "checks/terminal.c" // that program's fakes (the kernel's DRM and the GPU model's hook, the card, a popup's row, what the kit hands the font engine) and the terminal's rows: its frames, the selection and its scroll, the kit's keys, the pointer a program asked for
#include "checks/compositor.c" // the compositor's rows: a first show, glass, partial presents and runs, scanout, screens and scales, corners and turns, the snap preview, shared edges, Alt-Tab, the desktop, the taskbar and its launcher, bands
#include "checks/compositor_hand.c" // the compositor and the hand: a held button, the cursor, close buttons, popups and menus, dropdowns, the tray, a tab's reach, the launcher button, the pointer's motion, prediction, the wheel, volume, notices, the chrome
#include "checks/compositor_windows.c" // the compositor's windows: their lifecycle, the screens they are on, the card and the table, the snap, tiles, the grid and dividers, the lit edge
#include "checks/compositor_touch.c" // touch through the compositor, the snap's frames, the touch path, auto-scroll, the caret, scrolling, flicks, the wheel and the throw, the trace, the screen's edges
#include "checks/compositor_places.c" // where a window is put and kept, and the glass over a wallpaper
#include "checks/settings.c" // the settings window's model, its rows and its pages
#include "checks/kit.c" // the kit's widgets, their motion and the hand on them
#include "checks/apps.c" // the kit's frames, and the terminal's own program: fields, inputs, the editor, menus, languages, the identify app and the terminal against a full draw
#endif

#include "checks/probe.c" // the probe and the stack guard
#include "checks/checksum.c" // checksums and CRCs, and their benchmarks
#include "checks/spark.c" // the Unicode width and link benchmarks, and spark's entry
#include "checks/canvas.c" // the text engine and the canvas's cells
#include "checks/wayland.c" // the Wayland shim and the X window manager's wire
#include "checks/dbus.c" // the session bus
#include "checks/cast.c" // the capture engine: the PipeWire of a screen cast and the rings' slots
#include "checks/audiolib.c" // the audio library's routines
#include "checks/audio.c" // the sound server: its mixer, ALSA, PulseAudio, the service, control and state
#include "checks/native.c" // the stdio buffer and the native routines built against generated headers
#include "checks/edit.c" // the editor's regressions, the lexer's spans and regular expressions
#include "checks/bench.c" // the benchmarks: the floor, numbers and bases, and pixels and memory
#include "checks/bench_text.c" // the benchmarks of the ranged routines, text, numbers and the network's parsers
#include "checks/bench_system.c" // the benchmarks of the clock, randomness and limbs, the allocator and locks, streams, startup and exec
#include "checks/floors.c" // the floors: compression, the hardware, libc and inlining
#include "checks/hosted.c" // the static probes a booted image runs: the radio, ring 0 and the firewall
#include "checks/gpu.c" // the card and its kit
#include "checks/draw.c" // the issue board, draw lists and the GPU assembly
#include "checks/coverage.c" // the hook a coverage build calls at each basic block
#include "checks/gpu_model.c" // the model of the card held to the card and to itself, and the GPU kit's rows drawn on it
#include "checks/routine.c" // the routine driver's benchmark
#include "checks/rover.c" // rover, the rules: its schema, the round trip, refusals, the load-time check and the evaluator, and its nanoseconds per event
#endif

#if defined(CHECK_doc) && !defined(SHARED_counted)
/*
        The document model (ui.c's struct doc, under the kit): the rows for the fixes of the selections, the replace-all, the
        failure paths and the word breaks, and a bench of carets and of opening a 100 MB file. The bench prints its numbers;
        the rows are counted. Run as gate.sh kit-doc check doc.
*/
static long doc_clock_ns(void)
{
        struct { long sec, nsec; } now;
        system_call_2(syscall(clock_gettime), CLOCK_MONOTONIC, (positive)address_of now);
        return now.sec * 1000000000L + now.nsec;
}

static void doc_selections_set(struct doc address_to doc, const p64 address_to pairs, positive count, p32 primary)
{
        array_store_reserve(doc->selections, doc->selection_room, 0, count, 16);
        for (positive s = 0; s < count; s++)
                doc->selections[s] = (struct doc_selection){pairs[2 * s], pairs[2 * s + 1], -1};
        doc->selection_count = count;
        doc->primary = primary;
        doc_normalize(doc);
}

static void cdoc_rows(void)
{
        struct doc d;
        struct doc_find find = {0};
        static p16 props[200000];
        static p8 breaks[200001];

        /* F1: the primary survives an overlap merge: A [0,3) and B [2,4) merge, C and D stay, D is the primary. */
        {
                const p64 pairs[] = {0, 3, 2, 4, 5, 6, 6, 7};
                doc_open(&d, (const p8 address_to) "abcdefghijklmnop", 16);
                doc_selections_set(&d, pairs, 4, 3);
                check("doc: an overlap merge keeps the primary on its own selection",
                      d.selection_count == 3 && d.selections[d.primary].anchor == 6);
                doc_close(&d);
        }

        /* F23: select next passes over a selected match and takes the next one: matches at 0, 4, 8; 0 and 8 selected. */
        {
                const p64 pairs[] = {0, 1, 8, 9};
                doc_open(&d, (const p8 address_to) "a x a x a", 9);
                doc_selections_set(&d, pairs, 2, 1);
                doc_select_next(&d);
                check("doc: select next takes the unselected match between two selected ones",
                      d.selection_count == 3 && d.selections[1].anchor == 4 && d.selections[1].active == 5);
                doc_close(&d);
        }

        /* The one UTF-8 decoder (lib's): a truncated sequence is one invalid byte, and the byte after it is read as it comes;
           a surrogate and an overlong are invalid, one byte each. */
        {
                static const p8 cut[] = {0xe2, 'A'}, surrogate[] = {0xed, 0xa0, 0x80}, overlong[] = {0xc0, 0x80};
                p32 code = 0;
                positive used = doc_decode(cut, 2, &code);
                int invalid = (code & DOC_INVALID) != 0;

                check("doc: a truncated sequence is one invalid byte, and the byte after it is read",
                      used == 1 && invalid && doc_decode(cut + 1, 1, &code) == 1 && code == 'A');
                check("doc: a surrogate and an overlong are invalid, one byte each",
                      doc_decode(surrogate, 3, &code) == 1 && (code & DOC_INVALID) != 0 && doc_decode(overlong, 2, &code) == 1 &&
                              (code & DOC_INVALID) != 0);
        }

        /* F22 (replace-all of group references) and F3, F4, F21 (failure paths) have no row yet: the regex header chain and a fault-injected allocator are needed (see the ledger). */
}

static void cdoc_bench(void)
{
        const positive megabytes = 100, size = megabytes << 20;
        p8 address_to bytes = (p8 address_to)system_call_6(syscall(mmap), 0, size, 3, 0x22, -1, 0);
        long started, finished;
        positive at = 0;
        struct doc d;
        if ((long)bytes < 0)
        {
                string_format(log, "  bench: no memory for the 100 MB file\n");
                return;
        }
        for (positive i = 0; i < size; i++)
                bytes[i] = i % 61 == 60 ? '\n' : 'a' + i % 26;
        started = doc_clock_ns();
        doc_open(&d, bytes, size);
        finished = doc_clock_ns();
        string_format(log, "  bench: open 100 MB: %p ms, %p lines\n", (positive)((finished - started) / 1000000), (positive)doc_line_count(&d));
        for (positive carets = 1; carets <= 10000; carets = carets == 1 ? 100 : carets * 100)
        {
                p64 pairs[20000];
                positive count = carets;
                for (positive s = 0; s < count; s++)
                        pairs[2 * s] = pairs[2 * s + 1] = (p64)(s * (size / count)) + 1;
                doc_selections_set(&d, pairs, count, 0);
                started = doc_clock_ns();
                doc_type(&d, (const p8 address_to) "x", 1);
                finished = doc_clock_ns();
                string_format(log, "  bench: %p carets, one insert: %p us\n", (positive)count, (positive)((finished - started) / 1000));
        }
        doc_close(&d);
        system_call_2(syscall(munmap), (positive)bytes, size);
        (void)at;
}

int main(void)
{
        cdoc_rows();
        cdoc_bench();
        log_flush();
        return failures != 0;
}
#endif

#include "checks/libdiff.c" // the library's memory routines held to C (libdiff)
