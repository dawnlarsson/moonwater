/*
        test/checks/bench_system.c: the benchmarks of the clock, randomness and limbs, the allocator and locks,
        streams, startup and exec.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef BENCH_clock
/* Calendar text traffic and directive-parser costs. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define CLOCK_BENCH_ROUNDS (1u << 20)
#define CLOCK_BENCH_TRIES 5
#define CLOCK_FORMAT_DIRECTIVES 10
#define CLOCK_SCAN_DIRECTIVES 9

static p8 clock_bench_output[256];
static p8 clock_bench_input[256];
static tm clock_bench_broken;
static volatile positive clock_bench_sink;

static const char address_to clock_bench_literal =
        "ordinary calendar text has no directives; this deliberately makes "
        "the byte traffic, rather than date arithmetic, the measured work.";

static const char address_to clock_bench_format =
        "%Y-%m-%dT%H:%M:%S %A %B %p %z";
static const char address_to clock_bench_scan_format =
        "%Y-%m-%dT%H:%M:%S %A %B %p";
static const char address_to clock_bench_scan_input =
        "2024-08-17T13:42:51 Saturday August PM";

static positive (*volatile clock_bench_strftime)(
        p8 address_to, positive, const char address_to, const tm address_to) =
        strftime;
static p8 address_to (*volatile clock_bench_strptime)(
        const char address_to, const char address_to, tm address_to) = strptime;
static p8 address_to (*volatile clock_bench_asctime)(
        const tm address_to, p8 address_to) = asctime_r;

static fn clock_bench_copy()
{
        positive length = string_length((string_address)clock_bench_literal);

        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
        {
                memory_copy_apart(clock_bench_output,
                                  (address_any)clock_bench_literal, length);
                clock_bench_sink += clock_bench_output[i & 63];
        }
}

static fn clock_bench_compare()
{
        positive length = string_length((string_address)clock_bench_literal);

        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
                clock_bench_sink +=
                        (positive)memory_compare(clock_bench_literal,
                                                 clock_bench_input, length);
}

static fn clock_bench_scan_floor()
{
        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
        {
                const char address_to format = clock_bench_literal;
                const char address_to input = (const char address_to)clock_bench_input;

                while (address_to format != end)
                {
                        positive run;

                        if (byte_is_space((p8)(address_to format)))
                        {
                                format += string_span_of_set(
                                        (string_address)format, " \t\n\r\v\f");
                                input += string_span_of_set(
                                        (string_address)input, " \t\n\r\v\f");
                                continue;
                        }

                        run = string_span_without_set(
                                (string_address)format, "% \t\n\r\v\f");
                        clock_bench_sink += (positive)memory_compare(
                                (address_any)format, (address_any)input, run);
                        format += run;
                        input += run;
                }

                clock_bench_sink += (positive)input;
        }
}

static fn clock_bench_format_literal()
{
        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
                clock_bench_sink += clock_bench_strftime(
                        clock_bench_output, sizeof(clock_bench_output),
                        clock_bench_literal, address_of clock_bench_broken);
}

static fn clock_bench_format_directives()
{
        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
                clock_bench_sink += clock_bench_strftime(
                        clock_bench_output, sizeof(clock_bench_output),
                        clock_bench_format, address_of clock_bench_broken);
}

static fn clock_bench_scan_literal()
{
        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
                clock_bench_sink += (positive)clock_bench_strptime(
                        (const char address_to)clock_bench_input,
                        clock_bench_literal,
                        address_of clock_bench_broken);
}

static fn clock_bench_scan_directives()
{
        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
                clock_bench_sink += (positive)clock_bench_strptime(
                        clock_bench_scan_input, clock_bench_scan_format,
                        address_of clock_bench_broken);
}

static fn clock_bench_asctime_work()
{
        for (positive i = 0; i < CLOCK_BENCH_ROUNDS; i++)
                clock_bench_sink += (positive)clock_bench_asctime(
                        address_of clock_bench_broken, clock_bench_output);
}

static fn clock_bench_report(string_address name, bench_work work,
                             positive units, string_address unit)
{
        bench_report(name, work, CLOCK_BENCH_TRIES,
                     CLOCK_BENCH_ROUNDS * units, unit);
}

static bench_work clock_bench_named(string_address name)
{
        BENCH_NAME(name, "copy-floor", clock_bench_copy);
        BENCH_NAME(name, "compare-floor", clock_bench_compare);
        BENCH_NAME(name, "scan-floor", clock_bench_scan_floor);
        BENCH_NAME(name, "format-literal", clock_bench_format_literal);
        BENCH_NAME(name, "format-directives", clock_bench_format_directives);
        BENCH_NAME(name, "scan-literal", clock_bench_scan_literal);
        BENCH_NAME(name, "scan-directives", clock_bench_scan_directives);
        BENCH_NAME(name, "asctime", clock_bench_asctime_work);

        return null;
}

b32 main(void)
{
        positive literal_length =
                string_length((string_address)clock_bench_literal);

        memory_copy_apart(clock_bench_input,
                          (address_any)clock_bench_literal,
                          literal_length + 1);

        clock_bench_broken.tm_sec = 51;
        clock_bench_broken.tm_min = 42;
        clock_bench_broken.tm_hour = 13;
        clock_bench_broken.tm_mday = 17;
        clock_bench_broken.tm_mon = 7;
        clock_bench_broken.tm_year = 124;
        clock_bench_broken.tm_wday = 6;
        clock_bench_broken.tm_yday = 229;
        clock_bench_broken.tm_zone = "UTC";

        BENCH_RUN_NAMED(clock_bench_named);

        string_format(log, "clock text, best of %p (%p rounds)\n",
                      (positive)CLOCK_BENCH_TRIES,
                      (positive)CLOCK_BENCH_ROUNDS);
        clock_bench_report((string_address)"copy floor", clock_bench_copy,
                           literal_length, (string_address)"byte");
        clock_bench_report((string_address)"strftime literal",
                           clock_bench_format_literal, literal_length,
                           (string_address)"byte");
        clock_bench_report((string_address)"strftime directives",
                           clock_bench_format_directives,
                           CLOCK_FORMAT_DIRECTIVES,
                           (string_address)"directive");
        clock_bench_report((string_address)"compare floor",
                           clock_bench_compare, literal_length,
                           (string_address)"byte");
        clock_bench_report((string_address)"parser floor",
                           clock_bench_scan_floor, literal_length,
                           (string_address)"byte");
        clock_bench_report((string_address)"strptime literal",
                           clock_bench_scan_literal, literal_length,
                           (string_address)"byte");
        clock_bench_report((string_address)"strptime directives",
                           clock_bench_scan_directives, CLOCK_SCAN_DIRECTIVES,
                           (string_address)"directive");
        clock_bench_report((string_address)"asctime", clock_bench_asctime_work,
                           25, (string_address)"byte");
        log_flush();

        return 0;
}
#endif /* BENCH_clock */

#ifdef BENCH_scan_literal
/* sscanf literal matching against its control and semantic traffic floors. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define SCAN_LITERAL_ROUNDS (1u << 20)
#define SCAN_LITERAL_TRIES 5

static const char address_to scan_literal_format =
        "header.alpha-0123456789/body.beta-abcdefghijklmnopqrstuvwxyz/"
        "payload.gamma-ABCDEFGHIJKLMNOPQRSTUVWXYZ/trailer.delta-9876543210";
static p8 scan_literal_input[256];
static volatile positive scan_literal_sink;

static b32 (*volatile scan_literal_call)(string_address, string_address, ...) =
        sscanf;

static fn scan_literal_control()
{
        for (positive i = 0; i < SCAN_LITERAL_ROUNDS; i++)
                scan_literal_sink += scan_literal_call(
                        (string_address)scan_literal_input,
                        (string_address)"");
}

/* The unavoidable semantic work for one ordinary literal run: find the run,
   bound it by the input terminator, and answer its exact common prefix. */
static fn scan_literal_floor()
{
        for (positive i = 0; i < SCAN_LITERAL_ROUNDS; i++)
        {
                positive run = string_span_without_set(
                        (string_address)scan_literal_format, "% \t\n\r\v\f");
                positive available = string_length_max(
                        (string_address)scan_literal_input, run);
                positive matched = memory_common_prefix(
                        (address_any)scan_literal_format,
                        (address_any)scan_literal_input, available);

                scan_literal_sink += run + available + matched;
        }
}

static fn scan_literal_subject()
{
        for (positive i = 0; i < SCAN_LITERAL_ROUNDS; i++)
                scan_literal_sink += scan_literal_call(
                        (string_address)scan_literal_input,
                        (string_address)scan_literal_format);
}

static fn scan_literal_report(string_address name, bench_work work,
                              positive units, string_address unit)
{
        bench_report(name, work, SCAN_LITERAL_TRIES,
                     SCAN_LITERAL_ROUNDS * units, unit);
}

static bench_work scan_literal_named(string_address name)
{
        BENCH_NAME(name, "control", scan_literal_control);
        BENCH_NAME(name, "floor", scan_literal_floor);
        BENCH_NAME(name, "subject", scan_literal_subject);

        return null;
}

b32 main(void)
{
        positive bytes = string_length((string_address)scan_literal_format);

        memory_copy_apart(scan_literal_input,
                          (address_any)scan_literal_format, bytes + 1);

        BENCH_RUN_NAMED(scan_literal_named);

        string_format(log, "sscanf literal, best of %p (%p rounds, %p bytes)\n",
                      (positive)SCAN_LITERAL_TRIES,
                      (positive)SCAN_LITERAL_ROUNDS, bytes);
        scan_literal_report((string_address)"empty call control",
                            scan_literal_control, 1,
                            (string_address)"call");
        scan_literal_report((string_address)"semantic traffic floor",
                            scan_literal_floor, bytes,
                            (string_address)"byte");
        scan_literal_report((string_address)"sscanf literal",
                            scan_literal_subject, bytes,
                            (string_address)"byte");
        log_flush();

        return 0;
}
#endif /* BENCH_scan_literal */

#ifdef BENCH_random
/* random() draw cost against call/control and additive-ring floors. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define RANDOM_BENCH_ROUNDS (1u << 22)
#define RANDOM_BENCH_TRIES 7
#define RANDOM_BENCH_DEGREE 31

static volatile positive random_bench_sink;
static p32 random_bench_state[RANDOM_BENCH_DEGREE];
static positive random_bench_front = 3;
static positive random_bench_rear;

static b32 (*volatile random_bench_call)() = random;

static __attribute__((noinline, noclone)) b32 random_bench_empty()
{
        return 1;
}

static b32 (*volatile random_bench_control_call)() = random_bench_empty;

static __attribute__((noinline, noclone)) b32 random_bench_floor_draw()
{
        p32 sum = random_bench_state[random_bench_front] +
                  random_bench_state[random_bench_rear];

        random_bench_state[random_bench_front] = sum;
        random_bench_front++;
        random_bench_rear++;

        if (random_bench_front == RANDOM_BENCH_DEGREE)
                random_bench_front = 0;
        if (random_bench_rear == RANDOM_BENCH_DEGREE)
                random_bench_rear = 0;

        return (b32)(sum >> 1);
}

static b32 (*volatile random_bench_floor_call)() = random_bench_floor_draw;

static fn random_bench_control()
{
        for (positive i = 0; i < RANDOM_BENCH_ROUNDS; i++)
                random_bench_sink += (positive)random_bench_control_call();
}

static fn random_bench_floor()
{
        for (positive i = 0; i < RANDOM_BENCH_ROUNDS; i++)
                random_bench_sink += (positive)random_bench_floor_call();
}

static fn random_bench_subject()
{
        for (positive i = 0; i < RANDOM_BENCH_ROUNDS; i++)
                random_bench_sink += (positive)random_bench_call();
}

static fn random_bench_report(string_address name, bench_work work)
{
        bench_report(name, work, RANDOM_BENCH_TRIES,
                     RANDOM_BENCH_ROUNDS, (string_address)"draw");
}

static bench_work random_bench_named(string_address name)
{
        BENCH_NAME(name, "control", random_bench_control);
        BENCH_NAME(name, "floor", random_bench_floor);
        BENCH_NAME(name, "subject", random_bench_subject);

        return null;
}

b32 main(void)
{
        for (positive i = 0; i < RANDOM_BENCH_DEGREE; i++)
                random_bench_state[i] = (p32)(i * 1103515245u + 12345u);

        srandom(1);

        BENCH_RUN_NAMED(random_bench_named);

        string_format(log, "random draw, best of %p (%p rounds)\n",
                      (positive)RANDOM_BENCH_TRIES,
                      (positive)RANDOM_BENCH_ROUNDS);
        random_bench_report((string_address)"empty ABI control",
                            random_bench_control);
        random_bench_report((string_address)"additive ring floor",
                            random_bench_floor);
        random_bench_report((string_address)"random", random_bench_subject);
        log_flush();

        return 0;
}
#endif /* BENCH_random */

#ifdef BENCH_montgomery
/*
        The generic Montgomery multiply and square against the C crypto.c
        ran them in, at the limb counts that reach them: 4 and 6 for the
        P-256 and P-384 group orders inside ECDSA verification, 32 and 64
        for RSA-2048 and RSA-4096. Then the verifies around them, through
        crypto.c as it stands.

            sh test/run bench montgomery
            sh test/run bench montgomery -- multiply-c 32 200000

        A row named on the command line runs alone for that many rounds and
        prints nothing, which is what perf stat -e instructions:u,cycles:u
        is pointed at: multiply-c, multiply, square-c and square take a limb
        count, and ecdsa-p256, ecdsa-p384, rsa-2048, rsa-4096, x25519,
        x25519-c (the C crypto.c ran before lib.c's x25519, at the same
        caller), x25519-mulq (x86_64's body without BMI2 and ADX), p256-public, p256-shared, p384-public, p384-shared and gcm
        take only the rounds. The RSA rows are the public operation a verify runs,
        crypto_rsa_modexp with e = 65537, over a fixed odd modulus whose top
        bit is set; the ECDSA rows verify real signatures, RFC 6979's P-256
        "sample" and a P-384 one made once with openssl.
*/
#include "../../src/lib.util.c"
#include "../../src/net/net.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure
#define SHARED_montgomery_reference
#include "../checks.c"
#undef SHARED_montgomery_reference
#define SHARED_x25519_reference
#include "../checks.c"
#undef SHARED_x25519_reference

#define MONTGOMERY_BENCH_TRIES 7

typedef fn (*montgomery_bench_multiply_body)(p64 address_to,
                                             const p64 address_to,
                                             const p64 address_to,
                                             const p64 address_to, p64,
                                             positive);
typedef fn (*montgomery_bench_square_body)(p64 address_to,
                                           const p64 address_to,
                                           const p64 address_to, p64,
                                           positive);

static montgomery_bench_multiply_body volatile montgomery_bench_multiply_c =
    montgomery_reference_multiply;
static montgomery_bench_square_body volatile montgomery_bench_square_c =
    montgomery_reference_square;
static montgomery_bench_multiply_body volatile montgomery_bench_multiply_asm =
    montgomery_multiply;

static volatile p64 montgomery_bench_sink;
static p64 montgomery_bench_m[64];
static p64 montgomery_bench_a[64];
static p64 montgomery_bench_b[64];
static p64 montgomery_bench_d[64];
static p64 montgomery_bench_inverse;
static positive montgomery_bench_n = 4;
static positive montgomery_bench_rounds = 1;

static const p8 montgomery_bench_p256_qx[32] = {
    0x60, 0xfe, 0xd4, 0xba, 0x25, 0x5a, 0x9d, 0x31, 0xc9, 0x61, 0xeb,
    0x74, 0xc6, 0x35, 0x6d, 0x68, 0xc0, 0x49, 0xb8, 0x92, 0x3b, 0x61,
    0xfa, 0x6c, 0xe6, 0x69, 0x62, 0x2e, 0x60, 0xf2, 0x9f, 0xb6};
static const p8 montgomery_bench_p256_qy[32] = {
    0x79, 0x03, 0xfe, 0x10, 0x08, 0xb8, 0xbc, 0x99, 0xa4, 0x1a, 0xe9,
    0xe9, 0x56, 0x28, 0xbc, 0x64, 0xf2, 0xf1, 0xb2, 0x0c, 0x2d, 0x7e,
    0x9f, 0x51, 0x77, 0xa3, 0xc2, 0x94, 0xd4, 0x46, 0x22, 0x99};
static const p8 montgomery_bench_p256_r[32] = {
    0xef, 0xd4, 0x8b, 0x2a, 0xac, 0xb6, 0xa8, 0xfd, 0x11, 0x40, 0xdd,
    0x9c, 0xd4, 0x5e, 0x81, 0xd6, 0x9d, 0x2c, 0x87, 0x7b, 0x56, 0xaa,
    0xf9, 0x91, 0xc3, 0x4d, 0x0e, 0xa8, 0x4e, 0xaf, 0x37, 0x16};
static const p8 montgomery_bench_p256_s[32] = {
    0xf7, 0xcb, 0x1c, 0x94, 0x2d, 0x65, 0x7c, 0x41, 0xd4, 0x36, 0xc7,
    0xa1, 0xb6, 0xe2, 0x9f, 0x65, 0xf3, 0xe9, 0x00, 0xdb, 0xb9, 0xaf,
    0xf4, 0x06, 0x4d, 0xc4, 0xab, 0x2f, 0x84, 0x3a, 0xcd, 0xa8};
static const char montgomery_bench_p384_hex[] =
    /* the public point x, y */
    "ea8636cba0d1352aae4e9ecde992fd91bdb010ddf67e6aef0b91cb218fdc9c13"
    "3e8c81a4632080fe9583d00817f677c9ca5f0368605d161b3410f3ac507279c1"
    "99834c9ab12289010a7f8c0d1c0edb6c9d494b34892b26979bfc07fb2bd1e654"
    /* r, s */
    "bf0437db6a0149c023aedd2005b069a2fc1a0e14c8b8177c77d27b2ac66c0e04"
    "dc8309648e4fa734103e48ddfe3409880d8f773f442ce89ba14fae047ee04e48"
    "3022e03b7a9215b8876eec9dcc0e3711448d2adbe7aac19a74c7d7b11268c8b8"
    /* SHA-384 of "sample" */
    "9a9083505bc92276aec4be312696ef7bf3bf603f4bbd381196a029f340585312"
    "313bca4a9b5b890efee42c77b1ee25fe";
static p8 montgomery_bench_p256_digest[32];
static p8 montgomery_bench_p384[48 * 5];

static p64 montgomery_bench_draw(p64 address_to state)
{
        XORSHIFT64(address_to state);
        return address_to state;
}

//      The modulus and operands for n limbs: the group order at 4 and 6,
//      otherwise a drawn odd modulus with its top bit set. Operands have
//      their top bit clear, so both sit below the modulus.
static fn montgomery_bench_operands(positive n)
{
        p64 state = 0x9e3779b97f4a7c15ull ^ n;
        p64 inverse;

        for (positive i = 0; i < 64; i++)
        {
                montgomery_bench_m[i] = montgomery_bench_draw(address_of state);
                montgomery_bench_a[i] = montgomery_bench_draw(address_of state);
                montgomery_bench_b[i] = montgomery_bench_draw(address_of state);
        }
        if (n == 4)
                memory_copy(montgomery_bench_m, crypto_p256_n, 4 * 8);
        else if (n == 6)
                memory_copy(montgomery_bench_m, crypto_p384_n, 6 * 8);
        montgomery_bench_m[n - 1] |= 1ull << 63;
        montgomery_bench_m[0] |= 1;
        montgomery_bench_a[n - 1] >>= 1;
        montgomery_bench_b[n - 1] >>= 1;

        inverse = montgomery_bench_m[0];
        for (positive i = 0; i < 5; i++)
                inverse *= 2 - montgomery_bench_m[0] * inverse;
        montgomery_bench_inverse = 0 - inverse;
        montgomery_bench_n = n;
}

static fn montgomery_bench_multiply_c_row(void)
{
        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                montgomery_bench_multiply_c(
                    montgomery_bench_d, montgomery_bench_a, montgomery_bench_b,
                    montgomery_bench_m, montgomery_bench_inverse,
                    montgomery_bench_n);
                montgomery_bench_sink += montgomery_bench_d[0];
        }
}

static fn montgomery_bench_square_c_row(void)
{
        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                montgomery_bench_square_c(montgomery_bench_d, montgomery_bench_a,
                                          montgomery_bench_m,
                                          montgomery_bench_inverse,
                                          montgomery_bench_n);
                montgomery_bench_sink += montgomery_bench_d[0];
        }
}

static fn montgomery_bench_multiply_row(void)
{
        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                montgomery_bench_multiply_asm(
                    montgomery_bench_d, montgomery_bench_a, montgomery_bench_b,
                    montgomery_bench_m, montgomery_bench_inverse,
                    montgomery_bench_n);
                montgomery_bench_sink += montgomery_bench_d[0];
        }
}

static fn montgomery_bench_square_row(void)
{
        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                montgomery_bench_multiply_asm(
                    montgomery_bench_d, montgomery_bench_a, montgomery_bench_a,
                    montgomery_bench_m, montgomery_bench_inverse,
                    montgomery_bench_n);
                montgomery_bench_sink += montgomery_bench_d[0];
        }
}

static fn montgomery_bench_ecdsa_p256_row(void)
{
        for (positive i = 0; i < montgomery_bench_rounds; i++)
                montgomery_bench_sink += crypto_ecdsa_p256(
                    montgomery_bench_p256_digest, 32,
                    (p8 address_to)montgomery_bench_p256_r, 32,
                    (p8 address_to)montgomery_bench_p256_s, 32,
                    (p8 address_to)montgomery_bench_p256_qx,
                    (p8 address_to)montgomery_bench_p256_qy);
}

static fn montgomery_bench_ecdsa_p384_row(void)
{
        p8 address_to v = montgomery_bench_p384;

        for (positive i = 0; i < montgomery_bench_rounds; i++)
                montgomery_bench_sink += crypto_ecdsa_p384(
                    v + 48 * 4, 48, v + 48 * 2, 48, v + 48 * 3, 48, v,
                    v + 48);
}

static fn montgomery_bench_rsa_row(void)
{
        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                crypto_rsa_modexp(montgomery_bench_d, montgomery_bench_a,
                                  65537, montgomery_bench_m,
                                  montgomery_bench_n);
                montgomery_bench_sink += montgomery_bench_d[0];
        }
}

//      The key exchange a handshake runs: one X25519 and the P-256 and
//      P-384 key shares made, then one shared secret on each curve. The
//      peer point is the share made from the same fixed scalar, so the
//      shared rows multiply a real curve point. gcm seals one 16 KiB
//      record in place.
static p8 montgomery_bench_scalar[48];
static p8 montgomery_bench_peer[97];
static p8 montgomery_bench_record[16384];
static crypto_aesgcm_key montgomery_bench_gcm;

static fn montgomery_bench_x25519_row(void)
{
        static const p8 nine[32] = {9};
        p8 out[32];

        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                crypto_x25519(out, montgomery_bench_scalar, (p8 address_to)nine);
                montgomery_bench_sink += out[0];
        }
}

//      The same through the C crypto.c ran before lib.c's x25519, and the
//      same verdict over its answer, so the pair differs in the arithmetic
//      alone.
static fn montgomery_bench_x25519_c_row(void)
{
        static const p8 nine[32] = {9};
        p8 out[32];

        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                p8 nonzero = 0;

                x25519_reference(out, montgomery_bench_scalar, nine);
                for (positive j = 0; j < 32; j++)
                        nonzero |= out[j];
                montgomery_bench_sink += out[0] + (nonzero != 0);
        }
}

static fn montgomery_bench_p256_public_row(void)
{
        p8 out[65];

        for (positive i = 0; i < montgomery_bench_rounds; i++)
                montgomery_bench_sink += crypto_ecdh_p256_public(
                    out, montgomery_bench_scalar);
}

static fn montgomery_bench_p384_public_row(void)
{
        p8 out[97];

        for (positive i = 0; i < montgomery_bench_rounds; i++)
                montgomery_bench_sink += crypto_ecdh_p384_public(
                    out, montgomery_bench_scalar);
}

static fn montgomery_bench_p256_shared_row(void)
{
        p8 out[32];

        crypto_ecdh_p256_public(montgomery_bench_peer, montgomery_bench_scalar);
        for (positive i = 0; i < montgomery_bench_rounds; i++)
                montgomery_bench_sink += crypto_ecdh_p256_shared(
                    out, montgomery_bench_scalar, montgomery_bench_peer);
}

static fn montgomery_bench_p384_shared_row(void)
{
        p8 out[48];

        crypto_ecdh_p384_public(montgomery_bench_peer, montgomery_bench_scalar);
        for (positive i = 0; i < montgomery_bench_rounds; i++)
                montgomery_bench_sink += crypto_ecdh_p384_shared(
                    out, montgomery_bench_scalar, montgomery_bench_peer);
}

static fn montgomery_bench_gcm_row(void)
{
        static const p8 iv[12] = {1, 2, 3};
        static const p8 aad[5] = {23, 3, 3, 0x40, 0x11};
        p8 tag[16];

        for (positive i = 0; i < montgomery_bench_rounds; i++)
        {
                crypto_aesgcm_seal(address_of montgomery_bench_gcm,
                                   (p8 address_to)iv, (p8 address_to)aad, 5,
                                   montgomery_bench_record,
                                   sizeof montgomery_bench_record, tag);
                montgomery_bench_sink += tag[0];
        }
}

static positive montgomery_bench_number(string_address text)
{
        positive value = 0;

        while (address_to text >= '0' && address_to text <= '9')
                value = value * 10 + (positive)(address_to text++ - '0');
        return value;
}

static fn montgomery_bench_report(string_address name, bench_work work,
                                  positive rounds, string_address unit)
{
        montgomery_bench_rounds = rounds;
        bench_report(name, work, MONTGOMERY_BENCH_TRIES, rounds, unit);
}

b32 main(void)
{
        static const positive limbs[] = {4, 6, 32, 64};
        positive arguments = program_argument_count();

        crypto_sha256_of((p8 address_to)"sample", 6,
                         montgomery_bench_p256_digest);
        for (positive i = 0; i < sizeof montgomery_bench_p384; i++)
        {
                p8 high = (p8)montgomery_bench_p384_hex[i * 2];
                p8 low = (p8)montgomery_bench_p384_hex[i * 2 + 1];

                high = (p8)(high <= '9' ? high - '0' : high - 'a' + 10);
                low = (p8)(low <= '9' ? low - '0' : low - 'a' + 10);
                montgomery_bench_p384[i] = (p8)(high << 4 | low);
        }
        montgomery_bench_operands(4);
        for (positive i = 0; i < sizeof montgomery_bench_scalar; i++)
                montgomery_bench_scalar[i] = (p8)(0x5a ^ (i * 29));
        montgomery_bench_scalar[0] = 0x1f;
        crypto_aesgcm_prepare(address_of montgomery_bench_gcm,
                              montgomery_bench_scalar);

        if (arguments > 1)
        {
                string_address row = program_argument(1);
                positive first =
                    arguments > 2 ? montgomery_bench_number(program_argument(2))
                                  : 0;
                positive second =
                    arguments > 3 ? montgomery_bench_number(program_argument(3))
                                  : 0;
                bench_work work = null;

                if (string_compare(row, (string_address)"multiply-c") == 0)
                        work = montgomery_bench_multiply_c_row;
                else if (string_compare(row, (string_address)"multiply") == 0)
                        work = montgomery_bench_multiply_row;
                else if (string_compare(row, (string_address)"square") == 0)
                        work = montgomery_bench_square_row;
                else if (string_compare(row, (string_address)"square-c") == 0)
                        work = montgomery_bench_square_c_row;

                if (work)
                {
                        if (first != 4 && first != 6 && first != 32 &&
                            first != 64)
                                return 2;
                        montgomery_bench_operands(first);
                        montgomery_bench_rounds = second ? second : 1;
                        work();
                        return 0;
                }

                montgomery_bench_rounds = first ? first : 1;
                if (string_compare(row, (string_address)"ecdsa-p256") == 0)
                        work = montgomery_bench_ecdsa_p256_row;
                else if (string_compare(row, (string_address)"ecdsa-p384") == 0)
                        work = montgomery_bench_ecdsa_p384_row;
                else if (string_compare(row, (string_address)"x25519") == 0)
                        work = montgomery_bench_x25519_row;
                else if (string_compare(row, (string_address)"x25519-c") == 0)
                        work = montgomery_bench_x25519_c_row;
                else if (string_compare(row, (string_address)"x25519-mulq") == 0)
                {
                        //      The body a processor without BMI2 and ADX runs,
                        //      after one call has asked the processor.
                        p8 probe[32];

                        x25519(probe, montgomery_bench_scalar, montgomery_bench_scalar);
                        cpu_has_mulx = 0;
                        work = montgomery_bench_x25519_row;
                }
                else if (string_compare(row, (string_address)"p256-public") == 0)
                        work = montgomery_bench_p256_public_row;
                else if (string_compare(row, (string_address)"p256-shared") == 0)
                        work = montgomery_bench_p256_shared_row;
                else if (string_compare(row, (string_address)"p384-public") == 0)
                        work = montgomery_bench_p384_public_row;
                else if (string_compare(row, (string_address)"p384-shared") == 0)
                        work = montgomery_bench_p384_shared_row;
                else if (string_compare(row, (string_address)"gcm") == 0)
                        work = montgomery_bench_gcm_row;
                else if (string_compare(row, (string_address)"rsa-2048") == 0)
                {
                        montgomery_bench_operands(32);
                        work = montgomery_bench_rsa_row;
                }
                else if (string_compare(row, (string_address)"rsa-4096") == 0)
                {
                        montgomery_bench_operands(64);
                        work = montgomery_bench_rsa_row;
                }
                if (is_null(work))
                        return 2;
                work();
                return 0;
        }

        string_format(log, "Montgomery arithmetic, best of %p\n",
                      (positive)MONTGOMERY_BENCH_TRIES);
        montgomery_bench_sink = 0;
        montgomery_bench_rounds = 1;
        montgomery_bench_ecdsa_p256_row();
        montgomery_bench_ecdsa_p384_row();
        if (montgomery_bench_sink != 2)
                string_format(log, " the ECDSA signatures do not verify\n");
        for (positive at = 0; at < sizeof limbs / sizeof limbs[0]; at++)
        {
                positive n = limbs[at];
                positive rounds = (1u << 22) / (n * n);

                montgomery_bench_operands(n);
                string_format(log, " %p limbs, %p calls a try\n", n, rounds);
                montgomery_bench_report((string_address)"multiply, C     ",
                                        montgomery_bench_multiply_c_row,
                                        rounds, (string_address)"call");
                montgomery_bench_report((string_address)"multiply, asm   ",
                                        montgomery_bench_multiply_row,
                                        rounds, (string_address)"call");
                montgomery_bench_report((string_address)"square, C       ",
                                        montgomery_bench_square_c_row,
                                        rounds, (string_address)"call");
                montgomery_bench_report((string_address)"square, asm     ",
                                        montgomery_bench_square_row,
                                        rounds, (string_address)"call");
        }

        string_format(log, " verifies through crypto.c\n");
        montgomery_bench_report((string_address)"ECDSA P-256", 
                                montgomery_bench_ecdsa_p256_row, 64,
                                (string_address)"verify");
        montgomery_bench_report((string_address)"ECDSA P-384",
                                montgomery_bench_ecdsa_p384_row, 32,
                                (string_address)"verify");
        string_format(log, " key exchange and one record\n");
        montgomery_bench_report((string_address)"X25519, C  ",
                                montgomery_bench_x25519_c_row, 64,
                                (string_address)"multiply");
        montgomery_bench_report((string_address)"X25519     ",
                                montgomery_bench_x25519_row, 64,
                                (string_address)"multiply");
        montgomery_bench_report((string_address)"P-256 share",
                                montgomery_bench_p256_public_row, 64,
                                (string_address)"share");
        montgomery_bench_report((string_address)"P-256 ECDH ",
                                montgomery_bench_p256_shared_row, 32,
                                (string_address)"secret");
        montgomery_bench_report((string_address)"P-384 share",
                                montgomery_bench_p384_public_row, 32,
                                (string_address)"share");
        montgomery_bench_report((string_address)"P-384 ECDH ",
                                montgomery_bench_p384_shared_row, 16,
                                (string_address)"secret");
        montgomery_bench_report((string_address)"AES-GCM 16K",
                                montgomery_bench_gcm_row, 64,
                                (string_address)"record");
        montgomery_bench_operands(32);
        montgomery_bench_report((string_address)"RSA-2048 public",
                                montgomery_bench_rsa_row, 256,
                                (string_address)"verify");
        montgomery_bench_operands(64);
        montgomery_bench_report((string_address)"RSA-4096 public",
                                montgomery_bench_rsa_row, 64,
                                (string_address)"verify");
        log_flush();
        return 0;
}
#endif /* BENCH_montgomery */

#ifdef BENCH_limbs
/*
        The limb family against the C its callers ran, at the sizes they
        run it: a basecase product of n by n limbs (base58's
        big_multiply_into below BIG_KARATSUBA, 32, one row a limb of b) with
        text.c's C rows against the rows as limbs_multiply_word and
        limbs_add_multiply_word calls, one row alone, the add, subtract and
        compare over n limbs, and the 128/64 divide against the bit-serial
        loop numfmt and factor ran.

            sh test/run bench limbs
            sh test/run bench limbs -- basecase-c 16 100000

        A row named on the command line runs alone for that many rounds and
        prints nothing, for perf stat -e instructions:u,cycles:u: basecase,
        addmul, multiply, add, subtract and compare each with -c for the C,
        taking a limb count, and divide and divide-c taking only the rounds.
*/
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define LIMBS_BENCH_TRIES 7

static p64 limbs_bench_a[128], limbs_bench_b[128], limbs_bench_r[256];
static positive limbs_bench_n = 4;
static positive limbs_bench_rounds = 1;
static volatile p64 limbs_bench_sink;

// text.c's basecase before the fold, rows and all.
static __attribute__((noinline)) fn limbs_bench_basecase_c(p64 address_to r, const p64 address_to a,
                                          positive an, const p64 address_to b,
                                          positive bn)
{
        memory_fill(r, 0, (an + bn) * sizeof(p64));
        for (positive i = 0; i < bn; i++)
        {
                p64 carry = 0;

                for (positive j = 0; j < an; j++)
                {
                        unsigned __int128 t = (unsigned __int128)a[j] * b[i] +
                                              r[i + j] + carry;

                        r[i + j] = (p64)t;
                        carry = (p64)(t >> 64);
                }
                r[i + an] = carry;
        }
}

static __attribute__((noinline)) fn limbs_bench_basecase(p64 address_to r, const p64 address_to a,
                                        positive an, const p64 address_to b,
                                        positive bn)
{
        r[an] = limbs_multiply_word(r, a, an, b[0], 0);
        for (positive i = 1; i < bn; i++)
                r[i + an] = limbs_add_multiply_word(r + i, a, an, b[i]);
}

static __attribute__((noinline)) p64 limbs_bench_addmul_c(p64 address_to r, const p64 address_to a,
                                         positive n, p64 w)
{
        p64 carry = 0;

        for (positive j = 0; j < n; j++)
        {
                unsigned __int128 t = (unsigned __int128)a[j] * w + r[j] + carry;

                r[j] = (p64)t;
                carry = (p64)(t >> 64);
        }
        return carry;
}

static __attribute__((noinline)) p64 limbs_bench_multiply_c(p64 address_to r, const p64 address_to a,
                                           positive n, p64 w, p64 carry)
{
        for (positive j = 0; j < n; j++)
        {
                unsigned __int128 t = (unsigned __int128)a[j] * w + carry;

                r[j] = (p64)t;
                carry = (p64)(t >> 64);
        }
        return carry;
}

static __attribute__((noinline)) p64 limbs_bench_add_c(p64 address_to a, positive n,
                                      const p64 address_to b, positive m)
{
        p64 carry = 0;
        positive at = 0;

        for (; at < m; at++)
        {
                p64 sum = a[at] + carry;

                carry = sum < carry;
                sum += b[at];
                carry += sum < b[at];
                a[at] = sum;
        }
        for (; carry && at < n; at++)
                carry = !++a[at];
        return carry;
}

static __attribute__((noinline)) p64 limbs_bench_subtract_c(p64 address_to a, positive n,
                                           const p64 address_to b, positive m)
{
        p64 borrow = 0;
        positive at = 0;

        for (; at < m; at++)
        {
                p64 x = a[at];
                p64 y = b[at];

                a[at] = x - y - borrow;
                borrow = (x < y) | ((x == y) & borrow);
        }
        for (; borrow && at < n; at++)
                borrow = !a[at]--;
        return borrow;
}

static __attribute__((noinline)) bipolar limbs_bench_compare_c(const p64 address_to a,
                                              const p64 address_to b, positive n)
{
        while (n--)
                if (a[n] != b[n])
                        return a[n] < b[n] ? -1 : 1;
        return 0;
}

// numfmt_wide_divide's loop, cut to the 128/64 shape.
static __attribute__((noinline)) positive2 limbs_bench_divide_c(p64 high, p64 low, p64 divisor)
{
        unsigned __int128 rest = (unsigned __int128)high << 64 | low;
        unsigned __int128 d = (unsigned __int128)divisor << 64;
        p64 quotient = 0;

        for (b32 bit = 0; bit < 64; bit++)
        {
                rest <<= 1;
                quotient <<= 1;
                if (rest >= d)
                {
                        rest -= d;
                        quotient |= 1;
                }
        }
        return (positive2){{quotient, (p64)(rest >> 64)}};
}

static fn limbs_bench_basecase_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
        {
                limbs_bench_basecase_c(limbs_bench_r, limbs_bench_a, limbs_bench_n,
                                       limbs_bench_b, limbs_bench_n);
                limbs_bench_sink += limbs_bench_r[limbs_bench_n];
        }
}

static fn limbs_bench_basecase_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
        {
                limbs_bench_basecase(limbs_bench_r, limbs_bench_a, limbs_bench_n,
                                     limbs_bench_b, limbs_bench_n);
                limbs_bench_sink += limbs_bench_r[limbs_bench_n];
        }
}

static fn limbs_bench_addmul_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_bench_addmul_c(limbs_bench_r, limbs_bench_a,
                                                         limbs_bench_n, limbs_bench_b[i & 63]);
}

static fn limbs_bench_addmul_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_add_multiply_word(limbs_bench_r, limbs_bench_a,
                                                            limbs_bench_n, limbs_bench_b[i & 63]);
}

static fn limbs_bench_multiply_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_bench_multiply_c(limbs_bench_r, limbs_bench_a,
                                                           limbs_bench_n, limbs_bench_b[i & 63], i);
}

static fn limbs_bench_multiply_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_multiply_word(limbs_bench_r, limbs_bench_a,
                                                        limbs_bench_n, limbs_bench_b[i & 63], i);
}

static fn limbs_bench_add_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_bench_add_c(limbs_bench_r, limbs_bench_n,
                                                      limbs_bench_a, limbs_bench_n);
}

static fn limbs_bench_add_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_add(limbs_bench_r, limbs_bench_n, limbs_bench_a,
                                              limbs_bench_n);
}

static fn limbs_bench_subtract_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_bench_subtract_c(limbs_bench_r, limbs_bench_n,
                                                           limbs_bench_a, limbs_bench_n);
}

static fn limbs_bench_subtract_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += limbs_subtract(limbs_bench_r, limbs_bench_n,
                                                   limbs_bench_a, limbs_bench_n);
}

static fn limbs_bench_compare_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += (p64)limbs_bench_compare_c(limbs_bench_a, limbs_bench_b,
                                                               limbs_bench_n);
}

static fn limbs_bench_compare_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
                limbs_bench_sink += (p64)limbs_compare(limbs_bench_a, limbs_bench_b,
                                                       limbs_bench_n);
}

// Normalized divisors, as numfmt's significands are, and random ones.
static fn limbs_bench_divide_c_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
        {
                p64 d = limbs_bench_a[i & 127] >> (limbs_bench_n & 63) | 1;

                limbs_bench_sink += limbs_bench_divide_c(limbs_bench_b[i & 127] % d,
                                                         limbs_bench_r[i & 255], d).x;
        }
}

static fn limbs_bench_divide_row(void)
{
        for (positive i = 0; i < limbs_bench_rounds; i++)
        {
                p64 d = limbs_bench_a[i & 127] >> (limbs_bench_n & 63) | 1;

                limbs_bench_sink += positive_divide_wide(limbs_bench_b[i & 127] % d,
                                                         limbs_bench_r[i & 255], d).x;
        }
}

static fn limbs_bench_operands(positive n)
{
        p64 state = 0x9e3779b97f4a7c15ull ^ n;

        for (positive i = 0; i < 256; i++)
        {
                XORSHIFT64(state);
                if (i < 128)
                {
                        limbs_bench_a[i] = state | (p64)1 << 63;
                        limbs_bench_b[i] = state * 0x9e3779b97f4a7c15ull;
                }
                limbs_bench_r[i] = state ^ (state >> 29);
        }
        // compare: equal but for the lowest limb, so the whole of n is walked
        memory_copy(limbs_bench_b, limbs_bench_a, n * sizeof(p64));
        limbs_bench_b[0] ^= 1;
        limbs_bench_n = n;
}

static positive limbs_bench_number(string_address text)
{
        positive value = 0;

        while (address_to text >= '0' && address_to text <= '9')
                value = value * 10 + (positive)(address_to text++ - '0');
        return value;
}

static const struct
{
        string_address name;
        bench_work work;
} limbs_bench_rows[] = {
    {"basecase-c", limbs_bench_basecase_c_row}, {"basecase", limbs_bench_basecase_row},
    {"addmul-c", limbs_bench_addmul_c_row},     {"addmul", limbs_bench_addmul_row},
    {"multiply-c", limbs_bench_multiply_c_row}, {"multiply", limbs_bench_multiply_row},
    {"add-c", limbs_bench_add_c_row},           {"add", limbs_bench_add_row},
    {"subtract-c", limbs_bench_subtract_c_row}, {"subtract", limbs_bench_subtract_row},
    {"compare-c", limbs_bench_compare_c_row},   {"compare", limbs_bench_compare_row},
    {"divide-c", limbs_bench_divide_c_row},     {"divide", limbs_bench_divide_row},
};

b32 main(void)
{
        static const positive limbs[] = {4, 8, 16, 31, 64};
        positive arguments = program_argument_count();
        positive rows = sizeof limbs_bench_rows / sizeof limbs_bench_rows[0];

        if (arguments > 1)
        {
                string_address row = program_argument(1);
                positive first = arguments > 2 ? limbs_bench_number(program_argument(2)) : 0;
                positive second = arguments > 3 ? limbs_bench_number(program_argument(3)) : 0;

                for (positive at = 0; at < rows; at++)
                        if (string_compare(row, limbs_bench_rows[at].name) == 0)
                        {
                                if (at >= 12)
                                {
                                        limbs_bench_operands(0);
                                        limbs_bench_rounds = first ? first : 1;
                                }
                                else
                                {
                                        if (!first || first > 64)
                                                return 2;
                                        limbs_bench_operands(first);
                                        limbs_bench_rounds = second ? second : 1;
                                }
                                limbs_bench_rows[at].work();
                                return 0;
                        }
                return 2;
        }

        string_format(log, "Limb arithmetic, best of %p\n", (positive)LIMBS_BENCH_TRIES);
        for (positive at = 0; at < sizeof limbs / sizeof limbs[0]; at++)
        {
                positive n = limbs[at];

                limbs_bench_operands(n);
                string_format(log, " %p limbs\n", n);
                for (positive row = 0; row < 12; row++)
                {
                        limbs_bench_rounds = row < 2 ? (1u << 20) / (n * n) : (1u << 20) / n;
                        bench_report(limbs_bench_rows[row].name, limbs_bench_rows[row].work,
                                     LIMBS_BENCH_TRIES, limbs_bench_rounds,
                                     (string_address)"call");
                }
        }
        limbs_bench_operands(0);
        limbs_bench_rounds = 1u << 18;
        string_format(log, " 128/64 divide\n");
        bench_report("divide-c", limbs_bench_divide_c_row, LIMBS_BENCH_TRIES,
                     limbs_bench_rounds, (string_address)"call");
        bench_report("divide", limbs_bench_divide_row, LIMBS_BENCH_TRIES,
                     limbs_bench_rounds, (string_address)"call");
        log_flush();
        return 0;
}
#endif /* BENCH_limbs */

#ifdef BENCH_allocator
/* malloc/free class fast path against its call and free-list traffic floors.
   The subjects are memory_take and memory_give, which malloc and free are
   second labels on: what the pair times is the shelf pop and the shelf push
   in lib.c, with refill, mapping and every tag those two do not own
   staying in the C that their miss paths jump to. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define ALLOCATOR_BENCH_ROUNDS (1u << 22)
#define ALLOCATOR_BENCH_TRIES 7
#define ALLOCATOR_BENCH_SIZE 64

typedef address_any (*allocator_bench_take_call)(positive);
typedef fn (*allocator_bench_give_call)(address_any);

static volatile positive allocator_bench_sink;

/* One header word and a payload, aligned as a real class block is. */
static positive allocator_bench_floor_storage[12]
        __attribute__((aligned(ALLOCATOR_ALIGNMENT)));
static address_any allocator_bench_floor_head;

static __attribute__((noinline, noclone)) address_any
allocator_bench_empty_take(positive bytes)
{
        (void)bytes;
        return (address_any)(allocator_bench_floor_storage + 2);
}

static __attribute__((noinline, noclone)) fn
allocator_bench_empty_give(address_any block)
{
        (void)block;
}

static __attribute__((noinline, noclone)) address_any
allocator_bench_floor_take(positive bytes)
{
        (void)bytes;
        address_any block = allocator_bench_floor_head;

        allocator_bench_floor_head = address_to allocator_link(block);
        address_to allocator_tag(block) = 4;
        return block;
}

static __attribute__((noinline, noclone)) fn
allocator_bench_floor_give(address_any block)
{
        address_to allocator_tag(block) = ALLOCATOR_FREED + 4;
        address_to allocator_link(block) = allocator_bench_floor_head;
        allocator_bench_floor_head = block;
}

static allocator_bench_take_call volatile allocator_bench_empty_take_call =
        allocator_bench_empty_take;
static allocator_bench_give_call volatile allocator_bench_empty_give_call =
        allocator_bench_empty_give;
static allocator_bench_take_call volatile allocator_bench_floor_take_call =
        allocator_bench_floor_take;
static allocator_bench_give_call volatile allocator_bench_floor_give_call =
        allocator_bench_floor_give;
static allocator_bench_take_call volatile allocator_bench_subject_take_call =
        malloc;
static allocator_bench_give_call volatile allocator_bench_subject_give_call =
        free;

static fn allocator_bench_control()
{
        for (positive i = 0; i < ALLOCATOR_BENCH_ROUNDS; i++)
        {
                address_any block =
                        allocator_bench_empty_take_call(ALLOCATOR_BENCH_SIZE);

                allocator_bench_sink += (positive)block;
                allocator_bench_empty_give_call(block);
        }
}

static fn allocator_bench_floor()
{
        for (positive i = 0; i < ALLOCATOR_BENCH_ROUNDS; i++)
        {
                address_any block =
                        allocator_bench_floor_take_call(ALLOCATOR_BENCH_SIZE);

                allocator_bench_sink += (positive)block;
                allocator_bench_floor_give_call(block);
        }
}

static fn allocator_bench_subject()
{
        for (positive i = 0; i < ALLOCATOR_BENCH_ROUNDS; i++)
        {
                address_any block =
                        allocator_bench_subject_take_call(ALLOCATOR_BENCH_SIZE);

                allocator_bench_sink += (positive)block;
                allocator_bench_subject_give_call(block);
        }
}

static fn allocator_bench_report(string_address name, bench_work work)
{
        bench_report(name, work, ALLOCATOR_BENCH_TRIES,
                     ALLOCATOR_BENCH_ROUNDS, (string_address)"pair");
}

static bench_work allocator_bench_named(string_address name)
{
        BENCH_NAME(name, "control", allocator_bench_control);
        BENCH_NAME(name, "floor", allocator_bench_floor);
        BENCH_NAME(name, "subject", allocator_bench_subject);

        return null;
}

b32 main(void)
{
        address_any floor_block =
                (address_any)(allocator_bench_floor_storage + 2);

        allocator_bench_floor_head = floor_block;
        address_to allocator_link(floor_block) = null;
        address_to allocator_tag(floor_block) = ALLOCATOR_FREED + 4;

        /* Warm the real class so the measured subject never reaches mmap. */
        address_any warm = malloc(ALLOCATOR_BENCH_SIZE);
        free(warm);

        BENCH_RUN_NAMED(allocator_bench_named);

        string_format(log, "malloc/free class fast path, best of %p (%p pairs)\n",
                      (positive)ALLOCATOR_BENCH_TRIES,
                      (positive)ALLOCATOR_BENCH_ROUNDS);
        allocator_bench_report((string_address)"empty ABI control",
                               allocator_bench_control);
        allocator_bench_report((string_address)"free-list traffic floor",
                               allocator_bench_floor);
        allocator_bench_report((string_address)"malloc + free",
                               allocator_bench_subject);
        log_flush();

        return 0;
}
#endif /* BENCH_allocator */

#ifdef BENCH_lock
/* The lock's uncontended take/release pair. Alone is the threads_live
   elision; company is the same pair with a parked thread alive, which is the
   atomic path. Every subject is reached through a volatile pointer so the
   floors pay the same call: two plain stores are the traffic floor, and the
   inline compare-and-swap and exchange standard.c used to be are the
   before. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define LOCK_BENCH_ROUNDS (1u << 24)
#define LOCK_BENCH_TRIES 7

typedef fn (*lock_bench_call)(lock address_to);

static volatile positive lock_bench_sink;
static lock lock_bench_word;
static volatile b32 lock_bench_parked;

static __attribute__((noinline, noclone)) fn
lock_bench_store_take(lock address_to it)
{
        it->word = LOCK_HELD;
}

static __attribute__((noinline, noclone)) fn
lock_bench_store_release(lock address_to it)
{
        it->word = LOCK_FREE;
}

static __attribute__((noinline, noclone)) fn
lock_bench_atomic_take(lock address_to it)
{
        __sync_bool_compare_and_swap(address_of it->word, LOCK_FREE, LOCK_HELD);
}

static __attribute__((noinline, noclone)) fn
lock_bench_atomic_release(lock address_to it)
{
        if (__atomic_exchange_n(address_of it->word, LOCK_FREE,
                                __ATOMIC_SEQ_CST) == LOCK_WAITED)
                lock_bench_sink++;
}

static lock_bench_call volatile lock_bench_take_call;
static lock_bench_call volatile lock_bench_release_call;

static fn lock_bench_pairs()
{
        for (positive i = 0; i < LOCK_BENCH_ROUNDS; i++)
        {
                lock_bench_take_call(address_of lock_bench_word);
                lock_bench_sink += (positive)lock_bench_word.word;
                lock_bench_release_call(address_of lock_bench_word);
        }
}

static fn lock_bench_parked_thread(address_any argument)
{
        (void)argument;

        while (atomic_load(address_of lock_bench_parked) == 0)
                thread_wait(address_of lock_bench_parked, 0);
}

static fn lock_bench_use(string_address name)
{
        lock_bench_take_call = lock_take;
        lock_bench_release_call = lock_release;

        if (string_compare(name, (string_address)"floor") == 0)
        {
                lock_bench_take_call = lock_bench_store_take;
                lock_bench_release_call = lock_bench_store_release;
        }
        else if (string_compare(name, (string_address)"atomic") == 0)
        {
                lock_bench_take_call = lock_bench_atomic_take;
                lock_bench_release_call = lock_bench_atomic_release;
        }
}

b32 main(void)
{
        thread address_to parked;

        if (program_argument_count() > 1)
        {
                string_address name = program_argument(1);

                if (string_compare(name, (string_address)"floor") != 0 &&
                    string_compare(name, (string_address)"atomic") != 0 &&
                    string_compare(name, (string_address)"alone") != 0 &&
                    string_compare(name, (string_address)"company") != 0)
                        return 2;

                parked = string_compare(name, (string_address)"company") == 0
                                 ? thread_start(lock_bench_parked_thread, null)
                                 : null;
                lock_bench_use(name);
                lock_bench_pairs();

                if (parked)
                {
                        atomic_exchange(address_of lock_bench_parked, 1);
                        thread_wake(address_of lock_bench_parked, 1);
                        thread_join(parked);
                }

                return 0;
        }

        string_format(log, "lock take/release pair, best of %p (%p pairs)\n",
                      (positive)LOCK_BENCH_TRIES, (positive)LOCK_BENCH_ROUNDS);

        lock_bench_use((string_address)"floor");
        bench_report((string_address)"two plain stores (floor)", lock_bench_pairs,
                     LOCK_BENCH_TRIES, LOCK_BENCH_ROUNDS, (string_address)"pair");
        lock_bench_use((string_address)"atomic");
        bench_report((string_address)"inline C compare-and-swap pair (before)",
                     lock_bench_pairs, LOCK_BENCH_TRIES, LOCK_BENCH_ROUNDS,
                     (string_address)"pair");
        lock_bench_use((string_address)"alone");
        bench_report((string_address)"lock_take + lock_release, alone",
                     lock_bench_pairs, LOCK_BENCH_TRIES, LOCK_BENCH_ROUNDS,
                     (string_address)"pair");

        parked = thread_start(lock_bench_parked_thread, null);
        bench_report((string_address)"lock_take + lock_release, with company",
                     lock_bench_pairs, LOCK_BENCH_TRIES, LOCK_BENCH_ROUNDS,
                     (string_address)"pair");

        if (parked)
        {
                atomic_exchange(address_of lock_bench_parked, 1);
                thread_wake(address_of lock_bench_parked, 1);
                thread_join(parked);
        }

        log_flush();

        return 0;
}
#endif /* BENCH_lock */

#ifdef BENCH_pool
/* How parallel_for scales. The work is fixed -- 256 jobs of 1 MiB, each
   hashed with memory_hash_33 four times over -- and the width is set with
   parallel_reset from 1 to the affinity width, so the only thing that moves
   is the number of threads. Every width must reach the same sum, which is
   checked, because a benchmark that is fast and wrong measured nothing.
   Scaling is only meaningful natively, on cores nothing else is using. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define POOL_BENCH_JOBS 256
#define POOL_BENCH_CHUNK (1u << 20)
#define POOL_BENCH_PASSES 4

static p8 address_to pool_bench_data;
static positive pool_bench_sums[POOL_BENCH_JOBS];

static fn pool_bench_job(address_any context, positive index)
{
        p8 address_to chunk = pool_bench_data + index * POOL_BENCH_CHUNK;
        positive sum = 0;
        positive pass;

        (void)context;

        for (pass = 0; pass < POOL_BENCH_PASSES; pass++)
                sum += memory_hash_33(chunk, POOL_BENCH_CHUNK);

        pool_bench_sums[index] = sum;
}

static positive pool_bench_clock(void)
{
        positive when[2] = {0, 0};

        system_call_2(syscall(clock_gettime), 1, (positive)address_of when);
        return when[0] * 1000000000ull + when[1];
}

static volatile positive pool_tiny_sum = 0;

//      A job with almost no work, so a small run's time is the run itself.
static fn pool_tiny_job(address_any context, positive index)
{
        (void)context;
        atomic_add(address_of pool_tiny_sum, index + 1);
}

//      Voluntary plus involuntary switches of every thread of this process
//      (RUSAGE_SELF sums the threads, the exited ones included).
static positive pool_bench_switches(void)
{
        positive usage[18] = {0};

        system_call_2(syscall(getrusage), 0, (positive)usage);
        return usage[16] + usage[17];
}

b32 main(void)
{
        positive limit;
        positive width;
        positive single = 0;
        positive reference = 0;
        positive at;

        pool_bench_data = (p8 address_to)memory(POOL_BENCH_JOBS * POOL_BENCH_CHUNK);

        if (!pool_bench_data || system_failed((positive)pool_bench_data))
                return 1;

        for (at = 0; at < POOL_BENCH_JOBS * POOL_BENCH_CHUNK; at++)
                pool_bench_data[at] = (p8)(at * 2654435761u >> 13);

        parallel_reset(0);
        limit = parallel_width();

        string_format(log, "parallel_for over %p x 1 MiB, best of 3, affinity width %p\n",
                      (positive)POOL_BENCH_JOBS, limit);

        for (width = 1;; width = width * 2 > limit ? limit : width * 2)
        {
                positive best = positive_max;
                positive round;
                positive total = 0;

                parallel_reset(width);

                for (round = 0; round < 3; round++)
                {
                        positive started = pool_bench_clock();

                        parallel_for(pool_bench_job, null, POOL_BENCH_JOBS, PARALLEL_SPREAD);
                        started = pool_bench_clock() - started;

                        if (started < best)
                                best = started;
                }

                for (at = 0; at < POOL_BENCH_JOBS; at++)
                        total += pool_bench_sums[at];

                if (width == 1)
                {
                        single = best;
                        reference = total;
                }

                string_format(log, "  width %p  %p us  speedup x%p.%p%s\n", width,
                              best / 1000, single / (best ? best : 1),
                              (single * 10 / (best ? best : 1)) % 10,
                              total == reference ? (string_address)"" : (string_address)"  WRONG SUM");

                if (width == limit)
                        break;
        }

        //      Small runs at the counts callers use: the compositor's bands
        //      are at most eight, and the terminal's are at most its own
        //      bands. Best of three, 2000 runs each, time per run and the
        //      switches the process makes per run.
        {
                static const positive small_counts[] = {1, 2, 4, 8, 64};
                positive kind;

                parallel_reset(limit);

                for (kind = 0; kind < sizeof(small_counts) / sizeof(small_counts[0]); kind++)
                {
                        positive best = positive_max;
                        positive best_switches = 0;
                        positive round2;

                        for (round2 = 0; round2 < 3; round2++)
                        {
                                positive runs = 2000;
                                positive k;
                                positive switches_before = pool_bench_switches();
                                positive started = pool_bench_clock();
                                positive spent;
                                positive switches;

                                for (k = 0; k < runs; k++)
                                        parallel_for(pool_tiny_job, null, small_counts[kind], PARALLEL_SPREAD);

                                spent = pool_bench_clock() - started;
                                switches = pool_bench_switches() - switches_before;

                                if (spent / runs < best)
                                {
                                        best = spent / runs;
                                        best_switches = switches / runs;
                                }
                        }

                        string_format(log, "  small run: count %p  %p ns per run  %p switches per run (width %p)\n",
                                      small_counts[kind], best, best_switches, limit);
                }
        }

        parallel_reset(0);
        log_flush();

        return 0;
}
#endif /* BENCH_pool */

#ifdef BENCH_ordered
/* parallel_ordered with outputs big enough that writing them is real work.
   heavy: 23 jobs, each filling 25 MiB with a generator run over it several
   times, about 110 ms apiece at width 1 -- the shape of a multi-block xz -d
   -- written to the file named by the first argument (default ordered.out
   under TMPDIR or /tmp); a second and third argument change the job count
   and the passes. light: 4096 jobs hashing 256 KiB each and handing
   back eight bytes, written to /dev/null. Each prints wall, user and system
   milliseconds, best wall of three, at the affinity width. */
#include "../../src/lib.util.c"

#define ORDERED_HEAVY_JOBS 23
#define ORDERED_HEAVY_BYTES (25u << 20)
#define ORDERED_HEAVY_PASSES 24
#define ORDERED_LIGHT_JOBS 4096
#define ORDERED_LIGHT_BYTES (256u << 10)

static bipolar ordered_bench_out;
static p8 address_to ordered_light_data;
static positive ordered_heavy_jobs = ORDERED_HEAVY_JOBS;
static positive ordered_heavy_passes = ORDERED_HEAVY_PASSES;

//      When each heavy job finished and when the sink took it, so the time a
//      finished output waited can be printed beside the wall clock.
#define ORDERED_TIMED_JOBS 256
static positive ordered_done[ORDERED_TIMED_JOBS];
static positive ordered_sunk[ORDERED_TIMED_JOBS];
static positive ordered_length[ORDERED_TIMED_JOBS];

static positive ordered_bench_clock(void);

static fn ordered_heavy_job(address_any context, positive index,
                           parallel_output address_to output)
{
        p8 address_to span = parallel_reserve(output, ORDERED_HEAVY_BYTES);
        positive pass;

        (void)context;

        if (!span)
                return;

        for (pass = 0; pass < ordered_heavy_passes; pass++)
        {
                positive state = 0x9e3779b97f4a7c15ull * (index + 1) + pass;
                positive at;

                for (at = 0; at + 8 <= ORDERED_HEAVY_BYTES; at += 8)
                {
                        positive word;

                        XORSHIFT64(state);
                        memory_copy(address_of word, span + at, 8);
                        word = pass ? word ^ state : state;
                        memory_copy(span + at, address_of word, 8);
                }
        }

        if (index < ORDERED_TIMED_JOBS)
                ordered_done[index] = ordered_bench_clock();
}

static fn ordered_light_job(address_any context, positive index,
                           parallel_output address_to output)
{
        positive sum = memory_hash_33(ordered_light_data + index % 64, ORDERED_LIGHT_BYTES);

        (void)context;
        parallel_write(output, address_of sum, 8);
}

static p8 ordered_bench_sink(address_any context, positive index,
                               address_any data, positive length)
{
        (void)context;

        if (index < ORDERED_TIMED_JOBS)
        {
                ordered_sunk[index] = ordered_bench_clock();
                ordered_length[index] = length;
        }

        return system_write_all((positive)ordered_bench_out, data, length) == length;
}

static positive ordered_bench_clock(void)
{
        positive when[2] = {0, 0};

        system_call_2(syscall(clock_gettime), 1, (positive)address_of when);
        return when[0] * 1000000000ull + when[1];
}

//      user and system time so far, in microseconds.
static positive2 ordered_bench_usage(void)
{
        positive usage[18] = {0};

        system_call_2(syscall(getrusage), 0, (positive)usage);
        return (positive2){usage[0] * 1000000ull + usage[1],
                           usage[2] * 1000000ull + usage[3]};
}

static fn ordered_bench_run(string_address label, string_address path, p8 heavy)
{
        positive best = positive_max;
        positive best_user = 0;
        positive best_system = 0;
        positive best_held = 0;
        positive best_early = 0;
        positive round;

        for (round = 0; round < 3; round++)
        {
                positive started;
                positive2 before;
                positive2 after;

                ordered_bench_out = system_open_at_mode(
                        AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

                if (ordered_bench_out < 0)
                        return;

                before = ordered_bench_usage();
                started = ordered_bench_clock();

                if (heavy)
                        parallel_ordered(ordered_heavy_job, ordered_bench_sink, null,
                                         ordered_heavy_jobs, PARALLEL_SPREAD);
                else
                        parallel_ordered(ordered_light_job, ordered_bench_sink, null,
                                         ORDERED_LIGHT_JOBS, PARALLEL_SPREAD);

                started = ordered_bench_clock() - started;
                after = ordered_bench_usage();
                system_close(ordered_bench_out);

                if (started < best)
                {
                        positive jobs = heavy ? ordered_heavy_jobs : 0;
                        positive last = 0;
                        positive held = 0;
                        positive early = 0;
                        positive total = 0;
                        positive at;

                        if (jobs > ORDERED_TIMED_JOBS)
                                jobs = ORDERED_TIMED_JOBS;

                        for (at = 0; at < jobs; at++)
                                if (ordered_done[at] > last)
                                        last = ordered_done[at];

                        for (at = 0; at < jobs; at++)
                        {
                                held += ordered_sunk[at] - ordered_done[at];
                                total += ordered_length[at];
                                early += ordered_sunk[at] < last ? ordered_length[at] : 0;
                        }

                        best = started;
                        best_user = after.x - before.x;
                        best_system = after.y - before.y;
                        best_held = held / 1000000;
                        best_early = total ? early * 100 / total : 0;
                }
        }

        string_format(log, "%s width %p, %p heavy jobs x %p passes: %p ms wall, %p ms user, %p ms system, outputs waited %p ms in all, %p%% of bytes handed over before the last job finished\n",
                      label, parallel_width(), ordered_heavy_jobs, ordered_heavy_passes,
                      best / 1000000, best_user / 1000, best_system / 1000,
                      best_held, best_early);
        log_flush();
}

b32 main(void)
{
        p8 path[256];
        string_address place = (string_address)getenv("TMPDIR");
        positive at;

        if (program_argument_count() > 1)
        {
                at = string_length(program_argument(1));
                if (at > 250)
                        return 2;
                memory_copy(path, program_argument(1), at + 1);
        }
        else
        {
                if (!place || !string_get(place) || string_length(place) > 200)
                        place = (string_address)"/tmp";
                at = string_length(place);
                memory_copy(path, place, at);
                memory_copy(path + at, "/ordered.out", 13);
        }

        if (program_argument_count() > 2)
                ordered_heavy_jobs = string_to_positive(program_argument(2));

        if (program_argument_count() > 3)
                ordered_heavy_passes = string_to_positive(program_argument(3));

        if (!ordered_heavy_jobs || !ordered_heavy_passes)
                return 2;

        ordered_light_data = (p8 address_to)memory(ORDERED_LIGHT_BYTES + 64);

        if (!ordered_light_data || system_failed((positive)ordered_light_data))
                return 1;

        for (at = 0; at < ORDERED_LIGHT_BYTES + 64; at++)
                ordered_light_data[at] = (p8)(at * 2654435761u >> 11);

        parallel_reset(0);
        ordered_bench_run((string_address)"heavy to a file", path, true);
        ordered_bench_run((string_address)"light to /dev/null", (string_address)"/dev/null", false);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        parallel_reset(0);

        return 0;
}
#endif /* BENCH_ordered */

#ifdef BENCH_tree
/* parallel_tree as find and as du, over a directory named on the command
   line (the Linux tree tree-mt measures by default), at widths 1, 2, 4, 8 up
   to the affinity width, best of five, output written to standard output a
   buffer at a time. find writes every path; du stats every entry through the
   directory that holds it and writes each directory's total after its
   subtree, from leave. Compare the numbers with GNU find and du on the same
   cores. */
#include "../../src/lib.util.c"

typedef struct bench_tree_node
{
        struct bench_tree_node address_to parent;
        positive blocks;
        positive total;
        positive length;
        p8 path[];
} bench_tree_node;

static p8 bench_tree_buffer[1 << 16];
static positive bench_tree_used;
static bipolar bench_tree_out = 1;
static p8 bench_tree_du;

static bench_tree_node address_to bench_tree_new(bench_tree_node address_to parent,
                                                 string_address name)
{
        positive name_length = string_length(name);
        positive length = parent ? parent->length + 1 + name_length : name_length;
        bench_tree_node address_to node = malloc(sizeof(bench_tree_node) + length + 1);

        if (!node)
                return null;

        node->parent = parent;
        node->blocks = 0;
        node->total = 0;
        node->length = length;

        if (parent)
        {
                memory_copy(node->path, parent->path, parent->length);
                node->path[parent->length] = '/';
                memory_copy(node->path + parent->length + 1, name, name_length);
        }
        else
                memory_copy(node->path, name, name_length);

        node->path[length] = 0;
        return node;
}

static positive bench_tree_blocks(bipolar directory, string_address name)
{
        p8 facts[256];

        if (system_call_5(syscall(statx), (positive)directory, (positive)name,
                          name[0] ? 0x100 : 0x1100, 0x400, (positive)facts) < 0)
                return 0;

        return address_to (positive address_to)(facts + 48);
}

static fn bench_tree_enter(address_any context, address_any node, bipolar directory,
                           parallel_output address_to output)
{
        bench_tree_node address_to self = node;
        p8 records[32768];

        (void)context;

        if (directory < 0)
                return;

        if (bench_tree_du)
                self->blocks += bench_tree_blocks(directory, (string_address)"");

        for (;;)
        {
                bipolar got = system_read_directory(directory, records, sizeof(records));
                bipolar at = 0;

                if (got <= 0)
                        break;

                while (at < got)
                {
                        struct linux_dirent64 address_to record =
                                (struct linux_dirent64 address_to)(records + at);
                        positive length;

                        at += record->d_reclen;

                        if (record->d_name[0] == '.' &&
                            (!record->d_name[1] ||
                             (record->d_name[1] == '.' && !record->d_name[2])))
                                continue;

                        length = string_length(record->d_name);

                        if (!bench_tree_du)
                        {
                                p8 address_to line = parallel_reserve(output, self->length + length + 2);

                                if (line)
                                {
                                        memory_copy(line, self->path, self->length);
                                        line[self->length] = '/';
                                        memory_copy(line + self->length + 1, record->d_name, length);
                                        line[self->length + 1 + length] = '\n';
                                }
                        }
                        else if (record->d_type != DT_DIR)
                                self->blocks += bench_tree_blocks(directory, record->d_name);

                        if (record->d_type == DT_DIR)
                        {
                                bench_tree_node address_to child = bench_tree_new(self, record->d_name);

                                if (child)
                                        parallel_child(output, record->d_name, child);
                        }
                }
        }
}

static fn bench_tree_leave(address_any context, address_any node, bipolar directory,
                           parallel_output address_to output)
{
        bench_tree_node address_to self = node;
        positive total = atomic_load(address_of self->total) + self->blocks;
        p8 digits[24];
        positive count = 0;
        positive value = total / 2;
        p8 address_to line;

        (void)context;
        (void)directory;

        do
        {
                digits[count++] = (p8)('0' + value % 10);
                value /= 10;
        } while (value);

        line = parallel_reserve(output, count + 2 + self->length);

        if (line)
        {
                positive at = 0;

                while (count)
                        line[at++] = digits[--count];

                line[at++] = '\t';
                memory_copy(line + at, self->path, self->length);
                line[at + self->length] = '\n';
        }

        if (self->parent)
                atomic_add(address_of self->parent->total, total);
}

static p8 bench_tree_sink(address_any context, address_any node, address_any data,
                            positive length, p8 finished)
{
        (void)context;

        if (finished)
        {
                free(node);
                return true;
        }

        buffered_write((positive)bench_tree_out, bench_tree_buffer,
                       sizeof(bench_tree_buffer), address_of bench_tree_used,
                       data, length);
        return true;
}

static positive bench_tree_clock(void)
{
        positive when[2] = {0, 0};

        system_call_2(syscall(clock_gettime), 1, (positive)address_of when);
        return when[0] * 1000000000ull + when[1];
}

b32 main(void)
{
        string_address root = program_argument_count() > 1
                                      ? program_argument(1)
                                      : (string_address)"/home/dawn/tree-mt/data/linux-7.2";
        positive limit;
        positive width;
        positive mode;

        //      The walk's bytes go to /dev/null through write(2), so the
        //      timing lines on standard output stay readable.
        bench_tree_out = system_open_at(AT_FDCWD, (string_address)"/dev/null",
                                        O_WRONLY | O_CLOEXEC);

        if (bench_tree_out < 0)
                return 1;

        parallel_reset(0);
        limit = parallel_width();

        for (mode = 0; mode < 2; mode++)
        {
                bench_tree_du = mode == 1;

                for (width = 1;; width = width * 2 > limit ? limit : width * 2)
                {
                        positive best = positive_max;
                        positive round;

                        parallel_reset(width);

                        for (round = 0; round < 5; round++)
                        {
                                bipolar top = system_open_at(AT_FDCWD, root,
                                                             FILE_READ | O_DIRECTORY | O_CLOEXEC);
                                bench_tree_node address_to node = bench_tree_new(null, root);
                                positive started = bench_tree_clock();

                                if (top < 0 || !node)
                                        return 1;

                                parallel_tree(bench_tree_enter, bench_tree_du ? bench_tree_leave : null,
                                              bench_tree_sink, null, top, node, O_NOFOLLOW);
                                buffered_flush((positive)bench_tree_out, bench_tree_buffer,
                                               address_of bench_tree_used);
                                started = bench_tree_clock() - started;
                                system_close(top);

                                if (started < best)
                                        best = started;
                        }

                        string_format(log, "%s width %p  %p us\n",
                                      bench_tree_du ? (string_address)"du  " : (string_address)"find",
                                      width, best / 1000);
                        log_flush();

                        if (width == limit)
                                break;
                }
        }

        parallel_reset(0);
        return 0;
}
#endif /* BENCH_tree */

#ifdef BENCH_reserve
/* Run a fresh process per sample: ru_maxrss is a lifetime high-water mark.
   Usage: reserve [MiB, 1..128] [sparse]. Only growth is timed; allocation and
   initial touching precede it. Virtual capacity is not counted as RSS. */
#include "../../src/lib.util.c"

b32 main(void)
{
        positive mib = 64;
        if (program_argument_count() > 1)
                mib = string_to_number_unsigned(program_argument(1), null, 10);
        if (!mib || mib > 128)
                return 2;
        p8 sparse = program_argument_count() > 2 &&
                !string_compare(program_argument(2), (string_address)"sparse");
        positive have = mib * 1024 * 1024, used = have;
        address_any held = memory(have);
        if (!held || (positive)held >= (positive)-4095)
                return 1;
        if (sparse)
                system_call_3(syscall(madvise), (positive)held, have, 15);
        if (!sparse)
                memory_fill(held, 0x5a, have);
        ((p8 address_to)held)[0] = 0x3e;
        ((p8 address_to)held)[used - 1] = 0x7a;

        p64 started = get_cpu_time();
        p8 grown = memory_reserve(address_of held, address_of have,
                                    used, used + 1, 1, 64);
        p64 ticks = get_cpu_time() - started;
        positive usage[18] = {0};
        bipolar measured = system_call_2(syscall(getrusage), 0, (positive)usage);
        p8 valid = grown && ((p8 address_to)held)[0] == 0x3e &&
                ((p8 address_to)held)[used - 1] == 0x7a;
        string_format(log,
                "reserve mib=%p sparse=%p ticks=%p peak_rss_kib=%p minor_faults=%p valid=%p\n",
                mib, (positive)sparse, ticks, usage[4], usage[8], (positive)valid);
        memory_release(address_of held, address_of have, address_of used, 1);
        return valid && measured == 0 ? 0 : 1;
}
#endif /* BENCH_reserve */

#ifdef BENCH_storage_read
/* Real production reader, real warm memfd reads. Only reads are timed;
   file creation, initial touching, formatting and validation are outside.
   Usage: storage-read [bytes 1..65536] [iterations 1..1000000]. */
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

b32 main(void)
{
        positive size = 4096, loops = 262144;
        if (program_argument_count() > 1)
                size = string_to_number_unsigned(program_argument(1), null, 10);
        if (program_argument_count() > 2)
                loops = string_to_number_unsigned(program_argument(2), null, 10);
        if (!size || size > 65536 || !loops || loops > 1000000)
                return 2;
        bipolar handle = system_call_2(syscall(memfd_create),
                                        (positive)"storage-read-benchmark", 0);
        if (handle < 0)
                return 1;
        p8 bytes[65536];
        memory_fill(bytes, 0x5a, size);
        if (system_write_all((positive)handle, bytes, size) != size ||
            storage_read(handle, bytes, size, 0) != size)
                return 1;

        positive total = 0;
        p64 start = get_cpu_time();
        for (positive i = 0; i < loops; i++)
                total += storage_read(handle, bytes, size, 0);
        p64 ticks = get_cpu_time() - start;
        p8 valid = total == loops * size &&
                     bytes[0] == 0x5a && bytes[size - 1] == 0x5a;
        system_close((positive)handle);
        string_format(log,
                      "storage-read bytes=%p loops=%p ticks=%p valid=%p\n",
                      size, loops, ticks, (positive)valid);
        return valid ? 0 : 1;
}
#endif /* BENCH_storage_read */

#ifdef BENCH_compare_max
/* Dynamic-bound string_compare_max against semantic and traffic floors. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define COMPARE_MAX_ROUNDS (1u << 22)
#define COMPARE_MAX_TRIES 7
#define COMPARE_MAX_SHORT 23
#define COMPARE_MAX_LONG 256

typedef b32 (*compare_max_call)(const_string, const_string, positive);
typedef b32 (*compare_max_traffic)(const address_any, const address_any,
                                   positive);

static volatile bipolar compare_max_sink;
static p8 compare_max_left[COMPARE_MAX_LONG + 1] __attribute__((aligned(64)));
static p8 compare_max_right[COMPARE_MAX_LONG + 1] __attribute__((aligned(64)));

static __attribute__((noinline, noclone)) b32
compare_max_empty(const_string left, const_string right, positive size)
{
        (void)left;
        (void)right;
        (void)size;
        return 1;
}

/* Exact minimum semantics for the mismatch-at-byte-zero corpus. */
static __attribute__((noinline, noclone)) b32
compare_max_first_floor(const_string left, const_string right, positive size)
{
        if (!size)
                return 0;
        return (b32)left[0] - (b32)right[0];
}

static compare_max_call volatile compare_max_empty_call = compare_max_empty;
static compare_max_call volatile compare_max_first_floor_call =
        compare_max_first_floor;
static compare_max_call volatile compare_max_subject_call = string_compare_max;
static compare_max_traffic volatile compare_max_traffic_call = memory_compare;

static fn compare_max_control_first()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_empty_call(
                        compare_max_left, compare_max_right, COMPARE_MAX_SHORT);
}

static fn compare_max_floor_first()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_first_floor_call(
                        compare_max_left, compare_max_right, COMPARE_MAX_SHORT);
}

static fn compare_max_subject_first()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_subject_call(
                        compare_max_left, compare_max_right, COMPARE_MAX_SHORT);
}

static fn compare_max_subject_equal()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_subject_call(
                        compare_max_left, compare_max_left, COMPARE_MAX_LONG);
}

static fn compare_max_subject_short_equal()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_subject_call(
                        compare_max_left, compare_max_left, COMPARE_MAX_SHORT);
}

static fn compare_max_floor_short_equal()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_traffic_call(
                        compare_max_left, compare_max_left, COMPARE_MAX_SHORT);
}

/*
        No byte is zero inside this corpus, so memory_compare performs the
        unavoidable two-stream read and equality decision. It is a traffic
        proxy lower bound, not a full string semantic implementation.
*/
static fn compare_max_floor_equal()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_traffic_call(
                        compare_max_left, compare_max_left, COMPARE_MAX_LONG);
}

static fn compare_max_subject_late()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_subject_call(
                        compare_max_left, compare_max_right, COMPARE_MAX_LONG);
}

static fn compare_max_floor_late()
{
        for (positive i = 0; i < COMPARE_MAX_ROUNDS; i++)
                compare_max_sink += compare_max_traffic_call(
                        compare_max_left, compare_max_right, COMPARE_MAX_LONG);
}

static fn compare_max_report(string_address name, bench_work work)
{
        bench_report(name, work, COMPARE_MAX_TRIES,
                     COMPARE_MAX_ROUNDS, (string_address)"call");
}

static bench_work compare_max_named(string_address name)
{
        BENCH_NAME(name, "control-first", compare_max_control_first);
        BENCH_NAME(name, "floor-first", compare_max_floor_first);
        BENCH_NAME(name, "subject-first", compare_max_subject_first);
        BENCH_NAME(name, "floor-equal", compare_max_floor_equal);
        BENCH_NAME(name, "subject-equal", compare_max_subject_equal);
        BENCH_NAME(name, "floor-short-equal", compare_max_floor_short_equal);
        BENCH_NAME(name, "subject-short-equal", compare_max_subject_short_equal);
        BENCH_NAME(name, "floor-late", compare_max_floor_late);
        BENCH_NAME(name, "subject-late", compare_max_subject_late);
        return null;
}

b32 main(void)
{
        for (positive i = 0; i < COMPARE_MAX_LONG; i++)
                compare_max_left[i] = compare_max_right[i] =
                        (p8)('a' + (i * 7) % 23);

        compare_max_left[COMPARE_MAX_LONG] = end;
        compare_max_right[COMPARE_MAX_LONG] = end;

        if (program_argument_count() > 1)
        {
                bench_work work = compare_max_named(program_argument(1));
                if (is_null(work))
                        return 2;

                /* First and late mismatch use the same buffers, selected here. */
                if (string_compare(program_argument(1),
                                   (string_address)"subject-first") == 0 ||
                    string_compare(program_argument(1),
                                   (string_address)"floor-first") == 0)
                        compare_max_right[0]++;
                else if (string_compare(program_argument(1),
                                        (string_address)"subject-late") == 0 ||
                         string_compare(program_argument(1),
                                        (string_address)"floor-late") == 0)
                        compare_max_right[COMPARE_MAX_LONG - 1]++;

                work();
                return 0;
        }

        string_format(log, "string_compare_max dynamic bound, best of %p (%p calls)\n",
                      (positive)COMPARE_MAX_TRIES,
                      (positive)COMPARE_MAX_ROUNDS);

        compare_max_right[0]++;
        compare_max_report((string_address)"empty ABI control",
                           compare_max_control_first);
        compare_max_report((string_address)"first-byte semantic floor",
                           compare_max_floor_first);
        compare_max_report((string_address)"first-byte mismatch",
                           compare_max_subject_first);
        compare_max_right[0]--;

        compare_max_report((string_address)"short equal traffic proxy",
                           compare_max_floor_short_equal);
        compare_max_report((string_address)"equal 23 bytes",
                           compare_max_subject_short_equal);

        compare_max_report((string_address)"equal traffic proxy",
                           compare_max_floor_equal);
        compare_max_report((string_address)"equal 256 bytes",
                           compare_max_subject_equal);

        compare_max_right[COMPARE_MAX_LONG - 1]++;
        compare_max_report((string_address)"late traffic proxy",
                           compare_max_floor_late);
        compare_max_report((string_address)"late mismatch",
                           compare_max_subject_late);

        log_flush();
        return 0;
}
#endif /* BENCH_compare_max */

#ifdef BENCH_bounded_classes
/* The size-class variant string_copy_max_3_15 at bounds of three to fifteen against the generic routine at the same
   bound. Unit: calls, reported per hundred. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define BOUNDED_CLASS_ROUNDS (1u << 22)
#define BOUNDED_CLASS_TRIES 7

typedef string_address (*bounded_class_copy)(string_address, string_address, positive);

static char bounded_class_source[64] __attribute__((aligned(64)));
static char bounded_class_dest[64] __attribute__((aligned(64)));
static volatile bipolar bounded_class_sink;

static bounded_class_copy volatile bounded_class_copy_generic = string_copy_max;
static bounded_class_copy volatile bounded_class_copy_fifteen = string_copy_max_3_15;

#define BOUNDED_CLASS_COPY(name, call, bound) \
        static fn name(void) { \
                for (positive i = 0; i < BOUNDED_CLASS_ROUNDS; i++) \
                        bounded_class_sink += (bipolar)call(bounded_class_dest, bounded_class_source, bound)[0]; \
        }

BOUNDED_CLASS_COPY(bounded_copy_generic_three, bounded_class_copy_generic, 3)
BOUNDED_CLASS_COPY(bounded_copy_fifteen_three, bounded_class_copy_fifteen, 3)
BOUNDED_CLASS_COPY(bounded_copy_generic_eight, bounded_class_copy_generic, 8)
BOUNDED_CLASS_COPY(bounded_copy_fifteen_eight, bounded_class_copy_fifteen, 8)
BOUNDED_CLASS_COPY(bounded_copy_generic_fifteen, bounded_class_copy_generic, 15)
BOUNDED_CLASS_COPY(bounded_copy_fifteen_fifteen, bounded_class_copy_fifteen, 15)

static bench_work bounded_class_named(string_address name)
{
        BENCH_NAME(name, "copy-three-generic", bounded_copy_generic_three);
        BENCH_NAME(name, "copy-three-3-15", bounded_copy_fifteen_three);
        BENCH_NAME(name, "copy-eight-generic", bounded_copy_generic_eight);
        BENCH_NAME(name, "copy-eight-3-15", bounded_copy_fifteen_eight);
        BENCH_NAME(name, "copy-fifteen-generic", bounded_copy_generic_fifteen);
        BENCH_NAME(name, "copy-fifteen-3-15", bounded_copy_fifteen_fifteen);
        return null;
}

b32 main(void)
{
        for (positive i = 0; i < 63; i++)
                bounded_class_source[i] = (p8)('a' + (i * 7) % 23);
        bounded_class_source[63] = 0;
        bounded_class_source[40] = 0;

        if (program_argument_count() > 1)
        {
                bench_work work = bounded_class_named(program_argument(1));
                if (is_null(work))
                        return 2;
                work();
                return 0;
        }

        string_format(log, "string_copy_max size class three to fifteen, best of %p (%p calls)\n",
                      (positive)BOUNDED_CLASS_TRIES, (positive)BOUNDED_CLASS_ROUNDS);
        bench_report((string_address)"copy three bytes, generic", bounded_copy_generic_three, BOUNDED_CLASS_TRIES,
                     BOUNDED_CLASS_ROUNDS, (string_address)"call");
        bench_report((string_address)"copy three bytes, 3-15", bounded_copy_fifteen_three, BOUNDED_CLASS_TRIES,
                     BOUNDED_CLASS_ROUNDS, (string_address)"call");
        bench_report((string_address)"copy eight bytes, generic", bounded_copy_generic_eight, BOUNDED_CLASS_TRIES,
                     BOUNDED_CLASS_ROUNDS, (string_address)"call");
        bench_report((string_address)"copy eight bytes, 3-15", bounded_copy_fifteen_eight, BOUNDED_CLASS_TRIES,
                     BOUNDED_CLASS_ROUNDS, (string_address)"call");
        bench_report((string_address)"copy fifteen bytes, generic", bounded_copy_generic_fifteen, BOUNDED_CLASS_TRIES,
                     BOUNDED_CLASS_ROUNDS, (string_address)"call");
        bench_report((string_address)"copy fifteen bytes, 3-15", bounded_copy_fifteen_fifteen, BOUNDED_CLASS_TRIES,
                     BOUNDED_CLASS_ROUNDS, (string_address)"call");
        log_flush();
        return 0;
}
#endif /* BENCH_bounded_classes */

#ifdef BENCH_string_copy
/* string_copy over caller-shaped sizes, against semantic and traffic proxies. */
#include "../../src/lib.util.c"

#define COPY_BENCH_ROOM ((1u << 20) + 64)
#define COPY_BENCH_BYTES (1u << 26)
#define COPY_BENCH_TRIES 7

typedef fn (*copy_bench_work)(positive, positive);
typedef string_address (*copy_bench_call)(string_address, string_address);
typedef address_any (*copy_bench_traffic_call)(address_any, const address_any,
                                               positive);

static volatile positive copy_bench_sink;
static p8 copy_bench_source[COPY_BENCH_ROOM] __attribute__((aligned(64)));
static p8 copy_bench_output[COPY_BENCH_ROOM] __attribute__((aligned(64)));

static __attribute__((noinline, noclone)) string_address
copy_bench_empty(string_address destination, string_address source)
{
        (void)source;
        return destination;
}

/* Exact scalar semantics; a reference candidate, not a performance floor. */
static __attribute__((noinline, noclone)) string_address
copy_bench_scalar(string_address destination, string_address source)
{
        string_address answer = destination;
        p8 byte;

        do
        {
                byte = address_to source++;
                address_to destination++ = byte;
        }
        while (byte);

        return answer;
}

static copy_bench_call volatile copy_bench_empty_call = copy_bench_empty;
static copy_bench_call volatile copy_bench_scalar_call = copy_bench_scalar;
static copy_bench_call volatile copy_bench_subject_call = string_copy;
static copy_bench_traffic_call volatile copy_bench_traffic = memory_copy_apart;

static fn copy_bench_control(positive size, positive rounds)
{
        (void)size;
        for (positive i = 0; i < rounds; i++)
                copy_bench_sink += (positive)copy_bench_empty_call(
                        copy_bench_output, copy_bench_source);
}

static fn copy_bench_floor(positive size, positive rounds)
{
        for (positive i = 0; i < rounds; i++)
                copy_bench_sink += (positive)copy_bench_traffic(
                        copy_bench_output, copy_bench_source, size);
}

static fn copy_bench_scalar_floor(positive size, positive rounds)
{
        (void)size;
        for (positive i = 0; i < rounds; i++)
                copy_bench_sink += (positive)copy_bench_scalar_call(
                        copy_bench_output, copy_bench_source);
}

static fn copy_bench_subject(positive size, positive rounds)
{
        (void)size;
        for (positive i = 0; i < rounds; i++)
                copy_bench_sink += (positive)copy_bench_subject_call(
                        copy_bench_output, copy_bench_source);
}

static p64 copy_bench_best(copy_bench_work work, positive size,
                           positive rounds)
{
        p64 best = 0;

        for (positive which = 0; which < COPY_BENCH_TRIES; which++)
        {
                p64 started = get_cpu_time();
                p64 elapsed;

                work(size, rounds);
                elapsed = get_cpu_time() - started;

                if (!best || elapsed < best)
                        best = elapsed;
        }

        return best;
}

static fn copy_bench_report(string_address name, copy_bench_work work,
                            positive size, positive rounds)
{
        p64 ticks = copy_bench_best(work, size, rounds);
        positive scaled = (positive)(ticks * 100 / rounds);
        p8 fraction[3];

        positive_into_padded(fraction, scaled % 100, 2, '0');
        fraction[2] = end;
        string_format(log, "    %s  %p.%s ticks/call\n", name, scaled / 100,
                      fraction);
}

static copy_bench_work copy_bench_named(string_address name)
{
        BENCH_NAME(name, "control", copy_bench_control);
        BENCH_NAME(name, "floor", copy_bench_floor);
        BENCH_NAME(name, "scalar", copy_bench_scalar_floor);
        BENCH_NAME(name, "subject", copy_bench_subject);
        return null;
}

b32 main(void)
{
        static const positive sizes[] = {5, 23, 128, 4096, 1u << 20};

        for (positive i = 0; i < COPY_BENCH_ROOM - 1; i++)
                copy_bench_source[i] = (p8)('a' + (i * 7) % 23);
        copy_bench_source[COPY_BENCH_ROOM - 1] = end;

        if (program_argument_count() > 2)
        {
                copy_bench_work work = copy_bench_named(program_argument(1));
                positive size = string_to_positive(program_argument(2));

                if (is_null(work) || !size || size >= COPY_BENCH_ROOM)
                        return 2;

                copy_bench_source[size - 1] = end;
                positive rounds = COPY_BENCH_BYTES / size;
                if (!rounds)
                        rounds = 1;
                work(size, rounds);
                return 0;
        }

        string_format(log, "string_copy caller shapes, best of %p (~%p bytes/row)\n",
                      (positive)COPY_BENCH_TRIES,
                      (positive)COPY_BENCH_BYTES);

        for (positive which = 0; which < sizeof(sizes) / sizeof(sizes[0]); which++)
        {
                positive size = sizes[which];
                positive rounds = COPY_BENCH_BYTES / size;

                copy_bench_source[size - 1] = end;
                string_format(log, "  %p bytes (%p calls)\n", size, rounds);
                copy_bench_report((string_address)"empty ABI control",
                                  copy_bench_control, size, rounds);
                copy_bench_report((string_address)"copy-only traffic proxy",
                                  copy_bench_floor, size, rounds);
                copy_bench_report((string_address)"scalar semantic reference",
                                  copy_bench_scalar_floor, size, rounds);
                copy_bench_report((string_address)"string_copy",
                                  copy_bench_subject, size, rounds);
                copy_bench_source[size - 1] = 'q';
        }

        log_flush();
        return 0;
}
#endif /* BENCH_string_copy */

#ifdef BENCH_stream_get
/* Buffered stream_get_byte hits against ABI and state-machine floors. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define STREAM_GET_ROUNDS (1u << 24)
#define STREAM_GET_BUFFER 4096
#define STREAM_GET_TRIES 7

typedef b32 (*stream_get_call)(stream address_to);

static volatile positive stream_get_sink;
static p8 stream_get_bytes[STREAM_GET_BUFFER];
static stream stream_get_handle;

static __attribute__((noinline, noclone)) b32
stream_get_empty(stream address_to handle)
{
        (void)handle;
        return 1;
}

/* Minimum buffered-hit state transition, with the same EOF answer. */
static __attribute__((noinline, noclone)) b32
stream_get_floor(stream address_to handle)
{
        if (handle->read_head == handle->read_tail)
                return EOF;
        return (b32)handle->buffer[handle->read_head++];
}

static stream_get_call volatile stream_get_empty_call = stream_get_empty;
static stream_get_call volatile stream_get_floor_call = stream_get_floor;
static stream_get_call volatile stream_get_subject_call = stream_get_byte;

static fn stream_get_reset_if_needed()
{
        if (stream_get_handle.read_head == stream_get_handle.read_tail)
                stream_get_handle.read_head = 0;
}

static fn stream_get_control()
{
        for (positive i = 0; i < STREAM_GET_ROUNDS; i++)
        {
                stream_get_reset_if_needed();
                stream_get_sink += (positive)stream_get_empty_call(
                        address_of stream_get_handle);
        }
}

static fn stream_get_floor_work()
{
        for (positive i = 0; i < STREAM_GET_ROUNDS; i++)
        {
                stream_get_reset_if_needed();
                stream_get_sink += (positive)stream_get_floor_call(
                        address_of stream_get_handle);
        }
}

static fn stream_get_subject()
{
        for (positive i = 0; i < STREAM_GET_ROUNDS; i++)
        {
                stream_get_reset_if_needed();
                stream_get_sink += (positive)stream_get_subject_call(
                        address_of stream_get_handle);
        }
}

//      Every try begins at the front of the resident buffer, and the
//      rewind is not part of what the clock sees.
static fn stream_get_rewind()
{
        stream_get_handle.read_head = 0;
}

static fn stream_get_report(string_address name, bench_work work)
{
        bench_report(name, work, STREAM_GET_TRIES,
                     STREAM_GET_ROUNDS, (string_address)"byte");
}

static bench_work stream_get_named(string_address name)
{
        BENCH_NAME(name, "control", stream_get_control);
        BENCH_NAME(name, "floor", stream_get_floor_work);
        BENCH_NAME(name, "subject", stream_get_subject);
        return null;
}

/* Resident fgets workloads: short destination buffers must not repeatedly
   scan the unread suffix. No I/O or allocator is part of these timings. */
static b32 stream_line_bench(void)
{
        static const positive rooms[] = {1, 2, 8, 64, 256, 4096};
        static p8 output[STREAM_GET_BUFFER + 1];
        for (positive spacing = 0; spacing <= 64; spacing += 8)
        {
                memory_fill(stream_get_bytes, 'x', sizeof(stream_get_bytes));
                if (spacing)
                        for (positive at = spacing - 1; at < STREAM_GET_BUFFER; at += spacing)
                                stream_get_bytes[at] = '\n';
                for (positive row = 0; row < array_count(rooms); row++)
                {
                        positive room = rooms[row], times[STREAM_GET_TRIES];
                        positive rounds = (1u << 22) / room;
                        if (rounds < 4096) rounds = 4096;
                        for (positive trial = 0; trial < STREAM_GET_TRIES; trial++)
                        {
                                stream_get_handle.read_head = 0;
                                positive start = get_cpu_time();
                                for (positive at = 0; at < rounds; at++)
                                {
                                        if (STREAM_GET_BUFFER - stream_get_handle.read_head < room)
                                                stream_get_handle.read_head = 0;
                                        if (!stream_get_line(output, (b32)room + 1,
                                                             &stream_get_handle)) return 1;
                                        stream_get_sink += output[0];
                                }
                                times[trial] = get_cpu_time() - start;
                        }
                        order(times, STREAM_GET_TRIES);
                        string_format(log, "line spacing=%p room=%p ticks=%p rounds=%p\n",
                                      spacing, room, times[STREAM_GET_TRIES / 2], rounds);
                }
        }
        log_flush();
        return 0;
}

b32 main(void)
{
        for (positive i = 0; i < STREAM_GET_BUFFER; i++)
                stream_get_bytes[i] = (p8)(i * 13 + 1);

        stream_get_handle.descriptor = -1;
        stream_get_handle.flags = STREAM_READABLE | STREAM_MODE_KNOWN;
        stream_get_handle.buffer = stream_get_bytes;
        stream_get_handle.buffer_size = STREAM_GET_BUFFER;
        stream_get_handle.read_head = 0;
        stream_get_handle.read_tail = STREAM_GET_BUFFER;
        bench_prepare = stream_get_rewind;

        if (program_argument_count() > 1)
        {
                if (string_equals(program_argument(1), "line"))
                        return stream_line_bench();
                bench_work work = stream_get_named(program_argument(1));
                if (is_null(work))
                        return 2;
                work();
                return 0;
        }

        string_format(log, "stream_get_byte buffered hit, best of %p (%p bytes)\n",
                      (positive)STREAM_GET_TRIES,
                      (positive)STREAM_GET_ROUNDS);
        stream_get_report((string_address)"empty ABI control", stream_get_control);
        stream_get_report((string_address)"buffered state floor",
                          stream_get_floor_work);
        stream_get_report((string_address)"stream_get_byte", stream_get_subject);
        log_flush();
        return 0;
}
#endif /* BENCH_stream_get */

#ifdef BENCH_stream_write
/* Resident buffered writes against their unavoidable copy/state floor. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define STREAM_WRITE_ROUNDS (1u << 22)
#define STREAM_WRITE_BUFFER 4096
#define STREAM_WRITE_TRIES 7
#define STREAM_WRITE_LENGTH 23

typedef sized (*stream_write_call)(address_any, sized, sized,
                                   stream address_to);
typedef positive (*stream_put_bytes_call)(stream address_to, address_any,
                                          positive);

static volatile positive stream_write_sink;
static p8 stream_write_input[STREAM_WRITE_LENGTH];
static p8 stream_write_buffer[STREAM_WRITE_BUFFER];
static stream stream_write_handle;

static stream_write_call volatile stream_write_subject_call = stream_write;
static stream_put_bytes_call volatile stream_put_bytes_subject_call =
        stream_put_bytes;

static fn stream_write_reset()
{
        /* Keep every trial on the resident buffered path, with no getpid. */
        stream_write_handle.write_used = 1;
}

/* Minimum resident transition: the bytes move once and become staged. */
static fn stream_write_floor_work()
{
        for (positive i = 0; i < STREAM_WRITE_ROUNDS; i++)
        {
                stream_write_reset();
                memory_copy(stream_write_handle.buffer +
                                    stream_write_handle.write_used,
                            stream_write_input, STREAM_WRITE_LENGTH);
                stream_write_handle.write_used += STREAM_WRITE_LENGTH;
                stream_write_sink += STREAM_WRITE_LENGTH;
        }
}

/* The floor above folds its copy because the length is a literal, which no
   call can. This one moves a length the compiler cannot see through a
   called function, with the same inline copy and the same state update,
   so the gap left between it and stream_put_bytes is the stream's own. */
static volatile positive stream_write_length_unseen = STREAM_WRITE_LENGTH;

static __attribute__((noinline, noclone)) positive
stream_write_floor_call(stream address_to handle, address_any data,
                        positive length)
{
        stream_copy_in(handle->buffer + handle->write_used,
                       (const p8 address_to)data, length);
        handle->write_used += length;
        return length;
}

static fn stream_write_floor_running_work()
{
        for (positive i = 0; i < STREAM_WRITE_ROUNDS; i++)
        {
                stream_write_reset();
                stream_write_sink += stream_write_floor_call(
                        address_of stream_write_handle, stream_write_input,
                        stream_write_length_unseen);
        }
}

static fn stream_put_bytes_subject()
{
        for (positive i = 0; i < STREAM_WRITE_ROUNDS; i++)
        {
                stream_write_reset();
                stream_write_sink += stream_put_bytes_subject_call(
                        address_of stream_write_handle, stream_write_input,
                        STREAM_WRITE_LENGTH);
        }
}

static fn stream_write_subject_one()
{
        for (positive i = 0; i < STREAM_WRITE_ROUNDS; i++)
        {
                stream_write_reset();
                stream_write_sink += (positive)stream_write_subject_call(
                        stream_write_input, 1, STREAM_WRITE_LENGTH,
                        address_of stream_write_handle);
        }
}

/* A non-unit item shape catches collateral cost in the general path. */
static fn stream_write_subject_items()
{
        for (positive i = 0; i < STREAM_WRITE_ROUNDS; i++)
        {
                stream_write_reset();
                stream_write_sink += (positive)stream_write_subject_call(
                        stream_write_input, 8, 2,
                        address_of stream_write_handle);
        }
}

/* Line buffering must retain its newline scan and general flush decision. */
static fn stream_put_bytes_line()
{
        for (positive i = 0; i < STREAM_WRITE_ROUNDS; i++)
        {
                stream_write_reset();
                stream_write_handle.flags |= STREAM_LINE_BUFFERED;
                stream_write_sink += stream_put_bytes_subject_call(
                        address_of stream_write_handle, stream_write_input,
                        STREAM_WRITE_LENGTH);
                stream_write_handle.flags &= ~STREAM_LINE_BUFFERED;
        }
}

static fn stream_write_report(string_address name, bench_work work)
{
        bench_report(name, work, STREAM_WRITE_TRIES,
                     STREAM_WRITE_ROUNDS, (string_address)"call");
}

static bench_work stream_write_named(string_address name)
{
        BENCH_NAME(name, "floor-resident", stream_write_floor_work);
        BENCH_NAME(name, "floor-running", stream_write_floor_running_work);
        BENCH_NAME(name, "subject-put", stream_put_bytes_subject);
        BENCH_NAME(name, "subject-one", stream_write_subject_one);
        BENCH_NAME(name, "subject-items", stream_write_subject_items);
        BENCH_NAME(name, "subject-line", stream_put_bytes_line);
        return null;
}

b32 main(void)
{
        for (positive i = 0; i < STREAM_WRITE_LENGTH; i++)
                stream_write_input[i] = (p8)(i * 13 + 1);

        stream_write_handle.descriptor = -1;
        stream_write_handle.flags = STREAM_WRITABLE | STREAM_MODE_KNOWN;
        stream_write_handle.buffer = stream_write_buffer;
        stream_write_handle.buffer_size = STREAM_WRITE_BUFFER;
        stream_write_handle.write_used = 1;

        BENCH_RUN_NAMED(stream_write_named);

        string_format(log,
                      "stream_write buffered resident path, best of %p "
                      "(%p calls)\n",
                      (positive)STREAM_WRITE_TRIES,
                      (positive)STREAM_WRITE_ROUNDS);
        stream_write_report(
                (string_address)"resident copy/state floor (state-specific)",
                stream_write_floor_work);
        stream_write_report(
                (string_address)"resident floor, length unseen, called",
                stream_write_floor_running_work);
        stream_write_report((string_address)"stream_put_bytes",
                            stream_put_bytes_subject);
        stream_write_report((string_address)"stream_write size=1",
                            stream_write_subject_one);
        stream_write_report((string_address)"stream_write size=8",
                            stream_write_subject_items);
        stream_write_report((string_address)"line-buffered no newline",
                            stream_put_bytes_line);
        log_flush();
        return 0;
}
#endif /* BENCH_stream_write */

#ifdef BENCH_format_bounds
/*
        Bounded printf padding: retained bytes versus requested bytes.

        The wide case is deliberately a count-only snprintf.  Its answer is
        one hundred million, but its destination has no resident byte to
        fill; work proportional to the field width is therefore formatter
        overhead rather than useful output.  The ordinary mixed-format row
        keeps the optimization honest against the short widths programs use.
*/
#include "../../src/lib.util.c"

#define FORMAT_BOUNDS_TRIALS 9
#define FORMAT_BOUNDS_NORMAL_ROUNDS (1u << 17)

static volatile bipolar format_bounds_sink;
static p8 format_bounds_room[64];

static __attribute__((noinline, noclone)) bipolar format_bounds_normal(
    positive value)
{
        return snprintf(format_bounds_room, sizeof(format_bounds_room),
                        (string_address) "%08d:%-12s:%6.3f", (b32)value,
                        (string_address) "moon", (decimal)1.25);
}

static p64 format_bounds_wide_once(void)
{
        p64 began = get_cpu_time();

        format_bounds_sink +=
            snprintf(null, 0, (string_address) "%100000000d", 7);

        return get_cpu_time() - began;
}

static p64 format_bounds_normal_once(void)
{
        p64 began = get_cpu_time();

        for (positive round = 0; round < FORMAT_BOUNDS_NORMAL_ROUNDS; round++)
                format_bounds_sink += format_bounds_normal(round);

        return get_cpu_time() - began;
}

static p64 format_bounds_rounding_once(void)
{
        p64 began = get_cpu_time();
        for (positive round = 0; round < FORMAT_BOUNDS_NORMAL_ROUNDS; round++)
        {
                p64 bits = 0x3ff0000000000000ull |
                    ((round * 0x9e3779b97f4a7c15ull) & 0xfffffffffffffull);
                decimal value = memory_cast(decimal, bits);
                format_bounds_sink += snprintf(format_bounds_room,
                    sizeof(format_bounds_room), "%.6f|%.6a|%.2f", value, value, value);
        }
        return get_cpu_time() - began;
}

static fn format_bounds_order(p64 address_to values)
{
        for (positive at = 1; at < FORMAT_BOUNDS_TRIALS; at++)
        {
                p64 value = values[at];
                positive place = at;

                while (place && values[place - 1] > value)
                {
                        values[place] = values[place - 1];
                        place--;
                }

                values[place] = value;
        }
}

b32 main(void)
{
        p64 wide[FORMAT_BOUNDS_TRIALS];
        p64 normal[FORMAT_BOUNDS_TRIALS];
        p64 rounding[FORMAT_BOUNDS_TRIALS];
        p8 run_wide = true;
        p8 run_normal = true;
        p8 run_rounding = true;

        if (program_argument_count() > 1)
        {
                run_wide = !string_compare(program_argument(1),
                                           (string_address) "wide");
                run_normal = !string_compare(program_argument(1),
                                             (string_address) "normal");
                run_rounding = !string_compare(program_argument(1),
                                               (string_address) "rounding");

                if (!run_wide && !run_normal && !run_rounding)
                        return 2;
        }

        for (positive trial = 0; trial < FORMAT_BOUNDS_TRIALS; trial++)
        {
                if (run_wide)
                        wide[trial] = format_bounds_wide_once();
                if (run_normal)
                        normal[trial] = format_bounds_normal_once();
                if (run_rounding)
                        rounding[trial] = format_bounds_rounding_once();
        }

        if (run_wide)
        {
                format_bounds_order(wide);
                string_format(
                    log,
                    "  count-only width 100000000  %p ticks/call median of %p\n",
                    wide[FORMAT_BOUNDS_TRIALS / 2], FORMAT_BOUNDS_TRIALS);
        }

        if (run_normal)
        {
                format_bounds_order(normal);
                string_format(
                    log,
                    "  ordinary mixed format      %p ticks/%p calls median of %p\n",
                    normal[FORMAT_BOUNDS_TRIALS / 2],
                    FORMAT_BOUNDS_NORMAL_ROUNDS, FORMAT_BOUNDS_TRIALS);
        }

        if (run_rounding)
        {
                format_bounds_order(rounding);
                string_format(log, "  decimal/hex sticky rounding %p ticks/%p calls median of %p\n",
                    rounding[FORMAT_BOUNDS_TRIALS / 2], FORMAT_BOUNDS_NORMAL_ROUNDS,
                    FORMAT_BOUNDS_TRIALS);
        }

        string_format(log, "  retained sink              %b\n",
                      (b32)format_bounds_sink);
        log_flush();
        return 0;
}
#endif /* BENCH_format_bounds */

#ifdef BENCH_startup
/* Runtime startup components: CPU selection, identity, and stack publication. */
#include "../../src/lib.util.c"
#define SHARED_bench_measure
#include "../checks.c"
#undef SHARED_bench_measure

#define STARTUP_ROUNDS (1u << 16)
#define STARTUP_TRIES 7

typedef fn (*startup_void_call)(void);
typedef positive (*startup_identity_call)(void);
typedef string_address address_to (*startup_environment_call)(void);

static volatile positive startup_sink;
static startup_void_call volatile startup_cpu_call = moonwater_cpu_detect;
static startup_void_call volatile startup_begin_call = stdlib_program_starting;
static startup_identity_call volatile startup_identity =
        stdlib_process_identity;
static startup_identity_call volatile startup_initial_identity =
        program_initial_identity;
static startup_environment_call volatile startup_environment =
        program_environment_list;

/* Required x86 serialization/instruction traffic, without policy branches. */
static __attribute__((noinline, noclone)) fn startup_cpu_floor_call(void)
{
#if X64
        __asm__ volatile(
                "push %%rbx\n\t"
                "mov $1, %%eax\n\t"
                "xor %%ecx, %%ecx\n\t"
                "cpuid\n\t"
                "bt $27, %%ecx\n\t"
                "jnc 1f\n\t"
                "xor %%ecx, %%ecx\n\t"
                "xgetbv\n\t"
                "mov $7, %%eax\n\t"
                "xor %%ecx, %%ecx\n\t"
                "cpuid\n\t"
                "1:\n\t"
                "pop %%rbx"
                :
                :
                : "rax", "rcx", "rdx", "cc", "memory");
#endif
}

static startup_void_call volatile startup_cpu_floor = startup_cpu_floor_call;

static fn startup_cpu_floor_work()
{
        for (positive i = 0; i < STARTUP_ROUNDS; i++)
                startup_cpu_floor();
}

static fn startup_cpu_work()
{
        for (positive i = 0; i < STARTUP_ROUNDS; i++)
        {
                startup_cpu_call();
                startup_sink += cpu_has_avx2 + cpu_has_avx512;
        }
}

static fn startup_identity_work()
{
        for (positive i = 0; i < STARTUP_ROUNDS; i++)
                startup_sink += startup_identity();
}

static fn startup_initial_identity_work()
{
        for (positive i = 0; i < STARTUP_ROUNDS; i++)
                startup_sink += startup_initial_identity();
}

/* Exact environment half of stdlib_program_starting, without the PID trap. */
static fn startup_environment_work()
{
        for (positive i = 0; i < STARTUP_ROUNDS; i++)
        {
                environ = startup_environment();
                startup_sink += (positive)environ;
        }
}

static fn startup_begin_work()
{
        for (positive i = 0; i < STARTUP_ROUNDS; i++)
        {
                startup_begin_call();
                startup_sink += (positive)environ;
        }
}

static fn startup_report(string_address name, bench_work work)
{
        bench_report(name, work, STARTUP_TRIES,
                     STARTUP_ROUNDS, (string_address)"call");
}

static bench_work startup_named(string_address name)
{
        BENCH_NAME(name, "cpu", startup_cpu_work);
        BENCH_NAME(name, "cpu-floor", startup_cpu_floor_work);
        BENCH_NAME(name, "identity", startup_identity_work);
        BENCH_NAME(name, "initial-identity", startup_initial_identity_work);
        BENCH_NAME(name, "environment", startup_environment_work);
        BENCH_NAME(name, "begin", startup_begin_work);
        return null;
}

b32 main(void)
{
        BENCH_RUN_NAMED(startup_named);

        string_format(log, "runtime startup components, best of %p (%p calls)\n",
                      (positive)STARTUP_TRIES, (positive)STARTUP_ROUNDS);
        startup_report((string_address)"CPU instruction/serialization floor",
                       startup_cpu_floor_work);
        startup_report((string_address)"CPU feature selection",
                       startup_cpu_work);
        startup_report((string_address)"process identity syscall",
                       startup_identity_work);
        startup_report((string_address)"loader identity handoff",
                       startup_initial_identity_work);
        startup_report((string_address)"environment publication floor",
                       startup_environment_work);
        startup_report((string_address)"stdlib startup", startup_begin_work);
        log_flush();
        return 0;
}
#endif /* BENCH_startup */

#ifdef BENCH_exec
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"

// Times fork + exec + wait for a target, repeatedly. The fork and wait cost is
// identical for both formats, so the difference between two runs is the cost
// of the loader.
//
// A measurement and not a test, which is why it is here and not in test.
// It needs /dev/spark and the two images beside it, so it runs inside the
// booted image:
//
//     SPARK_CPPFLAGS=-DBENCH_tiny ./build spark test/checks fs/tiny.spark
//     SPARK_CPPFLAGS=-DBENCH_exec ./build spark test/checks fs/bench

#define CLOCK_MONOTONIC 1

#define AT_EMPTY_PATH 0x1000

#define ROUNDS 400

timespec started;
timespec finished;

positive now_ns()
{
        timespec t;
        system_call_2(syscall(clock_gettime), CLOCK_MONOTONIC, (positive)address_of t);
        return (positive)t.tv_sec * 1000000000 + (positive)t.tv_nsec;
}

static p8 bench_wait_child(bipolar child)
{
        positive status = 0;
        return child > 0 &&
               system_wait4_retry(child, address_of status, 0, null) == child &&
               !status;
}

// path == null measures fork + exit + wait with no exec at all, which is the
// floor: whatever that costs is not the loader's fault. Subtracting it from
// the other two leaves the cost of actually loading the image.
positive run_many_flags(string_address path, positive flags)
{
        string_address argv[] = {path, null};
        positive start = now_ns();
        p8 failed = false;

        for (positive i = 0; i < ROUNDS; i++)
        {
                bipolar child = system_call_2(syscall(clone), flags, 0);

                if (child == 0)
                {
                        if (path)
                        {
                                system_call_3(syscall(execve), (positive)path, (positive)argv, 0);
                                // A successful exec never returns.  Keep a missing or
                                // rejected fixture from looking like an exceptionally
                                // fast child.
                                system_call_1(syscall(exit), 127);
                        }
                        system_call_1(syscall(exit), 0);
                }

                if (child < 0)
                {
                        failed = true;
                        continue;
                }

                if (!bench_wait_child(child))
                        failed = true;
        }

        return failed ? 0 : now_ns() - start;
}

positive run_many(string_address path)
{
        return run_many_flags(path, SIGCHLD);
}

// Repeats a measurement and keeps the fastest, which is the one least
// disturbed by scheduling noise.
positive best_of_flags(string_address path, positive flags, positive tries)
{
        positive best = 0;

        for (positive i = 0; i < tries; i++)
        {
                positive t = run_many_flags(path, flags);
                if (!t)
                        return 0;
                if (!best || t < best)
                        best = t;
        }

        return best;
}

positive best_of(string_address path, positive tries)
{
        return best_of_flags(path, SIGCHLD, tries);
}

// Spawns through /dev/spark, which creates the task with no address space to
// copy instead of forking one and throwing it away.
b32 device = -1;

positive bench_spawn_device(string_address path)
{
        struct spawn request;
        p8 argv_block[128];
        positive path_length = string_length(path);

        memory_copy(argv_block, path, path_length + 1);

        request.path = (unsigned long)path;
        request.argv = (unsigned long)argv_block;
        request.argv_bytes = path_length + 1;
        request.argv_count = 1;
        request.envp = 0;
        request.envp_bytes = 0;
        request.envp_count = 0;
        request.envp_generation = 0;

        positive start = now_ns();
        p8 failed = false;

        for (positive i = 0; i < ROUNDS; i++)
        {
                bipolar child = system_call_3(syscall(ioctl), device,
                                              SPARK_IOCTL_SPAWN,
                                              (positive)address_of request);

                if (child < 0)
                {
                        string_format(log, "spawn ioctl failed: %b\n", child);
                        log_flush();
                        return 0;
                }

                if (!bench_wait_child(child))
                        failed = true;
        }

        return failed ? 0 : now_ns() - start;
}

/* The shell's exported environment normally does not change between
   commands. A nonzero generation lets the kernel retain that immutable block
   on this open descriptor; zero deliberately measures the copying path. */
positive bench_spawn_environment_submission(string_address path,
                                             positive generation)
{
        struct spawn request;
        p8 argv_block[128];
        p8 env_block[8192];
        positive path_length = string_length(path);

        memory_copy(argv_block, path, path_length + 1);
        memory_fill(env_block, 'x', sizeof(env_block));
        for (positive at = 63; at < sizeof(env_block); at += 64)
                env_block[at] = 0;
        request.path = (unsigned long)path;
        request.argv = (unsigned long)argv_block;
        request.argv_bytes = path_length + 1;
        request.argv_count = 1;
        request.envp = (unsigned long)env_block;
        request.envp_bytes = sizeof(env_block);
        request.envp_count = sizeof(env_block) / 64;
        request.envp_generation = generation;

        positive elapsed = 0;

        for (positive i = 0; i < ROUNDS; i++)
        {
                positive start = now_ns();
                bipolar child = system_call_3(syscall(ioctl), device,
                                              SPARK_IOCTL_SPAWN,
                                              (positive)address_of request);
                elapsed += now_ns() - start;
                if (!bench_wait_child(child))
                        return 0;
        }

        return elapsed;
}

// execveat on an already open descriptor skips pathname resolution entirely.
// The gap between this and execve by path is what the walk costs, and so what
// registering images up front could win back.
positive bench_execveat(string_address path)
{
        string_address argv[] = {path, null};
        b32 image = system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_READ, 0);

        if (image < 0)
                return 0;

        positive start = now_ns();
        p8 failed = false;

        for (positive i = 0; i < ROUNDS; i++)
        {
                bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);

                if (child == 0)
                {
                        system_call_5(syscall(execveat), image, (positive) "",
                                      (positive)argv, 0, AT_EMPTY_PATH);
                        system_call_1(syscall(exit), 127);
                }

                if (!bench_wait_child(child))
                        failed = true;
        }

        positive elapsed = now_ns() - start;
        system_call_1(syscall(close), image);
        return failed ? 0 : elapsed;
}

/* Time only the ioctl which creates the child, then reap that child outside
   its timed interval.  Launching all 400 at once made this a scheduler-contention
   benchmark and moved the answer by nearly two to one between identical boots. */
positive bench_spawn_submission(string_address path)
{
        struct spawn request;
        p8 argv_block[128];
        positive path_length = string_length(path);

        memory_copy(argv_block, path, path_length + 1);

        request.path = (unsigned long)path;
        request.argv = (unsigned long)argv_block;
        request.argv_bytes = path_length + 1;
        request.argv_count = 1;
        request.envp = 0;
        request.envp_bytes = 0;
        request.envp_count = 0;
        request.envp_generation = 0;

        positive elapsed = 0;
        p8 failed = false;

        for (positive i = 0; i < ROUNDS; i++)
        {
                positive start = now_ns();
                bipolar child = system_call_3(syscall(ioctl), device,
                                              SPARK_IOCTL_SPAWN,
                                              (positive)address_of request);
                elapsed += now_ns() - start;

                if (!bench_wait_child(child))
                        failed = true;
        }

        return failed ? 0 : elapsed;
}

fn report(string_address label, positive total)
{
        string_format(log, "%s  total %p us   per exec %p ns\n",
                      label, total / 1000, total / ROUNDS);
        log_flush();
}

fn report_difference(string_address label, positive baseline, positive candidate)
{
        positive difference;
        positive tenths;

        if (!baseline || !candidate)
                return;

        if (candidate <= baseline)
        {
                difference = baseline - candidate;
                tenths = difference * 1000 / baseline;
                string_format(log, "%s  %p ns/exec faster  (%p.%p percent)\n",
                              label, difference / ROUNDS,
                              tenths / 10, tenths % 10);
        }
        else
        {
                difference = candidate - baseline;
                tenths = difference * 1000 / baseline;
                string_format(log, "%s  %p ns/exec slower  (%p.%p percent)\n",
                              label, difference / ROUNDS,
                              tenths / 10, tenths % 10);
        }

        log_flush();
}

b32 main()
{
        struct stats stats_before = {0};
        struct stats stats_after = {0};

        log_direct((string_address) "spark process floor\n", 20);

        // warm the page cache for both images first
        run_many("/tiny.elf");
        run_many("/tiny.spark");

        positive floor = best_of(null, 5);
        positive elf = best_of("/tiny.elf", 5);
        positive spark = best_of("/tiny.spark", 5);

        if (!floor || !elf || !spark)
        {
                log_error("benchmark child could not execute\n", 0);
                return 1;
        }

        device = system_call_4(syscall(openat), AT_FDCWD,
                                     (positive)SPARK_DEVICE, FILE_READ_WRITE, 0);

        positive dev = 0;
        if (device >= 0)
        {
                if (system_call_3(syscall(ioctl), device, SPARK_IOCTL_STATS,
                                  (positive)address_of stats_before))
                {
                        log_error("could not read initial Spark counters\n", 0);
                        return 1;
                }
                bench_spawn_device("/tiny.spark");
                dev = bench_spawn_device("/tiny.spark");
                positive again = bench_spawn_device("/tiny.spark");
                if (again && again < dev)
                        dev = again;
                if (system_call_3(syscall(ioctl), device, SPARK_IOCTL_STATS,
                                  (positive)address_of stats_after))
                {
                        log_error("could not read final Spark counters\n", 0);
                        return 1;
                }
        }
        else
        {
                string_format(log, "could not open %s: %b\n", SPARK_DEVICE, device);
                log_flush();
        }

        positive at_fd = bench_execveat("/tiny.spark");
        positive at2 = bench_execveat("/tiny.spark");
        if (at2 && at2 < at_fd)
                at_fd = at2;

        positive copied_env = 0;
        positive cached_env = 0;
        if (device >= 0)
        {
                copied_env = bench_spawn_environment_submission("/tiny.spark", 0);
                cached_env = bench_spawn_environment_submission("/tiny.spark", 1);
        }

        positive nowait = 0;
        if (device >= 0)
        {
                bench_spawn_submission("/tiny.spark");
                nowait = bench_spawn_submission("/tiny.spark");
                positive nw2 = bench_spawn_submission("/tiny.spark");
                if (nw2 && nw2 < nowait)
                        nowait = nw2;
        }

        if (!at_fd || (device >= 0 && (!dev || !nowait)) ||
            (device >= 0 && (!copied_env || !cached_env)) ||
            (device >= 0 && stats_after.loads - stats_before.loads != 3 * ROUNDS))
        {
                log_error("benchmark launch path did not complete its work\n", 0);
                return 1;
        }

        log_direct((string_address) "results\n", 8);

        report("fork+wait only ", floor);
        report("fork  + elf    ", elf);
        report("fork  + spark  ", spark);
        if (at_fd)
                report("fork + execveat ", at_fd);
        if (dev)
                report("/dev/spark     ", dev);
        if (nowait)
                report("/dev/spark submit", nowait);
        if (copied_env && cached_env)
        {
                report("submit, copy 8K env ", copied_env);
                report("submit, cache 8K env", cached_env);
                report_difference("  cached environment      ",
                                  copied_env, cached_env);
        }

        string_format(log, "\nclear comparisons (lower is faster):\n");
        report_difference("  Spark image versus ELF ", elf, spark);
        if (dev)
                report_difference("  fresh-mm versus fork  ", spark, dev);

        if (stats_after.spawns > stats_before.spawns)
        {
                positive spawns = stats_after.spawns - stats_before.spawns;
                positive loads = stats_after.loads - stats_before.loads;
                positive task = stats_after.task_ns - stats_before.task_ns;
                positive exec = stats_after.exec_ns - stats_before.exec_ns;
                positive loader = stats_after.loader_ns - stats_before.loader_ns;
                positive mapped = stats_after.map_ns - stats_before.map_ns;

                string_format(log, "\nkernel side, averaged over %p sequential spawns:\n",
                              spawns);
                string_format(log, "  completed image loads %p\n", loads);
                string_format(log, "  creating the task  %p ns\n", task / spawns);
                string_format(log, "  loading the image  %p ns\n", exec / spawns);
                if (loads)
                {
                        loader /= loads;
                        mapped /= loads;
                        exec /= spawns;
                        string_format(log, "    spark binfmt     %p ns\n", loader);
                        string_format(log, "      exec commit    %p ns\n",
                                      loader > mapped ? loader - mapped : 0);
                        string_format(log, "      mapping regions %p ns\n",
                                      mapped);
                        string_format(log, "    generic prologue %p ns\n",
                                      exec > loader ? exec - loader : 0);
                }
                log_flush();
        }

        string_format(log, "load cost:  elf %p ns   spark %p ns\n",
                      elf > floor ? (elf - floor) / ROUNDS : 0,
                      spark > floor ? (spark - floor) / ROUNDS : 0);
        log_flush();

        return 0;
}
#endif /* BENCH_exec */

#ifdef BENCH_network_spawn
/*
        Boot-service launch floor: fork+exec against /dev/spark for the exact
        two-word /ip service shape, with an empty environment.

        Build into a Moonwater image and run there:

                SPARK_CPPFLAGS=-DBENCH_network_spawn ./build spark test/checks \
                        fs/bench-network-spawn

        `ip link` exits, unlike `ip watch`, but enters the same multicall image
        with the same argv/env/fd contract. Its netlink work is identical on
        both paths, so the paired gap is the launch cost. Kernel-side Spark
        counters are reported over the device-spawn rounds as a second clock.
*/
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"

#define CLOCK_MONOTONIC 1
#define ROUNDS 100
#define TRIES 3

static string_address ip_path = (string_address) "/ip";
static string_address ip_arguments[] = {(string_address) "/ip",
                                        (string_address) "link", null};
static string_address empty_environment[] = {null};
static b32 device = -1;

static positive now_ns(void)
{
        timespec now = {0, 0};

        system_call_2(syscall(clock_gettime), CLOCK_MONOTONIC,
                      (positive)address_of now);
        return (positive)now.tv_sec * 1000000000 + (positive)now.tv_nsec;
}

static positive run_fork_exec(void)
{
        positive started = now_ns();

        for (positive at = 0; at < ROUNDS; at++)
        {
                bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
                positive status = 0;

                if (child == 0)
                {
                        system_call_3(syscall(execve), (positive)ip_path,
                                      (positive)ip_arguments,
                                      (positive)empty_environment);
                        system_call_1(syscall(exit), 127);
                }

                if (child < 0)
                        return 0;

                if (system_wait4_retry(child, address_of status, 0, null) != child ||
                    status)
                        return 0;
        }

        return now_ns() - started;
}

static positive run_spark(void)
{
        static p8 argv_block[] = "/ip\0link\0";
        struct spawn request = {
            .path = (unsigned long)"/ip",
            .argv = (unsigned long)argv_block,
            .argv_bytes = sizeof(argv_block) - 1,
            .argv_count = 2,
            .envp = 0,
            .envp_bytes = 0,
            .envp_count = 0,
        };
        positive started = now_ns();

        for (positive at = 0; at < ROUNDS; at++)
        {
                bipolar child = system_call_3(syscall(ioctl), device,
                                              SPARK_IOCTL_SPAWN,
                                              (positive)address_of request);
                positive status = 0;

                if (child < 0)
                        return 0;

                if (system_wait4_retry(child, address_of status, 0, null) != child ||
                    status)
                        return 0;
        }

        return now_ns() - started;
}

static positive best(positive (*run)(void))
{
        positive answer = 0;

        for (positive at = 0; at < TRIES; at++)
        {
                positive elapsed = run();

                if (!elapsed)
                        return 0;

                if (!answer || elapsed < answer)
                        answer = elapsed;
        }

        return answer;
}

b32 main(void)
{
        struct stats before = {0};
        struct stats after = {0};
        b32 terminal;
        b32 quiet;
        positive fork_ns;
        positive spark_ns;

        device = system_call_4(syscall(openat), AT_FDCWD,
                               (positive)SPARK_DEVICE, FILE_READ_WRITE, 0);
        if (device < 0)
        {
                string_format(log_error, "cannot open %s: %b\n",
                              SPARK_DEVICE, device);
                return 1;
        }

        terminal = system_call_1(syscall(dup), standard_output_descriptor);
        quiet = system_call_4(syscall(openat), AT_FDCWD,
                              (positive)"/dev/null", FILE_WRITE, 0);
        if (terminal < 0 || quiet < 0)
                return 1;

        log_flush();
        system_call_3(syscall(dup3), quiet, standard_output_descriptor, 0);

        /* Warm image, netlink and scheduler paths before either timed best. */
        run_fork_exec();
        run_spark();

        fork_ns = best(run_fork_exec);
        system_call_3(syscall(ioctl), device, SPARK_IOCTL_STATS,
                      (positive)address_of before);
        spark_ns = best(run_spark);
        system_call_3(syscall(ioctl), device, SPARK_IOCTL_STATS,
                      (positive)address_of after);

        system_call_3(syscall(dup3), terminal, standard_output_descriptor, 0);
        system_call_1(syscall(close), quiet);
        system_call_1(syscall(close), terminal);

        if (!fork_ns || !spark_ns)
        {
                string_format(log_error, "a launch path failed\n");
                return 1;
        }

        string_format(log, "boot service /ip link, best of %p x %p\n",
                      (positive)TRIES, (positive)ROUNDS);
        string_format(log, "  fork+exec  %p ns/launch\n", fork_ns / ROUNDS);
        string_format(log, "  spark      %p ns/launch\n", spark_ns / ROUNDS);
        if (fork_ns >= spark_ns)
                string_format(log, "  saved      %p ns/launch\n",
                              (fork_ns - spark_ns) / ROUNDS);
        else
                string_format(log, "  extra      %p ns/launch\n",
                              (spark_ns - fork_ns) / ROUNDS);

        if (after.spawns > before.spawns)
        {
                positive count = after.spawns - before.spawns;

                string_format(log, "  kernel task %p ns, exec %p ns (%p spawns)\n",
                              (after.task_ns - before.task_ns) / count,
                              (after.exec_ns - before.exec_ns) / count, count);
        }

        log_flush();
        return 0;
}
#endif /* BENCH_network_spawn */

#ifdef BENCH_shell_document
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

/* Isolate the shared here-body expansion path from parsing/forking. Build
   this same probe in both source revisions with test/run's freestanding
   flags. Arguments: body bytes (16..1048576), repetitions, optional "dollar".
   Literal runs should go directly into the retained token arena, without a
   second body-sized expansion/mark allocation. The checksum must agree. */
b32 main()
{
        positive bytes = program_argument(1)
                             ? string_digits(program_argument(1), null) : 65536;
        positive repeats = program_argument(2)
                               ? string_digits(program_argument(2), null) : 1000;
        p8 dollar = program_argument(3) &&
                      word_is(program_argument(3), "dollar");
        positive sum = 0;
        p8 address_to body;

        if (bytes < 16 || bytes > 1048576 || !repeats || repeats > 1000000)
                return 2;
        body = memory(bytes + 1);
        if (!body || (positive)body >= (positive)-4095)
                return 2;
        memory_fill(body, 'x', bytes);
        body[bytes] = end;
        shell_env_init(environ);
        env_set("word", "abcdefg");
        if (dollar)
                memory_copy(body + bytes - 7, "${word}", 7);

        for (positive at = 0; at < repeats; at++)
        {
                string_address out;
                positive made;
                token_used = 0;
                token_overflow = false;
                made = exec_here_expand(body, bytes, address_of out);
                if (made != bytes || token_overflow || exec_line_aborted() ||
                    out[bytes - 1] != (dollar ? 'g' : 'x'))
                        return 1;
                sum += made;
        }
        memory_free(body, bytes + 1);
        string_format(log, "%p\n", sum);
        log_flush();
        return 0;
}
#endif /* BENCH_shell_document */

#ifdef BENCH_tiny
#include "../../src/lib.util.c"

// The smallest thing that can be exec'd: does nothing, exits immediately.
// Whatever time it takes to run this is almost entirely the cost of loading it.
b32 main()
{
        return 0;
}
#endif /* BENCH_tiny */

#ifdef BENCH_startup_empty
/*
        The host-kernel floor for startup measurements: enter, issue exit(0),
        and do nothing else.  This deliberately does not include Moonwater's
        runtime, so its process/image cost can be subtracted from the tiny
        main in bench_tiny.c rather than guessed from a full shell.
*/
__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start, @function\n"
    "_start:\n"
#if defined(__x86_64__)
    "xor %edi, %edi\n"
    "mov $60, %eax\n"
    "syscall\n"
#elif defined(__aarch64__)
    "mov x0, #0\n"
    "mov x8, #93\n"
    "svc #0\n"
#elif defined(__riscv)
    "li a0, 0\n"
    "li a7, 93\n"
    "ecall\n"
#else
#error unsupported startup benchmark architecture
#endif
    ".size _start, .-_start\n"
);
#endif /* BENCH_startup_empty */

#ifdef BENCH_one
#include "../../src/lib.util.c"

#ifndef LENGTH
#define LENGTH 4
#endif
#ifndef ROUNDS
#define ROUNDS 1
#endif
#ifndef WHICH
#define WHICH 0
#endif

#define SHARED_bench_reference
#include "../checks.c"
#undef SHARED_bench_reference

static p8 subject[8192];
static p8 mirror[8192];
static volatile positive sink;

b32 main()
{
        for (positive i = 0; i < LENGTH; i++)
                subject[i] = (p8)('a' + i % 26);

        subject[LENGTH] = 0;
        memory_copy_apart(mirror, subject, LENGTH + 1);

        for (positive r = 0; r < ROUNDS; r++)
        {
#if WHICH == 0
                sink += reference_length(subject);
#elif WHICH == 1
                sink += string_length(subject);
#elif WHICH == 2
                sink += (positive)reference_compare(subject, mirror);
#elif WHICH == 3
                sink += (positive)string_compare(subject, mirror);
#elif WHICH == 4
                sink += (positive)reference_first_of(subject, '#');
#elif WHICH == 5
                sink += (positive)string_first_of(subject, '#');
#endif
        }

        return 0;
}
#endif /* BENCH_one */
