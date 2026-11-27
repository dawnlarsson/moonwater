/*
        Exhaustive RFC 1035 security, edge-case, guard-page, differential-fuzz,
        and speed/size proof for src/net/dns_wire.asm.c (arm64, x86-64, riscv64).

        Build & run:
          cc -O2 -Wall -Werror test/dns_wire.c -o /tmp/t_dns && /tmp/t_dns
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/dns_wire.c -o /tmp/t_dns_asan && /tmp/t_dns_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define NET_DNS_WIRE_IMPLEMENTATION
#include "../src/net/dns_wire.asm.c"

/* ── -O2 C Reference Implementations (exact parity with net.c / ipp.c) ───── */

static __attribute__((noinline)) dw_i64 ref_dns_wire_write_name(
        dw_u8 *into, dw_u64 room, const char *name)
{
        if (!into || !name) return DW_MALFORMED;
        dw_u64 used = 0;
        while (*name) {
                const char *dot = name;
                while (*dot && *dot != '.') dot++;
                dw_u64 mark = used++;
                dw_u64 length = (dw_u64)(dot - name);
                if (used >= room || !length || length > 63 || length > room - used)
                        return DW_MALFORMED;
                memcpy(into + used, name, (size_t)length);
                into[mark] = (dw_u8)length;
                used += length;
                name = dot + (*dot == '.');
        }
        if (used + 1 > room || used + 1 > DW_NAME_MAX)
                return DW_MALFORMED;
        into[used++] = 0;
        return (dw_i64)used;
}

static __attribute__((noinline)) dw_i64 ref_dns_wire_copy_name(
        const dw_u8 *message, dw_u64 size, dw_u64 at,
        dw_u8 *into, dw_u64 room,
        dw_u64 *ended_out, dw_u64 *pointers_io)
{
        if (ended_out) *ended_out = 0;
        if (!message) return DW_MALFORMED;
        dw_u64 cap = room < DW_NAME_MAX ? room : DW_NAME_MAX;
        dw_u64 ceiling = size;
        dw_u64 pos = at;
        dw_u64 used = 0;
        dw_u64 jumps = 0;
        dw_u64 ended = 0;

        for (;;) {
                if (pos >= ceiling) return DW_MALFORMED;
                dw_u64 here = pos;
                dw_u8 length = message[pos++];

                if ((length & 0xc0u) == 0xc0u) {
                        if (pos >= ceiling) return DW_MALFORMED;
                        dw_u64 target = ((dw_u64)(length & 0x3fu) << 8) | message[pos++];
                        if (!ended) ended = here + 2;
                        if (target >= here || ++jumps > DW_POINTER_HOPS)
                                return DW_MALFORMED;
                        ceiling = here;
                        pos = target;
                        continue;
                }
                if ((length & 0xc0u) != 0) return DW_MALFORMED;
                if (length > ceiling - pos) return DW_MALFORMED;
                if (1 + (dw_u64)length > cap - used) return DW_MALFORMED;
                if (into) {
                        into[used] = length;
                        if (length) memcpy(into + used + 1, message + pos, length);
                }
                used += 1 + (dw_u64)length;
                pos += length;
                if (!length) {
                        if (pointers_io) {
                                if (*pointers_io < jumps) return DW_MALFORMED;
                                *pointers_io -= jumps;
                        }
                        if (!ended) ended = pos;
                        if (ended_out) *ended_out = ended;
                        return (dw_i64)used;
                }
        }
}

static __attribute__((noinline)) dw_i64 ref_dns_wire_skip_name(
        const dw_u8 *message, dw_u64 size, dw_u64 at)
{
        dw_u8 name[256];
        dw_u64 ended = 0;
        return ref_dns_wire_copy_name(message, size, at, name, sizeof(name),
                                      &ended, NULL) < 0
                   ? DW_MALFORMED : (dw_i64)ended;
}

static __attribute__((noinline)) dw_i32 ref_dns_wire_reply_identity(
        const dw_u8 *reply, dw_u64 size, dw_u32 id,
        const dw_u8 *request, dw_u64 question_length, dw_u32 folded)
{
        if (!reply || !request) return 0;
        if (size < DW_HEADER || question_length > size - DW_HEADER) return 0;
        dw_u32 asked = ((dw_u32)reply[0] << 8) | reply[1];
        dw_u32 questions = ((dw_u32)reply[4] << 8) | reply[5];
        if (asked != (id & 0xffffu) || questions != 1u) return 0;
        const dw_u8 *q = reply + DW_HEADER;
        const dw_u8 *r = request + DW_HEADER;
        if (!folded) {
                return memcmp(q, r, (size_t)question_length) == 0 ? 1 : 0;
        }
        for (dw_u64 i = 0; i < question_length; i++) {
                dw_u8 a = q[i], b = r[i];
                if (a >= 'A' && a <= 'Z') a = (dw_u8)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (dw_u8)(b + 32);
                if (a != b) return 0;
        }
        return 1;
}

static __attribute__((noinline)) dw_i64 ref_dns_wire_records_end(
        const dw_u8 *message, dw_u64 size, dw_u64 at, dw_u64 count, dw_u64 max_pointers)
{
        dw_u64 pointers = max_pointers;
        for (dw_u64 record = 0; record < count; record++) {
                dw_u8 owner[256];
                dw_u64 ended = 0;
                dw_i64 len = ref_dns_wire_copy_name(message, size, at, owner,
                                                    sizeof(owner), &ended, &pointers);
                if (len < 0) return DW_MALFORMED;
                if (ended > size || size - ended < 10) return DW_MALFORMED;
                dw_u64 rdlen = ((dw_u64)message[ended + 8] << 8) | message[ended + 9];
                ended += 10;
                if (rdlen > size - ended) return DW_MALFORMED;
                at = ended + rdlen;
        }
        return (dw_i64)at;
}

static __attribute__((noinline)) dw_u64 ref_dns_wire_to_host(
        const dw_u8 *wire, dw_u64 len, dw_u8 *out, dw_u64 room)
{
        dw_u64 at = 0, used = 0;
        if (!out || !room) return 0;
        if (!wire) {
                if (len) return 0;
                out[0] = 0;
                return 0;
        }
        while (at < len) {
                dw_u8 l = wire[at++];
                if (!l) break;
                if (l > 63 || l > len - at) return 0;
                if (used) {
                        if (used + 1 >= room) return 0;
                        out[used++] = '.';
                }
                if (l >= room - used) return 0;
                memcpy(out + used, wire + at, l);
                used += l;
                at += l;
        }
        out[used] = 0;
        return used;
}

/* ── Test Harness ─────────────────────────────────────────────────────────── */

static int g_fails = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
        g_checks++; \
        if (!(cond)) { \
                fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
                g_fails++; \
        } \
} while (0)

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rng_next(void) {
        uint64_t x = rng_state;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        return rng_state = x;
}

static void check_write_name(const char *name, dw_u64 room) {
        dw_u8 a[300], b[300];
        memset(a, 0xaa, sizeof(a));
        memset(b, 0xaa, sizeof(b));
        dw_i64 ra = dns_wire_write_name(a, room, name);
        dw_i64 rb = ref_dns_wire_write_name(b, room, name);
        CHECK(ra == rb, "write_name return mismatch");
        if (ra > 0) {
                CHECK(memcmp(a, b, (size_t)ra) == 0, "write_name bytes mismatch");
                if ((size_t)ra < sizeof(a))
                        CHECK(a[ra] == 0xaa, "write_name overwrote past return length");
        }
}

static void check_copy_and_skip(const dw_u8 *msg, dw_u64 size, dw_u64 at,
                                dw_u64 room, dw_u64 init_ptrs, int use_ptrs)
{
        dw_u8 a[300], b[300];
        memset(a, 0x55, sizeof(a));
        memset(b, 0x55, sizeof(b));
        dw_u64 ea = 0xdeadbeef, eb = 0xdeadbeef;
        dw_u64 pa = init_ptrs, pb = init_ptrs;

        dw_i64 ra = dns_wire_copy_name(msg, size, at, a, room, &ea, use_ptrs ? &pa : NULL);
        dw_i64 rb = ref_dns_wire_copy_name(msg, size, at, b, room, &eb, use_ptrs ? &pb : NULL);
        CHECK(ra == rb, "copy_name return mismatch");
        if (ra >= 0) {
                CHECK(ea == eb, "copy_name ended mismatch");
                CHECK(pa == pb, "copy_name pointers_io mismatch");
                CHECK(memcmp(a, b, (size_t)ra) == 0, "copy_name bytes mismatch");
                if ((size_t)ra < sizeof(a))
                        CHECK(a[ra] == 0x55, "copy_name overwrote past return length");
        } else {
                CHECK(pa == init_ptrs && pb == init_ptrs, "copy_name modified ptrs on error");
        }

        /* Also verify into == NULL mode */
        dw_u64 en = 0, pn = init_ptrs;
        dw_i64 rn = dns_wire_copy_name(msg, size, at, NULL, room, &en, use_ptrs ? &pn : NULL);
        CHECK(rn == rb, "copy_name(NULL into) return mismatch");
        if (rn >= 0) {
                CHECK(en == eb && pn == pb, "copy_name(NULL into) meta mismatch");
        }

        /* Verify skip_name */
        dw_i64 sa = dns_wire_skip_name(msg, size, at);
        dw_i64 sb = ref_dns_wire_skip_name(msg, size, at);
        CHECK(sa == sb, "skip_name return mismatch");
}

/* ── Edge & Security Tests ────────────────────────────────────────────────── */

static void test_write_name_edges(void) {
        check_write_name("", 0);
        check_write_name("", 1);
        check_write_name("", 255);
        check_write_name(".", 10);
        check_write_name("..", 10);
        check_write_name(".a", 10);
        check_write_name("a..b", 10);
        check_write_name("a.", 10);
        check_write_name("a.", 3);
        check_write_name("a.", 2);
        check_write_name("dawning.dev", 13);
        check_write_name("dawning.dev", 12);
        check_write_name("dawning.dev.", 13);
        check_write_name("dawning.dev.", 12);
        check_write_name("dawning.dev..", 20);

        /* 63-byte label (valid) vs 64-byte label (invalid) */
        char l63[64], l64[65];
        memset(l63, 'a', 63); l63[63] = 0;
        memset(l64, 'a', 64); l64[64] = 0;
        check_write_name(l63, 65);
        check_write_name(l63, 64);
        check_write_name(l64, 100);

        /* 255-byte wire name (valid: 63+1+63+1+63+1+61+1+1 = 255) vs 256-byte (invalid) */
        char n255[260], n256[260];
        memset(n255, 'x', 253);
        n255[63] = '.'; n255[127] = '.'; n255[191] = '.';
        n255[253] = 0; /* wire len = 64+64+64+62+1 = 255 */
        check_write_name(n255, 255);
        check_write_name(n255, 256);
        check_write_name(n255, 254);

        memset(n256, 'x', 254);
        n256[63] = '.'; n256[127] = '.'; n256[191] = '.';
        n256[254] = 0; /* wire len = 64+64+64+63+1 = 256 -> must fail */
        check_write_name(n256, 300);
        check_write_name(n256, 256);

        /* NULL safety */
        dw_u8 buf[16];
        CHECK(dns_wire_write_name(NULL, 16, "a.b") == DW_MALFORMED, "write_name NULL into");
        CHECK(dns_wire_write_name(buf, 16, NULL) == DW_MALFORMED, "write_name NULL name");
        CHECK(dns_wire_write_name(buf, 0, "") == DW_MALFORMED, "write_name zero room");
}

static void test_copy_skip_security(void) {
        /* 1. Self-pointing compression loop at offset 12: 0xc0 0x0c */
        dw_u8 self_loop[32];
        memset(self_loop, 0, sizeof(self_loop));
        self_loop[12] = 0xc0; self_loop[13] = 0x0c;
        check_copy_and_skip(self_loop, sizeof(self_loop), 12, 255, 4096, 1);

        /* Self-pointer at offset 0: 0xc0 0x00 */
        dw_u8 zero_loop[4] = { 0xc0, 0x00, 0x00, 0x00 };
        check_copy_and_skip(zero_loop, sizeof(zero_loop), 0, 255, 4096, 1);

        /* 2. Mutual 2-hop cycle: offset 12 -> 20, offset 20 -> 12 */
        dw_u8 cycle2[32];
        memset(cycle2, 0, sizeof(cycle2));
        cycle2[12] = 0xc0; cycle2[13] = 0x14; /* -> 20 (forward!) */
        cycle2[20] = 0xc0; cycle2[21] = 0x0c; /* -> 12 (backward, then 12->20 rejected!) */
        check_copy_and_skip(cycle2, sizeof(cycle2), 12, 255, 4096, 1);
        check_copy_and_skip(cycle2, sizeof(cycle2), 20, 255, 4096, 1);

        /* Backward jump then forward label step back onto same pointer */
        dw_u8 sneak[32];
        memset(sneak, 0, sizeof(sneak));
        sneak[10] = 1; sneak[11] = 'a'; sneak[12] = 0xc0; sneak[13] = 0x0a;
        check_copy_and_skip(sneak, sizeof(sneak), 12, 255, 4096, 1);

        /* 3. 127-hop valid chain vs 128-hop invalid chain */
        dw_u8 chain[300];
        memset(chain, 0, sizeof(chain));
        chain[0] = 0; /* root at offset 0 */
        for (int i = 1; i <= 128; i++) {
                int off = i * 2;
                int prev = (i - 1) * 2;
                chain[off] = (dw_u8)(0xc0 | ((prev >> 8) & 0x3f));
                chain[off + 1] = (dw_u8)(prev & 0xff);
        }
        /* 127 hops from offset 254 -> valid! */
        check_copy_and_skip(chain, sizeof(chain), 254, 255, 4096, 1);
        /* 127 hops with pointer budget 127 -> valid! budget 126 -> fail! */
        check_copy_and_skip(chain, sizeof(chain), 254, 255, 127, 1);
        check_copy_and_skip(chain, sizeof(chain), 254, 255, 126, 1);
        /* 128 hops from offset 256 -> invalid! */
        check_copy_and_skip(chain, sizeof(chain), 256, 255, 4096, 1);

        /* 4. Truncated compression pointer (only 1 byte 0xc0 at end) */
        dw_u8 trunc_ptr[13];
        memset(trunc_ptr, 0, sizeof(trunc_ptr));
        trunc_ptr[12] = 0xc0;
        check_copy_and_skip(trunc_ptr, 13, 12, 255, 4096, 1);

        /* Pointer targeting 1 byte before itself (overlapping 0xc0) */
        dw_u8 overlap[16];
        memset(overlap, 0, sizeof(overlap));
        overlap[11] = 0xc0; overlap[12] = 0xc0; overlap[13] = 0x0b;
        check_copy_and_skip(overlap, sizeof(overlap), 12, 255, 4096, 1);

        /* 5. Reserved label bits 0x40 and 0x80 */
        dw_u8 res40[8] = { 0x40, 'a', 0 };
        dw_u8 res41[8] = { 0x41, 'a', 0 };
        dw_u8 res80[8] = { 0x80, 'a', 0 };
        dw_u8 resbf[8] = { 0xbf, 'a', 0 };
        check_copy_and_skip(res40, sizeof(res40), 0, 255, 4096, 1);
        check_copy_and_skip(res41, sizeof(res41), 0, 255, 4096, 1);
        check_copy_and_skip(res80, sizeof(res80), 0, 255, 4096, 1);
        check_copy_and_skip(resbf, sizeof(resbf), 0, 255, 4096, 1);

        /* 6. 255-byte valid vs 256-byte invalid expanded wire name */
        dw_u8 w255[260], w256[260];
        dw_u64 p = 0;
        w255[p++] = 63; memset(w255 + p, 'a', 63); p += 63;
        w255[p++] = 63; memset(w255 + p, 'b', 63); p += 63;
        w255[p++] = 63; memset(w255 + p, 'c', 63); p += 63;
        w255[p++] = 61; memset(w255 + p, 'd', 61); p += 61;
        w255[p++] = 0; /* total = 255 */
        check_copy_and_skip(w255, p, 0, 255, 4096, 1);
        check_copy_and_skip(w255, p, 0, 256, 4096, 1);
        check_copy_and_skip(w255, p, 0, 254, 4096, 1);

        p = 0;
        w256[p++] = 63; memset(w256 + p, 'a', 63); p += 63;
        w256[p++] = 63; memset(w256 + p, 'b', 63); p += 63;
        w256[p++] = 63; memset(w256 + p, 'c', 63); p += 63;
        w256[p++] = 62; memset(w256 + p, 'd', 62); p += 62;
        w256[p++] = 0; /* total = 256 -> must fail */
        check_copy_and_skip(w256, p, 0, 300, 4096, 1);

        /* 7. Root label, out-of-bounds at, NULL message */
        dw_u8 root[1] = { 0 };
        check_copy_and_skip(root, 1, 0, 1, 4096, 1);
        check_copy_and_skip(root, 1, 0, 0, 4096, 1);
        check_copy_and_skip(root, 1, 1, 255, 4096, 1);
        check_copy_and_skip(root, 1, 99, 255, 4096, 1);
        CHECK(dns_wire_copy_name(NULL, 10, 0, w255, 255, NULL, NULL) == DW_MALFORMED, "copy NULL msg");
        CHECK(dns_wire_skip_name(NULL, 10, 0) == DW_MALFORMED, "skip NULL msg");
}

static void test_reply_identity_edges(void) {
        dw_u8 req[64], rep[64];
        memset(req, 0, sizeof(req));
        memset(rep, 0, sizeof(rep));
        req[0] = 0x12; req[1] = 0x34; req[5] = 1;
        rep[0] = 0x12; rep[1] = 0x34; rep[5] = 1;
        const char *q = "\x07" "DaWnInG" "\x03" "DeV" "\x00\x00\x01\x00\x01";
        dw_u64 qlen = 17;
        memcpy(req + 12, q, (size_t)qlen);
        memcpy(rep + 12, q, (size_t)qlen);

        CHECK(dns_wire_reply_identity(rep, 12 + qlen, 0x1234, req, qlen, 0) == 1, "id exact");
        CHECK(dns_wire_reply_identity(rep, 12 + qlen, 0x1234, req, qlen, 1) == 1, "id folded");
        CHECK(dns_wire_reply_identity(rep, 12 + qlen, 0x1235, req, qlen, 0) == 0, "id wrong txid");
        CHECK(dns_wire_reply_identity(rep, 12 + qlen - 1, 0x1234, req, qlen, 0) == 0, "id short");
        CHECK(dns_wire_reply_identity(rep, 11, 0x1234, req, 0, 0) == 0, "id < 12");

        /* Case-folded match */
        memcpy(rep + 12, "\x07" "dawning" "\x03" "dev" "\x00\x00\x01\x00\x01", (size_t)qlen);
        CHECK(dns_wire_reply_identity(rep, 12 + qlen, 0x1234, req, qlen, 0) == 0, "id case exact fail");
        CHECK(dns_wire_reply_identity(rep, 12 + qlen, 0x1234, req, qlen, 1) == 1, "id case folded ok");

        /* Non-letter bytes differing by 0x20 MUST NOT match even when folded! */
        req[12] = '@'; rep[12] = '`'; /* 0x40 vs 0x60 */
        CHECK(dns_wire_reply_identity(rep, 13, 0x1234, req, 1, 1) == 0, "id @ vs ` must not fold");
        req[12] = '['; rep[12] = '{'; /* 0x5b vs 0x7b */
        CHECK(dns_wire_reply_identity(rep, 13, 0x1234, req, 1, 1) == 0, "id [ vs { must not fold");
        req[12] = 0xc1; rep[12] = 0xe1;
        CHECK(dns_wire_reply_identity(rep, 13, 0x1234, req, 1, 1) == 0, "id non-ASCII must not fold");

        /* QDCOUNT != 1 */
        req[12] = 'a'; rep[12] = 'a';
        rep[5] = 2;
        CHECK(dns_wire_reply_identity(rep, 13, 0x1234, req, 1, 0) == 0, "id qdcount=2");
        rep[4] = 1; rep[5] = 1;
        CHECK(dns_wire_reply_identity(rep, 13, 0x1234, req, 1, 0) == 0, "id qdcount=257");
        rep[4] = 0; rep[5] = 1;
        /* Upper bits in id/folded */
        CHECK(dns_wire_reply_identity(rep, 13, 0x99991234u, req, 1, 0) == 1, "id mask 16-bit");
        CHECK(dns_wire_reply_identity(NULL, 13, 0x1234, req, 1, 0) == 0, "id NULL reply");
        CHECK(dns_wire_reply_identity(rep, 13, 0x1234, NULL, 1, 0) == 0, "id NULL req");
}

static void test_records_end_edges(void) {
        /* Build a valid 2-record message with compression pointer in 2nd record */
        dw_u8 pkt[128];
        memset(pkt, 0, sizeof(pkt));
        /* Offset 12: \3www\3dev\0 (9 bytes -> ends at 21) */
        dw_u64 p = 12;
        pkt[p++] = 3; memcpy(pkt + p, "www", 3); p += 3;
        pkt[p++] = 3; memcpy(pkt + p, "dev", 3); p += 3;
        pkt[p++] = 0;
        /* RR1: type=1, class=1, ttl=60, rdlen=4, rdata=1.2.3.4 */
        pkt[p + 1] = 1; pkt[p + 3] = 1; pkt[p + 7] = 60; pkt[p + 9] = 4;
        p += 10 + 4;
        /* RR2: pointer to offset 12 (0xc0 0x0c), rdlen=2, rdata=0xaa 0xbb */
        pkt[p++] = 0xc0; pkt[p++] = 0x0c;
        pkt[p + 9] = 2;
        p += 10 + 2;

        CHECK(dns_wire_records_end(pkt, p, 12, 0, 4096) == 12, "records_end count=0");
        CHECK(dns_wire_records_end(pkt, p, 12, 1, 4096) == 35, "records_end count=1");
        CHECK(dns_wire_records_end(pkt, p, 12, 2, 4096) == (dw_i64)p, "records_end count=2");
        CHECK(dns_wire_records_end(pkt, p - 1, 12, 2, 4096) == DW_MALFORMED, "records_end truncated rdata");
        CHECK(dns_wire_records_end(pkt, p, 12, 2, 0) == DW_MALFORMED, "records_end ptr budget=0");
        CHECK(dns_wire_records_end(pkt, p, 12, 2, 1) == (dw_i64)p, "records_end ptr budget=1");
        CHECK(dns_wire_records_end(NULL, p, 12, 1, 4096) == DW_MALFORMED, "records_end NULL msg");
}

static void test_to_host_edges(void) {
        const dw_u8 w1[] = { 7, 'p','r','i','n','t','e','r', 5, 'l','o','c','a','l', 0 };
        dw_u8 a[64], b[64];
        for (dw_u64 room = 0; room <= 20; room++) {
                memset(a, 0x77, sizeof(a));
                memset(b, 0x77, sizeof(b));
                dw_u64 ra = dns_wire_to_host(w1, sizeof(w1), a, room);
                dw_u64 rb = ref_dns_wire_to_host(w1, sizeof(w1), b, room);
                CHECK(ra == rb, "to_host len mismatch");
                if (ra > 0) CHECK(memcmp(a, b, (size_t)ra + 1) == 0, "to_host bytes mismatch");
        }
        /* Invalid label > 63, truncated label, root, empty, NULL */
        const dw_u8 bad_len[] = { 64, 'a' };
        const dw_u8 trunc[] = { 5, 'a', 'b' };
        const dw_u8 root[] = { 0 };
        CHECK(dns_wire_to_host(bad_len, sizeof(bad_len), a, sizeof(a)) == 0, "to_host >63");
        CHECK(dns_wire_to_host(trunc, sizeof(trunc), a, sizeof(a)) == 0, "to_host trunc");
        CHECK(dns_wire_to_host(root, sizeof(root), a, sizeof(a)) == 0 && a[0] == 0, "to_host root");
        CHECK(dns_wire_to_host(NULL, 0, a, sizeof(a)) == 0 && a[0] == 0, "to_host NULL len=0");
        CHECK(dns_wire_to_host(NULL, 5, a, sizeof(a)) == 0, "to_host NULL len=5");
        CHECK(dns_wire_to_host(w1, sizeof(w1), NULL, 32) == 0, "to_host NULL out");
}

/* ── mmap Guard-Page Safety Tests (PROT_NONE before AND after) ────────────── */

static void test_guard_pages(void) {
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t psize = (size_t)ps;

        /* 3 pages: [PROT_NONE][RW][PROT_NONE] for input and for output */
        dw_u8 *in_map = mmap(NULL, psize * 3, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON, -1, 0);
        dw_u8 *out_map = mmap(NULL, psize * 3, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(in_map != MAP_FAILED && out_map != MAP_FAILED, "mmap guard pages");
        if (in_map == MAP_FAILED || out_map == MAP_FAILED) return;

        CHECK(mprotect(in_map, psize, PROT_NONE) == 0, "mprotect in before");
        CHECK(mprotect(in_map + psize * 2, psize, PROT_NONE) == 0, "mprotect in after");
        CHECK(mprotect(out_map, psize, PROT_NONE) == 0, "mprotect out before");
        CHECK(mprotect(out_map + psize * 2, psize, PROT_NONE) == 0, "mprotect out after");

        dw_u8 *in_lo = in_map + psize;
        dw_u8 *in_hi = in_map + psize * 2;
        dw_u8 *out_lo = out_map + psize;
        dw_u8 *out_hi = out_map + psize * 2;

        /* 1. write_name flush at start and end of guard pages */
        const char *hn = "alpha.beta.dawning.dev";
        size_t hlen = strlen(hn) + 1;
        memcpy(in_hi - hlen, hn, hlen);
        dw_i64 w1 = dns_wire_write_name(out_hi - 24, 24, (const char *)(in_hi - hlen));
        CHECK(w1 == 24, "guard write_name flush-end exact fit");
        dw_i64 w2 = dns_wire_write_name(out_hi - 23, 23, (const char *)(in_hi - hlen));
        CHECK(w2 == DW_MALFORMED, "guard write_name flush-end 1-byte short");
        memcpy(in_lo, hn, hlen);
        dw_i64 w3 = dns_wire_write_name(out_lo, 24, (const char *)in_lo);
        CHECK(w3 == 24, "guard write_name flush-start");

        /* 2. copy_name + skip_name flush against end of input & output pages */
        dw_u8 wire[24];
        memcpy(wire, out_lo, 24);
        memcpy(in_hi - 24, wire, 24);
        dw_u64 ended = 0, ptrs = 16;
        dw_i64 c1 = dns_wire_copy_name(in_hi - 24, 24, 0, out_hi - 24, 24, &ended, &ptrs);
        CHECK(c1 == 24 && ended == 24, "guard copy_name flush-end");
        dw_i64 c2 = dns_wire_copy_name(in_hi - 24, 24, 0, out_hi - 23, 23, &ended, &ptrs);
        CHECK(c2 == DW_MALFORMED, "guard copy_name out 1-byte short");
        /* Truncated label flush at end of input page */
        in_hi[-2] = 5; in_hi[-1] = 'x';
        CHECK(dns_wire_copy_name(in_hi - 2, 2, 0, out_lo, 64, &ended, NULL) == DW_MALFORMED,
              "guard copy_name truncated label at end");
        CHECK(dns_wire_skip_name(in_hi - 2, 2, 0) == DW_MALFORMED,
              "guard skip_name truncated label at end");
        /* Truncated pointer 0xc0 at last byte of input page */
        in_hi[-1] = 0xc0;
        CHECK(dns_wire_copy_name(in_hi - 1, 1, 0, out_lo, 64, &ended, NULL) == DW_MALFORMED,
              "guard copy_name truncated ptr at end");
        CHECK(dns_wire_skip_name(in_hi - 1, 1, 0) == DW_MALFORMED,
              "guard skip_name truncated ptr at end");

        /* 3. reply_identity flush at end of both pages */
        dw_u8 pkt[28];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0xab; pkt[1] = 0xcd; pkt[5] = 1;
        memcpy(pkt + 12, "TestLabel.Local", 16);
        memcpy(in_hi - 28, pkt, 28);
        memcpy(out_hi - 28, pkt, 28);
        CHECK(dns_wire_reply_identity(in_hi - 28, 28, 0xabcd, out_hi - 28, 16, 0) == 1,
              "guard reply_identity exact flush-end");
        CHECK(dns_wire_reply_identity(in_hi - 28, 28, 0xabcd, out_hi - 28, 16, 1) == 1,
              "guard reply_identity folded flush-end");
        CHECK(dns_wire_reply_identity(in_hi - 11, 11, 0xabcd, out_hi - 28, 0, 0) == 0,
              "guard reply_identity short header flush-end");

        /* 4. records_end flush at end of input page */
        dw_u8 rr[15];
        memset(rr, 0, sizeof(rr));
        rr[0] = 0; rr[10] = 4; /* root(1) + hdr(10) + rdata(4) = 15 */
        memcpy(in_hi - 15, rr, 15);
        CHECK(dns_wire_records_end(in_hi - 15, 15, 0, 1, 4096) == 15,
              "guard records_end flush-end");
        CHECK(dns_wire_records_end(in_hi - 14, 14, 0, 1, 4096) == DW_MALFORMED,
              "guard records_end truncated flush-end");

        /* 5. to_host flush at end of both pages */
        memcpy(in_hi - 24, wire, 24);
        dw_u64 h1 = dns_wire_to_host(in_hi - 24, 24, out_hi - 23, 23);
        CHECK(h1 == 22 && strcmp((const char *)(out_hi - 23), hn) == 0,
              "guard to_host flush-end exact");
        dw_u64 h2 = dns_wire_to_host(in_hi - 24, 24, out_hi - 22, 22);
        CHECK(h2 == 0, "guard to_host flush-end 1-byte short");

        munmap(in_map, psize * 3);
        munmap(out_map, psize * 3);
}

/* ── Randomized Differential Fuzzing ──────────────────────────────────────── */

static void test_fuzz(void) {
        /* 1. write_name fuzz */
        for (int i = 0; i < 4000; i++) {
                char s[280];
                int len = (int)(rng_next() % 275);
                for (int j = 0; j < len; j++) {
                        uint64_t r = rng_next() % 100;
                        if (r < 12) s[j] = '.';
                        else s[j] = (char)('a' + (rng_next() % 26));
                }
                s[len] = 0;
                dw_u64 room = rng_next() % 270;
                check_write_name(s, room);
        }

        /* 2. copy_name + skip_name fuzz (random + structured compressed packets) */
        for (int i = 0; i < 6000; i++) {
                dw_u8 buf[300];
                dw_u64 sz = rng_next() % 280;
                for (dw_u64 j = 0; j < sz; j++) {
                        uint64_t r = rng_next() % 100;
                        if (r < 15) buf[j] = 0;
                        else if (r < 30) buf[j] = (dw_u8)(0xc0 | (rng_next() & 0x01));
                        else if (r < 38) buf[j] = (dw_u8)(0x40 | (rng_next() & 0x7f));
                        else if (r < 65) buf[j] = (dw_u8)(1 + (rng_next() % 12));
                        else buf[j] = (dw_u8)(rng_next() & 0xff);
                }
                dw_u64 at = sz ? (rng_next() % (sz + 4)) : 0;
                dw_u64 room = rng_next() % 270;
                dw_u64 ptrs = rng_next() % 16;
                check_copy_and_skip(buf, sz, at, room, ptrs, (int)(rng_next() & 1));
        }

        /* 3. reply_identity fuzz */
        for (int i = 0; i < 4000; i++) {
                dw_u8 req[128], rep[128];
                dw_u64 sz = rng_next() % 120;
                for (dw_u64 j = 0; j < 128; j++) req[j] = (dw_u8)(rng_next() & 0xff);
                memcpy(rep, req, 128);
                dw_u32 id = ((dw_u32)req[0] << 8) | req[1];
                if (rng_next() % 4 == 0) rep[5] = 1, rep[4] = 0;
                if (rng_next() % 3 == 0 && sz > 12) {
                        /* flip case of random bytes */
                        for (dw_u64 j = 12; j < sz; j++) {
                                if (rng_next() & 1) rep[j] ^= 0x20;
                        }
                }
                if (rng_next() % 5 == 0) id ^= 1;
                dw_u64 qlen = sz > 12 ? (rng_next() % (sz - 11)) : (rng_next() % 16);
                dw_u32 folded = (dw_u32)(rng_next() & 1);
                dw_i32 ra = dns_wire_reply_identity(rep, sz, id, req, qlen, folded);
                dw_i32 rb = ref_dns_wire_reply_identity(rep, sz, id, req, qlen, folded);
                CHECK(ra == rb, "fuzz reply_identity mismatch");
        }

        /* 4. records_end fuzz */
        for (int i = 0; i < 3000; i++) {
                dw_u8 pkt[256];
                dw_u64 p = 12;
                memset(pkt, 0, sizeof(pkt));
                dw_u64 nrec = rng_next() % 6;
                for (dw_u64 r = 0; r < nrec && p + 20 < sizeof(pkt); r++) {
                        if (r > 0 && (rng_next() & 1)) {
                                pkt[p++] = 0xc0; pkt[p++] = 12;
                        } else {
                                dw_u8 ll = (dw_u8)(1 + (rng_next() % 6));
                                pkt[p++] = ll;
                                for (dw_u8 k = 0; k < ll; k++) pkt[p++] = (dw_u8)('a' + k);
                                pkt[p++] = 0;
                        }
                        dw_u8 rdl = (dw_u8)(rng_next() % 8);
                        if (p + 10 + rdl > sizeof(pkt)) break;
                        pkt[p + 9] = rdl;
                        p += 10 + rdl;
                }
                /* Sometimes corrupt a random byte or truncate */
                dw_u64 sz = p;
                if (rng_next() % 3 == 0 && sz > 0) sz = rng_next() % sz;
                if (rng_next() % 4 == 0 && sz > 12) pkt[12 + (rng_next() % (sz - 12))] ^= (dw_u8)(rng_next() & 0xff);
                dw_u64 maxp = rng_next() % 8;
                dw_i64 ra = dns_wire_records_end(pkt, sz, 12, nrec, maxp);
                dw_i64 rb = ref_dns_wire_records_end(pkt, sz, 12, nrec, maxp);
                CHECK(ra == rb, "fuzz records_end mismatch");
        }

        /* 5. to_host fuzz */
        for (int i = 0; i < 3000; i++) {
                dw_u8 w[128], a[128], b[128];
                dw_u64 len = rng_next() % 100;
                dw_u64 p = 0;
                while (p < len) {
                        dw_u8 l = (dw_u8)(rng_next() % 16);
                        if (rng_next() % 20 == 0) l = (dw_u8)(64 + (rng_next() % 10));
                        w[p++] = l;
                        if (!l) break;
                        for (dw_u8 k = 0; k < l && p < len; k++) w[p++] = (dw_u8)('a' + (k % 26));
                }
                dw_u64 room = rng_next() % 110;
                memset(a, 0x33, sizeof(a));
                memset(b, 0x33, sizeof(b));
                dw_u64 ra = dns_wire_to_host(w, len, a, room);
                dw_u64 rb = ref_dns_wire_to_host(w, len, b, room);
                CHECK(ra == rb, "fuzz to_host return mismatch");
                if (ra > 0) CHECK(memcmp(a, b, (size_t)ra + 1) == 0, "fuzz to_host bytes mismatch");
        }
}

/* ── Throughput Benchmark ─────────────────────────────────────────────────── */

static double now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static volatile uint64_t g_sink = 0;

static void bench_all(void) {
        const int N = 400000;
        dw_u8 out[256];
        dw_u64 ended = 0, ptrs = 0;

        /* 1. write_name */
        const char *host = "api.v2.service.dawning.dev";
        double t0 = now_ns();
        uint64_t s = 0;
        for (int i = 0; i < N; i++) s += (uint64_t)dns_wire_write_name(out, sizeof(out), host);
        double asm_wn = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += (uint64_t)ref_dns_wire_write_name(out, sizeof(out), host);
        double ref_wn = (now_ns() - t0) / N;

        /* 2. copy_name (compressed 2-hop DNS name) */
        dw_u8 pkt[128];
        memset(pkt, 0, sizeof(pkt));
        /* offset 12: \7dawning\3dev\0 (13 bytes, ends 25) */
        pkt[12] = 7; memcpy(pkt + 13, "dawning", 7);
        pkt[20] = 3; memcpy(pkt + 21, "dev", 3); pkt[24] = 0;
        /* offset 30: \3api\2v2\xc0\x0c */
        pkt[30] = 3; memcpy(pkt + 31, "api", 3);
        pkt[34] = 2; memcpy(pkt + 35, "v2", 2);
        pkt[37] = 0xc0; pkt[38] = 12;
        t0 = now_ns();
        for (int i = 0; i < N; i++) { ptrs = 4096; s += (uint64_t)dns_wire_copy_name(pkt, 64, 30, out, 255, &ended, &ptrs); }
        double asm_cn = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) { ptrs = 4096; s += (uint64_t)ref_dns_wire_copy_name(pkt, 64, 30, out, 255, &ended, &ptrs); }
        double ref_cn = (now_ns() - t0) / N;

        /* 3. skip_name */
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += (uint64_t)dns_wire_skip_name(pkt, 64, 30);
        double asm_sn = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += (uint64_t)ref_dns_wire_skip_name(pkt, 64, 30);
        double ref_sn = (now_ns() - t0) / N;

        /* 4. reply_identity (case-folded) */
        dw_u8 req[64], rep[64];
        memset(req, 0, sizeof(req)); memset(rep, 0, sizeof(rep));
        req[0] = 0x55; req[1] = 0xaa; req[5] = 1;
        rep[0] = 0x55; rep[1] = 0xaa; rep[5] = 1;
        memcpy(req + 12, "\x03" "Api" "\x07" "DaWnInG" "\x03" "DeV" "\x00\x00\x01\x00\x01", 21);
        memcpy(rep + 12, "\x03" "api" "\x07" "dawning" "\x03" "dev" "\x00\x00\x01\x00\x01", 21);
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                __asm__ volatile("" : : "r"(rep), "r"(req) : "memory");
                s += (uint64_t)dns_wire_reply_identity(rep, 33, 0x55aa, req, 21, 1);
        }
        double asm_ri = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                __asm__ volatile("" : : "r"(rep), "r"(req) : "memory");
                s += (uint64_t)ref_dns_wire_reply_identity(rep, 33, 0x55aa, req, 21, 1);
        }
        double ref_ri = (now_ns() - t0) / N;

        /* 5. records_end (4 compressed RRs) */
        dw_u8 rrpkt[128];
        memset(rrpkt, 0, sizeof(rrpkt));
        dw_u64 p = 12;
        rrpkt[p++] = 7; memcpy(rrpkt + p, "dawning", 7); p += 7;
        rrpkt[p++] = 3; memcpy(rrpkt + p, "dev", 3); p += 3;
        rrpkt[p++] = 0;
        dw_u64 rr_start = p;
        for (int r = 0; r < 4; r++) {
                rrpkt[p++] = 0xc0; rrpkt[p++] = 12;
                rrpkt[p + 9] = 4;
                p += 14;
        }
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += (uint64_t)dns_wire_records_end(rrpkt, p, rr_start, 4, 4096);
        double asm_re = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += (uint64_t)ref_dns_wire_records_end(rrpkt, p, rr_start, 4, 4096);
        double ref_re = (now_ns() - t0) / N;

        /* 6. to_host */
        dw_u8 wname[32];
        dw_i64 wlen = dns_wire_write_name(wname, sizeof(wname), host);
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += dns_wire_to_host(wname, (dw_u64)wlen, out, sizeof(out));
        double asm_th = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) s += ref_dns_wire_to_host(wname, (dw_u64)wlen, out, sizeof(out));
        double ref_th = (now_ns() - t0) / N;

        g_sink = s;
        printf("  write_name     : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_wn, ref_wn, ref_wn / asm_wn);
        printf("  copy_name      : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_cn, ref_cn, ref_cn / asm_cn);
        printf("  skip_name      : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_sn, ref_sn, ref_sn / asm_sn);
        printf("  reply_identity : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_ri, ref_ri, ref_ri / asm_ri);
        printf("  records_end    : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_re, ref_re, ref_re / asm_re);
        printf("  to_host        : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_th, ref_th, ref_th / asm_th);
}

int main(void) {
        test_write_name_edges();
        test_copy_skip_security();
        test_reply_identity_edges();
        test_records_end_edges();
        test_to_host_edges();
        test_guard_pages();
        test_fuzz();
        printf("dns_wire: %d checks, %d failures\n", g_checks, g_fails);
        if (g_fails == 0) bench_all();
        return g_fails ? 1 : 0;
}
