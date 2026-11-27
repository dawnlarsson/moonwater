/*
        RFC 8010 IPP binary wire encoder/decoder & hardened URI parser in pure assembly:
          - ipp_wire_write_begin : initialize bounded writer + emit 8-byte BE IPP header
          - ipp_wire_write_group : validate delimiter tag (0x01..0x0f, != 0x03) + emit
          - ipp_wire_write_attr  : validate tag/lengths/room + emit TLV (name + value)
          - ipp_wire_write_end   : emit IPP_TAG_END (0x03) + return total bytes or 0
          - ipp_wire_read_open   : zero reader + validate version (1..2) + parse 8-byte header
          - ipp_wire_read_next   : hardened RFC 8010 TLV decoder (bounds, 1setOf, fixed-size tags, 4096 cap)
          - ipp_wire_attr_i32    : decode big-endian int32 from INTEGER/ENUM attribute
          - ipp_wire_attr_bool   : decode boolean from BOOLEAN attribute
          - ipp_wire_parse_uri   : ipp/ipps/http/https URI parser hardened against header injection

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define NET_IPP_WIRE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_NET_IPP_WIRE_H
#define MOONWATER_NET_IPP_WIRE_H

#ifdef STANDARD_MODERN_C
typedef p8  iw_u8;
typedef p16 iw_u16;
typedef p32 iw_u32;
typedef b32 iw_i32;
typedef p64 iw_u64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT8_TYPE__  iw_u8;
typedef __UINT16_TYPE__ iw_u16;
typedef __UINT32_TYPE__ iw_u32;
typedef __INT32_TYPE__  iw_i32;
typedef __UINT64_TYPE__ iw_u64;
#else
#include <stdint.h>
typedef uint8_t  iw_u8;
typedef uint16_t iw_u16;
typedef uint32_t iw_u32;
typedef int32_t  iw_i32;
typedef uint64_t iw_u64;
#endif

#define IW_PORT_DEFAULT    631u
#define IW_MAX_ATTRIBUTES  4096u
#define IW_TAG_END         0x03u
#define IW_TAG_INTEGER     0x21u
#define IW_TAG_BOOLEAN     0x22u
#define IW_TAG_ENUM        0x23u
#define IW_TAG_DATETIME    0x31u
#define IW_TAG_RESOLUTION  0x32u
#define IW_TAG_RANGE       0x33u
#define IW_TAG_END_COLL    0x37u
#define IW_TAG_MEMBERNAME  0x4au

/* 32 bytes — byte-for-byte identical to Moonwater's ipp_writer */
struct ipp_wire_writer {
        iw_u8  *bytes;   /* +0  */
        iw_u64  room;    /* +8  */
        iw_u64  used;    /* +16 */
        iw_u8   failed;  /* +24 */
};

/* 80 bytes — byte-for-byte identical to Moonwater's ipp_reader */
struct ipp_wire_reader {
        const iw_u8 *bytes;          /* +0  */
        iw_u64       length;         /* +8  */
        iw_u64       at;             /* +16 */
        iw_u8        ver_major;      /* +24 */
        iw_u8        ver_minor;      /* +25 */
        iw_u16       op_or_status;   /* +26 */
        iw_u32       request_id;     /* +28 */
        iw_u8        group;          /* +32 */
        const iw_u8 *last_name;      /* +40 */
        iw_u64       last_name_len;  /* +48 */
        iw_u64       attr_count;     /* +56 */
        iw_u64       payload_offset; /* +64 */
        iw_u8        ended;          /* +72 */
        iw_u8        failed;         /* +73 */
};

/* 48 bytes — byte-for-byte identical to Moonwater's ipp_attr */
struct ipp_wire_attr {
        iw_u8        group;         /* +0  */
        iw_u8        tag;           /* +1  */
        const iw_u8 *name;          /* +8  */
        iw_u64       name_len;      /* +16 */
        const iw_u8 *value;         /* +24 */
        iw_u64       value_len;     /* +32 */
        iw_u8        is_additional; /* +40 */
};

/* 292 bytes — byte-for-byte identical to Moonwater's ipp_uri */
struct ipp_wire_uri {
        iw_u8  tls;       /* +0   */
        iw_u8  host[128]; /* +1   */
        iw_u16 port;      /* +130 */
        iw_u8  path[160]; /* +132 */
};

#ifdef offset_of
#define IW_OFFSET(T, M) offset_of(T, M)
#else
#define IW_OFFSET(T, M) ((iw_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct ipp_wire_writer) == 32, "ipp_wire_writer size");
_Static_assert(IW_OFFSET(struct ipp_wire_writer, bytes)  == 0,  "writer.bytes");
_Static_assert(IW_OFFSET(struct ipp_wire_writer, room)   == 8,  "writer.room");
_Static_assert(IW_OFFSET(struct ipp_wire_writer, used)   == 16, "writer.used");
_Static_assert(IW_OFFSET(struct ipp_wire_writer, failed) == 24, "writer.failed");

_Static_assert(sizeof(struct ipp_wire_reader) == 80, "ipp_wire_reader size");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, bytes)          == 0,  "reader.bytes");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, length)         == 8,  "reader.length");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, at)             == 16, "reader.at");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, ver_major)      == 24, "reader.ver_major");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, ver_minor)      == 25, "reader.ver_minor");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, op_or_status)   == 26, "reader.op_or_status");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, request_id)     == 28, "reader.request_id");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, group)          == 32, "reader.group");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, last_name)      == 40, "reader.last_name");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, last_name_len)  == 48, "reader.last_name_len");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, attr_count)     == 56, "reader.attr_count");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, payload_offset) == 64, "reader.payload_offset");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, ended)          == 72, "reader.ended");
_Static_assert(IW_OFFSET(struct ipp_wire_reader, failed)         == 73, "reader.failed");

_Static_assert(sizeof(struct ipp_wire_attr) == 48, "ipp_wire_attr size");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, group)         == 0,  "attr.group");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, tag)           == 1,  "attr.tag");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, name)          == 8,  "attr.name");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, name_len)      == 16, "attr.name_len");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, value)         == 24, "attr.value");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, value_len)     == 32, "attr.value_len");
_Static_assert(IW_OFFSET(struct ipp_wire_attr, is_additional) == 40, "attr.is_additional");

_Static_assert(sizeof(struct ipp_wire_uri) == 292, "ipp_wire_uri size");
_Static_assert(IW_OFFSET(struct ipp_wire_uri, tls)  == 0,   "uri.tls");
_Static_assert(IW_OFFSET(struct ipp_wire_uri, host) == 1,   "uri.host");
_Static_assert(IW_OFFSET(struct ipp_wire_uri, port) == 130, "uri.port");
_Static_assert(IW_OFFSET(struct ipp_wire_uri, path) == 132, "uri.path");

void   ipp_wire_write_begin(struct ipp_wire_writer *w, iw_u8 *buf, iw_u64 room, iw_u64 header);
void   ipp_wire_write_group(struct ipp_wire_writer *w, iw_u32 group_tag);
void   ipp_wire_write_attr(struct ipp_wire_writer *w, iw_u32 tag,
                           const char *name, const iw_u8 *val, iw_u64 val_len);
iw_u64 ipp_wire_write_end(struct ipp_wire_writer *w);

iw_i32 ipp_wire_read_open(struct ipp_wire_reader *r, const iw_u8 *bytes, iw_u64 length);
iw_i32 ipp_wire_read_next(struct ipp_wire_reader *r, struct ipp_wire_attr *out);
iw_i32 ipp_wire_attr_i32(const struct ipp_wire_attr *a, iw_i32 *out);
iw_i32 ipp_wire_attr_bool(const struct ipp_wire_attr *a, iw_u8 *out);

iw_i32 ipp_wire_parse_uri(const char *text, struct ipp_wire_uri *out);

#endif /* MOONWATER_NET_IPP_WIRE_H */

#ifdef NET_IPP_WIRE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define IW_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define IW_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define IW_RISCV64 1
# else
#  error "src/net/ipp_wire.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define IW_ARM64 1
# elif defined(X64) && X64
#  define IW_X64 1
# elif defined(RISCV64) && RISCV64
#  define IW_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define IW_ASM_SECTION   ASM_SECTION
# define IW_ASM_FUNC(n)   ASM_FUNC(n)
# define IW_ASM_END(n)    ASM_END(n)
# define IW_ASM_RET       ASM_RET
#else
# define IW_ASM_SECTION   ".text\n"
# define IW_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define IW_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define IW_ASM_END(n)   ""
# elif defined(_WIN32)
#  define IW_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define IW_ASM_END(n)   ""
# else
#  if defined(IW_ARM64)
#   define IW_ASM_FTYPE   "%function"
#  else
#   define IW_ASM_FTYPE   "@function"
#  endif
#  define IW_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " IW_ASM_FTYPE "\n" #n ":\n"
#  define IW_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(IW_ARM64)

__asm__(
        IW_ASM_SECTION

        /* ipp_wire_write_begin(x0=w, x1=buf, x2=room, x3=header) */
        IW_ASM_FUNC(ipp_wire_write_begin)
        "cbz x0, 2f\n   stp x1, x2, [x0]\n   cbz x1, 1f\n   cmp x2, #9\n   b.lo 1f\n"
        "rev x3, x3\n   str x3, [x1]\n   mov x4, #8\n   str x4, [x0, #16]\n   strb wzr, [x0, #24]\n" IW_ASM_RET
        "1:  mov w4, #1\n   str xzr, [x0, #16]\n   strb w4, [x0, #24]\n"
        "2:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_begin)

        /* ipp_wire_write_group(x0=w, w1=group_tag) */
        IW_ASM_FUNC(ipp_wire_write_group)
        "cbz x0, 2f\n   sub w2, w1, #1\n   cmp w2, #14\n   b.hi 1f\n   cmp w1, #3\n   b.eq 1f\n"
        "ldrb w2, [x0, #24]\n   cbnz w2, 1f\n   ldp x3, x4, [x0]\n   ldr x5, [x0, #16]\n"
        "cbz x3, 1f\n   cmp x5, x4\n   b.hs 1f\n   strb w1, [x3, x5]\n   add x5, x5, #1\n   str x5, [x0, #16]\n" IW_ASM_RET
        "1:  mov w2, #1\n   strb w2, [x0, #24]\n"
        "2:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_group)

        /* ipp_wire_write_attr(x0=w, w1=tag, x2=name, x3=val, x4=val_len) */
        IW_ASM_FUNC(ipp_wire_write_attr)
        "cbz x0, 9f\n   ldrb w5, [x0, #24]\n   cbnz w5, 8f\n   sub w5, w1, #16\n   cmp w5, #239\n   b.hi 8f\n"
        "cmp x4, #0x8000\n   b.hs 8f\n   cbz x4, 1f\n   cbz x3, 8f\n"
        "1:  mov x5, #0\n   cbz x2, 3f\n"
        "2:  ldrb w6, [x2, x5]\n   cbz w6, 3f\n   add x5, x5, #1\n   cmp x5, #0x8000\n   b.lo 2b\n   b 8f\n"
        "3:  ldp x6, x7, [x0]\n   ldr x8, [x0, #16]\n   cbz x6, 8f\n   cmp x8, x7\n   b.hi 8f\n"
        "add x9, x5, x4\n   add x9, x9, #5\n   sub x7, x7, x8\n   cmp x9, x7\n   b.hi 8f\n"
        "add x10, x6, x8\n   strb w1, [x10], #1\n   rev16 w11, w5\n   strh w11, [x10], #2\n"
        "mov x12, x5\n   cbz x12, 5f\n"
        "4:  cmp x12, #8\n   b.lo 41f\n   ldr x13, [x2], #8\n   str x13, [x10], #8\n   sub x12, x12, #8\n   b 4b\n"
        "41: cbz x12, 5f\n   ldrb w13, [x2], #1\n   strb w13, [x10], #1\n   sub x12, x12, #1\n   b 41b\n"
        "5:  rev16 w11, w4\n   strh w11, [x10], #2\n   mov x12, x4\n   cbz x12, 7f\n"
        "6:  cmp x12, #8\n   b.lo 61f\n   ldr x13, [x3], #8\n   str x13, [x10], #8\n   sub x12, x12, #8\n   b 6b\n"
        "61: cbz x12, 7f\n   ldrb w13, [x3], #1\n   strb w13, [x10], #1\n   sub x12, x12, #1\n   b 61b\n"
        "7:  add x8, x8, x9\n   str x8, [x0, #16]\n" IW_ASM_RET
        "8:  mov w5, #1\n   strb w5, [x0, #24]\n"
        "9:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_attr)

        /* ipp_wire_write_end(x0=w) -> used bytes or 0 */
        IW_ASM_FUNC(ipp_wire_write_end)
        "cbz x0, 2f\n   ldrb w1, [x0, #24]\n   cbnz w1, 1f\n   ldp x2, x3, [x0]\n   ldr x4, [x0, #16]\n"
        "cbz x2, 1f\n   cmp x4, x3\n   b.hs 1f\n   mov w5, #3\n   strb w5, [x2, x4]\n"
        "add x4, x4, #1\n   str x4, [x0, #16]\n   mov x0, x4\n" IW_ASM_RET
        "1:  mov w1, #1\n   strb w1, [x0, #24]\n"
        "2:  mov x0, #0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_write_end)

        /* ipp_wire_read_open(x0=r, x1=bytes, x2=length) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_read_open)
        "cbz x0, 2f\n   stp xzr, xzr, [x0]\n   stp xzr, xzr, [x0, #16]\n   stp xzr, xzr, [x0, #32]\n"
        "stp xzr, xzr, [x0, #48]\n   stp xzr, xzr, [x0, #64]\n   cbz x1, 1f\n   cmp x2, #9\n   b.lo 1f\n"
        "stp x1, x2, [x0]\n   ldrb w3, [x1]\n   ldrb w4, [x1, #1]\n   strb w3, [x0, #24]\n   strb w4, [x0, #25]\n"
        "sub w3, w3, #1\n   cmp w3, #1\n   b.hi 1f\n   ldrh w5, [x1, #2]\n   rev16 w5, w5\n   strh w5, [x0, #26]\n"
        "ldr w6, [x1, #4]\n   rev w6, w6\n   str w6, [x0, #28]\n   mov x7, #8\n   str x7, [x0, #16]\n   mov w0, #1\n" IW_ASM_RET
        "1:  mov w3, #1\n   strb w3, [x0, #73]\n"
        "2:  mov w0, #0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_read_open)

        /* ipp_wire_read_next(x0=r, x1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_read_next)
        "cbz x1, 9f\n   stp xzr, xzr, [x1]\n   stp xzr, xzr, [x1, #16]\n   stp xzr, xzr, [x1, #32]\n"
        "cbz x0, 9f\n   ldrh w2, [x0, #72]\n   cbnz w2, 9f\n   ldp x3, x4, [x0]\n   ldr x5, [x0, #16]\n"
        "cbz x3, 8f\n"
        "1:  cmp x5, x4\n   b.hs 8f\n   ldrb w6, [x3, x5]\n   add x5, x5, #1\n"
        "cmp w6, #3\n   b.eq 7f\n   cmp w6, #16\n   b.hs 2f\n   cbz w6, 8f\n"
        "strb w6, [x0, #32]\n   stp xzr, xzr, [x0, #40]\n   b 1b\n"
        "2:  ldrb w7, [x0, #32]\n   cbz w7, 8f\n   ldr x8, [x0, #56]\n   cmp x8, #4096\n   b.hs 8f\n"
        "sub x9, x4, x5\n   cmp x9, #2\n   b.lo 8f\n   ldrh w10, [x3, x5]\n   rev16 w10, w10\n   and x10, x10, #0xffff\n"
        "add x5, x5, #2\n   sub x9, x4, x5\n   cmp x10, x9\n   b.hi 8f\n   cbz x10, 3f\n"
        "add x11, x3, x5\n   stp x11, x10, [x1, #8]\n   strb wzr, [x1, #40]\n   stp x11, x10, [x0, #40]\n"
        "add x5, x5, x10\n   b 4f\n"
        "3:  ldp x11, x12, [x0, #40]\n   cbnz x11, 31f\n   cmp w6, #0x37\n   b.eq 31f\n   cmp w6, #0x4a\n   b.ne 8f\n"
        "31: stp x11, x12, [x1, #8]\n   mov w13, #1\n   strb w13, [x1, #40]\n"
        "4:  sub x9, x4, x5\n   cmp x9, #2\n   b.lo 8f\n   ldrh w10, [x3, x5]\n   rev16 w10, w10\n   and x10, x10, #0xffff\n"
        "add x5, x5, #2\n   sub x9, x4, x5\n   cmp x10, x9\n   b.hi 8f\n   add x11, x3, x5\n"
        "stp x11, x10, [x1, #24]\n   add x5, x5, x10\n   sub w12, w6, #0x21\n   cmp w12, #0x12\n   b.hi 6f\n"
        "cmp w6, #0x21\n   b.eq 51f\n   cmp w6, #0x23\n   b.eq 51f\n   cmp w6, #0x22\n   b.eq 52f\n"
        "cmp w6, #0x33\n   b.eq 53f\n   cmp w6, #0x32\n   b.eq 54f\n   cmp w6, #0x31\n   b.ne 6f\n"
        "cmp x10, #11\n   b.ne 8f\n   b 6f\n"
        "51: cmp x10, #4\n   b.ne 8f\n   b 6f\n"
        "52: cmp x10, #1\n   b.ne 8f\n   ldrb w12, [x11]\n   cmp w12, #1\n   b.hi 8f\n   b 6f\n"
        "53: cmp x10, #8\n   b.ne 8f\n   b 6f\n"
        "54: cmp x10, #9\n   b.ne 8f\n"
        "6:  strb w7, [x1]\n   strb w6, [x1, #1]\n   add x8, x8, #1\n   str x8, [x0, #56]\n"
        "str x5, [x0, #16]\n   mov w0, #1\n" IW_ASM_RET
        "7:  mov w2, #1\n   strb w2, [x0, #72]\n   str x5, [x0, #16]\n   str x5, [x0, #64]\n   mov w0, #0\n" IW_ASM_RET
        "8:  mov w2, #1\n   strb w2, [x0, #73]\n   str x5, [x0, #16]\n"
        "9:  mov w0, #0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_read_next)

        /* ipp_wire_attr_i32(x0=a, x1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_attr_i32)
        "cbz x0, 1f\n   cbz x1, 1f\n   ldrb w2, [x0, #1]\n   cmp w2, #0x21\n   b.eq 2f\n   cmp w2, #0x23\n   b.ne 1f\n"
        "2:  ldr x3, [x0, #32]\n   cmp x3, #4\n   b.ne 1f\n   ldr x4, [x0, #24]\n   cbz x4, 1f\n"
        "ldr w5, [x4]\n   rev w5, w5\n   str w5, [x1]\n   mov w0, #1\n" IW_ASM_RET
        "1:  mov w0, #0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_attr_i32)

        /* ipp_wire_attr_bool(x0=a, x1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_attr_bool)
        "cbz x0, 1f\n   cbz x1, 1f\n   ldrb w2, [x0, #1]\n   cmp w2, #0x22\n   b.ne 1f\n"
        "ldr x3, [x0, #32]\n   cmp x3, #1\n   b.ne 1f\n   ldr x4, [x0, #24]\n   cbz x4, 1f\n"
        "ldrb w5, [x4]\n   cmp w5, #0\n   cset w5, ne\n   strb w5, [x1]\n   mov w0, #1\n" IW_ASM_RET
        "1:  mov w0, #0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_attr_bool)

        /* ipp_wire_parse_uri(x0=text, x1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_parse_uri)
        "cbz x1, 19f\n   mov x2, x1\n   mov x3, #4\n"
        "0:  stp xzr, xzr, [x2]\n   stp xzr, xzr, [x2, #16]\n   stp xzr, xzr, [x2, #32]\n"
        "stp xzr, xzr, [x2, #48]\n   add x2, x2, #64\n   subs x3, x3, #1\n   b.ne 0b\n"
        "stp xzr, xzr, [x2]\n   stp xzr, xzr, [x2, #16]\n   str wzr, [x2, #32]\n"
        "cbz x0, 19f\n   ldrb w4, [x0]\n   cbz w4, 19f\n   mov w5, #631\n"
        "orr w6, w4, #32\n   cmp w6, #105\n   b.eq 1f\n   cmp w6, #104\n   b.eq 2f\n   b 3f\n"
        "1:  ldrb w6, [x0, #1]\n   orr w6, w6, #32\n   cmp w6, #112\n   b.ne 3f\n"
        "ldrb w6, [x0, #2]\n   orr w6, w6, #32\n   cmp w6, #112\n   b.ne 3f\n"
        "ldrb w6, [x0, #3]\n   orr w7, w6, #32\n   cmp w7, #115\n   b.eq 11f\n"
        "cmp w6, #58\n   b.ne 3f\n   ldrb w6, [x0, #4]\n   cmp w6, #47\n   b.ne 3f\n"
        "ldrb w6, [x0, #5]\n   cmp w6, #47\n   b.ne 3f\n   add x0, x0, #6\n   b 5f\n"
        "11: ldrb w6, [x0, #4]\n   cmp w6, #58\n   b.ne 3f\n   ldrb w6, [x0, #5]\n   cmp w6, #47\n   b.ne 3f\n"
        "ldrb w6, [x0, #6]\n   cmp w6, #47\n   b.ne 3f\n   mov w7, #1\n   strb w7, [x1]\n   add x0, x0, #7\n   b 5f\n"
        "2:  ldrb w6, [x0, #1]\n   orr w6, w6, #32\n   cmp w6, #116\n   b.ne 3f\n"
        "ldrb w6, [x0, #2]\n   orr w6, w6, #32\n   cmp w6, #116\n   b.ne 3f\n"
        "ldrb w6, [x0, #3]\n   orr w6, w6, #32\n   cmp w6, #112\n   b.ne 3f\n"
        "ldrb w6, [x0, #4]\n   orr w7, w6, #32\n   cmp w7, #115\n   b.eq 21f\n"
        "cmp w6, #58\n   b.ne 3f\n   ldrb w6, [x0, #5]\n   cmp w6, #47\n   b.ne 3f\n"
        "ldrb w6, [x0, #6]\n   cmp w6, #47\n   b.ne 3f\n   mov w5, #80\n   add x0, x0, #7\n   b 5f\n"
        "21: ldrb w6, [x0, #5]\n   cmp w6, #58\n   b.ne 3f\n   ldrb w6, [x0, #6]\n   cmp w6, #47\n   b.ne 3f\n"
        "ldrb w6, [x0, #7]\n   cmp w6, #47\n   b.ne 3f\n   mov w7, #1\n   strb w7, [x1]\n   mov w5, #443\n   add x0, x0, #8\n   b 5f\n"
        "3:  mov x8, x0\n"
        "4:  ldrb w6, [x8]\n   cbz w6, 5f\n   cmp w6, #58\n   b.ne 41f\n"
        "ldrb w6, [x8, #1]\n   cmp w6, #47\n   b.ne 41f\n   ldrb w6, [x8, #2]\n   cmp w6, #47\n   b.eq 19f\n"
        "41: add x8, x8, #1\n   b 4b\n"
        "5:  mov x8, #0\n   add x9, x1, #1\n"
        "6:  ldrb w6, [x0, x8]\n   orr w7, w6, #32\n   sub w7, w7, #97\n   cmp w7, #25\n   b.ls 61f\n"
        "sub w7, w6, #48\n   cmp w7, #9\n   b.ls 61f\n   sub w7, w6, #45\n   cmp w7, #1\n   b.ls 61f\n"
        "cmp w6, #95\n   b.eq 61f\n   cbz w6, 7f\n   cmp w6, #58\n   b.eq 7f\n   cmp w6, #47\n   b.eq 7f\n   b 19f\n"
        "61: cmp x8, #127\n   b.hs 19f\n   strb w6, [x9, x8]\n   add x8, x8, #1\n   b 6b\n"
        "7:  cbz x8, 19f\n   add x0, x0, x8\n   ldrb w6, [x0]\n   cmp w6, #58\n   b.ne 9f\n"
        "add x0, x0, #1\n   mov w5, #0\n   mov w7, #0\n   mov w10, #10\n"
        "8:  ldrb w6, [x0]\n   sub w11, w6, #48\n   cmp w11, #9\n   b.hi 81f\n"
        "madd w5, w5, w10, w11\n   add w7, w7, #1\n   cmp w7, #5\n   b.hi 19f\n   cmp w5, #0x10000\n   b.hs 19f\n"
        "add x0, x0, #1\n   b 8b\n"
        "81: cbz w7, 19f\n   cbz w5, 19f\n"
        "9:  strh w5, [x1, #130]\n   ldrb w6, [x0]\n   cbnz w6, 10f\n"
        "movz x10, #0x692f\n   movk x10, #0x7070, lsl #16\n   movk x10, #0x702f, lsl #32\n   movk x10, #0x6972, lsl #48\n"
        "str x10, [x1, #132]\n   movz w10, #0x746e\n   strh w10, [x1, #140]\n   mov w0, #1\n" IW_ASM_RET
        "10: cmp w6, #47\n   b.ne 19f\n   mov x8, #0\n   add x9, x1, #132\n"
        "12: ldrb w6, [x0, x8]\n   cbz w6, 18f\n   sub w7, w6, #33\n   cmp w7, #93\n   b.hi 19f\n"
        "cmp w6, #92\n   b.eq 19f\n   cmp x8, #159\n   b.hs 19f\n   strb w6, [x9, x8]\n   add x8, x8, #1\n   b 12b\n"
        "18: mov w0, #1\n" IW_ASM_RET
        "19: mov w0, #0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_parse_uri)
);

#elif defined(IW_X64)

#if defined(_WIN32)
# define IW_LOAD1  "mov r8, rcx\n"
# define IW_LOAD2  "mov r8, rcx\n   mov r9, rdx\n"
# define IW_LOAD3  "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define IW_LOAD4  "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
#else
# define IW_LOAD1  "mov r8, rdi\n"
# define IW_LOAD2  "mov r8, rdi\n   mov r9, rsi\n"
# define IW_LOAD3  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define IW_LOAD4  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
#endif

__asm__(
        IW_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* ipp_wire_write_begin(w, buf, room, header) */
        IW_ASM_FUNC(ipp_wire_write_begin)
        IW_LOAD4
        "test r8, r8\n   jz 2f\n   mov [r8], r9\n   mov [r8 + 8], r10\n"
        "test r9, r9\n   jz 1f\n   cmp r10, 9\n   jb 1f\n"
        "bswap r11\n   mov [r9], r11\n   mov qword ptr [r8 + 16], 8\n   mov byte ptr [r8 + 24], 0\n" IW_ASM_RET
        "1:  mov qword ptr [r8 + 16], 0\n   mov byte ptr [r8 + 24], 1\n"
        "2:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_begin)

        /* ipp_wire_write_group(w, group_tag) */
        IW_ASM_FUNC(ipp_wire_write_group)
        IW_LOAD2
        "test r8, r8\n   jz 2f\n   lea eax, [r9 - 1]\n   cmp eax, 14\n   ja 1f\n   cmp r9d, 3\n   je 1f\n"
        "cmp byte ptr [r8 + 24], 0\n   jne 1f\n   mov r10, [r8]\n   test r10, r10\n   jz 1f\n"
        "mov r11, [r8 + 16]\n   cmp r11, [r8 + 8]\n   jae 1f\n   mov [r10 + r11], r9b\n   inc r11\n   mov [r8 + 16], r11\n" IW_ASM_RET
        "1:  mov byte ptr [r8 + 24], 1\n"
        "2:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_group)

        /* ipp_wire_write_attr(w, tag, name, val, val_len) */
        IW_ASM_FUNC(ipp_wire_write_attr)
#if defined(_WIN32)
        "push rdi\n   push rsi\n   mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9d, edx\n   mov rdx, [rsp + 56]\n"
#else
        "mov r10, rdx\n   mov r11, rcx\n   mov rdx, r8\n   mov r8, rdi\n   mov r9d, esi\n"
#endif
        "test r8, r8\n   jz 9f\n   cmp byte ptr [r8 + 24], 0\n   jne 8f\n"
        "lea eax, [r9 - 16]\n   cmp eax, 239\n   ja 8f\n   cmp rdx, 0x7fff\n   ja 8f\n"
        "test rdx, rdx\n   jz 1f\n   test r11, r11\n   jz 8f\n"
        "1:  xor ecx, ecx\n   test r10, r10\n   jz 3f\n"
        "2:  cmp byte ptr [r10 + rcx], 0\n   je 3f\n   inc rcx\n   cmp rcx, 0x7fff\n   jbe 2b\n   jmp 8f\n"
        "3:  mov rdi, [r8]\n   test rdi, rdi\n   jz 8f\n   mov rax, [r8 + 8]\n   mov rsi, [r8 + 16]\n"
        "cmp rsi, rax\n   ja 8f\n   sub rax, rsi\n   lea rdi, [rcx + rdx + 5]\n   cmp rdi, rax\n   ja 8f\n"
        "mov rdi, [r8]\n   add rdi, rsi\n   mov [rdi], r9b\n   mov eax, ecx\n   xchg al, ah\n   mov [rdi + 1], ax\n   add rdi, 3\n"
        "push rcx\n   mov rsi, r10\n   rep movsb\n   pop rcx\n"
        "mov eax, edx\n   xchg al, ah\n   mov [rdi], ax\n   add rdi, 2\n"
        "mov rax, rcx\n   mov rcx, rdx\n   mov rsi, r11\n   rep movsb\n"
        "lea rax, [rax + rdx + 5]\n   add [r8 + 16], rax\n   jmp 9f\n"
        "8:  mov byte ptr [r8 + 24], 1\n"
        "9:  "
#if defined(_WIN32)
        "pop rsi\n   pop rdi\n"
#endif
        IW_ASM_RET
        IW_ASM_END(ipp_wire_write_attr)

        /* ipp_wire_write_end(w) -> used bytes or 0 */
        IW_ASM_FUNC(ipp_wire_write_end)
        IW_LOAD1
        "test r8, r8\n   jz 2f\n   cmp byte ptr [r8 + 24], 0\n   jne 1f\n   mov r9, [r8]\n   test r9, r9\n   jz 1f\n"
        "mov rax, [r8 + 16]\n   cmp rax, [r8 + 8]\n   jae 1f\n   mov byte ptr [r9 + rax], 3\n"
        "inc rax\n   mov [r8 + 16], rax\n" IW_ASM_RET
        "1:  mov byte ptr [r8 + 24], 1\n"
        "2:  xor eax, eax\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_write_end)

        /* ipp_wire_read_open(r, bytes, length) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_read_open)
        IW_LOAD3
        "test r8, r8\n   jz 2f\n   xor eax, eax\n   mov rcx, 10\n"
        "0:  mov [r8 + rcx*8 - 8], rax\n   dec rcx\n   jnz 0b\n"
        "test r9, r9\n   jz 1f\n   cmp r10, 9\n   jb 1f\n"
        "mov [r8], r9\n   mov [r8 + 8], r10\n   movzx eax, byte ptr [r9]\n   movzx edx, byte ptr [r9 + 1]\n"
        "mov [r8 + 24], al\n   mov [r8 + 25], dl\n   lea ecx, [rax - 1]\n   cmp ecx, 1\n   ja 1f\n"
        "movzx eax, word ptr [r9 + 2]\n   xchg al, ah\n   mov [r8 + 26], ax\n"
        "mov eax, [r9 + 4]\n   bswap eax\n   mov [r8 + 28], eax\n   mov qword ptr [r8 + 16], 8\n   mov eax, 1\n" IW_ASM_RET
        "1:  mov byte ptr [r8 + 73], 1\n"
        "2:  xor eax, eax\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_read_open)

        /* ipp_wire_read_next(r, out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_read_next)
        IW_LOAD2
        "test r9, r9\n   jz 9f\n   xor eax, eax\n   mov [r9], rax\n   mov [r9 + 8], rax\n   mov [r9 + 16], rax\n"
        "mov [r9 + 24], rax\n   mov [r9 + 32], rax\n   mov [r9 + 40], rax\n"
        "test r8, r8\n   jz 9f\n   cmp word ptr [r8 + 72], 0\n   jne 9f\n"
        "mov r10, [r8]\n   mov r11, [r8 + 8]\n   mov rdx, [r8 + 16]\n   test r10, r10\n   jz 8f\n"
        "1:  cmp rdx, r11\n   jae 8f\n   movzx eax, byte ptr [r10 + rdx]\n   inc rdx\n"
        "cmp eax, 3\n   je 7f\n   cmp eax, 16\n   jae 2f\n   test eax, eax\n   jz 8f\n"
        "mov [r8 + 32], al\n   mov qword ptr [r8 + 40], 0\n   mov qword ptr [r8 + 48], 0\n   jmp 1b\n"
        "2:  cmp byte ptr [r8 + 32], 0\n   je 8f\n   cmp qword ptr [r8 + 56], 4096\n   jae 8f\n"
        "mov rcx, r11\n   sub rcx, rdx\n   cmp rcx, 2\n   jb 8f\n"
        "movzx ecx, word ptr [r10 + rdx]\n   xchg cl, ch\n   add rdx, 2\n"
        "mov [r9 + 1], al\n   mov rax, r11\n   sub rax, rdx\n   cmp rcx, rax\n   ja 8f\n   test rcx, rcx\n   jz 3f\n"
        "lea rax, [r10 + rdx]\n   mov [r9 + 8], rax\n   mov [r9 + 16], rcx\n   mov byte ptr [r9 + 40], 0\n"
        "mov [r8 + 40], rax\n   mov [r8 + 48], rcx\n   add rdx, rcx\n   jmp 4f\n"
        "3:  mov rax, [r8 + 40]\n   test rax, rax\n   jnz 31f\n   movzx ecx, byte ptr [r9 + 1]\n"
        "cmp ecx, 0x37\n   je 31f\n   cmp ecx, 0x4a\n   jne 8f\n"
        "31: mov [r9 + 8], rax\n   mov rcx, [r8 + 48]\n   mov [r9 + 16], rcx\n   mov byte ptr [r9 + 40], 1\n"
        "4:  mov rcx, r11\n   sub rcx, rdx\n   cmp rcx, 2\n   jb 8f\n"
        "movzx ecx, word ptr [r10 + rdx]\n   xchg cl, ch\n   add rdx, 2\n"
        "mov rax, r11\n   sub rax, rdx\n   cmp rcx, rax\n   ja 8f\n"
        "lea rax, [r10 + rdx]\n   mov [r9 + 24], rax\n   mov [r9 + 32], rcx\n   add rdx, rcx\n"
        "movzx eax, byte ptr [r9 + 1]\n   cmp eax, 0x21\n   je 51f\n   cmp eax, 0x23\n   je 51f\n"
        "cmp eax, 0x22\n   je 52f\n   cmp eax, 0x33\n   je 53f\n   cmp eax, 0x32\n   je 54f\n   cmp eax, 0x31\n   jne 6f\n"
        "cmp rcx, 11\n   jne 8f\n   jmp 6f\n"
        "51: cmp rcx, 4\n   jne 8f\n   jmp 6f\n"
        "52: cmp rcx, 1\n   jne 8f\n   mov rax, [r9 + 24]\n   cmp byte ptr [rax], 1\n   ja 8f\n   jmp 6f\n"
        "53: cmp rcx, 8\n   jne 8f\n   jmp 6f\n"
        "54: cmp rcx, 9\n   jne 8f\n"
        "6:  mov al, [r8 + 32]\n   mov [r9], al\n   inc qword ptr [r8 + 56]\n   mov [r8 + 16], rdx\n   mov eax, 1\n" IW_ASM_RET
        "7:  mov byte ptr [r8 + 72], 1\n   mov [r8 + 16], rdx\n   mov [r8 + 64], rdx\n   xor eax, eax\n" IW_ASM_RET
        "8:  mov byte ptr [r9 + 1], 0\n   mov byte ptr [r8 + 73], 1\n   mov [r8 + 16], rdx\n"
        "9:  xor eax, eax\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_read_next)

        /* ipp_wire_attr_i32(a, out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_attr_i32)
        IW_LOAD2
        "test r8, r8\n   jz 1f\n   test r9, r9\n   jz 1f\n   movzx eax, byte ptr [r8 + 1]\n"
        "cmp eax, 0x21\n   je 2f\n   cmp eax, 0x23\n   jne 1f\n"
        "2:  cmp qword ptr [r8 + 32], 4\n   jne 1f\n   mov r10, [r8 + 24]\n   test r10, r10\n   jz 1f\n"
        "mov eax, [r10]\n   bswap eax\n   mov [r9], eax\n   mov eax, 1\n" IW_ASM_RET
        "1:  xor eax, eax\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_attr_i32)

        /* ipp_wire_attr_bool(a, out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_attr_bool)
        IW_LOAD2
        "test r8, r8\n   jz 1f\n   test r9, r9\n   jz 1f\n   cmp byte ptr [r8 + 1], 0x22\n   jne 1f\n"
        "cmp qword ptr [r8 + 32], 1\n   jne 1f\n   mov r10, [r8 + 24]\n   test r10, r10\n   jz 1f\n"
        "cmp byte ptr [r10], 0\n   setne al\n   mov [r9], al\n   mov eax, 1\n" IW_ASM_RET
        "1:  xor eax, eax\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_attr_bool)

        /* ipp_wire_parse_uri(text, out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_parse_uri)
        IW_LOAD2
        "test r9, r9\n   jz 19f\n   xor eax, eax\n   mov rcx, 36\n"
        "0:  mov [r9 + rcx*8 - 8], rax\n   dec rcx\n   jnz 0b\n   mov dword ptr [r9 + 288], 0\n"
        "test r8, r8\n   jz 19f\n   movzx eax, byte ptr [r8]\n   test eax, eax\n   jz 19f\n   mov r10d, 631\n"
        "or eax, 32\n   cmp eax, 105\n   je 1f\n   cmp eax, 104\n   je 2f\n   jmp 3f\n"
        "1:  movzx eax, byte ptr [r8 + 1]\n   or eax, 32\n   cmp eax, 112\n   jne 3f\n"
        "movzx eax, byte ptr [r8 + 2]\n   or eax, 32\n   cmp eax, 112\n   jne 3f\n"
        "movzx eax, byte ptr [r8 + 3]\n   mov ecx, eax\n   or ecx, 32\n   cmp ecx, 115\n   je 11f\n"
        "cmp eax, 58\n   jne 3f\n   cmp byte ptr [r8 + 4], 47\n   jne 3f\n   cmp byte ptr [r8 + 5], 47\n   jne 3f\n"
        "add r8, 6\n   jmp 5f\n"
        "11: cmp byte ptr [r8 + 4], 58\n   jne 3f\n   cmp byte ptr [r8 + 5], 47\n   jne 3f\n   cmp byte ptr [r8 + 6], 47\n   jne 3f\n"
        "mov byte ptr [r9], 1\n   add r8, 7\n   jmp 5f\n"
        "2:  movzx eax, byte ptr [r8 + 1]\n   or eax, 32\n   cmp eax, 116\n   jne 3f\n"
        "movzx eax, byte ptr [r8 + 2]\n   or eax, 32\n   cmp eax, 116\n   jne 3f\n"
        "movzx eax, byte ptr [r8 + 3]\n   or eax, 32\n   cmp eax, 112\n   jne 3f\n"
        "movzx eax, byte ptr [r8 + 4]\n   mov ecx, eax\n   or ecx, 32\n   cmp ecx, 115\n   je 21f\n"
        "cmp eax, 58\n   jne 3f\n   cmp byte ptr [r8 + 5], 47\n   jne 3f\n   cmp byte ptr [r8 + 6], 47\n   jne 3f\n"
        "mov r10d, 80\n   add r8, 7\n   jmp 5f\n"
        "21: cmp byte ptr [r8 + 5], 58\n   jne 3f\n   cmp byte ptr [r8 + 6], 47\n   jne 3f\n   cmp byte ptr [r8 + 7], 47\n   jne 3f\n"
        "mov byte ptr [r9], 1\n   mov r10d, 443\n   add r8, 8\n   jmp 5f\n"
        "3:  mov rcx, r8\n"
        "4:  movzx eax, byte ptr [rcx]\n   test eax, eax\n   jz 5f\n   cmp eax, 58\n   jne 41f\n"
        "cmp byte ptr [rcx + 1], 47\n   jne 41f\n   cmp byte ptr [rcx + 2], 47\n   je 19f\n"
        "41: inc rcx\n   jmp 4b\n"
        "5:  xor r11d, r11d\n"
        "6:  movzx eax, byte ptr [r8 + r11]\n   test eax, eax\n   jz 7f\n   cmp eax, 58\n   je 7f\n   cmp eax, 47\n   je 7f\n"
        "mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 61f\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 61f\n   lea ecx, [rax - 45]\n   cmp ecx, 1\n   jbe 61f\n"
        "cmp eax, 95\n   jne 19f\n"
        "61: cmp r11, 127\n   jae 19f\n   mov [r9 + r11 + 1], al\n   inc r11\n   jmp 6b\n"
        "7:  test r11, r11\n   jz 19f\n   add r8, r11\n   cmp byte ptr [r8], 58\n   jne 9f\n"
        "inc r8\n   xor r10d, r10d\n   xor ecx, ecx\n"
        "8:  movzx eax, byte ptr [r8]\n   sub eax, 48\n   cmp eax, 9\n   ja 81f\n"
        "imul r10d, r10d, 10\n   add r10d, eax\n   inc ecx\n   cmp ecx, 5\n   ja 19f\n   cmp r10d, 65535\n   ja 19f\n"
        "inc r8\n   jmp 8b\n"
        "81: test ecx, ecx\n   jz 19f\n   test r10d, r10d\n   jz 19f\n"
        "9:  mov [r9 + 130], r10w\n   movzx eax, byte ptr [r8]\n   test eax, eax\n   jnz 10f\n"
        "mov rax, 0x6972702f7070692f\n   mov [r9 + 132], rax\n   mov word ptr [r9 + 140], 0x746e\n   mov eax, 1\n" IW_ASM_RET
        "10: cmp eax, 47\n   jne 19f\n   xor r11d, r11d\n"
        "12: movzx eax, byte ptr [r8 + r11]\n   test eax, eax\n   jz 18f\n   lea ecx, [rax - 33]\n   cmp ecx, 93\n   ja 19f\n"
        "cmp eax, 92\n   je 19f\n   cmp r11, 159\n   jae 19f\n   mov [r9 + r11 + 132], al\n   inc r11\n   jmp 12b\n"
        "18: mov eax, 1\n" IW_ASM_RET
        "19: xor eax, eax\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_parse_uri)

        ".att_syntax prefix\n"
);

#elif defined(IW_RISCV64)

__asm__(
        IW_ASM_SECTION

        /* ipp_wire_write_begin(a0=w, a1=buf, a2=room, a3=header) */
        IW_ASM_FUNC(ipp_wire_write_begin)
        "beqz a0, 2f\n   sd a1, 0(a0)\n   sd a2, 8(a0)\n   beqz a1, 1f\n   li t0, 9\n   bltu a2, t0, 1f\n"
        "li t1, 56\n   mv t2, a1\n"
        "0:  srl t3, a3, t1\n   sb t3, 0(t2)\n   addi t2, t2, 1\n   addi t1, t1, -8\n   bgez t1, 0b\n"
        "li t0, 8\n   sd t0, 16(a0)\n   sb zero, 24(a0)\n" IW_ASM_RET
        "1:  li t0, 1\n   sd zero, 16(a0)\n   sb t0, 24(a0)\n"
        "2:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_begin)

        /* ipp_wire_write_group(a0=w, a1=group_tag) */
        IW_ASM_FUNC(ipp_wire_write_group)
        "beqz a0, 2f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   addi t0, a1, -1\n   li t1, 14\n   bgtu t0, t1, 1f\n"
        "li t1, 3\n   beq a1, t1, 1f\n   lbu t0, 24(a0)\n   bnez t0, 1f\n   ld t2, 0(a0)\n   beqz t2, 1f\n"
        "ld t3, 8(a0)\n   ld t4, 16(a0)\n   bgeu t4, t3, 1f\n   add t5, t2, t4\n   sb a1, 0(t5)\n"
        "addi t4, t4, 1\n   sd t4, 16(a0)\n" IW_ASM_RET
        "1:  li t0, 1\n   sb t0, 24(a0)\n"
        "2:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_group)

        /* ipp_wire_write_attr(a0=w, a1=tag, a2=name, a3=val, a4=val_len) */
        IW_ASM_FUNC(ipp_wire_write_attr)
        "beqz a0, 9f\n   lbu t0, 24(a0)\n   bnez t0, 8f\n   slli a1, a1, 32\n   srli a1, a1, 32\n"
        "addi t0, a1, -16\n   li t1, 239\n   bgtu t0, t1, 8f\n   li t1, 0x7fff\n   bgtu a4, t1, 8f\n"
        "beqz a4, 1f\n   beqz a3, 8f\n"
        "1:  li a5, 0\n   beqz a2, 3f\n"
        "2:  add t2, a2, a5\n   lbu t3, 0(t2)\n   beqz t3, 3f\n   addi a5, a5, 1\n   bleu a5, t1, 2b\n   j 8f\n"
        "3:  ld t2, 0(a0)\n   beqz t2, 8f\n   ld t3, 8(a0)\n   ld t4, 16(a0)\n   bgtu t4, t3, 8f\n"
        "add a6, a5, a4\n   addi a6, a6, 5\n   sub t3, t3, t4\n   bgtu a6, t3, 8f\n"
        "add t5, t2, t4\n   sb a1, 0(t5)\n   srli t0, a5, 8\n   sb t0, 1(t5)\n   sb a5, 2(t5)\n   addi t5, t5, 3\n"
        "li t6, 0\n"
        "4:  bgeu t6, a5, 5f\n   add t0, a2, t6\n   lbu t1, 0(t0)\n   sb t1, 0(t5)\n   addi t5, t5, 1\n   addi t6, t6, 1\n   j 4b\n"
        "5:  srli t0, a4, 8\n   sb t0, 0(t5)\n   sb a4, 1(t5)\n   addi t5, t5, 2\n   li t6, 0\n"
        "6:  bgeu t6, a4, 7f\n   add t0, a3, t6\n   lbu t1, 0(t0)\n   sb t1, 0(t5)\n   addi t5, t5, 1\n   addi t6, t6, 1\n   j 6b\n"
        "7:  add t4, t4, a6\n   sd t4, 16(a0)\n" IW_ASM_RET
        "8:  li t0, 1\n   sb t0, 24(a0)\n"
        "9:  " IW_ASM_RET
        IW_ASM_END(ipp_wire_write_attr)

        /* ipp_wire_write_end(a0=w) -> used bytes or 0 */
        IW_ASM_FUNC(ipp_wire_write_end)
        "beqz a0, 2f\n   lbu t0, 24(a0)\n   bnez t0, 1f\n   ld t1, 0(a0)\n   beqz t1, 1f\n"
        "ld t2, 8(a0)\n   ld t3, 16(a0)\n   bgeu t3, t2, 1f\n   add t4, t1, t3\n   li t5, 3\n   sb t5, 0(t4)\n"
        "addi t3, t3, 1\n   sd t3, 16(a0)\n   mv a0, t3\n" IW_ASM_RET
        "1:  li t0, 1\n   sb t0, 24(a0)\n"
        "2:  li a0, 0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_write_end)

        /* ipp_wire_read_open(a0=r, a1=bytes, a2=length) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_read_open)
        "beqz a0, 2f\n   mv t0, a0\n   li t1, 10\n"
        "0:  sd zero, 0(t0)\n   addi t0, t0, 8\n   addi t1, t1, -1\n   bnez t1, 0b\n"
        "beqz a1, 1f\n   li t0, 9\n   bltu a2, t0, 1f\n   sd a1, 0(a0)\n   sd a2, 8(a0)\n"
        "lbu t0, 0(a1)\n   lbu t1, 1(a1)\n   sb t0, 24(a0)\n   sb t1, 25(a0)\n"
        "addi t0, t0, -1\n   li t2, 1\n   bgtu t0, t2, 1f\n"
        "lbu t0, 2(a1)\n   lbu t1, 3(a1)\n   slli t0, t0, 8\n   or t0, t0, t1\n   sh t0, 26(a0)\n"
        "lbu t0, 4(a1)\n   lbu t1, 5(a1)\n   lbu t2, 6(a1)\n   lbu t3, 7(a1)\n"
        "slli t0, t0, 24\n   slli t1, t1, 16\n   slli t2, t2, 8\n   or t0, t0, t1\n   or t0, t0, t2\n   or t0, t0, t3\n"
        "sw t0, 28(a0)\n   li t0, 8\n   sd t0, 16(a0)\n   li a0, 1\n" IW_ASM_RET
        "1:  li t0, 1\n   sb t0, 73(a0)\n"
        "2:  li a0, 0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_read_open)

        /* ipp_wire_read_next(a0=r, a1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_read_next)
        "beqz a1, 9f\n   sd zero, 0(a1)\n   sd zero, 8(a1)\n   sd zero, 16(a1)\n"
        "sd zero, 24(a1)\n   sd zero, 32(a1)\n   sd zero, 40(a1)\n"
        "beqz a0, 9f\n   lbu t0, 72(a0)\n   lbu t1, 73(a0)\n   or t0, t0, t1\n   bnez t0, 9f\n"
        "ld a2, 0(a0)\n   ld a3, 8(a0)\n   ld a4, 16(a0)\n   beqz a2, 8f\n"
        "1:  bgeu a4, a3, 8f\n   add t0, a2, a4\n   lbu a5, 0(t0)\n   addi a4, a4, 1\n"
        "li t1, 3\n   beq a5, t1, 7f\n   li t1, 16\n   bgeu a5, t1, 2f\n   beqz a5, 8f\n"
        "sb a5, 32(a0)\n   sd zero, 40(a0)\n   sd zero, 48(a0)\n   j 1b\n"
        "2:  lbu a6, 32(a0)\n   beqz a6, 8f\n   ld a7, 56(a0)\n   li t1, 4096\n   bgeu a7, t1, 8f\n"
        "sub t0, a3, a4\n   li t1, 2\n   bltu t0, t1, 8f\n   add t2, a2, a4\n"
        "lbu t3, 0(t2)\n   lbu t4, 1(t2)\n   slli t3, t3, 8\n   or t3, t3, t4\n   addi a4, a4, 2\n"
        "sub t0, a3, a4\n   bgtu t3, t0, 8f\n   beqz t3, 3f\n"
        "add t2, a2, a4\n   sd t2, 8(a1)\n   sd t3, 16(a1)\n   sb zero, 40(a1)\n"
        "sd t2, 40(a0)\n   sd t3, 48(a0)\n   add a4, a4, t3\n   j 4f\n"
        "3:  ld t2, 40(a0)\n   ld t3, 48(a0)\n   bnez t2, 31f\n   li t1, 0x37\n   beq a5, t1, 31f\n"
        "li t1, 0x4a\n   bne a5, t1, 8f\n"
        "31: sd t2, 8(a1)\n   sd t3, 16(a1)\n   li t1, 1\n   sb t1, 40(a1)\n"
        "4:  sub t0, a3, a4\n   li t1, 2\n   bltu t0, t1, 8f\n   add t2, a2, a4\n"
        "lbu t3, 0(t2)\n   lbu t4, 1(t2)\n   slli t3, t3, 8\n   or t3, t3, t4\n   addi a4, a4, 2\n"
        "sub t0, a3, a4\n   bgtu t3, t0, 8f\n   add t2, a2, a4\n   sd t2, 24(a1)\n   sd t3, 32(a1)\n   add a4, a4, t3\n"
        "li t1, 0x21\n   beq a5, t1, 51f\n   li t1, 0x23\n   beq a5, t1, 51f\n   li t1, 0x22\n   beq a5, t1, 52f\n"
        "li t1, 0x33\n   beq a5, t1, 53f\n   li t1, 0x32\n   beq a5, t1, 54f\n   li t1, 0x31\n   bne a5, t1, 6f\n"
        "li t1, 11\n   bne t3, t1, 8f\n   j 6f\n"
        "51: li t1, 4\n   bne t3, t1, 8f\n   j 6f\n"
        "52: li t1, 1\n   bne t3, t1, 8f\n   lbu t4, 0(t2)\n   bgtu t4, t1, 8f\n   j 6f\n"
        "53: li t1, 8\n   bne t3, t1, 8f\n   j 6f\n"
        "54: li t1, 9\n   bne t3, t1, 8f\n"
        "6:  sb a6, 0(a1)\n   sb a5, 1(a1)\n   addi a7, a7, 1\n   sd a7, 56(a0)\n   sd a4, 16(a0)\n   li a0, 1\n" IW_ASM_RET
        "7:  li t0, 1\n   sb t0, 72(a0)\n   sd a4, 16(a0)\n   sd a4, 64(a0)\n   li a0, 0\n" IW_ASM_RET
        "8:  li t0, 1\n   sb t0, 73(a0)\n   sd a4, 16(a0)\n"
        "9:  li a0, 0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_read_next)

        /* ipp_wire_attr_i32(a0=a, a1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_attr_i32)
        "beqz a0, 1f\n   beqz a1, 1f\n   lbu t0, 1(a0)\n   li t1, 0x21\n   beq t0, t1, 2f\n   li t1, 0x23\n   bne t0, t1, 1f\n"
        "2:  ld t2, 32(a0)\n   li t1, 4\n   bne t2, t1, 1f\n   ld t3, 24(a0)\n   beqz t3, 1f\n"
        "lbu t0, 0(t3)\n   lbu t1, 1(t3)\n   lbu t2, 2(t3)\n   lbu t4, 3(t3)\n"
        "slli t0, t0, 24\n   slli t1, t1, 16\n   slli t2, t2, 8\n   or t0, t0, t1\n   or t0, t0, t2\n   or t0, t0, t4\n"
        "sw t0, 0(a1)\n   li a0, 1\n" IW_ASM_RET
        "1:  li a0, 0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_attr_i32)

        /* ipp_wire_attr_bool(a0=a, a1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_attr_bool)
        "beqz a0, 1f\n   beqz a1, 1f\n   lbu t0, 1(a0)\n   li t1, 0x22\n   bne t0, t1, 1f\n"
        "ld t2, 32(a0)\n   li t1, 1\n   bne t2, t1, 1f\n   ld t3, 24(a0)\n   beqz t3, 1f\n"
        "lbu t0, 0(t3)\n   snez t0, t0\n   sb t0, 0(a1)\n   li a0, 1\n" IW_ASM_RET
        "1:  li a0, 0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_attr_bool)

        /* ipp_wire_parse_uri(a0=text, a1=out) -> 1/0 */
        IW_ASM_FUNC(ipp_wire_parse_uri)
        "beqz a1, 19f\n   mv t0, a1\n   li t1, 146\n"
        "0:  sh zero, 0(t0)\n   addi t0, t0, 2\n   addi t1, t1, -1\n   bnez t1, 0b\n"
        "beqz a0, 19f\n   lbu t0, 0(a0)\n   beqz t0, 19f\n   li a2, 631\n"
        "ori t1, t0, 32\n   li t2, 105\n   beq t1, t2, 1f\n   li t2, 104\n   beq t1, t2, 2f\n   j 3f\n"
        "1:  lbu t1, 1(a0)\n   ori t1, t1, 32\n   li t2, 112\n   bne t1, t2, 3f\n"
        "lbu t1, 2(a0)\n   ori t1, t1, 32\n   bne t1, t2, 3f\n   lbu t1, 3(a0)\n   ori t3, t1, 32\n   li t2, 115\n   beq t3, t2, 11f\n"
        "li t2, 58\n   bne t1, t2, 3f\n   lbu t1, 4(a0)\n   li t2, 47\n   bne t1, t2, 3f\n"
        "lbu t1, 5(a0)\n   bne t1, t2, 3f\n   addi a0, a0, 6\n   j 5f\n"
        "11: lbu t1, 4(a0)\n   li t2, 58\n   bne t1, t2, 3f\n   lbu t1, 5(a0)\n   li t2, 47\n   bne t1, t2, 3f\n"
        "lbu t1, 6(a0)\n   bne t1, t2, 3f\n   li t2, 1\n   sb t2, 0(a1)\n   addi a0, a0, 7\n   j 5f\n"
        "2:  lbu t1, 1(a0)\n   ori t1, t1, 32\n   li t2, 116\n   bne t1, t2, 3f\n"
        "lbu t1, 2(a0)\n   ori t1, t1, 32\n   bne t1, t2, 3f\n   lbu t1, 3(a0)\n   ori t1, t1, 32\n   li t2, 112\n   bne t1, t2, 3f\n"
        "lbu t1, 4(a0)\n   ori t3, t1, 32\n   li t2, 115\n   beq t3, t2, 21f\n"
        "li t2, 58\n   bne t1, t2, 3f\n   lbu t1, 5(a0)\n   li t2, 47\n   bne t1, t2, 3f\n"
        "lbu t1, 6(a0)\n   bne t1, t2, 3f\n   li a2, 80\n   addi a0, a0, 7\n   j 5f\n"
        "21: lbu t1, 5(a0)\n   li t2, 58\n   bne t1, t2, 3f\n   lbu t1, 6(a0)\n   li t2, 47\n   bne t1, t2, 3f\n"
        "lbu t1, 7(a0)\n   bne t1, t2, 3f\n   li t2, 1\n   sb t2, 0(a1)\n   li a2, 443\n   addi a0, a0, 8\n   j 5f\n"
        "3:  mv t0, a0\n   li t2, 58\n   li t3, 47\n"
        "4:  lbu t1, 0(t0)\n   beqz t1, 5f\n   bne t1, t2, 41f\n   lbu t1, 1(t0)\n   bne t1, t3, 41f\n"
        "lbu t1, 2(t0)\n   beq t1, t3, 19f\n"
        "41: addi t0, t0, 1\n   j 4b\n"
        "5:  li a3, 0\n   addi a4, a1, 1\n   li t2, 58\n   li t3, 47\n   li t4, 127\n"
        "6:  add t0, a0, a3\n   lbu t1, 0(t0)\n   beqz t1, 7f\n   beq t1, t2, 7f\n   beq t1, t3, 7f\n"
        "ori t5, t1, 32\n   addi t5, t5, -97\n   li t6, 25\n   bleu t5, t6, 61f\n"
        "addi t5, t1, -48\n   li t6, 9\n   bleu t5, t6, 61f\n   addi t5, t1, -45\n   li t6, 1\n   bleu t5, t6, 61f\n"
        "li t6, 95\n   bne t1, t6, 19f\n"
        "61: bgeu a3, t4, 19f\n   add t0, a4, a3\n   sb t1, 0(t0)\n   addi a3, a3, 1\n   j 6b\n"
        "7:  beqz a3, 19f\n   add a0, a0, a3\n   lbu t1, 0(a0)\n   bne t1, t2, 9f\n"
        "addi a0, a0, 1\n   li a2, 0\n   li a5, 0\n   li t4, 9\n   li t5, 5\n   li t6, 65535\n"
        "8:  lbu t1, 0(a0)\n   addi t1, t1, -48\n   bgtu t1, t4, 81f\n   slli t0, a2, 3\n   slli a6, a2, 1\n"
        "add a2, t0, a6\n   add a2, a2, t1\n   addi a5, a5, 1\n   bgtu a5, t5, 19f\n   bgtu a2, t6, 19f\n"
        "addi a0, a0, 1\n   j 8b\n"
        "81: beqz a5, 19f\n   beqz a2, 19f\n"
        "9:  sh a2, 130(a1)\n   lbu t1, 0(a0)\n   bnez t1, 10f\n"
        "li t0, 0x692f\n   sh t0, 132(a1)\n   li t0, 0x7070\n   sh t0, 134(a1)\n   li t0, 0x702f\n   sh t0, 136(a1)\n"
        "li t0, 0x6972\n   sh t0, 138(a1)\n   li t0, 0x746e\n   sh t0, 140(a1)\n   li a0, 1\n" IW_ASM_RET
        "10: bne t1, t3, 19f\n   li a3, 0\n   addi a4, a1, 132\n   li t4, 93\n   li t5, 92\n   li t6, 159\n"
        "12: add t0, a0, a3\n   lbu t1, 0(t0)\n   beqz t1, 18f\n   addi t2, t1, -33\n   bgtu t2, t4, 19f\n"
        "beq t1, t5, 19f\n   bgeu a3, t6, 19f\n   add t0, a4, a3\n   sb t1, 0(t0)\n   addi a3, a3, 1\n   j 12b\n"
        "18: li a0, 1\n" IW_ASM_RET
        "19: li a0, 0\n" IW_ASM_RET
        IW_ASM_END(ipp_wire_parse_uri)
);

#endif

#endif /* NET_IPP_WIRE_ASM_IMPLEMENTATION */
