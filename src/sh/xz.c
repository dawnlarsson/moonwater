/*
        xz -- LZMA2 inside the .xz stream (check none, CRC32 or CRC64).

        Decode runs every LZMA packet in the lzma_decode_span kernel over a
        liblzma-layout dictionary, then LZMA2 chunks and the stream wrapper. Encode follows xz's presets:
        hash-chain match finders and the fast parser at -0 to -3, a binary
        tree and the price-driven parser at -4 to -9, in blocks of three
        dictionaries that each start fresh (see the encoder below).
        Checksums are hash_crc32/hash_crc64. Concatenated streams are
        accepted the way xz -d accepts them. Decode also takes SHA-256
        checks. The branch converters (x86, PowerPC, IA-64, ARM, ARM Thumb,
        ARM64, SPARC, RISC-V) and delta run as filter chains in both
        directions, built from the --x86 and --lzma2 words.
*/


#define XZ_IN 16384
#define XZ_MATCH_MAX 273
#define XZ_MATCH_MIN 2
/* liblzma's property 40: a dictionary of 4 GiB less one. Nothing is mapped
   for it until a stream's data reaches that far. */
#define XZ_DICT_MAX 0xffffffffu
#define XZ_PROB_LIT (0x300u * 16)
#define XZ_STATES 12
#define XZ_POS 16
#define XZ_LEN_LOW 8
#define XZ_LEN_MID 8
#define XZ_LEN_HIGH 256
#define XZ_DIST_SLOTS 64
#define XZ_ALIGN 16
#define XZ_FULL_DIST 128

#define XZ_CHECK_NONE 0
#define XZ_CHECK_CRC32 1
#define XZ_CHECK_CRC64 4
#define XZ_CHECK_SHA256 10

static p8 xz_in_buf[XZ_IN];
static byte_input xz_input = {.buf = xz_in_buf, .room = XZ_IN};

static bipolar xz_out_fd;
static byte_store xz_output;
static string_address xz_why;
static b32 xz_status;

/* Shared range-encoder ABI. */
typedef struct
{
        p32 range;
        p32 cache;
        p64 low;
        positive pending;
        p8 address_to next;
        p8 address_to limit;
        positive full;
} xz_range_state;

typedef struct
{
        p16 is_match[XZ_STATES][XZ_POS];
        p16 is_rep[XZ_STATES];
        p16 is_rep0[XZ_STATES];
        p16 is_rep1[XZ_STATES];
        p16 is_rep2[XZ_STATES];
        p16 is_rep0_long[XZ_STATES][XZ_POS];
        p16 dist_slot[4][XZ_DIST_SLOTS];
        p16 dist_special[XZ_FULL_DIST - 14];
        p16 dist_align[XZ_ALIGN];
        p16 match_choice;
        p16 match_choice2;
        p16 match_low[XZ_POS][XZ_LEN_LOW];
        p16 match_mid[XZ_POS][XZ_LEN_MID];
        p16 match_high[XZ_LEN_HIGH];
        p16 rep_choice;
        p16 rep_choice2;
        p16 rep_low[XZ_POS][XZ_LEN_LOW];
        p16 rep_mid[XZ_POS][XZ_LEN_MID];
        p16 rep_high[XZ_LEN_HIGH];
        p16 lit[XZ_PROB_LIT];

} xz_probability_state;

static bool xz_fail(string_address why)
{
        xz_why = why;
        return false;
}

/* lzma_range_decode state (24 bytes): range, code, next, limit. */
typedef struct
{
        p32 range;
        p32 code;
        p8 address_to next;
        p8 address_to limit;
} xz_range_input;

static positive xz_dict_from_prop(p8 prop)
{
        if (prop > 40)
                return 0;
        if (prop == 40)
                return XZ_DICT_MAX;
        return (2u | (prop & 1)) << (prop / 2 + 11);
}

/*
        Filters. A block's chain lists its filters in the order the encoder
        applied them, LZMA2 last; the decoder runs LZMA2 and then the rest
        backwards. Delta and the branch converters (x86, PowerPC, IA-64, ARM,
        ARM Thumb, SPARC, ARM64, RISC-V) are one xz_filter each, with
        liblzma's arithmetic: a converter treats the bytes it can decide and
        leaves the last few, up to a whole instruction, for the next call, so
        chopping the stream anywhere gives the bytes of one call over the
        whole. At the end of a block what is left passes through as it is.
*/
#define XZ_FILTER_DELTA 0x03
#define XZ_FILTER_X86 0x04
#define XZ_FILTER_PPC 0x05
#define XZ_FILTER_IA64 0x06
#define XZ_FILTER_ARM 0x07
#define XZ_FILTER_ARMT 0x08
#define XZ_FILTER_SPARC 0x09
#define XZ_FILTER_ARM64 0x0a
#define XZ_FILTER_RISCV 0x0b
#define XZ_FILTER_LZMA2 0x21
/* Filters before LZMA2: liblzma takes four in all. */
#define XZ_FILTERS_MAX 3
#define XZ_FILTER_CARRY 24
/* Room before a filtered window for the bytes each stage carries over. */
#define XZ_FILTER_HEAD 128

typedef struct
{
        p8 id;
        p8 carry_n;
        p8 dist;
        p8 delta_at;
        p32 start;
        p32 pos;
        p32 mask;
        p32 prev;
        p8 carry[XZ_FILTER_CARRY];
        p8 history[256];
} xz_filter;

/* The alignment a converter's start offset must have, 0 for an id that is
   not one (delta has no start offset, and is not asked here). */
static positive xz_filter_alignment(p64 id)
{
        return id == XZ_FILTER_X86 ? 1
             : id == XZ_FILTER_ARMT || id == XZ_FILTER_RISCV ? 2
             : id == XZ_FILTER_PPC || id == XZ_FILTER_ARM || id == XZ_FILTER_SPARC ||
               id == XZ_FILTER_ARM64 ? 4
             : id == XZ_FILTER_IA64 ? 16 : 0;
}

static fn xz_filter_reset(xz_filter address_to f)
{
        f->pos = f->start;
        f->mask = 0;
        f->prev = (p32)-5;
        f->carry_n = 0;
        f->delta_at = 0;
        memory_fill(f->history, 0, sizeof(f->history));
}

#define XZ_X86_MSB(b) ((b) == 0 || (b) == 0xff)

static positive xz_filter_x86(xz_filter address_to f, bool encode, p8 address_to buffer,
                              positive size)
{
        static const p32 bit_of_mask[5] = {0, 1, 2, 2, 3};
        p32 now = f->pos;
        p32 prev_mask = f->mask;
        p32 prev_pos = f->prev;
        positive at = 0;

        if (size < 5)
                return 0;
        if (now - prev_pos > 5)
                prev_pos = now - 5;

        positive limit = size - 5;

        while (at <= limit)
        {
                p8 b = buffer[at];

                if (b != 0xe8 && b != 0xe9)
                {
                        at++;
                        continue;
                }

                p32 offset = now + (p32)at - prev_pos;

                prev_pos = now + (p32)at;
                if (offset > 5)
                        prev_mask = 0;
                else
                        for (p32 i = 0; i < offset; i++)
                        {
                                prev_mask &= 0x77;
                                prev_mask <<= 1;
                        }
                b = buffer[at + 4];
                if (XZ_X86_MSB(b) && (prev_mask >> 1) <= 4 && (prev_mask >> 1) != 3)
                {
                        p32 src = (p32)b << 24 | (p32)buffer[at + 3] << 16 |
                                  (p32)buffer[at + 2] << 8 | buffer[at + 1];
                        p32 dest;

                        for (;;)
                        {
                                dest = encode ? src + (now + (p32)at + 5)
                                              : src - (now + (p32)at + 5);
                                if (prev_mask == 0)
                                        break;

                                p32 i = bit_of_mask[prev_mask >> 1];

                                b = (p8)(dest >> (24 - i * 8));
                                if (!XZ_X86_MSB(b))
                                        break;
                                src = dest ^ ((1u << (32 - i * 8)) - 1);
                        }
                        buffer[at + 4] = (p8)(~(((dest >> 24) & 1) - 1));
                        buffer[at + 3] = (p8)(dest >> 16);
                        buffer[at + 2] = (p8)(dest >> 8);
                        buffer[at + 1] = (p8)dest;
                        at += 5;
                        prev_mask = 0;
                }
                else
                {
                        at++;
                        prev_mask |= 1;
                        if (XZ_X86_MSB(b))
                                prev_mask |= 0x10;
                }
        }
        f->mask = prev_mask;
        f->prev = prev_pos;
        return at;
}

static positive xz_filter_arm(xz_filter address_to f, bool encode, p8 address_to buffer,
                              positive size)
{
        positive i;

        size &= ~(positive)3;
        for (i = 0; i < size; i += 4)
                if (buffer[i + 3] == 0xeb)
                {
                        p32 src = ((p32)buffer[i + 2] << 16 | (p32)buffer[i + 1] << 8 |
                                   buffer[i]) << 2;
                        p32 dest = encode ? f->pos + (p32)i + 8 + src
                                          : src - (f->pos + (p32)i + 8);

                        dest >>= 2;
                        buffer[i + 2] = (p8)(dest >> 16);
                        buffer[i + 1] = (p8)(dest >> 8);
                        buffer[i] = (p8)dest;
                }
        return i;
}

static positive xz_filter_armt(xz_filter address_to f, bool encode, p8 address_to buffer,
                               positive size)
{
        positive i;

        if (size < 4)
                return 0;
        size -= 4;
        for (i = 0; i <= size; i += 2)
                if ((buffer[i + 1] & 0xf8) == 0xf0 && (buffer[i + 3] & 0xf8) == 0xf8)
                {
                        p32 src = (((p32)buffer[i + 1] & 7) << 19 | (p32)buffer[i] << 11 |
                                   ((p32)buffer[i + 3] & 7) << 8 | buffer[i + 2]) << 1;
                        p32 dest = encode ? f->pos + (p32)i + 4 + src
                                          : src - (f->pos + (p32)i + 4);

                        dest >>= 1;
                        buffer[i + 1] = (p8)(0xf0 | ((dest >> 19) & 7));
                        buffer[i] = (p8)(dest >> 11);
                        buffer[i + 3] = (p8)(0xf8 | ((dest >> 8) & 7));
                        buffer[i + 2] = (p8)dest;
                        i += 2;
                }
        return i;
}

static positive xz_filter_ppc(xz_filter address_to f, bool encode, p8 address_to buffer,
                              positive size)
{
        positive i;

        size &= ~(positive)3;
        for (i = 0; i < size; i += 4)
                if ((buffer[i] >> 2) == 0x12 && (buffer[i + 3] & 3) == 1)
                {
                        p32 src = ((p32)buffer[i] & 3) << 24 | (p32)buffer[i + 1] << 16 |
                                  (p32)buffer[i + 2] << 8 | ((p32)buffer[i + 3] & ~(p32)3);
                        p32 dest = encode ? f->pos + (p32)i + src
                                          : src - (f->pos + (p32)i);

                        buffer[i] = (p8)(0x48 | ((dest >> 24) & 3));
                        buffer[i + 1] = (p8)(dest >> 16);
                        buffer[i + 2] = (p8)(dest >> 8);
                        buffer[i + 3] &= 3;
                        buffer[i + 3] |= (p8)dest;
                }
        return i;
}

static positive xz_filter_sparc(xz_filter address_to f, bool encode, p8 address_to buffer,
                                positive size)
{
        positive i;

        size &= ~(positive)3;
        for (i = 0; i < size; i += 4)
                if ((buffer[i] == 0x40 && (buffer[i + 1] & 0xc0) == 0) ||
                    (buffer[i] == 0x7f && (buffer[i + 1] & 0xc0) == 0xc0))
                {
                        p32 src = ((p32)buffer[i] << 24 | (p32)buffer[i + 1] << 16 |
                                   (p32)buffer[i + 2] << 8 | buffer[i + 3]) << 2;
                        p32 dest = encode ? f->pos + (p32)i + src
                                          : src - (f->pos + (p32)i);

                        dest >>= 2;
                        dest = (((0 - ((dest >> 22) & 1)) << 22) & 0x3fffffff) |
                               (dest & 0x3fffff) | 0x40000000;
                        buffer[i] = (p8)(dest >> 24);
                        buffer[i + 1] = (p8)(dest >> 16);
                        buffer[i + 2] = (p8)(dest >> 8);
                        buffer[i + 3] = (p8)dest;
                }
        return i;
}

static positive xz_filter_ia64(xz_filter address_to f, bool encode, p8 address_to buffer,
                               positive size)
{
        static const p32 branch[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       4, 4, 6, 6, 0, 0, 7, 7, 4, 4, 0, 0, 4, 4, 0, 0};
        positive i;

        size &= ~(positive)15;
        for (i = 0; i < size; i += 16)
        {
                p32 mask = branch[buffer[i] & 0x1f];
                p32 bit_pos = 5;

                for (positive slot = 0; slot < 3; slot++, bit_pos += 41)
                {
                        if (((mask >> slot) & 1) == 0)
                                continue;

                        positive byte_pos = bit_pos >> 3;
                        p32 bit_res = bit_pos & 7;
                        p64 instruction = 0;

                        for (positive j = 0; j < 6; j++)
                                instruction += (p64)buffer[i + j + byte_pos] << (8 * j);

                        p64 norm = instruction >> bit_res;

                        if (((norm >> 37) & 0xf) == 0x5 && ((norm >> 9) & 7) == 0)
                        {
                                p32 src = (p32)((norm >> 13) & 0xfffff);

                                src |= (p32)((norm >> 36) & 1) << 20;
                                src <<= 4;

                                p32 dest = encode ? f->pos + (p32)i + src
                                                  : src - (f->pos + (p32)i);

                                dest >>= 4;
                                norm &= ~((p64)0x8fffff << 13);
                                norm |= (p64)(dest & 0xfffff) << 13;
                                norm |= (p64)(dest & 0x100000) << (36 - 20);
                                instruction &= (1u << bit_res) - 1;
                                instruction |= norm << bit_res;
                                for (positive j = 0; j < 6; j++)
                                        buffer[i + j + byte_pos] = (p8)(instruction >> (8 * j));
                        }
                }
        }
        return i;
}

static positive xz_filter_arm64(xz_filter address_to f, bool encode, p8 address_to buffer,
                                positive size)
{
        positive i;

        size &= ~(positive)3;
        for (i = 0; i < size; i += 4)
        {
                p32 pc = f->pos + (p32)i;
                p32 instr = memory_load_unaligned(p32, buffer + i);

                if ((instr >> 26) == 0x25)
                {
                        p32 src = instr;

                        instr = 0x94000000;
                        pc >>= 2;
                        if (!encode)
                                pc = 0u - pc;
                        instr |= (src + pc) & 0x03ffffff;
                        memory_store_unaligned(p32, buffer + i, instr);
                }
                else if ((instr & 0x9f000000) == 0x90000000)
                {
                        p32 src = ((instr >> 29) & 3) | ((instr >> 3) & 0x001ffffc);

                        if ((src + 0x00020000) & 0x001c0000)
                                continue;
                        instr &= 0x9000001f;
                        pc >>= 12;
                        if (!encode)
                                pc = 0u - pc;

                        p32 dest = src + pc;

                        instr |= (dest & 3) << 29;
                        instr |= (dest & 0x0003fffc) << 3;
                        instr |= (0u - (dest & 0x00020000)) & 0x00e00000;
                        memory_store_unaligned(p32, buffer + i, instr);
                }
        }
        return i;
}

#define XZ_RISCV_NOT_PAIR(auipc, inst2) ((((auipc) << 8) ^ ((inst2) - 3)) & 0xf8003)
#define XZ_RISCV_NOT_SPECIAL(auipc, rs1) ((p32)(((auipc) - 0x3117) << 18) >= ((rs1) & 0x1d))

static positive xz_filter_riscv(xz_filter address_to f, bool encode, p8 address_to buffer,
                                positive size)
{
        positive i;

        if (size < 8)
                return 0;
        size -= 8;
        for (i = 0; i <= size; i += 2)
        {
                p32 inst = buffer[i];

                if (inst == 0xef)
                {
                        p32 b1 = buffer[i + 1];

                        if ((b1 & 0x0d) != 0)
                                continue;

                        p32 b2 = buffer[i + 2];
                        p32 b3 = buffer[i + 3];
                        p32 pc = f->pos + (p32)i;

                        if (encode)
                        {
                                p32 addr = ((b1 & 0xf0) << 8) | ((b2 & 0x0f) << 16) |
                                           ((b2 & 0x10) << 7) | ((b2 & 0xe0) >> 4) |
                                           ((b3 & 0x7f) << 4) | ((b3 & 0x80) << 13);

                                addr += pc;
                                buffer[i + 1] = (p8)((b1 & 0x0f) | ((addr >> 13) & 0xf0));
                                buffer[i + 2] = (p8)(addr >> 9);
                                buffer[i + 3] = (p8)(addr >> 1);
                        }
                        else
                        {
                                p32 addr = ((b1 & 0xf0) << 13) | (b2 << 9) | (b3 << 1);

                                addr -= pc;
                                buffer[i + 1] = (p8)((b1 & 0x0f) | ((addr >> 8) & 0xf0));
                                buffer[i + 2] = (p8)(((addr >> 16) & 0x0f) |
                                                     ((addr >> 7) & 0x10) |
                                                     ((addr << 4) & 0xe0));
                                buffer[i + 3] = (p8)(((addr >> 4) & 0x7f) |
                                                     ((addr >> 13) & 0x80));
                        }
                        i += 4 - 2;
                }
                else if ((inst & 0x7f) == 0x17)
                {
                        inst |= (p32)buffer[i + 1] << 8;
                        inst |= (p32)buffer[i + 2] << 16;
                        inst |= (p32)buffer[i + 3] << 24;
                        if (inst & 0xe80)
                        {
                                p32 inst2 = memory_load_unaligned(p32, buffer + i + 4);

                                if (XZ_RISCV_NOT_PAIR(inst, inst2))
                                {
                                        i += 6 - 2;
                                        continue;
                                }
                                if (encode)
                                {
                                        p32 addr = inst & 0xfffff000;

                                        addr += (inst2 >> 20) - ((inst2 >> 19) & 0x1000);
                                        addr += f->pos + (p32)i;
                                        inst = 0x17 | (2 << 7) | (inst2 << 12);
                                        memory_store_unaligned(p32, buffer + i, inst);
                                        buffer[i + 4] = (p8)(addr >> 24);
                                        buffer[i + 5] = (p8)(addr >> 16);
                                        buffer[i + 6] = (p8)(addr >> 8);
                                        buffer[i + 7] = (p8)addr;
                                }
                                else
                                {
                                        p32 addr = inst & 0xfffff000;

                                        addr += inst2 >> 20;
                                        inst = 0x17 | (2 << 7) | (inst2 << 12);
                                        memory_store_unaligned(p32, buffer + i, inst);
                                        memory_store_unaligned(p32, buffer + i + 4, addr);
                                }
                        }
                        else
                        {
                                p32 rs1 = inst >> 27;

                                if (XZ_RISCV_NOT_SPECIAL(inst, rs1))
                                {
                                        i += 4 - 2;
                                        continue;
                                }
                                if (encode)
                                {
                                        p32 fake = memory_load_unaligned(p32, buffer + i + 4);
                                        p32 inst2 = (inst >> 12) | (fake << 20);

                                        inst = 0x17 | (rs1 << 7) | (fake & 0xfffff000);
                                        memory_store_unaligned(p32, buffer + i, inst);
                                        memory_store_unaligned(p32, buffer + i + 4, inst2);
                                }
                                else
                                {
                                        p32 addr = (p32)buffer[i + 4] << 24 |
                                                   (p32)buffer[i + 5] << 16 |
                                                   (p32)buffer[i + 6] << 8 | buffer[i + 7];

                                        addr -= f->pos + (p32)i;

                                        p32 inst2 = (inst >> 12) | (addr << 20);

                                        inst = 0x17 | (rs1 << 7) | ((addr + 0x800) & 0xfffff000);
                                        memory_store_unaligned(p32, buffer + i, inst);
                                        memory_store_unaligned(p32, buffer + i + 4, inst2);
                                }
                        }
                        i += 8 - 2;
                }
        }
        return i;
}

/* Delta: each byte against the one dist back, zero before the start; dist
   holds the property, the distance less one. */
static positive xz_filter_delta(xz_filter address_to f, bool encode, p8 address_to buffer,
                                positive size)
{
        p32 dist = (p32)f->dist + 1;
        p8 at = f->delta_at;

        for (positive i = 0; i < size; i++)
        {
                p8 old = f->history[(dist + at) & 0xff];
                p8 now = buffer[i];

                if (encode)
                        buffer[i] = (p8)(now - old);
                else
                        now = buffer[i] = (p8)(now + old);
                f->history[at-- & 0xff] = encode ? now : buffer[i];
        }
        f->delta_at = at;
        return size;
}

/* How many bytes of buffer[0, size) the filter has decided; the rest wait
   for more (or pass through at the end). */
static positive xz_filter_code(xz_filter address_to f, bool encode, p8 address_to buffer,
                               positive size)
{
        switch (f->id)
        {
        case XZ_FILTER_DELTA: return xz_filter_delta(f, encode, buffer, size);
        case XZ_FILTER_X86: return xz_filter_x86(f, encode, buffer, size);
        case XZ_FILTER_PPC: return xz_filter_ppc(f, encode, buffer, size);
        case XZ_FILTER_IA64: return xz_filter_ia64(f, encode, buffer, size);
        case XZ_FILTER_ARM: return xz_filter_arm(f, encode, buffer, size);
        case XZ_FILTER_ARMT: return xz_filter_armt(f, encode, buffer, size);
        case XZ_FILTER_SPARC: return xz_filter_sparc(f, encode, buffer, size);
        case XZ_FILTER_ARM64: return xz_filter_arm64(f, encode, buffer, size);
        case XZ_FILTER_RISCV: return xz_filter_riscv(f, encode, buffer, size);
        }
        return size;
}

/* One stage over a window that has XZ_FILTER_HEAD bytes of room before it:
   the stage's carried bytes go in front, and what the stage has decided
   comes back (or all of it when final). *at moves to the new front. */
static positive xz_filter_stage(xz_filter address_to f, bool encode, p8 address_to address_to at,
                                positive n, bool final)
{
        p8 address_to start = address_to at - f->carry_n;
        positive total = n + f->carry_n;

        memory_copy_apart(start, f->carry, f->carry_n);

        positive done = xz_filter_code(f, encode, start, total);

        f->pos += (p32)done;
        address_to at = start;
        if (final)
        {
                f->carry_n = 0;
                return total;
        }
        f->carry_n = (p8)(total - done);
        memory_copy_apart(f->carry, start + done, total - done);
        return done;
}

/* A whole chain, stages in the order given: the decoder's is the list
   backwards, the encoder's forwards. */
static positive xz_filter_chain(xz_filter address_to list, positive count, bool encode,
                                p8 address_to address_to at, positive n, bool final)
{
        for (positive k = 0; k < count; k++)
                n = xz_filter_stage(list + (encode ? k : count - 1 - k), encode, at, n, final);
        return n;
}

/* A failed linear block's bytes, filtered as far as liblzma gets: what each
   stage in turn has decided, the rest held back and lost. */
static positive xz_filter_prefix(xz_filter address_to list, positive count,
                                 p8 address_to buffer, positive n)
{
        for (positive k = 0; k < count; k++)
        {
                xz_filter address_to f = list + (count - 1 - k);

                xz_filter_reset(f);
                n = xz_filter_code(f, false, buffer, n);
        }
        return n;
}

/* A whole block in place, its state fresh: no carry, the tail passes as it is. */
static fn xz_filter_block(xz_filter address_to list, positive count, bool encode,
                          p8 address_to buffer, positive n)
{
        for (positive k = 0; k < count; k++)
        {
                xz_filter address_to f = list + (encode ? k : count - 1 - k);

                xz_filter_reset(f);
                xz_filter_code(f, encode, buffer, n);
        }
}

/*
        Decoding. A stream's whole state is one xz_decoder that whoever
        decodes allocates: the span kernel's job, the models, the input
        window, the dictionary, and the LZMA2, block and index framing between
        spans. Nothing here is static and nothing logs, so decoders on
        different threads share nothing; an error comes back as the
        decoder's why, a static string.

        A stream decodes into a ring with liblzma's layout. The buffer holds
        the rounded dictionary plus two 288-byte margins; decoding starts at
        576, and when the cursor passes the end, the last 288 bytes and any
        overshoot move to the front. A match source is then one contiguous
        run (a source below the buffer maps into the tail), buffer offsets
        keep the position modulo 16 for pos_state and the literal position,
        and out[-1] is always the previous byte. Output leaves in spans
        straight from the buffer and is checksummed there.

        A block or a memory image decodes linearly instead: the caller's
        output is the dictionary, a match source is out - rep0 above the
        latest reset, and the first packet after a reset runs on a scratch
        byte that reads as the zero a fresh dictionary holds.

        The kernel runs every packet. Before the input ends the window keeps
        48 bytes of lookahead; at the end, 64 zero bytes follow the data and a
        kernel read into them is a truncated stream.

        A damaged stream yields what liblzma 5.8 yields before it fails, so
        xz -dc of a broken file writes GNU's bytes: every packet before one
        the kernel refuses, a match cut by its chunk's end or the output's
        end copied up to that end, at the end of the input every packet its
        bytes complete, and a block whose check fails whole. The descriptor
        path writes the window out before it reports the failure, the pull
        reader hands it out before it returns -1, and xz_block_decode says
        how much of its output holds decoded bytes.
*/

#define XZ_DEC_IN 65536
#define XZ_IN_PAD 64
#define XZ_PACKET_IN 48
/* The most input one packet consumes: liblzma's LZMA_IN_REQUIRED. */
#define XZ_PACKET_READ 20
#define XZ_MIRROR 288
#define XZ_DICT_START (2 * XZ_MIRROR)
#define XZ_DICT_SLACK (XZ_MATCH_MAX + 64)
#define XZ_COPY_SLACK 32
#define XZ_SPAN (1u << 20)
/* A ring decode's window is at most a span and one match past it. */
#define XZ_FBUF_DATA (XZ_SPAN + 4096)

/* Span ABI, documented above lzma_decode_span in src/lib.c. */
typedef struct
{
        p32 range, code;
        p8 address_to next;
        p8 address_to in_stop;
        xz_probability_state address_to model;
        p8 address_to base;
        p8 address_to out;
        p8 address_to out_stop;
        p8 address_to out_end;
        p8 address_to copy_end;
        p8 address_to lo;
        p8 address_to bottom;
        positive wrap;
        positive dmax;
        p32 state, lc, lp, pb;
        positive rep[4];
        positive error;
} xz_decode_job;

typedef struct
{
        xz_decode_job job;
        xz_probability_state models;
        byte_input input;
        p64 in_abs;
        p8 address_to dict;
        positive dict_cap;
        positive dict_size;
        positive dict_limit;
        bool linear;
        bool first;
        bool wrapped;
        p8 address_to emitted;
        p8 address_to hashed;
        bipolar out_fd;
        bool pull;
        bool paused;
        bool finished;
        bool hdr_done;
        bool block_live;
        bool need_reset;
        bool need_props;
        p8 lz2_kind;
        positive raw_left;
        p64 chunk_left;
        p64 pack_from;
        p64 pack_want;
        p64 block_body_abs;
        p64 block_packed;
        p64 block_out;
        p64 index_blocks;
        p64 index_digest;
        positive block_hdr_size;
        p8 check;
        p8 check_bytes[32];
        p32 crc32;
        p64 crc64;
        digest_state sha256;
        /* The block's filters before LZMA2, in header order; the filtered
           window a ring decode hands out (wo, wn) sits in fbuf. */
        xz_filter filt[XZ_FILTERS_MAX];
        positive nfilt;
        p8 address_to fbuf;
        p8 address_to wo;
        positive wn;
        bool block_filtered;
        p64 hdr_packed;
        p64 hdr_plain;
        p8 hdr_has;
        string_address why;
        p8 scratch[1 + XZ_COPY_SLACK + 8];
        p8 in_buf[XZ_DEC_IN + XZ_IN_PAD];
} xz_decoder;

/* The check field's size, or -1 for a type the decoder refuses. */
static bipolar xz_check_size(p8 type)
{
        return type == XZ_CHECK_NONE ? 0 : type == XZ_CHECK_CRC32 ? 4
             : type == XZ_CHECK_CRC64 ? 8 : type == XZ_CHECK_SHA256 ? 32 : -1;
}

static bool xz_dec_fail(xz_decoder address_to d, string_address why)
{
        if (!d->why)
                d->why = why;
        return false;
}

/* True when at least one input byte is buffered. */
static bool xz_dec_more(xz_decoder address_to d)
{
        bipolar got = byte_input_need(address_of d->input, 1);

        if (got < 0)
                return xz_dec_fail(d, "xz read failed");
        return got != 0;
}

static bipolar xz_dec_byte(xz_decoder address_to d)
{
        if (d->input.at >= d->input.have && !xz_dec_more(d))
                return -1;
        d->in_abs++;
        return d->in_buf[d->input.at++];
}

/* A little-endian field, read a byte at a time across refills. */
static bool xz_dec_le(xz_decoder address_to d, p64 address_to value, p8 bytes)
{
        address_to value = 0;
        for (p8 at = 0; at < bytes; at++)
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return false;
                address_to value |= (p64)byte << (8 * at);
        }
        return true;
}

/* Count and checksum n bytes of the block's output. */
static fn xz_dec_sum(xz_decoder address_to d, p8 address_to at, positive n)
{
        d->block_out += n;
        if (d->check == XZ_CHECK_CRC32)
                d->crc32 = hash_crc32(d->crc32, at, n);
        else if (d->check == XZ_CHECK_CRC64)
                d->crc64 = hash_crc64(d->crc64, at, n);
        else if (d->check == XZ_CHECK_SHA256)
                digest_write(address_of d->sha256, at, n);
}

/* A ring decode with filters: what the dictionary gained goes through the
   chain into fbuf, where it waits as the window to hand out, and is
   checksummed there, for the check covers the block after its filters.
   Final at the block's end, when what a stage carried passes as it is. */
static bool xz_dec_filter(xz_decoder address_to d, bool final)
{
        positive n = (positive)(d->job.out - d->hashed);

        if (!n && !final)
                return true;
        if (n > XZ_FBUF_DATA || d->wn || !d->fbuf)
                return xz_dec_fail(d, "xz filter window");

        p8 address_to at = d->fbuf + XZ_FILTER_HEAD;

        memory_copy_apart(at, d->hashed, n);
        d->hashed = d->emitted = d->job.out;
        n = xz_filter_chain(d->filt, d->nfilt, false, address_of at, n, final);
        d->wo = at;
        d->wn = n;
        xz_dec_sum(d, at, n);
        return true;
}

/* Checksum what the dictionary gained since the last call. A block with
   filters waits: a linear one for its end, a ring one through its window. */
static bool xz_dec_hash(xz_decoder address_to d)
{
        positive n = (positive)(d->job.out - d->hashed);

        if (d->nfilt)
                return d->linear || xz_dec_filter(d, false);
        if (!n)
                return true;
        xz_dec_sum(d, d->hashed, n);
        d->hashed = d->job.out;
        return true;
}

/* The window a reader takes: the dictionary's undelivered bytes, or the
   filtered ones. */
static inline positive xz_win_len(xz_decoder address_to d)
{
        return d->nfilt && !d->linear ? d->wn : (positive)(d->job.out - d->emitted);
}

static inline p8 address_to xz_win_ptr(xz_decoder address_to d)
{
        return d->nfilt && !d->linear ? d->wo : d->emitted;
}

static inline fn xz_win_take(xz_decoder address_to d, positive k)
{
        if (d->nfilt && !d->linear)
        {
                d->wo += k;
                d->wn -= k;
        }
        else
                d->emitted += k;
}

/* Hand the undelivered window on. A descriptor takes it whole, a linear
   decode already holds it where the caller wants it, and a reader takes it
   through xz_pull_span, so until it has the stream pauses. */
static bool xz_dec_drain(xz_decoder address_to d)
{
        if (d->nfilt && !d->linear)
        {
                if (!xz_dec_hash(d))
                        return false;
                d->emitted = d->job.out;
                if (!d->wn)
                        return true;
                if (d->pull)
                {
                        d->paused = true;
                        return true;
                }
                if (d->out_fd >= 0 &&
                    system_write_all((positive)d->out_fd, d->wo, d->wn) != (bipolar)d->wn)
                        return xz_dec_fail(d, "xz write failed");
                d->wn = 0;
                return true;
        }
        xz_dec_hash(d);

        positive n = (positive)(d->job.out - d->emitted);

        if (!n || d->linear)
        {
                d->emitted = d->job.out;
                return true;
        }
        if (d->pull)
        {
                d->paused = true;
                return true;
        }
        if (d->out_fd >= 0 &&
            system_write_all((positive)d->out_fd, d->emitted, n) != (bipolar)n)
                return xz_dec_fail(d, "xz write failed");
        d->emitted = d->job.out;
        return true;
}

/* An LZMA2 dictionary reset; a ring must already be drained. */
static fn xz_dec_dict_reset(xz_decoder address_to d)
{
        d->wrapped = false;
        if (d->linear)
        {
                d->job.base = d->job.out;
                d->first = true;
                return;
        }
        d->job.base = d->dict;
        d->job.out = d->emitted = d->hashed = d->dict + XZ_DICT_START;
        d->dict[XZ_DICT_START - 1] = 0;
}

static bool xz_dec_dict_open(xz_decoder address_to d, positive size)
{
        if (!size || size > XZ_DICT_MAX)
                return xz_dec_fail(d, "xz dictionary");

        positive limit = (size + 15) & ~(positive)15;
        positive cap = limit + XZ_DICT_START + XZ_DICT_SLACK;

        d->dict_limit = limit;
        if (d->linear)
        {
                xz_dec_dict_reset(d);
                return true;
        }
        if (!d->dict || d->dict_cap < cap)
        {
                if (d->dict)
                        memory_free(d->dict, d->dict_cap);
                d->dict = (p8 address_to)memory_checked(cap);
                if (!d->dict)
                {
                        d->dict_cap = 0;
                        return xz_dec_fail(d, "xz cannot map the dictionary");
                }
                d->dict_cap = cap;
        }
        d->dict_size = limit + XZ_DICT_START;
        xz_dec_dict_reset(d);
        return true;
}

static fn xz_dec_dict_close(xz_decoder address_to d)
{
        if (d->dict && !d->linear)
                memory_free(d->dict, d->dict_cap);
        d->dict = null;
        d->dict_cap = 0;
        d->dict_size = 0;
        d->job.base = d->job.out = d->emitted = d->hashed = null;
}

/* The cursor is past the ring's end and the window is drained: the last
   288 bytes and the overshoot move to the front. */
static fn xz_dec_wrap(xz_decoder address_to d)
{
        positive shift = d->dict_size - XZ_MIRROR;
        positive keep = (positive)(d->job.out - d->dict) - shift;

        memory_copy_apart(d->dict, d->dict + shift, keep);
        d->job.out -= shift;
        d->emitted = d->hashed = d->job.out;
        d->wrapped = true;
}

/* Room to decode into: a full span is delivered and a full ring wrapped. A
   reader that still has to take the window pauses the stream; a linear
   decode has no more room past its end. */
static bool xz_dec_room(xz_decoder address_to d)
{
        p8 address_to top = d->dict + d->dict_size;

        if (d->linear)
                return d->job.out < top ||
                       xz_dec_fail(d, "xz output is too small");
        if (d->job.out < top && (positive)(d->job.out - d->emitted) < XZ_SPAN)
                return true;
        if (!xz_dec_drain(d))
                return false;
        if (d->paused)
                return true;
        if (d->job.out >= top)
                xz_dec_wrap(d);
        return true;
}

static fn xz_dec_models_reset(xz_decoder address_to d)
{
        p16 address_to cell = (p16 address_to)address_of d->models;

        for (positive i = 0; i < sizeof(d->models) / sizeof(p16); i++)
                cell[i] = 1024;
        d->job.state = 0;
        d->job.rep[0] = d->job.rep[1] = d->job.rep[2] = d->job.rep[3] = 1;
}

static bool xz_dec_props(xz_decoder address_to d, p8 packed)
{
        if (packed > (4 * 5 + 4) * 9 + 8)
                return xz_dec_fail(d, "xz LZMA properties");

        p32 lc = packed % 9;
        p32 lp = (packed / 9) % 5;
        p32 pb = packed / 45;

        if (lc + lp > 4)
                return xz_dec_fail(d, "xz lc+lp");
        d->job.lc = lc;
        d->job.lp = lp;
        d->job.pb = pb;
        return true;
}

static bool xz_dec_rc_init(xz_decoder address_to d)
{
        bipolar first = xz_dec_byte(d);

        if (first < 0)
                return xz_dec_fail(d, "xz truncated LZMA");
        if (first)
                return xz_dec_fail(d, "xz LZMA range init");
        d->job.range = 0xffffffffu;
        d->job.code = 0;
        for (positive at = 0; at < 4; at++)
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return xz_dec_fail(d, "xz truncated LZMA");
                d->job.code = (d->job.code << 8) | (p8)byte;
        }
        return true;
}

static bool xz_dec_raw(xz_decoder address_to d)
{
        while (d->raw_left)
        {
                if (!xz_dec_room(d))
                        return false;
                if (d->paused)
                        return true;
                if (d->input.at >= d->input.have && !xz_dec_more(d))
                        return xz_dec_fail(d, "xz truncated uncompressed");

                p8 address_to top = d->dict + d->dict_size;
                positive take = d->input.have - d->input.at;

                if (take > d->raw_left)
                        take = d->raw_left;
                if (take > (positive)(top - d->job.out))
                        take = (positive)(top - d->job.out);
                if (!d->linear &&
                    take > (positive)(d->emitted + XZ_SPAN - d->job.out))
                        take = (positive)(d->emitted + XZ_SPAN - d->job.out);
                memory_copy_apart(d->job.out, d->in_buf + d->input.at, take);
                d->job.out += take;
                d->input.at += take;
                d->in_abs += take;
                d->raw_left -= take;
                d->first = false;
        }
        d->lz2_kind = 0;
        return true;
}

/* A linear decode's first packet after a reset, on a scratch byte that
   reads as zero: it can only be a literal, and any match fails the source
   check against an empty history. */
static fn xz_dec_first(xz_decoder address_to d)
{
        xz_decode_job address_to job = address_of d->job;
        xz_decode_job one = address_to job;
        p8 address_to at = d->scratch + 1;

        d->scratch[0] = 0;
        one.base = one.bottom = one.lo = one.out = at;
        one.out_stop = one.out_end = one.copy_end = at + 1;
        one.wrap = 0;
        lzma_decode_span(address_of one);
        job->range = one.range;
        job->code = one.code;
        job->next = one.next;
        job->state = one.state;
        job->rep[0] = one.rep[0];
        job->rep[1] = one.rep[1];
        job->rep[2] = one.rep[2];
        job->rep[3] = one.rep[3];
        job->error = one.error;
        if (one.out != at)
        {
                job->out[0] = at[0];
                job->out++;
                d->first = false;
        }
}

/*
        The kernel refused the packet at out. A match whose source is valid
        was refused for running past out_end, and liblzma copies such a match
        up to that end before the stream fails: so is it here.
*/
static fn xz_dec_cut(xz_decoder address_to d)
{
        xz_decode_job address_to job = address_of d->job;
        positive rep0 = job->rep[0];
        positive src = (positive)job->out - rep0;

        if ((bipolar)(src - (positive)job->lo) < 0)
        {
                if (rep0 > job->dmax)
                        return;
                if ((bipolar)(src - (positive)job->bottom) < 0)
                {
                        if (!job->wrap)
                                return;
                        src += job->wrap;
                }
        }

        p8 address_to from = (p8 address_to)src;

        d->chunk_left -= (positive)(job->out_end - job->out);
        while (job->out < job->out_end)
                address_to job->out++ = address_to from++;
}

/*
        One LZMA2 chunk's packets, a kernel span at a time. No packet starts
        past the chunk's last compressed byte. At the end of the input a span
        runs only packets that cannot read past it and the last few run one
        at a time, so a packet that reads into the padding is dropped whole,
        where liblzma stops holding its bits.
*/
static bool xz_dec_lzma(xz_decoder address_to d)
{
        xz_decode_job address_to job = address_of d->job;

        while (d->chunk_left)
        {
                if (!xz_dec_room(d))
                        return false;
                if (d->paused)
                        return true;

                positive have = d->input.have - d->input.at;

                if (have < XZ_IN_PAD && !d->input.eof)
                {
                        if (byte_input_need(address_of d->input, XZ_IN_PAD) < 0)
                                return xz_dec_fail(d, "xz read failed");
                        have = d->input.have - d->input.at;
                }

                p8 address_to at = d->in_buf + d->input.at;
                p8 address_to top = d->dict + d->dict_size;
                positive pack_left = (positive)(d->pack_want - (d->in_abs - d->pack_from));
                bool tail = d->input.eof && have < XZ_PACKET_READ;

                job->next = at;
                if (!d->input.eof)
                        job->in_stop = at + have - (XZ_PACKET_IN - 1);
                else
                {
                        memory_fill(at + have, 0, XZ_IN_PAD);
                        job->in_stop = tail ? at + have + XZ_IN_PAD - (XZ_PACKET_IN - 1)
                                            : at + have - (XZ_PACKET_READ - 1);
                }
                if ((positive)(job->in_stop - at) > pack_left + 1)
                        job->in_stop = at + pack_left + 1;
                job->out_end = job->out + d->chunk_left;
                /* A linear output ends at its room: a chunk that claims more
                   fails on the match that would cross it. */
                if (d->linear && job->out_end > top)
                        job->out_end = top;
                job->out_stop = top < job->out_end ? top : job->out_end;
                job->copy_end = d->dict_cap > XZ_COPY_SLACK
                                        ? d->dict + d->dict_cap - XZ_COPY_SLACK
                                        : d->dict;
                if (job->copy_end > job->out_end)
                        job->copy_end = job->out_end;
                job->dmax = d->dict_limit;
                job->error = 0;
                if (d->linear)
                {
                        job->wrap = 0;
                        job->bottom = job->lo = job->base;
                        if ((positive)(job->out_stop - job->base) > d->dict_limit)
                                job->lo = job->out_stop - d->dict_limit;
                }
                else
                {
                        if (job->out_stop > d->emitted + XZ_SPAN)
                                job->out_stop = d->emitted + XZ_SPAN;
                        job->wrap = d->wrapped ? d->dict_size - XZ_MIRROR : 0;
                        job->bottom = d->wrapped ? d->dict : d->dict + XZ_DICT_START;
                        job->lo = job->bottom;
                        if (d->wrapped &&
                            (positive)(job->out_stop - d->dict) > d->dict_limit)
                                job->lo = job->out_stop - d->dict_limit;
                }
                if (tail)
                        job->out_stop = job->out + 1;

                p8 address_to before = job->out;

                if (d->linear && d->first)
                        xz_dec_first(d);
                else
                        lzma_decode_span(job);

                positive used = (positive)(job->next - at);

                if (used > have)
                {
                        job->out = before;
                        return xz_dec_fail(d, "xz truncated LZMA");
                }
                d->chunk_left -= (positive)(job->out - before);
                d->input.at += used;
                d->in_abs += used;
                if (job->error)
                {
                        xz_dec_cut(d);
                        return xz_dec_fail(d, "xz distance or chunk length");
                }
                if (d->in_abs - d->pack_from > d->pack_want)
                        return xz_dec_fail(d, "xz LZMA2 compressed size");
        }
        d->lz2_kind = 0;
        return true;
}

/* The end of an LZMA chunk as liblzma checks it: the byte a last
   normalization wants is read, then the code must be zero and every
   compressed byte used. */
static bool xz_dec_lzma_finish(xz_decoder address_to d)
{
        if (d->job.range < (1u << 24))
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return xz_dec_fail(d, "xz truncated LZMA2");
                d->job.range <<= 8;
                d->job.code = (d->job.code << 8) | (p8)byte;
        }
        if (d->job.code)
                return xz_dec_fail(d, "xz LZMA2 chunk end");
        if (d->in_abs - d->pack_from != d->pack_want)
                return xz_dec_fail(d, "xz LZMA2 compressed size");
        return true;
}

static bool xz_dec_lzma2(xz_decoder address_to d)
{
        if (d->lz2_kind == 1)
        {
                if (!xz_dec_raw(d))
                        return false;
                if (d->paused)
                        return true;
        }
        else if (d->lz2_kind == 2)
        {
                if (!xz_dec_lzma(d))
                        return false;
                if (d->paused)
                        return true;
                if (!xz_dec_lzma_finish(d))
                        return false;
        }

        for (;;)
        {
                if (d->input.at >= d->input.have && !xz_dec_more(d))
                        return xz_dec_fail(d, "xz truncated LZMA2");

                p8 control = d->in_buf[d->input.at];
                bool reset = control == 1 || control >= 0xe0;

                /* A dictionary reset rewinds a ring's cursor: deliver first. */
                if (reset && d->job.out != d->emitted)
                {
                        if (!xz_dec_drain(d))
                                return false;
                        if (d->paused)
                                return true;
                }
                d->input.at++;
                d->in_abs++;
                if (!control)
                {
                        d->lz2_kind = 0;
                        return true;
                }
                if (control > 2 && control < 0x80)
                        return xz_dec_fail(d, "xz LZMA2 control");
                if (reset)
                {
                        xz_dec_hash(d);
                        xz_dec_dict_reset(d);
                        d->need_reset = false;
                        d->need_props = true;
                }
                else if (d->need_reset)
                        return xz_dec_fail(d, "xz LZMA2 dictionary reset");

                bipolar hi = xz_dec_byte(d);
                bipolar lo = xz_dec_byte(d);

                if (hi < 0 || lo < 0)
                        return xz_dec_fail(d, "xz truncated LZMA2 sizes");
                if (control < 0x80)
                {
                        d->raw_left = ((positive)hi << 8) + (positive)lo + 1;
                        d->lz2_kind = 1;
                        if (!xz_dec_raw(d))
                                return false;
                        if (d->paused)
                                return true;
                        continue;
                }

                p64 want = ((p64)(control & 0x1f) << 16) +
                           ((p64)hi << 8) + (p64)lo + 1;

                hi = xz_dec_byte(d);
                lo = xz_dec_byte(d);
                if (hi < 0 || lo < 0)
                        return xz_dec_fail(d, "xz truncated LZMA2 sizes");
                d->pack_want = ((p64)hi << 8) + (p64)lo + 1;
                if (control >= 0xc0)
                {
                        bipolar prop = xz_dec_byte(d);

                        if (prop < 0)
                                return xz_dec_fail(d, "xz truncated properties");
                        if (!xz_dec_props(d, (p8)prop))
                                return false;
                        d->need_props = false;
                }
                else if (d->need_props)
                        return xz_dec_fail(d, "xz LZMA2 properties");
                if (control >= 0xa0)
                        xz_dec_models_reset(d);
                d->pack_from = d->in_abs;
                if (!xz_dec_rc_init(d))
                        return false;
                d->chunk_left = want;
                d->lz2_kind = 2;
                if (!xz_dec_lzma(d))
                        return false;
                if (d->paused)
                        return true;
                if (!xz_dec_lzma_finish(d))
                        return false;
        }
}

static bool xz_dec_pad4(xz_decoder address_to d, positive n)
{
        while (n & 3)
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return xz_dec_fail(d, "xz truncated padding");
                if (byte)
                        return xz_dec_fail(d, "xz padding");
                n++;
        }
        return true;
}

static bool xz_dec_check(xz_decoder address_to d)
{
        /*      xz_check_size answers -1 for a type it does not know, and
                the cast made that the whole unsigned range: the loop below
                would have written check_bytes, thirty two of them, until
                the input ran out. Both writers of check validate it first,
                so this is refused here rather than depended on there. */
        bipolar width = xz_check_size(d->check);
        positive size;

        if (width < 0)
                return xz_dec_fail(d, "xz check type");
        size = (positive)width;

        memory_fill(d->check_bytes, 0, sizeof(d->check_bytes));
        for (positive at = 0; at < size; at++)
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return xz_dec_fail(d, "xz truncated check");
                d->check_bytes[at] = (p8)byte;
        }
        if (d->check == XZ_CHECK_CRC32)
        {
                if (memory_load_unaligned(p32, d->check_bytes) != ~d->crc32)
                        return xz_dec_fail(d, "xz CRC32 mismatch");
        }
        else if (d->check == XZ_CHECK_CRC64)
        {
                if (memory_load_unaligned(p64, d->check_bytes) != ~d->crc64)
                        return xz_dec_fail(d, "xz CRC64 mismatch");
        }
        else if (d->check == XZ_CHECK_SHA256)
        {
                p8 sum[32];

                digest_close(address_of d->sha256, sum);
                if (memory_compare(sum, d->check_bytes, 32))
                        return xz_dec_fail(d, "xz SHA-256 mismatch");
        }
        return true;
}

/*
        What the index must say, gathered as the blocks end: how many there
        were, and a digest of each one's unpadded and uncompressed sizes in
        order. The serial decoder read the index's records and threw them
        away, so an index that disagreed with its blocks -- or a footer whose
        backward size did not measure the index -- passed here and was
        refused by the parallel reader, which checks both, and whether a file
        decoded came down to the thread count.
*/
static fn xz_dec_index_digest(p64 address_to digest, p64 unpadded,
                              p64 uncompressed)
{
        p64 pair[2] = {unpadded, uncompressed};

        address_to digest = hash_crc64(address_to digest, pair, sizeof(pair));
}

static fn xz_dec_index_note(xz_decoder address_to d, p64 unpadded,
                            p64 uncompressed)
{
        d->index_blocks++;
        xz_dec_index_digest(address_of d->index_digest, unpadded, uncompressed);
}

/* The block's data is all decoded: filter what is left and checksum it. A
   linear block is filtered whole, in place, here; a ring block's last window
   is filtered with what its stages carried. */
static bool xz_dec_block_data(xz_decoder address_to d)
{
        if (!d->nfilt)
                return xz_dec_hash(d);
        if (!d->linear)
                return xz_dec_filter(d, true);

        p8 address_to from = d->hashed;
        positive n = (positive)(d->job.out - from);

        xz_filter_block(d->filt, d->nfilt, false, from, n);
        d->block_filtered = true;
        xz_dec_sum(d, from, n);
        d->hashed = d->job.out;
        return true;
}

static bool xz_dec_block(xz_decoder address_to d)
{
        if (!d->block_live)
        {
                p8 header[1024];

                /* A block brings its own dictionary: deliver the last one. */
                if (!xz_dec_drain(d))
                        return false;
                if (d->paused)
                        return true;

                bipolar hdr0 = xz_dec_byte(d);

                if (hdr0 < 0)
                        return xz_dec_fail(d, "xz truncated block");

                positive header_size = ((positive)hdr0 + 1) * 4;

                header[0] = (p8)hdr0;
                for (positive at = 1; at < header_size; at++)
                {
                        bipolar byte = xz_dec_byte(d);

                        if (byte < 0)
                                return xz_dec_fail(d, "xz truncated block header");
                        header[at] = (p8)byte;
                }

                p8 flags = header[1];
                positive at = 2;
                positive limit = header_size - 4;
                positive count = (positive)(flags & 3) + 1;
                p32 got = 0;

                for (positive i = 0; i < 4; i++)
                        got |= (p32)header[limit + i] << (8 * i);
                if (got != ~hash_crc32(0xffffffffu, header, limit))
                        return xz_dec_fail(d, "xz block header CRC");
                if (flags & 0x3c)
                        return xz_dec_fail(d, "xz reserved block flags");
                d->hdr_packed = d->hdr_plain = 0;
                d->hdr_has = flags & 0xc0;
                if (flags & 0x40)
                {
                        positive k = memory_vli_get(header + at, limit - at, 9, address_of d->hdr_packed);

                        if (!k || !d->hdr_packed)
                                return xz_dec_fail(d, "xz block header");
                        at += k;
                }
                if (flags & 0x80)
                {
                        positive k = memory_vli_get(header + at, limit - at, 9, address_of d->hdr_plain);

                        if (!k)
                                return xz_dec_fail(d, "xz block header");
                        at += k;
                }

                positive dict = 0;

                d->nfilt = 0;
                for (positive i = 0; i < count; i++)
                {
                        p64 id;
                        p64 psize;
                        positive k = memory_vli_get(header + at, limit - at, 9, address_of id);

                        if (!k)
                                return xz_dec_fail(d, "xz filter flags");
                        at += k;
                        k = memory_vli_get(header + at, limit - at, 9, address_of psize);
                        if (!k || psize > limit - at - k)
                                return xz_dec_fail(d, "xz filter flags");
                        at += k;

                        const p8 address_to props = header + at;

                        at += (positive)psize;
                        if (i == count - 1)
                        {
                                if (id != XZ_FILTER_LZMA2)
                                        return xz_dec_fail(d, "xz filter is not LZMA2");
                                if (psize != 1 || props[0] > 40)
                                        return xz_dec_fail(d, "xz LZMA2 properties size");
                                dict = xz_dict_from_prop(props[0]);
                                continue;
                        }

                        xz_filter address_to f = d->filt + i;

                        memory_fill(f, 0, sizeof(*f));
                        f->id = (p8)id;
                        if (id == XZ_FILTER_DELTA)
                        {
                                if (psize != 1)
                                        return xz_dec_fail(d, "xz filter properties");
                                f->dist = props[0];
                        }
                        else if (xz_filter_alignment(id))
                        {
                                if (psize != 0 && psize != 4)
                                        return xz_dec_fail(d, "xz filter properties");
                                if (psize)
                                        f->start = memory_load_unaligned(p32, props);
                                if (f->start & (xz_filter_alignment(id) - 1))
                                        return xz_dec_fail(d, "xz filter properties");
                        }
                        else
                                return xz_dec_fail(d, "xz filter chain");
                        xz_filter_reset(f);
                        d->nfilt++;
                }
                for (; at < limit; at++)
                        if (header[at])
                                return xz_dec_fail(d, "xz block header");
                //      A block that says how much it holds never reaches back
                //      further than that, so a dictionary claimed larger maps
                //      no more than the block.
                if ((flags & 0x80) && d->hdr_plain < dict)
                        dict = d->hdr_plain ? (positive)d->hdr_plain : 1;
                if (!dict || !xz_dec_dict_open(d, dict))
                        return xz_dec_fail(d, "xz dictionary");
                if (d->nfilt && !d->linear && !d->fbuf)
                {
                        d->fbuf = (p8 address_to)memory_checked(XZ_FILTER_HEAD + XZ_FBUF_DATA +
                                                                XZ_FILTER_HEAD);
                        if (!d->fbuf)
                                return xz_dec_fail(d, "xz cannot map the filter window");
                }
                d->wn = 0;
                d->block_filtered = false;

                d->block_hdr_size = header_size;
                d->block_body_abs = d->in_abs;
                d->crc32 = 0xffffffffu;
                d->crc64 = 0xffffffffffffffffull;
                if (d->check == XZ_CHECK_SHA256)
                        digest_open(address_of d->sha256, DIGEST_SHA256, 32);
                d->hashed = d->job.out;
                d->block_out = 0;
                d->need_reset = true;
                d->need_props = true;
                d->lz2_kind = 0;
                d->block_live = true;
        }
        if (!xz_dec_lzma2(d))
                return false;
        if (d->paused)
                return true;
        d->block_packed = d->in_abs - d->block_body_abs;
        if (!xz_dec_pad4(d, d->block_hdr_size + (positive)d->block_packed))
                return false;
        if (!xz_dec_block_data(d))
                return false;
        if (((d->hdr_has & 0x40) && d->hdr_packed != d->block_packed) ||
            ((d->hdr_has & 0x80) && d->hdr_plain != d->block_out))
                return xz_dec_fail(d, "xz block sizes");
        if (!xz_dec_check(d))
                return false;
        xz_dec_index_note(d, d->block_hdr_size + d->block_packed +
                                 (p64)xz_check_size(d->check),
                          d->block_out);
        d->block_live = false;
        return true;
}

static bipolar xz_dec_vli(xz_decoder address_to d, p32 address_to crc,
                          positive address_to hashed)
{
        p8 bytes[9];
        positive n = 0;
        p64 v;

        // The bytes are gathered and read by memory_vli_get, as the parallel
        // reader reads them: nine at most, and no trailing zero byte, so a
        // padded encoding is refused on both paths alike.
        do
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return -1;
                bytes[n++] = (p8)byte;
        } while ((bytes[n - 1] & 0x80) && n < sizeof(bytes));

        address_to crc = hash_crc32(address_to crc, bytes, n);
        address_to hashed += n;
        if (memory_vli_get(bytes, n, 9, address_of v) != n || v > (p64)bipolar_max)
                return xz_dec_fail(d, "xz VLI"), -1;
        return (bipolar)v;
}

static bool xz_dec_index_and_footer(xz_decoder address_to d)
{
        p64 digest = 0;
        p8 zero = 0;
        p8 body[6];
        positive hashed = 1;
        p64 got;
        p64 back;

        if (xz_dec_byte(d) != 0)
                return xz_dec_fail(d, "xz index indicator");

        p32 crc = hash_crc32(0xffffffffu, address_of zero, 1);
        bipolar records = xz_dec_vli(d, address_of crc, address_of hashed);

        if (records < 0)
                return xz_dec_fail(d, "xz truncated index");
        if ((p64)records != d->index_blocks)
                return xz_dec_fail(d, "xz index does not match the blocks");
        while (records--)
        {
                bipolar unpadded = xz_dec_vli(d, address_of crc, address_of hashed);
                bipolar uncompressed = unpadded < 0 ? -1
                    : xz_dec_vli(d, address_of crc, address_of hashed);

                if (uncompressed < 0)
                        return xz_dec_fail(d, "xz truncated index");
                xz_dec_index_digest(address_of digest, (p64)unpadded,
                                    (p64)uncompressed);
        }
        if (digest != d->index_digest)
                return xz_dec_fail(d, "xz index does not match the blocks");
        while (hashed & 3)
        {
                bipolar byte = xz_dec_byte(d);

                if (byte < 0)
                        return xz_dec_fail(d, "xz truncated index padding");
                if (byte)
                        return xz_dec_fail(d, "xz index padding");
                crc = hash_crc32(crc, address_of zero, 1);
                hashed++;
        }
        if (!xz_dec_le(d, address_of got, 4))
                return xz_dec_fail(d, "xz truncated index CRC");
        if (got != (p32)~crc)
                return xz_dec_fail(d, "xz index CRC");
        if (!xz_dec_le(d, address_of got, 4) || !xz_dec_le(d, address_of back, 4))
                return xz_dec_fail(d, "xz truncated footer");

        bipolar fb0 = xz_dec_byte(d);
        bipolar fb1 = xz_dec_byte(d);

        if (fb0 < 0 || fb1 < 0)
                return xz_dec_fail(d, "xz truncated footer");
        if (fb0 || (fb1 & 0xf0) || (fb1 & 0xf) != d->check)
                return xz_dec_fail(d, "xz footer flags");
        if (xz_dec_byte(d) != 'Y' || xz_dec_byte(d) != 'Z')
                return xz_dec_fail(d, "xz footer magic");
        memory_store_unaligned(p32, body, (p32)back);
        body[4] = (p8)fb0;
        body[5] = (p8)fb1;
        if (got != (p32)~hash_crc32(0xffffffffu, body, 6))
                return xz_dec_fail(d, "xz footer CRC");
        // The backward size measures the index, its CRC included, in fours.
        if (back != (hashed + 4) / 4 - 1)
                return xz_dec_fail(d, "xz footer");
        d->index_blocks = 0;
        d->index_digest = 0;
        return true;
}

/* One stream, or as far as a reader lets it run. */
static bool xz_dec_stream(xz_decoder address_to d)
{
        /* Stream Padding: after a stream, NUL bytes in fours may precede the
           next one or the end of the file. */
        if (!d->hdr_done && d->in_abs)
        {
                p64 from = d->in_abs;

                while (xz_dec_more(d) && !d->in_buf[d->input.at])
                {
                        d->input.at++;
                        d->in_abs++;
                }
                if (d->why)
                        return false;
                if ((d->in_abs - from) & 3)
                        return xz_dec_fail(d, "xz stream padding");
                if (d->input.at >= d->input.have)
                        return true;
        }
        if (!d->hdr_done)
        {
                p64 magic;
                p64 got;
                p8 flags[2];

                if (!xz_dec_le(d, address_of magic, 6))
                        return xz_dec_fail(d, "xz truncated header");
                if (magic != 0x005a587a37fdull)
                        return xz_dec_fail(d, "xz bad magic");
                flags[0] = (p8)xz_dec_byte(d);
                flags[1] = (p8)xz_dec_byte(d);
                if (flags[0] || (flags[1] & 0xf0))
                        return xz_dec_fail(d, "xz reserved stream flags");
                d->check = flags[1] & 0xf;
                if (xz_check_size(d->check) < 0)
                        return xz_dec_fail(d, "xz check type");
                if (!xz_dec_le(d, address_of got, 4))
                        return xz_dec_fail(d, "xz truncated header CRC");
                if (got != (p32)~hash_crc32(0xffffffffu, flags, 2))
                        return xz_dec_fail(d, "xz header CRC");
                d->hdr_done = true;
        }
        for (;;)
        {
                if (!d->block_live)
                {
                        if (!xz_dec_more(d))
                                return xz_dec_fail(d, "xz truncated stream");
                        if (!d->in_buf[d->input.at])
                                break;
                }
                if (!xz_dec_block(d))
                        return false;
                if (d->paused)
                        return true;
        }
        if (!xz_dec_index_and_footer(d))
                return false;
        d->hdr_done = false;
        return true;
}

/* A decoder with nothing mapped but itself. */
static xz_decoder address_to xz_dec_new(void)
{
        xz_decoder address_to d = (xz_decoder address_to)memory_checked(sizeof(xz_decoder));

        if (!d)
                return null;
        memory_fill(d, 0, __builtin_offsetof(xz_decoder, in_buf));
        d->job.model = address_of d->models;
        d->out_fd = -1;
        return d;
}

/* Start over on new input, keeping a ring dictionary's mapping. */
static fn xz_dec_open(xz_decoder address_to d)
{
        if (d->linear)
                xz_dec_dict_close(d);
        d->linear = false;
        d->first = false;
        d->job.base = d->job.out = d->emitted = d->hashed = d->dict;
        d->why = null;
        d->out_fd = -1;
        d->pull = false;
        d->paused = false;
        d->nfilt = 0;
        d->wn = 0;
        d->finished = false;
        d->hdr_done = false;
        d->block_live = false;
        d->lz2_kind = 0;
        d->in_abs = 0;
}

static fn xz_dec_free(xz_decoder address_to d)
{
        if (!d)
                return;
        xz_dec_dict_close(d);
        memory_free(d->fbuf, XZ_FILTER_HEAD + XZ_FBUF_DATA + XZ_FILTER_HEAD);
        memory_free(d, sizeof(xz_decoder));
}

/* A failure on the descriptor path: what the window holds goes out first,
   as xz writes what it decoded from a damaged file. */
static bool xz_dec_salvage(xz_decoder address_to d)
{
        if (d->nfilt && !d->linear)
        {
                //      A window already filtered goes out, then what the
                //      dictionary holds through the chain, unfinished stages
                //      keeping their last bytes as liblzma does.
                if (!d->pull && d->out_fd >= 0)
                {
                        if (d->wn && system_write_all((positive)d->out_fd, d->wo, d->wn) == (bipolar)d->wn)
                                d->wn = 0;
                        if (!d->wn && xz_dec_filter(d, false) && d->wn &&
                            system_write_all((positive)d->out_fd, d->wo, d->wn) == (bipolar)d->wn)
                                d->wn = 0;
                }
                return false;
        }

        positive n = (positive)(d->job.out - d->emitted);

        if (n && !d->linear && !d->pull && d->out_fd >= 0 &&
            system_write_all((positive)d->out_fd, d->emitted, n) == (bipolar)n)
                d->emitted = d->job.out;
        return false;
}

/* Every stream on the input, delivered to the descriptor or kept in a
   linear output. */
static bool xz_dec_run(xz_decoder address_to d)
{
        bool any = false;

        while (xz_dec_more(d))
        {
                if (!xz_dec_stream(d))
                        return xz_dec_salvage(d);
                any = true;
        }
        if (d->why)
                return xz_dec_salvage(d);
        if (!any)
                return xz_dec_fail(d, "xz empty input");
        return xz_dec_drain(d);
}

/* Decode into dst's linear room: every block's dictionary is the output
   itself. */
static bipolar xz_inflate_mem(p8 address_to src, positive src_len,
                              p8 address_to dst, positive dst_cap)
{
        xz_decoder address_to d = xz_dec_new();

        if (!d)
                return -1;
        byte_input_open_memory(address_of d->input, src, src_len, d->in_buf,
                               XZ_DEC_IN);
        d->linear = true;
        d->dict = dst;
        d->dict_cap = dst_cap;
        d->dict_size = dst_cap;
        d->job.base = d->job.out = d->emitted = d->hashed = dst;

        bool ok = xz_dec_run(d);
        bipolar used = (bipolar)(d->job.out - dst);

        xz_dec_free(d);
        return ok ? used : -1;
}

/*
        One block, with nothing but its own decoder touched: the block header
        through its check, exactly block_len bytes at block, decoding to
        exactly uncompressed_len bytes at out, which is written only there
        (matches in its last 32 bytes copy exactly). block_len may stop short
        of the block when the input was cut. check_type is the stream's
        (none, CRC32, CRC64 or SHA-256), and check, when not null, receives
        the stored check field zero-extended to 32 bytes. written, when not
        null, receives how many bytes at out hold decoded data, on success
        and on failure alike, by liblzma's rules above. Decoders from
        xz_block_decoder on different threads may each run blocks at the
        same time; xz_pull_error names a failure and xz_pull_close frees
        one.
*/
static bool xz_block_decode(address_any state, p8 address_to block,
                            positive block_len, p64 uncompressed_len,
                            p8 address_to out, p8 check_type,
                            p8 address_to check, positive address_to written)
{
        xz_decoder address_to d = state;

        if (written)
                address_to written = 0;
        xz_dec_open(d);
        if (d->dict)
                xz_dec_dict_close(d);
        if (xz_check_size(check_type) < 0)
                return xz_dec_fail(d, "xz check type");
        byte_input_open_memory(address_of d->input, block, block_len, d->in_buf,
                               XZ_DEC_IN);
        d->linear = true;
        d->dict = out;
        d->dict_cap = (positive)uncompressed_len;
        d->dict_size = (positive)uncompressed_len;
        d->job.base = d->job.out = d->emitted = d->hashed = out;
        d->check = check_type;
        memory_fill(d->check_bytes, 0, sizeof(d->check_bytes));
        d->hdr_done = true;

        bool ok = xz_dec_block(d);

        if (d->nfilt && !d->block_filtered && !ok)
                d->job.out = out + xz_filter_prefix(d->filt, d->nfilt, out,
                                                    (positive)(d->job.out - out));
        if (written)
                address_to written = (positive)(d->job.out - out);
        if (!ok)
                return false;
        if (check)
                memory_copy_apart(check, d->check_bytes, sizeof(d->check_bytes));
        if (d->job.out != out + uncompressed_len || d->in_abs != block_len)
                return xz_dec_fail(d, "xz block sizes");
        return true;
}

static address_any xz_block_decoder(void)
{
        return xz_dec_new();
}

/*
        The pull interface: an opaque decoder on fd whose input starts with
        prefix. A span points into the decoder's window and stays valid until
        the next call on the same state. Reads return decoded bytes, 0 at the
        clean end of the input, -1 on an error that xz_pull_error names.
*/
static address_any xz_pull_open(bipolar fd, p8 address_to prefix,
                                positive prefix_len)
{
        xz_decoder address_to d = xz_dec_new();

        if (!d)
                return null;
        byte_input_open_fd(address_of d->input, fd, d->in_buf, XZ_DEC_IN);
        d->pull = true;
        if (prefix_len > XZ_DEC_IN)
                xz_dec_fail(d, "xz prefix");
        else if (prefix_len)
        {
                memory_copy(d->in_buf, prefix, prefix_len);
                d->input.have = prefix_len;
        }
        return d;
}

/* Bytes waiting in the window, decoding more when there are none. */
static bipolar xz_pull_more(xz_decoder address_to d)
{
        for (;;)
        {
                positive left = xz_win_len(d);

                if (left)
                        return (bipolar)left;
                if (d->why)
                        return -1;
                if (d->finished)
                        return 0;
                if (!d->hdr_done && !d->block_live && !xz_dec_more(d))
                {
                        if (d->why)
                                return -1;
                        d->finished = true;
                        return 0;
                }
                d->paused = false;
                /* A failure hands out what the window holds before -1. */
                if (!xz_dec_stream(d))
                {
                        if (d->nfilt && !d->linear && !d->wn)
                                xz_dec_filter(d, false);
                        if (!xz_win_len(d))
                                return -1;
                }
        }
}

static bipolar xz_pull_span(address_any state, p8 address_to address_to span)
{
        xz_decoder address_to d = state;
        bipolar n = xz_pull_more(d);

        if (n > 0)
        {
                address_to span = xz_win_ptr(d);
                xz_win_take(d, (positive)n);
        }
        return n;
}

static bipolar xz_pull_read(address_any state, p8 address_to into, positive n)
{
        xz_decoder address_to d = state;
        positive copied = 0;

        while (copied < n)
        {
                bipolar left = xz_pull_more(d);

                if (left < 0)
                        return copied ? (bipolar)copied : -1;
                if (!left)
                        break;

                positive take = min((positive)left, n - copied);

                memory_copy_apart(into + copied, xz_win_ptr(d), take);
                xz_win_take(d, take);
                copied += take;
        }
        return (bipolar)copied;
}

static string_address xz_pull_error(address_any state)
{
        return state ? ((xz_decoder address_to)state)->why
                     : (string_address)"xz cannot map the decoder";
}

static bool xz_pull_close(address_any state)
{
        xz_decoder address_to d = state;

        if (!d)
                return false;

        bool ok = !d->why;

        xz_dec_free(d);
        return ok;
}

static p8 xz_prop_from_dict(positive dict)
{
        p8 prop;

        for (prop = 0; prop < 40; prop++)
                if (xz_dict_from_prop(prop) >= dict)
                        return prop;
        return 39;
}

/*
        Encoder.

        One xz_encoder holds everything a block needs and nothing a second
        block shares: the preset, a pointer to the block's input (readable
        for XZ_SLACK bytes past its end), match-finder tables sized from the
        dictionary, LZMA models, price tables, the optimum array, and the
        block's finished bytes. A block is max(3 * dictionary, 1 MiB) of
        input, decided by the preset alone, and starts with a dictionary
        reset, so blocks can be encoded side by side and the bytes never
        depend on how many are. The preset table, the fast and the price
        driven normal parsers, the hc3/hc4/bt4 match finders and the LZMA2
        chunk rules follow liblzma 5.8. Positions and distances inside the
        encoder are zero-based, as in liblzma.
*/

#define XZ_OPTS 4096
#define XZ_LOOP_INPUT (XZ_OPTS + 1)
#define XZ_INFINITY_PRICE (1u << 30)
#define XZ_HASH2_SIZE (1u << 10)
#define XZ_HASH3_SIZE (1u << 16)
#define XZ_CHUNK_PACKED_MAX 65536u
#define XZ_CHUNK_PLAIN_MAX (1u << 21)
#define XZ_BLOCK_HEADER_MAX 96
#define XZ_SLACK 64
#define XZ_LITERAL 0xffffffffu
#define XZ_LEN_SYMBOLS (XZ_LEN_LOW + XZ_LEN_MID + XZ_LEN_HIGH)
#define XZ_CHANGE_PAIR(small_dist, big_dist) (((big_dist) >> 7) > (small_dist))

enum { XZ_FINDER_HC3, XZ_FINDER_HC4, XZ_FINDER_BT4 };

typedef struct
{
        p8 dict_log;
        bool normal;
        p8 finder;
        p16 nice;
        p32 depth;
        /* The rest is what a preset leaves to the command line: the exact
           dictionary size (a preset's power of two, or any value from 4 KiB
           to 1.5 GiB) and the literal and position bits. */
        p32 dict;
        p8 lc;
        p8 lp;
        p8 pb;
} xz_preset;

/* xz 5.8's -0 .. -9, all lc=3 lp=0 pb=2, then -0e .. -9e: the normal parser
   over bt4, nice 192 at -3e and -5e and 273 with depth 512 elsewhere. Depth
   0 means 16 + nice/2 for a binary tree and 4 + nice/4 for a hash chain. A
   level byte carries XZ_EXTREME beside the preset number. */
#define XZ_EXTREME 0x10

static const xz_preset xz_presets[20] = {
        {18, false, XZ_FINDER_HC3, 128, 4},
        {20, false, XZ_FINDER_HC4, 128, 8},
        {21, false, XZ_FINDER_HC4, 273, 24},
        {22, false, XZ_FINDER_HC4, 273, 48},
        {22, true, XZ_FINDER_BT4, 16, 0},
        {23, true, XZ_FINDER_BT4, 32, 0},
        {23, true, XZ_FINDER_BT4, 64, 0},
        {24, true, XZ_FINDER_BT4, 64, 0},
        {25, true, XZ_FINDER_BT4, 64, 0},
        {26, true, XZ_FINDER_BT4, 64, 0},
        {18, true, XZ_FINDER_BT4, 273, 512},
        {20, true, XZ_FINDER_BT4, 273, 512},
        {21, true, XZ_FINDER_BT4, 273, 512},
        {22, true, XZ_FINDER_BT4, 192, 0},
        {22, true, XZ_FINDER_BT4, 273, 512},
        {23, true, XZ_FINDER_BT4, 192, 0},
        {23, true, XZ_FINDER_BT4, 273, 512},
        {24, true, XZ_FINDER_BT4, 273, 512},
        {25, true, XZ_FINDER_BT4, 273, 512},
        {26, true, XZ_FINDER_BT4, 273, 512}};

static xz_preset xz_preset_of(p8 level)
{
        p8 number = level & 15;
        xz_preset p = xz_presets[(number > 9 ? 9 : number) + (level & XZ_EXTREME ? 10 : 0)];

        p.dict = (p32)1 << p.dict_log;
        p.lc = 3;
        p.lp = 0;
        p.pb = 2;
        return p;
}

/* What a stream is encoded with: the LZMA2 options and the filters in front
   of it, whose states are templates each block copies. */
typedef struct
{
        xz_preset lz;
        xz_filter filt[XZ_FILTERS_MAX];
        positive nfilt;
        /* -e without a chain of its own: each block tries the x86 converter. */
        bool pick;
} xz_options;

typedef struct
{
        p32 len;
        p32 dist;
} xz_found;

/* What the match finders read and write, apart from the encoder around
   them: the block's bytes, the position, the hash and tree tables and the
   matches of the last find. The parser and the finder may sit on different
   threads, and then the finder's copy is the only one the finder touches. */
typedef struct
{
        xz_preset preset;
        p8 address_to input;
        p32 input_n;
        p32 read_pos;
        p32 offset;
        p32 address_to hash;
        p32 hash_mask;
        p32 address_to son;
        p32 cyclic_pos;
        p32 cyclic_size;
        p32 nice;
        p32 depth;
        xz_found address_to matches;
} xz_finder;

typedef struct
{
        p8 state;
        bool prev_1_is_literal;
        bool prev_2;
        p32 pos_prev_2;
        p32 back_prev_2;
        p32 price;
        p32 pos_prev;
        p32 back_prev;
        p32 backs[4];
} xz_optimal;

typedef struct
{
        p32 prices[XZ_POS][XZ_LEN_SYMBOLS];
        p32 counters[XZ_POS];
} xz_length_price;

typedef struct
{
        xz_preset preset;
        xz_filter filt[XZ_FILTERS_MAX];
        positive nfilt;
        p8 address_to fbuf;
        positive fbuf_room;
        xz_finder fin;
        bool pick;
        address_any trial;
        address_any pipe;
        address_any pipe_area;
        positive pipe_room;
        bool pipe_ok;

        /* The block: input_n bytes at input, zero-based read position,
           how many of those the parser has looked at but not coded, and the
           match finder's position bias (positions start past the window, so
           zero is always too far to be a candidate). */
        p8 address_to input;
        p32 input_n;
        p32 read_pos;
        p32 read_ahead;
        p32 offset;

        p32 address_to hash;
        positive hash_room;
        p32 address_to son;
        positive son_room;
        p32 dict;
        p32 hash_mask;
        p32 cyclic_pos;
        p32 cyclic_size;
        p32 nice;
        p32 depth;

        xz_probability_state models;
        xz_range_state rc;
        p32 lc;
        p32 lp;
        p32 pb;
        p32 lp_mask;
        p32 pos_mask;
        p8 state;
        p32 reps[4];
        p64 position;
        p32 match_count;
        p32 longest;
        xz_found matches[XZ_MATCH_MAX + 1];

        p8 bit_price[128];
        xz_length_price match_prices;
        xz_length_price rep_prices;
        p32 len_table_size;
        p32 dist_slot_prices[4][XZ_DIST_SLOTS];
        p32 dist_prices[4][XZ_FULL_DIST];
        p32 dist_table_size;
        p32 match_price_count;
        p32 align_prices[XZ_ALIGN];
        p32 align_price_count;
        p32 opts_end;
        p32 opts_current;
        xz_optimal opts[XZ_OPTS];

        /* The finished block: out_n bytes at out + out_at. */
        p8 address_to out;
        positive out_room;
        positive out_at;
        positive out_n;
        p64 unpadded;
        p8 check;
        p8 chunk[XZ_CHUNK_PACKED_MAX + 32768];
} xz_encoder;

static bool xz_area(p8 address_to address_to area, positive address_to room,
                    positive need, bool address_to fresh)
{
        address_to fresh = false;
        if (address_to room >= need)
                return true;
        if (address_to area)
                memory_free(address_to area, address_to room);
        address_to area = null;
        address_to room = 0;
        p8 address_to bytes = (p8 address_to)memory_checked(need);
        if (!bytes)
                return false;
        address_to area = bytes;
        address_to room = need;
        address_to fresh = true;
        return true;
}

static xz_encoder address_to xz_encoder_open_with(const xz_options address_to o)
{
        xz_encoder address_to e = (xz_encoder address_to)memory_checked(sizeof(xz_encoder));

        if (!e)
                return null;
        e->preset = o->lz;
        e->pick = o->pick;
        e->nfilt = o->nfilt;
        memory_copy_apart(e->filt, o->filt, sizeof(e->filt));
        e->check = XZ_CHECK_CRC64;
        e->lc = o->lz.lc;
        e->lp = o->lz.lp;
        e->pb = o->lz.pb;
        e->lp_mask = ((p32)1 << e->lp) - 1;
        e->pos_mask = ((p32)1 << e->pb) - 1;
        for (p32 i = 8; i < 2048; i += 16)
        {
                p32 w = i;
                p32 bits = 0;

                for (p32 j = 0; j < 4; j++)
                {
                        w *= w;
                        bits <<= 1;
                        while (w >= (1u << 16))
                        {
                                w >>= 1;
                                bits++;
                        }
                }
                e->bit_price[i >> 4] = (p8)((11u << 4) - 15 - bits);
        }
        return e;
}

static xz_encoder address_to xz_encoder_open(p8 level)
{
        xz_options o;

        memory_fill(address_of o, 0, sizeof(o));
        o.lz = xz_preset_of(level);
        return xz_encoder_open_with(address_of o);
}

static fn xz_encoder_close(xz_encoder address_to e)
{
        if (!e)
                return;
        memory_free(e->hash, e->hash_room);
        memory_free(e->son, e->son_room);
        memory_free(e->out, e->out_room);
        memory_free(e->fbuf, e->fbuf_room);
        if (e->trial)
                xz_encoder_close((xz_encoder address_to)e->trial);
        memory_free(e->pipe_area, e->pipe_room);
        memory_free(e, sizeof(xz_encoder));
}

/* Prices, in 1/16 bits. */
static inline INLINE p32 xz_price(xz_encoder address_to e, p32 prob, p32 bit)
{
        return e->bit_price[(prob ^ ((0u - bit) & 2047)) >> 4];
}

static inline INLINE p32 xz_price0(xz_encoder address_to e, p32 prob)
{
        return e->bit_price[prob >> 4];
}

static inline INLINE p32 xz_price1(xz_encoder address_to e, p32 prob)
{
        return e->bit_price[(prob ^ 2047) >> 4];
}

static inline INLINE p32 xz_tree_price(xz_encoder address_to e, p16 address_to probs,
                                       p32 bits, p32 symbol)
{
        p32 price = 0;

        symbol += 1u << bits;
        do
        {
                p32 bit = symbol & 1;

                symbol >>= 1;
                price += xz_price(e, probs[symbol], bit);
        } while (symbol != 1);
        return price;
}

static inline INLINE p32 xz_reverse_price(xz_encoder address_to e, p16 address_to probs,
                                          p32 bits, p32 symbol)
{
        p32 price = 0;
        p32 index = 1;

        do
        {
                p32 bit = symbol & 1;

                symbol >>= 1;
                price += xz_price(e, probs[index], bit);
                index = (index << 1) + bit;
        } while (--bits);
        return price;
}

static inline INLINE p32 xz_slot(p32 dist)
{
        if (dist < 4)
                return dist;
        p32 top = top_bit_known(dist);
        return (top << 1) + ((dist >> (top - 1)) & 1);
}

static fn xz_length_prices(xz_encoder address_to e, bool rep, p32 ps)
{
        xz_probability_state address_to m = address_of e->models;
        xz_length_price address_to t = rep ? address_of e->rep_prices : address_of e->match_prices;
        p32 choice = rep ? m->rep_choice : m->match_choice;
        p32 choice2 = rep ? m->rep_choice2 : m->match_choice2;
        p16 address_to low = rep ? m->rep_low[ps] : m->match_low[ps];
        p16 address_to mid = rep ? m->rep_mid[ps] : m->match_mid[ps];
        p16 address_to high = rep ? m->rep_high : m->match_high;
        p32 a0 = xz_price0(e, choice);
        p32 a1 = xz_price1(e, choice);
        p32 b0 = a1 + xz_price0(e, choice2);
        p32 b1 = a1 + xz_price1(e, choice2);
        p32 size = e->len_table_size;
        p32 i;

        t->counters[ps] = size;
        for (i = 0; i < size && i < XZ_LEN_LOW; i++)
                t->prices[ps][i] = a0 + xz_tree_price(e, low, 3, i);
        for (; i < size && i < XZ_LEN_LOW + XZ_LEN_MID; i++)
                t->prices[ps][i] = b0 + xz_tree_price(e, mid, 3, i - XZ_LEN_LOW);
        for (; i < size; i++)
                t->prices[ps][i] = b1 + xz_tree_price(e, high, 8,
                                                      i - XZ_LEN_LOW - XZ_LEN_MID);
}

static fn xz_fill_dist_prices(xz_encoder address_to e)
{
        for (p32 ds = 0; ds < 4; ds++)
        {
                p32 address_to slot_prices = e->dist_slot_prices[ds];

                for (p32 slot = 0; slot < e->dist_table_size; slot++)
                        slot_prices[slot] = xz_tree_price(e, e->models.dist_slot[ds], 6, slot);
                for (p32 slot = 14; slot < e->dist_table_size; slot++)
                        slot_prices[slot] += (((slot >> 1) - 1) - 4) << 4;
                for (p32 i = 0; i < 4; i++)
                        e->dist_prices[ds][i] = slot_prices[i];
        }
        for (p32 i = 4; i < XZ_FULL_DIST; i++)
        {
                p32 slot = xz_slot(i);
                p32 footer = (slot >> 1) - 1;
                p32 base = (2 | (slot & 1)) << footer;
                p32 price = xz_reverse_price(e, e->models.dist_special + base - slot - 1,
                                             footer, i - base);

                for (p32 ds = 0; ds < 4; ds++)
                        e->dist_prices[ds][i] = price + e->dist_slot_prices[ds][slot];
        }
        e->match_price_count = 0;
}

static fn xz_fill_align_prices(xz_encoder address_to e)
{
        for (p32 i = 0; i < XZ_ALIGN; i++)
                e->align_prices[i] = xz_reverse_price(e, e->models.dist_align, 4, i);
        e->align_price_count = 0;
}

static inline INLINE p16 address_to xz_literal_probs(xz_encoder address_to e, p64 position,
                                                    p8 prev)
{
        return e->models.lit +
               0x300 * ((((p32)position & e->lp_mask) << e->lc) + ((p32)prev >> (8 - e->lc)));
}

static p32 xz_literal_price(xz_encoder address_to e, p64 position, p8 prev, bool matched,
                            p32 match_byte, p32 symbol)
{
        p16 address_to probs = xz_literal_probs(e, position, prev);

        if (!matched)
                return xz_tree_price(e, probs, 8, symbol);

        p32 price = 0;
        p32 offset = 0x100;

        symbol += 0x100;
        do
        {
                match_byte <<= 1;
                p32 match_bit = match_byte & offset;
                p32 index = offset + match_bit + (symbol >> 8);
                p32 bit = (symbol >> 7) & 1;

                price += xz_price(e, probs[index], bit);
                symbol <<= 1;
                offset &= ~(match_byte ^ symbol);
        } while (symbol < 0x10000);
        return price;
}

static inline INLINE p32 xz_short_rep_price(xz_encoder address_to e, p32 state, p32 ps)
{
        return xz_price0(e, e->models.is_rep0[state]) +
               xz_price0(e, e->models.is_rep0_long[state][ps]);
}

static inline INLINE p32 xz_pure_rep_price(xz_encoder address_to e, p32 rep, p32 state,
                                           p32 ps)
{
        xz_probability_state address_to m = address_of e->models;

        if (!rep)
                return xz_price0(e, m->is_rep0[state]) +
                       xz_price1(e, m->is_rep0_long[state][ps]);
        p32 price = xz_price1(e, m->is_rep0[state]);
        if (rep == 1)
                return price + xz_price0(e, m->is_rep1[state]);
        return price + xz_price1(e, m->is_rep1[state]) +
               xz_price(e, m->is_rep2[state], rep - 2);
}

static inline INLINE p32 xz_rep_price(xz_encoder address_to e, p32 rep, p32 len,
                                      p32 state, p32 ps)
{
        return e->rep_prices.prices[ps][len - 2] + xz_pure_rep_price(e, rep, state, ps);
}

static inline INLINE p32 xz_dist_len_price(xz_encoder address_to e, p32 dist, p32 len,
                                           p32 ps)
{
        p32 ds = len < 6 ? len - 2 : 3;
        p32 price = dist < XZ_FULL_DIST
                ? e->dist_prices[ds][dist]
                : e->dist_slot_prices[ds][xz_slot(dist)] + e->align_prices[dist & 15];

        return price + e->match_prices.prices[ps][len - 2];
}

/* Range coding into the chunk buffer. */
static inline INLINE fn xz_bit(xz_encoder address_to e, p16 address_to prob, p32 bit)
{
        lzma_range_encode(address_of e->rc, prob, bit, 0);
}

static fn xz_direct(xz_encoder address_to e, p32 value, p32 bits)
{
        while (bits)
        {
                bits--;
                e->rc.range >>= 1;
                if ((value >> bits) & 1)
                        e->rc.low += e->rc.range;
                while (e->rc.range < 0x1000000u)
                {
                        lzma_range_shift(address_of e->rc);
                        e->rc.range <<= 8;
                }
        }
}

static fn xz_length(xz_encoder address_to e, bool rep, p32 ps, p32 len)
{
        xz_probability_state address_to m = address_of e->models;
        p16 address_to choice = rep ? address_of m->rep_choice : address_of m->match_choice;
        p16 address_to choice2 = rep ? address_of m->rep_choice2 : address_of m->match_choice2;

        len -= 2;
        if (len < XZ_LEN_LOW)
        {
                xz_bit(e, choice, 0);
                lzma_range_encode(address_of e->rc, rep ? m->rep_low[ps] : m->match_low[ps],
                                  len, 3);
        }
        else
        {
                xz_bit(e, choice, 1);
                len -= XZ_LEN_LOW;
                if (len < XZ_LEN_MID)
                {
                        xz_bit(e, choice2, 0);
                        lzma_range_encode(address_of e->rc,
                                          rep ? m->rep_mid[ps] : m->match_mid[ps], len, 3);
                }
                else
                {
                        xz_bit(e, choice2, 1);
                        lzma_range_encode(address_of e->rc, rep ? m->rep_high : m->match_high,
                                          len - XZ_LEN_MID, 8);
                }
        }
        if (e->preset.normal)
        {
                xz_length_price address_to t = rep ? address_of e->rep_prices
                                                   : address_of e->match_prices;
                if (--t->counters[ps] == 0)
                        xz_length_prices(e, rep, ps);
        }
}

static fn xz_code_match(xz_encoder address_to e, p32 ps, p32 dist, p32 len)
{
        p32 slot = xz_slot(dist);

        e->state = e->state < 7 ? 7 : 10;
        xz_length(e, false, ps, len);
        lzma_range_encode(address_of e->rc, e->models.dist_slot[len < 6 ? len - 2 : 3],
                          slot, 6);
        if (slot >= 4)
        {
                p32 footer = (slot >> 1) - 1;
                p32 base = (2 | (slot & 1)) << footer;
                p32 reduced = dist - base;

                if (slot < 14)
                        lzma_range_encode(address_of e->rc,
                                          e->models.dist_special + base - slot - 1,
                                          reduced, 0x100 | footer);
                else
                {
                        xz_direct(e, reduced >> 4, footer - 4);
                        lzma_range_encode(address_of e->rc, e->models.dist_align,
                                          reduced & 15, 0x100 | 4);
                        e->align_price_count++;
                }
        }
        e->reps[3] = e->reps[2];
        e->reps[2] = e->reps[1];
        e->reps[1] = e->reps[0];
        e->reps[0] = dist;
        e->match_price_count++;
}

static fn xz_code_rep(xz_encoder address_to e, p32 ps, p32 rep, p32 len)
{
        xz_probability_state address_to m = address_of e->models;
        p8 state = e->state;

        if (!rep)
        {
                xz_bit(e, address_of m->is_rep0[state], 0);
                xz_bit(e, address_of m->is_rep0_long[state][ps], len != 1);
        }
        else
        {
                p32 distance = e->reps[rep];

                xz_bit(e, address_of m->is_rep0[state], 1);
                if (rep == 1)
                        xz_bit(e, address_of m->is_rep1[state], 0);
                else
                {
                        xz_bit(e, address_of m->is_rep1[state], 1);
                        xz_bit(e, address_of m->is_rep2[state], rep - 2);
                        if (rep == 3)
                                e->reps[3] = e->reps[2];
                        e->reps[2] = e->reps[1];
                }
                e->reps[1] = e->reps[0];
                e->reps[0] = distance;
        }
        if (len == 1)
                e->state = state < 7 ? 9 : 11;
        else
        {
                xz_length(e, true, ps, len);
                e->state = state < 7 ? 8 : 11;
        }
}

static fn xz_code_symbol(xz_encoder address_to e, p32 back, p32 len)
{
        xz_probability_state address_to m = address_of e->models;
        p32 ps = (p32)e->position & e->pos_mask;
        p8 state = e->state;

        if (back == XZ_LITERAL)
        {
                p8 address_to at = e->input + e->read_pos - e->read_ahead;
                p16 address_to probs = xz_literal_probs(e, e->position, at[-1]);

                xz_bit(e, address_of m->is_match[state][ps], 0);
                if (state < 7)
                {
                        e->state = state < 4 ? 0 : state - 3;
                        lzma_range_encode(address_of e->rc, probs, at[0], 8);
                }
                else
                {
                        p8 match = at[-(bipolar)e->reps[0] - 1];

                        e->state = state < 10 ? state - 3 : state - 6;
                        lzma_range_encode(address_of e->rc, probs, at[0],
                                          0x200 | 8 | ((positive)match << 16));
                }
        }
        else
        {
                xz_bit(e, address_of m->is_match[state][ps], 1);
                if (back < 4)
                {
                        xz_bit(e, address_of m->is_rep[state], 1);
                        xz_code_rep(e, ps, back, len);
                }
                else
                {
                        xz_bit(e, address_of m->is_rep[state], 0);
                        xz_code_match(e, ps, back - 4, len);
                }
        }
        e->read_ahead -= len;
        e->position += len;
}

/* Match finders. A candidate at distance delta (1-based) is stored as
   dist = delta - 1. Word compares read up to seven bytes past limit; the
   block's slack keeps that readable and the result is clamped. */
static inline INLINE p32 xz_common(p8 address_to a, p8 address_to b, p32 len, p32 limit)
{
        while (len < limit)
        {
                p64 x = memory_load_unaligned(p64, a + len) ^
                        memory_load_unaligned(p64, b + len);

                if (x)
                {
                        len += bottom_bit_known(x) >> 3;
                        return len < limit ? len : limit;
                }
                len += 8;
        }
        return limit;
}

static inline INLINE bool xz_differ16(p8 address_to a, p8 address_to b)
{
        return memory_load_unaligned(p16, a) != memory_load_unaligned(p16, b);
}

static inline INLINE fn xz_move(xz_finder address_to e)
{
        if (++e->cyclic_pos == e->cyclic_size)
                e->cyclic_pos = 0;
        e->read_pos++;
}

/* The two- and three-byte hashes keep the byte-table form: once the first
   byte of a candidate is known equal, an equal hash proves the second (and
   third) equal too, so those candidates start their compare past them. */
static inline INLINE p32 xz_hash_head(p8 address_to cur)
{
        return hash_crc32_tab[cur[0]] ^ cur[1];
}

static xz_found address_to xz_chain(xz_finder address_to e, p32 len_limit, p32 pos,
                                    p8 address_to cur, p32 cur_match,
                                    xz_found address_to matches, p32 len_best)
{
        p32 address_to son = e->son;
        p32 cyclic_pos = e->cyclic_pos;
        p32 cyclic_size = e->cyclic_size;
        p32 depth = e->depth;

        son[cyclic_pos] = cur_match;
        for (;;)
        {
                p32 delta = pos - cur_match;

                if (depth-- == 0 || delta >= cyclic_size)
                        return matches;
                p8 address_to pb = cur - delta;
                cur_match = son[cyclic_pos - delta + (delta > cyclic_pos ? cyclic_size : 0)];
                if (pb[len_best] == cur[len_best] && pb[0] == cur[0])
                {
                        p32 len = xz_common(pb, cur, 1, len_limit);

                        if (len_best < len)
                        {
                                len_best = len;
                                matches->len = len;
                                matches->dist = delta - 1;
                                matches++;
                                if (len == len_limit)
                                        return matches;
                        }
                }
        }
}

/* The binary tree walks. Every loop value is a plain local and find and
   skip are separate, so neither carries the other's state; that keeps the
   walk in registers instead of spilling it to the stack. The walk is a
   chain of misses (son is 8 bytes a dictionary position, past L3 from -6
   up), and which child comes next hangs on a byte compare no predictor
   gets right half the time, so both children's pairs and bytes are
   fetched as soon as their node's pair arrives: the right one is then in
   flight whichever way the branch goes. */
static xz_found address_to xz_tree_find(p32 address_to son, p8 address_to cur, p32 pos,
                                        p32 cur_match, p32 depth, p32 cyclic_pos,
                                        p32 cyclic_size, p32 len_limit,
                                        xz_found address_to matches, p32 len_best)
{
        p32 address_to ptr0 = son + ((positive)cyclic_pos << 1) + 1;
        p32 address_to ptr1 = son + ((positive)cyclic_pos << 1);
        p32 len0 = 0;
        p32 len1 = 0;

        for (;;)
        {
                p32 delta = pos - cur_match;

                if (depth-- == 0 || delta >= cyclic_size)
                {
                        *ptr0 = 0;
                        *ptr1 = 0;
                        return matches;
                }
                p32 at = cyclic_pos - delta;
                at += delta > cyclic_pos ? cyclic_size : 0;
                p32 address_to pair = son + ((positive)at << 1);
                p8 address_to pb = cur - delta;
                p32 len = len0 < len1 ? len0 : len1;
                {
                        p32 d0 = pos - pair[0];
                        p32 d1 = pos - pair[1];
                        p32 a0 = cyclic_pos - d0 + (d0 > cyclic_pos ? cyclic_size : 0);
                        p32 a1 = cyclic_pos - d1 + (d1 > cyclic_pos ? cyclic_size : 0);

                        __builtin_prefetch(son + ((positive)a0 << 1));
                        __builtin_prefetch(son + ((positive)a1 << 1));
                        __builtin_prefetch(cur - d0 + len);
                        __builtin_prefetch(cur - d1 + len);
                }

                if (pb[len] == cur[len])
                {
                        len = xz_common(pb, cur, len + 1, len_limit);
                        if (len_best < len)
                        {
                                len_best = len;
                                matches->len = len;
                                matches->dist = delta - 1;
                                matches++;
                                if (len == len_limit)
                                {
                                        *ptr1 = pair[0];
                                        *ptr0 = pair[1];
                                        return matches;
                                }
                        }
                }
                if (pb[len] < cur[len])
                {
                        *ptr1 = cur_match;
                        ptr1 = pair + 1;
                        cur_match = *ptr1;
                        len1 = len;
                }
                else
                {
                        *ptr0 = cur_match;
                        ptr0 = pair;
                        cur_match = *ptr0;
                        len0 = len;
                }
        }
}

static fn xz_tree_skip(p32 address_to son, p8 address_to cur, p32 pos, p32 cur_match,
                       p32 depth, p32 cyclic_pos, p32 cyclic_size, p32 len_limit)
{
        p32 address_to ptr0 = son + ((positive)cyclic_pos << 1) + 1;
        p32 address_to ptr1 = son + ((positive)cyclic_pos << 1);
        p32 len0 = 0;
        p32 len1 = 0;

        for (;;)
        {
                p32 delta = pos - cur_match;

                if (depth-- == 0 || delta >= cyclic_size)
                {
                        *ptr0 = 0;
                        *ptr1 = 0;
                        return;
                }
                p32 at = cyclic_pos - delta;
                at += delta > cyclic_pos ? cyclic_size : 0;
                p32 address_to pair = son + ((positive)at << 1);
                p8 address_to pb = cur - delta;
                p32 len = len0 < len1 ? len0 : len1;
                {
                        p32 d0 = pos - pair[0];
                        p32 d1 = pos - pair[1];
                        p32 a0 = cyclic_pos - d0 + (d0 > cyclic_pos ? cyclic_size : 0);
                        p32 a1 = cyclic_pos - d1 + (d1 > cyclic_pos ? cyclic_size : 0);

                        __builtin_prefetch(son + ((positive)a0 << 1));
                        __builtin_prefetch(son + ((positive)a1 << 1));
                        __builtin_prefetch(cur - d0 + len);
                        __builtin_prefetch(cur - d1 + len);
                }

                if (pb[len] == cur[len])
                {
                        len = xz_common(pb, cur, len + 1, len_limit);
                        if (len == len_limit)
                        {
                                *ptr1 = pair[0];
                                *ptr0 = pair[1];
                                return;
                        }
                }
                if (pb[len] < cur[len])
                {
                        *ptr1 = cur_match;
                        ptr1 = pair + 1;
                        cur_match = *ptr1;
                        len1 = len;
                }
                else
                {
                        *ptr0 = cur_match;
                        ptr0 = pair;
                        cur_match = *ptr0;
                        len0 = len;
                }
        }
}

/* One find at read_pos: the matches in increasing length, their count. */
static p32 xz_finder_find(xz_finder address_to e)
{
        const xz_preset address_to p = address_of e->preset;
        p32 avail = e->input_n - e->read_pos;
        p32 len_min = p->finder == XZ_FINDER_HC3 ? 3 : 4;
        p32 len_limit = e->nice;

        if (avail < len_limit)
        {
                if (avail < len_min)
                {
                        e->read_pos++;
                        return 0;
                }
                len_limit = avail;
        }

        p8 address_to cur = e->input + e->read_pos;
        p32 pos = e->read_pos + e->offset;
        p32 address_to hash = e->hash;
        xz_found address_to matches = e->matches;
        p32 count = 0;
        p32 temp = xz_hash_head(cur);
        p32 h2 = temp & (XZ_HASH2_SIZE - 1);
        p32 delta2 = pos - hash[h2];

        hash[h2] = pos;
        temp ^= (p32)cur[2] << 8;
        if (p->finder == XZ_FINDER_HC3)
        {
                p32 hv = XZ_HASH2_SIZE + (temp & e->hash_mask);
                p32 cur_match = hash[hv];
                p32 len_best = 2;

                __builtin_prefetch(hash + XZ_HASH2_SIZE +
                                   ((xz_hash_head(cur + 1) ^ ((p32)cur[3] << 8)) & e->hash_mask));

                hash[hv] = pos;
                if (delta2 < e->cyclic_size && *(cur - delta2) == *cur)
                {
                        len_best = xz_common(cur - delta2, cur, 2, len_limit);
                        matches[0].len = len_best;
                        matches[0].dist = delta2 - 1;
                        count = 1;
                        if (len_best == len_limit)
                        {
                                e->son[e->cyclic_pos] = cur_match;
                                xz_move(e);
                                return 1;
                        }
                }
                count = (p32)(xz_chain(e, len_limit, pos, cur, cur_match, matches + count,
                                       len_best) - matches);
                xz_move(e);
                return count;
        }

        p32 h3 = XZ_HASH2_SIZE + (temp & (XZ_HASH3_SIZE - 1));
        p32 h4 = XZ_HASH2_SIZE + XZ_HASH3_SIZE +
                 ((temp ^ (hash_crc32_tab[cur[3]] << 5)) & e->hash_mask);
        //      The next position's bucket, a miss of its own, is fetched a
        //      whole walk ahead; its bytes lie within the block's slack.
        __builtin_prefetch(hash + XZ_HASH2_SIZE + XZ_HASH3_SIZE +
                           ((xz_hash_head(cur + 1) ^ ((p32)cur[3] << 8) ^
                             (hash_crc32_tab[cur[4]] << 5)) & e->hash_mask));
        p32 delta3 = pos - hash[h3];
        p32 cur_match = hash[h4];
        p32 len_best = 1;

        hash[h3] = pos;
        hash[h4] = pos;
        if (delta2 < e->cyclic_size && *(cur - delta2) == *cur)
        {
                len_best = 2;
                matches[0].len = 2;
                matches[0].dist = delta2 - 1;
                count = 1;
        }
        if (delta2 != delta3 && delta3 < e->cyclic_size && *(cur - delta3) == *cur)
        {
                len_best = 3;
                matches[count++].dist = delta3 - 1;
                delta2 = delta3;
        }
        bool tree = p->finder == XZ_FINDER_BT4;
        if (count)
        {
                len_best = xz_common(cur - delta2, cur, len_best, len_limit);
                matches[count - 1].len = len_best;
                if (len_best == len_limit)
                {
                        if (tree)
                                xz_tree_skip(e->son, cur, pos, cur_match, e->depth,
                                             e->cyclic_pos, e->cyclic_size, len_limit);
                        else
                                e->son[e->cyclic_pos] = cur_match;
                        xz_move(e);
                        return count;
                }
        }
        if (len_best < 3)
                len_best = 3;
        count = (p32)((tree ? xz_tree_find(e->son, cur, pos, cur_match, e->depth,
                                           e->cyclic_pos, e->cyclic_size, len_limit,
                                           matches + count, len_best)
                            : xz_chain(e, len_limit, pos, cur, cur_match, matches + count,
                                       len_best)) - matches);
        xz_move(e);
        return count;
}

static fn xz_finder_skip(xz_finder address_to e, p32 amount)
{
        const xz_preset address_to p = address_of e->preset;
        p32 len_min = p->finder == XZ_FINDER_HC3 ? 3 : 4;

        while (amount--)
        {
                p32 avail = e->input_n - e->read_pos;

                if (avail < len_min)
                {
                        e->read_pos++;
                        continue;
                }
                p8 address_to cur = e->input + e->read_pos;
                p32 pos = e->read_pos + e->offset;
                p32 address_to hash = e->hash;
                p32 temp = xz_hash_head(cur);
                p32 cur_match;

                hash[temp & (XZ_HASH2_SIZE - 1)] = pos;
                temp ^= (p32)cur[2] << 8;
                if (p->finder == XZ_FINDER_HC3)
                {
                        p32 hv = XZ_HASH2_SIZE + (temp & e->hash_mask);
                        cur_match = hash[hv];
                        __builtin_prefetch(hash + XZ_HASH2_SIZE +
                                           ((xz_hash_head(cur + 1) ^ ((p32)cur[3] << 8)) & e->hash_mask));
                        hash[hv] = pos;
                }
                else
                {
                        p32 h4 = XZ_HASH2_SIZE + XZ_HASH3_SIZE +
                                 ((temp ^ (hash_crc32_tab[cur[3]] << 5)) & e->hash_mask);
                        __builtin_prefetch(hash + XZ_HASH2_SIZE + XZ_HASH3_SIZE +
                                           ((xz_hash_head(cur + 1) ^ ((p32)cur[3] << 8) ^
                                             (hash_crc32_tab[cur[4]] << 5)) & e->hash_mask));
                        hash[XZ_HASH2_SIZE + (temp & (XZ_HASH3_SIZE - 1))] = pos;
                        cur_match = hash[h4];
                        hash[h4] = pos;
                }
                if (p->finder == XZ_FINDER_BT4)
                        xz_tree_skip(e->son, cur, pos, cur_match, e->depth, e->cyclic_pos,
                                     e->cyclic_size, avail < e->nice ? avail : e->nice);
                else
                        e->son[e->cyclic_pos] = cur_match;
                xz_move(e);
        }
}

/*
        The match finder beside the parser. The find at each position is a
        chain of cache misses down a tree the parser never looks at, and it
        is more than half of what a block costs from -4 up; the parser's
        pricing is the rest. When a stream is one block and the process has
        a second CPU, the finder runs on the pool's beside thread over its
        own copy of the finder state and hands what each position found to
        the parser through a ring of words: the count, then a length and a
        distance for each match. The parser reads the same sequence it would
        have made itself, a find or a skip of the tables being the same
        insertion, so the bytes are the same with and without the thread.
        Both sides publish and wait on two counters, sleeping only when the
        ring is empty or full.
*/
#if X64
#define XZ_PIPE_PAUSE() __asm__ volatile("pause" ::: "memory")
#elif ARM64
#define XZ_PIPE_PAUSE() __asm__ volatile("yield" ::: "memory")
#else
#define XZ_PIPE_PAUSE() __asm__ volatile("" ::: "memory")
#endif
/* Loads of the other side's counter before a thread goes to sleep on it:
   a futex round trip costs more than a batch of finds, and the side that
   waits is the faster one, which has nothing better to do. */
#define XZ_PIPE_SPIN 0
#define XZ_PIPE_WORDS ((positive)1 << 15)
#define XZ_PIPE_BATCH 128
#define XZ_PIPE_GIVE 2048

typedef struct
{
        xz_finder fin;
        xz_found matches[XZ_MATCH_MAX + 1];
        p32 count;
        b32 head;
        b32 tail;
        b32 consumer_asleep;
        b32 producer_asleep;
        b32 quit;
        b32 skip_to;
        b32 tail_local;
        b32 head_seen;
        b32 tail_given;
        p32 words[XZ_PIPE_WORDS];
} xz_pipe;

static fn xz_pipe_job(address_any context, positive index)
{
        xz_pipe address_to q = (xz_pipe address_to)context;
        xz_finder address_to f = address_of q->fin;
        b32 head = 0;
        b32 tail_seen = 0;
        positive batch = 0;

        (void)index;
        for (p32 at = 0; at < q->count; at++)
        {
                p32 count = 0;

                //      A position the parser has said it will skip is inserted
                //      and no more: the tables come out the same, and the
                //      answer is a count of none nobody reads.
                if ((b32)at < atomic_load(address_of q->skip_to))
                        xz_finder_skip(f, 1);
                else
                        count = xz_finder_find(f);

                b32 need = (b32)(1 + 2 * count);

                while ((positive)(head - tail_seen) + (positive)need > XZ_PIPE_WORDS)
                {
                        tail_seen = atomic_load(address_of q->tail);
                        if ((positive)(head - tail_seen) + (positive)need <= XZ_PIPE_WORDS)
                                break;
                        __atomic_store_n(address_of q->head, head, __ATOMIC_SEQ_CST);
                        if (atomic_load(address_of q->consumer_asleep))
                                thread_wake(address_of q->head, 1);
                        if (atomic_load(address_of q->quit))
                                return;
                        for (positive spin = 0; spin < XZ_PIPE_SPIN; spin++)
                        {
                                XZ_PIPE_PAUSE();
                                tail_seen = atomic_load(address_of q->tail);
                                if ((positive)(head - tail_seen) + (positive)need <= XZ_PIPE_WORDS)
                                        break;
                        }
                        if ((positive)(head - tail_seen) + (positive)need <= XZ_PIPE_WORDS)
                                break;
                        __atomic_store_n(address_of q->producer_asleep, 1, __ATOMIC_SEQ_CST);
                        tail_seen = atomic_load(address_of q->tail);
                        if ((positive)(head - tail_seen) + (positive)need > XZ_PIPE_WORDS &&
                            !atomic_load(address_of q->quit))
                                thread_wait(address_of q->tail, tail_seen);
                        __atomic_store_n(address_of q->producer_asleep, 0, __ATOMIC_SEQ_CST);
                }
                q->words[(positive)head & (XZ_PIPE_WORDS - 1)] = count;
                for (p32 i = 0; i < count; i++)
                {
                        q->words[(positive)(head + 1 + 2 * i) & (XZ_PIPE_WORDS - 1)] = f->matches[i].len;
                        q->words[(positive)(head + 2 + 2 * i) & (XZ_PIPE_WORDS - 1)] = f->matches[i].dist;
                }
                head += need;
                if (++batch == XZ_PIPE_BATCH)
                {
                        batch = 0;
                        __atomic_store_n(address_of q->head, head, __ATOMIC_SEQ_CST);
                        if (atomic_load(address_of q->consumer_asleep))
                                thread_wake(address_of q->head, 1);
                        if (atomic_load(address_of q->quit))
                                return;
                }
        }
        __atomic_store_n(address_of q->head, head, __ATOMIC_SEQ_CST);
        if (atomic_load(address_of q->consumer_asleep))
                thread_wake(address_of q->head, 1);
}

/* The next position's find, as the finder would have answered it: the
   matches go to e->matches and the count comes back. */
static p32 xz_pipe_pop(xz_encoder address_to e)
{
        xz_pipe address_to q = (xz_pipe address_to)e->pipe;

        while (q->tail_local == q->head_seen)
        {
                q->head_seen = atomic_load(address_of q->head);
                if (q->tail_local != q->head_seen)
                        break;
                if (q->tail_local != q->tail_given)
                {
                        q->tail_given = q->tail_local;
                        __atomic_store_n(address_of q->tail, q->tail_local, __ATOMIC_SEQ_CST);
                        if (atomic_load(address_of q->producer_asleep))
                                thread_wake(address_of q->tail, 1);
                }
                for (positive spin = 0; spin < XZ_PIPE_SPIN; spin++)
                {
                        XZ_PIPE_PAUSE();
                        q->head_seen = atomic_load(address_of q->head);
                        if (q->tail_local != q->head_seen)
                                break;
                }
                if (q->tail_local != q->head_seen)
                        break;
                __atomic_store_n(address_of q->consumer_asleep, 1, __ATOMIC_SEQ_CST);
                q->head_seen = atomic_load(address_of q->head);
                if (q->tail_local == q->head_seen)
                        thread_wait(address_of q->head, q->head_seen);
                __atomic_store_n(address_of q->consumer_asleep, 0, __ATOMIC_SEQ_CST);
                q->head_seen = atomic_load(address_of q->head);
        }

        positive at = (positive)q->tail_local;
        p32 count = q->words[at & (XZ_PIPE_WORDS - 1)];

        for (p32 i = 0; i < count; i++)
        {
                e->matches[i].len = q->words[(at + 1 + 2 * i) & (XZ_PIPE_WORDS - 1)];
                e->matches[i].dist = q->words[(at + 2 + 2 * i) & (XZ_PIPE_WORDS - 1)];
        }
        q->tail_local += (b32)(1 + 2 * count);
        e->read_pos++;
        if ((positive)(q->tail_local - q->tail_given) >= XZ_PIPE_GIVE)
        {
                q->tail_given = q->tail_local;
                __atomic_store_n(address_of q->tail, q->tail_local, __ATOMIC_SEQ_CST);
                if (atomic_load(address_of q->producer_asleep))
                        thread_wake(address_of q->tail, 1);
        }
        return count;
}

/* Start the finder beside the caller on the block the encoder holds, or
   leave it to the parser. */
static fn xz_pipe_start(xz_encoder address_to e)
{
        e->pipe = null;
        //      The fast modes skip most of what a match covers with one hash
        //      insert; finding at every one of those on another thread costs
        //      more than the parser saves.
        if (!e->pipe_ok || !e->preset.normal || e->input_n < ((p32)1 << 16))
                return;
        if (!e->pipe_area)
        {
                e->pipe_area = memory_checked(sizeof(xz_pipe));
                if (!e->pipe_area)
                        return;
                e->pipe_room = sizeof(xz_pipe);
        }

        xz_pipe address_to q = (xz_pipe address_to)e->pipe_area;

        memory_fill(q, 0, __builtin_offsetof(xz_pipe, words));
        q->fin = e->fin;
        q->fin.matches = q->matches;
        q->count = e->input_n;
        if (!parallel_beside(xz_pipe_job, q))
                return;
        e->pipe = q;
}

/* The block is done, or given up: the finder is told to stop, and joined. */
static fn xz_pipe_stop(xz_encoder address_to e)
{
        xz_pipe address_to q = (xz_pipe address_to)e->pipe;

        if (!q)
                return;
        __atomic_store_n(address_of q->quit, 1, __ATOMIC_SEQ_CST);
        thread_wake(address_of q->tail, 1);
        parallel_beside_wait();
        e->pipe = null;
}

static p32 xz_find(xz_encoder address_to e, p32 address_to count_out)
{
        p32 count;

        if (e->pipe)
                count = xz_pipe_pop(e);
        else
        {
                count = xz_finder_find(address_of e->fin);
                e->read_pos = e->fin.read_pos;
        }
        p32 len_best = 0;

        if (count)
        {
                len_best = e->matches[count - 1].len;
                if (len_best == e->nice)
                {
                        p32 limit = e->input_n - e->read_pos + 1;
                        p8 address_to p1 = e->input + e->read_pos - 1;

                        if (limit > XZ_MATCH_MAX)
                                limit = XZ_MATCH_MAX;
                        len_best = xz_common(p1, p1 - e->matches[count - 1].dist - 1,
                                             len_best, limit);
                }
        }
        address_to count_out = count;
        e->read_ahead++;
        return len_best;
}

static fn xz_skip(xz_encoder address_to e, p32 amount)
{
        if (amount)
        {
                if (e->pipe)
                {
                        xz_pipe address_to q = (xz_pipe address_to)e->pipe;

                        __atomic_store_n(address_of q->skip_to, (b32)(e->read_pos + amount),
                                         __ATOMIC_SEQ_CST);
                        for (p32 i = 0; i < amount; i++)
                                xz_pipe_pop(e);
                }
                else
                {
                        xz_finder_skip(address_of e->fin, amount);
                        e->read_pos = e->fin.read_pos;
                }
                e->read_ahead += amount;
        }
}

static inline INLINE fn xz_make_literal(xz_optimal address_to o)
{
        o->back_prev = XZ_LITERAL;
        o->prev_1_is_literal = false;
}

static inline INLINE fn xz_make_short_rep(xz_optimal address_to o)
{
        o->back_prev = 0;
        o->prev_1_is_literal = false;
}

/* Levels 0-3: the longest match, repeats preferred by a distance rule and
   a one-byte lazy look. */
static fn xz_optimum_fast(xz_encoder address_to e, p32 address_to back_res,
                          p32 address_to len_res)
{
        p32 nice = e->nice;
        p32 len_main;
        p32 count;

        if (!e->read_ahead)
                len_main = xz_find(e, address_of count);
        else
        {
                len_main = e->longest;
                count = e->match_count;
        }

        p8 address_to buf = e->input + e->read_pos - 1;
        p32 buf_avail = e->input_n - e->read_pos + 1;

        if (buf_avail > XZ_MATCH_MAX)
                buf_avail = XZ_MATCH_MAX;
        address_to back_res = XZ_LITERAL;
        address_to len_res = 1;
        if (buf_avail < 2)
                return;

        p32 rep_len = 0;
        p32 rep_index = 0;

        for (p32 i = 0; i < 4; i++)
        {
                p8 address_to back = buf - e->reps[i] - 1;

                if (xz_differ16(buf, back))
                        continue;
                p32 len = xz_common(buf, back, 2, buf_avail);
                if (len >= nice)
                {
                        address_to back_res = i;
                        address_to len_res = len;
                        xz_skip(e, len - 1);
                        return;
                }
                if (len > rep_len)
                {
                        rep_index = i;
                        rep_len = len;
                }
        }
        if (len_main >= nice)
        {
                address_to back_res = e->matches[count - 1].dist + 4;
                address_to len_res = len_main;
                xz_skip(e, len_main - 1);
                return;
        }

        p32 back_main = 0;

        if (len_main >= 2)
        {
                back_main = e->matches[count - 1].dist;
                while (count > 1 && len_main == e->matches[count - 2].len + 1)
                {
                        if (!XZ_CHANGE_PAIR(e->matches[count - 2].dist, back_main))
                                break;
                        count--;
                        len_main = e->matches[count - 1].len;
                        back_main = e->matches[count - 1].dist;
                }
                if (len_main == 2 && back_main >= 0x80)
                        len_main = 1;
        }
        if (rep_len >= 2 &&
            (rep_len + 1 >= len_main ||
             (rep_len + 2 >= len_main && back_main > (1u << 9)) ||
             (rep_len + 3 >= len_main && back_main > (1u << 15))))
        {
                address_to back_res = rep_index;
                address_to len_res = rep_len;
                xz_skip(e, rep_len - 1);
                return;
        }
        if (len_main < 2 || buf_avail <= 2)
                return;

        e->longest = xz_find(e, address_of e->match_count);
        if (e->longest >= 2)
        {
                p32 new_dist = e->matches[e->match_count - 1].dist;

                if ((e->longest >= len_main && new_dist < back_main) ||
                    (e->longest == len_main + 1 && !XZ_CHANGE_PAIR(back_main, new_dist)) ||
                    e->longest > len_main + 1 ||
                    (e->longest + 1 >= len_main && len_main >= 3 &&
                     XZ_CHANGE_PAIR(new_dist, back_main)))
                        return;
        }
        buf++;
        p32 limit = len_main - 1 > 2 ? len_main - 1 : 2;
        for (p32 i = 0; i < 4; i++)
                if (xz_common(buf, buf - e->reps[i] - 1, 0, limit) == limit)
                        return;
        address_to back_res = back_main + 4;
        address_to len_res = len_main;
        xz_skip(e, len_main - 2);
}

static fn xz_backward(xz_encoder address_to e, p32 address_to len_res, p32 address_to back_res,
                      p32 cur)
{
        xz_optimal address_to opts = e->opts;
        p32 pos_mem = opts[cur].pos_prev;
        p32 back_mem = opts[cur].back_prev;

        e->opts_end = cur;
        do
        {
                if (opts[cur].prev_1_is_literal)
                {
                        xz_make_literal(opts + pos_mem);
                        opts[pos_mem].pos_prev = pos_mem - 1;
                        if (opts[cur].prev_2)
                        {
                                opts[pos_mem - 1].prev_1_is_literal = false;
                                opts[pos_mem - 1].pos_prev = opts[cur].pos_prev_2;
                                opts[pos_mem - 1].back_prev = opts[cur].back_prev_2;
                        }
                }
                p32 pos_prev = pos_mem;
                p32 back_cur = back_mem;

                back_mem = opts[pos_prev].back_prev;
                pos_mem = opts[pos_prev].pos_prev;
                opts[pos_prev].back_prev = back_cur;
                opts[pos_prev].pos_prev = cur;
                cur = pos_prev;
        } while (cur);
        e->opts_current = opts[0].pos_prev;
        address_to len_res = opts[0].pos_prev;
        address_to back_res = opts[0].back_prev;
}

static p32 xz_optimum_first(xz_encoder address_to e, p32 address_to back_res,
                            p32 address_to len_res, p32 position)
{
        xz_probability_state address_to m = address_of e->models;
        xz_optimal address_to opts = e->opts;
        p32 nice = e->nice;
        p32 len_main;
        p32 count;

        if (!e->read_ahead)
                len_main = xz_find(e, address_of count);
        else
        {
                len_main = e->longest;
                count = e->match_count;
        }

        p32 buf_avail = e->input_n - e->read_pos + 1;

        if (buf_avail > XZ_MATCH_MAX)
                buf_avail = XZ_MATCH_MAX;
        address_to back_res = XZ_LITERAL;
        address_to len_res = 1;
        if (buf_avail < 2)
                return XZ_LITERAL;

        p8 address_to buf = e->input + e->read_pos - 1;
        p32 rep_lens[4];
        p32 rep_max = 0;

        for (p32 i = 0; i < 4; i++)
        {
                p8 address_to back = buf - e->reps[i] - 1;

                if (xz_differ16(buf, back))
                {
                        rep_lens[i] = 0;
                        continue;
                }
                rep_lens[i] = xz_common(buf, back, 2, buf_avail);
                if (rep_lens[i] > rep_lens[rep_max])
                        rep_max = i;
        }
        if (rep_lens[rep_max] >= nice)
        {
                address_to back_res = rep_max;
                address_to len_res = rep_lens[rep_max];
                xz_skip(e, address_to len_res - 1);
                return XZ_LITERAL;
        }
        if (len_main >= nice)
        {
                address_to back_res = e->matches[count - 1].dist + 4;
                address_to len_res = len_main;
                xz_skip(e, len_main - 1);
                return XZ_LITERAL;
        }

        p8 current = buf[0];
        p8 match_byte = *(buf - e->reps[0] - 1);
        p32 state = e->state;

        if (len_main < 2 && current != match_byte && rep_lens[rep_max] < 2)
                return XZ_LITERAL;

        opts[0].state = e->state;
        p32 ps = position & e->pos_mask;

        opts[1].price = xz_price0(e, m->is_match[state][ps]) +
                        xz_literal_price(e, position, buf[-1], state >= 7, match_byte,
                                         current);
        xz_make_literal(opts + 1);

        p32 match_price = xz_price1(e, m->is_match[state][ps]);
        p32 rep_match_price = match_price + xz_price1(e, m->is_rep[state]);

        if (match_byte == current)
        {
                p32 short_rep_price = rep_match_price + xz_short_rep_price(e, state, ps);

                if (short_rep_price < opts[1].price)
                {
                        opts[1].price = short_rep_price;
                        xz_make_short_rep(opts + 1);
                }
        }

        p32 len_end = len_main > rep_lens[rep_max] ? len_main : rep_lens[rep_max];

        if (len_end < 2)
        {
                address_to back_res = opts[1].back_prev;
                address_to len_res = 1;
                return XZ_LITERAL;
        }
        opts[1].pos_prev = 0;
        for (p32 i = 0; i < 4; i++)
                opts[0].backs[i] = e->reps[i];

        p32 len = len_end;
        do
                opts[len].price = XZ_INFINITY_PRICE;
        while (--len >= 2);

        for (p32 i = 0; i < 4; i++)
        {
                p32 rep_len = rep_lens[i];

                if (rep_len < 2)
                        continue;
                p32 price = rep_match_price + xz_pure_rep_price(e, i, state, ps);
                do
                {
                        p32 cost = price + e->rep_prices.prices[ps][rep_len - 2];

                        if (cost < opts[rep_len].price)
                        {
                                opts[rep_len].price = cost;
                                opts[rep_len].pos_prev = 0;
                                opts[rep_len].back_prev = i;
                                opts[rep_len].prev_1_is_literal = false;
                        }
                } while (--rep_len >= 2);
        }

        p32 normal_match_price = match_price + xz_price0(e, m->is_rep[state]);

        len = rep_lens[0] >= 2 ? rep_lens[0] + 1 : 2;
        if (len <= len_main)
        {
                p32 i = 0;

                while (len > e->matches[i].len)
                        i++;
                for (;; len++)
                {
                        p32 dist = e->matches[i].dist;
                        p32 cost = normal_match_price + xz_dist_len_price(e, dist, len, ps);

                        if (cost < opts[len].price)
                        {
                                opts[len].price = cost;
                                opts[len].pos_prev = 0;
                                opts[len].back_prev = dist + 4;
                                opts[len].prev_1_is_literal = false;
                        }
                        if (len == e->matches[i].len && ++i == count)
                                break;
                }
        }
        return len_end;
}

static inline INLINE p8 xz_after_literal(p8 state)
{
        return state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
}

static inline INLINE p8 xz_after_match(p8 state)
{
        return state < 7 ? 7 : 10;
}

static inline INLINE p8 xz_after_long_rep(p8 state)
{
        return state < 7 ? 8 : 11;
}

static inline INLINE p8 xz_after_short_rep(p8 state)
{
        return state < 7 ? 9 : 11;
}

static p32 xz_optimum_next(xz_encoder address_to e, p32 address_to reps, p8 address_to buf,
                           p32 len_end, p32 position, p32 cur, p32 nice,
                           p32 buf_avail_full)
{
        xz_probability_state address_to m = address_of e->models;
        xz_optimal address_to opts = e->opts;
        p32 count = e->match_count;
        p32 new_len = e->longest;
        p32 pos_prev = opts[cur].pos_prev;
        p8 state;

        if (opts[cur].prev_1_is_literal)
        {
                pos_prev--;
                if (opts[cur].prev_2)
                {
                        state = opts[opts[cur].pos_prev_2].state;
                        state = opts[cur].back_prev_2 < 4 ? xz_after_long_rep(state)
                                                          : xz_after_match(state);
                }
                else
                        state = opts[pos_prev].state;
                state = xz_after_literal(state);
        }
        else
                state = opts[pos_prev].state;

        if (pos_prev == cur - 1)
        {
                state = opts[cur].back_prev == 0 ? xz_after_short_rep(state)
                                                 : xz_after_literal(state);
        }
        else
        {
                p32 pos;

                if (opts[cur].prev_1_is_literal && opts[cur].prev_2)
                {
                        pos_prev = opts[cur].pos_prev_2;
                        pos = opts[cur].back_prev_2;
                        state = xz_after_long_rep(state);
                }
                else
                {
                        pos = opts[cur].back_prev;
                        state = pos < 4 ? xz_after_long_rep(state) : xz_after_match(state);
                }
                if (pos < 4)
                {
                        p32 i;

                        reps[0] = opts[pos_prev].backs[pos];
                        for (i = 1; i <= pos; i++)
                                reps[i] = opts[pos_prev].backs[i - 1];
                        for (; i < 4; i++)
                                reps[i] = opts[pos_prev].backs[i];
                }
                else
                {
                        reps[0] = pos - 4;
                        for (p32 i = 1; i < 4; i++)
                                reps[i] = opts[pos_prev].backs[i - 1];
                }
        }
        opts[cur].state = state;
        for (p32 i = 0; i < 4; i++)
                opts[cur].backs[i] = reps[i];

        p32 cur_price = opts[cur].price;
        p8 current = buf[0];
        p8 match_byte = *(buf - reps[0] - 1);
        p32 ps = position & e->pos_mask;
        p32 cur_and_1_price = cur_price + xz_price0(e, m->is_match[state][ps]) +
                              xz_literal_price(e, position, buf[-1], state >= 7, match_byte,
                                               current);
        bool next_is_literal = false;

        if (cur_and_1_price < opts[cur + 1].price)
        {
                opts[cur + 1].price = cur_and_1_price;
                opts[cur + 1].pos_prev = cur;
                xz_make_literal(opts + cur + 1);
                next_is_literal = true;
        }

        p32 match_price = cur_price + xz_price1(e, m->is_match[state][ps]);
        p32 rep_match_price = match_price + xz_price1(e, m->is_rep[state]);

        if (match_byte == current &&
            !(opts[cur + 1].pos_prev < cur && opts[cur + 1].back_prev == 0))
        {
                p32 short_rep_price = rep_match_price + xz_short_rep_price(e, state, ps);

                if (short_rep_price <= opts[cur + 1].price)
                {
                        opts[cur + 1].price = short_rep_price;
                        opts[cur + 1].pos_prev = cur;
                        xz_make_short_rep(opts + cur + 1);
                        next_is_literal = true;
                }
        }
        if (buf_avail_full < 2)
                return len_end;

        p32 buf_avail = buf_avail_full < nice ? buf_avail_full : nice;

        if (!next_is_literal && match_byte != current)
        {
                /* Literal, then repeat 0. */
                p8 address_to back = buf - reps[0] - 1;
                p32 limit = buf_avail_full < nice + 1 ? buf_avail_full : nice + 1;
                p32 len_test = xz_common(buf, back, 1, limit) - 1;

                if (len_test >= 2)
                {
                        p8 state_2 = xz_after_literal(state);
                        p32 ps_next = (position + 1) & e->pos_mask;
                        p32 next_rep_match_price = cur_and_1_price +
                                xz_price1(e, m->is_match[state_2][ps_next]) +
                                xz_price1(e, m->is_rep[state_2]);
                        p32 offset = cur + 1 + len_test;

                        while (len_end < offset)
                                opts[++len_end].price = XZ_INFINITY_PRICE;
                        p32 cost = next_rep_match_price +
                                   xz_rep_price(e, 0, len_test, state_2, ps_next);
                        if (cost < opts[offset].price)
                        {
                                opts[offset].price = cost;
                                opts[offset].pos_prev = cur + 1;
                                opts[offset].back_prev = 0;
                                opts[offset].prev_1_is_literal = true;
                                opts[offset].prev_2 = false;
                        }
                }
        }

        p32 start_len = 2;

        for (p32 rep_index = 0; rep_index < 4; rep_index++)
        {
                p8 address_to back = buf - reps[rep_index] - 1;

                if (xz_differ16(buf, back))
                        continue;
                p32 len_test = xz_common(buf, back, 2, buf_avail);

                while (len_end < cur + len_test)
                        opts[++len_end].price = XZ_INFINITY_PRICE;

                p32 len_test_temp = len_test;
                p32 price = rep_match_price + xz_pure_rep_price(e, rep_index, state, ps);

                do
                {
                        p32 cost = price + e->rep_prices.prices[ps][len_test - 2];

                        if (cost < opts[cur + len_test].price)
                        {
                                opts[cur + len_test].price = cost;
                                opts[cur + len_test].pos_prev = cur;
                                opts[cur + len_test].back_prev = rep_index;
                                opts[cur + len_test].prev_1_is_literal = false;
                        }
                } while (--len_test >= 2);
                len_test = len_test_temp;
                if (!rep_index)
                        start_len = len_test + 1;

                /* Repeat, literal, repeat 0. */
                p32 len_test_2 = len_test + 1;
                p32 limit = buf_avail_full < len_test_2 + nice ? buf_avail_full
                                                               : len_test_2 + nice;
                if (len_test_2 < limit)
                        len_test_2 = xz_common(buf, back, len_test_2, limit);
                len_test_2 -= len_test + 1;
                if (len_test_2 >= 2)
                {
                        p8 state_2 = xz_after_long_rep(state);
                        p32 ps_next = (position + len_test) & e->pos_mask;
                        p32 cost_literal = price + e->rep_prices.prices[ps][len_test - 2] +
                                xz_price0(e, m->is_match[state_2][ps_next]) +
                                xz_literal_price(e, position + len_test, buf[len_test - 1],
                                                 true, back[len_test], buf[len_test]);

                        state_2 = xz_after_literal(state_2);
                        ps_next = (position + len_test + 1) & e->pos_mask;
                        p32 next_rep_match_price = cost_literal +
                                xz_price1(e, m->is_match[state_2][ps_next]) +
                                xz_price1(e, m->is_rep[state_2]);
                        p32 offset = cur + len_test + 1 + len_test_2;

                        while (len_end < offset)
                                opts[++len_end].price = XZ_INFINITY_PRICE;
                        p32 cost = next_rep_match_price +
                                   xz_rep_price(e, 0, len_test_2, state_2, ps_next);
                        if (cost < opts[offset].price)
                        {
                                opts[offset].price = cost;
                                opts[offset].pos_prev = cur + len_test + 1;
                                opts[offset].back_prev = 0;
                                opts[offset].prev_1_is_literal = true;
                                opts[offset].prev_2 = true;
                                opts[offset].pos_prev_2 = cur;
                                opts[offset].back_prev_2 = rep_index;
                        }
                }
        }

        if (new_len > buf_avail)
        {
                new_len = buf_avail;
                count = 0;
                while (new_len > e->matches[count].len)
                        count++;
                e->matches[count++].len = new_len;
        }
        if (new_len < start_len)
                return len_end;

        p32 normal_match_price = match_price + xz_price0(e, m->is_rep[state]);

        while (len_end < cur + new_len)
                opts[++len_end].price = XZ_INFINITY_PRICE;

        p32 i = 0;

        while (start_len > e->matches[i].len)
                i++;
        for (p32 len_test = start_len;; len_test++)
        {
                p32 cur_back = e->matches[i].dist;
                p32 cost = normal_match_price + xz_dist_len_price(e, cur_back, len_test, ps);

                if (cost < opts[cur + len_test].price)
                {
                        opts[cur + len_test].price = cost;
                        opts[cur + len_test].pos_prev = cur;
                        opts[cur + len_test].back_prev = cur_back + 4;
                        opts[cur + len_test].prev_1_is_literal = false;
                }
                if (len_test != e->matches[i].len)
                        continue;

                /* Match, literal, repeat 0. */
                p8 address_to back = buf - cur_back - 1;
                p32 len_test_2 = len_test + 1;
                p32 limit = buf_avail_full < len_test_2 + nice ? buf_avail_full
                                                               : len_test_2 + nice;

                if (len_test_2 < limit)
                        len_test_2 = xz_common(buf, back, len_test_2, limit);
                len_test_2 -= len_test + 1;
                if (len_test_2 >= 2)
                {
                        p8 state_2 = xz_after_match(state);
                        p32 ps_next = (position + len_test) & e->pos_mask;
                        p32 cost_literal = cost +
                                xz_price0(e, m->is_match[state_2][ps_next]) +
                                xz_literal_price(e, position + len_test, buf[len_test - 1],
                                                 true, back[len_test], buf[len_test]);

                        state_2 = xz_after_literal(state_2);
                        ps_next = (ps_next + 1) & e->pos_mask;
                        p32 next_rep_match_price = cost_literal +
                                xz_price1(e, m->is_match[state_2][ps_next]) +
                                xz_price1(e, m->is_rep[state_2]);
                        p32 offset = cur + len_test + 1 + len_test_2;

                        while (len_end < offset)
                                opts[++len_end].price = XZ_INFINITY_PRICE;
                        p32 cost_2 = next_rep_match_price +
                                     xz_rep_price(e, 0, len_test_2, state_2, ps_next);
                        if (cost_2 < opts[offset].price)
                        {
                                opts[offset].price = cost_2;
                                opts[offset].pos_prev = cur + len_test + 1;
                                opts[offset].back_prev = 0;
                                opts[offset].prev_1_is_literal = true;
                                opts[offset].prev_2 = true;
                                opts[offset].pos_prev_2 = cur;
                                opts[offset].back_prev_2 = cur_back + 4;
                        }
                }
                if (++i == count)
                        break;
        }
        return len_end;
}

/* Levels 4-9: price every way to cover the next positions, up to nice
   length or 4096 positions, and code the cheapest path. */
static fn xz_optimum_normal(xz_encoder address_to e, p32 address_to back_res,
                            p32 address_to len_res, p32 position)
{
        xz_optimal address_to opts = e->opts;

        if (e->opts_end != e->opts_current)
        {
                p32 at = e->opts_current;

                address_to len_res = opts[at].pos_prev - at;
                address_to back_res = opts[at].back_prev;
                e->opts_current = opts[at].pos_prev;
                return;
        }
        if (!e->read_ahead)
        {
                if (e->match_price_count >= (1u << 7))
                        xz_fill_dist_prices(e);
                if (e->align_price_count >= XZ_ALIGN)
                        xz_fill_align_prices(e);
        }

        p32 len_end = xz_optimum_first(e, back_res, len_res, position);

        if (len_end == XZ_LITERAL)
                return;

        p32 reps[4] = {e->reps[0], e->reps[1], e->reps[2], e->reps[3]};
        p32 cur;

        for (cur = 1; cur < len_end; cur++)
        {
                e->longest = xz_find(e, address_of e->match_count);
                if (e->longest >= e->nice)
                        break;
                p32 avail = e->input_n - e->read_pos + 1;
                if (avail > XZ_OPTS - 1 - cur)
                        avail = XZ_OPTS - 1 - cur;
                len_end = xz_optimum_next(e, reps, e->input + e->read_pos - 1, len_end,
                                          position + cur, cur, e->nice, avail);
        }
        xz_backward(e, len_res, back_res, cur);
}

static fn xz_lzma_reset(xz_encoder address_to e)
{
        p16 address_to cell = (p16 address_to)address_of e->models;

        for (positive i = 0; i < sizeof(e->models) / sizeof(p16); i++)
                cell[i] = 1024;
        e->state = 0;
        e->reps[0] = e->reps[1] = e->reps[2] = e->reps[3] = 0;
        e->match_price_count = 0x7fffffffu;
        e->align_price_count = 0x7fffffffu;
        e->opts_end = 0;
        e->opts_current = 0;
        if (e->preset.normal)
                for (p32 ps = 0; ps <= e->pos_mask; ps++)
                {
                        xz_length_prices(e, false, ps);
                        xz_length_prices(e, true, ps);
                }
}

/* Size the dictionary to the block, clear the match finder, and make sure
   the output span holds the block's worst case. */
static bool xz_block_prepare(xz_encoder address_to e, p32 n)
{
        const xz_preset address_to p = address_of e->preset;
        p32 dict = p->dict;
        bool fresh;

        while (dict > 4096 && dict / 2 >= n)
                dict >>= 1;
        e->dict = dict;
        e->cyclic_size = dict + 1;
        e->cyclic_pos = 0;
        e->read_pos = 0;
        e->read_ahead = 0;
        e->offset = e->cyclic_size;

        p32 hs = dict - 1;

        hs |= hs >> 1;
        hs |= hs >> 2;
        hs |= hs >> 4;
        hs |= hs >> 8;
        hs |= hs >> 16;
        hs >>= 1;
        hs |= 0xffff;
        if (hs > (1u << 24))
                hs = p->finder == XZ_FINDER_HC3 ? (1u << 24) - 1 : hs >> 1;
        e->hash_mask = hs;

        positive entries = (positive)hs + 1 + XZ_HASH2_SIZE +
                           (p->finder == XZ_FINDER_HC3 ? 0 : XZ_HASH3_SIZE);
        positive sons = (positive)e->cyclic_size * (p->finder == XZ_FINDER_BT4 ? 2 : 1);
        p8 address_to area = (p8 address_to)e->hash;

        if (!xz_area(address_of area, address_of e->hash_room, entries * sizeof(p32),
                     address_of fresh))
                return false;
        e->hash = (p32 address_to)area;
        if (!fresh)
                memory_fill(e->hash, 0, entries * sizeof(p32));
        area = (p8 address_to)e->son;
        if (!xz_area(address_of area, address_of e->son_room, sons * sizeof(p32),
                     address_of fresh))
                return false;
        e->son = (p32 address_to)area;
        if (!xz_area(address_of e->out, address_of e->out_room,
                     XZ_BLOCK_HEADER_MAX + (positive)n + (n >> 12) +
                             2 * XZ_CHUNK_PACKED_MAX + 64,
                     address_of fresh))
                return false;

        e->nice = p->nice;
        e->depth = p->depth ? p->depth
                 : p->finder == XZ_FINDER_BT4 ? 16 + e->nice / 2 : 4 + e->nice / 4;
        //      Two slots a bit of the dictionary's size rounded up to a power
        //      of two, as liblzma counts them: a dictionary that is not one
        //      (3 MiB, 96 MiB) has distances in the slot the floor left out,
        //      and priced at nothing they were never chosen.
        e->dist_table_size = 2 * (top_bit_known(dict - 1) + 1);
        e->len_table_size = e->nice + 1 - 2;
        e->fin = (xz_finder){e->preset, e->input, e->input_n, 0, e->offset, e->hash, e->hash_mask,
                             e->son, 0, e->cyclic_size, e->nice, e->depth, e->matches};
        return true;
}

/*
        The x86 converter, tried per block under -e. It takes most of a
        block's executable code down by a fraction and costs text and data a
        little, and a tar of a system holds both, so the answer is the
        block's own: 64 KiB windows from all over it, up to 2 MiB, go through
        a fast encoder as they are and converted, and the converter is used
        when it saves most of a percent. The choice is the block's alone, so
        the bytes still depend on the input and not on the width; any xz
        reads the result (the converter is in every liblzma since 5.0).
        Whole tars at -6, silesia -0.42% and Arch's rootfs -0.40%, which
        the trial finds; a block without code finds nothing and pays a
        few percent of its time for the asking.
*/
static bool xz_block_encode(xz_encoder address_to e, p8 address_to input, p32 n);

#define XZ_PICK_WINDOW ((p32)1 << 16)
#define XZ_PICK_WINDOWS 32

static bool xz_pick_x86(xz_encoder address_to e, p8 address_to input, p32 n)
{
        if (n < 4 * XZ_PICK_WINDOW)
                return false;

        positive windows = n / XZ_PICK_WINDOW < XZ_PICK_WINDOWS ? n / XZ_PICK_WINDOW : XZ_PICK_WINDOWS;
        positive total = windows * XZ_PICK_WINDOW;
        p8 address_to plain = (p8 address_to)memory_checked(2 * (total + XZ_SLACK));
        bool win = false;

        if (!plain)
                return false;

        p8 address_to bent = plain + total + XZ_SLACK;

        for (positive i = 0; i < windows; i++)
                memory_copy_apart(plain + i * XZ_PICK_WINDOW,
                                  input + (positive)((p64)(n - XZ_PICK_WINDOW) * i / (windows - 1)),
                                  XZ_PICK_WINDOW);
        memory_copy_apart(bent, plain, total);

        xz_filter list[1];

        memory_fill(list, 0, sizeof(list));
        list[0].id = XZ_FILTER_X86;
        xz_filter_block(list, 1, true, bent, total);
        if (!e->trial)
        {
                xz_options o;

                memory_fill(address_of o, 0, sizeof(o));
                o.lz = xz_preset_of(1);
                e->trial = xz_encoder_open_with(address_of o);
                if (e->trial)
                        ((xz_encoder address_to)e->trial)->check = XZ_CHECK_NONE;
        }
        if (e->trial)
        {
                xz_encoder address_to t = (xz_encoder address_to)e->trial;

                if (xz_block_encode(t, plain, (p32)total))
                {
                        p64 as_is = t->out_n;

                        if (xz_block_encode(t, bent, (p32)total))
                                win = (p64)t->out_n * 1000 < as_is * 992;
                }
        }
        memory_free(plain, 2 * (total + XZ_SLACK));
        return win;
}

/* Encode input[0, n) as one complete block: header with both sizes, LZMA2
   chunks and end marker, padding, then the check e->check names. The input must stay readable for
   XZ_SLACK bytes past n; what those bytes hold never changes the output. */
static bool xz_block_encode(xz_encoder address_to e, p8 address_to input, p32 n)
{
        xz_probability_state address_to m = address_of e->models;
        p8 address_to original = input;

        if (e->pick)
        {
                e->nfilt = xz_pick_x86(e, input, n) ? 1 : 0;
                if (e->nfilt)
                {
                        memory_fill(e->filt, 0, sizeof(e->filt[0]));
                        e->filt[0].id = XZ_FILTER_X86;
                }
        }

        //      With filters the block is coded from a filtered copy, and the
        //      check is of the block as it was.
        if (e->nfilt && n)
        {
                bool fresh;

                if (!xz_area(address_of e->fbuf, address_of e->fbuf_room,
                             (positive)n + XZ_SLACK, address_of fresh))
                        return false;
                memory_copy_apart(e->fbuf, input, n);
                memory_fill(e->fbuf + n, 0, XZ_SLACK);
                xz_filter_block(e->filt, e->nfilt, true, e->fbuf, n);
                input = e->fbuf;
        }
        e->input = input;
        e->input_n = n;
        if (!n || !xz_block_prepare(e, n))
                return false;
        xz_lzma_reset(e);
        xz_pipe_start(e);

        p8 address_to out = e->out + XZ_BLOCK_HEADER_MAX;
        positive at = 0;
        bool props = true;
        bool dict_reset = true;
        bool state_reset = false;
        bool started = false;

        e->position = 0;
        for (;;)
        {
                p32 start = e->read_pos - e->read_ahead;

                if (start >= n)
                        break;
                if (state_reset)
                        xz_lzma_reset(e);
                e->rc = (xz_range_state){0xffffffffu, 0, 0, 1, e->chunk,
                                         e->chunk + sizeof(e->chunk), 0};
                if (!started)
                {
                        xz_skip(e, 1);
                        e->read_ahead = 0;
                        xz_bit(e, address_of m->is_match[0][0], 0);
                        lzma_range_encode(address_of e->rc, m->lit, input[0], 8);
                        e->position = 1;
                        started = true;
                }

                p32 limit = start + XZ_CHUNK_PLAIN_MAX - XZ_MATCH_MAX;

                for (;;)
                {
                        if (e->read_pos - e->read_ahead >= limit ||
                            (positive)(e->rc.next - e->chunk) + e->rc.pending + 4 >=
                                    XZ_CHUNK_PACKED_MAX - XZ_LOOP_INPUT)
                                break;
                        if (e->read_pos >= n && !e->read_ahead)
                                break;

                        p32 back;
                        p32 len;

                        if (e->preset.normal)
                                xz_optimum_normal(e, address_of back, address_of len,
                                                  (p32)e->position);
                        else
                                xz_optimum_fast(e, address_of back, address_of len);
                        xz_code_symbol(e, back, len);
                }
                for (p32 i = 0; i < 5; i++)
                        lzma_range_shift(address_of e->rc);
                if (e->rc.full)
                {
                        xz_pipe_stop(e);
                        return false;
                }

                positive packed = (positive)(e->rc.next - e->chunk);
                positive plain = e->read_pos - e->read_ahead - start;

                if (packed >= plain)
                {
                        /* Stored: the models coded speculatively are dropped,
                           so the next compressed chunk resets its state. */
                        plain += e->read_ahead;
                        e->read_ahead = 0;
                        for (positive from = 0; from < plain;)
                        {
                                positive take = plain - from;

                                if (take > XZ_CHUNK_PACKED_MAX)
                                        take = XZ_CHUNK_PACKED_MAX;
                                out[at++] = dict_reset ? 1 : 2;
                                network_store_16(out + at, (p16)(take - 1));
                                at += 2;
                                memory_copy_apart(out + at, input + start + from, take);
                                at += take;
                                from += take;
                                dict_reset = false;
                        }
                        state_reset = true;
                        continue;
                }
                out[at++] = (p8)((props ? (dict_reset ? 0xe0 : 0xc0)
                                        : state_reset ? 0xa0 : 0x80) |
                                 ((plain - 1) >> 16));
                network_store_16(out + at, (p16)(plain - 1));
                network_store_16(out + at + 2, (p16)(packed - 1));
                at += 4;
                if (props)
                        out[at++] = (p8)((e->pb * 5 + e->lp) * 9 + e->lc);
                memory_copy_apart(out + at, e->chunk, packed);
                at += packed;
                props = dict_reset = state_reset = false;
        }
        out[at++] = 0;
        xz_pipe_stop(e);

        p8 header[XZ_BLOCK_HEADER_MAX];
        positive h = 2;

        header[1] = (p8)(0x40 | 0x80 | e->nfilt);
        h += memory_vli_put(header + h, at);
        h += memory_vli_put(header + h, n);
        for (positive i = 0; i < e->nfilt; i++)
        {
                const xz_filter address_to f = e->filt + i;

                header[h++] = f->id;
                if (f->id == XZ_FILTER_DELTA)
                {
                        header[h++] = 1;
                        header[h++] = f->dist;
                }
                else if (f->start)
                {
                        header[h++] = 4;
                        memory_store_unaligned(p32, header + h, f->start);
                        h += 4;
                }
                else
                        header[h++] = 0;
        }
        header[h++] = 0x21;
        header[h++] = 1;
        //      The header names the dictionary asked for, as xz does, though
        //      a short block's match finder needs no more than the block.
        header[h++] = xz_prop_from_dict(e->preset.dict);
        positive header_padding = (0 - (h + 4)) & 3;
        memory_zero(header + h, header_padding);
        h += header_padding;
        header[0] = (p8)((h + 4) / 4 - 1);
        memory_store_unaligned(p32, header + h, ~hash_crc32(0xffffffffu, header, h));
        h += 4;
        memory_copy_apart(e->out + XZ_BLOCK_HEADER_MAX - h, header, h);

        positive sum = (positive)xz_check_size(e->check);

        e->unpadded = h + at + sum;
        positive data_padding = (0 - at) & 3;
        memory_zero(out + at, data_padding);
        at += data_padding;
        if (e->check == XZ_CHECK_CRC32)
                memory_store_unaligned(p32, out + at, ~hash_crc32(0xffffffffu, original, n));
        else if (e->check == XZ_CHECK_CRC64)
                memory_store_unaligned(p64, out + at, ~hash_crc64(~(p64)0, original, n));
        else if (e->check == XZ_CHECK_SHA256)
        {
                digest_state digest;

                digest_open(address_of digest, DIGEST_SHA256, 32);
                digest_write(address_of digest, original, n);
                digest_close(address_of digest, out + at);
        }
        e->out_at = XZ_BLOCK_HEADER_MAX - h;
        e->out_n = h + at + sum;
        return true;
}

/*
        The stream around the blocks: header, blocks, index and footer.
        Input waits in batches of whole blocks. Each block is one job that
        encodes into its worker's xz_encoder; the sink writes the jobs'
        bytes and index records in block order on the calling thread. A
        batch only decides how much input waits in memory, never where a
        block starts, so the bytes are the same for any batch or worker count.
*/
#define XZ_BATCH_BYTES ((positive)1 << 30)
#define XZ_BATCH_BLOCKS 64

typedef struct
{
        xz_options options;
        positive block;
        positive batch_blocks;
        p8 address_to input;
        positive input_room;
        positive input_n;
        xz_encoder address_to address_to slots;
        positive slot_count;
        p64 address_to unpadded;
        p8 address_to index;
        positive index_room;
        positive index_n;
        p64 records;
        p8 check;
        byte_store address_to store;
        bipolar fd;
        bool failed;
} xz_stream_writer;

static xz_stream_writer xz_writer;

/* The check every block of the next stream carries: CRC64, as xz writes
   by default, unless the command line's -C names another. */
static p8 xz_encode_check = XZ_CHECK_CRC64;

/* The options of the next stream: a preset's, unless the command line
   built its own chain (xz_encode_custom), and the block size --block-size
   asked for, 0 for three dictionaries and at least 1 MiB. */
static xz_options xz_encode_options;
static bool xz_encode_custom;
static positive xz_block_size;
#define XZ_BLOCK_MAX ((positive)1 << 30)

/* -e on the command line: the extreme form of the preset. */
static bool xz_cli_extreme;

/* 1 keeps encoding on the calling thread; set by the command line only. */
static bool xz_serial;

/* -T N / --threads=N as xz spells them: 0 is every CPU the process may run
   on, 1 keeps the codec on the calling thread. Only whether work may spread
   is taken from it; the bytes never depend on it. */
static positive xz_threads;

static bool xz_writer_emit(p8 address_to bytes, positive n)
{
        if (xz_writer.failed)
                return false;
        if (xz_writer.store)
        {
                if (!byte_store_append_exact(xz_writer.store, bytes, n))
                {
                        xz_writer.failed = true;
                        return xz_fail("xz output is too small");
                }
        }
        else if (xz_writer.fd >= 0 &&
                 system_write_all((positive)xz_writer.fd, bytes, n) != (bipolar)n)
        {
                xz_writer.failed = true;
                return xz_fail("xz write failed");
        }
        return true;
}

static fn xz_writer_close(void)
{
        for (positive i = 0; i < xz_writer.slot_count; i++)
                xz_encoder_close(xz_writer.slots[i]);
        memory_free(xz_writer.slots, xz_writer.slot_count * sizeof(xz_encoder address_to));
        memory_free(xz_writer.unpadded, xz_writer.batch_blocks * sizeof(p64));
        memory_free(xz_writer.input, xz_writer.input_room);
        memory_free(xz_writer.index, xz_writer.index_room);
        xz_writer.slots = null;
        xz_writer.slot_count = 0;
        xz_writer.unpadded = null;
        xz_writer.input = null;
        xz_writer.input_room = 0;
        xz_writer.index = null;
        xz_writer.index_room = 0;
}

static bool xz_writer_record(p64 unpadded, p64 uncompressed)
{
        if (xz_writer.index_n + 20 > xz_writer.index_room)
        {
                positive room = xz_writer.index_room ? 2 * xz_writer.index_room : 4096;
                p8 address_to grown = (p8 address_to)memory_checked(room);

                if (!grown)
                        return xz_fail("xz cannot map the index");
                if (xz_writer.index)
                {
                        memory_copy_apart(grown, xz_writer.index, xz_writer.index_n);
                        memory_free(xz_writer.index, xz_writer.index_room);
                }
                xz_writer.index = grown;
                xz_writer.index_room = room;
        }
        xz_writer.index_n += memory_vli_put(xz_writer.index + xz_writer.index_n, unpadded);
        xz_writer.index_n += memory_vli_put(xz_writer.index + xz_writer.index_n, uncompressed);
        xz_writer.records++;
        return true;
}

static positive xz_batch_bytes(xz_stream_writer address_to w, positive index)
{
        positive from = index * w->block;

        return w->input_n - from < w->block ? w->input_n - from : w->block;
}

/* One block of the batch, on any thread: it touches only its worker's
   encoder, its own unpadded slot and its own output. A block that cannot be
   encoded leaves unpadded zero for the sink to report at its place. */
static fn xz_batch_job(address_any context, positive index,
                       parallel_output address_to output)
{
        xz_stream_writer address_to w = (xz_stream_writer address_to)context;
        positive slot = parallel_slot();
        xz_encoder address_to e = w->slots[slot];

        w->unpadded[index] = 0;
        if (!e)
        {
                e = xz_encoder_open_with(address_of w->options);
                if (!e)
                        return;
                w->slots[slot] = e;
        }
        e->check = w->check;
        if (xz_block_encode(e, w->input + index * w->block, (p32)xz_batch_bytes(w, index)) &&
            parallel_write(output, e->out + e->out_at, e->out_n))
                w->unpadded[index] = e->unpadded;
}

/* A block's bytes and index record, in block order, on the calling thread. */
static bool xz_batch_sink(address_any context, positive index, address_any data,
                          positive length)
{
        xz_stream_writer address_to w = (xz_stream_writer address_to)context;

        if (!w->unpadded[index])
                return xz_fail("xz cannot encode a block");
        return xz_writer_emit((p8 address_to)data, length) &&
               xz_writer_record(w->unpadded[index], xz_batch_bytes(w, index));
}

/* Every block of the batch: encoded side by side, written in block order.
   Encoding is heavy per byte, so the pool spreads even a small batch. */
static bool xz_writer_batch(void)
{
        xz_stream_writer address_to w = address_of xz_writer;
        positive count = (w->input_n + w->block - 1) / w->block;

        if (!count)
                return !w->failed;
        if (count == 1 && !xz_serial && parallel_width() > 1)
        {
                //      One block, and CPUs to spare: run it here, not as a
                //      pool job, so that its match finder can have the pool's
                //      beside thread. What is written is the same.
                xz_encoder address_to e = w->slots[0];

                if (!e)
                {
                        e = xz_encoder_open_with(address_of w->options);
                        if (!e)
                                return xz_fail("xz cannot map an encoder");
                        w->slots[0] = e;
                }
                e->check = w->check;
                e->pipe_ok = true;

                bool ok = xz_block_encode(e, w->input, (p32)xz_batch_bytes(w, 0));

                e->pipe_ok = false;
                if (!ok)
                        return xz_fail("xz cannot encode a block");
                w->unpadded[0] = e->unpadded;
                if (!xz_writer_emit(e->out + e->out_at, e->out_n) ||
                    !xz_writer_record(w->unpadded[0], xz_batch_bytes(w, 0)))
                        return false;
                w->input_n = 0;
                return !w->failed;
        }
        if (!parallel_ordered(xz_batch_job, xz_batch_sink, w, count,
                              xz_serial ? 0 : PARALLEL_SPREAD))
                return w->failed || xz_why ? false : xz_fail("xz cannot encode a block");
        w->input_n = 0;
        return !w->failed;
}

/* The input one batch may hold: at most an eighth of physical memory and
   XZ_BATCH_BYTES, but always one block. Linux's sysinfo keeps totalram at
   byte 32 and mem_unit at byte 104. This decides only how much waits in
   memory, never a block boundary. */
static positive xz_batch_room(positive block)
{
        p64 info[14];
        positive room = XZ_BATCH_BYTES;

        memory_fill(info, 0, sizeof(info));
        if (system_call_1(syscall(sysinfo), (positive)info) == 0)
        {
                positive unit = (p32)info[13] ? (p32)info[13] : 1;
                positive eighth = info[4] / 8 * unit;

                if (eighth && eighth < room)
                        room = eighth;
        }
        return room < block ? block : room;
}

static bool xz_encode_setup(p8 level)
{
        if (!xz_encode_custom)
        {
                memory_fill(address_of xz_encode_options, 0, sizeof(xz_encode_options));
                xz_encode_options.lz = xz_preset_of(level);
                xz_encode_options.pick = (level & XZ_EXTREME) != 0;
        }

        positive dict = xz_encode_options.lz.dict;
        p8 header[12] = {0xfd, 0x37, 0x7a, 0x58, 0x5a, 0, 0, xz_encode_check};

        xz_writer_close();
        xz_why = null;
        xz_writer.options = xz_encode_options;
        xz_writer.check = xz_encode_check;
        //      A block starts with an empty dictionary and untrained models,
        //      so a small one costs ratio: xz's three dictionaries and 1 MiB
        //      lose 0.7% at -0 and -1 on silesia against one block. The fast
        //      modes run at tens of MB/s, so a block of four dictionaries and
        //      at least 8 MiB keeps the loss to 0.1-0.3% and still gives a
        //      pool something to spread once the input is a few blocks long.
        xz_writer.block = xz_block_size ? xz_block_size
                        : !xz_encode_options.lz.normal
                              ? (4 * dict > ((positive)8 << 20) ? 4 * dict : (positive)8 << 20)
                        : 3 * dict > ((positive)1 << 20) ? 3 * dict : (positive)1 << 20;
        //      A block's positions are 32 bits with the dictionary added.
        if (xz_writer.block > XZ_BLOCK_MAX)
                xz_writer.block = XZ_BLOCK_MAX;
        xz_writer.batch_blocks = xz_batch_room(xz_writer.block) / xz_writer.block;
        if (xz_writer.batch_blocks > XZ_BATCH_BLOCKS)
                xz_writer.batch_blocks = XZ_BATCH_BLOCKS;
        //      One worker encodes one block at a time whatever waits behind
        //      it, and a single-core -6 held three 24 MiB blocks of input
        //      for nothing.
        if (!xz_writer.batch_blocks || xz_serial || parallel_width() == 1)
                xz_writer.batch_blocks = 1;
        xz_writer.input_n = 0;
        xz_writer.index_n = 0;
        xz_writer.records = 0;
        xz_writer.failed = false;
        xz_writer.store = xz_output.bytes ? address_of xz_output : null;
        xz_writer.fd = xz_out_fd;
        xz_writer.slot_count = parallel_slots();
        xz_writer.slots = (xz_encoder address_to address_to)memory_checked(
            xz_writer.slot_count * sizeof(xz_encoder address_to));
        xz_writer.unpadded =
            (p64 address_to)memory_checked(xz_writer.batch_blocks * sizeof(p64));
        xz_writer.input_room = xz_writer.batch_blocks * xz_writer.block + XZ_SLACK;
        xz_writer.input = (p8 address_to)memory_checked(xz_writer.input_room);
        if (!xz_writer.slots || !xz_writer.unpadded || !xz_writer.input)
                return xz_fail("xz cannot map the block input");
        memory_store_unaligned(p32, header + 8, ~hash_crc32(0xffffffffu, header + 6, 2));
        return xz_writer_emit(header, sizeof(header));
}

static bool xz_encode_write(p8 address_to src, positive n)
{
        positive capacity = xz_writer.batch_blocks * xz_writer.block;

        while (n)
        {
                positive take = min(n, capacity - xz_writer.input_n);

                memory_copy(xz_writer.input + xz_writer.input_n, src, take);
                xz_writer.input_n += take;
                src += take;
                n -= take;
                if (xz_writer.input_n == capacity && !xz_writer_batch())
                        return false;
        }
        return !xz_writer.failed;
}

static bool xz_encode_end(void)
{
        bool ok = xz_writer_batch();

        if (ok)
        {
                p8 head[16];
                p8 tail[8];
                p8 footer[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, xz_writer.check, 'Y', 'Z'};
                positive h = 1;
                positive size;
                p32 crc;

                head[0] = 0;
                h += memory_vli_put(head + h, xz_writer.records);
                size = h + xz_writer.index_n;
                crc = hash_crc32(0xffffffffu, head, h);
                crc = hash_crc32(crc, xz_writer.index, xz_writer.index_n);
                positive pad = (4 - (size & 3)) & 3;
                memory_fill(tail, 0, sizeof(tail));
                crc = hash_crc32(crc, tail, pad);
                memory_store_unaligned(p32, tail + pad, ~crc);
                size += pad + 4;
                memory_store_unaligned(p32, footer + 4, size / 4 - 1);
                memory_store_unaligned(p32, footer, ~hash_crc32(0xffffffffu, footer + 4, 6));
                ok = xz_writer_emit(head, h) &&
                     xz_writer_emit(xz_writer.index, xz_writer.index_n) &&
                     xz_writer_emit(tail, pad + 4) &&
                     xz_writer_emit(footer, sizeof(footer));
        }
        xz_writer_close();
        return ok;
}

static bool xz_stream_encode(p8 level)
{
        if (!xz_encode_setup(level))
        {
                xz_writer_close();
                return false;
        }

        positive capacity = xz_writer.batch_blocks * xz_writer.block;

        for (;;)
        {
                if (xz_writer.input_n == capacity && !xz_writer_batch())
                        break;
                bipolar got = system_read_retry((positive)xz_input.fd,
                                                xz_writer.input + xz_writer.input_n,
                                                capacity - xz_writer.input_n);
                if (got < 0)
                {
                        xz_fail("xz read failed");
                        break;
                }
                if (!got)
                        return xz_encode_end();
                xz_writer.input_n += (positive)got;
        }
        xz_writer_close();
        return false;
}

static bipolar xz_deflate_mem(p8 address_to src, positive src_len,
                              p8 address_to dst, positive dst_cap, p8 level)
{
        bool ok;

        xz_input.fd = -1;
        xz_out_fd = -1;
        xz_output.bytes = dst;
        xz_output.room = dst_cap;
        xz_output.used = 0;
        ok = xz_encode_setup(level) && xz_encode_write(src, src_len) && xz_encode_end();
        if (!ok)
                xz_writer_close();
        xz_output.bytes = null;
        return ok ? (bipolar)xz_output.used : -1;
}

/*
        tar's codec table reads through one live decoder between begin and
        end, on whichever thread calls read; the error it reports is that
        decoder's, mirrored into xz_why.
*/
static address_any xz_reader;

static bool xz_decode_begin_prefix(bipolar in, p8 address_to prefix, positive n)
{
        if (xz_reader)
                xz_pull_close(xz_reader);
        xz_reader = xz_pull_open(in, prefix, n);
        xz_why = xz_pull_error(xz_reader);
        return xz_why == null;
}

static bipolar xz_decode_read(p8 address_to dst, positive n)
{
        bipolar got = xz_reader ? xz_pull_read(xz_reader, dst, n) : -1;

        if (got < 0)
                xz_why = xz_pull_error(xz_reader);
        return got;
}

static bool xz_decode_end(void)
{
        if (xz_reader)
        {
                if (!xz_why)
                        xz_why = xz_pull_error(xz_reader);
                xz_pull_close(xz_reader);
                xz_reader = null;
        }
        return xz_why == null;
}

static bool xz_encode_begin(bipolar out, p8 level)
{
        xz_out_fd = out;
        xz_output.bytes = null;
        xz_input.fd = -1;
        return xz_encode_setup(level);
}

#ifndef XZ_CORE_ONLY

/*
        Multi-block decode.

        xz -T and this encoder write both sizes into every block header, so a
        block can be read whole without decoding it. While blocks carry sizes,
        the calling thread reads them exactly, a batch at a time, and each is
        one job that its worker's decoder turns into exactly its uncompressed
        size; the sink writes the blocks in order and the index is checked
        against the blocks seen. The first block without sizes hands the rest
        of the input to the serial decoder, whose prefix is only the bytes
        read for that block header (and its stream header), so it never needs
        a byte read ahead. A batch holds at most an eighth of physical memory
        of output, never fewer than two blocks, so what waits in the pool is
        bounded by the data and not by the width.

        Limit, measured on arch.tar.xz (23 blocks) at 16 CPUs: 0.30 s into
        /dev/null but 0.59 s into a file, against GNU's 0.35 s. The pool's
        caller runs the sink and also claims jobs, so finished blocks wait
        for it and the page-cache writes queue after the decoding instead of
        overlapping it; the pool fix needs no change here.
*/

#define XZ_PAR_BLOCKS_MAX 256
#define XZ_PAR_HEADER_MAX 1024

typedef struct
{
        positive at;            /* offset of the block header in the batch buffer */
        positive total;         /* header + compressed + padding + check */
        p64 unpadded;           /* header + compressed + check, for the index */
        p64 uncompressed;
        string_address why;     /* the job's verdict, reported by the sink */
        positive written;       /* bytes decoded before a failure, all on success */
} xz_par_block;

typedef struct
{
        bipolar in;
        bipolar out;
        p8 check;
        positive check_size;
        /* the batch: compressed blocks read exactly */
        p8 address_to bytes;
        positive room;
        positive used;
        xz_par_block blocks[XZ_PAR_BLOCKS_MAX];
        positive count;
        /* the index to verify, whole stream */
        p8 address_to records;
        positive records_room;
        positive records_used;
        p64 record_count;
        /* per-slot decoders */
        address_any address_to slots;
        positive slot_count;
        /* what the serial fallback needs: bytes consumed for the pending headers */
        p8 prefix[12 + XZ_PAR_HEADER_MAX];
        positive prefix_n;
        bool in_stream;
        bool failed;
        string_address why;
        p64 in_abs;
        positive partial;       /* bytes a short read delivered */
} xz_par;

static bool xz_par_fail(xz_par address_to r, string_address why)
{
        if (!r->why)
                r->why = why;
        r->failed = true;
        return false;
}

/* Exactly n bytes or a clean end: 1 read, 0 end before the first byte,
   -1 truncated or failed. */
static bipolar xz_par_read(xz_par address_to r, p8 address_to into, positive n)
{
        positive got = 0;

        while (got < n)
        {
                bipolar k = system_read_retry((positive)r->in, into + got, n - got);

                if (k < 0)
                        return xz_par_fail(r, "xz read failed"), -1;
                if (!k)
                {
                        r->partial = got;
                        return got ? (xz_par_fail(r, "xz truncated input"), -1) : 0;
                }
                got += (positive)k;
        }
        r->in_abs += n;
        return 1;
}

static bool xz_par_grow(p8 address_to address_to area, positive address_to room,
                        positive used, positive need)
{
        if (address_to room >= need)
                return true;

        // Doubling from a megabyte, and no answer once doubling would wrap:
        // a block header may claim a compressed size near 2^63, and the
        // loop this was spun forever when grown wrapped to nought.
        positive grown = memory_growth(address_to room, need, (positive)1 << 20);

        if (!grown)
                return false;

        p8 address_to bytes = (p8 address_to)memory_checked(grown);

        if (!bytes)
                return false;
        if (address_to area)
        {
                memory_copy_apart(bytes, address_to area, used);
                memory_free(address_to area, address_to room);
        }
        address_to area = bytes;
        address_to room = grown;
        return true;
}

/* Parse a block header already in the batch at `at`. 1 with both sizes,
   0 when a size is missing (serial from here), -1 malformed. */
static bipolar xz_par_header(xz_par address_to r, p8 address_to h, positive size,
                             p64 address_to compressed, p64 address_to uncompressed)
{
        p32 crc = 0;
        positive at = 2;
        p8 flags = h[1];

        for (positive i = 0; i < 4; i++)
                crc |= (p32)h[size - 4 + i] << (8 * i);
        if (crc != ~hash_crc32(0xffffffffu, h, size - 4))
                return xz_par_fail(r, "xz block header CRC"), -1;
        if (flags & 0x3c)
                return xz_par_fail(r, "xz reserved block flags"), -1;
        if ((flags & 0xc0) != 0xc0)
                return 0;

        positive k = memory_vli_get(h + at, size - 4 - at, 9, compressed);

        if (!k || !address_to compressed)
                return xz_par_fail(r, "xz block header"), -1;
        at += k;
        k = memory_vli_get(h + at, size - 4 - at, 9, uncompressed);
        if (!k)
                return xz_par_fail(r, "xz block header"), -1;
        return 1;
}

static bool xz_par_record(xz_par address_to r, p64 unpadded, p64 uncompressed)
{
        if (!xz_par_grow(address_of r->records, address_of r->records_room,
                         r->records_used, r->records_used + 20))
                return xz_par_fail(r, "xz cannot map the index");
        r->records_used += memory_vli_put(r->records + r->records_used, unpadded);
        r->records_used += memory_vli_put(r->records + r->records_used, uncompressed);
        r->record_count++;
        return true;
}

/* One block through a slot's decoder, from a memory span of its bytes into
   exactly its output. The decoder is kept per slot so its dictionary mapping
   survives from block to block. On a failure `written` still says how much
   of the block decoded, which is what GNU writes before its error. */
static string_address xz_par_block_decode(xz_par address_to r, positive slot,
                                          p8 address_to bytes, positive total,
                                          p8 address_to into, p64 uncompressed,
                                          positive address_to written)
{
        address_to written = 0;
        if (!r->slots[slot])
                r->slots[slot] = xz_block_decoder();
        if (!r->slots[slot])
                return "xz cannot map a decoder";
        if (!xz_block_decode(r->slots[slot], bytes, total, uncompressed, into,
                             r->check, null, written))
        {
                string_address why = xz_pull_error(r->slots[slot]);

                return why ? why : (string_address)"xz block";
        }
        return null;
}

/* One block, on any thread: its worker's decoder, exactly uncompressed bytes
   into the job's output. A malformed block records why for the sink. */
static fn xz_par_job(address_any context, positive index, parallel_output address_to output)
{
        xz_par address_to r = (xz_par address_to)context;
        xz_par_block address_to b = r->blocks + index;
        p8 address_to into = b->uncompressed ? parallel_reserve(output, b->uncompressed) : null;

        b->written = 0;
        if (b->uncompressed && !into)
        {
                b->why = "xz cannot map block output";
                return;
        }
        b->why = xz_par_block_decode(r, parallel_slot(), r->bytes + b->at, b->total,
                                     into, b->uncompressed, address_of b->written);
}

static bool xz_par_sink(address_any context, positive index, address_any data, positive length)
{
        xz_par address_to r = (xz_par address_to)context;
        xz_par_block address_to b = r->blocks + index;

        positive keep = b->why && b->written < length ? b->written : length;

        if (keep && system_write_all((positive)r->out, data, keep) != (bipolar)keep)
                return xz_par_fail(r, "xz write failed");
        return b->why ? xz_par_fail(r, b->why) : true;
}

static bool xz_par_run_batch(xz_par address_to r)
{
        if (!r->count)
                return true;

        bool ok = parallel_ordered(xz_par_job, xz_par_sink, r, r->count,
                                   xz_serial ? 0 : PARALLEL_SPREAD);

        r->count = 0;
        r->used = 0;
        return ok && !r->failed ? true : xz_par_fail(r, "xz cannot decode a block");
}

/* The index and footer of the stream whose blocks were all seen: every
   record must match, then the footer's backward size and flags. */
static bool xz_par_index(xz_par address_to r)
{
        p8 head[10];
        p64 count;
        positive n = 1;

        head[0] = 0;
        /* the indicator byte was read by the block loop */
        for (;; n++)
        {
                if (n >= sizeof(head) || xz_par_read(r, head + n, 1) != 1)
                        return xz_par_fail(r, "xz truncated index");
                if (!(head[n] & 0x80))
                        break;
        }
        n++;
        if (memory_vli_get(head + 1, n - 1, 9, address_of count) != n - 1 || count != r->record_count)
                return xz_par_fail(r, "xz index does not match the blocks");

        positive size = n + r->records_used;
        positive pad = (4 - (size & 3)) & 3;
        p8 tail[8 + 12];
        positive want = r->records_used;

        if (!xz_par_grow(address_of r->bytes, address_of r->room, 0, want + 1))
                return xz_par_fail(r, "xz cannot map the index");
        if (want && xz_par_read(r, r->bytes, want) != 1)
                return xz_par_fail(r, "xz truncated index");
        if (memory_compare(r->bytes, r->records, want))
                return xz_par_fail(r, "xz index does not match the blocks");
        if (xz_par_read(r, tail, pad + 4 + 12) != 1)
                return xz_par_fail(r, "xz truncated index");
        for (positive i = 0; i < pad; i++)
                if (tail[i])
                        return xz_par_fail(r, "xz index padding");

        p32 crc = hash_crc32(0xffffffffu, head, n);

        crc = hash_crc32(crc, r->bytes, want);
        crc = hash_crc32(crc, tail, pad);
        if (memory_load_unaligned(p32, tail + pad) != ~crc)
                return xz_par_fail(r, "xz index CRC");

        p8 address_to footer = tail + pad + 4;

        if (memory_load_unaligned(p32, footer) != ~hash_crc32(0xffffffffu, footer + 4, 6) ||
            memory_load_unaligned(p32, footer + 4) != (size + pad + 4) / 4 - 1 ||
            footer[8] || footer[9] != r->check || footer[10] != 'Y' || footer[11] != 'Z')
                return xz_par_fail(r, "xz footer");
        r->record_count = 0;
        r->records_used = 0;
        return true;
}

/* 1 decoded everything, 0 handed off to serial (prefix holds the consumed
   bytes, in_stream says where), -1 failed. */
static bipolar xz_par_walk(xz_par address_to r, positive batch_output)
{
        bool any = false;

        for (;;)
        {
                p8 stream[12];
                bipolar got;

                /* Stream Padding after a stream, then the next header or the end. */
                if (any)
                {
                        for (;;)
                        {
                                got = xz_par_read(r, stream, 4);
                                if (got < 0)
                                        return -1;
                                if (!got)
                                        return 1;
                                if (memory_load_unaligned(p32, stream))
                                        break;
                        }
                        if (xz_par_read(r, stream + 4, 8) != 1)
                                return xz_par_fail(r, "xz truncated header"), -1;
                }
                else
                {
                        got = xz_par_read(r, stream, 12);
                        if (got <= 0)
                                return got < 0 ? -1 : (xz_par_fail(r, "xz empty input"), -1);
                }
                if (memory_compare(stream, "\xfd" "7zXZ\0", 6))
                        return xz_par_fail(r, "xz bad magic"), -1;
                if (stream[6] || (stream[7] & 0xf0) ||
                    memory_load_unaligned(p32, stream + 8) != ~hash_crc32(0xffffffffu, stream + 6, 2))
                        return xz_par_fail(r, "xz header CRC"), -1;
                r->check = stream[7] & 15;
                /* A check the decoder does not know goes to the serial path,
                   which reports it exactly as a plain decode would. */
                if (r->check != XZ_CHECK_NONE && r->check != XZ_CHECK_CRC32 &&
                    r->check != XZ_CHECK_CRC64 && r->check != XZ_CHECK_SHA256)
                {
                        memory_copy_apart(r->prefix, stream, 12);
                        r->prefix_n = 12;
                        r->in_stream = false;
                        return 0;
                }
                r->check_size = r->check == XZ_CHECK_SHA256 ? 32
                              : r->check == XZ_CHECK_CRC64 ? 8
                              : r->check == XZ_CHECK_CRC32 ? 4 : 0;
                any = true;

                positive held = 0;
                bool first = true;

                for (;;)
                {
                        p8 h0;

                        if (xz_par_read(r, address_of h0, 1) != 1)
                                return xz_par_fail(r, "xz truncated stream"), -1;
                        if (!h0)
                        {
                                if (!xz_par_run_batch(r) || !xz_par_index(r))
                                        return -1;
                                break;
                        }

                        positive size = ((positive)h0 + 1) * 4;

                        if (!xz_par_grow(address_of r->bytes, address_of r->room, r->used,
                                         r->used + size))
                                return xz_par_fail(r, "xz cannot map the input"), -1;

                        p8 address_to h = r->bytes + r->used;

                        h[0] = h0;
                        if (xz_par_read(r, h + 1, size - 1) != 1)
                                return xz_par_fail(r, "xz truncated block header"), -1;

                        p64 compressed = 0;
                        p64 uncompressed = 0;
                        got = xz_par_header(r, h, size, address_of compressed,
                                            address_of uncompressed);
                        if (got < 0)
                                return -1;
                        if (!got)
                        {
                                /* Serial from this block on: what is decoded so
                                   far is written first, then the stream header
                                   (only for a first block) and this header. */
                                if (!xz_par_run_batch(r))
                                        return -1;
                                r->prefix_n = 0;
                                if (first)
                                {
                                        memory_copy_apart(r->prefix, stream, 12);
                                        r->prefix_n = 12;
                                }
                                memory_copy_apart(r->prefix + r->prefix_n, r->bytes, size);
                                r->prefix_n += size;
                                r->in_stream = !first;
                                return 0;
                        }

                        positive body = (positive)compressed;
                        positive total = size + body + ((4 - ((size + body) & 3)) & 3) +
                                         r->check_size;

                        if (compressed > (p64)positive_max / 2 ||
                            !xz_par_grow(address_of r->bytes, address_of r->room, r->used + size,
                                         r->used + total))
                                return xz_par_fail(r, "xz cannot map the input"), -1;
                        r->partial = 0;
                        if (xz_par_read(r, r->bytes + r->used + size, total - size) != 1)
                        {
                                /* Queued as it is: the driver writes the
                                   blocks before it, then what it decodes. */
                                r->blocks[r->count++] = (xz_par_block){
                                    r->used, size + r->partial, 0, uncompressed, null, 0};
                                return -1;
                        }
                        r->blocks[r->count] = (xz_par_block){
                            r->used, total, size + body + r->check_size, uncompressed, null};
                        if (!xz_par_record(r, size + body + r->check_size, uncompressed))
                                return -1;
                        r->used += total;
                        r->count++;
                        held += (positive)uncompressed;
                        first = false;
                        if (r->count == XZ_PAR_BLOCKS_MAX ||
                            (r->count >= 2 && held >= batch_output))
                        {
                                if (!xz_par_run_batch(r))
                                        return -1;
                                held = 0;
                        }
                }
        }
}

/* Serial from the saved prefix on: a stream header, or a block header inside
   the stream whose header the walk already read. */
static bool xz_par_serial_rest(xz_par address_to r)
{
        xz_decoder address_to d = xz_dec_new();

        if (!d)
        {
                r->why = "xz cannot map the decoder";
                return false;
        }
        byte_input_open_fd(address_of d->input, r->in, d->in_buf, XZ_DEC_IN);
        memory_copy_apart(d->in_buf, r->prefix, r->prefix_n);
        d->input.have = r->prefix_n;
        d->input.at = 0;
        d->out_fd = r->out;
        if (r->in_stream)
        {
                d->hdr_done = true;
                d->check = r->check;
        }

        bool ok = xz_dec_run(d);

        if (!ok)
                r->why = d->why;
        xz_dec_free(d);
        return ok;
}

static bool xz_par_decode(bipolar in, bipolar out)
{
        xz_par address_to r = (xz_par address_to)memory_checked(sizeof(xz_par));

        if (!r)
                return xz_fail("xz cannot map the decoder");
        r->in = in;
        r->out = out;
        r->slot_count = parallel_slots();
        r->slots = (address_any address_to)memory_checked(r->slot_count * sizeof(address_any));

        bool ok = false;

        if (r->slots)
        {
                bipolar got = xz_par_walk(r, xz_batch_room((positive)1 << 20));

                if (got < 0 && r->count)
                {
                        string_address why = r->why;

                        r->why = null;
                        r->failed = false;
                        xz_par_run_batch(r);
                        if (!r->why)
                                r->why = why;
                        r->failed = true;
                }
                ok = got > 0 || (got == 0 && xz_par_serial_rest(r));
                for (positive i = 0; i < r->slot_count; i++)
                        if (r->slots[i])
                                xz_pull_close(r->slots[i]);
                memory_free(r->slots, r->slot_count * sizeof(address_any));
        }
        else
                r->why = "xz cannot map the decoder";
        if (!ok)
                xz_why = r->why ? r->why : (string_address)"xz decode failed";
        memory_free(r->bytes, r->room);
        memory_free(r->records, r->records_room);
        memory_free(r, sizeof(xz_par));
        return ok;
}

static bool xz_cli_setup(void);

static b32 xz_stream_cli(bipolar in, bipolar out, bool decode, p8 level)
{
        bool ok;

        xz_status = 0;
        xz_serial = xz_threads == 1;
        //      One CPU gains nothing from blocks decoded side by side, and the
        //      batch held whole blocks of output: 148 MB where streaming
        //      through the dictionary holds 13.
        if (decode && !xz_serial && parallel_width() > 1)
                ok = xz_par_decode(in, out);
        else if (decode)
        {
                xz_decoder address_to d = xz_dec_new();

                ok = false;
                xz_why = "xz cannot map the decoder";
                if (d)
                {
                        byte_input_open_fd(address_of d->input, in, d->in_buf,
                                           XZ_DEC_IN);
                        d->out_fd = out;
                        ok = xz_dec_run(d);
                        xz_why = d->why;
                        xz_dec_free(d);
                }
        }
        else
        {
                byte_input_open_fd(address_of xz_input, in, xz_in_buf, XZ_IN);
                xz_out_fd = out;
                xz_output.bytes = null;
                if (!xz_cli_setup())
                {
                        xz_status = 1;
                        return 1;
                }
                ok = xz_stream_encode(level | (xz_cli_extreme ? XZ_EXTREME : 0));
        }
        if (!ok)
        {
                //      The reason is spelled for tar, codec first; the command
                //      has named itself already.
                if (xz_why)
                        string_format(log_error, "xz: %s\n",
                                      xz_why + (!string_compare_max(xz_why, "xz ", 3) ? 3 : 0));
                xz_status = 1;
                return 1;
        }
        return 0;
}

/*
        The options that shape a stream, as xz 5.8 reads them and in its
        order: -0 .. -9, --fast and --best set the preset and forget any
        chain built so far; -e sets the extreme flag and forgets it too; a
        filter (--x86 and its kin, --delta, --lzma2, --lzma1) is added to the
        chain and puts the preset back to 6; and --block-size, -C and -T. A
        chain must end in exactly one LZMA2 and hold at most four filters.
        Words that xz refuses are refused with its wording and status.
*/
enum { XZ_CLI_FILTER, XZ_CLI_LZMA2, XZ_CLI_LZMA1 };

typedef struct
{
        p8 kind;
        xz_filter f;
        xz_preset lz;
} xz_cli_entry;

static xz_cli_entry xz_cli_chain[4];
static positive xz_cli_count;

static bool xz_cli_say(string_address one, string_address two, string_address three)
{
        string_format(log_error, "xz: %s%s%s\n", one, two, three);
        return false;
}

/* xz's str_to_uint64: blanks, "max", decimal digits, a k/m/g suffix with
   Ki, KiB, KB or B after it, and a range. */
static bool xz_cli_number(string_address name, string_address value, p64 min, p64 max,
                          p64 address_to out)
{
        p64 result = 0;

        while (*value == ' ' || *value == '\t')
                value++;
        if (string_equals(value, "max"))
        {
                address_to out = max;
                return true;
        }
        if (*value < '0' || *value > '9')
        {
                string_format(log_error, "xz: %s: Value is not a non-negative decimal integer\n",
                              value);
                return false;
        }
        do
        {
                p64 add = (p64)(*value - '0');

                if (result > ~(p64)0 / 10)
                        goto range;
                result *= 10;
                if (~(p64)0 - add < result)
                        goto range;
                result += add;
                value++;
        } while (*value >= '0' && *value <= '9');
        if (*value)
        {
                p64 multiplier = 0;
                string_address suffix = value;

                if (*value == 'k' || *value == 'K')
                        multiplier = (p64)1 << 10;
                else if (*value == 'm' || *value == 'M')
                        multiplier = (p64)1 << 20;
                else if (*value == 'g' || *value == 'G')
                        multiplier = (p64)1 << 30;
                value++;
                if (*value && !string_equals(value, "i") && !string_equals(value, "iB") &&
                    !string_equals(value, "B"))
                        multiplier = 0;
                if (!multiplier)
                {
                        string_format(log_error, "xz: %s: Invalid multiplier suffix\n", suffix);
                        string_format(log_error,
                                      "xz: Valid suffixes are 'KiB' (2^10), 'MiB' (2^20), and 'GiB' (2^30).\n");
                        return false;
                }
                if (result > ~(p64)0 / multiplier)
                        goto range;
                result *= multiplier;
        }
        if (result < min || result > max)
                goto range;
        address_to out = result;
        return true;

range:
        string_format(log_error, "xz: Value of the option '%s' must be in the range [%p, %p]\n",
                      name, min, max);
        return false;
}

/* xz steps over one + before a thread count and before no other number. */
static bool xz_cli_threads(string_address text)
{
        p64 count;

        if (text[0] == '+')
                text++;
        if (!xz_cli_number("threads", text, 0, 16384, address_of count))
                return false;
        xz_threads = (positive)count;
        return true;
}

typedef struct
{
        string_address name;
        p8 kind; /* 0 a number, 1 the LZMA preset, 2 a word from words */
        p64 min;
        p64 max;
        const string_address address_to words;
} xz_cli_option_map;

static const string_address xz_cli_modes[] = {"fast", "normal", null};
static const string_address xz_cli_mfs[] = {"hc3", "hc4", "bt2", "bt3", "bt4", null};

/* xz's parse_options over `str`, calling set(key, value or word index). The
   value is copied to a small buffer, as a number needs its end. */
static bool xz_cli_options(string_address str, const xz_cli_option_map address_to map,
                           positive kinds, bool (*set)(address_any who, positive key, p64 value,
                                                       string_address text),
                           address_any who)
{
        positive at = 0;
        p8 text[64];

        if (!str)
                return true;
        while (str[at])
        {
                if (str[at] == ',')
                {
                        at++;
                        continue;
                }

                positive stop = at;
                positive eq = 0;

                while (str[stop] && str[stop] != ',')
                {
                        if (!eq && str[stop] == '=')
                                eq = stop;
                        stop++;
                }
                if (!eq || eq + 1 == stop)
                {
                        string_format(log_error,
                                      "xz: %s: Options must be 'name=value' pairs separated with commas\n",
                                      str);
                        return false;
                }

                positive name_n = eq - at;
                positive value_n = stop - eq - 1;
                positive key = 0;

                while (key < kinds && (string_length(map[key].name) != name_n ||
                                       string_compare_max(map[key].name, str + at, name_n)))
                        key++;
                if (key == kinds)
                {
                        p8 name[64];
                        positive k = name_n < sizeof(name) - 1 ? name_n : sizeof(name) - 1;

                        memory_copy_apart(name, str + at, k);
                        name[k] = 0;
                        return xz_cli_say(name, ": Invalid option name", "");
                }
                if (value_n >= sizeof(text))
                        value_n = sizeof(text) - 1;
                memory_copy_apart(text, str + eq + 1, value_n);
                text[value_n] = 0;

                p64 value = 0;

                if (map[key].words)
                {
                        positive w = 0;

                        while (map[key].words[w] && !string_equals(map[key].words[w], (string_address)text))
                                w++;
                        if (!map[key].words[w])
                                return xz_cli_say((string_address)text, ": Invalid option value", "");
                        value = w;
                }
                else if (map[key].kind == 0 &&
                         !xz_cli_number(map[key].name, (string_address)text, map[key].min,
                                        map[key].max, address_of value))
                        return false;
                if (!set(who, key, value, (string_address)text))
                        return false;
                at = stop;
        }
        return true;
}

static bool xz_cli_set_delta(address_any who, positive key, p64 value, string_address text)
{
        (void)key;
        (void)text;
        ((xz_cli_entry address_to)who)->f.dist = (p8)(value - 1);
        return true;
}

static bool xz_cli_set_bcj(address_any who, positive key, p64 value, string_address text)
{
        (void)key;
        (void)text;
        ((xz_cli_entry address_to)who)->f.start = (p32)value;
        return true;
}

static bool xz_cli_set_lzma(address_any who, positive key, p64 value, string_address text)
{
        xz_preset address_to lz = &((xz_cli_entry address_to)who)->lz;

        switch (key)
        {
        case 0: /* preset */
        {
                if (text[0] < '0' || text[0] > '9' ||
                    (text[1] && (text[1] != 'e' || text[2])))
                        return xz_cli_say("Unsupported LZMA1/LZMA2 preset: ", text, "");
                *lz = xz_preset_of((p8)((text[0] - '0') | (text[1] ? XZ_EXTREME : 0)));
                break;
        }
        case 1: lz->dict = (p32)value; break;
        case 2: lz->lc = (p8)value; break;
        case 3: lz->lp = (p8)value; break;
        case 4: lz->pb = (p8)value; break;
        case 5: lz->normal = value == 1; break;
        case 6: lz->nice = (p16)value; break;
        case 7:
                //      Hash chains of three and four bytes and binary trees
                //      of two, three and four: the trees of two and three
                //      bytes are searched as the tree of four, which finds
                //      what they find and more.
                lz->finder = value == 0 ? XZ_FINDER_HC3 : value == 1 ? XZ_FINDER_HC4 : XZ_FINDER_BT4;
                break;
        case 8: lz->depth = (p32)value; break;
        }
        return true;
}

static const xz_cli_option_map xz_cli_delta_map[] = {{"dist", 0, 1, 256, null}};
static const xz_cli_option_map xz_cli_bcj_map[] = {{"start", 0, 0, 0xffffffffu, null}};
static const xz_cli_option_map xz_cli_lzma_map[] = {
    {"preset", 1, 0, 0, null},
    {"dict", 0, 4096, ((p64)1 << 30) + ((p64)1 << 29), null},
    {"lc", 0, 0, 4, null},
    {"lp", 0, 0, 4, null},
    {"pb", 0, 0, 4, null},
    {"mode", 2, 0, 0, xz_cli_modes},
    {"nice", 0, 2, 273, null},
    {"mf", 2, 0, 0, xz_cli_mfs},
    {"depth", 0, 0, 0xffffffffu, null}};

/* Add a filter: id, and the text after '=' when there was one. */
static bool xz_cli_add(p8 kind, p8 id, string_address text)
{
        if (xz_cli_count == 4)
                return xz_cli_say("Maximum number of filters is four", "", "");

        xz_cli_entry address_to e = xz_cli_chain + xz_cli_count;

        memory_fill(e, 0, sizeof(*e));
        e->kind = kind;
        e->f.id = id;
        e->lz = xz_preset_of(6);
        if (kind != XZ_CLI_FILTER)
        {
                if (!xz_cli_options(text, xz_cli_lzma_map, array_count(xz_cli_lzma_map),
                                    xz_cli_set_lzma, e))
                        return false;
                if (e->lz.lc + e->lz.lp > 4)
                        return xz_cli_say("The sum of lc and lp must not exceed 4", "", "");
        }
        else if (id == XZ_FILTER_DELTA)
        {
                if (!xz_cli_options(text, xz_cli_delta_map, 1, xz_cli_set_delta, e))
                        return false;
        }
        else if (!xz_cli_options(text, xz_cli_bcj_map, 1, xz_cli_set_bcj, e))
                return false;
        xz_cli_count++;
        return true;
}

/* -e / --extreme, whatever order it takes with -0 .. -9, as xz keeps its
   preset flag apart from the number; -C WORD / --check=WORD, the four names
   xz spells, WORD being the rest of the cluster or the next argument. */

static bool xz_cli_check(string_address word)
{
        static const string_address names[] = {"none", "crc32", "crc64", "sha256"};
        static const p8 types[] = {XZ_CHECK_NONE, XZ_CHECK_CRC32, XZ_CHECK_CRC64,
                                   XZ_CHECK_SHA256};

        for (positive i = 0; i < array_count(names); i++)
                if (string_equals(word, names[i]))
                {
                        xz_encode_check = types[i];
                        return true;
                }
        string_format(log_error, "xz: %s: Unsupported integrity check type\n", word);
        return false;
}

static bipolar xz_cli_missing(string_address one, string_address name, string_address three)
{
        xz_cli_say(one, name, three);
        string_format(log_error, "xz: Try 'xz --help' for more information.\n");
        return -1;
}

/* A setting from the chain of words: the value of --word=VALUE, or the next
   argument for --word VALUE. */
static bipolar xz_cli_value(file_codec_cli address_to codec, string_address at,
                            string_address name, string_address address_to value)
{
        positive n = string_length(name);

        if (string_has_prefix(at, "--") && !string_compare_max(at + 2, name, n))
        {
                if (at[2 + n] == '=')
                {
                        address_to value = at + 3 + n;
                        return 1;
                }
                if (!at[2 + n])
                {
                        if (!codec->next_argument)
                                return xz_cli_missing("option '--", name, "' requires an argument");
                        codec->took_next = true;
                        address_to value = codec->next_argument;
                        return 1;
                }
        }
        return 0;
}

static bipolar xz_cli_option(file_codec_cli address_to codec, string_address at,
                             bool word)
{
        static const struct
        {
                string_address name;
                positive id;
        } bcj[] = {{"x86", XZ_FILTER_X86}, {"powerpc", XZ_FILTER_PPC}, {"ia64", XZ_FILTER_IA64},
                   {"arm", XZ_FILTER_ARM}, {"armthumb", XZ_FILTER_ARMT}, {"arm64", XZ_FILTER_ARM64},
                   {"sparc", XZ_FILTER_SPARC}, {"riscv", XZ_FILTER_RISCV}, {"delta", XZ_FILTER_DELTA},
                   {"lzma2", XZ_FILTER_LZMA2}, {"lzma1", 0x4000}};

        if (!word)
        {
                if (*at >= '0' && *at <= '9')
                {
                        codec->level = (p8)(*at - '0');
                        xz_cli_count = 0;
                        return 1;
                }
                if (*at == 'e')
                {
                        xz_cli_extreme = true;
                        xz_cli_count = 0;
                        return 1;
                }
                if (*at != 'C' && *at != 'T')
                        return 0;

                string_address value = at[1] ? at + 1 : codec->next_argument;
                p8 letter[2] = {*at, 0};

                if (!value)
                        return xz_cli_missing("option requires an argument -- '", (string_address)letter, "'");
                codec->took_next = !at[1];
                if (!(*at == 'C' ? xz_cli_check(value) : xz_cli_threads(value)))
                        return -1;
                return at[1] ? (bipolar)string_length(at) : 1;
        }
        if (string_equals(at, "--extreme"))
        {
                xz_cli_extreme = true;
                xz_cli_count = 0;
                return 1;
        }
        if (string_equals(at, "--fast") || string_equals(at, "--best"))
        {
                codec->level = at[2] == 'f' ? 0 : 9;
                xz_cli_count = 0;
                return 1;
        }
        for (positive i = 0; i < array_count(bcj); i++)
        {
                positive n = string_length(bcj[i].name);

                if (string_has_prefix(at, "--") && !string_compare_max(at + 2, bcj[i].name, n) &&
                    (!at[2 + n] || at[2 + n] == '='))
                {
                        string_address text = at[2 + n] ? at + 3 + n : null;

                        if (!xz_cli_add(bcj[i].id == XZ_FILTER_LZMA2 ? XZ_CLI_LZMA2
                                        : bcj[i].id == 0x4000 ? XZ_CLI_LZMA1 : XZ_CLI_FILTER,
                                        (p8)bcj[i].id, text))
                                return -1;
                        codec->level = 6;
                        xz_cli_extreme = false;
                        return 1;
                }
        }
        string_address text = null;
        bipolar got = xz_cli_value(codec, at, "check", address_of text);

        if (got)
                return got < 0 ? got : xz_cli_check(text) ? 1 : -1;
        got = xz_cli_value(codec, at, "threads", address_of text);
        if (got)
                return got < 0 ? got : xz_cli_threads(text) ? 1 : -1;
        got = xz_cli_value(codec, at, "block-size", address_of text);

        if (got <= 0)
                return got;

        p64 size;

        if (!xz_cli_number("block-size", text, 0, ((p64)1 << 63) - 1, address_of size))
                return -1;
        xz_block_size = size > XZ_BLOCK_MAX ? XZ_BLOCK_MAX : (positive)size;
        return 1;
}

/* The chain the words built, checked as xz checks it before it encodes:
   LZMA2 last and alone, start offsets aligned. Nothing built is a preset. */
static bool xz_cli_setup(void)
{
        xz_encode_custom = false;
        if (!xz_cli_count)
                return true;

        positive last = xz_cli_count - 1;
        bool has_lzma2 = false;
        bool bad_start = false;

        for (positive i = 0; i <= last; i++)
        {
                xz_cli_entry address_to e = xz_cli_chain + i;

                if (e->kind == XZ_CLI_LZMA1)
                        return xz_cli_say("LZMA1 cannot be used with the .xz format", "", "");
                has_lzma2 |= e->kind == XZ_CLI_LZMA2;
                bad_start |= e->kind == XZ_CLI_FILTER && e->f.id != XZ_FILTER_DELTA &&
                             (e->f.start & (xz_filter_alignment(e->f.id) - 1));
        }

        //      xz's threaded encoder looks for the block size of the chain
        //      first, and names the chain when it finds none or a start
        //      offset it cannot use; a chain that is only badly ordered is
        //      what the encoder itself refuses.
        string_address why = xz_threads != 1 && (!has_lzma2 || bad_start)
                                 ? "Unsupported options in filter chain 0"
                                 : "Unsupported filter chain or filter options";

        if (xz_cli_chain[last].kind != XZ_CLI_LZMA2 || bad_start)
                return xz_cli_say(why, "", "");
        memory_fill(address_of xz_encode_options, 0, sizeof(xz_encode_options));
        for (positive i = 0; i < last; i++)
        {
                xz_cli_entry address_to e = xz_cli_chain + i;

                if (e->kind != XZ_CLI_FILTER ||
                    (e->f.id != XZ_FILTER_DELTA &&
                     (e->f.start & (xz_filter_alignment(e->f.id) - 1))))
                        return xz_cli_say(why, "", "");
                xz_encode_options.filt[xz_encode_options.nfilt++] = e->f;
        }
        xz_encode_options.lz = xz_cli_chain[last].lz;
        xz_encode_custom = true;
        return true;
}

static const file_codec_suffix xz_suffixes[] = {
    {".xz", ""}, {".txz", ".tar"}};

static b32 file_xz(void)
{
        file_codec_cli codec = {
            .name = "xz", .decode_name = "unxz", .cat_name = "xzcat",
            .usage = "Usage: xz [-cdefkqt0123456789] [-C CHECK] [-T N] [--x86|--arm|--delta|--lzma2[=OPTS]...] [FILE...]",
            .version = "xz from moonwater",
            .status = address_of xz_status,
            .suffixes = xz_suffixes, .suffix_count = array_count(xz_suffixes),
            .decode_suffix_error = "unknown suffix; use -c",
            .encode_suffix_error = "cannot guess output name",
            .features = FILE_CODEC_LEVEL_ZERO,
            .remove_source = true, .level = 6,
            .run = xz_stream_cli, .option = xz_cli_option};
        return file_codec_main(address_of codec);
}

#endif /* XZ_CORE_ONLY */
