/*
        D-Bus wire protocol core primitives in pure assembly:
          - dbus_wire_utf8_valid     : strict UTF-8 validator (no NUL, no overlong, no surrogates, <= U+10FFFF, 8-byte SWAR ASCII fast path)
          - dbus_wire_valid_path     : D-Bus object path grammar validator (/, /a/b_1)
          - dbus_wire_valid_name     : D-Bus interface/error (0), bus/unique (1), and member (2) name validator
          - dbus_wire_sig_one        : single complete D-Bus type signature validator (depth <= 32 per container)
          - dbus_wire_valid_signature: full D-Bus signature validator (<= 255 bytes)
          - dbus_wire_variant_sig    : variant signature reader & validator (len + sig + NUL)
          - dbus_wire_fixed_size     : branchless fixed-size primitive width lookup (1, 2, 4, 8, or 0)
          - dbus_wire_pad_zero       : alignment zero-padding validator & cursor advance
          - dbus_wire_get32          : endian-aware ('l'/'B') 32-bit unsigned wire reader
          - dbus_wire_value          : recursive D-Bus wire value validator (depth <= 64, GDBus compat, 'l'/'B')
          - dbus_wire_body_valid     : full D-Bus message body validator against signature
          - dbus_wire_header_check   : fixed 16-byte header + header-end padding validator ('l'/'B')
          - dbus_wire_o_pad          : wire writer zero-padding emitter
          - dbus_wire_o_u32          : wire writer aligned LE u32 emitter
          - dbus_wire_o_string       : wire writer aligned LE string (u32 len + bytes + NUL) emitter

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define DBUS_WIRE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_DBUS_WIRE_ASM_H
#define MOONWATER_DBUS_WIRE_ASM_H

#if defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  dbus_u8;
typedef __UINT32_TYPE__ dbus_u32;
typedef __INT32_TYPE__  dbus_i32;
typedef __UINT64_TYPE__ dbus_u64;
typedef __INT64_TYPE__  dbus_i64;
#else
#include <stdint.h>
typedef uint8_t  dbus_u8;
typedef uint32_t dbus_u32;
typedef int32_t  dbus_i32;
typedef uint64_t dbus_u64;
typedef int64_t  dbus_i64;
#endif

/* 64 bytes — cursor for dbus_wire_value */
struct dbus_wire_cursor {
        const unsigned char *p;          /* +0  wire buffer */
        dbus_i64             at;         /* +8  current byte offset */
        dbus_i64             limit;      /* +16 exclusive byte limit */
        const char          *sig;        /* +24 signature bytes */
        dbus_i64             sig_len;    /* +32 signature length */
        dbus_i64             s;          /* +40 current signature offset */
        dbus_u64             fds;        /* +48 number of attached unix FDs */
        dbus_i32             depth;      /* +56 container nesting depth (<= 64) */
        dbus_i32             big_endian; /* +60 0 for 'l' (LE), non-zero for 'B' (BE) */
};

/* 32 bytes — parsed fixed 16-byte D-Bus message header */
struct dbus_wire_hdr {
        dbus_u32 serial;       /* +0  non-zero message serial */
        dbus_u32 body_len;     /* +4  body byte length */
        dbus_u32 fields_len;   /* +8  header fields array byte length */
        dbus_u8  type;         /* +12 1..4 (CALL/RETURN/ERROR/SIGNAL) */
        dbus_u8  flags;        /* +13 message flags */
        dbus_u8  big_endian;   /* +14 0 for 'l', 1 for 'B' */
        dbus_u8  _pad;         /* +15 zero */
        dbus_i64 fields_limit; /* +16 16 + fields_len */
        dbus_i64 body_at;      /* +24 DBUS_ALIGN(fields_limit, 8) */
};

/* 32 bytes — byte-for-byte identical to Moonwater's struct dbus_out */
struct dbus_wire_out {
        unsigned char *at;     /* +0  output buffer */
        dbus_i64       length; /* +8  bytes written */
        dbus_i64       room;   /* +16 buffer capacity */
        dbus_i32       bad;    /* +24 non-zero on overflow */
        dbus_i32       _pad;   /* +28 alignment padding */
};

#ifdef offset_of
#define DBUS_WIRE_OFFSET(T, M) offset_of(T, M)
#else
#define DBUS_WIRE_OFFSET(T, M) ((dbus_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct dbus_wire_cursor) == 64, "dbus_wire_cursor must be 64 bytes");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, p)          == 0,  "cursor.p");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, at)         == 8,  "cursor.at");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, limit)      == 16, "cursor.limit");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, sig)        == 24, "cursor.sig");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, sig_len)    == 32, "cursor.sig_len");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, s)          == 40, "cursor.s");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, fds)        == 48, "cursor.fds");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, depth)      == 56, "cursor.depth");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_cursor, big_endian) == 60, "cursor.big_endian");

_Static_assert(sizeof(struct dbus_wire_hdr) == 32, "dbus_wire_hdr must be 32 bytes");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, serial)       == 0,  "hdr.serial");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, body_len)     == 4,  "hdr.body_len");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, fields_len)   == 8,  "hdr.fields_len");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, type)         == 12, "hdr.type");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, flags)        == 13, "hdr.flags");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, big_endian)   == 14, "hdr.big_endian");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, fields_limit) == 16, "hdr.fields_limit");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_hdr, body_at)      == 24, "hdr.body_at");

_Static_assert(sizeof(struct dbus_wire_out) == 32, "dbus_wire_out must be 32 bytes");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_out, at)     == 0,  "out.at");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_out, length) == 8,  "out.length");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_out, room)   == 16, "out.room");
_Static_assert(DBUS_WIRE_OFFSET(struct dbus_wire_out, bad)    == 24, "out.bad");

dbus_i32 dbus_wire_utf8_valid(const unsigned char *s, dbus_i64 n);
dbus_i32 dbus_wire_valid_path(const char *s, dbus_i64 n);
dbus_i32 dbus_wire_valid_name(const char *s, dbus_i64 n, dbus_i32 kind);
dbus_i32 dbus_wire_sig_one(const char *s, dbus_i64 n, dbus_i64 *at, dbus_i32 arrays, dbus_i32 structs, dbus_i32 dicts);
dbus_i32 dbus_wire_valid_signature(const char *s, dbus_i64 n);
dbus_i64 dbus_wire_variant_sig(const unsigned char *p, dbus_i64 *at, dbus_i64 limit);
dbus_i64 dbus_wire_fixed_size(dbus_i32 c);
dbus_i32 dbus_wire_pad_zero(const unsigned char *p, dbus_i64 *at, dbus_i64 limit, dbus_i64 align);
dbus_u32 dbus_wire_get32(const unsigned char *p, dbus_i32 big_endian);
dbus_i32 dbus_wire_value(struct dbus_wire_cursor *c);
dbus_i32 dbus_wire_body_valid(const unsigned char *body, dbus_i64 length, const char *sig, dbus_i64 sig_len, dbus_u64 fds, dbus_i32 big_endian);
dbus_i32 dbus_wire_header_check(const unsigned char *b, dbus_i64 size, dbus_i64 max_size, struct dbus_wire_hdr *out);
void     dbus_wire_o_pad(struct dbus_wire_out *w, dbus_i64 align);
void     dbus_wire_o_u32(struct dbus_wire_out *w, dbus_u32 v);
void     dbus_wire_o_string(struct dbus_wire_out *w, const char *s);

#endif /* MOONWATER_DBUS_WIRE_ASM_H */

#ifdef DBUS_WIRE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define DBUS_WIRE_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define DBUS_WIRE_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define DBUS_WIRE_RISCV64 1
# else
#  error "src/dbus/dbus_wire.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define DBUS_WIRE_ARM64 1
# elif defined(X64) && X64
#  define DBUS_WIRE_X64 1
# elif defined(RISCV64) && RISCV64
#  define DBUS_WIRE_RISCV64 1
# endif
#endif

#if defined(__APPLE__)
# define DBUS_ASM_SYM(n) "_" #n
#else
# define DBUS_ASM_SYM(n) #n
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define DBUS_ASM_SECTION   ASM_SECTION
# define DBUS_ASM_FUNC(n)   ASM_FUNC(n)
# define DBUS_ASM_END(n)    ASM_END(n)
# define DBUS_ASM_RET       ASM_RET
#else
# define DBUS_ASM_SECTION   ".text\n"
# define DBUS_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define DBUS_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define DBUS_ASM_END(n)   ""
# elif defined(_WIN32)
#  define DBUS_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define DBUS_ASM_END(n)   ""
# else
#  if defined(DBUS_WIRE_ARM64)
#   define DBUS_ASM_FTYPE   "%function"
#  else
#   define DBUS_ASM_FTYPE   "@function"
#  endif
#  define DBUS_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " DBUS_ASM_FTYPE "\n" #n ":\n"
#  define DBUS_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(DBUS_WIRE_ARM64)

__asm__(
        DBUS_ASM_SECTION

        /* dbus_wire_utf8_valid(x0=s, x1=n) -> 1 valid, 0 invalid (8-byte SWAR ASCII fast path) */
        DBUS_ASM_FUNC(dbus_wire_utf8_valid)
        "cmp x1, #0\n   b.eq 9f\n   b.lt 8f\n   cbz x0, 8f\n   add x1, x0, x1\n"
        "mov x4, #0x0101010101010101\n   mov x5, #0x8080808080808080\n"
        "1:  sub x2, x1, x0\n   cmp x2, #8\n   b.lo 2f\n   ldr x3, [x0]\n"
        "sub x6, x3, x4\n   orr x6, x6, x3\n   tst x6, x5\n   b.ne 2f\n   add x0, x0, #8\n   b 1b\n"
        "2:  cmp x0, x1\n   b.hs 9f\n   ldrb w2, [x0]\n   cbz w2, 8f\n   cmp w2, #0x80\n   b.hs 3f\n"
        "add x0, x0, #1\n   b 1b\n"
        "3:  cmp w2, #0xc2\n   b.lo 8f\n   cmp w2, #0xe0\n   b.hs 4f\n"
        "sub x3, x1, x0\n   cmp x3, #2\n   b.lo 8f\n   ldrb w3, [x0, #1]\n"
        "and w3, w3, #0xc0\n   cmp w3, #0x80\n   b.ne 8f\n   add x0, x0, #2\n   b 1b\n"
        "4:  cmp w2, #0xf0\n   b.hs 6f\n   sub x3, x1, x0\n   cmp x3, #3\n   b.lo 8f\n"
        "ldrb w3, [x0, #1]\n   ldrb w6, [x0, #2]\n   and w7, w3, #0xc0\n   cmp w7, #0x80\n   b.ne 8f\n"
        "and w7, w6, #0xc0\n   cmp w7, #0x80\n   b.ne 8f\n   cmp w2, #0xe0\n   b.ne 5f\n"
        "cmp w3, #0xa0\n   b.lo 8f\n   add x0, x0, #3\n   b 1b\n"
        "5:  cmp w2, #0xed\n   b.ne 7f\n   cmp w3, #0xa0\n   b.hs 8f\n"
        "7:  add x0, x0, #3\n   b 1b\n"
        "6:  cmp w2, #0xf5\n   b.hs 8f\n   sub x3, x1, x0\n   cmp x3, #4\n   b.lo 8f\n"
        "ldrb w3, [x0, #1]\n   ldrb w6, [x0, #2]\n   ldrb w7, [x0, #3]\n"
        "and w8, w3, #0xc0\n   cmp w8, #0x80\n   b.ne 8f\n   and w8, w6, #0xc0\n   cmp w8, #0x80\n   b.ne 8f\n"
        "and w8, w7, #0xc0\n   cmp w8, #0x80\n   b.ne 8f\n   cmp w2, #0xf0\n   b.ne 10f\n"
        "cmp w3, #0x90\n   b.lo 8f\n   add x0, x0, #4\n   b 1b\n"
        "10: cmp w2, #0xf4\n   b.ne 11f\n   cmp w3, #0x90\n   b.hs 8f\n"
        "11: add x0, x0, #4\n   b 1b\n"
        "8:  mov w0, #0\n" DBUS_ASM_RET
        "9:  mov w0, #1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_utf8_valid)

        /* dbus_wire_valid_path(x0=s, x1=n) -> 1 valid, 0 invalid */
        DBUS_ASM_FUNC(dbus_wire_valid_path)
        "cbz x0, 5f\n   sub x2, x1, #1\n   cmp x2, #254\n   b.hi 5f\n"
        "ldrb w2, [x0]\n   cmp w2, #47\n   b.ne 5f\n   cmp x1, #1\n   b.eq 6f\n"
        "add x1, x0, x1\n   add x0, x0, #1\n   mov w3, #1\n"
        "1:  cmp x0, x1\n   b.hs 4f\n   ldrb w4, [x0], #1\n"
        "orr w5, w4, #32\n   sub w5, w5, #97\n   cmp w5, #25\n   b.ls 2f\n"
        "sub w5, w4, #48\n   cmp w5, #9\n   b.ls 2f\n   cmp w4, #95\n   b.eq 2f\n"
        "cmp w4, #47\n   b.ne 5f\n   cbnz w3, 5f\n   mov w3, #1\n   b 1b\n"
        "2:  mov w3, #0\n   b 1b\n"
        "4:  eor w0, w3, #1\n" DBUS_ASM_RET
        "6:  mov w0, #1\n" DBUS_ASM_RET
        "5:  mov w0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_path)

        /* dbus_wire_valid_name(x0=s, x1=n, w2=kind) -> 0 iface/err, 1 bus, 2 member */
        DBUS_ASM_FUNC(dbus_wire_valid_name)
        "cbz x0, 8f\n   sub x3, x1, #1\n   cmp x3, #254\n   b.hi 8f\n"
        "cmp w2, #2\n   b.ne 2f\n   ldrb w4, [x0]\n   orr w5, w4, #32\n   sub w5, w5, #97\n"
        "cmp w5, #25\n   b.ls 10f\n   cmp w4, #95\n   b.ne 8f\n"
        "10: mov x3, #1\n"
        "1:  cmp x3, x1\n   b.hs 9f\n   ldrb w4, [x0, x3]\n   add x3, x3, #1\n"
        "orr w5, w4, #32\n   sub w5, w5, #97\n   cmp w5, #25\n   b.ls 1b\n"
        "sub w5, w4, #48\n   cmp w5, #9\n   b.ls 1b\n   cmp w4, #95\n   b.eq 1b\n   b 8f\n"
        "2:  mov x3, #0\n   mov x4, #0\n   mov w6, #0\n   cmp w2, #1\n   b.ne 3f\n"
        "ldrb w5, [x0]\n   cmp w5, #58\n   b.ne 3f\n   mov w6, #1\n   mov x3, #1\n   mov x4, #1\n"
        "3:  mov x7, #0\n"
        "4:  cmp x3, x1\n   b.eq 5f\n   ldrb w5, [x0, x3]\n   cmp w5, #46\n   b.ne 6f\n"
        "5:  cmp x3, x4\n   b.eq 8f\n   add x4, x3, #1\n   cmp x3, x1\n   b.eq 7f\n"
        "add x7, x7, #1\n   add x3, x3, #1\n   b 4b\n"
        "6:  orr w8, w5, #32\n   sub w8, w8, #97\n   cmp w8, #25\n   b.ls 11f\n"
        "cmp w5, #95\n   b.eq 11f\n   cmp w2, #1\n   b.ne 12f\n   cmp w5, #45\n   b.eq 11f\n"
        "12: sub w8, w5, #48\n   cmp w8, #9\n   b.hi 8f\n   cbnz w6, 11f\n   cmp x3, x4\n   b.eq 8f\n"
        "11: add x3, x3, #1\n   b 4b\n"
        "7:  cbz x7, 8f\n   sub x5, x1, #1\n   ldrb w5, [x0, x5]\n   cmp w5, #46\n   b.eq 8f\n"
        "9:  mov w0, #1\n" DBUS_ASM_RET
        "8:  mov w0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_name)

        /* dbus_wire_sig_one(x0=s, x1=n, x2=at, w3=arrays, w4=structs, w5=dicts) */
        DBUS_ASM_FUNC(dbus_wire_sig_one)
        "cbz x0, 8f\n   cbz x2, 8f\n"
        "1:  ldr x6, [x2]\n   cmp x6, #0\n   b.lt 8f\n   cmp x6, x1\n   b.ge 8f\n"
        "cmp w3, #32\n   b.gt 8f\n   cmp w4, #32\n   b.gt 8f\n   cmp w5, #32\n   b.gt 8f\n"
        "ldrb w7, [x0, x6]\n   add x6, x6, #1\n   str x6, [x2]\n"
        "sub w8, w7, #97\n   cmp w8, #25\n   b.hi 2f\n"
        "movz w9, #0x61ca\n   movk w9, #0x01bd, lsl #16\n   lsr w9, w9, w8\n   tbnz w9, #0, 9f\n"
        "2:  cmp w7, #97\n   b.ne 4f\n   cmp x6, x1\n   b.ge 3f\n   ldrb w8, [x0, x6]\n   cmp w8, #123\n   b.ne 3f\n"
        "add x6, x6, #1\n   cmp x6, x1\n   b.ge 8f\n   ldrb w8, [x0, x6]\n   add x6, x6, #1\n"
        "sub w8, w8, #97\n   cmp w8, #25\n   b.hi 8f\n"
        "movz w9, #0x61ca\n   movk w9, #0x019d, lsl #16\n   lsr w9, w9, w8\n   tbz w9, #0, 8f\n"
        "str x6, [x2]\n   stp x29, x30, [sp, #-48]!\n   stp x19, x20, [sp, #16]\n   str x21, [sp, #32]\n"
        "mov x19, x0\n   mov x20, x1\n   mov x21, x2\n   add w3, w3, #1\n   add w5, w5, #1\n"
        "bl " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   cbz w0, 7f\n   ldr x6, [x21]\n   cmp x6, x20\n   b.ge 6f\n"
        "ldrb w8, [x19, x6]\n   cmp w8, #125\n   b.ne 6f\n   add x6, x6, #1\n   str x6, [x21]\n"
        "mov w0, #1\n   b 7f\n"
        "3:  add w3, w3, #1\n   b 1b\n"
        "4:  cmp w7, #40\n   b.ne 8f\n   stp x29, x30, [sp, #-80]!\n   stp x19, x20, [sp, #16]\n"
        "stp x21, x22, [sp, #32]\n   stp x23, x24, [sp, #48]\n   str x25, [sp, #64]\n"
        "mov x19, x0\n   mov x20, x1\n   mov x21, x2\n   mov w22, w3\n   add w23, w4, #1\n   mov w24, w5\n   mov w25, #0\n"
        "5:  ldr x6, [x21]\n   cmp x6, x20\n   b.ge 10f\n   ldrb w8, [x19, x6]\n   cmp w8, #41\n   b.eq 11f\n"
        "mov x0, x19\n   mov x1, x20\n   mov x2, x21\n   mov w3, w22\n   mov w4, w23\n   mov w5, w24\n"
        "bl " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   cbz w0, 10f\n   add w25, w25, #1\n   b 5b\n"
        "11: cbz w25, 10f\n   add x6, x6, #1\n   str x6, [x21]\n   mov w0, #1\n   b 12f\n"
        "10: mov w0, #0\n"
        "12: ldr x25, [sp, #64]\n   ldp x23, x24, [sp, #48]\n   ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #80\n" DBUS_ASM_RET
        "6:  mov w0, #0\n"
        "7:  ldr x21, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #48\n" DBUS_ASM_RET
        "8:  mov w0, #0\n" DBUS_ASM_RET
        "9:  mov w0, #1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_sig_one)

        /* dbus_wire_valid_signature(x0=s, x1=n) -> 1 valid, 0 invalid */
        DBUS_ASM_FUNC(dbus_wire_valid_signature)
        "cmp x1, #0\n   b.eq 3f\n   b.lt 2f\n   cmp x1, #255\n   b.gt 2f\n   cbz x0, 2f\n"
        "stp x29, x30, [sp, #-48]!\n   stp x19, x20, [sp, #16]\n   str xzr, [sp, #32]\n"
        "mov x19, x0\n   mov x20, x1\n"
        "1:  ldr x6, [sp, #32]\n   cmp x6, x20\n   b.ge 4f\n   mov x0, x19\n   mov x1, x20\n"
        "add x2, sp, #32\n   mov w3, #0\n   mov w4, #0\n   mov w5, #0\n   bl " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
        "cbnz w0, 1b\n   b 5f\n"
        "4:  mov w0, #1\n"
        "5:  ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #48\n" DBUS_ASM_RET
        "2:  mov w0, #0\n" DBUS_ASM_RET
        "3:  mov w0, #1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_signature)

        /* dbus_wire_variant_sig(x0=p, x1=at, x2=limit) -> sig length or -1 */
        DBUS_ASM_FUNC(dbus_wire_variant_sig)
        "cbz x0, 2f\n   cbz x1, 2f\n   ldr x3, [x1]\n   cmp x3, #0\n   b.lt 2f\n   sub x4, x2, x3\n   cmp x4, #1\n   b.lt 2f\n"
        "ldrb w5, [x0, x3]\n   add x3, x3, #1\n   str x3, [x1]\n   sub x4, x2, x3\n   cmp x4, x5\n   b.le 2f\n"
        "add x6, x3, x5\n   ldrb w7, [x0, x6]\n   cbnz w7, 2f\n"
        "stp x29, x30, [sp, #-48]!\n   stp x19, x20, [sp, #16]\n   str xzr, [sp, #32]\n"
        "mov x19, x1\n   mov x20, x5\n   add x0, x0, x3\n   mov x1, x5\n   add x2, sp, #32\n"
        "mov w3, #0\n   mov w4, #0\n   mov w5, #0\n   bl " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
        "cbz w0, 3f\n   ldr x6, [sp, #32]\n   cmp x6, x20\n   b.ne 3f\n"
        "ldr x7, [x19]\n   add x7, x7, x20\n   add x7, x7, #1\n   str x7, [x19]\n   mov x0, x20\n   b 4f\n"
        "3:  mov x0, #-1\n"
        "4:  ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #48\n" DBUS_ASM_RET
        "2:  mov x0, #-1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_variant_sig)

        /* dbus_wire_fixed_size(w0=c) -> 1, 2, 4, 8, or 0 */
        DBUS_ASM_FUNC(dbus_wire_fixed_size)
        "cmp w0, #121\n   b.eq 1f\n   sub w1, w0, #98\n   cmp w1, #22\n   b.hi 2f\n"
        "movz x2, #0xa032\n   movk x2, #0x4100, lsl #16\n   movk x2, #0x30b0, lsl #32\n"
        "lsl w1, w1, #1\n   lsr x2, x2, x1\n   and x2, x2, #3\n   mov x0, #1\n   lsl x0, x0, x2\n   bic x0, x0, #1\n" DBUS_ASM_RET
        "1:  mov x0, #1\n" DBUS_ASM_RET
        "2:  mov x0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_fixed_size)

        /* dbus_wire_pad_zero(x0=p, x1=at, x2=limit, x3=align) -> 1 ok, 0 fail */
        DBUS_ASM_FUNC(dbus_wire_pad_zero)
        "cbz x0, 3f\n   cbz x1, 3f\n   ldr x4, [x1]\n   cmp x4, #0\n   b.lt 3f\n"
        "sub x5, x3, #1\n   add x6, x4, x5\n   bic x6, x6, x5\n   cmp x6, x2\n   b.gt 3f\n"
        "1:  cmp x4, x6\n   b.hs 2f\n   ldrb w7, [x0, x4]\n   cbnz w7, 3f\n   add x4, x4, #1\n   b 1b\n"
        "2:  str x6, [x1]\n   mov w0, #1\n" DBUS_ASM_RET
        "3:  mov w0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_pad_zero)

        /* dbus_wire_get32(x0=p, w1=big_endian) -> u32 */
        DBUS_ASM_FUNC(dbus_wire_get32)
        "ldr w0, [x0]\n   cbz w1, 1f\n   rev w0, w0\n"
        "1:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_get32)

        /* dbus_wire_value(x0=cursor*) -> 1 valid, 0 invalid */
        DBUS_ASM_FUNC(dbus_wire_value)
        "cbz x0, 99f\n   stp x29, x30, [sp, #-80]!\n   stp x19, x20, [sp, #16]\n"
        "stp x21, x22, [sp, #32]\n   stp x23, x24, [sp, #48]\n   stp x25, x26, [sp, #64]\n"
        "mov x19, x0\n   ldr x1, [x19]\n   cbz x1, 98f\n   ldr x2, [x19, #24]\n   cbz x2, 98f\n"
        "ldr x3, [x19, #8]\n   ldr x4, [x19, #16]\n   cmp x3, #0\n   b.lt 98f\n   cmp x3, x4\n   b.gt 98f\n"
        "ldr x5, [x19, #40]\n   ldr x6, [x19, #32]\n   cmp x5, #0\n   b.lt 98f\n   cmp x5, x6\n   b.ge 98f\n"
        "ldr w7, [x19, #56]\n   cmp w7, #64\n   b.gt 98f\n"
        "ldrb w20, [x2, x5]\n   add x5, x5, #1\n   str x5, [x19, #40]\n"
        "mov w0, w20\n   bl " DBUS_ASM_SYM(dbus_wire_fixed_size) "\n   cbz x0, 10f\n"
        "mov x21, x0\n   ldr x0, [x19]\n   add x1, x19, #8\n   ldr x2, [x19, #16]\n   mov x3, x21\n"
        "bl " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   cbz w0, 98f\n"
        "ldr x3, [x19, #8]\n   ldr x4, [x19, #16]\n   sub x4, x4, x3\n   cmp x4, x21\n   b.lt 98f\n"
        "cmp w20, #98\n   b.eq 2f\n   cmp w20, #104\n   b.ne 3f\n"
        "ldr x0, [x19]\n   add x0, x0, x3\n   ldr w1, [x19, #60]\n   bl " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "ldr x4, [x19, #48]\n   cmp x0, x4\n   b.hs 98f\n   b 3f\n"
        "2:  ldr x0, [x19]\n   add x0, x0, x3\n   ldr w1, [x19, #60]\n   bl " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "cmp w0, #1\n   b.hi 98f\n"
        "3:  ldr x3, [x19, #8]\n   add x3, x3, x21\n   str x3, [x19, #8]\n   b 97f\n"
        /* s / o */
        "10: cmp w20, #115\n   b.eq 11f\n   cmp w20, #111\n   b.ne 20f\n"
        "11: ldr x0, [x19]\n   add x1, x19, #8\n   ldr x2, [x19, #16]\n   mov x3, #4\n"
        "bl " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   cbz w0, 98f\n"
        "ldr x3, [x19, #8]\n   ldr x4, [x19, #16]\n   sub x4, x4, x3\n   cmp x4, #4\n   b.lt 98f\n"
        "ldr x0, [x19]\n   add x0, x0, x3\n   ldr w1, [x19, #60]\n   bl " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "mov x21, x0\n   ldr x3, [x19, #8]\n   add x3, x3, #4\n   str x3, [x19, #8]\n"
        "ldr x4, [x19, #16]\n   sub x4, x4, x3\n   cmp x4, x21\n   b.le 98f\n"
        "ldr x0, [x19]\n   add x22, x0, x3\n   ldrb w5, [x22, x21]\n   cbnz w5, 98f\n"
        "mov x0, x22\n   mov x1, x21\n   bl " DBUS_ASM_SYM(dbus_wire_utf8_valid) "\n   cbz w0, 98f\n"
        "cmp w20, #111\n   b.ne 12f\n   mov x0, x22\n   mov x1, x21\n   bl " DBUS_ASM_SYM(dbus_wire_valid_path) "\n   cbz w0, 98f\n"
        "12: ldr x3, [x19, #8]\n   add x3, x3, x21\n   add x3, x3, #1\n   str x3, [x19, #8]\n   b 97f\n"
        /* g */
        "20: cmp w20, #103\n   b.ne 30f\n   ldr x3, [x19, #8]\n   ldr x4, [x19, #16]\n   sub x4, x4, x3\n   cmp x4, #1\n   b.lt 98f\n"
        "ldr x0, [x19]\n   ldrb w21, [x0, x3]\n   add x3, x3, #1\n   str x3, [x19, #8]\n"
        "ldr x4, [x19, #16]\n   sub x4, x4, x3\n   cmp x4, x21\n   b.le 98f\n"
        "add x22, x0, x3\n   ldrb w5, [x22, x21]\n   cbnz w5, 98f\n"
        "mov x0, x22\n   mov x1, x21\n   bl " DBUS_ASM_SYM(dbus_wire_valid_signature) "\n   cbz w0, 98f\n"
        "ldr x3, [x19, #8]\n   add x3, x3, x21\n   add x3, x3, #1\n   str x3, [x19, #8]\n   b 97f\n"
        /* v */
        "30: cmp w20, #118\n   b.ne 40f\n   ldr x21, [x19, #8]\n   add x21, x21, #1\n"
        "ldr x0, [x19]\n   add x1, x19, #8\n   ldr x2, [x19, #16]\n   bl " DBUS_ASM_SYM(dbus_wire_variant_sig) "\n"
        "cmp x0, #0\n   b.lt 98f\n   ldr x22, [x19, #24]\n   ldr x23, [x19, #32]\n   ldr x24, [x19, #40]\n   ldr w25, [x19, #56]\n"
        "ldr x1, [x19]\n   add x1, x1, x21\n   str x1, [x19, #24]\n   str x0, [x19, #32]\n   str xzr, [x19, #40]\n"
        "add w2, w25, #1\n   str w2, [x19, #56]\n   mov x0, x19\n   bl " DBUS_ASM_SYM(dbus_wire_value) "\n"
        "str x22, [x19, #24]\n   str x23, [x19, #32]\n   str x24, [x19, #40]\n   str w25, [x19, #56]\n   b 96f\n"
        /* a */
        "40: cmp w20, #97\n   b.ne 60f\n   ldr x21, [x19, #40]\n   ldr x4, [x19, #32]\n   cmp x21, x4\n   b.ge 98f\n"
        "ldr x0, [x19]\n   add x1, x19, #8\n   ldr x2, [x19, #16]\n   mov x3, #4\n"
        "bl " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   cbz w0, 98f\n"
        "ldr x3, [x19, #8]\n   ldr x4, [x19, #16]\n   sub x4, x4, x3\n   cmp x4, #4\n   b.lt 98f\n"
        "ldr x0, [x19]\n   add x0, x0, x3\n   ldr w1, [x19, #60]\n   bl " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "mov x22, x0\n   ldr x3, [x19, #8]\n   add x3, x3, #4\n   str x3, [x19, #8]\n   mov x23, x3\n"
        "ldr x2, [x19, #24]\n   ldrb w24, [x2, x21]\n"
        "cmp w24, #123\n   b.eq 41f\n   cmp w24, #40\n   b.eq 41f\n"
        "mov w0, w24\n   bl " DBUS_ASM_SYM(dbus_wire_fixed_size) "\n   cbnz x0, 42f\n"
        "cmp w24, #115\n   b.eq 43f\n   cmp w24, #111\n   b.eq 43f\n   cmp w24, #97\n   b.eq 43f\n   mov x0, #1\n   b 42f\n"
        "41: mov x0, #8\n   b 42f\n"
        "43: mov x0, #4\n"
        "42: mov x3, x0\n   ldr x0, [x19]\n   add x1, x19, #8\n   ldr x2, [x19, #16]\n"
        "bl " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   cbz w0, 98f\n"
        "mov x1, #0x04000000\n   cmp x22, x1\n   b.hi 98f\n   ldr x3, [x19, #8]\n   ldr x4, [x19, #16]\n"
        "sub x4, x4, x3\n   cmp x22, x4\n   b.hi 98f\n   add x25, x3, x22\n"
        "sub sp, sp, #48\n   sub x5, x21, #1\n   str x5, [sp]\n   ldr x0, [x19, #24]\n   ldr x1, [x19, #32]\n"
        "mov x2, sp\n   mov w3, #0\n   mov w4, #0\n   mov w5, #0\n   bl " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
        "cbz w0, 49f\n   ldr x4, [x19, #16]\n   ldr w5, [x19, #56]\n   str x4, [sp, #8]\n   str w5, [sp, #16]\n"
        "str xzr, [sp, #24]\n   str xzr, [sp, #32]\n   str x25, [x19, #16]\n"
        "44: ldr x3, [x19, #8]\n   cmp x3, x25\n   b.ge 47f\n   str x3, [sp, #24]\n"
        "ldr x6, [sp, #32]\n   add x6, x6, #1\n   str x6, [sp, #32]\n   cmp w24, #123\n   b.ne 45f\n"
        "ldr x0, [x19]\n   add x1, x19, #8\n   mov x2, x25\n   mov x3, #8\n"
        "bl " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   cbz w0, 48f\n   add x6, x21, #1\n   str x6, [x19, #40]\n"
        "ldr w5, [sp, #16]\n   add w5, w5, #2\n   str w5, [x19, #56]\n"
        "mov x0, x19\n   bl " DBUS_ASM_SYM(dbus_wire_value) "\n   cbz w0, 48f\n"
        "mov x0, x19\n   bl " DBUS_ASM_SYM(dbus_wire_value) "\n   cbnz w0, 44b\n   b 48f\n"
        "45: str x21, [x19, #40]\n   ldr w5, [sp, #16]\n   add w5, w5, #1\n   str w5, [x19, #56]\n"
        "mov x0, x19\n   bl " DBUS_ASM_SYM(dbus_wire_value) "\n   cbnz w0, 44b\n"
        "48: ldr x4, [sp, #8]\n   ldr w5, [sp, #16]\n   str x4, [x19, #16]\n   str w5, [x19, #56]\n"
        "49: add sp, sp, #48\n   b 98f\n"
        "47: ldr x4, [sp, #8]\n   ldr w5, [sp, #16]\n   str x4, [x19, #16]\n   str w5, [x19, #56]\n"
        "ldr x3, [x19, #8]\n   cmp x3, x25\n   b.ne 49b\n"
        "cmp w24, #40\n   b.eq 50f\n   cmp w24, #123\n   b.ne 51f\n"
        "50: add x6, x23, #4\n   sub x7, x25, x22\n   cmp x6, x7\n   b.ne 51f\n"
        "ldr x6, [sp, #32]\n   cmp x6, #1\n   b.le 51f\n   ldr x6, [sp, #24]\n   sub x7, x25, #4\n   cmp x6, x7\n   b.ge 49b\n"
        "51: ldr x6, [sp]\n   str x6, [x19, #40]\n   add sp, sp, #48\n   b 97f\n"
        /* ( */
        "60: cmp w20, #40\n   b.ne 98f\n   ldr x0, [x19]\n   add x1, x19, #8\n   ldr x2, [x19, #16]\n   mov x3, #8\n"
        "bl " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   cbz w0, 98f\n"
        "ldr w21, [x19, #56]\n   add w2, w21, #1\n   str w2, [x19, #56]\n   mov x22, #0\n"
        "61: ldr x5, [x19, #40]\n   ldr x6, [x19, #32]\n   cmp x5, x6\n   b.ge 62f\n"
        "ldr x2, [x19, #24]\n   ldrb w3, [x2, x5]\n   cmp w3, #41\n   b.eq 63f\n"
        "mov x0, x19\n   bl " DBUS_ASM_SYM(dbus_wire_value) "\n   cbz w0, 62f\n   add x22, x22, #1\n   b 61b\n"
        "63: str w21, [x19, #56]\n   cbz x22, 98f\n   add x5, x5, #1\n   str x5, [x19, #40]\n   b 97f\n"
        "62: str w21, [x19, #56]\n   b 98f\n"
        "97: mov w0, #1\n"
        "96: ldp x25, x26, [sp, #64]\n   ldp x23, x24, [sp, #48]\n   ldp x21, x22, [sp, #32]\n"
        "ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #80\n" DBUS_ASM_RET
        "98: mov w0, #0\n   b 96b\n"
        "99: mov w0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_value)

        /* dbus_wire_body_valid(x0=body, x1=len, x2=sig, x3=sig_len, x4=fds, w5=be) */
        DBUS_ASM_FUNC(dbus_wire_body_valid)
        "cmp x1, #0\n   b.lt 3f\n   cmp x3, #0\n   b.lt 3f\n   cbz x2, 3f\n"
        "cbnz x3, 10f\n   cmp x1, #0\n   cset w0, eq\n" DBUS_ASM_RET
        "10: cbz x0, 3f\n   stp x29, x30, [sp, #-80]!\n"
        "str x0, [sp, #16]\n   str xzr, [sp, #24]\n   str x1, [sp, #32]\n   str x2, [sp, #40]\n"
        "str x3, [sp, #48]\n   str xzr, [sp, #56]\n   str x4, [sp, #64]\n   str wzr, [sp, #72]\n   str w5, [sp, #76]\n"
        "1:  ldr x6, [sp, #56]\n   ldr x7, [sp, #48]\n   cmp x6, x7\n   b.ge 2f\n"
        "add x0, sp, #16\n   bl " DBUS_ASM_SYM(dbus_wire_value) "\n   cbnz w0, 1b\n"
        "ldp x29, x30, [sp], #80\n" DBUS_ASM_RET
        "2:  ldr x6, [sp, #24]\n   ldr x7, [sp, #32]\n   cmp x6, x7\n   cset w0, eq\n"
        "ldp x29, x30, [sp], #80\n" DBUS_ASM_RET
        "3:  mov w0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_body_valid)

        /* dbus_wire_header_check(x0=b, x1=size, x2=max_size, x3=out) -> 1 valid, 0 invalid */
        DBUS_ASM_FUNC(dbus_wire_header_check)
        "cbz x0, 5f\n   cbz x3, 5f\n   cmp x1, #16\n   b.lt 5f\n   cmp x1, x2\n   b.gt 5f\n"
        "ldrb w4, [x0]\n   mov w5, #0\n   cmp w4, #108\n   b.eq 1f\n   cmp w4, #66\n   b.ne 5f\n   mov w5, #1\n"
        "1:  ldrb w6, [x0, #3]\n   cmp w6, #1\n   b.ne 5f\n   ldrb w7, [x0, #1]\n   sub w8, w7, #1\n   cmp w8, #3\n   b.hi 5f\n"
        "ldrb w8, [x0, #2]\n   ldr w9, [x0, #4]\n   ldr w10, [x0, #8]\n   ldr w11, [x0, #12]\n"
        "cbz w5, 2f\n   rev w9, w9\n   rev w10, w10\n   rev w11, w11\n"
        "2:  cbz w10, 5f\n   sub x12, x1, #16\n   cmp x11, x12\n   b.hi 5f\n   cmp x9, x1\n   b.hi 5f\n"
        "add x12, x11, #16\n   add x13, x12, #7\n   bic x13, x13, #7\n   add x14, x13, x9\n   cmp x14, x1\n   b.ne 5f\n"
        "mov x14, x12\n"
        "3:  cmp x14, x13\n   b.hs 4f\n   ldrb w15, [x0, x14]\n   cbnz w15, 5f\n   add x14, x14, #1\n   b 3b\n"
        "4:  str w10, [x3]\n   str w9, [x3, #4]\n   str w11, [x3, #8]\n   strb w7, [x3, #12]\n"
        "strb w8, [x3, #13]\n   strb w5, [x3, #14]\n   strb wzr, [x3, #15]\n   stp x12, x13, [x3, #16]\n"
        "mov w0, #1\n" DBUS_ASM_RET
        "5:  mov w0, #0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_header_check)

        /* dbus_wire_o_pad(x0=w, x1=align) */
        DBUS_ASM_FUNC(dbus_wire_o_pad)
        "cbz x0, 3f\n   ldr w2, [x0, #24]\n   cbnz w2, 3f\n   ldr x2, [x0, #8]\n   ldr x3, [x0, #16]\n"
        "sub x4, x1, #1\n   add x5, x2, x4\n   bic x5, x5, x4\n   cmp x5, x3\n   b.gt 2f\n   ldr x6, [x0]\n"
        "1:  cmp x2, x5\n   b.hs 4f\n   strb wzr, [x6, x2]\n   add x2, x2, #1\n   b 1b\n"
        "4:  str x5, [x0, #8]\n" DBUS_ASM_RET
        "2:  mov w2, #1\n   str w2, [x0, #24]\n"
        "3:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_pad)

        /* dbus_wire_o_u32(x0=w, w1=v) */
        DBUS_ASM_FUNC(dbus_wire_o_u32)
        "cbz x0, 3f\n   ldr w2, [x0, #24]\n   cbnz w2, 3f\n   ldr x2, [x0, #8]\n   ldr x3, [x0, #16]\n"
        "add x4, x2, #3\n   bic x4, x4, #3\n   cmp x4, x3\n   b.gt 2f\n   sub x5, x3, x4\n   cmp x5, #4\n   b.lt 2f\n"
        "ldr x6, [x0]\n"
        "1:  cmp x2, x4\n   b.hs 4f\n   strb wzr, [x6, x2]\n   add x2, x2, #1\n   b 1b\n"
        "4:  str w1, [x6, x4]\n   add x4, x4, #4\n   str x4, [x0, #8]\n" DBUS_ASM_RET
        "2:  mov w2, #1\n   str w2, [x0, #24]\n"
        "3:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_u32)

        /* dbus_wire_o_string(x0=w, x1=s) */
        DBUS_ASM_FUNC(dbus_wire_o_string)
        "cbz x0, 5f\n   ldr w2, [x0, #24]\n   cbnz w2, 5f\n   cbz x1, 4f\n   mov x2, #0\n"
        "1:  ldrb w3, [x1, x2]\n   cbz w3, 2f\n   add x2, x2, #1\n   b 1b\n"
        "2:  ldr x3, [x0, #8]\n   ldr x4, [x0, #16]\n   add x5, x3, #3\n   bic x5, x5, #3\n"
        "cmp x5, x4\n   b.gt 4f\n   sub x6, x4, x5\n   add x7, x2, #5\n   cmp x6, x7\n   b.lt 4f\n"
        "ldr x8, [x0]\n"
        "3:  cmp x3, x5\n   b.hs 6f\n   strb wzr, [x8, x3]\n   add x3, x3, #1\n   b 3b\n"
        "6:  str w2, [x8, x5]\n   add x5, x5, #4\n   add x9, x8, x5\n   add x6, x2, #1\n   mov x7, #0\n"
        "8:  sub x11, x6, x7\n   cmp x11, #8\n   b.lo 7f\n   ldr x10, [x1, x7]\n   str x10, [x9, x7]\n   add x7, x7, #8\n   b 8b\n"
        "7:  cmp x7, x6\n   b.hs 9f\n   ldrb w10, [x1, x7]\n   strb w10, [x9, x7]\n   add x7, x7, #1\n   b 7b\n"
        "9:  add x5, x5, x6\n   str x5, [x0, #8]\n" DBUS_ASM_RET
        "4:  mov w2, #1\n   str w2, [x0, #24]\n"
        "5:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_string)
);

#elif defined(DBUS_WIRE_X64)

#if defined(_WIN32)
# define DW_LOAD1 "mov r8, rcx\n"
# define DW_LOAD2 "mov r8, rcx\n   mov r9, rdx\n"
# define DW_LOAD3 "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define DW_LOAD4 "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
# define DW_ARG1  "rcx"
# define DW_ARG1D "ecx"
# define DW_ARG2  "rdx"
# define DW_ARG2D "edx"
# define DW_ARG3  "r8"
# define DW_ARG4  "r9"
# define DW_ARG4D "r9d"
#else
# define DW_LOAD1 "mov r8, rdi\n"
# define DW_LOAD2 "mov r8, rdi\n   mov r9, rsi\n"
# define DW_LOAD3 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define DW_LOAD4 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
# define DW_ARG1  "rdi"
# define DW_ARG1D "edi"
# define DW_ARG2  "rsi"
# define DW_ARG2D "esi"
# define DW_ARG3  "rdx"
# define DW_ARG4  "rcx"
# define DW_ARG4D "ecx"
#endif

__asm__(
        DBUS_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* dbus_wire_utf8_valid(s, n) */
        DBUS_ASM_FUNC(dbus_wire_utf8_valid)
        DW_LOAD2
        "test r9, r9\n   jz 9f\n   js 8f\n   test r8, r8\n   jz 8f\n   add r9, r8\n"
        "mov r10, 0x0101010101010101\n   mov r11, 0x8080808080808080\n"
        "1:  mov rax, r9\n   sub rax, r8\n   cmp rax, 8\n   jb 2f\n   mov rcx, [r8]\n"
        "mov rdx, rcx\n   sub rdx, r10\n   or rdx, rcx\n   test rdx, r11\n   jnz 2f\n   add r8, 8\n   jmp 1b\n"
        "2:  cmp r8, r9\n   jae 9f\n   movzx eax, byte ptr [r8]\n   test eax, eax\n   jz 8f\n   cmp eax, 0x80\n   jae 3f\n"
        "inc r8\n   jmp 1b\n"
        "3:  cmp eax, 0xc2\n   jb 8f\n   cmp eax, 0xe0\n   jae 4f\n"
        "mov rcx, r9\n   sub rcx, r8\n   cmp rcx, 2\n   jb 8f\n   movzx edx, byte ptr [r8 + 1]\n"
        "and edx, 0xc0\n   cmp edx, 0x80\n   jne 8f\n   add r8, 2\n   jmp 1b\n"
        "4:  cmp eax, 0xf0\n   jae 6f\n   mov rcx, r9\n   sub rcx, r8\n   cmp rcx, 3\n   jb 8f\n"
        "movzx ecx, byte ptr [r8 + 1]\n   mov edx, ecx\n   and edx, 0xc0\n   cmp edx, 0x80\n   jne 8f\n"
        "movzx edx, byte ptr [r8 + 2]\n   and edx, 0xc0\n   cmp edx, 0x80\n   jne 8f\n"
        "cmp eax, 0xe0\n   jne 5f\n   cmp ecx, 0xa0\n   jb 8f\n   add r8, 3\n   jmp 1b\n"
        "5:  cmp eax, 0xed\n   jne 7f\n   cmp ecx, 0xa0\n   jae 8f\n"
        "7:  add r8, 3\n   jmp 1b\n"
        "6:  cmp eax, 0xf5\n   jae 8f\n   mov rcx, r9\n   sub rcx, r8\n   cmp rcx, 4\n   jb 8f\n"
        "movzx ecx, byte ptr [r8 + 1]\n   mov edx, ecx\n   and edx, 0xc0\n   cmp edx, 0x80\n   jne 8f\n"
        "movzx edx, byte ptr [r8 + 2]\n   and edx, 0xc0\n   cmp edx, 0x80\n   jne 8f\n"
        "movzx edx, byte ptr [r8 + 3]\n   and edx, 0xc0\n   cmp edx, 0x80\n   jne 8f\n"
        "cmp eax, 0xf0\n   jne 10f\n   cmp ecx, 0x90\n   jb 8f\n   add r8, 4\n   jmp 1b\n"
        "10: cmp eax, 0xf4\n   jne 11f\n   cmp ecx, 0x90\n   jae 8f\n"
        "11: add r8, 4\n   jmp 1b\n"
        "8:  xor eax, eax\n" DBUS_ASM_RET
        "9:  mov eax, 1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_utf8_valid)

        /* dbus_wire_valid_path(s, n) */
        DBUS_ASM_FUNC(dbus_wire_valid_path)
        DW_LOAD2
        "test r8, r8\n   jz 5f\n   lea rax, [r9 - 1]\n   cmp rax, 254\n   ja 5f\n"
        "cmp byte ptr [r8], 47\n   jne 5f\n   cmp r9, 1\n   je 6f\n   mov rcx, 1\n   mov rdx, 1\n"
        "1:  cmp rcx, r9\n   je 2f\n   movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 47\n   jne 3f\n"
        "2:  cmp rcx, rdx\n   je 5f\n   lea rdx, [rcx + 1]\n   cmp rcx, r9\n   je 4f\n   inc rcx\n   jmp 1b\n"
        "3:  mov r10d, eax\n   or r10d, 32\n   sub r10d, 97\n   cmp r10d, 25\n   jbe 7f\n"
        "lea r10d, [rax - 48]\n   cmp r10d, 9\n   jbe 7f\n   cmp eax, 95\n   jne 5f\n"
        "7:  inc rcx\n   jmp 1b\n"
        "4:  cmp byte ptr [r8 + r9 - 1], 47\n   je 5f\n"
        "6:  mov eax, 1\n" DBUS_ASM_RET
        "5:  xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_path)

        /* dbus_wire_valid_name(s, n, kind) */
        DBUS_ASM_FUNC(dbus_wire_valid_name)
        DW_LOAD3
        "test r8, r8\n   jz 8f\n   lea rax, [r9 - 1]\n   cmp rax, 254\n   ja 8f\n"
        "cmp r10d, 2\n   jne 2f\n   movzx eax, byte ptr [r8]\n   mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n"
        "cmp ecx, 25\n   jbe 10f\n   cmp eax, 95\n   jne 8f\n"
        "10: mov rcx, 1\n"
        "1:  cmp rcx, r9\n   jae 9f\n   movzx eax, byte ptr [r8 + rcx]\n   inc rcx\n"
        "mov edx, eax\n   or edx, 32\n   sub edx, 97\n   cmp edx, 25\n   jbe 1b\n"
        "lea edx, [rax - 48]\n   cmp edx, 9\n   jbe 1b\n   cmp eax, 95\n   je 1b\n   jmp 8f\n"
        "2:  push rbx\n   xor rcx, rcx\n   xor rdx, rdx\n   xor r11d, r11d\n   cmp r10d, 1\n   jne 3f\n"
        "cmp byte ptr [r8], 58\n   jne 3f\n   mov r11d, 1\n   mov rcx, 1\n   mov rdx, 1\n"
        "3:  xor ebx, ebx\n"
        "4:  cmp rcx, r9\n   je 5f\n   movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 46\n   jne 6f\n"
        "5:  cmp rcx, rdx\n   je 13f\n   lea rdx, [rcx + 1]\n   cmp rcx, r9\n   je 7f\n"
        "inc ebx\n   inc rcx\n   jmp 4b\n"
        "6:  push r11\n   mov r11d, eax\n   or r11d, 32\n   sub r11d, 97\n   cmp r11d, 25\n   pop r11\n   jbe 11f\n"
        "cmp eax, 95\n   je 11f\n   cmp r10d, 1\n   jne 12f\n   cmp eax, 45\n   je 11f\n"
        "12: sub eax, 48\n   cmp eax, 9\n   ja 13f\n   test r11d, r11d\n   jnz 11f\n   cmp rcx, rdx\n   je 13f\n"
        "11: inc rcx\n   jmp 4b\n"
        "7:  test ebx, ebx\n   jz 13f\n   cmp byte ptr [r8 + r9 - 1], 46\n   je 13f\n   pop rbx\n"
        "9:  mov eax, 1\n" DBUS_ASM_RET
        "13: pop rbx\n"
        "8:  xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_name)

        /* dbus_wire_sig_one(s, n, at, arrays, structs, dicts) */
        DBUS_ASM_FUNC(dbus_wire_sig_one)
#if defined(_WIN32)
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n   push rdi\n   push rsi\n"
        "mov r12, rcx\n   mov r13, rdx\n   mov r14, r8\n   mov r15d, r9d\n"
        "mov edi, [rsp + 96]\n   mov esi, [rsp + 104]\n"
#else
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, rdi\n   mov r13, rsi\n   mov r14, rdx\n   mov r15d, ecx\n   mov ebx, r8d\n   mov r10d, r9d\n"
#endif
        "test r12, r12\n   jz 8f\n   test r14, r14\n   jz 8f\n"
        "1:  mov rax, [r14]\n   test rax, rax\n   js 8f\n   cmp rax, r13\n   jge 8f\n"
#if defined(_WIN32)
        "cmp r15d, 32\n   jg 8f\n   cmp edi, 32\n   jg 8f\n   cmp esi, 32\n   jg 8f\n"
#else
        "cmp r15d, 32\n   jg 8f\n   cmp ebx, 32\n   jg 8f\n   cmp r10d, 32\n   jg 8f\n"
#endif
        "movzx ecx, byte ptr [r12 + rax]\n   inc rax\n   mov [r14], rax\n"
        "lea edx, [rcx - 97]\n   cmp edx, 25\n   ja 2f\n"
        "mov r11d, 0x01bd61ca\n   bt r11d, edx\n   jc 9f\n"
        "2:  cmp ecx, 97\n   jne 4f\n   cmp rax, r13\n   jge 3f\n   cmp byte ptr [r12 + rax], 123\n   jne 3f\n"
        "inc rax\n   cmp rax, r13\n   jge 8f\n   movzx edx, byte ptr [r12 + rax]\n   inc rax\n"
        "sub edx, 97\n   cmp edx, 25\n   ja 8f\n   mov r11d, 0x019d61ca\n   bt r11d, edx\n   jnc 8f\n"
        "mov [r14], rax\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov rcx, r12\n   mov rdx, r13\n   mov r8, r14\n   lea r9d, [r15 + 1]\n"
        "mov [rsp + 32], edi\n   lea eax, [rsi + 1]\n   mov [rsp + 40], eax\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   add rsp, 48\n"
#else
        "mov rdi, r12\n   mov rsi, r13\n   mov rdx, r14\n   lea ecx, [r15 + 1]\n   mov r8d, ebx\n   lea r9d, [r10 + 1]\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
#endif
        "test eax, eax\n   jz 8f\n   mov rax, [r14]\n   cmp rax, r13\n   jge 8f\n"
        "cmp byte ptr [r12 + rax], 125\n   jne 8f\n   inc rax\n   mov [r14], rax\n   jmp 9f\n"
        "3:  inc r15d\n   jmp 1b\n"
        "4:  cmp ecx, 40\n   jne 8f\n"
#if defined(_WIN32)
        "inc edi\n   xor ebx, ebx\n"
        "5:  mov rax, [r14]\n   cmp rax, r13\n   jge 8f\n   cmp byte ptr [r12 + rax], 41\n   je 6f\n"
        "sub rsp, 48\n   mov rcx, r12\n   mov rdx, r13\n   mov r8, r14\n   mov r9d, r15d\n"
        "mov [rsp + 32], edi\n   mov [rsp + 40], esi\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   add rsp, 48\n"
        "test eax, eax\n   jz 8f\n   inc ebx\n   jmp 5b\n"
        "6:  test ebx, ebx\n   jz 8f\n   inc rax\n   mov [r14], rax\n   jmp 9f\n"
#else
        "inc ebx\n   push rbp\n   push r10\n   sub rsp, 8\n   xor ebp, ebp\n"
        "5:  mov rax, [r14]\n   cmp rax, r13\n   jge 10f\n   cmp byte ptr [r12 + rax], 41\n   je 6f\n"
        "mov rdi, r12\n   mov rsi, r13\n   mov rdx, r14\n   mov ecx, r15d\n   mov r8d, ebx\n   mov r9d, [rsp + 8]\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   test eax, eax\n   jz 10f\n   inc ebp\n   jmp 5b\n"
        "6:  test ebp, ebp\n   jz 10f\n   inc rax\n   mov [r14], rax\n   add rsp, 8\n   pop r10\n   pop rbp\n   jmp 9f\n"
        "10: add rsp, 8\n   pop r10\n   pop rbp\n"
#endif
        "8:  xor eax, eax\n   jmp 11f\n"
        "9:  mov eax, 1\n"
#if defined(_WIN32)
        "11: pop rsi\n   pop rdi\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" DBUS_ASM_RET
#else
        "11: pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" DBUS_ASM_RET
#endif
        DBUS_ASM_END(dbus_wire_sig_one)

        /* dbus_wire_valid_signature(s, n) */
        DBUS_ASM_FUNC(dbus_wire_valid_signature)
        DW_LOAD2
        "test r9, r9\n   jz 3f\n   js 2f\n   cmp r9, 255\n   jg 2f\n   test r8, r8\n   jz 2f\n"
        "push r12\n   push r13\n   sub rsp, 24\n   mov r12, r8\n   mov r13, r9\n   mov qword ptr [rsp], 0\n"
        "1:  cmp qword ptr [rsp], r13\n   jge 4f\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov rcx, r12\n   mov rdx, r13\n   lea r8, [rsp + 48]\n   xor r9d, r9d\n"
        "mov dword ptr [rsp + 32], 0\n   mov dword ptr [rsp + 40], 0\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   add rsp, 48\n"
#else
        "mov rdi, r12\n   mov rsi, r13\n   mov rdx, rsp\n   xor ecx, ecx\n   xor r8d, r8d\n   xor r9d, r9d\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
#endif
        "test eax, eax\n   jnz 1b\n   jmp 5f\n"
        "4:  mov eax, 1\n"
        "5:  add rsp, 24\n   pop r13\n   pop r12\n" DBUS_ASM_RET
        "2:  xor eax, eax\n" DBUS_ASM_RET
        "3:  mov eax, 1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_signature)

        /* dbus_wire_variant_sig(p, at, limit) */
        DBUS_ASM_FUNC(dbus_wire_variant_sig)
        DW_LOAD3
        "test r8, r8\n   jz 2f\n   test r9, r9\n   jz 2f\n   mov rax, [r9]\n   test rax, rax\n   js 2f\n"
        "mov rcx, r10\n   sub rcx, rax\n   cmp rcx, 1\n   jl 2f\n   movzx r11d, byte ptr [r8 + rax]\n"
        "inc rax\n   mov [r9], rax\n   mov rcx, r10\n   sub rcx, rax\n   cmp rcx, r11\n   jle 2f\n"
        "lea rdx, [rax + r11]\n   cmp byte ptr [r8 + rdx], 0\n   jne 2f\n"
        "push r12\n   push r13\n   sub rsp, 24\n   mov r12, r9\n   mov r13, r11\n   mov qword ptr [rsp], 0\n"
#if defined(_WIN32)
        "lea rcx, [r8 + rax]\n   mov rdx, r11\n   lea r8, [rsp]\n   xor r9d, r9d\n"
        "sub rsp, 48\n   mov dword ptr [rsp + 32], 0\n   mov dword ptr [rsp + 40], 0\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   add rsp, 48\n"
#else
        "lea rdi, [r8 + rax]\n   mov rsi, r11\n   mov rdx, rsp\n   xor ecx, ecx\n   xor r8d, r8d\n   xor r9d, r9d\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
#endif
        "test eax, eax\n   jz 3f\n   cmp qword ptr [rsp], r13\n   jne 3f\n"
        "lea rax, [r13 + 1]\n   add [r12], rax\n   mov rax, r13\n   jmp 4f\n"
        "3:  mov rax, -1\n"
        "4:  add rsp, 24\n   pop r13\n   pop r12\n" DBUS_ASM_RET
        "2:  mov rax, -1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_variant_sig)

        /* dbus_wire_fixed_size(c) */
        DBUS_ASM_FUNC(dbus_wire_fixed_size)
        DW_LOAD1
        "cmp r8d, 121\n   je 1f\n   lea ecx, [r8d - 98]\n   cmp ecx, 22\n   ja 2f\n"
        "mov rax, 0x000030b04100a032\n   add ecx, ecx\n   shr rax, cl\n   and ecx, 0\n"
        "mov ecx, eax\n   and ecx, 3\n   mov eax, 1\n   shl rax, cl\n   and rax, -2\n" DBUS_ASM_RET
        "1:  mov eax, 1\n" DBUS_ASM_RET
        "2:  xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_fixed_size)

        /* dbus_wire_pad_zero(p, at, limit, align) */
        DBUS_ASM_FUNC(dbus_wire_pad_zero)
        DW_LOAD4
        "test r8, r8\n   jz 3f\n   test r9, r9\n   jz 3f\n   mov rax, [r9]\n   test rax, rax\n   js 3f\n"
        "lea rcx, [r11 - 1]\n   lea rdx, [rax + rcx]\n   not rcx\n   and rdx, rcx\n   cmp rdx, r10\n   jg 3f\n"
        "1:  cmp rax, rdx\n   jae 2f\n   cmp byte ptr [r8 + rax], 0\n   jne 3f\n   inc rax\n   jmp 1b\n"
        "2:  mov [r9], rdx\n   mov eax, 1\n" DBUS_ASM_RET
        "3:  xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_pad_zero)

        /* dbus_wire_get32(p, big_endian) */
        DBUS_ASM_FUNC(dbus_wire_get32)
        DW_LOAD2
        "mov eax, [r8]\n   test r9d, r9d\n   jz 1f\n   bswap eax\n"
        "1:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_get32)

        /* dbus_wire_value(cursor*) */
        DBUS_ASM_FUNC(dbus_wire_value)
        DW_LOAD1
        "test r8, r8\n   jz 99f\n   push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 104\n"
        "mov rbx, r8\n   mov rcx, [rbx]\n   test rcx, rcx\n   jz 98f\n   mov rdx, [rbx + 24]\n   test rdx, rdx\n   jz 98f\n"
        "mov rax, [rbx + 8]\n   test rax, rax\n   js 98f\n   cmp rax, [rbx + 16]\n   jg 98f\n"
        "mov r9, [rbx + 40]\n   test r9, r9\n   js 98f\n   cmp r9, [rbx + 32]\n   jge 98f\n"
        "cmp dword ptr [rbx + 56], 64\n   jg 98f\n"
        "movzx r12d, byte ptr [rdx + r9]\n   inc r9\n   mov [rbx + 40], r9\n"
        "mov " DW_ARG1D ", r12d\n   call " DBUS_ASM_SYM(dbus_wire_fixed_size) "\n   test rax, rax\n   jz 10f\n"
        "mov r13, rax\n   mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rbx + 16]\n   mov " DW_ARG4 ", r13\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   test eax, eax\n   jz 98f\n"
        "mov rax, [rbx + 16]\n   sub rax, [rbx + 8]\n   cmp rax, r13\n   jl 98f\n"
        "cmp r12d, 98\n   je 2f\n   cmp r12d, 104\n   jne 3f\n"
        "mov " DW_ARG1 ", [rbx]\n   add " DW_ARG1 ", [rbx + 8]\n   mov " DW_ARG2D ", [rbx + 60]\n"
        "call " DBUS_ASM_SYM(dbus_wire_get32) "\n   mov eax, eax\n   cmp rax, [rbx + 48]\n   jae 98f\n   jmp 3f\n"
        "2:  mov " DW_ARG1 ", [rbx]\n   add " DW_ARG1 ", [rbx + 8]\n   mov " DW_ARG2D ", [rbx + 60]\n"
        "call " DBUS_ASM_SYM(dbus_wire_get32) "\n   cmp eax, 1\n   ja 98f\n"
        "3:  add [rbx + 8], r13\n   jmp 97f\n"
        /* s / o */
        "10: cmp r12d, 115\n   je 11f\n   cmp r12d, 111\n   jne 20f\n"
        "11: mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rbx + 16]\n   mov " DW_ARG4 ", 4\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   test eax, eax\n   jz 98f\n"
        "mov rax, [rbx + 16]\n   sub rax, [rbx + 8]\n   cmp rax, 4\n   jl 98f\n"
        "mov " DW_ARG1 ", [rbx]\n   add " DW_ARG1 ", [rbx + 8]\n   mov " DW_ARG2D ", [rbx + 60]\n"
        "call " DBUS_ASM_SYM(dbus_wire_get32) "\n   mov r13d, eax\n   add qword ptr [rbx + 8], 4\n"
        "mov rax, [rbx + 16]\n   sub rax, [rbx + 8]\n   cmp rax, r13\n   jle 98f\n"
        "mov r14, [rbx]\n   add r14, [rbx + 8]\n   cmp byte ptr [r14 + r13], 0\n   jne 98f\n"
        "mov " DW_ARG1 ", r14\n   mov " DW_ARG2 ", r13\n   call " DBUS_ASM_SYM(dbus_wire_utf8_valid) "\n   test eax, eax\n   jz 98f\n"
        "cmp r12d, 111\n   jne 12f\n   mov " DW_ARG1 ", r14\n   mov " DW_ARG2 ", r13\n   call " DBUS_ASM_SYM(dbus_wire_valid_path) "\n   test eax, eax\n   jz 98f\n"
        "12: lea rax, [r13 + 1]\n   add [rbx + 8], rax\n   jmp 97f\n"
        /* g */
        "20: cmp r12d, 103\n   jne 30f\n   mov rax, [rbx + 16]\n   sub rax, [rbx + 8]\n   cmp rax, 1\n   jl 98f\n"
        "mov rcx, [rbx]\n   mov rdx, [rbx + 8]\n   movzx r13d, byte ptr [rcx + rdx]\n   inc rdx\n   mov [rbx + 8], rdx\n"
        "mov rax, [rbx + 16]\n   sub rax, rdx\n   cmp rax, r13\n   jle 98f\n"
        "lea r14, [rcx + rdx]\n   cmp byte ptr [r14 + r13], 0\n   jne 98f\n"
        "mov " DW_ARG1 ", r14\n   mov " DW_ARG2 ", r13\n   call " DBUS_ASM_SYM(dbus_wire_valid_signature) "\n   test eax, eax\n   jz 98f\n"
        "lea rax, [r13 + 1]\n   add [rbx + 8], rax\n   jmp 97f\n"
        /* v */
        "30: cmp r12d, 118\n   jne 40f\n   mov r13, [rbx + 8]\n   inc r13\n"
        "mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rbx + 16]\n"
        "call " DBUS_ASM_SYM(dbus_wire_variant_sig) "\n   test rax, rax\n   js 98f\n"
        "mov r14, [rbx + 24]\n   mov r15, [rbx + 32]\n   mov rbp, [rbx + 40]\n   mov r12d, [rbx + 56]\n"
        "mov rcx, [rbx]\n   add rcx, r13\n   mov [rbx + 24], rcx\n   mov [rbx + 32], rax\n   mov qword ptr [rbx + 40], 0\n"
        "lea eax, [r12d + 1]\n   mov [rbx + 56], eax\n   mov " DW_ARG1 ", rbx\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n"
        "mov [rbx + 24], r14\n   mov [rbx + 32], r15\n   mov [rbx + 40], rbp\n   mov [rbx + 56], r12d\n   jmp 96f\n"
        /* a */
        "40: cmp r12d, 97\n   jne 60f\n   mov r13, [rbx + 40]\n   cmp r13, [rbx + 32]\n   jge 98f\n"
        "mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rbx + 16]\n   mov " DW_ARG4 ", 4\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   test eax, eax\n   jz 98f\n"
        "mov rax, [rbx + 16]\n   sub rax, [rbx + 8]\n   cmp rax, 4\n   jl 98f\n"
        "mov " DW_ARG1 ", [rbx]\n   add " DW_ARG1 ", [rbx + 8]\n   mov " DW_ARG2D ", [rbx + 60]\n"
        "call " DBUS_ASM_SYM(dbus_wire_get32) "\n   mov r14d, eax\n   add qword ptr [rbx + 8], 4\n   mov r15, [rbx + 8]\n"
        "mov rcx, [rbx + 24]\n   movzx ebp, byte ptr [rcx + r13]\n"
        "cmp ebp, 123\n   je 41f\n   cmp ebp, 40\n   je 41f\n   mov " DW_ARG1D ", ebp\n   call " DBUS_ASM_SYM(dbus_wire_fixed_size) "\n"
        "test rax, rax\n   jnz 42f\n   cmp ebp, 115\n   je 43f\n   cmp ebp, 111\n   je 43f\n   cmp ebp, 97\n   je 43f\n   mov eax, 1\n   jmp 42f\n"
        "41: mov eax, 8\n   jmp 42f\n"
        "43: mov eax, 4\n"
        "42: mov " DW_ARG4 ", rax\n   mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rbx + 16]\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   test eax, eax\n   jz 98f\n"
        "cmp r14, 0x04000000\n   ja 98f\n   mov rax, [rbx + 16]\n   sub rax, [rbx + 8]\n   cmp r14, rax\n   ja 98f\n"
        "mov rax, [rbx + 8]\n   add rax, r14\n   mov [rsp + 48], rax\n" /* stop */
        "lea rax, [r13 - 1]\n   mov [rsp + 56], rax\n" /* after */
#if defined(_WIN32)
        "mov rcx, [rbx + 24]\n   mov rdx, [rbx + 32]\n   lea r8, [rsp + 56]\n   xor r9d, r9d\n"
        "mov dword ptr [rsp + 32], 0\n   mov dword ptr [rsp + 40], 0\n   call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
#else
        "mov rdi, [rbx + 24]\n   mov rsi, [rbx + 32]\n   lea rdx, [rsp + 56]\n   xor ecx, ecx\n   xor r8d, r8d\n   xor r9d, r9d\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
#endif
        "test eax, eax\n   jz 98f\n   mov rax, [rbx + 16]\n   mov [rsp + 64], rax\n" /* old_limit */
        "mov eax, [rbx + 56]\n   mov [rsp + 72], eax\n" /* old_depth */
        "mov qword ptr [rsp + 80], 0\n   mov qword ptr [rsp + 88], 0\n" /* previous, elements */
        "mov rax, [rsp + 48]\n   mov [rbx + 16], rax\n"
        "44: mov rax, [rbx + 8]\n   cmp rax, [rsp + 48]\n   jge 47f\n   mov [rsp + 80], rax\n   inc qword ptr [rsp + 88]\n"
        "cmp ebp, 123\n   jne 45f\n   mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rsp + 48]\n   mov " DW_ARG4 ", 8\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   test eax, eax\n   jz 48f\n"
        "lea rax, [r13 + 1]\n   mov [rbx + 40], rax\n   mov eax, [rsp + 72]\n   add eax, 2\n   mov [rbx + 56], eax\n"
        "mov " DW_ARG1 ", rbx\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   test eax, eax\n   jz 48f\n"
        "mov " DW_ARG1 ", rbx\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   test eax, eax\n   jnz 44b\n   jmp 48f\n"
        "45: mov [rbx + 40], r13\n   mov eax, [rsp + 72]\n   inc eax\n   mov [rbx + 56], eax\n"
        "mov " DW_ARG1 ", rbx\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   test eax, eax\n   jnz 44b\n"
        "48: mov rax, [rsp + 64]\n   mov [rbx + 16], rax\n   mov eax, [rsp + 72]\n   mov [rbx + 56], eax\n   jmp 98f\n"
        "47: mov rax, [rsp + 64]\n   mov [rbx + 16], rax\n   mov eax, [rsp + 72]\n   mov [rbx + 56], eax\n"
        "mov rax, [rbx + 8]\n   cmp rax, [rsp + 48]\n   jne 98f\n"
        "cmp ebp, 40\n   je 50f\n   cmp ebp, 123\n   jne 51f\n"
        "50: lea rax, [r15 + 4]\n   mov rcx, [rsp + 48]\n   sub rcx, r14\n   cmp rax, rcx\n   jne 51f\n"
        "cmp qword ptr [rsp + 88], 1\n   jle 51f\n   mov rax, [rsp + 48]\n   sub rax, 4\n   cmp [rsp + 80], rax\n   jge 98f\n"
        "51: mov rax, [rsp + 56]\n   mov [rbx + 40], rax\n   jmp 97f\n"
        /* ( */
        "60: cmp r12d, 40\n   jne 98f\n   mov " DW_ARG1 ", [rbx]\n   lea " DW_ARG2 ", [rbx + 8]\n   mov " DW_ARG3 ", [rbx + 16]\n   mov " DW_ARG4 ", 8\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   test eax, eax\n   jz 98f\n"
        "mov r13d, [rbx + 56]\n   lea eax, [r13d + 1]\n   mov [rbx + 56], eax\n   xor r14d, r14d\n"
        "61: mov rax, [rbx + 40]\n   cmp rax, [rbx + 32]\n   jge 62f\n   mov rcx, [rbx + 24]\n   cmp byte ptr [rcx + rax], 41\n   je 63f\n"
        "mov " DW_ARG1 ", rbx\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   test eax, eax\n   jz 62f\n   inc r14\n   jmp 61b\n"
        "63: mov [rbx + 56], r13d\n   test r14, r14\n   jz 98f\n   inc rax\n   mov [rbx + 40], rax\n   jmp 97f\n"
        "62: mov [rbx + 56], r13d\n   jmp 98f\n"
        "97: mov eax, 1\n"
        "96: add rsp, 104\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" DBUS_ASM_RET
        "98: xor eax, eax\n   jmp 96b\n"
        "99: xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_value)

        /* dbus_wire_body_valid(body, len, sig, sig_len, fds, be) */
        DBUS_ASM_FUNC(dbus_wire_body_valid)
#if defined(_WIN32)
        "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov rax, [rsp + 40]\n   mov edx, [rsp + 48]\n"
#else
        "mov r10, rdx\n   mov r11, rcx\n   mov rax, r8\n   mov edx, r9d\n   mov r8, rdi\n   mov r9, rsi\n"
#endif
        "test r9, r9\n   js 3f\n   test r11, r11\n   js 3f\n   test r10, r10\n   jz 3f\n"
        "test r11, r11\n   jnz 10f\n   test r9, r9\n   sete al\n   movzx eax, al\n" DBUS_ASM_RET
        "10: test r8, r8\n   jz 3f\n   sub rsp, 104\n"
        "mov [rsp + 32], r8\n   mov qword ptr [rsp + 40], 0\n   mov [rsp + 48], r9\n   mov [rsp + 56], r10\n"
        "mov [rsp + 64], r11\n   mov qword ptr [rsp + 72], 0\n   mov [rsp + 80], rax\n   mov dword ptr [rsp + 88], 0\n   mov [rsp + 92], edx\n"
        "1:  mov rax, [rsp + 72]\n   cmp rax, [rsp + 64]\n   jge 2f\n"
        "lea " DW_ARG1 ", [rsp + 32]\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   test eax, eax\n   jnz 1b\n"
        "add rsp, 104\n" DBUS_ASM_RET
        "2:  mov rax, [rsp + 40]\n   cmp rax, [rsp + 48]\n   sete al\n   movzx eax, al\n   add rsp, 104\n" DBUS_ASM_RET
        "3:  xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_body_valid)

        /* dbus_wire_header_check(b, size, max_size, out) */
        DBUS_ASM_FUNC(dbus_wire_header_check)
        DW_LOAD4
        "test r8, r8\n   jz 5f\n   test r11, r11\n   jz 5f\n   cmp r9, 16\n   jl 5f\n   cmp r9, r10\n   jg 5f\n"
        "movzx eax, byte ptr [r8]\n   xor ecx, ecx\n   cmp eax, 108\n   je 1f\n   cmp eax, 66\n   jne 5f\n   mov ecx, 1\n"
        "1:  cmp byte ptr [r8 + 3], 1\n   jne 5f\n   movzx edx, byte ptr [r8 + 1]\n   lea eax, [rdx - 1]\n   cmp eax, 3\n   ja 5f\n"
        "push rbx\n   push r12\n   push r13\n   mov r12d, [r8 + 4]\n   mov r13d, [r8 + 8]\n   mov ebx, [r8 + 12]\n"
        "test ecx, ecx\n   jz 2f\n   bswap r12d\n   bswap r13d\n   bswap ebx\n"
        "2:  test r13d, r13d\n   jz 6f\n   lea rax, [r9 - 16]\n   cmp rbx, rax\n   ja 6f\n   cmp r12, r9\n   ja 6f\n"
        "lea rax, [rbx + 16]\n   lea r10, [rax + 7]\n   and r10, -8\n   lea rdx, [r10 + r12]\n   cmp rdx, r9\n   jne 6f\n"
        "mov rdx, rax\n"
        "3:  cmp rdx, r10\n   jae 4f\n   cmp byte ptr [r8 + rdx], 0\n   jne 6f\n   inc rdx\n   jmp 3b\n"
        "4:  mov [r11], r13d\n   mov [r11 + 4], r12d\n   mov [r11 + 8], ebx\n"
        "movzx edx, byte ptr [r8 + 1]\n   mov [r11 + 12], dl\n   movzx edx, byte ptr [r8 + 2]\n   mov [r11 + 13], dl\n"
        "mov [r11 + 14], cl\n   mov byte ptr [r11 + 15], 0\n   mov [r11 + 16], rax\n   mov [r11 + 24], r10\n"
        "pop r13\n   pop r12\n   pop rbx\n   mov eax, 1\n" DBUS_ASM_RET
        "6:  pop r13\n   pop r12\n   pop rbx\n"
        "5:  xor eax, eax\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_header_check)

        /* dbus_wire_o_pad(w, align) */
        DBUS_ASM_FUNC(dbus_wire_o_pad)
        DW_LOAD2
        "test r8, r8\n   jz 3f\n   cmp dword ptr [r8 + 24], 0\n   jne 3f\n   mov rax, [r8 + 8]\n   mov rcx, [r8 + 16]\n"
        "lea r10, [r9 - 1]\n   lea rdx, [rax + r10]\n   not r10\n   and rdx, r10\n   cmp rdx, rcx\n   jg 2f\n   mov r11, [r8]\n"
        "1:  cmp rax, rdx\n   jae 4f\n   mov byte ptr [r11 + rax], 0\n   inc rax\n   jmp 1b\n"
        "4:  mov [r8 + 8], rdx\n" DBUS_ASM_RET
        "2:  mov dword ptr [r8 + 24], 1\n"
        "3:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_pad)

        /* dbus_wire_o_u32(w, v) */
        DBUS_ASM_FUNC(dbus_wire_o_u32)
        DW_LOAD2
        "test r8, r8\n   jz 3f\n   cmp dword ptr [r8 + 24], 0\n   jne 3f\n   mov rax, [r8 + 8]\n   mov rcx, [r8 + 16]\n"
        "lea rdx, [rax + 3]\n   and rdx, -4\n   cmp rdx, rcx\n   jg 2f\n   sub rcx, rdx\n   cmp rcx, 4\n   jl 2f\n"
        "mov r10, [r8]\n"
        "1:  cmp rax, rdx\n   jae 4f\n   mov byte ptr [r10 + rax], 0\n   inc rax\n   jmp 1b\n"
        "4:  mov [r10 + rdx], r9d\n   add rdx, 4\n   mov [r8 + 8], rdx\n" DBUS_ASM_RET
        "2:  mov dword ptr [r8 + 24], 1\n"
        "3:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_u32)

        /* dbus_wire_o_string(w, s) */
        DBUS_ASM_FUNC(dbus_wire_o_string)
        DW_LOAD2
        "test r8, r8\n   jz 5f\n   cmp dword ptr [r8 + 24], 0\n   jne 5f\n   test r9, r9\n   jz 4f\n   xor rcx, rcx\n"
        "1:  cmp byte ptr [r9 + rcx], 0\n   je 2f\n   inc rcx\n   jmp 1b\n"
        "2:  mov rax, [r8 + 8]\n   mov rdx, [r8 + 16]\n   lea r10, [rax + 3]\n   and r10, -4\n"
        "cmp r10, rdx\n   jg 4f\n   sub rdx, r10\n   lea r11, [rcx + 5]\n   cmp rdx, r11\n   jl 4f\n"
        "mov r11, [r8]\n"
        "3:  cmp rax, r10\n   jae 6f\n   mov byte ptr [r11 + rax], 0\n   inc rax\n   jmp 3b\n"
        "6:  mov [r11 + r10], ecx\n   add r10, 4\n   lea rdx, [r11 + r10]\n   inc rcx\n   xor eax, eax\n"
        "7:  mov r11b, [r9 + rax]\n   mov [rdx + rax], r11b\n   inc rax\n   cmp rax, rcx\n   jb 7b\n"
        "add r10, rcx\n   mov [r8 + 8], r10\n" DBUS_ASM_RET
        "4:  mov dword ptr [r8 + 24], 1\n"
        "5:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_string)

        ".att_syntax prefix\n"
);

#elif defined(DBUS_WIRE_RISCV64)

__asm__(
        DBUS_ASM_SECTION

        /* dbus_wire_utf8_valid(a0=s, a1=n) */
        DBUS_ASM_FUNC(dbus_wire_utf8_valid)
        "beqz a1, 9f\n   bltz a1, 8f\n   beqz a0, 8f\n   add a1, a0, a1\n"
        "li t4, 0x0101010101010101\n   li t5, 0x8080808080808080\n   li a4, 0x80\n   li a5, 0xc0\n"
        "1:  sub t0, a1, a0\n   li t1, 8\n   bltu t0, t1, 2f\n   andi t1, a0, 7\n   bnez t1, 2f\n"
        "ld t2, 0(a0)\n   sub t3, t2, t4\n   or t3, t3, t2\n   and t3, t3, t5\n   bnez t3, 2f\n   addi a0, a0, 8\n   j 1b\n"
        "2:  bgeu a0, a1, 9f\n   lbu a2, 0(a0)\n   beqz a2, 8f\n   bgeu a2, a4, 3f\n   addi a0, a0, 1\n   j 1b\n"
        "3:  li t0, 0xc2\n   bltu a2, t0, 8f\n   li t0, 0xe0\n   bgeu a2, t0, 4f\n"
        "sub t0, a1, a0\n   li t1, 2\n   bltu t0, t1, 8f\n   lbu a3, 1(a0)\n   and t0, a3, a5\n   bne t0, a4, 8f\n"
        "addi a0, a0, 2\n   j 1b\n"
        "4:  li t0, 0xf0\n   bgeu a2, t0, 6f\n   sub t0, a1, a0\n   li t1, 3\n   bltu t0, t1, 8f\n"
        "lbu a3, 1(a0)\n   lbu t2, 2(a0)\n   and t0, a3, a5\n   bne t0, a4, 8f\n   and t0, t2, a5\n   bne t0, a4, 8f\n"
        "li t0, 0xe0\n   li t1, 0xa0\n   bne a2, t0, 5f\n   bltu a3, t1, 8f\n   addi a0, a0, 3\n   j 1b\n"
        "5:  li t0, 0xed\n   bne a2, t0, 7f\n   bgeu a3, t1, 8f\n"
        "7:  addi a0, a0, 3\n   j 1b\n"
        "6:  li t0, 0xf5\n   bgeu a2, t0, 8f\n   sub t0, a1, a0\n   li t1, 4\n   bltu t0, t1, 8f\n"
        "lbu a3, 1(a0)\n   lbu t2, 2(a0)\n   lbu t3, 3(a0)\n   and t0, a3, a5\n   bne t0, a4, 8f\n"
        "and t0, t2, a5\n   bne t0, a4, 8f\n   and t0, t3, a5\n   bne t0, a4, 8f\n"
        "li t0, 0xf0\n   li t1, 0x90\n   bne a2, t0, 10f\n   bltu a3, t1, 8f\n   addi a0, a0, 4\n   j 1b\n"
        "10: li t0, 0xf4\n   bne a2, t0, 11f\n   bgeu a3, t1, 8f\n"
        "11: addi a0, a0, 4\n   j 1b\n"
        "8:  li a0, 0\n" DBUS_ASM_RET
        "9:  li a0, 1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_utf8_valid)

        /* dbus_wire_valid_path(a0=s, a1=n) */
        DBUS_ASM_FUNC(dbus_wire_valid_path)
        "beqz a0, 5f\n   addi t0, a1, -1\n   li t1, 254\n   bgtu t0, t1, 5f\n"
        "lbu t0, 0(a0)\n   li t1, 47\n   bne t0, t1, 5f\n   li t2, 1\n   beq a1, t2, 6f\n"
        "li a2, 1\n   li a3, 1\n   li t3, 25\n   li t4, 9\n   li t5, 95\n"
        "1:  beq a2, a1, 2f\n   add t6, a0, a2\n   lbu t0, 0(t6)\n   bne t0, t1, 3f\n"
        "2:  beq a2, a3, 5f\n   addi a3, a2, 1\n   beq a2, a1, 4f\n   addi a2, a2, 1\n   j 1b\n"
        "3:  ori t2, t0, 32\n   addi t2, t2, -97\n   bleu t2, t3, 7f\n"
        "addi t2, t0, -48\n   bleu t2, t4, 7f\n   bne t0, t5, 5f\n"
        "7:  addi a2, a2, 1\n   j 1b\n"
        "4:  add t6, a0, a1\n   lbu t0, -1(t6)\n   beq t0, t1, 5f\n"
        "6:  li a0, 1\n" DBUS_ASM_RET
        "5:  li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_path)

        /* dbus_wire_valid_name(a0=s, a1=n, a2=kind) */
        DBUS_ASM_FUNC(dbus_wire_valid_name)
        "beqz a0, 8f\n   addi t0, a1, -1\n   li t1, 254\n   bgtu t0, t1, 8f\n"
        "li t3, 25\n   li t4, 9\n   li t5, 95\n   li t0, 2\n   bne a2, t0, 2f\n"
        "lbu t0, 0(a0)\n   ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t3, 10f\n   bne t0, t5, 8f\n"
        "10: li a3, 1\n"
        "1:  bgeu a3, a1, 9f\n   add t6, a0, a3\n   lbu t0, 0(t6)\n   addi a3, a3, 1\n"
        "ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t3, 1b\n"
        "addi t1, t0, -48\n   bleu t1, t4, 1b\n   beq t0, t5, 1b\n   j 8f\n"
        "2:  li a3, 0\n   li a4, 0\n   li a5, 0\n   li t0, 1\n   bne a2, t0, 3f\n"
        "lbu t0, 0(a0)\n   li t1, 58\n   bne t0, t1, 3f\n   li a5, 1\n   li a3, 1\n   li a4, 1\n"
        "3:  li a6, 0\n"
        "4:  beq a3, a1, 5f\n   add t6, a0, a3\n   lbu t0, 0(t6)\n   li t1, 46\n   bne t0, t1, 6f\n"
        "5:  beq a3, a4, 8f\n   addi a4, a3, 1\n   beq a3, a1, 7f\n   addi a6, a6, 1\n   addi a3, a3, 1\n   j 4b\n"
        "6:  ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t3, 11f\n   beq t0, t5, 11f\n"
        "li t1, 1\n   bne a2, t1, 12f\n   li t1, 45\n   beq t0, t1, 11f\n"
        "12: addi t1, t0, -48\n   bgtu t1, t4, 8f\n   bnez a5, 11f\n   beq a3, a4, 8f\n"
        "11: addi a3, a3, 1\n   j 4b\n"
        "7:  beqz a6, 8f\n   add t6, a0, a1\n   lbu t0, -1(t6)\n   li t1, 46\n   beq t0, t1, 8f\n"
        "9:  li a0, 1\n" DBUS_ASM_RET
        "8:  li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_name)

        /* dbus_wire_sig_one(a0=s, a1=n, a2=at, a3=arrays, a4=structs, a5=dicts) */
        DBUS_ASM_FUNC(dbus_wire_sig_one)
        "beqz a0, 8f\n   beqz a2, 8f\n"
        "1:  ld t0, 0(a2)\n   bltz t0, 8f\n   bge t0, a1, 8f\n   li t1, 32\n"
        "bgt a3, t1, 8f\n   bgt a4, t1, 8f\n   bgt a5, t1, 8f\n"
        "add t2, a0, t0\n   lbu t3, 0(t2)\n   addi t0, t0, 1\n   sd t0, 0(a2)\n"
        "addi t4, t3, -97\n   li t1, 25\n   bgtu t4, t1, 2f\n"
        "li t5, 0x01bd61ca\n   srl t5, t5, t4\n   andi t5, t5, 1\n   bnez t5, 9f\n"
        "2:  li t1, 97\n   bne t3, t1, 4f\n   bge t0, a1, 3f\n   add t2, a0, t0\n   lbu t4, 0(t2)\n"
        "li t1, 123\n   bne t4, t1, 3f\n   addi t0, t0, 1\n   bge t0, a1, 8f\n"
        "add t2, a0, t0\n   lbu t4, 0(t2)\n   addi t0, t0, 1\n   addi t4, t4, -97\n   li t1, 25\n   bgtu t4, t1, 8f\n"
        "li t5, 0x019d61ca\n   srl t5, t5, t4\n   andi t5, t5, 1\n   beqz t5, 8f\n   sd t0, 0(a2)\n"
        "addi sp, sp, -32\n   sd ra, 24(sp)\n   sd s0, 16(sp)\n   sd s1, 8(sp)\n   sd s2, 0(sp)\n"
        "mv s0, a0\n   mv s1, a1\n   mv s2, a2\n   addi a3, a3, 1\n   addi a5, a5, 1\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   beqz a0, 7f\n   ld t0, 0(s2)\n   bge t0, s1, 6f\n"
        "add t2, s0, t0\n   lbu t4, 0(t2)\n   li t1, 125\n   bne t4, t1, 6f\n"
        "addi t0, t0, 1\n   sd t0, 0(s2)\n   li a0, 1\n   j 7f\n"
        "3:  addi a3, a3, 1\n   j 1b\n"
        "4:  li t1, 40\n   bne t3, t1, 8f\n"
        "addi sp, sp, -64\n   sd ra, 56(sp)\n   sd s0, 48(sp)\n   sd s1, 40(sp)\n   sd s2, 32(sp)\n"
        "sd s3, 24(sp)\n   sd s4, 16(sp)\n   sd s5, 8(sp)\n   sd s6, 0(sp)\n"
        "mv s0, a0\n   mv s1, a1\n   mv s2, a2\n   mv s3, a3\n   addi s4, a4, 1\n   mv s5, a5\n   li s6, 0\n"
        "5:  ld t0, 0(s2)\n   bge t0, s1, 10f\n   add t2, s0, t0\n   lbu t4, 0(t2)\n   li t1, 41\n   beq t4, t1, 11f\n"
        "mv a0, s0\n   mv a1, s1\n   mv a2, s2\n   mv a3, s3\n   mv a4, s4\n   mv a5, s5\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   beqz a0, 10f\n   addi s6, s6, 1\n   j 5b\n"
        "11: beqz s6, 10f\n   addi t0, t0, 1\n   sd t0, 0(s2)\n   li a0, 1\n   j 12f\n"
        "10: li a0, 0\n"
        "12: ld s6, 0(sp)\n   ld s5, 8(sp)\n   ld s4, 16(sp)\n   ld s3, 24(sp)\n   ld s2, 32(sp)\n"
        "ld s1, 40(sp)\n   ld s0, 48(sp)\n   ld ra, 56(sp)\n   addi sp, sp, 64\n" DBUS_ASM_RET
        "6:  li a0, 0\n"
        "7:  ld s2, 0(sp)\n   ld s1, 8(sp)\n   ld s0, 16(sp)\n   ld ra, 24(sp)\n   addi sp, sp, 32\n" DBUS_ASM_RET
        "8:  li a0, 0\n" DBUS_ASM_RET
        "9:  li a0, 1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_sig_one)

        /* dbus_wire_valid_signature(a0=s, a1=n) */
        DBUS_ASM_FUNC(dbus_wire_valid_signature)
        "beqz a1, 3f\n   bltz a1, 2f\n   li t0, 255\n   bgt a1, t0, 2f\n   beqz a0, 2f\n"
        "addi sp, sp, -32\n   sd ra, 24(sp)\n   sd s0, 16(sp)\n   sd s1, 8(sp)\n   sd zero, 0(sp)\n"
        "mv s0, a0\n   mv s1, a1\n"
        "1:  ld t0, 0(sp)\n   bge t0, s1, 4f\n   mv a0, s0\n   mv a1, s1\n   mv a2, sp\n"
        "li a3, 0\n   li a4, 0\n   li a5, 0\n   call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   bnez a0, 1b\n   j 5f\n"
        "4:  li a0, 1\n"
        "5:  ld s1, 8(sp)\n   ld s0, 16(sp)\n   ld ra, 24(sp)\n   addi sp, sp, 32\n" DBUS_ASM_RET
        "2:  li a0, 0\n" DBUS_ASM_RET
        "3:  li a0, 1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_valid_signature)

        /* dbus_wire_variant_sig(a0=p, a1=at, a2=limit) */
        DBUS_ASM_FUNC(dbus_wire_variant_sig)
        "beqz a0, 2f\n   beqz a1, 2f\n   ld t0, 0(a1)\n   bltz t0, 2f\n   sub t1, a2, t0\n   li t2, 1\n   blt t1, t2, 2f\n"
        "add t2, a0, t0\n   lbu t3, 0(t2)\n   addi t0, t0, 1\n   sd t0, 0(a1)\n"
        "sub t1, a2, t0\n   ble t1, t3, 2f\n   add t4, t0, t3\n   add t4, a0, t4\n   lbu t5, 0(t4)\n   bnez t5, 2f\n"
        "addi sp, sp, -32\n   sd ra, 24(sp)\n   sd s0, 16(sp)\n   sd s1, 8(sp)\n   sd zero, 0(sp)\n"
        "mv s0, a1\n   mv s1, t3\n   add a0, a0, t0\n   mv a1, t3\n   mv a2, sp\n"
        "li a3, 0\n   li a4, 0\n   li a5, 0\n   call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n"
        "beqz a0, 3f\n   ld t0, 0(sp)\n   bne t0, s1, 3f\n   ld t1, 0(s0)\n   add t1, t1, s1\n"
        "addi t1, t1, 1\n   sd t1, 0(s0)\n   mv a0, s1\n   j 4f\n"
        "3:  li a0, -1\n"
        "4:  ld s1, 8(sp)\n   ld s0, 16(sp)\n   ld ra, 24(sp)\n   addi sp, sp, 32\n" DBUS_ASM_RET
        "2:  li a0, -1\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_variant_sig)

        /* dbus_wire_fixed_size(a0=c) */
        DBUS_ASM_FUNC(dbus_wire_fixed_size)
        "li t0, 121\n   beq a0, t0, 1f\n   addi t0, a0, -98\n   li t1, 22\n   bgtu t0, t1, 2f\n"
        "li t2, 0x000030b04100a032\n   slli t0, t0, 1\n   srl t2, t2, t0\n   andi t2, t2, 3\n"
        "li a0, 1\n   sll a0, a0, t2\n   andi a0, a0, -2\n" DBUS_ASM_RET
        "1:  li a0, 1\n" DBUS_ASM_RET
        "2:  li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_fixed_size)

        /* dbus_wire_pad_zero(a0=p, a1=at, a2=limit, a3=align) */
        DBUS_ASM_FUNC(dbus_wire_pad_zero)
        "beqz a0, 3f\n   beqz a1, 3f\n   ld t0, 0(a1)\n   bltz t0, 3f\n   addi t1, a3, -1\n"
        "add t2, t0, t1\n   not t1, t1\n   and t2, t2, t1\n   bgt t2, a2, 3f\n"
        "1:  bgeu t0, t2, 2f\n   add t3, a0, t0\n   lbu t4, 0(t3)\n   bnez t4, 3f\n   addi t0, t0, 1\n   j 1b\n"
        "2:  sd t2, 0(a1)\n   li a0, 1\n" DBUS_ASM_RET
        "3:  li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_pad_zero)

        /* dbus_wire_get32(a0=p, a1=big_endian) */
        DBUS_ASM_FUNC(dbus_wire_get32)
        "lbu t0, 0(a0)\n   lbu t1, 1(a0)\n   lbu t2, 2(a0)\n   lbu t3, 3(a0)\n   bnez a1, 1f\n"
        "slli t1, t1, 8\n   slli t2, t2, 16\n   slli t3, t3, 24\n   or a0, t0, t1\n   or t2, t2, t3\n   or a0, a0, t2\n" DBUS_ASM_RET
        "1:  slli t0, t0, 24\n   slli t1, t1, 16\n   slli t2, t2, 8\n   or a0, t0, t1\n   or t2, t2, t3\n   or a0, a0, t2\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_get32)

        /* dbus_wire_value(a0=cursor*) */
        DBUS_ASM_FUNC(dbus_wire_value)
        "beqz a0, 99f\n   addi sp, sp, -112\n   sd ra, 104(sp)\n   sd s0, 96(sp)\n   sd s1, 88(sp)\n"
        "sd s2, 80(sp)\n   sd s3, 72(sp)\n   sd s4, 64(sp)\n   sd s5, 56(sp)\n   sd s6, 48(sp)\n"
        "mv s0, a0\n   ld t0, 0(s0)\n   beqz t0, 98f\n   ld t1, 24(s0)\n   beqz t1, 98f\n"
        "ld t2, 8(s0)\n   ld t3, 16(s0)\n   bltz t2, 98f\n   bgt t2, t3, 98f\n"
        "ld t4, 40(s0)\n   ld t5, 32(s0)\n   bltz t4, 98f\n   bge t4, t5, 98f\n"
        "lw t6, 56(s0)\n   li t2, 64\n   bgt t6, t2, 98f\n"
        "add t2, t1, t4\n   lbu s1, 0(t2)\n   addi t4, t4, 1\n   sd t4, 40(s0)\n"
        "mv a0, s1\n   call " DBUS_ASM_SYM(dbus_wire_fixed_size) "\n   beqz a0, 10f\n"
        "mv s2, a0\n   ld a0, 0(s0)\n   addi a1, s0, 8\n   ld a2, 16(s0)\n   mv a3, s2\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   beqz a0, 98f\n"
        "ld t2, 8(s0)\n   ld t3, 16(s0)\n   sub t3, t3, t2\n   blt t3, s2, 98f\n"
        "li t0, 98\n   beq s1, t0, 2f\n   li t0, 104\n   bne s1, t0, 3f\n"
        "ld a0, 0(s0)\n   add a0, a0, t2\n   lw a1, 60(s0)\n   call " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "slli a0, a0, 32\n   srli a0, a0, 32\n   ld t3, 48(s0)\n   bgeu a0, t3, 98f\n   j 3f\n"
        "2:  ld a0, 0(s0)\n   add a0, a0, t2\n   lw a1, 60(s0)\n   call " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "slli a0, a0, 32\n   srli a0, a0, 32\n   li t0, 1\n   bgtu a0, t0, 98f\n"
        "3:  ld t2, 8(s0)\n   add t2, t2, s2\n   sd t2, 8(s0)\n   j 97f\n"
        /* s / o */
        "10: li t0, 115\n   beq s1, t0, 11f\n   li t0, 111\n   bne s1, t0, 20f\n"
        "11: ld a0, 0(s0)\n   addi a1, s0, 8\n   ld a2, 16(s0)\n   li a3, 4\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   beqz a0, 98f\n"
        "ld t2, 8(s0)\n   ld t3, 16(s0)\n   sub t3, t3, t2\n   li t0, 4\n   blt t3, t0, 98f\n"
        "ld a0, 0(s0)\n   add a0, a0, t2\n   lw a1, 60(s0)\n   call " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "slli s2, a0, 32\n   srli s2, s2, 32\n   ld t2, 8(s0)\n   addi t2, t2, 4\n   sd t2, 8(s0)\n"
        "ld t3, 16(s0)\n   sub t3, t3, t2\n   ble t3, s2, 98f\n"
        "ld t0, 0(s0)\n   add s3, t0, t2\n   add t4, s3, s2\n   lbu t5, 0(t4)\n   bnez t5, 98f\n"
        "mv a0, s3\n   mv a1, s2\n   call " DBUS_ASM_SYM(dbus_wire_utf8_valid) "\n   beqz a0, 98f\n"
        "li t0, 111\n   bne s1, t0, 12f\n   mv a0, s3\n   mv a1, s2\n   call " DBUS_ASM_SYM(dbus_wire_valid_path) "\n   beqz a0, 98f\n"
        "12: ld t2, 8(s0)\n   add t2, t2, s2\n   addi t2, t2, 1\n   sd t2, 8(s0)\n   j 97f\n"
        /* g */
        "20: li t0, 103\n   bne s1, t0, 30f\n   ld t2, 8(s0)\n   ld t3, 16(s0)\n   sub t3, t3, t2\n   li t0, 1\n   blt t3, t0, 98f\n"
        "ld t0, 0(s0)\n   add t4, t0, t2\n   lbu s2, 0(t4)\n   addi t2, t2, 1\n   sd t2, 8(s0)\n"
        "ld t3, 16(s0)\n   sub t3, t3, t2\n   ble t3, s2, 98f\n   add s3, t0, t2\n   add t4, s3, s2\n   lbu t5, 0(t4)\n   bnez t5, 98f\n"
        "mv a0, s3\n   mv a1, s2\n   call " DBUS_ASM_SYM(dbus_wire_valid_signature) "\n   beqz a0, 98f\n"
        "ld t2, 8(s0)\n   add t2, t2, s2\n   addi t2, t2, 1\n   sd t2, 8(s0)\n   j 97f\n"
        /* v */
        "30: li t0, 118\n   bne s1, t0, 40f\n   ld s2, 8(s0)\n   addi s2, s2, 1\n"
        "ld a0, 0(s0)\n   addi a1, s0, 8\n   ld a2, 16(s0)\n   call " DBUS_ASM_SYM(dbus_wire_variant_sig) "\n   bltz a0, 98f\n"
        "ld s3, 24(s0)\n   ld s4, 32(s0)\n   ld s5, 40(s0)\n   lw s6, 56(s0)\n"
        "ld t0, 0(s0)\n   add t0, t0, s2\n   sd t0, 24(s0)\n   sd a0, 32(s0)\n   sd zero, 40(s0)\n"
        "addi t1, s6, 1\n   sw t1, 56(s0)\n   mv a0, s0\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n"
        "sd s3, 24(s0)\n   sd s4, 32(s0)\n   sd s5, 40(s0)\n   sw s6, 56(s0)\n   j 96f\n"
        /* a */
        "40: li t0, 97\n   bne s1, t0, 60f\n   ld s2, 40(s0)\n   ld t1, 32(s0)\n   bge s2, t1, 98f\n"
        "ld a0, 0(s0)\n   addi a1, s0, 8\n   ld a2, 16(s0)\n   li a3, 4\n   call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   beqz a0, 98f\n"
        "ld t2, 8(s0)\n   ld t3, 16(s0)\n   sub t3, t3, t2\n   li t0, 4\n   blt t3, t0, 98f\n"
        "ld a0, 0(s0)\n   add a0, a0, t2\n   lw a1, 60(s0)\n   call " DBUS_ASM_SYM(dbus_wire_get32) "\n"
        "slli s3, a0, 32\n   srli s3, s3, 32\n   ld t2, 8(s0)\n   addi t2, t2, 4\n   sd t2, 8(s0)\n   mv s4, t2\n"
        "ld t0, 24(s0)\n   add t0, t0, s2\n   lbu s5, 0(t0)\n"
        "li t0, 123\n   beq s5, t0, 41f\n   li t0, 40\n   beq s5, t0, 41f\n"
        "mv a0, s5\n   call " DBUS_ASM_SYM(dbus_wire_fixed_size) "\n   bnez a0, 42f\n"
        "li t0, 115\n   beq s5, t0, 43f\n   li t0, 111\n   beq s5, t0, 43f\n   li t0, 97\n   beq s5, t0, 43f\n   li a0, 1\n   j 42f\n"
        "41: li a0, 8\n   j 42f\n"
        "43: li a0, 4\n"
        "42: mv a3, a0\n   ld a0, 0(s0)\n   addi a1, s0, 8\n   ld a2, 16(s0)\n   call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   beqz a0, 98f\n"
        "li t0, 0x04000000\n   bgtu s3, t0, 98f\n   ld t2, 8(s0)\n   ld t3, 16(s0)\n   sub t3, t3, t2\n   bgtu s3, t3, 98f\n"
        "add s6, t2, s3\n   addi t0, s2, -1\n   sd t0, 0(sp)\n"
        "ld a0, 24(s0)\n   ld a1, 32(s0)\n   mv a2, sp\n   li a3, 0\n   li a4, 0\n   li a5, 0\n"
        "call " DBUS_ASM_SYM(dbus_wire_sig_one) "\n   beqz a0, 98f\n"
        "ld t0, 16(s0)\n   lw t1, 56(s0)\n   sd t0, 8(sp)\n   sw t1, 16(sp)\n   sd zero, 24(sp)\n   sd zero, 32(sp)\n   sd s6, 16(s0)\n"
        "44: ld t2, 8(s0)\n   bge t2, s6, 47f\n   sd t2, 24(sp)\n   ld t0, 32(sp)\n   addi t0, t0, 1\n   sd t0, 32(sp)\n"
        "li t0, 123\n   bne s5, t0, 45f\n   ld a0, 0(s0)\n   addi a1, s0, 8\n   mv a2, s6\n   li a3, 8\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   beqz a0, 48f\n   addi t0, s2, 1\n   sd t0, 40(s0)\n"
        "lw t1, 16(sp)\n   addi t1, t1, 2\n   sw t1, 56(s0)\n   mv a0, s0\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   beqz a0, 48f\n"
        "mv a0, s0\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   bnez a0, 44b\n   j 48f\n"
        "45: sd s2, 40(s0)\n   lw t1, 16(sp)\n   addi t1, t1, 1\n   sw t1, 56(s0)\n"
        "mv a0, s0\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   bnez a0, 44b\n"
        "48: ld t0, 8(sp)\n   lw t1, 16(sp)\n   sd t0, 16(s0)\n   sw t1, 56(s0)\n   j 98f\n"
        "47: ld t0, 8(sp)\n   lw t1, 16(sp)\n   sd t0, 16(s0)\n   sw t1, 56(s0)\n   ld t2, 8(s0)\n   bne t2, s6, 98f\n"
        "li t0, 40\n   beq s5, t0, 50f\n   li t0, 123\n   bne s5, t0, 51f\n"
        "50: addi t0, s4, 4\n   sub t1, s6, s3\n   bne t0, t1, 51f\n   ld t0, 32(sp)\n   li t1, 1\n   ble t0, t1, 51f\n"
        "ld t0, 24(sp)\n   addi t1, s6, -4\n   bge t0, t1, 98f\n"
        "51: ld t0, 0(sp)\n   sd t0, 40(s0)\n   j 97f\n"
        /* ( */
        "60: li t0, 40\n   bne s1, t0, 98f\n   ld a0, 0(s0)\n   addi a1, s0, 8\n   ld a2, 16(s0)\n   li a3, 8\n"
        "call " DBUS_ASM_SYM(dbus_wire_pad_zero) "\n   beqz a0, 98f\n"
        "lw s2, 56(s0)\n   addi t0, s2, 1\n   sw t0, 56(s0)\n   li s3, 0\n"
        "61: ld t0, 40(s0)\n   ld t1, 32(s0)\n   bge t0, t1, 62f\n   ld t2, 24(s0)\n   add t2, t2, t0\n   lbu t3, 0(t2)\n"
        "li t4, 41\n   beq t3, t4, 63f\n   mv a0, s0\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n   beqz a0, 62f\n   addi s3, s3, 1\n   j 61b\n"
        "63: sw s2, 56(s0)\n   beqz s3, 98f\n   addi t0, t0, 1\n   sd t0, 40(s0)\n   j 97f\n"
        "62: sw s2, 56(s0)\n   j 98f\n"
        "97: li a0, 1\n"
        "96: ld s6, 48(sp)\n   ld s5, 56(sp)\n   ld s4, 64(sp)\n   ld s3, 72(sp)\n   ld s2, 80(sp)\n"
        "ld s1, 88(sp)\n   ld s0, 96(sp)\n   ld ra, 104(sp)\n   addi sp, sp, 112\n" DBUS_ASM_RET
        "98: li a0, 0\n   j 96b\n"
        "99: li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_value)

        /* dbus_wire_body_valid(a0=body, a1=len, a2=sig, a3=sig_len, a4=fds, a5=be) */
        DBUS_ASM_FUNC(dbus_wire_body_valid)
        "bltz a1, 3f\n   bltz a3, 3f\n   beqz a2, 3f\n   bnez a3, 10f\n   seqz a0, a1\n" DBUS_ASM_RET
        "10: beqz a0, 3f\n   addi sp, sp, -80\n   sd ra, 72(sp)\n"
        "sd a0, 0(sp)\n   sd zero, 8(sp)\n   sd a1, 16(sp)\n   sd a2, 24(sp)\n"
        "sd a3, 32(sp)\n   sd zero, 40(sp)\n   sd a4, 48(sp)\n   sw zero, 56(sp)\n   sw a5, 60(sp)\n"
        "1:  ld t0, 40(sp)\n   ld t1, 32(sp)\n   bge t0, t1, 2f\n   mv a0, sp\n   call " DBUS_ASM_SYM(dbus_wire_value) "\n"
        "bnez a0, 1b\n   ld ra, 72(sp)\n   addi sp, sp, 80\n" DBUS_ASM_RET
        "2:  ld t0, 8(sp)\n   ld t1, 16(sp)\n   sub t0, t0, t1\n   seqz a0, t0\n   ld ra, 72(sp)\n   addi sp, sp, 80\n" DBUS_ASM_RET
        "3:  li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_body_valid)

        /* dbus_wire_header_check(a0=b, a1=size, a2=max_size, a3=out) */
        DBUS_ASM_FUNC(dbus_wire_header_check)
        "beqz a0, 5f\n   beqz a3, 5f\n   li t0, 16\n   blt a1, t0, 5f\n   bgt a1, a2, 5f\n"
        "lbu t0, 0(a0)\n   li t1, 0\n   li t2, 108\n   beq t0, t2, 1f\n   li t2, 66\n   bne t0, t2, 5f\n   li t1, 1\n"
        "1:  lbu t2, 3(a0)\n   li t3, 1\n   bne t2, t3, 5f\n   lbu a4, 1(a0)\n   addi t2, a4, -1\n   li t3, 3\n   bgtu t2, t3, 5f\n"
        "lbu a5, 2(a0)\n   bnez t1, 10f\n"
        "lbu t2, 4(a0)\n   lbu t3, 5(a0)\n   lbu t4, 6(a0)\n   lbu t5, 7(a0)\n   slli t3, t3, 8\n   slli t4, t4, 16\n   slli t5, t5, 24\n   or a6, t2, t3\n   or t4, t4, t5\n   or a6, a6, t4\n"
        "lbu t2, 8(a0)\n   lbu t3, 9(a0)\n   lbu t4, 10(a0)\n   lbu t5, 11(a0)\n   slli t3, t3, 8\n   slli t4, t4, 16\n   slli t5, t5, 24\n   or a7, t2, t3\n   or t4, t4, t5\n   or a7, a7, t4\n"
        "lbu t2, 12(a0)\n   lbu t3, 13(a0)\n   lbu t4, 14(a0)\n   lbu t5, 15(a0)\n   slli t3, t3, 8\n   slli t4, t4, 16\n   slli t5, t5, 24\n   or t0, t2, t3\n   or t4, t4, t5\n   or t0, t0, t4\n   j 2f\n"
        "10: lbu t2, 4(a0)\n   lbu t3, 5(a0)\n   lbu t4, 6(a0)\n   lbu t5, 7(a0)\n   slli t2, t2, 24\n   slli t3, t3, 16\n   slli t4, t4, 8\n   or a6, t2, t3\n   or t4, t4, t5\n   or a6, a6, t4\n"
        "lbu t2, 8(a0)\n   lbu t3, 9(a0)\n   lbu t4, 10(a0)\n   lbu t5, 11(a0)\n   slli t2, t2, 24\n   slli t3, t3, 16\n   slli t4, t4, 8\n   or a7, t2, t3\n   or t4, t4, t5\n   or a7, a7, t4\n"
        "lbu t2, 12(a0)\n   lbu t3, 13(a0)\n   lbu t4, 14(a0)\n   lbu t5, 15(a0)\n   slli t2, t2, 24\n   slli t3, t3, 16\n   slli t4, t4, 8\n   or t0, t2, t3\n   or t4, t4, t5\n   or t0, t0, t4\n"
        "2:  beqz a7, 5f\n   addi t2, a1, -16\n   bgtu t0, t2, 5f\n   bgtu a6, a1, 5f\n"
        "addi t2, t0, 16\n   addi t3, t2, 7\n   andi t3, t3, -8\n   add t4, t3, a6\n   bne t4, a1, 5f\n   mv t4, t2\n"
        "3:  bgeu t4, t3, 4f\n   add t5, a0, t4\n   lbu t6, 0(t5)\n   bnez t6, 5f\n   addi t4, t4, 1\n   j 3b\n"
        "4:  sw a7, 0(a3)\n   sw a6, 4(a3)\n   sw t0, 8(a3)\n   sb a4, 12(a3)\n   sb a5, 13(a3)\n"
        "sb t1, 14(a3)\n   sb zero, 15(a3)\n   sd t2, 16(a3)\n   sd t3, 24(a3)\n   li a0, 1\n" DBUS_ASM_RET
        "5:  li a0, 0\n" DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_header_check)

        /* dbus_wire_o_pad(a0=w, a1=align) */
        DBUS_ASM_FUNC(dbus_wire_o_pad)
        "beqz a0, 3f\n   lw t0, 24(a0)\n   bnez t0, 3f\n   ld t0, 8(a0)\n   ld t1, 16(a0)\n"
        "addi t2, a1, -1\n   add t3, t0, t2\n   not t2, t2\n   and t3, t3, t2\n   bgt t3, t1, 2f\n   ld t4, 0(a0)\n"
        "1:  bgeu t0, t3, 4f\n   add t5, t4, t0\n   sb zero, 0(t5)\n   addi t0, t0, 1\n   j 1b\n"
        "4:  sd t3, 8(a0)\n" DBUS_ASM_RET
        "2:  li t0, 1\n   sw t0, 24(a0)\n"
        "3:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_pad)

        /* dbus_wire_o_u32(a0=w, a1=v) */
        DBUS_ASM_FUNC(dbus_wire_o_u32)
        "beqz a0, 3f\n   lw t0, 24(a0)\n   bnez t0, 3f\n   ld t0, 8(a0)\n   ld t1, 16(a0)\n"
        "addi t2, t0, 3\n   andi t2, t2, -4\n   bgt t2, t1, 2f\n   sub t3, t1, t2\n   li t4, 4\n   blt t3, t4, 2f\n"
        "ld t4, 0(a0)\n"
        "1:  bgeu t0, t2, 4f\n   add t5, t4, t0\n   sb zero, 0(t5)\n   addi t0, t0, 1\n   j 1b\n"
        "4:  add t5, t4, t2\n   sb a1, 0(t5)\n   srli t6, a1, 8\n   sb t6, 1(t5)\n   srli t6, a1, 16\n   sb t6, 2(t5)\n"
        "srli t6, a1, 24\n   sb t6, 3(t5)\n   addi t2, t2, 4\n   sd t2, 8(a0)\n" DBUS_ASM_RET
        "2:  li t0, 1\n   sw t0, 24(a0)\n"
        "3:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_u32)

        /* dbus_wire_o_string(a0=w, a1=s) */
        DBUS_ASM_FUNC(dbus_wire_o_string)
        "beqz a0, 5f\n   lw t0, 24(a0)\n   bnez t0, 5f\n   beqz a1, 4f\n   li t0, 0\n"
        "1:  add t1, a1, t0\n   lbu t2, 0(t1)\n   beqz t2, 2f\n   addi t0, t0, 1\n   j 1b\n"
        "2:  ld t1, 8(a0)\n   ld t2, 16(a0)\n   addi t3, t1, 3\n   andi t3, t3, -4\n"
        "bgt t3, t2, 4f\n   sub t4, t2, t3\n   addi t5, t0, 5\n   blt t4, t5, 4f\n   ld t6, 0(a0)\n"
        "3:  bgeu t1, t3, 6f\n   add t4, t6, t1\n   sb zero, 0(t4)\n   addi t1, t1, 1\n   j 3b\n"
        "6:  add t4, t6, t3\n   sb t0, 0(t4)\n   srli t5, t0, 8\n   sb t5, 1(t4)\n   srli t5, t0, 16\n   sb t5, 2(t4)\n"
        "srli t5, t0, 24\n   sb t5, 3(t4)\n   addi t3, t3, 4\n   add a2, t6, t3\n   addi a3, t0, 1\n   li t1, 0\n"
        "7:  add t4, a1, t1\n   lbu t5, 0(t4)\n   add t4, a2, t1\n   sb t5, 0(t4)\n   addi t1, t1, 1\n   bltu t1, a3, 7b\n"
        "add t3, t3, a3\n   sd t3, 8(a0)\n" DBUS_ASM_RET
        "4:  li t0, 1\n   sw t0, 24(a0)\n"
        "5:  " DBUS_ASM_RET
        DBUS_ASM_END(dbus_wire_o_string)
);

#endif

#endif /* DBUS_WIRE_ASM_IMPLEMENTATION */
