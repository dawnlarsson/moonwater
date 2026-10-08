/* Semantic reference for zstd_sequences_decode: one block's sequence bitstream
   read a bit at a time, as RFC 8878 has it, into records. It is written over
   zstd.c's own pieces: zstd_seq_decode_job is the routine's job and
   zstd_fse the table whose cells it indexes. The stream is read from its end:
   the sentinel is the highest set bit of the last byte, the first bit read is
   the one below it, a field's first bit is its highest, and every bit down to
   the stream's first must be read by the last sequence -- fewer or more is a
   stream that is not what it says. The states start as the three tables'
   logs of bits in stream order (literal length, offset, match length); a
   sequence reads the offset's extra bits, the match length's, the literal
   length's, and all but the last then steps the literal length, match length
   and offset states in that order. An offset code of 0 or 1 is a repeat:
   which of the three, or the first less one, is the cell's base plus its
   extra bit plus one when the literal length is zero; any other code is the
   offset itself. The three repeat offsets start as the job's stand-ins
   (0xFFF0FFFF, 0xFFF1FFFF, 0xFFF2FFFF) and whatever is at least 0xFFF00000
   in a record is one; a real offset of that size, or an offset of zero,
   refuses. test/checks.c includes this file as its model and compares
   everything the routine leaves. */
static p8 zstd_decode_reference_bit(const p8 address_to seq, positive total, positive k)
{
        positive const g = total - 1 - k;

        return (seq[g / 8] >> (g % 8)) & 1;
}

static bipolar zstd_sequences_decode_reference(zstd_seq_decode_job address_to j)
{
        zstd_fse address_to const table[3] = {j->ll, j->of, j->ml};
        positive const len = j->seq_len;
        p32 state[3];
        p32 rep[3] = {j->rep[0], j->rep[1], j->rep[2]};
        positive total;
        positive used = 0;
        bool over = false;

        j->max_off = 0;
        j->sym_end = 0;
        j->sum_ll = 0;
        j->sum_ml = 0;
        if (!len || !j->seq[len - 1] || !j->nseq)
                return -1;
        {
                p8 top = 7;

                while (!((j->seq[len - 1] >> top) & 1))
                        top--;
                total = 8 * (len - 1) + top;
        }
#define DECODE_TAKE(into, n)                                                   \
        do                                                                     \
        {                                                                      \
                p64 value_ = 0;                                                \
                for (positive i_ = 0; i_ < (n); i_++)                          \
                {                                                              \
                        over |= used >= total;                                 \
                        value_ = value_ << 1 |                                 \
                                 (used < total ? zstd_decode_reference_bit(j->seq, total, used) : 0); \
                        used++;                                                \
                }                                                              \
                into = value_;                                                 \
        } while (0)
        for (positive k = 0; k < 3; k++)
                DECODE_TAKE(state[k], table[k]->log);
        for (positive i = 0; i < j->nseq; i++)
        {
                zstd_fse_cell const of = table[1]->cell[state[1]];
                zstd_fse_cell const ml = table[2]->cell[state[2]];
                zstd_fse_cell const ll = table[0]->cell[state[0]];
                p64 extra;
                p32 value, match, literal, offset;

                DECODE_TAKE(extra, of.extra);
                value = of.base + (p32)extra;
                DECODE_TAKE(extra, ml.extra);
                match = ml.base + (p32)extra;
                DECODE_TAKE(extra, ll.extra);
                literal = ll.base + (p32)extra;
                if (of.extra > 1)
                {
                        if (value >= 0xFFF00000u)
                                return -1;
                        offset = value;
                        rep[2] = rep[1];
                        rep[1] = rep[0];
                        rep[0] = offset;
                }
                else
                {
                        positive const which = value + (literal == 0);

                        if (which == 0)
                                offset = rep[0];
                        else if (which == 1)
                        {
                                offset = rep[1];
                                rep[1] = rep[0];
                                rep[0] = offset;
                        }
                        else
                        {
                                offset = which == 2 ? rep[2] : rep[0] - 1;
                                rep[2] = rep[1];
                                rep[1] = rep[0];
                                rep[0] = offset;
                        }
                }
                if (!offset)
                        return -1;
                if (offset >= 0xFFF00000u)
                        j->sym_end = i + 1;
                else if (offset > j->max_off)
                        j->max_off = offset;
                j->out[3 * i] = literal;
                j->out[3 * i + 1] = match;
                j->out[3 * i + 2] = offset;
                j->sum_ll += literal;
                j->sum_ml += match;
                if (i + 1 < j->nseq)
                {
                        p64 bits;

                        DECODE_TAKE(bits, ll.bits);
                        state[0] = ll.next + (p32)bits;
                        DECODE_TAKE(bits, ml.bits);
                        state[2] = ml.next + (p32)bits;
                        DECODE_TAKE(bits, of.bits);
                        state[1] = of.next + (p32)bits;
                }
        }
#undef DECODE_TAKE
        if (over || used != total)
                return -1;
        j->rep[0] = rep[0];
        j->rep[1] = rep[1];
        j->rep[2] = rep[2];
        return 0;
}
