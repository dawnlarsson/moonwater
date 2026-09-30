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
                                  positive n, positive root, positive kind,
                                  p8 address_to look)
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
        positive used = n - count[0];

        count[0] = 0;
        for (positive len = 1; len <= GZIP_MAXBITS; len++)
        {
                left <<= 1;
                left -= count[len];
                if (left < 0)
                        return -2;
        }
        /* gzip refuses a code that leaves room, but for its single one-bit
           code, and one that uses none, but for distances: -3. */
        if (left && !(count[1] == 1 && used == 1) && (used || kind != 2))
                return -3;
        if (look)
        {
                /* gzip's lookahead: the table's root of nine bits for lengths
                   and literals, six for distances and seven for the precode,
                   cut to the longest code and raised to the shortest. */
                positive least = 1, most = GZIP_MAXBITS;
                positive wanted = kind == 1 ? 9 : kind == 2 ? 6 : 7;

                while (least <= GZIP_MAXBITS && !count[least])
                        least++;
                while (most && !count[most])
                        most--;
                *look = !used ? 0 : (p8)(wanted > most ? most : wanted < least ? least : wanted);
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
        /* The bytes a CLI decode has kept back so far, sitting before out:
           gzip's window goes out in steps of 32 KiB, so a stream that fails
           inside a block loses the part of it past the last step. */
        positive carry;
        /* The second slab of a decode that writes on another thread, its
           history before it, and the memory it came from. */
        p8 address_to alt;
        p8 address_to alt_base;
        positive size;
        bool garbage;
        /* How many bits of lookahead gzip's decoder wants before it will
           decode a code: the table's root width, cut to the longest code
           and raised to the shortest. Near the end of input a token whose
           code is shorter still cannot be read, as there. */
        p8 look_lit;
        p8 look_dist;
        /* A failure's text, its first character telling how to say it (see
           gzip_note); a second one when the length is wrong as well as the
           checksum. why_text holds the ones that carry a number. */
        string_address why;
        string_address why2;
        p8 why_text[96];
        /* The tables the span kernel reads at random, each on a line of
           its own. */
        p32 litlen[GZIP_LITLEN_CELLS] __attribute__((aligned(64)));
        p32 offset[GZIP_OFFSET_CELLS] __attribute__((aligned(64)));
        p32 precode[1 << GZIP_PRECODE_ROOT] __attribute__((aligned(64)));
} __attribute__((aligned(64))) gzip_inflater;

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
        byte_input_open_fd(address_of z->input, -1, tail, GZIP_DECODE_IN);
        z->out = tail + GZIP_DECODE_IN + GZIP_WINDOW;
        z->crc = 0xffffffffu;
        z->size = GZIP_INFLATER_SIZE;
        return z;
}

/*
        What gzip -d says, which the first character of a failure's text
        chooses the shape of: a newline is "\ngzip: NAME: text\n", a colon
        "gzip: NAME: text\n", a blank "gzip: NAME" and the text and a
        newline. tar and the checks read the text after that character.
*/
#define GZIP_WHY_EOF "\nunexpected end of file"
#define GZIP_WHY_FORMAT "\ninvalid compressed data--format violated"
#define GZIP_WHY_CRC "\ninvalid compressed data--crc error"
#define GZIP_WHY_LENGTH "\ninvalid compressed data--length error"
#define GZIP_WHY_NOT_GZIP "\nnot in gzip format"
#define GZIP_WHY_GARBAGE "\ndecompression OK, trailing garbage ignored"
#define GZIP_WHY_ENCRYPTED " is encrypted -- not supported"
#define GZIP_WHY_READ ":read error"

static bool gzip_inflate_fail(gzip_inflater address_to z, string_address why)
{
        if (!z->why)
                z->why = why;
        return false;
}

/* A failure's text built in pieces: the shape character, then strings and
   numbers, then gzip_why_end. Nothing is built once a failure is held. */
static bool gzip_why_begin(gzip_inflater address_to z, p8 shape)
{
        if (z->why)
                return false;
        z->why_text[0] = shape;
        z->why_text[1] = 0;
        return true;
}

static fn gzip_why_str(gzip_inflater address_to z, string_address text)
{
        positive at = 0;

        while (z->why_text[at])
                at++;
        while (*text && at < sizeof(z->why_text) - 1)
                z->why_text[at++] = (p8)*text++;
        z->why_text[at] = 0;
}

static fn gzip_why_num(gzip_inflater address_to z, positive value, positive base, positive width)
{
        p8 digits[24];
        positive n = 0, at = 0;

        do
        {
                p8 digit = (p8)(value % base);

                digits[n++] = (p8)(digit < 10 ? '0' + digit : 'a' + digit - 10);
                value /= base;
        } while (value && n < sizeof(digits));
        while (n < width && n < sizeof(digits))
                digits[n++] = '0';
        while (z->why_text[at])
                at++;
        while (n && at < sizeof(z->why_text) - 1)
                z->why_text[at++] = digits[--n];
        z->why_text[at] = 0;
}

static bool gzip_why_end(gzip_inflater address_to z)
{
        z->why = (string_address)z->why_text;
        return false;
}

static fn gzip_inflater_free(gzip_inflater address_to z)
{
        if (z->alt_base)
                memory_free(z->alt_base, GZIP_WINDOW + GZIP_DECODE_OUT + GZIP_DECODE_SLACK);
        memory_free(z, z->size);
}

/* Hand whole lookahead bytes back to the input window first, so that a
   compaction keeps every byte the bit buffer has not consumed. */
static bool gzip_inflate_more(gzip_inflater address_to z, positive want)
{
        z->input.at -= z->count >> 3;
        z->count &= 7;
        z->bits &= ((p64)1 << z->count) - 1;
        if (byte_input_need(address_of z->input, want) < 0)
                return gzip_inflate_fail(z, GZIP_WHY_READ);
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
                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
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
                return gzip_inflate_fail(z, GZIP_WHY_FORMAT);
        z->input.at -= rewind;
        z->bits = 0;
        z->count = 0;
        return true;
}

static bool gzip_inflate_table(gzip_inflater address_to z, p32 address_to table,
                               p8 address_to length, positive n, positive root,
                               positive kind, p8 address_to look)
{
        bipolar built = gzip_huffman_cells(table, length, n, root, kind, look);

        if (built < 0)
                return gzip_inflate_fail(z, GZIP_WHY_FORMAT);
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
        p8 look_pre = 0;

        if (!gzip_inflate_table(z, z->precode, clen, 19, GZIP_PRECODE_ROOT, 0,
                                address_of look_pre))
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
                if (count < look_pre)
                        return gzip_inflate_fail(z, GZIP_WHY_EOF);
                cell = z->precode[bits & 127];
                take = cell & 255;
                if ((cell & GZIP_CELL_EXCEPTIONAL) || take > count)
                        return gzip_inflate_fail(z, count < 7 ? GZIP_WHY_EOF
                                                              : GZIP_WHY_FORMAT);
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
                        return gzip_inflate_fail(z, GZIP_WHY_EOF);
                repeat = (positive)(bits & (((p64)1 << width) - 1));
                bits >>= width;
                count -= width;
                if (symbol == 16)
                {
                        if (!at)
                                return gzip_inflate_fail(z, GZIP_WHY_FORMAT);
                        repeat += 3;
                        fill = last;
                }
                else
                {
                        repeat += symbol == 17 ? 3 : 11;
                        fill = 0;
                }
                if (at + repeat > nlit + ndist)
                        return gzip_inflate_fail(z, GZIP_WHY_FORMAT);
                memory_fill(lengths + at, fill, repeat);
                at += repeat;
                last = fill;
        }
        z->bits = bits & (((p64)1 << count) - 1);
        z->count = count;

        if (!lengths[256])
                return gzip_inflate_fail(z, GZIP_WHY_FORMAT);
        return gzip_inflate_table(z, z->litlen, lengths, nlit, GZIP_LITLEN_ROOT, 1,
                                  address_of z->look_lit) &&
               gzip_inflate_table(z, z->offset, lengths + nlit, ndist, GZIP_OFFSET_ROOT, 2,
                                  address_of z->look_dist);
}

static bool gzip_inflate_fixed(gzip_inflater address_to z)
{
        p8 lengths[GZIP_MAXLIT + GZIP_MAXDIST];

        z->look_lit = 7;
        z->look_dist = 5;
        if (z->fixed_loaded)
                return true;
        memory_fill(lengths, 8, 144);
        memory_fill(lengths + 144, 9, 112);
        memory_fill(lengths + 256, 7, 24);
        memory_fill(lengths + 280, 8, 8);
        memory_fill(lengths + GZIP_MAXLIT, 5, GZIP_MAXDIST);
        if (!gzip_inflate_table(z, z->litlen, lengths, GZIP_MAXLIT, GZIP_LITLEN_ROOT, 1, null) ||
            !gzip_inflate_table(z, z->offset, lengths + GZIP_MAXLIT, GZIP_MAXDIST,
                                GZIP_OFFSET_ROOT, 2, null))
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
        if (z->count < z->look_lit)
                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
        cell = gzip_inflate_cell(z->litlen, z->bits, GZIP_LITLEN_ROOT, address_of root);
        take = root + (cell & 255);
        if (cell & GZIP_CELL_LITERAL)
        {
                if (take > z->count)
                        return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                z->bits >>= take;
                z->count -= take;
                z->out[z->fill++] = (p8)(cell >> 8);
                return 0;
        }
        if ((cell & (GZIP_CELL_EXCEPTIONAL | GZIP_CELL_END)) == GZIP_CELL_EXCEPTIONAL)
                return gzip_inflate_fail(z, (cell & GZIP_CELL_SYMBOL) && take <= z->count
                                                ? GZIP_WHY_FORMAT
                                        : z->count < GZIP_MAXBITS ? GZIP_WHY_EOF
                                                                 : GZIP_WHY_FORMAT), -1;
        if (take > z->count)
                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
        if (cell & GZIP_CELL_END)
        {
                z->bits >>= take;
                z->count -= take;
                return 1;
        }
        length = cell >> 16;
        z->bits >>= take;
        z->count -= take;

        if (z->count < z->look_dist)
                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
        cell = gzip_inflate_cell(z->offset, z->bits, GZIP_OFFSET_ROOT, address_of root);
        take = root + (cell & 255);
        if (cell & GZIP_CELL_EXCEPTIONAL)
                return gzip_inflate_fail(z, (cell & GZIP_CELL_SYMBOL) && take <= z->count
                                                ? GZIP_WHY_FORMAT
                                        : z->count < GZIP_MAXBITS ? GZIP_WHY_EOF
                                                                 : GZIP_WHY_FORMAT), -1;
        if (take > z->count)
                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
        code = root + ((cell >> 8) & 255);
        distance = (cell >> 16) + ((z->bits >> code) & (((positive)1 << (take - code)) - 1));
        z->bits >>= take;
        z->count -= take;
        made = z->flushed + z->fill - z->member_start;
        if (distance > made)
                return gzip_inflate_fail(z, GZIP_WHY_FORMAT), -1;
        memory_copy_match(z->out + z->fill, distance, length);
        z->fill += length;
        return 0;
}

static const string_address gzip_span_why[5] = {
        null, null, (string_address)GZIP_WHY_FORMAT,
        (string_address)GZIP_WHY_FORMAT, (string_address)GZIP_WHY_FORMAT};

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
                        return gzip_inflate_fail(z, GZIP_WHY_FORMAT), -1;
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
                        return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
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
                                return gzip_inflate_fail(z, GZIP_WHY_FORMAT), -1;
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
                        return gzip_inflate_fail(z, GZIP_WHY_EOF);
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

                bipolar magic0 = gzip_head_byte(z, address_of sum);
                bipolar magic1 = magic0 < 0 ? -1 : gzip_head_byte(z, address_of sum);

                if (magic0 < 0 || magic1 < 0)
                        return gzip_inflate_fail(z, magic0 ? GZIP_WHY_EOF : GZIP_WHY_NOT_GZIP), -1;
                if (magic0 != GZIP_MAGIC0 || magic1 != GZIP_MAGIC1)
                        return gzip_inflate_fail(z, GZIP_WHY_NOT_GZIP), -1;
                method = gzip_head_byte(z, address_of sum);
                if (method < 0)
                        return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                if (method != GZIP_METHOD)
                {
                        gzip_why_begin(z, ':');
                        gzip_why_str(z, "unknown method ");
                        gzip_why_num(z, (positive)method, 10, 0);
                        gzip_why_str(z, " -- not supported");
                        return gzip_why_end(z), -1;
                }
                flags = gzip_head_byte(z, address_of sum);
                if (flags < 0)
                        return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                if ((p8)flags & 0x20)
                        return gzip_inflate_fail(z, GZIP_WHY_ENCRYPTED), -1;
                if ((p8)flags & 0xc0)
                {
                        gzip_why_begin(z, ' ');
                        gzip_why_str(z, "has flags 0x");
                        gzip_why_num(z, (positive)(p8)flags, 16, 0);
                        gzip_why_str(z, " -- not supported");
                        return gzip_why_end(z), -1;
                }
                for (positive at = 0; at < 6; at++)
                        if (gzip_head_byte(z, address_of sum) < 0)
                                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                if ((p8)flags & GZIP_FEXTRA)
                {
                        bipolar xlen = gzip_head_byte(z, address_of sum);
                        bipolar xlen_hi = gzip_head_byte(z, address_of sum);
                        bipolar extra;

                        if (xlen < 0 || xlen_hi < 0)
                                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                        extra = xlen + (xlen_hi << 8);
                        while (extra--)
                                if (gzip_head_byte(z, address_of sum) < 0)
                                        return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
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
                        p32 want = (sum ^ 0xffffffffu) & 0xffff;

                        if (low < 0 || high < 0)
                                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                        if ((p32)(low | high << 8) != want)
                        {
                                gzip_why_begin(z, ':');
                                gzip_why_str(z, "header checksum 0x");
                                gzip_why_num(z, (p32)(low | high << 8), 16, 4);
                                gzip_why_str(z, " != computed checksum 0x");
                                gzip_why_num(z, want, 16, 4);
                                return gzip_why_end(z), -1;
                        }
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
                return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
        /* Both are checked and both said, as gzip does. */
        if (got_crc != ~z->crc)
                gzip_inflate_fail(z, GZIP_WHY_CRC);
        if (got_size != (p32)(z->flushed + z->fill - z->member_start))
        {
                if (z->why)
                        z->why2 = GZIP_WHY_LENGTH;
                else
                        gzip_inflate_fail(z, GZIP_WHY_LENGTH);
        }
        if (z->why)
                return -1;
        z->head_done = false;
        z->members++;
        return 1;
}

/* Whether another member follows: 0 yes, 1 no (the input ended, or held
   only what gzip ignores after a member), -1 failed. After a member gzip
   takes zeros to the end of input as padding, and anything else that is
   not a header as garbage to warn of and stop at, but a first byte of a
   header with nothing after it is a truncation. */
static bipolar gzip_inflate_next(gzip_inflater address_to z)
{
        positive have;

        if (!gzip_inflate_more(z, 2))
                return -1;
        have = z->input.have - z->input.at;
        if (!have)
                return z->members ? 1 : (gzip_inflate_fail(z, GZIP_WHY_EOF), -1);
        if (!z->members)
                return 0;
        if (z->input.buf[z->input.at] == GZIP_MAGIC0)
        {
                if (have < 2)
                        return gzip_inflate_fail(z, GZIP_WHY_EOF), -1;
                if (z->input.buf[z->input.at + 1] == GZIP_MAGIC1)
                        return 0;
        }
        else if (!z->input.buf[z->input.at])
        {
                for (;;)
                {
                        while (z->input.at < z->input.have && !z->input.buf[z->input.at])
                                z->input.at++;
                        if (z->input.at < z->input.have)
                                break;
                        if (!gzip_inflate_more(z, 1))
                                return -1;
                        if (z->input.at >= z->input.have)
                                return 1;
                }
        }
        z->garbage = true;
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
                        bipolar next = gzip_inflate_next(z);

                        if (next < 0)
                                return false;
                        if (next)
                        {
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

/* The same, into the second slab: its history is the last 32 KiB of this
   one, and the writer may still be reading this one meanwhile. */
static fn gzip_inflate_swap(gzip_inflater address_to z)
{
        p8 address_to other = z->alt;

        if (z->crc_at < z->fill)
                z->crc = hash_crc32(z->crc, z->out + z->crc_at, z->fill - z->crc_at);
        memory_copy_apart(other - GZIP_WINDOW, z->out + z->fill - GZIP_WINDOW, GZIP_WINDOW);
        z->alt = z->out;
        z->out = other;
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
        gzip_inflater_free(z);
        return ok;
}

/* How a failure's text is said, and what a decode said: the first and
   second messages, copied out of the decoder before it is freed. */
static p8 gzip_note_text[96];
static string_address gzip_note;
static string_address gzip_note_more;
static bool gzip_warned;
static bool gzip_garbage;

static p8 gzip_note_shape(string_address why)
{
        return why[0] == '\n' || why[0] == ':' || why[0] == ' ' ? (p8)why[0] : 0;
}

/* The bytes of the slab a decode may write now: all when it has ended or
   failed for want of input or a checksum, and up to the last 32 KiB step of
   the member's output while a block is open, as gzip's window goes out --
   a stream that breaks inside a block loses that block's tail there. */
static positive gzip_hold(gzip_inflater address_to z, bool ok)
{
        p64 made;

        if (!z->head_done || (ok && z->finished))
                return 0;
        if (!ok && !string_equals(z->why, GZIP_WHY_FORMAT))
                return 0;
        made = z->flushed + z->fill - z->member_start;
        return (positive)(made & (GZIP_WINDOW - 1));
}

/*
        A decode that goes on past its first slab writes on a second thread:
        the slabs take turns, the decoder fills one while the other is being
        written, and hands each over whole. Which bytes of a slab go out is
        the decoder's to say and no different from the plain loop's, so the
        bytes on the descriptor are the same; only when the write syscalls
        run moves.
*/
typedef struct
{
        bipolar out;
        p8 address_to bytes[2];
        positive length[2];
        /* full says a slot holds bytes to write; wake counts changes of
           any kind for the futex both sides sleep on. */
        b32 full[2];
        b32 wake;
        b32 done;
        b32 failed;
} gzip_sink;

static fn gzip_sink_signal(gzip_sink address_to k)
{
        atomic_inc(address_of k->wake);
        thread_wake(address_of k->wake, 1 << 30);
}

static fn gzip_sink_job(address_any context, positive index)
{
        gzip_sink address_to k = (gzip_sink address_to)context;

        (void)index;
        for (positive slot = 0;; slot ^= 1)
        {
                for (;;)
                {
                        b32 word = atomic_load(address_of k->wake);

                        if (atomic_load(address_of k->full[slot]))
                                break;
                        if (atomic_load(address_of k->done))
                                return;
                        thread_wait(address_of k->wake, word);
                }
                if (k->length[slot] &&
                    system_write_all((positive)k->out, k->bytes[slot], k->length[slot]) !=
                        (bipolar)k->length[slot])
                        atomic_exchange(address_of k->failed, 1);
                atomic_exchange(address_of k->full[slot], 0);
                gzip_sink_signal(k);
        }
}

/* Sleep until a slot is empty. */
static fn gzip_sink_wait_empty(gzip_sink address_to k, positive slot)
{
        for (;;)
        {
                b32 word = atomic_load(address_of k->wake);

                if (!atomic_load(address_of k->full[slot]))
                        return;
                thread_wait(address_of k->wake, word);
        }
}

/* The whole stream from in to out (out < 0 tests without writing). */
static bool gzip_stream_decode(bipolar in, bipolar out)
{
        gzip_inflater address_to z = (gzip_inflater address_to)gzip_pull_open(in, null, 0);
        gzip_sink sink = {0};
        bool piped = false;
        positive slot = 0;
        bool ok;

        gzip_note = null;
        gzip_note_more = null;
        gzip_garbage = false;
        if (!z)
                return gzip_fail("gzip cannot map the decoder");
        for (;;)
        {
                positive hold;
                positive length;

                ok = gzip_inflate_run(z);
                hold = gzip_hold(z, ok);
                length = z->carry + z->fill - hold;
                if (piped)
                {
                        sink.bytes[slot] = z->out - z->carry;
                        sink.length[slot] = length;
                        atomic_exchange(address_of sink.full[slot], 1);
                        gzip_sink_signal(address_of sink);
                        if (atomic_load(address_of sink.failed))
                        {
                                z->why = null;
                                ok = gzip_inflate_fail(z, "gzip write failed");
                                break;
                        }
                }
                else if (out >= 0 && length &&
                         system_write_all((positive)out, z->out - z->carry, length) !=
                             (bipolar)length)
                {
                        z->why = null;
                        ok = gzip_inflate_fail(z, "gzip write failed");
                        break;
                }
                if (!ok || z->finished)
                        break;
                z->carry = hold;
                if (out >= 0 && !piped && !z->alt_base)
                {
                        /* Past the first slab: a second slab and a writer. */
                        p8 address_to more = (p8 address_to)memory_checked(
                            GZIP_WINDOW + GZIP_DECODE_OUT + GZIP_DECODE_SLACK);

                        if (more)
                        {
                                sink.out = out;
                                if (parallel_beside(gzip_sink_job, address_of sink))
                                {
                                        z->alt_base = more;
                                        z->alt = more + GZIP_WINDOW;
                                        piped = true;
                                        /* The next slab goes to the writer first. */
                                        slot = 1;
                                }
                                else
                                        memory_free(more, GZIP_WINDOW + GZIP_DECODE_OUT +
                                                              GZIP_DECODE_SLACK);
                        }
                }
                if (piped)
                {
                        /* The slab just read is the writer's to write while the
                           other is filled. */
                        gzip_sink_wait_empty(address_of sink, slot ^ 1);
                        gzip_inflate_swap(z);
                        slot ^= 1;
                }
                else
                        gzip_inflate_slide(z);
        }
        if (piped)
        {
                atomic_exchange(address_of sink.done, 1);
                gzip_sink_signal(address_of sink);
                parallel_beside_wait();
                if (ok && atomic_load(address_of sink.failed))
                {
                        z->why = null;
                        ok = gzip_inflate_fail(z, "gzip write failed");
                }
        }
        if (z->why)
        {
                positive at = 0;

                while (z->why[at] && at < sizeof(gzip_note_text) - 1)
                {
                        gzip_note_text[at] = (p8)z->why[at];
                        at++;
                }
                gzip_note_text[at] = 0;
                gzip_note = (string_address)gzip_note_text;
                gzip_why = gzip_note_text + (gzip_note_shape(gzip_note) ? 1 : 0);
        }
        else
                gzip_why = null;
        gzip_note_more = z->why2;
        gzip_garbage = ok && z->garbage;
        gzip_inflater_free(z);
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
        gzip_inflater_free(z);
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

static positive gzip_block_size = 1u << 16;
#define GZIP_BLOCK gzip_block_size
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
/* --ultra: the optimal parser, a level past 9 that GNU gzip has no spelling for. */
#define GZIP_ULTRA 10

typedef struct
{
        p8 parse;
        p16 depth;
        p16 nice;
} gzip_level_shape;

static const gzip_level_shape gzip_levels[11] = {
        {GZIP_PARSE_LAZY, 35, 65},
        {GZIP_PARSE_FAST, 2, 32},
        {GZIP_PARSE_GREEDY, 6, 10},
        {GZIP_PARSE_GREEDY, 12, 14},
        {GZIP_PARSE_GREEDY, 16, 30},
        {GZIP_PARSE_LAZY, 16, 30},
        {GZIP_PARSE_LAZY, 35, 65},
        {GZIP_PARSE_LAZY, 100, 130},
        {GZIP_PARSE_LAZY2, 300, 258},
        {GZIP_PARSE_LAZY2, 600, 258},
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
                        p16 head4[1u << GZIP_HASH4_BITS];
                        p16 head3[1u << GZIP_HASH3_BITS];
                        p16 prev[GZIP_WINDOW];
                };
                /* The fast finder's buckets: absolute positions plus one,
                   zero for none, two a hash. */
                p32 fast[2u << GZIP_FAST_BITS];
        };
        /* The open deflate block: its pairs and symbol counts, and what
           kind of symbols it has seen for the split test. */
        positive pairs;
        /* The open block's pairs: the arrays below, or the optimal
           parser's own, which a block of any number of pairs fits. */
        p32 address_to mpos;
        p16 address_to mlen;
        p16 address_to mdist;
        struct gzip_ultra address_to ultra;
        p32 own_pos[GZIP_PAIRS];
        p16 own_len[GZIP_PAIRS];
        p16 own_dist[GZIP_PAIRS];
        p32 lit_freq[GZIP_MAXLIT];
        p32 dist_freq[GZIP_MAXDIST];
        p32 seen[10];
        p32 fresh[10];
        p32 lit_seen[256];
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
                        return at + (bottom_bit_known(x) >> 3);
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

/* A literal and a pair, counted. The split test reads the literals'
   kinds from what lit_freq gained since it last looked; the chain parse
   counts pairs' kinds itself, and the fast level's blocks end at a fixed
   length and are never tested. */
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

static fn gzip_block_open(gzip_encoder address_to e)
{
        e->pairs = 0;
        memory_fill(e->lit_freq, 0, sizeof(e->lit_freq));
        memory_fill(e->dist_freq, 0, sizeof(e->dist_freq));
        memory_fill(e->seen, 0, sizeof(e->seen));
        memory_fill(e->fresh, 0, sizeof(e->fresh));
        memory_fill(e->lit_seen, 0, sizeof(e->lit_seen));
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
        for (positive c = 0; c < 256; c++)
        {
                e->fresh[((c >> 5) & 6) | (c & 1)] += e->lit_freq[c] - e->lit_seen[c];
                e->lit_seen[c] = e->lit_freq[c];
        }
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
                memory_store_unaligned(p16, e->out + e->out_n, (p16)chunk);
                memory_store_unaligned(p16, e->out + e->out_n + 2, (p16)~chunk);
                e->out_n += 4;
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

/* What a block would cost as each kind, and the dynamic kind's trees.
   dynamic is positive_max when no valid tree can be built. Every count
   is in bits and includes the three-bit block header. */
typedef struct
{
        p8 lit_len[GZIP_MAXLIT];
        p8 dist_len[GZIP_MAXDIST];
        p8 clen[19];
        p8 seq[288 + 32];
        /* The code lengths as code-length symbols and their extra bits. */
        p8 tok[288 + 32];
        p8 tok_extra[288 + 32];
        p32 cfreq[19];
        positive ntok;
        positive nseq;
        positive hlit;
        positive hdist;
        positive hclen;
        positive dynamic;
        positive fixed;
        positive stored;
} gzip_plan;

/* The lengths seq[0, n) as code-length symbols: runs of a length by 16, of
   zeros by 17 and 18, each only when its flag bit (1, 2, 4) is set. The
   best mix differs by block, so the plan tries all eight. */
static positive gzip_rle_tokens(p8 address_to seq, positive n, positive flags,
                                p8 address_to tok, p8 address_to extra,
                                p32 address_to freq)
{
        positive i = 0, count = 0;

        memory_fill(freq, 0, 19 * sizeof(p32));
        while (i < n)
        {
                p8 here = seq[i];
                positive run = 1;

                while (i + run < n && seq[i + run] == here)
                        run++;
                i += run;
                if (!here)
                {
                        while ((flags & 4) && run >= 11)
                        {
                                positive r = run > 138 ? 138 : run;

                                tok[count] = 18;
                                extra[count++] = (p8)(r - 11);
                                freq[18]++;
                                run -= r;
                        }
                        while ((flags & 2) && run >= 3)
                        {
                                positive r = run > 10 ? 10 : run;

                                tok[count] = 17;
                                extra[count++] = (p8)(r - 3);
                                freq[17]++;
                                run -= r;
                        }
                }
                else if ((flags & 1) && run >= 4)
                {
                        tok[count] = here;
                        extra[count++] = 0;
                        freq[here]++;
                        run--;
                        while (run >= 3)
                        {
                                positive r = run > 6 ? 6 : run;

                                tok[count] = 16;
                                extra[count++] = (p8)(r - 3);
                                freq[16]++;
                                run -= r;
                        }
                }
                while (run)
                {
                        tok[count] = here;
                        extra[count++] = 0;
                        freq[here]++;
                        run--;
                }
        }
        return count;
}

static fn gzip_plan_counts(p32 address_to lit_freq, p32 address_to dist_freq, positive bitn,
                           positive length, gzip_plan address_to plan)
{
        p8 address_to lit_len = plan->lit_len;
        p8 address_to dist_len = plan->dist_len;
        p8 address_to clen = plan->clen;
        p8 address_to seq = plan->seq;
        p32 address_to cfreq = plan->cfreq;
        positive bits = 0, extra_bits = 0, fixed_bits = 3;
        positive chunks = length ? (length + 65534) / 65535 : 1;
        positive i, run, nseq;
        positive hlit = 286, hdist = 30, hclen = 19;
        p8 here;

        plan->stored = length * 8 + chunks * 40 + ((8 - ((bitn + 3) & 7)) & 7) - 5;
        plan->dynamic = positive_max;
        lit_freq[256] = 1;
        for (i = 0; i < 29; i++)
                extra_bits += lit_freq[257 + i] * gzip_len_extra[i];
        for (i = 0; i < 30; i++)
                extra_bits += dist_freq[i] * gzip_dist_extra[i];

        fixed_bits += extra_bits;
        for (i = 0; i < GZIP_MAXLIT; i++)
                fixed_bits += lit_freq[i] * gzip_fixed_lit_len[i];
        for (i = 0; i < GZIP_MAXDIST; i++)
                fixed_bits += dist_freq[i] * 5;
        plan->fixed = fixed_bits;

        huffman_lengths(lit_freq, GZIP_MAXLIT, lit_len, GZIP_MAXBITS);
        huffman_lengths(dist_freq, GZIP_MAXDIST, dist_len, GZIP_MAXBITS);
        if (!lit_len[256])
                lit_len[256] = 1;
        {
                bool any_dist = false;

                for (i = 0; i < GZIP_MAXDIST; i++)
                        if (dist_len[i])
                                any_dist = true;
                if (!any_dist)
                        dist_len[0] = 1;
        }
        if (!gzip_lengths_ok(lit_len, GZIP_MAXLIT, GZIP_MAXBITS) ||
            !gzip_lengths_ok(dist_len, GZIP_MAXDIST, GZIP_MAXBITS) ||
            !gzip_used_coded(lit_freq, lit_len, GZIP_MAXLIT) ||
            !gzip_used_coded(dist_freq, dist_len, GZIP_MAXDIST))
                return;

        bits = extra_bits;
        for (i = 0; i < GZIP_MAXLIT; i++)
                bits += lit_freq[i] * lit_len[i];
        for (i = 0; i < GZIP_MAXDIST; i++)
                bits += dist_freq[i] * dist_len[i];

        hlit -= memory_span_byte_reverse(lit_len + 257, 0, hlit - 257);
        hdist -= memory_span_byte_reverse(dist_len + 1, 0, hdist - 1);
        memory_copy(seq, lit_len, hlit);
        memory_copy(seq + hlit, dist_len, hdist);
        nseq = hlit + hdist;

        {
                p32 tries[19];
                p8 tclen[19];
                p8 ttok[288 + 32];
                p8 textra[288 + 32];
                positive least = positive_max;

                for (positive flags = 0; flags < 8; flags++)
                {
                        positive count = gzip_rle_tokens(seq, nseq, flags, ttok, textra, tries);
                        positive size = 0, h = 19;

                        huffman_lengths(tries, 19, tclen, 7);
                        if (!gzip_lengths_ok(tclen, 19, 7) || !gzip_used_coded(tries, tclen, 19))
                                continue;
                        while (h > 4 && !tclen[gzip_clen_order[h - 1]])
                                h--;
                        size = h * 3 + tries[16] * 2 + tries[17] * 3 + tries[18] * 7;
                        for (positive c = 0; c < 19; c++)
                                size += tries[c] * tclen[c];
                        if (size < least)
                        {
                                least = size;
                                hclen = h;
                                memory_copy(cfreq, tries, sizeof(tries));
                                memory_copy(clen, tclen, sizeof(tclen));
                                memory_copy(plan->tok, ttok, count);
                                memory_copy(plan->tok_extra, textra, count);
                                plan->ntok = count;
                        }
                }
                if (least == positive_max)
                        return;
        }
        bits += 17 + hclen * 3 + cfreq[16] * 2 + cfreq[17] * 3 + cfreq[18] * 7;
        for (positive c = 0; c < 19; c++)
                bits += cfreq[c] * clen[c];
        plan->nseq = nseq;
        plan->hlit = hlit;
        plan->hdist = hdist;
        plan->hclen = hclen;
        plan->dynamic = bits;
}

static fn gzip_block_plan(gzip_encoder address_to e, positive length, gzip_plan address_to plan)
{
        gzip_plan_counts(e->lit_freq, e->dist_freq, e->bitn, length, plan);
}

/* The open block as a dynamic block, or fixed or stored when the tree
   would not pay; then a new block is open. */
static fn gzip_block_emit(gzip_encoder address_to e, p8 address_to src, positive length,
                          bool last)
{
        gzip_plan plan;
        p32 lit_code[GZIP_MAXLIT];
        p32 dist_code[GZIP_MAXDIST];
        p32 ccode[19];
        positive i, run;
        p8 here;

        gzip_block_plan(e, length, address_of plan);
        if (plan.stored <= plan.fixed && plan.stored <= plan.dynamic)
        {
                gzip_write_stored(e, src, length, last);
                gzip_block_open(e);
                return;
        }
        if (plan.fixed <= plan.dynamic)
        {
                gzip_write_fixed(e, src, length, last);
                gzip_block_open(e);
                return;
        }
        huffman_codes(plan.lit_len, GZIP_MAXLIT, lit_code);
        huffman_codes(plan.dist_len, GZIP_MAXDIST, dist_code);
        huffman_codes(plan.clen, 19, ccode);
        gzip_put_bits(e, last ? 1 : 0, 1);
        gzip_put_bits(e, 2, 2);
        gzip_put_bits(e, (p32)(plan.hlit - 257), 5);
        gzip_put_bits(e, (p32)(plan.hdist - 1), 5);
        gzip_put_bits(e, (p32)(plan.hclen - 4), 4);
        for (i = 0; i < plan.hclen; i++)
                gzip_put_bits(e, plan.clen[gzip_clen_order[i]], 3);

        for (i = 0; i < plan.ntok; i++)
        {
                positive sym = plan.tok[i];

                gzip_put_bits(e, ccode[sym] & 0xffff, ccode[sym] >> 16);
                if (sym == 16)
                        gzip_put_bits(e, plan.tok_extra[i], 2);
                else if (sym == 17)
                        gzip_put_bits(e, plan.tok_extra[i], 3);
                else if (sym == 18)
                        gzip_put_bits(e, plan.tok_extra[i], 7);
        }
        gzip_write_tokens(e, src, length, lit_code, dist_code);
        gzip_block_open(e);
}

/* Every table position down 32 KiB, and zero for any that falls out: one
   saturating subtract a lane. */
static fn gzip_slide(gzip_encoder address_to e)
{
        p16 address_to t = e->head4;
        positive n = (sizeof(e->head4) + sizeof(e->head3) + sizeof(e->prev)) / sizeof(p16);

        for (positive i = 0; i < n; i++)
                t[i] = (p16)(t[i] > GZIP_WINDOW ? t[i] - GZIP_WINDOW : 0);
        e->slid += GZIP_WINDOW;
}

static fn gzip_finder_open(gzip_encoder address_to e)
{
        memory_fill(e->head4, 0,
                    sizeof(e->head4) + sizeof(e->head3) + sizeof(e->prev));
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
        /* Held positions are relative plus 32 KiB: a node is in the window
           when it is above rel, and zero is none. */
        b32 floor = rel;
        p32 cur = (p32)rel + GZIP_WINDOW;
        p8 address_to window = e->base + e->slid - GZIP_WINDOW;
        p8 address_to here = e->base + pos;
        p32 seq = memory_load_unaligned(p32, here);
        b32 node3 = e->head3[address_to h3];
        b32 node = e->head4[address_to h4];
        p8 address_to match;

        e->head3[address_to h3] = (p16)cur;
        e->head4[address_to h4] = (p16)cur;
        e->prev[rel] = (p16)node;
        gzip_chain_ahead(e, h3, h4, here + 1);
        /* The window's line the next position's newest candidate is on,
           fetched now that the table's cell is on its way. */
        __builtin_prefetch(window + e->head4[address_to h4]);
        if (best < 4)
        {
                if (node3 <= floor)
                        return best;
                if (best < 3 &&
                    ((memory_load_unaligned(p32, window + node3) ^ seq) & 0xffffff) == 0)
                {
                        best = 3;
                        address_to dist = (positive)(cur - (p32)node3);
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
        p8 address_to at = e->base + pos + 1;

        while (count)
        {
                positive rel = pos - e->slid;
                positive run;
                p16 address_to link;
                p16 stored;

                if (rel >= GZIP_WINDOW)
                {
                        gzip_slide(e);
                        rel = pos - e->slid;
                }
                /* Up to where the positions would leave the table's range. */
                run = GZIP_WINDOW - rel;
                if (run > count)
                        run = count;
                count -= run;
                pos += run;
                link = e->prev + rel;
                stored = (p16)(rel + GZIP_WINDOW);
                for (; run; run--, link++, stored++, at++)
                {
                        p32 seq = memory_load_unaligned(p32, at);

                        e->head3[a] = stored;
                        *link = e->head4[b];
                        e->head4[b] = stored;
                        a = gzip_hash(seq << 8, GZIP_HASH3_BITS);
                        b = gzip_hash(seq, GZIP_HASH4_BITS);
                }
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

static __attribute__((noinline)) positive gzip_parse_fast(gzip_encoder address_to e,
                                                          positive start, positive pos,
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
               ((bipolar)top_bit_known(dist) - (bipolar)top_bit_known(next_dist));
}

static inline INLINE positive gzip_parse_chain(gzip_encoder address_to e, positive start,
                                               positive pos, positive limit, p8 parse,
                                               positive depth, positive nice)
{
        p8 address_to base = e->base;
        positive total = e->total;
        positive cap = min(limit, start + GZIP_SPLIT_MOST);
        positive least = gzip_min_match_ahead(base + pos, cap - pos, depth);
        positive recount = pos + min(total - pos, (positive)10000);
        positive far = parse >= GZIP_PARSE_LAZY ? 8192 : 4096;
        positive tokens = e->fresh_n;
        positive pairs = e->pairs;
        p32 h3 = e->hash3, h4 = e->hash4;

        while (pos < cap)
        {
                positive most = total - pos;
                positive dist = 0, length;

                if (tokens >= GZIP_SPLIT_CHECK)
                {
                        e->fresh_n = tokens;
                        if (gzip_split_test(e, pos - start, total - pos))
                                break;
                        tokens = e->fresh_n;
                }
                if (parse >= GZIP_PARSE_LAZY && pos >= recount)
                {
                        least = gzip_min_match_seen(e, depth);
                        recount += min(total - recount, pos - start);
                }
                if (most > GZIP_MAX_MATCH)
                        most = GZIP_MAX_MATCH;
                length = gzip_chain_find(e, pos, least - 1, most, nice, depth,
                                         address_of h3, address_of h4, address_of dist);
                if (length < least || (length == 3 && dist > far))
                {
                        e->lit_freq[base[pos]]++;
                        tokens++;
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
                                        e->lit_freq[base[pos]]++;
                                        tokens++;
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
                                        e->lit_freq[base[pos]]++;
                                        e->lit_freq[base[pos + 1]]++;
                                        tokens += 2;
                                        pos += 2;
                                        length = next_length;
                                        dist = next_dist;
                                        taken = 1;
                                        continue;
                                }
                                break;
                        }
                e->mpos[pairs] = (p32)(pos - start);
                e->mlen[pairs] = (p16)length;
                e->mdist[pairs] = (p16)dist;
                pairs++;
                tokens++;
                e->lit_freq[257 + deflate_symbol_tab[length - 3]]++;
                e->dist_freq[gzip_dist_code(dist)]++;
                e->fresh[8 + (length >= 9)]++;
                if (length > taken)
                        gzip_chain_skip(e, pos + taken, length - taken, address_of h3,
                                        address_of h4);
                pos += length;
                if (pairs >= GZIP_PAIRS - 2)
                        break;
        }
        e->pairs = pairs;
        e->fresh_n = tokens;
        e->hash3 = h3;
        e->hash4 = h4;
        return pos;
}

/* Each parse its own function, so each gets the registers to itself. */
static __attribute__((noinline)) positive gzip_parse_greedy(gzip_encoder address_to e,
                                                            positive start, positive pos,
                                                            positive limit, positive depth,
                                                            positive nice)
{
        return gzip_parse_chain(e, start, pos, limit, GZIP_PARSE_GREEDY, depth, nice);
}

static __attribute__((noinline)) positive gzip_parse_lazy(gzip_encoder address_to e,
                                                          positive start, positive pos,
                                                          positive limit, positive depth,
                                                          positive nice)
{
        return gzip_parse_chain(e, start, pos, limit, GZIP_PARSE_LAZY, depth, nice);
}

static __attribute__((noinline)) positive gzip_parse_lazy2(gzip_encoder address_to e,
                                                           positive start, positive pos,
                                                           positive limit, positive depth,
                                                           positive nice)
{
        return gzip_parse_chain(e, start, pos, limit, GZIP_PARSE_LAZY2, depth, nice);
}

/*
        The optimal parser, --ultra.

        Every position of a deflate block is searched in binary trees keyed
        by a four-byte hash, and the matches each finds, longest last and
        each longer than the one before, wait in a cache. The block is then
        parsed backwards: the cheapest way from each position to the block's
        end, a literal or any length of any cached match, priced from a cost
        model in sixteenths of a bit. The first pass prices from defaults,
        or from the last block's prices when the symbols look alike; each
        further pass prices from the code lengths the last pass's parse
        gives, and the cheapest block seen is kept. A block of only
        literals and, for small blocks, the fixed code are priced too.
        Blocks end where the symbol-kind test says the statistics moved,
        cut back to the last test that said they had not. This is
        libdeflate's near-optimal parser, its levels 10 to 12.
*/
#define GZIP_OPT_MOST 300000
#define GZIP_OPT_CACHE (GZIP_OPT_MOST * 5)
#define GZIP_OPT_DEPTH 300
#define GZIP_OPT_NICE 258
#define GZIP_OPT_PASSES 10
#define GZIP_OPT_STATIC 10000
#define GZIP_BIT 16
/* Places a chosen block may be cut again at, by cost. */
#define GZIP_CUTS 32
#define GZIP_OPT_BLOCK (1u << 20)

typedef struct
{
        p16 length;
        p16 offset;
} gzip_match;

typedef struct
{
        p32 cost;
        /* length | offset << 9, a literal being length 1 and its byte */
        p32 item;
} gzip_node;

typedef struct
{
        p32 lit[256];
        p32 len[GZIP_MAX_MATCH + 1];
        p32 off[GZIP_MAXDIST];
} gzip_costs;

struct gzip_ultra
{
        /* Positions plus one, zero for none: two a three-byte hash, a tree
           root a four-byte hash, and each position's two children. */
        p32 hash3[2u << 16];
        p32 hash4[1u << 16];
        p32 child[2 * GZIP_WINDOW];
        gzip_costs costs;
        gzip_costs saved;
        p32 prev_seen[10];
        p32 prev_seen_n;
        /* Longest match at each observed position: the merged block's, and
           those since the last split test. */
        p32 len_freq[GZIP_MAX_MATCH + 1];
        p32 new_len_freq[GZIP_MAX_MATCH + 1];
        gzip_match cache[GZIP_OPT_CACHE + 4 * GZIP_MAX_MATCH];
        gzip_node node[GZIP_OPT_MOST + GZIP_SPLIT_LEAST + 2 * GZIP_MAX_MATCH];
        /* The split search's running symbol counts at each candidate
           boundary, and where each boundary falls. */
        p32 cum_lit[GZIP_CUTS + 1][GZIP_MAXLIT];
        p32 cum_dist[GZIP_CUTS + 1][GZIP_MAXDIST];
        positive cut_pair[GZIP_CUTS + 1];
        positive cut_byte[GZIP_CUTS + 1];
        p32 mpos[(GZIP_OPT_MOST + GZIP_SPLIT_LEAST) / 3 + 8];
        p16 mlen[(GZIP_OPT_MOST + GZIP_SPLIT_LEAST) / 3 + 8];
        p16 mdist[(GZIP_OPT_MOST + GZIP_SPLIT_LEAST) / 3 + 8];
};

/* 65536 log2(x), floored, for x at least 1. */
static p64 gzip_log2_q16(p64 x)
{
        positive whole = 63 - (positive)bits_leading_zeros(x);
        p64 m = whole >= 31 ? x >> (whole - 31) : x << (31 - whole);
        p64 fraction = 0;

        for (positive bit = 15; bit < 16; bit--)
        {
                m = (m * m) >> 31;
                if (m >= (1ull << 32))
                {
                        m >>= 1;
                        fraction |= 1ull << bit;
                }
        }
        return ((p64)whole << 16) | fraction;
}

/* In sixteenths of a bit, log2(a / b), floored; a >= b. */
static p32 gzip_bits16(p64 a, p64 b)
{
        return (p32)((gzip_log2_q16(a) - gzip_log2_q16(b)) >> 12);
}

/* One position into the trees, and when out is not null, its matches
   written there: first a three-byte one when found, then each longer
   than the last. most bytes may match, and at least five follow pos. */
static inline INLINE gzip_match address_to gzip_bt_advance(struct gzip_ultra address_to u,
                                                           p8 address_to base, positive pos,
                                                           positive most, positive nice,
                                                           positive depth, p32 address_to h3,
                                                           p32 address_to h4,
                                                           gzip_match address_to out)
{
        p8 address_to here = base + pos;
        p32 floor = pos + 1 > GZIP_WINDOW ? (p32)(pos + 1 - GZIP_WINDOW) : 0;
        p32 cur = (p32)(pos + 1);
        p32 seq = memory_load_unaligned(p32, here + 1);
        p32 a = address_to h3, b = address_to h4;
        p32 node3, node3b, node;
        p32 address_to lt;
        p32 address_to gt;
        positive best_lt = 0, best_gt = 0, len = 0, best = 3;

        address_to h3 = gzip_hash(seq << 8, 16);
        address_to h4 = gzip_hash(seq, 16);
        __builtin_prefetch(u->hash3 + 2 * address_to h3, 1);
        __builtin_prefetch(u->hash4 + address_to h4, 1);
        node3 = u->hash3[2 * a];
        node3b = u->hash3[2 * a + 1];
        u->hash3[2 * a] = cur;
        u->hash3[2 * a + 1] = node3;
        if (out && node3 > floor)
        {
                p32 seq3 = memory_load_unaligned(p32, here) & 0xffffff;

                if ((memory_load_unaligned(p32, base + node3 - 1) & 0xffffff) == seq3)
                {
                        out->length = 3;
                        out->offset = (p16)(cur - node3);
                        out++;
                }
                else if (node3b > floor &&
                         (memory_load_unaligned(p32, base + node3b - 1) & 0xffffff) == seq3)
                {
                        out->length = 3;
                        out->offset = (p16)(cur - node3b);
                        out++;
                }
        }
        node = u->hash4[b];
        u->hash4[b] = cur;
        lt = u->child + 2 * (pos & GZIP_WMASK);
        gt = lt + 1;
        if (node <= floor)
        {
                *lt = 0;
                *gt = 0;
                return out;
        }
        for (;;)
        {
                p8 address_to match = base + node - 1;
                p32 address_to kids = u->child + 2 * ((node - 1) & GZIP_WMASK);

                if (match[len] == here[len])
                {
                        len = gzip_extend(here, match, len + 1, most);
                        if (!out || len > best)
                        {
                                if (out)
                                {
                                        best = len;
                                        out->length = (p16)len;
                                        out->offset = (p16)(cur - node);
                                        out++;
                                }
                                if (len >= nice)
                                {
                                        *lt = kids[0];
                                        *gt = kids[1];
                                        return out;
                                }
                        }
                }
                if (match[len] < here[len])
                {
                        *lt = node;
                        lt = kids + 1;
                        node = *lt;
                        best_lt = len;
                        if (best_gt < len)
                                len = best_gt;
                }
                else
                {
                        *gt = node;
                        gt = kids;
                        node = *gt;
                        best_gt = len;
                        if (best_lt < len)
                                len = best_lt;
                }
                if (node <= floor || !--depth)
                {
                        *lt = 0;
                        *gt = 0;
                        return out;
                }
        }
}

/* The prices a code's lengths give; a symbol the code left out is priced
   as if it were rare. */
static fn gzip_costs_from(gzip_costs address_to c, p8 address_to lit_len, p8 address_to dist_len)
{
        for (positive i = 0; i < 256; i++)
                c->lit[i] = (lit_len[i] ? lit_len[i] : 13) * GZIP_BIT;
        for (positive length = 3; length <= GZIP_MAX_MATCH; length++)
        {
                positive code = deflate_symbol_tab[length - 3];
                positive bits = lit_len[257 + code] ? lit_len[257 + code] : 13;

                c->len[length] = (p32)((bits + gzip_len_extra[code]) * GZIP_BIT);
        }
        for (positive i = 0; i < 30; i++)
                c->off[i] = (p32)(((dist_len[i] ? dist_len[i] : 10) + gzip_dist_extra[i]) *
                                  GZIP_BIT);
}

/* Prices from the code lengths a pass's parse gives, moved a quarter of
   the way toward the symbol counts themselves: -log2 of a symbol's share,
   in sixteenths of a bit, where a code length would round it to whole
   bits. Whole-bit lengths alone stall the search a percent or two short
   of what fractional prices reach; a quarter measured best of the shares
   tried (0, 6, 8, 12 and 16 sixteenths). */
static fn gzip_costs_freq(gzip_costs address_to c, p32 address_to lit_freq,
                          p32 address_to dist_freq, p8 address_to lit_len, p8 address_to dist_len,
                          positive share)
{
        p64 lit_total = 0, dist_total = 0, lit_log, dist_log;
        gzip_costs f;

        for (positive i = 0; i < 286; i++)
                lit_total += lit_freq[i];
        for (positive i = 0; i < 30; i++)
                dist_total += dist_freq[i];
        lit_log = gzip_log2_q16(lit_total ? lit_total : 1);
        dist_log = gzip_log2_q16(dist_total ? dist_total : 1);
        gzip_costs_from(c, lit_len, dist_len);
        gzip_costs_from(address_of f, lit_len, dist_len);
        for (positive i = 0; i < 256; i++)
                if (lit_freq[i])
                        f.lit[i] = (p32)((lit_log - gzip_log2_q16(lit_freq[i])) >> 12);
        for (positive length = 3; length <= GZIP_MAX_MATCH; length++)
        {
                positive code = deflate_symbol_tab[length - 3];

                if (lit_freq[257 + code])
                        f.len[length] = (p32)(((lit_log - gzip_log2_q16(lit_freq[257 + code])) >> 12) +
                                              gzip_len_extra[code] * GZIP_BIT);
        }
        for (positive i = 0; i < 30; i++)
                if (dist_freq[i])
                        f.off[i] = (p32)(((dist_log - gzip_log2_q16(dist_freq[i])) >> 12) +
                                         gzip_dist_extra[i] * GZIP_BIT);
        for (positive i = 0; i < 256; i++)
                c->lit[i] = (c->lit[i] * (16 - share) + f.lit[i] * share) / 16;
        for (positive i = 3; i <= GZIP_MAX_MATCH; i++)
                c->len[i] = (c->len[i] * (16 - share) + f.len[i] * share) / 16;
        for (positive i = 0; i < 30; i++)
                c->off[i] = (c->off[i] * (16 - share) + f.off[i] * share) / 16;
}

/* Default prices for a block: a literal by how many kinds of byte it has
   and how often a greedy parse found matches, a length symbol by the
   same, a distance symbol as one of thirty. */
static fn gzip_costs_default(struct gzip_ultra address_to u, p8 address_to block,
                             positive length, gzip_costs address_to c)
{
        p32 counts[256];
        positive kinds = 0, literals = length, matches = 0, share;
        p32 lit_cost, len_cost;

        memory_fill(counts, 0, sizeof(counts));
        for (positive i = 0; i < length; i++)
                counts[block[i]]++;
        for (positive i = 0; i < 256; i++)
                kinds += counts[i] > (length >> 11);
        if (!kinds)
                kinds = 1;
        for (positive i = gzip_min_match(kinds, GZIP_OPT_DEPTH); i <= GZIP_MAX_MATCH; i++)
        {
                matches += u->len_freq[i];
                literals -= min(literals, i * u->len_freq[i]);
        }
        share = matches > literals ? 2 : matches * 4 > literals ? 1 : 0;
        /* -log2((1 - p) / kinds) with p = 1/4, 1/2 or 3/4, and -log2(p / 29) */
        lit_cost = share == 0 ? gzip_bits16(4 * kinds, 3)
                   : share == 1 ? gzip_bits16(2 * kinds, 1) : gzip_bits16(4 * kinds, 1);
        len_cost = share == 0 ? gzip_bits16(116, 1) : share == 1 ? gzip_bits16(58, 1)
                                                            : gzip_bits16(116, 3);
        for (positive i = 0; i < 256; i++)
                c->lit[i] = lit_cost;
        for (positive i = 3; i <= GZIP_MAX_MATCH; i++)
                c->len[i] = len_cost + gzip_len_extra[deflate_symbol_tab[i - 3]] * GZIP_BIT;
        for (positive i = 0; i < 30; i++)
                c->off[i] = 4 * GZIP_BIT + (907 * GZIP_BIT) / 1000 + gzip_dist_extra[i] * GZIP_BIT;
}

/* The first pass's prices: the defaults, blended with the last block's
   prices by how alike the two blocks' symbol kinds look. */
static fn gzip_costs_open(gzip_encoder address_to e, struct gzip_ultra address_to u,
                          p8 address_to block, positive length, bool first)
{
        gzip_costs d;
        p64 delta = 0, cutoff;
        positive mix;

        gzip_costs_default(u, block, length, address_of d);
        if (first || !u->prev_seen_n || !e->seen_n)
        {
                u->costs = d;
                return;
        }
        for (positive i = 0; i < 10; i++)
        {
                p64 was = (p64)u->prev_seen[i] * e->seen_n;
                p64 now = (p64)e->seen[i] * u->prev_seen_n;

                delta += was > now ? was - now : now - was;
        }
        cutoff = ((p64)u->prev_seen_n * e->seen_n * 200) / 512;
        if (delta > 3 * cutoff)
        {
                u->costs = d;
                return;
        }
        mix = 4 * delta > 9 * cutoff ? 3 : 2 * delta > 3 * cutoff ? 2 : 2 * delta > cutoff ? 1 : 0;
        p32 address_to to = (p32 address_to)address_of u->costs;
        p32 address_to from = (p32 address_to)address_of d;

        for (positive i = 0; i < sizeof(gzip_costs) / sizeof(p32); i++)
                to[i] = mix == 0 ? (from[i] + 3 * to[i]) / 4
                        : mix == 1 ? (from[i] + to[i]) / 2
                        : mix == 2 ? (5 * from[i] + 3 * to[i]) / 8 : (3 * from[i] + to[i]) / 4;
}

/* The cheapest parse of the block at the current prices, backwards from
   its stop; then its symbols counted and its pairs written. stop is the
   cache entry past the block's last position. */
static fn gzip_opt_path(gzip_encoder address_to e, struct gzip_ultra address_to u,
                        positive length, gzip_match address_to stop)
{
        gzip_node address_to node = u->node;
        gzip_match address_to c = stop;
        gzip_costs address_to cost = address_of u->costs;
        positive pairs = 0;

        node[length].cost = 0;
        for (positive at = length; at-- > 0;)
        {
                positive count;
                p32 best, item;

                c--;
                count = c->length;
                best = cost->lit[c->offset] + node[at + 1].cost;
                item = (p32)c->offset << 9 | 1;
                if (count)
                {
                        gzip_match address_to m = c - count;
                        positive len = 3;

                        do
                        {
                                positive offset = m->offset;
                                p32 oc = cost->off[gzip_dist_code(offset)];

                                do
                                {
                                        p32 here = oc + cost->len[len] + node[at + len].cost;

                                        if (here < best)
                                        {
                                                best = here;
                                                item = (p32)(len | offset << 9);
                                        }
                                } while (++len <= m->length);
                        } while (++m != c);
                        c -= count;
                }
                node[at].cost = best;
                node[at].item = item;
        }
        memory_fill(e->lit_freq, 0, sizeof(e->lit_freq));
        memory_fill(e->dist_freq, 0, sizeof(e->dist_freq));
        for (positive at = 0; at < length;)
        {
                p32 item = node[at].item;
                positive len = item & 511;
                positive offset = item >> 9;

                if (len == 1)
                        e->lit_freq[offset]++;
                else
                {
                        e->mpos[pairs] = (p32)at;
                        e->mlen[pairs] = (p16)len;
                        e->mdist[pairs] = (p16)offset;
                        pairs++;
                        e->lit_freq[257 + deflate_symbol_tab[len - 3]]++;
                        e->dist_freq[gzip_dist_code(offset)]++;
                }
                at += len;
        }
        e->pairs = pairs;
}

/* The cheapest a block with the counted symbols can be written. */
static positive gzip_opt_cost(gzip_encoder address_to e, positive length,
                              gzip_plan address_to plan)
{
        gzip_block_plan(e, length, plan);
        return min(plan->dynamic, plan->fixed);
}

/* Choose and write one block: its matches are the cache entries before
   stop. Answers whether literals alone were cheapest. */
static fn gzip_opt_emit(gzip_encoder address_to e, struct gzip_ultra address_to u,
                        p8 address_to block, positive length);

static bool gzip_opt_block(gzip_encoder address_to e, struct gzip_ultra address_to u,
                           positive start, positive length, gzip_match address_to stop,
                           bool first)
{
        p8 address_to block = e->base + start;
        gzip_plan plan;
        positive only_literals, fixed = positive_max, best = positive_max, cost = 0;
        p32 seen[10], fresh[10];
        positive seen_n = e->seen_n, fresh_n = e->fresh_n;

        memory_copy(seen, e->seen, sizeof(seen));
        memory_copy(fresh, e->fresh, sizeof(fresh));
        for (positive at = length; at <= length + GZIP_MAX_MATCH - 1; at++)
                u->node[at].cost = 0x80000000u;

        memory_fill(e->lit_freq, 0, sizeof(e->lit_freq));
        memory_fill(e->dist_freq, 0, sizeof(e->dist_freq));
        for (positive i = 0; i < length; i++)
                e->lit_freq[block[i]]++;
        only_literals = gzip_opt_cost(e, length, address_of plan);

        if (length <= GZIP_OPT_STATIC)
        {
                u->saved = u->costs;
                gzip_costs_from(address_of u->costs, gzip_fixed_lit_len, gzip_fixed_dist_len);
                gzip_opt_path(e, u, length, stop);
                fixed = u->node[0].cost / GZIP_BIT + 7 + 3;
                u->costs = u->saved;
        }
        gzip_costs_open(e, u, block, length, first);
        for (positive pass = 0; pass < GZIP_OPT_PASSES; pass++)
        {
                gzip_opt_path(e, u, length, stop);
                cost = gzip_opt_cost(e, length, address_of plan);
                if (cost + 1 > best)
                        break;
                best = cost;
                u->saved = u->costs;
                gzip_costs_freq(address_of u->costs, e->lit_freq, e->dist_freq, plan.lit_len,
                                plan.dist_len, 4);
        }
        bool literals = false;

        if (min(only_literals, fixed) < best)
        {
                if (only_literals < fixed)
                {
                        memory_fill(e->lit_freq, 0, sizeof(e->lit_freq));
                        memory_fill(e->dist_freq, 0, sizeof(e->dist_freq));
                        for (positive i = 0; i < length; i++)
                                e->lit_freq[block[i]]++;
                        e->pairs = 0;
                        gzip_block_plan(e, length, address_of plan);
                        gzip_costs_from(address_of u->costs, plan.lit_len, plan.dist_len);
                        literals = true;
                }
                else
                {
                        gzip_costs_from(address_of u->costs, gzip_fixed_lit_len,
                                        gzip_fixed_dist_len);
                        gzip_opt_path(e, u, length, stop);
                }
        }
        else if (cost > best)
        {
                u->costs = u->saved;
                gzip_opt_path(e, u, length, stop);
                gzip_block_plan(e, length, address_of plan);
                gzip_costs_from(address_of u->costs, plan.lit_len, plan.dist_len);
        }
        gzip_opt_emit(e, u, block, length);
        memory_copy(e->seen, seen, sizeof(seen));
        memory_copy(e->fresh, fresh, sizeof(fresh));
        e->seen_n = seen_n;
        e->fresh_n = fresh_n;
        return literals;
}

/* The chosen block written as the cheapest run of blocks: the pairs are
   cut at GZIP_CUTS evenly spaced places, the symbols each run of them and
   the literals between would cost are priced with their own trees and
   header, and a dynamic program picks which cuts to take. The parse is not
   redone; only the codes are, per part. Small blocks pass through. */
static fn gzip_opt_emit(gzip_encoder address_to e, struct gzip_ultra address_to u,
                        p8 address_to block, positive length)
{
        positive pairs = e->pairs;
        positive cuts, covered = 0, next = 1;
        positive best_cost[GZIP_CUTS + 1], from[GZIP_CUTS + 1];
        p32 lit[GZIP_MAXLIT], dist[GZIP_MAXDIST];
        gzip_plan plan;

        if (pairs < 96 || length < 20000)
        {
                gzip_block_emit(e, block, length, false);
                return;
        }
        cuts = min((positive)GZIP_CUTS, pairs / 24);
        for (positive k = 0; k <= cuts; k++)
                u->cut_pair[k] = k * pairs / cuts;
        memory_fill(lit, 0, sizeof(lit));
        memory_fill(dist, 0, sizeof(dist));
        memory_copy(u->cum_lit[0], lit, sizeof(lit));
        memory_copy(u->cum_dist[0], dist, sizeof(dist));
        u->cut_byte[0] = 0;
        for (positive i = 0; i < pairs; i++)
        {
                positive stop = e->mpos[i];

                /* A cut falls before the literals that lead to its pair. */
                if (next < cuts && i == u->cut_pair[next])
                {
                        memory_copy(u->cum_lit[next], lit, sizeof(lit));
                        memory_copy(u->cum_dist[next], dist, sizeof(dist));
                        u->cut_byte[next] = covered;
                        next++;
                }
                for (; covered < stop; covered++)
                        lit[block[covered]]++;
                lit[257 + deflate_symbol_tab[e->mlen[i] - 3]]++;
                dist[gzip_dist_code(e->mdist[i])]++;
                covered = stop + e->mlen[i];
        }
        for (; covered < length; covered++)
                lit[block[covered]]++;
        memory_copy(u->cum_lit[cuts], lit, sizeof(lit));
        memory_copy(u->cum_dist[cuts], dist, sizeof(dist));
        u->cut_byte[cuts] = length;
        best_cost[0] = 0;
        for (positive j = 1; j <= cuts; j++)
        {
                best_cost[j] = positive_max;
                for (positive i = 0; i < j; i++)
                {
                        positive part;

                        for (positive c = 0; c < GZIP_MAXLIT; c++)
                                lit[c] = u->cum_lit[j][c] - u->cum_lit[i][c];
                        for (positive c = 0; c < GZIP_MAXDIST; c++)
                                dist[c] = u->cum_dist[j][c] - u->cum_dist[i][c];
                        gzip_plan_counts(lit, dist, 0, u->cut_byte[j] - u->cut_byte[i],
                                         address_of plan);
                        part = min(plan.dynamic, min(plan.fixed, plan.stored));
                        if (best_cost[i] + part < best_cost[j])
                        {
                                best_cost[j] = best_cost[i] + part;
                                from[j] = i;
                        }
                }
        }
        {
                positive order[GZIP_CUTS + 1], count = 0;
                p32 address_to base_pos = e->mpos;
                p16 address_to base_len = e->mlen;
                p16 address_to base_dist = e->mdist;

                for (positive j = cuts; j; j = from[j])
                        order[count++] = j;
                for (positive part = count; part--;)
                {
                        positive j = order[part];
                        positive i = from[j];
                        positive first = u->cut_pair[i], last = u->cut_pair[j];
                        positive shift = u->cut_byte[i];

                        for (positive c = 0; c < GZIP_MAXLIT; c++)
                                e->lit_freq[c] = u->cum_lit[j][c] - u->cum_lit[i][c];
                        for (positive c = 0; c < GZIP_MAXDIST; c++)
                                e->dist_freq[c] = u->cum_dist[j][c] - u->cum_dist[i][c];
                        for (positive k = first; k < last; k++)
                                base_pos[k] -= (p32)shift;
                        e->mpos = base_pos + first;
                        e->mlen = base_len + first;
                        e->mdist = base_dist + first;
                        e->pairs = last - first;
                        gzip_block_emit(e, block + shift, u->cut_byte[j] - shift, false);
                }
                e->mpos = base_pos;
                e->mlen = base_len;
                e->mdist = base_dist;
        }
}

/* The open input from pos to its stop in optimally parsed blocks, then the
   empty stored block that ends it on a byte boundary. */
static fn gzip_ultra_run(gzip_encoder address_to e, positive pos)
{
        struct gzip_ultra address_to u = e->ultra;
        p8 address_to base = e->base;
        positive total = e->total;
        positive start = pos;
        gzip_match address_to cache = u->cache;
        gzip_match address_to at = cache;
        bool first = true, literals = false;
        p32 h3 = 0, h4 = 0;

        memory_fill(u->hash3, 0, sizeof(u->hash3));
        memory_fill(u->hash4, 0, sizeof(u->hash4));
        memory_fill(u->len_freq, 0, sizeof(u->len_freq));
        memory_fill(u->new_len_freq, 0, sizeof(u->new_len_freq));
        u->prev_seen_n = 0;
        if (total >= 5)
        {
                p32 seq = memory_load_unaligned(p32, base);

                h3 = gzip_hash(seq << 8, 16);
                h4 = gzip_hash(seq, 16);
        }
        for (positive p = 0; p < pos && total - p >= 5; p++)
                gzip_bt_advance(u, base, p, GZIP_OPT_NICE, GZIP_OPT_NICE, GZIP_OPT_DEPTH,
                                address_of h3, address_of h4, null);
        gzip_block_open(e);
        while (pos < total)
        {
                positive cap = total - start < GZIP_OPT_MOST + GZIP_SPLIT_LEAST
                                       ? total : start + GZIP_OPT_MOST;
                positive least = literals ? GZIP_MAX_MATCH + 1
                                          : gzip_min_match_ahead(base + start, cap - start,
                                                                 GZIP_OPT_DEPTH);
                positive checked = 0, observe = pos;
                bool moved = false;

                for (;;)
                {
                        positive most = min(total - pos, (positive)GZIP_MAX_MATCH);
                        positive nice = min(most, (positive)GZIP_OPT_NICE);
                        gzip_match address_to matches = at;
                        positive best = 0;

                        if (most >= 5)
                        {
                                at = gzip_bt_advance(u, base, pos, most, nice, GZIP_OPT_DEPTH,
                                                     address_of h3, address_of h4, matches);
                                if (at > matches)
                                        best = at[-1].length;
                        }
                        if (pos >= observe)
                        {
                                if (best >= least)
                                {
                                        e->fresh[8 + (best >= 9)]++;
                                        u->new_len_freq[best]++;
                                        observe = pos + best;
                                }
                                else
                                {
                                        p8 c = base[pos];

                                        e->fresh[((c >> 5) & 6) | (c & 1)]++;
                                        observe = pos + 1;
                                }
                                e->fresh_n++;
                        }
                        at->length = (p16)(at - matches);
                        at->offset = base[pos];
                        at++;
                        pos++;
                        if (best >= 3 && best >= nice)
                                for (positive skip = best - 1; skip; skip--)
                                {
                                        most = min(total - pos, (positive)GZIP_MAX_MATCH);
                                        nice = min(most, (positive)GZIP_OPT_NICE);
                                        if (most >= 5)
                                                gzip_bt_advance(u, base, pos, nice, nice,
                                                                GZIP_OPT_DEPTH, address_of h3,
                                                                address_of h4, null);
                                        at->length = 0;
                                        at->offset = base[pos];
                                        at++;
                                        pos++;
                                }
                        if (pos >= cap || at >= cache + GZIP_OPT_CACHE)
                                break;
                        if (e->fresh_n < GZIP_SPLIT_CHECK || pos - start < GZIP_SPLIT_LEAST ||
                            total - pos < GZIP_SPLIT_LEAST)
                                continue;
                        if (gzip_split_test(e, pos - start, total - pos))
                        {
                                moved = true;
                                break;
                        }
                        for (positive i = 0; i <= GZIP_MAX_MATCH; i++)
                        {
                                u->len_freq[i] += u->new_len_freq[i];
                                u->new_len_freq[i] = 0;
                        }
                        checked = pos;
                }
                if (moved && checked)
                {
                        /* End the block where the statistics had not yet
                           moved; what was found past it opens the next. */
                        gzip_match address_to cut = at;

                        for (positive back = pos - checked; back; back--)
                        {
                                cut--;
                                cut -= cut->length;
                        }
                        literals = gzip_opt_block(e, u, start, checked - start, cut, first);
                        memory_copy(cache, cut, (positive)(at - cut) * sizeof(gzip_match));
                        at = cache + (at - cut);
                        memory_copy(u->prev_seen, e->seen, sizeof(u->prev_seen));
                        u->prev_seen_n = (p32)e->seen_n;
                        memory_fill(e->seen, 0, sizeof(e->seen));
                        e->seen_n = 0;
                        memory_fill(u->len_freq, 0, sizeof(u->len_freq));
                        start = checked;
                }
                else
                {
                        for (positive i = 0; i < 10; i++)
                        {
                                e->seen[i] += e->fresh[i];
                                e->fresh[i] = 0;
                        }
                        e->seen_n += e->fresh_n;
                        e->fresh_n = 0;
                        for (positive i = 0; i <= GZIP_MAX_MATCH; i++)
                        {
                                u->len_freq[i] += u->new_len_freq[i];
                                u->new_len_freq[i] = 0;
                        }
                        literals = gzip_opt_block(e, u, start, pos - start, at, first);
                        at = cache;
                        memory_copy(u->prev_seen, e->seen, sizeof(u->prev_seen));
                        u->prev_seen_n = (p32)e->seen_n;
                        memory_fill(e->seen, 0, sizeof(e->seen));
                        memory_fill(e->fresh, 0, sizeof(e->fresh));
                        e->seen_n = 0;
                        e->fresh_n = 0;
                        memory_fill(u->len_freq, 0, sizeof(u->len_freq));
                        memory_fill(u->new_len_freq, 0, sizeof(u->new_len_freq));
                        start = pos;
                }
                first = false;
        }
        gzip_write_stored(e, e->base + pos, 0, false);
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
                        pos = gzip_parse_greedy(e, start, pos, e->total, shape.depth, shape.nice);
                else if (shape.parse == GZIP_PARSE_LAZY)
                        pos = gzip_parse_lazy(e, start, pos, e->total, shape.depth, shape.nice);
                else
                        pos = gzip_parse_lazy2(e, start, pos, e->total, shape.depth, shape.nice);
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
        if (e->ultra)
        {
                gzip_ultra_run(e, history);
                return;
        }
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
        e->level = level;
        e->mpos = e->own_pos;
        e->mlen = e->own_len;
        e->mdist = e->own_dist;
        if (level == GZIP_ULTRA)
        {
                e->ultra = (struct gzip_ultra address_to)memory_checked(sizeof(struct gzip_ultra));
                if (!e->ultra)
                {
                        memory_free(e, sizeof(gzip_encoder));
                        return null;
                }
                e->mpos = e->ultra->mpos;
                e->mlen = e->ultra->mlen;
                e->mdist = e->ultra->mdist;
        }
        e->last = positive_max - 1;
        return e;
}

static fn gzip_writer_close(void)
{
        for (positive i = 0; i < gzip_writer.slot_count; i++)
        {
                gzip_encoder address_to e = gzip_writer.slots[i];

                if (e)
                {
                        if (e->ultra)
                                memory_free(e->ultra, sizeof(struct gzip_ultra));
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

        if (e->last + 1 == w->blocks + index && e->total < (1u << 30) && !e->ultra)
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
        gzip_writer.level = level > GZIP_ULTRA ? 9 : level;
        gzip_block_size = gzip_writer.level == GZIP_ULTRA ? GZIP_OPT_BLOCK : (1u << 16);
        header[8] = gzip_writer.level == 1 ? 4 : gzip_writer.level >= 9 ? 2 : 0;
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
                    : min(width * (gzip_writer.level == GZIP_ULTRA ? 2 : GZIP_BATCH_PER_WORKER), (positive)GZIP_BATCH_BLOCKS);
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

/* One of gzip's messages, by its shape (see GZIP_WHY_EOF). */
static fn gzip_say(string_address why)
{
        p8 shape = gzip_note_shape(why);

        if (shape == '\n')
                string_format(log_error, "\ngzip: %s: %s\n", file_codec_display, why + 1);
        else if (shape == ':')
                string_format(log_error, "gzip: %s: %s\n", file_codec_display, why + 1);
        else
                string_format(log_error, "gzip: %s %s\n", file_codec_display, why + 1);
}

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
                //      has named itself already. A decode's own words are
                //      gzip's, with the name of the input in them.
                if (decode && gzip_note)
                {
                        gzip_say(gzip_note);
                        if (gzip_note_more)
                                gzip_say(gzip_note_more);
                }
                else if (gzip_why)
                        string_format(log_error, "gzip: %s\n",
                                      gzip_why + (!string_compare_max(gzip_why, "gzip ", 5)
                                                          ? 5 : 0));
                gzip_status = 1;
                return 1;
        }
        if (decode && gzip_garbage)
        {
                gzip_say(GZIP_WHY_GARBAGE);
                gzip_warned = true;
        }
        return 0;
}

static const file_codec_suffix gzip_suffixes[] = {
    {".gz", ""}, {".Z", ""}, {".tgz", ".tar"}};

/* --ultra: the optimal parser, past -9. GNU gzip refuses the word, so no
   GNU spelling changes meaning; -10 and up still read as their last digit. */
static bipolar gzip_option(file_codec_cli address_to codec, string_address at, bool word)
{
        if (!word || !string_equals(at, "--ultra"))
                return 0;
        codec->level = GZIP_ULTRA;
        return 1;
}

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
            .run = gzip_stream, .option = gzip_option};
        gzip_warned = false;
        b32 status = file_codec_main(address_of codec);

        /* A warning ends the run with 2 unless something failed. */
        return status ? status : gzip_warned ? 2 : 0;
}

#endif /* GZIP_CORE_ONLY */
