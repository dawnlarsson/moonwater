/*
        Differential C-reference parity, security, guard-page, fuzz &
        throughput/size benchmark for src/net/http_parse.asm.c
        (arm64, x86-64, riscv64 pure-ASM HTTP parser/validator primitives).

        Build & run:
          cc -O2 -Wall -Werror test/http_parse.c -o /tmp/t_http && /tmp/t_http
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/http_parse.c -o /tmp/t_http_asan && /tmp/t_http_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define NET_HTTP_PARSE_IMPLEMENTATION
#include "../src/net/http_parse.asm.c"

/* ── Reference C implementations (matching src/net/net.c) ──────────────────── */

static inline int ref_is_alpha(unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline int ref_is_digit(unsigned char c) {
        return c >= '0' && c <= '9';
}
static inline int ref_is_alnum(unsigned char c) {
        return ref_is_alpha(c) || ref_is_digit(c);
}
static inline int ref_is_control(unsigned char c) {
        return c < 32 || c == 127;
}
static inline int ref_is_blank(unsigned char c) {
        return c == ' ' || c == '\t';
}
static inline int ref_is_text(unsigned char c) {
        return c == '\t' || (c >= 32 && c != 127);
}
static inline unsigned ref_hex_val(unsigned char c) {
        if (c >= '0' && c <= '9') return (unsigned)(c - '0');
        if (c >= 'A' && c <= 'F') return (unsigned)(c - 'A' + 10);
        if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a' + 10);
        return 16u;
}
static inline unsigned char ref_ascii_lower(unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

__attribute__((noinline)) hp_i32 ref_http_parse_host_byte(hp_u32 byte) {
        unsigned char c = (unsigned char)byte;
        return (ref_is_alnum(c) || c == '-' || c == '.' || c == '_') ? 1 : 0;
}

__attribute__((noinline)) hp_i32 ref_http_parse_token_byte(hp_u32 byte) {
        unsigned char c = (unsigned char)byte;
        if (ref_is_alnum(c)) return 1;
        return memchr("!#$%&'*+-.^_`|~", c, 15) != NULL ? 1 : 0;
}

__attribute__((noinline)) hp_u64 ref_http_parse_scheme_length(const char *url) {
        if (!url || !ref_is_alpha((unsigned char)url[0])) return 0;
        hp_u64 len = 0;
        while (ref_is_alnum((unsigned char)url[len]) || url[len] == '+' ||
               url[len] == '-' || url[len] == '.')
                len++;
        return url[len] == ':' ? len + 1 : 0;
}

__attribute__((noinline)) hp_i64 ref_http_parse_header_end(const hp_u8 *bytes, hp_u64 size) {
        if (!bytes || size < 2) return -1;
        const hp_u8 *crlf = NULL;
        for (hp_u64 i = 0; i + 3 <= size; i++) {
                if (bytes[i] == '\n' && bytes[i + 1] == '\r' && bytes[i + 2] == '\n') {
                        crlf = bytes + i;
                        break;
                }
        }
        hp_u64 span = crlf ? (hp_u64)(crlf - bytes) + 1 : size;
        const hp_u8 *bare = NULL;
        for (hp_u64 i = 0; i + 2 <= span; i++) {
                if (bytes[i] == '\n' && bytes[i + 1] == '\n') {
                        bare = bytes + i;
                        break;
                }
        }
        if (crlf && (!bare || crlf < bare)) return (hp_i64)(crlf - bytes) + 3;
        if (bare) return (hp_i64)(bare - bytes) + 2;
        return -1;
}

__attribute__((noinline)) hp_i64 ref_http_parse_field_name(const hp_u8 *line, hp_u64 stop) {
        if (!line || !stop) return -1;
        hp_u64 name = 0;
        while (name < stop && ref_http_parse_token_byte(line[name])) name++;
        if (!name || name == stop || line[name] != ':') return -1;
        for (hp_u64 i = name + 1; i < stop; i++) {
                if (!ref_is_text(line[i])) return -1;
        }
        return (hp_i64)name;
}

__attribute__((noinline)) hp_i32 ref_http_parse_line_valid(const hp_u8 *line, hp_u64 stop, hp_u32 field, hp_u32 tight) {
        if (field) return ref_http_parse_field_name(line, stop) >= 0 ? 1 : 0;
        if (!stop) return 1;
        if (!line) return 0;
        for (hp_u64 i = 0; i < stop; i++) {
                if (!ref_is_text(line[i])) {
                        if (tight || line[i] == 0 || line[i] == '\r') return 0;
                }
        }
        return 1;
}

__attribute__((noinline)) const char *ref_http_parse_header(const hp_u8 *bytes, hp_u64 size, const char *name,
                                                            hp_u64 *length_out, hp_u8 *repeated_out) {
        if (repeated_out) *repeated_out = 0;
        if (!bytes || !name) return NULL;
        hp_u64 want = strlen(name);
        const char *found = NULL;
        hp_u64 found_len = 0;
        hp_u64 at = 0;
        while (at < size) {
                hp_u64 line = at;
                hp_u64 stop = at;
                while (stop < size && bytes[stop] != '\n') stop++;
                at = stop + (stop < size ? 1 : 0);
                if (stop > line && bytes[stop - 1] == '\r') stop--;
                if (stop - line <= want || bytes[line + want] != ':') continue;
                int eq = 1;
                for (hp_u64 k = 0; k < want; k++) {
                        if (ref_ascii_lower(bytes[line + k]) != ref_ascii_lower((unsigned char)name[k])) {
                                eq = 0;
                                break;
                        }
                }
                if (!eq) continue;
                hp_u64 from = line + want + 1;
                while (from < stop && ref_is_blank(bytes[from])) from++;
                if (found) {
                        if (repeated_out) *repeated_out = 1;
                        continue;
                }
                found = (const char *)(bytes + from);
                found_len = stop - from;
                while (found_len && ref_is_blank((unsigned char)found[found_len - 1])) found_len--;
        }
        if (found && length_out) *length_out = found_len;
        return found;
}

__attribute__((noinline)) hp_i32 ref_http_parse_chunk_extensions_valid(const char *at, const char *stop) {
        if (at >= stop) return 1;
        if (!at || !stop) return 0;
        while (at < stop) {
                if (*at++ != ';') return 0;
                const char *name = at;
                while (at < stop && ref_http_parse_token_byte((unsigned char)*at)) at++;
                if (at == name) return 0;
                if (at < stop && *at == '=') {
                        at++;
                        if (at == stop) return 0;
                        if (*at == '"') {
                                for (at++; at < stop && *at != '"'; at++) {
                                        if ((*at == '\\' && ++at == stop) ||
                                            (ref_is_control((unsigned char)*at) && *at != '\t'))
                                                return 0;
                                }
                                if (at++ == stop) return 0;
                        } else {
                                const char *val = at;
                                while (at < stop && ref_http_parse_token_byte((unsigned char)*at)) at++;
                                if (at == val) return 0;
                        }
                }
        }
        return 1;
}

__attribute__((noinline)) hp_i64 ref_http_parse_chunk_line(const hp_u8 *line, hp_u64 line_length, hp_u64 *chunk_length_out) {
        if (!line || !line_length || line[line_length - 1] != '\n') return HP_MALFORMED;
        const hp_u8 *at = line;
        const hp_u8 *stop = line + line_length - 1;
        if (stop > at && stop[-1] == '\r') stop--;
        hp_u64 parsed = 0;
        int any = 0;
        while (at < stop) {
                unsigned d = ref_hex_val(*at);
                if (d >= 16) break;
                if (parsed > (UINT64_MAX - d) / 16) return HP_MALFORMED;
                parsed = parsed * 16 + d;
                at++;
                any = 1;
        }
        if (!any) return HP_MALFORMED;
        while (at < stop && ref_is_blank(*at)) at++;
        if (at < stop && !ref_http_parse_chunk_extensions_valid((const char *)at, (const char *)stop))
                return HP_MALFORMED;
        if (chunk_length_out) *chunk_length_out = parsed;
        return HP_OK;
}

__attribute__((noinline)) hp_i64 ref_http_parse_trailer_line(const hp_u8 *line, hp_u64 line_length) {
        if (!line || !line_length || line[line_length - 1] != '\n') return HP_MALFORMED;
        hp_u64 stop = line_length - 1;
        if (stop && line[stop - 1] == '\r') stop--;
        if (!stop) return 1;
        return ref_http_parse_field_name(line, stop) >= 0 ? 0 : HP_MALFORMED;
}

__attribute__((noinline)) hp_i64 ref_http_parse_status_code(const hp_u8 *bytes, hp_u64 size, hp_i32 *code_out) {
        if (!bytes || size < 13) return HP_NO_REPLY;
        if (memcmp(bytes, "HTTP/1.", 7) != 0 || !ref_is_digit(bytes[7]) || bytes[8] != ' ' ||
            !ref_is_digit(bytes[9]) || !ref_is_digit(bytes[10]) || !ref_is_digit(bytes[11]) ||
            (bytes[12] != ' ' && bytes[12] != '\r' && bytes[12] != '\n'))
                return HP_MALFORMED;
        if (code_out) {
                *code_out = (hp_i32)((bytes[9] - '0') * 100 + (bytes[10] - '0') * 10 + (bytes[11] - '0'));
        }
        return HP_OK;
}

__attribute__((noinline)) hp_i32 ref_http_parse_request_component_valid(const char *text, hp_u32 kind) {
        if (!text) return 0;
        if (kind == HP_REQ_HOST && !text[0]) return 0;
        const char *peeled = text;
        for (; *text; text++) {
                unsigned char byte = (unsigned char)*text;
                if (ref_is_control(byte) ||
                    (kind != HP_REQ_FIELD && byte == ' ') ||
                    (kind == HP_REQ_TARGET && (byte == '\\' || byte > 0x7f)))
                        return 0;
                if (kind == HP_REQ_TARGET && byte == '%' && text >= peeled) {
                        const char *at = text;
                        int percent_form = 1;
                        for (;;) {
                                const char *pair = at;
                                if (*at == '%') pair = at + 1;
                                else if (percent_form) break;
                                unsigned high = ref_hex_val((unsigned char)pair[0]);
                                if (high >= 16) break;
                                unsigned low = ref_hex_val((unsigned char)pair[1]);
                                if (low >= 16) break;
                                unsigned char decoded = (unsigned char)((high << 4) | low);
                                if (decoded == 0 || decoded == '\r' || decoded == '\n') return 0;
                                at = pair + 2;
                                if (decoded != '%') break;
                                percent_form = 0;
                        }
                        peeled = at;
                }
                if (kind == HP_REQ_HOST && !ref_http_parse_host_byte(byte)) return 0;
        }
        return 1;
}

__attribute__((noinline)) hp_i32 ref_http_parse_ipv4_spelling(const hp_u8 *host, hp_u64 length, hp_u32 *ip_out) {
        if (!host || !length) return 0;
        hp_u64 at = 0, address = 0, parts = 0;
        for (;;) {
                if (at >= length) return 0;
                unsigned base = 10;
                hp_u64 digits = 0, value = 0;
                if (host[at] == '0' && at + 1 < length) {
                        if ((host[at + 1] | 0x20) == 'x') { base = 16; at += 2; }
                        else if (ref_is_digit(host[at + 1])) { base = 8; at += 1; }
                }
                while (at < length) {
                        unsigned d = ref_hex_val(host[at]);
                        if (d >= base) break;
                        value = value * base + d;
                        if (value > 0xffffffffu) return 0;
                        at++;
                        digits++;
                }
                if (!digits) return 0;
                parts++;
                if (at == length) {
                        unsigned shift = (unsigned)(8 * (5 - parts));
                        if (value >> shift) return 0;
                        if (ip_out) *ip_out = (hp_u32)((address << shift) | value);
                        return 1;
                }
                if (parts == 4 || value > 255 || host[at] != '.') return 0;
                at++;
                address = (address << 8) | value;
        }
}

__attribute__((noinline)) hp_u64 ref_http_parse_dot_segment(const char *segment, hp_u64 length) {
        if (!segment || !length) return 0;
        hp_u64 dots = 0;
        for (hp_u64 at = 0; at < length; dots++) {
                if (segment[at] == '.') at++;
                else if (length - at >= 3 && segment[at] == '%' &&
                         segment[at + 1] == '2' && (segment[at + 2] | 0x20) == 'e')
                        at += 3;
                else return 0;
        }
        return dots <= 2 ? dots : 0;
}

__attribute__((noinline)) void ref_http_parse_path_simplify(hp_u8 *path) {
        if (!path || path[0] != '/') return;
        hp_u64 length = 0;
        while (path[length] && path[length] != '?' && path[length] != '#') length++;
        hp_u64 read = 0, wrote = 0;
        while (read < length) {
                hp_u64 start = read + 1;
                hp_u64 stop = start;
                while (stop < length && path[stop] != '/') stop++;
                hp_u64 dots = ref_http_parse_dot_segment((const char *)path + start, stop - start);
                if (!dots) {
                        if (wrote != read) memmove(path + wrote, path + read, stop - read);
                        wrote += stop - read;
                } else {
                        while (dots == 2 && wrote && path[--wrote] != '/') ;
                        if (stop == length) path[wrote++] = '/';
                }
                read = stop;
        }
        if (wrote != length) memmove(path + wrote, path + length, strlen((const char *)path + length) + 1);
}

/* ── Test harness ──────────────────────────────────────────────────────────── */

static int g_failures = 0;
static uint64_t g_checks = 0;

#define CHECK(cond, msg, ...) do { \
        g_checks++; \
        if (!(cond)) { \
                fprintf(stderr, "FAIL [%s:%d]: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
                g_failures++; \
                if (g_failures > 20) exit(1); \
        } \
} while (0)

static uint64_t xorshift64(uint64_t *s) {
        uint64_t x = *s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return (*s = x);
}

static void test_exhaustive_bytes(void) {
        for (hp_u32 b = 0; b < 512; b++) {
                CHECK(http_parse_host_byte(b) == ref_http_parse_host_byte(b),
                      "host_byte(%u) asm=%d ref=%d", b, http_parse_host_byte(b), ref_http_parse_host_byte(b));
                CHECK(http_parse_token_byte(b) == ref_http_parse_token_byte(b),
                      "token_byte(%u) asm=%d ref=%d", b, http_parse_token_byte(b), ref_http_parse_token_byte(b));
        }
        for (int b = 0; b < 256; b++) {
                hp_u8 line[4] = {'X', ':', (hp_u8)b, 'y'};
                CHECK(http_parse_field_name(line, 4) == ref_http_parse_field_name(line, 4),
                      "field_name val byte=%d", b);
                hp_u8 name_line[3] = {(hp_u8)b, ':', '1'};
                CHECK(http_parse_field_name(name_line, 3) == ref_http_parse_field_name(name_line, 3),
                      "field_name key byte=%d", b);
                for (hp_u32 field = 0; field <= 1; field++) {
                        for (hp_u32 tight = 0; tight <= 1; tight++) {
                                CHECK(http_parse_line_valid(line, 4, field, tight) ==
                                      ref_http_parse_line_valid(line, 4, field, tight),
                                      "line_valid byte=%d field=%u tight=%u", b, field, tight);
                        }
                }
                if (b != 0) {
                        char s[4] = {'a', (char)b, 'z', '\0'};
                        for (hp_u32 k = 0; k <= 2; k++) {
                                CHECK(http_parse_request_component_valid(s, k) ==
                                      ref_http_parse_request_component_valid(s, k),
                                      "req_comp byte=%d kind=%u", b, k);
                        }
                }
        }
}

static void test_security_and_edges(void) {
        /* Scheme length */
        const char *schemes[] = {
                "", "http://a", "HTTPS://a", "h+1-2.3:x", "1http:x", ":x",
                "http", "a:", "ftp:21", "HTTP/1.1", "a_b:x", NULL
        };
        for (size_t i = 0; i < sizeof(schemes)/sizeof(schemes[0]); i++) {
                CHECK(http_parse_scheme_length(schemes[i]) == ref_http_parse_scheme_length(schemes[i]),
                      "scheme[%zu]", i);
        }

        /* Header end (CRLF, LF, mixed, earliest wins, truncated) */
        const char *heads[] = {
                "", "\n", "\n\n", "\r\n\r\n", "\n\r\n", "\r\n\n",
                "HTTP/1.1 200 OK\r\nHost: a\r\n\r\nBody",
                "HTTP/1.1 200 OK\nHost: a\n\n\r\nBody",
                "HTTP/1.1 200 OK\nHost: a\n\r\n\n\nBody",
                "HTTP/1.1 200 OK\r\nHost: a\r\n",
                "a\nb\rc\n\rd\n\r\ne",
                "a\n\r",
        };
        for (size_t i = 0; i < sizeof(heads)/sizeof(heads[0]); i++) {
                hp_u64 sz = strlen(heads[i]);
                CHECK(http_parse_header_end((const hp_u8 *)heads[i], sz) ==
                      ref_http_parse_header_end((const hp_u8 *)heads[i], sz),
                      "header_end[%zu]", i);
        }
        CHECK(http_parse_header_end(NULL, 10) == -1, "header_end NULL");

        /* Field name / obs-fold / smuggling / NUL */
        struct { const char *s; hp_u64 n; } fields[] = {
                {"Host: example.com", 17},
                {" Host: example.com", 18},
                {"\tHost: example.com", 18},
                {"Host : example.com", 18},
                {": val", 5},
                {"Host:", 5},
                {"Host: \t value", 13},
                {"Host: a\rb", 9},
                {"Host: a\nb", 9},
                {"Host: a\x00""b", 9},
                {"Host: a\x7f""b", 9},
                {"X-Custom!#$%&'*+-.^_`|~: ok\x80\xff", 30},
                {"Bad(Name): ok", 13},
                {"", 0},
        };
        for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); i++) {
                CHECK(http_parse_field_name((const hp_u8 *)fields[i].s, fields[i].n) ==
                      ref_http_parse_field_name((const hp_u8 *)fields[i].s, fields[i].n),
                      "field_name[%zu]", i);
        }

        /* Header lookup: case-insensitivity, non-letter bit-5 trap (@ vs `, [ vs {), OWS trim, duplicates */
        const char *hdr_block =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type:  \t text/html \t \r\n"
                "cOnTeNt-LeNgTh: 42\r\n"
                "X-@: wrong\r\n"
                "X-[: wrong2\r\n"
                "content-length: 99\n"
                "Empty:\r\n"
                "NoNewline: end";
        const char *lookups[] = {
                "content-type", "CONTENT-LENGTH", "X-`", "X-{", "Empty", "NoNewline", "Missing", ""
        };
        for (size_t i = 0; i < sizeof(lookups)/sizeof(lookups[0]); i++) {
                hp_u64 al = 0xdead, rl = 0xdead;
                hp_u8 ar = 99, rr = 99;
                const char *ap = http_parse_header((const hp_u8 *)hdr_block, strlen(hdr_block), lookups[i], &al, &ar);
                const char *rp = ref_http_parse_header((const hp_u8 *)hdr_block, strlen(hdr_block), lookups[i], &rl, &rr);
                CHECK(ap == rp && al == rl && ar == rr,
                      "header[%s]: ap=%p rp=%p al=%llu rl=%llu ar=%u rr=%u",
                      lookups[i], (const void *)ap, (const void *)rp,
                      (unsigned long long)al, (unsigned long long)rl, ar, rr);
        }

        /* Chunk lines: overflow 0xffffffffffffffff, overflow +1, extensions, quoted strings, escapes, CTLs */
        const char *chunks[] = {
                "0\r\n", "0\n", "a\r\n", "DEADbeef\r\n",
                "ffffffffffffffff\r\n",
                "10000000000000000\r\n",
                "ffffffffffffffff0\r\n",
                "000000000000000000000000000000001\r\n",
                "1a ;ext=val;flag;q=\"a\\\"b\\\\c\td\"\r\n",
                "1a;q=\"bad\x01\"\r\n",
                "1a;q=\"bad\x7f\"\r\n",
                "1a;q=\"unterminated\r\n",
                "1a;q=\"escape\\\r\n",
                "1a;=noname\r\n",
                "1a;noval=\r\n",
                "1a;bad name=1\r\n",
                "1a\r\r\n",
                "\r\n", "\n", "", "1a", " 1a\r\n", "-1\r\n", "0x10\r\n",
        };
        for (size_t i = 0; i < sizeof(chunks)/sizeof(chunks[0]); i++) {
                hp_u64 ac = 0x12345678, rc = 0x12345678;
                hp_i64 ar = http_parse_chunk_line((const hp_u8 *)chunks[i], strlen(chunks[i]), &ac);
                hp_i64 rr = ref_http_parse_chunk_line((const hp_u8 *)chunks[i], strlen(chunks[i]), &rc);
                CHECK(ar == rr && ac == rc, "chunk[%zu] ar=%lld rr=%lld ac=%llx rc=%llx",
                      i, (long long)ar, (long long)rr, (unsigned long long)ac, (unsigned long long)rc);
        }
        /* Embedded NUL in chunk quoted-string */
        {
                hp_u8 nul_chunk[] = {'1', ';', 'a', '=', '"', '\0', '"', '\r', '\n'};
                hp_u64 ac = 0, rc = 0;
                CHECK(http_parse_chunk_line(nul_chunk, sizeof(nul_chunk), &ac) == HP_MALFORMED, "chunk NUL rejected");
                CHECK(ref_http_parse_chunk_line(nul_chunk, sizeof(nul_chunk), &rc) == HP_MALFORMED, "ref chunk NUL");
        }

        /* Trailer lines */
        const char *trailers[] = {
                "\r\n", "\n", "X-Checksum: abc\r\n", "X-Checksum: abc\n",
                " Folded: bad\r\n", "Bad\r\r\n", "NoColon\r\n", "NoLF: 1", "", "\r"
        };
        for (size_t i = 0; i < sizeof(trailers)/sizeof(trailers[0]); i++) {
                CHECK(http_parse_trailer_line((const hp_u8 *)trailers[i], strlen(trailers[i])) ==
                      ref_http_parse_trailer_line((const hp_u8 *)trailers[i], strlen(trailers[i])),
                      "trailer[%zu]", i);
        }

        /* Status codes */
        const char *statuses[] = {
                "HTTP/1.1 200 OK\r\n", "HTTP/1.0 404\r\n", "HTTP/1.9 999\n",
                "HTTP/1.1 000 ", "HTTP/2.0 200 OK", "HTTP/1.a 200 OK",
                "HTTP/1.1 20a OK", "HTTP/1.1 200X\r\n", "HTTP/1.1 200", ""
        };
        for (size_t i = 0; i < sizeof(statuses)/sizeof(statuses[0]); i++) {
                hp_i32 ac = -99, rc = -99;
                hp_i64 ar = http_parse_status_code((const hp_u8 *)statuses[i], strlen(statuses[i]), &ac);
                hp_i64 rr = ref_http_parse_status_code((const hp_u8 *)statuses[i], strlen(statuses[i]), &rc);
                CHECK(ar == rr && ac == rc, "status[%zu] ar=%lld rr=%lld ac=%d rc=%d",
                      i, (long long)ar, (long long)rr, ac, rc);
        }

        /* Request component validation + recursive %25 CRLF/NUL smuggling */
        const char *comps[] = {
                "", "example.com", "sub_1.host-2.org", "bad host", "bad/host",
                "/path/ok?a=1&b=2", "/a%20b", "/a%2520b",
                "/a%0d", "/a%0a", "/a%00", "/a%0D", "/a%0A",
                "/a%250d", "/a%250a", "/a%2500",
                "/a%25250d", "/a%2525250a", "/a%2525252500",
                "/a%25%0d", "/a%25%25%0a", "/a%25", "/a%", "/a%2", "/a%zz",
                "/a\\b", "/a\x80""b", "Mozilla/5.0 (ok) \x80", "Bad\r\nInject: 1",
                "Bad\x7f", NULL
        };
        for (size_t i = 0; i < sizeof(comps)/sizeof(comps[0]); i++) {
                for (hp_u32 k = 0; k <= 2; k++) {
                        CHECK(http_parse_request_component_valid(comps[i], k) ==
                              ref_http_parse_request_component_valid(comps[i], k),
                              "comp[%zu] kind=%u", i, k);
                }
        }

        /* IPv4 spellings */
        const char *ips[] = {
                "127.0.0.1", "127.1", "0x7f.1", "0X7F.0.0.1", "0177.0.0.1",
                "2130706433", "0xffffffff", "0x100000000", "4294967295", "4294967296",
                "0.0.0.0", "0", "00", "08.0.0.1", "0x", "1.2.3.4.5", "1.2.3.",
                ".1.2.3", "1..2", "256.0.0.1", "1.16777216", "1.2.65536", "1.2.3.256",
                "1.2.3", "0x1.02.3", "example.com", ""
        };
        for (size_t i = 0; i < sizeof(ips)/sizeof(ips[0]); i++) {
                hp_u32 ai = 0xdeadbeefu, ri = 0xdeadbeefu;
                hp_i32 ar = http_parse_ipv4_spelling((const hp_u8 *)ips[i], strlen(ips[i]), &ai);
                hp_i32 rr = ref_http_parse_ipv4_spelling((const hp_u8 *)ips[i], strlen(ips[i]), &ri);
                CHECK(ar == rr && ai == ri, "ipv4[%s] ar=%d rr=%d ai=%08x ri=%08x", ips[i], ar, rr, ai, ri);
        }

        /* Dot segments & path simplify */
        const char *dots[] = {"", ".", "..", "...", "%2e", "%2E", "%2e.", ".%2E", "%2e%2e", "%2e%2e.", "%2f", "%2"};
        for (size_t i = 0; i < sizeof(dots)/sizeof(dots[0]); i++) {
                CHECK(http_parse_dot_segment(dots[i], strlen(dots[i])) ==
                      ref_http_parse_dot_segment(dots[i], strlen(dots[i])),
                      "dot[%s]", dots[i]);
        }
        const char *paths[] = {
                "/", "/a/b/c/./../../g", "/../../a", "/a/b/..", "/a/b/.",
                "/%2e/%2e%2e/a?x=../y#z", "/a/%2E/b/%2e./c", "/a/.../b",
                "/a/b/c", "relative/path", "/a/b/../../..", "/.//a/./b/.."
        };
        for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
                char a[128], r[128];
                strcpy(a, paths[i]);
                strcpy(r, paths[i]);
                http_parse_path_simplify((hp_u8 *)a);
                ref_http_parse_path_simplify((hp_u8 *)r);
                CHECK(strcmp(a, r) == 0, "path[%s] asm='%s' ref='%s'", paths[i], a, r);
        }
}

static void test_guard_pages(void) {
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        hp_u8 *mem = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(mem != MAP_FAILED, "mmap failed");
        CHECK(mprotect(mem + pg, pg, PROT_READ | PROT_WRITE) == 0, "mprotect failed");

        hp_u8 *lo = mem + pg;         /* flush against leading PROT_NONE */
        hp_u8 *hi = mem + 2 * pg;     /* end of RW page, flush against trailing PROT_NONE */

        const char *samples[] = {
                "HTTP/1.1 200 OK\r\nHost: example.com\r\nContent-Length: 5\r\n\r\n",
                "HTTP/1.1 200 ",
                "1a;ext=\"ok\"\r\n",
                "X-Test: val\r\n",
                "127.0.0.1",
                "/a/b/../c?q=1",
                "/a%25",
                "/a%",
                "https://x",
                "\n\r",
                "a",
        };
        for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
                size_t len = strlen(samples[i]);
                for (int end_side = 0; end_side < 2; end_side++) {
                        /* Raw (non-NUL-terminated) flush placement */
                        hp_u8 *p = end_side ? (hi - len) : lo;
                        memcpy(p, samples[i], len);
                        CHECK(http_parse_header_end(p, len) == ref_http_parse_header_end(p, len), "gp header_end");
                        CHECK(http_parse_field_name(p, len) == ref_http_parse_field_name(p, len), "gp field_name");
                        CHECK(http_parse_line_valid(p, len, 0, 0) == ref_http_parse_line_valid(p, len, 0, 0), "gp line 0 0");
                        CHECK(http_parse_line_valid(p, len, 0, 1) == ref_http_parse_line_valid(p, len, 0, 1), "gp line 0 1");
                        CHECK(http_parse_line_valid(p, len, 1, 1) == ref_http_parse_line_valid(p, len, 1, 1), "gp line 1 1");
                        hp_u64 al = 0, rl = 0; hp_u8 ar = 0, rr = 0;
                        CHECK(http_parse_header(p, len, "Host", &al, &ar) ==
                              ref_http_parse_header(p, len, "Host", &rl, &rr), "gp header");
                        CHECK(http_parse_chunk_extensions_valid((const char *)p, (const char *)(p + len)) ==
                              ref_http_parse_chunk_extensions_valid((const char *)p, (const char *)(p + len)), "gp cext");
                        hp_u64 ac = 0, rc = 0;
                        CHECK(http_parse_chunk_line(p, len, &ac) == ref_http_parse_chunk_line(p, len, &rc), "gp chunk");
                        CHECK(http_parse_trailer_line(p, len) == ref_http_parse_trailer_line(p, len), "gp trailer");
                        hp_i32 as = 0, rs = 0;
                        CHECK(http_parse_status_code(p, len, &as) == ref_http_parse_status_code(p, len, &rs), "gp status");
                        hp_u32 ai = 0, ri = 0;
                        CHECK(http_parse_ipv4_spelling(p, len, &ai) == ref_http_parse_ipv4_spelling(p, len, &ri), "gp ipv4");
                        CHECK(http_parse_dot_segment((const char *)p, len) ==
                              ref_http_parse_dot_segment((const char *)p, len), "gp dot");

                        /* NUL-terminated flush placement */
                        hp_u8 *z = end_side ? (hi - len - 1) : lo;
                        memcpy(z, samples[i], len + 1);
                        CHECK(http_parse_scheme_length((const char *)z) ==
                              ref_http_parse_scheme_length((const char *)z), "gp scheme");
                        for (hp_u32 k = 0; k <= 2; k++) {
                                CHECK(http_parse_request_component_valid((const char *)z, k) ==
                                      ref_http_parse_request_component_valid((const char *)z, k), "gp req_comp");
                        }
                        char ref_path[128];
                        memcpy(ref_path, samples[i], len + 1);
                        http_parse_path_simplify(z);
                        ref_http_parse_path_simplify((hp_u8 *)ref_path);
                        CHECK(strcmp((const char *)z, ref_path) == 0, "gp path_simplify");
                }
        }
        /* Zero-length slice right AT the guard page boundary (must never dereference!) */
        CHECK(http_parse_header_end(hi, 0) == -1, "gp zero header_end");
        CHECK(http_parse_field_name(hi, 0) == -1, "gp zero field_name");
        CHECK(http_parse_line_valid(hi, 0, 0, 1) == 1, "gp zero line_valid");
        CHECK(http_parse_chunk_extensions_valid((const char *)hi, (const char *)hi) == 1, "gp zero cext");
        CHECK(http_parse_chunk_line(hi, 0, NULL) == HP_MALFORMED, "gp zero chunk");
        CHECK(http_parse_trailer_line(hi, 0) == HP_MALFORMED, "gp zero trailer");
        CHECK(http_parse_status_code(hi, 0, NULL) == HP_NO_REPLY, "gp zero status");
        CHECK(http_parse_ipv4_spelling(hi, 0, NULL) == 0, "gp zero ipv4");
        CHECK(http_parse_dot_segment((const char *)hi, 0) == 0, "gp zero dot");

        munmap(mem, 3 * pg);
}

static void test_fuzz(void) {
        uint64_t rng = 0x9e3779b97f4a7c15ULL;
        const char alpha[] = "aAzZ09xX.:/%2eE-;=\"\\ \t\r\n?#_+-!~";
        hp_u8 buf[96];

        for (int iter = 0; iter < 50000; iter++) {
                hp_u64 len = xorshift64(&rng) % 64;
                int bias = (int)(xorshift64(&rng) % 3);
                for (hp_u64 i = 0; i < len; i++) {
                        uint64_t r = xorshift64(&rng);
                        buf[i] = (bias == 0) ? (hp_u8)(r & 0xff) : (hp_u8)alpha[r % (sizeof(alpha) - 1)];
                }
                buf[len] = 0;

                CHECK(http_parse_header_end(buf, len) == ref_http_parse_header_end(buf, len), "fuzz header_end");
                CHECK(http_parse_field_name(buf, len) == ref_http_parse_field_name(buf, len), "fuzz field_name");
                hp_u32 f = (hp_u32)(xorshift64(&rng) & 1);
                hp_u32 t = (hp_u32)(xorshift64(&rng) & 1);
                CHECK(http_parse_line_valid(buf, len, f, t) == ref_http_parse_line_valid(buf, len, f, t), "fuzz line");
                CHECK(http_parse_chunk_extensions_valid((const char *)buf, (const char *)(buf + len)) ==
                      ref_http_parse_chunk_extensions_valid((const char *)buf, (const char *)(buf + len)), "fuzz cext");
                hp_u64 ac = 0x55, rc = 0x55;
                CHECK(http_parse_chunk_line(buf, len, &ac) == ref_http_parse_chunk_line(buf, len, &rc) && ac == rc, "fuzz chunk");
                CHECK(http_parse_trailer_line(buf, len) == ref_http_parse_trailer_line(buf, len), "fuzz trailer");
                hp_i32 as = 7, rs = 7;
                CHECK(http_parse_status_code(buf, len, &as) == ref_http_parse_status_code(buf, len, &rs) && as == rs, "fuzz status");
                hp_u32 ai = 9, ri = 9;
                CHECK(http_parse_ipv4_spelling(buf, len, &ai) == ref_http_parse_ipv4_spelling(buf, len, &ri) && ai == ri, "fuzz ipv4");
                CHECK(http_parse_dot_segment((const char *)buf, len) == ref_http_parse_dot_segment((const char *)buf, len), "fuzz dot");
                CHECK(http_parse_scheme_length((const char *)buf) == ref_http_parse_scheme_length((const char *)buf), "fuzz scheme");
                hp_u32 k = (hp_u32)(xorshift64(&rng) % 3);
                CHECK(http_parse_request_component_valid((const char *)buf, k) ==
                      ref_http_parse_request_component_valid((const char *)buf, k), "fuzz req_comp");

                hp_u64 al = 0, rl = 0; hp_u8 ar = 0, rr = 0;
                const char *ap = http_parse_header(buf, len, "X-A", &al, &ar);
                const char *rp = ref_http_parse_header(buf, len, "X-A", &rl, &rr);
                CHECK(ap == rp && al == rl && ar == rr, "fuzz header");

                hp_u8 pa[96], pr[96];
                memcpy(pa, buf, len + 1);
                memcpy(pr, buf, len + 1);
                http_parse_path_simplify(pa);
                ref_http_parse_path_simplify(pr);
                CHECK(strcmp((const char *)pa, (const char *)pr) == 0, "fuzz path");
        }
}

static uint64_t now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void run_bench(void) {
        const char *resp =
                "HTTP/1.1 200 OK\r\n"
                "Date: Fri, 27 Nov 2026 16:00:00 GMT\r\n"
                "Server: moonwater/1.0\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Transfer-Encoding: chunked\r\n"
                "X-Request-Id: 8f3a91c2-4b5e-4c6d-8e9f-0123456789ab\r\n"
                "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                "\r\n";
        hp_u64 rlen = strlen(resp);
        const char *field = "X-Request-Id: 8f3a91c2-4b5e-4c6d-8e9f-0123456789ab";
        hp_u64 flen = strlen(field);
        const char *chunk = "1a2b3c;ext=val;q=\"hello\\\"world\"\r\n";
        hp_u64 clen = strlen(chunk);
        const char *target = "/api/v1/users/12345/profile%20pic?sort=asc&limit=50";
        const char *ip = "192.168.100.42";
        hp_u64 ilen = strlen(ip);

        const int N = 500000;
        volatile uint64_t sink = 0;

        #define BENCH(label, ptr_var, asm_expr, ref_expr) do { \
                const void *volatile _p = (ptr_var); \
                uint64_t t0 = now_ns(); \
                uint64_t acc = 0; \
                for (int i = 0; i < N; i++) { \
                        const char *p = (const char *)_p; \
                        acc += (uint64_t)(asm_expr); \
                } \
                uint64_t t1 = now_ns(); \
                for (int i = 0; i < N; i++) { \
                        const char *p = (const char *)_p; \
                        acc += (uint64_t)(ref_expr); \
                } \
                uint64_t t2 = now_ns(); \
                sink ^= acc; \
                double a_ns = (double)(t1 - t0) / N; \
                double r_ns = (double)(t2 - t1) / N; \
                printf("  %-26s ASM %6.2f ns/op | C -O2 %6.2f ns/op | %.2fx\n", \
                       label, a_ns, r_ns, r_ns / (a_ns > 0.001 ? a_ns : 0.001)); \
        } while (0)

        hp_u64 out_u64 = 0; hp_u8 out_rep = 0; hp_i32 out_code = 0; hp_u32 out_ip = 0;
        printf("Benchmark (%d iters each):\n", N);
        BENCH("http_parse_header_end", resp,
              http_parse_header_end((const hp_u8 *)p, rlen),
              ref_http_parse_header_end((const hp_u8 *)p, rlen));
        BENCH("http_parse_field_name", field,
              http_parse_field_name((const hp_u8 *)p, flen),
              ref_http_parse_field_name((const hp_u8 *)p, flen));
        BENCH("http_parse_header", resp,
              (uintptr_t)http_parse_header((const hp_u8 *)p, rlen, "cache-control", &out_u64, &out_rep),
              (uintptr_t)ref_http_parse_header((const hp_u8 *)p, rlen, "cache-control", &out_u64, &out_rep));
        BENCH("http_parse_chunk_line", chunk,
              http_parse_chunk_line((const hp_u8 *)p, clen, &out_u64),
              ref_http_parse_chunk_line((const hp_u8 *)p, clen, &out_u64));
        BENCH("http_parse_status_code", resp,
              http_parse_status_code((const hp_u8 *)p, rlen, &out_code),
              ref_http_parse_status_code((const hp_u8 *)p, rlen, &out_code));
        BENCH("http_parse_req_target", target,
              http_parse_request_component_valid(p, HP_REQ_TARGET),
              ref_http_parse_request_component_valid(p, HP_REQ_TARGET));
        BENCH("http_parse_ipv4_spelling", ip,
              http_parse_ipv4_spelling((const hp_u8 *)p, ilen, &out_ip),
              ref_http_parse_ipv4_spelling((const hp_u8 *)p, ilen, &out_ip));
        (void)sink;
}

int main(int argc, char **argv) {
        test_exhaustive_bytes();
        test_security_and_edges();
        test_guard_pages();
        test_fuzz();
        printf("PASS: %llu differential/security/guard-page/fuzz checks, 0 failures\n",
               (unsigned long long)g_checks);
        if (argc > 1 && strcmp(argv[1], "--bench") == 0)
                run_bench();
        return g_failures ? 1 : 0;
}
