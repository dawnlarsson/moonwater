/*
        test/checks/spark.c: the Unicode width and link benchmarks, and spark's entry.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef BENCH_unicode_width
/*
        Display widths, variant by variant: the C src/canvas/term.c and
        src/sh/text.c ran -- character_width's two range searches behind its
        shortcuts, with the ranges glibc widths since, and wc_width's search
        of 1,044 rows -- against
        unicode_width, over streams of code points drawn the way their readers
        meet them. The C is SHARED_unicode_width_reference, static here as it
        was static there and free to inline; the assembly is a call.

        Streams: mixed is Latin, Greek and Cyrillic, box drawing, ideographs,
        Hangul, kana, emoji, combining marks and a few unassigned code points
        in the proportions of a multilingual log; the rest are one family
        each. Every stream is 65,536 code points from a fixed seed.

        With no arguments it prints best-of-seven ticks a code point. `BODY
        VARIANT STREAM [ROUNDS]`, BODY former, assembly or none and VARIANT
        terminal or wcwidth, runs one body over one stream and prints the sum
        of its widths, so perf stat or an emulator's instruction count sees
        one body a process; none builds the stream and exits, for the fixed
        cost. `table` prints unicode_width_tab's lines for lib.c, laid out
        from the references.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure
#define SHARED_unicode_width_reference
#include "../checks.c"
#undef SHARED_unicode_width_reference

#define WIDTH_BENCH_CODES 65536

struct width_bench_range
{
        p32 first, count, weight;
};

struct width_bench_stream
{
        string_address name;
        const struct width_bench_range address_to ranges;
        positive count;
};

static const struct width_bench_range width_bench_mixed[] = {
    {0x00a0, 0x0260, 25}, {0x0370, 0x0190, 15}, {0x2500, 0x0100, 15},
    {0x4e00, 0x5200, 15}, {0xac00, 0x2ba4, 5},  {0x3040, 0x00c0, 5},
    {0x1f300, 0x0350, 10}, {0x0300, 0x0070, 7}, {0x2fe0, 0x0010, 3},
};
static const struct width_bench_range width_bench_latin[] = {
    {0x00a0, 0x0260, 1},
};
static const struct width_bench_range width_bench_box[] = {
    {0x2500, 0x0100, 1},
};
static const struct width_bench_range width_bench_wide[] = {
    {0x4e00, 0x5200, 6}, {0xac00, 0x2ba4, 3}, {0x3040, 0x00c0, 2},
    {0xff01, 0x005f, 1},
};
static const struct width_bench_range width_bench_emoji[] = {
    {0x1f300, 0x0350, 3}, {0x1f900, 0x0100, 1}, {0x2600, 0x0100, 1},
};
static const struct width_bench_range width_bench_marks[] = {
    {0x0300, 0x0070, 3}, {0x0591, 0x0037, 1}, {0x0900, 0x0080, 1},
    {0x200b, 0x0005, 1}, {0xfe00, 0x0010, 1},
};

static const struct width_bench_stream width_bench_streams[] = {
    {"mixed", width_bench_mixed, array_count(width_bench_mixed)},
    {"latin", width_bench_latin, array_count(width_bench_latin)},
    {"box", width_bench_box, array_count(width_bench_box)},
    {"wide", width_bench_wide, array_count(width_bench_wide)},
    {"emoji", width_bench_emoji, array_count(width_bench_emoji)},
    {"marks", width_bench_marks, array_count(width_bench_marks)},
};

static p32 width_bench_codes[WIDTH_BENCH_CODES];
static positive width_bench_rounds = 64;
static volatile positive width_bench_sink;

static fn width_bench_fill(const struct width_bench_stream address_to stream)
{
        p32 random = 0x2545f491u;
        positive total = 0;

        for (positive at = 0; at < stream->count; at++)
                total += stream->ranges[at].weight;

        for (positive at = 0; at < WIDTH_BENCH_CODES; at++)
        {
                XORSHIFT32(random);
                positive pick = random % total;
                positive which = 0;

                while (pick >= stream->ranges[which].weight)
                        pick -= stream->ranges[which++].weight;

                XORSHIFT32(random);
                width_bench_codes[at] = stream->ranges[which].first +
                                        random % stream->ranges[which].count;
        }
}

static fn width_bench_former_terminal(void)
{
        positive sum = 0;

        for (positive round = 0; round < width_bench_rounds; round++)
                for (positive at = 0; at < WIDTH_BENCH_CODES; at++)
                        sum += unicode_width_reference_terminal(width_bench_codes[at]);
        width_bench_sink = sum;
}

static fn width_bench_former_wcwidth(void)
{
        positive sum = 0;

        for (positive round = 0; round < width_bench_rounds; round++)
                for (positive at = 0; at < WIDTH_BENCH_CODES; at++)
                        sum += unicode_width_reference_wcwidth(width_bench_codes[at]);
        width_bench_sink = sum;
}

static fn width_bench_assembly_terminal(void)
{
        positive sum = 0;

        for (positive round = 0; round < width_bench_rounds; round++)
                for (positive at = 0; at < WIDTH_BENCH_CODES; at++)
                        sum += unicode_width(width_bench_codes[at], UNICODE_WIDTH_TERMINAL);
        width_bench_sink = sum;
}

static fn width_bench_assembly_wcwidth(void)
{
        positive sum = 0;

        for (positive round = 0; round < width_bench_rounds; round++)
                for (positive at = 0; at < WIDTH_BENCH_CODES; at++)
                        sum += unicode_width(width_bench_codes[at], UNICODE_WIDTH_WCWIDTH);
        width_bench_sink = sum;
}

static positive width_bench_number(string_address text, positive otherwise)
{
        positive value = 0;

        if (!text)
                return otherwise;
        while (*text >= '0' && *text <= '9')
                value = value * 10 + (positive)(*text++ - '0');
        return value;
}

static fn width_bench_quads(const p8 address_to table, positive from, positive to)
{
        static const p8 digits[] = "0123456789abcdef";
        p8 line[128];

        for (positive quad = from; quad < to; quad += 4)
        {
                positive used = 0;

                memory_copy(line, "    \".quad ", 11);
                used = 11;
                for (positive at = quad; at < to && at < quad + 4; at++)
                {
                        p64 value = 0;

                        for (positive byte = 0; byte < 8; byte++)
                                value |= (p64)table[at * 8 + byte] << (byte * 8);
                        if (at != quad)
                                line[used++] = ',';
                        line[used++] = '0';
                        line[used++] = 'x';
                        for (positive nibble = 16; nibble--;)
                                line[used++] = digits[(value >> (nibble * 4)) & 15];
                }
                memory_copy(line + used, "\\n\"\n", 4);
                used += 4;
                line[used] = end;
                string_format(log, "%s", line);
        }
}

static fn width_bench_mark(positive at, string_address before, string_address after)
{
        string_format(log, "    \".if . - unicode_width_tab - %p\\n.error "
                           "\\\"unicode_width_tab: %s %p%s\\\"\\n.endif\\n\"\n",
                      at, before, at, after);
}

static b32 width_bench_table(void)
{
        static p8 table[16384];
        positive blocks_at = 0;
        positive leaves_at = 0;
        positive size = unicode_width_reference_table(table, sizeof table,
                                                      address_of blocks_at,
                                                      address_of leaves_at);

        if (!size || size % 8)
        {
                string_format(log, "the references no longer fit the layout\n");
                return 1;
        }

        width_bench_quads(table, 0, blocks_at / 8);
        width_bench_mark(blocks_at, "the blocks do not start at", "");
        width_bench_quads(table, blocks_at / 8, leaves_at / 8);
        width_bench_mark(leaves_at, "the leaves do not start at", "");
        width_bench_quads(table, leaves_at / 8, size / 8);
        width_bench_mark(size, "the table is not", " bytes");
        return 0;
}

b32 main(void)
{
        string_address body = program_argument(1);

        if (body && string_equals(body, "table"))
                return width_bench_table();

        if (body)
        {
                string_address variant = program_argument(2);
                string_address name = program_argument(3);
                const struct width_bench_stream address_to stream = null;

                for (positive at = 0; name && at < array_count(width_bench_streams); at++)
                        if (string_equals(name, width_bench_streams[at].name))
                                stream = width_bench_streams + at;

                if (!variant || !stream)
                        return 2;

                width_bench_fill(stream);
                width_bench_rounds = width_bench_number(program_argument(4), 64);

                p8 terminal = string_equals(variant, "terminal");

                if (!terminal && !string_equals(variant, "wcwidth"))
                        return 2;
                if (string_equals(body, "none"))
                        return 0;
                if (string_equals(body, "former"))
                        (terminal ? width_bench_former_terminal
                                  : width_bench_former_wcwidth)();
                else if (string_equals(body, "assembly"))
                        (terminal ? width_bench_assembly_terminal
                                  : width_bench_assembly_wcwidth)();
                else
                        return 2;
                string_format(log, "%p\n", (positive)width_bench_sink);
                return 0;
        }

        p8 agree = true;

        for (positive at = 0; at < array_count(width_bench_streams); at++)
        {
                const struct width_bench_stream address_to stream = width_bench_streams + at;
                positive units = width_bench_rounds * WIDTH_BENCH_CODES;

                width_bench_fill(stream);
                string_format(log, "  %s:\n", stream->name);

                bench_report("terminal, term.c's C  ", width_bench_former_terminal, 7,
                             units, "code point");
                positive former = width_bench_sink;
                bench_report("terminal, unicode_width", width_bench_assembly_terminal, 7,
                             units, "code point");
                agree = agree && former == width_bench_sink;

                bench_report("wcwidth, text.c's C   ", width_bench_former_wcwidth, 7,
                             units, "code point");
                former = width_bench_sink;
                bench_report("wcwidth, unicode_width ", width_bench_assembly_wcwidth, 7,
                             units, "code point");
                agree = agree && former == width_bench_sink;
        }

        if (!agree)
                string_format(log, "  the C and unicode_width disagree\n");
        log_flush();
        return agree ? 0 : 1;
}
#endif /* BENCH_unicode_width */

#ifdef BENCH_link
/*
        Waterlink's transform, step by step, in the steady state of a stream:
        a sender posts a frame, fills and seals a datagram; a receiver opens
        it, judges it, asks the replay window, applies it, and fills the acknowledgement
        the sender then takes back. The receiver's steps run in link_carried's
        order: the body is judged whole before the replay window is asked,
        and applied from what the judge read. Every step is timed where it sits in that
        loop, so its caches and branches are the ones a real stream has, and
        reported as ticks a frame, the timer's own cost taken off. Two shapes:
        bulk (1,139-byte frames, one to a datagram) and a keystroke (one byte,
        urgent). With an argument it runs only the pipeline, that many frames
        of the named shape, for perf stat to count.
*/
#include "../../src/lib.util.c"
#include "../../src/net/net.c"
#include "../../src/waterlink/link.c"
#include "../../src/waterlink/seal.c"
#include "../../src/waterlink/handshake.c"

#define STEPS 9
static struct waterlink_link sender, receiver;
static struct waterlink_replay window;
static crypto_aesgcm_key key;
static p8 datagram[WATERLINK_DATAGRAM];
static p8 acks[WATERLINK_DATAGRAM];
static p8 payload[WATERLINK_FRAME_MAX];
static p64 spent[STEPS];
static p64 counter;
static p64 stream_clock;
static const char *step_name[STEPS] = {"post",  "fill",  "seal",
                                       "open",  "judge", "replay",
                                       "apply", "fill ack", "take ack"};

static fn frame_once(positive length, p16 flags, p8 timed)
{
        p64 t[STEPS + 1];
        positive used;
        positive sealed;
        positive acked;
        bipolar count;
        struct waterlink_part parts[WATERLINK_PARTS];
        p8 alone = false;
        struct waterlink_datagram head = {WATERLINK_KIND_CARRY, 1, 0};

        stream_clock += 20;
        t[0] = timed ? get_cpu_time() : 0;
        waterlink_post(address_of sender, 1, (p8)flags, payload, (p16)length,
                       stream_clock);
        t[1] = timed ? get_cpu_time() : 0;
        used = waterlink_fill(address_of sender, datagram + 16, stream_clock,
                              address_of alone);
        t[2] = timed ? get_cpu_time() : 0;
        head.counter = counter++;
        memory_copy(datagram, address_of head, 16);
        sealed = waterlink_seal(address_of key, datagram, used);
        t[3] = timed ? get_cpu_time() : 0;
        (void)waterlink_open(address_of key, datagram, sealed);
        t[4] = timed ? get_cpu_time() : 0;
        count = waterlink_judge(datagram + 16, sealed - 32, parts);
        t[5] = timed ? get_cpu_time() : 0;
        (void)waterlink_replay_new(address_of window, head.counter);
        t[6] = timed ? get_cpu_time() : 0;
        if (count >= 0)
                waterlink_apply(address_of receiver, datagram + 16, parts,
                                (positive)count, stream_clock, null, null);
        t[7] = timed ? get_cpu_time() : 0;
        acked = waterlink_fill(address_of receiver, acks, stream_clock + 1,
                               address_of alone);
        t[8] = timed ? get_cpu_time() : 0;
        if (acked)
                (void)waterlink_deliver(address_of sender, acks, acked,
                                           stream_clock + 2, null, null);
        t[9] = timed ? get_cpu_time() : 0;

        if (timed)
                for (positive at = 0; at < STEPS; at++)
                        spent[at] += t[at + 1] - t[at];
}

static fn stream_reset(void)
{
        p8 raw[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

        waterlink_link_reset(address_of sender);
        waterlink_link_reset(address_of receiver);
        memory_zero(address_of window, sizeof window);
        crypto_aesgcm_prepare(address_of key, raw);
        counter = 0;
        stream_clock = 1000;
        for (positive at = 0; at < sizeof payload; at++)
                payload[at] = (p8)(at * 7);
}

static fn shape(string_address name, positive length, p16 flags)
{
        p64 overhead = ~0ull;
        p64 best[STEPS];
        p64 total = 0;

        for (positive at = 0; at < 1000; at++)
        {
                p64 a = get_cpu_time();
                p64 b = get_cpu_time();

                if (b - a < overhead)
                        overhead = b - a;
        }

        for (positive at = 0; at < STEPS; at++)
                best[at] = ~0ull;

        for (positive round = 0; round < 7; round++)
        {
                stream_reset();
                for (positive at = 0; at < 2000; at++)
                        frame_once(length, flags, false);
                memory_zero(spent, sizeof spent);
                for (positive at = 0; at < 20000; at++)
                        frame_once(length, flags, true);
                for (positive at = 0; at < STEPS; at++)
                {
                        p64 each = spent[at] / 20000;

                        each = each > overhead ? each - overhead : 0;
                        if (each < best[at])
                                best[at] = each;
                }
        }

        string_format(log, "  %s, ticks a frame:\n", name);
        for (positive at = 0; at < STEPS; at++)
        {
                string_format(log, "    %s  %p\n", (string_address)step_name[at],
                              (positive)best[at]);
                total += best[at];
        }
        string_format(log, "    all  %p\n", (positive)total);
}

/*
        The first thing an initiation meets is its gate, asked once for each
        key the machine holds -- its own, and a group's for each group -- and
        for a stranger's datagram that none of them accepts it is all the
        datagram costs: ticks a datagram for the machine's own key and eight
        groups.
*/
/*
        The HMAC under a prepared key that every one of those checks is, alone,
        for a message of the length an initiation's is (156 bytes): the
        assembly where the machine has it, the C that stands in for it, and the
        digest functions that both replaced.
*/
static fn hmac_shape(void)
{
        static struct waterlink_identity key;
        static p8 data[256];
        p8 secret[32];
        p8 mac[32];
        p64 best[3] = {~0ull, ~0ull, ~0ull};

        for (positive at = 0; at < 32; at++)
                secret[at] = (p8)(at * 31 + 1);
        for (positive at = 0; at < sizeof data; at++)
                data[at] = (p8)(at * 7);
        waterlink_identity_from(address_of key, secret);
        for (positive round = 0; round < 7; round++)
                for (positive which = 0; which < 3; which++)
                {
                        p64 start = get_cpu_time();

                        for (positive each = 0; each < 20000; each++)
                        {
                                if (which == 0)
                                {
                                        if (!hash_hmac_sha256_prepared(
                                                key.gate_ready.inner.state.narrow,
                                                key.gate_ready.outer.state.narrow,
                                                data, 156, mac))
                                                crypto_hmac_sha256_prepared(
                                                    address_of key.gate_ready, data,
                                                    156, mac);
                                }
                                else if (which == 1)
                                        crypto_hmac_sha256_prepared(
                                            address_of key.gate_ready, data, 156, mac);
                                else
                                {
                                        digest_state work = key.gate_ready.inner;
                                        p8 inner[64];

                                        digest_write(address_of work, data, 156);
                                        digest_close(address_of work, inner);
                                        work = key.gate_ready.outer;
                                        digest_write(address_of work, inner, 32);
                                        digest_close(address_of work, mac);
                                }
                        }
                        start = (get_cpu_time() - start) / 20000;
                        if (start < best[which])
                                best[which] = start;
                }
        string_format(log, "  hmac of 156 bytes under a prepared key, ticks: "
                           "assembly or C %p, C %p, digest functions %p\n",
                      (positive)best[0], (positive)best[1], (positive)best[2]);
}

static fn gate_shape(void)
{
        static struct waterlink_identity keys[9];
        static p8 gate_datagram[WATERLINK_DATAGRAM];
        struct waterlink_datagram head = {WATERLINK_KIND_INITIATE, 0, 0};
        p64 best = ~0ull;
        positive refused = 0;

        for (positive at = 0; at < 9; at++)
        {
                p8 secret[32];

                for (positive byte = 0; byte < 32; byte++)
                        secret[byte] = (p8)(at * 31 + byte * 7 + 1);
                waterlink_identity_from(keys + at, secret);
        }
        memory_copy(gate_datagram, address_of head, 16);
        for (positive round = 0; round < 7; round++)
        {
                p64 start = get_cpu_time();
                p64 took;

                for (positive each = 0; each < 20000; each++)
                        for (positive at = 0; at < 9; at++)
                                refused += !waterlink_gate_passes(
                                        keys + at, gate_datagram,
                                        WATERLINK_DATAGRAM);
                took = (get_cpu_time() - start) / 20000;
                if (took < best)
                        best = took;
        }
        string_format(log, "  gate, nine keys and none accepts, ticks a datagram:"
                           " %p (%p refused)\n",
                      (positive)best, refused);
}

b32 main(void)
{
        string_address address_to words = program_argument_list();

        if (program_argument_count() > 2)
        {
                p8 keystroke = string_equals(words[1], "keystroke");
                positive count = 0;

                for (string_address at = words[2]; *at; at++)
                        count = count * 10 + (positive)(*at - '0');
                stream_reset();
                for (positive at = 0; at < count; at++)
                        frame_once(keystroke ? 1 : WATERLINK_FRAME_MAX,
                                   keystroke ? WATERLINK_FRAME_DURABLE |
                                                       WATERLINK_FRAME_URGENT
                                             : WATERLINK_FRAME_DURABLE,
                                   false);
                string_format(log, "%p frames\n", count);
                log_flush();
                return 0;
        }

        hmac_shape();
        gate_shape();
        shape("bulk", WATERLINK_FRAME_MAX, WATERLINK_FRAME_DURABLE);
        shape("keystroke", 1, WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_URGENT);
        log_flush();
        return 0;
}
#endif /* BENCH_link */

#ifdef CHECK_spark_entry
#include "../../src/lib.util.c"

#if X64
#define SPARK_TEST_TEXT_(value) #value
#define SPARK_TEST_TEXT(value) SPARK_TEST_TEXT_(value)
/* Arrive at the real _start with the original Linux stack and a simulated
   loader handoff, one mode a digit from '0' on: the feature word, the magic
   and the entry facts each from a table. Modes 7 and 13 carry no Spark magic
   and prove CPUID was actually run; 10 and 11 are version 3 with and without
   the facts, and 12 is version 2 with garbage where version 3 keeps them.
   14 is version 3 saying SPARK_ENTRY_AUXV: the probe runs on the stack Linux
   built, whose auxiliary vector names the host's vDSO, so the clock must
   resolve through it, as a Spark program's does from the loader's vector;
   its feature word is empty, since the lookup runs library code. */
__asm__(
    ASM_SECTION
    ASM_FUNC(spark_entry_probe)
    "mov 16(%rsp), %rax\n   movzbl (%rax), %eax\n   sub $48, %eax\n"
    "cmp $14, %eax\n   ja 1f\n"
    "lea spark_entry_words(%rip), %rcx\n   mov (%rcx,%rax,8), %r13\n"
    "lea spark_entry_magics(%rip), %rcx\n   mov (%rcx,%rax,8), %r12\n"
    "lea spark_entry_facts_in(%rip), %rcx\n   mov (%rcx,%rax,8), %r15\n"
    "mov $123456789, %r14\n   jmp _start\n"
    "1: xor %r12d, %r12d\n   jmp _start\n"
    ASM_END(spark_entry_probe)
    ASM_RODATA_OBJECT_BEGIN(spark_entry_words, 8)
    ".quad 0, 1, 257, 16777216, 16777217, 16777473, 16843009, 0\n"
    ".quad 0x0101010101010101, 0x0101010100000000\n"
    ".quad 0x0101010101010101, 257, 16777217, 0, 0\n"
    ASM_OBJECT_END(spark_entry_words)
    ASM_RODATA_OBJECT_BEGIN(spark_entry_magics, 8)
    ".quad " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC)
    ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) "\n"
    ".quad " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC)
    ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", 0\n"
    ".quad " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) "\n"
    ".quad " SPARK_TEST_TEXT(SPARK_START_MAGIC_FACTS) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC_FACTS)
    ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC_FACTS) " + 1, "
    SPARK_TEST_TEXT(SPARK_START_MAGIC_FACTS) "\n"
    ASM_OBJECT_END(spark_entry_magics)
    ASM_RODATA_OBJECT_BEGIN(spark_entry_facts_in, 8)
    ".quad 0, 0, 0, 0, 0, 0, 0, 0, 0, 0\n"
    ".quad 1, 0, 0xff, 1, 3\n"
    ASM_OBJECT_END(spark_entry_facts_in)
);

static positive spark_entry_published(void)
{
        return cpu_has_avx2 | ((positive)cpu_has_avx512 << 8) |
               ((positive)cpu_has_avx512_vbmi << 16) |
               ((positive)cpu_has_fma << 24) |
               ((positive)cpu_has_pclmul << 32) |
               ((positive)cpu_has_aes << 40) |
               ((positive)cpu_has_vpclmul << 48) |
               ((positive)cpu_has_vaes << 56);
}

b32 main(void)
{
        static positive words[] = {0, 1, 257, 16777216, 16777217,
                                    16777473, 16843009, 0,
                                    0x0101010101010101ull,
                                    0x0101010100000000ull,
                                    0x0101010101010101ull, 257, 16777217, 0,
                                    0};
        positive mode = program_argument(1)[0] - '0';
        positive got = spark_entry_published();
        p8 facts = program_entry_facts;
        positive clock = program_vdso_clock;
        timespec now = {0, 0};

        //      An auxiliary vector is looked in only where _start was told
        //      there is one: every ELF, and a loader saying SPARK_ENTRY_AUXV.
        if (clock != (mode == 7 || mode == 13 || mode == 14))
                return 1;
        if (mode == 14)
                return got != words[mode] ||
                       facts != (SPARK_ENTRY_UNFILTERED | SPARK_ENTRY_AUXV) ||
                       clock_gettime(CLOCK_MONOTONIC, address_of now) != 0 ||
                       program_vdso_clock <= 2 || now.tv_nsec >= 1000000000;
        if (mode == 7 || mode == 13)
        {
                cpu_has_avx2 = cpu_has_avx512 = cpu_has_avx512_vbmi = cpu_has_fma = 0;
                cpu_has_pclmul = cpu_has_aes = cpu_has_vpclmul = cpu_has_vaes = 0;
                moonwater_cpu_detect();
                positive detected = spark_entry_published();
                return got != detected || facts != 0 ||
                       program_initial_identity() !=
                           (positive)system_call_1(syscall(getpid), 0);
        }
        // Do not execute instructions advertised by a synthetic handoff:
        // scalar-only CPUs can still verify every publication combination.
        return mode >= array_count(words) || got != words[mode] ||
               facts != (mode == 10 ? SPARK_ENTRY_UNFILTERED : 0) ||
               program_initial_identity() != 123456789 ||
               program_argument_count() != 2 || !program_environment_list();
}
#elif RISCV64
#define SPARK_TEST_TEXT_(value) #value
#define SPARK_TEST_TEXT(value) SPARK_TEST_TEXT_(value)
/* The same on riscv64, where a loader hands _start the magic in s2, the
   feature word in s3 -- Zbc in byte 0, Zvkned in byte 1, Zbb in byte 2 --
   the identity in s4 and the facts in s5. Mode 7 carries no magic and
   proves riscv_hwprobe was asked; 8 is version 3 with the facts. */
__asm__(
    ASM_SECTION
    ASM_FUNC(spark_entry_probe)
    "ld t0, 16(sp)\n   lbu t0, 0(t0)\n   addi t0, t0, -48\n   li t1, 9\n   bgeu t0, t1, 1f\n   slli t0, t0, 3\n"
    "lla t1, spark_entry_words\n   add t1, t1, t0\n   ld s3, 0(t1)\n   lla t1, spark_entry_magics\n   add t1, t1, t0\n"
    "ld s2, 0(t1)\n   lla t1, spark_entry_facts_in\n   add t1, t1, t0\n   ld s5, 0(t1)\n   li s4, 123456789\n   j _start\n"
    "1:  li s2, 0\n   j _start\n"
    ASM_END(spark_entry_probe)
    ASM_RODATA_OBJECT_BEGIN(spark_entry_words, 8)
    ".quad 0, 1, 0x100, 0x10000, 0x10101, 0x10001, 0x10100, 0x10101, 0x10000\n"
    ASM_OBJECT_END(spark_entry_words)
    ASM_RODATA_OBJECT_BEGIN(spark_entry_magics, 8)
    ".quad " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", "
    SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) "\n"
    ".quad " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", " SPARK_TEST_TEXT(SPARK_START_MAGIC) ", "
    SPARK_TEST_TEXT(SPARK_START_MAGIC) ", 0, " SPARK_TEST_TEXT(SPARK_START_MAGIC_FACTS) "\n"
    ASM_OBJECT_END(spark_entry_magics)
    ASM_RODATA_OBJECT_BEGIN(spark_entry_facts_in, 8)
    ".quad 0, 0, 0, 0, 0, 0, 0, 0, 1\n"
    ASM_OBJECT_END(spark_entry_facts_in)
);

static positive spark_entry_published(void)
{
        return cpu_has_pclmul | ((positive)cpu_has_aes << 8) | ((positive)cpu_has_zbb << 16);
}

b32 main(void)
{
        static positive words[] = {0, 1, 0x100, 0x10000, 0x10101, 0x10001, 0x10100, 0, 0x10000};
        positive mode = program_argument(1)[0] - '0';
        positive got = spark_entry_published();
        p8 facts = program_entry_facts;

        if (mode == 7)
        {
                cpu_has_pclmul = cpu_has_aes = cpu_has_zbb = 0;
                moonwater_cpu_detect();
                return got != spark_entry_published() || facts != 0 || program_vdso_clock != 1 ||
                       program_initial_identity() != (positive)system_call_1(syscall(getpid), 0);
        }
        return mode >= array_count(words) || got != words[mode] ||
               facts != (mode == 8 ? SPARK_ENTRY_UNFILTERED : 0) ||
               program_initial_identity() != 123456789 ||
               program_argument_count() != 2 || !program_environment_list();
}
#endif
#endif /* CHECK_spark_entry */

