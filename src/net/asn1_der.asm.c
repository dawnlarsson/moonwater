/*
        X.690 / RFC 5280 strict canonical ASN.1 DER primitives in pure assembly:
          - asn1_der_length           : canonical DER length (short or 1..3-byte minimal long form)
          - asn1_der_enter            : tag match + canonical DER length + bounds check
          - asn1_der_skip             : skip one DER TLV of any tag with full canonical checks
          - asn1_der_take             : capture one whole DER TLV (header + content) and advance
          - asn1_der_true             : optional canonical BOOLEAN DEFAULT FALSE (absent or 01 01 ff)
          - asn1_der_null_or_absent   : AlgorithmIdentifier NULL (05 00) or absent check
          - asn1_der_oid_content      : X.690 minimal base-128 OBJECT IDENTIFIER content validator
          - asn1_der_enter_oid        : enter tag 0x06 + validate canonical OID content
          - asn1_der_integer          : X.690 8.3.2 minimal DER INTEGER content validator
          - asn1_der_named_bits       : canonical DER BIT STRING named-bit-list validator
          - asn1_der_positive_integer : strictly positive minimal DER INTEGER magnitude extractor
          - asn1_der_date_value       : RFC 5280 UTCTime (1950..2049) / GeneralizedTime (>=2050)
          - asn1_der_date_general     : RFC 6960 OCSP GeneralizedTime (1950..9999)
          - asn1_der_date_seconds     : YYYYMMDDHHMMSS -> proleptic Gregorian seconds (Hinnant)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define NET_ASN1_DER_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_NET_ASN1_DER_H
#define MOONWATER_NET_ASN1_DER_H

#ifdef STANDARD_MODERN_C
typedef p8  der_u8;
typedef p32 der_u32;
typedef b32 der_i32;
typedef p64 der_u64;
typedef b64 der_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
    defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  der_u8;
typedef __UINT32_TYPE__ der_u32;
typedef __INT32_TYPE__  der_i32;
typedef __UINT64_TYPE__ der_u64;
typedef __INT64_TYPE__  der_i64;
#else
#include <stdint.h>
typedef uint8_t  der_u8;
typedef uint32_t der_u32;
typedef int32_t  der_i32;
typedef uint64_t der_u64;
typedef int64_t  der_i64;
#endif

#define ASN1_DER_OK   0
#define ASN1_DER_FAIL (-1)

der_i32 asn1_der_length(const der_u8 *bytes, der_u64 size, der_u64 *at, der_u64 *length);
der_i32 asn1_der_enter(const der_u8 *bytes, der_u64 size, der_u32 tag, der_u64 *at, der_u64 *stop);
der_i32 asn1_der_skip(const der_u8 *bytes, der_u64 size, der_u64 *at);
der_i32 asn1_der_take(const der_u8 *bytes, der_u64 size, der_u32 tag, der_u64 *at,
                      const der_u8 **value, der_u64 *length);
der_i32 asn1_der_true(const der_u8 *bytes, der_u64 stop, der_u64 *at, der_u8 *value);
der_i32 asn1_der_null_or_absent(const der_u8 *der, der_u64 at, der_u64 stop);
der_i32 asn1_der_oid_content(const der_u8 *bytes, der_u64 length);
der_i32 asn1_der_enter_oid(const der_u8 *bytes, der_u64 size, der_u64 *at, der_u64 *stop);
der_i32 asn1_der_integer(const der_u8 *bytes, der_u64 length);
der_i32 asn1_der_named_bits(const der_u8 *bytes, der_u64 length);
der_i32 asn1_der_positive_integer(const der_u8 *bytes, der_u64 at, der_u64 stop,
                                  der_u64 *value_at, der_u64 *value_length);
der_i32 asn1_der_date_value(der_u32 tag, const der_u8 *text, der_u64 length, der_u64 *value);
der_i32 asn1_der_date_general(const der_u8 *text, der_u64 length, der_u64 *value);
der_u64 asn1_der_date_seconds(der_u64 date);

#endif /* MOONWATER_NET_ASN1_DER_H */

#ifdef NET_ASN1_DER_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define DER_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define DER_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define DER_RISCV64 1
# else
#  error "src/net/asn1_der.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define DER_ARM64 1
# elif defined(X64) && X64
#  define DER_X64 1
# elif defined(RISCV64) && RISCV64
#  define DER_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define DER_ASM_SECTION   ASM_SECTION
# define DER_ASM_FUNC(n)   ASM_FUNC(n)
# define DER_ASM_END(n)    ASM_END(n)
# define DER_ASM_RET       ASM_RET
#else
# define DER_ASM_SECTION   ".text\n"
# define DER_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define DER_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define DER_ASM_END(n)   ""
# elif defined(_WIN32)
#  define DER_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define DER_ASM_END(n)   ""
# else
#  if defined(DER_ARM64)
#   define DER_ASM_FTYPE   "%function"
#  else
#   define DER_ASM_FTYPE   "@function"
#  endif
#  define DER_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " DER_ASM_FTYPE "\n" #n ":\n"
#  define DER_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(DER_ARM64)

__asm__(
        DER_ASM_SECTION

        /* asn1_der_length(x0=bytes, x1=size, x2=at*, x3=length*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_length)
        "cbz x0, 9f\n   cbz x2, 9f\n   cbz x3, 9f\n   ldr x4, [x2]\n   cmp x4, x1\n   b.hs 9f\n"
        "ldrb w5, [x0, x4]\n   add x4, x4, #1\n   cmp w5, #0x80\n   b.hs 1f\n"
        "str x5, [x3]\n   str x4, [x2]\n   mov w0, #0\n" DER_ASM_RET
        "1:  and w6, w5, #0x7f\n   cbz w6, 9f\n   cmp w6, #3\n   b.hi 9f\n"
        "sub x7, x1, x4\n   cmp x7, x6\n   b.lo 9f\n   ldrb w5, [x0, x4]\n   cbz w5, 9f\n   mov x7, #0\n"
        "2:  ldrb w5, [x0, x4]\n   add x4, x4, #1\n   orr x7, x5, x7, lsl #8\n   subs w6, w6, #1\n   b.ne 2b\n"
        "cmp x7, #0x80\n   b.lo 9f\n   str x7, [x3]\n   str x4, [x2]\n   mov w0, #0\n" DER_ASM_RET
        "9:  mov w0, #-1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_length)

        /* asn1_der_enter(x0=bytes, x1=size, w2=tag, x3=at*, x4=stop*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_enter)
        "cbz x0, 9f\n   cbz x3, 9f\n   cbz x4, 9f\n   ldr x5, [x3]\n   cmp x5, x1\n   b.hs 9f\n"
        "ldrb w6, [x0, x5]\n   and w2, w2, #0xff\n   cmp w6, w2\n   b.ne 9f\n"
        "add x5, x5, #1\n   cmp x5, x1\n   b.hs 9f\n   ldrb w6, [x0, x5]\n   add x5, x5, #1\n"
        "cmp w6, #0x80\n   b.lo 3f\n   and w7, w6, #0x7f\n   cbz w7, 9f\n   cmp w7, #3\n   b.hi 9f\n"
        "sub x8, x1, x5\n   cmp x8, x7\n   b.lo 9f\n   ldrb w6, [x0, x5]\n   cbz w6, 9f\n   mov x6, #0\n"
        "2:  ldrb w8, [x0, x5]\n   add x5, x5, #1\n   orr x6, x8, x6, lsl #8\n   subs w7, w7, #1\n   b.ne 2b\n"
        "cmp x6, #0x80\n   b.lo 9f\n"
        "3:  sub x8, x1, x5\n   cmp x6, x8\n   b.hi 9f\n   add x6, x5, x6\n"
        "str x5, [x3]\n   str x6, [x4]\n   mov w0, #0\n" DER_ASM_RET
        "9:  mov w0, #-1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_enter)

        /* asn1_der_skip(x0=bytes, x1=size, x2=at*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_skip)
        "cbz x0, 9f\n   cbz x2, 9f\n   ldr x4, [x2]\n   cmp x4, x1\n   b.hs 9f\n"
        "add x4, x4, #1\n   cmp x4, x1\n   b.hs 9f\n   ldrb w5, [x0, x4]\n   add x4, x4, #1\n"
        "cmp w5, #0x80\n   b.lo 3f\n   and w6, w5, #0x7f\n   cbz w6, 9f\n   cmp w6, #3\n   b.hi 9f\n"
        "sub x7, x1, x4\n   cmp x7, x6\n   b.lo 9f\n   ldrb w5, [x0, x4]\n   cbz w5, 9f\n   mov x5, #0\n"
        "2:  ldrb w7, [x0, x4]\n   add x4, x4, #1\n   orr x5, x7, x5, lsl #8\n   subs w6, w6, #1\n   b.ne 2b\n"
        "cmp x5, #0x80\n   b.lo 9f\n"
        "3:  sub x7, x1, x4\n   cmp x5, x7\n   b.hi 9f\n   add x4, x4, x5\n"
        "str x4, [x2]\n   mov w0, #0\n" DER_ASM_RET
        "9:  mov w0, #-1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_skip)

        /* asn1_der_take(x0=bytes, x1=size, w2=tag, x3=at*, x4=value**, x5=length*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_take)
        "cbz x0, 9f\n   cbz x3, 9f\n   cbz x4, 9f\n   cbz x5, 9f\n   ldr x9, [x3]\n   cmp x9, x1\n   b.hs 9f\n"
        "ldrb w6, [x0, x9]\n   and w2, w2, #0xff\n   cmp w6, w2\n   b.ne 9f\n"
        "add x10, x9, #1\n   cmp x10, x1\n   b.hs 9f\n   ldrb w6, [x0, x10]\n   add x10, x10, #1\n"
        "cmp w6, #0x80\n   b.lo 3f\n   and w7, w6, #0x7f\n   cbz w7, 9f\n   cmp w7, #3\n   b.hi 9f\n"
        "sub x8, x1, x10\n   cmp x8, x7\n   b.lo 9f\n   ldrb w6, [x0, x10]\n   cbz w6, 9f\n   mov x6, #0\n"
        "2:  ldrb w8, [x0, x10]\n   add x10, x10, #1\n   orr x6, x8, x6, lsl #8\n   subs w7, w7, #1\n   b.ne 2b\n"
        "cmp x6, #0x80\n   b.lo 9f\n"
        "3:  sub x8, x1, x10\n   cmp x6, x8\n   b.hi 9f\n   add x10, x10, x6\n"
        "add x7, x0, x9\n   sub x8, x10, x9\n   str x7, [x4]\n   str x8, [x5]\n   str x10, [x3]\n   mov w0, #0\n" DER_ASM_RET
        "9:  mov w0, #-1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_take)

        /* asn1_der_true(x0=bytes, x1=stop, x2=at*, x3=value*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_true)
        "cbz x2, 9f\n   cbz x3, 9f\n   ldr x4, [x2]\n   cmp x4, x1\n   b.hs 8f\n"
        "cbz x0, 9f\n   ldrb w5, [x0, x4]\n   cmp w5, #1\n   b.ne 8f\n"
        "sub x6, x1, x4\n   cmp x6, #3\n   b.lo 9f\n   add x6, x0, x4\n"
        "ldrb w5, [x6, #1]\n   cmp w5, #1\n   b.ne 9f\n   ldrb w5, [x6, #2]\n   cmp w5, #0xff\n   b.ne 9f\n"
        "mov w5, #1\n   strb w5, [x3]\n   add x4, x4, #3\n   str x4, [x2]\n"
        "8:  mov w0, #0\n" DER_ASM_RET
        "9:  mov w0, #-1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_true)

        /* asn1_der_null_or_absent(x0=der, x1=at, x2=stop) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_null_or_absent)
        "cmp x1, x2\n   b.eq 1f\n   b.hi 0f\n   cbz x0, 0f\n   sub x3, x2, x1\n   cmp x3, #2\n   b.ne 0f\n"
        "add x0, x0, x1\n   ldrb w4, [x0]\n   cmp w4, #5\n   b.ne 0f\n   ldrb w4, [x0, #1]\n   cbnz w4, 0f\n"
        "1:  mov w0, #1\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_null_or_absent)

        /* asn1_der_oid_content(x0=bytes, x1=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_oid_content)
        "cbz x0, 0f\n   cbz x1, 0f\n   add x1, x0, x1\n"
        "1:  cmp x0, x1\n   b.hs 3f\n   ldrb w2, [x0], #1\n   cmp w2, #0x80\n   b.eq 0f\n"
        "2:  tbz w2, #7, 1b\n   cmp x0, x1\n   b.hs 0f\n   ldrb w2, [x0], #1\n   b 2b\n"
        "3:  mov w0, #1\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_oid_content)

        /* asn1_der_enter_oid(x0=bytes, x1=size, x2=at*, x3=stop*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_enter_oid)
        "cbz x0, 9f\n   cbz x2, 9f\n   cbz x3, 9f\n   ldr x4, [x2]\n   cmp x4, x1\n   b.hs 9f\n"
        "ldrb w5, [x0, x4]\n   cmp w5, #6\n   b.ne 9f\n   add x4, x4, #1\n   cmp x4, x1\n   b.hs 9f\n"
        "ldrb w5, [x0, x4]\n   add x4, x4, #1\n   cmp w5, #0x80\n   b.lo 3f\n"
        "and w6, w5, #0x7f\n   cbz w6, 9f\n   cmp w6, #3\n   b.hi 9f\n"
        "sub x7, x1, x4\n   cmp x7, x6\n   b.lo 9f\n   ldrb w5, [x0, x4]\n   cbz w5, 9f\n   mov x5, #0\n"
        "2:  ldrb w7, [x0, x4]\n   add x4, x4, #1\n   orr x5, x7, x5, lsl #8\n   subs w6, w6, #1\n   b.ne 2b\n"
        "cmp x5, #0x80\n   b.lo 9f\n"
        "3:  cbz x5, 9f\n   sub x7, x1, x4\n   cmp x5, x7\n   b.hi 9f\n"
        "add x6, x4, x5\n   add x7, x0, x4\n   add x8, x0, x6\n"
        "4:  cmp x7, x8\n   b.hs 6f\n   ldrb w9, [x7], #1\n   cmp w9, #0x80\n   b.eq 9f\n"
        "5:  tbz w9, #7, 4b\n   cmp x7, x8\n   b.hs 9f\n   ldrb w9, [x7], #1\n   b 5b\n"
        "6:  str x4, [x2]\n   str x6, [x3]\n   mov w0, #0\n" DER_ASM_RET
        "9:  mov w0, #-1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_enter_oid)

        /* asn1_der_integer(x0=bytes, x1=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_integer)
        "cbz x0, 0f\n   cbz x1, 0f\n   cmp x1, #1\n   b.eq 1f\n"
        "ldrb w2, [x0]\n   ldrb w3, [x0, #1]\n   cbnz w2, 2f\n   tbz w3, #7, 0f\n   b 1f\n"
        "2:  cmp w2, #0xff\n   b.ne 1f\n   tbnz w3, #7, 0f\n"
        "1:  mov w0, #1\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_integer)

        /* asn1_der_named_bits(x0=bytes, x1=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_named_bits)
        "cbz x0, 0f\n   cbz x1, 0f\n   ldrb w2, [x0]\n   cmp w2, #7\n   b.hi 0f\n"
        "cmp x1, #1\n   b.ne 1f\n   cmp w2, #0\n   cset w0, eq\n" DER_ASM_RET
        "1:  sub x1, x1, #1\n   ldrb w3, [x0, x1]\n   neg w4, w3\n   and w3, w3, w4\n"
        "mov w5, #1\n   lsl w5, w5, w2\n   cmp w3, w5\n   cset w0, eq\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_named_bits)

        /* asn1_der_positive_integer(x0=bytes, x1=at, x2=stop, x3=value_at*, x4=value_len*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_positive_integer)
        "cbz x0, 0f\n   cbz x3, 0f\n   cbz x4, 0f\n   cmp x1, x2\n   b.hs 0f\n"
        "ldrb w5, [x0, x1]\n   tbnz w5, #7, 0f\n   cbnz w5, 1f\n"
        "add x1, x1, #1\n   cmp x1, x2\n   b.hs 0f\n   ldrb w5, [x0, x1]\n   tbz w5, #7, 0f\n"
        "1:  sub x2, x2, x1\n   str x1, [x3]\n   str x2, [x4]\n   mov w0, #1\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_positive_integer)

        /* asn1_der_date_value(w0=tag, x1=text, x2=length, x3=value*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_date_value)
        "cbz x1, 0f\n   cbz x3, 0f\n   cmp w0, #0x17\n   b.eq 1f\n   cmp w0, #0x18\n   b.ne 0f\n"
        "mov x4, #4\n   cmp x2, #15\n   b.ne 0f\n   b 2f\n"
        "1:  mov x4, #2\n   cmp x2, #13\n   b.ne 0f\n"
        "2:  sub x5, x2, #1\n   ldrb w6, [x1, x5]\n   cmp w6, #90\n   b.ne 0f\n   mov x6, #0\n"
        "3:  ldrb w7, [x1, x6]\n   sub w7, w7, #48\n   cmp w7, #9\n   b.hi 0f\n"
        "add x6, x6, #1\n   cmp x6, x5\n   b.lo 3b\n"
        "ldrb w7, [x1]\n   sub w7, w7, #48\n   ldrb w8, [x1, #1]\n   sub w8, w8, #48\n"
        "mov w9, #10\n   madd w7, w7, w9, w8\n   cmp x4, #2\n   b.ne 4f\n"
        "mov w8, #1900\n   mov w10, #2000\n   cmp w7, #50\n   csel w8, w8, w10, hs\n   add w7, w7, w8\n   b 5f\n"
        "4:  ldrb w8, [x1, #2]\n   sub w8, w8, #48\n   madd w7, w7, w9, w8\n"
        "ldrb w8, [x1, #3]\n   sub w8, w8, #48\n   madd w7, w7, w9, w8\n   cmp w7, #2050\n   b.lo 0f\n"
        "5:  add x1, x1, x4\n"
        "ldrb w8, [x1]\n   sub w8, w8, #48\n   ldrb w10, [x1, #1]\n   sub w10, w10, #48\n   madd w8, w8, w9, w10\n"
        "ldrb w10, [x1, #2]\n   sub w10, w10, #48\n   ldrb w11, [x1, #3]\n   sub w11, w11, #48\n   madd w10, w10, w9, w11\n"
        "ldrb w11, [x1, #4]\n   sub w11, w11, #48\n   ldrb w12, [x1, #5]\n   sub w12, w12, #48\n   madd w11, w11, w9, w12\n"
        "ldrb w12, [x1, #6]\n   sub w12, w12, #48\n   ldrb w13, [x1, #7]\n   sub w13, w13, #48\n   madd w12, w12, w9, w13\n"
        "ldrb w13, [x1, #8]\n   sub w13, w13, #48\n   ldrb w14, [x1, #9]\n   sub w14, w14, #48\n   madd w13, w13, w9, w14\n"
        "sub w14, w8, #1\n   cmp w14, #11\n   b.hi 0f\n   cbz w10, 0f\n"
        "cmp w11, #23\n   b.hi 0f\n   cmp w12, #59\n   b.hi 0f\n   cmp w13, #59\n   b.hi 0f\n"
        "cmp w8, #2\n   b.eq 6f\n   mov w14, #0x15aa\n   lsr w14, w14, w8\n   and w14, w14, #1\n   add w14, w14, #30\n   b 8f\n"
        "6:  mov w14, #28\n   tst w7, #3\n   b.ne 8f\n   mov w15, #5243\n   mul w15, w7, w15\n   lsr w15, w15, #19\n"
        "mov w16, #100\n   mul w16, w15, w16\n   cmp w16, w7\n   b.ne 7f\n   tst w15, #3\n   b.ne 8f\n"
        "7:  mov w14, #29\n"
        "8:  cmp w10, w14\n   b.hi 0f\n   mov x9, #100\n   mov x14, x7\n"
        "madd x14, x14, x9, x8\n   madd x14, x14, x9, x10\n   madd x14, x14, x9, x11\n"
        "madd x14, x14, x9, x12\n   madd x14, x14, x9, x13\n   str x14, [x3]\n   mov w0, #1\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_value)

        /* asn1_der_date_general(x0=text, x1=length, x2=value*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_date_general)
        "cbz x0, 0f\n   cbz x2, 0f\n   cmp x1, #15\n   b.ne 0f\n   ldrb w6, [x0, #14]\n   cmp w6, #90\n   b.ne 0f\n"
        "mov x6, #0\n"
        "3:  ldrb w7, [x0, x6]\n   sub w7, w7, #48\n   cmp w7, #9\n   b.hi 0f\n"
        "add x6, x6, #1\n   cmp x6, #14\n   b.lo 3b\n"
        "mov w9, #10\n   ldrb w7, [x0]\n   sub w7, w7, #48\n   ldrb w8, [x0, #1]\n   sub w8, w8, #48\n   madd w7, w7, w9, w8\n"
        "ldrb w8, [x0, #2]\n   sub w8, w8, #48\n   madd w7, w7, w9, w8\n"
        "ldrb w8, [x0, #3]\n   sub w8, w8, #48\n   madd w7, w7, w9, w8\n   cmp w7, #1950\n   b.lo 0f\n"
        "ldrb w8, [x0, #4]\n   sub w8, w8, #48\n   ldrb w10, [x0, #5]\n   sub w10, w10, #48\n   madd w8, w8, w9, w10\n"
        "ldrb w10, [x0, #6]\n   sub w10, w10, #48\n   ldrb w11, [x0, #7]\n   sub w11, w11, #48\n   madd w10, w10, w9, w11\n"
        "ldrb w11, [x0, #8]\n   sub w11, w11, #48\n   ldrb w12, [x0, #9]\n   sub w12, w12, #48\n   madd w11, w11, w9, w12\n"
        "ldrb w12, [x0, #10]\n   sub w12, w12, #48\n   ldrb w13, [x0, #11]\n   sub w13, w13, #48\n   madd w12, w12, w9, w13\n"
        "ldrb w13, [x0, #12]\n   sub w13, w13, #48\n   ldrb w14, [x0, #13]\n   sub w14, w14, #48\n   madd w13, w13, w9, w14\n"
        "sub w14, w8, #1\n   cmp w14, #11\n   b.hi 0f\n   cbz w10, 0f\n"
        "cmp w11, #23\n   b.hi 0f\n   cmp w12, #59\n   b.hi 0f\n   cmp w13, #59\n   b.hi 0f\n"
        "cmp w8, #2\n   b.eq 6f\n   mov w14, #0x15aa\n   lsr w14, w14, w8\n   and w14, w14, #1\n   add w14, w14, #30\n   b 8f\n"
        "6:  mov w14, #28\n   tst w7, #3\n   b.ne 8f\n   mov w15, #5243\n   mul w15, w7, w15\n   lsr w15, w15, #19\n"
        "mov w16, #100\n   mul w16, w15, w16\n   cmp w16, w7\n   b.ne 7f\n   tst w15, #3\n   b.ne 8f\n"
        "7:  mov w14, #29\n"
        "8:  cmp w10, w14\n   b.hi 0f\n   mov x9, #100\n   mov x14, x7\n"
        "madd x14, x14, x9, x8\n   madd x14, x14, x9, x10\n   madd x14, x14, x9, x11\n"
        "madd x14, x14, x9, x12\n   madd x14, x14, x9, x13\n   str x14, [x2]\n   mov w0, #1\n" DER_ASM_RET
        "0:  mov w0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_general)

        /* asn1_der_date_seconds(x0=date) -> u64 seconds */
        DER_ASM_FUNC(asn1_der_date_seconds)
        "mov x1, #100\n   udiv x2, x0, x1\n   msub x3, x2, x1, x0\n"
        "udiv x4, x2, x1\n   msub x5, x4, x1, x2\n   udiv x6, x4, x1\n   msub x7, x6, x1, x4\n"
        "udiv x8, x6, x1\n   msub x9, x8, x1, x6\n   udiv x10, x8, x1\n   msub x11, x10, x1, x8\n"
        "cbz x10, 0f\n   cbz x11, 0f\n   cbz x9, 0f\n"
        "sub x12, x10, #1\n   cmp x11, #2\n   csel x10, x12, x10, ls\n"
        "mov x12, #400\n   udiv x13, x10, x12\n   msub x14, x13, x12, x10\n"
        "sub x12, x11, #3\n   add x15, x11, #9\n   cmp x11, #2\n   csel x11, x12, x15, hi\n"
        "mov x12, #153\n   mul x11, x11, x12\n   add x11, x11, #2\n   mov x12, #5\n   udiv x11, x11, x12\n"
        "add x11, x11, x9\n   sub x11, x11, #1\n"
        "movz x12, #0x3ab1\n   movk x12, #2, lsl #16\n   madd x11, x13, x12, x11\n"
        "mov x12, #365\n   madd x11, x14, x12, x11\n   add x11, x11, x14, lsr #2\n"
        "udiv x12, x14, x1\n   sub x11, x11, x12\n"
        "mov x12, #24\n   madd x11, x11, x12, x7\n   mov x12, #60\n   madd x11, x11, x12, x5\n"
        "madd x0, x11, x12, x3\n" DER_ASM_RET
        "0:  mov x0, #0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_seconds)
);

#elif defined(DER_X64)

#if defined(_WIN32)
# define DER_LOAD2    "mov r8, rcx\n   mov r9, rdx\n"
# define DER_LOAD3    "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define DER_LOAD4    "mov r11, r9\n   mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define DER_LOAD5    "mov r11, r8\n   mov r8, [rsp + 40]\n   mov r10, rdx\n   xchg rcx, r9\n"
# define DER_LOAD6    "mov r11, r8\n   mov r8, [rsp + 48]\n   mov rbx, [rsp + 56]\n   mov r10, rdx\n   xchg rcx, r9\n"
# define DER_ARG1_RAX "mov rax, rcx\n"
#else
# define DER_LOAD2    "mov r8, rdi\n   mov r9, rsi\n"
# define DER_LOAD3    "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define DER_LOAD4    "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
# define DER_LOAD5    "mov r9, rdi\n   mov r10, rsi\n   mov r11, rdx\n"
# define DER_LOAD6    "mov rbx, r9\n   mov r9, rdi\n   mov r10, rsi\n   mov r11, rdx\n"
# define DER_ARG1_RAX "mov rax, rdi\n"
#endif

__asm__(
        ".intel_syntax noprefix\n"
        DER_ASM_SECTION

        /* asn1_der_length(r8=bytes, r9=size, r10=at*, r11=length*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_length)
        DER_LOAD4
        "test r8, r8\n   jz 9f\n   test r10, r10\n   jz 9f\n   test r11, r11\n   jz 9f\n"
        "mov rdx, [r10]\n   cmp rdx, r9\n   jae 9f\n   movzx eax, byte ptr [r8 + rdx]\n   inc rdx\n"
        "cmp eax, 0x80\n   jae 1f\n   mov [r11], rax\n   mov [r10], rdx\n   xor eax, eax\n" DER_ASM_RET
        "1:  and eax, 0x7f\n   jz 9f\n   cmp eax, 3\n   ja 9f\n   mov rcx, r9\n   sub rcx, rdx\n"
        "cmp rcx, rax\n   jb 9f\n   cmp byte ptr [r8 + rdx], 0\n   je 9f\n   mov ecx, eax\n   xor eax, eax\n"
        "2:  shl rax, 8\n   or al, byte ptr [r8 + rdx]\n   inc rdx\n   dec ecx\n   jnz 2b\n"
        "cmp rax, 0x80\n   jb 9f\n   mov [r11], rax\n   mov [r10], rdx\n   xor eax, eax\n" DER_ASM_RET
        "9:  mov eax, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_length)

        /* asn1_der_enter(r9=bytes, r10=size, r11=tag, rcx=at*, r8=stop*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_enter)
        DER_LOAD5
        "test r9, r9\n   jz 9f\n   test rcx, rcx\n   jz 9f\n   test r8, r8\n   jz 9f\n"
        "mov rdx, [rcx]\n   cmp rdx, r10\n   jae 9f\n   cmp byte ptr [r9 + rdx], r11b\n   jne 9f\n"
        "inc rdx\n   cmp rdx, r10\n   jae 9f\n   movzx eax, byte ptr [r9 + rdx]\n   inc rdx\n"
        "cmp eax, 0x80\n   jb 3f\n   and eax, 0x7f\n   jz 9f\n   cmp eax, 3\n   ja 9f\n"
        "mov r11, r10\n   sub r11, rdx\n   cmp r11, rax\n   jb 9f\n   cmp byte ptr [r9 + rdx], 0\n   je 9f\n"
        "mov r11d, eax\n   xor eax, eax\n"
        "2:  shl rax, 8\n   or al, byte ptr [r9 + rdx]\n   inc rdx\n   dec r11d\n   jnz 2b\n"
        "cmp rax, 0x80\n   jb 9f\n"
        "3:  mov r11, r10\n   sub r11, rdx\n   cmp rax, r11\n   ja 9f\n"
        "add rax, rdx\n   mov [rcx], rdx\n   mov [r8], rax\n   xor eax, eax\n" DER_ASM_RET
        "9:  mov eax, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_enter)

        /* asn1_der_skip(r8=bytes, r9=size, r10=at*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_skip)
        DER_LOAD3
        "test r8, r8\n   jz 9f\n   test r10, r10\n   jz 9f\n   mov rdx, [r10]\n   cmp rdx, r9\n   jae 9f\n"
        "inc rdx\n   cmp rdx, r9\n   jae 9f\n   movzx eax, byte ptr [r8 + rdx]\n   inc rdx\n"
        "cmp eax, 0x80\n   jb 3f\n   and eax, 0x7f\n   jz 9f\n   cmp eax, 3\n   ja 9f\n"
        "mov rcx, r9\n   sub rcx, rdx\n   cmp rcx, rax\n   jb 9f\n   cmp byte ptr [r8 + rdx], 0\n   je 9f\n"
        "mov ecx, eax\n   xor eax, eax\n"
        "2:  shl rax, 8\n   or al, byte ptr [r8 + rdx]\n   inc rdx\n   dec ecx\n   jnz 2b\n"
        "cmp rax, 0x80\n   jb 9f\n"
        "3:  mov rcx, r9\n   sub rcx, rdx\n   cmp rax, rcx\n   ja 9f\n"
        "add rdx, rax\n   mov [r10], rdx\n   xor eax, eax\n" DER_ASM_RET
        "9:  mov eax, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_skip)

        /* asn1_der_take(r9=bytes, r10=size, r11=tag, rcx=at*, r8=value**, rbx=length*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_take)
        "push rbx\n"
        DER_LOAD6
        "test r9, r9\n   jz 9f\n   test rcx, rcx\n   jz 9f\n   test r8, r8\n   jz 9f\n   test rbx, rbx\n   jz 9f\n"
        "mov rdx, [rcx]\n   cmp rdx, r10\n   jae 9f\n   cmp byte ptr [r9 + rdx], r11b\n   jne 9f\n"
        "mov r11, rdx\n   inc rdx\n   cmp rdx, r10\n   jae 9f\n   movzx eax, byte ptr [r9 + rdx]\n   inc rdx\n"
        "cmp eax, 0x80\n   jb 3f\n   and eax, 0x7f\n   jz 9f\n   cmp eax, 3\n   ja 9f\n"
        "push r11\n   mov r11, r10\n   sub r11, rdx\n   cmp r11, rax\n   jb 8f\n"
        "cmp byte ptr [r9 + rdx], 0\n   je 8f\n   mov r11d, eax\n   xor eax, eax\n"
        "2:  shl rax, 8\n   or al, byte ptr [r9 + rdx]\n   inc rdx\n   dec r11d\n   jnz 2b\n"
        "pop r11\n   cmp rax, 0x80\n   jb 9f\n"
        "3:  sub r10, rdx\n   cmp rax, r10\n   ja 9f\n   add rdx, rax\n"
        "lea rax, [r9 + r11]\n   mov [r8], rax\n   mov rax, rdx\n   sub rax, r11\n"
        "mov [rbx], rax\n   mov [rcx], rdx\n   xor eax, eax\n   pop rbx\n" DER_ASM_RET
        "8:  pop r11\n"
        "9:  mov eax, -1\n   pop rbx\n" DER_ASM_RET
        DER_ASM_END(asn1_der_take)

        /* asn1_der_true(r8=bytes, r9=stop, r10=at*, r11=value*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_true)
        DER_LOAD4
        "test r10, r10\n   jz 9f\n   test r11, r11\n   jz 9f\n   mov rdx, [r10]\n   cmp rdx, r9\n   jae 8f\n"
        "test r8, r8\n   jz 9f\n   cmp byte ptr [r8 + rdx], 1\n   jne 8f\n"
        "sub r9, rdx\n   cmp r9, 3\n   jb 9f\n   cmp byte ptr [r8 + rdx + 1], 1\n   jne 9f\n"
        "cmp byte ptr [r8 + rdx + 2], 0xff\n   jne 9f\n   mov byte ptr [r11], 1\n"
        "add rdx, 3\n   mov [r10], rdx\n"
        "8:  xor eax, eax\n" DER_ASM_RET
        "9:  mov eax, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_true)

        /* asn1_der_null_or_absent(r8=der, r9=at, r10=stop) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_null_or_absent)
        DER_LOAD3
        "cmp r9, r10\n   je 1f\n   ja 0f\n   test r8, r8\n   jz 0f\n   sub r10, r9\n   cmp r10, 2\n   jne 0f\n"
        "cmp byte ptr [r8 + r9], 5\n   jne 0f\n   cmp byte ptr [r8 + r9 + 1], 0\n   jne 0f\n"
        "1:  mov eax, 1\n" DER_ASM_RET
        "0:  xor eax, eax\n" DER_ASM_RET
        DER_ASM_END(asn1_der_null_or_absent)

        /* asn1_der_oid_content(r8=bytes, r9=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_oid_content)
        DER_LOAD2
        "test r8, r8\n   jz 0f\n   test r9, r9\n   jz 0f\n   add r9, r8\n"
        "1:  cmp r8, r9\n   jae 3f\n   movzx eax, byte ptr [r8]\n   inc r8\n   cmp eax, 0x80\n   je 0f\n"
        "2:  test eax, 0x80\n   jz 1b\n   cmp r8, r9\n   jae 0f\n   movzx eax, byte ptr [r8]\n   inc r8\n   jmp 2b\n"
        "3:  mov eax, 1\n" DER_ASM_RET
        "0:  xor eax, eax\n" DER_ASM_RET
        DER_ASM_END(asn1_der_oid_content)

        /* asn1_der_enter_oid(r8=bytes, r9=size, r10=at*, r11=stop*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_enter_oid)
        DER_LOAD4
        "test r8, r8\n   jz 9f\n   test r10, r10\n   jz 9f\n   test r11, r11\n   jz 9f\n"
        "mov rdx, [r10]\n   cmp rdx, r9\n   jae 9f\n   cmp byte ptr [r8 + rdx], 6\n   jne 9f\n"
        "inc rdx\n   cmp rdx, r9\n   jae 9f\n   movzx eax, byte ptr [r8 + rdx]\n   inc rdx\n"
        "cmp eax, 0x80\n   jb 3f\n   and eax, 0x7f\n   jz 9f\n   cmp eax, 3\n   ja 9f\n"
        "mov rcx, r9\n   sub rcx, rdx\n   cmp rcx, rax\n   jb 9f\n   cmp byte ptr [r8 + rdx], 0\n   je 9f\n"
        "mov ecx, eax\n   xor eax, eax\n"
        "2:  shl rax, 8\n   or al, byte ptr [r8 + rdx]\n   inc rdx\n   dec ecx\n   jnz 2b\n"
        "cmp rax, 0x80\n   jb 9f\n"
        "3:  test rax, rax\n   jz 9f\n   sub r9, rdx\n   cmp rax, r9\n   ja 9f\n"
        "lea r9, [rdx + rax]\n   lea rcx, [r8 + rdx]\n   add r8, r9\n"
        "4:  cmp rcx, r8\n   jae 6f\n   movzx eax, byte ptr [rcx]\n   inc rcx\n   cmp eax, 0x80\n   je 9f\n"
        "5:  test eax, 0x80\n   jz 4b\n   cmp rcx, r8\n   jae 9f\n   movzx eax, byte ptr [rcx]\n   inc rcx\n   jmp 5b\n"
        "6:  mov [r10], rdx\n   mov [r11], r9\n   xor eax, eax\n" DER_ASM_RET
        "9:  mov eax, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_enter_oid)

        /* asn1_der_integer(r8=bytes, r9=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_integer)
        DER_LOAD2
        "test r8, r8\n   jz 0f\n   test r9, r9\n   jz 0f\n   cmp r9, 1\n   je 1f\n"
        "movzx eax, byte ptr [r8]\n   movzx edx, byte ptr [r8 + 1]\n   test eax, eax\n   jnz 2f\n"
        "test edx, 0x80\n   jz 0f\n   jmp 1f\n"
        "2:  cmp eax, 0xff\n   jne 1f\n   test edx, 0x80\n   jnz 0f\n"
        "1:  mov eax, 1\n" DER_ASM_RET
        "0:  xor eax, eax\n" DER_ASM_RET
        DER_ASM_END(asn1_der_integer)

        /* asn1_der_named_bits(r8=bytes, r9=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_named_bits)
        DER_LOAD2
        "test r8, r8\n   jz 0f\n   test r9, r9\n   jz 0f\n   movzx ecx, byte ptr [r8]\n   cmp ecx, 7\n   ja 0f\n"
        "cmp r9, 1\n   jne 1f\n   xor eax, eax\n   test ecx, ecx\n   sete al\n" DER_ASM_RET
        "1:  movzx edx, byte ptr [r8 + r9 - 1]\n   mov eax, edx\n   neg eax\n   and edx, eax\n"
        "mov eax, 1\n   shl eax, cl\n   cmp edx, eax\n   sete al\n   movzx eax, al\n" DER_ASM_RET
        "0:  xor eax, eax\n" DER_ASM_RET
        DER_ASM_END(asn1_der_named_bits)

        /* asn1_der_positive_integer(r9=bytes, r10=at, r11=stop, rcx=value_at*, r8=value_len*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_positive_integer)
        DER_LOAD5
        "test r9, r9\n   jz 0f\n   test rcx, rcx\n   jz 0f\n   test r8, r8\n   jz 0f\n   cmp r10, r11\n   jae 0f\n"
        "movzx eax, byte ptr [r9 + r10]\n   test eax, 0x80\n   jnz 0f\n   test eax, eax\n   jnz 1f\n"
        "inc r10\n   cmp r10, r11\n   jae 0f\n   test byte ptr [r9 + r10], 0x80\n   jz 0f\n"
        "1:  sub r11, r10\n   mov [rcx], r10\n   mov [r8], r11\n   mov eax, 1\n" DER_ASM_RET
        "0:  xor eax, eax\n" DER_ASM_RET
        DER_ASM_END(asn1_der_positive_integer)

        /* asn1_der_date_value(r8=tag, r9=text, r10=length, r11=value*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_date_value)
        "push rbx\n   push r12\n"
        DER_LOAD4
        "test r9, r9\n   jz 0f\n   test r11, r11\n   jz 0f\n   cmp r8d, 0x17\n   je 1f\n   cmp r8d, 0x18\n   jne 0f\n"
        "mov rcx, 4\n   cmp r10, 15\n   jne 0f\n   jmp 2f\n"
        "1:  mov rcx, 2\n   cmp r10, 13\n   jne 0f\n"
        "2:  dec r10\n   cmp byte ptr [r9 + r10], 90\n   jne 0f\n   xor edx, edx\n"
        "3:  movzx eax, byte ptr [r9 + rdx]\n   sub eax, 48\n   cmp eax, 9\n   ja 0f\n"
        "inc rdx\n   cmp rdx, r10\n   jb 3b\n"
        "movzx eax, byte ptr [r9]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 1]\n   lea eax, [rax + rdx - 48]\n"
        "cmp rcx, 2\n   jne 4f\n   mov edx, 1900\n   mov ebx, 2000\n   cmp eax, 50\n   cmovb edx, ebx\n   add eax, edx\n   jmp 5f\n"
        "4:  imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 2]\n   lea eax, [rax + rdx - 48]\n"
        "imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 3]\n   lea eax, [rax + rdx - 48]\n   cmp eax, 2050\n   jb 0f\n"
        "5:  add r9, rcx\n   mov r8d, eax\n"
        "movzx eax, byte ptr [r9]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 1]\n   lea ecx, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 2]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 3]\n   lea r10d, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 4]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 5]\n   lea ebx, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 6]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 7]\n   lea r12d, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 8]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 9]\n   lea r9d, [rax + rdx - 48]\n"
        "lea eax, [rcx - 1]\n   cmp eax, 11\n   ja 0f\n   test r10d, r10d\n   jz 0f\n"
        "cmp ebx, 23\n   ja 0f\n   cmp r12d, 59\n   ja 0f\n   cmp r9d, 59\n   ja 0f\n"
        "cmp ecx, 2\n   je 6f\n   mov eax, 0x15aa\n   shr eax, cl\n   and eax, 1\n   add eax, 30\n   jmp 8f\n"
        "6:  mov eax, 28\n   test r8d, 3\n   jnz 8f\n   imul edx, r8d, 5243\n   shr edx, 19\n"
        "imul eax, edx, 100\n   cmp eax, r8d\n   jne 7f\n   test edx, 3\n   jnz 10f\n"
        "7:  mov eax, 29\n   jmp 8f\n   10: mov eax, 28\n"
        "8:  cmp r10d, eax\n   ja 0f\n   mov rax, r8\n"
        "imul rax, rax, 100\n   add rax, rcx\n   imul rax, rax, 100\n   add rax, r10\n"
        "imul rax, rax, 100\n   add rax, rbx\n   imul rax, rax, 100\n   add rax, r12\n"
        "imul rax, rax, 100\n   add rax, r9\n   mov [r11], rax\n   mov eax, 1\n   pop r12\n   pop rbx\n" DER_ASM_RET
        "0:  xor eax, eax\n   pop r12\n   pop rbx\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_value)

        /* asn1_der_date_general(r8=text, r9=length, r10=value*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_date_general)
        "push rbx\n   push r12\n"
        DER_LOAD3
        "test r8, r8\n   jz 0f\n   test r10, r10\n   jz 0f\n   cmp r9, 15\n   jne 0f\n"
        "cmp byte ptr [r8 + 14], 90\n   jne 0f\n   xor edx, edx\n"
        "3:  movzx eax, byte ptr [r8 + rdx]\n   sub eax, 48\n   cmp eax, 9\n   ja 0f\n"
        "inc rdx\n   cmp rdx, 14\n   jb 3b\n"
        "mov r9, r8\n   mov r11, r10\n"
        "movzx eax, byte ptr [r9]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 1]\n   lea eax, [rax + rdx - 48]\n"
        "imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 2]\n   lea eax, [rax + rdx - 48]\n"
        "imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 3]\n   lea eax, [rax + rdx - 48]\n   cmp eax, 1950\n   jb 0f\n"
        "mov r8d, eax\n   add r9, 4\n"
        "movzx eax, byte ptr [r9]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 1]\n   lea ecx, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 2]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 3]\n   lea r10d, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 4]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 5]\n   lea ebx, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 6]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 7]\n   lea r12d, [rax + rdx - 48]\n"
        "movzx eax, byte ptr [r9 + 8]\n   sub eax, 48\n   imul eax, eax, 10\n   movzx edx, byte ptr [r9 + 9]\n   lea r9d, [rax + rdx - 48]\n"
        "lea eax, [rcx - 1]\n   cmp eax, 11\n   ja 0f\n   test r10d, r10d\n   jz 0f\n"
        "cmp ebx, 23\n   ja 0f\n   cmp r12d, 59\n   ja 0f\n   cmp r9d, 59\n   ja 0f\n"
        "cmp ecx, 2\n   je 6f\n   mov eax, 0x15aa\n   shr eax, cl\n   and eax, 1\n   add eax, 30\n   jmp 8f\n"
        "6:  mov eax, 28\n   test r8d, 3\n   jnz 8f\n   imul edx, r8d, 5243\n   shr edx, 19\n"
        "imul eax, edx, 100\n   cmp eax, r8d\n   jne 7f\n   test edx, 3\n   jnz 10f\n"
        "7:  mov eax, 29\n   jmp 8f\n   10: mov eax, 28\n"
        "8:  cmp r10d, eax\n   ja 0f\n   mov rax, r8\n"
        "imul rax, rax, 100\n   add rax, rcx\n   imul rax, rax, 100\n   add rax, r10\n"
        "imul rax, rax, 100\n   add rax, rbx\n   imul rax, rax, 100\n   add rax, r12\n"
        "imul rax, rax, 100\n   add rax, r9\n   mov [r11], rax\n   mov eax, 1\n   pop r12\n   pop rbx\n" DER_ASM_RET
        "0:  xor eax, eax\n   pop r12\n   pop rbx\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_general)

        /* asn1_der_date_seconds(date) -> u64 seconds */
        DER_ASM_FUNC(asn1_der_date_seconds)
        "push rbx\n   push r12\n"
        DER_ARG1_RAX
        "mov ecx, 100\n   xor edx, edx\n   div rcx\n   mov r8, rdx\n"
        "xor edx, edx\n   div rcx\n   mov r9, rdx\n   xor edx, edx\n   div rcx\n   mov r10, rdx\n"
        "xor edx, edx\n   div rcx\n   mov r11, rdx\n   xor edx, edx\n   div rcx\n"
        "test rax, rax\n   jz 0f\n   test rdx, rdx\n   jz 0f\n   test r11, r11\n   jz 0f\n"
        "mov rbx, rdx\n   cmp rbx, 2\n   ja 1f\n   dec rax\n"
        "1:  mov r12d, 400\n   xor edx, edx\n   div r12\n   imul rax, rax, 146097\n"
        "imul r12, rdx, 365\n   add rax, r12\n   mov r12, rdx\n   shr r12, 2\n   add rax, r12\n"
        "imul r12, rdx, 41\n   shr r12, 12\n   sub rax, r12\n"
        "cmp rbx, 2\n   ja 2f\n   add rbx, 9\n   jmp 3f\n"
        "2:  sub rbx, 3\n"
        "3:  imul rbx, rbx, 153\n   add rbx, 2\n   imul rbx, rbx, 52429\n   shr rbx, 18\n"
        "add rax, rbx\n   add rax, r11\n   dec rax\n"
        "imul rax, rax, 24\n   add rax, r10\n   imul rax, rax, 60\n   add rax, r9\n"
        "imul rax, rax, 60\n   add rax, r8\n   pop r12\n   pop rbx\n" DER_ASM_RET
        "0:  xor eax, eax\n   pop r12\n   pop rbx\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_seconds)

        ".att_syntax prefix\n"
);

#elif defined(DER_RISCV64)

__asm__(
        DER_ASM_SECTION

        /* asn1_der_length(a0=bytes, a1=size, a2=at*, a3=length*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_length)
        "beqz a0, 9f\n   beqz a2, 9f\n   beqz a3, 9f\n   ld t0, 0(a2)\n   bgeu t0, a1, 9f\n"
        "add t1, a0, t0\n   lbu t2, 0(t1)\n   addi t0, t0, 1\n   li t3, 0x80\n   bgeu t2, t3, 1f\n"
        "sd t2, 0(a3)\n   sd t0, 0(a2)\n   li a0, 0\n" DER_ASM_RET
        "1:  andi t4, t2, 0x7f\n   beqz t4, 9f\n   li t5, 3\n   bgtu t4, t5, 9f\n"
        "sub t5, a1, t0\n   bltu t5, t4, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   beqz t2, 9f\n   li t5, 0\n"
        "2:  add t1, a0, t0\n   lbu t2, 0(t1)\n   addi t0, t0, 1\n   slli t5, t5, 8\n   or t5, t5, t2\n"
        "addi t4, t4, -1\n   bnez t4, 2b\n   bltu t5, t3, 9f\n"
        "sd t5, 0(a3)\n   sd t0, 0(a2)\n   li a0, 0\n" DER_ASM_RET
        "9:  li a0, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_length)

        /* asn1_der_enter(a0=bytes, a1=size, a2=tag, a3=at*, a4=stop*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_enter)
        "beqz a0, 9f\n   beqz a3, 9f\n   beqz a4, 9f\n   ld t0, 0(a3)\n   bgeu t0, a1, 9f\n"
        "add t1, a0, t0\n   lbu t2, 0(t1)\n   andi a2, a2, 0xff\n   bne t2, a2, 9f\n"
        "addi t0, t0, 1\n   bgeu t0, a1, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   addi t0, t0, 1\n"
        "li t3, 0x80\n   bltu t2, t3, 3f\n   andi t4, t2, 0x7f\n   beqz t4, 9f\n   li t5, 3\n   bgtu t4, t5, 9f\n"
        "sub t5, a1, t0\n   bltu t5, t4, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   beqz t2, 9f\n   li t2, 0\n"
        "2:  add t1, a0, t0\n   lbu t5, 0(t1)\n   addi t0, t0, 1\n   slli t2, t2, 8\n   or t2, t2, t5\n"
        "addi t4, t4, -1\n   bnez t4, 2b\n   bltu t2, t3, 9f\n"
        "3:  sub t5, a1, t0\n   bgtu t2, t5, 9f\n   add t2, t0, t2\n"
        "sd t0, 0(a3)\n   sd t2, 0(a4)\n   li a0, 0\n" DER_ASM_RET
        "9:  li a0, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_enter)

        /* asn1_der_skip(a0=bytes, a1=size, a2=at*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_skip)
        "beqz a0, 9f\n   beqz a2, 9f\n   ld t0, 0(a2)\n   bgeu t0, a1, 9f\n"
        "addi t0, t0, 1\n   bgeu t0, a1, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   addi t0, t0, 1\n"
        "li t3, 0x80\n   bltu t2, t3, 3f\n   andi t4, t2, 0x7f\n   beqz t4, 9f\n   li t5, 3\n   bgtu t4, t5, 9f\n"
        "sub t5, a1, t0\n   bltu t5, t4, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   beqz t2, 9f\n   li t2, 0\n"
        "2:  add t1, a0, t0\n   lbu t5, 0(t1)\n   addi t0, t0, 1\n   slli t2, t2, 8\n   or t2, t2, t5\n"
        "addi t4, t4, -1\n   bnez t4, 2b\n   bltu t2, t3, 9f\n"
        "3:  sub t5, a1, t0\n   bgtu t2, t5, 9f\n   add t0, t0, t2\n"
        "sd t0, 0(a2)\n   li a0, 0\n" DER_ASM_RET
        "9:  li a0, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_skip)

        /* asn1_der_take(a0=bytes, a1=size, a2=tag, a3=at*, a4=value**, a5=length*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_take)
        "beqz a0, 9f\n   beqz a3, 9f\n   beqz a4, 9f\n   beqz a5, 9f\n   ld t6, 0(a3)\n   bgeu t6, a1, 9f\n"
        "add t1, a0, t6\n   lbu t2, 0(t1)\n   andi a2, a2, 0xff\n   bne t2, a2, 9f\n"
        "addi t0, t6, 1\n   bgeu t0, a1, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   addi t0, t0, 1\n"
        "li t3, 0x80\n   bltu t2, t3, 3f\n   andi t4, t2, 0x7f\n   beqz t4, 9f\n   li t5, 3\n   bgtu t4, t5, 9f\n"
        "sub t5, a1, t0\n   bltu t5, t4, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   beqz t2, 9f\n   li t2, 0\n"
        "2:  add t1, a0, t0\n   lbu t5, 0(t1)\n   addi t0, t0, 1\n   slli t2, t2, 8\n   or t2, t2, t5\n"
        "addi t4, t4, -1\n   bnez t4, 2b\n   bltu t2, t3, 9f\n"
        "3:  sub t5, a1, t0\n   bgtu t2, t5, 9f\n   add t0, t0, t2\n"
        "add t1, a0, t6\n   sub t2, t0, t6\n   sd t1, 0(a4)\n   sd t2, 0(a5)\n   sd t0, 0(a3)\n   li a0, 0\n" DER_ASM_RET
        "9:  li a0, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_take)

        /* asn1_der_true(a0=bytes, a1=stop, a2=at*, a3=value*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_true)
        "beqz a2, 9f\n   beqz a3, 9f\n   ld t0, 0(a2)\n   bgeu t0, a1, 8f\n"
        "beqz a0, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   li t3, 1\n   bne t2, t3, 8f\n"
        "sub t4, a1, t0\n   li t5, 3\n   bltu t4, t5, 9f\n   lbu t2, 1(t1)\n   bne t2, t3, 9f\n"
        "lbu t2, 2(t1)\n   li t4, 0xff\n   bne t2, t4, 9f\n   sb t3, 0(a3)\n   addi t0, t0, 3\n   sd t0, 0(a2)\n"
        "8:  li a0, 0\n" DER_ASM_RET
        "9:  li a0, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_true)

        /* asn1_der_null_or_absent(a0=der, a1=at, a2=stop) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_null_or_absent)
        "beq a1, a2, 1f\n   bgtu a1, a2, 0f\n   beqz a0, 0f\n   sub t0, a2, a1\n   li t1, 2\n   bne t0, t1, 0f\n"
        "add t0, a0, a1\n   lbu t1, 0(t0)\n   li t2, 5\n   bne t1, t2, 0f\n   lbu t1, 1(t0)\n   bnez t1, 0f\n"
        "1:  li a0, 1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_null_or_absent)

        /* asn1_der_oid_content(a0=bytes, a1=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_oid_content)
        "beqz a0, 0f\n   beqz a1, 0f\n   add a1, a0, a1\n   li t1, 0x80\n"
        "1:  bgeu a0, a1, 3f\n   lbu t0, 0(a0)\n   addi a0, a0, 1\n   beq t0, t1, 0f\n"
        "2:  andi t2, t0, 0x80\n   beqz t2, 1b\n   bgeu a0, a1, 0f\n   lbu t0, 0(a0)\n   addi a0, a0, 1\n   j 2b\n"
        "3:  li a0, 1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_oid_content)

        /* asn1_der_enter_oid(a0=bytes, a1=size, a2=at*, a3=stop*) -> 0 / -1 */
        DER_ASM_FUNC(asn1_der_enter_oid)
        "beqz a0, 9f\n   beqz a2, 9f\n   beqz a3, 9f\n   ld t0, 0(a2)\n   bgeu t0, a1, 9f\n"
        "add t1, a0, t0\n   lbu t2, 0(t1)\n   li t3, 6\n   bne t2, t3, 9f\n"
        "addi t0, t0, 1\n   bgeu t0, a1, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   addi t0, t0, 1\n"
        "li t3, 0x80\n   bltu t2, t3, 3f\n   andi t4, t2, 0x7f\n   beqz t4, 9f\n   li t5, 3\n   bgtu t4, t5, 9f\n"
        "sub t5, a1, t0\n   bltu t5, t4, 9f\n   add t1, a0, t0\n   lbu t2, 0(t1)\n   beqz t2, 9f\n   li t2, 0\n"
        "2:  add t1, a0, t0\n   lbu t5, 0(t1)\n   addi t0, t0, 1\n   slli t2, t2, 8\n   or t2, t2, t5\n"
        "addi t4, t4, -1\n   bnez t4, 2b\n   bltu t2, t3, 9f\n"
        "3:  beqz t2, 9f\n   sub t5, a1, t0\n   bgtu t2, t5, 9f\n"
        "add t4, t0, t2\n   add t5, a0, t0\n   add t6, a0, t4\n"
        "4:  bgeu t5, t6, 6f\n   lbu t1, 0(t5)\n   addi t5, t5, 1\n   beq t1, t3, 9f\n"
        "5:  andi t2, t1, 0x80\n   beqz t2, 4b\n   bgeu t5, t6, 9f\n   lbu t1, 0(t5)\n   addi t5, t5, 1\n   j 5b\n"
        "6:  sd t0, 0(a2)\n   sd t4, 0(a3)\n   li a0, 0\n" DER_ASM_RET
        "9:  li a0, -1\n" DER_ASM_RET
        DER_ASM_END(asn1_der_enter_oid)

        /* asn1_der_integer(a0=bytes, a1=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_integer)
        "beqz a0, 0f\n   beqz a1, 0f\n   li t0, 1\n   beq a1, t0, 1f\n"
        "lbu t1, 0(a0)\n   lbu t2, 1(a0)\n   andi t3, t2, 0x80\n   bnez t1, 2f\n   beqz t3, 0f\n   j 1f\n"
        "2:  li t4, 0xff\n   bne t1, t4, 1f\n   bnez t3, 0f\n"
        "1:  li a0, 1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_integer)

        /* asn1_der_named_bits(a0=bytes, a1=length) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_named_bits)
        "beqz a0, 0f\n   beqz a1, 0f\n   lbu t0, 0(a0)\n   li t1, 7\n   bgtu t0, t1, 0f\n"
        "li t1, 1\n   bne a1, t1, 1f\n   seqz a0, t0\n" DER_ASM_RET
        "1:  add t2, a0, a1\n   lbu t3, -1(t2)\n   neg t4, t3\n   and t3, t3, t4\n"
        "sll t1, t1, t0\n   xor t3, t3, t1\n   seqz a0, t3\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_named_bits)

        /* asn1_der_positive_integer(a0=bytes, a1=at, a2=stop, a3=value_at*, a4=value_len*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_positive_integer)
        "beqz a0, 0f\n   beqz a3, 0f\n   beqz a4, 0f\n   bgeu a1, a2, 0f\n"
        "add t0, a0, a1\n   lbu t1, 0(t0)\n   andi t2, t1, 0x80\n   bnez t2, 0f\n   bnez t1, 1f\n"
        "addi a1, a1, 1\n   bgeu a1, a2, 0f\n   lbu t1, 1(t0)\n   andi t2, t1, 0x80\n   beqz t2, 0f\n"
        "1:  sub t0, a2, a1\n   sd a1, 0(a3)\n   sd t0, 0(a4)\n   li a0, 1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_positive_integer)

        /* asn1_der_date_value(a0=tag, a1=text, a2=length, a3=value*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_date_value)
        "beqz a1, 0f\n   beqz a3, 0f\n   li t0, 0x17\n   beq a0, t0, 1f\n   li t0, 0x18\n   bne a0, t0, 0f\n"
        "li a4, 4\n   li t0, 15\n   bne a2, t0, 0f\n   j 2f\n"
        "1:  li a4, 2\n   li t0, 13\n   bne a2, t0, 0f\n"
        "2:  addi t0, a2, -1\n   add t1, a1, t0\n   lbu t2, 0(t1)\n   li t3, 90\n   bne t2, t3, 0f\n   li t1, 0\n   li t3, 9\n"
        "3:  add t2, a1, t1\n   lbu t2, 0(t2)\n   addi t2, t2, -48\n   bgtu t2, t3, 0f\n"
        "addi t1, t1, 1\n   bltu t1, t0, 3b\n"
        "li a5, 10\n   lbu t0, 0(a1)\n   addi t0, t0, -48\n   lbu t1, 1(a1)\n   addi t1, t1, -48\n"
        "mul t0, t0, a5\n   add t0, t0, t1\n   li t2, 2\n   bne a4, t2, 4f\n"
        "li t2, 50\n   li t3, 1900\n   bgeu t0, t2, 11f\n   li t3, 2000\n   11: add t0, t0, t3\n   j 5f\n"
        "4:  lbu t1, 2(a1)\n   addi t1, t1, -48\n   mul t0, t0, a5\n   add t0, t0, t1\n"
        "lbu t1, 3(a1)\n   addi t1, t1, -48\n   mul t0, t0, a5\n   add t0, t0, t1\n   li t2, 2050\n   bltu t0, t2, 0f\n"
        "5:  add a1, a1, a4\n"
        "lbu t1, 0(a1)\n   addi t1, t1, -48\n   lbu t2, 1(a1)\n   addi t2, t2, -48\n   mul t1, t1, a5\n   add t1, t1, t2\n"
        "lbu t2, 2(a1)\n   addi t2, t2, -48\n   lbu t3, 3(a1)\n   addi t3, t3, -48\n   mul t2, t2, a5\n   add t2, t2, t3\n"
        "lbu t3, 4(a1)\n   addi t3, t3, -48\n   lbu t4, 5(a1)\n   addi t4, t4, -48\n   mul t3, t3, a5\n   add t3, t3, t4\n"
        "lbu t4, 6(a1)\n   addi t4, t4, -48\n   lbu t5, 7(a1)\n   addi t5, t5, -48\n   mul t4, t4, a5\n   add t4, t4, t5\n"
        "lbu t5, 8(a1)\n   addi t5, t5, -48\n   lbu t6, 9(a1)\n   addi t6, t6, -48\n   mul t5, t5, a5\n   add t5, t5, t6\n"
        "addi t6, t1, -1\n   li a6, 11\n   bgtu t6, a6, 0f\n   beqz t2, 0f\n"
        "li a6, 23\n   bgtu t3, a6, 0f\n   li a6, 59\n   bgtu t4, a6, 0f\n   bgtu t5, a6, 0f\n"
        "li a6, 2\n   beq t1, a6, 6f\n   li a6, 0x15aa\n   srl a6, a6, t1\n   andi a6, a6, 1\n   addi a6, a6, 30\n   j 8f\n"
        "6:  li a6, 28\n   andi t6, t0, 3\n   bnez t6, 8f\n   li t6, 5243\n   mul t6, t0, t6\n   srli t6, t6, 19\n"
        "li a7, 100\n   mul a7, t6, a7\n   bne a7, t0, 7f\n   andi t6, t6, 3\n   bnez t6, 8f\n"
        "7:  li a6, 29\n"
        "8:  bgtu t2, a6, 0f\n   li a5, 100\n"
        "mul t0, t0, a5\n   add t0, t0, t1\n   mul t0, t0, a5\n   add t0, t0, t2\n"
        "mul t0, t0, a5\n   add t0, t0, t3\n   mul t0, t0, a5\n   add t0, t0, t4\n"
        "mul t0, t0, a5\n   add t0, t0, t5\n   sd t0, 0(a3)\n   li a0, 1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_value)

        /* asn1_der_date_general(a0=text, a1=length, a2=value*) -> 0 / 1 */
        DER_ASM_FUNC(asn1_der_date_general)
        "beqz a0, 0f\n   beqz a2, 0f\n   li t0, 15\n   bne a1, t0, 0f\n"
        "lbu t1, 14(a0)\n   li t2, 90\n   bne t1, t2, 0f\n   li t1, 0\n   li t2, 14\n   li t3, 9\n"
        "3:  add t4, a0, t1\n   lbu t4, 0(t4)\n   addi t4, t4, -48\n   bgtu t4, t3, 0f\n"
        "addi t1, t1, 1\n   bltu t1, t2, 3b\n"
        "li a5, 10\n   lbu t0, 0(a0)\n   addi t0, t0, -48\n   lbu t1, 1(a0)\n   addi t1, t1, -48\n"
        "mul t0, t0, a5\n   add t0, t0, t1\n   lbu t1, 2(a0)\n   addi t1, t1, -48\n   mul t0, t0, a5\n   add t0, t0, t1\n"
        "lbu t1, 3(a0)\n   addi t1, t1, -48\n   mul t0, t0, a5\n   add t0, t0, t1\n   li t2, 1950\n   bltu t0, t2, 0f\n"
        "lbu t1, 4(a0)\n   addi t1, t1, -48\n   lbu t2, 5(a0)\n   addi t2, t2, -48\n   mul t1, t1, a5\n   add t1, t1, t2\n"
        "lbu t2, 6(a0)\n   addi t2, t2, -48\n   lbu t3, 7(a0)\n   addi t3, t3, -48\n   mul t2, t2, a5\n   add t2, t2, t3\n"
        "lbu t3, 8(a0)\n   addi t3, t3, -48\n   lbu t4, 9(a0)\n   addi t4, t4, -48\n   mul t3, t3, a5\n   add t3, t3, t4\n"
        "lbu t4, 10(a0)\n   addi t4, t4, -48\n   lbu t5, 11(a0)\n   addi t5, t5, -48\n   mul t4, t4, a5\n   add t4, t4, t5\n"
        "lbu t5, 12(a0)\n   addi t5, t5, -48\n   lbu t6, 13(a0)\n   addi t6, t6, -48\n   mul t5, t5, a5\n   add t5, t5, t6\n"
        "addi t6, t1, -1\n   li a6, 11\n   bgtu t6, a6, 0f\n   beqz t2, 0f\n"
        "li a6, 23\n   bgtu t3, a6, 0f\n   li a6, 59\n   bgtu t4, a6, 0f\n   bgtu t5, a6, 0f\n"
        "li a6, 2\n   beq t1, a6, 6f\n   li a6, 0x15aa\n   srl a6, a6, t1\n   andi a6, a6, 1\n   addi a6, a6, 30\n   j 8f\n"
        "6:  li a6, 28\n   andi t6, t0, 3\n   bnez t6, 8f\n   li t6, 5243\n   mul t6, t0, t6\n   srli t6, t6, 19\n"
        "li a7, 100\n   mul a7, t6, a7\n   bne a7, t0, 7f\n   andi t6, t6, 3\n   bnez t6, 8f\n"
        "7:  li a6, 29\n"
        "8:  bgtu t2, a6, 0f\n   li a5, 100\n"
        "mul t0, t0, a5\n   add t0, t0, t1\n   mul t0, t0, a5\n   add t0, t0, t2\n"
        "mul t0, t0, a5\n   add t0, t0, t3\n   mul t0, t0, a5\n   add t0, t0, t4\n"
        "mul t0, t0, a5\n   add t0, t0, t5\n   sd t0, 0(a2)\n   li a0, 1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_general)

        /* asn1_der_date_seconds(a0=date) -> u64 seconds */
        DER_ASM_FUNC(asn1_der_date_seconds)
        "li t0, 100\n   divu t1, a0, t0\n   remu a1, a0, t0\n"
        "divu t2, t1, t0\n   remu a2, t1, t0\n   divu t3, t2, t0\n   remu a3, t2, t0\n"
        "divu t4, t3, t0\n   remu a4, t3, t0\n   divu a5, t4, t0\n   remu a6, t4, t0\n"
        "beqz a5, 0f\n   beqz a6, 0f\n   beqz a4, 0f\n"
        "li t1, 2\n   bgtu a6, t1, 1f\n   addi a5, a5, -1\n"
        "1:  li t2, 400\n   divu t3, a5, t2\n   remu t4, a5, t2\n"
        "bgtu a6, t1, 2f\n   addi a6, a6, 9\n   j 3f\n"
        "2:  addi a6, a6, -3\n"
        "3:  li t5, 153\n   mul a6, a6, t5\n   addi a6, a6, 2\n   li t5, 5\n   divu a6, a6, t5\n"
        "add a6, a6, a4\n   addi a6, a6, -1\n   li t5, 146097\n   mul t3, t3, t5\n   add a6, a6, t3\n"
        "li t5, 365\n   mul t5, t4, t5\n   add a6, a6, t5\n   srli t5, t4, 2\n   add a6, a6, t5\n"
        "divu t5, t4, t0\n   sub a6, a6, t5\n"
        "li t5, 24\n   mul a6, a6, t5\n   add a6, a6, a3\n   li t5, 60\n   mul a6, a6, t5\n"
        "add a6, a6, a2\n   mul a6, a6, t5\n   add a0, a6, a1\n" DER_ASM_RET
        "0:  li a0, 0\n" DER_ASM_RET
        DER_ASM_END(asn1_der_date_seconds)
);

#endif /* arch */
#endif /* NET_ASN1_DER_IMPLEMENTATION */
