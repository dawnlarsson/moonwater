/*
        gzip -- RFC 1952 members around RFC 1951 deflate.

        Decode runs a per-stream state over packed Huffman cells and a shared
        assembly token loop, with an exact scalar decoder for the ends of the
        input window and the output slab. Encode
        deflates fixed 1 MiB blocks, each with the previous 32 KiB as
        history, through a 32 KiB hash chain with lazy matching and dynamic
        Huffman blocks that fall back to fixed or stored when the tree would
        not pay (see the encoder below). Match lengths are memory_common_prefix;
        checksums are hash_crc32. Concatenated members are accepted the
        way gzip -d accepts them. There is no encryption, no LZW, and no
        zlib wrapper.
*/

#define GZIP_MAGIC0 0x1f
#define GZIP_MAGIC1 0x8b
#define GZIP_METHOD 8
#define GZIP_WINDOW 32768
#define GZIP_WMASK (GZIP_WINDOW - 1)
#define GZIP_IN 16384
#define GZIP_MAXBITS 15
#define GZIP_MAXLIT 288
#define GZIP_MAXDIST 32
#define GZIP_MIN_MATCH 3
#define GZIP_MAX_MATCH 258

#define GZIP_FTEXT 1
#define GZIP_FHCRC 2
#define GZIP_FEXTRA 4
#define GZIP_FNAME 8
#define GZIP_FCOMMENT 16

static const p8 gzip_len_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const p16 gzip_len_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const p8 gzip_dist_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const p16 gzip_dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
        12289, 16385, 24577};
static const p8 gzip_clen_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static p8 gzip_in_buf[GZIP_IN];
static byte_input gzip_input = {.buf = gzip_in_buf, .room = GZIP_IN};
static bipolar gzip_out_fd;
static byte_store gzip_output;
static string_address gzip_why;
static b32 gzip_status;

static bool gzip_fail(string_address why)
{
        gzip_why = why;
        return false;
}

/* Count code lengths into count. The code space still unused, -1 for a
   length past limit, -2 for lengths that over-subscribe the space. */
static bipolar gzip_code_space(p8 address_to length, positive n, p8 limit,
                               p16 address_to count)
{
        bipolar left = 1;

        memory_fill(count, 0, (GZIP_MAXBITS + 1) * sizeof(p16));
        for (positive at = 0; at < n; at++)
                if (length[at] > limit)
                        return -1;
                else
                        count[length[at]]++;
        count[0] = 0;
        for (positive len = 1; len <= limit; len++)
        {
                left <<= 1;
                if (left < count[len])
                        return -2;
                left -= count[len];
        }
        return left;
}

static p8 gzip_fixed_lit_len[GZIP_MAXLIT];
static p32 gzip_fixed_lit_code[GZIP_MAXLIT];
static p8 gzip_fixed_dist_len[GZIP_MAXDIST];
static p32 gzip_fixed_dist_code[GZIP_MAXDIST];
static bool gzip_fixed_codes;
static fn gzip_fixed_init(void);

/*
        Decode. A gzip_inflater holds everything one stream needs, so decoders
        can run side by side: the input window, the bit reader, 32 KiB of
        history in front of a 256 KiB output slab, the packed Huffman cells,
        member and block framing, CRC and length, and the first error. It
        never logs; callers report why. deflate_decode_span decodes whole runs
        of tokens straight into the slab and gzip_inflate_token is the exact
        scalar decoder for the ends of the input and the slab, reading the
        same cells. CRC runs once per slab and once per member end.
*/

/* The span kernel contract; test/codec_floor extracts from here to its end.
   Cells are one u32 each:
     literal   GZIP_CELL_LITERAL | byte << 8 | codeword bits
     length    length << 16 | (codeword + extra bits)
     distance  base << 16 | codeword bits << 8 | (codeword + extra bits)
     end       EXCEPTIONAL | END | codeword bits
     subtable  start << 16 | EXCEPTIONAL | SUBTABLE | subtable bits << 8 | root
     invalid   EXCEPTIONAL, with SYMBOL | codeword bits for 286, 287, 30, 31
   A length code is widened by its extra bits: every extra value is a code of
   its own, so a length cell holds the final length. Distances keep their
   extra bits in the stream. Cells inside a subtable count only the bits past
   the root. */
#define GZIP_CELL_LITERAL 0x80000000u
#define GZIP_CELL_EXCEPTIONAL 0x8000u
#define GZIP_CELL_SUBTABLE 0x4000u
#define GZIP_CELL_END 0x2000u
#define GZIP_CELL_SYMBOL 0x1000u
#define GZIP_LITLEN_ROOT 11
#define GZIP_OFFSET_ROOT 8
#define GZIP_PRECODE_ROOT 7
/* The root plus the widest subtable any prefix can need, incomplete codes
   included: a symbol with c code and x extra bits widens to at most 16 << x
   cells past an 11-bit root (256 literals, end, 286 and 287 at 16 each; the
   29 lengths sum to 16 * 257), and a distance code to 128 past 8 bits. */
#define GZIP_LITLEN_CELLS (2048 + 16 * (256 + 1 + 2 + 257))
#define GZIP_OFFSET_CELLS (256 + 32 * 128)
/* The kernel runs only with this much input and output room ahead. */
#define GZIP_SPAN_IN 33
#define GZIP_SPAN_OUT 301
typedef struct
{
        p64 bits;
        positive count;
        p8 address_to next;
        p8 address_to limit;
        p8 address_to out;
        p8 address_to out_limit;
        p8 address_to window;
        const p32 address_to litlen;
        const p32 address_to offset;
        const p32 address_to masks;
        positive status;
} gzip_decode_job;

/* masks[n] is (1 << n) - 1, for distance extra bits and subtable indexes. */
static const p32 gzip_extra_masks[32] = {
        0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff,
        0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff, 0x1ffff, 0x3ffff, 0x7ffff,
        0xfffff, 0x1fffff, 0x3fffff, 0x7fffff, 0xffffff, 0x1ffffff, 0x3ffffff,
        0x7ffffff, 0xfffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff};

/* kind 0 is the code-length code, 1 literal/length, 2 distance; extra is
   the value of a length's extra bits, which bits already counts. */
static p32 gzip_cell(positive kind, positive symbol, positive extra, positive bits)
{
        if (kind == 0)
                return (p32)(symbol << 16 | bits);
        if (kind == 1)
        {
                if (symbol < 256)
                        return GZIP_CELL_LITERAL | (p32)(symbol << 8 | bits);
                if (symbol == 256)
                        return GZIP_CELL_EXCEPTIONAL | GZIP_CELL_END | (p32)bits;
                if (symbol > 285)
                        return GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SYMBOL | (p32)bits;
                return (p32)(gzip_len_base[symbol - 257] + extra) << 16 | (p32)bits;
        }
        if (symbol >= 30)
                return GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SYMBOL | (p32)bits;
        return (p32)gzip_dist_base[symbol] << 16 | (p32)(bits << 8) |
               (p32)(bits + gzip_dist_extra[symbol]);
}

/* The next canonical code, bit-reversed, of the same length. */
static p32 gzip_revnext(p32 rev, positive len)
{
        p32 bit = (p32)1 << (len - 1);

        while (rev & bit)
        {
                rev ^= bit;
                bit >>= 1;
        }
        return rev | bit;
}

/* The extra bits a length symbol's codes widen by. */
static const p8 gzip_symbol_extra[GZIP_MAXLIT] = {
        [265] = 1, [266] = 1, [267] = 1, [268] = 1, [269] = 2, [270] = 2,
        [271] = 2, [272] = 2, [273] = 3, [274] = 3, [275] = 3, [276] = 3,
        [277] = 4, [278] = 4, [279] = 4, [280] = 4, [281] = 5, [282] = 5,
        [283] = 5, [284] = 5};

/* A symbol's cell before its bit counts: the builder adds the bits once for
   the code-length and literal/length kinds, and at bits 0 and 8 for
   distances; a length adds its extra value at bit 16. */
static inline INLINE p32 gzip_cell_base(positive kind, positive symbol)
{
        if (kind == 0)
                return (p32)symbol << 16;
        if (kind == 1)
        {
                if (symbol < 256)
                        return GZIP_CELL_LITERAL | (p32)symbol << 8;
                if (symbol == 256)
                        return GZIP_CELL_EXCEPTIONAL | GZIP_CELL_END;
                if (symbol > 285)
                        return GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SYMBOL;
                return (p32)gzip_len_base[symbol - 257] << 16;
        }
        if (symbol >= 30)
                return GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SYMBOL;
        return (p32)gzip_dist_base[symbol] << 16 | gzip_dist_extra[symbol];
}

/* Widened codes a table can hold: 256 literals, end, 286, 287 and the 257
   extra values of the 29 lengths. */
#define GZIP_WIDE_CODES (256 + 1 + 2 + 257)

/* Canonical cells for length[0..n) (each at most 15): a root `root` bits
   wide and, past it, one subtable per prefix sized for the widest code under
   that prefix. Incomplete codes are accepted and their unused codes decode
   to an invalid cell. 0, or -2 for lengths that over-subscribe the space. */
static bipolar gzip_huffman_cells(p32 address_to table, p8 address_to length,
                                  positive n, positive root, positive kind)
{
        p16 count[GZIP_MAXBITS + 1];
        p16 offs[GZIP_MAXBITS + 1];
        p16 widths[GZIP_MAXBITS + 6];
        p16 place[GZIP_MAXBITS + 6];
        p16 sorted[GZIP_MAXLIT];
        p32 codes[GZIP_WIDE_CODES];
        p32 cells[GZIP_WIDE_CODES];
        p32 step = kind == 2 ? 0x101 : 1;
        positive cellcount = (positive)1 << root;
        positive spare = cellcount;
        positive total = 0;
        positive index = 0;
        positive first = GZIP_MAXBITS + 6;
        bipolar left = 1;
        p32 rev = 0;

        memory_fill(count, 0, sizeof(count));
        memory_fill(widths, 0, sizeof(widths));
        for (positive at = 0; at < n; at++)
                count[length[at]]++;
        count[0] = 0;
        for (positive len = 1; len <= GZIP_MAXBITS; len++)
        {
                left <<= 1;
                left -= count[len];
                if (left < 0)
                        return -2;
        }
        offs[1] = 0;
        for (positive len = 1; len < GZIP_MAXBITS; len++)
                offs[len + 1] = offs[len] + count[len];
        for (positive at = 0; at < n; at++)
                if (length[at])
                        sorted[offs[length[at]]++] = (p16)at;
        if (kind == 1)
                for (positive at = 265; at < n && at < 285; at++)
                        if (length[at])
                        {
                                widths[length[at]]--;
                                widths[length[at] + gzip_symbol_extra[at]] +=
                                        (p16)(1 << gzip_symbol_extra[at]);
                        }
        for (positive len = 1; len <= GZIP_MAXBITS; len++)
                widths[len] += count[len];
        for (positive width = 0; width < GZIP_MAXBITS + 6; width++)
        {
                if (widths[width] && first > width)
                        first = width;
                place[width] = (p16)total;
                total += widths[width];
        }
        for (positive len = 1; len <= GZIP_MAXBITS; len++)
                for (positive k = 0; k < count[len]; k++, index++)
                {
                        positive symbol = sorted[index];
                        positive extra = kind == 1 ? gzip_symbol_extra[symbol] : 0;
                        positive at = place[len + extra];
                        p32 base = gzip_cell_base(kind, symbol);

                        place[len + extra] = (p16)(at + ((positive)1 << extra));
                        for (positive value = 0; value < (positive)1 << extra; value++)
                        {
                                codes[at + value] = rev | (p32)(value << len);
                                cells[at + value] = base + (p32)(value << 16);
                        }
                        rev = gzip_revnext(rev, len);
                }
        /* place[w] now ends the codes exactly w bits wide. The root doubles
           as it widens: a code up to w bits wide repeats every 2^w cells, so
           each step copies the filled half and places the codes of width w. */
        if (first > root)
                first = root;
        if (left)
                for (positive at = 0; at < (positive)1 << first; at++)
                        table[at] = GZIP_CELL_EXCEPTIONAL;
        for (positive width = first; width <= root; width++)
        {
                if (width > first)
                        memory_copy_apart(table + ((positive)1 << (width - 1)), table,
                                          ((positive)1 << (width - 1)) * sizeof(p32));
                for (positive at = place[width] - widths[width]; at < place[width]; at++)
                        table[codes[at]] = cells[at] + (p32)width * step;
        }
        if (total == place[root])
                return 0;
        /* Past the root, widest first, so the first code met under a prefix
           sizes its subtable. Pointers an earlier table left are cleared. */
        for (positive at = place[root]; at < total; at++)
                table[codes[at] & (cellcount - 1)] = GZIP_CELL_EXCEPTIONAL;
        for (positive width = GZIP_MAXBITS + 5; width > root; width--)
                for (positive at = place[width] - widths[width]; at < place[width]; at++)
                {
                        positive prefix = codes[at] & (cellcount - 1);
                        p32 pointer = table[prefix];

                        if (!(pointer & GZIP_CELL_SUBTABLE))
                        {
                                positive bits = width - root;

                                pointer = (p32)(spare << 16) | GZIP_CELL_EXCEPTIONAL |
                                          GZIP_CELL_SUBTABLE | (p32)(bits << 8) | (p32)root;
                                table[prefix] = pointer;
                                if (left)
                                        for (positive cell = 0; cell < (positive)1 << bits; cell++)
                                                table[spare + cell] = GZIP_CELL_EXCEPTIONAL;
                                spare += (positive)1 << bits;
                        }
                        positive start = pointer >> 16;
                        positive bits = (pointer >> 8) & 15;
                        p32 cell = cells[at] + (p32)(width - root) * step;

                        for (positive slot = codes[at] >> root; slot < (positive)1 << bits;
                             slot += (positive)1 << (width - root))
                                table[start + slot] = cell;
                }
        return 0;
}
/* End of the span kernel contract. */

#define GZIP_DECODE_IN (256 * 1024)
#define GZIP_DECODE_OUT (256 * 1024)
/* Past the slab: the kernel's widest overshoot, and a scalar match. */
#define GZIP_DECODE_SLACK 320

typedef struct
{
        p64 bits;
        positive count;
        byte_input input;
        p8 address_to out;
        positive fill;
        positive taken;
        positive crc_at;
        p64 flushed;
        p64 member_start;
        p32 crc;
        positive members;
        positive stored_left;
        p8 block_kind;
        bool have_block;
        bool block_last;
        bool stored_open;
        bool head_done;
        bool finished;
        bool fixed_loaded;
        string_address why;
        p32 litlen[GZIP_LITLEN_CELLS];
        p32 offset[GZIP_OFFSET_CELLS];
        p32 precode[1 << GZIP_PRECODE_ROOT];
} gzip_inflater;

/* The state, then the input window, the history and the slab with slack. */
#define GZIP_INFLATER_SIZE (sizeof(gzip_inflater) + GZIP_DECODE_IN + \
                            GZIP_WINDOW + GZIP_DECODE_OUT + GZIP_DECODE_SLACK)

static gzip_inflater address_to gzip_inflater_new(void)
{
        gzip_inflater address_to z = (gzip_inflater address_to)memory_checked(GZIP_INFLATER_SIZE);
        p8 address_to tail;

        if (!z)
                return null;
        tail = (p8 address_to)(z + 1);
        z->bits = 0;
        z->count = 0;
        byte_input_open_fd(address_of z->input, -1, tail, GZIP_DECODE_IN);
        z->out = tail + GZIP_DECODE_IN + GZIP_WINDOW;
        z->fill = 0;
        z->taken = 0;
        z->crc_at = 0;
        z->flushed = 0;
        z->member_start = 0;
        z->crc = 0xffffffffu;
        z->members = 0;
        z->stored_left = 0;
        z->block_kind = 0;
        z->have_block = false;
        z->block_last = false;
        z->stored_open = false;
        z->head_done = false;
        z->finished = false;
        z->fixed_loaded = false;
        z->why = null;
        return z;
}

static bool gzip_inflate_fail(gzip_inflater address_to z, string_address why)
{
        if (!z->why)
                z->why = why;
        return false;
}

/* Hand whole lookahead bytes back to the input window first, so that a
   compaction keeps every byte the bit buffer has not consumed. */
static bool gzip_inflate_more(gzip_inflater address_to z, positive want)
{
        z->input.at -= z->count >> 3;
        z->count &= 7;
        z->bits &= ((p64)1 << z->count) - 1;
        if (byte_input_need(address_of z->input, want) < 0)
                return gzip_inflate_fail(z, "gzip read failed");
        return true;
}

/* Hold at least need bits (need <= 56), fewer only when the input ends. */
static bool gzip_inflate_bits(gzip_inflater address_to z, positive need)
{
        while (z->count < need)
        {
                if (z->input.at >= z->input.have)
                {
                        if (z->input.eof)
                                return true;
                        if (!gzip_inflate_more(z, 8))
                                return false;
                        continue;
                }
                z->bits |= (p64)z->input.buf[z->input.at++] << z->count;
                z->count += 8;
        }
        return true;
}

static bipolar gzip_inflate_get(gzip_inflater address_to z, positive n)
{
        p32 value;

        if (!gzip_inflate_bits(z, n))
                return -1;
        if (z->count < n)
                return gzip_inflate_fail(z, "gzip truncated bitstream"), -1;
        value = (p32)z->bits & (((p32)1 << n) - 1);
        z->bits >>= n;
        z->count -= n;
        return (bipolar)value;
}

static bipolar gzip_inflate_byte(gzip_inflater address_to z)
{
        if (z->input.at >= z->input.have &&
            (!gzip_inflate_more(z, 1) || z->input.at >= z->input.have))
                return -1;
        return z->input.buf[z->input.at++];
}

static bool gzip_inflate_align(gzip_inflater address_to z)
{
        positive rewind = z->count >> 3;

        if (rewind > z->input.at)
                return gzip_inflate_fail(z, "gzip bit rewind");
        z->input.at -= rewind;
        z->bits = 0;
        z->count = 0;
        return true;
}

static bool gzip_inflate_table(gzip_inflater address_to z, p32 address_to table,
                               p8 address_to length, positive n, positive root,
                               positive kind)
{
        bipolar built = gzip_huffman_cells(table, length, n, root, kind);

        if (built == -1)
                return gzip_inflate_fail(z, "gzip Huffman length");
        if (built < 0)
                return gzip_inflate_fail(z, "gzip Huffman over-subscribed");
        return true;
}

static bool gzip_inflate_dynamic(gzip_inflater address_to z)
{
        p8 lengths[GZIP_MAXLIT + GZIP_MAXDIST];
        p8 clen[19];
        bipolar hlit = gzip_inflate_get(z, 5);
        bipolar hdist = gzip_inflate_get(z, 5);
        bipolar hclen = gzip_inflate_get(z, 4);
        positive nlit;
        positive ndist;
        positive at = 0;
        p8 last = 0;

        if (hlit < 0 || hdist < 0 || hclen < 0)
                return false;
        nlit = (positive)hlit + 257;
        ndist = (positive)hdist + 1;
        memory_fill(clen, 0, sizeof(clen));
        for (positive k = 0; k < (positive)hclen + 4; k++)
        {
                bipolar len = gzip_inflate_get(z, 3);

                if (len < 0)
                        return false;
                clen[gzip_clen_order[k]] = (p8)len;
        }
        z->fixed_loaded = false;
        if (!gzip_inflate_table(z, z->precode, clen, 19, GZIP_PRECODE_ROOT, 0))
                return false;

        /* The code lengths decode on a local bit buffer, refilled a word at a
           time while the window has eight bytes ahead; bits above count are
           stream bits already loaded and are cleared on the way out. */
        p64 bits = z->bits;
        positive count = z->count;

        while (at < nlit + ndist)
        {
                p32 cell;
                positive take;
                positive symbol;
                positive repeat;
                positive width;
                p8 fill;

                if (count < 14)
                {
                        if (z->input.have - z->input.at >= 8)
                        {
                                bits |= memory_load_unaligned(p64, z->input.buf + z->input.at) << count;
                                z->input.at += (63 - count) >> 3;
                                count |= 56;
                        }
                        else
                        {
                                z->bits = bits & (((p64)1 << count) - 1);
                                z->count = count;
                                if (!gzip_inflate_bits(z, 14))
                                        return false;
                                bits = z->bits;
                                count = z->count;
                        }
                }
                cell = z->precode[bits & 127];
                take = cell & 255;
                if ((cell & GZIP_CELL_EXCEPTIONAL) || take > count)
                        return gzip_inflate_fail(z, count < 7 ? "gzip truncated bitstream"
                                                              : "gzip bad Huffman code");
                bits >>= take;
                count -= take;
                symbol = cell >> 16;
                if (symbol < 16)
                {
                        lengths[at++] = (p8)symbol;
                        last = (p8)symbol;
                        continue;
                }
                width = symbol == 16 ? 2 : symbol == 17 ? 3 : 7;
                if (count < width)
                        return gzip_inflate_fail(z, "gzip truncated bitstream");
                repeat = (positive)(bits & (((p64)1 << width) - 1));
                bits >>= width;
                count -= width;
                if (symbol == 16)
                {
                        if (!at)
                                return gzip_inflate_fail(z, "gzip repeat with no length");
                        repeat += 3;
                        fill = last;
                }
                else
                {
                        repeat += symbol == 17 ? 3 : 11;
                        fill = 0;
                }
                if (at + repeat > nlit + ndist)
                        return gzip_inflate_fail(z, "gzip length overflow");
                memory_fill(lengths + at, fill, repeat);
                at += repeat;
                last = fill;
        }
        z->bits = bits & (((p64)1 << count) - 1);
        z->count = count;

        if (!lengths[256])
                return gzip_inflate_fail(z, "gzip missing end-of-block");
        return gzip_inflate_table(z, z->litlen, lengths, nlit, GZIP_LITLEN_ROOT, 1) &&
               gzip_inflate_table(z, z->offset, lengths + nlit, ndist, GZIP_OFFSET_ROOT, 2);
}

static bool gzip_inflate_fixed(gzip_inflater address_to z)
{
        p8 lengths[GZIP_MAXLIT + GZIP_MAXDIST];

        if (z->fixed_loaded)
                return true;
        memory_fill(lengths, 8, 144);
        memory_fill(lengths + 144, 9, 112);
        memory_fill(lengths + 256, 7, 24);
        memory_fill(lengths + 280, 8, 8);
        memory_fill(lengths + GZIP_MAXLIT, 5, GZIP_MAXDIST);
        if (!gzip_inflate_table(z, z->litlen, lengths, GZIP_MAXLIT, GZIP_LITLEN_ROOT, 1) ||
            !gzip_inflate_table(z, z->offset, lengths + GZIP_MAXLIT, GZIP_MAXDIST,
                                GZIP_OFFSET_ROOT, 2))
                return false;
        z->fixed_loaded = true;
        return true;
}

/* A root cell, or the subtable cell it points at; *root is the bits the
   pointer stood for. A literal's byte shares bits 8..15 with the flags, so
   the literal bit is always tested first. */
static p32 gzip_inflate_cell(const p32 address_to table, p64 bits, positive width,
                             positive address_to root)
{
        p32 cell = table[bits & (((positive)1 << width) - 1)];

        address_to root = 0;
        if (!(cell & GZIP_CELL_LITERAL) &&
            (cell & (GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SUBTABLE)) ==
            (GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SUBTABLE))
        {
                address_to root = width;
                cell = table[(cell >> 16) +
                             ((bits >> width) & ((1u << ((cell >> 8) & 15)) - 1))];
        }
        return cell;
}

/* One token, exactly: -1 failed, 0 decoded, 1 end of block. */
static bipolar gzip_inflate_token(gzip_inflater address_to z)
{
        positive root;
        positive take;
        positive code;
        positive length;
        positive distance;
        p64 made;
        p32 cell;

        if (!gzip_inflate_bits(z, 48))
                return -1;
        cell = gzip_inflate_cell(z->litlen, z->bits, GZIP_LITLEN_ROOT, address_of root);
        take = root + (cell & 255);
        if (cell & GZIP_CELL_LITERAL)
        {
                if (take > z->count)
                        return gzip_inflate_fail(z, "gzip truncated bitstream"), -1;
                z->bits >>= take;
                z->count -= take;
                z->out[z->fill++] = (p8)(cell >> 8);
                return 0;
        }
        if ((cell & (GZIP_CELL_EXCEPTIONAL | GZIP_CELL_END)) == GZIP_CELL_EXCEPTIONAL)
                return gzip_inflate_fail(z, (cell & GZIP_CELL_SYMBOL) && take <= z->count
                                                ? "gzip length symbol"
                                        : z->count < GZIP_MAXBITS ? "gzip truncated bitstream"
                                                                 : "gzip bad Huffman code"), -1;
        if (take > z->count)
                return gzip_inflate_fail(z, "gzip truncated bitstream"), -1;
        if (cell & GZIP_CELL_END)
        {
                z->bits >>= take;
                z->count -= take;
                return 1;
        }
        length = cell >> 16;
        z->bits >>= take;
        z->count -= take;

        cell = gzip_inflate_cell(z->offset, z->bits, GZIP_OFFSET_ROOT, address_of root);
        take = root + (cell & 255);
        if (cell & GZIP_CELL_EXCEPTIONAL)
                return gzip_inflate_fail(z, (cell & GZIP_CELL_SYMBOL) && take <= z->count
                                                ? "gzip distance symbol"
                                        : z->count < GZIP_MAXBITS ? "gzip truncated bitstream"
                                                                 : "gzip bad Huffman code"), -1;
        if (take > z->count)
                return gzip_inflate_fail(z, "gzip truncated bitstream"), -1;
        code = root + ((cell >> 8) & 255);
        distance = (cell >> 16) + ((z->bits >> code) & (((positive)1 << (take - code)) - 1));
        z->bits >>= take;
        z->count -= take;
        made = z->flushed + z->fill - z->member_start;
        if (distance > made)
                return gzip_inflate_fail(z, "gzip distance"), -1;
        memory_copy_match(z->out + z->fill, distance, length);
        z->fill += length;
        return 0;
}

static const string_address gzip_span_why[5] = {
        null, null, (string_address)"gzip bad Huffman code",
        (string_address)"gzip bad distance code", (string_address)"gzip distance"};

/* Codes to the end of the block: -1 failed, 0 slab full, 1 end of block. */
static bipolar gzip_inflate_codes(gzip_inflater address_to z)
{
        for (;;)
        {
                positive ahead;

                if (z->fill >= GZIP_DECODE_OUT)
                        return 0;
                ahead = z->input.have - z->input.at;
                if (ahead < 64 && !z->input.eof)
                {
                        if (!gzip_inflate_more(z, 64))
                                return -1;
                        ahead = z->input.have - z->input.at;
                }
                if (ahead >= GZIP_SPAN_IN)
                {
                        p8 address_to out = z->out + z->fill;
                        p64 made = z->flushed + z->fill - z->member_start;
                        gzip_decode_job job = {
                                z->bits, z->count,
                                z->input.buf + z->input.at, z->input.buf + z->input.have,
                                out, z->out + GZIP_DECODE_OUT + GZIP_DECODE_SLACK,
                                out - (made < GZIP_WINDOW ? made : GZIP_WINDOW),
                                z->litlen, z->offset, gzip_extra_masks, 0};

                        deflate_decode_span(address_of job);
                        z->bits = job.bits;
                        z->count = job.count;
                        z->input.at = (positive)(job.next - z->input.buf);
                        z->fill = (positive)(job.out - z->out);
                        if (job.status == 1)
                                return 1;
                        if (job.status)
                                return gzip_inflate_fail(z, gzip_span_why[job.status]), -1;
                        continue;
                }
                bipolar token = gzip_inflate_token(z);
                if (token)
                        return token;
        }
}

/* -1 failed, 0 slab full, 1 block done. */
static bipolar gzip_inflate_stored(gzip_inflater address_to z)
{
        if (!z->stored_open)
        {
                bipolar len;
                bipolar nlen;

                if (!gzip_inflate_align(z))
                        return -1;
                len = gzip_inflate_get(z, 16);
                nlen = gzip_inflate_get(z, 16);
                if (len < 0 || nlen < 0)
                        return -1;
                if ((p16)len != (p16)(~(p16)nlen))
                        return gzip_inflate_fail(z, "gzip stored length"), -1;
                z->stored_left = (positive)len;
                z->stored_open = true;
        }

        while (z->stored_left)
        {
                positive take;

                if (z->fill >= GZIP_DECODE_OUT)
                        return 0;
                if (z->input.at >= z->input.have &&
                    (!gzip_inflate_more(z, 1) || z->input.at >= z->input.have))
                        return gzip_inflate_fail(z, "gzip truncated stored block"), -1;
                take = z->input.have - z->input.at;
                if (take > z->stored_left)
                        take = z->stored_left;
                if (take > GZIP_DECODE_OUT - z->fill)
                        take = GZIP_DECODE_OUT - z->fill;
                memory_copy_apart(z->out + z->fill, z->input.buf + z->input.at, take);
                z->input.at += take;
                z->fill += take;
                z->stored_left -= take;
        }
        z->stored_open = false;
        return 1;
}

/* -1 failed, 0 slab full, 1 the member's last block is done. */
static bipolar gzip_inflate_blocks(gzip_inflater address_to z)
{
        for (;;)
        {
                bipolar done;

                if (!z->have_block)
                {
                        bipolar head = gzip_inflate_get(z, 3);

                        if (head < 0)
                                return -1;
                        z->block_last = head & 1;
                        if ((head >> 1) == 0)
                        {
                                z->stored_open = false;
                                z->block_kind = 0;
                        }
                        else if ((head >> 1) == 1)
                        {
                                if (!gzip_inflate_fixed(z))
                                        return -1;
                                z->block_kind = 1;
                        }
                        else if ((head >> 1) == 2)
                        {
                                if (!gzip_inflate_dynamic(z))
                                        return -1;
                                z->block_kind = 1;
                        }
                        else
                                return gzip_inflate_fail(z, "gzip reserved block type"), -1;
                        z->have_block = true;
                }
                done = z->block_kind ? gzip_inflate_codes(z) : gzip_inflate_stored(z);
                if (done <= 0)
                        return done;
                z->have_block = false;
                if (z->block_last)
                        return 1;
        }
}

/*      A header byte, and the header's CRC-32 carried over it: FHCRC puts
        the low sixteen bits of that sum after the header, and a header that
        does not match it is damaged, as gzip -d says -- read and thrown
        away, it let a damaged name, comment or extra field through. */
static bipolar gzip_head_byte(gzip_inflater address_to z, p32 address_to sum)
{
        bipolar byte = gzip_inflate_byte(z);

        if (byte >= 0)
        {
                p8 held = (p8)byte;

                address_to sum = hash_crc32(address_to sum, address_of held, 1);
        }
        return byte;
}

static bool gzip_inflate_skip_string(gzip_inflater address_to z, p32 address_to sum)
{
        bipolar byte;

        do
        {
                byte = gzip_head_byte(z, sum);
                if (byte < 0)
                        return gzip_inflate_fail(z, "gzip truncated header string");
        } while (byte);
        return true;
}

static bool gzip_inflate_word(gzip_inflater address_to z, p32 address_to word)
{
        address_to word = 0;
        for (positive at = 0; at < 32; at += 8)
        {
                bipolar byte = gzip_inflate_byte(z);

                if (byte < 0)
                        return false;
                address_to word |= (p32)byte << at;
        }
        return true;
}

/* -1 failed, 0 slab full, 1 member done. */
static bipolar gzip_inflate_member(gzip_inflater address_to z)
{
        p32 got_crc;
        p32 got_size;
        bipolar done;

        if (!z->head_done)
        {
                bipolar method;
                bipolar flags;
                p32 sum = 0xffffffffu;

                if (gzip_head_byte(z, address_of sum) != GZIP_MAGIC0 ||
                    gzip_head_byte(z, address_of sum) != GZIP_MAGIC1)
                        return gzip_inflate_fail(z, "gzip bad magic"), -1;
                method = gzip_head_byte(z, address_of sum);
                flags = gzip_head_byte(z, address_of sum);
                if (method != GZIP_METHOD)
                        return gzip_inflate_fail(z, "gzip method is not deflate"), -1;
                if (flags < 0)
                        return gzip_inflate_fail(z, "gzip truncated header"), -1;
                if ((p8)flags & 0xe0)
                        return gzip_inflate_fail(z, "gzip reserved header flags"), -1;
                for (positive at = 0; at < 6; at++)
                        if (gzip_head_byte(z, address_of sum) < 0)
                                return gzip_inflate_fail(z, "gzip truncated header"), -1;
                if ((p8)flags & GZIP_FEXTRA)
                {
                        bipolar xlen = gzip_head_byte(z, address_of sum);
                        bipolar xlen_hi = gzip_head_byte(z, address_of sum);
                        bipolar extra;

                        if (xlen < 0 || xlen_hi < 0)
                                return gzip_inflate_fail(z, "gzip truncated extra"), -1;
                        extra = xlen + (xlen_hi << 8);
                        while (extra--)
                                if (gzip_head_byte(z, address_of sum) < 0)
                                        return gzip_inflate_fail(z, "gzip truncated extra"), -1;
                }
                if (((p8)flags & GZIP_FNAME) &&
                    !gzip_inflate_skip_string(z, address_of sum))
                        return -1;
                if (((p8)flags & GZIP_FCOMMENT) &&
                    !gzip_inflate_skip_string(z, address_of sum))
                        return -1;
                if ((p8)flags & GZIP_FHCRC)
                {
                        bipolar low = gzip_inflate_byte(z);
                        bipolar high = gzip_inflate_byte(z);

                        if (low < 0 || high < 0)
                                return gzip_inflate_fail(z, "gzip truncated header crc"), -1;
                        if ((p32)(low | high << 8) != ((sum ^ 0xffffffffu) & 0xffff))
                                return gzip_inflate_fail(z, "gzip header crc mismatch"), -1;
                }
                z->member_start = z->flushed + z->fill;
                z->crc = 0xffffffffu;
                z->crc_at = z->fill;
                z->bits = 0;
                z->count = 0;
                z->have_block = false;
                z->stored_open = false;
                z->head_done = true;
        }

        done = gzip_inflate_blocks(z);
        if (done <= 0)
                return done;
        if (!gzip_inflate_align(z))
                return -1;
        z->crc = hash_crc32(z->crc, z->out + z->crc_at, z->fill - z->crc_at);
        z->crc_at = z->fill;
        if (!gzip_inflate_word(z, address_of got_crc) ||
            !gzip_inflate_word(z, address_of got_size))
                return gzip_inflate_fail(z, "gzip truncated trailer"), -1;
        if (got_crc != ~z->crc)
                return gzip_inflate_fail(z, "gzip crc mismatch"), -1;
        if (got_size != (p32)(z->flushed + z->fill - z->member_start))
                return gzip_inflate_fail(z, "gzip length mismatch"), -1;
        z->head_done = false;
        z->members++;
        return 1;
}

/* Decode until the slab is full or the stream ends; false once failed. */
static bool gzip_inflate_run(gzip_inflater address_to z)
{
        if (z->why)
                return false;
        while (!z->finished && z->fill < GZIP_DECODE_OUT)
        {
                if (!z->head_done)
                {
                        if (z->input.at >= z->input.have && !gzip_inflate_more(z, 1))
                                return false;
                        if (z->input.at >= z->input.have)
                        {
                                if (!z->members)
                                        return gzip_inflate_fail(z, "gzip empty input");
                                z->finished = true;
                                break;
                        }
                }
                if (gzip_inflate_member(z) < 0)
                        return false;
        }
        return true;
}

/* Account the slab's bytes and keep the last 32 KiB as history. */
static fn gzip_inflate_slide(gzip_inflater address_to z)
{
        if (z->crc_at < z->fill)
                z->crc = hash_crc32(z->crc, z->out + z->crc_at, z->fill - z->crc_at);
        memory_copy(z->out - GZIP_WINDOW, z->out + z->fill - GZIP_WINDOW, GZIP_WINDOW);
        z->flushed += z->fill;
        z->fill = 0;
        z->taken = 0;
        z->crc_at = 0;
}

/* The pull interface. State comes from memory() and is freed by close. */
static address_any gzip_pull_open(bipolar fd, p8 address_to prefix, positive prefix_len)
{
        gzip_inflater address_to z;

        if (prefix_len > GZIP_DECODE_IN)
                return null;
        z = gzip_inflater_new();
        if (!z)
                return null;
        z->input.fd = fd;
        if (prefix_len)
                memory_copy_apart(z->input.buf, prefix, prefix_len);
        z->input.have = prefix_len;
        return z;
}

static bipolar gzip_pull_read(address_any state, p8 address_to into, positive n)
{
        gzip_inflater address_to z = (gzip_inflater address_to)state;
        positive copied = 0;

        while (copied < n)
        {
                if (z->taken < z->fill)
                {
                        positive take = z->fill - z->taken;

                        if (take > n - copied)
                                take = n - copied;
                        memory_copy_apart(into + copied, z->out + z->taken, take);
                        z->taken += take;
                        copied += take;
                        continue;
                }
                if (z->finished)
                        break;
                if (z->fill)
                        gzip_inflate_slide(z);
                if (!gzip_inflate_run(z))
                        return -1;
        }
        return (bipolar)copied;
}

static bool gzip_pull_close(address_any state)
{
        gzip_inflater address_to z = (gzip_inflater address_to)state;
        bool ok;

        if (!z)
                return false;
        ok = !z->why;
        memory_free(z, GZIP_INFLATER_SIZE);
        return ok;
}

/* The whole stream from in to out (out < 0 tests without writing). */
static bool gzip_stream_decode(bipolar in, bipolar out)
{
        gzip_inflater address_to z = (gzip_inflater address_to)gzip_pull_open(in, null, 0);
        bool ok;

        if (!z)
                return gzip_fail("gzip cannot map the decoder");
        for (;;)
        {
                ok = gzip_inflate_run(z);
                if (!ok)
                        break;
                if (out >= 0 && z->fill &&
                    system_write_all((positive)out, z->out, z->fill) != z->fill)
                {
                        ok = gzip_inflate_fail(z, "gzip write failed");
                        break;
                }
                if (z->finished)
                        break;
                gzip_inflate_slide(z);
        }
        gzip_why = z->why;
        memory_free(z, GZIP_INFLATER_SIZE);
        return ok;
}

static bipolar gzip_inflate_mem(p8 address_to src, positive src_len,
                                p8 address_to dst, positive dst_cap)
{
        gzip_inflater address_to z = gzip_inflater_new();
        positive used = 0;
        bool ok;

        if (!z)
                return gzip_fail("gzip cannot map the decoder"), -1;
        byte_input_open_memory(address_of z->input, src, src_len, z->input.buf,
                               GZIP_DECODE_IN);
        for (;;)
        {
                ok = gzip_inflate_run(z);
                if (!ok)
                        break;
                if (z->fill > dst_cap - used)
                {
                        ok = gzip_inflate_fail(z, "gzip output is too small");
                        break;
                }
                memory_copy_apart(dst + used, z->out, z->fill);
                used += z->fill;
                if (z->finished)
                        break;
                gzip_inflate_slide(z);
        }
        gzip_why = z->why;
        memory_free(z, GZIP_INFLATER_SIZE);
        return ok ? (bipolar)used : -1;
}

/*
        Encoder.

        Input is deflated in fixed blocks of GZIP_BLOCK bytes. A gzip_encoder
        sees the 32 KiB before its block as history, hashed but never
        emitted, so matches still reach back across the cut, and it ends the
        block on a byte boundary with an empty stored block. Blocks can so be
        deflated side by side and concatenated as they are; the stream closes
        with an empty final fixed block. Block size, history and every
        boundary come from the input alone, never from how many blocks are
        deflated at once or how the input arrived.

        Blocks are 64 KiB, as pigz's are 128 KiB, so the input and output a
        worker holds stay small. An encoder handed the block after the one
        it did last keeps its finder instead of hashing the history again:
        it already holds every position of that history but the few it
        could not take at the end, and the bytes come out the same, which
        is what makes the small blocks free on one core.
*/

#define GZIP_BLOCK (1u << 16)
/* A deflate block holds at most this many pairs and, once it is past
   GZIP_SPLIT_MOST bytes, ends at the next token. */
#define GZIP_PAIRS 16384
#define GZIP_SPLIT_LEAST 5000
#define GZIP_SPLIT_MOST 300000
#define GZIP_SPLIT_CHECK 512
#define GZIP_HASH4_BITS 16
#define GZIP_HASH3_BITS 15
#define GZIP_FAST_BITS 15

/* How each level parses: the fast finder's buckets, or chains read
   greedily, lazily one position on, or lazily two on; how many chain
   links a search may follow, and the length that ends it. */
#define GZIP_PARSE_FAST 0
#define GZIP_PARSE_GREEDY 1
#define GZIP_PARSE_LAZY 2
#define GZIP_PARSE_LAZY2 3

typedef struct
{
        p8 parse;
        p16 depth;
        p16 nice;
} gzip_level_shape;

static const gzip_level_shape gzip_levels[10] = {
        {GZIP_PARSE_LAZY, 35, 65},
        {GZIP_PARSE_FAST, 2, 32},
        {GZIP_PARSE_GREEDY, 6, 10},
        {GZIP_PARSE_GREEDY, 12, 14},
        {GZIP_PARSE_GREEDY, 16, 30},
        {GZIP_PARSE_LAZY, 16, 30},
        {GZIP_PARSE_LAZY, 35, 65},
        {GZIP_PARSE_LAZY, 100, 130},
        {GZIP_PARSE_LAZY2, 300, 258},
        {GZIP_PARSE_LAZY2, 600, 258}};

typedef struct
{
        p8 level;
        /* history bytes, then the block: base[0, total) */
        p8 address_to base;
        positive total;
        /* The finders' positions are 16-bit, relative to slid, the
           absolute position of relative zero; every 32 KiB slid moves on and
           the tables with it, so a stale position falls to -32768, which
           no search takes. The chain finder keeps the newest position of
           each four-byte hash, of each three-byte hash, and the one before
           of the same four-byte hash. hash3 and hash4 are the next
           position's hashes, worked out and fetched a position early. */
        positive slid;
        p32 hash3;
        p32 hash4;
        /* The first position the finder did not take: every one before it
           went in, none after it did. The block that follows takes the rest
           and needs no history pass. */
        positive mark;
        /* The stream's number of the input block deflated last. */
        positive last;
        union
        {
                struct
                {
                        b16 head4[1u << GZIP_HASH4_BITS];
                        b16 head3[1u << GZIP_HASH3_BITS];
                        b16 prev[GZIP_WINDOW];
                };
                /* The fast finder's buckets: absolute positions plus one,
                   zero for none, two a hash. */
                p32 fast[2u << GZIP_FAST_BITS];
        };
        /* The open deflate block: its pairs and symbol counts, and what
           kind of symbols it has seen for the split test. */
        positive pairs;
        p32 mpos[GZIP_PAIRS];
        p16 mlen[GZIP_PAIRS];
        p16 mdist[GZIP_PAIRS];
        p32 lit_freq[GZIP_MAXLIT];
        p32 dist_freq[GZIP_MAXDIST];
        p32 seen[10];
        p32 fresh[10];
        positive seen_n;
        positive fresh_n;
        p8 address_to out;
        positive out_room;
        positive out_n;
        p64 bits;
        p32 bitn;
} gzip_encoder;

static inline INLINE fn gzip_put_bits(gzip_encoder address_to e, p32 value, p32 n)
{
        e->bits |= (p64)value << e->bitn;
        e->bitn += n;
        if (e->bitn >= 32)
        {
                memory_store_unaligned(p32, e->out + e->out_n, (p32)e->bits);
                e->out_n += 4;
                e->bits >>= 32;
                e->bitn -= 32;
        }
}

static fn gzip_bits_align(gzip_encoder address_to e)
{
        while (e->bitn)
        {
                e->out[e->out_n++] = (p8)e->bits;
                e->bits >>= 8;
                e->bitn = e->bitn > 8 ? e->bitn - 8 : 0;
        }
        e->bits = 0;
}

static inline INLINE positive gzip_low_bit(p64 value)
{
#if X64 || ARM64
        return (positive)__builtin_ctzll(value);
#else
        return (positive)bits_trailing_zeros(value);
#endif
}

static inline INLINE positive gzip_high_bit(p32 value)
{
#if X64 || ARM64
        return 31 - (positive)__builtin_clz(value);
#else
        return 63 - (positive)bits_leading_zeros((p64)value);
#endif
}

/* The finders' hashes of the four and three bytes at a position. */
static inline INLINE p32 gzip_hash(p32 bytes, p32 bits)
{
        return (bytes * 0x1e35a7bdu) >> (32 - bits);
}

/* How far here and there agree, from at up to limit. */
static inline INLINE positive gzip_extend(p8 address_to here, p8 address_to there,
                                          positive at, positive limit)
{
        while (at + 8 <= limit)
        {
                p64 x = memory_load_unaligned(p64, here + at) ^
                        memory_load_unaligned(p64, there + at);

                if (x)
                        return at + (gzip_low_bit(x) >> 3);
                at += 8;
        }
        while (at < limit && here[at] == there[at])
                at++;
        return at;
}

static inline INLINE positive gzip_dist_code(positive dist)
{
        return dist <= 256 ? deflate_symbol_tab[256 + dist - 1]
                           : deflate_symbol_tab[512 + ((dist - 1) >> 7)];
}

static inline INLINE fn gzip_note_literal(gzip_encoder address_to e, p8 c)
{
        e->lit_freq[c]++;
        e->fresh[((c >> 5) & 6) | (c & 1)]++;
        e->fresh_n++;
}

/* The fast level's literal and pair: counted, never observed, as its
   blocks end at a fixed length. */
static inline INLINE fn gzip_note_literal_fast(gzip_encoder address_to e, p8 c)
{
        e->lit_freq[c]++;
}

static inline INLINE fn gzip_note_match_fast(gzip_encoder address_to e, positive at,
                                             positive length, positive dist)
{
        positive k = e->pairs++;

        e->mpos[k] = (p32)at;
        e->mlen[k] = (p16)length;
        e->mdist[k] = (p16)dist;
        e->lit_freq[257 + deflate_symbol_tab[length - 3]]++;
        e->dist_freq[gzip_dist_code(dist)]++;
}

static inline INLINE fn gzip_note_match(gzip_encoder address_to e, positive at,
                                        positive length, positive dist)
{
        positive k = e->pairs++;

        e->mpos[k] = (p32)at;
        e->mlen[k] = (p16)length;
        e->mdist[k] = (p16)dist;
        e->lit_freq[257 + deflate_symbol_tab[length - 3]]++;
        e->dist_freq[gzip_dist_code(dist)]++;
        e->fresh[8 + (length >= 9)]++;
        e->fresh_n++;
}

static fn gzip_block_open(gzip_encoder address_to e)
{
        e->pairs = 0;
        memory_fill(e->lit_freq, 0, sizeof(e->lit_freq));
        memory_fill(e->dist_freq, 0, sizeof(e->dist_freq));
        memory_fill(e->seen, 0, sizeof(e->seen));
        memory_fill(e->fresh, 0, sizeof(e->fresh));
        e->seen_n = 0;
        e->fresh_n = 0;
}

/* Whether the symbols of the last GZIP_SPLIT_CHECK tokens differ enough
   from the block's earlier ones to start a new block: libdeflate's test,
   the sum of the differences of the kinds' shares. */
static bool gzip_split_test(gzip_encoder address_to e, positive length, positive left)
{
        if (length < GZIP_SPLIT_LEAST || left < GZIP_SPLIT_LEAST)
                return false;
        if (e->seen_n)
        {
                p64 delta = 0;
                p64 items = e->seen_n + e->fresh_n;
                p64 cutoff = (p64)e->fresh_n * 200 / 512 * e->seen_n;

                for (positive i = 0; i < 10; i++)
                {
                        p64 expected = (p64)e->seen[i] * e->fresh_n;
                        p64 actual = (p64)e->fresh[i] * e->seen_n;

                        delta += actual > expected ? actual - expected : expected - actual;
                }
                if (length < 10000 && items < 8192)
                        cutoff += cutoff * (8192 - items) / 8192;
                if (delta + (length / 4096) * e->seen_n >= cutoff)
                        return true;
        }
        for (positive i = 0; i < 10; i++)
        {
                e->seen[i] += e->fresh[i];
                e->fresh[i] = 0;
        }
        e->seen_n += e->fresh_n;
        e->fresh_n = 0;
        return false;
}

/* Whether the open block ends before the token at pos. */
static inline INLINE bool gzip_block_full(gzip_encoder address_to e, positive start,
                                          positive pos)
{
        if (e->pairs >= GZIP_PAIRS - 2 || pos - start >= GZIP_SPLIT_MOST)
                return true;
        return e->fresh_n >= GZIP_SPLIT_CHECK &&
               gzip_split_test(e, pos - start, e->total - pos);
}

static bool gzip_lengths_ok(p8 address_to length, positive n, p8 limit)
{
        p16 count[GZIP_MAXBITS + 1];

        return gzip_code_space(length, n, limit, count) == 0;
}

static bool gzip_used_coded(p32 address_to freq, p8 address_to length, positive n)
{
        positive at;

        for (at = 0; at < n; at++)
                if (freq[at] && !length[at])
                        return false;
        return true;
}

/* The deflate_tokens job: a block's tokens for lib.c to write. */
typedef struct
{
        p8 address_to src;
        p32 address_to mpos;
        p16 address_to mlen;
        p16 address_to mdist;
        positive length;
        positive pairs;
        p32 address_to lit;
        p32 address_to dist;
        p8 address_to out;
        p64 bits;
        positive bitn;
} gzip_tokens;

/* Stored blocks of at most 65535 bytes; length zero is the byte-aligning
   empty block that ends every deflated input block. */
static fn gzip_write_stored(gzip_encoder address_to e, p8 address_to src, positive length,
                            bool last)
{
        do
        {
                positive chunk = length > 65535 ? 65535 : length;

                gzip_put_bits(e, last && chunk == length, 1);
                gzip_put_bits(e, 0, 2);
                gzip_bits_align(e);
                e->out[e->out_n++] = (p8)chunk;
                e->out[e->out_n++] = (p8)(chunk >> 8);
                e->out[e->out_n++] = (p8)~chunk;
                e->out[e->out_n++] = (p8)(~chunk >> 8);
                if (chunk)
                        memory_copy_apart(e->out + e->out_n, src, chunk);
                e->out_n += chunk;
                src += chunk;
                length -= chunk;
        } while (length);
}

static fn gzip_fixed_init(void)
{
        positive at;

        if (gzip_fixed_codes)
                return;
        for (at = 0; at <= 143; at++)
                gzip_fixed_lit_len[at] = 8;
        for (; at <= 255; at++)
                gzip_fixed_lit_len[at] = 9;
        for (; at <= 279; at++)
                gzip_fixed_lit_len[at] = 7;
        for (; at <= 287; at++)
                gzip_fixed_lit_len[at] = 8;
        for (at = 0; at < GZIP_MAXDIST; at++)
                gzip_fixed_dist_len[at] = 5;
        huffman_codes(gzip_fixed_lit_len, GZIP_MAXLIT, gzip_fixed_lit_code);
        huffman_codes(gzip_fixed_dist_len, GZIP_MAXDIST, gzip_fixed_dist_code);
        gzip_fixed_codes = true;
}

/* The tokens of one deflate block: literals from src, and pairs whose
   positions are offsets into src, in order, then the end of block. */
static fn gzip_write_tokens(gzip_encoder address_to e, p8 address_to src, positive length,
                            p32 address_to lit, p32 address_to dist)
{
        gzip_tokens j;

        j.src = src;
        j.mpos = e->mpos;
        j.mlen = e->mlen;
        j.mdist = e->mdist;
        j.length = length;
        j.pairs = e->pairs;
        j.lit = lit;
        j.dist = dist;
        j.out = e->out + e->out_n;
        j.bits = e->bits;
        j.bitn = e->bitn;
        deflate_tokens_encode(address_of j);
        e->out_n = (positive)(j.out - e->out);
        e->bits = j.bits;
        e->bitn = (p32)j.bitn;
}

static fn gzip_write_fixed(gzip_encoder address_to e, p8 address_to src, positive length,
                           bool last)
{
        gzip_put_bits(e, last ? 1 : 0, 1);
        gzip_put_bits(e, 1, 2);
        gzip_write_tokens(e, src, length, gzip_fixed_lit_code, gzip_fixed_dist_code);
}

/* The open block as a dynamic block, or fixed or stored when the tree
   would not pay; then a new block is open. */
static fn gzip_block_emit(gzip_encoder address_to e, p8 address_to src, positive length,
                          bool last)
{
        p32 address_to lit_freq = e->lit_freq;
        p32 address_to dist_freq = e->dist_freq;
        p8 lit_len[GZIP_MAXLIT];
        p8 dist_len[GZIP_MAXDIST];
        p32 lit_code[GZIP_MAXLIT];
        p32 dist_code[GZIP_MAXDIST];
        positive bits = 0, extra_bits = 0, fixed_bits = 3;
        positive chunks = length ? (length + 65534) / 65535 : 1;
        positive stored_bits = length * 8 + chunks * 40 +
                               ((8 - ((e->bitn + 3) & 7)) & 7) - 5;

        lit_freq[256] = 1;
        for (positive i = 0; i < 29; i++)
                extra_bits += lit_freq[257 + i] * gzip_len_extra[i];
        for (positive i = 0; i < 30; i++)
                extra_bits += dist_freq[i] * gzip_dist_extra[i];

        fixed_bits += extra_bits;
        for (positive i = 0; i < GZIP_MAXLIT; i++)
                fixed_bits += lit_freq[i] * gzip_fixed_lit_len[i];
        for (positive i = 0; i < GZIP_MAXDIST; i++)
                fixed_bits += dist_freq[i] * 5;

        huffman_lengths(lit_freq, GZIP_MAXLIT, lit_len, GZIP_MAXBITS);
        huffman_lengths(dist_freq, GZIP_MAXDIST, dist_len, GZIP_MAXBITS);
        if (!lit_len[256])
                lit_len[256] = 1;
        {
                bool any_dist = false;

                for (positive i = 0; i < GZIP_MAXDIST; i++)
                        if (dist_len[i])
                                any_dist = true;
                if (!any_dist)
                        dist_len[0] = 1;
        }
        if (!gzip_lengths_ok(lit_len, GZIP_MAXLIT, GZIP_MAXBITS) ||
            !gzip_lengths_ok(dist_len, GZIP_MAXDIST, GZIP_MAXBITS) ||
            !gzip_used_coded(lit_freq, lit_len, GZIP_MAXLIT) ||
            !gzip_used_coded(dist_freq, dist_len, GZIP_MAXDIST))
        {
                if (stored_bits <= fixed_bits)
                        gzip_write_stored(e, src, length, last);
                else
                        gzip_write_fixed(e, src, length, last);
                gzip_block_open(e);
                return;
        }
        huffman_codes(lit_len, GZIP_MAXLIT, lit_code);
        huffman_codes(dist_len, GZIP_MAXDIST, dist_code);

        bits = extra_bits;
        for (positive i = 0; i < GZIP_MAXLIT; i++)
                bits += lit_freq[i] * lit_len[i];
        for (positive i = 0; i < GZIP_MAXDIST; i++)
                bits += dist_freq[i] * dist_len[i];

        p8 clen[19];
        p32 ccode[19];
        p32 cfreq[19];
        p8 seq[288 + 32];
        positive nseq = 0;
        positive i;
        positive run;
        positive hlit = 286, hdist = 30, hclen = 19;
        p8 here;

        hlit -= memory_span_byte_reverse(lit_len + 257, 0, hlit - 257);
        hdist -= memory_span_byte_reverse(dist_len + 1, 0, hdist - 1);
        memory_copy(seq, lit_len, hlit);
        memory_copy(seq + hlit, dist_len, hdist);
        nseq = hlit + hdist;

        memory_fill(cfreq, 0, sizeof(cfreq));
        i = 0;
        while (i < nseq)
        {
                here = seq[i];
                run = 1;
                i++;
                while (i < nseq && seq[i] == here && run < 138)
                {
                        run++;
                        i++;
                }
                if (here)
                {
                        cfreq[here]++;
                        run--;
                        while (run >= 3)
                        {
                                positive take = run > 6 ? 6 : run;

                                cfreq[16]++;
                                run -= take;
                        }
                        cfreq[here] += run;
                }
                else if (run >= 11)
                        cfreq[18]++;
                else if (run >= 3)
                        cfreq[17]++;
                else
                        cfreq[0] += run;
        }
        huffman_lengths(cfreq, 19, clen, 7);
        if (!gzip_lengths_ok(clen, 19, 7) || !gzip_used_coded(cfreq, clen, 19))
        {
                if (stored_bits <= fixed_bits)
                        gzip_write_stored(e, src, length, last);
                else
                        gzip_write_fixed(e, src, length, last);
                gzip_block_open(e);
                return;
        }
        huffman_codes(clen, 19, ccode);

        while (hclen > 4 && !clen[gzip_clen_order[hclen - 1]])
                hclen--;
        bits += 17 + hclen * 3 + cfreq[16] * 2 + cfreq[17] * 3 + cfreq[18] * 7;
        for (positive c = 0; c < 19; c++)
                bits += cfreq[c] * clen[c];
        if (stored_bits <= bits && stored_bits <= fixed_bits)
        {
                gzip_write_stored(e, src, length, last);
                gzip_block_open(e);
                return;
        }
        if (fixed_bits <= bits)
        {
                gzip_write_fixed(e, src, length, last);
                gzip_block_open(e);
                return;
        }
        gzip_put_bits(e, last ? 1 : 0, 1);
        gzip_put_bits(e, 2, 2);
        gzip_put_bits(e, (p32)(hlit - 257), 5);
        gzip_put_bits(e, (p32)(hdist - 1), 5);
        gzip_put_bits(e, (p32)(hclen - 4), 4);
        for (i = 0; i < hclen; i++)
                gzip_put_bits(e, clen[gzip_clen_order[i]], 3);

        i = 0;
        while (i < nseq)
        {
                here = seq[i];
                run = 1;
                i++;
                while (i < nseq && seq[i] == here && run < 138)
                {
                        run++;
                        i++;
                }
                if (here)
                {
                        gzip_put_bits(e, ccode[here] & 0xffff, ccode[here] >> 16);
                        run--;
                        while (run >= 3)
                        {
                                positive take = run > 6 ? 6 : run;

                                gzip_put_bits(e, ccode[16] & 0xffff, ccode[16] >> 16);
                                gzip_put_bits(e, (p32)(take - 3), 2);
                                run -= take;
                        }
                        while (run)
                        {
                                gzip_put_bits(e, ccode[here] & 0xffff, ccode[here] >> 16);
                                run--;
                        }
                }
                else if (run >= 11)
                {
                        gzip_put_bits(e, ccode[18] & 0xffff, ccode[18] >> 16);
                        gzip_put_bits(e, (p32)(run - 11), 7);
                }
                else if (run >= 3)
                {
                        gzip_put_bits(e, ccode[17] & 0xffff, ccode[17] >> 16);
                        gzip_put_bits(e, (p32)(run - 3), 3);
                }
                else
                        while (run)
                        {
                                gzip_put_bits(e, ccode[0] & 0xffff, ccode[0] >> 16);
                                run--;
                        }
        }
        gzip_write_tokens(e, src, length, lit_code, dist_code);
        gzip_block_open(e);
}

/* Every table position down 32 KiB, and -32768 for any that falls out. */
static fn gzip_slide(gzip_encoder address_to e)
{
        b16 address_to t = e->head4;
        positive n = (sizeof(e->head4) + sizeof(e->head3) + sizeof(e->prev)) / sizeof(b16);

        for (positive i = 0; i < n; i++)
                t[i] = (b16)((t[i] & ~(t[i] >> 15)) | (b16)0x8000);
        e->slid += GZIP_WINDOW;
}

static fn gzip_finder_open(gzip_encoder address_to e)
{
        b16 address_to t = e->head4;
        positive n = (sizeof(e->head4) + sizeof(e->head3) + sizeof(e->prev)) / sizeof(b16);

        for (positive i = 0; i < n; i++)
                t[i] = (b16)0x8000;
        e->slid = 0;
        e->hash3 = 0;
        e->hash4 = 0;
}

/* pos relative to slid, sliding first when it is a window on. */
static inline INLINE b32 gzip_relative(gzip_encoder address_to e, positive pos)
{
        if (pos - e->slid >= GZIP_WINDOW)
                gzip_slide(e);
        return (b32)(pos - e->slid);
}

/* The next position's hashes, and their table cells fetched. */
static inline INLINE fn gzip_chain_ahead(gzip_encoder address_to e, p32 address_to h3,
                                         p32 address_to h4, p8 address_to next)
{
        p32 seq = memory_load_unaligned(p32, next);

        address_to h3 = gzip_hash(seq << 8, GZIP_HASH3_BITS);
        address_to h4 = gzip_hash(seq, GZIP_HASH4_BITS);
        __builtin_prefetch(e->head3 + address_to h3, 1);
        __builtin_prefetch(e->head4 + address_to h4, 1);
}

/* Chain finder: pos goes in, and the longest match longer than best
   within depth links comes out, its distance in *dist. *h3 and *h4 hold
   pos's hashes and leave with pos + 1's. Five bytes must follow pos:
   nothing goes in and nothing is found with fewer. */
static inline INLINE positive gzip_chain_find(gzip_encoder address_to e, positive pos,
                                              positive best, positive most,
                                              positive nice, positive depth,
                                              p32 address_to h3, p32 address_to h4,
                                              positive address_to dist)
{
        if (most < 5)
        {
                if (pos < e->mark)
                        e->mark = pos;
                return best;
        }
        if (nice > most)
                nice = most;

        b32 rel = gzip_relative(e, pos);
        b32 floor = rel - (b32)GZIP_WINDOW;
        p8 address_to window = e->base + e->slid;
        p8 address_to here = e->base + pos;
        p32 seq = memory_load_unaligned(p32, here);
        b32 node3 = e->head3[address_to h3];
        b32 node = e->head4[address_to h4];
        p8 address_to match;

        e->head3[address_to h3] = (b16)rel;
        e->head4[address_to h4] = (b16)rel;
        e->prev[rel] = (b16)node;
        gzip_chain_ahead(e, h3, h4, here + 1);
        if (best < 4)
        {
                if (node3 <= floor)
                        return best;
                if (best < 3 &&
                    ((memory_load_unaligned(p32, window + node3) ^ seq) & 0xffffff) == 0)
                {
                        best = 3;
                        address_to dist = (positive)(rel - node3);
                }
                for (;;)
                {
                        if (node <= floor)
                                return best;
                        match = window + node;
                        if (memory_load_unaligned(p32, match) == seq)
                                break;
                        node = e->prev[node & GZIP_WMASK];
                        if (!--depth)
                                return best;
                }
                best = gzip_extend(here, match, 4, most);
                address_to dist = (positive)(here - match);
                if (best >= nice)
                        return best;
                node = e->prev[node & GZIP_WMASK];
                if (!--depth)
                        return best;
        }
        else if (best >= nice || best >= most)
                return best;
        for (;;)
        {
                for (;;)
                {
                        if (node <= floor)
                                return best;
                        match = window + node;
                        if (memory_load_unaligned(p32, match + best - 3) ==
                                    memory_load_unaligned(p32, here + best - 3) &&
                            memory_load_unaligned(p32, match) == seq)
                                break;
                        node = e->prev[node & GZIP_WMASK];
                        if (!--depth)
                                return best;
                }
                positive length = gzip_extend(here, match, 4, most);

                if (length > best)
                {
                        best = length;
                        address_to dist = (positive)(here - match);
                        if (best >= nice)
                                return best;
                }
                node = e->prev[node & GZIP_WMASK];
                if (!--depth)
                        return best;
        }
}

/* The chain finder takes count positions from pos without a search, or
   none when fewer than five bytes would follow the last. */
static inline INLINE fn gzip_chain_skip(gzip_encoder address_to e, positive pos,
                                        positive count, p32 address_to h3,
                                        p32 address_to h4)
{
        if (pos + count + 5 > e->total)
        {
                if (pos < e->mark)
                        e->mark = pos;
                return;
        }
        p32 a = address_to h3, b = address_to h4;

        for (; count; count--, pos++)
        {
                b32 rel = gzip_relative(e, pos);
                p32 seq = memory_load_unaligned(p32, e->base + pos + 1);

                e->head3[a] = (b16)rel;
                e->prev[rel] = e->head4[b];
                e->head4[b] = (b16)rel;
                a = gzip_hash(seq << 8, GZIP_HASH3_BITS);
                b = gzip_hash(seq, GZIP_HASH4_BITS);
        }
        __builtin_prefetch(e->head3 + a, 1);
        __builtin_prefetch(e->head4 + b, 1);
        address_to h3 = a;
        address_to h4 = b;
}

/* The fast finder takes count positions from pos without a search. */
static inline INLINE fn gzip_fast_skip(gzip_encoder address_to e, positive pos,
                                       positive count, p32 address_to h)
{
        if (pos + count + 5 > e->total)
        {
                if (pos < e->mark)
                        e->mark = pos;
                return;
        }
        p32 a = address_to h;
        p32 address_to fast = e->fast;
        p8 address_to base = e->base;

        for (; count; count--, pos++)
        {
                fast[2 * a + 1] = fast[2 * a];
                fast[2 * a] = (p32)(pos + 1);
                a = gzip_hash(memory_load_unaligned(p32, base + pos + 1), GZIP_FAST_BITS);
        }
        __builtin_prefetch(fast + 2 * a, 1);
        address_to h = a;
}

/* Tokens from pos until the block is done or limit is reached: the
   position where the open block now ends. The fast level's blocks end at
   64 KiB or 8192 pairs, as libdeflate's fastest level does. */
#define GZIP_FAST_BLOCK 65535
#define GZIP_FAST_PAIRS 8192

static positive gzip_parse_fast(gzip_encoder address_to e, positive start, positive pos,
                                positive limit, positive nice)
{
        p8 address_to base = e->base;
        p32 address_to fast = e->fast;
        positive total = e->total;
        p32 h = e->hash4;

        if (limit - start >= GZIP_FAST_BLOCK + GZIP_SPLIT_LEAST)
                limit = start + GZIP_FAST_BLOCK;
        while (pos < limit && e->pairs < GZIP_FAST_PAIRS)
        {
                positive most = total - pos;

                if (most < 5)
                {
                        if (pos < e->mark)
                                e->mark = pos;
                        while (pos < limit)
                                gzip_note_literal_fast(e, base[pos++]);
                        break;
                }
                if (most > GZIP_MAX_MATCH)
                        most = GZIP_MAX_MATCH;

                p8 address_to here = base + pos;
                p32 seq = memory_load_unaligned(p32, here);
                p32 floor = pos + 1 > GZIP_WINDOW ? (p32)(pos + 1 - GZIP_WINDOW) : 1;
                p32 first = fast[2 * h];
                p32 second = fast[2 * h + 1];
                positive best = 0, dist = 0;

                fast[2 * h] = (p32)(pos + 1);
                fast[2 * h + 1] = first;
                h = gzip_hash(memory_load_unaligned(p32, here + 1), GZIP_FAST_BITS);
                __builtin_prefetch(fast + 2 * h, 1);
                if (first >= floor && memory_load_unaligned(p32, base + first - 1) == seq)
                {
                        best = gzip_extend(here, base + first - 1, 4, most);
                        dist = pos + 1 - first;
                        if (best < nice && best < most && second >= floor &&
                            memory_load_unaligned(p32, base + second - 1) == seq &&
                            memory_load_unaligned(p32, base + second - 1 + best - 3) ==
                                memory_load_unaligned(p32, here + best - 3))
                        {
                                positive length = gzip_extend(here, base + second - 1, 4, most);

                                if (length > best)
                                {
                                        best = length;
                                        dist = pos + 1 - second;
                                }
                        }
                }
                else if (second >= floor && memory_load_unaligned(p32, base + second - 1) == seq)
                {
                        best = gzip_extend(here, base + second - 1, 4, most);
                        dist = pos + 1 - second;
                }
                if (!best)
                {
                        gzip_note_literal_fast(e, *here);
                        pos++;
                        continue;
                }
                gzip_note_match_fast(e, pos - start, best, dist);
                gzip_fast_skip(e, pos + 1, best - 1, address_of h);
                pos += best;
        }
        e->hash4 = h;
        return pos;
}

/* The shortest match worth taking when literals are this varied: few
   kinds of literal make each one cheap, so short matches lose to them.
   libdeflate's table; a shallow search seldom finds the long ones. */
static positive gzip_min_match(positive kinds, positive depth)
{
        static const p8 least[80] = {
                9, 9, 9, 9, 9, 9, 8, 8, 7, 7, 6, 6, 6, 6, 6, 6,
                5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
                5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
        positive length = kinds < 80 ? least[kinds] : 3;

        if (depth < 16)
                length = min(length, (positive)(depth < 5 ? 4 : depth < 10 ? 5 : 7));
        return length;
}

/* The kinds of byte in the block's first 4 KiB. */
static positive gzip_min_match_ahead(p8 address_to bytes, positive n, positive depth)
{
        p8 used[256];
        positive kinds = 0;

        if (n < 512)
                return 3;
        if (n > 4096)
                n = 4096;
        memory_fill(used, 0, sizeof(used));
        for (positive i = 0; i < n; i++)
                used[bytes[i]] = 1;
        for (positive i = 0; i < 256; i++)
                kinds += used[i];
        return gzip_min_match(kinds, depth);
}

/* The kinds of literal the open block has used more than rarely. */
static positive gzip_min_match_seen(gzip_encoder address_to e, positive depth)
{
        p32 all = 0, kinds = 0;

        for (positive i = 0; i < 256; i++)
                all += e->lit_freq[i];
        for (positive i = 0; i < 256; i++)
                kinds += e->lit_freq[i] > (all >> 10);
        return gzip_min_match(kinds, depth);
}

/* A match's worth against one found a position or two later: libdeflate's
   rule, four a byte of length against the bits of the distance. */
static inline INLINE bipolar gzip_lazy_gain(positive length, positive dist,
                                            positive next_length, positive next_dist)
{
        return 4 * ((bipolar)next_length - (bipolar)length) +
               ((bipolar)gzip_high_bit((p32)dist) - (bipolar)gzip_high_bit((p32)next_dist));
}

static inline INLINE positive gzip_parse_chain(gzip_encoder address_to e, positive start,
                                               positive pos, positive limit, p8 parse,
                                               positive depth, positive nice)
{
        p8 address_to base = e->base;
        positive total = e->total;
        positive least = gzip_min_match_ahead(base + pos, min(limit, start + GZIP_SPLIT_MOST) - pos,
                                              depth);
        positive recount = pos + min(total - pos, (positive)10000);
        p32 h3 = e->hash3, h4 = e->hash4;

        while (pos < limit && !gzip_block_full(e, start, pos))
        {
                positive most = total - pos;
                positive dist = 0, length;

                if (parse >= GZIP_PARSE_LAZY && pos >= recount)
                {
                        least = gzip_min_match_seen(e, depth);
                        recount += min(total - recount, pos - start);
                }
                if (most > GZIP_MAX_MATCH)
                        most = GZIP_MAX_MATCH;
                length = gzip_chain_find(e, pos, least - 1, most, nice, depth,
                                         address_of h3, address_of h4, address_of dist);
                if (length < least ||
                    (length == 3 && dist > (parse >= GZIP_PARSE_LAZY ? 8192u : 4096u)))
                {
                        gzip_note_literal(e, base[pos]);
                        pos++;
                        continue;
                }
                /* pos + 1 .. pos + taken - 1 are in the finder. */
                positive taken = 1;

                if (parse >= GZIP_PARSE_LAZY)
                        while (length < nice)
                        {
                                positive next_most = total - pos - 1;
                                positive next_dist = 0, next_length;

                                if (next_most > GZIP_MAX_MATCH)
                                        next_most = GZIP_MAX_MATCH;
                                next_length = gzip_chain_find(e, pos + 1, length - 1, next_most,
                                                              nice, (depth >> 1) | 1,
                                                              address_of h3, address_of h4,
                                                              address_of next_dist);
                                taken = 2;
                                if (next_length >= length &&
                                    gzip_lazy_gain(length, dist, next_length, next_dist) > 2)
                                {
                                        gzip_note_literal(e, base[pos]);
                                        pos++;
                                        length = next_length;
                                        dist = next_dist;
                                        taken = 1;
                                        continue;
                                }
                                if (parse < GZIP_PARSE_LAZY2)
                                        break;
                                next_most = total - pos - 2;
                                if (next_most > GZIP_MAX_MATCH)
                                        next_most = GZIP_MAX_MATCH;
                                next_dist = 0;
                                next_length = gzip_chain_find(e, pos + 2, length - 1, next_most,
                                                              nice, (depth >> 2) | 1,
                                                              address_of h3, address_of h4,
                                                              address_of next_dist);
                                taken = 3;
                                if (next_length >= length &&
                                    gzip_lazy_gain(length, dist, next_length, next_dist) > 6)
                                {
                                        gzip_note_literal(e, base[pos]);
                                        gzip_note_literal(e, base[pos + 1]);
                                        pos += 2;
                                        length = next_length;
                                        dist = next_dist;
                                        taken = 1;
                                        continue;
                                }
                                break;
                        }
                gzip_note_match(e, pos - start, length, dist);
                if (length > taken)
                        gzip_chain_skip(e, pos + taken, length - taken, address_of h3,
                                        address_of h4);
                pos += length;
        }
        e->hash3 = h3;
        e->hash4 = h4;
        return pos;
}

/* The open input from pos to its end in deflate blocks, then the empty
   stored block that ends it on a byte boundary. */
static fn gzip_block_run(gzip_encoder address_to e, positive pos)
{
        gzip_level_shape shape = gzip_levels[e->level];
        positive start = pos;

        while (pos < e->total)
        {
                if (shape.parse == GZIP_PARSE_FAST)
                        pos = gzip_parse_fast(e, start, pos, e->total, shape.nice);
                else if (shape.parse == GZIP_PARSE_GREEDY)
                        pos = gzip_parse_chain(e, start, pos, e->total, GZIP_PARSE_GREEDY,
                                               shape.depth, shape.nice);
                else if (shape.parse == GZIP_PARSE_LAZY)
                        pos = gzip_parse_chain(e, start, pos, e->total, GZIP_PARSE_LAZY,
                                               shape.depth, shape.nice);
                else
                        pos = gzip_parse_chain(e, start, pos, e->total, GZIP_PARSE_LAZY2,
                                               shape.depth, shape.nice);
                gzip_block_emit(e, e->base + start, pos - start, false);
                start = pos;
        }
        gzip_write_stored(e, e->base + pos, 0, false);
}

/* The next position's hashes from its bytes. */
static fn gzip_hashes_at(gzip_encoder address_to e, positive pos)
{
        if (pos + 4 > e->total)
                return;
        p32 seq = memory_load_unaligned(p32, e->base + pos);

        e->hash3 = gzip_hash(seq << 8, GZIP_HASH3_BITS);
        e->hash4 = gzip_hash(seq, gzip_levels[e->level].parse == GZIP_PARSE_FAST
                                          ? GZIP_FAST_BITS
                                          : GZIP_HASH4_BITS);
}

/* The finder takes from..to without searches. */
static fn gzip_finder_take(gzip_encoder address_to e, positive from, positive to)
{
        if (to <= from)
                return;
        if (gzip_levels[e->level].parse == GZIP_PARSE_FAST)
                gzip_fast_skip(e, from, to - from, address_of e->hash4);
        else
                gzip_chain_skip(e, from, to - from, address_of e->hash3, address_of e->hash4);
}

/* Deflate data[0, n) after history bytes at data - history, ending on a
   byte boundary. The output span must hold n + n / 8 + 4096 bytes. */
static fn gzip_block_deflate(gzip_encoder address_to e, p8 address_to data,
                             positive history, positive n)
{
        gzip_level_shape shape = gzip_levels[e->level];

        e->base = data - history;
        e->total = history + n;
        e->mark = e->total;
        e->out_n = 0;
        e->bits = 0;
        e->bitn = 0;
        if (shape.parse == GZIP_PARSE_FAST)
                memory_fill(e->fast, 0, sizeof(e->fast));
        else
                gzip_finder_open(e);
        gzip_block_open(e);
        e->hash3 = e->hash4 = 0;
        if (e->total >= 5)
        {
                p32 seq = memory_load_unaligned(p32, e->base);

                e->hash3 = gzip_hash(seq << 8, GZIP_HASH3_BITS);
                e->hash4 = gzip_hash(seq, shape.parse == GZIP_PARSE_FAST ? GZIP_FAST_BITS
                                                                         : GZIP_HASH4_BITS);
        }
        gzip_finder_take(e, 0, history);
        gzip_block_run(e, history);
}

/* Deflate the n bytes at data that follow the input this encoder deflated
   last, as gzip_block_deflate would with the 32 KiB before them as
   history: the finder already holds every position it took, and takes the
   few it could not while the input stopped short. */
static fn gzip_block_follow(gzip_encoder address_to e, p8 address_to data, positive n)
{
        positive old = e->total;
        positive mark = min(e->mark, old >= 4 ? old - 4 : 0);

        /* Only the 32 KiB before data need be where they were: positions
           older than that are outside every window from here on, and no
           search reads them. */
        e->base = data - old;
        e->total = old + n;
        e->mark = e->total;
        e->out_n = 0;
        e->bits = 0;
        e->bitn = 0;
        gzip_block_open(e);
        gzip_hashes_at(e, mark);
        gzip_finder_take(e, mark, old);
        gzip_hashes_at(e, old);
        gzip_block_run(e, old);
}

/*
        The member around the blocks: header, blocks, the empty final block,
        CRC-32 and size. Input waits in batches of whole blocks after a
        32 KiB history tail. Each block is one job deflating into its
        worker's gzip_encoder; the sink checksums the block's input and
        writes its bytes in block order on the calling thread. The batch
        only decides how much input waits in memory, never where a block
        starts or what history it sees.
*/
#define GZIP_BATCH_BLOCKS 64
/* Blocks a batch holds for each worker: enough that the last blocks of a
   batch rarely leave workers waiting, with one worker holding one. A
   single-core gzip held sixty four megabytes of input it had no second core
   to hand; at sixteen workers the batch was sixty four 1 MiB blocks and
   gzip -6 peaked at 142 MB where pigz holds 14. */
#define GZIP_BATCH_PER_WORKER 3

typedef struct
{
        p8 level;
        /* [0, GZIP_WINDOW) history tail, then the batch */
        p8 address_to input;
        positive input_room;
        positive batch_blocks;
        /* Blocks in the batches before this one, so an encoder knows the
           block that follows the one it deflated last. */
        positive blocks;
        positive history;
        positive n;
        gzip_encoder address_to address_to slots;
        positive slot_count;
        p8 address_to done;
        p32 crc;
        p32 isize;
        byte_store address_to store;
        bipolar fd;
        bool failed;
} gzip_stream_writer;

static gzip_stream_writer gzip_writer;

static bool gzip_writer_emit(p8 address_to bytes, positive n)
{
        if (gzip_writer.failed)
                return false;
        if (gzip_writer.store)
        {
                if (!byte_store_append_exact(gzip_writer.store, bytes, n))
                {
                        gzip_writer.failed = true;
                        return gzip_fail("gzip output is too small");
                }
        }
        else if (gzip_writer.fd >= 0 &&
                 system_write_all((positive)gzip_writer.fd, bytes, n) != (bipolar)n)
        {
                gzip_writer.failed = true;
                return gzip_fail("gzip write failed");
        }
        return true;
}

static gzip_encoder address_to gzip_encoder_open(p8 level)
{
        gzip_encoder address_to e = (gzip_encoder address_to)memory_checked(sizeof(gzip_encoder));

        if (!e)
                return null;
        e->out_room = 0;
        e->out = null;
        e->level = level;
        e->last = positive_max - 1;
        e->total = 0;
        return e;
}

static fn gzip_writer_close(void)
{
        for (positive i = 0; i < gzip_writer.slot_count; i++)
        {
                gzip_encoder address_to e = gzip_writer.slots[i];

                if (e)
                {
                        memory_free(e, sizeof(gzip_encoder));
                }
        }
        memory_free(gzip_writer.slots, gzip_writer.slot_count * sizeof(gzip_encoder address_to));
        memory_free(gzip_writer.done, gzip_writer.batch_blocks);
        memory_free(gzip_writer.input, gzip_writer.input_room);
        gzip_writer.slots = null;
        gzip_writer.slot_count = 0;
        gzip_writer.done = null;
        gzip_writer.input = null;
        gzip_writer.input_room = 0;
}

static positive gzip_batch_bytes(gzip_stream_writer address_to w, positive index)
{
        positive from = index * GZIP_BLOCK;

        return w->n - from < GZIP_BLOCK ? w->n - from : GZIP_BLOCK;
}

/* One block of the batch, on any thread: it touches only its worker's
   encoder, its own done byte and its own output. */
static fn gzip_batch_job(address_any context, positive index,
                         parallel_output address_to output)
{
        gzip_stream_writer address_to w = (gzip_stream_writer address_to)context;
        positive slot = parallel_slot();
        gzip_encoder address_to e = w->slots[slot];

        w->done[index] = false;
        if (!e)
        {
                e = gzip_encoder_open(w->level);
                if (!e)
                        return;
                w->slots[slot] = e;
        }
        /* The block deflates straight into its output: room for it stored,
           the most it can take, and what it did not use given back. */
        positive n = gzip_batch_bytes(w, index);
        positive room = n + n / 8 + 4096;

        e->out = parallel_reserve(output, room);
        if (!e->out)
                return;
        p8 address_to data = w->input + GZIP_WINDOW + index * GZIP_BLOCK;

        if (e->last + 1 == w->blocks + index && e->total < (1u << 30))
                gzip_block_follow(e, data, n);
        else
                gzip_block_deflate(e, data, index ? GZIP_WINDOW : w->history, n);
        e->last = w->blocks + index;
        output->used -= room - e->out_n;
        w->done[index] = true;
}

/* A block's checksum and bytes, in block order, on the calling thread. */
static bool gzip_batch_sink(address_any context, positive index, address_any data,
                            positive length)
{
        gzip_stream_writer address_to w = (gzip_stream_writer address_to)context;
        positive n = gzip_batch_bytes(w, index);

        if (!w->done[index])
                return gzip_fail("gzip cannot map the encoder");
        w->crc = hash_crc32(w->crc, w->input + GZIP_WINDOW + index * GZIP_BLOCK, n);
        w->isize += (p32)n;
        return gzip_writer_emit((p8 address_to)data, length);
}

/* Every block of the batch, deflated side by side and written in block
   order, then the last 32 KiB kept as the next batch's history. */
static bool gzip_writer_batch(void)
{
        gzip_stream_writer address_to w = address_of gzip_writer;
        positive count = (w->n + GZIP_BLOCK - 1) / GZIP_BLOCK;

        if (!w->n)
                return !w->failed;
        if (!parallel_ordered(gzip_batch_job, gzip_batch_sink, w, count, w->n))
                return w->failed || gzip_why ? false
                                             : gzip_fail("gzip cannot map the encoder");

        positive keep = w->history + w->n;

        if (keep > GZIP_WINDOW)
                keep = GZIP_WINDOW;
        memory_copy(w->input + GZIP_WINDOW - keep, w->input + GZIP_WINDOW + w->n - keep, keep);
        w->history = keep;
        w->blocks += count;
        w->n = 0;
        return true;
}

static bool gzip_encode_setup(p8 level)
{
        p8 header[10] = {GZIP_MAGIC0, GZIP_MAGIC1, GZIP_METHOD, 0, 0, 0, 0, 0, 0, 3};

        gzip_writer_close();
        gzip_why = null;
        gzip_fixed_init();
        level = level ? level : 6;
        gzip_writer.level = level > 9 ? 9 : level;
        header[8] = gzip_writer.level == 1 ? 4 : gzip_writer.level == 9 ? 2 : 0;
        gzip_writer.history = 0;
        gzip_writer.blocks = 0;
        gzip_writer.n = 0;
        gzip_writer.crc = 0xffffffffu;
        gzip_writer.isize = 0;
        gzip_writer.failed = false;
        gzip_writer.store = gzip_output.bytes ? address_of gzip_output : null;
        gzip_writer.fd = gzip_out_fd;
        gzip_writer.slot_count = parallel_slots();
        gzip_writer.slots = (gzip_encoder address_to address_to)memory_checked(
            gzip_writer.slot_count * sizeof(gzip_encoder address_to));
        {
                positive width = parallel_width();

                gzip_writer.batch_blocks =
                    width == 1 ? 1
                    : min(width * GZIP_BATCH_PER_WORKER, (positive)GZIP_BATCH_BLOCKS);
        }
        gzip_writer.done = (p8 address_to)memory_checked(gzip_writer.batch_blocks);
        gzip_writer.input_room = GZIP_WINDOW + gzip_writer.batch_blocks * GZIP_BLOCK;
        gzip_writer.input = (p8 address_to)memory_checked(gzip_writer.input_room);
        if (!gzip_writer.slots || !gzip_writer.done || !gzip_writer.input)
                return gzip_fail("gzip cannot map the block input");
        return gzip_writer_emit(header, sizeof(header));
}

static bool gzip_encode_trailer(void)
{
        p8 tail[10] = {0x03, 0x00};
        bool ok = gzip_writer_batch();

        if (ok)
        {
                memory_store_unaligned(p32, tail + 2, ~gzip_writer.crc);
                memory_store_unaligned(p32, tail + 6, gzip_writer.isize);
                ok = gzip_writer_emit(tail, sizeof(tail));
        }
        gzip_writer_close();
        return ok;
}

static bool gzip_stream_encode(void)
{
        positive capacity = gzip_writer.batch_blocks * GZIP_BLOCK;

        for (;;)
        {
                if (gzip_writer.n == capacity && !gzip_writer_batch())
                        break;
                bipolar got = system_read_retry(
                    (positive)gzip_input.fd, gzip_writer.input + GZIP_WINDOW + gzip_writer.n,
                    capacity - gzip_writer.n);
                if (got < 0)
                {
                        gzip_fail("gzip read failed");
                        break;
                }
                if (!got)
                        return gzip_encode_trailer();
                gzip_writer.n += (positive)got;
        }
        gzip_writer_close();
        return false;
}

static bool gzip_encode_begin(bipolar out, p8 level)
{
        gzip_out_fd = out;
        gzip_output.bytes = null;
        gzip_input.fd = -1;
        return gzip_encode_setup(level);
}

static bool gzip_encode_write(p8 address_to src, positive n)
{
        positive capacity = gzip_writer.batch_blocks * GZIP_BLOCK;

        while (n)
        {
                positive take = min(n, capacity - gzip_writer.n);

                memory_copy(gzip_writer.input + GZIP_WINDOW + gzip_writer.n, src, take);
                gzip_writer.n += take;
                src += take;
                n -= take;
                if (gzip_writer.n == capacity && !gzip_writer_batch())
                        return false;
        }
        return !gzip_writer.failed;
}

static bool gzip_encode_end(void)
{
        return gzip_encode_trailer();
}

static bipolar gzip_deflate_mem(p8 address_to src, positive src_len,
                                p8 address_to dst, positive dst_cap, p8 level)
{
        bool ok;

        gzip_input.fd = -1;
        gzip_out_fd = -1;
        gzip_output.bytes = dst;
        gzip_output.room = dst_cap;
        gzip_output.used = 0;
        ok = gzip_encode_setup(level) && gzip_encode_write(src, src_len) &&
             gzip_encode_trailer();
        if (!ok)
                gzip_writer_close();
        gzip_output.bytes = null;
        return ok ? (bipolar)gzip_output.used : -1;
}

/* tar's codec table keeps one decoder behind begin/read/end. Only the
   thread between begin and end touches it; why is mirrored into gzip_why. */
static gzip_inflater address_to gzip_pull_one;

static bool gzip_decode_begin_prefix(bipolar in, p8 address_to prefix,
                                     positive n)
{
        if (gzip_pull_one)
                gzip_pull_close(gzip_pull_one);
        gzip_why = null;
        gzip_pull_one = (gzip_inflater address_to)gzip_pull_open(in, prefix, n);
        if (!gzip_pull_one)
                return gzip_fail(n > GZIP_DECODE_IN ? "gzip prefix"
                                                    : "gzip cannot map the decoder");
        return true;
}

static bipolar gzip_decode_read(p8 address_to dst, positive n)
{
        bipolar got;

        if (!gzip_pull_one)
                return -1;
        got = gzip_pull_read(gzip_pull_one, dst, n);
        gzip_why = gzip_pull_one->why;
        return got;
}

static bool gzip_decode_end(void)
{
        if (gzip_pull_one)
        {
                gzip_why = gzip_pull_one->why;
                gzip_pull_close(gzip_pull_one);
                gzip_pull_one = null;
        }
        return gzip_why == null;
}

#ifndef GZIP_CORE_ONLY

static b32 gzip_stream(bipolar in, bipolar out, bool decode, p8 level)
{
        bool ok;

        byte_input_open_fd(address_of gzip_input, in, gzip_in_buf, GZIP_IN);
        gzip_out_fd = out;
        gzip_output.bytes = null;
        gzip_status = 0;
        if (decode)
                ok = gzip_stream_decode(in, out);
        else
                ok = gzip_encode_setup(level) && gzip_stream_encode();
        if (!ok)
        {
                //      The reason is spelled for tar, codec first; the command
                //      has named itself already.
                if (gzip_why)
                        string_format(log_error, "gzip: %s\n",
                                      gzip_why + (!string_compare_max(gzip_why, "gzip ", 5)
                                                          ? 5 : 0));
                gzip_status = 1;
                return 1;
        }
        return 0;
}


static const file_codec_suffix gzip_suffixes[] = {
    {".gz", ""}, {".Z", ""}, {".tgz", ".tar"}};

static b32 file_gzip(void)
{
        file_codec_cli codec = {
            .name = "gzip", .decode_name = "gunzip", .cat_name = "zcat",
            .usage = "Usage: gzip [-cdfkqt123456789] [FILE...]",
            .version = "gzip from moonwater",
            .status = address_of gzip_status, .suffixes = gzip_suffixes,
            .suffix_count = array_count(gzip_suffixes),
            .decode_suffix_error = "unknown suffix; use -c",
            .encode_suffix_error = "cannot guess output name",
            .features = FILE_CODEC_LONG_QUIET | FILE_CODEC_LEVEL_WORDS |
                        FILE_CODEC_NO_NAME | FILE_CODEC_SHORT_VERSION,
            .remove_source = true, .level = 6,
            .run = gzip_stream};
        return file_codec_main(address_of codec);
}

#endif /* GZIP_CORE_ONLY */
