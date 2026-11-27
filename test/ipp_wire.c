/*
        Exhaustive RFC 8010 IPP wire encoder/decoder & URI parser security,
        edge-case, guard-page, differential-fuzz, and speed/size proof for
        src/net/ipp_wire.asm.c (arm64, x86-64, riscv64).

        Build & run:
          cc -O2 -Wall -Werror test/ipp_wire.c -o /tmp/t_ipp && /tmp/t_ipp
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/ipp_wire.c -o /tmp/t_ipp_asan && /tmp/t_ipp_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define NET_IPP_WIRE_ASM_IMPLEMENTATION
#include "../src/net/ipp_wire.asm.c"

/* ── -O2 C Reference Implementations (exact parity with src/net/ipp.c) ───── */

static inline iw_u16 ref_be16(const iw_u8 *p)
{
        return (iw_u16)(((iw_u16)p[0] << 8) | p[1]);
}

static inline iw_u32 ref_be32(const iw_u8 *p)
{
        return ((iw_u32)p[0] << 24) | ((iw_u32)p[1] << 16) |
               ((iw_u32)p[2] << 8) | (iw_u32)p[3];
}

static inline void ref_put16(iw_u8 *p, iw_u16 v)
{
        p[0] = (iw_u8)(v >> 8);
        p[1] = (iw_u8)v;
}

static inline void ref_put32(iw_u8 *p, iw_u32 v)
{
        p[0] = (iw_u8)(v >> 24);
        p[1] = (iw_u8)(v >> 16);
        p[2] = (iw_u8)(v >> 8);
        p[3] = (iw_u8)v;
}

static __attribute__((noinline)) void ref_ipp_wire_write_begin(
        struct ipp_wire_writer *w, iw_u8 *buf, iw_u64 room, iw_u64 header)
{
        if (!w) return;
        w->bytes = buf;
        w->room = room;
        w->used = 0;
        w->failed = 0;
        if (!buf || room < 9) {
                w->failed = 1;
                return;
        }
        ref_put32(buf, (iw_u32)(header >> 32));
        ref_put32(buf + 4, (iw_u32)header);
        w->used = 8;
}

static __attribute__((noinline)) void ref_ipp_wire_write_group(
        struct ipp_wire_writer *w, iw_u32 group_tag)
{
        if (!w) return;
        if (group_tag < 0x01 || group_tag > 0x0f || group_tag == IW_TAG_END ||
            w->failed || !w->bytes || w->used >= w->room) {
                w->failed = 1;
                return;
        }
        w->bytes[w->used++] = (iw_u8)group_tag;
}

static __attribute__((noinline)) void ref_ipp_wire_write_attr(
        struct ipp_wire_writer *w, iw_u32 tag,
        const char *name, const iw_u8 *val, iw_u64 val_len)
{
        if (!w) return;
        if (w->failed || !w->bytes || tag < 0x10 || tag > 0xff || val_len > 0x7fff ||
            (val_len && !val)) {
                w->failed = 1;
                return;
        }
        iw_u64 name_len = 0;
        if (name) {
                while (name[name_len]) {
                        name_len++;
                        if (name_len > 0x7fff) {
                                w->failed = 1;
                                return;
                        }
                }
        }
        iw_u64 need = 5 + name_len + val_len;
        if (w->used > w->room || need > w->room - w->used) {
                w->failed = 1;
                return;
        }
        iw_u8 *p = w->bytes + w->used;
        *p++ = (iw_u8)tag;
        ref_put16(p, (iw_u16)name_len);
        p += 2;
        if (name_len) {
                memcpy(p, name, (size_t)name_len);
                p += name_len;
        }
        ref_put16(p, (iw_u16)val_len);
        p += 2;
        if (val_len)
                memcpy(p, val, (size_t)val_len);
        w->used += need;
}

static __attribute__((noinline)) iw_u64 ref_ipp_wire_write_end(
        struct ipp_wire_writer *w)
{
        if (!w) return 0;
        if (w->failed || !w->bytes || w->used >= w->room) {
                w->failed = 1;
                return 0;
        }
        w->bytes[w->used++] = IW_TAG_END;
        return w->used;
}

static __attribute__((noinline)) iw_i32 ref_ipp_wire_read_open(
        struct ipp_wire_reader *r, const iw_u8 *bytes, iw_u64 length)
{
        if (!r) return 0;
        memset(r, 0, sizeof(*r));
        if (!bytes || length < 9) {
                r->failed = 1;
                return 0;
        }
        r->bytes = bytes;
        r->length = length;
        r->ver_major = bytes[0];
        r->ver_minor = bytes[1];
        if (r->ver_major < 1 || r->ver_major > 2) {
                r->failed = 1;
                return 0;
        }
        r->op_or_status = ref_be16(bytes + 2);
        r->request_id = ref_be32(bytes + 4);
        r->at = 8;
        return 1;
}

static __attribute__((noinline)) iw_i32 ref_ipp_wire_read_next(
        struct ipp_wire_reader *r, struct ipp_wire_attr *out)
{
        if (!out) return 0;
        memset(out, 0, sizeof(*out));
        if (!r || r->failed || r->ended)
                return 0;
        if (!r->bytes) {
                r->failed = 1;
                return 0;
        }
        for (;;) {
                if (r->at >= r->length) {
                        r->failed = 1;
                        return 0;
                }
                iw_u8 tag = r->bytes[r->at++];
                if (tag == IW_TAG_END) {
                        r->ended = 1;
                        r->payload_offset = r->at;
                        return 0;
                }
                if (tag < 0x10) {
                        if (tag == 0x00) {
                                r->failed = 1;
                                return 0;
                        }
                        r->group = tag;
                        r->last_name = NULL;
                        r->last_name_len = 0;
                        continue;
                }
                if (!r->group || r->attr_count >= IW_MAX_ATTRIBUTES ||
                    r->length - r->at < 2) {
                        r->failed = 1;
                        return 0;
                }
                iw_u16 name_len = ref_be16(r->bytes + r->at);
                r->at += 2;
                if (name_len > r->length - r->at) {
                        r->failed = 1;
                        return 0;
                }
                if (name_len) {
                        out->name = r->bytes + r->at;
                        out->name_len = name_len;
                        out->is_additional = 0;
                        r->last_name = out->name;
                        r->last_name_len = name_len;
                        r->at += name_len;
                } else {
                        if (tag != IW_TAG_END_COLL && tag != IW_TAG_MEMBERNAME &&
                            !r->last_name) {
                                r->failed = 1;
                                return 0;
                        }
                        out->name = r->last_name;
                        out->name_len = r->last_name_len;
                        out->is_additional = 1;
                }
                if (r->length - r->at < 2) {
                        r->failed = 1;
                        return 0;
                }
                iw_u16 val_len = ref_be16(r->bytes + r->at);
                r->at += 2;
                if (val_len > r->length - r->at) {
                        r->failed = 1;
                        return 0;
                }
                out->value = r->bytes + r->at;
                out->value_len = val_len;
                r->at += val_len;

                if ((tag == IW_TAG_INTEGER || tag == IW_TAG_ENUM) && val_len != 4) {
                        r->failed = 1;
                        return 0;
                }
                if (tag == IW_TAG_BOOLEAN && (val_len != 1 || out->value[0] > 1)) {
                        r->failed = 1;
                        return 0;
                }
                if (tag == IW_TAG_RANGE && val_len != 8) {
                        r->failed = 1;
                        return 0;
                }
                if (tag == IW_TAG_RESOLUTION && val_len != 9) {
                        r->failed = 1;
                        return 0;
                }
                if (tag == IW_TAG_DATETIME && val_len != 11) {
                        r->failed = 1;
                        return 0;
                }
                out->group = r->group;
                out->tag = tag;
                r->attr_count++;
                return 1;
        }
}

static __attribute__((noinline)) iw_i32 ref_ipp_wire_attr_i32(
        const struct ipp_wire_attr *a, iw_i32 *out)
{
        if (!a || !out) return 0;
        if ((a->tag != IW_TAG_INTEGER && a->tag != IW_TAG_ENUM) ||
            a->value_len != 4 || !a->value)
                return 0;
        *out = (iw_i32)ref_be32(a->value);
        return 1;
}

static __attribute__((noinline)) iw_i32 ref_ipp_wire_attr_bool(
        const struct ipp_wire_attr *a, iw_u8 *out)
{
        if (!a || !out) return 0;
        if (a->tag != IW_TAG_BOOLEAN || a->value_len != 1 || !a->value)
                return 0;
        *out = a->value[0] != 0 ? 1 : 0;
        return 1;
}

static int ref_ascii_ieq(const char *p, const char *lit, size_t n)
{
        for (size_t i = 0; i < n; i++) {
                iw_u8 a = (iw_u8)p[i];
                iw_u8 b = (iw_u8)lit[i];
                if (!a) return 0;
                if (a >= 'A' && a <= 'Z') a = (iw_u8)(a | 32);
                if (a != b) return 0;
        }
        return 1;
}

static int ref_host_byte(iw_u8 c)
{
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

static __attribute__((noinline)) iw_i32 ref_ipp_wire_parse_uri(
        const char *text, struct ipp_wire_uri *out)
{
        if (!out) return 0;
        memset(out, 0, sizeof(*out));
        if (!text || !text[0]) return 0;

        const char *p = text;
        iw_u16 default_port = IW_PORT_DEFAULT;

        if (ref_ascii_ieq(p, "ipps://", 7)) {
                out->tls = 1;
                default_port = IW_PORT_DEFAULT;
                p += 7;
        } else if (ref_ascii_ieq(p, "ipp://", 6)) {
                out->tls = 0;
                default_port = IW_PORT_DEFAULT;
                p += 6;
        } else if (ref_ascii_ieq(p, "https://", 8)) {
                out->tls = 1;
                default_port = 443;
                p += 8;
        } else if (ref_ascii_ieq(p, "http://", 7)) {
                out->tls = 0;
                default_port = 80;
                p += 7;
        } else if (strstr(p, "://")) {
                return 0;
        }

        size_t host_len = 0;
        while (p[host_len] && p[host_len] != ':' && p[host_len] != '/') {
                iw_u8 c = (iw_u8)p[host_len];
                if (!ref_host_byte(c) || host_len >= sizeof(out->host) - 1)
                        return 0;
                out->host[host_len++] = c;
        }
        if (!host_len) return 0;
        out->host[host_len] = 0;
        p += host_len;

        if (*p == ':') {
                iw_u64 port_val = 0, digits = 0;
                p++;
                while (*p >= '0' && *p <= '9') {
                        port_val = port_val * 10 + (iw_u64)(*p - '0');
                        if (++digits > 5 || port_val > 65535)
                                return 0;
                        p++;
                }
                if (!digits || !port_val) return 0;
                out->port = (iw_u16)port_val;
        } else {
                out->port = default_port;
        }

        if (!*p) {
                memcpy(out->path, "/ipp/print", 11);
                return 1;
        }
        if (*p != '/') return 0;
        size_t path_len = 0;
        while (p[path_len]) {
                iw_u8 c = (iw_u8)p[path_len];
                if (c <= 0x20 || c == '\\' || c >= 0x7f ||
                    path_len >= sizeof(out->path) - 1)
                        return 0;
                out->path[path_len++] = c;
        }
        out->path[path_len] = 0;
        return 1;
}

/* ── Test Harness ────────────────────────────────────────────────────────── */

static int g_fails = 0;

#define CHECK(cond, msg, ...) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL [%s:%d]: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
                g_fails++; \
        } \
} while (0)

static iw_u64 g_rng = 0x9e3779b97f4a7c15ULL;
static iw_u64 xorshift64(void)
{
        iw_u64 x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        g_rng = x;
        return x;
}

static inline iw_u64 make_hdr(iw_u8 maj, iw_u8 min, iw_u16 op, iw_u32 req)
{
        return ((iw_u64)maj << 56) | ((iw_u64)min << 48) | ((iw_u64)op << 32) | (iw_u64)req;
}

static void test_encoder_edge_cases(void)
{
        iw_u8 buf[256];
        struct ipp_wire_writer w;

        /* NULL writer & NULL buffer & short room */
        ipp_wire_write_begin(NULL, buf, sizeof(buf), make_hdr(2, 0, 0x000b, 1));
        ipp_wire_write_group(NULL, 0x01);
        ipp_wire_write_attr(NULL, 0x47, "a", (const iw_u8 *)"b", 1);
        CHECK(ipp_wire_write_end(NULL) == 0, "write_end(NULL)");

        ipp_wire_write_begin(&w, NULL, 64, make_hdr(2, 0, 0x000b, 1));
        CHECK(w.failed == 1 && w.used == 0, "NULL buf");
        CHECK(ipp_wire_write_end(&w) == 0 && w.failed == 1, "end after NULL buf");

        for (iw_u64 r = 0; r < 9; r++) {
                ipp_wire_write_begin(&w, buf, r, make_hdr(2, 0, 0x000b, 1));
                CHECK(w.failed == 1 && w.used == 0, "room=%llu must fail", (unsigned long long)r);
        }

        /* Exact room=9: 8-byte header + 1-byte END tag */
        memset(buf, 0xcc, sizeof(buf));
        ipp_wire_write_begin(&w, buf, 9, make_hdr(2, 0, 0x000b, 0x12345678));
        CHECK(w.failed == 0 && w.used == 8, "room=9 begin");
        iw_u64 n = ipp_wire_write_end(&w);
        CHECK(n == 9 && w.failed == 0, "room=9 end");
        CHECK(buf[0] == 2 && buf[1] == 0 && buf[2] == 0x00 && buf[3] == 0x0b &&
              buf[4] == 0x12 && buf[5] == 0x34 && buf[6] == 0x56 && buf[7] == 0x78 &&
              buf[8] == 0x03 && buf[9] == 0xcc, "room=9 bytes & no over-write");

        /* Invalid group tags */
        iw_u32 bad_groups[] = {0x00, 0x03, 0x10, 0x21, 0xff, 0x100, 0xffffffffu};
        for (size_t i = 0; i < sizeof(bad_groups) / sizeof(bad_groups[0]); i++) {
                ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(1, 1, 2, 1));
                ipp_wire_write_group(&w, bad_groups[i]);
                CHECK(w.failed == 1, "bad group 0x%x", bad_groups[i]);
                CHECK(ipp_wire_write_end(&w) == 0, "end after bad group");
        }

        /* Valid group tags */
        for (iw_u32 g = 0x01; g <= 0x0f; g++) {
                if (g == 0x03) continue;
                ipp_wire_write_begin(&w, buf, 16, make_hdr(1, 1, 2, 1));
                ipp_wire_write_group(&w, g);
                CHECK(w.failed == 0 && w.used == 9 && buf[8] == g, "valid group 0x%x", g);
        }

        /* Invalid attr tags, NULL val with nonzero len, oversized lengths */
        iw_u32 bad_tags[] = {0x00, 0x01, 0x03, 0x0f, 0x100, 0xffffffffu};
        for (size_t i = 0; i < sizeof(bad_tags) / sizeof(bad_tags[0]); i++) {
                ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 2, 1));
                ipp_wire_write_attr(&w, bad_tags[i], "k", (const iw_u8 *)"v", 1);
                CHECK(w.failed == 1, "bad attr tag 0x%x", bad_tags[i]);
        }

        ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 2, 1));
        ipp_wire_write_attr(&w, 0x47, "k", NULL, 1);
        CHECK(w.failed == 1, "NULL val with len=1");

        ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 2, 1));
        ipp_wire_write_attr(&w, 0x47, NULL, NULL, 0);
        CHECK(w.failed == 0 && w.used == 13, "NULL name + NULL val with len=0");

        ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 2, 1));
        ipp_wire_write_attr(&w, 0x47, "k", (const iw_u8 *)"v", 0x8000);
        CHECK(w.failed == 1, "val_len=0x8000");

        ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 2, 1));
        ipp_wire_write_attr(&w, 0x47, "k", (const iw_u8 *)"v", UINT64_MAX);
        CHECK(w.failed == 1, "val_len=UINT64_MAX");

        /* Oversized name (> 0x7fff) */
        char *big_name = (char *)malloc(0x8002);
        memset(big_name, 'a', 0x8000);
        big_name[0x8000] = 0;
        iw_u8 *big_buf = (iw_u8 *)malloc(0x9000);
        ipp_wire_write_begin(&w, big_buf, 0x9000, make_hdr(2, 0, 2, 1));
        ipp_wire_write_attr(&w, 0x47, big_name, (const iw_u8 *)"x", 1);
        CHECK(w.failed == 1, "name_len=0x8000 must fail");

        big_name[0x7fff] = 0;
        ipp_wire_write_begin(&w, big_buf, 0x9000, make_hdr(2, 0, 2, 1));
        ipp_wire_write_attr(&w, 0x47, big_name, (const iw_u8 *)"x", 1);
        CHECK(w.failed == 0 && w.used == 8 + 5 + 0x7fff + 1, "name_len=0x7fff must succeed");
        free(big_name);
        free(big_buf);
}

static void test_decoder_edge_cases(void)
{
        struct ipp_wire_writer w;
        struct ipp_wire_reader r, rr;
        struct ipp_wire_attr a, ra;
        iw_u8 wire[512];

        /* Build a rich multi-group RFC 8010 message with all fixed-size tags + 1setOf + collection + payload */
        ipp_wire_write_begin(&w, wire, sizeof(wire), make_hdr(2, 0, 0x0000, 42));
        ipp_wire_write_group(&w, 0x01);
        ipp_wire_write_attr(&w, 0x47, "attributes-charset", (const iw_u8 *)"utf-8", 5);
        ipp_wire_write_attr(&w, 0x48, "attributes-natural-language", (const iw_u8 *)"en", 2);
        ipp_wire_write_group(&w, 0x04);
        iw_u8 v_int[4] = {0xff, 0xff, 0xff, 0xf6}; /* -10 */
        iw_u8 v_enum[4] = {0x00, 0x00, 0x00, 0x03};
        iw_u8 v_bool1[1] = {1};
        iw_u8 v_bool0[1] = {0};
        iw_u8 v_range[8] = {0, 0, 0, 1, 0, 0, 0, 99};
        iw_u8 v_res[9] = {0, 0, 2, 88, 0, 0, 2, 88, 3};
        iw_u8 v_dt[11] = {0x07, 0xea, 11, 27, 12, 0, 0, 0, '+', 0, 0};
        ipp_wire_write_attr(&w, 0x21, "copies", v_int, 4);
        ipp_wire_write_attr(&w, 0x23, "printer-state", v_enum, 4);
        ipp_wire_write_attr(&w, 0x22, "color-supported", v_bool1, 1);
        ipp_wire_write_attr(&w, 0x22, NULL, v_bool0, 1); /* 1setOf additional */
        ipp_wire_write_attr(&w, 0x33, "copies-supported", v_range, 8);
        ipp_wire_write_attr(&w, 0x32, "printer-resolution", v_res, 9);
        ipp_wire_write_attr(&w, 0x31, "printer-current-time", v_dt, 11);
        ipp_wire_write_group(&w, 0x02);
        ipp_wire_write_attr(&w, 0x4a, NULL, (const iw_u8 *)"media-type", 10); /* membername without last_name */
        ipp_wire_write_attr(&w, 0x37, NULL, NULL, 0); /* endCollection without last_name */
        iw_u64 msg_len = ipp_wire_write_end(&w);
        CHECK(msg_len > 0 && w.failed == 0, "rich message encode");
        memcpy(wire + msg_len, "%PDF-1.7", 8);
        iw_u64 total_len = msg_len + 8;

        /* Full decode & parity with C ref */
        CHECK(ipp_wire_read_open(&r, wire, total_len) == 1, "read_open");
        CHECK(ref_ipp_wire_read_open(&rr, wire, total_len) == 1, "ref read_open");
        CHECK(memcmp(&r, &rr, sizeof(r)) == 0, "read_open state parity");

        int count = 0;
        for (;;) {
                iw_i32 ok1 = ipp_wire_read_next(&r, &a);
                iw_i32 ok2 = ref_ipp_wire_read_next(&rr, &ra);
                CHECK(ok1 == ok2, "read_next ret parity at %d", count);
                CHECK(memcmp(&a, &ra, sizeof(a)) == 0, "attr parity at %d", count);
                CHECK(memcmp(&r, &rr, sizeof(r)) == 0, "reader parity at %d", count);
                if (!ok1) break;
                iw_i32 i1 = 0, i2 = 0;
                iw_u8 b1 = 0, b2 = 0;
                CHECK(ipp_wire_attr_i32(&a, &i1) == ref_ipp_wire_attr_i32(&ra, &i2) && i1 == i2,
                      "attr_i32 parity at %d", count);
                CHECK(ipp_wire_attr_bool(&a, &b1) == ref_ipp_wire_attr_bool(&ra, &b2) && b1 == b2,
                      "attr_bool parity at %d", count);
                if (count == 2) CHECK(i1 == -10, "decoded copies=-10");
                if (count == 4) CHECK(b1 == 1, "decoded color=1");
                if (count == 5) CHECK(b1 == 0 && a.is_additional == 1, "1setOf additional");
                count++;
        }
        CHECK(count == 11 && r.ended == 1 && r.failed == 0 && r.payload_offset == msg_len,
              "decoded all 11 attrs + payload_offset=%llu", (unsigned long long)r.payload_offset);

        /* Truncation at every single byte offset 0..msg_len-1 must fail cleanly */
        for (iw_u64 trunc = 0; trunc < msg_len; trunc++) {
                iw_i32 o1 = ipp_wire_read_open(&r, wire, trunc);
                iw_i32 o2 = ref_ipp_wire_read_open(&rr, wire, trunc);
                CHECK(o1 == o2 && memcmp(&r, &rr, sizeof(r)) == 0, "trunc open %llu", (unsigned long long)trunc);
                if (o1) {
                        while (ipp_wire_read_next(&r, &a)) {}
                        while (ref_ipp_wire_read_next(&rr, &ra)) {}
                        CHECK(r.failed == 1 && r.ended == 0, "trunc %llu must fail", (unsigned long long)trunc);
                        CHECK(memcmp(&r, &rr, sizeof(r)) == 0, "trunc state parity %llu", (unsigned long long)trunc);
                }
        }

        /* Bad versions */
        iw_u8 bad_ver[10] = {0, 1, 0, 0, 0, 0, 0, 1, 0x03, 0};
        CHECK(ipp_wire_read_open(&r, bad_ver, 9) == 0 && r.failed == 1, "ver 0.1");
        bad_ver[0] = 3;
        CHECK(ipp_wire_read_open(&r, bad_ver, 9) == 0 && r.failed == 1, "ver 3.1");
        CHECK(ipp_wire_read_open(NULL, bad_ver, 9) == 0, "NULL reader");
        CHECK(ipp_wire_read_open(&r, NULL, 20) == 0 && r.failed == 1, "NULL bytes");

        /* Malformed packets: 0xffff name_len, 0xffff val_len, group 0x00, attr before group, bad fixed lens */
        const struct { const char *desc; iw_u8 pkt[32]; iw_u64 len; } bad_pkts[] = {
                {"group 0x00", {2,0,0,0,0,0,0,1, 0x00, 0x03}, 10},
                {"attr before group", {2,0,0,0,0,0,0,1, 0x47, 0,1,'a', 0,1,'b', 0x03}, 15},
                {"0xffff name_len", {2,0,0,0,0,0,0,1, 0x01, 0x47, 0xff,0xff, 'a','b'}, 14},
                {"0xffff val_len", {2,0,0,0,0,0,0,1, 0x01, 0x47, 0,1,'a', 0xff,0xff, 'b'}, 16},
                {"1setOf no last_name", {2,0,0,0,0,0,0,1, 0x01, 0x47, 0,0, 0,1,'a', 0x03}, 16},
                {"int len=3", {2,0,0,0,0,0,0,1, 0x01, 0x21, 0,1,'i', 0,3, 1,2,3, 0x03}, 19},
                {"enum len=5", {2,0,0,0,0,0,0,1, 0x01, 0x23, 0,1,'e', 0,5, 1,2,3,4,5, 0x03}, 21},
                {"bool len=2", {2,0,0,0,0,0,0,1, 0x01, 0x22, 0,1,'b', 0,2, 0,1, 0x03}, 18},
                {"bool val=2", {2,0,0,0,0,0,0,1, 0x01, 0x22, 0,1,'b', 0,1, 2, 0x03}, 17},
                {"range len=7", {2,0,0,0,0,0,0,1, 0x01, 0x33, 0,1,'r', 0,7, 0,0,0,0,0,0,0, 0x03}, 23},
                {"res len=8", {2,0,0,0,0,0,0,1, 0x01, 0x32, 0,1,'r', 0,8, 0,0,0,0,0,0,0,0, 0x03}, 24},
                {"datetime len=10", {2,0,0,0,0,0,0,1, 0x01, 0x31, 0,1,'d', 0,10, 0,0,0,0,0,0,0,0,0,0, 0x03}, 26},
        };
        for (size_t i = 0; i < sizeof(bad_pkts) / sizeof(bad_pkts[0]); i++) {
                CHECK(ipp_wire_read_open(&r, bad_pkts[i].pkt, bad_pkts[i].len) == 1, "%s open", bad_pkts[i].desc);
                CHECK(ref_ipp_wire_read_open(&rr, bad_pkts[i].pkt, bad_pkts[i].len) == 1, "%s ref open", bad_pkts[i].desc);
                iw_i32 n1 = ipp_wire_read_next(&r, &a);
                iw_i32 n2 = ref_ipp_wire_read_next(&rr, &ra);
                CHECK(n1 == 0 && n2 == 0 && r.failed == 1 && rr.failed == 1, "%s must fail", bad_pkts[i].desc);
                CHECK(memcmp(&r, &rr, sizeof(r)) == 0, "%s state parity", bad_pkts[i].desc);
        }

        /* 4096 attribute limit: 4096 ok, 4097th fails */
        iw_u64 cap_size = 9 + 4097 * 7 + 1;
        iw_u8 *cap_buf = (iw_u8 *)malloc(cap_size);
        ipp_wire_write_begin(&w, cap_buf, cap_size, make_hdr(2, 0, 0, 1));
        ipp_wire_write_group(&w, 0x01);
        for (int i = 0; i < 4097; i++)
                ipp_wire_write_attr(&w, 0x44, "k", (const iw_u8 *)"v", 1);
        iw_u64 cap_len = ipp_wire_write_end(&w);
        CHECK(cap_len > 0, "4097 attr build");
        CHECK(ipp_wire_read_open(&r, cap_buf, cap_len) == 1, "4097 open");
        int got = 0;
        while (ipp_wire_read_next(&r, &a)) got++;
        CHECK(got == 4096 && r.failed == 1 && r.ended == 0, "4096 cap (got=%d)", got);
        free(cap_buf);
}

static void test_uri_edge_cases(void)
{
        const char *cases[] = {
                "",
                "printer.local",
                "printer.local:631",
                "printer.local:8631/ipp/print",
                "ipp://printer.local",
                "IPP://Printer-01.Local:631/ipp/print",
                "ipps://hp_laser.local/ipp/print",
                "IPPS://192.168.1.50:443/printers/main",
                "http://localhost/ipp/print",
                "HTTP://localhost:8080/",
                "https://secure.example.com",
                "HTTPS://secure.example.com:8443/a/b?c=1",
                "ftp://printer.local/ipp/print",
                "javascript://alert(1)",
                "printer.local/a://b",
                "ipp://[::1]/ipp/print",
                "http://[fe80::1]:631/ipp/print",
                "ipp:///ipp/print",
                "ipp://:631/ipp/print",
                "ipp://host:0/ipp/print",
                "ipp://host:65535/ipp/print",
                "ipp://host:65536/ipp/print",
                "ipp://host:00001/ipp/print",
                "ipp://host:000001/ipp/print",
                "ipp://host:/ipp/print",
                "ipp://host:631abc/ipp/print",
                "ipp://host:631foo",
                "ipp://bad host/ipp/print",
                "ipp://bad\r\nhost/ipp/print",
                "ipp://host/bad path",
                "ipp://host/bad\\path",
                "ipp://host/bad\r\nHeader:x",
                "ipp://host/bad\x7f",
                "ipp://host/bad\x80",
                "ipp://host/bad\x01",
                "ip://short-scheme",
                "htt://short-scheme",
                "ipps:",
                "ipp:/",
                "https:/",
                "h",
                "i",
        };
        struct ipp_wire_uri u1, u2;
        CHECK(ipp_wire_parse_uri(NULL, &u1) == 0 && ref_ipp_wire_parse_uri(NULL, &u2) == 0, "NULL text");
        CHECK(ipp_wire_parse_uri("ipp://a", NULL) == 0 && ref_ipp_wire_parse_uri("ipp://a", NULL) == 0, "NULL out");

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                iw_i32 r1 = ipp_wire_parse_uri(cases[i], &u1);
                iw_i32 r2 = ref_ipp_wire_parse_uri(cases[i], &u2);
                CHECK(r1 == r2, "uri '%s': asm=%d ref=%d", cases[i], r1, r2);
                if (r1 == 1) {
                        CHECK(memcmp(&u1, &u2, sizeof(u1)) == 0, "uri '%s' struct mismatch", cases[i]);
                }
        }

        /* Boundary host lengths 127 (ok) and 128 (fail) */
        char long_uri[300];
        memset(long_uri, 'a', 127);
        long_uri[127] = 0;
        CHECK(ipp_wire_parse_uri(long_uri, &u1) == 1 && ref_ipp_wire_parse_uri(long_uri, &u2) == 1 &&
              memcmp(&u1, &u2, sizeof(u1)) == 0, "host len 127");
        long_uri[127] = 'b';
        long_uri[128] = 0;
        CHECK(ipp_wire_parse_uri(long_uri, &u1) == 0 && ref_ipp_wire_parse_uri(long_uri, &u2) == 0, "host len 128");

        /* Boundary path lengths 159 (ok) and 160 (fail) */
        long_uri[0] = 'h';
        long_uri[1] = '/';
        memset(long_uri + 2, 'p', 158);
        long_uri[160] = 0;
        CHECK(ipp_wire_parse_uri(long_uri, &u1) == 1 && ref_ipp_wire_parse_uri(long_uri, &u2) == 1 &&
              memcmp(&u1, &u2, sizeof(u1)) == 0, "path len 159");
        long_uri[160] = 'q';
        long_uri[161] = 0;
        CHECK(ipp_wire_parse_uri(long_uri, &u1) == 0 && ref_ipp_wire_parse_uri(long_uri, &u2) == 0, "path len 160");
}

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        /* 3 pages: [PROT_NONE] [RW] [PROT_NONE] */
        iw_u8 *mem = (iw_u8 *)mmap(NULL, 3 * page, PROT_NONE,
                                   MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(mem != MAP_FAILED, "mmap 3 pages");
        if (mem == MAP_FAILED) return;
        CHECK(mprotect(mem + page, page, PROT_READ | PROT_WRITE) == 0, "mprotect RW");

        iw_u8 *rw_start = mem + page;
        iw_u8 *rw_end   = mem + 2 * page;

        /* 1. Writer flush at end of RW page (exact fit, then 1 byte short) */
        struct ipp_wire_writer w;
        const char *k = "attributes-charset";
        const iw_u8 *v = (const iw_u8 *)"utf-8";
        iw_u64 exact = 8 + 1 + (5 + strlen(k) + 5) + 1;

        iw_u8 *out_end = rw_end - exact;
        ipp_wire_write_begin(&w, out_end, exact, make_hdr(2, 0, 0x000b, 7));
        ipp_wire_write_group(&w, 0x01);
        ipp_wire_write_attr(&w, 0x47, k, v, 5);
        CHECK(ipp_wire_write_end(&w) == exact && w.failed == 0, "guard write flush-end exact");

        /* 2. Reader flush at start AND flush at end of RW page */
        struct ipp_wire_reader r;
        struct ipp_wire_attr a;
        CHECK(ipp_wire_read_open(&r, out_end, exact) == 1, "guard read flush-end open");
        CHECK(ipp_wire_read_next(&r, &a) == 1 && a.value_len == 5, "guard read flush-end attr");
        CHECK(ipp_wire_read_next(&r, &a) == 0 && r.ended == 1 && r.failed == 0, "guard read flush-end end");

        memcpy(rw_start, out_end, (size_t)exact);
        CHECK(ipp_wire_read_open(&r, rw_start, exact) == 1, "guard read flush-start open");
        CHECK(ipp_wire_read_next(&r, &a) == 1 && a.value_len == 5, "guard read flush-start attr");
        CHECK(ipp_wire_read_next(&r, &a) == 0 && r.ended == 1 && r.failed == 0, "guard read flush-start end");

        /* 1-byte-short writer flush at end of RW page (must never touch PROT_NONE) */
        iw_u8 *out_short = rw_end - (exact - 1);
        ipp_wire_write_begin(&w, out_short, exact - 1, make_hdr(2, 0, 0x000b, 7));
        ipp_wire_write_group(&w, 0x01);
        ipp_wire_write_attr(&w, 0x47, k, v, 5);
        CHECK(ipp_wire_write_end(&w) == 0 && w.failed == 1, "guard write flush-end short");

        /* Truncated packet flush at end of RW page (must never overread into PROT_NONE!) */
        for (iw_u64 t = 1; t < exact; t++) {
                iw_u8 *p = rw_end - t;
                memmove(p, rw_start, (size_t)t);
                if (ipp_wire_read_open(&r, p, t)) {
                        while (ipp_wire_read_next(&r, &a)) {}
                        CHECK(r.failed == 1, "guard trunc %llu fail", (unsigned long long)t);
                }
        }

        /* 3. URI strings flush at end of RW page (short & full, including partial schemes) */
        const char *guard_uris[] = {
                "i", "ip", "ipp", "ipp:", "ipp:/", "ipps", "ipps:",
                "h", "ht", "htt", "http", "http:", "https", "https:",
                "a:", "a:/", "ipp://h:631/ipp/print", "https://printer.local",
                "printer.local", "ftp://x", "ipp://[::1]/p"
        };
        struct ipp_wire_uri u1, u2;
        for (size_t i = 0; i < sizeof(guard_uris) / sizeof(guard_uris[0]); i++) {
                size_t len = strlen(guard_uris[i]) + 1;
                char *p_end = (char *)(rw_end - len);
                memcpy(p_end, guard_uris[i], len);
                CHECK(ipp_wire_parse_uri(p_end, &u1) == ref_ipp_wire_parse_uri(p_end, &u2),
                      "guard uri flush-end '%s'", guard_uris[i]);

                char *p_start = (char *)rw_start;
                memcpy(p_start, guard_uris[i], len);
                CHECK(ipp_wire_parse_uri(p_start, &u1) == ref_ipp_wire_parse_uri(p_start, &u2),
                      "guard uri flush-start '%s'", guard_uris[i]);
        }

        /* 4. Output struct ipp_wire_uri placed flush at end of RW page */
        struct ipp_wire_uri *u_end = (struct ipp_wire_uri *)(rw_end - sizeof(struct ipp_wire_uri));
        CHECK(ipp_wire_parse_uri("ipps://printer.local:631/ipp/print", u_end) == 1, "guard uri out flush-end");
        CHECK(u_end->tls == 1 && u_end->port == 631 && strcmp((char *)u_end->host, "printer.local") == 0,
              "guard uri out contents");

        munmap(mem, 3 * page);
}

static void test_differential_fuzz(void)
{
        /* 1. Fuzz encoder (4000 trials) */
        iw_u8 b1[512], b2[512];
        for (int t = 0; t < 4000; t++) {
                struct ipp_wire_writer w1, w2;
                iw_u64 room = xorshift64() % 260;
                iw_u64 hdr = xorshift64();
                memset(b1, 0xaa, sizeof(b1));
                memset(b2, 0xaa, sizeof(b2));
                ipp_wire_write_begin(&w1, b1, room, hdr);
                ref_ipp_wire_write_begin(&w2, b2, room, hdr);
                int ops = (int)(xorshift64() % 12);
                for (int k = 0; k < ops; k++) {
                        if ((xorshift64() & 3) == 0) {
                                iw_u32 g = (iw_u32)(xorshift64() % 20);
                                ipp_wire_write_group(&w1, g);
                                ref_ipp_wire_write_group(&w2, g);
                        } else {
                                iw_u32 tag = (iw_u32)(xorshift64() % 270);
                                char name[40];
                                iw_u8 val[40];
                                size_t nlen = (size_t)(xorshift64() % 32);
                                size_t vlen = (size_t)(xorshift64() % 32);
                                for (size_t i = 0; i < nlen; i++) name[i] = (char)('a' + (xorshift64() % 26));
                                name[nlen] = 0;
                                for (size_t i = 0; i < vlen; i++) val[i] = (iw_u8)xorshift64();
                                const char *np = (xorshift64() & 7) == 0 ? NULL : name;
                                const iw_u8 *vp = (xorshift64() & 15) == 0 ? NULL : val;
                                ipp_wire_write_attr(&w1, tag, np, vp, vlen);
                                ref_ipp_wire_write_attr(&w2, tag, np, vp, vlen);
                        }
                }
                iw_u64 e1 = ipp_wire_write_end(&w1);
                iw_u64 e2 = ref_ipp_wire_write_end(&w2);
                CHECK(e1 == e2 && w1.used == w2.used && w1.failed == w2.failed,
                      "fuzz encode trial %d", t);
                if (e1 > 0)
                        CHECK(memcmp(b1, b2, (size_t)e1) == 0, "fuzz encode bytes %d", t);
        }

        /* 2. Fuzz decoder on structured + mutated + pure random packets (10000 trials) */
        iw_u8 pkt[256];
        for (int t = 0; t < 10000; t++) {
                iw_u64 len;
                if (t < 5000) {
                        /* Structured then randomly mutated */
                        struct ipp_wire_writer w;
                        ipp_wire_write_begin(&w, pkt, sizeof(pkt),
                                             make_hdr((iw_u8)(1 + (xorshift64() & 1)), 0, 0x000b, (iw_u32)t));
                        ipp_wire_write_group(&w, (iw_u32)(1 + (xorshift64() % 7)));
                        int nattrs = (int)(xorshift64() % 6);
                        for (int k = 0; k < nattrs; k++) {
                                iw_u8 tags[] = {0x21, 0x22, 0x23, 0x31, 0x32, 0x33, 0x44, 0x47, 0x4a, 0x37};
                                iw_u8 tag = tags[xorshift64() % sizeof(tags)];
                                iw_u8 v[16];
                                for (int i = 0; i < 16; i++) v[i] = (iw_u8)xorshift64();
                                v[0] &= 1;
                                iw_u64 vl = (tag == 0x21 || tag == 0x23) ? 4 :
                                            (tag == 0x22) ? 1 :
                                            (tag == 0x33) ? 8 :
                                            (tag == 0x32) ? 9 :
                                            (tag == 0x31) ? 11 : (xorshift64() % 12);
                                ipp_wire_write_attr(&w, tag, (xorshift64() & 3) ? "attr" : NULL, v, vl);
                        }
                        len = ipp_wire_write_end(&w);
                        if (len > 0 && (xorshift64() & 1)) {
                                int muts = 1 + (int)(xorshift64() % 3);
                                for (int m = 0; m < muts; m++)
                                        pkt[xorshift64() % len] = (iw_u8)xorshift64();
                        }
                        if (len > 0 && (xorshift64() & 3) == 0)
                                len = xorshift64() % len;
                } else {
                        len = xorshift64() % 128;
                        for (iw_u64 i = 0; i < len; i++) pkt[i] = (iw_u8)xorshift64();
                        if (len >= 2 && (xorshift64() & 1)) pkt[0] = (iw_u8)(1 + (xorshift64() & 1));
                }
                struct ipp_wire_reader r1, r2;
                struct ipp_wire_attr a1, a2;
                iw_i32 o1 = ipp_wire_read_open(&r1, pkt, len);
                iw_i32 o2 = ref_ipp_wire_read_open(&r2, pkt, len);
                CHECK(o1 == o2 && memcmp(&r1, &r2, sizeof(r1)) == 0, "fuzz decode open %d", t);
                if (o1) {
                        for (int step = 0; step < 64; step++) {
                                iw_i32 n1 = ipp_wire_read_next(&r1, &a1);
                                iw_i32 n2 = ref_ipp_wire_read_next(&r2, &a2);
                                CHECK(n1 == n2, "fuzz decode next %d.%d", t, step);
                                CHECK(memcmp(&a1, &a2, sizeof(a1)) == 0, "fuzz decode attr %d.%d", t, step);
                                CHECK(memcmp(&r1, &r2, sizeof(r1)) == 0, "fuzz decode reader %d.%d", t, step);
                                if (!n1) break;
                                iw_i32 i1 = 0, i2 = 0;
                                iw_u8 b1v = 0, b2v = 0;
                                CHECK(ipp_wire_attr_i32(&a1, &i1) == ref_ipp_wire_attr_i32(&a2, &i2) && i1 == i2,
                                      "fuzz i32 %d.%d", t, step);
                                CHECK(ipp_wire_attr_bool(&a1, &b1v) == ref_ipp_wire_attr_bool(&a2, &b2v) && b1v == b2v,
                                      "fuzz bool %d.%d", t, step);
                        }
                }
        }

        /* 3. Fuzz URI parser (8000 trials) */
        const char *schemes[] = {"", "ipp://", "ipps://", "http://", "https://",
                                 "IPP://", "IpPs://", "HtTp://", "HTTPS://", "ftp://", "x://"};
        char uri[256];
        for (int t = 0; t < 8000; t++) {
                size_t pos = 0;
                const char *sc = schemes[xorshift64() % (sizeof(schemes) / sizeof(schemes[0]))];
                size_t sl = strlen(sc);
                memcpy(uri, sc, sl);
                pos += sl;
                size_t hl = (size_t)(xorshift64() % 24);
                for (size_t i = 0; i < hl && pos < 200; i++) {
                        iw_u64 r = xorshift64();
                        uri[pos++] = (r & 7) ? (char)('a' + (r % 26)) : (char)(".0-_:[/@ \r\n"[(r >> 4) % 11]);
                }
                if (xorshift64() & 1) {
                        uri[pos++] = ':';
                        size_t pl = (size_t)(xorshift64() % 7);
                        for (size_t i = 0; i < pl && pos < 220; i++)
                                uri[pos++] = (char)('0' + (xorshift64() % 11));
                }
                if (xorshift64() & 1) {
                        uri[pos++] = '/';
                        size_t ptl = (size_t)(xorshift64() % 24);
                        for (size_t i = 0; i < ptl && pos < 250; i++) {
                                iw_u64 r = xorshift64();
                                uri[pos++] = (r & 7) ? (char)('a' + (r % 26)) : (char)("/._- \\?\x7f\r\n"[(r >> 4) % 10]);
                        }
                }
                uri[pos] = 0;
                struct ipp_wire_uri u1, u2;
                iw_i32 r1 = ipp_wire_parse_uri(uri, &u1);
                iw_i32 r2 = ref_ipp_wire_parse_uri(uri, &u2);
                CHECK(r1 == r2, "fuzz uri %d: '%s' asm=%d ref=%d", t, uri, r1, r2);
                if (r1 == 1)
                        CHECK(memcmp(&u1, &u2, sizeof(u1)) == 0, "fuzz uri struct %d: '%s'", t, uri);
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
        const int ITERS = 200000;
        iw_u8 buf[512];
        volatile iw_u64 sink = 0;

        /* 1. Encode benchmark (typical Get-Printer-Attributes request) */
        double t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
                struct ipp_wire_writer w;
                ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 0x000b, (iw_u32)i));
                ipp_wire_write_group(&w, 0x01);
                ipp_wire_write_attr(&w, 0x47, "attributes-charset", (const iw_u8 *)"utf-8", 5);
                ipp_wire_write_attr(&w, 0x48, "attributes-natural-language", (const iw_u8 *)"en", 2);
                ipp_wire_write_attr(&w, 0x45, "printer-uri", (const iw_u8 *)"ipp://printer.local:631/ipp/print", 33);
                ipp_wire_write_attr(&w, 0x44, "requested-attributes", (const iw_u8 *)"printer-state", 13);
                sink += ipp_wire_write_end(&w);
        }
        double asm_enc = (now_ns() - t0) / ITERS;

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
                struct ipp_wire_writer w;
                ref_ipp_wire_write_begin(&w, buf, sizeof(buf), make_hdr(2, 0, 0x000b, (iw_u32)i));
                ref_ipp_wire_write_group(&w, 0x01);
                ref_ipp_wire_write_attr(&w, 0x47, "attributes-charset", (const iw_u8 *)"utf-8", 5);
                ref_ipp_wire_write_attr(&w, 0x48, "attributes-natural-language", (const iw_u8 *)"en", 2);
                ref_ipp_wire_write_attr(&w, 0x45, "printer-uri", (const iw_u8 *)"ipp://printer.local:631/ipp/print", 33);
                ref_ipp_wire_write_attr(&w, 0x44, "requested-attributes", (const iw_u8 *)"printer-state", 13);
                sink += ref_ipp_wire_write_end(&w);
        }
        double ref_enc = (now_ns() - t0) / ITERS;

        /* 2. Decode benchmark (multi-attr IPP reply) */
        struct ipp_wire_writer bw;
        iw_u8 v_int[4] = {0, 0, 0, 3}, v_bool[1] = {1};
        ipp_wire_write_begin(&bw, buf, sizeof(buf), make_hdr(2, 0, 0x0000, 1));
        ipp_wire_write_group(&bw, 0x01);
        ipp_wire_write_attr(&bw, 0x47, "attributes-charset", (const iw_u8 *)"utf-8", 5);
        ipp_wire_write_attr(&bw, 0x48, "attributes-natural-language", (const iw_u8 *)"en", 2);
        ipp_wire_write_group(&bw, 0x04);
        ipp_wire_write_attr(&bw, 0x23, "printer-state", v_int, 4);
        ipp_wire_write_attr(&bw, 0x22, "color-supported", v_bool, 1);
        ipp_wire_write_attr(&bw, 0x49, "document-format-supported", (const iw_u8 *)"application/pdf", 15);
        ipp_wire_write_attr(&bw, 0x49, NULL, (const iw_u8 *)"image/pwg-raster", 16);
        ipp_wire_write_attr(&bw, 0x49, NULL, (const iw_u8 *)"image/urf", 9);
        iw_u64 pkt_len = ipp_wire_write_end(&bw);

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
                struct ipp_wire_reader r;
                struct ipp_wire_attr a;
                iw_i32 iv = 0;
                iw_u8 bv = 0;
                ipp_wire_read_open(&r, buf, pkt_len);
                while (ipp_wire_read_next(&r, &a)) {
                        if (ipp_wire_attr_i32(&a, &iv)) sink += (iw_u64)iv;
                        if (ipp_wire_attr_bool(&a, &bv)) sink += bv;
                        sink += a.value_len;
                }
        }
        double asm_dec = (now_ns() - t0) / ITERS;

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
                struct ipp_wire_reader r;
                struct ipp_wire_attr a;
                iw_i32 iv = 0;
                iw_u8 bv = 0;
                ref_ipp_wire_read_open(&r, buf, pkt_len);
                while (ref_ipp_wire_read_next(&r, &a)) {
                        if (ref_ipp_wire_attr_i32(&a, &iv)) sink += (iw_u64)iv;
                        if (ref_ipp_wire_attr_bool(&a, &bv)) sink += bv;
                        sink += a.value_len;
                }
        }
        double ref_dec = (now_ns() - t0) / ITERS;

        /* 3. URI parse benchmark */
        const char *u_str = "ipps://hp-laserjet-pro.local:631/ipp/print";
        struct ipp_wire_uri u;
        t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
                sink += (iw_u64)ipp_wire_parse_uri(u_str, &u);
                sink += u.port;
        }
        double asm_uri = (now_ns() - t0) / ITERS;

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
                sink += (iw_u64)ref_ipp_wire_parse_uri(u_str, &u);
                sink += u.port;
        }
        double ref_uri = (now_ns() - t0) / ITERS;

        printf("BENCH encode : ASM %6.1f ns/op  vs  C -O2 %6.1f ns/op  (%.2fx)\n",
               asm_enc, ref_enc, ref_enc / asm_enc);
        printf("BENCH decode : ASM %6.1f ns/op  vs  C -O2 %6.1f ns/op  (%.2fx)\n",
               asm_dec, ref_dec, ref_dec / asm_dec);
        printf("BENCH uri    : ASM %6.1f ns/op  vs  C -O2 %6.1f ns/op  (%.2fx)\n",
               asm_uri, ref_uri, ref_uri / asm_uri);
        (void)sink;
}

int main(void)
{
        test_encoder_edge_cases();
        test_decoder_edge_cases();
        test_uri_edge_cases();
        test_guard_pages();
        test_differential_fuzz();
        if (g_fails == 0) {
                printf("ALL TESTS PASSED (encoder + decoder + uri + guard-pages + 22k differential fuzz)\n");
                run_benchmarks();
        } else {
                printf("FAILED: %d errors\n", g_fails);
        }
        return g_fails ? 1 : 0;
}
