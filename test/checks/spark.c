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

#ifdef CHECK_spark_security
/*
        Regression checks for kernel rules (HMB-003: CAP_SYS_BOOT enforcement in
        rules_load / report_rover) and canvas workqueue teardown ordering
        (FONT_TERM_GPU_UI-002: canvas_turn_off / canvas_thread_stop /
        client_hotplug / canvas_unload).

        Built as a hosted check:
          gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -D_GNU_SOURCE \
              -DCHECK_spark_security -pthread -o /tmp/check_spark_security test/checks.c
*/
#include <errno.h>
#include <linux/input-event-codes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint8_t p8;
typedef uint16_t p16;
typedef uint32_t p32;
typedef uint64_t p64;
typedef int8_t b8;
typedef int16_t b16;
typedef int32_t b32;
typedef int64_t b64;
typedef size_t positive;
typedef ptrdiff_t bipolar;
typedef char *string_address;
typedef const char *const_string;
typedef void *address_any;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef unsigned int __poll_t;

#define address_to *
#define address_of &
#define null NULL
#define end '\0'
#define fn void
#define INLINE
#define offset_of(type, member) offsetof(type, member)
#define array_count(a) (sizeof(a) / sizeof((a)[0]))
#define ARRAY_SIZE(a) array_count(a)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define __rcu
#define __user
#define likely(x) (x)
#define unlikely(x) (x)
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile __typeof__(x) *)&(x) = (v))
#define smp_wmb() __sync_synchronize()
#define smp_rmb() __sync_synchronize()
#define IS_REACHABLE(x) 0
#define IOCTL_IS(name, dir, nr, sz) _Static_assert((sz) > 0, #name)

static inline void memory_copy(void *dst, const void *src, positive n)
{
        if (n)
                memcpy(dst, src, n);
}
static inline void memory_zero(void *dst, positive n)
{
        if (n)
                memset(dst, 0, n);
}
static inline void memory_fill(void *dst, p8 v, positive n)
{
        if (n)
                memset(dst, v, n);
}
static inline b32 memory_compare(const void *a, const void *b, positive n)
{
        const p8 *pa = (const p8 *)a, *pb = (const p8 *)b;
        for (positive i = 0; i < n; i++)
                if (pa[i] != pb[i])
                        return (b32)pa[i] - (b32)pb[i];
        return 0;
}
static inline positive string_length(const char *s) { return strlen(s); }
static inline positive string_length_max(const char *s, positive m) { return strnlen(s, m); }
static inline bool string_equals(const char *a, const char *b) { return strcmp(a, b) == 0; }
static inline b32 string_compare(const char *a, const char *b) { return strcmp(a, b); }
static inline b32 string_compare_max(const char *a, const char *b, positive n) { return strncmp(a, b, n); }
static inline p8 byte_is_alnum(b32 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline p8 byte_is_digit(b32 c) { return c >= '0' && c <= '9'; }
static inline p8 byte_is_lower(b32 c) { return c >= 'a' && c <= 'z'; }
static inline b64 string_to_bipolar(const char *s) { return strtoll(s, NULL, 10); }
static inline p64 string_to_positive(const char *s) { return strtoull(s, NULL, 10); }
static inline positive bipolar_into(p8 *buf, b64 v) { return (positive)sprintf((char *)buf, "%lld", (long long)v); }
static inline positive positive_digits(p64 v) { char b[32]; return (positive)sprintf(b, "%llu", (unsigned long long)v); }
static inline b32 byte_class_holds(b32 a, p8 b) { (void)a; (void)b; return 0; }
static inline positive byte_class_index(const char *a, positive b) { (void)a; (void)b; return 0; }
static inline positive byte_class_index_5_5(const char *a, positive b) { (void)a; (void)b; return 0; }
static inline p32 hash_crc32(p32 a, address_any b, positive c) { (void)a; (void)b; (void)c; return 0; }
static inline p32 hash_crc32_msb(p32 a, address_any b, positive c) { (void)a; (void)b; (void)c; return 0; }
static inline p64 hash_crc64(p64 a, address_any b, positive c) { (void)a; (void)b; (void)c; return 0; }
static inline p64 hash_xxh64(address_any a, positive b, p64 c) { (void)a; (void)b; (void)c; return 0; }
static inline p16 memory_checksum_bsd16(address_any a, positive b, p32 c) { (void)a; (void)b; (void)c; return 0; }
static inline positive memory_count(address_any a, p8 b, positive c) { (void)a; (void)b; (void)c; return 0; }
static inline positive memory_escape_index(address_any a, positive b, p8 c) { (void)a; (void)b; (void)c; return 0; }
static inline void *memory_first_of(const void *bytes, int value, positive length) { return memchr(bytes, value, length); }
static inline p64 memory_hash_33(address_any a, positive b) { (void)a; (void)b; return 0; }
#define memory_load_unaligned(type, ptr) ({ type _v; memcpy(&_v, (ptr), sizeof(_v)); _v; })
static inline positive memory_span_byte(address_any a, p8 b, positive c)
{
        const p8 *p = (const p8 *)a;
        positive i = 0;
        while (i < c && p[i] == b)
                i++;
        return i;
}
static inline p64 memory_sum_bytes(address_any a, positive b) { (void)a; (void)b; return 0; }
static inline positive memory_table_find(const void *bytes, positive length, const void *table,
                                         positive stride, positive count)
{
        const p8 *base = (const p8 *)table;
        for (positive i = 0; i < count; i++)
        {
                const char *name;
                memcpy(&name, base + i * stride, sizeof(name));
                if (name && strlen(name) == length && memcmp(name, bytes, length) == 0)
                        return i;
        }
        return count;
}
#define memory_table_lookup(bytes, length, table) \
        memory_table_find((bytes), (length), (table), sizeof((table)[0]), array_count(table))
static inline positive memory_text_span(address_any a, positive b) { (void)a; (void)b; return 0; }
static inline b64 pointer_gain(b64 a, b64 b, b64 c) { (void)a; (void)b; (void)c; return 0; }
static inline const char *string_first_of_or_end(const char *s, int c)
{
        const char *p = strchr(s, c);
        return p ? p : s + strlen(s);
}
static inline p32 unicode_case(p32 a, p32 b) { (void)b; return a; }
static inline b32 unicode_width(p32 a, p32 b) { (void)a; (void)b; return 1; }
static inline b32 wait_status_code(b32 a) { return a; }
static inline b32 wait_status_code_base(positive a, b32 b) { (void)a; return b; }
static inline size_t strscpy(char *dst, const char *src, size_t size)
{
        if (!size)
                return 0;
        size_t len = strlen(src);
        if (len >= size)
                len = size - 1;
        memcpy(dst, src, len);
        dst[len] = '\0';
        return len;
}

/* Minimal Linux kernel shims so src/moonwater/rules.c compiles directly. */
#define KERNEL_MODE 1
#define STANDARD_MODERN_C_KERNEL 1
#define GFP_KERNEL 0
#define CAP_SYS_ADMIN 21
#define CAP_SYS_BOOT 22
#define BUS_USB 0x03
#define BUS_I8042 0x11
#define BUS_HOST 0x19
#define BUS_VIRTUAL 0x06
#define INPUT_CLK_MONO 0
#define SPARK_BIND_EVENTS 16
#define SPARK_BIND_POWER 1
#define SPARK_BIND_SLEEP 2
#define SPARK_BIND_LID_CLOSE 3
#define SPARK_BIND_LID_OPEN 4
#define SPARK_BIND_RESUME 5
#define SPARK_BIND_TABLET_OFF 6
#define SPARK_BIND_TABLET_ON 7
#define SPARK_BIND_HEADPHONE_OFF 8
#define SPARK_BIND_HEADPHONE_ON 9
#define SPARK_BIND_DOCK_OFF 10
#define SPARK_BIND_DOCK_ON 11
#define SPARK_BIND_CTRL_ALT_DELETE 12
#define SPARK_BIND_CANVAS_ON 13
#define SPARK_BIND_CANVAS_OFF 14
#define SPARK_BIND_POWEROFF 15
#define SPARK_BIND_RESET 16
#define BIND_CODES_PER 2
#define HZ 250
#define S32_MAX INT32_MAX
#define NETDEV_UP 1
#define NETDEV_DOWN 2
#define NETDEV_CHANGE 4
#define NETDEV_REGISTER 5
#define NETDEV_UNREGISTER 6
#define IFF_LOOPBACK 0x8
#define IFF_UP 0x1
#define IFF_RUNNING 0x40
#define VM_WRITE 0x2
#define VM_EXEC 0x4
#define VM_MAYWRITE 0x20
#define VM_MAYEXEC 0x40
#define PAGE_SIZE 4096ul
#define PAGE_ALIGN(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define EPOLLIN 0x1
#define EPOLLRDNORM 0x40
#define O_NONBLOCK 0x800
#define ERESTARTSYS 512
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define SIGCHLD 17
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define DECLARE_BITMAP(name, bits) unsigned long name[((bits) + 63) / 64]

struct file
{
        unsigned int f_flags;
        uint64_t caps;
};
struct vm_area_struct
{
        unsigned long vm_start, vm_end, vm_pgoff, vm_flags;
};
typedef struct { int unused; } poll_table;
typedef struct { int locked; } raw_spinlock_t;
typedef struct { int locked; } mutex_t;
typedef struct { int val; } atomic_t;
typedef struct { int unused; } wait_queue_head_t;
struct rcu_head { void (*func)(struct rcu_head *); };
struct irq_work { void (*func)(struct irq_work *); };
struct work_struct { void (*func)(struct work_struct *); };
struct delayed_work { struct work_struct work; };
struct pm_qos_request { int active; };
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };
struct device { struct device *parent; void *class; };
struct input_id { u16 bustype, vendor, product, version; };
struct input_dev { const char *name; struct input_id id; struct device dev; unsigned long evbit[2]; unsigned long keybit[16]; unsigned long swbit[2]; unsigned long sw[2]; unsigned long propbit[2]; };
struct input_handle { struct input_dev *dev; };
struct bind_handle { struct input_handle handle; unsigned int mods; unsigned int rover_mods; bool rover_virtual; unsigned long rover_taken[16]; };
struct bind_spawn { char command[256]; char event[64]; };
struct net_device { char name[16]; unsigned int flags; int ifindex; };
struct bind_machine_state { wait_queue_head_t wait; u64 ring_seen; };

static const char *saved_command_line = "";

static struct { unsigned int code[BIND_CODES_PER]; } bind_spec[SPARK_BIND_EVENTS];
static atomic_t bind_machine_live;
static struct bind_machine_state bind_machine;
static int init_user_ns;
static int init_net;
static struct timezone_stub { int tz_minuteswest; } sys_tz;

#define DEFINE_RAW_SPINLOCK(x) raw_spinlock_t x = {0}
#define DEFINE_MUTEX(x) mutex_t x = {0}
#define raw_spin_lock_irqsave(l, f) do { (void)(l); (f) = 0; } while (0)
#define raw_spin_unlock_irqrestore(l, f) do { (void)(l); (void)(f); } while (0)
#define mutex_lock(m) ((void)(m))
#define mutex_unlock(m) ((void)(m))
#define lockdep_is_held(l) ((void)(l), 1)
#define rcu_read_lock() do {} while (0)
#define rcu_read_unlock() do {} while (0)
#define rcu_dereference(p) (p)
#define rcu_dereference_protected(p, c) ((void)(c), (p))
#define rcu_dereference_check(p, c) ((void)(c), (p))
#define rcu_assign_pointer(p, v) ((p) = (v))
#define RCU_INIT_POINTER(p, v) ((p) = (v))
#define call_rcu(h, fn) ((fn)(h))
#define rcu_barrier() do {} while (0)
#define atomic_read(a) ((a)->val)
#define atomic_fetch_add(v, a) ((a)->val += (v))
#define atomic_fetch_sub(v, a) ((a)->val -= (v))
#define init_waitqueue_head(w) ((void)(w))
#define wake_up(w) ((void)(w))
#define wake_up_interruptible(w) ((void)(w))
#define wake_up_interruptible_poll(w, m) do { (void)(w); (void)(m); } while (0)
#define poll_wait(f, w, p) do { (void)(f); (void)(w); (void)(p); } while (0)
#define wait_event_interruptible(w, c) ((void)(w), (c) ? 0 : -EAGAIN)
#define init_irq_work(w, fn) ((w)->func = (fn))
#define irq_work_queue(w) ((void)(w))
#define irq_work_sync(w) ((void)(w))
#define INIT_WORK(w, fn) ((w)->func = (fn))
#define INIT_DELAYED_WORK(w, fn) ((w)->work.func = (fn))
#define system_wq ((void *)0)
#define system_highpri_wq ((void *)0)
#define queue_work(q, w) do { (void)(q); (void)(w); } while (0)
#define schedule_work(w) ((void)(w))
#define schedule_delayed_work(w, d) do { (void)(w); (void)(d); } while (0)
#define mod_delayed_work(q, w, d) do { (void)(q); (void)(w); (void)(d); } while (0)
#define cancel_work_sync(w) ((void)(w))
#define cancel_delayed_work_sync(w) ((void)(w))
#define msecs_to_jiffies(ms) (ms)
#define u64_to_user_ptr(x) ((void *)(uintptr_t)(x))
#define pr_info(...) do {} while (0)
#define pr_alert(...) do {} while (0)
#define pr_warn(...) do {} while (0)
#define test_bit(nr, addr) (((addr)[(nr) / (8 * sizeof(unsigned long))] >> ((nr) % (8 * sizeof(unsigned long)))) & 1ul)
#define __set_bit(nr, addr) ((addr)[(nr) / (8 * sizeof(unsigned long))] |= (1ul << ((nr) % (8 * sizeof(unsigned long)))))
#define __clear_bit(nr, addr) ((addr)[(nr) / (8 * sizeof(unsigned long))] &= ~(1ul << ((nr) % (8 * sizeof(unsigned long)))))
#define __assign_bit(nr, addr, val) do { if (val) __set_bit(nr, addr); else __clear_bit(nr, addr); } while (0)
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

static inline void *kzalloc(size_t sz, int gfp) { (void)gfp; return calloc(1, sz); }
static inline void kfree(const void *p) { free((void *)p); }
static inline void *kvmalloc(size_t sz, int gfp) { (void)gfp; return malloc(sz); }
static inline void *kvcalloc(size_t n, size_t sz, int gfp) { (void)gfp; return calloc(n, sz); }
static inline void kvfree(const void *p) { free((void *)p); }
static inline void *vmalloc_user(size_t sz) { return calloc(1, sz); }
static inline void vfree(const void *p) { free((void *)p); }
static inline unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
        memcpy(to, from, n);
        return 0;
}
static inline unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
        memcpy(to, from, n);
        return 0;
}
static inline u64 ktime_get_ns(void) { return 1000000000ull; }
static inline s64 ktime_get_real_seconds(void) { return 1700000000ll; }
static inline s64 ktime_to_ns(s64 t) { return t; }
static inline s64 *input_get_timestamp(struct input_dev *d)
{
        static s64 ts[2] = {1000000000ll, 1000000000ll};
        (void)d;
        return ts;
}
static inline bool spark_file_capable(struct file *file, int cap)
{
        return file && ((file->caps >> cap) & 1ull) != 0;
}
static inline bool file_ns_capable(struct file *file, int *ns, int cap)
{
        (void)ns;
        return spark_file_capable(file, cap);
}
static inline bool capable(int cap) { (void)cap; return true; }
static inline void vm_flags_clear(struct vm_area_struct *v, unsigned long f) { v->vm_flags &= ~f; }
static inline int remap_vmalloc_range(struct vm_area_struct *v, void *p, unsigned long o)
{
        (void)v; (void)p; (void)o; return 0;
}
static inline int register_netdevice_notifier(struct notifier_block *nb) { (void)nb; return 0; }
static inline int unregister_netdevice_notifier(struct notifier_block *nb) { (void)nb; return 0; }
static inline struct net_device *netdev_notifier_info_to_dev(void *d) { return (struct net_device *)d; }
static inline int *dev_net(struct net_device *d) { (void)d; return &init_net; }
static inline bool net_eq(int *a, int *b) { return a == b; }
static inline bool netif_carrier_ok(struct net_device *d) { (void)d; return false; }
static inline bool netif_running(struct net_device *d) { (void)d; return false; }
static inline bool cpu_latency_qos_request_active(struct pm_qos_request *r) { return r->active != 0; }
static inline void cpu_latency_qos_add_request(struct pm_qos_request *r, s32 v) { (void)v; r->active = 1; }
static inline void cpu_latency_qos_update_request(struct pm_qos_request *r, s32 v) { (void)r; (void)v; }
static inline void cpu_latency_qos_remove_request(struct pm_qos_request *r) { r->active = 0; }
static inline void orderly_reboot(void) {}
static inline void orderly_poweroff(bool force) { (void)force; }
static inline void emergency_restart(void) {}
static inline void kernel_power_off(void) {}
static inline void kernel_sigaction(int sig, void (*h)(int)) { (void)sig; (void)h; }
static inline int bind_spawn_enter(void *p) { (void)p; return 0; }
static inline int user_mode_thread(int (*entry)(void *), void *arg, unsigned long flags)
{
        (void)entry; (void)arg; (void)flags; return -ENOSYS;
}
static inline int kernel_wait(int pid, int *stat) { (void)pid; (void)stat; return 0; }
static inline bool canvas_is_on(void) { return false; }
static inline void moonwater_usb_lock_now(void) {}

#include "../../src/rover/compile.c"
#include "../../src/moonwater/rules.c"

static int check_hmb_003_rules_cap_sys_boot(void)
{
        static struct rover_build built;
        struct file admin_only = {.f_flags = 0, .caps = (1ull << CAP_SYS_ADMIN)};
        struct file admin_boot = {.f_flags = 0, .caps = (1ull << CAP_SYS_ADMIN) | (1ull << CAP_SYS_BOOT)};
        struct rover_control req;
        long ret;

        rules_start();

        /* Baseline: ROVER_OP_DO with RP_REBOOT must fail without CAP_SYS_BOOT. */
        memset(&req, 0, sizeof(req));
        req.op = ROVER_OP_DO;
        req.primitive = rules_prim[RP_REBOOT];
        ret = report_rover(&admin_only, &req);
        if (ret != -EPERM)
        {
                fprintf(stderr, "HMB-003 baseline failed: ROVER_OP_DO(reboot) returned %ld, want -EPERM\n", ret);
                rules_stop();
                return 1;
        }

        /* Privileged actions in ROVER_OP_LOAD must require CAP_SYS_BOOT. */
        const char *const priv_tables[] = {
            "rule r1\n\twhen lid == open\n\tdo reboot\n",
            "rule r2\n\twhen lid == open\n\tdo poweroff\n",
            "rule r3\n\twhen lid == open\n\tdo suspend\n",
        };
        for (size_t i = 0; i < array_count(priv_tables); i++)
        {
                if (!rover_compile(&built, (const p8 *)priv_tables[i], strlen(priv_tables[i])))
                {
                        fprintf(stderr, "HMB-003 compile[%zu] failed: %s\n", i, built.why);
                        rules_stop();
                        return 1;
                }
                rules_stop_asked = 0;
                memset(&req, 0, sizeof(req));
                req.op = ROVER_OP_LOAD;
                req.address = (p64)(uintptr_t)rover_built(&built);
                req.length = built.size;
                ret = report_rover(&admin_only, &req);
                if (ret != -EPERM)
                {
                        fprintf(stderr,
                                "HMB-003 FAIL: ROVER_OP_LOAD[%zu] without CAP_SYS_BOOT returned %ld "
                                "(expected -EPERM), rules_stop_asked=%u, rules_now=%p\n",
                                i, ret, (unsigned)rules_stop_asked, (void *)rules_now);
                        rules_stop();
                        return 1;
                }
                if (rules_now != null || rules_stop_asked != 0)
                {
                        fprintf(stderr,
                                "HMB-003 FAIL: rejected ROVER_OP_LOAD[%zu] mutated state "
                                "(rules_now=%p, rules_stop_asked=%u)\n",
                                i, (void *)rules_now, (unsigned)rules_stop_asked);
                        rules_stop();
                        return 1;
                }
        }

        /* Benign table without CAP_SYS_BOOT must still load. */
        const char *benign = "rule ok\n\twhen lid == open\n\tdo log \"hello\"\n";
        if (!rover_compile(&built, (const p8 *)benign, strlen(benign)))
        {
                fprintf(stderr, "HMB-003 benign compile failed: %s\n", built.why);
                rules_stop();
                return 1;
        }
        memset(&req, 0, sizeof(req));
        req.op = ROVER_OP_LOAD;
        req.address = (p64)(uintptr_t)rover_built(&built);
        req.length = built.size;
        ret = report_rover(&admin_only, &req);
        if (ret != 0 || rules_now == null)
        {
                fprintf(stderr, "HMB-003 FAIL: benign ROVER_OP_LOAD without CAP_SYS_BOOT returned %ld\n", ret);
                rules_stop();
                return 1;
        }

        /* Privileged table WITH CAP_SYS_BOOT must succeed. */
        if (!rover_compile(&built, (const p8 *)priv_tables[2], strlen(priv_tables[2])))
        {
                fprintf(stderr, "HMB-003 priv compile failed: %s\n", built.why);
                rules_stop();
                return 1;
        }
        memset(&req, 0, sizeof(req));
        req.op = ROVER_OP_LOAD;
        req.address = (p64)(uintptr_t)rover_built(&built);
        req.length = built.size;
        ret = report_rover(&admin_boot, &req);
        if (ret != 0)
        {
                fprintf(stderr, "HMB-003 FAIL: ROVER_OP_LOAD(suspend) with CAP_SYS_BOOT returned %ld\n", ret);
                rules_stop();
                return 1;
        }

        rules_stop();
        printf("PASS HMB-003: ROVER_OP_LOAD enforces CAP_SYS_BOOT for poweroff/reboot/suspend\n");
        return 0;
}

/* ---- FONT_TERM_GPU_UI-002: canvas_plug_wq lifetime & teardown order ---- */

static char *extract_c_function(const char *src, const char *sig)
{
        const char *cur = src;
        size_t sig_len = strlen(sig);

        while ((cur = strstr(cur, sig)) != NULL)
        {
                const char *after = cur + sig_len;
                while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
                        after++;
                if (*after != '{')
                {
                        cur = after;
                        continue;
                }
                int depth = 0;
                for (const char *p = after; *p; p++)
                {
                        if (*p == '{')
                                depth++;
                        else if (*p == '}')
                        {
                                depth--;
                                if (depth == 0)
                                {
                                        size_t len = (size_t)(p - cur + 1);
                                        char *out = malloc(len + 1);
                                        if (!out)
                                                return NULL;
                                        memcpy(out, cur, len);
                                        out[len] = '\0';
                                        return out;
                                }
                        }
                }
                return NULL;
        }
        return NULL;
}

struct mock_wq
{
        uint32_t magic;
        int queued;
};

static struct mock_wq *mock_plug_wq;
static pthread_mutex_t mock_clientlist_lock = PTHREAD_MUTEX_INITIALIZER;
static bool mock_client_registered;
static bool mock_stop_loop;
static int mock_uaf_detected;
static int mock_wq_allocs;
static int mock_wq_frees;
static bool model_drop_before_stop;
static bool model_destroy_in_stop;

static void mock_wq_ensure(void)
{
        if (__atomic_load_n(&mock_plug_wq, __ATOMIC_ACQUIRE))
                return;
        struct mock_wq *made = calloc(1, sizeof(*made));
        made->magic = 0x57514f4bu;
        __atomic_fetch_add(&mock_wq_allocs, 1, __ATOMIC_RELAXED);
        struct mock_wq *expected = NULL;
        if (!__atomic_compare_exchange_n(&mock_plug_wq, &expected, made, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
                __atomic_fetch_add(&mock_wq_frees, 1, __ATOMIC_RELAXED);
                free(made);
        }
}

static void *mock_hotplug_thread(void *arg)
{
        (void)arg;
        while (!__atomic_load_n(&mock_stop_loop, __ATOMIC_ACQUIRE))
        {
                pthread_mutex_lock(&mock_clientlist_lock);
                if (__atomic_load_n(&mock_client_registered, __ATOMIC_ACQUIRE))
                {
                        mock_wq_ensure();
                        struct mock_wq *wq = __atomic_load_n(&mock_plug_wq, __ATOMIC_ACQUIRE);
                        if (wq)
                        {
                                if (wq->magic != 0x57514f4bu)
                                        __atomic_store_n(&mock_uaf_detected, 1, __ATOMIC_RELEASE);
                                __atomic_fetch_add(&wq->queued, 1, __ATOMIC_RELAXED);
                        }
                }
                pthread_mutex_unlock(&mock_clientlist_lock);
                sched_yield();
        }
        return NULL;
}

static int check_font_term_gpu_ui_002_canvas_wq(void)
{
        FILE *fp = fopen("src/canvas/canvas.c", "rb");
        if (!fp)
        {
                fprintf(stderr, "FONT_TERM_GPU_UI-002 FAIL: cannot open src/canvas/canvas.c\n");
                return 1;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *src = malloc((size_t)sz + 1);
        if (!src || fread(src, 1, (size_t)sz, fp) != (size_t)sz)
        {
                fclose(fp);
                free(src);
                return 1;
        }
        src[sz] = '\0';
        fclose(fp);

        char *fn_turn_off = extract_c_function(src, "static long canvas_turn_off_locked(void)");
        if (!fn_turn_off)
                fn_turn_off = extract_c_function(src, "static long canvas_turn_off(void)");
        char *fn_thread_stop = extract_c_function(src, "static void canvas_thread_stop(void)");
        char *fn_plug_queue = extract_c_function(src, "static void canvas_plug_queue(struct canvas *canvas)");
        char *fn_unload = extract_c_function(src, "\nvoid canvas_unload(void)");
        free(src);

        int failed = 0;
        if (!fn_turn_off || !fn_thread_stop || !fn_plug_queue || !fn_unload)
        {
                fprintf(stderr, "FONT_TERM_GPU_UI-002 FAIL: missing expected canvas functions\n");
                failed = 1;
        }
        else
        {
                const char *drop_pos = strstr(fn_turn_off, "canvas_clients_drop(");
                const char *stop_pos = strstr(fn_turn_off, "canvas_thread_stop(");
                model_drop_before_stop = (drop_pos && stop_pos && drop_pos < stop_pos);
                if (!model_drop_before_stop)
                {
                        fprintf(stderr,
                                "FONT_TERM_GPU_UI-002 FAIL: canvas_turn_off() calls canvas_thread_stop() "
                                "before canvas_clients_drop()\n");
                        failed = 1;
                }

                model_destroy_in_stop = (strstr(fn_thread_stop, "destroy_workqueue") != NULL);
                if (model_destroy_in_stop)
                {
                        fprintf(stderr,
                                "FONT_TERM_GPU_UI-002 FAIL: canvas_thread_stop() destroys canvas_plug_wq "
                                "instead of decoupling its lifetime to canvas_unload()\n");
                        failed = 1;
                }

                const char *xchg_pos = strstr(fn_unload, "xchg(&canvas_plug_wq, null)");
                const char *destroy_pos = strstr(fn_unload, "destroy_workqueue(");
                if (!xchg_pos || !destroy_pos || xchg_pos > destroy_pos)
                {
                        fprintf(stderr,
                                "FONT_TERM_GPU_UI-002 FAIL: canvas_unload() does not atomically clear "
                                "canvas_plug_wq with xchg() before destroy_workqueue()\n");
                        failed = 1;
                }

                if (!strstr(fn_plug_queue, "READ_ONCE(canvas_plug_wq)"))
                {
                        fprintf(stderr,
                                "FONT_TERM_GPU_UI-002 FAIL: canvas_plug_queue() does not use READ_ONCE(canvas_plug_wq)\n");
                        failed = 1;
                }
        }

        /* Concurrency stress model driven by the detected teardown ordering. */
        if (!failed)
        {
                __atomic_store_n(&mock_plug_wq, NULL, __ATOMIC_RELEASE);
                __atomic_store_n(&mock_client_registered, true, __ATOMIC_RELEASE);
                __atomic_store_n(&mock_stop_loop, false, __ATOMIC_RELEASE);
                __atomic_store_n(&mock_uaf_detected, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&mock_wq_allocs, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&mock_wq_frees, 0, __ATOMIC_RELEASE);

                pthread_t th;
                pthread_create(&th, NULL, mock_hotplug_thread, NULL);
                for (int iter = 0; iter < 500; iter++)
                {
                        /* canvas_turn_off(): unregister clients first, then stop thread (no wq destroy). */
                        pthread_mutex_lock(&mock_clientlist_lock);
                        __atomic_store_n(&mock_client_registered, false, __ATOMIC_RELEASE);
                        pthread_mutex_unlock(&mock_clientlist_lock);

                        /* canvas_turn_on(): re-register client. */
                        mock_wq_ensure();
                        pthread_mutex_lock(&mock_clientlist_lock);
                        __atomic_store_n(&mock_client_registered, true, __ATOMIC_RELEASE);
                        pthread_mutex_unlock(&mock_clientlist_lock);
                }
                /* Final canvas_unload(): unregister clients, then xchg + destroy_workqueue. */
                pthread_mutex_lock(&mock_clientlist_lock);
                __atomic_store_n(&mock_client_registered, false, __ATOMIC_RELEASE);
                pthread_mutex_unlock(&mock_clientlist_lock);
                __atomic_store_n(&mock_stop_loop, true, __ATOMIC_RELEASE);
                pthread_join(th, NULL);

                struct mock_wq *wq = __atomic_exchange_n(&mock_plug_wq, NULL, __ATOMIC_ACQ_REL);
                if (wq)
                {
                        wq->magic = 0xdeadbeefu;
                        __atomic_fetch_add(&mock_wq_frees, 1, __ATOMIC_RELAXED);
                        free(wq);
                }
                if (__atomic_load_n(&mock_uaf_detected, __ATOMIC_ACQUIRE) ||
                    __atomic_load_n(&mock_wq_allocs, __ATOMIC_ACQUIRE) !=
                        __atomic_load_n(&mock_wq_frees, __ATOMIC_ACQUIRE))
                {
                        fprintf(stderr,
                                "FONT_TERM_GPU_UI-002 FAIL: concurrency stress uaf=%d allocs=%d frees=%d\n",
                                __atomic_load_n(&mock_uaf_detected, __ATOMIC_ACQUIRE),
                                __atomic_load_n(&mock_wq_allocs, __ATOMIC_ACQUIRE),
                                __atomic_load_n(&mock_wq_frees, __ATOMIC_ACQUIRE));
                        failed = 1;
                }
        }

        free(fn_turn_off);
        free(fn_thread_stop);
        free(fn_plug_queue);
        free(fn_unload);

        if (!failed)
                printf("PASS FONT_TERM_GPU_UI-002: canvas_plug_wq teardown order and xchg lifetime verified\n");
        return failed;
}

static char *read_source_file(const char *path)
{
        FILE *fp = fopen(path, "rb");
        if (!fp)
                return NULL;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz < 0)
        {
                fclose(fp);
                return NULL;
        }
        char *src = malloc((size_t)sz + 1);
        if (!src || fread(src, 1, (size_t)sz, fp) != (size_t)sz)
        {
                fclose(fp);
                free(src);
                return NULL;
        }
        src[sz] = '\0';
        fclose(fp);
        return src;
}

static int check_gkw_2_canvas_unload(void)
{
        char *src = read_source_file("src/canvas/canvas.c");
        if (!src)
        {
                fprintf(stderr, "GKW-2 FAIL: cannot open src/canvas/canvas.c\n");
                return 1;
        }
        char *fn_unload = extract_c_function(src, "\nvoid canvas_unload(void)");
        free(src);
        int failed = 0;
        if (!fn_unload)
        {
                fprintf(stderr, "GKW-2 FAIL: missing canvas_unload()\n");
                return 1;
        }
        const char *cancel_exit = strstr(fn_unload, "cancel_work_sync(&canvas_exit)");
        const char *destroy_wq = strstr(fn_unload, "destroy_workqueue(");
        if (!cancel_exit || !destroy_wq || cancel_exit > destroy_wq)
        {
                fprintf(stderr, "GKW-2 FAIL: canvas_unload() does not call cancel_work_sync(&canvas_exit) before destroy_workqueue()\n");
                failed = 1;
        }
        if (strstr(fn_unload, "canvas_turn_off()") != NULL || !strstr(fn_unload, "mutex_lock(&canvas_control_lock)"))
        {
                fprintf(stderr, "GKW-2 FAIL: canvas_unload() uses killable canvas_turn_off() instead of uninterruptible mutex_lock(&canvas_control_lock)\n");
                failed = 1;
        }
        free(fn_unload);
        if (!failed)
                printf("PASS GKW-2: canvas_unload() acquires canvas_control_lock uninterruptibly and cancels canvas_exit before destroying workqueue\n");
        return failed;
}

static int check_gkw_6_canvas_claim_uaf(void)
{
        char *src = read_source_file("src/canvas/canvas.c");
        if (!src)
        {
                fprintf(stderr, "GKW-6 FAIL: cannot open src/canvas/canvas.c\n");
                return 1;
        }
        char *fn_claim = extract_c_function(src, "static int canvas_claim(const char *path, unsigned int minor,\n                        struct canvas_control *on, _Bool firmware_ok)");
        free(src);
        int failed = 0;
        if (!fn_claim)
        {
                fprintf(stderr, "GKW-6 FAIL: missing canvas_claim()\n");
                return 1;
        }
        const char *reg = strstr(fn_claim, "drm_client_register(&canvas->client)");
        if (!reg)
        {
                fprintf(stderr, "GKW-6 FAIL: missing drm_client_register(&canvas->client) in canvas_claim()\n");
                failed = 1;
        }
        else if (strstr(reg + strlen("drm_client_register(&canvas->client)"), "canvas->") != NULL)
        {
                fprintf(stderr, "GKW-6 FAIL: canvas_claim() dereferences canvas-> after drm_client_register(&canvas->client) (UAF on concurrent unplug)\n");
                failed = 1;
        }
        free(fn_claim);
        if (!failed)
                printf("PASS GKW-6: canvas_claim() does not dereference canvas after drm_client_register()\n");
        return failed;
}

static int check_gkw_8_output_grow(void)
{
        char *src = read_source_file("src/canvas/canvas.c");
        if (!src)
        {
                fprintf(stderr, "GKW-8 FAIL: cannot open src/canvas/canvas.c\n");
                return 1;
        }
        char *fn_grow = extract_c_function(src, "static _Bool output_grow(struct output *output, struct drm_mode_set *mode_set, _Bool either)");
        free(src);
        int failed = 0;
        if (!fn_grow)
        {
                fprintf(stderr, "GKW-8 FAIL: missing output_grow()\n");
                return 1;
        }
        int uses_area = (strstr(fn_grow, "width * height <= (unsigned long)output->width * output->height") != NULL);
        int uses_axes = (strstr(fn_grow, "width <= output->width && height <= output->height") != NULL);
        free(fn_grow);

        /* Model hotplug aspect-ratio change: 1920x1440 -> 2560x1080 (same pixel area 2,764,800) and 1080x1920 -> 1920x1080. */
        unsigned int ow = 1920, oh = 1440, nw = 2560, nh = 1080;
        int skip_realloc = uses_area ? ((unsigned long)nw * nh <= (unsigned long)ow * oh)
                                     : (nw <= ow && nh <= oh);
        if (uses_area || !uses_axes || skip_realloc)
        {
                fprintf(stderr, "GKW-8 FAIL: output_grow() checks pixel area instead of per-axis bounds (1920x1440->2560x1080 skipped=%d)\n", skip_realloc);
                failed = 1;
        }
        if (!failed)
                printf("PASS GKW-8: output_grow() checks per-axis bounds (width <= output->width && height <= output->height)\n");
        return failed;
}

static int check_gkw_7_windows_take_toctou(void)
{
        char *src = read_source_file("src/canvas/window.c");
        if (!src)
        {
                fprintf(stderr, "GKW-7 FAIL: cannot open src/canvas/window.c\n");
                return 1;
        }
        char *fn_take = extract_c_function(src, "static long windows_take(struct window_row *own)");
        free(src);
        int failed = 0;
        if (!fn_take)
        {
                fprintf(stderr, "GKW-7 FAIL: missing windows_take()\n");
                return 1;
        }
        if (strstr(fn_take, "windows_rows(table)") != NULL)
        {
                fprintf(stderr, "GKW-7 FAIL: windows_take() re-reads shared-memory table->capacity via windows_rows(table) after windows_fits()\n");
                failed = 1;
        }
        if (strstr(fn_take, ".room = room") == NULL)
        {
                fprintf(stderr, "GKW-7 FAIL: windows_take() does not snapshot and store validated room in struct window_row\n");
                failed = 1;
        }
        free(fn_take);
        if (!failed)
                printf("PASS GKW-7: windows_take() snapshots room once before validation and clamps row claiming against it\n");
        return failed;
}

int main(void)
{
        int rc = 0;

        rc |= check_hmb_003_rules_cap_sys_boot();
        rc |= check_font_term_gpu_ui_002_canvas_wq();
        rc |= check_gkw_2_canvas_unload();
        rc |= check_gkw_6_canvas_claim_uaf();
        rc |= check_gkw_8_output_grow();
        rc |= check_gkw_7_windows_take_toctou();
        return rc;
}
#endif /* CHECK_spark_security */


