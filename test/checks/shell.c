/*
        test/checks/shell.c: the shell's reuse, its path stage, the builtins' regressions, slurp, the wait retry and
        the writer's buffer.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_reuse_shell
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 reuse_output[16384];
static positive reuse_used;

static fn reuse_arguments(void)
{
        string_address words[] = {"test", "-abvalue", "--name=", "--", "-x", ""};
        argument_cursor cursor = {.argc = array_count(words), .argv = words, .at = 1};
        check("argument cursor preserves a short cluster",
              argument_next(&cursor) == 'a' && argument_next(&cursor) == 'b' &&
              string_equals(argument_value(&cursor, true), "value"));
        check("argument cursor distinguishes an explicit empty value",
              argument_next(&cursor) == ARGUMENT_LONG && cursor.name_length == 4 &&
              argument_value(&cursor, false) == words[2] + 7);
        check("argument boundary makes dash words and empty words operands",
              argument_next(&cursor) == ARGUMENT_OPERAND && cursor.word == words[4] &&
              argument_next(&cursor) == ARGUMENT_OPERAND && cursor.word == words[5] &&
              argument_next(&cursor) == ARGUMENT_END && cursor.at == array_count(words));
        string_address valued[] = {"test", "-v", "--", "-o", "operand"};
        cursor = (argument_cursor){.argc = array_count(valued), .argv = valued, .at = 1};
        check("required values consume a boundary as data",
              argument_next(&cursor) == 'v' && argument_value(&cursor, true) == valued[2] &&
              argument_next(&cursor) == 'o');
        check("optional values leave the following operand untouched",
              !argument_value(&cursor, false) && argument_next(&cursor) == ARGUMENT_OPERAND &&
              cursor.word == valued[4]);

        const argument_option names[] = {
            {"value", 'v', ARGUMENT_REQUIRED},
            {null},
        };
        string_address storage[] = {"test", "--val", "-v"}, value = "prior";
        cursor = (argument_cursor){.argc = array_count(storage), .argv = storage, .at = 1};
        check("storage long names remain exact",
              storage_argument_next(&cursor, names, &value) == ARGUMENT_UNKNOWN &&
              string_equals(value, "prior") && cursor.word == storage[1]);
        check("storage missing values retain the diagnostic letter and prior value",
              storage_argument_next(&cursor, names, &value) == ARGUMENT_MISSING &&
              string_equals(value, "prior") && cursor.letters[-1] == 'v');

        string_address address_to saved = program_argument_list();
        b32 count = program_argument_count();
        const argument_option options[] = {
            {"value", 'v', ARGUMENT_REQUIRED},
            {"namespace", 'n', ARGUMENT_LONG_OPTIONAL | ARGUMENT_STICKY},
            {"a", 0},
            {null},
        };
        string_address utility[] = {"test", "--val=", "-na", "--namespace=kept",
                                    "--namespace", "--", "-v"};
        program_arguments_use(utility, array_count(utility));
        file_taking taking = {
            .options = options,
        };
        check("utility prefixes, short clusters and sticky long values retain their policies",
              file_take(&taking) && taking.first == 6 && taking.last == 'n' &&
              (taking.flags & FILE_FLAG('a')) && (taking.bare & FILE_FLAG('n')) &&
              taking.value[file_letter_bit('v')] == utility[1] + 6 &&
              string_equals(taking.value[file_letter_bit('n')], "kept"));
        const argument_option selecting[] = {
            {"force", 'f', 0, 1}, {"interactive", 'i', 0, 3},
            {"directory", 't', ARGUMENT_REQUIRED},
            {"quiet", 'q', ARGUMENT_LONG_ONLY, 2},
            {"optional", 'o', ARGUMENT_OPTIONAL}, {null},
        };
        p8 selected[] = {'x', 'y', 0xa5};
        string_address ordered[] = {"test", "-if", "--quiet", "-tfirst",
                                    "--directory=last", "-o", "operand"};
        program_arguments_use(ordered, array_count(ordered));
        taking = (file_taking){.options = selecting, .selection = selected};
        check("overlapping selections retain their own last occurrence and bounds",
              file_take(&taking) && selected[0] == 'f' && selected[1] == 'q' &&
              selected[2] == 0xa5 && taking.first == 6);
        check("short and long aliases share duplicate tracking and last values",
              taking.repeated == FILE_FLAG('t') && taking.last == 'o' &&
              string_equals(file_option_value(&taking, 't'), "last") &&
              !file_option_value(&taking, 'o') && (taking.bare & FILE_FLAG('o')));
        string_address missing[] = {"test", "--directory"};
        cursor = (argument_cursor){.argc = array_count(missing), .argv = missing, .at = 1};
        argument_match match;
        check("missing values expose their option without applying selection effects",
              argument_option_take(&cursor, selecting, true, &match) == ARGUMENT_MISSING &&
              match.letter == 't' && !match.value && cursor.long_option);
        string_address short_only[] = {"test", "-q", "--quiet=", "--qui", "-oattached"};
        cursor = (argument_cursor){.argc = array_count(short_only), .argv = short_only, .at = 1};
        check("long-only names reject short spellings and attached flag arguments",
              argument_option_take(&cursor, selecting, true, &match) == ARGUMENT_UNKNOWN &&
              match.letter == 'q' &&
              argument_option_take(&cursor, selecting, true, &match) == ARGUMENT_UNEXPECTED);
        check("prefix lookup and attached optional values keep independent policies",
              argument_option_take(&cursor, selecting, true, &match) == 'q' &&
              match.selection == 2 &&
              argument_option_take(&cursor, selecting, true, &match) == 'o' &&
              string_equals(match.value, "attached"));
        program_arguments_use(saved, count);
}

static fn reuse_capture(address_any data, positive length)
{
        if (!length) length = string_length(data);
        if (length <= sizeof(reuse_output) - reuse_used)
                memory_copy(reuse_output + reuse_used, data, length);
        reuse_used += length;
}

static positive reuse_bulk_calls;
static fn reuse_bulk_capture(address_any data, positive length)
{
        reuse_bulk_calls++;
        reuse_capture(data, length);
}

static fn reuse_bulk_output(void)
{
        p8 expected[4096], actual[4096];
        const positive widths[] = {0, 1, 2, 15, 16, 255, 256, 257, 1024, 4096};
        p8 body[] = {'a', 0, 'z'};
        for (positive left = 0; left < 2; left++)
        for (positive pad = 0; pad < 256; pad += 17)
        for (positive length = 0; length <= sizeof(body); length++)
        for (positive i = 0; i < array_count(widths); i++)
        {
                positive width = widths[i], size = max(length, width);
                memory_fill(expected, 0xa5, sizeof(expected));
                memory_fill(expected, pad, size);
                if (length)
                        memory_copy(expected + (left ? 0 : size - length), body, length);
                memory_fill(actual, 0xa5, sizeof(actual));
                reuse_used = reuse_bulk_calls = 0;
                writer_field_bulk(reuse_bulk_capture, length ? body : null,
                                  length, width, pad, left);
                memory_copy(actual, reuse_output, reuse_used);
                positive padding = size - length;
                check("bulk fields preserve bounded bytes, alignment and NUL padding",
                      reuse_used == size && !memory_compare(actual, expected, sizeof(actual)));
                check("bulk padding bounds callback work independently of width",
                      reuse_bulk_calls == (padding + 255) / 256 + (length != 0));
        }
        for (positive room = 0; room <= 32; room++)
        for (positive prefix = 0; prefix <= room; prefix++)
        for (positive length = 0; length <= 33; length++)
        {
                p8 source[33], target[34];
                memory_fill(source, 0x37, sizeof(source));
                memory_fill(target, 0xa5, sizeof(target));
                byte_store sink = {target + 1, room, prefix};
                p8 complete = byte_store_append_span(&sink, length ? source : null, length);
                positive kept = min(length, room - prefix);
                p8 okay = complete == (kept == length) && sink.used == prefix + kept;
                for (positive at = 0; at < sizeof(target); at++)
                        okay &= target[at] == (at > prefix && at <= prefix + kept ? 0x37 : 0xa5);
                check("bounded sink preserves its prefix and both guards", okay);
                memory_fill(target, 0xa5, sizeof(target));
                sink.used = prefix;
                complete = byte_store_append_exact(&sink, length ? source : null, length);
                kept = length <= room - prefix ? length : 0;
                okay = complete == (length <= room - prefix) && sink.used == prefix + kept;
                for (positive at = 0; at < sizeof(target); at++)
                        okay &= target[at] == (at > prefix && at <= prefix + kept ? 0x37 : 0xa5);
                check("atomic sink either commits the whole span or preserves its state", okay);
        }
        byte_store invalid = {null, 1, 2};
        check("bounded sink refuses an invalid cursor without reading a source",
              !byte_store_append_span(&invalid, null, positive_max) && invalid.used == 2 &&
              !byte_store_append_exact(&invalid, null, positive_max) && invalid.used == 2);
}

static fn reuse_stable_arena(void)
{
        p8 held[160] __attribute__((aligned(32)));
        for (positive alignment = 1; alignment <= 32; alignment *= 2)
        for (positive prefix = 0; prefix <= 128; prefix += alignment)
        for (positive wanted = 0; wanted <= 129; wanted++)
        {
                memory_fill(held, 0xa5, sizeof(held));
                memory_arena arena = {held, 128, prefix};
                positive rounded = (wanted + alignment - 1) & ~(alignment - 1);
                p8 address_to answer = memory_arena_take(&arena, wanted, alignment);
                p8 fits = rounded <= 128 - prefix;
                p8 okay = fits ? answer == held + prefix && arena.used == prefix + rounded
                                 : !answer && arena.used == prefix;
                for (positive at = 0; at < sizeof(held); at++)
                        okay &= held[at] == 0xa5;
                check("stable arena checks capacity before changing its cursor", okay);
        }
        memory_arena arena = {held, 128, 16};
        check("stable arena refuses wrapping and invalid alignment",
              !memory_arena_take(&arena, positive_max, 16) && arena.used == 16 &&
              !memory_arena_take(&arena, 1, 0) && arena.used == 16 &&
              !memory_arena_take(&arena, 1, 3) && arena.used == 16);
        const positive firsts[] = {0, 1, 16, 63, 64};
        p8 input[64];
        for (positive at = 0; at < sizeof(input); at++) input[at] = (p8)at;
        for (positive first_at = 0; first_at < array_count(firsts); first_at++)
        for (positive length = 0; length <= sizeof(input); length++)
        {
                b32 pipe[2];
                if (system_call_2(syscall(pipe2), (positive)pipe, 0) < 0)
                {
                        check("arena read fixture opens its pipe", false);
                        return;
                }
                p8 sent = system_call_3(syscall(write), pipe[1],
                                           (positive)input, length) == (bipolar)length;
                system_close(pipe[1]);
                memory_fill(held, 0xa5, sizeof(held));
                arena = (memory_arena){held, 80, 16};
                positive first = firsts[first_at], used = 0;
                p8 read_failed = false;
                p8 address_to start = memory_arena_take(&arena, first, 16);
                p8 address_to answer = memory_arena_read_tail(
                    &arena, pipe[0], 16, first, 16, &used, &read_failed);
                system_close(pipe[0]);
                p8 fits = first && length < 64;
                p8 okay = sent && start == held + 16 && !read_failed &&
                    (fits ? answer == start && used == length && !answer[length] &&
                            !memory_compare(answer, input, length) &&
                            arena.used == 16 + ((length + 16) & ~(positive)15)
                          : !answer && !used && arena.used == 16);
                for (positive at = 0; at < 16; at++) okay &= held[at] == 0xa5;
                for (positive at = 80; at < sizeof(held); at++) okay &= held[at] == 0xa5;
                check("arena read grows in place, keeps a sentinel and rolls exhaustion back", okay);
        }
        arena = (memory_arena){held, 80, 16};
        positive used = 0;
        p8 read_failed = false;
        memory_arena_take(&arena, 16, 16);
        check("arena read failure retains the previous allocations",
              !memory_arena_read_tail(&arena, (positive)-1, 16, 16, 16,
                                       &used, &read_failed) &&
              read_failed && !used && arena.used == 16);

        /*
                A room that is not a multiple of the alignment: 83 bytes with
                16 taken, so 67 are left, and the block that fits is the
                aligned floor, 64. The first read size of 20 grows to 40, and
                the next step asked for 80, which was cut to the 67 left and
                rounded past the room, so the read failed with 50 bytes that
                fit.
        */
        {
                p8 roomy[160] __attribute__((aligned(32)));
                p8 tail[50];
                b32 pipe[2];
                positive length = sizeof(tail), roomy_used = 0;
                p8 roomy_failed = false;

                for (positive at = 0; at < sizeof(tail); at++)
                        tail[at] = (p8)(at + 1);
                memory_fill(roomy, 0xa5, sizeof(roomy));
                memory_arena roomy_arena = {roomy, 83, 16};
                if (system_call_2(syscall(pipe2), (positive)pipe, 0) < 0)
                {
                        check("arena fixture opens its pipe", false);
                        return;
                }
                p8 sent = system_call_3(syscall(write), pipe[1],
                                           (positive)tail, length) == (bipolar)length;
                system_close(pipe[1]);
                p8 address_to answer = memory_arena_read_tail(
                    &roomy_arena, pipe[0], 16, 20, 16, &roomy_used, &roomy_failed);
                system_close(pipe[0]);
                check("arena read takes the aligned floor of an unaligned room",
                      sent && answer == roomy + 16 && !roomy_failed &&
                      roomy_used == length && !answer[length] &&
                      !memory_compare(answer, tail, length) &&
                      roomy_arena.used == 16 + 64);
        }
}

static p8 reuse_hex(p8 value)
{
        return value < 10 ? '0' + value : 'a' + value - 10;
}

static fn reuse_masks(void)
{
        positive set[UL_CPU_WORDS] = {0};
        p8 wanted[256], bounded[258];
        positive seed = 0x534841524544;
        for (positive shape = 0; shape < 64; shape++)
        {
                for (positive i = 0; i < 8; i++)
                {
                        XORSHIFT64(seed);
                        set[i] = shape == 0 ? 0 : shape == 1 ? positive_max : seed;
                }
                for (positive bytes = 0; bytes <= 64; bytes++)
                        for (positive grouped = 0; grouped < 2; grouped++)
                        {
                                positive nibbles = bytes * 2, used = 0;
                                while (nibbles > 1 &&
                                       !((set[(nibbles - 1) / 16] >>
                                          (((nibbles - 1) % 16) * 4)) & 15))
                                        nibbles--;
                                for (positive left = nibbles; left; left--)
                                {
                                        if (grouped && left != nibbles && !(left % 8))
                                                wanted[used++] = ',';
                                        positive at = left - 1;
                                        wanted[used++] = reuse_hex(
                                            set[at / 16] >> ((at % 16) * 4) & 15);
                                }
                                reuse_used = 0;
                                ul_cpu_mask_write(reuse_capture, set, bytes, grouped);
                                check("mask formatter byte widths and grouping",
                                      reuse_used == used &&
                                      !memory_compare(reuse_output, wanted, used));
                        }
                reuse_used = 0;
                ul_cpu_mask_write(reuse_capture, set, sizeof(set), true);
                for (positive room = 0; room < sizeof(bounded) - 2; room++)
                {
                        memory_fill(bounded, 0xa5, sizeof(bounded));
                        ul_lscpu_set_text(bounded + 1, room, set, true);
                        positive take = room ? min(room - 1, reuse_used) : 0;
                        check("lscpu bounded shared mask sink",
                              bounded[0] == 0xa5 && bounded[room + 1] == 0xa5 &&
                              !memory_compare(bounded + 1, reuse_output, take) &&
                              (!room || !bounded[take + 1]));
                }
        }
        memory_zero(set, sizeof(set));
        ul_bit_range_set(set, 1, 2, 1);
        ul_bit_range_set(set, 5, 13, 4);
        reuse_used = 0;
        ul_cpu_list_write(reuse_capture, set, sizeof(set), true);
        check("stride list keeps singleton pairs",
              reuse_used == 10 && !memory_compare(reuse_output, "1,2,5-13:4", 10));
        ul_lscpu_set_text(bounded, sizeof(bounded), set, false);
        check("lscpu keeps contiguous-pair policy", string_equals(bounded, "1-2,5,9,13"));
}

static fn reuse_uuid(void)
{
        p8 input[16], made[39], wanted[37];
        for (positive seed = 0; seed < 256; seed++)
        {
                positive used = 0;
                for (positive i = 0; i < 16; i++)
                {
                        input[i] = (p8)(seed + i * 31);
                        if (i == 4 || i == 6 || i == 8 || i == 10)
                                wanted[used++] = '-';
                        wanted[used++] = reuse_hex(input[i] >> 4);
                        wanted[used++] = reuse_hex(input[i] & 15);
                }
                wanted[used] = 0;
                memory_fill(made, 0xa5, sizeof(made));
                storage_uuid_bytes(made + 1, input);
                check("shared UUID hex groups and exact bounds",
                      made[0] == 0xa5 && made[38] == 0xa5 &&
                      !memory_compare(made + 1, wanted, sizeof(wanted)));
        }
}

static fn reuse_lists(void)
{
        static const positive offsets[] = {0, 25, 57, 121};
        positive set[UL_CPU_WORDS], parsed[UL_CPU_WORDS];
        p8 wanted[256], actual[256];
        for (positive pattern = 0; pattern < 4096; pattern++)
                for (positive shift = 0; shift < array_count(offsets); shift++)
                {
                        positive base = offsets[shift], used = 0;
                        memory_zero(set, sizeof(set));
                        for (positive bit = 0; bit < 12; bit++)
                                if (pattern & ((positive)1 << bit))
                                        set[(base + bit) / 64] |= (positive)1 << ((base + bit) % 64);
                        for (positive bit = 0; bit < 12; bit++)
                        {
                                if (!(pattern & ((positive)1 << bit))) continue;
                                positive last = bit;
                                while (last + 1 < 12 && (pattern & ((positive)1 << (last + 1)))) last++;
                                if (used) wanted[used++] = ',';
                                used += positive_into(wanted + used, base + bit);
                                if (last != bit)
                                {
                                        wanted[used++] = '-';
                                        used += positive_into(wanted + used, base + last);
                                }
                                bit = last;
                        }
                        wanted[used] = 0;
                        ul_lscpu_set_text(actual, sizeof(actual), set, false);
                        check("contiguous list all 12-bit masks across word boundaries",
                              string_equals(actual, wanted));
                        reuse_used = 0;
                        ul_cpu_list_write(reuse_capture, set, sizeof(set), true);
                        reuse_output[reuse_used] = 0;
                        memory_zero(parsed, sizeof(parsed));
                        p8 okay = !reuse_used || ul_bit_list_read(reuse_output, parsed, UL_CPU_BITS, false);
                        check("stride list round trip all 12-bit masks across word boundaries",
                              okay && !memory_compare(parsed, set, sizeof(set)));
                }
}

static fn reuse_json(void)
{
        static const positive lengths[] = {0, 1, 7, 8, 15, 16, 17, 31, 32, 33,
                                            127, 255, 256, 257, 511, 513,
                                            1023, 1024, 4097, 8193};
        static p8 input[8193], wanted[65536];
        for (positive seed = 0; seed < 256; seed++)
                for (positive n = 0; n < array_count(lengths); n++)
                        for (positive lower = 0; lower < 2; lower++)
                        {
                                positive length = lengths[n], used = 0;
                                wanted[used++] = '"';
                                for (positive i = 0; i < length; i++)
                                {
                                        p8 byte = input[i] = (p8)(seed + i * 13);
                                        if (lower && byte >= 'A' && byte <= 'Z') byte += 32;
                                        p8 short_name = byte == '\b' ? 'b' : byte == '\f' ? 'f' :
                                            byte == '\n' ? 'n' : byte == '\r' ? 'r' : byte == '\t' ? 't' : 0;
                                        if (short_name || byte == '"' || byte == '\\')
                                        {
                                                wanted[used++] = '\\';
                                                wanted[used++] = short_name ? short_name : byte;
                                        }
                                        else if (byte < 32)
                                        {
                                                wanted[used++] = '\\'; wanted[used++] = 'u';
                                                wanted[used++] = '0'; wanted[used++] = '0';
                                                wanted[used++] = reuse_hex(byte >> 4);
                                                wanted[used++] = reuse_hex(byte & 15);
                                        }
                                        else wanted[used++] = byte;
                                }
                                wanted[used++] = '"';
                                text_out_used = 0;
                                writer_json_span(text_put, (byte_span){input, length}, lower, true);
                                check("column JSON exact short controls and lowercase chunks",
                                      text_out_used == used &&
                                      !memory_compare(text_out_buffer, wanted, used));
                        }
        static p8 long_name[32768];
        memory_fill(long_name, 'Q', sizeof(long_name));
        text_out_used = 0;
        writer_json_span(text_put, (byte_span){long_name, sizeof(long_name)}, true, true);
        check("long literal JSON key bounded lowercase chunks",
              text_out_used == sizeof(long_name) + 2 && text_out_buffer[0] == '"' &&
              text_out_buffer[text_out_used - 1] == '"' &&
              memory_count(text_out_buffer + 1, sizeof(long_name), 'q') == sizeof(long_name));
        text_out_used = 0;
}

static fn reuse_braces(void)
{
        static const bipolar values[] = {0, 1, -1, 9, -9, 10, -10, 99, -99,
                                         bipolar_min, bipolar_max};
        for (positive i = 0; i < array_count(values); i++)
                for (positive width = 0; width <= 40; width++)
                        for (positive padded = 0; padded < 2; padded++)
                        {
                                p8 plain[24], wanted[64], actual[66];
                                positive length = bipolar_into(plain, values[i]);
                                positive sign = values[i] < 0, used = 0;
                                if (sign) wanted[used++] = '-';
                                if (padded)
                                        for (positive n = length; n < width; n++) wanted[used++] = '0';
                                for (positive n = sign; n < length; n++) wanted[used++] = plain[n];
                                memory_fill(actual, 0xa5, sizeof(actual));
                                positive made = expand_brace_number_text(actual + 1, values[i], width, padded);
                                check("brace signed minimum widths use shared decimal floor",
                                      made == used && !memory_compare(actual + 1, wanted, used) &&
                                      actual[0] == 0xa5 && actual[used + 1] == 0xa5);
                        }
}

static fn reuse_percent_b(void)
{
        static const positive lengths[] = {0, 1, 7, 8, 15, 16, 255, 256, 4096, 8193};
        static const struct { string_address source, wanted; } endings[] = {
            {"", ""}, {"\\n", "\n"}, {"\\012", "\n"}, {"\\q", "\\q"},
            {"\\\\", "\\"}, {"\\", "\\"}, {"\\cignored", ""},
        };
        static p8 source[8224];
        for (positive n = 0; n < array_count(lengths); n++)
                for (positive e = 0; e < array_count(endings); e++)
                {
                        positive length = lengths[n];
                        memory_fill(source, 'Q', length);
                        string_copy(source + length, endings[e].source);
                        printf_in_b = true;
                        printf_cut = false;
                        reuse_used = 0;
                        printf_escaped(reuse_capture, source);
                        positive tail = string_length(endings[e].wanted);
                        check("printf percent-b shared slash scanner",
                              reuse_used == length + tail &&
                              memory_count(reuse_output, length, 'Q') == length &&
                              !memory_compare(reuse_output + length, endings[e].wanted, tail));
                }
        printf_cut = printf_in_b = false;
}

#ifdef REUSE_BENCHMARK
static fn reuse_json_scalar(byte_span value, p8 lower)
{
        text_put_character('"');
        for (positive i = 0; i < value.length; i++)
        {
                p8 byte = value.bytes[i];
                if (lower && byte >= 'A' && byte <= 'Z') byte += 32;
                if (byte == '"' || byte == '\\')
                {
                        text_put_character('\\');
                        text_put_character(byte);
                }
                else if (byte == '\b') text_put_string("\\b");
                else if (byte == '\f') text_put_string("\\f");
                else if (byte == '\n') text_put_string("\\n");
                else if (byte == '\r') text_put_string("\\r");
                else if (byte == '\t') text_put_string("\\t");
                else if (byte < 32)
                {
                        p8 escaped[] = {'\\', 'u', '0', '0', reuse_hex(byte >> 4), reuse_hex(byte & 15)};
                        text_put(escaped, sizeof(escaped));
                }
                else text_put_character(byte);
        }
        text_put_character('"');
}

static fn reuse_benchmark(void)
{
        static const positive lengths[] = {16, 256, 4096, 32768};
        static p8 bytes[32768];
        for (positive shape = 0; shape < 2; shape++)
        {
                for (positive i = 0; i < sizeof(bytes); i++)
                        bytes[i] = shape && i % 32 == 31 ? '\n' : 'Q';
                for (positive n = 0; n < array_count(lengths); n++)
                        for (positive lower = 0; lower < 2; lower++)
                        {
                                positive elapsed[2];
                                for (positive engine = 0; engine < 2; engine++)
                                {
                                        positive start = get_cpu_time();
                                        for (positive i = 0; i < 8388608 / lengths[n]; i++)
                                        {
                                                text_out_used = 0;
                                                if (engine) writer_json_span(text_put, (byte_span){bytes, lengths[n]}, lower, true);
                                                else reuse_json_scalar((byte_span){bytes, lengths[n]}, lower);
                                        }
                                        elapsed[engine] = get_cpu_time() - start;
                                }
                                string_format(log, "json shape=%p bytes=%p lower=%p scalar=%p shared=%p percent=%p\n",
                                    shape, lengths[n], lower, elapsed[0], elapsed[1], elapsed[1] * 100 / elapsed[0]);
                        }
        }
        text_out_used = 0;
}
#endif

static fn reuse_lookup_contracts(void)
{
        for (positive byte = 0; byte < 256; byte++)
        {
                p8 digit = byte >= '0' && byte <= '9';
                p8 name = digit || byte == '_' ||
                            (byte >= 'A' && byte <= 'Z') ||
                            (byte >= 'a' && byte <= 'z');
                p8 text[] = {(p8)byte, 0};
                check("prepared sets cover exactly ASCII names and digits",
                      string_set_name[byte] == name &&
                      string_set_digits[byte] == digit &&
                      string_span_max(text, 1, string_set_name) == name &&
                      string_span(text, string_set_digits) == digit);
        }
        const named_byte rows[] = {
            {"Zero", 0}, {"VALUE", 7}, {"value", 9}, {null, 0}, {"later", 1}};
        check("folded lookup returns first row with its full stride",
              string_table_find_ascii_case("zErO", rows, sizeof(rows[0]), 5) == 0 &&
              string_table_find_ascii_case("vAlUe", rows, sizeof(rows[0]), 5) == 1);
        check("folded lookup preserves misses, null rows and empty tables",
              string_table_find_ascii_case("later", rows, sizeof(rows[0]), 5) == 5 &&
              string_table_find_ascii_case("", rows, sizeof(rows[0]), 5) == 5 &&
              string_table_find_ascii_case("value", rows, sizeof(rows[0]), 1) == 1 &&
              string_table_find_ascii_case("value", null, 0, 0) == 0);
        check("syscall zero and large results are successes",
              !system_failed(0) && !system_failed(1) &&
              !system_failed((positive)1 << 63) && !system_failed(-4096));
        check("syscall error window includes both boundaries",
              system_failed(-1) && system_failed(-4095));
}

static fn reuse_counted_classes(void)
{
        p8 address_to pages = memory(8192);
        check("class guard fixture allocates", pages && (positive)pages < (positive)-4095);
        if (!pages || (positive)pages >= (positive)-4095) return;
        bipolar status = system_call_3(syscall(mprotect), (positive)(pages + 4096),
                                       4096, FILE_PROTECT_NONE);
        check("class guard fixture protects", !status);
        if (!status)
                for (positive length = 0; length <= 9; length++)
                {
                        p8 address_to text = pages + 4096 - length;
                        memory_copy(text, "[:alpha:]", length);
                        check("class prefix never reads past its counted end",
                              byte_class_end(text, pages + 4096) ==
                                  (length == 9 ? pages + 4096 : null));
                }
        memory_free(pages, 8192);
}

static fn reuse_utility_numbers(void)
{
        check("shared size suffixes retain case and dialect policy",
              size_suffix_power('K', false) == 1 &&
              size_suffix_power('q', true) == 10 &&
              !size_suffix_power('q', false) &&
              !size_suffix_power('J', true));
        p64 scaled = 73;
        check("shared size scaling commits only checked products",
              size_scale_power_checked(2, 1024, 3, positive_max,
                                       &scaled) &&
              scaled == 2147483648ULL);
        scaled = 73;
        check("shared size scaling rejects overflow without changing output",
              !size_scale_power_checked(positive_max, 1024, 1,
                                        positive_max, &scaled) &&
              scaled == 73);
        positive unit = 0;
        p8 human = false, si = false;
        p8 suffix[8];
        check("ls block size applies the checked count and suffix together",
              ls_block_size_read("2K", &unit, &human, &si, suffix) &&
              unit == 2048 && !suffix[0]);
        check("ls block size refuses count-times-suffix overflow",
              !ls_block_size_read("18446744073709551615K", &unit,
                                  &human, &si, suffix));

        for (bipolar sign = -1; sign <= 1; sign += 2)
        {
                check("file reasons preserve accepted error text and both signs",
                      string_equals(file_reason(sign * ERROR_ACCESS), "Permission denied") &&
                      string_equals(file_reason(sign * ERROR_OVER_QUOTA), "Disk quota exceeded"));
                check("file reasons name EINTR as GNU does and leave unlisted errors as Error",
                      string_equals(file_reason(sign * EINTR), "Interrupted system call") &&
                      string_equals(file_reason(sign * EDOM), "Error"));
        }
        check("file reasons bound magnitudes before narrowing to a byte",
              string_equals(file_reason(256 + ERROR_ACCESS), "Error") &&
              string_equals(file_reason(bipolar_min), "Error") &&
              string_equals(file_reason(bipolar_max), "Error"));
        positive number = 9;
        check("checked utility decimals accept the whole unsigned range",
              file_unsigned_decimal("18446744073709551615", &number) && number == positive_max);
        static const string_address rejected[] = {
            "", "+1", "-1", " 1", "1 ", "1x", "18446744073709551616",
        };
        for (positive at = 0; at < array_count(rejected); at++)
        {
                number = 9;
                check("checked utility decimals reject overflow without changing the default",
                      !file_unsigned_decimal(rejected[at], &number) && number == 9);
        }
}

/* Retained programs own their metadata; transient and failed compilations
   must not replace it. Exercise the production matcher and library floor. */
static fn reuse_regex(void)
{
        regex_program kept[3];
        string_address patterns[] = {"(ab){2,4}", "[a-z]+", "x|yz"};
        string_address subjects[] = {"ababab", "alpha", "yz"};
        for (positive i = 0; i < 3; i++)
        {
                check("retained regex compiles",
                      regex_compile(patterns[i], true, false, false, REGEX_POLICY_DEFAULT));
                regex_keep(kept + i);
        }
        for (positive round = 0; round < 80; round++)
        {
                check("transient regex compiles above retained programs",
                      regex_compile("temporary", true, false, false, REGEX_POLICY_DEFAULT));
                for (positive i = 0; i < 3; i++)
                {
                        regex_current = kept[i];
                        check("selected regex retains graph and prepared metadata",
                              regex_find(REGEX_LONGEST, subjects[i], string_length(subjects[i]), 0) &&
                              regex_slots[0] == 0 && regex_slots[1] == string_length(subjects[i]));
                }
        }
        regex_pool.used = (rx_mark){0};
        static p8 repeated[30002];
        for (positive i = 0; i < 4090; i++)
        {
                repeated[i * 2] = 'a';
                repeated[i * 2 + 1] = '*';
        }
        repeated[8180] = 0;
        check("long simple-repeat chains retain the old accepted capacity",
              regex_compile(repeated, true, false, false, REGEX_POLICY_DEFAULT) &&
              regex_find(REGEX_LONGEST, "", 0, 0));
        repeated[0] = 'a';
        for (positive i = 0; i < 10000; i++)
                memory_copy_apart(repeated + 1 + i * 3, "{1}", 3);
        repeated[30001] = 0;
        check("identity intervals do not consume extra graph nodes",
              regex_compile(repeated, true, false, false, REGEX_POLICY_DEFAULT) &&
              regex_find(REGEX_LONGEST, "a", 1, 0));
        memory_copy_apart(repeated, "(){0}", 5);
        for (positive i = 0; i < 25; i++)
                memory_copy_apart(repeated + 5 + i * 3, "{2}", 3);
        repeated[80] = 0;
        check("mandatory repeats of erased groups stay empty without exponential work",
              regex_compile(repeated, true, false, false, REGEX_POLICY_DEFAULT) &&
              regex_find(REGEX_LONGEST | REGEX_CAPTURES, "", 0, 0) && regex_slots[2] == positive_max);
        rx_mark mark = regex_pool.used;
        regex_program published = regex_current;
        check("invalid compile does not publish partial state",
              !rx_compile(&regex_pool, &regex_current, "([", true, false, false,
                          REGEX_POLICY_DEFAULT) && regex_current.first == published.first &&
              regex_current.hints == published.hints &&
              regex_pool.used.nodes == mark.nodes && regex_pool.used.sets == mark.sets);
        check("capture-bearing search succeeds",
              regex_compile("(a)", true, false, false, REGEX_POLICY_DEFAULT) &&
              regex_find(REGEX_LONGEST | REGEX_CAPTURES, "a", 1, 0) && regex_slots[2] == 0);
        // A count of 2^64 + 1 wrapped to one in the reader; it is text now, as a count past 32767 is.
        check("an interval count past 2^64 is not read as a small count",
              regex_compile("a{18446744073709551617}", true, false, false, REGEX_POLICY_DEFAULT) &&
              !regex_find(REGEX_LONGEST, "aaa", 3, 0) &&
              regex_find(REGEX_LONGEST, "a{18446744073709551617}", 23, 0));
        check("literal shortcut clears captures removed by a zero count",
              regex_compile("(b){0}x", true, false, false, REGEX_POLICY_DEFAULT) &&
              regex_find(REGEX_LONGEST | REGEX_CAPTURES, "x", 1, 0) &&
              regex_slots[2] == positive_max && regex_slots[3] == positive_max);
        regex_pool.used = (rx_mark){0};

        /*
                More programs kept than a table held, and a pattern bigger
                than the scratch began with: a thousand and a hundred of
                them each its own block and each answering after the rest
                were made, a pattern of three hundred brackets, which a byte
                numbered up to two hundred and fifty-five of, and one of a
                hundred thousand bytes, which was refused at eight thousand.
        */
        {
                static regex_program many[1100];
                static p8 pattern[32], subject[32];
                p8 made = true, answered = true, apart = true;

                for (positive i = 0; i < 1100; i++)
                {
                        pattern[0] = 'k';
                        positive_into_padded(pattern + 1, i, 4, '0');
                        memory_copy_apart(pattern + 5, "[a-c]", 6);
                        made = made && regex_compile((string_address)pattern, true, false, false,
                                                     REGEX_POLICY_DEFAULT);
                        if (made)
                                regex_keep(many + i);
                }
                check("a thousand and a hundred programs are kept", made);

                for (positive i = 0; made && i < 1100; i++)
                {
                        subject[0] = 'k';
                        positive_into_padded(subject + 1, i, 4, '0');
                        memory_copy_apart(subject + 5, "b", 2);
                        regex_current = many[i];
                        answered = answered &&
                                   regex_find(REGEX_LONGEST, (string_address)subject, 6, 0) &&
                                   regex_slots[0] == 0 && regex_slots[1] == 6;
                        regex_current = many[(i + 1) % 1100];
                        apart = apart && !regex_find(REGEX_LONGEST, (string_address)subject, 6, 0);
                }
                check("each of them still answers after the others were made", made && answered);
                check("and only for what it was made of", made && apart);
        }
        {
                static p8 brackets[260 * 5 + 6], text[262];

                for (positive i = 0; i < 260; i++)
                        memory_copy_apart(brackets + 5 * i, "[0-9]", 5);
                memory_copy_apart(brackets + 5 * 260, "[a-z]", 6);
                memory_fill(text, '0', 260);
                text[260] = 'a';
                text[261] = 0;
                check("a pattern of more brackets than a byte numbers finds what they say",
                      regex_compile((string_address)brackets, false, false, false, REGEX_POLICY_DEFAULT) &&
                      regex_find(REGEX_LONGEST, (string_address)text, 261, 0) &&
                      regex_slots[0] == 0 && regex_slots[1] == 261);
        }
        {
                static p8 literal[100001], text[100011];

                memory_fill(literal, 'x', 100000);
                literal[100000] = 0;
                memory_fill(text, 'y', 10);
                memory_fill(text + 10, 'x', 100000);
                text[100010] = 0;
                check("a pattern of a hundred thousand bytes is compiled and found",
                      regex_compile((string_address)literal, false, false, false, REGEX_POLICY_DEFAULT) &&
                      regex_find(REGEX_LONGEST, (string_address)text, 100010, 0) &&
                      regex_slots[0] == 10 && regex_slots[1] == 100010);
        }
        {
                regex_program kept;

                check("a pattern of nothing is kept, and finds the empty line",
                      regex_compile("", false, false, false, REGEX_POLICY_EXPR) &&
                      rx_keep(&regex_pool, &regex_current, &kept) &&
                      (regex_current = kept, regex_find(REGEX_LONGEST, "abc", 3, 0)) &&
                      regex_slots[0] == 0 && regex_slots[1] == 0);
        }
        {
                static const struct { string_address pattern, text; positive length; p8 found; } folds[] = {
                    {"\xc3\x89", "caf\xc3\xa9", 5, true},
                    {"caf\xc3\xa9+", "CAF\xc3\x89\xc3\x89", 7, true},
                    {"[\xc3\xa9x]\xc3\xa0", "\xc3\x89\xc3\x80", 4, true},
                    {"\xc3\xa9", "e", 1, false},
                    {"\xc7\x84", "\xc7\x85", 2, true},
                    {"\xcf\x83", "\xcf\x82", 2, true},
                    {"\xce\xa3", "\xcf\x82", 2, true},
                    {"\xcf\x82", "\xcf\x83", 2, true},
                    {"\xe2\x84\xaa", "k", 1, false},
                    {"\xc3\x85", "\xe2\x84\xab", 3, false}};
                p8 every = true;

                for (positive i = 0; i < sizeof(folds) / sizeof(folds[0]); i++)
                        every = every &&
                                regex_compile(folds[i].pattern, true, true, false,
                                              REGEX_POLICY_DEFAULT | REGEX_CHARACTERS) &&
                                regex_find(REGEX_LONGEST, folds[i].text, folds[i].length, 0) ==
                                    folds[i].found;
                check("a character of more than one byte matches the others of its case", every);
        }
        regex_pool.used = (rx_mark){0};
}

b32 main(void)
{
        reuse_regex();
        reuse_utility_numbers();
        reuse_lookup_contracts();
        reuse_counted_classes();
        timespec span;
        check("read timeout keeps permissive zero spellings",
              read_timeout("", &span) && !span.tv_sec && !span.tv_nsec &&
              read_timeout("-.", &span) && !span.tv_sec && !span.tv_nsec);
        check("read timeout keeps signed limit and microsecond truncation",
              read_timeout("9223372036854775807.1234569", &span) &&
              span.tv_sec == b64_max && span.tv_nsec == 123456000);
        check("read timeout refuses overflowing seconds and suffixes",
              !read_timeout("9223372036854775808", &span) &&
              !read_timeout("18446744073709551616", &span) &&
              !read_timeout("12x", &span) && !read_timeout("-0.000001", &span));
        bipolar number;
        positive mode;
        check("nice saturates the complete overflowing digit run",
              nice_adjustment("18446744073709551616", &number) && number == 39 &&
              nice_adjustment("-18446744073709551616", &number) && number == -39 &&
              !nice_adjustment("18446744073709551616x", &number));
        check("chmod rejects octal overflow",
              !file_mode_adjust("2000000000000000000000", 0, false, 0, false, &mode, false));
        check("chmod keeps explicit leading-zero policy",
              file_mode_adjust("0000755", 06000, true, 0, false, &mode, false) && mode == 0755);
        check("replay refuses nanosecond ceil-add overflow",
              !process_replay_sleep(18446744055453255927ull, 999999999, true, 0));
        positive duration;
        check("duration nanosecond limit survives discarded zeros",
              file_duration_read("18446744073.709551615", false, &duration) && duration == positive_max &&
              file_duration_read("18446744073.7095516150", false, &duration) && duration == positive_max);
        check("duration overflow cannot drop a digit and resume",
              !file_duration_read("18446744073.7095516160", false, &duration) &&
              !file_duration_read("18446744073709551616", true, &duration));
        check("duration scientific notation and suffix policy",
              file_duration_read("1.25e-3", false, &duration) && duration == 1250000 &&
              file_duration_read("1.5m", true, &duration) && duration == 90000000000 &&
              !file_duration_read("1.5m", false, &duration));
        p8 long_duration[1024];
        memory_fill(long_duration, '0', sizeof(long_duration));
        memory_copy(long_duration + sizeof(long_duration) - 2, "1", 2);
        check("duration spelling has no fixed scratch ceiling",
              file_duration_read(long_duration, true, &duration) && duration == 1000000000);
        storage_mount_options options = {0};
        check("mount data refuses empty and nonempty size overflow",
              !storage_data_add(&options, "x", positive_max) && !options.data.used &&
              storage_data_add(&options, "x", 1) &&
              !storage_data_add(&options, "x", positive_max - 1) && options.data.used == 1 &&
              options.data.bytes[0] == 'x');
        byte_store_release(&options.data);
        shell_store store = {0};
        check("arena starts with a small block", shell_store_take(&store, 1) != null);
        shell_block address_to head = store.head;
        store.here = null;
        check("arena grows before its head without losing the chain",
              head && shell_store_take(&store, head->size + 1) &&
              store.head != head && store.head->next == head);
        while (store.head)
        {
                shell_block address_to next = store.head->next;
                memory_free(store.head, sizeof(shell_block) + store.head->size);
                store.head = next;
        }
        store.here = null;
        shell_memory_failed = false;
        check("arena span rejects length overflow",
              !shell_store_copy(&store, null, positive_max) && shell_memory_failed);
        shell_memory_failed = false;
        {
                shell_store odd = {0};
                p8 aligned = true;

                //      A string of any length ahead of a record must leave
                //      the record eight-aligned: it holds pointers, and a
                //      word stored through an odd address is the undefined
                //      behaviour the compiler's alignment check reports.
                for (positive length = 0; length < 24; length++)
                {
                        p8 address_to text = shell_store_copy(&odd, "abcdefghijklmnopqrstuvwx", length);
                        p8 address_to record = shell_store_take_aligned(&odd, 24);

                        if (!text || !record || ((positive)record & 7))
                                aligned = false;
                }
                check("a record taken after a string of any length is eight-aligned", aligned);
                while (odd.head)
                {
                        shell_block address_to next = odd.head->next;
                        memory_free(odd.head, sizeof(shell_block) + odd.head->size);
                        odd.head = next;
                }
        }
        seq_format sequence;
        //      seq_format_read answers with why a format is refused and
        //      SEQ_FORMAT_GOOD (nought) for one it takes, where it once
        //      answered true, and it no longer cuts a width or a precision
        //      at a million and nine: GNU takes any size up to what an int
        //      holds and fails when it writes, which the differential asks.
        check("seq shared conversion fields retain literals and flags and leave the precision unsaid",
              seq_format_read("%%[%+-08Lf]%%", &sequence) == SEQ_FORMAT_GOOD && sequence.directive == 3 &&
              sequence.after == 10 && sequence.width == 8 && !sequence.precise &&
              sequence.flags == (CONVERSION_FLAG_LEFT | CONVERSION_FLAG_PLUS | CONVERSION_FLAG_ZERO));
        check("seq reads a width and a precision of any size an int holds",
              seq_format_read("%1000009.1000009f", &sequence) == SEQ_FORMAT_GOOD &&
              sequence.width == 1000009 && sequence.precision == 1000009 &&
              seq_format_read("%1000010f", &sequence) == SEQ_FORMAT_GOOD && sequence.width == 1000010 &&
              seq_format_read("%.1000010f", &sequence) == SEQ_FORMAT_GOOD && sequence.precision == 1000010);
        check("seq rejects stars, saturates overflow and refuses multiple conversions",
              seq_format_read("%*.2f", &sequence) == SEQ_FORMAT_UNKNOWN &&
              seq_format_read("%1.*f", &sequence) == SEQ_FORMAT_UNKNOWN &&
              seq_format_read("%18446744073709551616f", &sequence) == SEQ_FORMAT_GOOD &&
              sequence.width > SEQ_INT_MAX &&
              seq_format_read("%f%%f%f", &sequence) == SEQ_FORMAT_MANY);
        reuse_arguments();
        reuse_bulk_output();
        reuse_stable_arena();
        reuse_masks();
        reuse_lists();
        reuse_uuid();
        reuse_json();
        reuse_braces();
        reuse_percent_b();
        check("uniq field skip terminates at EOL", uniq_skipped(" a b", 4, positive_max, 0) == 4);
        check("uniq character skip saturates", uniq_skipped(" a b", 4, 1, positive_max) == 4);
        check("uniq empty field skip is bounded", !uniq_skipped("", 0, positive_max, positive_max));
#ifdef REUSE_BENCHMARK
        if (!failures) reuse_benchmark();
#endif
        return test_report(null);
}
#endif /* CHECK_reuse_shell */

#ifdef CHECK_path_stage
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

#ifndef LINUX
#error "protected path staging is a Linux userspace primitive"
#endif

static bipolar path_stage_make(
    bipolar directory, string_address name, string_address data)
{
        bipolar handle = system_open_at_mode(
            directory, name,
            FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC, 0600);
        bipolar written = handle < 0 ? handle : system_write_all(
            (positive)handle, data, string_length(data));
        bipolar closed = handle < 0 ? 0 : system_close(handle);

        return handle < 0 ? handle : written < 0 ? written : closed;
}

static bipolar path_stage_read(
    bipolar directory, string_address name, p8 address_to into, positive room)
{
        bipolar handle = system_open_at(
            directory, name, FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar got = handle < 0 ? handle : system_read_retry(
            (positive)handle, into, room);

        if (handle >= 0)
                system_close(handle);
        return got;
}

static p8 path_stage_exists(bipolar directory, string_address name)
{
        system_path_identity identity;

        return system_path_identity_at(
                   directory, name,
                   SYSTEM_PATH_AT_SYMLINK_NOFOLLOW |
                       SYSTEM_PATH_AT_NO_AUTOMOUNT,
                   SYSTEM_PATH_STATX_IDENTITY, address_of identity) >= 0;
}

static fn path_stage_copy_name(
    p8 address_to into, string_address from)
{
        memory_copy_end(into, from, string_length(from));
}

static bipolar path_stage_restore(system_path_stage address_to stage)
{
        bipolar restored = system_path_stage_publish_at(
            stage, stage->parent, stage->original,
            SYSTEM_PATH_RENAME_NOREPLACE);
        if (restored < 0)
                system_path_stage_release(stage);
        return restored;
}

static fn path_stage_normal(bipolar directory)
{
        p8 private_name[256];
        system_path_stage stage;

        check("protected removal fixture is created",
              path_stage_make(directory, (string_address)"normal",
                              (string_address)"ours") == 0);
        bipolar opened = system_open_at(
            directory, (string_address)"normal",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        check("protected removal fixture stays open", opened >= 0);
        bipolar staged = opened < 0 ? opened : system_path_stage_opened_at(
            address_of stage, directory, (string_address)"normal", opened);
        check("a verified name moves under an fd-held private directory",
              staged == 0 && stage.verified &&
                  system_path_same_opened_at(
                      opened, stage.directory,
                      SYSTEM_PATH_STAGE_LEAF) == 0);
        if (staged >= 0)
        {
                path_stage_copy_name(private_name, stage.private_name);
                check("the protected inode is removed through the private fd",
                      system_path_stage_remove(address_of stage, 0) == 0);
                check("owner-private parent cleanup removes the empty stage",
                      !path_stage_exists(directory, private_name));
        }
        check("protected removal vacates only the requested public name",
              !path_stage_exists(directory, (string_address)"normal"));
        if (opened >= 0)
                system_close(opened);
}

static fn path_stage_restrictive_umask(bipolar directory)
{
        system_path_stage stage;

        check("restrictive-umask source is created",
              path_stage_make(directory, (string_address)"masked",
                              (string_address)"ours") == 0);
        bipolar opened = system_open_at(
            directory, (string_address)"masked",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar former = system_call_1(syscall(umask), 0777);
        bipolar staged = opened < 0 || former < 0 ? -1 :
            system_path_stage_opened_at(
                address_of stage, directory, (string_address)"masked",
                opened);
        if (former >= 0)
                system_call_1(syscall(umask), (positive)former);

        check("restrictive umask still yields an exact private stage",
              staged == 0 && system_path_private_directory_valid(
                                 stage.directory, directory,
                                 stage.private_name) == 0);
        check("restrictive-umask stage restores its exact source",
              staged >= 0 && path_stage_restore(address_of stage) == 0);
        check("restrictive-umask source identity survives staging",
              opened >= 0 && system_path_same_opened_at(
                                 opened, directory,
                                 (string_address)"masked") == 0);
        if (opened >= 0)
                system_close(opened);
        system_remove_at(directory, (string_address)"masked", 0);
}

static fn path_stage_mismatch(bipolar directory)
{
        p8 bytes[16];

        check("mismatch original fixture is created",
              path_stage_make(directory, (string_address)"mismatch",
                              (string_address)"expected") == 0);
        bipolar opened = system_open_at(
            directory, (string_address)"mismatch",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        check("mismatch original fixture stays open", opened >= 0);
        check("mismatch original name is displaced",
              system_call_5(
                  syscall(renameat2), (positive)directory,
                  (positive)(string_address)"mismatch", (positive)directory,
                  (positive)(string_address)"expected",
                  SYSTEM_PATH_RENAME_NOREPLACE) == 0);
        check("mismatch attacker fixture is created",
              path_stage_make(directory, (string_address)"mismatch",
                              (string_address)"attacker") == 0);

        bipolar removed = opened < 0 ? opened : system_path_remove_opened_at(
            directory, (string_address)"mismatch", opened, 0);
        check("an entry that differs from the open handle is refused",
              removed == -11);
        bipolar got = path_stage_read(
            directory, (string_address)"mismatch", bytes, sizeof bytes);
        check("a refused mismatching entry is restored without replacement",
              got == 8 && !memory_compare(bytes, "attacker", 8));
        check("the caller's opened inode remains under its displaced name",
              opened >= 0 && system_path_same_opened_at(
                                 opened, directory,
                                 (string_address)"expected") == 0);

        if (opened >= 0)
                system_close(opened);
        system_remove_at(directory, (string_address)"mismatch", 0);
        system_remove_at(directory, (string_address)"expected", 0);
}

static fn path_stage_restore_collision(bipolar directory)
{
        p8 private_name[256];
        p8 bytes[16];
        system_path_stage stage;

        check("restore fixture is created",
              path_stage_make(directory, (string_address)"restore",
                              (string_address)"protected") == 0);
        bipolar opened = system_open_at(
            directory, (string_address)"restore",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar staged = opened < 0 ? opened : system_path_stage_opened_at(
            address_of stage, directory, (string_address)"restore", opened);
        check("restore fixture enters protected staging", staged == 0);
        if (staged >= 0)
        {
                path_stage_copy_name(private_name, stage.private_name);
                check("a concurrent public claimant is created",
                      path_stage_make(directory, (string_address)"restore",
                                      (string_address)"claimant") == 0);
                check("restore never overwrites a concurrent claimant",
                      path_stage_restore(address_of stage) == -17);
                bipolar got = path_stage_read(
                    directory, (string_address)"restore", bytes,
                    sizeof bytes);
                check("the concurrent claimant remains intact",
                      got == 8 && !memory_compare(bytes, "claimant", 8));

                bipolar private_directory = system_open_at(
                    directory, private_name,
                    FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                check("failed restore retains the private directory",
                      private_directory >= 0);
                check("failed restore retains the exact protected inode",
                      private_directory >= 0 && opened >= 0 &&
                          system_path_same_opened_at(
                              opened, private_directory,
                              SYSTEM_PATH_STAGE_LEAF) == 0);
                if (private_directory >= 0)
                {
                        system_remove_at(private_directory,
                                         SYSTEM_PATH_STAGE_LEAF, 0);
                        system_close(private_directory);
                }
                system_remove_at(directory, private_name,
                                 SYSTEM_PATH_AT_REMOVEDIR);
        }
        if (opened >= 0)
                system_close(opened);
        system_remove_at(directory, (string_address)"restore", 0);
}

static fn path_stage_publish_retry(bipolar directory)
{
        system_path_stage stage;

        check("publish-retry source is created",
              path_stage_make(directory, (string_address)"publish-source",
                              (string_address)"ours") == 0);
        check("publish-retry claimant is created",
              path_stage_make(directory, (string_address)"publish-target",
                              (string_address)"theirs") == 0);
        bipolar opened = system_open_at(
            directory, (string_address)"publish-source",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar staged = opened < 0 ? opened : system_path_stage_opened_at(
            address_of stage, directory,
            (string_address)"publish-source", opened);
        check("publish-retry source enters protected staging", staged == 0);
        if (staged >= 0)
        {
                bipolar published = system_path_stage_publish_at(
                    address_of stage, directory,
                    (string_address)"publish-target",
                    SYSTEM_PATH_RENAME_NOREPLACE);
                check("failed protected publication reports the collision",
                      published == -17);
                check("failed protected publication keeps its live stage",
                      stage.verified && stage.directory >= 0 &&
                          system_path_same_opened_at(
                              opened, stage.directory,
                              SYSTEM_PATH_STAGE_LEAF) == 0);
                check("a live failed publication can restore its source",
                      path_stage_restore(address_of stage) == 0);
                check("restored source retains its exact opened identity",
                      system_path_same_opened_at(
                          opened, directory,
                          (string_address)"publish-source") == 0);
        }
        if (opened >= 0)
                system_close(opened);
        system_remove_at(directory, (string_address)"publish-source", 0);
        system_remove_at(directory, (string_address)"publish-target", 0);
}

static fn path_stage_failed_remove(bipolar directory)
{
        check("failed-remove directory is created",
              system_make_directory_exact_at(
                  directory, (string_address)"occupied", 0700) == 0);
        bipolar occupied = system_open_at(
            directory, (string_address)"occupied",
            FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        check("failed-remove occupant is created",
              occupied >= 0 &&
                  path_stage_make(occupied, (string_address)"inside",
                                  (string_address)"bytes") == 0);
        bipolar removed = occupied < 0 ? occupied :
            system_path_remove_opened_at(
                directory, (string_address)"occupied", occupied,
                SYSTEM_PATH_AT_REMOVEDIR);
        check("a failed private removal reports the original failure",
              removed == -39);
        check("a failed private removal restores the verified directory",
              occupied >= 0 && system_path_same_opened_at(
                                   occupied, directory,
                                   (string_address)"occupied") == 0);
        if (occupied >= 0)
        {
                system_remove_at(occupied, (string_address)"inside", 0);
                system_close(occupied);
        }
        system_remove_at(directory, (string_address)"occupied",
                         SYSTEM_PATH_AT_REMOVEDIR);
}

static fn path_stage_hostile_parent(bipolar root)
{
        system_path_stage stage;

        check("hostile writable parent fixture is created",
              system_make_directory_exact_at(
                  root, (string_address)"hostile", 0777) == 0);
        bipolar directory = system_open_at(
            root, (string_address)"hostile",
            FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#if MOONWATER_STRICT < STRICT_TIGHT
        //      The reference makes an entry in any directory the kernel
        //      lets it; only the tight tier calls such a parent hostile.
        check("non-sticky writable parent is trusted as the reference trusts it",
              directory >= 0 && system_path_parent_cleanup_safe(directory));
#else
        check("non-sticky writable parent is classified as hostile",
              directory >= 0 &&
                  !system_path_parent_cleanup_safe(directory));
#endif
        check("hostile-parent source fixture is created",
              directory >= 0 &&
                  path_stage_make(directory, (string_address)"source",
                                  (string_address)"ours") == 0);
        bipolar opened = directory < 0 ? directory : system_open_at(
            directory, (string_address)"source",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar staged = opened < 0 ? opened : system_path_stage_opened_at(
            address_of stage, directory, (string_address)"source", opened);
#if MOONWATER_STRICT >= STRICT_TIGHT
        check("hostile-parent staging fails before namespace mutation",
              staged == -13 && stage.directory < 0 &&
                  !string_get(stage.private_name));
        check("hostile-parent refusal preserves the selected source",
              opened >= 0 && system_path_same_opened_at(
                                 opened, directory,
                                 (string_address)"source") == 0);
#else
        if (staged >= 0)
                (void)system_path_stage_discard(address_of stage, 0);
#endif
        if (opened >= 0)
                system_close(opened);
        if (directory >= 0)
                system_remove_at(directory, (string_address)"source", 0);
        if (directory >= 0)
                system_close(directory);
        system_remove_at(root, (string_address)"hostile",
                         SYSTEM_PATH_AT_REMOVEDIR);
}

static fn path_stage_sticky_parent(bipolar root)
{
        p8 private_name[256];
        system_path_stage stage;

        check("sticky writable parent fixture is created",
              system_make_directory_exact_at(
                  root, (string_address)"sticky", 01777) == 0);
        bipolar directory = system_open_at(
            root, (string_address)"sticky",
            FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        check("sticky writable parent permits identity-safe cleanup",
              directory >= 0 && system_path_parent_cleanup_safe(directory));
        check("sticky-parent source fixture is created",
              directory >= 0 &&
                  path_stage_make(directory, (string_address)"source",
                                  (string_address)"ours") == 0);
        bipolar opened = directory < 0 ? directory : system_open_at(
            directory, (string_address)"source",
            FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar staged = opened < 0 ? opened : system_path_stage_opened_at(
            address_of stage, directory, (string_address)"source", opened);
        check("sticky-parent source enters protected staging", staged == 0);
        if (staged >= 0)
        {
                path_stage_copy_name(private_name, stage.private_name);
                check("sticky-parent protected removal succeeds",
                      system_path_stage_remove(address_of stage, 0) == 0);
                check("sticky-parent cleanup removes the exact empty stage",
                      !path_stage_exists(directory, private_name));
        }
        if (opened >= 0)
                system_close(opened);
        if (directory >= 0)
                system_close(directory);
        system_remove_at(root, (string_address)"sticky",
                         SYSTEM_PATH_AT_REMOVEDIR);
}

b32 main(void)
{
        p8 root[128];
        static const p8 prefix[] = "/tmp/moonwater-path-stage-";

        memory_copy_apart(root, prefix, sizeof(prefix) - 1);
        positive_into_string(root + sizeof(prefix) - 1, system_nonce());

        check("protected staging root is created",
              system_make_directory_exact_at(
                  AT_FDCWD, root, 0700) == 0);
        bipolar directory = system_open_at(
            AT_FDCWD, root,
            FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        check("protected staging root stays pinned", directory >= 0);
        check("an owner-private parent is cleanup-safe",
              directory >= 0 && system_path_parent_cleanup_safe(directory));
        if (directory >= 0)
        {
                path_stage_normal(directory);
                path_stage_restrictive_umask(directory);
                path_stage_mismatch(directory);
                path_stage_restore_collision(directory);
                path_stage_publish_retry(directory);
                path_stage_failed_remove(directory);
                path_stage_hostile_parent(directory);
                path_stage_sticky_parent(directory);
                system_close(directory);
        }
        system_remove_at(AT_FDCWD, root, SYSTEM_PATH_AT_REMOVEDIR);
        return test_report(null);
}
#endif /* CHECK_path_stage */

#ifdef CHECK_audit_builtin_regressions
/* Pure-state regressions for the verified enable/formatter/signal repairs.
   No machine-control builtin or utility is invoked. */
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

_Static_assert(SIGSTOP == 19, "Linux SIGSTOP must match the syscall ABI");
static p8 captured[8192];
static positive captured_used;
static fn capture(address_any data, positive length)
{
        if (!length) length = string_length(data);
        if (length < sizeof(captured) - captured_used)
        {
                memory_copy(captured + captured_used, data, length);
                captured_used += length;
                captured[captured_used] = 0;
        }
}
static fn enable_name(string_address name, p8 off)
{
        shell_argc = off ? 3 : 2;
        shell_argv[0] = "enable";
        shell_argv[1] = off ? (string_address)"-n" : name;
        shell_argv[2] = name;
        shell_enable(capture);
}

typedef struct { p16 code; p8 yes, no; p32 value; } audit_filter_instruction;
typedef struct { p16 length; audit_filter_instruction address_to instructions; }
    audit_filter_program;

static bipolar audit_refuse_randomness(void)
{
        audit_filter_instruction instructions[] = {
            {0x20, 0, 0, 0},
            {0x15, 0, 1, syscall(getrandom)},
            {0x06, 0, 0, 0x0005000b},
            {0x06, 0, 0, 0x7fff0000},
        };
        audit_filter_program program = {
            array_count(instructions), instructions};

        if (system_call_5(syscall(prctl), 38, 1, 0, 0, 0) < 0)
                return -1;
        return system_call_3(syscall(seccomp), 1, 0,
                             (positive)address_of program);
}
b32 main(void)
{
        string_address arguments[8] = {0};
        shell_argv = arguments;

        /* dash 0.5.13 lists an alias as one quoted word, 'mw_a=echo one',
           and bash as mw_a='echo one'; each reads back in its own shell.  A
           quote in the value is closed and double quoted as dash does it:
           'mw_q=it'"'"'s', never the unbalanced 'mw_q=it's'. */
        {
                static const struct
                {
                        string_address value;
                        p8 bash;
                        string_address listed;
                } aliases[] = {
                    {"echo one", false, "'mw_a=echo one'\n"},
                    {"it's", false, "'mw_a=it'\"'\"'s'\n"},
                    {"y''", false, "'mw_a=y'\"''\"\n"},
                    {"", false, "'mw_a='\n"},
                    {"echo one", true, "mw_a='echo one'\n"},
                    {"it's", true, "mw_a='it'\\''s'\n"},
                };

                for (positive at = 0; at < array_count(aliases); at++)
                {
                        shell_bash_compat = aliases[at].bash;
                        check("alias regression records",
                              alias_record((string_address)"mw_a", 4,
                                           aliases[at].value));
                        captured_used = 0;
                        captured[0] = end;
                        alias_written(capture, 0);
                        check("an alias lists as its reference shell lists it",
                              string_equals(captured, aliases[at].listed));
                }
                shell_bash_compat = true;
        }

        /* Spark's fast path must reject oversized vectors before reserving or
           copying their flat representation.  Otherwise fallback remains
           correct but a hostile expansion can force avoidable multi-megabyte
           (or, before the wire limits existed, multi-gigabyte) work first. */
        static p8 oversized[SPARK_SPAWN_MAX_BYTES + 1];
        static string_address crowded[SPARK_SPAWN_MAX_STRINGS + 2];
        p8 address_to flat = null;
        positive flat_room = 0;
        positive flat_count = 0;

        memory_fill(oversized, 'x', sizeof(oversized));
        oversized[sizeof(oversized) - 1] = 0;
        string_address too_large[] = {oversized, null};
        check("Spark argv byte ceiling rejects before allocation",
              !shell_flatten_strings(too_large, address_of flat,
                                     address_of flat_room,
                                     address_of flat_count) &&
                  flat_count == positive_max && !flat && !flat_room);

        for (positive i = 0; i <= SPARK_SPAWN_MAX_STRINGS; i++)
                crowded[i] = (string_address)"";
        check("Spark argv count ceiling rejects before allocation",
              !shell_flatten_strings(crowded, address_of flat,
                                     address_of flat_room,
                                     address_of flat_count) &&
                  flat_count == positive_max && !flat && !flat_room);

        for (positive cycle = 0; cycle < 1024; cycle++)
        {
                enable_name("echo", true);
                check("unbounded repeat disable", !shell_status && shell_builtin_disabled("echo"));
                enable_name("echo", false);
                check("unbounded repeat enable", !shell_status && !shell_disabled_count);
        }
        for (positive i = 0; i < SHELL_COMMAND_COUNT; i++)
        {
                enable_name(shell_commands[i].name, true);
                enable_name(shell_commands[i].name, true);
                check("all registry identities disable idempotently",
                      !shell_status && shell_disabled_count == i + 1 &&
                      !shell_command_named_hashed(shell_commands[i].name,
                          string_hash_33_length(shell_commands[i].name)));
        }
        for (positive i = 0; i < SHELL_COMMAND_COUNT; i++)
        {
                enable_name(shell_commands[i].name, false);
                enable_name(shell_commands[i].name, false);
                check("all registry identities enable idempotently",
                      !shell_status && shell_disabled_count == SHELL_COMMAND_COUNT - i - 1 &&
                      shell_command_named_hashed(shell_commands[i].name,
                          string_hash_33_length(shell_commands[i].name)) == shell_commands + i);
        }
        enable_name("complete", true);
        check("same-handler aliases retain independent state",
              shell_builtin_disabled("complete") && !shell_builtin_disabled("bind") &&
              !shell_builtin_disabled("true") && !shell_builtin_disabled(":"));
        for (positive mode = 0; mode < 4; mode++)
        {
                shell_argc = mode ? 2 : 1;
                shell_argv[0] = "enable";
                shell_argv[1] = mode == 1 ? "-n" : mode == 2 ? "-p" : "-a";
                captured_used = 0; captured[0] = 0;
                shell_enable(capture);
                check("listing selects disabled state", !!string_search(captured, "enable -n complete\n") == (mode == 1 || mode == 3));
                check("listing selects enabled state", !!string_search(captured, "enable bind\n") == (mode != 1));
        }
        enable_name("complete", false);
        const string_address names[] = {"bind", "complete", "compopt"};
        for (positive i = 0; i < array_count(names); i++)
        {
                shell_argc = 1; shell_argv[0] = names[i];
                shell_command address_to command = shell_command_named_hashed(
                    names[i], string_hash_33_length(names[i]));
                command->function(capture);
                check("constant-status handlers", shell_status == (i == 2));
        }
        static const struct {positive bytes; string_address text;} sizes[] = {
            {0, "0 B"}, {1, "1 B"}, {1024, "1 KiB"}, {8192, "8 KiB"},
            {1536, "1.5 KiB"}, {1048576, "1 MiB"}, {1073741824, "1 GiB"},
            {13107200, "12.5 MiB"}, {1023, "1023 B"},
        };
        utility_arena.used = 0;
        for (positive i = 0; i < array_count(sizes); i++)
                check("util-linux human size spelling",
                      string_equals(ul_lscpu_cache_size(sizes[i].bytes, false, true), sizes[i].text));

        {
                positive product = 0xa5a5;

                check("sort table products reject integer wrap before allocation",
                      !sort_count_product(positive_max, 2,
                                          address_of product) &&
                          product == 0xa5a5);
                check("sort table products retain the largest representable product",
                      sort_count_product(positive_max / 2, 2,
                                         address_of product) &&
                          product == (positive_max / 2) * 2);
        }

        /* nohup used to follow this public output name with the privileges
           of its caller.  Exercise the descriptor policy directly: neither
           a symlink nor another hard-link name for an existing inode may be
           opened, while a single regular output remains appendable. */
        {
                p8 root[96] = "/tmp/moonwater-nohup.";
                p8 victim[128];
                p8 output[128];
                positive used = string_length(root);

                used += positive_into(root + used,
                    (positive)system_call_1(syscall(getpid), 0));
                root[used] = end;
                path_join(victim, sizeof victim, root,
                          (string_address)"victim");
                path_join(output, sizeof output, root,
                          (string_address)"nohup.out");
                (void)system_remove_at(AT_FDCWD, output, 0);
                (void)system_remove_at(AT_FDCWD, victim, 0);
                (void)system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
                check("nohup security directory is private",
                      system_make_directory_exact_at(AT_FDCWD, root, 0700) == 0);
                bipolar made = system_open_at_mode(
                    AT_FDCWD, victim,
                    FILE_WRITE | FILE_CREATE | FILE_EXCLUSIVE | O_CLOEXEC,
                    0600);
                check("nohup security victim is created", made >= 0);
                if (made >= 0)
                        system_close((positive)made);

                check("nohup output symlink is planted",
                      system_symbolic_link_at(victim, AT_FDCWD, output) == 0);
                bipolar opened = process_nohup_output_open(output);
                check("nohup refuses a planted output symlink", opened < 0);
                if (opened >= 0)
                        system_close((positive)opened);
                (void)system_remove_at(AT_FDCWD, output, 0);

                check("nohup output hard link is planted",
                      system_link_at(AT_FDCWD, victim, AT_FDCWD, output, 0) == 0);
                opened = process_nohup_output_open(output);
                check("nohup refuses a multiply-linked output inode", opened < 0);
                if (opened >= 0)
                        system_close((positive)opened);
                (void)system_remove_at(AT_FDCWD, output, 0);

                opened = process_nohup_output_open(output);
                check("nohup accepts one regular output inode", opened >= 0);
                if (opened >= 0)
                        system_close((positive)opened);

                /* A single-link regular file is still hostile when another
                   account planted it in a directory the privileged caller
                   entered.  Ownership is checked through the descriptor,
                   after open, so a pathname swap cannot change this proof. */
                opened = system_open_at(
                    AT_FDCWD, output, FILE_READ_WRITE | O_CLOEXEC);
                p8 foreign = opened >= 0 &&
                    system_call_3(syscall(fchown), (positive)opened,
                                  65534, 65534) == 0;
                if (opened >= 0)
                        system_close((positive)opened);
                if (foreign)
                {
                        opened = process_nohup_output_open(output);
                        check("nohup refuses output owned by another account",
                              opened < 0);
                        if (opened >= 0)
                                system_close((positive)opened);
                }
                (void)system_remove_at(AT_FDCWD, output, 0);
                (void)system_remove_at(AT_FDCWD, victim, 0);
                (void)system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
        }

        /* A denied getrandom must fail before sort creates a guessable spill
           name.  The old pid/counter fallback made this child succeed. */
        {
                bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
                positive raw = 0;

                check("sort entropy-policy child starts", child >= 0);
                if (!child)
                {
                        if (audit_refuse_randomness() < 0)
                                system_call_1(syscall(exit_group), 77);
                        bipolar temporary = sort_temporary_named(
                            (string_address)"/tmp");
                        if (temporary >= 0)
                                system_close((positive)temporary);
                        system_call_1(syscall(exit_group),
                                      temporary < 0 ? 0 : 1);
                }
                if (child > 0)
                        check("sort refuses predictable temporary names when entropy fails",
                              system_wait4_retry((b32)child, address_of raw,
                                                 0, null) == child &&
                                  (wait_status_code(raw) == 0 ||
                                   wait_status_code(raw) == 77));
        }

        /* test used one call of its own stack per ( and per !, so a word
           list of a few hundred thousand of them ended the shell with a
           segmentation fault. The depth is cut and the expression refused
           with status 2. The child takes the crash, not this process. */
        {
                static string_address deep[300003];
                bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
                positive raw = 0;

                check("test: deep nesting child starts", child >= 0);
                if (!child)
                {
                        positive at;

                        deep[0] = (string_address)"[";
                        for (at = 1; at <= 300000; at++)
                                deep[at] = (string_address)"!";
                        deep[300001] = (string_address)"x";
                        deep[300002] = (string_address)"]";
                        shell_argv = deep;
                        shell_argc = 300003;
                        shell_test(capture);
                        system_call_1(syscall(exit_group),
                                      shell_status == 2 ? 0 : 1);
                }
                if (child > 0)
                        check("test refuses a nesting too deep for the stack with status 2",
                              system_wait4_retry((b32)child, address_of raw,
                                                 0, null) == child &&
                                  wait_status_code(raw) == 0);
        }
        /* set +m: the three terminal signals job_monitor_start took are given
           back to default, as a command's own stop keys must be. Each is asked
           of the kernel after the stop, and each was cached by
           shell_was_ignored first, as job_monitor_start caches it. */
        {
                positive disposition[4];
                bool all_default = true;
                b32 number;

                for (number = JOB_SIGNAL_STOP_KEY; number <= JOB_SIGNAL_TTY_OUTPUT;
                     number++)
                        (void)shell_was_ignored(number);
                for (number = JOB_SIGNAL_STOP_KEY; number <= JOB_SIGNAL_TTY_OUTPUT;
                     number++)
                        shell_ignore(number);
                job_signals_taken = true;
                job_monitor_stop();
                for (number = JOB_SIGNAL_STOP_KEY; number <= JOB_SIGNAL_TTY_OUTPUT;
                     number++)
                {
                        disposition[0] = 0x5a;
                        system_call_4(syscall(rt_sigaction), (positive)number, 0,
                                      (positive)disposition, 8);
                        if (disposition[0] != 0)
                                all_default = false;
                }
                check("set +m gives the monitor's terminal signals back to default",
                      all_default);
        }

        /* A trap on CHLD takes the child-watch handler's place, and the news
           it carries must still reach the mid-command notice. */
        {
                job_child_news = false;
                trap_signal_caught(SIGNAL_CHILD);
                check("a trapped CHLD raises the job-child news the notice reads",
                      job_child_news);
                trap_pending[SIGNAL_CHILD] = 0;
                trap_caught = false;
                job_child_news = false;
        }

        /* A row holds its child's pidfd from the moment it is recorded until it
           is done: a sweep leaves a running child alone, and reaps it, with its
           pidfd closed, once the child has exited. */
        {
                bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
                positive pause[2] = {0, 300000000};
                bipolar children[1];
                positive row;
                positive waited = 0;

                check("pidfd test child starts", child >= 0);
                if (!child)
                {
                        system_call_2(syscall(nanosleep), (positive)pause, 0);
                        system_call_1(syscall(exit_group), 0);
                }
                if (child <= 0)
                        return test_report(null);

                children[0] = child;
                row = shell_wait_count;
                check("background row is recorded with a pidfd",
                      shell_background_started(children, 1, false, false) &&
                          shell_wait_table[row].pidfd >= 0);
                job_reap();
                check("a sweep leaves a running child's row unfinished",
                      !(shell_wait_table[row].flags & SHELL_WAIT_DONE) &&
                          shell_wait_table[row].pidfd >= 0);
                while (waited++ < 40 &&
                       !(shell_wait_table[row].flags & SHELL_WAIT_DONE))
                {
                        system_call_2(syscall(nanosleep), (positive)pause, 0);
                        job_reap();
                }
                check("a sweep reaps an exited child and closes its pidfd",
                      (shell_wait_table[row].flags & SHELL_WAIT_DONE) &&
                          shell_wait_table[row].pidfd == -1);
        }

        /* Every redirection keeps one save, even one whose descriptor is not
           open, so a command with more than a thousand of them must run: the
           table that holds the saves grows rather than refusing. 9>&- saves
           nothing a descriptor is spent on. */
        {
                static p8 wide[8192];
                positive at = 2;

                memory_copy(wide, ": ", 2);
                for (positive i = 0; i < 1025; i++)
                {
                        memory_copy(wide + at, "9>&- ", 5);
                        at += 5;
                }
                wide[at] = 0;
                shell_status = 99;
                exec_run_nested((string_address)wide, true, 0);
                check("a command with 1025 redirections runs", shell_status == 0);
        }

        /* A confined shell reaps its orphans before an in-place exec, and it
           must read every direct child to do it: 1200 exited, unreaped children
           overflow the old 4 KiB list at any pid width. They are untracked, as
           an orphan from a finished command is, so the reap takes them all. */
        {
                timespec pause = {0, 1000000};
                positive forked = 0;

                floodlight_parent_supervised = true;
                for (; forked < 1200; forked++)
                {
                        bipolar child = system_fork();

                        if (child == 0)
                                system_call_1(syscall(exit), 0);
                        if (child < 0)
                                break;
                }
                check("1200 children forked", forked == 1200);
                for (positive tries = 0; tries < 4000 && floodlight_descendants_present(); tries++)
                {
                        exec_unknown_children_reap();
                        system_call_2(syscall(nanosleep), address_of pause, 0);
                }
                check("the reap takes every orphan past the old 4 KiB list",
                      !floodlight_descendants_present());
                check("no orphan declines an in-place exec",
                      !floodlight_descendants_blocking());
                floodlight_parent_supervised = false;
        }

        /* A value-form ${ } whose locals cannot be entered leaves REPLY as it
           was: the refusal comes before REPLY is held or cleared. */
        {
                b32 status = 0;
                positive depth = local_depth;

                env_assign("REPLY", "kept");
                local_depth = positive_max;
                bipolar handle = shell_funsub_run("true", true, address_of status);
                local_depth = depth;
                check("a forced local-depth failure refuses the value form", handle < 0);
                check("a forced local-depth failure leaves REPLY unchanged",
                      env_get("REPLY") && string_equals(env_get("REPLY"), "kept"));
        }

        /* SIGTERM while the editor holds the terminal raw: the modes it saved
           come back before the signal ends it, so the terminal is cooked
           again. A pty stands in for the terminal, and the child is the
           editor, which dies by the signal it was sent. */
        {
                b32 master = 0, slave = 0;
                bipolar opened = process_pty_open(address_of master, address_of slave, false);

                check("a pty stands in for the terminal", opened >= 0);
                if (opened >= 0)
                {
                        bipolar child = system_fork();

                        if (child == 0)
                        {
                                system_duplicate(slave, 0, 0);
                                if (!edit_terminal_raw())
                                        system_call_1(syscall(exit), 2);
                                system_call_2(syscall(kill),
                                              (positive)system_call_1(syscall(getpid), 0),
                                              SIGTERM);
                                system_call_1(syscall(exit), 3);
                        }

                        positive status = 0;
                        terminal_modes modes = {0};

                        (void)system_wait4_retry(child, address_of status, 0, null);
                        check("the editor ends by the SIGTERM it was sent", (status & 0x7f) == SIGTERM);
                        check("the terminal reads back after the editor ends",
                              system_control(slave, PTY_TCGETS, address_of modes) == 0);
                        check("the terminal is cooked again after SIGTERM",
                              (modes.behaviour & EDIT_LOCAL_CANONICAL) &&
                              (modes.behaviour & EDIT_LOCAL_ECHO));
                        system_close((positive)slave);
                        system_close((positive)master);
                }
        }

        return test_report(null);
}
#endif /* CHECK_audit_builtin_regressions */

#ifdef CHECK_slurp
#include "../../src/lib.util.c"

/*
        file_slurp's contract at the syscall boundary.

        Ordinary files pin every capacity edge and the guard bytes around an
        unaligned destination. Two FIFO runs force the two EINTR paths
        separately: the first signal arrives while openat is waiting for a
        writer; the second writer is already open and holds the pipe empty
        while read waits. Neither test relies on SA_RESTART -- x86_64 supplies
        its required rt_sigreturn trampoline explicitly, while arm64 and
        riscv64 use the kernel-provided return path.
*/

#ifndef LINUX
#error "file_slurp is a Linux userspace primitive"
#endif

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static volatile positive signals_caught;

#define SLURP_TEST_SIGNAL 10
#define SLURP_TEST_RESTORER 0x04000000
#define SLURP_TEST_FIFO_MODE (0010000 | 0600)

#define SLURP_TEST_TEXT_INNER(value) #value
#define SLURP_TEST_TEXT(value) SLURP_TEST_TEXT_INNER(value)

#if X64
__asm__(
    ASM_SECTION
    ASM_FUNC(slurp_test_signal_return)
    "mov $" SLURP_TEST_TEXT(syscall(rt_sigreturn)) ", %eax\n   syscall\n"
    ASM_END(slurp_test_signal_return)
);

fn slurp_test_signal_return(void);
#define SLURP_TEST_SIGNAL_FLAGS SLURP_TEST_RESTORER
#define SLURP_TEST_SIGNAL_RETURN ((positive)slurp_test_signal_return)
#else
#define SLURP_TEST_SIGNAL_FLAGS 0
#define SLURP_TEST_SIGNAL_RETURN 0
#endif

static fn slurp_test_signal_caught(b32 number)
{
        (void)number;
        signals_caught++;
}

static fn slurp_test_path(p8 address_to into, string_address suffix)
{
        static const string_address prefix =
              (string_address) "/tmp/moonwater-slurp-";
        positive at = string_length(prefix);
        positive suffix_length = string_length(suffix);

        memory_copy(into, prefix, at);
        at += positive_into_string(into + at,
                                   (positive)system_call(syscall(getpid)));
        memory_copy(into + at, suffix, suffix_length + 1);
}

static fn slurp_test_unlink(string_address path)
{
        system_call_3(syscall(unlinkat), (positive)(bipolar)AT_FDCWD,
                      (positive)path, 0);
}

static p8 slurp_test_file(string_address path, address_any data, positive length)
{
        slurp_test_unlink(path);

        bipolar handle = system_call_4(syscall(openat),
                                       (positive)(bipolar)AT_FDCWD,
                                       (positive)path, FILE_WRITE, 0600);

        if (handle < 0)
                return false;

        positive written = system_write_all((positive)handle, data, length);
        system_call_1(syscall(close), (positive)handle);
        return written == length;
}

static p8 slurp_test_guards(p8 address_to room, positive size,
                              positive at, positive capacity)
{
        for (positive i = 0; i < at; i++)
                if (room[i] != 0xa5)
                        return false;

        for (positive i = at + capacity; i < size; i++)
                if (room[i] != 0xa5)
                        return false;

        return true;
}

static p8 slurp_test_all(p8 address_to room, positive size, p8 value)
{
        for (positive i = 0; i < size; i++)
                if (room[i] != value)
                        return false;

        return true;
}

static fn slurp_test_nap(void)
{
        positive duration[2] = {0, 50000000};
        system_call_2(syscall(nanosleep), (positive)duration, 0);
}

static fn slurp_test_fifo_child(string_address path, positive parent,
                                p8 signal_before_open,
                                address_any data, positive length)
{
        bipolar handle;

        if (signal_before_open)
        {
                slurp_test_nap();
                system_call_2(syscall(kill), parent, SLURP_TEST_SIGNAL);
                slurp_test_nap();
        }

        handle = system_call_4(syscall(openat), (positive)(bipolar)AT_FDCWD,
                               (positive)path, 1, 0);

        if (handle < 0)
                exit(91);

        if (!signal_before_open)
        {
                slurp_test_nap();
                system_call_2(syscall(kill), parent, SLURP_TEST_SIGNAL);
                slurp_test_nap();
        }

        positive written = system_write_all((positive)handle, data, length);
        system_call_1(syscall(close), (positive)handle);
        exit(written == length ? 0 : 92);
}

static fn slurp_test_fifo(string_address path, p8 signal_before_open)
{
        static p8 payload[] = "EINTR survived";
        p8 room[64];
        positive state = 0;
        positive parent = (positive)system_call(syscall(getpid));

        slurp_test_unlink(path);
        bipolar made = system_call_4(syscall(mknodat),
                                     (positive)(bipolar)AT_FDCWD,
                                     (positive)path, SLURP_TEST_FIFO_MODE, 0);
        check("FIFO made", made == 0);

        if (made < 0)
                return;

        signals_caught = 0;
        bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);

        if (child == 0)
                slurp_test_fifo_child(path, parent, signal_before_open,
                                      payload, sizeof payload - 1);

        check("FIFO child made", child > 0);

        if (child > 0)
        {
                memory_fill(room, 0xa5, sizeof room);
                bipolar got;

                if (signal_before_open)
                        got = file_slurp(path, room + 3, sizeof room - 6);
                else
                {
                        bipolar handle = system_call_4(
                              syscall(openat), (positive)(bipolar)AT_FDCWD,
                              (positive)path, FILE_READ, 0);

                        check("retry primitive FIFO opened", handle >= 0);

                        if (handle >= 0)
                        {
                                got = system_read_retry((positive)handle, room + 3,
                                                        sizeof payload - 1);
                                system_call_1(syscall(close), (positive)handle);
                        }
                        else
                        {
                                got = handle;
                                system_call_2(syscall(kill), (positive)child, 9);
                        }
                }

                system_call_4(syscall(wait4), (positive)child,
                              (positive)address_of state, 0, 0);

                check("FIFO signal caught", signals_caught != 0);
                check("FIFO child succeeded", ((state >> 8) & 0xff) == 0);
                check("FIFO slurp length", got == sizeof payload - 1);
                check("FIFO slurp bytes",
                      !memory_compare(room + 3, payload, sizeof payload - 1));

                if (signal_before_open)
                {
                        check("FIFO slurp terminator",
                              room[3 + sizeof payload - 1] == 0);
                        check("FIFO slurp guards",
                              slurp_test_guards(room, sizeof room, 3,
                                                sizeof room - 6));
                }
                else
                        check("retry primitive guards",
                              slurp_test_guards(room, sizeof room, 3,
                                                sizeof payload - 1));
        }

        slurp_test_unlink(path);
}

static fn slurp_test_regular(string_address path, string_address empty,
                             string_address absent)
{
        static p8 contents[] = "hardware floor slurp";
        p8 room[96];
        positive length = sizeof contents - 1;
        bipolar got;

        check("fixture made", slurp_test_file(path, contents, length));
        check("empty fixture made", slurp_test_file(empty, contents, 0));
        slurp_test_unlink(absent);

        memory_fill(room, 0xa5, sizeof room);
        got = system_read_retry((positive)(bipolar)-1, room + 4, 8);
        check("retry primitive preserves error", got == -9);
        check("retry primitive error leaves destination",
              slurp_test_all(room, sizeof room, 0xa5));

        bipolar handle = system_call_4(syscall(openat),
                                       (positive)(bipolar)AT_FDCWD,
                                       (positive)path, FILE_READ, 0);
        check("retry primitive regular opened", handle >= 0);

        if (handle >= 0)
        {
                memory_fill(room, 0xa5, sizeof room);
                got = system_read_retry((positive)handle, null, 0);
                check("retry primitive preserves zero count", got == 0);
                check("retry primitive zero count leaves destination",
                      slurp_test_all(room, sizeof room, 0xa5));
                system_call_1(syscall(close), (positive)handle);
        }

        handle = system_call_4(syscall(openat),
                               (positive)(bipolar)AT_FDCWD,
                               (positive)empty, FILE_READ, 0);
        check("retry primitive empty opened", handle >= 0);

        if (handle >= 0)
        {
                memory_fill(room, 0xa5, sizeof room);
                got = system_read_retry((positive)handle, room + 3, 8);
                check("retry primitive preserves EOF", got == 0);
                check("retry primitive EOF leaves destination",
                      slurp_test_all(room, sizeof room, 0xa5));
                system_call_1(syscall(close), (positive)handle);
        }

        // Zero capacity is before both pointer use and openat. A null path
        // would have returned EFAULT if the syscall had happened.
        check("capacity zero accepts nulls", file_slurp(null, null, 0) == 0);
        memory_fill(room, 0xa5, sizeof room);
        check("capacity zero skips open", file_slurp(absent, room + 5, 0) == 0);
        check("capacity zero leaves destination",
              slurp_test_all(room, sizeof room, 0xa5));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(absent, room + 5, 1);
        check("capacity one still opens", got == -2);
        check("capacity one open error leaves destination",
              slurp_test_all(room, sizeof room, 0xa5));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(path, room + 5, 1);
        check("capacity one length", got == 0);
        check("capacity one terminator", room[5] == 0);
        check("capacity one guards",
              slurp_test_guards(room, sizeof room, 5, 1));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(path, room + 3, 6);
        check("truncated length", got == 5);
        check("truncated bytes", !memory_compare(room + 3, contents, 5));
        check("truncated terminator", room[8] == 0);
        check("truncated guards",
              slurp_test_guards(room, sizeof room, 3, 6));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(path, room + 7, length + 1);
        check("exact capacity length", got == (bipolar)length);
        check("exact capacity bytes",
              !memory_compare(room + 7, contents, length));
        check("exact capacity terminator", room[7 + length] == 0);
        check("exact capacity guards",
              slurp_test_guards(room, sizeof room, 7, length + 1));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(path, room + 1, length + 8);
        check("EOF length", got == (bipolar)length);
        check("EOF bytes", !memory_compare(room + 1, contents, length));
        check("EOF terminator", room[1 + length] == 0);
        check("EOF guards",
              slurp_test_guards(room, sizeof room, 1, length + 8));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(empty, room + 9, 8);
        check("empty length", got == 0);
        check("empty terminator", room[9] == 0);
        check("empty guards", slurp_test_guards(room, sizeof room, 9, 8));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp(absent, room + 4, 8);
        check("open error", got == -2);
        check("open error leaves destination",
              slurp_test_all(room, sizeof room, 0xa5));

        memory_fill(room, 0xa5, sizeof room);
        got = file_slurp((string_address) "/tmp", room + 5, 8);
        check("directory read error", got == -21);
        check("directory error terminator", room[5] == 0);
        check("directory error guards",
              slurp_test_guards(room, sizeof room, 5, 8));

        slurp_test_unlink(path);
        slurp_test_unlink(empty);
}

static fn slurp_test_store(string_address path)
{
        byte_store store = {0};
        p8 payload[9000];
        memory_fill(payload, 'x', sizeof(payload));
        check("store fixture made", slurp_test_file(path, payload, sizeof(payload)));
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ);
        check("store reads through growth and EOF",
              handle >= 0 && file_store_read(handle, &store) == 0 &&
              store.used == sizeof(payload) && !store.bytes[store.used] &&
              !memory_compare(store.bytes, payload, sizeof(payload)));
        check("store keeps its descriptor open", system_seek(handle, 0, 0) == 0);
        check("store reuses its allocation", file_store_read(handle, &store) == 0 &&
              store.used == sizeof(payload));
        check("store resets at EOF", file_store_read(handle, &store) == 0 &&
              !store.used && !store.bytes[0]);
        system_close(handle);
        check("store preserves raw read errors", file_store_read(handle, &store) == -EBADF);
        byte_store_release(&store);
        slurp_test_unlink(path);
}

b32 main(void)
{
        p8 regular[128], empty[128], absent[128], open_fifo[128], read_fifo[128];
        positive action[4] = {(positive)slurp_test_signal_caught,
                              SLURP_TEST_SIGNAL_FLAGS,
                              SLURP_TEST_SIGNAL_RETURN, 0};

        slurp_test_path(regular, (string_address) "-regular");
        slurp_test_path(empty, (string_address) "-empty");
        slurp_test_path(absent, (string_address) "-absent");
        slurp_test_path(open_fifo, (string_address) "-open-fifo");
        slurp_test_path(read_fifo, (string_address) "-read-fifo");

        check("signal action installed",
              system_call_4(syscall(rt_sigaction), SLURP_TEST_SIGNAL,
                            (positive)action, 0, 8) == 0);

        slurp_test_regular(regular, empty, absent);
        slurp_test_store(regular);
        slurp_test_fifo(open_fifo, true);
        slurp_test_fifo(read_fifo, false);

        //      Taking nothing from a null reader is not a failure. skip read
        //      the null that take gives back for that as a failed read, so the
        //      reader that a zero-length window hands on reported a failure.
        {
                byte_reader empty_reader = byte_reader_open(null, 0);

                check("skipping nothing from a null reader succeeds",
                      byte_reader_skip(address_of empty_reader, 0) && !empty_reader.failed);
        }

        return test_report(null);
}

#undef SLURP_TEST_TEXT
#undef SLURP_TEST_TEXT_INNER
#endif /* CHECK_slurp */

#ifdef CHECK_wait_retry
#include "../../src/lib.util.c"

/*
        The wait4 retry leaf at the syscall boundary.

        A child reports the pid it is about to wait for, then has no blocking
        operation except wait4. Its parent polls /proc/<pid>/stat until that
        waiter is observably asleep, sends a non-restarting signal, and waits
        for the handler's pipe acknowledgement before releasing the worker.
        Scheduling luck before wait4 therefore cannot satisfy the assertions.

        Supplying an unaligned rusage block proves the fourth argument survived
        that retry (and, on x86_64, reached r10). A second child pins WNOHANG
        before it is collected normally, and waiting twice for the worker pins
        every non-EINTR error unchanged.
*/

#ifndef LINUX
#error "system_wait4_retry is a Linux userspace primitive"
#endif

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static volatile positive signals_caught;
static bipolar wait_retry_signal_report = -1;

#define WAIT_RETRY_SIGNAL 10
#define WAIT_RETRY_RESTORER 0x04000000
#define WAIT_RETRY_USAGE 144
#define WAIT_RETRY_NO_HANG 1
#define WAIT_RETRY_REPORT_ALL 0x3f

#define WAIT_RETRY_TEXT_INNER(value) #value
#define WAIT_RETRY_TEXT(value) WAIT_RETRY_TEXT_INNER(value)

#if X64
__asm__(
    ASM_SECTION
    ASM_FUNC(wait_retry_signal_return)
    "mov $" WAIT_RETRY_TEXT(syscall(rt_sigreturn)) ", %eax\n   syscall\n"
    ASM_END(wait_retry_signal_return)
);

fn wait_retry_signal_return(void);
#define WAIT_RETRY_SIGNAL_FLAGS WAIT_RETRY_RESTORER
#define WAIT_RETRY_SIGNAL_RETURN ((positive)wait_retry_signal_return)
#else
#define WAIT_RETRY_SIGNAL_FLAGS 0
#define WAIT_RETRY_SIGNAL_RETURN 0
#endif

static fn wait_retry_signal_caught(b32 number)
{
        p8 report = 0x5a;

        (void)number;
        signals_caught++;

        if (wait_retry_signal_report >= 0)
                system_call_3(syscall(write),
                              (positive)wait_retry_signal_report,
                              (positive)address_of report, 1);
}

static fn wait_retry_nap(void)
{
        positive duration[2] = {0, 50000000};
        system_call_2(syscall(nanosleep), (positive)duration, 0);
}

static fn wait_retry_poll_nap(void)
{
        positive duration[2] = {0, 1000000};
        system_call_2(syscall(nanosleep), (positive)duration, 0);
}

static fn wait_retry_child(bipolar read_end, bipolar write_end, b32 status)
{
        p8 release = 0;

        system_call_1(syscall(close), (positive)write_end);
        system_read_retry((positive)read_end, address_of release, 1);
        system_call_1(syscall(close), (positive)read_end);

        exit(status);
}

static fn wait_retry_proc_path(p8 address_to into, bipolar process)
{
        static p8 prefix[] = "/proc/";
        static p8 suffix[] = "/stat";
        positive at = sizeof prefix - 1;

        memory_copy(into, prefix, at);
        at += positive_into_string(into + at, (positive)process);
        memory_copy(into + at, suffix, sizeof suffix);
}

static p8 wait_retry_observe_wait(bipolar process)
{
        p8 path[64], state[512];

        wait_retry_proc_path(path, process);

        for (positive attempt = 0; attempt < 2000; attempt++)
        {
                bipolar length = file_slurp(path, state, sizeof state);

                if (length > 3)
                {
                        bipolar close = -1;

                        for (bipolar i = 0; i < length; i++)
                                if (state[i] == ')')
                                        close = i;

                        if (close >= 0 && close + 2 < length && state[close + 2] == 'S')
                                return true;
                }

                wait_retry_poll_nap();
        }

        return false;
}

static p8 wait_retry_usage_changed(p8 address_to room)
{
        for (positive i = 3; i < 3 + WAIT_RETRY_USAGE; i++)
                if (room[i] != 0xa5)
                        return true;

        return false;
}

static p8 wait_retry_usage_guards(p8 address_to room, positive size)
{
        for (positive i = 0; i < 3; i++)
                if (room[i] != 0xa5)
                        return false;

        for (positive i = 3 + WAIT_RETRY_USAGE; i < size; i++)
                if (room[i] != 0xa5)
                        return false;

        return true;
}

static fn wait_retry_worker(bipolar read_end, bipolar write_end)
{
        system_call_1(syscall(close), (positive)read_end);
        system_call_1(syscall(close), (positive)write_end);

        for (;;)
                wait_retry_nap();
}

static fn wait_retry_waiter(bipolar read_end, bipolar write_end)
{
        p8 usage[WAIT_RETRY_USAGE + 16];
        positive status = 0;
        positive report = 0;

        system_call_1(syscall(close), (positive)read_end);
        memory_fill(usage, 0xa5, sizeof usage);
        signals_caught = 0;
        wait_retry_signal_report = write_end;

        bipolar worker = system_call_2(syscall(clone), SIGCHLD, 0);

        if (worker == 0)
                wait_retry_worker(read_end, write_end);

        system_write_all((positive)write_end, address_of worker, sizeof worker);

        if (worker > 0)
        {
                bipolar got = system_wait4_retry(worker, address_of status, 0,
                                                 usage + 3);

                if (got == worker)
                        report |= 1;

                if (signals_caught)
                        report |= 2;

                if ((status & 0x7f) == 9)
                        report |= 4;

                if (wait_retry_usage_changed(usage))
                        report |= 8;

                if (wait_retry_usage_guards(usage, sizeof usage))
                        report |= 16;

                if (system_wait4_retry(worker, address_of status, 0, null) == -10)
                        report |= 32;
        }

        system_write_all((positive)write_end, address_of report, sizeof report);
        system_call_1(syscall(close), (positive)write_end);
        exit(report == WAIT_RETRY_REPORT_ALL ? 0 : 90);
}

static fn wait_retry_interrupted()
{
        b32 channel[2] = {-1, -1};
        bipolar worker = -1;
        positive report = 0;
        positive status = 0;

        check("interrupt channel made",
              system_call_2(syscall(pipe2), (positive)channel, 0) == 0);

        if (channel[0] < 0)
                return;

        bipolar waiter = system_call_2(syscall(clone), SIGCHLD, 0);

        if (waiter == 0)
                wait_retry_waiter(channel[0], channel[1]);

        system_call_1(syscall(close), (positive)channel[1]);
        check("interrupt waiter made", waiter > 0);

        if (waiter > 0)
        {
                check("worker pid reported",
                      system_read_retry((positive)channel[0], address_of worker,
                                        sizeof worker) == sizeof worker && worker > 0);

                p8 observed = worker > 0 && wait_retry_observe_wait(waiter);
                check("wait4 observed before signal", observed);

                if (observed)
                {
                        p8 acknowledged = 0;

                        check("waiter signal sent",
                              system_call_2(syscall(kill), (positive)waiter,
                                            WAIT_RETRY_SIGNAL) == 0);
                        check("interrupt handler acknowledged",
                              system_read_retry((positive)channel[0],
                                                address_of acknowledged, 1) == 1 &&
                                    acknowledged == 0x5a);
                        check("worker release sent",
                              system_call_2(syscall(kill), (positive)worker, 9) == 0);
                }
                else
                {
                        system_call_2(syscall(kill), (positive)waiter, 9);

                        if (worker > 0)
                                system_call_2(syscall(kill), (positive)worker, 9);
                }

                check("waiter report returned",
                      system_read_retry((positive)channel[0], address_of report,
                                        sizeof report) == sizeof report);
                check("interrupted wait returns child", report & 1);
                check("interrupt handler ran", report & 2);
                check("interrupted wait status", report & 4);
                check("retry preserves rusage", report & 8);
                check("rusage guards", report & 16);
                check("non-EINTR error preserved", report & 32);
                check("waiter succeeded",
                      system_wait4_retry(waiter, address_of status, 0, null) == waiter &&
                            wait_status_code(status) == 0);
        }

        system_call_1(syscall(close), (positive)channel[0]);
}

static fn wait_retry_nohang()
{
        b32 channel[2] = {-1, -1};
        p8 release = 1;
        positive status = 0;

        check("nohang channel made",
              system_call_2(syscall(pipe2), (positive)channel, 0) == 0);

        if (channel[0] < 0)
                return;

        bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);

        if (child == 0)
                wait_retry_child(channel[0], channel[1], 19);

        system_call_1(syscall(close), (positive)channel[0]);

        check("nohang child made", child > 0);

        if (child > 0)
        {
                check("WNOHANG forwarded",
                      system_wait4_retry(child, address_of status,
                                         WAIT_RETRY_NO_HANG, null) == 0);
                check("nohang child released",
                      system_write_all((positive)channel[1],
                                       address_of release, 1) == 1);
                check("nohang child collected",
                      system_wait4_retry(child, address_of status, 0, null) == child);
                check("nohang child status", wait_status_code(status) == 19);
        }

        system_call_1(syscall(close), (positive)channel[1]);
}

b32 main(void)
{
        positive action[4] = {(positive)wait_retry_signal_caught,
                              WAIT_RETRY_SIGNAL_FLAGS,
                              WAIT_RETRY_SIGNAL_RETURN, 0};

        check("signal action installed",
              system_call_4(syscall(rt_sigaction), WAIT_RETRY_SIGNAL,
                            (positive)action, 0, 8) == 0);

        wait_retry_interrupted();
        wait_retry_nohang();

        return test_report(null);
}

#undef WAIT_RETRY_TEXT
#undef WAIT_RETRY_TEXT_INNER
#endif /* CHECK_wait_retry */

#ifdef CHECK_writer_buffer
#include "../../src/lib.util.c"

/* The shared buffered-writer policy, including both exact-capacity modes. */

#ifndef LINUX
#error "writer-buffer syscall behavior is tested on Linux"
#endif

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 read_exact(positive handle, p8 address_to into, positive want)
{
        positive have = 0;

        while (have < want)
        {
                bipolar got = system_read_retry(handle, into + have, want - have);

                if (got <= 0)
                        return false;

                have += (positive)got;
        }

        return true;
}

static p8 bytes_are(p8 address_to got, string_address want, positive length)
{
        return !memory_compare(got, want, length);
}

b32 main(void)
{
        b32 ends[2];
        p8 guarded[18];
        p8 address_to buffer = guarded + 1;
        p8 address_to reserved;
        p8 received[64];
        p8 exact[16] = "0123456789abcdef";
        p8 large[17] = "ABCDEFGHIJKLMNOPQ";
        positive used = 0;
        bipolar saved_output;
        p8 deferred_not_failed;
        p8 flush_failed;
        p8 direct_failed;
        p8 large_failed;
        p8 failure_stuck;

        check("pipe made", system_call_2(syscall(pipe2), (positive)ends, 0) == 0);
        memory_fill(guarded, 0xa5, sizeof guarded);

        check("zero length succeeds",
              buffered_write((positive)ends[1], buffer, 16, address_of used,
                             (address_any)"ignored", 0));
        check("zero length is a no-op", used == 0 && guarded[0] == 0xa5 &&
                                                guarded[17] == 0xa5);

        reserved = buffered_reserve((positive)ends[1], buffer, 16,
                                    address_of used, 3);
        check("reserve answers its span", reserved == buffer && used == 3);
        memory_copy(reserved, "abc", 3);
        reserved = buffered_reserve((positive)ends[1], buffer, 16,
                                    address_of used, 13);
        check("reserve exact remainder stays buffered",
              reserved == buffer + 3 && used == 16);
        memory_copy(reserved, "0123456789abc", 13);
        reserved = buffered_reserve((positive)ends[1], buffer, 16,
                                    address_of used, 1);
        check("reserve flushes before a new span", reserved == buffer && used == 1);
        reserved[0] = '!';
        check("reserved full buffer arrived",
              read_exact((positive)ends[0], received, 16) &&
                  bytes_are(received, "abc0123456789abc", 16));
        check("reserved tail flush succeeds",
              buffered_flush((positive)ends[1], buffer, address_of used));
        check("reserved tail arrived",
              read_exact((positive)ends[0], received, 1) && received[0] == '!');

        used = 3;
        memory_copy(buffer, "pre", 3);
        check("over-capacity reserve is refused",
              !buffered_reserve((positive)ends[1], buffer, 16,
                                address_of used, 17));
        check("refused reserve leaves pending bytes", used == 3 &&
                                                        bytes_are(buffer, "pre", 3));
        check("pending bytes after refusal flush",
              buffered_flush((positive)ends[1], buffer, address_of used) &&
                  read_exact((positive)ends[0], received, 3) &&
                  bytes_are(received, "pre", 3));

        check("buffered run succeeds",
              buffered_write((positive)ends[1], buffer, 16, address_of used,
                             (address_any)"abc", 3));
        check("small write is buffered", used == 3 && bytes_are(buffer, "abc", 3));

        check("exact fill succeeds",
              buffered_write((positive)ends[1], buffer, 16, address_of used,
                             exact, 13));
        check("combined exact fill stays buffered", used == 16);
        check("combined exact bytes", bytes_are(buffer, "abc0123456789abc", 16));
        check("buffer guards survive", guarded[0] == 0xa5 && guarded[17] == 0xa5);

        check("byte after full buffer succeeds",
              buffered_write_byte((positive)ends[1], buffer, 16,
                                  address_of used, '!'));
        check("byte flushes a full buffer", used == 1 && buffer[0] == '!');
        check("full buffer arrived", read_exact((positive)ends[0], received, 16) &&
                                            bytes_are(received, "abc0123456789abc", 16));

        check("flush succeeds",
              buffered_flush((positive)ends[1], buffer, address_of used));
        check("flush clears pending count", used == 0);
        check("pending byte arrived", read_exact((positive)ends[0], received, 1) &&
                                          received[0] == '!');

        check("direct exact succeeds",
              buffered_write((positive)ends[1], buffer, 16, address_of used,
                             exact, 16));
        check("at-capacity policy writes directly", used == 0);
        check("direct exact bytes", read_exact((positive)ends[0], received, 16) &&
                                       bytes_are(received, exact, 16));

        check("deferred exact succeeds",
              buffered_write_deferred_equal((positive)ends[1], buffer, 16,
                                            address_of used, exact, 16));
        check("hold-equal policy buffers exact", used == 16 &&
                                                  bytes_are(buffer, exact, 16));
        check("deferred flush succeeds",
              buffered_flush((positive)ends[1], buffer, address_of used));
        check("held exact bytes", read_exact((positive)ends[0], received, 16) &&
                                     bytes_are(received, exact, 16));

        check("direct prefix buffers",
              buffered_write((positive)ends[1], buffer, 16, address_of used,
                             (address_any)"pre", 3));
        check("flush then direct succeeds",
              buffered_write((positive)ends[1], buffer, 16, address_of used,
                             large, 17));
        check("over-capacity write leaves no pending bytes", used == 0);
        check("pending precedes direct bytes",
              read_exact((positive)ends[0], received, 20) &&
                  bytes_are(received, "preABCDEFGHIJKLMNOPQ", 20));

        check("unbuffered byte succeeds",
              buffered_write_byte((positive)ends[1], buffer, 0,
                                  address_of used, '?'));
        check("zero-capacity byte is unbuffered", used == 0 &&
                                                  read_exact((positive)ends[0], received, 1) &&
                                                  received[0] == '?');

        check("unbuffered run succeeds",
              buffered_write_deferred_equal((positive)ends[1], buffer, 0,
                                            address_of used,
                                            (address_any)"xy", 2));
        check("zero-capacity run is unbuffered", used == 0 &&
                                                 read_exact((positive)ends[0], received, 2) &&
                                                 bytes_are(received, "xy", 2));

        used = 3;
        memory_copy(buffer, "bad", 3);
        check("failed flush is reported",
              !buffered_flush((positive)-1, buffer, address_of used));
        check("failed flush still clears pending state", used == 0);

        used = 16;
        memory_copy(buffer, exact, 16);
        check("failed reserve flush is reported",
              !buffered_reserve((positive)-1, buffer, 16,
                                address_of used, 1));
        check("failed reserve clears pending state", used == 0);

        check("failed direct run is reported",
              !buffered_write((positive)-1, buffer, 0, address_of used,
                              (address_any)"x", 1));
        check("failed direct byte is reported",
              !buffered_write_byte((positive)-1, buffer, 0,
                                   address_of used, 'x'));

        used = 3;
        memory_copy(buffer, "bad", 3);
        check("failed prefix flush stops a direct run",
              !buffered_write((positive)-1, buffer, 16, address_of used,
                              large, 17));
        check("failed prefix is discarded", used == 0);

        used = 16;
        memory_copy(buffer, exact, 16);
        check("failed full flush stops a byte",
              !buffered_write_byte((positive)-1, buffer, 16,
                                   address_of used, 'x'));
        check("failed full buffer is discarded", used == 0);
        check("final guards survive", guarded[0] == 0xa5 && guarded[17] == 0xa5);

        /*
                The process logger has a void writer ABI.  Its separate
                sticky bit carries an output error across that ABI until a
                command boundary consumes it.  A pipe's read end is a
                deterministic unwritable stdout on every Linux architecture.
        */
        log_flush();
        saved_output = system_call_1(syscall(dup), 1);
        check("stdout saved for logger failure checks", saved_output >= 0);
        check("unwritable stdout installed",
              system_call_3(syscall(dup3), (positive)ends[0], 1, 0) == 1);

        log_failure_reset();
        log("x", 1);
        deferred_not_failed = !log_failed();
        log_flush();
        flush_failed = log_failed() && log_writer_buffer_length == 0;

        log_failure_reset();
        log_direct("x", 1);
        direct_failed = log_failed();

        log_failure_reset();
        log(log_writer_buffer, MAX_INPUT);
        large_failed = log_failed() && log_writer_buffer_length == 0;
        failure_stuck = log_failed() && log_failed();

        if (saved_output >= 0)
                system_call_3(syscall(dup3), (positive)saved_output, 1, 0);
        log_failure_reset();

        check("buffered logger defers failure until flush", deferred_not_failed);
        check("logger flush records failure and clears pending bytes", flush_failed);
        check("direct logger records failure", direct_failed);
        check("capacity-sized logger write records immediate failure", large_failed);
        check("logger failure is sticky until reset", failure_stuck);
        check("logger reset clears failure", !log_failed());

        if (saved_output >= 0)
                system_call_1(syscall(close), (positive)saved_output);

        system_call_1(syscall(close), (positive)ends[0]);
        system_call_1(syscall(close), (positive)ends[1]);

        return test_report(null);
}
#endif /* CHECK_writer_buffer */

