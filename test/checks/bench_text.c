/*
        test/checks/bench_text.c: the benchmarks of the ranged routines, text, numbers and the network's parsers.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef BENCH_ranged
/* The call-site entries at the shapes their callers have, against what the
   same callers got before -- memory_encode_power2_lines a 76-column base64
   block against the routine and then every line moved, and
   memory_cut_fields__room_64 over 4 MiB of records against the walk cut -f
   had, memory_reverse_lines__room_64 over src/lib.c against the walk rev
   had, memory_fold_lines__room_64 over the first 4 MiB of it against
   GNU's walk, memory_unique_lines__room_64 over its first MiB and over
   that with every line four times against the walk uniq had,
   memory_paste_lines__room_64 over its first MiB beside its second
   against the walk paste had, below -- and
   memory_copy_apart__room_64 over the lines of
   src/lib.c (every length to 128, in order) into a buffer nothing reads and
   into one buffer read at once, and at a fixed 40 and 76 bytes hidden from
   the compiler, against lib.util.c's memory_copy_apart (the inline pair
   under sixteen bytes, the routine above); x86_64 with AVX2 and with SSE2. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define LINES 65536

static p8 text[1 << 23];
static p8 out[LINES * 140 + 4096];
static p32 line_at[LINES], line_size[LINES];
static positive lines;
static volatile positive fixed_size[2] = {40, 76};
static volatile p64 sink;

/*      base64 at 76 columns: the encoder as it was -- every quantum, then each
        line moved up a byte for the newline before it, from the last line back
        -- against memory_encode_power2_lines. 57,000 bytes, a thousand lines. */
#define WRAP_LINES 1000
static p8 wrap_out[WRAP_LINES * 77 + 64];

static p64 wrap_run(positive side)
{
        static const string_address alphabet = (string_address) "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        positive groups = WRAP_LINES * 19, symbols = groups * 4, reserved = symbols + WRAP_LINES;
        p64 start = get_cpu_time();

        for (positive round = 0; round < 64; round++)
                if (side)
                        sink += memory_encode_power2_lines(wrap_out, text, groups, alphabet, 6, 19);
                else
                {
                        memory_encode_power2(wrap_out, text, groups, alphabet, 6);
                        positive left = symbols, ending = reserved, take = 0;

                        while (ending > left)
                        {
                                ending -= take;
                                left -= take;
                                memory_copy(wrap_out + ending, wrap_out + left, take);
                                wrap_out[--ending] = '\n';
                                take = min(left, (positive)76);
                        }
                        sink += wrap_out[ending];
                }
        return get_cpu_time() - start;
}

/*      cut -d: over 4 MiB of four-field records, -f2,4 and -f2: the walk
        cut -f had -- every boundary of the read from memory_offsets_of_either,
        a line's delimiters marked, then its kept fields copied with
        memory_copy_apart -- against memory_cut_fields__room_64. */
static p8 cut_text[(1 << 22) + 64], cut_out[(1 << 22) + 64];
static positive cut_length;
static p32 cut_offsets[4096], cut_marks[4096];
static const p8 cut_keep_24[5] = {0, 1, 0, 1, 0}, cut_keep_2[3] = {0, 1, 0};

static fn cut_records(void)
{
        static const string_address names[] = {(string_address) "alpha", (string_address) "beta"};
        static const string_address kinds[] = {(string_address) "plain", (string_address) "needle"};
        positive at = 0;

        for (positive i = 0; at + 64 < (1 << 22); i++)
        {
                string_address name = names[i % 3 == 0], kind = kinds[i % 97 == 0];
                p8 digits[24];
                positive n = i * 7919, d = 0;

                while (*name)
                        cut_text[at++] = (p8)*name++;
                cut_text[at++] = ':';
                cut_text[at++] = (p8)('0' + i / 100 % 10);
                cut_text[at++] = (p8)('0' + i / 10 % 10);
                cut_text[at++] = (p8)('0' + i % 10);
                cut_text[at++] = ':';
                while (*kind)
                        cut_text[at++] = (p8)*kind++;
                cut_text[at++] = ':';
                do
                        digits[d++] = (p8)('0' + n % 10);
                while (n /= 10);
                while (d)
                        cut_text[at++] = digits[--d];
                cut_text[at++] = '\n';
        }
        cut_length = at;
}

static positive cut_walk(p8 address_to into, const p8 address_to base, positive left, const p8 address_to keep,
                         positive cap)
{
        positive made = 0, line_start = 0, fields = 0, scan = 0;

        for (;;)
        {
                positive count = memory_offsets_of_either(cut_offsets, (address_any)(base + scan), left - scan, ':', '\n', 4096);

                for (positive i = 0; i < count; i++)
                {
                        positive ending = scan + cut_offsets[i];

                        if (base[ending] != '\n')
                        {
                                cut_marks[fields++] = (p32)ending;
                                continue;
                        }
                        positive from = line_start;
                        p8 wrote = false;

                        for (positive f = 0; f <= fields; f++)
                        {
                                positive stop = f < fields ? cut_marks[f] : ending;

                                if (keep[f < cap ? f : cap])
                                {
                                        if (wrote)
                                                into[made++] = ':';
                                        memory_copy_apart(into + made, base + from, stop - from);
                                        made += stop - from;
                                        wrote = true;
                                }
                                from = stop + 1;
                        }
                        into[made++] = '\n';
                        line_start = ending + 1;
                        fields = 0;
                }
                if (count < 4096)
                        break;
                scan += cut_offsets[count - 1] + 1;
        }
        return made;
}

static p64 cut_run(positive side, positive list)
{
        const p8 address_to keep = list ? cut_keep_2 : cut_keep_24;
        positive cap = list ? 2 : 4;
        p64 start = get_cpu_time();

        for (positive round = 0; round < 4; round++)
        {
                positive made = 0;

                if (side)
                        memory_cut_fields__room_64(cut_out, address_of made, cut_text, cut_length, keep,
                                                   ':' | '\n' << 8 | cap << 32);
                else
                        made = cut_walk(cut_out, cut_text, cut_length, keep, cap);
                sink += made + cut_out[made / 2];
        }
        return get_cpu_time() - start;
}

/*      rev over src/lib.c in bytes: the walk rev had -- every line end of the
        read from memory_offsets_of_either, each line copied and turned over
        in place with memory_reverse -- against memory_reverse_lines__room_64. */
static positive text_length;

static positive rev_walk(p8 address_to into, const p8 address_to base, positive left)
{
        positive made = 0, line_start = 0, scan = 0;

        for (;;)
        {
                positive count = memory_offsets_of_either(cut_offsets, (address_any)(base + scan), left - scan, '\n', '\n', 4096);

                for (positive i = 0; i < count; i++)
                {
                        positive ending = scan + cut_offsets[i], length = ending - line_start;

                        memory_copy_apart(into + made, base + line_start, length);
                        memory_reverse(into + made, length);
                        made += length;
                        into[made++] = '\n';
                        line_start = ending + 1;
                }
                if (count < 4096)
                        break;
                scan += cut_offsets[count - 1] + 1;
        }
        return made;
}

static p64 rev_run(positive side)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < 2; round++)
        {
                positive made = 0;

                if (side)
                        memory_reverse_lines__room_64(out, address_of made, text, text_length, '\n');
                else
                        made = rev_walk(out, text, text_length);
                sink += made + out[made / 2];
        }
        return get_cpu_time() - start;
}

/*      fold -w 60 over the whole lines of src/lib.c's first 4 MiB, in C: GNU's
        walk, a byte at a time with its column, against
        memory_fold_lines__room_64, whose output may be twice its input. */
static positive fold_length;

static positive fold_walk(p8 address_to into, const p8 address_to from, positive length, positive width)
{
        positive made = 0, column = 0, line = 0;

        for (positive at = 0; at < length; at++)
        {
                p8 c = from[at];

                if (c == '\n')
                {
                        into[made++] = c;
                        column = 0;
                        line = made;
                        continue;
                }
                positive after = c == '\t' ? (column | 7) + 1 : column + 1;
                if (after > width && made > line)
                {
                        into[made++] = '\n';
                        line = made;
                        after = c == '\t' ? 8 : 1;
                }
                into[made++] = c;
                column = after;
        }
        return made;
}

static p64 fold_run(positive side)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < 2; round++)
        {
                positive made = 0;

                if (side)
                        memory_fold_lines__room_64(out, address_of made, text, fold_length, 60, 0);
                else
                        made = fold_walk(out, text, fold_length, 60);
                sink += made + out[made / 2];
        }
        return get_cpu_time() - start;
}

/*      uniq over the whole lines of src/lib.c's first MiB, and of that with
        each line four times: the walk uniq had -- each line's end from
        memory_first_of, its length and memory_compare against the one
        before, and a line that differs copied -- against
        memory_unique_lines__room_64 from the first line on. */
static p8 uniq_text[(1 << 22) + 4096];
static positive uniq_once, uniq_four;

static positive uniq_walk(p8 address_to into, const p8 address_to base, positive left)
{
        positive made = 0, at = 0, previous = 0, previous_length = (positive)-1;

        while (at < left)
        {
                const p8 address_to found = memory_first_of((address_any)(base + at), '\n', left - at);

                if (!found)
                        break;
                positive length = (positive)(found - (base + at));
                if (length != previous_length || memory_compare(base + at, base + previous, length))
                {
                        memory_copy(into + made, base + at, length + 1);
                        made += length + 1;
                }
                previous = at;
                previous_length = length;
                at += length + 1;
        }
        return made;
}

static p64 uniq_run(positive side, positive shape)
{
        const p8 address_to base = shape ? uniq_text : text;
        positive length = shape ? uniq_four : uniq_once;
        p64 start = get_cpu_time();

        for (positive round = 0; round < 2; round++)
        {
                positive made = 0;

                if (side)
                {
                        positive first = (positive)((const p8 address_to)memory_first_of((address_any)base, '\n', length) - base);
                        positive state[2] = {1, first};

                        memory_copy(out, base, first + 1);
                        memory_unique_lines__room_64(out + first + 1, address_of made, base + first + 1, length - first - 1,
                                                     state, '\n' << 8);
                        made += first + 1;
                }
                else
                        made = uniq_walk(out, base, length);
                sink += made + out[made / 2];
        }
        return get_cpu_time() - start;
}

/*      paste of the lines of src/lib.c's first MiB beside those of its
        second: the walk paste had -- each line's end from memory_first_of,
        the line copied, a tab between and a newline after -- against
        memory_paste_lines__room_64. */
static positive paste_left, paste_right;

static positive paste_walk(p8 address_to into, const p8 address_to left, positive left_length,
                           const p8 address_to right, positive right_length)
{
        positive made = 0, l = 0, r = 0;

        for (;;)
        {
                const p8 address_to le = memory_first_of((address_any)(left + l), '\n', left_length - l);
                const p8 address_to re = memory_first_of((address_any)(right + r), '\n', right_length - r);

                if (!le || !re)
                        break;
                memory_copy(into + made, left + l, (positive)(le - (left + l)));
                made += (positive)(le - (left + l));
                into[made++] = '\t';
                memory_copy(into + made, right + r, (positive)(re - (right + r)));
                made += (positive)(re - (right + r));
                into[made++] = '\n';
                l = (positive)(le - left) + 1;
                r = (positive)(re - right) + 1;
        }
        return made;
}

static p64 paste_run(positive side)
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < 2; round++)
        {
                positive made, taken[2];

                if (side)
                        made = memory_paste_lines__room_64(out, taken, text, paste_left, text + paste_left, paste_right,
                                                           '\t' | '\n' << 8);
                else
                        made = paste_walk(out, text, paste_left, text + paste_left, paste_right);
                sink += made + out[made / 2];
        }
        return get_cpu_time() - start;
}

static p64 run(positive side, positive shape)
{
        p64 start = get_cpu_time(), sum = 0;
        positive made = 0;

        for (positive round = 0; round < 8; round++)
                for (positive i = 0; i < lines; i++)
                {
                        positive n = shape < 2 ? line_size[i] : fixed_size[shape - 2];
                        p8 address_to d = shape == 0 ? out + made : out;

                        if (side)
                                memory_copy_apart__room_64(d, text + line_at[i], n);
                        else
                                memory_copy_apart(d, text + line_at[i], n);
                        if (shape == 0)
                                made = made + n + 8 > LINES * 130 ? 0 : made + n + 8;
                        else
                                sum += d[n >> 1];
                }
        sink += sum;
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const string_address names[] = {"lines into a buffer nothing reads", "lines into one buffer read at once",
                                               "a fixed 40", "a fixed 76"};
        bipolar got = file_slurp((string_address) "src/lib.c", text, sizeof(text) - 4096);
        positive start = 0;

        for (positive i = 0; got > 0 && i < (positive)got && lines < LINES; i++)
                if (text[i] == '\n')
                {
                        if (i - start <= 128)
                                line_at[lines] = (p32)start, line_size[lines++] = (p32)(i - start);
                        start = i + 1;
                }
        if (!lines)
        {
                log("ranged: no src/lib.c to take lines from\n", 39);
                log_flush();
                return 1;
        }
        cut_records();
        text_length = (positive)got;
        fold_length = text_length < (positive)1 << 22 ? text_length : (positive)1 << 22;
        while (fold_length && text[fold_length - 1] != '\n')
                fold_length--;
        uniq_once = text_length < (positive)1 << 20 ? text_length : (positive)1 << 20;
        paste_left = uniq_once;
        while (paste_left && text[paste_left - 1] != '\n')
                paste_left--;
        paste_right = text_length - paste_left < (positive)1 << 20 ? text_length - paste_left : (positive)1 << 20;
        while (paste_right && text[paste_left + paste_right - 1] != '\n')
                paste_right--;
        while (uniq_once && text[uniq_once - 1] != '\n')
                uniq_once--;
        for (positive at = 0, line = 0; at < uniq_once; at++)
                if (text[at] == '\n')
                {
                        for (positive turn = 0; turn < 4; turn++)
                        {
                                memory_copy(uniq_text + uniq_four, text + line, at + 1 - line);
                                uniq_four += at + 1 - line;
                        }
                        line = at + 1;
                }
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier == 0 ? wide2 : 0;
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                if (tier == 0 && !wide2)
                        continue;
#endif
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, walked, wrap_run(0), lined, wrap_run(1));
                        string_format(log, "memory_encode_power2_lines base64 at 76 columns%s: paired median lines/walked %p.",
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, walked, rev_run(0), engine, rev_run(1));
                        string_format(log, "memory_reverse_lines__room_64 src/lib.c%s: paired median engine/walk %p.",
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, walked, fold_run(0), engine, fold_run(1));
                        string_format(log, "memory_fold_lines__room_64 -w 60 src/lib.c%s: paired median engine/walk %p.",
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, walked, paste_run(0), engine, paste_run(1));
                        string_format(log, "memory_paste_lines__room_64 src/lib.c%s: paired median engine/walk %p.",
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
                for (positive shape = 0; shape < 2; shape++)
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, walked, uniq_run(0, shape), engine, uniq_run(1, shape));
                        string_format(log, "memory_unique_lines__room_64 src/lib.c%s%s: paired median engine/walk %p.",
                                      shape ? (string_address) ", each line four times" : (string_address) "",
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
                for (positive list = 0; list < 2; list++)
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, walked, cut_run(0, list), engine, cut_run(1, list));
                        string_format(log, "memory_cut_fields__room_64 %s%s: paired median engine/walk %p.",
                                      list ? (string_address) "-f2" : (string_address) "-f2,4",
#if X64
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " sse2",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
                for (positive shape = 0; shape < 4; shape++)
                {
                        positive ratios[TRIES];

                        BENCH_PAIRED(ratios, shipped, run(0, shape), entry, run(1, shape));
                        string_format(log, "memory_copy_apart__room_64 %s%s: paired median entry/memory_copy_apart %p.", names[shape],
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
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide2;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_ranged */

#ifdef BENCH_tensor
/* The tensor block against the compiler: the same algorithm written in C for
   the vectoriser and built -O3 for the tier being timed (x86-64-v4 with 512-bit
   vectors for the AVX-512 body, x86-64-v3 for AVX2 + FMA, the baseline for
   SSE2; riscv64's with V for the V body), at the trainer's rows: H = 682 and
   1024, one row (L1), 64 rows back to back (L2) and, natively, 8192 (the
   training call, from memory). Paired medians of ASM/C, and each side's best
   ticks a call. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9

//      no-math-errno lets 1/sqrtf be the instruction, as a trainer's C
//      would compile it, rather than a call kept for errno.
#if X64
#define TENSOR_C_WIDE __attribute__((optimize("O3", "no-math-errno"), target("arch=x86-64-v4,prefer-vector-width=512")))
#define TENSOR_C_YMM __attribute__((optimize("O3", "no-math-errno"), target("arch=x86-64-v3")))
#elif RISCV64
#define TENSOR_C_WIDE __attribute__((optimize("O3", "no-math-errno"), target("arch=+v")))
#define TENSOR_C_YMM __attribute__((optimize("O3", "no-math-errno")))
#else
#define TENSOR_C_WIDE __attribute__((optimize("O3", "no-math-errno")))
#define TENSOR_C_YMM __attribute__((optimize("O3", "no-math-errno")))
#endif
#define TENSOR_C_BASE __attribute__((optimize("O3", "no-math-errno")))

static inline __attribute__((always_inline)) f32 tensor_c_float(p32 bits)
{
        f32 value;

        __builtin_memcpy(&value, &bits, 4);
        return value;
}

static inline __attribute__((always_inline)) p32 tensor_c_bits(f32 value)
{
        p32 bits;

        __builtin_memcpy(&bits, &value, 4);
        return bits;
}

//      swiglu_forward_f32's algorithm, as C: the overflow-free sigmoid with a
//      degree-6 exp and the Cephes split of ln2, whose products are exact
//      with or without an FMA to contract them into.
#define TENSOR_C_SWIGLU(name, attributes)                                                                \
        attributes static fn name(f32 *restrict out, const f32 *restrict gate, const f32 *restrict up,   \
                                  positive n)                                                            \
        {                                                                                                \
                for (positive i = 0; i < n; i++)                                                         \
                {                                                                                        \
                        f32 g = gate[i];                                                                 \
                        f32 a = __builtin_fabsf(g);                                                      \
                        a = a < 105.0f ? a : 105.0f;                                                     \
                        f32 z = a * 1.44269502f + 12583015.0f;                                           \
                        f32 m = z - 12583015.0f;                                                         \
                        f32 x = m * 0.693359375f - a;                                                    \
                        x = m * -2.12194440e-4f + x;                                                     \
                        f32 p = tensor_c_float(0x2eb55ce8u);                                             \
                        p = p * x + tensor_c_float(0x300936aeu);                                         \
                        p = p * x + tensor_c_float(0x312aac65u);                                         \
                        p = p * x + tensor_c_float(0x322aaa1fu);                                         \
                        p = p * x + tensor_c_float(0x32fffffdu);                                         \
                        p = p * x + tensor_c_float(0x33800000u);                                         \
                        p = p * x + tensor_c_float(0x33800000u);                                         \
                        f32 w = tensor_c_float(tensor_c_bits(z) << 23);                                  \
                        f32 numerator = tensor_c_bits(g) >> 31 ? p : w;                                  \
                        out[i] = g * (numerator / (w + p)) * up[i];                                      \
                }                                                                                        \
        }
TENSOR_C_SWIGLU(tensor_c_swiglu_wide, TENSOR_C_WIDE)
TENSOR_C_SWIGLU(tensor_c_swiglu_ymm, TENSOR_C_YMM)
TENSOR_C_SWIGLU(tensor_c_swiglu_base, TENSOR_C_BASE)

//      rmsnorm's two passes as C: 64 partial sums, which the vectoriser
//      keeps in registers without reassociating anything, then the scale.
#define TENSOR_C_RMSNORM(name, attributes)                                                               \
        attributes static f32 name(f32 *y, const f32 *x, const f32 *w, positive n, f32 eps)              \
        {                                                                                                \
                f32 acc[64] = {0};                                                                       \
                positive i = 0;                                                                          \
                for (; i + 64 <= n; i += 64)                                                             \
                        for (positive j = 0; j < 64; j++)                                                \
                                acc[j] += x[i + j] * x[i + j];                                           \
                for (positive j = 0; i + j < n; j++)                                                     \
                        acc[j] += x[i + j] * x[i + j];                                                   \
                for (positive s = 32; s; s /= 2)                                                         \
                        for (positive j = 0; j < s; j++)                                                 \
                                acc[j] += acc[j + s];                                                    \
                f32 r = 1.0f / __builtin_sqrtf(acc[0] * (1.0f / (f32)(n ? n : 1)) + eps);                \
                for (i = 0; i < n; i++)                                                                  \
                        y[i] = x[i] * w[i] * r;                                                          \
                return r;                                                                                \
        }
#define TENSOR_C_RMSNORM_BACK(name, attributes)                                                          \
        attributes static fn name(f32 *dx, f32 *restrict dw, const f32 *dy, const f32 *x, const f32 *w,  \
                                  positive n, f32 r)                                                     \
        {                                                                                                \
                f32 acc[64] = {0};                                                                       \
                positive i = 0;                                                                          \
                for (; i + 64 <= n; i += 64)                                                             \
                        for (positive j = 0; j < 64; j++)                                                \
                        {                                                                                \
                                f32 a = dy[i + j] * r;                                                   \
                                dw[i + j] += a * x[i + j];                                               \
                                acc[j] += a * w[i + j] * x[i + j];                                       \
                        }                                                                                \
                for (positive j = 0; i + j < n; j++)                                                     \
                {                                                                                        \
                        f32 a = dy[i + j] * r;                                                           \
                        dw[i + j] += a * x[i + j];                                                       \
                        acc[j] += a * w[i + j] * x[i + j];                                               \
                }                                                                                        \
                for (positive s = 32; s; s /= 2)                                                         \
                        for (positive j = 0; j < s; j++)                                                 \
                                acc[j] += acc[j + s];                                                    \
                f32 k = acc[0] * (r * r / (f32)(n ? n : 1));                                             \
                for (i = 0; i < n; i++)                                                                  \
                        dx[i] = dy[i] * r * w[i] - k * x[i];                                             \
        }
TENSOR_C_RMSNORM(tensor_c_rmsnorm_wide, TENSOR_C_WIDE)
TENSOR_C_RMSNORM(tensor_c_rmsnorm_ymm, TENSOR_C_YMM)
TENSOR_C_RMSNORM(tensor_c_rmsnorm_base, TENSOR_C_BASE)
TENSOR_C_RMSNORM_BACK(tensor_c_rmsnorm_back_wide, TENSOR_C_WIDE)
TENSOR_C_RMSNORM_BACK(tensor_c_rmsnorm_back_ymm, TENSOR_C_YMM)
TENSOR_C_RMSNORM_BACK(tensor_c_rmsnorm_back_base, TENSOR_C_BASE)

typedef fn (*tensor_c_swiglu)(f32 *restrict, const f32 *restrict, const f32 *restrict, positive);
typedef f32 (*tensor_c_rmsnorm)(f32 *, const f32 *, const f32 *, positive, f32);
typedef fn (*tensor_c_rmsnorm_back)(f32 *, f32 *restrict, const f32 *, const f32 *, const f32 *, positive, f32);

static f32 *tensor_out, *tensor_gate, *tensor_up, *tensor_w, *tensor_dw;
static tensor_c_swiglu tensor_former;
static tensor_c_rmsnorm tensor_former_norm;
static tensor_c_rmsnorm_back tensor_former_back;
static f32 tensor_sink;

static p64 run(p8 assembly, positive h, positive rows, positive reps)
{
        p64 start = get_cpu_time();

        for (positive r = 0; r < reps; r++)
                for (positive row = 0; row < rows; row++)
                        if (assembly)
                                swiglu_forward_f32(tensor_out + row * h, tensor_gate + row * h, tensor_up + row * h, h);
                        else
                                tensor_former(tensor_out + row * h, tensor_gate + row * h, tensor_up + row * h, h);
        return get_cpu_time() - start;
}

static fn tensor_hundredths(positive value)
{
        string_format(log, "%p.", value / 100);
        positive_to_padded(log, value % 100, 2, '0', 0);
}

static fn tensor_swiglu_shapes(string_address tier)
{
        static const positive widths[] = {682, 1024};
#if X64
        static const positive heights[] = {1, 64, 8192};
#else
        static const positive heights[] = {1, 64};
#endif

        for (positive w = 0; w < 2; w++)
                for (positive t = 0; t < sizeof(heights) / sizeof(heights[0]); t++)
                {
                        positive h = widths[w], rows = heights[t];
#if X64
                        positive reps = rows == 1 ? 4000 : rows == 64 ? 64 : 1;
#else
                        positive reps = rows == 1 ? 64 : 2;
#endif
                        positive ratios[TRIES];
                        p64 best_c = ~(p64)0, best_a = ~(p64)0;

                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former, assembly;

                                BENCH_ALTERNATE(trial, former, run(false, h, rows, reps), assembly, run(true, h, rows, reps));
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                        }
                        order(ratios, TRIES);
                        string_format(log, "swiglu_forward_f32 %p by %p, %s: paired median ASM/C ", h, rows, tier);
                        tensor_hundredths(ratios[TRIES / 2]);
                        string_format(log, "%%; best ticks a call ASM ");
                        tensor_hundredths((positive)(best_a * 100 / (rows * reps)));
                        string_format(log, ", C ");
                        tensor_hundredths((positive)(best_c * 100 / (rows * reps)));
                        log("\n", 1);
                }
}

//      rmsnorm rows: x (dy and x for the backward) from the big arrays, w and
//      dw one row each, as a trainer holds them. rows 0 is the chain: one
//      row in place, each call waiting on the last, which is the latency.
static p64 run_rmsnorm(p8 assembly, p8 backward, positive d, positive rows, positive reps)
{
        p64 start = get_cpu_time();
        positive each = rows ? rows : 1;

        for (positive r = 0; r < reps; r++)
                for (positive row = 0; row < each; row++)
                {
                        f32 *y = rows ? tensor_out + row * d : tensor_gate, *x = tensor_gate + row * d;

                        if (!backward)
                                tensor_sink += assembly ? rmsnorm_forward_f32(y, x, tensor_w, d, 1e-5f)
                                                        : tensor_former_norm(y, x, tensor_w, d, 1e-5f);
                        else if (assembly)
                                rmsnorm_backward_f32(y, tensor_dw, x, tensor_up + row * d, tensor_w, d, 1.0f);
                        else
                                tensor_former_back(y, tensor_dw, x, tensor_up + row * d, tensor_w, d, 1.0f);
                }
        return get_cpu_time() - start;
}

static fn tensor_rmsnorm_shapes(string_address tier)
{
        static const positive widths[] = {256, 1024};
#if X64
        static const positive heights[] = {0, 8, 64, 8192};
#else
        static const positive heights[] = {0, 8};
#endif

        for (positive backward = 0; backward < 2; backward++)
                for (positive w = 0; w < 2; w++)
                        for (positive t = 0; t < sizeof(heights) / sizeof(heights[0]); t++)
                        {
                                positive d = widths[w], rows = heights[t];
#if X64
                                positive reps = rows == 0 ? 8000 : rows == 8 ? 1000 : rows == 64 ? 128 : 1;
#else
                                positive reps = rows == 0 ? 64 : 8;
#endif
                                positive ratios[TRIES], calls = (rows ? rows : 1) * reps;
                                p64 best_c = ~(p64)0, best_a = ~(p64)0;

                                for (positive trial = 0; trial < TRIES; trial++)
                                {
                                        p64 former, assembly;

                                        BENCH_ALTERNATE(trial, former, run_rmsnorm(false, backward, d, rows, reps), assembly,
                                                        run_rmsnorm(true, backward, d, rows, reps));
                                        ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                                        best_c = former < best_c ? former : best_c;
                                        best_a = assembly < best_a ? assembly : best_a;
                                }
                                order(ratios, TRIES);
                                string_format(log, "%s %p by %p, %s: paired median ASM/C ",
                                              backward ? "rmsnorm_backward_f32" : "rmsnorm_forward_f32", d, rows, tier);
                                tensor_hundredths(ratios[TRIES / 2]);
                                string_format(log, "%%; best ticks a call ASM ");
                                tensor_hundredths((positive)(best_a * 100 / calls));
                                string_format(log, ", C ");
                                tensor_hundredths((positive)(best_c * 100 / calls));
                                log("\n", 1);
                        }
}

//      One shape on one tier, alone, for perf stat: 20000 calls of a row in
//      L1. "nothing" is the same program doing none, the baseline to subtract.
#if X64
static fn tensor_force(p32 tier)
{
        static p8 wide512 = 2, wide2, fused;

        if (wide512 == 2)
                wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;
        cpu_has_avx512 = tier == 0 ? wide512 : 0;
        cpu_has_avx2 = tier < 2 ? wide2 : 0;
        cpu_has_fma = tier < 2 ? fused : 0;
        tensor_former = tier == 0 ? tensor_c_swiglu_wide : tier == 1 ? tensor_c_swiglu_ymm : tensor_c_swiglu_base;
        tensor_former_norm = tier == 0 ? tensor_c_rmsnorm_wide : tier == 1 ? tensor_c_rmsnorm_ymm : tensor_c_rmsnorm_base;
        tensor_former_back = tier == 0 ? tensor_c_rmsnorm_back_wide
                             : tier == 1 ? tensor_c_rmsnorm_back_ymm
                                         : tensor_c_rmsnorm_back_base;
}
#else
//      Tier 0's C is the wide build (riscv64's with V), the others the base.
static fn tensor_force(p32 tier)
{
        tensor_former = tier == 0 ? tensor_c_swiglu_wide : tensor_c_swiglu_base;
        tensor_former_norm = tier == 0 ? tensor_c_rmsnorm_wide : tensor_c_rmsnorm_base;
        tensor_former_back = tier == 0 ? tensor_c_rmsnorm_back_wide : tensor_c_rmsnorm_back_base;
}
#endif
#define TENSOR_NAMED(id, h, tier, assembly)                                                              \
        static fn tensor_named_##id(void)                                                                \
        {                                                                                                \
                tensor_force(tier);                                                                      \
                run(assembly, h, 1, 20000);                                                              \
        }
//      rmsnorm alone: eight rows of 256 back to back, 2500 times (in L1).
#define TENSOR_NAMED_RMS(id, backward, tier, assembly)                                                   \
        static fn tensor_named_rms_##id(void)                                                            \
        {                                                                                                \
                tensor_force(tier);                                                                      \
                run_rmsnorm(assembly, backward, 256, 8, 2500);                                           \
        }
TENSOR_NAMED_RMS(zmm, false, 0, true)
TENSOR_NAMED_RMS(zmm_c, false, 0, false)
TENSOR_NAMED_RMS(ymm, false, 1, true)
TENSOR_NAMED_RMS(ymm_c, false, 1, false)
TENSOR_NAMED_RMS(xmm, false, 2, true)
TENSOR_NAMED_RMS(xmm_c, false, 2, false)
TENSOR_NAMED_RMS(back_zmm, true, 0, true)
TENSOR_NAMED_RMS(back_zmm_c, true, 0, false)
TENSOR_NAMED_RMS(back_ymm, true, 1, true)
TENSOR_NAMED_RMS(back_ymm_c, true, 1, false)
TENSOR_NAMED_RMS(back_xmm, true, 2, true)
TENSOR_NAMED_RMS(back_xmm_c, true, 2, false)
TENSOR_NAMED(682_zmm, 682, 0, true)
TENSOR_NAMED(682_zmm_c, 682, 0, false)
TENSOR_NAMED(682_ymm, 682, 1, true)
TENSOR_NAMED(682_ymm_c, 682, 1, false)
TENSOR_NAMED(682_xmm, 682, 2, true)
TENSOR_NAMED(682_xmm_c, 682, 2, false)
TENSOR_NAMED(1024_zmm, 1024, 0, true)
TENSOR_NAMED(1024_zmm_c, 1024, 0, false)
TENSOR_NAMED(1024_ymm, 1024, 1, true)
TENSOR_NAMED(1024_ymm_c, 1024, 1, false)
TENSOR_NAMED(1024_xmm, 1024, 2, true)
TENSOR_NAMED(1024_xmm_c, 1024, 2, false)

static fn tensor_named_nothing(void)
{
}

static bench_work tensor_named(string_address name)
{
        BENCH_NAME(name, "nothing", tensor_named_nothing);
        BENCH_NAME(name, "swiglu-682-zmm", tensor_named_682_zmm);
        BENCH_NAME(name, "swiglu-682-zmm-c", tensor_named_682_zmm_c);
        BENCH_NAME(name, "swiglu-682-ymm", tensor_named_682_ymm);
        BENCH_NAME(name, "swiglu-682-ymm-c", tensor_named_682_ymm_c);
        BENCH_NAME(name, "swiglu-682-xmm", tensor_named_682_xmm);
        BENCH_NAME(name, "swiglu-682-xmm-c", tensor_named_682_xmm_c);
        BENCH_NAME(name, "swiglu-1024-zmm", tensor_named_1024_zmm);
        BENCH_NAME(name, "swiglu-1024-zmm-c", tensor_named_1024_zmm_c);
        BENCH_NAME(name, "swiglu-1024-ymm", tensor_named_1024_ymm);
        BENCH_NAME(name, "swiglu-1024-ymm-c", tensor_named_1024_ymm_c);
        BENCH_NAME(name, "swiglu-1024-xmm", tensor_named_1024_xmm);
        BENCH_NAME(name, "swiglu-1024-xmm-c", tensor_named_1024_xmm_c);
        BENCH_NAME(name, "rmsnorm-256-zmm", tensor_named_rms_zmm);
        BENCH_NAME(name, "rmsnorm-256-zmm-c", tensor_named_rms_zmm_c);
        BENCH_NAME(name, "rmsnorm-256-ymm", tensor_named_rms_ymm);
        BENCH_NAME(name, "rmsnorm-256-ymm-c", tensor_named_rms_ymm_c);
        BENCH_NAME(name, "rmsnorm-256-xmm", tensor_named_rms_xmm);
        BENCH_NAME(name, "rmsnorm-256-xmm-c", tensor_named_rms_xmm_c);
        BENCH_NAME(name, "rmsnorm-back-256-zmm", tensor_named_rms_back_zmm);
        BENCH_NAME(name, "rmsnorm-back-256-zmm-c", tensor_named_rms_back_zmm_c);
        BENCH_NAME(name, "rmsnorm-back-256-ymm", tensor_named_rms_back_ymm);
        BENCH_NAME(name, "rmsnorm-back-256-ymm-c", tensor_named_rms_back_ymm_c);
        BENCH_NAME(name, "rmsnorm-back-256-xmm", tensor_named_rms_back_xmm);
        BENCH_NAME(name, "rmsnorm-back-256-xmm-c", tensor_named_rms_back_xmm_c);
        return null;
}

b32 main(void)
{
        positive floats = 8192 * 1024;

        tensor_out = memory(floats * 4), tensor_gate = memory(floats * 4), tensor_up = memory(floats * 4);
        tensor_w = memory(4096 * 4), tensor_dw = memory(4096 * 4);
        if (!tensor_out || !tensor_gate || !tensor_up || !tensor_w || !tensor_dw)
                return 1;
        p64 state = 0x9e3779b97f4a7c15ull;
        for (positive i = 0; i < floats; i++)
        {
                XORSHIFT64(state);
                tensor_gate[i] = (f32)((b64)(state >> 40) - (1 << 23)) * (6.0f / (f32)(1 << 23));
                tensor_up[i] = (f32)((b64)(state & 0xffffff) - (1 << 23)) * (2.0f / (f32)(1 << 23));
                tensor_out[i] = 0.0f;
        }
        //      w = 1 and r = 1 keep the chained rows normal: the forward
        //      normalises its row each call, the backward projects x out of it.
        for (positive i = 0; i < 4096; i++)
                tensor_w[i] = 1.0f, tensor_dw[i] = 0.0f;
        tensor_force(0);
        BENCH_RUN_NAMED(tensor_named);
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;

        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                cpu_has_fma = tier < 2 ? fused : 0;
                if ((tier == 0 && !wide512) || (tier == 1 && !(wide2 && fused)))
                        continue;
                tensor_force(tier);
                tensor_swiglu_shapes(tier == 0 ? "avx512" : tier == 1 ? "avx2 fma" : "sse2");
                tensor_rmsnorm_shapes(tier == 0 ? "avx512" : tier == 1 ? "avx2 fma" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2, cpu_has_fma = fused;
#elif RISCV64
        cpu_has_vector = 0;
        swiglu_forward_f32(tensor_out, tensor_gate, tensor_up, 1);
        p8 vector = cpu_has_vector;

        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                if (tier == 0 && vector != 2)
                        continue;
                tensor_former = tier == 0 ? tensor_c_swiglu_wide : tensor_c_swiglu_base;
                tensor_former_norm = tier == 0 ? tensor_c_rmsnorm_wide : tensor_c_rmsnorm_base;
                tensor_former_back = tier == 0 ? tensor_c_rmsnorm_back_wide : tensor_c_rmsnorm_back_base;
                tensor_swiglu_shapes(tier == 0 ? "V" : "no V");
                tensor_rmsnorm_shapes(tier == 0 ? "V" : "no V");
        }
        cpu_has_vector = vector;
#else
        tensor_force(0);
        tensor_swiglu_shapes("neon");
        tensor_rmsnorm_shapes("neon");
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_tensor */

#ifdef BENCH_sweep_coverage
/* memory_sweep_coverage against the loop it replaced in font_raster, over
   runs as long as a glyph's touched blocks are at 13, 24, 48, 200 and 400
   px: 8, 16, 24, 48, 160 and 400 cells, two in five of them an edge's. On
   x86_64 for each of its three bodies. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define RUNS 256

static p32 pattern[RUNS * 400], cells[RUNS * 400];
static p8 bytes[RUNS * 400];
static volatile p32 sink;

__attribute__((noinline, noclone)) static p32 former_sweep(p8 address_to into, p32 address_to row,
                                                           positive count, p32 sum)
{
        for (positive x = 0; x < count; x++)
        {
                sum += row[x];
                row[x] = 0;

                p32 area = (b32)sum < 0 ? -sum : sum;
                area = area > 0x200000 ? 0x200000 : area;
                into[x] = (p8)((area * 255 + 0x100000) >> 21);
        }
        return sum;
}

//      Each run starts from the cells the pattern holds, as a band does.
static p64 run(p8 assembly, positive count, positive rounds)
{
        p64 took = 0;

        for (positive round = 0; round < rounds; round++)
        {
                memory_copy(cells, pattern, RUNS * count * 4);
                p64 start = get_cpu_time();
                for (positive r = 0; r < RUNS; r++)
                        sink += assembly ? memory_sweep_coverage(bytes + r * count, cells + r * count, count, 0)
                                         : former_sweep(bytes + r * count, cells + r * count, count, 0);
                took += get_cpu_time() - start;
        }
        return took;
}

b32 main(void)
{
        static const positive counts[] = {8, 16, 24, 48, 160, 400};
        static p8 former_bytes[RUNS * 400];
        p64 seed = 0x9e3779b97f4a7c15ULL;

        for (positive i = 0; i < RUNS * 400; i++)
        {
                XORSHIFT64(seed);
                pattern[i] = seed % 5 < 2 ? (p32)((b32)(seed >> 32) >> 10) : 0;
        }
#if X64
        p8 wide = cpu_has_avx512, two = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier ? 0 : wide;
                cpu_has_avx2 = tier == 2 ? 0 : two;
#endif
                for (positive row = 0; row < sizeof(counts) / sizeof(counts[0]); row++)
                {
                        positive count = counts[row];
                        positive ratios[TRIES];
                        positive rounds = 2000000 / (RUNS * count) + 1;

                        memory_copy(cells, pattern, RUNS * count * 4);
                        for (positive r = 0; r < RUNS; r++)
                                memory_sweep_coverage(bytes + r * count, cells + r * count, count, 0);
                        memory_copy(cells, pattern, RUNS * count * 4);
                        for (positive r = 0; r < RUNS; r++)
                                former_sweep(former_bytes + r * count, cells + r * count, count, 0);
                        if (memory_compare(bytes, former_bytes, RUNS * count))
                                return 1;
                        BENCH_PAIRED_RUNS(ratios, former, assembly, count, rounds);
                        string_format(log, "memory_sweep_coverage %p cells%s: paired median ASM/C %p.", count,
#if X64
                                      cpu_has_avx512 ? (string_address) " avx512" :
                                      cpu_has_avx2 ? (string_address) " avx2" : (string_address) " scalar",
#else
                                      (string_address) "",
#endif
                                      ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                }
#if X64
        }
        cpu_has_avx512 = wide, cpu_has_avx2 = two;
#endif
        log_flush();
        return 0;
}
#endif /* BENCH_sweep_coverage */

#ifdef BENCH_escape
/* Shared escaping versus the former writer loops. Native timings only measure
   hardware; QEMU remains useful for checking equal bytes. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define MAXIMUM 65536
static p8 input[MAXIMUM + 1], output[MAXIMUM * 6 + 2], expected[MAXIMUM * 6 + 2];
static positive used;
#ifdef ESCAPE_BENCH_POLICY
static const p8 hex_policy = ESCAPE_BENCH_POLICY;
#else
static p8 hex_policy = 63;
#endif
static volatile positive sink;

__attribute__((noinline, noclone)) static fn capture(address_any data, positive length)
{
        memory_copy(output + used, data, length);
        used += length;
}

static fn former_hex(positive size, p8 policy)
{
        static const p8 categories[256] = {
            [0 ... 8] = HEX_CONTROL, [9] = HEX_TAB,
            [10 ... 31] = HEX_CONTROL, [' '] = HEX_SPACE,
            ['"'] = HEX_QUOTE, ['\\'] = HEX_SLASH, [127] = HEX_CONTROL,
            [128 ... 255] = HEX_HIGH,
        };
        positive start = 0;
        for (positive at = 0; at < size; at++)
        {
                if (!(categories[input[at]] & policy))
                        continue;
                if (at > start)
                        capture(input + start, at - start);
                p8 escaped[4] = {'\\', 'x'};
                memory_into_hex(escaped + 2, input + at, 1);
                capture(escaped, sizeof(escaped));
                start = at + 1;
        }
        if (size > start)
                capture(input + start, size - start);
}

static fn former_json(void)
{
        p8 address_to at = input;
        p8 address_to start = input;
        capture("\"", 1);
        while (*at)
        {
                p8 byte = *at;
                if (byte >= 32 && byte != '"' && byte != '\\')
                {
                        at++;
                        continue;
                }
                if (at > start)
                        capture(start, at - start);
                if (byte == '"' || byte == '\\')
                {
                        p8 escaped[2] = {'\\', byte};
                        capture(escaped, sizeof(escaped));
                }
                else
                {
                        p8 escaped[6] = {'\\', 'u', '0', '0'};
                        memory_into_hex(escaped + 4, &byte, 1);
                        capture(escaped, sizeof(escaped));
                }
                at++;
                start = at;
        }
        if (at > start)
                capture(start, at - start);
        capture("\"", 1);
}

static p64 run(p8 assembly, positive size, positive rounds, p8 json)
{
        p64 start = get_cpu_time();
        for (positive at = 0; at < rounds; at++)
        {
                used = 0;
                if (assembly)
                {
                        if (json) writer_json_string(capture, input);
                        else writer_hex_escaped(capture, input, size, hex_policy);
                }
                else if (json) former_json();
                else former_hex(size, hex_policy);
                sink += used;
        }
        return get_cpu_time() - start;
}

b32 main(void)
{
#ifndef ESCAPE_BENCH_POLICY
        if (program_argument_count() > 1)
        {
                positive policy = string_to_positive(program_argument(1));
                if (policy > 63) return 2;
                hex_policy = policy;
        }
#endif
        static const positive sizes[] = {1, 2, 3, 4, 6, 7, 8, 16, 20, 21,
                                         32, 128, 4096, MAXIMUM};
        static string_address shapes[] = {"plain", "sparse", "dense", "mixed", "high"};
        for (positive json = 0; json < 2; json++)
                for (positive shape = 0; shape < array_count(shapes); shape++)
                        for (positive row = 0; row < array_count(sizes); row++)
                        {
                                positive size = sizes[row], ratios[TRIES];
                                for (positive at = 0; at < size; at++)
                                {
                                        input[at] = 'a' + at % 26;
                                        if (shape == 1 && at % 127 == 7) input[at] = '\n';
                                        if (shape == 2) input[at] = 1 + at % 31;
                                        if (shape == 3 && at % 3) input[at] = at % 2 ? '\\' : 7;
                                        if (shape == 4) input[at] = 128 + at % 128;
                                }
                                input[size] = 0;
                                run(false, size, 1, json);
                                positive expected_size = used;
                                memory_copy(expected, output, used);
                                run(true, size, 1, json);
                                if (expected_size != used || memory_compare(expected, output, used))
                                        return 1;
                                positive rounds = (1u << 22) / size;
                                if (rounds > 100000) rounds = 100000;
                                BENCH_PAIRED_RUNS(ratios, former, assembly, size, rounds, json);
                                string_format(log, "%s policy%p %s %p bytes: paired median ASM/C %p.",
                                    json ? "JSON" : "hex", json ? 64 : hex_policy, shapes[shape], size,
                                    ratios[TRIES / 2] / 100);
                                positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                                log("%\n", 2);
                                log_flush();
                        }
        return 0;
}
#endif /* BENCH_escape */

#ifdef BENCH_spelled
/* memory_into_spelled against the C it replaces: cat -v's text_visible, a
   call per byte into a buffer with the literal runs found by a table scan,
   at the sizes the folded callers hand over -- a name, a sed l line, a whole
   read -- over printable text, sparse controls and random bytes. Native
   timings only measure hardware; QEMU remains useful for equal bytes. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define MAXIMUM 131072
static p8 input[MAXIMUM], output[MAXIMUM * 4 + 16], expected[MAXIMUM * 4 + 16];
static volatile positive sink;

static positive former_visible(p8 address_to into, p8 value)
{
        positive have = 0;
        if (value >= 128)
        {
                into[have++] = 'M';
                into[have++] = '-';
                value -= 128;
        }
        if (value == 127)
        {
                into[have++] = '^';
                into[have++] = '?';
        }
        else if (value < 32)
        {
                into[have++] = '^';
                into[have++] = (p8)(value + 64);
        }
        else
                into[have++] = value;
        into[have] = 0;
        return have;
}

//      cat_walked's -v loop as it stood: a printable run copied whole, and
//      every other byte through text_visible (tab and newline kept).
__attribute__((noinline, noclone)) static positive former(p8 address_to into,
                                                          p8 address_to at,
                                                          positive size)
{
        p8 address_to start = into;
        p8 address_to stop = at + size;
        while (at < stop)
        {
                p8 address_to run = at;
                while (run < stop && *run >= 32 && *run < 127)
                        run++;
                if (run > at)
                {
                        memory_copy_apart(into, at, (positive)(run - at));
                        into += run - at;
                        at = run;
                        continue;
                }
                p8 value = *at++;
                if (value == '\t' || value == '\n')
                        *into++ = value;
                else
                        into += former_visible(into, value);
        }
        return (positive)(into - start);
}

static p64 run(p8 assembly, positive size, positive rounds)
{
        p64 address_to table = spelling_caret(SPELL_SHOW);
        p64 start = get_cpu_time();
        for (positive at = 0; at < rounds; at++)
                sink += assembly ? memory_into_spelled(output, input, size,
                                                       sizeof(output), table).y
                                 : former(output, input, size);
        return get_cpu_time() - start;
}

b32 main(void)
{
        static const positive sizes[] = {1, 4, 8, 12, 16, 24, 32, 48, 64, 80,
                                         128, 511, 512, 1024, 4096, MAXIMUM};
        static string_address shapes[] = {"text", "sparse", "binary"};
        p64 seed = 0x9e3779b97f4a7c15ull;
        for (positive shape = 0; shape < array_count(shapes); shape++)
                for (positive row = 0; row < array_count(sizes); row++)
                {
                        positive size = sizes[row], ratios[TRIES];
                        for (positive at = 0; at < size; at++)
                        {
                                XORSHIFT64(seed);
                                input[at] = shape == 2 ? (p8)seed
                                            : shape == 1 && seed % 23 == 0 ? (p8)(seed >> 8)
                                                                           : (p8)(' ' + (seed >> 16) % 95);
                        }
                        positive want = former(expected, input, size);
                        positive2 got = memory_into_spelled(output, input, size, sizeof(output),
                                                            spelling_caret(SPELL_SHOW));
                        if (got.x != size || got.y != want || memory_compare(expected, output, want))
                                return 1;
                        positive rounds = (1u << 24) / size;
                        if (rounds > 200000) rounds = 200000;
                        BENCH_PAIRED_RUNS(ratios, before, after, size, rounds);
                        string_format(log, "spelled %s %p bytes: paired median ASM/C %p.",
                                      shapes[shape], size, ratios[TRIES / 2] / 100);
                        positive_to_padded(log, ratios[TRIES / 2] % 100, 2, '0', 0);
                        log("%\n", 2);
                        log_flush();
                }
        return 0;
}
#endif /* BENCH_spelled */

#ifdef BENCH_codec
/* Body-to-body timing, separate from the shell's streaming benchmark. The C
   references retain the folded engine's runtime-width bit loops. QEMU runs
   establish correctness only; their ticks are not hardware measurements. */
#include "../../src/lib.util.c"

#define NO_INLINE __attribute__((noinline, noclone))
static p8 input[5120], encoded[8192], decoded[5120], values[256];
static volatile positive sink;

NO_INLINE static positive encode_c(p8 address_to into, p8 address_to source,
                                   positive groups, string_address alphabet,
                                   positive width)
{
        positive bits = width == 9 ? 1 : width;
        positive bytes = bits == 6 ? 3 : bits == 5 ? 5 : 1;
        positive symbols = bytes * 8 / bits;
        for (positive group = 0; group < groups; group++)
        {
                positive value = 0;
                for (positive at = 0; at < bytes; at++)
                        value = (value << 8) | *source++;
                for (positive at = 0; at < symbols; at++)
                {
                        positive shift = width == 9 ? at : (symbols - at - 1) * bits;
                        *into++ = alphabet[(value >> shift) & (((positive)1 << bits) - 1)];
                }
        }
        return groups;
}

NO_INLINE static positive decode_c(p8 address_to into, p8 address_to source,
                                   positive groups, address_any table,
                                   positive width)
{
        positive bits = width == 9 ? 1 : width;
        positive bytes = bits == 6 ? 3 : bits == 5 ? 5 : 1;
        positive symbols = bytes * 8 / bits;
        p8 address_to map = table;
        positive accumulator = 0, held = 0;
        for (positive at = 0; at < groups * symbols; at++)
        {
                p8 value = map[source[at]];
                if (value == 255) return at / symbols;
                accumulator = width == 9 ? accumulator | ((positive)value << held)
                    : (accumulator << bits) | value;
                held += bits;
                if (held >= 8)
                {
                        held -= 8;
                        *into++ = (p8)(accumulator >> held);
                        accumulator &= ((positive)1 << held) - 1;
                }
        }
        return groups;
}

typedef positive (*codec_function)(address_any, address_any, positive,
                                    address_any, positive);
static positive measured(codec_function function, address_any into,
                          address_any source, positive groups,
                          address_any table, positive bits)
{
        positive best = positive_max;
        for (positive trial = 0; trial < 7; trial++)
        {
                positive start = get_cpu_time();
                for (positive round = 0; round < 2048; round++)
                        sink += function(into, source, groups, table, bits);
                positive elapsed = get_cpu_time() - start;
                if (elapsed < best) best = elapsed;
        }
        sink += ((p8 address_to)into)[0];
        return best;
}

b32 main(void)
{
        const char *alphabets[] = {
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", "0123456789ABCDEFGHIJKLMNOPQRSTUV",
            "0123456789ABCDEF", "01", "01",
        };
        p8 widths[] = {6, 6, 5, 5, 4, 1, 9};
        for (positive at = 0; at < sizeof(input); at++)
                input[at] = (p8)(at * 197 + at / 97);
#if X64
        p8 vector = cpu_has_avx2;
        p8 wide = cpu_has_avx512, vbmi = cpu_has_avx512_vbmi;
        for (positive mode = 0; mode < 3; mode++)
        {
                cpu_has_avx2 = mode ? vector : 0;
                cpu_has_avx512 = mode > 1 ? wide : 0;
                cpu_has_avx512_vbmi = mode > 1 ? vbmi : 0;
                string_format(log, "avx2=%p avx512=%p vbmi=%p\n", (positive)cpu_has_avx2,
                              (positive)cpu_has_avx512, (positive)cpu_has_avx512_vbmi);
#endif
        for (positive kind = 0; kind < sizeof(widths); kind++)
        {
                positive bits = widths[kind] == 9 ? 1 : widths[kind];
                memory_fill(values, 255, sizeof(values));
                for (positive at = 0; at < ((positive)1 << bits); at++)
                        values[(p8)alphabets[kind][at]] = (p8)at;
                for (positive groups = 1; groups <= 1024; groups *= 32)
                {
                        positive before = measured((codec_function)encode_c, encoded,
                            input, groups, (address_any)alphabets[kind], widths[kind]);
                        positive after = measured((codec_function)memory_encode_power2,
                            encoded, input, groups, (address_any)alphabets[kind], widths[kind]);
                        string_format(log, "encode kind=%p groups=%p C=%p ASM=%p ticks\n",
                                      kind, groups, before, after);
                        before = measured((codec_function)decode_c, decoded, encoded,
                                          groups, values, widths[kind]);
                        after = measured((codec_function)memory_decode_power2, decoded,
                                         encoded, groups, values, widths[kind]);
                        string_format(log, "decode kind=%p groups=%p C=%p ASM=%p ticks\n",
                                      kind, groups, before, after);
                        log_flush();
                }
        }
#if X64
        }
        cpu_has_avx2 = vector;
        cpu_has_avx512 = wide;
        cpu_has_avx512_vbmi = vbmi;
#endif
        return 0;
}
#endif /* BENCH_codec */

#ifdef BENCH_fixed
#include "../../src/lib.util.c"

static p8 records[65536];
static volatile positive observed;

/* Independent C carry loop, with identical complete-record boundaries. */
static __attribute__((noinline)) positive series_c(
    p8 address_to into, positive room, positive length,
    positive first, positive finish, bipolar step)
{
        positive used = length;
        while (room - used >= length)
        {
                memory_copy_apart(into + used, into + used - length, length);
                positive carry = step < 0 ? (positive)0 - (positive)step : (positive)step;
                for (positive at = finish; carry && at > first;)
                {
                        p8 address_to byte = into + used + --at;
                        if (*byte == '.') continue;
                        b32 digit = *byte - '0', part = carry % 10;
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
        const bipolar steps[] = {1, -1, 25, -25};
        for (positive s = 0; s < array_count(steps); s++)
                for (positive pass = 0; pass < 4; pass++)
                {
                        p8 assembly = pass & 1;
                        positive started = get_cpu_time();
                        for (positive run = 0; run < 1000; run++)
                        {
                                memory_copy_apart(records, steps[s] < 0
                                    ? "9999999\n" : "0000000\n", 8);
                                positive used = assembly
                                    ? memory_decimal_series(records, sizeof(records), 8, 0, 7, steps[s])
                                    : series_c(records, sizeof(records), 8, 0, 7, steps[s]);
                                observed += used + records[used - 2];
                        }
                        positive elapsed = get_cpu_time() - started;
                        log(assembly ? "asm step " : "C step ", 0);
                        bipolar_to_string(log, steps[s]);
                        log(": ", 2);
                        positive_to_string(log, elapsed);
                        log(" ticks / 65536000 bytes\n", 0);
                }
        log_flush();
        return observed == 0;
}
#endif /* BENCH_fixed */

#ifdef BENCH_table
/* Time the production table renderer after its independent byte/line checks. */
#define TABLE_BENCHMARK
#define CHECK_table
#undef BENCH_table
#include "../checks.c"
#endif /* BENCH_table */

#ifdef BENCH_padded
/*
        The assembly padded-decimal writer against the exact C call shape it
        replaced: convert into twenty-four bytes, emit every pad byte in its
        own writer call, then emit the optional prefix and the digit run.

        A native run is hardware time. A qemu run is emulator work only.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define VALUE_COUNT 64
#define ROUNDS (1u << 15)
#define TRIES 7

static volatile positive sink;
static positive values[VALUE_COUNT];

NOT_INLINED fn discard_writer(address_any data, positive length)
{
        volatile p8 address_to bytes = (volatile p8 address_to)data;

        sink += length + bytes[0] + bytes[length - 1];
}

NOT_INLINED fn former_padded(writer write, positive value, positive width,
                             p8 pad, p8 prefix)
{
        p8 digits[24];
        positive length = positive_into(digits, value);
        positive occupied = length + (prefix != 0);

        while (pad && width > occupied)
        {
                write(address_of pad, 1);
                width--;
        }

        if (prefix)
                write(address_of prefix, 1);

        write(digits, length);
}

static fn make_values()
{
        positive state = 0x9e3779b97f4a7c15ull;

        for (positive i = 0; i < VALUE_COUNT; i++)
        {
                XORSHIFT64(state);
                values[i] = i < 32 ? i * 313u : state;
        }

        values[0] = 0;
        values[1] = 9;
        values[2] = 10;
        values[3] = 99;
        values[4] = 100;
        values[5] = 9999;
        values[6] = 10000;
        values[7] = positive_max;
}

BENCH_ONCE(former_once, (positive width, p8 prefix), ROUNDS, r,
           former_padded(discard_writer,
                         values[r & (VALUE_COUNT - 1)], width,
                         ' ', prefix))

BENCH_ONCE(assembly_once, (positive width, p8 prefix), ROUNDS, r,
           positive_to_padded(discard_writer,
                              values[r & (VALUE_COUNT - 1)], width,
                              ' ', prefix))

static fn row(string_address name, positive width, p8 prefix)
{
        p64 former = positive_max;
        p64 assembly = positive_max;

        // Alternate who runs first. Frequency changes over a long callback
        // row otherwise consistently reward the first implementation and can
        // turn the same binary from an apparent win into an apparent loss.
        BENCH_BEST_PAIRED(former, former_once(width, prefix),
                          assembly, assembly_once(width, prefix));

        string_format(log, "  %s: former-C %p  assembly %p  asm/C %p%%\n",
                      name, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

b32 main()
{
        make_values();
        moonwater_cpu_detect();

        string_format(log, "padded decimal writer, best of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        row((string_address)"width 0", 0, 0);
        row((string_address)"small pad to 6", 6, 0);
        row((string_address)"wide pad to 32", 32, 0);
        row((string_address)"signed pad to 32", 32, '-');
        log_flush();
        return 0;
}
#endif /* BENCH_padded */

#ifdef BENCH_input_bases
/*
        Bounded base-input parsing at the call shapes that were folded.

        The scalar columns are the exact fixed-base loops the shell used:
        octal has one contiguous digit range, hexadecimal folds two letter
        ranges, and base 36 is the generic classifier. Both columns are called
        through the same function-pointer shape so dispatch overhead is equal.

        Native runs are hardware ticks. Qemu runs are emulator work and only
        useful as regression ratios, never as hardware-floor measurements.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define SUBJECTS 64
#define ROUNDS (1u << 20)
#define TRIES 11

/*
        Hardware-counter mode builds one isolated side of one row:

          -DINPUT_BASE_PERF_ROW=1..5
          -DINPUT_BASE_PERF_ASSEMBLY=0|1

        Rows are octal short/word, hexadecimal short/word, then base 36.
        Both sides still use the same noinline indirect-call harness.
*/
#ifndef INPUT_BASE_PERF_ASSEMBLY
#define INPUT_BASE_PERF_ASSEMBLY 0
#endif

typedef positive (*fixed_parser)(string_address, positive, positive address_to);
typedef positive (*base_parser)(string_address, positive, positive,
                                positive address_to);

static p8 subjects[SUBJECTS][96];
static positive limits[SUBJECTS];
static volatile positive sink;

NOT_INLINED positive scalar_octal(string_address source, positive bound,
                                  positive address_to used)
{
        positive value = 0;
        positive at = 0;

        while (at < bound && source[at] >= '0' && source[at] <= '7')
        {
                value = (value << 3) + (positive)(source[at] - '0');
                at++;
        }

        if (used)
                address_to used = at;
        return value;
}

NOT_INLINED positive scalar_hex(string_address source, positive bound,
                                positive address_to used)
{
        positive value = 0;
        positive at = 0;

        while (at < bound)
        {
                p8 character = source[at];
                positive digit;

                if (character >= '0' && character <= '9')
                        digit = (positive)(character - '0');
                else if (character >= 'a' && character <= 'f')
                        digit = (positive)(character - 'a' + 10);
                else if (character >= 'A' && character <= 'F')
                        digit = (positive)(character - 'A' + 10);
                else
                        break;

                value = (value << 4) + digit;
                at++;
        }

        if (used)
                address_to used = at;
        return value;
}

NOT_INLINED positive scalar_generic(string_address source, positive bound,
                                    positive base, positive address_to used)
{
        positive value = 0;
        positive at = 0;

        while (at < bound)
        {
                p8 character = source[at];
                positive digit;

                if (character >= '0' && character <= '9')
                        digit = (positive)(character - '0');
                else if (character >= 'a' && character <= 'z')
                        digit = (positive)(character - 'a' + 10);
                else if (character >= 'A' && character <= 'Z')
                        digit = (positive)(character - 'A' + 10);
                else
                        break;

                if (digit >= base)
                        break;

                value = value * base + digit;
                at++;
        }

        if (used)
                address_to used = at;
        return value;
}

static fn make_subjects(positive base, positive maximum)
{
        for (positive which = 0; which < SUBJECTS; which++)
        {
                positive length = 1 + which % maximum;

                for (positive at = 0; at < length; at++)
                {
                        positive digit = (which * 13 + at * 17) % base;

                        subjects[which][at] =
                            (p8)(digit < 10 ? '0' + digit
                                            : ((at & 1) ? 'A' : 'a') + digit - 10);
                }

                // Included in the bound, so both parsers pay their stopping
                // classifier rather than timing only the all-valid fast path.
                subjects[which][length] = '@';
                limits[which] = length + 1;
        }
}

NOT_INLINED BENCH_ONCE(run_fixed, (fixed_parser parser), ROUNDS, round,
        {
                positive which = round & (SUBJECTS - 1);
                positive used;
                positive value;

                value = parser(subjects[which], limits[which], address_of used);
                sink += value + used;
        })

NOT_INLINED BENCH_ONCE(run_base, (base_parser parser, positive base), ROUNDS, round,
        {
                positive which = round & (SUBJECTS - 1);
                positive used;
                positive value;

                value = parser(subjects[which], limits[which], base,
                               address_of used);
                sink += value + used;
        })

static p64 median(p64 values[TRIES])
{
        for (positive at = 1; at < TRIES; at++)
        {
                p64 value = values[at];
                positive before = at;

                while (before && values[before - 1] > value)
                {
                        values[before] = values[before - 1];
                        before--;
                }

                values[before] = value;
        }

        return values[TRIES / 2];
}

#ifdef INPUT_BASE_PERF_ROW
NOT_INLINED static fn perf_fixed(fixed_parser parser)
{
        for (positive trial = 0; trial < TRIES; trial++)
                for (positive round = 0; round < ROUNDS; round++)
                {
                        positive which = round & (SUBJECTS - 1);
                        positive used;
                        positive value = parser(subjects[which], limits[which],
                                                address_of used);

                        sink += value + used;
                }
}

NOT_INLINED static fn perf_base(base_parser parser, positive base)
{
        for (positive trial = 0; trial < TRIES; trial++)
                for (positive round = 0; round < ROUNDS; round++)
                {
                        positive which = round & (SUBJECTS - 1);
                        positive used;
                        positive value = parser(subjects[which], limits[which],
                                                base, address_of used);

                        sink += value + used;
                }
}
#endif

static fn fixed_row(string_address name, fixed_parser scalar,
                    fixed_parser assembly, positive base, positive maximum)
{
        make_subjects(base, maximum);

        p64 scalar_samples[TRIES];
        p64 assembly_samples[TRIES];

        // Alternate order so frequency scaling or host contention cannot
        // consistently favor the scalar or assembly half of a row.
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 scalar_ticks;
                p64 assembly_ticks;

                BENCH_ALTERNATE(trial, scalar_ticks, run_fixed(scalar),
                                assembly_ticks, run_fixed(assembly));

                scalar_samples[trial] = scalar_ticks;
                assembly_samples[trial] = assembly_ticks;
        }

        p64 old = median(scalar_samples);
        p64 floor = median(assembly_samples);

        string_format(log,
                      "  %s: scalar %p  assembly %p  assembly/scalar %p%%\n",
                      name, (positive)old, (positive)floor,
                      (positive)(floor * 100 / (old ? old : 1)));
}

static fn base_row(string_address name, base_parser scalar, positive base,
                   positive maximum)
{
        make_subjects(base, maximum);

        p64 scalar_samples[TRIES];
        p64 assembly_samples[TRIES];

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 scalar_ticks;
                p64 assembly_ticks;

                BENCH_ALTERNATE(trial,
                                scalar_ticks, run_base(scalar, base),
                                assembly_ticks, run_base(string_digits_base_max, base));

                scalar_samples[trial] = scalar_ticks;
                assembly_samples[trial] = assembly_ticks;
        }

        p64 old = median(scalar_samples);
        p64 floor = median(assembly_samples);

        string_format(log,
                      "  %s: scalar %p  assembly %p  assembly/scalar %p%%\n",
                      name, (positive)old, (positive)floor,
                      (positive)(floor * 100 / (old ? old : 1)));
}

b32 main()
{
#ifdef INPUT_BASE_PERF_ROW
#if INPUT_BASE_PERF_ROW == 1
        make_subjects(8, 3);
#if INPUT_BASE_PERF_ASSEMBLY
        perf_fixed(string_digits_octal_escape_max);
#else
        perf_fixed(scalar_octal);
#endif
#elif INPUT_BASE_PERF_ROW == 2
        make_subjects(8, 22);
#if INPUT_BASE_PERF_ASSEMBLY
        perf_fixed(string_digits_octal_max);
#else
        perf_fixed(scalar_octal);
#endif
#elif INPUT_BASE_PERF_ROW == 3
        make_subjects(16, 2);
#if INPUT_BASE_PERF_ASSEMBLY
        perf_fixed(string_digits_hexadecimal_escape_max);
#else
        perf_fixed(scalar_hex);
#endif
#elif INPUT_BASE_PERF_ROW == 4
        make_subjects(16, 16);
#if INPUT_BASE_PERF_ASSEMBLY
        perf_fixed(string_digits_hexadecimal_max);
#else
        perf_fixed(scalar_hex);
#endif
#elif INPUT_BASE_PERF_ROW == 5
        make_subjects(36, 13);
#if INPUT_BASE_PERF_ASSEMBLY
        perf_base(string_digits_base_max, 36);
#else
        perf_base(scalar_generic, 36);
#endif
#else
#error INPUT_BASE_PERF_ROW must be 1 through 5
#endif
        return sink == (positive)-1;
#else
        string_format(log, "bounded base parsing, paired median of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        fixed_row((string_address)"octal escape, 1-3", scalar_octal,
                  string_digits_octal_escape_max, 8, 3);
        fixed_row((string_address)"octal word, 1-22", scalar_octal,
                  string_digits_octal_max, 8, 22);
        fixed_row((string_address)"hex escape, 1-2", scalar_hex,
                  string_digits_hexadecimal_escape_max, 16, 2);
        fixed_row((string_address)"hex word, 1-16", scalar_hex,
                  string_digits_hexadecimal_max, 16, 16);
        base_row((string_address)"base 36, 1-13", scalar_generic, 36, 13);
        log_flush();
        return 0;
#endif
}
#endif /* BENCH_input_bases */

#ifdef BENCH_fields
/*
        Contiguous fixed-width decimal fields against the C bodies they fold:
        a direct two-digit pair and the former scale/divide loops for six and
        nine places. Values stay inside each caller's proven field domain.

        Native results are hardware time. Qemu results are emulator work only.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define VALUE_COUNT 64
#define ROUNDS (1u << 17)
#define TRIES 7

static p8 output[24];
static positive values[VALUE_COUNT];
static volatile positive sink;

NOT_INLINED positive former_pair(p8 address_to into, positive value)
{
        into[0] = (p8)('0' + (value / 10) % 10);
        into[1] = (p8)('0' + value % 10);
        return 2;
}

NOT_INLINED positive former_scaled(p8 address_to into, positive value,
                                   positive scale)
{
        positive length = 0;

        while (scale)
        {
                into[length++] = (p8)('0' + (value / scale) % 10);
                scale /= 10;
        }

        return length;
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
        values[1] = 1;
        values[2] = 9;
        values[3] = 10;
        values[4] = 99;
        values[5] = 999999;
        values[6] = 999999999;
}

BENCH_ONCE(former_once, (positive width), ROUNDS, r,
        {
                positive value = values[r & (VALUE_COUNT - 1)];
                positive length;

                if (width == 2)
                        length = former_pair(output, value % 100);
                else if (width == 6)
                        length = former_scaled(output, value % 1000000, 100000);
                else
                        length = former_scaled(output, value % 1000000000, 100000000);

                sink += length + output[0] + output[width - 1];
        })

BENCH_ONCE(assembly_once, (positive width), ROUNDS, r,
        {
                positive value = values[r & (VALUE_COUNT - 1)];
                positive length;

                if (width == 2)
                {
                        value %= 100;
                        length = positive_into_pair(output, value);
                }
                else if (width == 6)
                {
                        value %= 1000000;
                        length = positive_into_padded(output, value, width, '0');
                }
                else
                {
                        value %= 1000000000;
                        length = positive_into_padded(output, value, width, '0');
                }

                sink += length + output[0] + output[width - 1];
        })

static fn row(string_address name, positive width)
{
        p64 former = positive_max;
        p64 assembly = positive_max;

        BENCH_BEST_PAIRED(former, former_once(width),
                          assembly, assembly_once(width));

        string_format(log, "  %s: former-C %p  assembly %p  asm/C %p%%\n",
                      name, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

b32 main()
{
        make_values();
        moonwater_cpu_detect();

        string_format(log, "contiguous padded decimal, best of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        row((string_address)"width 2", 2);
        row((string_address)"width 6", 6);
        row((string_address)"width 9", 9);
        log_flush();
        return 0;
}
#endif /* BENCH_fields */

#ifdef BENCH_human
/*
        Compact binary-size formatting at both public call shapes.

        The former column is the overflow-safe C policy removed from file.c:
        a power-of-1024 walk and upward rounding around the existing decimal
        primitives. The assembly column is the shared buffer or writer API.
        Both writer forms preserve the same callback boundaries.

        Native runs are hardware ticks. Qemu ratios are emulator work only.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define VALUE_COUNT 64
#define ROUNDS (1u << 16)
#define TRIES 7

static p8 output[8];
static positive values[4][VALUE_COUNT];
static volatile positive sink;

NOT_INLINED positive former_human_buffer(p8 address_to into, positive value)
{
        p8 units[7] = "BKMGTPE";
        positive divisor = 1;
        positive unit = 0;

        while (value / divisor >= 1024 && unit < 6)
        {
                divisor *= 1024;
                unit++;
        }

        if (!unit)
                return positive_into_string(into, value);

        positive quotient = value / divisor;
        positive remainder = value % divisor;
        positive length;

        if (quotient >= 10)
                length = positive_into(into, quotient + (remainder != 0));
        else
        {
                positive fraction = (remainder * 10 + divisor - 1) / divisor;

                if (fraction == 10)
                {
                        quotient++;
                        fraction = 0;
                }

                if (quotient >= 10)
                        length = positive_into(into, quotient);
                else
                {
                        length = positive_into(into, quotient);
                        into[length++] = '.';
                        length += positive_into(into + length, fraction);
                }
        }

        into[length++] = units[unit];
        into[length] = end;
        return length;
}

NOT_INLINED fn former_human_writer(writer write, positive value)
{
        p8 units[7] = "BKMGTPE";
        positive divisor = 1;
        positive unit = 0;

        while (value / divisor >= 1024 && unit < 6)
        {
                divisor *= 1024;
                unit++;
        }

        if (!unit)
                return positive_to_string(write, value);

        positive quotient = value / divisor;
        positive remainder = value % divisor;
        if (quotient >= 10)
        {
                positive_to_string(write, quotient + (remainder != 0));
                write(units + unit, 1);
                return;
        }

        positive fraction = (remainder * 10 + divisor - 1) / divisor;

        if (fraction == 10)
        {
                quotient++;
                fraction = 0;
        }

        positive_to_string(write, quotient);

        if (quotient < 10)
        {
                write(".", 1);
                positive_to_string(write, fraction);
        }

        write(units + unit, 1);
}

NOT_INLINED fn discard_writer(address_any data, positive length)
{
        volatile p8 address_to bytes = (volatile p8 address_to)data;

        sink += length + bytes[0] + bytes[length - 1];
}

static fn make_values()
{
        positive state = 0x9e3779b97f4a7c15ull;

        for (positive i = 0; i < VALUE_COUNT; i++)
        {
                XORSHIFT64(state);

                values[0][i] = state & 1023;
                values[1][i] = 1024 + state % (8 * 1024);
                values[2][i] = 10 * 1024 + state % (1014 * 1024);
                values[3][i] = state;
        }

        values[0][0] = 0;
        values[0][1] = 1023;
        values[1][0] = 1024;
        values[1][1] = 9 * 1024;
        values[1][2] = 9 * 1024 + 1;
        values[2][0] = 1024 * 1024 - 1;
        values[2][1] = 1024 * 1024;
        values[3][0] = positive_max;
        values[3][1] = ((positive)15 << 60) + 1;
}

BENCH_ONCE(buffer_once, (positive shape, p8 assembly), ROUNDS, r,
           sink += assembly
                       ? positive_into_human_1024_string(
                             output, values[shape][r & (VALUE_COUNT - 1)])
                       : former_human_buffer(
                             output, values[shape][r & (VALUE_COUNT - 1)]))

BENCH_ONCE(writer_once, (positive shape, p8 assembly), ROUNDS, r,
           if (assembly)
                   positive_to_human_1024(
                       discard_writer, values[shape][r & (VALUE_COUNT - 1)]);
           else
                   former_human_writer(
                       discard_writer, values[shape][r & (VALUE_COUNT - 1)]))

static fn row(string_address name, positive shape, p8 writer_form)
{
        p64 former = positive_max;
        p64 assembly = positive_max;

        BENCH_BEST_PAIRED(former,
                          writer_form ? writer_once(shape, false)
                                      : buffer_once(shape, false),
                          assembly,
                          writer_form ? writer_once(shape, true)
                                      : buffer_once(shape, true));

        string_format(log, "  %s: former-C %p  assembly %p  asm/C %p%%\n",
                      name, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

b32 main()
{
        make_values();
        moonwater_cpu_detect();

        string_format(log, "compact binary sizes, best of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        row((string_address)"buffer plain", 0, false);
        row((string_address)"buffer fractional", 1, false);
        row((string_address)"buffer integer", 2, false);
        row((string_address)"buffer mixed u64", 3, false);
        row((string_address)"writer plain", 0, true);
        row((string_address)"writer fractional", 1, true);
        row((string_address)"writer integer", 2, true);
        row((string_address)"writer mixed u64", 3, true);
        log_flush();
        return 0;
}
#endif /* BENCH_human */

#ifdef BENCH_human_nearest
/* dd's nearest human formatter: exact former C body against the ASM leaf. */
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define ROUNDS (1u << 18)
#define VALUE_COUNT 64
#define TRIES 7

static p8 output[9];
static volatile positive sink;

// Verbatim policy removed from tools.c. Keeping the whole body -- including
// decimal emission and suffix construction -- makes this a body-to-body
// measurement rather than a scaling-loop proxy.
NOT_INLINED positive former_dd_human(p8 address_to into, positive n, p8 binary)
{
        positive base = binary ? 1024 : 1000;
        positive amount = n;
        positive tenths = 0;
        positive rounding = 0;
        positive exponent = 0;
        p8 letters[11] = {0, 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q'};
        p8 digits[24];
        positive have = 0;
        positive used = 0;
        positive fraction = 0;
        p8 point = false;

        if (base <= amount)
        {
                do
                {
                        positive ten = (amount % base) * 10 + tenths;
                        positive two = (ten % base) * 2 + (rounding >> 1);

                        amount /= base;
                        tenths = ten / base;
                        rounding = two < base ? ((two + rounding) != 0)
                                              : 2 + (base < two + rounding);
                        exponent++;
                }
                while (base <= amount && exponent < 10);

                if (amount < 10)
                {
                        if (2 < rounding + (tenths & 1))
                        {
                                tenths++;
                                rounding = 0;
                                if (tenths == 10)
                                {
                                        amount++;
                                        tenths = 0;
                                }
                        }

                        if (amount < 10)
                        {
                                point = true;
                                fraction = tenths;
                                tenths = rounding = 0;
                        }
                }
        }

        if (5 < tenths + (0 < rounding + (amount & 1)))
        {
                amount++;
                if (amount == base && exponent < 10)
                {
                        exponent++;
                        point = true;
                        fraction = 0;
                        amount = 1;
                }
        }

        if (!amount)
                digits[have++] = '0';

        while (amount)
        {
                digits[have++] = (p8)('0' + amount % 10);
                amount /= 10;
        }

        while (have)
                into[used++] = digits[--have];

        if (point)
        {
                into[used++] = '.';
                into[used++] = (p8)('0' + fraction);
        }
        into[used++] = ' ';
        if (exponent)
                into[used++] = !binary && exponent == 1 ? 'k' : letters[exponent];
        if (binary && exponent)
                into[used++] = 'i';
        into[used++] = 'B';
        into[used] = end;
        return used;
}

static positive values[2][3][VALUE_COUNT];

static fn make_values()
{
        positive state = 0x9e3779b97f4a7c15ull;

        for (positive binary = 0; binary < 2; binary++)
        {
                positive base = binary ? 1024 : 1000;
                for (positive i = 0; i < VALUE_COUNT; i++)
                {
                        XORSHIFT64(state);
                        values[binary][0][i] = state % base;
                        values[binary][1][i] = base + state % (8 * base);
                        values[binary][2][i] = state;
                }

                values[binary][0][0] = 0;
                values[binary][0][1] = base - 1;
                values[binary][1][0] = base;
                values[binary][1][1] = 9 * base - 1;
                values[binary][2][0] = positive_max;
                values[binary][2][1] = binary ? 1023 * 1024 : 999999;
        }
}

static p64 run_once(p8 assembly, p8 binary, positive shape)
{
        p64 start = get_cpu_time();
        for (positive i = 0; i < ROUNDS; i++)
        {
                positive value = values[binary][shape][i & (VALUE_COUNT - 1)];
                positive length = assembly
                                      ? positive_into_human_nearest_string(output, value,
                                                                            binary)
                                      : former_dd_human(output, value, binary);
                sink += length + output[0] + output[length - 1];
        }
        return get_cpu_time() - start;
}

static fn row(string_address name, p8 binary, positive shape)
{
        p64 former = positive_max;
        p64 assembly = positive_max;

        BENCH_BEST_PAIRED(former, run_once(false, binary, shape),
                          assembly, run_once(true, binary, shape));

        string_format(log, "  %s: former-C %p  assembly %p  asm/C %p%%\n",
                      name, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

b32 main()
{
        make_values();
        moonwater_cpu_detect();

        string_format(log, "nearest human sizes, best of %p, %p calls\n",
                      (positive)TRIES, (positive)ROUNDS);
        row((string_address)"SI plain", false, 0);
        row((string_address)"SI fractional", false, 1);
        row((string_address)"SI mixed u64", false, 2);
        row((string_address)"IEC plain", true, 0);
        row((string_address)"IEC fractional", true, 1);
        row((string_address)"IEC mixed u64", true, 2);
        log_flush();
        return 0;
}
#endif /* BENCH_human_nearest */

#ifdef BENCH_hash_33
/* DJB2 byte hash: dependent scalar loop against four-byte assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 25)

static p8 block[MAXIMUM + 16];
static volatile positive sink;

NOT_INLINED static positive former_hash(address_any block_address, positive length)
{
        p8 address_to text = block_address;
        positive value = 5381;

        for (positive at = 0; at < length; at++)
                value = value * 33 + text[at];

        return value;
}

typedef positive (*hash_call)(address_any block_address, positive length);
static hash_call volatile hash_calls[2] = {former_hash, memory_hash_33};

NOT_INLINED static positive2 former_string_hash_length(string_address text)
{
        positive2 answer;

        answer.y = string_length(text);
        answer.x = memory_hash_33(text, answer.y);
        return answer;
}

typedef positive2 (*string_hash_call)(string_address text);
static string_hash_call volatile string_hash_calls[2] = {
    former_string_hash_length, string_hash_33_length};

static p8 correctness(void)
{
        static const positive offsets[] = {0, 1, 3, 7, 15, 4093, 4095};

        for (positive at = 0; at < sizeof(block); at++)
                block[at] = (p8)(at * 37 + 11);

        for (positive oi = 0; oi < sizeof(offsets) / sizeof(*offsets); oi++)
                for (positive length = 0; length <= 257; length++)
                {
                        positive offset = offsets[oi];

                        if (offset + length > sizeof(block))
                                continue;

                        if (memory_hash_33(block + offset, length) !=
                            former_hash(block + offset, length))
                                return false;
                }

        for (positive oi = 0; oi < sizeof(offsets) / sizeof(*offsets); oi++)
                for (positive length = 0; length <= 257; length++)
                {
                        positive offset = offsets[oi];
                        positive2 want;
                        positive2 got;

                        if (offset + length >= sizeof(block))
                                continue;

                        for (positive at = 0; at < length; at++)
                                block[offset + at] = (p8)(at % 251 + 1);
                        block[offset + length] = 0;
                        want = former_string_hash_length(
                            (string_address)(block + offset));
                        got = string_hash_33_length(
                            (string_address)(block + offset));

                        if (got.x != want.x || got.y != want.y)
                                return false;
                }

        return true;
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / (length ? length : 1);

        if (rounds < 8)
                rounds = 8;
        if (rounds > (1u << 22))
                rounds = 1u << 22;
        return rounds;
}

BENCH_ONCE(run, (p8 assembly, positive length, positive rounds), rounds, round,
           sink += hash_calls[assembly](block, length))

BENCH_ONCE(run_string, (p8 assembly, positive rounds), rounds, round,
        {
                positive2 answer = string_hash_calls[assembly]((string_address)block);

                sink += answer.x + answer.y;
        })

static fn row(positive length)
{
        positive ratios[TRIES];
        positive rounds = rounds_for(length);

        BENCH_PAIRED_RUNS(ratios, former, assembly, length, rounds);
        string_format(log, "  %p bytes  median asm/C %p.%p%%\n", length,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

static fn string_row(positive length)
{
        positive ratios[TRIES];
        positive rounds = rounds_for(length);

        for (positive at = 0; at < length; at++)
                block[at] = (p8)(at % 251 + 1);
        block[length] = 0;

        BENCH_PAIRED(ratios, former, run_string(false, rounds),
                     assembly, run_string(true, rounds));
        string_format(log, "  %p bytes  median one-pass/two-pass %p.%p%%\n",
                      length, ratios[TRIES / 2] / 100,
                      ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        static const positive sizes[] = {0, 1, 4, 8, 12, 16, 24,
                                         32, 64, 128, 256, 4096, MAXIMUM};

        if (!correctness())
        {
                string_format(log, "memory_hash_33 correctness failed\n");
                log_flush();
                return 1;
        }

        string_format(log, "memory_hash_33, paired median of %p\n",
                      (positive)TRIES);
        for (positive at = 0; at < sizeof(sizes) / sizeof(*sizes); at++)
                row(sizes[at]);

        string_format(log, "string_hash_33_length, paired median of %p\n",
                      (positive)TRIES);
        for (positive at = 0; at < sizeof(sizes) / sizeof(*sizes); at++)
                string_row(sizes[at]);

        log_flush();
        return 0;
}
#endif /* BENCH_hash_33 */

#ifdef BENCH_span_byte
/* Bounded equal-byte prefix: scalar caller loop against library assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 25)

static p8 block[MAXIMUM + 16];
static volatile positive sink;

NOT_INLINED static positive former_span(address_any address, p8 value,
                                        positive length)
{
        p8 address_to bytes = address;
        positive at = 0;

        while (at < length && bytes[at] == value)
                at++;

        return at;
}

NOT_INLINED static positive former_span_reverse(address_any address, p8 value,
                                                positive length)
{
        p8 address_to bytes = address;
        positive at = length;

        while (at && bytes[at - 1] == value)
                at--;

        return length - at;
}

typedef positive (*span_call)(address_any, p8, positive);
static span_call volatile span_calls[2] = {former_span, memory_span_byte};
static span_call volatile reverse_span_calls[2] = {
    former_span_reverse, memory_span_byte_reverse};

static p8 correctness(void)
{
        static const positive long_lengths[] = {255, 256, 257, 4095};

        for (positive value = 0; value < 256; value++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive length = 0; length <= 20; length++)
                                for (positive mismatch = 0; mismatch <= length; mismatch++)
                                {
                                        memory_fill(block + offset, (p8)value, length);
                                        if (mismatch < length)
                                                block[offset + mismatch] = (p8)(value + 1);
                                        if (memory_span_byte(block + offset, (p8)value,
                                                             length) != mismatch)
                                                return false;
                                        positive suffix = mismatch < length
                                                              ? length - mismatch - 1
                                                              : length;
                                        if ((memory_span_byte_reverse)(
                                                block + offset, (p8)value,
                                                length) != suffix)
                                                return false;
                                }

        for (positive value = 0; value < 256; value++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive li = 0;
                             li < sizeof(long_lengths) / sizeof(*long_lengths); li++)
                        {
                                positive length = long_lengths[li];
                                positive positions[] = {0, 1, 15, 16, length / 2,
                                                        length - 1, length};

                                for (positive pi = 0;
                                     pi < sizeof(positions) / sizeof(*positions); pi++)
                                {
                                        positive mismatch = positions[pi];

                                        memory_fill(block + offset, (p8)value, length);
                                        if (mismatch < length)
                                                block[offset + mismatch] = (p8)(value + 1);
                                        if (memory_span_byte(block + offset, (p8)value,
                                                             length) != mismatch)
                                                return false;
                                        positive suffix = mismatch < length
                                                              ? length - mismatch - 1
                                                              : length;
                                        if ((memory_span_byte_reverse)(
                                                block + offset, (p8)value,
                                                length) != suffix)
                                                return false;
                                }
                        }
        return true;
}

static positive rounds_for(positive n){positive r=TARGET_BYTES/(n?n:1);if(r<8)r=8;if(r>(1u<<22))r=1u<<22;return r;}
static p64 run(positive implementation, positive n, positive rounds)
{
        p64 start = get_cpu_time();
        while (rounds--) sink += span_calls[implementation](block, '0', n);
        return get_cpu_time() - start;
}
static p64 run_reverse(positive implementation, positive n, positive rounds)
{
        p64 start = get_cpu_time();
        while (rounds--)
                sink += reverse_span_calls[implementation](block, '0', n);
        return get_cpu_time() - start;
}

static fn row(positive n, positive shape)
{
        positive raw[TRIES], rounds = rounds_for(n);
        memory_fill(block, '0', n);
        if (shape && n) block[shape == 1 ? n - 1 : 0] = '1';
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 elapsed[2];
                for (positive i = 0; i < 2; i++)
                {
                        positive which = (i + trial) % 2;
                        elapsed[which] = run(which, n, rounds);
                }
                raw[trial] = elapsed[1] * 10000 / max(elapsed[0], (p64)1);
        }
        order(raw, TRIES);
        string_format(log, "  %s %p bytes  asm/C %p.%p%%\n",
                      shape == 2 ? "first" : shape ? "late" : "equal", n,
                      raw[TRIES / 2] / 100, raw[TRIES / 2] % 100);
}

static fn reverse_row(positive n, positive shape)
{
        positive raw[TRIES], rounds = rounds_for(n);
        memory_fill(block, '0', n);
        if (shape && n)
                block[shape == 1 ? 0 : n - 1] = '1';
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 elapsed[2];
                for (positive i = 0; i < 2; i++)
                {
                        positive which = (i + trial) % 2;
                        elapsed[which] = run_reverse(which, n, rounds);
                }
                raw[trial] = elapsed[1] * 10000 / max(elapsed[0], (p64)1);
        }
        order(raw, TRIES);
        string_format(log, "  %s %p bytes  asm/C %p.%p%%\n",
                      shape == 2 ? "last" : shape ? "early" : "equal", n,
                      raw[TRIES / 2] / 100, raw[TRIES / 2] % 100);
}

b32 main(void)
{
        static const positive sizes[]={0,1,2,4,8,16,24,32,64,128,256,4096,MAXIMUM};
        if(!correctness()){string_format(log,"memory_span_byte correctness failed\n");log_flush();return 1;}
        string_format(log,"memory_span_byte, paired median of %p\n",(positive)TRIES);
        for (positive i = 0; i < array_count(sizes); i++)
                for (positive shape = 0; shape < 3; shape++) row(sizes[i], shape);
        string_format(log, "memory_span_byte_reverse, paired median of %p\n",
                      (positive)TRIES);
        for (positive i = 0; i < array_count(sizes); i++)
                for (positive shape = 0; shape < 3; shape++)
                        reverse_row(sizes[i], shape);
        log_flush();return 0;
}
#endif /* BENCH_span_byte */

#ifdef BENCH_net_parsers
/*
        The network parsers' byte loops against the lib.c routines that
        could replace them, each timed at its real caller: the whole
        dhcp_walk over an options field, the whole KeyUsage parse, the whole
        request-component check. Each pair is two copies of the caller that
        differ only in the loop, in alternating order per trial; a row is
        the median lib.c/C time ratio, under 100% when lib.c wins.
*/
#include "../../src/lib.util.c"
#include "../../src/net/net.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 15
#define ROUNDS 200000

static volatile positive sink;

NOT_INLINED static bipolar walk_pad(p8 address_to region, positive size,
                                    p8 library)
{
        positive at = 0;

        while (at < size)
        {
                p8 option = region[at];

                if (option == DHCP_OPTION_END)
                {
                        at++;
                        if (library)
                                return memory_span_byte(region + at,
                                                        DHCP_OPTION_PAD,
                                                        size - at) ==
                                               size - at
                                           ? 0
                                           : -1;
                        for (; at < size; at++)
                                if (region[at] != DHCP_OPTION_PAD)
                                        return -1;
                        return 0;
                }
                if (option == DHCP_OPTION_PAD)
                {
                        at++;
                        continue;
                }
                if (at + 1 >= size || at + 2 + region[at + 1] > size)
                        return -1;
                at += 2 + region[at + 1];
        }
        return -1;
}

NOT_INLINED static bipolar key_usage(p8 address_to value, positive length,
                                     tls_cert address_to cert, p8 library)
{
        positive at = 0;
        positive stop = 0;
        p8 unused;
        p8 canonical_unused = 0;
        p8 final;

        if (tls_asn1_enter(value, length, 0x03, address_of at, address_of stop) ||
            stop != length || at >= stop)
                return TLS_FAIL;
        unused = value[at++];
        if (unused > 7 || at >= stop)
                return TLS_FAIL;
        final = value[stop - 1];
        if (!final)
                return TLS_FAIL;
        if (library)
                canonical_unused = (p8)bits_trailing_zeros(final);
        else
                while (!(final & 1))
                {
                        canonical_unused++;
                        final >>= 1;
                }
        if (unused != canonical_unused)
                return TLS_FAIL;
        if (stop - at > 2 ||
            (stop - at == 2 &&
             (value[at + 1] != 0x80 || !(value[at] & 0x08))))
                return TLS_FAIL;
        cert->digital_signature = (value[at] & 0x80) != 0;
        cert->key_cert_sign = (value[at] & 0x04) != 0;
        return TLS_OK;
}

static p8 host_set[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._";

NOT_INLINED static p8 host_valid(string_address text, p8 library)
{
        if (!string_get(text))
                return false;
        if (library)
                return !string_get(text + string_span_of_set(text, host_set));
        for (; string_get(text); text++)
        {
                p8 byte = string_get(text);

                if (byte_is_control(byte) || byte == 0x7f || byte == ' ' ||
                    (!byte_is_alnum(byte) && byte != '-' && byte != '.' &&
                     byte != '_'))
                        return false;
        }
        return true;
}

/* Target bytes outside the plain run: controls, space, '%', '\\', DEL and
   every byte above it. Built once; the '%' escapes still take the peel. */
static p8 target_reject[1 + 32 + 2 + 129];

NOT_INLINED static p8 target_plain(string_address text, p8 library)
{
        if (library)
        {
                for (;;)
                {
                        p8 byte;

                        text += string_span_without_set(text, target_reject);
                        byte = string_get(text);
                        if (!byte)
                                return true;
                        if (byte != '%')
                                return false;
                        text++;
                }
        }
        for (; string_get(text); text++)
        {
                p8 byte = string_get(text);

                if (byte_is_control(byte) || byte == 0x7f || byte == ' ' ||
                    byte == '\\' || byte > 0x7f)
                        return false;
        }
        return true;
}

static p8 offer[312];
static positive offer_size;
static p8 usage_digital[] = {0x03, 0x02, 0x07, 0x80};
static p8 usage_sign[] = {0x03, 0x02, 0x01, 0x06};
static string_address bench_text;

static positive run_row(positive which, p8 library)
{
        positive total = 0;
        tls_cert cert = {0};

        for (positive round = 0; round < ROUNDS; round++)
                switch (which)
                {
                case 0: total += (positive)walk_pad(offer, offer_size, library); break;
                case 1: total += (positive)key_usage(usage_digital, 4, &cert, library); break;
                case 2: total += (positive)key_usage(usage_sign, 4, &cert, library); break;
                case 3: total += host_valid(bench_text, library); break;
                default: total += target_plain(bench_text, library); break;
                }
        return total;
}

static fn row(string_address name, positive which)
{
        positive ratio[TRIES];
        positive c_answer = run_row(which, false);

        if (c_answer != run_row(which, true))
        {
                string_format(log, "  %s: lib.c and C disagree\n", name);
                return;
        }
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 elapsed[2];

                for (positive i = 0; i < 2; i++)
                {
                        positive library = (i + trial) & 1;
                        p64 started = get_cpu_time();

                        sink += run_row(which, library);
                        elapsed[library] = get_cpu_time() - started;
                }
                ratio[trial] = (positive)(elapsed[1] * 10000 /
                                          max(elapsed[0], (p64)1));
        }
        order(ratio, TRIES);
        {
                p8 fraction[3];

                positive_into_padded(fraction, ratio[TRIES / 2] % 100, 2, '0');
                fraction[2] = end;
                string_format(log, "  %s  lib.c/C %p.%s%%\n", name,
                              ratio[TRIES / 2] / 100, fraction);
        }
}

b32 main(void)
{
        positive at = 0;

        for (positive byte = 1; byte < 0x20; byte++)
                target_reject[at++] = (p8)byte;
        target_reject[at++] = ' ';
        target_reject[at++] = '%';
        target_reject[at++] = '\\';
        for (positive byte = 0x7f; byte < 0x100; byte++)
                target_reject[at++] = (p8)byte;

        /* A typical OFFER's options field: cookie-less options then END and
           the zero padding a 300-byte BOOTP minimum leaves. */
        {
                static const p8 options[] = {
                    53, 1, 2, 54, 4, 10, 0, 2, 2, 51, 4, 0, 0, 14, 16,
                    1, 4, 255, 255, 255, 0, 3, 4, 10, 0, 2, 2,
                    6, 4, 10, 0, 2, 3, 255};

                memory_copy(offer, options, sizeof options);
                offer_size = 300 - 240;
                row("dhcp_walk, END then 26 PAD bytes (300-byte packet)", 0);
                offer_size = sizeof offer;
                row("dhcp_walk, END then 278 PAD bytes (552-byte packet)", 0);
        }
        row("KeyUsage digitalSignature (7 trailing zeros)", 1);
        row("KeyUsage keyCertSign|cRLSign (1 trailing zero)", 2);
        bench_text = "example.com";
        row("Host example.com", 3);
        bench_text = "a-rather-long-subdomain.cdn.example-provider.net";
        row("Host 48 bytes", 3);
        bench_text = "/";
        row("target /", 4);
        bench_text = "/assets/js/app.bundle.min.js?v=20260928&lang=en";
        row("target 48 bytes", 4);
        bench_text = "/releases/download/v7.2.0/moonwater-x86_64-bootx64.efi.sha256?"
                     "response-content-disposition=attachment&filename=image";
        row("target 115 bytes", 4);
        return 0;
}
#endif /* BENCH_net_parsers */

#ifdef BENCH_net_hot
/* Production response parsing, TLS wire primitives, DHCP padding and queued
   datagram reception.
   The same harness builds against saved pre-change network sources, so the
   comparison measures complete callers rather than isolated byte scans. */
#include "../../src/lib.util.c"
#include "../../src/net/net.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

static volatile positive net_hot_sink;
static p8 net_hot_bytes[HTTP_HEAD_MAX];
static positive net_hot_size;
static positive net_hot_rounds;
static positive net_hot_kind;
static b32 net_hot_pair[2];
static p8 net_hot_iv[12] = {0x31, 0x41, 0x59, 0x26, 0x53, 0x58,
                            0x97, 0x93, 0x23, 0x84, 0x62, 0x64};
static p8 net_hot_dns_request[64];
static p8 net_hot_dns_reply[128];
static positive net_hot_dns_size;
static positive net_hot_dns_question;
static positive net_hot_dns_ttl;

__attribute__((noipa)) static positive net_hot_response(void)
{
        positive head = 0;
        http_response response;

        if (http_response_framing(net_hot_bytes, net_hot_size, address_of head,
                                   address_of response))
                return 0;
        return head + response.code + response.body_length;
}

__attribute__((noipa)) static positive net_hot_padding(void)
{
        dhcp_gathered gathered[256] = {0};
        p8 overload = 0;

        return dhcp_walk(net_hot_bytes, net_hot_size, gathered,
                          address_of overload) == 0;
}

/*      A TLS record's body under its traffic key: sealed where it lies, and a
        sealed copy opened where it lies, which is every byte of an HTTPS
        transfer once each way. */
static crypto_aesgcm_key net_hot_gcm;
static p8 net_hot_record[TLS_PLAINTEXT_MAX + 16];
static p8 net_hot_sealed[TLS_PLAINTEXT_MAX + 16];
static positive net_hot_body;

static fn net_hot_work(void)
{
        if (net_hot_kind == 8 || net_hot_kind == 9)
        {
                p8 nonce[12];
                p8 head[5];

                tls_record_header(head, TLS_CT_APP, net_hot_body + 16);
                for (positive round = 0; round < net_hot_rounds; round++)
                {
                        tls_nonce(net_hot_iv, round, nonce);
                        if (net_hot_kind == 8)
                                crypto_aesgcm_seal(address_of net_hot_gcm, nonce, head, 5,
                                                   net_hot_record, net_hot_body,
                                                   net_hot_record + net_hot_body);
                        else
                        {
                                tls_nonce(net_hot_iv, 0, nonce);
                                memory_copy(net_hot_record, net_hot_sealed,
                                            net_hot_body + 16);
                                if (!crypto_aesgcm_open(address_of net_hot_gcm, nonce, head, 5,
                                                        net_hot_record, net_hot_body,
                                                        net_hot_record + net_hot_body))
                                        system_call_1(syscall(exit_group), 2);
                        }
                        net_hot_sink += net_hot_record[round % (net_hot_body + 16)];
                }
                return;
        }
        if (net_hot_kind == 7)
        {
                for (positive round = 0; round < net_hot_rounds; round++)
                {
                        tls_record_header(net_hot_bytes, (p8)round,
                                          round * 257);
                        net_hot_sink += net_hot_bytes[4];
                }
                return;
        }
        if (net_hot_kind == 6)
        {
                for (positive round = 0; round < net_hot_rounds; round++)
                {
                        p32 found = 0;

                        net_hot_dns_reply[net_hot_dns_ttl] ^= 1;
                        net_hot_sink += (positive)dns_reply_result(
                            net_hot_dns_reply, net_hot_dns_size, 0x4312,
                            net_hot_dns_request, net_hot_dns_question, address_of found) +
                            found;
                }
                return;
        }
        if (net_hot_kind == 5)
        {
                p8 nonce[12];

                for (positive round = 0; round < net_hot_rounds; round++)
                {
                        tls_nonce(net_hot_iv, round * 0x9e3779b97f4a7c15ull,
                                  nonce);
                        net_hot_sink += nonce[round % sizeof nonce];
                }
                return;
        }
        if (net_hot_kind == 3 || net_hot_kind == 4)
        {
                p8 byte;

                for (positive round = 0; round < net_hot_rounds; round++)
                {
                        p8 sent;

                        if (net_hot_kind == 3)
                                sent = network_stream_send_all_for(
                                    net_hot_pair[0], (p8 address_to)"w", 1, 30, 0);
                        else
                        {
                                network_deadline deadline;

                                sent = network_deadline_begin(address_of deadline, 30, 0) &&
                                       network_stream_send_all_until(
                                           net_hot_pair[0], (p8 address_to)"w", 1,
                                           address_of deadline);
                        }
                        if (!sent || socket_receive(net_hot_pair[1], address_of byte,
                                                     1, MSG_DONTWAIT, null, null) != 1)
                                system_call_1(syscall(exit_group), 2);
                        net_hot_sink += byte;
                }
                return;
        }
        if (net_hot_kind == 2)
        {
                p8 bytes[32];

                for (positive round = 0; round < net_hot_rounds; round++)
                {
                        network_deadline deadline;

                        if (!network_deadline_begin(address_of deadline, 2, 0))
                                system_call_1(syscall(exit_group), 2);
                        for (positive at = 0; at < 64; at++)
                                if (socket_send(net_hot_pair[1], "q", 1,
                                                 MSG_DONTWAIT, null, 0) != 1)
                                        system_call_1(syscall(exit_group), 2);
                        for (positive at = 0; at < 64; at++)
                        {
                                bipolar got = network_receive_next(net_hot_pair[0],
                                    bytes, sizeof bytes, null, null,
                                    address_of deadline);

                                if (got != 1 || bytes[0] != 'q')
                                        system_call_1(syscall(exit_group), 2);
                                net_hot_sink += (positive)got;
                        }
                }
                return;
        }
        for (positive round = 0; round < net_hot_rounds; round++)
                net_hot_sink += net_hot_kind ? net_hot_padding()
                                            : net_hot_response();
}

static fn net_hot_row(string_address name)
{
        positive samples[9];
        positive units = net_hot_rounds * (net_hot_kind == 2 ? 64 : 1);

        net_hot_work();
        for (positive trial = 0; trial < array_count(samples); trial++)
        {
                p64 started = get_cpu_time();

                net_hot_work();
                samples[trial] = (positive)((get_cpu_time() - started) * 100 / units);
        }
        order(samples, array_count(samples));
        p8 fraction[3];
        positive median = samples[array_count(samples) / 2];

        positive_into_padded(fraction, median % 100, 2, '0');
        fraction[2] = 0;
        string_format(log, "  %s  %p.%s ticks/operation\n", name,
                      median / 100, fraction);
}

static fn net_hot_head(string_address prefix, positive padding)
{
        net_hot_size = string_length(prefix);
        memory_copy(net_hot_bytes, prefix, net_hot_size);
        if (padding)
        {
                memory_copy(net_hot_bytes + net_hot_size, "X-Large: ", 9);
                net_hot_size += 9;
                memory_fill(net_hot_bytes + net_hot_size, 'a', padding);
                net_hot_size += padding;
                memory_copy(net_hot_bytes + net_hot_size, "\r\n", 2);
                net_hot_size += 2;
        }
        memory_copy(net_hot_bytes + net_hot_size, "\r\n", 2);
        net_hot_size += 2;
        net_hot_rounds = max((positive)1024, (positive)(1 << 20) / net_hot_size);
        if (!net_hot_response())
                system_call_1(syscall(exit_group), 2);
}

b32 main(void)
{
        net_hot_head("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n", 0);
        net_hot_row("HTTP short length head");
        net_hot_head("HTTP/1.1 302 Found\r\nServer: moonwater\r\n"
                     "Location: /new?x=1\r\nContent-Length: 17\r\n"
                     "Cache-Control: no-cache\r\nX-Trace: abc123\r\n", 0);
        net_hot_row("HTTP redirect with five fields");
        net_hot_head("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n", 2048);
        net_hot_row("HTTP 2 KiB unknown value");
        net_hot_head("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n", 15000);
        net_hot_row("HTTP 15000-byte unknown value");

        net_hot_kind = 1;
        net_hot_rounds = 100000;
        net_hot_bytes[0] = DHCP_OPTION_END;
        memory_zero(net_hot_bytes + 1, 1023);
        net_hot_size = 27;
        net_hot_row("DHCP END and 26 PAD bytes");
        net_hot_size = 279;
        net_hot_row("DHCP END and 278 PAD bytes");

        net_hot_kind = 5;
        net_hot_rounds = 1000000;
        net_hot_row("TLS record nonce");

        {
                bipolar name = dns_write_name(net_hot_dns_request + DNS_HEADER,
                    sizeof net_hot_dns_request - DNS_HEADER,
                    (string_address)"example.com");
                positive at;

                net_hot_dns_question = (positive)name + 4;
                at = DNS_HEADER + net_hot_dns_question;
                network_store_16(net_hot_dns_request, 0x4312);
                network_store_16(net_hot_dns_request + 4, 1);
                network_store_16(net_hot_dns_request + DNS_HEADER + name,
                                 DNS_TYPE_A);
                network_store_16(net_hot_dns_request + DNS_HEADER + name + 2,
                                 DNS_CLASS_IN);
                memory_copy(net_hot_dns_reply, net_hot_dns_request, at);
                network_store_16(net_hot_dns_reply + 2, DNS_FLAG_RESPONSE);
                network_store_16(net_hot_dns_reply + 6, 1);
                net_hot_dns_reply[at++] = 0xc0;
                net_hot_dns_reply[at++] = DNS_HEADER;
                network_store_16(net_hot_dns_reply + at, DNS_TYPE_A);
                network_store_16(net_hot_dns_reply + at + 2, DNS_CLASS_IN);
                net_hot_dns_ttl = at + 4;
                network_store_32(net_hot_dns_reply + net_hot_dns_ttl, 300);
                network_store_16(net_hot_dns_reply + at + 8, 4);
                network_store_32(net_hot_dns_reply + at + 10, 0xc0000201);
                net_hot_dns_size = at + 14;
                net_hot_kind = 6;
                net_hot_rounds = 10000;
                net_hot_row("DNS compressed one-record reply");
        }
        net_hot_kind = 7;
        net_hot_rounds = 1000000;
        net_hot_row("TLS record header");
        {
                static const positive bodies[] = {64, 1400, TLS_PLAINTEXT_MAX};
                static const string_address sealing[] = {
                    "TLS record of 64 bytes sealed",
                    "TLS record of 1400 bytes sealed",
                    "TLS record of 16384 bytes sealed"};
                static const string_address opening[] = {
                    "TLS record of 64 bytes copied and opened",
                    "TLS record of 1400 bytes copied and opened",
                    "TLS record of 16384 bytes copied and opened"};
                p8 nonce[12];
                p8 head[5];

                crypto_aesgcm_prepare(address_of net_hot_gcm, net_hot_iv);
                for (positive at = 0; at < array_count(bodies); at++)
                {
                        net_hot_body = bodies[at];
                        net_hot_rounds = max((positive)64, (positive)(1 << 22) / net_hot_body);
                        for (positive byte = 0; byte < net_hot_body; byte++)
                                net_hot_sealed[byte] = (p8)(byte * 131 + 7);
                        tls_nonce(net_hot_iv, 0, nonce);
                        tls_record_header(head, TLS_CT_APP, net_hot_body + 16);
                        crypto_aesgcm_seal(address_of net_hot_gcm, nonce, head, 5,
                                           net_hot_sealed, net_hot_body,
                                           net_hot_sealed + net_hot_body);
                        net_hot_kind = 8;
                        net_hot_row(sealing[at]);
                        net_hot_kind = 9;
                        net_hot_row(opening[at]);
                }
        }
        log_flush();

        if (system_call_4(syscall(socketpair), AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK,
                          0, (positive)net_hot_pair))
                return 2;
        net_hot_kind = 2;
        net_hot_rounds = 128;
        net_hot_row("Queued 64-datagram burst, send and receive");
        socket_close(net_hot_pair[0]);
        socket_close(net_hot_pair[1]);
        if (system_call_4(syscall(socketpair), AF_UNIX, SOCK_STREAM, 0,
                          (positive)net_hot_pair))
                return 2;
        net_hot_rounds = 100000;
        net_hot_kind = 4;
        net_hot_row("Queued stream write with eager deadline, send and receive");
        net_hot_kind = 3;
        net_hot_row("Queued stream write with deferred deadline, send and receive");
        socket_close(net_hot_pair[0]);
        socket_close(net_hot_pair[1]);
        return 0;
}
#endif /* BENCH_net_hot */

#ifdef BENCH_utf8_valid
/*
        memory_utf8_valid_span against the C it replaced in grep_text_valid:
        eight ASCII bytes a copy and test, string_span_max over the ASCII set,
        and a strict decode a sequence at a time. Lines of grep's sizes, all
        ASCII and the survey's mixed line (Latin, CJK, Greek, Cyrillic, an
        emoji), whose valid prefix is the whole line in both. On x86_64 also
        with the AVX2 body turned off, so the walk under it is timed too.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define TARGET_BYTES (1u << 24)

static p8 block[1u << 17];
static volatile positive sink;

static PURE p8 former_decode(const p8 address_to at, positive size,
                             positive address_to length)
{
        p8 lead = at[0], low = 0x80, high = 0xbf;
        positive need;

        address_to length = 1;
        if (lead < 0x80)
                return 1;
        if (lead < 0xc2 || lead > 0xf4)
                return 0;
        if (lead <= 0xdf)
                need = 2;
        else if (lead <= 0xef)
        {
                need = 3;
                low = lead == 0xe0 ? 0xa0 : 0x80;
                high = lead == 0xed ? 0x9f : 0xbf;
        }
        else
        {
                need = 4;
                low = lead == 0xf0 ? 0x90 : 0x80;
                high = lead == 0xf4 ? 0x8f : 0xbf;
        }
        for (positive i = 1; i < need; i++)
        {
                if (i == size)
                        return 2;
                p8 byte = at[i];
                if (byte < (i == 1 ? low : 0x80) || byte > (i == 1 ? high : 0xbf))
                        return 0;
        }
        address_to length = need;
        return 1;
}

NOT_INLINED static positive former_valid(address_any bytes, positive length)
{
        const p8 address_to at = bytes;
        positive i = 0;

        while (i < length)
        {
                while (i + 8 <= length)
                {
                        p64 word;

                        memory_copy_apart(address_of word, at + i, 8);
                        if (word & 0x8080808080808080ull)
                                break;
                        i += 8;
                }
                if (i < length)
                        i += string_span_max(at + i, length - i, string_set_ascii);
                if (i == length)
                        break;

                positive size;

                if (former_decode(at + i, length - i, address_of size) != 1)
                        return false;
                i += size;
        }
        return true;
}

//      grep_text_valid as it is now, inlined where it is called: the one call
//      is the library's, as the former's one call is its own.
static inline positive library_valid(address_any bytes, positive length)
{
        return memory_utf8_valid_span(bytes, length).x == length;
}

static positive rounds_for(positive n){positive r=TARGET_BYTES/(n?n:1);if(r<64)r=64;if(r>(1u<<21))r=1u<<21;return r;}
static p64 run(p8 assembly, positive n, positive rounds)
{
        p64 start = get_cpu_time();
        if (assembly)
                while (rounds--) sink += library_valid(block, n);
        else
                while (rounds--) sink += former_valid(block, n);
        return get_cpu_time() - start;
}

static fn fill(positive n, p8 mixed)
{
        static const char line[] = "h\xc3\xa9llo w\xc3\xb6rld \xc3\xb1" "and\xc3\xba "
            "\xe6\xbc\xa2\xe5\xad\x97\xe3\x83\x86\xe3\x82\xad\xe3\x82\xb9\xe3\x83\x88 "
            "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac "
            "\xd1\x80\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9 "
            "\xd1\x82\xd0\xb5\xd0\xba\xd1\x81\xd1\x82 emoji \xf0\x9f\x99\x82 "
            "plain ascii words here ";
        static const char ascii[] = "        static positive rounds_for(positive n) { return n; } ";
        const char address_to from = mixed ? line : ascii;
        positive period = (mixed ? sizeof(line) : sizeof(ascii)) - 1;
        for (positive i = 0; i < n; i++)
                block[i] = (p8)from[i % period];
        //      Whole characters only: blank a sequence the size cuts.
        positive last = n;
        while (last && (block[last - 1] & 0xc0) == 0x80)
                last--;
        if (last && block[last - 1] >= 0xc0)
                for (positive i = last - 1; i < n; i++)
                        block[i] = ' ';
}

static fn row(positive n, p8 mixed)
{
        positive raw[TRIES], rounds = rounds_for(n);
        p64 best_asm = ~(p64)0, best_c = ~(p64)0;
        fill(n, mixed);
        if (library_valid(block, n) != 1 || former_valid(block, n) != 1)
        {
                string_format(log, "  %p bytes: not valid\n", n);
                return;
        }
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 wide, former;
                BENCH_BOTH_ORDERS(trial, wide, former, n, rounds);
                if (wide < best_asm) best_asm = wide;
                if (former < best_c) best_c = former;
                raw[trial] = wide * 10000 / max(former, (p64)1);
        }
        order(raw, TRIES);
        string_format(log, "  %s %p bytes  C %p  asm %p ticks/1000 calls  asm/C %p.%p%%\n",
                      mixed ? "mixed" : "ascii", n, best_c * 1000 / rounds,
                      best_asm * 1000 / rounds, raw[TRIES / 2] / 100,
                      raw[TRIES / 2] % 100);
}

#if X64
/*
        memory_utf8_span with the whole line as its count -- cut -c's test of
        a line, ${#x} -- its block body against the walk it had alone, which
        is the same routine with the feature byte cleared.
*/
static p64 span_run(p8 wide, positive n, positive rounds)
{
        p8 avx2 = cpu_has_avx2;
        if (!wide)
                cpu_has_avx2 = 0;
        p64 start = get_cpu_time();
        while (rounds--)
                sink += memory_utf8_span(block, n, positive_max).y;
        p64 elapsed = get_cpu_time() - start;
        cpu_has_avx2 = avx2;
        return elapsed;
}

static fn span_row(positive n, p8 mixed)
{
        positive raw[TRIES], rounds = rounds_for(n);
        p64 best_wide = ~(p64)0, best_walk = ~(p64)0;
        fill(n, mixed);
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 wide = span_run(true, n, rounds), walk = span_run(false, n, rounds);
                if (wide < best_wide) best_wide = wide;
                if (walk < best_walk) best_walk = walk;
                raw[trial] = wide * 10000 / max(walk, (p64)1);
        }
        order(raw, TRIES);
        string_format(log, "  %s %p bytes  walk %p  blocks %p ticks/1000 calls  blocks/walk %p.%p%%\n",
                      mixed ? "mixed" : "ascii", n, best_walk * 1000 / rounds,
                      best_wide * 1000 / rounds, raw[TRIES / 2] / 100,
                      raw[TRIES / 2] % 100);
}
#endif

//      What rev and wc asked for the ASCII run: eight bytes a copy and test,
//      then string_span_max over the ASCII set.
NOT_INLINED static positive former_ascii(address_any bytes, positive length)
{
        const p8 address_to at = bytes;
        positive i = 0;

        while (i + 8 <= length)
        {
                p64 word;

                memory_copy_apart(address_of word, at + i, 8);
                if (word & 0x8080808080808080ull)
                        break;
                i += 8;
        }
        if (i < length)
                i += string_span_max(at + i, length - i, string_set_ascii);
        return i;
}

NOT_INLINED static positive library_ascii(address_any bytes, positive length)
{
        return memory_ascii_span(bytes, length);
}

static p64 ascii_run(p8 assembly, positive n, positive rounds)
{
        p64 start = get_cpu_time();
        if (assembly)
                while (rounds--) sink += library_ascii(block, n);
        else
                while (rounds--) sink += former_ascii(block, n);
        return get_cpu_time() - start;
}

static fn ascii_row(positive n, p8 late)
{
        positive raw[TRIES], rounds = rounds_for(n);
        memory_fill(block, 'a', n);
        if (late && n)
                block[n - 1] = 0xc3;
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 wide, former;
                BENCH_BOTH_ORDERS(trial, wide, former, n, rounds);
                raw[trial] = wide * 10000 / max(former, (p64)1);
        }
        order(raw, TRIES);
        string_format(log, "  %s %p bytes  asm/C %p.%p%%\n", late ? "late" : "all", n,
                      raw[TRIES / 2] / 100, raw[TRIES / 2] % 100);
}

b32 main(void)
{
        static const positive sizes[] = {8, 16, 24, 31, 40, 64, 118, 256, 1024, 65536};
        string_format(log, "memory_ascii_span against the word loop and string_span_max, paired median of %p\n",
                      (positive)TRIES);
        for (positive i = 0; i < array_count(sizes); i++)
                for (positive late = 0; late < 2; late++)
                        ascii_row(sizes[i], late);
#if X64
        if (cpu_has_avx2)
        {
                string_format(log, "memory_utf8_span, count unbounded, paired median of %p\n",
                              (positive)TRIES);
                for (positive i = 0; i < array_count(sizes); i++)
                        for (positive mixed = 0; mixed < 2; mixed++)
                                span_row(sizes[i], mixed);
        }
#endif
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? avx512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : avx2;
                if (tier == 0 && !avx512)
                        continue;
                string_format(log, "memory_utf8_valid_span, %s, paired median of %p\n",
                              cpu_has_avx512 ? "AVX-512" : cpu_has_avx2 ? "AVX2" : "without AVX2",
                              (positive)TRIES);
#else
        {
                string_format(log, "memory_utf8_valid_span, paired median of %p\n",
                              (positive)TRIES);
#endif
                for (positive i = 0; i < array_count(sizes); i++)
                        for (positive mixed = 0; mixed < 2; mixed++)
                                row(sizes[i], mixed);
        }
        log_flush();
        return 0;
}
#endif /* BENCH_utf8_valid */

#ifdef BENCH_cells_ascii
/*
        Terminal cells from a printable run: the loop src/canvas/term.c's
        text_ascii ran before cells_from_ascii, against the loop it runs now,
        in cycles and instructions a cell from the processor's own counters,
        on every body the machine has. Both loops are here word for word, the
        wide-pair test and unpair included, and they are checked against each
        other over lines with pairs in them before anything is timed.

        A fresh line is one the run extends, so there is no guard; a guarded
        one is a line being written over, clean of pairs, so every cell pays
        for the question. The calls go round sixteen lines: on one line, a
        guard load lands on the stores of the call before it, which no
        terminal's does -- the line it writes over was drawn frames ago. Under qemu the counters are the emulator's, which
        is why `count SIDE TIER LENGTH GUARDED ROUNDS`, SIDE 0 for the old
        loop and 1 for the new, runs one loop and
        nothing else, for an instruction count taken from outside.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 7
#define BENCH_COLUMNS 180
#define BENCH_TARGET_CELLS (1u << 22)
#define BENCH_WIDE 128
#define BENCH_WIDE_RIGHT 256

struct bench_cell
{
        unsigned int character;
        unsigned char ink, paper;
        unsigned short flags;
};

#define BENCH_LINES 16
static struct bench_cell bench_lines[BENCH_LINES][BENCH_COLUMNS];
static p8 bench_text[BENCH_COLUMNS + 64];
static volatile positive sink;

static fn former_unpair(struct bench_cell address_to cells, unsigned int length,
                        unsigned int at)
{
        if (at >= length)
                return;

        if ((cells[at].flags & BENCH_WIDE) && at + 1 < length)
        {
                cells[at + 1].character = ' ';
                cells[at + 1].flags &= (unsigned short)~BENCH_WIDE_RIGHT;
        }

        if ((cells[at].flags & BENCH_WIDE_RIGHT) && at)
        {
                cells[at - 1].character = ' ';
                cells[at - 1].flags &= (unsigned short)~BENCH_WIDE;
        }
}

NOT_INLINED static positive former_cells(struct bench_cell address_to cells,
                                         const p8 address_to bytes, positive count,
                                         unsigned int first, unsigned int had,
                                         positive attribute)
{
        positive room = BENCH_COLUMNS - first, n = 0;

        for (;;)
        {
                unsigned int at = first + (unsigned int)n;
                positive word = attribute | bytes[n];

                if (at < had &&
                    (cells[at].flags & (BENCH_WIDE | BENCH_WIDE_RIGHT)))
                        former_unpair(cells, had, at);

                memory_copy(cells + at, address_of word, sizeof(word));
                n++;

                if (n == count || n == room || (unsigned int)bytes[n] - ' ' >= 95)
                        break;
        }

        return n;
}

NOT_INLINED static positive library_cells(struct bench_cell address_to cells,
                                          const p8 address_to bytes, positive count,
                                          unsigned int first, unsigned int had,
                                          positive attribute)
{
        positive room = BENCH_COLUMNS - first, n = 0;
        positive limit = count < room ? count : room, guarded = had - first;

        for (;;)
        {
                unsigned int at = first + (unsigned int)n;
                positive word = attribute | bytes[n];

                if (at < had &&
                    (cells[at].flags & (BENCH_WIDE | BENCH_WIDE_RIGHT)))
                        former_unpair(cells, had, at);

                memory_copy(cells + at, address_of word, sizeof(word));
                n++;

                if (n == limit || (unsigned int)bytes[n] - ' ' >= 95)
                        break;

                n += cells_from_ascii(cells + first + n, (address_any)(bytes + n),
                                      limit - n, guarded > n ? guarded - n : 0,
                                      attribute,
                                      (p64)(BENCH_WIDE | BENCH_WIDE_RIGHT) << 48);

                if (n == limit || (unsigned int)bytes[n] - ' ' >= 95)
                        break;
        }

        return n;
}

typedef positive (*cells_call)(struct bench_cell address_to, const p8 address_to,
                               positive, unsigned int, unsigned int, positive);
static cells_call volatile cells_calls[2] = {former_cells, library_cells};

#if X64
static const_string tier_names[] = {"widest", "AVX2", "word"};
static positive tier_count = 3;
static p8 saved_avx2, saved_avx512;
#else
static const_string tier_names[] = {"word"};
static positive tier_count = 1;
#endif

static fn tier_set(positive tier)
{
#if X64
        cpu_has_avx2 = tier < 2 ? saved_avx2 : 0;
        cpu_has_avx512 = tier < 1 ? saved_avx512 : 0;
#else
        (void)tier;
#endif
}

static p64 random_state = 0x2545f4914f6cdd1d;

static p64 random_next(void)
{
        XORSHIFT64(random_state);
        return random_state;
}

/* The two loops leave every line the same, pairs and all. */
static p8 correctness(void)
{
        static struct bench_cell start[BENCH_COLUMNS], after[BENCH_COLUMNS];

        for (positive tier = 0; tier < tier_count; tier++)
        {
                tier_set(tier);
                for (positive round = 0; round < 60000; round++)
                {
                        unsigned int first = (unsigned int)(random_next() % BENCH_COLUMNS);
                        unsigned int had = first + (unsigned int)(random_next() %
                                                                  (BENCH_COLUMNS - first + 1));
                        positive length = random_next() % (BENCH_COLUMNS + 20) + 1;
                        positive count = random_next() % 3 ? sizeof(bench_text)
                                                           : random_next() % 200 + 1;
                        positive attribute = (random_next() & 0x3fff) << 32;
                        positive density = random_next() % 4;

                        if (count > sizeof(bench_text))
                                count = sizeof(bench_text);
                        for (positive at = 0; at < sizeof(bench_text); at++)
                                bench_text[at] = (p8)(' ' + random_next() % 95);
                        if (length < sizeof(bench_text))
                                bench_text[length] = (p8)(random_next() % 32);

                        for (unsigned int at = 0; at < BENCH_COLUMNS; at++)
                        {
                                start[at].character = (unsigned int)('a' + at % 26);
                                start[at].ink = (unsigned char)at;
                                start[at].paper = 0;
                                start[at].flags = (unsigned short)(random_next() & 0x60);
                        }
                        for (unsigned int at = 0; at + 1 < BENCH_COLUMNS; at++)
                                if (density && random_next() % (4 << density) == 0)
                                {
                                        start[at].flags |= BENCH_WIDE;
                                        start[at + 1].flags |= BENCH_WIDE_RIGHT;
                                        at++;
                                }
                        if (random_next() % 8 == 0)
                                start[random_next() % BENCH_COLUMNS].flags |=
                                    random_next() & 1 ? BENCH_WIDE : BENCH_WIDE_RIGHT;

                        memory_copy(bench_lines[0], start, sizeof(start));
                        positive want = former_cells(bench_lines[0], bench_text, count, first, had,
                                                     attribute);
                        memory_copy(after, bench_lines[0], sizeof(after));
                        memory_copy(bench_lines[0], start, sizeof(start));
                        positive got = library_cells(bench_lines[0], bench_text, count, first, had,
                                                     attribute);

                        if (got != want || memory_compare(after, bench_lines[0], sizeof(after)))
                        {
                                string_format(log, "  %s body: first %p had %p length %p count %p: %p against %p\n",
                                              tier_names[tier], (positive)first, (positive)had,
                                              length, count, got, want);
                                tier_set(0);
                                return false;
                        }
                }
        }
        tier_set(0);
        return true;
}

static fn shape(positive length, positive guarded)
{
        for (positive at = 0; at < sizeof(bench_text); at++)
                bench_text[at] = (p8)(' ' + (at * 37) % 95);
        if (length < sizeof(bench_text))
                bench_text[length] = 0x1b;
        memory_fill(bench_lines, 0, sizeof(bench_lines));
        (void)guarded;
}

static fn run(positive side, positive length, positive guarded, positive rounds)
{
        unsigned int had = guarded ? BENCH_COLUMNS : 0;

        while (rounds--)
                sink += cells_calls[side](bench_lines[rounds & (BENCH_LINES - 1)],
                                          bench_text, sizeof(bench_text), 0, had,
                                          (positive)0x0701 << 32);
}

static bipolar counter_open(p64 config)
{
        struct
        {
                unsigned int type, size;
                p64 config, period, sample_type, read_format, flags;
                unsigned int wakeup, breakpoint_type;
                p64 breakpoint_address;
        } attributes;

        memory_fill(address_of attributes, 0, sizeof(attributes));
        attributes.size = sizeof(attributes);
        attributes.config = config;
        attributes.flags = (1u << 5) | (1u << 6);   // user space only

        return system_call_5(syscall(perf_event_open), (positive)address_of attributes,
                             0, (positive)-1, (positive)-1, 0);
}

static p64 counter_read(bipolar counter)
{
        p64 value = 0;

        if (counter >= 0)
                system_call_3(syscall(read), (positive)counter,
                              (positive)address_of value, sizeof(value));
        return value;
}

static bipolar cycles_counter = -1, instructions_counter = -1;

static fn thousandths(p64 value)
{
        p8 fraction[4];

        positive_into_padded(fraction, (positive)(value % 1000), 3, '0');
        fraction[3] = end;
        string_format(log, "%p.%s", (positive)(value / 1000), fraction);
}

static fn row(positive tier, positive length, positive guarded)
{
        static const_string sides[] = {"C", "term.c"};
        positive rounds = max(BENCH_TARGET_CELLS / length, (positive)64);
        p64 cycles[2][TRIES], instructions[2][TRIES], ticks[2][TRIES];
        p64 cells = (p64)rounds * length;

        tier_set(tier);
        shape(length, guarded);

        for (positive trial = 0; trial < TRIES; trial++)
                for (positive i = 0; i < 2; i++)
                {
                        positive side = (i + trial) % 2;
                        p64 c0 = counter_read(cycles_counter);
                        p64 i0 = counter_read(instructions_counter);
                        p64 t0 = get_cpu_time();

                        run(side, length, guarded, rounds);
                        ticks[side][trial] = get_cpu_time() - t0;
                        instructions[side][trial] = (counter_read(instructions_counter) - i0) * 1000 / cells;
                        cycles[side][trial] = (counter_read(cycles_counter) - c0) * 1000 / cells;
                }

        for (positive side = 0; side < 2; side++)
        {
                order(cycles[side], TRIES);
                order(instructions[side], TRIES);
                order(ticks[side], TRIES);
        }

        string_format(log, "  %s %s %p", tier_names[tier],
                      guarded ? "guarded" : "fresh  ", length);
        for (positive side = 0; side < 2; side++)
        {
                string_format(log, "  %s ", sides[side]);
                thousandths(cycles[side][TRIES / 2]);
                string_format(log, " cyc ");
                thousandths(instructions[side][TRIES / 2]);
                string_format(log, " ins");
        }
        string_format(log, "  time against C %p%%\n",
                      (positive)(ticks[1][TRIES / 2] * 100 /
                                 max(ticks[0][TRIES / 2], (p64)1)));
        tier_set(0);
}

static positive number(string_address text)
{
        positive value = 0;

        while (*text >= '0' && *text <= '9')
                value = value * 10 + (positive)(*text++ - '0');
        return value;
}

b32 main(void)
{
        static const positive lengths[] = {2, 4, 8, 16, 32, 64, 180};
        string_address address_to arguments = program_argument_list();

#if X64
        saved_avx2 = cpu_has_avx2;
        saved_avx512 = cpu_has_avx512;
#endif
        if (program_argument_count() == 7 && arguments[1][0] == 'c')
        {
                positive side = number(arguments[2]) & 1;
                positive tier = number(arguments[3]);
                positive length = number(arguments[4]);
                positive guarded = number(arguments[5]);

                tier_set(tier < tier_count ? tier : 0);
                shape(length, guarded);
                run(side, length, guarded, number(arguments[6]));
                return 0;
        }

        if (!correctness())
        {
                string_format(log, "cells_from_ascii: the two loops disagree\n");
                log_flush();
                return 1;
        }

        cycles_counter = counter_open(0);
        instructions_counter = counter_open(1);
        string_format(log, "cells_from_ascii against term.c's loop, per cell, median of %p%s\n",
                      (positive)TRIES,
                      cycles_counter < 0 ? " (no counters here: the cycle and instruction columns are zero)" : "");

        for (positive tier = 0; tier < tier_count; tier++)
                for (positive guarded = 0; guarded < 2; guarded++)
                        for (positive i = 0; i < array_count(lengths); i++)
                                row(tier, lengths[i], guarded);

        log_flush();
        return 0;
}
#endif /* BENCH_cells_ascii */

#if defined(BENCH_fill_u32) || defined(BENCH_fill_u64)
/*
        A span fill against its scalar and bulk-store floors: the 32-bit
        primitive the Canvas and windows share (BENCH_fill_u32) and the
        naturally aligned 64-bit pattern fill (BENCH_fill_u64). Each section
        names its subject, pattern and sizes and writes its two floors; the
        correctness pass, the paired trials and the report are one.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define TRIES 9
#define TARGET_BYTES (1u << 26)
#define MAXIMUM 4096

#ifdef BENCH_fill_u32
#define FILL_NAME "memory_fill_u32"
#define FILL_SUBJECT memory_fill_u32
#define FILL_PATTERN 0x13579bdfu
#define FILL_JUNK 0xa5a5a5a5u
#define FILL_SIZES                                                            \
        1, 2, 3, 4, 8, 16, 32, 64, 96, 128, 160, 192, 200, 208, 216, 224,     \
        232, 240, 248, 256, 320, 384, 512, 1024, 4096

typedef unsigned int fill_word;
static fill_word block[MAXIMUM + 2] __attribute__((aligned(64)));

/* REP pays setup; the scalar body pays a back edge per four words. */
__asm__(
    ".text\n"
    ASM_FUNC(fill_u32_scalar_floor)
#if X64
    "mov %edx, %eax\n   mov %eax, %r8d\n   shl $32, %r8\n   or %r8, %rax\n"
    "cmp $4, %rsi\n   jb 2f\n"
    "1:  mov %rax, (%rdi)\n   mov %rax, 8(%rdi)\n   add $16, %rdi\n"
    "sub $4, %rsi\n   cmp $4, %rsi\n   jae 1b\n"
    "2:  test $2, %sil\n   jz 3f\n   mov %rax, (%rdi)\n   add $8, %rdi\n"
    "3:  test $1, %sil\n   jz 4f\n   mov %eax, (%rdi)\n"
    "4:\n"
#elif ARM64
    "mov w2, w2\n   orr x3, x2, x2, lsl #32\n"
    "1:  cmp x1, #4\n   b.lo 2f\n   stp x3, x3, [x0], #16\n"
    "sub x1, x1, #4\n   b 1b\n"
    "2:  tbz x1, #1, 3f\n   str x3, [x0], #8\n"
    "3:  tbz x1, #0, 4f\n   str w2, [x0]\n   4:\n"
#else
    "slli a2, a2, 32\n   srli a2, a2, 32\n   slli t0, a2, 32\n"
    "or t0, t0, a2\n   li t1, 2\n"
    "1:  blt a1, t1, 2f\n   sd t0, 0(a0)\n   addi a0, a0, 8\n"
    "addi a1, a1, -2\n   j 1b\n"
    "2:  beqz a1, 3f\n   sw a2, 0(a0)\n   3:\n"
#endif
    ASM_RET
    ASM_END(fill_u32_scalar_floor)
    ASM_FUNC(fill_u32_bulk_floor)
#if X64
    "mov %edx, %eax\n   mov %eax, %r8d\n   shl $32, %r8\n   or %r8, %rax\n"
    "mov %rsi, %rcx\n   shr $1, %rcx\n   rep stosq\n"
    "test $1, %sil\n   jz 1f\n   mov %eax, (%rdi)\n   1:\n"
#elif ARM64
    "b fill_u32_scalar_floor\n"
#else
    "j fill_u32_scalar_floor\n"
#endif
    ASM_RET
    ASM_END(fill_u32_bulk_floor)
);

fn fill_u32_scalar_floor(address_any destination, positive count,
                         unsigned int value);
fn fill_u32_bulk_floor(address_any destination, positive count,
                       unsigned int value);

typedef fn (*fill_call)(address_any, positive, unsigned int);
static fill_call volatile calls[] = {
    memory_fill_u32, fill_u32_scalar_floor, fill_u32_bulk_floor,
};
#else
#define FILL_NAME "memory_fill_u64_aligned"
#define FILL_SUBJECT memory_fill_u64_aligned
#define FILL_PATTERN 0x13579bdf2468ace0ull
#define FILL_JUNK 0xa5a5a5a5a5a5a5a5ull
#define FILL_SIZES                                                            \
        1, 2, 3, 4, 8, 16, 32, 48, 64, 80, 88, 96, 100, 104, 108, 112, 120,   \
        128, 160, 192, 256, 512, 1024, 4096

typedef positive fill_word;
static fill_word block[MAXIMUM + 2] __attribute__((aligned(64)));

__asm__(
    ".text\n"
    ASM_FUNC(fill_u64_scalar_floor)
#if X64
    "1:  cmp $4, %rsi\n   jb 2f\n"
    "mov %rdx, (%rdi)\n   mov %rdx, 8(%rdi)\n"
    "mov %rdx, 16(%rdi)\n   mov %rdx, 24(%rdi)\n"
    "add $32, %rdi\n   sub $4, %rsi\n   jmp 1b\n"
    "2:  test $2, %sil\n   jz 3f\n   mov %rdx, (%rdi)\n"
    "mov %rdx, 8(%rdi)\n   add $16, %rdi\n"
    "3:  test $1, %sil\n   jz 4f\n   mov %rdx, (%rdi)\n   4:\n"
#elif ARM64
    "1:  cmp x1, #4\n   b.lo 2f\n"
    "stp x2, x2, [x0], #16\n   stp x2, x2, [x0], #16\n"
    "sub x1, x1, #4\n   b 1b\n"
    "2:  tbz x1, #1, 3f\n   stp x2, x2, [x0], #16\n"
    "3:  tbz x1, #0, 4f\n   str x2, [x0]\n   4:\n"
#else
    "li t0, 4\n"
    "1:  blt a1, t0, 2f\n   sd a2, 0(a0)\n   sd a2, 8(a0)\n"
    "sd a2, 16(a0)\n   sd a2, 24(a0)\n   addi a0, a0, 32\n"
    "addi a1, a1, -4\n   j 1b\n"
    "2:  andi t0, a1, 2\n   beqz t0, 3f\n"
    "sd a2, 0(a0)\n   sd a2, 8(a0)\n   addi a0, a0, 16\n"
    "3:  andi t0, a1, 1\n   beqz t0, 4f\n   sd a2, 0(a0)\n   4:\n"
#endif
    ASM_RET
    ASM_END(fill_u64_scalar_floor)
    ASM_FUNC(fill_u64_bulk_floor)
#if X64
    "mov %rdx, %rax\n   mov %rsi, %rcx\n   rep stosq\n"
#elif ARM64
    "b fill_u64_scalar_floor\n"
#else
    "j fill_u64_scalar_floor\n"
#endif
    ASM_RET
    ASM_END(fill_u64_bulk_floor)
);

fn fill_u64_scalar_floor(address_any, positive, positive);
fn fill_u64_bulk_floor(address_any, positive, positive);

typedef fn (*fill_call)(address_any, positive, positive);
static fill_call volatile calls[] = {
    memory_fill_u64_aligned, fill_u64_scalar_floor, fill_u64_bulk_floor,
};
#endif

static p8 correctness(void)
{
        for (positive offset = 0; offset < 2; offset++)
                for (positive count = 0; count <= MAXIMUM; count++)
                {
                        memory_fill(block, 0xa5, sizeof(block));
                        FILL_SUBJECT(block + offset, count, FILL_PATTERN);

                        for (positive i = 0; i < MAXIMUM + 2; i++)
                        {
                                fill_word expected =
                                    i >= offset && i < offset + count
                                        ? FILL_PATTERN
                                        : FILL_JUNK;

                                if (block[i] != expected)
                                        return false;
                        }
                }

        return true;
}

static p64 run(unsigned int which, positive count, positive rounds,
               positive offset)
{
        p64 started = get_cpu_time();

        while (rounds--)
                calls[which](block + offset, count, FILL_PATTERN);

        return get_cpu_time() - started;
}

static fn row(positive count, positive offset)
{
        positive scalar[TRIES], bulk[TRIES];
        positive rounds = TARGET_BYTES / max(count * sizeof(*block), 1ul);

        if (rounds < 32)
                rounds = 32;

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 ours, floor;

                BENCH_ALTERNATE(trial, ours, run(0, count, rounds, offset),
                                floor, run(1, count, rounds, offset));

                scalar[trial] = (positive)(ours * 10000 / max(floor, 1ull));
                BENCH_ALTERNATE(trial, ours, run(0, count, rounds, offset),
                                floor, run(2, count, rounds, offset));
                bulk[trial] = (positive)(ours * 10000 / max(floor, 1ull));
        }

        order(scalar, TRIES);
        order(bulk, TRIES);
        string_format(log,
                      "  %p words +%p  current/scalar %p.%p%%  current/bulk %p.%p%%\n",
                      count, offset, scalar[TRIES / 2] / 100,
                      scalar[TRIES / 2] % 100, bulk[TRIES / 2] / 100,
                      bulk[TRIES / 2] % 100);
}

b32 main(void)
{
        static const positive sizes[] = {FILL_SIZES};

        if (!correctness())
        {
                string_format(log, FILL_NAME " correctness failed\n");
                log_flush();
                return 1;
        }

        string_format(log, FILL_NAME ", paired median of %p\n",
                      (positive)TRIES);

        for (positive i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
        {
                row(sizes[i], 0);
                row(sizes[i], 1);
        }

        log_flush();
        return 0;
}
#endif /* BENCH_fill_u32 || BENCH_fill_u64 */

#ifdef BENCH_paths
/* Former path C bodies against the shared three-architecture ASM paths. */
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 5
#define PATH_CAPACITY 4096

enum { BENCH_JOIN, BENCH_TAIL, BENCH_HEAD };

static p8 output[PATH_CAPACITY];
static p8 long_directory[2048];
static p8 long_name[1024];
static p8 long_path[3072];
static volatile positive sink;

NOT_INLINED positive former_path_join(p8 address_to into, positive limit,
                                      string_address directory,
                                      string_address name)
{
        positive length = string_length_max(directory, limit - 1);

        memory_copy_apart(into, directory, length);

        if (length > 0 && into[length - 1] != '/' && length + 1 < limit)
                into[length++] = '/';

        positive tail = string_length_max(name, limit - 1 - length);

        return (positive)(memory_copy_apart_end(into + length, name, tail) - into);
}

NOT_INLINED positive former_path_tail(p8 address_to into, string_address path)
{
        positive length = string_length(path);

        if (!length)
        {
                into[0] = end;
                return 0;
        }

        while (length > 1 && path[length - 1] == '/')
                length--;

        if (length == 1 && path[0] == '/')
        {
                into[0] = '/';
                into[1] = end;
                return 1;
        }

        positive start = length;

        while (start > 0 && path[start - 1] != '/')
                start--;

        positive found = length - start;

        if (found > PATH_CAPACITY - 1)
                found = PATH_CAPACITY - 1;

        memory_copy_apart_end(into, path + start, found);
        return found;
}

NOT_INLINED positive former_path_head(p8 address_to into, string_address path)
{
        positive length = string_length(path);

        while (length > 1 && path[length - 1] == '/')
                length--;

        positive cut = length;

        while (cut > 0 && path[cut - 1] != '/')
                cut--;

        if (!cut)
        {
                into[0] = '.';
                into[1] = end;
                return 1;
        }

        while (cut > 1 && path[cut - 1] == '/')
                cut--;

        memory_copy_apart_end(into, path, cut);
        return cut;
}

static fn paths_make_long()
{
        for (positive i = 0; i < sizeof(long_directory) - 1; i++)
                long_directory[i] = i && !(i % 31) ? '/' : (p8)('a' + i % 23);
        long_directory[sizeof(long_directory) - 1] = end;

        for (positive i = 0; i < sizeof(long_name) - 1; i++)
                long_name[i] = i && !(i % 47) ? '/' : (p8)('A' + i % 19);
        long_name[sizeof(long_name) - 1] = end;

        positive at = 0;
        for (positive i = 0; i < sizeof(long_directory) - 1; i++)
                long_path[at++] = long_directory[i];
        long_path[at++] = '/';
        for (positive i = 0; i < sizeof(long_name) - 1; i++)
                long_path[at++] = long_name[i];
        long_path[at] = end;
}

BENCH_ONCE(path_run, (p8 assembly, positive operation,
                    string_address directory, string_address name,
                    string_address path, positive rounds), rounds, i,
        {
                positive length;

                if (operation == BENCH_JOIN)
                        length = assembly
                                     ? path_join(output, PATH_CAPACITY,
                                                 directory, name)
                                     : former_path_join(output, PATH_CAPACITY,
                                                        directory, name);
                else if (operation == BENCH_TAIL)
                        length = assembly
                                     ? path_tail_copy(output, PATH_CAPACITY, path)
                                     : former_path_tail(output, path);
                else
                        length = assembly
                                     ? path_head_copy(output, PATH_CAPACITY, path)
                                     : former_path_head(output, path);

                sink += length + output[0] + output[length ? length - 1 : 0];
        })

/* path_join's generic against its zero-to-fourteen-byte size class, the same join at a short length. */
BENCH_ONCE(path_class_run, (p8 classed, string_address directory, string_address name,
                            positive rounds), rounds, i,
        {
                positive length = classed
                                      ? path_join_0_14(output, PATH_CAPACITY, directory, name)
                                      : path_join(output, PATH_CAPACITY, directory, name);

                sink += length + output[0] + output[length ? length - 1 : 0];
        })

static fn path_class_row(string_address label, string_address directory,
                         string_address name, positive rounds)
{
        p64 generic = positive_max;
        p64 classed = positive_max;

        BENCH_BEST_PAIRED(generic, path_class_run(false, directory, name, rounds),
                          classed, path_class_run(true, directory, name, rounds));

        string_format(log, "  %s: generic %p  class %p  class/generic %p%%\n",
                      label, (positive)generic, (positive)classed,
                      (positive)(classed * 100 / (generic ? generic : 1)));
}

static fn path_row(string_address label, positive operation,
                   string_address directory, string_address name,
                   string_address path, positive rounds)
{
        p64 former = positive_max;
        p64 assembly = positive_max;

        BENCH_BEST_PAIRED(former, path_run(false, operation, directory, name, path, rounds),
                          assembly, path_run(true, operation, directory, name, path, rounds));

        string_format(log, "  %s: former-C %p  assembly %p  asm/C %p%%\n",
                      label, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

b32 main()
{
        paths_make_long();
        moonwater_cpu_detect();

        string_format(log, "paths, best of %p\n", (positive)TRIES);

        path_row("join short", BENCH_JOIN, "usr", "bin", "usr/bin", 1u << 18);
        path_row("join nested", BENCH_JOIN, "/usr/local/share", "moonwater/bin",
                 "/usr/local/share/moonwater/bin", 1u << 16);
        path_row("join long", BENCH_JOIN, long_directory, long_name, long_path,
                 1u << 13);

        path_row("tail short", BENCH_TAIL, "usr", "bin", "usr/bin", 1u << 18);
        path_row("tail nested", BENCH_TAIL, "/usr/local/share", "moonwater/bin",
                 "/usr/local/share/moonwater/bin///", 1u << 16);
        path_row("tail long", BENCH_TAIL, long_directory, long_name, long_path,
                 1u << 13);

        path_row("head short", BENCH_HEAD, "usr", "bin", "usr/bin", 1u << 18);
        path_row("head nested", BENCH_HEAD, "/usr/local/share", "moonwater/bin",
                 "/usr/local/share/moonwater/bin///", 1u << 16);
        path_row("head long", BENCH_HEAD, long_directory, long_name, long_path,
                 1u << 13);

        path_class_row("join class, 1 byte", "", "A", 1u << 18);
        path_class_row("join class, 7 bytes", "usr", "bin", 1u << 18);
        path_class_row("join class, 14 bytes", "usr/lib", "xbins0", 1u << 18);

        log_flush();
        return 0;
}
#endif /* BENCH_paths */

#ifdef BENCH_reverse
/* memory_reverse and the rev fold against the literal C shapes they replace. */
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 7
#define MAXIMUM ((1u << 20) + 128)
#define OUT_MAX (1u << 16)
#define TARGET_BYTES (1u << 22)

static p8 block[MAXIMUM];
static p8 output[OUT_MAX];
static positive output_used;
static volatile positive sink;

NOT_INLINED address_any former_memory_reverse(address_any memory, positive size)
{
        p8 address_to left = memory;
        p8 address_to right = left + size;

        while (left < right)
        {
                p8 value;

                right--;
                if (left >= right)
                        break;

                value = address_to left;
                address_to left++ = address_to right;
                address_to right = value;
        }

        return memory;
}

/*
        These are text_put_character and the bounded half of text_put, with a
        flush represented by a volatile observation instead of a system call.
        The former rev body really did call the first once per byte.  The fold
        reverses the line in place and calls the second once, so this measures
        the actual old/new memory work without charging either side for I/O.
*/
static inline fn former_flush()
{
        if (output_used)
                sink += output[0] + output[output_used - 1] + output_used;
        output_used = 0;
}

static inline fn former_put_character(p8 value)
{
        if (output_used == OUT_MAX)
                former_flush();

        output[output_used++] = value;
}

NOT_INLINED fn former_rev_line(p8 address_to line, positive size)
{
        for (positive c = size; c > 0; c--)
                former_put_character(line[c - 1]);
}

NOT_INLINED fn folded_rev_line(p8 address_to line, positive size)
{
        memory_reverse(line, size);

        if (output_used + size > OUT_MAX)
                former_flush();

        memory_copy(output + output_used, line, size);
        output_used += size;
}

static fn prepare(positive size)
{
        // A palindrome makes every repeated folded call emit the same bytes
        // as the former body without adding a restore copy to either timing.
        for (positive i = 0; i < (size + 1) / 2; i++)
        {
                p8 value = (p8)(i * 37 + size * 11);

                block[i] = value;
                block[size - 1 - i] = value;
        }
}

static positive rounds_for(positive size)
{
        positive rounds = TARGET_BYTES / (size ? size : 1);

        if (rounds > (1u << 18))
                rounds = 1u << 18;
        if (rounds < 8)
                rounds = 8;
        return rounds;
}

BENCH_ONCE(primitive_run, (p8 assembly, positive size, positive rounds), rounds, i,
        {
                address_any answer = assembly ? memory_reverse(block, size)
                                              : former_memory_reverse(block, size);

                sink += (positive)answer + block[0] + block[size - 1];
        })

static p64 rev_run(p8 folded, positive size, positive rounds)
{
        output_used = 0;
        p64 start = get_cpu_time();

        for (positive i = 0; i < rounds; i++)
                if (folded)
                        folded_rev_line(block, size);
                else
                        former_rev_line(block, size);

        p64 finish = get_cpu_time();
        former_flush();
        return finish - start;
}

static fn primitive_row(positive size)
{
        positive rounds = rounds_for(size);
        p64 former = positive_max;
        p64 assembly = positive_max;

        prepare(size);

        BENCH_BEST_PAIRED(former, primitive_run(false, size, rounds),
                          assembly, primitive_run(true, size, rounds));

        string_format(log,
                      "  primitive %p bytes: former-C %p  assembly %p  asm/C %p%%\n",
                      size, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

static fn rev_row(positive size)
{
        positive rounds = rounds_for(size);
        p64 former = positive_max;
        p64 folded = positive_max;

        prepare(size);

        BENCH_BEST_PAIRED(former, rev_run(false, size, rounds),
                          folded, rev_run(true, size, rounds));

        string_format(log,
                      "  rev fold  %p bytes: former-loop %p  folded %p  new/old %p%%\n",
                      size, (positive)former, (positive)folded,
                      (positive)(folded * 100 / (former ? former : 1)));
}

b32 main()
{
        static const positive primitive_sizes[] = {
            8, 32, 64, 256, 4096, 65536, 1u << 20,
        };
        static const positive rev_sizes[] = {8, 32, 64, 256, 4096, 32768};

        moonwater_cpu_detect();
        string_format(log, "memory_reverse, best of %p\n", (positive)TRIES);

        for (positive i = 0;
             i < sizeof(primitive_sizes) / sizeof(primitive_sizes[0]); i++)
                primitive_row(primitive_sizes[i]);

        for (positive i = 0; i < sizeof(rev_sizes) / sizeof(rev_sizes[0]); i++)
                rev_row(rev_sizes[i]);

        log_flush();
        return 0;
}
#endif /* BENCH_reverse */

#ifdef BENCH_writer_field
/*
        The counted and string field primitives against the literal C helper
        shape they replace: compute minimum-width padding, preserve one writer
        call per pad byte, and make one counted body call only when nonempty.

        The string rows include exact/narrow fields of length 0, 1, 8 and 64
        and their width-64 padded forms. That makes the fixed wrapper/frame
        cost visible instead of letting a long padding loop hide it. Native
        runs are hardware ticks; qemu ratios are emulator work only.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 7
#define TARGET_CALLBACKS (1u << 20)
#define MAX_ROUNDS (1u << 18)
#define MIN_ROUNDS (1u << 12)

static p8 texts[4][80];
static const positive lengths[4] = {0, 1, 8, 64};
static volatile positive sink;

NOT_INLINED fn discard_writer(address_any data, positive length)
{
        volatile p8 address_to bytes = (volatile p8 address_to)data;

        sink += length + bytes[0] + bytes[length - 1];
}

NOT_INLINED fn former_writer_field(writer write, address_any data,
                                   positive length, positive width,
                                   b8 pad, p8 left)
{
        positive padding = width > length ? width - length : 0;

        if (!left)
                writer_fill(write, padding, (p8)pad);

        if (length)
                write(data, length);

        if (left)
                writer_fill(write, padding, (p8)pad);
}

NOT_INLINED fn former_string_to_field(writer write, string_address text,
                                      positive width, b8 pad, p8 left)
{
        positive length = string_length(text);
        positive padding = width > length ? width - length : 0;

        if (!left)
                writer_fill(write, padding, (p8)pad);

        if (length)
                write(text, length);

        if (left)
                writer_fill(write, padding, (p8)pad);
}

static positive rounds_for(positive length, positive width)
{
        positive callbacks = (width > length ? width - length : 0) +
                             (length != 0);
        positive rounds = TARGET_CALLBACKS / (callbacks ? callbacks : 1);

        if (rounds > MAX_ROUNDS)
                rounds = MAX_ROUNDS;
        if (rounds < MIN_ROUNDS)
                rounds = MIN_ROUNDS;
        return rounds;
}

static p64 raw_once(p8 assembly, positive shape, positive width,
                    p8 left, positive rounds)
{
        positive length = lengths[shape];
        p64 start = get_cpu_time();

        for (positive i = 0; i < rounds; i++)
                if (assembly)
                        writer_field(discard_writer, texts[shape], length,
                                     width, (b8)0xa5, left);
                else
                        former_writer_field(discard_writer, texts[shape], length,
                                            width, (b8)0xa5, left);

        return get_cpu_time() - start;
}

BENCH_ONCE(string_once, (p8 assembly, positive shape, positive width,
                       p8 left, positive rounds), rounds, i,
           if (assembly)
                   string_to_field(discard_writer, texts[shape], width,
                                   (b8)0xa5, left);
           else
                   former_string_to_field(discard_writer, texts[shape], width,
                                          (b8)0xa5, left))

static fn row(string_address name, p8 string_form, positive shape,
              positive width, p8 left)
{
        positive rounds = rounds_for(lengths[shape], width);
        p64 former = positive_max;
        p64 assembly = positive_max;

        BENCH_BEST_PAIRED(former,
                          string_form
                              ? string_once(false, shape, width, left, rounds)
                              : raw_once(false, shape, width, left, rounds),
                          assembly,
                          string_form
                              ? string_once(true, shape, width, left, rounds)
                              : raw_once(true, shape, width, left, rounds));

        string_format(log,
                      "  %s (%p calls): former-C %p  assembly %p  asm/C %p%%\n",
                      name, rounds, (positive)former, (positive)assembly,
                      (positive)(assembly * 100 / (former ? former : 1)));
}

b32 main()
{
        for (positive shape = 0; shape < 4; shape++)
        {
                positive length = lengths[shape];

                for (positive i = 0; i < length; i++)
                        texts[shape][i] = (p8)(1 + (i * 47 + length) % 255);
                texts[shape][length] = end;
        }

        moonwater_cpu_detect();
        string_format(log, "writer fields, best of %p\n", (positive)TRIES);

        row((string_address)"raw empty width 0", false, 0, 0, false);
        row((string_address)"raw 1 width 1", false, 1, 1, false);
        row((string_address)"raw 8 width 8", false, 2, 8, false);
        row((string_address)"raw 1 width 8 right", false, 1, 8, false);
        row((string_address)"raw 1 width 8 left", false, 1, 8, true);

        row((string_address)"string 0 width 0", true, 0, 0, false);
        row((string_address)"string 1 width 1", true, 1, 1, false);
        row((string_address)"string 8 width 8", true, 2, 8, false);
        row((string_address)"string 64 width 64", true, 3, 64, false);
        row((string_address)"string 0 width 64 right", true, 0, 64, false);
        row((string_address)"string 1 width 64 right", true, 1, 64, false);
        row((string_address)"string 8 width 64 right", true, 2, 64, false);
        row((string_address)"string 8 width 64 left", true, 2, 64, true);

        log_flush();
        return 0;
}
#endif /* BENCH_writer_field */

#ifdef BENCH_writer_text
/* Buffered output policy: former C against the shared assembly core. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

// Keep the former general-purpose body general. GCC otherwise clones a copy
// for the constant capacity in each row and times that specialised copy
// against the public assembly ABI, which is not the implementation replaced.
#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define BUFFER_ROUNDS (1u << 18)
#define BYTE_ROUNDS (1u << 20)
#define SYSCALL_ROUNDS 2048
#define CAPACITY 4096

static p8 output[CAPACITY];
static p8 payload[CAPACITY * 2];
static positive used;
static positive null_handle;
static volatile positive sink;
static volatile positive benchmark_capacity = CAPACITY;
static volatile positive benchmark_reserve_length = 16;

NOT_INLINED static b32 former_flush(positive handle, p8 address_to buffer,
                                    positive address_to count)
{
        if (!address_to count)
                return true;

        positive wanted = address_to count;
        positive written = system_write_all(handle, buffer, wanted);
        address_to count = 0;
        return written == wanted;
}

NOT_INLINED static b32 former_put(positive handle, p8 address_to buffer,
                                  positive capacity, positive address_to count,
                                  address_any data, positive length, p8 hold_equal)
{
        if (!length)
                return true;

        if (length > capacity || (!hold_equal && length == capacity))
        {
                if (!former_flush(handle, buffer, count))
                        return false;

                return system_write_all(handle, data, length) == length;
        }

        if (length > capacity - address_to count &&
            !former_flush(handle, buffer, count))
                return false;

        memory_copy(buffer + address_to count, data, length);
        address_to count += length;
        return true;
}

NOT_INLINED static p8 address_to former_reserve(
    positive handle, p8 address_to buffer, positive capacity,
    positive address_to count, positive length)
{
        if (length > capacity)
                return null;

        if (length > capacity - address_to count &&
            !former_flush(handle, buffer, count))
                return null;

        p8 address_to answer = buffer + address_to count;
        address_to count += length;
        return answer;
}

// Hot no-flush traffic floor: the caller-proven span needs one old-count load,
// one new-count store and one returned address. It deliberately omits the
// capacity and flush semantics the public routine must retain.
NOT_INLINED static p8 address_to reserve_floor(p8 address_to buffer,
                                               positive address_to count,
                                               positive length)
{
        p8 address_to answer = buffer + address_to count;
        address_to count += length;
        return answer;
}

NOT_INLINED static b32 former_byte(positive handle, p8 address_to buffer,
                                   positive capacity, positive address_to count,
                                   p8 byte)
{
        if (!capacity)
                return system_write_all(handle, address_of byte, 1) == 1;

        if (address_to count >= capacity &&
            !former_flush(handle, buffer, count))
                return false;

        buffer[address_to count] = byte;
        address_to count += 1;
        return true;
}

NOT_INLINED static fn former_log(address_any data, positive length)
{
        if (!length)
                length = string_length(data);

        /* The replaced logger was exactly this adapter with a tail call into
           the already-assembly buffered core.  Keep that core on both sides
           so this row isolates sticky-status overhead rather than comparing
           two different buffered writers. */
        (fn)buffered_write(1, log_writer_buffer, CAPACITY,
                          address_of log_writer_buffer_length, data, length);
}

static p64 put_once(positive length, p8 hold_equal, p8 assembly,
                    positive rounds, positive pending)
{
        positive capacity = benchmark_capacity;
        p64 start = get_cpu_time();

        for (positive i = 0; i < rounds; i++)
        {
                b32 success;
                used = pending;

                if (assembly)
                {
                        if (hold_equal)
                                success = buffered_write_deferred_equal(
                                    null_handle, output, capacity, address_of used,
                                    payload, length);
                        else
                                success = buffered_write(null_handle, output, capacity,
                                                         address_of used, payload, length);
                }
                else
                        success = former_put(null_handle, output, capacity,
                                             address_of used, payload, length,
                                             hold_equal);

                sink += used + output[0] + (positive)success;
        }

        return get_cpu_time() - start;
}

static p64 byte_once(p8 assembly)
{
        positive capacity = benchmark_capacity;
        p64 start = get_cpu_time();

        for (positive i = 0; i < BYTE_ROUNDS; i++)
        {
                b32 success;
                used = i & (capacity - 1);

                if (assembly)
                        success = buffered_write_byte(null_handle, output, capacity,
                                                      address_of used, (p8)i);
                else
                        success = former_byte(null_handle, output, capacity,
                                              address_of used, (p8)i);

                sink += used + output[i & (capacity - 1)] + (positive)success;
        }

        return get_cpu_time() - start;
}

static p64 reserve_once(b32 which)
{
        positive capacity = benchmark_capacity;
        positive length = benchmark_reserve_length;
        p64 start = get_cpu_time();

        for (positive i = 0; i < BUFFER_ROUNDS; i++)
        {
                // Keep this row at the no-syscall semantic floor. Flush and
                // failure paths have their own rows and focused tests.
                used = i & 2047;
                p8 address_to at;

                if (which == 1)
                        at = buffered_reserve(null_handle, output, capacity,
                                              address_of used, length);
                else if (which == 2)
                        at = reserve_floor(output, address_of used, length);
                else
                        at = former_reserve(null_handle, output, capacity,
                                            address_of used, length);

                at[0] = (p8)i;
                at[length - 1] = (p8)(i >> 8);
                sink += used + at[0] + at[length - 1];
        }

        return get_cpu_time() - start;
}

BENCH_ONCE(flush_once, (p8 assembly), SYSCALL_ROUNDS, i,
        {
                b32 success;
                used = CAPACITY;

                if (assembly)
                        success = buffered_flush(null_handle, output, address_of used);
                else
                        success = former_flush(null_handle, output, address_of used);

                sink += used + (positive)success;
        })

BENCH_ONCE(log_once, (positive length, p8 assembly), BUFFER_ROUNDS, i,
        {
                log_writer_buffer_length = 0;

                if (assembly)
                        log(payload, length);
                else
                        former_log(payload, length);

                sink += log_writer_buffer_length + log_writer_buffer[0];
        })

static fn row(string_address name, positive length, p8 hold_equal,
              positive rounds, positive pending, p8 byte, p8 flush)
{
        positive ratios[TRIES];

        BENCH_PAIRED(ratios, former,
                     byte ? byte_once(false)
                          : (flush ? flush_once(false)
                                   : put_once(length, hold_equal, false, rounds,
                                              pending)),
                     assembly,
                     byte ? byte_once(true)
                          : (flush ? flush_once(true)
                                   : put_once(length, hold_equal, true, rounds,
                                              pending)));
        string_format(log, "  %s  median asm/C %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

static fn log_row(string_address name, positive length)
{
        positive ratios[TRIES];

        /* Preserve the report accumulated by the non-logger rows before the
           benchmark deliberately reuses the process log buffer as scratch. */
        log_flush();

        BENCH_PAIRED(ratios, former, log_once(length, false),
                     assembly, log_once(length, true));
        log_writer_buffer_length = 0;
        string_format(log, "  %s  median current/former %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

static fn reserve_row()
{
        positive ratios[TRIES];
        positive assembly_each[TRIES];
        positive floor_each[TRIES];

        for (positive t = 0; t < TRIES; t++)
        {
                p64 former;
                p64 assembly;
                p64 floor;

                if (t & 1)
                {
                        assembly = reserve_once(1);
                        floor = reserve_once(2);
                        former = reserve_once(0);
                }
                else
                {
                        former = reserve_once(0);
                        floor = reserve_once(2);
                        assembly = reserve_once(1);
                }

                ratios[t] = (positive)(assembly * 10000 / (former ? former : 1));
                assembly_each[t] = (positive)(assembly * 100 / BUFFER_ROUNDS);
                floor_each[t] = (positive)(floor * 100 / BUFFER_ROUNDS);
        }

        order(ratios, TRIES);
        order(assembly_each, TRIES);
        order(floor_each, TRIES);
        string_format(log, "  reserve 16  median asm/C %p.%p%%\n",
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        positive assembly = assembly_each[TRIES / 2];
        positive floor = floor_each[TRIES / 2];
        if (assembly >= floor)
                string_format(log,
                              "              asm %p.%p cycles/op, no-flush floor %p.%p, gap %p.%p\n",
                              assembly / 100, assembly % 100,
                              floor / 100, floor % 100,
                              (assembly - floor) / 100,
                              (assembly - floor) % 100);
        else
                string_format(log,
                              "              asm %p.%p cycles/op beat the %p.%p floor proxy; unresolved\n",
                              assembly / 100, assembly % 100,
                              floor / 100, floor % 100);
}

b32 main(void)
{
        for (positive i = 0; i < sizeof(payload); i++)
                payload[i] = (p8)(i * 37 + 11);

        null_handle = (positive)system_call_4(syscall(openat),
                                               (positive)(bipolar)AT_FDCWD,
                                               (positive)"/dev/null", 1, 0);

        string_format(log, "buffered writer, paired median of %p\n", (positive)TRIES);
        row((string_address)"buffer 8", 8, false, BUFFER_ROUNDS, 0, false, false);
        row((string_address)"buffer 64", 64, false, BUFFER_ROUNDS, 0, false, false);
        reserve_row();
        row((string_address)"byte", 0, false, 0, 0, true, false);
        row((string_address)"combined overflow", 8, false, SYSCALL_ROUNDS,
            CAPACITY - 4, false, false);
        row((string_address)"direct exact", CAPACITY, false, SYSCALL_ROUNDS,
            0, false, false);
        row((string_address)"hold exact", CAPACITY, true, SYSCALL_ROUNDS,
            0, false, false);
        row((string_address)"flush", 0, false, 0, 0, false, true);
        log_row((string_address)"log buffer 8", 8);
        log_row((string_address)"log buffer 64", 64);

        system_call_1(syscall(close), null_handle);
        log_flush();
        return 0;
}
#endif /* BENCH_writer_text */

#ifdef BENCH_text_hot
/* Hot text-loop folds against the literal scalar loops they replace. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define BLOCK 4096
#define ROUNDS (1u << 15)

static p8 one[BLOCK];
static p8 two[BLOCK];
static volatile positive sink;

static p8 prefix_boundaries(void)
{
        p8 left[272];
        p8 right[272];

        for (positive left_offset = 0; left_offset < 8; left_offset++)
                for (positive right_offset = 0; right_offset < 8; right_offset++)
                        for (positive length = 0; length <= 257; length++)
                        {
                                for (positive at = 0; at < length; at++)
                                        left[left_offset + at] =
                                            right[right_offset + at] = (p8)(at * 37 + length);

                                if (memory_common_prefix(left + left_offset,
                                                         right + right_offset,
                                                         length) != length)
                                        return false;

                                if (!length)
                                        continue;

                                positive positions[] = {
                                    0, length / 2, length - 1, 7, 8, 31, 32,
                                };

                                for (positive which = 0;
                                     which < sizeof(positions) / sizeof(positions[0]);
                                     which++)
                                {
                                        positive position = positions[which];

                                        if (position >= length)
                                                continue;

                                        right[right_offset + position] ^= 1;

                                        if (memory_common_prefix(left + left_offset,
                                                                 right + right_offset,
                                                                 length) != position)
                                                return false;

                                        right[right_offset + position] ^= 1;
                                }
                        }

        return true;
}

NOT_INLINED static bipolar former_compare(p8 address_to a, positive la,
                                          p8 address_to b, positive lb)
{
        positive at = 0;
        positive length = la < lb ? la : lb;

        while (at < length)
        {
                if (a[at] != b[at])
                        return a[at] < b[at] ? -1 : 1;
                at++;
        }

        return la == lb ? 0 : la < lb ? -1 : 1;
}

NOT_INLINED static bipolar folded_compare(p8 address_to a, positive la,
                                          p8 address_to b, positive lb)
{
        positive length = la < lb ? la : lb;
        bipolar order = memory_compare(a, b, length);

        if (order)
                return order < 0 ? -1 : 1;

        return la == lb ? 0 : la < lb ? -1 : 1;
}

NOT_INLINED static positive former_equal_block(p8 address_to a,
                                               p8 address_to b,
                                               positive length)
{
        positive lines = 0;

        for (positive at = 0; at < length; at++)
        {
                if (a[at] != b[at])
                        return lines + 1;
                if (a[at] == '\n')
                        lines++;
        }

        return lines;
}

NOT_INLINED static positive folded_equal_block(p8 address_to a,
                                               p8 address_to b,
                                               positive length)
{
        if (memory_compare(a, b, length))
                return 1;

        return memory_count(a, length, '\n');
}

NOT_INLINED static positive former_late_difference(p8 address_to a,
                                                   p8 address_to b,
                                                   positive length)
{
        positive lines = 0;
        positive at = 0;

        while (at < length && a[at] == b[at])
        {
                if (a[at] == '\n')
                        lines++;
                at++;
        }

        return at + lines;
}

NOT_INLINED static positive folded_late_difference(p8 address_to a,
                                                   p8 address_to b,
                                                   positive length)
{
        if (!memory_compare(a, b, length))
                return length + memory_count(a, length, '\n');

        positive prefix = memory_common_prefix(a, b, length);

        return prefix + memory_count(a, prefix, '\n');
}

NOT_INLINED static positive former_newline(p8 address_to data, positive length)
{
        positive at = 0;

        while (at < length && data[at] != '\n')
                at++;

        return at;
}

NOT_INLINED static positive folded_newline(p8 address_to data, positive length)
{
        p8 address_to found = memory_first_of(data, '\n', length);
        return found ? (positive)(found - data) : length;
}

BENCH_ONCE(compare_once, (p8 folded), ROUNDS, round,
           sink += (positive)(folded ? folded_compare(one, BLOCK, two, BLOCK)
                                     : former_compare(one, BLOCK, two, BLOCK)))

BENCH_ONCE(equal_once, (p8 folded), ROUNDS, round,
           sink += folded ? folded_equal_block(one, two, BLOCK)
                          : former_equal_block(one, two, BLOCK))

BENCH_ONCE(newline_once, (p8 folded), ROUNDS, round,
           sink += folded ? folded_newline(one, BLOCK)
                          : former_newline(one, BLOCK))

BENCH_ONCE(late_once, (p8 folded), ROUNDS, round,
           sink += folded ? folded_late_difference(one, two, BLOCK)
                          : former_late_difference(one, two, BLOCK))

BENCH_ONCE(prefix_once, (p8 folded), ROUNDS, round,
           if (folded)
                   sink += memory_common_prefix(one, two, BLOCK);
           else
           {
                   positive prefix = 0;

                   while (prefix < BLOCK && one[prefix] == two[prefix])
                           prefix++;

                   sink += prefix;
           })

NOT_INLINED static positive former_alpha(p8 address_to data, positive length)
{
        positive at = 0;

        while (at < length && byte_is_alpha(data[at]))
                at++;

        return at;
}

NOT_INLINED static positive former_digit(p8 address_to data, positive length)
{
        positive at = 0;

        while (at < length && (p8)(data[at] - '0') < 10)
                at++;

        return at;
}

NOT_INLINED static positive former_high(p8 address_to data, positive length)
{
        positive at = 0;

        while (at < length && (data[at] & 0x80))
                at++;

        return at;
}

NOT_INLINED static positive former_ascii(p8 address_to data, positive length)
{
        positive at = 0;

        while (at < length && data[at] < 0x80)
                at++;

        return at;
}

static p64 class_once(p8 folded, const b8 address_to set, positive length,
                      positive (*former)(p8 address_to, positive))
{
        p64 start = get_cpu_time();

        for (positive round = 0; round < ROUNDS; round++)
                sink += folded ? string_span_max(one, length, set)
                               : former(one, length);

        return get_cpu_time() - start;
}

static p64 alpha_short_once(p8 folded)
{
        return class_once(folded, string_set_alpha, 6, former_alpha);
}

static p64 digit_short_once(p8 folded)
{
        return class_once(folded, string_set_digits, 8, former_digit);
}

static p64 high_short_once(p8 folded)
{
        return class_once(folded, string_set_high, 5, former_high);
}

static p64 ascii_medium_once(p8 folded)
{
        return class_once(folded, string_set_ascii, 128, former_ascii);
}

static p8 class_boundaries(void)
{
        for (positive byte = 0; byte < 256; byte++)
                if (string_set_alpha[byte] != byte_is_alpha((p8)byte) ||
                    string_set_digits[byte] != ((p8)(byte - '0') < 10) ||
                    string_set_ascii[byte] != (byte < 0x80) ||
                    string_set_high[byte] != (byte >= 0x80) ||
                    string_set_printable[byte] !=
                        (byte >= ' ' && byte < 0x7f))
                        return false;

        return true;
}

static fn row(string_address name, p64 (*run)(p8))
{
        positive ratios[TRIES];

        BENCH_PAIRED(ratios, former, run(false),
                     folded, run(true));
        string_format(log, "  %s  median folded/scalar %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        if (!prefix_boundaries() || !class_boundaries())
        {
                string_format(log, "text-fold boundary check failed\n");
                log_flush();
                return 1;
        }

        for (positive at = 0; at < BLOCK; at++)
        {
                one[at] = (p8)(at * 37 + 11);
                if ((at & 63) == 63)
                        one[at] = '\n';
                two[at] = one[at];
        }

        moonwater_cpu_detect();
        string_format(log, "hot text folds, paired median of %p\n",
                      (positive)TRIES);
        row((string_address)"sort equal 4K", compare_once);
        row((string_address)"cmp equal+line count 4K", equal_once);

        two[BLOCK - 1] ^= 1;
        row((string_address)"common prefix late 4K", prefix_once);
        row((string_address)"cmp late difference 4K", late_once);
        two[BLOCK - 1] ^= 1;

        /* sed P/D's first newline is deliberately late for the scan row. */
        for (positive at = 0; at < BLOCK; at++)
                one[at] = 'x';
        one[BLOCK - 1] = '\n';
        row((string_address)"sed newline late 4K", newline_once);

        for (positive at = 0; at < BLOCK; at++)
                one[at] = 'a';
        row((string_address)"alpha class 6", alpha_short_once);
        for (positive at = 0; at < BLOCK; at++)
                one[at] = '7';
        row((string_address)"digit class 8", digit_short_once);
        one[0] = 'x';
        row((string_address)"digit class 8, first outside", digit_short_once);
        for (positive at = 0; at < BLOCK; at++)
                one[at] = 0x80;
        row((string_address)"high-bit class 5", high_short_once);
        one[0] = 'x';
        row((string_address)"high-bit class 5, first outside", high_short_once);
        for (positive at = 0; at < BLOCK; at++)
                one[at] = 'x';
        row((string_address)"ASCII class 128", ascii_medium_once);
        one[0] = 0x80;
        row((string_address)"ASCII class 128, first outside", ascii_medium_once);

        log_flush();
        return 0;
}
#endif /* BENCH_text_hot */

#ifdef BENCH_prefix_known
/* Literal-size equal-span walk against the out-of-line hardware routine.
   Mismatch positions are exhaustively guarded by the exact_prefix section of test/checks.c;
   this harness intentionally measures the maximum-traffic shape. */
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define SUBJECTS 64
#define ROUNDS (1u << 19)
#define TRIES 9

static p8 left[SUBJECTS][144];
static p8 right[SUBJECTS][144];
static volatile positive sink;

#define BENCH_PAIRED_INDEXED
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

static fn prepare(void)
{
        for (positive which = 0; which < SUBJECTS; which++)
                for (positive at = 0; at < 144; at++)
                        left[which][at] = right[which][at] =
                            (p8)(at * 37 + which * 11);
}

#define DEFINE_SIZE(N)                                                        \
        NOT_INLINED static positive routine_##N(positive which)               \
        {                                                                     \
                return (memory_common_prefix)(left[which], right[which], (N));\
        }                                                                     \
        NOT_INLINED static positive folded_##N(positive which)                \
        {                                                                     \
                return common_prefix_known(left[which], right[which], (N));   \
        }

DEFINE_SIZE(1)
DEFINE_SIZE(2)
DEFINE_SIZE(3)
DEFINE_SIZE(4)
DEFINE_SIZE(5)
DEFINE_SIZE(6)
DEFINE_SIZE(7)
DEFINE_SIZE(8)
DEFINE_SIZE(9)
DEFINE_SIZE(10)
DEFINE_SIZE(11)
DEFINE_SIZE(12)
DEFINE_SIZE(13)
DEFINE_SIZE(14)
DEFINE_SIZE(15)
DEFINE_SIZE(16)
DEFINE_SIZE(17)
DEFINE_SIZE(18)
DEFINE_SIZE(19)
DEFINE_SIZE(20)
DEFINE_SIZE(21)
DEFINE_SIZE(22)
DEFINE_SIZE(23)
DEFINE_SIZE(24)
DEFINE_SIZE(31)
DEFINE_SIZE(32)
DEFINE_SIZE(40)
DEFINE_SIZE(64)
DEFINE_SIZE(80)
DEFINE_SIZE(96)
DEFINE_SIZE(127)
DEFINE_SIZE(128)

static fn row(positive size, bench_indexed_work routine,
              bench_indexed_work folded)
{
        positive got = bench_paired_median(routine, folded);

        string_format(log, "  %p bytes: folded/routine %p.%p%%\n", size,
                      got / 100, got % 100);
}

b32 main(void)
{
        prepare();
        moonwater_cpu_detect();
        string_format(log, "literal common prefix, paired median of %p\n",
                      (positive)TRIES);
#define ROW(N) row((N), routine_##N, folded_##N)
        ROW(1); ROW(2); ROW(3); ROW(4); ROW(5); ROW(6); ROW(7); ROW(8);
        ROW(9); ROW(10); ROW(11); ROW(12); ROW(13); ROW(14); ROW(15);
        ROW(16); ROW(17); ROW(18); ROW(19); ROW(20); ROW(21); ROW(22);
        ROW(23); ROW(24); ROW(31); ROW(32); ROW(40);
        ROW(64); ROW(80); ROW(96); ROW(127); ROW(128);
#undef ROW
        log_flush();
        return 0;
}
#endif /* BENCH_prefix_known */

#ifdef BENCH_translate
/* In-place byte-table translation: former scalar C against library assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 26)

static p8 former_block[MAXIMUM];
static p8 assembly_block[MAXIMUM];
static p8 translation[256];
static volatile positive sink;

NOT_INLINED static address_any former_translate(address_any block,
                                                positive length,
                                                address_any table_address)
{
        p8 address_to bytes = block;
        p8 address_to table = table_address;

        for (positive at = 0; at < length; at++)
                bytes[at] = table[bytes[at]];

        return block;
}

static fn prepare(positive length)
{
        for (positive at = 0; at < length; at++)
        {
                p8 value = (p8)(at * 37 + 11);
                former_block[at] = value;
                assembly_block[at] = value;
        }
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / length;

        if (rounds < 8)
                rounds = 8;
        if (rounds > (1u << 20))
                rounds = 1u << 20;
        return rounds;
}

static p64 run(p8 assembly, positive length, positive rounds)
{
        p8 address_to block = assembly ? assembly_block : former_block;
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
        {
                address_any answer = assembly
                                         ? memory_translate(block, length, translation)
                                         : former_translate(block, length, translation);
                sink += (positive)answer + block[0] + block[length - 1];
        }

        return get_cpu_time() - start;
}

static p8 row(positive length)
{
        positive rounds = rounds_for(length);
        positive ratios[TRIES];

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 former;
                p64 assembly;

                prepare(length);

                BENCH_BOTH_ORDERS(trial, assembly, former, length, rounds);

                if (memory_compare(former_block, assembly_block, length))
                        return false;

                ratios[trial] = (positive)(assembly * 10000 /
                                            (former ? former : 1));
        }

        order(ratios, TRIES);
        string_format(log, "  %p bytes  median asm/C %p.%p%%\n", length,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        return true;
}

static p8 boundaries(void)
{
        p8 guarded[264];
        p8 expected[264];

        for (positive length = 0; length <= 257; length++)
        {
                for (positive at = 0; at < sizeof(guarded); at++)
                        guarded[at] = expected[at] = (p8)(at * 19 + length);

                for (positive at = 0; at < length; at++)
                        expected[3 + at] = translation[expected[3 + at]];

                if (memory_translate(guarded + 3, length, translation) != guarded + 3 ||
                    memory_compare(guarded, expected, sizeof(guarded)))
                        return false;
        }

        return true;
}

b32 main(void)
{
        static const positive sizes[] = {8, 64, 4096, MAXIMUM};

        for (positive value = 0; value < 256; value++)
                translation[value] = (p8)(value * 197 + 101);

        if (!boundaries())
        {
                string_format(log, "memory_translate boundary check failed\n");
                log_flush();
                return 1;
        }

        string_format(log, "memory_translate, paired median of %p\n",
                      (positive)TRIES);

        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
                if (!row(sizes[at]))
                {
                        string_format(log, "memory_translate result mismatch\n");
                        log_flush();
                        return 1;
                }

        log_flush();
        return 0;
}
#endif /* BENCH_translate */

#if defined(BENCH_delete) || defined(BENCH_squeeze)
/*
        tr's in-place table compaction, its former byte loop against library
        assembly: deletion (BENCH_delete) and squeezing of repeats
        (BENCH_squeeze), over text-shaped bytes. Each round restores the block
        first, because both shrink it; that copy is timed on its own and taken
        out of both sides. Each section writes its former loop, its source
        text and its mark sets; the trials and the report are one.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 26)

static p8 source_block[MAXIMUM];
static p8 former_block[MAXIMUM];
static p8 assembly_block[MAXIMUM];
static p8 marks[256];
static positive former_kept;
static positive assembly_kept;
static volatile positive sink;

#ifdef BENCH_delete
#define SUBJECT "memory_delete_bytes"
#define SUBJECT_KEPT(block, length) memory_delete_bytes(block, length, marks)
#define FORMER_KEPT(block, length) former_delete(block, length, marks)

NOT_INLINED static positive former_delete(address_any block, positive length,
                                          address_any table_address)
{
        p8 address_to bytes = block;
        p8 address_to table = table_address;
        positive kept = 0;

        for (positive at = 0; at < length; at++)
        {
                p8 character = bytes[at];

                if (!table[character])
                        bytes[kept++] = character;
        }

        return kept;
}

// Text-shaped bytes: letters, spaces, commas and newlines.
static fn prepare_source(void)
{
        p64 seed = 0x9e3779b97f4a7c15ull;

        for (positive at = 0; at < MAXIMUM; at++)
        {
                XORSHIFT64(seed);

                positive pick = (positive)(seed % 40);

                source_block[at] = pick < 26 ? (p8)('a' + pick)
                                   : pick < 34 ? (p8)' '
                                   : pick < 38 ? (p8)','
                                               : (p8)'\n';
        }
}

// tr -d ',' (rare), tr -d ' ,' (common) and tr -cd 'a-z\n' (most).
static fn choose_marks(positive set)
{
        for (positive value = 0; value < 256; value++)
                marks[value] = set == 2 ? (p8)!((value >= 'a' && value <= 'z') ||
                                                value == '\n')
                                        : 0;

        if (set <= 1)
                marks[','] = 1;

        if (set == 1)
                marks[' '] = 1;
}
#else
#define SUBJECT "memory_squeeze_bytes"
#define SUBJECT_KEPT(block, length) memory_squeeze_bytes(block, length, marks, 256)
#define FORMER_KEPT(block, length) former_squeeze(block, length, marks)

// The loop tr -s ran before the library had this: a byte at a time, the run
// after a marked byte skipped with the hardware span.
NOT_INLINED static positive former_squeeze(p8 address_to at, positive left,
                                           const p8 address_to squeezed)
{
        positive kept = 0;
        b32 last_written = -1;

        for (positive c = 0; c < left;)
        {
                p8 character = at[c++];

                if (!squeezed[character] || (b32)character != last_written)
                        at[kept++] = character;

                last_written = character;

                if (squeezed[character] && c < left && at[c] == character)
                        c += memory_span_byte(at + c, character, left - c);
        }

        return kept;
}

// Words of letters with one to three spaces between them and a newline now
// and then.
static fn prepare_source(void)
{
        p64 seed = 0x9e3779b97f4a7c15ull;

        for (positive at = 0; at < MAXIMUM; at++)
        {
                XORSHIFT64(seed);

                positive pick = (positive)(seed % 40);

                source_block[at] = pick < 30 ? (p8)('a' + pick % 26)
                                   : pick < 38 ? (p8)' '
                                               : (p8)'\n';
        }
}

// tr -s ' ', tr -s 'a-z' and tr -s with every byte marked.
static fn choose_marks(positive set)
{
        for (positive value = 0; value < 256; value++)
                marks[value] = set == 2 || (set == 1 && value >= 'a' && value <= 'z');

        if (set == 0)
                marks[' '] = 1;
}
#endif

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / length;

        if (rounds < 8)
                rounds = 8;
        if (rounds > (1u << 20))
                rounds = 1u << 20;
        return rounds;
}

BENCH_ONCE(run_copy, (positive length, positive rounds), rounds, round,
        {
                memory_copy_apart(former_block, source_block, length);
                sink += former_block[0];
        })

static p64 run(p8 assembly, positive length, positive rounds)
{
        p8 address_to block = assembly ? assembly_block : former_block;
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
        {
                memory_copy_apart(block, source_block, length);

                positive kept = assembly ? SUBJECT_KEPT(block, length)
                                         : FORMER_KEPT(block, length);

                if (assembly)
                        assembly_kept = kept;
                else
                        former_kept = kept;

                sink += kept + block[0];
        }

        return get_cpu_time() - start;
}

static p8 row(positive length, positive set)
{
        positive rounds = rounds_for(length);
        positive ratios[TRIES];

        choose_marks(set);

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 copy = run_copy(length, rounds);
                p64 former;
                p64 assembly;

                BENCH_BOTH_ORDERS(trial, assembly, former, length, rounds);

                if (former_kept != assembly_kept ||
                    memory_compare(former_block, assembly_block, former_kept))
                        return false;

                p64 former_work = former > copy ? former - copy : 1;
                p64 assembly_work = assembly > copy ? assembly - copy : 0;

                ratios[trial] = (positive)(assembly_work * 10000 / former_work);
        }

        order(ratios, TRIES);
        string_format(log, "  %p bytes, set %p  median asm/C %p.%p%%\n", length,
                      set, ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        return true;
}

#ifdef BENCH_delete
static p8 boundaries(void)
{
        p8 guarded[264];
        p8 expected[264];

        choose_marks(2);

        for (positive length = 0; length <= 257; length++)
        {
                for (positive at = 0; at < sizeof(guarded); at++)
                        guarded[at] = expected[at] = source_block[at * 7 + length];

                positive kept = 0;

                for (positive at = 0; at < length; at++)
                        if (!marks[expected[3 + at]])
                                expected[3 + kept++] = expected[3 + at];

                if (memory_delete_bytes(guarded + 3, length, marks) != kept ||
                    memory_compare(guarded, expected, 3 + kept) ||
                    memory_compare(guarded + 3 + length, expected + 3 + length,
                                   sizeof(guarded) - 3 - length))
                        return false;
        }

        return true;
}
#endif

b32 main(void)
{
        static const positive sizes[] = {64, 4096, MAXIMUM};

        prepare_source();
#ifdef BENCH_delete
        if (!boundaries())
        {
                string_format(log, SUBJECT " boundary check failed\n");
                log_flush();
                return 1;
        }
#endif

        string_format(log, SUBJECT ", paired median of %p, restoring copy taken out\n",
                      (positive)TRIES);

        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
                for (positive set = 0; set < 3; set++)
                        if (!row(sizes[at], set))
                        {
                                string_format(log, SUBJECT " result mismatch\n");
                                log_flush();
                                return 1;
                        }

        log_flush();
        return 0;
}
#endif /* BENCH_delete || BENCH_squeeze */

#ifdef BENCH_offsets
/* Every delimiter and line end of a read as offsets: a byte loop in C
   against library assembly, over comma-separated rows. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 16)
#define TARGET_BYTES (1u << 26)

static p8 source_block[MAXIMUM];
static p32 former_offsets[MAXIMUM];
static p32 assembly_offsets[MAXIMUM];
static positive former_count;
static positive assembly_count;
static volatile positive sink;

NOT_INLINED static positive former_offsets_of(p32 address_to positions,
                                              const p8 address_to block,
                                              positive size, p8 first, p8 second)
{
        positive count = 0;

        for (positive at = 0; at < size; at++)
                if (block[at] == first || block[at] == second)
                        positions[count++] = (p32)at;

        return count;
}

// Rows of three to eight fields of up to nine letters.
static fn prepare_source(void)
{
        p64 seed = 0x9e3779b97f4a7c15ull;
        positive fields = 0;

        for (positive at = 0; at < MAXIMUM; at++)
        {
                XORSHIFT64(seed);

                positive pick = (positive)(seed % 8);

                if (pick == 0)
                {
                        fields++;
                        source_block[at] = fields > 3 + (seed >> 20) % 6 ? '\n' : ',';
                        if (source_block[at] == '\n')
                                fields = 0;
                }
                else
                        source_block[at] = (p8)('a' + (seed >> 8) % 26);
        }
}

BENCH_ONCE(run, (p8 assembly, positive length, positive rounds), rounds, round,
        {
                positive count = assembly
                                     ? memory_offsets_of_either(assembly_offsets, source_block,
                                                                length, ',', '\n', MAXIMUM)
                                     : former_offsets_of(former_offsets, source_block,
                                                         length, ',', '\n');

                if (assembly)
                        assembly_count = count;
                else
                        former_count = count;

                sink += count;
        })

NOT_INLINED static positive former_outside(p32 address_to positions,
                                           const p8 address_to block, positive size)
{
        positive count = 0;

        for (positive at = 0; at < size; at++)
                if (block[at] < 0x20 || block[at] > 0x7e)
                        positions[count++] = (p32)at;

        return count;
}

static b8 special_set[256];

NOT_INLINED static positive former_in_set(p32 address_to positions,
                                          const p8 address_to block, positive size)
{
        positive count = 0;

        for (positive at = 0; at < size; at++)
                if (special_set[block[at]])
                        positions[count++] = (p32)at;

        return count;
}

static p8 set_row(positive length)
{
        positive rounds = TARGET_BYTES / length;
        positive ratios[TRIES];

        for (positive i = 0; i < 256; i++)
                special_set[i] = i < 9 || (i > 10 && i < 32) || i > 126 || i == ',';
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 start = get_cpu_time();
                for (positive round = 0; round < rounds; round++)
                        sink += former_count = former_in_set(former_offsets, source_block, length);
                p64 former = get_cpu_time() - start;
                start = get_cpu_time();
                for (positive round = 0; round < rounds; round++)
                        sink += assembly_count = memory_offsets_in_set(assembly_offsets, source_block,
                                                                       length, special_set, MAXIMUM);
                p64 assembly = get_cpu_time() - start;

                if (former_count != assembly_count ||
                    memory_compare(former_offsets, assembly_offsets, former_count * sizeof(p32)))
                        return false;

                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
        }

        order(ratios, TRIES);
        string_format(log, "  in a table set, %p bytes  median asm/C %p.%p%%\n", length,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        return true;
}

static p8 range_row(positive length)
{
        positive rounds = TARGET_BYTES / length;
        positive ratios[TRIES];

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 start = get_cpu_time();
                for (positive round = 0; round < rounds; round++)
                        sink += former_count = former_outside(former_offsets, source_block, length);
                p64 former = get_cpu_time() - start;
                start = get_cpu_time();
                for (positive round = 0; round < rounds; round++)
                        sink += assembly_count = memory_offsets_outside(assembly_offsets, source_block,
                                                                        length, 0x20, 0x7e, MAXIMUM);
                p64 assembly = get_cpu_time() - start;

                if (former_count != assembly_count ||
                    memory_compare(former_offsets, assembly_offsets, former_count * sizeof(p32)))
                        return false;

                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
        }

        order(ratios, TRIES);
        string_format(log, "  outside printable, %p bytes  median asm/C %p.%p%%\n", length,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        return true;
}

static p8 row(positive length)
{
        positive rounds = TARGET_BYTES / length;
        positive ratios[TRIES];

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 former;
                p64 assembly;

                BENCH_BOTH_ORDERS(trial, assembly, former, length, rounds);

                if (former_count != assembly_count ||
                    memory_compare(former_offsets, assembly_offsets,
                                   former_count * sizeof(p32)))
                        return false;

                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
        }

        order(ratios, TRIES);
        string_format(log, "  %p bytes  median asm/C %p.%p%%\n", length,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
        return true;
}

b32 main(void)
{
        static const positive sizes[] = {64, 1024, MAXIMUM};

        prepare_source();
        string_format(log, "memory_offsets_of_either, paired median of %p\n", (positive)TRIES);

        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
                if (!row(sizes[at]) || !range_row(sizes[at]) || !set_row(sizes[at]))
                {
                        string_format(log, "memory_offsets_of_either result mismatch\n");
                        log_flush();
                        return 1;
                }

        log_flush();
        return 0;
}
#endif /* BENCH_offsets */

#ifdef BENCH_record_scans
/* The record scans at their callers' shapes, against the calls they
   replace: cut -w's two string_span_max a field against one
   memory_offsets_fields_blank a line, over lines of text.txt's shape (39
   bytes, fields of about six letters, blank runs of about four); split
   -l's memory_first_of a line against memory_nth_of, tail's memory_last_of
   a line against memory_nth_last_of; and fold -s's two memory_last_of over
   one window against memory_last_of_either. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define BLOCK (1u << 20)
#define LINES (BLOCK / 16)

static p8 text[BLOCK + 64];
static p32 line_start[LINES + 1];
static positive lines;
static p32 edges[2 * 256];
static volatile positive sink;
static b8 inside[256];
static b8 blanks[256];

// Lines of words of one to eleven letters, blank runs of one to seven
// spaces and tabs, a line ending after thirty to fifty bytes.
static fn prepare_text(void)
{
        p64 seed = 0x9e3779b97f4a7c15ull;
        positive at = 0, line = 0;

        lines = 0;
        while (at < BLOCK - 128)
        {
                XORSHIFT64(seed);
                if (at - line > 30 + (seed >> 40) % 20)
                {
                        text[at++] = '\n';
                        line_start[++lines] = (p32)at;
                        line = at;
                        continue;
                }
                positive word = 1 + seed % 11, blank = 1 + (seed >> 8) % 7;

                for (positive i = 0; i < word; i++)
                        text[at++] = (p8)('a' + (seed >> (16 + i)) % 26);
                for (positive i = 0; i < blank; i++)
                        text[at++] = (seed >> (32 + i)) & 3 ? ' ' : '\t';
        }
        for (positive i = 0; i < 256; i++)
        {
                blanks[i] = i == ' ' || i == '\t';
                inside[i] = !blanks[i];
        }
}

// cut -w's walk before: a span to learn whether the line splits, then a
// span over each field and one over the blanks after it.
NOT_INLINED static positive former_fields(const p8 address_to line, positive length)
{
        positive count = 0, at = 0;

        if (string_span_max(line, length, inside) == length)
                return 1;
        while (at <= length)
        {
                positive from = at;

                at += string_span_max(line + at, length - at, inside);
                count += at - from;
                if (at == length)
                        break;
                at++;
                at += string_span_max(line + at, length - at, blanks);
        }
        return count;
}

NOT_INLINED static positive assembly_fields(const p8 address_to line, positive length)
{
        positive found = memory_offsets_fields_blank(edges, line, length, ' ', 256);
        positive count = 0;

        for (positive f = 0; f < found; f++)
                count += edges[2 * f + 1] - edges[2 * f];
        return count;
}

static p64 fields_run(p8 assembly)
{
        p64 start = get_cpu_time();
        positive total = 0;

        for (positive l = 0; l < lines; l++)
        {
                const p8 address_to line = text + line_start[l];
                positive length = line_start[l + 1] - line_start[l] - 1;

                total += assembly ? assembly_fields(line, length) : former_fields(line, length);
        }
        sink += total;
        return get_cpu_time() - start;
}

NOT_INLINED static positive former_nth(const p8 address_to block, positive size, positive n)
{
        const p8 address_to scan = block;

        for (positive i = 0; i < n; i++)
        {
                const p8 address_to found = memory_first_of(scan, '\n', size - (positive)(scan - block));

                if (!found)
                        return size;
                scan = found + 1;
        }
        return (positive)(scan - block);
}

NOT_INLINED static positive former_nth_last(const p8 address_to block, positive size, positive n)
{
        positive stop = size;

        for (positive i = 0; i < n; i++)
        {
                const p8 address_to found = memory_last_of(block, '\n', stop);

                if (!found)
                        return 0;
                stop = (positive)(found - block);
        }
        return stop;
}

static p64 nth_run(p8 assembly, p8 back, positive n)
{
        p64 start = get_cpu_time();
        positive at = 0, total = 0;

        // Pieces of n lines through the block, as split -l cuts it.
        while (at < line_start[lines])
        {
                positive size = line_start[lines] - at;

                if (back)
                {
                        positive window = n * 42 < size ? n * 42 : size;
                        positive got = assembly ? memory_nth_last_of(text + at, '\n', window, n).x
                                                : former_nth_last(text + at, window, n);
                        total += got;
                        at += window;
                        continue;
                }
                positive got = assembly ? memory_nth_of(text + at, '\n', size, n).x
                                        : former_nth(text + at, size, n);
                total += got;
                at += got ? got : size;
        }
        sink += total;
        return get_cpu_time() - start;
}

NOT_INLINED static const p8 address_to former_either(const p8 address_to window, positive size)
{
        const p8 address_to space = memory_last_of(window, ' ', size);
        const p8 address_to tab = memory_last_of(window, '\t', size);

        return tab > space ? tab : space;
}

static p64 either_run(p8 assembly, positive width)
{
        p64 start = get_cpu_time();
        positive total = 0;

        for (positive at = 0; at + width < BLOCK; at += width)
                total += (positive)(assembly ? memory_last_of_either(text + at, ' ', '\t', width)
                                             : former_either(text + at, width));
        sink += total;
        return get_cpu_time() - start;
}

static fn report_row(string_address name, positive address_to ratios, p64 best_c, p64 best_a,
                     positive units, string_address unit)
{
        order(ratios, TRIES);
        string_format(log, "  %s  median asm/C %p.%p%%  C %p ticks/%s x100, asm %p\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100,
                      (positive)(best_c * 100 / units), unit, (positive)(best_a * 100 / units));
}

b32 main(void)
{
        positive ratios[TRIES];
        p64 best_c, best_a;

        prepare_text();
        for (positive l = 0; l < lines; l++)
        {
                const p8 address_to line = text + line_start[l];
                positive length = line_start[l + 1] - line_start[l] - 1;

                if (former_fields(line, length) != assembly_fields(line, length) &&
                    former_fields(line, length) != 1)
                {
                        string_format(log, "record scans: fields disagree at line %p\n", l);
                        log_flush();
                        return 1;
                }
        }
        string_format(log, "record scans, paired median of %p, %p lines of %p bytes\n",
                      (positive)TRIES, lines, (positive)line_start[lines]);

        best_c = best_a = ~0ull;
        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 former = fields_run(false), assembly = fields_run(true);

                best_c = former < best_c ? former : best_c;
                best_a = assembly < best_a ? assembly : best_a;
                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
        }
        report_row("cut -w fields of a line", ratios, best_c, best_a, lines, "line");

        static const positive ns[] = {1, 10, 1000};
        for (positive back = 0; back < 2; back++)
                for (positive k = 0; k < array_count(ns); k++)
                {
                        if (memory_nth_of(text, '\n', BLOCK, ns[k]).x != former_nth(text, BLOCK, ns[k]) ||
                            memory_nth_last_of(text, '\n', 5000, ns[k]).x != former_nth_last(text, 5000, ns[k]))
                        {
                                string_format(log, "record scans: nth disagrees at %p\n", ns[k]);
                                log_flush();
                                return 1;
                        }
                        best_c = best_a = ~0ull;
                        for (positive trial = 0; trial < TRIES; trial++)
                        {
                                p64 former = nth_run(false, back, ns[k]),
                                    assembly = nth_run(true, back, ns[k]);

                                best_c = former < best_c ? former : best_c;
                                best_a = assembly < best_a ? assembly : best_a;
                                ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                        }
                        string_format(log, "  n = %p:", ns[k]);
                        report_row(back ? "memory_nth_last_of" : "memory_nth_of", ratios, best_c, best_a,
                                   line_start[lines] / 100, "100 B");
                }

        static const positive widths[] = {20, 80, 200};
        for (positive k = 0; k < array_count(widths); k++)
        {
                best_c = best_a = ~0ull;
                for (positive trial = 0; trial < TRIES; trial++)
                {
                        p64 former = either_run(false, widths[k]), assembly = either_run(true, widths[k]);

                        best_c = former < best_c ? former : best_c;
                        best_a = assembly < best_a ? assembly : best_a;
                        ratios[trial] = (positive)(assembly * 10000 / (former ? former : 1));
                }
                string_format(log, "  window %p:", widths[k]);
                report_row("memory_last_of_either", ratios, best_c, best_a, BLOCK / widths[k], "window");
        }
        log_flush();
        return 0;
}
#endif /* BENCH_record_scans */

#ifdef BENCH_ascii_case
/* ASCII-folded bounded comparison: scalar reference against library assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 26)

static p8 one[MAXIMUM + 8];
static p8 two[MAXIMUM + 8];
static volatile positive sink;

static p8 folded(p8 value)
{
        return value >= 'a' && value <= 'z' ? (p8)(value - 32) : value;
}

NOT_INLINED static bipolar former_compare(p8 address_to a, p8 address_to b,
                                          positive length)
{
        for (positive at = 0; at < length; at++)
        {
                p8 left = folded(a[at]);
                p8 right = folded(b[at]);

                if (left != right)
                        return (bipolar)left - (bipolar)right;
        }

        return 0;
}

static p8 correctness(void)
{
        p8 a;
        p8 b;

        for (positive left = 0; left < 256; left++)
                for (positive right = 0; right < 256; right++)
                {
                        a = (p8)left;
                        b = (p8)right;

                        bipolar want = (bipolar)folded(a) - (bipolar)folded(b);
                        b32 got = memory_compare_ascii_case(address_of a,
                                                            address_of b, 1);

                        if (got != want)
                        {
                                string_format(log, "pair check failed: %p %p signs %p %p\n",
                                              left, right, (positive)(want < 0),
                                              (positive)(got < 0));
                                return false;
                        }
                }

        for (positive left_offset = 0; left_offset < 8; left_offset++)
                for (positive right_offset = 0; right_offset < 8; right_offset++)
                        for (positive length = 0; length <= 257; length++)
                        {
                                for (positive at = 0; at < length; at++)
                                {
                                        one[left_offset + at] = (p8)(at * 37 + length);
                                        two[right_offset + at] = one[left_offset + at];

                                        if ((at & 7) == 3 && one[left_offset + at] >= 'A' &&
                                            one[left_offset + at] <= 'Z')
                                                two[right_offset + at] += 32;
                                }

                                if (memory_compare_ascii_case(one + left_offset,
                                                              two + right_offset,
                                                              length))
                                {
                                        string_format(log,
                                                      "equal check failed: offsets %p %p length %p\n",
                                                      left_offset, right_offset, length);
                                        return false;
                                }

                                if (!length)
                                        continue;

                                positive positions[] = {0, length / 2, length - 1,
                                                        7, 8, 15, 16, 31, 32};

                                for (positive which = 0;
                                     which < sizeof(positions) / sizeof(positions[0]);
                                     which++)
                                {
                                        positive position = positions[which];

                                        if (position >= length)
                                                continue;

                                        p8 address_to changed = two + right_offset + position;
                                        p8 saved = address_to changed;
                                        address_to changed = saved == 0xff ? 0 : (p8)(saved + 1);

                                        bipolar want = former_compare(one + left_offset,
                                                                       two + right_offset,
                                                                       length);
                                        b32 got = memory_compare_ascii_case(
                                            one + left_offset, two + right_offset, length);

                                        if (got != want)
                                        {
                                                string_format(log,
                                                              "span check failed: offsets %p %p length %p position %p signs %p %p\n",
                                                              left_offset, right_offset, length,
                                                              position, (positive)(want < 0),
                                                              (positive)(got < 0));
                                                return false;
                                        }

                                        address_to changed = saved;
                                }
                        }

        return true;
}

static fn prepare(positive length, p8 late)
{
        for (positive at = 0; at < length; at++)
        {
                p8 value = (p8)(at * 37 + 11);
                one[at] = value;
                two[at] = value >= 'A' && value <= 'Z' && (at & 1)
                              ? (p8)(value + 32)
                              : value;
        }

        if (late)
                two[length - 1] ^= 1;
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / length;
        if (rounds < 8) rounds = 8;
        if (rounds > (1u << 20)) rounds = 1u << 20;
        return rounds;
}

BENCH_ONCE(run, (p8 assembly, positive length, positive rounds), rounds, round,
           sink += (positive)(assembly
                                  ? memory_compare_ascii_case(one, two, length)
                                  : former_compare(one, two, length)))

static fn row(string_address name, positive length, p8 late)
{
        positive ratios[TRIES];
        positive rounds = rounds_for(length);

        prepare(length, late);

        BENCH_PAIRED_RUNS(ratios, former, assembly, length, rounds);
        string_format(log, "  %s  median asm/C %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        if (!correctness())
        {
                string_format(log, "memory_compare_ascii_case check failed\n");
                log_flush();
                return 1;
        }

        moonwater_cpu_detect();
        string_format(log, "ASCII case compare, paired median of %p\n",
                      (positive)TRIES);
        // HTTP field names are ordinarily four to thirty two bytes. Keep the
        // short rows explicit: a bulk fold is not a win merely because its
        // four-kilobyte row is.
        row((string_address)"equal 4", 4, false);
        row((string_address)"equal 8", 8, false);
        row((string_address)"equal 12", 12, false);
        row((string_address)"equal 16", 16, false);
        row((string_address)"equal 24", 24, false);
        row((string_address)"equal 32", 32, false);
        row((string_address)"equal 4K", 4096, false);
        row((string_address)"equal 1M", MAXIMUM, false);
        row((string_address)"late 4K", 4096, true);
        log_flush();
        return 0;
}
#endif /* BENCH_ascii_case */

#ifdef BENCH_ascii_case_fold
/* Literal-size ASCII-folded compare against the out-of-line floor routine. */
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define INLINE_ALWAYS __attribute__((always_inline))
#define SUBJECTS 64
#define ROUNDS (1u << 19)
#define TRIES 9

static p8 left[SUBJECTS][40];
static p8 right[SUBJECTS][40];
static volatile positive sink;

#define BENCH_PAIRED_INDEXED
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

static inline INLINE_ALWAYS p8 case_byte(p8 value)
{
        return value >= 'a' && value <= 'z' ? (p8)(value - 32) : value;
}

static inline INLINE_ALWAYS b32 case_known(const p8 address_to one,
                                           const p8 address_to two,
                                           positive size)
{
        for (positive at = 0; at < size; at++)
        {
                p8 a = case_byte(one[at]);
                p8 b = case_byte(two[at]);

                if (a != b)
                        return (b32)a - (b32)b;
        }

        return 0;
}

static fn prepare(void)
{
        for (positive which = 0; which < SUBJECTS; which++)
                for (positive at = 0; at < 40; at++)
                {
                        p8 value = (p8)(at * 37 + which * 11);

                        left[which][at] = value;
                        right[which][at] =
                            value >= 'A' && value <= 'Z' && ((at + which) & 1)
                                ? (p8)(value + 32)
                                : value;
                }
}

#define DEFINE_SIZE(N)                                                        \
        NOT_INLINED static positive routine_##N(positive which)               \
        {                                                                     \
                return (positive)(memory_compare_ascii_case)(                 \
                    left[which], right[which], (N));                          \
        }                                                                     \
        NOT_INLINED static positive folded_##N(positive which)                \
        {                                                                     \
                return (positive)case_known(left[which], right[which], (N));  \
        }

DEFINE_SIZE(4)
DEFINE_SIZE(8)
DEFINE_SIZE(12)
DEFINE_SIZE(16)
DEFINE_SIZE(24)
DEFINE_SIZE(32)

static fn row(positive size, bench_indexed_work routine,
              bench_indexed_work folded)
{
        positive got = bench_paired_median(routine, folded);

        string_format(log, "  %p bytes: folded/routine %p.%p%%\n", size,
                      got / 100, got % 100);
}

b32 main(void)
{
        prepare();
        moonwater_cpu_detect();
        string_format(log, "literal ASCII-case compare, paired median of %p\n",
                      (positive)TRIES);
        row(4, routine_4, folded_4);
        row(8, routine_8, folded_8);
        row(12, routine_12, folded_12);
        row(16, routine_16, folded_16);
        row(24, routine_24, folded_24);
        row(32, routine_32, folded_32);
        log_flush();
        return 0;
}
#endif /* BENCH_ascii_case_fold */

#ifdef BENCH_line_break
/* Canvas line breaking: the former byte walk against shared bounded hunts. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM 4096
#define TARGET_BYTES (1u << 25)

static p8 text[MAXIMUM];
static volatile positive sink;

NOT_INLINED static positive former_line_end(string_address input,
                                             positive length,
                                             positive start,
                                             positive columns,
                                             p8 wrap)
{
        positive i;
        positive last_space = 0;

        for (i = start; i < length; i++)
        {
                if (input[i] == '\n')
                        return i;

                if (!wrap)
                        continue;

                if (input[i] == ' ')
                        last_space = i;

                if (i - start + 1 > columns)
                        return last_space > start ? last_space : i;
        }

        return length;
}

NOT_INLINED static positive shared_line_end(string_address input,
                                             positive length,
                                             positive start,
                                             positive columns,
                                             p8 wrap)
{
        positive remaining;
        positive scanned;
        string_address found;

        if (start >= length)
                return length;

        remaining = length - start;
        scanned = remaining;

        if (wrap && remaining > columns)
                scanned = columns + 1;

        found = memory_first_of((address_any)(input + start), '\n', scanned);

        if (found)
                return (positive)(found - input);

        if (!wrap || remaining <= columns)
                return length;

        found = memory_last_of((address_any)(input + start), ' ', scanned);

        return found && found > input + start ? (positive)(found - input)
                                              : start + columns;
}

static positive random_state = 0x93d7654bu;

static positive next_random(void)
{
        random_state = random_state * 1664525u + 1013904223u;
        return random_state;
}

static p8 correctness(void)
{
        for (positive length = 0; length < 257; length++)
                for (positive trial = 0; trial < 257; trial++)
                {
                        positive columns = next_random() % 65;
                        positive start = next_random() % (length + 2);
                        p8 wrap = (p8)(next_random() & 1);

                        for (positive at = 0; at < length; at++)
                        {
                                positive pick = next_random() & 31;
                                text[at] = pick == 0 ? '\n' : pick < 6 ? ' '
                                                                         : 'a';
                        }

                        if (former_line_end(text, length, start, columns, wrap) !=
                            shared_line_end(text, length, start, columns, wrap))
                                return false;
                }

        return true;
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / (length ? length : 1);
        if (rounds < 32) rounds = 32;
        if (rounds > (1u << 20)) rounds = 1u << 20;
        return rounds;
}

BENCH_ONCE(run, (p8 shared, positive length, positive columns, p8 wrap,
               positive rounds), rounds, round,
           sink += shared ? shared_line_end(text, length, 0, columns, wrap)
                          : former_line_end(text, length, 0, columns, wrap))

static void row(string_address name, positive length, positive columns, p8 wrap,
                positive space)
{
        positive ratios[TRIES];
        positive rounds = rounds_for(length);

        memory_fill(text, 'a', length);
        if (space < length)
                text[space] = ' ';

        BENCH_PAIRED_RUNS(ratios, former, shared, length, columns, wrap, rounds);
        string_format(log, "  %s  shared/C %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        if (!correctness())
        {
                string_format(log, "line-break correctness failed\n");
                log_flush();
                return 1;
        }

        moonwater_cpu_detect();
        string_format(log, "Canvas line break, paired median of %p\n",
                      (positive)TRIES);
        row((string_address)"label 8", 8, 8, false, MAXIMUM);
        row((string_address)"label 16", 16, 16, false, MAXIMUM);
        row((string_address)"label 32", 32, 32, false, MAXIMUM);
        row((string_address)"label 64", 64, 64, false, MAXIMUM);
        row((string_address)"label 128", 128, 128, false, MAXIMUM);
        row((string_address)"wrap 16 no space", 128, 16, true, MAXIMUM);
        row((string_address)"wrap 32 mid space", 128, 32, true, 20);
        row((string_address)"wrap 64 mid space", 192, 64, true, 40);
        row((string_address)"wrap 80 mid space", 256, 80, true, 48);
        log_flush();
        return 0;
}
#endif /* BENCH_line_break */

#ifdef BENCH_bounded_fold
/*
        Literal-bound string operations at the production shapes which can
        actually reach lib.util.c's bounded specializers.

        HTTP compares seven and eight bytes, resolv.conf compares eleven, and
        interface names are copied through a fifteen-byte bound.  Each row
        keeps the literal at the call site on the expanded side and hides the
        same value behind a volatile load on the assembly-routine side.  The
        source rotates through short, full, equal, early-different and
        late-different cases so no row measures one friendly exit alone.
*/
#include "../../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define SUBJECTS 64
#define ROUNDS (1u << 19)
#define TRIES 9

static p8 left[SUBJECTS][32];
static p8 right[SUBJECTS][32];
static p8 target[SUBJECTS][32];
static volatile positive sink;

#define BENCH_PAIRED_INDEXED
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

static fn prepare(void)
{
        for (positive which = 0; which < SUBJECTS; which++)
        {
                positive stop = which & 15;

                for (positive at = 0; at < 32; at++)
                        left[which][at] = right[which][at] =
                            (p8)('a' + (which * 3 + at * 5) % 23);

                if ((which & 3) == 0)
                        left[which][stop] = right[which][stop] = 0;
                else if ((which & 3) == 1)
                        right[which][0] ^= 0x11;
                else if ((which & 3) == 2)
                        right[which][14] ^= 0x21;
        }
}

#define DEFINE_BOUND(B)                                                       \
        NOT_INLINED static positive length_routine_##B(positive which)        \
        {                                                                     \
                return (string_length_max)(left[which], (B));                 \
        }                                                                     \
        NOT_INLINED static positive length_folded_##B(positive which)         \
        {                                                                     \
                return string_length_max(left[which], (B));                   \
        }                                                                     \
        NOT_INLINED static b32 compare_routine_##B(positive which)            \
        {                                                                     \
                return (string_compare_max)(left[which], right[which],        \
                                            (B));                             \
        }                                                                     \
        NOT_INLINED static b32 compare_folded_##B(positive which)             \
        {                                                                     \
                return string_compare_max(left[which], right[which], (B));    \
        }                                                                     \
        NOT_INLINED static positive copy_routine_##B(positive which)          \
        {                                                                     \
                return (positive)(string_copy_max_end)(                       \
                    target[which], left[which], (B));                         \
        }                                                                     \
        NOT_INLINED static positive copy_folded_##B(positive which)           \
        {                                                                     \
                return (positive)string_copy_max_end(                         \
                    target[which], left[which], (B));                         \
        }

DEFINE_BOUND(7)
DEFINE_BOUND(8)
DEFINE_BOUND(11)
DEFINE_BOUND(15)

static fn show(string_address name, positive bound,
               bench_indexed_work routine, bench_indexed_work folded)
{
        positive got = bench_paired_median(routine, folded);

        string_format(log, "  %s %p: folded/routine %p.%p%%\n", name, bound,
                      got / 100, got % 100);
}

#define SHOW(B)                                                               \
        show((string_address)"length", (B), length_routine_##B,              \
             length_folded_##B);                                              \
        show((string_address)"compare", (B),                                \
             (bench_indexed_work)compare_routine_##B,                         \
             (bench_indexed_work)compare_folded_##B);                         \
        show((string_address)"copy-end", (B), copy_routine_##B,              \
             copy_folded_##B)

b32 main(void)
{
        prepare();
        moonwater_cpu_detect();
        string_format(log, "literal bounded strings, paired median of %p\n",
                      (positive)TRIES);
        SHOW(7);
        SHOW(8);
        SHOW(11);
        SHOW(15);
        log_flush();
        return 0;
}
#endif /* BENCH_bounded_fold */

#ifdef BENCH_ascii_convert
/* ASCII case conversion: inlined former loops against reusable assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 7
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 24)

enum case_operation { LOWER_CASE, UPPER_CASE };
enum case_method {
        FORMER_LOOP,
        FORMER_FIXED,
        SCALAR_CALLS,
        BULK_ASSEMBLY,
        TABLE_ASSEMBLY
};

static p8 former_block[MAXIMUM + 16];
static p8 fixed_block[MAXIMUM + 16];
static p8 scalar_block[MAXIMUM + 16];
static p8 bulk_block[MAXIMUM + 16];
static p8 table_block[MAXIMUM + 16];
static p8 lower_table[256];
static p8 upper_table[256];
static volatile positive sink;

NOT_INLINED static address_any former_convert(p8 address_to block,
                                              positive length,
                                              enum case_operation operation)
{
        for (positive at = 0; at < length; at++)
        {
                p8 value = block[at];

                if (operation == LOWER_CASE)
                        block[at] = value >= 'A' && value <= 'Z'
                                        ? (p8)(value + 32) : value;
                else
                        block[at] = value >= 'a' && value <= 'z'
                                        ? (p8)(value - 32) : value;
        }

        return block;
}

// dd has selected its conv mode outside the loop, unlike the shell expansion
// loop above whose direction is data. Keep both caller shapes in the lane.
NOT_INLINED static address_any former_fixed_convert(
    p8 address_to block, positive length, enum case_operation operation)
{
        if (operation == LOWER_CASE)
                for (positive at = 0; at < length; at++)
                {
                        p8 value = block[at];
                        block[at] = value >= 'A' && value <= 'Z'
                                        ? (p8)(value + 32) : value;
                }
        else
                for (positive at = 0; at < length; at++)
                {
                        p8 value = block[at];
                        block[at] = value >= 'a' && value <= 'z'
                                        ? (p8)(value - 32) : value;
                }

        return block;
}

NOT_INLINED static address_any scalar_convert(p8 address_to block,
                                              positive length,
                                              enum case_operation operation)
{
        for (positive at = 0; at < length; at++)
                block[at] = operation == LOWER_CASE
                                ? byte_to_lower(block[at])
                                : byte_to_upper(block[at]);

        return block;
}

static fn fill(p8 address_to block, positive length, positive salt)
{
        for (positive at = 0; at < length; at++)
                block[at] = (p8)(at * 37 + salt);
}

static p8 correctness_one(enum case_operation operation)
{
        p8 guarded[4128];
        p8 expected[4128];
        p8 fixed[4128];
        p8 scalar[4128];
        p8 bulk[4128];
        p8 table[4128];
        static const positive offsets[] = {0, 1, 7, 15, 31, 4093, 4095};
        static const positive lengths[] = {0, 1, 2, 15, 16, 17, 31, 32, 33};
        p8 address_to translation = operation == LOWER_CASE
                                        ? lower_table : upper_table;

        for (positive value = 0; value < 256; value++)
        {
                p8 byte = (p8)value;
                p8 wanted = operation == LOWER_CASE
                                ? (byte >= 'A' && byte <= 'Z'
                                       ? (p8)(byte + 32) : byte)
                                : (byte >= 'a' && byte <= 'z'
                                       ? (p8)(byte - 32) : byte);

                if ((operation == LOWER_CASE ? byte_to_lower(byte)
                                             : byte_to_upper(byte)) != wanted ||
                    translation[value] != wanted)
                        return false;
        }

        for (positive oi = 0; oi < sizeof(offsets) / sizeof(offsets[0]); oi++)
                for (positive li = 0; li < sizeof(lengths) / sizeof(lengths[0]); li++)
                {
                        positive offset = offsets[oi];
                        positive length = lengths[li];

                        if (offset + length > sizeof(guarded))
                                continue;

                        fill(guarded, sizeof(guarded), offset + length);
                        memory_copy(expected, guarded, sizeof(guarded));
                        memory_copy(fixed, guarded, sizeof(guarded));
                        memory_copy(scalar, guarded, sizeof(guarded));
                        memory_copy(bulk, guarded, sizeof(guarded));
                        memory_copy(table, guarded, sizeof(guarded));

                        former_convert(expected + offset, length, operation);
                        former_fixed_convert(fixed + offset, length, operation);
                        scalar_convert(scalar + offset, length, operation);
                        address_any bulk_answer = operation == LOWER_CASE
                                                      ? memory_to_lower_ascii(
                                                            bulk + offset, length)
                                                      : memory_to_upper_ascii(
                                                            bulk + offset, length);

                        if (bulk_answer != bulk + offset ||
                            memory_translate(table + offset, length, translation) !=
                            table + offset ||
                            memory_compare(guarded, expected, offset) ||
                            memory_compare(expected, fixed, sizeof(guarded)) ||
                            memory_compare(expected, scalar, sizeof(guarded)) ||
                            memory_compare(expected, bulk, sizeof(guarded)) ||
                            memory_compare(expected, table, sizeof(guarded)))
                                return false;
                }

        return true;
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / length;

        if (rounds < 8)
                rounds = 8;
        if (rounds > (1u << 20))
                rounds = 1u << 20;
        return rounds;
}

static p64 run(enum case_method method, enum case_operation operation,
               positive length, positive rounds)
{
        p8 address_to block = method == FORMER_LOOP ? former_block
                              : method == FORMER_FIXED ? fixed_block
                              : method == SCALAR_CALLS ? scalar_block
                              : method == BULK_ASSEMBLY ? bulk_block
                                                       : table_block;
        p8 address_to translation = operation == LOWER_CASE
                                        ? lower_table : upper_table;
        p64 start = get_cpu_time();

        for (positive round = 0; round < rounds; round++)
        {
                address_any answer = method == FORMER_LOOP
                                         ? former_convert(block, length, operation)
                                     : method == FORMER_FIXED
                                         ? former_fixed_convert(block, length, operation)
                                     : method == SCALAR_CALLS
                                         ? scalar_convert(block, length, operation)
                                     : method == BULK_ASSEMBLY
                                         ? (operation == LOWER_CASE
                                                ? memory_to_lower_ascii(block, length)
                                                : memory_to_upper_ascii(block, length))
                                         : memory_translate(block, length, translation);
                sink += (positive)answer + block[0] + block[length - 1];
        }

        return get_cpu_time() - start;
}

static fn row(enum case_operation operation, positive length)
{
        positive scalar_ratios[TRIES];
        positive bulk_ratios[TRIES];
        positive fixed_ratios[TRIES];
        positive table_ratios[TRIES];
        positive rounds = rounds_for(length);

        for (positive trial = 0; trial < TRIES; trial++)
        {
                p64 former;
                p64 fixed;
                p64 scalar;
                p64 bulk;
                p64 table;

                fill(former_block, length, trial + 11);
                memory_copy(fixed_block, former_block, length);
                memory_copy(scalar_block, former_block, length);
                memory_copy(bulk_block, former_block, length);
                memory_copy(table_block, former_block, length);

                if (trial & 1)
                {
                        table = run(TABLE_ASSEMBLY, operation, length, rounds);
                        bulk = run(BULK_ASSEMBLY, operation, length, rounds);
                        scalar = run(SCALAR_CALLS, operation, length, rounds);
                        fixed = run(FORMER_FIXED, operation, length, rounds);
                        former = run(FORMER_LOOP, operation, length, rounds);
                }
                else
                {
                        former = run(FORMER_LOOP, operation, length, rounds);
                        fixed = run(FORMER_FIXED, operation, length, rounds);
                        scalar = run(SCALAR_CALLS, operation, length, rounds);
                        bulk = run(BULK_ASSEMBLY, operation, length, rounds);
                        table = run(TABLE_ASSEMBLY, operation, length, rounds);
                }

                if (memory_compare(former_block, fixed_block, length) ||
                    memory_compare(former_block, scalar_block, length) ||
                    memory_compare(former_block, bulk_block, length) ||
                    memory_compare(former_block, table_block, length))
                {
                        string_format(log, "  result mismatch at %p bytes\n", length);
                        return;
                }

                scalar_ratios[trial] = (positive)(scalar * 10000 /
                                                   (former ? former : 1));
                bulk_ratios[trial] = (positive)(bulk * 10000 /
                                                 (former ? former : 1));
                fixed_ratios[trial] = (positive)(bulk * 10000 /
                                                  (fixed ? fixed : 1));
                table_ratios[trial] = (positive)(table * 10000 /
                                                  (former ? former : 1));
        }

        order(scalar_ratios, TRIES);
        order(bulk_ratios, TRIES);
        order(fixed_ratios, TRIES);
        order(table_ratios, TRIES);
        string_format(log,
                      "  %p bytes  scalar/dispatch %p.%p%%  bulk/dispatch %p.%p%%  bulk/fixed %p.%p%%  table/dispatch %p.%p%%\n",
                      length, scalar_ratios[TRIES / 2] / 100,
                      scalar_ratios[TRIES / 2] % 100,
                      bulk_ratios[TRIES / 2] / 100,
                      bulk_ratios[TRIES / 2] % 100,
                      fixed_ratios[TRIES / 2] / 100,
                      fixed_ratios[TRIES / 2] % 100,
                      table_ratios[TRIES / 2] / 100,
                      table_ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        static const positive sizes[] = {1, 8, 16, 24, 32, 48, 64,
                                         128, 256, 4096, MAXIMUM};

        for (positive value = 0; value < 256; value++)
        {
                lower_table[value] = value >= 'A' && value <= 'Z'
                                         ? (p8)(value + 32) : (p8)value;
                upper_table[value] = value >= 'a' && value <= 'z'
                                         ? (p8)(value - 32) : (p8)value;
        }

        if (!correctness_one(LOWER_CASE) || !correctness_one(UPPER_CASE))
        {
                string_format(log, "ASCII conversion correctness failed\n");
                log_flush();
                return 1;
        }

        string_format(log, "ASCII lower, paired median of %p\n", (positive)TRIES);
        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
                row(LOWER_CASE, sizes[at]);

        string_format(log, "ASCII upper, paired median of %p\n", (positive)TRIES);
        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
                row(UPPER_CASE, sizes[at]);

        log_flush();
        return 0;
}
#endif /* BENCH_ascii_convert */

#ifdef BENCH_ascii_search
/* Fixed ASCII-insensitive search against the literal C loop it replaces. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define ROOM (1u << 20)

static p8 hay[ROOM + 64];
static p8 needle[64];
static volatile positive sink;

static p8 lower_ascii(p8 value)
{
        return value >= 'A' && value <= 'Z' ? (p8)(value + 32) : value;
}

NOT_INLINED static address_any former_find(p8 address_to text, positive length,
                                            p8 address_to want, positive size)
{
        if (!size)
                return text;

        p8 head = want[0];
        p8 upper = head >= 'a' && head <= 'z' ? (p8)(head - 32) : head;

        for (positive at = 0; at + size <= length;)
        {
                positive left = length - at - size + 1;
                p8 address_to low = memory_first_of(text + at, head, left);
                p8 address_to high = upper == head
                                         ? null
                                         : memory_first_of(text + at, upper, left);
                p8 address_to hit = !low ? high : (!high || low < high ? low : high);

                if (!hit)
                        return null;

                at = (positive)(hit - text);
                positive i = 1;

                while (i < size && lower_ascii(text[at + i]) == want[i])
                        i++;

                if (i == size)
                        return text + at;

                at++;
        }

        return null;
}

BENCH_ONCE(run, (p8 assembly, positive length, positive size, positive rounds), rounds, i,
           sink += (positive)(assembly
                                  ? memory_search_ascii_case(hay, length,
                                                             needle, size)
                                  : former_find(hay, length, needle, size)))

static fn row(string_address name, positive length, string_address wanted,
              p8 late, positive traffic)
{
        positive size = string_length(wanted);

        memory_copy(needle, wanted, size);

        if (late)
        {
                positive at = length - size;

                for (positive i = 0; i < size; i++)
                {
                        p8 value = needle[i];
                        hay[at + i] = value >= 'a' && value <= 'z'
                                          ? (p8)(value - 32)
                                          : value;
                }
        }

        positive rounds = traffic / (length ? length : 1) + 1;
        positive ratios[TRIES];

        BENCH_PAIRED_RUNS(ratios, former, assembly, length, size, rounds);
        string_format(log, "  %s  median asm/former %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        moonwater_cpu_detect();

        for (positive i = 0; i < ROOM; i++)
                hay[i] = (p8)("alpha beta gamma delta epsilon "[i % 31]);

        string_format(log, "ASCII-insensitive fixed search, paired median of %p\n",
                      (positive)TRIES);
        row((string_address)"1 MiB absent rare", ROOM,
            (string_address)"zqxjv", false, 1u << 15);
        row((string_address)"4 KiB late hit", 4096,
            (string_address)"epsilon zeta", true, 1);

        for (positive i = 0; i < ROOM; i++)
                hay[i] = 'a';

        row((string_address)"4 KiB common false candidates", 4096,
            (string_address)"aaaaaaab", false, 1);

        for (positive i = 0; i < 96; i++)
                hay[i] = (p8)("alpha beta gamma delta epsilon "[i % 31]);

        row((string_address)"32 byte absent", 32,
            (string_address)"zeta", false, 1u << 16);
        row((string_address)"32 byte late hit", 32,
            (string_address)"epsilon", true, 1u << 16);

        log_flush();
        return 0;
}
#endif /* BENCH_ascii_search */

#ifdef BENCH_grep_search
/*
        Grep's repeated literal hunt: preparing the rare byte once versus
        asking memory_search to choose its anchors again after every match.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define ROOM (1u << 20)

static p8 hay[ROOM + 64];
static p8 needle[64];
static volatile positive sink;

struct prepared_search
{
        p8 address_to needle;
        positive size;
        positive anchor;
        positive second_anchor;
        p8 icase;
};

static fn prepare(struct prepared_search address_to search,
                  p8 address_to wanted, positive size, p8 icase)
{
        positive2 anchors = memory_search_prepare(wanted, size, icase);

        search->needle = wanted;
        search->size = size;
        search->anchor = anchors.x;
        search->second_anchor = anchors.y;
        search->icase = icase;
}

NOT_INLINED static p8 address_to prepared_find(
    p8 address_to text, positive length,
    const struct prepared_search address_to search)
{
        return search->icase
                   ? memory_search_ascii_case_prepared(
                         text, length, search->needle, search->size,
                         search->anchor, search->second_anchor)
                   : memory_search_prepared(text, length, search->needle,
                                            search->size, search->anchor,
                                            search->second_anchor);
}

static positive scan(p8 prepared, positive length,
                     struct prepared_search address_to search)
{
        positive at = 0;
        positive count = 0;

        while (at <= length)
        {
                p8 address_to found = prepared
                                          ? prepared_find(hay + at, length - at,
                                                          search)
                                          : (search->icase
                                                 ? memory_search_ascii_case(
                                                       hay + at, length - at,
                                                       search->needle, search->size)
                                                 : memory_search(
                                                       hay + at, length - at,
                                                       search->needle, search->size));

                if (!found)
                        break;

                count++;
                at = (positive)(found - hay) + max(search->size, 1ull);
        }

        return count;
}

BENCH_ONCE(run, (p8 prepared, positive length,
               struct prepared_search address_to search, positive rounds), rounds, round,
           sink += scan(prepared, length, search))

static fn row(string_address name, string_address wanted, p8 icase,
              positive traffic)
{
        struct prepared_search search;
        positive size = string_length(wanted);
        positive rounds = traffic / ROOM + 1;
        positive ratios[TRIES];

        memory_copy(needle, wanted, size);
        prepare(address_of search, needle, size, icase);

        if (scan(false, ROOM, address_of search) !=
            scan(true, ROOM, address_of search))
        {
                string_format(log, "  %s: answers differ\n", name);
                return;
        }

        BENCH_PAIRED_RUNS(ratios, current, prepared_time, ROOM, address_of search, rounds);
        string_format(log, "  %s  prepared/current %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        moonwater_cpu_detect();

        for (positive at = 0; at < ROOM; at++)
                hay[at] = (p8)("alpha:123:plain\n"[at % 16]);

        for (positive at = 121; at + 6 < ROOM; at += 256)
                memory_copy(hay + at, (address_any)"needle", 6);

        string_format(log, "prepared grep search, paired median of %p\n",
                      (positive)TRIES);
        row((string_address)"needle every 256 bytes", (string_address)"needle",
            false, 1u << 25);
        row((string_address)"absent rare", (string_address)"zqxjv", false,
            1u << 25);
        row((string_address)"folded needle", (string_address)"NEEDLE", true,
            1u << 25);

        for (positive at = 0; at < ROOM; at++)
                hay[at] = 'a';

        row((string_address)"common false candidates",
            (string_address)"aaaaaaab", false, 1u << 25);
        row((string_address)"dense matches", (string_address)"aaaa", false,
            1u << 22);

        log_flush();
        return 0;
}
#endif /* BENCH_grep_search */

#ifdef BENCH_grep_count
/*
        Fixed-needle record counting at three layers:

        read-only is an explicit serialized traffic lower-bound candidate;
        byte count is a semantic delimiter-count proxy; repeated is grep's
        former prepared-search/newline-search loop; and fused is the
        record-aware assembly core which keeps its anchors live.

        The absolute result is counter ticks per KiB.  On x86_64 those are TSC
        ticks; under qemu they are useful only as paired ratios, as the bench
        driver in test/run says.  Output is consumed through sink and every shape is checked
        against a deliberately obvious record-by-record reference first.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define PAGE 4096

#if defined(__x86_64__)
#define ROOM (1u << 22)
#define ROUNDS 32
#else
#define ROOM (1u << 16)
#define ROUNDS 4
#endif

static p8 block[ROOM + 64];
static p8 needle[] = "needle";
static volatile positive sink;
static positive2 anchors;

/* The explicit read-only traffic candidate from the floor benchmark, kept beside the
   semantic microkernel so both are timed with the same fences and corpus. */
__asm__(
    ".text\n"
    ASM_FUNC(grep_floor_ticks)
#if X64
    "lfence\n   rdtsc\n   shl $32, %rdx\n   or %rdx, %rax\n   lfence\n"
#elif ARM64
    "isb\n   mrs x0, cntvct_el0\n   isb\n"
#else
    "fence iorw, iorw\n   rdtime a0\n   fence iorw, iorw\n"
#endif
    ASM_RET
    ASM_END(grep_floor_ticks)
    ASM_FUNC(grep_floor_read)
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
    "6:  add (%rdi), %rax\n   add $8, %rdi\n   sub $8, %rsi\n"
    "cmp $8, %rsi\n   jae 6b\n   9:\n"
#elif ARM64
    "mov x2, #0\n   cbz x1, 9f\n"
    "1:  cmp x1, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x0]\n   ldp q2, q3, [x0, #32]\n"
    "add x0, x0, #64\n   sub x1, x1, #64\n   b 1b\n"
    "5:  cbz x1, 9f\n"
    "6:  ldr x3, [x0]\n   add x2, x2, x3\n   add x0, x0, #8\n"
    "subs x1, x1, #8\n   b.hi 6b\n   9:  mov x0, x2\n"
#else
    "li a2, 0\n   beqz a1, 9f\n"
    "1:  li a3, 8\n   bltu a1, a3, 9f\n"
    "ld a4, 0(a0)\n   add a2, a2, a4\n   addi a0, a0, 8\n"
    "addi a1, a1, -8\n   j 1b\n   9:  mv a0, a2\n"
#endif
    ASM_RET
    ASM_END(grep_floor_read)
);

positive grep_floor_read(address_any data, positive size);
p64 grep_floor_ticks(void);

static positive reference(p8 address_to data, positive size)
{
        positive count = 0;
        positive start = 0;

        while (start < size)
        {
                positive stop = start;

                while (stop < size && data[stop] != '\n')
                        stop++;

                for (positive at = start; at + 6 <= stop; at++)
                        if (!memory_compare(data + at, needle, 6))
                        {
                                count++;
                                break;
                        }

                start = stop + (stop < size);
        }

        return count;
}

NOT_INLINED static positive repeated(p8 address_to data, positive size)
{
        positive count = 0;
        positive at = 0;

        while (at + 6 <= size)
        {
                p8 address_to found = memory_search_prepared(
                    data + at, size - at, needle, 6, anchors.x, anchors.y);

                if (!found)
                        break;

                count++;
                found += 6;

                p8 address_to delimiter = memory_first_of(
                    found, '\n', size - (positive)(found - data));

                if (!delimiter)
                        break;

                at = (positive)(delimiter - data) + 1;
        }

        return count;
}

NOT_INLINED static positive fused(p8 address_to data, positive size)
{
        return memory_count_records_with_prepared(
            data, size, needle, 6, anchors.x, anchors.y, '\n');
}

static p64 run(positive which)
{
        p64 started = grep_floor_ticks();

        for (positive round = 0; round < ROUNDS; round++)
                sink += which == 0 ? grep_floor_read(block, ROOM)
                                   : which == 1 ? memory_count(block, ROOM, '\n')
                                   : which == 2 ? repeated(block, ROOM)
                                                : fused(block, ROOM);

        return grep_floor_ticks() - started;
}

static fn row(string_address name, positive which)
{
        positive samples[TRIES];

        for (positive trial = 0; trial < TRIES; trial++)
                samples[trial] = (positive)run(which);

        order(samples, TRIES);
        positive ticks = samples[TRIES / 2];
        positive thousandths = ticks * 1024 * 1000 /
                               ((positive)ROOM * ROUNDS);

        string_format(log, "  %s  %p.%p ticks/KiB\n", name,
                      thousandths / 1000, thousandths % 1000);
}

static fn gap(void)
{
        positive ratios[TRIES];

        BENCH_PAIRED(ratios, floor_time, run(0),
                     fused_time, run(3));
        positive ratio = ratios[TRIES / 2];

        if (ratio < 10000)
                string_format(log, "  lower-bound status  unresolved "
                                   "(read-only candidate slower than fused)\n");
        else
                string_format(log, "  fused gap to read-only bound  %p.%p%% slower\n",
                              (ratio - 10000) / 100,
                              (ratio - 10000) % 100);
}

static bipolar named(string_address name)
{
        if (!string_compare(name, (string_address)"read"))
                return 0;
        if (!string_compare(name, (string_address)"delimiter"))
                return 1;
        if (!string_compare(name, (string_address)"repeated"))
                return 2;
        if (!string_compare(name, (string_address)"fused"))
                return 3;

        return -1;
}

static p8 boundaries(void)
{
        p8 small[320];

        for (positive size = 0; size <= 257; size++)
                for (positive shift = 0; shift < 23; shift++)
                {
                        for (positive at = 0; at < size; at++)
                                small[at] = (at + shift) % 17 == 16 ? '\n' : 'x';

                        for (positive at = shift; at + 6 <= size; at += 43)
                                memory_copy(small + at, needle, 6);

                        positive expected = reference(small, size);

                        if (repeated(small, size) != expected ||
                            fused(small, size) != expected)
                                return false;
                }

        return true;
}

/* Each end of the exact span is placed against an inaccessible page in turn.
   The three shapes
   cover an absent sparse anchor, one match in nearly every short record and
   a final record without a delimiter; the model remains byte-at-a-time and
   independent of both optimized searches. */
static p8 guarded_boundaries(void)
{
        p8 address_to base = (p8 address_to)memory(3 * PAGE);

        if (!base ||
            system_call_3(syscall(mprotect), (positive)base, PAGE,
                          FILE_PROTECT_NONE) < 0 ||
            system_call_3(syscall(mprotect), (positive)(base + 2 * PAGE), PAGE,
                          FILE_PROTECT_NONE) < 0)
                return false;

        p8 address_to room = base + PAGE;

        for (positive size = 0; size <= 257; size++)
                for (positive side = 0; side < 2; side++)
                        for (positive shape = 0; shape < 3; shape++)
                        {
                                p8 address_to data = side ? room + PAGE - size
                                                          : room;

                                for (positive at = 0; at < size; at++)
                                        data[at] = shape == 0 ? 'x' :
                                                   (at % 11 == 10 ? '\n' : 'q');

                                if (shape)
                                        for (positive at = shape - 1;
                                             at + 6 <= size; at += 11)
                                                memory_copy(data + at, needle, 6);

                                if (shape == 2 && size && data[size - 1] == '\n')
                                        data[size - 1] = 'q';

                                if (fused(data, size) != reference(data, size))
                                {
                                        memory_free(base, 3 * PAGE);
                                        return false;
                                }
                        }

        memory_free(base, 3 * PAGE);
        return true;
}

b32 main(void)
{
        moonwater_cpu_detect();
        anchors = memory_search_prepare(needle, 6, false);

        for (positive at = 0; at < ROOM; at++)
                block[at] = (p8)"alpha:123:plain\n"[at % 16];

        for (positive at = 121; at + 6 < ROOM; at += 272)
                memory_copy(block + at, needle, 6);

        /* A named lane gives perf a long, quiet interval for retired-event
           counters. Its denominator is 16 * ROUNDS * ROOM bytes. */
        if (program_argument_count() > 1)
        {
                bipolar which = named(program_argument(1));

                if (which < 0)
                        return 2;

                for (positive pass = 0; pass < 16; pass++)
                        sink += (positive)run((positive)which);

                return 0;
        }

        if (!boundaries() || !guarded_boundaries() ||
            repeated(block, ROOM) != fused(block, ROOM))
        {
                string_format(log, "grep record count check failed\n");
                log_flush();
                return 1;
        }

        string_format(log, "grep record count, %p-byte resident input\n",
                      (positive)ROOM);
        row((string_address)"read-only traffic candidate", 0);
        row((string_address)"full-byte delimiter count", 1);
        row((string_address)"repeated search + newline", 2);
        row((string_address)"fused semantic scan", 3);
        gap();
        log_flush();
        return 0;
}
#endif /* BENCH_grep_count */

#ifdef BENCH_last_of
/* Bounded reverse byte search: scalar reference against library assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM (1u << 20)
#define TARGET_BYTES (1u << 26)
#define NEEDLE 0xa7

static p8 block[MAXIMUM + 64];
static volatile positive sink;

NOT_INLINED static address_any former_last(address_any memory, b8 value,
                                           positive length)
{
        p8 address_to bytes = memory;

        while (length)
        {
                length--;
                if (bytes[length] == (p8)value)
                        return bytes + length;
        }

        return null;
}

static p8 correctness(void)
{
        static const positive values[] = {0, 37, 255};
        p8 guarded[336];

        if (memory_last_of(null, 0, 0) != null)
                return false;

        for (positive offset = 0; offset < 16; offset++)
                for (positive length = 0; length <= 257; length++)
                        for (positive v = 0; v < sizeof(values) / sizeof(values[0]); v++)
                        {
                                p8 needle = (p8)values[v];

                                for (positive at = 0; at < sizeof(guarded); at++)
                                        guarded[at] = (p8)(needle + 1);

                                if (memory_last_of(guarded + offset, (b8)needle,
                                                   length) != null)
                                        return false;

                                for (positive position = 0; position < length; position++)
                                {
                                        guarded[offset + position] = needle;

                                        if (memory_last_of(guarded + offset, (b8)needle,
                                                           length) !=
                                            guarded + offset + position)
                                                return false;

                                        guarded[offset + position] = (p8)(needle + 1);
                                }

                                if (length > 1)
                                {
                                        guarded[offset] = needle;
                                        guarded[offset + length - 1] = needle;

                                        if (memory_last_of(guarded + offset, (b8)needle,
                                                           length) !=
                                            guarded + offset + length - 1)
                                                return false;
                                }
                        }

        return true;
}

static fn prepare(positive length, positive hit)
{
        for (positive at = 0; at < length; at++)
                block[at] = (p8)(at * 2 + 2);

        if (hit < length)
                block[hit] = NEEDLE;
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / length;
        if (rounds < 8) rounds = 8;
        if (rounds > (1u << 20)) rounds = 1u << 20;
        return rounds;
}

BENCH_ONCE(run, (p8 assembly, positive length, positive rounds), rounds, round,
        {
                address_any found = assembly
                                        ? memory_last_of(block, (b8)NEEDLE, length)
                                        : former_last(block, (b8)NEEDLE, length);
                sink += (positive)found;
        })

static fn row(string_address name, positive length, positive hit)
{
        positive ratios[TRIES];
        positive rounds = rounds_for(length);

        prepare(length, hit);

        BENCH_PAIRED_RUNS(ratios, former, assembly, length, rounds);
        string_format(log, "  %s  median asm/C %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        if (!correctness())
        {
                string_format(log, "memory_last_of check failed\n");
                log_flush();
                return 1;
        }

        moonwater_cpu_detect();
        string_format(log, "memory_last_of, paired median of %p\n",
                      (positive)TRIES);
        row((string_address)"absent 8", 8, 8);
        row((string_address)"absent 64", 64, 64);
        row((string_address)"absent 4K", 4096, 4096);
        row((string_address)"absent 1M", MAXIMUM, MAXIMUM);
        row((string_address)"front hit 4K", 4096, 0);
        row((string_address)"end hit 64", 64, 63);
        log_flush();
        return 0;
}
#endif /* BENCH_last_of */

#ifdef BENCH_words
/* Stateful ASCII word counting: scalar reference against library assembly. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define NOT_INLINED __attribute__((noinline, noclone))
#define TRIES 9
#define MAXIMUM 65536
#define TARGET_BYTES (1u << 26)

static p8 block[MAXIMUM + 16];
static volatile positive sink;

static p8 is_space(p8 value)
{
        return value == 0x20 || (value >= 0x09 && value <= 0x0d);
}

NOT_INLINED static positive2 former_words(address_any memory, positive length,
                                          p8 inside)
{
        p8 address_to bytes = memory;
        positive2 answer = {.x = 0, .y = inside ? 1 : 0};

        for (positive at = 0; at < length; at++)
        {
                p8 next = !is_space(bytes[at]);

                if (next && !answer.y)
                        answer.x++;

                answer.y = next;
        }

        return answer;
}

static p8 same(positive2 a, positive2 b)
{
        return a.x == b.x && a.y == b.y;
}

static p8 correctness(void)
{
        p8 byte;

        for (positive value = 0; value < 256; value++)
                for (positive inside = 0; inside < 2; inside++)
                {
                        byte = (p8)value;
                        if (!same(memory_count_words(address_of byte, 1, (p8)inside),
                                  former_words(address_of byte, 1, (p8)inside)))
                                return false;
                }

        for (positive offset = 0; offset < 16; offset++)
                for (positive length = 0; length <= 257; length++)
                {
                        static const p8 alphabet[] = {
                            0, 8, 9, 10, 11, 12, 13, 14, 31, 32, 33, 127, 128, 255,
                        };

                        for (positive at = 0; at < length; at++)
                                block[offset + at] = alphabet[(at * 7 + length) %
                                                              sizeof(alphabet)];

                        for (positive inside = 0; inside < 2; inside++)
                        {
                                positive2 want = former_words(block + offset, length,
                                                               (p8)inside);
                                positive2 got = memory_count_words(block + offset, length,
                                                                   (p8)inside);

                                if (!same(want, got))
                                        return false;

                                positive splits[] = {0, length / 2, length, 7, 8,
                                                     15, 16, 31, 32, 63, 64};

                                for (positive which = 0;
                                     which < sizeof(splits) / sizeof(splits[0]);
                                     which++)
                                {
                                        positive split = splits[which];

                                        if (split > length)
                                                continue;

                                        positive2 first = memory_count_words(
                                            block + offset, split, (p8)inside);
                                        positive2 second = memory_count_words(
                                            block + offset + split, length - split,
                                            (p8)(first.y != 0));

                                        if (first.x + second.x != want.x ||
                                            second.y != want.y)
                                                return false;
                                }
                        }
                }

        return true;
}

static fn prepare(positive length, positive shape)
{
        static const p8 prose[] = "one two\tthree\nfour\r\nfive six ";

        for (positive at = 0; at < length; at++)
                block[at] = shape == 0   ? ' '
                            : shape == 1 ? 'x'
                            : shape == 2 ? ((at & 1) ? ' ' : 'x')
                                         : prose[at % (sizeof(prose) - 1)];
}

static positive rounds_for(positive length)
{
        positive rounds = TARGET_BYTES / length;
        if (rounds < 16) rounds = 16;
        if (rounds > (1u << 20)) rounds = 1u << 20;
        return rounds;
}

BENCH_ONCE(run, (p8 assembly, positive length, positive rounds), rounds, round,
        {
                positive2 answer = assembly
                                       ? memory_count_words(block, length, false)
                                       : former_words(block, length, false);
                sink += answer.x + answer.y;
        })

static fn row(string_address name, positive length, positive shape)
{
        positive ratios[TRIES];
        positive rounds = rounds_for(length);

        prepare(length, shape);

        BENCH_PAIRED_RUNS(ratios, former, assembly, length, rounds);
        string_format(log, "  %s  median asm/C %p.%p%%\n", name,
                      ratios[TRIES / 2] / 100, ratios[TRIES / 2] % 100);
}

b32 main(void)
{
        if (!correctness())
        {
                string_format(log, "memory_count_words check failed\n");
                log_flush();
                return 1;
        }

        string_format(log, "memory_count_words, paired median of %p\n",
                      (positive)TRIES);
        row((string_address)"spaces 64", 64, 0);
        row((string_address)"word 64", 64, 1);
        row((string_address)"alternating 64", 64, 2);
        row((string_address)"prose 64", 64, 3);
        row((string_address)"spaces 64K", MAXIMUM, 0);
        row((string_address)"word 64K", MAXIMUM, 1);
        row((string_address)"alternating 64K", MAXIMUM, 2);
        row((string_address)"prose 64K", MAXIMUM, 3);
        log_flush();
        return 0;
}
#endif /* BENCH_words */

