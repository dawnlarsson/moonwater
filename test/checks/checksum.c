/*
        test/checks/checksum.c: checksums and CRCs, and their benchmarks.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_checksum_crc
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p32 crc_model(p8 address_to bytes, positive length, p32 crc)
{
        while (length--)
        {
                crc ^= (p32)*bytes++ << 24;
                for (positive bit = 0; bit < 8; bit++)
                        crc = (crc << 1) ^
                              (crc >> 31 ? 0x04c11db7u : 0);
        }
        return crc;
}

/*
        The hash block cores against the rounds they replaced, and the
        streaming digests against known answers.

        The references are the textbook rounds and have nothing in common
        with the assembly: no renamed registers, no carried terms, no
        schedule ring, no extension instructions. Every pass runs each body
        this machine has by writing the feature bytes down, one subset at a
        time, and putting them back. Runs end against a protected page, the
        data must come back untouched, zero blocks must change nothing, and
        the BLAKE2b counter carries out of its low word.

        The known answers are Python hashlib's: "abc", a million "a" written
        a thousand bytes at a time, and one SHA-256 over the digests of every
        prefix of 0 to 300 bytes of the CRC pattern, which walks every length
        a padding branch cares about in both block sizes.
*/
#define SHARED_hash_reference
#include "../checks.c"
#undef SHARED_hash_reference

static p64 hash_check_seed = 0x9e3779b97f4a7c15ull;

static p8 hash_check_byte(void)
{
        XORSHIFT64(hash_check_seed);
        return (p8)(hash_check_seed >> 29);
}

static p8 hash_check_hex(const p8 address_to bytes, positive length,
                           string_address hex)
{
        p8 text[130];

        return string_length(hex) == 2 * length &&
               memory_into_hex(text, (p8 address_to)bytes, length) == 2 * length &&
               !memory_compare(text, hex, 2 * length);
}

/* Every block core, every run length to 40 blocks, against its reference. */
static positive hash_check_cores(p8 address_to limit)
{
        positive wrong = 0;
        p8 kept[128 * 40];

        for (positive round = 0; round < 700; round++)
        {
                positive blocks = round < 300 ? round % 41 : hash_check_byte() % 41;
                p8 address_to wide = limit - 128 * blocks;
                p8 address_to narrow = limit - 64 * blocks;

                for (p8 address_to at = limit - sizeof(kept); at < limit; at++)
                        *at = round % 53 == 7 ? 0xff : hash_check_byte();
                memory_copy(kept, limit - sizeof(kept), sizeof(kept));

                p32 n0[8], n1[8], n2[8];
                p64 w0[11], w1[11], w2[11];

                for (positive i = 0; i < 8; i++)
                        n0[i] = (p32)hash_check_byte() << 24 | (p32)hash_check_byte() << 13 |
                                hash_check_byte();
                for (positive i = 0; i < 11; i++)
                        w0[i] = (p64)n0[i % 8] << 29 ^ hash_check_byte() ^ (p64)hash_check_byte() << 56;
                if (round % 5 == 1)
                        w0[8] = ~(p64)0 - hash_check_byte();
                if (round % 7 == 0)
                        w0[10] = 0;
                positive tail = hash_check_byte() % 129;

                memory_copy(n1, n0, sizeof n0); memory_copy(n2, n0, sizeof n0);
                hash_reference_md5(n1, narrow, blocks);
                md5_blocks(n2, narrow, blocks);
                wrong += memory_compare(n1, n2, sizeof n1) != 0;

                memory_copy(n1, n0, sizeof n0); memory_copy(n2, n0, sizeof n0);
                hash_reference_sha1(n1, narrow, blocks);
                sha1_blocks(n2, narrow, blocks);
                wrong += memory_compare(n1, n2, sizeof n1) != 0;

                memory_copy(n1, n0, sizeof n0); memory_copy(n2, n0, sizeof n0);
                hash_reference_sha256(n1, narrow, blocks);
                sha256_blocks(n2, narrow, blocks);
                wrong += memory_compare(n1, n2, sizeof n1) != 0;

                memory_copy(w1, w0, sizeof w0); memory_copy(w2, w0, sizeof w0);
                hash_reference_sha512(w1, wide, blocks);
                sha512_blocks(w2, wide, blocks);
                wrong += memory_compare(w1, w2, sizeof w1) != 0;

                memory_copy(w1, w0, sizeof w0); memory_copy(w2, w0, sizeof w0);
                hash_reference_blake2b(w1, wide, blocks, tail);
                blake2b_blocks(w2, wide, blocks, tail);
                wrong += memory_compare(w1, w2, sizeof w1) != 0;

                if (blocks == 1)
                {
                        memory_copy(n1, n0, sizeof n0); memory_copy(n2, n0, sizeof n0);
                        hash_reference_sha256(n1, narrow, 1);
                        sha256_compress(n2, narrow);
                        wrong += memory_compare(n1, n2, sizeof n1) != 0;
                }

                wrong += memory_compare(kept, limit - sizeof(kept), sizeof(kept)) != 0;
        }

        return wrong;
}

typedef struct
{
        p8 algorithm;
        p8 size;
        string_address abc;
        string_address million;
        string_address prefixes;
} hash_known;

static const hash_known hash_known_answers[] = {
    {DIGEST_MD5, 16, "900150983cd24fb0d6963f7d28e17f72", "7707d6ae4e027c70eea2a935c2296f21",
     "c57122937f1e88fbfb78519972fbc1947787867aa9246d518d8b25e8f153ad69"},
    {DIGEST_SHA1, 20, "a9993e364706816aba3e25717850c26c9cd0d89d", "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
     "5161c9d0714b463562f96b93f01769cb63f8e8b6ffa3396eebf0340a327d0392"},
    {DIGEST_SHA224, 28, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
     "20794655980c91d8bbb4c1ea97618a4bf03f42581948b2ee4ee7ad67",
     "22a7c48d9df4a71b924b6cb73ed3540beccfffbb3ef68ee26649093988e660f8"},
    {DIGEST_SHA256, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
     "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
     "aff524ac6cb4167a937f9cc6db4a070a7732401a07a57ea02099d4695421890d"},
    {DIGEST_SHA384, 48,
     "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
     "9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985",
     "aee147e9c3a93089dfeec0abaeb3930da8d88e27454dd647a89f50efcfdfb4ac"},
    {DIGEST_SHA512, 64,
     "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
     "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b",
     "a87e61ec127593fe7a1a3c29358d5caad988dc34e4c2b7cc4043ca8295fdcbca"},
    {DIGEST_BLAKE2B, 64,
     "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923",
     "98fb3efb7206fd19ebf69b6f312cf7b64e3b94dbe1a17107913975a793f177e1d077609d7fba363cbba00d05f7aa4e4fa8715d6428104c0a75643b0ff3fd3eaf",
     "f9ec4eaca209a007f292a85842515342d655b1dcf3a9324afb97b3ee82b8a31a"},
    {DIGEST_BLAKE2B, 32, "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319",
     "0741850f36cba4259628355d1073e24ddb9ca0e1bfac36fd39ae5dc2101e23a4",
     "13bb7fffa1e6d7874a8020b625c7c9796ef7169cc4d9122be842c20a1a750c46"},
    {DIGEST_BLAKE2B, 1, "6b", "ef", "83d73b617f64886dee62baec67f7ee51d7517627aa57c4083c487df4916dbf44"},
};

/* The streaming digests: known answers, and every prefix split three ways. */
static positive hash_check_streams(const p8 address_to pattern)
{
        positive wrong = 0;
        p8 thousand[1000];

        memory_fill(thousand, 'a', sizeof thousand);

        for (positive which = 0; which < array_count(hash_known_answers); which++)
        {
                const hash_known address_to known = hash_known_answers + which;
                digest_state digest;
                digest_state outer;
                p8 out[64];
                p8 again[64];

                digest_open(address_of digest, known->algorithm, known->size);
                digest_write(address_of digest, "abc", 3);
                digest_close(address_of digest, out);
                wrong += !hash_check_hex(out, known->size, known->abc);

                digest_open(address_of digest, known->algorithm, known->size);
                for (positive i = 0; i < 1000; i++)
                        digest_write(address_of digest, thousand, sizeof thousand);
                digest_close(address_of digest, out);
                wrong += !hash_check_hex(out, known->size, known->million);

                digest_open(address_of outer, DIGEST_SHA256, 32);
                for (positive length = 0; length <= 300; length++)
                {
                        digest_open(address_of digest, known->algorithm, known->size);
                        digest_write(address_of digest, pattern, length);
                        digest_close(address_of digest, out);
                        digest_write(address_of outer, out, known->size);

                        positive first = hash_check_byte() % (length + 1);
                        positive second = hash_check_byte() % (length - first + 1);

                        digest_open(address_of digest, known->algorithm, known->size);
                        digest_write(address_of digest, pattern, first);
                        digest_write(address_of digest, pattern + first, second);
                        digest_write(address_of digest, pattern + first + second,
                                     length - first - second);
                        digest_close(address_of digest, again);
                        wrong += memory_compare(out, again, known->size) != 0;
                }
                digest_close(address_of outer, out);
                wrong += !hash_check_hex(out, 32, known->prefixes);
        }

        return wrong;
}

/*
        hash_hmac_sha256_prepared against the streaming digests it stands in
        for, over three keys: every message length from 0 to 300, ending on
        the guard page so that a read past the end is a fault and not a
        wrong answer. Where the machine has no body for it the answer is no,
        and the buffer it was handed is as it was; where it has one the 32
        bytes are the HMAC.
*/
#if X64
#define HASH_HMAC_MADE (cpu_has_sha != 0)
#else
#define HASH_HMAC_MADE false
#endif

static positive hash_check_hmac(p8 address_to limit, p8 made)
{
        positive wrong = 0;

        for (positive round = 0; round < 3; round++)
        {
                p8 key[64];
                p8 pad[64];
                digest_state inner;
                digest_state outer;

                for (positive at = 0; at < 64; at++)
                        key[at] = hash_check_byte();
                for (positive at = 0; at < 64; at++)
                        pad[at] = key[at] ^ 0x36;
                digest_open(address_of inner, DIGEST_SHA256, 32);
                digest_write(address_of inner, pad, 64);
                for (positive at = 0; at < 64; at++)
                        pad[at] = key[at] ^ 0x5c;
                digest_open(address_of outer, DIGEST_SHA256, 32);
                digest_write(address_of outer, pad, 64);

                for (positive length = 0; length <= 300; length++)
                {
                        p8 address_to data = limit - length;
                        p8 middle[32];
                        p8 want[32];
                        p8 out[32];
                        digest_state work = inner;

                        digest_write(address_of work, data, length);
                        digest_close(address_of work, middle);
                        work = outer;
                        digest_write(address_of work, middle, 32);
                        digest_close(address_of work, want);

                        memory_fill(out, 0xa5, sizeof out);
                        p8 got = hash_hmac_sha256_prepared(inner.state.narrow,
                                                             outer.state.narrow,
                                                             data, length, out);

                        wrong += got != made;
                        if (got)
                                wrong += memory_compare(out, want, 32) != 0;
                        else
                                for (positive at = 0; at < 32; at++)
                                        wrong += out[at] != 0xa5;
                }
        }

        return wrong;
}

static fn hash_check_all(p8 address_to limit)
{
        positive cores = 0;
        positive streams = 0;
        positive hmacs = 0;
        positive bodies = 1;

#ifndef KERNEL_MODE
        //      One call through a dispatching core runs cpu_hash_detect and
        //      writes the bytes this machine has; each pass after the first
        //      takes a subset away.
        {
                p32 scratch[8] = {0};
                p8 block[64] = {0};
                sha256_blocks(scratch, block, 1);
        }
        p8 sha = cpu_has_sha;
        p8 sha512 = cpu_has_sha512;
        p8 zbb = cpu_has_zbb;
#if X64
        p8 avx2 = cpu_has_avx2;
        p8 avx512 = cpu_has_avx512;
#endif
        for (positive pass = 1; pass < 32; pass++)
        {
                cpu_has_sha = (pass & 1) ? 0 : sha;
                cpu_has_sha512 = (pass & 2) ? 0 : sha512;
                cpu_has_zbb = (pass & 16) ? 0 : zbb;
#if X64
                cpu_has_avx512 = (pass & 4) ? 0 : avx512;
                cpu_has_avx2 = (pass & 8) ? 0 : avx2;
                if (((pass & 1) && !sha) || ((pass & 2) && !sha512) ||
                    ((pass & 4) && !avx512) || ((pass & 8) && !avx2) || ((pass & 16) && !zbb))
                        continue;
#else
                if ((pass & 12) || ((pass & 1) && !sha) || ((pass & 2) && !sha512) ||
                    ((pass & 16) && !zbb))
                        continue;
#endif
                bodies++;
                cores += hash_check_cores(limit);
                streams += hash_check_streams(limit - 8192);
                hmacs += hash_check_hmac(limit, HASH_HMAC_MADE);
        }
        cpu_has_sha = sha;
        cpu_has_sha512 = sha512;
        cpu_has_zbb = zbb;
#if X64
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#endif
#endif
        cores += hash_check_cores(limit);
        streams += hash_check_streams(limit - 8192);
        hmacs += hash_check_hmac(limit, HASH_HMAC_MADE);

        check("hash cores ran under at least the floor's feature bytes", bodies >= 1);
        check("an HMAC-SHA-256 under a prepared key is the streaming digests', or no, to the end of the message",
              hmacs == 0);
        check("hash block cores agree with the textbook rounds", cores == 0);
        check("streaming digests give the known answers at every split", streams == 0);
}

/*
        hash_crc32_msb, cksum's CRC, against the bit-at-a-time model above:
        its table slice by slice, then every size to 300 at 32 alignments,
        runs that end on the protected page, and random lengths at random
        alignments, each whole and in three pieces, under every feature
        subset this machine has -- the braided table floor, the PCLMULQDQ
        or PMULL folds, and on x86_64 the VPCLMULQDQ zmm folds -- written
        down one tier at a time and put back.
*/
static p32 crc_swap32(p32 value)
{
        return value >> 24 | (value >> 8 & 0xff00) | (value << 8 & 0xff0000) |
               value << 24;
}

static positive crc_check_case(p8 address_to at, positive size, p32 seed,
                               positive split)
{
        p32 want = crc_model(at, size, seed);
        positive rest = (size - split) / 2;
        positive bad = 0;
        p8 pclmul = cpu_has_pclmul;
#if X64
        p8 vpclmul = cpu_has_vpclmul;
        p8 avx512 = cpu_has_avx512;
        positive tiers = 4;
#else
        positive tiers = 2;
#endif

        for (positive tier = 0; tier < tiers; tier++)
        {
                cpu_has_pclmul = tier >= 1 ? pclmul : 0;
#if X64
                cpu_has_vpclmul = tier >= 2 ? vpclmul : 0;
                cpu_has_avx512 = tier >= 3 ? avx512 : 0;
#endif
                bad += hash_crc32_msb(seed, at, size) != want;

                p32 crc = hash_crc32_msb(seed, at, split);
                crc = hash_crc32_msb(crc, at + split, rest);
                crc = hash_crc32_msb(crc, at + split + rest, size - split - rest);
                bad += crc != want;
        }

        cpu_has_pclmul = pclmul;
#if X64
        cpu_has_vpclmul = vpclmul;
        cpu_has_avx512 = avx512;
#endif
        return bad;
}

/*
        The 16-lane fold keeps sixteen vector registers and v8 to v15 are the
        caller's: their low halves are written into d8 to d15, the three CRCs
        are run over spans that take each tier, and the registers are read
        back. arm64 only; nothing else saves a register a caller owns.
*/
#if ARM64
static positive crc_check_registers(p8 address_to bytes)
{
        static const p64 sentinel[8] = {0x0123456789abcdefull, 0x1122334455667788ull, 0x2468ace013579bdfull, 0x8000000000000001ull,
                                        0xfedcba9876543210ull, 0x0f1e2d3c4b5a6978ull, 0xa5a5a5a55a5a5a5aull, 0xdeadbeefcafef00dull};
        positive bad = 0;
        static const positive sizes[] = {64, 130, 511, 512, 700, 1279, 1280, 1281, 4096, 5000, 8000};

        for (positive k = 0; k < array_count(sizes); k++)
                for (positive which = 0; which < 3; which++)
                {
                        p64 got[8];
                        volatile p64 sink = 0;

                        __asm__ volatile("ldp d8, d9, [%0]\n   ldp d10, d11, [%0, #16]\n   ldp d12, d13, [%0, #32]\n   ldp d14, d15, [%0, #48]\n"
                                         : : "r"(sentinel) : "memory", "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15");
                        if (which == 0)
                                sink = hash_crc32(1, bytes + 3, sizes[k]);
                        else if (which == 1)
                                sink = hash_crc32c(1, bytes + 3, sizes[k]);
                        else
                                sink = hash_crc64(1, bytes + 3, sizes[k]);
                        __asm__ volatile("stp d8, d9, [%0]\n   stp d10, d11, [%0, #16]\n   stp d12, d13, [%0, #32]\n   stp d14, d15, [%0, #48]\n"
                                         : : "r"(got) : "memory");
                        bad += memory_compare(got, sentinel, sizeof got) != 0;
                }
        return bad;
}
#endif

static fn crc_check_all(p8 address_to bytes)
{
        static const p8 zero = 0;
        positive table = 0;

        for (positive slice = 0; slice < 16; slice++)
                for (positive value = 0; value < 256; value++)
                {
                        p8 byte = (p8)value;
                        p32 entry = crc_model(address_of byte, 1, 0);

                        for (positive at = 0; at < (slice < 8 ? slice : slice + 16); at++)
                                entry = crc_model((p8 address_to)address_of zero, 1, entry);
                        table += hash_crc32_msb_tab[slice * 256 + value] !=
                                 crc_swap32(entry);
                }

        positive aligned = 0;
        positive tails = 0;
        positive draws = 0;

        for (positive size = 0; size <= 300; size++)
                for (positive offset = 0; offset < 32; offset++)
                        aligned += crc_check_case(bytes + offset, size,
                                                  (p32)(size * 0x9e3779b9u + offset),
                                                  size / 3);
        for (positive size = 0; size <= 8192; size += size < 300 ? 1 : 127)
                tails += crc_check_case(bytes + 8192 - size, size, 0xdeadbeef,
                                        size / 3);

        p32 random = 0x2545f491u;
        for (positive draw = 0; draw < 400; draw++)
        {
                XORSHIFT32(random);
                positive offset = random % 64;
                p32 seed = random;
                XORSHIFT32(random);
                positive size = random % (8192 - offset + 1);
                draws += crc_check_case(bytes + offset, size, seed,
                                        (random >> 7) % (size + 1));
        }

        check("the MSB CRC table is the model's, slice by slice", table == 0);
        check("hash_crc32_msb is the model at every size to 300 and 32 alignments",
              aligned == 0);
        check("hash_crc32_msb runs that end on a protected page", tails == 0);
        check("hash_crc32_msb at random lengths, alignments and splits",
              draws == 0);
        check("hash_crc32_msb of nothing is the crc",
              hash_crc32_msb(0x12345678u, null, 0) == 0x12345678u);
}

/*
        hash_crc32c against the bit-at-a-time reflected model: its table
        slice by slice, RFC 3720's check value, then every size to 300 at 32
        alignments, runs that end on the protected page -- long enough for
        the three-stream path -- and random lengths, alignments and splits,
        under every body this machine has: slicing by eight, the crc32
        instruction one stream at a time, the three streams PCLMULQDQ joins
        and on x86_64 with AVX-512 the zmm fold; on arm64 the PMULL folds of
        4, 8 and 16 lanes in place of the streams (on RISC-V the table and
        the Zbc Barrett step).
*/
static p32 crc32c_model(const p8 address_to bytes, positive length, p32 crc)
{
        while (length--)
        {
                crc ^= *bytes++;
                for (positive bit = 0; bit < 8; bit++)
                        crc = (crc >> 1) ^ (crc & 1 ? 0x82f63b78u : 0);
        }
        return crc;
}

static positive crc32c_check_case(p8 address_to at, positive size, p32 seed,
                                  positive split)
{
        p32 want = crc32c_model(at, size, seed);
        positive rest = (size - split) / 2;
        positive bad = 0;
        p8 pclmul = cpu_has_pclmul;
#if X64
        p8 instruction = cpu_has_sse42;
        p8 vpclmul = cpu_has_vpclmul;
        p8 avx512 = cpu_has_avx512;
        positive tiers = 4;
#elif ARM64
        p8 instruction = cpu_has_crc32;
        positive tiers = 3;
#else
        p8 instruction = pclmul;
        positive tiers = 3;
#endif

        for (positive tier = 0; tier < tiers; tier++)
        {
                cpu_has_pclmul = tier >= 2 ? pclmul : 0;
#if X64
                cpu_has_sse42 = tier >= 1 ? instruction : 0;
                cpu_has_vpclmul = tier >= 3 ? vpclmul : 0;
                cpu_has_avx512 = tier >= 3 ? avx512 : 0;
#elif ARM64
                cpu_has_crc32 = tier >= 1 ? instruction : 0;
#endif
                bad += hash_crc32c(seed, at, size) != want;

                p32 crc = hash_crc32c(seed, at, split);
                crc = hash_crc32c(crc, at + split, rest);
                crc = hash_crc32c(crc, at + split + rest, size - split - rest);
                bad += crc != want;
        }

        cpu_has_pclmul = pclmul;
#if X64
        cpu_has_sse42 = instruction;
        cpu_has_vpclmul = vpclmul;
        cpu_has_avx512 = avx512;
#elif ARM64
        cpu_has_crc32 = instruction;
#endif
        return bad;
}

static fn crc32c_check_all(p8 address_to bytes)
{
        static const p8 zero = 0;
        static const p8 digits[] = "123456789";
        positive table = 0;
        p32 sink = hash_crc32c(0, digits, 1);

        for (positive slice = 0; slice < 8; slice++)
                for (positive value = 0; value < 256; value++)
                {
                        p8 byte = (p8)value;
                        p32 entry = crc32c_model(address_of byte, 1, 0);

                        for (positive at = 0; at < slice; at++)
                                entry = crc32c_model(address_of zero, 1, entry);
                        table += hash_crc32c_tab[slice * 256 + value] != entry;
                }

        positive aligned = 0;
        positive tails = 0;
        positive draws = 0;

        for (positive size = 0; size <= 300; size++)
                for (positive offset = 0; offset < 32; offset++)
                        aligned += crc32c_check_case(bytes + offset, size,
                                                     (p32)(size * 0x9e3779b9u + offset),
                                                     size / 3);
        for (positive size = 0; size <= 8192; size += size < 300 ? 1 : size < 3000 ? 7 : 127)
                tails += crc32c_check_case(bytes + 8192 - size, size, 0xdeadbeef,
                                           size / 3);

        p32 random = 0x2545f491u ^ sink;
        for (positive draw = 0; draw < 400; draw++)
        {
                XORSHIFT32(random);
                positive offset = random % 64;
                p32 seed = random;
                XORSHIFT32(random);
                positive size = random % (8192 - offset + 1);
                draws += crc32c_check_case(bytes + offset, size, seed,
                                           (random >> 7) % (size + 1));
        }

        check("the CRC-32C table is the model's, slice by slice", table == 0);
        check("hash_crc32c of 123456789 is RFC 3720's check value",
              (hash_crc32c(~(p32)0, digits, 9) ^ ~(p32)0) == 0xe3069283);
        check("hash_crc32c is the model at every size to 300 and 32 alignments",
              aligned == 0);
        check("hash_crc32c runs that end on a protected page", tails == 0);
        check("hash_crc32c at random lengths, alignments and splits", draws == 0);
        check("hash_crc32c of nothing is the crc",
              hash_crc32c(0x12345678u, null, 0) == 0x12345678u);
}

/*
        keccak_blocks against the textbook permutation: every rate from 8 to
        168 bytes, every run to 12 blocks and a few longer ones, runs that end
        on the protected page and runs one to seven bytes short of it, with
        the state and the bytes around the data checked, under every body
        this machine has (x86_64's AVX-512 and arm64's SHA3 bodies written
        down to the floor and back). Then SHA-3 through cksum's sponge:
        FIPS 202's empty and "abc" answers at all four lengths, a million
        "a", and one SHA-256 over the digests of every prefix of 0 to 300
        bytes of the pattern, each also written in three pieces.
*/
static fn keccak_reference(p64 address_to a)
{
        static const p64 round[24] = {
            0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
            0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
            0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
            0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
            0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
            0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
            0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
            0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
        };
        static const p8 rho[25] = {0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
                                   25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14};

        for (positive r = 0; r < 24; r++)
        {
                p64 c[5], b[25];

                for (positive x = 0; x < 5; x++)
                        c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
                for (positive x = 0; x < 25; x++)
                        a[x] ^= c[(x + 4) % 5] ^ hash_reference_ror(c[(x + 1) % 5], 63);
                for (positive x = 0; x < 5; x++)
                        for (positive y = 0; y < 5; y++)
                                b[y + 5 * ((2 * x + 3 * y) % 5)] =
                                    hash_reference_ror(a[x + 5 * y], (64 - rho[x + 5 * y]) % 64);
                for (positive y = 0; y < 25; y += 5)
                        for (positive x = 0; x < 5; x++)
                                a[y + x] = b[y + x] ^ (~b[y + (x + 1) % 5] & b[y + (x + 2) % 5]);
                a[0] ^= round[r];
        }
}

static positive keccak_check_run(p8 address_to limit, positive rate, positive blocks,
                                 positive short_by)
{
        p8 address_to data = limit - rate * blocks - short_by;
        p8 before = data[-1];
        p64 want[26], got[26];

        for (positive at = 0; at < rate * blocks; at++)
                data[at] = hash_check_byte();
        for (positive i = 0; i < 26; i++)
                want[i] = got[i] = (p64)hash_check_byte() << 56 ^ hash_check_seed;
        for (positive block = 0; block < blocks; block++)
        {
                for (positive lane = 0; lane < rate / 8; lane++)
                        want[lane] ^= (p64)hash_reference_le32(data + block * rate + 8 * lane) |
                                      (p64)hash_reference_le32(data + block * rate + 8 * lane + 4) << 32;
                keccak_reference(want);
        }
        keccak_blocks(got, data, blocks, rate);
        return (memory_compare(want, got, sizeof want) != 0) + (data[-1] != before);
}

static positive keccak_check_cores(p8 address_to limit)
{
        positive wrong = 0;

        for (positive rate = 8; rate <= 168; rate += 8)
        {
                for (positive blocks = 0; blocks <= 12; blocks++)
                        for (positive short_by = 0; short_by < 8; short_by += blocks < 4 ? 1 : 3)
                                wrong += keccak_check_run(limit, rate, blocks, short_by);
                wrong += keccak_check_run(limit, rate, 8000 / rate - 2, 5);
        }
        return wrong;
}

typedef struct
{
        p8 size;
        string_address empty;
        string_address abc;
        string_address prefixes;
} keccak_known;

static const keccak_known keccak_known_answers[] = {
    {28, "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7",
     "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf",
     "02791b212c03278422657b73e01a4420453f6dc808c72a3486bd5269980e79cf"},
    {32, "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a",
     "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
     "f4c7631b6d13bc7d820ddd14c0622ac9e50540c903397a7423934489a532603b"},
    {48, "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004",
     "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25",
     "375a692cdf08c0f84ecedcba26d3b45eaf9c50b7bfd5753caece5ba670166103"},
    {64, "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26",
     "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0",
     "39d0ead02d07b005272304772b97452d6ad4ce508e01a00add1da42f807f38e8"},
};

static positive keccak_check_sponge(p8 address_to pattern)
{
        positive wrong = 0;
        p8 thousand[1000];

        //      The cores' runs wrote over the pattern; put it back.
        for (positive at = 0; at < 8192; at++)
                pattern[at] = (p8)((at * 73) ^ (at >> 3));

        memory_fill(thousand, 'a', sizeof thousand);
        for (positive which = 0; which < array_count(keccak_known_answers); which++)
        {
                const keccak_known address_to known = keccak_known_answers + which;
                checksum_sponge sponge;
                digest_state outer;
                p8 out[64], again[64];

                checksum_sponge_open(address_of sponge, false, known->size);
                checksum_sponge_close(address_of sponge, out);
                wrong += !hash_check_hex(out, known->size, known->empty);

                checksum_sponge_open(address_of sponge, false, known->size);
                checksum_sponge_write(address_of sponge, "abc", 3);
                checksum_sponge_close(address_of sponge, out);
                wrong += !hash_check_hex(out, known->size, known->abc);

                digest_open(address_of outer, DIGEST_SHA256, 32);
                for (positive length = 0; length <= 300; length++)
                {
                        checksum_sponge_open(address_of sponge, false, known->size);
                        checksum_sponge_write(address_of sponge, pattern, length);
                        checksum_sponge_close(address_of sponge, out);
                        digest_write(address_of outer, out, known->size);

                        positive first = hash_check_byte() % (length + 1);
                        positive second = hash_check_byte() % (length - first + 1);

                        checksum_sponge_open(address_of sponge, false, known->size);
                        checksum_sponge_write(address_of sponge, pattern, first);
                        checksum_sponge_write(address_of sponge, pattern + first, second);
                        checksum_sponge_write(address_of sponge, pattern + first + second,
                                              length - first - second);
                        checksum_sponge_close(address_of sponge, again);
                        wrong += memory_compare(out, again, known->size) != 0;
                }
                digest_close(address_of outer, out);
                wrong += !hash_check_hex(out, 32, known->prefixes);
        }

        checksum_sponge sponge;
        p8 out[32];

        checksum_sponge_open(address_of sponge, false, 32);
        for (positive i = 0; i < 1000; i++)
                checksum_sponge_write(address_of sponge, thousand, sizeof thousand);
        checksum_sponge_close(address_of sponge, out);
        wrong += !hash_check_hex(out, 32,
                                 "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1");
        return wrong;
}

static fn sm3_reference(p32 address_to st, const p8 address_to d, positive n)
{
        for (; n; n--, d += 64)
        {
                p32 w[68];

                for (positive j = 0; j < 16; j++)
                        w[j] = hash_reference_be32(d + 4 * j);
                for (positive j = 16; j < 68; j++)
                {
                        p32 x = w[j - 16] ^ w[j - 9] ^ hash_reference_rol(w[j - 3], 15);

                        w[j] = (x ^ hash_reference_rol(x, 15) ^ hash_reference_rol(x, 23)) ^
                               hash_reference_rol(w[j - 13], 7) ^ w[j - 6];
                }
                p32 v[8];
                for (positive i = 0; i < 8; i++)
                        v[i] = st[i];
                for (positive j = 0; j < 64; j++)
                {
                        p32 t = j < 16 ? 0x79cc4519u : 0x7a879d8au;
                        p32 a12 = hash_reference_rol(v[0], 12);
                        p32 ss1 = hash_reference_rol(a12 + v[4] + hash_reference_rol(t, j % 32), 7);
                        p32 ss2 = ss1 ^ a12;
                        p32 ff = j < 16 ? v[0] ^ v[1] ^ v[2] : (v[0] & v[1]) | (v[0] & v[2]) | (v[1] & v[2]);
                        p32 gg = j < 16 ? v[4] ^ v[5] ^ v[6] : (v[4] & v[5]) | (~v[4] & v[6]);
                        p32 tt1 = ff + v[3] + ss2 + (w[j] ^ w[j + 4]);
                        p32 tt2 = gg + v[7] + ss1 + w[j];

                        v[3] = v[2];
                        v[2] = hash_reference_rol(v[1], 9);
                        v[1] = v[0];
                        v[0] = tt1;
                        v[7] = v[6];
                        v[6] = hash_reference_rol(v[5], 19);
                        v[5] = v[4];
                        v[4] = tt2 ^ hash_reference_rol(tt2, 9) ^ hash_reference_rol(tt2, 17);
                }
                for (positive i = 0; i < 8; i++)
                        st[i] ^= v[i];
        }
}

static positive sm3_check_cores(p8 address_to limit)
{
        positive wrong = 0;

        for (positive blocks = 0; blocks <= 20; blocks++)
                for (positive short_by = 0; short_by < 8; short_by++)
                {
                        p8 address_to data = limit - 64 * blocks - short_by;
                        p8 before = data[-1];
                        p32 want[9], got[9], one[9];

                        for (positive at = 0; at < 64 * blocks; at++)
                                data[at] = hash_check_byte();
                        for (positive i = 0; i < 9; i++)
                                want[i] = got[i] = (p32)hash_check_byte() << 24 ^ (p32)hash_check_seed;
                        for (positive i = 0; i < 9; i++)
                                one[i] = got[i];
                        sm3_reference(want, data, blocks);
                        sm3_blocks(got, data, blocks);
                        wrong += memory_compare(want, got, sizeof want) != 0;
                        if (blocks == 1)
                        {
                                sm3_blocks_1_1(one, data);
                                wrong += memory_compare(want, one, sizeof want) != 0;
                        }
                        wrong += data[-1] != before;
                }
        return wrong;
}

static positive sm3_check_sponge(p8 address_to pattern)
{
        static const char address_to abc =
            "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0";
        static const char address_to empty =
            "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b";
        static const char address_to prefixes =
            "738abfcfd4080662e63aac398e44924cf8dc8589d057edd1d2c9ea5e31c713f1";
        static const char address_to million =
            "c8aaf89429554029e231941a2acc0ad61ff2a5acd8fadd25847a3a732b3b02c3";
        positive wrong = 0;
        p8 thousand[1000];
        checksum_sponge sponge;
        digest_state outer;
        p8 out[64], again[64];

        for (positive at = 0; at < 8192; at++)
                pattern[at] = (p8)((at * 73) ^ (at >> 3));
        memory_fill(thousand, 'a', sizeof thousand);

        checksum_sponge_open(address_of sponge, true, 32);
        checksum_sponge_close(address_of sponge, out);
        wrong += !hash_check_hex(out, 32, empty);
        checksum_sponge_open(address_of sponge, true, 32);
        checksum_sponge_write(address_of sponge, "abc", 3);
        checksum_sponge_close(address_of sponge, out);
        wrong += !hash_check_hex(out, 32, abc);
        checksum_sponge_open(address_of sponge, true, 32);
        for (positive i = 0; i < 1000; i++)
                checksum_sponge_write(address_of sponge, thousand, sizeof thousand);
        checksum_sponge_close(address_of sponge, out);
        wrong += !hash_check_hex(out, 32, million);

        digest_open(address_of outer, DIGEST_SHA256, 32);
        for (positive length = 0; length <= 300; length++)
        {
                checksum_sponge_open(address_of sponge, true, 32);
                checksum_sponge_write(address_of sponge, pattern, length);
                checksum_sponge_close(address_of sponge, out);
                digest_write(address_of outer, out, 32);

                positive first = hash_check_byte() % (length + 1);
                positive second = hash_check_byte() % (length - first + 1);

                checksum_sponge_open(address_of sponge, true, 32);
                checksum_sponge_write(address_of sponge, pattern, first);
                checksum_sponge_write(address_of sponge, pattern + first, second);
                checksum_sponge_write(address_of sponge, pattern + first + second,
                                      length - first - second);
                checksum_sponge_close(address_of sponge, again);
                wrong += memory_compare(out, again, 32) != 0;
        }
        digest_close(address_of outer, out);
        wrong += !hash_check_hex(out, 32, prefixes);
        return wrong;
}

/*
        sha512_blocks (and its sha512_blocks_avx2 body) at every alignment, which the feature walk in
        hash_check_all does not reach (it always hands the core a run that
        ends on the page): the AVX2 body with BMI2 and the floor, 0 to 9
        blocks at each of 16 byte offsets, against the textbook rounds.
*/
static positive sha512_check_aligned(p8 address_to limit)
{
        positive wrong = 0;

        for (positive blocks = 0; blocks <= 9; blocks++)
                for (positive offset = 0; offset < 16; offset++)
                {
                        p8 address_to data = limit - 128 * blocks - offset;
                        p64 want[9], got[9];

                        for (positive at = 0; at < 128 * blocks; at++)
                                data[at] = hash_check_byte();
                        for (positive i = 0; i < 9; i++)
                                want[i] = got[i] = (p64)hash_check_byte() << 56 ^ hash_check_seed;
                        hash_reference_sha512(want, data, blocks);
                        sha512_blocks(got, data, blocks);
                        wrong += memory_compare(want, got, sizeof want) != 0;
                }
        return wrong;
}

static fn keccak_check_all(p8 address_to limit)
{
        positive cores = 0;
        positive sponges = 0;
        positive bodies = 1;
        p64 scratch[25] = {0};

        keccak_blocks(scratch, limit - 8, 1, 8);
#if X64
        p8 wide = cpu_has_avx512;
#elif ARM64
        p8 wide = cpu_has_sha3;
#else
        p8 wide = 0;
#endif
        if (wide)
        {
#if X64
                cpu_has_avx512 = 0;
#elif ARM64
                cpu_has_sha3 = 0;
#endif
                bodies++;
                cores += keccak_check_cores(limit);
                sponges += keccak_check_sponge(limit - 8192);
#if X64
                cpu_has_avx512 = wide;
#elif ARM64
                cpu_has_sha3 = wide;
#endif
        }
        cores += keccak_check_cores(limit);
        sponges += keccak_check_sponge(limit - 8192);

        check("keccak_blocks ran under every body this machine has", bodies == 1 + (wide != 0));
        check("keccak_blocks agrees with the textbook permutation at every rate", cores == 0);
        check("SHA-3 gives FIPS 202's answers at every length and split", sponges == 0);

        positive sm3 = 0;
        p32 sm3_scratch[8] = {0};

        sm3_blocks(sm3_scratch, limit - 64, 1);
#if ARM64
        p8 extension = cpu_has_sm3;

        if (extension)
        {
                cpu_has_sm3 = 0;
                sm3 += sm3_check_cores(limit) + sm3_check_sponge(limit - 8192);
                cpu_has_sm3 = extension;
        }
#elif X64
        p8 bmi2 = cpu_has_mulx;

        if (bmi2)
        {
                cpu_has_mulx = 0;
                sm3 += sm3_check_cores(limit) + sm3_check_sponge(limit - 8192);
                cpu_has_mulx = bmi2;
        }
#endif
        sm3 += sm3_check_cores(limit) + sm3_check_sponge(limit - 8192);
        check("sm3_blocks agrees with the textbook rounds, and SM3 gives GB/T 32905's answers", sm3 == 0);

        positive aligned = sha512_check_aligned(limit);
#if X64
        p8 vector = cpu_has_avx2;

        cpu_has_avx2 = 0;
        aligned += sha512_check_aligned(limit);
        cpu_has_avx2 = vector;
#endif
        check("sha512_blocks agrees with the textbook rounds at every 16 byte offset", aligned == 0);
}

/*
        A program name that is empty or missing names no algorithm. An empty
        argv[0] used to match the first row with no command, sha3-224, and ran
        as that; the rows are matched by their whole name, and the name of a
        row is never empty.
*/
static void checksum_names_check(void)
{
        check("an empty program name names no checksum", checksum_algorithm_find((string_address)"", false) == null);
        check("a missing program name names no checksum", checksum_algorithm_find(null, false) == null);
        check("an empty type name names no checksum", checksum_algorithm_find((string_address)"", true) == null);
        check("md5sum still names its algorithm", checksum_algorithm_find((string_address)"md5sum", false) != null);
}

b32 main(void)
{
        positive page = system_page_size();
        positive room = ((8192 + page - 1) / page) * page;
        p8 address_to bytes = memory(room + page);
        if (!bytes || system_call_3(syscall(mprotect),
                                    (positive)(bytes + room), page, 0) < 0)
                return 1;
        bytes += room - 8192;
        for (positive at = 0; at < 8192; at++)
                bytes[at] = (p8)((at * 73) ^ (at >> 3));
        hash_check_all(bytes + 8192);
        crc_check_all(bytes);
        crc32c_check_all(bytes);
        checksum_names_check();
#if ARM64
        check("hash_crc32, hash_crc32c and hash_crc64 keep the caller's v8 to v15", crc_check_registers(bytes) == 0);
#endif
        keccak_check_all(bytes + 8192);
        return test_report(null);
}
#endif /* CHECK_checksum_crc */

#ifdef BENCH_cksum_digests
/*
        cksum's SHA-3 and SM3 cores against the C src/sh/checksum.c carried
        until keccak_blocks and sm3_blocks, copied here under former_ and
        called a block at a time as the sponge called them. Tier 1 is each
        core's floor and tier 2 its extension body (AVX-512 on x86_64, SHA3
        on arm64); a tier this machine lacks runs the floor.

        With no arguments it prints best-of-seven ticks a byte over 64 MiB,
        SHA3-256's 136-byte blocks and SHA3-512's 72, handed over 963 blocks
        a call (a 128 KiB read) and one a call. `former|assembly <tier>
        <rate> <blocks> [<MiB>]` runs one body once and prints a lane, so
        perf stat sees one body a process. `former|assembly <tier> sm3 <blocks>
        [<MiB>]` does the same for SM3, whose tier 2 is arm64's SM3 extension.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define former_rotl64(x, n) (((x) << (n)) | ((x) >> ((64 - (n)) & 63)))

static fn former_keccak(p64 address_to a)
{
        static const p64 round[24] = {
            0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
            0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
            0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
            0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
            0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
            0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
            0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
            0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
        };
        static const p8 rho[25] = {
            0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
            25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
        };

        for (positive r = 0; r < 24; r++)
        {
                p64 c[5], b[25];

                for (positive x = 0; x < 5; x++)
                        c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
                for (positive x = 0; x < 5; x++)
                {
                        p64 d = c[(x + 4) % 5] ^ former_rotl64(c[(x + 1) % 5], 1);

                        for (positive y = 0; y < 25; y += 5)
                                a[y + x] ^= d;
                }
                for (positive x = 0; x < 5; x++)
                        for (positive y = 0; y < 5; y++)
                                b[y + 5 * ((2 * x + 3 * y) % 5)] =
                                    former_rotl64(a[x + 5 * y], rho[x + 5 * y]);
                for (positive y = 0; y < 25; y += 5)
                        for (positive x = 0; x < 5; x++)
                                a[y + x] = b[y + x] ^
                                           (~b[y + (x + 1) % 5] & b[y + (x + 2) % 5]);
                a[0] ^= round[r];
        }
}

static __attribute__((noinline)) fn former_keccak_block(p64 address_to lanes,
                                                        const p8 address_to data,
                                                        positive rate)
{
        for (positive lane = 0; lane < rate / 8; lane++)
        {
                p64 value = 0;

                for (positive at = 0; at < 8; at++)
                        value |= (p64)data[lane * 8 + at] << (8 * at);
                lanes[lane] ^= value;
        }
        former_keccak(lanes);
}

#define former_rotl32(x, n) (((x) << (n)) | ((x) >> ((32 - (n)) & 31)))

static __attribute__((noinline)) fn former_sm3_block(p32 address_to v, const p8 address_to data)
{
        p32 w[68];

        for (positive j = 0; j < 16; j++)
                w[j] = (p32)data[4 * j] << 24 | (p32)data[4 * j + 1] << 16 |
                       (p32)data[4 * j + 2] << 8 | data[4 * j + 3];
        for (positive j = 16; j < 68; j++)
        {
                p32 x = w[j - 16] ^ w[j - 9] ^ former_rotl32(w[j - 3], 15);

                w[j] = (x ^ former_rotl32(x, 15) ^ former_rotl32(x, 23)) ^
                       former_rotl32(w[j - 13], 7) ^ w[j - 6];
        }

        p32 a = v[0], b = v[1], c = v[2], d = v[3];
        p32 e = v[4], f = v[5], g = v[6], h = v[7];

        for (positive j = 0; j < 64; j++)
        {
                p32 t = j < 16 ? 0x79cc4519u : 0x7a879d8au;
                p32 a12 = former_rotl32(a, 12);
                p32 ss1 = former_rotl32(a12 + e + former_rotl32(t, j % 32), 7);
                p32 ss2 = ss1 ^ a12;
                p32 ff = j < 16 ? a ^ b ^ c : (a & b) | (a & c) | (b & c);
                p32 gg = j < 16 ? e ^ f ^ g : (e & f) | (~e & g);
                p32 tt1 = ff + d + ss2 + (w[j] ^ w[j + 4]);
                p32 tt2 = gg + h + ss1 + w[j];

                d = c;
                c = former_rotl32(b, 9);
                b = a;
                a = tt1;
                h = g;
                g = former_rotl32(f, 19);
                f = e;
                e = tt2 ^ former_rotl32(tt2, 9) ^ former_rotl32(tt2, 17);
        }

        v[0] ^= a; v[1] ^= b; v[2] ^= c; v[3] ^= d;
        v[4] ^= e; v[5] ^= f; v[6] ^= g; v[7] ^= h;
}

#define DIGESTS_BENCH_BLOCKS 963

static p8 digests_bench_data[DIGESTS_BENCH_BLOCKS * 168] __attribute__((aligned(64)));
static p64 digests_bench_lanes[25];
static positive digests_bench_rate = 136;
static positive digests_bench_per = DIGESTS_BENCH_BLOCKS;
static positive digests_bench_total = 64u << 20;
static fn (*volatile digests_bench_keccak)(p64 address_to, const p8 address_to,
                                           positive, positive) = keccak_blocks;
static p8 digests_bench_wide;
static p8 digests_bench_sm3_extension;

static fn digests_bench_former_keccak(void)
{
        positive block_bytes = digests_bench_rate * digests_bench_per;

        for (positive done = 0; done < digests_bench_total; done += block_bytes)
                for (positive block = 0; block < digests_bench_per; block++)
                        former_keccak_block(digests_bench_lanes,
                                            digests_bench_data + block * digests_bench_rate,
                                            digests_bench_rate);
}

static fn digests_bench_assembly_keccak(void)
{
        positive block_bytes = digests_bench_rate * digests_bench_per;

        for (positive done = 0; done < digests_bench_total; done += block_bytes)
                digests_bench_keccak(digests_bench_lanes, digests_bench_data,
                                     digests_bench_per, digests_bench_rate);
}

static fn digests_bench_tier(positive tier)
{
#if X64
        cpu_has_avx512 = tier >= 2 ? digests_bench_wide : 0;
#elif ARM64
        cpu_has_sha3 = tier >= 2 ? digests_bench_wide : 0;
        cpu_has_sm3 = tier >= 2 ? digests_bench_sm3_extension : 0;
#else
        (void)tier;
#endif
}

static p32 digests_bench_words[8];
static fn (*volatile digests_bench_sm3)(p32 address_to, const p8 address_to,
                                        positive) = sm3_blocks;

static fn digests_bench_former_sm3(void)
{
        for (positive done = 0; done < digests_bench_total; done += 64 * digests_bench_per)
                for (positive block = 0; block < digests_bench_per; block++)
                        former_sm3_block(digests_bench_words, digests_bench_data + block * 64);
}

static fn digests_bench_assembly_sm3(void)
{
        for (positive done = 0; done < digests_bench_total; done += 64 * digests_bench_per)
                digests_bench_sm3(digests_bench_words, digests_bench_data, digests_bench_per);
}

static positive digests_bench_number(string_address text, positive otherwise)
{
        positive value = 0;

        if (!text)
                return otherwise;
        while (*text >= '0' && *text <= '9')
                value = value * 10 + (positive)(*text++ - '0');
        return value;
}

b32 main(void)
{
        p32 random = 0x7433291u;

        for (positive at = 0; at < sizeof(digests_bench_data); at++)
        {
                XORSHIFT32(random);
                digests_bench_data[at] = (p8)random;
        }
        keccak_blocks(digests_bench_lanes, digests_bench_data, 1, 136);
#if X64
        digests_bench_wide = cpu_has_avx512;
#elif ARM64
        digests_bench_wide = cpu_has_sha3;
        digests_bench_sm3_extension = cpu_has_sm3;
#endif

        string_address body = program_argument(1);

        if (body)
        {
                digests_bench_tier(digests_bench_number(program_argument(2), 2));
                if (program_argument(3) && string_equals(program_argument(3), "sm3"))
                {
                        digests_bench_per = digests_bench_number(program_argument(4), DIGESTS_BENCH_BLOCKS);
                        digests_bench_total = digests_bench_number(program_argument(5), 64) << 20;
                        if (!digests_bench_per || digests_bench_per > DIGESTS_BENCH_BLOCKS)
                                return 2;
                        if (string_equals(body, "former"))
                                digests_bench_former_sm3();
                        else if (string_equals(body, "assembly"))
                                digests_bench_assembly_sm3();
                        else
                                return 2;
                        string_format(log, "%p\n", (positive)digests_bench_words[0]);
                        return 0;
                }
                digests_bench_rate = digests_bench_number(program_argument(3), 136);
                digests_bench_per = digests_bench_number(program_argument(4), DIGESTS_BENCH_BLOCKS);
                digests_bench_total = digests_bench_number(program_argument(5), 64) << 20;
                if (!digests_bench_rate || digests_bench_rate > 168 || digests_bench_rate % 8 ||
                    !digests_bench_per || digests_bench_per > DIGESTS_BENCH_BLOCKS)
                        return 2;
                if (string_equals(body, "none"))
                        return 0;
                if (string_equals(body, "former"))
                        digests_bench_former_keccak();
                else if (string_equals(body, "assembly"))
                        digests_bench_assembly_keccak();
                else
                        return 2;
                string_format(log, "%p\n", (positive)digests_bench_lanes[0]);
                return 0;
        }

        static const positive rates[] = {136, 72};
        static const positive pers[] = {DIGESTS_BENCH_BLOCKS, 1};
        p8 agree = true;

        for (positive which = 0; which < array_count(rates); which++)
                for (positive per = 0; per < array_count(pers); per++)
                {
                        digests_bench_rate = rates[which];
                        digests_bench_per = pers[per];
                        string_format(log, "  keccak, %p-byte blocks, %p a call:\n",
                                      digests_bench_rate, digests_bench_per);
                        memory_fill(digests_bench_lanes, 0, sizeof digests_bench_lanes);
                        bench_report("former C", digests_bench_former_keccak, 7,
                                     digests_bench_total, "byte");
                        for (positive tier = 1; tier <= 2; tier++)
                        {
                                p64 kept[25];

                                memory_copy(kept, digests_bench_lanes, sizeof kept);
                                digests_bench_tier(tier);
                                bench_report(tier == 1 ? "tier 1 keccak_blocks"
                                                       : "tier 2 keccak_blocks",
                                             digests_bench_assembly_keccak, 7,
                                             digests_bench_total, "byte");
                                memory_copy(digests_bench_lanes, kept, sizeof kept);
                                digests_bench_former_keccak();
                                p64 former = digests_bench_lanes[0];
                                memory_copy(digests_bench_lanes, kept, sizeof kept);
                                digests_bench_assembly_keccak();
                                agree = agree && former == digests_bench_lanes[0];
                        }
                }
        for (positive per = 0; per < 2; per++)
        {
                digests_bench_per = per ? 1 : DIGESTS_BENCH_BLOCKS;
                string_format(log, "  sm3, %p a call:\n", digests_bench_per);
                memory_fill(digests_bench_words, 0, sizeof digests_bench_words);
                bench_report("former C", digests_bench_former_sm3, 7, digests_bench_total, "byte");
                for (positive tier = 1; tier <= 2; tier++)
                {
                        p32 kept[8];

                        memory_copy(kept, digests_bench_words, sizeof kept);
                        digests_bench_tier(tier);
                        bench_report(tier == 1 ? "tier 1 sm3_blocks" : "tier 2 sm3_blocks",
                                     digests_bench_assembly_sm3, 7, digests_bench_total, "byte");
                        memory_copy(digests_bench_words, kept, sizeof kept);
                        digests_bench_former_sm3();
                        p32 former = digests_bench_words[0];
                        memory_copy(digests_bench_words, kept, sizeof kept);
                        digests_bench_assembly_sm3();
                        agree = agree && former == digests_bench_words[0];
                }
        }
        digests_bench_tier(2);
        if (!agree)
                string_format(log, "  the former C and the assembly disagree\n");
        return agree ? 0 : 1;
}
#endif /* BENCH_cksum_digests */

#ifdef BENCH_cksum_crc
/*
        cksum's CRC over its own read blocks, tier for tier: the C that
        src/sh/checksum.c carried until hash_crc32_msb -- slice tables built at
        run time, four table streams joined by GF(2) shifts, SSE and
        AVX-512 folds 64 bytes a turn -- copied here unchanged under a
        former_ prefix, against the assembly with its feature bytes written
        down to the same tier. Tier 1 is the table, 2 the PCLMULQDQ folds
        (PMULL on arm64), 3 the VPCLMULQDQ zmm folds; a tier this machine
        lacks runs the best one below it.

        With no arguments it prints best-of-seven ticks a byte over 64 MiB
        in 4 KiB and 128 KiB blocks. `former|assembly <tier> <block>
        [<MiB>]` runs one body once and prints its crc, so perf stat or an
        emulator's instruction count sees one body a process; the former
        body pays its table preparation as the applet did. `none` exits
        after the block is filled, for the fixed cost.

        Then CRC-32C the way the ext4 format asks for it: a block group's
        4 KiB bitmap, its number and its 32-byte descriptor, and a reserved
        inode's two seed words and 256 bytes -- the loop storage.c carried
        until hash_crc32c, copied under former_crc32c, against the assembly
        tier by tier (1 the table, 2 the crc32 instruction or Zbc, 3 three
        streams joined by PCLMULQDQ or PMULL). `former32c|assembly32c <tier>
        groups|inodes [<calls>]` runs one body once for perf stat.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define FORMER_CRC_POLYNOMIAL 0x04c11db7u
#define FORMER_CRC_SLICES 8

static p32 former_crc_table[FORMER_CRC_SLICES][256];
static p32 former_crc_shift_power[positive_bits];
static p8 former_crc_table_ready;
#if X64
/* 0 is unprobed, 1 is unavailable, 2 is PCLMUL, 3 is VPCLMUL. A shell should
   not serialize itself with CPUID again on every cksum call on an older CPU. */
static p8 former_crc_pclmul_state;
#endif

/* Multiplication in the same GF(2) field as the byte recurrence. It is cold:
   table preparation uses it once, and a full shared input block needs only
   three combines. */
static p32 former_crc_multiply(p32 left, p32 right)
{
        p32 result = 0;

        for (positive bit = 0; bit < 32; bit++)
        {
                if (right & 1)
                        result ^= left;

                right >>= 1;
                left = (left << 1) ^
                       ((left & 0x80000000u) ? FORMER_CRC_POLYNOMIAL : 0);
        }

        return result;
}

static fn former_crc_prepare()
{
        if (former_crc_table_ready)
                return;

        for (positive value = 0; value < 256; value++)
        {
                p32 crc = (p32)value << 24;

                for (positive bit = 0; bit < 8; bit++)
                        crc = (crc << 1) ^
                              ((crc & 0x80000000u) ? FORMER_CRC_POLYNOMIAL : 0);

                former_crc_table[0][value] = crc;
        }

        for (positive slice = 1; slice < FORMER_CRC_SLICES; slice++)
                for (positive value = 0; value < 256; value++)
                {
                        p32 crc = former_crc_table[slice - 1][value];

                        former_crc_table[slice][value] =
                            (crc << 8) ^ former_crc_table[0][crc >> 24];
                }

        former_crc_shift_power[0] = 0x100;
        for (positive bit = 1; bit < positive_bits; bit++)
                former_crc_shift_power[bit] = former_crc_multiply(
                    former_crc_shift_power[bit - 1],
                    former_crc_shift_power[bit - 1]);

        former_crc_table_ready = true;
}

static inline INLINE p32 former_crc_word(p64 word, p32 crc)
{
        return former_crc_table[7][((crc >> 24) ^ word) & 255] ^
               former_crc_table[6][((crc >> 16) ^ (word >> 8)) & 255] ^
               former_crc_table[5][((crc >> 8) ^ (word >> 16)) & 255] ^
               former_crc_table[4][(crc ^ (word >> 24)) & 255] ^
               former_crc_table[3][(word >> 32) & 255] ^
               former_crc_table[2][(word >> 40) & 255] ^
               former_crc_table[1][(word >> 48) & 255] ^
               former_crc_table[0][word >> 56];
}

static p32 former_crc_serial(p8 address_to bytes, positive length, p32 crc)
{
        while (length >= 8)
        {
                p64 word = memory_load_unaligned(p64, bytes);

                crc = former_crc_word(word, crc);
                bytes += 8;
                length -= 8;
        }

        while (length--)
                crc = (crc << 8) ^
                      former_crc_table[0][(crc >> 24) ^ *bytes++];

        return crc;
}

#if X64
/* SSE's carry-less multiply evaluates the same polynomial 128 bytes at a
   time. The constants are x^128 and x^512 reduced modulo 0x104c11db7, from
   Intel's generic-polynomial CRC construction. SSSE3 reverses each vector
   because POSIX CRC is the non-reflected, most-significant-bit-first form.
   The table floor remains authoritative for the final folded vector and for
   every processor without both instructions. */
typedef p64 former_crc_vector
    __attribute__((vector_size(16), aligned(1), may_alias));
typedef long long former_crc_vector_signed __attribute__((vector_size(16)));
typedef char former_crc_bytes_signed __attribute__((vector_size(16)));

static p8 former_crc_hardware()
{
        p32 leaf = 1;
        p32 ebx;
        p32 features = 0;
        p32 edx;

        __asm__ volatile("cpuid"
                         : "+a"(leaf), "=b"(ebx), "+c"(features), "=d"(edx));
        (void)ebx;
        (void)edx;
        if (!(features & ((p32)1 << 1)) || !(features & ((p32)1 << 9)))
                return 1;
        if (cpu_has_avx512)
        {
                leaf = 7;
                features = 0;
                __asm__ volatile("cpuid"
                                 : "+a"(leaf), "=b"(ebx), "+c"(features),
                                   "=d"(edx));
                if (features & ((p32)1 << 10))
                        return 3;
        }
        return 2;
}

static __attribute__((target("pclmul,ssse3"))) former_crc_vector
former_crc_reverse(former_crc_vector value)
{
        const former_crc_vector mask = {
            0x08090a0b0c0d0e0full, 0x0001020304050607ull};

        return (former_crc_vector)__builtin_ia32_pshufb128(
            (former_crc_bytes_signed)value,
            (former_crc_bytes_signed)mask);
}

static __attribute__((target("pclmul,ssse3"))) former_crc_vector
former_crc_fold(former_crc_vector value, former_crc_vector constant,
               former_crc_vector following)
{
        former_crc_vector low =
            (former_crc_vector)__builtin_ia32_pclmulqdq128(
                (former_crc_vector_signed)value,
                (former_crc_vector_signed)constant, 0x00);
        former_crc_vector high =
            (former_crc_vector)__builtin_ia32_pclmulqdq128(
                (former_crc_vector_signed)value,
                (former_crc_vector_signed)constant, 0x11);

        return low ^ high ^ following;
}

static __attribute__((target("pclmul,ssse3"))) p32
former_crc_pclmul(p8 address_to bytes, positive length, p32 crc)
{
        const former_crc_vector four = {0xe6228b11ull, 0x8833794cull};
        const former_crc_vector one = {0xe8a45605ull, 0xc5b9cd4cull};
        former_crc_vector first = former_crc_reverse(
            *(former_crc_vector address_to)(bytes));
        former_crc_vector second = former_crc_reverse(
            *(former_crc_vector address_to)(bytes + 16));
        former_crc_vector third = former_crc_reverse(
            *(former_crc_vector address_to)(bytes + 32));
        former_crc_vector fourth = former_crc_reverse(
            *(former_crc_vector address_to)(bytes + 48));
        const former_crc_vector initial = {0, (p64)crc << 32};

        first ^= initial;
        bytes += 64;
        length -= 64;

        while (length >= 64)
        {
                first = former_crc_fold(
                    first, four,
                    former_crc_reverse(
                        *(former_crc_vector address_to)(bytes)));
                second = former_crc_fold(
                    second, four,
                    former_crc_reverse(
                        *(former_crc_vector address_to)(bytes + 16)));
                third = former_crc_fold(
                    third, four,
                    former_crc_reverse(
                        *(former_crc_vector address_to)(bytes + 32)));
                fourth = former_crc_fold(
                    fourth, four,
                    former_crc_reverse(
                        *(former_crc_vector address_to)(bytes + 48)));
                bytes += 64;
                length -= 64;
        }

        first = former_crc_fold(first, one, second);
        first = former_crc_fold(first, one, third);
        first = former_crc_fold(first, one, fourth);
        first = former_crc_reverse(first);
        crc = former_crc_serial((p8 address_to)address_of first, 16, 0);
        return former_crc_serial(bytes, length, crc);
}

/* The same four polynomial chains as the SSE path, packed into one ZMM.
   Only the lane width changes; final reduction stays in the shared floor.
   Dispatch requires both OS-enabled AVX-512 and the separate VPCLMUL bit. */
typedef p64 former_crc_wide
    __attribute__((vector_size(64), aligned(1), may_alias));
typedef long long former_crc_wide_signed __attribute__((vector_size(64)));
typedef char former_crc_wide_bytes __attribute__((vector_size(64)));

static __attribute__((target("avx512f,avx512bw,vpclmulqdq,pclmul,ssse3"))) p32
former_crc_vpclmul(p8 address_to bytes, positive length, p32 crc)
{
        const former_crc_wide four = {
            0xe6228b11ull, 0x8833794cull, 0xe6228b11ull, 0x8833794cull,
            0xe6228b11ull, 0x8833794cull, 0xe6228b11ull, 0x8833794cull};
        const former_crc_wide mask = {
            0x08090a0b0c0d0e0full, 0x0001020304050607ull,
            0x08090a0b0c0d0e0full, 0x0001020304050607ull,
            0x08090a0b0c0d0e0full, 0x0001020304050607ull,
            0x08090a0b0c0d0e0full, 0x0001020304050607ull};
        former_crc_wide value = {0, (p64)crc << 32, 0, 0, 0, 0, 0, 0};
        p8 first = true;

        do
        {
                former_crc_wide next = (former_crc_wide)
                    __builtin_ia32_pshufb512_mask(
                        (former_crc_wide_bytes)*(former_crc_wide address_to)bytes,
                        (former_crc_wide_bytes)mask,
                        (former_crc_wide_bytes){0}, (p64)-1);
                if (!first)
                {
                        former_crc_wide low = (former_crc_wide)
                            __builtin_ia32_vpclmulqdq_v8di(
                                (former_crc_wide_signed)value,
                                (former_crc_wide_signed)four, 0x00);
                        former_crc_wide high = (former_crc_wide)
                            __builtin_ia32_vpclmulqdq_v8di(
                                (former_crc_wide_signed)value,
                                (former_crc_wide_signed)four, 0x11);
                        value = low ^ high;
                }
                value ^= next;
                first = false;
                bytes += 64;
                length -= 64;
        } while (length >= 64);

        const former_crc_vector one = {0xe8a45605ull, 0xc5b9cd4cull};
        former_crc_vector folded = {value[0], value[1]};
        folded = former_crc_fold(folded, one,
                                (former_crc_vector){value[2], value[3]});
        folded = former_crc_fold(folded, one,
                                (former_crc_vector){value[4], value[5]});
        folded = former_crc_fold(folded, one,
                                (former_crc_vector){value[6], value[7]});
        folded = former_crc_reverse(folded);
        crc = former_crc_serial((p8 address_to)address_of folded, 16, 0);
        return former_crc_serial(bytes, length, crc);
}
#endif

static p32 former_crc_shift(p32 crc, p64 bytes)
{
        positive bit = 0;

        while (bytes)
        {
                if (bytes & 1)
                        crc = former_crc_multiply(
                            crc, former_crc_shift_power[bit]);

                bytes >>= 1;
                bit++;
        }

        return crc;
}

static HOT __attribute__((noinline)) p32 former_crc_block(
    p8 address_to bytes, positive length, p32 crc)
{
#if X64
        if (length >= 128 && former_crc_pclmul_state == 3)
                return former_crc_vpclmul(bytes, length, crc);
        if (length >= 128 && former_crc_pclmul_state == 2)
                return former_crc_pclmul(bytes, length, crc);
#endif

        positive span = (length >> 2) & ~(positive)7;

        if (span < 256)
                return former_crc_serial(bytes, length, crc);

        p8 address_to second = bytes + span;
        p8 address_to third = second + span;
        p8 address_to fourth = third + span;
        p32 first_crc = crc;
        p32 second_crc = 0;
        p32 third_crc = 0;
        p32 fourth_crc = 0;

        for (positive at = 0; at < span; at += 8)
        {
                first_crc = former_crc_word(
                    memory_load_unaligned(p64, bytes + at), first_crc);
                second_crc = former_crc_word(
                    memory_load_unaligned(p64, second + at), second_crc);
                third_crc = former_crc_word(
                    memory_load_unaligned(p64, third + at), third_crc);
                fourth_crc = former_crc_word(
                    memory_load_unaligned(p64, fourth + at), fourth_crc);
        }

        positive fourth_length = length - span * 3;
        fourth_crc = former_crc_serial(fourth + span,
                                      fourth_length - span, fourth_crc);
        crc = former_crc_shift(first_crc, span) ^ second_crc;
        crc = former_crc_shift(crc, span) ^ third_crc;
        return former_crc_shift(crc, fourth_length) ^ fourth_crc;
}

#define CKSUM_BENCH_LARGEST (128u << 10)

static p8 cksum_bench_block[CKSUM_BENCH_LARGEST] __attribute__((aligned(64)));
static positive cksum_bench_size;
static positive cksum_bench_total = 64u << 20;
static volatile p32 cksum_bench_sink;
static p32 (*volatile cksum_bench_call)(p32, address_any, positive) = hash_crc32_msb;
static p8 cksum_bench_pclmul;
#if X64
static p8 cksum_bench_vpclmul;
static p8 cksum_bench_avx512;
#endif

static fn cksum_bench_former(void)
{
        p32 crc = 0;

        for (positive done = 0; done < cksum_bench_total; done += cksum_bench_size)
                crc = former_crc_block(cksum_bench_block, cksum_bench_size, crc);
        cksum_bench_sink = crc;
}

static fn cksum_bench_assembly(void)
{
        p32 crc = 0;

        for (positive done = 0; done < cksum_bench_total; done += cksum_bench_size)
                crc = cksum_bench_call(crc, cksum_bench_block, cksum_bench_size);
        cksum_bench_sink = crc;
}

static fn cksum_bench_tier(positive tier, p8 former)
{
        cpu_has_pclmul = tier >= 2 ? cksum_bench_pclmul : 0;
#if X64
        cpu_has_vpclmul = tier >= 3 ? cksum_bench_vpclmul : 0;
        cpu_has_avx512 = tier >= 3 ? cksum_bench_avx512 : 0;
        if (former)
        {
                p8 hardware = former_crc_hardware();

                former_crc_pclmul_state = (p8)(tier < hardware ? tier : hardware);
        }
#else
        (void)former;
#endif
}

static positive cksum_bench_number(string_address text, positive otherwise)
{
        positive value = 0;

        if (!text)
                return otherwise;
        while (*text >= '0' && *text <= '9')
                value = value * 10 + (positive)(*text++ - '0');
        return value;
}

static p32 former_crc32c_table[256];

static __attribute__((noinline)) p32 former_crc32c(p32 crc, p8 address_to bytes,
                                                  positive length)
{
        if (!former_crc32c_table[128])
                for (positive at = 0; at < 256; at++)
                {
                        p32 value = (p32)at;

                        for (positive bit = 0; bit < 8; bit++)
                                value = value & 1 ? (value >> 1) ^ 0x82F63B78
                                                  : value >> 1;

                        former_crc32c_table[at] = value;
                }

        while (length--)
                crc = former_crc32c_table[(crc ^ *bytes++) & 0xff] ^ (crc >> 8);

        return crc;
}

static p32 (*volatile crc32c_bench_call)(p32, address_any, positive) = hash_crc32c;
static const positive crc32c_bench_groups[] = {4096, 4, 32};
static const positive crc32c_bench_inodes[] = {4, 4, 256};
static const positive address_to crc32c_bench_shape = crc32c_bench_groups;
static positive crc32c_bench_calls = 3u << 14;
static p8 crc32c_bench_instruction;

static fn crc32c_bench_former(void)
{
        p32 crc = ~(p32)0;

        for (positive call = 0; call < crc32c_bench_calls; call++)
                crc = former_crc32c(crc, cksum_bench_block + (call & 7) * 4096,
                                    crc32c_bench_shape[call % 3]);
        cksum_bench_sink = crc;
}

static fn crc32c_bench_assembly(void)
{
        p32 crc = ~(p32)0;

        for (positive call = 0; call < crc32c_bench_calls; call++)
                crc = crc32c_bench_call(crc, cksum_bench_block + (call & 7) * 4096,
                                        crc32c_bench_shape[call % 3]);
        cksum_bench_sink = crc;
}

static fn crc32c_bench_tier(positive tier)
{
#if RISCV64
        cpu_has_pclmul = tier >= 2 ? cksum_bench_pclmul : 0;
#else
        cpu_has_pclmul = tier >= 3 ? cksum_bench_pclmul : 0;
#endif
#if X64
        cpu_has_sse42 = tier >= 2 ? crc32c_bench_instruction : 0;
#elif ARM64
        cpu_has_crc32 = tier >= 2 ? crc32c_bench_instruction : 0;
#endif
}

static positive crc32c_bench_bytes(void)
{
        positive bytes = 0;

        for (positive call = 0; call < crc32c_bench_calls; call++)
                bytes += crc32c_bench_shape[call % 3];
        return bytes;
}

b32 main(void)
{
        p32 random = 0x7433291u;

        for (positive at = 0; at < sizeof(cksum_bench_block); at++)
        {
                XORSHIFT32(random);
                cksum_bench_block[at] = (p8)random;
        }

        cksum_bench_pclmul = cpu_has_pclmul;
#if X64
        cksum_bench_vpclmul = cpu_has_vpclmul;
        cksum_bench_avx512 = cpu_has_avx512;
#endif
        cksum_bench_sink = hash_crc32c(0, cksum_bench_block, 1);
#if X64
        crc32c_bench_instruction = cpu_has_sse42;
#elif ARM64
        crc32c_bench_instruction = cpu_has_crc32;
#endif

        string_address body = program_argument(1);

        if (body)
        {
                positive tier = cksum_bench_number(program_argument(2), 3);

                if (string_equals(body, "former32c") || string_equals(body, "assembly32c"))
                {
                        string_address shape = program_argument(3);

                        if (shape && string_equals(shape, "inodes"))
                                crc32c_bench_shape = crc32c_bench_inodes;
                        crc32c_bench_calls = cksum_bench_number(program_argument(4), 3u << 20);
                        crc32c_bench_tier(tier);
                        if (string_equals(body, "former32c"))
                                crc32c_bench_former();
                        else
                                crc32c_bench_assembly();
                        string_format(log, "%p\n", (positive)cksum_bench_sink);
                        return 0;
                }
                cksum_bench_size = cksum_bench_number(program_argument(3), 4096);
                cksum_bench_total = cksum_bench_number(program_argument(4), 64) << 20;
                if (!cksum_bench_size || cksum_bench_size > CKSUM_BENCH_LARGEST)
                        return 2;
                if (string_equals(body, "none"))
                        return 0;

                if (string_equals(body, "former"))
                {
                        former_crc_prepare();
                        cksum_bench_tier(tier, true);
                        cksum_bench_former();
                }
                else if (string_equals(body, "assembly"))
                {
                        cksum_bench_tier(tier, false);
                        cksum_bench_assembly();
                }
                else
                        return 2;
                string_format(log, "%p\n", (positive)cksum_bench_sink);
                return 0;
        }

#if X64
        positive tiers = 3;
#elif ARM64
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        static const positive sizes[] = {4096, CKSUM_BENCH_LARGEST};
        static const string_address former_names[] = {
            "tier 1 former C", "tier 2 former C", "tier 3 former C"};
        static const string_address assembly_names[] = {
            "tier 1 assembly", "tier 2 assembly", "tier 3 assembly"};
        p8 agree = true;

        former_crc_prepare();
        for (positive which = 0; which < array_count(sizes); which++)
        {
                cksum_bench_size = sizes[which];
                string_format(log, "  64 MiB in %p-byte blocks:\n", cksum_bench_size);
                for (positive tier = 1; tier <= tiers; tier++)
                {
                        cksum_bench_tier(tier, true);
                        bench_report(former_names[tier - 1], cksum_bench_former, 7,
                                     cksum_bench_total, "byte");
                        p32 former = cksum_bench_sink;

                        bench_report(assembly_names[tier - 1], cksum_bench_assembly, 7,
                                     cksum_bench_total, "byte");
                        agree = agree && former == cksum_bench_sink;
                }
        }
        cksum_bench_tier(3, false);

        static const string_address shapes[] = {"groups", "inodes"};
        static const string_address former32c_names[] = {
            "tier 1 former C", "tier 2 former C", "tier 3 former C"};
        static const string_address assembly32c_names[] = {
            "tier 1 hash_crc32c", "tier 2 hash_crc32c", "tier 3 hash_crc32c"};
#if RISCV64
        positive crc32c_tiers = 2;
#else
        positive crc32c_tiers = 3;
#endif

        for (positive which = 0; which < 2; which++)
        {
                crc32c_bench_shape = which ? crc32c_bench_inodes : crc32c_bench_groups;
                crc32c_bench_calls = 3u << 14;
                string_format(log, "  CRC-32C over ext4 %s calls:\n", shapes[which]);
                for (positive tier = 1; tier <= crc32c_tiers; tier++)
                {
                        crc32c_bench_tier(tier);
                        bench_report(former32c_names[tier - 1], crc32c_bench_former, 7,
                                     crc32c_bench_bytes(), "byte");
                        p32 former = cksum_bench_sink;

                        bench_report(assembly32c_names[tier - 1], crc32c_bench_assembly, 7,
                                     crc32c_bench_bytes(), "byte");
                        agree = agree && former == cksum_bench_sink;
                }
        }
        crc32c_bench_tier(3);
        if (!agree)
                string_format(log, "  the former C and the assembly disagree\n");
        return agree ? 0 : 1;
}
#endif /* BENCH_cksum_crc */

