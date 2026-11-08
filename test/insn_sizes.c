/*
        One routine, one size, one alignment pair, per process: the guest
        instruction driver behind test/insn_sizes.

        The process runs the chosen row's body ROUNDS times on fixed buffers
        and does nothing else that matters. The qemu plugin in test/insn.c
        counts what it executes, so the wrapper runs each cell with one round
        and with a thousand and one and takes the difference over a thousand.
        Startup, buffer setup and the one priming copy are the same in both
        runs and cancel exactly.

        Rows are the callable forms of lib.c routines. Each row body sees d
        (destination), s (source) and n (size), and must consume its result
        into sink, because PURE routines may be dropped otherwise. To add a
        routine, add one X(name, body) line to ROW_LIST; the name is what the
        wrapper takes on its command line. Keep names unique across families.

        Usage (by the wrapper, not by hand):
            insn_sizes ROW SIZE ROUNDS DST_OFFSET SRC_OFFSET

        Buffers are 64-byte aligned, so DST_OFFSET and SRC_OFFSET choose the
        alignment the routine sees, 0 to 63. Sizes up to BUF_BYTES - 64 fit.

        Compiled against the tree it sits in (the include below is relative
        to this file), so a candidate body in another worktree is measured by
        running that worktree's copy of the wrapper.
*/
#include "../src/lib.util.c"

#define NOT_INLINED __attribute__((noinline, noclone))
#define BUF_BYTES (1 << 21)

static p8 buf_dst[BUF_BYTES] __attribute__((aligned(64)));
static p8 buf_src[BUF_BYTES] __attribute__((aligned(64)));
static volatile positive sink;

typedef void (*row_body)(p8 address_to d, p8 address_to s, positive n);

#define ROW_FN(name, body)                                  \
        static NOT_INLINED void row_##name(p8 address_to d, p8 address_to s, positive n) \
        {                                                   \
                body;                                       \
        }

/*
        Every row consumes its result. string_length reads a NUL the setup
        places at SRC_OFFSET + n, so its answer is n.
*/
#define ROW_LIST(X)                                                              \
        X(empty, (void)d)                                                        \
        X(memory_copy, memory_copy(d, s, n))                                     \
        X(memory_copy_apart, memory_copy_apart(d, s, n))                         \
        X(memory_fill, memory_fill(d, 0x5a, n))                                  \
        X(memory_compare, sink += (positive)memory_compare(d, s, n))             \
        X(memory_first_of, sink += memory_first_of(d, '{', n) == null)           \
        X(memory_sum_bytes, sink += (positive)memory_sum_bytes(d, n))           \
        X(string_length, sink += string_length(s))

ROW_LIST(ROW_FN)

#define ROW_ENTRY(name, body) { #name, row_##name },

static const struct
{
        const char *name;
        row_body body;
} rows[] = {ROW_LIST(ROW_ENTRY)};

static int same(const char *a, const char *b)
{
        while (*a && *a == *b)
                a++, b++;
        return *a == *b;
}

static positive parse_count(const char *text, int *bad)
{
        positive value = 0;
        if (!*text)
                *bad = 1;
        for (; *text; text++)
        {
                if (*text < '0' || *text > '9')
                {
                        *bad = 1;
                        return 0;
                }
                value = value * 10 + (positive)(*text - '0');
        }
        return value;
}

b32 main(void)
{
        int bad = 0;
        int argc = program_argument_count();
        string_address address_to argv = program_argument_list();

        if (argc != 6)
                return 2;

        positive size = parse_count(argv[2], &bad);
        positive rounds = parse_count(argv[3], &bad);
        positive doff = parse_count(argv[4], &bad);
        positive soff = parse_count(argv[5], &bad);

        if (bad || size + 64 > BUF_BYTES || doff > 63 || soff > 63 || rounds == 0)
                return 2;

        row_body body = null;
        for (positive at = 0; at < sizeof rows / sizeof rows[0]; at++)
                if (same(argv[1], rows[at].name))
                        body = rows[at].body;
        if (!body)
                return 3;

        p8 address_to d = buf_dst + doff;
        p8 address_to s = buf_src + soff;

        /* A pattern that never equals '{', so first_of scans the whole run. */
        for (positive i = 0; i < size + 128; i++)
                buf_src[i] = (p8)('a' + i % 20);
        s[size] = 0;
        memory_copy(d, s, size);
        d[size] = 0;

        for (positive r = 0; r < rounds; r++)
                body(d, s, size);

        return 0;
}
