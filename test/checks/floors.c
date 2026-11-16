/*
        test/checks/floors.c: the floors: compression, the hardware, libc and inlining.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#if defined(CHECK_compression_floor) || defined(BENCH_compression_floor)
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define GZIP_CORE_ONLY
#include "../../src/sh/gzip.c"
#undef GZIP_CORE_ONLY
#define XZ_CORE_ONLY
#include "../../src/sh/xz.c"
#undef XZ_CORE_ONLY
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p64 floor_crc(p64 crc, p8 address_to data, positive n, p64 polynomial)
{
        for (positive i = 0; i < n; i++)
        {
                crc ^= data[i];
                for (positive j = 0; j < 8; j++)
                        crc = (crc >> 1) ^ ((0 - (crc & 1)) & polynomial);
        }
        return crc;
}

static p8 address_to floor_pages(positive pages)
{
        p8 address_to p = memory(pages * 4096);
        if (system_failed(p) || !p)
                return null;
        if (system_call_3(syscall(mprotect), (positive)p, 4096, 0) != 0 ||
            system_call_3(syscall(mprotect), (positive)(p + (pages - 1) * 4096),
                          4096, 0) != 0)
        {
                memory_free(p, pages * 4096);
                return null;
        }
        return p;
}

static fn floor_checksums(void)
{
        //      Two pages of data between the guards: the folds of 4, 8 and 16 lanes
        //      change over at 512 and 1280 bytes (arm64) and the zmm fold at 256 and 512 (x86_64).
        p8 address_to p = floor_pages(4);
        check("CRC protected mappings", p != null);
        if (!p) return;
        for (positive i = 0; i < 8192; i++) p[4096 + i] = (p8)(i * 37 + i / 11);
        const positive sizes[] = {0,1,2,3,4,5,6,7,8,9,15,16,17,31,32,33,63,64,
                                  65,127,128,129,191,192,255,256,257,383,384,511,512,513,767,768,769,1023,1024,1025,1039,1040,1055,1056,1087,1088,1089,
                                  1279,1280,1281,1535,1536,1537,2047,2048,2049,4095,4096,4097,8191,8192};
        p8 pclmul = cpu_has_pclmul;
#if X64
        p8 vpclmul = cpu_has_vpclmul;
#endif
        //      Path 0 is the table walk, 1 the xmm fold and 2 the zmm fold
        //      where the processor has one.
        for (positive path = 0; path < (pclmul ? 3 : 1); path++)
        {
        cpu_has_pclmul = path ? pclmul : 0;
#if X64
        cpu_has_vpclmul = path == 2 ? vpclmul : 0;
#endif
        for (positive k = 0; k < array_count(sizes); k++)
                for (positive edge = 0; edge < 2; edge++)
                        for (positive seed = 0; seed < 3; seed++)
                        {
                                positive n = sizes[k];
                                p8 address_to src = p + (edge ? 12288 - n : 4096);
                                p64 initial = seed == 0 ? 0 : seed == 1 ? ~(p64)0
                                                                       : 0x9b37216c8123abfeull;
                                p32 c32 = hash_crc32((p32)initial, src, n);
                                p64 c64 = hash_crc64(initial, src, n);
                                check("IEEE reflected CRC, seed/span/alignment",
                                      c32 == (p32)floor_crc((p32)initial, src, n, 0xedb88320u));
                                check("ECMA reflected CRC, seed/span/alignment",
                                      c64 == floor_crc(initial, src, n, 0xc96c5795d7870f42ull));
                                positive split = n / 2;
                                check("CRC continuation agrees across slices",
                                      c32 == hash_crc32(hash_crc32((p32)initial, src, split),
                                                        src + split, n - split) &&
                                      c64 == hash_crc64(hash_crc64(initial, src, split),
                                                        src + split, n - split));
                        }
        }
        cpu_has_pclmul = pclmul;
#if X64
        cpu_has_vpclmul = vpclmul;
#endif
        memory_free(p, 4 * 4096);
}

static fn floor_shift_reference(xz_range_state address_to s)
{
        p32 low = (p32)s->low;
        p8 carry = (p8)(s->low >> 32);
        if (low < 0xff000000u || carry)
        {
                p8 byte = (p8)s->cache;
                for (positive i = 0; i < s->pending; i++)
                {
                        if (s->next < s->limit) *s->next++ = (p8)(byte + carry);
                        else s->full = 1;
                        byte = 255;
                }
                s->cache = low >> 24;
                s->pending = 0;
        }
        s->pending++;
        s->low = (p32)(low << 8);
}

static fn floor_bit_reference(xz_range_state address_to s, p16 address_to prob, positive bit)
{
        p32 bound = (s->range >> 11) * *prob;
        if (bit)
        {
                s->low += bound;
                s->range -= bound;
                *prob -= *prob >> 5;
        }
        else
        {
                s->range = bound;
                *prob += (2048 - *prob) >> 5;
        }
        while (s->range < 0x1000000)
        {
                floor_shift_reference(s);
                s->range <<= 8;
        }
}

static fn floor_tree_reference(xz_range_state address_to s, p16 address_to probs,
                               positive value, positive mode)
{
        positive count = mode & 255;
        positive symbol = count ? 1 : 0;
        positive result = 0;
        p8 matched = (mode & 512) != 0;
        positive match = (mode >> 16) & 255;
        if (!count) count = 1;
        for (positive i = 0; i < count; i++)
        {
                positive bit = (value >> ((mode & 256) ? i : count - 1 - i)) & 1;
                positive index = symbol;
                if (mode & 256)
                        index = ((positive)1 << i) | result;
                else if (matched)
                {
                        positive predicted = (match >> (count - 1 - i)) & 1;
                        index += (1 + predicted) << 8;
                        matched = predicted == bit;
                }
                floor_bit_reference(s, probs + index, bit);
                symbol = (symbol << 1) | bit;
                result |= bit << i;
        }
}

static fn floor_range(void)
{
        static p8 actual[8192], expected[8192];
        static p16 ap[4096], ep[4096];
        p64 lows[] = {0,0xfeffffffu,0xff000000u,0xffffffffu,0x100000000ull,0x1ffffffffull};
        for (positive l = 0; l < array_count(lows); l++)
                for (positive pending = 1; pending <= 33; pending += 8)
                        for (positive cap = 0; cap <= 40; cap++)
                        {
                                memory_fill(actual, 0x5a, sizeof(actual));
                                memory_fill(expected, 0x5a, sizeof(expected));
                                xz_range_state a = {0x12345678u, 42, lows[l], pending, actual, actual + cap, 0};
                                xz_range_state e = a; e.next = expected; e.limit = expected + cap;
                                lzma_range_shift(address_of a);
                                floor_shift_reference(address_of e);
                                check("range carry, deferred bytes, exact capacity",
                                      a.range == e.range && a.cache == e.cache && a.low == e.low &&
                                      a.pending == e.pending && a.full == e.full &&
                                      a.next - actual == e.next - expected &&
                                      !memory_compare(actual, expected, sizeof(actual)));
                        }
        positive modes[512], values[512];
        p32 random = 0x79131415u;
        for (positive i = 0; i < 4096; i++) ap[i] = ep[i] = 1024;
        memory_fill(actual, 0, sizeof(actual)); memory_fill(expected, 0, sizeof(expected));
        xz_range_state a = {0xffffffffu,0,0,1,actual,actual + sizeof(actual),0};
        xz_range_state e = a; e.next = expected; e.limit = expected + sizeof(expected);
        for (positive i = 0; i < 512; i++)
        {
                XORSHIFT32(random);
                positive count = i % 12;
                modes[i] = i % 3 == 0 ? 8 | 512 | ((positive)(random & 255) << 16)
                            : i % 3 == 1 && count ? count | 256 : count;
                values[i] = (random >> 8) & (((positive)1 << (count ? count : 1)) - 1);
                if ((modes[i] & 255) == 8) values[i] &= 255;
                lzma_range_encode(address_of a, ap, values[i], modes[i]);
                floor_tree_reference(address_of e, ep, values[i], modes[i]);
                check("range trees retain exactly the scalar probability state",
                      a.range == e.range && a.low == e.low && a.cache == e.cache &&
                      a.pending == e.pending && a.full == e.full &&
                      a.next - actual == e.next - expected &&
                      !memory_compare(actual, expected, sizeof(actual)) &&
                      !memory_compare(ap, ep, sizeof(ap)));
        }
        for (positive i = 0; i < 5; i++) lzma_range_shift(address_of a);
        xz_range_input decoded = {0xffffffffu,0,actual + 5,a.next};
        for (positive i = 1; i < 5; i++) decoded.code = (decoded.code << 8) | actual[i];
        for (positive i = 0; i < 4096; i++) ep[i] = 1024;
        for (positive i = 0; i < 512; i++)
                check("range literal/reverse/tree decode", lzma_range_decode(address_of decoded, ep, modes[i]) == (bipolar)values[i]);
        check("range encode/decode models agree", !memory_compare(ap, ep, sizeof(ap)));
        p8 address_to guard = floor_pages(3);
        check("range decode guarded input", guard != null);
        if (guard)
        {
                xz_range_input short_input = {1,0,guard + 8192,guard + 8192};
                p16 prob = 1024;
                check("range exhaustion touches no protected input or probability",
                      lzma_range_decode(address_of short_input, address_of prob, 0) < 0 && prob == 1024);
                memory_free(guard, 3 * 4096);
        }
}

static fn floor_huffman(void)
{
        p8 address_to input = floor_pages(3);
        p8 address_to output = floor_pages(3);
        p32 table[256];
        check("Huffman guarded mappings", input && output);
        if (!input || !output) return;
        for (positive i = 0; i < 256; i++) table[i] = (8u << 16) | i;
        for (positive n = 0; n <= 1024; n++)
        {
                p8 address_to src = input + 8192 - n;
                p8 address_to dst = output + 8192 - n - 1;
                for (positive i = 0; i < n; i++) src[i] = (p8)(i * 17 + n);
                dst[-1] = 0x5a;
                positive got = huffman_encode_back(dst, src, n, table);
                p8 okay = got == n + 1 && dst[n] == 1 && dst[-1] == 0x5a;
                for (positive i = 0; i < n; i++) okay &= dst[i] == src[n - 1 - i];
                check("backward stream exact byte/word ends and alignments", okay);
        }
        memory_free(input, 3 * 4096); memory_free(output, 3 * 4096);
}

/* Guard geometry follows the machine's page; static buffers take a constant
   bound no smaller than any page the native harness runs on. */
#ifndef FLOOR_PAGE
#define FLOOR_PAGE 4096
#endif
#ifndef FLOOR_PAGE_STATIC
#define FLOOR_PAGE_STATIC 4096
#endif

/* One-bit codes over all-zero input: code 0 is the symbol, code 1 unused. */
static fn floor_deflate_one_bit(p32 address_to lit, p32 lit_cell, p32 address_to dist, p32 dist_cell)
{
        for (positive i = 0; i < 2048; i++)
                lit[i] = (i & 1) ? GZIP_CELL_EXCEPTIONAL : lit_cell;
        for (positive i = 0; i < 256; i++)
                dist[i] = (i & 1) ? GZIP_CELL_EXCEPTIONAL : dist_cell;
}

static fn floor_deflate(void)
{
        p8 address_to input = floor_pages(3);
        p8 address_to output = floor_pages(11);
        static p32 lit[GZIP_LITLEN_CELLS], dist[GZIP_OFFSET_CELLS];
        static p8 before[2 * GZIP_SPAN_OUT];
        check("deflate guarded mappings", input && output);
        if (!input || !output) return;
        check("deflate span ABI", sizeof(gzip_decode_job) == 88 &&
              __builtin_offsetof(gzip_decode_job, window) == 48 &&
              __builtin_offsetof(gzip_decode_job, status) == 80);
        p8 address_to stop = input + 2 * FLOOR_PAGE;
        p8 address_to guard = output + 10 * FLOOR_PAGE;
        memory_fill(input + FLOOR_PAGE, 0, FLOOR_PAGE);
        for (positive i = FLOOR_PAGE; i < 10 * FLOOR_PAGE; i++) output[i] = (p8)(i * 13 + i / 29);
        for (positive l = 0; l < 29; l++)
                for (positive d = 0; d < 30; d++)
                        for (positive residue = 0; residue < 8; residue++)
                        {
                                floor_deflate_one_bit(lit, gzip_cell(1, 257 + l, 0, 1 + gzip_len_extra[l]), dist, gzip_cell(2, d, 0, 1));
                                p8 address_to dst = guard - GZIP_SPAN_OUT - residue;
                                p8 address_to next = stop - GZIP_SPAN_IN - residue;
                                p8 want[258];
                                positive distance = gzip_dist_base[d], length = gzip_len_base[l];
                                positive used = 2 + gzip_len_extra[l] + gzip_dist_extra[d];
                                for (positive i = 0; i < length; i++)
                                        want[i] = i < distance ? dst[(bipolar)i - (bipolar)distance] : want[i - distance];
                                gzip_decode_job job = {0, 0, next, stop, dst, guard, dst - distance, lit, dist, gzip_extra_masks, 5};
                                deflate_decode_span(address_of job);
                                check("deflate length/distance/alignment at both guards, exact bit handoff",
                                      job.status == 0 && job.out == dst + length &&
                                      !memory_compare(dst, want, length) && job.next <= stop &&
                                      job.count < 64 && !(job.bits >> job.count) &&
                                      (positive)(job.next - next) * 8 - job.count == used);
                                memory_copy_apart(before, dst, GZIP_SPAN_OUT + residue);
                                job = (gzip_decode_job){0, 0, next, stop, dst, guard, dst - distance + 1, lit, dist, gzip_extra_masks, 0};
                                deflate_decode_span(address_of job);
                                check("deflate distance past the window stops before writing",
                                      job.status == 4 && job.out == dst &&
                                      !memory_compare(dst, before, GZIP_SPAN_OUT + residue));
                        }
        p8 address_to dst = guard - GZIP_SPAN_OUT;
        for (positive n = 0; n < GZIP_SPAN_IN; n++)
        {
                gzip_decode_job job = {0, 0, stop - n, stop, dst, guard, dst, lit, dist, gzip_extra_masks, 5};
                deflate_decode_span(address_of job);
                check("deflate short lookahead consumes nothing and never crosses the input guard",
                      job.status == 0 && job.out == dst && job.next == stop - n && job.count == 0);
        }
        for (positive n = 0; n < GZIP_SPAN_OUT; n++)
        {
                gzip_decode_job job = {0, 0, stop - 64, stop, guard - n, guard, dst, lit, dist, gzip_extra_masks, 5};
                deflate_decode_span(address_of job);
                check("deflate short output room writes nothing",
                      job.status == 0 && job.out == guard - n && job.next == stop - 64 && job.count == 0);
        }
        floor_deflate_one_bit(lit, GZIP_CELL_EXCEPTIONAL, dist, gzip_cell(2, 0, 0, 1));
        gzip_decode_job bad = {0, 0, stop - 64, stop, dst, guard, dst - 1, lit, dist, gzip_extra_masks, 0};
        deflate_decode_span(address_of bad);
        check("deflate unused literal/length code stops", bad.status == 2 && bad.out == dst);
        floor_deflate_one_bit(lit, gzip_cell(1, 286, 0, 1), dist, gzip_cell(2, 0, 0, 1));
        bad = (gzip_decode_job){0, 0, stop - 64, stop, dst, guard, dst - 1, lit, dist, gzip_extra_masks, 0};
        deflate_decode_span(address_of bad);
        check("deflate length symbol 286 stops", bad.status == 2 && bad.out == dst);
        floor_deflate_one_bit(lit, gzip_cell(1, 257, 0, 1), dist, gzip_cell(2, 30, 0, 1));
        bad = (gzip_decode_job){0, 0, stop - 64, stop, dst, guard, dst - 1, lit, dist, gzip_extra_masks, 0};
        deflate_decode_span(address_of bad);
        check("deflate distance symbol 30 stops", bad.status == 3 && bad.out == dst);
        floor_deflate_one_bit(lit, gzip_cell(1, 256, 0, 1), dist, gzip_cell(2, 0, 0, 1));
        bad = (gzip_decode_job){0, 0, stop - 64, stop, dst, guard, dst, lit, dist, gzip_extra_masks, 0};
        deflate_decode_span(address_of bad);
        check("deflate stop of block is consumed",
              bad.status == 1 && bad.out == dst && (positive)(bad.next - (stop - 64)) * 8 - bad.count == 1);
        for (positive residue = 0; residue < 8; residue++)
        {
                floor_deflate_one_bit(lit, gzip_cell(1, 0x5a, 0, 1), dist, gzip_cell(2, 0, 0, 1));
                p8 address_to from = guard - GZIP_SPAN_OUT - residue - 4000;
                gzip_decode_job run = {0, 0, input + FLOOR_PAGE, stop, from, guard, from, lit, dist, gzip_extra_masks, 0};
                deflate_decode_span(address_of run);
                p8 same = run.status == 0 && run.out >= guard - 300 && run.out <= guard - 298 &&
                            (positive)(run.next - (input + FLOOR_PAGE)) * 8 - run.count == (positive)(run.out - from);
                for (p8 address_to at = from; same && at < run.out; at++)
                        same = *at == 0x5a;
                check("deflate literal runs stop within the output room", same);
        }
        memory_free(input, 3 * FLOOR_PAGE);
        memory_free(output, 11 * FLOOR_PAGE);
}

/* Random tokens under skewed complete codes up to 15 bits, so subtables and
   every extra-bit width appear; the kernel must stop on a token boundary
   with exactly that token prefix written and exactly its bits consumed. */
static fn floor_deflate_codes(void)
{
        static p32 freq[GZIP_MAXLIT + GZIP_MAXDIST];
        static p8 lens[GZIP_MAXLIT + GZIP_MAXDIST];
        static p32 revs[GZIP_MAXLIT + GZIP_MAXDIST];
        static p32 lit[GZIP_LITLEN_CELLS], dist[GZIP_OFFSET_CELLS];
        static p8 packed[24 * FLOOR_PAGE_STATIC];
        static p8 full[32768 + 24 * FLOOR_PAGE_STATIC + 300];
        static positive boundary_bits[24 * FLOOR_PAGE_STATIC], boundary_out[24 * FLOOR_PAGE_STATIC];
        p8 address_to input = floor_pages(26);
        p8 address_to output = floor_pages(30);
        check("deflate code mappings", input && output);
        if (!input || !output) return;
        p8 address_to stop = input + 25 * FLOOR_PAGE;
        p8 address_to guard = output + 29 * FLOOR_PAGE;
        p32 random = 0x2545f491u;
        for (positive trial = 0; trial < 16; trial++)
        {
                memory_fill(freq, 0, sizeof(freq));
                p32 a = 1, b = 1;
                for (positive i = 0; i < GZIP_MAXLIT + 30; i++) freq[i] = 1;
                for (positive i = 0; i < 24; i++)
                {
                        freq[(i * 7 + trial * 13) % 256] += a;
                        freq[GZIP_MAXLIT + (i + trial) % 30] += a;
                        p32 next = a + b; a = b; b = next;
                }
                freq[256 + trial % 30] += 100000;
                /* Stale subtable pointers from an earlier table must not survive. */
                for (positive i = 0; i < GZIP_LITLEN_CELLS; i++)
                        lit[i] = GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SUBTABLE | (p32)((i * 37) % 4000) << 16 | 0x0400;
                for (positive i = 0; i < GZIP_OFFSET_CELLS; i++)
                        dist[i] = GZIP_CELL_EXCEPTIONAL | GZIP_CELL_SUBTABLE | (p32)((i * 29) % 3000) << 16 | 0x0700;
                p8 built = huffman_lengths(freq, GZIP_MAXLIT, lens, 15) &&
                             huffman_lengths(freq + GZIP_MAXLIT, 30, lens + GZIP_MAXLIT, 15);
                lens[GZIP_MAXLIT + 30] = lens[GZIP_MAXLIT + 31] = 0;
                built = built && gzip_huffman_cells(lit, lens, GZIP_MAXLIT, GZIP_LITLEN_ROOT, 1, null) == 0 &&
                        gzip_huffman_cells(dist, lens + GZIP_MAXLIT, 30, GZIP_OFFSET_ROOT, 2, null) == 0;
                positive deepest = 0;
                for (positive i = 0; i < GZIP_MAXLIT + 30; i++) deepest = lens[i] > deepest ? lens[i] : deepest;
                check("deflate differential codes build with subtables", built && deepest > GZIP_LITLEN_ROOT);
                if (!built) break;
                for (positive part = 0; part < 2; part++)
                {
                        p8 address_to l = lens + (part ? GZIP_MAXLIT : 0);
                        positive n = part ? 30 : GZIP_MAXLIT;
                        p32 rev = 0;
                        for (positive len = 1; len <= GZIP_MAXBITS; len++)
                                for (positive s = 0; s < n; s++)
                                        if (l[s] == len)
                                        {
                                                revs[(part ? GZIP_MAXLIT : 0) + s] = rev;
                                                rev = gzip_revnext(rev, len);
                                        }
                }
                positive room = 3 * FLOOR_PAGE + trial * 3701;
                positive target = (trial & 1) ? room + 20000 : room / 2;
                p8 address_to dst = guard - room;
                for (positive i = 0; i < 32768; i++) full[i] = (p8)(i * 7 + (i >> 9) + trial);
                memory_copy_apart(dst - 32768, full, 32768);
                p64 acc = 0;
                positive held = 0, bytes = 0, bits = 0, made = 0, tokens = 0;
                while (made < target && bytes + 8 < 24 * FLOOR_PAGE)
                {
                        XORSHIFT32(random);
                        positive mode = (trial >> 1) % 4;
                        p8 literal = mode == 0 ? (random & 7) != 0 : mode == 1 ? (random & 7) == 0 : (random & 1);
                        if (literal)
                        {
                                positive s = (random >> 8) & 255;
                                acc |= (p64)revs[s] << held; held += lens[s]; bits += lens[s];
                                full[32768 + made++] = (p8)s;
                        }
                        else
                        {
                                positive ls = (random >> 8) % 29, ds = (random >> 16) % 30;
                                if (mode == 3) ds %= 4;
                                XORSHIFT32(random);
                                positive le = random & ((1u << gzip_len_extra[ls]) - 1);
                                positive de = (random >> 8) & ((1u << gzip_dist_extra[ds]) - 1);
                                positive length = gzip_len_base[ls] + le, distance = gzip_dist_base[ds] + de;
                                acc |= (p64)revs[257 + ls] << held; held += lens[257 + ls];
                                acc |= (p64)le << held; held += gzip_len_extra[ls];
                                while (held >= 8) { packed[bytes++] = (p8)acc; acc >>= 8; held -= 8; }
                                acc |= (p64)revs[GZIP_MAXLIT + ds] << held; held += lens[GZIP_MAXLIT + ds];
                                acc |= (p64)de << held; held += gzip_dist_extra[ds];
                                bits += lens[257 + ls] + gzip_len_extra[ls] + lens[GZIP_MAXLIT + ds] + gzip_dist_extra[ds];
                                for (positive i = 0; i < length; i++, made++)
                                        full[32768 + made] = full[32768 + made - distance];
                        }
                        while (held >= 8) { packed[bytes++] = (p8)acc; acc >>= 8; held -= 8; }
                        boundary_bits[tokens] = bits;
                        boundary_out[tokens++] = made;
                }
                if (held) packed[bytes++] = (p8)acc;
                p8 address_to next = stop - bytes;
                memory_copy_apart(next, packed, bytes);
                gzip_decode_job job = {0, 0, next, stop, dst, guard, dst - 32768, lit, dist, gzip_extra_masks, 0};
                deflate_decode_span(address_of job);
                positive out = (positive)(job.out - dst);
                positive consumed = (positive)(job.next - next) * 8 - job.count;
                positive k = 0;
                while (k < tokens && boundary_out[k] < out) k++;
                p8 same = job.status == 0 && k < tokens && boundary_out[k] == out &&
                            boundary_bits[k] == consumed && !memory_compare(dst, full + 32768, out) &&
                            ((trial & 1) ? out + 300 >= room : stop - job.next <= 32);
                check("deflate kernel stops on a token boundary with exactly its bytes and bits", same);
#ifndef FLOOR_NATIVE
                /* The scalar decoder over the same cells, token by token. */
                gzip_inflater address_to z = gzip_inflater_new();
                check("deflate scalar decoder maps", z != null);
                if (!z) break;
                memory_copy_apart(z->litlen, lit, sizeof(lit));
                memory_copy_apart(z->offset, dist, sizeof(dist));
                byte_input_open_memory(address_of z->input, packed, bytes, z->input.buf, GZIP_DECODE_IN);
                memory_copy_apart(z->out - 32768, full, 32768);
                z->flushed = 32768;
                p8 scalar = true;
                for (positive i = 0; scalar && i < tokens; i++)
                        scalar = gzip_inflate_token(z) == 0 && z->fill == boundary_out[i] &&
                                 z->input.at * 8 - z->count == boundary_bits[i];
                check("deflate scalar decoder agrees token by token, bytes and bits",
                      scalar && !z->why && !memory_compare(z->out, full + 32768, made));
                memory_free(z, GZIP_INFLATER_SIZE);
#endif
        }
        memory_free(input, 26 * FLOOR_PAGE);
        memory_free(output, 30 * FLOOR_PAGE);
}

/* deflate_decode_marked against a model over the streams floor_deflate_codes
   makes: the same random tokens under skewed complete codes, written as
   16-bit values over a 32768-value history that is part bytes and part
   values naming the window (a value is nothing to the kernel but a value,
   copied as it stands). It must stop on a token boundary with exactly that
   token prefix and exactly its bits, from every alignment of the end of its
   output against a protected page. */
static fn floor_deflate_marked(void)
{
        static p32 freq[GZIP_MAXLIT + GZIP_MAXDIST];
        static p8 lens[GZIP_MAXLIT + GZIP_MAXDIST];
        static p32 revs[GZIP_MAXLIT + GZIP_MAXDIST];
        static p32 lit[GZIP_LITLEN_CELLS], dist[GZIP_OFFSET_CELLS];
        static p8 packed[24 * FLOOR_PAGE_STATIC];
        static p16 full[32768 + 40 * FLOOR_PAGE_STATIC + 300];
        static positive boundary_bits[24 * FLOOR_PAGE_STATIC], boundary_out[24 * FLOOR_PAGE_STATIC];
        p8 address_to input = floor_pages(26);
        p8 address_to output = floor_pages(60);
        check("marked kernel mappings", input && output);
        if (!input || !output) return;
        p8 address_to stop = input + 25 * FLOOR_PAGE;
        p8 address_to guard = output + 59 * FLOOR_PAGE;
        p32 random = 0x7f4a7c15u;
        for (positive trial = 0; trial < 16; trial++)
        {
                memory_fill(freq, 0, sizeof(freq));
                p32 a = 1, b = 1;
                for (positive i = 0; i < GZIP_MAXLIT + 30; i++) freq[i] = 1;
                for (positive i = 0; i < 24; i++)
                {
                        freq[(i * 7 + trial * 13) % 256] += a;
                        freq[GZIP_MAXLIT + (i + trial) % 30] += a;
                        p32 next = a + b; a = b; b = next;
                }
                freq[256 + trial % 30] += 100000;
                p8 built = huffman_lengths(freq, GZIP_MAXLIT, lens, 15) &&
                             huffman_lengths(freq + GZIP_MAXLIT, 30, lens + GZIP_MAXLIT, 15);
                lens[GZIP_MAXLIT + 30] = lens[GZIP_MAXLIT + 31] = 0;
                built = built && gzip_huffman_cells(lit, lens, GZIP_MAXLIT, GZIP_LITLEN_ROOT, 1, null) == 0 &&
                        gzip_huffman_cells(dist, lens + GZIP_MAXLIT, 30, GZIP_OFFSET_ROOT, 2, null) == 0;
                check("marked kernel codes build", built);
                if (!built) break;
                for (positive part = 0; part < 2; part++)
                {
                        p8 address_to l = lens + (part ? GZIP_MAXLIT : 0);
                        positive n = part ? 30 : GZIP_MAXLIT;
                        p32 rev = 0;
                        for (positive len = 1; len <= GZIP_MAXBITS; len++)
                                for (positive s = 0; s < n; s++)
                                        if (l[s] == len)
                                        {
                                                revs[(part ? GZIP_MAXLIT : 0) + s] = rev;
                                                rev = gzip_revnext(rev, len);
                                        }
                }
                positive room = 3 * FLOOR_PAGE + trial * 3701;
                positive target = (trial & 1) ? room + 20000 : room / 2;
                p16 address_to dst = (p16 address_to)(guard - 2 * room);
                for (positive i = 0; i < 32768; i++)
                {
                        XORSHIFT32(random);
                        full[i] = (random & 3) ? (p16)((random >> 11) & 255) : (p16)(32768 + ((random >> 5) % 32768));
                }
                memory_copy_apart(dst - 32768, full, 32768 * sizeof(p16));
                p64 acc = 0;
                positive held = 0, bytes = 0, bits = 0, made = 0, tokens = 0;
                while (made < target && bytes + 8 < 24 * FLOOR_PAGE)
                {
                        XORSHIFT32(random);
                        positive mode = (trial >> 1) % 4;
                        p8 literal = mode == 0 ? (random & 7) != 0 : mode == 1 ? (random & 7) == 0 : (random & 1);
                        if (literal)
                        {
                                positive s = (random >> 8) & 255;
                                acc |= (p64)revs[s] << held; held += lens[s]; bits += lens[s];
                                full[32768 + made++] = (p16)s;
                        }
                        else
                        {
                                positive ls = (random >> 8) % 29, ds = (random >> 16) % 30;
                                if (mode == 3) ds %= 4;
                                XORSHIFT32(random);
                                positive le = random & ((1u << gzip_len_extra[ls]) - 1);
                                positive de = (random >> 8) & ((1u << gzip_dist_extra[ds]) - 1);
                                positive length = gzip_len_base[ls] + le, distance = gzip_dist_base[ds] + de;
                                acc |= (p64)revs[257 + ls] << held; held += lens[257 + ls];
                                acc |= (p64)le << held; held += gzip_len_extra[ls];
                                while (held >= 8) { packed[bytes++] = (p8)acc; acc >>= 8; held -= 8; }
                                acc |= (p64)revs[GZIP_MAXLIT + ds] << held; held += lens[GZIP_MAXLIT + ds];
                                acc |= (p64)de << held; held += gzip_dist_extra[ds];
                                bits += lens[257 + ls] + gzip_len_extra[ls] + lens[GZIP_MAXLIT + ds] + gzip_dist_extra[ds];
                                for (positive i = 0; i < length; i++, made++)
                                        full[32768 + made] = full[32768 + made - distance];
                        }
                        while (held >= 8) { packed[bytes++] = (p8)acc; acc >>= 8; held -= 8; }
                        boundary_bits[tokens] = bits;
                        boundary_out[tokens++] = made;
                }
                if (held) packed[bytes++] = (p8)acc;
                p8 address_to next = stop - bytes;
                memory_copy_apart(next, packed, bytes);
                gzip_decode_job job = {0, 0, next, stop, (p8 address_to)dst, guard,
                                       (p8 address_to)(dst - 32768), lit, dist, gzip_extra_masks, 0};
                deflate_decode_marked(address_of job);
                positive out = (positive)((p16 address_to)job.out - dst);
                positive consumed = (positive)(job.next - next) * 8 - job.count;
                positive k = 0;
                while (k < tokens && boundary_out[k] < out) k++;
                p8 same = job.status == 0 && k < tokens && boundary_out[k] == out &&
                            boundary_bits[k] == consumed &&
                            !memory_compare(dst, full + 32768, out * sizeof(p16)) &&
                            ((trial & 1) ? 2 * out + 600 >= 2 * room : stop - job.next <= 32);
                check("marked kernel stops on a token boundary with exactly its values and bits", same);
        }
        memory_free(input, 26 * FLOOR_PAGE);
        memory_free(output, 60 * FLOOR_PAGE);
}

/* The scalar LZMA decoder the span kernel is held to: one packet and one bit
   at a time over a plain ring, reading nothing past its input limit. */
typedef struct
{
        p32 range, code;
        p8 address_to next;
        p8 address_to limit;
        xz_probability_state m;
        p8 ring[4096];
        positive pos, full, unpacked;
        positive rep[4];
        p32 state, lc, lp, pb;
        p8 short_input;
} floor_lzma_oracle;

static p32 floor_lzma_bit(floor_lzma_oracle address_to o, p16 address_to prob)
{
        if (o->range < 0x1000000u)
        {
                if (o->next >= o->limit)
                {
                        o->short_input = true;
                        return 0;
                }
                o->range <<= 8;
                o->code = (o->code << 8) | address_to o->next++;
        }

        p32 bound = (o->range >> 11) * address_to prob;

        if (o->code < bound)
        {
                o->range = bound;
                address_to prob += (2048 - address_to prob) >> 5;
                return 0;
        }
        o->range -= bound;
        o->code -= bound;
        address_to prob -= address_to prob >> 5;
        return 1;
}

static p32 floor_lzma_tree(floor_lzma_oracle address_to o, p16 address_to probs, p32 bits)
{
        p32 symbol = 1;

        while (symbol < (1u << bits))
                symbol = (symbol << 1) | floor_lzma_bit(o, probs + symbol);
        return symbol - (1u << bits);
}

/* Reverse trees index by the low bits already decoded, as the kernel does. */
static p32 floor_lzma_reverse(floor_lzma_oracle address_to o, p16 address_to probs, p32 bits)
{
        p32 value = 0;

        for (p32 i = 0; i < bits; i++)
                value |= floor_lzma_bit(o, probs + ((1u << i) | value)) << i;
        return value;
}

static p8 floor_lzma_back(floor_lzma_oracle address_to o, positive dist)
{
        return o->ring[(o->pos + sizeof(o->ring) - dist) % sizeof(o->ring)];
}

static fn floor_lzma_put(floor_lzma_oracle address_to o, p8 byte, p8 address_to address_to out)
{
        o->ring[o->pos] = byte;
        o->pos = (o->pos + 1) % sizeof(o->ring);
        if (o->full < sizeof(o->ring))
                o->full++;
        o->unpacked++;
        *(*out)++ = byte;
}

static p32 floor_lzma_length(floor_lzma_oracle address_to o, p8 repeat, positive ps)
{
        xz_probability_state address_to m = address_of o->m;

        if (!floor_lzma_bit(o, repeat ? address_of m->rep_choice : address_of m->match_choice))
                return 2 + floor_lzma_tree(o, repeat ? m->rep_low[ps] : m->match_low[ps], 3);
        if (!floor_lzma_bit(o, repeat ? address_of m->rep_choice2 : address_of m->match_choice2))
                return 10 + floor_lzma_tree(o, repeat ? m->rep_mid[ps] : m->match_mid[ps], 3);
        return 18 + floor_lzma_tree(o, repeat ? m->rep_high : m->match_high, 8);
}

/* One packet of at most room bytes; false for a malformed packet. */
static p8 floor_lzma_packet(floor_lzma_oracle address_to o, p8 address_to address_to out,
                              positive room)
{
        xz_probability_state address_to m = address_of o->m;
        positive ps = o->unpacked & ((1u << o->pb) - 1);
        positive len;

        if (!floor_lzma_bit(o, address_of m->is_match[o->state][ps]))
        {
                p8 prev = o->full ? floor_lzma_back(o, 1) : 0;
                p16 address_to probs = m->lit + 0x300 *
                        (((o->unpacked & ((1u << o->lp) - 1)) << o->lc) + (prev >> (8 - o->lc)));
                p32 symbol = 1;

                if (o->state >= 7)
                {
                        if (o->rep[0] > o->full)
                                return false;

                        p32 match = floor_lzma_back(o, o->rep[0]);

                        do
                        {
                                p32 predicted = (match >> 7) & 1;

                                match <<= 1;

                                p32 bit = floor_lzma_bit(o, probs + ((1 + predicted) << 8) + symbol);

                                symbol = (symbol << 1) | bit;
                                if (bit != predicted)
                                        break;
                        } while (symbol < 0x100);
                }
                while (symbol < 0x100)
                        symbol = (symbol << 1) | floor_lzma_bit(o, probs + symbol);
                floor_lzma_put(o, (p8)symbol, out);
                o->state = o->state < 4 ? 0 : o->state < 10 ? o->state - 3 : o->state - 6;
                return true;
        }
        if (!floor_lzma_bit(o, address_of m->is_rep[o->state]))
        {
                o->rep[3] = o->rep[2];
                o->rep[2] = o->rep[1];
                o->rep[1] = o->rep[0];
                len = floor_lzma_length(o, false, ps);

                p32 slot = floor_lzma_tree(o, m->dist_slot[len < 6 ? len - 2 : 3], 6);
                positive dist = slot;

                if (slot >= 4)
                {
                        p32 bits = (slot >> 1) - 1;

                        dist = (positive)(2 | (slot & 1)) << bits;
                        if (slot < 14)
                                dist += floor_lzma_reverse(o, m->dist_special + dist - slot - 1, bits);
                        else
                        {
                                positive direct = 0;

                                for (p32 i = 0; i < bits - 4; i++)
                                {
                                        if (o->range < 0x1000000u)
                                        {
                                                if (o->next >= o->limit)
                                                {
                                                        o->short_input = true;
                                                        return true;
                                                }
                                                o->range <<= 8;
                                                o->code = (o->code << 8) | address_to o->next++;
                                        }
                                        o->range >>= 1;

                                        p32 one = o->code >= o->range;

                                        if (one)
                                                o->code -= o->range;
                                        direct = (direct << 1) | one;
                                }
                                dist += (direct << 4) + floor_lzma_reverse(o, m->dist_align, 4);
                        }
                }
                o->rep[0] = dist + 1;
                o->state = o->state < 7 ? 7 : 10;
        }
        else
        {
                if (!floor_lzma_bit(o, address_of m->is_rep0[o->state]))
                {
                        if (!floor_lzma_bit(o, address_of m->is_rep0_long[o->state][ps]))
                        {
                                o->state = o->state < 7 ? 9 : 11;
                                if (o->rep[0] > o->full)
                                        return false;
                                floor_lzma_put(o, floor_lzma_back(o, o->rep[0]), out);
                                return true;
                        }
                }
                else
                {
                        positive dist;

                        if (!floor_lzma_bit(o, address_of m->is_rep1[o->state]))
                                dist = o->rep[1];
                        else
                        {
                                if (!floor_lzma_bit(o, address_of m->is_rep2[o->state]))
                                        dist = o->rep[2];
                                else
                                {
                                        dist = o->rep[3];
                                        o->rep[3] = o->rep[2];
                                }
                                o->rep[2] = o->rep[1];
                        }
                        o->rep[1] = o->rep[0];
                        o->rep[0] = dist;
                }
                len = floor_lzma_length(o, true, ps);
                o->state = o->state < 7 ? 8 : 11;
        }
        if (o->rep[0] > o->full || len > room)
                return false;
        for (positive i = 0; i < len; i++)
                floor_lzma_put(o, floor_lzma_back(o, o->rep[0]), out);
        return true;
}

/* The span kernel against the oracle: a 4096-byte dictionary that wraps
   several times, spans stopped at random packet boundaries, the input's last
   bytes run through a zero-padded tail, the dictionary buffer ending at a
   guard page, and three malformed streams: a distance past the history, a
   match past the chunk, and a distance past address zero from a dictionary
   low in memory, which only a signed source check refuses. */
static fn floor_lzma_span(void)
{
        static p8 plain_storage[1 + 16384];
        p8 address_to plain = plain_storage + 1;
        const positive plain_n = 16384;
        static p8 expect[16384];
        static p8 tail[128];
        static floor_lzma_oracle oracle;
        static xz_probability_state model;
        static p8 low_dict[4096 + XZ_DICT_START + XZ_DICT_SLACK];
        const positive limit = 4096;
        const positive size = limit + XZ_DICT_START;
        const positive cap = size + XZ_DICT_SLACK;
        p8 address_to input = floor_pages(6);
        p8 address_to output = floor_pages(4);

        check("LZMA span guard mappings", input && output);
        if (!input || !output)
                return;
        check("LZMA span ABI",
              sizeof(xz_decode_job) == 160 &&
              __builtin_offsetof(xz_decode_job, in_stop) == 16 &&
              __builtin_offsetof(xz_decode_job, base) == 32 &&
              __builtin_offsetof(xz_decode_job, copy_end) == 64 &&
              __builtin_offsetof(xz_decode_job, lo) == 72 &&
              __builtin_offsetof(xz_decode_job, dmax) == 96 &&
              __builtin_offsetof(xz_decode_job, state) == 104 &&
              __builtin_offsetof(xz_decode_job, rep) == 120 &&
              __builtin_offsetof(xz_decode_job, error) == 152);

        p8 address_to guarded = output + 3 * FLOOR_PAGE - cap;

        for (positive trial = 0; trial < 17; trial++)
        {
                p32 random = 0x57139021u + (p32)trial;
                p8 low = trial == 16;
                p8 far = trial == 14 || low;
                p8 overrun = trial == 15;
                p8 address_to dict = low ? low_dict : guarded;
                positive first_match_end = 0;
                positive at = 0;
                p32 lc = trial % 5;
                p32 lp = trial % (5 - lc);
                p32 pb = trial % 5;

                /* The block encoder codes each chosen packet; the position
                   before plain reads as the zero a fresh dictionary holds. */
                xz_encoder address_to e = xz_encoder_open(0);

                check("LZMA fixture encoder", e != null);
                if (!e)
                        break;
                e->lc = (p8)lc;
                e->lp = (p8)lp;
                e->pb = (p8)pb;
                e->lp_mask = ((p32)1 << lp) - 1;
                e->pos_mask = ((p32)1 << pb) - 1;
                xz_lzma_reset(e);
                e->input = plain;
                e->input_n = (p32)plain_n;
                e->rc = (xz_range_state){0xffffffffu, 0, 0, 1, e->chunk,
                                         e->chunk + sizeof(e->chunk), 0};
                while (at < plain_n)
                {
                        XORSHIFT32(random);
                        positive n = 1;

                        if (far && at >= 20)
                        {
                                /* A distance past everything decoded so far. */
                                for (positive k = 0; k < 5; k++) plain[at + k] = 0;
                                e->read_pos = (p32)(at + 5); e->read_ahead = 5; e->position = at;
                                xz_code_symbol(e, low ? 0xf0000000u - 1 + 4 : 3999 + 4, 5);
                                at += 5;
                                break;
                        }
                        if (at > 8 && (random & 31) == 1)
                        {
                                /* A short repeat, which the parsers rarely pick here. */
                                plain[at] = plain[at - e->reps[0] - 1];
                                e->read_pos = (p32)(at + 1); e->read_ahead = 1; e->position = at;
                                xz_code_symbol(e, 0, 1);
                                at++;
                                continue;
                        }
                        if (at > 8 && (random & 3))
                        {
                                positive dist = (random >> 8) % (at < limit ? at : limit) + 1;
                                positive which = (random >> 6) & 3;
                                p8 repeat = (random & 4) && e->reps[which] + 1 <= (at < limit ? at : limit);
                                if (repeat) dist = e->reps[which] + 1;
                                if (trial < 7 && !repeat) dist = trial + 1;
                                n = 2 + ((random >> 16) % 272);
                                if (n > plain_n - at) n = plain_n - at;
                                if (n >= 2)
                                {
                                        for (positive k = 0; k < n; k++) plain[at + k] = plain[at + k - dist];
                                        e->read_pos = (p32)(at + n); e->read_ahead = (p32)n; e->position = at;
                                        xz_code_symbol(e, repeat ? (p32)which : (p32)(dist - 1 + 4), (p32)n);
                                        if (!first_match_end)
                                                first_match_end = at + n;
                                }
                        }
                        if (n == 1)
                        {
                                plain[at] = (p8)random;
                                e->read_pos = (p32)(at + 1); e->read_ahead = 1; e->position = at;
                                xz_code_symbol(e, XZ_LITERAL, 1);
                        }
                        at += n;
                }
                for (positive i = 0; i < 5; i++)
                        lzma_range_shift(address_of e->rc);

                positive packed = (positive)(e->rc.next - e->chunk);
                positive total = at;
                positive goal = overrun ? first_match_end - 1 : total;
                p8 fits = !e->rc.full && packed <= 4 * FLOOR_PAGE && packed > 5;
                p8 address_to bytes = input + 5 * FLOOR_PAGE - packed;

                check("LZMA differential fixture fits", fits);
                if (fits)
                        memory_copy_apart(bytes, e->chunk, packed);
                xz_encoder_close(e);
                if (!fits)
                        break;

                p32 code = 0;
                p16 address_to cells = (p16 address_to)address_of oracle.m;

                for (positive i = 1; i < 5; i++)
                        code = (code << 8) | bytes[i];
                memory_fill(address_of oracle, 0, sizeof(oracle));
                oracle.range = 0xffffffffu;
                oracle.code = code;
                oracle.next = bytes + 5;
                oracle.limit = bytes + packed;
                oracle.lc = lc;
                oracle.lp = lp;
                oracle.pb = pb;
                oracle.rep[0] = oracle.rep[1] = oracle.rep[2] = oracle.rep[3] = 1;
                for (positive i = 0; i < sizeof(oracle.m) / sizeof(p16); i++)
                        cells[i] = 1024;
                memory_copy_apart(address_of model, address_of oracle.m, sizeof(model));

                xz_decode_job job = {0};

                job.range = 0xffffffffu;
                job.code = code;
                job.next = bytes + 5;
                job.model = address_of model;
                job.base = dict;
                job.out = dict + XZ_DICT_START;
                dict[XZ_DICT_START - 1] = 0;
                job.lc = lc;
                job.lp = lp;
                job.pb = pb;
                job.rep[0] = job.rep[1] = job.rep[2] = job.rep[3] = 1;

                p8 address_to oracle_out = expect;
                positive produced = 0;
                positive spans = 0;
                p8 wrapped = false;
                p8 same = true;
                p8 rejected = false;

                while (produced < goal && same)
                {
                        p8 address_to top = dict + size;

                        if (job.out >= top)
                        {
                                positive shift = size - XZ_MIRROR;
                                positive keep = (positive)(job.out - dict) - shift;

                                memory_copy_apart(dict, dict + shift, keep);
                                job.out -= shift;
                                wrapped = true;
                        }

                        positive left = (positive)(bytes + packed - job.next);
                        p8 address_to from = job.next;
                        p8 padded = left < XZ_PACKET_IN;

                        if (padded)
                        {
                                memory_fill(tail, 0, sizeof(tail));
                                memory_copy_apart(tail, job.next, left);
                                job.next = tail;
                                job.in_stop = tail + left + XZ_IN_PAD - (XZ_PACKET_IN - 1);
                        }
                        else
                                job.in_stop = bytes + packed - (XZ_PACKET_IN - 1);
                        job.out_end = job.out + (goal - produced);
                        job.out_stop = top < job.out_end ? top : job.out_end;
                        XORSHIFT32(random);
                        if ((trial & 1) && job.out_stop > job.out + 1 + random % 300)
                                job.out_stop = job.out + 1 + random % 300;
                        job.copy_end = dict + cap - XZ_COPY_SLACK;
                        if (job.copy_end > job.out_end)
                                job.copy_end = job.out_end;
                        job.dmax = low ? ~(p64)0 : limit;
                        job.wrap = wrapped ? size - XZ_MIRROR : 0;
                        job.bottom = wrapped ? dict : dict + XZ_DICT_START;
                        job.lo = job.bottom;
                        if (wrapped && (positive)(job.out_stop - dict) > limit)
                                job.lo = job.out_stop - limit;
                        job.error = 0;

                        p8 address_to before = job.out;

                        lzma_decode_span(address_of job);
                        if (padded)
                        {
                                positive used = (positive)(job.next - tail);

                                check("LZMA span reads no padding from a whole stream",
                                      used <= left || job.error);
                                job.next = from + used;
                        }

                        positive made = (positive)(job.out - before);
                        positive want = produced + made;

                        while (oracle.unpacked < want &&
                               floor_lzma_packet(address_of oracle, address_of oracle_out,
                                                 goal - oracle.unpacked))
                                ;
                        if (job.error)
                        {
                                p8 agrees = oracle.unpacked == want &&
                                        !floor_lzma_packet(address_of oracle, address_of oracle_out,
                                                           goal - oracle.unpacked);

                                check("LZMA span and oracle reject the same packet",
                                      (far || overrun) && agrees &&
                                      !memory_compare(before, expect + produced, made));
                                rejected = true;
                                break;
                        }
                        if (made)
                                spans++;
                        same = oracle.unpacked == want && !oracle.short_input &&
                               oracle.range == job.range && oracle.code == job.code &&
                               oracle.next == job.next && oracle.state == job.state &&
                               !memory_compare(oracle.rep, job.rep, sizeof(job.rep)) &&
                               !memory_compare(address_of oracle.m, address_of model, sizeof(model)) &&
                               !memory_compare(before, expect + produced, made);
                        check("LZMA span equals the scalar oracle: range, input, state, repeats, models and output",
                              same);
                        produced = want;
                }
                if (far || overrun)
                        check("LZMA malformed fixture is rejected", rejected);
                else
                        check("LZMA span decodes the whole fixture",
                              same && produced == total && spans &&
                              !memory_compare(expect, plain, total));
        }
        memory_free(input, 6 * FLOOR_PAGE);
        memory_free(output, 4 * FLOOR_PAGE);
}

/* huffman_lengths against the heap it replaced: the heap pops the least
   (count, node) each time, and the lengths must come out the same for
   counts of every shape summing below 2^22, the contract, including
   Fibonacci runs past every limit. */
static fn floor_huffman_heap_down(p32 address_to count, p32 address_to heap, positive used, positive at)
{
        p32 item = heap[at];

        for (;;)
        {
                positive child = at * 2;

                if (child > used)
                        break;
                if (child < used && (count[heap[child + 1]] < count[heap[child]] ||
                                     (count[heap[child + 1]] == count[heap[child]] && heap[child + 1] < heap[child])))
                        child++;
                if (count[item] < count[heap[child]] || (count[item] == count[heap[child]] && item <= heap[child]))
                        break;
                heap[at] = heap[child];
                at = child;
        }
        heap[at] = item;
}

static p8 floor_huffman_heap(p32 address_to freq, positive n, p8 address_to length, positive limit)
{
        p32 count[576], heap[577];
        b32 dad[576];
        positive used = 0, next = n, counts[16] = {0}, slots = 0, sorted[288], placed = 0;

        memory_fill(length, 0, n);
        for (positive at = 0; at < n; at++)
        {
                count[at] = freq[at];
                dad[at] = -1;
                if (freq[at])
                        heap[++used] = (p32)at;
        }
        for (positive at = used / 2; at >= 1; at--)
                floor_huffman_heap_down(count, heap, used, at);
        if (used < 2)
        {
                if (!used) { length[0] = 1; if (n > 1) length[1] = 1; }
                else { length[heap[1]] = 1; if (heap[1] == 0 && n > 1) length[1] = 1; else if (n) length[0] = 1; }
                return true;
        }
        while (used > 1)
        {
                p32 first = heap[1], second;

                heap[1] = heap[used--];
                floor_huffman_heap_down(count, heap, used, 1);
                second = heap[1];
                count[next] = count[first] + count[second];
                dad[first] = dad[second] = (b32)next;
                dad[next] = -1;
                heap[1] = (p32)next;
                floor_huffman_heap_down(count, heap, used, 1);
                next++;
        }
        for (positive at = 0; at < n; at++)
        {
                positive depth = 0;

                if (!freq[at])
                        continue;
                for (b32 walk = dad[at]; walk >= 0; walk = dad[walk])
                        depth++;
                if (depth > limit)
                        depth = limit;
                length[at] = (p8)depth;
                counts[depth]++;
                slots += (positive)1 << (limit - depth);
        }
        if (slots <= ((positive)1 << limit))
                return true;
        while (slots > ((positive)1 << limit))
        {
                positive bits = limit - 1;

                while (bits && !counts[bits])
                        bits--;
                if (!bits || !counts[limit])
                        return false;
                counts[bits]--;
                counts[bits + 1] += 2;
                counts[limit]--;
                slots--;
        }
        for (positive at = 0; at < n; at++)
                if (freq[at])
                {
                        positive place = placed++;

                        while (place && freq[sorted[place - 1]] > freq[at])
                        {
                                sorted[place] = sorted[place - 1];
                                place--;
                        }
                        sorted[place] = at;
                }
        placed = 0;
        for (positive bits = limit; bits; bits--)
                for (positive take = 0; take < counts[bits]; take++)
                        length[sorted[placed++]] = (p8)bits;
        return true;
}

static fn floor_huffman_lengths(void)
{
        static p32 freq[288];
        p8 got[288], want[288];
        p32 random = 0x9e3779b9u;
        p8 same = true;
        positive widths[] = {1, 2, 3, 19, 30, 32, 256, 286, 288};

        for (positive trial = 0; trial < 60000; trial++)
        {
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive n = widths[FLOOR_RANDOM() % array_count(widths)];
                positive limit = n == 19 ? 7 : 1 + FLOOR_RANDOM() % 15;
                positive shape = FLOOR_RANDOM() % 8;

                if (limit < 9 && n > 256) limit = 9;
                if (limit < 8 && n > 128) limit = 8;
                if (limit < 5 && n > 19) limit = 5;
                for (positive i = 0; i < n; i++)
                {
                        p32 r = FLOOR_RANDOM();
                        freq[i] = shape == 0 ? (r % 3 ? 0 : r >> 22)
                                : shape == 1 ? (r & 1) << ((r >> 27) % 14)
                                : shape == 2 ? (r % 5 == 0)
                                : shape == 3 ? r % 7
                                : shape == 4 ? (r >> 8) & ((1u << (1 + r % 13)) - 1)
                                : shape == 5 ? (i < 30 ? (p32)(i * i * i + 1) : r % 4 == 0)
                                : shape == 6 ? r >> 19
                                : r % 50;
                }
                //      One count past sixteen bits, for a third radix pass.
                if (shape == 7)
                        freq[FLOOR_RANDOM() % n] = FLOOR_RANDOM() & 0x1fffff;
                if (shape == 5 && FLOOR_RANDOM() % 2)
                {
                        p32 a = 1, b = 1;

                        for (positive i = 0; i < n && i < 30; i++)
                        {
                                freq[i] = a;
                                p32 t = a + b; a = b; b = t;
                        }
                }
                memory_fill(got, 0x5a, sizeof(got));
                p8 r1 = floor_huffman_heap(freq, n, want, limit);
                p8 r2 = huffman_lengths(freq, n, got, limit);
                if (r1 != r2 || (r1 && memory_compare(got, want, n)))
                        same = false;
#undef FLOOR_RANDOM
        }
        check("huffman_lengths builds the heap's tree for every shape of counts", same);
}

static fn floor_codebook(void)
{
        p32 freq[288];
        p8 length[288];
        for (positive kind = 0; kind < 3; kind++)
                for (positive rotate = 0; rotate < 19; rotate++)
                {
                        positive n = kind == 0 ? 19 : kind == 1 ? 256 : 288;
                        p8 limit = kind == 0 ? 7 : kind == 1 ? 11 : 15;
                        memory_fill(freq, 0, sizeof(freq));
                        p32 a = 1, b = 1;
                        for (positive i = 0; i < (kind ? 24 : 19); i++)
                        {
                                freq[(i + rotate) % n] = a;
                                p32 next = a + b; a = b; b = next;
                        }
                        p8 valid = huffman_lengths(freq, n, length, limit);
                        positive kraft = 0;
                        for (positive i = 0; i < n; i++)
                        {
                                if (freq[i] && !length[i]) valid = false;
                                if (length[i] > limit) valid = false;
                                if (length[i] && length[i] <= limit) kraft += (positive)1 << (limit - length[i]);
                        }
                        check("length-limited skewed Huffman codebook is complete", valid && kraft == (positive)1 << limit);
                }
}

/* deflate_tokens_count and deflate_tokens_encode against a bit at a time
   model: random blocks of literals and pairs over random code tables,
   every pending width on entry, with the output guarded past its end. */
static fn floor_deflate_tokens_put(p8 address_to out, positive address_to at, p64 value, positive n)
{
        for (positive i = 0; i < n; i++, (*at)++)
                out[*at / 8] = (p8)((out[*at / 8] & ~(1u << (*at % 8))) | (((value >> i) & 1) << (*at % 8)));
}

static fn floor_deflate_tokens(void)
{
        static p8 src[4096], got[16384], want[16384];
        static p32 mpos[1024];
        static p16 mlen[1024], mdist[1024];
        p32 lit[GZIP_MAXLIT], dist[GZIP_MAXDIST], count_lit[GZIP_MAXLIT], count_dist[GZIP_MAXDIST];
        p32 want_lit[GZIP_MAXLIT], want_dist[GZIP_MAXDIST];
        p32 random = 0x2545f491u;
        p8 same = true, counted = true;

        for (positive trial = 0; trial < 3000; trial++)
        {
                positive length = 0, pairs = 0, extra = 0;
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive room = 1 + FLOOR_RANDOM() % 4000;
                //      Every third block is dense in pairs: the tables the wide body
                //      makes only pay from 64 pairs, and the short pairs run back to
                //      back with no literal between them.
                p8 dense = trial % 3 == 0;
                for (positive i = 0; i < GZIP_MAXLIT; i++)
                {
                        positive len = 1 + FLOOR_RANDOM() % 15;
                        lit[i] = (FLOOR_RANDOM() & ((1u << len) - 1)) | (p32)len << 16;
                }
                for (positive i = 0; i < GZIP_MAXDIST; i++)
                {
                        positive len = 1 + FLOOR_RANDOM() % 15;
                        dist[i] = (FLOOR_RANDOM() & ((1u << len) - 1)) | (p32)len << 16;
                }
                for (positive i = 0; i < sizeof(src); i++)
                        src[i] = (p8)FLOOR_RANDOM();
                while (length < room)
                {
                        positive mode = dense ? (FLOOR_RANDOM() % 6 == 0 ? 1 : 0) : FLOOR_RANDOM() % 4;
                        if (mode == 0 && length + 3 <= room && pairs < 1024)
                        {
                                positive m = 3 + FLOOR_RANDOM() % (dense ? 14 : 256);
                                positive d = FLOOR_RANDOM() % 3 ? 1 + FLOOR_RANDOM() % 32768
                                                                : 1 + FLOOR_RANDOM() % 8;
                                if (FLOOR_RANDOM() % 8 == 0) m = 258;
                                if (length + m > room) m = room - length < 3 ? 3 : room - length;
                                mpos[pairs] = (p32)length;
                                mlen[pairs] = (p16)m;
                                mdist[pairs] = (p16)d;
                                pairs++;
                                length += m;
                        }
                        else
                                length += 1 + FLOOR_RANDOM() % (mode == 1 ? 1 : 9);
                }
                if (length > room && (!pairs || mpos[pairs - 1] + mlen[pairs - 1] != length))
                        length = room;

                //      The model: the reference's walk, one bit at a time.
                positive held = FLOOR_RANDOM() % 32;
                p64 pending = FLOOR_RANDOM() & (((p64)1 << held) - 1);
                positive bit = 0, at = 0, pair = 0;
                memory_fill(want, 0xa5, sizeof(want));
                memory_fill(got, 0xa5, sizeof(got));
                memory_fill(want_lit, 0, sizeof(want_lit));
                memory_fill(want_dist, 0, sizeof(want_dist));
                floor_deflate_tokens_put(want, address_of bit, pending, held);
                while (at < length)
                {
                        if (pair < pairs && mpos[pair] == at)
                        {
                                positive l = mlen[pair], d = mdist[pair], lc = 0, dc = 0;
                                while (lc < 28 && gzip_len_base[lc + 1] <= l) lc++;
                                while (dc < 29 && gzip_dist_base[dc + 1] <= d) dc++;
                                floor_deflate_tokens_put(want, address_of bit, lit[257 + lc] & 0xffff, lit[257 + lc] >> 16);
                                floor_deflate_tokens_put(want, address_of bit, l - gzip_len_base[lc], gzip_len_extra[lc]);
                                floor_deflate_tokens_put(want, address_of bit, dist[dc] & 0xffff, dist[dc] >> 16);
                                floor_deflate_tokens_put(want, address_of bit, d - gzip_dist_base[dc], gzip_dist_extra[dc]);
                                want_lit[257 + lc]++;
                                want_dist[dc]++;
                                extra += gzip_len_extra[lc] + gzip_dist_extra[dc];
                                at += l;
                                pair++;
                        }
                        else
                        {
                                floor_deflate_tokens_put(want, address_of bit, lit[src[at]] & 0xffff, lit[src[at]] >> 16);
                                want_lit[src[at]]++;
                                at++;
                        }
                }
                floor_deflate_tokens_put(want, address_of bit, lit[256] & 0xffff, lit[256] >> 16);

                gzip_tokens j = {src, mpos, mlen, mdist, length, pairs, lit, dist, got, pending, held};
                deflate_tokens_encode(address_of j);
                positive whole = (positive)(j.out - got);
                same = same && whole == bit / 8 && j.bitn == bit % 8 &&
                       !memory_compare(got, want, whole) &&
                       (j.bits == (bit % 8 ? want[whole] & ((1u << (bit % 8)) - 1) : 0)) &&
                       got[whole + 8] == 0xa5;
                memory_fill(count_lit, 0, sizeof(count_lit));
                memory_fill(count_dist, 0, sizeof(count_dist));
                gzip_tokens c = {src, mpos, mlen, mdist, length, pairs, count_lit, count_dist, null, 7, 0};
                deflate_tokens_count(address_of c);
                counted = counted && c.bits == 7 + extra &&
                          !memory_compare(count_lit, want_lit, sizeof(want_lit)) &&
                          !memory_compare(count_dist, want_dist, sizeof(want_dist));
#undef FLOOR_RANDOM
        }
        check("deflate tokens written bit-exact from any pending width", same);
        check("deflate tokens counted into both alphabets with their extra bits", counted);
}

/* deflate_parse_fast against gzip's level 1 walk written out a position at
   a time: the same pairs, literals, counts, table and stopping place from
   blocks of random bytes, of a few symbols, of copies of earlier bytes, of
   runs, of short periods and of short stretches copied in turn from 100
   back and from exactly the window's reach (so the search past a near
   match meets a candidate 32768 back), started anywhere from the first
   byte to past the first window, with a pair limit that bites, and with a
   nice the routine leaves to the caller. */
typedef struct
{
        p8 address_to base;
        p32 address_to fast;
        positive total, start, pos, limit, nice, pairs;
        p32 address_to mpos;
        p16 address_to mlen;
        p16 address_to mdist;
        p32 address_to lit_freq;
        p32 address_to dist_freq;
        positive hash, pairs_max;
} floor_fast_job;

static positive floor_fast_extend(p8 address_to a, p8 address_to b, positive limit)
{
        positive at = 4;

        while (at < limit && a[at] == b[at])
                at++;
        return at;
}

static fn floor_deflate_parse_model(floor_fast_job address_to j)
{
        positive pos = j->pos;
        positive lim = j->total >= 299 ? j->total - 299 : 0;
        p32 h = (p32)j->hash;

        if (j->limit < lim)
                lim = j->limit;
        if (j->nice < 16)
                return;
        while (pos < lim && j->pairs < j->pairs_max)
        {
                p8 address_to here = j->base + pos;
                p32 seq = memory_load_unaligned(p32, here);
                p32 low = pos + 1 > 32768 ? (p32)(pos + 1 - 32768) : 1;
                p32 first = j->fast[2 * h];
                p32 second = j->fast[2 * h + 1];
                positive best = 0, dist = 0;

                j->fast[2 * h] = (p32)(pos + 1);
                j->fast[2 * h + 1] = first;
                h = (memory_load_unaligned(p32, here + 1) * 0x1e35a7bdu) >> 17;
                if (first >= low && memory_load_unaligned(p32, j->base + first - 1) == seq)
                {
                        best = floor_fast_extend(here, j->base + first - 1, 258);
                        dist = pos + 1 - first;
                        if (best < j->nice && best < 258 && second >= low &&
                            memory_load_unaligned(p32, j->base + second - 1) == seq &&
                            memory_load_unaligned(p32, j->base + second - 1 + best - 3) ==
                                    memory_load_unaligned(p32, here + best - 3))
                        {
                                positive length = floor_fast_extend(here, j->base + second - 1, 258);

                                if (length > best)
                                {
                                        best = length;
                                        dist = pos + 1 - second;
                                }
                        }
                }
                else if (second >= low && memory_load_unaligned(p32, j->base + second - 1) == seq)
                {
                        best = floor_fast_extend(here, j->base + second - 1, 258);
                        dist = pos + 1 - second;
                }
                if (!best)
                {
                        j->lit_freq[*here]++;
                        pos++;
                        continue;
                }
                {
                        positive k = j->pairs++;

                        j->mpos[k] = (p32)(pos - j->start);
                        j->mlen[k] = (p16)best;
                        j->mdist[k] = (p16)dist;
                        j->lit_freq[257 + deflate_symbol_tab[best - 3]]++;
                        j->dist_freq[dist <= 256 ? deflate_symbol_tab[256 + dist - 1]
                                                 : deflate_symbol_tab[512 + ((dist - 1) >> 7)]]++;
                }
                for (positive c = best - 1, p = pos + 1; c; c--, p++)
                {
                        j->fast[2 * h + 1] = j->fast[2 * h];
                        j->fast[2 * h] = (p32)(p + 1);
                        h = (memory_load_unaligned(p32, j->base + p + 1) * 0x1e35a7bdu) >> 17;
                }
                pos += best;
        }
        j->pos = pos;
        j->hash = h;
}

static fn floor_deflate_parse(void)
{
        static p8 data[70000 + 1024];
        static p32 fast_got[2u << 15], fast_want[2u << 15];
        static p32 mpos_got[8192 + 16], mpos_want[8192 + 16];
        static p16 mlen_got[8192 + 16], mlen_want[8192 + 16], mdist_got[8192 + 16], mdist_want[8192 + 16];
        p32 lit_got[288], lit_want[288], dist_got[32], dist_want[32];
        p32 random = 0x7a3c91d5u;
        p8 same = true, anything = false;
        //      The input the walk reads lives against a guard page, its end on even
        //      trials and its start on odd ones, so a read past the last byte or
        //      before the first (a candidate, a window, a sixteen byte compare) is a
        //      fault and not a quiet wrong answer.
        p8 address_to guarded = floor_pages(24);
        positive usable = 22 * FLOOR_PAGE;

        if (!guarded)
        {
                check("deflate parse fast: guard pages mapped", false);
                return;
        }
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
        for (positive trial = 0; trial < 210; trial++)
        {
                positive shape = trial < 180 ? trial % 6 : 6, total = 70000 + FLOOR_RANDOM() % 900, at = 0, turn = 0;
                static const positive starts[5] = {0, 3, 4096, 33000, 41000};
                positive pos = starts[trial % 5], start, limit;
                floor_fast_job got, want;

                memory_fill(data, 0, sizeof(data));
                while (at < total)
                {
                        positive run, take;

                        if (shape == 0)
                                data[at++] = (p8)FLOOR_RANDOM();
                        else if (shape == 1)
                                data[at++] = (p8)(FLOOR_RANDOM() % 4);
                        else if (shape == 2 || shape == 5)
                        {
                                run = FLOOR_RANDOM() % (shape == 5 ? 3 : 12);
                                while (run-- && at < total)
                                        data[at++] = (p8)FLOOR_RANDOM();
                                take = 4 + FLOOR_RANDOM() % (FLOOR_RANDOM() % 8 ? 20 : 300);
                                if (at > 8)
                                {
                                        positive from = 1 + FLOOR_RANDOM() % (at > 32768 ? 32768 : at);

                                        while (take-- && at < total)
                                        {
                                                data[at] = data[at - from];
                                                at++;
                                        }
                                }
                        }
                        else if (shape == 6)
                        {
                                positive from = turn++ % 2 ? 32768 : 100;

                                run = 5 + FLOOR_RANDOM() % 8;
                                if (at < 32768 + 16)
                                        data[at++] = (p8)FLOOR_RANDOM();
                                while (at >= 32768 + 16 && run-- && at < total)
                                {
                                        data[at] = data[at - from];
                                        at++;
                                }
                        }
                        else if (shape == 3)
                        {
                                run = 1 + FLOOR_RANDOM() % 700;
                                p8 value = (p8)FLOOR_RANDOM();

                                while (run-- && at < total)
                                        data[at++] = value;
                        }
                        else
                        {
                                positive period = 3 + FLOOR_RANDOM() % 5;

                                data[at] = at >= period && FLOOR_RANDOM() % 50 ? data[at - period] : (p8)FLOOR_RANDOM();
                                at++;
                        }
                }
                p8 address_to input = trial % 2 ? guarded + FLOOR_PAGE : guarded + FLOOR_PAGE + usable - total;

                memory_copy(input, data, total);
                start = pos - FLOOR_RANDOM() % (pos + 1) % 5000;
                limit = pos + 1 + FLOOR_RANDOM() % 65535;
                if (limit > total)
                        limit = total;
                memory_fill(fast_got, 0, sizeof(fast_got));
                for (positive p = 0; p < pos && p + 5 <= total; p++)
                {
                        p32 h = (memory_load_unaligned(p32, data + p) * 0x1e35a7bdu) >> 17;

                        fast_got[2 * h + 1] = fast_got[2 * h];
                        fast_got[2 * h] = (p32)(p + 1);
                }
                memory_copy(fast_want, fast_got, sizeof(fast_got));
                memory_fill(mpos_got, 0xa5, sizeof(mpos_got));
                memory_fill(mlen_got, 0xa5, sizeof(mlen_got));
                memory_fill(mdist_got, 0xa5, sizeof(mdist_got));
                memory_copy(mpos_want, mpos_got, sizeof(mpos_got));
                memory_copy(mlen_want, mlen_got, sizeof(mlen_got));
                memory_copy(mdist_want, mdist_got, sizeof(mdist_got));
                for (positive i = 0; i < 288; i++)
                        lit_got[i] = lit_want[i] = (p32)(FLOOR_RANDOM() % 5);
                for (positive i = 0; i < 32; i++)
                        dist_got[i] = dist_want[i] = (p32)(FLOOR_RANDOM() % 5);
                got.base = want.base = input;
                got.total = want.total = total;
                got.start = want.start = start;
                got.pos = want.pos = pos;
                got.limit = want.limit = limit;
                got.nice = want.nice = trial % 7 == 0 ? 8 : trial % 11 == 0 ? 16 : 32;
                got.pairs = want.pairs = trial % 13 == 0 ? 4090 : 0;
                got.pairs_max = want.pairs_max = trial % 5 == 0 ? got.pairs + 1 + FLOOR_RANDOM() % 40 : 8192;
                got.hash = want.hash = (memory_load_unaligned(p32, data + pos) * 0x1e35a7bdu) >> 17;
                got.fast = fast_got;
                want.fast = fast_want;
                got.mpos = mpos_got;
                want.mpos = mpos_want;
                got.mlen = mlen_got;
                want.mlen = mlen_want;
                got.mdist = mdist_got;
                want.mdist = mdist_want;
                got.lit_freq = lit_got;
                want.lit_freq = lit_want;
                got.dist_freq = dist_got;
                want.dist_freq = dist_want;
                floor_deflate_parse_model(address_of want);
                deflate_parse_fast(address_of got);
                anything = anything || want.pairs > 0;
                same = same && got.pos == want.pos && got.pairs == want.pairs && (p32)got.hash == (p32)want.hash &&
                       !memory_compare(fast_got, fast_want, sizeof(fast_got)) &&
                       !memory_compare(mpos_got, mpos_want, sizeof(mpos_got)) &&
                       !memory_compare(mlen_got, mlen_want, sizeof(mlen_got)) &&
                       !memory_compare(mdist_got, mdist_want, sizeof(mdist_got)) &&
                       !memory_compare(lit_got, lit_want, sizeof(lit_got)) &&
                       !memory_compare(dist_got, dist_want, sizeof(dist_got));
        }
#undef FLOOR_RANDOM
        memory_free(guarded, 24 * FLOOR_PAGE);
        check("deflate parse fast agrees with the position at a time walk: pairs, counts, table, stopping place", same && anything);
}

/* deflate_parse_chain against gzip's chain levels written out a position at
   a time: the same pairs, literals, counts, three tables, hashes and stopping
   place from blocks of random bytes, of a few symbols, of copies of earlier
   bytes, of runs, of short runs and of short periods, every shape at every
   number of links and every parse, at lengths to beat and nices that vary,
   started from the first window to past the first slide, with a pair limit,
   a split test and a count of the literals that stop it where they bite. */
typedef struct
{
        p8 address_to base;
        p16 address_to tab;
        positive total, start, pos, limit, slid, nice, depth, least, far, mode, pairs;
        p32 address_to mpos;
        p16 address_to mlen;
        p16 address_to mdist;
        p32 address_to lit_freq;
        p32 address_to dist_freq;
        p32 address_to fresh;
        positive tokens, recount, h3, h4, pairs_max, split_lo, split_hi;
} floor_chain_job;

static fn floor_chain_hashes(p8 address_to at, positive address_to h3, positive address_to h4)
{
        p32 seq = memory_load_unaligned(p32, at);

        address_to h3 = (seq << 8) * 0x1e35a7bdu >> 17;
        address_to h4 = seq * 0x1e35a7bdu >> 16;
}

/* The position goes into the tables at its place in the window. */
static fn floor_chain_insert(floor_chain_job address_to j, positive pos)
{
        p16 address_to head4 = j->tab;
        p16 address_to head3 = j->tab + 65536;
        p16 address_to prev = j->tab + 65536 + 32768;
        positive rel = pos - j->slid;

        head3[j->h3] = (p16)(rel + 32768);
        prev[rel] = head4[j->h4];
        head4[j->h4] = (p16)(rel + 32768);
        floor_chain_hashes(j->base + pos + 1, address_of j->h3, address_of j->h4);
}

static positive floor_chain_find(floor_chain_job address_to j, positive pos, positive best,
                                 positive depth, positive address_to dist)
{
        p16 address_to head4 = j->tab;
        p16 address_to head3 = j->tab + 65536;
        p16 address_to prev = j->tab + 65536 + 32768;
        positive rel = pos - j->slid;
        positive cur = rel + 32768;
        p8 address_to window = j->base + j->slid - 32768;
        p8 address_to here = j->base + pos;
        positive node3 = head3[j->h3];
        positive node = head4[j->h4];

        floor_chain_insert(j, pos);
        if (best < 4)
        {
                if (node3 <= rel)
                        return best;
                if (best < 3 && !memory_compare(window + node3, here, 3))
                {
                        best = 3;
                        address_to dist = cur - node3;
                }
        }
        else if (best >= j->nice)
                return best;
        for (; depth && node > rel; depth--, node = prev[node & 32767])
        {
                p8 address_to match = window + node;
                positive length = 0;

                while (length < 258 && match[length] == here[length])
                        length++;
                if (length >= 4 && length > best)
                {
                        best = length;
                        address_to dist = (positive)(here - match);
                        if (best >= j->nice)
                                return best;
                }
        }
        return best;
}

static bipolar floor_chain_gain(positive length, positive dist, positive next, positive next_dist)
{
        bipolar a = 0, b = 0;

        while (dist >> (a + 1))
                a++;
        while (next_dist >> (b + 1))
                b++;
        return 4 * ((bipolar)next - (bipolar)length) + a - b;
}

static fn floor_deflate_chain_model(floor_chain_job address_to j)
{
        positive pos = j->pos;
        positive stop = j->total >= 299 ? j->total - 299 : 0;

        if (j->limit < stop)
                stop = j->limit;
        while (pos < stop && !(j->tokens >= 512 && pos >= j->split_lo && pos <= j->split_hi) &&
               pos < j->recount && pos - j->slid <= 32507)
        {
                positive dist = 0, length, taken = 1;

                length = floor_chain_find(j, pos, j->least - 1, j->depth, address_of dist);
                if (length < j->least || (length == 3 && dist > j->far))
                {
                        j->lit_freq[j->base[pos]]++;
                        j->tokens++;
                        pos++;
                        continue;
                }
                if (j->mode >= 2)
                        while (length < j->nice)
                        {
                                positive next_dist = 0, next;

                                next = floor_chain_find(j, pos + 1, length - 1, (j->depth >> 1) | 1,
                                                        address_of next_dist);
                                taken = 2;
                                if (next >= length && floor_chain_gain(length, dist, next, next_dist) > 2)
                                {
                                        j->lit_freq[j->base[pos]]++;
                                        j->tokens++;
                                        pos++;
                                        length = next;
                                        dist = next_dist;
                                        taken = 1;
                                        continue;
                                }
                                if (j->mode < 3)
                                        break;
                                next = floor_chain_find(j, pos + 2, length - 1, (j->depth >> 2) | 1,
                                                        address_of next_dist);
                                taken = 3;
                                if (next >= length && floor_chain_gain(length, dist, next, next_dist) > 6)
                                {
                                        j->lit_freq[j->base[pos]]++;
                                        j->lit_freq[j->base[pos + 1]]++;
                                        j->tokens += 2;
                                        pos += 2;
                                        length = next;
                                        dist = next_dist;
                                        taken = 1;
                                        continue;
                                }
                                break;
                        }
                {
                        positive k = j->pairs++;

                        j->mpos[k] = (p32)(pos - j->start);
                        j->mlen[k] = (p16)length;
                        j->mdist[k] = (p16)dist;
                        j->tokens++;
                        j->lit_freq[257 + deflate_symbol_tab[length - 3]]++;
                        j->dist_freq[dist <= 256 ? deflate_symbol_tab[256 + dist - 1]
                                                 : deflate_symbol_tab[512 + ((dist - 1) >> 7)]]++;
                        j->fresh[8 + (length >= 9)]++;
                }
                for (positive p = pos + taken; p < pos + length; p++)
                        floor_chain_insert(j, p);
                pos += length;
                if (j->pairs >= j->pairs_max)
                        break;
        }
        j->pos = pos;
}

static fn floor_deflate_chain(void)
{
        static p8 data[70000 + 1024];
        static p16 tab_got[65536 + 32768 + 32768], tab_want[65536 + 32768 + 32768];
        static p32 mpos_got[16500], mpos_want[16500];
        static p16 mlen_got[16500], mlen_want[16500], mdist_got[16500], mdist_want[16500];
        static const positive depths[9] = {1, 2, 3, 6, 16, 35, 100, 300, 600};
        static const positive nices[7] = {5, 10, 14, 30, 65, 130, 258};
        static const positive leasts[5] = {3, 4, 5, 7, 9};
        static const positive starts[8] = {0, 3, 4096, 20000, 32400, 33000, 41000, 64900};
        p32 lit_got[288], lit_want[288], dist_got[32], dist_want[32], fresh_got[10], fresh_want[10];
        p32 random = 0x5d2f1b97u;
        p8 same = true, anything = false;

#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
        for (positive trial = 0; trial < 189; trial++)
        {
                positive shape = trial % 7, total = 70000 + FLOOR_RANDOM() % 900, at = 0;
                positive pos = starts[trial % 8], before;
                floor_chain_job got, want;

                memory_fill(data, 0, sizeof(data));
                while (at < total)
                {
                        positive run, take;

                        if (shape == 0)
                                data[at++] = (p8)FLOOR_RANDOM();
                        else if (shape == 1)
                                data[at++] = (p8)(FLOOR_RANDOM() % 4);
                        else if (shape == 2 || shape == 5)
                        {
                                run = FLOOR_RANDOM() % (shape == 5 ? 3 : 12);
                                while (run-- && at < total)
                                        data[at++] = (p8)FLOOR_RANDOM();
                                take = 3 + FLOOR_RANDOM() % (FLOOR_RANDOM() % 8 ? 20 : 300);
                                if (at > 8)
                                {
                                        positive from = 1 + FLOOR_RANDOM() % (at > 32767 ? 32767 : at);

                                        while (take-- && at < total)
                                        {
                                                data[at] = data[at - from];
                                                at++;
                                        }
                                }
                        }
                        else if (shape == 3)
                        {
                                run = 1 + FLOOR_RANDOM() % 700;
                                p8 value = (p8)FLOOR_RANDOM();

                                while (run-- && at < total)
                                        data[at++] = value;
                        }
                        else if (shape == 6)
                        {
                                p8 value = (p8)(FLOOR_RANDOM() % 3);

                                run = 2 + FLOOR_RANDOM() % 40;
                                while (run-- && at < total)
                                        data[at++] = value;
                        }
                        else
                        {
                                positive period = 3 + FLOOR_RANDOM() % 5;

                                data[at] = at >= period && FLOOR_RANDOM() % 50 ? data[at - period] : (p8)FLOOR_RANDOM();
                                at++;
                        }
                }
                memory_fill(tab_got, 0, sizeof(tab_got));
                got.base = data;
                got.tab = tab_got;
                got.slid = 0;
                floor_chain_hashes(data, address_of got.h3, address_of got.h4);
                for (positive p = 0; p < pos; p++)
                {
                        if (p - got.slid >= 32768)
                        {
                                for (positive i = 0; i < sizeof(tab_got) / sizeof(p16); i++)
                                        tab_got[i] = (p16)(tab_got[i] > 32768 ? tab_got[i] - 32768 : 0);
                                got.slid += 32768;
                        }
                        floor_chain_insert(address_of got, p);
                }
                if (pos - got.slid >= 32768)
                {
                        for (positive i = 0; i < sizeof(tab_got) / sizeof(p16); i++)
                                tab_got[i] = (p16)(tab_got[i] > 32768 ? tab_got[i] - 32768 : 0);
                        got.slid += 32768;
                }
                memory_copy(tab_want, tab_got, sizeof(tab_got));
                memory_fill(mpos_got, 0xa5, sizeof(mpos_got));
                memory_fill(mlen_got, 0xa5, sizeof(mlen_got));
                memory_fill(mdist_got, 0xa5, sizeof(mdist_got));
                memory_copy(mpos_want, mpos_got, sizeof(mpos_got));
                memory_copy(mlen_want, mlen_got, sizeof(mlen_got));
                memory_copy(mdist_want, mdist_got, sizeof(mdist_got));
                for (positive i = 0; i < 288; i++)
                        lit_got[i] = lit_want[i] = (p32)(FLOOR_RANDOM() % 5);
                for (positive i = 0; i < 32; i++)
                        dist_got[i] = dist_want[i] = (p32)(FLOOR_RANDOM() % 5);
                for (positive i = 0; i < 10; i++)
                        fresh_got[i] = fresh_want[i] = (p32)(FLOOR_RANDOM() % 5);
                got.total = total;
                got.start = pos - FLOOR_RANDOM() % (pos + 1) % 5000;
                got.pos = pos;
                got.limit = trial % 4 == 0 ? pos + 1 + FLOOR_RANDOM() % 9000 : total;
                got.nice = nices[(trial / 3) % 7];
                got.depth = depths[(trial / 7) % 9];
                got.least = leasts[(trial / 2) % 5];
                got.mode = 1 + (trial / 63) % 3;
                got.far = got.mode >= 2 ? 8192 : 4096;
                got.pairs = trial % 13 == 0 ? 16380 : 0;
                got.pairs_max = trial % 5 == 0 ? got.pairs + 1 + FLOOR_RANDOM() % 40 : 16382;
                got.tokens = trial % 7 == 0 ? 500 + FLOOR_RANDOM() % 20 : FLOOR_RANDOM() % 400;
                got.recount = got.mode >= 2 && trial % 3 != 1 ? pos + 1 + FLOOR_RANDOM() % 20000 : total;
                got.split_lo = got.start + 5000;
                got.split_hi = total - 5000;
                got.mpos = mpos_got;
                got.mlen = mlen_got;
                got.mdist = mdist_got;
                got.lit_freq = lit_got;
                got.dist_freq = dist_got;
                got.fresh = fresh_got;
                want = got;
                want.tab = tab_want;
                want.mpos = mpos_want;
                want.mlen = mlen_want;
                want.mdist = mdist_want;
                want.lit_freq = lit_want;
                want.dist_freq = dist_want;
                want.fresh = fresh_want;
                before = want.pairs;
                floor_deflate_chain_model(address_of want);
                deflate_parse_chain(address_of got);
                anything = anything || want.pairs > before;
                same = same && got.pos == want.pos && got.pairs == want.pairs && got.tokens == want.tokens &&
                       (p32)got.h3 == (p32)want.h3 && (p32)got.h4 == (p32)want.h4 &&
                       !memory_compare(tab_got, tab_want, sizeof(tab_got)) &&
                       !memory_compare(mpos_got, mpos_want, sizeof(mpos_got)) &&
                       !memory_compare(mlen_got, mlen_want, sizeof(mlen_got)) &&
                       !memory_compare(mdist_got, mdist_want, sizeof(mdist_got)) &&
                       !memory_compare(lit_got, lit_want, sizeof(lit_got)) &&
                       !memory_compare(dist_got, dist_want, sizeof(dist_got)) &&
                       !memory_compare(fresh_got, fresh_want, sizeof(fresh_got));
        }
#undef FLOOR_RANDOM
        check("deflate parse chain agrees with the position at a time walk: pairs, counts, tables, hashes, stopping place",
              same && anything);
}

/* zstd_huffman_cells against a cell at a time: complete codes from
   huffman_lengths over random counts, as weights, into a guarded table. */
static fn floor_zstd_huffman_cells(void)
{
        static p32 freq[256];
        p8 length[256], weight[256];
        p16 want[2048 + 8], got[2048 + 8];
        p32 random = 0x6b43a9b5u;
        p8 same = true;

        for (positive trial = 0; trial < 20000; trial++)
        {
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive count = 2 + FLOOR_RANDOM() % 255, max_bits = 0;
                p32 first[13] = {0}, place[13] = {0}, rank[13] = {0};

                for (positive i = 0; i < count; i++)
                        freq[i] = FLOOR_RANDOM() % 3 ? FLOOR_RANDOM() % (1 + FLOOR_RANDOM() % 5000) : 0;
                freq[0] |= 1;
                freq[count - 1] |= 1;
                if (!huffman_lengths(freq, count, length, 11))
                        continue;
                for (positive i = 0; i < count; i++)
                        if (length[i] > max_bits)
                                max_bits = length[i];
                for (positive i = 0; i < count; i++)
                {
                        weight[i] = length[i] ? (p8)(max_bits + 1 - length[i]) : 0;
                        rank[weight[i]]++;
                }
                for (positive w = 1, start = 0; w <= 12; w++)
                {
                        first[w] = place[w] = (p32)start;
                        if (w <= max_bits)
                                start += (positive)rank[w] << (w - 1 + 11 - max_bits);
                }
                memory_fill(want, 0xa5, sizeof(want));
                memory_fill(got, 0xa5, sizeof(got));
                for (positive i = 0; i < count; i++)
                        if (weight[i])
                                for (positive k = 0; k < (positive)1 << (weight[i] - 1 + 11 - max_bits); k++)
                                        want[place[weight[i]]++] = (p16)((max_bits + 1 - weight[i]) | i << 8);
                zstd_huffman_cells(got, weight, count, max_bits, first);
                if (memory_compare(got, want, sizeof(got)) || memory_compare(first, place, sizeof(first)))
                        same = false;
#undef FLOOR_RANDOM
        }
        check("zstd Huffman cells fill the table a symbol at a time would", same);
}

/* huffman_codes against RFC 1951's canonical assignment, reversed a bit
   at a time, over lengths huffman_lengths makes and fixed-code shapes. */
static fn floor_huffman_codes(void)
{
        static p32 freq[288];
        p8 length[288];
        p32 got[288], want[288];
        p32 random = 0x85ebca6bu;
        p8 same = true;

        for (positive trial = 0; trial < 20000; trial++)
        {
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive n = 1 + FLOOR_RANDOM() % 288, limit = 7 + FLOOR_RANDOM() % 9;
                p32 next[16] = {0}, count[16] = {0}, code = 0;

                for (positive i = 0; i < n; i++)
                        freq[i] = FLOOR_RANDOM() % 4 ? FLOOR_RANDOM() % (1 + FLOOR_RANDOM() % 3000) : 0;
                if (trial % 7 == 0)
                        for (positive i = 0; i < n; i++)
                                length[i] = (p8)(i % 16);
                else if (!huffman_lengths(freq, n, length, limit))
                        continue;
                for (positive i = 0; i < n; i++)
                        count[length[i]]++;
                count[0] = 0;
                for (positive len = 1; len < 16; len++)
                {
                        code = (code + count[len - 1]) << 1;
                        next[len] = code;
                }
                for (positive i = 0; i < n; i++)
                {
                        p32 value = 0, c;

                        want[i] = 0;
                        if (!length[i])
                                continue;
                        c = next[length[i]]++;
                        for (positive b = 0; b < length[i]; b++, c >>= 1)
                                value = value << 1 | (c & 1);
                        want[i] = value | (p32)length[i] << 16;
                }
                huffman_codes(length, n, got);
                if (memory_compare(got, want, n * sizeof(p32)))
                        same = false;
#undef FLOOR_RANDOM
        }
        check("huffman_codes gives deflate's canonical codes, reversed", same);
}

/* zstd_fse_cells against a cell at a time over random spreads of random
   counts at every table size a sequence table has. */
static fn floor_zstd_fse_cells(void)
{
        p8 symbol[512];
        p16 next[64], model_next[64];
        p64 template[64], got[512], want[512];
        p32 random = 0x27d4eb2fu;
        p8 same = true;

        for (positive trial = 0; trial < 20000; trial++)
        {
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive log = 5 + FLOOR_RANDOM() % 5, size = (positive)1 << log;
                positive symbols = 1 + FLOOR_RANDOM() % 53;

                memory_fill(next, 0, sizeof(next));
                for (positive u = 0; u < size; u++)
                {
                        symbol[u] = (p8)(FLOOR_RANDOM() % symbols);
                        next[symbol[u]]++;
                }
                for (positive s = 0; s < 64; s++)
                        template[s] = (p64)FLOOR_RANDOM() << 32 | (p64)(FLOOR_RANDOM() & 255) << 16;
                memory_copy(model_next, next, sizeof(next));
                for (positive u = 0; u < size; u++)
                {
                        p16 n = model_next[symbol[u]]++;
                        positive bits = log - (63 - bits_leading_zeros(n));

                        want[u] = template[symbol[u]] | (p64)bits << 24 |
                                  (p16)(((positive)n << bits) - size);
                }
                zstd_fse_cells(got, symbol, size, log, next, template);
                if (memory_compare(got, want, size * sizeof(p64)) ||
                    memory_compare(next, model_next, sizeof(next)))
                        same = false;
#undef FLOOR_RANDOM
        }
        check("zstd sequence table cells match a cell at a time", same);
}

/* zstd_huffman_codes against zstd's order: weight by weight from the
   lightest, symbols in order within a weight, over lengths huffman_lengths
   makes at eleven bits. */
static fn floor_zstd_huffman_codes(void)
{
        static p32 freq[256];
        p8 length[256], weight[256];
        p32 got[256], want[256];
        p32 random = 0x94d049bbu;
        p8 same = true;

        for (positive trial = 0; trial < 20000; trial++)
        {
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive count = 2 + FLOOR_RANDOM() % 255, max_bits = 0, start = 0;

                for (positive i = 0; i < count; i++)
                        freq[i] = FLOOR_RANDOM() % 3 ? FLOOR_RANDOM() % (1 + FLOOR_RANDOM() % 4000) : 0;
                freq[0] |= 1;
                freq[count - 1] |= 1;
                if (!huffman_lengths(freq, count, length, 11))
                        continue;
                for (positive i = 0; i < count; i++)
                        if (length[i] > max_bits)
                                max_bits = length[i];
                for (positive i = 0; i < count; i++)
                        weight[i] = length[i] ? (p8)(max_bits + 1 - length[i]) : 0;
                memory_fill(want, 0x5a, sizeof(want));
                memory_fill(got, 0x5a, sizeof(got));
                for (positive w = 1; w <= max_bits; w++)
                        for (positive i = 0; i < count; i++)
                                if (weight[i] == w)
                                {
                                        want[i] = (p32)(length[i] << 16 | start >> (w - 1));
                                        start += (positive)1 << (w - 1);
                                }
                zstd_huffman_codes(got, weight, count, max_bits);
                if (memory_compare(got, want, sizeof(got)))
                        same = false;
#undef FLOOR_RANDOM
        }
        check("zstd Huffman codes in zstd's order", same);
}

/* zstd_sequences_encode against a bit at a time model: random sequences
   (every code, base and extra width, up to the longest of each) over random
   tables of any log and the all-zero table of a one-symbol stream, every
   pending width on entry, the output ending at a protected page. A call
   given 16 bytes a sequence and 8 over must finish; one given less may
   answer that the room ran out, and when it does not it must have written
   exactly what the model did. */
typedef struct
{
        p32 lit, match, off;
        p8 ll, ml, of, spare;
} floor_zseq;

typedef struct
{
        p16 state[512];
        p32 delta_nb[53];
        bipolar delta_find[53];
        p8 log;
} floor_zct;

typedef struct
{
        const floor_zseq address_to seqs;
        positive count;
        floor_zct address_to of;
        floor_zct address_to ml;
        floor_zct address_to ll;
        positive of_state;
        positive ml_state;
        positive ll_state;
        p64 acc;
        positive bits;
        p8 address_to out;
        p8 address_to limit;
} floor_zjob;

static const p8 floor_zll_extra[36] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const p32 floor_zll_base[36] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096,
        0x2000, 16384, 32768, 65536};
static const p8 floor_zml_extra[53] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const p32 floor_zml_base[53] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
        35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051,
        4099, 8195, 16387, 32771, 65539};

/* A table of 1 << log states over the symbols that are used, each at least
   once and the rest dealt out at random, spread at random: the layout of
   zstd_ctable_build, so every state a step can reach is in the table. */
static fn floor_zstd_table(floor_zct address_to ct, const p8 address_to used, positive symbols,
                           positive log, p32 address_to random)
{
#define FLOOR_RANDOM() (*random ^= *random << 13, *random ^= *random >> 17, *random ^= *random << 5, *random)
        positive size = (positive)1 << log;
        p16 count[53] = {0}, cumul[54];
        p8 symbol[512];
        positive left = size, total = 0, at = 0;

        for (positive s = 0; s < symbols; s++)
                if (used[s])
                {
                        count[s] = 1;
                        left--;
                }
        while (left)
        {
                positive s = FLOOR_RANDOM() % symbols;
                if (used[s])
                {
                        count[s]++;
                        left--;
                }
        }
        cumul[0] = 0;
        for (positive s = 0; s < symbols; s++)
                cumul[s + 1] = (p16)(cumul[s] + count[s]);
        for (positive s = 0; s < symbols; s++)
                for (positive k = 0; k < count[s]; k++)
                        symbol[at++] = (p8)s;
        for (positive u = size; u > 1; u--)
        {
                positive v = FLOOR_RANDOM() % u;
                p8 swap = symbol[u - 1];
                symbol[u - 1] = symbol[v];
                symbol[v] = swap;
        }
        for (positive u = 0; u < size; u++)
                ct->state[cumul[symbol[u]]++] = (p16)(size + u);
        memory_fill(ct->delta_nb, 0, sizeof(ct->delta_nb));
        memory_fill(ct->delta_find, 0, sizeof(ct->delta_find));
        for (positive s = 0; s < symbols; s++)
        {
                positive n = count[s];

                if (!n)
                        continue;
                if (n == 1)
                {
                        ct->delta_nb[s] = ((p32)log << 16) - ((p32)1 << log);
                        ct->delta_find[s] = (bipolar)total - 1;
                }
                else
                {
                        positive top = 0;
                        positive max_bits;

                        while (((positive)1 << (top + 1)) <= n - 1)
                                top++;
                        max_bits = log - top;
                        ct->delta_nb[s] = (p32)((max_bits << 16) - (n << max_bits));
                        ct->delta_find[s] = (bipolar)total - (bipolar)n;
                }
                total += n;
        }
        ct->log = (p8)log;
#undef FLOOR_RANDOM
}

static fn floor_zstd_put(p8 address_to out, positive address_to at, p64 value, positive n)
{
        for (positive i = 0; i < n; i++, (*at)++)
                out[*at / 8] = (p8)((out[*at / 8] & ~(1u << (*at % 8))) | (((value >> i) & 1) << (*at % 8)));
}

static fn floor_zstd_sequences_encode_run(void)
{
        static floor_zseq seqs[320];
        static floor_zct tables[3], zero;
        static p8 want[8192];
        p8 address_to pages = floor_pages(4);
        p32 random = 0x3c6ef372u;
        p8 same = true, finished = true, short_ok = true;
        positive ran = 0, refused = 0;

        check("sequence encoder protected mappings", pages != null);
        if (!pages)
                return;
        for (positive trial = 0; trial < 4000; trial++)
        {
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
                positive count = 1 + FLOOR_RANDOM() % (trial % 7 ? 40 : 320);
                positive wild = FLOOR_RANDOM() % 4;
                p8 used[3][53] = {{0}};
                const positive symbols[3] = {32, 53, 36};
                const positive limits[3] = {8, 9, 9};
                floor_zct address_to ct[3];
                p64 begin[3];
                p64 finish[3];
                p8 rle[3];
                positive bit = 0, held = FLOOR_RANDOM() % 8;
                p64 pending = FLOOR_RANDOM() & (((p64)1 << held) - 1);

                for (positive i = 0; i < count; i++)
                {
                        positive ll = wild == 0 ? FLOOR_RANDOM() % 36
                                                : (FLOOR_RANDOM() % 5 ? FLOOR_RANDOM() % 20 : 20 + FLOOR_RANDOM() % 16);
                        positive ml = wild == 0 ? FLOOR_RANDOM() % 53
                                                : (FLOOR_RANDOM() % 5 ? FLOOR_RANDOM() % 36 : 36 + FLOOR_RANDOM() % 17);
                        positive of = wild == 0 ? FLOOR_RANDOM() % 32
                                                : (FLOOR_RANDOM() % 5 ? FLOOR_RANDOM() % 20 : 20 + FLOOR_RANDOM() % 12);

                        if (wild == 3 && !(i % 5))
                                ll = 35, ml = 52, of = 31;
                        seqs[i].ll = (p8)ll;
                        seqs[i].ml = (p8)ml;
                        seqs[i].of = (p8)of;
                        seqs[i].lit = floor_zll_base[ll] + (FLOOR_RANDOM() & (((p32)1 << floor_zll_extra[ll]) - 1));
                        seqs[i].match = floor_zml_base[ml] + (FLOOR_RANDOM() & (((p32)1 << floor_zml_extra[ml]) - 1));
                        seqs[i].off = ((p32)1 << of) + (of ? (FLOOR_RANDOM() & (((p32)1 << of) - 1)) : 0);
                        used[0][of] = used[1][ml] = used[2][ll] = 1;
                }
                for (positive k = 0; k < 3; k++)
                {
                        positive kinds = 0, log = 1;

                        for (positive s = 0; s < symbols[k]; s++)
                                kinds += used[k][s];
                        rle[k] = FLOOR_RANDOM() % 6 == 0;
                        if (rle[k])
                        {
                                memory_fill(address_of zero, 0, sizeof(zero));
                                ct[k] = address_of zero;
                                begin[k] = 0;
                                continue;
                        }
                        while (((positive)1 << log) < kinds)
                                log++;
                        log += FLOOR_RANDOM() % (limits[k] - log + 1);
                        ct[k] = tables + k;
                        floor_zstd_table(ct[k], used[k], symbols[k], log, address_of random);
                        begin[k] = ((p64)1 << log) + FLOOR_RANDOM() % ((positive)1 << log);
                }
                //      The model: the sequences last to first, a bit at a time.
                memory_fill(want, 0xa5, sizeof(want));
                floor_zstd_put(want, address_of bit, pending, held);
                for (positive k = 0; k < 3; k++)
                        finish[k] = begin[k];
                for (positive i = count; i--;)
                {
                        const p8 code[3] = {seqs[i].of, seqs[i].ml, seqs[i].ll};

                        for (positive k = 0; k < 3; k++)
                        {
                                positive nb = (positive)((finish[k] + ct[k]->delta_nb[code[k]]) >> 16);

                                floor_zstd_put(want, address_of bit, finish[k], nb);
                                finish[k] = ct[k]->state[(bipolar)(finish[k] >> nb) + ct[k]->delta_find[code[k]]];
                        }
                        floor_zstd_put(want, address_of bit, seqs[i].lit - floor_zll_base[seqs[i].ll],
                                       floor_zll_extra[seqs[i].ll]);
                        floor_zstd_put(want, address_of bit, seqs[i].match - floor_zml_base[seqs[i].ml],
                                       floor_zml_extra[seqs[i].ml]);
                        floor_zstd_put(want, address_of bit, seqs[i].off - ((p32)1 << seqs[i].of), seqs[i].of);
                }
                //      Room: enough for the call to finish, or anything less.
                {
                        positive const enough = 16 * count + 8;
                        positive const room = trial % 3 ? enough + FLOOR_RANDOM() % 24
                                                        : FLOOR_RANDOM() % (enough + 24);
                        p8 address_to const limit = pages + 3 * FLOOR_PAGE;
                        p8 address_to const start = limit - room;
                        floor_zjob job = {seqs, count, ct[0], ct[1], ct[2], begin[0], begin[1], begin[2],
                                          pending, held, start, limit};
                        bipolar result;

                        memory_fill(start, 0xa5, room);
                        result = zstd_sequences_encode(address_of job);
                        if (room >= enough)
                        {
                                finished = finished && result == 0;
                                ran++;
                        }
                        else if (result)
                        {
                                short_ok = short_ok && result == 1;
                                refused++;
                                continue;
                        }
                        if (!result)
                        {
                                positive whole = bit / 8;

                                same = same && (positive)(job.out - start) == whole && job.bits == bit % 8 &&
                                       job.count == 0 &&
                                       !memory_compare(start, want, whole) &&
                                       job.acc == (bit % 8 ? want[whole] & ((1u << (bit % 8)) - 1) : 0) &&
                                       job.of_state == finish[0] && job.ml_state == finish[1] &&
                                       job.ll_state == finish[2];
                        }
                }
#undef FLOOR_RANDOM
        }
        check("zstd sequences written bit-exact from any pending width", same);
        check("zstd sequences finish when given 16 bytes a sequence and 8", finished && ran > 1000);
        check("zstd sequences answer 1 and nothing else when the room runs out", short_ok && refused > 100);
        {
                //      A limit before out is no room, and nothing is written.
                floor_zjob job = {seqs, 1, address_of zero, address_of zero, address_of zero, 0, 0, 0, 0, 0,
                                  pages + FLOOR_PAGE + 64, pages + FLOOR_PAGE + 32};

                seqs[0].lit = 0;
                seqs[0].match = 3;
                seqs[0].off = 1;
                seqs[0].ll = seqs[0].ml = seqs[0].of = 0;
                memory_fill(address_of zero, 0, sizeof(zero));
                check("zstd sequences answer 1 when the limit is before out", zstd_sequences_encode(address_of job) == 1);
        }
        memory_free(pages, 4 * FLOOR_PAGE);
}

static fn floor_zstd_sequences_encode(void)
{
#if X64
        p8 const probed = cpu_hash_probed;
        p8 const mulx = cpu_has_mulx;

        //      The body without BMI2 is what the kernel and older processors
        //      run and a processor with it never would, so it is walked to.
        cpu_hash_probed = 1;
        cpu_has_mulx = 0;
        floor_zstd_sequences_encode_run();
        cpu_hash_probed = probed;
        cpu_has_mulx = mulx;
#endif
        floor_zstd_sequences_encode_run();
}

/* lzma_tree_walk against xz's binary tree walk written out a node at a time
   (the C it replaced): the pairs it finds, the tree it leaves and the
   places it stops, from blocks of random bytes, of a few symbols, of copies
   of earlier bytes, of runs and of short periods, every shape at depths from
   one to 512, nices from 4 to 273, windows small enough to wrap many times
   and positions near the top of 32 bits, finds and skips mixed, with the
   block ending where the bytes the routine may read end against a
   protected page. */
static xz_found address_to floor_tree_find_model(p32 address_to son, p8 address_to cur, p32 pos,
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

static fn floor_tree_skip_model(p32 address_to son, p8 address_to cur, p32 pos, p32 cur_match,
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


static fn floor_lzma_walk(void)
{
        static const p32 nices[7] = {4, 5, 8, 16, 32, 64, 273};
        static const p32 depths[8] = {1, 2, 3, 6, 16, 48, 130, 512};
        static const p32 sizes[6] = {37, 100, 256, 1000, 3000, 9000};
        p8 address_to pages = floor_pages(5);
        static p32 hash_want[4096], hash_got[4096], son_want[2 * 9001 + 4], son_got[2 * 9001 + 4];
        static xz_found found_want[XZ_MATCH_MAX + 2], found_got[XZ_MATCH_MAX + 2];
        p32 random = 0x2f6e91b5u;
        p8 same = true, anything = false, longs = false;

        if (!pages)
        {
                check("lzma tree walk agrees with the node at a time walk: pairs, tree, stopping place", false);
                return;
        }
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
        for (positive trial = 0; trial < 1260 && same; trial++)
        {
                positive n = 600 + FLOOR_RANDOM() % 5400;
                p32 size = sizes[trial % 6] + 1;
                p32 nice = nices[(trial / 6) % 7];
                p32 depth = depths[(trial / 42) % 8];
                p32 shape = (p32)(trial / 336);
                p32 alphabet = 2 + FLOOR_RANDOM() % 5;
                p32 offset = size + (trial % 5 == 4 ? 0xe0000000u : 0);
                p8 address_to block = pages + FLOOR_PAGE * 4 - n - 15;
                p32 cyclic_pos = 0;

                //      The block ends 15 bytes short of the protected page: the routine reads no
                //      further than that past the limit.
                for (positive i = 0; i < n + 15; i++)
                        block[i] = 0;
                for (positive i = 0; i < n;)
                {
                        positive run = 1 + FLOOR_RANDOM() % 40;

                        switch ((shape + (FLOOR_RANDOM() % 4 == 0 ? FLOOR_RANDOM() : 0)) % 5)
                        {
                        case 0:
                                for (positive k = 0; k < run && i < n; k++)
                                        block[i++] = (p8)(FLOOR_RANDOM() % alphabet);
                                break;
                        case 1:
                                for (positive k = 0; k < run * 3 && i < n; k++)
                                        block[i++] = (p8)alphabet;
                                break;
                        case 2:
                        {
                                positive back = 1 + FLOOR_RANDOM() % (i + 1);
                                positive copy = run * (1 + FLOOR_RANDOM() % 12);

                                for (positive k = 0; k < copy && i < n; k++, i++)
                                        block[i] = i >= back ? block[i - back] : (p8)(FLOOR_RANDOM() % 3);
                                break;
                        }
                        case 3:
                        {
                                positive period = 1 + FLOOR_RANDOM() % 9;

                                for (positive k = 0; k < run * 4 && i < n; k++, i++)
                                        block[i] = i >= period ? block[i - period] : (p8)(FLOOR_RANDOM() % 4);
                                break;
                        }
                        default:
                                for (positive k = 0; k < run && i < n; k++)
                                        block[i++] = (p8)(FLOOR_RANDOM());
                                break;
                        }
                }
                memory_fill(hash_want, 0, sizeof(hash_want));
                memory_fill(hash_got, 0, sizeof(hash_got));
                memory_fill(son_want, 0xa5, sizeof(son_want));
                memory_fill(son_got, 0xa5, sizeof(son_got));
                for (positive i = 0; i < 2 * size; i++)
                        son_want[i] = son_got[i] = 0;
                for (positive at = 0; at + 4 <= n && same; at++)
                {
                        p32 pos = (p32)at + offset;
                        p32 avail = (p32)(n - at);
                        p32 limit = avail < nice ? avail : nice;
                        p8 address_to cur = block + at;
                        p32 h = (cur[0] * 0x9e3779b1u ^ cur[1] * 0x85ebca6bu ^ cur[2] * 0xc2b2ae35u ^ cur[3] * 0x27d4eb2fu) >> 20;
                        p32 cur_match = hash_want[h];
                        p32 best = 3 + FLOOR_RANDOM() % 3;
                        p8 find = FLOOR_RANDOM() % 4 != 0;

                        if (best >= limit)
                                best = limit - 1;
                        hash_want[h] = hash_got[h] = pos;
                        if (find)
                        {
                                xz_found address_to end_want;
                                xz_found address_to end_got;

                                memory_fill(found_want, 0xcc, sizeof(found_want));
                                memory_fill(found_got, 0xcc, sizeof(found_got));
                                end_want = floor_tree_find_model(son_want, cur, pos, cur_match, depth,
                                                                 cyclic_pos, size, limit, found_want, best);
                                end_got = (xz_found address_to)lzma_tree_walk(
                                        address_of (xz_tree_job){son_got, cur, found_got, pos, cur_match, depth,
                                                       cyclic_pos, size, limit, best, 1});
                                same = same && end_got - found_got == end_want - found_want;
                                for (positive k = 0; k < (positive)(end_want - found_want) && same; k++)
                                        same = found_got[k].len == found_want[k].len &&
                                               found_got[k].dist == found_want[k].dist;
                                anything = anything || end_want > found_want;
                                longs = longs || (end_want > found_want && end_want[-1].len == limit);
                        }
                        else
                        {
                                same = same && !lzma_tree_walk(address_of (xz_tree_job){son_got, cur, null, pos, cur_match, depth,
                                                                              cyclic_pos, size, limit, 0, 0});
                                floor_tree_skip_model(son_want, cur, pos, cur_match, depth, cyclic_pos, size, limit);
                        }
                        if (++cyclic_pos == size)
                                cyclic_pos = 0;
                        if (at % 64 == 0 || at + 5 > n)
                                same = same && !memory_compare(son_got, son_want, sizeof(son_got));
                }
                same = same && !memory_compare(son_got, son_want, sizeof(son_got));
        }
#undef FLOOR_RANDOM
        memory_free(pages, 5 * FLOOR_PAGE);
        check("lzma tree walk agrees with the node at a time walk: pairs, tree, stopping place",
              same && anything && longs);
}

/* lzma_match_prices and lzma_relax against the loops of xz.c's optimal parser that they replaced, written
   out a length at a time: for random matches (near and far distances, lengths increasing, started anywhere
   in them), random price tables and tables of costs, the same entries of the four arrays changed and the
   same stops, where a match may be followed by a literal and the same distance and the parser goes on in C
   (here a canned update stands for that work, in the same place of the order). Once for each body the
   machine has (zmm, ymm and one by one on x86-64), the buffer ending where the bytes the routine reads end
   against a protected page. */
#define FLOOR_TABLE (XZ_OPTS + XZ_OPT_PAD)

//      What the parser does in C at a match that may be followed by a literal and the same distance:
//      a price tried against an entry past the match, then the entry (so a later length can meet it).
static positive floor_prices_extension(p32 address_to table, p32 cur, p32 len, p32 dist, p32 base, p32 avail,
                                       p32 nice, const p8 address_to buf)
{
        const p8 address_to back = buf - dist - 1;
        p32 len_2 = len + 1;
        p32 limit = avail < len_2 + nice ? avail : len_2 + nice;

        while (len_2 < limit && buf[len_2] == back[len_2])
                len_2++;
        len_2 -= len + 1;
        if (len_2 < 2)
                return 0;

        p32 offset = cur + len + 1 + len_2;
        p32 cost = base + 3 * len + len_2;

        if (cost < table[offset])
        {
                table[offset] = cost;
                table[FLOOR_TABLE + offset] = cur + len + 1;
                table[2 * FLOOR_TABLE + offset] = 0;
                table[3 * FLOOR_TABLE + offset] = 1;
        }
        return 1;
}

static positive floor_prices_model(xz_match_job address_to j, p32 address_to table, p32 nice, positive address_to stops)
{
        positive count = 0;
        p32 len = (p32)j->len;
        p32 cur = (p32)j->cur;

        for (positive i = j->index;; len++)
        {
                p32 hi = j->matches[i].len, dist = j->matches[i].dist;
                p32 ds = len < 6 ? len - 2 : 3;
                p32 base = dist < XZ_FULL_DIST ? j->dist_prices[ds * XZ_FULL_DIST + dist]
                                               : j->slot_prices[ds * XZ_DIST_SLOTS + xz_slot(dist)] +
                                                         j->align_prices[dist & 15];
                p32 cost = (p32)j->base + base + j->prices[len - 2];

                if (cost < table[cur + len])
                {
                        table[cur + len] = cost;
                        table[FLOOR_TABLE + cur + len] = cur;
                        table[2 * FLOOR_TABLE + cur + len] = dist + 4;
                        table[3 * FLOOR_TABLE + cur + len] = 0;
                }
                if (len != hi)
                        continue;
                if (j->stop && floor_prices_extension(table, cur, len, dist, (p32)j->base, (p32)j->avail, nice, j->buf))
                        count = count * 31 + len + 1000 * i;
                if (++i == j->count)
                        break;
        }
        *stops = count;
        return count;
}

static fn floor_lzma_prices(void)
{
        static p32 want[4 * FLOOR_TABLE], got[4 * FLOOR_TABLE];
        static p32 prices[300], dists[XZ_FULL_DIST * 4], slots[XZ_DIST_SLOTS * 4], aligns[16];
        static xz_found matches[16];
        p8 address_to region = floor_pages(42);
        p32 random = 0x7f4a7c15u;
        p8 same = true, extended = false, far = false;
        positive bodies = 1;

        if (!region)
        {
                check("lzma match prices agree with the length at a time loops: entries, stops", false);
                return;
        }
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;

        bodies = 3;
#endif
#define FLOOR_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
        for (positive body = 0; body < bodies; body++)
        {
#if X64
                cpu_has_avx2 = body < 2 ? avx2 : 0;
                cpu_has_avx512 = body < 1 ? avx512 : 0;
#endif
                for (positive trial = 0; trial < 3000 && same; trial++)
                {
                        positive count = 1 + FLOOR_RANDOM() % 10;
                        p32 cur = FLOOR_RANDOM() % 3500;
                        p32 avail = FLOOR_RANDOM() % 130;
                        p32 nice = 16 + FLOOR_RANDOM() % 258;
                        p8 address_to buf = region + 41 * FLOOR_PAGE - avail;
                        p32 len = 2;
                        xz_match_job job;

                        for (positive i = 0; i < count; i++)
                        {
                                len += 1 + FLOOR_RANDOM() % (trial % 4 == 0 ? 25 : 6);
                                matches[i].len = len;
                                matches[i].dist = FLOOR_RANDOM() % 3 == 0 ? FLOOR_RANDOM() % 130
                                                  : FLOOR_RANDOM() % 2 ? FLOOR_RANDOM() % 4096 : FLOOR_RANDOM() % 100000;
                                far = far || matches[i].dist >= XZ_FULL_DIST;
                        }
                        for (positive i = 0; i < sizeof(prices) / sizeof(prices[0]); i++)
                                prices[i] = FLOOR_RANDOM() % 3000;
                        for (positive i = 0; i < sizeof(dists) / sizeof(dists[0]); i++)
                                dists[i] = FLOOR_RANDOM() % 9000;
                        for (positive i = 0; i < sizeof(slots) / sizeof(slots[0]); i++)
                                slots[i] = FLOOR_RANDOM() % 9000;
                        for (positive i = 0; i < 16; i++)
                                aligns[i] = FLOOR_RANDOM() % 300;
                        for (positive i = 0; i < 4 * FLOOR_TABLE; i++)
                                want[i] = FLOOR_RANDOM() % 5 ? FLOOR_RANDOM() % 20000 : FLOOR_RANDOM() % 3 ? XZ_INFINITY_PRICE : 5;
                        memory_copy(got, want, sizeof(got));
                        //      Bytes that repeat at the distances, so the two after a match sometimes agree.
                        for (positive i = 0; i < 100001 + avail; i++)
                                (buf - 100001)[i] = (p8)(FLOOR_RANDOM() % 3);
                        for (positive i = 0; i < count; i++)
                                if (FLOOR_RANDOM() % 2 && matches[i].len + 3 <= avail)
                                {
                                        p8 address_to at = buf + matches[i].len - matches[i].dist;

                                        at[0] = buf[matches[i].len + 1];
                                        at[1] = buf[matches[i].len + 2];
                                }
                        positive first = FLOOR_RANDOM() % count;
                        p32 start = first ? matches[first - 1].len + 1 : 2;

                        job = (xz_match_job){got, matches, prices, dists, slots, aligns,
                                             FLOOR_RANDOM() % 5000, cur, first, count, start, avail, buf,
                                             FLOOR_RANDOM() % 4 != 0};

                        xz_match_job model = job;
                        positive stops_want = 0, stops_got = 0;

                        model.arr = want;
                        floor_prices_model(address_of model, want, nice, address_of stops_want);
                        while (lzma_match_prices(address_of job))
                        {
                                if (floor_prices_extension(got, cur, (p32)job.len, matches[job.index].dist,
                                                           (p32)job.base, avail, nice, buf))
                                {
                                        stops_got = stops_got * 31 + (p32)job.len + 1000 * job.index;
                                        extended = true;
                                }
                                if (++job.index == count)
                                        break;
                                job.len = job.len + 1;
                        }
                        same = same && stops_got == stops_want && !memory_compare(got, want, sizeof(got));
                }
                //      lzma_relax: a run of costs P[k] + base from one entry on.
                for (positive trial = 0; trial < 3000 && same; trial++)
                {
                        p32 first = FLOOR_RANDOM() % 3500;
                        positive n = 1 + FLOOR_RANDOM() % 70;
                        p32 base = FLOOR_RANDOM() % 5000, pos = FLOOR_RANDOM() % 4000, back = FLOOR_RANDOM() % 4;

                        for (positive i = 0; i < sizeof(prices) / sizeof(prices[0]); i++)
                                prices[i] = FLOOR_RANDOM() % 3000;
                        for (positive i = 0; i < 4 * FLOOR_TABLE; i++)
                                want[i] = FLOOR_RANDOM() % 5 ? FLOOR_RANDOM() % 9000 : XZ_INFINITY_PRICE;
                        memory_copy(got, want, sizeof(got));
                        for (positive k = 0; k < n; k++)
                                if (base + prices[k] < want[first + k])
                                {
                                        want[first + k] = base + prices[k];
                                        want[FLOOR_TABLE + first + k] = pos;
                                        want[2 * FLOOR_TABLE + first + k] = back;
                                        want[3 * FLOOR_TABLE + first + k] = 0;
                                }
                        xz_relax_job relax = {got, prices, first, n, base, pos, back};

                        lzma_relax(address_of relax);
                        same = same && !memory_compare(got, want, sizeof(got));
                }
        }
#if X64
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#endif
#undef FLOOR_RANDOM
        memory_free(region, 42 * FLOOR_PAGE);
        check("lzma match prices agree with the length at a time loops: entries, stops",
              same && extended && far);
}

#ifdef CHECK_compression_floor
b32 main(void)
{
        floor_checksums();
        floor_codebook();
        floor_lzma_span();
        floor_range();
        floor_huffman();
        floor_deflate();
        floor_deflate_marked();
        floor_deflate_codes();
        floor_deflate_tokens();
        floor_deflate_parse();
        floor_deflate_chain();
        floor_lzma_walk();
        floor_lzma_prices();
        floor_huffman_lengths();
        floor_zstd_huffman_cells();
        floor_huffman_codes();
        floor_zstd_fse_cells();
        floor_zstd_huffman_codes();
        floor_zstd_sequences_encode();
        return test_report(null);
}
#endif
#ifdef BENCH_compression_floor
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define COMPRESSION_BENCH_SIZE (256 * 1024)
#define COMPRESSION_BENCH_ROUNDS 64
static p8 compression_input[COMPRESSION_BENCH_SIZE] __attribute__((aligned(64)));
static p8 compression_output[2 * COMPRESSION_BENCH_SIZE] __attribute__((aligned(64)));
static p32 compression_codes[256];
static p16 compression_probs[768];
static volatile positive compression_sink;
static p32 (*volatile compression_crc32_call)(p32,address_any,positive) = hash_crc32;
static p64 (*volatile compression_crc64_call)(p64,address_any,positive) = hash_crc64;
static __typeof__(memory_copy_apart) *volatile compression_copy_call = memory_copy_apart;
static positive (*volatile compression_huffman_call)(address_any,address_any,positive,address_any) = huffman_encode_back;

static __attribute__((noinline)) p32 compression_crc_scalar(p32 crc, p8 address_to p, positive n)
{
        for (positive i = 0; i < n; i++) crc = hash_crc32_tab[(crc ^ p[i]) & 255] ^ (crc >> 8);
        return crc;
}
static p32 (*volatile compression_scalar_call)(p32,p8 address_to,positive) = compression_crc_scalar;

static fn compression_copy_work(void)
{
        for (positive i = 0; i < COMPRESSION_BENCH_ROUNDS; i++)
                compression_sink ^= (positive)compression_copy_call(compression_output,compression_input,COMPRESSION_BENCH_SIZE);
}
static fn compression_crc_scalar_work(void)
{
        for (positive i = 0; i < COMPRESSION_BENCH_ROUNDS; i++)
                compression_sink ^= compression_scalar_call((p32)i,compression_input,COMPRESSION_BENCH_SIZE);
}
static fn compression_crc32_work(void)
{
        for (positive i = 0; i < COMPRESSION_BENCH_ROUNDS; i++)
                compression_sink ^= compression_crc32_call((p32)i,compression_input,COMPRESSION_BENCH_SIZE);
}
static fn compression_crc64_work(void)
{
        for (positive i = 0; i < COMPRESSION_BENCH_ROUNDS; i++)
                compression_sink ^= compression_crc64_call(i,compression_input,COMPRESSION_BENCH_SIZE);
}
static fn compression_huffman_work(void)
{
        for (positive i = 0; i < COMPRESSION_BENCH_ROUNDS; i++)
                compression_sink ^= compression_huffman_call(compression_output,compression_input,COMPRESSION_BENCH_SIZE,compression_codes);
}
static fn compression_range_work(p8 assembly)
{
        for (positive trial = 0; trial < 32; trial++)
        {
                for (positive i = 0; i < 768; i++) compression_probs[i] = 1024;
                xz_range_state state = {0xffffffffu,0,0,1,compression_output,
                                        compression_output + sizeof(compression_output),0};
                for (positive i = 0; i < 4096; i++)
                {
                        if (assembly)
                                lzma_range_encode(address_of state,compression_probs,compression_input[i],8);
                        else
                                floor_tree_reference(address_of state,compression_probs,compression_input[i],8);
                }
                for (positive i = 0; i < 5; i++) lzma_range_shift(address_of state);
                compression_sink ^= state.next - compression_output;
        }
}
static fn compression_range_asm_work(void) { compression_range_work(true); }
static fn compression_range_c_work(void) { compression_range_work(false); }

b32 main(void)
{
        p32 random = 0x7433291u;
        for (positive i = 0; i < sizeof(compression_input); i++)
        {
                XORSHIFT32(random);
                compression_input[i] = (p8)random;
        }
        for (positive i = 0; i < 256; i++) compression_codes[i] = (8 << 16) | i;
        string_format(log,"  The copy row is a traffic proxy, not an entropy-coding lower bound.\n");
        bench_report("copy traffic proxy",compression_copy_work,7,COMPRESSION_BENCH_SIZE*COMPRESSION_BENCH_ROUNDS,"byte");
        bench_report("CRC32 dependent byte reference",compression_crc_scalar_work,7,COMPRESSION_BENCH_SIZE*COMPRESSION_BENCH_ROUNDS,"byte");
        bench_report("CRC32 dispatched kernel",compression_crc32_work,7,COMPRESSION_BENCH_SIZE*COMPRESSION_BENCH_ROUNDS,"byte");
        bench_report("CRC64 dispatched kernel",compression_crc64_work,7,COMPRESSION_BENCH_SIZE*COMPRESSION_BENCH_ROUNDS,"byte");
        bench_report("backward Huffman kernel",compression_huffman_work,7,COMPRESSION_BENCH_SIZE*COMPRESSION_BENCH_ROUNDS,"byte");
        bench_report("scalar range trees",compression_range_c_work,7,32*4096,"byte");
        bench_report("assembly range trees",compression_range_asm_work,7,32*4096,"byte");
        return 0;
}
#endif /* BENCH_compression_floor */
#endif /* CHECK_compression_floor */

#ifdef BENCH_hardware_floor
/*
        Each CPU-bound lib.c leaf against this machine's traffic floor.

        Not vs glibc. The floor is a loop that moves the same bytes and
        computes nothing: one-stream read, two-stream read, copy, or fill.
        Ratio is ours_ns / floor_ns. 1.00 is the traffic bound. Below 1
        the floor was not a floor (printed "unresolved" in BENCH_floor).
        Compute and latency leaves have no traffic bound that means the
        same thing; they still print a floor column so the CSV is one shape.

            Darwin (lifted ARM64 bodies):
                python3 test/differential.py --harness native_extract \
                    src/lib.c $NAMES > /tmp/lifted.h
                cc -O2 -fno-builtin -DUSE_LIFTED -DSKIP_SHA256 -DSKIP_GHASH -DSKIP_FIELD -DSKIP_AES -DSKIP_HEX -DSKIP_LOCK \
                    -DSKIP_ITOA -I/tmp -DBENCH_hardware_floor test/checks.c -o /tmp/hwfloor

            Darwin names (sha256/ghash/field/aes/hex/itoa skipped: Mach-O :lo12: tables
                or not lifted):
                memory_copy_apart memory_copy memory_fill memory_fill_32
                memory_fill_64 memory_reverse memory_frob
                memory_to_lower_ascii memory_to_upper_ascii
                memory_exchange_apart memory_translate string_length
                string_length_max memory_compare memory_common_prefix
                memory_compare_ascii_case memory_first_of memory_last_of
                memory_count memory_sum_bytes memory_hash_33 hash_xxh64
                memory_checksum_bsd16 memory_span_byte memory_utf8_span
                string_compare string_compare_max string_first_of
                string_first_of_or_end string_last_of_or_end string_copy
                string_copy_max string_search string_find memory_search
                memory_search_prepare memory_search_prepared
                memory_search_prepared_core string_to_positive
                memory_copy_end memory_copy_apart_end

            Linux (outlined lib.c):
                printf '%s\n' '#include "src/lib.c"' > /tmp/mw.c
                cc -O2 -ffreestanding -fno-builtin -fno-stack-protector \
                    -c /tmp/mw.c -o /tmp/libmw.o -I.
                objcopy --redefine-sym _start=moonwater_start /tmp/libmw.o
                cc -O2 -fno-builtin -DUSE_OURS -DBENCH_hardware_floor test/checks.c \
                    /tmp/libmw.o -o /tmp/hwfloor

        Prints CSV: arch,name,shape,class,bytes,ours_ns,floor_ns,ratio
        qemu-user is not a hardware floor; do not quote it as one.
*/

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef USE_LIFTED
#define ASM_WIDER(a, b) ""
#define ASM_NARROW(a, b) ""
#define ASM_RET "ret\n"
#define AVX2_WIDTH "32"
#define AVX2_ZEROED ""
#define AVX2_ZEROS_AT(x) ""
#define AVX2_LEAVE ""
#define STRING_COPY_TAIL ""
#include "lifted.h"
#endif

#if defined(__APPLE__)
#define ASM_C_SYM(name) "_" name
#else
#define ASM_C_SYM(name) name
#endif

__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl " ASM_C_SYM("floor_read") "\n"
    ASM_C_SYM("floor_read") ":\n"
#if defined(__x86_64__)
    "xor %eax, %eax\n   test %rsi, %rsi\n   jz 9f\n"
    "1:  cmp $64, %rsi\n   jb 5f\n"
    "movdqu 0(%rdi), %xmm0\n   movdqu 16(%rdi), %xmm1\n"
    "movdqu 32(%rdi), %xmm2\n   movdqu 48(%rdi), %xmm3\n"
    "add $64, %rdi\n   sub $64, %rsi\n   jmp 1b\n"
    "5:  test %rsi, %rsi\n   jz 9f\n"
    "6:  add (%rdi), %rax\n   add $8, %rdi\n   sub $8, %rsi\n"
    "cmp $8, %rsi\n   jae 6b\n"
    "9:  ret\n"
#elif defined(__aarch64__)
    "mov x2, #0\n   cbz x1, 9f\n"
    "1:  cmp x1, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x0]\n   ldp q2, q3, [x0, #32]\n"
    "add x0, x0, #64\n   sub x1, x1, #64\n   b 1b\n"
    "5:  cbz x1, 9f\n"
    "6:  ldr x3, [x0]\n   add x2, x2, x3\n   add x0, x0, #8\n"
    "subs x1, x1, #8\n   b.hi 6b\n"
    "9:  mov x0, x2\n   ret\n"
#else
    "li a2, 0\n   beqz a1, 9f\n"
    "1:  ld a3, 0(a0)\n   add a2, a2, a3\n"
    "addi a0, a0, 8\n   addi a1, a1, -8\n   bnez a1, 1b\n"
    "9:  mv a0, a2\n   ret\n"
#endif

    ".p2align 4\n"
    ".globl " ASM_C_SYM("floor_read_two") "\n"
    ASM_C_SYM("floor_read_two") ":\n"
#if defined(__x86_64__)
    "xor %eax, %eax\n   test %rdx, %rdx\n   jz 9f\n"
    "1:  cmp $64, %rdx\n   jb 5f\n"
    "movdqu 0(%rdi), %xmm0\n   movdqu 16(%rdi), %xmm1\n"
    "movdqu 32(%rdi), %xmm2\n   movdqu 48(%rdi), %xmm3\n"
    "movdqu 0(%rsi), %xmm4\n   movdqu 16(%rsi), %xmm5\n"
    "movdqu 32(%rsi), %xmm6\n   movdqu 48(%rsi), %xmm7\n"
    "add $64, %rdi\n   add $64, %rsi\n   sub $64, %rdx\n   jmp 1b\n"
    "5:  test %rdx, %rdx\n   jz 9f\n"
    "6:  add (%rdi), %rax\n   add (%rsi), %rax\n"
    "add $8, %rdi\n   add $8, %rsi\n   sub $8, %rdx\n"
    "cmp $8, %rdx\n   jae 6b\n"
    "9:  ret\n"
#elif defined(__aarch64__)
    "mov x3, #0\n   cbz x2, 9f\n"
    "1:  cmp x2, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x0]\n   ldp q2, q3, [x0, #32]\n"
    "ldp q4, q5, [x1]\n   ldp q6, q7, [x1, #32]\n"
    "add x0, x0, #64\n   add x1, x1, #64\n"
    "sub x2, x2, #64\n   b 1b\n"
    "5:  cbz x2, 9f\n"
    "6:  ldr x4, [x0], #8\n   ldr x5, [x1], #8\n"
    "add x3, x3, x4\n   add x3, x3, x5\n"
    "subs x2, x2, #8\n   b.hi 6b\n"
    "9:  mov x0, x3\n   ret\n"
#else
    "li a3, 0\n   beqz a2, 9f\n"
    "1:  ld a4, 0(a0)\n   ld a5, 0(a1)\n"
    "add a3, a3, a4\n   add a3, a3, a5\n"
    "addi a0, a0, 8\n   addi a1, a1, 8\n   addi a2, a2, -8\n   bnez a2, 1b\n"
    "9:  mv a0, a3\n   ret\n"
#endif

    ".p2align 4\n"
    ".globl " ASM_C_SYM("floor_copy") "\n"
    ASM_C_SYM("floor_copy") ":\n"
#if defined(__x86_64__)
    "xor %eax, %eax\n   test %rdx, %rdx\n   jz 9f\n"
    "1:  cmp $64, %rdx\n   jb 5f\n"
    "movdqu 0(%rsi), %xmm0\n   movdqu 16(%rsi), %xmm1\n"
    "movdqu 32(%rsi), %xmm2\n   movdqu 48(%rsi), %xmm3\n"
    "movdqu %xmm0, 0(%rdi)\n   movdqu %xmm1, 16(%rdi)\n"
    "movdqu %xmm2, 32(%rdi)\n   movdqu %xmm3, 48(%rdi)\n"
    "add $64, %rdi\n   add $64, %rsi\n   sub $64, %rdx\n   jmp 1b\n"
    "5:  test %rdx, %rdx\n   jz 9f\n"
    "6:  mov (%rsi), %rax\n   mov %rax, (%rdi)\n"
    "add $8, %rdi\n   add $8, %rsi\n   sub $8, %rdx\n"
    "cmp $8, %rdx\n   jae 6b\n"
    "9:  ret\n"
#elif defined(__aarch64__)
    "cbz x2, 9f\n"
    "1:  cmp x2, #64\n   b.lo 5f\n"
    "ldp q0, q1, [x1]\n   ldp q2, q3, [x1, #32]\n"
    "stp q0, q1, [x0]\n   stp q2, q3, [x0, #32]\n"
    "add x0, x0, #64\n   add x1, x1, #64\n"
    "sub x2, x2, #64\n   b 1b\n"
    "5:  cbz x2, 9f\n"
    "6:  ldr x3, [x1], #8\n   str x3, [x0], #8\n"
    "subs x2, x2, #8\n   b.hi 6b\n"
    "9:  mov x0, #0\n   ret\n"
#else
    "beqz a2, 9f\n"
    "1:  ld a3, 0(a1)\n   sd a3, 0(a0)\n"
    "addi a0, a0, 8\n   addi a1, a1, 8\n   addi a2, a2, -8\n   bnez a2, 1b\n"
    "9:  li a0, 0\n   ret\n"
#endif

    ".p2align 4\n"
    ".globl " ASM_C_SYM("floor_fill") "\n"
    ASM_C_SYM("floor_fill") ":\n"
#if defined(__x86_64__)
    "xor %eax, %eax\n   test %rsi, %rsi\n   jz 9f\n"
    "pxor %xmm0, %xmm0\n"
    "1:  cmp $64, %rsi\n   jb 5f\n"
    "movdqu %xmm0, 0(%rdi)\n   movdqu %xmm0, 16(%rdi)\n"
    "movdqu %xmm0, 32(%rdi)\n   movdqu %xmm0, 48(%rdi)\n"
    "add $64, %rdi\n   sub $64, %rsi\n   jmp 1b\n"
    "5:  test %rsi, %rsi\n   jz 9f\n"
    "6:  mov %rax, (%rdi)\n   add $8, %rdi\n   sub $8, %rsi\n"
    "cmp $8, %rsi\n   jae 6b\n"
    "9:  ret\n"
#elif defined(__aarch64__)
    "movi v0.16b, #0\n   cbz x1, 9f\n"
    "1:  cmp x1, #64\n   b.lo 5f\n"
    "stp q0, q0, [x0]\n   stp q0, q0, [x0, #32]\n"
    "add x0, x0, #64\n   sub x1, x1, #64\n   b 1b\n"
    "5:  cbz x1, 9f\n"
    "6:  str xzr, [x0], #8\n   subs x1, x1, #8\n   b.hi 6b\n"
    "9:  mov x0, #0\n   ret\n"
#else
    "beqz a1, 9f\n"
    "1:  sd zero, 0(a0)\n   addi a0, a0, 8\n"
    "addi a1, a1, -8\n   bnez a1, 1b\n"
    "9:  li a0, 0\n   ret\n"
#endif
);

unsigned long floor_read(void *, unsigned long);
unsigned long floor_read_two(void *, void *, unsigned long);
unsigned long floor_copy(void *, void *, unsigned long);
unsigned long floor_fill(void *, unsigned long);

#if defined(USE_OURS) || defined(USE_LIFTED)
void *memory_copy_apart(void *, const void *, unsigned long);
void *memory_copy(void *, const void *, unsigned long);
void *memory_fill(void *, int, unsigned long);
void *memory_fill_32(void *, unsigned int, unsigned long);
void *memory_fill_64(void *, unsigned long, unsigned long);
void *memory_reverse(void *, unsigned long);
void *memory_frob(void *, unsigned long);
void *memory_to_lower_ascii(void *, unsigned long);
void *memory_to_upper_ascii(void *, unsigned long);
void *memory_exchange_apart(void *, void *, unsigned long);
void *memory_translate(void *, unsigned long, const void *);
unsigned long string_length(const char *);
unsigned long string_length_max(const char *, unsigned long);
int memory_compare(const void *, const void *, unsigned long);
unsigned long memory_common_prefix(const void *, const void *, unsigned long);
int memory_compare_ascii_case(const void *, const void *, unsigned long);
void *memory_first_of(const void *, int, unsigned long);
void *memory_last_of(const void *, int, unsigned long);
unsigned long memory_count(const void *, unsigned long, int);
unsigned long memory_sum_bytes(const void *, unsigned long);
unsigned long memory_hash_33(const void *, unsigned long);
unsigned long long hash_xxh64(const void *, unsigned long, unsigned long long);
unsigned memory_checksum_bsd16(const void *, unsigned long, unsigned);
#ifndef SKIP_SHA256
void sha256_compress(unsigned int *, unsigned char *);
void md5_blocks(unsigned int *, const unsigned char *, unsigned long);
void sha1_blocks(unsigned int *, const unsigned char *, unsigned long);
void sha256_blocks(unsigned int *, const unsigned char *, unsigned long);
void sha512_blocks(unsigned long *, const unsigned char *, unsigned long);
void blake2b_blocks(unsigned long *, const unsigned char *, unsigned long,
                    unsigned long);
#endif
#ifndef SKIP_AES
void aes128_ctr_blocks(const unsigned char *, unsigned char *,
                       const unsigned char *, unsigned char *, unsigned long);
#endif
#ifndef SKIP_FIELD
void p256_multiply(unsigned long *, const unsigned long *, const unsigned long *);
void p256_square(unsigned long *, const unsigned long *);
void p256_add(unsigned long *, const unsigned long *, const unsigned long *);
void p256_subtract(unsigned long *, const unsigned long *, const unsigned long *);
void p384_multiply(unsigned long *, const unsigned long *, const unsigned long *);
void p384_square(unsigned long *, const unsigned long *);
void p384_add(unsigned long *, const unsigned long *, const unsigned long *);
void p384_subtract(unsigned long *, const unsigned long *, const unsigned long *);
#endif
#ifndef SKIP_GHASH
void ghash_key(unsigned char *, const unsigned char *);
void ghash_blocks(unsigned char *, const unsigned char *,
                  const unsigned char *, unsigned long);
#endif
#ifndef SKIP_HEX
unsigned long memory_into_hex(void *, const void *, unsigned long);
#endif
unsigned long memory_span_byte(const void *, int, unsigned long);
typedef struct { unsigned long x, y; } floor_positive2;
floor_positive2 memory_utf8_span(const void *, unsigned long, unsigned long);
int string_compare(const char *, const char *);
int string_compare_max(const char *, const char *, unsigned long);
char *string_first_of(const char *, int);
char *string_last_of_or_end(const char *, int);
char *string_copy(char *, const char *);
char *string_copy_max(char *, const char *, unsigned long);
char *string_search(const char *, const char *);
char *string_find(const char *, const char *);
void *memory_search(const void *, unsigned long, const void *, unsigned long);
#ifndef SKIP_ITOA
unsigned long positive_into(char *, unsigned long);
#endif
unsigned long string_to_positive(const char *);
void *memory_copy_end(void *, const void *, unsigned long);
void *memory_copy_apart_end(void *, const void *, unsigned long);
#endif

/* The lock is Linux runtime assembly (in lib.c) and only the
   outlined library carries it. threads_live is forced to one around the
   company row to time the atomic path in this single-threaded harness. */
#if defined(USE_OURS) && !defined(SKIP_LOCK)
typedef struct { int word; } floor_lock;
void lock_take(floor_lock *);
void lock_release(floor_lock *);
extern unsigned long threads_live;
#endif

enum { ROOM = 1 << 20 };
static unsigned char src[ROOM] __attribute__((aligned(64)));
static unsigned char dst[ROOM] __attribute__((aligned(64)));
static unsigned char src2[ROOM] __attribute__((aligned(64)));
static unsigned char table[256] __attribute__((aligned(64)));
static unsigned int sha_state[8];
static unsigned char sha_block[64];
static volatile unsigned long sink;

static uint64_t now_ns(void)
{
#if defined(__APPLE__)
        return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC_RAW, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

static uint64_t best_ns(void (*work)(unsigned long, unsigned long),
                        unsigned long size, unsigned long rounds)
{
        uint64_t best = (uint64_t)-1;
        int trial;
        unsigned long live = rounds;

        /* Keep the iteration count in a GPR across the body: some ARM64
           leaves clobber v8-v15 (AAPCS callee-saved SIMD). Clang will
           otherwise hoist (double)rounds into d8 and the ratio becomes inf. */
        __asm__ volatile("" : "+r"(live) : : "memory");
        work(size, live / 8 + 1);
        for (trial = 0; trial < 5; trial++) {
                uint64_t start = now_ns();
                work(size, live);
                uint64_t took = now_ns() - start;
                if (took < best)
                        best = took;
        }
        return best;
}

static unsigned long rounds_for(unsigned long guess_ns)
{
        unsigned long rounds = 80000000ull / (guess_ns ? guess_ns : 1);
        if (rounds < 80)
                rounds = 80;
        if (rounds > 2000000)
                rounds = 2000000;
        return rounds;
}

static const char *arch_name(void)
{
#if defined(__x86_64__)
        return "x86_64";
#elif defined(__aarch64__)
        return "arm64";
#elif defined(__riscv) && __riscv_xlen == 64
        return "riscv64";
#else
        return "unknown";
#endif
}

static void row(const char *name, const char *shape, const char *klass,
                unsigned long size, void (*ours)(unsigned long, unsigned long),
                void (*floor)(unsigned long, unsigned long),
                unsigned long guess)
{
        unsigned long rounds = rounds_for(guess);
        uint64_t ours_ticks = best_ns(ours, size, rounds);
        uint64_t floor_ticks = best_ns(floor, size, rounds);
        double ours_ns, floor_ns, ratio;

        __asm__ volatile("" : "+r"(rounds) : : "memory");
        ours_ns = (double)ours_ticks / (double)(rounds ? rounds : 1);
        floor_ns = (double)floor_ticks / (double)(rounds ? rounds : 1);
        ratio = floor_ns > 0 ? ours_ns / floor_ns : 0;

        printf("%s,%s,%s,%s,%lu,%.3f,%.3f,%.3f\n", arch_name(), name, shape,
               klass, size, ours_ns, floor_ns, ratio);
}

HARDWARE_WORK(floor_one_w, , floor_read(src, size))
HARDWARE_WORK(floor_two_w, , floor_read_two(src, src2, size))
HARDWARE_WORK(floor_copy_w, , floor_copy(dst, src, size))
HARDWARE_WORK(floor_fill_w, , floor_fill(dst, size))
HARDWARE_WORK(memcpy_w, , (unsigned long)memory_copy_apart(dst, src, size))
HARDWARE_WORK(memmove_w, , (unsigned long)memory_copy(dst, src, size))
HARDWARE_WORK(memset_w, , (unsigned long)memory_fill(dst, 0xa5, size))
HARDWARE_WORK(fill32_w, unsigned long words = size / 4;,
              (unsigned long)memory_fill_32(dst, 0xa5a5a5a5u, words))
HARDWARE_WORK(fill64_w, unsigned long words = size / 8;,
              (unsigned long)memory_fill_64(dst, 0xa5a5a5a5a5a5a5a5ull, words))
HARDWARE_WORK(reverse_w, , (unsigned long)memory_reverse(dst, size))
HARDWARE_WORK(frob_w, , (unsigned long)memory_frob(dst, size))
HARDWARE_WORK(lower_w, , (unsigned long)memory_to_lower_ascii(dst, size))
HARDWARE_WORK(upper_w, , (unsigned long)memory_to_upper_ascii(dst, size))
HARDWARE_WORK(exchange_w, , (unsigned long)memory_exchange_apart(dst, src, size))
HARDWARE_WORK(translate_w, , (unsigned long)memory_translate(dst, size, table))

#ifndef SKIP_HEX
HARDWARE_WORK(hex_w, unsigned long n = size > ROOM / 2 ? ROOM / 2 : size;,
              memory_into_hex(dst, src, n))
#endif

HARDWARE_WORK(strlen_w, (void)size;, string_length((char *)src))
HARDWARE_WORK(strnlen_w, , string_length_max((char *)src, size))
HARDWARE_WORK(memcmp_w, , (unsigned)memory_compare(src, src2, size))
HARDWARE_WORK(prefix_w, , memory_common_prefix(src, src2, size))
HARDWARE_WORK(memcmp_case_w, , (unsigned)memory_compare_ascii_case(src, src2, size))
HARDWARE_WORK(memchr_w, , (unsigned long)memory_first_of(src, 'Z', size))
HARDWARE_WORK(memrchr_w, , (unsigned long)memory_last_of(src, 'Z', size))
HARDWARE_WORK(count_w, , memory_count(src, size, 'a'))
HARDWARE_WORK(sum_w, , memory_sum_bytes(src, size))
HARDWARE_WORK(hash33_w, , memory_hash_33(src, size))
HARDWARE_WORK(xxh_w, , (unsigned long)hash_xxh64(src, size, 0))
HARDWARE_WORK(bsd16_w, , memory_checksum_bsd16(src, size, 0))

#ifndef SKIP_SHA256
#if defined(USE_OURS) && !defined(SKIP_LOCK)
static floor_lock lock_word;

static void lock_pair_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        (void)size;
        for (i = 0; i < rounds; i++) {
                lock_take(&lock_word);
                sink += (unsigned long)lock_word.word;
                lock_release(&lock_word);
        }
}

/* The traffic of an uncontended pair: the word goes to 1 and back to 0. */
static void lock_store_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        (void)size;
        for (i = 0; i < rounds; i++) {
                lock_word.word = 1;
                sink += (unsigned long)lock_word.word;
                lock_word.word = 0;
        }
}

/* What standard.c's C lock was: an inline compare-and-swap and exchange. */
static void lock_atomic_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        (void)size;
        for (i = 0; i < rounds; i++) {
                __sync_bool_compare_and_swap(&lock_word.word, 0, 1);
                sink += (unsigned long)lock_word.word;
                __atomic_exchange_n(&lock_word.word, 0, __ATOMIC_SEQ_CST);
        }
}
#endif

static void sha_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        (void)size;
        for (i = 0; i < rounds; i++) {
                sha256_compress(sha_state, sha_block);
                sink += sha_state[0];
        }
}

/*
        The hash block cores over 64 KiB against the textbook rounds compiled
        from C here: no renamed registers, no carried terms, no extension
        instructions -- what every body has to beat, not a traffic bound.
        The cores ask for their extension bodies themselves on the first
        call, so these rows time what the machine has: SHA-NI for sha1 and
        sha256 on a processor with it, the floor for the rest.
*/
#define HASH_BYTES 65536
static unsigned char hash_data[HASH_BYTES] __attribute__((aligned(64)));
static unsigned int hash_narrow[8];
static unsigned long hash_wide[11];

//      The rounds the cores are timed against are CHECK_verify's own, in the
//      names it writes them in.
typedef unsigned char p8;
typedef unsigned int p32;
typedef unsigned long p64, positive;
#define address_to *
#define fn void
#define SHARED_hash_reference
#include "../checks.c"
#undef SHARED_hash_reference

#define HASH_WORKERS(core, call_ours, call_c, sum)                            \
        static void core##_w(unsigned long size, unsigned long rounds)        \
        {                                                                     \
                unsigned long i;                                              \
                for (i = 0; i < rounds; i++) {                                \
                        call_ours;                                            \
                        sink += sum;                                          \
                }                                                             \
                (void)size;                                                   \
        }                                                                     \
        static void core##_c_w(unsigned long size, unsigned long rounds)      \
        {                                                                     \
                unsigned long i;                                              \
                for (i = 0; i < rounds; i++) {                                \
                        call_c;                                               \
                        sink += sum;                                          \
                }                                                             \
                (void)size;                                                   \
        }

HASH_WORKERS(md5, md5_blocks(hash_narrow, hash_data, HASH_BYTES / 64),
             hash_reference_md5(hash_narrow, hash_data, HASH_BYTES / 64), hash_narrow[0])
HASH_WORKERS(sha1, sha1_blocks(hash_narrow, hash_data, HASH_BYTES / 64),
             hash_reference_sha1(hash_narrow, hash_data, HASH_BYTES / 64), hash_narrow[0])
HASH_WORKERS(sha256, sha256_blocks(hash_narrow, hash_data, HASH_BYTES / 64),
             hash_reference_sha256(hash_narrow, hash_data, HASH_BYTES / 64), hash_narrow[0])
HASH_WORKERS(sha512, sha512_blocks(hash_wide, hash_data, HASH_BYTES / 128),
             hash_reference_sha512(hash_wide, hash_data, HASH_BYTES / 128), hash_wide[0])
HASH_WORKERS(blake2b, blake2b_blocks(hash_wide, hash_data, HASH_BYTES / 128, 128),
             hash_reference_blake2b(hash_wide, hash_data, HASH_BYTES / 128, 128), hash_wide[0])
#endif

#ifndef SKIP_FIELD
/*
        The P-256/P-384 field routines against the Montgomery arithmetic
        crypto.c keeps for the group orders and RSA, compiled from C: the
        schoolbook product, word-by-word reduction and a masked final
        subtraction. Operands are fixed values below p.
*/
typedef unsigned __int128 field_wide;

static const unsigned long field_p256[4] = {
    0xffffffffffffffffUL, 0x00000000ffffffffUL, 0, 0xffffffff00000001UL};
static const unsigned long field_p384[6] = {
    0x00000000ffffffffUL, 0xffffffff00000000UL, 0xfffffffffffffffeUL,
    0xffffffffffffffffUL, 0xffffffffffffffffUL, 0xffffffffffffffffUL};
static unsigned long field_a[6] = {
    0x243f6a8885a308d3UL, 0x13198a2e03707344UL, 0xa4093822299f31d0UL,
    0x082efa98ec4e6c89UL, 0x452821e638d01377UL, 0x0be5466cf34e90c6UL};
static unsigned long field_b[6] = {
    0xc0ac29b7c97c50ddUL, 0x3f84d5b5b5470917UL, 0x9216d5d98979fb1bUL,
    0x0801f2e2858efc16UL, 0x636920d871574e69UL, 0x0a458fea3f4933d7UL};
static unsigned long field_r[6];

static unsigned long field_c_sub(unsigned long *d, const unsigned long *a,
                                 const unsigned long *b, int n)
{
        field_wide borrow = 0;
        for (int i = 0; i < n; i++)
        {
                field_wide v = (field_wide)a[i] - b[i] - borrow;
                d[i] = (unsigned long)v;
                borrow = (v >> 64) & 1;
        }
        return (unsigned long)borrow;
}

static void field_c_pick(unsigned long *d, const unsigned long *a,
                         const unsigned long *b, int n, unsigned long take_b)
{
        unsigned long mask = 0 - take_b;
        for (int i = 0; i < n; i++)
                d[i] = (a[i] & ~mask) | (b[i] & mask);
}

__attribute__((noinline))
static void field_c_multiply(unsigned long *d, const unsigned long *a,
                             const unsigned long *b, const unsigned long *m,
                             unsigned long inverse, int n)
{
        unsigned long t[12] = {0}, reduced[6], top = 0;
        for (int i = 0; i < n; i++)
        {
                field_wide carry = 0;
                for (int j = 0; j < n; j++)
                {
                        carry += (field_wide)t[i + j] + (field_wide)a[i] * b[j];
                        t[i + j] = (unsigned long)carry;
                        carry >>= 64;
                }
                t[i + n] = (unsigned long)carry;
        }
        for (int i = 0; i < n; i++)
        {
                unsigned long q = t[i] * inverse;
                field_wide carry = 0;
                for (int j = 0; j < n; j++)
                {
                        carry += (field_wide)t[i + j] + (field_wide)q * m[j];
                        t[i + j] = (unsigned long)carry;
                        carry >>= 64;
                }
                carry += (field_wide)t[i + n] + top;
                t[i + n] = (unsigned long)carry;
                top = (unsigned long)(carry >> 64);
        }
        unsigned long borrow = field_c_sub(reduced, t + n, m, n);
        field_c_pick(d, t + n, reduced, n, top | (borrow ^ 1));
}

__attribute__((noinline))
static void field_c_add(unsigned long *d, const unsigned long *a,
                        const unsigned long *b, const unsigned long *m, int n)
{
        unsigned long sum[6], reduced[6];
        field_wide carry = 0;
        for (int i = 0; i < n; i++)
        {
                carry += (field_wide)a[i] + b[i];
                sum[i] = (unsigned long)carry;
                carry >>= 64;
        }
        unsigned long borrow = field_c_sub(reduced, sum, m, n);
        field_c_pick(d, sum, reduced, n, (unsigned long)carry | (borrow ^ 1));
}

__attribute__((noinline))
static void field_c_subtract(unsigned long *d, const unsigned long *a,
                             const unsigned long *b, const unsigned long *m,
                             int n)
{
        unsigned long difference[6], restored[6];
        unsigned long borrow = field_c_sub(difference, a, b, n);
        field_wide carry = 0;
        for (int i = 0; i < n; i++)
        {
                carry += (field_wide)difference[i] + m[i];
                restored[i] = (unsigned long)carry;
                carry >>= 64;
        }
        field_c_pick(d, difference, restored, n, borrow);
}

#define FIELD_ROW(name, call)                                                  \
        static void name(unsigned long size, unsigned long rounds)             \
        {                                                                      \
                (void)size;                                                    \
                for (unsigned long i = 0; i < rounds; i++)                     \
                {                                                              \
                        call;                                                  \
                        sink += field_r[0];                                    \
                }                                                              \
        }

FIELD_ROW(p256_mul_w, p256_multiply(field_r, field_a, field_b))
FIELD_ROW(p256_mul_c_w, field_c_multiply(field_r, field_a, field_b, field_p256, 1, 4))
FIELD_ROW(p256_sqr_w, p256_square(field_r, field_a))
FIELD_ROW(p256_sqr_c_w, field_c_multiply(field_r, field_a, field_a, field_p256, 1, 4))
FIELD_ROW(p256_add_w, p256_add(field_r, field_a, field_b))
FIELD_ROW(p256_add_c_w, field_c_add(field_r, field_a, field_b, field_p256, 4))
FIELD_ROW(p256_sub_w, p256_subtract(field_r, field_a, field_b))
FIELD_ROW(p256_sub_c_w, field_c_subtract(field_r, field_a, field_b, field_p256, 4))
FIELD_ROW(p384_mul_w, p384_multiply(field_r, field_a, field_b))
FIELD_ROW(p384_mul_c_w, field_c_multiply(field_r, field_a, field_b, field_p384, 0x100000001UL, 6))
FIELD_ROW(p384_sqr_w, p384_square(field_r, field_a))
FIELD_ROW(p384_sqr_c_w, field_c_multiply(field_r, field_a, field_a, field_p384, 0x100000001UL, 6))
FIELD_ROW(p384_add_w, p384_add(field_r, field_a, field_b))
FIELD_ROW(p384_add_c_w, field_c_add(field_r, field_a, field_b, field_p384, 6))
FIELD_ROW(p384_sub_w, p384_subtract(field_r, field_a, field_b))
FIELD_ROW(p384_sub_c_w, field_c_subtract(field_r, field_a, field_b, field_p384, 6))
#endif

#ifndef SKIP_GHASH
/*
        GHASH against the integer carry-less multiply written in C: the
        floor column is the compiler's arrangement of the baseline algorithm,
        the thing every body has to beat, not a traffic bound. ghash_blocks
        takes its widest body here (VPCLMULQDQ zmm on a 9950X, PCLMULQDQ,
        PMULL or Zbc where present). The ISA references for a 16 KiB record:
        OpenSSL GMAC 37.0 GB/s on the 9950X and 7.6 GB/s on an M2 Pro.
*/
static const unsigned char ghash_key_bytes[16] = {
    0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b,
    0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e};
static unsigned char ghash_state[16];
static unsigned char ghash_table[1552] __attribute__((aligned(64)));

static uint64_t ghash_c_load(const unsigned char *at)
{
        uint64_t value = 0;
        for (int i = 0; i < 8; i++)
                value = value << 8 | at[i];
        return value;
}

static void ghash_c_store(unsigned char *at, uint64_t value)
{
        for (int i = 7; i >= 0; i--)
        {
                at[i] = (unsigned char)value;
                value >>= 8;
        }
}

static uint64_t ghash_c_reverse(uint64_t x)
{
        x = ((x >> 1) & 0x5555555555555555ull) | ((x & 0x5555555555555555ull) << 1);
        x = ((x >> 2) & 0x3333333333333333ull) | ((x & 0x3333333333333333ull) << 2);
        x = ((x >> 4) & 0x0f0f0f0f0f0f0f0full) | ((x & 0x0f0f0f0f0f0f0f0full) << 4);
        return __builtin_bswap64(x);
}

static uint64_t ghash_c_low(uint64_t x, uint64_t y)
{
        uint64_t x0 = x & 0x1111111111111111ull, x1 = x & 0x2222222222222222ull;
        uint64_t x2 = x & 0x4444444444444444ull, x3 = x & 0x8888888888888888ull;
        uint64_t y0 = y & 0x1111111111111111ull, y1 = y & 0x2222222222222222ull;
        uint64_t y2 = y & 0x4444444444444444ull, y3 = y & 0x8888888888888888ull;
        uint64_t z0 = (x0 * y0) ^ (x1 * y3) ^ (x2 * y2) ^ (x3 * y1);
        uint64_t z1 = (x0 * y1) ^ (x1 * y0) ^ (x2 * y3) ^ (x3 * y2);
        uint64_t z2 = (x0 * y2) ^ (x1 * y1) ^ (x2 * y0) ^ (x3 * y3);
        uint64_t z3 = (x0 * y3) ^ (x1 * y2) ^ (x2 * y1) ^ (x3 * y0);

        return (z0 & 0x1111111111111111ull) | (z1 & 0x2222222222222222ull) |
               (z2 & 0x4444444444444444ull) | (z3 & 0x8888888888888888ull);
}

static void ghash_c_blocks(unsigned char *state, const unsigned char *key,
                           const unsigned char *data, unsigned long blocks)
{
        uint64_t h1 = ghash_c_load(key), h0 = ghash_c_load(key + 8);
        uint64_t h1r = ghash_c_reverse(h1), h0r = ghash_c_reverse(h0);
        uint64_t hm = h0 ^ h1, hmr = h0r ^ h1r;
        uint64_t s1 = ghash_c_load(state), s0 = ghash_c_load(state + 8);

        while (blocks--)
        {
                uint64_t x1 = s1 ^ ghash_c_load(data);
                uint64_t x0 = s0 ^ ghash_c_load(data + 8);
                uint64_t lo0 = ghash_c_low(x0, h0), lo1 = ghash_c_low(x1, h1);
                uint64_t lom = ghash_c_low(x0 ^ x1, hm);
                uint64_t hi0 = ghash_c_reverse(ghash_c_low(ghash_c_reverse(x0), h0r));
                uint64_t hi1 = ghash_c_reverse(ghash_c_low(ghash_c_reverse(x1), h1r));
                uint64_t him = ghash_c_reverse(
                    ghash_c_low(ghash_c_reverse(x0 ^ x1), hmr));
                uint64_t w0 = lo0 << 1;
                uint64_t w1 = hi0 ^ ((lo0 ^ lo1 ^ lom) << 1);
                uint64_t w2 = (hi0 ^ hi1 ^ him) ^ (lo1 << 1);
                uint64_t w3 = hi1;

                w2 ^= w0 ^ (w0 >> 1) ^ (w0 >> 2) ^ (w0 >> 7);
                w1 ^= (w0 << 63) ^ (w0 << 62) ^ (w0 << 57);
                w3 ^= w1 ^ (w1 >> 1) ^ (w1 >> 2) ^ (w1 >> 7);
                w2 ^= (w1 << 63) ^ (w1 << 62) ^ (w1 << 57);
                s1 = w3;
                s0 = w2;
                data += 16;
        }

        ghash_c_store(state, s1);
        ghash_c_store(state + 8, s0);
}

static void ghash_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        if (!ghash_table[1536] && !ghash_table[1537])
                ghash_key(ghash_table, ghash_key_bytes);
        for (i = 0; i < rounds; i++) {
                ghash_blocks(ghash_state, ghash_table, src, size / 16);
                sink += ghash_state[0];
        }
}

static void ghash_c_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        for (i = 0; i < rounds; i++) {
                ghash_c_blocks(ghash_state, ghash_key_bytes, src, size / 16);
                sink += ghash_state[0];
        }
}
#endif

#ifndef SKIP_AES
/*
        AES-128 counter mode against FIPS-197 written with an S-box table in
        C: the floor column is a table AES, which indexes on the key and the
        data and is faster than any constant-time software AES, so a ratio
        under one is the hardware body (VAES, AES-NI, the AES extension or
        Zvkned) and over one is the bitsliced floor paying for constant time.
        ISA references for a 16 KiB record on the 9950X: OpenSSL AES-128-CTR
        15.5 GB/s (AES-NI) and AES-128-GCM 27.2 GB/s.
*/
static unsigned char aes_round[176];
static unsigned char aes_counter[16];
static unsigned char aes_sbox[256];

static unsigned char aes_c_xtime(unsigned char v)
{
        return (unsigned char)((v << 1) ^ (v & 0x80 ? 0x1b : 0));
}

static void aes_c_setup(void)
{
        static const unsigned char rcon[10] = {1, 2, 4, 8, 16, 32, 64, 128, 0x1b, 0x36};
        unsigned char p = 1, q = 1;
        if (aes_sbox[0])
                return;
        do {
                p = p ^ (unsigned char)(p << 1) ^ (p & 0x80 ? 0x1b : 0);
                q ^= q << 1; q ^= q << 2; q ^= q << 4;
                if (q & 0x80) q ^= 0x09;
                aes_sbox[p] = q ^ (unsigned char)(q << 1 | q >> 7) ^ (unsigned char)(q << 2 | q >> 6) ^
                              (unsigned char)(q << 3 | q >> 5) ^ (unsigned char)(q << 4 | q >> 4) ^ 0x63;
        } while (p != 1);
        aes_sbox[0] = 0x63;
        for (int i = 0; i < 16; i++)
                aes_round[i] = (unsigned char)(i * 29 + 7);
        for (int i = 16; i < 176; i += 4) {
                unsigned char t[4] = {aes_round[i - 4], aes_round[i - 3], aes_round[i - 2], aes_round[i - 1]};
                if (i % 16 == 0) {
                        unsigned char k = t[0];
                        t[0] = aes_sbox[t[1]] ^ rcon[i / 16 - 1];
                        t[1] = aes_sbox[t[2]]; t[2] = aes_sbox[t[3]]; t[3] = aes_sbox[k];
                }
                for (int j = 0; j < 4; j++)
                        aes_round[i + j] = aes_round[i - 16 + j] ^ t[j];
        }
}

static void aes_c_blocks(const unsigned char *round, unsigned char *counter,
                         const unsigned char *in, unsigned char *out, unsigned long blocks)
{
        static const unsigned char shift[16] = {0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12, 1, 6, 11};
        for (; blocks; blocks--, in += 16, out += 16) {
                unsigned char s[16], n[16];
                for (int i = 0; i < 16; i++) s[i] = counter[i] ^ round[i];
                for (int r = 1; r <= 10; r++) {
                        for (int i = 0; i < 16; i++) n[i] = aes_sbox[s[shift[i]]];
                        if (r < 10)
                                for (int i = 0; i < 16; i += 4) {
                                        unsigned char a = n[i], b = n[i + 1], c = n[i + 2], d = n[i + 3];
                                        n[i] = aes_c_xtime(a) ^ aes_c_xtime(b) ^ b ^ c ^ d;
                                        n[i + 1] = a ^ aes_c_xtime(b) ^ aes_c_xtime(c) ^ c ^ d;
                                        n[i + 2] = a ^ b ^ aes_c_xtime(c) ^ aes_c_xtime(d) ^ d;
                                        n[i + 3] = aes_c_xtime(a) ^ a ^ b ^ c ^ aes_c_xtime(d);
                                }
                        for (int i = 0; i < 16; i++) s[i] = n[i] ^ round[16 * r + i];
                }
                for (int i = 0; i < 16; i++) out[i] = in[i] ^ s[i];
                for (int i = 15; i >= 12; i--)
                        if (++counter[i]) break;
        }
}

static void aes_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        aes_c_setup();
        for (i = 0; i < rounds; i++) {
                aes128_ctr_blocks(aes_round, aes_counter, src, dst, size / 16);
                sink += dst[0];
        }
}

static void aes_c_w(unsigned long size, unsigned long rounds)
{
        unsigned long i;
        aes_c_setup();
        for (i = 0; i < rounds; i++) {
                aes_c_blocks(aes_round, aes_counter, src, dst, size / 16);
                sink += dst[0];
        }
}
#endif

HARDWARE_WORK(span_byte_w, , memory_span_byte(src, 'a', size))
HARDWARE_WORK(utf8_w, , memory_utf8_span(src, size, size).x)
HARDWARE_WORK(strcmp_w, (void)size;, (unsigned)string_compare((char *)src, (char *)src2))
HARDWARE_WORK(strncmp_w, , (unsigned)string_compare_max((char *)src, (char *)src2, size))
HARDWARE_WORK(strchr_w, (void)size;, (unsigned long)string_first_of((char *)src, 'Z'))
HARDWARE_WORK(strrchr_w, (void)size;,
              (unsigned long)string_last_of_or_end((char *)src, 'Z'))
HARDWARE_WORK(strcpy_w, (void)size;, (unsigned long)string_copy((char *)dst, (char *)src))
HARDWARE_WORK(strncpy_w, , (unsigned long)string_copy_max((char *)dst, (char *)src, size))
HARDWARE_WORK(strstr_w, (void)size;,
              (unsigned long)string_search((char *)src, (char *)src2))
HARDWARE_WORK(find_w, (void)size;, (unsigned long)string_find((char *)src, (char *)src2))
HARDWARE_WORK(memmem_w, , (unsigned long)memory_search(src, size, src2, 3))
HARDWARE_WORK(copy_end_w, , (unsigned long)memory_copy_end(dst, src, size))
HARDWARE_WORK(copy_apart_end_w, , (unsigned long)memory_copy_apart_end(dst, src, size))

#ifndef SKIP_ITOA
HARDWARE_WORK(into_w, (void)size;, positive_into((char *)dst, 1234567890123ull))
#endif

HARDWARE_WORK(parse_w, (void)size;, string_to_positive((char *)src))

static void fill_az(unsigned long n)
{
        unsigned long i;
        for (i = 0; i < n; i++)
                src[i] = (unsigned char)('a' + (i % 26));
        src[n] = 0;
        memcpy(src2, src, n + 1);
        memcpy(dst, src, n + 1);
}

int main(void)
{
        static const unsigned long sizes[] = {8, 16, 32, 64, 256, 4096, 65536};
        unsigned long s, n, guess;
        unsigned i;

        for (i = 0; i < 256; i++)
                table[i] = (unsigned char)i;
        for (i = 0; i < 8; i++)
                sha_state[i] = 0x6a09e667u + i * 0x11111111u;
        memset(sha_block, 0x5a, sizeof sha_block);

        printf("arch,name,shape,class,bytes,ours_ns,floor_ns,ratio\n");

        for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
                n = sizes[s];
                guess = n < 64 ? 8 : n < 4096 ? 20 : n / 8;

                memset(src, 0x5a, n);
                memcpy(src2, src, n);
                memcpy(dst, src, n);

                row("memory_copy_apart", "copy", "traffic_copy", n, memcpy_w,
                    floor_copy_w, guess);
                row("memory_copy", "apart", "traffic_copy", n, memmove_w,
                    floor_copy_w, guess);
                row("memory_fill", "fill", "traffic_fill", n, memset_w,
                    floor_fill_w, guess);
                if (n >= 4)
                        row("memory_fill_32", "fill", "traffic_fill", n, fill32_w,
                            floor_fill_w, guess);
                if (n >= 8)
                        row("memory_fill_64", "fill", "traffic_fill", n, fill64_w,
                            floor_fill_w, guess);
                memcpy(dst, src, n);
                row("memory_reverse", "inplace", "traffic_copy", n, reverse_w,
                    floor_copy_w, guess);
                memcpy(dst, src, n);
                row("memory_frob", "inplace", "traffic_copy", n, frob_w,
                    floor_copy_w, guess);
                memcpy(dst, src, n);
                row("memory_to_lower_ascii", "inplace", "traffic_copy", n,
                    lower_w, floor_copy_w, guess);
                row("memory_to_upper_ascii", "inplace", "traffic_copy", n,
                    upper_w, floor_copy_w, guess);
                row("memory_exchange_apart", "swap", "traffic_copy", n,
                    exchange_w, floor_copy_w, guess);
                memcpy(dst, src, n);
                row("memory_translate", "table", "traffic_copy", n, translate_w,
                    floor_copy_w, guess);
#ifndef SKIP_HEX
                row("memory_into_hex", "encode", "compute", n, hex_w,
                    floor_copy_w, guess);
#endif
                row("memory_copy_end", "copy", "traffic_copy", n, copy_end_w,
                    floor_copy_w, guess);
                row("memory_copy_apart_end", "copy", "traffic_copy", n,
                    copy_apart_end_w, floor_copy_w, guess);

                fill_az(n);
                row("string_length", "len", "traffic_one", n, strlen_w,
                    floor_one_w, guess);
                row("string_length_max", "len", "traffic_one", n, strnlen_w,
                    floor_one_w, guess);
                row("string_compare", "equal", "traffic_two", n, strcmp_w,
                    floor_two_w, guess);
                row("string_compare_max", "equal", "traffic_two", n, strncmp_w,
                    floor_two_w, guess);
                row("string_copy", "copy", "traffic_copy", n, strcpy_w,
                    floor_copy_w, guess);
                row("string_copy_max", "copy", "traffic_copy", n, strncpy_w,
                    floor_copy_w, guess);

                memcpy(src2, src, n + 1);
                row("memory_compare", "equal", "traffic_two", n, memcmp_w,
                    floor_two_w, guess);
                src2[0] ^= 0xff;
                row("memory_compare", "miss@0", "traffic_two", n, memcmp_w,
                    floor_two_w, 8);
                memcpy(src2, src, n + 1);
                src2[n - 1] ^= 0xff;
                row("memory_compare", "miss@end", "traffic_two", n, memcmp_w,
                    floor_two_w, guess);
                memcpy(src2, src, n + 1);
                row("memory_common_prefix", "equal", "traffic_two", n, prefix_w,
                    floor_two_w, guess);
                row("memory_compare_ascii_case", "equal", "traffic_two", n,
                    memcmp_case_w, floor_two_w, guess);

                memcpy(src2, src, n + 1);
                src2[0] ^= 1;
                row("string_compare", "miss@0", "traffic_two", n, strcmp_w,
                    floor_two_w, 8);
                memcpy(src2, src, n + 1);
                src2[n - 1] ^= 1;
                row("string_compare", "miss@end", "traffic_two", n, strcmp_w,
                    floor_two_w, guess);

                memset(src, 'a', n);
                src[0] = 'Z';
                src[n] = 0;
                row("memory_first_of", "hit@0", "traffic_one", n, memchr_w,
                    floor_one_w, 8);
                src[0] = 'a';
                src[n - 1] = 'Z';
                row("memory_first_of", "hit@end", "traffic_one", n, memchr_w,
                    floor_one_w, guess);
                row("memory_last_of", "hit@end", "traffic_one", n, memrchr_w,
                    floor_one_w, guess);
                src[n - 1] = 'a';
                row("memory_first_of", "miss", "traffic_one", n, memchr_w,
                    floor_one_w, guess);
                row("memory_count", "count", "traffic_one", n, count_w,
                    floor_one_w, guess);
                row("memory_sum_bytes", "sum", "compute", n, sum_w, floor_one_w,
                    guess);
                row("memory_hash_33", "hash", "compute", n, hash33_w,
                    floor_one_w, guess);
                row("hash_xxh64", "hash", "compute", n, xxh_w, floor_one_w,
                    guess);
                row("memory_checksum_bsd16", "sum", "compute", n, bsd16_w,
                    floor_one_w, guess);
                row("memory_span_byte", "span", "traffic_one", n, span_byte_w,
                    floor_one_w, guess);
                row("memory_utf8_span", "span", "traffic_one", n, utf8_w,
                    floor_one_w, guess);

                fill_az(n);
                src[0] = 'Z';
                row("string_first_of", "hit@0", "traffic_one", n, strchr_w,
                    floor_one_w, 8);
                src[0] = 'a';
                src[n - 1] = 'Z';
                src[n] = 0;
                row("string_last_of_or_end", "hit@end", "traffic_one", n,
                    strrchr_w, floor_one_w, guess);

                fill_az(n);
                src2[0] = 'z';
                src2[1] = 'z';
                src2[2] = 0;
                row("string_search", "miss", "traffic_one", n, strstr_w,
                    floor_one_w, guess);
                row("string_find", "miss", "traffic_one", n, find_w, floor_one_w,
                    guess);
                memcpy(src2, src + (n > 8 ? 7 : 0), 3);
                src2[3] = 0;
                row("memory_search", "hit", "traffic_one", n, memmem_w,
                    floor_one_w, guess);

#ifndef SKIP_ITOA
                row("positive_into", "itoa", "compute", n, into_w, floor_one_w,
                    8);
#endif
                memcpy(src, "1234567890123", 14);
                row("string_to_positive", "atoi", "compute", n, parse_w,
                    floor_one_w, 8);
        }

#ifndef SKIP_SHA256
        row("sha256_compress", "block", "compute", 64, sha_w, floor_one_w, 20);
        for (i = 0; i < HASH_BYTES; i++)
                hash_data[i] = (unsigned char)(i * 131 ^ i >> 7);
        row("md5_blocks", "64KiB", "compute", HASH_BYTES, md5_w, md5_c_w, 60000000);
        row("sha1_blocks", "64KiB", "compute", HASH_BYTES, sha1_w, sha1_c_w, 60000000);
        row("sha256_blocks", "64KiB", "compute", HASH_BYTES, sha256_w, sha256_c_w, 60000000);
        row("sha512_blocks", "64KiB", "compute", HASH_BYTES, sha512_w, sha512_c_w, 60000000);
        row("blake2b_blocks", "64KiB", "compute", HASH_BYTES, blake2b_w, blake2b_c_w, 60000000);
#endif
#if defined(USE_OURS) && !defined(SKIP_LOCK)
        /* Alone: the threads_live elision against the two stores it makes.
           Company: the atomic path against the inline C pair it replaced. */
        row("lock_take", "pair_alone", "latency", 0, lock_pair_w, lock_store_w, 2);
        threads_live = 1;
        row("lock_take", "pair_company", "latency", 0, lock_pair_w, lock_atomic_w, 8);
        threads_live = 0;
#endif
#ifndef SKIP_FIELD
        row("p256_multiply", "field", "compute", 32, p256_mul_w, p256_mul_c_w, 8);
        row("p256_square", "field", "compute", 32, p256_sqr_w, p256_sqr_c_w, 6);
        row("p256_add", "field", "compute", 32, p256_add_w, p256_add_c_w, 2);
        row("p256_subtract", "field", "compute", 32, p256_sub_w, p256_sub_c_w, 2);
        row("p384_multiply", "field", "compute", 48, p384_mul_w, p384_mul_c_w, 20);
        row("p384_square", "field", "compute", 48, p384_sqr_w, p384_sqr_c_w, 15);
        row("p384_add", "field", "compute", 48, p384_add_w, p384_add_c_w, 2);
        row("p384_subtract", "field", "compute", 48, p384_sub_w, p384_sub_c_w, 2);
#endif
#ifndef SKIP_GHASH
        row("ghash_blocks", "record", "compute", 16384, ghash_w, ghash_c_w,
            60000);
#endif
#ifndef SKIP_AES
        row("aes128_ctr_blocks", "record", "compute", 16384, aes_w, aes_c_w,
            20000);
#endif
        return 0;
}
#endif /* BENCH_hardware_floor */

#ifdef BENCH_libc_audit
/*
        Floor vs the host libc, same call shapes.

            Linux (spark-free, two binaries):
                cc -O2 -fno-builtin -DUSE_LIBC -DBENCH_libc_audit test/checks.c -o /tmp/audit.libc
                cc -O2 -ffreestanding -fno-builtin -fno-stack-protector \
                    -c -x c -o /tmp/libmw.o -I. src/lib.c
                printf '%s\n' '#include "src/lib.c"' > /tmp/mw.c
                cc -O2 -ffreestanding -fno-builtin -fno-stack-protector \
                    -c /tmp/mw.c -o /tmp/libmw.o -I.
                cc -O2 -fno-builtin -DUSE_OURS -DBENCH_libc_audit test/checks.c /tmp/libmw.o \
                    -o /tmp/audit.ours

            Darwin (lifted ARM64 bodies):
                python3 test/differential.py --harness native_extract src/lib.c \
                    ... > /tmp/lifted.h
                cc -O2 -fno-builtin -DUSE_LIBC -DBENCH_libc_audit test/checks.c -o /tmp/audit.libc
                cc -O2 -fno-builtin -DUSE_LIFTED -I/tmp -DBENCH_libc_audit test/checks.c \
                    -o /tmp/audit.ours

        Prints CSV: name,shape,bytes,ns_per_call
        Ratio is libc/ours (>1 we win) and is joined outside.
*/

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef USE_LIFTED
#define ASM_WIDER(a, b) ""
#define ASM_NARROW(a, b) ""
#define ASM_RET "ret\n"
#define AVX2_WIDTH "32"
#define AVX2_ZEROED ""
#define AVX2_ZEROS_AT(x) ""
#define AVX2_LEAVE ""
#define STRING_COPY_TAIL ""
#define WIDE_LENGTH_INTO(...) ""
#include "lifted.h"
#endif

#ifdef USE_OURS
void *memory_copy_apart(void *, const void *, unsigned long);
void *memory_copy(void *, const void *, unsigned long);
void *memory_fill(void *, int, unsigned long);
unsigned long string_length(const char *);
unsigned long string_length_max(const char *, unsigned long);
int memory_compare(const void *, const void *, unsigned long);
void *memory_first_of(const void *, int, unsigned long);
int string_compare(const char *, const char *);
int string_compare_max(const char *, const char *, unsigned long);
char *string_first_of(const char *, int);
char *string_last_of_or_end(const char *, int);
char *string_copy(char *, const char *);
char *string_search(const char *, const char *);
void *memory_search(const void *, unsigned long, const void *, unsigned long);
#elif defined(USE_LIFTED)
void *memory_copy_apart(void *, const void *, unsigned long);
void *memory_copy(void *, const void *, unsigned long);
void *memory_fill(void *, int, unsigned long);
unsigned long string_length(const char *);
unsigned long string_length_max(const char *, unsigned long);
int memory_compare(const void *, const void *, unsigned long);
void *memory_first_of(const void *, int, unsigned long);
int string_compare(const char *, const char *);
int string_compare_max(const char *, const char *, unsigned long);
char *string_first_of(const char *, int);
char *string_last_of_or_end(const char *, int);
char *string_copy(char *, const char *);
char *string_search(const char *, const char *);
void *memory_search(const void *, unsigned long, const void *, unsigned long);
#endif

#if defined(USE_OURS) || defined(USE_LIFTED)
#define COPY memory_copy_apart
#define MOVE memory_copy
#define FILL memory_fill
#define LEN string_length
#define LENMAX string_length_max
#define CMP memory_compare
#define CHR memory_first_of
#define SCMP string_compare
#define SCMPMAX string_compare_max
#define SCHR string_first_of
#define SRCHR string_last_of_or_end
#define SCOPY string_copy
#define SSTR string_search
#define MMEM memory_search
#else
#define COPY memcpy
#define MOVE memmove
#define FILL memset
#define LEN strlen
#define LENMAX strnlen
#define CMP memcmp
#define CHR memchr
#define SCMP strcmp
#define SCMPMAX strncmp
#define SCHR strchr
#define SRCHR strrchr
#define SCOPY strcpy
#define SSTR strstr
#define MMEM memmem
#endif

enum { ROOM = 1 << 20 };
static unsigned char src[ROOM] __attribute__((aligned(64)));
static unsigned char dst[ROOM] __attribute__((aligned(64)));
static unsigned char src2[ROOM] __attribute__((aligned(64)));
static volatile unsigned long sink;

static uint64_t now_ns(void)
{
#if defined(__APPLE__)
        return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC_RAW, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

static void (*work_fn)(unsigned long, unsigned long);

static uint64_t best_ns(void (*work)(unsigned long, unsigned long),
                        unsigned long size, unsigned long rounds)
{
        uint64_t best = (uint64_t)-1;
        int trial;

        work(size, rounds / 8 + 1);
        for (trial = 0; trial < 5; trial++)
        {
                uint64_t start = now_ns();
                work(size, rounds);
                uint64_t took = now_ns() - start;
                if (took < best)
                        best = took;
        }
        return best;
}

static unsigned long rounds_for(unsigned long guess_ns)
{
        unsigned long rounds = 80000000ull / (guess_ns ? guess_ns : 1);
        if (rounds < 80)
                rounds = 80;
        if (rounds > 2000000)
                rounds = 2000000;
        return rounds;
}

HARDWARE_WORK(memcpy_w, , (unsigned long)COPY(dst, src, size))
HARDWARE_WORK(memmove_w, , (unsigned long)MOVE(dst, src, size))
HARDWARE_WORK(memset_w, , (unsigned long)FILL(dst, 0xa5, size))
HARDWARE_WORK(strlen_w, , LEN((char *)src))
HARDWARE_WORK(strnlen_w, , LENMAX((char *)src, size))
HARDWARE_WORK(memcmp_w, , (unsigned)CMP(src, src2, size))
HARDWARE_WORK(memchr_w, , (unsigned long)CHR(src, 'Z', size))
HARDWARE_WORK(strcmp_w, , (unsigned)SCMP((char *)src, (char *)src2))
HARDWARE_WORK(strncmp_w, , (unsigned)SCMPMAX((char *)src, (char *)src2, size))
HARDWARE_WORK(strchr_w, , (unsigned long)SCHR((char *)src, 'Z'))
HARDWARE_WORK(strrchr_w, , (unsigned long)SRCHR((char *)src, 'Z'))
HARDWARE_WORK(strcpy_w, , (unsigned long)SCOPY((char *)dst, (char *)src))
HARDWARE_WORK(strstr_w, , (unsigned long)SSTR((char *)src, (char *)src2))
HARDWARE_WORK(memmem_w, , (unsigned long)MMEM(src, size, src2, 3))

static void row(const char *name, const char *shape, unsigned long size,
                void (*work)(unsigned long, unsigned long), unsigned long guess)
{
        unsigned long rounds = rounds_for(guess);
        double ns = (double)best_ns(work, size, rounds) / (double)rounds;
        printf("%s,%s,%lu,%.3f\n", name, shape, size, ns);
}

static void fill_az(unsigned long n)
{
        unsigned long i;
        for (i = 0; i < n; i++)
                src[i] = (unsigned char)('a' + (i % 26));
        src[n] = 0;
        memcpy(src2, src, n + 1);
}

int main(void)
{
        static const unsigned long sizes[] = {8, 16, 32, 64, 256, 4096, 65536};
        unsigned long s, n, guess;

        for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        {
                n = sizes[s];
                guess = n < 64 ? 8 : n < 4096 ? 20 : n / 8;

                memset(src, 0x5a, n);
                row("memcpy", "copy", n, memcpy_w, guess);
                row("memmove", "apart", n, memmove_w, guess);
                row("memset", "fill", n, memset_w, guess);

                fill_az(n);
                row("strlen", "len", n, strlen_w, guess);
                row("strnlen", "len", n, strnlen_w, guess);
                row("strcmp", "equal", n, strcmp_w, guess);
                row("strncmp", "equal", n, strncmp_w, guess);
                row("strcpy", "copy", n, strcpy_w, guess);

                memcpy(src2, src, n + 1);
                row("memcmp", "equal", n, memcmp_w, guess);
                src2[0] ^= 0xff;
                row("memcmp", "miss@0", n, memcmp_w, 8);
                memcpy(src2, src, n + 1);
                src2[n > 8 ? 7 : 0] ^= 0xff;
                row("memcmp", "miss@7", n, memcmp_w, 8);
                memcpy(src2, src, n + 1);
                src2[n - 1] ^= 0xff;
                row("memcmp", "miss@end", n, memcmp_w, guess);

                memcpy(src2, src, n + 1);
                src2[0] ^= 1;
                row("strcmp", "miss@0", n, strcmp_w, 8);
                memcpy(src2, src, n + 1);
                src2[n > 8 ? 7 : 0] ^= 1;
                row("strcmp", "miss@7", n, strcmp_w, 8);
                memcpy(src2, src, n + 1);
                src2[n - 1] ^= 1;
                row("strcmp", "miss@end", n, strcmp_w, guess);

                memcpy(src2, src, n + 1);
                src2[n > 8 ? 7 : 0] ^= 1;
                row("strncmp", "miss@7", n, strncmp_w, 8);

                memset(src, 'a', n);
                src[0] = 'Z';
                src[n] = 0;
                row("strchr", "at-0", n, strchr_w, 8);
                row("memchr", "at-0", n, memchr_w, 8);
                src[0] = 'a';
                src[n - 1] = 'Z';
                row("strchr", "at-end", n, strchr_w, guess);
                row("strrchr", "at-end", n, strrchr_w, guess);
                row("memchr", "at-end", n, memchr_w, guess);

                memset(src, 'a', n);
                src[n] = 0;
                src2[0] = 'x';
                src2[1] = 'y';
                src2[2] = 'z';
                src2[3] = 0;
                row("strstr", "miss", n, strstr_w, n < 256 ? 20 : n / 20);
                row("memmem", "miss", n, memmem_w, n < 256 ? 20 : n / 20);
                memcpy(src + (n > 3 ? n - 3 : 0), "xyz", 3);
                src[n] = 0;
                row("strstr", "hit-end", n, strstr_w, n < 256 ? 20 : n / 20);
                row("memmem", "hit-end", n, memmem_w, n < 256 ? 20 : n / 20);
        }

        return sink ? 0 : 0;
}
#endif /* BENCH_libc_audit */

#ifdef CHECK_library_inline
/*
        Compile-time targeting probe for lib.util.c.

        A translation unit sets LIBRARY_SIZE_MAX (and the family INLINE
        knobs) before the include. This file is two literal copies: 16 and
        64 bytes. With LIBRARY_SIZE_MAX 16 the 64-byte site must remain a
        call; with 128 it must be the inlined window. objdump of the two
        probe_* symbols is the evidence.

            cc -O2 -ffreestanding -fno-builtin -fno-stack-protector -c \
                -DLIBRARY_SIZE_MAX=16 -o /tmp/probe16.o \
                -DCHECK_library_inline test/checks.c -I.
            cc -O2 -ffreestanding -fno-builtin -fno-stack-protector -c \
                -DLIBRARY_SIZE_MAX=128 -o /tmp/probe128.o \
                -DCHECK_library_inline test/checks.c -I.
*/

#include "../../src/lib.util.c"

__attribute__((noinline, noclone))
void probe_copy_16(void *d, void *s)
{
        memory_copy(d, s, 16);
}

__attribute__((noinline, noclone))
void probe_copy_64(void *d, void *s)
{
        memory_copy(d, s, 64);
}

__attribute__((noinline, noclone))
void probe_fill_32(void *d)
{
        memory_fill(d, 0, 32);
}
#endif /* CHECK_library_inline */

