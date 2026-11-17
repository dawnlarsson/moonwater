/*
        test/checks/stdio.c: format and scan, streams and spools, and their glibc twins.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_format_standalone
#define FORMAT_STANDALONE
#define CHECK_format
#endif
#if defined(FORMAT_STANDALONE) && \
    (defined(CHECK_format) || defined(CHECK_format_differential))
/*
        printf on lib.c and lib.util.c alone, with none of the
        families it normally sits among. They live in one file now, so the
        way to ask for one of them is to skip the rest by name. Not by
        claiming their include guards: format.c carries its own minimal FILE
        and errno for exactly this configuration, guarded on stream.c's and
        error.c's guards, so claiming those would take the fallback away with
        the family. CHECK_format_differential takes the same arrangement when
        the format lane builds it with FORMAT_STANDALONE.

        The allocator is the reason this matters rather than being tidy. It
        reaches for top_bit_known and memory_give, which the umbrella
        defines, so a build without the umbrella cannot compile it -- which
        is exactly what this configuration is, and exactly what it is for.
*/
#define STANDARD_SKIP_ERROR
#define STANDARD_SKIP_LOCK
#define STANDARD_SKIP_ALLOCATOR
#define STANDARD_SKIP_NUMBERS
#define STANDARD_SKIP_STDLIB
#define STANDARD_SKIP_CLOCK
#define STANDARD_SKIP_MATH
#define STANDARD_SKIP_SIGNAL
#define STANDARD_SKIP_STREAM
#define STANDARD_SKIP_SCAN
#define STANDARD_SKIP_SPOOL
#define STANDARD_SKIP_PROCESS
#define LIB_SKIP_UMBRELLA
#endif
#ifdef CHECK_format
#include "../../src/lib.util.c"
#if defined(FORMAT_STANDALONE) && \
    (defined(__SSP__) || defined(__SSP_ALL__) || defined(__SSP_STRONG__) || \
     defined(__SSP_EXPLICIT__))
//      The stack protector's guard and its failure live with stdlib, which
//      this arrangement leaves out; a build with the protector on (MW_HARDEN=1)
//      would not link without them.
KEEP unsigned long __stack_chk_guard = 0x2f5a8c1d93e6b700ul;

KEEP DEAD_END fn __stack_chk_fail(void)
{
        system_call_1(syscall(exit_group), 134);
        __builtin_unreachable();
}
#endif
/*
        Experimental C standard library

        printf, pinned against the answers a real glibc gave

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        What printf answers is compared with the machine's glibc, not pinned
        here: CHECK_format_differential walks a grid of every flag spelling,
        width, precision, length modifier, conversion and value kind, some
        four million cases, and draws four million floats besides, on all
        three machines and in this section's standalone arrangement too. What
        stays below is what a comparison cannot state: the bounded write, the
        entry points against each other, the messages and the streams.

        The stream entries -- printf, fprintf, puts, putchar, fputs and fputc
        -- are checked by putting a pipe on the standard output and reading
        back what they wrote, which works whether or not the stream family is
        in the tree. It has been run both ways: alone, where a handle is the
        descriptor and a write is one trap, and on top of the allocator, the
        stream family and the error family together, where the same calls go
        through a buffered stream and a fuller message table. Two of the
        message checks are the fallback table's own wording and are skipped in
        the second arrangement, which is why the count differs by two.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 room[8192];
static p8 second[8192];

/*
        One pinned case: the bytes have to match and the answer has to be the
        length of them. Failures print all three so a regression says what it
        did rather than only that it did something.
*/
static fn pinned(string_address want, string_address format, bipolar answered)
{
        positive wanted = string_length(want);

        checks++;

        if (string_compare(room, want) == 0 && answered == (bipolar)wanted)
                return;

        failures++;
        string_format(log, "  FAIL [%s] wanted [%s] got [%s] answer %b\n",
                      format, want, room, (b32)answered);
}

#define CASE(want, fmt, ...)                                                  \
        pinned((string_address)(want), (string_address)(fmt),                 \
               snprintf(room, sizeof(room), (string_address)(fmt), __VA_ARGS__))

#define CASE0(want, fmt)                                                      \
        pinned((string_address)(want), (string_address)(fmt),                 \
               snprintf(room, sizeof(room), (string_address)(fmt)))

/*
        What the grid in CHECK_format_differential cannot say: several
        conversions in one format, a star on a percent, and formats with no
        conversion in them at all. A percent is a percent, a literal is a
        literal, and a conversion nobody knows keeps its percent. Every
        single-conversion case this section once pinned is a point on that
        grid, compared with glibc on all three machines.
*/
static fn shapes(void)
{
        CASE("007   |ab|9", "%*.*d|%.*s|%d", -6, 3, 7, 2, "abcd", 9);
        CASE("%|7", "%*.*%|%d", 5, 2, 7);
        CASE0("%", "%%");
        CASE0("100% sure", "100%% sure");
        CASE0("%y", "%y");
        CASE0("a%zzb", "a%zzb");
        CASE0("ends with ", "ends with %");
        CASE0("", "");
}

/*
        The bounded write, which is where a hand-written printf usually breaks.

        The answer is the length the format wanted, not the length it got, and
        a size of zero touches nothing at all -- the buffer is poisoned before
        every call and the poison has to survive.
*/
static fn bounded(void)
{
        p8 poison[64];
        positive index;
        bipolar answered;
        p8 untouched;

        for (index = 0; index < sizeof(poison); index++)
                poison[index] = 0xa5;

        answered = snprintf(poison, 0, (string_address) "%d", -123456);
        untouched = true;

        for (index = 0; index < sizeof(poison); index++)
                untouched = untouched && poison[index] == 0xa5;

        check("a size of zero writes the length and nothing else",
              answered == 7 && untouched);

        answered = snprintf(null, 0, (string_address) "%s%d", "abc", 42);
        check("a size of zero accepts a null buffer", answered == 5);

        answered = snprintf(poison, 1, (string_address) "%d", -123456);
        check("a size of one writes only the terminator",
              answered == 7 && poison[0] == end && poison[1] == 0xa5);

        answered = snprintf(poison, 4, (string_address) "%d", -123456);
        check("a short size truncates and still counts the whole",
              answered == 7 && string_compare(poison, "-12") == 0 &&
                  poison[4] == 0xa5);

        answered = snprintf(poison, 8, (string_address) "%d", -123456);
        check("an exact size fits with its terminator",
              answered == 7 && string_compare(poison, "-123456") == 0 &&
                  poison[8] == 0xa5);

        answered = snprintf(poison, 32, (string_address) "%10s|", "ab");
        check("padding is truncated like anything else",
              answered == 11 && string_compare(poison, "        ab|") == 0);

        //      A width far past any buffer anybody sizes by hand.
        answered = snprintf(room, sizeof(room), (string_address) "%2000d", 7);
        check("a two thousand column field is two thousand columns",
              answered == 2000 && room[0] == ' ' && room[1998] == ' ' &&
                  room[1999] == '7' && room[2000] == end);

        answered = snprintf(room, sizeof(room), (string_address) "%.2000d", 7);
        check("a two thousand digit precision is two thousand digits",
              answered == 2000 && room[0] == '0' && room[1999] == '7');

        answered = snprintf(room, sizeof(room), (string_address) "%.1000f", 0.1);
        check("a thousand exact places", answered == 1002 &&
                                             room[0] == '0' && room[1] == '.' &&
                                             room[2] == '1' && room[56] == '5' &&
                                             room[1001] == '0');

        //      The grammar carries int-sized fields.  Refusing an oversized
        //      literal is both glibc's answer and what keeps a wrapped width
        //      from silently turning back into a small, valid request.
        errno = 0;
        answered = snprintf(poison, sizeof(poison),
                            (string_address) "%18446744073709551616d", 7);
        check("a native-word-overflowing width is refused",
              answered == -1 && errno == EOVERFLOW && poison[0] == end);

        errno = 0;
        answered = snprintf(poison, sizeof(poison),
                            (string_address) "%.2147483648d", 7);
        check("a precision above int is refused",
              answered == -1 && errno == EOVERFLOW && poison[0] == end);

        errno = 0;
        answered = snprintf(poison, sizeof(poison),
                            (string_address) "%2147483648.*d", 1, 7);
        check("oversized width rejects a later starred precision",
              answered == -1 && errno == EOVERFLOW && poison[0] == end);

        //      Counting a truncated padding run is constant work after the
        //      resident prefix has been written, even when the answer itself
        //      is too large once the surrounding literal is included.
        errno = 0;
        answered = snprintf(poison, 4,
                            (string_address) "x%2147483647d", 7);
        check("an unrepresentable total keeps the bounded prefix",
              answered == -1 && errno == EOVERFLOW && poison[0] == 'x' &&
                  poison[1] == ' ' && poison[2] == ' ' && poison[3] == end);

        answered = snprintf(null, 0, (string_address) "%100000000d", 7);
        check("a count-only wide field returns its exact width",
              answered == 100000000);

        {
                bipolar at_63 = snprintf(poison, 4,
                                         (string_address) "%63d", 7);
                bipolar at_64 = snprintf(poison, 4,
                                         (string_address) "%64d", 7);
                bipolar at_65 = snprintf(poison, 4,
                                         (string_address) "%65d", 7);
                bipolar at_66 = snprintf(poison, 4,
                                         (string_address) "%66d", 7);

                check("padding agrees across the retained-run cutoff",
                      at_63 == 63 && at_64 == 64 && at_65 == 65 &&
                          at_66 == 66 &&
                          poison[0] == ' ' && poison[1] == ' ' &&
                          poison[2] == ' ' && poison[3] == end);
        }
}

/*
        Everything that is not snprintf. sprintf and the four v-forms have to
        agree with it, since they are the same engine with a different
        destination, and format_to_writer has to put the same bytes through a
        writer that the buffer entries put in a buffer.
*/
static p8 collected[512];
static positive collected_used;
static positive collected_calls;

static fn collect(address_any data, positive length)
{
        if (length == 0)
                length = string_length((string_address)data);

        memory_copy(collected + collected_used, data, length);
        collected_used += length;
        collected_calls++;
}

static bipolar forward_vsnprintf(p8 address_to into, positive size,
                                 string_address format, ...)
{
        var_args list;
        bipolar answered;

        var_list(list, format);
        answered = vsnprintf(into, size, format, list);
        var_list_end(list);

        return answered;
}

static bipolar forward_vsprintf(p8 address_to into, string_address format, ...)
{
        var_args list;
        bipolar answered;

        var_list(list, format);
        answered = vsprintf(into, format, list);
        var_list_end(list);

        return answered;
}

static fn entries(void)
{
        bipolar one;
        bipolar two;

        one = sprintf(room, (string_address) "%d|%s|%.3f|%x", -42,
                      (string_address) "text", 2.5, 48879u);
        check("sprintf writes the whole thing",
              one == 19 && string_compare(room, "-42|text|2.500|beef") == 0);

        two = snprintf(second, sizeof(second), (string_address) "%d|%s|%.3f|%x",
                       -42, (string_address) "text", 2.5, 48879u);
        check("sprintf and snprintf agree",
              one == two && string_compare(room, second) == 0);

        one = forward_vsprintf(room, (string_address) "%s %d %g",
                               (string_address) "v", 7, 0.125);
        two = snprintf(second, sizeof(second), (string_address) "%s %d %g",
                       (string_address) "v", 7, 0.125);
        check("vsprintf agrees with snprintf",
              one == two && string_compare(room, second) == 0);

        one = forward_vsnprintf(room, 8, (string_address) "%s and more",
                                (string_address) "little");
        check("vsnprintf truncates and counts",
              one == 15 && string_compare(room, "little ") == 0);

        one = forward_vsnprintf(null, 0, (string_address) "%s and more",
                                (string_address) "little");
        check("vsnprintf at size zero counts alone", one == 15);

        collected_used = 0;
        one = format_to_writer(collect, (string_address) "%s %d %.4f %x %c|",
                               (string_address) "writer", -17, 2.71828, 48879u,
                               'q');
        collected[collected_used] = end;
        two = snprintf(second, sizeof(second),
                       (string_address) "%s %d %.4f %x %c|",
                       (string_address) "writer", -17, 2.71828, 48879u, 'q');
        check("a writer receives what a buffer receives",
              one == two && (positive)one == collected_used &&
                  string_compare(collected, second) == 0);

        //      A run of plain text crosses the sink once, which is the whole
        //      reason the scan looks for the next percent rather than the
        //      next byte.
        collected_used = 0;
        collected_calls = 0;
        format_to_writer(collect,
                         (string_address) "a line with one %s in it\n",
                         (string_address) "specifier");
        check("plain runs are not emitted one byte at a time",
              collected_used == 32 && collected_calls == 3);
}

/*
        The sentences perror puts after the colon.

        The four that are checked unconditionally are the four the fallback
        table here and the error family's full one agree on word for word. The
        two below them are not: the error family answers "Unknown error 4095"
        with the number in it, and reads a negative code as a negative number
        rather than as its magnitude, both on purpose and both better. Those
        two are checked only when this file's own table is what is compiled.
*/
static fn reasons(void)
{
        check("zero is success", string_compare(strerror(0), "Success") == 0);
        check("two is a missing file",
              string_compare(strerror(2), "No such file or directory") == 0);
        check("thirteen is a refusal",
              string_compare(strerror(13), "Permission denied") == 0);
        check("twenty two is a bad argument",
              string_compare(strerror(22), "Invalid argument") == 0);

#ifndef STANDARD_MODERN_C_STANDARD_ERROR
        check("a negative code reads as its magnitude",
              string_compare(strerror(-2), "No such file or directory") == 0);
        check("an unknown code says so",
              string_compare(strerror(4095), "Unknown error") == 0);
        check("the minimum signed error code stays outside the table",
              string_compare(strerror(b32_min), "Unknown error") == 0);
        check("standalone formatting keeps its limited diagnostic set",
              string_compare(strerror(6), "Unknown error") == 0 &&
              string_compare(strerror(-6), "Unknown error") == 0 &&
              string_compare(strerror(133), "Unknown error") == 0);
#endif
}

/*
        The stream entries, through a pipe on the standard output.

        A pipe is the only destination available to a freestanding test that
        can be read back, and its buffer is far larger than anything written
        here. The descriptor is put back before anything else prints.

        With the stream family present the writes are buffered, so they are
        flushed before the descriptor goes back -- which is also the only line
        in this file that knows the stream family exists.
*/
static fn streams(void)
{
        b32 ends[2];
        bipolar saved;
        bipolar read_back;

        if (system_call_2(syscall(pipe2), (positive)ends, 0) != 0)
        {
                check("a pipe for the stream entries", false);
                return;
        }

        saved = (bipolar)system_call_1(syscall(dup), 1);

        if (saved < 0 ||
            (bipolar)system_call_3(syscall(dup3), (positive)ends[1], 1, 0) != 1)
        {
                check("standard output redirected", false);
                return;
        }

        printf((string_address) "printf %d %s %.2f %%\n", 1,
               (string_address) "one", 1.5);
        fprintf(format_output, (string_address) "fprintf %o %X\n", 8u, 255u);
        puts((string_address) "puts adds one");
        fputs((string_address) "fputs does not", format_output);
        putchar('\n');
        fputc('z', format_output);
        fputc('\n', format_output);

#ifdef STANDARD_MODERN_C_STANDARD_STREAM
        fflush(format_output);
#endif

        system_call_3(syscall(dup3), (positive)saved, 1, 0);
        system_call_1(syscall(close), (positive)saved);
        system_call_1(syscall(close), (positive)ends[1]);

        read_back = (bipolar)system_call_3(syscall(read), (positive)ends[0],
                                           (positive)address_of room,
                                           sizeof(room) - 1);

        system_call_1(syscall(close), (positive)ends[0]);

        if (read_back < 0)
                read_back = 0;

        room[read_back] = end;

        check("the stream entries wrote what they say they wrote",
              string_compare(room,
                             "printf 1 one 1.50 %\n"
                             "fprintf 10 FF\n"
                             "puts adds one\n"
                             "fputs does not\n"
                             "z\n") == 0);

        //      stderr is unbuffered, so a closed descriptor forces the
        //      formatter's write path to observe the failure immediately.
        saved = (bipolar)system_call_1(syscall(dup), 2);

        if (saved < 0)
        {
                check("standard error saved for a failed write", false);
        }
        else
        {
                bipolar answered;

                system_call_1(syscall(close), 2);
                answered = fprintf(format_error,
                                   (string_address) "this cannot be written");
                system_call_3(syscall(dup3), (positive)saved, 2, 0);
                system_call_1(syscall(close), (positive)saved);

#ifdef STANDARD_MODERN_C_STANDARD_STREAM
                clearerr(format_error);
#endif

                check("fprintf reports an immediate stream failure",
                      answered == -1);
        }
}

static fn shared_sticky_tails(void)
{
        p8 bytes[128];
        check("zero-bound known byte span accepts null", !memory_span_byte(null, '0', 0));
        for (positive size = 0; size <= sizeof(bytes); size++)
                for (positive at = 0; at <= size; at++)
                {
                        memory_fill(bytes, '0', size);
                        if (at < size) bytes[at] = '1';
                        check("known byte span exact mismatch", memory_span_byte(bytes, '0', size) == at);
                        memory_fill(bytes, 0, size);
                        if (at < size) bytes[at] = 1;
                        check("known zero span exact mismatch", memory_span_byte(bytes, 0, size) == at);
                }
        for (positive keep = 1; keep <= 20; keep++)
                for (positive tail = 0; tail <= 32; tail++)
                        for (positive sticky = 0; sticky <= tail; sticky++)
                                for (positive odd = 0; odd < 2; odd++)
                                {
                                        format_number number = {0};
                                        number.count = keep + 1 + tail;
                                        number.exponent = 1;
                                        memory_fill(number.digit, '2', keep);
                                        number.digit[keep - 1] += odd;
                                        number.digit[keep] = '5';
                                        memory_fill(number.digit + keep + 1, '0', tail);
                                        if (sticky < tail) number.digit[keep + 1 + sticky] = '1';
                                        format_round(&number, keep);
                                        check("decimal tie uses every bounded sticky position",
                                              number.count == keep && number.exponent == 1 &&
                                              number.digit[keep - 1] == '2' + odd +
                                                  (sticky < tail || odd));
                                }
}

static fn lexical_fields(void)
{
        string_address at = null;
        conversion_spec got = conversion_spec_take_max(&at, 0);
        check("empty counted format does not access its cursor", !at && !got.flags &&
            got.fields == 1 && !got.stars && !got.overflow && !got.field[0]);
        for (positive byte = 0; byte < 256; byte++)
        {
                p8 text[] = {(p8)byte, '9', '.', '*', 'x'};
                positive flags = byte == '-' ? CONVERSION_FLAG_LEFT :
                    byte == '+' ? CONVERSION_FLAG_PLUS : byte == ' ' ? CONVERSION_FLAG_SPACE :
                    byte == '#' ? CONVERSION_FLAG_ALTERNATE : byte == '0' ? CONVERSION_FLAG_ZERO : 0;
                p8 digit = byte >= '1' && byte <= '9';
                at = text;
                got = conversion_spec_take_max(&at, 1);
                check("bounded field grammar classifies every byte", got.flags == flags &&
                    got.fields == 1 + (byte == '.') && got.stars == (byte == '*') &&
                    !got.overflow && got.field[0] == (digit ? byte - '0' : 0) &&
                    !got.field[1] && at == text + (flags || digit || byte == '.' || byte == '*'));
        }
        static const struct {
                string_address text; p32 width, precision; p8 fields, stars, overflow;
        } cases[] = {
            {"12",12,0,1,0,0}, {"12.",12,0,2,0,0}, {"12.*",12,0,2,2,0},
            {"*",0,0,1,1,0}, {"*.*",0,0,2,3,0}, {"*.003",0,3,2,1,0},
            //      The fields are thirty two bits, so these wrap at four
            //      billion and not at eighteen quintillion. Both are past
            //      what any consumer accepts and every one of them tests the
            //      overflow bit; the record is this width so that it lands in
            //      registers rather than on the stack. See lib.util.c.
            {"4294967295",4294967295u,0,1,0,0},
            {"4294967296.4294967297",0,1,2,0,3},
            {"*.00000000000000000000000003",0,3,2,1,0}
        };
        for (positive i = 0; i < array_count(cases); i++)
        {
                at = cases[i].text;
                positive length = string_length(at);
                got = conversion_spec_take_max(&at, length);
                check("field values retain star, dot and overflow policy", !*at &&
                    got.field[0] == cases[i].width && got.field[1] == cases[i].precision &&
                    got.fields == cases[i].fields && got.stars == cases[i].stars &&
                    got.overflow == cases[i].overflow);
        }
}

/*
        The estimate tables in src/lib.util.c, multiplied back out.

        Each entry for 5^(27j) has to be the top 128 bits of that power with
        the exponent that puts them back, and each entry c for 5^-(27j) with
        exponent -K has to be the floor of 2^K over the power, which is
        c * 5^(27j) <= 2^K < (c + 1) * 5^(27j). Nothing but multiplication.
*/
static positive table_limbs[16];
static positive table_count;

static fn table_power(positive power)
{
        table_limbs[0] = 1;
        table_count = 1;

        while (power)
        {
                positive step = power < 27 ? power : 27;
                positive factor = 1;
                positive carry = 0;
                positive i;

                for (i = 0; i < step; i++)
                        factor *= 5;

                for (i = 0; i < table_count; i++)
                {
                        p128 product = (p128)table_limbs[i] * factor + carry;

                        table_limbs[i] = (positive)product;
                        carry = (positive)(product >> 64);
                }

                if (carry)
                        table_limbs[table_count++] = carry;

                power -= step;
        }
}

static positive table_bits(positive address_to limbs, positive count)
{
        positive bits = 0;
        positive i;

        for (i = 0; i < count * 64; i++)
                if ((limbs[i / 64] >> (i % 64)) & 1)
                        bits = i + 1;

        return bits;
}

//      Sixty four bits of the limbs from bit from up, zeros past either end.
static positive table_window(positive address_to limbs, positive count,
                             bipolar from)
{
        positive word = 0;
        bipolar bit;

        for (bit = 63; bit >= 0; bit--)
        {
                bipolar at = from + bit;
                positive value = 0;

                if (at >= 0 && (positive)at < count * 64)
                        value = (limbs[at / 64] >> (at % 64)) & 1;

                word = (word << 1) | value;
        }

        return word;
}

//      The limbs against 2^bit: below, equal or above.
static b32 table_against(positive address_to limbs, positive count,
                         positive bit)
{
        positive bits = table_bits(limbs, count);
        positive i;

        if (bits != bit + 1)
                return bits > bit + 1 ? 1 : -1;

        for (i = 0; i < bit; i++)
                if ((limbs[i / 64] >> (i % 64)) & 1)
                        return 1;

        return 0;
}

static fn estimate_tables(void)
{
        positive j;

        for (j = 0; j < array_count(format_five_high); j++)
        {
                positive bits;

                table_power(27 * j);
                bits = table_bits(table_limbs, table_count);

                check("an estimate table entry is its power of five, rounded down",
                      format_five_exponent[j] == (bipolar)bits - 128 &&
                          format_five_high[j] ==
                              table_window(table_limbs, table_count,
                                           (bipolar)bits - 64) &&
                          format_five_low[j] ==
                              table_window(table_limbs, table_count,
                                           (bipolar)bits - 128));
        }

        for (j = 0; j < array_count(format_fifth_high); j++)
        {
                positive factor[2] = {format_fifth_low[j], format_fifth_high[j]};
                positive product[20] = {0};
                positive carry = 0;
                positive count;
                positive bit = (positive)-format_fifth_exponent[j];
                positive i;
                positive k;

                table_power(27 * j);
                count = table_count + 2;

                for (k = 0; k < 2; k++)
                {
                        carry = 0;

                        for (i = 0; i < table_count; i++)
                        {
                                p128 sum = (p128)table_limbs[i] * factor[k] +
                                           product[i + k] + carry;

                                product[i + k] = (positive)sum;
                                carry = (positive)(sum >> 64);
                        }

                        product[table_count + k] += carry;
                }

                check("a reciprocal table entry times its power is at most the power of two",
                      (factor[1] >> 63) && table_against(product, count, bit) <= 0);

                carry = 0;

                for (i = 0; i < count; i++)
                {
                        p128 sum = (p128)product[i] +
                                   (i < table_count ? table_limbs[i] : 0) + carry;

                        product[i] = (positive)sum;
                        carry = (positive)(sum >> 64);
                }

                check("a reciprocal table entry plus one times its power is past the power of two",
                      !carry && table_against(product, count, bit) > 0);
        }
}

b32 main(void)
{
        lexical_fields();
        shared_sticky_tails();
        estimate_tables();
        shapes();
        bounded();
        entries();
        reasons();
        streams();

        return test_report(null);
}
#endif /* CHECK_format */

#ifdef CHECK_format_differential
/*
        Experimental C standard library

        printf's decimal floats, generated, over this tree's own

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        The digests go through log, lib.c's own buffered writer on
        descriptor one, rather than through the stdio family whose formatter
        is the thing being measured.
*/
#include "../../src/lib.util.c"
#if defined(FORMAT_STANDALONE) && \
    (defined(__SSP__) || defined(__SSP_ALL__) || defined(__SSP_STRONG__) || \
     defined(__SSP_EXPLICIT__))
//      The stack protector's guard and its failure live with stdlib, which
//      this arrangement leaves out; a build with the protector on (MW_HARDEN=1)
//      would not link without them.
KEEP unsigned long __stack_chk_guard = 0x2f5a8c1d93e6b700ul;

KEEP DEAD_END fn __stack_chk_fail(void)
{
        system_call_1(syscall(exit_group), 134);
        __builtin_unreachable();
}
#endif

static void fd_text(const char *text)
{
        string_format(log, "%s", (string_address)text);
}

#define SHARED_format_differential_body
#include "../checks.c"
#undef SHARED_format_differential_body

b32 main(void)
{
        positive arguments = program_argument_count();

        format_differential_main(
            arguments > 1 ? (const char *)program_argument(1) : "0",
            arguments > 2 ? (const char *)program_argument(2) : "",
            arguments > 3 ? (const char *)program_argument(3) : "",
            arguments > 4 ? (const char *)program_argument(4) : "");
        log_flush();
        return 0;
}
#endif /* CHECK_format_differential */

#ifdef CHECK_format_differential_reference
/*
        Experimental C standard library

        the same generated floats, over the machine's glibc

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        Built by the host compiler in the ordinary way, links glibc, and is
        the answer key for CHECK_format_differential.
*/
#include <stdio.h>

static void fd_text(const char *text)
{
        fputs(text, stdout);
}

#define FD_REFERENCE
#define SHARED_format_differential_body
#include "../checks.c"
#undef SHARED_format_differential_body

int main(int argc, char **argv)
{
        format_differential_main(argc > 1 ? argv[1] : "0",
                                 argc > 2 ? argv[2] : "",
                                 argc > 3 ? argv[3] : "",
                                 argc > 4 ? argv[4] : "");
        fflush(stdout);
        return 0;
}
#endif /* CHECK_format_differential_reference */

#ifdef CHECK_scan_differential
/*
        scanf's grid over this tree's own, digests through lib.c's log.
*/
#include "../../src/lib.util.c"

static void sd_text(const char *text)
{
        string_format(log, "%s", (string_address)text);
}

#define SHARED_scan_differential_body
#include "../checks.c"
#undef SHARED_scan_differential_body

b32 main(void)
{
        positive arguments = program_argument_count();

        scan_differential_main(
            arguments > 1 ? (const char *)program_argument(1) : "",
            arguments > 2 ? (const char *)program_argument(2) : "",
            arguments > 3 ? (const char *)program_argument(3) : "");
        log_flush();
        return 0;
}
#endif /* CHECK_scan_differential */

#ifdef CHECK_scan_differential_reference
/*
        The same grid over the machine's glibc, built by the host compiler:
        the answer key for CHECK_scan_differential.
*/
#include <stdio.h>

static void sd_text(const char *text)
{
        fputs(text, stdout);
}

#define SHARED_scan_differential_body
#include "../checks.c"
#undef SHARED_scan_differential_body

int main(int argc, char **argv)
{
        scan_differential_main(argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "",
                               argc > 3 ? argv[3] : "");
        fflush(stdout);
        return 0;
}
#endif /* CHECK_scan_differential_reference */

#ifdef CHECK_scan
#include "../../src/lib.util.c"
/*
        Experimental C standard library

        scanf, what a comparison with glibc cannot state

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        Every single-conversion answer -- the value stored, how many bytes of
        the object moved, where %n landed and what came back -- is compared
        with the machine's glibc by CHECK_scan_differential, a grid of some
        eighty thousand formats and inputs walked on all three machines. It
        holds every case this section used to pin from a glibc 2.44 run,
        the length modifiers' narrowing and %p's (nil) among them.

        What stays here is what that grid cannot say: %Lf, whose object
        differs between x86_64's eighty bits and the others' binary128,
        several directives in one format, the stream entries with their
        position and indicators, errno, and the v-forms. Two notes from the
        run the pins came from still hold: the riscv64 cross toolchain's
        glibc 2.41 consumes incomplete prefixes -- "0x", "1e", "0x1p" --
        where 2.44 and the standard call them a matching failure, and this
        follows 2.44; and without numbers.c, "%f" is a double narrowed rather
        than a float parsed and "%Lf" a double widened.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static b32 used;

/*
        %Lf, which is the one conversion whose object this tree has no
        arithmetic for. The value is compared against a compile time constant
        of the same type, byte for byte, because comparing two long doubles
        with == is a call into libgcc on two of the three machines and there
        is none to call. Ten bytes on x86_64, where the format is eighty bits
        inside a sixteen byte object, and the whole object elsewhere.
*/
static fn wide_decimal(void)
{
#if __LDBL_MANT_DIG__ == 64
#define WIDE_DECIMAL_BYTES 10
#else
#define WIDE_DECIMAL_BYTES sizeof(f128)
#endif

        f128 got;
        f128 want;

        want = 2.5L;
        memory_fill(address_of got, 0xA5, sizeof(got));
        check("%Lf stores a long double",
              sscanf((string_address) "2.5", (string_address) "%Lf",
                     address_of got) == 1 &&
                  memory_compare(address_of got, address_of want,
                                 WIDE_DECIMAL_BYTES) == 0);

        want = 0.0L;
        memory_fill(address_of got, 0xA5, sizeof(got));
        check("%Lf stores a zero",
              sscanf((string_address) "0", (string_address) "%Lf",
                     address_of got) == 1 &&
                  memory_compare(address_of got, address_of want,
                                 WIDE_DECIMAL_BYTES) == 0);

        want = -1024.0L;
        memory_fill(address_of got, 0xA5, sizeof(got));
        check("%Lf stores a negative",
              sscanf((string_address) "-1024", (string_address) "%Lf",
                     address_of got) == 1 &&
                  memory_compare(address_of got, address_of want,
                                 WIDE_DECIMAL_BYTES) == 0);

        want = 0.0625L;
        memory_fill(address_of got, 0xA5, sizeof(got));
        check("%Lf stores a fraction that is exact in both formats",
              sscanf((string_address) "0x1p-4", (string_address) "%Lf",
                     address_of got) == 1 &&
                  memory_compare(address_of got, address_of want,
                                 WIDE_DECIMAL_BYTES) == 0);

        memory_fill(address_of got, 0xA5, sizeof(got));
        check("%Lf leaves the object alone on a matching failure",
              sscanf((string_address) "abc", (string_address) "%Lf",
                     address_of got) == 0 &&
                  ((p8 address_to)address_of got)[0] == 0xA5);
}


/*
        Several directives in one format, which is where the answer stops
        being a boolean. The count is the number ASSIGNED, so a suppressed
        conversion that succeeded is not in it and a %n never is, and a
        failure part way through answers with what had already landed rather
        than with a failure.
*/
static fn several(void)
{
        b32 first = 0;
        b32 second = 0;
        p8 word[16];

        check("two conversions, both assigned",
              sscanf((string_address) "12 34", (string_address) "%d %d",
                     address_of first, address_of second) == 2 &&
                  first == 12 && second == 34);

        check("a separator between them",
              sscanf((string_address) "12,34", (string_address) "%d,%d",
                     address_of first, address_of second) == 2 &&
                  first == 12 && second == 34);

        check("white space in the format matches a run",
              sscanf((string_address) "12   \t\n 34", (string_address) "%d %d",
                     address_of first, address_of second) == 2 &&
                  first == 12 && second == 34);

        check("white space in the format matches nothing at all",
              sscanf((string_address) "12abc", (string_address) "%d %s",
                     address_of first, word) == 2 &&
                  first == 12 && string_compare(word, "abc") == 0);

        first = 0;
        second = 7;
        check("the second failing answers with the first",
              sscanf((string_address) "12 abc", (string_address) "%d %d",
                     address_of first, address_of second) == 1 &&
                  first == 12 && second == 7);

        first = 0;
        used = -1;
        check("%n between two conversions reports the first",
              sscanf((string_address) "12", (string_address) "%d%n%d",
                     address_of first, address_of used,
                     address_of second) == 1 &&
                  first == 12 && used == 2);

        first = 0;
        second = 0;
        check("a suppressed conversion is not counted and is consumed",
              sscanf((string_address) "1 2 3", (string_address) "%d%*d%d",
                     address_of first, address_of second) == 2 &&
                  first == 1 && second == 3);

        used = -1;
        check("a suppressed conversion still moves %n",
              sscanf((string_address) "12", (string_address) "%*d%n",
                     address_of used) == 0 && used == 2);

        used = -1;
        check("a literal that matched moves %n and assigns nothing",
              sscanf((string_address) "ab", (string_address) "a%n",
                     address_of used) == 0 && used == 1);

        used = -1;
        check("a literal mismatch keeps its matched prefix consumed",
              sscanf((string_address) "abX", (string_address) "ab%nY",
                     address_of used) == 0 && used == 2);

        used = -1;
        check("%n alone on an empty input is zero and not the end",
              sscanf((string_address) "", (string_address) "%n",
                     address_of used) == 0 && used == 0);

        first = 12;
        check("a suppressed %n writes nothing",
              sscanf((string_address) "ab", (string_address) "%*n") == 0 &&
                  first == 12);

        memory_fill(word, 0x7F, sizeof(word));
        check("a set stops where the set says and terminates",
              sscanf((string_address) "abc123", (string_address) "%[a-z]",
                     word) == 1 &&
                  string_compare(word, "abc") == 0 && word[4] == 0x7F);
}

/*
        THE STREAM HALF

        The same engine over a FILE, which adds three answers a string cannot
        have: where the stream was left, whether the end was reached, and
        whether the byte that stopped a conversion went back.

        The last of those is the whole reason ungetc exists and is the thing
        that has to be true for a program to read two numbers out of one file:
        the byte that ended the first conversion is the byte the second one
        starts at. It is checked here by asking the stream for it directly
        after the scan rather than by scanning again, so that a failure says
        which of the two halves was wrong.
*/
//      One file for each run: the name used to be the same for everybody, so
//      a second run at once, or a file left behind with another mode, stopped
//      every row that reads one back.
static p8 scan_test_path[48];

static string_address scan_test_file(void)
{
        if (scan_test_path[0] == 0)
        {
                positive length = 23;

                memory_copy(scan_test_path, "/tmp/dawning-scan-test.", length);
                length += positive_into(scan_test_path + length,
                                        (positive)getpid());
                scan_test_path[length] = 0;
        }

        return (string_address)scan_test_path;
}

static fn lay_down(string_address text)
{
        FILE address_to handle = fopen(scan_test_file(), (string_address) "w");
        positive length = string_length(text);

        if (handle == null)
                return;

        if (length != 0)
                fwrite((address_any)text, 1, length, handle);

        fclose(handle);
}

static FILE address_to pick_up(void)
{
        return fopen(scan_test_file(), (string_address) "r");
}

//      vfscanf reached the way a caller reaches it. The list cannot be
//      written down as a null: on arm64 var_args is a structure and not a
//      pointer, so the only way to hand one over is to have been passed one.
static b32 through_stream(FILE address_to handle, string_address format, ...)
{
        var_args list;
        b32 answer;

        var_list(list, format);
        answer = vfscanf(handle, format, list);
        var_list_end(list);

        return answer;
}

static fn streams(void)
{
        FILE address_to handle;
        b32 first = 0;
        b32 second = 0;
        p8 word[16];
        decimal number = 0;

        lay_down((string_address) "12 abc 3.5\n");
        handle = pick_up();

        if (handle == null)
        {
                check("the scratch file could be written and reopened", false);
                return;
            }

        check("fscanf reads a number and stops on the space",
              fscanf(handle, (string_address) "%d", address_of first) == 1 &&
                  first == 12 && ftell(handle) == 2 && !feof(handle));

        check("the byte that stopped it is the next byte out",
              stream_get_byte(handle) == ' ' &&
                  stream_unget_byte(' ', handle) == ' ');

        check("a matching failure leaves the position past the white space",
              fscanf(handle, (string_address) "%d", address_of second) == 0 &&
                  ftell(handle) == 3 && !feof(handle));

        check("%s reads the word and pushes back the space after it",
              fscanf(handle, (string_address) "%s", word) == 1 &&
                  string_compare(word, "abc") == 0 && ftell(handle) == 6);

        check("%lf reads the number",
              fscanf(handle, (string_address) "%lf", address_of number) == 1 &&
                  number == 3.5 && ftell(handle) == 10);

        check("the end of the file is an input failure and sets feof",
              fscanf(handle, (string_address) "%d", address_of first) == EOF &&
                  feof(handle));

        fclose(handle);

        lay_down((string_address) "abc");
        handle = pick_up();

        check("a matching failure at the front consumes nothing",
              fscanf(handle, (string_address) "%d", address_of first) == 0 &&
                  ftell(handle) == 0 && !feof(handle));

        check("and the bytes are all still there",
              fscanf(handle, (string_address) "%s", word) == 1 &&
                  string_compare(word, "abc") == 0 && ftell(handle) == 3 &&
                  feof(handle));

        fclose(handle);

        //      A prefix that ran off the end of the file. Characters WERE
        //      read, so this is a matching failure and not an input one, and
        //      the answer is zero rather than EOF even though the file ended.
        lay_down((string_address) "1e+");
        handle = pick_up();

        check("a prefix that the file ended in is a matching failure",
              fscanf(handle, (string_address) "%f", address_of first) == 0 &&
                  ftell(handle) == 3 && feof(handle));

        fclose(handle);

        //      A trailing white space directive reads until it finds a byte
        //      that is not white, which at the end of a file means reading
        //      the end. That is the one directive that can set feof on a
        //      call that succeeded.
        lay_down((string_address) "5");
        handle = pick_up();

        check("a trailing space directive reads to the end and still answers",
              fscanf(handle, (string_address) "%d ", address_of first) == 1 &&
                  first == 5 && feof(handle));

        fclose(handle);

        //      A width that ended exactly at the end of the file. glibc looks
        //      at the byte after the last digit whatever the width said, so
        //      the indicator is set here and the position is not moved.
        lay_down((string_address) "007");
        handle = pick_up();

        check("a width that ends at the end of the file sets feof",
              fscanf(handle, (string_address) "%3d", address_of first) == 1 &&
                  first == 7 && ftell(handle) == 3 && feof(handle));

        fclose(handle);

        //      Except when the width ran out on the "x" of a "0x", which is
        //      the one place glibc stops without looking.
        lay_down((string_address) "0x");
        handle = pick_up();

        check("a width that ran out on the base letter does not",
              fscanf(handle, (string_address) "%2x", address_of first) == 0 &&
                  ftell(handle) == 2 && !feof(handle));

        fclose(handle);

        lay_down((string_address) "42");
        handle = pick_up();

        check("vfscanf is the same engine",
              through_stream(handle, (string_address) "%d",
                             address_of first) == 1 &&
                  first == 42);

        fclose(handle);

        check("a null handle answers the end",
              through_stream(null, (string_address) "%d",
                             address_of first) == EOF);

        unlink(scan_test_file());
}

/*
        WHAT ERRNO SAYS AFTERWARDS

        glibc puts the caller's errno aside, sets it to zero, and puts it back
        on the way out unless a conversion set one of its own -- except on a
        call that ended because the input ran out, where it puts nothing back
        and throws away whatever was set. So the ERANGE from an overflowing
        conversion survives a call that finished and does not survive one that
        then ran off the end, which is a difference a program reading errno
        after a scanf can see and could not have predicted.
*/
static fn reasons(void)
{
#ifdef STANDARD_MODERN_C_STANDARD_ERROR
        bipolar wide = 0;
        b32 whole = 0;
        p8 word[16];

        errno = 0;
        check("an overflow on a call that finished reports ERANGE",
              sscanf((string_address) "99999999999999999999",
                     (string_address) "%ld", address_of wide) == 1 &&
                  errno == ERANGE);

        errno = 0;
        check("and does not, once the input ran out afterwards",
              sscanf((string_address) "99999999999999999999",
                     (string_address) "%ld%s", address_of wide, word) == 1 &&
                  errno == 0);

        errno = 0;
        check("a byte after the number puts the ERANGE back",
              sscanf((string_address) "99999999999999999999 z",
                     (string_address) "%ld%s", address_of wide, word) == 2 &&
                  errno == ERANGE);

        errno = 7;
        check("a clean call leaves the caller's own errno alone",
              sscanf((string_address) "12", (string_address) "%d",
                     address_of whole) == 1 &&
                  errno == 7);

        errno = 7;
        check("so does a matching failure",
              sscanf((string_address) "ab", (string_address) "%d",
                     address_of whole) == 0 &&
                  errno == 7);

        errno = 7;
        check("an input failure does not",
              sscanf((string_address) "", (string_address) "%d",
                     address_of whole) == EOF &&
                  errno == 0);

        errno = 0;
        check("a trailing space directive that ran out clears it too",
              sscanf((string_address) "99999999999999999999",
                     (string_address) "%ld ", address_of wide) == 1 &&
                  errno == 0);

        errno = 0;
        check("and one that found a space first does not",
              sscanf((string_address) "99999999999999999999 ",
                     (string_address) "%ld ", address_of wide) == 1 &&
                  errno == ERANGE);

        errno = 0;
#endif
}

//      The var_args entries, reached the way a caller reaches them.
static b32 through_list(string_address input, string_address format, ...)
{
        var_args list;
        b32 answer;

        var_list(list, format);
        answer = vsscanf(input, format, list);
        var_list_end(list);

        return answer;
}

static fn entries(void)
{
        b32 first = 0;
        b32 second = 0;

        check("vsscanf carries the list",
              through_list((string_address) "7 8", (string_address) "%d %d",
                           address_of first, address_of second) == 2 &&
                  first == 7 && second == 8);

        check("a null format answers the end",
              sscanf((string_address) "12", null) == EOF);

        check("a null input is an empty one",
              sscanf(null, (string_address) "%d", address_of first) == EOF);
}

static fn scan_set_ranges(void)
{
        static const p8 edges[] = {1, 9, 31, 32, 33, 45, 48, 57, 65, 90,
                                  97, 122, 126, 127, 128, 254, 255};
        for (positive first = 0; first < sizeof edges; first++)
                for (positive last = 0; last < sizeof edges; last++)
                        for (positive inverse = 0; inverse < 2; inverse++)
                        {
                                p8 low = edges[first], high = edges[last];
                                p8 format[] = {'^', low, '-', high, ']', 'X', 0};
                                b8 table[258];
                                p8 negated = !inverse, intact = true;
                                memory_fill(table, 0x5a, sizeof table);
                                string_address after = scan_build_set(
                                        format + !inverse, table + 1, &negated);
                                for (positive byte = 0; byte < 256; byte++)
                                {
                                        p8 member = low <= high
                                                ? byte >= low && byte <= high
                                                : byte == low || byte == high || byte == '-';
                                        intact &= table[byte + 1] == (member != inverse);
                                }
                                check("scanset range bytes and negation",
                                      intact && negated == inverse && after == format + 5);
                                check("scanset table bounds", table[0] == 0x5a && table[257] == 0x5a);
                        }
}

b32 main(void)
{
        scan_set_ranges();
        wide_decimal();
        several();
        streams();
        reasons();
        entries();

        return test_report(null);
}
#endif /* CHECK_scan */

#ifdef CHECK_stream
/*
        Experimental C standard library

        the FILE trace, over this tree's own streams

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        The trace goes through log, which is the library's own buffered writer
        on descriptor one and has nothing to do with the FILE streams being
        measured. A trace printed through the thing it is measuring cannot
        report on it: a stream that flushed at the wrong moment would move the
        evidence rather than show it.
*/
#include "../../src/lib.util.c"

#define trace_number(label, value)                                    \
        string_format(log, "%s %b\n", (string_address)(label),        \
                      (b32)(long)(value))

#define body_limit_memory() \
        (system_call_4(syscall(prlimit64), 0, 9, \
                        (positive)(p64[]){1, 1}, 0) == 0)

/*
        What the shared walk cannot ask glibc, asked of this tree's streams
        after every step. A resident write never leaves a buffer full; staged
        output is never beside read-ahead or on an unbuffered stream; a line
        buffered stream never has a newline waiting; waiting bytes carry this
        process's stamp, which a fresh stream starts without, so a stamp
        skipped where it was due shows; and every stream on the open list is
        named by the link its back pointer says names it.
*/
static fn stream_walk_invariants(stream address_to handle)
{
        stream address_to walk;
        positive links = 0;

        if (handle->write_used != 0)
        {
                if (handle->read_head != handle->read_tail)
                        trace_number("invariant: staged beside read-ahead", 1);

                if (handle->write_used >= handle->buffer_size)
                        trace_number("invariant: buffer left full", (long)handle->write_used);

                if (handle->flags & STREAM_UNBUFFERED)
                        trace_number("invariant: unbuffered stream staged", (long)handle->write_used);

                if ((handle->flags & STREAM_LINE_BUFFERED) &&
                    memory_first_of(handle->buffer, '\n', handle->write_used))
                        trace_number("invariant: newline waiting", (long)handle->write_used);

                if (handle->owner != (positive)system_call(syscall(getpid)))
                        trace_number("invariant: waiting bytes not stamped", (long)handle->write_used);
        }

        for (walk = stream_open_list; walk != null && links < 100000; walk = walk->next)
        {
                if (walk->back == null || address_to walk->back != walk ||
                    !(walk->flags & STREAM_REGISTERED))
                        trace_number("invariant: open list link", (long)links);

                links++;
        }
}

#define body_stream_invariants(f) stream_walk_invariants(f)
#define SHARED_stream_body
#include "../checks.c"
#undef SHARED_stream_body

/*
        A write straight after ungetc, which C leaves undefined, answered the
        way the general path has always answered it: the pushed-back byte is
        given up and its distance is given back to the kernel's offset. After
        reading, that puts the write where the caller was. After writing there
        is no read-ahead and the output is still staged, so the staged bytes
        land one place early -- "abcdeghi" rather than anything a caller would
        choose -- and that is pinned as it stands, because the point here is
        not that answer but that the resident put paths, which skip
        stream_face_writing, still refuse the one state it exists for. The
        flush comes straight after the write so that no later general call
        can give the byte back on the resident path's behalf.
*/
static p8 write_after_pushback_through(b32 entry)
{
        b32 descriptor = (b32)system_call_2(syscall(memfd_create),
                                            (positive)"pushback-write", 1);
        p8 seen[16];
        p8 okay;

        if (descriptor < 0) return false;
        FILE *file = fdopen(descriptor, "w+");
        if (!file) { close(descriptor); return false; }

        okay = fputs("abcdef", file) >= 0 && fseek(file, 0, SEEK_SET) == 0 &&
               fgetc(file) == 'a' && fgetc(file) == 'b' &&
               ungetc('Z', file) == 'Z' && ftell(file) == 1 &&
               fputc('X', file) == 'X' && ftell(file) == 2 &&
               fputs("YY", file) >= 0 && ftell(file) == 4 &&
               fflush(file) == 0;
        okay &= pread(descriptor, seen, 16, 0) == 6 &&
                memory_compare(seen, "aXYYef", 6) == 0;

        okay &= fputs("abcdef", file) >= 0 && fseek(file, 0, SEEK_SET) == 0 &&
                fseek(file, 0, SEEK_END) == 0;
        okay &= entry == 0 ? fputc('g', file) == 'g' && fputc('h', file) == 'h'
                : entry == 1 ? fputs("gh", file) >= 0
                             : fprintf(file, "%s", "gh") == 2;
        okay &= ungetc('Q', file) == 'Q';
        okay &= entry == 0 ? fputc('i', file) == 'i'
                : entry == 1 ? fputs("i", file) >= 0
                             : fprintf(file, "%c", 'i') == 1;
        okay &= fflush(file) == 0;
        okay &= pread(descriptor, seen, 16, 4) == 8 &&
                memory_compare(seen, "abcdeghi", 8) == 0;

        fclose(file);
        return okay;
}

static p8 write_after_pushback(void)
{
        return write_after_pushback_through(0) &&
               write_after_pushback_through(1) &&
               write_after_pushback_through(2);
}

static p8 dynamic_buffer_fits_one_shelf(void)
{
        stream address_to handle =
                stream_open((string_address)"/dev/null", (string_address)"w");

        if (is_null(handle))
                return false;

        stream_ready(handle);
        p8 fits = handle->buffer_size == STREAM_DYNAMIC_BUFFER &&
                    memory_usable_size(handle->buffer) ==
                            STREAM_DYNAMIC_BUFFER;

        stream_close(handle);
        return fits;
}

static p8 empty_mode_stays_bounded(void)
{
        // 64 KiB is a multiple of every supported Linux base page size.
        positive page = 64 * 1024;
        p8 address_to bytes = memory(page * 2);
        if (!bytes || system_failed(bytes))
                return false;
        if (system_call_3(syscall(mprotect), (positive)(bytes + page), page, 0))
        {
                memory_free(bytes, page * 2);
                return false;
        }

        p8 address_to mode = bytes + page - 1;
        *mode = end;
        errno = 0;
        stream address_to opened = fopen("/dev/null", mode);
        p8 bounded = !opened && errno == EINVAL;
        if (opened)
                fclose(opened);
        memory_free(bytes, page * 2);
        return bounded;
}

/* The interrupted-write row's pipe: the handler empties the read end, which
   is what lets the blocked write go on, and counts what it took. */
static bipolar interrupt_drain_fd;
static positive interrupt_drained;

static fn interrupt_write(b32 number)
{
        p8 sink[4096];
        bipolar got;

        (void)number;
        while ((got = read(interrupt_drain_fd, sink, sizeof sink)) > 0)
                interrupt_drained += (positive)got;
}

/* Forty pushes, past the eight the struct holds, read back in the order a
   stack gives: the ninth push used to be refused. The reads go part by fgetc
   and part by fread, so both the byte path and the bulk path see the spill. */
static p8 pushback_spill_holds(void)
{
        b32 descriptor = (b32)system_call_2(syscall(memfd_create),
                                            (positive)"pushback-spill", 1);
        FILE *file;
        p8 pushed[40], read_back[30];
        p8 okay = descriptor >= 0;

        if (!okay)
                return false;
        file = fdopen(descriptor, "r+");
        if (!file)
        {
                close(descriptor);
                return false;
        }
        for (positive i = 0; i < array_count(pushed); i++)
        {
                pushed[i] = (p8)(i * 37 + 11);
                okay &= ungetc(pushed[i], file) == pushed[i];
        }
        for (positive i = 0; i < 10; i++)
                okay &= fgetc(file) == pushed[array_count(pushed) - 1 - i];
        okay &= fread(read_back, 1, array_count(read_back), file) ==
                array_count(read_back);
        for (positive i = 0; i < array_count(read_back); i++)
                okay &= read_back[i] == pushed[array_count(pushed) - 11 - i];
        okay &= fgetc(file) == EOF;
        fclose(file);
        return okay;
}

/* A freopen that fails leaves no byte of the old file readable: the one read
   ahead is not served, and the stream says it failed. */
static p8 failed_reopen_reads_nothing(void)
{
        FILE address_to file;
        p8 okay;

        lay_down("abc");
        file = pick_up();
        if (!file)
                return false;
        okay = fgetc(file) == 'a';
        okay &= freopen("/nonexistent/moonwater-check", "r", file) == null &&
                ferror(file);
        okay &= fgetc(file) == EOF;
        fclose(file);
        return okay;
}

static p8 bounded_line_reads(void)
{
        p8 bytes[257], copied[260];
        static const positive limits[] = {0, 1, 2, 3, 7, 16, 63, 127, 256, 257, 259};
        for (positive delimiter = 0; delimiter < 256; delimiter++)
                for (positive stop = 0; stop <= sizeof(bytes); stop++)
                        for (positive row = 0; row < array_count(limits); row++)
                        {
                                positive limit = limits[row];
                                memory_fill(bytes, (p8)(delimiter + 1), sizeof(bytes));
                                if (stop < sizeof(bytes)) bytes[stop] = (p8)delimiter;
                                memory_fill(copied, 0xa5, sizeof(copied));
                                stream handle = {.descriptor = -1, .buffer = bytes,
                                    .read_tail = sizeof(bytes)};
                                p8 found = true, ended = true;
                                positive want = stop < sizeof(bytes) ? stop + 1 : stop;
                                if (want > limit) want = limit;
                                positive got = stream_take_line(&handle, copied, limit,
                                    (b32)delimiter, &found, &ended);
                                if (got != want || handle.read_head != want || ended ||
                                    found != (stop < sizeof(bytes) && stop < limit) ||
                                    memory_compare(copied, bytes, want) || copied[want] != 0xa5)
                                        return false;
                        }
        return true;
}

static p8 checked_write_errors(void)
{
        system_write_result result = system_write_all_checked(-1, address_bad, 0);
        if (result.bytes || result.error) return false;
        result = system_write_all_checked(-1, "x", 1);
        if (result.bytes || result.error != -EBADF) return false;
        b32 sink = open("/dev/null", O_WRONLY, 0);
        if (sink < 0) return false;
        errno = EDOM;
        result = system_write_all_checked(sink, "x", 1);
        p8 correct = result.bytes == 1 && !result.error && errno == EDOM;
        FILE *closed = fdopen(sink, "w");
        if (!closed || setvbuf(closed, 0, _IONBF, 0)) return false;
        close(sink);
        errno = 0;
        correct &= fputc('x', closed) == EOF && errno == EBADF && ferror(closed);
        fclose(closed);
        sink = open("/dev/full", O_WRONLY, 0);
        if (sink < 0) return false;
        result = system_write_all_checked(sink, "x", 1);
        correct &= !result.bytes && result.error == -ENOSPC;
        close(sink);

        b32 ends[2];
        if (pipe2(ends, O_NONBLOCK)) return false;
        result = system_write_all_checked(ends[1], address_bad, 1);
        correct &= !result.bytes && result.error == -EFAULT;
        bipolar capacity = fcntl(ends[1], 1032, 0UL);
        if (capacity <= 0) return false;
        positive length = (positive)capacity + 4096;
        p8 *bytes = malloc(length);
        if (!bytes) return false;
        memory_fill(bytes, 0x5a, length);
        result = system_write_all_checked(ends[1], bytes, length);
        correct &= result.bytes == (positive)capacity && result.error == -EAGAIN;
        correct &= read(ends[0], bytes, length) == capacity;
        FILE *f = fdopen(ends[1], "w");
        if (!f || setvbuf(f, 0, _IONBF, 0)) return false;
        errno = 0;
        correct &= fwrite(bytes, 1, length, f) == (positive)capacity &&
                   errno == EAGAIN && ferror(f);
        clearerr(f);

        /* Leave the pipe full, block, and interrupt without SA_RESTART. The
           handler empties the pipe, so the blocked write goes on from what it
           moved -- nothing, when the signal came first -- and every staged
           byte completes: the bytes taken by the handler and the ones left
           are the pipe's capacity plus the length written. The periodic
           timer also prevents a lost first signal hanging the test. */
        signal_action wanted = { .handler = interrupt_write }, previous;
        if (sigaction(SIGALRM, &wanted, &previous) ||
            fcntl(ends[1], 4, 0UL)) return false;
        p64 timer[4] = {0, 20000, 0, 20000};
        if (system_call_3(syscall(setitimer), 0, (positive)timer, 0)) return false;
        interrupt_drain_fd = ends[0];
        interrupt_drained = 0;
        correct &= fwrite(bytes, 1, length, f) == length && !ferror(f);
        memory_fill(timer, 0, sizeof(timer));
        system_call_3(syscall(setitimer), 0, (positive)timer, 0);
        sigaction(SIGALRM, &previous, 0);
        clearerr(f);
        bipolar left = read(ends[0], bytes, length);
        correct &= left >= 0 &&
                   interrupt_drained + (positive)left == capacity + length &&
                   interrupt_drained >= (positive)capacity;
        for (positive i = 0; i < (positive)left; i++)
                correct &= bytes[i] == 0x5a;
        close(ends[0]);
        wanted.handler = SIG_IGN;
        if (sigaction(SIGPIPE, &wanted, &previous)) return false;
        result = system_write_all_checked(ends[1], "x", 1);
        correct &= !result.bytes && result.error == -EPIPE;
        errno = 0;
        correct &= fputc('x', f) == EOF && errno == EPIPE && ferror(f);
        sigaction(SIGPIPE, &previous, 0);
        fclose(f);
        free(bytes);
        return correct;
}

/*
        Every write from here on fails the way the lane asked: seccomp answers
        the write call with errno EXPECTED (0 is a write that returns 0), as
        strace's inject=write:error=...:when=1+ did, and with no tracer in
        the way, so the lane runs where strace is not installed. -1 when this
        kernel cannot install a filter.
*/
typedef struct
{
        p16 code;
        p8 yes;
        p8 no;
        p32 value;
} write_stop_step;

static bipolar write_stop_install(b32 expected)
{
        write_stop_step steps[] = {
            {0x20, 0, 0, 0},
            {0x15, 0, 1, (p32)syscall(write)},
            {0x06, 0, 0, 0x00050000u | (p32)expected},
            {0x06, 0, 0, 0x7fff0000u}};
        struct
        {
                p16 count;
                write_stop_step address_to steps;
        } filter = {array_count(steps), steps};

        if (system_call_5(syscall(prctl), 38, 1, 0, 0, 0) < 0)
                return -1;
        return system_call_3(syscall(seccomp), 1, 0, (positive)address_of filter);
}

static b32 injected_write_stop(b32 expected)
{
        b32 sink = open("/dev/null", O_WRONLY, 0);
        if (sink < 0) return 1;
        if (write_stop_install(expected) < 0) return 2;
        system_write_result result = system_write_all_checked(sink, "x", 1);
        p8 correct = !result.bytes && result.error == -expected &&
                       system_write_all(sink, "x", 1) == 0;
        errno = 0;
        correct &= stream_trap_write(sink, "x", 1) == 0 &&
                   errno == (expected ? expected : EIO);
        close(sink);
        b32 next = dup(1);
        close(next);
        errno = 0;
        FILE *f = fmemopen("x", 1, "r");
        correct &= !f && errno == (expected ? expected : EIO);
        if (f) fclose(f);
        sink = dup(1);
        correct &= sink == next;
        close(sink);
        return correct ? 0 : 1;
}

/*
        Read-ahead given back to a descriptor whose seek fails. The filter
        answers lseek(descriptor, ..., SEEK_CUR) with EIO for the memfd
        alone, so a pipe still answers its own ESPIPE. fflush of an input
        stream is EOF with errno EIO and the stream still reads on from where
        its caller was; a write after reading is refused rather than landing
        past the read-ahead; a pipe's ESPIPE is let go, as glibc's
        _IO_new_file_sync lets it go. stream_drop_input dropped the refusal
        with the buffer, so fflush answered 0, the next fgetc read past the
        end and the X landed after "abcdef". 2 when this kernel takes no
        filter, which the lane says and does not pass.
*/
static b32 injected_seek_stop(void)
{
        b32 descriptor = (b32)system_call_2(syscall(memfd_create),
                                            (positive)"seek-stop", 1);
        b32 ends[2];
        p8 seen[16];

        if (descriptor < 0 || pipe2(ends, 0)) return 1;
        if (system_call_4(syscall(pwrite64), (positive)descriptor, (positive)"abcdef", 6, 0) != 6 ||
            write(ends[1], "uv", 2) != 2)
                return 1;

        write_stop_step steps[] = {
            {0x20, 0, 0, 0},
            {0x15, 0, 5, (p32)syscall(lseek)},
            {0x20, 0, 0, 16},
            {0x15, 0, 3, (p32)descriptor},
            {0x20, 0, 0, 32},
            {0x15, 0, 1, 1},
            {0x06, 0, 0, 0x00050000u | 5u},
            {0x06, 0, 0, 0x7fff0000u}};
        struct
        {
                p16 count;
                write_stop_step address_to steps;
        } filter = {array_count(steps), steps};

        if (system_call_5(syscall(prctl), 38, 1, 0, 0, 0) < 0 ||
            system_call_3(syscall(seccomp), 1, 0, (positive)address_of filter) < 0)
                return 2;

        FILE *file = fdopen(descriptor, "r+");
        FILE *pipe = fdopen(ends[0], "r");
        if (!file || !pipe) return 1;

        p8 okay = fgetc(file) == 'a';
        errno = 0;
        okay &= fflush(file) == EOF && errno == EIO && ferror(file);
        clearerr(file);
        okay &= fgetc(file) == 'b';
        errno = 0;
        okay &= fputc('X', file) == EOF && errno == EIO && ferror(file);
        okay &= fflush(file) == EOF;
        okay &= pread(descriptor, seen, 16, 0) == 6 &&
                memory_compare(seen, "abcdef", 6) == 0;
        clearerr(file);
        okay &= fgetc(file) == 'c';

        okay &= fgetc(pipe) == 'u' && fflush(pipe) == 0 && !ferror(pipe);

        fclose(pipe);
        close(ends[1]);
        fclose(file);
        return okay ? 0 : 1;
}

static p8 bounded_stream_positions(void)
{
        b32 descriptor = (b32)system_call_2(syscall(memfd_create),
                                            (positive)"stream-positions", 1);
        if (descriptor < 0) return false;
        FILE *file = fdopen(descriptor, "w+");
        if (!file) { close(descriptor); return false; }
        p8 okay = fputs("ab", file) >= 0 && fseek(file, 0, SEEK_SET) == 0 &&
                    fgetc(file) == 'a';
        errno = 0;
        okay &= fseek(file, bipolar_min, SEEK_CUR) == -1 && errno == EINVAL;
        okay &= ftell(file) == 1 && fgetc(file) == 'b';
        okay &= fseek(file, bipolar_max, SEEK_SET) == 0 && fputc('z', file) == 'z';
        errno = 0;
        okay &= ftell(file) == -1 && errno == EOVERFLOW;
        fclose(file);
        return okay;
}

b32 main(void)
{
        if (program_argument_count() > 1 &&
            string_equals(program_argument(1), "--seek-stop"))
                return injected_seek_stop();
        if (program_argument_count() > 2)
                return injected_write_stop((b32)string_to_positive(program_argument(2)));
        if (program_argument_count() > 1)
                return body_allocation_failure();
        if (!empty_mode_stays_bounded() || !checked_write_errors() ||
            !bounded_line_reads() || !bounded_stream_positions() ||
            !write_after_pushback() || !pushback_spill_holds() ||
            !failed_reopen_reads_nothing())
                return 1;
        trace_body();
        p8 fits = dynamic_buffer_fits_one_shelf();

        log_flush();
        return fits ? 0 : 1;
}
#endif /* CHECK_stream */

#ifdef CHECK_stream_reference
/*
        Experimental C standard library

        the same FILE trace, over the machine's glibc

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        This one is built by the host compiler in the ordinary way, links
        glibc, and is the answer key. It is the only file in the tree that
        includes a system header on purpose.
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>

#define trace_number(label, value) \
        printf("%s %ld\n", (const char *)(label), (long)(value))

#define body_limit_memory() (setrlimit(RLIMIT_AS, &(struct rlimit){1, 1}) == 0)
#define SHARED_stream_body
#include "../checks.c"
#undef SHARED_stream_body

int main(int argc, char **argv)
{
        if (argc > 1)
                return body_allocation_failure();
        trace_body();
        fflush(stdout);
        return 0;
}
#endif /* CHECK_stream_reference */

#ifdef CHECK_stream_standard
/*
        Experimental C standard library

        the standard stream trace, over this tree's own streams

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        The trace cannot go through log here: this test writes to standard
        output on purpose and log writes there too, so the two would
        interleave and the comparison would be measuring flush order rather
        than stream behaviour. Descriptor three is opened by the runner and
        written with the raw retrying primitive, which is outside everything
        under test.
*/
#include "../../src/lib.util.c"

static fn standard_trace_writer(address_any data, positive length)
{
        if (length == 0)
                length = string_length((string_address)data);

        system_write_all(3, data, length);
}

#define trace_number(label, value)                                     \
        string_format(standard_trace_writer, "%s %b\n",                \
                      (string_address)(label), (b32)(long)(value))

#define SHARED_stream_standard_body
#include "../checks.c"
#undef SHARED_stream_standard_body

b32 main(void)
{
        trace_body();
        return 0;
}
#endif /* CHECK_stream_standard */

#ifdef CHECK_stream_standard_reference
/*
        Experimental C standard library

        the same standard stream trace, over the machine's glibc

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void standard_trace_number(const char *label, long value)
{
        char text[160];
        int length = snprintf(text, sizeof(text), "%s %ld\n", label, value);
        ssize_t ignored = write(3, text, (size_t)length);
        (void)ignored;
}

#define trace_number(label, value) standard_trace_number((label), (long)(value))

#define SHARED_stream_standard_body
#include "../checks.c"
#undef SHARED_stream_standard_body

int main(void)
{
        trace_body();
        return 0;
}
#endif /* CHECK_stream_standard_reference */

#ifdef CHECK_stream_buffering
/*
        Experimental C standard library

        the buffering observation, over this tree's own streams

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/
#include "../../src/lib.util.c"

static fn body_pause_now(void)
{
        timespec moment;

        moment.tv_sec = 0;
        moment.tv_nsec = 700000000;
        sleep(address_of moment);
}

#define body_pause() body_pause_now()

#define SHARED_stream_buffering_body
#include "../checks.c"
#undef SHARED_stream_buffering_body

b32 main(void)
{
        trace_body();
        return 0;
}
#endif /* CHECK_stream_buffering */

#ifdef CHECK_stream_buffering_reference
/*
        Experimental C standard library

        the same buffering observation, over the machine's glibc

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/
#include <stdio.h>
#include <time.h>

static void body_pause_now(void)
{
        struct timespec moment = {0, 700000000};

        nanosleep(&moment, 0);
}

#define body_pause() body_pause_now()

#define SHARED_stream_buffering_body
#include "../checks.c"
#undef SHARED_stream_buffering_body

int main(void)
{
        trace_body();
        return 0;
}
#endif /* CHECK_stream_buffering_reference */

#ifdef CHECK_spool
/*
        Experimental C standard library

        the trace of <stdio.h>'s remainder, over this tree's own

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        The trace goes through log, which is lib.c's own buffered writer
        on descriptor one and has nothing to do with the streams and pipes
        being measured. CHECK_stream says why in more words and the
        reason is the same: a trace printed through the thing it is measuring
        moves the evidence rather than showing it. It matters more here than
        there, because half of these entries fork, and a trace sitting in a
        FILE buffer at the moment of a fork is a trace that gets printed
        twice.
*/
#include "../../src/lib.util.c"

#define trace_number(label, value)                             \
        string_format(log, "%s %b\n", (string_address)(label), \
                      (b32)(long)(value))

//      See the same function in spool_reference.c: the structure differs
//      between the two sides, the number does not.
static long body_file_mode(const char *path)
{
        error_stat info;

        if (stat((string_address)path, address_of info) < 0)
                return -1;

        return (long)(info.st_mode & 0777);
}

#define SHARED_spool_body
#include "../checks.c"
#undef SHARED_spool_body

static p8 line_buffer_fits_one_shelf(void)
{
        stream address_to handle =
                stream_open((string_address)"/dev/null", (string_address)"w");

        if (is_null(handle))
                return false;

        spool_set_line_buffered(handle);
        p8 fits = handle->buffer_size == STREAM_DYNAMIC_BUFFER &&
                    memory_usable_size(handle->buffer) ==
                            STREAM_DYNAMIC_BUFFER;

        stream_close(handle);
        return fits;
}

// A pipe may already occupy the child's destination descriptor. Its `e`
// flag belongs to the parent's end; exec must still inherit the child's end.
static p8 process_pipe_closed_standard(void)
{
        p8 okay = true;
        for (b32 reading = 0; reading < 2; reading++)
        {
                log_flush();
                b32 saved_input = dup(0), saved_output = dup(1);
                if (saved_input < 0 || saved_output < 0)
                        return false;
                close(0);
                if (reading) close(1);
                stream address_to pipe = popen(reading ? "printf ok" :
                        "read code && exit \"$code\"", reading ? "re" : "we");
                if (!pipe)
                {
                dup2(saved_input, 0);
                dup2(saved_output, 1);
                close(saved_input);
                close(saved_output);
                        return false;
                }
                okay &= (fcntl(fileno(pipe), 1, (positive)0) & 1) != 0;
                if (reading)
                        okay &= fgetc(pipe) == 'o' && fgetc(pipe) == 'k' &&
                                fgetc(pipe) == EOF;
                else
                        okay &= fputs("0\n", pipe) >= 0;
                okay &= pclose(pipe) == 0;
                dup2(saved_input, 0);
                dup2(saved_output, 1);
                close(saved_input);
                close(saved_output);
        }
        return okay;
}

b32 main(void)
{
        char bytes[] = "read only";
        errno = 0;
        stream address_to update = fmemopen(bytes, sizeof(bytes), "r+");
        if (update)
                fclose(update);
        if (update || errno != EINVAL)
                return 1;
        trace_body();
        p8 fits = line_buffer_fits_one_shelf();

        log_flush();
        return fits && process_pipe_closed_standard() ? 0 : 1;
}
#endif /* CHECK_spool */

#ifdef CHECK_spool_reference
/*
        Experimental C standard library

        the same trace of <stdio.h>'s remainder, over the machine's glibc

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        Built by the host compiler in the ordinary way, links glibc, and is
        the answer key. It and CHECK_stream_reference are the only files
        in the tree that include a system header on purpose.

        _GNU_SOURCE is what puts fread_unlocked, fputs_unlocked, setbuffer and
        tempnam into <stdio.h>; __fpurge lives in <stdio_ext.h> on its own.
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define trace_number(label, value) \
        printf("%s %ld\n", (const char *)(label), (long)(value))

/*
        The one thing the body cannot write once. Both sides have a stat, and
        both spell st_mode the same way, but the structure's tag and the
        header it comes from are different objects, so the call is made here
        and the body only ever sees a number.
*/
static long body_file_mode(const char *path)
{
        struct stat info;

        if (stat(path, &info) < 0)
                return -1;

        return (long)(info.st_mode & 0777);
}

#define SHARED_spool_body
#include "../checks.c"
#undef SHARED_spool_body

int main(void)
{
        trace_body();
        fflush(stdout);
        return 0;
}
#endif /* CHECK_spool_reference */

