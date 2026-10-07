/* Semantic reference for zstd_fast_parse: libzstd 1.5's fast strategy over
   [from, to) of a block, as the encoder had it in C before the routine. It
   is written over zstd.c's own pieces, which are what the routine folds in:
   zstd_hash_bytes (the first mls bytes shifted to the top and multiplied by
   0x9E3779B185EBCA87, taken from the top hlog bits; the routine multiplies by
   the constant shifted instead, the same product), zstd_common (how far two
   places agree), zstd_store (the sequence record, the repeat-offset coding of
   RFC 8878, the sixteen-byte literal copy and the code counts) and
   zstd_repeat_run (matches at the second repeat offset right where a match
   ended). e is the encoder: base, hash, seqs and nseq, lits and nlit, freq,
   rep, and the level's hash_log, min_match and target_length. The answer is
   where the literals not yet stored begin; the caller copies them. test/checks.c
   includes this file as its model and compares everything the routine
   leaves. */
static p8 address_to zstd_parse_fast_reference(zstd_encoder address_to e, p32 from, p32 to, p32 low)
{
        p8 address_to const base = e->base;
        p32 address_to const table = e->hash;
        p8 const hlog = e->p.hash_log;
        p8 const mls = e->p.min_match < 4 ? 4 : e->p.min_match > 8 ? 8 : e->p.min_match;
        positive const step_size = (positive)e->p.target_length + !e->p.target_length + 1;
        p8 address_to const lowest = base + low;
        p8 address_to const iend = base + to;
        p8 address_to const ilimit = iend - 8;
        p8 address_to ip0 = base + from;
        p8 address_to anchor = ip0;
        p32 rep = e->rep[0];

        ip0 += ip0 == lowest;
        if (rep > (p32)(ip0 - lowest))
                rep = 0;
        for (;;)
        {
                positive step = step_size;
                p8 address_to next_step = ip0 + 128;
                p8 address_to ip1 = ip0 + 1;
                p8 address_to ip2 = ip0 + step;
                p8 address_to ip3 = ip2 + 1;
                p8 address_to match0;
                positive h0, h1, distance, length;
                p32 candidate, cur0;

                if (ip3 >= ilimit)
                        break;
                h0 = zstd_hash_bytes(ip0, hlog, mls);
                h1 = zstd_hash_bytes(ip1, hlog, mls);
                candidate = table[h0];
                for (;;)
                {
                        p32 const rval = memory_load_unaligned(p32, ip2 - rep);

                        cur0 = (p32)(ip0 - base);
                        table[h0] = cur0;
                        if (rep && memory_load_unaligned(p32, ip2) == rval)
                        {
                                ip0 = ip2;
                                match0 = ip0 - rep;
                                length = ip0[-1] == match0[-1];
                                ip0 -= length;
                                match0 -= length;
                                length += 4;
                                distance = rep;
                                table[h1] = (p32)(ip1 - base);
                                goto matched;
                        }
                        if (candidate >= low &&
                            memory_load_unaligned(p32, ip0) == memory_load_unaligned(p32, base + candidate))
                        {
                                table[h1] = (p32)(ip1 - base);
                                goto found;
                        }
                        candidate = table[h1];
                        h0 = h1;
                        h1 = zstd_hash_bytes(ip2, hlog, mls);
                        ip0 = ip1;
                        ip1 = ip2;
                        ip2 = ip3;
                        cur0 = (p32)(ip0 - base);
                        table[h0] = cur0;
                        if (candidate >= low &&
                            memory_load_unaligned(p32, ip0) == memory_load_unaligned(p32, base + candidate))
                        {
                                if (step <= 4)
                                        table[h1] = (p32)(ip1 - base);
                                goto found;
                        }
                        candidate = table[h1];
                        h0 = h1;
                        h1 = zstd_hash_bytes(ip2, hlog, mls);
                        ip0 = ip1;
                        ip1 = ip2;
                        ip2 = ip0 + step;
                        ip3 = ip1 + step;
                        if (ip2 >= next_step)
                        {
                                step++;
                                next_step += 128;
                        }
                        if (ip3 >= ilimit)
                                return anchor;
                }
        found:
                match0 = base + candidate;
                distance = (positive)(ip0 - match0);
                length = 4;
                while (ip0 > anchor && match0 > lowest && ip0[-1] == match0[-1])
                        ip0--, match0--, length++;
        matched:
                length += zstd_common(ip0 + length, match0 + length, (positive)(iend - ip0 - length));
                zstd_store(e, anchor, (positive)(ip0 - anchor), distance, length);
                ip0 += length;
                anchor = ip0;
                if (ip0 <= ilimit)
                {
                        table[zstd_hash_bytes(base + cur0 + 2, hlog, mls)] = cur0 + 2;
                        table[zstd_hash_bytes(ip0 - 2, hlog, mls)] = (p32)(ip0 - 2 - base);
                        ip0 = zstd_repeat_run(e, ip0, ilimit + 1, iend, low, table, hlog, mls);
                        anchor = ip0;
                }
                rep = e->rep[0];
        }
        return anchor;
}
