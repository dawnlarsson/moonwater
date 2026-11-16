/*
        test/checks/numbers.c: hex, codecs, fixed point, tables, errors, strings, stdlib, leaving, numbers, clocks,
        math, matrices, modulo, the trainer, the ranged routines and the environment.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_hex
#include "../../src/lib.util.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 escape_output[2048];
static positive escape_used, escape_calls;
static p8 escape_overflow;

static fn escape_capture(address_any data, positive length)
{
        escape_calls++;
        if (length > sizeof(escape_output) - escape_used)
                escape_overflow = true;
        else
        {
                memory_copy(escape_output + escape_used, data, length);
                escape_used += length;
        }
}

static positive escape_oracle(p8 address_to into, p8 byte, positive policy)
{
        p8 selected = policy == 64 ? byte < 32 || byte == '"' || byte == '\\' :
            (byte < 32 && byte != 9 && (policy & 1)) ||
            (byte == 127 && (policy & 1)) || (byte == 9 && (policy & 2)) ||
            (byte == 32 && (policy & 4)) || (byte == 34 && (policy & 8)) ||
            (byte == 92 && (policy & 16)) || (byte >= 128 && (policy & 32));
        if (!selected)
        {
                into[0] = byte;
                return 1;
        }
        into[0] = '\\';
        if (policy == 64 && byte >= 32)
        {
                into[1] = byte;
                return 2;
        }
        positive size = policy == 64 ? 6 : 4;
        into[1] = policy == 64 ? 'u' : 'x';
        into[2] = into[3] = '0';
        for (positive at = 0; at < 2; at++)
        {
                p8 nibble = (byte >> (at ? 0 : 4)) & 15;
                into[size - 2 + at] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        }
        return size;
}

static fn escape_full_check(p8 address_to input, positive length, positive policy)
{
        p8 output[208], expected[208];
        memory_fill(output, 0xa5, sizeof(output));
        memory_fill(expected, 0xa5, sizeof(expected));
        positive written = 0;
        for (positive at = 0; at < length; at++)
                written += escape_oracle(expected + 8 + written, input[at], policy);
        positive2 result = memory_into_escaped(output + 8, input, length, length * 6, policy);
        check("escape complete short vector with exact scratch canaries",
              result.x == length && result.y == written &&
              !memory_compare(output, expected, sizeof(output)));
}

static fn escape_span_check(p8 address_to input, positive length)
{
        for (positive policy = 0; policy <= 64; policy++)
        {
                positive first = length;
                p8 encoded[6];
                for (positive at = 0; at < length; at++)
                        if (escape_oracle(encoded, input[at], policy) != 1)
                        {
                                first = at;
                                break;
                        }
                check("escape bounded first index",
                      memory_escape_index(input, length, policy) == first);
                for (positive mode = 0; mode < 2; mode++)
                for (positive capacity = 0; capacity <= 80; capacity++)
                {
                        p8 output[96], expected[96];
                        positive read = 0, written = 0;
                        memory_fill(output, 0xa5, sizeof(output));
                        memory_fill(expected, 0xa5, sizeof(expected));
                        while (read < length)
                        {
                                positive size = escape_oracle(encoded, input[read], policy);
                                if (size > capacity - written)
                                        break;
                                for (positive at = 0; at < size; at++)
                                        expected[written + 8 + at] = encoded[at];
                                written += size;
                                read++;
                        }
                        positive2 result = memory_into_escaped(output + 8, input,
                            length, capacity, policy | (mode ? 128 : 0));
                        if (mode && result.x < read)
                        {
                                check("escape literal yield makes progress", result.x &&
                                    result.x + 1 < length &&
                                    escape_oracle(encoded, input[result.x], policy) == 1 &&
                                    escape_oracle(encoded, input[result.x + 1], policy) == 1);
                                read = result.x;
                                written = 0;
                                memory_fill(expected, 0xa5, sizeof(expected));
                                for (positive at = 0; at < read; at++)
                                        written += escape_oracle(expected + 8 + written, input[at], policy);
                        }
                        check("escape exact capacity and atomic replacements",
                              result.x == read && result.y == written &&
                              !memory_compare(output, expected, sizeof(output)));
                }
        }
}

static fn escape_stream_check(p8 address_to input, positive length)
{
        for (positive policy = 0; policy <= 64; policy++)
        {
                p8 expected[2048], output[2048];
                positive written = 0;
                for (positive at = 0; at < length; at++)
                        written += escape_oracle(expected + written, input[at], policy);
                for (positive mode = 0; mode < 2; mode++)
                for (positive split = 0; split <= length; split++)
                {
                        positive read = 0, used = 0;
                        while (read < length)
                        {
                                positive size = read < split ? split - read : length - read;
                                positive2 chunk = memory_into_escaped(output + used, input + read,
                                    size, 6 + (read % 59), policy | (mode ? 128 : 0));
                                if (!chunk.x)
                                        break;
                                read += chunk.x;
                                used += chunk.y;
                        }
                        check("escape split stream",
                              read == length && used == written &&
                              !memory_compare(output, expected, written));
                }
        }
        // JSON strings exclude NUL, but include high bytes and DEL unchanged.
        p8 expected[2048], value[257];
        positive used = 1;
        expected[0] = '"';
        for (positive at = 0; at < 255; at++)
        {
                value[at] = at + 1;
                used += escape_oracle(expected + used, value[at], 64);
        }
        value[255] = 0;
        expected[used++] = '"';
        escape_used = escape_calls = 0;
        escape_overflow = false;
        writer_json_string(escape_capture, value);
        check("JSON shared spelling and batching", !escape_overflow &&
              escape_used == used && escape_calls < 16 &&
              !memory_compare(expected, escape_output, used));
        for (positive size = 0; size <= 21; size++)
                for (positive byte = 1; byte < 256; byte++)
                {
                        used = 1;
                        for (positive at = 0; at < size; at++)
                        {
                                value[at] = byte;
                                used += escape_oracle(expected + used, byte, 64);
                        }
                        value[size] = 0;
                        expected[used++] = '"';
                        escape_used = escape_calls = 0;
                        escape_overflow = false;
                        writer_json_string(escape_capture, value);
                        check("JSON short cells and batching threshold", !escape_overflow &&
                            escape_used == used && (size > 20 || escape_calls == 1) &&
                            !memory_compare(expected, escape_output, used));
                }
}

static fn escape_scan_check(void)
{
        static const positive sizes[] = {1, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                                         63, 64, 65, 127, 128, 129, 255, 256, 257};
        p8 storage[272], encoded[6];
        memory_fill(storage, 'A', sizeof(storage));
        for (positive byte = 0; byte < 256; byte++)
                for (positive row = 0; row < array_count(sizes); row++)
                {
                        positive size = sizes[row];
                        p8 address_to input = storage + byte % 16;
                        positive places[] = {0, size / 2, size - 1, 7, 8, 15, 16};
                        for (positive which = 0; which < array_count(places); which++)
                        {
                                positive at = places[which];
                                if (at >= size) continue;
                                input[at] = byte;
                                for (positive policy = 0; policy <= 64; policy++)
                                {
                                        check("escape scan every byte across vector lanes",
                                            memory_escape_index(input, size, policy) ==
                                            (escape_oracle(encoded, byte, policy) == 1 ? size : at));
                                        if (size <= 32)
                                                escape_full_check(input, size, policy);
                                }
                                input[at] = 'A';
                        }
                }
}

static fn escape_check(p8 address_to input, positive length)
{
        for (positive policy = 0; policy < 64; policy++)
        {
                p8 expected[1024];
                positive used = 0;
                for (positive at = 0; at < length; at++)
                {
                        p8 byte = input[at];
                        p8 escaped = (byte < 32 && byte != '\t' && (policy & HEX_CONTROL)) ||
                            (byte == 127 && (policy & HEX_CONTROL)) ||
                            (byte == '\t' && (policy & HEX_TAB)) ||
                            (byte == ' ' && (policy & HEX_SPACE)) ||
                            (byte == '"' && (policy & HEX_QUOTE)) ||
                            (byte == '\\' && (policy & HEX_SLASH)) ||
                            (byte > 127 && (policy & HEX_HIGH));
                        if (escaped)
                        {
                                expected[used++] = '\\';
                                expected[used++] = 'x';
                                for (b32 shift = 4; shift >= 0; shift -= 4)
                                {
                                        p8 nibble = (byte >> shift) & 15;
                                        expected[used++] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
                                }
                        }
                        else
                                expected[used++] = byte;
                }
                escape_used = escape_calls = 0;
                escape_overflow = false;
                writer_hex_escaped(escape_capture, input, length, (p8)policy);
                check("hex escape policy and bounded bytes", !escape_overflow &&
                      escape_used == used && !memory_compare(escape_output, expected, used));
                check("hex literal batching", policy || escape_calls == (length != 0));
        }
}

static fn hex_check(p8 address_to out, p8 address_to input, positive size)
{
        check("hex returned length", memory_into_hex(out, input, size) == size * 2);
        p8 correct = true;
        for (positive i = 0; i < size * 2; i++)
        {
                // Arithmetic oracle, independent of the assembly lookup table.
                p8 nibble = (input[i / 2] >> ((i & 1) ? 0 : 4)) & 15;
                p8 expected = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
                if (out[i] != expected)
                        correct = false;
        }
        check("hex bytes", correct);
}

/* The hexadecimal decode against the C loop it replaced: every byte count
   from nothing to seventy, valid and mixed text, a bad character or a NUL
   planted at a random offset, and a sentinel past the stop. Both the result
   and every byte written must agree. */
static inline p8 hex_decode_reference(address_any into, string_address text,
                                        positive bytes)
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

static fn hex_decode_suite(void)
{
        static const char mixed[] = "0123456789abcdefABCDEFgG xz";
        static const char valid[] = "0123456789abcdefABCDEF";
        static char text[160];
        static p8 got[80], want[80];
        positive bad = 0;
        p64 seed = 0x2545F4914F6CDD1DULL;

        for (positive round = 0; round < 2; round++)
                for (positive bytes = 0; bytes <= 70; bytes++)
                        for (positive trial = 0; trial < 300; trial++)
                        {
                                for (positive i = 0; i < bytes * 2 + 4; i++)
                                {
                                        seed = seed * 6364136223846793005ULL +
                                               1442695040888963407ULL;
                                        text[i] = round ? mixed[(seed >> 33) % 28] :
                                                          valid[(seed >> 33) % 22];
                                }
                                if (trial % 3 == 0 && bytes)
                                        text[(seed >> 40) % (bytes * 2)] =
                                                (char)"\0\x20\xff"[trial % 3];
                                for (positive i = 0; i < 80; i++)
                                        got[i] = want[i] = 0xA5;
                                p8 a = hex_decode_reference(want, (string_address)text, bytes);
                                p8 b = memory_from_hex(got, (string_address)text, bytes);
                                if (a != b || memory_compare(got, want, 80))
                                        bad++;
                        }
        check("hex decode agrees with the C loop it replaced, every byte count", bad == 0);
}

static fn hex_suite(void)
{
        hex_decode_suite();
        p8 source[288], output[560];
        for (positive at = 0; at < 256; at++) source[at] = (p8)at;
        escape_check(source, 256);
        escape_stream_check(source, 256);
        escape_scan_check();
        for (positive offset = 0; offset < 16; offset++)
                for (positive size = 0; size < 66; size++)
                {
                        escape_span_check(source + offset, size);
                        escape_check(source + offset, size);
                }
        escape_check(address_bad, 0);
        escape_span_check(address_bad, 0);
        positive2 empty = memory_into_escaped(address_bad, address_bad, 17, 0, 64);
        check("escape zero capacity does not access input", !empty.x && !empty.y);
        check("hex empty null spans", memory_into_hex(null, null, 0) == 0);
        for (positive source_at = 0; source_at < 16; source_at++)
                for (positive output_at = 1; output_at <= 16; output_at++)
                        for (positive size = 0; size <= 257; size++)
                        {
                                for (positive at = 0; at < sizeof(source); at++)
                                        source[at] = (p8)(at * 197 + size);
                                memory_fill(output, 0xa5, sizeof(output));
                                hex_check(output + output_at, source + source_at, size);
                                p8 untouched = true;
                                for (positive at = 0; at < sizeof(output); at++)
                                        if ((at < output_at || at >= output_at + size * 2) &&
                                            output[at] != 0xa5)
                                                untouched = false;
                                for (positive at = 0; at < sizeof(source); at++)
                                        if (source[at] != (p8)(at * 197 + size))
                                                untouched = false;
                                check("hex source and output canaries", untouched);
                        }

        const positive quantum = 65536;
        p8 address_to guarded[2] = {memory(quantum * 3), memory(quantum * 3)};
        p8 mapped = guarded[0] && guarded[1] &&
                      (positive)guarded[0] < positive_max - 4095 &&
                      (positive)guarded[1] < positive_max - 4095;
        check("hex guard mappings", mapped);
        if (mapped)
        {
                for (positive at = 0; at < 2; at++)
                {
                        check("hex left guard", system_call_3(syscall(mprotect),
                              (positive)guarded[at], quantum, 0) == 0);
                        check("hex right guard", system_call_3(syscall(mprotect),
                              (positive)(guarded[at] + quantum * 2), quantum, 0) == 0);
                }
                for (positive size = 0; size <= 257; size++)
                        for (positive source_edge = 0; source_edge < 2; source_edge++)
                                for (positive output_edge = 0; output_edge < 2; output_edge++)
                                {
                                        p8 address_to input = guarded[0] +
                                            (source_edge ? quantum * 2 - size : quantum);
                                        p8 address_to out = guarded[1] +
                                            (output_edge ? quantum * 2 - size * 2 : quantum);
                                        for (positive at = 0; at < size; at++)
                                                input[at] = (p8)(at * 37 + size);
                                        hex_check(out, input, size);
                                }
                check("hex inaccessible empty spans",
                      memory_into_hex(guarded[1], guarded[0], 0) == 0);
                for (positive at = 0; at < 256; at++)
                        guarded[0][quantum * 2 - 256 + at] = (p8)at;
                escape_check(guarded[0] + quantum * 2 - 256, 256);
                for (positive size = 0; size < 66; size++)
                {
                        p8 address_to input = guarded[0] + quantum * 2 - size;
                        escape_span_check(input, size);
                        if (size <= 32)
                                for (positive policy = 0; policy <= 64; policy++)
                                {
                                        p8 expected[192];
                                        positive written = 0;
                                        for (positive at = 0; at < size; at++)
                                                written += escape_oracle(expected + written,
                                                    input[at], policy);
                                        p8 address_to into = guarded[1] + quantum * 2 - size * 6;
                                        positive2 chunk = memory_into_escaped(into, input,
                                            size, size * 6, policy);
                                        check("escape short vectors at both exact page ends",
                                            chunk.x == size && chunk.y == written &&
                                            !memory_compare(into, expected, written));
                                }
                        for (positive policy = 0; policy <= 64; policy++)
                                for (positive mode = 0; mode < 2; mode++)
                                for (positive capacity = 0; capacity < 66; capacity++)
                                {
                                        p8 address_to into = guarded[1] + quantum * 2 - capacity;
                                        positive2 chunk = memory_into_escaped(into, input,
                                            size, capacity, policy | (mode ? 128 : 0));
                                        check("escape input/output exact page ends",
                                              chunk.x <= size && chunk.y <= capacity);
                                }
                }
        }
        for (positive at = 0; at < 2; at++)
                if (guarded[at] && (positive)guarded[at] < positive_max - 4095)
                        memory_free(guarded[at], quantum * 3);
}

/*
        memory_into_spelled against a C encoder written from the contract:
        every size to 300 at sixteen source residues, then the lengths either
        side of the wide path's 512 and past, over five tables -- the caret
        spelling cat -v uses, a substitution whose specials are {1, '?'},
        random lengths with junk above each spelling, a mix of literal,
        translated and long entries, and printable ASCII literal with every
        other byte seven long, the most eight bytes can grow by -- and three
        input shapes. Every answer
        is compared whole, the bytes before the destination and the 64 at and
        after destination + capacity must be untouched, and the capacity runs
        through full, exact, exact less one to eight and a spread below, so
        the exact-length tail meets every length. Guard pages hold the source,
        the destination and the table each at a page end.
*/
static p64 spelled_tables[5][256];
static p64 spelled_seed = 0x243f6a8885a308d3ull;

static p64 spelled_random(void)
{
        XORSHIFT64(spelled_seed);
        return spelled_seed;
}

static p64 spelled_junk_entry(positive length)
{
        return (spelled_random() << 8) | length;
}

static fn spelled_tables_build(void)
{
        p64 address_to caret = spelling_caret(SPELL_SHOW | SPELL_TABS | SPELL_ENDS);
        for (positive byte = 0; byte < 256; byte++)
        {
                positive r = (positive)spelled_random();
                spelled_tables[0][byte] = caret[byte];
                spelled_tables[1][byte] = (byte < 32 || byte >= 128)
                                              ? ((p64)'?' << 8 | 1)
                                              : ((p64)byte << 8 | 1);
                spelled_tables[2][byte] = spelled_junk_entry(1 + r % 7);
                spelled_tables[3][byte] =
                    r % 3 == 0 ? ((p64)byte << 8 | 1)
                    : r % 3 == 1 ? ((p64)(p8)(byte ^ (1 + (r >> 8) % 255)) << 8 | 1 |
                                    (spelled_random() & ~(p64)0xffff))
                                 : spelled_junk_entry(2 + (r >> 8) % 6);
                spelled_tables[4][byte] = byte >= 0x20 && byte < 0x7f ? ((p64)byte << 8 | 1)
                                                                      : spelled_junk_entry(7);
        }
}

static positive2 spelled_oracle(p8 address_to out, p8 address_to in,
                                positive size, positive capacity,
                                p64 address_to table)
{
        positive read = 0, written = 0;
        while (read < size)
        {
                p64 entry = table[in[read]];
                positive length = (positive)(entry & 255);
                if (length > capacity - written)
                        break;
                for (positive at = 0; at < length; at++)
                        out[written + at] = (p8)(entry >> (8 * (at + 1)));
                written += length;
                read++;
        }
        return (positive2){.x = read, .y = written};
}

#define SPELLED_MOST 4200
static p8 spelled_in[SPELLED_MOST + 64], spelled_out[SPELLED_MOST * 7 + 256],
    spelled_expected[SPELLED_MOST * 7 + 256];

static fn spelled_input(p8 address_to in, positive size, positive shape)
{
        for (positive at = 0; at < size; at++)
        {
                p64 r = spelled_random();
                in[at] = shape == 0   ? (p8)r
                         : shape == 1 ? (r % 40 ? (p8)('a' + (r >> 8) % 26) : (p8)(r >> 16))
                                      : (p8)(' ' + (r >> 8) % 95);
        }
}

// Every call the first in a process, on riscv64's first suite.
static p8 spelled_first;

static p8 spelled_one(p8 address_to in, positive size, positive capacity,
                        p64 address_to table, positive shift)
{
        p8 address_to out = spelled_out + 16 + shift;
        positive span = capacity + 64 + 32;
        if (span > sizeof(spelled_out) - 16 - shift)
                span = sizeof(spelled_out) - 16 - shift;
        memory_fill(spelled_out, 0xa5, 16 + shift + span);
        positive2 want = spelled_oracle(spelled_expected, in, size, capacity, table);
        VECTOR_FIRST_CALL(spelled_first);
        positive2 got = memory_into_spelled(out, in, size, capacity, table);
        p8 right = got.x == want.x && got.y == want.y &&
                     !memory_compare(out, spelled_expected, want.y);
        for (positive at = 0; at < 16 + shift; at++)
                right = right && spelled_out[at] == 0xa5;
        for (positive at = capacity; at < capacity + 64 && 16 + shift + at < sizeof(spelled_out); at++)
                right = right && out[at] == 0xa5;
        return right;
}

static fn spelled_capacities(p8 address_to in, positive size,
                             p64 address_to table, positive shift)
{
        positive full = spelled_oracle(spelled_expected, in, size, positive_max, table).y;
        positive capacities[24], count = 0;
        capacities[count++] = full;
        capacities[count++] = full + 1 + (positive)(spelled_random() % 9);
        for (positive less = 1; less <= 8; less++)
                if (full >= less)
                        capacities[count++] = full - less;
        capacities[count++] = full / 2;
        capacities[count++] = full / 3 + 1;
        capacities[count++] = 0;
        capacities[count++] = (positive)(spelled_random() % (full + 1));
        if (full > 257)
        {
                capacities[count++] = 255;
                capacities[count++] = 256;
                capacities[count++] = 257;
        }
        for (positive c = 0; c < count; c++)
                check("spelled output, answer and canaries against the C encoder",
                      spelled_one(in, size, capacities[c], table, shift));
}

static fn spelled_caret_check(void)
{
        for (positive flags = 0; flags < 16; flags++)
        {
                p64 address_to table = spelling_caret(flags);
                p8 right = table == spelling_caret(flags);
                for (positive byte = 0; byte < 256; byte++)
                {
                        p8 text[8];
                        positive length = 0, value = byte;
                        p8 show = flags & SPELL_SHOW;
                        if (byte == 9 && (flags & SPELL_TABS))
                                text[0] = '^', text[1] = 'I', length = 2;
                        else if (byte == 10 && (flags & SPELL_ENDS))
                                text[0] = '$', text[1] = 10, length = 2;
                        else if (((byte == 9 || byte == 10) && !(flags & SPELL_ALL)) || !show)
                                text[0] = (p8)byte, length = 1;
                        else
                        {
                                if (value > 127)
                                {
                                        text[length++] = 'M';
                                        text[length++] = '-';
                                        value -= 128;
                                }
                                if (value == 127 || value < 32)
                                {
                                        text[length++] = '^';
                                        text[length++] = value == 127 ? '?' : (p8)(value + 64);
                                }
                                else
                                        text[length++] = (p8)value;
                        }
                        p64 entry = table[byte];
                        right = right && (entry & 255) == length;
                        for (positive at = 0; at < length; at++)
                                right = right && (p8)(entry >> (8 * (at + 1))) == text[at];
                }
                check("spelling_caret: every byte under every flag set", right);
        }
}

//      Mutation fuzz against the C encoder: a random table (real caret
//      tables, substitution, junk and mixed entries, some then rewritten a
//      few entries at a time), a random size biased across the wide path's
//      thresholds, a random capacity from nothing to a little past
//      complete, and a random residue on both sides.
static fn spelled_fuzz(positive rounds)
{
        static p64 mutated[256];
        for (positive round = 0; round < rounds; round++)
        {
                positive r = (positive)spelled_random();
                positive pick = r % 6;
                p64 address_to table = pick < 5 ? spelled_tables[pick]
                                                : spelling_caret((positive)(r >> 8) & 7);
                memory_copy(mutated, table, 2048);
                for (positive flips = (r >> 16) % 5; flips; flips--)
                {
                        positive which = (positive)(spelled_random() & 255);
                        mutated[which] = spelled_junk_entry(1 + (positive)(spelled_random() % 7));
                }
                positive size = (r >> 24) % 3 ? (positive)(spelled_random() % 700)
                                              : (positive)(spelled_random() % SPELLED_MOST);
                positive residue = (positive)(spelled_random() & 63);
                spelled_input(spelled_in + residue, size, (positive)(spelled_random() % 3));
                positive full = spelled_oracle(spelled_expected, spelled_in + residue, size,
                                               positive_max, mutated).y;
                positive capacity = (positive)(spelled_random() % (full + 12));
                check("spelled fuzz against the C encoder",
                      spelled_one(spelled_in + residue, size, capacity, mutated,
                                  (positive)(spelled_random() & 15)));
        }
}

//      spelling_c against the per-byte rules it replaced (tar_quoted,
//      writer_c_escape, sed l): every byte under every extra set.
static fn spelled_c_check(void)
{
        for (positive high = 0; high < 2; high++)
        {
                p64 address_to table = spelling_ansi(high);
                p8 right = table == spelling_ansi(high);
                for (positive byte = 0; byte < 256; byte++)
                {
                        static const p8 names[256] = {
                            [7] = 'a', [8] = 'b', [27] = 'E', [12] = 'f',
                            ['\n'] = 'n', ['\r'] = 'r', ['\t'] = 't', [11] = 'v',
                            ['\\'] = '\\', ['\''] = '\''};
                        p8 text[4] = {'\\'};
                        positive length = 1;
                        if (names[byte])
                                text[1] = names[byte], length = 2;
                        else if (escape_categories[byte] & (HEX_CONTROL | HEX_TAB) ||
                                 (high && byte >= 128))
                        {
                                text[1] = (p8)('0' + (byte >> 6));
                                text[2] = (p8)('0' + ((byte >> 3) & 7));
                                text[3] = (p8)('0' + (byte & 7));
                                length = 4;
                        }
                        else
                                text[0] = (p8)byte;
                        p64 entry = table[byte];
                        right = right && (entry & 255) == length;
                        for (positive at = 0; at < length; at++)
                                right = right && (p8)(entry >> (8 * (at + 1))) == text[at];
                }
                check("spelling_ansi: every byte as shell_ansi_byte spells it", right);
        }
        for (positive high = 0; high < 2; high++)
        {
                p64 address_to table = spelling_hidden(high);
                p8 right = table == spelling_hidden(high);
                for (positive byte = 0; byte < 256; byte++)
                {
                        p8 hide = byte < 32 || byte == 127 || (high && byte >= 128);
                        right = right && table[byte] == ((p64)(hide ? '?' : byte) << 8 | 1);
                }
                check("spelling_hidden: every byte, with and without the high half", right);
        }
        for (positive extra = 0; extra < 128; extra++)
        {
                p64 address_to table = spelling_c(extra);
                p8 right = table == spelling_c(extra);
                for (positive byte = 0; byte < 256; byte++)
                {
                        p8 text[4];
                        positive length;
                        p8 slashed = (byte == '\\' && !(extra & SPELL_C_BACKSLASH_BARE)) ||
                                       (byte == '\'' && (extra & SPELL_C_APOSTROPHE)) ||
                                       (byte == '"' && (extra & SPELL_C_QUOTE)) ||
                                       (byte == ' ' && (extra & SPELL_C_SPACE)) ||
                                       (byte == ':' && (extra & SPELL_C_COLON));
                        if (byte >= 128 && (extra & SPELL_C_HIGH_BARE))
                                text[0] = (p8)byte, length = 1;
                        else if (byte >= 7 && byte <= 13 &&
                                 (!(extra & SPELL_C_NT_ONLY) || byte == '\t' || byte == '\n'))
                                text[0] = '\\', text[1] = "abtnvfr"[byte - 7], length = 2;
                        else if (byte < ' ' || byte >= 127)
                        {
                                text[0] = '\\';
                                text[1] = (p8)('0' + (byte >> 6));
                                text[2] = (p8)('0' + ((byte >> 3) & 7));
                                text[3] = (p8)('0' + (byte & 7));
                                length = 4;
                        }
                        else if (slashed)
                                text[0] = '\\', text[1] = (p8)byte, length = 2;
                        else
                                text[0] = (p8)byte, length = 1;
                        p64 entry = table[byte];
                        right = right && (entry & 255) == length;
                        for (positive at = 0; at < length; at++)
                                right = right && (p8)(entry >> (8 * (at + 1))) == text[at];
                }
                check("spelling_c: every byte under every extra set", right);
        }
}

static fn spelled_suite(void)
{
        spelled_c_check();
        static const positive longer[] = {400, 500, 511, 512, 513, 515, 543, 600,
                                          777, 1024, 1100, 2047, 4096, SPELLED_MOST};
        spelled_caret_check();
        spelled_fuzz(40000);
        for (positive t = 0; t < 5; t++)
        {
                p64 address_to table = spelled_tables[t];
                for (positive size = 0; size <= 300; size++)
                        for (positive residue = 0; residue < 16; residue++)
                        {
                                spelled_input(spelled_in + residue, size, (size + residue) % 3);
                                spelled_capacities(spelled_in + residue, size, table,
                                                   (residue * 5) & 15);
                        }
                for (positive s = 0; s < array_count(longer); s++)
                        for (positive residue = 0; residue < 16; residue += 3)
                                for (positive shape = 0; shape < 3; shape++)
                                {
                                        spelled_input(spelled_in + residue, longer[s], shape);
                                        spelled_capacities(spelled_in + residue, longer[s],
                                                           table, residue);
                                }
        }
        positive2 empty = memory_into_spelled(address_bad, address_bad, 0, 99, address_bad);
        check("spelled zero size touches nothing", !empty.x && !empty.y);
        empty = memory_into_spelled(address_bad, address_bad, 17, 0, address_bad);
        check("spelled zero capacity touches nothing", !empty.x && !empty.y);

        const positive quantum = 65536;
        p8 address_to guarded[2] = {memory(quantum * 3), memory(quantum * 3)};
        p8 mapped = guarded[0] && guarded[1] &&
                      (positive)guarded[0] < positive_max - 4095 &&
                      (positive)guarded[1] < positive_max - 4095;
        check("spelled guard mappings", mapped);
        if (!mapped)
                return;
        for (positive at = 0; at < 2; at++)
        {
                system_call_3(syscall(mprotect), (positive)guarded[at], quantum, 0);
                system_call_3(syscall(mprotect), (positive)(guarded[at] + quantum * 2), quantum, 0);
        }
        static const positive sizes[] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                                          63, 64, 65, 100, 511, 512, 513, 600, 1000};
        for (positive t = 0; t < 5; t++)
        {
                // The table at the end of a page, the rest well inside.
                p64 address_to table = (p64 address_to)(guarded[0] + quantum * 2 - 2048);
                memory_copy(table, spelled_tables[t], 2048);
                for (positive s = 0; s < array_count(sizes); s++)
                {
                        positive size = sizes[s];
                        p8 address_to in = guarded[0] + quantum * 2 - 2048 - size;
                        spelled_input(in, size, t == 4 ? 0 : t % 3);
                        positive full = spelled_oracle(spelled_expected, in, size,
                                                       positive_max, table).y;
                        // Every capacity to 80, then steps, with the output
                        // ending at the page.
                        for (positive capacity = 0; capacity <= full + 8;
                             capacity += capacity < 80 ? 1 : 1 + capacity / 64)
                        {
                                p8 address_to into = guarded[1] + quantum * 2 - capacity;
                                positive2 want = spelled_oracle(spelled_expected, in, size,
                                                                capacity, table);
                                VECTOR_FIRST_CALL(spelled_first);
                                positive2 got = memory_into_spelled(into, in, size, capacity, table);
                                check("spelled source, destination and table at page ends",
                                      got.x == want.x && got.y == want.y &&
                                      !memory_compare(into, spelled_expected, want.y));
                        }
                        // The source alone at the page end.
                        p8 address_to end_in = guarded[0] + quantum * 2 - size;
                        memory_copy(spelled_in, in, size);
                        memory_copy(end_in, spelled_in, size);
                        check("spelled source at a page end",
                              spelled_one(end_in, size, full, spelled_tables[t], 0));
                        memory_copy(table, spelled_tables[t], 2048);
                }
        }
        for (positive at = 0; at < 2; at++)
                memory_free(guarded[at], quantum * 3);
}

b32 main(void)
{
        spelled_tables_build();
#if X64
        p8 vector = cpu_has_avx2, wide = cpu_has_avx512, vbmi = cpu_has_avx512_vbmi;
        cpu_has_avx2 = 0;
        hex_suite();
        spelled_suite();
        cpu_has_avx2 = vector;
        if (vector)
        {
                hex_suite();
                cpu_has_avx512 = 0;
                spelled_suite();
                cpu_has_avx512 = wide;
                if (wide && vbmi)
                        spelled_suite();
        }
#elif RISCV64
        // Every call the first in a process, which asks for V and comes back
        // to the strip; then V known; then the stream body without it.
        hex_suite();
        spelled_first = true;
        spelled_suite();
        spelled_first = false;
        spelled_suite();
        p8 vector = cpu_has_vector;
        cpu_has_vector = 1;
        spelled_suite();
        cpu_has_vector = vector;
#else
        hex_suite();
        spelled_suite();
#endif
        return test_report(null);
}
#endif /* CHECK_hex */

#ifdef CHECK_codec
#include "../../src/lib.util.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static char high_alphabet[64];
static const char *alphabets[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", "0123456789ABCDEFGHIJKLMNOPQRSTUV",
    "0123456789ABCDEF", "01", "01",
    "fedcba9876543210", "10", high_alphabet,
};
static const p8 widths[] = {6, 6, 5, 5, 4, 1, 9, 4, 9, 6};

// Every decode the first in a process, on riscv64's first tier.
static p8 codec_first;

static fn codec_check(p8 address_to source, p8 address_to output,
                      p8 address_to decoded, positive groups, positive kind)
{
        positive bits = widths[kind] == 9 ? 1 : widths[kind];
        positive bytes = bits == 6 ? 3 : bits == 5 ? 5 : 1;
        positive symbols = bytes * 8 / bits;
        p8 values[256];
        memory_fill(values, 255, sizeof(values));
        for (positive at = 0; at < ((positive)1 << bits); at++)
                values[(p8)alphabets[kind][at]] = (p8)at;
        check("codec encoded groups", memory_encode_power2(output, source,
              groups, (string_address)alphabets[kind], widths[kind]) == groups);
        p8 valid = true;
        for (positive group = 0; group < groups; group++)
                for (positive at = 0; at < symbols; at++)
                {
                        positive value = 0;
                        for (positive bit = 0; bit < bits; bit++)
                        {
                                positive position = at * bits + bit;
                                p8 byte = source[group * bytes + position / 8];
                                positive shift = widths[kind] == 9 ? position & 7
                                    : 7 - (position & 7);
                                value = (value << 1) | ((byte >> shift) & 1);
                        }
                        if (output[group * symbols + at] !=
                            (p8)alphabets[kind][value])
                                valid = false;
                }
        check("codec independent bit oracle", valid);
        VECTOR_FIRST_CALL(codec_first);
        check("codec decoded groups", memory_decode_power2(decoded, output,
              groups, values, widths[kind]) == groups);
        check("codec round trip", !memory_compare(source, decoded, groups * bytes));
        if (kind == 4)
        {
                positive cases[] = {0, 1, (positive)1 << 32, (positive)1 << 63};
                for (positive at = 0; at < sizeof(cases) / sizeof(cases[0]); at++)
                {
                        check("hex case exact length", memory_into_hex_case(output,
                              source, groups, cases[at]) == groups * 2);
                        p8 correct = true;
                        for (positive byte = 0; byte < groups * 2; byte++)
                        {
                                p8 nibble = (source[byte / 2] >> ((byte & 1) ? 0 : 4)) & 15;
                                p8 expected = nibble < 10 ? '0' + nibble
                                    : (cases[at] ? 'A' : 'a') + nibble - 10;
                                if (output[byte] != expected) correct = false;
                        }
                        check("hex case arithmetic oracle", correct);
                }
        }
}

/*
        The varints against a model written another way round: it finds the
        last byte first and only then adds the groups up, where the routines
        stop at the first byte without a continuation bit. Every one and two
        byte input under every cut; every three byte input whole; every last
        byte after every run of continuations from none to ten, which holds
        every tenth byte and every second spelling, under every cut; random
        continuation-heavy bytes under every cut; every width's edges written,
        read back, and read cut one short; and numbers ending at a guard page,
        read with exactly the bytes to the page's end and written exactly up
        to it.
*/
static positive vli_model_size(p64 value)
{
        positive size = 1;

        while (size < 10 && value >> (7 * size))
                size++;
        return size;
}

static positive vli_model_get(const p8 address_to bytes, positive length,
                              positive most, p64 address_to value)
{
        positive limit = length < most ? length : most;
        positive last = 0;
        p64 got = 0;

        if (limit > 10)
                limit = 10;
        while (last < limit && bytes[last] & 0x80)
                last++;
        if (last == limit || (last && !bytes[last]) || (last == 9 && bytes[9] != 1))
                return 0;
        for (positive at = 0; at <= last; at++)
                got |= (p64)(bytes[at] & 0x7f) << (7 * at);
        address_to value = got;
        return last + 1;
}

static p64 vli_seed = 0x9e3779b97f4a7c15ull;

static p64 vli_random(void)
{
        XORSHIFT64(vli_seed);
        return vli_seed;
}

//      Every cut of one input, length and most from 0 to 12, against the
//      model; a refusal must leave the value where it was.
static p8 vli_cuts_agree(const p8 address_to bytes)
{
        for (positive length = 0; length <= 12; length++)
                for (positive most = 0; most <= 12; most++)
                {
                        p64 mine = 0x5a5a5a5a5a5a5a5aull, theirs = mine;
                        positive a = memory_vli_get(bytes, length, most,
                                                    address_of mine);
                        positive b = vli_model_get(bytes, length, most,
                                                   address_of theirs);

                        if (a != b || mine != theirs)
                                return false;
                }
        return true;
}

static fn vli_checks(void)
{
        p8 bytes[16];
        p8 agree = true;

        for (positive first = 0; first < 256; first++)
                for (positive second = 0; second < 256; second++)
                {
                        memory_fill(bytes, 0xff, sizeof bytes);
                        bytes[0] = (p8)first;
                        bytes[1] = (p8)second;
                        agree = agree && vli_cuts_agree(bytes);
                        bytes[2] = 0x01;
                        agree = agree && vli_cuts_agree(bytes);
                }
        check("vli every one and two byte input, under every cut", agree);

        for (p32 word = 0; word < 1u << 24 && agree; word++)
        {
                p64 mine = 7, theirs = 7;

                bytes[0] = (p8)word;
                bytes[1] = (p8)(word >> 8);
                bytes[2] = (p8)(word >> 16);
                bytes[3] = 0xff;
                agree = memory_vli_get(bytes, 3, 10, address_of mine) ==
                                vli_model_get(bytes, 3, 10, address_of theirs) &&
                        mine == theirs &&
                        memory_vli_get(bytes, 4, 3, address_of mine) ==
                                vli_model_get(bytes, 4, 3, address_of theirs) &&
                        mine == theirs;
        }
        check("vli every three byte input", agree);

        for (positive run = 0; run <= 10; run++)
                for (positive last = 0; last < 256; last++)
                        for (positive fill = 0; fill < 3; fill++)
                        {
                                for (positive at = 0; at < run; at++)
                                        bytes[at] = fill == 0 ? 0x80
                                                    : fill == 1 ? 0xff
                                                                : (p8)(vli_random() | 0x80);
                                bytes[run] = (p8)last;
                                for (positive at = run + 1; at < sizeof bytes; at++)
                                        bytes[at] = (p8)vli_random();
                                agree = agree && vli_cuts_agree(bytes);
                        }
        check("vli every last byte after every run of continuations, under "
              "every cut", agree);

        for (positive round = 0; round < 200000; round++)
        {
                for (positive at = 0; at < sizeof bytes; at++)
                {
                        p64 roll = vli_random();

                        bytes[at] = (p8)(roll >> 8);
                        if (roll % 5)
                                bytes[at] |= 0x80;
                }
                agree = agree && vli_cuts_agree(bytes);
        }
        check("vli random continuation-heavy bytes, under every cut", agree);

        p8 written = true, read = true;
        for (positive bit = 0; bit <= 64 + 20000; bit++)
                for (positive edge = 0; edge < 4; edge++)
                {
                        p64 value = bit < 64 ? ((p64)1 << bit) + edge - 2
                                    : bit == 64 ? ~0ull - edge
                                                : vli_random() >> (vli_random() % 64);
                        positive size = vli_model_size(value);
                        p8 model[16];
                        p64 back = ~value;

                        for (positive at = 0; at < size; at++)
                                model[at] = (p8)(((value >> (7 * at)) & 0x7f) |
                                                 (at + 1 < size ? 0x80 : 0));
                        memory_fill(bytes, 0xee, sizeof bytes);
                        written = written && memory_vli_size(value) == size &&
                                  memory_vli_put(bytes, value) == size &&
                                  !memory_compare(bytes, model, size);
                        for (positive at = size; at < sizeof bytes; at++)
                                written = written && bytes[at] == 0xee;
                        read = read &&
                               memory_vli_get(bytes, sizeof bytes, 10,
                                              address_of back) == size &&
                               back == value &&
                               memory_vli_get(bytes, size, size,
                                              address_of back) == size &&
                               back == value &&
                               !memory_vli_get(bytes, size - 1, 10,
                                               address_of back) &&
                               !memory_vli_get(bytes, sizeof bytes, size - 1,
                                               address_of back) &&
                               back == value;
                }
        check("vli every width's edges written exactly, byte for byte as "
              "the model writes them", written);
        check("vli and read back, and refused one byte short", read);

        const positive quantum = 65536;
        p8 address_to guarded = memory(quantum * 2);
        p8 mapped = guarded && (positive)guarded < positive_max - 4095;
        check("vli guard mapping", mapped);
        if (mapped)
        {
                check("vli guard", system_call_3(syscall(mprotect),
                      (positive)(guarded + quantum), quantum, 0) == 0);
                p8 edge = true;
                for (positive run = 0; run <= 10; run++)
                        for (positive last = 0; last < 256; last += 3)
                                for (positive length = 0; length <= 12; length++)
                                {
                                        p8 address_to at = guarded + quantum - length;
                                        p8 copy[16];

                                        for (positive k = 0; k < sizeof copy; k++)
                                                copy[k] = (p8)(k < run ? vli_random() | 0x80
                                                               : k == run ? last
                                                                          : vli_random());
                                        memory_copy(at, copy, length);
                                        for (positive most = 0; most <= 12; most++)
                                        {
                                                p64 mine = 1, theirs = 1;

                                                edge = edge &&
                                                       memory_vli_get(at, length, most,
                                                                      address_of mine) ==
                                                               vli_model_get(copy, length, most,
                                                                             address_of theirs) &&
                                                       mine == theirs;
                                        }
                                }
                for (positive bit = 0; bit < 64; bit++)
                {
                        p64 value = ((p64)1 << bit) | (vli_random() >> (63 - bit) >> 1);
                        positive size = vli_model_size(value);
                        p64 back = 0;

                        edge = edge &&
                               memory_vli_put(guarded + quantum - size, value) == size &&
                               memory_vli_get(guarded + quantum - size, size, 10,
                                              address_of back) == size &&
                               back == value;
                }
                check("vli ends at a guard page: read to its edge, written up "
                      "to it", edge);
                memory_free(guarded, quantum * 2);
        }
}

/*
        What the hand loops over a span became. A decimal number over a
        pointer and a length says overflow where it used to wrap or be
        refused by a test of `n > (max - digit) / 10` at every site; the
        offset after the last of a byte and the run of a class at the end
        of a text are what a path's last component, a record's line ending
        and a trim of blanks each walked back for.
*/
static fn span_checks(void)
{
        static const p8 text[] = "18446744073709551615" "18446744073709551616"
                                 "4294967295" "4294967296" "123abc" "0012";
        positive value = 7;
        positive got;

        value = 0;
        check("digit appended", positive_append_digit(address_of value, 5, 9) &&
                                    value == 5);
        check("digit past a small limit refused, value kept",
              !positive_append_digit(address_of value, 6, 5) && value == 5);
        value = 12;
        check("digit makes the limit", positive_append_digit(address_of value, 3, 123) &&
                                           value == 123);
        value = 12;
        check("digit makes one past the limit",
              !positive_append_digit(address_of value, 4, 123) && value == 12);
        value = positive_max / 10;
        check("last digit of the largest number",
              positive_append_digit(address_of value, 5, positive_max) &&
                  value == positive_max);
        value = positive_max / 10;
        check("one past the largest number",
              !positive_append_digit(address_of value, 6, positive_max) &&
                  value == positive_max / 10);
        value = positive_max / 10 + 1;
        check("a number times ten past the word",
              !positive_append_digit(address_of value, 0, positive_max) &&
                  value == positive_max / 10 + 1);

        {
                /* the 16_40 variants against the generic: every span length of the text, both limits */
                p8 agree = true;

                for (positive length = 0; length <= sizeof text - 1; length++)
                        for (int lim = 0; lim < 2; lim++)
                        {
                                positive limit = lim ? 0xffffffffull : positive_max;
                                positive mine = 7, theirs = 7;
                                positive want = memory_digits_checked(text, length, limit, address_of theirs);
                                positive got = memory_digits_checked_16_40(text, length, limit, address_of mine);
                                p8 whole_want = memory_digits_whole(text, length, limit, address_of theirs);
                                p8 whole = memory_digits_whole_16_40(text, length, limit, address_of mine);

                                if (got != want || mine != theirs || whole != whole_want)
                                        agree = false;
                        }
                check("16_40 variants agree with the generic on every span of the text, both limits",
                      agree);
        }
        value = 7;
        check("digits at the front, count and number",
              memory_digits_checked(text + 60, 6, positive_max, address_of value) == 3 &&
                  value == 123);
        value = 7;
        check("no digits, number kept",
              memory_digits_checked(text + 63, 3, positive_max, address_of value) == 0 &&
                  value == 7);
        check("nothing to read",
              memory_digits_checked(text, 0, positive_max, address_of value) == 0 &&
                  value == 7);
        check("the largest word, exactly",
              memory_digits_checked(text, 20, positive_max, address_of value) == 20 &&
                  value == positive_max);
        value = 7;
        check("one past the largest word",
              memory_digits_checked(text + 20, 20, positive_max, address_of value) == 0 &&
                  value == 7);
        check("a limit of 32 bits holds",
              memory_digits_checked(text + 40, 10, 0xffffffffull, address_of value) == 10 &&
                  value == 0xffffffffull);
        value = 7;
        check("a limit of 32 bits refuses one more",
              memory_digits_checked(text + 50, 10, 0xffffffffull, address_of value) == 0 &&
                  value == 7);
        check("the length bounds the read, not the digits after it",
              memory_digits_checked(text + 60, 2, positive_max, address_of value) == 2 &&
                  value == 12);

        value = 7;
        check("a whole span of digits", memory_digits_whole(text + 66, 4, 99, address_of value) &&
                                            value == 12);
        value = 7;
        check("a span with a letter in it, number kept",
              !memory_digits_whole(text + 60, 6, positive_max, address_of value) &&
                  value == 7);
        check("an empty span is no number",
              !memory_digits_whole(text, 0, positive_max, address_of value) && value == 7);
        check("a span past the limit",
              !memory_digits_whole(text + 66, 4, 11, address_of value) && value == 7);

        check("after the last slash", memory_after_last((const p8 *)"a/b/c", '/', 5) == 4);
        check("after the last slash of a cut text",
              memory_after_last((const p8 *)"a/b/c", '/', 3) == 2);
        check("after a final slash", memory_after_last((const p8 *)"a/", '/', 2) == 2);
        check("after the only byte", memory_after_last((const p8 *)"/", '/', 1) == 1);
        check("no such byte", memory_after_last((const p8 *)"abc", '/', 3) == 0);
        check("no bytes", memory_after_last((const p8 *)"", '/', 0) == 0);

        got = memory_trailing((const p8 *)"ab \t\n", 5, byte_is_space);
        check("white space at the end", got == 3);
        got = memory_trailing((const p8 *)"ab \t\n", 5, byte_is_blank);
        check("blanks stop at a newline", got == 0);
        got = memory_trailing((const p8 *)"ab \t", 4, byte_is_blank);
        check("blanks at the end", got == 2);
        got = memory_trailing((const p8 *)"ab\r\n", 4, byte_is_line_end);
        check("a line ending of two bytes", got == 2);
        got = memory_trailing((const p8 *)"ab\n\nc\r", 6, byte_is_line_end);
        check("a line ending stops at a byte of the line", got == 1);
        got = memory_trailing((const p8 *)"\v\f\r ", 4, byte_is_space);
        check("every byte of the text is the run", got == 4);
        got = memory_trailing((const p8 *)"a\xa0", 2, byte_is_space);
        check("a byte past ASCII is not white space", got == 0);
        got = memory_trailing((const p8 *)"", 0, byte_is_space);
        check("no bytes, no run", got == 0);
}

/*
        The controller macros at their edges. scope_exit runs at every way out
        of its scope -- falling off the end, a return, a break, a continue --
        in the reverse of the order they were written, and after the value a
        return hands back has been worked out; a cleanup that is guarded by a
        flag is skipped by it. return_if leaves with its value when its
        condition holds and asks the condition once. array_where names the
        index for the test to read, gives the count when nothing holds and
        reads its count once; array_any is the same asked as a yes or a no.
*/
static positive control_trail[8];
static positive control_trails;

static positive control_scoped(positive way)
{
        positive value = 5;

        scope_exit(control_trail[control_trails++] = 1);
        scope_exit(control_trail[control_trails++] = value);
        if (way == 1)
                return value;
        value = 9;
        if (way == 2)
                return value;
        return 0;
}

static positive control_kept(p8 kept)
{
        positive undone = 0;
        positive lost = 0;

        {
                scope_exit(if (!kept) undone++);
                lost = undone;
        }
        return undone * 10 + lost;
}

static positive control_left(positive way)
{
        return_if(way == 1, 11);
        return_if(way == 2, 22);
        return 33;
}

static positive control_asked;

static positive control_once(void)
{
        return_if(control_asked++ == 1, 7);
        return 8;
}

static fn control_left_void(positive address_to reached, positive way)
{
        return_if(way);
        address_to reached = 1;
}

static fn control_checks(void)
{
        static const positive table[] = {4, 7, 7, 1};
        positive calls = 0;
        positive found;
        positive loops = 0;

        control_trails = 0;
        check("a scope falls off its end: last written, first run",
              control_scoped(0) == 0 && control_trails == 2 &&
                  control_trail[0] == 9 && control_trail[1] == 1);
        control_trails = 0;
        check("a return computes its value before the cleanup runs",
              control_scoped(1) == 5 && control_trails == 2 &&
                  control_trail[0] == 5 && control_trail[1] == 1);
        control_trails = 0;
        check("a later return sees what was stored before it, and both cleanups run",
              control_scoped(2) == 9 && control_trails == 2 &&
                  control_trail[0] == 9 && control_trail[1] == 1);
        check("a cleanup behind a flag is skipped by it and taken without it",
              control_kept(true) == 0 && control_kept(false) == 10);

        control_trails = 0;
        for (positive turn = 0; turn < 4; turn++)
        {
                scope_exit(loops++);
                if (turn == 1)
                        continue;
                if (turn == 2)
                        break;
        }
        check("a continue and a break leave the scope of the turn like any other exit",
              loops == 3);

        {
                positive reached = 0;

                control_left_void(address_of reached, 1);
                check("a void function leaves where it says so", reached == 0);
                control_left_void(address_of reached, 0);
                check("and goes on where it does not", reached == 1);
        }
        check("an early exit gives its value, the first that holds, or goes on",
              control_left(1) == 11 && control_left(2) == 22 && control_left(0) == 33);
        check("the condition is asked once, not once for the test and once for the return",
              control_once() == 8 && control_once() == 7 && control_asked == 2);

        check("nothing to look at is the count, which is nothing",
              array_where(at, 0, true) == 0 && !array_any(at, 0, true));
        check("the first index that holds", array_where(at, 4, table[at] == 7) == 1);
        check("the last index is looked at", array_where(at, 4, table[at] == 1) == 3);
        check("nothing holds: the count", array_where(at, 4, table[at] == 5) == 4);
        check("a count shorter than the table keeps the rest unseen",
              array_where(at, 2, table[at] == 1) == 2 && !array_any(at, 3, table[at] == 1) &&
                  array_any(at, 4, table[at] == 1));
        found = array_where(at, (calls++, 4), table[at] == 5);
        check("the count is read once, not every turn", calls == 1 && found == 4);
        calls = 0;
        check("the test reads the index, and nothing past the first hit is looked at",
              array_any(at, 4, (calls += at, table[at] == 7)) && calls == 1);
}

b32 main(void)
{
        p8 source[700], output[1100], decoded[700];
        vli_checks();
        span_checks();
        control_checks();
        for (positive at = 0; at < sizeof(high_alphabet); at++)
                high_alphabet[at] = (char)(192 + at);
#if X64
        p8 vector = cpu_has_avx2;
        p8 wide = cpu_has_avx512, vbmi = cpu_has_avx512_vbmi;
        for (positive mode = 0; mode < 4; mode++)
        {
                cpu_has_avx2 = mode ? vector : 0;
                cpu_has_avx512 = mode > 1 ? wide : 0;
                cpu_has_avx512_vbmi = mode > 2 ? vbmi : 0;
#elif RISCV64
        //      Every decode the first in a process, which asks for V and
        //      comes back to the routine's entry; then without V.
        p8 vector = 0;
        for (positive mode = 0; mode < 2; mode++)
        {
                codec_first = mode == 0;
                if (mode == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
#endif
        for (positive kind = 0; kind < sizeof(widths); kind++)
        {
                positive bits = widths[kind] == 9 ? 1 : widths[kind];
                positive bytes = bits == 6 ? 3 : bits == 5 ? 5 : 1;
                positive symbols = bytes * 8 / bits;
                check("codec empty inaccessible encode", memory_encode_power2(
                      address_bad, address_bad, 0, address_bad, widths[kind]) == 0);
                check("codec empty inaccessible decode", memory_decode_power2(
                      address_bad, address_bad, 0, address_bad, widths[kind]) == 0);
                // Every count to 64, and past a strip of V at 128 bits whatever the width.
                static const positive longer[] = {100, 129};
                for (positive offset = 1; offset <= 16; offset++)
                        for (positive g = 0; g <= 64 + array_count(longer); g++)
                        {
                                positive groups = g <= 64 ? g : longer[g - 65];
                                for (positive at = 0; at < sizeof(source); at++)
                                        source[at] = (p8)(at * 197 + groups * 47);
                                memory_fill(output, 0xa5, sizeof(output));
                                memory_fill(decoded, 0xa5, sizeof(decoded));
                                codec_check(source + offset, output + offset,
                                            decoded + offset, groups, kind);
                                p8 valid = true;
                                for (positive at = 0; at < sizeof(output); at++)
                                        if ((at < offset || at >= offset + groups * symbols) &&
                                            output[at] != 0xa5) valid = false;
                                for (positive at = 0; at < sizeof(decoded); at++)
                                        if ((at < offset || at >= offset + groups * bytes) &&
                                            decoded[at] != 0xa5) valid = false;
                                check("codec exact output canaries", valid);
                        }
                p8 values[256];
                memory_fill(values, 255, sizeof(values));
                for (positive at = 0; at < ((positive)1 << bits); at++)
                        values[(p8)alphabets[kind][at]] = (p8)at;
                for (positive invalid = 0; invalid < 256; invalid++)
                        if (values[invalid] == 255)
                                for (positive at = 0; at < symbols * 40; at++)
                                {
                                        // Past the sixteenth quantum, a strip of V at 128 bits
                                        // for the widest, one malformed byte in eight.
                                        if (at >= symbols * 16 && invalid % 8 != 1)
                                                continue;
                                        memory_fill(output, alphabets[kind][0], symbols * 40);
                                        memory_fill(decoded, 0xa5, sizeof(decoded));
                                        output[at] = (p8)invalid;
                                        VECTOR_FIRST_CALL(codec_first);
                                        positive done = memory_decode_power2(decoded,
                                            output, 40, values, widths[kind]);
                                        check("codec first malformed quantum", done == at / symbols);
                                        p8 valid = true;
                                        for (positive byte = done * bytes; byte < sizeof(decoded); byte++)
                                                if (decoded[byte] != 0xa5) valid = false;
                                        check("codec malformed quantum untouched", valid);
                                }
        }
        for (positive kind = 0; kind < 32; kind++)
                if (kind != 1 && kind != 4 && kind != 5 && kind != 6 && kind != 9)
                {
                        check("codec invalid encode shape", memory_encode_power2(
                              address_bad, address_bad, 1, address_bad, kind) == 0);
                        check("codec invalid decode shape", memory_decode_power2(
                              address_bad, address_bad, 1, address_bad, kind) == 0);
                }

        const positive quantum = 65536;
        p8 address_to guarded[4] = {memory(quantum * 3), memory(quantum * 3),
                                   memory(quantum * 3), memory(quantum * 3)};
        p8 mapped = true;
        for (positive at = 0; at < 4; at++)
                mapped &= guarded[at] && (positive)guarded[at] < positive_max - 4095;
        check("codec guard mappings", mapped);
        if (mapped)
        {
                for (positive at = 0; at < 4; at++)
                {
                        check("codec left guard", system_call_3(syscall(mprotect),
                              (positive)guarded[at], quantum, 0) == 0);
                        check("codec right guard", system_call_3(syscall(mprotect),
                              (positive)(guarded[at] + quantum * 2), quantum, 0) == 0);
                }
                for (positive kind = 0; kind < sizeof(widths); kind++)
                        for (positive groups = 0; groups <= 64; groups++)
                                for (positive edges = 0; edges < 8; edges++)
                                {
                                        positive bits = widths[kind] == 9 ? 1 : widths[kind];
                                        positive bytes = bits == 6 ? 3 : bits == 5 ? 5 : 1;
                                        positive symbols = bytes * 8 / bits;
                                        p8 address_to input = guarded[0] + ((edges & 1) ?
                                            quantum * 2 - groups * bytes : quantum);
                                        p8 address_to out = guarded[1] + ((edges & 2) ?
                                            quantum * 2 - groups * symbols : quantum);
                                        p8 address_to back = guarded[2] + ((edges & 4) ?
                                            quantum * 2 - groups * bytes : quantum);
                                        for (positive at = 0; at < groups * bytes; at++)
                                                input[at] = (p8)(at * 37 + groups);
                                        codec_check(input, out, back, groups, kind);
                                        p8 address_to map = guarded[3] + ((edges & 1) ?
                                            quantum * 2 - 256 : quantum);
                                        memory_fill(map, 255, 256);
                                        for (positive at = 0; at < ((positive)1 << bits); at++)
                                                map[(p8)alphabets[kind][at]] = (p8)at;
                                        VECTOR_FIRST_CALL(codec_first);
                                        check("codec exact table guard", memory_decode_power2(
                                              back, out, groups, map, widths[kind]) == groups);
                                        check("codec table guard decoded bytes",
                                              !memory_compare(back, input, groups * bytes));
                                }
        }
        for (positive at = 0; at < 4; at++)
                if (guarded[at] && (positive)guarded[at] < positive_max - 4095)
                        memory_free(guarded[at], quantum * 3);
#if X64
        }
        cpu_has_avx2 = vector;
        cpu_has_avx512 = wide;
        cpu_has_avx512_vbmi = vbmi;
#elif RISCV64
        }
        codec_first = false;
        cpu_has_vector = vector;
#endif
        return test_report(null);
}
#endif /* CHECK_codec */

#ifdef CHECK_fixed
#include "../../src/lib.util.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static positive fixed_oracle(p8 address_to out, positive magnitude,
                              positive scale, p8 minus, positive width,
                              positive precision, positive flags)
{
        positive divisor = 1;
        for (positive i = precision; i < scale; i++) divisor *= 10;
        if (scale > precision)
        {
                positive remainder = magnitude % divisor;
                magnitude /= divisor;
                if (remainder > divisor / 2 ||
                    (remainder == divisor / 2 && magnitude % 2)) magnitude++;
                scale = precision;
        }
        p8 reverse[40], body[128];
        positive digits = 0, used = 0;
        do { reverse[digits++] = '0' + magnitude % 10; magnitude /= 10; }
        while (magnitude);
        while (digits <= scale) reverse[digits++] = '0';
        p8 sign = minus ? '-' : flags & 2 ? '+' : flags & 4 ? ' ' : 0;
        if (sign) body[used++] = sign;
        for (positive i = digits; i;)
        {
                if (i == scale) body[used++] = '.';
                body[used++] = reverse[--i];
        }
        if (!scale && (precision || (flags & 8))) body[used++] = '.';
        for (positive i = scale; i < precision; i++) body[used++] = '0';
        positive padding = width > used ? width - used : 0, at = 0;
        if (!(flags & 1))
        {
                if ((flags & 16) && sign) out[at++] = sign;
                for (positive i = 0; i < padding; i++) out[at++] = flags & 16 ? '0' : ' ';
        }
        positive skip = !(flags & 1) && (flags & 16) && sign ? 1 : 0;
        for (positive i = skip; i < used; i++) out[at++] = body[i];
        if (flags & 1)
                for (positive i = 0; i < padding; i++) out[at++] = ' ';
        return at;
}

static positive series_oracle(p8 address_to into, positive room,
                               positive length, positive first,
                               positive finish, bipolar step)
{
        if (!length || room < length || first >= finish || finish > length)
                return 0;
        positive used = length;
        while (room - used >= length)
        {
                for (positive i = 0; i < length; i++) into[used + i] = into[used - length + i];
                positive carry = step < 0 ? (positive)0 - (positive)step : (positive)step;
                for (positive i = finish; carry && i > first;)
                {
                        p8 address_to byte = into + used + --i;
                        if (*byte == '.') continue;
                        b32 digit = *byte - '0';
                        b32 part = carry % 10;
                        carry /= 10;
                        digit += step < 0 ? -part : part;
                        if (digit < 0) { digit += 10; carry++; }
                        if (digit >= 10) { digit -= 10; carry++; }
                        *byte = '0' + digit;
                }
                if (carry) break;
                used += length;
        }
        return used;
}

b32 main(void)
{
        const positive values[] = {0, 1, 5, 9, 10, 15, 25, 95, 99, 999,
            99995, 1000000000000000000ULL, 9223372036854775808ULL, positive_max};
        p8 actual[4096], expected[4096];
        for (positive v = 0; v < array_count(values); v++)
                for (positive scale = 0; scale <= 18; scale++)
                        for (positive precision = 0; precision <= 20; precision++)
                                for (positive flags = 0; flags < 32; flags++)
                                {
                                        positive width = (v * 13 + scale * 7 + flags) % 80;
                                        p8 minus = (v + scale + precision) & 1;
                                        fixed_decimal field = fixed_decimal_prepare(values[v], scale,
                                            minus, width, precision, flags);
                                        positive wanted = fixed_oracle(expected, values[v], scale,
                                            minus, width, precision, flags);
                                        memory_fill(actual, 0xa5, sizeof(actual));
                                        check("fixed short buffer is untouched",
                                            fixed_decimal_into(actual, wanted - 1, address_of field) == 0 &&
                                            actual[0] == 0xa5 && actual[wanted - 1] == 0xa5);
                                        positive got = fixed_decimal_into(actual, wanted, address_of field);
                                        check("fixed exact decimal field", got == wanted &&
                                            !memory_compare(actual, expected, wanted) && actual[wanted] == 0xa5);
                                }
        const bipolar steps[] = {0, 1, -1, 3, -3, 25, -25, 999, -999,
                                 bipolar_max, bipolar_min};
        for (positive length = 1; length <= 64; length++)
                for (positive room = 0; room <= length * 9 + 1; room++)
                        for (positive s = 0; s < array_count(steps); s++)
                        {
                                positive first = length / 4, finish = length - length / 4;
                                memory_fill(actual, 0xa5, sizeof(actual));
                                for (positive at = 0; at < length; at++)
                                        actual[at] = at >= first && at < finish
                                            ? '0' + (at * 7 + room) % 10 : '#';
                                if (finish - first > 3) actual[finish - 3] = '.';
                                memory_copy_apart(expected, actual, sizeof(actual));
                                positive wanted = series_oracle(expected, room, length, first, finish, steps[s]);
                                positive got = memory_decimal_series(actual, room, length, first, finish, steps[s]);
                                check("decimal series exact carry and boundary", got == wanted &&
                                      !memory_compare(actual, expected, got));
                                p8 intact = true;
                                for (positive at = max(room, length); at < sizeof(actual); at++)
                                        if (actual[at] != 0xa5) intact = false;
                                check("decimal series bounded writes", intact);
                        }
        check("decimal empty inaccessible span",
            memory_decimal_series(address_bad, 0, 0, 0, 0, 1) == 0 &&
            memory_decimal_series(address_bad, 0, 1, 0, 1, 1) == 0 &&
            memory_decimal_series(address_bad, 8, 8, 8, 8, 1) == 0 &&
            memory_decimal_series(address_bad, 8, 8, 0, 9, 1) == 0);
        // Cached short records must carry across every digit position without
        // touching literal prefixes, suffixes or a decimal point. All-nines
        // fields exercise rollover, including a final partial reservation.
        for (positive length = 4; length <= 8; length++)
                for (positive first = 0; first < length; first++)
                        for (positive finish = first + 1; finish <= length; finish++)
                                for (positive seed = 7; seed <= 9; seed++)
                                        for (positive point = first; point < finish; point++)
                                                for (positive extra = 0; extra <= length * 3 + 7; extra++)
                                                {
                                                        positive room = length + extra;
                                                        memory_fill(actual, 0xa5, sizeof(actual));
                                                        for (positive at = 0; at < length; at++)
                                                                actual[at] = at < first || at >= finish
                                                                    ? '#' : '9';
                                                        actual[finish - 1] = '0' + seed;
                                                        if (point > first && point + 1 < finish)
                                                                actual[point] = '.';
                                                        memory_copy_apart(expected, actual, sizeof(actual));
                                                        positive wanted = series_oracle(expected, room,
                                                            length, first, finish, 1);
                                                        positive got = memory_decimal_series(actual, room,
                                                            length, first, finish, 1);
                                                        check("decimal cached carry positions", got == wanted &&
                                                              !memory_compare(actual, expected, got));
                                                        p8 intact = true;
                                                        for (positive at = room; at < sizeof(actual); at++)
                                                                if (actual[at] != 0xa5) intact = false;
                                                        check("decimal cached partial reservation", intact);
                                                }
        const positive quantum = 65536;
        p8 address_to guarded = memory(quantum * 3);
        p8 mapped = guarded && (positive)guarded < positive_max - 4095;
        check("decimal guard mapping", mapped);
        if (mapped)
        {
                check("decimal left guard", system_call_3(syscall(mprotect),
                    (positive)guarded, quantum, 0) == 0);
                check("decimal right guard", system_call_3(syscall(mprotect),
                    (positive)(guarded + quantum * 2), quantum, 0) == 0);
                for (positive length = 1; length <= 128; length++)
                        for (positive extra = 0; extra <= length * 2; extra++)
                                for (positive edge = 0; edge < 2; edge++)
                                {
                                        positive room = length + extra;
                                        p8 address_to into = guarded +
                                            (edge ? quantum * 2 - room : quantum);
                                        for (positive i = 0; i < length; i++)
                                                into[i] = '0' + (i * 3 + extra) % 10;
                                        memory_copy_apart(expected, into, length);
                                        bipolar step = extra & 1 ? -1 : 1;
                                        positive wanted = series_oracle(expected, room,
                                            length, 0, length, step);
                                        positive got = memory_decimal_series(into, room,
                                            length, 0, length, step);
                                        check("decimal exact guard edge", got == wanted &&
                                            !memory_compare(into, expected, got));
                                        fixed_decimal field = fixed_decimal_prepare(positive_max,
                                            extra % 19, edge, extra, extra % 21, extra % 32);
                                        positive size = field.length + field.zeroes + field.padding;
                                        p8 address_to field_at = guarded +
                                            (edge ? quantum * 2 - size : quantum);
                                        check("fixed exact guard edge",
                                            fixed_decimal_into(field_at, size, address_of field) == size);
                                }
                memory_free(guarded, quantum * 3);
        }
        return test_report(null);
}
#endif /* CHECK_fixed */

#ifdef CHECK_table
#include "../../src/lib.util.c"

static p8 table_output[16384];
static positive table_used, table_calls;
static p8 table_overflow;

static fn table_capture(address_any bytes, positive length)
{
        table_calls++;
        if (!length)
                length = string_length(bytes);
        if (length > sizeof(table_output) - table_used)
        {
                table_overflow = true;
                return;
        }
        memory_copy_apart(table_output + table_used, bytes, length);
        table_used += length;
}

#define log table_capture
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#undef log
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static fn table_reset(void)
{
        table_used = table_calls = 0;
        table_overflow = false;
}

static string_address table_field(address_any row, p8 column,
                                   p8 address_to scratch)
{
        (void)column; (void)scratch;
        return *(string_address address_to)row;
}

static const ul_table_column table_columns[] = {
    {"value", "VALUE", 0, false, TABLE_STRING, true},
    {"other", "OTHER", 0, false, TABLE_STRING, true},
    {"third", "THIRD", 0, false, TABLE_STRING, true}
};
static p8 table_selected[] = {0};

static fn table_render(string_address value, positive mode)
{
        table_reset();
        ul_table(mode == 2 ? "test" : null, address_of value, 1,
                 table_columns, table_selected, 1, true, mode == 1, table_field);
}

static p8 table_hex_digit(p8 nibble)
{
        return nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
}

static fn name_list_checks(void)
{
        /*
                The reader answers which of three things happened rather
                than yes or no, and hands back the name it could not place:
                an empty list is refused without a word, a name it does not
                know is named, and a name written twice is a column written
                twice -- `lsns -o TYPE,TYPE` prints TYPE beside TYPE.
        */
        p8 columns[8] = {0xa5};
        positive have = 99;
        p8 unknown[UL_COLUMN_NAME];

        check("table defaults without an output option",
              ul_table_column_list(null, table_columns, 3, table_selected, 1,
                                   columns, address_of have, unknown) == UL_COLUMNS_OK &&
              have == 1 && !columns[0]);
        check("table defaults stay bounded",
              ul_table_column_list(null, table_columns, 0, table_selected, 1,
                                   columns, address_of have, unknown) == UL_COLUMNS_UNKNOWN &&
              !have);
        check("table append keeps the defaults in front",
              ul_table_column_list("+other", table_columns, 3, table_selected, 1,
                                   columns, address_of have, unknown) == UL_COLUMNS_OK &&
              have == 2 && !columns[0] && columns[1] == 1);
        check("table keeps a column named twice",
              ul_table_column_list("value,value", table_columns, 3, table_selected, 1,
                                   columns, address_of have, unknown) == UL_COLUMNS_OK &&
              have == 2 && !columns[0] && !columns[1]);
        check("table refuses an empty list without a word",
              ul_table_column_list("", table_columns, 3, table_selected, 1,
                                   columns, address_of have, unknown) == UL_COLUMNS_EMPTY);
        check("table names the column it does not know",
              ul_table_column_list("value,nosuch", table_columns, 3, table_selected, 1,
                                   columns, address_of have, unknown) == UL_COLUMNS_UNKNOWN &&
              !string_compare(unknown, "nosuch"));
        typedef struct
        {
                string_address name;
                positive payload;
        } named;
        static const named definitions[] = {
            {(string_address)"alpha", 11},
            {(string_address)"Beta", 22},
            {(string_address)"gamma", 33},
        };
        p8 selected[4];
        positive count = 0;

        check("name list case fold and trailing comma",
              name_list_select((string_address)"ALPHA,beta,", definitions,
                               sizeof(definitions[0]),
                               array_count(definitions), selected,
                               address_of count, array_count(selected), 0, null) &&
              count == 2 && selected[0] == 0 && selected[1] == 1);

        count = 0;
        check("name list duplicates retained",
              name_list_select((string_address)"alpha,alpha", definitions,
                               sizeof(definitions[0]),
                               array_count(definitions), selected,
                               address_of count, array_count(selected), 0, null) &&
              count == 2 && selected[0] == 0 && selected[1] == 0);

        selected[0] = 0;
        count = 1;
        check("name list unique seeded prefix",
              name_list_select((string_address)"ALPHA,gamma", definitions,
                               sizeof(definitions[0]),
                               array_count(definitions), selected,
                               address_of count, array_count(selected),
                               NAME_LIST_UNIQUE, null) &&
              count == 2 && selected[0] == 0 && selected[1] == 2);

        count = 0;
        check("name list exact case",
              !name_list_select((string_address)"ALPHA", definitions,
                                sizeof(definitions[0]),
                                array_count(definitions), selected,
                                address_of count, array_count(selected),
                                NAME_LIST_CASE_SENSITIVE, null));

        count = 0;
        check("name list strict trailing comma",
              !name_list_select((string_address)"alpha,", definitions,
                                sizeof(definitions[0]),
                                array_count(definitions), selected,
                                address_of count, array_count(selected),
                                NAME_LIST_REJECT_TRAILING, null));

        count = 0;
        check("name list capacity bound",
              !name_list_select((string_address)"alpha,Beta", definitions,
                                sizeof(definitions[0]),
                                array_count(definitions), selected,
                                address_of count, 1, 0, null));

        selected[0] = 0;
        count = 1;
        check("name list full unique prefix",
              name_list_select((string_address)"alpha", definitions,
                               sizeof(definitions[0]),
                               array_count(definitions), selected,
                               address_of count, 1, NAME_LIST_UNIQUE, null) &&
              count == 1 &&
              !name_list_select((string_address)"Beta", definitions,
                                sizeof(definitions[0]),
                                array_count(definitions), selected,
                                address_of count, 1, NAME_LIST_UNIQUE, null));

        named wide[256];
        for (positive i = 0; i < array_count(wide); i++)
                wide[i] = (named){(string_address)"other", i};
        wide[255].name = (string_address)"last";
        count = 0;
        check("name list byte index boundary",
              name_list_select((string_address)"last", wide, sizeof(wide[0]),
                               array_count(wide), selected,
                               address_of count, array_count(selected), 0, null) &&
              count == 1 && selected[0] == 255);
        count = 0;
        check("name list excessive definitions",
              !name_list_select((string_address)"last", wide,
                                sizeof(wide[0]), 257, selected,
                                address_of count, array_count(selected), 0, null));

        name_list_error error;
        string_address rejected = "alpha,unknown,Beta";
        count = 0;
        check("name list reports the rejected bounded span after a prefix",
              !name_list_select(rejected, definitions, sizeof(definitions[0]),
                                array_count(definitions), selected,
                                address_of count, array_count(selected), 0,
                                address_of error) && count == 1 &&
              error.at == rejected + 6 && error.length == 7);
        count = 0;
        rejected = "alpha,";
        check("name list reports an empty strict trailing item",
              !name_list_select(rejected, definitions, sizeof(definitions[0]),
                                array_count(definitions), selected,
                                address_of count, array_count(selected),
                                NAME_LIST_REJECT_TRAILING, address_of error) &&
              count == 1 && error.at == rejected + 6 && !error.length);
        p8 short_name[] = {0xa5, 0xa5, 0xa5, 0xa5};
        count = 0;
        check("column diagnostic truncates only within its owned buffer",
              name_list_columns("alpha,unknown", definitions,
                  sizeof(definitions[0]), array_count(definitions), selected,
                  address_of count, short_name, 3) == NAME_LIST_UNKNOWN &&
              count == 1 && !memory_compare(short_name, "un\0\xa5", 4));
        count = 1;
        check("column empty append refuses a seeded prefix",
              name_list_columns("", definitions, sizeof(definitions[0]),
                  array_count(definitions), selected, address_of count,
                  null, 0) == NAME_LIST_EMPTY && count == 1);
        count = 0;
        check("column repeats take an entry each, as util-linux's lsblk -o NAME,NAME does",
              name_list_columns("alpha,alpha,Beta,gamma", definitions,
                  sizeof(definitions[0]), array_count(definitions), selected,
                  address_of count, unknown, sizeof(unknown)) == NAME_LIST_OK &&
              count == 4 && selected[0] == 0 && selected[1] == 0 &&
              selected[2] == 1 && selected[3] == 2);
        {
                static p8 wide[256];
                static char long_list[256 * 6 + 8];
                positive at = 0;

                for (positive n = 0; n < 256; n++)
                {
                        memory_copy_apart(long_list + at, "alpha,", 6);
                        at += 6;
                }
                memory_copy_apart(long_list + at, "Beta", 5);
                count = 0;
                check("column capacity refuses the name past the selection's room",
                      name_list_columns(long_list, definitions, sizeof(definitions[0]),
                          array_count(definitions), wide, address_of count,
                          unknown, sizeof(unknown)) == NAME_LIST_UNKNOWN &&
                      count == 256 && string_equals(unknown, "Beta"));
        }

        p8 lock_fields[] = "  1\tPOSIX\vowner";
        p8 address_to field_at = lock_fields;
        string_address id = storage_field(address_of field_at);
        string_address kind = storage_field(address_of field_at);
        check("kernel field grammar is space and tab",
              string_equals(id, "1") &&
              string_equals(kind, "POSIX\vowner"));

        p8 records[] = "first\nlast";
        p8 address_to record_at = records;
        p8 address_to record_limit = records + sizeof(records) - 1;
        string_address first = storage_line_next(address_of record_at,
                                                 record_limit);
        string_address last = storage_line_next(address_of record_at,
                                                record_limit);
        check("kernel final line uses owned sentinel",
              string_equals(first, "first") && string_equals(last, "last") &&
              !storage_line_next(address_of record_at, record_limit));
}

static table_cell table_scratch_field(address_any context, positive row,
                                       positive column, p8 address_to scratch)
{
        (void)context;
        string_address text = row == TABLE_HEADING ? "KEY" : column ? "Z" : "a\t";
        positive length = string_length(text);
        memory_copy(scratch, text, length);
        return (table_cell){.text = {scratch, length}, .minimum = column ? 2 : 3,
            .flags = TABLE_CLIP_OUTPUT | (column ? TABLE_RIGHT : 0),
            .escape = HEX_CONTROL | HEX_TAB};
}

static fn table_layout_checks(void)
{
        table_view view = {.output = table_capture, .cell = table_scratch_field,
            .count = 2, .separator = " ", .fixed_widths = true, .pad_empty = true};
        table_reset();
        table_row(&view, 0, null);
        check("borrowed fields clip escaped output before the next column",
              table_used == 7 && !memory_compare(table_output, "a\\x  Z\n", 7));
        view.fixed_widths = false;
        view.pairs = true;
        table_reset();
        table_row(&view, 0, null);
        string_address expected = "KEY=\"a\\x09\" KEY=\"Z\"\n";
        check("pair headings do not overwrite scratch-backed values",
              table_used == string_length(expected) &&
              !memory_compare(table_output, expected, table_used));
}

static fn table_checks(void)
{
        p8 bytes[1030], expected[8192];
        for (positive size = 0; size <= 1024; size++)
        {
                positive width = 0, longest = 0, lines = 1, single_width = 0;
                for (positive at = 0; at < size; at++)
                {
                        p8 byte = (p8)(1 + ((at * 197 + size) % 255));
                        bytes[at] = byte;
                        single_width += byte < ' ' || byte == 127 ? 4 : 1;
                        if (byte == '\n')
                        {
                                longest = max(longest, width);
                                width = 0;
                                lines++;
                        }
                        else
                                width += byte < ' ' || byte == 127 ? 4 : 1;
                }
                bytes[size] = 0;
                longest = max(longest, width);
                check("table multiline width", (table_cell_width((table_cell){.text = {bytes, size}, .escape = HEX_CONTROL | HEX_TAB, .flags = TABLE_LINES})) == longest);
                check("table single-line width", (memory_hex_width((byte_span){bytes, size}, HEX_CONTROL | HEX_TAB)) == single_width);
                check("table newline count", memory_count(bytes, size, '\n') + 1 == lines);
                positive start = 0;
                for (positive line = 0; line <= lines; line++)
                {
                        positive stop = start;
                        while (stop < size && bytes[stop] != '\n')
                                stop++;
                        byte_span got = table_part((table_cell){.text = {bytes, size}, .flags = TABLE_LINES}, 0, line);
                        positive wanted = line < lines ? stop - start : 0;
                        check("table indexed line", got.length == wanted &&
                              !memory_compare(got.bytes, bytes + start, wanted));
                        start = stop < size ? stop + 1 : size;
                }
        }

        // Bounded safe fields include embedded NUL; raw/JSON fields end at it.
        positive used = 0;
        for (positive byte = 0; byte < 256; byte++)
        {
                bytes[byte] = (p8)byte;
                if (byte < ' ' || byte == 127)
                {
                        expected[used++] = '\\'; expected[used++] = 'x';
                        expected[used++] = table_hex_digit(byte >> 4);
                        expected[used++] = table_hex_digit(byte & 15);
                }
                else
                        expected[used++] = byte;
        }
        table_reset();
        writer_hex_span(table_capture, (byte_span){bytes, 256}, HEX_CONTROL | HEX_TAB, positive_max);
        check("safe byte policy", !table_overflow && table_used == used &&
              !memory_compare(table_output, expected, used));
        check("safe width agrees with output", (memory_hex_width((byte_span){bytes, 256}, HEX_CONTROL | HEX_TAB)) == used);

        for (positive limit = 0; limit <= used + 1; limit++)
        {
                table_reset();
                writer_hex_span(table_capture, (byte_span){bytes, 256},
                                HEX_CONTROL | HEX_TAB, limit);
                check("clipped escapes retain every output-byte boundary",
                      table_used == min(used, limit) &&
                      !memory_compare(table_output, expected, table_used));
        }

        for (positive byte = 1; byte < 256; byte++)
                bytes[byte - 1] = byte;
        bytes[255] = 0;
        for (positive mode = 1; mode <= 2; mode++)
        {
                string_address prefix = mode == 1 ? "VALUE\n" :
                    "{\n   \"test\": [\n      {\n         \"value\": \"";
                used = string_length(prefix);
                memory_copy(expected, prefix, used);
                for (positive byte = 1; byte < 256; byte++)
                {
                        if (mode == 1 && (byte <= ' ' || byte >= 127 || byte == '\\'))
                        {
                                expected[used++] = '\\'; expected[used++] = 'x';
                        }
                        else if (mode == 2 && byte < ' ')
                        {
                                expected[used++] = '\\'; expected[used++] = 'u';
                                expected[used++] = '0'; expected[used++] = '0';
                        }
                        else
                        {
                                if (mode == 2 && (byte == '"' || byte == '\\'))
                                        expected[used++] = '\\';
                                expected[used++] = byte;
                                continue;
                        }
                        expected[used++] = table_hex_digit(byte >> 4);
                        expected[used++] = table_hex_digit(byte & 15);
                }
                string_address suffix = mode == 1 ? "\n" : "\"\n      }\n   ]\n}\n";
                positive tail = string_length(suffix);
                memory_copy(expected + used, suffix, tail);
                used += tail;
                table_render(bytes, mode);
                if (table_used != used || memory_compare(table_output, expected, used))
                        string_format(log, "table mode=%p got=%p wanted=%p prefix=%p\n",
                                      mode, table_used, used,
                                      memory_common_prefix(table_output, expected,
                                                           min(table_used, used)));
                check("raw/JSON exact byte policy", !table_overflow && table_used == used &&
                      !memory_compare(table_output, expected, used));
        }

        table_render("ab\tcd\nx\n", 0);
        check("normal multiline layout", table_used == 18 &&
              !memory_compare(table_output, "VALUE\nab\\x09cd\nx\n\n", 18));

        table_render("", 1);
        check("empty raw field", table_used == 7 &&
              !memory_compare(table_output, "VALUE\n\n", 7));
        string_address empty_json =
            "{\n   \"test\": [\n      {\n         \"value\": \"\"\n      }\n   ]\n}\n";
        table_render("", 2);
        check("empty JSON field", table_used == string_length(empty_json) &&
              !memory_compare(table_output, empty_json, table_used));
}

typedef struct
{
        p8 column;
        string_address expected;
} table_projection;

// Paired benchmarks define TABLE_BASELINE on both revisions so these
// reference-only call sites do not change the optimizer's decisions.
#ifndef TABLE_BASELINE
_Static_assert(sizeof(ul_table_column) == 32, "field facts must fit column padding");

static fn table_projection_modes(address_any row,
    const ul_table_column address_to columns, positive count,
    p8 address_to selected, positive selected_count, ul_table_field field)
{
        ul_table_column callbacks[64];
        p8 expected[8192];
        memory_copy_apart(callbacks, (address_any)columns, count * sizeof(columns[0]));
        for (positive i = 0; i < count; i++)
                callbacks[i].decimal = false;
        for (positive mode = 0; mode < 5; mode++)
        {
                positive expected_size = 0;
                for (positive path = 0; path < 2; path++)
                {
                        const ul_table_column address_to definitions = path ? columns : callbacks;
                        table_reset();
                        if (mode == 4)
                                ul_table_json("test", row, 0, 1, definitions,
                                              selected, selected_count, field);
                        else
                                ul_table_out(row, 0, 1, definitions, count,
                                             selected, selected_count, mode & 1,
                                             mode & 2, field);
                        check("projection output bound", !table_overflow && table_used <= sizeof(expected));
                        if (table_overflow || table_used > sizeof(expected))
                                return;
                        if (!path)
                        {
                                expected_size = table_used;
                                memory_copy_apart(expected, table_output, table_used);
                        }
                        else
                                check("decimal/escaped exact output",
                                      table_used == expected_size &&
                                      !memory_compare(table_output, expected, table_used));
                }
        }
}
#endif

static fn table_projection_check(string_address name, address_any row,
                                 const ul_table_column address_to columns,
                                 positive count, ul_table_field field,
                                 const table_projection address_to expected,
                                 positive projection_count)
{
        p8 scratch[96], selected[64];
        for (positive i = 0; i < projection_count; i++)
        {
                memory_fill(scratch, 0xa5, sizeof(scratch));
                selected[i] = expected[i].column;
                p8 same = string_equals(field(row, selected[i], scratch),
                                           expected[i].expected);
                if (!same)
                        string_format(log, "projection %s column=%p\n", name, (positive)selected[i]);
                check("column projection", same);
                check("projection scratch bound", scratch[32] == 0xa5);
        }
#ifndef TABLE_BASELINE
        table_projection_modes(row, columns, count, selected, projection_count, field);
#endif
#ifdef TABLE_BENCHMARK
        for (positive mode = 0; mode < 3; mode++)
        {
                p64 start = get_cpu_time();
                for (positive round = 0; round < 16384; round++)
                {
                        table_reset();
                        if (mode == 2)
                                ul_table_json(name, row, 0, 1, columns,
                                              selected, projection_count, field);
                        else
                                ul_table_out(row, 0, 1, columns, count,
                                             selected, projection_count, true,
                                             mode == 1, field);
                }
                p64 elapsed = get_cpu_time() - start;
                string_format(log, "projection %s mode=%p ticks=%p\n",
                              name, mode, (positive)elapsed);
        }
#endif
}

static fn table_projection_checks(void)
{
#ifndef TABLE_BASELINE
        // Exercise the numeric fast path against escaping, including empty
        // nullable fields, both alignments, heading escapes and wider padding.
        static const string_address numbers[] = {
            "", "0", "1", "4294967295", "18446744073709551615",
        };
        p8 selected[] = {0, 0};
        ul_table_column column = {"number", "N\tUM", 0, false,
                                   TABLE_STRING, .decimal = true};
        for (positive i = 0; i < array_count(numbers); i++)
                for (positive shape = 0; shape < 8; shape++)
                        for (p8 json = TABLE_STRING; json <= TABLE_NULL_NUMBER; json++)
                        {
                                string_address value = numbers[i];
                                column.width = (shape >> 1) * 10;
                                column.number = shape & 1;
                                column.json = json;
                                table_projection_modes(address_of value,
                                    address_of column, 1, selected, 2, table_field);
                        }
#endif
        struct snapshot_process process = {
            .pid = p32_max, .ppid = 1, .uid = 42, .command = "worker",
        };
        ul_lsns_entry ns = {.inode = p64_max, .process = address_of process,
                            .processes = p32_max, .command = "session", .user = "owner"};
        static const table_projection ns_expected[] = {
            {UL_LSNS_NS, "18446744073709551615"}, {UL_LSNS_NPROCS, "4294967295"},
            {UL_LSNS_PID, "4294967295"}, {UL_LSNS_PPID, "1"},
            {UL_LSNS_COMMAND, "session"}, {UL_LSNS_UID, "42"}, {UL_LSNS_USER, "owner"},
        };
        ul_lsfd_entry fd = {.process = address_of process, .user = "owner",
            .fd = p32_max, .inode = p64_max, .size = (p64)1 << 32,
            .type = "REG", .name = "a\tb\nc", .deleted = true, .access = 2};
        static const table_projection fd_expected[] = {
            {UL_LSFD_COMMAND, "worker"}, {UL_LSFD_PID, "4294967295"},
            {UL_LSFD_USER, "owner"}, {UL_LSFD_FD, "4294967295"},
            {UL_LSFD_TYPE, "REG"}, {UL_LSFD_NAME, "a\tb\nc"}, {UL_LSFD_KNAME, "a\tb\nc"},
            {UL_LSFD_INODE, "18446744073709551615"}, {UL_LSFD_SIZE, "4294967296"},
            {UL_LSFD_UID, "42"}, {UL_LSFD_DELETED, "1"},
            {UL_LSFD_MNTID, ""}, {UL_LSFD_POS, ""}, {UL_LSFD_MODE, ""},
        };
        ul_lslocks_entry lock = {.inode = p64_max, .start = 1,
                                 .finish = p64_max, .mandatory = true, .pid = -1};
        static const table_projection lock_expected[] = {
            {UL_LOCKS_COMMAND, ""}, {UL_LOCKS_INODE, "18446744073709551615"},
            {UL_LOCKS_MANDATORY, "1"}, {UL_LOCKS_START, "1"},
            {UL_LOCKS_END, "18446744073709551615"}, {UL_LOCKS_PATH, ""}, {UL_LOCKS_PID, "-1"},
        };
        ul_wipefs_row signature = {.device = "/dev/x", .type = "ext4",
            .label = "data", .length = 255, .magic = {0xa5, 0x5a, 1, 2, 3, 4, 5, 6},
            .usage = "filesystem"};
        static const table_projection signature_expected[] = {
            {UL_WIPEFS_DEVICE, "/dev/x"}, {UL_WIPEFS_TYPE, "ext4"},
            {UL_WIPEFS_UUID, ""}, {UL_WIPEFS_LABEL, "data"},
            {UL_WIPEFS_LENGTH, "255"}, {UL_WIPEFS_USAGE, "filesystem"},
        };
        ul_lsblk_device device = {.kname = "disk0", .path = "/dev/disk0", .type = "disk",
            .mount_text = "m1\nm2", .fstype = "ext4", .fsver = "1.0", .label = "data",
            .uuid = "fs-uuid", .partuuid = "part-uuid", .partlabel = "part-label",
            .owner = "root", .group = "disk", .mode = "brw-", .scheduler = "none",
            .transport = "pcie", .vendor = "vendor", .model = "model", .revision = "rev",
            .serial = "serial", .hctl = "0:1:2:3",
            .removable = true, .rotational = true, .alignment = p64_max,
            .minimum_io = 1, .optimal_io = (positive)1 << 32,
            .physical_sector = 4096, .logical_sector = 512, .read_ahead = 128};
        static const table_projection device_expected[] = {
            {UL_LSBLK_KNAME, "disk0"}, {UL_LSBLK_PATH, "/dev/disk0"},
            {UL_LSBLK_RM, "1"}, {UL_LSBLK_RO, "0"}, {UL_LSBLK_TYPE, "disk"},
            {UL_LSBLK_MOUNTPOINTS, "m1\nm2"}, {UL_LSBLK_FSTYPE, "ext4"}, {UL_LSBLK_FSVER, "1.0"},
            {UL_LSBLK_LABEL, "data"}, {UL_LSBLK_UUID, "fs-uuid"}, {UL_LSBLK_PARTUUID, "part-uuid"},
            {UL_LSBLK_PARTLABEL, "part-label"}, {UL_LSBLK_OWNER, "root"}, {UL_LSBLK_GROUP, "disk"},
            {UL_LSBLK_MODE, "brw-"}, {UL_LSBLK_ALIGNMENT, "18446744073709551615"},
            {UL_LSBLK_MINIO, "1"}, {UL_LSBLK_OPTIO, "4294967296"},
            {UL_LSBLK_PHYSEC, "4096"}, {UL_LSBLK_LOGSEC, "512"},
            {UL_LSBLK_ROTA, "1"}, {UL_LSBLK_SCHED, "none"}, {UL_LSBLK_RQSIZE, ""},
            {UL_LSBLK_RA, "128"}, {UL_LSBLK_TRAN, "pcie"}, {UL_LSBLK_VENDOR, "vendor"},
            {UL_LSBLK_MODEL, "model"}, {UL_LSBLK_REV, "rev"}, {UL_LSBLK_SERIAL, "serial"}, {UL_LSBLK_HCTL, "0:1:2:3"},
        };
        ul_ipc_row ipc = {.id = p64_max, .uid = 42, .gid = 43, .cuid = 44,
                          .cgid = 45, .count = (positive)1 << 32,
                          .pid_one = p32_max, .pid_two = 1};
        static const table_projection ipc_expected[] = {
            {UL_IPC_ID, "18446744073709551615"}, {UL_IPC_CUID, "44"},
            {UL_IPC_CGID, "45"}, {UL_IPC_UID, "42"}, {UL_IPC_GID, "43"},
            {UL_IPC_NATTCH, "4294967296"}, {UL_IPC_MSGS, "4294967296"},
            {UL_IPC_NSEMS, "4294967296"}, {UL_IPC_CPID, "4294967295"},
            {UL_IPC_LSPID, "4294967295"}, {UL_IPC_LPID, "1"}, {UL_IPC_LRPID, "1"},
        };
#define PROJECTIONS(name, row, columns, field, expected)                     \
        table_projection_check(name, address_of row, columns,               \
                               array_count(columns), field, expected,       \
                               array_count(expected))
        PROJECTIONS("lsns", ns, ul_lsns_columns, ul_lsns_table_field, ns_expected);
        PROJECTIONS("lsfd", fd, ul_lsfd_columns, ul_lsfd_field, fd_expected);
        PROJECTIONS("lslocks", lock, ul_lslocks_columns, ul_lslocks_field, lock_expected);
        PROJECTIONS("wipefs", signature, ul_wipefs_columns, ul_wipefs_field, signature_expected);
        ul_lsblk_row device_view = {&device, 0, false};
        PROJECTIONS("lsblk", device_view, ul_lsblk_columns, ul_lsblk_field, device_expected);
        PROJECTIONS("ipc", ipc, ul_ipc_columns, ul_ipc_field, ipc_expected);
#undef PROJECTIONS

        p8 long_name[256], guarded[98];
        memory_fill(long_name, 'x', sizeof(long_name) - 1);
        long_name[255] = end;
        memory_fill(guarded, 0xa5, sizeof(guarded));
        device.kname = long_name;
        device_view.depth = 32;
        ul_lsblk_tree = true;
        ul_lsblk_json = ul_lsblk_paths = false;
        string_address tree = ul_lsblk_field(&device_view, UL_LSBLK_NAME, guarded + 1);
        check("long lsblk tree name stays complete",
              string_length(tree) == 319 && !memory_compare(tree + 64, long_name, 256));
        check("long lsblk tree name preserves callback scratch bounds",
              guarded[0] == 0xa5 && guarded[97] == 0xa5);
        ul_lsblk_tree = false;
}

static fn table_typed_projection_checks(void)
{
        struct snapshot_process process = {.pid = 0, .uid = p32_max, .command = ""};
        ul_lsfd_entry fd = {.process = &process, .user = "", .name = "", .type = "",
            .major = p32_max, .minor = p32_max, .mount_known = true,
            .position_known = true, .position = p64_max, .access_known = true};
        static const table_projection descriptor[] = {
            {UL_LSFD_PID, "0"}, {UL_LSFD_UID, "4294967295"},
            {UL_LSFD_DEVICE, "4294967295:4294967295"}, {UL_LSFD_MNTID, "0"},
            {UL_LSFD_POS, "18446744073709551615"}, {UL_LSFD_DELETED, "0"},
            {UL_LSFD_MODE, "r--"}, {UL_LSFD_XMODE, "r-----"},
        };
        table_projection_check("lsfd known zero", &fd, ul_lsfd_columns,
            UL_LSFD_COLUMNS, ul_lsfd_field, descriptor, array_count(descriptor));
        static const string_address modes[] = {"r--", "-w-", "rw-"};
        static const string_address xmodes[] = {"r-----", "-w----", "rw----"};
        p8 scratch[96];
        for (positive access = 0; access < 3; access++)
        {
                fd.access = access;
                check("lsfd access modes retain custom projection",
                      string_equals(ul_lsfd_field(&fd, UL_LSFD_MODE, scratch), modes[access]) &&
                      string_equals(ul_lsfd_field(&fd, UL_LSFD_XMODE, scratch), xmodes[access]));
        }
        ul_lsblk_device device = {0};
        ul_lsblk_row device_view = {&device, 0, false};
        static const table_projection absent[] = {
            {UL_LSBLK_TYPE, ""}, {UL_LSBLK_MOUNTPOINT, ""}, {UL_LSBLK_MOUNTPOINTS, ""},
            {UL_LSBLK_FSTYPE, ""}, {UL_LSBLK_FSVER, ""}, {UL_LSBLK_LABEL, ""},
            {UL_LSBLK_UUID, ""}, {UL_LSBLK_PARTUUID, ""}, {UL_LSBLK_PARTLABEL, ""},
            {UL_LSBLK_OWNER, ""}, {UL_LSBLK_GROUP, ""}, {UL_LSBLK_MODE, ""},
            {UL_LSBLK_SCHED, ""}, {UL_LSBLK_TRAN, ""}, {UL_LSBLK_VENDOR, ""},
            {UL_LSBLK_MODEL, ""}, {UL_LSBLK_REV, ""}, {UL_LSBLK_SERIAL, ""},
            {UL_LSBLK_HCTL, ""}, {UL_LSBLK_RQSIZE, ""}, {UL_LSBLK_RA, "0"},
            {UL_LSBLK_FSAVAIL, ""}, {UL_LSBLK_FSUSE, ""},
        };
        table_projection_check("lsblk missing", &device_view, ul_lsblk_columns,
            UL_LSBLK_COLUMNS, ul_lsblk_field, absent, array_count(absent));
        device.fs_measured = true;
        device.request_size = positive_max;
        static const table_projection measured[] = {
            {UL_LSBLK_FSAVAIL, "0"}, {UL_LSBLK_FSUSE, "0%"},
            {UL_LSBLK_RQSIZE, "18446744073709551615"},
        };
        table_projection_check("lsblk measured zero", &device_view, ul_lsblk_columns,
            UL_LSBLK_COLUMNS, ul_lsblk_field, measured, array_count(measured));
        ul_ipc_row ipc = {.id = p64_max, .count = p64_max, .pid_one = p64_max};
        p8 selected[] = {UL_IPC_ID, UL_IPC_MSGS, UL_IPC_LSPID, UL_IPC_STATUS};
        string_address expected = "{\n   \"ipc\": [\n      {\n"
            "         \"id\": \"18446744073709551615\",\n"
            "         \"msgs\": \"18446744073709551615\",\n"
            "         \"lspid\": \"18446744073709551615\",\n"
            "         \"status\": null\n      }\n   ]\n}\n";
        table_reset();
        ul_table_json("ipc", &ipc, sizeof(ipc), 1, ul_ipc_columns,
                      selected, array_count(selected), ul_ipc_field);
        check("IPC decimal projections keep JSON strings and nulls",
              !table_overflow && table_used == string_length(expected) &&
              !memory_compare(table_output, expected, table_used));
}

/* Build the legacy prlimit projection independently: width comes from the
   complete resource set, duplicate selected columns remain significant,
   only spaces need escaping in these fixed metadata/decimal fields. */
static fn table_limit_checks(void)
{
        ul_limit_row rows[UL_RESOURCES];
        p8 scratch[96], expected[8192], selected[5];
        static const p64 values[] = {0, 1, 999, 10000000000ull,
                                     UL_LIMIT_INFINITE - 1, UL_LIMIT_INFINITE};
        for (positive shape = 0; shape < 32; shape++)
        {
                positive count = shape % (UL_RESOURCES + 1);
                for (positive row = 0; row < count; row++)
                        rows[row] = (ul_limit_row){ul_resources + row,
                            {values[(row + shape) % array_count(values)],
                             values[(row * 3 + shape) % array_count(values)]}};
                for (positive field = 0; field < array_count(selected); field++)
                        selected[field] = (p8)((field * (shape % 5) + shape) % 5);
                for (positive mode = 0; mode < 4; mode++)
                {
                        p8 headings = mode & 1, raw = mode & 2;
                        positive widths[5] = {0}, used = 0;
                        for (positive field = 0; field < 5; field++)
                        {
                                if (headings)
                                        widths[field] = string_length(ul_limit_definitions[field].heading);
                                for (positive row = 0; row < count; row++)
                                        widths[field] = max(widths[field],
                                            string_length(ul_limit_field(rows + row, field, scratch)));
                        }
                        for (positive row = 0; count && row < count + headings; row++)
                        {
                                p8 heading = headings && !row;
                                for (positive field = 0; field < 5; field++)
                                {
                                        p8 column = selected[field];
                                        string_address value = heading
                                            ? ul_limit_definitions[column].heading
                                            : ul_limit_field(rows + row - headings, column, scratch);
                                        positive length = string_length(value);
                                        p8 number = column == UL_LIMIT_SOFT || column == UL_LIMIT_HARD;
                                        positive pad = raw || (field == 4 && !number) ? 0
                                            : widths[column] - length;
                                        if (field) expected[used++] = ' ';
                                        if (number) while (pad) expected[used++] = ' ', pad--;
                                        for (positive at = 0; at < length; at++)
                                                if (raw && value[at] == ' ')
                                                {
                                                        memory_copy_apart(expected + used, "\\x20", 4);
                                                        used += 4;
                                                }
                                                else expected[used++] = value[at];
                                        while (pad) expected[used++] = ' ', pad--;
                                }
                                expected[used++] = '\n';
                        }
                        table_reset();
                        ul_limit_table(rows, count, selected, 5, headings, raw);
                        check("prlimit shared rendering exact bytes", !table_overflow &&
                              table_used == used && !memory_compare(table_output, expected, used));
                }
        }
        positive count = 0;
        p8 refused[UL_COLUMN_NAME];

        //      Answers which of the three things happened, as the shared
        //      reader does, and hands back the name it could not place.
        check("prlimit preserves duplicate selected columns",
              ul_limit_columns("soft,SOFT,units", selected, &count,
                               refused) == UL_COLUMNS_OK &&
              count == 3 && selected[0] == UL_LIMIT_SOFT &&
              selected[1] == UL_LIMIT_SOFT && selected[2] == UL_LIMIT_UNITS);
        check("prlimit rejects appended defaults syntax",
              ul_limit_columns("+SOFT", selected, &count,
                               refused) == UL_COLUMNS_UNKNOWN);
        check("prlimit refuses an empty list without a word",
              ul_limit_columns("", selected, &count, refused) == UL_COLUMNS_EMPTY);
        check("prlimit names the column it does not know",
              ul_limit_columns("soft,nosuch", selected, &count,
                               refused) == UL_COLUMNS_UNKNOWN &&
              !string_compare(refused, "nosuch"));
}

static string_address table_pair_field(address_any opaque, p8 column,
                                       p8 address_to scratch)
{
        (void)scratch;
        return ((string_address address_to)opaque)[column];
}

static fn table_printable_checks(void)
{
        p8 value[97], expected[8192], selected[] = {0, 1, 0};
        string_address row[] = {value, "one\ntwo\n"};
        ul_table_column columns[] = {
            {"value", "VALUE", 0, false, TABLE_STRING},
            {"lines", "LINES", 0, false, TABLE_STRING, .multiline=true},
        };
        for (positive length = 0; length <= 95; length++)
        {
                for (positive at = 0; at < length; at++) value[at] = (p8)(' ' + at);
                value[length] = 0;
                for (positive mode = 0; mode < 8; mode++)
                {
                        columns[0].number = (mode & 4) != 0;
                        positive wanted = 0;
                        for (positive fast = 0; fast < 2; fast++)
                        {
                                columns[0].printable = fast;
                                table_reset();
                                ul_table_out(row, sizeof(row), 1, columns, 2,
                                             selected, 3, mode & 1, mode & 2, table_pair_field);
                                if (!fast)
                                {
                                        wanted = table_used;
                                        memory_copy_apart(expected, table_output, wanted);
                                }
                                else
                                        check("printable fields match escaped path with multiline neighbors",
                                              !table_overflow && table_used == wanted &&
                                              !memory_compare(table_output, expected, wanted));
                        }
                }
        }
}

static fn table_ipcs_checks(void)
{
        ul_ipc_row rows[3] = {
            {.key=1, .id=11, .mode=0600, .uid=4294967295u, .size=17,
             .count=2, .type=UL_IPC_MESSAGE},
            {.key=2, .id=22, .mode=0600, .uid=4294967295u, .size=4096,
             .count=3, .type=UL_IPC_SHARED},
            {.key=3, .id=33, .mode=0600, .uid=4294967295u,
             .count=4, .type=UL_IPC_SEMAPHORE},
        };
        /*
                Every column is padded to its width, the last one included,
                and so is a field that is empty -- which is why the status
                column of a segment nobody has marked runs to the end of the
                line. That is util-linux, not a choice of ours: with a real
                queue, segment and semaphore made by ipcmk, ipcs -q, -m, -s,
                -a and --bytes are byte for byte what this tree prints,
                trailing blanks and all.
        */
        static const string_address expected[] = {
            "\n------ Message Queues --------\n"
            "key        msqid      owner      perms      used-bytes   messages    \n"
            "0x00000001 11         4294967295 600        17           2           \n",
            "\n------ Shared Memory Segments --------\n"
            "key        shmid      owner      perms      bytes      nattch     status      \n"
            "0x00000002 22         4294967295 600        4096       3                       \n",
            "\n------ Semaphore Arrays --------\n"
            "key        semid      owner      perms      nsems     \n"
            "0x00000003 33         4294967295 600        4         \n",
        };
        static const string_address empty[] = {
            "\n------ Message Queues --------\n"
            "key        msqid      owner      perms      used-bytes   messages    \n",
            "\n------ Shared Memory Segments --------\n"
            "key        shmid      owner      perms      bytes      nattch     status      \n",
            "\n------ Semaphore Arrays --------\n"
            "key        semid      owner      perms      nsems     \n",
        };
        ul_ipc.rows = rows;
        ul_ipc_bytes = ul_ipc_numeric_permissions = true;
        // Revisit each resource after changing the ID heading for another.
        for (positive visit = 0; visit < 18; visit++)
        {
                p8 type = (p8)(visit % 3);
                ul_ipc.count = visit < 9 ? array_count(rows) : 0;
                string_address want = visit < 9 ? expected[type] : empty[type];
                table_reset();
                ul_ipcs_table(type);
                check("legacy IPC shared field IDs", !table_overflow &&
                      table_used == string_length(want) &&
                      !memory_compare(table_output, want, table_used));
        }
}

/* Independent fixed-field oracle: decimal digits are divided out backwards,
   so integration checks do not reproduce the new prepared-field machinery. */
static positive table_fixed_expected(p8 address_to into, positive value,
                                      positive places, positive width, p8 sign)
{
        p8 reverse[20];
        positive divisor = places == 1 ? 10 : places == 2 ? 100 : 1000000;
        positive whole = value / divisor, used = 0, at = 0;
        do { reverse[used++] = '0' + whole % 10; whole /= 10; } while (whole);
        positive body = used + places + 1 + (sign != 0);
        while (at + body < width) into[at++] = ' ';
        if (sign) into[at++] = sign;
        while (used) into[at++] = reverse[--used];
        into[at++] = '.';
        for (positive step = divisor / 10; step; step /= 10)
                into[at++] = '0' + (value / step) % 10;
        return at;
}

static fn table_fixed_callers(void)
{
        static const positive values[] = {
            0, 1, 9, 10, 99, 100, 999999, 1000000, 9999999,
            10000000, (positive)bipolar_max, (positive)bipolar_max + 1,
            positive_max,
        };
        for (positive i = 0; i < array_count(values); i++)
        {
                p8 expected[128];
                for (positive places = 1; places <= 2; places++)
                        for (positive width = 0; width <= 30; width++)
                        {
                                positive length = table_fixed_expected(
                                    expected, values[i], places, width, 0);
                                table_reset();
                                monitor_fixed(table_capture, values[i], places, width);
                                check("monitor shared fixed field", !table_overflow &&
                                      table_used == length &&
                                      !memory_compare(table_output, expected, length));
                        }
                for (positive mode = 0; mode < 4; mode++)
                {
                        p8 signed_value = mode & 1, negative = mode & 2;
                        expected[0] = '[';
                        positive length = 1 + table_fixed_expected(expected + 1,
                            values[i], 6, signed_value ? 11 : 12,
                            signed_value ? negative ? '-' : '+' : 0);
                        expected[length++] = ']'; expected[length++] = ' ';
                        text_out_used = 0;
                        tools_dmesg_timestamp(values[i], signed_value, negative);
                        check("dmesg shared signed fixed field",
                              text_out_used == length &&
                              !memory_compare(text_out_buffer, expected, length));

                        positive delta = values[array_count(values) - 1 - i];
                        length = 1 + table_fixed_expected(expected + 1,
                            values[i], 6, 12, 0);
                        expected[length++] = ' '; expected[length++] = '<';
                        length += table_fixed_expected(expected + length,
                            delta, 6, 12, negative ? '-' : 0);
                        expected[length++] = '>'; expected[length++] = ']';
                        expected[length++] = ' ';
                        text_out_used = 0;
                        tools_dmesg_timestamp_delta(values[i], delta, negative);
                        check("dmesg shared delta fixed field",
                              text_out_used == length &&
                              !memory_compare(text_out_buffer, expected, length));
                }
        }
        text_out_used = 0;
}

/* Independent former traversal: scan the sorted records for each parent.
   The implementation builds adjacency once and writes pointer views. */
static ul_lsblk_row table_tree_expected[65];
static positive table_tree_count;
static fn table_tree_scan(ul_lsblk_device address_to device,
                           positive depth, p8 last, p8 dependencies)
{
        if (table_tree_count == ul_lsblk.capacity)
                return;
        table_tree_expected[table_tree_count++] = (ul_lsblk_row){device, depth, last};
        if (!dependencies || depth >= 32)
                return;
        positive children = 0, seen = 0;
        for (positive i = 0; i < ul_lsblk.count; i++)
                children += ul_lsblk.order[i]->parent == device;
        for (positive i = 0; i < ul_lsblk.count; i++)
                if (ul_lsblk.order[i]->parent == device)
                        table_tree_scan(ul_lsblk.order[i], depth + 1,
                                        ++seen == children, true);
}

static fn table_tree_checks(void)
{
        ul_lsblk_snapshot saved = ul_lsblk;
        ul_lsblk_device devices[65];
        ul_lsblk_device address_to order[65];
        ul_lsblk_row rows[65];
        positive random = 0x5a82c673;
        for (positive count = 1; count <= 65; count++)
        for (positive round = 0; round < 32; round++)
        {
                memory_zero(devices, sizeof(devices));
                for (positive i = 0; i < count; i++)
                {
                        XORSHIFT64(random);
                        devices[i].parent = i && (random & 3)
                            ? devices + random % i : null;
                        order[i] = devices + i;
                }
                for (positive i = count; i > 1; i--)
                {
                        positive at = random % i;
                        ul_lsblk_device address_to swap = order[at];
                        order[at] = order[i - 1]; order[i - 1] = swap;
                        random = random * 6364136223846793005ull + 1;
                }
                ul_lsblk = (ul_lsblk_snapshot){.devices = devices, .order = order,
                    .rows = rows, .capacity = round & 1 ? count : count / 2,
                    .count = count};
                table_tree_count = 0;
                ul_lsblk_link_children();
                for (positive i = 0; i < count; i++)
                        if (!order[i]->parent)
                        {
                                p8 dependencies = (round & 2) != 0;
                                table_tree_scan(order[i], 0, false, dependencies);
                                ul_lsblk_append(order[i], 0, false, dependencies);
                        }
                p8 same = ul_lsblk.row_count == table_tree_count;
                for (positive i = 0; i < table_tree_count; i++)
                        same &= rows[i].device == table_tree_expected[i].device &&
                                rows[i].depth == table_tree_expected[i].depth &&
                                rows[i].last == table_tree_expected[i].last;
                check("linked tree preserves sibling order, depth, truncation and dependency policy", same);
        }
        ul_lsblk = (ul_lsblk_snapshot){.devices = devices, .order = order,
            .rows = rows, .capacity = 65, .count = 65};
        for (positive i = 0; i < 65; i++)
        {
                order[i] = devices + i;
                devices[i].parent = i ? devices + i - 1 : null;
        }
        ul_lsblk_link_children();
        ul_lsblk_append(devices, 0, false, true);
        check("linked tree retains the depth ceiling", ul_lsblk.row_count == 33 && rows[32].depth == 32);
        devices[0].parent = devices;
        ul_lsblk.row_count = table_tree_count = 0;
        ul_lsblk_link_children();
        table_tree_scan(devices, 0, false, true);
        ul_lsblk_append(devices, 0, false, true);
        p8 same = ul_lsblk.row_count == table_tree_count;
        for (positive i = 0; i < table_tree_count; i++)
                same &= rows[i].device == table_tree_expected[i].device &&
                        rows[i].depth == table_tree_expected[i].depth &&
                        rows[i].last == table_tree_expected[i].last;
        check("linked tree keeps cycles bounded by the existing depth and row limits", same);
        ul_lsblk = saved;
}

/*
        Generated tables against an independent renderer. The oracle keeps
        the layout rules and spells every byte on its own: the class from the
        byte's value rather than the assembly's table, a width by counting, a
        field's line by walking from the field's start, JSON from a per-byte
        choice. The tables mix multi-line fields, controls, clipped and
        wrapped widths, pairs, fixed widths, reordered and repeated columns,
        rows wider than the row's held fields and scratch-backed cells, whose
        scratch is rewritten whole on every projection so a stale span shows.
*/
enum { TABLE_GENERATED_COLUMNS = 6, TABLE_GENERATED_ROWS = 5, TABLE_GENERATED_SHOWN = 20 };

typedef struct
{
        p8 text[TABLE_GENERATED_ROWS * TABLE_GENERATED_COLUMNS][96];
        positive length[TABLE_GENERATED_ROWS * TABLE_GENERATED_COLUMNS];
        p8 heading[TABLE_GENERATED_COLUMNS][12], name[TABLE_GENERATED_COLUMNS][12];
        positive heading_length[TABLE_GENERATED_COLUMNS], name_length[TABLE_GENERATED_COLUMNS];
        positive minimum[TABLE_GENERATED_COLUMNS];
        p8 flags[TABLE_GENERATED_COLUMNS], escape[TABLE_GENERATED_COLUMNS], json[TABLE_GENERATED_COLUMNS];
        positive rows, columns;
        p8 scratch;
} table_generated;

static p8 table_generated_output[1 << 20], table_oracle_output[1 << 20];
static positive table_generated_used, table_oracle_used;
static p8 table_generated_overflow, table_oracle_overflow;

static fn table_generated_capture(address_any bytes, positive length)
{
        if (length > sizeof(table_generated_output) - table_generated_used)
        {
                table_generated_overflow = true;
                return;
        }
        memory_copy_apart(table_generated_output + table_generated_used, bytes, length);
        table_generated_used += length;
}

static fn table_oracle_put(address_any bytes, positive length)
{
        p8 address_to from = bytes;
        for (positive at = 0; at < length; at++)
                if (table_oracle_used < sizeof(table_oracle_output))
                        table_oracle_output[table_oracle_used++] = from[at];
                else
                        table_oracle_overflow = true;
}

static fn table_oracle_spaces(positive count)
{
        while (count--)
                table_oracle_put(" ", 1);
}

static table_cell table_generated_cell(address_any context, positive row,
                                       positive column, p8 address_to scratch)
{
        table_generated address_to table = context;
        p8 heading = row == TABLE_HEADING, name = row == TABLE_NAME;
        positive at = heading || name ? 0 : row * TABLE_GENERATED_COLUMNS + column;
        p8 address_to text = heading ? table->heading[column] : name ? table->name[column]
                                                           : table->text[at];
        positive length = heading ? table->heading_length[column]
            : name ? table->name_length[column] : table->length[at];
        if (table->scratch)
        {
                memory_copy_apart(scratch, text, length);
                memory_fill(scratch + length, (p8)(0xa0 + row + column), 96 - length);
                text = scratch;
        }
        return (table_cell){.text = {text, length}, .minimum = table->minimum[column],
            .escape = table->escape[column], .json = table->json[column],
            .flags = heading || name ? table->flags[column] & ~TABLE_LINES
                                     : table->flags[column]};
}

static positive table_generated_write(writer output, address_any context,
                                      positive row, positive column)
{
        p8 scratch[96];
        table_cell cell = table_generated_cell(context, row, column, scratch);
        if (cell.text.length)
                output(cell.text.bytes, cell.text.length);
        return cell.text.length;
}

static p8 table_oracle_escapes(p8 byte, p8 policy)
{
        if (policy & 64)
                return byte < ' ' || byte == '"' || byte == '\\';
        return ((policy & HEX_CONTROL) && ((byte < ' ' && byte != '\t') || byte == 127)) ||
               ((policy & HEX_TAB) && byte == '\t') ||
               ((policy & HEX_SPACE) && byte == ' ') ||
               ((policy & HEX_QUOTE) && byte == '"') ||
               ((policy & HEX_SLASH) && byte == '\\') ||
               ((policy & HEX_HIGH) && byte >= 128);
}

static positive table_oracle_width(p8 address_to bytes, positive length, p8 policy)
{
        positive width = 0;
        for (positive at = 0; at < length; at++)
                width += policy && table_oracle_escapes(bytes[at], policy) ? 4 : 1;
        return width;
}

static fn table_oracle_hex(p8 address_to bytes, positive length, p8 policy, positive limit)
{
        for (positive at = 0; at < length && limit; at++)
        {
                p8 spelled[4] = {bytes[at]};
                positive size = 1;
                if (policy && table_oracle_escapes(bytes[at], policy))
                {
                        spelled[0] = '\\';
                        spelled[1] = 'x';
                        spelled[2] = table_hex_digit(bytes[at] >> 4);
                        spelled[3] = table_hex_digit(bytes[at] & 15);
                        size = 4;
                }
                positive kept = min(size, limit);
                table_oracle_put(spelled, kept);
                limit -= kept;
        }
}

static fn table_oracle_json(p8 address_to bytes, positive length, p8 lower, p8 short_escapes)
{
        table_oracle_put("\"", 1);
        for (positive at = 0; at < length; at++)
        {
                p8 byte = bytes[at], spelled[6] = {'\\', byte};
                positive size = 2;
                if (byte < ' ')
                {
                        p8 letter = byte == '\b' ? 'b' : byte == '\f' ? 'f' : byte == '\n' ? 'n'
                            : byte == '\r' ? 'r' : byte == '\t' ? 't' : 0;
                        if (short_escapes && letter)
                                spelled[1] = letter;
                        else
                        {
                                spelled[1] = 'u';
                                spelled[2] = spelled[3] = '0';
                                spelled[4] = table_hex_digit(byte >> 4);
                                spelled[5] = table_hex_digit(byte & 15);
                                size = 6;
                        }
                }
                else if (byte != '"' && byte != '\\')
                {
                        spelled[0] = lower && byte >= 'A' && byte <= 'Z' ? byte + 32 : byte;
                        size = 1;
                }
                table_oracle_put(spelled, size);
        }
        table_oracle_put("\"", 1);
}

static positive table_oracle_index(const table_view address_to view, positive shown)
{
        if (!view->order)
                return shown;
        return view->order_size == 1 ? ((p8 address_to)view->order)[shown]
                                     : ((positive address_to)view->order)[shown];
}

static byte_span table_oracle_part(table_cell cell, positive width, positive part)
{
        p8 address_to bytes = cell.text.bytes;
        positive length = cell.text.length;
        if (cell.flags & TABLE_TRUNCATE)
                return (byte_span){bytes, part ? 0 : min(length, width)};
        if (cell.flags & TABLE_WRAP)
                return part * width < length
                    ? (byte_span){bytes + part * width, min(width, length - part * width)}
                    : (byte_span){bytes, 0};
        if (!(cell.flags & TABLE_LINES))
                return (byte_span){bytes, part ? 0 : length};
        positive start = 0, seen = 0, stop;
        for (positive at = 0; at < length && seen < part; at++)
                if (bytes[at] == '\n')
                {
                        seen++;
                        start = at + 1;
                }
        if (seen < part)
                return (byte_span){bytes, 0};
        for (stop = start; stop < length && bytes[stop] != '\n'; stop++)
                ;
        return (byte_span){bytes + start, stop - start};
}

static positive table_oracle_cell_width(table_cell cell)
{
        if (cell.flags & TABLE_WIDTH_BYTES)
                return cell.text.length;
        positive widest = 0, width = 0;
        for (positive at = 0; at < cell.text.length; at++)
                if (cell.text.bytes[at] == '\n' && (cell.flags & TABLE_LINES))
                {
                        widest = max(widest, width);
                        width = 0;
                }
                else
                        width += table_oracle_width(cell.text.bytes + at, 1, cell.escape);
        return max(widest, width);
}

static fn table_oracle_measure(const table_view address_to view, positive rows,
    p8 headings, p8 declared, p8 free_widths, positive all_columns,
    positive address_to widths, positive address_to second)
{
        positive fields = all_columns ? all_columns : view->count;
        for (positive shown = 0; shown < fields; shown++)
        {
                positive col = all_columns ? shown : table_oracle_index(view, shown);
                p8 scratch[96];
                table_cell heading = table_generated_cell(view->context, TABLE_HEADING, col, scratch);
                widths[col] = max(declared ? heading.minimum : 0,
                                  headings ? table_oracle_cell_width(heading) : 0);
                if (second)
                        second[col] = 0;
        }
        for (positive row = 0; row < rows; row++)
                for (positive shown = 0; shown < fields; shown++)
                {
                        positive col = all_columns ? shown : table_oracle_index(view, shown);
                        p8 scratch[96];
                        table_cell cell = table_generated_cell(view->context, row, col, scratch);
                        positive length = table_oracle_cell_width(cell);
                        if (length > widths[col])
                        {
                                if (second)
                                        second[col] = widths[col];
                                widths[col] = length;
                        }
                        else if (second)
                                second[col] = max(second[col], length);
                        if (!headings && !free_widths && length)
                                widths[col] = max(widths[col], cell.minimum);
                }
}

static fn table_oracle_row(const table_view address_to view, positive row,
                           positive address_to widths)
{
        positive parts = 1;
        for (positive shown = 0; widths && view->multipart && shown < view->count; shown++)
        {
                positive col = table_oracle_index(view, shown), have = 1;
                p8 scratch[96];
                table_cell cell = table_generated_cell(view->context, row, col, scratch);
                if (cell.flags & TABLE_LINES)
                        for (positive at = 0; at < cell.text.length; at++)
                                have += cell.text.bytes[at] == '\n';
                else if ((cell.flags & TABLE_WRAP) && widths[col] && cell.text.length)
                        have = (cell.text.length + widths[col] - 1) / widths[col];
                parts = max(parts, have);
        }
        for (positive part = 0; part < parts; part++)
        {
                for (positive shown = 0; shown < view->count; shown++)
                {
                        positive col = table_oracle_index(view, shown);
                        p8 last = shown + 1 == view->count;
                        p8 measured = widths || view->fixed_widths;
                        p8 scratch[96];
                        table_cell cell = table_generated_cell(view->context, row, col, scratch);
                        positive width = widths ? widths[col] : view->fixed_widths ? cell.minimum : 0;
                        byte_span text = widths && (parts > 1 ||
                            (cell.flags & (TABLE_WRAP | TABLE_TRUNCATE)))
                            ? table_oracle_part(cell, width, part) : cell.text;
                        positive length = measured
                            ? table_oracle_width(text.bytes, text.length, cell.escape) : 0;
                        positive limit = measured && (cell.flags & TABLE_CLIP_OUTPUT) && !last
                            ? width : positive_max;
                        length = min(length, limit);
                        positive pad = width > length ? width - length : 0;
                        if (last && !view->pad_last &&
                            (!(cell.flags & TABLE_RIGHT) || (!length && !view->pad_empty)))
                                pad = 0;
                        if (last && view->pad_last && row != TABLE_HEADING)
                                pad = width + view->pad_extra > length
                                    ? width + view->pad_extra - length : 0;
                        if (view->pairs)
                        {
                                p8 name_scratch[96];
                                table_cell name = table_generated_cell(view->context,
                                    TABLE_HEADING, col, name_scratch);
                                table_oracle_put(name.text.bytes, name.text.length);
                                table_oracle_put("=\"", 2);
                        }
                        if (cell.flags & TABLE_RIGHT)
                                table_oracle_spaces(pad);
                        if (view->write)
                                table_generated_write(table_oracle_put, view->context, row, col);
                        else
                                table_oracle_hex(text.bytes, text.length, cell.escape, limit);
                        if (!(cell.flags & TABLE_RIGHT))
                                table_oracle_spaces(pad);
                        if (view->pairs)
                                table_oracle_put("\"", 1);
                        if (last)
                                continue;
                        positive separator = 0;
                        while (view->separator[separator])
                                separator++;
                        if ((cell.flags & TABLE_OVERFLOW) && !(cell.flags & TABLE_RIGHT) &&
                            length > width)
                        {
                                table_oracle_put("\n", 1);
                                for (positive prior = 0; prior <= shown; prior++)
                                {
                                        if (prior)
                                                table_oracle_put(view->separator, separator);
                                        table_oracle_spaces(widths[table_oracle_index(view, prior)]);
                                }
                        }
                        table_oracle_put(view->separator, separator);
                }
                table_oracle_put("\n", 1);
        }
}

static fn table_oracle_json_table(const table_view address_to view, positive rows,
                                  p8 column_style)
{
        table_oracle_put("{\n   ", 5);
        table_oracle_json((p8 address_to)"generated", 9, false, column_style);
        table_oracle_put(": [", 3);
        for (positive row = 0; row < rows; row++)
        {
                table_oracle_put(row ? ",{\n" : "\n      {\n", row ? 3 : 9);
                for (positive field = 0; field < view->count; field++)
                {
                        positive col = table_oracle_index(view, field);
                        p8 scratch[96];
                        if (field)
                                table_oracle_put(",\n", 2);
                        table_oracle_put("         ", 9);
                        table_cell key = table_generated_cell(view->context, TABLE_NAME, col, scratch);
                        table_oracle_json(key.text.bytes, key.text.length, column_style, column_style);
                        table_oracle_put(": ", 2);
                        table_cell cell = table_generated_cell(view->context, row, col, scratch);
                        p8 address_to value = cell.text.bytes;
                        positive length = cell.text.length;
                        if ((cell.json == TABLE_NULL_STRING || cell.json == TABLE_NULL_NUMBER || cell.json == TABLE_NUMBER) && !length)
                                table_oracle_put("null", 4);
                        else if (cell.json == TABLE_BOOLEAN)
                        {
                                p8 no = (length == 1 && value[0] == '0') ||
                                          (length == 2 && value[0] == 'n' && value[1] == 'o');
                                table_oracle_put(no ? "false" : "true", no ? 5 : 4);
                        }
                        else if (cell.json == TABLE_NUMBER || cell.json == TABLE_NULL_NUMBER)
                                table_oracle_put(value, length);
                        else
                                table_oracle_json(value, length, false, column_style);
                }
                if (view->count || !column_style)
                        table_oracle_put("\n", 1);
                table_oracle_put("      }", 7);
        }
        if (!rows)
                table_oracle_put("\n", 1);
        table_oracle_put("\n   ]\n}\n", 8);
}

static positive table_generated_random = 0x2545f4914f6cdd1dull;

static positive table_generated_next(void)
{
        XORSHIFT64(table_generated_random);
        return table_generated_random;
}

/* Bytes for a generated field: shape 0 dense controls and quotes, 1 literal
   and replacement alternating, 2 literal runs either side of the scan's
   hand-back, 3 newlines packed, otherwise mostly letters with a few. */
static fn table_generated_bytes(p8 address_to into, positive length, positive shape)
{
        static const p8 unusual[] = {'\n', '\t', 1, 27, 127, 0xc3, 0xa9, ' ', '"', '\\',
                                     '\b', '\f', '\r', 0, 0x80, 0xff};
        static const p8 letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_/.:";
        positive run = 14 + table_generated_next() % 5, since = 0;
        for (positive at = 0; at < length; at++)
        {
                positive pick = table_generated_next();
                p8 odd = unusual[(pick >> 8) % sizeof(unusual)];
                p8 plain = letters[(pick >> 16) % (sizeof(letters) - 1)];
                into[at] = shape == 0 ? ((pick & 3) ? odd : plain)
                    : shape == 1 ? (at & 1 ? odd : plain)
                    : shape == 2 ? (since++ == run ? (since = 0, odd) : plain)
                    : shape == 3 ? ((pick & 1) ? '\n' : plain)
                    : ((pick & 15) ? plain : odd);
        }
}

static fn table_generated_checks(void)
{
        static table_generated table;
        static const p8 policies[] = {0, HEX_CONTROL | HEX_TAB, HEX_HIGH, 63,
            HEX_CONTROL | HEX_TAB | HEX_SLASH | HEX_HIGH | HEX_SPACE, HEX_CONTROL,
            HEX_TAB | HEX_QUOTE};
        static const string_address separators[] = {" ", "  ", "|", " | ", ""};
        for (positive round = 0; round < 3000; round++)
        {
                p8 wide = !(table_generated_next() % 5);
                positive longest = wide ? 32 : 95;
                table.rows = table_generated_next() % (wide ? 3 : TABLE_GENERATED_ROWS + 1);
                table.columns = 1 + table_generated_next() % TABLE_GENERATED_COLUMNS;
                table.scratch = table_generated_next() & 1;
                for (positive col = 0; col < table.columns; col++)
                {
                        table.flags[col] = (p8)(table_generated_next() & 127);
                        if (table_generated_next() & 1)
                                table.flags[col] &= TABLE_RIGHT | TABLE_LINES | TABLE_CLIP_OUTPUT;
                        table.minimum[col] = table_generated_next() % 14;
                        table.escape[col] = policies[table_generated_next() % sizeof(policies)];
                        table.json[col] = (p8)(table_generated_next() % 5);
                        table.heading_length[col] = table_generated_next() % 12;
                        table.name_length[col] = table_generated_next() % 12;
                        table_generated_bytes(table.heading[col], table.heading_length[col], 4);
                        table_generated_bytes(table.name[col], table.name_length[col], 4);
                }
                for (positive row = 0; row < table.rows; row++)
                        for (positive col = 0; col < table.columns; col++)
                        {
                                positive at = row * TABLE_GENERATED_COLUMNS + col;
                                positive shape = table_generated_next() % 6;
                                positive size = table_generated_next() % 4;
                                positive length = size == 0 ? 0 : size == 1
                                    ? table_generated_next() % (longest + 1)
                                    : table_generated_next() % 24;
                                if (shape == 3)
                                        length = min(length, 32);
                                table.length[at] = min(length, longest);
                                table_generated_bytes(table.text[at], table.length[at], shape);
                        }
                positive shown = wide ? 7 + table_generated_next() % 14
                                      : 1 + table_generated_next() % 8;
                p8 order8[TABLE_GENERATED_SHOWN];
                positive order64[TABLE_GENERATED_SHOWN];
                for (positive at = 0; at < shown; at++)
                        order8[at] = (p8)(order64[at] = table_generated_next() % table.columns);
                p8 wide_order = table_generated_next() & 1;
                p8 identity = !wide && !(table_generated_next() % 4);
                table_view view = {.output = table_generated_capture, .context = &table,
                    .cell = table_generated_cell,
                    .order = identity ? null : wide_order ? (address_any)order64 : (address_any)order8,
                    .order_size = wide_order ? sizeof(order64[0]) : 1,
                    .count = identity ? table.columns : shown,
                    .separator = separators[table_generated_next() % 5],
                    .pad_last = table_generated_next() & 1, .pad_empty = table_generated_next() & 1,
                    .pairs = !(table_generated_next() % 4),
                    .multipart = wide || (table_generated_next() & 1),
                    .fixed_widths = !(table_generated_next() % 4),
                    .pad_extra = table_generated_next() % 3};
                if (!(table_generated_next() % 6))
                        view.write = table_generated_write;
                positive mode = table_generated_next() & 255;
                p8 headings = mode & 1, same_widths = true;
                table_generated_used = table_oracle_used = 0;
                table_generated_overflow = table_oracle_overflow = false;
                if (mode & 32)
                {
                        table_json(&view, (byte_span){(p8 address_to)"generated", 9},
                                   table.rows, (mode & 64) != 0);
                        table_oracle_json_table(&view, table.rows, (mode & 64) != 0);
                }
                else if (mode & 16)
                {
                        // Overflow lines index the widths, which a direct row has none of.
                        for (positive col = 0; col < table.columns; col++)
                                table.flags[col] &= ~TABLE_OVERFLOW;
                        if (headings)
                        {
                                table_row(&view, TABLE_HEADING, null);
                                table_oracle_row(&view, TABLE_HEADING, null);
                        }
                        for (positive row = 0; row < table.rows; row++)
                        {
                                table_row(&view, row, null);
                                table_oracle_row(&view, row, null);
                        }
                }
                else
                {
                        // Columns no shown field names are left alone by both.
                        positive widths[TABLE_GENERATED_COLUMNS] = {0}, second[TABLE_GENERATED_COLUMNS] = {0};
                        positive wanted[TABLE_GENERATED_COLUMNS] = {0}, wanted_second[TABLE_GENERATED_COLUMNS] = {0};
                        positive all = mode & 8 ? table.columns : 0;
                        table_measure(&view, table.rows, headings, mode & 2, mode & 4, all,
                                      widths, mode & 128 ? second : null);
                        table_oracle_measure(&view, table.rows, headings, mode & 2, mode & 4, all,
                                             wanted, mode & 128 ? wanted_second : null);
                        positive shrink = table_generated_next();
                        for (positive col = 0; col < table.columns; col++)
                        {
                                same_widths &= widths[col] == wanted[col] &&
                                    (!(mode & 128) || second[col] == wanted_second[col]);
                                // Narrower columns wrap and truncate, never below eight.
                                if ((shrink >> col) & 1 && widths[col] > 8)
                                        widths[col] = wanted[col] =
                                            8 + (widths[col] - 8) * ((shrink >> (col + 8)) & 3) / 4;
                        }
                        if (headings)
                        {
                                table_row(&view, TABLE_HEADING, widths);
                                table_oracle_row(&view, TABLE_HEADING, wanted);
                        }
                        for (positive row = 0; row < table.rows; row++)
                        {
                                table_row(&view, row, widths);
                                table_oracle_row(&view, row, wanted);
                        }
                }
                p8 same = same_widths && !table_generated_overflow && !table_oracle_overflow &&
                    table_generated_used == table_oracle_used &&
                    !memory_compare(table_generated_output, table_oracle_output, table_oracle_used);
                if (!same)
                        string_format(log, "generated table round=%p mode=%p widths=%p got=%p wanted=%p prefix=%p\n",
                                      round, mode, (positive)same_widths, table_generated_used,
                                      table_oracle_used,
                                      memory_common_prefix(table_generated_output, table_oracle_output,
                                                           min(table_generated_used, table_oracle_used)));
                check("generated tables match the independent renderer", same);
        }

        static p8 span[720];
        for (positive round = 0; round < 4000; round++)
        {
                positive length = table_generated_next() % sizeof(span);
                table_generated_bytes(span, length, table_generated_next() % 5);
                p8 policy = (p8)(table_generated_next() % 64);
                byte_span text = {span, length};
                check("generated hex widths count every replacement",
                      memory_hex_width(text, policy) == table_oracle_width(span, length, policy));
                positive limit = table_generated_next() % 3 ? positive_max
                    : table_generated_next() % (table_oracle_width(span, length, policy) + 2);
                table_generated_used = table_oracle_used = 0;
                writer_hex_span(table_generated_capture, text, policy, limit);
                table_oracle_hex(span, length, policy, limit);
                check("generated hex spans match the byte-at-a-time spelling",
                      table_generated_used == table_oracle_used &&
                      !memory_compare(table_generated_output, table_oracle_output, table_oracle_used));
                p8 lower = table_generated_next() & 1, short_escapes = table_generated_next() & 1;
                table_generated_used = table_oracle_used = 0;
                writer_json_span(table_generated_capture, text, lower, short_escapes);
                table_oracle_json(span, length, lower, short_escapes);
                p8 same = table_generated_used == table_oracle_used &&
                    !memory_compare(table_generated_output, table_oracle_output, table_oracle_used);
                if (!same)
                        string_format(log, "generated JSON round=%p lower=%p short=%p got=%p wanted=%p\n",
                                      round, (positive)lower, (positive)short_escapes,
                                      table_generated_used, table_oracle_used);
                check("generated JSON spans match in every dialect", same);
        }
}

/*
        A number cell with no text. Writing nothing left the key with no value
        after it, which is not JSON; lsblk -J writes null for the same cell.
*/
static fn table_empty_number_checks(void)
{
        byte_span empty = {(p8 address_to)"", 0};

        table_reset();
        table_json_value(table_capture, empty, TABLE_NUMBER, false);
        check("an empty number cell is written as null",
              !table_overflow && table_used == 4 && !memory_compare(table_output, "null", 4));
}

b32 main(void)
{
        table_layout_checks();
        table_generated_checks();
        table_fixed_callers();
        table_tree_checks();
        name_list_checks();
        table_checks();
        table_projection_checks();
        table_typed_projection_checks();
        table_limit_checks();
        table_printable_checks();
        table_ipcs_checks();
        table_empty_number_checks();
#ifdef TABLE_BENCHMARK
        if (!failures)
        {
                static p8 text[2049];
                for (positive shape = 0; shape < 3; shape++)
                {
                        for (positive at = 0; at < sizeof(text) - 1; at++)
                                text[at] = shape == 1 && at % 64 == 63 ? '\n' :
                                           shape == 2 && at % 64 == 63 ? '\t' : 'a';
                        for (positive mode = 0; mode < 3; mode++)
                        {
                                p64 start = get_cpu_time();
                                for (positive round = 0; round < 4096; round++)
                                        table_render(text, mode);
                                p64 elapsed = get_cpu_time() - start;
                                string_format(log, "table shape=%p mode=%p ticks=%p writes=%p\n",
                                              shape, mode, (positive)elapsed, table_calls);
                        }
                }
        }
#endif
        return test_report(null);
}
#endif /* CHECK_table */

#ifdef CHECK_error
#include "../../src/lib.util.c"

/*
        errno, strerror and the POSIX wrappers, against glibc.

        Two halves. The self-checking half pins the things a reference cannot
        show -- that a wrapper leaves the library's own negative-errno routine
        alone, that the message table has no accidental hole, that the stat
        layout the kernel filled is the one this thinks it is. The transcript
        half prints a stream of lines that CHECK_error_reference, built
        against the real glibc, prints identically, so a diff of the two is
        the check. Anything printed with the TRANSCRIPT prefix is part of that
        contract and must not be reordered.

        Everything goes to descriptor one, including what perror writes,
        because descriptor two is pointed at descriptor one before anything
        runs and the buffered log is flushed before every perror. Without both
        of those the two streams interleave differently on each run and the
        diff is noise.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

//      What the shared transcript body says a line and a flush with: this
//      side's log, and the library's own sync, whose answer is compared.
#define error_trace(...) string_format(log, __VA_ARGS__)
#define error_flush() log_flush()
#define error_sync() sync()

#define SHARED_error_body
#include "../checks.c"
#undef SHARED_error_body

/*
        What a reference cannot show.

        The point of the whole design is that the library's own routines did
        not change, so the first group calls a library routine and a wrapper
        against the same missing file and requires the two different answers.
        The rest are structural: the table has an entry wherever Linux has a
        number, the two holes are holes, and the structure the kernel filled
        has its fields where this file says they are.
*/
static fn error_test_contract(void)
{
        file one;
        error_stat filled;
        b32 number;
        b32 handle;
        positive length;
        p8 room[ERROR_MESSAGE_MAX];

        errno = 0;
        file_new(&one, (string_address)"/nonexistent-dawning-error", FILE_READ);
        check("library routine still returns the kernel number",
              (bipolar)one.handle == -ENOENT);
        check("library routine left errno alone", errno == 0);

        errno = 0;
        handle = open((string_address) "/nonexistent-dawning-error", O_RDONLY);
        check("wrapper returned minus one", handle == -1);
        check("wrapper set errno", errno == ENOENT);

        errno = 0;
        check("faccessat rejects flags it cannot enforce",
              faccessat(AT_FDCWD, (string_address)"/dev/null", F_OK,
                        AT_EACCESS) == -1);
        check("faccessat unsupported flags report ENOTSUP", errno == ENOTSUP);

        //      A second failing call overwrites the first, which is the whole
        //      of what is wrong with errno and is worth having pinned.
        errno = 0;
        close(-1);
        check("second failure overwrote the first", errno == EBADF);

        //      A succeeding call must not clear it. Programs depend on this:
        //      the idiom is to zero errno, do the work, then look.
        errno = EPIPE;
        getpid();
        check("success left errno untouched", errno == EPIPE);

        check("errno is an lvalue through the accessor",
              address_of errno == __errno_location());

        for (number = 0; number <= 134; number++)
        {
                if (number == 41 || number == 58)
                        continue;

                checks++;
                if (is_null(system_error_message(number)))
                {
                        failures++;
                        string_format(log, "  FAIL message missing for %b\n",
                                      (bipolar)number);
                }
        }

        check("41 is a hole", is_null(system_error_message(41)));
        check("58 is a hole", is_null(system_error_message(58)));

        static const bipolar outside[] = {-1, -134, 135, 4095,
                                          bipolar_min, bipolar_max};
        for (positive i = 0; i < array_count(outside); i++)
                check("unknown message has no table address",
                      system_error_message(outside[i]) == null);

        static const bipolar raw[] = {bipolar_min, -4096, -4095, -2, -1,
                                      0, 1, b32_max, (bipolar)b32_max + 1,
                                      bipolar_max};
        for (positive i = 0; i < array_count(raw); i++)
        {
                errno = EPIPE;
                p8 failed = raw[i] >= -4095 && raw[i] < 0;
                check("error conversion preserves wide successful results",
                      error_result(raw[i]) == (failed ? -1 : raw[i]));
                check("only the kernel error window changes errno",
                      errno == (failed ? -raw[i] : EPIPE));
        }

        //      The two spellings that are one number, which a program may
        //      compare either way.
        check("EWOULDBLOCK is EAGAIN", EWOULDBLOCK == EAGAIN);
        check("ENOTSUP is EOPNOTSUPP", ENOTSUP == EOPNOTSUPP);
        check("EDEADLOCK is EDEADLK", EDEADLOCK == EDEADLK);

        //      The layout, checked against a structure the kernel actually
        //      filled rather than against arithmetic done here. /dev/null is a
        //      character device of size zero on every Linux there is.
        memory_fill(&filled, 0xa5, sizeof filled);
        check("stat of /dev/null",
              stat((string_address) "/dev/null", &filled) == 0);
        check("mode says character device", S_ISCHR(filled.st_mode));
        check("size is zero", filled.st_size == 0);
        check("link count is one", filled.st_nlink == 1);
        check("permissions are readable and writable by all",
              (filled.st_mode & 0666) == 0666);

#if X64
        check("structure is the x86_64 size", sizeof(error_stat) == 144);
#else
        check("structure is the asm-generic size", sizeof(error_stat) == 128);
#endif

        //      The GNU extension this deliberately does not have. glibc
        //      allocates a buffer for a null pointer; this refuses, so the
        //      answer cannot be compared against the reference and is pinned
        //      here instead.
        errno = 0;
        check("getcwd of a null buffer is refused", is_null(getcwd(null, 64)));
        check("and refused with EINVAL", errno == EINVAL);

        /*
                strerror_r terminates what it wrote at every size above zero,
                and touches nothing past the size it was given.

                84 is the longest message in the table, forty nine bytes, so
                the loop below crosses both regimes: under fifty the message
                is cut and the terminator lands at the last byte of the
                buffer, at fifty and above the whole message fits and the
                terminator lands right after it with the rest untouched.
        */
        length = string_length(system_error_message(84));

        for (number = 1; number < (b32)sizeof room; number++)
        {
                positive size = (positive)number;
                positive at = length < size ? length : size - 1;

                memory_fill(room, '@', sizeof room);
                strerror_r(84, room, size);

                checks++;
                if (room[at] != end || (at + 1 < sizeof room
                                        && room[at + 1] != '@'))
                {
                        failures++;
                        string_format(log,
                                      "  FAIL terminator wrong at size %b\n",
                                      (bipolar)number);
                }
        }

        //      And must write nothing at all at size zero.
        memory_fill(room, '@', sizeof room);
        strerror_r(84, room, 0);
        check("size zero wrote nothing", room[0] == '@');

        //      Nothing in the table may outgrow the buffer perror builds its
        //      line in, which is the assumption that would fail silently.
        for (number = 0; number <= 134; number++)
        {
                if (is_null(system_error_message(number)))
                        continue;

                checks++;
                if (string_length(system_error_message(number))
                    >= ERROR_MESSAGE_MAX)
                {
                        failures++;
                        string_format(log, "  FAIL message %b is too long\n",
                                      (bipolar)number);
                }
        }

        check("strerror of a known number is the table entry",
              strerror(ENOENT) == system_error_message(ENOENT));

        /*
                The three that the differential cannot reach, checked here
                instead of against glibc.

                getdents64 is behind _GNU_SOURCE in glibc's headers and
                _GNU_SOURCE would swap strerror_r for the other one, so the
                reference cannot call it. setsid succeeds by detaching the
                caller from its controlling terminal, which is not a thing to
                do to a test process, so it is done in a child. And wait4's
                fourth argument is a buffer the kernel writes, which the
                transcript only ever passes as null -- so nothing so far says
                that argument is in the right place.
        */
        handle = open((string_address) "/tmp", O_RDONLY | O_DIRECTORY);
        check("directory opened for reading", handle >= 0);

        {
                p8 entries[1024];
                bipolar got;

                errno = 0;
                got = getdents64(handle, entries, sizeof entries);
                check("getdents64 returned entries", got > 0);
                check("getdents64 left errno alone", errno == 0);

                errno = 0;
                check("getdents64 on a bad handle",
                      getdents64(-1, entries, sizeof entries) == -1);
                check("and reported EBADF", errno == EBADF);
        }

        check("directory closed", close(handle) == 0);

        {
                b32 child = fork();
                b32 status = 0;

                if (child == 0)
                        _exit(setsid() < 0 ? 1 : 0);

                check("child reaped", waitpid(child, &status, 0) == child);
                check("setsid succeeded in the child", status == 0);
        }

        {
                //      struct rusage is 144 bytes on all three here, and the
                //      only thing this needs to know is that the kernel wrote
                //      into it -- a fourth argument in the wrong place either
                //      faults or leaves the fill byte alone.
                p8 usage[144];
                b32 child;
                b32 status = 0;

                memory_fill(usage, 0xa5, sizeof usage);

                child = fork();

                if (child == 0)
                        _exit(0);

                check("wait4 reaped the child",
                      wait4(child, &status, 0, usage) == child);
                check("wait4 filled the usage buffer",
                      !(usage[0] == 0xa5 && usage[8] == 0xa5
                        && usage[16] == 0xa5));
        }
}

b32 main(void)
{
        error_test_transcript();
        error_test_contract();

        return test_report(null);
}
#endif /* CHECK_error */

#ifdef CHECK_error_reference
/*
        The same transcript, out of the real C library.

        CHECK_error prints a stream of lines beginning TRANSCRIPT. This
        program prints the same stream using glibc's strerror, strerror_r,
        perror and syscall wrappers, so the check is

            diff <(reference) <(grep TRANSCRIPT freestanding)

        and any disagreement about a message, a return value or an errno is a
        line in that diff. It is built and linked normally -- it is the one
        file in the tree that is not freestanding, and it exists only to be
        disagreed with.

        _GNU_SOURCE is deliberately absent. glibc chooses between the POSIX
        strerror_r, which returns int, and the GNU one, which returns char *,
        on that macro, and the POSIX one is the contract the error section of
        src/lib.util.c implements.
*/

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//      dup3 and pipe2 are behind _GNU_SOURCE in the headers, and _GNU_SOURCE
//      would swap strerror_r for the char * returning one this does not test.
//      They are in the library either way, so they are declared here.
extern int dup3(int from, int to, int flags);
extern int pipe2(int pair[2], int flags);

//      The names the shared body is written in, as glibc has them.
#define fn void
#define address_to *
#define null NULL
#define end 0
typedef char p8;
typedef unsigned int p32;
typedef int b32;
typedef long bipolar;
typedef unsigned long positive;
typedef const char *string_address;
typedef void *address_any;
typedef struct stat error_stat;
#define memory_fill(block, value, count) memset(block, value, count)
#define address_bad MAP_FAILED
#define FILE_PROTECT_READ PROT_READ
#define FILE_PROTECT_WRITE PROT_WRITE
#define FILE_MAP_PRIVATE MAP_PRIVATE
#define FILE_MAP_ANONYMOUS MAP_ANONYMOUS

//      renameat rather than renameat2: the freestanding side has to call
//      renameat2 because riscv64 has no renameat at all, and renameat2 with
//      no flags is defined to be the same call.
#define renameat2(from_directory, from, to_directory, to, flags) \
        renameat(from_directory, from, to_directory, to)

#define error_flush() fflush(stdout)
#define error_sync() (sync(), 0)

//      The library's format on glibc's printf: %b is a bipolar and %p a
//      positive, which are long and unsigned long here.
static void error_trace(const char *format, ...)
{
        char native[256];
        size_t at = 0;
        const char *from = format;
        va_list arguments;

        for (; *from; from++)
        {
                if (*from == '%' && (from[1] == 'b' || from[1] == 'p'))
                {
                        native[at++] = '%';
                        native[at++] = 'l';
                        native[at++] = from[1] == 'b' ? 'd' : 'u';
                        from++;
                }
                else
                        native[at++] = *from;
        }

        native[at] = 0;
        va_start(arguments, format);
        vprintf(native, arguments);
        va_end(arguments);
}

#define SHARED_error_body
#include "../checks.c"
#undef SHARED_error_body

int main(void)
{
        error_test_transcript();
        fflush(stdout);
        return 0;
}
#endif /* CHECK_error_reference */

#ifdef CHECK_strings
#include "../../src/lib.util.c"

/* String and jump cases share strings_cases.inc with the system-libc
   reference. The umbrella supplies the actual text and allocator families. */
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        What the cases are written against.

        One name per routine, so the case list mentions neither the library's
        spelling nor glibc's and can be compiled against either.
*/
typedef string_address text_string;
typedef positive text_size;
typedef p8 text_byte;

#define TX(literal) ((text_string)(literal))

#define text_length(source)              string_length(source)
#define text_equal(one, two)             (string_compare((text_string)(one),   \
                                                         (text_string)(two)) == 0)
#define text_fill(block, value, size)    memory_fill((block), (value), (size))

#define text_duplicate(source)           string_duplicate(source)
#define text_duplicate_max(source, n)    string_duplicate_max((source), (n))
#define text_token(source, delims)       string_token((source), (delims))
#define text_token_next(source, delims, place)                                \
        string_token_next((source), (delims), (place))
#define text_split_next(holder, delims)  string_split_next((holder), (delims))
#define text_search_folded(hay, needle)  string_search_folded((hay), (needle))
#define text_copy_bounded(dest, source, n)                                    \
        string_copy_bounded((dest), (source), (n))
#define text_append_bounded(dest, source, n)                                  \
        string_append_bounded((dest), (source), (n))
#define text_frob(block, size)           memory_frob((block), (size))

#define text_jump_state                  jump_state
#define text_jump_mark(state)            jump_mark(state)
#define text_jump_to_mark(state, value)  jump_to_mark((state), (value))

#define SHARED_strings_cases
#include "../checks.c"
#undef SHARED_strings_cases

b32 main(void)
{
        text_case_all();

        /*
                The one question glibc cannot be asked.

                strtok_r with no source and nothing saved dereferences the
                null in glibc and the program stops there, so this case is
                outside the shared list. Ours answers nothing, which is the
                only answer a routine with no starting point has.
        */
        {
                text_string place = 0;

                check("strtok_r with no place at all",
                      string_token_next(null, TX(","), &place) == null);
        }

        return test_report(null);
}
#endif /* CHECK_strings */

#ifdef CHECK_strings_reference
#define _GNU_SOURCE

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
        The same cases, against the host's glibc.

        This is not built by the distribution and it links nothing the
        distribution ships. It exists so that every expectation in
        CHECK_strings is checked against the implementation those
        routines are specified by, on the machine the tests run on -- an
        expectation that only this library's string routines satisfy is an expectation
        that agrees with a bug.

        Build it beside the freestanding one and compare the verdicts:

            gcc -O2 -no-pie -w -DCHECK_strings_reference \
                -o /tmp/text.reference test/checks.c

        -no-pie because the register probe in the case file addresses
        text_case_state and calls setjmp directly, and a position independent
        executable would want those through the GOT and the PLT.

        strlcpy and strlcat arrived in glibc 2.38. On an older library this
        will not link, and the two of them are the only cases that need it.
*/

static unsigned long long checks;
static unsigned long long failures;

#define check(name, condition)                                          \
        do {                                                            \
                checks++;                                               \
                if (!(condition)) {                                     \
                        failures++;                                     \
                        printf("  FAIL " name "\n");                    \
                }                                                       \
        } while (0)

typedef char *text_string;
typedef size_t text_size;
typedef char text_byte;

#define TX(literal) ((text_string)(literal))

#define text_length(source)              strlen((const char *)(source))
#define text_equal(one, two)             (strcmp((const char *)(one),          \
                                                 (const char *)(two)) == 0)
#define text_fill(block, value, size)    memset((block), (value), (size))

#define text_duplicate(source)           strdup((const char *)(source))
#define text_duplicate_max(source, n)    strndup((const char *)(source), (n))
#define text_token(source, delims)       strtok((source), (delims))
#define text_token_next(source, delims, place)                                \
        strtok_r((source), (delims), (place))
#define text_split_next(holder, delims)  strsep((holder), (delims))
#define text_search_folded(hay, needle)  strcasestr((hay), (needle))
#define text_copy_bounded(dest, source, n)                                    \
        strlcpy((dest), (source), (n))
#define text_append_bounded(dest, source, n)                                  \
        strlcat((dest), (source), (n))
#define text_frob(block, size)           memfrob((block), (size))

#define text_jump_state                  jmp_buf
#define text_jump_mark(state)            setjmp(state)
#define text_jump_to_mark(state, value)  longjmp((state), (value))

#define SHARED_strings_cases
#include "../checks.c"
#undef SHARED_strings_cases

int main(void)
{
        text_case_all();

        /*
                One check to stand where the freestanding build asks strtok_r
                what it does with no place at all. glibc faults on that
                question, so it is not asked here and this keeps the two
                verdict lines comparable.
        */
        check("strtok_r with no place at all is not asked of glibc", 1);

        printf("%llu checks, %llu failures\n", checks, failures);

        return failures ? 1 : 0;
}
#endif /* CHECK_strings_reference */

#ifdef CHECK_stdlib
/*
        Experimental C standard library

        stdlib: the environment, sorting, leaving, and a generator that has to
        agree with glibc's

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/
#include "../../src/lib.util.c"

/*
        Half of this family cannot be checked from inside the process it is
        running in. exit runs handlers and then does not come back; abort is
        supposed to kill whatever installed a handler over it; _Exit is
        defined by what it does not do. Every one of those is a statement
        about a process, so the checks that make them are made about a child:
        the test clones, the child leaves by the path being examined, and the
        parent reads the status the kernel gives it back and, where the
        question is about buffered output, the bytes that came out of a pipe.

        The numbers the generator is checked against are glibc's, taken from a
        program linked against it on the same machine and pasted here. They
        are not a plausible-looking sequence; they are the sequence, and a
        change in this file that alters one of them has changed the answer.
*/

//      A literal where a string_address is wanted. str() in lib.c gives a
//      pointer and a length together, which is the wrong shape for a call that
//      takes one argument.
#define text(literal) ((string_address)(literal))

#define SHARED_named_cases
#include "../checks.c"
#undef SHARED_named_cases

//      What a cloned child is being asked to do. The child never returns from
//      any of these; the number it leaves with is the answer.
#define LEAVE_EXIT 1
#define LEAVE_QUICK 2
#define LEAVE_IMMEDIATE 3
#define LEAVE_ABORT 4
#define LEAVE_ABORT_IGNORED 5
#define LEAVE_ABORT_BLOCKED 6
#define LEAVE_ORDER 7
#define LEAVE_UNNAMED 8

#define TEST_MARKER "buffered\n"
#define TEST_SIGNAL_ABRT 6

static b32 stdlib_test_order = 0;

static fn stdlib_test_mark_one(void) { stdlib_test_order = stdlib_test_order * 10 + 1; }
static fn stdlib_test_mark_two(void) { stdlib_test_order = stdlib_test_order * 10 + 2; }
static fn stdlib_test_mark_three(void) { stdlib_test_order = stdlib_test_order * 10 + 3; }

//      Registered first, so it runs last, and it is the only place where the
//      whole order is known. Three handlers registered one two three and run
//      in reverse spell 321; anything else leaves with one.
static fn stdlib_test_mark_judge(void)
{
        _Exit(stdlib_test_order == 321 ? 0 : 1);
}

//      SIG_IGN is one, and installing it needs no restorer because nothing is
//      ever delivered into this program's own code.
static fn stdlib_test_ignore_abort(void)
{
        positive action[4] = {1, 0, 0, 0};

        system_call_4(syscall(rt_sigaction), TEST_SIGNAL_ABRT,
                      (positive)address_of action, 0, 8);
}

//      SIG_BLOCK is zero.
static fn stdlib_test_block_abort(void)
{
        positive mask = (positive)1 << (TEST_SIGNAL_ABRT - 1);

        system_call_4(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8);
}

DEAD_END static fn stdlib_test_child(b32 which)
{
        //      Left in the buffer on purpose: whether it ever reaches the
        //      pipe is what separates exit from the three that do not flush.
        //
        //      Through a stream rather than through the log, because in a
        //      forked child the two buffers answer differently and this is a
        //      forked child. A stream records which process filled it and this
        //      one filled its own, so exit writes it out; the log buffer lives
        //      in lib.c where there is nowhere to keep that, so an implicit
        //      flush leaves it alone in any child at all. The block above
        //      stdlib_buffers_are_ours says why, and CHECK_leaving has
        //      both halves under test.
        printf("%s", TEST_MARKER);

        switch (which)
        {
        case LEAVE_EXIT:
                stdlib_exit(5);
                break;

        case LEAVE_QUICK:
                quick_exit(6);
                break;

        case LEAVE_IMMEDIATE:
                _Exit(7);
                break;

        case LEAVE_ABORT:
                abort();
                break;

        case LEAVE_ABORT_IGNORED:
                stdlib_test_ignore_abort();
                abort();
                break;

        case LEAVE_ABORT_BLOCKED:
                stdlib_test_block_abort();
                abort();
                break;

        case LEAVE_ORDER:
                atexit(stdlib_test_mark_judge);
                atexit(stdlib_test_mark_one);
                atexit(stdlib_test_mark_two);
                atexit(stdlib_test_mark_three);
                stdlib_exit(99);
                break;

        default:
                break;
        }

        //      Only reached when the path above was defeated, which is itself
        //      the failure this reports.
        _Exit(70);
}

/*
        Run one of those in a child and bring back both halves of its answer:
        the status the shell would have reported, and how many bytes it wrote
        before it went. The child's standard output is the pipe, so the parent
        is reading exactly what the child flushed and nothing else.

        The parent flushes its own buffer before cloning. A child inherits it,
        and a child that flushed would print the parent's pending output a
        second time -- the hazard the C spelling of exit used to be kept away
        from, and is now guarded against rather than avoided.
*/
static b32 stdlib_test_leave(b32 which, positive address_to wrote)
{
        b32 ends[2];
        bipolar child;
        positive raw = 0;
        p8 seen[64];
        positive filled = 0;

        *wrote = 0;

        if (system_call_2(syscall(pipe2), (positive)(address_any)ends, 0) < 0)
                return -1;

        log_flush();

        child = system_call_2(syscall(clone), SIGCHLD, 0);

        if (child < 0)
                return -1;

        if (child == 0)
        {
                system_call_1(syscall(close), (positive)ends[0]);
                system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                system_call_1(syscall(close), (positive)ends[1]);
                stdlib_test_child(which);
        }

        system_call_1(syscall(close), (positive)ends[1]);

        for (;;)
        {
                bipolar got = system_read_retry((positive)ends[0], seen + filled,
                                                sizeof(seen) - filled);

                if (got <= 0)
                        break;

                filled += (positive)got;

                if (filled >= sizeof(seen))
                        break;
        }

        system_call_1(syscall(close), (positive)ends[0]);

        if (system_wait4_retry(child, address_of raw, 0, null) < 0)
                return -1;

        *wrote = filled;

        return wait_status_code(raw);
}

test(exit_runs_handlers_and_flushes)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_EXIT, address_of wrote) == 5);
        fail(wrote == sizeof(TEST_MARKER) - 1);

        return true;
}

test(exit_runs_handlers_in_reverse)
{
        positive wrote = 0;

        //      Zero only when the three handlers ran three, two, one.
        fail(stdlib_test_leave(LEAVE_ORDER, address_of wrote) == 0);

        return true;
}

test(quick_exit_does_not_flush)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_QUICK, address_of wrote) == 6);
        fail(wrote == 0);

        return true;
}

test(immediate_exit_does_not_flush)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_IMMEDIATE, address_of wrote) == 7);
        fail(wrote == 0);

        return true;
}

test(abort_raises_sigabrt)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_ABORT, address_of wrote) == 128 + TEST_SIGNAL_ABRT);

        return true;
}

//      The two ways a program can try to survive its own abort, and neither
//      of them is allowed to work.
test(abort_beats_an_ignored_signal)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_ABORT_IGNORED, address_of wrote) ==
             128 + TEST_SIGNAL_ABRT);

        return true;
}

test(abort_beats_a_blocked_signal)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_ABORT_BLOCKED, address_of wrote) ==
             128 + TEST_SIGNAL_ABRT);

        return true;
}

/*
        A way of leaving this file does not name reaches the trap.

        This was called returning_from_main_is_the_bare_trap, and the block
        here said that making the C spelling reach a return would mean
        changing assembly inside lib.c's graph, which no family owned. It
        did not need to. The umbrella grew a startup shim, the shim calls main
        and then exit, and returning from main now runs the handlers and
        flushes exactly as C says it does; CHECK_leaving proves that by
        being it, since its verdict line is written into the log buffer and
        never flushed by hand.

        What was always actually under test here is what the name should have
        said: a child handed a number this file has no case for falls off the
        end of the switch, reaches the _Exit below it, and that is the trap --
        status seventy, and nothing written.
*/
test(an_unnamed_way_of_leaving_reaches_the_trap)
{
        positive wrote = 0;

        fail(stdlib_test_leave(LEAVE_UNNAMED, address_of wrote) == 70);
        fail(wrote == 0);

        return true;
}

test(environment_reads_what_the_kernel_gave)
{
        //      The kernel hands over a vector, and with nothing in it (env -i)
        //      it is the null that ends it: the lane runs this under that
        //      too, so no entry is looked for.
        fail(!is_null(program_environment_list()));
        fail(is_null(getenv(text("A NAME NOTHING WOULD SET"))));

        return true;
}

test(environment_set_then_get)
{
        fail(setenv(text("DAWNING_ONE"), text("first"), 1) == 0);
        fail(string_compare(getenv(text("DAWNING_ONE")), text("first")) == 0);

        //      Without permission to overwrite, the value that was there wins
        //      and the call still succeeds.
        fail(setenv(text("DAWNING_ONE"), text("second"), 0) == 0);
        fail(string_compare(getenv(text("DAWNING_ONE")), text("first")) == 0);

        fail(setenv(text("DAWNING_ONE"), text("second"), 1) == 0);
        fail(string_compare(getenv(text("DAWNING_ONE")), text("second")) == 0);

        //      An empty value is a variable that is set and empty, which is
        //      not the same thing as one that is absent.
        fail(setenv(text("DAWNING_EMPTY"), text(""), 1) == 0);
        fail(!is_null(getenv(text("DAWNING_EMPTY"))));
        fail(getenv(text("DAWNING_EMPTY"))[0] == end);

        return true;
}

test(environment_refuses_a_bad_name)
{
        string_address names[] = {null, text(""), text("HAS=EQUALS")};
        for (positive at = 0; at < array_count(names); at++)
        {
                errno = EINTR;
                fail(setenv(names[at], text("x"), 1) == -1 && errno == EINVAL);
                errno = EINTR;
                fail(unsetenv(names[at]) == -1 && errno == EINVAL);
        }
        errno = EINTR;
        fail(putenv(null) == -1 && errno == EINVAL);
        errno = EINTR;
        fail(putenv(text("=value")) == -1 && errno == EINVAL);
        fail(setenv(text("DAWNING_VALID"), text("value"), 1) == 0);
        errno = EINTR;
        fail(setenv(text("DAWNING_VALID"), text("ignored"), 0) == 0 && errno == EINTR);
        fail(unsetenv(text("DAWNING_VALID")) == 0 && errno == EINTR);
        return true;
}

test(environment_unset)
{
        fail(setenv(text("DAWNING_GOING"), text("here"), 1) == 0);
        fail(!is_null(getenv(text("DAWNING_GOING"))));
        fail(unsetenv(text("DAWNING_GOING")) == 0);
        fail(is_null(getenv(text("DAWNING_GOING"))));

        //      Removing what is not there is a success, not an error.
        fail(unsetenv(text("DAWNING_GOING")) == 0);

        return true;
}

//      putenv installs the caller's own bytes, so writing through the caller's
//      pointer afterwards changes the variable. That is the contract, and it
//      is the whole reason setenv exists beside it.
static p8 stdlib_test_entry[32] = "DAWNING_HELD=before";

test(environment_putenv_keeps_the_caller_bytes)
{
        fail(putenv(stdlib_test_entry) == 0);
        fail(string_compare(getenv(text("DAWNING_HELD")), text("before")) == 0);

        stdlib_test_entry[13] = 'a';
        stdlib_test_entry[14] = 'f';
        stdlib_test_entry[15] = 't';
        stdlib_test_entry[16] = 'e';
        stdlib_test_entry[17] = 'r';
        stdlib_test_entry[18] = end;

        fail(string_compare(getenv(text("DAWNING_HELD")), text("after")) == 0);

        //      No equals in it means remove that name, which is what glibc
        //      does and what putenv("TZ") is asking for.
        fail(putenv(text("DAWNING_HELD")) == 0);
        fail(is_null(getenv(text("DAWNING_HELD"))));

        return true;
}

test(environment_clear)
{
        fail(setenv(text("DAWNING_TWO"), text("x"), 1) == 0);
        fail(clearenv() == 0);
        fail(is_null(getenv(text("DAWNING_TWO"))));
        fail(is_null(stdlib_environment_list()[0]));

        //      An emptied environment is still a working one.
        fail(setenv(text("DAWNING_THREE"), text("y"), 1) == 0);
        fail(string_compare(getenv(text("DAWNING_THREE")), text("y")) == 0);

        return true;
}

/*
        The growth path, which the handful of variables above never reaches.

        The vector starts with eight spare slots and the arena hands out sixty
        four kilobyte chunks, so nothing until here has made either of them
        reallocate. Two hundred names with four hundred byte values reallocate
        the vector five times over and roll the arena more than once, and the
        check afterwards is that every one of them is still readable -- a
        growth that got its arithmetic wrong truncates the environment and
        every test that only ever set three variables would still pass.
*/
test(environment_grows_past_its_first_vector)
{
        p8 name[32];
        p8 value[512];
        positive index;
        positive fill;
        string_address kernel_first = program_environment_list()[0];

        for (fill = 0; fill < sizeof(value) - 1; fill++)
                value[fill] = (p8)('a' + (fill % 26));

        value[sizeof(value) - 1] = end;

        for (index = 0; index < 200; index++)
        {
                positive length = 0;

                memory_copy_apart(name, text("DAWNING_MANY_"), 13);
                length = 13 + positive_into(name + 13, index);
                name[length] = end;

                //      A different length each time, so the arena is asked
                //      for a different size on every call and a rollover
                //      lands in the middle of a string rather than tidily
                //      between two.
                value[100 + (index % 300)] = end;

                fail(setenv(name, value, 1) == 0);

                value[100 + (index % 300)] = (p8)('a' + ((100 + (index % 300)) % 26));
        }

        for (index = 0; index < 200; index++)
        {
                positive length = 0;
                string_address found;

                memory_copy_apart(name, text("DAWNING_MANY_"), 13);
                length = 13 + positive_into(name + 13, index);
                name[length] = end;

                found = getenv(name);

                fail(!is_null(found));
                fail(string_length(found) == 100 + (index % 300));
                fail(found[0] == 'a');
        }

        //      And they come out again, which walks the shift in unsetenv
        //      over a vector that is no longer the one it started on.
        for (index = 0; index < 200; index += 2)
        {
                positive length = 0;

                memory_copy_apart(name, text("DAWNING_MANY_"), 13);
                length = 13 + positive_into(name + 13, index);
                name[length] = end;

                fail(unsetenv(name) == 0);
        }

        for (index = 0; index < 200; index++)
        {
                positive length = 0;

                memory_copy_apart(name, text("DAWNING_MANY_"), 13);
                length = 13 + positive_into(name + 13, index);
                name[length] = end;

                //      Braced on both sides on purpose: fail() expands to a
                //      bare if, and an unbraced else after it binds to that
                //      inner if instead of this one.
                if (index % 2 == 0)
                {
                        fail(is_null(getenv(name)));
                }
                else
                {
                        fail(!is_null(getenv(name)));
                }
        }

        //      What the kernel gave is still there under all of that.
        fail(program_environment_list()[0] == kernel_first);

        return true;
}

#define SORT_MOST 2048

static b32 sort_narrow[SORT_MOST];
static b64 sort_wide[SORT_MOST];

typedef struct
{
        p8 bytes[5];
} sort_odd;

static sort_odd sort_odds[SORT_MOST];

#define SORT_WIDTH_COUNT 53
#define SORT_WIDTH_MAX 64
#define SORT_WIDTH_GUARD 32

static p8 sort_width_storage[SORT_WIDTH_GUARD + 16 +
                             SORT_WIDTH_COUNT * SORT_WIDTH_MAX +
                             SORT_WIDTH_GUARD];

static b32 sort_compare_narrow(address_any left, address_any right)
{
        b32 a = *(b32 address_to)left;
        b32 b = *(b32 address_to)right;

        return a < b ? -1 : (a > b ? 1 : 0);
}

static b32 sort_compare_wide(address_any left, address_any right)
{
        b64 a = *(b64 address_to)left;
        b64 b = *(b64 address_to)right;

        return a < b ? -1 : (a > b ? 1 : 0);
}

static b32 sort_compare_odd(address_any left, address_any right)
{
        return memory_compare(((sort_odd address_to)left)->bytes,
                              ((sort_odd address_to)right)->bytes, 5);
}

static b32 sort_compare_width(address_any left, address_any right,
                              address_any context)
{
        return memory_compare(left, right, *(positive address_to)context);
}

//      The comparator qsort_r is given, which reads its answer's direction out
//      of the context rather than out of the code.
static b32 sort_compare_directed(address_any left, address_any right,
                                 address_any context)
{
        b32 order = sort_compare_narrow(left, right);

        return *(b32 address_to)context < 0 ? -order : order;
}

//      The five shapes, and a sixth with only four distinct values so runs of
//      equal keys are everywhere rather than nowhere.
static b64 sort_shaped(b32 shape, positive index, positive count, b32 draw)
{
        switch (shape)
        {
        case 0:
                return draw;
        case 1:
                return (b64)index;
        case 2:
                return (b64)(count - index);
        case 3:
                return 42;
        case 4:
                return (b64)(index * 2 < count ? index : count - index);
        default:
                return draw % 4;
        }
}

/*
        The heap sort a quicksort that has used up its budget falls back to is
        reached only by input that makes the splits bad again and again, and
        no shape above does. McIlroy's adversary ("A killer adversary for
        quicksort", 1999) makes that input for any sort that compares through
        a function: every value starts as gas, a comparison between two gas
        values turns one of them into the next solid value, and what the sort
        was shown is the permutation it took the worst of. The array that
        comes out is then sorted for real, and has to come out sorted, with
        the same elements, in a number of comparisons nowhere near the square.
*/
#define SORT_ADVERSARY_COUNT 4096
#define SORT_ADVERSARY_GAS 0x7fffffff

static b32 sort_adversary_value[SORT_ADVERSARY_COUNT];
static b32 sort_adversary_index[SORT_ADVERSARY_COUNT];
static b32 sort_adversary_real[SORT_ADVERSARY_COUNT];
static b32 sort_adversary_solid;
static b32 sort_adversary_candidate;
static positive sort_adversary_compares;

static b32 sort_adversary_compare(address_any left, address_any right)
{
        b32 x = *(b32 address_to)left;
        b32 y = *(b32 address_to)right;

        sort_adversary_compares++;

        if (sort_adversary_value[x] == SORT_ADVERSARY_GAS &&
            sort_adversary_value[y] == SORT_ADVERSARY_GAS)
                sort_adversary_value[x == sort_adversary_candidate ? x : y] =
                        sort_adversary_solid++;

        if (sort_adversary_value[x] == SORT_ADVERSARY_GAS)
                sort_adversary_candidate = x;
        else if (sort_adversary_value[y] == SORT_ADVERSARY_GAS)
                sort_adversary_candidate = y;

        return sort_adversary_value[x] < sort_adversary_value[y]
                   ? -1
                   : sort_adversary_value[x] > sort_adversary_value[y];
}

static b32 sort_adversary_plain(address_any left, address_any right)
{
        b32 x = *(b32 address_to)left;
        b32 y = *(b32 address_to)right;

        sort_adversary_compares++;

        return x < y ? -1 : x > y;
}

test(sort_beats_an_adversary_through_the_heap)
{
        positive index;
        positive total = 0;
        positive after = 0;

        for (index = 0; index < SORT_ADVERSARY_COUNT; index++)
        {
                sort_adversary_value[index] = SORT_ADVERSARY_GAS;
                sort_adversary_index[index] = (b32)index;
        }

        sort_adversary_solid = 0;
        sort_adversary_candidate = 0;
        sort_adversary_compares = 0;

        qsort(sort_adversary_index, SORT_ADVERSARY_COUNT, sizeof(b32),
              sort_adversary_compare);

        //      What the adversary was still holding back is the largest.
        for (index = 0; index < SORT_ADVERSARY_COUNT; index++)
                if (sort_adversary_value[index] == SORT_ADVERSARY_GAS)
                        sort_adversary_value[index] = sort_adversary_solid++;

        for (index = 0; index < SORT_ADVERSARY_COUNT; index++)
        {
                sort_adversary_real[index] = sort_adversary_value[index];
                total += (positive)(p32)sort_adversary_real[index];
        }

        //      Sorting the adversary's own run took the heap too, and ended
        //      in no more than a few times n log n comparisons.
        fail(sort_adversary_compares < 64 * SORT_ADVERSARY_COUNT * 12);

        sort_adversary_compares = 0;
        qsort(sort_adversary_real, SORT_ADVERSARY_COUNT, sizeof(b32),
              sort_adversary_plain);

        for (index = 0; index < SORT_ADVERSARY_COUNT; index++)
        {
                after += (positive)(p32)sort_adversary_real[index];
                fail(sort_adversary_real[index] == (b32)index);
        }

        fail(total == after);
        fail(sort_adversary_compares < 64 * SORT_ADVERSARY_COUNT * 12);

        return true;
}

test(sort_every_shape_at_every_width)
{
        static const positive counts[] = {0, 1, 2, 3, 5, 8, 12, 13, 17,
                                          41, 64, 257, 1000, 2048};
        positive which;
        b32 seed;

        for (seed = 1; seed <= 8; seed++)
        {
                srand((p32)seed);

                for (which = 0; which < 14; which++)
                {
                        positive count = counts[which];
                        b32 shape;

                        for (shape = 0; shape < 6; shape++)
                        {
                                positive index;
                                positive total = 0;
                                positive after = 0;

                                for (index = 0; index < count; index++)
                                {
                                        b64 value = sort_shaped(shape, index, count,
                                                                rand());
                                        b32 byte;

                                        sort_narrow[index] = (b32)value;
                                        sort_wide[index] = value;

                                        for (byte = 0; byte < 5; byte++)
                                                sort_odds[index].bytes[byte] =
                                                        (p8)((value >> (byte * 5)) &
                                                             0xff);

                                        total += (positive)(p32)value;
                                }

                                qsort(sort_narrow, count, sizeof(b32),
                                      sort_compare_narrow);
                                qsort(sort_wide, count, sizeof(b64), sort_compare_wide);
                                qsort(sort_odds, count, sizeof(sort_odd),
                                      sort_compare_odd);

                                for (index = 0; index < count; index++)
                                {
                                        after += (positive)(p32)sort_narrow[index];

                                        if (index == 0)
                                                continue;

                                        //      Ordered, at all three widths.
                                        fail(sort_narrow[index - 1] <=
                                             sort_narrow[index]);
                                        fail(sort_wide[index - 1] <= sort_wide[index]);
                                        fail(memory_compare(
                                                     sort_odds[index - 1].bytes,
                                                     sort_odds[index].bytes, 5) <= 0);
                                }

                                //      And still the same elements: a sort
                                //      that drops one is ordered too.
                                fail(total == after);
                        }
                }
        }

        return true;
}

test(sort_every_record_width_and_base_residue)
{
        positive width;

        for (width = 1; width <= SORT_WIDTH_MAX; width++)
        {
                positive residue;

                for (residue = 0; residue < 16; residue++)
                {
                        positive begin = SORT_WIDTH_GUARD + residue;
                        positive used = SORT_WIDTH_COUNT * width;
                        p8 address_to base = sort_width_storage + begin;
                        positive index;

                        memory_fill(sort_width_storage, 0xa5,
                                    sizeof(sort_width_storage));

                        for (index = 0; index < SORT_WIDTH_COUNT; index++)
                        {
                                p8 key = (p8)((index * 37) % SORT_WIDTH_COUNT);
                                positive byte;

                                base[index * width] = key;

                                for (byte = 1; byte < width; byte++)
                                        base[index * width + byte] =
                                            (p8)(key ^ (byte * 73 + width));
                        }

                        qsort_r(base, SORT_WIDTH_COUNT, width, sort_compare_width,
                                address_of width);

                        for (index = 0; index < SORT_WIDTH_COUNT; index++)
                        {
                                positive byte;

                                fail(base[index * width] == (p8)index);

                                for (byte = 1; byte < width; byte++)
                                        fail(base[index * width + byte] ==
                                             (p8)(index ^ (byte * 73 + width)));
                        }

                        for (index = 0; index < begin; index++)
                                fail(sort_width_storage[index] == 0xa5);

                        for (index = begin + used;
                             index < sizeof(sort_width_storage); index++)
                                fail(sort_width_storage[index] == 0xa5);
                }
        }

        return true;
}

test(sort_leaves_the_degenerate_alone)
{
        sort_narrow[0] = 7;

        qsort(sort_narrow, 0, sizeof(b32), sort_compare_narrow);
        qsort(sort_narrow, 1, sizeof(b32), sort_compare_narrow);
        qsort(null, 10, sizeof(b32), sort_compare_narrow);
        qsort(sort_narrow, 10, 0, sort_compare_narrow);
        qsort(sort_narrow, 10, sizeof(b32), null);

        fail(sort_narrow[0] == 7);

        return true;
}

test(sort_with_a_context)
{
        b32 direction;
        positive index;

        for (index = 0; index < 100; index++)
                sort_narrow[index] = (b32)((index * 37) % 100);

        direction = 1;
        qsort_r(sort_narrow, 100, sizeof(b32), sort_compare_directed,
                address_of direction);

        for (index = 1; index < 100; index++)
                fail(sort_narrow[index - 1] <= sort_narrow[index]);

        direction = -1;
        qsort_r(sort_narrow, 100, sizeof(b32), sort_compare_directed,
                address_of direction);

        for (index = 1; index < 100; index++)
                fail(sort_narrow[index - 1] >= sort_narrow[index]);

        return true;
}

test(search_finds_and_misses)
{
        positive index;

        for (index = 0; index < 100; index++)
                sort_narrow[index] = (b32)(index * 2);

        for (index = 0; index < 100; index++)
        {
                b32 key = (b32)(index * 2);
                b32 address_to found = (b32 address_to)bsearch(
                        address_of key, sort_narrow, 100, sizeof(b32),
                        sort_compare_narrow);

                fail(!is_null(found));
                fail(*found == key);
        }

        for (index = 0; index < 100; index++)
        {
                b32 key = (b32)(index * 2 + 1);

                fail(is_null(bsearch(address_of key, sort_narrow, 100, sizeof(b32),
                                     sort_compare_narrow)));
        }

        {
                b32 key = 0;

                //      Nothing to search is a miss, not a fault.
                fail(is_null(bsearch(address_of key, sort_narrow, 0, sizeof(b32),
                                     sort_compare_narrow)));
        }

        return true;
}

test(division_keeps_the_sign_of_the_numerator)
{
        div_t narrow = div(-7, 2);
        ldiv_t wide = ldiv(-7, 2);
        lldiv_t widest = lldiv(7, -2);

        fail(narrow.quot == -3 && narrow.rem == -1);
        fail(wide.quot == -3 && wide.rem == -1);
        fail(widest.quot == -3 && widest.rem == 1);

        return true;
}

/*
        The generator, against glibc's own numbers.

        srand and srandom are the same generator in glibc -- measured, for
        every seed tried -- and a seed of zero is folded onto one, which is
        why the first two lists below are identical.
*/
test(random_is_glibc_number_for_number)
{
        static const b32 from_one[] = {1804289383, 846930886,  1681692777,
                                       1714636915, 1957747793, 424238335,
                                       719885386,  1649760492};
        static const b32 from_two[] = {1505335290, 1738766719, 190686788,
                                       260874575,  747983061,  906156498,
                                       1502820864, 142559277};
        static const b32 from_many[] = {383100999,  858300821, 357768173,
                                        455528251,  133005921, 116285904,
                                        591987137,  102557902};
        positive index;

        srand(1);

        for (index = 0; index < 8; index++)
                fail(rand() == from_one[index]);

        srand(0);

        for (index = 0; index < 8; index++)
                fail(rand() == from_one[index]);

        srandom(2);

        for (index = 0; index < 8; index++)
                fail(random() == from_two[index]);

        srandom(12345);

        for (index = 0; index < 8; index++)
                fail(random() == from_many[index]);

        srand(1);

        //      Nothing it produces is outside what RAND_MAX promises.
        for (index = 0; index < 4096; index++)
        {
                b32 drawn = rand();

                fail(drawn >= 0 && drawn <= RAND_MAX);
        }

        return true;
}

test(random_defaults_to_seed_one)
{
        static const b32 expected[] = {1804289383, 846930886, 1681692777,
                                       1714636915, 1957747793, 424238335,
                                       719885386, 1649760492};

        /* This case must remain before every case that calls srand. */
        for (positive index = 0; index < 8; index++)
                fail(random() == expected[index]);

        return true;
}

test(random_reentrant_is_glibc_too)
{
        static const b32 expected[] = {476707713, 1186278907, 505671508,
                                       2137716191, 936145377};
        p32 seed = 1;
        positive index;

        for (index = 0; index < 5; index++)
                fail(rand_r(address_of seed) == expected[index]);

        return true;
}

test(atexit_refuses_what_it_cannot_hold)
{
        fail(atexit(null) == -1);
        fail(at_quick_exit(null) == -1);

        return true;
}

test(system_runs_a_command)
{
        //      Nonzero when there is a shell to run things with, which there
        //      has to be for the next two answers to mean anything.
        fail(system(null) != 0);

        fail(wait_status_code((positive)system(text("exit 9"))) == 9);
        fail(wait_status_code((positive)system(text("exit 0"))) == 0);

        return true;
}

test(environment_arena_rejects_wrapping_sizes)
{
        p8 *next = stdlib_arena_next;
        positive left = stdlib_arena_left;

        for (positive tail = 0; tail < STDLIB_ARENA_ALIGN; tail++)
        {
                errno = EINTR;
                fail(stdlib_arena_take((positive)-1 - tail) == null && errno == ENOMEM);
        }
        errno = EINTR;
        fail(stdlib_arena_take(((positive)1 << 63) + STDLIB_ARENA_ALIGN) == null &&
             errno == ENOMEM);
        /* This fits the size arithmetic but cannot be mapped on any target;
           it exercises the raw mmap error path, including under qemu-user. */
        errno = EINTR;
        fail(stdlib_arena_take((positive)1 << 62) == null && errno == ENOMEM);
        fail(stdlib_arena_next == next && stdlib_arena_left == left);
        p8 *small = stdlib_arena_take(17);
        fail(small != null && ((positive)small & (STDLIB_ARENA_ALIGN - 1)) == 0);
        memory_fill(small, 0xa5, 17);
        fail(stdlib_arena_take(1) == small + 32);
        return true;
}

test_case test_cases[] = {
        case(environment_arena_rejects_wrapping_sizes),
        case(exit_runs_handlers_and_flushes),
        case(exit_runs_handlers_in_reverse),
        case(quick_exit_does_not_flush),
        case(immediate_exit_does_not_flush),
        case(abort_raises_sigabrt),
        case(abort_beats_an_ignored_signal),
        case(abort_beats_a_blocked_signal),
        case(an_unnamed_way_of_leaving_reaches_the_trap),
        case(environment_reads_what_the_kernel_gave),
        case(environment_set_then_get),
        case(environment_refuses_a_bad_name),
        case(environment_unset),
        case(environment_putenv_keeps_the_caller_bytes),
        case(environment_clear),
        case(environment_grows_past_its_first_vector),
        case(random_defaults_to_seed_one),
        case(sort_beats_an_adversary_through_the_heap),
        case(sort_every_shape_at_every_width),
        case(sort_every_record_width_and_base_residue),
        case(sort_leaves_the_degenerate_alone),
        case(sort_with_a_context),
        case(search_finds_and_misses),
        case(division_keeps_the_sign_of_the_numerator),
        case(random_is_glibc_number_for_number),
        case(random_reentrant_is_glibc_too),
        case(atexit_refuses_what_it_cannot_hold),
        case(system_runs_a_command),
        {null, null},
};

b32 main(void)
{
        log_direct(str("stdlib tests\n\n"));

        test_cases_walk(test_cases);

        //      Deliberately through exit rather than a return, because this
        //      is the family that knows the difference.
        stdlib_exit(test_report((string_address) "\n"));

        return 1;
}
#endif /* CHECK_stdlib */

#ifdef CHECK_leaving
/*
        Experimental C standard library

        leaving: what a program writes on its way out, what it does not, and
        what a forked child must never write a second time

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/
#include "../../src/lib.util.c"

/*
        Nothing here can be answered from inside the process asking it.

        Every question in this file is about what reached a descriptor at the
        moment a process stopped existing, and a process cannot report on its
        own last act. So each one is asked of a child: the test clones, the
        child writes something and leaves by the path under examination, and
        the parent reads the pipe the child's standard output was and counts
        the bytes that actually arrived.

        The child's standard output is set fully buffered on purpose. Whether
        a stream is line buffered is decided by asking the descriptor if it is
        a terminal, and every question here is about bytes that are still in a
        buffer when the process ends -- run from a terminal without this, the
        buffer would already be empty and every check would pass while proving
        nothing.

        One thing is deliberately not asked of a child, because it cannot be:
        whether returning from main flushes. main belongs to the umbrella's
        startup shim, a child cannot return from it without unwinding the
        whole test, and there is nowhere else to stand. So this file proves it
        by being it. The verdict line at the bottom is written into the log
        buffer and never flushed by hand; the only thing that can put it in
        front of the runner's grep is the shim calling exit on the way out of
        main. If that stops working this lane does not report a failure, it
        reports no verdict at all, which the runner treats as a failure and
        says so in those words.
*/

//      A literal where a string_address is wanted.
#define text(literal) ((string_address)(literal))

#define SHARED_named_cases
#include "../checks.c"
#undef SHARED_named_cases

/*
        The bytes a child writes, and never more than one buffer's worth of
        them, so that nothing here depends on how large BUFSIZ happens to be.
*/
#define LEAVING_FIRST "first\n"
#define LEAVING_SECOND "second\n"
#define LEAVING_HANDLER "handler\n"

//      What a cloned child is being asked to do, and the status it leaves
//      with, which is checked beside the bytes so that a child that died on
//      the way to the interesting part cannot be mistaken for one that got
//      there and wrote nothing.
#define LEAVE_STREAM_EXIT 21
#define LEAVE_STREAM_UNDERSCORE 22
#define LEAVE_STREAM_CAPITAL 23
#define LEAVE_STREAM_QUICK 24
#define LEAVE_LOG_EXIT 25
#define LEAVE_LOG_BY_HAND 26
#define LEAVE_BOTH 27
#define LEAVE_FORK_STREAM 28
#define LEAVE_FORK_LOG 29
#define LEAVE_FORK_TWICE 30
#define LEAVE_FORK_FLUSHED 31
#define LEAVE_FORK_CHILD_WROTE 32
#define LEAVE_ATEXIT 33
#define LEAVE_FORK_CHILD_FLUSHED 35
#define LEAVE_MANY_STREAMS 36
#define LEAVE_MATRIX_STATUS 40
#define LEAVE_MATRIX 0x10000

static fn leaving_handler(void)
{
        printf("%s", LEAVING_HANDLER);
}

/*
        Fork, let the child leave by exit, and wait for it.

        This is the shape the shell uses at every one of its twenty exit sites
        and the shape a failed execve leaves behind: a child that has nothing
        of its own to say, inherits whatever the parent had not written yet,
        and stops. It is the one place a flush at exit could print the
        parent's output twice.
*/
static fn leaving_fork_and_leave(void)
{
        bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
        positive raw = 0;

        if (child == 0)
                exit(0);

        if (child > 0)
                system_wait4_retry(child, address_of raw, 0, null);
}

/*
        The ownership stamp, walked rather than picked.

        A stream stamps its buffer with the process that filled it when a
        call leaves bytes behind in a buffer that was empty, and not when the
        call empties it again before returning -- which is what keeps a line
        buffered stream from asking the kernel for its identity once a line.
        The handful of cases above pin the shapes that motivated the stamp.
        This walks every pairing of a few steps before a fork with one step
        in the child, fully and line buffered, and checks what reaches the
        pipe against the policy written out as a model below, so a stamp that
        is skipped where it was due -- or taken where the bytes were somebody
        else's -- shows up as bytes printed twice or not at all.

        The steps are the ways a byte gets into a stream: printf with and
        without a newline and with one in the middle, a padded conversion,
        putc, puts, fwrite, fputs, and fflush.
*/
#define LEAVING_STEPS 10
#define LEAVING_STEP_FLUSH 6

static const char *const leaving_step_bytes[LEAVING_STEPS] = {
        "", "pp", "ll\n", "7\nqq", "c", "uu\n", "", "ww", "f\n", "  5",
};

static fn leaving_step(b32 step)
{
        switch (step)
        {
        case 1: printf("%s", "pp"); break;
        case 2: printf("%s\n", "ll"); break;
        case 3: printf("%d\n%s", 7, "qq"); break;
        case 4: putc('c', stdout); break;
        case 5: puts("uu"); break;
        case LEAVING_STEP_FLUSH: fflush(stdout); break;
        case 7: fwrite("ww", 1, 2, stdout); break;
        case 8: fputs("f\n", stdout); break;
        case 9: printf("%*d", 3, 5); break;
        default: break;
        }
}

DEAD_END static fn leaving_matrix_child(b32 which)
{
        bipolar grandchild;
        positive raw = 0;

        if ((which >> 12) & 1)
                stream_set_buffering(stdout, null, _IOLBF, BUFSIZ);

        leaving_step((which >> 8) & 15);
        leaving_step((which >> 4) & 15);

        grandchild = system_call_2(syscall(clone), SIGCHLD, 0);

        if (grandchild == 0)
        {
                leaving_step(which & 15);
                exit(0);
        }

        if (grandchild > 0)
                system_wait4_retry(grandchild, address_of raw, 0, null);

        exit(LEAVE_MATRIX_STATUS);
}

typedef struct
{
        p8 bytes[256];
        positive length;
} leaving_text;

static fn leaving_append(leaving_text address_to into, address_any bytes,
                         positive length)
{
        memory_copy(into->bytes + into->length, bytes, length);
        into->length += length;
}

//      One step of the policy: a flush sends what is waiting, a line buffered
//      stream sends everything once a step stages a newline, and a buffer
//      that went from empty to holding something and still holds it belongs
//      to whoever ran the step.
static fn leaving_model_step(b32 step, p8 line, leaving_text address_to disk,
                             leaving_text address_to pending,
                             b32 address_to owner, b32 me)
{
        string_address text = (string_address)leaving_step_bytes[step];
        positive length = string_length(text);
        p8 was_empty = pending->length == 0;

        if (step == LEAVING_STEP_FLUSH)
        {
                leaving_append(disk, pending->bytes, pending->length);
                pending->length = 0;
                return;
        }

        leaving_append(pending, text, length);

        if (line && length && memory_first_of(text, '\n', length))
        {
                leaving_append(disk, pending->bytes, pending->length);
                pending->length = 0;
                return;
        }

        if (was_empty && pending->length != 0)
                address_to owner = me;
}

//      What the pipe holds at the end: the parent's steps, the child's step
//      with whatever it inherited, the child's exit, then the parent's exit.
static fn leaving_matrix_expect(b32 which, leaving_text address_to out)
{
        p8 line = (which >> 12) & 1;
        leaving_text pending;
        leaving_text inherited;
        b32 owner = 0;
        b32 child_owner;

        out->length = 0;
        pending.length = 0;

        leaving_model_step((which >> 8) & 15, line, out, address_of pending,
                           address_of owner, 1);
        leaving_model_step((which >> 4) & 15, line, out, address_of pending,
                           address_of owner, 1);

        inherited = pending;
        child_owner = owner;
        leaving_model_step(which & 15, line, out, address_of inherited,
                           address_of child_owner, 2);

        if (inherited.length && child_owner == 2)
                leaving_append(out, inherited.bytes, inherited.length);

        if (pending.length && owner == 1)
                leaving_append(out, pending.bytes, pending.length);
}

DEAD_END static fn leaving_child(b32 which)
{
        bipolar grandchild;
        positive raw = 0;

        stream_set_buffering(stdout, null, _IOFBF, BUFSIZ);

        switch (which)
        {
        case LEAVE_STREAM_EXIT:
                printf("%s", LEAVING_FIRST);
                exit(LEAVE_STREAM_EXIT);
                break;

        case LEAVE_STREAM_UNDERSCORE:
                printf("%s", LEAVING_FIRST);
                _exit(LEAVE_STREAM_UNDERSCORE);
                break;

        case LEAVE_STREAM_CAPITAL:
                printf("%s", LEAVING_FIRST);
                _Exit(LEAVE_STREAM_CAPITAL);
                break;

        case LEAVE_STREAM_QUICK:
                printf("%s", LEAVING_FIRST);
                quick_exit(LEAVE_STREAM_QUICK);
                break;

        case LEAVE_LOG_EXIT:
                log(str(LEAVING_FIRST));
                exit(LEAVE_LOG_EXIT);
                break;

        case LEAVE_LOG_BY_HAND:
                log(str(LEAVING_FIRST));
                log_flush();
                exit(LEAVE_LOG_BY_HAND);
                break;

        case LEAVE_BOTH:
                printf("%s", LEAVING_FIRST);
                log(str(LEAVING_SECOND));
                log_flush();
                exit(LEAVE_BOTH);
                break;

        case LEAVE_FORK_STREAM:
                printf("%s", LEAVING_FIRST);
                leaving_fork_and_leave();
                exit(LEAVE_FORK_STREAM);
                break;

        case LEAVE_FORK_LOG:
                log(str(LEAVING_FIRST));
                leaving_fork_and_leave();
                log_flush();
                exit(LEAVE_FORK_LOG);
                break;

        case LEAVE_FORK_TWICE:
                printf("%s", LEAVING_FIRST);
                leaving_fork_and_leave();
                leaving_fork_and_leave();
                exit(LEAVE_FORK_TWICE);
                break;

        case LEAVE_FORK_FLUSHED:
                printf("%s", LEAVING_FIRST);
                fflush(stdout);

                grandchild = system_call_2(syscall(clone), SIGCHLD, 0);

                if (grandchild == 0)
                {
                        printf("%s", LEAVING_SECOND);
                        exit(0);
                }

                if (grandchild > 0)
                        system_wait4_retry(grandchild, address_of raw, 0, null);

                exit(LEAVE_FORK_FLUSHED);
                break;

        case LEAVE_FORK_CHILD_WROTE:
                printf("%s", LEAVING_FIRST);

                grandchild = system_call_2(syscall(clone), SIGCHLD, 0);

                if (grandchild == 0)
                {
                        printf("%s", LEAVING_SECOND);
                        exit(0);
                }

                if (grandchild > 0)
                        system_wait4_retry(grandchild, address_of raw, 0, null);

                exit(LEAVE_FORK_CHILD_WROTE);
                break;

        case LEAVE_FORK_CHILD_FLUSHED:
                printf("%s", LEAVING_FIRST);

                grandchild = system_call_2(syscall(clone), SIGCHLD, 0);

                if (grandchild == 0)
                {
                        fflush(stdout);
                        _exit(0);
                }

                if (grandchild > 0)
                        system_wait4_retry(grandchild, address_of raw, 0, null);

                exit(LEAVE_FORK_CHILD_FLUSHED);
                break;

        case LEAVE_ATEXIT:
                atexit(leaving_handler);
                printf("%s", LEAVING_FIRST);
                exit(LEAVE_ATEXIT);
                break;

        //      Five streams onto the pipe, each holding a line, two of them
        //      closed: the closes write theirs, and exit writes the three
        //      still open, newest first, through the list the closes
        //      unlinked from.
        case LEAVE_MANY_STREAMS:
        {
                stream address_to many[5];
                b32 at;

                for (at = 0; at < 5; at++)
                {
                        many[at] = fdopen(dup(1), "w");

                        if (is_null(many[at]))
                                _Exit(71);

                        fprintf(many[at], "s%d\n", at);
                }

                fclose(many[1]);
                fclose(many[3]);
                exit(LEAVE_MANY_STREAMS);
                break;
        }

        default:
                if (which >= LEAVE_MATRIX)
                        leaving_matrix_child(which);
                break;
        }

        //      Only reached when the path above did not leave, which is the
        //      failure this reports.
        _Exit(70);
}

/*
        Run one of those and bring back both halves of the answer: the status
        a shell would have reported, and the exact bytes that reached the
        pipe the child's standard output was.

        The parent flushes its own buffers before cloning, which is the advice
        POSIX has always given and which this file follows so that what the
        child inherits is only ever what the scenario put there on purpose.
*/
static b32 leaving_run(b32 which, p8 address_to seen, positive room,
                       positive address_to filled)
{
        b32 ends[2];
        bipolar child;
        positive raw = 0;

        *filled = 0;
        seen[0] = end;

        if (system_call_2(syscall(pipe2), (positive)(address_any)ends, 0) < 0)
                return -1;

        fflush(stdout);
        log_flush();

        child = system_call_2(syscall(clone), SIGCHLD, 0);

        if (child < 0)
                return -1;

        if (child == 0)
        {
                system_call_1(syscall(close), (positive)ends[0]);
                system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                system_call_1(syscall(close), (positive)ends[1]);
                leaving_child(which);
        }

        system_call_1(syscall(close), (positive)ends[1]);

        for (;;)
        {
                bipolar got = system_read_retry((positive)ends[0], seen + *filled,
                                                room - 1 - *filled);

                if (got <= 0)
                        break;

                *filled += (positive)got;

                if (*filled >= room - 1)
                        break;
        }

        seen[*filled] = end;

        system_call_1(syscall(close), (positive)ends[0]);

        if (system_wait4_retry(child, address_of raw, 0, null) < 0)
                return -1;

        return wait_status_code(raw);
}

//      Every check in the first half is the same two questions asked of a
//      different way of leaving, so they are asked once, here.
static p8 leaving_says(b32 which, string_address expected)
{
        p8 seen[256];
        positive filled = 0;

        if (leaving_run(which, seen, sizeof(seen), address_of filled) != which)
                return false;

        if (filled != string_length(expected))
                return false;

        return string_compare((string_address)seen, expected) == 0;
}

/*
        The divergence this family was written to close.

        printf then exit lost its bytes: the C spelling of exit was a macro
        nothing turned on, so the call reached the assembly trap and the
        buffer went with the address space. Every C program on earth assumes
        otherwise and glibc has always done otherwise.
*/
test(printf_then_exit_prints)
{
        return leaving_says(LEAVE_STREAM_EXIT, text(LEAVING_FIRST));
}

//      The three that must not flush, which is the whole of what distinguishes
//      them from the one above.
test(printf_then_underscore_exit_prints_nothing)
{
        return leaving_says(LEAVE_STREAM_UNDERSCORE, text(""));
}

test(printf_then_capital_exit_prints_nothing)
{
        return leaving_says(LEAVE_STREAM_CAPITAL, text(""));
}

test(printf_then_quick_exit_prints_nothing)
{
        return leaving_says(LEAVE_STREAM_QUICK, text(""));
}

test(an_atexit_handler_writes_before_the_flush)
{
        return leaving_says(LEAVE_ATEXIT,
                            text(LEAVING_FIRST LEAVING_HANDLER));
}

/*
        The hazard, and it is the reason this was not simply switched on.

        The child inherits a buffer holding bytes the parent wrote and has not
        written out. A flush at exit that did not ask whose bytes those were
        would print them from the child and then again from the parent, which
        is precisely what glibc does and what the folklore answer -- flush
        before you fork -- is folklore about.
*/
/*
        The identity the whole fork-ownership mechanism rests on.

        program_initial_identity is read once, at the program's first line,
        and every later decision about whose bytes are in a buffer compares
        against it. The cases below prove the CONSEQUENCE -- that a child
        does not reprint its parent's output -- which is the behaviour worth
        having. This one proves the PREMISE, because a routine that quietly
        returned zero, or read the identity afresh on each call, would let
        every case below pass while the mechanism did nothing: a child whose
        recorded identity is re-read after the fork matches itself, and
        decides the buffer is its own.

        So: it agrees with the kernel, it does not change when asked twice,
        and it is not zero.
*/
test(the_initial_identity_is_this_process_and_stays_put)
{
        positive first = program_initial_identity();
        positive again = program_initial_identity();

        fail(first != 0);
        fail(first == again);
        fail(first == (positive)system_call(syscall(getpid)));

        return true;
}

test(a_forked_child_leaving_does_not_print_the_parents_bytes_again)
{
        return leaving_says(LEAVE_FORK_STREAM, text(LEAVING_FIRST));
}

//      Twice over, because a mechanism that only survives one generation is
//      not a mechanism.
test(two_forked_children_leaving_still_print_it_once)
{
        return leaving_says(LEAVE_FORK_TWICE, text(LEAVING_FIRST));
}

/*
        And the case the ownership stamp exists for: a parent that did flush
        before it forked hands its child an empty buffer, so the child's own
        output is its own and goes out at exit exactly as an unforked
        program's would. A guard that asked only "am I a forked child" would
        throw this away.
*/
test(a_forked_child_with_a_clean_buffer_prints_its_own)
{
        return leaving_says(LEAVE_FORK_FLUSHED,
                            text(LEAVING_FIRST LEAVING_SECOND));
}

/*
        The residue, pinned rather than hidden.

        A child that writes into a buffer that already held its parent's loses
        its own bytes with the inherited ones, because the two are in one
        buffer with nothing between them to say where one stops. glibc writes
        both and then writes the parent's a second time from the parent. This
        writes the parent's once and drops the child's, which is a different
        wrong answer to a question POSIX answers by telling the caller to
        flush before forking. The check is here so a future change that alters
        it has to say so.
*/
test(a_forked_child_writing_into_an_inherited_buffer_loses_its_own)
{
        return leaving_says(LEAVE_FORK_CHILD_WROTE, text(LEAVING_FIRST));
}

/*
        The guard is on the implicit flush only. A child that calls fflush
        itself is asking for exactly what POSIX says fflush does, gets it, and
        gets the double print with it -- the same as glibc, and the reason the
        advice is to flush before the fork rather than after it.
*/
test(an_explicit_flush_in_a_child_still_writes_what_it_inherited)
{
        return leaving_says(LEAVE_FORK_CHILD_FLUSHED,
                            text(LEAVING_FIRST LEAVING_FIRST));
}

/*
        The log buffer is coarser, and the block above stdlib_buffers_are_ours
        says why: it lives in assembly, lib.c holds assembly and
        declarations and nothing else, so there is nowhere in it to keep the
        stamp a stream keeps. What gates it instead is the identity recorded
        at startup, so a forked child's implicit flush leaves the log alone.

        That is not a loss against anything that ever worked -- no exit path
        in this library flushed the log before this family wrote one -- and
        the explicit call still does what it always did, which is what the
        shell in this tree relies on at every site where it leaves a child.
*/
test(a_forked_childs_log_buffer_is_not_flushed_for_it)
{
        return leaving_says(LEAVE_LOG_EXIT, text(""));
}

test(a_forked_child_can_still_flush_the_log_itself)
{
        return leaving_says(LEAVE_LOG_BY_HAND, text(LEAVING_FIRST));
}

test(a_forked_childs_log_flush_is_not_doubled_by_a_fork)
{
        return leaving_says(LEAVE_FORK_LOG, text(LEAVING_FIRST));
}

/*
        The two buffers are two buffers, and a program that writes through
        both has to be able to predict which arrives first.

        They are independent all the way down: the stream buffer is stream.c's
        and the log buffer is lib.c's, they are flushed by different code and
        they reach descriptor one by different calls. So a hand flush of the
        log goes out immediately and a printf still waiting in the stream
        buffer goes out later, whatever order the two calls were written in.
        This pins that, because a program mixing printf and
        string_format(log, ...) and expecting source order is going to be
        surprised, and the surprise should be documented rather than found.
*/
test(a_hand_flushed_log_lands_before_a_stream_still_waiting)
{
        return leaving_says(LEAVE_BOTH, text(LEAVING_SECOND LEAVING_FIRST));
}

test(streams_left_open_are_flushed_at_exit_newest_first_after_closes)
{
        return leaving_says(LEAVE_MANY_STREAMS,
                            text("s1\ns3\ns4\ns2\ns0\n"));
}

//      Every pairing of two parent steps -- the second one of none, a whole
//      line, a flush or a partial line -- with one child step, fully and
//      line buffered: 800 forks, each checked against the model.
test(fork_ownership_follows_the_policy_for_every_generated_pairing)
{
        static const b32 second[] = {0, 2, LEAVING_STEP_FLUSH, 1};
        p8 seen[512];
        leaving_text want;
        b32 line;
        b32 first;
        b32 other;
        b32 child;

        for (line = 0; line < 2; line++)
                for (first = 0; first < LEAVING_STEPS; first++)
                        for (other = 0; other < (b32)array_count(second); other++)
                                for (child = 0; child < LEAVING_STEPS; child++)
                                {
                                        b32 which = LEAVE_MATRIX | (line << 12) |
                                                    (first << 8) |
                                                    (second[other] << 4) | child;
                                        positive filled = 0;
                                        b32 status;

                                        leaving_matrix_expect(which,
                                                              address_of want);
                                        status = leaving_run(which, seen,
                                                             sizeof(seen),
                                                             address_of filled);

                                        if (status == LEAVE_MATRIX_STATUS &&
                                            filled == want.length &&
                                            memory_compare(seen, want.bytes,
                                                           filled) == 0)
                                                continue;

                                        string_format(log,
                                                      "\n  pairing %p: status %b, %p bytes where %p were due\n",
                                                      (positive)which, status,
                                                      filled, want.length);
                                        return false;
                                }

        return true;
}

/*
        environ, which the stdlib family left out until there was a C startup
        to point it at anything.

        The first check has to be first: getenv takes a private copy of the
        vector the moment it is called, and publishes the copy over what the
        shim published, so this is the only place in the file where the
        kernel's own vector is still what environ holds.
*/
static string_address address_to leaving_environ_at_start = null;

test(environ_holds_the_kernel_vector_before_anything_asks)
{
        fail(!is_null(leaving_environ_at_start));
        fail(leaving_environ_at_start == program_environment_list());

        //      An empty environment is a vector that is its null alone, and
        //      is still the kernel's vector: the lane runs this under
        //      environments of every size from none, so no entry is assumed.

        return true;
}

//      Every entry, as a name and a value, has to be the entry getenv finds
//      under that name. A vector that were merely non-null would pass a
//      liveness check and still be the wrong vector.
test(environ_agrees_with_getenv_entry_by_entry)
{
        string_address address_to walk;
        positive seen = 0;

        for (walk = environ; !is_null(*walk); walk++)
        {
                p8 name[256];
                p8 address_to equals = string_first_of(*walk, '=');
                positive length;
                string_address found;

                fail(!is_null(equals));

                length = (positive)(equals - *walk);

                fail(length > 0 && length < sizeof(name));

                memory_copy(name, *walk, length);
                name[length] = end;

                found = getenv((string_address)name);

                fail(!is_null(found));
                fail(string_compare(found, (string_address)(equals + 1)) == 0);

                seen++;
        }

        //      Every entry there was agreed; with an empty environment there
        //      were none, and that agrees too.
        (void)seen;

        return true;
}

//      getenv is what takes the private copy, so by here environ must be the
//      copy rather than the kernel's vector it started as.
test(environ_is_the_vector_the_family_owns_once_it_owns_one)
{
        fail(!is_null(getenv(text("PATH"))) || true);
        fail(environ == stdlib_environment_list());

        return true;
}

/*
        Growth is where a published pointer goes stale, and it is the reason
        publishing is not one act at startup. The vector starts with eight
        spare slots and is replaced wholesale when they run out, so forty
        names guarantee at least one replacement and the check is that environ
        followed it.
*/
#define LEAVING_NAMES 40
#define LEAVING_MADE "LEAVING_MADE_"
#define LEAVING_VALUE "value "

//      Built with the library rather than with snprintf: positive_into is the
//      assembly that turns a number into digits and answers how many it wrote,
//      and the two prefixes are literals whose length the known-size copy
//      folds where the call is written.
static fn leaving_name(p8 address_to name, p8 address_to value, positive index)
{
        positive length;

        memory_copy(name, LEAVING_MADE, sizeof(LEAVING_MADE) - 1);
        length = positive_into(name + sizeof(LEAVING_MADE) - 1, index);
        name[sizeof(LEAVING_MADE) - 1 + length] = end;

        memory_copy(value, LEAVING_VALUE, sizeof(LEAVING_VALUE) - 1);
        length = positive_into(value + sizeof(LEAVING_VALUE) - 1, index);
        value[sizeof(LEAVING_VALUE) - 1 + length] = end;
}

test(environ_follows_setenv_through_a_growth)
{
        positive index;

        for (index = 0; index < LEAVING_NAMES; index++)
        {
                p8 name[32];
                p8 value[32];

                leaving_name(name, value, index);

                fail(setenv((string_address)name, (string_address)value, 1) == 0);
        }

        fail(environ == stdlib_environment_list());

        for (index = 0; index < LEAVING_NAMES; index++)
        {
                p8 name[32];
                p8 value[32];
                string_address address_to walk;
                p8 found = false;

                leaving_name(name, value, index);

                //      Walked rather than looked up, because the question is
                //      whether the vector environ names holds the entry, not
                //      whether getenv can find it in the one it holds itself.
                for (walk = environ; !is_null(*walk); walk++)
                {
                        p8 address_to equals = string_first_of(*walk, '=');

                        if (is_null(equals))
                                continue;

                        if (string_compare((string_address)(equals + 1),
                                           (string_address)value) != 0)
                                continue;

                        if ((positive)(equals - *walk) !=
                            string_length((string_address)name))
                                continue;

                        if (memory_compare(*walk, name,
                                           string_length((string_address)name)) == 0)
                                found = true;
                }

                fail(found);
        }

        return true;
}

//      unsetenv moves entries about inside the vector it was given, so the
//      pointer must not move and the name must be gone from what environ
//      names rather than only from what getenv looks at. The pointer is
//      taken after the setenv: that call may be the one that grows the
//      vector and publishes a new one, and whether it is depends on how many
//      entries the process was started with (24 made it so, and the row
//      failed for a lane whose environment held that many).
test(environ_follows_unsetenv_without_moving)
{
        string_address address_to before;
        string_address address_to walk;
        positive left = 0;

        fail(setenv(text("LEAVING_GOING"), text("here"), 1) == 0);
        fail(!is_null(getenv(text("LEAVING_GOING"))));
        before = environ;
        fail(unsetenv(text("LEAVING_GOING")) == 0);
        fail(is_null(getenv(text("LEAVING_GOING"))));
        fail(environ == before);

        for (walk = environ; !is_null(*walk); walk++)
        {
                p8 address_to equals = string_first_of(*walk, '=');

                if (is_null(equals))
                        continue;

                if ((positive)(equals - *walk) == 13 &&
                    memory_compare(*walk, "LEAVING_GOING", 13) == 0)
                        left++;
        }

        fail(left == 0);

        return true;
}

//      putenv installs the caller's own bytes into the same vector, and
//      environ has to be showing that vector.
static p8 leaving_entry[32] = "LEAVING_HELD=before";

test(environ_shows_what_putenv_installed)
{
        string_address address_to walk;
        p8 found = false;

        fail(putenv(leaving_entry) == 0);

        for (walk = environ; !is_null(*walk); walk++)
                if (*walk == leaving_entry)
                        found = true;

        fail(found);

        return true;
}

static test_case test_cases[] = {
        case(environ_holds_the_kernel_vector_before_anything_asks),
        case(printf_then_exit_prints),
        case(printf_then_underscore_exit_prints_nothing),
        case(printf_then_capital_exit_prints_nothing),
        case(printf_then_quick_exit_prints_nothing),
        case(an_atexit_handler_writes_before_the_flush),
        case(the_initial_identity_is_this_process_and_stays_put),
        case(a_forked_child_leaving_does_not_print_the_parents_bytes_again),
        case(two_forked_children_leaving_still_print_it_once),
        case(a_forked_child_with_a_clean_buffer_prints_its_own),
        case(a_forked_child_writing_into_an_inherited_buffer_loses_its_own),
        case(an_explicit_flush_in_a_child_still_writes_what_it_inherited),
        case(a_forked_childs_log_buffer_is_not_flushed_for_it),
        case(a_forked_child_can_still_flush_the_log_itself),
        case(a_forked_childs_log_flush_is_not_doubled_by_a_fork),
        case(a_hand_flushed_log_lands_before_a_stream_still_waiting),
        case(streams_left_open_are_flushed_at_exit_newest_first_after_closes),
        case(fork_ownership_follows_the_policy_for_every_generated_pairing),
        case(environ_agrees_with_getenv_entry_by_entry),
        case(environ_is_the_vector_the_family_owns_once_it_owns_one),
        case(environ_follows_setenv_through_a_growth),
        case(environ_follows_unsetenv_without_moving),
        case(environ_shows_what_putenv_installed),
        {null, null},
};

b32 main(void)
{
        //      Read before anything else in the program can call getenv,
        //      because the first getenv replaces what the shim published.
        leaving_environ_at_start = environ;

        log_direct(str("leaving tests\n\n"));

        test_cases_walk(test_cases);

        /*
                Written into the log buffer and deliberately not flushed.

                Returning from main is defined by C to be a call to exit, and
                the umbrella's startup shim is what makes that true here. If
                it stops being true these bytes never leave the buffer, the
                runner finds no verdict line at all, and it says so -- which
                is a louder failure than a count being one short and is the
                only way this particular claim can be checked from inside the
                process making it.
        */
        string_format(log, "\n%p checks, %p failures\n", checks, failures);

        return failures > 0 ? 1 : 0;
}
#endif /* CHECK_leaving */

#ifdef CHECK_number
//      The widest integer the comparison tier builds, watched through the
//      hook standard.c leaves for it, so the limb count stays a measured
//      claim rather than an arithmetic one.
static int number_widest_limbs;
#define NUMBERS_BIG_WATCH(limbs)                                              \
        (number_widest_limbs = (limbs) > number_widest_limbs ? (limbs)        \
                                                             : number_widest_limbs)
#include "../../src/lib.util.c"

/*
        The number conversions: text into an integer, and text into a float
        that is the nearest one.

        Every strtod case here also exercises lib.c's
        string_to_decimal_short, which reads a plain run of at most fifteen
        digits and hands everything harder to the general path. Its bail set
        is what this lane is for: the first version answered nought for
        0x1p0 and failed 129 of these cases, all hexadecimal.

        Six things are checked here and they are checked in six different
        ways, because the failure modes are not alike.

        THE INTEGER NAMES ARE CHECKED BY BEING CALLED. abs, labs, llabs, atoi,
        atol, atoll, strtol, strtoll, strtoul, strtoull, imaxabs, strtoimax
        and strtoumax are assembly in src/lib.c with no C
        declaration in front of them, which meant that until numbers.c every
        one of those call sites was a compile error in a file that linked
        perfectly. So the first group is a call to each of them with an answer
        beside it: it is a test of the arithmetic, and it is also the only
        thing that can prove the declarations are right, because a wrong one
        does not link and a missing one does not compile.

        THE FLOAT ANSWERS ARE GLIBC'S, ASKED LIVE. The numbers lane runs
        every listed input of SHARED_number_stream -- the boundaries, the
        subnormals, the classic hard cases, the hexadecimal corners and the
        malformed strings -- and every drawn one against the machine's glibc,
        bits, end pointer and errno. Here only where the three conversions
        stop is held together.

        THE THREE TIERS ARE CHECKED AGAINST EACH OTHER. numbers.c answers a
        conversion three ways depending on the input -- an exact multiply, a
        128 bit estimate, or a decimal register that is exact by construction
        -- and the last of those is reachable on its own by calling
        numbers_read and numbers_assemble, which are static in the same
        translation unit this file is. So every generated input is converted
        twice, once by strtod and once by the register alone, and they have to
        agree. That finds a defect in either fast tier without a reference
        library being anywhere near it.

        THE SHIFT TABLE IS CHECKED AGAINST ITSELF. The decimal register
        multiplies by two to the k in one pass using a table that says how
        many digits the answer gains. Shifting by one, k times, uses only the
        first entry of that table and reaches the same number, so the two
        routes are compared for every k up to sixty on a dozen registers.

        MONOTONICITY, which is the property a conversion can lose without any
        single answer looking wrong: a larger decimal must never convert to a
        smaller float.

        AND THE SMALL INTEGERS EXHAUSTIVELY, in three spellings each, against
        the compiler's own conversion of the same number.
*/

#define number_case(name) p8 number_test_##name(void)

static positive number_checks = 0;
static positive number_failures = 0;
static positive number_reported = 0;

static fn number_note(p8 held, string_address what, positive got, positive wanted)
{
        number_checks++;

        if (held)
                return;

        number_failures++;

        if (number_reported < 40)
        {
                number_reported++;
                string_format(log, "FAIL %s: got %p wanted %p\n", what, got, wanted);
        }
}

static fn number_say(p8 held, string_address what)
{
        number_checks++;

        if (held)
                return;

        number_failures++;

        if (number_reported < 40)
        {
                number_reported++;
                string_format(log, "FAIL %s\n", what);
        }
}

#define text(literal) ((string_address)(literal))

typedef union
{
        decimal value;
        p64 bits;
} number_wide_shape;

typedef union
{
        f32 value;
        p32 bits;
} number_narrow_shape;

typedef union
{
        f128 value;
        p128 bits;
} number_extended_shape;

//      The bits of a long double, with what the compiler knew of where they
//      came from taken away: their compare against the bits of an infinity
//      or a NaN is something clang turns back into a long double compare, a
//      call to __eqtf2 or __unordtf2 that a freestanding link has no library
//      to answer.
static p128 number_extended_bits(number_extended_shape shape)
{
        p128 bits = shape.bits;

        __asm__("" : "+m"(bits));
        return bits;
}

typedef struct
{
        const char address_to input;
        b32 base;
        p64 answer;
        b32 out_of_range;
} number_integer_range_case;

static const number_integer_range_case number_signed_range_cases[] = {
        {"9223372036854775807!", 10, 0x7fffffffffffffffULL, 0},
        {"9223372036854775808!", 10, 0x7fffffffffffffffULL, 1},
        {"-9223372036854775808!", 10, 0x8000000000000000ULL, 0},
        {"-9223372036854775809!", 10, 0x8000000000000000ULL, 1},
        {"0x7fffffffffffffff!", 0, 0x7fffffffffffffffULL, 0},
        {"0x8000000000000000!", 0, 0x7fffffffffffffffULL, 1},
        {"-0x8000000000000000!", 0, 0x8000000000000000ULL, 0},
        {"-0x8000000000000001!", 0, 0x8000000000000000ULL, 1},
        {"111111111111111111111111111111111111111111111111111111111111111!",
         2, 0x7fffffffffffffffULL, 0},
        {"1000000000000000000000000000000000000000000000000000000000000000!",
         2, 0x7fffffffffffffffULL, 1},
        {"777777777777777777777!", 8, 0x7fffffffffffffffULL, 0},
        {"1000000000000000000000!", 8, 0x7fffffffffffffffULL, 1},
        {"7fffffffffffffff!", 16, 0x7fffffffffffffffULL, 0},
        {"8000000000000000!", 16, 0x7fffffffffffffffULL, 1},
        {"1y2p0ij32e8e7!", 36, 0x7fffffffffffffffULL, 0},
        {"1y2p0ij32e8e8!", 36, 0x7fffffffffffffffULL, 1},
};

static const number_integer_range_case number_unsigned_range_cases[] = {
        {"18446744073709551615!", 10, 0xffffffffffffffffULL, 0},
        {"18446744073709551616!", 10, 0xffffffffffffffffULL, 1},
        {"0xffffffffffffffff!", 0, 0xffffffffffffffffULL, 0},
        {"0x10000000000000000!", 0, 0xffffffffffffffffULL, 1},
        {"1111111111111111111111111111111111111111111111111111111111111111!",
         2, 0xffffffffffffffffULL, 0},
        {"10000000000000000000000000000000000000000000000000000000000000000!",
         2, 0xffffffffffffffffULL, 1},
        {"1777777777777777777777!", 8, 0xffffffffffffffffULL, 0},
        {"2000000000000000000000!", 8, 0xffffffffffffffffULL, 1},
        {"ffffffffffffffff!", 16, 0xffffffffffffffffULL, 0},
        {"10000000000000000!", 16, 0xffffffffffffffffULL, 1},
        {"3w5e11264sgsf!", 36, 0xffffffffffffffffULL, 0},
        {"3w5e11264sgsg!", 36, 0xffffffffffffffffULL, 1},
};

//      Going through a volatile pointer prevents atoi's PURE declaration from
//      making the errno-preservation check below true without a runtime call.
static int (*volatile number_atoi_call)(const char address_to) = atoi;


/*
        The integer half, which is the half that had no declarations.

        Every call below was a compile error in this tree until numbers.c
        declared the name, and the symbol each one resolves to has been in the
        built object all along.
*/
number_case(integers)
{
        string_address stop;
        positive index;

        number_note(abs(-5) == 5, text("abs"), (positive)abs(-5), 5);
        number_note(abs(0) == 0, text("abs zero"), (positive)abs(0), 0);
        number_note(labs(-9223372036854775807LL) == 9223372036854775807LL,
                    text("labs"), (positive)labs(-9223372036854775807LL),
                    9223372036854775807ULL);
        number_note(llabs(-1) == 1, text("llabs"), (positive)llabs(-1), 1);
        number_note(imaxabs(-42) == 42, text("imaxabs"), (positive)imaxabs(-42), 42);

        number_note(atoi(text("42")) == 42, text("atoi"),
                    (positive)atoi(text("42")), 42);
        number_note(atoi(text("  -17abc")) == -17, text("atoi signed"),
                    (positive)(bipolar)atoi(text("  -17abc")), (positive)(bipolar)-17);
        number_note(atoi(text("xyz")) == 0, text("atoi none"),
                    (positive)atoi(text("xyz")), 0);
        number_note(atol(text("9223372036854775807")) == 9223372036854775807LL,
                    text("atol"), (positive)atol(text("9223372036854775807")),
                    9223372036854775807ULL);
        number_note(atoll(text("-1")) == -1, text("atoll"),
                    (positive)atoll(text("-1")), (positive)-1);

        stop = null;
        number_note(strtol(text("  -0x1fzz"), (char address_to address_to)address_of stop, 0) == -31,
                    text("strtol base zero"),
                    (positive)(bipolar)strtol(text("  -0x1fzz"), null, 0),
                    (positive)(bipolar)-31);
        number_say(stop != null && address_to stop == 'z', text("strtol endptr"));

        stop = null;
        number_note(strtol(text("0x"), (char address_to address_to)address_of stop, 0) == 0,
                    text("strtol bare prefix"), 0, 0);
        number_say(stop != null && address_to stop == 'x',
                   text("strtol bare prefix endptr"));

        stop = null;
        errno = EDOM;
        number_note(strtol(text("zzz"), (char address_to address_to)address_of stop, 10) == 0,
                    text("strtol no conversion"), 0, 0);
        number_say(stop != null && address_to stop == 'z',
                   text("strtol no conversion endptr"));
        number_note(errno == EDOM, text("strtol no conversion errno"), errno,
                    EDOM);

        stop = null;
        errno = EDOM;
        number_note(strtoul(text("10"),
                            (char address_to address_to)address_of stop, 1) == 0,
                    text("strtoul invalid base"), 0, 0);
        number_say(stop == text("10"), text("strtoul invalid base endptr"));
        number_note(errno == EDOM, text("strtoul invalid base errno"), errno,
                    EDOM);

        number_note(strtoll(text("777"), null, 8) == 511, text("strtoll octal"),
                    (positive)strtoll(text("777"), null, 8), 511);
        number_note(strtoul(text("-1"), null, 10) == 18446744073709551615ULL,
                    text("strtoul wrap"), strtoul(text("-1"), null, 10),
                    18446744073709551615ULL);
        number_note(strtoull(text("ffffffffffffffff"), null, 16) ==
                            18446744073709551615ULL,
                    text("strtoull hex"),
                    strtoull(text("ffffffffffffffff"), null, 16),
                    18446744073709551615ULL);
        number_note(strtoimax(text("-1234"), null, 10) == -1234,
                    text("strtoimax"),
                    (positive)strtoimax(text("-1234"), null, 10),
                    (positive)(bipolar)-1234);
        number_note(strtoumax(text("1234"), null, 10) == 1234, text("strtoumax"),
                    strtoumax(text("1234"), null, 10), 1234);

        /*
                Every integer width here is sixty four bits, but the four C
                entry points are called separately so an alias or wrapper can
                never make one of them silently lose the range report. Bases
                0, 2, 8, 10, 16 and 36 cover prefix selection, both digit
                alphabets, the multiply path and the power-of-two path. The
                marker proves an overflowing run is consumed in full.
        */
        for (index = 0;
             index < sizeof number_signed_range_cases /
                             sizeof number_signed_range_cases[0];
             index++)
        {
                const number_integer_range_case address_to one =
                        address_of number_signed_range_cases[index];
                long first;
                long long second;

                stop = null;
                errno = EDOM;
                first = strtol(one->input,
                               (char address_to address_to)address_of stop,
                               one->base);
                number_note((p64)first == one->answer, text("strtol boundary"),
                            (p64)first, one->answer);
                number_note(errno == (one->out_of_range ? ERANGE : EDOM),
                            text("strtol boundary errno"), errno,
                            one->out_of_range ? ERANGE : EDOM);
                number_say(stop != null && address_to stop == '!',
                           text("strtol boundary endptr"));

                stop = null;
                errno = EDOM;
                second = strtoll(one->input,
                                 (char address_to address_to)address_of stop,
                                 one->base);
                number_note((p64)second == one->answer,
                            text("strtoll boundary"), (p64)second,
                            one->answer);
                number_note(errno == (one->out_of_range ? ERANGE : EDOM),
                            text("strtoll boundary errno"), errno,
                            one->out_of_range ? ERANGE : EDOM);
                number_say(stop != null && address_to stop == '!',
                           text("strtoll boundary endptr"));
        }

        for (index = 0;
             index < sizeof number_unsigned_range_cases /
                             sizeof number_unsigned_range_cases[0];
             index++)
        {
                const number_integer_range_case address_to one =
                        address_of number_unsigned_range_cases[index];
                unsigned long first;
                unsigned long long second;

                stop = null;
                errno = EDOM;
                first = strtoul(one->input,
                                (char address_to address_to)address_of stop,
                                one->base);
                number_note((p64)first == one->answer,
                            text("strtoul boundary"), (p64)first, one->answer);
                number_note(errno == (one->out_of_range ? ERANGE : EDOM),
                            text("strtoul boundary errno"), errno,
                            one->out_of_range ? ERANGE : EDOM);
                number_say(stop != null && address_to stop == '!',
                           text("strtoul boundary endptr"));

                stop = null;
                errno = EDOM;
                second = strtoull(one->input,
                                  (char address_to address_to)address_of stop,
                                  one->base);
                number_note((p64)second == one->answer,
                            text("strtoull boundary"), (p64)second,
                            one->answer);
                number_note(errno == (one->out_of_range ? ERANGE : EDOM),
                            text("strtoull boundary errno"), errno,
                            one->out_of_range ? ERANGE : EDOM);
                number_say(stop != null && address_to stop == '!',
                           text("strtoull boundary endptr"));
        }

        //      A valid negative magnitude is reduced modulo the unsigned
        //      width; only a magnitude that itself overflowed reports range.
        errno = EDOM;
        number_note(strtoul(text("-18446744073709551615"), null, 10) == 1,
                    text("strtoul negative boundary"),
                    strtoul(text("-18446744073709551615"), null, 10), 1);
        number_note(errno == EDOM, text("strtoul negative boundary errno"),
                    errno, EDOM);
        errno = EDOM;
        number_note(strtoul(text("-18446744073709551616"), null, 10) ==
                            0xffffffffffffffffULL,
                    text("strtoul negative overflow"),
                    strtoul(text("-18446744073709551616"), null, 10),
                    0xffffffffffffffffULL);
        number_note(errno == ERANGE, text("strtoul negative overflow errno"),
                    errno, ERANGE);

        //      Exercise the constant-base expansion directly. It must carry
        //      the same status and end pointer as the assembly checked entry,
        //      including the no-conversion case.
        {
                b32 out_of_range = -1;
                bipolar folded;

                stop = null;
                folded = string_to_number_checked(
                        text("9223372036854775808!"), address_of stop, 10,
                        address_of out_of_range);
                number_note((p64)folded == 0x7fffffffffffffffULL,
                            text("folded signed boundary"), (p64)folded,
                            0x7fffffffffffffffULL);
                number_say(out_of_range == 1 && stop && address_to stop == '!',
                           text("folded signed range and endptr"));

                out_of_range = -1;
                stop = null;
                folded = string_to_number_checked(text("nope"), address_of stop,
                                                  16, address_of out_of_range);
                number_say(folded == 0 && out_of_range == 0 &&
                                   stop == text("nope"),
                           text("folded no conversion"));

                out_of_range = -1;
                stop = null;
                number_note(string_to_number_unsigned_checked(
                                    text("xyz"), address_of stop, 10,
                                    address_of out_of_range) == 0,
                            text("folded unsigned no conversion"), 0, 0);
                number_say(out_of_range == 0 && stop == text("xyz"),
                           text("folded unsigned no conversion state"));

                out_of_range = -1;
                stop = null;
                number_note((p64)(string_to_number_checked)(
                                    text("none"), address_of stop, 10,
                                    address_of out_of_range) == 0,
                            text("dynamic checked no conversion"), 0, 0);
                number_say(out_of_range == 0 && stop == text("none"),
                           text("dynamic checked no conversion state"));

                out_of_range = -1;
                stop = null;
                number_note((string_to_number_unsigned_checked)(
                                    text("10"), address_of stop, 37,
                                    address_of out_of_range) == 0,
                            text("dynamic checked invalid base"), 0, 0);
                number_say(out_of_range == 0 && stop == text("10"),
                           text("dynamic checked invalid base state"));

                number_note((p64)(string_to_number_checked)(
                                    text("9223372036854775808"), null, 10,
                                    null) == 0x7fffffffffffffffULL,
                            text("dynamic checked optional status"),
                            0x7fffffffffffffffULL,
                            0x7fffffffffffffffULL);
        }

        //      The raw parser and the pure atoi alias do not own errno. Even
        //      their saturating paths must leave caller state alone.
        errno = EDOM;
        number_note((p64)(string_to_number)(
                            text("9223372036854775808"), null, 10) ==
                            0x7fffffffffffffffULL,
                    text("raw signed overflow"), 0x7fffffffffffffffULL,
                    0x7fffffffffffffffULL);
        number_note(errno == EDOM, text("raw signed overflow errno"), errno,
                    EDOM);
        errno = EDOM;
        b32 narrowed = number_atoi_call(
                (const char address_to)text("99999999999999999999"));
        number_note(narrowed == -1,
                    text("atoi overflowing raw value"),
                    (positive)(bipolar)narrowed, (positive)(bipolar)-1);
        number_note(errno == EDOM, text("atoi overflow errno"), errno, EDOM);

        //      The addresses have to be takeable, which a macro alias could
        //      not give and an undeclared name could not give either.
        number_say((address_any)address_of strtol != null &&
                           (address_any)address_of strtod != null,
                   text("addresses of the names"));

        return number_failures == 0;
}

/*
        long double, without a reference and without arithmetic.

        There is no baked table here because there could not be one that ran
        everywhere: long double is the eighty bit x87 format on x86_64 and
        binary128 on arm64 and riscv64, so the bits are different answers to
        the same question. What is architecture independent is that a decimal
        which is exactly a double must be exactly the same number as a long
        double, and the bits of that wider number can be built out of the bits
        of the narrower one -- shift the significand up, rebias the exponent,
        carry the sign. So the check is a conversion this file computes
        against a conversion the library computed, in whichever of the two
        formats the machine has.

        Nothing here compares two long doubles with an operator. A -nostdlib
        link has no libgcc under it and a single binary128 comparison is a
        call to __letf2 that would not resolve; every comparison below is
        between integers taken out of a union.

        And on x86_64 only ten of the sixteen bytes of a long double mean
        anything. The other six are padding the ABI never defines -- a long
        double comes back from a call in an x87 register and is stored into
        whatever the caller's stack slot held -- so the comparison masks down
        to the eighty bits that exist. Comparing all sixteen bytes was the
        first version of this and it failed on every line, including on zero.
*/
static p8 number_extended_same(p128 left, p128 right)
{
#if __LDBL_MANT_DIG__ == 64
        return (p64)left == (p64)right &&
               (((p64)(left >> 64)) & 0xffff) == (((p64)(right >> 64)) & 0xffff);
#else
        return left == right;
#endif
}

static p128 number_widened(p64 wide_bits)
{
        p64 sign = wide_bits >> 63;
        p64 field = (wide_bits >> 52) & 0x7ff;
        p64 fraction = wide_bits & 0xfffffffffffffULL;
        p128 bits;

        if (field == 0 && fraction == 0)
#if __LDBL_MANT_DIG__ == 64
                return (p128)sign << 79;
#else
                return (p128)sign << 127;
#endif

        //      A saturated exponent stays saturated rather than being
        //      rebiased: an infinity is an infinity in both formats and its
        //      field is all ones in both.
        {
                p64 wider = field == 0x7ff ? 0x7fff : (field - 1023 + 16383) & 0x7fff;

#if __LDBL_MANT_DIG__ == 64
                bits = (p128)(((p64)1 << 63) | (fraction << 11));
                bits |= (p128)wider << 64;
                bits |= (p128)sign << 79;
#else
                bits = (p128)fraction << 60;
                bits |= (p128)wider << 112;
                bits |= (p128)sign << 127;
#endif
        }

        return bits;
}

number_case(extended)
{
        //      Every one of these is a dyadic rational small enough to be a
        //      double exactly, which is what makes the widening the right
        //      answer. A decimal that is only NEAR a double -- 0.1, or
        //      1e100 -- is a different number in the two formats, and
        //      comparing them would be comparing two correct answers.
        static const char address_to exactly[] = {
                "0", "-0", "1", "-1", "2", "0.5", "-0.5", "1.5", "0.25",
                "1024", "-4096.125", "3", "7.75", "1e10", "-1e10", "65536",
                "9007199254740992", "-9007199254740992", "0.00390625",
                "1e15", "-1e15", "1048576.5", "255.9375", "1e0", "8388608"};
        positive index;
        positive total = sizeof exactly / sizeof exactly[0];
        number_extended_shape extended;
        number_wide_shape wide;

        for (index = 0; index < total; index++)
        {
                string_address input = (string_address)exactly[index];

                wide.value = strtod(input, null);
                extended.value = strtold(input, null);

                number_say(number_extended_same(number_extended_bits(extended),
                                                number_widened(wide.bits)),
                           input);
        }

        //      The specials, in whichever format the machine has.
        extended.value = strtold(text("inf"), null);
        number_say(number_extended_same(number_extended_bits(extended),
                                        number_widened(0x7ff0000000000000ULL)),
                   text("strtold inf"));

        extended.value = strtold(text("-inf"), null);
        number_say(number_extended_same(number_extended_bits(extended),
                                        number_widened(0xfff0000000000000ULL)),
                   text("strtold -inf"));

        //      A NaN is not the widening of a NaN, so this asks the two
        //      questions a NaN has to answer: saturated exponent, and a
        //      significand that is not zero.
        extended.value = strtold(text("nan"), null);
#if __LDBL_MANT_DIG__ == 64
        number_say(((p64)(number_extended_bits(extended) >> 64) & 0x7fff) == 0x7fff &&
                           (p64)number_extended_bits(extended) != 0,
                   text("strtold nan"));
#else
        number_say(((p64)(number_extended_bits(extended) >> 112) & 0x7fff) == 0x7fff &&
                           (number_extended_bits(extended) & ((((p128)1 << 112) - 1))) != 0,
                   text("strtold nan"));
#endif

        static const struct { const char *input; p64 bits; positive stop; b32 error; } edges[] = {
                {"", 0, 0, 99}, {" +", 0, 0, 99}, {"-0!", 1ULL << 63, 2, 99},
                {"0x!", 0, 1, 99}, {"inf!", 0x7ff0000000000000ULL, 3, 99},
                {"-1e99999!", 0xfff0000000000000ULL, 8, ERANGE},
                {"-1e-99999!", 1ULL << 63, 9, ERANGE},
        };
        for (positive i = 0; i < array_count(edges); i++)
        {
                char *stop;
                errno = 99;
                extended.value = strtold(edges[i].input, &stop);
                number_say(number_extended_same(number_extended_bits(extended), number_widened(edges[i].bits)),
                           text("extended dispatch bits"));
                number_say((positive)(stop - edges[i].input) == edges[i].stop,
                           text("extended dispatch end pointer"));
                number_say(errno == edges[i].error, text("extended dispatch errno retention/range"));
        }

        return number_failures == 0;
}

/*
        The public floating wrappers take ordinary terminated strings.  The
        maximum used while recognizing inf/nan does not make the bytes after
        that terminator readable, so put the terminator at the final byte of a
        mapped page and exercise every wrapper that shares numbers_read.
*/
number_case(page_edge_names)
{
        p8 address_to pages = memory(3 * 4096);

        number_say(!system_failed(pages) && pages != null,
                   text("floating-name guard allocation"));
        if (system_failed(pages) || !pages)
                return false;

        p8 guarded =
            !system_call_3(syscall(mprotect), (positive)pages, 4096, 0) &&
            !system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0);
        number_say(guarded, text("floating-name guard protection"));
        if (guarded)
        {
                static string_address names[] = {"inf", "nan"};

                for (positive which = 0; which < array_count(names); which++)
                {
                        string_address input = pages + 8192 - 4;
                        string_address stop = null;
                        number_wide_shape wide;
                        number_wide_shape alias;
                        number_narrow_shape narrow;
                        number_extended_shape extended;

                        memory_copy(input, names[which], 4);
                        wide.value = strtod(input,
                            (char address_to address_to)address_of stop);
                        number_say(stop == input + 3,
                                   text("strtod page-edge end pointer"));

                        stop = null;
                        narrow.value = strtof(input,
                            (char address_to address_to)address_of stop);
                        number_say(stop == input + 3,
                                   text("strtof page-edge end pointer"));

                        stop = null;
                        extended.value = strtold(input,
                            (char address_to address_to)address_of stop);
                        number_say(stop == input + 3,
                                   text("strtold page-edge end pointer"));

                        alias.value = atof(input);
                        if (!which)
                        {
                                number_say(wide.bits == 0x7ff0000000000000ULL &&
                                               alias.bits == wide.bits &&
                                               narrow.bits == 0x7f800000u &&
                                               number_extended_same(
                                                   number_extended_bits(extended),
                                                   number_widened(wide.bits)),
                                           text("page-edge infinity values"));
                        }
                        else
                        {
                                p8 extended_nan;
#if __LDBL_MANT_DIG__ == 64
                                extended_nan =
                                    ((p64)(number_extended_bits(extended) >> 64) & 0x7fff) ==
                                        0x7fff &&
                                    (p64)number_extended_bits(extended) != 0;
#else
                                extended_nan =
                                    ((p64)(number_extended_bits(extended) >> 112) & 0x7fff) ==
                                        0x7fff &&
                                    (number_extended_bits(extended) & (((p128)1 << 112) - 1)) != 0;
#endif
                                number_say(
                                    (wide.bits & 0x7ff0000000000000ULL) ==
                                            0x7ff0000000000000ULL &&
                                        (wide.bits & 0xfffffffffffffULL) != 0 &&
                                        (alias.bits & 0x7ff0000000000000ULL) ==
                                            0x7ff0000000000000ULL &&
                                        (alias.bits & 0xfffffffffffffULL) != 0 &&
                                        (narrow.bits & 0x7f800000u) ==
                                            0x7f800000u &&
                                        (narrow.bits & 0x7fffffu) != 0 &&
                                        extended_nan,
                                    text("page-edge NaN values"));
                        }
                }
        }

        memory_free(pages, 3 * 4096);
        return number_failures == 0;
}

/*
        The shift table, checked against the one entry of it that cannot be
        wrong.

        numbers_shift_left multiplies the decimal register by two to the k in
        a single pass, and to write from the right it must know beforehand how
        many digits the answer will gain. That count comes from a table, and a
        table is exactly the kind of thing that is right for fifty nine
        entries and wrong for one. Shifting by one, sixty times, uses only the
        first entry and reaches the same number by a different road, so the
        two roads are compared -- for every k, on registers built from real
        digits rather than from a single one.
*/
number_case(shift_table)
{
        static const char address_to seeds[] = {
                "1", "9", "1234567890", "5", "4999999999", "5000000001",
                "999999999999999999999", "1000000000000000000001",
                "31415926535897932384626433832795", "7", "12", "8888888888"};
        positive which;
        positive total = sizeof seeds / sizeof seeds[0];
        b32 places;

        for (which = 0; which < total; which++)
        {
                for (places = 1; places <= NUMBERS_SHIFT_MAX; places++)
                {
                        numbers_scan once;
                        numbers_scan step;
                        b32 count;

                        numbers_read((string_address)seeds[which], address_of once);
                        memory_copy(address_of step, address_of once, sizeof step);

                        numbers_shift_left(address_of once, places);

                        for (count = 0; count < places; count++)
                                numbers_shift_left(address_of step, 1);

                        number_say(once.count == step.count &&
                                           once.point == step.point &&
                                           memory_compare(once.digits, step.digits,
                                                          (positive)once.count) == 0,
                                   (string_address)seeds[which]);
                }
        }

        return number_failures == 0;
}

/*
        A generator, and the three tiers against each other.

        numbers_read and numbers_assemble are static in the translation unit
        this file compiles into, so the decimal register can be driven
        directly and its answer compared against whatever tier strtod chose.
        The register is exact by construction; a disagreement is a defect in
        the exact tier or in the estimating one, and it is found here rather
        than by a reference library that this program does not have.
*/
static p64 number_seed = 0x243f6a8885a308d3ULL;

static p64 number_random(void)
{
        p64 mixed = (number_seed += 0x9e3779b97f4a7c15ULL);

        mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;

        return mixed ^ (mixed >> 31);
}

static p8 number_text[128];

//      A decimal with the given number of significant digits and the given
//      power of ten, written the way a program writes one. positive_into and
//      bipolar_into do the two numeric parts; only the random digits in the
//      middle are a loop, because there is no routine that invents digits.
static string_address number_compose(b32 digits, b32 exponent)
{
        positive at = 0;
        b32 index;

        for (index = 0; index < digits; index++)
        {
                p64 digit = number_random() % 10;

                if (index == 0)
                        digit = 1 + number_random() % 9;

                number_text[at] = (p8)('0' + digit);
                at++;

                if (index == 0)
                {
                        number_text[at] = '.';
                        at++;
                }
        }

        number_text[at] = 'e';
        at++;
        at += bipolar_into_string(number_text + at, exponent);

        return number_text;
}

static p8 number_register_answer(string_address input, p64 address_to bits)
{
        numbers_scan scan;
        b32 condition = 0;

        numbers_read(input, address_of scan);

        if (scan.kind != NUMBERS_NUMBER || scan.hexadecimal)
                return false;

        address_to bits = (p64)numbers_assemble(address_of scan,
                                                address_of numbers_binary64,
                                                address_of condition);

        return true;
}

static p8 number_register_narrow(string_address input, p32 address_to bits)
{
        numbers_scan scan;
        b32 condition = 0;

        numbers_read(input, address_of scan);

        if (scan.kind != NUMBERS_NUMBER || scan.hexadecimal)
                return false;

        address_to bits = (p32)numbers_assemble(address_of scan,
                                                address_of numbers_binary32,
                                                address_of condition);

        return true;
}

#ifndef NUMBER_SWEEP
#define NUMBER_SWEEP 120000
#endif

number_case(tiers_agree)
{
        b32 round;

        for (round = 0; round < NUMBER_SWEEP; round++)
        {
                b32 digits = 1 + (b32)(number_random() % 25);
                b32 exponent = (b32)(number_random() % 700) - 350;
                string_address input = number_compose(digits, exponent);
                number_wide_shape wide;
                number_narrow_shape narrow;
                p64 slow_wide;
                p32 slow_narrow;

                wide.value = strtod(input, null);
                narrow.value = strtof(input, null);

                if (number_register_answer(input, address_of slow_wide))
                        number_note(wide.bits == slow_wide, input, wide.bits,
                                    slow_wide);

                if (number_register_narrow(input, address_of slow_narrow))
                        number_note(narrow.bits == slow_narrow, input,
                                    narrow.bits, slow_narrow);
        }

        return number_failures == 0;
}

/*
        Monotonicity, which is the property a conversion can lose one input at
        a time without any single answer looking wrong.

        A decimal that is larger must never convert to a float that is
        smaller. The pairs are a significand and the same significand plus
        one, at the same power of ten, across the whole exponent range
        including both ends of it, so the pairs that straddle a rounding
        boundary are the common case rather than the rare one.
*/
number_case(monotone)
{
        b32 round;

        for (round = 0; round < NUMBER_SWEEP / 2; round++)
        {
                p64 significand = number_random() % 10000000000000000ULL;
                b32 exponent = (b32)(number_random() % 700) - 350;
                positive at;
                number_wide_shape low;
                number_wide_shape high;

                at = positive_into(number_text, significand);
                number_text[at] = 'e';
                at++;
                at += bipolar_into_string(number_text + at, exponent);
                low.value = strtod(number_text, null);

                at = positive_into(number_text, significand + 1);
                number_text[at] = 'e';
                at++;
                at += bipolar_into_string(number_text + at, exponent);
                high.value = strtod(number_text, null);

                number_note(low.bits <= high.bits, text("monotone"), low.bits,
                            high.bits);
        }

        return number_failures == 0;
}

/*
        The small integers, exhaustively, in three spellings each.

        The compiler converted the same numbers when it built this file, so
        the comparison is against a second implementation of the same
        conversion rather than against a table -- and the three spellings put
        the same value through the integer path, the fraction path and the
        leading-zero path.
*/
number_case(small_integers)
{
        p64 value;

        for (value = 0; value < 100000; value++)
        {
                positive at = positive_into_string(number_text, value);
                number_wide_shape shape;
                number_narrow_shape narrow;

                shape.value = strtod(number_text, null);
                narrow.value = strtof(number_text, null);

                number_note(shape.value == (decimal)value, number_text,
                            (positive)shape.bits, value);
                number_note(narrow.value == (f32)value, number_text,
                            (positive)narrow.bits, value);

                number_text[at] = '.';
                number_text[at + 1] = '0';
                number_text[at + 2] = 0;
                shape.value = strtod(number_text, null);
                number_note(shape.value == (decimal)value, number_text,
                            (positive)shape.bits, value);

                //      The same number written as a fraction of ten, which
                //      takes the scaling path rather than the integer one.
                at = positive_into(number_text + 1, value);
                number_text[0] = '.';
                number_text[at + 1] = 'e';
                number_text[at + 2] = 0;
                at = positive_into_string(number_text + at + 2, at);
                shape.value = strtod(number_text, null);
                number_note(shape.value == (decimal)value, number_text,
                            (positive)shape.bits, value);
        }

        return number_failures == 0;
}

/*
        The short decimal, held to the general path over generated text.

        string_to_decimal_short answers only inside Clinger's window and
        declines everything else, and both halves of that are checked here on
        every case SHARED_number_stream makes: an answer has to be the general
        path's bits and end pointer with errno untouched, and a decline must
        leave the end pointer and the answer where they were. It is called by
        its own name, which on x86_64 is not what strtod reaches first --
        lib.util.c places an integer-only front half there -- so the
        routine is exercised on its own on all three machines. It must also
        take a real share of the stream, or agreeing proves nothing.
*/
#define SHARED_number_stream
#include "../checks.c"
#undef SHARED_number_stream

/*
        What glibc answers for the listed inputs is the numbers lane's to
        compare. What stays here needs no reference: the three conversions
        share one parser, so wherever they stop they stop together.
*/
number_case(listed_ends)
{
        for (positive index = 0; index < NS_LISTED; index++)
        {
                string_address input = (string_address)ns_listed[index];
                string_address wide_stop = null;
                string_address narrow_stop = null;
                string_address widest_stop = null;

                strtod(input, (char address_to address_to)address_of wide_stop);
                strtof(input, (char address_to address_to)address_of narrow_stop);
                strtold(input, (char address_to address_to)address_of widest_stop);
                number_note(narrow_stop == wide_stop && widest_stop == wide_stop, input,
                            (positive)(widest_stop - input), (positive)(wide_stop - input));
        }

        return number_failures == 0;
}

number_case(short_agrees)
{
        positive taken = 0;
        positive block;
        positive index;

        for (block = 0; block < 2; block++)
        {
                ns_seed(0x5348 + block);

                for (index = 0; index < NS_BLOCK; index++)
                {
                        string_address input;
                        string_address untouched = (string_address)1;
                        string_address quick_stop = untouched;
                        string_address general_stop = null;
                        number_wide_shape quick;
                        number_wide_shape general;
                        p8 took;

                        ns_case();
                        input = (string_address)ns_text;
                        quick.bits = 0x5a5a5a5a5a5a5a5aULL;
                        errno = 0;

                        took = (string_to_decimal_short)(input,
                                                         index & 1 ? null : address_of quick_stop,
                                                         address_of quick.value);

                        if (!took)
                        {
                                number_say(quick_stop == untouched &&
                                                   quick.bits == 0x5a5a5a5a5a5a5a5aULL,
                                           text("short decline wrote"));
                                continue;
                        }

                        taken++;
                        number_say(errno == 0, text("short answer set errno"));

                        general.value = string_to_decimal_general(input, address_of general_stop);
                        number_note(quick.bits == general.bits, input, quick.bits,
                                    general.bits);

                        if (!(index & 1))
                                number_note(quick_stop == general_stop, input,
                                            (positive)(quick_stop - input),
                                            (positive)(general_stop - input));
                }
        }

        number_note(taken > 10000, text("short took a share"), taken, 10000);

        return number_failures == 0;
}

/*
        The comparison tier, held to the register it replaced.

        numbers_compared answers binary64 and binary32 when the fast tiers
        decline, and numbers_assemble is the decimal register that answered
        before it and still answers long double. Both are static in this
        translation unit, so every decimal the generator makes is put through
        both, for both formats, and the bits and the errno condition have to
        agree -- on every input, not only the ones the fast tiers leave, which
        is what makes the tininess trial, the exactness question and the
        truncated register all reachable in a lane with no glibc under it. The
        comparison is also required never to decline for want of room: its
        fallback exists for safety, and a generator input that reaches it is
        a sizing error.
*/
number_case(compared_agrees)
{
        static const numbers_format address_to formats[2] = {
                address_of numbers_binary64, address_of numbers_binary32};
        positive declined = 0;
        positive compared = 0;
        positive index;

        ns_seed(0x434d50);

        for (index = 0; index < NS_BLOCK; index++)
        {
                numbers_scan scan;
                b32 which;

                ns_case();
                numbers_read((string_address)ns_text, address_of scan);

                if (scan.kind != NUMBERS_NUMBER || scan.hexadecimal)
                        continue;

                for (which = 0; which < 2; which++)
                {
                        numbers_scan copy;
                        p64 bits = 0;
                        p64 register_bits;
                        b32 fresh = -1;
                        b32 old = -1;

                        if (!numbers_compared(address_of scan, formats[which],
                                              address_of bits, address_of fresh))
                        {
                                declined++;
                                continue;
                        }

                        compared++;
                        memory_copy(address_of copy, address_of scan, sizeof copy);
                        register_bits = (p64)numbers_assemble(address_of copy, formats[which],
                                                              address_of old);

                        number_note(bits == register_bits, (string_address)ns_text, bits,
                                    register_bits);
                        number_note(fresh == old, (string_address)ns_text, (positive)fresh,
                                    (positive)old);
                }
        }

        //      declined is the sizing proof and does not depend on how deep the
        //      generator reaches; the widest comparison is printed so the
        //      measurement standard.c quotes can be read off a run.
        number_note(declined == 0, text("compared ran out of room"), declined, 0);
        number_note(compared > 50000, text("compared took a share"), compared, 50000);
        string_format(log, "  (comparison widest: %p limbs) ", (positive)number_widest_limbs);

        return number_failures == 0;
}

typedef struct
{
        const char address_to name;
        p8(address_to run)(void);
} number_entry;

static const number_entry number_entries[] = {
        {"short agrees", number_test_short_agrees},
        {"compared agrees", number_test_compared_agrees},
        {"integers", number_test_integers},
        {"listed ends", number_test_listed_ends},
        {"extended", number_test_extended},
        {"page-edge names", number_test_page_edge_names},
        {"shift table", number_test_shift_table},
        {"tiers agree", number_test_tiers_agree},
        {"monotone", number_test_monotone},
        {"small integers", number_test_small_integers},
        {null, null}};

b32 main(void)
{
        const number_entry address_to entry = number_entries;

        log_direct(str("number tests\n\n"));

        while (entry->name)
        {
                positive before = number_failures;

                log_direct((string_address)entry->name,
                           string_length((string_address)entry->name));
                entry->run();

                if (number_failures == before)
                        log_direct(str(" PASSED\n"));
                else
                        log_direct(str(" ----- FAILED\n"));

                entry++;
        }

        string_format(log, "\n%p checks, %p failures\n", number_checks,
                      number_failures);
        log_flush();

        return number_failures > 0 ? 1 : 0;
}
#endif /* CHECK_number */

#ifdef CHECK_number_differential
/*
        Experimental C standard library

        strtod and strtof over generated text, on this tree's conversions

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        The generator and the block walk are SHARED_number_stream, which says
        what the cases are. This side puts this tree's strtod and strtof under
        it and prints through log; CHECK_number_differential_reference puts
        glibc's under the same body, and the numbers lane of test/run diffs
        the two.

            number_differential FIRST COUNT          one hash line a block
            number_differential FIRST COUNT BLOCK    every case of BLOCK
*/
#include "../../src/lib.util.c"

#define NS_STRTOD strtod
#define NS_STRTOF strtof
#define NS_ERRNO errno

static fn ns_emit(const char address_to line)
{
        string_format(log, "%s\n", (string_address)line);
}

#define SHARED_number_stream
#include "../checks.c"
#undef SHARED_number_stream

b32 main(void)
{
        b32 words = program_argument_count();
        ns_u64 first = words > 1 ? ns_parse((const char address_to)program_argument(1)) : 0;
        ns_u64 count = words > 2 ? ns_parse((const char address_to)program_argument(2)) : 1;
        ns_u64 chosen = words > 3 ? ns_parse((const char address_to)program_argument(3)) : 0;

        ns_run(first, count, chosen, words > 3);
        log_flush();
        return 0;
}
#endif /* CHECK_number_differential */

#ifdef CHECK_number_differential_reference
/*
        Experimental C standard library

        the same generated text, over the machine's glibc

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        Built by the host compiler in the ordinary way against glibc, and the
        answer key for CHECK_number_differential on all three machines.
*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define NS_STRTOD strtod
#define NS_STRTOF strtof
#define NS_ERRNO errno

static void ns_emit(const char *line)
{
        fputs(line, stdout);
        fputc('\n', stdout);
}

#define SHARED_number_stream
#include "../checks.c"
#undef SHARED_number_stream

int main(int words, char **word)
{
        ns_run(words > 1 ? ns_parse(word[1]) : 0,
               words > 2 ? ns_parse(word[2]) : 1,
               words > 3 ? ns_parse(word[3]) : 0, words > 3);
        fflush(stdout);
        return 0;
}
#endif /* CHECK_number_differential_reference */

#ifdef CHECK_clock
#include "../../src/lib.util.c"

/*
        <time.h> against the answers glibc gives.

        The calendar has one property that makes it awkward to test the way
        the rest of this tree is tested: there is no second implementation to
        keep beside it, because the C it replaced never existed. What exists
        instead is glibc, on the machine this is built on, and the way to use
        it is to run the same sweep through both and compare -- 73,000 days
        for the dense one, another 71,000 timestamps for the coarse one,
        thousands of deliberately out-of-range struct tm for mktime, every
        specifier of strftime at every buffer size from zero to past the
        answer, and then every specifier again crossed with every flag, nine
        widths and both modifiers.

        That is far too much output to keep in a source file, so what is kept
        is a hash of it. Each sweep below folds every field of every answer
        into one sixty four bit number, and the number glibc produced for the
        identical sweep is the constant it is compared against. A single wrong
        day in eighty thousand changes it.

        The sweeps between the two marker lines are the text that was compiled
        against glibc to produce those constants -- extracted from this file
        mechanically, under a shim that spells positive, bipolar and
        address_to for a hosted compiler, so the two sides cannot be running
        different loops. To regenerate them:

            awk '/^.\* SWEEPS-BEGIN \*.$/{f=1;next}
                 /^.\* SWEEPS-END \*.$/{f=0} f' test/checks.c

        goes between the shim and a main that prints each hash, that is built
        with a plain hosted gcc, and it is run as TZ=UTC LC_ALL=C. The full
        differential harness, which prints every line rather than hashing it,
        does the same extraction and is what found the disagreements listed
        below; neither belongs in the tree, because both need a glibc and
        nothing else here does.

        Eight places where this deliberately does not match glibc 2.44, all
        eight checked by hand and all eight believed to be the better answer.
        The first four are the calendar and strftime:

          - tm_zone is "UTC" and not "GMT". glibc's gmtime says GMT; musl says
            UTC; this computes Coordinated Universal Time and says so.
          - the ISO week-based year of a date whose Thursday falls in the year
            2147483648 is that number here and -2147483648 in glibc, which
            holds it in an int. %G and %g both follow it over.
          - %Y, %C and %G are unpadded, which is what glibc 2.44 does and is
            not what musl does; see the note in the clock section of src/lib.util.c.
          - localtime follows POSIX TZ, then /root/timezone, then the footer
            of /etc/localtime, then UTC. glibc reads the whole of that file,
            history included, and so disagrees about dates before the zone's
            last rule change, which is one reason the reference side is
            pinned to TZ=UTC.

        The other four are strptime, and three of the four are glibc defects
        rather than choices it made. Each of them is checked below by hand,
        against this answer, and each is kept out of the two hashed sweeps:

          - %E in front of a specifier reads that specifier here. In glibc it
            reads the number twice: "%Ey" against "20000229" consumes "2000"
            into tm_year, then falls into the plain %y arm without winding the
            input back and consumes "02" on top of it, answering the year
            2002 and a cursor six bytes in. Four digits and no more, without
            an era to select, is what the C locale means.
          - %O in front of a specifier likewise reads that specifier. glibc
            keeps a list of the specifiers %O may precede and refuses the
            rest, so %OY is a parse failure there and a year here -- and it
            also gets only one %O per call: the second one asks the locale for
            alternative digits, is told for the second time that there are
            none, and returns null instead of falling back the way the first
            one did. "%Od %OH %Om" parses nothing at all in glibc.
          - %P is %p. glibc's strftime writes "%P" and its strptime will not
            read it back, which is a hole rather than a decision.
          - the weekday of a date in January or February of the year zero.
            glibc computes it with a formula whose integer division goes the
            wrong way for a year before the first, and answers Sunday for
            0000-01-01, which was a Saturday. The corner table below has that
            date in it from the day this file was written.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static fn same(string_address name, positive got, positive want)
{
        checks++;

        if (got == want)
                return;

        failures++;
        string_format(log, "  FAILED %s: got %p want %p\n", name, got, want);
}

static fn same_signed(string_address name, bipolar got, bipolar want)
{
        checks++;

        if (got == want)
                return;

        failures++;
        string_format(log, "  FAILED %s: got %b want %b\n", name, got, want);
}

static fn same_text(string_address name, string_address got, string_address want)
{
        checks++;

        if (!is_null(got) && string_compare(got, want) == 0)
                return;

        failures++;
        string_format(log, "  FAILED %s: got %s want %s\n", name,
                      is_null(got) ? (string_address) "(null)" : got, want);
}

static fn good(string_address name, p8 held)
{
        checks++;

        if (held)
                return;

        failures++;
        string_format(log, "  FAILED %s\n", name);
}

/* SWEEPS-BEGIN */
/*
        One round of FNV-1a's multiply with an extra fold, which is enough to
        make a single changed field change the answer and is short enough to
        be written the same way twice without a mistake. It is not a hash
        anybody should rely on for anything else.
*/
static positive clock_test_stir(positive state, bipolar value)
{
        state ^= (positive)value;
        state *= 1099511628211ULL;
        state ^= state >> 29;

        return state;
}

static positive clock_test_stir_text(positive state, const char address_to text)
{
        while (address_to text != end)
        {
                state = clock_test_stir(state, (bipolar)(p8)(address_to text));
                text++;
        }

        return state;
}

static positive clock_test_broken(positive state, bipolar stamp)
{
        struct tm broken;
        time_t moment = (time_t)stamp;

        if (is_null(gmtime_r(address_of moment, address_of broken)))
                return clock_test_stir(state, -999999);

        state = clock_test_stir(state, stamp);
        state = clock_test_stir(state, (bipolar)broken.tm_year + 1900);
        state = clock_test_stir(state, broken.tm_mon);
        state = clock_test_stir(state, broken.tm_mday);
        state = clock_test_stir(state, broken.tm_hour);
        state = clock_test_stir(state, broken.tm_min);
        state = clock_test_stir(state, broken.tm_sec);
        state = clock_test_stir(state, broken.tm_wday);
        state = clock_test_stir(state, broken.tm_yday);

        return state;
}

/*
        Every day from 1900-01-01 to 2100-01-01, with the time of day walking
        round the clock as the days go by so that the hour, minute and second
        arithmetic is swept too rather than being zero eighty thousand times.
*/
static positive clock_test_dense(void)
{
        positive state = 1469598103934665603ULL;
        bipolar day;

        for (day = -25567; day <= 47482; day++)
        {
                positive spin = (positive)day * 7919ULL;

                state = clock_test_broken(state, day * 86400 +
                                                         (bipolar)(spin % 86400ULL));
        }

        return state;
}

/*
        And the same again from about the year -1993 to the last second of
        9999, which is the sweep that actually reaches the negative branches
        of the two civil-date routines. The dense one above does not: 1900 is
        still six hundred thousand days after the start of the era the
        arithmetic counts from, so its sign guards never fire.
*/
static positive clock_test_coarse(void)
{
        positive state = 1469598103934665603ULL;
        bipolar stamp;

        for (stamp = -125000000000LL; stamp < 253402300800LL; stamp += 5270401LL)
                state = clock_test_broken(state, stamp);

        return state;
}

static const b32 clock_test_years[] = {-1900, -1000, -1, 0, 69, 70,
                                       100,   120,   200, 8099};
static const b32 clock_test_months[] = {-25, -13, -12, -1, 0,  1,
                                        2,   11,  12,  13, 25, 400};
static const b32 clock_test_days_of[] = {-400, -31, -1,  0,   1,   28, 29,
                                         30,   31,  32,  60,  366, 1000};
static const b32 clock_test_hours[] = {-25, -1, 0, 1, 12, 23, 24, 100};
static const b32 clock_test_minutes[] = {-61, -1, 0, 30, 59, 60, 61, 1000};
static const b32 clock_test_seconds[] = {-61, -1, 0, 30, 59, 60, 61, 3700};

static positive clock_test_one_normalise(positive state, b32 year, b32 month,
                                         b32 day, b32 hour, b32 minute,
                                         b32 second, p8 through_mktime)
{
        struct tm broken;
        time_t got;

        broken.tm_year = year;
        broken.tm_mon = month;
        broken.tm_mday = day;
        broken.tm_hour = hour;
        broken.tm_min = minute;
        broken.tm_sec = second;
        broken.tm_wday = 99;
        broken.tm_yday = 99;
        broken.tm_isdst = 0;

        got = through_mktime ? mktime(address_of broken) : timegm(address_of broken);

        state = clock_test_stir(state, (bipolar)got);
        state = clock_test_stir(state, broken.tm_year);
        state = clock_test_stir(state, broken.tm_mon);
        state = clock_test_stir(state, broken.tm_mday);
        state = clock_test_stir(state, broken.tm_hour);
        state = clock_test_stir(state, broken.tm_min);
        state = clock_test_stir(state, broken.tm_sec);
        state = clock_test_stir(state, broken.tm_wday);
        state = clock_test_stir(state, broken.tm_yday);

        return state;
}

/*
        mktime and timegm are required to take fields that are out of range
        and answer the right second anyway, and to write the in-range spelling
        of that second back through the caller's pointer. A round trip through
        gmtime cannot see whether the second half happens, because gmtime only
        ever hands back a structure that is already normal -- so every input
        here is deliberately wrong in at least one field.
*/
static positive clock_test_normalise(void)
{
        positive state = 1469598103934665603ULL;
        positive year, month, day, hour, minute, second;

        for (year = 0; year < sizeof(clock_test_years) / sizeof(b32); year++)
                for (month = 0; month < sizeof(clock_test_months) / sizeof(b32);
                     month++)
                        for (day = 0;
                             day < sizeof(clock_test_days_of) / sizeof(b32);
                             day++)
                        {
                                state = clock_test_one_normalise(
                                        state, clock_test_years[year],
                                        clock_test_months[month],
                                        clock_test_days_of[day], 23, 59, 61,
                                        false);
                                state = clock_test_one_normalise(
                                        state, clock_test_years[year],
                                        clock_test_months[month],
                                        clock_test_days_of[day], 5, 6, 7, true);
                        }

        for (year = 0; year < sizeof(clock_test_years) / sizeof(b32); year++)
                for (hour = 0; hour < sizeof(clock_test_hours) / sizeof(b32);
                     hour++)
                        for (minute = 0;
                             minute < sizeof(clock_test_minutes) / sizeof(b32);
                             minute++)
                                for (second = 0;
                                     second < sizeof(clock_test_seconds) /
                                                      sizeof(b32);
                                     second++)
                                        state = clock_test_one_normalise(
                                                state, clock_test_years[year],
                                                1, 15, clock_test_hours[hour],
                                                clock_test_minutes[minute],
                                                clock_test_seconds[second],
                                                false);

        return state;
}

#define CLOCK_TEST_EVERY_SPECIFIER \
        "%a|%A|%b|%B|%c|%C|%d|%D|%e|%F|%g|%G|%H|%I|%j|%k|%l|%m|%M|%n|%p|%P|" \
        "%r|%R|%s|%S|%t|%T|%u|%U|%V|%w|%W|%x|%X|%y|%Y|%z|%%|%Q|%Ey|%OS|%h|"

/*
        Every specifier at once, over the whole range and then densely over
        the years either side of the epoch. %Z is not in the format: it is the
        one specifier this deliberately answers differently from glibc, and
        including it would drown the constant it is compared against.
*/
static positive clock_test_format(void)
{
        positive state = 1469598103934665603ULL;
        bipolar stamp;

        for (stamp = -125000000000LL; stamp < 253402300800LL;
             stamp += 194713891LL)
        {
                struct tm broken;
                time_t moment = (time_t)stamp;
                char written[512];

                if (is_null(gmtime_r(address_of moment, address_of broken)))
                {
                        state = clock_test_stir(state, -999999);
                        continue;
                }

                state = clock_test_stir(
                        state,
                        (bipolar)strftime((void *)written, sizeof(written),
                                          CLOCK_TEST_EVERY_SPECIFIER,
                                          address_of broken));
                state = clock_test_stir_text(state, written);
        }

        for (stamp = -100000000LL; stamp < 100000000LL; stamp += 91711LL)
        {
                struct tm broken;
                time_t moment = (time_t)stamp;
                char written[512];

                if (is_null(gmtime_r(address_of moment, address_of broken)))
                {
                        state = clock_test_stir(state, -999999);
                        continue;
                }

                state = clock_test_stir(
                        state,
                        (bipolar)strftime((void *)written, sizeof(written),
                                          CLOCK_TEST_EVERY_SPECIFIER,
                                          address_of broken));
                state = clock_test_stir_text(state, written);
        }

        return state;
}

/*
        The half of strftime that is not the specifiers.

        A result that will not fit, terminator included, must answer zero and
        must not have written a byte past max -- and max of zero must not write
        even the terminator, which is the case a generous buffer never reaches.
        Every byte past max is checked here for having been left alone, and
        both the answer and the contents are folded in at every size from
        nothing to past the longest result.

        One of the formats is decorated and one of the moments is before the
        common era, because the widest path through the writer is the one a
        negative number under a zero-padded width takes: the sign, then the
        run of zeros, then the digits, three separately bounded writes for one
        field. An undecorated format never reaches the second of them, so a
        sweep without one would leave the strictest promise this family makes
        resting on a proof rather than on a test.

        The bound is past the longest answer and not a round number: the
        every-specifier format is two hundred and twenty one bytes at its
        longest, so a sweep that stopped at two hundred and twenty would never
        once see it succeed.
*/
static positive clock_test_bounds(void)
{
        static const char address_to formats[] = {
                CLOCK_TEST_EVERY_SPECIFIER,
                "%Y-%m-%d %H:%M:%S",
                "",
                "%",
                "%%",
                "x",
                "%c",
                "%n%t%n",
                "no specifiers at all",
                "%010Y|%_10Y|%-10Y|%040F|%5z|%-6s|%^a|%#p|%3e|%03k|%_3l",
        };
        static const bipolar moments[] = {0LL, 1234567890LL, -1234567890LL,
                                          951782400LL, -62167219201LL};
        positive state = 1469598103934665603ULL;
        positive which, when, max, byte;

        for (which = 0; which < sizeof(formats) / sizeof(formats[0]); which++)
                for (when = 0; when < sizeof(moments) / sizeof(moments[0]);
                     when++)
                {
                        struct tm broken;
                        time_t moment = (time_t)moments[when];

                        if (is_null(gmtime_r(address_of moment,
                                             address_of broken)))
                                continue;

                        for (max = 0; max < 240; max++)
                        {
                                char written[256];
                                positive answered;

                                for (byte = 0; byte < sizeof(written); byte++)
                                        written[byte] = '@';

                                answered = (positive)strftime(
                                        (void *)written, max, formats[which],
                                        address_of broken);

                                state = clock_test_stir(state, (bipolar)max);
                                state = clock_test_stir(state,
                                                        (bipolar)answered);

                                if (answered)
                                        state = clock_test_stir_text(state,
                                                                     written);

                                for (byte = max; byte < sizeof(written); byte++)
                                        if (written[byte] != '@')
                                        {
                                                state = clock_test_stir(
                                                        state, -424242);
                                                break;
                                        }
                        }
                }

        return state;
}

/*
        Every specifier crossed with every flag, a range of widths and both
        modifiers.

        Ninety three specifier bytes -- every printable one, so that a
        directive nobody has ever heard of is swept as hard as one that works
        -- times fourteen combinations of the five flags, times nine widths,
        times three modifiers, at eight instants chosen for their corners.
        Something over a quarter of a million directives, which is the sweep
        that says a flag is not being quietly ignored, that a width pads the
        side it should with the byte it should, and that a modifier is refused
        in exactly the places glibc refuses it and nowhere else.

        %Z is left out for the reason it is left out of the sweep above: it is
        the one specifier this deliberately answers differently, and it would
        drown the constant the rest of them produce.
*/
static const char address_to clock_test_flags[] = {
        "", "-", "_", "0", "^", "#", "-^",
        "0#", "_^", "^#", "0_", "-0", "^0#", "#_"};

static const char address_to clock_test_widths[] = {
        "", "1", "2", "3", "5", "6", "10", "12", "31"};

static const char address_to clock_test_modifiers[] = {"", "E", "O"};

static positive clock_test_directive(positive state,
                                     const struct tm address_to broken,
                                     const char address_to format)
{
        char written[256];
        positive answered = (positive)strftime((void address_to)written,
                                               sizeof(written), format,
                                               broken);

        state = clock_test_stir(state, (bipolar)answered);

        /*
                A refusal leaves the buffer unspecified and the two sides are
                not obliged to have left the same bytes in it, so a refusal
                folds in its own marker and not the wreckage.
        */
        if (answered)
                return clock_test_stir_text(state, written);

        return clock_test_stir(state, -313131);
}

static positive clock_test_decorated(void)
{
        static const bipolar moments[] = {
                951825045LL, 0LL, -1LL, -62167219201LL, 1136073600LL,
                253402300799LL, -2203977600LL, 1451606400LL};
        positive state = 1469598103934665603ULL;
        positive when, spec, flag, width, modifier;

        for (when = 0; when < sizeof(moments) / sizeof(moments[0]); when++)
        {
                struct tm broken;
                time_t moment = (time_t)moments[when];

                if (is_null(gmtime_r(address_of moment, address_of broken)))
                {
                        state = clock_test_stir(state, -999999);
                        continue;
                }

                for (spec = 0x21; spec < 0x7f; spec++)
                {
                        if (spec == 'Z')
                                continue;

                        for (flag = 0;
                             flag < sizeof(clock_test_flags) /
                                            sizeof(clock_test_flags[0]);
                             flag++)
                                for (width = 0;
                                     width < sizeof(clock_test_widths) /
                                                     sizeof(clock_test_widths[0]);
                                     width++)
                                        for (modifier = 0;
                                             modifier <
                                             sizeof(clock_test_modifiers) /
                                                     sizeof(clock_test_modifiers[0]);
                                             modifier++)
                                        {
                                                char format[16];
                                                positive at = 0;
                                                positive part;

                                                format[at++] = '%';

                                                for (part = 0;
                                                     clock_test_flags[flag][part];
                                                     part++)
                                                        format[at++] =
                                                                clock_test_flags[flag][part];

                                                for (part = 0;
                                                     clock_test_widths[width][part];
                                                     part++)
                                                        format[at++] =
                                                                clock_test_widths[width][part];

                                                for (part = 0;
                                                     clock_test_modifiers[modifier][part];
                                                     part++)
                                                        format[at++] =
                                                                clock_test_modifiers[modifier][part];

                                                format[at++] = (char)spec;
                                                format[at] = 0;

                                                state = clock_test_directive(
                                                        state,
                                                        address_of broken,
                                                        format);
                                        }
                }
        }

        return state;
}

/*
        Every day from 1965 to 2035, through the four counters that disagree
        with each other around the new year on purpose.

        %U counts weeks from the first Sunday, %W from the first Monday, %V
        from the first week with four days of the new year in it, and %G is
        the year that %V belongs to rather than the year the date is in -- so
        the second of January can be week 53 of the year before it and the
        thirty first of December can be week 1 of the year after it, and the
        three of them can be 00, 52 and 53 on the same day. That week either
        side of New Year is where a hand-written strftime is wrong, and this
        walks seventy of them, bare and then decorated.
*/
static positive clock_test_weeks(void)
{
        positive state = 1469598103934665603ULL;
        bipolar day;

        for (day = -1826; day <= 23741; day++)
        {
                struct tm broken;
                time_t moment = (time_t)(day * 86400 + 43200);

                if (is_null(gmtime_r(address_of moment, address_of broken)))
                {
                        state = clock_test_stir(state, -999999);
                        continue;
                }

                state = clock_test_directive(
                        state, address_of broken,
                        "%G|%g|%V|%U|%W|%u|%w|%j|%C|%y|%Y|%a|%A|%F");
                state = clock_test_directive(
                        state, address_of broken,
                        "%-G|%_V|%03U|%^a|%#A|%5G|%05g|%-j|%_5V|%2C");
        }

        return state;
}

/*
        strptime, against the answers glibc gives, twice over.

        First a cross of every format worth writing against every input worth
        refusing -- fifty six formats by a hundred inputs, most of which do
        not match and are there for exactly that reason, because a parser is
        as much its refusals as its acceptances. A refusal folds in the
        refusal and nothing else: the standard leaves the structure
        unspecified after one, and the two sides are not obliged to have
        stopped writing in the same place.

        Then the round trip: every moment written out with strftime and read
        straight back in through the same format. strftime already agrees with
        glibc byte for byte, so the text going in is the same text on both
        sides and anything that comes out different is strptime's fault.

        What is deliberately not in either sweep is listed with the rest of
        the disagreements at the top of this file: %E and %O in front of a
        specifier glibc will not take them on, %P, and the year zero.
*/
static const char address_to clock_test_scan_formats[] = {
        "%Y-%m-%d", "%Y-%m-%d %H:%M:%S", "%d/%m/%Y", "%m/%d/%y", "%D", "%F",
        "%T", "%R", "%r", "%c", "%x", "%X", "%a %b %e %H:%M:%S %Y",
        "%A, %B %d, %Y", "%j %Y", "%Y %j", "%Y%m%d", "%Y%m%d%H%M%S",
        "%C%y-%m-%d", "%C %y", "%y", "%Y", "%I:%M %p", "%I:%M:%S %p",
        "%H:%M", "%s", "%Y-%m-%dT%H:%M:%S%z", "%u", "%w", "%U %W %V %G %g",
        "%b %d", "%B %d", "%h %d", "%e %b %Y", "%k:%M", "%l:%M %p",
        "%n%Y%t%m%t%d", "%%", "%Y-%m-%d%n", "%Z %Y", "%z",
        "%Y-%m-%d %H:%M:%S %z", "  %Y  %m  %d  ", "%Y.%m.%d", "%p %I",
        "%a", "%A", "%m", "%d", "%H", "%M", "%S", "%j", "%C", "%G%j"};

static const char address_to clock_test_scan_inputs[] = {
        "2000-02-29", "2000-02-29 11:50:45", "29/02/2000", "02/29/00",
        "2000-02-29", "11:50:45", "11:50", "11:50:45 AM",
        "Tue Feb 29 11:50:45 2000", "Tuesday, February 29, 2000",
        "060 2000", "2000 060", "20000229", "20000229115045",
        "19 70", "0001-01-01", "00", "2000", "11:50 PM", "11:50:45 pm",
        "951825045", "2000-02-29T11:50:45+0100", "7", "0",
        "09 09 09 2000 00", "Feb 29", "February 29", "feb 29",
        "29 Feb 2000", " 5:06", " 5:06 am", "\n2000\t02\t29", "%",
        "2000-02-29\n", "UTC 2000", "-0530", "2000-02",
        "2000-02-29 11:50:45 -08:00", "  2000  02  29  ", "2000.02.29",
        "PM 11", "Sun", "Sunday", "12", "31", "23", "59", "60", "366", "20",
        "", "x", "1", "312", "0102", "13", "32", "  7", "7x",
        "SUNDAY", "sunday", "SuNdAy", "Sun day", "Junk", "+05", "+0530",
        "+05:30", "Z", "-2400", "+2401", "+0560", "2000-2-9",
        "2000-02-29 25:00:00", "1969-12-31", "9999-12-31",
        "  Feb  29", "\t2000", "2000 ", " 2000", "68", "69", "70",
        "951825045junk", "1e5", "2000-02-30", "-1", "  ", "AM", "am", "pm",
        "P", "A", "1:2:3", "001:02:03", "23:59:60", "23:59:61", "23:59:62"};

static positive clock_test_scan_one(positive state, const char address_to input,
                                    const char address_to format)
{
        struct tm broken;
        char address_to answered;

        /*
                Nothing is cleared, on purpose: strptime does not clear the
                structure and a caller who did not clear it first sees what
                was there. A recognisable value in every field is what makes
                "did not touch this one" visible in the hash.
        */
        broken.tm_sec = -77;
        broken.tm_min = -77;
        broken.tm_hour = -77;
        broken.tm_mday = -77;
        broken.tm_mon = -77;
        broken.tm_year = -77;
        broken.tm_wday = -77;
        broken.tm_yday = -77;
        broken.tm_isdst = -77;
        broken.tm_gmtoff = -77;
        broken.tm_zone = 0;

        answered = (char address_to)strptime(input, format, address_of broken);

        state = clock_test_stir_text(state, format);
        state = clock_test_stir_text(state, input);

        if (is_null(answered))
                return clock_test_stir(state, -212121);

        state = clock_test_stir(state, (bipolar)(answered - input));
        state = clock_test_stir(state, broken.tm_year);
        state = clock_test_stir(state, broken.tm_mon);
        state = clock_test_stir(state, broken.tm_mday);
        state = clock_test_stir(state, broken.tm_hour);
        state = clock_test_stir(state, broken.tm_min);
        state = clock_test_stir(state, broken.tm_sec);
        state = clock_test_stir(state, broken.tm_wday);
        state = clock_test_stir(state, broken.tm_yday);
        state = clock_test_stir(state, (bipolar)broken.tm_gmtoff);

        return state;
}

static positive clock_test_scan(void)
{
        positive state = 1469598103934665603ULL;
        positive which, when;

        for (which = 0; which < sizeof(clock_test_scan_formats) /
                                        sizeof(clock_test_scan_formats[0]);
             which++)
                for (when = 0; when < sizeof(clock_test_scan_inputs) /
                                              sizeof(clock_test_scan_inputs[0]);
                     when++)
                        state = clock_test_scan_one(
                                state, clock_test_scan_inputs[when],
                                clock_test_scan_formats[which]);

        return state;
}

static const char address_to clock_test_round_formats[] = {
        "%Y-%m-%d %H:%M:%S", "%a %b %e %H:%M:%S %Y", "%D %T", "%F %T",
        "%m/%d/%y %I:%M:%S %p", "%Y %j %H %M %S", "%C%y%m%d", "%s",
        "%A %B %d %Y", "%e %b %Y %k:%M", "%Y-%m-%dT%H:%M:%S%z",
        "%j", "%Y", "%y", "%u %w", "%H %I %p", "%G %V %U %W"};

static positive clock_test_scan_round(void)
{
        positive state = 1469598103934665603ULL;
        bipolar stamp;
        positive which;

        for (stamp = 0; stamp < 4102444800LL; stamp += 5270401LL)
                for (which = 0;
                     which < sizeof(clock_test_round_formats) /
                                     sizeof(clock_test_round_formats[0]);
                     which++)
                {
                        struct tm broken;
                        time_t moment = (time_t)stamp;
                        char written[256];

                        if (is_null(gmtime_r(address_of moment,
                                             address_of broken)))
                                continue;

                        if (strftime((void address_to)written, sizeof(written),
                                     clock_test_round_formats[which],
                                     address_of broken) == 0)
                                continue;

                        state = clock_test_scan_one(
                                state, written,
                                clock_test_round_formats[which]);
                }

        return state;
}

/*
        asctime's twenty six bytes, over a range wide enough to reach the
        years it cannot spell inside them and has to refuse.
*/
static positive clock_test_asctime(void)
{
        positive state = 1469598103934665603ULL;
        bipolar stamp;

        for (stamp = -62167219200LL; stamp < 253402300800LL;
             stamp += 1943713891LL)
        {
                struct tm broken;
                time_t moment = (time_t)stamp;
                char written[64];

                if (is_null(gmtime_r(address_of moment, address_of broken)))
                {
                        state = clock_test_stir(state, -999999);
                        continue;
                }

                if (is_null(asctime_r(address_of broken, (void *)written)))
                {
                        state = clock_test_stir(state, -111111);
                        continue;
                }

                state = clock_test_stir_text(state, written);
        }

        return state;
}
/* SWEEPS-END */

/*
        The constants the sweeps above produced under glibc 2.44 on x86_64,
        with TZ=UTC and LC_ALL=C, from the same source text.
*/
#define CLOCK_HASH_DENSE 17294647340756726478ULL
#define CLOCK_HASH_COARSE 511831104147379237ULL
#define CLOCK_HASH_NORMALISE 9235080613808746534ULL
#define CLOCK_HASH_FORMAT 5799989417746424739ULL
#define CLOCK_HASH_BOUNDS 18209079861022630276ULL
#define CLOCK_HASH_DECORATED 2088967578927533204ULL
#define CLOCK_HASH_WEEKS 5920955691035123170ULL
#define CLOCK_HASH_SCAN 5430387038418749550ULL
#define CLOCK_HASH_SCAN_ROUND 15503327748273598656ULL
#define CLOCK_HASH_ASCTIME 15797585760646486659ULL

/*
        A hash says something changed and says nothing about what. These say
        what: one date from each of the corners the arithmetic has, spelled
        out, so that a failure is readable before the sweeps are reached.
*/
typedef struct clock_test_date
{
        bipolar stamp;
        bipolar year;
        b32 month;
        b32 day;
        b32 hour;
        b32 minute;
        b32 second;
        b32 weekday;
        b32 day_of_year;
} clock_test_date;

static const clock_test_date clock_test_corners[] = {
        {0LL, 1970, 0, 1, 0, 0, 0, 4, 0},
        {-1LL, 1969, 11, 31, 23, 59, 59, 3, 364},
        {86399LL, 1970, 0, 1, 23, 59, 59, 4, 0},
        {86400LL, 1970, 0, 2, 0, 0, 0, 5, 1},
        {-86400LL, 1969, 11, 31, 0, 0, 0, 3, 364},
        {951782400LL, 2000, 1, 29, 0, 0, 0, 2, 59},
        {-2203977600LL, 1900, 1, 28, 0, 0, 0, 3, 58},
        {-2203977601LL, 1900, 1, 27, 23, 59, 59, 2, 57},
        {-2203891200LL, 1900, 2, 1, 0, 0, 0, 4, 59},
        {-2203891201LL, 1900, 1, 28, 23, 59, 59, 3, 58},
        {4107542400LL, 2100, 2, 1, 0, 0, 0, 1, 59},
        {4107542399LL, 2100, 1, 28, 23, 59, 59, 0, 58},
        {-11670998400LL, 1600, 1, 29, 0, 0, 0, 2, 59},
        {-62167219200LL, 0, 0, 1, 0, 0, 0, 6, 0},
        {-62162035200LL, 0, 2, 1, 0, 0, 0, 3, 60},
        {-62162035201LL, 0, 1, 29, 23, 59, 59, 2, 59},
        {-62167219201LL, -1, 11, 31, 23, 59, 59, 5, 364},
        {-62198755200LL, -1, 0, 1, 0, 0, 0, 5, 0},
        {-62135596800LL, 1, 0, 1, 0, 0, 0, 1, 0},
        {253402300799LL, 9999, 11, 31, 23, 59, 59, 5, 364},
        {2147483647LL, 2038, 0, 19, 3, 14, 7, 2, 18},
};
static fn check_corners(void)
{
        positive at;

        for (at = 0; at < sizeof(clock_test_corners) / sizeof(clock_test_date);
             at++)
        {
                const clock_test_date address_to want = clock_test_corners + at;
                time_t moment = (time_t)want->stamp;
                struct tm broken;
                time_t back;

                if (is_null(gmtime_r(address_of moment, address_of broken)))
                {
                        checks++;
                        failures++;
                        string_format(log, "  FAILED gmtime refused %b\n",
                                      want->stamp);
                        continue;
                }

                same_signed((string_address) "corner year",
                            (bipolar)broken.tm_year + 1900, want->year);
                same_signed((string_address) "corner month", broken.tm_mon,
                            want->month);
                same_signed((string_address) "corner day", broken.tm_mday,
                            want->day);
                same_signed((string_address) "corner hour", broken.tm_hour,
                            want->hour);
                same_signed((string_address) "corner minute", broken.tm_min,
                            want->minute);
                same_signed((string_address) "corner second", broken.tm_sec,
                            want->second);
                same_signed((string_address) "corner weekday", broken.tm_wday,
                            want->weekday);
                same_signed((string_address) "corner day of year",
                            broken.tm_yday, want->day_of_year);

                back = timegm(address_of broken);
                same_signed((string_address) "corner round trip",
                            (bipolar)back, want->stamp);
        }
}

static fn check_format_strings(void)
{
        struct tm broken;
        time_t moment;
        p8 written[128];

        moment = 951825045;                     /* 2000-02-29 11:50:45 UTC */
        gmtime_r(address_of moment, address_of broken);

        strftime(written, sizeof(written), "%Y-%m-%d %H:%M:%S", address_of broken);
        same_text((string_address) "%F-alike", written,
                  (string_address) "2000-02-29 11:50:45");

        strftime(written, sizeof(written), "%a %A %b %B", address_of broken);
        same_text((string_address) "names", written,
                  (string_address) "Tue Tuesday Feb February");

        strftime(written, sizeof(written), "%c", address_of broken);
        same_text((string_address) "%c", written,
                  (string_address) "Tue Feb 29 11:50:45 2000");

        strftime(written, sizeof(written), "%x %X %D %T %R %r", address_of broken);
        same_text((string_address) "compound", written,
                  (string_address) "02/29/00 11:50:45 02/29/00 11:50:45 "
                                   "11:50 11:50:45 AM");

        strftime(written, sizeof(written), "%j %U %W %V %G %u %w %C %y %e",
                 address_of broken);
        same_text((string_address) "counters", written,
                  (string_address) "060 09 09 09 2000 2 2 20 00 29");

        strftime(written, sizeof(written), "%s %z %Z %p %P %%%Q", address_of broken);
        same_text((string_address) "the rest", written,
                  (string_address) "951825045 +0000 UTC AM am %%Q");

        written[0] = '@';
        same((string_address) "max of zero writes nothing",
             strftime(written, 0, "abc", address_of broken), 0);
        same((string_address) "not even a terminator", (positive)written[0],
             (positive)'@');

        /*
                Three bytes of room for three bytes of answer is not enough,
                because the terminator is the fourth. The answer is zero and
                what is left in the buffer is unspecified -- both this and
                glibc leave the three bytes there -- but the byte at max must
                not have been touched, and that is what is checked.
        */
        written[3] = '@';
        same((string_address) "a result that just fits is refused",
             strftime(written, 3, "abc", address_of broken), 0);
        same((string_address) "and wrote nothing at max", (positive)written[3],
             (positive)'@');

        same((string_address) "a result that fits with its terminator",
             strftime(written, 4, "abc", address_of broken), 3);
        same_text((string_address) "and is the result", written,
                  (string_address) "abc");

        moment = -62167219201LL;                /* -0001-12-31 23:59:59 */
        gmtime_r(address_of moment, address_of broken);
        strftime(written, sizeof(written), "%Y|%F|%C|%G|%g|%y", address_of broken);
        same_text((string_address) "a year before the common era", written,
                  (string_address) "-1|-1-12-31|-1|-1|99|99");
}

/*
        The flags, the widths and the two modifiers, spelled out.

        Every expected string below was read off glibc 2.44 under TZ=UTC and
        LC_ALL=C before it was written down, and every line is one of the
        places where the answer is not what a first reading of the manual
        would have guessed. A hash says something moved; these say what.
*/
static fn check_format_flags(void)
{
        struct tm broken;
        time_t moment = 951800767;              /* 2000-02-29 05:06:07 UTC */
        p8 written[256];

        gmtime_r(address_of moment, address_of broken);

        strftime(written, sizeof(written), "%H|%k|%I|%l|%e|%_d|%-d|%0e|%-e",
                 address_of broken);
        same_text((string_address) "the space padded fields", written,
                  (string_address) "05| 5|05| 5|29|29|29|29|29");

        strftime(written, sizeof(written), "%5d|%05d|%-5d|%_5d|%3j|%-j|%_j",
                 address_of broken);
        same_text((string_address) "a width and the three padding flags",
                  written, (string_address) "00029|00029|   29|   29|060|60| 60");

        /*
                ^ folds up and # folds the other way from usual, and "usual"
                is decided by the specifier: a name is capitalised so # makes
                it shout, and %p is already shouting so # makes it quiet. %P
                is lower case whatever the flags say.
        */
        strftime(written, sizeof(written), "%^a|%#a|%^A|%#B|%^p|%#p|%^P|%#P",
                 address_of broken);
        same_text((string_address) "the two case flags", written,
                  (string_address) "TUE|TUE|TUESDAY|FEBRUARY|AM|am|am|am");

        strftime(written, sizeof(written), "%^c", address_of broken);
        same_text((string_address) "a compound folds once, as a whole",
                  written, (string_address) "TUE FEB 29 05:06:07 2000");

        strftime(written, sizeof(written), "%012F|%-12F|%_12F",
                 address_of broken);
        same_text((string_address) "a compound pads once, as a whole", written,
                  (string_address) "002000-02-29|  2000-02-29|  2000-02-29");

        /*
                The offset is one four digit number and not two of two, which
                only shows once a width is named: the five in "%5z" widens the
                sign and then widens all four digits again.
        */
        strftime(written, sizeof(written), "%z|%5z|%-5z|%_z|%0z",
                 address_of broken);
        same_text((string_address) "the offset and its width", written,
                  (string_address) "+0000|    +00000|    +    0|+   0|+0000");

        strftime(written, sizeof(written),
                 "%Ec|%EC|%Ex|%EX|%Ey|%EY|%Ep|%Eu|%Ez", address_of broken);
        same_text((string_address) "the era modifier where it is taken",
                  written,
                  (string_address) "Tue Feb 29 05:06:07 2000|20|02/29/00|"
                                   "05:06:07|00|2000|AM|2|+0000");

        strftime(written, sizeof(written),
                 "%Od|%Oe|%OH|%OS|%Ou|%OV|%Om|%Oy|%Ob", address_of broken);
        same_text((string_address) "the digits modifier where it is taken",
                  written, (string_address) "29|29|05|07|2|09|02|00|Feb");

        /*
                And where it is not taken, which is a table with no rule in
                it: %EC is a directive and %Eb is not, %OS is one and %ES is
                not. A refused modifier is not an error, it is a directive
                nobody knows, and it comes back out as the text it was.
        */
        strftime(written, sizeof(written),
                 "%Ea|%Ed|%ES|%OY|%Ox|%Oc|%Eb|%EV", address_of broken);
        same_text((string_address) "a modifier the specifier will not take",
                  written,
                  (string_address) "%Ea|%Ed|%ES|%OY|%Ox|%Oc|%Eb|%EV");

        // The width goes before the modifier and not after it, and getting
        // that backwards is not a warning, it is a different answer.
        strftime(written, sizeof(written), "%E5Y|%5EY|%O5d|%5OS",
                 address_of broken);
        same_text((string_address) "the width goes before the modifier",
                  written, (string_address) "%E5Y|02000|%O5d|00007");

        // An unknown directive comes back out through the same path a name
        // does, so it is folded and padded on the way.
        strftime(written, sizeof(written), "%^q|%5Q|%05q|%-q|%^Q",
                 address_of broken);
        same_text((string_address) "an unknown directive is decorated too",
                  written, (string_address) "%^Q|  %5Q|0%05q|%-q|%^Q");

        // %s is the one number that does not pad its own digits, so a width
        // in front of it spaces rather than zeroes and its zeros, when asked
        // for, land in front of the sign rather than behind it.
        strftime(written, sizeof(written), "%12s|%020s|%-12s|%_12s",
                 address_of broken);
        same_text((string_address) "the seconds since the epoch", written,
                  (string_address) "   951800767|00000000000951800767|"
                                   "   951800767|   951800767");

        strftime(written, sizeof(written), "%%|%5%|%05%|%n|%3n|%t|%3t",
                 address_of broken);
        same_text((string_address) "the three that are not fields at all",
                  written, (string_address) "%|    %|0000%|\n|  \n|\t|  \t");

        moment = -62167219201LL;                /* -0001-12-31 23:59:59 */
        gmtime_r(address_of moment, address_of broken);

        strftime(written, sizeof(written),
                 "%Y|%5Y|%05Y|%_5Y|%-5Y|%3G|%2C|%C|%G|%g|%y", address_of broken);
        same_text((string_address) "a negative year takes its sign first",
                  written, (string_address) "-1|-0001|-0001|   -1|   -1|-01|"
                                            "-1|-1|-1|99|99");

        strftime(written, sizeof(written), "%k|%l|%e|%3e|%_3d",
                 address_of broken);
        same_text((string_address) "the last hour of the year before the era",
                  written, (string_address) "23|11|31| 31| 31");

        moment = 0;
        gmtime_r(address_of moment, address_of broken);

        strftime(written, sizeof(written), "%k|%l|%I|%p|%P|%e|%s|%5s|%-5s|%05s",
                 address_of broken);
        same_text((string_address) "midnight at the epoch", written,
                  (string_address) " 0|12|12|AM|am| 1|0|    0|    0|00000");
}

/*
        mktime and timegm are required to take fields that are outside their
        ranges and answer the right second anyway, and then to write the
        in-range spelling of that second back through the caller's pointer.
        The thirteenth month is January of the next year, month minus one is
        December of the year before, day zero is the last day of the month
        before, and an hour of twenty five is one in the morning of the day
        after. The hashed sweep walks thousands of these against glibc; these
        five are the ones anybody would name.
*/
static fn check_normalise(void)
{
        static const clock_test_date wanted[] = {
                {979516800LL, 2001, 0, 15, 0, 0, 0, 1, 14},
                {945216000LL, 1999, 11, 15, 0, 0, 0, 3, 348},
                {951782400LL, 2000, 1, 29, 0, 0, 0, 2, 59},
                {946774800LL, 2000, 0, 2, 1, 0, 0, 0, 1},
                {946684799LL, 1999, 11, 31, 23, 59, 59, 5, 364},
        };
        static const b32 given[5][6] = {
                {100, 12, 15, 0, 0, 0},
                {100, -1, 15, 0, 0, 0},
                {100, 2, 0, 0, 0, 0},
                {100, 0, 1, 25, 0, 0},
                {100, 0, 1, 0, 0, -1},
        };
        positive at;

        for (at = 0; at < sizeof(wanted) / sizeof(clock_test_date); at++)
        {
                struct tm broken;
                time_t answered;

                broken.tm_year = given[at][0];
                broken.tm_mon = given[at][1];
                broken.tm_mday = given[at][2];
                broken.tm_hour = given[at][3];
                broken.tm_min = given[at][4];
                broken.tm_sec = given[at][5];
                broken.tm_wday = 99;
                broken.tm_yday = 99;
                broken.tm_isdst = 0;

                answered = timegm(address_of broken);

                same_signed((string_address) "normalised second",
                            (bipolar)answered, wanted[at].stamp);
                same_signed((string_address) "normalised year",
                            (bipolar)broken.tm_year + 1900, wanted[at].year);
                same_signed((string_address) "normalised month", broken.tm_mon,
                            wanted[at].month);
                same_signed((string_address) "normalised day", broken.tm_mday,
                            wanted[at].day);
                same_signed((string_address) "normalised hour", broken.tm_hour,
                            wanted[at].hour);
                same_signed((string_address) "normalised minute", broken.tm_min,
                            wanted[at].minute);
                same_signed((string_address) "normalised second of the minute",
                            broken.tm_sec, wanted[at].second);
                same_signed((string_address) "normalised weekday",
                            broken.tm_wday, wanted[at].weekday);
                same_signed((string_address) "normalised day of the year",
                            broken.tm_yday, wanted[at].day_of_year);
        }
}

/*
        strptime, spelled out, with a pointer answer to check as well as the
        fields: it returns where it stopped, and where it stopped is half of
        what a caller uses it for.
*/
static fn check_scan(void)
{
        struct tm broken;
        p8 address_to rest;

        broken.tm_isdst = 0;
        broken.tm_gmtoff = 0;
        broken.tm_zone = null;

        rest = strptime("2000-02-29 11:50:45", "%Y-%m-%d %H:%M:%S",
                        address_of broken);
        good((string_address) "strptime read a whole timestamp",
             !is_null(rest));
        same_signed((string_address) "scanned year",
                    (bipolar)broken.tm_year + 1900, 2000);
        same_signed((string_address) "scanned month", broken.tm_mon, 1);
        same_signed((string_address) "scanned day", broken.tm_mday, 29);
        same_signed((string_address) "scanned hour", broken.tm_hour, 11);
        same_signed((string_address) "scanned minute", broken.tm_min, 50);
        same_signed((string_address) "scanned second", broken.tm_sec, 45);
        same_signed((string_address) "derived weekday", broken.tm_wday, 2);
        same_signed((string_address) "derived day of the year", broken.tm_yday,
                    59);
        same_text((string_address) "and stopped at the end", rest,
                  (string_address) "");

        // It answers where it stopped and does not care that there is more.
        rest = strptime("2000-02-29 and more", "%Y-%m-%d", address_of broken);
        same_text((string_address) "strptime stops where the format does",
                  rest, (string_address) " and more");

        good((string_address) "strptime refuses what does not match",
             is_null(strptime("not a date", "%Y-%m-%d", address_of broken)));

        // Longest name first, and case does not matter.
        broken.tm_wday = -1;
        rest = strptime("TUESDAY afternoon", "%A", address_of broken);
        same_signed((string_address) "a name in the wrong case",
                    broken.tm_wday, 2);
        same_text((string_address) "and the longest one that fits", rest,
                  (string_address) " afternoon");

        broken.tm_wday = -1;
        rest = strptime("Tues", "%A", address_of broken);
        same_signed((string_address) "the short name where the long one fails",
                    broken.tm_wday, 2);
        same_text((string_address) "leaves what it did not use", rest,
                  (string_address) "s");

        // A number skips whatever whitespace is in front of it, whether or
        // not the format asked for any.
        rest = strptime(" 5:06", "%H:%M", address_of broken);
        same_signed((string_address) "a number skips leading whitespace",
                    broken.tm_hour, 5);
        same_signed((string_address) "and reads what follows it",
                    broken.tm_min, 6);

        /*
                And it stops before it overflows the field rather than after,
                which is what lets two fields sit against each other with no
                separator: "0102" is the first of February and not month one
                hundred and two.
        */
        broken.tm_mon = -1;
        broken.tm_mday = -1;
        strptime("0102", "%m%d", address_of broken);
        same_signed((string_address) "two fields with nothing between them",
                    broken.tm_mon, 0);
        same_signed((string_address) "stop at the top of the range",
                    broken.tm_mday, 2);

        strptime("11:50 pm", "%I:%M %p", address_of broken);
        same_signed((string_address) "an afternoon on a twelve hour clock",
                    broken.tm_hour, 23);

        strptime("12:00 am", "%I:%M %p", address_of broken);
        same_signed((string_address) "and midnight on one", broken.tm_hour, 0);

        strptime("951825045", "%s", address_of broken);
        same_signed((string_address) "seconds since the epoch fill everything",
                    (bipolar)broken.tm_year + 1900, 2000);
        same_signed((string_address) "and the month with them", broken.tm_mon,
                    1);
        same_signed((string_address) "and the hour with them", broken.tm_hour,
                    11);

        broken.tm_gmtoff = -1;
        strptime("+05:30", "%z", address_of broken);
        same_signed((string_address) "an offset with a colon in it",
                    (bipolar)broken.tm_gmtoff, 19800);

        broken.tm_gmtoff = -1;
        strptime("-0530", "%z", address_of broken);
        same_signed((string_address) "an offset behind Greenwich",
                    (bipolar)broken.tm_gmtoff, -19800);

        broken.tm_gmtoff = -1;
        strptime("Z", "%z", address_of broken);
        same_signed((string_address) "the offset that is a letter",
                    (bipolar)broken.tm_gmtoff, 0);

        good((string_address) "sixty minutes is not an offset",
             is_null(strptime("+0060", "%z", address_of broken)));

        /*
                A day of the year on its own derives nothing, because a day of
                the year without a year is not a date. Put a year beside it
                and both the month and the weekday fall out.
        */
        broken.tm_mon = -1;
        broken.tm_mday = -1;
        broken.tm_wday = -1;
        strptime("060", "%j", address_of broken);
        same_signed((string_address) "a day of the year, stored",
                    broken.tm_yday, 59);
        same_signed((string_address) "and nothing derived from it alone",
                    broken.tm_mon, -1);

        strptime("060 2000", "%j %Y", address_of broken);
        same_signed((string_address) "with a year, the month falls out",
                    broken.tm_mon, 1);
        same_signed((string_address) "and the day of the month",
                    broken.tm_mday, 29);
        same_signed((string_address) "and the weekday", broken.tm_wday, 2);

        // %Z steps over a word and does nothing with it, which is why a
        // format that puts %Z in front of a date eats the date.
        rest = strptime("UTC 2000", "%Z %Y", address_of broken);
        same_signed((string_address) "a zone name is stepped over",
                    (bipolar)broken.tm_year + 1900, 2000);
        same_text((string_address) "and consumed to the end", rest,
                  (string_address) "");
        good((string_address) "and it eats a date if one is where it looks",
             is_null(strptime("2000", "%Z %Y", address_of broken)));

        /*
                The four places this does not agree with glibc, checked
                against this answer. See the list at the top of the file for
                why each of them is the answer here.
        */
        broken.tm_year = -1;
        rest = strptime("2000-02-29", "%Ey", address_of broken);
        same_signed((string_address) "%E reads the plain specifier",
                    broken.tm_year, 120);
        same_text((string_address) "and consumes what it read", rest,
                  (string_address) "00-02-29");

        broken.tm_year = -1;
        rest = strptime("2000-02", "%OY-%Om", address_of broken);
        good((string_address) "%O reads the plain specifier too",
             !is_null(rest));
        same_signed((string_address) "and the year with it", broken.tm_year,
                    100);

        broken.tm_hour = -1;
        strptime("11:50 PM", "%l:%M %P", address_of broken);
        same_signed((string_address) "%P is %p", broken.tm_hour, 23);

        broken.tm_wday = -1;
        strptime("0000-01-01", "%Y-%m-%d", address_of broken);
        same_signed((string_address) "the first of January of the year zero",
                    broken.tm_wday, 6);
}

static fn check_asctime(void)
{
        struct tm broken;
        time_t moment = 951825045;
        p8 written[64];

        gmtime_r(address_of moment, address_of broken);
        same_text((string_address) "asctime", asctime_r(address_of broken, written),
                  (string_address) "Tue Feb 29 11:50:45 2000\n");

        same_text((string_address) "ctime", ctime_r(address_of moment, written),
                  (string_address) "Tue Feb 29 11:50:45 2000\n");

        moment = -62167219200LL;                /* 0000-01-01 */
        gmtime_r(address_of moment, address_of broken);
        same_text((string_address) "asctime in the year zero",
                  asctime_r(address_of broken, written),
                  (string_address) "Sat Jan  1 00:00:00 0\n");

        broken.tm_wday = 9;
        good((string_address) "asctime refuses a weekday it cannot name",
             is_null(asctime_r(address_of broken, written)));

        gmtime_r(address_of moment, address_of broken);
        broken.tm_year = 90000;
        good((string_address) "asctime refuses a year it cannot fit",
             is_null(asctime_r(address_of broken, written)));
}

/*
        The four routines that ask the kernel rather than doing arithmetic.
        Nothing here can be compared against a constant, so each is compared
        against the others: they read the same clock and must agree, and the
        two that are supposed to move must move.
*/
static fn check_live(void)
{
        time_t first = time(null);
        time_t written = 0;
        time_t answered = time(address_of written);
        timeval wall = {0, 0};
        timespec real = {0, 0};
        timespec monotonic_first = {0, 0};
        timespec monotonic_second = {0, 0};
        timespec grain = {0, 0};
        timespec nap = {0, 20000000};
        clock_t burned_first;
        clock_t burned_second;
        volatile positive spin = 0;
        positive turn;
        tm address_to broken;
        tm copy;

        good((string_address) "time wrote through its argument",
             written == answered);
        good((string_address) "time is a plausible year", first > 1700000000);

        same((string_address) "gettimeofday answered",
             (positive)gettimeofday(address_of wall, null), 0);
        good((string_address) "gettimeofday agrees with time",
             wall.tv_sec - (b64)first <= 2 && (b64)first - wall.tv_sec <= 2);
        good((string_address) "gettimeofday microseconds are in range",
             wall.tv_usec >= 0 && wall.tv_usec < 1000000);

        same((string_address) "clock_gettime answered",
             (positive)clock_gettime(CLOCK_REALTIME, address_of real), 0);
        good((string_address) "clock_gettime agrees with time",
             (bipolar)real.tv_sec - (bipolar)first <= 2 &&
                     (bipolar)first - (bipolar)real.tv_sec <= 2);
        good((string_address) "clock_gettime nanoseconds are in range",
             real.tv_nsec < 1000000000);

        same((string_address) "monotonic answered",
             (positive)clock_gettime(CLOCK_MONOTONIC,
                                     address_of monotonic_first),
             0);
        sleep(address_of nap);
        same((string_address) "monotonic answered again",
             (positive)clock_gettime(CLOCK_MONOTONIC,
                                     address_of monotonic_second),
             0);
        good((string_address) "monotonic moved forward",
             (bipolar)monotonic_second.tv_sec * 1000000000 +
                             (bipolar)monotonic_second.tv_nsec >
                     (bipolar)monotonic_first.tv_sec * 1000000000 +
                             (bipolar)monotonic_first.tv_nsec);

        same((string_address) "clock_getres answered",
             (positive)clock_getres(CLOCK_REALTIME, address_of grain), 0);
        good((string_address) "clock_getres named a grain",
             grain.tv_sec > 0 || grain.tv_nsec > 0);

        burned_first = clock();
        good((string_address) "clock answered", burned_first >= 0);

        for (turn = 0; turn < 40000000; turn++)
                spin = spin + turn;

        burned_second = clock();
        good((string_address) "clock moved forward", burned_second > burned_first);

        broken = gmtime(address_of first);
        good((string_address) "gmtime answered", !is_null(broken));

        if (!is_null(broken))
        {
                copy = address_to broken;
                good((string_address) "timegm undoes gmtime",
                     timegm(address_of copy) == first);
                setenv((string_address) "TZ", (string_address) "UTC", 1);
                tzset();
                good((string_address) "localtime is UTC when TZ is",
                     localtime(address_of first)->tm_hour == copy.tm_hour);
                good((string_address) "and says so",
                     localtime(address_of first)->tm_gmtoff == 0 &&
                         localtime(address_of first)->tm_isdst == 0);
                setenv((string_address) "TZ", (string_address) "CET-1", 1);
                tzset();
                {
                        time_t epoch = 0;

                        good((string_address) "CET-1 is an hour east",
                             localtime(address_of epoch)->tm_hour == 1 &&
                                 localtime(address_of epoch)->tm_gmtoff == 3600);
                }

                //      The ends of the range, where taking a zone's offset off
                //      a time went past the word: the wall time of the last
                //      second east of UTC and of the first one west of it is
                //      not a date, and the answer is none rather than a date
                //      a wrapped subtraction made up.
                {
                        time_t last = (time_t)0x7fffffffffffffffll;
                        time_t first = (time_t)(-0x7fffffffffffffffll - 1);
                        time_t zero = 0;

                        good((string_address) "the last second has no local time east of UTC",
                             is_null(localtime(address_of last)));
                        setenv((string_address) "TZ",
                               (string_address) "EST5EDT,M3.2.0,M11.1.0", 1);
                        tzset();
                        good((string_address) "the first second has no local time west of UTC",
                             is_null(localtime(address_of first)));
                        good((string_address) "a zone with daylight time answers at both ends",
                             localtime(address_of zero) != null);
                        setenv((string_address) "TZ",
                               (string_address) "NZST-12NZDT,M9.5.0,M4.1.0/3", 1);
                        tzset();
                        good((string_address) "the last second is not in daylight time when its offset overflows",
                             !clock_tz_in_dst(0x7fffffffffffffffll) &&
                                 !clock_tz_in_dst(0x7ffffffffffffffell));
                        setenv((string_address) "TZ", (string_address) "CET-1", 1);
                        tzset();
                }
                good((string_address) "Stockholm maps to CET",
                     clock_zone_posix((string_address) "Europe/Stockholm") &&
                         clock_zone_posix((string_address) "Europe/Stockholm")[0] == 'C');

                //      A keyboard layout's code names its country's zone, and
                //      comes back as the table's own spelling -- that is what
                //      gets stored, so a bowl's glibc can read it too.
                good((string_address) "se is Stockholm",
                     clock_zone_named((string_address) "se") &&
                         string_equals(clock_zone_named((string_address) "SE"),
                                       (string_address) "Europe/Stockholm"));
                good((string_address) "us is the zone most Americans live in",
                     string_equals(clock_zone_named((string_address) "us"),
                                   (string_address) "America/New_York"));
                good((string_address) "a name comes back in the table's case",
                     string_equals(clock_zone_named((string_address)
                                                        "europe/london"),
                                   (string_address) "Europe/London"));
                good((string_address) "every code resolves to a zone",
                     clock_zone_posix((string_address) "no") &&
                         clock_zone_posix((string_address) "dk") &&
                         clock_zone_posix((string_address) "fi") &&
                         clock_zone_posix((string_address) "es") &&
                         clock_zone_posix((string_address) "it"));
                good((string_address) "a word that is no zone is not one",
                     !clock_zone_named((string_address) "mars"));

                //      The table is tzdata's, generated: every zone, link
                //      and country code, sorted so a lookup can bisect, and
                //      every rule one this parser reads. A row out of order
                //      is a zone that silently stops being found.
                {
                        p8 sorted = true;
                        p8 parsed = true;
                        p8 spoken = true;

                        for (positive at = 1; at < array_count(clock_zones); at++)
                                if (string_compare_folded((string_address)clock_zones[at - 1].name,
                                                          (string_address)clock_zones[at].name) >= 0)
                                        sorted = false;
                        for (positive at = 1; at < array_count(clock_zone_links); at++)
                                if (string_compare_folded((string_address)clock_zone_links[at - 1].name,
                                                          (string_address)clock_zone_links[at].name) >= 0)
                                        sorted = false;
                        for (positive at = 1; at < array_count(clock_zone_codes); at++)
                                if (string_compare_folded((string_address)clock_zone_codes[at - 1].code,
                                                          (string_address)clock_zone_codes[at].code) >= 0)
                                        sorted = false;
                        for (positive at = 0; at < array_count(clock_zones); at++)
                        {
                                if (!clock_tz_parse((string_address)clock_zones[at].posix) ||
                                    clock_zone_row((string_address)clock_zones[at].name) !=
                                        (bipolar)at)
                                        parsed = false;
                                if (!clock_zone_spoken((string_address)clock_zones[at].name))
                                        spoken = false;
                        }
                        for (positive at = 0; at < array_count(clock_zone_links); at++)
                                if (clock_zone_row((string_address)clock_zone_links[at].name) < 0)
                                        parsed = false;
                        for (positive at = 0; at < array_count(clock_zone_codes); at++)
                                if (!clock_zone_country((string_address)clock_zone_codes[at].code))
                                        parsed = false;
                        tzset();
                        good((string_address) "the zone tables are in bisecting order",
                             sorted);
                        good((string_address) "every zone, link and country finds a rule that parses",
                             parsed);
                        good((string_address) "and every zone has a name in words",
                             spoken);
                        good((string_address) "hundreds of zones, not a handful",
                             array_count(clock_zones) > 400 &&
                                 array_count(clock_zone_codes) > 240);
                }
                good((string_address) "a tzdata link names its zone",
                     string_equals(clock_zone_named((string_address) "Asia/Calcutta"),
                                   (string_address) "Asia/Kolkata") &&
                         string_equals(clock_zone_named((string_address) "us/eastern"),
                                       (string_address) "America/New_York"));
                good((string_address) "a country from the network is its ISO code",
                     string_equals(clock_zone_country((string_address) "SE"),
                                   (string_address) "Europe/Stockholm") &&
                         string_equals(clock_zone_country((string_address) "sv"),
                                       (string_address) "America/El_Salvador") &&
                         !clock_zone_country((string_address) "uk") &&
                         !clock_zone_country((string_address) "Europe/Stockholm"));
                good((string_address) "while a typed layout word keeps its own country",
                     string_equals(clock_zone_named((string_address) "sv"),
                                   (string_address) "Europe/Stockholm"));

                //      Rules whose change falls outside the day: Jerusalem's
                //      at 26:00 of a Thursday, Nuuk's at -1:00, Chatham's at
                //      2:45 with a 12:45 offset. Each read against the offset
                //      glibc gives for the same instant.
                {
                        static const struct
                        {
                                char zone[24];
                                time_t when;
                                bipolar east;
                        } rules[] = {
                                {"Asia/Jerusalem", 1774569599, 7200},  // 2026-03-26 23:59:59Z
                                {"Asia/Jerusalem", 1774569600, 10800}, // a second later
                                {"America/Nuuk", 1774745999, -7200},   // 2026-03-29 00:59:59Z
                                {"America/Nuuk", 1774746000, -3600},   // a second later
                                {"Pacific/Chatham", 1784116800, 45900},
                                {"Pacific/Chatham", 1768478400, 49500},
                                {"Asia/Gaza", 1784116800, 10800},
                        };
                        p8 all = true;

                        for (positive at = 0; at < array_count(rules); at++)
                        {
                                setenv((string_address) "TZ",
                                       (string_address)rules[at].zone, 1);
                                tzset();
                                if (localtime(address_of rules[at].when)->tm_gmtoff !=
                                    rules[at].east)
                                        all = false;
                        }
                        good((string_address) "rules past midnight and before it read as glibc reads them",
                             all);
                }


                //      An offset is written with the clock's sign, and the
                //      proof is what localtime then says, not the string.
                {
                        static const struct
                        {
                                char text[12];
                                bipolar east;
                        } offsets[] = {
                                {"+1", 3600},      {"-1", -3600},
                                {"UTC+2", 7200},   {"utc-3", -10800},
                                {"+5:30", 19800},  {"-9:30", -34200},
                                {"+14", 50400},    {"GMT+0", 0},
                        };
                        p8 all = true;

                        for (positive at = 0; at < array_count(offsets); at++)
                        {
                                p8 zone[24];
                                time_t epoch = 86400;

                                if (!clock_zone_offset(
                                        (string_address)offsets[at].text, zone,
                                        sizeof(zone)))
                                {
                                        all = false;
                                        continue;
                                }
                                setenv((string_address) "TZ", zone, 1);
                                tzset();
                                if (localtime(address_of epoch)->tm_gmtoff !=
                                    offsets[at].east)
                                        all = false;
                        }
                        good((string_address) "offsets read the way a clock shows them",
                             all);
                }
                {
                        p8 zone[24];

                        good((string_address) "+1 is written as its own name",
                             clock_zone_offset((string_address) "+1", zone,
                                               sizeof(zone)) &&
                                 string_equals(zone, (string_address) "<+01>-1"));
                        good((string_address) "and not as TZ=UTC+1, an hour behind",
                             !string_equals(zone, (string_address) "UTC+1"));
                        good((string_address) "offsets past fourteen hours are refused",
                             !clock_zone_offset((string_address) "+15", zone,
                                                sizeof(zone)));
                        good((string_address) "an offset needs its sign",
                             !clock_zone_offset((string_address) "1", zone,
                                                sizeof(zone)));
                        //      The TZif a bowl reads: glibc and Python's
                        //      zoneinfo were checked against real tzdata by
                        //      hand; this holds the shape that made them agree
                        //      -- version 2, and the rule in the footer.
                        {
                                static p8 tzif[512];
                                string_address rule = (string_address)
                                        "\nCET-1CEST,M3.5.0,M10.5.0/3\n";
                                positive length = clock_zone_tzif(
                                        (string_address) "se", tzif,
                                        sizeof(tzif));
                                positive tail = string_length(rule);

                                good((string_address) "a zone makes a version 2 TZif",
                                     length > tail &&
                                         !memory_compare(tzif, "TZif2", 5));
                                good((string_address) "whose footer is the zone's rule",
                                     length > tail &&
                                         !memory_compare(tzif + length - tail,
                                                         rule, tail));
                                good((string_address) "and a zone that does not parse makes none",
                                     !clock_zone_tzif((string_address) "mars",
                                                      tzif, sizeof(tzif)));
                        }
                        good((string_address) "and nothing after it",
                             !clock_zone_offset((string_address) "+1x", zone,
                                                sizeof(zone)) &&
                                 !clock_zone_offset((string_address) "+5:3",
                                                    zone, sizeof(zone)));
                }

                //      A POSIX zone string, as tzset reads a TZ and as a zone
                //      is kept for every bowl to read. Whole: what the parser
                //      leaves unread is junk in a file every bowl copies, and
                //      glibc's leniency (a summer name it cannot read leaves
                //      the standard zone standing) is for a TZ and no more.
                //      A number too long for a word is not read as the number
                //      its low digits make.
                {
                        static const struct
                        {
                                char text[56];
                                p8 read;  //  tzset's parse takes it
                                p8 whole; //  and it is all of the text
                        } strings[] = {
                                {"CET-1", true, true},
                                {"CET-1CEST", true, true},
                                {"CET-1CEST,M3.5.0,M10.5.0/3", true, true},
                                {"<+0530>-5:30", true, true},
                                {"<-03>3", true, true},
                                {"EST5EDT,M3.2.0,M11.1.0", true, true},
                                {"CET-1CEST,J60/-167,J365/167", true, true},
                                {"CET-1CEST,59,M10.5.0", true, true},
                                {"CET-24", true, true},
                                {"CET-24:00:00", true, true},
                                {"CET-1CEST,M3.4.4/26,M10.5.0", true, true},
                                //      junk after the standard offset
                                {"CET-1 xyz", true, false},
                                {"CET-1\x7f", true, false},
                                {"CET-1\xff", true, false},
                                {"CET-1\n", true, false},
                                {"CET-1CE", true, false},
                                {"CET-1CEST x", false, false},
                                //      too long for a word, wrapping to a good one
                                {"CET-1CEST,M18446744073709551619.1.0,M10.5.0", false, false},
                                {"CET-1CEST,M3.18446744073709551621.0,M10.5.0", false, false},
                                {"CET-1CEST,J18446744073709551617,M10.5.0", false, false},
                                {"CET-1CEST,18446744073709551617,M10.5.0", false, false},
                                {"CET-1CEST,M3.5.0,M99999999999999999999999.5.0", false, false},
                                //      outside the grammar
                                {"<>-1", false, false},
                                {"<ab>-1", false, false},
                                {"<+0530-5:30", false, false},
                                {"CET", false, false},
                                {"CET-25", false, false},
                                {"CET-24:01", false, false},
                                {"CET-24:00:01", false, false},
                                {"CET-1CEST,M3.5.0", false, false},
                                {"CET-1CEST,M13.1.0,M10.5.0", false, false},
                                {"CET-1CEST,M0.1.0,M10.5.0", false, false},
                                {"CET-1CEST,M3.0.0,M10.5.0", false, false},
                                {"CET-1CEST,M3.6.0,M10.5.0", false, false},
                                {"CET-1CEST,M3.5.7,M10.5.0", false, false},
                                {"CET-1CEST,J0,M10.5.0", false, false},
                                {"CET-1CEST,J366,M10.5.0", false, false},
                                {"CET-1CEST,366,M10.5.0", false, false},
                                {"CET-1CEST,J,M10.5.0", false, false},
                                {"CET-1CEST,M3.5.0/-168,M10.5.0", false, false},
                                {"CET-1CEST,M3.5.0/168,M10.5.0", false, false},
                                {"CET-1CEST,M3.5.0,M10.5.0,", false, false},
                                {"CET-1CEST,M3.5.0,M10.5.0x", false, false},
                        };
                        p8 all = true;
                        p8 whole = true;

                        for (positive at = 0; at < array_count(strings); at++)
                        {
                                if (clock_tz_parse((string_address)strings[at].text) !=
                                    strings[at].read)
                                {
                                        all = false;
                                        string_format(log, "  tzset read of %s\n",
                                                      (string_address)strings[at].text);
                                }
                                if (clock_tz_whole((string_address)strings[at].text) !=
                                    strings[at].whole)
                                {
                                        whole = false;
                                        string_format(log, "  kept whole: %s\n",
                                                      (string_address)strings[at].text);
                                }
                        }
                        tzset();
                        good((string_address) "a TZ is read as glibc reads it, and a number is not wrapped",
                             all);
                        good((string_address) "a zone to keep is the whole of its text and is in the grammar",
                             whole);
                        {
                                static p8 tzif[512];

                                good((string_address) "a zone with junk after it is no TZif footer for a bowl",
                                     !clock_zone_tzif((string_address) "CET-1 xyz",
                                                      tzif, sizeof(tzif)) &&
                                         !clock_zone_tzif((string_address) "CET-1\x7f",
                                                          tzif, sizeof(tzif)) &&
                                         clock_zone_tzif((string_address) "CET-1",
                                                         tzif, sizeof(tzif)));
                        }
                }

                setenv((string_address) "TZ", (string_address) "UTC", 1);
                tzset();
        }

        same_signed((string_address) "difftime",
                    (bipolar)difftime((time_t)1000000, (time_t)3), 999997);
        same_signed((string_address) "difftime backwards",
                    (bipolar)difftime((time_t)3, (time_t)1000000), -999997);
}

/*
        The vDSO clock against the trap it replaces.

        An ELF _start marks program_vdso_clock 1, and the first read looks
        the entry up. Wherever the auxiliary vector names a vDSO -- the host
        kernel, and qemu-user where it maps one -- the lookup must find the
        clock, and its readings must sit between two trapped readings of the
        same clock. Marked as having none, the reads fall back to the trap;
        marked unresolved again, the lookup finds the same entry.
*/
static fn check_vdso_clock(void)
{
        string_address address_to walk = program_environment_list();
        positive named = 0;
        positive entry;
        p8 ordered = true;

        good((string_address) "_start marked an auxiliary vector",
             program_vdso_clock != 0);
        while (!is_null(walk) && !is_null(address_to walk))
                walk++;
        if (!is_null(walk))
                for (const p64 address_to aux = (const p64 address_to)(walk + 1);
                     aux[0]; aux += 2)
                        named += aux[0] == 33;

        timespec first = {0, 0}, middle = {0, 0}, last = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, address_of middle);
        entry = program_vdso_clock;
        good((string_address) "the vDSO the auxiliary vector names is found",
             named ? entry > 2 : entry == 2);
        for (positive round = 0; round < 1000; round++)
        {
                system_call_2(syscall(clock_gettime), CLOCK_MONOTONIC,
                              (positive)address_of first);
                clock_gettime(CLOCK_MONOTONIC, address_of middle);
                system_call_2(syscall(clock_gettime), CLOCK_MONOTONIC,
                              (positive)address_of last);
                positive a = first.tv_sec * 1000000000 + first.tv_nsec;
                positive b = middle.tv_sec * 1000000000 + middle.tv_nsec;
                positive c = last.tv_sec * 1000000000 + last.tv_nsec;
                ordered = ordered && a <= b && b <= c;
        }
        good((string_address) "vDSO monotonic readings sit between trapped ones",
             ordered);

        program_vdso_clock = 2;
        good((string_address) "marked without a vDSO, the trap answers",
             clock_gettime(CLOCK_MONOTONIC, address_of middle) == 0 &&
                 program_vdso_clock == 2 && middle.tv_nsec < 1000000000);
        program_vdso_clock = 1;
        clock_gettime(CLOCK_REALTIME, address_of middle);
        good((string_address) "marked unresolved, the lookup finds the same entry",
             program_vdso_clock == entry);
}

static fn check_failure_and_precision(void)
{
        timespec reading;
        struct tm broken;
        static const time_t unrepresentable[] = {b64_min, b64_max};

        errno = 0;
        same_signed("clock_gettime invalid clock", clock_gettime(b32_max, &reading), -1);
        same_signed("clock_gettime sets errno", errno, EINVAL);
        errno = 0;
        same_signed("clock_getres invalid clock", clock_getres(b32_max, &reading), -1);
        same_signed("clock_getres sets errno", errno, EINVAL);
        errno = 123;
        same_signed("clock_gettime succeeds", clock_gettime(CLOCK_REALTIME, &reading), 0);
        same_signed("clock_gettime preserves success errno", errno, 123);

        for (positive i = 0; i < array_count(unrepresentable); i++)
        {
                errno = 0;
                good("gmtime refuses an unrepresentable year",
                     !gmtime_r(unrepresentable + i, &broken));
                same_signed("gmtime reports year overflow", errno, EOVERFLOW);
        }
        broken = (struct tm){.tm_year = b32_max, .tm_mon = 12, .tm_mday = 1};
        errno = 0;
        same_signed("timegm refuses normalization beyond tm_year", timegm(&broken), -1);
        same_signed("timegm reports year overflow", errno, EOVERFLOW);

        // Nearby integer timestamps still differ by whole seconds above
        // double's exact-integer range; rounding them separately loses that.
        static const time_t gaps[] = {1, 3, 17, 1025};
        for (positive power = 53; power < 63; power++)
                for (positive sign = 0; sign < 2; sign++)
                        for (positive i = 0; i < array_count(gaps); i++)
                        {
                                time_t start = (time_t)1 << power;
                                if (sign)
                                        start = -start;
                                good("difftime retains nearby seconds",
                                     difftime(start + gaps[i], start) == (decimal)gaps[i]);
                                good("difftime retains nearby negative seconds",
                                     difftime(start, start + gaps[i]) == -(decimal)gaps[i]);
                        }
        good("difftime spans the full signed range",
             difftime(b64_max, b64_min) == 0x1p64);
        good("difftime spans the full signed range backwards",
             difftime(b64_min, b64_max) == -0x1p64);
}

/*
        The calendar's fast forms against the originals they stand in front
        of. The day count and the year are split in the Neri and Schneider
        form for two billion days either side of the epoch and by Hinnant's
        arithmetic beyond it; the two are walked against each other over the
        decades people live in, over the edge of the window, and over a
        spread of the rest (the whole window was compared once, all
        4,294,967,296 days, on a 9950X in two minutes). strftime's plain
        path -- the directives that carry no flag, width or case -- is
        compared with the state machine, whose answer is the definition,
        over a spread of times, zones, formats and buffer sizes.
*/
static positive fast_state = 88172645463325252ULL;

static positive fast_random(void)
{
        fast_state ^= fast_state << 13;
        fast_state ^= fast_state >> 7;
        fast_state ^= fast_state << 17;
        return fast_state;
}

static fn fast_civil(bipolar days)
{
        bipolar year[2], month[2], day[2];

        clock_civil_from_days(days, address_of year[0], address_of month[0], address_of day[0]);
        clock_civil_from_days_far(days, address_of year[1], address_of month[1], address_of day[1]);
        checks++;
        if (year[0] != year[1] || month[0] != month[1] || day[0] != day[1])
        {
                failures++;
                string_format(log, "  FAILED civil_from_days %b: %b-%b-%b against %b-%b-%b\n", days,
                              year[0], month[0], day[0], year[1], month[1], day[1]);
        }
}

static fn fast_break(bipolar seconds)
{
        tm one, two;
        p8 first = clock_break_down(seconds, address_of one);
        p8 second = clock_break_down_far(seconds, address_of two);

        checks++;
        if (first != second ||
            (first && (one.tm_sec != two.tm_sec || one.tm_min != two.tm_min || one.tm_hour != two.tm_hour ||
                       one.tm_mday != two.tm_mday || one.tm_mon != two.tm_mon || one.tm_year != two.tm_year ||
                       one.tm_wday != two.tm_wday || one.tm_yday != two.tm_yday)))
        {
                failures++;
                string_format(log, "  FAILED break_down %b\n", seconds);
        }
}

static fn check_fast_forms(void)
{
        static const bipolar edges[] = {0, 1, -1, 2147483647, -2147483648, 2147483648, -2147483649, 4294967295,
                                        (bipolar)1 << 40, -((bipolar)1 << 40), (bipolar)1 << 60, -((bipolar)1 << 60)};

        for (bipolar days = -800000; days <= 800000; days++)
                fast_civil(days);
        for (positive i = 0; i < array_count(edges); i++)
                for (bipolar offset = -400; offset <= 400; offset++)
                        fast_civil(edges[i] + offset);
        for (positive i = 0; i < 400000; i++)
                fast_civil((bipolar)fast_random() >> (fast_random() % 63));

        for (bipolar year = -6000; year <= 12000; year++)
                for (bipolar month = 0; month <= 13; month++)
                        for (bipolar day = -1; day <= 32; day += (day < 28 ? 9 : 1))
                        {
                                checks++;
                                if (clock_days_from_civil(year, month, day) != clock_days_from_civil_far(year, month, day))
                                {
                                        failures++;
                                        string_format(log, "  FAILED days_from_civil %b-%b-%b\n", year, month, day);
                                }
                        }
        for (positive i = 0; i < 400000; i++)
        {
                bipolar year = (bipolar)fast_random() >> (14 + fast_random() % 50);
                bipolar month = (bipolar)(fast_random() % 16) - 2;
                bipolar day = (bipolar)(fast_random() % 40) - 4;

                checks++;
                if (clock_days_from_civil(year, month, day) != clock_days_from_civil_far(year, month, day))
                {
                        failures++;
                        string_format(log, "  FAILED days_from_civil %b-%b-%b\n", year, month, day);
                }
        }
        for (bipolar year = -2000; year <= 6000; year++)
        {
                checks++;
                if (clock_year_leap(year) != (clock_days_from_civil_far(year, 3, 1) - clock_days_from_civil_far(year, 2, 1) == 29))
                {
                        failures++;
                        string_format(log, "  FAILED year_leap %b\n", year);
                }
        }

        for (positive i = 0; i < array_count(edges); i++)
                for (bipolar offset = -3000; offset <= 3000; offset += 7)
                        fast_break(edges[i] + offset * 86400 + offset);
        for (positive i = 0; i < 600000; i++)
                fast_break((bipolar)fast_random() >> (fast_random() % 63));
        for (bipolar seconds = -90000; seconds <= 90000 * 400; seconds += 997)
                fast_break(seconds);

        /* strftime: the plain path against the state machine */
        static const char pieces[] =
                "%Y\0%y\0%F\0%D\0%x\0%m\0%d\0%e\0%H\0%k\0%I\0%l\0%M\0%S\0%T\0%R\0%j\0%a\0%A\0%b\0%B\0%h\0%p\0%P\0%Z\0%z\0"
                "%n\0%t\0%%\0%c\0%X\0%r\0%s\0%u\0%w\0-\0:\0 \0T\0/\0x\0,\0UTC\0%-d\0%_H\0%05Y\0%Ey\0%Oe\0%U\0%:z\0%N\0%G\0%V\0"
                "%^a\0%#Z\0%q\0%C\0%g\0%W\0%Q\0%\0%1\0%+Y\0%10z\0"
                "%Y-%m-%d\0%H:%M:%S\0%Y-%m-%\0%H:%M:%\0%Y-%m\0%H:%M\0%Y-%m-%e\0%H:%M:%M\0";
        const char address_to piece[80];
        positive pieces_count = 0;

        for (const char address_to walk = pieces; address_to walk != end; walk += string_length((string_address)walk) + 1)
                piece[pieces_count++] = walk;
        for (positive i = 0; i < 400000; i++)
        {
                tm broken;
                p8 format[256];
                p8 plain[512], machine[512];
                positive at = 0;
                positive picks = 1 + fast_random() % 8;
                p8 extensions = fast_random() & 1;
                positive room = fast_random() % 3 ? 512 : fast_random() % 100;

                if (!clock_break_down((bipolar)fast_random() >> (fast_random() % 20 + 16), address_of broken))
                        continue;
                if (fast_random() % 4 == 0)
                        broken.tm_gmtoff = ((bipolar)(fast_random() % 100000) - 50000) * (fast_random() % 3 ? 60 : 1);
                if (fast_random() % 6 == 0)
                        broken.tm_zone = fast_random() % 2 ? null : "CEST";
                if (fast_random() % 10 == 0)
                        broken.tm_hour = (b32)(fast_random() % 130) - 10;
                if (fast_random() % 10 == 0)
                        broken.tm_mon = (b32)(fast_random() % 20) - 3;
                if (fast_random() % 10 == 0)
                        broken.tm_wday = (b32)(fast_random() % 12) - 2;
                if (fast_random() % 12 == 0)
                        broken.tm_year = (b32)(fast_random() % 20000) - 5000;
                for (positive pick = 0; pick < picks; pick++)
                {
                        const char address_to text = piece[fast_random() % 10 < 8 ? fast_random() % 43 : fast_random() % pieces_count];

                        for (; address_to text != end && at < 200; text++)
                                format[at++] = (p8)address_to text;
                }
                format[at] = end;

                memory_fill(plain, 0xa5, sizeof(plain));
                memory_fill(machine, 0xa5, sizeof(machine));
                positive fraction = fast_random() % 2 ? 123456789 : fast_random() % 1500000000;
                positive got = clock_format(plain, room, (const char address_to)format, address_of broken, extensions, fraction);
                positive want = clock_format_machine(machine, room, (const char address_to)format, address_of broken,
                                                     extensions, fraction);
                p8 same_bytes = got == want && (!got || memory_compare(plain, machine, got + 1) == 0);

                for (positive past = room; past < room + 8 && past < sizeof(plain); past++)
                        same_bytes = same_bytes && plain[past] == 0xa5;
                checks++;
                if (!same_bytes && format[0] == '%')
                {
                        failures++;
                        string_format(log, "  FAILED strftime plain path: [%s] got %p [%s] want %p [%s]\n",
                                      (string_address)format, got, (string_address)plain, want, (string_address)machine);
                }
        }
}

b32 main(void)
{
        tm epoch = {0};

        setenv((string_address) "TZ", (string_address) "UTC", 1);
        tzset();
        check_fast_forms();
        good("strptime refuses signed epoch overflow",
             !strptime("9223372036854775808", "%s", &epoch));
        good("strptime refuses wrapped epoch overflow",
             !strptime("18446744073709551616", "%s", &epoch));
        check_corners();
        check_format_strings();
        check_format_flags();
        check_normalise();
        check_scan();
        check_asctime();
        check_live();
        check_vdso_clock();
        check_failure_and_precision();

        same((string_address) "every day from 1900 to 2100", clock_test_dense(),
             CLOCK_HASH_DENSE);
        same((string_address) "a coarse sweep of twelve thousand years",
             clock_test_coarse(), CLOCK_HASH_COARSE);
        same((string_address) "mktime and timegm normalising",
             clock_test_normalise(), CLOCK_HASH_NORMALISE);
        same((string_address) "every strftime specifier", clock_test_format(),
             CLOCK_HASH_FORMAT);
        same((string_address) "strftime at every buffer size",
             clock_test_bounds(), CLOCK_HASH_BOUNDS);
        same((string_address) "every specifier under every flag and width",
             clock_test_decorated(), CLOCK_HASH_DECORATED);
        same((string_address) "seventy new years through the week counters",
             clock_test_weeks(), CLOCK_HASH_WEEKS);
        same((string_address) "strptime against what it will and will not take",
             clock_test_scan(), CLOCK_HASH_SCAN);
        same((string_address) "strptime reading back what strftime wrote",
             clock_test_scan_round(), CLOCK_HASH_SCAN_ROUND);
        same((string_address) "asctime", clock_test_asctime(),
             CLOCK_HASH_ASCTIME);

        return test_report(null);
}
#endif /* CHECK_clock */

#ifdef CHECK_math_differential
/*
        Each line of CHECK_math_differential_reference's answers on the
        standard input, answered again by this tree's routine.
*/
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

#define SHARED_math_differential_body
#include "../checks.c"
#undef SHARED_math_differential_body

static p8 md_text[1 << 24];

static positive md_field(p8 address_to address_to at)
{
        positive value = 0;

        while (**at == ' ')
                (*at)++;

        for (; **at && **at != ' ' && **at != '\n'; (*at)++)
                value = value << 4 | (positive)(**at <= '9' ? **at - '0' : (**at | 0x20) - 'a' + 10);

        return value;
}

static decimal md_value(positive bits)
{
        union
        {
                decimal value;
                positive bits;
        } view;

        view.bits = bits;
        return view.value;
}

static positive md_bits(decimal value)
{
        union
        {
                decimal value;
                positive bits;
        } view;

        view.value = value;
        return view.bits;
}

static decimal md_call(positive routine, decimal x, decimal y)
{
        switch (routine)
        {
        case 0: return sine(x);
        case 1: return cosine(x);
        case 2: return tangent(x);
        case 3: return exponential(x);
        case 4: return exponential_two(x);
        case 5: return exponential_minus_one(x);
        case 6: return logarithm(x);
        case 7: return logarithm_two(x);
        case 8: return logarithm_ten(x);
        case 9: return arc_sine(x);
        case 10: return arc_cosine(x);
        case 11: return arc_tangent(x);
        case 12: return hyperbolic_sine(x);
        case 13: return hyperbolic_cosine(x);
        case 14: return hyperbolic_tangent(x);
        case 15: return cube_root(x);
        case 16: return power(x, y);
        case 17: return arc_tangent_two(x, y);
        case 18: return hypotenuse(x, y);
        case 19: return decimal_modulo(x, y);
        default: return decimal_remainder(x, y);
        }
}

//      Last places apart, the negatives reflected about the sign bit so the
//      patterns are in numeric order; two NaNs are the same answer.
static positive md_distance(positive mine, positive theirs)
{
        p8 mine_nan = (mine >> 52 & 0x7ff) == 0x7ff && (mine & ((1ull << 52) - 1));
        p8 theirs_nan = (theirs >> 52 & 0x7ff) == 0x7ff && (theirs & ((1ull << 52) - 1));

        if (mine_nan || theirs_nan)
                return mine_nan && theirs_nan ? 0 : ~(positive)0;
        if (mine >> 63)
                mine = 0x8000000000000000ull - (mine & 0x7fffffffffffffffull);
        if (theirs >> 63)
                theirs = 0x8000000000000000ull - (theirs & 0x7fffffffffffffffull);
        return mine > theirs ? mine - theirs : theirs - mine;
}

b32 main(void)
{
        positive have = 0;
        bipolar got;
        p8 address_to at = md_text;

        while (have + 1 < sizeof(md_text) &&
               (got = (bipolar)system_call_3(syscall(read), 0,
                                             (positive)(md_text + have),
                                             sizeof(md_text) - 1 - have)) > 0)
                have += (positive)got;
        md_text[have] = end;

        while (*at)
        {
                positive routine = md_field(address_of at);
                positive x = md_field(address_of at);
                positive y = md_field(address_of at);
                positive want = md_field(address_of at);
                positive answer = md_bits(md_call(routine, md_value(x), md_value(y)));
                positive distance = md_distance(answer, want);

                while (*at == '\n')
                        at++;

                checks++;

                if (routine >= MD_ROUTINES ||
                    distance > (positive)md_routines[routine].allowance)
                {
                        if (failures++ < 20)
                                string_format(log, "  FAIL %s(%p, %p) is %p, glibc %p\n",
                                              routine < MD_ROUTINES
                                                  ? (string_address)md_routines[routine].name
                                                  : (string_address) "?",
                                              x, y, answer, want);
                }
        }

        return test_report(null);
}
#endif /* CHECK_math_differential */

#ifdef CHECK_math_differential_reference
/*
        The arguments, and glibc's long double answers to them rounded to a
        double. Built by the host compiler with -lm; the answer key for
        CHECK_math_differential.
*/
#include <math.h>
#include <stdio.h>

#define MD_REFERENCE
#define SHARED_math_differential_body
#include "../checks.c"
#undef SHARED_math_differential_body

static double md_call(long routine, double x, double y)
{
        long double a = x;
        long double b = y;

        switch (routine)
        {
        case 0: return (double)sinl(a);
        case 1: return (double)cosl(a);
        case 2: return (double)tanl(a);
        case 3: return (double)expl(a);
        case 4: return (double)exp2l(a);
        case 5: return (double)expm1l(a);
        case 6: return (double)logl(a);
        case 7: return (double)log2l(a);
        case 8: return (double)log10l(a);
        case 9: return (double)asinl(a);
        case 10: return (double)acosl(a);
        case 11: return (double)atanl(a);
        case 12: return (double)sinhl(a);
        case 13: return (double)coshl(a);
        case 14: return (double)tanhl(a);
        case 15: return (double)cbrtl(a);
        case 16: return (double)powl(a, b);
        case 17: return (double)atan2l(a, b);
        case 18: return (double)hypotl(a, b);
        case 19: return fmod(x, y);
        default: return remainder(x, y);
        }
}

int main(void)
{
        for (long routine = 0; routine < (long)MD_ROUTINES; routine++)
                for (long index = 0; index < md_cases(&md_routines[routine]); index++)
                {
                        union
                        {
                                double value;
                                md_word bits;
                        } x, y, answer;

                        md_case(routine, index, &x.value, &y.value);
                        answer.value = md_call(routine, x.value, y.value);
                        printf("%lx %016llx %016llx %016llx\n", routine, x.bits, y.bits,
                               answer.bits);
                }
        return 0;
}
#endif /* CHECK_math_differential_reference */

#ifdef CHECK_math
#include "../../src/lib.util.c"

/*
        The rest of <math.h>: what a comparison with glibc cannot state.

        How close each routine comes to the right answer is measured by
        CHECK_math_differential, against glibc's long double routines rounded
        back to a double, over every argument this file once pinned a table
        row for and eight thousand more a routine drawn over its domain. What
        stays here is the identities and the special values: the classes of
        every kind of number the format holds, both signed zeros where they
        have to survive, the round trips that must come back unchanged, and
        the handful of arguments where the standard says exactly what the
        answer is.

        This runs on all three architectures. arm64 and riscv64 produce
        byte identical results to x86_64 on everything here.
*/

#if defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic ignored "-Woverflow"
#endif

#define SHARED_named_cases
#include "../checks.c"
#undef SHARED_named_cases

typedef union
{
        decimal value;
        p64 bits;
} math_test_shape;

static decimal math_test_value(p64 bits)
{
        math_test_shape shape;
        shape.bits = bits;
        return shape.value;
}

static p64 math_test_bits(decimal value)
{
        math_test_shape shape;
        shape.value = value;
        return shape.bits;
}

/*
        The distance between two doubles counted in last places.

        Two's complement on the bit pattern does not order negative doubles,
        which are stored sign-and-magnitude, so the negatives are reflected
        about the sign bit first. After that the patterns are in increasing
        numeric order and their difference is the number of representable
        values between them, which is what an ulp count means. Two NaNs are
        the same answer whatever their payloads; a NaN against a number is as
        far away as it is possible to be.
*/
static positive math_test_distance(decimal mine, decimal reference)
{
        p64 left = math_test_bits(mine);
        p64 right = math_test_bits(reference);

        if (decimal_is_nan(mine) && decimal_is_nan(reference))
                return 0;
        if (decimal_is_nan(mine) || decimal_is_nan(reference))
                return ~(p64)0;

        if (left >> 63)
                left = 0x8000000000000000ULL - (left & 0x7fffffffffffffffULL);
        if (right >> 63)
                right = 0x8000000000000000ULL - (right & 0x7fffffffffffffffULL);

        return left > right ? left - right : right - left;
}

static positive math_test_failures = 0;


/*
        The classes, of one of every kind of number the format holds.

        Six questions about ten values, and every answer is written down
        rather than derived, because a classification routine that agreed
        with a second classification routine written the same way would prove
        nothing about either.
*/
test(classification)
{
        decimal zero = 0.0;
        decimal minus_zero = math_test_value(0x8000000000000000ULL);
        decimal one = 1.0;
        decimal minus_one = -1.0;
        decimal endless = math_test_value(0x7ff0000000000000ULL);
        decimal minus_endless = math_test_value(0xfff0000000000000ULL);
        decimal not_a_number = math_test_value(0x7ff8000000000000ULL);
        decimal smallest = math_test_value(0x0000000000000001ULL);
        decimal largest_subnormal = math_test_value(0x000fffffffffffffULL);
        decimal largest = math_test_value(0x7fefffffffffffffULL);

        fail(decimal_class(zero) == MATH_CLASS_ZERO);
        fail(decimal_class(minus_zero) == MATH_CLASS_ZERO);
        fail(decimal_class(one) == MATH_CLASS_NORMAL);
        fail(decimal_class(minus_one) == MATH_CLASS_NORMAL);
        fail(decimal_class(endless) == MATH_CLASS_INFINITE);
        fail(decimal_class(minus_endless) == MATH_CLASS_INFINITE);
        fail(decimal_class(not_a_number) == MATH_CLASS_NAN);
        fail(decimal_class(smallest) == MATH_CLASS_SUBNORMAL);
        fail(decimal_class(largest_subnormal) == MATH_CLASS_SUBNORMAL);
        fail(decimal_class(largest) == MATH_CLASS_NORMAL);

        fail(!decimal_is_nan(zero) && !decimal_is_nan(endless) && decimal_is_nan(not_a_number));
        fail(!decimal_is_infinite(largest) && decimal_is_infinite(endless) &&
             decimal_is_infinite(minus_endless));
        fail(decimal_is_finite(zero) && decimal_is_finite(smallest) && decimal_is_finite(largest));
        fail(!decimal_is_finite(endless) && !decimal_is_finite(not_a_number));
        fail(decimal_is_normal(one) && decimal_is_normal(largest));
        fail(!decimal_is_normal(zero) && !decimal_is_normal(smallest) && !decimal_is_normal(endless));

        //      The one question a comparison against zero cannot answer.
        fail(!decimal_sign_bit(zero) && decimal_sign_bit(minus_zero));
        fail(!decimal_sign_bit(one) && decimal_sign_bit(minus_one));
        fail(!decimal_sign_bit(endless) && decimal_sign_bit(minus_endless));

        //      And the same six of a float, which are a different set of
        //      masks against a different width.
        fail(!narrow_is_nan(1.0f) && narrow_is_nan(0.0f / 0.0f));
        fail(narrow_is_infinite(1.0f / 0.0f) && !narrow_is_infinite(3.4028234663852886e38f));
        fail(narrow_is_finite(0.0f) && !narrow_is_finite(1.0f / 0.0f));
        fail(narrow_is_normal(1.0f) && !narrow_is_normal(0.0f));
        fail(narrow_sign_bit(-0.0f) && !narrow_sign_bit(0.0f));
        fail(narrow_class(0.0f) == MATH_CLASS_ZERO);
        fail(narrow_class(1.0f) == MATH_CLASS_NORMAL);

        // Extended values must retain their own range, subnormal boundary,
        // NaN/sign encoding and single-evaluation macro contract.
        static const struct { f128 value; b32 kind; p8 minus; } extended[] = {
                {0.0L, FP_ZERO, false}, {-0.0L, FP_ZERO, true},
                {1.0L, FP_NORMAL, false}, {-1.0L, FP_NORMAL, true},
                {__LDBL_MAX__, FP_NORMAL, false}, {-__LDBL_MAX__, FP_NORMAL, true},
                {__LDBL_MIN__, FP_NORMAL, false},
                {__LDBL_DENORM_MIN__, FP_SUBNORMAL, false},
                {__builtin_huge_vall(), FP_INFINITE, false},
                {-__builtin_huge_vall(), FP_INFINITE, true},
                {__builtin_nanl(""), FP_NAN, false},
                {-__builtin_nansl(""), FP_NAN, true},
        };
        for (positive i = 0; i < array_count(extended); i++)
        {
                volatile f128 value = extended[i].value;
                b32 kind = extended[i].kind;
                positive reads = 0;
                fail(fpclassify((reads++, value)) == kind);
                fail(isnan((reads++, value)) == (kind == FP_NAN));
                fail(isinf((reads++, value)) == (kind == FP_INFINITE));
                fail(isfinite((reads++, value)) == (kind != FP_NAN && kind != FP_INFINITE));
                fail(isnormal((reads++, value)) == (kind == FP_NORMAL));
                fail(signbit((reads++, value)) == extended[i].minus);
                fail(reads == 6);
        }
#if __LDBL_MANT_DIG__ == 64
        static const struct { p64 low; p32 high; b32 kind; } x87[] = {
                {0x8000000000000000ULL, 0, FP_NORMAL},
                {0x8000000000000001ULL, 0, FP_NORMAL},
                {0, 1, FP_NAN}, {1, 0x7ffe, FP_NAN}, {0, 0x7fff, FP_NAN},
        };
        for (positive i = 0; i < array_count(x87); i++)
        for (positive minus = 0; minus < 2; minus++)
        {
                p128 bits = x87[i].low | (p128)(x87[i].high | (minus << 15)) << 64;
                f128 value = memory_cast(f128, bits);
                fail(fpclassify(value) == x87[i].kind && signbit(value) == minus);
                fail(isnan(value) == (x87[i].kind == FP_NAN));
        }
#endif

        return true;
}

/*
        The zeros that have to stay negative.

        Every one of these is a place where the obvious arithmetic produces a
        positive zero and the standard asks for a negative one, usually
        because a series multiplied a minus zero by a negative coefficient on
        its way past. They are cheap to get right and impossible to notice
        going wrong.
*/
test(signed_zeros)
{
        decimal minus_zero = math_test_value(0x8000000000000000ULL);

        fail(math_test_bits(sine(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(tangent(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(arc_sine(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(arc_tangent(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(exponential_minus_one(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(hyperbolic_sine(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(hyperbolic_tangent(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(cube_root(minus_zero)) == 0x8000000000000000ULL);
        fail(math_test_bits(decimal_scaled(minus_zero, 10)) == 0x8000000000000000ULL);

        //      cos of a minus zero is a positive one, which is the one place
        //      here the sign is supposed to disappear.
        fail(math_test_bits(cosine(minus_zero)) == 0x3ff0000000000000ULL);

        //      modf of a whole negative number leaves a negative zero
        //      behind, and modf of a negative zero leaves two of them.
        {
                decimal whole;
                fail(math_test_bits(decimal_split_whole(-3.0, address_of whole)) ==
                     0x8000000000000000ULL);
                fail(whole == -3.0);
                fail(math_test_bits(decimal_split_whole(minus_zero, address_of whole)) ==
                     0x8000000000000000ULL);
                fail(math_test_bits(whole) == 0x8000000000000000ULL);
        }

        //      fmod keeps the dividend's sign on a zero remainder, and
        //      remainder does the same.
        fail(math_test_bits(decimal_modulo(-6.0, 3.0)) == 0x8000000000000000ULL);
        fail(math_test_bits(decimal_remainder(-6.0, 3.0)) == 0x8000000000000000ULL);

        //      atan2 has four zeros of its own and the standard names all of
        //      them.
        fail(math_test_bits(arc_tangent_two(0.0, 1.0)) == 0);
        fail(math_test_bits(arc_tangent_two(minus_zero, 1.0)) == 0x8000000000000000ULL);
        fail(arc_tangent_two(0.0, -1.0) > 3.14 && arc_tangent_two(0.0, -1.0) < 3.15);
        fail(decimal_sign_bit(arc_tangent_two(minus_zero, -1.0)));

        return true;
}

/*
        The answers the standard states outright, which no approximation is
        allowed to be near rather than equal to.
*/
test(exact_answers)
{
        decimal endless = math_test_value(0x7ff0000000000000ULL);
        decimal minus_endless = math_test_value(0xfff0000000000000ULL);

        fail(exponential(0.0) == 1.0);
        fail(exponential_two(0.0) == 1.0);
        fail(exponential_minus_one(0.0) == 0.0);
        fail(logarithm(1.0) == 0.0);
        fail(logarithm_two(1.0) == 0.0);
        fail(logarithm_ten(1.0) == 0.0);
        fail(sine(0.0) == 0.0);
        fail(cosine(0.0) == 1.0);
        fail(tangent(0.0) == 0.0);
        fail(arc_sine(0.0) == 0.0);
        fail(arc_tangent(0.0) == 0.0);
        fail(arc_cosine(1.0) == 0.0);
        fail(hyperbolic_sine(0.0) == 0.0);
        fail(hyperbolic_cosine(0.0) == 1.0);
        fail(hyperbolic_tangent(0.0) == 0.0);

        //      Every power of two is its own base two logarithm, exactly,
        //      because the exponent field is the answer and nothing is
        //      multiplied into it.
        {
                b32 exponent;
                for (exponent = -1022; exponent <= 1023; exponent++)
                        fail(logarithm_two(decimal_scaled(1.0, exponent)) == (decimal)exponent);
        }

        //      And exp2 of a whole number is that power of two.
        {
                b32 exponent;
                for (exponent = -1022; exponent <= 1023; exponent++)
                        fail(exponential_two((decimal)exponent) == decimal_scaled(1.0, exponent));
        }

        //      Small whole powers are exact until the result stops fitting.
        fail(power(2.0, 10.0) == 1024.0);
        fail(power(3.0, 5.0) == 243.0);
        fail(power(10.0, 22.0) == 1e22);
        fail(power(-2.0, 3.0) == -8.0);
        fail(power(-2.0, 4.0) == 16.0);
        fail(power(0.5, 3.0) == 0.125);

        //      Exact cubes come back exact.
        fail(cube_root(8.0) == 2.0);
        fail(cube_root(-8.0) == -2.0);
        fail(cube_root(27.0) == 3.0);
        fail(cube_root(1000000.0) == 100.0);

        //      hypot of a leg and a zero is the leg, and the three four five
        //      triangle is exact.
        fail(hypotenuse(3.0, 4.0) == 5.0);
        fail(hypotenuse(5.0, 0.0) == 5.0);
        fail(hypotenuse(0.0, -7.0) == 7.0);

        //      The cases C99 lists for pow that do not depend on the base at
        //      all, a NaN included.
        fail(power(math_test_value(0x7ff8000000000000ULL), 0.0) == 1.0);
        fail(power(1.0, math_test_value(0x7ff8000000000000ULL)) == 1.0);
        fail(power(-1.0, endless) == 1.0);
        fail(power(-1.0, minus_endless) == 1.0);
        fail(power(0.5, endless) == 0.0);
        fail(power(2.0, endless) == endless);
        fail(power(2.0, minus_endless) == 0.0);
        fail(power(endless, 2.0) == endless);
        fail(power(minus_endless, 3.0) == minus_endless);
        fail(power(minus_endless, 2.0) == endless);
        fail(power(0.0, -1.0) == endless);
        fail(power(math_test_value(0x8000000000000000ULL), -1.0) == minus_endless);

        //      A negative base and a fractional exponent is not a real
        //      number and has to say so.
        fail(decimal_is_nan(power(-2.0, 0.5)));
        //      Every double from 2^52 up is a whole number, so a negative
        //      base to one of them is +1 or -1 by the parity of the exponent.
        //      math_whole_kind shifted by 64 at this exponent, and the bit it
        //      read as "not whole" turned each of these into a NaN.
        fail(power(-1.0, 4503599627370496.0) == 1.0);
        fail(power(-1.0, 4503599627370497.0) == -1.0);
        fail(power(-1.0, 9007199254740990.0) == 1.0);
        fail(power(-1.0, 9007199254740991.0) == -1.0);
        fail(decimal_is_nan(logarithm(-1.0)));
        fail(decimal_is_nan(arc_sine(1.5)));
        fail(decimal_is_nan(arc_cosine(-1.5)));
        fail(decimal_is_nan(sine(endless)));
        fail(decimal_is_nan(decimal_modulo(1.0, 0.0)));

        //      An infinity beats a NaN in hypot, which is the one place C
        //      says it does.
        fail(hypotenuse(endless, math_test_value(0x7ff8000000000000ULL)) == endless);

        //      log of zero is minus infinity and of a negative is a NaN.
        fail(logarithm(0.0) == minus_endless);
        fail(logarithm_two(0.0) == minus_endless);
        fail(logarithm_ten(0.0) == minus_endless);

        return true;
}

/*
        The round trips, which say more about the exact routines than any
        table of answers could: they run over the whole exponent range rather
        than over a handful of points.
*/
test(round_trips)
{
        b32 exponent;
        b32 given;
        decimal value;
        decimal part;
        decimal whole;

        //      frexp hands back a significand in [1/2, 1) and an exponent
        //      that rebuilds the argument exactly, for every exponent the
        //      format has including the subnormal ones.
        for (exponent = -1074; exponent <= 1023; exponent++)
        {
                value = decimal_scaled(1.5, exponent);
                if (value == 0.0 || decimal_is_infinite(value))
                        continue;
                part = decimal_split_exponent(value, address_of given);
                fail(absolute(part) >= 0.5 && absolute(part) < 1.0);
                fail(decimal_scaled(part, given) == value);
        }

        //      ldexp up and back down is the identity wherever nothing
        //      overflowed on the way.
        for (exponent = -500; exponent <= 500; exponent++)
        {
                value = 1.2345678901234567;
                fail(decimal_scaled(decimal_scaled(value, exponent), -exponent) == value);
        }

        //      modf's two halves add back to the argument exactly.
        for (exponent = -60; exponent <= 60; exponent++)
        {
                value = decimal_scaled(1.4142135623730951, exponent);
                part = decimal_split_whole(value, address_of whole);
                fail(whole + part == value);
                fail(whole == decimal_truncated(value));
        }

        //      fmod is exact by construction, so the identity that defines
        //      it holds to the bit: x is a whole number of divisors plus the
        //      remainder, and the remainder is smaller than the divisor.
        for (exponent = -300; exponent <= 300; exponent++)
        {
                decimal left = decimal_scaled(1.7320508075688772, exponent);
                decimal right = 3.25;
                decimal left_over = decimal_modulo(left, right);
                fail(absolute(left_over) < absolute(right));
                fail(decimal_modulo(left_over, right) == left_over);
        }

        //      remainder lands inside half a divisor either way, always.
        for (exponent = -300; exponent <= 300; exponent++)
        {
                decimal left = decimal_scaled(1.7320508075688772, exponent);
                decimal right = 3.25;
                fail(absolute(decimal_remainder(left, right)) <= 0.5 * absolute(right));
        }

        return true;
}

/*
        Payne and Hanek's reduction is what this is really testing.

        An argument reduction that degrades with the size of the argument
        will pass every table above, because the tables cannot cover the
        whole exponent range densely. What catches it is an identity that has
        to hold at every scale: sine squared plus cosine squared is one, and
        the tangent is their quotient, and both of those stop holding the
        moment the reduction loses bits -- long before the answer looks
        obviously wrong.
*/
test(circular_identities)
{
        b32 exponent;
        decimal value;
        decimal opposite;
        decimal adjacent;
        decimal total;

        for (exponent = -60; exponent <= 1023; exponent++)
        {
                value = decimal_scaled(1.4142135623730951, exponent);
                opposite = sine(value);
                adjacent = cosine(value);
                total = opposite * opposite + adjacent * adjacent;

                //      Four ulp of one, which is what two kernels squared
                //      and added can cost and no more.
                fail(math_test_distance(total, 1.0) <= 4);

                //      And the tangent agrees with the quotient it is not
                //      computed as, everywhere the quotient is meaningful.
                if (absolute(adjacent) > 1e-8)
                        fail(math_test_distance(tangent(value), opposite / adjacent) <= 8);
        }

        //      The inverses undo the forward functions across the domain.
        //
        //      The tolerance here is absolute rather than in last places,
        //      and that is not laziness. acos of zero is pi/2, which is not
        //      a double; the cosine of the double it rounds to is 6.1e-17
        //      rather than zero, and 6.1e-17 is an infinite number of last
        //      places away from zero while being a perfectly correct answer.
        //      A round trip through a function with a zero in it can only be
        //      judged on the absolute size of what comes back.
        for (exponent = 0; exponent < 2000; exponent++)
        {
                value = -1.0 + (decimal)exponent * 0.001;
                fail(absolute(sine(arc_sine(value)) - value) <= 1.0e-15);
                fail(absolute(cosine(arc_cosine(value)) - value) <= 1.0e-15);
                fail(absolute(tangent(arc_tangent(value)) - value) <= 1.0e-15);
                fail(absolute(hyperbolic_tangent(value) -
                              hyperbolic_sine(value) / hyperbolic_cosine(value)) <= 1.0e-15);
        }

        //      exp and log undo each other, and pow agrees with repeated
        //      multiplication where the multiplication is exact.
        //
        //      The allowance on the two round trips grows with the size of
        //      the logarithm, and that is arithmetic rather than slack. The
        //      logarithm's error is half an ulp OF THE LOGARITHM, which for
        //      an argument of 2^-300 is an absolute error of 208 times
        //      2^-53; the exponential then turns an absolute error in its
        //      argument into a relative error in its answer of the same
        //      size. A round trip through both is therefore allowed to be
        //      about |log x| units out and cannot be asked for better by any
        //      implementation of either.
        for (exponent = -300; exponent <= 300; exponent++)
        {
                positive allowance;

                value = decimal_scaled(1.7320508075688772, exponent);

                allowance = 8 + 2 * (positive)absolute(logarithm(value));
                fail(math_test_distance(exponential(logarithm(value)), value) <= allowance);

                allowance = 8 + 2 * (positive)absolute(logarithm_two(value));
                fail(math_test_distance(exponential_two(logarithm_two(value)), value) <= allowance);

                fail(math_test_distance(power(value, 2.0), value * value) <= 2);
                fail(math_test_distance(power(value, 0.5), square_root(value)) <= 2);

                //      Only where the cube itself survived the format: a
                //      cube that overflowed or flushed to zero says nothing
                //      about the cube root.
                if (decimal_is_finite(value * value * value) && value * value * value != 0.0)
                        fail(math_test_distance(cube_root(value * value * value), value) <= 4);
        }

        //      cosh squared minus sinh squared is one, which is the
        //      hyperbolic version of the same test and reaches the far end
        //      of the exponential's range.
        //      The two squares grow together and their difference stays
        //      one, so the difference is judged against the last bit of the
        //      squares rather than against the last bit of the one. Past a
        //      cosine of about a hundred million the one is entirely below
        //      that last bit and the subtraction can only answer zero, which
        //      would be a statement about the format rather than about these
        //      two routines, so the sweep stops there.
        for (exponent = 0; exponent < 700; exponent++)
        {
                value = (decimal)exponent * 0.025;
                opposite = hyperbolic_sine(value);
                adjacent = hyperbolic_cosine(value);
                if (adjacent > 1.0e8)
                        break;
                fail(absolute(adjacent * adjacent - opposite * opposite - 1.0) <=
                     8.0 * adjacent * adjacent * 2.220446049250313e-16);
        }

        return true;
}

test(modulo_subnormal_bit_ranges)
{
        for (positive bit = 0; bit < MATH_MANTISSA_BITS; bit++)
                for (positive low = 0; low < 32; low++)
                        for (positive divisor = 1; divisor < 64; divisor++)
                                for (positive sign = 0; sign < 4; sign++)
                                {
                                        p64 numerator = ((p64)1 << bit) | low;
                                        p64 top_sign = (p64)(sign & 1) << 63;
                                        p64 bottom_sign = (p64)(sign >> 1) << 63;
                                        p64 quotient = 0;
                                        decimal got = math_modulo_quotient(
                                                math_test_value(numerator | top_sign),
                                                math_test_value(divisor | bottom_sign), &quotient);
                                        fail(math_test_bits(got) == ((numerator % divisor) | top_sign));
                                        fail(quotient == numerator / divisor);
                                }
        return true;
}

static test_case test_cases[] = {
        case(modulo_subnormal_bit_ranges),
        case(classification),
        case(signed_zeros),
        case(exact_answers),
        case(round_trips),
        case(circular_identities),
        {0, 0, 0},
};

b32 main()
{
        log_direct(str("math tests\n\n"));

        test_cases_walk(test_cases);

        return test_report((string_address) "\n");
}
#endif /* CHECK_math */

#ifdef CHECK_matrix
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
#endif
#ifdef CHECK_native_matrix
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "matrix.h"
#endif

#if defined(CHECK_matrix) || defined(CHECK_native_matrix)
/*
        gemm_f32 against a double-precision reference.

        Two programs are built from this: CHECK_matrix, freestanding on the
        library, which test/run's matrix lane runs on all three machines --
        x86_64 once per tier with the feature bytes walked down, riscv64 under
        qemu with no V and at VLEN 128, 256 and 512 -- and CHECK_native_matrix,
        which links the arm64 bodies lifted out of lib.c and runs them on an
        arm64 Mac (its main is with the other native sections). What is
        between here and the first main is what both share, and is written
        against nothing but the compiler: the two builds differ only in how
        they map pages and how they report.

        Every M, N and K from 0 to 40 (every one to 14, 34 and 10, then in
        steps) both ways round: at beta 0 over random floats, beta 1 over
        small integers, beta 0.5 and -0.75 over random floats, and beta 0
        over a C full of NaN. Each operand ends against a page that faults
        and C has sixteen sentinels in front of it, so no tail reads or
        writes past an end. Random floats are held to the order-independent
        bound of a serial sum, |got - want| <= (K + 2) eps S + 1e-35 with eps
        2^-24 and S the sum of the terms' magnitudes; small integers, whose
        products and partial sums are all exact, to equality, which catches
        an index wrong by one that a loose bound would not.

        Then the trainer's N and K -- 768, 1364, 682, 256, 32 and 8192 -- with
        M cut short (a long K still runs every block of it and every panel),
        682, 1024, 1536, 2048 and 4096 alone in each dimension, and the full
        8192-row shapes wherever the program runs natively, those checked at
        sampled outputs. A NaN in A, B or C reaches every output it feeds and
        nothing else; K of 0 leaves beta C without reading A or B, and M or N
        of 0 touches nothing, all with null operands; trans_b arrives with
        junk above its 32 bits and must answer as its low half.
*/
typedef void matrix_call(float *, const float *, const float *, unsigned long long, unsigned long long,
                         unsigned long long, unsigned, float);
typedef void matrix_wide_call(float *, const float *, const float *, unsigned long long, unsigned long long,
                              unsigned long long, unsigned long long, float);

typedef struct
{
        unsigned char *base;
        unsigned long long span;
} matrix_pages;

//      The faults a property found, by kind.
typedef struct
{
        unsigned long long values, outside, nans, edges, narrow, cases;
} matrix_faults;

#ifdef CHECK_matrix
static unsigned long long matrix_page(void)
{
        return 4096;
}

static unsigned char *matrix_mapping(unsigned long long span)
{
        bipolar mapped = system_call_6(syscall(mmap), 0, span, FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                                       FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS, (positive)-1, 0);

        return mapped < 0 ? (unsigned char *)0 : (unsigned char *)mapped;
}

static int matrix_fence(unsigned char *page, unsigned long long size)
{
        return system_call_3(syscall(mprotect), (positive)page, size, 0) == 0;
}

static void matrix_unmapping(unsigned char *base, unsigned long long span)
{
        system_call_2(syscall(munmap), (positive)base, span);
}
#else
static unsigned long long matrix_page(void)
{
        return (unsigned long long)getpagesize();
}

static unsigned char *matrix_mapping(unsigned long long span)
{
        void *mapped = mmap(0, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

        return mapped == MAP_FAILED ? (unsigned char *)0 : (unsigned char *)mapped;
}

static int matrix_fence(unsigned char *page, unsigned long long size)
{
        return mprotect(page, size, PROT_NONE) == 0;
}

static void matrix_unmapping(unsigned char *base, unsigned long long span)
{
        munmap(base, span);
}
#endif

static unsigned long long matrix_seed = 0x9e3779b97f4a7c15ull;

static unsigned long long matrix_next(void)
{
        XORSHIFT64(matrix_seed);
        return matrix_seed;
}

static double matrix_real(void)
{
        return (double)((long long)(matrix_next() >> 11) - (1ll << 52)) / (double)(1ll << 52);
}

static float matrix_small(int span)
{
        return (float)((int)(matrix_next() % (unsigned long long)(2 * span + 1)) - span);
}

static float matrix_float(unsigned bits)
{
        float value;

        __builtin_memcpy(&value, &bits, 4);
        return value;
}

//      C's starting value at an index, so that any sampled output knows
//      what it began from: kind 1 a small integer, 2 a NaN, else a float.
static float matrix_c0(unsigned long long at, unsigned kind)
{
        unsigned long long h = (at + 1) * 0x9e3779b97f4a7c15ull;

        if (kind == 2)
                return matrix_float(0x7fc00001u);
        if (kind == 1)
                return (float)((int)(h >> 57) - 64);
        return (float)((double)((long long)(h >> 11) - (1ll << 52)) / (double)(1ll << 52));
}

//      An array of floats ending against a page that faults, with before
//      floats of room in front of it.
static float *matrix_against(matrix_pages *pages, unsigned long long floats, unsigned long long before)
{
        unsigned long long page = matrix_page(), bytes = (floats + before) * 4;
        unsigned long long span = (bytes + page - 1) / page * page + page;

        pages->base = matrix_mapping(span);
        pages->span = span;
        if (!pages->base || !matrix_fence(pages->base + span - page, page))
                return (float *)0;
        return (float *)(pages->base + span - page) - floats;
}

static void matrix_release(matrix_pages *pages)
{
        if (pages->base && pages->span)
                matrix_unmapping(pages->base, pages->span);
}

//      The sweep's operands, mapped once: three regions of 16384 floats
//      before a page that faults, an operand placed to end against it.
static matrix_pages matrix_pool[3];
static float *matrix_pool_end[3];

static float *matrix_take(matrix_pages *pages, int which, unsigned long long floats, unsigned long long before)
{
        if (floats + before > 16384)
                return matrix_against(pages, floats, before);
        if (!matrix_pool_end[which])
        {
                float *start = matrix_against(&matrix_pool[which], 16384, 0);

                if (!start)
                        return (float *)0;
                matrix_pool_end[which] = start + 16384;
        }
        pages->base = (unsigned char *)0, pages->span = 0;
        return matrix_pool_end[which] - floats;
}

//      The worst error seen, in eps S: what the lane log reports.
static double matrix_worst;

//      One output against the reference: the sum and its magnitude in
//      double, beta C added when C is read.
static int matrix_holds(const float *A, const float *B, float got, unsigned long long at, unsigned long long N,
                        unsigned long long K, unsigned trans, float beta, unsigned kind)
{
        unsigned long long i = at / N, j = at % N;
        double want = 0, size = 0;

        for (unsigned long long k = 0; k < K; k++)
        {
                double term = (double)A[i * K + k] * (double)(trans ? B[j * K + k] : B[k * N + j]);

                want += term;
                size += __builtin_fabs(term);
        }
        if (beta != 0)
        {
                double c = (double)beta * (double)matrix_c0(at, kind);

                want += c;
                size += __builtin_fabs(c);
        }
        double off = __builtin_fabs((double)got - want);

        if (kind == 1)
                return off == 0;
        if (size > 1e-30 && off > matrix_worst * 0x1p-24 * size)
                matrix_worst = off / (0x1p-24 * size);
        return off <= (double)(K + 2) * 0x1p-24 * size + 1e-35;
}

//      kind 0: random floats; 1: small integers, exact; 2: random, C NaN at
//      beta 0. sample 0 judges every output, else that many at random.
static void matrix_one(matrix_call *routine, unsigned long long M, unsigned long long N, unsigned long long K,
                       unsigned trans, float beta, unsigned kind, unsigned long long sample, matrix_faults *faults)
{
        matrix_pages pa, pb, pc;
        unsigned long long na = M * K, nb = N * K, nc = M * N;
        float *A = matrix_take(&pa, 0, na, 0), *B = matrix_take(&pb, 1, nb, 0), *C = matrix_take(&pc, 2, nc, 16);

        faults->cases++;
        if (!A || !B || !C)
        {
                faults->outside++;
                return;
        }
        for (unsigned long long i = 0; i < na; i++)
                A[i] = kind == 1 ? matrix_small(8) : (float)matrix_real();
        for (unsigned long long i = 0; i < nb; i++)
                B[i] = kind == 1 ? matrix_small(8) : (float)matrix_real();
        for (int i = 0; i < 16; i++)
                C[i - 16] = 1234.5f;
        for (unsigned long long i = 0; i < nc; i++)
                C[i] = matrix_c0(i, kind);
        routine(C, A, B, M, N, K, trans, beta);
        unsigned long long count = sample ? sample : nc;

        for (unsigned long long t = 0; t < count && nc; t++)
        {
                unsigned long long at = sample ? matrix_next() % nc : t;

                if (!matrix_holds(A, B, C[at], at, N, K, trans, beta, kind))
                {
                        faults->values++;
                        break;
                }
        }
        for (int i = 0; i < 16; i++)
                faults->outside += C[i - 16] != 1234.5f;
        matrix_release(&pa), matrix_release(&pb), matrix_release(&pc);
}

static void matrix_sweep(matrix_call *routine, matrix_faults *faults, int full)
{
        static const float betas[] = {0.5f, -0.75f};

        for (unsigned long long M = 0; M <= 40; M += M < 14 ? 1 : 3)
                for (unsigned long long N = 0; N <= 70; N += N < 34 ? 1 : 5)
                        for (unsigned long long K = 0; K <= 40; K += K < 10 ? 1 : 7)
                                for (unsigned trans = 0; trans < 2; trans++)
                                {
                                        unsigned long long turn = M + N + K;

                                        matrix_one(routine, M, N, K, trans, 0.0f, 0, 0, faults);
                                        if (turn % 3 == 0)
                                                matrix_one(routine, M, N, K, trans, 1.0f, 1, 0, faults);
                                        if (turn % 5 == 0)
                                                matrix_one(routine, M, N, K, trans, betas[turn % 2], 0, 0, faults);
                                        if (turn % 7 == 0)
                                                matrix_one(routine, M, N, K, trans, 0.0f, 2, 0, faults);
                                }
        static const unsigned long long shapes[][3] = {
                {50, 768, 256},  {37, 256, 256},  {26, 1364, 256}, {31, 682, 256}, {40, 256, 682},
                {29, 256, 768},  {19, 256, 1364}, {13, 256, 8192}, {7, 682, 8192}, {256, 256, 32},
                {256, 32, 256},  {31, 256, 682},  {33, 682, 256},  {40, 256, 256}, {3, 682, 5},
                {682, 3, 7},     {5, 7, 1024},    {1024, 9, 3},    {2, 4096, 3},   {4096, 2, 5},
                {7, 3, 4096},    {13, 33, 4100},  {29, 50, 2048},  {37, 97, 1536}, {40, 70, 512},
                {31, 53, 2049},
        };
        for (unsigned s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++)
                for (unsigned trans = 0; trans < 2; trans++)
                {
                        unsigned long long M = shapes[s][0], N = shapes[s][1], K = shapes[s][2];
                        unsigned long long sample = M * N > 20000 ? 512 : 0;

                        matrix_one(routine, M, N, K, trans, 0.0f, 0, sample, faults);
                        matrix_one(routine, M, N, K, trans, 1.0f, 1, sample, faults);
                        matrix_one(routine, M, N, K, trans, -0.75f, 0, sample, faults);
                        matrix_one(routine, M, N, K, trans, 0.0f, 2, sample, faults);
                }
        if (!full)
                return;
        //      the trainer's shapes at full size, judged at 256 outputs
        static const unsigned long long trained[][4] = {
                {8192, 768, 256, 1},  {8192, 256, 256, 1}, {8192, 1364, 256, 1}, {8192, 682, 256, 1},
                {8192, 256, 682, 1},  {8192, 256, 768, 0}, {8192, 256, 1364, 0}, {8192, 682, 256, 0},
                {768, 256, 8192, 0},  {1364, 256, 8192, 0}, {256, 682, 8192, 0}, {256, 256, 8192, 0},
                {256, 256, 32, 1},    {256, 32, 256, 0},   {256, 256, 682, 1},  {256, 682, 256, 0},
                {256, 256, 256, 0},
        };
        for (unsigned s = 0; s < sizeof(trained) / sizeof(trained[0]); s++)
        {
                unsigned long long M = trained[s][0], N = trained[s][1], K = trained[s][2];
                unsigned trans = (unsigned)trained[s][3];

                matrix_one(routine, M, N, K, trans, 1.0f, 1, 256, faults);
                matrix_one(routine, M, N, K, trans, 0.0f, 2, 256, faults);
        }
}

//      A NaN in A, B or C reaches every output it feeds and no other.
static void matrix_nans(matrix_call *routine, matrix_faults *faults)
{
        static float A[37 * 23], B[23 * 41], C[37 * 41];

        for (unsigned trans = 0; trans < 2; trans++)
                for (int where = 0; where < 3; where++)
                {
                        for (int i = 0; i < 37 * 23; i++)
                                A[i] = (float)matrix_real();
                        for (int i = 0; i < 23 * 41; i++)
                                B[i] = (float)matrix_real();
                        for (int i = 0; i < 37 * 41; i++)
                                C[i] = 1.0f;
                        if (where == 0)
                                A[5 * 23 + 17] = matrix_float(0x7fc00001u);
                        else if (where == 1)
                                B[trans ? 29 * 23 + 11 : 11 * 41 + 29] = matrix_float(0xffc00002u);
                        else
                                C[7 * 41 + 3] = matrix_float(0x7fc00003u);
                        routine(C, A, B, 37, 41, 23, trans, 1.0f);
                        for (int i = 0; i < 37; i++)
                                for (int j = 0; j < 41; j++)
                                {
                                        int here = where == 0 ? i == 5 : where == 1 ? j == 29 : i == 7 && j == 3;

                                        faults->nans += here != (__builtin_isnan(C[i * 41 + j]) != 0);
                                }
                }
}

//      K of 0 is beta C, zero at beta 0 without reading C; M or N of 0
//      touches nothing; and none of them reads A or B.
static void matrix_edges(matrix_call *routine, matrix_faults *faults)
{
        static float C[6 * 7];

        for (int i = 0; i < 42; i++)
                C[i] = matrix_float(0x7fc00001u);
        routine(C, (float *)0, (float *)0, 6, 7, 0, 0, 0.0f);
        for (int i = 0; i < 42; i++)
                faults->edges += C[i] != 0.0f;
        for (int i = 0; i < 42; i++)
                C[i] = (float)i - 7.0f;
        routine(C, (float *)0, (float *)0, 6, 7, 0, 1, -0.5f);
        for (int i = 0; i < 42; i++)
                faults->edges += C[i] != ((float)i - 7.0f) * -0.5f;
        for (int i = 0; i < 42; i++)
                C[i] = 3.0f;
        routine(C, (float *)0, (float *)0, 0, 7, 9, 0, 0.0f);
        routine(C, (float *)0, (float *)0, 6, 0, 9, 1, 0.0f);
        for (int i = 0; i < 42; i++)
                faults->edges += C[i] != 3.0f;
}

//      trans_b is 32 bits: junk above it must not change the answer.
static void matrix_narrow(matrix_call *routine, matrix_wide_call *wide, matrix_faults *faults)
{
        static float A[9 * 13], B[13 * 11], one[9 * 11], two[9 * 11];

        for (int i = 0; i < 9 * 13; i++)
                A[i] = (float)matrix_real();
        for (int i = 0; i < 13 * 11; i++)
                B[i] = (float)matrix_real();
        for (unsigned trans = 0; trans < 2; trans++)
                for (unsigned long long junk = 1; junk < 1u << 20; junk = junk * 7 + 3)
                {
                        for (int i = 0; i < 99; i++)
                                one[i] = two[i] = (float)i;
                        routine(one, A, B, 9, 11, 13, trans, 0.5f);
                        wide(two, A, B, 9, 11, 13, trans | junk << 32, 0.5f);
                        for (int i = 0; i < 99; i++)
                                faults->narrow += __builtin_memcmp(&one[i], &two[i], 4) != 0;
                }
}
#endif /* CHECK_matrix || CHECK_native_matrix */

#ifdef CHECK_matrix
//      the full-size shapes where the lane runs natively
#if X64
#define MATRIX_FULL 1
#else
#define MATRIX_FULL 0
#endif

// Every call the first in a process, on riscv64's short pass.
static p8 matrix_first;

static void matrix_gemm(float *C, const float *A, const float *B, unsigned long long M, unsigned long long N,
                        unsigned long long K, unsigned trans, float beta)
{
        VECTOR_FIRST_CALL(matrix_first);
        gemm_f32(C, A, B, M, N, K, trans, beta);
}

//      One tier's properties, each one check.
static fn matrix_tier(string_address tier)
{
        matrix_faults faults = {0, 0, 0, 0, 0, 0};

        matrix_worst = 0;
        matrix_sweep(matrix_gemm, &faults, MATRIX_FULL);
        matrix_nans(matrix_gemm, &faults);
        matrix_edges(matrix_gemm, &faults);
        matrix_narrow(matrix_gemm, (matrix_wide_call *)(address_any)gemm_f32, &faults);
        check("gemm_f32: every shape within its bound, small integers exact", faults.values == 0);
        check("gemm_f32: nothing past an end read or written, sentinels kept", faults.outside == 0);
        check("gemm_f32: a NaN in A, B or C reaches every output it feeds and no other", faults.nans == 0);
        check("gemm_f32: K of 0 is beta C, M or N of 0 touches nothing, A and B unread", faults.edges == 0);
        check("gemm_f32: trans_b is read as 32 bits", faults.narrow == 0);
        string_format(log, "  %s: gemm_f32 %p cases, worst ", tier, faults.cases);
        decimal_to_string(log, matrix_worst);
        string_format(log, " eps S\n");
}

b32 main()
{
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;

        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                cpu_has_fma = tier < 2 ? fused : 0;
                matrix_tier(cpu_has_avx512 ? "avx512" : cpu_has_avx2 && cpu_has_fma ? "avx2 fma" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2, cpu_has_fma = fused;
#elif RISCV64
        //      The first call asks for V itself and answers; the lane runs
        //      this once with no V and once at each length, so the sweep is
        //      the machine's own body, and the other body gets a short pass.
        {
                static float a[3] = {1, 2, 3}, b[3] = {4, 5, 6}, c[1] = {0};

                cpu_has_vector = 0;
                gemm_f32(c, a, b, 1, 1, 3, 1, 0.0f);
                check("gemm_f32: the first call asks for V and answers", c[0] == 32.0f && cpu_has_vector != 0);
        }
        p8 vector = cpu_has_vector;

        //      Every call the first in a process, which asks for V and comes
        //      back to the body with its state in the frame, the operands
        //      against pages that fault.
        {
                matrix_faults faults = {0, 0, 0, 0, 0, 0};

                matrix_first = true;
                for (unsigned long long M = 0; M <= 13; M++)
                        for (unsigned long long N = 0; N <= 9; N++)
                                for (unsigned trans = 0; trans < 2; trans++)
                                {
                                        matrix_one(matrix_gemm, M, N, M + N, trans, 0.0f, 0, 0, &faults);
                                        matrix_one(matrix_gemm, M, N, 2 * N + 1, trans, 1.0f, 1, 0, &faults);
                                }
                matrix_one(matrix_gemm, 19, 682, 257, 1, -0.75f, 0, 0, &faults);
                matrix_one(matrix_gemm, 13, 256, 600, 0, 0.0f, 2, 512, &faults);
                matrix_first = false;
                check("gemm_f32: every call the first in a process", faults.values == 0 && faults.outside == 0 &&
                                                                      cpu_has_vector == vector);
        }
        matrix_tier(vector == 2 ? "V" : "no V");
        if (vector == 2)
        {
                matrix_faults faults = {0, 0, 0, 0, 0, 0};

                cpu_has_vector = 1;
                for (unsigned long long M = 0; M <= 13; M++)
                        for (unsigned long long N = 0; N <= 9; N++)
                                for (unsigned trans = 0; trans < 2; trans++)
                                {
                                        matrix_one(matrix_gemm, M, N, M + N, trans, 0.0f, 0, 0, &faults);
                                        matrix_one(matrix_gemm, M, N, 2 * N + 1, trans, 1.0f, 1, 0, &faults);
                                }
                matrix_one(matrix_gemm, 19, 682, 257, 1, -0.75f, 0, 0, &faults);
                matrix_one(matrix_gemm, 13, 256, 600, 0, 0.0f, 2, 512, &faults);
                check("gemm_f32: the scalar body where V is present", faults.values == 0 && faults.outside == 0);
                cpu_has_vector = vector;
        }
#else
        matrix_tier("neon");
#endif
        return test_report(null);
}
#endif /* CHECK_matrix */

#ifdef CHECK_modulo
/*
        Experimental C standard library

        fmod and remainder, against glibc's

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        One file, built twice, the way CHECK_allocator is.

        Built the ordinary way it includes lib.util.c and runs
        standard.c's fmod and remainder on x86_64, arm64 and riscv64. Built
        with MODULO_REFERENCE defined it links glibc's libm instead, and
        test/run's math lane builds it with -fno-builtin and calls through a
        pointer besides, because GCC otherwise expands fmod inline as an x87
        fprem loop and the reference would be the compiler's, not glibc's.

        Both builds walk the same seeded generator and print, before the
        "shared-end" line, one digest per generator class of every fmod and
        remainder answer. The lane diffs those lines, so a class that disagrees
        is named. The classes are built to reach every path of the division:
        raw bit patterns, every pairing of exponent fields, equal exponents,
        subnormal pairs of every width, huge ratios, exact multiples, halfway
        quotients for remainder's tie, small integers, the special values
        against anything, near-equal magnitudes, and the widest gaps with odd
        full-width divisors.

        A NaN answer is folded to one pattern before it is digested. Which
        NaN comes out is not an answer anybody defines: the sign and the
        payload of a NaN are left open by IEEE 754 and by C, SSE, NEON and
        RISC-V each choose differently when both operands are NaNs (riscv64
        always answers its canonical NaN), GCC may swap the operands of the
        multiplication that makes one, and glibc 2.43's remainder clears the
        sign that its fmod keeps. Every other bit is compared, both zeros
        included.

        After "shared-end", each build checks what needs no reference on
        every pair and prints the verdict: a finite dividend over a finite
        nonzero divisor answers a finite value smaller than the divisor with
        the dividend's sign, remainder lands within half a divisor, an
        infinite dividend or a zero or NaN operand answers a NaN, and an
        infinite divisor or a zero dividend hands the dividend back.
*/

#ifdef MODULO_REFERENCE

#include <math.h>
#include <stdio.h>

typedef unsigned char p8;
typedef unsigned long long positive;
typedef unsigned long long p64;
typedef long long bipolar;
typedef int b32;
typedef double decimal;
typedef void fn;

#define address_to *
#define address_of &
#define bool p8

#else

#include "../../src/lib.util.c"

#endif

#define MODULO_PAIRS 4194304
#define MODULO_CLASSES 12
#define MODULO_SIGN 0x8000000000000000ULL
#define MODULO_MAGNITUDE 0x7fffffffffffffffULL
#define MODULO_INFINITY 0x7ff0000000000000ULL
#define MODULO_NAN 0x7ff8000000000000ULL

typedef union
{
        decimal value;
        p64 bits;
} modulo_shape;

//      Through a pointer, so neither build can see which routine it calls.
static decimal (address_to volatile modulo_fmod)(decimal, decimal) = fmod;
static decimal (address_to volatile modulo_remainder)(decimal, decimal) = remainder;

static p8 modulo_said[4096];
static positive modulo_said_used;

static fn modulo_say(const char address_to text)
{
        while (address_to text)
        {
                if (modulo_said_used < sizeof(modulo_said))
                        modulo_said[modulo_said_used++] = (p8)address_to text;
                text++;
        }
}

static fn modulo_say_number(positive value)
{
        p8 digits[24];
        b32 count = 0;

        do
                digits[count++] = (p8)('0' + value % 10);
        while (value /= 10);

        while (count--)
                if (modulo_said_used < sizeof(modulo_said))
                        modulo_said[modulo_said_used++] = digits[count];
}

static fn modulo_say_hex(p64 value)
{
        for (b32 shift = 60; shift >= 0; shift -= 4)
                if (modulo_said_used < sizeof(modulo_said))
                        modulo_said[modulo_said_used++] =
                                (p8)"0123456789abcdef"[(value >> shift) & 15];
}

static fn modulo_flush(void)
{
#ifdef MODULO_REFERENCE
        fwrite(modulo_said, 1, (size_t)modulo_said_used, stdout);
        fflush(stdout);
#else
        log(modulo_said, modulo_said_used);
        log_flush();
#endif
}

static p64 modulo_state;

//      splitmix64: a fixed seed and the same sequence in both builds.
static p64 modulo_next(void)
{
        p64 z = (modulo_state += 0x9e3779b97f4a7c15ULL);

        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
}

static decimal modulo_value(p64 bits)
{
        modulo_shape shape;

        shape.bits = bits;
        return shape.value;
}

static p64 modulo_bits(decimal value)
{
        modulo_shape shape;

        shape.value = value;
        return shape.bits;
}

static p64 modulo_field(p64 sign, p64 exponent, p64 mantissa)
{
        return (sign << 63) | ((exponent & 0x7ff) << 52) | (mantissa & 0xfffffffffffffULL);
}

//      A loop rather than the builtin, which is a libgcc call on a riscv
//      baseline and this file links no libgcc.
static b32 modulo_leading_zeros(p64 value)
{
        b32 count = 0;

        if (!value)
                return 64;

        while (!(value >> 63))
        {
                value <<= 1;
                count++;
        }

        return count;
}

//      The double equal to integer times two to scale, for an integer below
//      2^53 and a scale that keeps it normal.
static p64 modulo_scaled(p64 sign, p64 integer, bipolar scale)
{
        b32 lead = modulo_leading_zeros(integer) - 11;

        return modulo_field(sign, (p64)(scale - lead + 52 + 1023), integer << lead);
}

static const p64 modulo_specials[] = {
        0x0000000000000000ULL, 0x8000000000000000ULL, 0x7ff0000000000000ULL,
        0xfff0000000000000ULL, 0x7ff8000000000000ULL, 0xfff8000000000000ULL,
        0x7ff4000000000001ULL, 0x0000000000000001ULL, 0x8000000000000001ULL,
        0x000fffffffffffffULL, 0x0010000000000000ULL, 0x0010000000000001ULL,
        0x7fefffffffffffffULL, 0xffefffffffffffffULL, 0x3ff0000000000000ULL,
        0xbff0000000000000ULL, 0x4008000000000000ULL, 0x3fe0000000000000ULL,
};

static fn modulo_pair(positive index, p64 address_to x, p64 address_to y)
{
        p64 a = modulo_next();
        p64 b = modulo_next();
        p64 c = modulo_next();

        switch (index % MODULO_CLASSES)
        {
        case 0: //      raw bit patterns
                address_to x = a;
                address_to y = b;
                break;

        case 1: //      every pairing of exponent fields
                address_to x = modulo_field(a >> 63, c, a);
                address_to y = modulo_field(b >> 63, c >> 11, b);
                break;

        case 2: //      equal exponents
                address_to x = modulo_field(a >> 63, c, a);
                address_to y = modulo_field(b >> 63, c, b);
                break;

        case 3: //      subnormal pairs of every width
                address_to x = modulo_field(a >> 63, 0, a >> (c % 53));
                address_to y = modulo_field(b >> 63, 0, b >> ((c >> 8) % 53));
                break;

        case 4: //      huge ratios, subnormal divisors among them
                address_to x = modulo_field(a >> 63, 1900 + c % 147, a);
                address_to y = modulo_field(b >> 63, (c >> 8) % 64, b);
                break;

        case 5: //      exact multiples: y = m 2^s, x = k m 2^(s + j), j up to the top
        {
                p64 m = (b % (1ULL << 26)) | 1;
                p64 k = a % (1ULL << 26) + 1;
                bipolar s = (bipolar)(c % 1901) - 1000;

                address_to y = modulo_scaled(b >> 63, m, s);
                address_to x = modulo_scaled(a >> 63, k * m,
                                             s + (bipolar)((c >> 16) % (p64)(971 - s)));
                break;
        }

        case 6: //      halfway quotients: x = (2k + 1) m 2^(s - 1)
        {
                p64 m = (b % (1ULL << 26)) | 1;
                p64 k = a % (1ULL << 25);
                bipolar s = (bipolar)(c % 1901) - 1000;

                address_to y = modulo_scaled(b >> 63, m, s);
                address_to x = modulo_scaled(a >> 63, (2 * k + 1) * m, s - 1);
                break;
        }

        case 7: //      small integers
        {
                bipolar p = (bipolar)(a % (1ULL << 21)) - (1 << 20);
                bipolar q = (bipolar)(b % (1ULL << 21)) - (1 << 20);

                address_to x = modulo_bits((decimal)p);
                address_to y = modulo_bits((decimal)(q ? q : 1));
                break;
        }

        case 8: //      the special values against anything
                address_to x = (c & 1) ? modulo_specials[a % (sizeof(modulo_specials) / 8)] : a;
                address_to y = (c & 2) ? modulo_specials[b % (sizeof(modulo_specials) / 8)] : b;
                break;

        case 9: //      near-equal magnitudes
                address_to y = modulo_field(b >> 63, c, b);
                address_to x = (((address_to y) & MODULO_MAGNITUDE) + a % 8 - 4) |
                               ((a >> 63) << 63);
                break;

        case 10: //     the widest gaps, odd full-width divisors
                address_to x = modulo_field(a >> 63, 2040 + c % 7, a);
                address_to y = modulo_field(b >> 63, c % 3, b | 1);
                break;

        default: //     huge dividends by small integers, multiples or not
        {
                decimal small = (decimal)(b % 1024 + 1);

                address_to x = modulo_field(a >> 63, 1100 + c % 947, a);
                address_to y = modulo_bits((b >> 63) ? -small : small);
                break;
        }
        }
}

static p8 modulo_is_nan(p64 bits)
{
        return (bits & MODULO_MAGNITUDE) > MODULO_INFINITY;
}

static p64 modulo_mix(p64 digest, p64 value)
{
        if (modulo_is_nan(value))
                value = MODULO_NAN;

        digest ^= value;
        digest *= 0x100000001b3ULL;
        return digest ^ (digest >> 29);
}

//      What each answer must be without anything to compare it with.
static p8 modulo_sound(p64 x, p64 y, p64 kept, p64 near)
{
        p64 x_size = x & MODULO_MAGNITUDE;
        p64 y_size = y & MODULO_MAGNITUDE;

        if (modulo_is_nan(x) || modulo_is_nan(y) || x_size == MODULO_INFINITY || y_size == 0)
                return modulo_is_nan(kept) && modulo_is_nan(near);

        if (y_size == MODULO_INFINITY || x_size == 0)
                return kept == x && near == x;

        decimal divisor = modulo_value(y_size);
        decimal truncated = modulo_value(kept & MODULO_MAGNITUDE);
        decimal nearest = modulo_value(near & MODULO_MAGNITUDE);

        return (kept & MODULO_MAGNITUDE) < MODULO_INFINITY &&
               (kept & MODULO_SIGN) == (x & MODULO_SIGN) &&
               truncated < divisor &&
               (near & MODULO_MAGNITUDE) < MODULO_INFINITY &&
               nearest <= divisor - nearest;
}

#ifdef MODULO_REFERENCE
int main(int argument_count, char **arguments)
#else
b32 main(void)
#endif
{
        positive pairs = MODULO_PAIRS;
        positive failures = 0;
        p64 digest[MODULO_CLASSES] = {0};
        const char address_to count_text = 0;

#ifdef MODULO_REFERENCE
        if (argument_count > 1)
                count_text = arguments[1];
#else
        if (program_argument_count() > 1)
                count_text = (const char address_to)program_argument(1);
#endif

        //      A count on the command line walks further than the lane does,
        //      the same sequence from the same seed.
        if (count_text && address_to count_text)
        {
                pairs = 0;
                while (address_to count_text >= '0' && address_to count_text <= '9')
                        pairs = pairs * 10 + (positive)(address_to count_text++ - '0');
        }

        modulo_state = 0x2545f4914f6cdd1dULL;

        for (positive index = 0; index < pairs; index++)
        {
                p64 x, y;

                modulo_pair(index, address_of x, address_of y);

                p64 kept = modulo_bits(modulo_fmod(modulo_value(x), modulo_value(y)));
                p64 near = modulo_bits(modulo_remainder(modulo_value(x), modulo_value(y)));

                digest[index % MODULO_CLASSES] =
                        modulo_mix(modulo_mix(digest[index % MODULO_CLASSES], kept), near);

                if (!modulo_sound(x, y, kept, near))
                {
                        if (failures < 8)
                        {
                                modulo_say("FAIL x ");
                                modulo_say_hex(x);
                                modulo_say(" y ");
                                modulo_say_hex(y);
                                modulo_say(" fmod ");
                                modulo_say_hex(kept);
                                modulo_say(" remainder ");
                                modulo_say_hex(near);
                                modulo_say("\n");
                        }
                        failures++;
                }
        }

        //      The FAIL lines above are the lane's to show; they come before
        //      the digests only when something is already wrong.
        modulo_say("pairs ");
        modulo_say_number(pairs);
        modulo_say("\n");

        for (b32 class = 0; class < MODULO_CLASSES; class++)
        {
                modulo_say("class ");
                modulo_say_number((positive)class);
                modulo_say(" ");
                modulo_say_hex(digest[class]);
                modulo_say("\n");
        }

        modulo_say("shared-end\n");
        modulo_say("\n");
        modulo_say_number(pairs);
        modulo_say(" checks, ");
        modulo_say_number(failures);
        modulo_say(" failures\n");
        modulo_flush();

        return failures ? 1 : 0;
}
#endif /* CHECK_modulo */

#ifdef CHECK_train
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
#endif
#ifdef CHECK_native_train
#include <stdio.h>
#include <sys/mman.h>
#include "train.h"
#endif

#if defined(CHECK_train) || defined(CHECK_native_train)
/*
        lib.c's training and inference block against references written here.

        Two programs are built from this: CHECK_train, freestanding on the
        library, which test/run's train lane runs on all three machines --
        x86_64 once for each of its bodies with the feature bytes walked down,
        riscv64 under qemu with no V and at VLEN 128, 256 and 512 -- and
        CHECK_native_train, which links the arm64 bodies lifted out of lib.c
        and runs them on an arm64 Mac (its main is with the other native
        sections). Everything between here and the first main is what both
        share, written against nothing but the compiler's builtins and the
        machine's own scalar instructions.

        Every routine is walked over every count to 100 and the training sizes,
        each array ending against a page that faults or a few floats short of
        it, so that where a start falls in its line moves apart from the count,
        with sentinels in front and in the gap behind; every in-place call the
        routine allows; and its edges.

        fused_residual_add_f32 is held to the bit: the bodies with a fused
        multiply-add to this machine's scalar fma, SSE2 to the product rounded
        and then the sum. A NaN is held to being a NaN, its payload being the
        machine's. The same answers are held to the bound the header states
        against a double sum, k = 1 fused and 2 for SSE2, and small integers to
        the exact answer.
*/
#define TRAIN_MOST 2097152
#define TRAIN_SENTINEL 0x5a5aa5a5u

typedef void train_residual_call(float *, const float *, unsigned long long, float);

static unsigned long long train_state = 0x6a09e667f3bcc909ull;

static unsigned long long train_next(void)
{
        XORSHIFT64(train_state);
        return train_state;
}

static float train_float(unsigned bits)
{
        float value;

        __builtin_memcpy(&value, &bits, 4);
        return value;
}

static unsigned train_bits(float value)
{
        unsigned bits;

        __builtin_memcpy(&bits, &value, 4);
        return bits;
}

//      One fused multiply-add, the machine's own scalar instruction: a * b + c.
static float train_fma(float a, float b, float c)
{
#if defined(__x86_64__)
        __asm__("vfmadd231ss %2, %1, %0" : "+x"(c) : "x"(a), "x"(b));
        return c;
#elif defined(__aarch64__)
        float r;

        __asm__("fmadd %s0, %s1, %s2, %s3" : "=w"(r) : "w"(a), "w"(b), "w"(c));
        return r;
#else
        float r;

        __asm__("fmadd.s %0, %1, %2, %3" : "=f"(r) : "f"(a), "f"(b), "f"(c));
        return r;
#endif
}

/*
        A float to test with: mostly a random sign and mantissa with an
        exponent within `scale` binades of one, and one in sixteen a value no
        routine may get wrong -- the zeros, the infinities, NaNs with payloads,
        the denormals, the largest and smallest normals.
*/
static float train_value(unsigned scale)
{
        static const unsigned special[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc12345,
                                           0x7f800001, 0x00000001, 0x807fffff, 0x7f7fffff, 0xff7fffff, 0x00800000,
                                           0x80800000, 0x3f800000, 0x00400000, 0x7fbfffff};
        unsigned long long r = train_next();

        if ((r & 15) == 0)
                return train_float(special[(r >> 4) & 15]);
        unsigned exponent = 127 + (unsigned)((r >> 8) % (2 * scale + 1)) - scale;

        return train_float((unsigned)(r >> 63) << 31 | exponent << 23 | ((unsigned)(r >> 32) & 0x7fffff));
}

//      Every count to 100, the vector edges past it, the trainer's sizes.
static const unsigned long long train_long[] = {127, 128, 129, 255, 256, 257, 682, 1000, 1023, 1024, 1025,
                                                4095, 4096, 4097, 65539, 2097152};
#define TRAIN_COUNTS (101 + sizeof(train_long) / sizeof(train_long[0]))

static unsigned long long train_count(unsigned long long at)
{
        return at <= 100 ? at : train_long[at - 101];
}

/*
        Where the arrays sit: each ends this many floats before its guard page,
        so the starts' alignments move apart from the counts' and the floats in
        the gap stand guard too. The first is both against the page; the counts
        past 4097 take only the first two.
*/
static const unsigned train_gaps[][2] = {{0, 0}, {0, 3}, {1, 1}, {5, 0}, {3, 7}, {9, 9}, {15, 4}, {2, 13}};
#define TRAIN_PLACEMENTS (sizeof(train_gaps) / sizeof(train_gaps[0]))

static unsigned long long train_placements(unsigned long long count)
{
        return count > 4097 ? 2 : TRAIN_PLACEMENTS;
}

static void train_fence(unsigned *start, unsigned long long count, unsigned gap, unsigned mark)
{
        for (int i = 0; i < 16; i++)
                start[i - 16] = mark + (unsigned)i;
        for (unsigned i = 0; i < gap; i++)
                start[count + i] = mark - i;
}

static int train_fenced(const unsigned *start, unsigned long long count, unsigned gap, unsigned mark)
{
        for (int i = 0; i < 16; i++)
                if (start[i - 16] != mark + (unsigned)i)
                        return 0;
        for (unsigned i = 0; i < gap; i++)
                if (start[count + i] != mark - i)
                        return 0;
        return 1;
}

//      The faults a walk found, by kind.
typedef struct
{
        unsigned long long values, bounded, outside, kept, aliased, integers;
} train_faults;

static double train_magnitude(double value)
{
        return value < 0 ? -value : value;
}

static int train_same(float got, float want)
{
        return want != want ? got != got : train_bits(got) == train_bits(want);
}

/*
        fused_residual_add_f32 through one body. fused says whether this body
        rounds once.
*/
static void train_residual_cases(train_residual_call *routine, float *x_end, float *h_end, int fused,
                                 train_faults *faults)
{
        static const unsigned alphas[] = {0x3f400000, 0xb99d4952, 0x3f800000, 0x00000000, 0x80000000, 0x7e61dabc,
                                          0x7fc00000, 0x7f800000};
        static float was[TRAIN_MOST], from[TRAIN_MOST];

        for (unsigned long long at = 0; at < TRAIN_COUNTS; at++)
                for (unsigned long long place = 0; place < train_placements(train_count(at)); place++)
                {
                        unsigned long long count = train_count(at);
                        float alpha = train_float(alphas[(at + place) % 8]);
                        unsigned x_gap = train_gaps[place][0], h_gap = train_gaps[place][1];
                        float *x = x_end - count - x_gap, *h = h_end - count - h_gap;

                        train_fence((unsigned *)x, count, x_gap, TRAIN_SENTINEL);
                        train_fence((unsigned *)h, count, h_gap, ~TRAIN_SENTINEL);
                        for (unsigned long long i = 0; i < count; i++)
                        {
                                was[i] = x[i] = train_value(at & 1 ? 3 : 20);
                                from[i] = h[i] = train_value(at & 2 ? 3 : 20);
                        }
                        routine(x, h, count, alpha);
                        for (unsigned long long i = 0; i < count; i++)
                        {
                                float product = alpha * from[i];
                                float want = fused ? train_fma(alpha, from[i], was[i]) : product + was[i];
                                double sum = (double)alpha * (double)from[i] + (double)was[i];
                                double scale = train_magnitude((double)was[i]) + train_magnitude((double)alpha * (double)from[i]);

                                faults->values += !train_same(x[i], want);
                                faults->kept += train_bits(h[i]) != train_bits(from[i]);
                                //      Where the answer and, unfused, the rounded product are
                                //      finite; the 1e-6 is the double sum's own rounding.
                                if (want == want && train_magnitude(sum) <= 3.4028234663852886e38 && scale < 1e300 &&
                                    (fused || train_magnitude((double)alpha * (double)from[i]) <= 3.4028234663852886e38))
                                        faults->bounded += train_magnitude((double)x[i] - sum) >
                                                           ((fused ? 1 : 2) + 1e-6) * 0x1p-24 * scale + 1e-35;
                        }
                        faults->outside += !train_fenced((unsigned *)x, count, x_gap, TRAIN_SENTINEL) +
                                           !train_fenced((unsigned *)h, count, h_gap, ~TRAIN_SENTINEL);

                        //      x as h: x * (1 + alpha), one rounding where fused.
                        if ((place & 1) == 0 && count <= 4097)
                        {
                                for (unsigned long long i = 0; i < count; i++)
                                        was[i] = x[i] = train_value(10);
                                routine(x, x, count, alpha);
                                for (unsigned long long i = 0; i < count; i++)
                                {
                                        float product = alpha * was[i];
                                        float want = fused ? train_fma(alpha, was[i], was[i]) : product + was[i];

                                        faults->aliased += !train_same(x[i], want);
                                }
                                faults->outside += !train_fenced((unsigned *)x, count, x_gap, TRAIN_SENTINEL);
                        }

                        //      Small integers and a power of two: every product and sum exact.
                        if ((place & 1) == 1 && count <= 4097)
                        {
                                for (unsigned long long i = 0; i < count; i++)
                                {
                                        x[i] = (float)(int)(train_next() % 2001) - 1000;
                                        h[i] = (float)(int)(train_next() % 2001) - 1000;
                                        was[i] = x[i] + 0.25f * h[i];
                                }
                                routine(x, h, count, 0.25f);
                                for (unsigned long long i = 0; i < count; i++)
                                        faults->integers += x[i] != was[i];
                        }
                }
}

typedef void train_to_bf16_call(unsigned short *, const float *, unsigned long long);
typedef void train_from_bf16_call(float *, const unsigned short *, unsigned long long);

//      f32_to_bf16's definition, which is VCVTNEPS2BF16's.
static unsigned short train_bf16(unsigned x)
{
        if ((x & 0x7f800000u) == 0)
                return (unsigned short)(x >> 16 & 0x8000u);
        if ((x & 0x7fffffffu) > 0x7f800000u)
                return (unsigned short)(x >> 16 | 0x40u);
        return (unsigned short)((x + 0x7fffu + (x >> 16 & 1u)) >> 16);
}

/*
        A float's bits to convert: one of every sign and exponent with a low
        23 bits at and around the rounding edges (the low half at 0x7fff,
        0x8000, 0x8001 with the bit above it 0 and 1), the carries into the
        exponent and the NaN payloads above and below the half, or any bits.
*/
static const unsigned train_bf16_lows[] = {0x000000, 0x000001, 0x007fff, 0x008000, 0x008001, 0x00ffff, 0x010000, 0x017fff,
                                           0x018000, 0x018001, 0x7fffff, 0x7f8000, 0x7f7fff, 0x400000, 0x3fffff, 0x408000};

static unsigned train_bf16_input(unsigned long long draw)
{
        if (draw % 8 == 7)
                return (unsigned)(draw >> 32);
        return (unsigned)(draw >> 8) % 512 << 23 | train_bf16_lows[(draw >> 20) % 16];
}

static void train_fence16(unsigned short *start, unsigned long long count, unsigned gap, unsigned short mark)
{
        for (int i = 0; i < 16; i++)
                start[i - 16] = (unsigned short)(mark + i);
        for (unsigned i = 0; i < gap; i++)
                start[count + i] = (unsigned short)(mark - i);
}

static int train_fenced16(const unsigned short *start, unsigned long long count, unsigned gap, unsigned short mark)
{
        for (int i = 0; i < 16; i++)
                if (start[i - 16] != (unsigned short)(mark + i))
                        return 0;
        for (unsigned i = 0; i < gap; i++)
                if (start[count + i] != (unsigned short)(mark - i))
                        return 0;
        return 1;
}

//      The faults the bf16 walks found, by kind.
typedef struct
{
        unsigned long long narrowed, widened, kept, outside, strata, every;
} train_bf16_faults;

/*
        f32_to_bf16 and bf16_to_f32 through one body, each answer the
        definition to the bit: every count and placement over drawn inputs,
        then every stratum of train_bf16_input in order and all 65536 bf16
        values in one call each.
*/
static void train_bf16_cases(train_to_bf16_call *to, train_from_bf16_call *from, float *f_end, unsigned short *h_end,
                             train_bf16_faults *faults)
{
        static unsigned kept[TRAIN_MOST];
        static unsigned short half[TRAIN_MOST];

        for (unsigned long long at = 0; at < TRAIN_COUNTS; at++)
                for (unsigned long long place = 0; place < train_placements(train_count(at)); place++)
                {
                        unsigned long long count = train_count(at);
                        unsigned f_gap = train_gaps[place][0], h_gap = train_gaps[place][1];
                        float *f = f_end - count - f_gap;
                        unsigned short *h = h_end - count - h_gap;

                        train_fence((unsigned *)f, count, f_gap, TRAIN_SENTINEL);
                        train_fence16(h, count, h_gap, 0xa55a);
                        for (unsigned long long i = 0; i < count; i++)
                                ((unsigned *)f)[i] = kept[i] = train_bf16_input(train_next());
                        to(h, f, count);
                        for (unsigned long long i = 0; i < count; i++)
                        {
                                faults->narrowed += h[i] != train_bf16(kept[i]);
                                faults->kept += ((unsigned *)f)[i] != kept[i];
                        }
                        faults->outside += !train_fenced((unsigned *)f, count, f_gap, TRAIN_SENTINEL) +
                                           !train_fenced16(h, count, h_gap, 0xa55a);

                        //      And back: any halfword at all, each the high half of its float.
                        train_fence((unsigned *)f, count, f_gap, TRAIN_SENTINEL);
                        for (unsigned long long i = 0; i < count; i++)
                                h[i] = half[i] = (unsigned short)train_next();
                        from(f, h, count);
                        for (unsigned long long i = 0; i < count; i++)
                        {
                                faults->widened += ((unsigned *)f)[i] != (unsigned)half[i] << 16;
                                faults->kept += h[i] != half[i];
                        }
                        faults->outside += !train_fenced((unsigned *)f, count, f_gap, TRAIN_SENTINEL) +
                                           !train_fenced16(h, count, h_gap, 0xa55a);
                }

        //      Every stratum in order, 8192 a call.
        float *f = f_end - 8192;
        unsigned short *h = h_end - 8192;

        for (unsigned long long i = 0; i < 8192; i++)
                ((unsigned *)f)[i] = kept[i] = (unsigned)i / 16 % 512 << 23 | train_bf16_lows[i % 16];
        to(h, f, 8192);
        for (unsigned long long i = 0; i < 8192; i++)
                faults->strata += h[i] != train_bf16(kept[i]);

        //      Every bf16 value, widened in one call.
        f = f_end - 65536, h = h_end - 65536;
        for (unsigned long long i = 0; i < 65536; i++)
                h[i] = (unsigned short)i;
        from(f, h, 65536);
        for (unsigned long long i = 0; i < 65536; i++)
                faults->every += ((unsigned *)f)[i] != (unsigned)i << 16;
}

typedef unsigned long long train_argmax_call(const float *, unsigned long long);

//      The first NaN, or else the first index of the maximum, ties and zeros
//      of either sign compared as equal.
static unsigned long long train_argmax_reference(const float *x, unsigned long long n)
{
        unsigned long long best = 0;

        for (unsigned long long i = 0; i < n; i++)
                if (x[i] != x[i])
                        return i;
        for (unsigned long long i = 1; i < n; i++)
                if (x[i] > x[best])
                        best = i;
        return best;
}

typedef struct
{
        unsigned long long random, planted, ties, zeros, nans, infinities;
} train_argmax_faults;

//      A logit with no NaN and nothing above 32: mostly a random float within
//      a few binades of one, and one in sixteen a zero, a denormal, minus
//      infinity or the most negative float.
static float train_logit(void)
{
        static const unsigned special[] = {0x00000000, 0x80000000, 0xff800000, 0x00000001,
                                           0x807fffff, 0xff7fffff, 0x00800000, 0xc1800000};
        unsigned long long r = train_next();

        if ((r & 15) == 0)
                return train_float(special[(r >> 4) & 7]);
        return train_float((unsigned)(r >> 63) << 31 | (unsigned)(124 + (r >> 8) % 7) << 23 | ((unsigned)(r >> 32) & 0x7fffff));
}

//      Where a maximum or a NaN is put: every place up to 64, then the ends,
//      the lanes and blocks around them, and a few at random.
static unsigned long long train_argmax_place(unsigned long long n, unsigned long long k)
{
        static const unsigned long long fixed[] = {0, 1, 7, 8, 15, 16, 17, 31, 32, 63, 64, 65, 127, 128, 255, 256, 257, 511, 512};

        if (n <= 64)
                return k;
        if (k < sizeof(fixed) / sizeof(fixed[0]))
                return fixed[k] < n ? fixed[k] : n - 1 - k % 3;
        if (k < sizeof(fixed) / sizeof(fixed[0]) + 4)
                return n - 1 - (k - sizeof(fixed) / sizeof(fixed[0])) * 15;
        return train_next() % n;
}

static unsigned long long train_argmax_places(unsigned long long n)
{
        return n <= 64 ? n : n > 4097 ? 8 : 32;
}

/*
        fast_argmax_f32 through one body: at every count and placement, and
        with the array starting just after a page that faults, random logits,
        a maximum planted at each place, two copies of it (the first wins),
        minus and plus zero as the maximum, a NaN at each place after a larger
        finite value and at each of two places, and infinities.
*/
static void train_argmax_cases(train_argmax_call *routine, float *x_end, float *front, train_argmax_faults *faults)
{
        for (unsigned long long at = 0; at < TRAIN_COUNTS + 4; at++)
                for (unsigned long long place = 0; place <= TRAIN_PLACEMENTS; place++)
                {
                        static const unsigned long long vocabularies[] = {2048, 32000, 50257, 128256};
                        unsigned long long n = at < TRAIN_COUNTS ? train_count(at) : vocabularies[at - TRAIN_COUNTS];
                        float *x = place == TRAIN_PLACEMENTS ? front : x_end - n - train_gaps[place][0];

                        //      Past the largest vocabulary nothing is new.
                        if (n > 128256 || (place != TRAIN_PLACEMENTS && place >= train_placements(n)))
                                continue;
                        for (unsigned long long i = 0; i < n; i++)
                                x[i] = train_value(3);
                        faults->random += routine(x, n) != train_argmax_reference(x, n);
                        if (n == 0)
                                continue;
                        for (unsigned long long i = 0; i < n; i++)
                                x[i] = train_logit();
                        for (unsigned long long k = 0; k < train_argmax_places(n); k++)
                        {
                                unsigned long long p = train_argmax_place(n, k), q = train_next() % n;
                                float was = x[p], other = x[q];

                                x[p] = 3.0e30f;
                                faults->planted += routine(x, n) != p;
                                x[q] = 3.0e30f;
                                faults->ties += routine(x, n) != (p < q ? p : q);
                                x[q] = other, x[p] = was;

                                //      A NaN after a larger finite value still wins.
                                x[p] = train_float(0x7fc00000u | (unsigned)(train_next() & 0x3fffff) | (unsigned)(k & 1) << 31);
                                if (p > 0)
                                        x[q % p] = 3.0e38f;
                                faults->nans += routine(x, n) != p;
                                x[q] = train_float(0x7f800001u);
                                faults->nans += routine(x, n) != (p < q ? p : q);
                                for (unsigned long long i = 0; i < n; i++)
                                        if (x[i] != x[i] || x[i] == 3.0e38f)
                                                x[i] = train_logit();
                        }
                        //      Zeros of both signs as the maximum, first of either.
                        {
                                unsigned long long p = train_next() % n, q = train_next() % n;

                                for (unsigned long long i = 0; i < n; i++)
                                        x[i] = -1.0f - (float)(i % 7);
                                x[p] = -0.0f, x[q] = 0.0f;
                                faults->zeros += routine(x, n) != (p < q ? p : q);
                                for (unsigned long long i = 0; i < n; i++)
                                        x[i] = i & 1 ? -0.0f : 0.0f;
                                faults->zeros += routine(x, n) != 0;
                        }
                        //      Infinities: all of minus, one finite among them, plus one.
                        {
                                unsigned long long p = train_next() % n, q = train_next() % n;

                                for (unsigned long long i = 0; i < n; i++)
                                        x[i] = train_float(0xff800000u);
                                faults->infinities += routine(x, n) != 0;
                                x[p] = -3.0e38f;
                                faults->infinities += routine(x, n) != p;
                                x[q] = train_float(0x7f800000u);
                                faults->infinities += routine(x, n) != q;
                                for (unsigned long long i = 0; i < n; i++)
                                        x[i] = train_float(0x7fc00000u);
                                faults->infinities += routine(x, n) != 0;
                        }
                }
}
#endif /* CHECK_train || CHECK_native_train */

#ifdef CHECK_train
static float *train_x_end, *train_h_end, *train_front;

//      Two runs of TRAIN_MOST floats and room for the gaps, each followed by
//      a page that faults.
static p8 train_guard(void)
{
        positive span = ((TRAIN_MOST + 64) * 4 + 4095) / 4096 * 4096;
        float *ends[2];

        for (int at = 0; at < 2; at++)
        {
                p8 address_to pages = memory(span + 4096);

                if (!pages || system_call_3(syscall(mprotect), (positive)(pages + span), 4096, 0) != 0)
                        return false;
                ends[at] = (float *)(pages + span);
        }
        train_x_end = ends[0], train_h_end = ends[1];

        //      And one whose start is just after a page that faults.
        p8 address_to pages = memory(span + 2 * 4096);

        if (!pages)
                return false;
        p8 address_to start = (p8 address_to)(((positive)pages + 4095) & ~(positive)4095) + 4096;
        if (system_call_3(syscall(mprotect), (positive)(start - 4096), 4096, 0) != 0)
                return false;
        train_front = (float *)start;
        return true;
}

// Every call the first in a process, on riscv64's first tier.
static p8 train_first;

static fn train_call_residual(float *x, const float *h, unsigned long long count, float alpha)
{
        VECTOR_FIRST_CALL(train_first);
        fused_residual_add_f32(x, h, count, alpha);
}

static fn train_call_to_bf16(unsigned short *to, const float *from, unsigned long long count)
{
        VECTOR_FIRST_CALL(train_first);
        f32_to_bf16(to, from, count);
}

static fn train_call_from_bf16(float *to, const unsigned short *from, unsigned long long count)
{
        VECTOR_FIRST_CALL(train_first);
        bf16_to_f32(to, from, count);
}

static unsigned long long train_call_argmax(const float *logits, unsigned long long length)
{
        VECTOR_FIRST_CALL(train_first);
        return fast_argmax_f32(logits, length);
}

#if X64
//      The instruction the definition is: one float through vcvtneps2bf16.
static unsigned short train_bf16_instruction(unsigned x)
{
        unsigned answer;

        __asm__("vmovd %1, %%xmm0\n   vcvtneps2bf16 %%xmm0, %%xmm0\n   vmovd %%xmm0, %0" : "=r"(answer) : "r"(x) : "xmm0");
        return (unsigned short)answer;
}
#endif

//      One machine state's properties, each a check.
//      Which walks a machine state runs: the bodies it reaches that an
//      earlier state has not.
#define TRAIN_RESIDUAL 1
#define TRAIN_BF16 2
#define TRAIN_ARGMAX 4

static fn train_tier(string_address tier, p8 fused, p32 walks)
{
        train_faults faults = {0, 0, 0, 0, 0, 0};
        train_bf16_faults bf16 = {0, 0, 0, 0, 0, 0};
        train_argmax_faults argmax = {0, 0, 0, 0, 0, 0};

        string_format(log, "  %s\n", tier);
        if (walks & TRAIN_RESIDUAL)
        {
                train_residual_cases(train_call_residual, train_x_end, train_h_end, fused, &faults);
                check("fused_residual_add_f32: every count and placement, each element the reference to the bit", faults.values == 0);
                check("fused_residual_add_f32: within k eps of the double sum (k = 1 fused, 2 for SSE2)", faults.bounded == 0);
                check("fused_residual_add_f32: sentinels and gaps beside every array untouched", faults.outside == 0);
                check("fused_residual_add_f32: h unchanged", faults.kept == 0);
                check("fused_residual_add_f32: x as h", faults.aliased == 0);
                check("fused_residual_add_f32: small integers exactly", faults.integers == 0);
        }
        if (walks & TRAIN_BF16)
        {
                train_bf16_cases(train_call_to_bf16, train_call_from_bf16, train_x_end, (unsigned short *)train_h_end, &bf16);
                check("f32_to_bf16: every count and placement, each the definition to the bit", bf16.narrowed == 0);
                check("bf16_to_f32: every count and placement, each the high half of its float", bf16.widened == 0);
                check("f32_to_bf16, bf16_to_f32: sources unchanged", bf16.kept == 0);
                check("f32_to_bf16, bf16_to_f32: sentinels and gaps beside every array untouched", bf16.outside == 0);
                check("f32_to_bf16: every sign and exponent at every rounding and NaN edge", bf16.strata == 0);
                check("bf16_to_f32: all 65536 halfwords", bf16.every == 0);
        }
        if (walks & TRAIN_ARGMAX)
        {
                train_argmax_cases(train_call_argmax, train_x_end, train_front, &argmax);
                check("fast_argmax_f32: random logits, every count and placement, the reference's index", argmax.random == 0);
                check("fast_argmax_f32: a maximum at each place", argmax.planted == 0);
                check("fast_argmax_f32: two of the maximum, the first", argmax.ties == 0);
                check("fast_argmax_f32: zeros of both signs, the first of either", argmax.zeros == 0);
                check("fast_argmax_f32: a NaN wins, after a larger value too, and the first of two", argmax.nans == 0);
                check("fast_argmax_f32: infinities and all NaN", argmax.infinities == 0);
        }
}

b32 main(void)
{
        check("train: guard pages", train_guard());
        if (!train_h_end)
                return test_report(null);
#if X64
        //      The first call asks for AVX512_BF16 and keeps its arguments.
        {
                float in[3] = {1.0f, -2.5f, 3.0e38f};
                unsigned short out[3] = {0, 0, 0};

                cpu_has_bf16 = 0;
                f32_to_bf16(out, in, 3);
                //      Only a processor with AVX-512 gets as far as asking for
                //      BF16: the AVX2 and SSE2 bodies are taken first.
                check("f32_to_bf16: the first call asks and answers", (cpu_has_bf16 != 0 || !cpu_has_avx512) && out[0] == 0x3f80 &&
                                                                      out[1] == 0xc020 && out[2] == train_bf16(train_bits(in[2])));
        }
        p8 had512 = cpu_has_avx512, had2 = cpu_has_avx2, hadfma = cpu_has_fma, had16 = cpu_has_bf16;

        //      The definition against the instruction, where there is one: every
        //      stratum and a stride through all 2^32.
        if (had512 && had16 == 2)
        {
                unsigned long long differ = 0;

                for (unsigned long long i = 0; i < 8192; i++)
                {
                        unsigned x = (unsigned)i / 16 % 512 << 23 | train_bf16_lows[i % 16];

                        differ += train_bf16_instruction(x) != train_bf16(x);
                }
                for (unsigned long long x = 0; x < 0x100000000ull; x += 4093)
                        differ += train_bf16_instruction((unsigned)x) != train_bf16((unsigned)x);
                check("f32_to_bf16: the definition is vcvtneps2bf16", differ == 0);
        }
        //      AVX-512 with BF16 and without, AVX2 with FMA, AVX2 alone (SSE2 for
        //      the residual add), FMA alone and nothing (SSE2 for both).
        static const p8 tiers[][4] = {{1, 1, 1, 2}, {1, 1, 1, 1}, {0, 1, 1, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {0, 0, 0, 1}};
        static const string_address names[] = {"avx512 bf16", "avx512", "avx2 fma", "avx2 (sse2 residual)", "sse2 (no avx2)", "sse2"};

        for (positive tier = 0; tier < 6; tier++)
        {
                if ((tiers[tier][0] && !had512) || (tiers[tier][3] == 2 && had16 != 2))
                        continue;
                cpu_has_avx512 = tiers[tier][0] & had512;
                cpu_has_avx2 = tiers[tier][1] & had2;
                cpu_has_fma = tiers[tier][2] & hadfma;
                cpu_has_bf16 = tiers[tier][3];
                train_tier(names[tier], cpu_has_avx2 && cpu_has_fma, TRAIN_RESIDUAL | TRAIN_BF16 | TRAIN_ARGMAX);
        }
        cpu_has_avx512 = had512, cpu_has_avx2 = had2, cpu_has_fma = hadfma, cpu_has_bf16 = had16;
#elif RISCV64
        //      The first call asks for V and keeps its arguments through the
        //      question.
        cpu_has_vector = 0;
        {
                f32 one = 1;

                fused_residual_add_f32(&one, &one, 1, 1);
                check("fused_residual_add_f32: the first call asks for V and keeps its arguments", cpu_has_vector != 0 && one == 2);
        }
        p8 vector = cpu_has_vector;

        //      Every call of the first tier the first in a process, which
        //      asks for V and comes back to the routine's entry: with V where
        //      the machine has it and the scalar bodies where it has not.
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                train_first = tier == 0;
                train_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first", true,
                           TRAIN_RESIDUAL | TRAIN_BF16 | TRAIN_ARGMAX);
        }
        train_first = false;
        cpu_has_vector = vector;
#else
        //      The first call asks for FEAT_BF16; then each of the two bodies.
        {
                float in[2] = {1.0f, -2.5f};
                unsigned short out[2] = {0, 0};

                cpu_has_bf16 = 0;
                f32_to_bf16(out, in, 2);
                check("f32_to_bf16: the first call asks and answers", cpu_has_bf16 != 0 && out[0] == 0x3f80 && out[1] == 0xc020);
        }
        p8 had16 = cpu_has_bf16;

        for (p32 tier = 0; tier < 2; tier++)
        {
                if (tier == 0 && had16 != 2)
                        continue;
                cpu_has_bf16 = tier ? 1 : 2;
                //      The second state differs in f32_to_bf16's body alone.
                train_tier(cpu_has_bf16 == 2 ? "neon bf16" : "neon", true,
                           tier == 0 || had16 != 2 ? TRAIN_RESIDUAL | TRAIN_BF16 | TRAIN_ARGMAX : TRAIN_BF16);
        }
        cpu_has_bf16 = had16;
#endif
        return test_report(null);
}
#endif /* CHECK_train */

#if defined(CHECK_ranged) || defined(CHECK_native_ranged)
#ifdef CHECK_ranged
//      The checked build, always: what a broken promise does under it is
//      part of what this section proves, and the entries are called by
//      their own names below, around the macros, for the plain bodies.
#define MOONWATER_RANGES_CHECKED
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
typedef address_any ranged_copy_call(address_any, const address_any, positive);
#else
#include <stdio.h>
#include <sys/mman.h>
#include "ranged.h"
void *memory_copy_apart__room_64(void *, const void *, unsigned long long);
void *memory_copy_apart(void *, const void *, unsigned long long);
unsigned long long string_length(const char *);
unsigned long long memory_encode_power2(void *, const void *, unsigned long long, const char *, unsigned long long);
unsigned long long memory_encode_power2_lines(void *, const void *, unsigned long long, const char *, unsigned long long,
                                              unsigned long long);
unsigned long long memory_cut_fields__room_64(unsigned char *, unsigned long long *, const unsigned char *,
                                              unsigned long long, const unsigned char *, unsigned long long);
unsigned long long memory_reverse_lines__room_64(unsigned char *, unsigned long long *, const unsigned char *,
                                                 unsigned long long, unsigned long long);
unsigned long long memory_fold_lines__room_64(unsigned char *, unsigned long long *, const unsigned char *,
                                              unsigned long long, unsigned long long, unsigned long long);
unsigned long long memory_unique_lines__room_64(unsigned char *, unsigned long long *, const unsigned char *,
                                                unsigned long long, unsigned long long *, unsigned long long);
unsigned long long memory_paste_lines__room_64(unsigned char *, unsigned long long *, const unsigned char *,
                                               unsigned long long, const unsigned char *, unsigned long long,
                                               unsigned long long);
typedef void *ranged_copy_call(void *, const void *, unsigned long long);
#endif

// Every call the first in a process, on riscv64's first tier.
static int ranged_first;

/*
        lib.c's call-site entries against what their names promise.

        Two programs are built from this: CHECK_ranged, freestanding on the
        library, which test/run's ranged lane runs on all three machines --
        x86_64 once for each body with the feature bytes walked down, riscv64
        under qemu with no V and at VLEN 128 to 1024 -- and
        CHECK_native_ranged, which links the arm64 bodies lifted out of lib.c
        and runs them on an arm64 Mac. What is between here and the first main
        is what both share.

        memory_copy_apart__room_64 is walked over every count to 300 and the
        long ones past it, with each run's room ending against a page that
        faults or a few bytes short of it, so that no load or store past the
        room goes unseen; the bytes before each start and after each room
        stand guard and must come back as they were, and so must the source.
        Then both runs start just after a page that faults.
*/
static unsigned long long ranged_state = 0xbb67ae8584caa73bull;

static unsigned char ranged_byte(void)
{
        XORSHIFT64(ranged_state);
        return (unsigned char)(ranged_state >> 32);
}

static const unsigned long long ranged_long[] = {511, 512, 513, 1000, 4095, 4096, 4097, 65539};
#define RANGED_COUNTS (301 + sizeof(ranged_long) / sizeof(ranged_long[0]))
#define RANGED_MOST 65539
#define RANGED_SPAN (RANGED_MOST + 64 + 128 + 16)

static unsigned long long ranged_count(unsigned long long at)
{
        return at <= 300 ? at : ranged_long[at - 301];
}

//      How far short of its page each run's room ends: destination, source.
static const unsigned ranged_gaps[][2] = {{0, 0}, {0, 5}, {3, 0}, {17, 9}, {63, 1}, {64, 64}, {1, 100}};
#define RANGED_PLACEMENTS (sizeof(ranged_gaps) / sizeof(ranged_gaps[0]))

typedef struct
{
        unsigned long long copied, answer, outside, source, front;
} ranged_copy_faults;

static unsigned char ranged_saved[RANGED_SPAN];

static int ranged_same(const unsigned char *one, const unsigned char *two, unsigned long long count)
{
        for (unsigned long long i = 0; i < count; i++)
                if (one[i] != two[i])
                        return 0;
        return 1;
}

static void ranged_copy_cases(unsigned char *d_end, unsigned char *s_end, unsigned char *d_front,
                              unsigned char *s_front, ranged_copy_faults *faults)
{
        for (unsigned long long at = 0; at < RANGED_COUNTS; at++)
        {
                unsigned long long n = ranged_count(at);

                for (unsigned long long p = 0; p < RANGED_PLACEMENTS; p++)
                {
                        unsigned dg = ranged_gaps[p][0], sg = ranged_gaps[p][1];
                        unsigned char *d = d_end - dg - 64 - n, *s = s_end - sg - 64 - n;
                        long long source_span = (long long)(n + 64 + sg), into_span = (long long)(n + 64 + dg);

                        for (long long i = -16; i < source_span; i++)
                                ranged_saved[i + 16] = s[i] = ranged_byte();
                        for (long long i = -16; i < into_span; i++)
                                d[i] = (unsigned char)(0x3c ^ i);
                        //      In brackets, the routine and not lib.util.c's macro of it.
                        VECTOR_FIRST_CALL(ranged_first);
                        faults->answer += (memory_copy_apart__room_64)(d, s, n) != d;
                        faults->copied += !ranged_same(d, s, n);
                        for (long long i = -16; i < 0; i++)
                                faults->outside += d[i] != (unsigned char)(0x3c ^ i);
                        for (long long i = (long long)n + 64; i < into_span; i++)
                                faults->outside += d[i] != (unsigned char)(0x3c ^ i);
                        faults->source += !ranged_same(s - 16, ranged_saved, (unsigned long long)source_span + 16);
                }
                if (n + 64 <= 16384)
                {
                        for (unsigned long long i = 0; i < n + 64; i++)
                                s_front[i] = ranged_byte(), d_front[i] = 0;
                        VECTOR_FIRST_CALL(ranged_first);
                        faults->front += (memory_copy_apart__room_64)(d_front, s_front, n) != d_front ||
                                         !ranged_same(d_front, s_front, n);
                }
        }
}

/*
        string_length's short window reads thirty two bytes where the string
        starts, and only when they cannot leave its page; past that the scan.
        Every length to 100 starting at each of the last 96 bytes before a
        page that faults, so the window's page test is walked across, with
        zeros and non-zeros in front of the start that must not count; then
        every length to 300 at every alignment in the middle of a page, the
        bytes after the terminator zeros and not.
*/
static unsigned long long ranged_length_cases(unsigned char *page_end)
{
        unsigned long long faults = 0;

        for (unsigned long long back = 1; back <= 96; back++)
                for (unsigned long long n = 0; n < back && n <= 100; n++)
                {
                        unsigned char *start = page_end - back;

                        for (unsigned long long i = 0; i < 64; i++)
                                start[(long long)i - 64] = (unsigned char)(i & 1 ? 0 : 'x');
                        for (unsigned long long i = 0; i < n; i++)
                                start[i] = (unsigned char)(1 + ranged_byte() % 255);
                        start[n] = 0;
                        for (unsigned long long i = n + 1; i < back; i++)
                                start[i] = (unsigned char)(i & 3 ? 'y' : 0);
                        //      In brackets, the routine and not lib.util.c's macro of it.
                        faults += (string_length)((const char *)start) != n;
                }
        for (unsigned long long align = 0; align < 64; align++)
                for (unsigned long long n = 0; n <= 300; n++)
                {
                        unsigned char *start = page_end - 4096 + 1024 + align;

                        for (unsigned long long i = 0; i < n; i++)
                                start[i] = (unsigned char)(1 + ranged_byte() % 255);
                        start[n] = 0;
                        for (unsigned long long i = n + 1; i < n + 40; i++)
                                start[i] = (unsigned char)(i & 1 ? 0 : 'z');
                        faults += (string_length)((const char *)start) != n;
                }
        return faults;
}

/*
        memory_encode_power2_lines against memory_encode_power2 into a buffer
        of its own with the newlines put in afterwards: every bits value
        (base64, base32, hex, binary both ways), groups to 100 and long ones,
        lines of one to seventy seven groups, the destination ending exactly
        at a page that faults with sixteen guard bytes in front, the source
        ending at one too, and the answer the bytes written.
*/
static unsigned char ranged_reference[8192 * 9];

static unsigned long long ranged_lines_cases(unsigned char *d_end, unsigned char *s_end)
{
        static const char *alphabets[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
                                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", "0123456789abcdef", "01", "01"};
        static const unsigned long long bits_of[] = {6, 5, 4, 1, 9}, in_of[] = {3, 5, 1, 1, 1}, out_of[] = {4, 8, 2, 8, 8};
        static const unsigned long long lines[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 18, 19, 20, 31, 32, 33, 47, 77};
        static const unsigned long long long_groups[] = {127, 128, 129, 255, 257, 1000, 2047};
        unsigned long long faults = 0;

        for (unsigned long long kind = 0; kind < 5; kind++)
                for (unsigned long long at = 0; at < 101 + sizeof(long_groups) / sizeof(long_groups[0]); at++)
                {
                        unsigned long long groups = at <= 100 ? at : long_groups[at - 101];
                        unsigned long long in = groups * in_of[kind], symbols = groups * out_of[kind];
                        unsigned char *source = s_end - in;

                        for (unsigned long long i = 0; i < in; i++)
                                source[i] = ranged_byte();
                        (memory_encode_power2)(ranged_reference, source, groups, alphabets[kind], bits_of[kind]);
                        for (unsigned long long l = 0; l < sizeof(lines) / sizeof(lines[0]); l++)
                        {
                                unsigned long long line = lines[l], want = symbols + groups / line * 1;
                                unsigned char *into = d_end - want;

                                for (int i = 1; i <= 16; i++)
                                        into[-i] = (unsigned char)(0xc3 ^ i);
                                faults += (memory_encode_power2_lines)(into, source, groups, alphabets[kind], bits_of[kind], line) !=
                                          want;
                                unsigned long long made = 0;
                                for (unsigned long long g = 0; g < groups; g++)
                                {
                                        for (unsigned long long k = 0; k < out_of[kind]; k++)
                                                faults += into[made++] != ranged_reference[g * out_of[kind] + k];
                                        if ((g + 1) % line == 0)
                                                faults += into[made++] != '\n';
                                }
                                for (int i = 1; i <= 16; i++)
                                        faults += into[-i] != (unsigned char)(0xc3 ^ i);
                        }
                }
        return faults;
}

/*
        memory_cut_fields__room_64 against cut a line at a time: every count
        to 300 and long ones, fields from a byte to hundreds in four
        densities, ':' and 0xe9 for the delimiter with a newline, and '\n'
        and ',' with a NUL, tables of caps one to ten and 4100 with random
        entries, -s on and off; the source's room and the destination's
        both ending exactly against a page that faults, newlines in the
        source's room that must not be taken, guard bytes in front of the
        output, and the answer, the count made and the bytes all checked.
*/
static unsigned long long ranged_cut_reference(unsigned char *out, const unsigned char *in, unsigned long long length,
                                               const unsigned char *keep, unsigned long long control,
                                               unsigned long long *taken)
{
        unsigned char delimiter = (unsigned char)control, newline = (unsigned char)(control >> 8);
        unsigned long long only = control >> 16 & 1, cap = control >> 32, made = 0, start = 0;

        for (unsigned long long at = 0; at < length; at++)
        {
                if (in[at] != newline)
                        continue;
                unsigned long long fields = 0;
                for (unsigned long long i = start; i < at; i++)
                        fields += in[i] == delimiter;
                if (!fields)
                {
                        if (!only)
                                for (unsigned long long i = start; i <= at; i++)
                                        out[made++] = in[i];
                }
                else
                {
                        unsigned long long f = 0, from = start;
                        int wrote = 0;

                        for (unsigned long long i = start; i <= at; i++)
                                if (i == at || in[i] == delimiter)
                                {
                                        if (keep[f < cap ? f : cap])
                                        {
                                                if (wrote)
                                                        out[made++] = delimiter;
                                                for (unsigned long long k = from; k < i; k++)
                                                        out[made++] = in[k];
                                                wrote = 1;
                                        }
                                        f++;
                                        from = i + 1;
                                }
                        out[made++] = newline;
                }
                start = at + 1;
        }
        *taken = start;
        return made;
}

static unsigned char ranged_cut_want[20000 + 64], ranged_cut_keep[4101];

/*
        The shapes the word walks and their copies turn on, at every residue
        of the source against every residue of the destination: three fields
        of widths either side of 8, 16, 24, 32, 48 and 64 bytes, a delimiter
        at every residue of a line, lines with no delimiter (written whole,
        or dropped under -s), and a delimiter as the buffer's last byte with
        no newline after it; -f2, -f1, -f2- and -f1,3.
*/
static unsigned long long ranged_cut_shapes(unsigned char *d_end, unsigned char *s_end)
{
        static const unsigned char widths[] = {0, 1, 7, 8, 9, 15, 16, 17, 23, 24, 25, 31, 32, 33, 47, 48, 49, 64, 65};
        static const unsigned char lists[4][5] = {{3, 0, 1, 0}, {2, 1, 0}, {2, 0, 1}, {4, 1, 0, 1, 0}};
        static unsigned char text[4096];
        unsigned long long faults = 0;
        unsigned count = sizeof(widths) / sizeof(widths[0]);

        for (unsigned shape = 0; shape < 4; shape++)
        {
                unsigned long long length = 0;

                if (shape == 0)
                        for (unsigned a = 0; a < count; a++)
                        {
                                unsigned b = (a * 5 + 3) % count, c = (a * 7 + 1) % count;

                                for (unsigned i = 0; i < widths[a]; i++)
                                        text[length++] = (unsigned char)('a' + i % 26);
                                text[length++] = ':';
                                for (unsigned i = 0; i < widths[b]; i++)
                                        text[length++] = (unsigned char)('A' + i % 26);
                                text[length++] = ':';
                                for (unsigned i = 0; i < widths[c]; i++)
                                        text[length++] = (unsigned char)('0' + i % 10);
                                text[length++] = '\n';
                        }
                else if (shape == 1)
                        for (unsigned r = 0; r < 24; r++)
                        {
                                for (unsigned i = 0; i < r; i++)
                                        text[length++] = 'x';
                                text[length++] = ':';
                                for (unsigned i = 0; i < 17 + r; i++)
                                        text[length++] = 'y';
                                text[length++] = ':';
                                for (unsigned i = 0; i < 9; i++)
                                        text[length++] = 'z';
                                text[length++] = '\n';
                        }
                else if (shape == 2)
                        for (unsigned a = 0; a < count; a++)
                        {
                                for (unsigned i = 0; i < widths[a]; i++)
                                        text[length++] = 'q';
                                text[length++] = '\n';
                        }
                else
                {
                        for (unsigned a = 0; a < 6; a++)
                        {
                                for (unsigned i = 0; i < widths[a + 6]; i++)
                                        text[length++] = 'p';
                                text[length++] = ':';
                                text[length++] = 'r';
                                text[length++] = '\n';
                        }
                        for (unsigned i = 0; i < 20; i++)
                                text[length++] = 's';
                        text[length++] = ':';
                }
                for (unsigned so = 0; so < 8; so++)
                        for (unsigned dz = 0; dz < 8; dz++)
                                for (unsigned list = 0; list < 4; list++)
                                        for (unsigned long long only = 0; only < 2; only++)
                                        {
                                                unsigned long long cap = lists[list][0] - 1u;
                                                unsigned char *source = s_end - 64 - length - so;
                                                unsigned char *into = d_end - 64 - length - dz;
                                                unsigned long long control = ':' | (unsigned long long)'\n' << 8 | only << 16 |
                                                                             cap << 32;
                                                unsigned long long want_taken = 0, made = ~0ull;

                                                for (unsigned long long f = 0; f <= cap; f++)
                                                        ranged_cut_keep[f] = lists[list][1 + f];
                                                for (unsigned long long i = 0; i < length; i++)
                                                        source[i] = text[i];
                                                for (unsigned long long i = length; i < length + 64 + so; i++)
                                                        source[i] = i & 1 ? '\n' : ':';
                                                unsigned long long want = ranged_cut_reference(ranged_cut_want, source, length,
                                                                                               ranged_cut_keep, control, &want_taken);

                                                for (int i = 1; i <= 16; i++)
                                                        into[-i] = (unsigned char)(0x5c ^ i);
                                                VECTOR_FIRST_CALL(ranged_first);
                                                unsigned long long taken = (memory_cut_fields__room_64)(into, &made, source, length,
                                                                                                        ranged_cut_keep, control);
                                                faults += taken != want_taken || made != want ||
                                                          (made == want && !ranged_same(into, ranged_cut_want, want));
                                                for (int i = 1; i <= 16; i++)
                                                        faults += into[-i] != (unsigned char)(0x5c ^ i);
                                        }
        }
        return faults;
}

static unsigned long long ranged_cut_cases(unsigned char *d_end, unsigned char *s_end)
{
        static const unsigned long long longs[] = {1000, 4096, 5000, 20000};
        static const unsigned long long caps[] = {1, 2, 3, 4, 5, 7, 10, 4100};
        static const unsigned char ends[][2] = {{':', '\n'}, {0xe9, '\n'}, {'\n', 0}, {',', 0}};
        static const unsigned densities[][2] = {{40, 60}, {20, 28}, {4, 6}, {1, 2}};
        unsigned long long faults = 0;

        for (unsigned long long at = 0; at < 301 + sizeof(longs) / sizeof(longs[0]); at++)
        {
                unsigned long long length = at <= 300 ? at : longs[at - 301];

                for (unsigned e = 0; e < 4; e++)
                {
                        unsigned char delimiter = ends[e][0], newline = ends[e][1];
                        const unsigned *density = densities[(at + e) % 4];
                        unsigned char *source = s_end - 64 - length;

                        for (unsigned long long i = 0; i < length; i++)
                        {
                                unsigned r = ranged_byte();
                                source[i] = r < density[0]   ? delimiter
                                            : r < density[1] ? newline
                                                             : (unsigned char)('a' + r % 26);
                        }
                        for (unsigned long long i = length; i < length + 64; i++)
                                source[i] = i & 1 ? newline : delimiter;
                        for (unsigned c = 0; c < sizeof(caps) / sizeof(caps[0]); c++)
                        {
                                unsigned long long cap = caps[c];

                                if (cap > 10 && at % 16)
                                        continue;
                                for (unsigned long long f = 0; f <= cap; f++)
                                        ranged_cut_keep[f] = ranged_byte() & 1 ? (unsigned char)(ranged_byte() | 1) : 0;
                                for (unsigned long long only = 0; only < 2; only++)
                                {
                                        unsigned long long control = delimiter | (unsigned long long)newline << 8 | only << 16 |
                                                                     cap << 32;
                                        unsigned long long want_taken = 0;
                                        unsigned long long want = ranged_cut_reference(ranged_cut_want, source, length,
                                                                                       ranged_cut_keep, control, &want_taken);
                                        unsigned char *into = d_end - 64 - length;
                                        unsigned long long made = ~0ull;

                                        for (int i = 1; i <= 16; i++)
                                                into[-i] = (unsigned char)(0x5c ^ i);
                                        VECTOR_FIRST_CALL(ranged_first);
                                        unsigned long long taken = (memory_cut_fields__room_64)(into, &made, source, length,
                                                                                                ranged_cut_keep, control);
                                        faults += taken != want_taken || made != want ||
                                                  (made == want && !ranged_same(into, ranged_cut_want, want));
                                        for (int i = 1; i <= 16; i++)
                                                faults += into[-i] != (unsigned char)(0x5c ^ i);
                                }
                        }
                }
        }
        return faults + ranged_cut_shapes(d_end, s_end);
}

/*
        memory_reverse_lines__room_64 against rev a line at a time: lines of
        every length to 300 and long ones of printable bytes, two to four
        byte sequences of every lead class (E0, ED, F0 and F4 at their
        edges) and, in some, a byte that begins nothing, a cut sequence, an
        overlong form or a surrogate; '\n' and NUL for the newline; bytes and
        UTF-8. Under UTF-8 a line that is not well formed is where the
        routine stops. The source's room and the destination's end against a
        page that faults, and then the source starts just after one, which
        is what proves the turns never read before it.
*/
static int ranged_rev_valid(const unsigned char *line, unsigned long long length)
{
        for (unsigned long long i = 0; i < length;)
        {
                unsigned char b = line[i];
                unsigned long long n = b < 0x80 ? 1 : b >= 0xc2 && b <= 0xdf ? 2 : b >= 0xe0 && b <= 0xef ? 3
                                                    : b >= 0xf0 && b <= 0xf4 ? 4 : 0;
                if (!n || i + n > length)
                        return 0;
                for (unsigned long long k = 1; k < n; k++)
                        if ((line[i + k] & 0xc0) != 0x80)
                                return 0;
                if (n > 2)
                {
                        unsigned char second = line[i + 1];
                        if ((b == 0xe0 && second < 0xa0) || (b == 0xed && second > 0x9f) ||
                            (b == 0xf0 && second < 0x90) || (b == 0xf4 && second > 0x8f))
                                return 0;
                }
                i += n;
        }
        return 1;
}

static unsigned long long ranged_rev_reference(unsigned char *out, const unsigned char *in, unsigned long long length,
                                               unsigned long long control, unsigned long long *taken)
{
        unsigned char newline = (unsigned char)control;
        int utf8 = (int)(control >> 8 & 1);
        unsigned long long made = 0, start = 0;

        for (unsigned long long at = 0; at < length; at++)
        {
                if (in[at] != newline)
                        continue;
                unsigned long long size = at - start;
                if (utf8 && !ranged_rev_valid(in + start, size))
                        break;
                for (unsigned long long i = 0; i < size;)
                {
                        unsigned char b = in[start + i];
                        unsigned long long n = !utf8 || b < 0x80 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : 4;
                        for (unsigned long long k = 0; k < n; k++)
                                out[made + size - i - n + k] = in[start + i + k];
                        i += n;
                }
                made += size;
                out[made++] = newline;
                start = at + 1;
        }
        *taken = start;
        return made;
}

static unsigned char ranged_rev_want[20000 + 64];

static unsigned long long ranged_rev_fill(unsigned char *source, unsigned long long length, unsigned char newline,
                                          unsigned mode)
{
        static const unsigned char sequences[][4] = {
            {0xc3, 0xa9}, {0xd0, 0xbf}, {0xc2, 0x80}, {0xdf, 0xbf}, {0xe0, 0xa0, 0x80}, {0xe6, 0x97, 0xa5}, {0xed, 0x9f, 0xbf},
            {0xef, 0xbf, 0xbd}, {0xf0, 0x90, 0x80, 0x80}, {0xf0, 0x9f, 0x98, 0x80}, {0xf4, 0x8f, 0xbf, 0xbf}};
        static const unsigned char broken[][3] = {{0xff}, {0x80}, {0xc3}, {0xe6, 0x97}, {0xc0, 0xaf}, {0xed, 0xa0, 0x80}, {0xf5}};
        static const unsigned sizes[] = {2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4}, broken_sizes[] = {1, 1, 1, 2, 2, 3, 1};
        unsigned long long at = 0;

        while (at < length)
        {
                unsigned r = ranged_byte();
                if (r < (mode & 1 ? 12u : 3u))
                        source[at++] = newline;
                else if (mode & 2 && r < 80)
                {
                        unsigned which = ranged_byte() % 11;
                        for (unsigned k = 0; k < sizes[which] && at < length; k++)
                                source[at++] = sequences[which][k];
                }
                else if (mode & 4 && r == 255)
                {
                        unsigned which = ranged_byte() % 7;
                        for (unsigned k = 0; k < broken_sizes[which] && at < length; k++)
                                source[at++] = broken[which][k];
                }
                else
                        source[at++] = (unsigned char)(' ' + r % 95);
        }
        return at;
}

/*
        Long lines then short ones then long again, the same with sequences
        of every lead class in some, and lines of 31 to 34 and 63 to 66 bytes
        in turn, about the walks' 32- and 64-byte turns: each source against
        the end of its room and from the start of its own.
*/
static unsigned long long ranged_rev_one(unsigned char *d_end, unsigned char *source, unsigned long long length)
{
        unsigned long long faults = 0;

        for (unsigned long long i = length; i < length + 64; i++)
                source[i] = '\n';
        for (unsigned long long utf8 = 0; utf8 < 2; utf8++)
        {
                unsigned long long control = '\n' | utf8 << 8, want_taken = 0, made = ~0ull;
                unsigned long long want = ranged_rev_reference(ranged_rev_want, source, length, control, &want_taken);
                unsigned char *into = d_end - 64 - length;

                for (int i = 1; i <= 16; i++)
                        into[-i] = (unsigned char)(0x3a ^ i);
                unsigned long long taken = (memory_reverse_lines__room_64)(into, &made, source, length, control);
                faults += taken != want_taken || made != want || (made == want && !ranged_same(into, ranged_rev_want, want));
                for (int i = 1; i <= 16; i++)
                        faults += into[-i] != (unsigned char)(0x3a ^ i);
        }
        return faults;
}

static unsigned long long ranged_rev_shapes(unsigned char *d_end, unsigned char *s_end, unsigned char *s_front)
{
        static const unsigned char sequence[] = {0xc3, 0xa9, 0xe6, 0x97, 0xa5, 0xf0, 0x9f, 0x98, 0x80};
        static unsigned char text[16000];
        unsigned long long faults = 0;

        for (unsigned shape = 0; shape < 3; shape++)
        {
                unsigned long long length = 0, line = 0;

                while (length < sizeof(text) - 80)
                {
                        unsigned long long width = shape == 2 ? (line & 1 ? 63 : 31) + (line >> 1 & 3)
                                                   : length < 4600 || length > 12000 ? 40 + ranged_byte() % 21
                                                                                      : 4 + ranged_byte() % 11;
                        for (unsigned long long i = 0; i < width; i++)
                                text[length + i] = (unsigned char)('a' + (length + i) % 26);
                        if (shape == 1 && line % 3 == 0 && width >= 9)
                                for (unsigned long long i = 0; i < 9; i++)
                                        text[length + i] = sequence[i];
                        length += width;
                        text[length++] = '\n';
                        line++;
                }
                for (unsigned place = 0; place < 2; place++)
                {
                        unsigned char *source = place ? s_front : s_end - 64 - length;

                        for (unsigned long long i = 0; i < length; i++)
                                source[i] = text[i];
                        faults += ranged_rev_one(d_end, source, length);
                }
        }
        return faults;
}

static unsigned long long ranged_rev_cases(unsigned char *d_end, unsigned char *s_end, unsigned char *s_front)
{
        static const unsigned long long longs[] = {1000, 4096, 5000, 20000};
        unsigned long long faults = ranged_rev_shapes(d_end, s_end, s_front);

        for (unsigned long long at = 0; at < 301 + sizeof(longs) / sizeof(longs[0]); at++)
        {
                unsigned long long length = at <= 300 ? at : longs[at - 301];

                for (unsigned mode = 0; mode < 8; mode++)
                        for (unsigned place = 0; place < 2; place++)
                        {
                                unsigned char newline = mode & 1 ? '\n' : 0;
                                unsigned char *source = place ? s_front : s_end - 64 - length;

                                if (place && length > 16384 - 64)
                                        continue;
                                ranged_rev_fill(source, length, newline, mode);
                                for (unsigned long long i = length; i < length + 64; i++)
                                        source[i] = newline;
                                for (unsigned long long utf8 = 0; utf8 < 2; utf8++)
                                {
                                        unsigned long long control = newline | utf8 << 8, want_taken = 0, made = ~0ull;
                                        unsigned long long want = ranged_rev_reference(ranged_rev_want, source, length, control,
                                                                                       &want_taken);
                                        unsigned char *into = d_end - 64 - length;

                                        for (int i = 1; i <= 16; i++)
                                                into[-i] = (unsigned char)(0x3a ^ i);
                                        VECTOR_FIRST_CALL(ranged_first);
                                        unsigned long long taken = (memory_reverse_lines__room_64)(into, &made, source, length,
                                                                                                   control);
                                        faults += taken != want_taken || made != want ||
                                                  (made == want && !ranged_same(into, ranged_rev_want, want));
                                        for (int i = 1; i <= 16; i++)
                                                faults += into[-i] != (unsigned char)(0x3a ^ i);
                                }
                        }
        }
        return faults;
}

/*
        memory_fold_lines__room_64 against GNU's fold a line at a time:
        lines of every length to 300 and long ones, of printable bytes and,
        by mode, many newlines or few, tabs, and now and then a NUL, a
        backspace, a return, another control or a byte past ASCII; widths
        from one to past the longest line, each length meeting two of them;
        bytes and columns, C and UTF-8. A line with a byte the routine stops
        at is where it stops. The source's room holds newlines, so a block
        that took one would answer wrong; the rooms end at a page that
        faults, and then the source starts just after one.
*/
static unsigned long long ranged_fold_reference(unsigned char *out, const unsigned char *in, unsigned long long length,
                                                unsigned long long width, unsigned long long control,
                                                unsigned long long *taken)
{
        int bytes = (int)(control & 1), utf8 = (int)(control >> 1 & 1);
        unsigned long long made = 0, start = 0;

        for (unsigned long long at = 0; at < length; at++)
        {
                if (in[at] != '\n')
                        continue;
                int stop = 0;
                for (unsigned long long k = start; k < at; k++)
                        stop |= (!bytes && (in[k] == 0 || in[k] == 8 || in[k] == 13)) || (utf8 && in[k] >= 0x80);
                if (stop)
                        break;
                unsigned long long column = 0, line = made;
                for (unsigned long long k = start; k < at; k++)
                {
                        int tab = !bytes && in[k] == '\t';
                        unsigned long long after = tab ? (column | 7) + 1 : column + 1;
                        if (after > width && made > line)
                        {
                                out[made++] = '\n';
                                line = made;
                                after = tab ? 8 : 1;
                        }
                        out[made++] = in[k];
                        column = after;
                }
                out[made++] = '\n';
                start = at + 1;
        }
        *taken = start;
        return made;
}

static unsigned char ranged_fold_want[2 * 20000 + 64];

static void ranged_fold_fill(unsigned char *source, unsigned long long length, unsigned mode)
{
        static const unsigned char odd[] = {0, 8, 13, 1, 11, 0x7f, 0x80, 0xc3, 0xa9, 0xff};

        for (unsigned long long at = 0; at < length; at++)
        {
                unsigned r = ranged_byte();
                source[at] = r < (mode & 1 ? 12u : 2u)         ? '\n'
                             : mode & 2 && r >= 200 && r < 236 ? '\t'
                             : mode & 4 && r == 255            ? odd[ranged_byte() % sizeof(odd)]
                                                               : (unsigned char)(' ' + r % 95);
        }
}

static unsigned long long ranged_fold_cases(unsigned char *d_end, unsigned char *s_end, unsigned char *s_front)
{
        static const unsigned long long longs[] = {1000, 4096, 5000, 20000};
        static const unsigned long long widths[] = {1, 2, 7, 8, 9, 15, 16, 17, 33, 63, 64, 65, 80, 127, 200, 4100};
        unsigned long long faults = 0;

        for (unsigned long long at = 0; at < 301 + sizeof(longs) / sizeof(longs[0]); at++)
        {
                unsigned long long length = at <= 300 ? at : longs[at - 301];

                for (unsigned mode = 0; mode < 8; mode++)
                        for (unsigned place = 0; place < 2; place++)
                        {
                                unsigned char *source = place ? s_front : s_end - 64 - length;

                                if (place && length > 16384 - 64)
                                        continue;
                                ranged_fold_fill(source, length, mode);
                                for (unsigned long long i = length; i < length + 64; i++)
                                        source[i] = '\n';
                                for (unsigned pick = 0; pick < 2; pick++)
                                        for (unsigned long long control = 0; control < 4; control++)
                                        {
                                                unsigned long long width = widths[(at * 3 + mode * 5 + pick * 7 + place) % 16];
                                                unsigned long long want_taken = 0, made = ~0ull;
                                                unsigned long long want = ranged_fold_reference(ranged_fold_want, source, length,
                                                                                                width, control, &want_taken);
                                                unsigned char *into = d_end - 64 - 2 * length;

                                                for (int i = 1; i <= 16; i++)
                                                        into[-i] = (unsigned char)(0x3a ^ i);
                                                VECTOR_FIRST_CALL(ranged_first);
                                                unsigned long long taken = (memory_fold_lines__room_64)(into, &made, source,
                                                                                                       length, width, control);
                                                faults += taken != want_taken || made != want ||
                                                          (made == want && !ranged_same(into, ranged_fold_want, want));
                                                for (int i = 1; i <= 16; i++)
                                                        faults += into[-i] != (unsigned char)(0x3a ^ i);
                                        }
                        }
        }
        return faults;
}

/*
        memory_unique_lines__room_64 against uniq a line at a time: groups of
        one to thirty lines and now and then two hundred, of every length to
        300 and of long ones, from an alphabet of three so that lines alike
        but for a byte or a length are common; every mix of -c, -d and -u,
        the newline and NUL for the delimiter; the group open on entry of one
        line, of five, and of just under and just at ten million, its line
        the source's first or not. The line before the source is in the same
        run, and the source's room holds the delimiter, so a run that took a
        line from the room would answer wrong; the rooms end at a page that
        faults, and then the line before the source starts just after one,
        which is what proves nothing before it is read.
*/
static unsigned long long ranged_uniq_count(unsigned char *out, unsigned long long count)
{
        unsigned char digits[24];
        unsigned long long n = 0, made = 0;

        do
                digits[n++] = (unsigned char)('0' + count % 10);
        while (count /= 10);
        for (unsigned long long pad = n; pad < 7; pad++)
                out[made++] = ' ';
        while (n)
                out[made++] = digits[--n];
        out[made++] = ' ';
        return made;
}

static unsigned long long ranged_uniq_reference(unsigned char *out, const unsigned char *in, unsigned long long length,
                                                unsigned long long *state, unsigned long long control,
                                                unsigned long long *taken)
{
        unsigned char delimiter = (unsigned char)(control >> 8);
        int counting = (int)(control & 1), repeated = (int)(control >> 1 & 1), unique = (int)(control >> 2 & 1);
        const unsigned char *previous = in - state[1] - 1;
        unsigned long long previous_length = state[1], count = state[0], made = 0, start = 0;

        for (unsigned long long at = 0; at < length; at++)
        {
                if (in[at] != delimiter)
                        continue;
                const unsigned char *line = in + start;
                unsigned long long size = at - start;

                if (size == previous_length && ranged_same(line, previous, size))
                        count++;
                else
                {
                        if (!(control & 7))
                        {
                                for (unsigned long long i = 0; i <= size; i++)
                                        out[made++] = line[i];
                        }
                        else if (!(repeated && count < 2) && !(unique && count > 1))
                        {
                                if (counting)
                                        made += ranged_uniq_count(out + made, count);
                                for (unsigned long long i = 0; i <= previous_length; i++)
                                        out[made++] = previous[i];
                        }
                        count = 1;
                }
                previous = line;
                previous_length = size;
                start = at + 1;
        }
        *taken = start;
        state[0] = count;
        state[1] = previous_length;
        return made;
}

static unsigned char ranged_uniq_want[9 * 20000 + 1024];

//      Lines from an alphabet of three in groups, and now and then one alike but for its last byte.
static void ranged_uniq_fill(unsigned char *run, unsigned long long length, unsigned char delimiter, unsigned mode)
{
        unsigned char line[300];
        unsigned long long at = 0;

        while (at < length)
        {
                unsigned r = ranged_byte();
                unsigned long long size = mode & 1 ? r % 12 : (unsigned long long)r * ranged_byte() / 217;
                unsigned repeat = 1 + ranged_byte() % (mode & 2 ? 30 : 3);

                if (ranged_byte() == 7)
                        size = 64 + ranged_byte() % 200;
                if (ranged_byte() == 9)
                        repeat = 200;
                for (unsigned long long i = 0; i < size; i++)
                        line[i] = (unsigned char)('a' + ranged_byte() % 3);
                line[size] = delimiter;
                for (unsigned k = 0; k < repeat && at < length; k++)
                {
                        if (k && size && ranged_byte() < 8)
                                line[size - 1] ^= 1;
                        for (unsigned long long i = 0; i <= size && at < length; i++)
                                run[at++] = line[i];
                }
        }
}

static unsigned char ranged_uniq_made[20000];

static unsigned long long ranged_uniq_cases(unsigned char *d_end, unsigned char *s_end, unsigned char *s_front)
{
        static const unsigned long long longs[] = {1000, 4096, 5000, 20000};
        static const unsigned long long counts[] = {1, 5, 9999999, 10000000};
        unsigned long long faults = 0;

        for (unsigned long long at = 0; at < 301 + sizeof(longs) / sizeof(longs[0]); at++)
        {
                unsigned long long length = at <= 300 ? at : longs[at - 301];

                for (unsigned mode = 0; mode < 4; mode++)
                        for (unsigned place = 0; place < 2; place++)
                                for (unsigned pick = 0; pick < 4; pick++)
                                {
                                        unsigned long long control = (at * 3 + mode * 5 + place + pick * 2) % 8;
                                        unsigned char delimiter = (at + pick) % 4 == 3 ? 0 : '\n';
                                        unsigned long long base = 1 + (at * 7 + pick) % 9, first = 0;

                                        ranged_uniq_fill(ranged_uniq_made, length, delimiter, mode);
                                        while (first < length && ranged_uniq_made[first] != delimiter)
                                                first++;
                                        //      Half the time the line before is the source's first, when that is whole.
                                        if (pick & 1 && first < length)
                                                base = first + 1;
                                        if ((control & 1) && 9 * length + 4096 > RANGED_SPAN)
                                                control &= 6;
                                        unsigned char *run = place ? s_front : s_end - 64 - length - base;

                                        if (place && length + base > 16384 - 64)
                                                continue;
                                        for (unsigned long long i = 0; i + 1 < base; i++)
                                                run[i] = pick & 1 && first < length ? ranged_uniq_made[i]
                                                                                    : (unsigned char)('a' + ranged_byte() % 3);
                                        run[base - 1] = delimiter;
                                        for (unsigned long long i = 0; i < length; i++)
                                                run[base + i] = ranged_uniq_made[i];
                                        for (unsigned long long i = 0; i < 64; i++)
                                                run[base + length + i] = delimiter;
                                        control |= (unsigned long long)delimiter << 8;

                                        unsigned long long want_state[2] = {counts[pick], base - 1}, state[2] = {counts[pick], base - 1};
                                        unsigned long long want_taken = 0, made = ~0ull;
                                        unsigned long long want = ranged_uniq_reference(ranged_uniq_want, run + base, length,
                                                                                        want_state, control, &want_taken);
                                        unsigned long long most = control & 1   ? 9 * length + base + 21
                                                                  : control & 6 ? length + base
                                                                                : length;
                                        unsigned char *into = d_end - 64 - most;

                                        for (int i = 1; i <= 16; i++)
                                                into[-i] = (unsigned char)(0x3a ^ i);
                                        VECTOR_FIRST_CALL(ranged_first);
                                        unsigned long long taken = (memory_unique_lines__room_64)(into, &made, run + base,
                                                                                                 length, state, control);
                                        faults += taken != want_taken || made != want || state[0] != want_state[0] ||
                                                  state[1] != want_state[1] ||
                                                  (made == want && !ranged_same(into, ranged_uniq_want, want));
                                        for (int i = 1; i <= 16; i++)
                                                faults += into[-i] != (unsigned char)(0x3a ^ i);
                                }
        }
        return faults;
}

/*
        memory_paste_lines__room_64 against paste a pair of lines at a time:
        two inputs of lines of every length to 300 and of long ones, either
        holding more lines, the tab, a comma and NUL for the delimiter, the
        newline and NUL for the line end, and each, the other or both inputs
        ended. Each input's room holds the line end, so a pair that took a
        line from the room would answer wrong; the left ends against a page
        that faults, the right starts just after one, and the destination's
        room ends against one.
*/
static unsigned long long ranged_paste_reference(unsigned char *out, const unsigned char *left, unsigned long long left_length,
                                                 const unsigned char *right, unsigned long long right_length,
                                                 unsigned long long control, unsigned long long *taken)
{
        unsigned char delimiter = (unsigned char)control, line_end = (unsigned char)(control >> 8);
        int left_ended = (int)(control >> 16 & 1), right_ended = (int)(control >> 17 & 1);
        unsigned long long made = 0, l = 0, r = 0;

        while (!(left_ended && right_ended))
        {
                unsigned long long le = l, re = r;
                if (!left_ended)
                {
                        while (le < left_length && left[le] != line_end)
                                le++;
                        if (le >= left_length)
                                break;
                }
                if (!right_ended)
                {
                        while (re < right_length && right[re] != line_end)
                                re++;
                        if (re >= right_length)
                                break;
                }
                if (!left_ended)
                        for (unsigned long long i = l; i < le; i++)
                                out[made++] = left[i];
                out[made++] = delimiter;
                if (!right_ended)
                        for (unsigned long long i = r; i < re; i++)
                                out[made++] = right[i];
                out[made++] = line_end;
                if (!left_ended)
                        l = le + 1;
                if (!right_ended)
                        r = re + 1;
        }
        taken[0] = l;
        taken[1] = r;
        return made;
}

static unsigned char ranged_paste_want[2 * 2 * 20000 + 1024];

static void ranged_paste_fill(unsigned char *run, unsigned long long length, unsigned char line_end, unsigned mode)
{
        unsigned long long at = 0;

        while (at < length)
        {
                unsigned r = ranged_byte();
                unsigned long long size = mode & 1 ? r % 9 : (unsigned long long)r * ranged_byte() / 217;

                if (ranged_byte() == 5)
                        size = 64 + ranged_byte() % 400;
                for (unsigned long long i = 0; i < size && at < length; i++)
                        run[at++] = (unsigned char)(' ' + ranged_byte() % 95);
                if (at < length)
                        run[at++] = line_end;
        }
}

static unsigned long long ranged_paste_cases(unsigned char *d_end, unsigned char *s_end, unsigned char *s_front)
{
        static const unsigned long long longs[] = {1000, 4096, 5000};
        static const unsigned char delimiters[] = {'\t', ',', 0};
        unsigned long long faults = 0;

        for (unsigned long long at = 0; at < 301 + sizeof(longs) / sizeof(longs[0]); at++)
        {
                unsigned long long left_length = at <= 300 ? at : longs[at - 301];

                for (unsigned mode = 0; mode < 4; mode++)
                        for (unsigned ended = 0; ended < 4; ended++)
                        {
                                unsigned long long right_length = (left_length * (1 + mode) + at * 7) % 4300;
                                unsigned char line_end = (at + mode) % 5 == 4 ? 0 : '\n';
                                unsigned long long control = delimiters[(at + ended) % 3] | (unsigned long long)line_end << 8 |
                                                             (unsigned long long)ended << 16;
                                unsigned char *left = s_end - 64 - left_length, *right = s_front;

                                if (right_length > 16384 - 64)
                                        continue;
                                ranged_paste_fill(left, left_length, line_end, mode);
                                ranged_paste_fill(right, right_length, line_end, mode >> 1);
                                for (unsigned long long i = 0; i < 64; i++)
                                        left[left_length + i] = right[right_length + i] = line_end;

                                unsigned long long want_taken[2], taken[2] = {~0ull, ~0ull};
                                unsigned long long want = ranged_paste_reference(ranged_paste_want, left, left_length, right,
                                                                                 right_length, control, want_taken);
                                unsigned char *into = d_end - 64 - 2 * (left_length + right_length);

                                for (int i = 1; i <= 16; i++)
                                        into[-i] = (unsigned char)(0x3a ^ i);
                                VECTOR_FIRST_CALL(ranged_first);
                                unsigned long long made = (memory_paste_lines__room_64)(into, taken, left, left_length, right,
                                                                                        right_length, control);
                                faults += made != want || taken[0] != want_taken[0] || taken[1] != want_taken[1] ||
                                          (made == want && !ranged_same(into, ranged_paste_want, want));
                                for (int i = 1; i <= 16; i++)
                                        faults += into[-i] != (unsigned char)(0x3a ^ i);
                        }
        }
        return faults;
}

#ifdef CHECK_ranged
/*
        memory_first_of's windows for a short count and the first sixty four
        of a long one: every count to 300 and 4096 and 65536, the byte at every
        place in the first 80 and the last 80 and nowhere, each count's span
        ending against a page that faults and starting just after one, and the
        bytes past the count holding the byte so a read past it answers wrong.
*/
static unsigned long long ranged_first_of_cases(p8 address_to page_end, p8 address_to page_front)
{
        static const positive longs[] = {511, 4095, 4096, 4097, 65536};
        unsigned long long faults = 0;

        for (positive at = 0; at < 301 + sizeof(longs) / sizeof(longs[0]); at++)
        {
                positive n = at <= 300 ? at : longs[at - 301];

                for (positive where = 0; where < 2; where++)
                {
                        p8 address_to span = where ? page_front : page_end - n;
                        p8 front = where;

                        if (front && n > 16384 - 64)
                                continue;
                        for (positive i = 0; i < n; i++)
                                span[i] = (p8)(ranged_byte() | 1);
                        if (front)
                                for (positive i = n; i < n + 64; i++)
                                        span[i] = 0x2a;
                        for (b64 hit = -1; hit < (b64)n; hit++)
                        {
                                if (hit >= 80 && hit < (b64)n - 80)
                                        continue;
                                if (hit >= 0)
                                        span[hit] = 0x2a;
                                address_any got = (memory_first_of)(span, 0x2a, n);
                                faults += got != (hit >= 0 ? span + hit : null);
                                if (hit >= 0)
                                        span[hit] = (p8)(ranged_byte() | 1) == 0x2a ? 0x2b : 0x2d;
                        }
                }
        }
        return faults;
}

/*
        The terminated hunts beside string_length -- string_first_of,
        string_first_of_or_end, string_first_of_max and string_length_max --
        over every string to 100 bytes at each of the last 96 starts before a
        page that faults and to 300 at every alignment, the byte hunted
        nowhere, first, in the middle, last and just past the terminator, and
        bounds short of the string, at it and past it.
*/
static unsigned long long ranged_scans_one(p8 address_to start, positive n, b64 hit)
{
        unsigned long long faults = 0;
        p8 address_to want = hit >= 0 && hit < (b64)n ? start + hit : null;
        positive bounds[] = {0, n / 2, n, n + 1, n + 4096};

        faults += (p8 address_to)(string_first_of)((string_address)start, 0x2a) != want;
        faults += (p8 address_to)(string_first_of_or_end)((string_address)start, 0x2a) != (want ? want : start + n);
        faults += (p8 address_to)(string_first_of)((string_address)start, 0) != start + n;
        for (positive b = 0; b < sizeof(bounds) / sizeof(bounds[0]); b++)
        {
                positive bound = bounds[b];

                faults += (string_length_max)((string_address)start, bound) != (bound < n ? bound : n);
                faults += (p8 address_to)(string_first_of_max)((string_address)start, bound, 0x2a) !=
                          (want && (positive)hit < bound ? want : null);
        }
        return faults;
}

static unsigned long long ranged_scans_cases(p8 address_to page_end)
{
        unsigned long long faults = 0;

        for (positive back = 1; back <= 96; back++)
                for (positive n = 0; n < back && n <= 100; n++)
                {
                        p8 address_to start = page_end - back;
                        b64 hits[] = {-1, 0, (b64)n / 2, (b64)n - 1, (b64)n + 1};

                        for (positive h = 0; h < sizeof(hits) / sizeof(hits[0]); h++)
                        {
                                for (positive i = 0; i < 64; i++)
                                        start[(b64)i - 64] = (p8)(i & 1 ? 0 : 0x2a);
                                for (positive i = 0; i < n; i++)
                                        start[i] = (p8)(ranged_byte() % 254 + 1);
                                for (positive i = 0; i < n; i++)
                                        start[i] = start[i] == 0x2a ? 0x2d : start[i];
                                start[n] = 0;
                                for (positive i = n + 1; i < back; i++)
                                        start[i] = (p8)(i & 3 ? 0x2a : 0);
                                if (hits[h] >= 0 && hits[h] < (b64)back && hits[h] != (b64)n)
                                        start[hits[h]] = 0x2a;
                                faults += ranged_scans_one(start, n, hits[h]);
                        }
                }
        for (positive align = 0; align < 64; align++)
                for (positive n = 0; n <= 300; n++)
                {
                        p8 address_to start = page_end - 4096 + 1024 + align;
                        b64 hits[] = {-1, (b64)(ranged_byte() % (n + 1)) - 1, (b64)n - 1};

                        for (positive h = 0; h < sizeof(hits) / sizeof(hits[0]); h++)
                        {
                                for (positive i = 0; i < n; i++)
                                        start[i] = (p8)(ranged_byte() % 254 + 1);
                                for (positive i = 0; i < n; i++)
                                        start[i] = start[i] == 0x2a ? 0x2d : start[i];
                                start[n] = 0;
                                for (positive i = n + 1; i < n + 40; i++)
                                        start[i] = (p8)(i & 1 ? 0 : 0x2a);
                                if (hits[h] >= 0)
                                        start[hits[h]] = 0x2a;
                                faults += ranged_scans_one(start, n, hits[h]);
                        }
                }
        return faults;
}

//      A run of RANGED_SPAN bytes whose end is a page that faults, or whose
//      start is just after one.
static unsigned char *ranged_end(void)
{
        positive span = (RANGED_SPAN + 4095) / 4096 * 4096;
        p8 address_to pages = memory(span + 4096);

        if (!pages || system_call_3(syscall(mprotect), (positive)(pages + span), 4096, 0) != 0)
                return null;
        return pages + span;
}

static unsigned char *ranged_front(void)
{
        p8 address_to pages = memory(16384 + 3 * 4096);

        if (!pages)
                return null;
        p8 address_to start = (p8 address_to)(((positive)pages + 4095) & ~(positive)4095) + 4096;
        if (system_call_3(syscall(mprotect), (positive)(start - 4096), 4096, 0) != 0)
                return null;
        return start;
}

static unsigned char *ranged_d_end, *ranged_s_end, *ranged_d_front, *ranged_s_front;

static fn ranged_tier(string_address tier)
{
        ranged_copy_faults faults = {0, 0, 0, 0, 0};

        string_format(log, "  %s\n", tier);
        ranged_copy_cases(ranged_d_end, ranged_s_end, ranged_d_front, ranged_s_front, address_of faults);
        check("memory_copy_apart__room_64: every count to 300 and the long ones, each placement, the bytes copied",
              faults.copied == 0);
        check("memory_copy_apart__room_64: answers the destination", faults.answer == 0);
        check("memory_copy_apart__room_64: nothing before a start or past the room touched", faults.outside == 0);
        check("memory_copy_apart__room_64: the source as it was", faults.source == 0);
        check("memory_copy_apart__room_64: both runs just after a page that faults", faults.front == 0);
        check("string_length: every length to 100 at each of the last 96 starts before a page that faults, and to 300 "
              "at every alignment",
              ranged_length_cases(ranged_d_end) == 0);
        check("string_first_of, string_first_of_or_end, string_first_of_max and string_length_max: every string to "
              "100 at each of the last 96 starts before a page that faults and to 300 at every alignment, the byte "
              "nowhere, first, in the middle, last and past the end, bounds short of the string, at it and past it",
              ranged_scans_cases(ranged_d_end) == 0);
        check("memory_encode_power2_lines: every bits, groups and line, against the routine with the newlines put in",
              ranged_lines_cases(ranged_d_end, ranged_s_end) == 0);
        check("memory_first_of: every count to 300 and long ones, the byte at each end's every place and nowhere, against "
              "pages that fault at either end",
              ranged_first_of_cases(ranged_d_end, ranged_d_front) == 0);
        check("memory_cut_fields__room_64: every count to 300 and long ones, four densities, four delimiter and line end "
              "pairs, caps one to ten and 4100, -s, against cut a line at a time with both rooms ending at a page that faults",
              ranged_cut_cases(ranged_d_end, ranged_s_end) == 0);
        check("memory_reverse_lines__room_64: every count to 300 and long ones, ASCII, UTF-8 of every lead class and broken "
              "bytes, two newlines, bytes and UTF-8, against rev a line at a time, the rooms ending at a page that faults "
              "and the source starting just after one",
              ranged_rev_cases(ranged_d_end, ranged_s_end, ranged_s_front) == 0);
        check("memory_fold_lines__room_64: every count to 300 and long ones, tabs, controls and bytes past ASCII, widths "
              "one to 4100, bytes and columns, C and UTF-8, against fold a line at a time, the rooms ending at a page "
              "that faults and the source starting just after one",
              ranged_fold_cases(ranged_d_end, ranged_s_end, ranged_s_front) == 0);
        check("memory_unique_lines__room_64: every count to 300 and long ones, groups of one to two hundred lines "
              "alike or all but a byte, every mix of -c, -d and -u, the newline and NUL, groups open on entry of one "
              "to ten million lines, against uniq a line at a time, the rooms ending at a page that faults and the "
              "line before the source starting just after one",
              ranged_uniq_cases(ranged_d_end, ranged_s_end, ranged_s_front) == 0);
        check("memory_paste_lines__room_64: every count to 300 and long ones, lines of every length, the tab, a comma "
              "and NUL between, the newline and NUL at the end, either input or both ended, against paste a pair at a "
              "time, the left ending at a page that faults and the right starting just after one",
              ranged_paste_cases(ranged_d_end, ranged_s_end, ranged_s_front) == 0);
}

/*
        The spelled forms through lib.util.c, in the checked build: a literal
        count is the expansion, any other the entry, and either way the room
        comes back spoiled, which is what makes a caller that kept something
        there give a wrong answer in a lane.
*/
static p8 ranged_spoiled(const p8 address_to room)
{
        for (positive i = 0; i < 64; i++)
                if (room[i] != 0xa5)
                        return false;
        return true;
}

#define RANGED_LITERAL(count)                                                         \
        do {                                                                          \
                for (positive i = 0; i < (count) + 64; i++)                           \
                        s[i] = ranged_byte(), d[i] = 0;                               \
                fine = fine && MEMORY_COPY_APART__ROOM(64, d, s, count) == d &&      \
                       ranged_same(d, s, count) && ranged_spoiled(d + (count));       \
        } while (0)

static fn ranged_macros(void)
{
        p8 address_to d = ranged_d_end - 64 - 300, address_to s = ranged_s_end - 64 - 300;
        p8 fine = true;

        RANGED_LITERAL(0);
        RANGED_LITERAL(1);
        RANGED_LITERAL(7);
        RANGED_LITERAL(16);
        RANGED_LITERAL(33);
        RANGED_LITERAL(64);
        RANGED_LITERAL(100);
        RANGED_LITERAL(128);
        RANGED_LITERAL(129);
        check("MEMORY_COPY_APART__ROOM(64, ...): literal counts copied, the room spoiled", fine);

        for (volatile positive count = 0; count <= 300; count++)
        {
                positive n = count;

                for (positive i = 0; i < n + 64; i++)
                        s[i] = ranged_byte(), d[i] = 0;
                fine = fine && memory_copy_apart__room_64(d, s, n) == d && ranged_same(d, s, n) && ranged_spoiled(d + n);
        }
        check("memory_copy_apart__room_64(...): every count to 300 through the macro, the room spoiled", fine);
}

/*
        string_span's first byte is placed at the call site ahead of the
        routine: through lib.util.c's macro and through the routine by its
        own name, over tables of every density (an empty set to every byte
        but the terminator) and strings with spans of nothing to their whole
        length, the two answers and the reference agree.
*/
static fn ranged_spans(void)
{
        static b8 table[256];
        static p8 text[80];
        p8 fine = true;

        for (positive density = 0; density <= 256; density += 16)
                for (positive round = 0; round < 64; round++)
                {
                        for (positive i = 0; i < 256; i++)
                                table[i] = (b8)(i && ranged_byte() < density);
                        positive n = ranged_byte() % 64;
                        for (positive i = 0; i < n; i++)
                                text[i] = (p8)(1 + ranged_byte() % 255);
                        text[n] = 0;
                        positive want = 0;
                        while (table[text[want]])
                                want++;
                        fine = fine && string_span((string_address)text, table) == want &&
                               (string_span)((string_address)text, table) == want;
                }
        check("string_span: the placed first byte and the routine agree with the walk, empty spans and whole ones", fine);
}

//      The checked macro reads the last byte of the source's room first, so
//      a source with no room past it is a fault and not a quiet over-read.
static fn ranged_promise_broken(void)
{
        log_flush();
        bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);

        if (child == 0)
        {
                static p8 into[256];
                volatile positive n = 8;

                memory_copy_apart__room_64(into, ranged_s_end - 8, n);
                exit(0);
        }
        positive raw = 0;

        if (child > 0)
                system_wait4_retry(child, address_of raw, 0, null);
        check("MOONWATER_RANGES_CHECKED: a source with no room past it faults", child > 0 && (raw & 0x7f) == SIGSEGV);
}

b32 main(void)
{
        ranged_d_end = ranged_end(), ranged_s_end = ranged_end();
        ranged_d_front = ranged_front(), ranged_s_front = ranged_front();
        check("ranged: guard pages", ranged_d_end && ranged_s_end && ranged_d_front && ranged_s_front);
        if (!ranged_d_end || !ranged_s_end || !ranged_d_front || !ranged_s_front)
                return test_report(null);
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;

        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier == 0 ? wide2 : 0;
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                if (tier == 0 && !wide2)
                        continue;
                ranged_tier(cpu_has_avx2 ? "avx2" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2;
#elif RISCV64
        p8 vector = cpu_has_vector;
        {
                p8 address_to d = ranged_d_end - 64 - 40, address_to s = ranged_s_end - 64 - 40;

                for (positive i = 0; i < 40; i++)
                        s[i] = ranged_byte();
                cpu_has_vector = 0;
                (memory_copy_apart__room_64)(d, s, 40);
                check("memory_copy_apart__room_64: the first call asks for V and keeps its arguments",
                      cpu_has_vector != 0 && ranged_same(d, s, 40));
                vector = cpu_has_vector;
        }
        //      _start wrote cpu_has_zbb from riscv_hwprobe (syscall 258, key 4,
        //      Zbb at bit 4) before main; the third tier takes it away.
        p8 zbb = cpu_has_zbb;
        {
                positive pair[2] = {4, 0};
                b64 asked = (b64)system_call_5(258, (positive)pair, 1, 0, 0, 0);
                p8 said = asked == 0 && pair[0] == 4 ? (p8)(pair[1] >> 4 & 1) : 0;

                check("cpu_has_zbb: _start wrote what riscv_hwprobe says", zbb == said);
        }
        //      Every call of the first tier the first in a process, which asks
        //      for V and comes back to the routine's entry: with V where the
        //      machine has it and the word bodies where it has not.
        for (p32 tier = 0; tier < 3; tier++)
        {
                if (tier == 2 && !zbb)
                        continue;
                cpu_has_vector = tier ? 1 : vector;
                cpu_has_zbb = tier == 2 ? 0 : zbb;
                ranged_first = tier == 0;
                ranged_tier(tier == 0 ? vector == 2 ? "V, every call the first" : "no V, every call the first"
                            : tier == 2 ? "no V, no Zbb" : zbb ? "no V, Zbb" : "no V");
        }
        ranged_first = 0;
        cpu_has_vector = vector;
        cpu_has_zbb = zbb;
#else
        ranged_tier("neon");
#endif
        ranged_macros();
        ranged_spans();
        ranged_promise_broken();
        return test_report(null);
}
#else
/*
        CHECK_native_ranged: the arm64 call-site entries lifted out of
        lib.c, through the walks CHECK_ranged runs above, then timed
        on this machine against what a caller gets today: lib.util.c's
        inline overlapping pair under sixteen bytes and the lifted
        memory_copy_apart above, which is copy_apart_running's shape.
*/
static unsigned long long ranged_ticks(void)
{
        unsigned long long value;

        __asm__ volatile("isb\n   mrs %0, cntvct_el0" : "=r"(value));
        return value;
}

//      Apple's pages are 16 KiB.
static unsigned char *ranged_native_end(void)
{
        unsigned long long span = (RANGED_SPAN + 16383) / 16384 * 16384;
        unsigned char *pages = mmap(0, span + 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

        if (pages == MAP_FAILED || mprotect(pages + span, 16384, PROT_NONE) != 0)
                return 0;
        return pages + span;
}

static unsigned char *ranged_native_front(void)
{
        unsigned char *pages = mmap(0, 3 * 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

        if (pages == MAP_FAILED || mprotect(pages, 16384, PROT_NONE) != 0)
                return 0;
        return pages + 16384;
}

//      copy_apart_running at LIBRARY_RUNNING_MAX 16, as the compiler builds it here.
__attribute__((noinline)) static void *ranged_shipped(void *destination, const void *source, unsigned long long size)
{
        unsigned char *to = destination;
        const unsigned char *from = source;

        if (size >= 16)
                return memory_copy_apart(destination, source, size);
        if (size >= 8)
        {
                unsigned long long head, tail;

                __builtin_memcpy(&head, from, 8);
                __builtin_memcpy(&tail, from + size - 8, 8);
                __builtin_memcpy(to, &head, 8);
                __builtin_memcpy(to + size - 8, &tail, 8);
        }
        else if (size >= 4)
        {
                unsigned head, tail;

                __builtin_memcpy(&head, from, 4);
                __builtin_memcpy(&tail, from + size - 4, 4);
                __builtin_memcpy(to, &head, 4);
                __builtin_memcpy(to + size - 4, &tail, 4);
        }
        else if (size >= 2)
        {
                unsigned short head, tail;

                __builtin_memcpy(&head, from, 2);
                __builtin_memcpy(&tail, from + size - 2, 2);
                __builtin_memcpy(to, &head, 2);
                __builtin_memcpy(to + size - 2, &tail, 2);
        }
        else if (size)
                to[0] = from[0];
        return destination;
}

#define RANGED_LINES 65536
static unsigned ranged_line_at[RANGED_LINES], ranged_line_size[RANGED_LINES];
static unsigned ranged_lines;
static unsigned char ranged_text[1 << 23];
static unsigned char ranged_out[RANGED_LINES * 140 + 4096];
static volatile unsigned long long ranged_sink;

//      The lines of lib.c itself, up to 128 bytes, in order: a text tool's
//      lengths, which a predictor cannot learn.
static void ranged_load_lines(void)
{
        FILE *in = fopen("src/lib.c", "rb");
        unsigned long long have = in ? fread(ranged_text, 1, sizeof(ranged_text) - 4096, in) : 0;
        unsigned long long start = 0;

        if (in)
                fclose(in);
        for (unsigned long long i = 0; i < have && ranged_lines < RANGED_LINES; i++)
                if (ranged_text[i] == '\n')
                {
                        if (i - start <= 128)
                        {
                                ranged_line_at[ranged_lines] = (unsigned)start;
                                ranged_line_size[ranged_lines++] = (unsigned)(i - start);
                        }
                        start = i + 1;
                }
}

//      shape 0: into a buffer nothing reads back; 1: into one buffer, read at once.
static unsigned long long ranged_time_one(ranged_copy_call *routine, int shape)
{
        unsigned long long best = ~0ull;

        for (int trial = 0; trial < 21; trial++)
        {
                unsigned long long made = 0, sum = 0, start = ranged_ticks();

                for (unsigned i = 0; i < ranged_lines; i++)
                {
                        unsigned long long n = ranged_line_size[i];
                        unsigned char *d = shape ? ranged_out : ranged_out + made;

                        routine(d, ranged_text + ranged_line_at[i], n);
                        if (shape)
                                sum += d[n >> 1];
                        else
                                made += n + 8;
                }
                unsigned long long took = ranged_ticks() - start;

                ranged_sink += sum + made;
                if (took < best)
                        best = took;
        }
        return best;
}

static void ranged_time(void)
{
        static const char *shapes[] = {"into a buffer nothing reads", "into one buffer read at once"};

        ranged_load_lines();
        if (!ranged_lines)
                return;
        for (int shape = 0; shape < 2; shape++)
        {
                //      Alternated, best of each, in nanoseconds a line (the counter is 24 MHz).
                unsigned long long shipped = ranged_time_one(ranged_shipped, shape);
                unsigned long long entry = ranged_time_one(memory_copy_apart__room_64, shape);
                unsigned long long shipped2 = ranged_time_one(ranged_shipped, shape);

                if (shipped2 < shipped)
                        shipped = shipped2;
                printf("  arm64 memory_copy_apart__room_64 %s, %u lines: shipped %.2f ns, entry %.2f ns a line\n", shapes[shape],
                       ranged_lines, shipped * 1e9 / 24e6 / ranged_lines, entry * 1e9 / 24e6 / ranged_lines);
        }
}

int main(void)
{
        unsigned char *d_end = ranged_native_end(), *s_end = ranged_native_end();
        unsigned char *d_front = ranged_native_front(), *s_front = ranged_native_front();
        ranged_copy_faults faults = {0, 0, 0, 0, 0};

        if (!d_end || !s_end || !d_front || !s_front)
        {
                printf("arm64 ranged: 1 checks | 1 failures\n");
                return 1;
        }
        ranged_copy_cases(d_end, s_end, d_front, s_front, &faults);
        unsigned long long lengths = ranged_length_cases(d_end);
        unsigned long long lines = ranged_lines_cases(d_end, s_end);
        unsigned long long cuts = ranged_cut_cases(d_end, s_end);
        unsigned long long revs = ranged_rev_cases(d_end, s_end, s_front);
        unsigned long long folds = ranged_fold_cases(d_end, s_end, s_front);
        unsigned long long uniqs = ranged_uniq_cases(d_end, s_end, s_front);
        unsigned long long pastes = ranged_paste_cases(d_end, s_end, s_front);
        ranged_time();
        printf("arm64 ranged: 12 checks | copied %llu | answer %llu | outside %llu | source %llu | front %llu | lengths %llu | "
               "lines %llu | cuts %llu | revs %llu | folds %llu | uniqs %llu | pastes %llu\n",
               faults.copied, faults.answer, faults.outside, faults.source, faults.front, lengths, lines, cuts, revs, folds,
               uniqs, pastes);
        return faults.copied + faults.answer + faults.outside + faults.source + faults.front + lengths + lines + cuts + revs +
                   folds + uniqs + pastes !=
               0;
}
#endif
#endif /* CHECK_ranged || CHECK_native_ranged */

#ifdef CHECK_environment
/*
        Experimental C standard library

        setenv, unsetenv, putenv, clearenv and getenv, against glibc's

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        One file, built twice, the way CHECK_allocator and CHECK_modulo are.

        Built the ordinary way it runs standard.c's environment on x86_64,
        arm64 and riscv64; built with ENVIRONMENT_REFERENCE it links glibc.
        Both walk the same seeded sequences of calls over a small pool of
        names, values and putenv buffers and print what every call answered
        -- the return, errno after a refusal, what getenv found -- and what
        environ holds after it, and the stdlib lane diffs those lines up to
        "shared-end".

        environ is printed for the pool's names only. Both builds start from
        the environment the lane runs them in, and qemu-user and the host are
        free to differ in it; the pool's names are the test's own, and the
        clearenv that ends every sequence drops everything else in both.

        A string getenv answered is sometimes kept and read again later in
        the sequence. glibc never frees or rewrites one, and neither does
        standard.c, which reinstalls a dropped string when a set asks for
        exactly its bytes instead of making another; a putenv buffer written
        through afterwards reads back changed in both.

        Left out of the pool on purpose, because the two libraries differ by
        design or glibc does not survive the call: a name with an equals in
        it handed to getenv (a key ends at its first equals here, so it is
        never found; glibc matches the longer prefix), putenv of an entry
        with an empty name (refused here with EINVAL, which CHECK_stdlib
        pins), and null names or values anywhere but setenv and unsetenv's
        name.

        After "shared-end" each build prints its own verdict on what needs no
        reference: after every call, getenv of every pool name is exactly the
        value of the first entry in environ with that key, or null.
*/

#ifdef ENVIRONMENT_REFERENCE

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef unsigned long long positive;
typedef unsigned long long p64;
typedef long long bipolar;
typedef int b32;
typedef void fn;

#define address_to *
#define address_of &
#define bool unsigned char
#define true 1
#define false 0

#define environment_set(name, value, overwrite) setenv((name), (value), (overwrite))
#define environment_unset(name) unsetenv(name)
#define environment_put(entry) putenv(entry)
#define environment_clear() clearenv()
#define environment_get(name) ((const char *)getenv(name))
#define environment_entries() ((const char **)environ)

#else

#include "../../src/lib.util.c"

#define environment_set(name, value, overwrite)                               \
        setenv((string_address)(name), (string_address)(value), (overwrite))
#define environment_unset(name) unsetenv((string_address)(name))
#define environment_put(entry) putenv((string_address)(entry))
#define environment_clear() clearenv()
#define environment_get(name) ((const char *)getenv((string_address)(name)))
#define environment_entries() ((const char **)environ)

#endif

#define ENVIRONMENT_SEQUENCES 12
#define ENVIRONMENT_CALLS 300
#define ENVIRONMENT_KEPT 8
#define ENVIRONMENT_HELD 256

static const char address_to const environment_names[] = {
        "A", "B", "EMPTY_VALUE", "LONG_ENVIRONMENT_NAME", "LONG_ENVIRONMENT_NAME_2",
        "PATH_LIKE", "Z9",
};

static const char address_to const environment_bad_names[] = {"", "A=B", "="};

static const char address_to const environment_values[] = {
        "",
        "1",
        "two",
        "a value with spaces",
        "=leading",
        "x=y=z",
        "BbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBbBb",
        "two",
};

#define environment_count(table) (sizeof(table) / sizeof(table[0]))

//      putenv's strings belong to the test, and some are written through
//      after they are installed.
static const char address_to const environment_buffer_start[] = {
        "A=from putenv", "B=", "PATH_LIKE=/bin:/usr/bin", "Z9=nine", "A",
        "LONG_ENVIRONMENT_NAME=put",
};

static char environment_buffers[6][40];

static const char address_to environment_kept[ENVIRONMENT_KEPT];
static char environment_kept_copy[ENVIRONMENT_KEPT][ENVIRONMENT_HELD];
static positive environment_kept_turn;

static positive environment_checks;
static positive environment_failures;

static char environment_said[1 << 16];
static positive environment_said_used;

static fn environment_flush(void)
{
#ifdef ENVIRONMENT_REFERENCE
        fwrite(environment_said, 1, (size_t)environment_said_used, stdout);
        fflush(stdout);
#else
        log((p8 address_to)environment_said, environment_said_used);
        log_flush();
#endif
        environment_said_used = 0;
}

static positive environment_length(const char address_to text)
{
        positive length = 0;

        while (text[length])
                length++;

        return length;
}

static fn environment_say_bytes(const char address_to text, positive length)
{
        for (positive at = 0; at < length; at++)
        {
                if (environment_said_used == sizeof(environment_said))
                        environment_flush();

                environment_said[environment_said_used++] = text[at];
        }
}

static fn environment_say(const char address_to text)
{
        environment_say_bytes(text, environment_length(text));
}

static fn environment_say_number(bipolar value)
{
        char digits[24];
        b32 count = 0;
        positive magnitude = value < 0 ? (positive)0 - (positive)value : (positive)value;

        if (value < 0)
                environment_say("-");

        do
                digits[count++] = (char)('0' + magnitude % 10);
        while (magnitude /= 10);

        while (count--)
                environment_say_bytes(address_of digits[count], 1);
}

static fn environment_say_name(const char address_to name)
{
        if (!name)
        {
                environment_say("(null)");
                return;
        }

        environment_say("[");
        environment_say(name);
        environment_say("]");
}

static fn environment_say_answer(b32 answer)
{
        environment_say(" -> ");
        environment_say_number(answer);

        if (answer)
        {
                environment_say(" errno ");
                environment_say_number(errno);
        }

        environment_say("\n");
}

static p64 environment_state;

static p64 environment_next(void)
{
        p64 z = (environment_state += 0x9e3779b97f4a7c15ULL);

        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
}

static positive environment_key_length(const char address_to entry)
{
        positive length = 0;

        while (entry[length] && entry[length] != '=')
                length++;

        return length;
}

//      Whether an entry's key, length bytes long, is this name.
static p8 environment_key_is_name(const char address_to entry, positive length,
                                    const char address_to name)
{
        for (positive at = 0; at < length; at++)
                if (entry[at] != name[at])
                        return false;

        return name[length] == 0 && entry[length] == '=';
}

static fn environment_show(void)
{
        const char address_to address_to walk = environment_entries();

        environment_say("  env");

        if (walk)
                for (; address_to walk; walk++)
                {
                        positive key = environment_key_length(address_to walk);

                        for (positive n = 0; n < environment_count(environment_names); n++)
                                if (environment_key_is_name(address_to walk, key, environment_names[n]))
                                {
                                        environment_say(" ");
                                        environment_say(address_to walk);
                                        break;
                                }
                }

        environment_say("\n");
}

static fn environment_agree(void)
{
        for (positive n = 0; n < environment_count(environment_names); n++)
        {
                const char address_to name = environment_names[n];
                positive length = environment_length(name);
                const char address_to found = environment_get(name);
                const char address_to first = 0;
                const char address_to address_to walk = environment_entries();

                if (walk)
                        for (; address_to walk && !first; walk++)
                                if (environment_key_length(address_to walk) == length &&
                                    environment_key_is_name(address_to walk, length, name))
                                        first = address_to walk + length + 1;

                environment_checks++;

                if (found != first)
                {
                        if (environment_failures < 8)
                        {
                                environment_say("FAIL getenv ");
                                environment_say_name(name);
                                environment_say(" is not the first entry with that key\n");
                        }

                        environment_failures++;
                }
        }
}

static fn environment_keep(const char address_to value)
{
        positive slot = environment_kept_turn++ % ENVIRONMENT_KEPT;
        positive at = 0;

        environment_kept[slot] = value;

        for (; value[at] && at + 1 < ENVIRONMENT_HELD; at++)
                environment_kept_copy[slot][at] = value[at];

        environment_kept_copy[slot][at] = 0;
}

static fn environment_step(void)
{
        p64 draw = environment_next();
        p64 more = environment_next();
        positive kind = draw % 100;

        if (kind < 30)
        {
                positive pick = more % 24;
                const char address_to name =
                        pick == 0   ? 0
                        : pick < 3  ? environment_bad_names[pick - 1]
                                    : environment_names[pick % environment_count(environment_names)];
                positive value = (more >> 8) % environment_count(environment_values);
                b32 overwrite = (b32)((more >> 16) & 1);

                environment_say("setenv ");
                environment_say_name(name);
                environment_say(" v");
                environment_say_number((bipolar)value);
                environment_say(overwrite ? " replace" : " keep");
                environment_say_answer(environment_set(name, environment_values[value], overwrite));
        }
        else if (kind < 45)
        {
                positive pick = more % 20;
                const char address_to name =
                        pick == 0   ? 0
                        : pick < 4  ? environment_bad_names[pick - 1]
                                    : environment_names[pick % environment_count(environment_names)];

                environment_say("unsetenv ");
                environment_say_name(name);
                environment_say_answer(environment_unset(name));
        }
        else if (kind < 60)
        {
                positive buffer = more % environment_count(environment_buffer_start);

                environment_say("putenv b");
                environment_say_number((bipolar)buffer);
                environment_say(" ");
                environment_say_name(environment_buffers[buffer]);
                environment_say_answer(environment_put(environment_buffers[buffer]));
        }
        else if (kind < 65)
        {
                positive buffer = more % environment_count(environment_buffer_start);
                char address_to text = environment_buffers[buffer];
                positive key = environment_key_length(text);
                positive length = environment_length(text);

                //      Only a value's bytes, never the key's or the end.
                if (text[key] == '=' && length > key + 1)
                {
                        positive at = key + 1 + (more >> 8) % (length - key - 1);

                        text[at] = (char)('a' + (more >> 20) % 26);
                        environment_say("write b");
                        environment_say_number((bipolar)buffer);
                        environment_say(" at ");
                        environment_say_number((bipolar)at);
                        environment_say(" ");
                        environment_say_name(text);
                        environment_say("\n");
                }
                else
                {
                        environment_say("write b");
                        environment_say_number((bipolar)buffer);
                        environment_say(" nothing to write\n");
                }
        }
        else if (kind < 68)
        {
                environment_say("clearenv");
                environment_say_answer(environment_clear());
        }
        else if (kind < 90)
        {
                positive pick = more % 16;
                const char address_to name =
                        pick == 0 ? "" : environment_names[pick % environment_count(environment_names)];
                const char address_to found = environment_get(name);

                environment_say("getenv ");
                environment_say_name(name);
                environment_say(" -> ");

                if (found)
                {
                        environment_say_name(found);

                        if ((more >> 8) & 1)
                                environment_keep(found);
                }
                else
                {
                        environment_say("(none)");
                }

                environment_say("\n");
        }
        else
        {
                positive slot = more % ENVIRONMENT_KEPT;
                const char address_to kept = environment_kept[slot];
                positive at = 0;

                environment_say("reread k");
                environment_say_number((bipolar)slot);

                if (!kept)
                {
                        environment_say(" empty\n");
                        return;
                }

                while (kept[at] && kept[at] == environment_kept_copy[slot][at])
                        at++;

                environment_say(kept[at] == environment_kept_copy[slot][at] ? " same " : " changed ");
                environment_say_name(kept);
                environment_say("\n");
        }
}

static fn environment_reset_buffers(void)
{
        for (positive buffer = 0; buffer < environment_count(environment_buffer_start); buffer++)
        {
                positive at = 0;

                for (; environment_buffer_start[buffer][at]; at++)
                        environment_buffers[buffer][at] = environment_buffer_start[buffer][at];

                environment_buffers[buffer][at] = 0;
        }
}

#ifdef ENVIRONMENT_REFERENCE
int main(void)
#else
b32 main(void)
#endif
{
        positive calls = 0;

        environment_state = 0x5851f42d4c957f2dULL;
        environment_reset_buffers();

        for (positive sequence = 0; sequence < ENVIRONMENT_SEQUENCES; sequence++)
        {
                environment_say("sequence ");
                environment_say_number((bipolar)sequence);
                environment_say("\n");

                for (positive call = 0; call < ENVIRONMENT_CALLS; call++, calls++)
                {
                        environment_step();
                        environment_agree();
                        environment_show();
                }

                //      Nothing of this sequence may be installed once its
                //      buffers are written back to how they started.
                environment_say("clearenv");
                environment_say_answer(environment_clear());
                environment_show();
                environment_reset_buffers();

                for (positive slot = 0; slot < ENVIRONMENT_KEPT; slot++)
                        environment_kept[slot] = 0;
        }

        environment_say("calls ");
        environment_say_number((bipolar)calls);
        environment_say("\n");
        environment_say("shared-end\n");
        environment_say("\n");
        environment_say_number((bipolar)environment_checks);
        environment_say(" checks, ");
        environment_say_number((bipolar)environment_failures);
        environment_say(" failures\n");
        environment_flush();

        return environment_failures ? 1 : 0;
}
#endif /* CHECK_environment */

