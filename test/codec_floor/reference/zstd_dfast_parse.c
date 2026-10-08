/* Semantic reference for zstd_dfast_parse: libzstd 1.5's double-fast strategy
   over [from, to) of a block, as the encoder had it in C before the routine.
   Like zstd_parse_fast_reference it is written over zstd.c's own pieces:
   zstd_hash_bytes, zstd_common, zstd_store. e is the encoder: base, hash (the
   long table), chain (the short one), seqs and nseq, lits and nlit, freq, rep,
   and the level's hash_log, chain_log and min_match. The answer is where the
   literals not yet stored begin. test/checks.c includes this file as its
   model and compares everything the routine leaves. */
static p8 address_to zstd_parse_dfast_reference(zstd_encoder address_to e, p32 from,
                                      p32 to, p32 low)
{
        p8 address_to const base = e->base;
        p32 address_to const longer = e->hash;
        p32 address_to const shorter = e->chain;
        p8 const hlog = e->p.hash_log;
        p8 const slog = e->p.chain_log;
        p8 const mls = e->p.min_match < 4 ? 4 : e->p.min_match > 8 ? 8 : e->p.min_match;
        p8 address_to const lowest = base + low;
        p8 address_to const iend = base + to;
        p8 address_to const ilimit = iend - 8;
        p8 address_to ip = base + from;
        p8 address_to anchor = ip;
        p32 rep = e->rep[0];

        ip += ip == lowest;
        if (rep > (p32)(ip - lowest))
                rep = 0;
        for (;;)
        {
                positive step = 1;
                p8 address_to next_step = ip + 256;
                p8 address_to ip1 = ip + step;
                p8 address_to there;
                positive hl0;
                positive hl1;
                positive distance;
                positive match;
                p32 idxl0;
                p32 idxl1;
                p32 cur;

                if (ip1 > ilimit)
                        break;
                hl0 = zstd_hash_bytes(ip, hlog, 8);
                idxl0 = longer[hl0];
                for (;;)
                {
                        positive const hs0 = zstd_hash_bytes(ip, slog, mls);
                        p32 const idxs0 = shorter[hs0];

                        cur = (p32)(ip - base);
                        longer[hl0] = shorter[hs0] = cur;
                        if (rep && memory_load_unaligned(p32, ip + 1 - rep) ==
                                       memory_load_unaligned(p32, ip + 1))
                        {
                                match = 4 + zstd_common(ip + 5, ip + 5 - rep,
                                                                 (positive)(iend - ip - 5));
                                ip++;
                                zstd_store(e, anchor, (positive)(ip - anchor), rep, match);
                                goto stored;
                        }
                        hl1 = zstd_hash_bytes(ip1, hlog, 8);
                        if (idxl0 > low &&
                            memory_load_unaligned(p64, base + idxl0) == memory_load_unaligned(p64, ip))
                        {
                                there = base + idxl0;
                                match = 8 + zstd_common(ip + 8, there + 8,
                                                                 (positive)(iend - ip - 8));
                                distance = (positive)(ip - there);
                                while (ip > anchor && there > lowest && ip[-1] == there[-1])
                                        ip--, there--, match++;
                                goto found;
                        }
                        idxl1 = longer[hl1];
                        if (idxs0 > low &&
                            memory_load_unaligned(p32, base + idxs0) == memory_load_unaligned(p32, ip))
                        {
                                there = base + idxs0;
                                match = 4 + zstd_common(ip + 4, there + 4,
                                                                 (positive)(iend - ip - 4));
                                distance = (positive)(ip - there);
                                if (idxl1 > low &&
                                    memory_load_unaligned(p64, base + idxl1) ==
                                        memory_load_unaligned(p64, ip1))
                                {
                                        positive const length =
                                            8 + zstd_common(ip1 + 8, base + idxl1 + 8,
                                                                     (positive)(iend - ip1 - 8));

                                        if (length > match)
                                        {
                                                ip = ip1;
                                                match = length;
                                                there = base + idxl1;
                                                distance = (positive)(ip - there);
                                        }
                                }
                                while (ip > anchor && there > lowest && ip[-1] == there[-1])
                                        ip--, there--, match++;
                                goto found;
                        }
                        if (ip1 >= next_step)
                        {
                                __builtin_prefetch(ip1 + 64);
                                __builtin_prefetch(ip1 + 128);
                                step++;
                                next_step += 256;
                        }
                        ip = ip1;
                        ip1 += step;
                        hl0 = hl1;
                        idxl0 = idxl1;
                        if (ip1 > ilimit)
                                return anchor;
                }
        found:
                if (step < 4)
                        longer[hl1] = (p32)(ip1 - base);
                zstd_store(e, anchor, (positive)(ip - anchor), distance, match);
        stored:
                ip += match;
                anchor = ip;
                if (ip <= ilimit)
                {
                        p32 const at = cur + 2;

                        longer[zstd_hash_bytes(base + at, hlog, 8)] = at;
                        longer[zstd_hash_bytes(ip - 2, hlog, 8)] = (p32)(ip - 2 - base);
                        shorter[zstd_hash_bytes(base + at, slog, mls)] = at;
                        shorter[zstd_hash_bytes(ip - 1, slog, mls)] = (p32)(ip - 1 - base);
                        while (ip <= ilimit && e->rep[1] &&
                               e->rep[1] <= (p32)(ip - lowest) &&
                               memory_load_unaligned(p32, ip) ==
                                   memory_load_unaligned(p32, ip - e->rep[1]))
                        {
                                positive const length =
                                    4 + zstd_common(ip + 4, ip + 4 - e->rep[1],
                                                             (positive)(iend - ip - 4));

                                shorter[zstd_hash_bytes(ip, slog, mls)] = (p32)(ip - base);
                                longer[zstd_hash_bytes(ip, hlog, 8)] = (p32)(ip - base);
                                zstd_store(e, ip, 0, e->rep[1], length);
                                ip += length;
                                anchor = ip;
                        }
                }
                rep = e->rep[0];
        }
        return anchor;
}
