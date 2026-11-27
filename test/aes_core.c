/*
        Proof & security test suite for src/net/aes_core.asm.c:
          - All 256 AES S-box & InvS-box values + round-trip bijectivity
          - Official FIPS-197 Appendix B & Appendix C.1 AES-128 vectors
          - Official RFC 3394 Section 4.1 (128-bit KEK, 128-bit Key Data) Key Unwrap
          - Official RFC 5297 / RFC 4493 SIV GF(2^128) doubling vectors
          - Constant-time ct_equal: single-bit flip at every byte offset 0..len-1
          - NULL / 0-length safety + in-place (in == out) decrypt safety
          - Randomized differential fuzzing vs -O2 C reference (original net.c)
          - mmap PROT_NONE guard-page tests (before & after every buffer)
          - Speed benchmark: ASM vs -O2 C reference
*/
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define NET_AES_CORE_ASM_IMPLEMENTATION
#include "../src/net/aes_core.asm.c"

/* ---------- Exact -O2 C reference (original src/net/net.c) ---------- */
#define NOINLINE __attribute__((noinline))

NOINLINE static uint8_t ref_gf_mul(uint8_t left, uint8_t right)
{
        uint8_t product = 0;
        for (size_t bit = 0; bit < 8; bit++) {
                uint8_t selected = (uint8_t)(0 - (right & 1));
                uint8_t high = left >> 7;
                product ^= left & selected;
                left = (uint8_t)((left << 1) ^ (0x1b & (uint8_t)(0 - high)));
                right >>= 1;
        }
        return product;
}

NOINLINE static uint8_t ref_gf_inv(uint8_t value)
{
        uint8_t x2 = ref_gf_mul(value, value);
        uint8_t x3 = ref_gf_mul(x2, value);
        uint8_t x6 = ref_gf_mul(x3, x3);
        uint8_t x12 = ref_gf_mul(x6, x6);
        uint8_t x15 = ref_gf_mul(x12, x3);
        uint8_t x30 = ref_gf_mul(x15, x15);
        uint8_t x60 = ref_gf_mul(x30, x30);
        uint8_t x120 = ref_gf_mul(x60, x60);
        uint8_t x240 = ref_gf_mul(x120, x120);
        return ref_gf_mul(ref_gf_mul(x240, x12), x2);
}

NOINLINE uint8_t ref_aes_sbox_byte(uint8_t value)
{
        uint8_t inv = ref_gf_inv(value);
        return (uint8_t)(inv ^
                         ((inv << 1) | (inv >> 7)) ^
                         ((inv << 2) | (inv >> 6)) ^
                         ((inv << 3) | (inv >> 5)) ^
                         ((inv << 4) | (inv >> 4)) ^ 0x63);
}

NOINLINE uint8_t ref_aes_inv_sbox_byte(uint8_t value)
{
        return ref_gf_inv((uint8_t)(((value << 1) | (value >> 7)) ^
                                    ((value << 3) | (value >> 5)) ^
                                    ((value << 6) | (value >> 2)) ^ 0x05));
}

NOINLINE void ref_aes128_expand(const uint8_t *key, uint8_t *round)
{
        static const uint8_t rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                         0x20, 0x40, 0x80, 0x1b, 0x36};
        memcpy(round, key, 16);
        for (size_t i = 16; i < 176; i += 4) {
                uint8_t t0 = round[i - 4], t1 = round[i - 3];
                uint8_t t2 = round[i - 2], t3 = round[i - 1];
                if (i % 16 == 0) {
                        uint8_t k = t0;
                        t0 = ref_aes_sbox_byte(t1) ^ rcon[i / 16 - 1];
                        t1 = ref_aes_sbox_byte(t2);
                        t2 = ref_aes_sbox_byte(t3);
                        t3 = ref_aes_sbox_byte(k);
                }
                round[i] = round[i - 16] ^ t0;
                round[i + 1] = round[i - 15] ^ t1;
                round[i + 2] = round[i - 14] ^ t2;
                round[i + 3] = round[i - 13] ^ t3;
        }
}

NOINLINE static uint8_t ref_aes_unmix(const uint8_t *column, size_t row)
{
        static const uint8_t factor[4] = {0x0e, 0x0b, 0x0d, 0x09};
        uint8_t mixed = 0;
        for (size_t at = 0; at < 4; at++)
                mixed ^= ref_gf_mul(column[at], factor[(4 + at - row) & 3]);
        return mixed;
}

NOINLINE void ref_aes128_decrypt(const uint8_t *round, const uint8_t *in, uint8_t *out)
{
        uint8_t state[16], hold[16];
        memcpy(state, in, 16);
        for (size_t at = 0; at < 16; at++) state[at] ^= round[160 + at];
        for (size_t step = 9;; step--) {
                for (size_t row = 1; row < 4; row++)
                        for (size_t turn = 0; turn < row; turn++) {
                                uint8_t temp = state[row + 12];
                                state[row + 12] = state[row + 8];
                                state[row + 8] = state[row + 4];
                                state[row + 4] = state[row];
                                state[row] = temp;
                        }
                for (size_t at = 0; at < 16; at++)
                        state[at] = ref_aes_inv_sbox_byte(state[at]) ^ round[step * 16 + at];
                if (!step) break;
                memcpy(hold, state, 16);
                for (size_t col = 0; col < 4; col++)
                        for (size_t row = 0; row < 4; row++)
                                state[col * 4 + row] = ref_aes_unmix(hold + col * 4, row);
        }
        memcpy(out, state, 16);
}

NOINLINE void ref_siv_double(uint8_t *block)
{
        uint8_t carry = (uint8_t)(block[0] >> 7);
        for (size_t at = 0; at < 15; at++)
                block[at] = (uint8_t)((block[at] << 1) | (block[at + 1] >> 7));
        block[15] = (uint8_t)((block[15] << 1) ^ (0x87 & (uint8_t)(0 - carry)));
}

NOINLINE int ref_ct_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
        uint8_t diff = 0;
        for (size_t at = 0; at < length; at++) diff |= (uint8_t)(left[at] ^ right[at]);
        return !diff;
}

/* Forward AES-128 encrypt for round-trip verification */
static void ref_aes128_encrypt(const uint8_t *round, const uint8_t *in, uint8_t *out)
{
        uint8_t s[16], h[16];
        memcpy(s, in, 16);
        for (size_t i = 0; i < 16; i++) s[i] ^= round[i];
        for (size_t step = 1; step <= 10; step++) {
                for (size_t i = 0; i < 16; i++) s[i] = ref_aes_sbox_byte(s[i]);
                for (size_t row = 1; row < 4; row++)
                        for (size_t t = 0; t < row; t++) {
                                uint8_t tmp = s[row];
                                s[row] = s[row + 4]; s[row + 4] = s[row + 8];
                                s[row + 8] = s[row + 12]; s[row + 12] = tmp;
                        }
                if (step < 10) {
                        memcpy(h, s, 16);
                        for (size_t c = 0; c < 4; c++) {
                                uint8_t *p = h + c * 4, *q = s + c * 4;
                                q[0] = ref_gf_mul(p[0], 2) ^ ref_gf_mul(p[1], 3) ^ p[2] ^ p[3];
                                q[1] = p[0] ^ ref_gf_mul(p[1], 2) ^ ref_gf_mul(p[2], 3) ^ p[3];
                                q[2] = p[0] ^ p[1] ^ ref_gf_mul(p[2], 2) ^ ref_gf_mul(p[3], 3);
                                q[3] = ref_gf_mul(p[0], 3) ^ p[1] ^ p[2] ^ ref_gf_mul(p[3], 2);
                        }
                }
                for (size_t i = 0; i < 16; i++) s[i] ^= round[step * 16 + i];
        }
        memcpy(out, s, 16);
}

static uint64_t xorshift64(uint64_t *s)
{
        uint64_t x = *s;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        *s = x;
        return x;
}

static double now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); exit(1); } } while (0)

static void test_sboxes(void)
{
        for (int i = 0; i < 256; i++) {
                uint8_t s = aes_core_sbox_byte((uint8_t)i);
                uint8_t rs = ref_aes_sbox_byte((uint8_t)i);
                uint8_t is = aes_core_inv_sbox_byte((uint8_t)i);
                uint8_t ris = ref_aes_inv_sbox_byte((uint8_t)i);
                CHECK(s == rs, "sbox_byte matches ref");
                CHECK(is == ris, "inv_sbox_byte matches ref");
                CHECK(aes_core_inv_sbox_byte(s) == (uint8_t)i, "inv_sbox(sbox(x)) == x");
                CHECK(aes_core_sbox_byte(is) == (uint8_t)i, "sbox(inv_sbox(x)) == x");
        }
        CHECK(aes_core_sbox_byte(0x00) == 0x63 && aes_core_sbox_byte(0x53) == 0xed, "known S-box spots");
        CHECK(aes_core_inv_sbox_byte(0x63) == 0x00 && aes_core_inv_sbox_byte(0xed) == 0x53, "known InvS-box spots");
}

static void test_fips197(void)
{
        /* FIPS-197 Appendix B */
        const uint8_t key_b[16] = {
                0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
        };
        const uint8_t pt_b[16] = {
                0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
                0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
        };
        const uint8_t ct_b[16] = {
                0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
                0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32
        };
        const uint8_t rk10_b[16] = {
                0xd0, 0x14, 0xf9, 0xa8, 0xc9, 0xee, 0x25, 0x89,
                0xe1, 0x3f, 0x0c, 0xc8, 0xb6, 0x63, 0x0c, 0xa6
        };
        uint8_t rk[176], rk_ref[176], out[16];
        aes_core_128_expand(key_b, rk);
        ref_aes128_expand(key_b, rk_ref);
        CHECK(memcmp(rk, rk_ref, 176) == 0, "FIPS-197 App B key schedule vs ref");
        CHECK(memcmp(rk + 160, rk10_b, 16) == 0, "FIPS-197 App B round 10 key");
        aes_core_128_decrypt(rk, ct_b, out);
        CHECK(memcmp(out, pt_b, 16) == 0, "FIPS-197 App B decrypt");

        /* In-place decrypt */
        memcpy(out, ct_b, 16);
        aes_core_128_decrypt(rk, out, out);
        CHECK(memcmp(out, pt_b, 16) == 0, "FIPS-197 App B in-place decrypt");

        /* FIPS-197 Appendix C.1 */
        const uint8_t key_c[16] = {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
        };
        const uint8_t pt_c[16] = {
                0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
        };
        const uint8_t ct_c[16] = {
                0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
        };
        aes_core_128_expand(key_c, rk);
        ref_aes128_expand(key_c, rk_ref);
        CHECK(memcmp(rk, rk_ref, 176) == 0, "FIPS-197 App C.1 key schedule");
        aes_core_128_decrypt(rk, ct_c, out);
        CHECK(memcmp(out, pt_c, 16) == 0, "FIPS-197 App C.1 decrypt");
}

static uint64_t load_be64(const uint8_t *p)
{
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
        return v;
}
static void store_be64(uint8_t *p, uint64_t v)
{
        for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)v; v >>= 8; }
}

static void test_rfc3394_unwrap(void)
{
        /* RFC 3394 Section 4.1: Wrap 128 bits of Key Data with a 128-bit KEK */
        const uint8_t kek[16] = {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
        };
        const uint8_t wrap[24] = {
                0x1f, 0xa6, 0x8b, 0x0a, 0x81, 0x12, 0xb4, 0x47,
                0xae, 0xf3, 0x4b, 0xd8, 0xfb, 0x5a, 0x7b, 0x82,
                0x9d, 0x3e, 0x86, 0x23, 0x71, 0xd2, 0xcf, 0xe5
        };
        const uint8_t expect[16] = {
                0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
        };
        uint8_t rk[176], a[8], r[16], blk[16];
        aes_core_128_expand(kek, rk);
        memcpy(a, wrap, 8);
        memcpy(r, wrap + 8, 16);
        for (int turn = 6; turn > 0; turn--) {
                for (int i = 2; i > 0; i--) {
                        uint64_t t = (uint64_t)2 * (uint64_t)(turn - 1) + (uint64_t)i;
                        store_be64(a, load_be64(a) ^ t);
                        memcpy(blk, a, 8);
                        memcpy(blk + 8, r + (i - 1) * 8, 8);
                        aes_core_128_decrypt(rk, blk, blk);
                        memcpy(a, blk, 8);
                        memcpy(r + (i - 1) * 8, blk + 8, 8);
                }
        }
        CHECK(load_be64(a) == 0xa6a6a6a6a6a6a6a6ULL, "RFC 3394 IV check");
        CHECK(memcmp(r, expect, 16) == 0, "RFC 3394 unwrapped key data");
}

static void test_rfc5297_siv_double(void)
{
        /* RFC 4493 Section 4 / RFC 5297: AES-128(K, 0^128) -> K1 -> K2 */
        const uint8_t L[16] = {
                0x7d, 0xf7, 0x6b, 0x0c, 0x1a, 0xb8, 0x99, 0xb3,
                0x3e, 0x42, 0xf0, 0x47, 0xb9, 0x1b, 0x54, 0x6f
        };
        const uint8_t K1[16] = {
                0xfb, 0xee, 0xd6, 0x18, 0x35, 0x71, 0x33, 0x66,
                0x7c, 0x85, 0xe0, 0x8f, 0x72, 0x36, 0xa8, 0xde
        };
        const uint8_t K2[16] = {
                0xf7, 0xdd, 0xac, 0x30, 0x6a, 0xe2, 0x66, 0xcc,
                0xf9, 0x0b, 0xc1, 0x1e, 0xe4, 0x6d, 0x51, 0x3b
        };
        uint8_t blk[16];
        memcpy(blk, L, 16);
        aes_core_siv_double(blk);
        CHECK(memcmp(blk, K1, 16) == 0, "RFC 4493/5297 K1 = dbl(L) (MSB=0)");
        aes_core_siv_double(blk);
        CHECK(memcmp(blk, K2, 16) == 0, "RFC 4493/5297 K2 = dbl(K1) (MSB=1, XOR 0x87)");
}

static void test_ct_equal_and_nulls(void)
{
        uint8_t a[131], b[131];
        for (size_t i = 0; i < sizeof(a); i++) a[i] = b[i] = (uint8_t)(i * 37 + 11);

        /* NULL and 0-len safety */
        CHECK(aes_core_ct_equal(NULL, NULL, 0) == 1, "ct_equal(NULL, NULL, 0) == 1");
        CHECK(aes_core_ct_equal(a, NULL, 0) == 1, "ct_equal(a, NULL, 0) == 1");
        CHECK(aes_core_ct_equal(NULL, b, 0) == 1, "ct_equal(NULL, b, 0) == 1");
        CHECK(aes_core_ct_equal(NULL, b, 1) == 0, "ct_equal(NULL, b, 1) == 0");
        CHECK(aes_core_ct_equal(a, NULL, 16) == 0, "ct_equal(a, NULL, 16) == 0");
        aes_core_128_expand(NULL, a);
        aes_core_128_expand(a, NULL);
        aes_core_128_decrypt(NULL, a, b);
        aes_core_128_decrypt(a, NULL, b);
        aes_core_128_decrypt(a, b, NULL);
        aes_core_siv_double(NULL);

        /* Exhaustive lengths 0..64 + single-bit flip at every byte & bit offset */
        for (size_t len = 0; len <= 64; len++) {
                CHECK(aes_core_ct_equal(a, b, len) == 1, "ct_equal match");
                CHECK(aes_core_ct_equal(a + 1, b + 1, len) == 1, "ct_equal unaligned match");
                for (size_t off = 0; off < len; off++) {
                        for (int bit = 0; bit < 8; bit++) {
                                b[off] ^= (uint8_t)(1u << bit);
                                CHECK(aes_core_ct_equal(a, b, len) == 0, "ct_equal detects 1-bit flip");
                                b[off] ^= (uint8_t)(1u << bit);
                        }
                }
        }
}

static void test_fuzz(void)
{
        uint64_t rng = 0x123456789abcdef0ULL;
        for (int iter = 0; iter < 500; iter++) {
                uint8_t key[16], pt[16], ct[16], dec[16], dec_ref[16], rk[176], rk_ref[176];
                uint8_t siv_a[16], siv_r[16];
                for (int i = 0; i < 16; i++) {
                        key[i] = (uint8_t)xorshift64(&rng);
                        pt[i] = (uint8_t)xorshift64(&rng);
                        siv_a[i] = siv_r[i] = (uint8_t)xorshift64(&rng);
                }
                aes_core_128_expand(key, rk);
                ref_aes128_expand(key, rk_ref);
                CHECK(memcmp(rk, rk_ref, 176) == 0, "fuzz key expand");

                ref_aes128_encrypt(rk, pt, ct);
                aes_core_128_decrypt(rk, ct, dec);
                ref_aes128_decrypt(rk_ref, ct, dec_ref);
                CHECK(memcmp(dec, pt, 16) == 0, "fuzz encrypt->decrypt roundtrip");
                CHECK(memcmp(dec, dec_ref, 16) == 0, "fuzz decrypt vs ref");

                aes_core_siv_double(siv_a);
                ref_siv_double(siv_r);
                CHECK(memcmp(siv_a, siv_r, 16) == 0, "fuzz siv_double vs ref");
        }
}

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        CHECK(ps > 0, "page size");
        /* 3 pages: [PROT_NONE][RW][PROT_NONE] */
        uint8_t *m1 = mmap(NULL, (size_t)ps * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        uint8_t *m2 = mmap(NULL, (size_t)ps * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        uint8_t *m3 = mmap(NULL, (size_t)ps * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(m1 != MAP_FAILED && m2 != MAP_FAILED && m3 != MAP_FAILED, "mmap");
        CHECK(mprotect(m1, (size_t)ps, PROT_NONE) == 0 && mprotect(m1 + ps * 2, (size_t)ps, PROT_NONE) == 0, "guard m1");
        CHECK(mprotect(m2, (size_t)ps, PROT_NONE) == 0 && mprotect(m2 + ps * 2, (size_t)ps, PROT_NONE) == 0, "guard m2");
        CHECK(mprotect(m3, (size_t)ps, PROT_NONE) == 0 && mprotect(m3 + ps * 2, (size_t)ps, PROT_NONE) == 0, "guard m3");

        uint8_t *p1 = m1 + ps, *p2 = m2 + ps, *p3 = m3 + ps;
        /* Flush against leading guard page AND flush against trailing guard page */
        for (int end = 0; end < 2; end++) {
                uint8_t *key = end ? (p1 + ps - 16) : p1;
                uint8_t *rk  = end ? (p2 + ps - 176) : p2;
                uint8_t *blk = end ? (p3 + ps - 16) : p3;
                for (int i = 0; i < 16; i++) { key[i] = (uint8_t)i; blk[i] = (uint8_t)(i * 17); }
                aes_core_128_expand(key, rk);
                aes_core_128_decrypt(rk, blk, blk);
                aes_core_siv_double(blk);
                /* ct_equal flush against guard page for lengths 1..33 */
                for (size_t len = 1; len <= 33; len++) {
                        uint8_t *ca = end ? (p1 + ps - len) : p1;
                        uint8_t *cb = end ? (p3 + ps - len) : p3;
                        memcpy(cb, ca, len);
                        CHECK(aes_core_ct_equal(ca, cb, len) == 1, "guard ct_equal eq");
                        cb[len - 1] ^= 1;
                        CHECK(aes_core_ct_equal(ca, cb, len) == 0, "guard ct_equal neq");
                }
        }
        munmap(m1, (size_t)ps * 3);
        munmap(m2, (size_t)ps * 3);
        munmap(m3, (size_t)ps * 3);
}

static void bench(void)
{
        uint8_t key[16] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
        uint8_t rk[176], blk[16] = {0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32};
        uint8_t a[64], b[64];
        memset(a, 0x5a, sizeof(a)); memset(b, 0x5a, sizeof(b));
        volatile uint64_t sink = 0;
        double t0, dt_asm, dt_ref;

        /* 1. aes_core_128_expand */
        const int N_EXP = 50000;
        t0 = now_ns();
        for (int i = 0; i < N_EXP; i++) { aes_core_128_expand(key, rk); key[0] ^= rk[175]; }
        dt_asm = (now_ns() - t0) / N_EXP;
        t0 = now_ns();
        for (int i = 0; i < N_EXP; i++) { ref_aes128_expand(key, rk); key[0] ^= rk[175]; }
        dt_ref = (now_ns() - t0) / N_EXP;
        sink += rk[0];
        printf("  128_expand : ASM %7.1f ns/op | C -O2 %7.1f ns/op | %.2fx faster\n", dt_asm, dt_ref, dt_ref / dt_asm);

        /* 2. aes_core_128_decrypt */
        const int N_DEC = 20000;
        aes_core_128_expand(key, rk);
        t0 = now_ns();
        for (int i = 0; i < N_DEC; i++) aes_core_128_decrypt(rk, blk, blk);
        dt_asm = (now_ns() - t0) / N_DEC;
        t0 = now_ns();
        for (int i = 0; i < N_DEC; i++) ref_aes128_decrypt(rk, blk, blk);
        dt_ref = (now_ns() - t0) / N_DEC;
        sink += blk[0];
        printf("  128_decrypt: ASM %7.1f ns/op | C -O2 %7.1f ns/op | %.2fx faster\n", dt_asm, dt_ref, dt_ref / dt_asm);

        /* 3. aes_core_siv_double */
        const int N_SIV = 2000000;
        t0 = now_ns();
        for (int i = 0; i < N_SIV; i++) aes_core_siv_double(blk);
        dt_asm = (now_ns() - t0) / N_SIV;
        t0 = now_ns();
        for (int i = 0; i < N_SIV; i++) ref_siv_double(blk);
        dt_ref = (now_ns() - t0) / N_SIV;
        sink += blk[0];
        printf("  siv_double : ASM %7.1f ns/op | C -O2 %7.1f ns/op | %.2fx faster\n", dt_asm, dt_ref, dt_ref / dt_asm);

        /* 4. aes_core_ct_equal (64B) */
        const int N_EQ = 2000000;
        t0 = now_ns();
        for (int i = 0; i < N_EQ; i++) { a[0] = (uint8_t)i; b[0] = (uint8_t)i; sink += (uint64_t)aes_core_ct_equal(a, b, 64); }
        dt_asm = (now_ns() - t0) / N_EQ;
        t0 = now_ns();
        for (int i = 0; i < N_EQ; i++) { a[0] = (uint8_t)i; b[0] = (uint8_t)i; sink += (uint64_t)ref_ct_equal(a, b, 64); }
        dt_ref = (now_ns() - t0) / N_EQ;
        printf("  ct_equal64 : ASM %7.1f ns/op | C -O2 %7.1f ns/op | %.2fx faster\n", dt_asm, dt_ref, dt_ref / dt_asm);
        (void)sink;
}

int main(void)
{
        test_sboxes();
        test_fips197();
        test_rfc3394_unwrap();
        test_rfc5297_siv_double();
        test_ct_equal_and_nulls();
        test_fuzz();
        test_guard_pages();
        printf("PASS: all aes_core tests passed!\n");
        bench();
        return 0;
}
