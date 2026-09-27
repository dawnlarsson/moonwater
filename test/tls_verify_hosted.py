"""Hosted crypto lift for tls_verify_fuzz (C montgomery + pure SHA).

Used by test/differential.py harness_tls_verify_fuzz. CHECK_net still proves
the freestanding path with lib.c ASM; this lift lets libFuzzer exercise
production tls_verify_one under ASan/UBSan/MSan without freestanding link.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

TLS_VERIFY_HOSTED_SHA = r"""
/* Compact SHA-256 / SHA-384 for hosted tls_verify_fuzz (public-domain style). */
typedef struct {
        p64 len;
        p32 state[8];
        p8 buf[64];
        positive used;
} fuzz_sha256;

static void fuzz_sha256_init(fuzz_sha256 *h)
{
        h->len = 0;
        h->used = 0;
        h->state[0] = 0x6a09e667u;
        h->state[1] = 0xbb67ae85u;
        h->state[2] = 0x3c6ef372u;
        h->state[3] = 0xa54ff53au;
        h->state[4] = 0x510e527fu;
        h->state[5] = 0x9b05688cu;
        h->state[6] = 0x1f83d9abu;
        h->state[7] = 0x5be0cd19u;
}

static p32 fuzz_rotr32(p32 x, positive n)
{
        return (x >> n) | (x << (32 - n));
}

static void fuzz_sha256_block(fuzz_sha256 *h, const p8 *block)
{
        static const p32 K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
            0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
            0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
            0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
            0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
            0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
            0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
            0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        p32 w[64];
        p32 a, b, c, d, e, f, g, hh;
        positive i;

        for (i = 0; i < 16; i++)
                w[i] = ((p32)block[i * 4] << 24) | ((p32)block[i * 4 + 1] << 16) |
                       ((p32)block[i * 4 + 2] << 8) | (p32)block[i * 4 + 3];
        for (; i < 64; i++)
        {
                p32 s0 = fuzz_rotr32(w[i - 15], 7) ^ fuzz_rotr32(w[i - 15], 18) ^
                         (w[i - 15] >> 3);
                p32 s1 = fuzz_rotr32(w[i - 2], 17) ^ fuzz_rotr32(w[i - 2], 19) ^
                         (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h->state[0];
        b = h->state[1];
        c = h->state[2];
        d = h->state[3];
        e = h->state[4];
        f = h->state[5];
        g = h->state[6];
        hh = h->state[7];
        for (i = 0; i < 64; i++)
        {
                p32 S1 = fuzz_rotr32(e, 6) ^ fuzz_rotr32(e, 11) ^ fuzz_rotr32(e, 25);
                p32 ch = (e & f) ^ ((~e) & g);
                p32 t1 = hh + S1 + ch + K[i] + w[i];
                p32 S0 = fuzz_rotr32(a, 2) ^ fuzz_rotr32(a, 13) ^ fuzz_rotr32(a, 22);
                p32 maj = (a & b) ^ (a & c) ^ (b & c);
                p32 t2 = S0 + maj;
                hh = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
        }
        h->state[0] += a;
        h->state[1] += b;
        h->state[2] += c;
        h->state[3] += d;
        h->state[4] += e;
        h->state[5] += f;
        h->state[6] += g;
        h->state[7] += hh;
}

static void fuzz_sha256_update(fuzz_sha256 *h, const p8 *data, positive n)
{
        h->len += (p64)n * 8;
        while (n)
        {
                positive take = 64 - h->used;
                if (take > n)
                        take = n;
                memory_copy(h->buf + h->used, data, take);
                h->used += take;
                data += take;
                n -= take;
                if (h->used == 64)
                {
                        fuzz_sha256_block(h, h->buf);
                        h->used = 0;
                }
        }
}

static void fuzz_sha256_final(fuzz_sha256 *h, p8 *out)
{
        p8 pad[64];
        p64 bits = h->len;
        positive i;

        memory_fill(pad, 0, sizeof pad);
        pad[0] = 0x80;
        if (h->used > 55)
        {
                fuzz_sha256_update(h, pad, 64 - h->used);
                memory_fill(pad, 0, 56);
                fuzz_sha256_update(h, pad, 56);
        }
        else
                fuzz_sha256_update(h, pad, 56 - h->used);
        for (i = 0; i < 8; i++)
                pad[i] = (p8)(bits >> (56 - 8 * i));
        fuzz_sha256_update(h, pad, 8);
        for (i = 0; i < 8; i++)
        {
                out[i * 4] = (p8)(h->state[i] >> 24);
                out[i * 4 + 1] = (p8)(h->state[i] >> 16);
                out[i * 4 + 2] = (p8)(h->state[i] >> 8);
                out[i * 4 + 3] = (p8)h->state[i];
        }
}

typedef struct {
        p64 len_hi;
        p64 len_lo;
        p64 state[8];
        p8 buf[128];
        positive used;
} fuzz_sha512;

static p64 fuzz_rotr64(p64 x, positive n)
{
        return (x >> n) | (x << (64 - n));
}

static void fuzz_sha384_init(fuzz_sha512 *h)
{
        h->len_hi = 0;
        h->len_lo = 0;
        h->used = 0;
        h->state[0] = 0xcbbb9d5dc1059ed8ull;
        h->state[1] = 0x629a292a367cd507ull;
        h->state[2] = 0x9159015a3070dd17ull;
        h->state[3] = 0x152fecd8f70e5939ull;
        h->state[4] = 0x67332667ffc00b31ull;
        h->state[5] = 0x8eb44a8768581511ull;
        h->state[6] = 0xdb0c2e0d64f98fa7ull;
        h->state[7] = 0x47b5481dbefa4fa4ull;
}

static void fuzz_sha512_block(fuzz_sha512 *h, const p8 *block)
{
        static const p64 K[80] = {
            0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full,
            0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
            0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull,
            0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
            0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
            0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
            0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull,
            0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
            0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full,
            0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
            0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull,
            0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
            0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull,
            0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
            0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
            0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
            0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull,
            0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
            0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull,
            0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
            0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull,
            0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
            0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull,
            0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
            0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
            0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
            0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
        p64 w[80];
        p64 a, b, c, d, e, f, g, hh;
        positive i;

        for (i = 0; i < 16; i++)
        {
                w[i] = ((p64)block[i * 8] << 56) | ((p64)block[i * 8 + 1] << 48) |
                       ((p64)block[i * 8 + 2] << 40) | ((p64)block[i * 8 + 3] << 32) |
                       ((p64)block[i * 8 + 4] << 24) | ((p64)block[i * 8 + 5] << 16) |
                       ((p64)block[i * 8 + 6] << 8) | (p64)block[i * 8 + 7];
        }
        for (; i < 80; i++)
        {
                p64 s0 = fuzz_rotr64(w[i - 15], 1) ^ fuzz_rotr64(w[i - 15], 8) ^
                         (w[i - 15] >> 7);
                p64 s1 = fuzz_rotr64(w[i - 2], 19) ^ fuzz_rotr64(w[i - 2], 61) ^
                         (w[i - 2] >> 6);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h->state[0];
        b = h->state[1];
        c = h->state[2];
        d = h->state[3];
        e = h->state[4];
        f = h->state[5];
        g = h->state[6];
        hh = h->state[7];
        for (i = 0; i < 80; i++)
        {
                p64 S1 = fuzz_rotr64(e, 14) ^ fuzz_rotr64(e, 18) ^ fuzz_rotr64(e, 41);
                p64 ch = (e & f) ^ ((~e) & g);
                p64 t1 = hh + S1 + ch + K[i] + w[i];
                p64 S0 = fuzz_rotr64(a, 28) ^ fuzz_rotr64(a, 34) ^ fuzz_rotr64(a, 39);
                p64 maj = (a & b) ^ (a & c) ^ (b & c);
                p64 t2 = S0 + maj;
                hh = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
        }
        h->state[0] += a;
        h->state[1] += b;
        h->state[2] += c;
        h->state[3] += d;
        h->state[4] += e;
        h->state[5] += f;
        h->state[6] += g;
        h->state[7] += hh;
}

static void fuzz_sha512_update(fuzz_sha512 *h, const p8 *data, positive n)
{
        p64 add = (p64)n * 8;
        h->len_lo += add;
        if (h->len_lo < add)
                h->len_hi++;
        while (n)
        {
                positive take = 128 - h->used;
                if (take > n)
                        take = n;
                memory_copy(h->buf + h->used, data, take);
                h->used += take;
                data += take;
                n -= take;
                if (h->used == 128)
                {
                        fuzz_sha512_block(h, h->buf);
                        h->used = 0;
                }
        }
}

static void fuzz_sha384_final(fuzz_sha512 *h, p8 *out)
{
        p8 pad[128];
        p64 hi = h->len_hi;
        p64 lo = h->len_lo;
        positive i;

        memory_fill(pad, 0, sizeof pad);
        pad[0] = 0x80;
        if (h->used > 111)
        {
                fuzz_sha512_update(h, pad, 128 - h->used);
                memory_fill(pad, 0, 112);
                fuzz_sha512_update(h, pad, 112);
        }
        else
                fuzz_sha512_update(h, pad, 112 - h->used);
        for (i = 0; i < 8; i++)
                pad[i] = (p8)(hi >> (56 - 8 * i));
        for (i = 0; i < 8; i++)
                pad[8 + i] = (p8)(lo >> (56 - 8 * i));
        fuzz_sha512_update(h, pad, 16);
        for (i = 0; i < 6; i++)
        {
                out[i * 8] = (p8)(h->state[i] >> 56);
                out[i * 8 + 1] = (p8)(h->state[i] >> 48);
                out[i * 8 + 2] = (p8)(h->state[i] >> 40);
                out[i * 8 + 3] = (p8)(h->state[i] >> 32);
                out[i * 8 + 4] = (p8)(h->state[i] >> 24);
                out[i * 8 + 5] = (p8)(h->state[i] >> 16);
                out[i * 8 + 6] = (p8)(h->state[i] >> 8);
                out[i * 8 + 7] = (p8)h->state[i];
        }
}

typedef fuzz_sha256 crypto_sha256;
typedef fuzz_sha512 crypto_sha512;
static void crypto_sha256_open(crypto_sha256 *h) { fuzz_sha256_init(h); }
static void crypto_sha256_write(crypto_sha256 *h, p8 *d, positive n)
{
        fuzz_sha256_update(h, d, n);
}
static void crypto_sha256_close(crypto_sha256 *h, p8 *out)
{
        fuzz_sha256_final(h, out);
}
static void crypto_sha256_of(p8 *d, positive n, p8 *out)
{
        crypto_sha256 h;
        fuzz_sha256_init(&h);
        fuzz_sha256_update(&h, d, n);
        fuzz_sha256_final(&h, out);
}
static void crypto_sha384(p8 *d, positive n, p8 *out)
{
        crypto_sha512 h;
        fuzz_sha384_init(&h);
        fuzz_sha512_update(&h, d, n);
        fuzz_sha384_final(&h, out);
}

"""


def _sec(text: str, first: str, following: str) -> str:
    i = text.index(first)
    return text[i : text.index(following, i)]


def force_c_field_ops(text: str) -> str:
    """Strip lib.c p256_/p384_ ASM fast-paths; always use C montgomery."""
    text = re.sub(
        r"if \(f == address_of crypto_p256_field\)\s*\{\s*p256_add\(d, a, b\);\s*return;\s*\}\s*"
        r"if \(f == address_of crypto_p384_field\)\s*\{\s*p384_add\(d, a, b\);\s*return;\s*\}",
        "",
        text,
    )
    text = re.sub(
        r"if \(f == address_of crypto_p256_field\)\s*\{\s*p256_subtract\(d, a, b\);\s*return;\s*\}\s*"
        r"if \(f == address_of crypto_p384_field\)\s*\{\s*p384_subtract\(d, a, b\);\s*return;\s*\}",
        "",
        text,
    )
    text = re.sub(
        r"if \(f == address_of crypto_p256_field\)\s*p256_multiply\(d, a, b\);\s*"
        r"else if \(f == address_of crypto_p384_field\)\s*p384_multiply\(d, a, b\);\s*"
        r"else\s*",
        "",
        text,
    )
    text = re.sub(
        r"if \(f == address_of crypto_p256_field\)\s*p256_square\(d, a\);\s*"
        r"else if \(f == address_of crypto_p384_field\)\s*p384_square\(d, a\);\s*"
        r"else\s*",
        "",
        text,
    )
    return text


def montgomery_reference_c(checks_text: str | None = None) -> str:
    if checks_text is None:
        checks_text = (ROOT / "test" / "checks.c").read_text()
    mref = _sec(
        checks_text,
        "#elif defined(SHARED_montgomery_reference)",
        "#elif defined(SHARED_unicode_width_reference)",
    )
    mref = mref.split("\n", 1)[1]
    mref = mref[: mref.rfind("#endif") + len("#endif")]
    return mref.replace("montgomery_reference_multiply", "montgomery_multiply")


def crypto_verify_slices(net_text: str | None = None) -> tuple[str, str]:
    if net_text is None:
        net_text = (ROOT / "src" / "net" / "net.c").read_text()
    ecdsa = force_c_field_ops(
        _sec(
            net_text,
            "#define CRYPTO_FE_MAX 6\n",
            "static bool crypto_scalar_reduce_be(p8 address_to out, const p8 address_to bytes,",
        )
    )
    rsa = _sec(
        net_text,
        "/* x = 2x mod m for x below m.  Public moduli only: the reduction branches. */\n"
        "static fn crypto_rsa_double",
        "static fn crypto_mgf1_sha256",
    )
    return ecdsa, rsa


def extract_cert_hex(name: str, checks_text: str | None = None) -> bytes:
    if checks_text is None:
        checks_text = (ROOT / "test" / "checks.c").read_text()
    match = re.search(
        rf'static const char {name}\[\] =\s*((?:"[0-9a-fA-F]+"\s*)+);',
        checks_text,
    )
    if not match:
        raise ValueError(f"missing {name} in checks.c")
    return bytes.fromhex(re.sub(r'["\s]', "", match.group(1)))


def tls_verify_hosted_shim() -> str:
    return r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ctype.h>
typedef uint8_t p8;
typedef uint8_t b8;
typedef uint16_t p16;
typedef uint32_t p32;
typedef uint64_t p64;
typedef int32_t b32;
typedef long bipolar;
typedef unsigned long positive;
typedef void *address_any;
typedef char *string_address;
typedef const char *const_string;
#define COLD
#define CONST
#define PURE
#define fn void
#define address_to *
#define address_of &
#define null NULL
#define end ((p8)0)
#define positive_max (~(positive)0)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define memory_compare memcmp
#define memory_copy memcpy
#define memory_fill(at, v, n) memset((at), (int)(v), (n))
#define TLS_OK 0
#define TLS_FAIL (-1)
typedef unsigned __int128 crypto_wide;
static void crypto_forget(address_any secret, positive length)
{
        volatile p8 *at = secret;
        while (length)
        {
                *at++ = 0;
                length--;
        }
}
static p64 crypto_be64(const p8 *bytes)
{
        return ((p64)(((p32)bytes[0] << 24) | ((p32)bytes[1] << 16) |
                      ((p32)bytes[2] << 8) | (p32)bytes[3])
                << 32) |
               (p64)(((p32)bytes[4] << 24) | ((p32)bytes[5] << 16) |
                     ((p32)bytes[6] << 8) | (p32)bytes[7]);
}
static void crypto_put_be64(p8 *bytes, p64 value)
{
        bytes[0] = (p8)(value >> 56);
        bytes[1] = (p8)(value >> 48);
        bytes[2] = (p8)(value >> 40);
        bytes[3] = (p8)(value >> 32);
        bytes[4] = (p8)(value >> 24);
        bytes[5] = (p8)(value >> 16);
        bytes[6] = (p8)(value >> 8);
        bytes[7] = (p8)value;
}
static positive memory_span_byte(const void *block, p8 byte, positive size)
{
        const p8 *at = block;
        positive i = 0;
        while (i < size && at[i] == byte)
                i++;
        return i;
}
""" + TLS_VERIFY_HOSTED_SHA + r"""
static inline p8 byte_is_alnum(p8 b) { return isalnum(b) != 0; }
static inline p8 byte_is_digit(p8 b) { return isdigit(b) != 0; }
static inline p8 byte_is_control(p8 b) { return b < 0x20 || b == 0x7f; }
static inline p8 byte_is_blank(p8 b) { return b == ' ' || b == '\t'; }
static positive string_length(const_string s) { return (positive)strlen(s); }
static string_address string_first_of(string_address s, int c)
{
        return (string_address)strchr(s, c);
}
static b32 memory_compare_ascii_case(const void *one, const void *two,
                                     positive size)
{
        const p8 *a = one, *b = two;
        for (positive i = 0; i < size; i++)
        {
                p8 x = a[i], y = b[i];
                if (x >= 'A' && x <= 'Z')
                        x = (p8)(x - 'A' + 'a');
                if (y >= 'A' && y <= 'Z')
                        y = (p8)(y - 'A' + 'a');
                if (x != y)
                        return (b32)(x - y);
        }
        return 0;
}
static positive memory_span_without_byte(const void *block, p8 byte,
                                         positive size)
{
        const p8 *at = block;
        positive i = 0;
        while (i < size && at[i] != byte)
                i++;
        return i;
}
static p16 network_load_16(const p8 *b)
{
        return (p16)((b[0] << 8) | b[1]);
}
static p32 network_load_32(const p8 *b)
{
        return ((p32)b[0] << 24) | ((p32)b[1] << 16) | ((p32)b[2] << 8) | b[3];
}
static bipolar string_to_host(string_address host)
{
        unsigned a, b, c, d;
        char tail;
        if (!host)
                return -1;
        if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
            a < 256 && b < 256 && c < 256 && d < 256)
                return (bipolar)(((p32)a << 24) | ((p32)b << 16) |
                                 ((p32)c << 8) | (p32)d);
        return -1;
}
"""


def wr2_gts_prove_c(checks_text: str | None = None) -> str:
    wr2 = extract_cert_hex("wr2_hex", checks_text)
    gts = extract_cert_hex("gts_r1_hex", checks_text)
    wr2_c = ", ".join(f"0x{b:02x}" for b in wr2)
    gts_c = ", ".join(f"0x{b:02x}" for b in gts)
    return f"""
static const p8 fuzz_wr2_der[] = {{ {wr2_c} }};
static const p8 fuzz_gts_der[] = {{ {gts_c} }};

static bool fuzz_prove_wr2_gts(void)
{{
        p8 wr2_buf[sizeof fuzz_wr2_der];
        p8 gts_buf[sizeof fuzz_gts_der];
        tls_cert wr2, gts;

        memory_copy(wr2_buf, fuzz_wr2_der, sizeof fuzz_wr2_der);
        memory_copy(gts_buf, fuzz_gts_der, sizeof fuzz_gts_der);
        memory_fill(address_of wr2, 0, sizeof wr2);
        memory_fill(address_of gts, 0, sizeof gts);
        if (tls_parse_cert(wr2_buf, sizeof fuzz_wr2_der, address_of wr2, null))
                return false;
        if (tls_parse_cert(gts_buf, sizeof fuzz_gts_der, address_of gts, null))
                return false;
        if (!tls_certificate_names_chain(address_of wr2, address_of gts))
                return false;
        if (!tls_verify_one(address_of wr2, address_of gts))
                return false;
        wr2.sig[wr2.sig_length - 1] ^= 1;
        if (tls_verify_one(address_of wr2, address_of gts))
                return false;
        return true;
}}
"""


def build_tls_verify_fuzz_source(net_text: str, oids: str, parsers: str,
                                 policy: str, framing: str, driver: str,
                                 checks_text: str | None = None) -> str:
    ecdsa, rsa = crypto_verify_slices(net_text)
    verify_one = _sec(
        net_text,
        "static COLD bool tls_verify_one(tls_cert address_to child, tls_cert address_to issuer)",
        "/* The last certificate served names its issuer.",
    )
    parts = [
        tls_verify_hosted_shim(),
        montgomery_reference_c(checks_text),
        ecdsa,
        rsa,
        oids,
        parsers,
        policy,
        verify_one,
        wr2_gts_prove_c(checks_text),
        framing,
        driver,
    ]
    source = "\n".join(parts)
    for bad in (
        "p256_multiply",
        "p384_multiply",
        "p256_add",
        "p256_square",
        "p384_add",
        "p384_square",
        "p256_subtract",
        "p384_subtract",
    ):
        if re.search(rf"\b{bad}\s*\(", source):
            raise RuntimeError(f"hosted tls_verify lift still references {bad}")
    return source
