/*
        The float ops the kernels use, in integer arithmetic, as the dispatch's float mode has them (the first resource word's 0xc0: single
        precision rounded to nearest even, its denormals flushed to zero in and out). v_mul_f32 and the two conversions are exact by IEEE;
        v_rcp_f32 and v_sqrt_f32 are correctly rounded here, where the card's documentation promises one ulp, so the model has gpu_model.nudge
        to move their results by an ulp and the rows show the pictures do not depend on it (the scene kernel corrects both to the exact
        integer). NaN is quieted, an invalid result is 0x7fc00000; no kernel reaches either.
*/

// The float nearest sign * value * 2^scale (value an integer, sticky when the exact value is a little more than value): rounded to
// nearest even, flushed to zero below the smallest normal, infinite past the largest.
static p32 gpu_model_pack(p32 sign, p64 value, b32 scale, b32 sticky)
{
        b32 top = 63, exponent;
        p32 mantissa;

        if (!value)
                return sign;
        while (!(value >> top & 1))
                top--;
        exponent = top + scale;
        if (top > 23)
        {
                b32 shift = top - 23;
                p64 rest = value & ((1ull << shift) - 1), half = 1ull << (shift - 1);

                mantissa = (p32)(value >> shift);
                sticky = sticky || (rest & (half - 1)) != 0;
                if ((rest & half) && (sticky || (mantissa & 1)) && !(gpu_model.doubt & GPU_MODEL_DOUBT_ROUND))
                        mantissa++;
                if (mantissa == 1u << 24)
                        mantissa >>= 1, exponent++;
        }
        else
                mantissa = (p32)(value << (23 - top));
        if (exponent + 127 >= 255)
                return sign | 0x7f800000u;
        if (exponent + 127 <= 0)
                return sign; // flushed
        return sign | (p32)(exponent + 127) << 23 | (mantissa & 0x7fffffu);
}

// A float's kind: 0 zero (a denormal is one, flushed), 1 normal (its mantissa with the leading 1, the exponent of its lowest bit),
// 2 infinite, 3 NaN.
static int gpu_model_parts(p32 bits, p64 address_to mantissa, b32 address_to exponent)
{
        p32 biased = bits >> 23 & 255;

        if (biased == 255)
                return bits & 0x7fffffu ? 3 : 2;
        if (!biased)
                return 0;
        *mantissa = (bits & 0x7fffffu) | 0x800000u, *exponent = (b32)biased - 150;
        return 1;
}

static p32 gpu_model_mul_f32(p32 a, p32 b)
{
        p32 sign = (a ^ b) & 0x80000000u;
        p64 ma = 0, mb = 0;
        b32 ea = 0, eb = 0;
        int ka = gpu_model_parts(a, &ma, &ea), kb = gpu_model_parts(b, &mb, &eb);

        if (ka == 3)
                return a | 0x400000u;
        if (kb == 3)
                return b | 0x400000u;
        if ((ka == 2 && kb == 0) || (ka == 0 && kb == 2))
                return 0x7fc00000u;
        if (ka == 2 || kb == 2)
                return sign | 0x7f800000u;
        if (ka == 0 || kb == 0)
                return sign;
        return gpu_model_pack(sign, ma * mb, ea + eb, 0);
}

static p32 gpu_model_rcp_f32(p32 a)
{
        p32 sign = a & 0x80000000u;
        p64 m = 0;
        b32 e = 0;
        int kind = gpu_model_parts(a, &m, &e);

        if (kind == 3)
                return a | 0x400000u;
        if (kind == 2)
                return sign;
        if (kind == 0)
                return sign | 0x7f800000u;
        // 1 / (m 2^e) = (2^50 / m) 2^(-50 - e), the quotient 27 bits and the remainder sticky
        return gpu_model_pack(sign, (1ull << 50) / m, -50 - e, (1ull << 50) % m != 0);
}

static p64 gpu_model_isqrt(p64 n)
{
        p64 root = 0, bit = 1ull << 62;

        while (bit > n)
                bit >>= 2;
        while (bit)
        {
                if (n >= root + bit)
                        n -= root + bit, root = (root >> 1) + bit;
                else
                        root >>= 1;
                bit >>= 2;
        }
        return root;
}

static p32 gpu_model_sqrt_f32(p32 a)
{
        p64 m = 0, root;
        b32 e = 0;
        int kind = gpu_model_parts(a, &m, &e);

        if (kind == 3)
                return a | 0x400000u;
        if (kind == 0)
                return a & 0x80000000u;
        if (a & 0x80000000u)
                return 0x7fc00000u;
        if (kind == 2)
                return a;
        if (e & 1)
                m <<= 1, e--;
        m <<= 38; // the root has 31 or 32 bits: 7 past the float's
        root = gpu_model_isqrt(m);
        return gpu_model_pack(0, root, e / 2 - 19, root * root != m);
}

static p32 gpu_model_cvt_f32_u32(p32 a)
{
        return gpu_model_pack(0, a, 0, 0);
}

// Toward zero; a negative number, a NaN and a zero are 0, 2^32 and past (infinity too) the largest.
static p32 gpu_model_cvt_u32_f32(p32 a)
{
        p64 m = 0;
        b32 e = 0;
        int kind = gpu_model_parts(a, &m, &e);

        if (kind == 3 || kind == 0 || (a & 0x80000000u))
                return 0;
        if (kind == 2 || e >= 9)
                return 0xffffffffu;
        if (gpu_model.doubt & GPU_MODEL_DOUBT_TRUNC && e < 0)
                return e > -25 ? (p32)((m + (1ull << (-e - 1))) >> -e) : 0;
        return e >= 0 ? (p32)(m << e) : e > -24 ? (p32)(m >> -e) : 0;
}

// A v_rcp_f32 or v_sqrt_f32 result moved as gpu_model.nudge asks: 0 not at all, 1 an ulp up, 2 an ulp down, 3 an ulp up or down or not
// as the lane and the wave fall (a finite nonzero result only).
static p32 gpu_model_nudged(p32 bits, unsigned int lane, unsigned int x, unsigned int y)
{
        p32 biased = bits >> 23 & 255;
        unsigned int way = gpu_model.nudge;

        if (!way || biased == 0 || biased == 255)
                return bits;
        if (way == 3)
                way = ((lane * 2654435761u + x * 40503u + y * 9973u) >> 9) % 3;
        return way == 1 ? bits + 1 : way == 2 ? bits - 1 : bits;
}
