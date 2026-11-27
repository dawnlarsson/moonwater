/*
        RFC 9110 / 9112 / 3986 HTTP parser & validator primitives in pure assembly:
          - http_parse_host_byte              : DNS/Host byte allowlist [A-Za-z0-9._-]
          - http_parse_token_byte             : RFC 9110 tchar byte allowlist (zero-table bitmask)
          - http_parse_scheme_length          : RFC 3986 3.1 scheme + ':' length scanner
          - http_parse_header_end             : single-pass \n\r\n / \n\n blank-line finder
          - http_parse_field_name             : token ':' text-value validator (rejects obs-fold/CTL)
          - http_parse_line_valid             : status-line (default/tight) or field-line validator
          - http_parse_header                 : case-insensitive header lookup + OWS trim + repeat detect
          - http_parse_chunk_extensions_valid : RFC 9112 chunk-ext grammar (quoted-string + escapes)
          - http_parse_chunk_line             : overflow-checked hex chunk-size + extensions parser
          - http_parse_trailer_line           : chunked trailer / terminating blank-line validator
          - http_parse_status_code            : HTTP/1.x NNN status-line parser
          - http_parse_request_component_valid: target/host/field validator + recursive %25 CRLF/NUL peel
          - http_parse_ipv4_spelling          : inet_aton 1..4 part dec/oct/hex IPv4 parser
          - http_parse_dot_segment            : '.' / '..' / %2e segment recognizer
          - http_parse_path_simplify          : in-place RFC 3986 5.2.4 remove_dot_segments

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define NET_HTTP_PARSE_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_NET_HTTP_PARSE_H
#define MOONWATER_NET_HTTP_PARSE_H

#ifdef STANDARD_MODERN_C
typedef p8  hp_u8;
typedef p32 hp_u32;
typedef b32 hp_i32;
typedef p64 hp_u64;
typedef b64 hp_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
    defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  hp_u8;
typedef __UINT32_TYPE__ hp_u32;
typedef __INT32_TYPE__  hp_i32;
typedef __UINT64_TYPE__ hp_u64;
typedef __INT64_TYPE__  hp_i64;
#else
#include <stdint.h>
typedef uint8_t  hp_u8;
typedef uint32_t hp_u32;
typedef int32_t  hp_i32;
typedef uint64_t hp_u64;
typedef int64_t  hp_i64;
#endif

#define HP_OK         0
#define HP_NO_REPLY  (-4)
#define HP_MALFORMED (-5)

#define HP_REQ_FIELD  0u
#define HP_REQ_TARGET 1u
#define HP_REQ_HOST   2u

#if defined(HTTP_OK) && defined(HTTP_NO_REPLY) && defined(HTTP_MALFORMED)
_Static_assert(HP_OK == HTTP_OK && HP_NO_REPLY == HTTP_NO_REPLY && HP_MALFORMED == HTTP_MALFORMED,
               "HP_* status constants mismatch");
#endif

hp_i32      http_parse_host_byte(hp_u32 byte);
hp_i32      http_parse_token_byte(hp_u32 byte);
hp_u64      http_parse_scheme_length(const char *url);
hp_i64      http_parse_header_end(const hp_u8 *bytes, hp_u64 size);
hp_i64      http_parse_field_name(const hp_u8 *line, hp_u64 stop);
hp_i32      http_parse_line_valid(const hp_u8 *line, hp_u64 stop, hp_u32 field, hp_u32 tight);
const char *http_parse_header(const hp_u8 *bytes, hp_u64 size, const char *name,
                              hp_u64 *length_out, hp_u8 *repeated_out);
hp_i32      http_parse_chunk_extensions_valid(const char *at, const char *stop);
hp_i64      http_parse_chunk_line(const hp_u8 *line, hp_u64 line_length, hp_u64 *chunk_length_out);
hp_i64      http_parse_trailer_line(const hp_u8 *line, hp_u64 line_length);
hp_i64      http_parse_status_code(const hp_u8 *bytes, hp_u64 size, hp_i32 *code_out);
hp_i32      http_parse_request_component_valid(const char *text, hp_u32 kind);
hp_i32      http_parse_ipv4_spelling(const hp_u8 *host, hp_u64 length, hp_u32 *ip_out);
hp_u64      http_parse_dot_segment(const char *segment, hp_u64 length);
void        http_parse_path_simplify(hp_u8 *path);

#endif /* MOONWATER_NET_HTTP_PARSE_H */

#ifdef NET_HTTP_PARSE_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define HP_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define HP_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define HP_RISCV64 1
# else
#  error "src/net/http_parse.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define HP_ARM64 1
# elif defined(X64) && X64
#  define HP_X64 1
# elif defined(RISCV64) && RISCV64
#  define HP_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define HP_ASM_SECTION   ASM_SECTION
# define HP_ASM_FUNC(n)   ASM_FUNC(n)
# define HP_ASM_END(n)    ASM_END(n)
# define HP_ASM_RET       ASM_RET
#else
# define HP_ASM_SECTION   ".text\n"
# define HP_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define HP_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define HP_ASM_END(n)   ""
# elif defined(_WIN32)
#  define HP_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define HP_ASM_END(n)   ""
# else
#  if defined(HP_ARM64)
#   define HP_ASM_FTYPE   "%function"
#  else
#   define HP_ASM_FTYPE   "@function"
#  endif
#  define HP_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " HP_ASM_FTYPE "\n" #n ":\n"
#  define HP_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(HP_ARM64)

__asm__(
        HP_ASM_SECTION

        /* http_parse_host_byte(w0=byte) -> [A-Za-z0-9._-] */
        HP_ASM_FUNC(http_parse_host_byte)
        "and w0, w0, #0xff\n   orr w1, w0, #32\n   sub w1, w1, #97\n   cmp w1, #25\n   b.ls 1f\n"
        "sub w1, w0, #48\n   cmp w1, #9\n   b.ls 1f\n   sub w1, w0, #45\n   cmp w1, #1\n   b.ls 1f\n"
        "cmp w0, #95\n   cset w0, eq\n" HP_ASM_RET
        "1:  mov w0, #1\n" HP_ASM_RET
        HP_ASM_END(http_parse_host_byte)

        /* http_parse_token_byte(w0=byte) -> RFC 9110 tchar via 128-bit bitmask */
        HP_ASM_FUNC(http_parse_token_byte)
        "and w0, w0, #0xff\n   cmp w0, #128\n   b.hs 2f\n   cmp w0, #64\n   b.hs 1f\n"
        "movz x1, #0x6cfa, lsl #32\n   movk x1, #0x03ff, lsl #48\n   lsr x1, x1, x0\n   and w0, w1, #1\n" HP_ASM_RET
        "1:  movn x1, #1\n   movk x1, #0xc7ff, lsl #16\n   movk x1, #0x57ff, lsl #48\n   lsr x1, x1, x0\n   and w0, w1, #1\n" HP_ASM_RET
        "2:  mov w0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_token_byte)

        /* http_parse_scheme_length(x0=url) -> len(scheme + ':') or 0 */
        HP_ASM_FUNC(http_parse_scheme_length)
        "cbz x0, 4f\n   ldrb w1, [x0]\n   orr w2, w1, #32\n   sub w2, w2, #97\n   cmp w2, #25\n   b.hi 4f\n"
        "mov x3, #1\n"
        "1:  ldrb w1, [x0, x3]\n   orr w2, w1, #32\n   sub w2, w2, #97\n   cmp w2, #25\n   b.ls 2f\n"
        "sub w2, w1, #48\n   cmp w2, #9\n   b.ls 2f\n   sub w2, w1, #45\n   cmp w2, #1\n   b.ls 2f\n"
        "cmp w1, #43\n   b.ne 3f\n"
        "2:  add x3, x3, #1\n   b 1b\n"
        "3:  cmp w1, #58\n   b.ne 4f\n   add x0, x3, #1\n" HP_ASM_RET
        "4:  mov x0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_scheme_length)

        /* http_parse_header_end(x0=bytes, x1=size) -> end offset of \n\n or \n\r\n, else -1 */
        HP_ASM_FUNC(http_parse_header_end)
        "cbz x0, 4f\n   cmp x1, #2\n   b.lo 4f\n   sub x2, x1, #1\n   mov x3, #0\n"
        "1:  cmp x3, x2\n   b.hs 4f\n   ldrb w4, [x0, x3]\n   add x3, x3, #1\n   cmp w4, #10\n   b.ne 1b\n"
        "ldrb w4, [x0, x3]\n   cmp w4, #10\n   b.eq 2f\n   cmp w4, #13\n   b.ne 1b\n"
        "add x5, x3, #1\n   cmp x5, x1\n   b.hs 1b\n   ldrb w4, [x0, x5]\n   cmp w4, #10\n   b.ne 1b\n"
        "add x0, x5, #1\n" HP_ASM_RET
        "2:  add x0, x3, #1\n" HP_ASM_RET
        "4:  mov x0, #-1\n" HP_ASM_RET
        HP_ASM_END(http_parse_header_end)

        /* http_parse_field_name(x0=line, x1=stop) -> name len before ':', else -1 */
        HP_ASM_FUNC(http_parse_field_name)
        "cbz x0, 6f\n   cbz x1, 6f\n   movz x8, #0x6cfa, lsl #32\n   movk x8, #0x03ff, lsl #48\n"
        "movn x9, #1\n   movk x9, #0xc7ff, lsl #16\n   movk x9, #0x57ff, lsl #48\n   mov x2, #0\n"
        "1:  cmp x2, x1\n   b.hs 6f\n   ldrb w3, [x0, x2]\n   cmp w3, #128\n   b.hs 6f\n"
        "cmp w3, #64\n   csel x4, x8, x9, lo\n   lsr x4, x4, x3\n   tbz x4, #0, 2f\n   add x2, x2, #1\n   b 1b\n"
        "2:  cbz x2, 6f\n   cmp w3, #58\n   b.ne 6f\n   add x5, x2, #1\n"
        "3:  cmp x5, x1\n   b.hs 5f\n   ldrb w3, [x0, x5]\n   add x5, x5, #1\n"
        "cmp w3, #9\n   b.eq 3b\n   cmp w3, #32\n   b.lo 6f\n   cmp w3, #127\n   b.ne 3b\n"
        "6:  mov x0, #-1\n" HP_ASM_RET
        "5:  mov x0, x2\n" HP_ASM_RET
        HP_ASM_END(http_parse_field_name)

        /* http_parse_line_valid(x0=line, x1=stop, w2=field, w3=tight) -> 1 valid, 0 invalid */
        HP_ASM_FUNC(http_parse_line_valid)
        "cbnz w2, 4f\n   cbz x1, 8f\n   cbz x0, 9f\n   mov x4, #0\n   cbnz w3, 2f\n"
        "1:  cmp x4, x1\n   b.hs 8f\n   ldrb w5, [x0, x4]\n   add x4, x4, #1\n"
        "cbz w5, 9f\n   cmp w5, #13\n   b.ne 1b\n   b 9f\n"
        "2:  cmp x4, x1\n   b.hs 8f\n   ldrb w5, [x0, x4]\n   add x4, x4, #1\n"
        "cmp w5, #9\n   b.eq 2b\n   cmp w5, #32\n   b.lo 9f\n   cmp w5, #127\n   b.ne 2b\n   b 9f\n"
        "4:  cbz x0, 9f\n   cbz x1, 9f\n   movz x8, #0x6cfa, lsl #32\n   movk x8, #0x03ff, lsl #48\n"
        "movn x9, #1\n   movk x9, #0xc7ff, lsl #16\n   movk x9, #0x57ff, lsl #48\n   mov x4, #0\n"
        "5:  cmp x4, x1\n   b.hs 9f\n   ldrb w5, [x0, x4]\n   cmp w5, #128\n   b.hs 9f\n"
        "cmp w5, #64\n   csel x6, x8, x9, lo\n   lsr x6, x6, x5\n   tbz x6, #0, 6f\n   add x4, x4, #1\n   b 5b\n"
        "6:  cbz x4, 9f\n   cmp w5, #58\n   b.ne 9f\n   add x4, x4, #1\n   b 2b\n"
        "8:  mov w0, #1\n" HP_ASM_RET
        "9:  mov w0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_line_valid)

        /* http_parse_header(x0=bytes, x1=size, x2=name, x3=length_out, x4=repeated_out) */
        HP_ASM_FUNC(http_parse_header)
        "cbz x4, 1f\n   strb wzr, [x4]\n"
        "1:  cbz x0, 14f\n   cbz x2, 14f\n   mov x5, #0\n"
        "2:  ldrb w6, [x2, x5]\n   cbz w6, 3f\n   add x5, x5, #1\n   b 2b\n"
        "3:  mov x6, #0\n   mov x7, #0\n   mov x8, #0\n"
        "4:  cmp x6, x1\n   b.hs 12f\n   mov x9, x6\n   mov x10, x6\n"
        "5:  cmp x10, x1\n   b.hs 6f\n   ldrb w11, [x0, x10]\n   cmp w11, #10\n   b.eq 6f\n   add x10, x10, #1\n   b 5b\n"
        "6:  cmp x10, x1\n   cinc x6, x10, lo\n   cmp x10, x9\n   b.ls 7f\n"
        "sub x11, x10, #1\n   ldrb w12, [x0, x11]\n   cmp w12, #13\n   csel x10, x11, x10, eq\n"
        "7:  sub x11, x10, x9\n   cmp x11, x5\n   b.ls 4b\n   add x11, x9, x5\n"
        "ldrb w12, [x0, x11]\n   cmp w12, #58\n   b.ne 4b\n   mov x12, #0\n"
        "8:  cmp x12, x5\n   b.hs 9f\n   add x13, x9, x12\n   ldrb w14, [x0, x13]\n   ldrb w15, [x2, x12]\n"
        "cmp w14, w15\n   b.eq 15f\n   eor w16, w14, w15\n   cmp w16, #32\n   b.ne 4b\n"
        "orr w14, w14, #32\n   sub w14, w14, #97\n   cmp w14, #25\n   b.hi 4b\n"
        "15: add x12, x12, #1\n   b 8b\n"
        "9:  cbz x7, 10f\n   cbz x4, 4b\n   mov w12, #1\n   strb w12, [x4]\n   b 4b\n"
        "10: add x11, x11, #1\n"
        "11: cmp x11, x10\n   b.hs 16f\n   ldrb w12, [x0, x11]\n   cmp w12, #32\n   b.eq 17f\n   cmp w12, #9\n   b.ne 16f\n"
        "17: add x11, x11, #1\n   b 11b\n"
        "16: cmp x10, x11\n   b.ls 18f\n   sub x12, x10, #1\n   ldrb w13, [x0, x12]\n"
        "cmp w13, #32\n   b.eq 19f\n   cmp w13, #9\n   b.ne 18f\n"
        "19: mov x10, x12\n   b 16b\n"
        "18: add x7, x0, x11\n   sub x8, x10, x11\n   b 4b\n"
        "12: cbz x7, 13f\n   cbz x3, 13f\n   str x8, [x3]\n"
        "13: mov x0, x7\n" HP_ASM_RET
        "14: mov x0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_header)

        /* http_parse_chunk_extensions_valid(x0=at, x1=stop) -> 1 valid, 0 invalid */
        HP_ASM_FUNC(http_parse_chunk_extensions_valid)
        "cmp x0, x1\n   b.hs 11f\n   cbz x0, 12f\n   cbz x1, 12f\n"
        "movz x8, #0x6cfa, lsl #32\n   movk x8, #0x03ff, lsl #48\n"
        "movn x9, #1\n   movk x9, #0xc7ff, lsl #16\n   movk x9, #0x57ff, lsl #48\n"
        "1:  cmp x0, x1\n   b.hs 11f\n   ldrb w2, [x0], #1\n   cmp w2, #59\n   b.ne 12f\n   mov x3, x0\n"
        "2:  cmp x0, x1\n   b.hs 3f\n   ldrb w2, [x0]\n   cmp w2, #128\n   b.hs 3f\n"
        "cmp w2, #64\n   csel x4, x8, x9, lo\n   lsr x4, x4, x2\n   tbz x4, #0, 3f\n   add x0, x0, #1\n   b 2b\n"
        "3:  cmp x0, x3\n   b.eq 12f\n   cmp x0, x1\n   b.hs 11f\n   ldrb w2, [x0]\n   cmp w2, #61\n   b.ne 1b\n"
        "add x0, x0, #1\n   cmp x0, x1\n   b.hs 12f\n   ldrb w2, [x0]\n   cmp w2, #34\n   b.ne 8f\n   add x0, x0, #1\n"
        "4:  cmp x0, x1\n   b.hs 12f\n   ldrb w2, [x0]\n   cmp w2, #34\n   b.eq 7f\n"
        "cmp w2, #92\n   b.ne 5f\n   add x0, x0, #1\n   cmp x0, x1\n   b.hs 12f\n   ldrb w2, [x0]\n"
        "5:  cmp w2, #9\n   b.eq 6f\n   cmp w2, #32\n   b.lo 12f\n   cmp w2, #127\n   b.eq 12f\n"
        "6:  add x0, x0, #1\n   b 4b\n"
        "7:  add x0, x0, #1\n   b 1b\n"
        "8:  mov x3, x0\n"
        "9:  cmp x0, x1\n   b.hs 10f\n   ldrb w2, [x0]\n   cmp w2, #128\n   b.hs 10f\n"
        "cmp w2, #64\n   csel x4, x8, x9, lo\n   lsr x4, x4, x2\n   tbz x4, #0, 10f\n   add x0, x0, #1\n   b 9b\n"
        "10: cmp x0, x3\n   b.ne 1b\n"
        "12: mov w0, #0\n" HP_ASM_RET
        "11: mov w0, #1\n" HP_ASM_RET
        HP_ASM_END(http_parse_chunk_extensions_valid)

        /* http_parse_chunk_line(x0=line, x1=len, x2=chunk_len_out) -> 0 ok, -5 malformed */
        HP_ASM_FUNC(http_parse_chunk_line)
        "cbz x0, 15f\n   cbz x1, 15f\n   add x1, x0, x1\n   ldrb w3, [x1, #-1]!\n   cmp w3, #10\n   b.ne 15f\n"
        "cmp x1, x0\n   b.ls 1f\n   ldrb w3, [x1, #-1]\n   cmp w3, #13\n   b.ne 1f\n   sub x1, x1, #1\n"
        "1:  mov x4, #0\n   mov x5, #0\n"
        "2:  cmp x0, x1\n   b.hs 4f\n   ldrb w3, [x0]\n   sub w6, w3, #48\n   cmp w6, #9\n   b.ls 3f\n"
        "orr w6, w3, #32\n   sub w6, w6, #97\n   cmp w6, #5\n   b.hi 4f\n   add w6, w6, #10\n"
        "3:  tst x4, #0xf000000000000000\n   b.ne 15f\n   orr x4, x6, x4, lsl #4\n   add x0, x0, #1\n   mov x5, #1\n   b 2b\n"
        "4:  cbz x5, 15f\n"
        "5:  cmp x0, x1\n   b.hs 14f\n   ldrb w3, [x0]\n   cmp w3, #32\n   b.eq 6f\n   cmp w3, #9\n   b.ne 7f\n"
        "6:  add x0, x0, #1\n   b 5b\n"
        "7:  movz x8, #0x6cfa, lsl #32\n   movk x8, #0x03ff, lsl #48\n"
        "movn x9, #1\n   movk x9, #0xc7ff, lsl #16\n   movk x9, #0x57ff, lsl #48\n"
        "8:  cmp x0, x1\n   b.hs 14f\n   ldrb w3, [x0], #1\n   cmp w3, #59\n   b.ne 15f\n   mov x6, x0\n"
        "9:  cmp x0, x1\n   b.hs 10f\n   ldrb w3, [x0]\n   cmp w3, #128\n   b.hs 10f\n"
        "cmp w3, #64\n   csel x7, x8, x9, lo\n   lsr x7, x7, x3\n   tbz x7, #0, 10f\n   add x0, x0, #1\n   b 9b\n"
        "10: cmp x0, x6\n   b.eq 15f\n   cmp x0, x1\n   b.hs 14f\n   ldrb w3, [x0]\n   cmp w3, #61\n   b.ne 8b\n"
        "add x0, x0, #1\n   cmp x0, x1\n   b.hs 15f\n   ldrb w3, [x0]\n   cmp w3, #34\n   b.ne 12f\n   add x0, x0, #1\n"
        "11: cmp x0, x1\n   b.hs 15f\n   ldrb w3, [x0]\n   cmp w3, #34\n   b.eq 16f\n"
        "cmp w3, #92\n   b.ne 17f\n   add x0, x0, #1\n   cmp x0, x1\n   b.hs 15f\n   ldrb w3, [x0]\n"
        "17: cmp w3, #9\n   b.eq 18f\n   cmp w3, #32\n   b.lo 15f\n   cmp w3, #127\n   b.eq 15f\n"
        "18: add x0, x0, #1\n   b 11b\n"
        "16: add x0, x0, #1\n   b 8b\n"
        "12: mov x6, x0\n"
        "13: cmp x0, x1\n   b.hs 19f\n   ldrb w3, [x0]\n   cmp w3, #128\n   b.hs 19f\n"
        "cmp w3, #64\n   csel x7, x8, x9, lo\n   lsr x7, x7, x3\n   tbz x7, #0, 19f\n   add x0, x0, #1\n   b 13b\n"
        "19: cmp x0, x6\n   b.ne 8b\n"
        "15: mov x0, #-5\n" HP_ASM_RET
        "14: cbz x2, 20f\n   str x4, [x2]\n"
        "20: mov x0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_chunk_line)

        /* http_parse_trailer_line(x0=line, x1=len) -> 1 blank, 0 valid field, -5 malformed */
        HP_ASM_FUNC(http_parse_trailer_line)
        "cbz x0, 6f\n   cbz x1, 6f\n   sub x1, x1, #1\n   ldrb w2, [x0, x1]\n   cmp w2, #10\n   b.ne 6f\n"
        "cbz x1, 7f\n   sub x3, x1, #1\n   ldrb w2, [x0, x3]\n   cmp w2, #13\n   csel x1, x3, x1, eq\n   cbz x1, 7f\n"
        "movz x8, #0x6cfa, lsl #32\n   movk x8, #0x03ff, lsl #48\n"
        "movn x9, #1\n   movk x9, #0xc7ff, lsl #16\n   movk x9, #0x57ff, lsl #48\n   mov x3, #0\n"
        "1:  cmp x3, x1\n   b.hs 6f\n   ldrb w2, [x0, x3]\n   cmp w2, #128\n   b.hs 6f\n"
        "cmp w2, #64\n   csel x4, x8, x9, lo\n   lsr x4, x4, x2\n   tbz x4, #0, 2f\n   add x3, x3, #1\n   b 1b\n"
        "2:  cbz x3, 6f\n   cmp w2, #58\n   b.ne 6f\n   add x3, x3, #1\n"
        "3:  cmp x3, x1\n   b.hs 5f\n   ldrb w2, [x0, x3]\n   add x3, x3, #1\n"
        "cmp w2, #9\n   b.eq 3b\n   cmp w2, #32\n   b.lo 6f\n   cmp w2, #127\n   b.ne 3b\n"
        "6:  mov x0, #-5\n" HP_ASM_RET
        "7:  mov x0, #1\n" HP_ASM_RET
        "5:  mov x0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_trailer_line)

        /* http_parse_status_code(x0=bytes, x1=size, x2=code_out) -> 0 ok, -4 short, -5 bad */
        HP_ASM_FUNC(http_parse_status_code)
        "cbz x0, 1f\n   cmp x1, #13\n   b.lo 1f\n"
        "ldr x3, [x0]\n   and x4, x3, #0x00ffffffffffffff\n"
        "movz x5, #0x5448\n   movk x5, #0x5054, lsl #16\n   movk x5, #0x312f, lsl #32\n   movk x5, #0x002e, lsl #48\n"
        "cmp x4, x5\n   b.ne 2f\n   lsr x3, x3, #56\n   sub w3, w3, #48\n   cmp w3, #9\n   b.hi 2f\n"
        "ldrb w3, [x0, #8]\n   cmp w3, #32\n   b.ne 2f\n"
        "ldrb w4, [x0, #9]\n   sub w4, w4, #48\n   cmp w4, #9\n   b.hi 2f\n"
        "ldrb w5, [x0, #10]\n   sub w5, w5, #48\n   cmp w5, #9\n   b.hi 2f\n"
        "ldrb w6, [x0, #11]\n   sub w6, w6, #48\n   cmp w6, #9\n   b.hi 2f\n"
        "ldrb w7, [x0, #12]\n   cmp w7, #32\n   b.eq 3f\n   cmp w7, #13\n   b.eq 3f\n   cmp w7, #10\n   b.ne 2f\n"
        "3:  cbz x2, 4f\n   mov w7, #100\n   mov w8, #10\n   madd w6, w4, w7, w6\n   madd w6, w5, w8, w6\n   str w6, [x2]\n"
        "4:  mov x0, #0\n" HP_ASM_RET
        "1:  mov x0, #-4\n" HP_ASM_RET
        "2:  mov x0, #-5\n" HP_ASM_RET
        HP_ASM_END(http_parse_status_code)

        /* http_parse_request_component_valid(x0=text, w1=kind) -> 1 valid, 0 invalid */
        HP_ASM_FUNC(http_parse_request_component_valid)
        "cbz x0, 12f\n   cbz w1, 20f\n   cmp w1, #1\n   b.eq 21f\n   cmp w1, #2\n   b.ne 22f\n"
        "ldrb w3, [x0]\n   cbz w3, 12f\n"
        "9:  ldrb w3, [x0], #1\n   cbz w3, 11f\n   orr w6, w3, #32\n   sub w6, w6, #97\n   cmp w6, #25\n   b.ls 9b\n"
        "sub w6, w3, #48\n   cmp w6, #9\n   b.ls 9b\n   sub w6, w3, #45\n   cmp w6, #1\n   b.ls 9b\n"
        "cmp w3, #95\n   b.eq 9b\n   b 12f\n"
        "20: ldrb w3, [x0], #1\n   cbz w3, 11f\n   cmp w3, #32\n   b.lo 12f\n   cmp w3, #127\n   b.ne 20b\n   b 12f\n"
        "22: ldrb w3, [x0], #1\n   cbz w3, 11f\n   cmp w3, #33\n   b.lo 12f\n   cmp w3, #127\n   b.ne 22b\n   b 12f\n"
        "21: mov x2, x0\n"
        "1:  ldrb w3, [x0]\n   cbz w3, 11f\n   sub w6, w3, #33\n   cmp w6, #93\n   b.hi 12f\n"
        "cmp w3, #92\n   b.eq 12f\n   cmp w3, #37\n   b.ne 10f\n"
        "cmp x0, x2\n   b.lo 10f\n   mov x4, x0\n   mov w5, #1\n"
        "2:  ldrb w6, [x4]\n   cmp w6, #37\n   b.ne 3f\n   add x7, x4, #1\n   b 4f\n"
        "3:  cbnz w5, 8f\n   mov x7, x4\n"
        "4:  ldrb w6, [x7]\n   sub w8, w6, #48\n   cmp w8, #9\n   b.ls 5f\n"
        "orr w8, w6, #32\n   sub w8, w8, #97\n   cmp w8, #5\n   b.hi 8f\n   add w8, w8, #10\n"
        "5:  ldrb w6, [x7, #1]\n   sub w9, w6, #48\n   cmp w9, #9\n   b.ls 6f\n"
        "orr w9, w6, #32\n   sub w9, w9, #97\n   cmp w9, #5\n   b.hi 8f\n   add w9, w9, #10\n"
        "6:  orr w8, w9, w8, lsl #4\n   cbz w8, 12f\n   cmp w8, #13\n   b.eq 12f\n   cmp w8, #10\n   b.eq 12f\n"
        "add x4, x7, #2\n   cmp w8, #37\n   b.ne 8f\n   mov w5, #0\n   b 2b\n"
        "8:  mov x2, x4\n"
        "10: add x0, x0, #1\n   b 1b\n"
        "11: mov w0, #1\n" HP_ASM_RET
        "12: mov w0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_request_component_valid)

        /* http_parse_ipv4_spelling(x0=host, x1=len, x2=ip_out) -> 1 valid, 0 invalid */
        HP_ASM_FUNC(http_parse_ipv4_spelling)
        "cbz x0, 12f\n   cbz x1, 12f\n   mov x3, #0\n   mov x4, #0\n   mov x5, #0\n"
        "1:  mov x6, #10\n   mov x7, #0\n   mov x8, #0\n   cmp x3, x1\n   b.hs 12f\n"
        "ldrb w9, [x0, x3]\n   cmp w9, #48\n   b.ne 3f\n   add x10, x3, #1\n   cmp x10, x1\n   b.hs 3f\n"
        "ldrb w11, [x0, x10]\n   orr w12, w11, #32\n   cmp w12, #120\n   b.ne 2f\n   mov x6, #16\n   add x3, x3, #2\n   b 3f\n"
        "2:  sub w12, w11, #48\n   cmp w12, #9\n   b.hi 3f\n   mov x6, #8\n   add x3, x3, #1\n"
        "3:  cmp x3, x1\n   b.hs 6f\n   ldrb w9, [x0, x3]\n   sub w10, w9, #48\n   cmp w10, #9\n   b.ls 4f\n"
        "orr w10, w9, #32\n   sub w10, w10, #97\n   cmp w10, #5\n   b.hi 6f\n   add w10, w10, #10\n"
        "4:  cmp x10, x6\n   b.hs 6f\n   madd x8, x8, x6, x10\n   lsr x11, x8, #32\n   cbnz x11, 12f\n"
        "add x3, x3, #1\n   add x7, x7, #1\n   b 3b\n"
        "6:  cbz x7, 12f\n   add x5, x5, #1\n   cmp x3, x1\n   b.lo 7f\n"
        "mov x10, #5\n   sub x10, x10, x5\n   lsl x10, x10, #3\n   lsr x11, x8, x10\n   cbnz x11, 12f\n"
        "lsl x4, x4, x10\n   orr x4, x4, x8\n   cbz x2, 11f\n   str w4, [x2]\n"
        "11: mov w0, #1\n" HP_ASM_RET
        "7:  cmp x5, #4\n   b.eq 12f\n   cmp x8, #255\n   b.hi 12f\n"
        "ldrb w9, [x0, x3]\n   cmp w9, #46\n   b.ne 12f\n   add x3, x3, #1\n   orr x4, x8, x4, lsl #8\n   b 1b\n"
        "12: mov w0, #0\n" HP_ASM_RET
        HP_ASM_END(http_parse_ipv4_spelling)

        /* http_parse_dot_segment(x0=seg, x1=len) -> 1 for '.', 2 for '..', else 0 */
        HP_ASM_FUNC(http_parse_dot_segment)
        "cbz x0, 5f\n   cbz x1, 5f\n   mov x2, #0\n   mov x3, #0\n"
        "1:  cmp x2, x1\n   b.hs 4f\n   ldrb w4, [x0, x2]\n   cmp w4, #46\n   b.ne 2f\n   add x2, x2, #1\n   b 3f\n"
        "2:  cmp w4, #37\n   b.ne 5f\n   sub x5, x1, x2\n   cmp x5, #3\n   b.lo 5f\n"
        "add x6, x0, x2\n   ldrb w4, [x6, #1]\n   cmp w4, #50\n   b.ne 5f\n"
        "ldrb w4, [x6, #2]\n   orr w4, w4, #32\n   cmp w4, #101\n   b.ne 5f\n   add x2, x2, #3\n"
        "3:  add x3, x3, #1\n   cmp x3, #2\n   b.ls 1b\n"
        "5:  mov x0, #0\n" HP_ASM_RET
        "4:  mov x0, x3\n" HP_ASM_RET
        HP_ASM_END(http_parse_dot_segment)

        /* http_parse_path_simplify(x0=path) -> in-place RFC 3986 5.2.4 */
        HP_ASM_FUNC(http_parse_path_simplify)
        "cbz x0, 18f\n   ldrb w1, [x0]\n   cmp w1, #47\n   b.ne 18f\n   mov x1, #0\n"
        "1:  ldrb w2, [x0, x1]\n   cbz w2, 2f\n   cmp w2, #63\n   b.eq 2f\n   cmp w2, #35\n   b.eq 2f\n   add x1, x1, #1\n   b 1b\n"
        "2:  mov x2, #0\n   mov x3, #0\n"
        "3:  cmp x2, x1\n   b.hs 15f\n   add x4, x2, #1\n   mov x5, x4\n"
        "4:  cmp x5, x1\n   b.hs 5f\n   ldrb w6, [x0, x5]\n   cmp w6, #47\n   b.eq 5f\n   add x5, x5, #1\n   b 4b\n"
        "5:  mov x6, x4\n   mov x7, #0\n"
        "6:  cmp x6, x5\n   b.hs 9f\n   ldrb w8, [x0, x6]\n   cmp w8, #46\n   b.ne 7f\n   add x6, x6, #1\n   b 8f\n"
        "7:  cmp w8, #37\n   b.ne 10f\n   sub x9, x5, x6\n   cmp x9, #3\n   b.lo 10f\n"
        "add x10, x0, x6\n   ldrb w8, [x10, #1]\n   cmp w8, #50\n   b.ne 10f\n"
        "ldrb w8, [x10, #2]\n   orr w8, w8, #32\n   cmp w8, #101\n   b.ne 10f\n   add x6, x6, #3\n"
        "8:  add x7, x7, #1\n   cmp x7, #2\n   b.ls 6b\n   b 10f\n"
        "9:  cbnz x7, 12f\n"
        "10: cmp x3, x2\n   b.eq 11f\n   mov x8, x2\n   mov x9, x3\n"
        "20: cmp x8, x5\n   b.hs 11f\n   ldrb w10, [x0, x8]\n   strb w10, [x0, x9]\n   add x8, x8, #1\n   add x9, x9, #1\n   b 20b\n"
        "11: sub x8, x5, x2\n   add x3, x3, x8\n   mov x2, x5\n   b 3b\n"
        "12: cmp x7, #2\n   b.ne 14f\n"
        "13: cbz x3, 14f\n   sub x3, x3, #1\n   ldrb w8, [x0, x3]\n   cmp w8, #47\n   b.ne 13b\n"
        "14: cmp x5, x1\n   b.ne 21f\n   mov w8, #47\n   strb w8, [x0, x3]\n   add x3, x3, #1\n"
        "21: mov x2, x5\n   b 3b\n"
        "15: cmp x3, x1\n   b.eq 18f\n"
        "16: ldrb w6, [x0, x1]\n   strb w6, [x0, x3]\n   add x1, x1, #1\n   add x3, x3, #1\n   cbnz w6, 16b\n"
        "18: " HP_ASM_RET
        HP_ASM_END(http_parse_path_simplify)
);

#elif defined(HP_X64)

#if defined(_WIN32)
# define HP_LD1  "mov r8, rcx\n"
# define HP_LD1D "mov eax, ecx\n"
# define HP_LD2  "mov r8, rcx\n   mov r9, rdx\n"
# define HP_LD3  "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define HP_LD4  "mov r10d, r8d\n   mov r11d, r9d\n   mov r8, rcx\n   mov r9, rdx\n"
# define HP_W64_PUSH "push rsi\n   push rdi\n"
# define HP_W64_POP  "pop rdi\n   pop rsi\n"
# define HP_HDR5 "mov r11, [rsp + 40]\n   push rbx\n   push rsi\n   push rdi\n   push r12\n   push r13\n   push r14\n   push r15\n   mov r12, rcx\n   mov r13, rdx\n   mov r14, r8\n   mov r15, r9\n   mov rbx, r11\n"
# define HP_HDR_POP "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbx\n"
#else
# define HP_LD1  "mov r8, rdi\n"
# define HP_LD1D "mov eax, edi\n"
# define HP_LD2  "mov r8, rdi\n   mov r9, rsi\n"
# define HP_LD3  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define HP_LD4  "mov r8, rdi\n   mov r9, rsi\n   mov r10d, edx\n   mov r11d, ecx\n"
# define HP_W64_PUSH ""
# define HP_W64_POP  ""
# define HP_HDR5 "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n   mov r12, rdi\n   mov r13, rsi\n   mov r14, rdx\n   mov r15, rcx\n   mov rbx, r8\n"
# define HP_HDR_POP "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n"
#endif

__asm__(
        HP_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* http_parse_host_byte(byte) */
        HP_ASM_FUNC(http_parse_host_byte)
        HP_LD1D
        "movzx eax, al\n   mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 1f\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 1f\n   lea ecx, [rax - 45]\n   cmp ecx, 1\n   jbe 1f\n"
        "cmp eax, 95\n   sete al\n   movzx eax, al\n" HP_ASM_RET
        "1:  mov eax, 1\n" HP_ASM_RET
        HP_ASM_END(http_parse_host_byte)

        /* http_parse_token_byte(byte) */
        HP_ASM_FUNC(http_parse_token_byte)
        HP_LD1D
        "movzx eax, al\n   cmp eax, 128\n   jae 2f\n   cmp eax, 64\n   jae 1f\n"
        "mov rdx, 0x03ff6cfa00000000\n   bt rdx, rax\n   setc al\n   movzx eax, al\n" HP_ASM_RET
        "1:  mov rdx, 0x57ffffffc7fffffe\n   bt rdx, rax\n   setc al\n   movzx eax, al\n" HP_ASM_RET
        "2:  xor eax, eax\n" HP_ASM_RET
        HP_ASM_END(http_parse_token_byte)

        /* http_parse_scheme_length(url) */
        HP_ASM_FUNC(http_parse_scheme_length)
        HP_LD1
        "test r8, r8\n   jz 4f\n   movzx eax, byte ptr [r8]\n   mov ecx, eax\n   or ecx, 32\n"
        "sub ecx, 97\n   cmp ecx, 25\n   ja 4f\n   mov r9, 1\n"
        "1:  movzx eax, byte ptr [r8 + r9]\n   mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 2f\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 2f\n   lea ecx, [rax - 45]\n   cmp ecx, 1\n   jbe 2f\n"
        "cmp eax, 43\n   jne 3f\n"
        "2:  inc r9\n   jmp 1b\n"
        "3:  cmp eax, 58\n   jne 4f\n   lea rax, [r9 + 1]\n" HP_ASM_RET
        "4:  xor eax, eax\n" HP_ASM_RET
        HP_ASM_END(http_parse_scheme_length)

        /* http_parse_header_end(bytes, size) */
        HP_ASM_FUNC(http_parse_header_end)
        HP_LD2
        "test r8, r8\n   jz 4f\n   cmp r9, 2\n   jb 4f\n   lea r10, [r9 - 1]\n   xor ecx, ecx\n"
        "1:  cmp rcx, r10\n   jae 4f\n   movzx eax, byte ptr [r8 + rcx]\n   inc rcx\n   cmp eax, 10\n   jne 1b\n"
        "movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 10\n   je 2f\n   cmp eax, 13\n   jne 1b\n"
        "lea r11, [rcx + 1]\n   cmp r11, r9\n   jae 1b\n   cmp byte ptr [r8 + r11], 10\n   jne 1b\n"
        "lea rax, [r11 + 1]\n" HP_ASM_RET
        "2:  lea rax, [rcx + 1]\n" HP_ASM_RET
        "4:  mov rax, -1\n" HP_ASM_RET
        HP_ASM_END(http_parse_header_end)

        /* http_parse_field_name(line, stop) */
        HP_ASM_FUNC(http_parse_field_name)
        HP_LD2
        "test r8, r8\n   jz 6f\n   test r9, r9\n   jz 6f\n"
        "mov r10, 0x03ff6cfa00000000\n   mov r11, 0x57ffffffc7fffffe\n   xor ecx, ecx\n"
        "1:  cmp rcx, r9\n   jae 6f\n   movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 128\n   jae 6f\n"
        "mov rdx, r10\n   cmp eax, 64\n   cmovae rdx, r11\n   bt rdx, rax\n   jnc 2f\n   inc rcx\n   jmp 1b\n"
        "2:  test rcx, rcx\n   jz 6f\n   cmp eax, 58\n   jne 6f\n   lea rdx, [rcx + 1]\n"
        "3:  cmp rdx, r9\n   jae 5f\n   movzx eax, byte ptr [r8 + rdx]\n   inc rdx\n"
        "cmp eax, 9\n   je 3b\n   cmp eax, 32\n   jb 6f\n   cmp eax, 127\n   jne 3b\n"
        "6:  mov rax, -1\n" HP_ASM_RET
        "5:  mov rax, rcx\n" HP_ASM_RET
        HP_ASM_END(http_parse_field_name)

        /* http_parse_line_valid(line, stop, field, tight) */
        HP_ASM_FUNC(http_parse_line_valid)
        HP_LD4
        "test r10d, r10d\n   jnz 4f\n   test r9, r9\n   jz 8f\n   test r8, r8\n   jz 9f\n   xor ecx, ecx\n"
        "test r11d, r11d\n   jnz 2f\n"
        "1:  cmp rcx, r9\n   jae 8f\n   movzx eax, byte ptr [r8 + rcx]\n   inc rcx\n"
        "test eax, eax\n   jz 9f\n   cmp eax, 13\n   jne 1b\n   jmp 9f\n"
        "2:  cmp rcx, r9\n   jae 8f\n   movzx eax, byte ptr [r8 + rcx]\n   inc rcx\n"
        "cmp eax, 9\n   je 2b\n   cmp eax, 32\n   jb 9f\n   cmp eax, 127\n   jne 2b\n   jmp 9f\n"
        "4:  test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n"
        "mov r10, 0x03ff6cfa00000000\n   mov r11, 0x57ffffffc7fffffe\n   xor ecx, ecx\n"
        "5:  cmp rcx, r9\n   jae 9f\n   movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 128\n   jae 9f\n"
        "mov rdx, r10\n   cmp eax, 64\n   cmovae rdx, r11\n   bt rdx, rax\n   jnc 6f\n   inc rcx\n   jmp 5b\n"
        "6:  test rcx, rcx\n   jz 9f\n   cmp eax, 58\n   jne 9f\n   inc rcx\n   jmp 2b\n"
        "8:  mov eax, 1\n" HP_ASM_RET
        "9:  xor eax, eax\n" HP_ASM_RET
        HP_ASM_END(http_parse_line_valid)

        /* http_parse_header(bytes, size, name, length_out, repeated_out) */
        HP_ASM_FUNC(http_parse_header)
        HP_HDR5
        "test rbx, rbx\n   jz 1f\n   mov byte ptr [rbx], 0\n"
        "1:  test r12, r12\n   jz 14f\n   test r14, r14\n   jz 14f\n   xor r8d, r8d\n"
        "2:  cmp byte ptr [r14 + r8], 0\n   je 3f\n   inc r8\n   jmp 2b\n"
        "3:  xor r9d, r9d\n   xor r10d, r10d\n   xor r11d, r11d\n"
        "4:  cmp r9, r13\n   jae 12f\n   mov rcx, r9\n   mov rdx, r9\n"
        "5:  cmp rdx, r13\n   jae 6f\n   cmp byte ptr [r12 + rdx], 10\n   je 6f\n   inc rdx\n   jmp 5b\n"
        "6:  mov r9, rdx\n   cmp rdx, r13\n   adc r9, 0\n   cmp rdx, rcx\n   jbe 7f\n"
        "cmp byte ptr [r12 + rdx - 1], 13\n   jne 7f\n   dec rdx\n"
        "7:  mov rax, rdx\n   sub rax, rcx\n   cmp rax, r8\n   jbe 4b\n"
        "lea rsi, [rcx + r8]\n   cmp byte ptr [r12 + rsi], 58\n   jne 4b\n   xor edi, edi\n"
        "8:  cmp rdi, r8\n   jae 9f\n   lea rax, [rcx + rdi]\n   movzx eax, byte ptr [r12 + rax]\n"
        "movzx r11d, byte ptr [r14 + rdi]\n   cmp eax, r11d\n   je 15f\n"
        "xor r11d, eax\n   cmp r11d, 32\n   jne 4b\n   or eax, 32\n   sub eax, 97\n   cmp eax, 25\n   ja 4b\n"
        "15: inc rdi\n   jmp 8b\n"
        "9:  test r10, r10\n   jz 10f\n   test rbx, rbx\n   jz 4b\n   mov byte ptr [rbx], 1\n   jmp 4b\n"
        "10: inc rsi\n"
        "11: cmp rsi, rdx\n   jae 16f\n   movzx eax, byte ptr [r12 + rsi]\n   cmp eax, 32\n   je 17f\n   cmp eax, 9\n   jne 16f\n"
        "17: inc rsi\n   jmp 11b\n"
        "16: cmp rdx, rsi\n   jbe 18f\n   movzx eax, byte ptr [r12 + rdx - 1]\n   cmp eax, 32\n   je 19f\n   cmp eax, 9\n   jne 18f\n"
        "19: dec rdx\n   jmp 16b\n"
        "18: lea r10, [r12 + rsi]\n   mov rax, rdx\n   sub rax, rsi\n   test r15, r15\n   jz 4b\n   mov [r15], rax\n   jmp 4b\n"
        "12: mov rax, r10\n   " HP_HDR_POP HP_ASM_RET
        "14: xor eax, eax\n   " HP_HDR_POP HP_ASM_RET
        HP_ASM_END(http_parse_header)

        /* http_parse_chunk_extensions_valid(at, stop) */
        HP_ASM_FUNC(http_parse_chunk_extensions_valid)
        HP_LD2
        "cmp r8, r9\n   jae 11f\n   test r8, r8\n   jz 12f\n   test r9, r9\n   jz 12f\n"
        "mov r10, 0x03ff6cfa00000000\n   mov r11, 0x57ffffffc7fffffe\n"
        "1:  cmp r8, r9\n   jae 11f\n   cmp byte ptr [r8], 59\n   jne 12f\n   inc r8\n   mov rcx, r8\n"
        "2:  cmp r8, r9\n   jae 3f\n   movzx eax, byte ptr [r8]\n   cmp eax, 128\n   jae 3f\n"
        "mov rdx, r10\n   cmp eax, 64\n   cmovae rdx, r11\n   bt rdx, rax\n   jnc 3f\n   inc r8\n   jmp 2b\n"
        "3:  cmp r8, rcx\n   je 12f\n   cmp r8, r9\n   jae 11f\n   cmp byte ptr [r8], 61\n   jne 1b\n"
        "inc r8\n   cmp r8, r9\n   jae 12f\n   cmp byte ptr [r8], 34\n   jne 8f\n   inc r8\n"
        "4:  cmp r8, r9\n   jae 12f\n   movzx eax, byte ptr [r8]\n   cmp eax, 34\n   je 7f\n"
        "cmp eax, 92\n   jne 5f\n   inc r8\n   cmp r8, r9\n   jae 12f\n   movzx eax, byte ptr [r8]\n"
        "5:  cmp eax, 9\n   je 6f\n   cmp eax, 32\n   jb 12f\n   cmp eax, 127\n   je 12f\n"
        "6:  inc r8\n   jmp 4b\n"
        "7:  inc r8\n   jmp 1b\n"
        "8:  mov rcx, r8\n"
        "9:  cmp r8, r9\n   jae 10f\n   movzx eax, byte ptr [r8]\n   cmp eax, 128\n   jae 10f\n"
        "mov rdx, r10\n   cmp eax, 64\n   cmovae rdx, r11\n   bt rdx, rax\n   jnc 10f\n   inc r8\n   jmp 9b\n"
        "10: cmp r8, rcx\n   jne 1b\n"
        "12: xor eax, eax\n" HP_ASM_RET
        "11: mov eax, 1\n" HP_ASM_RET
        HP_ASM_END(http_parse_chunk_extensions_valid)

        /* http_parse_chunk_line(line, len, chunk_len_out) */
        HP_ASM_FUNC(http_parse_chunk_line)
        HP_LD3
        "push rbx\n   test r8, r8\n   jz 15f\n   test r9, r9\n   jz 15f\n"
        "add r9, r8\n   dec r9\n   cmp byte ptr [r9], 10\n   jne 15f\n"
        "cmp r9, r8\n   jbe 1f\n   cmp byte ptr [r9 - 1], 13\n   jne 1f\n   dec r9\n"
        "1:  xor ebx, ebx\n   xor r11d, r11d\n"
        "2:  cmp r8, r9\n   jae 4f\n   movzx eax, byte ptr [r8]\n   lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 3f\n"
        "or eax, 32\n   lea ecx, [rax - 97]\n   cmp ecx, 5\n   ja 4f\n   add ecx, 10\n"
        "3:  mov rax, rbx\n   shr rax, 60\n   jnz 15f\n   shl rbx, 4\n   or rbx, rcx\n   inc r8\n   mov r11d, 1\n   jmp 2b\n"
        "4:  test r11d, r11d\n   jz 15f\n"
        "5:  cmp r8, r9\n   jae 14f\n   movzx eax, byte ptr [r8]\n   cmp eax, 32\n   je 6f\n   cmp eax, 9\n   jne 7f\n"
        "6:  inc r8\n   jmp 5b\n"
        "7:  push r12\n   mov r11, 0x03ff6cfa00000000\n   mov r12, 0x57ffffffc7fffffe\n"
        "8:  cmp r8, r9\n   jae 13f\n   cmp byte ptr [r8], 59\n   jne 16f\n   inc r8\n   mov rcx, r8\n"
        "9:  cmp r8, r9\n   jae 10f\n   movzx eax, byte ptr [r8]\n   cmp eax, 128\n   jae 10f\n"
        "mov rdx, r11\n   cmp eax, 64\n   cmovae rdx, r12\n   bt rdx, rax\n   jnc 10f\n   inc r8\n   jmp 9b\n"
        "10: cmp r8, rcx\n   je 16f\n   cmp r8, r9\n   jae 13f\n   cmp byte ptr [r8], 61\n   jne 8b\n"
        "inc r8\n   cmp r8, r9\n   jae 16f\n   cmp byte ptr [r8], 34\n   jne 12f\n   inc r8\n"
        "11: cmp r8, r9\n   jae 16f\n   movzx eax, byte ptr [r8]\n   cmp eax, 34\n   je 17f\n"
        "cmp eax, 92\n   jne 18f\n   inc r8\n   cmp r8, r9\n   jae 16f\n   movzx eax, byte ptr [r8]\n"
        "18: cmp eax, 9\n   je 19f\n   cmp eax, 32\n   jb 16f\n   cmp eax, 127\n   je 16f\n"
        "19: inc r8\n   jmp 11b\n"
        "17: inc r8\n   jmp 8b\n"
        "12: mov rcx, r8\n"
        "20: cmp r8, r9\n   jae 21f\n   movzx eax, byte ptr [r8]\n   cmp eax, 128\n   jae 21f\n"
        "mov rdx, r11\n   cmp eax, 64\n   cmovae rdx, r12\n   bt rdx, rax\n   jnc 21f\n   inc r8\n   jmp 20b\n"
        "21: cmp r8, rcx\n   jne 8b\n"
        "16: pop r12\n"
        "15: mov rax, -5\n   pop rbx\n" HP_ASM_RET
        "13: pop r12\n"
        "14: test r10, r10\n   jz 22f\n   mov [r10], rbx\n"
        "22: xor eax, eax\n   pop rbx\n" HP_ASM_RET
        HP_ASM_END(http_parse_chunk_line)

        /* http_parse_trailer_line(line, len) */
        HP_ASM_FUNC(http_parse_trailer_line)
        HP_LD2
        "test r8, r8\n   jz 6f\n   test r9, r9\n   jz 6f\n   dec r9\n   cmp byte ptr [r8 + r9], 10\n   jne 6f\n"
        "test r9, r9\n   jz 7f\n   cmp byte ptr [r8 + r9 - 1], 13\n   jne 8f\n   dec r9\n   jz 7f\n"
        "8:  mov r10, 0x03ff6cfa00000000\n   mov r11, 0x57ffffffc7fffffe\n   xor ecx, ecx\n"
        "1:  cmp rcx, r9\n   jae 6f\n   movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 128\n   jae 6f\n"
        "mov rdx, r10\n   cmp eax, 64\n   cmovae rdx, r11\n   bt rdx, rax\n   jnc 2f\n   inc rcx\n   jmp 1b\n"
        "2:  test rcx, rcx\n   jz 6f\n   cmp eax, 58\n   jne 6f\n   inc rcx\n"
        "3:  cmp rcx, r9\n   jae 5f\n   movzx eax, byte ptr [r8 + rcx]\n   inc rcx\n"
        "cmp eax, 9\n   je 3b\n   cmp eax, 32\n   jb 6f\n   cmp eax, 127\n   jne 3b\n"
        "6:  mov rax, -5\n" HP_ASM_RET
        "7:  mov eax, 1\n" HP_ASM_RET
        "5:  xor eax, eax\n" HP_ASM_RET
        HP_ASM_END(http_parse_trailer_line)

        /* http_parse_status_code(bytes, size, code_out) */
        HP_ASM_FUNC(http_parse_status_code)
        HP_LD3
        "test r8, r8\n   jz 1f\n   cmp r9, 13\n   jb 1f\n"
        "mov rax, [r8]\n   mov rcx, 0x00ffffffffffffff\n   mov rdx, rax\n   and rdx, rcx\n"
        "mov r11, 0x002e312f50545448\n   cmp rdx, r11\n   jne 2f\n"
        "shr rax, 56\n   sub eax, 48\n   cmp eax, 9\n   ja 2f\n   cmp byte ptr [r8 + 8], 32\n   jne 2f\n"
        "movzx eax, byte ptr [r8 + 9]\n   sub eax, 48\n   cmp eax, 9\n   ja 2f\n"
        "movzx ecx, byte ptr [r8 + 10]\n   sub ecx, 48\n   cmp ecx, 9\n   ja 2f\n"
        "movzx edx, byte ptr [r8 + 11]\n   sub edx, 48\n   cmp edx, 9\n   ja 2f\n"
        "movzx r11d, byte ptr [r8 + 12]\n   cmp r11d, 32\n   je 3f\n   cmp r11d, 13\n   je 3f\n   cmp r11d, 10\n   jne 2f\n"
        "3:  test r10, r10\n   jz 4f\n   imul eax, eax, 100\n   imul ecx, ecx, 10\n   add eax, ecx\n   add eax, edx\n   mov [r10], eax\n"
        "4:  xor eax, eax\n" HP_ASM_RET
        "1:  mov rax, -4\n" HP_ASM_RET
        "2:  mov rax, -5\n" HP_ASM_RET
        HP_ASM_END(http_parse_status_code)

        /* http_parse_request_component_valid(text, kind) */
        HP_ASM_FUNC(http_parse_request_component_valid)
        HP_LD2 HP_W64_PUSH
        "test r8, r8\n   jz 12f\n   mov r10, r8\n   cmp r9d, 2\n   jne 1f\n   cmp byte ptr [r8], 0\n   je 12f\n"
        "1:  movzx eax, byte ptr [r8]\n   test eax, eax\n   jz 11f\n   cmp eax, 32\n   jb 12f\n   cmp eax, 127\n   je 12f\n"
        "test r9d, r9d\n   jz 10f\n   cmp eax, 32\n   je 12f\n   cmp r9d, 1\n   jne 9f\n"
        "cmp eax, 92\n   je 12f\n   cmp eax, 127\n   ja 12f\n   cmp eax, 37\n   jne 10f\n"
        "cmp r8, r10\n   jb 10f\n   mov rcx, r8\n   mov r11d, 1\n"
        "2:  cmp byte ptr [rcx], 37\n   jne 3f\n   lea rdx, [rcx + 1]\n   jmp 4f\n"
        "3:  test r11d, r11d\n   jnz 8f\n   mov rdx, rcx\n"
        "4:  movzx eax, byte ptr [rdx]\n   lea esi, [rax - 48]\n   cmp esi, 9\n   jbe 5f\n"
        "or eax, 32\n   lea esi, [rax - 97]\n   cmp esi, 5\n   ja 8f\n   add esi, 10\n"
        "5:  movzx eax, byte ptr [rdx + 1]\n   lea edi, [rax - 48]\n   cmp edi, 9\n   jbe 6f\n"
        "or eax, 32\n   lea edi, [rax - 97]\n   cmp edi, 5\n   ja 8f\n   add edi, 10\n"
        "6:  shl esi, 4\n   or esi, edi\n   test esi, esi\n   jz 12f\n   cmp esi, 13\n   je 12f\n   cmp esi, 10\n   je 12f\n"
        "lea rcx, [rdx + 2]\n   cmp esi, 37\n   jne 8f\n   xor r11d, r11d\n   jmp 2b\n"
        "8:  mov r10, rcx\n   jmp 10f\n"
        "9:  cmp r9d, 2\n   jne 10f\n   mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 10f\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 10f\n   lea ecx, [rax - 45]\n   cmp ecx, 1\n   jbe 10f\n"
        "cmp eax, 95\n   jne 12f\n"
        "10: inc r8\n   jmp 1b\n"
        "11: mov eax, 1\n   " HP_W64_POP HP_ASM_RET
        "12: xor eax, eax\n   " HP_W64_POP HP_ASM_RET
        HP_ASM_END(http_parse_request_component_valid)

        /* http_parse_ipv4_spelling(host, len, ip_out) */
        HP_ASM_FUNC(http_parse_ipv4_spelling)
        HP_LD3 HP_W64_PUSH
        "push rbx\n   push r12\n   test r8, r8\n   jz 12f\n   test r9, r9\n   jz 12f\n"
        "xor ecx, ecx\n   xor r11d, r11d\n   xor r12d, r12d\n"
        "1:  mov rbx, 10\n   xor esi, esi\n   xor edx, edx\n   cmp rcx, r9\n   jae 12f\n"
        "movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 48\n   jne 3f\n   lea rdi, [rcx + 1]\n   cmp rdi, r9\n   jae 3f\n"
        "movzx eax, byte ptr [r8 + rdi]\n   mov edi, eax\n   or edi, 32\n   cmp edi, 120\n   jne 2f\n   mov rbx, 16\n   add rcx, 2\n   jmp 3f\n"
        "2:  sub eax, 48\n   cmp eax, 9\n   ja 3f\n   mov rbx, 8\n   inc rcx\n"
        "3:  cmp rcx, r9\n   jae 6f\n   movzx eax, byte ptr [r8 + rcx]\n   lea edi, [rax - 48]\n   cmp edi, 9\n   jbe 4f\n"
        "or eax, 32\n   lea edi, [rax - 97]\n   cmp edi, 5\n   ja 6f\n   add edi, 10\n"
        "4:  cmp rdi, rbx\n   jae 6f\n   imul rdx, rbx\n   add rdx, rdi\n   mov rax, rdx\n   shr rax, 32\n   jnz 12f\n"
        "inc rcx\n   inc rsi\n   jmp 3b\n"
        "6:  test rsi, rsi\n   jz 12f\n   inc r12\n   cmp rcx, r9\n   jb 7f\n"
        "mov ecx, 5\n   sub ecx, r12d\n   shl ecx, 3\n   mov rax, rdx\n   shr rax, cl\n   jnz 12f\n"
        "shl r11, cl\n   or r11, rdx\n   test r10, r10\n   jz 11f\n   mov [r10], r11d\n"
        "11: mov eax, 1\n   pop r12\n   pop rbx\n   " HP_W64_POP HP_ASM_RET
        "7:  cmp r12, 4\n   je 12f\n   cmp rdx, 255\n   ja 12f\n"
        "cmp byte ptr [r8 + rcx], 46\n   jne 12f\n   inc rcx\n   shl r11, 8\n   or r11, rdx\n   jmp 1b\n"
        "12: xor eax, eax\n   pop r12\n   pop rbx\n   " HP_W64_POP HP_ASM_RET
        HP_ASM_END(http_parse_ipv4_spelling)

        /* http_parse_dot_segment(seg, len) */
        HP_ASM_FUNC(http_parse_dot_segment)
        HP_LD2
        "test r8, r8\n   jz 5f\n   test r9, r9\n   jz 5f\n   xor ecx, ecx\n   xor r10d, r10d\n"
        "1:  cmp rcx, r9\n   jae 4f\n   movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 46\n   jne 2f\n   inc rcx\n   jmp 3f\n"
        "2:  cmp eax, 37\n   jne 5f\n   mov rdx, r9\n   sub rdx, rcx\n   cmp rdx, 3\n   jb 5f\n"
        "cmp byte ptr [r8 + rcx + 1], 50\n   jne 5f\n   movzx eax, byte ptr [r8 + rcx + 2]\n   or eax, 32\n   cmp eax, 101\n   jne 5f\n   add rcx, 3\n"
        "3:  inc r10\n   cmp r10, 2\n   jbe 1b\n"
        "5:  xor eax, eax\n" HP_ASM_RET
        "4:  mov rax, r10\n" HP_ASM_RET
        HP_ASM_END(http_parse_dot_segment)

        /* http_parse_path_simplify(path) */
        HP_ASM_FUNC(http_parse_path_simplify)
        HP_LD1 HP_W64_PUSH
        "test r8, r8\n   jz 18f\n   cmp byte ptr [r8], 47\n   jne 18f\n   xor r9d, r9d\n"
        "1:  movzx eax, byte ptr [r8 + r9]\n   test eax, eax\n   jz 2f\n   cmp eax, 63\n   je 2f\n   cmp eax, 35\n   je 2f\n   inc r9\n   jmp 1b\n"
        "2:  xor r10d, r10d\n   xor r11d, r11d\n"
        "3:  cmp r10, r9\n   jae 15f\n   lea rcx, [r10 + 1]\n   mov rdx, rcx\n"
        "4:  cmp rdx, r9\n   jae 5f\n   cmp byte ptr [r8 + rdx], 47\n   je 5f\n   inc rdx\n   jmp 4b\n"
        "5:  mov rsi, rcx\n   xor edi, edi\n"
        "6:  cmp rsi, rdx\n   jae 9f\n   movzx eax, byte ptr [r8 + rsi]\n   cmp eax, 46\n   jne 7f\n   inc rsi\n   jmp 8f\n"
        "7:  cmp eax, 37\n   jne 10f\n   mov rax, rdx\n   sub rax, rsi\n   cmp rax, 3\n   jb 10f\n"
        "cmp byte ptr [r8 + rsi + 1], 50\n   jne 10f\n   movzx eax, byte ptr [r8 + rsi + 2]\n   or eax, 32\n   cmp eax, 101\n   jne 10f\n   add rsi, 3\n"
        "8:  inc rdi\n   cmp rdi, 2\n   jbe 6b\n   jmp 10f\n"
        "9:  test rdi, rdi\n   jnz 12f\n"
        "10: cmp r11, r10\n   je 11f\n   mov rsi, r10\n   mov rdi, r11\n"
        "20: cmp rsi, rdx\n   jae 11f\n   mov al, byte ptr [r8 + rsi]\n   mov byte ptr [r8 + rdi], al\n   inc rsi\n   inc rdi\n   jmp 20b\n"
        "11: mov rax, rdx\n   sub rax, r10\n   add r11, rax\n   mov r10, rdx\n   jmp 3b\n"
        "12: cmp rdi, 2\n   jne 14f\n"
        "13: test r11, r11\n   jz 14f\n   dec r11\n   cmp byte ptr [r8 + r11], 47\n   jne 13b\n"
        "14: cmp rdx, r9\n   jne 21f\n   mov byte ptr [r8 + r11], 47\n   inc r11\n"
        "21: mov r10, rdx\n   jmp 3b\n"
        "15: cmp r11, r9\n   je 18f\n"
        "16: mov al, byte ptr [r8 + r9]\n   mov byte ptr [r8 + r11], al\n   inc r9\n   inc r11\n   test al, al\n   jnz 16b\n"
        "18: " HP_W64_POP HP_ASM_RET
        HP_ASM_END(http_parse_path_simplify)

        ".att_syntax prefix\n"
);

#elif defined(HP_RISCV64)

__asm__(
        HP_ASM_SECTION

        /* http_parse_host_byte(a0=byte) */
        HP_ASM_FUNC(http_parse_host_byte)
        "andi a0, a0, 0xff\n   ori t0, a0, 32\n   addi t0, t0, -97\n   li t1, 25\n   bleu t0, t1, 1f\n"
        "addi t0, a0, -48\n   li t1, 9\n   bleu t0, t1, 1f\n   addi t0, a0, -45\n   li t1, 1\n   bleu t0, t1, 1f\n"
        "addi t0, a0, -95\n   seqz a0, t0\n" HP_ASM_RET
        "1:  li a0, 1\n" HP_ASM_RET
        HP_ASM_END(http_parse_host_byte)

        /* http_parse_token_byte(a0=byte) */
        HP_ASM_FUNC(http_parse_token_byte)
        "andi a0, a0, 0xff\n   li t0, 128\n   bgeu a0, t0, 2f\n   li t0, 64\n   bgeu a0, t0, 1f\n"
        "li t1, 0x03ff6cfa00000000\n   srl t1, t1, a0\n   andi a0, t1, 1\n" HP_ASM_RET
        "1:  li t1, 0x57ffffffc7fffffe\n   srl t1, t1, a0\n   andi a0, t1, 1\n" HP_ASM_RET
        "2:  li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_token_byte)

        /* http_parse_scheme_length(a0=url) */
        HP_ASM_FUNC(http_parse_scheme_length)
        "beqz a0, 4f\n   lbu t0, 0(a0)\n   ori t1, t0, 32\n   addi t1, t1, -97\n   li t2, 25\n   bgtu t1, t2, 4f\n"
        "li a1, 1\n   li t3, 9\n   li t4, 1\n   li t5, 43\n"
        "1:  add t6, a0, a1\n   lbu t0, 0(t6)\n   ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t2, 2f\n"
        "addi t1, t0, -48\n   bleu t1, t3, 2f\n   addi t1, t0, -45\n   bleu t1, t4, 2f\n   bne t0, t5, 3f\n"
        "2:  addi a1, a1, 1\n   j 1b\n"
        "3:  li t1, 58\n   bne t0, t1, 4f\n   addi a0, a1, 1\n" HP_ASM_RET
        "4:  li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_scheme_length)

        /* http_parse_header_end(a0=bytes, a1=size) */
        HP_ASM_FUNC(http_parse_header_end)
        "beqz a0, 4f\n   li t0, 2\n   bltu a1, t0, 4f\n   addi a2, a1, -1\n   li a3, 0\n   li t1, 10\n   li t2, 13\n"
        "1:  bgeu a3, a2, 4f\n   add t3, a0, a3\n   lbu t4, 0(t3)\n   addi a3, a3, 1\n   bne t4, t1, 1b\n"
        "add t3, a0, a3\n   lbu t4, 0(t3)\n   beq t4, t1, 2f\n   bne t4, t2, 1b\n"
        "addi a4, a3, 1\n   bgeu a4, a1, 1b\n   add t3, a0, a4\n   lbu t4, 0(t3)\n   bne t4, t1, 1b\n"
        "addi a0, a4, 1\n" HP_ASM_RET
        "2:  addi a0, a3, 1\n" HP_ASM_RET
        "4:  li a0, -1\n" HP_ASM_RET
        HP_ASM_END(http_parse_header_end)

        /* http_parse_field_name(a0=line, a1=stop) */
        HP_ASM_FUNC(http_parse_field_name)
        "beqz a0, 6f\n   beqz a1, 6f\n   li t0, 0x03ff6cfa00000000\n   li t1, 0x57ffffffc7fffffe\n"
        "li a2, 0\n   li t2, 128\n   li t3, 64\n"
        "1:  bgeu a2, a1, 6f\n   add t4, a0, a2\n   lbu t5, 0(t4)\n   bgeu t5, t2, 6f\n"
        "mv t6, t0\n   bltu t5, t3, 7f\n   mv t6, t1\n"
        "7:  srl t6, t6, t5\n   andi t6, t6, 1\n   beqz t6, 2f\n   addi a2, a2, 1\n   j 1b\n"
        "2:  beqz a2, 6f\n   li t4, 58\n   bne t5, t4, 6f\n   addi a3, a2, 1\n   li t2, 9\n   li t3, 32\n   li t4, 127\n"
        "3:  bgeu a3, a1, 5f\n   add t6, a0, a3\n   lbu t5, 0(t6)\n   addi a3, a3, 1\n"
        "beq t5, t2, 3b\n   bltu t5, t3, 6f\n   bne t5, t4, 3b\n"
        "6:  li a0, -1\n" HP_ASM_RET
        "5:  mv a0, a2\n" HP_ASM_RET
        HP_ASM_END(http_parse_field_name)

        /* http_parse_line_valid(a0=line, a1=stop, a2=field, a3=tight) */
        HP_ASM_FUNC(http_parse_line_valid)
        "slli a2, a2, 32\n   bnez a2, 4f\n   beqz a1, 8f\n   beqz a0, 9f\n   li a4, 0\n"
        "slli a3, a3, 32\n   bnez a3, 2f\n   li t0, 13\n"
        "1:  bgeu a4, a1, 8f\n   add t1, a0, a4\n   lbu t2, 0(t1)\n   addi a4, a4, 1\n"
        "beqz t2, 9f\n   bne t2, t0, 1b\n   j 9f\n"
        "2:  li t2, 9\n   li t3, 32\n   li t4, 127\n"
        "3:  bgeu a4, a1, 8f\n   add t1, a0, a4\n   lbu t5, 0(t1)\n   addi a4, a4, 1\n"
        "beq t5, t2, 3b\n   bltu t5, t3, 9f\n   bne t5, t4, 3b\n   j 9f\n"
        "4:  beqz a0, 9f\n   beqz a1, 9f\n   li t0, 0x03ff6cfa00000000\n   li t1, 0x57ffffffc7fffffe\n"
        "li a4, 0\n   li t2, 128\n   li t3, 64\n"
        "5:  bgeu a4, a1, 9f\n   add t4, a0, a4\n   lbu t5, 0(t4)\n   bgeu t5, t2, 9f\n"
        "mv t6, t0\n   bltu t5, t3, 7f\n   mv t6, t1\n"
        "7:  srl t6, t6, t5\n   andi t6, t6, 1\n   beqz t6, 6f\n   addi a4, a4, 1\n   j 5b\n"
        "6:  beqz a4, 9f\n   li t4, 58\n   bne t5, t4, 9f\n   addi a4, a4, 1\n   j 2b\n"
        "8:  li a0, 1\n" HP_ASM_RET
        "9:  li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_line_valid)

        /* http_parse_header(a0=bytes, a1=size, a2=name, a3=length_out, a4=repeated_out) */
        HP_ASM_FUNC(http_parse_header)
        "beqz a4, 1f\n   sb zero, 0(a4)\n"
        "1:  beqz a0, 14f\n   beqz a2, 14f\n   li a5, 0\n"
        "2:  add t0, a2, a5\n   lbu t1, 0(t0)\n   beqz t1, 3f\n   addi a5, a5, 1\n   j 2b\n"
        "3:  li a6, 0\n   li a7, 0\n   li t0, 0\n"
        "4:  bgeu a6, a1, 12f\n   mv t1, a6\n   mv t2, a6\n   li t3, 10\n"
        "5:  bgeu t2, a1, 6f\n   add t4, a0, t2\n   lbu t5, 0(t4)\n   beq t5, t3, 6f\n   addi t2, t2, 1\n   j 5b\n"
        "6:  mv a6, t2\n   bgeu t2, a1, 20f\n   addi a6, t2, 1\n"
        "20: bleu t2, t1, 7f\n   add t4, a0, t2\n   lbu t5, -1(t4)\n   li t3, 13\n   bne t5, t3, 7f\n   addi t2, t2, -1\n"
        "7:  sub t4, t2, t1\n   bleu t4, a5, 4b\n   add t3, t1, a5\n   add t4, a0, t3\n"
        "lbu t5, 0(t4)\n   li t6, 58\n   bne t5, t6, 4b\n   li t4, 0\n"
        "8:  bgeu t4, a5, 9f\n   add t5, t1, t4\n   add t5, a0, t5\n   lbu t5, 0(t5)\n"
        "add t6, a2, t4\n   lbu t6, 0(t6)\n   beq t5, t6, 15f\n"
        "xor t6, t5, t6\n   addi t6, t6, -32\n   bnez t6, 4b\n"
        "ori t5, t5, 32\n   addi t5, t5, -97\n   li t6, 25\n   bgtu t5, t6, 4b\n"
        "15: addi t4, t4, 1\n   j 8b\n"
        "9:  beqz a7, 10f\n   beqz a4, 4b\n   li t4, 1\n   sb t4, 0(a4)\n   j 4b\n"
        "10: addi t3, t3, 1\n   li t5, 32\n   li t6, 9\n"
        "11: bgeu t3, t2, 16f\n   add t4, a0, t3\n   lbu t4, 0(t4)\n   beq t4, t5, 17f\n   bne t4, t6, 16f\n"
        "17: addi t3, t3, 1\n   j 11b\n"
        "16: bleu t2, t3, 18f\n   add t4, a0, t2\n   lbu t4, -1(t4)\n   beq t4, t5, 19f\n   bne t4, t6, 18f\n"
        "19: addi t2, t2, -1\n   j 16b\n"
        "18: add a7, a0, t3\n   sub t0, t2, t3\n   j 4b\n"
        "12: beqz a7, 13f\n   beqz a3, 13f\n   sd t0, 0(a3)\n"
        "13: mv a0, a7\n" HP_ASM_RET
        "14: li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_header)

        /* http_parse_chunk_extensions_valid(a0=at, a1=stop) */
        HP_ASM_FUNC(http_parse_chunk_extensions_valid)
        "bgeu a0, a1, 11f\n   beqz a0, 12f\n   beqz a1, 12f\n"
        "li t0, 0x03ff6cfa00000000\n   li t1, 0x57ffffffc7fffffe\n   li t2, 128\n   li t3, 64\n"
        "1:  bgeu a0, a1, 11f\n   lbu t4, 0(a0)\n   addi a0, a0, 1\n   li t5, 59\n   bne t4, t5, 12f\n   mv a2, a0\n"
        "2:  bgeu a0, a1, 3f\n   lbu t4, 0(a0)\n   bgeu t4, t2, 3f\n   mv t6, t0\n   bltu t4, t3, 13f\n   mv t6, t1\n"
        "13: srl t6, t6, t4\n   andi t6, t6, 1\n   beqz t6, 3f\n   addi a0, a0, 1\n   j 2b\n"
        "3:  beq a0, a2, 12f\n   bgeu a0, a1, 11f\n   lbu t4, 0(a0)\n   li t5, 61\n   bne t4, t5, 1b\n"
        "addi a0, a0, 1\n   bgeu a0, a1, 12f\n   lbu t4, 0(a0)\n   li t5, 34\n   bne t4, t5, 8f\n   addi a0, a0, 1\n"
        "4:  bgeu a0, a1, 12f\n   lbu t4, 0(a0)\n   li t5, 34\n   beq t4, t5, 7f\n"
        "li t5, 92\n   bne t4, t5, 5f\n   addi a0, a0, 1\n   bgeu a0, a1, 12f\n   lbu t4, 0(a0)\n"
        "5:  li t5, 9\n   beq t4, t5, 6f\n   li t5, 32\n   bltu t4, t5, 12f\n   li t5, 127\n   beq t4, t5, 12f\n"
        "6:  addi a0, a0, 1\n   j 4b\n"
        "7:  addi a0, a0, 1\n   j 1b\n"
        "8:  mv a2, a0\n"
        "9:  bgeu a0, a1, 10f\n   lbu t4, 0(a0)\n   bgeu t4, t2, 10f\n   mv t6, t0\n   bltu t4, t3, 14f\n   mv t6, t1\n"
        "14: srl t6, t6, t4\n   andi t6, t6, 1\n   beqz t6, 10f\n   addi a0, a0, 1\n   j 9b\n"
        "10: bne a0, a2, 1b\n"
        "12: li a0, 0\n" HP_ASM_RET
        "11: li a0, 1\n" HP_ASM_RET
        HP_ASM_END(http_parse_chunk_extensions_valid)

        /* http_parse_chunk_line(a0=line, a1=len, a2=chunk_len_out) */
        HP_ASM_FUNC(http_parse_chunk_line)
        "beqz a0, 15f\n   beqz a1, 15f\n   add a1, a0, a1\n   addi a1, a1, -1\n   lbu t0, 0(a1)\n   li t1, 10\n   bne t0, t1, 15f\n"
        "bleu a1, a0, 1f\n   lbu t0, -1(a1)\n   li t1, 13\n   bne t0, t1, 1f\n   addi a1, a1, -1\n"
        "1:  li a3, 0\n   li a4, 0\n   li t2, 9\n   li t3, 5\n"
        "2:  bgeu a0, a1, 4f\n   lbu t0, 0(a0)\n   addi t1, t0, -48\n   bleu t1, t2, 3f\n"
        "ori t1, t0, 32\n   addi t1, t1, -97\n   bgtu t1, t3, 4f\n   addi t1, t1, 10\n"
        "3:  srli t4, a3, 60\n   bnez t4, 15f\n   slli a3, a3, 4\n   or a3, a3, t1\n   addi a0, a0, 1\n   li a4, 1\n   j 2b\n"
        "4:  beqz a4, 15f\n   li t2, 32\n   li t3, 9\n"
        "5:  bgeu a0, a1, 14f\n   lbu t0, 0(a0)\n   beq t0, t2, 6f\n   bne t0, t3, 7f\n"
        "6:  addi a0, a0, 1\n   j 5b\n"
        "7:  li t0, 0x03ff6cfa00000000\n   li t1, 0x57ffffffc7fffffe\n   li t2, 128\n   li t3, 64\n"
        "8:  bgeu a0, a1, 14f\n   lbu t4, 0(a0)\n   addi a0, a0, 1\n   li t5, 59\n   bne t4, t5, 15f\n   mv a5, a0\n"
        "9:  bgeu a0, a1, 10f\n   lbu t4, 0(a0)\n   bgeu t4, t2, 10f\n   mv t6, t0\n   bltu t4, t3, 16f\n   mv t6, t1\n"
        "16: srl t6, t6, t4\n   andi t6, t6, 1\n   beqz t6, 10f\n   addi a0, a0, 1\n   j 9b\n"
        "10: beq a0, a5, 15f\n   bgeu a0, a1, 14f\n   lbu t4, 0(a0)\n   li t5, 61\n   bne t4, t5, 8b\n"
        "addi a0, a0, 1\n   bgeu a0, a1, 15f\n   lbu t4, 0(a0)\n   li t5, 34\n   bne t4, t5, 12f\n   addi a0, a0, 1\n"
        "11: bgeu a0, a1, 15f\n   lbu t4, 0(a0)\n   li t5, 34\n   beq t4, t5, 17f\n"
        "li t5, 92\n   bne t4, t5, 18f\n   addi a0, a0, 1\n   bgeu a0, a1, 15f\n   lbu t4, 0(a0)\n"
        "18: li t5, 9\n   beq t4, t5, 19f\n   li t5, 32\n   bltu t4, t5, 15f\n   li t5, 127\n   beq t4, t5, 15f\n"
        "19: addi a0, a0, 1\n   j 11b\n"
        "17: addi a0, a0, 1\n   j 8b\n"
        "12: mv a5, a0\n"
        "13: bgeu a0, a1, 20f\n   lbu t4, 0(a0)\n   bgeu t4, t2, 20f\n   mv t6, t0\n   bltu t4, t3, 21f\n   mv t6, t1\n"
        "21: srl t6, t6, t4\n   andi t6, t6, 1\n   beqz t6, 20f\n   addi a0, a0, 1\n   j 13b\n"
        "20: bne a0, a5, 8b\n"
        "15: li a0, -5\n" HP_ASM_RET
        "14: beqz a2, 22f\n   sd a3, 0(a2)\n"
        "22: li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_chunk_line)

        /* http_parse_trailer_line(a0=line, a1=len) */
        HP_ASM_FUNC(http_parse_trailer_line)
        "beqz a0, 6f\n   beqz a1, 6f\n   addi a1, a1, -1\n   add t0, a0, a1\n   lbu t1, 0(t0)\n   li t2, 10\n   bne t1, t2, 6f\n"
        "beqz a1, 7f\n   lbu t1, -1(t0)\n   li t2, 13\n   bne t1, t2, 8f\n   addi a1, a1, -1\n   beqz a1, 7f\n"
        "8:  li t0, 0x03ff6cfa00000000\n   li t1, 0x57ffffffc7fffffe\n   li a2, 0\n   li t2, 128\n   li t3, 64\n"
        "1:  bgeu a2, a1, 6f\n   add t4, a0, a2\n   lbu t5, 0(t4)\n   bgeu t5, t2, 6f\n"
        "mv t6, t0\n   bltu t5, t3, 9f\n   mv t6, t1\n"
        "9:  srl t6, t6, t5\n   andi t6, t6, 1\n   beqz t6, 2f\n   addi a2, a2, 1\n   j 1b\n"
        "2:  beqz a2, 6f\n   li t4, 58\n   bne t5, t4, 6f\n   addi a2, a2, 1\n   li t2, 9\n   li t3, 32\n   li t4, 127\n"
        "3:  bgeu a2, a1, 5f\n   add t6, a0, a2\n   lbu t5, 0(t6)\n   addi a2, a2, 1\n"
        "beq t5, t2, 3b\n   bltu t5, t3, 6f\n   bne t5, t4, 3b\n"
        "6:  li a0, -5\n" HP_ASM_RET
        "7:  li a0, 1\n" HP_ASM_RET
        "5:  li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_trailer_line)

        /* http_parse_status_code(a0=bytes, a1=size, a2=code_out) */
        HP_ASM_FUNC(http_parse_status_code)
        "beqz a0, 1f\n   li t0, 13\n   bltu a1, t0, 1f\n"
        "lbu t0, 0(a0)\n   li t1, 72\n   bne t0, t1, 2f\n   lbu t0, 1(a0)\n   li t1, 84\n   bne t0, t1, 2f\n"
        "lbu t0, 2(a0)\n   bne t0, t1, 2f\n   lbu t0, 3(a0)\n   li t1, 80\n   bne t0, t1, 2f\n"
        "lbu t0, 4(a0)\n   li t1, 47\n   bne t0, t1, 2f\n   lbu t0, 5(a0)\n   li t1, 49\n   bne t0, t1, 2f\n"
        "lbu t0, 6(a0)\n   li t1, 46\n   bne t0, t1, 2f\n"
        "lbu t0, 7(a0)\n   addi t0, t0, -48\n   li t6, 9\n   bgtu t0, t6, 2f\n"
        "lbu t0, 8(a0)\n   li t1, 32\n   bne t0, t1, 2f\n"
        "lbu t2, 9(a0)\n   addi t2, t2, -48\n   bgtu t2, t6, 2f\n"
        "lbu t3, 10(a0)\n   addi t3, t3, -48\n   bgtu t3, t6, 2f\n"
        "lbu t4, 11(a0)\n   addi t4, t4, -48\n   bgtu t4, t6, 2f\n"
        "lbu t0, 12(a0)\n   beq t0, t1, 3f\n   li t1, 13\n   beq t0, t1, 3f\n   li t1, 10\n   bne t0, t1, 2f\n"
        "3:  beqz a2, 4f\n   li t0, 100\n   mul t2, t2, t0\n   li t0, 10\n   mul t3, t3, t0\n   add t2, t2, t3\n   add t2, t2, t4\n   sw t2, 0(a2)\n"
        "4:  li a0, 0\n" HP_ASM_RET
        "1:  li a0, -4\n" HP_ASM_RET
        "2:  li a0, -5\n" HP_ASM_RET
        HP_ASM_END(http_parse_status_code)

        /* http_parse_request_component_valid(a0=text, a1=kind) */
        HP_ASM_FUNC(http_parse_request_component_valid)
        "beqz a0, 12f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   mv a2, a0\n   li t0, 2\n   bne a1, t0, 1f\n"
        "lbu t0, 0(a0)\n   beqz t0, 12f\n"
        "1:  lbu t0, 0(a0)\n   beqz t0, 11f\n   li t1, 32\n   bltu t0, t1, 12f\n   li t2, 127\n   beq t0, t2, 12f\n"
        "beqz a1, 10f\n   beq t0, t1, 12f\n   li t1, 1\n   bne a1, t1, 9f\n"
        "li t1, 92\n   beq t0, t1, 12f\n   bgtu t0, t2, 12f\n   li t1, 37\n   bne t0, t1, 10f\n"
        "bltu a0, a2, 10f\n   mv a3, a0\n   li a4, 1\n"
        "2:  lbu t0, 0(a3)\n   li t1, 37\n   bne t0, t1, 3f\n   addi a5, a3, 1\n   j 4f\n"
        "3:  bnez a4, 8f\n   mv a5, a3\n"
        "4:  lbu t0, 0(a5)\n   addi t1, t0, -48\n   li t2, 9\n   bleu t1, t2, 5f\n"
        "ori t1, t0, 32\n   addi t1, t1, -97\n   li t3, 5\n   bgtu t1, t3, 8f\n   addi t1, t1, 10\n"
        "5:  lbu t0, 1(a5)\n   addi t4, t0, -48\n   bleu t4, t2, 6f\n"
        "ori t4, t0, 32\n   addi t4, t4, -97\n   li t3, 5\n   bgtu t4, t3, 8f\n   addi t4, t4, 10\n"
        "6:  slli t1, t1, 4\n   or t1, t1, t4\n   beqz t1, 12f\n   li t2, 13\n   beq t1, t2, 12f\n"
        "li t2, 10\n   beq t1, t2, 12f\n   addi a3, a5, 2\n   li t2, 37\n   bne t1, t2, 8f\n   li a4, 0\n   j 2b\n"
        "8:  mv a2, a3\n   j 10f\n"
        "9:  li t1, 2\n   bne a1, t1, 10f\n   ori t1, t0, 32\n   addi t1, t1, -97\n   li t2, 25\n   bleu t1, t2, 10f\n"
        "addi t1, t0, -48\n   li t2, 9\n   bleu t1, t2, 10f\n   addi t1, t0, -45\n   li t2, 1\n   bleu t1, t2, 10f\n"
        "li t1, 95\n   bne t0, t1, 12f\n"
        "10: addi a0, a0, 1\n   j 1b\n"
        "11: li a0, 1\n" HP_ASM_RET
        "12: li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_request_component_valid)

        /* http_parse_ipv4_spelling(a0=host, a1=len, a2=ip_out) */
        HP_ASM_FUNC(http_parse_ipv4_spelling)
        "beqz a0, 12f\n   beqz a1, 12f\n   li a3, 0\n   li a4, 0\n   li a5, 0\n"
        "1:  li a6, 10\n   li a7, 0\n   li t0, 0\n   bgeu a3, a1, 12f\n"
        "add t1, a0, a3\n   lbu t2, 0(t1)\n   li t3, 48\n   bne t2, t3, 3f\n"
        "addi t4, a3, 1\n   bgeu t4, a1, 3f\n   lbu t2, 1(t1)\n   ori t5, t2, 32\n   li t3, 120\n   bne t5, t3, 2f\n"
        "li a6, 16\n   addi a3, a3, 2\n   j 3f\n"
        "2:  addi t5, t2, -48\n   li t3, 9\n   bgtu t5, t3, 3f\n   li a6, 8\n   addi a3, a3, 1\n"
        "3:  bgeu a3, a1, 6f\n   add t1, a0, a3\n   lbu t2, 0(t1)\n   addi t3, t2, -48\n   li t4, 9\n   bleu t3, t4, 4f\n"
        "ori t3, t2, 32\n   addi t3, t3, -97\n   li t4, 5\n   bgtu t3, t4, 6f\n   addi t3, t3, 10\n"
        "4:  bgeu t3, a6, 6f\n   mul t0, t0, a6\n   add t0, t0, t3\n   srli t4, t0, 32\n   bnez t4, 12f\n"
        "addi a3, a3, 1\n   addi a7, a7, 1\n   j 3b\n"
        "6:  beqz a7, 12f\n   addi a5, a5, 1\n   bltu a3, a1, 7f\n"
        "li t1, 5\n   sub t1, t1, a5\n   slli t1, t1, 3\n   srl t2, t0, t1\n   bnez t2, 12f\n"
        "sll a4, a4, t1\n   or a4, a4, t0\n   beqz a2, 11f\n   sw a4, 0(a2)\n"
        "11: li a0, 1\n" HP_ASM_RET
        "7:  li t1, 4\n   beq a5, t1, 12f\n   li t1, 255\n   bgtu t0, t1, 12f\n"
        "add t1, a0, a3\n   lbu t2, 0(t1)\n   li t3, 46\n   bne t2, t3, 12f\n"
        "addi a3, a3, 1\n   slli a4, a4, 8\n   or a4, a4, t0\n   j 1b\n"
        "12: li a0, 0\n" HP_ASM_RET
        HP_ASM_END(http_parse_ipv4_spelling)

        /* http_parse_dot_segment(a0=seg, a1=len) */
        HP_ASM_FUNC(http_parse_dot_segment)
        "beqz a0, 5f\n   beqz a1, 5f\n   li a2, 0\n   li a3, 0\n   li t2, 46\n   li t3, 37\n   li t4, 2\n"
        "1:  bgeu a2, a1, 4f\n   add t0, a0, a2\n   lbu t1, 0(t0)\n   bne t1, t2, 2f\n   addi a2, a2, 1\n   j 3f\n"
        "2:  bne t1, t3, 5f\n   sub t5, a1, a2\n   li t6, 3\n   bltu t5, t6, 5f\n"
        "lbu t1, 1(t0)\n   li t6, 50\n   bne t1, t6, 5f\n   lbu t1, 2(t0)\n   ori t1, t1, 32\n   li t6, 101\n   bne t1, t6, 5f\n   addi a2, a2, 3\n"
        "3:  addi a3, a3, 1\n   bleu a3, t4, 1b\n"
        "5:  li a0, 0\n" HP_ASM_RET
        "4:  mv a0, a3\n" HP_ASM_RET
        HP_ASM_END(http_parse_dot_segment)

        /* http_parse_path_simplify(a0=path) */
        HP_ASM_FUNC(http_parse_path_simplify)
        "beqz a0, 18f\n   lbu t0, 0(a0)\n   li t1, 47\n   bne t0, t1, 18f\n   li a1, 0\n   li t2, 63\n   li t3, 35\n"
        "1:  add t4, a0, a1\n   lbu t0, 0(t4)\n   beqz t0, 2f\n   beq t0, t2, 2f\n   beq t0, t3, 2f\n   addi a1, a1, 1\n   j 1b\n"
        "2:  li a2, 0\n   li a3, 0\n"
        "3:  bgeu a2, a1, 15f\n   addi a4, a2, 1\n   mv a5, a4\n"
        "4:  bgeu a5, a1, 5f\n   add t4, a0, a5\n   lbu t0, 0(t4)\n   beq t0, t1, 5f\n   addi a5, a5, 1\n   j 4b\n"
        "5:  mv a6, a4\n   li a7, 0\n"
        "6:  bgeu a6, a5, 9f\n   add t4, a0, a6\n   lbu t0, 0(t4)\n   li t2, 46\n   bne t0, t2, 7f\n   addi a6, a6, 1\n   j 8f\n"
        "7:  li t2, 37\n   bne t0, t2, 10f\n   sub t5, a5, a6\n   li t6, 3\n   bltu t5, t6, 10f\n"
        "lbu t0, 1(t4)\n   li t6, 50\n   bne t0, t6, 10f\n   lbu t0, 2(t4)\n   ori t0, t0, 32\n   li t6, 101\n   bne t0, t6, 10f\n   addi a6, a6, 3\n"
        "8:  addi a7, a7, 1\n   li t6, 2\n   bleu a7, t6, 6b\n   j 10f\n"
        "9:  bnez a7, 12f\n"
        "10: beq a3, a2, 11f\n   mv t2, a2\n   mv t3, a3\n"
        "20: bgeu t2, a5, 11f\n   add t4, a0, t2\n   lbu t0, 0(t4)\n   add t5, a0, t3\n   sb t0, 0(t5)\n   addi t2, t2, 1\n   addi t3, t3, 1\n   j 20b\n"
        "11: sub t2, a5, a2\n   add a3, a3, t2\n   mv a2, a5\n   j 3b\n"
        "12: li t6, 2\n   bne a7, t6, 14f\n"
        "13: beqz a3, 14f\n   addi a3, a3, -1\n   add t4, a0, a3\n   lbu t0, 0(t4)\n   bne t0, t1, 13b\n"
        "14: bne a5, a1, 21f\n   add t4, a0, a3\n   sb t1, 0(t4)\n   addi a3, a3, 1\n"
        "21: mv a2, a5\n   j 3b\n"
        "15: beq a3, a1, 18f\n"
        "16: add t4, a0, a1\n   lbu t0, 0(t4)\n   add t5, a0, a3\n   sb t0, 0(t5)\n   addi a1, a1, 1\n   addi a3, a3, 1\n   bnez t0, 16b\n"
        "18: " HP_ASM_RET
        HP_ASM_END(http_parse_path_simplify)
);

#endif

#endif /* NET_HTTP_PARSE_IMPLEMENTATION */
