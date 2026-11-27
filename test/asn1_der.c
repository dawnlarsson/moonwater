/*
        Exhaustive X.690 / RFC 5280 ASN.1 DER security, edge-case, guard-page,
        differential-fuzz, and speed/size proof for src/net/asn1_der.asm.c
        (arm64, x86-64, riscv64).

        Build & run:
          cc -O2 -Wall -Werror test/asn1_der.c -o /tmp/t_asn1 && /tmp/t_asn1
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/asn1_der.c -o /tmp/t_asn1_asan && /tmp/t_asn1_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define NET_ASN1_DER_IMPLEMENTATION
#include "../src/net/asn1_der.asm.c"

/* ── -O2 C Reference Implementations (exact parity with net.c) ───────────── */

static __attribute__((noinline)) der_i32 ref_asn1_der_length(
        const der_u8 *bytes, der_u64 size, der_u64 *at, der_u64 *length)
{
        if (!bytes || !at || !length) return ASN1_DER_FAIL;
        der_u64 pos = *at;
        if (pos >= size) return ASN1_DER_FAIL;
        der_u8 first = bytes[pos++];
        if (first < 0x80u) {
                *length = first;
                *at = pos;
                return ASN1_DER_OK;
        }
        der_u64 count = first & 0x7fu;
        if (!count || count > 3u || count > size - pos || !bytes[pos])
                return ASN1_DER_FAIL;
        der_u64 value = 0;
        for (der_u64 i = 0; i < count; i++)
                value = (value << 8) | bytes[pos++];
        if (value < 0x80u) return ASN1_DER_FAIL;
        *length = value;
        *at = pos;
        return ASN1_DER_OK;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_enter(
        const der_u8 *bytes, der_u64 size, der_u32 tag, der_u64 *at, der_u64 *stop)
{
        if (!bytes || !at || !stop) return ASN1_DER_FAIL;
        der_u64 pos = *at;
        if (pos >= size || bytes[pos] != (der_u8)tag) return ASN1_DER_FAIL;
        pos++;
        der_u64 len = 0;
        if (ref_asn1_der_length(bytes, size, &pos, &len) || len > size - pos)
                return ASN1_DER_FAIL;
        *at = pos;
        *stop = pos + len;
        return ASN1_DER_OK;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_skip(
        const der_u8 *bytes, der_u64 size, der_u64 *at)
{
        if (!bytes || !at) return ASN1_DER_FAIL;
        der_u64 pos = *at;
        if (pos >= size) return ASN1_DER_FAIL;
        der_u64 stop = 0;
        if (ref_asn1_der_enter(bytes, size, bytes[pos], &pos, &stop))
                return ASN1_DER_FAIL;
        *at = stop;
        return ASN1_DER_OK;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_take(
        const der_u8 *bytes, der_u64 size, der_u32 tag, der_u64 *at,
        const der_u8 **value, der_u64 *length)
{
        if (!bytes || !at || !value || !length) return ASN1_DER_FAIL;
        der_u64 start = *at;
        der_u64 pos = start;
        der_u64 stop = 0;
        if (ref_asn1_der_enter(bytes, size, tag, &pos, &stop))
                return ASN1_DER_FAIL;
        *value = bytes + start;
        *length = stop - start;
        *at = stop;
        return ASN1_DER_OK;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_true(
        const der_u8 *bytes, der_u64 stop, der_u64 *at, der_u8 *value)
{
        if (!at || !value) return ASN1_DER_FAIL;
        if (*at >= stop) return ASN1_DER_OK;
        if (!bytes) return ASN1_DER_FAIL;
        if (bytes[*at] != 0x01u) return ASN1_DER_OK;
        der_u64 pos = *at;
        der_u64 bstop = 0;
        if (ref_asn1_der_enter(bytes, stop, 0x01u, &pos, &bstop) ||
            pos + 1u != bstop || bytes[pos] != 0xffu)
                return ASN1_DER_FAIL;
        *value = 1u;
        *at = bstop;
        return ASN1_DER_OK;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_null_or_absent(
        const der_u8 *der, der_u64 at, der_u64 stop)
{
        if (at == stop) return 1;
        if (at > stop || !der) return 0;
        return (stop - at == 2u && der[at] == 0x05u && !der[at + 1u]) ? 1 : 0;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_oid_content(
        const der_u8 *bytes, der_u64 length)
{
        if (!bytes || !length) return 0;
        der_u64 at = 0;
        while (at < length) {
                if (bytes[at] == 0x80u) return 0;
                while (bytes[at++] & 0x80u)
                        if (at == length) return 0;
        }
        return 1;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_enter_oid(
        const der_u8 *bytes, der_u64 size, der_u64 *at, der_u64 *stop)
{
        if (!bytes || !at || !stop) return ASN1_DER_FAIL;
        der_u64 pos = *at;
        der_u64 st = 0;
        if (ref_asn1_der_enter(bytes, size, 0x06u, &pos, &st))
                return ASN1_DER_FAIL;
        if (!ref_asn1_der_oid_content(bytes + pos, st - pos))
                return ASN1_DER_FAIL;
        *at = pos;
        *stop = st;
        return ASN1_DER_OK;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_integer(
        const der_u8 *bytes, der_u64 length)
{
        if (!bytes || !length) return 0;
        if (length == 1u) return 1;
        der_u8 first = bytes[0];
        der_u8 second = bytes[1];
        return !(first == 0u && !(second & 0x80u)) &&
               !(first == 0xffu && (second & 0x80u)) ? 1 : 0;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_named_bits(
        const der_u8 *bytes, der_u64 length)
{
        if (!bytes || !length) return 0;
        der_u8 unused = bytes[0];
        if (unused > 7u) return 0;
        if (length == 1u) return unused == 0u ? 1 : 0;
        der_u8 final_b = bytes[length - 1u];
        if (!final_b) return 0;
        der_u8 zeros = 0;
        while (!(final_b & 1u)) {
                zeros++;
                final_b >>= 1;
        }
        return zeros == unused ? 1 : 0;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_positive_integer(
        const der_u8 *bytes, der_u64 at, der_u64 stop,
        der_u64 *value_at, der_u64 *value_length)
{
        if (!bytes || !value_at || !value_length) return 0;
        if (at >= stop || (bytes[at] & 0x80u)) return 0;
        if (!bytes[at]) {
                if (at + 1u >= stop || !(bytes[at + 1u] & 0x80u))
                        return 0;
                at++;
        }
        *value_at = at;
        *value_length = stop - at;
        return 1;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_date_value(
        der_u32 tag, const der_u8 *text, der_u64 length, der_u64 *value)
{
        static const der_u8 days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                                 31, 31, 30, 31, 30, 31};
        if (!text || !value) return 0;
        der_u64 year_digits;
        if (tag == 0x17u) year_digits = 2u;
        else if (tag == 0x18u) year_digits = 4u;
        else return 0;
        if (length != year_digits + 11u || text[length - 1u] != 'Z') return 0;
        for (der_u64 i = 0; i + 1u < length; i++)
                if (text[i] < '0' || text[i] > '9') return 0;
        der_u64 year = 0;
        for (der_u64 i = 0; i < year_digits; i++)
                year = year * 10u + (der_u64)(text[i] - '0');
        if (year_digits == 2u) year += year >= 50u ? 1900u : 2000u;
        if (!year || (tag == 0x18u && year < 2050u)) return 0;
        der_u64 month = (der_u64)(text[year_digits] - '0') * 10u + (der_u64)(text[year_digits + 1u] - '0');
        der_u64 day = (der_u64)(text[year_digits + 2u] - '0') * 10u + (der_u64)(text[year_digits + 3u] - '0');
        der_u64 hour = (der_u64)(text[year_digits + 4u] - '0') * 10u + (der_u64)(text[year_digits + 5u] - '0');
        der_u64 minute = (der_u64)(text[year_digits + 6u] - '0') * 10u + (der_u64)(text[year_digits + 7u] - '0');
        der_u64 second = (der_u64)(text[year_digits + 8u] - '0') * 10u + (der_u64)(text[year_digits + 9u] - '0');
        if (!month || month > 12u || !day || hour > 23u || minute > 59u || second > 59u)
                return 0;
        der_u64 limit = days_in_month[month - 1u];
        if (month == 2u && (!(year % 4u) && (year % 100u || !(year % 400u))))
                limit++;
        if (day > limit) return 0;
        der_u64 ans = year;
        ans = ans * 100u + month;
        ans = ans * 100u + day;
        ans = ans * 100u + hour;
        ans = ans * 100u + minute;
        ans = ans * 100u + second;
        *value = ans;
        return 1;
}

static __attribute__((noinline)) der_i32 ref_asn1_der_date_general(
        const der_u8 *text, der_u64 length, der_u64 *value)
{
        der_u64 answer = 0;
        if (!text || !value) return 0;
        if (ref_asn1_der_date_value(0x18u, text, length, value))
                return 1;
        if (length != 15u || text[0] < '1' || text[0] > '2' ||
            text[1] < '0' || text[1] > '9' ||
            !ref_asn1_der_date_value(0x17u, text + 2, 13u, &answer) ||
            answer / 1000000000000ull !=
                (der_u64)(text[0] - '0') * 10u + (der_u64)(text[1] - '0'))
                return 0;
        *value = answer;
        return 1;
}

static __attribute__((noinline)) der_u64 ref_asn1_der_date_seconds(der_u64 date)
{
        der_u64 year = date / 10000000000ull;
        der_u64 month = date / 100000000ull % 100u;
        der_u64 day = date / 1000000ull % 100u;
        if (!year || !month || !day) return 0;
        der_u64 shifted = month <= 2u ? year - 1u : year;
        der_u64 era = shifted / 400u;
        der_u64 of_era = shifted - era * 400u;
        der_u64 of_year = (153u * (month > 2u ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
        der_u64 days = era * 146097u + of_era * 365u + of_era / 4u - of_era / 100u + of_year;
        return ((days * 24u + date / 10000u % 100u) * 60u + date / 100u % 100u) * 60u +
               date % 100u;
}

/* ── Test harness ────────────────────────────────────────────────────────── */

static int g_fails = 0;

#define CHECK(cond, msg) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
                g_fails++; \
        } \
} while (0)

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t xorshift64(void)
{
        uint64_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng_state = x;
        return x;
}

static void test_security_and_edges(void)
{
        /* 1. asn1_der_length: canonical, indefinite, non-minimal, >3 bytes, truncated, overflow */
        {
                der_u64 at, len;
                const der_u8 b_short0[] = {0x00};
                at = 0; len = 99;
                CHECK(asn1_der_length(b_short0, 1, &at, &len) == 0 && at == 1 && len == 0, "short 0");

                const der_u8 b_short7f[] = {0x7f};
                at = 0; len = 0;
                CHECK(asn1_der_length(b_short7f, 1, &at, &len) == 0 && at == 1 && len == 0x7f, "short 0x7f");

                const der_u8 b_indef[] = {0x80, 0x00, 0x00};
                at = 0; len = 77;
                CHECK(asn1_der_length(b_indef, 3, &at, &len) == -1 && at == 0 && len == 77, "indefinite 0x80 rejected");

                const der_u8 b_nonmin1[] = {0x81, 0x05};
                at = 0; len = 77;
                CHECK(asn1_der_length(b_nonmin1, 2, &at, &len) == -1 && at == 0, "non-minimal 81 05 rejected");

                const der_u8 b_nonmin1b[] = {0x81, 0x7f};
                at = 0;
                CHECK(asn1_der_length(b_nonmin1b, 2, &at, &len) == -1, "non-minimal 81 7f rejected");

                const der_u8 b_nonmin1z[] = {0x81, 0x00};
                at = 0;
                CHECK(asn1_der_length(b_nonmin1z, 2, &at, &len) == -1, "non-minimal 81 00 rejected");

                const der_u8 b_nonmin2[] = {0x82, 0x00, 0x80};
                at = 0;
                CHECK(asn1_der_length(b_nonmin2, 3, &at, &len) == -1, "non-minimal 82 00 80 rejected");

                const der_u8 b_nonmin3[] = {0x83, 0x00, 0x01, 0x00};
                at = 0;
                CHECK(asn1_der_length(b_nonmin3, 4, &at, &len) == -1, "non-minimal 83 00 01 00 rejected");

                const der_u8 b_4byte[] = {0x84, 0xff, 0xff, 0xff, 0xff};
                at = 0;
                CHECK(asn1_der_length(b_4byte, 5, &at, &len) == -1, "4-byte 0xffffffff rejected");

                const der_u8 b_5byte[] = {0x85, 0x01, 0x00, 0x00, 0x00, 0x00};
                at = 0;
                CHECK(asn1_der_length(b_5byte, 6, &at, &len) == -1, "5-byte 0x85 rejected");

                const der_u8 b_ff[] = {0xff, 0x01, 0x02};
                at = 0;
                CHECK(asn1_der_length(b_ff, 3, &at, &len) == -1, "0xff length rejected");

                const der_u8 b_ok1[] = {0x81, 0x80};
                at = 0; len = 0;
                CHECK(asn1_der_length(b_ok1, 2, &at, &len) == 0 && at == 2 && len == 0x80, "81 80 ok");

                const der_u8 b_ok2[] = {0x82, 0x01, 0x00};
                at = 0; len = 0;
                CHECK(asn1_der_length(b_ok2, 3, &at, &len) == 0 && at == 3 && len == 0x100, "82 01 00 ok");

                const der_u8 b_ok3[] = {0x83, 0x12, 0x34, 0x56};
                at = 0; len = 0;
                CHECK(asn1_der_length(b_ok3, 4, &at, &len) == 0 && at == 4 && len == 0x123456, "83 12 34 56 ok");

                /* Truncated multi-byte length */
                at = 0;
                CHECK(asn1_der_length(b_ok3, 3, &at, &len) == -1, "truncated 3-byte length");
                at = 1;
                CHECK(asn1_der_length(b_short0, 1, &at, &len) == -1, "at == size");
                at = UINT64_MAX;
                CHECK(asn1_der_length(b_short0, 1, &at, &len) == -1, "at == UINT64_MAX");
                CHECK(asn1_der_length(NULL, 1, &at, &len) == -1, "NULL bytes");
                CHECK(asn1_der_length(b_short0, 1, NULL, &len) == -1, "NULL at");
                CHECK(asn1_der_length(b_short0, 1, &at, NULL) == -1, "NULL len");
        }

        /* 2. asn1_der_enter / skip / take */
        {
                const der_u8 tlv[] = {0x30, 0x03, 0x02, 0x01, 0x05};
                der_u64 at = 0, stop = 0, len = 0;
                const der_u8 *val = NULL;
                CHECK(asn1_der_enter(tlv, sizeof(tlv), 0x30, &at, &stop) == 0 && at == 2 && stop == 5, "enter seq");
                CHECK(asn1_der_enter(tlv, sizeof(tlv), 0x31, &at, &stop) == -1, "enter wrong tag");
                at = 0;
                CHECK(asn1_der_enter(tlv, 4, 0x30, &at, &stop) == -1 && at == 0, "enter truncated content");
                at = 0;
                CHECK(asn1_der_skip(tlv, sizeof(tlv), &at) == 0 && at == 5, "skip seq");
                at = 0;
                CHECK(asn1_der_skip(tlv, 4, &at) == -1 && at == 0, "skip truncated");
                at = 0;
                CHECK(asn1_der_take(tlv, sizeof(tlv), 0x30, &at, &val, &len) == 0 &&
                      val == tlv && len == 5 && at == 5, "take seq");
                at = 0;
                CHECK(asn1_der_take(tlv, 4, 0x30, &at, &val, &len) == -1 && at == 0, "take truncated");
                /* Overflow: 3-byte max length 0xffffff in tiny buffer */
                const der_u8 huge[] = {0x04, 0x83, 0xff, 0xff, 0xff, 0x00};
                at = 0;
                CHECK(asn1_der_enter(huge, sizeof(huge), 0x04, &at, &stop) == -1, "enter 0xffffff overflow");
                CHECK(asn1_der_skip(huge, sizeof(huge), &at) == -1, "skip 0xffffff overflow");
                CHECK(asn1_der_take(huge, sizeof(huge), 0x04, &at, &val, &len) == -1, "take 0xffffff overflow");
        }

        /* 3. asn1_der_true */
        {
                der_u64 at = 0;
                der_u8 flag = 0;
                const der_u8 b_true[] = {0x01, 0x01, 0xff};
                CHECK(asn1_der_true(b_true, 3, &at, &flag) == 0 && flag == 1 && at == 3, "canonical TRUE");

                const der_u8 b_false[] = {0x01, 0x01, 0x00};
                at = 0; flag = 0;
                CHECK(asn1_der_true(b_false, 3, &at, &flag) == -1 && flag == 0, "DER FALSE rejected");

                const der_u8 b_noncanon[] = {0x01, 0x01, 0x01};
                at = 0; flag = 0;
                CHECK(asn1_der_true(b_noncanon, 3, &at, &flag) == -1, "0x01 boolean rejected");

                const der_u8 b_longlen[] = {0x01, 0x81, 0x01, 0xff};
                at = 0; flag = 0;
                CHECK(asn1_der_true(b_longlen, 4, &at, &flag) == -1, "non-minimal bool length rejected");

                const der_u8 b_other[] = {0x02, 0x01, 0x00};
                at = 0; flag = 0;
                CHECK(asn1_der_true(b_other, 3, &at, &flag) == 0 && flag == 0 && at == 0, "absent bool (other tag)");

                at = 3; flag = 0;
                CHECK(asn1_der_true(b_other, 3, &at, &flag) == 0 && flag == 0 && at == 3, "absent bool (at==stop)");
                at = 0;
                CHECK(asn1_der_true(b_true, 2, &at, &flag) == -1, "truncated bool");
        }

        /* 4. asn1_der_null_or_absent */
        {
                const der_u8 b_null[] = {0x05, 0x00};
                const der_u8 b_bad1[] = {0x05, 0x01};
                const der_u8 b_bad2[] = {0x06, 0x00};
                CHECK(asn1_der_null_or_absent(NULL, 5, 5) == 1, "absent");
                CHECK(asn1_der_null_or_absent(b_null, 0, 2) == 1, "DER NULL");
                CHECK(asn1_der_null_or_absent(b_bad1, 0, 2) == 0, "bad NULL len");
                CHECK(asn1_der_null_or_absent(b_bad2, 0, 2) == 0, "bad NULL tag");
                CHECK(asn1_der_null_or_absent(b_null, 0, 1) == 0, "truncated NULL");
                CHECK(asn1_der_null_or_absent(b_null, 2, 1) == 0, "at > stop");
                CHECK(asn1_der_null_or_absent(b_null, UINT64_MAX - 1, 0) == 0, "at+2 wraparound safe");
        }

        /* 5. asn1_der_oid_content & asn1_der_enter_oid */
        {
                const der_u8 oid_sha256_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
                const der_u8 oid_inner80[]    = {0x2a, 0x81, 0x80, 0x00}; /* 16384: valid! */
                const der_u8 oid_lead80[]     = {0x80, 0x01};
                const der_u8 oid_mid_lead80[] = {0x2a, 0x80, 0x01};
                const der_u8 oid_trunc[]      = {0x2a, 0x86, 0x48, 0x86};
                CHECK(asn1_der_oid_content(oid_sha256_rsa, sizeof(oid_sha256_rsa)) == 1, "valid sha256WithRSA OID");
                CHECK(asn1_der_oid_content(oid_inner80, sizeof(oid_inner80)) == 1, "valid inner 0x80 arc");
                CHECK(asn1_der_oid_content(oid_lead80, sizeof(oid_lead80)) == 0, "leading 0x80 rejected");
                CHECK(asn1_der_oid_content(oid_mid_lead80, sizeof(oid_mid_lead80)) == 0, "mid leading 0x80 rejected");
                CHECK(asn1_der_oid_content(oid_trunc, sizeof(oid_trunc)) == 0, "truncated continuation rejected");
                CHECK(asn1_der_oid_content(oid_sha256_rsa, 0) == 0, "empty OID rejected");

                const der_u8 tlv_oid[] = {0x06, 0x04, 0x2a, 0x81, 0x80, 0x00};
                const der_u8 tlv_oid_empty[] = {0x06, 0x00};
                const der_u8 tlv_oid_bad[] = {0x06, 0x02, 0x80, 0x01};
                der_u64 at = 0, stop = 0;
                CHECK(asn1_der_enter_oid(tlv_oid, sizeof(tlv_oid), &at, &stop) == 0 && at == 2 && stop == 6, "enter_oid ok");
                at = 0;
                CHECK(asn1_der_enter_oid(tlv_oid_empty, sizeof(tlv_oid_empty), &at, &stop) == -1, "enter_oid empty");
                at = 0;
                CHECK(asn1_der_enter_oid(tlv_oid_bad, sizeof(tlv_oid_bad), &at, &stop) == -1 && at == 0, "enter_oid bad");
        }

        /* 6. asn1_der_integer & asn1_der_positive_integer */
        {
                const der_u8 i_zero[]    = {0x00};
                const der_u8 i_neg1[]    = {0xff};
                const der_u8 i_pad_ok[]  = {0x00, 0x80};
                const der_u8 i_pad_bad[] = {0x00, 0x7f};
                const der_u8 i_ff_ok[]   = {0xff, 0x7f};
                const der_u8 i_ff_bad[]  = {0xff, 0x80};
                const der_u8 i_pos[]     = {0x01, 0x02};
                CHECK(asn1_der_integer(i_zero, 0) == 0, "empty int");
                CHECK(asn1_der_integer(i_zero, 1) == 1, "int 0");
                CHECK(asn1_der_integer(i_neg1, 1) == 1, "int -1");
                CHECK(asn1_der_integer(i_pad_ok, 2) == 1, "int 00 80 ok");
                CHECK(asn1_der_integer(i_pad_bad, 2) == 0, "int 00 7f non-canonical");
                CHECK(asn1_der_integer(i_ff_ok, 2) == 1, "int ff 7f ok");
                CHECK(asn1_der_integer(i_ff_bad, 2) == 0, "int ff 80 non-canonical");

                der_u64 vat = 0, vlen = 0;
                CHECK(asn1_der_positive_integer(i_zero, 0, 1, &vat, &vlen) == 0, "pos_int 0 rejected");
                CHECK(asn1_der_positive_integer(i_neg1, 0, 1, &vat, &vlen) == 0, "pos_int neg rejected");
                CHECK(asn1_der_positive_integer(i_pad_bad, 0, 2, &vat, &vlen) == 0, "pos_int 00 7f rejected");
                CHECK(asn1_der_positive_integer(i_pad_ok, 0, 2, &vat, &vlen) == 1 && vat == 1 && vlen == 1, "pos_int 00 80 stripped");
                CHECK(asn1_der_positive_integer(i_pos, 0, 2, &vat, &vlen) == 1 && vat == 0 && vlen == 2, "pos_int 01 02 ok");
        }

        /* 7. asn1_der_named_bits */
        {
                const der_u8 nb_empty[]    = {0x00};
                const der_u8 nb_empty_bad[] = {0x01};
                const der_u8 nb_u8[]       = {0x08, 0x80};
                const der_u8 nb_zero_end[] = {0x00, 0x00};
                const der_u8 nb_u0_ok[]    = {0x00, 0x01};
                const der_u8 nb_u7_ok[]    = {0x07, 0x80};
                const der_u8 nb_u7_bad[]   = {0x07, 0xc0};
                const der_u8 nb_u3_ok[]    = {0x03, 0xff, 0x08};
                const der_u8 nb_u3_pad[]   = {0x03, 0xff, 0x0c};
                const der_u8 nb_u3_zero[]  = {0x03, 0xff, 0x10};
                CHECK(asn1_der_named_bits(nb_empty, 0) == 0, "named_bits empty");
                CHECK(asn1_der_named_bits(nb_empty, 1) == 1, "named_bits 00");
                CHECK(asn1_der_named_bits(nb_empty_bad, 1) == 0, "named_bits 01 len 1");
                CHECK(asn1_der_named_bits(nb_u8, 2) == 0, "named_bits unused > 7");
                CHECK(asn1_der_named_bits(nb_zero_end, 2) == 0, "named_bits trailing 00");
                CHECK(asn1_der_named_bits(nb_u0_ok, 2) == 1, "named_bits 00 01");
                CHECK(asn1_der_named_bits(nb_u7_ok, 2) == 1, "named_bits 07 80");
                CHECK(asn1_der_named_bits(nb_u7_bad, 2) == 0, "named_bits 07 c0 non-zero pad");
                CHECK(asn1_der_named_bits(nb_u3_ok, 3) == 1, "named_bits 03 ff 08");
                CHECK(asn1_der_named_bits(nb_u3_pad, 3) == 0, "named_bits 03 ff 0c non-zero pad");
                CHECK(asn1_der_named_bits(nb_u3_zero, 3) == 0, "named_bits 03 ff 10 wrong unused");
        }

        /* 8. Dates: UTCTime pivot, GeneralizedTime RFC 5280 vs RFC 6960, leap years */
        {
                der_u64 v = 0;
                /* UTCTime 1950..2049 pivot */
                CHECK(asn1_der_date_value(0x17, (const der_u8 *)"500101000000Z", 13, &v) == 1 &&
                      v == 19500101000000ull, "UTC 1950");
                CHECK(asn1_der_date_value(0x17, (const der_u8 *)"491231235959Z", 13, &v) == 1 &&
                      v == 20491231235959ull, "UTC 2049");
                CHECK(asn1_der_date_value(0x17, (const der_u8 *)"000229120000Z", 13, &v) == 1 &&
                      v == 20000229120000ull, "UTC 2000-02-29 leap");
                CHECK(asn1_der_date_value(0x17, (const der_u8 *)"240229235959Z", 13, &v) == 1 &&
                      v == 20240229235959ull, "UTC 2024-02-29 leap");
                CHECK(asn1_der_date_value(0x17, (const der_u8 *)"250229000000Z", 13, &v) == 0,
                      "UTC 2025-02-29 non-leap rejected");

                /* GeneralizedTime RFC 5280 (>=2050) vs RFC 6960 OCSP (>=1950) */
                CHECK(asn1_der_date_value(0x18, (const der_u8 *)"20491231235959Z", 15, &v) == 0,
                      "GenTime 2049 rejected by date_value");
                CHECK(asn1_der_date_general((const der_u8 *)"20491231235959Z", 15, &v) == 1 &&
                      v == 20491231235959ull, "GenTime 2049 accepted by date_general");
                CHECK(asn1_der_date_value(0x18, (const der_u8 *)"20500101000000Z", 15, &v) == 1 &&
                      v == 20500101000000ull, "GenTime 2050 accepted by date_value");
                CHECK(asn1_der_date_general((const der_u8 *)"19500101000000Z", 15, &v) == 1 &&
                      v == 19500101000000ull, "GenTime 1950 accepted by date_general");
                CHECK(asn1_der_date_general((const der_u8 *)"19491231235959Z", 15, &v) == 0,
                      "GenTime 1949 rejected by date_general");
                CHECK(asn1_der_date_general((const der_u8 *)"19000229000000Z", 15, &v) == 0,
                      "GenTime 1900-02-29 rejected");
                CHECK(asn1_der_date_general((const der_u8 *)"20000229000000Z", 15, &v) == 1 &&
                      v == 20000229000000ull, "GenTime 2000-02-29 leap accepted");
                CHECK(asn1_der_date_value(0x18, (const der_u8 *)"21000229000000Z", 15, &v) == 0,
                      "GenTime 2100-02-29 century non-leap rejected");
                CHECK(asn1_der_date_value(0x18, (const der_u8 *)"21000228235959Z", 15, &v) == 1 &&
                      v == 21000228235959ull, "GenTime 2100-02-28 accepted");
                CHECK(asn1_der_date_value(0x18, (const der_u8 *)"24000229000000Z", 15, &v) == 1 &&
                      v == 24000229000000ull, "GenTime 2400-02-29 400-yr leap accepted");

                /* Month day limits */
                CHECK(asn1_der_date_general((const der_u8 *)"20260431000000Z", 15, &v) == 0, "Apr 31 rejected");
                CHECK(asn1_der_date_general((const der_u8 *)"20260430000000Z", 15, &v) == 1, "Apr 30 accepted");
                CHECK(asn1_der_date_general((const der_u8 *)"20260631000000Z", 15, &v) == 0, "Jun 31 rejected");
                CHECK(asn1_der_date_general((const der_u8 *)"20260931000000Z", 15, &v) == 0, "Sep 31 rejected");
                CHECK(asn1_der_date_general((const der_u8 *)"20261131000000Z", 15, &v) == 0, "Nov 31 rejected");
                CHECK(asn1_der_date_general((const der_u8 *)"20260731000000Z", 15, &v) == 1, "Jul 31 accepted");
                CHECK(asn1_der_date_general((const der_u8 *)"20260831000000Z", 15, &v) == 1, "Aug 31 accepted");

                /* asn1_der_date_seconds */
                CHECK(asn1_der_date_seconds(0) == 0, "seconds(0) == 0");
                CHECK(asn1_der_date_seconds(20240301000000ull) - asn1_der_date_seconds(20240228000000ull) == 2ull * 86400ull,
                      "2024 leap Feb has 29 days");
                CHECK(asn1_der_date_seconds(21000301000000ull) - asn1_der_date_seconds(21000228000000ull) == 86400ull,
                      "2100 non-leap Feb has 28 days");
                CHECK(asn1_der_date_seconds(20000301000000ull) - asn1_der_date_seconds(20000228000000ull) == 2ull * 86400ull,
                      "2000 leap Feb has 29 days");
        }
}

static void test_guard_pages(void)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        /* [PROT_NONE guard] [RW data page] [PROT_NONE guard] */
        uint8_t *base = (uint8_t *)mmap(NULL, 3 * pg, PROT_NONE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(base != MAP_FAILED, "mmap guard pages");
        if (base == MAP_FAILED) return;
        CHECK(mprotect(base + pg, pg, PROT_READ | PROT_WRITE) == 0, "mprotect middle page");

        uint8_t *lo = base + pg;       /* flush against leading PROT_NONE */
        uint8_t *hi = base + 2 * pg;   /* end of RW page; hi - len is flush against trailing PROT_NONE */

        /* Test every buffer-reading function flush against BOTH leading and trailing guard pages */
        for (int side = 0; side < 2; side++) {
                /* 1. TLV functions (short & long & truncated) */
                const der_u8 sample[] = {0x30, 0x05, 0x06, 0x03, 0x2a, 0x86, 0x48};
                for (size_t n = 0; n <= sizeof(sample); n++) {
                        uint8_t *p = side == 0 ? lo : (hi - n);
                        if (n) memcpy(p, sample, n);
                        der_u64 at = 0, st = 0, ln = 0;
                        const der_u8 *val = NULL;
                        (void)asn1_der_length(p, n, &at, &ln);
                        at = 0; (void)asn1_der_enter(p, n, 0x30, &at, &st);
                        at = 0; (void)asn1_der_skip(p, n, &at);
                        at = 0; (void)asn1_der_take(p, n, 0x30, &at, &val, &ln);
                        at = 0; (void)asn1_der_enter_oid(p, n, &at, &st);
                        der_u8 flag = 0;
                        at = 0; (void)asn1_der_true(p, n, &at, &flag);
                        (void)asn1_der_null_or_absent(p, 0, n);
                        (void)asn1_der_oid_content(p, n);
                        (void)asn1_der_integer(p, n);
                        (void)asn1_der_named_bits(p, n);
                        der_u64 vat = 0, vlen = 0;
                        (void)asn1_der_positive_integer(p, 0, n, &vat, &vlen);
                }
                /* 2. Dates flush against guard pages */
                const char *utc = "261127172653Z";
                const char *gen = "20500228235959Z";
                der_u64 v = 0;
                uint8_t *pu = side == 0 ? lo : (hi - 13);
                memcpy(pu, utc, 13);
                CHECK(asn1_der_date_value(0x17, pu, 13, &v) == 1 && v == 20261127172653ull, "guard UTC");
                uint8_t *pgn = side == 0 ? lo : (hi - 15);
                memcpy(pgn, gen, 15);
                CHECK(asn1_der_date_value(0x18, pgn, 15, &v) == 1 && v == 20500228235959ull, "guard Gen");
                CHECK(asn1_der_date_general(pgn, 15, &v) == 1 && v == 20500228235959ull, "guard GenOCSP");
        }

        munmap(base, 3 * pg);
}

static void test_differential_fuzz(void)
{
        der_u8 buf[32];
        for (int iter = 0; iter < 100000; iter++) {
                der_u64 sz = xorshift64() % 20u;
                for (der_u64 i = 0; i < sz; i++)
                        buf[i] = (der_u8)xorshift64();
                /* Bias some iterations toward valid structures */
                if ((iter & 3) == 0 && sz >= 2) {
                        buf[0] = (der_u8)(xorshift64() & 1 ? 0x06 : 0x30);
                        buf[1] = (der_u8)(xorshift64() % (sz + 3));
                }
                der_u64 start_at = xorshift64() % (sz + 3u);
                der_u32 tag = (der_u32)(xorshift64() & 0xffu);

                /* length */
                der_u64 a1 = start_at, a2 = start_at, l1 = 0xdead, l2 = 0xdead;
                der_i32 r1 = asn1_der_length(buf, sz, &a1, &l1);
                der_i32 r2 = ref_asn1_der_length(buf, sz, &a2, &l2);
                CHECK(r1 == r2 && a1 == a2 && l1 == l2, "fuzz length");

                /* enter */
                a1 = a2 = start_at;
                der_u64 s1 = 0xbeef, s2 = 0xbeef;
                r1 = asn1_der_enter(buf, sz, tag, &a1, &s1);
                r2 = ref_asn1_der_enter(buf, sz, tag, &a2, &s2);
                CHECK(r1 == r2 && a1 == a2 && s1 == s2, "fuzz enter");

                /* skip */
                a1 = a2 = start_at;
                r1 = asn1_der_skip(buf, sz, &a1);
                r2 = ref_asn1_der_skip(buf, sz, &a2);
                CHECK(r1 == r2 && a1 == a2, "fuzz skip");

                /* take */
                a1 = a2 = start_at;
                l1 = l2 = 0xcafe;
                const der_u8 *v1 = NULL, *v2 = NULL;
                r1 = asn1_der_take(buf, sz, tag, &a1, &v1, &l1);
                r2 = ref_asn1_der_take(buf, sz, tag, &a2, &v2, &l2);
                CHECK(r1 == r2 && a1 == a2 && v1 == v2 && l1 == l2, "fuzz take");

                /* true */
                a1 = a2 = start_at;
                der_u8 f1 = 0, f2 = 0;
                r1 = asn1_der_true(buf, sz, &a1, &f1);
                r2 = ref_asn1_der_true(buf, sz, &a2, &f2);
                CHECK(r1 == r2 && a1 == a2 && f1 == f2, "fuzz true");

                /* null_or_absent */
                CHECK(asn1_der_null_or_absent(buf, start_at, sz) ==
                      ref_asn1_der_null_or_absent(buf, start_at, sz), "fuzz null_or_absent");

                /* oid_content & enter_oid */
                CHECK(asn1_der_oid_content(buf, sz) ==
                      ref_asn1_der_oid_content(buf, sz), "fuzz oid_content");
                a1 = a2 = start_at; s1 = s2 = 0;
                r1 = asn1_der_enter_oid(buf, sz, &a1, &s1);
                r2 = ref_asn1_der_enter_oid(buf, sz, &a2, &s2);
                CHECK(r1 == r2 && a1 == a2 && s1 == s2, "fuzz enter_oid");

                /* integer & named_bits & positive_integer */
                CHECK(asn1_der_integer(buf, sz) == ref_asn1_der_integer(buf, sz), "fuzz integer");
                CHECK(asn1_der_named_bits(buf, sz) == ref_asn1_der_named_bits(buf, sz), "fuzz named_bits");
                der_u64 va1 = 0, va2 = 0, vl1 = 0, vl2 = 0;
                r1 = asn1_der_positive_integer(buf, start_at, sz, &va1, &vl1);
                r2 = ref_asn1_der_positive_integer(buf, start_at, sz, &va2, &vl2);
                CHECK(r1 == r2 && va1 == va2 && vl1 == vl2, "fuzz positive_integer");

                /* dates: generate random-ish calendar strings */
                der_u8 dbuf[16];
                unsigned y = (unsigned)(xorshift64() % 3000u);
                unsigned m = (unsigned)(xorshift64() % 15u);
                unsigned d = (unsigned)(xorshift64() % 34u);
                unsigned H = (unsigned)(xorshift64() % 26u);
                unsigned M = (unsigned)(xorshift64() % 62u);
                unsigned S = (unsigned)(xorshift64() % 62u);
                int is_utc = (int)(xorshift64() & 1u);
                der_u64 dlen = is_utc ? 13u : 15u;
                if (is_utc) {
                        snprintf((char *)dbuf, sizeof(dbuf), "%02u%02u%02u%02u%02u%02uZ",
                                 y % 100u, m % 100u, d % 100u, H % 100u, M % 100u, S % 100u);
                } else {
                        snprintf((char *)dbuf, sizeof(dbuf), "%04u%02u%02u%02u%02u%02uZ",
                                 y % 10000u, m % 100u, d % 100u, H % 100u, M % 100u, S % 100u);
                }
                if ((iter & 31) == 0) dbuf[xorshift64() % dlen] ^= 0x10;
                der_u32 dtag = is_utc ? 0x17u : 0x18u;
                der_u64 dv1 = 0x1111, dv2 = 0x1111;
                r1 = asn1_der_date_value(dtag, dbuf, dlen, &dv1);
                r2 = ref_asn1_der_date_value(dtag, dbuf, dlen, &dv2);
                CHECK(r1 == r2 && dv1 == dv2, "fuzz date_value");

                dv1 = dv2 = 0x2222;
                r1 = asn1_der_date_general(dbuf, dlen, &dv1);
                r2 = ref_asn1_der_date_general(dbuf, dlen, &dv2);
                CHECK(r1 == r2 && dv1 == dv2, "fuzz date_general");

                /* date_seconds */
                der_u64 dval = (r1 == 1) ? dv1 : (xorshift64() % 99999999999999ull);
                CHECK(asn1_der_date_seconds(dval) == ref_asn1_der_date_seconds(dval), "fuzz date_seconds");

                if (g_fails > 20) break;
        }
}

static double now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void run_benchmarks(void)
{
        const int N = 2000000;
        const der_u8 seq_tlv[] = {
                0x30, 0x81, 0x85, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b,
                0x05, 0x00, 0x01, 0x01, 0xff
        };
        der_u8 big[160];
        memset(big, 0x55, sizeof(big));
        memcpy(big, seq_tlv, sizeof(seq_tlv));
        const der_u8 *utc = (const der_u8 *)"261127172653Z";
        const der_u8 *gen = (const der_u8 *)"20261127172653Z";
        const der_u8 nb[] = {0x03, 0xff, 0x08};
        const der_u8 posint[] = {0x00, 0x9f, 0x12, 0x34};
        volatile der_u64 sink = 0;

        double t0, t1, asm_ns, ref_ns;

        /* 1. TLV enter + oid + null + true */
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                der_u64 at = 0, st = 0, ost = 0;
                der_u8 fl = 0;
                asn1_der_enter(big, sizeof(big), 0x30, &at, &st);
                asn1_der_enter_oid(big, st, &at, &ost);
                at = ost;
                sink += (der_u64)asn1_der_null_or_absent(big, at, at + 2);
                at += 2;
                asn1_der_true(big, st, &at, &fl);
                sink += at + fl;
        }
        t1 = now_ns();
        asm_ns = (t1 - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                der_u64 at = 0, st = 0, ost = 0;
                der_u8 fl = 0;
                ref_asn1_der_enter(big, sizeof(big), 0x30, &at, &st);
                ref_asn1_der_enter_oid(big, st, &at, &ost);
                at = ost;
                sink += (der_u64)ref_asn1_der_null_or_absent(big, at, at + 2);
                at += 2;
                ref_asn1_der_true(big, st, &at, &fl);
                sink += at + fl;
        }
        t1 = now_ns();
        ref_ns = (t1 - t0) / N;
        printf("bench tlv+oid+null+true : ASM %6.2f ns/op | C-O2 %6.2f ns/op | speedup %.2fx\n",
               asm_ns, ref_ns, ref_ns / asm_ns);

        /* 2. integer + named_bits + positive_integer */
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                der_u64 va = 0, vl = 0;
                sink += (der_u64)asn1_der_integer(posint, sizeof(posint));
                sink += (der_u64)asn1_der_named_bits(nb, sizeof(nb));
                sink += (der_u64)asn1_der_positive_integer(posint, 0, sizeof(posint), &va, &vl);
                sink += va + vl;
        }
        t1 = now_ns();
        asm_ns = (t1 - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                der_u64 va = 0, vl = 0;
                sink += (der_u64)ref_asn1_der_integer(posint, sizeof(posint));
                sink += (der_u64)ref_asn1_der_named_bits(nb, sizeof(nb));
                sink += (der_u64)ref_asn1_der_positive_integer(posint, 0, sizeof(posint), &va, &vl);
                sink += va + vl;
        }
        t1 = now_ns();
        ref_ns = (t1 - t0) / N;
        printf("bench int+bits+posint   : ASM %6.2f ns/op | C-O2 %6.2f ns/op | speedup %.2fx\n",
               asm_ns, ref_ns, ref_ns / asm_ns);

        /* 3. date_value + date_general + date_seconds */
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                der_u64 v1 = 0, v2 = 0;
                asn1_der_date_value(0x17, utc, 13, &v1);
                asn1_der_date_general(gen, 15, &v2);
                sink += asn1_der_date_seconds(v1) + asn1_der_date_seconds(v2);
        }
        t1 = now_ns();
        asm_ns = (t1 - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                der_u64 v1 = 0, v2 = 0;
                ref_asn1_der_date_value(0x17, utc, 13, &v1);
                ref_asn1_der_date_general(gen, 15, &v2);
                sink += ref_asn1_der_date_seconds(v1) + ref_asn1_der_date_seconds(v2);
        }
        t1 = now_ns();
        ref_ns = (t1 - t0) / N;
        printf("bench dates+seconds     : ASM %6.2f ns/op | C-O2 %6.2f ns/op | speedup %.2fx\n",
               asm_ns, ref_ns, ref_ns / asm_ns);
        (void)sink;
}

int main(void)
{
        test_security_and_edges();
        test_guard_pages();
        test_differential_fuzz();
        if (g_fails) {
                fprintf(stderr, "FAILED: %d errors\n", g_fails);
                return 1;
        }
        printf("ALL ASN.1 DER TESTS PASSED (edges + security + guard-pages + 100k fuzz)\n");
        run_benchmarks();
        return 0;
}
