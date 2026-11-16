/*
        test/checks/memory.c: the memory routines at literal sizes: exact copies and sets, needles, bounded and exact
        scans, prefixes, case, the text and shell assembly, hashes, bases, folds, the allocator, reserve and UTF-8.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_exact
/*
        The wide tiers are raised here so the literal sizes below reach them:
        the known-size copies and fills up to 256 bytes, and the running
        windows up to 160. At the shipped 128 and 64 the 129 .. 160 literals
        fall to the routine and never touch a rung past 96 bytes, so a rung
        that drops bytes there is invisible to every lane.
*/
#define LIBRARY_SIZE_MAX 256
#define LIBRARY_RUNNING_MAX 160
#include "../../src/lib.util.c"
#define SHARED_sizes
#include "../checks.c"
#undef SHARED_sizes
/*
        Every size that is known where the call is written.


        memory_copy, memory_copy_apart and memory_fill expand in place when the
        size is a literal, and the line they expand to depends on the number: eight
        bytes is a pair of moves, forty is a vector and a remainder, two
        hundred is still a call. Nothing that takes its size from a loop
        counter can reach any of that, because the choice is made by the
        compiler, from the token. So every size is written out here.

        Each check runs into a buffer with guard bytes either side, and both
        halves are looked at: that the bytes asked for arrived, and that the
        bytes either side did not move. A size class that reaches one byte too
        far writes into a guard, and a size class that stops one byte short
        leaves the pattern showing.

        memory_copy is the name memmove is an alias for, so it is slid through
        every overlap in both directions: an expansion that stores before it
        has finished loading is correct on every test that does not overlap.
        memory_copy_apart promises nothing about overlap and is only checked
        where the two do not touch.

        161 sizes, 32 guard bytes, 40 of slide each way.
*/

#define TOP 160
#define GUARD 32
#define SLIDE 40

static p8 model[TOP];
static p8 arena[GUARD + TOP + GUARD];
static p8 field_room[SLIDE + TOP + SLIDE];
static p8 shadow[SLIDE + TOP + SLIDE];
static p8 temp[TOP];

static positive checks;
static positive failures;

#define FIELD (arena + GUARD)

//      The pattern the guards are set to, and the one the model carries. They
//      differ at every index so a copy that lands one byte off is visible.
static p8 guard_byte(positive i) { return (p8)(0x5a ^ (i * 7)); }
static p8 model_byte(positive i) { return (p8)(0xa5 ^ (i * 13)); }

static void fail(const p8 address_to what, positive size, const p8 address_to why)
{
        failures++;
        string_format(log, "FAIL %s at %p: %s\n", what, size, why);
}

static void prepare(void)
{
        for (positive i = 0; i < sizeof(arena); i++)
                arena[i] = guard_byte(i);
}

//      Guards on both sides, and the body against whatever it should hold.
static void inspect(const p8 address_to what, positive size,
                    const p8 address_to want, b32 want_is_byte)
{
        checks++;

        for (positive i = 0; i < GUARD; i++)
                if (arena[i] != guard_byte(i)) {
                        fail(what, size, "wrote before the front");
                        return;
                }

        for (positive i = GUARD + size; i < sizeof(arena); i++)
                if (arena[i] != guard_byte(i)) {
                        fail(what, size, "wrote past the end");
                        return;
                }

        for (positive i = 0; i < size; i++) {
                p8 got = FIELD[i];
                p8 expect = want_is_byte ? want[0] : want[i];

                if (got != expect) {
                        fail(what, size, "wrong byte");
                        return;
                }
        }
}

//      What a move means, spelled out, for the slid cases to be judged against.
static void reference_move(p8 address_to to, const p8 address_to from, positive size)
{
        for (positive i = 0; i < size; i++) temp[i] = from[i];
        for (positive i = 0; i < size; i++) to[i] = temp[i];
}

#define CHECK_COPY(K)                                                         \
        do {                                                                  \
                prepare();                                                    \
                answer = memory_copy_apart(FIELD, model, K);                   \
                checks++;                                                     \
                if (answer != (address_any)FIELD)                             \
                        fail((const p8 address_to)"memory_copy_apart", K,      \
                             "did not answer the destination");               \
                inspect((const p8 address_to)"memory_copy_apart", K, model, 0);\
        } while (0)

#define CHECK_FILL(K)                                                         \
        do {                                                                  \
                p8 value = (p8)(0x3c ^ (K));                                  \
                prepare();                                                    \
                answer = memory_fill(FIELD, value, K);                        \
                checks++;                                                     \
                if (answer != (address_any)FIELD)                             \
                        fail((const p8 address_to)"memory_fill", K,            \
                             "did not answer the destination");               \
                inspect((const p8 address_to)"memory_fill", K, &value, 1);    \
        } while (0)

//      Slid through every overlap the size allows, both directions. The shadow
//      carries the same bytes through the reference, and the two are compared
//      across the whole room, so a store that lands outside the size shows up.
#define CHECK_MOVE(K)                                                         \
        do {                                                                  \
                for (b32 slide = -SLIDE; slide <= SLIDE; slide++) {           \
                        p8 address_to from = field_room + SLIDE;              \
                        p8 address_to to = from + slide;                      \
                        p8 address_to shade = shadow + SLIDE + slide;         \
                                                                              \
                        for (positive i = 0; i < sizeof(field_room); i++)     \
                                field_room[i] = shadow[i] = model_byte(i);    \
                                                                              \
                        answer = memory_copy(to, from, K);                    \
                        checks++;                                             \
                        if (answer != (address_any)to)                        \
                                fail((const p8 address_to)"memory_copy", K,    \
                                     "did not answer the destination");       \
                        reference_move(shade, shadow + SLIDE, K);             \
                                                                              \
                        checks++;                                             \
                                                                              \
                        for (positive i = 0; i < sizeof(field_room); i++)     \
                                if (field_room[i] != shadow[i]) {             \
                                        fail((const p8 address_to)"memory_copy", K, \
                                             "differs from a byte at a time");\
                                        break;                                \
                                }                                             \
                }                                                             \
        } while (0)

/*
        Sizes known only at run time, which is what the running windows are
        for: every size from 0 to 255, copied and filled at an offset so the
        windows start unaligned, with a guard byte past each end that must
        keep its value. running_zero keeps the size out of the compiler's
        hands, so it cannot choose the literal tier for them.
*/
static volatile positive running_zero;

static void running_sizes(void)
{
        static p8 source[272], target[272];

        for (positive size = 0; size < 256; size++)
        {
                positive n = size + running_zero;
                p8 okay = true;

                for (positive i = 0; i < sizeof(source); i++)
                        source[i] = (p8)(i * 37 + size + 1);
                memory_fill(target, 0xee, sizeof(target));
                memory_copy(target + 8, source + 3, n);
                for (positive i = 0; i < n; i++)
                        okay &= target[8 + i] == source[3 + i];
                for (positive i = 0; i < 8; i++)
                        okay &= target[i] == 0xee;
                for (positive i = 8 + n; i < sizeof(target); i++)
                        okay &= target[i] == 0xee;
                checks++;
                if (!okay)
                {
                        failures++;
                        fail((const p8 address_to)"running copy", size, (const p8 address_to)"bytes");
                }

                memory_fill(target, 0xee, sizeof(target));
                memory_fill(target + 8, 0xab, n);
                okay = true;
                for (positive i = 0; i < n; i++)
                        okay &= target[8 + i] == 0xab;
                for (positive i = 0; i < 8; i++)
                        okay &= target[i] == 0xee;
                for (positive i = 8 + n; i < sizeof(target); i++)
                        okay &= target[i] == 0xee;
                checks++;
                if (!okay)
                {
                        failures++;
                        fail((const p8 address_to)"running fill", size, (const p8 address_to)"bytes");
                }
        }
}

//      Every size, once. Called once per tier below.
static void sweep(void)
{
        address_any answer;

#define CHECK_SIZE(K) CHECK_COPY(K); CHECK_FILL(K); CHECK_MOVE(K);
        EACH_SIZE(CHECK_SIZE)
#undef CHECK_SIZE
        running_sizes();
}

/*
        Once per tier, not once.

        The wide bodies are chosen at run time, by a byte, and a machine that
        has AVX2 takes that branch every time -- so a whole tier ships having
        never run. That has happened twice in this tree already: a one byte
        sabotage in the AVX2 path of memory_first_of passed the entire suite
        because every case was shorter than a vector.

        So the flag is walked down and the sweep repeated. The narrow tier is
        what a build with no permission to use vectors gets, and what every
        pre-AVX2 machine gets, and it is checked here on a machine that has
        them.
*/
b32 main(void)
{
        moonwater_cpu_detect();

        for (positive i = 0; i < TOP; i++) model[i] = model_byte(i);

        p8 found_avx2 = cpu_has_avx2;
        p8 found_avx512 = cpu_has_avx512;

        for (b32 tier = 0; tier < 2; tier++) {
                cpu_has_avx2 = tier ? 0 : found_avx2;
                cpu_has_avx512 = tier ? 0 : found_avx512;

                positive before = failures;
                sweep();

                string_format(log, "  %s: %p checks, %p failures\n",
                              tier ? (const p8 address_to)"narrow"
                                   : (const p8 address_to)"as found",
                              checks, failures - before);
        }

        cpu_has_avx2 = found_avx2;
        cpu_has_avx512 = found_avx512;

        string_format(log, "%p checks, %p failures\n", checks, failures);
        log_flush();

        return failures ? 1 : 0;
}
#endif /* CHECK_exact */

#ifdef CHECK_exact_set
#include "../../src/lib.util.c"
/*
        Every set that is known where the call is written.

        string_span_of_set, string_span_without_set and string_first_of_set
        expand in place when the members are a literal, and which line they
        expand to depends on the literal: no members at all is the answer
        without a scan, one member is a byte hunt, and anything more is a
        table folded into read only memory. Nothing that takes its members
        from a pointer can reach any of that, because the choice is made by
        the compiler, from the token. So every shape of set is written out
        here.

        Each check is run against two answers that were arrived at
        differently: the routine, reached by parenthesising its name so the
        macro does not fire, and a reference walk that carries no cleverness
        at all. The expansion has to agree with both.

        A scan that reads past the terminator is caught by running every
        source twice over. The bytes after the terminator are members the
        first time and strangers the second, and the terminator ends the run
        in all three routines, so the two runs must give the same answer. One
        that reads on gets a longer run from one layout than from the other.

        Guard bytes either side of the room say that nothing wrote, which none
        of these three is allowed to do.
*/

#define GUARD 32
#define ROOM 384
#define SHAPES 7

static p8 arena[GUARD + ROOM + GUARD];

#define FIELD (arena + GUARD)

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

//      The pattern the guards carry. It differs at every index so a store
//      that lands one byte off is visible.
static p8 guard_byte(positive i) { return (p8)(0x5a ^ (i * 7)); }

static void prepare(void)
{
        for (positive i = 0; i < sizeof(arena); i++) arena[i] = guard_byte(i);
}

static p8 guards_intact(void)
{
        for (positive i = 0; i < GUARD; i++)
                if (arena[i] != guard_byte(i)) return false;

        for (positive i = GUARD + ROOM; i < sizeof(arena); i++)
                if (arena[i] != guard_byte(i)) return false;

        return true;
}

//      The three answers, worked out the long way. A member is looked for by
//      walking the whole set for every byte, which is the definition and
//      nothing else.
static p8 member_of(string_address set, p8 value)
{
        if (value == end) return false;

        for (positive i = 0; set[i]; i++)
                if (set[i] == value) return true;

        return false;
}

static positive reference_span_of_set(string_address source, string_address set)
{
        positive length = 0;
        while (source[length] && member_of(set, source[length])) length++;
        return length;
}

static positive reference_span_without_set(string_address source, string_address set)
{
        positive length = 0;
        while (source[length] && !member_of(set, source[length])) length++;
        return length;
}

static string_address reference_first_of_set(string_address source, string_address set)
{
        for (positive at = 0; source[at]; at++)
                if (member_of(set, source[at])) return source + at;

        return (string_address)null;
}

//      A byte the set holds and a byte it does not, so that a source can be
//      built out of both. The set with every byte in it has no stranger but
//      the terminator, which is what the fallback answers.
static p8 a_member(string_address set) { return set[0] ? set[0] : (p8)'m'; }

static p8 a_stranger(string_address set)
{
        for (positive value = 1; value < 256; value++)
                if (!member_of(set, (p8)value)) return (p8)value;

        return (p8)end;
}

//      Seven shapes, so that the run stops at the front, at the back, in the
//      middle, and never; and two of them walk every byte value there is.
static p8 shaped(string_address set, positive shape, positive at, positive length)
{
        p8 member = a_member(set);
        p8 stranger = a_stranger(set);

        switch (shape)
        {
        case 0: return member;
        case 1: return stranger;
        case 2: return (at & 1) ? stranger : member;
        case 3: return at + 1 == length ? member : stranger;
        case 4: return at + 1 == length ? stranger : member;
        case 5: return (p8)(at % 255 + 1);
        default: return (p8)(255 - at % 255);
        }
}

//      The source, and behind its terminator either members or strangers.
static string_address lay(string_address set, positive shape, positive length,
                          p8 members_behind)
{
        p8 behind = members_behind ? a_member(set) : a_stranger(set);

        prepare();

        for (positive i = 0; i < length; i++)
                FIELD[i] = shaped(set, shape, i, length);

        FIELD[length] = end;

        for (positive i = length + 1; i < ROOM; i++) FIELD[i] = behind;

        return FIELD;
}

static void fail(string_address what, positive shape, positive length)
{
        failures++;
        string_format(log, "FAIL %s shape %p length %p\n", what, shape, length);
}

//      The three answers the expansion gave, against the routine and against
//      the reference. Both, because the routine and the expansion could agree
//      on the same misreading of what the set means.
static void judge(string_address set, string_address source, positive shape,
                  positive length, positive span, positive without,
                  string_address first)
{
        checks += 3;

        if (span != (string_span_of_set)(source, set) ||
            span != reference_span_of_set(source, set))
                fail((string_address)"span_of_set", shape, length);

        if (without != (string_span_without_set)(source, set) ||
            without != reference_span_without_set(source, set))
                fail((string_address)"span_without_set", shape, length);

        if (first != (string_first_of_set)(source, set) ||
            first != reference_first_of_set(source, set))
                fail((string_address)"first_of_set", shape, length);

        if (!guards_intact()) fail((string_address)"guards", shape, length);
}

//      Lengths that matter: every one up to past a block, then the sizes at
//      which a byte hunt changes width.
static positive lengths[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
        37, 38, 39, 40, 47, 48, 49, 63, 64, 65, 95, 96, 97, 127, 128, 129,
        191, 192, 193, 255, 256, 257, 300, 350
};

#define LENGTHS (sizeof(lengths) / sizeof(lengths[0]))

/*
        A stopping table is asked, before it is used, for one entry a table
        folded from the literal holds as 1: a byte outside the set. It asked
        for the terminator, which a stopping table holds as 0 however it was
        built, so every set of two or more members went to the routine and
        the table was never read -- about eighty instructions a call where
        the table's scan is a handful, on every arithmetic body the shell
        expanded. The table the macro reads has to answer 1 at the byte it
        is asked about, whenever the set leaves one out.
*/
static void witness(string_address set, p8 asked, b8 answer)
{
        checks++;

        if (asked && answer != 1)
        {
                failures++;
                string_format(log, "  FAIL witness: [%s] folds to a table that "
                                   "stops at the byte it is asked about\n", set);
        }
}

//      The literal has to be written into every call, because a set held in a
//      variable is a set the compiler has not been told.
#define CHECK_SET(literal)                                                    \
        do                                                                    \
        {                                                                     \
                string_address set = (string_address)(literal);               \
                for (positive shape = 0; shape < SHAPES; shape++)             \
                        for (positive behind = 0; behind < 2; behind++)       \
                                for (positive n = 0; n < LENGTHS; n++)        \
                                {                                             \
                                        positive length = lengths[n];         \
                                        string_address at =                   \
                                                lay(set, shape, length, behind); \
                                        judge(set, at, shape, length,         \
                                              string_span_of_set(at, literal), \
                                              string_span_without_set(at, literal), \
                                              string_first_of_set(at, literal)); \
                                }                                             \
                witness(set, set_known_length(literal) > 1 &&                 \
                                     set_known_outside(literal),              \
                        set_known_table(literal, set_known_stops)             \
                                [set_known_outside(literal)]);                \
        } while (0)

//      A set the compiler cannot see, so that the other arm of every macro is
//      compiled and run too. volatile is what stops the pointer folding.
static string_address volatile hidden = (string_address)"\n";

static void unknown_sets(void)
{
        static string_address sets[] = {
                (string_address)"", (string_address)"a", (string_address)" \t\n",
                (string_address)"\x01\x40\x80\xff",
        };

        for (positive s = 0; s < sizeof(sets) / sizeof(sets[0]); s++)
        {
                hidden = sets[s];

                for (positive shape = 0; shape < SHAPES; shape++)
                        for (positive n = 0; n < LENGTHS; n++)
                        {
                                string_address set = hidden;
                                positive length = lengths[n];
                                string_address at = lay(set, shape, length, 0);

                                judge(set, at, shape, length,
                                      string_span_of_set(at, hidden),
                                      string_span_without_set(at, hidden),
                                      string_first_of_set(at, hidden));
                        }
        }
}

//      A set the compiler can read that is not written at the call site: a
//      named const array, a slice of one, and an array something has stored
//      into, which is the one that must go to the routine instead of folding
//      to a table of whatever the front end could not see.
static const p8 named_set[] = " \t\n";

static void named_sets(void)
{
        for (positive shape = 0; shape < SHAPES; shape++)
                for (positive n = 0; n < LENGTHS; n++)
                {
                        positive length = lengths[n];
                        string_address at = lay(named_set, shape, length, 0);

                        judge(named_set, at, shape, length,
                              string_span_of_set(at, named_set),
                              string_span_without_set(at, named_set),
                              string_first_of_set(at, named_set));

                        at = lay(named_set + 1, shape, length, 0);

                        judge(named_set + 1, at, shape, length,
                              string_span_of_set(at, named_set + 1),
                              string_span_without_set(at, named_set + 1),
                              string_first_of_set(at, named_set + 1));
                }

        for (positive shape = 0; shape < SHAPES; shape++)
                for (positive n = 0; n < LENGTHS; n++)
                {
                        p8 written[4];
                        positive length = lengths[n];

                        written[0] = (p8)' ';
                        written[1] = (p8)'\t';
                        written[2] = (p8)hidden[0];
                        written[3] = end;

                        string_address at = lay(written, shape, length, 0);

                        judge(written, at, shape, length,
                              string_span_of_set(at, written),
                              string_span_without_set(at, written),
                              string_first_of_set(at, written));
                }
}

b32 main(void)
{
        //      No members at all, which answers without scanning.
        CHECK_SET("");

        //      One member, which is a byte hunt and no table: the lowest, the
        //      highest, either side of the word boundary the routine's bitmap
        //      has at sixty four, and the one at a hundred and twenty eight
        //      where a signed byte would have gone negative.
        CHECK_SET("a");
        CHECK_SET("\x01");
        CHECK_SET("\x3f");
        CHECK_SET("\x40");
        CHECK_SET("\x41");
        CHECK_SET("\x7f");
        CHECK_SET("\x80");
        CHECK_SET("\xff");

        //      Two and three, which is what a lexer writes down.
        CHECK_SET("=;");
        CHECK_SET(" \t");
        CHECK_SET(" \t\n");
        CHECK_SET(" \t\n\r");

        //      The same member twice, which must not be two members.
        CHECK_SET("aa");
        CHECK_SET("\xff\xff\xff");

        //      A terminator inside the literal ends the set: the members here
        //      are the letter a and nothing else.
        CHECK_SET("a\0b");

        //      Across every word of the bitmap the routine builds, and across
        //      the seam where a byte stops fitting in a signed char.
        CHECK_SET("\x01\x40\x80\xff");
        CHECK_SET("\x7f\x80");
        CHECK_SET("\x3f\x40\x41");

        //      Eight, sixteen and thirty two members.
        CHECK_SET(" \t\n\r;=()");
        CHECK_SET(" \t\n\r;=()[]{}<>|&");
        CHECK_SET("0123456789abcdefABCDEF !\"#$%&'()*+,-");

        //      Every byte value there is except the terminator, which cannot
        //      be a member however the set was written.
        CHECK_SET("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                  "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"
                  "\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f"
                  "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f"
                  "\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f"
                  "\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f"
                  "\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f"
                  "\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f"
                  "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
                  "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
                  "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf"
                  "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
                  "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
                  "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
                  "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
                  "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff");

        unknown_sets();
        named_sets();

        return test_report(null);
}
#endif /* CHECK_exact_set */

#ifdef CHECK_needle
#include "../../src/lib.util.c"
/*
        Every needle length that is known where the call is written.

        memory_search expands in place when the needle's length is a literal,
        and what it expands to depends on the number: nothing at all is a
        pointer to the front of the haystack, one byte is memory_first_of, and
        anything longer is still the call. Nothing that takes its needle
        length from a loop counter can reach any of that, because the choice
        is made by the compiler, from the token. So every length is written
        out here.

        Two things are checked at once and they need different apparatus.

        That the answer is right: the expansion is compared against the
        routine it replaces and against the obvious loop, at every haystack
        length from nothing to past a block, with the needle absent, at the
        front, at the last position it fits, in the middle, and at every
        position at once. The overlap case is in there by name -- "aab" is in
        "aaab", and a scan that resumes past what it matched cannot see it --
        because that is the one a rewrite gets wrong.

        That nothing was read that was not asked for: guard bytes cannot show
        this. memory_copy writes, so a store one byte past the size lands in a
        guard and is visible; memory_search only reads, and an expansion that
        loads a whole word past the end of the haystack corrupts nothing and
        passes every guard there is. So the haystack is put with its last byte
        against the last byte of a mapped page and the page after it is given
        back to the kernel. A read one byte too far is then a fault and not a
        silence.
*/

#define TOP 48
#define GUARD 32
#define SLAB (256u * 1024u)
#define PAGE (64u * 1024u)

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 arena[GUARD + TOP + GUARD];
static p8 record_arena[160];
static p8 address_to edge;              // one past the last mapped byte

#define FIELD (arena + GUARD)

static p8 guard_byte(positive i) { return (p8)(0x5a ^ (i * 7)); }

static void fail(const p8 address_to what, positive length, positive size,
                 const p8 address_to why)
{
        failures++;
        string_format(log, "FAIL %s needle %p hay %p: %s\n", what, length, size, why);
}

//      What a search means, spelled out, for the two quick ones to be judged
//      against. An empty needle is at the front, which is what memmem says
//      and is the case a rewrite forgets.
static address_any reference_search(const p8 address_to hay, positive size,
                                    const p8 address_to want, positive length)
{
        if (length == 0)
                return (address_any)hay;
        if (length > size)
                return null;
        for (positive at = 0; at + length <= size; at++) {
                positive i = 0;
                while (i < length && hay[at + i] == want[i])
                        i++;
                if (i == length)
                        return (address_any)(hay + at);
        }
        return null;
}

static p8 fold_ascii(p8 byte)
{
        return byte >= 'a' && byte <= 'z' ? (p8)(byte - 32) : byte;
}

static address_any reference_search_ascii_case(const p8 address_to hay,
                                               positive size,
                                               const p8 address_to want,
                                               positive length)
{
        if (!length)
                return (address_any)hay;
        if (length > size)
                return null;

        for (positive at = 0; at + length <= size; at++) {
                positive i = 0;
                while (i < length &&
                       fold_ascii(hay[at + i]) == fold_ascii(want[i]))
                        i++;
                if (i == length)
                        return (address_any)(hay + at);
        }
        return null;
}

static positive reference_records(const p8 address_to hay, positive size,
                                  const p8 address_to want, positive length,
                                  p8 delimiter)
{
        positive count = 0;
        positive at = 0;

        while (at < size) {
                positive stop = at;

                while (stop < size && hay[stop] != delimiter)
                        stop++;

                if (reference_search(hay + at, stop - at, want, length))
                        count++;

                at = stop + (stop < size);
        }

        return count;
}

/*
        One haystack, three answers, and the guards either side looked at
        afterwards: a search writes nothing, so a byte that moved is a defect
        whatever else was right.
*/
static void judge(const p8 address_to what, address_any got, address_any want,
                  positive length, positive size)
{
        checks++;
        if (got != want) {
                fail(what, length, size, "a different answer from the routine");
                return;
        }
        for (positive i = 0; i < GUARD; i++)
                if (arena[i] != guard_byte(i) ||
                    arena[GUARD + TOP + i] != guard_byte(GUARD + TOP + i)) {
                        fail(what, length, size, "wrote outside the haystack");
                        return;
                }
}

//      The haystack, built so that the needle is where the placement says and
//      nowhere earlier. Byte 'a' fills, 'b' breaks, and the needles below are
//      spelled out of both.
static void lay(positive size, positive where, const p8 address_to want,
                positive length)
{
        for (positive i = 0; i < sizeof(arena); i++)
                arena[i] = guard_byte(i);
        for (positive i = 0; i < size; i++)
                FIELD[i] = (p8)('a' + (i % 3));
        if (where != (positive)-1 && where + length <= size)
                for (positive i = 0; i < length; i++)
                        FIELD[where + i] = want[i];
}

/*
        The same haystack again with its last byte against the edge of the
        map. Only the answer is compared here: the arena's guards are not
        this copy's, and the point of this copy is the fault it does not take.
*/
static void judge_at_edge(const p8 address_to what, const p8 address_to want,
                          positive length, positive size, positive where)
{
        p8 address_to hay = edge - size;

        for (positive i = 0; i < size; i++)
                hay[i] = (p8)('a' + (i % 3));
        if (where != (positive)-1 && where + length <= size)
                for (positive i = 0; i < length; i++)
                        hay[where + i] = want[i];

        positive2 anchors = memory_search_prepare(want, length, false);
        address_any expected = reference_search(hay, size, want, length);

        checks++;
        if (memory_search((address_any)hay, size, want, length) != expected)
                fail(what, length, size, "a different answer against the map edge");
        checks++;
        if (memory_search_prepared((address_any)hay, size, want, length,
                                   anchors.x, anchors.y) != expected)
                fail((const p8 address_to)"prepared against edge", length, size,
                     "a different answer against the map edge");
}

static void judge_case_at_edge(const p8 address_to want, positive length,
                               positive size, positive where)
{
        p8 address_to hay = edge - size;

        for (positive i = 0; i < size; i++)
                hay[i] = (p8)('a' + (i % 3));
        if (where != (positive)-1 && where + length <= size)
                for (positive i = 0; i < length; i++)
                        hay[where + i] = want[i];

        positive2 anchors = memory_search_prepare(want, length, true);
        address_any expected = reference_search_ascii_case(hay, size, want,
                                                            length);

        checks++;
        if (memory_search_ascii_case((address_any)hay, size, want, length) !=
            expected)
                fail((const p8 address_to)"folded against edge", length, size,
                     "a different answer against the map edge");
        checks++;
        if (memory_search_ascii_case_prepared((address_any)hay, size, want,
                                              length, anchors.x, anchors.y) != expected)
                fail((const p8 address_to)"prepared folded against edge",
                     length, size,
                     "a different answer against the map edge");
}

/*
        One literal needle length, every haystack length, every placement.

        The length is written into the call as a token, which is the only way
        the expansion is reached at all, and the placements are the ones that
        separate a right answer from one that happens to agree: absent, at the
        front, at the last position it fits, in the middle, and -- through the
        overlap sweep below -- at more than one position at once.
*/
#define CHECK_SEARCH(K, LITERAL)                                              \
        do {                                                                  \
                const p8 address_to want = (const p8 address_to)LITERAL;      \
                for (positive size = 0; size <= TOP; size++) {                \
                        static const positive spots[] = {                     \
                                (positive)-1, 0, 1, 2, 7, TOP / 2, TOP        \
                        };                                                    \
                        for (positive s = 0; s < sizeof(spots) / sizeof(*spots); s++) { \
                                positive where = spots[s];                    \
                                if (where != (positive)-1 && where + (K) > size) \
                                        where = (size >= (K)) ? size - (K)    \
                                                              : (positive)-1; \
                                lay(size, where, want, (K));                  \
                                judge((const p8 address_to)"memory_search",   \
                                      memory_search((address_any)FIELD, size, \
                                                    LITERAL, (K)),            \
                                      reference_search(FIELD, size, want, (K)), \
                                      (K), size);                             \
                                judge((const p8 address_to)"against routine", \
                                      memory_search((address_any)FIELD, size, \
                                                    LITERAL, (K)),            \
                                      (memory_search)((address_any)FIELD, size, \
                                                      LITERAL, (K)),          \
                                      (K), size);                             \
                                positive2 anchors = memory_search_prepare(   \
                                        want, (K), false);                   \
                                judge((const p8 address_to)"prepared",      \
                                      memory_search_prepared(                \
                                              (address_any)FIELD, size, want, \
                                              (K), anchors.x, anchors.y),    \
                                      reference_search(FIELD, size, want, (K)), \
                                      (K), size);                            \
                                judge_at_edge((const p8 address_to)"memory_search", \
                                              want, (K), size, where);        \
                        }                                                     \
                }                                                             \
        } while (0)

//      The case-folded sibling, whose routine has no one byte redirect of its
//      own, so the expansion is the only place one exists. Both spellings of
//      a letter are asked for, and a byte that is not a letter as well.
#define CHECK_SEARCH_CASE(K, LITERAL)                                         \
        do {                                                                  \
                const p8 address_to want = (const p8 address_to)LITERAL;      \
                for (positive size = 0; size <= TOP; size++)                  \
                        for (positive s = 0; s < 4; s++) {                    \
                                positive where = s == 0 ? (positive)-1        \
                                        : (size >= (K) ? (s - 1) % (size - (K) + 1) \
                                                       : (positive)-1);       \
                                lay(size, where, want, (K));                  \
                                judge((const p8 address_to)"memory_search_ascii_case", \
                                      memory_search_ascii_case(               \
                                              (address_any)FIELD, size,       \
                                              LITERAL, (K)),                  \
                                      (memory_search_ascii_case)(             \
                                              (address_any)FIELD, size,       \
                                              LITERAL, (K)),                  \
                                      (K), size);                             \
                                positive2 anchors = memory_search_prepare(   \
                                        want, (K), true);                    \
                                judge((const p8 address_to)"prepared folded", \
                                      memory_search_ascii_case_prepared(     \
                                              (address_any)FIELD, size, want, \
                                              (K), anchors.x, anchors.y),    \
                                      reference_search_ascii_case(           \
                                              FIELD, size, want, (K)),       \
                                      (K), size);                            \
                                judge_case_at_edge(want, (K), size, where); \
                        }                                                     \
        } while (0)

/*
        "aab" in "aaab", and every other shape a resume past the match would
        lose. The haystack is a run of one letter with a break somewhere in
        it, so the needle matches at overlapping positions and only the first
        of them is the answer.
*/
#define CHECK_OVERLAP(K, LITERAL)                                             \
        do {                                                                  \
                const p8 address_to want = (const p8 address_to)LITERAL;      \
                for (positive size = 0; size <= TOP; size++)                  \
                        for (positive breaks = 0; breaks <= size; breaks++) { \
                                for (positive i = 0; i < sizeof(arena); i++)  \
                                        arena[i] = guard_byte(i);             \
                                for (positive i = 0; i < size; i++)           \
                                        FIELD[i] = 'a';                       \
                                if (breaks < size)                            \
                                        FIELD[breaks] = 'b';                  \
                                judge((const p8 address_to)"overlap",         \
                                      memory_search((address_any)FIELD, size, \
                                                    LITERAL, (K)),            \
                                      reference_search(FIELD, size, want, (K)), \
                                      (K), size);                             \
                        }                                                     \
        } while (0)

//      Every length, once. Called once per tier below.
static void sweep(void)
{
        CHECK_SEARCH(0, "");
        CHECK_SEARCH(1, "b");
        CHECK_SEARCH(2, "bc");
        CHECK_SEARCH(3, "abc");
        CHECK_SEARCH(4, "bcab");
        CHECK_SEARCH(5, "cabca");
        CHECK_SEARCH(6, "abcabc");
        CHECK_SEARCH(7, "bcabcab");
        CHECK_SEARCH(8, "cabcabca");
        CHECK_SEARCH(9, "abcabcabc");
        CHECK_SEARCH(16, "abcabcabcabcabca");
        CHECK_SEARCH(33, "abcabcabcabcabcabcabcabcabcabcabc");

        CHECK_SEARCH_CASE(0, "");
        CHECK_SEARCH_CASE(1, "b");
        CHECK_SEARCH_CASE(1, "B");
        CHECK_SEARCH_CASE(1, "-");
        CHECK_SEARCH_CASE(2, "bc");
        CHECK_SEARCH_CASE(3, "aBc");
        CHECK_SEARCH_CASE(33, "aBcAbCaBcAbCaBcAbCaBcAbCaBcAbCaBc");

        CHECK_OVERLAP(1, "b");
        CHECK_OVERLAP(2, "ab");
        CHECK_OVERLAP(3, "aab");
        CHECK_OVERLAP(4, "aaab");
        CHECK_OVERLAP(5, "aaaab");
        CHECK_OVERLAP(8, "aaaaaaab");
}

//      Every byte value as a one byte needle, since that length is the one
//      the expansion actually replaces and a byte the hunt treats specially
//      -- zero, 0xff, a letter -- would otherwise never be asked for.
static void every_byte(void)
{
        for (positive value = 0; value < 256; value++)
                for (positive size = 0; size <= 8; size++) {
                        p8 one = (p8)value;

                        for (positive i = 0; i < sizeof(arena); i++)
                                arena[i] = guard_byte(i);
                        for (positive i = 0; i < size; i++)
                                FIELD[i] = (p8)(value + i);

                        judge((const p8 address_to)"one byte",
                              memory_search((address_any)FIELD, size, &one, 1),
                              (memory_search)((address_any)FIELD, size, &one, 1),
                              1, size);
                }
}

/*
        Record counting mixes two ordered event streams: matches and record
        boundaries.  Exhaust every short three-symbol text to cover all their
        relative positions, then put the same shapes across each vector edge.
*/
static void records(void)
{
        static const p8 address_to wants[] = {
                (const p8 address_to)"", (const p8 address_to)"a",
                (const p8 address_to)"b", (const p8 address_to)"ab",
                (const p8 address_to)"ba", (const p8 address_to)"aa",
                (const p8 address_to)"\n", (const p8 address_to)"a\n"
        };
        static const positive lengths[] = {0, 1, 1, 2, 2, 2, 1, 2};

        for (positive size = 0; size <= 8; size++) {
                positive arrangements = 1;
                for (positive i = 0; i < size; i++)
                        arrangements *= 3;

                for (positive arrangement = 0; arrangement < arrangements;
                     arrangement++) {
                        positive value = arrangement;
                        for (positive i = 0; i < size; i++) {
                                positive digit = value % 3;
                                record_arena[i] = digit == 0 ? 'a'
                                                   : digit == 1 ? 'b' : '\n';
                                value /= 3;
                        }

                        for (positive w = 0; w < sizeof(wants) / sizeof(*wants);
                             w++) {
                                positive2 anchors = memory_search_prepare(
                                        wants[w], lengths[w], false);
                                positive expected = reference_records(
                                        record_arena, size, wants[w], lengths[w], '\n');
                                positive got = memory_count_records_with_prepared(
                                        record_arena, size, wants[w], lengths[w],
                                        anchors.x, anchors.y, '\n');

                                checks++;
                                if (got != expected)
                                        fail((const p8 address_to)"records",
                                             lengths[w], size,
                                             (const p8 address_to)"wrong count");
                        }
                }
        }

        static const positive edges[] = {15, 16, 17, 31, 32, 33, 47, 48,
                                         63, 64, 65, 95, 96, 97, 127};
        const p8 address_to wanted = (const p8 address_to)"needle";
        positive2 anchors = memory_search_prepare(wanted, 6, false);

        for (positive e = 0; e < sizeof(edges) / sizeof(*edges); e++) {
                positive size = edges[e];

                for (positive shift = 0; shift < 19; shift++) {
                        for (positive i = 0; i < size; i++)
                                record_arena[i] = (i + shift) % 13 == 12
                                                      ? '\n' : 'x';
                        for (positive i = shift; i + 6 <= size; i += 29)
                                memory_copy(record_arena + i, wanted, 6);

                        positive expected = reference_records(
                                record_arena, size, wanted, 6, '\n');
                        positive got = memory_count_records_with_prepared(
                                record_arena, size, wanted, 6,
                                anchors.x, anchors.y,
                                '\n');

                        checks++;
                        if (got != expected)
                                fail((const p8 address_to)"record edges", 6,
                                     size,
                                     (const p8 address_to)"wrong count");
                }
        }

        /* A proven match may leave its delimiter in a later vector, or no
           delimiter at all. End every span at an unmapped page: anchor and
           delimiter SIMD must both respect the same exact bound. */
        static p8 guard_needle[] = "needle0123456789ABCDEFGH";

        for (positive length = 1; length < sizeof(guard_needle); length++) {
                anchors = memory_search_prepare(guard_needle, length, false);

                for (positive size = length; size <= 260; size++) {
                        p8 address_to data = edge - size;

                        for (positive delimiter = 0; delimiter < 2; delimiter++) {
                                p8 separator = delimiter ? 0 : '\n';

                                for (positive step = 7; step <= 101; step += 23) {
                                        for (positive i = 0; i < size; i++)
                                                data[i] = (i + 1) % step ? 'x'
                                                                         : separator;
                                        for (positive i = 0; i + length <= size; i += 29) {
                                                memory_copy(data + i, guard_needle, length);
                                                if (length > 4 && (i / 29) % 2)
                                                        data[i + length / 2] = 'x';
                                        }

                                        positive expected = reference_records(
                                                data, size, guard_needle, length, separator);
                                        positive got = memory_count_records_with_prepared(
                                                data, size, guard_needle, length, anchors.x,
                                                anchors.y, separator);

                                        checks++;
                                        if (got != expected)
                                                fail((const p8 address_to)"record guard", length,
                                                     size, (const p8 address_to)"wrong count");
                                }
                        }
                }
        }
}

/*
        Once per tier, not once.

        The wide bodies are chosen at run time, by a byte, and a machine that
        has AVX2 takes that branch every time -- so a whole tier ships having
        never run. memory_first_of, which is what a one byte needle becomes,
        has three of them.
*/
b32 main(void)
{
        p8 address_to slab;

        moonwater_cpu_detect();

        //      A page given back to the kernel, and the haystack laid so its
        //      last byte is the last byte still mapped before it.
        slab = (p8 address_to)memory(SLAB);
        if ((positive)slab > (positive)-4096 || slab == null) {
                string_format(log, "no memory for the edge\n");
                log_flush();
                return 1;
        }
        edge = (p8 address_to)(((positive)slab + PAGE - 1) & ~(positive)(PAGE - 1))
               + PAGE;
        memory_free((address_any)edge, PAGE);

        p8 found_avx2 = cpu_has_avx2;
        p8 found_avx512 = cpu_has_avx512;

#if X64
        const b32 tiers = 3;
#else
        const b32 tiers = 1;
#endif
        for (b32 tier = 0; tier < tiers; tier++) {
                cpu_has_avx2 = tier == 2 ? 0 : found_avx2;
                cpu_has_avx512 = tier ? 0 : found_avx512;

                positive before = failures;
                sweep();
                every_byte();
                records();

                string_format(log, "  %s: %p checks, %p failures\n",
                              tier == 2 ? (const p8 address_to)"narrow"
                                        : tier ? (const p8 address_to)"without AVX-512"
                                               : (const p8 address_to)"as found",
                              checks, failures - before);
        }

        cpu_has_avx2 = found_avx2;
        cpu_has_avx512 = found_avx512;

        return test_report(null);
}
#endif /* CHECK_needle */

#ifdef CHECK_bounded
#include "../../src/lib.util.c"
/*
        Every bound that is known where the call is written.

        The bounded string routines expand in place when the bound is a
        literal, and which line they expand to depends on the number: two
        bytes is two compares, sixteen is two word loads, sixty four is still
        a call. Nothing that takes its bound from a loop counter reaches any
        of it, because the choice is made by the compiler, from the token. So
        every bound is written out here, the way CHECK_exact writes out
        every size.

        Two things are being checked and they need different machinery.

        That nothing is written past what the caller allowed is checked with
        guard bytes, the way exact.c does: the room in front of every
        destination carries a pattern, and so does the room behind it as far
        as the routine's own precondition allows.

        That nothing is READ past the bound cannot be checked with a pattern
        at all -- a read that runs on takes bytes that are there and says
        nothing. So every subject sits with its last byte against the last
        byte of a mapped page, and the page after it is taken away. A read one
        byte past the bound is then a fault and not a quiet pass, which is
        exactly the failure a buffer at the end of a mapping would suffer in
        the field. The destinations are placed the same way, so a write one
        past the end faults rather than landing in a guard nobody inspects.

        The three routines that take a bound and a source both need two fenced
        rooms, so there are three live pages with a dead one either side of
        each.

        The contracts differ in ways that matter to where the fence goes:
        string_copy_max writes at most the bound; string_copy_max_endptr
        writes exactly the bound and does not terminate when the source filled
        it; string_copy_max_end and string_append_max are allowed the bound
        plus one, which is their documented precondition. One guard rule for
        all six would fail three of them.
*/
#define TOP 64
#define GUARD 32
#define PREFIX 7

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 address_to source_edge;
static p8 address_to twin_edge;
static p8 address_to target_edge;

static p8 model[GUARD + TOP + 8];
static p8 taken[GUARD + TOP + 8];
static p8 spare[GUARD + TOP + 8];

static p8 address_to target(positive allowed);

#define NOT_INLINED __attribute__((noinline, noclone))

//      What each of the six means, spelled out, so the expansion and the
//      routine are both judged against something neither of them is.
NOT_INLINED positive reference_length_max(string_address source, positive bound)
{
        positive walked = 0;

        while (walked < bound && source[walked])
                walked++;

        return walked;
}

NOT_INLINED b32 reference_compare_max(string_address a, string_address b,
                                      positive bound)
{
        for (positive i = 0; i < bound; i++)
        {
                if (a[i] != b[i])
                        return (b32)a[i] - (b32)b[i];

                if (!a[i])
                        return 0;
        }

        return 0;
}

NOT_INLINED fn reference_copy_max(p8 address_to into, string_address source,
                                  positive bound)
{
        for (positive i = 0; i < bound; i++)
        {
                into[i] = source[i];

                if (!source[i])
                        return;
        }
}

NOT_INLINED positive reference_copy_max_end(p8 address_to into,
                                            string_address source, positive bound)
{
        positive walked = 0;

        while (walked < bound && source[walked])
        {
                into[walked] = source[walked];
                walked++;
        }

        into[walked] = 0;

        return walked;
}

NOT_INLINED positive reference_copy_max_endptr(p8 address_to into,
                                               string_address source,
                                               positive bound)
{
        positive walked = 0;

        while (walked < bound && source[walked])
        {
                into[walked] = source[walked];
                walked++;
        }

        for (positive pad = walked; pad < bound; pad++)
                into[pad] = 0;

        return walked;
}

static fn fail(string_address what, positive bound, positive where,
               string_address why)
{
        failures++;
        string_format(log, "FAIL %s bound %p at %p: %s\n", what, bound, where, why);
}

static fn judge(string_address what, positive bound, positive where,
                positive got, positive want)
{
        checks++;

        if (got != want)
                fail(what, bound, where, "the wrong answer");
}

static fn judge_bytes(string_address what, positive bound, positive where,
                      const p8 address_to got, const p8 address_to want,
                      positive size)
{
        checks++;

        for (positive i = 0; i < size; i++)
                if (got[i] != want[i])
                {
                        fail(what, bound, where, "the wrong bytes");
                        return;
                }
}

//      Three live pages with a dead one either side of each. Nothing here is
//      freed: the program is the test.
static fn fence_open(void)
{
        p8 address_to base = (p8 address_to)memory(7 * 4096);

        for (positive page = 0; page < 7; page += 2)
                system_call_3(syscall(mprotect),
                              (positive)(address_any)(base + page * 4096), 4096,
                              FILE_PROTECT_NONE);

        source_edge = base + 2 * 4096;
        twin_edge = base + 4 * 4096;
        target_edge = base + 6 * 4096;
}

//      The subject, its last byte against the last byte of a live page.
static string_address subject(positive bound, positive terminator)
{
        string_address source = source_edge - bound;

        for (positive i = 0; i < bound; i++)
                source[i] = (p8)('a' + (i * 5 + terminator) % 23);

        if (terminator < bound)
                source[terminator] = 0;

        return source;
}

/*
        A string may end before its maximum length, and no byte beyond that
        terminator has to be mapped.  The literal-bound sweep above puts its
        subjects exactly K bytes before a page edge; at K=64 that accidentally
        aligns the AVX-512 load and cannot catch an unaligned vector crossing
        the dead page.  Put every possible short suffix at the edge while
        keeping a deliberately larger runtime bound.  A page-safe scan finds
        the final NUL; an unaligned wide load faults before it can report it.
*/
static fn length_guard_edge(void)
{
        positive bound = 128;

        for (positive suffix = 1; suffix <= 64; suffix++)
        {
                string_address source = source_edge - suffix;

                for (positive i = 0; i < suffix; i++)
                        source[i] = (p8)('a' + (i * 7 + suffix) % 23);

                source[suffix - 1] = 0;
                judge((string_address)"string_length_max page edge", bound,
                      suffix, (string_length_max)(source, bound), suffix - 1);

                string_address found = null;

                if (suffix > 1)
                {
                        source[suffix - 2] = (p8)'z';
                        found = source + suffix - 2;
                }

                judge((string_address)"string_first_of_max page edge", bound,
                      suffix,
                      (positive)(string_first_of_max)(source, bound, (p8)'z'),
                      (positive)found);

                string_address twin = twin_edge - suffix;

                for (positive i = 0; i < suffix; i++)
                        twin[i] = source[i];

                judge((string_address)"string_compare_max page edge", bound,
                      suffix,
                      (positive)(string_compare_max)(source, twin, bound), 0);
        }
}

/*
        A literal maximum is not a promise that every byte up to that maximum
        is mapped.  Put an earlier terminator on the final readable byte and
        keep the literal at each call site, so this reaches the compiler's
        bounded-string specializers rather than the outlined page-aware
        routines exercised above.
*/
#define EARLY_LITERAL_GUARD(K)                                                \
        do {                                                                  \
                for (positive suffix = 1; suffix < (K); suffix++) {           \
                        string_address source = source_edge - suffix;         \
                        string_address twin = twin_edge - suffix;             \
                        p8 address_to into;                                   \
                        p8 address_to endp;                                   \
                        p8 padded = true;                                   \
                                                                                \
                        for (positive i = 0; i + 1 < suffix; i++)             \
                                source[i] = twin[i] =                         \
                                    (p8)('a' + (i * 7 + suffix) % 23);        \
                        source[suffix - 1] = twin[suffix - 1] = 0;            \
                                                                                \
                        judge((string_address)                                \
                              "string_length_max literal early NUL",        \
                              (K), suffix, string_length_max(source, (K)),    \
                              suffix - 1);                                    \
                        judge((string_address)                                \
                              "string_compare_max literal early NUL",       \
                              (K), suffix,                                    \
                              (positive)string_compare_max(source, twin,      \
                                                           (K)), 0);          \
                                                                                \
                        into = target((K) + 1);                               \
                        endp = string_copy_max_end(into, source, (K));        \
                        judge((string_address)                                \
                              "string_copy_max_end literal early NUL",      \
                              (K), suffix, (positive)(endp - into),           \
                              suffix - 1);                                    \
                        judge((string_address)                                \
                              "string_copy_max_end literal bytes",          \
                              (K), suffix,                                    \
                              (positive)!memory_compare(into, source, suffix),\
                              1);                                             \
                                                                                \
                        into = target((K));                                   \
                        endp = string_copy_max_endptr(into, source, (K));     \
                        for (positive i = suffix; i < (K); i++)               \
                                padded &= into[i] == 0;                       \
                        judge((string_address)                                \
                              "string_copy_max_endptr literal early NUL",   \
                              (K), suffix, (positive)(endp - into),           \
                              suffix - 1);                                    \
                        judge((string_address)                                \
                              "string_copy_max_endptr literal padding",     \
                              (K), suffix, padded, 1);                        \
                                                                                \
                        into = target(PREFIX + (K) + 1);                      \
                        for (positive i = 0; i < PREFIX; i++)                 \
                                into[i] = (p8)'z';                            \
                        into[PREFIX] = 0;                                     \
                        judge((string_address)                                \
                              "string_append_max literal early NUL",        \
                              (K), suffix,                                    \
                              (positive)(string_append_max(into, source,      \
                                                           (K)) == into &&   \
                                  !memory_compare(into + PREFIX, source,      \
                                                  suffix)),                  \
                              1);                                             \
                }                                                             \
        } while (0)

static fn literal_guard_edge(void)
{
        EARLY_LITERAL_GUARD(4);
        EARLY_LITERAL_GUARD(8);
}

static string_address mirror(string_address source, positive bound,
                             positive differ, p8 by)
{
        string_address twin = twin_edge - bound;

        for (positive i = 0; i < bound; i++)
                twin[i] = source[i];

        if (differ < bound)
                twin[differ] = (p8)(twin[differ] ^ by);

        return twin;
}

//      The destination, with its last allowed byte against the page edge and
//      a pattern in the room in front of it.
static p8 address_to target(positive allowed)
{
        p8 address_to into = target_edge - allowed;

        for (positive i = 0; i < allowed; i++)
                into[i] = (p8)0xc3;

        for (positive i = 1; i <= GUARD; i++)
                into[-(bipolar)i] = (p8)(0x5a ^ (i * 7));

        return into;
}

static fn keep(p8 address_to into, positive allowed, p8 address_to where)
{
        for (positive i = 0; i < GUARD; i++)
                where[i] = into[(bipolar)i - GUARD];

        for (positive i = 0; i < allowed; i++)
                where[GUARD + i] = into[i];
}

static fn model_room(positive allowed)
{
        for (positive i = 0; i < GUARD; i++)
                model[i] = (p8)(0x5a ^ ((GUARD - i) * 7));

        for (positive i = 0; i < allowed; i++)
                model[GUARD + i] = (p8)0xc3;
}

#define LENGTH_CASE(K)                                                         \
        do {                                                                   \
                for (positive t = 0; t <= (K); t++) {                          \
                        string_address s = subject((K), t);                    \
                        positive want = reference_length_max(s, (K));          \
                                                                               \
                        judge((string_address)"string_length_max routine",     \
                              (K), t, (string_length_max)(s, (K)), want);      \
                        judge((string_address)"string_length_max expanded",    \
                              (K), t, string_length_max(s, (K)), want);        \
                }                                                              \
        } while (0)

#define COMPARE_CASE(K)                                                        \
        do {                                                                   \
                for (positive t = 0; t <= (K); t++)                            \
                        for (positive d = 0; d <= (K); d++)                    \
                                for (positive f = 0; f < 2; f++) {             \
                                        string_address s = subject((K), t);    \
                                        string_address w = mirror(s, (K), d,   \
                                                f ? (p8)0x80 : (p8)0x01);      \
                                        positive want = (positive)             \
                                                reference_compare_max(s, w, (K)); \
                                                                               \
                                        judge((string_address)                 \
                                              "string_compare_max routine",    \
                                              (K), d,                          \
                                              (positive)(string_compare_max)(  \
                                                      s, w, (K)), want);       \
                                        judge((string_address)                 \
                                              "string_compare_max expanded",   \
                                              (K), d,                          \
                                              (positive)string_compare_max(    \
                                                      s, w, (K)), want);       \
                                        judge((string_address)                 \
                                              "string_compare_max swapped",    \
                                              (K), d,                          \
                                              (positive)string_compare_max(    \
                                                      w, s, (K)),              \
                                              (positive)                       \
                                              reference_compare_max(w, s, (K))); \
                                }                                              \
        } while (0)

//      string_copy_max is the one of the six with no expansion -- its window
//      was a bound of four and under, and nothing in the tree passes one that
//      small -- so this pins the routine against the model instead. Which is
//      worth doing on its own: strncpy now pads through its own wrapper, while
//      this routine deliberately does not.
#define COPY_CASE(K)                                                           \
        do {                                                                   \
                for (positive t = 0; t <= (K); t++) {                          \
                        string_address s = subject((K), t);                    \
                        p8 address_to into;                                    \
                                                                               \
                        model_room((K));                                       \
                        reference_copy_max(model + GUARD, s, (K));             \
                                                                               \
                        into = target((K));                                    \
                        (string_copy_max)(into, s, (K));                       \
                        keep(into, (K), taken);                                \
                                                                               \
                        judge_bytes((string_address)"string_copy_max",         \
                                    (K), t, taken, model, GUARD + (K));        \
                }                                                              \
        } while (0)

#define END_CASE(K)                                                            \
        do {                                                                   \
                for (positive t = 0; t <= (K); t++) {                          \
                        string_address s = subject((K), t);                    \
                        p8 address_to into;                                    \
                        positive want;                                         \
                                                                               \
                        model_room((K) + 1);                                   \
                        want = reference_copy_max_end(model + GUARD, s, (K));  \
                                                                               \
                        into = target((K) + 1);                                \
                        judge((string_address)"string_copy_max_end routine",   \
                              (K), t,                                          \
                              (positive)((string_copy_max_end)(into, s, (K)) - \
                                         into), want);                         \
                        keep(into, (K) + 1, taken);                            \
                                                                               \
                        s = subject((K), t);                                   \
                        into = target((K) + 1);                                \
                        judge((string_address)"string_copy_max_end expanded",  \
                              (K), t,                                          \
                              (positive)(string_copy_max_end(into, s, (K)) -   \
                                         into), want);                         \
                        keep(into, (K) + 1, spare);                            \
                                                                               \
                        judge_bytes((string_address)                           \
                                    "string_copy_max_end routine bytes",       \
                                    (K), t, taken, model, GUARD + (K) + 1);    \
                        judge_bytes((string_address)                           \
                                    "string_copy_max_end expanded bytes",      \
                                    (K), t, spare, model, GUARD + (K) + 1);    \
                }                                                              \
        } while (0)

#define PTR_CASE(K)                                                            \
        do {                                                                   \
                for (positive t = 0; t <= (K); t++) {                          \
                        string_address s = subject((K), t);                    \
                        p8 address_to into;                                    \
                        positive want;                                         \
                                                                               \
                        model_room((K));                                       \
                        want = reference_copy_max_endptr(model + GUARD, s, (K)); \
                                                                               \
                        into = target((K));                                    \
                        judge((string_address)"string_copy_max_endptr routine",\
                              (K), t,                                          \
                              (positive)((string_copy_max_endptr)(into, s, (K)) \
                                         - into), want);                       \
                        keep(into, (K), taken);                                \
                                                                               \
                        s = subject((K), t);                                   \
                        into = target((K));                                    \
                        judge((string_address)                                 \
                              "string_copy_max_endptr expanded", (K), t,       \
                              (positive)(string_copy_max_endptr(into, s, (K))  \
                                         - into), want);                       \
                        keep(into, (K), spare);                                \
                                                                               \
                        judge_bytes((string_address)                           \
                                    "string_copy_max_endptr routine bytes",    \
                                    (K), t, taken, model, GUARD + (K));        \
                        judge_bytes((string_address)                           \
                                    "string_copy_max_endptr expanded bytes",   \
                                    (K), t, spare, model, GUARD + (K));        \
                }                                                              \
        } while (0)

//      strncat: the bound counts source bytes only, the terminator is always
//      written, and the room it needs is what is already in the destination
//      plus the bound plus one.
#define APPEND_CASE(K)                                                         \
        do {                                                                   \
                for (positive t = 0; t <= (K); t++) {                          \
                        string_address s = subject((K), t);                    \
                        p8 address_to into;                                    \
                        positive want;                                         \
                                                                               \
                        model_room(PREFIX + (K) + 1);                          \
                        for (positive i = 0; i < PREFIX; i++)                  \
                                model[GUARD + i] = (p8)'z';                    \
                        model[GUARD + PREFIX] = 0;                             \
                        want = reference_copy_max_end(model + GUARD + PREFIX,  \
                                                      s, (K));                 \
                                                                               \
                        into = target(PREFIX + (K) + 1);                       \
                        for (positive i = 0; i < PREFIX; i++)                  \
                                into[i] = (p8)'z';                             \
                        into[PREFIX] = 0;                                      \
                        judge((string_address)"string_append_max routine",     \
                              (K), t,                                          \
                              (positive)((string_append_max)(into, s, (K)) ==  \
                                         into), 1);                            \
                        keep(into, PREFIX + (K) + 1, taken);                   \
                                                                               \
                        s = subject((K), t);                                   \
                        into = target(PREFIX + (K) + 1);                       \
                        for (positive i = 0; i < PREFIX; i++)                  \
                                into[i] = (p8)'z';                             \
                        into[PREFIX] = 0;                                      \
                        judge((string_address)"string_append_max expanded",    \
                              (K), t,                                          \
                              (positive)(string_append_max(into, s, (K)) ==    \
                                         into), 1);                            \
                        keep(into, PREFIX + (K) + 1, spare);                   \
                                                                               \
                        judge_bytes((string_address)                           \
                                    "string_append_max routine bytes", (K), t, \
                                    taken, model,                              \
                                    GUARD + PREFIX + (K) + 1);                 \
                        judge_bytes((string_address)                           \
                                    "string_append_max expanded bytes", (K), t,\
                                    spare, model,                              \
                                    GUARD + PREFIX + (K) + 1);                 \
                }                                                              \
        } while (0)

#define EVERY_BOUND(CASE)                                                      \
        CASE(0); CASE(1); CASE(2); CASE(3); CASE(4); CASE(5); CASE(6);         \
        CASE(7); CASE(8); CASE(9); CASE(10); CASE(11); CASE(12); CASE(13);     \
        CASE(14); CASE(15); CASE(16); CASE(17); CASE(18); CASE(19); CASE(20);  \
        CASE(21); CASE(22); CASE(23); CASE(24); CASE(25); CASE(26); CASE(27);  \
        CASE(28); CASE(29); CASE(30); CASE(31); CASE(32); CASE(33); CASE(34);  \
        CASE(35); CASE(36); CASE(37); CASE(38); CASE(39); CASE(40); CASE(41);  \
        CASE(42); CASE(43); CASE(44); CASE(45); CASE(46); CASE(47); CASE(48);  \
        CASE(49); CASE(50); CASE(51); CASE(52); CASE(53); CASE(54); CASE(55);  \
        CASE(56); CASE(57); CASE(58); CASE(59); CASE(60); CASE(61); CASE(62);  \
        CASE(63); CASE(64)

/*
        A bound past the end of memory. strnlen(s, SIZE_MAX) and
        strncmp(a, b, (size_t)-1) are legal C, "no bound" to the imported
        code that writes them, and the narrow x86_64 bodies and every riscv64
        one computed the end as start plus bound, which wrapped below the
        string: string_length_max answered SIZE_MAX, string_first_of_max
        missed a byte that was there, string_compare_max called two strings
        equal, and string_duplicate_max then took SIZE_MAX plus one bytes --
        none -- and copied the rest of memory into it. Every bound whose sum
        with the address overflows, from one past to all of it, at every x86
        feature tier, on a subject whose terminator is the page's last byte.
*/
static fn unbounded_guard_edge(void)
{
#if X64
        p8 wide = cpu_has_avx2, widest = cpu_has_avx512, vector_bytes = cpu_has_avx512_vbmi;
        positive tiers = 3;
#else
        positive tiers = 1;
#endif
        static b8 letters[256];

        for (positive c = 'a'; c <= 'z'; c++)
                letters[c] = 1;

        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx2 = tier < 2 ? wide : 0;
                cpu_has_avx512 = tier < 1 ? widest : 0;
                cpu_has_avx512_vbmi = tier < 1 ? vector_bytes : 0;
#endif
                for (positive suffix = 1; suffix <= 200; suffix++)
                {
                        string_address source = source_edge - suffix;
                        string_address twin = twin_edge - suffix;
                        positive start = (positive)source;
                        positive bounds[] = {positive_max, positive_max / 2 + 1,
                                             0 - start, 0 - start + 1,
                                             0 - start + suffix, positive_max - 7};

                        for (positive i = 0; i < suffix; i++)
                                source[i] = twin[i] =
                                    (p8)('a' + (i * 7 + suffix) % 23);
                        source[suffix - 1] = twin[suffix - 1] = 0;
                        if (suffix > 1)
                                source[suffix - 2] = (p8)'z';

                        for (positive b = 0; b < array_count(bounds); b++)
                        {
                                positive bound = bounds[b];

                                judge((string_address)"string_length_max past memory",
                                      bound, suffix,
                                      (string_length_max)(source, bound), suffix - 1);
                                judge((string_address)"string_first_of_max past memory",
                                      bound, suffix,
                                      (positive)(string_first_of_max)(source, bound, (p8)'z'),
                                      suffix > 1 ? (positive)(source + suffix - 2) : 0);
                                judge((string_address)"string_span_max past memory",
                                      bound, suffix,
                                      (string_span_max)(source, bound, letters), suffix - 1);
                                if (suffix > 1)
                                        judge((string_address)"string_compare_max past memory",
                                              bound, suffix,
                                              (positive)((string_compare_max)(source, twin, bound) != 0),
                                              (positive)(twin[suffix - 2] != 'z'));
                                string_address copy = string_duplicate_max(source, bound);
                                judge((string_address)"string_duplicate_max past memory",
                                      bound, suffix,
                                      copy ? string_length(copy) : positive_max, suffix - 1);
                                memory_give(copy);
                        }
                }

                //      And from the first byte of a live page, with the dead one
                //      below it: a block that starts before the subject faults.
                static const positive stops[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 31, 32, 33, 59, 60,
                                                 63, 64, 65, 66, 67, 68, 100, 127, 128, 129, 130,
                                                 131, 191, 192, 193, 4094, 4095};
                string_address first = source_edge - 4096;

                for (positive s = 0; s < array_count(stops); s++)
                {
                        positive stop = stops[s];
                        positive bounds[] = {positive_max, 4096, 0 - (positive)first, stop, stop + 1};

                        for (positive i = 0; i < 4096; i++)
                                ((p8 address_to)first)[i] = (p8)('a' + (i * 7 + stop) % 23);
                        ((p8 address_to)first)[stop] = 0;

                        for (positive b = 0; b < array_count(bounds); b++)
                                judge((string_address)"string_span_max from a page start",
                                      bounds[b], stop,
                                      (string_span_max)(first, bounds[b], letters),
                                      stop < bounds[b] ? stop : bounds[b]);
                }
        }
#if X64
        cpu_has_avx2 = wide;
        cpu_has_avx512 = widest;
        cpu_has_avx512_vbmi = vector_bytes;
#endif
}

/*
        strncasecmp at a page edge. A bound is not a licence to read past the
        terminator, and the SSE2 and NEON steps of string_compare_folded_max
        loaded sixteen bytes wherever they stood: strncasecmp("", "", 16)
        with either NUL on a page's last byte faulted at every feature tier.
        Both strings end on the last byte of a live page, one in capitals,
        at every length to 72 and bounds from the length to far past it,
        equal and with one byte made to differ.
*/
static fn folded_guard_edge(void)
{
        positive bounds[] = {16, 17, 31, 32, 64, 128, 4096, positive_max};

        for (positive suffix = 1; suffix <= 72; suffix++)
        {
                string_address one = source_edge - suffix;
                string_address two = twin_edge - suffix;

                for (positive i = 0; i + 1 < suffix; i++)
                {
                        one[i] = (p8)('a' + (i * 7 + suffix) % 26);
                        two[i] = (p8)(one[i] - 32);
                }
                one[suffix - 1] = two[suffix - 1] = 0;

                for (positive b = 0; b < array_count(bounds); b++)
                {
                        positive bound = bounds[b];

                        judge((string_address)"string_compare_folded_max page edge",
                              bound, suffix,
                              (positive)((string_compare_folded_max)(one, two, bound) != 0), 0);
                        if (suffix < 2)
                                continue;
                        two[suffix - 2] = '~';
                        judge((string_address)"string_compare_folded_max page edge differs",
                              bound, suffix,
                              (positive)((string_compare_folded_max)(one, two, bound) < 0),
                              (positive)(suffix - 2 < bound));
                        two[suffix - 2] = (p8)(one[suffix - 2] - 32);
                }
        }
}

/*
        string_digits_max with a literal bound: the expansion reads the same
        digits the routine does, and writes the same count. A run of digits
        that stops early, one that fills the bound and one that runs past it
        are all in the walk, at every literal from nought to sixty four.
*/
static positive digits_expect_reference(const p8 address_to source,
                                        positive bound, positive address_to used)
{
        positive value = 0, at = 0;

        while (at < bound && source[at] >= '0' && source[at] <= '9') {
                value = value * 10 + (source[at] - '0');
                at++;
        }
        if (used)
                *used = at;
        return value;
}

static fn digits_expect_fill(p8 address_to text, positive length, positive run)
{
        for (positive i = 0; i < length; i++)
                text[i] = i < run ? (p8)('1' + i % 9) : i == run ? (p8)' ' : (p8)'x';
}

#define DIGITS_CASE(K)                                                         \
        do {                                                                   \
                for (positive t = 0; t <= (K) + 1; t++) {                      \
                        p8 text[(K) + 2];                                      \
                        positive want_used = 0, used_routine = 0, used_expanded = 0; \
                        positive want;                                         \
                                                                               \
                        digits_expect_fill(text, (K) + 2, t);                  \
                        want = digits_expect_reference(text, (K), &want_used); \
                        judge((string_address)"string_digits_max routine",     \
                              (K), t, (string_digits_max)(text, (K), &used_routine), want); \
                        judge((string_address)"string_digits_max routine count", \
                              (K), t, used_routine, want_used);                \
                        judge((string_address)"string_digits_max expanded",    \
                              (K), t, string_digits_max(text, (K), &used_expanded), want); \
                        judge((string_address)"string_digits_max expanded count", \
                              (K), t, used_expanded, want_used);               \
                        judge((string_address)"string_digits_max expanded, no count", \
                              (K), t, string_digits_max(text, (K), 0), want);  \
                }                                                              \
        } while (0)

b32 main(void)
{
        fence_open();

        folded_guard_edge();
        unbounded_guard_edge();
        EVERY_BOUND(LENGTH_CASE);
        EVERY_BOUND(COMPARE_CASE);
        EVERY_BOUND(COPY_CASE);
        EVERY_BOUND(END_CASE);
        EVERY_BOUND(PTR_CASE);
        EVERY_BOUND(APPEND_CASE);
        EVERY_BOUND(DIGITS_CASE);
        length_guard_edge();
        literal_guard_edge();

        return test_report((string_address) "bounded: ");
}
#endif /* CHECK_bounded */

#ifdef CHECK_exact_scan
#include "../../src/lib.util.c"
#define SHARED_sizes
#include "../checks.c"
#undef SHARED_sizes
/*
        Every length that is known where the call is written.


        memory_compare, memory_first_of and memory_last_of expand in place
        when the length is a literal, and the line they expand to depends on
        the number: four bytes is a pair of loads, twelve is two words that
        overlap, two hundred is still a call. Nothing that takes its length
        from a loop counter can reach any of that, because the choice is made
        by the compiler, from the token. So every length is written out here.

        These three read and never write, so guard bytes either side would
        guard nothing: the mistake available to an expansion here is reading
        past the bound, not writing past it. Two things catch that instead.

        The block is laid against an unmapped page, once at each end, so a
        read of even one byte outside it stops the program rather than
        returning a wrong answer quietly. And in the second half of each
        check the bytes around the block are the very thing being looked
        for, so an expansion that reads too far without faulting still
        answers with a pointer outside the bound or a difference where there
        was none, and the check says so.

        Both the expansion and the routine are held against a byte at a time
        model rather than against each other, because two bodies that share a
        mistake agree with each other perfectly.

        161 lengths, every difference position and every match position for
        each, both ends of the page, and the whole sweep once more with the
        wide flags walked down.
*/

#define TOP 160
#define PAGE 4096
#define PROT_NONE 0
#define PROT_READ_WRITE 3

static p8 address_to first_room;
static p8 address_to second_room;

static positive checks;
static positive failures;

static fn fail(const p8 address_to what, positive size, positive detail,
               const p8 address_to why)
{
        failures++;
        string_format(log, "FAIL %s at %p (%p): %s\n", what, size, detail, why);
}

//      What each of the three means, spelled out a byte at a time, for the
//      other two answers to be judged against.
static b32 model_compare(const p8 address_to left, const p8 address_to right,
                         positive size)
{
        for (positive i = 0; i < size; i++)
                if (left[i] != right[i])
                        return (b32)left[i] - (b32)right[i];
        return 0;
}

static address_any model_first(const p8 address_to block, p8 value, positive size)
{
        for (positive i = 0; i < size; i++)
                if (block[i] == value)
                        return (address_any)(block + i);
        return null;
}

static address_any model_last(const p8 address_to block, p8 value, positive size)
{
        positive i = size;

        while (i) {
                i--;
                if (block[i] == value)
                        return (address_any)(block + i);
        }
        return null;
}

/*
        Three pages, and the two outside ones taken away.

        The middle page is where a block goes. Laid at its front, one byte
        before it is unmapped; laid at its back, one byte past it is. So a
        sweep that runs each length both ways has an unmapped byte on
        whichever side the expansion might have wandered to.
*/
static p8 address_to room(void)
{
        p8 address_to base = (p8 address_to)memory(3 * PAGE);

        if (!base)
                return null;
        system_call_3(syscall(mprotect), (positive)base, PAGE, PROT_NONE);
        system_call_3(syscall(mprotect), (positive)(base + 2 * PAGE), PAGE,
                      PROT_NONE);
        return base + PAGE;
}

//      side 0 puts the block against the unmapped page in front of it, end 1
//      against the one behind it.
static p8 address_to placed(p8 address_to region, positive size, positive side)
{
        return side ? region + PAGE - size : region;
}

#define CHECK_COMPARE(K)                                                      \
        do {                                                                  \
                for (positive side = 0; side < 2; side++) {                      \
                        p8 address_to left = placed(first_room, K, side);       \
                        p8 address_to right = placed(second_room, K, side);     \
                                                                              \
                        for (positive i = 0; i < K; i++)                       \
                                left[i] = right[i] = (p8)(0xa5 ^ (i * 13));   \
                                                                              \
                        for (positive spot = 0; spot <= K; spot++) {          \
                                b32 want, got;                                \
                                                                              \
                                if (spot < K)                                 \
                                        right[spot] = (p8)(left[spot] ^ 0xff);\
                                want = model_compare(left, right, K);         \
                                checks++;                                     \
                                got = compare_known(left, right, K);          \
                                if (got != want)                              \
                                        fail((const p8 address_to)            \
                                             "compare_known", K, spot,        \
                                             "differs from a byte at a time");\
                                checks++;                                     \
                                got = (memory_compare)(left, right, K);       \
                                if (got != want)                              \
                                        fail((const p8 address_to)            \
                                             "memory_compare", K, spot,       \
                                             "differs from a byte at a time");\
                                if (spot < K)                                 \
                                        right[spot] = left[spot];             \
                        }                                                     \
                }                                                             \
        } while (0)

/*
        The same lengths with the block put in the middle of the page and
        every byte around it made to differ, so an expansion that reads past
        the bound sees a difference that is not inside it.
*/
#define CHECK_COMPARE_SURROUNDED(K)                                           \
        do {                                                                  \
                p8 address_to left = first_room + 64;                         \
                p8 address_to right = second_room + 64;                       \
                                                                              \
                for (positive i = 0; i < PAGE - 128; i++) {                   \
                        left[i] = 0x11;                                       \
                        right[i] = 0x22;                                      \
                }                                                             \
                for (positive i = 0; i < 64; i++) {                           \
                        first_room[i] = 0x33;                                 \
                        second_room[i] = 0x44;                                \
                }                                                             \
                for (positive i = 0; i < K; i++)                              \
                        left[i] = right[i] = (p8)(0x5a ^ (i * 7));            \
                checks++;                                                     \
                if (compare_known(left, right, K) != 0)                       \
                        fail((const p8 address_to)"compare_known", K, 0,      \
                             "read past the bound");                          \
                checks++;                                                     \
                if ((memory_compare)(left, right, K) != 0)                    \
                        fail((const p8 address_to)"memory_compare", K, 0,     \
                             "read past the bound");                          \
        } while (0)

#define CHECK_SCAN(K)                                                         \
        do {                                                                  \
                static const p8 wanted[3] = {0x00, 0xa7, 0xff};               \
                                                                              \
                for (positive side = 0; side < 2; side++)                        \
                        for (positive w = 0; w < 3; w++) {                    \
                                p8 needle = wanted[w];                        \
                                p8 address_to block =                         \
                                        placed(first_room, K, side);           \
                                                                              \
                                for (positive i = 0; i < K; i++)              \
                                        block[i] = (p8)(needle ^ 0x5b ^       \
                                                        (i * 29));            \
                                for (positive i = 0; i < K; i++)              \
                                        if (block[i] == needle)               \
                                                block[i] = (p8)(needle ^ 1);  \
                                                                              \
                                for (positive spot = 0; spot <= K; spot++) {  \
                                        address_any want_first;               \
                                        address_any want_last;                \
                                                                              \
                                        if (spot < K)                         \
                                                block[spot] = needle;         \
                                        want_first = model_first(block,       \
                                                                 needle, K);  \
                                        want_last = model_last(block,         \
                                                               needle, K);    \
                                        checks++;                             \
                                        if (first_of_known(block, (b8)needle, \
                                                           K) != want_first)  \
                                                fail((const p8 address_to)    \
                                                     "first_of_known", K,     \
                                                     spot, "wrong answer");   \
                                        checks++;                             \
                                        if ((memory_first_of)(block,          \
                                                              (b8)needle, K)  \
                                            != want_first)                    \
                                                fail((const p8 address_to)    \
                                                     "memory_first_of", K,    \
                                                     spot, "wrong answer");   \
                                        checks++;                             \
                                        if (last_of_known(block, (b8)needle,  \
                                                          K) != want_last)    \
                                                fail((const p8 address_to)    \
                                                     "last_of_known", K,      \
                                                     spot, "wrong answer");   \
                                        checks++;                             \
                                        if ((memory_last_of)(block,           \
                                                             (b8)needle, K)   \
                                            != want_last)                     \
                                                fail((const p8 address_to)    \
                                                     "memory_last_of", K,     \
                                                     spot, "wrong answer");   \
                                        if (spot < K)                         \
                                                block[spot] =                 \
                                                        (p8)(needle ^ 1);     \
                                }                                             \
                        }                                                     \
        } while (0)

/*
        The block in the middle of the page with the byte being looked for
        laid on every side of it. A scan that reads one byte too far finds
        one and answers with an address outside the bound, which the model
        cannot produce and this catches.
*/
#define CHECK_SCAN_SURROUNDED(K)                                              \
        do {                                                                  \
                p8 needle = 0xa7;                                             \
                p8 address_to block = first_room + 64;                        \
                                                                              \
                for (positive i = 0; i < PAGE; i++)                           \
                        first_room[i] = needle;                               \
                for (positive i = 0; i < K; i++)                              \
                        block[i] = (p8)(needle ^ 1);                          \
                checks++;                                                     \
                if (first_of_known(block, (b8)needle, K) != null)             \
                        fail((const p8 address_to)"first_of_known", K, 0,     \
                             "read past the bound");                          \
                checks++;                                                     \
                if ((memory_first_of)(block, (b8)needle, K) != null)          \
                        fail((const p8 address_to)"memory_first_of", K, 0,    \
                             "read past the bound");                          \
                checks++;                                                     \
                if (last_of_known(block, (b8)needle, K) != null)              \
                        fail((const p8 address_to)"last_of_known", K, 0,      \
                             "read before the front");                        \
                checks++;                                                     \
                if ((memory_last_of)(block, (b8)needle, K) != null)           \
                        fail((const p8 address_to)"memory_last_of", K, 0,     \
                             "read before the front");                        \
        } while (0)

/*
        Matches and differences packed together rather than one at a time.

        A sweep that plants a single needle never tells a scan that answers
        with the first match from one that answers with the last, because
        with one match they are the same address. And the cheap carry-free
        zero test marks a byte holding one just above a byte holding zero,
        which only shows when the two are neighbours. So each length is also
        filled from a small alphabet that puts the needle, the needle with
        its bottom bit flipped, and two bytes that are neither, next to each
        other at every spacing the seeds reach.
*/
#define CHECK_DENSE(K)                                                        \
        do {                                                                  \
                p8 needle = 0xa7;                                             \
                                                                              \
                for (positive seed = 1; seed <= 24; seed++) {                 \
                        p8 address_to block = first_room + 64;                \
                        p8 address_to other = second_room + 64;               \
                        address_any want_first;                               \
                        address_any want_last;                                \
                        b32 want_difference;                                  \
                                                                              \
                        for (positive i = 0; i < K; i++) {                    \
                                positive roll = (i * seed + seed * 7) % 5;    \
                                                                              \
                                block[i] = roll == 0 ? needle                 \
                                         : roll == 1 ? (p8)(needle ^ 1)       \
                                         : roll == 2 ? 0                      \
                                         : (p8)(0x5a ^ (i * 11));             \
                                other[i] = (roll == 3) ? (p8)(block[i] ^ 0x80)\
                                                       : block[i];            \
                        }                                                     \
                                                                              \
                        want_first = model_first(block, needle, K);           \
                        want_last = model_last(block, needle, K);             \
                        want_difference = model_compare(block, other, K);     \
                                                                              \
                        checks++;                                             \
                        if (first_of_known(block, (b8)needle, K) != want_first)\
                                fail((const p8 address_to)"first_of_known",   \
                                     K, seed, "wrong match among many");      \
                        checks++;                                             \
                        if ((memory_first_of)(block, (b8)needle, K)           \
                            != want_first)                                    \
                                fail((const p8 address_to)"memory_first_of",  \
                                     K, seed, "wrong match among many");      \
                        checks++;                                             \
                        if (last_of_known(block, (b8)needle, K) != want_last) \
                                fail((const p8 address_to)"last_of_known", K, \
                                     seed, "wrong match among many");         \
                        checks++;                                             \
                        if ((memory_last_of)(block, (b8)needle, K)            \
                            != want_last)                                     \
                                fail((const p8 address_to)"memory_last_of", K,\
                                     seed, "wrong match among many");         \
                        checks++;                                             \
                        if (compare_known(block, other, K) != want_difference)\
                                fail((const p8 address_to)"compare_known", K, \
                                     seed, "wrong difference among many");    \
                        checks++;                                             \
                        if ((memory_compare)(block, other, K)                 \
                            != want_difference)                               \
                                fail((const p8 address_to)"memory_compare", K,\
                                     seed, "wrong difference among many");    \
                                                                              \
                        /*    And once more through the macros themselves,    \
                              which is the only place the cutoff is decided:  \
                              under it these are the expansion and over it    \
                              they are the call, and both have to be right. */\
                        checks++;                                             \
                        if (memory_first_of(block, (b8)needle, K)             \
                            != want_first)                                    \
                                fail((const p8 address_to)"the first_of "     \
                                     "macro", K, seed, "wrong answer");       \
                        checks++;                                             \
                        if (memory_last_of(block, (b8)needle, K) != want_last)\
                                fail((const p8 address_to)"the last_of macro",\
                                     K, seed, "wrong answer");                \
                        checks++;                                             \
                        if (memory_compare(block, other, K) != want_difference)\
                                fail((const p8 address_to)"the compare macro",\
                                     K, seed, "wrong difference");            \
                }                                                             \
        } while (0)

/*
        Every length, once, in bands of twenty.

        One function per band rather than one for the lot, because a length
        that expands to a straight line expands to a long one, and on riscv64
        a single sweep of a hundred and sixty of them puts the far end of the
        function out of reach of its own jumps.
*/
#define CHECK_SCAN_SIZE(K) \
        CHECK_COMPARE(K); CHECK_COMPARE_SURROUNDED(K); \
        CHECK_SCAN(K); CHECK_SCAN_SURROUNDED(K); CHECK_DENSE(K);

static void sweep_0_19(void)
{
        EACH_SIZE_0_19(CHECK_SCAN_SIZE)
}

static void sweep_20_39(void)
{
        EACH_SIZE_20_39(CHECK_SCAN_SIZE)
}

static void sweep_40_59(void)
{
        EACH_SIZE_40_59(CHECK_SCAN_SIZE)
}

static void sweep_60_79(void)
{
        EACH_SIZE_60_79(CHECK_SCAN_SIZE)
}

static void sweep_80_99(void)
{
        EACH_SIZE_80_99(CHECK_SCAN_SIZE)
}

static void sweep_100_119(void)
{
        EACH_SIZE_100_119(CHECK_SCAN_SIZE)
}

static void sweep_120_139(void)
{
        EACH_SIZE_120_139(CHECK_SCAN_SIZE)
}

static void sweep_140_159(void)
{
        EACH_SIZE_140_159(CHECK_SCAN_SIZE)
}

static void sweep_160_160(void)
{
        EACH_SIZE_160_160(CHECK_SCAN_SIZE)
}

#undef CHECK_SCAN_SIZE

//      Every band, once. Called once per tier below.
static void sweep(void)
{
        sweep_0_19();
        sweep_20_39();
        sweep_40_59();
        sweep_60_79();
        sweep_80_99();
        sweep_100_119();
        sweep_120_139();
        sweep_140_159();
        sweep_160_160();
}

/*
        Once per tier, not once.

        The wide bodies are chosen at run time, by a byte, and a machine that
        has AVX2 takes that branch every time -- so a whole tier ships having
        never run. The expansion has no tier of its own, but the routine it
        is measured and checked against has three, and the model is what both
        are held to. So the flag is walked down and the sweep repeated.
*/
b32 main(void)
{
        moonwater_cpu_detect();

        /*
                Function-like, so taking the address still names the routine
                rather than expanding: if any of the three macros were object
                like these three lines would not compile.
        */
        b32 (address_to compare_itself)(const address_any, const address_any,
                                        positive) = memory_compare;
        address_any (address_to first_itself)(address_any, b8, positive) =
                memory_first_of;
        address_any (address_to last_itself)(address_any, b8, positive) =
                memory_last_of;

        first_room = room();
        second_room = room();

        if (!first_room || !second_room) {
                string_format(log, "no room to work in\n");
                log_flush();
                return 1;
        }

        checks++;
        if (compare_itself((const address_any)"ab", (const address_any)"ab", 2)
            || first_itself((address_any)"ab", (b8)'b', 2) == null
            || last_itself((address_any)"ab", (b8)'a', 2) == null)
                fail((const p8 address_to)"the routine by its address", 0, 0,
                     "wrong answer");

        p8 found_avx2 = cpu_has_avx2;
        p8 found_avx512 = cpu_has_avx512;

        for (b32 tier = 0; tier < 2; tier++) {
                cpu_has_avx2 = tier ? 0 : found_avx2;
                cpu_has_avx512 = tier ? 0 : found_avx512;

                positive before = failures;
                sweep();

                string_format(log, "  %s: %p checks, %p failures\n",
                              tier ? (const p8 address_to)"narrow"
                                   : (const p8 address_to)"as found",
                              checks, failures - before);
        }

        cpu_has_avx2 = found_avx2;
        cpu_has_avx512 = found_avx512;

        string_format(log, "%p checks, %p failures\n", checks, failures);
        log_flush();

        return failures ? 1 : 0;
}
#endif /* CHECK_exact_scan */

#ifdef CHECK_exact_prefix
#include "../../src/lib.util.c"
/*
        Literal common-prefix lengths against a byte model, with the spans at
        both edges of a mapped page.  The adjacent pages are absent, so even
        one byte read before or after the caller's exact bound faults.
*/

#define PAGE 4096
#define PROT_NONE 0

static p8 address_to one_room;
static p8 address_to two_room;
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static positive model(const p8 address_to one, const p8 address_to two,
                      positive size)
{
        positive at = 0;

        while (at < size && one[at] == two[at])
                at++;
        return at;
}

static p8 address_to room(void)
{
        p8 address_to base = memory(PAGE * 3);

        if (!base)
                return null;
        if ((bipolar)system_call_3(syscall(mprotect), (positive)base, PAGE,
                                   PROT_NONE) < 0 ||
            (bipolar)system_call_3(syscall(mprotect),
                                   (positive)(base + PAGE * 2), PAGE,
                                   PROT_NONE) < 0)
        {
                memory_free(base, PAGE * 3);
                return null;
        }
        return base + PAGE;
}

static fn judge(string_address what, positive size, positive position,
                positive got, positive want)
{
        checks++;
        if (got != want)
        {
                failures++;
                if (failures < 20)
                        string_format(log,
                                      "FAIL %s size %p position %p: %p != %p\n",
                                      what, size, position, got, want);
        }
}

#define CHECK(N)                                                              \
        do                                                                    \
        {                                                                     \
                for (positive side = 0; side < 2; side++)                    \
                {                                                             \
                        p8 address_to one = side ? one_room + PAGE - (N)      \
                                                 : one_room;                  \
                        p8 address_to two = side ? two_room + PAGE - (N)      \
                                                 : two_room;                  \
                                                                              \
                        for (positive at = 0; at < (N); at++)                 \
                                one[at] = two[at] =                           \
                                    (p8)(at * 37 + (N) * 11);                 \
                                                                              \
                        for (positive position = 0; position <= (N);         \
                             position++)                                      \
                        {                                                     \
                                if (position < (N))                           \
                                        two[position] ^= 0xff;                \
                                positive want = model(one, two, (N));         \
                                judge((string_address)"known", (N),         \
                                      position,                               \
                                      common_prefix_known(one, two, (N)),     \
                                      want);                                  \
                                judge((string_address)"macro", (N),         \
                                      position,                               \
                                      memory_common_prefix(one, two, (N)),    \
                                      want);                                  \
                                judge((string_address)"routine", (N),       \
                                      position,                               \
                                      (memory_common_prefix)(one, two, (N)),  \
                                      want);                                  \
                                if (position < (N))                           \
                                        two[position] ^= 0xff;                \
                        }                                                     \
                }                                                             \
        } while (0)

b32 main(void)
{
        one_room = room();
        two_room = room();
        if (!one_room || !two_room)
                return 1;

        CHECK(0); CHECK(1); CHECK(2); CHECK(3); CHECK(4); CHECK(5); CHECK(6);
        CHECK(7); CHECK(8); CHECK(9); CHECK(10); CHECK(11); CHECK(12);
        CHECK(13); CHECK(14); CHECK(15); CHECK(16); CHECK(17); CHECK(18);
        CHECK(19); CHECK(20); CHECK(21); CHECK(22); CHECK(23); CHECK(24);
        CHECK(25); CHECK(26); CHECK(27); CHECK(28); CHECK(29); CHECK(30);
        CHECK(31); CHECK(32); CHECK(33); CHECK(40); CHECK(48); CHECK(64);
        CHECK(80); CHECK(96); CHECK(127); CHECK(128); CHECK(160);

        return test_report((string_address) "common-prefix literals: ");
}
#endif /* CHECK_exact_prefix */

#ifdef CHECK_exact_ascii_case
#include "../../src/lib.util.c"
/*
        Every literal size admitted by the folded ASCII comparison.

        Blocks are placed against the end of mapped pages so even one byte of
        over-read faults.  Each size exercises equality under case changes,
        every differing position, high bytes that must not be folded, and the
        public macro beside the out-of-line routine and a byte model.
*/

#define PAGE 4096
#define PROT_NONE 0

static p8 address_to one_edge;
static p8 address_to two_edge;
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 model_upper(p8 value)
{
        return value >= 'a' && value <= 'z' ? (p8)(value - 32) : value;
}

static b32 model(const p8 address_to one, const p8 address_to two,
                 positive size)
{
        for (positive at = 0; at < size; at++)
        {
                p8 left = model_upper(one[at]);
                p8 right = model_upper(two[at]);

                if (left != right)
                        return (b32)left - (b32)right;
        }

        return 0;
}

static p8 address_to room(void)
{
        p8 address_to base = memory(PAGE * 2);

        system_call_3(syscall(mprotect), (positive)(base + PAGE), PAGE,
                      PROT_NONE);
        return base + PAGE;
}

static fn judge(string_address what, positive size, positive position,
                b32 got, b32 want)
{
        checks++;
        if (got != want)
        {
                failures++;
                if (failures < 20)
                        string_format(log, "FAIL %s size %p position %p: %b != %b\n",
                                      what, size, position, got, want);
        }
}

#define CHECK(N)                                                              \
        do                                                                    \
        {                                                                     \
                p8 address_to one = one_edge - (N);                           \
                p8 address_to two = two_edge - (N);                           \
                                                                              \
                for (positive at = 0; at < (N); at++)                         \
                {                                                             \
                        p8 value = (p8)(at * 37 + (N) * 11);                  \
                        one[at] = value;                                       \
                        two[at] = value >= 'A' && value <= 'Z'                \
                                      ? (p8)(value + 32)                      \
                                      : value;                                \
                }                                                             \
                                                                              \
                judge((string_address)"known equal", (N), (N),              \
                      compare_ascii_case_known(one, two, (N)),                \
                      model(one, two, (N)));                                  \
                judge((string_address)"macro equal", (N), (N),              \
                      memory_compare_ascii_case(one, two, (N)),               \
                      model(one, two, (N)));                                  \
                                                                              \
                for (positive position = 0; position < (N); position++)       \
                {                                                             \
                        p8 saved = two[position];                              \
                        two[position] = saved == 0xff ? 0x80 : (p8)(saved + 1); \
                        judge((string_address)"known difference", (N),       \
                              position,                                       \
                              compare_ascii_case_known(one, two, (N)),        \
                              model(one, two, (N)));                          \
                        judge((string_address)"macro difference", (N),       \
                              position,                                       \
                              memory_compare_ascii_case(one, two, (N)),       \
                              model(one, two, (N)));                          \
                        two[position] = saved;                                \
                }                                                             \
        } while (0)

b32 main(void)
{
        one_edge = room();
        two_edge = room();

        if (!one_edge || !two_edge)
                return 1;

        for (positive left = 0; left < 256; left++)
                for (positive right = 0; right < 256; right++)
                {
                        p8 one = (p8)left;
                        p8 two = (p8)right;

                        judge((string_address)"all byte pairs", 1, 0,
                              compare_ascii_case_known(address_of one,
                                                       address_of two, 1),
                              model(address_of one, address_of two, 1));
                }

        CHECK(0); CHECK(1); CHECK(2); CHECK(3); CHECK(4); CHECK(5); CHECK(6);
        CHECK(7); CHECK(8); CHECK(9); CHECK(10); CHECK(11); CHECK(12);

        return test_report((string_address) "ASCII-case literal sizes: ");
}
#endif /* CHECK_exact_ascii_case */

#ifdef CHECK_text_asm
/*
        The branching assembly the text tools keep in src/sh/text.c, lifted
        block and all by the extractor's --source mode and built here on each
        machine: text_blank_field against the byte walk join ran before it, at
        every length, start and alignment, through bytes of every class, and
        against the last bytes of a page.
*/
#include "../../src/lib.util.c"
#include "text_asm.h"

positive2 text_blank_field(const p8 address_to bytes, positive length, positive at,
                           p8 extra);

static positive checks, failures;

static p8 text_asm_extra;

static p8 text_asm_blank(p8 byte) { return byte == ' ' || byte == '\t' || byte == text_asm_extra; }

static positive2 text_asm_former(const p8 address_to bytes, positive length, positive at)
{
        positive2 answer;

        while (at < length && text_asm_blank(bytes[at]))
                at++;
        answer.x = at;
        while (at < length && !text_asm_blank(bytes[at]))
                at++;
        answer.y = at;
        return answer;
}

static fn text_asm_join(const p8 address_to bytes, positive length)
{
        for (positive at = 0; at <= length; at++)
        {
                positive2 want = text_asm_former(bytes, length, at);
                positive2 got = text_blank_field(bytes, length, at, text_asm_extra);

                checks++;
                if (got.x != want.x || (want.x != length && got.y != want.y))
                {
                        failures++;
                        if (failures < 10)
                                string_format(log, "FAIL text_blank_field length %p at %p\n",
                                              length, at);
                }
        }
}

b32 main(void)
{
        static p8 room[512];
        static const p8 pieces[] = {' ', '\t', '\n', 'a', 'b', 0xff, 0x01, 'z'};
        p64 seed = 0x243f6a8885a308d3ull;
        p8 address_to pages = memory(3 * 4096);

        for (positive i = 0; i < sizeof(room); i++)
        {
                XORSHIFT64(seed);
                room[i] = (seed & 3) ? pieces[3 + (seed >> 8) % 5] : pieces[(seed >> 8) % 3];
        }

        for (positive extra = 0; extra < 2; extra++)
        {
                text_asm_extra = extra ? '\t' : '\n';
                for (positive length = 0; length <= 120; length++)
                        for (positive offset = 0; offset < 16; offset++)
                                text_asm_join(room + 64 + offset, length);
        }

        text_asm_extra = '\n';

        if ((bipolar)(positive)pages > 0 &&
            system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0)
                for (positive length = 0; length <= 40; length++)
                {
                        p8 address_to bytes = pages + 8192 - length;

                        memory_copy_apart(bytes, room + length, length);
                        text_asm_join(bytes, length);
                }

        string_format(log, "text assembly: %p checks, %p failures\n", checks, failures);
        log_flush();
        return failures != 0;
}
#endif /* CHECK_text_asm */

#ifdef CHECK_shell_asm
/*
        The branching assembly the shell keeps in src/sh/builtin.c, lifted
        block and all by the extractor's --source mode and built here on each
        machine: floodlight_status_clear against the byte walk that read
        /proc/self/status before it, over status files built line by line,
        every name the scan could mistake, each one cut, doubled and
        stretched, then random edits of those, at every alignment and against
        the last bytes of a page.
*/
#include "../../src/lib.util.c"
#include "shell_asm.h"

p8 floodlight_status_clear(const p8 address_to text, positive length);

static positive checks, failures;

/* The walk as it stood, whole file in one chunk: each line copied into 32
   bytes, the long ones marked, the two names judged at their newline. */
static p8 shell_asm_former(const p8 address_to text, positive length)
{
        p8 line[32];
        positive used = 0;
        p8 long_line = false, ended = true, mode = false, count = false;

        for (positive at = 0; at < length; at++)
        {
                p8 byte = text[at];
                positive name = 0, value;
                p8 address_to seen = null;

                if (byte != '\n')
                {
                        ended = false;
                        if (used < sizeof(line))
                                line[used++] = byte;
                        else
                                long_line = true;
                        continue;
                }
                if (used >= 8 && !memory_compare(line, "Seccomp:", 8))
                        name = 8, seen = &mode;
                else if (used >= 16 && !memory_compare(line, "Seccomp_filters:", 16))
                        name = 16, seen = &count;
                if (seen)
                {
                        if (long_line || *seen)
                                return false;
                        value = name;
                        while (value < used && (line[value] == ' ' || line[value] == '\t'))
                                value++;
                        if (value + 1 != used || line[value] != '0')
                                return false;
                        *seen = true;
                }
                used = 0;
                long_line = false;
                ended = true;
        }
        return ended && mode && count;
}

static p64 shell_asm_seed = 0x243f6a8885a308d3ull;

static p64 shell_asm_next(void)
{
        XORSHIFT64(shell_asm_seed);
        return shell_asm_seed;
}

static fn shell_asm_one(const p8 address_to text, positive length, positive what)
{
        p8 want = shell_asm_former(text, length);
        p8 got = floodlight_status_clear(text, length);

        checks++;
        if (want != got)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL floodlight_status_clear case %p length %p: %p want %p\n",
                                      what, length, (positive)got, (positive)want);
        }
}

/* The lines a real status has, with the two that matter where the kernel
   puts them, and the names the scan must not take for them. */
static const char address_to shell_asm_lines[] = {
        "Name:\tbasename", "Umask:\t0022", "State:\tR (running)", "Tgid:\t4242",
        "Pid:\t4242", "PPid:\t1", "Uid:\t1000\t1000\t1000\t1000", "FDSize:\t64",
        "Groups:\t998 1000 ", "VmPeak:\t    1024 kB", "SigQ:\t0/255445",
        "SigPnd:\t0000000000000000", "ShdPnd:\t0000000000000000",
        "SigBlk:\t0000000000000000", "SigIgn:\t0000000000000000",
        "CapEff:\t0000000000000000", "NoNewPrivs:\t0", "SECCOMP", "Seccomp",
        "Seccomp_", "Seccompx:\t0", "Seccomp_filter:\t0", "SSeccomp:\t0",
        "Speculation_Store_Bypass:\tthread vulnerable",
        "SpeculationIndirectBranch:\tconditional enabled",
        "Cpus_allowed_list:\t0-31", "voluntary_ctxt_switches:\t0", "S", "",
};

static const char address_to shell_asm_mode[] = {
        "Seccomp:\t0", "Seccomp: 0", "Seccomp:0", "Seccomp:\t2", "Seccomp:\t0 ",
        "Seccomp:\t00", "Seccomp:", "Seccomp:\t",
        "Seccomp: \t \t \t \t \t \t \t \t \t \t \t 0",
        "Seccomp: \t \t \t \t \t \t \t \t \t \t \t \t0", "Seccomp:\t\x00", "Seccomp:\t 0\t",
};

static const char address_to shell_asm_count[] = {
        "Seccomp_filters:\t0", "Seccomp_filters: 0", "Seccomp_filters:0",
        "Seccomp_filters:\t1", "Seccomp_filters:\t0 ", "Seccomp_filters:",
        "Seccomp_filters: \t \t \t \t \t \t \t 0",
        "Seccomp_filters: \t \t \t \t \t \t \t \t0",
        "Seccomp_filters:\t10",
};

static positive shell_asm_put(p8 address_to into, positive used, const char address_to line, p8 newline)
{
        positive length = string_length((string_address)line);

        if (line == shell_asm_mode[10])
                length = 10;
        memory_copy_apart(into + used, line, length);
        used += length;
        if (newline)
                into[used++] = '\n';
        return used;
}

/* A status file: most of the common lines, one of each name wherever the
   draw puts it, sometimes a second one or none, sometimes no last newline. */
static positive shell_asm_build(p8 address_to into)
{
        positive used = 0;
        positive lines = 4 + shell_asm_next() % 40;
        positive mode_at = shell_asm_next() % (lines + 2);
        positive count_at = shell_asm_next() % (lines + 2);
        positive twice = shell_asm_next() % 8;

        for (positive i = 0; i <= lines; i++)
        {
                if (i == mode_at || (twice == 0 && i == lines / 2))
                        used = shell_asm_put(into, used, shell_asm_mode[
                                shell_asm_next() % 3 ? 0 : shell_asm_next() % array_count(shell_asm_mode)], true);
                if (i == count_at || (twice == 1 && i == lines / 3))
                        used = shell_asm_put(into, used, shell_asm_count[
                                shell_asm_next() % 3 ? 0 : shell_asm_next() % array_count(shell_asm_count)], true);
                if (i < lines)
                        used = shell_asm_put(into, used,
                                             shell_asm_lines[shell_asm_next() % array_count(shell_asm_lines)],
                                             i + 1 < lines || shell_asm_next() % 8);
        }
        return used;
}

/* An edit drawn from the bytes the scan cares about. */
static positive shell_asm_edit(p8 address_to text, positive length, positive room)
{
        static const p8 bytes[] = "\nSeccomp:_filters0 \t\x00x";
        positive at = length ? shell_asm_next() % length : 0;
        p8 byte = bytes[shell_asm_next() % (sizeof(bytes) - 1)];

        switch (shell_asm_next() % 3)
        {
        case 0:
                if (length)
                        text[at] = byte;
                return length;
        case 1:
                if (length + 1 >= room)
                        return length;
                memory_copy(text + at + 1, text + at, length - at);
                text[at] = byte;
                return length + 1;
        default:
                if (!length)
                        return 0;
                memory_copy(text + at, text + at + 1, length - at - 1);
                return length - 1;
        }
}

/*
        parse_keyword_of from src/sh/parse.c against the length switch it
        replaced, over every reserved word, each cut, stretched and changed
        by a byte, words of every length to twelve drawn from their letters,
        tokens that are not words, at every alignment, and words that end on
        the last byte before a page nobody may read.
*/
typedef struct
{
        b32 kind, op, joined;
        string_address text;
        positive length;
        address_any alias_trace;
        b32 alias_forced, line;
} shell_asm_token;

b32 parse_keyword_of(const shell_asm_token address_to token);

static b32 shell_asm_keyword_former(const shell_asm_token address_to token)
{
        string_address text;

        if (token->kind != 1)
                return 0;

        text = token->text;

        switch (token->length)
        {
        case 1:
                switch (text[0])
                {
                case '!': return 19;
                case '{': return 9;
                case '}': return 17;
                default: return 0;
                }
        case 2:
                switch (memory_load_unaligned(p16, text))
                {
                case byte_word_2('i', 'f'): return 1;
                case byte_word_2('f', 'i'): return 13;
                case byte_word_2('d', 'o'): return 14;
                case byte_word_2('i', 'n'): return 18;
                case byte_word_2(']', ']'): return 20;
                default: return 0;
                }
        case 3:
                return memory_is_2(text, 'f', 'o') && text[2] == 'r' ? 4 : 0;
        case 4:
                switch (memory_load_unaligned(p32, text))
                {
                case byte_word_4('t', 'h', 'e', 'n'): return 10;
                case byte_word_4('e', 'l', 's', 'e'): return 11;
                case byte_word_4('e', 'l', 'i', 'f'): return 12;
                case byte_word_4('d', 'o', 'n', 'e'): return 15;
                case byte_word_4('c', 'a', 's', 'e'): return 5;
                case byte_word_4('e', 's', 'a', 'c'): return 16;
                case byte_word_4('t', 'i', 'm', 'e'): return 7;
                default: return 0;
                }
        case 5:
                if (memory_is_4(text, 'w', 'h', 'i', 'l') && text[4] == 'e')
                        return 2;
                if (memory_is_4(text, 'u', 'n', 't', 'i') && text[4] == 'l')
                        return 3;
                return 0;
        case 6:
                if (memory_is_4(text, 's', 'e', 'l', 'e') &&
                    memory_is_2(text + 4, 'c', 't'))
                        return 6;
                if (memory_is_4(text, 'c', 'o', 'p', 'r') &&
                    memory_is_2(text + 4, 'o', 'c'))
                        return 8;
                return 0;
        default:
                return 0;
        }
}

static const char address_to const shell_asm_reserved[] = {
        "!", "{", "}", "if", "fi", "do", "in", "]]", "for", "then", "else",
        "elif", "done", "case", "esac", "time", "while", "until", "select",
        "coproc",
};

static fn shell_asm_keyword_one(b32 kind, const p8 address_to text, positive length)
{
        shell_asm_token token = {kind, 0, 0, (string_address)text, length, null, 0, 1};
        b32 want = shell_asm_keyword_former(address_of token);
        b32 got = parse_keyword_of(address_of token);

        checks++;
        if (want != got)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL parse_keyword_of kind %p length %p: %p want %p\n",
                                      (positive)kind, length, (positive)got, (positive)want);
        }
}

static fn shell_asm_keywords(p8 address_to pages)
{
        static p8 room[64];
        static const p8 letters[] = "ifthenlsdocaeswhiuntmprx!{}][ ";

        for (positive i = 0; i < array_count(shell_asm_reserved); i++)
        {
                const char address_to word = shell_asm_reserved[i];
                positive length = string_length((string_address)word);

                for (positive offset = 0; offset < 16; offset++)
                {
                        p8 address_to at = room + offset;

                        memory_copy_apart(at, word, length);
                        at[length] = 'x';
                        shell_asm_keyword_one(1, at, length);
                        shell_asm_keyword_one(1, at, length + 1);
                        for (positive cut = 0; cut < length; cut++)
                                shell_asm_keyword_one(1, at, cut);
                        for (b32 kind = 0; kind < 6; kind++)
                                shell_asm_keyword_one(kind, at, length);
                        for (positive change = 0; change < length; change++)
                        {
                                p8 held = at[change];

                                at[change] ^= 0x20;
                                shell_asm_keyword_one(1, at, length);
                                at[change] = held + 1;
                                shell_asm_keyword_one(1, at, length);
                                at[change] = held;
                        }
                }
        }

        for (positive round = 0; round < 200000; round++)
        {
                positive length = shell_asm_next() % 13;
                positive offset = shell_asm_next() % 16;

                for (positive i = 0; i < length; i++)
                        room[offset + i] = letters[shell_asm_next() % (sizeof(letters) - 1)];
                room[offset + length] = shell_asm_next() % 2 ? 0 : 'q';
                if (shell_asm_next() % 3 == 0 && length)
                {
                        const char address_to word = shell_asm_reserved[
                            shell_asm_next() % array_count(shell_asm_reserved)];
                        positive have = string_length((string_address)word);

                        memory_copy_apart(room + offset, word, have < length ? have : length);
                }
                shell_asm_keyword_one(1, room + offset, length);
        }

        /* Words whose last byte is the last byte before a page nobody may read. */
        if (pages)
                for (positive i = 0; i < array_count(shell_asm_reserved); i++)
                {
                        const char address_to word = shell_asm_reserved[i];
                        positive length = string_length((string_address)word);

                        for (positive cut = 0; cut <= length; cut++)
                        {
                                p8 address_to at = pages + 8192 - cut;

                                memory_copy_apart(at, word, cut);
                                shell_asm_keyword_one(1, at, cut);
                        }
                        for (positive extra = 0; extra < 8; extra++)
                        {
                                p8 address_to at = pages + 8192 - length - extra;

                                memory_copy_apart(at, word, length);
                                memory_fill(at + length, 'e', extra);
                                shell_asm_keyword_one(1, at, length);
                                shell_asm_keyword_one(1, at, length + extra);
                        }
                }
}

/*
        env_find_hashed_span_probe from src/sh/builtin.c against the C probe
        it replaced, with memory_compare, which keeps its own copy of the last
        hit so the two can be compared after every call as well as their
        answers, alone and behind the C check of the name found last. The tables are built here: names
        from a few letters so neighbours differ by a byte, a real hash and one
        that collides on purpose, tombstones, slots that name a variable past
        the count, no index at all, and names and texts that end on the last
        byte before a page nobody may read.
*/
typedef struct
{
        string_address text;
        positive hash, name_length, value_length;
        p8 owned, permanent, declared;
        p8 attributes;
        b32 array;
} shell_asm_variable;

typedef struct
{
        positive hash, length, index_plus_one;
} shell_asm_slot;

typedef struct
{
        positive generation, hit_generation, hit_hash, hit_length, hit_index;
} shell_asm_lookup;

shell_asm_variable address_to shell_vars;
positive shell_var_count;
shell_asm_lookup env_lookup;
shell_asm_slot address_to env_index;
positive env_index_slots;

positive env_find_hashed_span_probe(const_string name, positive length, positive hash);

/* The check of the name found last, which stays C in the shell. */
static p8 shell_asm_found_last(shell_asm_lookup address_to lookup, const_string name,
                                 positive length, positive hash)
{
        if (lookup->hit_generation && lookup->hit_generation == lookup->generation &&
            lookup->hit_hash == hash && lookup->hit_length == length &&
            lookup->hit_index < shell_var_count)
        {
                positive at = 0;

                while (at < length && shell_vars[lookup->hit_index].text[at] == name[at])
                        at++;
                return at == length;
        }
        return false;
}

static positive shell_asm_find_former(shell_asm_lookup address_to lookup, const_string name,
                                      positive length, positive hash)
{
        if (env_index_slots)
        {
                positive at = hash & (env_index_slots - 1);

                for (positive probes = 0; probes < env_index_slots; probes++)
                {
                        shell_asm_slot address_to slot = env_index + at;

                        if (!slot->index_plus_one)
                                return shell_var_count;
                        if (slot->index_plus_one != positive_max && slot->hash == hash &&
                            slot->length == length)
                        {
                                positive index = slot->index_plus_one - 1;

                                if (index < shell_var_count &&
                                    !memory_compare(shell_vars[index].text, (address_any)name, length))
                                {
                                        if (!lookup->generation)
                                                lookup->generation = 1;
                                        lookup->hit_generation = lookup->generation;
                                        lookup->hit_hash = hash;
                                        lookup->hit_length = length;
                                        lookup->hit_index = index;
                                        return index;
                                }
                        }
                        at = (at + 1) & (env_index_slots - 1);
                }
                return shell_var_count;
        }
        for (positive index = 0; index < shell_var_count; index++)
                if (shell_vars[index].hash == hash && shell_vars[index].name_length == length &&
                    !memory_compare(shell_vars[index].text, (address_any)name, length))
                {
                        if (!lookup->generation)
                                lookup->generation = 1;
                        lookup->hit_generation = lookup->generation;
                        lookup->hit_hash = hash;
                        lookup->hit_length = length;
                        lookup->hit_index = index;
                        return index;
                }
        return shell_var_count;
}

static positive shell_asm_weak;

static positive shell_asm_name_hash(const p8 address_to name, positive length)
{
        return shell_asm_weak ? length * 0x9e3779b97f4a7c15ull >> (shell_asm_weak * 8)
                              : memory_hash_33((address_any)name, length);
}

/* The probe alone, and the whole lookup as the shell makes it. */
static fn shell_asm_find_one(const p8 address_to name, positive length, positive hash)
{
        for (positive whole = 0; whole < 2; whole++)
        {
                shell_asm_lookup former = env_lookup, held = env_lookup;
                p8 last = whole && shell_asm_found_last(address_of former, (const_string)name, length, hash);
                positive want = last ? former.hit_index
                                     : shell_asm_find_former(address_of former, (const_string)name, length, hash);
                positive got = last ? env_lookup.hit_index
                                    : env_find_hashed_span_probe((const_string)name, length, hash);

                checks++;
                if (want != got || memory_compare(address_of former, address_of env_lookup, sizeof(former)))
                {
                        failures++;
                        if (failures < 10)
                                string_format(log, "FAIL env_find_hashed_span_probe length %p count %p slots %p: %p want %p\n",
                                              length, shell_var_count, env_index_slots, got, want);
                }
                env_lookup = whole ? former : held;
        }
}

static fn shell_asm_variables(p8 address_to pages)
{
        static shell_asm_variable variables[96];
        static shell_asm_slot slots[256];
        static p8 texts[96][64];
        static p8 query[128];
        static const p8 letters[] = "ab_Z9";

        shell_vars = variables;
        env_index = slots;
        for (positive round = 0; round < 6000; round++)
        {
                positive count = shell_asm_next() % 90;
                positive shape = shell_asm_next() % 4;

                shell_asm_weak = shell_asm_next() % 3;
                shell_var_count = count;
                env_index_slots = shape == 0 ? 0 : shape == 1 ? 256 : 128;
                memory_fill(slots, 0, sizeof(slots));
                for (positive i = 0; i < count; i++)
                {
                        positive length = 1 + shell_asm_next() % (shell_asm_next() % 4 ? 12 : 40);
                        p8 address_to text = texts[i] + shell_asm_next() % 8;

                        for (positive c = 0; c < length; c++)
                                text[c] = letters[shell_asm_next() % 5];
                        text[length] = shell_asm_next() % 4 ? '=' : 0;
                        text[length + 1] = 0;
                        variables[i] = (shell_asm_variable){(string_address)text,
                                                            shell_asm_name_hash(text, length), length};
                        if (env_index_slots)
                        {
                                positive at = variables[i].hash & (env_index_slots - 1);

                                while (slots[at].index_plus_one)
                                        at = (at + 1) & (env_index_slots - 1);
                                slots[at] = (shell_asm_slot){variables[i].hash, length, i + 1};
                        }
                }
                // Tombstones, and slots that outlived the variables they named.
                if (env_index_slots)
                        for (positive i = 0; i < 12; i++)
                        {
                                positive at = shell_asm_next() % env_index_slots;

                                if (shell_asm_next() % 2)
                                        slots[at].index_plus_one = positive_max;
                                else if (!slots[at].index_plus_one)
                                        slots[at] = (shell_asm_slot){shell_asm_next() % 7, 1 + shell_asm_next() % 9,
                                                                     count + 1 + shell_asm_next() % 3};
                        }
                // A full table walks every slot.
                if (shape == 3)
                        for (positive at = 0; at < env_index_slots; at++)
                                if (!slots[at].index_plus_one)
                                        slots[at].index_plus_one = positive_max;

                for (positive q = 0; q < 24; q++)
                {
                        positive offset = shell_asm_next() % 16, length;
                        p8 address_to name = query + offset;

                        if (count && shell_asm_next() % 3)
                        {
                                shell_asm_variable address_to held = variables + shell_asm_next() % count;

                                length = held->name_length;
                                memory_copy_apart(name, held->text, length);
                                if (shell_asm_next() % 3 == 0)
                                        name[shell_asm_next() % length] ^= 1;
                                if (shell_asm_next() % 4 == 0)
                                        length -= shell_asm_next() % (length + 1);
                        }
                        else
                        {
                                length = shell_asm_next() % 20;
                                for (positive c = 0; c < length; c++)
                                        name[c] = letters[shell_asm_next() % 5];
                        }
                        name[length] = '=';
                        // The last hit: this name, another, stale, or none.
                        env_lookup.generation = shell_asm_next() % 3;
                        env_lookup.hit_generation = shell_asm_next() % 2 ? env_lookup.generation
                                                                          : shell_asm_next() % 3;
                        env_lookup.hit_index = count ? shell_asm_next() % (count + 2) : 0;
                        env_lookup.hit_hash = shell_asm_name_hash(name, length);
                        env_lookup.hit_length = shell_asm_next() % 4 ? length : length + 1;
                        shell_asm_find_one(name, length, shell_asm_name_hash(name, length));
                        shell_asm_find_one(name, length, shell_asm_name_hash(name, length));
                }

                /* A name, and a variable's text, ending on the last byte before
                   the page nobody may read. */
                if (pages && count)
                        for (positive length = 0; length < 12; length++)
                        {
                                p8 address_to name = pages + 8192 - length;
                                shell_asm_variable address_to held = variables + shell_asm_next() % count;
                                string_address text = held->text;

                                memory_copy_apart(name, held->text, held->name_length < length
                                                                        ? held->name_length : length);
                                shell_asm_find_one(name, length, shell_asm_name_hash(name, length));
                                if (held->name_length < 12)
                                {
                                        p8 address_to moved = pages + 8192 - held->name_length;

                                        memory_copy_apart(moved, text, held->name_length);
                                        held->text = (string_address)moved;
                                        shell_asm_find_one(name, length, shell_asm_name_hash(name, length));
                                        // Found last: the hit names this variable and its length.
                                        env_lookup.generation = env_lookup.hit_generation = 1;
                                        env_lookup.hit_index = (positive)(held - variables);
                                        env_lookup.hit_hash = held->hash;
                                        env_lookup.hit_length = held->name_length;
                                        shell_asm_find_one(moved, held->name_length, held->hash);
                                        held->text = text;
                                }
                        }
        }
}

/*
        exec_function_slot from src/sh/exec.c against the walk it replaced,
        which keeps its own copy of the slot found last: tables of up to two
        hundred functions, some unset, names that collide on a weak hash,
        the one found last valid, unset, gone or past the count, and names
        that end on the last byte before a page nobody may read.
*/
typedef struct
{
        p8 address_to name;
        positive name_room, name_hash, name_length;
        b32 body;
        positive active;
        p8 readonly, exported;
        p8 address_to environment;
        positive environment_room;
        p8 environment_valid;
        p8 special_kind;
} shell_asm_function;

shell_asm_function address_to exec_functions;
positive exec_function_count;
positive exec_function_recent;

positive exec_function_slot(string_address name, positive2 named);

static positive shell_asm_slot_former(positive address_to recent, const p8 address_to name,
                                      positive hash, positive length)
{
#define SHELL_ASM_MATCHES(index)                                                         (exec_functions[index].name_hash == hash &&                                     exec_functions[index].name_length == length &&                                 !memory_compare(exec_functions[index].name, name, length))
        if (address_to recent < exec_function_count && exec_functions[address_to recent].body &&
            SHELL_ASM_MATCHES(address_to recent))
                return address_to recent;
        for (positive index = 0; index < exec_function_count; index++)
        {
                if (index == address_to recent || !exec_functions[index].body)
                        continue;
                if (SHELL_ASM_MATCHES(index))
                {
                        address_to recent = index;
                        return index;
                }
        }
        return positive_max;
#undef SHELL_ASM_MATCHES
}

static fn shell_asm_slot_one(const p8 address_to name, positive length, positive hash)
{
        positive recent = exec_function_recent;
        positive want = shell_asm_slot_former(address_of recent, name, hash, length);
        positive2 named = {hash, length};
        positive got = exec_function_slot((string_address)name, named);

        checks++;
        if (want != got || recent != exec_function_recent)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL exec_function_slot length %p count %p: %p want %p\n",
                                      length, exec_function_count, got, want);
                exec_function_recent = recent;
        }
}

static fn shell_asm_functions(p8 address_to pages)
{
        static shell_asm_function functions[200];
        static p8 names[200][48];
        static p8 query[96];
        static const p8 letters[] = "func_ab";

        exec_functions = functions;
        for (positive round = 0; round < 3000; round++)
        {
                positive count = shell_asm_next() % 200;

                shell_asm_weak = shell_asm_next() % 3;
                exec_function_count = count;
                for (positive i = 0; i < count; i++)
                {
                        positive length = 1 + shell_asm_next() % (shell_asm_next() % 3 ? 10 : 40);
                        p8 address_to name = names[i] + shell_asm_next() % 8;

                        for (positive c = 0; c < length; c++)
                                name[c] = letters[shell_asm_next() % 7];
                        name[length] = 0;
                        functions[i] = (shell_asm_function){name, 48, shell_asm_name_hash(name, length),
                                                            length, shell_asm_next() % 6 ? 1 : 0};
                }
                for (positive q = 0; q < 24; q++)
                {
                        positive offset = shell_asm_next() % 16, length;
                        p8 address_to name = query + offset;

                        if (count && shell_asm_next() % 3)
                        {
                                shell_asm_function address_to held = functions + shell_asm_next() % count;

                                length = held->name_length;
                                memory_copy_apart(name, held->name, length);
                                if (shell_asm_next() % 4 == 0)
                                        name[shell_asm_next() % length] ^= 1;
                        }
                        else
                        {
                                length = shell_asm_next() % 24;
                                for (positive c = 0; c < length; c++)
                                        name[c] = letters[shell_asm_next() % 7];
                        }
                        name[length] = 0;
                        exec_function_recent = shell_asm_next() % 4 ? (count ? shell_asm_next() % (count + 2) : 0)
                                                                     : positive_max;
                        shell_asm_slot_one(name, length, shell_asm_name_hash(name, length));
                        shell_asm_slot_one(name, length, shell_asm_name_hash(name, length));
                }
                /* A name, and a function's own name, ending on the last byte
                   before the page nobody may read. */
                if (pages && count)
                        for (positive length = 0; length < 12; length++)
                        {
                                shell_asm_function address_to held = functions + shell_asm_next() % count;
                                p8 address_to name = pages + 8192 - held->name_length;
                                p8 address_to kept = held->name;

                                memory_copy_apart(name, kept, held->name_length);
                                shell_asm_slot_one(name, held->name_length, held->name_hash);
                                held->name = name;
                                exec_function_recent = (positive)(held - functions);
                                shell_asm_slot_one(kept, held->name_length, held->name_hash);
                                exec_function_recent = positive_max;
                                shell_asm_slot_one(kept, held->name_length, held->name_hash);
                                held->name = kept;
                        }
        }
}

/*
        shell_assignment_kind from src/sh/shell.c against the C it replaced,
        string_span over the name set and a look at what follows, over words
        drawn from the bytes a name, an assignment and a subscript are made
        of, at every alignment and ending on the last byte before a page
        nobody may read. The subscript walk is the lexer's and not what is
        under test: both take the same stand-in here, which closes at the
        first ] that an = or += follows.
*/
p8 shell_assignment_kind(string_address word, positive address_to name_length);

static string_address shell_asm_subscript_end(string_address at)
{
        for (; string_get(at); at++)
                if (string_is(at, ']') &&
                    (string_is(at + 1, '=') || (string_is(at + 1, '+') && string_is(at + 2, '='))))
                        return at + 1;
        return null;
}

KEEP p8 shell_assignment_subscript(string_address word, positive length,
                              positive address_to name_length)
{
        string_address stop = shell_asm_subscript_end(word + length + 1);

        if (stop && stop - (word + length) > 2)
                length = (positive)(stop - word);
        if (name_length)
                address_to name_length = length;
        if (string_get(word + length) == '=')
                return 1;
        return string_get(word + length) == '+' && string_get(word + length + 1) == '=' ? 2 : 0;
}

static p8 shell_asm_assignment_former(string_address word, positive address_to name_length)
{
        positive length = string_span(word, string_set_name);

        if (!length || (string_get(word) >= '0' && string_get(word) <= '9'))
        {
                if (name_length)
                        address_to name_length = length;
                return 0;
        }
        if (string_get(word + length) == '[')
        {
                string_address stop = shell_asm_subscript_end(word + length + 1);

                if (stop && stop - (word + length) > 2)
                        length = (positive)(stop - word);
        }
        if (name_length)
                address_to name_length = length;
        if (string_get(word + length) == '=')
                return 1;
        return string_get(word + length) == '+' && string_get(word + length + 1) == '=' ? 2 : 0;
}

/* parse_word_kind from src/sh/parse.c: the same words, both questions at
   once, against the two C answers it replaced, at the word's own length and
   shorter, since the name is walked to the terminator and the rest to the
   length. */
b8 expand_literal_set[STRING_SET_BYTES];
p8 parse_word_kind(string_address text, positive length, positive address_to name_length);

static p8 shell_asm_literal_former(string_address word, positive length)
{
        return string_span_max(word, length, expand_literal_set) == length ||
               (length == 1 && string_is(word, '['));
}

KEEP p8 parse_word_kind_subscript(string_address text, positive length, positive address_to name_length)
{
        return shell_assignment_kind(text, name_length) | (shell_asm_literal_former(text, length) ? 4 : 0);
}

static fn shell_asm_word_kind_one(const p8 address_to word, positive length)
{
        positive want_length = 77, got_length = 77;
        p8 want = shell_asm_assignment_former((string_address)word, address_of want_length) |
                  (shell_asm_literal_former((string_address)word, length) ? 4 : 0);
        p8 got = parse_word_kind((string_address)word, length, address_of got_length);

        checks++;
        if (want != got || want_length != got_length)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL parse_word_kind %s length %p: %p/%p want %p/%p\n",
                                      (string_address)word, length, (positive)got, got_length,
                                      (positive)want, want_length);
        }
}

static fn shell_asm_assignment_one(const p8 address_to word)
{
        positive whole = string_length((string_address)word);

        shell_asm_word_kind_one(word, whole);
        if (whole)
                shell_asm_word_kind_one(word, shell_asm_next() % whole);

        positive want_length = 77, got_length = 77;
        p8 want = shell_asm_assignment_former((string_address)word, address_of want_length);
        p8 got = shell_assignment_kind((string_address)word, address_of got_length);
        p8 bare = shell_assignment_kind((string_address)word, null);

        checks++;
        if (want != got || want != bare || want_length != got_length)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL shell_assignment_kind %s: %p/%p want %p/%p\n",
                                      (string_address)word, (positive)got, got_length,
                                      (positive)want, want_length);
        }
}

static fn shell_asm_assignments(p8 address_to pages)
{
        static p8 room[96];
        static const p8 bytes[] = "aZ_09=+[]$'\"- x\x80\xff*?{~<>(`\\";

        /* The set expand.c builds: every byte but these and the terminator. */
        memory_fill(expand_literal_set + 1, 1, STRING_SET_BYTES - 1);
        for (const char address_to at = "'\"\\$`*?[{~<>("; *at; at++)
                expand_literal_set[(p8)*at] = 0;

        for (positive round = 0; round < 300000; round++)
        {
                positive length = shell_asm_next() % (shell_asm_next() % 4 ? 12 : 60);
                positive offset = shell_asm_next() % 16;
                positive named = shell_asm_next() % 3 ? shell_asm_next() % (length + 1) : 0;
                p8 address_to word = room + offset;

                for (positive i = 0; i < length; i++)
                        word[i] = i < named ? "aZ_09bc"[shell_asm_next() % 7]
                                            : bytes[shell_asm_next() % (sizeof(bytes) - 1)];
                word[length] = 0;
                shell_asm_assignment_one(word);
        }
        if (pages)
                for (positive length = 0; length < 24; length++)
                        for (positive shape = 0; shape < 8; shape++)
                        {
                                p8 address_to word = pages + 8192 - length - 1;

                                for (positive i = 0; i < length; i++)
                                        word[i] = "ab_9"[(i + shape) % 4];
                                if (length && shape & 1)
                                        word[length - 1] = shape & 2 ? '=' : '+';
                                if (length > 1 && shape & 4)
                                        word[length - 2] = '[';
                                word[length] = 0;
                                shell_asm_assignment_one(word);
                        }
}

/*
        argument_option_take from src/lib.util.c against the four C functions
        it folds, run side by side over the same argument vector until it
        ends: tables of letters, grouped letters and long names that begin
        one another, every mode, and argument vectors of clusters, long names
        with and without =, prefixes, --, -, operands, values, empty words,
        a value missing at the end, and words that end before a page nobody
        may read. The answer, the whole match and the whole cursor are
        compared after every call.
*/
static b32 shell_asm_take_former(argument_cursor address_to cursor,
                                 const argument_option address_to options, p8 prefix,
                                 argument_match address_to match)
{
        b32 token = argument_next(cursor);
        *match = (argument_match){};
        if (token == ARGUMENT_END || token == ARGUMENT_OPERAND)
        {
                match->value = cursor->word;
                return token;
        }
        const argument_option address_to option = cursor->long_option
            ? argument_option_long(options, cursor->word + 2, cursor->name_length, prefix)
            : argument_option_short(options, (p8)token);
        match->letter = cursor->long_option ? (option ? option->letter : 0) : (p8)token;
        if (!option || (!cursor->long_option && (option->mode & ARGUMENT_LONG_ONLY)))
                return ARGUMENT_UNKNOWN;
        match->mode = option->mode;
        match->selection = option->selection;
        match->optional = (option->mode & ARGUMENT_OPTIONAL) ||
            (cursor->long_option && (option->mode & ARGUMENT_LONG_OPTIONAL));
        p8 required = (option->mode & ARGUMENT_REQUIRED) && !match->optional;
        if (cursor->attached && !match->optional && !required)
                return ARGUMENT_UNEXPECTED;
        if (required || match->optional)
        {
                match->value = argument_value(cursor, required);
                if (required && !match->value)
                        return ARGUMENT_MISSING;
        }
        return match->letter;
}

static fn shell_asm_take_run(string_address address_to argv, positive argc,
                             const argument_option address_to options, p8 prefix)
{
        argument_cursor want = {.argc = argc, .argv = argv, .at = shell_asm_next() % 2};
        argument_cursor got = want;

        for (positive call = 0; call < 64; call++)
        {
                argument_match want_match, got_match;

                memory_fill(address_of want_match, 0x5a, sizeof(want_match));
                memory_fill(address_of got_match, 0xa5, sizeof(got_match));
                b32 a = shell_asm_take_former(address_of want, options, prefix, address_of want_match);
                b32 b = argument_option_take(address_of got, options, prefix, address_of got_match);

                checks++;
                if (a != b || memory_compare(address_of want, address_of got, sizeof(want)) ||
                    want_match.value != got_match.value || want_match.selection != got_match.selection ||
                    want_match.letter != got_match.letter || want_match.mode != got_match.mode ||
                    want_match.optional != got_match.optional)
                {
                        failures++;
                        if (failures < 10)
                                string_format(log, "FAIL argument_option_take call %p word %p: %p want %p\n",
                                              call, want.at, (positive)(bipolar)b, (positive)(bipolar)a);
                        return;
                }
                if (a == ARGUMENT_END)
                        return;
        }
}

/*
        A long name that is exactly a row, with two rows that begin with it
        before it in the table. Prefix matching may take a unique prefix, but
        an exact name is the answer whatever order the rows come in, so the
        lookup must reach the exact row after the second prefix match. The
        randomized walk above compares against the C lookup it shares a source
        with, so an error both sides make is invisible to it; this case states
        the answer.
*/
static fn shell_asm_exact_after_prefixes(void)
{
        static const argument_option rows[] = {
            {(string_address)"ab-x", 'x', 0, 1},
            {(string_address)"ab-y", 'y', 0, 2},
            {(string_address)"ab", 'z', 0, 3},
            {null},
        };
        string_address argv[2] = {(string_address)"--ab", null};
        argument_cursor cursor = {.argc = 1, .argv = argv, .at = 0};
        argument_match match;
        b32 answer;

        memory_fill(address_of match, 0x5a, sizeof(match));
        answer = argument_option_take(address_of cursor, rows, true, address_of match);
        checks++;
        if (answer != 'z' || match.letter != 'z' || match.selection != 3)
        {
                failures++;
                string_format(log, "FAIL exact long name after two prefixes: %p\n",
                              (positive)(bipolar)answer);
        }
}

static fn shell_asm_arguments(p8 address_to pages)
{
        shell_asm_exact_after_prefixes();
        static argument_option table[24];
        static char names[24][16];
        static char words[16][24];
        static string_address argv[16];
        static const char letters[] = "abcdlnvxZ0-9";
        static const char address_to stems[] = {"all", "almost", "al", "block", "b", "color",
                                                "col", "x", "name", "number", "numeric-sort"};

        for (positive round = 0; round < 40000; round++)
        {
                positive rows = shell_asm_next() % 20;

                for (positive i = 0; i < rows; i++)
                {
                        const char address_to stem = stems[shell_asm_next() % array_count(stems)];
                        positive length = string_length((string_address)stem);

                        memory_copy_apart(names[i], stem, length + 1);
                        if (shell_asm_next() % 3 == 0 && length < 12)
                        {
                                names[i][length] = letters[shell_asm_next() % 12];
                                names[i][length + 1] = 0;
                        }
                        table[i] = (argument_option){(string_address)names[i],
                                                     shell_asm_next() % 5 ? (p8)letters[shell_asm_next() % 12] : 0,
                                                     (p8)(shell_asm_next() % 32),
                                                     (p16)shell_asm_next()};
                }
                table[rows] = (argument_option){null};

                positive argc = shell_asm_next() % 10;
                for (positive i = 0; i < argc; i++)
                {
                        char address_to word = words[i];
                        positive shape = shell_asm_next() % 8, at = 0;

                        if (shape < 3)
                        {
                                word[at++] = '-';
                                for (positive n = shell_asm_next() % 4; n; n--)
                                        word[at++] = letters[shell_asm_next() % 12];
                        }
                        else if (shape < 6)
                        {
                                const char address_to stem = stems[shell_asm_next() % array_count(stems)];
                                positive length = string_length((string_address)stem);

                                word[at++] = '-';
                                word[at++] = '-';
                                length -= shell_asm_next() % 2 && length > 1 ? shell_asm_next() % length : 0;
                                memory_copy_apart(word + at, stem, length);
                                at += length;
                                if (shell_asm_next() % 3 == 0)
                                {
                                        word[at++] = '=';
                                        if (shell_asm_next() % 2)
                                                word[at++] = 'v';
                                }
                        }
                        else if (shape == 6)
                        {
                                word[at++] = '-';
                                if (shell_asm_next() % 2)
                                        word[at++] = '-';
                        }
                        else
                                for (positive n = shell_asm_next() % 4; n; n--)
                                        word[at++] = "fv-="[shell_asm_next() % 4];
                        word[at] = 0;
                        argv[i] = (string_address)word;
                }
                shell_asm_take_run(argv, argc, table, shell_asm_next() % 2);
        }

        /* Words that end on the last byte before a page nobody may read. */
        if (pages)
        {
                static const char address_to ends[] = {"-a", "-abc", "--al", "--color", "--color=",
                                                       "--x=1", "--", "-", "file", "--numeric-sort"};

                for (positive i = 0; i < 20; i++)
                        table[i] = (argument_option){(string_address)stems[i % array_count(stems)],
                                                     (p8)letters[i % 12], (p8)(i % 16)};
                table[20] = (argument_option){(string_address)"abc", 0};
                table[21] = (argument_option){null};
                for (positive i = 0; i < array_count(ends); i++)
                {
                        positive length = string_length((string_address)ends[i]) + 1;
                        char address_to word = (char address_to)pages + 8192 - length;

                        memory_copy_apart(word, ends[i], length);
                        argv[0] = (string_address)word;
                        argv[1] = (string_address)word;
                        shell_asm_take_run(argv, 1 + i % 2, table, i % 2);
                }
        }
}

/*
        test_operator_kind from src/sh/builtin.c against the letter-pair walk it
        replaced, read the way that walk read it but never past a terminator.
        Every word of up to four bytes over the bytes an operator is made of
        and a few that are not, at every alignment, with the bytes after the
        terminator set and cleared; then each one ending on the last byte
        before a page nobody may read, where a four-byte read would fault.
*/
positive test_operator_kind(string_address word);

static positive shell_asm_binary_former(const p8 address_to word)
{
        p8 first, second = 0, third = 0, fourth = 0;

        if (!word)
                return 0;
        first = word[0];
        if (first)
                second = word[1];
        if (second)
                third = word[2];
        if (third)
                fourth = word[3];
        if (first == '-' && second && third && !fourth)
        {
                p16 pair = (p16)(second << 8 | third);

                if (pair == ('e' << 8 | 'q')) return 3;
                if (pair == ('n' << 8 | 'e')) return 4;
                if (pair == ('l' << 8 | 't')) return 5;
                if (pair == ('l' << 8 | 'e')) return 6;
                if (pair == ('g' << 8 | 't')) return 7;
                if (pair == ('g' << 8 | 'e')) return 8;
                if (pair == ('n' << 8 | 't')) return 9;
                if (pair == ('o' << 8 | 't')) return 10;
                if (pair == ('e' << 8 | 'f')) return 11;
        }
        if (!second)
                return first == '=' ? 1 : first == '<' ? 12 : first == '>' ? 13 : 0;
        if (!third)
        {
                if (first == '!' && second == '=')
                        return 2;
                if (first == '=' && second == '=')
                        return 1;
        }
        return 0;
}

/*
        The shell calls it from inline assembly that names the registers it
        may change, so the ones it does not name have to come back as they
        went in: this call fills every one of those with a marker first and
        says whether any came back different.
*/
positive shell_asm_kind_kept(string_address word, positive address_to changed);

#if X64
__asm__(
    ".text\n.globl shell_asm_kind_kept\n.type shell_asm_kind_kept, @function\n"
    "shell_asm_kind_kept:\n"
    "push %rbx\n   push %r12\n   push %r13\n"
    "mov %rsi, %r12\n   mov %rdi, %r13\n"
    "movabs $0x5151515151515151, %rsi\n   movabs $0x0808080808080808, %r8\n"
    "movabs $0x0909090909090909, %r9\n   movabs $0x1010101010101010, %r10\n"
    "movabs $0x1111111111111111, %r11\n"
    "call test_operator_kind\n"
    "xor %ebx, %ebx\n"
    "cmp %r13, %rdi\n   setne %cl\n   or %cl, %bl\n"
    "movabs $0x5151515151515151, %rdx\n   cmp %rdx, %rsi\n   setne %cl\n   or %cl, %bl\n"
    "movabs $0x0808080808080808, %rdx\n   cmp %rdx, %r8\n   setne %cl\n   or %cl, %bl\n"
    "movabs $0x0909090909090909, %rdx\n   cmp %rdx, %r9\n   setne %cl\n   or %cl, %bl\n"
    "movabs $0x1010101010101010, %rdx\n   cmp %rdx, %r10\n   setne %cl\n   or %cl, %bl\n"
    "movabs $0x1111111111111111, %rdx\n   cmp %rdx, %r11\n   setne %cl\n   or %cl, %bl\n"
    "mov %rbx, (%r12)\n"
    "pop %r13\n   pop %r12\n   pop %rbx\n   ret\n"
);
#elif ARM64
__asm__(
    ".text\n.globl shell_asm_kind_kept\n.type shell_asm_kind_kept, %function\n"
    "shell_asm_kind_kept:\n"
    "stp x29, x30, [sp, #-48]!\n   stp x19, x20, [sp, #16]\n   str x21, [sp, #32]\n"
    "mov x19, x1\n"
    "mov x4, #0x404\n   mov x5, #0x505\n   mov x6, #0x606\n   mov x7, #0x707\n"
    "mov x8, #0x808\n   mov x12, #0x1212\n   mov x13, #0x1313\n   mov x14, #0x1414\n"
    "mov x15, #0x1515\n"
    "bl test_operator_kind\n"
    "mov x20, #0\n"
    "mov x21, #0x404\n   cmp x4, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x505\n   cmp x5, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x606\n   cmp x6, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x707\n   cmp x7, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x808\n   cmp x8, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x1212\n   cmp x12, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x1313\n   cmp x13, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x1414\n   cmp x14, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "mov x21, #0x1515\n   cmp x15, x21\n   cset x21, ne\n   orr x20, x20, x21\n"
    "str x20, [x19]\n"
    "ldr x21, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #48\n   ret\n"
);
#elif RISCV64
__asm__(
    ".text\n.globl shell_asm_kind_kept\n.type shell_asm_kind_kept, %function\n"
    "shell_asm_kind_kept:\n"
    "addi sp, sp, -32\n   sd ra, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n"
    "mv s1, a1\n"
    "li a1, 0x101\n   li a2, 0x202\n   li a3, 0x303\n   li a4, 0x404\n"
    "li a5, 0x505\n   li a6, 0x606\n   li a7, 0x707\n"
    "call test_operator_kind\n"
    "li s2, 0\n"
    "addi a1, a1, -0x101\n   or s2, s2, a1\n"
    "addi a2, a2, -0x202\n   or s2, s2, a2\n"
    "addi a3, a3, -0x303\n   or s2, s2, a3\n"
    "addi a4, a4, -0x404\n   or s2, s2, a4\n"
    "addi a5, a5, -0x505\n   or s2, s2, a5\n"
    "addi a6, a6, -0x606\n   or s2, s2, a6\n"
    "addi a7, a7, -0x707\n   or s2, s2, a7\n"
    "sd s2, 0(s1)\n"
    "ld ra, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   addi sp, sp, 32\n   ret\n"
);
#endif

static fn shell_asm_binary_one(const p8 address_to word)
{
        positive want = shell_asm_binary_former(word);
        positive changed = 0;
        positive got = shell_asm_kind_kept((string_address)word, address_of changed);

        checks++;
        if (want != got || changed)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL test_operator_kind \"%s\": %p want %p%s\n",
                                      word ? (string_address)word : (string_address)"(null)", got, want,
                                      changed ? " (a register it may not change changed)" : "");
        }
}

static fn shell_asm_binaries(p8 address_to pages)
{
        static const p8 alphabet[] = {'-', '=', '!', '<', '>', 'e', 'q', 'n', 'l', 't',
                                      'g', 'o', 'f', 'x', 'E', 0x80, 0xed, 0xad};
        static p8 room[64];
        positive count = array_count(alphabet);

        shell_asm_binary_one(null);
        for (positive length = 0; length <= 4; length++)
        {
                positive total = 1;

                for (positive i = 0; i < length; i++)
                        total *= count;
                for (positive code = 0; code < total; code++)
                {
                        p8 word[5];
                        positive rest = code;

                        for (positive i = 0; i < length; i++)
                        {
                                word[i] = alphabet[rest % count];
                                rest /= count;
                        }
                        word[length] = 0;
                        for (positive offset = 0; offset < 8; offset += (code & 7) ? 7 : 1)
                                for (positive after = 0; after < 2; after++)
                                {
                                        memory_fill(room, after ? 0 : 'e', sizeof(room));
                                        memory_copy_apart(room + offset, word, length + 1);
                                        shell_asm_binary_one(room + offset);
                                }
                        if (pages && length < 4)
                        {
                                p8 address_to at = pages + 8192 - length - 1;

                                memory_copy_apart(at, word, length + 1);
                                shell_asm_binary_one(at);
                        }
                }
        }

        // Every operator, and a byte more, against the unreadable page.
        if (pages)
        {
                static const char address_to const operators[] = {
                    "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef",
                    "=", "==", "!=", "<", ">", "-", "", "-lte", "-l", "!", "=!"};

                for (positive i = 0; i < array_count(operators); i++)
                {
                        positive length = string_length((string_address)operators[i]);

                        for (positive back = length + 1; back <= length + 5; back++)
                        {
                                p8 address_to at = pages + 8192 - back;

                                memory_fill(at, 'q', back);
                                memory_copy_apart(at, operators[i], length + 1);
                                shell_asm_binary_one(at);
                        }
                }
        }

        // Random words of every length up to six, over all bytes.
        for (positive round = 0; round < 200000; round++)
        {
                positive length = shell_asm_next() % 7;
                positive offset = shell_asm_next() % 32;

                for (positive i = 0; i < length; i++)
                {
                        p8 byte = (p8)shell_asm_next();

                        room[offset + i] = byte ? byte : '-';
                }
                room[offset + length] = 0;
                room[offset + length + 1] = (p8)shell_asm_next();
                shell_asm_binary_one(room + offset);
        }
}

/*
        env_write_destination from src/sh/builtin.c: the assignment it takes
        and the ones it hands to the whole way. A variable, its cell and a
        value are built each round -- attributes, ownership, rooms around
        the sixteen bytes it stores and around what a long value needs, names
        of every length and PATH's and the locale names' lengths among them,
        values that are long, empty, inside the variable's own cell or ending
        on the last bytes before a page nobody may read, restricted and bash
        shells, allexport, a destination or an index past the table -- and
        the answer is checked
        against the rule: the cell's bytes up to the terminator, the fields,
        the environment's dirty mark, and every byte outside what it may
        write; or, for one it does not take, that env_write_whole was reached
        with all eight arguments as they were and nothing was touched.
*/
KEEP p8 shell_restricted, shell_bash_compat;
KEEP p8 shell_envp_dirty;
KEEP positive shell_options;

p8 env_write_destination(const_string name, positive name_len, positive hash, positive idx,
                           const_string value, p8 assignment,
                           shell_asm_variable address_to destination, p8 protect);

static struct
{
        p8 called;
        const_string name, value;
        positive name_len, hash, idx;
        p8 assignment, protect;
        shell_asm_variable address_to destination;
} shell_asm_whole;

KEEP p8 env_write_whole(const_string name, positive name_len, positive hash, positive idx,
                     const_string value, p8 assignment,
                     shell_asm_variable address_to destination, p8 protect)
{
        shell_asm_whole.called = true;
        shell_asm_whole.name = name;
        shell_asm_whole.name_len = name_len;
        shell_asm_whole.hash = hash;
        shell_asm_whole.idx = idx;
        shell_asm_whole.value = value;
        shell_asm_whole.assignment = assignment;
        shell_asm_whole.destination = destination;
        shell_asm_whole.protect = protect;
        return protect != assignment;
}

static fn shell_asm_write_fail(const char address_to what, positive round)
{
        failures++;
        if (failures < 10)
                string_format(log, "FAIL env_write_destination round %p: %s\n", round, what);
}

static fn shell_asm_writes(p8 address_to pages)
{
        static shell_asm_variable variables[8];
        static shell_asm_variable spare;
        static p8 cell[512];
        static p8 before[512];
        static p8 names[32];
        static p8 values[128];
        static const p8 letters[] = "abcPATHLC_xyz09";

        shell_vars = variables;
        for (positive round = 0; round < 400000; round++)
        {
                positive name_len = 1 + shell_asm_next() % 12;
                positive old_len = shell_asm_next() % 24;
                positive room = shell_asm_next() % 4 ? name_len + 10 + shell_asm_next() % 24
                                                      : 64 + shell_asm_next() % 200;
                positive where = shell_asm_next() % 16;
                positive value_len = shell_asm_next() % 3 ? shell_asm_next() % 16 : shell_asm_next() % 60;
                p8 use_destination = shell_asm_next() % 4 == 0;
                positive idx = shell_asm_next() % 10;
                shell_asm_variable address_to held;
                p8 address_to text;
                p8 address_to value;
                const p8 address_to name;
                positive hash = shell_asm_next();
                p8 assignment = shell_asm_next() % 2, protect = shell_asm_next() % 2;
                p8 answer;

                if (room < name_len + 1)
                        room = name_len + 1;
                if (room > sizeof(cell) - 64)
                        room = sizeof(cell) - 64;
                shell_var_count = shell_asm_next() % 9;
                shell_restricted = shell_asm_next() % 16 == 0;
                shell_bash_compat = shell_asm_next() % 16 == 0;
                shell_options = shell_asm_next() % 4;
                shell_envp_dirty = false;

                /* The cell: its header, the name, = and an old value, filler
                   to its room and a guard past it. */
                memory_fill(cell, 'G', sizeof(cell));
                ((positive address_to)(cell + 16))[0] = 0x1234;
                ((positive address_to)(cell + 16))[1] = room;
                text = cell + 32;
                for (positive i = 0; i < name_len; i++)
                        text[i] = letters[shell_asm_next() % (sizeof(letters) - 1)];
                memory_fill(text + name_len, 'Z', room - name_len);
                if (name_len + 1 + old_len < room)
                {
                        text[name_len] = '=';
                        memory_fill(text + name_len + 1, 'o', old_len);
                        text[name_len + 1 + old_len] = 0;
                }
                memory_copy_apart(names, text, name_len);
                names[name_len] = 0;
                name = shell_asm_next() % 64 ? names : null;

                held = use_destination ? &spare : variables + (idx < 8 ? idx : 0);
                for (positive i = 0; i < 8; i++)
                        variables[i] = (shell_asm_variable){(string_address)"x=y", 1, 1, 1, true};
                *held = (shell_asm_variable){(string_address)text, hash, name_len, old_len,
                                             shell_asm_next() % 8 != 0, shell_asm_next() % 2 == 0,
                                             shell_asm_next() % 2 == 0,
                                             (p8)(shell_asm_next() % 6 == 0 ? 1 << (shell_asm_next() % 8) : 0), 0};

                /* The value: in its own buffer, against the unreadable page,
                   inside the cell, or none. */
                if (where < 10)
                        value = values + shell_asm_next() % 64;
                else if (where < 13 && pages)
                        value = pages + 8192 - value_len - 1 - shell_asm_next() % 20;
                else if (where < 15)
                        value = text + name_len + 1 + shell_asm_next() % 4;
                else
                        value = null;
                if (value && (where < 13 || value + value_len + 1 <= text + room))
                {
                        for (positive i = 0; i < value_len; i++)
                                value[i] = (p8)('A' + shell_asm_next() % 26);
                        value[value_len] = 0;
                }

                /* The rule. */
                shell_asm_variable address_to variable = use_destination ? &spare
                    : idx < shell_var_count ? variables + idx : null;
                p8 fast = variable && !variable->attributes && variable->owned && name && value &&
                            !shell_restricted && !shell_bash_compat &&
                            !(name_len <= 8 && ((0x150u >> name_len) & 1));
                positive length = 0;
                if (fast)
                {
                        fast = (positive)(value - text) >= room && name_len + 17 <= room;
#if !RISCV64
                        fast = fast && ((positive)value & 4095) <= 4080;
#endif
                        while (fast && value[length])
                                length++;
                        fast = fast && name_len + 1 + length + 1 <= room;
                }

                shell_asm_variable kept = *held;
                memory_copy_apart(before, cell, sizeof(cell));
                shell_asm_whole.called = false;
                answer = env_write_destination((const_string)name, name_len, hash, idx, (const_string)value,
                                               assignment, use_destination ? &spare : null, protect);
                checks++;
                if (fast)
                {
                        p8 permanent = kept.permanent || (assignment && (shell_options & 1));
                        /* Up to sixteen bytes past the = may be stored whatever the length. */
                        positive stored = length + 1 > 16 ? length + 1 : 16;

                        if (!answer || shell_asm_whole.called)
                                shell_asm_write_fail("an assignment it takes went the whole way", round);
                        else if (text[name_len] != '=' || memory_compare(text + name_len + 1, value, length + 1))
                                shell_asm_write_fail("the value is not in the cell", round);
                        else if (memory_compare(cell, before, 32 + name_len) ||
                                 memory_compare(text + name_len + 1 + stored, before + 32 + name_len + 1 + stored,
                                                sizeof(cell) - 32 - name_len - 1 - stored))
                                shell_asm_write_fail("a byte outside the value changed", round);
                        else if (held->value_length != length || !held->declared ||
                                 held->permanent != permanent || held->text != kept.text ||
                                 held->owned != kept.owned || held->attributes != kept.attributes ||
                                 held->hash != kept.hash || held->name_length != kept.name_length)
                                shell_asm_write_fail("the variable's fields", round);
                        else if (shell_envp_dirty != (!use_destination && permanent))
                                shell_asm_write_fail("the environment's dirty mark", round);
                }
                else
                {
                        if (!shell_asm_whole.called || answer != (protect != assignment))
                                shell_asm_write_fail("one it does not take was not handed on", round);
                        else if (shell_asm_whole.name != (const_string)name || shell_asm_whole.name_len != name_len ||
                                 shell_asm_whole.hash != hash || shell_asm_whole.idx != idx ||
                                 shell_asm_whole.value != (const_string)value ||
                                 shell_asm_whole.assignment != assignment || shell_asm_whole.protect != protect ||
                                 shell_asm_whole.destination != (use_destination ? &spare : null))
                                shell_asm_write_fail("the arguments handed on changed", round);
                        else if (memory_compare(cell, before, sizeof(cell)) ||
                                 memory_compare(held, &kept, sizeof(kept)) || shell_envp_dirty)
                                shell_asm_write_fail("one it does not take was touched", round);
                }
        }
}

/*
        shell_name_index_probe from src/sh/builtin.c against the C probe it
        replaced, with memory_compare, over the word-sized slots the shell
        fills now: tables of a name and a function a row filled into an index
        the way the shell fills it, the first free slot from the hash's own,
        with hashes that collide on purpose -- the same slot, the same low half
        over different high halves, and the same hash and length over
        different bytes -- names of every length up to forty, lookups of every
        name, of names that differ in one byte, of hashes and lengths nothing
        has and of lengths past 255, and names that end on the last byte
        before a page nobody may read.
*/
typedef struct
{
        p32 hash;
        p8 length;
        p8 spare;
        p16 index_plus_one;
} shell_asm_name_slot;

positive shell_name_index_probe(string_address name, positive length, positive hash,
                                const shell_asm_name_slot address_to slots, positive mask,
                                address_any table);

typedef struct
{
        string_address name;
        address_any function;
} shell_asm_row;

static positive shell_asm_probe_former(const p8 address_to name, positive length, positive hash,
                                       const shell_asm_name_slot address_to slots, positive mask,
                                       const shell_asm_row address_to table)
{
        positive at = hash & mask;

        if (length > 255)
                return (positive)-1;
        for (positive probes = 0; probes <= mask; probes++)
        {
                const shell_asm_name_slot address_to slot = slots + at;

                if (!slot->index_plus_one)
                        return (positive)-1;
                if (slot->hash == (p32)hash && slot->length == length &&
                    !memory_compare(name, table[slot->index_plus_one - 1].name, length))
                        return slot->index_plus_one - 1;
                at = (at + 1) & mask;
        }
        return (positive)-1;
}

static fn shell_asm_probe_one(const p8 address_to name, positive length, positive hash,
                              const shell_asm_name_slot address_to slots, positive mask,
                              const shell_asm_row address_to table)
{
        positive want = shell_asm_probe_former(name, length, hash, slots, mask, table);
        positive got = shell_name_index_probe((string_address)name, length, hash, slots, mask,
                                              (address_any)table);

        checks++;
        if (want != got)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL shell_name_index_probe length %p hash %p: %p want %p\n",
                                      length, hash, got, want);
        }
}

static fn shell_asm_names(p8 address_to pages)
{
        static shell_asm_row table[300];
        static p8 names[300][48];
        static positive lengths[300], hashes[300];
        static shell_asm_name_slot slots[512];
        static p8 query[64];
        static const p8 letters[] = "abc-_9";

        for (positive round = 0; round < 3000; round++)
        {
                positive room = shell_asm_next() % 2 ? 512 : 256;
                positive count = 1 + shell_asm_next() % (room / 2 + 40);
                positive shape = shell_asm_next() % 5;

                if (count >= room)
                        count = room - 1;
                if (count > 300)
                        count = 300;
                memory_fill(slots, 0, sizeof(slots));
                for (positive i = 0; i < count; i++)
                {
                        positive length = shell_asm_next() % 4 ? 1 + shell_asm_next() % 12 : shell_asm_next() % 41;

                        for (positive j = 0; j < length; j++)
                                names[i][j] = letters[shell_asm_next() % (sizeof(letters) - 1)];
                        names[i][length] = 0;
                        lengths[i] = length;
                        /* A real hash, one of four slots, one hash for every name
                           of a length, a narrow hash, or one low half. */
                        hashes[i] = shape == 0 ? memory_hash_33(names[i], length)
                                  : shape == 1 ? (shell_asm_next() % 4) * room + (shell_asm_next() << 32)
                                  : shape == 2 ? 77 + length
                                  : shape == 3 ? memory_hash_33(names[i], length) & 0x3ff
                                               : 0x1234 + (shell_asm_next() << 32);
                        table[i] = (shell_asm_row){(string_address)names[i], null};
                        positive at = hashes[i] & (room - 1);
                        while (slots[at].index_plus_one)
                                at = (at + 1) & (room - 1);
                        slots[at] = (shell_asm_name_slot){(p32)hashes[i], (p8)length, 0, (p16)(i + 1)};
                }
                for (positive i = 0; i < count; i++)
                {
                        positive length = lengths[i];
                        positive offset = shell_asm_next() % 16;

                        memory_copy_apart(query + offset, names[i], length + 1);
                        shell_asm_probe_one(query + offset, length, hashes[i], slots, room - 1, table);
                        if (length)
                        {
                                /* One byte different, under the same hash and length. */
                                positive at = shell_asm_next() % length;

                                query[offset + at] ^= 0x20;
                                shell_asm_probe_one(query + offset, length, hashes[i], slots, room - 1, table);
                                query[offset + at] ^= 0x20;
                        }
                        /* A hash nothing has, a length nothing under this hash
                           has, the high half changed, and too long a length. */
                        shell_asm_probe_one(query + offset, length, hashes[i] ^ 0x5a5a0000ull, slots, room - 1, table);
                        shell_asm_probe_one(query + offset, length + 1, hashes[i], slots, room - 1, table);
                        shell_asm_probe_one(query + offset, length, hashes[i] ^ (0x77ull << 40), slots, room - 1, table);
                        shell_asm_probe_one(query + offset, length + 256, hashes[i], slots, room - 1, table);
                        if (pages && length < 48)
                        {
                                p8 address_to last = pages + 8192 - length;

                                memory_copy_apart(last, names[i], length);
                                shell_asm_probe_one(last, length, hashes[i], slots, room - 1, table);
                        }
                }
        }
}

/*
        shell_key_next from src/sh/builtin.c against the loop it replaces,
        over tables of every count to 300 (and every count that is a whole
        number of vectors, of words and one short of them), from every place
        in the table, with keys drawn from a few values so that matches repeat,
        values one apart and differing in one bit so the borrow of the word
        test has something to be wrong about, the high bit of a halfword and
        all ones, a key with garbage above its sixteen bits, and the table
        ending on the last byte before a page nobody may read.
*/
positive shell_key_next(const void address_to keys, positive count, positive from, positive key);

static fn shell_asm_keys(p8 address_to pages)
{
        static p16 own[320] __attribute__((aligned(8)));
        static p16 pool[48];

        for (positive round = 0; round < 6000; round++)
        {
                positive count = shell_asm_next() % 301;
                positive alphabet = 1 + shell_asm_next() % 40;
                p16 address_to keys = own;

                if (round % 4 == 0)
                        count &= ~(positive)15;
                else if (round % 4 == 1)
                        count = (count & ~(positive)3) + 3;
                if (count > 300)
                        count = 300;
                if (pages && round % 2)
                        keys = (p16 address_to)(((positive)(pages + 8192 - count * 2)) & ~(positive)7);
                for (positive j = 0; j < alphabet; j++)
                {
                        p64 roll = shell_asm_next();

                        pool[j] = j % 4 == 0 ? (p16)roll
                                : j % 4 == 1 ? (p16)(pool[j - 1] + 1)
                                : j % 4 == 2 ? (p16)(pool[j - 2] ^ 0x8000)
                                             : (p16)(pool[j - 3] - 1);
                        if (roll >> 60 == 0)
                                pool[j] = 0xffff;
                        else if (roll >> 60 == 1)
                                pool[j] = 0;
                }
                for (positive i = 0; i < count; i++)
                        keys[i] = pool[shell_asm_next() % alphabet];

                positive key = pool[shell_asm_next() % alphabet];
                positive step = count < 40 ? 1 : 1 + shell_asm_next() % 13;

                for (positive from = 0; from <= count; from += step)
                {
                        positive want = count;
                        positive ask = shell_asm_next() % 3 ? key : (key | shell_asm_next() << 16);

                        for (positive i = from; i < count; i++)
                                if (keys[i] == key)
                                {
                                        want = i;
                                        break;
                                }
                        positive got = shell_key_next(keys, count, from, ask);

                        checks++;
                        if (got != want)
                        {
                                failures++;
                                if (failures < 10)
                                        string_format(log, "FAIL shell_key_next count %p from %p key %p: %p want %p\n",
                                                      count, from, key, got, want);
                        }
                }
        }
}

/*
        arith_plain_natural from src/sh/expand.c against the C it replaced,
        which asked at every digit whether the next would overflow: every
        byte after 7, 0 and 123, every pair and triple of bytes a literal,
        a name and a base are made of, digit runs of every length to 24 with
        the boundaries of a signed word among them, at every alignment and
        ending on the last byte before a page nobody may read. The routine
        answers in two registers, the value and the end, both zero for a
        refusal; the former answered through two addresses and left them
        alone when it refused.
*/
positive2 arith_plain_natural(string_address at);

static p8 shell_asm_natural_former(string_address address_to at,
                                     bipolar address_to value)
{
        string_address step = address_to at;
        p8 seen = string_get(step);
        positive held = 0;

        if (seen < '0' || seen > '9')
                return false;

        if (seen == '0')
        {
                p8 next = string_get(step + 1);

                if (byte_is_digit(next) || next == 'x' || next == 'X' ||
                    next == '#' || byte_is_alnum(next) || next == '_')
                        return false;

                address_to at = step + 1;
                address_to value = 0;
                return true;
        }

        do
        {
                positive digit = (positive)(seen - '0');

                if (held > (positive)bipolar_max / 10 ||
                    (held == (positive)bipolar_max / 10 &&
                     digit > (positive)bipolar_max % 10))
                        return false;

                held = held * 10 + digit;
                step++;
                seen = string_get(step);
        } while (byte_is_digit(seen));

        if (byte_is_alnum(seen) || seen == '_')
                return false;

        address_to at = step;
        address_to value = (bipolar)held;
        return true;
}

static fn shell_asm_natural_one(const p8 address_to text)
{
        string_address want_at = (string_address)text;
        bipolar want_value = 0;
        p8 want = shell_asm_natural_former(address_of want_at, address_of want_value);
        positive2 answer = arith_plain_natural((string_address)text);
        p8 got = answer.y != 0;
        string_address got_at = got ? (string_address)answer.y : (string_address)text;
        bipolar got_value = (bipolar)answer.x;

        if (!want)
                want_value = 0;
        if (!got && answer.x)
                got_value = -1;

        checks++;
        if (want != got || want_at != got_at || want_value != got_value)
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL arith_plain_natural %s: %p at +%p value %p want %p at +%p value %p\n",
                                      (string_address)text, (positive)got,
                                      (positive)(got_at - (string_address)text), (positive)got_value,
                                      (positive)want, (positive)(want_at - (string_address)text),
                                      (positive)want_value);
        }
}

static fn shell_asm_naturals(p8 address_to pages)
{
        static p8 room[64];
        static const p8 bytes[] = "0123456789aZxX_#@ +-)\x80\xff";
        static const char address_to edges[] = {
                "9223372036854775807", "9223372036854775808", "9223372036854775806",
                "9999999999999999999", "10000000000000000000", "18446744073709551615",
                "18446744073709551616", "922337203685477580", "922337203685477581",
                "999999999999999999", "1000000000000000000", "00", "0", "01", "0x1",
        };
        positive length;

        for (positive lead = 0; lead < 3; lead++)
                for (positive byte = 0; byte < 256; byte++)
                {
                        static const char address_to leads[] = {"7", "0", "123"};

                        length = string_length((string_address)leads[lead]);
                        memory_copy_apart(room, leads[lead], length);
                        room[length] = (p8)byte;
                        room[length + 1] = 0;
                        shell_asm_natural_one(room);
                        room[0] = (p8)byte;
                        room[1] = 0;
                        shell_asm_natural_one(room);
                }

        for (positive a = 0; a < sizeof(bytes) - 1; a++)
                for (positive b = 0; b < sizeof(bytes); b++)
                        for (positive c = 0; c < sizeof(bytes); c++)
                        {
                                room[0] = bytes[a];
                                room[1] = b < sizeof(bytes) - 1 ? bytes[b] : 0;
                                room[2] = c < sizeof(bytes) - 1 ? bytes[c] : 0;
                                room[3] = 0;
                                shell_asm_natural_one(room);
                        }

        for (positive i = 0; i < array_count(edges); i++)
                for (positive tail = 0; tail < 4; tail++)
                {
                        length = string_length((string_address)edges[i]);
                        memory_copy_apart(room, edges[i], length);
                        room[length] = "\0a#)"[tail];
                        room[length + 1] = 0;
                        shell_asm_natural_one(room);
                }

        for (positive round = 0; round < 200000; round++)
        {
                positive digits = 1 + shell_asm_next() % 24;
                positive offset = shell_asm_next() % 16;
                p8 address_to text = room + offset;

                for (positive i = 0; i < digits; i++)
                        text[i] = (p8)('0' + shell_asm_next() % 10);
                if (shell_asm_next() % 2)
                        text[0] = (p8)('1' + shell_asm_next() % 9);
                text[digits] = shell_asm_next() % 3 ? bytes[shell_asm_next() % (sizeof(bytes) - 1)] : 0;
                text[digits + 1] = 0;
                shell_asm_natural_one(text);
        }

        if (pages)
                for (length = 0; length < 24; length++)
                        for (positive shape = 0; shape < 4; shape++)
                        {
                                p8 address_to text = pages + 8192 - length - 1;

                                for (positive i = 0; i < length; i++)
                                        text[i] = (p8)('1' + (i + shape) % 9);
                                if (length && shape == 3)
                                        text[0] = '0';
                                text[length] = 0;
                                shell_asm_natural_one(text);
                        }
}

/*
        arith_plain_scalar from src/sh/expand.c against the C it replaced,
        blanks, a sign, the former literal reader and blanks to the end:
        every text of up to five bytes drawn from blanks, signs, digits and
        the bytes that end a number, the signed-word boundaries with signs
        and padding around them, random padded digit runs, a null text, at
        every alignment and ending before an unreadable page.
*/
positive2 arith_plain_scalar(string_address text);

static p8 shell_asm_scalar_former(string_address text, bipolar address_to value)
{
        string_address step;
        p8 negative = false;
        bipolar magnitude;

        if (!text)
                return false;

        step = text + string_span(text, string_set_blanks);
        if (!string_get(step))
        {
                address_to value = 0;
                return true;
        }

        if (string_is(step, '-') || string_is(step, '+'))
        {
                negative = string_is(step, '-');
                step++;
        }

        if (!shell_asm_natural_former(address_of step, address_of magnitude))
                return false;

        step += string_span(step, string_set_blanks);
        if (string_get(step))
                return false;

        address_to value = negative ? (bipolar)(0 - (positive)magnitude) : magnitude;
        return true;
}

static fn shell_asm_scalar_one(const p8 address_to text)
{
        bipolar want_value = 0;
        p8 want = shell_asm_scalar_former((string_address)text, address_of want_value);
        positive2 answer = arith_plain_scalar((string_address)text);

        checks++;
        if (answer.y != (positive)want || (bipolar)answer.x != (want ? want_value : 0))
        {
                failures++;
                if (failures < 10)
                        string_format(log, "FAIL arith_plain_scalar [%s]: %p %p want %p %p\n",
                                      text ? (string_address)text : (string_address)"(null)",
                                      answer.y, answer.x, (positive)want, (positive)want_value);
        }
}

static fn shell_asm_scalars(p8 address_to pages)
{
        static p8 room[96];
        static const p8 bytes[] = " \t-+0179a#_x\n)";
        static const char address_to edges[] = {
                "9223372036854775807", "9223372036854775808", "9999999999999999999",
                "10000000000000000000", "18446744073709551616", "999999999999999999",
                "0", "00", "010",
        };
        positive count = sizeof(bytes) - 1;

        shell_asm_scalar_one(null);
        for (positive length = 0; length <= 5; length++)
        {
                positive total = 1;

                for (positive i = 0; i < length; i++)
                        total *= count;
                for (positive n = 0; n < total; n++)
                {
                        positive left = n;

                        for (positive i = 0; i < length; i++, left /= count)
                                room[i] = bytes[left % count];
                        room[length] = 0;
                        shell_asm_scalar_one(room);
                }
        }

        for (positive i = 0; i < array_count(edges); i++)
                for (positive shape = 0; shape < 16; shape++)
                {
                        positive at = 0, length = string_length((string_address)edges[i]);

                        if (shape & 1)
                                room[at++] = ' ';
                        if (shape & 2)
                                room[at++] = '\t';
                        if (shape & 4)
                                room[at++] = shape & 8 ? '-' : '+';
                        else if (shape & 8)
                                room[at++] = '-';
                        memory_copy_apart(room + at, edges[i], length);
                        at += length;
                        if (shape & 1)
                                room[at++] = '\t';
                        room[at] = 0;
                        shell_asm_scalar_one(room);
                }

        for (positive round = 0; round < 200000; round++)
        {
                positive offset = shell_asm_next() % 16, at = 0;
                p8 address_to text = room + offset;

                for (positive n = shell_asm_next() % 3; n; n--)
                        text[at++] = shell_asm_next() % 2 ? ' ' : '\t';
                if (shell_asm_next() % 3 == 0)
                        text[at++] = shell_asm_next() % 2 ? '-' : '+';
                for (positive n = shell_asm_next() % 22; n; n--)
                        text[at++] = (p8)('0' + shell_asm_next() % 10);
                for (positive n = shell_asm_next() % 3; n; n--)
                        text[at++] = bytes[shell_asm_next() % 3];
                if (shell_asm_next() % 8 == 0)
                        text[at++] = bytes[shell_asm_next() % count];
                text[at] = 0;
                shell_asm_scalar_one(text);
        }

        if (pages)
                for (positive length = 0; length < 24; length++)
                        for (positive shape = 0; shape < 4; shape++)
                        {
                                p8 address_to text = pages + 8192 - length - 1;

                                for (positive i = 0; i < length; i++)
                                        text[i] = (p8)('1' + (i + shape) % 9);
                                if (length && shape == 1)
                                        text[0] = '-';
                                if (length && shape == 2)
                                        text[length - 1] = ' ';
                                if (length && shape == 3)
                                        text[0] = ' ';
                                text[length] = 0;
                                shell_asm_scalar_one(text);
                        }
}

b32 main(void)
{
        static p8 built[4096];
        static p8 room[4096 + 128];
        p8 address_to pages = memory(3 * 4096);
        positive valid = 0;

        for (positive round = 0; round < 3000; round++)
        {
                positive length = shell_asm_build(built);

                if (shell_asm_former(built, length))
                        valid++;
                for (positive offset = 0; offset < 64; offset += round % 4 ? 17 : 1)
                {
                        memory_copy_apart(room + offset, built, length);
                        shell_asm_one(room + offset, length, round);
                }
                for (positive edits = 0; edits < 6; edits++)
                {
                        length = shell_asm_edit(built, length, sizeof(built));
                        memory_copy_apart(room + (edits & 31), built, length);
                        shell_asm_one(room + (edits & 31), length, round);
                }
        }

        /* A file that ends on the last byte of a page, and one that starts on
           the first, against a page nobody may read. */
        if ((bipolar)(positive)pages > 0 &&
            system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0)
                for (positive round = 0; round < 400; round++)
                {
                        positive length = shell_asm_build(built);

                        if (length > 4096)
                                continue;
                        memory_copy_apart(pages + 8192 - length, built, length);
                        shell_asm_one(pages + 8192 - length, length, round);
                        memory_copy_apart(pages + 4096, built, length);
                        shell_asm_one(pages + 4096, length, round);
                }

        checks++;
        if (valid < 300)
        {
                failures++;
                string_format(log, "FAIL only %p of the built files were clear\n", valid);
        }

        shell_asm_keywords((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_variables((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_functions((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_assignments((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_arguments((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_binaries((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_writes((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_names((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_keys((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_naturals((bipolar)(positive)pages > 0 ? pages : null);
        shell_asm_scalars((bipolar)(positive)pages > 0 ? pages : null);

        string_format(log, "shell assembly: %p checks, %p failures\n", checks, failures);
        log_flush();
        return failures != 0;
}
#endif /* CHECK_shell_asm */

#ifdef CHECK_hash_length
#include "../../src/lib.util.c"
/*
        The fused NUL-name pass at every alignment and a protected page edge.

        Its word path may only begin after alignment: a load that crosses the
        terminator into the second page faults instead of being hidden by an
        ordinary mapped neighbour.  The byte model also proves that the hash
        excludes NUL and that the second return register is the exact length.
*/

#define PAGE 4096
#define PROT_NONE 0

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static positive model_hash(const p8 address_to text, positive length)
{
        positive hash = 5381;

        for (positive at = 0; at < length; at++)
                hash = hash * 33 + text[at];

        return hash;
}

b32 main(void)
{
        p8 address_to room = memory(PAGE * 2);
        p8 address_to edge;

        if (!room)
                return 1;

        edge = room + PAGE;
        if (system_call_3(syscall(mprotect), (positive)edge, PAGE,
                          PROT_NONE) < 0)
                return 1;

        for (positive length = 0; length <= 257; length++)
        {
                p8 address_to text = edge - length - 1;
                positive2 got;
                positive want;

                for (positive at = 0; at < length; at++)
                        text[at] = (p8)(at % 251 + 1);
                text[length] = 0;

                want = model_hash(text, length);
                got = string_hash_33_length((string_address)text);
                checks += 2;

                if (got.x != want)
                {
                        failures++;
                        if (failures < 20)
                                string_format(log,
                                              "FAIL hash length %p: %p != %p\n",
                                              length, got.x, want);
                }

                if (got.y != length)
                {
                        failures++;
                        if (failures < 20)
                                string_format(log,
                                              "FAIL length %p: %p != %p\n",
                                              length, got.y, length);
                }
        }

        // Every start within a word, bytes from the whole range, and bytes
        // after the terminator that are not zero, so a word holding the end
        // of a name holds something after it too.
        p8 address_to middle = room + 1024;

        for (positive offset = 0; offset < 16; offset++)
                for (positive length = 0; length <= 40; length++)
                {
                        p8 address_to text = middle + offset;
                        positive2 got;

                        for (positive at = 0; at < length + 16; at++)
                                text[at] = (p8)((at * 97 + offset * 13 + length) % 255 + 1);
                        text[length] = 0;

                        got = string_hash_33_length((string_address)text);
                        checks += 2;

                        if (got.x != model_hash(text, length) || got.y != length)
                        {
                                failures++;
                                if (failures < 20)
                                        string_format(log, "FAIL offset %p length %p\n",
                                                      offset, length);
                        }
                }

        return test_report((string_address) "hash+length page edge: ");
}
#endif /* CHECK_hash_length */

#ifdef CHECK_exact_base
#include "../../src/lib.util.c"
/*
        Every base that is known where the call is written.

        string_to_number, string_to_number_unsigned and positive_into_base
        expand in place when the base is a literal, and the line they expand
        to depends on the number: base ten has no letter path at all, sixteen
        accumulates by shift, thirty six divides by a constant. Nothing that
        takes its base from a variable can reach any of that, because the
        choice is made by the compiler, from the token. So every base is
        written out here, one call per base, and the answer is compared
        against the routine reached through a base the compiler cannot see.

        The parse is checked on a list of subjects that covers what the
        contract says and what it is easy to get wrong: leading space, both
        signs, a hexadecimal prefix with and without a digit after it, the two
        saturation edges from either side, a string that is nothing but a
        sign, and a digit that belongs to a larger base than the one asked
        for. Both the answer and the end pointer are compared, and the parse
        is run a third time with a null end pointer, because that is a
        separate expansion: the store is a branch on a pointer the compiler
        has folded away.

        positive_into_base writes into a buffer with guard bytes either side,
        and both halves are looked at: that the bytes asked for arrived, and
        that the bytes either side did not move. A base class that reaches one
        byte too far writes into a guard, and one that stops a byte short
        leaves the guard pattern showing inside the answer. The values are the
        neighbours of every carry the conversion has a case for, and then a
        long pseudorandom sweep shifted by every width, because a carry that
        only shows at one bit pattern is what a table of round numbers misses.
*/

#define GUARD 32
#define ROOM 72
#define SWEEP 20000

static p8 routine_arena[GUARD + ROOM + GUARD];
static p8 known_arena[GUARD + ROOM + GUARD];

#define ROUTINE_FIELD (routine_arena + GUARD)
#define KNOWN_FIELD (known_arena + GUARD)

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

//      The base the routine is handed. Volatile so that the macro above
//      cannot fold it and the general path is what answers.
static volatile b32 hidden;

//      The guard pattern differs at every index so a write that lands one
//      byte off is visible.
static p8 guard_byte(positive at) { return (p8)(0x5a ^ (at * 7)); }

static fn fail(const p8 address_to what, positive base, positive one,
               positive two)
{
        failures++;
        if (failures < 40)
                string_format(log, "FAIL %s base %p: routine %p known %p\n",
                              what, base, one, two);
}

//      Both arenas, guards laid and field wiped, before every conversion.
static fn prepare(void)
{
        for (positive at = 0; at < sizeof(routine_arena); at++)
        {
                routine_arena[at] = guard_byte(at);
                known_arena[at] = guard_byte(at);
        }
}

static fn guards_held(const p8 address_to what, positive base)
{
        for (positive at = 0; at < GUARD; at++)
        {
                checks++;
                if (known_arena[at] != guard_byte(at))
                        fail(what, base, at, known_arena[at]);
                checks++;
                if (known_arena[GUARD + ROOM + at] !=
                    guard_byte(GUARD + ROOM + at))
                        fail(what, base, GUARD + ROOM + at,
                             known_arena[GUARD + ROOM + at]);
        }
}

//      One conversion, both ways, with the length, every written byte and
//      both guards compared.
static fn agree_into(positive value, positive base, p8 upper, positive known)
{
        positive was;

        hidden = (b32)base;
        was = positive_into_base(ROUTINE_FIELD, value, (positive)hidden, upper);

        checks++;
        if (was != known)
                fail((const p8 address_to)"into length", base, was, known);

        for (positive at = 0; at < ROOM; at++)
        {
                checks++;
                if (routine_arena[GUARD + at] != known_arena[GUARD + at])
                {
                        fail((const p8 address_to)"into byte", base,
                             routine_arena[GUARD + at],
                             known_arena[GUARD + at]);
                        break;
                }
        }

        guards_held((const p8 address_to)"into guard", base);
}

/*
        One base, written as a token, so the macro fires. The routine side is
        reached through the volatile above, which the macro cannot fold, so
        the two sides of the comparison are genuinely the two paths.
*/
#define INTO(base)                                                            \
        do                                                                    \
        {                                                                     \
                prepare();                                                    \
                agree_into(value, (base), upper,                              \
                           positive_into_base(KNOWN_FIELD, value, (base),      \
                                              upper));                        \
        } while (0)

static fn into_sweep(positive value, p8 upper)
{
        INTO(2);  INTO(3);  INTO(4);  INTO(5);  INTO(6);  INTO(7);
        INTO(8);  INTO(9);  INTO(10); INTO(11); INTO(12); INTO(13);
        INTO(14); INTO(15); INTO(16); INTO(17); INTO(18); INTO(19);
        INTO(20); INTO(21); INTO(22); INTO(23); INTO(24); INTO(25);
        INTO(26); INTO(27); INTO(28); INTO(29); INTO(30); INTO(31);
        INTO(32); INTO(33); INTO(34); INTO(35); INTO(36);
}

/*
        The parse, on one subject, at one base written as a token: the signed
        answer and its end pointer, the unsigned answer and its end pointer,
        and the signed answer again with no end pointer at all, which is the
        second fold and a different line of code.
*/
#define PARSE(base)                                                           \
        do                                                                    \
        {                                                                     \
                string_address was_stop = null;                               \
                string_address now_stop = null;                               \
                positive was;                                                 \
                positive now;                                                 \
                                                                              \
                hidden = (base);                                              \
                was = (positive)string_to_number(subject, address_of was_stop, \
                                                 hidden);                     \
                now = (positive)string_to_number(subject,                     \
                                                 address_of now_stop, (base)); \
                checks++;                                                     \
                if (was != now)                                               \
                        fail((const p8 address_to)"signed", (base), was, now); \
                checks++;                                                     \
                if (was_stop != now_stop)                                     \
                        fail((const p8 address_to)"signed end", (base),        \
                             (positive)(was_stop - subject),                  \
                             (positive)(now_stop - subject));                 \
                                                                              \
                was_stop = null;                                              \
                now_stop = null;                                              \
                hidden = (base);                                              \
                was = string_to_number_unsigned(subject,                       \
                                                address_of was_stop, hidden);  \
                now = string_to_number_unsigned(subject,                       \
                                                address_of now_stop, (base));  \
                checks++;                                                     \
                if (was != now)                                               \
                        fail((const p8 address_to)"unsigned", (base), was,     \
                             now);                                            \
                checks++;                                                     \
                if (was_stop != now_stop)                                      \
                        fail((const p8 address_to)"unsigned end", (base),      \
                             (positive)(was_stop - subject),                  \
                             (positive)(now_stop - subject));                 \
                                                                              \
                hidden = (base);                                              \
                was = (positive)string_to_number(subject, null, hidden);       \
                now = (positive)string_to_number(subject, null, (base));       \
                checks++;                                                     \
                if (was != now)                                               \
                        fail((const p8 address_to)"no end pointer", (base),    \
                             was, now);                                        \
        } while (0)

static fn parse_sweep(string_address subject)
{
        PARSE(2);  PARSE(3);  PARSE(4);  PARSE(5);  PARSE(6);
        PARSE(7);  PARSE(8);  PARSE(9);  PARSE(10); PARSE(16);
}

//      Everything the two have to agree on, and the reason each one is here
//      is in the shape of it rather than in a comment per line.
static const p8 address_to subjects[] = {
        "", " ", "   \t\n\v\f\r 42", "0", "00", "000", "7", "42", "-42", "+42",
        "  -0007", "9", "10", "99", "100", "2147483647", "2147483648",
        "4294967295", "9223372036854775807", "9223372036854775808",
        "18446744073709551615", "18446744073709551616",
        "-9223372036854775808", "-9223372036854775809",
        "99999999999999999999999999", "-99999999999999999999999999",
        "-1", "-0", "+0", "abc", "-abc", "+", "-", " + 7", "12a34", "1_2",
        "0x", "0X", "0x1", "0xg", "0Xff", "0xFF", "0xffffffffffffffff",
        "0x10000000000000000", "-0x20", "0x0", "0x00", "  \t 0x", "\n-0x1f",
        " 0x7fffffffffffffff", "0xdeadBEEF", "007", "089", "0b101", "0o17",
        "z", "Z", "F", "f", "g", "G", "9999999999999999999999",
        "1111111111111111", "11111111111111111", "77777777777777777777777",
        "3735928559", "111111111111111111111111111111111111111111111111",
};

b32 main(void)
{
        moonwater_cpu_detect();

        for (positive which = 0;
             which < sizeof(subjects) / sizeof(subjects[0]); which++)
                parse_sweep((string_address)subjects[which]);

        //      Every value the conversion has a case for, and the neighbours
        //      of every carry it could get wrong.
        static const positive values[] = {
                0, 1, 2, 3, 7, 8, 9, 10, 15, 16, 31, 32, 35, 36, 63, 64, 99,
                100, 255, 256, 999, 1000, 1023, 1024, 9999, 10000, 65535,
                65536, 99999999, 100000000, 4294967295UL, 4294967296UL,
                9223372036854775807UL, 9223372036854775808UL,
                18446744073709551615UL, 1844674407370955161UL,
                1844674407370955162UL,
        };

        for (positive which = 0;
             which < sizeof(values) / sizeof(values[0]); which++)
        {
                into_sweep(values[which], 0);
                into_sweep(values[which], 1);
        }

        positive seed = 0x243f6a8885a308d3UL;

        for (positive which = 0; which < SWEEP; which++)
        {
                seed = seed * 6364136223846793005UL + 1442695040888963407UL;
                into_sweep(seed >> (which & 63), (p8)(which & 1));
        }

        return test_report(null);
}
#endif /* CHECK_exact_base */

#ifdef CHECK_single
#include "../../src/lib.util.c"
/*
        Every value that is known where the call is written.

        The single-value routines take a byte class or a word and answer from
        it directly, so what a literal folds here is not a shorter line of
        code but the whole call: byte_is_digit('7') is the token 1 and there
        is nothing left to run. Nothing that takes its value from a loop
        counter reaches any of that, because the choice is made by the
        compiler, from the token, so every value is written out here.

        There is no buffer and nothing is written, so exact.c's guard bytes
        have no counterpart. What takes their place is the range: an
        expansion that agrees with the routine on the printable bytes and
        disagrees on EOF, or on a byte plus two hundred and fifty six, or on
        the two ends of an int, is the failure this is looking for, and each
        of those is a literal here beside the two hundred and fifty six that
        a caller would actually write.

        The routine is called through a parenthesised name, which is how a
        function-like macro is stepped around: (byte_is_digit)(v) is the call
        and byte_is_digit(v) is whatever the macro makes of it, so both sides
        of every comparison below are the thing they claim to be.
*/

positive failures = 0;

void report(string_address name, bipolar value, b32 expanded, b32 routine)
{
        failures++;
        string_format(log, "%s(%b) expanded %b routine %b\n",
                      name, value, expanded, routine);
        log_flush();
}

#define CHECK(name, known, V)                                                \
        do {                                                                 \
                b32 expanded = known(V);                                     \
                b32 routine = (name)(V);                                     \
                if (expanded != routine)                                     \
                        report(#name, (bipolar)(V), expanded, routine);      \
        } while (0)

/*      Eight literals at a time, because a value built from a literal and a
        literal is still a literal and still folds; a loop counter would not. */
#define EIGHT(name, known, V)                                                \
        CHECK(name, known, (V) + 0); CHECK(name, known, (V) + 1);            \
        CHECK(name, known, (V) + 2); CHECK(name, known, (V) + 3);            \
        CHECK(name, known, (V) + 4); CHECK(name, known, (V) + 5);            \
        CHECK(name, known, (V) + 6); CHECK(name, known, (V) + 7)

#define SIXTY_FOUR(name, known, V)                                           \
        EIGHT(name, known, (V) +  0); EIGHT(name, known, (V) +  8);          \
        EIGHT(name, known, (V) + 16); EIGHT(name, known, (V) + 24);          \
        EIGHT(name, known, (V) + 32); EIGHT(name, known, (V) + 40);          \
        EIGHT(name, known, (V) + 48); EIGHT(name, known, (V) + 56)

/*      Every byte, then the values a caller reaches by accident: EOF, a byte
        that arrived with the high bits still on it, and the two ends. */
#define EVERY_VALUE(name, known)                                             \
        do {                                                                 \
                SIXTY_FOUR(name, known,   0); SIXTY_FOUR(name, known,  64);  \
                SIXTY_FOUR(name, known, 128); SIXTY_FOUR(name, known, 192);  \
                CHECK(name, known, -1);   CHECK(name, known, -2);            \
                CHECK(name, known, -48);  CHECK(name, known, -97);           \
                CHECK(name, known, 256);  CHECK(name, known, 304);           \
                CHECK(name, known, 321);  CHECK(name, known, 511);           \
                CHECK(name, known, 65535); CHECK(name, known, -65536);       \
                CHECK(name, known, 2147483647);                              \
                CHECK(name, known, -2147483647 - 1);                         \
        } while (0)

/*      One set bit at a time from the bottom of the word to the top, which is
        where a count that answered for a thirty two bit register rather than
        the sixty four bit one it was handed shows itself. */
#define ONE_BIT(name, known, S)                                              \
        CHECK(name, known, 1ull << ((S) + 0)); CHECK(name, known, 1ull << ((S) + 1)); \
        CHECK(name, known, 1ull << ((S) + 2)); CHECK(name, known, 1ull << ((S) + 3)); \
        CHECK(name, known, 1ull << ((S) + 4)); CHECK(name, known, 1ull << ((S) + 5)); \
        CHECK(name, known, 1ull << ((S) + 6)); CHECK(name, known, 1ull << ((S) + 7))

#define EVERY_WORD(name, known)                                              \
        do {                                                                 \
                ONE_BIT(name, known,  0); ONE_BIT(name, known,  8);          \
                ONE_BIT(name, known, 16); ONE_BIT(name, known, 24);          \
                ONE_BIT(name, known, 32); ONE_BIT(name, known, 40);          \
                ONE_BIT(name, known, 48); ONE_BIT(name, known, 56);          \
                CHECK(name, known, 0ull);                                    \
                CHECK(name, known, 1ull);                                    \
                CHECK(name, known, 3ull);                                    \
                CHECK(name, known, 0xffull);                                 \
                CHECK(name, known, 0x5555555555555555ull);                   \
                CHECK(name, known, 0xaaaaaaaaaaaaaaaaull);                   \
                CHECK(name, known, 0xdeadbeefull);                           \
                CHECK(name, known, 0x8000000000000000ull);                   \
                CHECK(name, known, 0xffffffffffffffffull);                   \
                CHECK(name, known, 0x7fffffffffffffffull);                   \
                CHECK(name, known, 0x100000000ull);                          \
                CHECK(name, known, 0xffffffffull);                           \
        } while (0)

/*      The narrow one takes an int, so a set bit above thirty one arrives
        sign extended and the answer is still the position of the lowest. */
#define EVERY_WHOLE(name, known)                                             \
        do {                                                                 \
                ONE_BIT(name, known,  0); ONE_BIT(name, known,  8);          \
                ONE_BIT(name, known, 16); ONE_BIT(name, known, 23);          \
                CHECK(name, known, 0);                                       \
                CHECK(name, known, -1);                                      \
                CHECK(name, known, -2);                                      \
                CHECK(name, known, 0x7fffffff);                              \
                CHECK(name, known, -2147483647 - 1);                         \
                CHECK(name, known, 0x5555);                                  \
        } while (0)

/*
        And the macro itself, which is the thing that actually fires at a call
        site. Everything above compares the expansion against the routine by
        naming both; this compares what the preprocessor makes of an ordinary
        call against the routine, so that a macro that expanded the wrong way
        round, or evaluated its argument twice, or stopped a caller taking the
        routine's address, is caught here rather than in somebody's program.
*/
b32 side_effects = 0;
b32 bump(void) { side_effects++; return 'q'; }
b32 (*taken_address)(b32) = byte_is_digit;
volatile b32 not_folded = '7';

void check_macro(void)
{
        if (byte_is_digit('7') != 1)          report("macro byte_is_digit", '7', byte_is_digit('7'), 1);
        if (byte_is_punctuation('!') != 1)    report("macro byte_is_punctuation", '!', byte_is_punctuation('!'), 1);
        if (byte_to_lower('A') != 'a')        report("macro byte_to_lower", 'A', byte_to_lower('A'), 'a');
        if (bits_leading_zeros(1ull) != 63)   report("macro bits_leading_zeros", 1, bits_leading_zeros(1ull), 63);
        if (bits_trailing_zeros(0ull) != 64)  report("macro bits_trailing_zeros", 0, bits_trailing_zeros(0ull), 64);
        if (bits_first_set(0) != 0)           report("macro bits_first_set", 0, bits_first_set(0), 0);
        if (bits_counted(0ull) != 0)          report("macro bits_counted", 0, bits_counted(0ull), 0);

        //      A value the compiler cannot know takes the ordinary call.
        if (byte_is_digit(not_folded) != 1)
                report("macro unfolded", not_folded, byte_is_digit(not_folded), 1);

        //      The argument is evaluated exactly once.
        byte_is_alpha(bump());
        if (side_effects != 1)
                report("macro evaluations", 0, side_effects, 1);

        //      A bare name is not a call, so the macro leaves it alone.
        if (taken_address('3') != 1)
                report("macro address", '3', taken_address('3'), 1);
}

b32 main(void)
{
        check_macro();

        EVERY_VALUE(byte_is_digit,       known_is_digit);
        EVERY_VALUE(byte_is_upper,       known_is_upper);
        EVERY_VALUE(byte_is_lower,       known_is_lower);
        EVERY_VALUE(byte_is_alpha,       known_is_alpha);
        EVERY_VALUE(byte_is_alnum,       known_is_alnum);
        EVERY_VALUE(byte_is_space,       known_is_space);
        EVERY_VALUE(byte_is_hexadecimal, known_is_hexadecimal);
        EVERY_VALUE(byte_is_printable,   known_is_printable);
        EVERY_VALUE(byte_is_graphic,     known_is_graphic);
        EVERY_VALUE(byte_is_control,     known_is_control);
        EVERY_VALUE(byte_is_punctuation, known_is_punctuation);
        EVERY_VALUE(byte_is_blank,       known_is_blank);
        EVERY_VALUE(byte_is_ascii,       known_is_ascii);
        EVERY_VALUE(byte_to_ascii,       known_to_ascii);
        EVERY_VALUE(byte_to_upper,       known_to_upper);
        EVERY_VALUE(byte_to_lower,       known_to_lower);

        /* Every 16-bit value against the C definitions, past what the table names. */
        for (b32 value = 0; value < 65536; value++) {
                b32 up = value >= 'a' && value <= 'z' ? value - 32 : value;
                b32 down = value >= 'A' && value <= 'Z' ? value + 32 : value;
                if (byte_to_upper(value) != up)   report("byte_to_upper wide", value, up, byte_to_upper(value));
                if (byte_to_lower(value) != down) report("byte_to_lower wide", value, down, byte_to_lower(value));
        }

        EVERY_WORD(bits_counted,         known_counted);
        EVERY_WORD(bits_trailing_zeros,  known_trailing_zeros);
        EVERY_WORD(bits_leading_zeros,   known_leading_zeros);
        EVERY_WORD(bits_first_set_wide,  known_first_set_wide);
        EVERY_WHOLE(bits_first_set,      known_first_set);

        if (failures == 0)
                string_format(log, "single: every literal agrees with its routine\n");
        else
                string_format(log, "single: %p disagreements\n", failures);
        log_flush();
        return failures != 0;
}
#endif /* CHECK_single */

#ifdef CHECK_exact_family
#include "../../src/lib.util.c"
/*
        Every byte, every stop position and every source length that is known
        where the call is written.

        Written out by hand, and it has to stay that way: the generator this
        once named has never existed in this repository. It is the sibling of
        the CHECK_exact section and has its shape for the same reason: a
        size, a byte or a string length that arrives from a loop counter
        reaches none of the expansions, because the choice is made by the
        compiler from the token. So all of them are written out.

        memory_fill is checked with a folded zero and with a folded byte that
        is not zero, because the two take different lines above thirty two
        bytes and only one of them is new. memory_zero is checked beside them
        because it is the same expansion with the byte already chosen.

        memory_copy_until gets the strictest check here. Its answer is a
        pointer, its copy length is the stop position and not the size, and
        the bytes past the one it stopped on belong to the caller -- so the
        whole arena outside what was copied is compared against the pattern it
        was prepared with, not only the guards either side of it. An expansion
        that laid down the whole span would pass every check that looked at
        the copied prefix alone.

        string_copy_end takes its length from a string literal rather than a
        number, since that is where stpcpy's length actually comes from, and
        the expansion is reached through __builtin_strlen folding the literal.
        A length the compiler cannot fold, and one at or past the cutoff, both
        go to the routine, and the last three lengths here are there to say so.
*/

#define TOP 136
#define GUARD 32
#define SKEWS 4

static p8 arena[GUARD + TOP + GUARD];
static p8 quarry[TOP + SKEWS];

static positive checks;
static positive failures;

#define FIELD (arena + GUARD)

//      A pattern that differs at every index, so a write that lands one byte
//      off is visible wherever it lands.
static p8 guard_byte(positive i) { return (p8)(0x27 ^ (i * 7)); }

static fn fail(const p8 address_to what, positive size, const p8 address_to why)
{
        failures++;
        string_format(log, "FAIL %s at %p: %s\n", what, size, why);
}

static fn prepare(void)
{
        for (positive i = 0; i < sizeof(arena); i++) arena[i] = guard_byte(i);
}

/*      The arena outside [from, until) still holds the pattern prepare left.
        This is the whole of the tail check as well as the guard check: past
        the length a routine promised to write, every byte is somebody else's. */
static p8 untouched(const p8 address_to what, positive size,
                      positive from, positive until)
{
        for (positive i = 0; i < sizeof(arena); i++) {
                if (i >= from && i < until) continue;
                if (arena[i] != guard_byte(i)) {
                        fail(what, size, i < from ? (const p8 address_to)"wrote before the front"
                                                  : (const p8 address_to)"wrote past the end");
                        return 0;
                }
        }
        return 1;
}

static fn inspect_byte(const p8 address_to what, positive size, p8 want)
{
        checks++;
        if (!untouched(what, size, GUARD, GUARD + size)) return;
        for (positive i = 0; i < size; i++)
                if (FIELD[i] != want) {
                        fail(what, size, (const p8 address_to)"wrong byte");
                        return;
                }
}

#define CHECK_FILL(K)                                                         \
        do {                                                                  \
                prepare();                                                    \
                memory_fill(FIELD, 0, K);                                     \
                inspect_byte((const p8 address_to)"memory_fill zero", K, 0);  \
                prepare();                                                    \
                memory_fill(FIELD, 0x5a, K);                                  \
                inspect_byte((const p8 address_to)"memory_fill byte", K, 0x5a); \
                prepare();                                                    \
                memory_zero(FIELD, K);                                        \
                inspect_byte((const p8 address_to)"memory_zero", K, 0);       \
        } while (0)

/*      Every stop position the size allows, and the position past the end,
        which is the byte being absent and the answer being nothing. */
#define CHECK_UNTIL(K)                                                        \
        do {                                                                  \
                for (positive skew = 0; skew < SKEWS; skew++)                 \
                for (positive at = 0; at <= K; at++) {                        \
                        const p8 address_to from = quarry + skew;             \
                        positive taken = at < K ? at + 1 : K;                 \
                        address_any want = at < K                             \
                                ? (address_any)(FIELD + at + 1) : null;       \
                        address_any answer;                                   \
                                                                              \
                        for (positive i = 0; i < K; i++)                      \
                                ((p8 address_to)from)[i] = (p8)('A' + (i & 7)); \
                        if (at < K) ((p8 address_to)from)[at] = 0x5a;         \
                                                                              \
                        prepare();                                            \
                        checks++;                                             \
                        answer = memory_copy_until(FIELD, (address_any)from,  \
                                                   0x5a, K);                  \
                        if (answer != want) {                                 \
                                fail((const p8 address_to)"memory_copy_until", \
                                     K, (const p8 address_to)"wrong answer"); \
                                continue;                                     \
                        }                                                     \
                        if (!untouched((const p8 address_to)"memory_copy_until", \
                                       K, GUARD, GUARD + taken))              \
                                continue;                                     \
                        for (positive i = 0; i < taken; i++)                  \
                                if (FIELD[i] != from[i]) {                    \
                                        fail((const p8 address_to)"memory_copy_until", \
                                             K, (const p8 address_to)"wrong byte"); \
                                        break;                                \
                                }                                             \
                } while (0);                                                  \
        } while (0)

/*      The literal is the length. The terminator is copied with the rest and
        the answer is where it landed. */
#define CHECK_END(K, TEXT)                                                    \
        do {                                                                  \
                string_address text = (string_address)(TEXT);                 \
                p8 address_to answer;                                         \
                                                                              \
                prepare();                                                    \
                checks++;                                                     \
                answer = string_copy_end(FIELD, (string_address)(TEXT));      \
                if (answer != FIELD + (K)) {                                  \
                        fail((const p8 address_to)"string_copy_end", K,       \
                             (const p8 address_to)"wrong answer");            \
                        break;                                                \
                }                                                             \
                if (!untouched((const p8 address_to)"string_copy_end", K,     \
                               GUARD, GUARD + (K) + 1))                       \
                        break;                                                \
                if (answer[0] != end) {                                       \
                        fail((const p8 address_to)"string_copy_end", K,       \
                             (const p8 address_to)"no terminator");           \
                        break;                                                \
                }                                                             \
                for (positive i = 0; i < (K); i++)                            \
                        if (FIELD[i] != text[i]) {                            \
                                fail((const p8 address_to)"string_copy_end", K, \
                                     (const p8 address_to)"wrong byte");      \
                                break;                                        \
                        }                                                     \
        } while (0)

//      Lengths built by doubling, so a literal of any length is a handful of
//      tokens rather than a line of quoted bytes.
#define A1  "a"
#define A2  A1 A1
#define A4  A2 A2
#define A8  A4 A4
#define A16 A8 A8
#define A32 A16 A16
#define A64 A32 A32
#define A128 A64 A64

b32 main(void)
{
        //      memory_fill with a folded byte, and memory_zero beside it.
        CHECK_FILL(0); CHECK_FILL(1); CHECK_FILL(2); CHECK_FILL(3); CHECK_FILL(4); CHECK_FILL(5); CHECK_FILL(6); CHECK_FILL(7);
        CHECK_FILL(8); CHECK_FILL(9); CHECK_FILL(10); CHECK_FILL(11); CHECK_FILL(12); CHECK_FILL(13); CHECK_FILL(14); CHECK_FILL(15);
        CHECK_FILL(16); CHECK_FILL(17); CHECK_FILL(18); CHECK_FILL(19); CHECK_FILL(20); CHECK_FILL(21); CHECK_FILL(22); CHECK_FILL(23);
        CHECK_FILL(24); CHECK_FILL(25); CHECK_FILL(26); CHECK_FILL(27); CHECK_FILL(28); CHECK_FILL(29); CHECK_FILL(30); CHECK_FILL(31);
        CHECK_FILL(32); CHECK_FILL(33); CHECK_FILL(34); CHECK_FILL(35); CHECK_FILL(36); CHECK_FILL(37); CHECK_FILL(38); CHECK_FILL(39);
        CHECK_FILL(40); CHECK_FILL(41); CHECK_FILL(42); CHECK_FILL(43); CHECK_FILL(44); CHECK_FILL(45); CHECK_FILL(46); CHECK_FILL(47);
        CHECK_FILL(48); CHECK_FILL(49); CHECK_FILL(50); CHECK_FILL(51); CHECK_FILL(52); CHECK_FILL(53); CHECK_FILL(54); CHECK_FILL(55);
        CHECK_FILL(56); CHECK_FILL(57); CHECK_FILL(58); CHECK_FILL(59); CHECK_FILL(60); CHECK_FILL(61); CHECK_FILL(62); CHECK_FILL(63);
        CHECK_FILL(64); CHECK_FILL(65); CHECK_FILL(66); CHECK_FILL(67); CHECK_FILL(68); CHECK_FILL(69); CHECK_FILL(70); CHECK_FILL(71);
        CHECK_FILL(72); CHECK_FILL(73); CHECK_FILL(74); CHECK_FILL(75); CHECK_FILL(76); CHECK_FILL(77); CHECK_FILL(78); CHECK_FILL(79);
        CHECK_FILL(80); CHECK_FILL(81); CHECK_FILL(82); CHECK_FILL(83); CHECK_FILL(84); CHECK_FILL(85); CHECK_FILL(86); CHECK_FILL(87);
        CHECK_FILL(88); CHECK_FILL(89); CHECK_FILL(90); CHECK_FILL(91); CHECK_FILL(92); CHECK_FILL(93); CHECK_FILL(94); CHECK_FILL(95);
        CHECK_FILL(96); CHECK_FILL(97); CHECK_FILL(98); CHECK_FILL(99); CHECK_FILL(100); CHECK_FILL(101); CHECK_FILL(102); CHECK_FILL(103);
        CHECK_FILL(104); CHECK_FILL(105); CHECK_FILL(106); CHECK_FILL(107); CHECK_FILL(108); CHECK_FILL(109); CHECK_FILL(110); CHECK_FILL(111);
        CHECK_FILL(112); CHECK_FILL(113); CHECK_FILL(114); CHECK_FILL(115); CHECK_FILL(116); CHECK_FILL(117); CHECK_FILL(118); CHECK_FILL(119);
        CHECK_FILL(120); CHECK_FILL(121); CHECK_FILL(122); CHECK_FILL(123); CHECK_FILL(124); CHECK_FILL(125); CHECK_FILL(126); CHECK_FILL(127);
        CHECK_FILL(128);
        //      memccpy, every stop position, either side of KNOWN_UNTIL_MAX.
        CHECK_UNTIL(0); CHECK_UNTIL(1); CHECK_UNTIL(2); CHECK_UNTIL(3); CHECK_UNTIL(4); CHECK_UNTIL(5); CHECK_UNTIL(6); CHECK_UNTIL(7);
        CHECK_UNTIL(8); CHECK_UNTIL(9); CHECK_UNTIL(10); CHECK_UNTIL(11); CHECK_UNTIL(12); CHECK_UNTIL(13); CHECK_UNTIL(14); CHECK_UNTIL(15);
        CHECK_UNTIL(16); CHECK_UNTIL(17); CHECK_UNTIL(18); CHECK_UNTIL(19); CHECK_UNTIL(20); CHECK_UNTIL(21); CHECK_UNTIL(22); CHECK_UNTIL(23);
        CHECK_UNTIL(24); CHECK_UNTIL(25); CHECK_UNTIL(26); CHECK_UNTIL(27); CHECK_UNTIL(28); CHECK_UNTIL(29); CHECK_UNTIL(30); CHECK_UNTIL(31);
        CHECK_UNTIL(32); CHECK_UNTIL(40); CHECK_UNTIL(48); CHECK_UNTIL(64); CHECK_UNTIL(96); CHECK_UNTIL(128);
        //      stpcpy, the length coming from the literal itself. The last
        //      three are at and past the cutoff and reach the routine.
        CHECK_END(0, ""); CHECK_END(1, A1); CHECK_END(2, A2); CHECK_END(3, A2 A1);
        CHECK_END(4, A4); CHECK_END(5, A4 A1); CHECK_END(6, A4 A2); CHECK_END(7, A4 A2 A1);
        CHECK_END(8, A8); CHECK_END(9, A8 A1); CHECK_END(10, A8 A2); CHECK_END(11, A8 A2 A1);
        CHECK_END(12, A8 A4); CHECK_END(13, A8 A4 A1); CHECK_END(14, A8 A4 A2); CHECK_END(15, A8 A4 A2 A1);
        CHECK_END(16, A16); CHECK_END(17, A16 A1); CHECK_END(18, A16 A2); CHECK_END(19, A16 A2 A1);
        CHECK_END(20, A16 A4); CHECK_END(21, A16 A4 A1); CHECK_END(22, A16 A4 A2); CHECK_END(23, A16 A4 A2 A1);
        CHECK_END(24, A16 A8); CHECK_END(25, A16 A8 A1); CHECK_END(26, A16 A8 A2); CHECK_END(27, A16 A8 A2 A1);
        CHECK_END(28, A16 A8 A4); CHECK_END(29, A16 A8 A4 A1); CHECK_END(30, A16 A8 A4 A2); CHECK_END(31, A16 A8 A4 A2 A1);
        CHECK_END(32, A32); CHECK_END(33, A32 A1); CHECK_END(34, A32 A2); CHECK_END(35, A32 A2 A1);
        CHECK_END(36, A32 A4); CHECK_END(37, A32 A4 A1); CHECK_END(38, A32 A4 A2); CHECK_END(39, A32 A4 A2 A1);
        CHECK_END(40, A32 A8); CHECK_END(41, A32 A8 A1); CHECK_END(42, A32 A8 A2); CHECK_END(43, A32 A8 A2 A1);
        CHECK_END(44, A32 A8 A4); CHECK_END(45, A32 A8 A4 A1); CHECK_END(46, A32 A8 A4 A2); CHECK_END(47, A32 A8 A4 A2 A1);
        CHECK_END(48, A32 A16); CHECK_END(49, A32 A16 A1); CHECK_END(50, A32 A16 A2); CHECK_END(51, A32 A16 A2 A1);
        CHECK_END(52, A32 A16 A4); CHECK_END(53, A32 A16 A4 A1); CHECK_END(54, A32 A16 A4 A2); CHECK_END(55, A32 A16 A4 A2 A1);
        CHECK_END(56, A32 A16 A8); CHECK_END(57, A32 A16 A8 A1); CHECK_END(58, A32 A16 A8 A2); CHECK_END(59, A32 A16 A8 A2 A1);
        CHECK_END(60, A32 A16 A8 A4); CHECK_END(61, A32 A16 A8 A4 A1); CHECK_END(62, A32 A16 A8 A4 A2); CHECK_END(63, A32 A16 A8 A4 A2 A1);
        CHECK_END(64, A64); CHECK_END(65, A64 A1); CHECK_END(66, A64 A2); CHECK_END(67, A64 A2 A1);
        CHECK_END(68, A64 A4); CHECK_END(69, A64 A4 A1); CHECK_END(70, A64 A4 A2); CHECK_END(71, A64 A4 A2 A1);
        CHECK_END(72, A64 A8); CHECK_END(73, A64 A8 A1); CHECK_END(74, A64 A8 A2); CHECK_END(75, A64 A8 A2 A1);
        CHECK_END(76, A64 A8 A4); CHECK_END(77, A64 A8 A4 A1); CHECK_END(78, A64 A8 A4 A2); CHECK_END(79, A64 A8 A4 A2 A1);
        CHECK_END(80, A64 A16); CHECK_END(81, A64 A16 A1); CHECK_END(82, A64 A16 A2); CHECK_END(83, A64 A16 A2 A1);
        CHECK_END(84, A64 A16 A4); CHECK_END(85, A64 A16 A4 A1); CHECK_END(86, A64 A16 A4 A2); CHECK_END(87, A64 A16 A4 A2 A1);
        CHECK_END(88, A64 A16 A8); CHECK_END(89, A64 A16 A8 A1); CHECK_END(90, A64 A16 A8 A2); CHECK_END(91, A64 A16 A8 A2 A1);
        CHECK_END(92, A64 A16 A8 A4); CHECK_END(93, A64 A16 A8 A4 A1); CHECK_END(94, A64 A16 A8 A4 A2); CHECK_END(95, A64 A16 A8 A4 A2 A1);
        CHECK_END(96, A64 A32); CHECK_END(97, A64 A32 A1); CHECK_END(98, A64 A32 A2); CHECK_END(99, A64 A32 A2 A1);
        CHECK_END(100, A64 A32 A4); CHECK_END(101, A64 A32 A4 A1); CHECK_END(102, A64 A32 A4 A2); CHECK_END(103, A64 A32 A4 A2 A1);
        CHECK_END(104, A64 A32 A8); CHECK_END(105, A64 A32 A8 A1); CHECK_END(106, A64 A32 A8 A2); CHECK_END(107, A64 A32 A8 A2 A1);
        CHECK_END(108, A64 A32 A8 A4); CHECK_END(109, A64 A32 A8 A4 A1); CHECK_END(110, A64 A32 A8 A4 A2); CHECK_END(111, A64 A32 A8 A4 A2 A1);
        CHECK_END(112, A64 A32 A16); CHECK_END(113, A64 A32 A16 A1); CHECK_END(114, A64 A32 A16 A2); CHECK_END(115, A64 A32 A16 A2 A1);
        CHECK_END(116, A64 A32 A16 A4); CHECK_END(117, A64 A32 A16 A4 A1); CHECK_END(118, A64 A32 A16 A4 A2); CHECK_END(119, A64 A32 A16 A4 A2 A1);
        CHECK_END(120, A64 A32 A16 A8); CHECK_END(121, A64 A32 A16 A8 A1); CHECK_END(122, A64 A32 A16 A8 A2); CHECK_END(123, A64 A32 A16 A8 A2 A1);
        CHECK_END(124, A64 A32 A16 A8 A4); CHECK_END(125, A64 A32 A16 A8 A4 A1); CHECK_END(126, A64 A32 A16 A8 A4 A2); CHECK_END(127, A64 A32 A16 A8 A4 A2 A1);
        CHECK_END(128, A128); CHECK_END(129, A128 A1);
        string_format(log, "%p checks, %p failures\n", checks, failures);
        log_flush();
        return failures ? 1 : 0;
}
#endif /* CHECK_exact_family */

#ifdef CHECK_declare
#include "../../src/lib.util.c"

/*
        The standard names, called by their standard names.

        The text family reaches its assembly through these: strdup is
        string_duplicate, strndup is string_duplicate_max, strtok is
        string_token, strtok_r is string_token_next, strsep is
        string_split_next, strcasestr is string_search_folded, strlcpy is
        string_copy_bounded and strlcat is string_append_bounded. Calling the
        standard spelling here is what exercises the routine under it.

        This is the freestanding half of a pair. Everything it checks is in
        CHECK_declare, and CHECK_declare_reference builds
        that same file against the host's real headers and glibc. The pair is
        what makes the lane mean anything: a case file that compiles here and
        not there would mean the prototypes in src/lib.c had declared something in
        a way no real header would recognise, and a case file that passes
        here and fails there is a genuine disagreement about behaviour rather
        than about what to test.

        The freestanding and reference builds exercise the same standard
        semantics. House-only identity and string_copy_max checks stay behind
        DECLARE_FREESTANDING.

        DECLARE_FREESTANDING switches on the identity checks at the end,
        which ask whether memcpy and memory_copy are one address. glibc has
        no answer to that and no reason to.
*/
#define DECLARE_FREESTANDING 1

#define declare_jump_state jump_state

static positive declare_checks;
static positive declare_failures;

#define declare_check(name, condition)                                        \
        do {                                                                  \
                declare_checks++;                                             \
                if (!(condition)) {                                           \
                        declare_failures++;                                   \
                        string_format(log, "  FAIL " name "\n");              \
                }                                                             \
        } while (0)

#define SHARED_declare_cases
#include "../checks.c"
#undef SHARED_declare_cases

b32 main(void)
{
        declare_cases();

        string_format(log, "%p checks, %p failures\n", declare_checks,
                      declare_failures);
        log_flush();

        return declare_failures != 0;
}
#endif /* CHECK_declare */

#ifdef CHECK_declare_reference
#define _GNU_SOURCE

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
        The same cases, against the host's glibc.

        Nothing the distribution ships is linked here and nothing here is
        built by the distribution. It exists so that CHECK_declare
        is put through the library those names are specified by, on the
        machine the tests run on, and so that the case file has to compile
        against real headers -- which is the actual assertion about
        the prototypes in src/lib.c. A prototype there that disagreed with
        <string.h> by a const or by a size_t would make this file fail to
        compile, and no amount of agreement at run time would make up for it.

        Build it beside the freestanding one and compare the verdicts:

            gcc -O2 -w -DCHECK_declare_reference -o /tmp/declare.reference \
                test/checks.c -lm

        strlcpy and strlcat arrived in glibc 2.38 and are the only two cases
        that need a library that new.

        DECLARE_FREESTANDING is not defined here, which takes away the
        identity and house-name checks -- glibc's memcpy is not anybody's
        memory_copy.
*/

static unsigned long long declare_checks;
static unsigned long long declare_failures;

#define declare_jump_state jmp_buf

#define declare_check(name, condition)                                        \
        do {                                                                  \
                declare_checks++;                                             \
                if (!(condition)) {                                           \
                        declare_failures++;                                   \
                        printf("  FAIL " name "\n");                          \
                }                                                             \
        } while (0)

#define SHARED_declare_cases
#include "../checks.c"
#undef SHARED_declare_cases

int main(void)
{
        declare_cases();

        printf("%llu checks, %llu failures\n", declare_checks, declare_failures);

        return declare_failures != 0;
}
#endif /* CHECK_declare_reference */

#ifdef CHECK_fold
/*
        The constant-size fold that kernel/patch/fold-x86.h puts into the
        kernel's own string header, checked against the routines it stands in
        for.

        WHY THIS FILE EXISTS. The fold is the one edit we make to Linux that
        changes what a CALL SITE compiles to rather than what a name resolves
        to, and it lands in a header that every kernel translation unit
        includes. A fault in it is a fault everywhere. The first version wrote
        five bytes behind its destination for a count of three, and the kernel
        took a general protection fault in PID 1 before the first shell.

        WHY THE POISON IS DIFFERENT IN EACH BUFFER. The first version of this
        check filled every buffer with the same byte and PASSED against that
        broken fold: the out of bounds READ landed in the neighbouring array,
        which held the same poison, so it wrote the poison back over the
        poison and the comparison saw nothing. Each region has its own value
        now, and the source sits inside a third, so a read or a write outside
        the destination shows up as the wrong byte rather than as the right
        one by accident.

        The fold is header-only and compiles as ordinary C, so this runs on
        the host like any other lane rather than needing a kernel.
*/
#include "../../src/lib.util.c"

typedef unsigned long __kernel_size_t;
#include "../../kernel/patch/fold-x86.h"

static p8 fold_mine[256];
static p8 fold_theirs[256];
static p8 fold_region[256];

static positive fold_checks;
static positive fold_failures;

static fn fold_note(string_address what, positive size, positive at,
                    positive got, positive want)
{
        fold_failures++;
        string_format(log, "  %s size %p byte %p: %p want %p\n",
                      what, size, at, got, want);
}

static fn fold_check(string_address what, positive size, p8 address_to source)
{
        fold_checks++;
        for (positive at = 0; at < sizeof(fold_mine); at++)
        {
                p8 want = at >= 96 && at < 96 + size
                    ? (source ? source[at - 96] : 0x5C) : 0xA5;
                if (fold_mine[at] != want)
                {
                        fold_note(what, size, at, fold_mine[at], want);
                        break;
                }
        }
}

/* Literal tokens reach the builtin arms; changed arguments must run once. */
#define FOLD_LITERAL(size)                                                   \
        do {                                                                 \
                positive to = 96, from = 64;                                 \
                memory_fill(fold_mine, 0xA5, sizeof(fold_mine));                \
                if (memcpy(fold_mine + to++, fold_region + from++, size) !=   \
                        fold_mine + 96 || to != 97 || from != 65)             \
                        fold_note("literal copy arguments", size, 0, 1, 0); \
                fold_check("literal copy", size, fold_region + 64);          \
                memory_fill(fold_mine, 0xA5, sizeof(fold_mine));                \
                if (memset(fold_mine + --to, 0x5C, size) != fold_mine + 96 || \
                    to != 96)                                                \
                        fold_note("literal fill arguments", size, 0, 1, 0); \
                fold_check("literal fill", size, null);                      \
        } while (0)

b32 main(void)
{
        positive size;
        positive at;
        p8 address_to source = fold_region + 64;

        for (size = 0; size <= MOONWATER_FOLD_MAX; size++)
        {
                memory_fill(fold_mine, 0xA5, sizeof(fold_mine));
                memory_fill(fold_theirs, 0x3C, sizeof(fold_theirs));
                memory_fill(fold_region, 0x77, sizeof(fold_region));

                for (at = 0; at < 64; at++)
                        source[at] = (p8)(at * 37 + 11);

                memcpy(fold_mine + 96, source, size);
                memory_copy_apart(fold_theirs + 96, source, size);

                fold_check("copy", size, fold_theirs + 96);

                memory_fill(fold_mine, 0xA5, sizeof(fold_mine));
                memset(fold_mine + 96, 0x5C, size);

                fold_check("fill", size, null);
        }

        FOLD_LITERAL(0);
        FOLD_LITERAL(1);
        FOLD_LITERAL(7);
        FOLD_LITERAL(8);
        FOLD_LITERAL(15);
        FOLD_LITERAL(16);
        FOLD_LITERAL(31);
        FOLD_LITERAL(32);
        FOLD_LITERAL(33);

        positive to = 96, from = 64, count = 8, byte = 0x15C;
        memory_fill(fold_mine, 0xA5, sizeof(fold_mine));
        if (memcpy(fold_mine + to++, fold_region + from++, count++) !=
                fold_mine + 96 || to != 97 || from != 65 || count != 9)
                fold_note("dynamic copy arguments", 8, 0, 1, 0);
        fold_check("dynamic copy", 8, source);
        memory_fill(fold_mine, 0xA5, sizeof(fold_mine));
        if (memset(fold_mine + --to, byte++, count++) != fold_mine + 96 ||
            to != 96 || byte != 0x15D || count != 10)
                fold_note("dynamic fill arguments", 9, 0, 1, 0);
        fold_check("dynamic fill", 9, null);

        string_format(log, "%p checks, %p failures\n", fold_checks,
                      fold_failures);
        log_flush();

        return fold_failures != 0;
}
#endif /* CHECK_fold */

#ifdef CHECK_allocator
/*
        Experimental C standard library

        The allocator, against the one on the machine

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        One file, built twice.

        Built the ordinary way it is the freestanding test: it includes
        lib.util.c and calls the allocator in lib.util.c,
        and it runs on x86_64, arm64 and riscv64. Built with
        ALLOCATOR_REFERENCE defined it links glibc instead and the same lines
        run through glibc's malloc.

        Everything printed before the "shared-end" line is a statement about
        behaviour rather than about addresses -- a count, a boolean, a
        checksum -- so the two builds must print those bytes identically, and
        the allocator lane of test/run diffs them. Anything printed after that
        line is
        about this allocator's own internals and has no reference to compare
        with.

        The torture loop is the part that matters. A linear congruential
        sequence with a fixed seed decides every size and every operation, so
        both builds walk the identical script; every live block carries a byte
        pattern derived from a tag that changes on every write, and every
        block is checked against its pattern before it is touched again. A
        realloc that loses a byte, a free list that hands the same block out
        twice, a class that reports more usable bytes than it has -- all three
        show up as a mismatch count that is not zero, on both sides, without
        either side knowing anything about the other's layout.
*/

#ifdef ALLOCATOR_REFERENCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

typedef unsigned char p8;
typedef unsigned long long positive;
typedef long long bipolar;
typedef int b32;
typedef void fn;

#define address_to *
#define address_of &
#define address_any void *
#define null ((void *)0)
#define bool p8

#define memory_copy_apart(destination, source, size) \
        memcpy((destination), (source), (size))

#define memory_zero(destination, size) memset((destination), 0, (size))

#else

#include "../../src/lib.util.c"

#endif

//====================================================================
//      saying things, the same bytes either way
//====================================================================
/*
        Both builds fill one buffer and write it once at the end, so nothing
        about either standard output's buffering can reorder a line.
*/

static p8 say_buffer[1 << 16];
static positive say_used;

static fn say(const char address_to text)
{
        while (address_to text)
        {
                if (say_used < sizeof(say_buffer))
                        say_buffer[say_used++] = (p8)address_to text;

                text++;
        }
}

static fn say_number(positive value)
{
        p8 digits[24];
        b32 count = 0;

        if (!value)
                digits[count++] = '0';

        while (value)
        {
                digits[count++] = (p8)('0' + (value % 10));
                value /= 10;
        }

        while (count--)
                if (say_used < sizeof(say_buffer))
                        say_buffer[say_used++] = digits[count];
}

static fn say_line(const char address_to label, positive value)
{
        say(label);
        say(" ");
        say_number(value);
        say("\n");
}

static fn say_flush(void)
{
#ifdef ALLOCATOR_REFERENCE
        fwrite(say_buffer, 1, (size_t)say_used, stdout);
        fflush(stdout);
#else
        log(say_buffer, say_used);
        log_flush();
#endif
}

//====================================================================
//      the pattern every live block carries
//====================================================================

static positive check_failures;
static positive check_checks;

static fn check_fill(address_any block, positive size, positive tag)
{
        p8 address_to bytes = (p8 address_to)block;
        positive index = 0;

        while (index < size)
        {
                bytes[index] = (p8)(tag + index * 31);
                index++;
        }
}

static p8 check_holds(address_any block, positive size, positive tag)
{
        p8 address_to bytes = (p8 address_to)block;
        positive index = 0;

        while (index < size)
        {
                if (bytes[index] != (p8)(tag + index * 31))
                        return 0;

                index++;
        }

        return 1;
}

static fn check_true(p8 condition)
{
        check_checks++;

        if (!condition)
                check_failures++;
}

//====================================================================
//      the script both builds walk
//====================================================================

static positive check_seed = 0x2545F4914F6CDD1Dull;

static positive check_random(void)
{
        check_seed = check_seed * 6364136223846793005ull +
                     1442695040888963407ull;

        return check_seed >> 17;
}

#define CHECK_SLOTS 256
#define CHECK_TURNS 200000

static address_any check_live[CHECK_SLOTS];
static positive check_size[CHECK_SLOTS];
static positive check_tag[CHECK_SLOTS];

static positive check_allocations;
static positive check_frees;
static positive check_resizes;
static positive check_verifies;
static positive check_refused;

//      Every block this test is handed, whatever produced it, has to be
//      aligned for any type and has to say it holds at least what was asked.
static fn check_block(address_any block, positive size)
{
        check_true(((positive)block & 15) == 0);
        check_true(malloc_usable_size(block) >= size);
}

static positive check_size_wanted(void)
{
        //      Mostly small, because that is what programs do, with one
        //      request in sixty four large enough to leave the size classes
        //      entirely and become a mapping of its own.
        if ((check_random() & 63) == 0)
                return check_random() % 300000;

        return check_random() % 200;
}

static fn check_torture(void)
{
        positive turn = 0;
        positive tag = 1;

        while (turn < CHECK_TURNS)
        {
                positive slot = check_random() % CHECK_SLOTS;
                positive choice = check_random() % 100;

                turn++;

                if (!check_live[slot])
                {
                        positive size = check_size_wanted();
                        address_any block = malloc(size);

                        if (!block)
                        {
                                check_refused++;
                                continue;
                        }

                        check_allocations++;
                        check_block(block, size);
                        check_fill(block, size, tag);

                        check_live[slot] = block;
                        check_size[slot] = size;
                        check_tag[slot] = tag;
                        tag++;
                        continue;
                }

                check_verifies++;

                check_true(check_holds(check_live[slot], check_size[slot],
                                       check_tag[slot]));

                if (choice < 40)
                {
                        positive size = check_size_wanted();
                        positive keep = size < check_size[slot]
                                                ? size
                                                : check_size[slot];
                        address_any block =
                                realloc(check_live[slot], size);

                        if (size && !block)
                        {
                                check_refused++;
                                continue;
                        }

                        check_resizes++;

                        if (!size)
                        {
                                //      realloc to nothing frees and answers
                                //      null, so the slot is now empty.
                                check_true(block == null);
                                check_live[slot] = null;
                                continue;
                        }

                        check_block(block, size);

                        //      Whatever was there has to still be there, up
                        //      to the smaller of the two sizes.
                        check_true(check_holds(block, keep, check_tag[slot]));

                        check_fill(block, size, tag);
                        check_live[slot] = block;
                        check_size[slot] = size;
                        check_tag[slot] = tag;
                        tag++;
                        continue;
                }

                if (choice < 55)
                {
                        //      Overwrite in place, which is the case a free
                        //      list that hands one block out twice fails.
                        check_fill(check_live[slot], check_size[slot], tag);
                        check_tag[slot] = tag;
                        tag++;
                        continue;
                }

                free(check_live[slot]);
                check_frees++;
                check_live[slot] = null;
        }

        //      Everything still standing is checked once more and released,
        //      so a block corrupted late still counts.
        positive slot = 0;

        while (slot < CHECK_SLOTS)
        {
                if (check_live[slot])
                {
                        check_verifies++;

                        check_true(check_holds(check_live[slot],
                                               check_size[slot],
                                               check_tag[slot]));

                        free(check_live[slot]);
                        check_frees++;
                        check_live[slot] = null;
                }

                slot++;
        }
}

//====================================================================
//      the corners
//====================================================================

#define CHECK_HELD 64

static address_any check_held[CHECK_HELD];
static positive check_held_size[CHECK_HELD];
static positive check_held_tag[CHECK_HELD];

static fn check_corners(void)
{
        //      A request of nothing still answers with a pointer free takes,
        //      and two of them are two different blocks.
        address_any nothing_one = malloc(0);
        address_any nothing_two = malloc(0);

        check_true(nothing_one != null);
        check_true(nothing_two != null);
        check_true(nothing_one != nothing_two);
        free(nothing_one);
        free(nothing_two);

        //      Freeing nothing does nothing.
        free(null);

        //      A resize from nothing is an allocation.
        address_any grown = realloc(null, 100);

        check_true(grown != null);
        check_block(grown, 100);
        check_fill(grown, 100, 7);

        //      Growing preserves, shrinking preserves what is left.
        grown = realloc(grown, 4000);
        check_true(grown != null);
        check_true(check_holds(grown, 100, 7));
        check_block(grown, 4000);

        grown = realloc(grown, 40);
        check_true(grown != null);
        check_true(check_holds(grown, 40, 7));

        //      And a resize to nothing frees it and answers null.
        check_true(realloc(grown, 0) == null);

        //      Zeroed memory, from a block that was certainly dirty first.
        address_any dirty = malloc(600);

        check_true(dirty != null);
        check_fill(dirty, 600, 200);
        free(dirty);

        positive round = 0;

        while (round < 32)
        {
                address_any clean = calloc(600, 1);
                positive index = 0;
                p8 zero = 1;

                check_true(clean != null);

                while (index < 600)
                {
                        if (((p8 address_to)clean)[index])
                                zero = 0;

                        index++;
                }

                check_true(zero);
                check_fill(clean, 600, 99);
                free(clean);
                round++;
        }

        //      A large calloc, which on a fresh mapping is allowed to skip
        //      the store loop entirely and must still read back as zero.
        address_any wide = calloc(400000, 1);
        positive index = 0;
        p8 zero = 1;

        check_true(wide != null);

        while (index < 400000)
        {
                if (((p8 address_to)wide)[index])
                        zero = 0;

                index += 7;
        }

        check_true(zero);
        free(wide);

        //      The multiply that must not wrap.
        check_true(calloc((positive)-1, 2) == null);
        check_true(calloc(2, (positive)-1) == null);
        check_true(calloc((positive)1 << 62, 8) == null);

        //      Zero elements is a legal allocation of nothing.
        address_any none = calloc(0, 8);

        check_true(none != null);
        free(none);

        none = calloc(8, 0);
        check_true(none != null);
        free(none);

        //      Nothing has a usable size of nothing.
        check_true(malloc_usable_size(null) == 0);

        //      Alignments, every power of two the page and under.
        positive alignment = 16;

        while (alignment <= 65536)
        {
                address_any placed = aligned_alloc(alignment, alignment * 3);

                check_true(placed != null);
                check_true(((positive)placed & (alignment - 1)) == 0);
                check_true(malloc_usable_size(placed) >= alignment * 3);
                check_fill(placed, alignment * 3, 11);
                check_true(check_holds(placed, alignment * 3, 11));
                free(placed);

                alignment += alignment;
        }

        //      The older interface, and the codes it answers with. glibc
        //      refuses anything that is not a power of two at least as wide
        //      as a pointer, and this table is the whole of that rule.
        static const positive asked[] = {0, 1, 2, 3, 4, 8, 16, 24, 32, 64,
                                         4096, 65536};
        positive which = 0;

        while (which < sizeof(asked) / sizeof(asked[0]))
        {
                address_any placed = (address_any)1;
                b32 answer = posix_memalign(address_of placed, asked[which],
                                            300);

                say("memalign ");
                say_number(asked[which]);
                say(" ");
                say_number((positive)answer);
                say("\n");

                if (!answer)
                {
                        check_true(((positive)placed &
                                    (asked[which] - 1)) == 0);
                        check_true(malloc_usable_size(placed) >= 300);
                        free(placed);
                }

                which++;
        }

        //      A mapping grown and shrunk across the whole range, with the
        //      contents checked at every step. This is the path mremap is on.
        positive want = 1 << 20;
        address_any big = malloc(want);

        check_true(big != null);
        check_fill(big, want, 55);

        big = realloc(big, 4 << 20);
        check_true(big != null);
        check_true(check_holds(big, want, 55));
        check_block(big, 4 << 20);

        big = realloc(big, 300000);
        check_true(big != null);
        check_true(check_holds(big, 300000 < want ? 300000 : want, 55));

        big = realloc(big, 900);
        check_true(big != null);
        check_true(check_holds(big, 900, 55));
        check_block(big, 900);

        big = realloc(big, 8 << 20);
        check_true(big != null);
        check_true(check_holds(big, 900, 55));
        check_block(big, 8 << 20);

        free(big);

        //      A grow-by-one loop, which is the shape that a realloc deciding
        //      badly turns into a copy every turn.
        address_any creep = malloc(1);
        positive length = 1;

        check_true(creep != null);
        check_fill(creep, 1, 3);

        while (length < 5000)
        {
                positive next = length + 1;
                address_any moved = realloc(creep, next);

                check_true(moved != null);
                check_true(check_holds(moved, length, 3));
                ((p8 address_to)moved)[length] = (p8)(3 + length * 31);
                creep = moved;
                length = next;
        }

        check_true(check_holds(creep, length, 3));
        free(creep);

        /*
                Every size across the class boundaries, sixty four blocks held
                at a time, each written to the very last byte it claims to
                have.

                Sixty four and not one. A block filled to its usable size with
                nothing else live writes into memory no other block owns yet,
                so a usable size that over-reported by a word would read back
                perfectly and nothing would ever know. With sixty three
                neighbours standing, the same word lands in one of them and
                comes back as a mismatch -- on either allocator, because
                glibc's malloc_usable_size is exact too.
        */
        positive filled = 0;
        positive size = 0;

        while (size <= 70000)
        {
                address_any block = malloc(size);

                check_true(block != null);
                check_block(block, size);

                check_held[filled] = block;
                check_held_size[filled] = malloc_usable_size(block);
                check_held_tag[filled] = size + 1;
                check_fill(block, check_held_size[filled],
                           check_held_tag[filled]);
                filled++;

                if (filled == CHECK_HELD)
                {
                        positive which = 0;

                        while (which < CHECK_HELD)
                        {
                                check_true(check_holds(
                                        check_held[which],
                                        check_held_size[which],
                                        check_held_tag[which]));

                                free(check_held[which]);
                                which++;
                        }

                        filled = 0;
                }

                size = size < 300 ? size + 1 : size + 97;
        }

        while (filled)
        {
                filled--;

                check_true(check_holds(check_held[filled],
                                       check_held_size[filled],
                                       check_held_tag[filled]));

                free(check_held[filled]);
        }
}

//====================================================================
//      what only this allocator can be asked
//====================================================================

#ifndef ALLOCATOR_REFERENCE
static fn check_own(void)
{
        positive own_failures = 0;

        //      Every shelf is a multiple of the alignment and every shelf is
        //      larger than the one before it.
        b32 class = 0;

        while (class < ALLOCATOR_CLASSES)
        {
                if (allocator_class_size[class] % ALLOCATOR_ALIGNMENT)
                        own_failures++;

                if (class && allocator_class_size[class] <=
                                     allocator_class_size[class - 1])
                        own_failures++;

                class++;
        }

        say_line("classes", ALLOCATOR_CLASSES);
        say_line("class-table-faults", own_failures);
        check_true(own_failures == 0);

        //      The arithmetic in allocator_class_of, against the table it is
        //      not allowed to look at: for every request the shelf it names
        //      has to be large enough, and the shelf below it has to be too
        //      small, which is what makes it the smallest one that fits.
        positive want = 1;
        positive lookup_failures = 0;

        while (want <= ALLOCATOR_LARGEST)
        {
                b32 found = allocator_class_of(want);

                if (found >= ALLOCATOR_CLASSES)
                        lookup_failures++;
                else
                {
                        if (allocator_class_size[found] < want)
                                lookup_failures++;

                        if (found && allocator_class_size[found - 1] >= want)
                                lookup_failures++;
                }

                want++;
        }

        say_line("lookup-faults", lookup_failures);
        say_line("lookup-above-largest",
                 (positive)allocator_class_of(ALLOCATOR_LARGEST + 1));
        check_true(lookup_failures == 0);
        check_true(allocator_class_of(ALLOCATOR_LARGEST + 1) ==
                   ALLOCATOR_CLASSES);

        //      What the smallest requests actually cost, which is the number
        //      the eight byte header exists for.
        address_any block = malloc(0);
        say_line("usable-of-0", malloc_usable_size(block));
        free(block);

        block = malloc(8);
        say_line("usable-of-8", malloc_usable_size(block));
        free(block);

        block = malloc(24);
        say_line("usable-of-24", malloc_usable_size(block));
        free(block);

        block = malloc(1000);
        say_line("usable-of-1000", malloc_usable_size(block));
        free(block);

        //      A freed block of a shelf comes straight back for the next
        //      request of that shelf, which is the whole point of the lists.
        address_any first = malloc(100);
        free(first);
        address_any again = malloc(100);

        say_line("reused", (positive)(first == again));
        check_true(first == again);
        free(again);

        //      Sizes inside one shelf do not move the block.
        address_any held = malloc(100);
        address_any same = realloc(held, 96);

        say_line("resize-in-place", (positive)(held == same));
        check_true(held == same);
        free(same);

        /*
                A second free of the same pointer, which cannot be asked of
                glibc in the shared half above because glibc answers it by
                killing the program. Here it is a no-op, and the proof is that
                the next two allocations of that shelf are two blocks and not
                one: without the freed tag the list would point at the block
                twice and hand the same address out twice.
        */
        address_any twice = malloc(48);

        free(twice);
        free(twice);

        address_any one = malloc(48);
        address_any two = malloc(48);

        say_line("double-free-distinct", (positive)(one != two));
        check_true(one != two);
        free(one);
        free(two);

        //      And a pointer this allocator never handed out, with a word in
        //      front of it that is not a tag it wrote. Nothing is written and
        //      no list is touched.
        static positive stranger[8];

        stranger[0] = 0x5AFE5AFE5AFE5AFEull;
        free(address_of stranger[1]);

        say_line("stranger-untouched",
                 (positive)(stranger[0] == 0x5AFE5AFE5AFE5AFEull));
        check_true(stranger[0] == 0x5AFE5AFE5AFE5AFEull);
}
#endif

b32 main(void)
{
        check_torture();
        check_corners();

        say_line("allocations", check_allocations);
        say_line("frees", check_frees);
        say_line("resizes", check_resizes);
        say_line("verifies", check_verifies);
        say_line("refused", check_refused);
        say_line("failures", check_failures);
        say("shared-end\n");

#ifndef ALLOCATOR_REFERENCE
        check_own();
#endif

        //      The verdict in the shape test/run reads, after the line
        //      the glibc diff stops at so that the two builds are allowed to
        //      have counted different numbers of checks.
        say_number(check_checks);
        say(" checks, ");
        say_number(check_failures);
        say(" failures\n");

        say_flush();
        return check_failures ? 1 : 0;
}
#endif /* CHECK_allocator */

#ifdef CHECK_reserve
#include "../../src/lib.util.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/* Exercise the real mapping owner on Linux, including movement and the
   allocate/copy fallback. 64 KiB boundaries also suit larger-page ARM hosts. */
#define RESERVE_QUANTUM 65536

static fn reserve_boundaries(void)
{
        address_any held = null;
        positive have = 0, used = 0;

        check("initial reserve", memory_reserve(address_of held, address_of have,
              0, 1, sizeof(positive), 64));
        if (!held)
                return;
        check("geometric initial capacity", have == 64);
        positive address_to words = held;
        for (used = 0; used < have; used++)
                words[used] = used ^ 0xa55a;
        address_any before = held;
        check("capacity hit", memory_reserve(address_of held, address_of have,
              used, have, sizeof(positive), 64) && held == before);
        check("growth with used beyond capacity is refused", !memory_reserve(
              address_of held, address_of have, have + 1, have + 1,
              sizeof(positive), 64));
        check("invalid count leaves owner alone", held == before && have == 64);
        check("growth preserves elements", memory_reserve(address_of held,
              address_of have, used, 65, sizeof(positive), 64));
        words = held;
        for (positive i = 0; i < used; i++)
                check("element survived", words[i] == (i ^ 0xa55a));
        before = held;
        positive old_room = have;
        check("overflow is refused", !memory_reserve(address_of held,
              address_of have, used, positive_max, sizeof(positive), 64));
        check("overflow leaves owner alone", held == before && have == old_room);
        memory_release(address_of held, address_of have, address_of used,
                       sizeof(positive));
        check("release resets owner", !held && !have && !used);
}

static fn reserve_mapping(p8 split)
{
        positive half = RESERVE_QUANTUM;
        address_any allocation = memory(half * 2);
        check("mapping fixture allocated", allocation &&
              (positive)allocation < (positive)-4095);
        if (!allocation || (positive)allocation >= (positive)-4095)
                return;
        p8 address_to bytes = allocation;
        bytes[0] = 0x5a;
        bytes[half - 1] = 0xa5;
        bytes[half] = 0x7c;
        bipolar protected = system_call_3(syscall(mprotect),
              (positive)(bytes + half), half, split ? FILE_PROTECT_READ : 0);
        check("mapping protection split", protected == 0);
        if (protected)
        {
                memory_free(allocation, half * 2);
                return;
        }

        /* A guard beyond a single VMA forces movement. A read-only second
           VMA inside the owned range refuses a growing mremap and exercises
           its copy fallback instead. Both must preserve the owned bytes. */
        positive have = split ? half * 2 : half;
        positive used = have;
        p8 grew = memory_reserve(address_of allocation, address_of have,
                                   used, have + 1, 1, half);
        check("split mapping grows", grew);
        check("blocked mapping moved", !grew || allocation != bytes);
        if (grew)
        {
                p8 address_to moved = allocation;
                check("mapped bytes survive", moved[0] == 0x5a &&
                      moved[half - 1] == 0xa5);
                if (split)
                {
                        check("fallback copied second VMA", moved[half] == 0x7c);
                        moved[half] = 0x6d;
                        check("fallback is writable", moved[half] == 0x6d);
                }
        }
        memory_release(address_of allocation, address_of have, address_of used, 1);
        if (!split)
                memory_free(bytes + half, half);
}

static fn reserve_sparse(void)
{
        positive have = 2 * 1024 * 1024;
        positive used = have;
        address_any held = memory(have);
        if (!held || (positive)held >= (positive)-4095)
        {
                check("sparse fixture allocated", false);
                return;
        }
        /* This test needs base-page residency, not a transparent huge-page
           fault populating its whole 2 MiB span when the first byte is set. */
        system_call_3(syscall(madvise), (positive)held, have, 15);
        ((p8 address_to)held)[0] = 0x3e;
        p8 residency[16] = {0};
        bipolar queried = system_call_3(syscall(mincore),
              (positive)held + RESERVE_QUANTUM, RESERVE_QUANTUM,
              (positive)residency);
        check("initial sparse residency query", queried == 0);
        p8 sparse = queried == 0 && !(residency[0] & 1);
        check("sparse reserve grows", memory_reserve(address_of held,
              address_of have, used, used + 1, 1, 64));
        queried = system_call_3(syscall(mincore),
              (positive)held + RESERVE_QUANTUM, RESERVE_QUANTUM,
              (positive)residency);
        check("sparse residency query", queried == 0);
        if (sparse)
                check("growth did not populate untouched pages", !queried &&
                      !(residency[0] & 1));
        else
                log_direct(str("reserve: sparse RAM assertion NOT RUN -- pages already resident (huge-page advice may be ignored under emulation)\n"));
        check("sparse live byte preserved", ((p8 address_to)held)[0] == 0x3e);
        memory_release(address_of held, address_of have, address_of used, 1);
}

static fn reserve_refused(void)
{
        positive have = RESERVE_QUANTUM, used = 1;
        address_any held = memory(have);
        if (!held || (positive)held >= (positive)-4095)
        {
                check("refusal fixture allocated", false);
                return;
        }
        ((p8 address_to)held)[0] = 0x29;
        positive limits[2], constrained[2];
        bipolar answer = system_call_4(syscall(prlimit64), 0, 9, 0,
                                       (positive)limits);
        check("query address-space limit", answer == 0);
        if (!answer)
        {
                constrained[0] = 0;
                constrained[1] = limits[1];
                answer = system_call_4(syscall(prlimit64), 0, 9,
                                        (positive)constrained, 0);
                check("constrain own address space", answer == 0);
                if (!answer)
                {
                        address_any before = held;
                        p8 grew = memory_reserve(address_of held,
                                address_of have, used, 64 * 1024 * 1024, 1, 64);
                        answer = system_call_4(syscall(prlimit64), 0, 9,
                                                (positive)limits, 0);
                        check("restore own address-space limit", answer == 0);
                        check("both remap and allocation refused", !grew);
                        check("refusal preserves mapping and capacity",
                              held == before && have == RESERVE_QUANTUM &&
                              ((p8 address_to)held)[0] == 0x29);
                }
        }
        memory_release(address_of held, address_of have, address_of used, 1);
}

/* The doubling loop memory_growth replaced: the answer it must still give on
   every architecture, for any start, want and first. */
static positive growth_loop(positive have, positive want, positive first)
{
        if (have >= want)
                return have;
        if (!have)
                have = first;
        if (!have)
                return 0;
        while (have < want) {
                if (have >> (sizeof(positive) * 8 - 1))
                        return 0;
                have <<= 1;
        }
        return have;
}

static fn growth_matches_loop(void)
{
        static const positive edge[] = {
                0, 1, 2, 3, 5, 16, 63, 64, 65, 1000, 4096, 1ul << 31, 1ul << 62,
                (1ul << 62) + 1, 1ul << 63, (1ul << 63) + 1, positive_max - 1, positive_max,
        };
        positive mismatches = 0, state = 0x9e3779b97f4a7c15ul;
        for (positive a = 0; a < sizeof edge / sizeof edge[0]; a++)
                for (positive b = 0; b < sizeof edge / sizeof edge[0]; b++)
                        for (positive c = 0; c < sizeof edge / sizeof edge[0]; c++)
                                mismatches += memory_growth(edge[a], edge[b], edge[c]) !=
                                              growth_loop(edge[a], edge[b], edge[c]);
        for (positive i = 0; i < 100000; i++) {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                positive have = state >> (state & 63);
                positive want = (state * 0x2545f4914f6cdd1dul) >> ((state >> 8) & 63);
                positive first = state >> ((state >> 16) & 63);
                mismatches += memory_growth(have, want, first) != growth_loop(have, want, first);
        }
        check("growth matches the doubling loop it replaced", mismatches == 0);
}

b32 main(void)
{
        reserve_boundaries();
        growth_matches_loop();
        reserve_mapping(false);
        reserve_mapping(true);
        reserve_sparse();
        /* User-mode emulators can satisfy guest mappings inside a host VMA
           reserved before the limit was lowered. This cannot prove native
           address-space refusal, so the runner names that case explicitly. */
        if (program_argument_count() > 1 &&
            !string_compare(program_argument(1), (string_address)"--emulated"))
                log_direct(str("reserve: address-space limit assertion NOT RUN under emulation\n"));
        else
                reserve_refused();
        return test_report(null);
}
#endif /* CHECK_reserve */

#ifdef CHECK_utf8
#include "../../src/lib.util.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

// Independent scalar oracle: decode into a code point, then reject overlong,
// surrogate and out-of-range values. Production validates the byte ranges.
static positive utf8_reference_width(p8 address_to data, positive size)
{
        if (!size)
                return 0;
        p32 first = data[0], value, least;
        positive width;
        if (first < 0x80)
                return 1;
        if ((first & 0xe0) == 0xc0)
                width = 2, value = first & 31, least = 0x80;
        else if ((first & 0xf0) == 0xe0)
                width = 3, value = first & 15, least = 0x800;
        else if ((first & 0xf8) == 0xf0)
                width = 4, value = first & 7, least = 0x10000;
        else
                return 1;
        if (size < width)
                return 1;
        for (positive at = 1; at < width; at++)
        {
                if ((data[at] & 0xc0) != 0x80)
                        return 1;
                value = (value << 6) | (data[at] & 63);
        }
        return value < least || value > 0x10ffff ||
                       (value >= 0xd800 && value <= 0xdfff) ? 1 : width;
}

// Every call the first in a process, in riscv64's walks against the guards.
static p8 utf8_first;

static fn utf8_span_check(p8 address_to bytes, positive size, positive count)
{
        positive at = 0, characters = 0;
        while (at < size && characters < count)
        {
                at += utf8_reference_width(bytes + at, size - at);
                characters++;
        }
        VECTOR_FIRST_CALL(utf8_first);
        positive2 got = memory_utf8_span(bytes, size, count);
        check("bounded UTF-8 span", got.x == at && got.y == characters);
}

/*
        memory_utf8_valid_span against the same oracle: the prefix stops at
        the first byte past ASCII that the oracle takes on its own, which is
        both an invalid sequence and one size cuts short.
*/
static positive utf8_valid_wrong;

static fn utf8_valid_check(p8 address_to bytes, positive size)
{
        positive at = 0, characters = 0;
        while (at < size)
        {
                positive width = utf8_reference_width(bytes + at, size - at);
                if (width == 1 && bytes[at] >= 0x80)
                        break;
                at += width;
                characters++;
        }
        VECTOR_FIRST_CALL(utf8_first);
        positive2 got = memory_utf8_valid_span(bytes, size);
        checks++;
        if (got.x != at || got.y != characters)
        {
                failures++;
                if (utf8_valid_wrong++ < 8)
                        string_format(log, "  FAIL memory_utf8_valid_span size %p: "
                                           "%p/%p where %p/%p\n",
                                      size, got.x, got.y, at, characters);
        }
}

//      Characters of each width, well formed, at each bound the strict
//      rules draw; and the sequences that are not, one a kind.
static const p8 utf8_valid_good[][5] = {
        {1, 'a'}, {1, 0x7f}, {2, 0xc2, 0x80}, {2, 0xc3, 0xa9}, {2, 0xdf, 0xbf},
        {3, 0xe0, 0xa0, 0x80}, {3, 0xe6, 0xbc, 0xa2}, {3, 0xed, 0x9f, 0xbf},
        {3, 0xef, 0xbf, 0xbf}, {4, 0xf0, 0x90, 0x80, 0x80},
        {4, 0xf0, 0x9f, 0x99, 0x82}, {4, 0xf4, 0x8f, 0xbf, 0xbf},
};
static const p8 utf8_valid_bad[][5] = {
        {1, 0x80}, {1, 0xbf}, {2, 0xc0, 0x80}, {2, 0xc1, 0xbf}, {1, 0xf5},
        {1, 0xff}, {3, 0xe0, 0x80, 0x80}, {3, 0xe0, 0x9f, 0xbf},
        {3, 0xed, 0xa0, 0x80}, {3, 0xed, 0xbf, 0xbf},
        {4, 0xf0, 0x80, 0x80, 0x80}, {4, 0xf0, 0x8f, 0xbf, 0xbf},
        {4, 0xf4, 0x90, 0x80, 0x80}, {4, 0xf8, 0x88, 0x80, 0x80},
        {2, 0xc3, 'a'}, {2, 0xe6, 0xbc}, {3, 0xf0, 0x9f, 0x99},
        {2, 0xe6, 0xc3}, {3, 0xc3, 0xa9, 0xa9},
};

//      A run of background characters: all ASCII, all of one width, or a
//      mix, with the first character at phase so every block edge falls
//      inside one somewhere.
static fn utf8_valid_fill(p8 address_to out, positive size, positive background,
                          positive phase, positive address_to random)
{
        static const positive only[4] = {0, 3, 6, 10};
        positive at = 0, turn = phase;
        while (at < size)
        {
                positive pick;
                if (background < 4)
                        pick = only[background] + (turn % 2) * (background ? 1 : 0);
                else
                {
                        address_to random = address_to random * 1664525 + 1013904223;
                        pick = (address_to random >> 16) % array_count(utf8_valid_good);
                        if (background == 5 && (address_to random >> 8) % 4)
                                pick = 0;
                }
                const p8 address_to character = utf8_valid_good[pick];
                for (positive i = 0; i < character[0] && at < size; i++)
                        out[at++] = character[1 + i];
                turn++;
        }
}

static fn utf8_valid_checks(p8 address_to guarded, positive quantum)
{
        static p8 bytes[64 + 400 + 64];
        positive random = 0x2545f491;
        for (positive background = 0; background < 6; background++)
                for (positive align = 0; align < 64; align += background < 4 ? 1 : 3)
                        for (positive size = 0; size <= 200; size++)
                        {
                                p8 address_to at = bytes + align;
                                utf8_valid_fill(at, size + 8, background, align + size, address_of random);
                                utf8_valid_check(at, size);
                                // Every place for an error, each kind in turn,
                                // at a quarter of the alignments.
                                if (align % 4 != background % 4)
                                        continue;
                                for (positive place = 0; place < size; place += size > 140 ? 7 : 1)
                                {
                                        const p8 address_to bad = utf8_valid_bad[
                                            (place + size + align + background) % array_count(utf8_valid_bad)];
                                        utf8_valid_fill(at, size + 8, background, align + size, address_of random);
                                        for (positive i = 0; i < bad[0] && place + i < size + 8; i++)
                                                at[place + i] = bad[1 + i];
                                        utf8_valid_check(at, size);
                                }
                        }
        //      Past the tail's reach, with an error far in and none at all.
        for (positive background = 0; background < 6; background++)
                for (positive size = 201; size <= 400; size += 13)
                {
                        utf8_valid_fill(bytes, size, background, size, address_of random);
                        utf8_valid_check(bytes, size);
                        bytes[size - 40] = 0xc0;
                        utf8_valid_check(bytes, size);
                }
        utf8_valid_check(null, 0);
        if (!guarded)
                return;
        //      Against both guards: nothing before the block, nothing after.
        for (positive background = 0; background < 6; background++)
                for (positive size = 0; size <= 200; size++)
                {
                        p8 address_to tail = guarded + quantum * 2 - size;
                        p8 address_to head = guarded + quantum;
                        utf8_valid_fill(tail, size, background, size, address_of random);
                        utf8_valid_check(tail, size);
                        utf8_valid_fill(head, size, background, size, address_of random);
                        utf8_valid_check(head, size);
                        if (size)
                        {
                                tail[size - 1] = 0xe6;
                                utf8_valid_check(tail, size);
                                head[0] = 0x80;
                                utf8_valid_check(head, size);
                        }
                }
}

//      memory_utf8_span over the same runs, long enough and with counts large
//      enough for its block body, the count running out before, inside and
//      after the blocks, and the errors it counts one byte a time.
static fn utf8_span_long_checks(p8 address_to guarded, positive quantum)
{
        static p8 bytes[64 + 400 + 64];
        positive random = 0x6c078965;
        for (positive background = 0; background < 6; background++)
                for (positive align = 0; align < 64; align += 3)
                        for (positive size = 0; size <= 300; size += size < 140 ? 1 : 7)
                        {
                                p8 address_to at = bytes + align;
                                positive counts[] = {1, 31, 32, 33, 63, 64, 65, 96, 97,
                                                     size / 3, size / 2, size - 1, size,
                                                     size + 1, positive_max};
                                utf8_valid_fill(at, size + 8, background, align + size, address_of random);
                                if ((size + align) % 3 == 0 && size)
                                {
                                        const p8 address_to bad = utf8_valid_bad[
                                            (size + background) % array_count(utf8_valid_bad)];
                                        positive place = (size * 7 + align) % size;
                                        for (positive i = 0; i < bad[0] && place + i < size + 8; i++)
                                                at[place + i] = bad[1 + i];
                                }
                                for (positive c = 0; c < array_count(counts); c++)
                                        utf8_span_check(at, size, counts[c]);
                        }
        if (!guarded)
                return;
        for (positive background = 0; background < 6; background++)
                for (positive size = 64; size <= 200; size++)
                {
                        p8 address_to tail = guarded + quantum * 2 - size;
                        p8 address_to head = guarded + quantum;
                        utf8_valid_fill(tail, size, background, size, address_of random);
                        utf8_span_check(tail, size, positive_max);
                        utf8_span_check(tail, size, size / 2);
                        utf8_valid_fill(head, size, background, size, address_of random);
                        utf8_span_check(head, size, positive_max);
                        head[0] = 0x80;
                        utf8_span_check(head, size, positive_max);
                }
}

/*
        memory_ascii_span: the ASCII prefix of a run of bytes with a byte
        of every kind past ASCII planted at every place, at alignments that
        put the 64, 16 and 8 byte steps and the overlapping tail in turn on
        the boundary, and against both guard pages.
*/
static positive ascii_wrong;

static fn ascii_span_check(p8 address_to bytes, positive size)
{
        positive at = 0;
        while (at < size && bytes[at] < 0x80)
                at++;
        positive got = memory_ascii_span(bytes, size);
        checks++;
        if (got != at)
        {
                failures++;
                if (ascii_wrong++ < 8)
                        string_format(log, "  FAIL memory_ascii_span size %p: %p where %p\n",
                                      size, got, at);
        }
}

static fn ascii_span_checks(p8 address_to guarded, positive quantum)
{
        static p8 bytes[64 + 400 + 64];
        static const p8 high[] = {0x80, 0xc3, 0xff, 0xa0, 0xf0};
        positive random = 0x3c6ef372;
        for (positive align = 0; align < 64; align += align < 20 ? 1 : 7)
                for (positive size = 0; size <= 300; size++)
                {
                        p8 address_to at = bytes + align;
                        for (positive i = 0; i < size + 8; i++)
                        {
                                random = random * 1664525 + 1013904223;
                                at[i] = (i & 7) ? (random >> 16) & 0x7f : (p8)(random >> 16) % 3 ? 'a' : 0;
                        }
                        ascii_span_check(at, size);
                        for (positive place = 0; place < size; place += size > 140 ? 5 : 1)
                        {
                                p8 keep = at[place];
                                at[place] = high[(place + size) % array_count(high)];
                                ascii_span_check(at, size);
                                if (place + 1 < size)
                                {
                                        //  A second past ASCII after it changes nothing.
                                        at[place + 1] = 0xff;
                                        ascii_span_check(at, size);
                                        at[place + 1] = 'b';
                                }
                                at[place] = keep;
                        }
                }
        ascii_span_check(null, 0);
        if (!guarded)
                return;
        for (positive size = 0; size <= 200; size++)
        {
                p8 address_to tail = guarded + quantum * 2 - size;
                p8 address_to head = guarded + quantum;
                memory_fill(tail, 'x', size);
                memory_fill(head, 'x', size);
                ascii_span_check(tail, size);
                ascii_span_check(head, size);
                for (positive place = 0; place < size; place += 3)
                {
                        tail[place] = 0x80;
                        head[place] = 0xff;
                        ascii_span_check(tail, size);
                        ascii_span_check(head, size);
                        tail[place] = head[place] = 'x';
                }
        }
}

/*
        Mutation fuzz against the oracle: buffers built from well-formed
        pieces and from every malformed class the strict rules draw (the
        overlong forms, surrogates, past U+10FFFF, stray continuations, leads
        that no sequence follows, sequences cut off at the end), then a few
        bytes replaced by the values that sit on a boundary or by anything,
        and the size chosen so that a sequence begins, ends or is cut in the
        last bytes of a 16, 32 or 64 byte block. Each buffer asks
        memory_utf8_valid_span, memory_ascii_span and memory_utf8_span at
        counts that fall before, inside and after the blocks. Every arm of the
        block bodies and the walk under them meets errors at every offset.
*/
static positive utf8_fuzz_next(positive address_to state)
{
        address_to state = address_to state * 1664525 + 1013904223;
        return address_to state >> 8;
}

static fn utf8_fuzz(positive rounds, positive seed)
{
        static p8 bytes[64 + 1536 + 64];
        static const p8 edge[] = {0x00, 0x7f, 0x80, 0x8f, 0x90, 0x9f, 0xa0, 0xbf,
                                  0xc0, 0xc1, 0xc2, 0xdf, 0xe0, 0xe1, 0xec, 0xed,
                                  0xef, 0xf0, 0xf1, 0xf3, 0xf4, 0xf5, 0xf7, 0xf8, 0xff};
        static const p8 piece[][5] = {
                {1, 'a'}, {1, ' '}, {2, 0xc2, 0x80}, {2, 0xc3, 0xa9}, {2, 0xdf, 0xbf},
                {3, 0xe0, 0xa0, 0x80}, {3, 0xe6, 0xbc, 0xa2}, {3, 0xed, 0x9f, 0xbf},
                {3, 0xef, 0xbf, 0xbf}, {4, 0xf0, 0x90, 0x80, 0x80},
                {4, 0xf0, 0x9f, 0x99, 0x82}, {4, 0xf4, 0x8f, 0xbf, 0xbf},
                {2, 0xc0, 0x80}, {2, 0xc1, 0xbf}, {3, 0xe0, 0x80, 0x80},
                {3, 0xe0, 0x9f, 0xbf}, {3, 0xed, 0xa0, 0x80}, {3, 0xed, 0xbf, 0xbf},
                {4, 0xf0, 0x80, 0x80, 0x80}, {4, 0xf0, 0x8f, 0xbf, 0xbf},
                {4, 0xf4, 0x90, 0x80, 0x80}, {4, 0xf7, 0xbf, 0xbf, 0xbf},
                {1, 0x80}, {1, 0xbf}, {1, 0xf8}, {1, 0xff}, {1, 0xc3}, {2, 0xe6, 0xbc},
                {3, 0xf0, 0x9f, 0x99}, {2, 0xc3, 'x'}, {2, 0xe2, 0x82},
        };
        positive random = seed;

        for (positive round = 0; round < rounds; round++)
        {
                positive align = utf8_fuzz_next(address_of random) % 64;
                positive shape = utf8_fuzz_next(address_of random) % 8;
                positive size = utf8_fuzz_next(address_of random) % 4 == 0 ? utf8_fuzz_next(address_of random) % 40
                                                     : 16 * (utf8_fuzz_next(address_of random) % 20) + utf8_fuzz_next(address_of random) % 3 - 1 + (utf8_fuzz_next(address_of random) % 4 ? 0 : 16);
                p8 address_to at = bytes + align;
                positive fill = 0;

                //  Now and then long, for the blocks of sixty four.
                if (size > 500)
                        size = utf8_fuzz_next(address_of random) % 8 ? 500 : 500 + utf8_fuzz_next(address_of random) % 1000;
                //  Mostly well formed, so an error is found late as often as early.
                positive bad = shape < 3 ? 1 + utf8_fuzz_next(address_of random) % 40 : 0;
                while (fill < size + 8)
                {
                        positive pick = bad && utf8_fuzz_next(address_of random) % bad == 0
                                            ? 12 + utf8_fuzz_next(address_of random) % (array_count(piece) - 12)
                                        : shape == 7 ? utf8_fuzz_next(address_of random) % 2
                                                     : utf8_fuzz_next(address_of random) % 12;
                        for (positive k = 0; k < piece[pick][0] && fill < size + 8; k++)
                                at[fill++] = piece[pick][1 + k];
                }
                //  Then a few bytes made what a boundary is made of, or anything.
                for (positive m = utf8_fuzz_next(address_of random) % 4 == 0 ? utf8_fuzz_next(address_of random) % 4 : 0; m; m--)
                        at[utf8_fuzz_next(address_of random) % (size + 1)] = utf8_fuzz_next(address_of random) % 2 ? edge[utf8_fuzz_next(address_of random) % array_count(edge)]
                                                                        : (p8)utf8_fuzz_next(address_of random);
                //  A sequence cut by size: the last piece stops short.
                if (size && utf8_fuzz_next(address_of random) % 6 == 0)
                        at[size - 1] = 0xe0 + utf8_fuzz_next(address_of random) % 32;

                utf8_valid_check(at, size);
                ascii_span_check(at, size);
                positive counts[] = {1, 2, 31, 32, 33, 47, 48, 64, utf8_fuzz_next(address_of random) % (size + 2),
                                     size, size + 1, positive_max};
                for (positive c = 0; c < array_count(counts); c++)
                        utf8_span_check(at, size, counts[c]);
        }
}

//      Once per body: on x86_64 the AVX2 validator and the walk under it.
static fn utf8_valid_tiers(p8 address_to guarded, positive quantum)
{
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? avx512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : avx2;
                utf8_valid_checks(guarded, quantum);
                utf8_span_long_checks(guarded, quantum);
                utf8_fuzz(60000, 0x9e3779b9 + tier);
        }
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#else
        //      On riscv64 every call of the walks the first in a process,
        //      which asks for V and comes back to the routine's entry; the
        //      fuzz asks once.
        utf8_first = true;
        utf8_valid_checks(guarded, quantum);
        utf8_span_long_checks(guarded, quantum);
        utf8_first = false;
        utf8_fuzz(100000, 0x9e3779b9);
#endif
}

static positive utf8_encode_reference(p8 address_to out, positive scalar)
{
        if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar < 0xe000))
                return 0;
        if (scalar < 128)
        {
                out[0] = scalar;
                return 1;
        }
        positive size = scalar < 2048 ? 2 : scalar < 65536 ? 3 : 4;
        for (positive at = size - 1; at; at--)
        {
                out[at] = 128 + scalar % 64;
                scalar /= 64;
        }
        out[0] = (size == 2 ? 192 : size == 3 ? 224 : 240) + scalar;
        return size;
}

static fn utf8_encode_check(p8 address_to out, positive room, positive scalar)
{
        p8 expected[4];
        positive size = utf8_encode_reference(expected, scalar);
        if (size > room)
                size = 0;
        memory_fill(out, 0xa5, room);
        positive got = memory_utf8_encode(out, room, scalar);
        p8 exact = got == size;
        for (positive at = 0; at < room; at++)
                if (out[at] != (at < size ? expected[at] : 0xa5))
                        exact = false;
        check("UTF-8 scalar bytes, bounds and transactional failure", exact);
}

static fn utf8_stream_checks()
{
        p8 bytes[4];
        for (positive byte = 0; byte < 256; byte++)
        {
                memory_utf8_state state = {0};
                b32 expected = byte < 0x80 ? 1 : byte >= 0xc0 && byte < 0xf8 ? 0 : -1;
                check("stream classifies every leading byte", memory_utf8_feed(address_of state, byte) == expected);
        }
        for (positive scalar = 0; scalar <= 0x10ffff; scalar++)
        {
                positive width = utf8_encode_reference(bytes, scalar);
                if (!width)
                        continue;
                memory_utf8_state state = {0};
                p8 valid = true;
                for (positive at = 0; at < width; at++)
                        if (memory_utf8_feed(address_of state, bytes[at]) !=
                            (at + 1 == width ? 1 : 0))
                                valid = false;
                check("stream every Unicode scalar", valid && state.value == scalar && !state.left);
        }
        for (positive lead = 0xc0; lead < 0xf8; lead++)
        {
                positive width = lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                for (positive changed = 1; changed < width; changed++)
                        for (positive byte = 0; byte < 256; byte++)
                        {
                                memory_utf8_state state = {0};
                                memory_fill(bytes, 0x80, sizeof(bytes));
                                bytes[0] = lead;
                                bytes[changed] = byte;
                                b32 result = 0;
                                for (positive at = 0; at < width && result >= 0; at++)
                                        result = memory_utf8_feed(address_of state, bytes[at]);
                                p8 valid = utf8_reference_width(bytes, width) == width;
                                check("stream malformed scalar boundaries", result == (valid ? 1 : -1) && !state.left);
                                check("stream recovers after rejection", memory_utf8_feed(address_of state, 'Q') == 1 && state.value == 'Q');
                        }
        }
}

b32 main()
{
        utf8_stream_checks();
        static const positive scalars[] = {
            0, 1, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xd800,
            0xdfff, 0xe000, 0xffff, 0x10000, 0x10ffff, 0x110000,
            0xffffffff, positive_max,
        };
        const p8 sample[] = {
            'A', 'B', 0, 0x7f, 0xc2, 0x80, 0xdf, 0xbf,
            0xe0, 0xa0, 0x80, 0xed, 0x9f, 0xbf, 0xee, 0x80, 0x80,
            0xf0, 0x90, 0x80, 0x80, 0xf4, 0x8f, 0xbf, 0xbf,
            0xc0, 0x80, 0xc1, 0xbf, 0xe0, 0x9f, 0xbf,
            0xed, 0xa0, 0x80, 0xf0, 0x8f, 0xbf, 0xbf,
            0xf4, 0x90, 0x80, 0x80, 0xf5, 0x80, 0x80, 0x80,
            0xff, 0x80, 0xbf, 0xe2, 'x', 0x80, 0xf0, 0x90, 'x', 0x80
        };
        p8 bytes[160];
        positive random = 0x7f6a29c3;
        utf8_span_check(null, 0, positive_max);
        utf8_span_check(null, 9, 0);
        utf8_encode_check(null, 0, 0);
        for (positive align = 1; align <= 16; align++)
                for (positive room = 0; room <= 5; room++)
                        for (positive at = 0; at < array_count(scalars); at++)
                        {
                                bytes[align - 1] = bytes[align + room] = 0xa5;
                                utf8_encode_check(bytes + align, room, scalars[at]);
                                check("UTF-8 scalar output canaries",
                                      bytes[align - 1] == 0xa5 &&
                                      bytes[align + room] == 0xa5);
                        }
        for (positive at = 0; at < 4096; at++)
        {
                random = random * 1664525 + 1013904223;
                utf8_encode_check(bytes + (at & 15), 4, random % 0x120000);
        }

        for (positive shape = 0; shape < 4; shape++)
                for (positive align = 0; align < 16; align++)
                {
                        for (positive i = 0; i < 128; i++)
                        {
                                random = random * 1664525 + 1013904223;
                                bytes[align + i] = shape == 0 ? 'a' :
                                    shape == 1 ? sample[i % sizeof(sample)] :
                                    shape == 2 ? (p8)(random >> 16) :
                                    i < 64 ? 'x' : sample[i % sizeof(sample)];
                        }
                        for (positive size = 0; size <= 128; size++)
                                for (positive count = 0; count <= size + 1; count++)
                                        utf8_span_check(bytes + align, size, count);
                }

        // Every lead/second-byte pair, including partial three/four-byte
        // sequences, distinguishes continuation checks and special bounds.
        for (positive lead = 0; lead < 256; lead++)
                for (positive second = 0; second < 256; second++)
                {
                        bytes[0] = lead; bytes[1] = second;
                        bytes[2] = 0x80; bytes[3] = 0x80;
                        for (positive size = 1; size <= 4; size++)
                                utf8_span_check(bytes, size, 1);
                }

        positive quantum = 65536;
        p8 address_to guarded = memory(quantum * 3);
        check("UTF-8 guard mapping", guarded && (positive)guarded < positive_max - 4095);
        if (guarded && (positive)guarded < positive_max - 4095)
        {
                check("UTF-8 left guard", system_call_3(syscall(mprotect),
                      (positive)guarded, quantum, 0) == 0);
                check("UTF-8 right guard", system_call_3(syscall(mprotect),
                      (positive)(guarded + quantum * 2), quantum, 0) == 0);
                for (positive room = 0; room <= 4; room++)
                        for (positive at = 0; at < array_count(scalars); at++)
                                utf8_encode_check(guarded + quantum * 2 - room,
                                                  room, scalars[at]);
                for (positive size = 0; size <= 96; size++)
                {
                        p8 address_to tail = guarded + quantum * 2 - size;
                        for (positive i = 0; i < size; i++)
                                tail[i] = sample[i % sizeof(sample)];
                        for (positive count = 0; count <= size + 1; count++)
                                utf8_span_check(tail, size, count);
                }
                utf8_span_check(guarded, 0, 8);
                utf8_span_check(guarded, 8, 0);
                utf8_valid_tiers(guarded, quantum);
                ascii_span_checks(guarded, quantum);
                memory_free(guarded, quantum * 3);
        }
        else
        {
                utf8_valid_tiers(null, 0);
                ascii_span_checks(null, 0);
        }
        return test_report(null);
}
#endif /* CHECK_utf8 */

