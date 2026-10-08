/*
        zstd -- RFC 8878 decode and encode.

        Decode is the existing frame walker: content checksum is hash_xxh64,
        match copies are memory_copy_match, backward bitstreams are
        zstd_bits_open. Encode runs libzstd's levels from --fast to 22: hash
        chain, row hash and binary tree match finders under the fast, double
        fast, lazy and price-driven parsers, repeat offsets and sequence FSE
        tables. Literals use four Huffman streams with
        direct or FSE-coded weights; raw literals/blocks win when entropy
        coding would grow. Match lengths are memory_common_prefix. A large
        input is cut into jobs the pool encodes at once. Dictionaries are
        read (-D) and trained (--train). Concatenated frames and skippable
        frames are accepted the way zstd -d accepts them.
*/

#define ZSTD_MAGIC 0xFD2FB528u
#define ZSTD_SKIP_MAGIC 0x184D2A50u
#define ZSTD_SKIP_MASK 0xFFFFFFF0u
#define ZSTD_WINDOW_MAX (1u << 27)
#define ZSTD_BLOCK_MAX (1u << 17)
#define ZSTD_IN 262144
#define ZSTD_OUT 131072
#define ZSTD_FSE_MAX 512
#define ZSTD_HUF_MAX 2048

static const bipolar zstd_ll_default[36] = {
        4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
        -1, -1, -1, -1};
static const bipolar zstd_ml_default[53] = {
        1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1,
        -1, -1, -1, -1, -1};
static const bipolar zstd_of_default[29] = {
        1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

/*
        One FSE cell, eight bytes, the same layout zstd's sequence walker
        keeps: next state, extra bits, FSE nbits, baseline. Huffman weight
        tables leave extra 0 and the symbol in base, and are never fused.
*/
typedef struct
{
        p16 next;
        p8 extra;
        p8 bits;
        p32 base;
} zstd_fse_cell;

typedef struct
{
        p8 log;
        p8 rle;
        bool valid;
        p8 pad;
        p32 cells_at_8;
        zstd_fse_cell cell[ZSTD_FSE_MAX];
} zstd_fse;

typedef struct
{
        p8 address_to window;
        positive pos;
        positive window_size;
        p8 address_to lits;
        positive lit_len;
        p8 address_to seq;
        positive seq_len;
        zstd_fse address_to ll;
        zstd_fse address_to of;
        zstd_fse address_to ml;
        p32 address_to rep;
        positive nseq;
        p8 address_to output_end;
} zstd_seq_job;

/* The two halves of zstd_sequences_run the pool's decoder runs apart, lib.c's
   zstd_sequences_decode and zstd_sequences_exec: the layouts of their jobs
   (see the x86_64 assembly). */
typedef struct
{
        p8 address_to seq;
        positive seq_len;
        zstd_fse address_to ll;
        zstd_fse address_to of;
        zstd_fse address_to ml;
        positive nseq;
        p32 address_to out;
        p32 rep[3];
        p32 max_off;
        positive sym_end;
        p64 sum_ll;
        p64 sum_ml;
} zstd_seq_decode_job;

typedef struct
{
        p8 address_to out;
        p8 address_to lits;
        p32 address_to recs;
        positive nseq;
} zstd_seq_exec_job;

typedef struct
{
        p8 max_bits;
        bool valid;
        /* The weights the table was built from, with the last one, for a
           dictionary's encoder to make its codes from. */
        p8 real_bits;
        positive weights;
        p8 weight[256];
        p16 cell[ZSTD_HUF_MAX];
} zstd_huff;

/* 48 bytes. Layout is the lib.c floor ABI: bits, consumed, ptr,
   start, limit, last. Huffman, sequences, and FSE unpack share it. */
typedef struct
{
        p64 bits;
        positive consumed;
        p8 address_to ptr;
        p8 address_to start;
        p8 address_to limit;
        p8 address_to last;
} zstd_bits;

typedef struct
{
        p64 total;
        p64 acc[4];
        p64 seed;
        p8 hold[32];
        p8 held;
} zstd_xxh;

static string_address zstd_why;
static p8 zstd_in_buf[ZSTD_IN];
static byte_input zstd_src = {.buf = zstd_in_buf, .room = ZSTD_IN};
static p8 address_to zstd_window;
static positive zstd_window_cap;
/* The mapping behind the window when it is a ring: ring_size bytes of one
   memfd mapped twice, back to back, at zstd_ring. Null when the window is a
   plain mapping twice the window's size. */
static p8 address_to zstd_ring;
static positive zstd_ring_size;
static positive zstd_window_size;
static positive zstd_pos;
static positive zstd_keep;
static byte_store zstd_output;
static bipolar zstd_out_fd;
static p8 zstd_out_buf[ZSTD_OUT];
static positive zstd_out_fill;
static positive zstd_out_taken;
static bool zstd_pull;
static bool zstd_paused;
static bool zstd_live;
static bool zstd_finished;
static p8 address_to zstd_rest;
static positive zstd_rest_n;
static bool zstd_frame_open;
static bool zstd_need_trailer;
static bool zstd_checksum_on;
static p64 zstd_frame_begin;
static bool zstd_hold_emit;
static bool zstd_hashing;
static zstd_xxh zstd_hash;
static p64 zstd_decoded;
static p64 zstd_fcs;
static bool zstd_have_fcs;
static p32 zstd_rep[3];
static zstd_huff zstd_lit_huff;
/* Each sequence alphabet's table lives in one of two buffers, and a block
   takes its tables by pointer: a repeated table is the previous block's
   own, a default is the prebuilt one, and a new one is built into the
   buffer the previous block did not use, so no table is ever copied. */
static zstd_fse zstd_seq_tables[3][2];
static zstd_fse address_to zstd_seq_prev[3];
static p8 zstd_lit_buf[ZSTD_BLOCK_MAX + 64];
static p8 zstd_comp[ZSTD_BLOCK_MAX + 32];
static bool zstd_fse_ready;
static zstd_fse zstd_ll_def;
static zstd_fse zstd_of_def;
static zstd_fse zstd_ml_def;
static positive zstd_block_limit;

static p32 zstd_get24(p8 address_to p)
{
        return (p32)p[0] | ((p32)p[1] << 8) | ((p32)p[2] << 16);
}

/* The highest set bit's index; zero for zero, which FSE's empty weights use.
   Once per sequence: the compiler's inline count where the ISA has one. */
static p8 zstd_highbit32(p32 value)
{
        return value ? (p8)top_bit_known(value) : 0;
}

static bool zstd_fail(string_address why)
{
        zstd_why = why;
        return false;
}

static bool zstd_in_need(positive n)
{
        return_if(n > ZSTD_IN, zstd_fail("zstd block larger than the input window"));
        if (zstd_src.fd < 0 && !zstd_src.mem)
                zstd_src.eof = true;
        bipolar got = byte_input_need(address_of zstd_src, n);
        return_if(got < 0, zstd_fail("zstd: read failed"));
        return (positive)got >= n ? true : zstd_fail("zstd truncated input");
}

static p8 address_to zstd_in_at(void)
{
        return zstd_src.buf + zstd_src.at;
}

static fn zstd_in_skip(positive n)
{
        zstd_src.at += n;
}

static bool zstd_in_take(p8 address_to into, positive n)
{
        if (!n)
                return true;
        if (!zstd_in_need(n))
                return false;
        memory_copy(into, zstd_in_at(), n);
        zstd_in_skip(n);
        return true;
}

static bool zstd_in_skip_bytes(positive n)
{
        while (n)
        {
                positive chunk;

                if (zstd_src.at == zstd_src.have && !zstd_in_need(1) && n)
                        return false;
                chunk = zstd_src.have - zstd_src.at;
                if (chunk > n)
                        chunk = n;
                zstd_in_skip(chunk);
                n -= chunk;
        }

        return true;
}

static p8 zstd_fse_peek(zstd_fse address_to table, p16 state)
{
        if (!table->log)
                return table->rle;
        return (p8)table->cell[state].base;
}

static fn zstd_fse_step(zstd_fse address_to table, p16 address_to state,
                        zstd_bits address_to bits)
{
        zstd_fse_cell cell;

        if (!table->log)
                return;

        cell = table->cell[address_to state];
        address_to state =
            (p16)(cell.next + (p16)zstd_bits_get(bits, cell.bits));
}

static p8 zstd_fse_symbol(zstd_fse address_to table, p16 address_to state,
                          zstd_bits address_to bits)
{
        p8 symbol = zstd_fse_peek(table, address_to state);

        zstd_fse_step(table, state, bits);
        return symbol;
}

/* The symbol spread both FSE directions share: "less than one" symbols
   take the top cells, the rest step through the remainder. False when the
   counts do not fill the table exactly. */
static bool zstd_fse_spread(p8 address_to symbol, const bipolar address_to norm,
                            positive max_sym, p8 log)
{
        positive mask = ((positive)1 << log) - 1;
        positive high = mask;
        positive step = ((mask + 1) >> 1) + ((mask + 1) >> 3) + 3;
        positive pos = 0;

        for (positive s = 0; s <= max_sym; s++)
                if (norm[s] == -1)
                        symbol[high--] = (p8)s;
        for (positive s = 0; s <= max_sym; s++)
                for (bipolar i = 0; i < norm[s]; i++)
                {
                        while (pos > high)
                                pos = (pos + step) & mask;
                        symbol[pos] = (p8)s;
                        pos = (pos + step) & mask;
                }
        return !pos;
}

static bool zstd_fse_build(zstd_fse address_to table, const bipolar address_to norm,
                           positive max_sym, p8 log)
{
        positive size = (positive)1 << log;
        p16 next[256];
        p8 symbol[ZSTD_FSE_MAX];
        positive s;
        positive u;

        return_if(!log, zstd_fail("zstd FSE table log is zero"));
        return_if(log > 9 || size > ZSTD_FSE_MAX, zstd_fail("zstd FSE table too large"));

        memory_fill(symbol, 0xff, size);
        memory_fill(next, 0, sizeof(next));
        for (s = 0; s <= max_sym; s++)
                next[s] = (p16)(norm[s] == -1 ? 1 : norm[s] < 0 ? 0 : norm[s]);
        if (!zstd_fse_spread(symbol, norm, max_sym, log))
                return zstd_fail("zstd FSE table did not fill");

        for (u = 0; u < size; u++)
        {
                p8 sym = symbol[u];
                p16 n = next[sym]++;
                p8 bits;

                return_if(!n, zstd_fail("zstd FSE empty cell"));
                bits = (p8)(log - zstd_highbit32(n));
                table->cell[u].extra = 0;
                table->cell[u].bits = bits;
                table->cell[u].next = (p16)((n << bits) - size);
                table->cell[u].base = (p32)sym;
        }

        table->log = log;
        table->rle = 0;
        table->valid = true;
        return true;
}

/* The literal length and match length codes' bases and extra bit counts are
   lib.c's zstd_sequence_tab, which the sequence encoder indexes too. */
#define zstd_ll_extra (zstd_sequence_tab.ll_extra)
#define zstd_ml_extra (zstd_sequence_tab.ml_extra)
#define zstd_ll_base (zstd_sequence_tab.ll_base)
#define zstd_ml_base (zstd_sequence_tab.ml_base)

/*
        Fold the sequence baselines into the FSE cells. The walker then
        loads one word per stream instead of a symbol plus two RIP tables.
        A lone RLE symbol lives in cell 0 with no FSE step, so the same
        load serves both the compressed tables and a constant one.
*/
/*
        A compressed sequence stream's table in one pass, as libzstd builds
        it: the symbols spread across the table by the fixed step (laid down
        eight bytes a store and scattered two at a time when no symbol is
        below one, the one-at-a-time spread otherwise), then every cell takes
        its next state and bit count with the symbol's baseline and extra
        bits already folded in, which zstd_seq_fuse did in a second pass.
        zstd_fse_read's counts sum to the table; the sum is checked anyway.
*/
static bool zstd_seq_build(zstd_fse address_to table, const bipolar address_to norm,
                           positive max_sym, p8 log, p8 kind)
{
        positive const size = (positive)1 << log;
        positive const mask = size - 1;
        positive const step = (size >> 1) + (size >> 3) + 3;
        /*      Both tables are indexed by lib.c's zstd_fse_cells with a
                whole byte out of symbol, and next is written through as
                well, so they are the width of what can index them rather
                than of what this builds -- sixty four entries and one
                spread cell that was never written would have been a store
                into this frame. Cleared for the same reason, and symbol
                poisoned as zstd_fse_build already poisons its own: a cell
                the spread missed then names a symbol whose row is zero
                instead of naming whatever the stack held. */
        p16 next[256];
        p64 template[256];
        p8 symbol[ZSTD_FSE_MAX];
        p8 spread[ZSTD_FSE_MAX + 8];
        positive total = 0;
        bool below_one = false;

        return_if(log < 5 || log > 9 || max_sym > 52, zstd_fail("zstd FSE table log"));
        memory_fill(next, 0, sizeof(next));
        memory_fill(template, 0, sizeof(template));
        memory_fill(symbol, 0xff, size);
        for (positive s = 0; s <= max_sym; s++)
        {
                next[s] = (p16)(norm[s] < 0 ? 1 : norm[s]);
                below_one |= norm[s] < 0;
                total += next[s];
        }
        return_if(total != size, zstd_fail("zstd FSE counts do not sum"));
        if (!below_one)
        {
                p64 value = 0;
                positive at = 0;
                positive position = 0;

                for (positive s = 0; s <= max_sym; s++, value += 0x0101010101010101ull)
                {
                        memory_store_unaligned(p64, spread + at, value);
                        for (bipolar i = 8; i < norm[s]; i += 8)
                                memory_store_unaligned(p64, spread + at + (positive)i, value);
                        at += (positive)norm[s];
                }
                for (positive u = 0; u < size; u += 2)
                {
                        symbol[position] = spread[u];
                        symbol[(position + step) & mask] = spread[u + 1];
                        position = (position + 2 * step) & mask;
                }
        }
        else if (!zstd_fse_spread(symbol, norm, max_sym, log))
                return zstd_fail("zstd FSE table did not fill");
        //      Each symbol's baseline and extra bits in a cell template, and the
        //      cells from the spread in lib.c.
        for (positive s = 0; s <= max_sym; s++)
        {
                p32 const base = kind == 1 ? (s < 2 ? (p32)s : ((p32)1 << s) - 3)
                               : kind == 0 ? zstd_ll_base[s] : zstd_ml_base[s];
                p8 const extra = kind == 1 ? (p8)s : kind == 0 ? zstd_ll_extra[s] : zstd_ml_extra[s];

                template[s] = (p64)base << 32 | (p64)extra << 16;
        }
        zstd_fse_cells(table->cell, symbol, size, log, next, template);
        table->log = log;
        table->rle = 0;
        table->valid = true;
        return true;
}

static bool zstd_seq_fuse(zstd_fse address_to table, p8 kind)
{
        const p8 address_to extra;
        const p32 address_to base;
        positive max;
        positive n;
        positive i;

        if (kind == 0)
        {
                extra = zstd_ll_extra;
                base = zstd_ll_base;
                max = 35;
        }
        else if (kind == 1)
        {
                extra = null;
                base = null;
                max = 31;
        }
        else
        {
                extra = zstd_ml_extra;
                base = zstd_ml_base;
                max = 52;
        }

        n = table->log ? (positive)1 << table->log : 1;
        for (i = 0; i < n; i++)
        {
                p8 sym = table->log ? (p8)table->cell[i].base : table->rle;

                return_if(sym > max, zstd_fail("zstd sequence symbol too large"));
                if (kind == 1)
                {
                        table->cell[i].extra = sym;
                        table->cell[i].base =
                            (p32)(sym < 2 ? (positive)sym : ((positive)1 << sym) - 3);
                }
                else
                {
                        table->cell[i].extra = extra[sym];
                        table->cell[i].base = base[sym];
                }
                if (!table->log)
                {
                        table->cell[i].next = 0;
                        table->cell[i].bits = 0;
                }
        }

        return true;
}

static fn zstd_fse_rle(zstd_fse address_to table, p8 symbol)
{
        table->log = 0;
        table->rle = symbol;
        table->valid = true;
}

static bool zstd_fse_read(p8 address_to src, positive src_len,
                          positive address_to used, bipolar address_to norm,
                          positive max_sym, p8 address_to log, p8 max_log)
{
        p8 pad[520];
        p8 address_to ip;
        p8 address_to iend;
        p32 bit_stream;
        positive bit_count;
        p8 table_log;
        positive remaining;
        positive threshold;
        p8 nb_bits;
        positive charnum = 0;
        bool previous0 = false;

        return_if(!src_len, zstd_fail("zstd truncated FSE header"));
        if (src_len > 512)
                src_len = 512;

        memory_fill(pad, 0, sizeof(pad));
        memory_copy(pad, src, src_len);
        memory_fill(norm, 0, (max_sym + 1) * sizeof(norm[0]));

        ip = pad;
        iend = pad + src_len + 8;
        bit_stream = memory_load_unaligned(p32, ip);
        bit_count = 0;

        table_log = (p8)((bit_stream & 15) + 5);
        return_if(table_log > max_log, zstd_fail("zstd FSE accuracy log too large"));
        bit_stream >>= 4;
        bit_count = 4;
        remaining = ((positive)1 << table_log) + 1;
        threshold = (positive)1 << table_log;
        nb_bits = table_log + 1;

        for (;;)
        {
                if (previous0)
                {
                        while ((bit_stream & 0xffffu) == 0xffffu)
                        {
                                charnum += 24;
                                return_if(charnum > max_sym + 1, zstd_fail("zstd FSE zero run"));
                                bit_stream >>= 16;
                                bit_count += 16;
                                if (ip + 4 <= iend)
                                {
                                        ip += bit_count >> 3;
                                        bit_count &= 7;
                                        bit_stream = memory_load_unaligned(p32, ip) >> bit_count;
                                }
                        }
                        while ((bit_stream & 3) == 3)
                        {
                                charnum += 3;
                                return_if(charnum > max_sym + 1, zstd_fail("zstd FSE zero run"));
                                bit_stream >>= 2;
                                bit_count += 2;
                        }
                        charnum += bit_stream & 3;
                        bit_count += 2;
                        bit_stream >>= 2;
                        return_if(charnum > max_sym + 1, zstd_fail("zstd FSE zero run"));
                        previous0 = false;
                        if (ip + 4 <= iend)
                        {
                                ip += bit_count >> 3;
                                bit_count &= 7;
                                bit_stream = memory_load_unaligned(p32, ip) >> bit_count;
                        }
                        continue;
                }

                {
                        positive max = (2 * threshold - 1) - remaining;
                        positive count;
                        p32 low = threshold - 1;
                        p32 mask = (threshold << 1) - 1;

                        if ((bit_stream & low) < max)
                        {
                                count = bit_stream & low;
                                bit_count += nb_bits - 1;
                                bit_stream >>= (nb_bits - 1);
                        }
                        else
                        {
                                count = bit_stream & mask;
                                if (count >= threshold)
                                        count -= max;
                                bit_count += nb_bits;
                                bit_stream >>= nb_bits;
                        }

                        {
                                bipolar prob = (bipolar)count - 1;

                                remaining -= prob < 0 ? 1 : (positive)prob;
                                if (charnum > max_sym)
                                        return zstd_fail("zstd FSE too many symbols");
                                norm[charnum++] = prob;
                                previous0 = !prob;
                        }
                        if (remaining <= 1)
                                break;
                        while (remaining < threshold)
                        {
                                nb_bits--;
                                threshold >>= 1;
                        }
                        if (ip + 4 <= iend)
                        {
                                ip += bit_count >> 3;
                                bit_count &= 7;
                                bit_stream = memory_load_unaligned(p32, ip) >> bit_count;
                        }
                }
        }

        return_if(remaining != 1, zstd_fail("zstd FSE counts do not sum"));

        ip += (bit_count + 7) >> 3;
        address_to used = (positive)(ip - pad);
        return_if(address_to used > src_len, zstd_fail("zstd FSE header over-read"));
        address_to log = table_log;
        return true;
}

static fn zstd_fse_defaults(void)
{
        if (zstd_fse_ready)
                return;
        zstd_fse_build(address_of zstd_ll_def, zstd_ll_default, 35, 6);
        zstd_fse_build(address_of zstd_ml_def, zstd_ml_default, 52, 6);
        zstd_fse_build(address_of zstd_of_def, zstd_of_default, 28, 5);
        zstd_seq_fuse(address_of zstd_ll_def, 0);
        zstd_seq_fuse(address_of zstd_ml_def, 2);
        zstd_seq_fuse(address_of zstd_of_def, 1);
        zstd_fse_ready = true;
}

static bool zstd_fse_unpack(zstd_fse address_to table, p8 address_to into,
                            positive max_out, positive address_to produced,
                            p8 address_to src, positive size)
{
        zstd_bits bits;
        p16 state1;
        p16 state2;
        positive n = 0;

        return_if(!size, zstd_fail("zstd empty bitstream"));
        if (zstd_bits_open(address_of bits, src, size))
                return zstd_fail("zstd bitstream missing the end mark");
        state1 = (p16)zstd_bits_get(address_of bits, table->log);
        state2 = (p16)zstd_bits_get(address_of bits, table->log);
        return_if(zstd_bits_reload(address_of bits), zstd_fail("zstd Huffman FSE overflow"));

        for (;;)
        {
                return_if(n + 2 > max_out, zstd_fail("zstd Huffman too many weights"));
                into[n++] = zstd_fse_symbol(table, address_of state1,
                                            address_of bits);
                if (zstd_bits_reload(address_of bits))
                {
                        into[n++] = zstd_fse_symbol(table, address_of state2,
                                                    address_of bits);
                        break;
                }
                into[n++] = zstd_fse_symbol(table, address_of state2,
                                            address_of bits);
                if (zstd_bits_reload(address_of bits))
                {
                        into[n++] = zstd_fse_symbol(table, address_of state1,
                                                    address_of bits);
                        break;
                }
        }

        address_to produced = n;
        return true;
}

static bool zstd_huff_from_weights(zstd_huff address_to huff, p8 address_to weight,
                                   positive provided)
{
        p32 rank[13];
        p32 first_cell[13];
        positive sum = 0;
        positive s;
        p8 max_bits;
        p32 rest;
        p8 last_weight;
        positive start;
        p8 w;

        memory_fill(rank, 0, sizeof(rank));

        for (s = 0; s < provided; s++)
        {
                return_if(weight[s] > 11, zstd_fail("zstd Huffman weight too large"));
                if (weight[s])
                {
                        sum += (positive)1 << (weight[s] - 1);
                        rank[weight[s]]++;
                }
        }

        return_if(!sum, zstd_fail("zstd Huffman weights are empty"));

        max_bits = (p8)(zstd_highbit32(sum) + 1);
        rest = ((p32)1 << max_bits) - (p32)sum;
        if (!rest || (rest & (rest - 1)))
                return zstd_fail("zstd Huffman weights are not a power of two");
        last_weight = (p8)(zstd_highbit32(rest) + 1);
        return_if(last_weight > 11 || provided >= 256, zstd_fail("zstd Huffman last weight"));
        weight[provided] = last_weight;
        rank[last_weight]++;
        return_if(max_bits > 11, zstd_fail("zstd Huffman deeper than 11"));
        return_if(rank[1] < 2 || (rank[1] & 1), zstd_fail("zstd Huffman rank-1 weights"));

        /* Built at eleven bits whatever the depth: a depth-L cell repeats
           1 << (11 - L) times, so every table takes the four-stream
           walker, which indexes by the top eleven bits.  Cells run by
           weight, then by symbol; the ranks say where each weight's cells
           begin, and one pass over the symbols fills them. */
        huff->max_bits = 11;
        huff->real_bits = max_bits;
        huff->weights = provided + 1;
        memory_copy_apart(huff->weight, weight, provided + 1);
        start = 0;
        for (w = 1; w <= 12; w++)
        {
                first_cell[w] = (p32)start;
                if (w <= max_bits)
                        start += (positive)rank[w] << (w - 1 + 11 - max_bits);
        }
        return_if(start != ((positive)1 << 11), zstd_fail("zstd Huffman table did not fill"));
        //      A weight never passes max_bits: 2^(w-1) is within the sum, and
        //      the last weight's power of two is below 2^max_bits.
        zstd_huffman_cells(huff->cell, weight, provided + 1, max_bits, first_cell);
        huff->valid = true;
        return true;
}

static bool zstd_huff_read(p8 address_to src, positive src_len,
                           positive address_to used, zstd_huff address_to huff)
{
        p8 header;
        p8 weight[256];
        positive provided;

        return_if(!src_len, zstd_fail("zstd truncated Huffman header"));

        header = src[0];
        memory_fill(weight, 0, sizeof(weight));

        if (header >= 128)
        {
                positive symbols = header - 127;
                positive bytes = (symbols + 1) / 2;
                positive i;

                return_if(1 + bytes > src_len, zstd_fail("zstd truncated Huffman weights"));
                for (i = 0; i < symbols; i += 2)
                {
                        weight[i] = src[1 + i / 2] >> 4;
                        if (i + 1 < symbols)
                                weight[i + 1] = src[1 + i / 2] & 15;
                }
                provided = symbols;
                address_to used = 1 + bytes;
        }
        else
        {
                bipolar norm[256];
                zstd_fse table;
                positive ncount;
                p8 log;
                positive unpacked;

                return_if(!header, zstd_fail("zstd empty Huffman FSE header"));
                return_if(1 + header > src_len, zstd_fail("zstd truncated Huffman FSE header"));
                if (!zstd_fse_read(src + 1, header, address_of ncount, norm, 255,
                                   address_of log, 6))
                        return false;
                if (!zstd_fse_build(address_of table, norm, 255, log))
                        return false;
                return_if(ncount >= header, zstd_fail("zstd Huffman FSE stream empty"));
                if (!zstd_fse_unpack(address_of table, weight, 255,
                                     address_of unpacked, src + 1 + ncount,
                                     header - ncount))
                        return false;
                provided = unpacked;
                address_to used = 1 + header;
        }

        return_if(!provided, zstd_fail("zstd Huffman no weights"));
        return zstd_huff_from_weights(huff, weight, provided);
}

static bool zstd_huff_stream(zstd_huff address_to huff, p8 address_to into,
                             positive need, p8 address_to src, positive size)
{
        if (!need)
                return true;
        if (zstd_huffman_stream(into, need, src, size, huff->cell, huff->max_bits))
                return zstd_fail("zstd Huffman over-read");
        return true;
}

static bool zstd_huff_four(zstd_huff address_to huff, p8 address_to into,
                           positive need, p8 address_to src, positive size)
{
        if (zstd_huffman_4x(into, need, src, size, huff->cell, huff->max_bits))
                return zstd_fail("zstd Huffman over-read");
        return true;
}

static bool zstd_flush(void)
{
        if (!zstd_out_fill || zstd_out_fd < 0)
                return true;
        if (system_write_all((positive)zstd_out_fd, zstd_out_buf,
                             zstd_out_fill) != (bipolar)zstd_out_fill)
                return zstd_fail("zstd: write failed");
        zstd_out_fill = zstd_out_taken = 0;
        return true;
}

static bool zstd_emit(p8 address_to p, positive n)
{
        if (!n)
                return true;

        if (zstd_output.bytes)
        {
                if (!byte_store_append_exact(address_of zstd_output, p, n))
                        return zstd_fail("zstd output larger than the destination");
                if (zstd_hashing)
                        hash_xxh64_add(address_of zstd_hash, p, n);
                zstd_decoded += n;
                return true;
        }

        if (zstd_out_fd < 0 && !zstd_pull)
        {
                if (zstd_hashing)
                        hash_xxh64_add(address_of zstd_hash, p, n);
                zstd_decoded += n;
                return true;
        }

        /* A pull reader takes the span where it lies: the window keeps it
           until the reader has drained it, since the next block is only
           decoded after that. */
        if (zstd_pull)
        {
                if (zstd_hashing)
                        hash_xxh64_add(address_of zstd_hash, p, n);
                zstd_decoded += n;
                zstd_rest = p;
                zstd_rest_n = n;
                zstd_paused = true;
                return true;
        }

        /* A block's worth goes to the descriptor from the window itself. */
        if (n >= ZSTD_OUT)
        {
                if (!zstd_flush())
                        return false;
                if (zstd_hashing)
                        hash_xxh64_add(address_of zstd_hash, p, n);
                zstd_decoded += n;
                if (system_write_all((positive)zstd_out_fd, p, n) !=
                    (bipolar)n)
                        return zstd_fail("zstd: write failed");
                zstd_rest = null;
                zstd_rest_n = 0;
                return true;
        }

        while (n)
        {
                positive room = ZSTD_OUT - zstd_out_fill;
                positive chunk;

                if (zstd_pull && !room)
                {
                        zstd_rest = p;
                        zstd_rest_n = n;
                        zstd_paused = true;
                        return true;
                }

                chunk = n < room ? n : room;
                if (zstd_hashing)
                        hash_xxh64_add(address_of zstd_hash, p, chunk);
                zstd_decoded += chunk;
                memory_copy(zstd_out_buf + zstd_out_fill, p, chunk);
                zstd_out_fill += chunk;
                p += chunk;
                n -= chunk;
                if (!zstd_pull && zstd_out_fill == ZSTD_OUT && !zstd_flush())
                        return false;
        }

        zstd_rest = null;
        zstd_rest_n = 0;
        return true;
}

static bool zstd_window_room(positive need)
{
        if (zstd_pos + need <= zstd_window_cap)
                return true;

        if (zstd_pos > zstd_keep)
        {
                //      A ring slides by moving where the window starts: the
                //      bytes it keeps are already the ones behind the new
                //      start, and a start past the first copy is the same
                //      memory one ring earlier.
                if (zstd_ring)
                {
                        zstd_window += zstd_pos - zstd_keep;
                        if (zstd_window >= zstd_ring + zstd_ring_size)
                                zstd_window -= zstd_ring_size;
                }
                else
                        memory_copy(zstd_window,
                                    zstd_window + zstd_pos - zstd_keep,
                                    zstd_keep);
                zstd_pos = zstd_keep;
        }

        return zstd_pos + need <= zstd_window_cap
                   ? true
                   : zstd_fail("zstd window overflow");
}

static bool zstd_put(p8 address_to p, positive n)
{
        if (!n)
                return true;
        if (!zstd_window_room(n))
                return false;
        memory_copy(zstd_window + zstd_pos, p, n);
        if (!zstd_hold_emit && !zstd_emit(zstd_window + zstd_pos, n))
                return false;
        zstd_pos += n;
        return true;
}

static bool zstd_put_fill(p8 value, positive n)
{
        if (!n)
                return true;
        if (!zstd_window_room(n))
                return false;
        memory_fill(zstd_window + zstd_pos, value, n);
        if (!zstd_hold_emit && !zstd_emit(zstd_window + zstd_pos, n))
                return false;
        zstd_pos += n;
        return true;
}


static bool zstd_seq_table(zstd_fse address_to address_to out, p8 mode, p8 address_to src,
                           positive src_len, positive address_to used, p8 kind,
                           positive max_sym, p8 max_log)
{
        zstd_fse address_to const prev = zstd_seq_prev[kind];
        zstd_fse address_to const table = prev == address_of zstd_seq_tables[kind][0]
                                              ? address_of zstd_seq_tables[kind][1]
                                              : address_of zstd_seq_tables[kind][0];

        address_to used = 0;

        if (mode == 0)
        {
                zstd_fse_defaults();
                address_to out = kind == 0 ? address_of zstd_ll_def
                               : kind == 1 ? address_of zstd_of_def : address_of zstd_ml_def;
                return true;
        }
        address_to out = table;
        if (mode == 1)
        {
                return_if(!src_len, zstd_fail("zstd truncated RLE table"));
                zstd_fse_rle(table, src[0]);
                address_to used = 1;
                return zstd_seq_fuse(table, kind);
        }
        if (mode == 2)
        {
                bipolar norm[256];
                p8 log;
                positive ncount;

                if (!zstd_fse_read(src, src_len, address_of ncount, norm, max_sym,
                                   address_of log, max_log))
                        return false;
                if (!zstd_seq_build(table, norm, max_sym, log, kind))
                        return false;
                address_to used = ncount;
                return true;
        }
        if (mode == 3)
        {
                return_if(!prev, zstd_fail("zstd repeat FSE with no previous table"));
                address_to out = prev;
                return true;
        }

        return zstd_fail("zstd unknown FSE mode");
}

static bool zstd_literals(p8 address_to src, positive src_len,
                          positive address_to used, p8 address_to lit,
                          positive address_to lit_len)
{
        p8 type;
        p8 format;
        positive regen = 0;
        positive compressed = 0;
        positive header = 0;
        p8 address_to body;

        return_if(!src_len, zstd_fail("zstd truncated literals"));

        type = src[0] & 3;
        format = (src[0] >> 2) & 3;

        if (type <= 1)
        {
                if (format == 0 || format == 2)
                {
                        header = 1;
                        regen = src[0] >> 3;
                }
                else if (format == 1)
                {
                        return_if(src_len < 2, zstd_fail("zstd truncated literals size"));
                        header = 2;
                        regen = memory_load_unaligned(p16, src) >> 4;
                }
                else
                {
                        return_if(src_len < 3, zstd_fail("zstd truncated literals size"));
                        header = 3;
                        regen = zstd_get24(src) >> 4;
                }
                return_if(regen > zstd_block_limit, zstd_fail("zstd literals larger than a block"));
                body = src + header;
                if (type == 0)
                {
                        if (header + regen > src_len)
                                return zstd_fail("zstd truncated raw literals");
                        memory_copy(lit, body, regen);
                        address_to used = header + regen;
                }
                else
                {
                        return_if(header + 1 > src_len, zstd_fail("zstd truncated RLE literals"));
                        memory_fill(lit, body[0], regen);
                        address_to used = header + 1;
                }
                address_to lit_len = regen;
                return true;
        }

        {
                p32 pack;

                if (format <= 1)
                {
                        return_if(src_len < 3, zstd_fail("zstd truncated literals header"));
                        pack = zstd_get24(src);
                        header = 3;
                        regen = (pack >> 4) & 0x3ff;
                        compressed = (pack >> 14) & 0x3ff;
                }
                else if (format == 2)
                {
                        return_if(src_len < 4, zstd_fail("zstd truncated literals header"));
                        pack = memory_load_unaligned(p32, src);
                        header = 4;
                        regen = (pack >> 4) & 0x3fff;
                        compressed = pack >> 18;
                }
                else
                {
                        return_if(src_len < 5, zstd_fail("zstd truncated literals header"));
                        pack = memory_load_unaligned(p32, src);
                        header = 5;
                        regen = (pack >> 4) & 0x3ffff;
                        compressed = (pack >> 22) + ((positive)src[4] << 10);
                }
        }

        if (regen > zstd_block_limit || header + compressed > src_len)
                return zstd_fail("zstd compressed literals size");

        body = src + header;
        if (type == 2)
        {
                positive tree;

                if (!zstd_huff_read(body, compressed, address_of tree,
                                    address_of zstd_lit_huff))
                        return false;
                return_if(tree > compressed, zstd_fail("zstd Huffman tree larger than literals"));
                body += tree;
                compressed -= tree;
        }
        else if (!zstd_lit_huff.valid)
                return zstd_fail("zstd treeless literals with no Huffman table");

        if (!format)
        {
                if (!zstd_huff_stream(address_of zstd_lit_huff, lit, regen, body,
                                      compressed))
                        return false;
        }
        else
        {
                return_if(regen < 6, zstd_fail("zstd 4-stream literals too small"));
                if (!zstd_huff_four(address_of zstd_lit_huff, lit, regen, body,
                                    compressed))
                        return false;
        }

        address_to used = header + (positive)(body - (src + header)) + compressed;
        address_to lit_len = regen;
        return true;
}


static bool zstd_sequences(p8 address_to src, positive src_len, p8 address_to lit,
                           positive lit_len)
{
        p8 address_to p = src;
        p8 address_to stop = src + src_len;
        positive nseq;
        p8 modes;
        positive used;
        zstd_seq_job job;

        return_if(p >= stop, zstd_fail("zstd truncated sequences"));

        if (p[0] < 128)
        {
                nseq = p[0];
                p += 1;
        }
        else if (p[0] < 255)
        {
                return_if(p + 2 > stop, zstd_fail("zstd truncated sequence count"));
                nseq = ((positive)(p[0] - 128) << 8) + p[1];
                p += 2;
        }
        else
        {
                return_if(p + 3 > stop, zstd_fail("zstd truncated sequence count"));
                nseq = 0x7f00u + memory_load_unaligned(p16, p + 1);
                p += 3;
        }

        if (!nseq)
        {
                return_if(p != stop, zstd_fail("zstd extra bytes after no sequences"));
                return_if(lit_len > zstd_block_limit, zstd_fail("zstd block output is too large"));
                return zstd_put(lit, lit_len);
        }

        return_if(p >= stop, zstd_fail("zstd truncated sequence tables"));
        modes = p[0];
        p += 1;
        return_if(modes & 3, zstd_fail("zstd reserved sequence bits"));

        if (!zstd_seq_table(address_of job.ll, (p8)(modes >> 6), p, (positive)(stop - p),
                            address_of used, 0, 35, 9))
                return false;
        p += used;
        if (!zstd_seq_table(address_of job.of, (p8)((modes >> 4) & 3), p, (positive)(stop - p),
                            address_of used, 1, 31, 8))
                return false;
        p += used;
        if (!zstd_seq_table(address_of job.ml, (p8)((modes >> 2) & 3), p, (positive)(stop - p),
                            address_of used, 2, 52, 9))
                return false;
        p += used;

        zstd_seq_prev[0] = job.ll;
        zstd_seq_prev[1] = job.of;
        zstd_seq_prev[2] = job.ml;

        return_if(p >= stop, zstd_fail("zstd truncated sequence bitstream"));

        job.window = zstd_window;
        job.pos = zstd_pos;
        job.window_size = zstd_window_size;
        job.lits = lit;
        job.lit_len = lit_len;
        job.seq = p;
        job.seq_len = (positive)(stop - p);
        job.rep = zstd_rep;
        job.nseq = nseq;
        job.output_end = zstd_window + zstd_pos + zstd_block_limit;
        if (zstd_sequences_run(address_of job))
                return zstd_fail("zstd sequence input or output overrun");
        zstd_pos = job.pos;
        return true;
}

/* One frame's window, kept across the frames of a stream when the one in
   hand is already big enough. Concatenated frames are ordinary zstd -- the
   parallel encoder writes them -- and mapping, faulting in and unmapping the
   same megabytes once per frame was most of what decoding them cost. Nothing
   of the old frame survives that the new one can read: zstd_pos returns to
   zero and zstd_sequences_run refuses any offset past what this frame has
   written, so a larger mapping only means the slide in zstd_room happens
   later. */
#define ZSTD_MAP_FIXED 0x10
#define ZSTD_MEMFD_CLOEXEC 1u
#define ZSTD_RING_ALIGN ((positive)2 << 20)
/* Below this the plain mapping's doubling is a few megabytes, and a ring
   would round up to whole huge pages. */
#define ZSTD_RING_LEAST ((positive)8 << 20)

/* size bytes of one memfd mapped twice back to back, so that any span of up
   to size bytes starting in the first copy reads and writes as one run. The
   window slides through it by moving its start, where a plain mapping had
   to be twice the window and copy the whole window down each time it
   filled: a 128 MiB window touched 256 MiB and copied 128 MiB per 128 MiB
   decoded. Null when the kernel refuses any step, and the caller falls back
   to the plain mapping. */
static p8 address_to zstd_ring_map(positive size)
{
        bipolar handle;
        bipolar at;
        p8 address_to ring = null;

        handle = system_call_2(syscall(memfd_create),
                               (positive)(address_any) "zstd window",
                               ZSTD_MEMFD_CLOEXEC);
        if (handle < 0)
                return null;
        if (system_call_2(syscall(ftruncate), (positive)handle, size) < 0)
                goto done;
        at = system_call_6(syscall(mmap), 0, 2 * size + ZSTD_RING_ALIGN,
                           FILE_PROTECT_NONE,
                           FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS,
                           (positive)-1, 0);
        if (at < 0 && at > -4096)
                goto done;
        {
                bipolar aligned = (at + (bipolar)ZSTD_RING_ALIGN - 1) &
                                  ~(bipolar)(ZSTD_RING_ALIGN - 1);

                if (aligned > at)
                        system_call_2(syscall(munmap), (positive)at,
                                      (positive)(aligned - at));
                if (aligned + (bipolar)(2 * size) < at + (bipolar)(2 * size + ZSTD_RING_ALIGN))
                        system_call_2(syscall(munmap),
                                      (positive)(aligned + (bipolar)(2 * size)),
                                      (positive)(at + (bipolar)ZSTD_RING_ALIGN - aligned));
                at = aligned;
        }
        if (system_call_6(syscall(mmap), (positive)at, size,
                          FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                          FILE_MAP_SHARED | ZSTD_MAP_FIXED,
                          (positive)handle, 0) != at ||
            system_call_6(syscall(mmap), (positive)at + size, size,
                          FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                          FILE_MAP_SHARED | ZSTD_MAP_FIXED,
                          (positive)handle, 0) != at + (bipolar)size)
        {
                system_call_2(syscall(munmap), (positive)at, 2 * size);
                goto done;
        }
        system_call_3(syscall(madvise), (positive)at, 2 * size, MEMORY_ADVISE_HUGE_PAGE);
        ring = (p8 address_to)at;
done:
        system_call_1(syscall(close), (positive)handle);
        return ring;
}

/* Whether a memfd can be given huge pages, from the kernel's own switch.
   The ring only pays with them: on ordinary pages each of its two mappings
   faults every page on its own, 66 thousand faults for a 128 MiB window,
   and the ring decoded Arch's bootstrap 4 percent slower than the plain
   mapping; with them it faulted 469 times to the plain mapping's thousand
   on anonymous huge pages, and decoded 9 percent faster in half the
   memory. Asked once a process. */
static bool zstd_ring_huge(void)
{
        static p8 known;
        p8 text[64];
        bipolar handle;
        bipolar got;

        if (known)
                return known == 1;
        known = 2;
        handle = system_open_at(AT_FDCWD,
                                "/sys/kernel/mm/transparent_hugepage/shmem_enabled",
                                FILE_READ);
        if (handle < 0)
                return false;
        got = system_call_3(syscall(read), (positive)handle, (positive)text,
                            sizeof text - 1);
        system_close(handle);
        if (got <= 0)
                return false;
        text[got] = 0;
        //      The mode in force is the bracketed word: never and deny keep
        //      a memfd on small pages whatever it asks.
        for (bipolar at = 0; at < got; at++)
                if (text[at] == '[')
                {
                        if (!string_compare_max((string_address)text + at,
                                                "[never]", 7) ||
                            !string_compare_max((string_address)text + at,
                                                "[deny]", 6))
                                return false;
                        known = 1;
                        return true;
                }
        return false;
}

static fn zstd_window_free(void)
{
        if (zstd_ring)
                system_call_2(syscall(munmap), (positive)zstd_ring,
                              2 * zstd_ring_size);
        else if (zstd_window)
                memory_free(zstd_window, zstd_window_cap);
        zstd_ring = null;
        zstd_ring_size = 0;
        zstd_window = null;
        zstd_window_cap = 0;
}

static bool zstd_window_open(positive window)
{
        positive cap;
        positive ring;

        zstd_window_size = window;
        zstd_keep = window;
        zstd_pos = 0;
        if (!window)
                return true;

        ring = (window + ZSTD_BLOCK_MAX + 64 + ZSTD_RING_ALIGN - 1) &
               ~(ZSTD_RING_ALIGN - 1);
        if (zstd_ring && zstd_ring_size >= ring)
        {
                zstd_window = zstd_ring;
                return true;
        }
        cap = window * 2 + ZSTD_BLOCK_MAX + 64;
        if (!zstd_ring && zstd_window && zstd_window_cap >= cap)
                return true;
        zstd_window_free();

        zstd_ring = window >= ZSTD_RING_LEAST && zstd_ring_huge()
                            ? zstd_ring_map(ring)
                            : null;
        if (zstd_ring)
        {
                zstd_ring_size = ring;
                zstd_window = zstd_ring;
                zstd_window_cap = ring;
                return true;
        }

        zstd_window = (p8 address_to)memory_checked(cap);
        return_if(!zstd_window, zstd_fail("zstd cannot map the window"));
        zstd_window_cap = cap;
        return true;
}

static fn zstd_window_close(void)
{
        zstd_window_free();
}

/*
        A dictionary, RFC 8878's: content alone (an ID of none, the default
        history and no tables), or the magic 0xEC30A437, a four-byte ID, a
        Huffman table, the offset, match length and literal length tables,
        three repeat offsets and then the content.  It is read once; every
        frame decoded after starts with its content as history, its tables
        as the ones a first block may repeat, and its repeat offsets.
*/
#define ZSTD_DICT_MAGIC 0xEC30A437u
static p8 address_to zstd_dict_content;
static positive zstd_dict_size;
static p32 zstd_dict_id;
static bool zstd_dict_on;
static bool zstd_dict_entropy;
static bool zstd_cli_no_dict_id;
static zstd_huff zstd_dict_huff;
static zstd_fse zstd_dict_fse[3];
static bipolar zstd_dict_norm[3][256];
static p8 zstd_dict_log[3];
static p32 zstd_dict_rep[3] = {1, 4, 8};
static const p8 zstd_dict_symbols[3] = {35, 31, 52};

static bool zstd_dictionary_load(p8 address_to bytes, positive length)
{
        positive at = 8;
        positive used;

        zstd_dict_on = false;
        zstd_dict_entropy = false;
        zstd_dict_id = 0;
        zstd_dict_rep[0] = 1;
        zstd_dict_rep[1] = 4;
        zstd_dict_rep[2] = 8;
        return_if(length > ZSTD_WINDOW_MAX, zstd_fail("zstd dictionary larger than 128 MiB"));
        if (length < 8 || memory_load_unaligned(p32, bytes) != ZSTD_DICT_MAGIC)
        {
                zstd_dict_content = bytes;
                zstd_dict_size = length;
                zstd_dict_on = true;
                return true;
        }
        zstd_dict_id = memory_load_unaligned(p32, bytes + 4);
        if (!zstd_huff_read(bytes + at, length - at, address_of used, address_of zstd_dict_huff))
                return zstd_fail("zstd dictionary is corrupt");
        at += used;
        {
                static const p8 order[3] = {1, 2, 0};
                static const p8 symbols[3] = {31, 52, 35};
                static const p8 logs[3] = {8, 9, 9};

                for (positive k = 0; k < 3; k++)
                {
                        p8 const kind = order[k];

                        memory_fill(zstd_dict_norm[kind], 0, sizeof(zstd_dict_norm[kind]));
                        if (at >= length ||
                            !zstd_fse_read(bytes + at, length - at, address_of used,
                                           zstd_dict_norm[kind], symbols[k],
                                           address_of zstd_dict_log[kind], logs[k]) ||
                            !zstd_seq_build(address_of zstd_dict_fse[kind], zstd_dict_norm[kind],
                                            symbols[k], zstd_dict_log[kind], kind))
                                return zstd_fail("zstd dictionary is corrupt");
                        at += used;
                }
        }
        return_if(at + 12 > length, zstd_fail("zstd dictionary is corrupt"));
        for (positive k = 0; k < 3; k++)
        {
                p32 const rep = memory_load_unaligned(p32, bytes + at + 4 * k);

                return_if(!rep || rep > length - at - 12, zstd_fail("zstd dictionary is corrupt"));
                zstd_dict_rep[k] = rep;
        }
        at += 12;
        zstd_dict_content = bytes + at;
        zstd_dict_size = length - at;
        zstd_dict_entropy = true;
        zstd_dict_on = true;
        return true;
}

static bool zstd_frame(void)
{
        p8 desc[1];
        p8 scratch[8];
        p32 dict = 0;
        p8 fcs_flag;
        p8 dict_flag;
        bool single;
        bool checksum;
        positive window = 0;
        p64 frame_start;

        if (zstd_need_trailer)
                goto zstd_frame_trailer;
        if (zstd_frame_open)
                goto zstd_frame_blocks;

        if (!zstd_in_take(scratch, 4))
                return false;
        return_if(memory_load_unaligned(p32, scratch) != ZSTD_MAGIC, zstd_fail("zstd bad magic"));
        if (!zstd_in_take(desc, 1))
                return false;

        fcs_flag = (p8)(desc[0] >> 6);
        single = (desc[0] & 0x20) != 0;
        checksum = (desc[0] & 0x04) != 0;
        dict_flag = desc[0] & 3;
        return_if(desc[0] & 0x08, zstd_fail("zstd reserved frame bit"));

        if (!single)
        {
                p8 win[1];
                p8 mantissa;
                p8 exponent;
                positive base;

                if (!zstd_in_take(win, 1))
                        return false;
                exponent = (p8)(win[0] >> 3);
                mantissa = win[0] & 7;
                return_if(exponent > 17, zstd_fail("zstd window larger than 128 MiB"));
                base = (positive)1 << (10 + exponent);
                window = base + (base >> 3) * mantissa;
        }

        if (dict_flag == 1)
        {
                if (!zstd_in_take(scratch, 1))
                        return false;
                dict = scratch[0];
        }
        else if (dict_flag == 2)
        {
                if (!zstd_in_take(scratch, 2))
                        return false;
                dict = memory_load_unaligned(p16, scratch);
        }
        else if (dict_flag == 3)
        {
                if (!zstd_in_take(scratch, 4))
                        return false;
                dict = memory_load_unaligned(p32, scratch);
        }
        if (dict && (!zstd_dict_on || dict != zstd_dict_id))
                return zstd_fail("zstd Dictionary mismatch");

        zstd_have_fcs = false;
        zstd_fcs = 0;
        if (fcs_flag == 0)
        {
                if (single)
                {
                        if (!zstd_in_take(scratch, 1))
                                return false;
                        zstd_fcs = scratch[0];
                        zstd_have_fcs = true;
                }
        }
        else if (fcs_flag == 1)
        {
                if (!zstd_in_take(scratch, 2))
                        return false;
                zstd_fcs = (p64)memory_load_unaligned(p16, scratch) + 256;
                zstd_have_fcs = true;
        }
        else if (fcs_flag == 2)
        {
                if (!zstd_in_take(scratch, 4))
                        return false;
                zstd_fcs = memory_load_unaligned(p32, scratch);
                zstd_have_fcs = true;
        }
        else
        {
                if (!zstd_in_take(scratch, 8))
                        return false;
                zstd_fcs = memory_get64(scratch);
                zstd_have_fcs = true;
        }

        if (single)
        {
                return_if(!zstd_have_fcs, zstd_fail("zstd single-segment frame has no size"));
                return_if(zstd_fcs > ZSTD_WINDOW_MAX, zstd_fail("zstd frame larger than 128 MiB"));
                window = (positive)zstd_fcs;
        }

        return_if(window > ZSTD_WINDOW_MAX, zstd_fail("zstd window larger than 128 MiB"));

        // A single segment of no bytes has a window of none, and no block
        // may carry anything: this read it as the full 128 KiB and wrote the
        // block into the previous frame's window before the size said no.
        zstd_block_limit = window < ZSTD_BLOCK_MAX ? window : ZSTD_BLOCK_MAX;
        if (!zstd_window_open(window + (zstd_dict_on ? zstd_dict_size : 0)))
                return false;

        zstd_rep[0] = 1;
        zstd_rep[1] = 4;
        zstd_rep[2] = 8;
        zstd_seq_prev[0] = null;
        zstd_seq_prev[1] = null;
        zstd_seq_prev[2] = null;
        zstd_lit_huff.valid = false;
        if (zstd_dict_on)
        {
                /* The content is history the frame's matches may reach at
                   any distance while it is kept (a dictionary is not held
                   to the window), and its tables and repeat offsets are
                   the state before the first block. */
                memory_copy(zstd_window, zstd_dict_content, zstd_dict_size);
                zstd_pos = zstd_dict_size;
                zstd_window_size = 0;
                zstd_keep = window + zstd_dict_size;
                zstd_rep[0] = zstd_dict_rep[0];
                zstd_rep[1] = zstd_dict_rep[1];
                zstd_rep[2] = zstd_dict_rep[2];
                if (zstd_dict_entropy)
                {
                        memory_copy_apart(address_of zstd_lit_huff, address_of zstd_dict_huff,
                                          sizeof(zstd_huff));
                        for (positive k = 0; k < 3; k++)
                                zstd_seq_prev[k] = address_of zstd_dict_fse[k];
                }
        }
        frame_start = zstd_decoded;
        if (checksum)
        {
                zstd_hashing = true;
                hash_xxh64_begin(address_of zstd_hash, 0);
        }
        else
                zstd_hashing = false;

        zstd_frame_open = true;
        zstd_checksum_on = checksum;
        zstd_frame_begin = frame_start;

zstd_frame_blocks:
        checksum = zstd_checksum_on;
        frame_start = zstd_frame_begin;

        for (;;)
        {
                p8 header[3];
                p32 pack;
                bool last;
                p8 type;
                positive size;

                if (!zstd_in_take(header, 3))
                        return false;
                pack = zstd_get24(header);
                last = (pack & 1) != 0;
                type = (p8)((pack >> 1) & 3);
                size = pack >> 3;
                return_if(type == 3, zstd_fail("zstd reserved block type"));
                if (type == 0)
                {
                        return_if(size > zstd_block_limit, zstd_fail("zstd raw block too large"));
                        /* Consume a whole block before pausing. The window
                           owns the remainder until the pull reader drains it. */
                        if (!zstd_in_need(size) || !zstd_put(zstd_in_at(), size))
                                return false;
                        zstd_in_skip(size);
                        if (zstd_paused)
                        {
                                zstd_need_trailer = last;
                                return true;
                        }
                }
                else if (type == 1)
                {
                        p8 value[1];

                        return_if(size > zstd_block_limit, zstd_fail("zstd RLE block too large"));
                        if (!zstd_in_take(value, 1) || !zstd_put_fill(value[0], size))
                                return false;
                        if (zstd_paused)
                        {
                                zstd_need_trailer = last;
                                return true;
                        }
                }
                else
                {
                        positive lit_used;
                        positive lit_len;

                        if (size > ZSTD_BLOCK_MAX || !size)
                                return zstd_fail("zstd compressed block size");
                        if (!zstd_in_take(zstd_comp, size))
                                return false;
                        if (!zstd_literals(zstd_comp, size, address_of lit_used,
                                           zstd_lit_buf, address_of lit_len))
                                return false;
                        return_if(lit_used > size, zstd_fail("zstd literals overran the block"));
                        if (!zstd_window_room(zstd_block_limit))
                                return false;
                        {
                                positive at = zstd_pos;
                                bool ok;

                                zstd_hold_emit = true;
                                ok = zstd_sequences(zstd_comp + lit_used,
                                                    size - lit_used, zstd_lit_buf,
                                                    lit_len);
                                zstd_hold_emit = false;
                                if (!ok || !zstd_emit(zstd_window + at, zstd_pos - at))
                                        return false;
                                if (zstd_paused)
                                {
                                        zstd_need_trailer = last;
                                        return true;
                                }
                        }
                }

                if (last)
                        break;
        }

zstd_frame_trailer:
        checksum = zstd_checksum_on;
        frame_start = zstd_frame_begin;
        if (zstd_have_fcs && zstd_decoded - frame_start != zstd_fcs)
                return zstd_fail("zstd frame content size mismatch");

        if (checksum)
        {
                p32 got;
                p32 want;

                if (!zstd_in_take(scratch, 4))
                        return false;
                want = memory_load_unaligned(p32, scratch);
                got = (p32)hash_xxh64_finish(address_of zstd_hash);
                return_if(got != want, zstd_fail("zstd content checksum mismatch"));
        }

        zstd_hashing = false;
        zstd_frame_open = false;
        zstd_need_trailer = false;
        return true;
}

static bool zstd_skippable(void)
{
        p8 sizeb[4];
        p32 size;

        if (!zstd_in_take(sizeb, 4))
                return false;
        size = memory_load_unaligned(p32, sizeb);
        return zstd_in_skip_bytes(size);
}

/*      Whether this stream has shown a frame of either kind yet. The command
        refuses input that ends before it has, as zstd refuses it --
        "unexpected end of file" for an empty .zst, which decoded here to
        nothing and 0 -- where the memory API keeps answering an empty input
        with an empty output, which is what its callers ask of it. A stream
        of skippable frames alone is still a stream. */
static bool zstd_began;
static bool zstd_empty_refused;

static bool zstd_stream(void)
{
        bool any = zstd_live && zstd_decoded > 0;

        if (!zstd_live)
        {
                zstd_began = false;
                zstd_decoded = 0;
                zstd_output.used = 0;
                if (!zstd_pull)
                        zstd_out_fill = zstd_out_taken = 0;
                zstd_hold_emit = false;
                zstd_why = null;
                zstd_live = true;
        }

        for (;;)
        {
                p8 peek[4];
                p32 magic;

                if (zstd_paused)
                        return true;
                if (zstd_frame_open || zstd_need_trailer)
                {
                        if (!zstd_frame())
                                return false;
                        if (zstd_paused)
                                return true;
                        any = true;
                        continue;
                }

                if (zstd_src.at == zstd_src.have)
                {
                        zstd_why = null;
                        if (!zstd_in_need(1))
                        {
                                if (zstd_src.eof && zstd_src.at == zstd_src.have)
                                {
                                        if (!zstd_began && zstd_empty_refused)
                                                return zstd_fail("zstd unexpected end of file");
                                        zstd_why = null;
                                        break;
                                }
                                return false;
                        }
                }
                if (!zstd_in_need(4))
                {
                        if (zstd_src.eof && zstd_src.at == zstd_src.have)
                        {
                                if (!zstd_began && zstd_empty_refused)
                                        return zstd_fail("zstd unexpected end of file");
                                zstd_why = null;
                                break;
                        }
                        return false;
                }
                memory_copy(peek, zstd_in_at(), 4);
                magic = memory_load_unaligned(p32, peek);
                zstd_began = true;
                if (magic == ZSTD_MAGIC)
                {
                        if (!zstd_frame())
                                return false;
                        any = true;
                        continue;
                }
                if ((magic & ZSTD_SKIP_MASK) == ZSTD_SKIP_MAGIC)
                {
                        zstd_in_skip(4);
                        if (!zstd_skippable())
                                return false;
                        continue;
                }
                return_if(!any, zstd_fail("zstd bad magic"));
                return zstd_fail("zstd trailing garbage");
        }

        return true;
}

static bipolar zstd_inflate(p8 address_to src, positive src_len,
                            p8 address_to dst, positive dst_cap)
{
        bool ok;

        byte_input_open_memory(address_of zstd_src, src, src_len, zstd_in_buf, ZSTD_IN);
        zstd_output.bytes = dst;
        zstd_output.room = dst_cap;
        zstd_out_fd = -1;
        zstd_pull = false;
        zstd_live = false;
        zstd_frame_open = false;
        zstd_paused = false;
        zstd_need_trailer = false;
        ok = zstd_stream();
        if (ok)
                ok = zstd_flush();
        zstd_window_close();
        zstd_output.bytes = null;
        return ok ? (bipolar)zstd_output.used : -1;
}

static bool zstd_decode_begin(bipolar in)
{
        byte_input_open_fd(address_of zstd_src, in, zstd_in_buf, ZSTD_IN);
        zstd_output.bytes = null;
        zstd_out_fd = -1;
        zstd_out_fill = zstd_out_taken = 0;
        zstd_pull = true;
        zstd_paused = false;
        zstd_live = false;
        zstd_finished = false;
        zstd_frame_open = false;
        zstd_need_trailer = false;
        zstd_rest = null;
        zstd_rest_n = 0;
        zstd_why = null;
        return true;
}

static bool zstd_decode_begin_prefix(bipolar in, p8 address_to prefix, positive n)
{
        zstd_decode_begin(in);
        return_if(n > ZSTD_IN, zstd_fail("zstd prefix"));
        memory_copy(zstd_src.buf, prefix, n);
        zstd_src.have = n;
        zstd_src.at = 0;
        return true;
}

static bipolar zstd_decode_read(p8 address_to dst, positive n)
{
        positive copied = 0;

        while (copied < n)
        {
                positive take;

                if (zstd_out_fill)
                {
                        positive left = zstd_out_fill - zstd_out_taken;
                        take = left > n - copied ? n - copied : left;
                        memory_copy_apart(dst + copied, zstd_out_buf + zstd_out_taken, take);
                        zstd_out_taken += take;
                        if (zstd_out_taken == zstd_out_fill)
                                zstd_out_fill = zstd_out_taken = 0;
                        copied += take;
                        continue;
                }
                if (zstd_rest_n)
                {
                        take = zstd_rest_n > n - copied ? n - copied
                                                        : zstd_rest_n;
                        memory_copy_apart(dst + copied, zstd_rest, take);
                        zstd_rest += take;
                        zstd_rest_n -= take;
                        copied += take;
                        if (!zstd_rest_n)
                                zstd_paused = false;
                        continue;
                }
                if (zstd_finished)
                        break;
                zstd_paused = false;
                if (!zstd_stream())
                        return -1;
                if (!zstd_out_fill && !zstd_paused && !zstd_rest_n &&
                    zstd_src.eof && zstd_src.at == zstd_src.have)
                {
                        zstd_finished = true;
                        break;
                }
        }
        return (bipolar)copied;
}

static bool zstd_decode_end(void)
{
        zstd_pull = false;
        zstd_finished = true;
        zstd_window_close();
        return zstd_why == null;
}


static zstd_xxh zstd_enc_hash;

/*
        Encoder.  A level names a strategy and its sizes the way libzstd's
        table does: fast (one hash), dfast (a long and a short hash), and
        greedy, lazy and lazy2 over rows of tagged hash slots, and the
        binary-tree finder and optimal parser at the top levels.  A parser fills one block's
        sequences and literals, and the entropy stage writes the block, or a
        raw or RLE block when that is no larger.  Everything an encoder keeps
        between blocks is one zstd_encoder.
*/
#define ZSTD_FAST 1
#define ZSTD_DFAST 2
#define ZSTD_GREEDY 3
#define ZSTD_LAZY 4
#define ZSTD_LAZY2 5
#define ZSTD_BTLAZY2 6
#define ZSTD_BTOPT 7
#define ZSTD_BTULTRA 8
#define ZSTD_BTULTRA2 9
/* Every sequence takes at least three bytes of the block. */
#define ZSTD_ENC_SEQ_MAX (ZSTD_BLOCK_MAX / 3 + 8)

typedef struct
{
        p8 window_log;
        p8 chain_log;
        p8 hash_log;
        p8 search_log;
        p8 min_match;
        p8 strategy;
        p16 target_length;
} zstd_params;

/* libzstd 1.5's levels: window, chain, hash and search logs, minimum match,
   strategy and target length, in four tables by the size of the input --
   past 256 KiB (and for an input of no known size), to 256 KiB, to 128 KiB
   and to 16 KiB.  Row 0 is the base of --fast=N, whose N becomes the target
   length (the step). */
#define ZSTD_L(w, c, h, s, l, strat, t) {w, c, h, s, l, strat, t}
static const zstd_params zstd_level_table[4][23] = {
    {
        ZSTD_L(19, 12, 13, 1, 6, ZSTD_FAST, 1),      ZSTD_L(19, 13, 14, 1, 7, ZSTD_FAST, 0),
        ZSTD_L(20, 15, 16, 1, 6, ZSTD_FAST, 0),      ZSTD_L(21, 16, 17, 1, 5, ZSTD_DFAST, 0),
        ZSTD_L(21, 18, 18, 1, 5, ZSTD_DFAST, 0),     ZSTD_L(21, 18, 19, 3, 5, ZSTD_GREEDY, 2),
        ZSTD_L(21, 18, 19, 3, 5, ZSTD_LAZY, 4),      ZSTD_L(21, 19, 20, 4, 5, ZSTD_LAZY, 8),
        ZSTD_L(21, 19, 20, 4, 5, ZSTD_LAZY2, 16),    ZSTD_L(22, 20, 21, 4, 5, ZSTD_LAZY2, 16),
        ZSTD_L(22, 21, 22, 5, 5, ZSTD_LAZY2, 16),    ZSTD_L(22, 21, 22, 6, 5, ZSTD_LAZY2, 16),
        ZSTD_L(22, 22, 23, 6, 5, ZSTD_LAZY2, 32),    ZSTD_L(22, 22, 22, 4, 5, ZSTD_BTLAZY2, 32),
        ZSTD_L(22, 22, 23, 5, 5, ZSTD_BTLAZY2, 32),  ZSTD_L(22, 23, 23, 6, 5, ZSTD_BTLAZY2, 32),
        ZSTD_L(22, 22, 22, 5, 5, ZSTD_BTOPT, 48),    ZSTD_L(23, 23, 22, 5, 4, ZSTD_BTOPT, 64),
        ZSTD_L(23, 23, 22, 6, 3, ZSTD_BTULTRA, 64),  ZSTD_L(23, 24, 22, 7, 3, ZSTD_BTULTRA2, 256),
        ZSTD_L(25, 25, 23, 7, 3, ZSTD_BTULTRA2, 256), ZSTD_L(26, 26, 24, 7, 3, ZSTD_BTULTRA2, 512),
        ZSTD_L(27, 27, 25, 9, 3, ZSTD_BTULTRA2, 999),
    },
    {
        ZSTD_L(18, 12, 13, 1, 5, ZSTD_FAST, 1),      ZSTD_L(18, 13, 14, 1, 6, ZSTD_FAST, 0),
        ZSTD_L(18, 14, 14, 1, 5, ZSTD_DFAST, 0),     ZSTD_L(18, 16, 16, 1, 4, ZSTD_DFAST, 0),
        ZSTD_L(18, 16, 17, 3, 5, ZSTD_GREEDY, 2),    ZSTD_L(18, 17, 18, 5, 5, ZSTD_GREEDY, 2),
        ZSTD_L(18, 18, 19, 3, 5, ZSTD_LAZY, 4),      ZSTD_L(18, 18, 19, 4, 4, ZSTD_LAZY, 4),
        ZSTD_L(18, 18, 19, 4, 4, ZSTD_LAZY2, 8),     ZSTD_L(18, 18, 19, 5, 4, ZSTD_LAZY2, 8),
        ZSTD_L(18, 18, 19, 6, 4, ZSTD_LAZY2, 8),     ZSTD_L(18, 18, 19, 5, 4, ZSTD_BTLAZY2, 12),
        ZSTD_L(18, 19, 19, 7, 4, ZSTD_BTLAZY2, 12),  ZSTD_L(18, 18, 19, 4, 4, ZSTD_BTOPT, 16),
        ZSTD_L(18, 18, 19, 4, 3, ZSTD_BTOPT, 32),    ZSTD_L(18, 18, 19, 6, 3, ZSTD_BTOPT, 128),
        ZSTD_L(18, 19, 19, 6, 3, ZSTD_BTULTRA, 128), ZSTD_L(18, 19, 19, 8, 3, ZSTD_BTULTRA, 256),
        ZSTD_L(18, 19, 19, 6, 3, ZSTD_BTULTRA2, 128), ZSTD_L(18, 19, 19, 8, 3, ZSTD_BTULTRA2, 256),
        ZSTD_L(18, 19, 19, 10, 3, ZSTD_BTULTRA2, 512), ZSTD_L(18, 19, 19, 12, 3, ZSTD_BTULTRA2, 512),
        ZSTD_L(18, 19, 19, 13, 3, ZSTD_BTULTRA2, 999),
    },
    {
        ZSTD_L(17, 12, 12, 1, 5, ZSTD_FAST, 1),      ZSTD_L(17, 12, 13, 1, 6, ZSTD_FAST, 0),
        ZSTD_L(17, 13, 15, 1, 5, ZSTD_FAST, 0),      ZSTD_L(17, 15, 16, 2, 5, ZSTD_DFAST, 0),
        ZSTD_L(17, 17, 17, 2, 4, ZSTD_DFAST, 0),     ZSTD_L(17, 16, 17, 3, 4, ZSTD_GREEDY, 2),
        ZSTD_L(17, 16, 17, 3, 4, ZSTD_LAZY, 4),      ZSTD_L(17, 16, 17, 3, 4, ZSTD_LAZY2, 8),
        ZSTD_L(17, 16, 17, 4, 4, ZSTD_LAZY2, 8),     ZSTD_L(17, 16, 17, 5, 4, ZSTD_LAZY2, 8),
        ZSTD_L(17, 16, 17, 6, 4, ZSTD_LAZY2, 8),     ZSTD_L(17, 17, 17, 5, 4, ZSTD_BTLAZY2, 8),
        ZSTD_L(17, 18, 17, 7, 4, ZSTD_BTLAZY2, 12),  ZSTD_L(17, 18, 17, 3, 4, ZSTD_BTOPT, 12),
        ZSTD_L(17, 18, 17, 4, 3, ZSTD_BTOPT, 32),    ZSTD_L(17, 18, 17, 6, 3, ZSTD_BTOPT, 256),
        ZSTD_L(17, 18, 17, 6, 3, ZSTD_BTULTRA, 128), ZSTD_L(17, 18, 17, 8, 3, ZSTD_BTULTRA, 256),
        ZSTD_L(17, 18, 17, 10, 3, ZSTD_BTULTRA, 512), ZSTD_L(17, 18, 17, 5, 3, ZSTD_BTULTRA2, 256),
        ZSTD_L(17, 18, 17, 7, 3, ZSTD_BTULTRA2, 512), ZSTD_L(17, 18, 17, 9, 3, ZSTD_BTULTRA2, 512),
        ZSTD_L(17, 18, 17, 11, 3, ZSTD_BTULTRA2, 999),
    },
    {
        ZSTD_L(14, 12, 13, 1, 5, ZSTD_FAST, 1),      ZSTD_L(14, 14, 15, 1, 5, ZSTD_FAST, 0),
        ZSTD_L(14, 14, 15, 1, 4, ZSTD_FAST, 0),      ZSTD_L(14, 14, 15, 2, 4, ZSTD_DFAST, 0),
        ZSTD_L(14, 14, 14, 4, 4, ZSTD_GREEDY, 2),    ZSTD_L(14, 14, 14, 3, 4, ZSTD_LAZY, 4),
        ZSTD_L(14, 14, 14, 4, 4, ZSTD_LAZY2, 8),     ZSTD_L(14, 14, 14, 6, 4, ZSTD_LAZY2, 8),
        ZSTD_L(14, 14, 14, 8, 4, ZSTD_LAZY2, 8),     ZSTD_L(14, 15, 14, 5, 4, ZSTD_BTLAZY2, 8),
        ZSTD_L(14, 15, 14, 9, 4, ZSTD_BTLAZY2, 8),   ZSTD_L(14, 15, 14, 3, 4, ZSTD_BTOPT, 12),
        ZSTD_L(14, 15, 14, 4, 3, ZSTD_BTOPT, 24),    ZSTD_L(14, 15, 14, 5, 3, ZSTD_BTULTRA, 32),
        ZSTD_L(14, 15, 15, 6, 3, ZSTD_BTULTRA, 64),  ZSTD_L(14, 15, 15, 7, 3, ZSTD_BTULTRA, 256),
        ZSTD_L(14, 15, 15, 5, 3, ZSTD_BTULTRA2, 48), ZSTD_L(14, 15, 15, 6, 3, ZSTD_BTULTRA2, 128),
        ZSTD_L(14, 15, 15, 7, 3, ZSTD_BTULTRA2, 256), ZSTD_L(14, 15, 15, 8, 3, ZSTD_BTULTRA2, 256),
        ZSTD_L(14, 15, 15, 8, 3, ZSTD_BTULTRA2, 512), ZSTD_L(14, 15, 15, 9, 3, ZSTD_BTULTRA2, 512),
        ZSTD_L(14, 15, 15, 10, 3, ZSTD_BTULTRA2, 999),
    },
};
#undef ZSTD_L

/* Level 0 is the default, 3; a level below 0 is --fast.  A long window
   replaces the level's.  A known input size shrinks the window to the
   input and the tables to the window, as libzstd's adjustment does. */
static fn zstd_level_params(b32 level, p8 long_log, p64 size,
                            zstd_params address_to p)
{
        /* A known size picks its row from the table that size belongs to:
           to 16 KiB, 128 KiB, 256 KiB, else the largest. */
        positive const table = size ? (size <= (256u << 10)) + (size <= (128u << 10)) +
                                          (size <= (16u << 10))
                                    : 0;

        if (level < 0)
        {
                address_to p = zstd_level_table[table][0];
                p->target_length = (p16)(-level > 65535 ? 65535 : -level);
        }
        else
                address_to p = zstd_level_table[table][!level ? 3 : level > 22 ? 22 : level];
        if (long_log)
                p->window_log = long_log;
        if (size && size < ((p64)1 << 30))
        {
                p8 source_log = size < 64 ? 6
                                          : (p8)(zstd_highbit32((p32)(size - 1)) + 1);

                if (p->window_log > source_log)
                        p->window_log = source_log;
        }
        if (p->window_log < 10)
                p->window_log = 10;
        if (p->hash_log > p->window_log + 1)
                p->hash_log = p->window_log + 1;
        {
                p8 cycle = (p8)(p->chain_log - (p->strategy >= ZSTD_BTLAZY2));

                if (cycle > p->window_log)
                        p->chain_log = (p8)(p->chain_log - (cycle - p->window_log));
        }
}

/* The row finder's slots a row: the search log held to 4-6, as libzstd. */
static p8 zstd_row_log(const zstd_params address_to p)
{
        return p->search_log < 4 ? 4 : p->search_log > 6 ? 6 : p->search_log;
}

/* What chain holds: nothing for fast, a row finder's tags and row heads
   for greedy to lazy2, and 2^chain_log indices (dfast's short hash, the
   binary tree's links) otherwise. */
static positive zstd_chain_bytes(const zstd_params address_to p)
{
        if (p->strategy == ZSTD_FAST)
                return 0;
        if (p->strategy >= ZSTD_GREEDY && p->strategy <= ZSTD_LAZY2)
                return ((positive)1 << p->hash_log) +
                       ((positive)1 << (p->hash_log - zstd_row_log(p)));
        return (positive)4 << p->chain_log;
}

typedef struct
{
        p32 lit;
        p32 match;
        p32 off;
        p8 ll_code;
        p8 ml_code;
        p8 of_code;
} zstd_enc_seq;

/* How many sequences use each literal length, offset and match length
   code: counted as zstd_store makes them, for the tables of the block. */
typedef struct
{
        p32 ll[36];
        p32 of[32];
        p32 ml[53];
} zstd_seq_freq;

typedef struct
{
        p64 acc;
        p8 bits;
        p8 address_to buf;
        positive cap;
        positive n;
        bool full;
} zstd_bout;

typedef struct
{
        p16 state[512];
        p32 delta_nb[53];
        bipolar delta_find[53];
        p8 log;
} zstd_ctable;

/* What a sequence stream's last block left for a repeat: its mode (0
   predefined, 1 RLE, 2 its own table), the table and its counts, and the
   RLE symbol. */
typedef struct
{
        zstd_ctable table;
        bipolar norm[53];
        p8 mode;
        p8 symbol;
        bool valid;
} zstd_fse_prior;

/* What a block's tables become once it is written: the literal table it
   sent, when it sent one, and the three sequence tables. */
typedef struct
{
        bool fresh;
        bool seqs;
        zstd_fse_prior pending[3];
} zstd_entropy_state;

/* The tables a block starts from, kept to put back. */
typedef struct
{
        zstd_fse_prior prior[3];
        p32 huf_table[256];
        p8 huf_length[256];
        bool huf_valid;
} zstd_snapshot;

/* The optimal parser's statistics, libzstd's: counts of literal bytes,
   literal length codes, match length codes and offset codes from the
   sequences already chosen, and the cost in 1/256 bits of each whole. */
typedef struct
{
        p32 lit[256];
        p32 ll[36];
        p32 ml[53];
        p32 of[32];
        p32 lit_sum;
        p32 ll_sum;
        p32 ml_sum;
        p32 of_sum;
        p32 lit_base;
        p32 ll_base;
        p32 ml_base;
        p32 of_base;
        bool predefined;
        //      Each symbol's weight, and each code's extra bits less its
        //      weight, kept current as the counts move: a price asked once a
        //      length at every position is then two loads, not a bit scan.
        bool fractional;
        p32 lit_w[256];
        p32 ll_w[36];
        p32 ml_w[53];
        p32 of_w[32];
} zstd_price;

/* A position on the optimal path: its price, and the stretch that reaches
   it (a match of mlen at offset value off, then litlen literals), with the
   repeat offsets after it. */
typedef struct
{
        bipolar price;
        p32 off;
        p32 mlen;
        p32 litlen;
        p32 rep[3];
} zstd_opt_node;

typedef struct
{
        p32 off;
        p32 len;
} zstd_opt_match;

typedef struct
{
        zstd_params p;
        bool checksum;
        /* History, then the block being filled.  Index i is base[i]. */
        p8 address_to storage;
        positive storage_room;
        p32 storage_index;
        /* Bytes emitted, and what the frame (or job) has saved so far:
           the block splitter only cuts once blocks are paying. */
        p64 emitted;
        bipolar savings;
        p8 address_to base;
        p32 start;      /* the frame's first index */
        p32 block;      /* the first index not yet compressed */
        p32 filled;     /* one past the last byte taken */
        p32 next;       /* the first index the chain has not taken */
        p32 address_to hash;
        p32 address_to chain;
        positive hash_bytes;
        positive chain_bytes;
        p32 rep[3];
        positive nseq;
        positive nlit;
        zstd_seq_freq freq;
        /* The literal table the last compressed literals sent. */
        p32 huf_table[256];
        p8 huf_length[256];
        bool huf_valid;
        zstd_fse_prior prior[3];
        /* The binary-tree levels: a three-byte hash when min_match is 3,
           the next index it takes, and the parser's statistics. */
        p32 address_to hash3;
        positive hash3_bytes;
        p8 hash3_log;
        p32 next3;
        zstd_price price;
        /* The scratch a block is built in, and where its bytes go: the
           process's buffers and zstd_enc_out for the one encoder, a job's
           own mappings and its parallel output for each job's. */
        zstd_enc_seq address_to seqs;
        p8 address_to lits;
        p8 address_to bits;
        p8 address_to block_out;
        p8 address_to packed;
        p32 address_to lit_table_new;
        p8 address_to lit_length_new;
        zstd_opt_node address_to opt;
        zstd_opt_match address_to matches;
        /* The post-parse block splitter's scratch: the state a block leaves,
           room for a trial block and for a block's chunks, and the literals
           and source bytes before each sequence. */
        zstd_entropy_state split_state;
        zstd_snapshot snapshot;
        p8 address_to trial_out;
        p8 address_to split_out;
        p32 address_to cum_lit;
        p32 address_to cum_src;
        address_any output;
} zstd_encoder;

/* What a training pass counts in the blocks it parses instead of writing
   them: the literals and the three sequence code streams. */
typedef struct
{
        p32 lit[256];
        p32 ll[36];
        p32 of[32];
        p32 ml[53];
} zstd_train_stats;
static zstd_train_stats address_to zstd_train_sink;

static zstd_encoder zstd_enc;
static zstd_ctable zstd_ct_ll;
static zstd_ctable zstd_ct_of;
static zstd_ctable zstd_ct_ml;
static bool zstd_ct_ready;
static zstd_enc_seq zstd_seqs[ZSTD_ENC_SEQ_MAX];
static p8 zstd_enc_lits[ZSTD_BLOCK_MAX + 64];
static p8 zstd_enc_bits[ZSTD_BLOCK_MAX + 256];
static p8 zstd_enc_block_out[ZSTD_BLOCK_MAX + 2048];
static p8 zstd_enc_trial_out[ZSTD_BLOCK_MAX + 2048];
static p8 zstd_enc_split_out[ZSTD_BLOCK_MAX + 2048 * 4];
static p32 zstd_enc_cum_lit[ZSTD_ENC_SEQ_MAX + 2];
static p32 zstd_enc_cum_src[ZSTD_ENC_SEQ_MAX + 2];
static p8 zstd_packed_lits[ZSTD_BLOCK_MAX * 2 + 512];
static p32 zstd_lit_table_new[256];
static p8 zstd_lit_length_new[256];
#define ZSTD_OPT_NUM 4096
#define ZSTD_PRICE_MAX ((bipolar)1 << 30)
static zstd_opt_node zstd_opt[ZSTD_OPT_NUM + 3];
static zstd_opt_match zstd_matches[ZSTD_OPT_NUM + 3];

static __attribute__((always_inline)) inline fn zstd_bout_add(zstd_bout address_to b, p64 v, p8 nbits)
{
        if (!nbits)
                return;
        if (nbits >= 64 || b->n + 16 >= b->cap)
        {
                b->full = true;
                return;
        }
        b->acc |= (v & (((p64)1 << nbits) - 1)) << b->bits;
        b->bits += nbits;
        if (b->bits >= 32)
        {
                memory_store_unaligned(p32, b->buf + b->n, (p32)b->acc);
                b->n += 4;
                b->acc >>= 32;
                b->bits -= 32;
        }
}

static fn zstd_bout_pad(zstd_bout address_to b)
{
        while (b->bits >= 8)
        {
                b->buf[b->n++] = (p8)b->acc;
                b->acc >>= 8;
                b->bits -= 8;
        }
        if (b->bits)
                b->buf[b->n++] = (p8)b->acc;
        b->bits = 0;
        b->acc = 0;
}

static bool zstd_bout_close(zstd_bout address_to b)
{
        zstd_bout_add(b, 1, 1);
        zstd_bout_pad(b);
        return !b->full && b->n && b->buf[b->n - 1];
}

static bool zstd_ctable_build(zstd_ctable address_to ct,
                              const bipolar address_to norm, positive max_sym,
                              p8 log)
{
        positive size = (positive)1 << log;
        p16 cumul[64];
        p8 symbol[512];
        positive s;
        positive u;
        positive total;

        if (log > 9 || size > 512 || max_sym > 52)
                return false;
        memory_fill(symbol, 0, size);
        cumul[0] = 0;
        for (s = 0; s <= max_sym; s++)
                cumul[s + 1] = cumul[s] +
                    (norm[s] == -1 ? 1 : norm[s] < 0 ? 0 : (p16)norm[s]);
        if (!zstd_fse_spread(symbol, norm, max_sym, log))
                return false;
        for (u = 0; u < size; u++)
        {
                p8 sym = symbol[u];

                ct->state[cumul[sym]++] = (p16)(size + u);
        }
        total = 0;
        memory_fill(ct->delta_nb, 0, sizeof(ct->delta_nb));
        memory_fill(ct->delta_find, 0, sizeof(ct->delta_find));
        for (s = 0; s <= max_sym; s++)
        {
                bipolar n = norm[s];

                if (!n)
                        continue;
                if (n == -1 || n == 1)
                {
                        ct->delta_nb[s] = ((p32)log << 16) - ((p32)1 << log);
                        ct->delta_find[s] = (bipolar)total - 1;
                        total++;
                }
                else
                {
                        p8 max_bits = (p8)(log - zstd_highbit32((p32)n - 1));
                        p32 min_state = (p32)n << max_bits;

                        ct->delta_nb[s] = ((p32)max_bits << 16) - min_state;
                        ct->delta_find[s] = (bipolar)total - (bipolar)n;
                        total += (positive)n;
                }
        }
        ct->log = log;
        return true;
}

static fn zstd_ct_init(void)
{
        if (zstd_ct_ready)
                return;
        if (zstd_ctable_build(address_of zstd_ct_ll, zstd_ll_default, 35, 6) &&
            zstd_ctable_build(address_of zstd_ct_of, zstd_of_default, 28, 5) &&
            zstd_ctable_build(address_of zstd_ct_ml, zstd_ml_default, 52, 6))
                zstd_ct_ready = true;
}

#define zstd_ll_codes (zstd_sequence_tab.ll_codes)
#define zstd_ml_codes (zstd_sequence_tab.ml_codes)

typedef struct
{
        p32 value;
        zstd_ctable address_to ct;
} zstd_cstate;

static bool zstd_cstate_init2(zstd_cstate address_to st, zstd_ctable address_to ct,
                              p8 symbol)
{
        p32 nb;
        bipolar idx;

        st->ct = ct;
        nb = (ct->delta_nb[symbol] + (1u << 15)) >> 16;
        st->value = (nb << 16) - ct->delta_nb[symbol];
        idx = (bipolar)(st->value >> nb) + ct->delta_find[symbol];
        if (idx < 0 || idx >= (bipolar)((positive)1 << ct->log))
                return false;
        st->value = ct->state[idx];
        return true;
}

static __attribute__((always_inline)) inline fn zstd_cstate_encode(zstd_bout address_to b, zstd_cstate address_to st,
                             p8 symbol)
{
        p32 nb = (st->value + st->ct->delta_nb[symbol]) >> 16;
        bipolar idx = (bipolar)(st->value >> nb) + st->ct->delta_find[symbol];

        zstd_bout_add(b, st->value, (p8)nb);
        if (idx < 0 || idx >= (bipolar)((positive)1 << st->ct->log))
        {
                b->full = true;
                return;
        }
        st->value = st->ct->state[idx];
}

static fn zstd_cstate_flush(zstd_bout address_to b, zstd_cstate address_to st)
{
        zstd_bout_add(b, st->value, st->ct->log);
}

static positive zstd_write_norm(p8 address_to dst, const bipolar address_to norm,
                                 positive max_sym, p8 log)
{
        zstd_bout b = {0};
        positive remaining = ((positive)1 << log) + 1;
        positive threshold = (positive)1 << log;
        positive sym = 0;
        p8 bits = log + 1;
        bool zero = false;

        b.buf = dst;
        b.cap = 256;
        zstd_bout_add(address_of b, log - 5, 4);
        while (remaining > 1 && sym <= max_sym)
        {
                if (zero)
                {
                        positive run = 0;
                        while (sym <= max_sym && !norm[sym])
                                sym++, run++;
                        while (run >= 3)
                                zstd_bout_add(address_of b, 3, 2), run -= 3;
                        zstd_bout_add(address_of b, run, 2);
                }
                if (sym > max_sym)
                        return 0;
                positive maximum = 2 * threshold - 1 - remaining;
                positive count = norm[sym] + 1;
                positive value = count;
                if (count >= threshold)
                        value += maximum;
                zstd_bout_add(address_of b, value,
                              count < maximum ? bits - 1 : bits);
                remaining -= norm[sym] < 0 ? 1 : (positive)norm[sym];
                zero = !norm[sym++];
                while (remaining < threshold)
                        threshold >>= 1, bits--;
        }
        zstd_bout_pad(address_of b);
        return remaining == 1 && !b.full ? b.n : 0;
}

/* Fractional log estimate is used only to choose between legal tables.
   Both candidates use the exact FSE coder after the choice. */
static positive zstd_log_cost(p32 n)
{
        positive log = zstd_highbit32(n);
        return log * 256 + (((positive)n - ((positive)1 << log)) << 8) /
                               ((positive)1 << log);
}

/*
        libzstd's FSE_normalizeCount, symbol for symbol: each count scaled to
        the table's 2^log cells by a 62-bit reciprocal, the fractions of the
        small ones rounded by libzstd's own thresholds, a symbol of no more
        than total >> log occupying one cell as "less than one" (-1) when
        low_prob and as 1 otherwise, and what is left over given to the
        largest; where that would take more than half of it the second method
        spreads the remainder over the symbols not yet placed.  False when
        the counts cannot be laid out (one symbol holds them all, or the
        table is too small for the symbols).
*/
static bool zstd_normalize_second(bipolar address_to norm, positive log,
                                  const p32 address_to freq, positive total,
                                  positive count, bipolar low)
{
        bipolar const not_yet = -2;
        positive distributed = 0;
        positive to_distribute;
        positive const low_threshold = total >> log;
        positive low_one = (total * 3) >> (log + 1);

        for (positive s = 0; s < count; s++)
        {
                if (!freq[s])
                {
                        norm[s] = 0;
                        continue;
                }
                if (freq[s] <= low_threshold)
                {
                        norm[s] = low;
                        distributed++;
                        total -= freq[s];
                        continue;
                }
                if (freq[s] <= low_one)
                {
                        norm[s] = 1;
                        distributed++;
                        total -= freq[s];
                        continue;
                }
                norm[s] = not_yet;
        }
        to_distribute = ((positive)1 << log) - distributed;
        if (!to_distribute)
                return true;
        if (total / to_distribute > low_one)
        {
                low_one = (total * 3) / (to_distribute * 2);
                for (positive s = 0; s < count; s++)
                        if (norm[s] == not_yet && freq[s] <= low_one)
                        {
                                norm[s] = 1;
                                distributed++;
                                total -= freq[s];
                        }
                to_distribute = ((positive)1 << log) - distributed;
        }
        if (distributed == count)
        {
                positive best = 0, best_count = 0;

                for (positive s = 0; s < count; s++)
                        if (freq[s] > best_count)
                                best = s, best_count = freq[s];
                norm[best] += (bipolar)to_distribute;
                return true;
        }
        if (!total)
        {
                for (positive s = 0; to_distribute; s = (s + 1) % count)
                        if (norm[s] > 0)
                        {
                                to_distribute--;
                                norm[s]++;
                        }
                return true;
        }
        {
                p64 const step_log = 62 - log;
                p64 const mid = ((p64)1 << (step_log - 1)) - 1;
                p64 const step = ((((p64)1 << step_log) * to_distribute) + mid) / total;
                p64 running = mid;

                for (positive s = 0; s < count; s++)
                        if (norm[s] == not_yet)
                        {
                                p64 const stop = running + (p64)freq[s] * step;
                                p32 const weight = (p32)(stop >> step_log) - (p32)(running >> step_log);

                                if (weight < 1)
                                        return false;
                                norm[s] = (bipolar)weight;
                                running = stop;
                        }
        }
        return true;
}

static bool zstd_normalize(const p32 address_to freq, positive count, positive total,
                           positive log, bool low_prob, bipolar address_to norm,
                           positive address_to last)
{
        static const p32 round_to_beat[8] = {0, 473195, 504333, 520860, 550000, 700000, 750000, 830000};
        bipolar const low = low_prob ? -1 : 1;
        p64 const scale = 62 - log;
        p64 const step = ((p64)1 << 62) / total;
        p64 const vstep = (p64)1 << (scale - 20);
        bipolar to_distribute = (bipolar)1 << log;
        positive const low_threshold = total >> log;
        positive largest = 0;
        bipolar largest_p = 0;

        address_to last = 0;
        for (positive s = 0; s < count; s++)
        {
                if (freq[s] == total)
                        return false;
                if (!freq[s])
                {
                        norm[s] = 0;
                        continue;
                }
                address_to last = s;
                if (freq[s] <= low_threshold)
                {
                        norm[s] = low;
                        to_distribute--;
                }
                else
                {
                        bipolar proba = (bipolar)(((p64)freq[s] * step) >> scale);

                        if (proba < 8)
                        {
                                p64 const beat = vstep * round_to_beat[proba];

                                proba += ((p64)freq[s] * step) - ((p64)proba << scale) > beat;
                        }
                        if (proba > largest_p)
                        {
                                largest_p = proba;
                                largest = s;
                        }
                        norm[s] = proba;
                        to_distribute -= proba;
                }
        }
        if (-to_distribute >= (norm[largest] >> 1))
                return zstd_normalize_second(norm, log, freq, total, count, low);
        norm[largest] += to_distribute;
        return true;
}

static p8 zstd_fse_log(positive n, positive last, p8 max_log);

static positive zstd_pack_weights(p8 address_to dst, p8 address_to weight,
                                   positive n)
{
        p32 freq[12] = {0};
        bipolar norm[12] = {0};
        zstd_ctable ct;
        zstd_cstate state[2];
        zstd_bout b = {0};
        positive at;
        positive max_sym = 0;
        positive head;
        p8 log;

        if (n < 2)
                return 0;
        for (at = 0; at < n; at++)
                freq[weight[at]]++;
        for (at = 0; at < 12; at++)
                if (freq[at])
                        max_sym = at;
        log = zstd_fse_log(n, max_sym, 6);
        /* A one-symbol, zero-bit FSE machine has no finite end marker. */
        if (freq[max_sym] == n)
        {
                positive other = max_sym ? 0 : 1;
                norm[max_sym] = (bipolar)(((positive)1 << log) - 1);
                norm[other] = 1;
                if (other > max_sym)
                        max_sym = other;
        }
        else if (!zstd_normalize(freq, max_sym + 1, n, log, false, norm, address_of at))
                return 0;
        head = zstd_write_norm(dst + 1, norm, max_sym, log);
        if (!head || !zstd_ctable_build(address_of ct, norm, max_sym, log))
                return 0;
        b.buf = dst + 1 + head;
        b.cap = 256 - head;
        if (!zstd_cstate_init2(address_of state[(n - 1) & 1], address_of ct,
                                weight[n - 1]) ||
            !zstd_cstate_init2(address_of state[(n - 2) & 1], address_of ct,
                                weight[n - 2]))
                return 0;
        at = n - 2;
        while (at)
        {
                at--;
                zstd_cstate_encode(address_of b, address_of state[at & 1],
                                    weight[at]);
        }
        zstd_cstate_flush(address_of b, address_of state[1]);
        zstd_cstate_flush(address_of b, address_of state[0]);
        if (!zstd_bout_close(address_of b) || head + b.n >= 128)
                return 0;
        dst[0] = (p8)(head + b.n);
        return 1 + head + b.n;
}

static bool zstd_enc_out(p8 address_to p, positive n)
{
        if (zstd_output.bytes)
                return byte_store_append_exact(address_of zstd_output, p, n) ||
                       zstd_fail("zstd output is too small");
        return system_write_all((positive)zstd_out_fd, p, n) == n ||
               zstd_fail("zstd write failed");
}

/* A block header: last flag, type and size in three little-endian bytes;
   the fourth is the caller's (an RLE block's byte) or unused. */
static fn zstd_block_header(p8 address_to into, bool last, p8 type, positive size)
{
        memory_store_unaligned(p32, into,
                               (last ? 1u : 0) | (p32)type << 1 | (p32)size << 3);
}

/* A block's bytes: a job's go to its output, which the pool orders. */
static bool zstd_enc_emit(zstd_encoder address_to e, p8 address_to p, positive n)
{
        e->emitted += n;
        if (e->output)
                return parallel_write(e->output, p, n);
        return zstd_enc_out(p, n);
}

static bool zstd_emit_raw_block(zstd_encoder address_to e, p8 address_to src,
                                positive n, bool last)
{
        p8 header[4];

        zstd_block_header(header, last, 0, n);
        return zstd_enc_emit(e, header, 3) && zstd_enc_emit(e, src, n);
}

/* A raw or RLE literals header: the type, then the size in 5, 12 or 20
   bits.  Three bytes are always written; the answer is how many count. */
static positive zstd_literals_header(p8 address_to out, p8 type, positive n)
{
        p32 v = type | (p32)n << 3;
        positive used = 1;

        if (n >= 32)
        {
                v = type | (n < 4096 ? 4u : 12u) | (p32)n << 4;
                used = n < 4096 ? 2 : 3;
        }
        memory_store_unaligned(p16, out, (p16)v);
        out[2] = (p8)(v >> 16);
        return used;
}

/*
        The Huffman code for the counts of literals freq, symbols to max_sym:
        each symbol's length (eleven bits at most, the tree complete), the
        weights they name, the longest, the tree's description in out with
        its size in *head (0 when it cannot be written), and the bits the
        counts need in *bits.  The weights go FSE-compressed when that comes
        to under half the count of them (and more than one byte), as
        libzstd's writer decides, and four bits each otherwise.
*/
static fn zstd_huffman_shape(const p32 address_to freq, positive max_sym,
                             p8 address_to length_new, p8 address_to weight,
                             positive address_to max_bits_out, p8 address_to out,
                             positive address_to head_out, p64 address_to bits_out)
{
        p32 work[256];
        p8 max_bits;
        positive head;
        p64 new_bits = 0;
        positive at;

        memory_copy_apart(work, freq, sizeof(work));
        for (;;)
        {
                positive kraft = 0;
                max_bits = 0;
                huffman_lengths(work, 256, length_new, 11);
                for (at = 0; at < 256; at++)
                        if (length_new[at])
                        {
                                kraft += (positive)1 << (11 - length_new[at]);
                                if (length_new[at] > max_bits)
                                        max_bits = length_new[at];
                        }
                if (kraft == 2048)
                        break;
                /* Flatten only when an unconstrained tree exceeds 11 bits.
                   Every used symbol stays present, and the rebuilt tree is
                   complete; truncating depths alone oversubscribes it. */
                for (at = 0; at < 256; at++)
                        if (work[at])
                                work[at] = (work[at] + 1) >> 1;
        }
        for (at = 0; at <= max_sym; at++)
        {
                weight[at] = length_new[at]
                                 ? (p8)(max_bits + 1 - length_new[at])
                                 : 0;
                new_bits += (p64)freq[at] * length_new[at];
        }
        /* The weights go FSE-compressed when that comes to under half the
           count of them (and more than one byte), as libzstd's writer
           decides, and four bits each otherwise. */
        head = zstd_pack_weights(out, weight, max_sym);
        if (max_sym <= 128 && !(head > 2 && head - 1 < max_sym / 2))
        {
                out[0] = (p8)(127 + max_sym);
                for (at = 0; at < max_sym; at += 2)
                        out[1 + at / 2] = (p8)(weight[at] << 4) |
                                (at + 1 < max_sym ? weight[at + 1] : 0);
                head = 1 + (max_sym + 1) / 2;
        }
        address_to max_bits_out = max_bits;
        address_to head_out = head;
        address_to bits_out = new_bits;
}

/*
        Huffman literals into the encoder's packed buffer (zstd_packed_lits
        with no encoder): a new tree, or none when the
        encoder's last table codes every symbol here for no more bytes than
        a new tree and its description (treeless literals); one stream under
        256 literals, four above.  Answers the payload's size and writes its
        header, or 0 when raw literals are no larger.  *fresh says a new tree
        went out, which the block commits once it wins.  With no encoder
        there is no last table.
*/
static positive zstd_pack_literals(zstd_encoder address_to e,
                                   p8 address_to src, positive n,
                                   p8 address_to header, positive address_to hn,
                                   bool address_to fresh)
{
        p32 freq[256] = {0}, f1[256] = {0}, f2[256] = {0}, f3[256] = {0};
        p8 weight[256];
        positive max_sym = 0;
        positive symbols = 0;
        positive max_bits = 0;
        positive head = 0;
        positive size;
        positive at;
        positive header_n;
        p64 new_bits = 0;
        p64 old_bits = 0;
        p64 field;
        bool reuse = false;
        bool const single = n < 256;
        p8 address_to const packed_out = e ? e->packed : zstd_packed_lits;
        p32 address_to const table_new = e ? e->lit_table_new : zstd_lit_table_new;
        p8 address_to const length_new = e ? e->lit_length_new : zstd_lit_length_new;
        p32 address_to table = table_new;
        p8 type = 2;

        address_to fresh = false;
        if (n < 64)
                return 0;
        /* Literals that look like noise are sent raw without a full count,
           as libzstd judges them: when the block is mostly literals, the
           commonest byte of its first and last 4 KiB must beat 1/128 of
           them, and then of the whole. */
        if (e && n >= 40960 && (!e->nseq || n / e->nseq >= 20))
        {
                positive largest = 0;

                for (positive part = 0; part < 2; part++)
                {
                        p8 address_to const from = part ? src + n - 4096 : src;
                        positive most = 0;

                        memory_fill(freq, 0, sizeof(freq));
                        for (at = 0; at < 4096; at++)
                                freq[from[at]]++;
                        for (at = 0; at < 256; at++)
                                if (freq[at] > most)
                                        most = freq[at];
                        largest += most;
                }
                if (largest <= (8192 >> 7) + 4)
                        return 0;
                memory_fill(freq, 0, sizeof(freq));
        }
        for (at = 0; at + 4 <= n; at += 4)
        {
                freq[src[at]]++; f1[src[at + 1]]++;
                f2[src[at + 2]]++; f3[src[at + 3]]++;
        }
        for (; at < n; at++) freq[src[at]]++;
        for (at = 0; at < 256; at++) freq[at] += f1[at] + f2[at] + f3[at];
        {
                positive largest = 0;

                for (at = 0; at < 256; at++)
                        if (freq[at])
                        {
                                symbols++, max_sym = at;
                                if (freq[at] > largest)
                                        largest = freq[at];
                        }
                if (largest <= (n >> 7) + 4)
                        return 0;
        }
        if (symbols < 2)
                return 0;
        if (e && e->huf_valid)
        {
                reuse = true;
                for (at = 0; at <= max_sym && reuse; at++)
                        if (freq[at])
                        {
                                reuse = e->huf_length[at] != 0;
                                old_bits += (p64)freq[at] * e->huf_length[at];
                        }
        }
        zstd_huffman_shape(freq, max_sym, length_new, weight, address_of max_bits, packed_out,
                           address_of head, address_of new_bits);
        if (reuse && (!head || (old_bits + 7) / 8 <= head + (new_bits + 7) / 8))
        {
                table = e->huf_table;
                type = 3;
                size = 0;
        }
        else
        {
                if (!head)
                        return 0;
                zstd_huffman_codes(table_new, weight, max_sym + 1, max_bits);
                size = head;
                address_to fresh = true;
        }
        if (single)
                size += huffman_encode_back(packed_out + size, src, n, table);
        else
        {
                positive const segment = (n + 3) / 4;
                positive const jump = size;
                positive position = 0;

                size += 6;
                for (at = 0; at < 4; at++)
                {
                        positive const take = at == 3 ? n - position : segment;
                        positive const packed = huffman_encode_back(
                            packed_out + size, src + position, take, table);

                        if (at < 3)
                        {
                                memory_store_unaligned(p16, packed_out + jump + 2 * at, (p16)packed);
                        }
                        size += packed;
                        position += take;
                }
        }
        if (single)
        {
                field = type | (p64)n << 4 | (p64)size << 14;
                header_n = 3;
        }
        else if (n < 1024 && size < 1024)
        {
                field = type | 4 | (p64)n << 4 | (p64)size << 14;
                header_n = 3;
        }
        else if (n < 16384 && size < 16384)
        {
                field = type | 8 | (p64)n << 4 | (p64)size << 18;
                header_n = 4;
        }
        else
        {
                field = type | 12 | (p64)n << 4 | (p64)size << 22;
                header_n = 5;
        }
        if (size + header_n >= n + (n < 32 ? 1 : n < 4096 ? 2 : 3))
        {
                address_to fresh = false;
                return 0;
        }
        for (at = 0; at < header_n; at++)
                header[at] = (p8)(field >> (at * 8));
        address_to hn = header_n;
        return size;
}

/* libzstd's table log for n sequences up to symbol last: fewer states for
   fewer sequences, enough for every symbol, from 5 to max_log. */
static p8 zstd_fse_log(positive n, positive last, p8 max_log)
{
        bipolar const source = (bipolar)zstd_highbit32((p32)(n > 1 ? n - 1 : 1)) - 2;
        p8 minimum = (p8)(zstd_highbit32((p32)n) + 1);
        p8 const symbols = (p8)(zstd_highbit32((p32)(last ? last : 1)) + 2);
        p8 log = max_log;

        if (symbols < minimum)
                minimum = symbols;
        if (source < (bipolar)log)
                log = source < 0 ? 0 : (p8)source;
        if (minimum > log)
                log = minimum;
        if (log < 5)
                log = 5;
        if (log > max_log)
                log = max_log;
        return log;
}

/*
        One sequence stream's table for this block: predefined, RLE, a table
        of its own, or the last block's again (repeat), whichever costs the
        fewest bits with its description.  A new table's description or the
        RLE symbol goes at header[*header_n].  *next is what the stream
        leaves for the block after, committed when this block wins.  Answers
        the mode, or 255 when no mode can code the stream.
*/
static p8 zstd_choose_table(const zstd_fse_prior address_to prior,
                            zstd_fse_prior address_to next,
                            const p32 address_to freq, positive nseq,
                            positive max, p8 max_log,
                            const bipolar address_to defaults,
                            positive default_max,
                            const zstd_ctable address_to predefined,
                            p8 address_to header, positive address_to header_n,
                            positive last_code)
{
        p64 const none = (p64)-1;
        p64 basic = none;
        p64 repeat = none;
        p64 own = none;
        bipolar norm[53];
        positive last = 0;
        positive distinct = 0;
        positive norm_last = 0;
        positive described = 0;
        p8 log;
        bool ok;

        for (positive s = 0; s <= max; s++)
                if (freq[s])
                        distinct++, last = s;
        if (distinct == 1 && nseq > 2)
        {
                if (prior->valid && prior->mode == 1 && prior->symbol == last)
                {
                        address_to next = address_to prior;
                        return 3;
                }
                header[address_to header_n] = (p8)last;
                address_to header_n += 1;
                next->mode = 1;
                next->symbol = (p8)last;
                next->valid = true;
                return 1;
        }
        if (last <= default_max)
        {
                basic = 0;
                for (positive s = 0; s <= last; s++)
                        if (freq[s])
                                basic += (p64)freq[s] *
                                         (predefined->log * 256 -
                                          zstd_log_cost(defaults[s] < 0 ? 1 : (p32)defaults[s]));
        }
        if (prior->valid && prior->mode != 1)
        {
                repeat = 0;
                for (positive s = 0; s <= last; s++)
                        if (freq[s])
                        {
                                if (!prior->norm[s])
                                {
                                        repeat = none;
                                        break;
                                }
                                repeat += (p64)freq[s] *
                                          (prior->table.log * 256 -
                                           zstd_log_cost(prior->norm[s] < 0 ? 1 : (p32)prior->norm[s]));
                        }
        }
        log = zstd_fse_log(nseq, last, max_log);
        memory_fill(norm, 0, sizeof(norm));
        /* The last symbol is in the first state, not the stream, so the
           counts the table is built from leave one of it out, as libzstd's
           do; below 2048 sequences a rare symbol takes a cell whole, above
           it a fraction of one. */
        {
                p32 counted[53];
                positive total = nseq;

                memory_copy_apart(counted, freq, sizeof(counted));
                if (counted[last_code] > 1)
                {
                        counted[last_code]--;
                        total--;
                }
                ok = zstd_normalize(counted, last + 1, total, log, total >= 2048, norm,
                                    address_of norm_last);
        }
        if (ok &&
            (described = zstd_write_norm(header + address_to header_n, norm,
                                         norm_last, log)))
        {
                own = (p64)described * 8 * 256;
                for (positive s = 0; s <= last; s++)
                        if (freq[s])
                                own += (p64)freq[s] *
                                       (log * 256 - zstd_log_cost((p32)(norm[s] < 0 ? 1 : norm[s])));
        }
        if (repeat != none && repeat <= basic && repeat <= own)
        {
                address_to next = address_to prior;
                return 3;
        }
        if (basic != none && basic <= own)
        {
                next->mode = 0;
                next->table = address_to predefined;
                memory_fill(next->norm, 0, sizeof(next->norm));
                memory_copy_apart(next->norm, defaults,
                                  (default_max + 1) * sizeof(bipolar));
                next->valid = true;
                return 0;
        }
        if (own == none ||
            !zstd_ctable_build(address_of next->table, norm, norm_last, log))
                return 255;
        next->mode = 2;
        memory_copy_apart(next->norm, norm, sizeof(norm));
        next->valid = true;
        address_to header_n += described;
        return 2;
}

/*
        The sequence bitstream's writer, shaped like libzstd's: a field goes
        into a 64-bit container with no check and the container is stored
        whole (eight bytes) and advanced by its full bytes.  The first and
        last sequences are written here; lib.c's zstd_sequences_encode writes
        the rest, and sees to the room.  The first adds at most 16 bits of
        literal length before its first store and 47 of match length and
        offset before its second, so the container never passes 64 bits.
*/
typedef struct
{
        p64 acc;
        positive bits;
        p8 address_to at;
} zstd_bw;

static __attribute__((always_inline)) inline fn
zstd_bw_add(zstd_bw address_to w, p64 value, positive count)
{
        w->acc |= (value & (((p64)1 << count) - 1)) << w->bits;
        w->bits += count;
}

static __attribute__((always_inline)) inline fn zstd_bw_flush(zstd_bw address_to w)
{
        positive const bytes = w->bits >> 3;

        memory_store_unaligned(p64, w->at, w->acc);
        w->at += bytes;
        w->acc = bytes == 8 ? 0 : w->acc >> (bytes * 8);
        w->bits &= 7;
}

/* lib.c's zstd_sequences_encode job: the sequences from seqs[count - 1]
   down, the offset, match length and literal length tables (an all-zero
   table for a stream of one symbol, which sends no bits), their states, the
   pending bits and where the next byte goes, and where the room ends. */
typedef struct
{
        const zstd_enc_seq address_to seqs;
        positive count;
        zstd_ctable address_to of;
        zstd_ctable address_to ml;
        zstd_ctable address_to ll;
        positive of_state;
        positive ml_state;
        positive ll_state;
        p64 acc;
        positive bits;
        p8 address_to out;
        p8 address_to limit;
} zstd_seq_encode;

_Static_assert(sizeof(zstd_enc_seq) == 16 &&
               __builtin_offsetof(zstd_enc_seq, lit) == 0 &&
               __builtin_offsetof(zstd_enc_seq, match) == 4 &&
               __builtin_offsetof(zstd_enc_seq, off) == 8 &&
               __builtin_offsetof(zstd_enc_seq, ll_code) == 12 &&
               __builtin_offsetof(zstd_enc_seq, ml_code) == 13 &&
               __builtin_offsetof(zstd_enc_seq, of_code) == 14 &&
               __builtin_offsetof(zstd_ctable, delta_nb) == 1024 &&
               __builtin_offsetof(zstd_ctable, delta_find) == 1240 &&
               sizeof(zstd_seq_encode) == 96 &&
               sizeof(zstd_sequence_codes) == 1100,
               "the layouts lib.c's zstd_sequences_encode reads");

/* The table of a stream of one symbol: no bits, and the state stays 0. */
static zstd_ctable zstd_ct_zero;

/* One compressed block from nseq sequences and their nlit literals, of the
   n source bytes they cover, into block (three bytes of header, then the
   sections; room for a whole block): 1 with its size, or 0 when it would be
   no smaller than raw.  Nothing is committed: the state it would leave is
   in *state.  counted is the code counts of these sequences when the parse
   kept them, null when they are a part of a block's, which are counted
   here. */
static b32 zstd_entropy_chunk(zstd_encoder address_to e, const zstd_enc_seq address_to seqs,
                              const zstd_seq_freq address_to counted,
                              positive const nseq, p8 address_to lits, positive const nlit,
                              positive n, bool last, p8 address_to block,
                              positive address_to size, zstd_entropy_state address_to state)
{
        p8 address_to const out = block + 3;
        p8 address_to at = out;
        positive hn = 0;
        positive packed = 0;
        bool fresh = false;
        zstd_fse_prior address_to const pending = state->pending;
        p8 head[4];

        /* --fast sends its literals raw, as libzstd's negative levels
           do: the speed they ask for is mostly the Huffman pass. */
        if (nlit && !(e->p.strategy == ZSTD_FAST && e->p.target_length))
                packed = zstd_pack_literals(e, lits, nlit, at,
                                            address_of hn, address_of fresh);
        if (packed)
        {
                memory_copy_apart(at + hn, e->packed, packed);
                at += hn + packed;
        }
        else if (nlit > 1 && memory_span_byte(lits, lits[0], nlit) == nlit)
        {
                at += zstd_literals_header(at, 1, nlit);
                *at++ = lits[0];
        }
        else
        {
                at += zstd_literals_header(at, 0, nlit);
                memory_copy_apart(at, lits, nlit);
                at += nlit;
        }
        if ((positive)(at - out) + 4 >= n)
                return 0;
        if (nseq < 128)
                *at++ = (p8)nseq;
        else if (nseq < 0x7f00)
        {
                *at++ = (p8)(128 + (nseq >> 8));
                *at++ = (p8)nseq;
        }
        else
        {
                *at++ = 255;
                *at++ = (p8)(nseq - 0x7f00);
                *at++ = (p8)((nseq - 0x7f00) >> 8);
        }
        if (nseq)
        {
                zstd_seq_freq own;
                const zstd_seq_freq address_to freq = counted;
                p8 address_to const modes = at++;
                positive described = 0;
                zstd_ctable address_to lt;
                zstd_ctable address_to ot;
                zstd_ctable address_to mt;
                zstd_bw bits;
                zstd_cstate ls = {0}, os = {0}, ms = {0};
                zstd_seq_encode job;
                zstd_enc_seq address_to s;
                p8 ll_mode, of_mode, ml_mode;

                if (!freq)
                {
                        memory_fill(address_of own, 0, sizeof(own));
                        for (positive i = 0; i < nseq; i++)
                        {
                                own.ll[seqs[i].ll_code]++;
                                own.of[seqs[i].of_code]++;
                                own.ml[seqs[i].ml_code]++;
                        }
                        freq = address_of own;
                }
                ll_mode = zstd_choose_table(address_of e->prior[0], address_of pending[0],
                                            freq->ll, nseq, 35, 9, zstd_ll_default, 35,
                                            address_of zstd_ct_ll, at, address_of described,
                                            seqs[nseq - 1].ll_code);
                of_mode = zstd_choose_table(address_of e->prior[1], address_of pending[1],
                                            freq->of, nseq, 31, 8, zstd_of_default, 28,
                                            address_of zstd_ct_of, at, address_of described,
                                            seqs[nseq - 1].of_code);
                ml_mode = zstd_choose_table(address_of e->prior[2], address_of pending[2],
                                            freq->ml, nseq, 52, 9, zstd_ml_default, 52,
                                            address_of zstd_ct_ml, at, address_of described,
                                            seqs[nseq - 1].ml_code);
                if ((ll_mode | of_mode | ml_mode) > 3)
                        return 0;
                address_to modes = (p8)(ll_mode << 6 | of_mode << 4 | ml_mode << 2);
                at += described;
                /* An RLE stream sends no state, and neither does a repeat of
                   one: the table a repeat names is the RLE cell the decoder
                   kept, not the last table this encoder built. */
                lt = pending[0].mode == 1 ? address_of zstd_ct_zero : address_of pending[0].table;
                ot = pending[1].mode == 1 ? address_of zstd_ct_zero : address_of pending[1].table;
                mt = pending[2].mode == 1 ? address_of zstd_ct_zero : address_of pending[2].table;
                if (lt->log > 9 || ot->log > 8 || mt->log > 9)
                        return 0;
                bits.acc = 0;
                bits.bits = 0;
                bits.at = at;
                s = seqs + nseq - 1;
                if (!zstd_cstate_init2(address_of ls, lt, s->ll_code) ||
                    !zstd_cstate_init2(address_of os, ot, s->of_code) ||
                    !zstd_cstate_init2(address_of ms, mt, s->ml_code))
                        return 0;
                zstd_bw_add(address_of bits, s->lit - zstd_ll_base[s->ll_code],
                            zstd_ll_extra[s->ll_code]);
                zstd_bw_flush(address_of bits);
                zstd_bw_add(address_of bits, s->match - zstd_ml_base[s->ml_code],
                            zstd_ml_extra[s->ml_code]);
                zstd_bw_add(address_of bits, s->off - ((p32)1 << s->of_code), s->of_code);
                zstd_bw_flush(address_of bits);
                /* Past out + n the block is no smaller than raw whatever
                   follows, so that is where the room ends, with slack for the
                   stores that overhang it. */
                job.seqs = seqs;
                job.count = nseq - 1;
                job.of = ot;
                job.ml = mt;
                job.ll = lt;
                job.of_state = os.value;
                job.ml_state = ms.value;
                job.ll_state = ls.value;
                job.acc = bits.acc;
                job.bits = bits.bits;
                job.out = bits.at;
                job.limit = out + n + 24;
                if (zstd_sequences_encode(address_of job))
                        return 0;
                os.value = (p32)job.of_state;
                ms.value = (p32)job.ml_state;
                ls.value = (p32)job.ll_state;
                bits.acc = job.acc;
                bits.bits = job.bits;
                bits.at = job.out;
                zstd_bw_add(address_of bits, ms.value, mt->log);
                zstd_bw_add(address_of bits, os.value, ot->log);
                zstd_bw_flush(address_of bits);
                zstd_bw_add(address_of bits, ls.value, lt->log);
                zstd_bw_add(address_of bits, 1, 1);
                zstd_bw_flush(address_of bits);
                at = bits.at + (bits.bits ? 1 : 0);
                if ((positive)(at - out) >= n)
                        return 0;
        }
        zstd_block_header(head, last, 2, (positive)(at - out));
        memory_copy_apart(block, head, 3);
        address_to size = (positive)(at - block);
        state->fresh = fresh;
        state->seqs = nseq != 0;
        return 1;
}

/* The tables a written block leaves behind. */
static fn zstd_entropy_commit(zstd_encoder address_to e, zstd_entropy_state address_to state)
{
        if (state->fresh)
        {
                memory_copy_apart(e->huf_table, e->lit_table_new, sizeof(e->huf_table));
                memory_copy_apart(e->huf_length, e->lit_length_new, sizeof(e->huf_length));
                e->huf_valid = true;
        }
        if (state->seqs)
                memory_copy_apart(e->prior, state->pending, sizeof(state->pending));
}

/* The encoder's own sequences and literals as one block, written: 1 when
   written, 0 when it would be no smaller than raw (nothing written and
   nothing committed), -1 when the write failed. */
static b32 zstd_entropy_block(zstd_encoder address_to e, positive n, bool last)
{
        positive size;

        if (!zstd_entropy_chunk(e, e->seqs, address_of e->freq, e->nseq, e->lits, e->nlit, n, last,
                                e->block_out, address_of size, address_of e->split_state))
                return 0;
        if (!zstd_enc_emit(e, e->block_out, size))
                return -1;
        zstd_entropy_commit(e, address_of e->split_state);
        return 1;
}

/*
        libzstd's split after the parse, for btopt and up: a block's
        sequences are cut in halves where the halves written apart take fewer
        bytes than the whole, recursively down to 300 sequences, and each
        piece is a block of its own.  The sizes the halves are judged by are
        the blocks themselves written into a scratch buffer, against the
        tables the whole block would have started from; the pieces are then
        written in turn, each leaving its tables for the next, and if any is
        no smaller than raw the block goes out whole as it would have.
*/
#define ZSTD_SPLIT_MIN_SEQUENCES 300
#define ZSTD_SPLIT_MAX 196

/* The encoded size of sequences from first to stop, with its header. */
static positive zstd_split_cost(zstd_encoder address_to e, positive first, positive stop,
                                positive n, bool address_to compressed)
{
        positive const nseq = e->nseq;
        positive const lit_from = e->cum_lit[first];
        positive const lit_to = stop == nseq ? e->nlit : e->cum_lit[stop];
        positive const source = (stop == nseq ? n : e->cum_src[stop]) - e->cum_src[first];
        positive size;

        if (!zstd_entropy_chunk(e, e->seqs + first, null, stop - first, e->lits + lit_from,
                                lit_to - lit_from, source, false, e->trial_out,
                                address_of size, address_of e->split_state))
        {
                address_to compressed = false;
                return source + 3;
        }
        address_to compressed = true;
        return size;
}

static fn zstd_snapshot_take(zstd_encoder address_to e)
{
        memory_copy_apart(e->snapshot.prior, e->prior, sizeof(e->prior));
        memory_copy_apart(e->snapshot.huf_table, e->huf_table, sizeof(e->huf_table));
        memory_copy_apart(e->snapshot.huf_length, e->huf_length, sizeof(e->huf_length));
        e->snapshot.huf_valid = e->huf_valid;
}

static fn zstd_snapshot_put(zstd_encoder address_to e)
{
        memory_copy_apart(e->prior, e->snapshot.prior, sizeof(e->prior));
        memory_copy_apart(e->huf_table, e->snapshot.huf_table, sizeof(e->huf_table));
        memory_copy_apart(e->huf_length, e->snapshot.huf_length, sizeof(e->huf_length));
        e->huf_valid = e->snapshot.huf_valid;
}

static fn zstd_split_derive(zstd_encoder address_to e, positive first, positive stop,
                            positive whole, positive n, positive address_to cuts,
                            positive address_to count)
{
        positive const middle = (first + stop) / 2;
        positive left;
        positive right;
        bool compressed;
        bool ignored;

        if (stop - first < ZSTD_SPLIT_MIN_SEQUENCES || address_to count >= ZSTD_SPLIT_MAX)
                return;
        /* The second half is judged as it would be written, after the
           first: from the tables the first leaves. */
        left = zstd_split_cost(e, first, middle, n, address_of compressed);
        if (compressed)
        {
                zstd_snapshot_take(e);
                zstd_entropy_commit(e, address_of e->split_state);
        }
        right = zstd_split_cost(e, middle, stop, n, address_of ignored);
        if (compressed)
                zstd_snapshot_put(e);
        if (left + right >= whole)
                return;
        zstd_split_derive(e, first, middle, left, n, cuts, count);
        cuts[address_to count] = middle;
        address_to count += 1;
        zstd_split_derive(e, middle, stop, right, n, cuts, count);
}

/* The parsed block as one or several: same answers as zstd_entropy_block. */
static b32 zstd_split_sequences(zstd_encoder address_to e, positive n, bool last)
{
        positive const nseq = e->nseq;
        positive cuts[ZSTD_SPLIT_MAX + 1];
        positive count = 0;
        positive lit_sum = 0;
        positive src_sum = 0;
        positive at = 0;

        if (nseq < 2 * ZSTD_SPLIT_MIN_SEQUENCES)
                return zstd_entropy_block(e, n, last);
        for (positive i = 0; i < nseq; i++)
        {
                e->cum_lit[i] = (p32)lit_sum;
                e->cum_src[i] = (p32)src_sum;
                lit_sum += e->seqs[i].lit;
                src_sum += e->seqs[i].lit + e->seqs[i].match;
        }
        e->cum_lit[nseq] = (p32)lit_sum;
        e->cum_src[nseq] = (p32)src_sum;
        {
                bool whole_ok;
                positive const whole = zstd_split_cost(e, 0, nseq, n, address_of whole_ok);

                zstd_split_derive(e, 0, nseq, whole, n, cuts, address_of count);
        }
        if (!count)
                return zstd_entropy_block(e, n, last);
        cuts[count] = nseq;
        zstd_snapshot_take(e);
        for (positive piece = 0; piece <= count; piece++)
        {
                positive const first = piece ? cuts[piece - 1] : 0;
                positive const stop = cuts[piece];
                positive const lit_from = e->cum_lit[first];
                positive const lit_to = stop == nseq ? e->nlit : e->cum_lit[stop];
                positive const source = (stop == nseq ? n : e->cum_src[stop]) - e->cum_src[first];
                positive size;

                if (!zstd_entropy_chunk(e, e->seqs + first, null, stop - first, e->lits + lit_from,
                                        lit_to - lit_from, source, last && stop == nseq,
                                        e->split_out + at, address_of size,
                                        address_of e->split_state))
                {
                        zstd_snapshot_put(e);
                        return zstd_entropy_block(e, n, last);
                }
                zstd_entropy_commit(e, address_of e->split_state);
                at += size;
        }
        return zstd_enc_emit(e, e->split_out, at) ? 1 : -1;
}

/* A hash of the first `bytes` (4 to 8) bytes at p, `log` bits wide.  Eight
   bytes at p are always readable where a parser hashes. */
static __attribute__((always_inline)) inline positive
zstd_hash_bytes(p8 address_to p, p8 log, p8 bytes)
{
        return (positive)(((memory_load_unaligned(p64, p) << (64 - 8 * bytes)) *
                           0x9E3779B185EBCA87ull) >> (64 - log));
}

/*
        One sequence: the literals since the last match, then a match
        `distance` back.  The offset value follows RFC 8878's repeat
        offsets: with literals 1-3 name rep[0-2]; without, 1-2 name rep[1-2]
        and 3 is rep[0] - 1.  Sixteen literal bytes always copy; the literal
        buffer and the window both carry the slack.
*/
static __attribute__((always_inline)) inline fn
zstd_store(zstd_encoder address_to e, p8 address_to literals, positive run,
           positive distance, positive match)
{
        zstd_enc_seq address_to const s = e->seqs + e->nseq++;
        p8 address_to const into = e->lits + e->nlit;
        p32 value = (p32)distance + 3;
        positive which = 3;

        memory_store_unaligned(p64, into, memory_load_unaligned(p64, literals));
        memory_store_unaligned(p64, into + 8, memory_load_unaligned(p64, literals + 8));
        if (run > 16)
                memory_copy_apart(into + 16, literals + 16, run - 16);
        e->nlit += run;
        if (run && distance == e->rep[0])
                value = 1, which = 0;
        else if (distance == e->rep[1])
                value = run ? 2 : 1, which = 1;
        else if (distance == e->rep[2])
                value = run ? 3 : 2, which = 2;
        else if (!run && e->rep[0] > 1 && distance == e->rep[0] - 1)
                value = 3;
        if (which)
        {
                if (which != 1)
                        e->rep[2] = e->rep[1];
                e->rep[1] = e->rep[0];
                e->rep[0] = (p32)distance;
        }
        s->lit = (p32)run;
        s->match = (p32)match;
        s->off = value;
        s->ll_code = run < 64 ? zstd_ll_codes[run]
                              : (p8)(19 + zstd_highbit32((p32)run));
        s->ml_code = match < 131 ? zstd_ml_codes[match]
                                 : (p8)(36 + zstd_highbit32((p32)match - 3));
        s->of_code = zstd_highbit32(value);
        e->freq.ll[s->ll_code]++;
        e->freq.of[s->of_code]++;
        e->freq.ml[s->ml_code]++;
}

/* How far a and b agree, up to room. Most matches end within sixteen bytes
   of where they were last checked, so on x86-64 the first sixteen are
   compared here by one vector compare whose answer is a count of trailing
   zeros: no loop to leave and no branch taken until a match runs past
   sixteen, where the library takes over. An eight-byte compare with a
   branch on its result cost more in mispredicts than the call it saved
   (zstd -1 on text: 2.5M more branch misses, no fewer cycles), and a call
   costs a fifth of a fast level's cycles when matches average seven bytes.
   On arm64 it is two eight-byte compares, each a branch: moving a NEON
   compare's answer to a register costs more than the call did (an M2 Pro,
   matches of seven bytes on average, each answer deciding the next: the
   library 5.55 ns, the NEON compare 8.00, a branch-free pair of words
   5.85, two words and two branches 5.03). The other machines call the
   library. */
#if X64
typedef char zstd_v16 __attribute__((vector_size(16), aligned(1), may_alias));
#endif
static inline INLINE positive zstd_common(p8 address_to a, p8 address_to b, positive room)
{
#if X64
        if (room >= 16)
        {
                p32 equal = (p32)__builtin_ia32_pmovmskb128(
                        __builtin_ia32_pcmpeqb128(*(zstd_v16 *)a, *(zstd_v16 *)b));
                positive n = bottom_bit_known(~equal | 0x10000u);

                if (n < 16)
                        return n;
                return 16 + memory_common_prefix(a + 16, b + 16, room - 16);
        }
#elif ARM64
        if (room >= 16)
        {
                p64 x = memory_load_unaligned(p64, a) ^ memory_load_unaligned(p64, b);

                if (x)
                        return bottom_bit_known(x) >> 3;
                x = memory_load_unaligned(p64, a + 8) ^ memory_load_unaligned(p64, b + 8);
                if (x)
                        return 8 + (bottom_bit_known(x) >> 3);
                return 16 + memory_common_prefix(a + 16, b + 16, room - 16);
        }
#endif
        return memory_common_prefix(a, b, room);
}

/* Matches at the last repeat offset right where the last one ended, each a
   sequence with no literals, as long as they keep coming.  Answers the new
   position; hash, when present, takes each position a match starts at. */
static __attribute__((always_inline)) inline p8 address_to
zstd_repeat_run(zstd_encoder address_to e, p8 address_to ip,
                p8 address_to ilimit, p8 address_to iend, p32 low,
                p32 address_to hash, p8 hlog, p8 mls)
{
        while (ip < ilimit)
        {
                p32 const now = (p32)(ip - e->base);
                p32 const other = e->rep[1];
                positive match;

                if (!other || other > now - low ||
                    memory_load_unaligned(p32, ip - other) != memory_load_unaligned(p32, ip))
                        break;
                match = 4 + zstd_common(ip + 4, ip + 4 - other,
                                                 (positive)(iend - ip - 4));
                if (hash)
                        hash[zstd_hash_bytes(ip, hlog, mls)] = now;
                zstd_store(e, ip, 0, other, match);
                ip += match;
        }
        return ip;
}

/*
        fast, libzstd 1.5's, which lib.c's zstd_fast_parse runs: positions go
        in pairs, ip0 and ip1 adjacent, each hashed one step before its table
        slot is read and that slot's candidate one step before it is
        compared, so no load waits on the one before it.  A repeat is tried
        at ip2, the pair after.  Pairs start step apart, 2 (or --fast's
        target length + 1), and the step grows by one every 128 bytes without
        a match.  The routine takes the encoder's tables and arrays as a job
        and answers where the literals not yet stored begin.
*/
typedef struct
{
        p8 address_to base;
        p32 address_to table;
        zstd_enc_seq address_to seqs;
        p8 address_to lits;
        zstd_seq_freq address_to freq;
        positive from;
        positive to;
        positive low;
        positive hash_log;
        positive min_match;
        positive step;
        p32 address_to rep;
        p8 address_to anchor;
} zstd_fast_job;

_Static_assert(sizeof(zstd_fast_job) == 104 &&
               __builtin_offsetof(zstd_fast_job, seqs) == 16 &&
               __builtin_offsetof(zstd_fast_job, lits) == 24 &&
               __builtin_offsetof(zstd_fast_job, freq) == 32 &&
               __builtin_offsetof(zstd_fast_job, from) == 40 &&
               __builtin_offsetof(zstd_fast_job, hash_log) == 64 &&
               __builtin_offsetof(zstd_fast_job, rep) == 88 &&
               __builtin_offsetof(zstd_fast_job, anchor) == 96 &&
               __builtin_offsetof(zstd_seq_freq, of) == 144 &&
               __builtin_offsetof(zstd_seq_freq, ml) == 272,
               "the layouts lib.c's zstd_fast_parse reads");

static p8 address_to zstd_parse_fast(zstd_encoder address_to e, p32 from,
                                     p32 to, p32 low)
{
        zstd_fast_job job;

        job.base = e->base;
        job.table = e->hash;
        job.seqs = e->seqs + e->nseq;
        job.lits = e->lits + e->nlit;
        job.freq = address_of e->freq;
        job.from = from;
        job.to = to;
        job.low = low;
        job.hash_log = e->p.hash_log;
        job.min_match = e->p.min_match < 4 ? 4 : e->p.min_match > 8 ? 8 : e->p.min_match;
        job.step = (positive)e->p.target_length + !e->p.target_length + 1;
        job.rep = e->rep;
        zstd_fast_parse(address_of job);
        e->nseq = (positive)(job.seqs - e->seqs);
        e->nlit = (positive)(job.lits - e->lits);
        return job.anchor;
}

/*
        dfast, libzstd 1.5's, which lib.c's zstd_dfast_parse runs: an
        eight-byte hash and a min_match hash at each position, the long one
        of the next position read one step early; a repeat one byte on first,
        then the long match, then the short one, which a long match one byte
        on replaces only when it is longer.  The step starts at 1 and grows by
        one every 256 bytes without a match.  The job is the encoder's tables
        and arrays; the answer is where the literals not yet stored begin.
*/
typedef struct
{
        p8 address_to base;
        p32 address_to longer;
        p32 address_to shorter;
        zstd_enc_seq address_to seqs;
        p8 address_to lits;
        zstd_seq_freq address_to freq;
        positive from;
        positive to;
        positive low;
        positive hash_log;
        positive chain_log;
        positive min_match;
        p32 address_to rep;
        p8 address_to anchor;
} zstd_dfast_job;

_Static_assert(sizeof(zstd_dfast_job) == 112 &&
               __builtin_offsetof(zstd_dfast_job, shorter) == 16 &&
               __builtin_offsetof(zstd_dfast_job, seqs) == 24 &&
               __builtin_offsetof(zstd_dfast_job, freq) == 40 &&
               __builtin_offsetof(zstd_dfast_job, from) == 48 &&
               __builtin_offsetof(zstd_dfast_job, hash_log) == 72 &&
               __builtin_offsetof(zstd_dfast_job, rep) == 96 &&
               __builtin_offsetof(zstd_dfast_job, anchor) == 104,
               "the layouts lib.c's zstd_dfast_parse reads");

static p8 address_to zstd_parse_dfast(zstd_encoder address_to e, p32 from,
                                      p32 to, p32 low)
{
        zstd_dfast_job job;

        job.base = e->base;
        job.longer = e->hash;
        job.shorter = e->chain;
        job.seqs = e->seqs + e->nseq;
        job.lits = e->lits + e->nlit;
        job.freq = address_of e->freq;
        job.from = from;
        job.to = to;
        job.low = low;
        job.hash_log = e->p.hash_log;
        job.chain_log = e->p.chain_log;
        job.min_match = e->p.min_match < 4 ? 4 : e->p.min_match > 8 ? 8 : e->p.min_match;
        job.rep = e->rep;
        zstd_dfast_parse(address_of job);
        e->nseq = (positive)(job.seqs - e->seqs);
        e->nlit = (positive)(job.lits - e->lits);
        return job.anchor;
}

/*
        The row finder of greedy, lazy and lazy2, as libzstd's.  The hash
        table is rows of 2^row_log indices chosen by the hash's top bits; a
        byte tag from the next eight bits sits beside each index in chain,
        and a head per row (the byte before its tags) wraps downward, so a row keeps
        its latest positions with the newest at the head.  A search compares
        the row's tags a word at a time and walks, newest first, only the
        slots whose tag matched; the first one older than the window ends it.
*/
/* One bit a slot whose tag is tag.  x86-64 compares sixteen tags at a time
   (pcmpeqb, pmovmskb: three instructions a sixteen where the word-at-a-time
   form below is about thirty); elsewhere the exact zero-byte test on the row xor
   the tag, and a multiply that gathers a word's eight flags.  (On an M2 the
   vector compare and a multiply to gather it is a quarter faster alone and
   a seventh slower where the mask is waited for, so arm64 keeps the words.) */
static __attribute__((always_inline)) inline p64
zstd_row_mask(p8 address_to tags, p8 tag, positive entries)
{
#if X64
        zstd_v16 const pattern = {tag, tag, tag, tag, tag, tag, tag, tag,
                                  tag, tag, tag, tag, tag, tag, tag, tag};
        p64 mask = 0;

        for (positive at = 0; at < entries; at += 16)
                mask |= (p64)(p32)__builtin_ia32_pmovmskb128(
                                __builtin_ia32_pcmpeqb128(*(zstd_v16 address_to)(tags + at), pattern))
                        << at;
        return mask;
#else
        p64 const lows = 0x7f7f7f7f7f7f7f7full;
        p64 const pattern = 0x0101010101010101ull * tag;
        p64 mask = 0;

        for (positive at = 0; at < entries; at += 8)
        {
                p64 const x = memory_load_unaligned(p64, tags + at) ^ pattern;
                p64 const zero = ~(((x & lows) + lows) | x | lows);

                mask |= (((zero >> 7) * 0x0102040810204080ull) >> 56) << at;
        }
        return mask;
#endif
}

static __attribute__((always_inline)) inline fn
zstd_row_insert(zstd_encoder address_to e, p32 at, p8 row_log, p8 mls)
{
        positive const h = zstd_hash_bytes(e->base + at,
                                           (p8)(e->p.hash_log - row_log + 8), mls);
        positive const row = h >> 8;
        p8 address_to const cell = (p8 address_to)e->chain + (row << row_log) + row;
        positive const head = (positive)(cell[0] - 1) & (((positive)1 << row_log) - 1);

        cell[0] = (p8)head;
        cell[1 + head] = (p8)h;
        e->hash[(row << row_log) + head] = at;
}

/* Positions from at to stop into their rows, the tables and the width held
   in registers: the tag bytes are stores through a byte pointer, which the
   compiler must assume may change the table pointers it would otherwise
   read from the encoder again after every one. */
static __attribute__((always_inline)) inline fn
zstd_row_run(zstd_encoder address_to e, p32 at, p32 stop, p8 row_log, p8 mls)
{
        p8 address_to const base = e->base;
        p8 address_to const chain = (p8 address_to)e->chain;
        p32 address_to const hash = e->hash;
        p8 const hlog = (p8)(e->p.hash_log - row_log + 8);
        positive const mask = ((positive)1 << row_log) - 1;

        for (; at < stop; at++)
        {
                positive const h = zstd_hash_bytes(base + at, hlog, mls);
                positive const row = h >> 8;
                p8 address_to const cell = chain + (row << row_log) + row;
                positive const head = (positive)(cell[0] - 1) & mask;

                cell[0] = (p8)head;
                cell[1 + head] = (p8)h;
                hash[(row << row_log) + head] = at;
        }
}

/* The positions from e->next to target go in; after a long match only its
   first 96 and last 32 do, as libzstd skips them. */
static __attribute__((always_inline)) inline fn
zstd_row_update(zstd_encoder address_to e, p32 target, p32 low, p8 row_log, p8 mls)
{
        p32 at = e->next < low ? low : e->next;

        if (target > at && target - at > 384)
        {
                zstd_row_run(e, at, at + 96, row_log, mls);
                at = target - 32;
        }
        if (at < target)
                zstd_row_run(e, at, target, row_log, mls);
        if (e->next < target)
                e->next = target;
}

/*
        One search of the row finder, libzstd's shape: the row of ip's hash
        is read once, the candidates whose tag matches are gathered (each
        one's bytes fetched as it goes), ip itself goes into the row's
        newest slot from what is already in hand, so the next update has one
        position less to hash, and then the candidates are compared newest
        first, each by the four bytes that end at the best length so far.
        row_log and mls arrive as constants for the levels that have them
        (mls is 5 at every row level), so the row's width is a shift and the
        mask an unrolled compare.
*/
static __attribute__((always_inline)) inline positive
zstd_row_find(zstd_encoder address_to e, p8 address_to ip, p8 address_to iend,
              p32 low, positive address_to distance, bool skipping,
              const p8 row_log, const p8 mls)
{
        positive const entries = (positive)1 << row_log;
        p8 address_to const base = e->base;
        p8 address_to const chain = (p8 address_to)e->chain;
        p32 address_to const table = e->hash;
        p32 const cur = (p32)(ip - base);
        positive const room = (positive)(iend - ip);
        positive attempts = (positive)1 << (e->p.search_log < row_log ? e->p.search_log : row_log);
        positive best = 3;
        positive h;
        positive row;
        positive head;
        positive gathered = 0;
        positive found[64];
        p8 address_to cell;
        p32 address_to slots;
        p64 mask;

        if (skipping)
                e->next = cur;
        else
                zstd_row_update(e, cur, low, row_log, mls);
        h = zstd_hash_bytes(ip, (p8)(e->p.hash_log - row_log + 8), mls);
        row = h >> 8;
        /* The row a search eight bytes on will read is fetched now, as
           libzstd does, so the load it waits on is in cache by then. */
        if (ip + 16 <= iend)
        {
                positive const next = zstd_hash_bytes(ip + 8, (p8)(e->p.hash_log - row_log + 8), mls) >> 8;
                p8 address_to const ahead = chain + (next << row_log) + next;

                __builtin_prefetch(ahead);
                __builtin_prefetch(ahead + entries);
                __builtin_prefetch(table + (next << row_log));
                if (row_log > 4)
                        __builtin_prefetch(table + (next << row_log) + 16);
        }
        cell = chain + (row << row_log) + row;
        slots = table + (row << row_log);
        head = cell[0];
        mask = zstd_row_mask(cell + 1, (p8)h, entries);
        if (head)
                mask = (mask >> head) | (mask << (entries - head));
        if (entries < 64)
                mask &= ((p64)1 << entries) - 1;
        while (mask && attempts)
        {
                p32 const candidate = slots[(bottom_bit_known(mask) + head) & (entries - 1)];

                mask &= mask - 1;
                if (candidate < low)
                        break;
                if (candidate >= cur)
                        continue;
                __builtin_prefetch(base + candidate);
                found[gathered++] = candidate;
                attempts--;
        }
        {
                positive const at = (head - 1) & (entries - 1);

                cell[0] = (p8)at;
                cell[1 + at] = (p8)h;
                slots[at] = cur;
                e->next = cur + 1;
        }
        for (positive n = 0; n < gathered; n++)
        {
                p8 address_to const there = base + found[n];

                if (memory_load_unaligned(p32, there + best - 3) ==
                    memory_load_unaligned(p32, ip + best - 3))
                {
                        positive const length = zstd_common(ip, there, room);

                        if (length > best)
                        {
                                best = length;
                                address_to distance = cur - found[n];
                                if (length == room)
                                        break;
                        }
                }
        }
        return best > 3 ? best : 0;
}

/*
        The binary-tree levels.  Under each hash a tree orders the earlier
        positions by the bytes that follow them, smaller to the left, so one
        walk down it finds each longer match and puts the position in;
        chain holds two links an index, a cycle of 2^(chain_log - 1).
        btlazy2 hands the longest match to lazy2's parse.  btopt, btultra and
        btultra2 price every match and literal from the statistics of the
        sequences already chosen and keep the cheapest path, as libzstd does.
*/
static __attribute__((always_inline)) inline positive
zstd_hash3(p8 address_to p, p8 log)
{
        return (positive)(((p32)(memory_load_unaligned(p32, p) << 8) * 506832829u) >>
                          (32 - log));
}

static __attribute__((always_inline)) inline bool
zstd_same_start(p8 address_to a, p8 address_to b, positive bytes)
{
        p32 const mask = bytes == 3 ? 0xFFFFFFu : 0xFFFFFFFFu;

        return ((memory_load_unaligned(p32, a) ^ memory_load_unaligned(p32, b)) & mask) == 0;
}

/* Puts index cur into its tree; answers how far the caller may step before
   the next position goes in (a long repeat need not go in byte by byte). */
static p32 zstd_bt_insert(zstd_encoder address_to e, p8 address_to ip,
                          p8 address_to iend, p32 low, p8 mls)
{
        p8 address_to const base = e->base;
        p32 address_to const bt = e->chain;
        p32 const mask = ((p32)1 << (e->p.chain_log - 1)) - 1;
        p32 const cur = (p32)(ip - base);
        p32 const oldest = mask >= cur ? 0 : cur - mask;
        positive const h = zstd_hash_bytes(ip, e->p.hash_log, mls);
        positive const room = (positive)(iend - ip);
        p32 address_to smaller = bt + 2 * (cur & mask);
        p32 address_to larger = smaller + 1;
        p32 candidate = e->hash[h];
        p32 match_end = cur + 9;
        p32 dummy;
        positive best = 8;
        positive common_smaller = 0;
        positive common_larger = 0;
        positive compares = (positive)1 << e->p.search_log;

        e->hash[h] = cur;
        for (; compares && candidate >= low; compares--)
        {
                p32 address_to const next = bt + 2 * (candidate & mask);
                p8 address_to const match = base + candidate;
                positive length = common_smaller < common_larger ? common_smaller
                                                                 : common_larger;

                length += zstd_common(ip + length, match + length, room - length);
                if (length > best)
                {
                        best = length;
                        if (length > match_end - candidate)
                                match_end = candidate + (p32)length;
                }
                if (length == room)
                        break;
                if (match[length] < ip[length])
                {
                        address_to smaller = candidate;
                        common_smaller = length;
                        if (candidate <= oldest)
                        {
                                smaller = address_of dummy;
                                break;
                        }
                        smaller = next + 1;
                        candidate = next[1];
                }
                else
                {
                        address_to larger = candidate;
                        common_larger = length;
                        if (candidate <= oldest)
                        {
                                larger = address_of dummy;
                                break;
                        }
                        larger = next;
                        candidate = next[0];
                }
        }
        address_to smaller = 0;
        address_to larger = 0;
        {
                p32 const skip = best > 384 ? (p32)(best - 384 < 192 ? best - 384 : 192) : 0;
                p32 const reach = match_end - (cur + 8);

                return reach > skip ? reach : skip;
        }
}

/*
        Every match at ip longer than the one before it: the repeat offsets
        (rep[0] - 1 as a fourth after no literals), a three-byte hash when
        min_match is 3, then the tree's walk, which also puts ip in.  Each
        offset value is a repeat code (1-3) or the distance + 3.  Positions
        a long match already covered are not searched.
*/
static positive zstd_opt_matches(zstd_encoder address_to e, p8 address_to ip,
                                 p8 address_to iend, const p32 address_to rep,
                                 bool ll0, zstd_opt_match address_to matches)
{
        p8 address_to const base = e->base;
        p8 const mls = e->p.min_match < 3 ? 3 : e->p.min_match > 6 ? 6 : e->p.min_match;
        positive const min_match = mls == 3 ? 3 : 4;
        positive const sufficient = e->p.target_length < ZSTD_OPT_NUM - 1
                                        ? e->p.target_length
                                        : ZSTD_OPT_NUM - 1;
        p32 const cur = (p32)(ip - base);
        p32 const window = (p32)1 << e->p.window_log;
        p32 const low = cur - e->start > window ? cur - window : e->start;
        p32 address_to const bt = e->chain;
        p32 const mask = ((p32)1 << (e->p.chain_log - 1)) - 1;
        p32 const oldest = mask >= cur ? 0 : cur - mask;
        positive const room = (positive)(iend - ip);
        p32 address_to smaller;
        p32 address_to larger;
        p32 candidate;
        p32 match_end = cur + 9;
        p32 dummy;
        positive h;
        positive best = min_match - 1;
        positive count = 0;
        positive common_smaller = 0;
        positive common_larger = 0;
        positive compares = (positive)1 << e->p.search_log;

        if (cur < e->next)
                return 0;
        for (p32 at = e->next < low ? low : e->next; at < cur;)
                at += zstd_bt_insert(e, base + at, iend, low, mls < 4 ? 4 : mls);
        e->next = cur;

        for (positive code = ll0; code < 3 + (positive)ll0; code++)
        {
                p32 const offset = code == 3 ? rep[0] - 1 : rep[code];
                positive length = 0;

                if (offset - 1 < cur - e->start && cur - offset >= low &&
                    zstd_same_start(ip, ip - offset, min_match))
                        length = min_match +
                                 zstd_common(ip + min_match, ip + min_match - offset,
                                                      room - min_match);
                if (length > best)
                {
                        best = length;
                        matches[count].off = (p32)(code - ll0 + 1);
                        matches[count].len = (p32)length;
                        count++;
                        if (length > sufficient || length == room)
                                return count;
                }
        }
        if (mls == 3 && best < 3)
        {
                p32 at = e->next3 < low ? low : e->next3;
                p32 found;

                for (; at < cur; at++)
                        e->hash3[zstd_hash3(base + at, e->hash3_log)] = at;
                e->next3 = cur;
                found = e->hash3[zstd_hash3(ip, e->hash3_log)];
                if (found >= low && found < cur && cur - found < ((p32)1 << 18))
                {
                        positive const length = zstd_common(ip, base + found, room);

                        if (length >= 3)
                        {
                                best = length;
                                matches[0].off = cur - found + 3;
                                matches[0].len = (p32)length;
                                count = 1;
                                if (length > sufficient || length == room)
                                {
                                        e->next = cur + 1;
                                        return 1;
                                }
                        }
                }
        }
        /* The tree is keyed on four bytes when the minimum match is three:
           libzstd's hash of length three is the hash of length four, the
           three-byte matches being the hash3 table's. */
        h = zstd_hash_bytes(ip, e->p.hash_log, mls < 4 ? 4 : mls);
        candidate = e->hash[h];
        e->hash[h] = cur;
        smaller = bt + 2 * (cur & mask);
        larger = smaller + 1;
        for (; compares && candidate >= low; compares--)
        {
                p32 address_to const next = bt + 2 * (candidate & mask);
                p8 address_to const match = base + candidate;
                positive length = common_smaller < common_larger ? common_smaller
                                                                 : common_larger;

                length += zstd_common(ip + length, match + length, room - length);
                if (length > best)
                {
                        if (length > match_end - candidate)
                                match_end = candidate + (p32)length;
                        best = length;
                        matches[count].off = cur - candidate + 3;
                        matches[count].len = (p32)length;
                        count++;
                        if (length > ZSTD_OPT_NUM || length == room)
                                break;
                }
                if (match[length] < ip[length])
                {
                        address_to smaller = candidate;
                        common_smaller = length;
                        if (candidate <= oldest)
                        {
                                smaller = address_of dummy;
                                break;
                        }
                        smaller = next + 1;
                        candidate = next[1];
                }
                else
                {
                        address_to larger = candidate;
                        common_larger = length;
                        if (candidate <= oldest)
                        {
                                larger = address_of dummy;
                                break;
                        }
                        larger = next;
                        candidate = next[0];
                }
        }
        address_to smaller = 0;
        address_to larger = 0;
        e->next = match_end - 8;
        return count;
}

/*
        btlazy2's search, libzstd's DUBT: a position goes into its hash's
        chain unsorted (its second link marks it so), and is sorted into
        the tree only when a search meets it, at most 2^search_log a
        search, newest last.  Then the search walks the tree for ip and puts
        it in.  A longer match takes over only when its extra length pays
        for its extra offset bits, from a first offset worth 29 bits.
*/
#define ZSTD_DUBT_UNSORTED 1u

static fn zstd_dubt_sort(zstd_encoder address_to e, p32 cur, p8 address_to iend,
                         positive compares, p32 bt_low, p32 low)
{
        p8 address_to const base = e->base;
        p32 address_to const bt = e->chain;
        p32 const mask = ((p32)1 << (e->p.chain_log - 1)) - 1;
        p8 address_to const ip = base + cur;
        positive const room = (positive)(iend - ip);
        p32 address_to smaller = bt + 2 * (cur & mask);
        p32 address_to larger = smaller + 1;
        p32 candidate = address_to smaller;
        p32 dummy;
        positive common_smaller = 0;
        positive common_larger = 0;

        for (; compares && candidate > low; compares--)
        {
                p32 address_to const next = bt + 2 * (candidate & mask);
                p8 address_to const match = base + candidate;
                positive length = common_smaller < common_larger ? common_smaller
                                                                 : common_larger;

                length += zstd_common(ip + length, match + length, room - length);
                if (length == room)
                        break;
                if (match[length] < ip[length])
                {
                        address_to smaller = candidate;
                        common_smaller = length;
                        if (candidate <= bt_low)
                        {
                                smaller = address_of dummy;
                                break;
                        }
                        smaller = next + 1;
                        candidate = next[1];
                }
                else
                {
                        address_to larger = candidate;
                        common_larger = length;
                        if (candidate <= bt_low)
                        {
                                larger = address_of dummy;
                                break;
                        }
                        larger = next;
                        candidate = next[0];
                }
        }
        address_to smaller = 0;
        address_to larger = 0;
}

static positive zstd_bt_best(zstd_encoder address_to e, p8 address_to ip,
                             p8 address_to iend, positive address_to distance)
{
        p8 address_to const base = e->base;
        p8 const mls = e->p.min_match < 4 ? 4 : e->p.min_match > 6 ? 6 : e->p.min_match;
        p8 const hlog = e->p.hash_log;
        p32 address_to const hash = e->hash;
        p32 address_to const bt = e->chain;
        p32 const mask = ((p32)1 << (e->p.chain_log - 1)) - 1;
        p32 const cur = (p32)(ip - base);
        p32 const window = (p32)1 << e->p.window_log;
        p32 const low = cur - e->start > window ? cur - window : e->start;
        p32 const bt_low = mask >= cur ? 0 : cur - mask;
        p32 const unsort_limit = bt_low > low ? bt_low : low;
        positive const room = (positive)(iend - ip);
        positive compares = (positive)1 << e->p.search_log;
        positive candidates = compares;
        p32 previous = 0;
        p32 candidate;
        p32 address_to smaller;
        p32 address_to larger;
        p32 match_end = cur + 9;
        p32 dummy;
        p32 off_base = 999999999;
        positive best = 0;
        positive common_smaller = 0;
        positive common_larger = 0;
        positive h;

        if (cur < e->next)
                return 0;
        for (p32 at = e->next < e->start ? e->start : e->next; at < cur; at++)
        {
                positive const k = zstd_hash_bytes(base + at, hlog, mls);
                p32 address_to const node = bt + 2 * (at & mask);

                node[0] = hash[k];
                node[1] = ZSTD_DUBT_UNSORTED;
                hash[k] = at;
        }
        e->next = cur;

        h = zstd_hash_bytes(ip, hlog, mls);
        candidate = hash[h];
        /* The unsorted run from the head, its marks turned into a chain
           back up; a last one still unsorted is dropped. */
        while (candidate > unsort_limit && bt[2 * (candidate & mask) + 1] == ZSTD_DUBT_UNSORTED &&
               candidates > 1)
        {
                p32 address_to const node = bt + 2 * (candidate & mask);

                node[1] = previous;
                previous = candidate;
                candidate = node[0];
                candidates--;
        }
        if (candidate > unsort_limit && bt[2 * (candidate & mask) + 1] == ZSTD_DUBT_UNSORTED)
                bt[2 * (candidate & mask)] = bt[2 * (candidate & mask) + 1] = 0;
        for (candidate = previous; candidate;)
        {
                p32 const up = bt[2 * (candidate & mask) + 1];

                zstd_dubt_sort(e, candidate, iend, candidates, unsort_limit, low);
                candidate = up;
                candidates++;
        }

        candidate = hash[h];
        hash[h] = cur;
        smaller = bt + 2 * (cur & mask);
        larger = smaller + 1;
        for (; compares && candidate > low; compares--)
        {
                p32 address_to const next = bt + 2 * (candidate & mask);
                p8 address_to const match = base + candidate;
                positive length = common_smaller < common_larger ? common_smaller
                                                                 : common_larger;

                length += zstd_common(ip + length, match + length, room - length);
                if (length > best)
                {
                        if (length > match_end - candidate)
                                match_end = candidate + (p32)length;
                        if (4 * (b32)(length - best) >
                            (b32)zstd_highbit32(cur - candidate + 1) - (b32)zstd_highbit32(off_base))
                        {
                                best = length;
                                off_base = cur - candidate + 3;
                        }
                        if (length == room)
                                break;
                }
                if (match[length] < ip[length])
                {
                        address_to smaller = candidate;
                        common_smaller = length;
                        if (candidate <= bt_low)
                        {
                                smaller = address_of dummy;
                                break;
                        }
                        smaller = next + 1;
                        candidate = next[1];
                }
                else
                {
                        address_to larger = candidate;
                        common_larger = length;
                        if (candidate <= bt_low)
                        {
                                larger = address_of dummy;
                                break;
                        }
                        larger = next;
                        candidate = next[0];
                }
        }
        address_to smaller = 0;
        address_to larger = 0;
        e->next = match_end - 8;
        if (best < 4)
                return 0;
        address_to distance = off_base - 3;
        return best;
}

static __attribute__((always_inline)) inline p32 zstd_weight(p32 stat, bool fractional)
{
        p32 const s = stat + 1;
        p32 const high = zstd_highbit32(s);

        return high * 256 + (fractional ? (s << 8) >> high : 0);
}

static p32 zstd_price_downscale(p32 address_to table, positive count, p8 shift,
                                bool floor_one)
{
        p32 sum = 0;

        for (positive s = 0; s < count; s++)
        {
                table[s] = (floor_one ? 1 : table[s] > 0) + (table[s] >> shift);
                sum += table[s];
        }
        return sum;
}

static p32 zstd_price_scale(p32 address_to table, positive count, p8 log)
{
        p32 sum = 0;

        for (positive s = 0; s < count; s++)
                sum += table[s];
        if ((sum >> log) <= 1)
                return sum;
        return zstd_price_downscale(table, count, zstd_highbit32(sum >> log), true);
}

static fn zstd_price_bases(zstd_price address_to pr, bool fractional)
{
        pr->lit_base = zstd_weight(pr->lit_sum, fractional);
        pr->ll_base = zstd_weight(pr->ll_sum, fractional);
        pr->ml_base = zstd_weight(pr->ml_sum, fractional);
        pr->of_base = zstd_weight(pr->of_sum, fractional);
}

static fn zstd_price_ll_w(zstd_price address_to pr, positive c)
{
        pr->ll_w[c] = zstd_ll_extra[c] * 256 - zstd_weight(pr->ll[c], pr->fractional);
}

static fn zstd_price_ml_w(zstd_price address_to pr, positive c)
{
        pr->ml_w[c] = zstd_ml_extra[c] * 256 - zstd_weight(pr->ml[c], pr->fractional);
}

static fn zstd_price_of_w(zstd_price address_to pr, positive c)
{
        pr->of_w[c] = (p32)c * 256 - zstd_weight(pr->of[c], pr->fractional) +
                      (!pr->fractional && c >= 20 ? ((p32)c - 19) * 2 * 256 : 0);
}

/* At a block's start.  The frame's first block counts its own bytes for
   literals and takes libzstd's guesses for lengths and offset codes (and
   fixed prices when it is tiny); later blocks keep what the chosen
   sequences counted, scaled down. */
static fn zstd_price_begin(zstd_price address_to pr, p8 address_to src,
                           positive n, bool fractional)
{
        static const p8 ll_seed[36] = {4, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        static const p8 of_seed[32] = {6, 2, 1, 1, 2, 3, 4, 4, 4, 3, 2, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1};

        pr->predefined = false;
        if (!pr->ll_sum)
        {
                pr->predefined = n <= 8;
                memory_fill(pr->lit, 0, sizeof(pr->lit));
                for (positive i = 0; i < n; i++)
                        pr->lit[src[i]]++;
                pr->lit_sum = zstd_price_downscale(pr->lit, 256, 8, false);
                pr->ll_sum = 0;
                for (positive i = 0; i < 36; i++)
                        pr->ll_sum += pr->ll[i] = ll_seed[i];
                for (positive i = 0; i < 53; i++)
                        pr->ml[i] = 1;
                pr->ml_sum = 53;
                pr->of_sum = 0;
                for (positive i = 0; i < 32; i++)
                        pr->of_sum += pr->of[i] = of_seed[i];
        }
        else
        {
                pr->lit_sum = zstd_price_scale(pr->lit, 256, 12);
                pr->ll_sum = zstd_price_scale(pr->ll, 36, 11);
                pr->ml_sum = zstd_price_scale(pr->ml, 53, 11);
                pr->of_sum = zstd_price_scale(pr->of, 32, 11);
        }
        pr->fractional = fractional;
        for (positive i = 0; i < 256; i++)
                pr->lit_w[i] = zstd_weight(pr->lit[i], fractional);
        for (positive i = 0; i < 36; i++)
                zstd_price_ll_w(pr, i);
        for (positive i = 0; i < 53; i++)
                zstd_price_ml_w(pr, i);
        for (positive i = 0; i < 32; i++)
                zstd_price_of_w(pr, i);
        zstd_price_bases(pr, fractional);
}

static __attribute__((always_inline)) inline bipolar
zstd_price_literal(const zstd_price address_to pr, p8 byte)
{
        p32 weight;

        if (pr->predefined)
                return 6 * 256;
        weight = pr->lit_w[byte];
        if (weight > pr->lit_base - 256)
                weight = pr->lit_base - 256;
        return (bipolar)(pr->lit_base - weight);
}

static __attribute__((always_inline)) inline bipolar
zstd_price_litlen(const zstd_price address_to pr, positive length, bool fractional)
{
        bipolar over = 0;
        p8 code;

        if (pr->predefined)
                return zstd_weight((p32)length, fractional);
        //      A run past the block ceiling is priced as the longest run
        //      there is a code for, plus a bit. This used to call back into
        //      this function with that length, which is a recursion the
        //      compiler has to unwind before it can honour always_inline --
        //      it manages under the flags shipped today and refuses outright
        //      under others, with an error naming the attribute.
        if (length >= ZSTD_BLOCK_MAX)
        {
                over = 256;
                length = ZSTD_BLOCK_MAX - 1;
        }
        code = length < 64 ? zstd_ll_codes[length]
                           : (p8)(19 + zstd_highbit32((p32)length));
        return over + (bipolar)(pr->ll_w[code] + pr->ll_base);
}

static __attribute__((always_inline)) inline bipolar
zstd_price_match(const zstd_price address_to pr, p32 off_base, positive length,
                 bool fractional)
{
        p32 const code = zstd_highbit32(off_base);
        p8 ml;
        p32 price;

        if (pr->predefined)
                return zstd_weight((p32)length - 3, fractional) + (16 + code) * 256;
        price = pr->of_w[code] + pr->of_base;
        ml = length < 131 ? zstd_ml_codes[length]
                          : (p8)(36 + zstd_highbit32((p32)length - 3));
        price += pr->ml_w[ml] + pr->ml_base;
        return (bipolar)price + 256 / 5;
}

static fn zstd_price_update(zstd_price address_to pr, p8 address_to literals,
                            positive run, p32 off_base, positive match)
{
        positive const ll = run < 64 ? zstd_ll_codes[run] : 19 + zstd_highbit32((p32)run);
        positive const of = zstd_highbit32(off_base);
        positive const ml = match < 131 ? zstd_ml_codes[match] : 36 + zstd_highbit32((p32)match - 3);

        for (positive u = 0; u < run; u++)
        {
                pr->lit[literals[u]] += 2;
                pr->lit_w[literals[u]] = zstd_weight(pr->lit[literals[u]], pr->fractional);
        }
        pr->lit_sum += (p32)run * 2;
        pr->ll[ll]++;
        zstd_price_ll_w(pr, ll);
        pr->ll_sum++;
        pr->of[of]++;
        zstd_price_of_w(pr, of);
        pr->of_sum++;
        pr->ml[ml]++;
        zstd_price_ml_w(pr, ml);
        pr->ml_sum++;
}

/* The repeat offsets after offset value off_base, as RFC 8878 moves them;
   into may be rep. */
static __attribute__((always_inline)) inline fn
zstd_opt_rep(p32 address_to into, const p32 address_to rep, p32 off_base, bool ll0)
{
        p32 const r0 = rep[0], r1 = rep[1], r2 = rep[2];
        p32 const code = off_base - 1 + (p32)ll0;

        if (off_base > 3)
        {
                into[0] = off_base - 3;
                into[1] = r0;
                into[2] = r1;
        }
        else if (!code)
        {
                into[0] = r0;
                into[1] = r1;
                into[2] = r2;
        }
        else
        {
                into[0] = code == 3 ? r0 - 1 : code == 1 ? r1 : r2;
                into[1] = r0;
                into[2] = code >= 2 ? r1 : r2;
        }
}

static __attribute__((always_inline)) inline p32
zstd_opt_distance(const p32 address_to rep, p32 off_base, bool ll0)
{
        p32 const code = off_base - 1 + (p32)ll0;

        return off_base > 3 ? off_base - 3 : code == 3 ? rep[0] - 1 : rep[code];
}

/*
        libzstd's optimal parse.  From each position with a match, prices go
        forward: every length of every match found, and one more literal at
        each position, keep the cheapest way to reach each position up to
        the longest match (at most ZSTD_OPT_NUM on).  Then the path goes
        back from the end and its sequences are stored.  A match longer than
        the target length is taken at once.  ultra is btultra's precision:
        fractional bit costs, no penalty on far offsets, and a match that
        one literal later would cost less; without it positions and lengths
        that cannot win are skipped.
*/
static p8 address_to zstd_parse_opt(zstd_encoder address_to e, p32 from, p32 to,
                                    bool ultra)
{
        p8 address_to const base = e->base;
        p8 address_to const iend = base + to;
        p8 address_to const ilimit = iend - 8;
        zstd_price address_to const pr = address_of e->price;
        zstd_opt_node address_to const opt = e->opt;
        zstd_opt_match address_to const matches = e->matches;
        positive const sufficient = e->p.target_length < ZSTD_OPT_NUM - 1
                                        ? e->p.target_length
                                        : ZSTD_OPT_NUM - 1;
        positive const min_match = e->p.min_match == 3 ? 3 : 4;
        p8 address_to ip = base + from;
        p8 address_to anchor = ip;
        p32 rep[3] = {e->rep[0], e->rep[1], e->rep[2]};

        zstd_price_begin(pr, ip, to - from, ultra);
        ip += ip == base + e->start;
        while (ip < ilimit)
        {
                p32 const segment[3] = {rep[0], rep[1], rep[2]};
                positive cur;
                positive last_pos = 0;
                zstd_opt_node last;

                {
                        positive const litlen = (positive)(ip - anchor);
                        positive const count = zstd_opt_matches(e, ip, iend, rep, !litlen, matches);
                        positive pos;

                        if (!count)
                        {
                                ip++;
                                continue;
                        }
                        opt[0].mlen = 0;
                        opt[0].litlen = (p32)litlen;
                        opt[0].price = zstd_price_litlen(pr, litlen, ultra);
                        memory_copy_apart(opt[0].rep, rep, sizeof(rep));
                        if (matches[count - 1].len > sufficient)
                        {
                                last.litlen = 0;
                                last.mlen = matches[count - 1].len;
                                last.off = matches[count - 1].off;
                                cur = 0;
                                last_pos = last.mlen;
                                goto shortest;
                        }
                        for (pos = 1; pos < min_match; pos++)
                        {
                                opt[pos].price = ZSTD_PRICE_MAX;
                                opt[pos].mlen = 0;
                                opt[pos].litlen = (p32)(litlen + pos);
                        }
                        for (positive n = 0; n < count; n++)
                        {
                                p32 const off = matches[n].off;
                                p32 const reach = matches[n].len;

                                for (; pos <= reach; pos++)
                                {
                                        opt[pos].mlen = (p32)pos;
                                        opt[pos].off = off;
                                        opt[pos].litlen = 0;
                                        opt[pos].price = opt[0].price +
                                                         zstd_price_match(pr, off, pos, ultra) +
                                                         zstd_price_litlen(pr, 0, ultra);
                                }
                        }
                        last_pos = pos - 1;
                        opt[pos].price = ZSTD_PRICE_MAX;
                }
                for (cur = 1; cur <= last_pos; cur++)
                {
                        p8 address_to const inr = ip + cur;

                        {
                                positive const litlen = opt[cur - 1].litlen + 1;
                                bipolar const price = opt[cur - 1].price +
                                                      zstd_price_literal(pr, ip[cur - 1]) +
                                                      zstd_price_litlen(pr, litlen, ultra) -
                                                      zstd_price_litlen(pr, litlen - 1, ultra);

                                if (price <= opt[cur].price)
                                {
                                        zstd_opt_node const previous = opt[cur];

                                        opt[cur] = opt[cur - 1];
                                        opt[cur].litlen = (p32)litlen;
                                        opt[cur].price = price;
                                        if (ultra && previous.litlen == 0 &&
                                            zstd_price_litlen(pr, 1, ultra) < zstd_price_litlen(pr, 0, ultra) &&
                                            inr < iend)
                                        {
                                                bipolar const with1 = previous.price +
                                                    zstd_price_literal(pr, ip[cur]) +
                                                    zstd_price_litlen(pr, 1, ultra) -
                                                    zstd_price_litlen(pr, 0, ultra);
                                                bipolar const more = price +
                                                    zstd_price_literal(pr, ip[cur]) +
                                                    zstd_price_litlen(pr, litlen + 1, ultra) -
                                                    zstd_price_litlen(pr, litlen, ultra);

                                                if (with1 < more && with1 < opt[cur + 1].price)
                                                {
                                                        positive const prev = cur - previous.mlen;

                                                        opt[cur + 1] = previous;
                                                        zstd_opt_rep(opt[cur + 1].rep, opt[prev].rep,
                                                                     previous.off, opt[prev].litlen == 0);
                                                        opt[cur + 1].litlen = 1;
                                                        opt[cur + 1].price = with1;
                                                        if (last_pos < cur + 1)
                                                                last_pos = cur + 1;
                                                }
                                        }
                                }
                        }
                        if (opt[cur].litlen == 0)
                        {
                                positive const prev = cur - opt[cur].mlen;

                                zstd_opt_rep(opt[cur].rep, opt[prev].rep, opt[cur].off,
                                             opt[prev].litlen == 0);
                        }
                        if (inr > ilimit)
                                continue;
                        if (cur == last_pos)
                                break;
                        if (!ultra && opt[cur + 1].price <= opt[cur].price + 128)
                                continue;
                        {
                                bool const ll0 = opt[cur].litlen == 0;
                                bipolar const base_price = opt[cur].price +
                                                           zstd_price_litlen(pr, 0, ultra);
                                positive const count = zstd_opt_matches(e, inr, iend, opt[cur].rep,
                                                                        ll0, matches);

                                if (!count)
                                        continue;
                                {
                                        positive const longest = matches[count - 1].len;

                                        if (longest > sufficient || cur + longest >= ZSTD_OPT_NUM ||
                                            inr + longest >= iend)
                                        {
                                                last.mlen = (p32)longest;
                                                last.off = matches[count - 1].off;
                                                last.litlen = 0;
                                                last_pos = cur + longest;
                                                goto shortest;
                                        }
                                }
                                for (positive n = 0; n < count; n++)
                                {
                                        p32 const off = matches[n].off;
                                        positive const first = n ? matches[n - 1].len + 1 : min_match;

                                        for (positive length = matches[n].len; length >= first; length--)
                                        {
                                                positive const pos = cur + length;
                                                bipolar const price = base_price +
                                                                      zstd_price_match(pr, off, length, ultra);

                                                if (pos > last_pos || price < opt[pos].price)
                                                {
                                                        while (last_pos < pos)
                                                        {
                                                                last_pos++;
                                                                opt[last_pos].price = ZSTD_PRICE_MAX;
                                                                opt[last_pos].litlen = 1;
                                                        }
                                                        opt[pos].mlen = (p32)length;
                                                        opt[pos].off = off;
                                                        opt[pos].litlen = 0;
                                                        opt[pos].price = price;
                                                }
                                                else if (!ultra)
                                                        break;
                                        }
                                }
                        }
                        opt[last_pos + 1].price = ZSTD_PRICE_MAX;
                }
                last = opt[last_pos];
                cur = last_pos - last.mlen;
        shortest:
                if (!last.mlen)
                {
                        ip += last_pos;
                        continue;
                }
                if (!last.litlen)
                        zstd_opt_rep(rep, opt[cur].rep, last.off, opt[cur].litlen == 0);
                else
                {
                        memory_copy_apart(rep, last.rep, sizeof(rep));
                        cur -= last.litlen;
                }
                {
                        positive const store_end = cur + 2;
                        positive store_start;
                        positive stretch = cur;
                        p32 path[3] = {segment[0], segment[1], segment[2]};

                        if (last.litlen)
                        {
                                opt[store_end].litlen = last.litlen;
                                opt[store_end].mlen = 0;
                                store_start = store_end - 1;
                                opt[store_start] = last;
                        }
                        else
                        {
                                store_start = store_end;
                                opt[store_end] = last;
                        }
                        for (;;)
                        {
                                zstd_opt_node const step = opt[stretch];

                                opt[store_start].litlen = step.litlen;
                                if (!step.mlen)
                                        break;
                                store_start--;
                                opt[store_start] = step;
                                stretch -= step.litlen + step.mlen;
                        }
                        for (positive at = store_start; at <= store_end; at++)
                        {
                                positive const run = opt[at].litlen;
                                positive const match = opt[at].mlen;
                                p32 const off = opt[at].off;

                                if (!match)
                                {
                                        ip = anchor + run;
                                        continue;
                                }
                                zstd_price_update(pr, anchor, run, off, match);
                                zstd_store(e, anchor, run, zstd_opt_distance(path, off, !run), match);
                                zstd_opt_rep(path, path, off, !run);
                                anchor += run + match;
                                ip = anchor;
                        }
                        zstd_price_bases(pr, ultra);
                }
        }
        return anchor;
}

/* The bits an offset value costs, near enough to weigh one match against
   another: none for the last offset repeated. */
static __attribute__((always_inline)) inline bipolar
zstd_offset_cost(zstd_encoder address_to e, positive distance)
{
        return distance == e->rep[0] ? 0
                                     : (bipolar)zstd_highbit32((p32)distance + 3);
}

/*
        greedy (depth 0), lazy (1) and lazy2 (2), libzstd's parse: a repeat
        one byte ahead, the chain's best, then up to depth positions further
        on, each taking over when its gain (length against offset bits)
        beats the match in hand.  A match found at an offset extends back
        into the literals before it.
*/
static __attribute__((always_inline)) inline p8 address_to
zstd_parse_lazy_body(zstd_encoder address_to e, p32 from, p32 to, p32 low,
                     const positive depth, const bool tree, const p8 row_log,
                     const p8 mls)
{
        p8 address_to const base = e->base;
        p8 address_to const lowest = base + low;
        p8 address_to const iend = base + to;
        p8 address_to const ilimit = iend - 8;
        p8 address_to ip = base + from;
        p8 address_to anchor = ip;
        bool skipping = false;

        ip += ip == lowest;
        while (ip < ilimit)
        {
                p32 cur = (p32)(ip - base);
                p32 rep = e->rep[0];
                positive match = 0;
                positive distance = 0;
                positive found;
                positive found_distance = 0;
                p8 address_to start = ip + 1;

                if (rep && rep <= cur + 1 - low &&
                    memory_load_unaligned(p32, ip + 1) == memory_load_unaligned(p32, ip + 1 - rep))
                {
                        match = 4 + zstd_common(ip + 5, ip + 5 - rep,
                                                         (positive)(iend - ip - 5));
                        distance = rep;
                        if (!depth)
                                goto store;
                }
                found = tree ? zstd_bt_best(e, ip, iend, address_of found_distance)
                             : zstd_row_find(e, ip, iend, low, address_of found_distance, skipping,
                                                           row_log, mls);
                if (found > match)
                {
                        match = found;
                        distance = found_distance;
                        start = ip;
                }
                if (match < 4)
                {
                        positive const step = ((positive)(ip - anchor) >> 8) + 1;

                        ip += step;
                        skipping = step > 8;
                        continue;
                }
                while (depth && ip < ilimit)
                {
                        positive length;

                        ip++;
                        cur++;
                        rep = e->rep[0];
                        if (rep && rep <= cur - low &&
                            memory_load_unaligned(p32, ip) == memory_load_unaligned(p32, ip - rep))
                        {
                                length = 4 + zstd_common(ip + 4, ip + 4 - rep,
                                                                  (positive)(iend - ip - 4));
                                if ((bipolar)length * 3 >
                                    (bipolar)match * 3 - zstd_offset_cost(e, distance) + 1)
                                {
                                        match = length;
                                        distance = rep;
                                        start = ip;
                                }
                        }
                        found = tree ? zstd_bt_best(e, ip, iend, address_of found_distance)
                             : zstd_row_find(e, ip, iend, low, address_of found_distance, skipping,
                                                           row_log, mls);
                        if (found >= 4 &&
                            (bipolar)found * 4 - (bipolar)zstd_highbit32((p32)found_distance + 3) >
                                (bipolar)match * 4 - zstd_offset_cost(e, distance) + 4)
                        {
                                match = found;
                                distance = found_distance;
                                start = ip;
                                continue;
                        }
                        if (depth == 2 && ip < ilimit)
                        {
                                ip++;
                                cur++;
                                rep = e->rep[0];
                                if (rep && rep <= cur - low &&
                                    memory_load_unaligned(p32, ip) == memory_load_unaligned(p32, ip - rep))
                                {
                                        length = 4 + zstd_common(ip + 4, ip + 4 - rep,
                                                                          (positive)(iend - ip - 4));
                                        if ((bipolar)length * 4 >
                                            (bipolar)match * 4 - zstd_offset_cost(e, distance) + 1)
                                        {
                                                match = length;
                                                distance = rep;
                                                start = ip;
                                        }
                                }
                                found = tree ? zstd_bt_best(e, ip, iend, address_of found_distance)
                             : zstd_row_find(e, ip, iend, low, address_of found_distance, skipping,
                                                           row_log, mls);
                                if (found >= 4 &&
                                    (bipolar)found * 4 - (bipolar)zstd_highbit32((p32)found_distance + 3) >
                                        (bipolar)match * 4 - zstd_offset_cost(e, distance) + 7)
                                {
                                        match = found;
                                        distance = found_distance;
                                        start = ip;
                                        continue;
                                }
                        }
                        break;
                }
                while (start > anchor && (positive)(start - lowest) > distance &&
                       start[-1] == start[-1 - (bipolar)distance])
                        start--, match++;
        store:
                zstd_store(e, anchor, (positive)(start - anchor), distance, match);
                skipping = false;
                ip = zstd_repeat_run(e, start + match, ilimit, iend, low, null, 0, 0);
                anchor = ip;
        }
        return anchor;
}

/* The row levels all have min_match 5 and a row of 16, 32 or 64 slots, and
   the depth is fixed by the strategy: each combination is its own copy of
   the parse with those as constants.  The tree parse and any other shape
   take the general copy. */
#define ZSTD_LAZY_ROWS(depth)                                                         \
        switch (zstd_row_log(address_of e->p))                                        \
        {                                                                             \
        case 4:                                                                       \
                return zstd_parse_lazy_body(e, from, to, low, depth, false, 4, 5);   \
        case 5:                                                                       \
                return zstd_parse_lazy_body(e, from, to, low, depth, false, 5, 5);   \
        default:                                                                      \
                return zstd_parse_lazy_body(e, from, to, low, depth, false, 6, 5);   \
        }

static p8 address_to zstd_parse_lazy(zstd_encoder address_to e, p32 from,
                                     p32 to, p32 low, positive depth, bool tree)
{
        if (tree)
                return zstd_parse_lazy_body(e, from, to, low, 2, true, 4, 5);
        if (e->p.min_match != 5)
                return zstd_parse_lazy_body(e, from, to, low, depth, false,
                                            zstd_row_log(address_of e->p),
                                            e->p.min_match < 4 ? 4 : e->p.min_match > 6 ? 6 : e->p.min_match);
        if (depth == 0)
        {
                ZSTD_LAZY_ROWS(0)
        }
        if (depth == 1)
        {
                ZSTD_LAZY_ROWS(1)
        }
        ZSTD_LAZY_ROWS(2)
}
#undef ZSTD_LAZY_ROWS

static fn zstd_encoder_close(zstd_encoder address_to e)
{
        if (e->storage)
                memory_free(e->storage, e->storage_room);
        if (e->hash)
                memory_free(e->hash, e->hash_bytes);
        if (e->chain)
                memory_free(e->chain, e->chain_bytes);
        if (e->hash3)
                memory_free(e->hash3, e->hash3_bytes);
        e->storage = null;
        e->hash = null;
        e->chain = null;
        e->hash3 = null;
        e->storage_room = 0;
        e->hash_bytes = 0;
        e->chain_bytes = 0;
        e->hash3_bytes = 0;
        e->filled = 0;
}

/* fast and dfast take positions only as they parse, so a job's history
   goes into their tables first, every third position, as libzstd loads a
   prefix. */
static fn zstd_encoder_prefix(zstd_encoder address_to e, p32 from, p32 to, p32 step)
{
        p8 const mls = e->p.min_match < 4 ? 4 : e->p.min_match > 8 ? 8 : e->p.min_match;

        for (p32 at = from; at < to; at += step)
        {
                p8 address_to const p = e->base + at;

                if (e->p.strategy == ZSTD_DFAST)
                {
                        e->hash[zstd_hash_bytes(p, e->p.hash_log, 8)] = at;
                        e->chain[zstd_hash_bytes(p, e->p.chain_log, mls)] = at;
                }
                else
                        e->hash[zstd_hash_bytes(p, e->p.hash_log, mls)] = at;
        }
}

/*
        A dictionary in the encoder: the last window's worth of its content
        goes before the first block as history that is already there, every
        position of it into the match finder's tables (the binary trees take
        theirs as the first searches walk over them), and the state before
        the first block is the dictionary's: its repeat offsets, its Huffman
        table and its three sequence tables as the ones a first block may
        repeat.
*/
static positive zstd_dictionary_kept(p8 window_log)
{
        positive const window = (positive)1 << window_log;

        return zstd_dict_on ? (zstd_dict_size < window ? zstd_dict_size : window) : 0;
}

static fn zstd_encoder_dictionary_state(zstd_encoder address_to e)
{
        e->rep[0] = zstd_dict_rep[0];
        e->rep[1] = zstd_dict_rep[1];
        e->rep[2] = zstd_dict_rep[2];
        if (!zstd_dict_entropy)
                return;
        {
                zstd_huff address_to const huff = address_of zstd_dict_huff;

                zstd_huffman_codes(e->huf_table, huff->weight, huff->weights, huff->real_bits);
                for (positive s = 0; s < 256; s++)
                        e->huf_length[s] = s < huff->weights && huff->weight[s]
                                               ? (p8)(huff->real_bits + 1 - huff->weight[s])
                                               : 0;
                e->huf_valid = true;
        }
        for (positive k = 0; k < 3; k++)
        {
                zstd_fse_prior address_to const prior = e->prior + k;

                memory_fill(prior->norm, 0, sizeof(prior->norm));
                for (positive s = 0; s <= zstd_dict_symbols[k] && s < 53; s++)
                        prior->norm[s] = zstd_dict_norm[k][s];
                prior->mode = 2;
                prior->symbol = 0;
                prior->valid = zstd_ctable_build(address_of prior->table, zstd_dict_norm[k],
                                                 zstd_dict_symbols[k], zstd_dict_log[k]);
        }
}

/* The tables' positions from first to stop: every one for fast and dfast,
   the rows of the row finder; the trees fill as they are searched. */
static fn zstd_encoder_dictionary_tables(zstd_encoder address_to e, p32 first, p32 stop)
{
        if (stop <= first + 8)
                return;
        if (e->p.strategy <= ZSTD_DFAST)
                zstd_encoder_prefix(e, first, stop - 8, 1);
        else if (e->p.strategy <= ZSTD_LAZY2)
        {
                p8 const row_log = zstd_row_log(address_of e->p);
                p8 const mls = e->p.min_match < 4 ? 4 : e->p.min_match > 6 ? 6 : e->p.min_match;

                for (p32 at = first; at < stop - 8; at++)
                        zstd_row_insert(e, at, row_log, mls);
                e->next = stop - 8;
        }
}

/*
        The window and tables for these parameters, kept from the last frame
        when they are the same size.  A frame's indices start a window past
        the last frame's end, so whatever the tables still hold from it is
        below every window and never matched; near 2^30 they start over.
*/
static bool zstd_encoder_open(zstd_encoder address_to e,
                              const zstd_params address_to p, bool checksum)
{
        positive const window = (positive)1 << p->window_log;
        positive const room = 2 * window + 2 * ZSTD_BLOCK_MAX + 64;
        positive const hash_bytes = (positive)4 << p->hash_log;
        positive const chain_bytes = zstd_chain_bytes(p);
        p8 const hash3_log = p->window_log < 17 ? p->window_log : 17;
        positive const hash3_bytes = p->strategy >= ZSTD_BTOPT && p->min_match == 3
                                         ? (positive)4 << hash3_log
                                         : 0;
        p32 start;

        if (e->storage_room < room || e->hash_bytes != hash_bytes ||
            e->chain_bytes != chain_bytes || e->hash3_bytes != hash3_bytes)
        {
                zstd_encoder_close(e);
                if (!(e->storage = memory_checked(room)))
                        return zstd_fail("zstd cannot map its window");
                e->storage_room = room;
                if (!(e->hash = memory_checked(hash_bytes)))
                        return zstd_encoder_close(e), zstd_fail("zstd cannot map its tables");
                e->hash_bytes = hash_bytes;
                if (chain_bytes && !(e->chain = memory_checked(chain_bytes)))
                        return zstd_encoder_close(e), zstd_fail("zstd cannot map its tables");
                e->chain_bytes = chain_bytes;
                if (hash3_bytes && !(e->hash3 = memory_checked(hash3_bytes)))
                        return zstd_encoder_close(e), zstd_fail("zstd cannot map its tables");
                e->hash3_bytes = hash3_bytes;
        }
        start = e->filled ? e->filled + (p32)window + 1 : 1;
        if (start > 0x40000000u)
        {
                memory_fill(e->hash, 0, hash_bytes);
                if (chain_bytes)
                        memory_fill(e->chain, 0, chain_bytes);
                if (hash3_bytes)
                        memory_fill(e->hash3, 0, hash3_bytes);
                start = 1;
        }
        e->hash3_log = hash3_log;
        e->next3 = start;
        memory_fill(address_of e->price, 0, sizeof(e->price));
        e->p = address_to p;
        e->checksum = checksum;
        e->start = start;
        e->block = start;
        e->filled = start;
        e->next = start;
        e->storage_index = start;
        e->base = e->storage - start;
        e->rep[0] = 1;
        e->rep[1] = 4;
        e->rep[2] = 8;
        e->savings = 0;
        e->nseq = 0;
        e->nlit = 0;
        e->huf_valid = false;
        e->prior[0].valid = false;
        e->prior[1].valid = false;
        e->prior[2].valid = false;
        e->output = null;
        if (!e->seqs)
        {
                e->seqs = zstd_seqs;
                e->lits = zstd_enc_lits;
                e->bits = zstd_enc_bits;
                e->block_out = zstd_enc_block_out;
                e->trial_out = zstd_enc_trial_out;
                e->split_out = zstd_enc_split_out;
                e->cum_lit = zstd_enc_cum_lit;
                e->cum_src = zstd_enc_cum_src;
                e->packed = zstd_packed_lits;
                e->lit_table_new = zstd_lit_table_new;
                e->lit_length_new = zstd_lit_length_new;
                e->opt = zstd_opt;
                e->matches = zstd_matches;
        }
        if (zstd_dict_on)
        {
                positive const keep = zstd_dictionary_kept(p->window_log);

                memory_copy(e->storage, zstd_dict_content + zstd_dict_size - keep, keep);
                e->filled = start + (p32)keep;
                e->block = start + (p32)keep;
                zstd_encoder_dictionary_state(e);
                zstd_encoder_dictionary_tables(e, start, start + (p32)keep);
        }
        return true;
}

/* Keep one window of history before the block being filled. */
static fn zstd_encoder_slide(zstd_encoder address_to e)
{
        p32 const window = (p32)1 << e->p.window_log;
        p32 const keep = e->block - e->storage_index > window ? e->block - window
                                                             : e->storage_index;

        if (keep == e->storage_index)
                return;
        memory_copy(e->storage, e->base + keep, e->filled - keep);
        e->storage_index = keep;
        e->base = e->storage - keep;
}

/* Before indices reach 2^32, move every index down by a whole number of
   chain cycles, so each chain slot stays its slot; what falls below the
   history kept becomes 0, which no window reaches. */
static fn zstd_encoder_reduce(zstd_encoder address_to e)
{
        p32 const unit = (p32)1 << e->p.chain_log;
        p32 const shift = (e->storage_index - 1) & ~(unit - 1);

        if (!shift)
                return;
        for (positive i = 0; i < e->hash_bytes / 4; i++)
                e->hash[i] = e->hash[i] > shift ? e->hash[i] - shift : 0;
        if (e->p.strategy < ZSTD_GREEDY || e->p.strategy > ZSTD_LAZY2)
                for (positive i = 0; i < e->chain_bytes / 4; i++)
                        e->chain[i] = e->chain[i] > shift ? e->chain[i] - shift
                                      : e->p.strategy == ZSTD_BTLAZY2 &&
                                                e->chain[i] == ZSTD_DUBT_UNSORTED
                                          ? ZSTD_DUBT_UNSORTED
                                          : 0;
        for (positive i = 0; i < e->hash3_bytes / 4; i++)
                e->hash3[i] = e->hash3[i] > shift ? e->hash3[i] - shift : 0;
        e->start = e->start > shift ? e->start - shift : 1;
        e->block -= shift;
        e->filled -= shift;
        e->storage_index -= shift;
        e->next = e->next > shift + e->storage_index ? e->next - shift : e->storage_index;
        e->next3 = e->next3 > shift + e->storage_index ? e->next3 - shift : e->storage_index;
        e->base = e->storage - e->storage_index;
}

/* Where the next input bytes go, and how many: two blocks wait before
   the first is compressed, so the splitter sees a whole block ahead. */
static p8 address_to zstd_encode_room(zstd_encoder address_to e,
                                      positive address_to room)
{
        if (e->filled > 0xC0000000u)
                zstd_encoder_reduce(e);
        if ((positive)(e->block - e->storage_index) + 2 * ZSTD_BLOCK_MAX + 64 >
            e->storage_room)
                zstd_encoder_slide(e);
        address_to room = 2 * ZSTD_BLOCK_MAX - (e->filled - e->block);
        return e->base + e->filled;
}

/*
        libzstd 1.5.7's pre-block splitter.  A full 128 KiB block about to be
        compressed is cut where its bytes change character, found from
        fingerprints: counts of a hash of each two bytes, sampled every 43,
        11, 5 or 1 positions over 8 KiB chunks (dfast to btultra2), a chunk
        cut off from the ones before it when its counts stray far enough; or
        for fast, the first, middle and last 512 bytes decide 32, 64 or 96
        KiB.  A block only splits once the frame has saved three bytes.
*/
#define ZSTD_SPLIT_CHUNK ((positive)8 << 10)

typedef struct
{
        p32 events[1024];
        positive count;
} zstd_print;

static __attribute__((always_inline)) inline positive
zstd_print_hash(p8 address_to p, p8 log)
{
        return log == 8 ? p[0]
                        : (positive)(((p32)memory_load_unaligned(p16, p) * 0x9e3779b9u) >>
                                     (32 - log));
}

static fn zstd_print_record(zstd_print address_to f, p8 address_to src, positive rate, p8 log)
{
        positive const limit = ZSTD_SPLIT_CHUNK - 1;

        memory_fill(f->events, 0, sizeof(p32) << log);
        for (positive at = 0; at < limit; at += rate)
                f->events[zstd_print_hash(src + at, log)]++;
        f->count = limit / rate;
}

static p64 zstd_print_distance(const zstd_print address_to a, const zstd_print address_to b,
                               p8 log)
{
        p64 distance = 0;

        for (positive at = 0; at < ((positive)1 << log); at++)
        {
                bipolar const d = (bipolar)a->events[at] * (bipolar)b->count -
                                  (bipolar)b->events[at] * (bipolar)a->count;

                distance += (p64)(d < 0 ? -d : d);
        }
        return distance;
}

static bool zstd_print_differ(const zstd_print address_to past, const zstd_print address_to next,
                              positive penalty, p8 log)
{
        p64 const p50 = (p64)past->count * (p64)next->count;

        return zstd_print_distance(past, next, log) >= p50 * (14 + penalty) / 16;
}

static positive zstd_split_block(p8 address_to src, p8 strategy)
{
        static const p8 levels[10] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 4};
        static const p8 rates[4] = {43, 11, 5, 1};
        static const p8 logs[4] = {8, 9, 10, 10};
        p8 const level = levels[strategy];
        zstd_print past;
        zstd_print next;

        if (!level)
        {
                zstd_print middle;
                p64 from_begin;
                p64 from_end;
                bipolar gap;

                memory_fill(address_of past, 0, sizeof(past));
                memory_fill(address_of next, 0, sizeof(next));
                memory_fill(address_of middle, 0, sizeof(middle));
                for (positive at = 0; at < 512; at++)
                {
                        past.events[src[at]]++;
                        next.events[src[ZSTD_BLOCK_MAX - 512 + at]]++;
                        middle.events[src[ZSTD_BLOCK_MAX / 2 - 256 + at]]++;
                }
                past.count = next.count = middle.count = 512;
                if (!zstd_print_differ(address_of past, address_of next, 0, 8))
                        return ZSTD_BLOCK_MAX;
                from_begin = zstd_print_distance(address_of past, address_of middle, 8);
                from_end = zstd_print_distance(address_of next, address_of middle, 8);
                gap = (bipolar)from_begin - (bipolar)from_end;
                if ((gap < 0 ? -gap : gap) < 512 * 512 / 3)
                        return 64 << 10;
                return from_begin > from_end ? 32 << 10 : 96 << 10;
        }
        {
                positive const rate = rates[level - 1];
                p8 const log = logs[level - 1];
                positive penalty = 3;

                memory_fill(address_of past, 0, sizeof(past));
                memory_fill(address_of next, 0, sizeof(next));
                zstd_print_record(address_of past, src, rate, log);
                for (positive at = ZSTD_SPLIT_CHUNK; at <= ZSTD_BLOCK_MAX - ZSTD_SPLIT_CHUNK;
                     at += ZSTD_SPLIT_CHUNK)
                {
                        zstd_print_record(address_of next, src + at, rate, log);
                        if (zstd_print_differ(address_of past, address_of next, penalty, log))
                                return at;
                        for (positive k = 0; k < ((positive)1 << log); k++)
                                past.events[k] += next.events[k];
                        past.count += next.count;
                        if (penalty)
                                penalty--;
                }
        }
        return ZSTD_BLOCK_MAX;
}

/* The next block's size when `left` bytes wait: a full block's worth may
   be cut short by the splitter. */
static positive zstd_block_size(zstd_encoder address_to e, positive left)
{
        if (left < ZSTD_BLOCK_MAX)
                return left;
        if (e->savings < 3)
                return ZSTD_BLOCK_MAX;
        return zstd_split_block(e->base + e->block, e->p.strategy);
}

/* The next n bytes as a block: RLE when they are one byte, raw under 12,
   otherwise parsed and entropy coded, or raw when that is no smaller. */
static bool zstd_encode_block(zstd_encoder address_to e, positive n, bool last)
{
        p32 const from = e->block;
        p8 address_to const src = e->base + from;
        p64 const before = e->emitted;
        bool ok;

        if (n && memory_span_byte(src, src[0], n) == n)
        {
                p8 head[4];

                zstd_block_header(head, last, 1, n);
                head[3] = src[0];
                ok = zstd_enc_emit(e, head, sizeof(head));
        }
        else if (n < 12)
                ok = zstd_emit_raw_block(e, src, n, last);
        else
        {
                p32 const to = from + (p32)n;
                p32 const window = (p32)1 << e->p.window_log;
                p32 const low = to - e->start > window ? to - window : e->start;
                p32 const saved[3] = {e->rep[0], e->rep[1], e->rep[2]};
                p8 address_to anchor;
                b32 written;

                e->nseq = 0;
                e->nlit = 0;
                memory_fill(address_of e->freq, 0, sizeof(e->freq));
                /* btultra2 parses a frame's first block twice: the first
                   pass only counts, then its bytes take new indices past
                   the tables, so the second pass starts with no history
                   and the counts of the first. */
                if (e->p.strategy == ZSTD_BTULTRA2 && !e->price.ll_sum &&
                    from == e->start)
                {
                        zstd_parse_opt(e, from, to, true);
                        memory_copy_apart(e->rep, saved, sizeof(saved));
                        e->nseq = 0;
                        e->nlit = 0;
                        memory_fill(address_of e->freq, 0, sizeof(e->freq));
                        e->base -= n;
                        e->start += (p32)n;
                        e->block += (p32)n;
                        e->filled += (p32)n;
                        e->storage_index += (p32)n;
                        e->next = e->start;
                        e->next3 = e->start;
                        return zstd_encode_block(e, n, last);
                }
                switch (e->p.strategy)
                {
                case ZSTD_FAST:
                        anchor = zstd_parse_fast(e, from, to, low);
                        break;
                case ZSTD_DFAST:
                        anchor = zstd_parse_dfast(e, from, to, low);
                        break;
                case ZSTD_GREEDY:
                        anchor = zstd_parse_lazy(e, from, to, low, 0, false);
                        break;
                case ZSTD_LAZY:
                        anchor = zstd_parse_lazy(e, from, to, low, 1, false);
                        break;
                case ZSTD_LAZY2:
                        anchor = zstd_parse_lazy(e, from, to, low, 2, false);
                        break;
                case ZSTD_BTLAZY2:
                        anchor = zstd_parse_lazy(e, from, to, low, 2, true);
                        break;
                case ZSTD_BTOPT:
                        anchor = zstd_parse_opt(e, from, to, false);
                        break;
                default:
                        anchor = zstd_parse_opt(e, from, to, true);
                        break;
                }
                memory_copy_apart(e->lits + e->nlit, anchor,
                                  (positive)(src + n - anchor));
                e->nlit += (positive)(src + n - anchor);
                if (zstd_train_sink)
                {
                        for (positive i = 0; i < e->nlit; i++)
                                zstd_train_sink->lit[e->lits[i]]++;
                        for (positive i = 0; i < e->nseq; i++)
                        {
                                zstd_train_sink->ll[e->seqs[i].ll_code]++;
                                zstd_train_sink->of[e->seqs[i].of_code]++;
                                zstd_train_sink->ml[e->seqs[i].ml_code]++;
                        }
                        e->block += (p32)n;
                        return true;
                }
                written = e->p.strategy >= ZSTD_BTOPT && e->p.window_log >= 17
                              ? zstd_split_sequences(e, n, last)
                              : zstd_entropy_block(e, n, last);
                ok = written > 0;
                if (!written)
                {
                        memory_copy_apart(e->rep, saved, sizeof(saved));
                        ok = zstd_emit_raw_block(e, src, n, last);
                }
        }
        e->block += (p32)n;
        e->savings += (bipolar)n - (bipolar)(e->emitted - before);
        return ok;
}

/* --single-thread: one encoder over the whole input, as libzstd's single
   thread mode, whose bytes differ from the jobs'.  -T#: at most that many
   jobs at once; 0, the default, as many as the memory budget holds. */
static bool zstd_single_job;
static positive zstd_job_limit;
static bool zstd_enc_checksum;

/*
        Jobs.  Unless --single-thread, a frame is cut into jobs of
        2^max(20, window_log + 2) bytes, as libzstd's multithreaded mode cuts
        them.  Each is compressed on its own, by a fresh encoder whose
        history is the tail of the input before it (the overlap: a window
        shifted down by strategy, as libzstd sizes it).  Input gathers into
        rounds of as many jobs as 256 MiB holds (at most -T#), and a round
        runs through parallel_ordered: a job builds its blocks in its own
        output and the sink writes them in order.  Job sizes come from the
        level alone, never the worker count, so the frame is the same bytes
        on one CPU or sixty-four; xxh64 runs here, over the input in order.
*/
#define ZSTD_JOBS_BUDGET ((positive)256 << 20)
#define ZSTD_JOBS_MAX 256

static struct
{
        bool active;
        bool ran;       /* the fd path ran the final round itself */
        bool final;
        bool first_round;
        zstd_params p;
        positive job;
        positive overlap;
        positive per_round;
        positive history;
        positive filled;
        positive count;
        p8 address_to buffer;
        positive buffer_room;
        /* The fd path reads the next round here while the round in buffer
           runs, then the two trade places. */
        p8 address_to spare;
        positive spare_room;
        zstd_encoder address_to slots;
        positive slot_count;
        p8 failed[ZSTD_JOBS_MAX];
} zstd_jobs;

/* A job encoder's tables and scratch, mapped on its first job and cleared
   for every one after, so no job sees what another job left. */
static bool zstd_encoder_job_tables(zstd_encoder address_to e,
                                    const zstd_params address_to p)
{
        positive const hash_bytes = (positive)4 << p->hash_log;
        positive const chain_bytes = zstd_chain_bytes(p);
        p8 const hash3_log = p->window_log < 17 ? p->window_log : 17;
        positive const hash3_bytes = p->strategy >= ZSTD_BTOPT && p->min_match == 3
                                         ? (positive)4 << hash3_log
                                         : 0;

        if (!e->seqs &&
            (!(e->lits = memory_checked(ZSTD_BLOCK_MAX + 64)) ||
             !(e->bits = memory_checked(ZSTD_BLOCK_MAX + 256)) ||
             !(e->block_out = memory_checked(ZSTD_BLOCK_MAX + 2048)) ||
             !(e->trial_out = memory_checked(ZSTD_BLOCK_MAX + 2048)) ||
             !(e->split_out = memory_checked(ZSTD_BLOCK_MAX + 2048 * 4)) ||
             !(e->cum_lit = memory_checked(sizeof(p32) * (ZSTD_ENC_SEQ_MAX + 2))) ||
             !(e->cum_src = memory_checked(sizeof(p32) * (ZSTD_ENC_SEQ_MAX + 2))) ||
             !(e->packed = memory_checked(ZSTD_BLOCK_MAX * 2 + 512)) ||
             !(e->lit_table_new = memory_checked(sizeof(p32) * 256)) ||
             !(e->lit_length_new = memory_checked(256)) ||
             !(e->opt = memory_checked(sizeof(zstd_opt_node) * (ZSTD_OPT_NUM + 3))) ||
             !(e->matches = memory_checked(sizeof(zstd_opt_match) * (ZSTD_OPT_NUM + 3))) ||
             !(e->seqs = memory_checked(sizeof(zstd_enc_seq) * ZSTD_ENC_SEQ_MAX))))
                return false;
        if (e->hash_bytes != hash_bytes || e->chain_bytes != chain_bytes ||
            e->hash3_bytes != hash3_bytes)
        {
                if (e->hash)
                        memory_free(e->hash, e->hash_bytes);
                if (e->chain)
                        memory_free(e->chain, e->chain_bytes);
                if (e->hash3)
                        memory_free(e->hash3, e->hash3_bytes);
                e->hash = null;
                e->chain = null;
                e->hash3 = null;
                e->hash_bytes = 0;
                e->chain_bytes = 0;
                e->hash3_bytes = 0;
                if (!(e->hash = memory_checked(hash_bytes)))
                        return false;
                e->hash_bytes = hash_bytes;
                if (chain_bytes && !(e->chain = memory_checked(chain_bytes)))
                        return false;
                e->chain_bytes = chain_bytes;
                if (hash3_bytes && !(e->hash3 = memory_checked(hash3_bytes)))
                        return false;
                e->hash3_bytes = hash3_bytes;
        }
        else
        {
                memory_fill(e->hash, 0, hash_bytes);
                if (chain_bytes)
                        memory_fill(e->chain, 0, chain_bytes);
                if (hash3_bytes)
                        memory_fill(e->hash3, 0, hash3_bytes);
        }
        e->hash3_log = hash3_log;
        return true;
}

/* One job: size bytes at from + prefix, with the prefix bytes before them
   as history, into output; a last job's last block ends the frame.  Only
   the frame's first job knows the decoder's repeat offsets (1, 4, 8); any
   later job enters after sequences it never saw, so its offsets start at 0,
   which no match equals, as libzstd invalidates them, until its own
   sequences set them. */
static bool zstd_encoder_job(zstd_encoder address_to e, const zstd_params address_to p,
                             p8 address_to from, positive prefix, positive size,
                             address_any output, bool first, bool last)
{
        if (!zstd_encoder_job_tables(e, p))
                return false;
        e->p = address_to p;
        e->checksum = false;
        e->output = output;
        e->base = from - 1;
        e->start = 1;
        e->storage_index = 1;
        e->block = 1 + (p32)prefix;
        e->filled = (p32)(1 + prefix + size);
        e->next = 1;
        e->next3 = 1;
        e->savings = 0;
        e->rep[0] = first ? 1 : 0;
        e->rep[1] = first ? 4 : 0;
        e->rep[2] = first ? 8 : 0;
        e->nseq = 0;
        e->nlit = 0;
        e->huf_valid = false;
        e->prior[0].valid = false;
        e->prior[1].valid = false;
        e->prior[2].valid = false;
        memory_fill(address_of e->price, 0, sizeof(e->price));
        if (first && zstd_dict_on)
                zstd_encoder_dictionary_state(e);
        if (prefix && p->strategy <= ZSTD_DFAST)
                zstd_encoder_prefix(e, 1, 1 + (p32)prefix, first && zstd_dict_on ? 1 : 3);
        /* The row finder takes every position of the prefix, as libzstd
           loads one; left to the parse, its first update would skip all
           but 128 of them as the tail of a long match. */
        else if (prefix > 8 && p->strategy <= ZSTD_LAZY2)
        {
                p8 const row_log = zstd_row_log(p);
                p8 const mls = p->min_match < 4 ? 4 : p->min_match > 6 ? 6 : p->min_match;
                p32 const target = 1 + (p32)prefix - 8;

                for (p32 at = 1; at < target; at++)
                        zstd_row_insert(e, at, row_log, mls);
                e->next = target;
        }
        do
        {
                positive const left = e->filled - e->block;
                positive const n = zstd_block_size(e, left);

                if (!zstd_encode_block(e, n, last && n == left))
                        return false;
        } while (e->block < e->filled);
        return true;
}

static fn zstd_job_run(address_any context, positive index,
                       parallel_output address_to output)
{
        positive const offset = zstd_jobs.history + index * zstd_jobs.job;
        positive const input = zstd_jobs.history + zstd_jobs.filled;
        positive const size = index + 1 < zstd_jobs.count ? zstd_jobs.job
                                                          : input - offset;
        positive const prefix = zstd_jobs.first_round && !index && zstd_dict_on
                                    ? offset
                                    : offset < zstd_jobs.overlap ? offset : zstd_jobs.overlap;

        (void)context;
        zstd_jobs.failed[index] = !zstd_encoder_job(
            zstd_jobs.slots + parallel_slot(), address_of zstd_jobs.p,
            zstd_jobs.buffer + offset - prefix, prefix, size, output,
            zstd_jobs.first_round && !index,
            zstd_jobs.final && index + 1 == zstd_jobs.count);
}

static bool zstd_job_sink(address_any context, positive index,
                          address_any data, positive length)
{
        (void)context;
        return_if(zstd_jobs.failed[index], zstd_fail("zstd cannot map a job's tables"));
        return zstd_enc_out(data, length);
}

/* 1 when this frame runs as jobs, 0 when it runs one encoder, -1 when the
   jobs' memory cannot be had. */
static b32 zstd_jobs_open(const zstd_params address_to p)
{
        p8 const job_log = p->window_log + 2 < 20   ? 20
                           : p->window_log + 2 > 30 ? 30
                                                    : (p8)(p->window_log + 2);
        p8 const overlap_log = p->strategy == ZSTD_BTULTRA2 ? 9
                               : p->strategy >= ZSTD_BTOPT  ? 8
                               : p->strategy >= ZSTD_LAZY2  ? 7
                                                            : 6;
        positive const job = (positive)1 << job_log;
        positive const slots = parallel_slots();
        positive per_round = ZSTD_JOBS_BUDGET / job;
        positive room;

        zstd_jobs.active = false;
        zstd_jobs.ran = false;
        if (zstd_single_job)
                return 0;
        if (!per_round)
                per_round = 1;
        if (zstd_job_limit && per_round > zstd_job_limit)
                per_round = zstd_job_limit;
        /* With no -T a round is two waves of the pool, so the next round
           can be read while this one runs; the bytes never depend on it. */
        if (!zstd_job_limit && parallel_width() > 1 && per_round > 2 * parallel_width())
                per_round = 2 * parallel_width();
        zstd_jobs.overlap = ((positive)1 << p->window_log) >> (9 - overlap_log);
        room = zstd_jobs.overlap + per_round * job + 64 + zstd_dictionary_kept(p->window_log);
        if (zstd_jobs.buffer_room < room)
        {
                if (zstd_jobs.buffer)
                        memory_free(zstd_jobs.buffer, zstd_jobs.buffer_room);
                zstd_jobs.buffer_room = 0;
                if (!(zstd_jobs.buffer = memory_checked(room)))
                        return -1;
                zstd_jobs.buffer_room = room;
        }
        if (zstd_jobs.slot_count < slots)
        {
                if (!(zstd_jobs.slots = memory_checked(slots * sizeof(zstd_encoder))))
                        return -1;
                zstd_jobs.slot_count = slots;
        }
        zstd_jobs.p = address_to p;
        zstd_jobs.job = job;
        zstd_jobs.per_round = per_round;
        /* A dictionary is the first job's history, before its input. */
        zstd_jobs.history = zstd_dictionary_kept(p->window_log);
        if (zstd_jobs.history)
                memory_copy(zstd_jobs.buffer, zstd_dict_content + zstd_dict_size - zstd_jobs.history,
                            zstd_jobs.history);
        zstd_jobs.filled = 0;
        zstd_jobs.first_round = true;
        zstd_jobs.active = true;
        return 1;
}

/* The jobs of the round in the buffer, the last one ending the frame when
   final. */
static bool zstd_jobs_run(bool final)
{
        zstd_jobs.count = final ? (zstd_jobs.filled + zstd_jobs.job - 1) / zstd_jobs.job
                                : zstd_jobs.per_round;
        if (!zstd_jobs.count)
                zstd_jobs.count = 1;
        zstd_jobs.final = final;
        if (!parallel_ordered(zstd_job_run, zstd_job_sink, null, zstd_jobs.count,
                              zstd_jobs.filled))
                return zstd_why ? false : zstd_fail("zstd jobs stopped");
        return true;
}

/* The jobs gathered so far, then the overlap kept for the next round's
   first job.  The final round's last job may be short, or empty when the
   input ended with a round. */
static bool zstd_jobs_round(bool final)
{
        positive const input = zstd_jobs.history + zstd_jobs.filled;
        positive const keep = input < zstd_jobs.overlap ? input : zstd_jobs.overlap;

        if (!zstd_jobs_run(final))
                return false;
        memory_copy(zstd_jobs.buffer, zstd_jobs.buffer + input - keep, keep);
        zstd_jobs.history = keep;
        zstd_jobs.filled = 0;
        zstd_jobs.first_round = false;
        return true;
}

/*
        A regular file needs no rounds: every job reads its own bytes (and
        the overlap before them) from the file at its own offset into a
        buffer its pool slot owns, so the workers never wait on a reader nor
        on each other at a round's end, and the input's checksum is taken in
        order by the sink, which reads each job's bytes a second time from
        the cache while the workers compress the next.  The cuts, the
        overlaps and so the bytes are the rounds'.  0 done, 1 read failed, -1
        anything else (zstd_why says), -2 when this input is not one for it.
*/
typedef struct
{
        bipolar in;
        p64 base;
        positive size;
        positive count;
        positive kept;
        p8 address_to slots;
        positive slot_room;
        p8 address_to check;
        p8 address_to failed;
} zstd_file_run;

static bool zstd_file_read(bipolar in, p8 address_to into, positive n, p64 at)
{
        positive got = 0;

        while (got < n)
        {
                bipolar const r = system_call_4(syscall(pread64), (positive)in,
                                                (positive)(into + got), n - got,
                                                (positive)(at + got));

                if (r == -4)
                        continue;
                if (r <= 0)
                        return false;
                got += (positive)r;
        }
        return true;
}

static fn zstd_file_job(address_any context, positive index,
                        parallel_output address_to output)
{
        zstd_file_run address_to const f = context;
        positive const offset = index * zstd_jobs.job;
        positive const size = index + 1 < f->count ? zstd_jobs.job : f->size - offset;
        positive const prefix = !index ? f->kept
                                       : offset < zstd_jobs.overlap ? offset : zstd_jobs.overlap;
        p8 address_to const buffer = f->slots + parallel_slot() * f->slot_room;
        bool ok = true;

        if (!index)
        {
                if (prefix)
                        memory_copy(buffer, zstd_dict_content + zstd_dict_size - prefix, prefix);
        }
        else if (prefix)
                ok = zstd_file_read(f->in, buffer, prefix, f->base + offset - prefix);
        ok = ok && zstd_file_read(f->in, buffer + prefix, size, f->base + offset);
        f->failed[index] = !ok ? 2
                               : !zstd_encoder_job(zstd_jobs.slots + parallel_slot(),
                                                   address_of zstd_jobs.p, buffer, prefix, size,
                                                   output, !index, index + 1 == f->count);
}

static bool zstd_file_sink(address_any context, positive index, address_any data,
                           positive length)
{
        zstd_file_run address_to const f = context;

        if (f->failed[index])
                return f->failed[index] == 2 ? zstd_fail("zstd read failed")
                                             : zstd_fail("zstd cannot map a job's tables");
        if (zstd_enc_checksum)
        {
                positive const offset = index * zstd_jobs.job;
                positive const size = index + 1 < f->count ? zstd_jobs.job : f->size - offset;

                if (!zstd_file_read(f->in, f->check, size, f->base + offset))
                        return zstd_fail("zstd read failed");
                hash_xxh64_add(address_of zstd_enc_hash, f->check, size);
        }
        return zstd_enc_out(data, length);
}

static b32 zstd_jobs_file(bipolar in, p64 size)
{
        zstd_file_run f = {0};
        bipolar const at = system_call_3(syscall(lseek), (positive)in, 0, 1);
        positive const slots = parallel_slots();
        bool ok;

        if (at < 0 || (p64)at >= size || size - (p64)at <= zstd_jobs.job ||
            (zstd_job_limit && zstd_job_limit < parallel_width()))
                return -2;
        f.in = in;
        f.base = (p64)at;
        f.size = (positive)(size - (p64)at);
        f.count = (f.size + zstd_jobs.job - 1) / zstd_jobs.job;
        f.kept = zstd_dictionary_kept(zstd_jobs.p.window_log);
        f.slot_room = (f.kept > zstd_jobs.overlap ? f.kept : zstd_jobs.overlap) + zstd_jobs.job + 64;
        if (slots * f.slot_room > ((positive)3 << 30) || f.count > ((positive)1 << 24))
                return -2;
        f.slots = memory_checked(slots * f.slot_room);
        f.failed = memory_checked(f.count);
        f.check = zstd_enc_checksum ? memory_checked(zstd_jobs.job) : null;
        if (!f.slots || !f.failed || (zstd_enc_checksum && !f.check))
        {
                if (f.slots)
                        memory_free(f.slots, slots * f.slot_room);
                if (f.failed)
                        memory_free(f.failed, f.count);
                return -2;
        }
        ok = parallel_ordered(zstd_file_job, zstd_file_sink, address_of f, f.count, f.size);
        memory_free(f.slots, slots * f.slot_room);
        memory_free(f.failed, f.count);
        if (f.check)
                memory_free(f.check, zstd_jobs.job);
        if (!ok)
                return zstd_why ? -1 : (zstd_fail("zstd jobs stopped"), -1);
        zstd_jobs.ran = true;
        return 0;
}

/* Reading ahead: the next round's bytes, into its buffer behind the
   overlap, and into the checksum in order. */
typedef struct
{
        bipolar in;
        p8 address_to into;
        positive room;
        positive got;
        bool failed;
} zstd_reader;

static fn zstd_reader_job(address_any context, positive index)
{
        zstd_reader address_to const r = context;

        (void)index;
        while (r->got < r->room)
        {
                bipolar const n = system_read_retry((positive)r->in, r->into + r->got,
                                                    r->room - r->got);

                if (n < 0)
                {
                        r->failed = true;
                        return;
                }
                if (!n)
                        break;
                if (zstd_enc_checksum)
                        hash_xxh64_add(address_of zstd_enc_hash, r->into + r->got, (positive)n);
                r->got += (positive)n;
        }
}

/*
        A descriptor's frame as jobs, reading the next round beside the
        pool while this one compresses, as libzstd's reader thread keeps
        its workers fed; the read was the serial front of every round, the
        workers idle through it.  A full round waits for one more byte
        before it runs, as the loop over zstd_encode_space does, so the
        frame's last block always falls in the final round.  0 done, 1 read
        failed, -1 anything else (zstd_why says).
*/
static b32 zstd_jobs_fd(bipolar in)
{
        positive const capacity = zstd_jobs.per_round * zstd_jobs.job;
        zstd_reader reader = {0};

        if (zstd_jobs.spare_room < zstd_jobs.buffer_room)
        {
                if (zstd_jobs.spare)
                        memory_free(zstd_jobs.spare, zstd_jobs.spare_room);
                zstd_jobs.spare_room = 0;
                if (!(zstd_jobs.spare = memory_checked(zstd_jobs.buffer_room)))
                        return zstd_fail("zstd cannot map its jobs"), -1;
                zstd_jobs.spare_room = zstd_jobs.buffer_room;
        }
        reader.in = in;
        reader.into = zstd_jobs.buffer + zstd_jobs.history;
        reader.room = capacity;
        zstd_reader_job(address_of reader, 0);
        if (reader.failed)
                return 1;
        zstd_jobs.filled = reader.got;
        for (;;)
        {
                positive const input = zstd_jobs.history + zstd_jobs.filled;
                positive const keep = input < zstd_jobs.overlap ? input : zstd_jobs.overlap;
                p8 address_to const next = zstd_jobs.spare;
                bipolar probe;
                bool beside;
                bool ok;

                if (zstd_jobs.filled < capacity)
                        return zstd_jobs_run(true) ? 0 : -1;
                memory_copy_apart(next, zstd_jobs.buffer + input - keep, keep);
                probe = system_read_retry((positive)in, next + keep, 1);
                if (probe < 0)
                        return 1;
                if (!probe)
                        return zstd_jobs_run(true) ? 0 : -1;
                if (zstd_enc_checksum)
                        hash_xxh64_add(address_of zstd_enc_hash, next + keep, 1);
                reader = (zstd_reader){in, next + keep + 1, capacity - 1, 0, false};
                beside = parallel_beside(zstd_reader_job, address_of reader);
                ok = zstd_jobs_run(false);
                if (beside)
                        parallel_beside_wait();
                else
                        zstd_reader_job(address_of reader, 0);
                if (!ok)
                        return -1;
                if (reader.failed)
                        return 1;
                {
                        positive const room = zstd_jobs.spare_room;

                        zstd_jobs.spare = zstd_jobs.buffer;
                        zstd_jobs.spare_room = zstd_jobs.buffer_room;
                        zstd_jobs.buffer = next;
                        zstd_jobs.buffer_room = room;
                }
                zstd_jobs.history = keep;
                zstd_jobs.filled = 1 + reader.got;
                zstd_jobs.first_round = false;
        }
}

/* Where the next input bytes go, and how many fit before work must run. */
static p8 address_to zstd_encode_space(positive address_to room)
{
        if (zstd_jobs.active)
        {
                positive const capacity = zstd_jobs.per_round * zstd_jobs.job;

                /* A full round runs only when more input comes: at the end of
                   the input it runs as the final round instead, so where
                   rounds fall never moves the frame's last block.  Null when
                   the round failed. */
                if (zstd_jobs.filled == capacity && !zstd_jobs_round(false))
                        return null;
                address_to room = capacity - zstd_jobs.filled;
                return zstd_jobs.buffer + zstd_jobs.history + zstd_jobs.filled;
        }
        return zstd_encode_room(address_of zstd_enc, room);
}

/* n bytes put at zstd_encode_space: the one encoder compresses a full block
   now; jobs wait for zstd_encode_space or the end. */
static bool zstd_encode_taken(positive n)
{
        if (zstd_jobs.active)
        {
                zstd_jobs.filled += n;
                return true;
        }
        zstd_enc.filled += (p32)n;
        return zstd_enc.filled - zstd_enc.block < 2 * ZSTD_BLOCK_MAX ||
               zstd_encode_block(address_of zstd_enc,
                                 zstd_block_size(address_of zstd_enc, ZSTD_BLOCK_MAX), false);
}

static bool zstd_encode_start(const zstd_params address_to p, bool checksum)
{
        p8 head[10];
        positive head_size = 6;
        p32 const dict_id = zstd_dict_on && !zstd_cli_no_dict_id ? zstd_dict_id : 0;
        b32 jobs;

        zstd_why = null;
        zstd_ct_init();
        return_if(!zstd_ct_ready, zstd_fail("zstd cannot build its predefined tables"));
        zstd_enc_checksum = checksum;
        jobs = zstd_jobs_open(p);
        return_if(jobs < 0, zstd_fail("zstd cannot map its jobs"));
        if (!jobs && !zstd_encoder_open(address_of zstd_enc, p, checksum))
                return false;
        hash_xxh64_begin(address_of zstd_enc_hash, 0);
        head[0] = 0x28;
        head[1] = 0xb5;
        head[2] = 0x2f;
        head[3] = 0xfd;
        head[4] = checksum ? 0x04 : 0x00;
        head[5] = (p8)((p->window_log - 10) << 3);
        if (dict_id)
        {
                p8 const bytes = dict_id < 256 ? 1 : dict_id < 65536 ? 2 : 4;

                head[4] |= bytes == 1 ? 1 : bytes == 2 ? 2 : 3;
                for (positive k = 0; k < bytes; k++)
                        head[head_size++] = (p8)(dict_id >> (8 * k));
        }
        return (!zstd_output.bytes && zstd_out_fd < 0) ||
               zstd_enc_out(head, head_size);
}

/* The codec table's entry: a level from 1 to 22 and no size to go by. */
static bool zstd_encode_begin(bipolar out, p8 level)
{
        zstd_params params;

        zstd_level_params(level, 0, 0, address_of params);
        zstd_out_fd = out;
        zstd_output.bytes = null;
        return zstd_encode_start(address_of params, true);
}

static bool zstd_encode_write(p8 address_to src, positive n)
{
        if (zstd_enc_checksum)
                hash_xxh64_add(address_of zstd_enc_hash, src, n);
        while (n)
        {
                positive room;
                p8 address_to const into = zstd_encode_space(address_of room);
                positive take;

                if (!into)
                        return false;
                take = n < room ? n : room;
                memory_copy_apart(into, src, take);
                src += take;
                n -= take;
                if (!zstd_encode_taken(take))
                        return false;
        }
        return true;
}

static bool zstd_encode_end(void)
{
        p8 tail[4];

        if (zstd_jobs.active)
        {
                zstd_jobs.active = false;
                if (!zstd_jobs.ran && !zstd_jobs_round(true))
                        return false;
        }
        else
                do
                {
                        positive const left = zstd_enc.filled - zstd_enc.block;
                        positive const n = zstd_block_size(address_of zstd_enc, left);

                        if (!zstd_encode_block(address_of zstd_enc, n, n == left))
                                return false;
                } while (zstd_enc.block < zstd_enc.filled);
        if (!zstd_enc_checksum)
                return true;
        memory_store_unaligned(p32, tail,
                               (p32)hash_xxh64_finish(address_of zstd_enc_hash));
        return (!zstd_output.bytes && zstd_out_fd < 0) ||
               zstd_enc_out(tail, sizeof tail);
}

static bipolar zstd_deflate_mem(p8 address_to src, positive src_len,
                                p8 address_to dst, positive dst_cap)
{
        zstd_params params;

        zstd_output.bytes = dst;
        zstd_output.room = dst_cap;
        zstd_output.used = 0;
        zstd_out_fd = -1;
        zstd_level_params(3, 0, 0, address_of params);
        if (!zstd_encode_start(address_of params, true) ||
            !zstd_encode_write(src, src_len) || !zstd_encode_end())
                return -1;
        zstd_output.bytes = null;
        return (bipolar)zstd_output.used;
}

#ifndef ZSTD_CORE_ONLY

static b32 zstd_status;
static bool zstd_cli_ultra;
static bool zstd_cli_no_check;
static bool zstd_cli_warned;
static bool zstd_cli_quiet;
static positive zstd_cli_fast;
static p8 zstd_cli_long;

static fn zstd_refuse(string_address message)
{
        //      A reason is spelled for tar, which puts it after its own name
        //      and so needs the codec's in front; this command has already
        //      said whose it is.
        if (!string_compare_max(message, "zstd: ", 6))
                message += 6;
        else if (!string_compare_max(message, "zstd ", 5))
                message += 5;
        string_format(log_error, "zstd: %s\n", message);
        zstd_status = 1;
}

static const file_codec_suffix zstd_suffixes[] = {
    {".zst", ""}, {".tzst", ".tar"}};

/* A decimal number at `at`, saturating; answers how many digits. */
static positive zstd_cli_number(string_address at, positive address_to value)
{
        positive taken = 0;

        address_to value = 0;
        while (at[taken] >= '0' && at[taken] <= '9')
        {
                if (address_to value < 1000000)
                        address_to value = address_to value * 10 + (positive)(at[taken] - '0');
                taken++;
        }
        return taken;
}

/*
        zstd's own options, ahead of the shared ones.  -# is a whole number:
        a level, 0 the default; above 19 it needs --ultra.  -T# and
        --threads=# cap the jobs run at once (0, or -T alone: as many as
        memory holds); --single-thread is one encoder over the whole input,
        whose bytes differ from the jobs'.  --fast[=#] is a negative level and
        --long[=#] a window of 2^# bytes (27 alone).  --no-check leaves out
        the content checksum; -C and --check put it back.
*/
/* A dictionary file whole, into memory that stays. */
static bool zstd_cli_dictionary(string_address path)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ);
        file_facts facts;
        p8 address_to bytes;
        positive size = 0;
        positive got = 0;
        p8 reason[64];

        if (handle < 0)
        {
                strerror_r((b32)-handle, (string_address)reason, sizeof reason);
                string_format(log_error, "zstd: error 31 : Stat failed on dictionary file %s: %s \n",
                              path, reason);
                return false;
        }
        if (file_look_code(handle, (string_address)"", AT_EMPTY_PATH, address_of facts) == 0 &&
            (facts.mode & MODE_FORMAT) == MODE_FILE)
                size = facts.size;
        if (size > ZSTD_WINDOW_MAX)
        {
                system_close(handle);
                string_format(log_error, "zstd: error 32 : dictionary file %s is too large \n", path);
                return false;
        }
        bytes = memory_checked(size + 64);
        if (!bytes)
        {
                system_close(handle);
                string_format(log_error, "zstd: error 31 : cannot allocate dictionary \n");
                return false;
        }
        while (got < size)
        {
                bipolar const n = system_read_retry((positive)handle, bytes + got, size - got);

                if (n <= 0)
                        break;
                got += (positive)n;
        }
        system_close(handle);
        if (got != size)
        {
                string_format(log_error, "zstd: error 31 : Read error : %s \n", path);
                return false;
        }
        if (!zstd_dictionary_load(bytes, size))
        {
                string_format(log_error, "zstd: error 32 : Dictionary is corrupted : %s \n", path);
                return false;
        }
        return true;
}

static bipolar zstd_cli_option(file_codec_cli address_to codec,
                               string_address at, bool word)
{
        positive value = 0;
        positive taken;

        if (!word)
        {
                if (*at >= '0' && *at <= '9')
                {
                        taken = zstd_cli_number(at, address_of value);
                        codec->level = value > 255 ? 255 : (p8)value;
                        zstd_cli_fast = 0;
                        return (bipolar)taken;
                }
                if (*at == 'T')
                {
                        taken = zstd_cli_number(at + 1, address_of zstd_job_limit);
                        zstd_single_job = false;
                        return (bipolar)taken + 1;
                }
                if (*at == 'D')
                {
                        string_address const path = at[1] ? at + 1 : codec->next_argument;

                        if (!path)
                        {
                                string_format(log_error, "zstd: missing argument to -D\n");
                                return -1;
                        }
                        if (!zstd_cli_dictionary(path))
                                return -1;
                        if (!at[1])
                        {
                                codec->took_next = true;
                                return 1;
                        }
                        return (bipolar)string_length(at);
                }
                if (*at == 'C')
                        zstd_cli_no_check = false;
                else if (*at == 'q')
                        zstd_cli_quiet = true;
                else
                        return 0;
                return 1;
        }
        if (string_equals(at, "--ultra"))
                zstd_cli_ultra = true;
        else if (string_equals(at, "--single-thread"))
                zstd_single_job = true;
        else if (string_equals(at, "--no-check"))
                zstd_cli_no_check = true;
        else if (string_equals(at, "--no-dictID"))
                zstd_cli_no_dict_id = true;
        else if (string_equals(at, "--check"))
                zstd_cli_no_check = false;
        else if (string_equals(at, "--quiet"))
                zstd_cli_quiet = true;
        else if (string_equals(at, "--fast"))
                zstd_cli_fast = 1;
        else if (string_equals(at, "--long"))
                zstd_cli_long = 27;
        else if (string_has_prefix(at, "--fast=") ||
                 string_has_prefix(at, "--long=") ||
                 string_has_prefix(at, "--threads="))
        {
                string_address const number = at + (at[2] == 't' ? 10 : 7);

                taken = zstd_cli_number(number, address_of value);
                if (!taken || number[taken] || (at[2] == 'f' && !value))
                {
                        string_format(log_error, "zstd: incorrect parameter: %s\n", at);
                        return -1;
                }
                if (at[2] == 'l' && (value < 10 || value > 27))
                {
                        string_format(log_error,
                                      "zstd: %s: the window log is 10 to 27\n", at);
                        return -1;
                }
                if (at[2] == 'f')
                        zstd_cli_fast = value;
                else if (at[2] == 'l')
                        zstd_cli_long = (p8)value;
                else
                {
                        zstd_job_limit = value;
                        zstd_single_job = false;
                }
        }
        else
                return 0;
        return 1;
}

/* Read straight into the block or round the encoder fills, as xz reads into its
   pending window; zstd_encode_write is the copy for callers with a span.
   A regular input's size shrinks the window to fit, as zstd does. */
static b32 zstd_encode_fd(bipolar in, bipolar out, p8 level)
{
        zstd_params params;
        file_facts facts;
        p64 size = 0;
        b32 wanted = zstd_cli_fast
                         ? -(b32)(zstd_cli_fast > 65535 ? 65535 : zstd_cli_fast)
                         : (b32)level;

        if (wanted > 19 && !zstd_cli_ultra)
        {
                if (!zstd_cli_warned && !zstd_cli_quiet)
                        string_format(log_error,
                                      "Warning : compression level higher than max, reduced to 19\n");
                zstd_cli_warned = true;
                wanted = 19;
        }
        if (file_look_code(in, (string_address)"", AT_EMPTY_PATH,
                           address_of facts) == 0 &&
            (facts.mode & MODE_FORMAT) == MODE_FILE)
                size = facts.size;
        zstd_level_params(wanted, zstd_cli_long, size ? size + (zstd_dict_on ? zstd_dict_size : 0) : 0,
                          address_of params);
        zstd_out_fd = out;
        zstd_output.bytes = null;
        if (!zstd_encode_start(address_of params, !zstd_cli_no_check))
                goto refused;
        if (zstd_jobs.active)
        {
                b32 ran = size ? zstd_jobs_file(in, size) : -2;

                if (ran == -2)
                        ran = zstd_jobs_fd(in);

                if (ran > 0)
                {
                        zstd_refuse("read failed");
                        return 1;
                }
                if (ran < 0)
                        goto refused;
                zstd_jobs.ran = true;
                if (zstd_encode_end())
                        return 0;
                goto refused;
        }
        for (;;)
        {
                positive room;
                p8 address_to into;
                bipolar got;

                /* A full round runs only once the input has another byte:
                   at its end the round is the final one, wherever rounds
                   fall, so -T# never moves the frame's last block. */
                if (zstd_jobs.active &&
                    zstd_jobs.filled == zstd_jobs.per_round * zstd_jobs.job)
                {
                        p8 next;

                        got = system_read_retry((positive)in, address_of next, 1);
                        if (got < 0)
                        {
                                zstd_refuse("read failed");
                                return 1;
                        }
                        if (!got)
                                break;
                        if (!zstd_encode_write(address_of next, 1))
                                goto refused;
                        continue;
                }
                into = zstd_encode_space(address_of room);
                if (!into)
                        goto refused;
                got = system_read_retry((positive)in, into, room);

                if (got < 0)
                {
                        zstd_refuse("read failed");
                        return 1;
                }
                if (!got)
                        break;
                if (zstd_enc_checksum)
                        hash_xxh64_add(address_of zstd_enc_hash, into, (positive)got);
                if (!zstd_encode_taken((positive)got))
                        goto refused;
        }
        if (zstd_encode_end())
                return 0;
refused:
        zstd_refuse(zstd_why ? zstd_why : (string_address) "encode failed");
        return 1;
}

static b32 zstd_one(bipolar in, bipolar out)
{
        bool ok;

        byte_input_open_fd(address_of zstd_src, in, zstd_in_buf, ZSTD_IN);
        zstd_output.bytes = null;
        zstd_out_fd = out;
        zstd_pull = false;
        zstd_live = false;
        zstd_frame_open = false;
        zstd_paused = false;
        zstd_need_trailer = false;
        zstd_empty_refused = true;
        ok = zstd_stream();
        zstd_empty_refused = false;
        if (ok)
                ok = zstd_flush();
        zstd_window_close();
        if (!ok)
        {
                zstd_refuse(zstd_why ? zstd_why
                                     : (string_address) "decode failed");
                return zstd_status ? zstd_status : 1;
        }
        return 0;
}

static b32 zstd_cli_stream(bipolar in, bipolar out, bool decompress, p8 level)
{
        return decompress ? zstd_one(in, out) : zstd_encode_fd(in, out, level);
}

/*
        Training a dictionary, zstd --train.  The samples are the files,
        each at most its first 128 KiB.  The content is picked as COVER
        picks it: every eight-byte string is counted once for each sample
        it is in, the samples are cut into as many epochs as the dictionary
        has segments' room, and from each epoch the segment (of k bytes)
        whose strings have the highest counts is taken, its strings counted
        no more, the first taken ending nearest the end where matches are
        cheapest.  Two segment sizes are tried and the dictionary that packs
        a sample of the files smaller is kept.  The tables are what ZDICT
        writes: the samples parsed with the content as their dictionary at
        level 3, the literals and the three code streams counted with every
        symbol given at least one, a Huffman tree and three FSE tables from
        the counts, repeat offsets 1, 4 and 8, an ID from the content's
        hash unless one was asked for.
*/
#define ZSTD_TRAIN_SAMPLE 131072
#define ZSTD_TRAIN_DMER 8
#define ZSTD_TRAIN_LOG 20

typedef struct
{
        p8 address_to bytes;
        positive address_to sizes;
        positive count;
        positive total;
} zstd_train_set;

static bool zstd_train_load(string_address address_to paths, positive count,
                            zstd_train_set address_to set)
{
        positive total = 0;

        for (positive i = 0; i < count; i++)
        {
                bipolar const handle = system_open_at(AT_FDCWD, paths[i], FILE_READ);
                file_facts facts;

                if (handle < 0)
                {
                        string_format(log_error, "zstd: error 31 : Can't open %s \n", paths[i]);
                        return false;
                }
                if (file_look_code(handle, (string_address)"", AT_EMPTY_PATH, address_of facts) == 0 &&
                    (facts.mode & MODE_FORMAT) == MODE_FILE)
                        total += facts.size < ZSTD_TRAIN_SAMPLE ? (positive)facts.size : ZSTD_TRAIN_SAMPLE;
                system_close(handle);
        }
        if (!total || total > ((positive)1 << 31))
        {
                string_format(log_error, "zstd: error 14 : nothing to train on \n");
                return false;
        }
        set->bytes = memory_checked(total + 64);
        set->sizes = memory_checked(count * sizeof(positive));
        if (!set->bytes || !set->sizes)
                return false;
        set->count = 0;
        set->total = 0;
        for (positive i = 0; i < count; i++)
        {
                bipolar const handle = system_open_at(AT_FDCWD, paths[i], FILE_READ);
                positive got = 0;

                if (handle < 0)
                        return false;
                while (set->total + got < total && got < ZSTD_TRAIN_SAMPLE)
                {
                        positive const room = total - set->total - got < ZSTD_TRAIN_SAMPLE - got
                                                  ? total - set->total - got
                                                  : ZSTD_TRAIN_SAMPLE - got;
                        bipolar const n = system_read_retry((positive)handle,
                                                            set->bytes + set->total + got, room);

                        if (n <= 0)
                                break;
                        got += (positive)n;
                }
                system_close(handle);
                if (got)
                {
                        set->sizes[set->count++] = got;
                        set->total += got;
                }
        }
        return set->count != 0;
}

static positive zstd_train_hash(p8 address_to p)
{
        return (positive)((memory_load_unaligned(p64, p) * 0x9E3779B185EBCA87ull) >>
                          (64 - ZSTD_TRAIN_LOG));
}

/* The best segment of k bytes among [from, stop): the one whose distinct
   strings' counts sum highest. */
static positive zstd_train_segment(const zstd_train_set address_to set, positive from,
                                   positive stop, positive k, p32 address_to freq,
                                   p8 address_to active)
{
        positive best = from;
        p64 best_score = 0;
        p64 score = 0;
        positive const dmers = k - ZSTD_TRAIN_DMER + 1;

        if (stop - from <= k)
                return from;
        for (positive at = from; at + k <= stop; at++)
        {
                if (at == from)
                {
                        for (positive u = 0; u < dmers; u++)
                        {
                                positive const h = zstd_train_hash(set->bytes + from + u);

                                if (!active[h]++)
                                        score += freq[h];
                        }
                }
                else
                {
                        positive const out = zstd_train_hash(set->bytes + at - 1);
                        positive const in = zstd_train_hash(set->bytes + at + dmers - 1);

                        if (!--active[out])
                                score -= freq[out];
                        if (!active[in]++)
                                score += freq[in];
                }
                if (score > best_score)
                {
                        best_score = score;
                        best = at;
                }
        }
        /* the last window is still counted in active: clear it */
        for (positive u = 0; u < dmers; u++)
                active[zstd_train_hash(set->bytes + stop - k + u)] = 0;
        return best;
}

/* A dictionary's content of at most budget bytes, into out; its size. */
static positive zstd_train_content(const zstd_train_set address_to set, positive k,
                                   positive budget, p8 address_to out)
{
        p32 address_to const freq = memory_checked((positive)4 << ZSTD_TRAIN_LOG);
        p32 address_to const seen = memory_checked((positive)4 << ZSTD_TRAIN_LOG);
        p8 address_to const active = memory_checked((positive)1 << ZSTD_TRAIN_LOG);
        positive tail = budget;
        positive epochs;
        positive epoch_size;
        positive at = 0;

        if (!freq || !seen || !active)
                return 0;
        for (positive sample = 0; sample < set->count; sample++)
        {
                positive const size = set->sizes[sample];

                for (positive u = 0; u + ZSTD_TRAIN_DMER <= size; u++)
                {
                        positive const h = zstd_train_hash(set->bytes + at + u);

                        if (seen[h] != sample + 1)
                        {
                                seen[h] = (p32)(sample + 1);
                                freq[h]++;
                        }
                }
                at += size;
        }
        if (k > set->total)
                k = set->total;
        if (k < 2 * ZSTD_TRAIN_DMER)
                k = 2 * ZSTD_TRAIN_DMER;
        epochs = budget / k;
        if (!epochs)
                epochs = 1;
        epoch_size = set->total / epochs;
        if (epoch_size < k)
                epoch_size = k;
        for (positive epoch = 0; epoch < epochs && tail >= k; epoch++)
        {
                positive const from = epoch * epoch_size;
                positive const stop = epoch + 1 == epochs || from + epoch_size > set->total
                                          ? set->total
                                          : from + epoch_size;
                positive best;

                if (from + k > set->total)
                        break;
                best = zstd_train_segment(set, from, stop, k, freq, active);
                tail -= k;
                memory_copy_apart(out + tail, set->bytes + best, k);
                for (positive u = 0; u + ZSTD_TRAIN_DMER <= k; u++)
                        freq[zstd_train_hash(set->bytes + best + u)] = 0;
        }
        memory_free(freq, (positive)4 << ZSTD_TRAIN_LOG);
        memory_free(seen, (positive)4 << ZSTD_TRAIN_LOG);
        memory_free(active, (positive)1 << ZSTD_TRAIN_LOG);
        if (tail)
                memory_copy(out, out + tail, budget - tail);
        return budget - tail;
}

/* The bytes of a dictionary before its content: magic, ID, tables and
   repeat offsets, from the counts of what samples parsed against content
   of content_size bytes used.  Its size, or 0 when a table cannot be
   written. */
static positive zstd_train_header(zstd_train_stats address_to stats, positive content_size,
                                  p32 id, p8 address_to out)
{
        static const p8 fse_symbols[3] = {35, 31, 52};
        static const p8 fse_logs[3] = {9, 8, 9};
        p8 weight[256];
        p8 length[256];
        positive max_bits;
        p8 tree[256];
        positive head;
        positive at = 8;
        p64 bits;
        p32 count[3][53];
        positive const of_max = zstd_highbit32((p32)(content_size + (128u << 10))) > 30
                                    ? 30
                                    : zstd_highbit32((p32)(content_size + (128u << 10)));

        memory_store_unaligned(p32, out, ZSTD_DICT_MAGIC);
        memory_store_unaligned(p32, out + 4, id);
        for (positive s = 0; s < 256; s++)
                if (!stats->lit[s])
                        stats->lit[s] = 1;
        zstd_huffman_shape(stats->lit, 255, length, weight, address_of max_bits, tree,
                           address_of head, address_of bits);
        if (!head)
                return 0;
        memory_copy_apart(out + at, tree, head);
        at += head;
        memory_fill(count, 0, sizeof(count));
        memory_copy_apart(count[0], stats->ll, sizeof(stats->ll));
        memory_copy_apart(count[1], stats->of, sizeof(stats->of));
        memory_copy_apart(count[2], stats->ml, sizeof(stats->ml));
        for (positive k = 0; k < 3; k++)
        {
                static const p8 order[3] = {1, 2, 0};
                positive const kind = order[k];
                positive const top = kind == 1 ? of_max : fse_symbols[kind];
                bipolar norm[53];
                positive total = 0;
                positive last = 0;
                p8 log;
                positive described;

                for (positive s = 0; s <= top; s++)
                {
                        if (!count[kind][s])
                                count[kind][s] = 1;
                        total += count[kind][s];
                }
                log = zstd_fse_log(total, top, fse_logs[kind]);
                memory_fill(norm, 0, sizeof(norm));
                if (!zstd_normalize(count[kind], top + 1, total, log, total >= 2048, norm,
                                    address_of last))
                        return 0;
                described = zstd_write_norm(out + at, norm, last, log);
                if (!described)
                        return 0;
                at += described;
        }
        memory_store_unaligned(p32, out + at, 1);
        memory_store_unaligned(p32, out + at + 4, 4);
        memory_store_unaligned(p32, out + at + 8, 8);
        return at + 12;
}

/* What samples, parsed with content as the dictionary, count. */
static bool zstd_train_count(const zstd_train_set address_to set, p8 address_to content,
                             positive content_size, zstd_train_stats address_to stats)
{
        positive at = 0;
        zstd_params params;

        zstd_dict_on = true;
        zstd_dict_entropy = false;
        zstd_dict_content = content;
        zstd_dict_size = content_size;
        zstd_dict_rep[0] = 1;
        zstd_dict_rep[1] = 4;
        zstd_dict_rep[2] = 8;
        zstd_train_sink = stats;
        zstd_single_job = true;
        zstd_out_fd = -1;
        zstd_output.bytes = null;
        for (positive i = 0; i < set->count; i++)
        {
                zstd_level_params(3, 0, set->sizes[i] + content_size, address_of params);
                if (!zstd_encode_start(address_of params, false) ||
                    !zstd_encode_write(set->bytes + at, set->sizes[i]) || !zstd_encode_end())
                {
                        zstd_train_sink = null;
                        return false;
                }
                at += set->sizes[i];
        }
        zstd_train_sink = null;
        return true;
}

/* The bytes a sample of the set takes packed with the dictionary in bytes. */
static positive zstd_train_score(const zstd_train_set address_to set, p8 address_to dict,
                                 positive size)
{
        positive at = 0;
        positive total = 0;
        positive const step = set->count > 64 ? set->count / 64 : 1;
        p8 address_to const room = memory_checked(ZSTD_TRAIN_SAMPLE + 4096);
        zstd_params params;

        if (!room || !zstd_dictionary_load(dict, size))
                return (positive)-1;
        zstd_single_job = true;
        for (positive i = 0; i < set->count; i++)
        {
                if (i % step == 0)
                {
                        zstd_output.bytes = room;
                        zstd_output.room = ZSTD_TRAIN_SAMPLE + 4096;
                        zstd_output.used = 0;
                        zstd_out_fd = -1;
                        zstd_level_params(3, 0, set->sizes[i] + zstd_dict_size, address_of params);
                        if (!zstd_encode_start(address_of params, false) ||
                            !zstd_encode_write(set->bytes + at, set->sizes[i]) || !zstd_encode_end())
                                total = (positive)-1;
                        else
                                total += zstd_output.used;
                }
                at += set->sizes[i];
        }
        zstd_output.bytes = null;
        memory_free(room, ZSTD_TRAIN_SAMPLE + 4096);
        return total;
}

static b32 zstd_cli_train(void)
{
        string_address paths[4096];
        positive nfiles = 0;
        string_address output = "dictionary";
        positive maxdict = 112640;
        p32 forced_id = 0;
        positive const count = (positive)program_argument_count();
        zstd_train_set set = {0};
        p8 address_to best = null;
        positive best_size = 0;
        positive best_score = (positive)-1;
        zstd_train_stats address_to stats;
        bool quiet = false;

        for (positive i = 1; i < count; i++)
        {
                string_address const word = program_argument((b32)i);
                positive value = 0;

                if (string_equals(word, "--"))
                {
                        for (i++; i < count && nfiles < 4096; i++)
                                paths[nfiles++] = program_argument((b32)i);
                        break;
                }
                if (string_has_prefix(word, "--train"))
                        continue;
                if (string_equals(word, "-o") && i + 1 < count)
                        output = program_argument((b32)++i);
                else if (word[0] == '-' && word[1] == 'o' && word[2])
                        output = word + 2;
                else if (string_has_prefix(word, "--maxdict=") &&
                         zstd_cli_number(word + 10, address_of value))
                        maxdict = value;
                else if (string_has_prefix(word, "--dictID=") &&
                         zstd_cli_number(word + 9, address_of value))
                        forced_id = (p32)value;
                else if (string_equals(word, "-q") || string_equals(word, "--quiet"))
                        quiet = true;
                else if (word[0] == '-' && word[1])
                        continue;
                else if (nfiles < 4096)
                        paths[nfiles++] = word;
        }
        if (!nfiles)
        {
                string_format(log_error, "zstd: error 14 : nothing to train on \n");
                return 1;
        }
        if (maxdict < 256)
        {
                string_format(log_error, "zstd: error 25 : dictionary size %p is too small \n", maxdict);
                return 1;
        }
        if (maxdict > ((positive)1 << 30))
                maxdict = (positive)1 << 30;
        if (!zstd_train_load(paths, nfiles, address_of set))
                return 1;
        if (!quiet && maxdict / 10 > set.total)
                string_format(log_error,
                              "WARNING: The maximum dictionary size %p is too large compared to the "
                              "source size %p! This may lead to a subpar dictionary! We recommend "
                              "training on sources at least 10x, and preferably 100x the size of the "
                              "dictionary! \n",
                              maxdict, set.total);
        stats = memory_checked(sizeof(zstd_train_stats));
        best = memory_checked(maxdict + 4096);
        if (!stats || !best)
                return 1;
        zstd_cli_no_dict_id = false;
        for (positive attempt = 0; attempt < 2; attempt++)
        {
                positive const k = attempt ? 256 : 1024;
                p8 address_to const dict = memory_checked(maxdict + 4096);
                p8 address_to const content = memory_checked(maxdict + 4096);
                positive content_size;
                positive header;
                positive dict_size;
                p64 hash_id;
                p32 id;

                if (!dict || !content)
                        return 1;
                content_size = zstd_train_content(address_of set, k, maxdict > 768 ? maxdict - 384 : maxdict / 2, content);
                if (!content_size)
                        continue;
                memory_fill(stats, 0, sizeof(zstd_train_stats));
                if (!zstd_train_count(address_of set, content, content_size, stats))
                        continue;
                {
                        hash_xxh64_begin(address_of zstd_enc_hash, 0);
                        hash_xxh64_add(address_of zstd_enc_hash, content, content_size);
                        hash_id = hash_xxh64_finish(address_of zstd_enc_hash);
                        id = forced_id ? forced_id : (p32)(hash_id % (((p64)1 << 31) - 32768)) + 32768;
                }
                header = zstd_train_header(stats, content_size, id, dict);
                if (!header)
                        continue;
                if (header + content_size > maxdict)
                {
                        positive const drop = header + content_size - maxdict;

                        content_size -= drop;
                        memory_copy(content, content + drop, content_size);
                }
                memory_copy(dict + header, content, content_size);
                dict_size = header + content_size;
                {
                        positive const score = zstd_train_score(address_of set, dict, dict_size);

                        if (score < best_score)
                        {
                                best_score = score;
                                memory_copy(best, dict, dict_size);
                                best_size = dict_size;
                        }
                }
        }
        zstd_dict_on = false;
        if (!best_size)
        {
                string_format(log_error, "zstd: error 20 : dictionary training failed \n");
                return 1;
        }
        {
                bipolar const handle = system_open_at_mode(AT_FDCWD, output, FILE_WRITE, 0644);

                if (handle < 0 || system_write_all((positive)handle, best, best_size) != best_size)
                {
                        string_format(log_error, "zstd: error 31 : cannot write dictionary %s \n", output);
                        return 1;
                }
                system_close(handle);
        }
        if (!quiet)
                string_format(log_error, "Save dictionary of size %p into file %s \n", best_size, output);
        return 0;
}

static b32 file_zstd(void)
{
        for (positive i = 1; i < (positive)program_argument_count(); i++)
        {
                string_address const word = program_argument((b32)i);

                if (string_equals(word, "--"))
                        break;
                if (string_has_prefix(word, "--train"))
                        return zstd_cli_train();
        }
        file_codec_cli codec = {
            .name = "zstd", .decode_name = "unzstd", .cat_name = "zstdcat",
            .usage =
                "Usage: zstd [-cdfkqzC#] [-T#] [--ultra] [--fast[=#]] [--long[=#]]\n"
                "            [--single-thread] [--no-check] [-D DICT] [--no-dictID]\n"
                "            [-o FILE] [--rm] [FILE...]\n"
                "       zstd --train FILE... [-o DICT] [--maxdict=#] [--dictID=#]",
            .version = "zstd from moonwater",
            .status = address_of zstd_status, .suffixes = zstd_suffixes,
            .suffix_count = array_count(zstd_suffixes),
            .decode_suffix_error = "cannot guess output name; use -c or -o",
            .encode_suffix_error = "cannot guess output name; use -c or -o",
            .features = FILE_CODEC_COMPRESS_OPTION |
                        FILE_CODEC_OUTPUT_OPTION | FILE_CODEC_REMOVE_OPTION |
                        FILE_CODEC_LONG_QUIET | FILE_CODEC_SHORT_VERSION,
            .level = 3,
            .run = zstd_cli_stream,
            .option = zstd_cli_option};
        return file_codec_main(address_of codec);
}

#endif /* ZSTD_CORE_ONLY */
