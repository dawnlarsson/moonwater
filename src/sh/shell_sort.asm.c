/*
        POSIX / Moonwater-compatible sort core primitives in pure assembly:
          - sh_sort_number_window  : -n/-rn 64-bit order-preserving radix window + exact flag
          - sh_sort_human_order    : -h SI suffix rank (k/K..Q, signed -10..+10)
          - sh_sort_number_parse   : -n arbitrary-precision number view + <=8-digit word pack
          - sh_sort_compare_parsed : -n parsed-number comparator (packed word or exact digits)
          - sh_sort_translate      : -d/-i/-f key filter & ASCII uppercase fold
          - sh_sort_compare_bytes  : C-locale byte comparator (plain 8-byte SWAR, -f, -d, -i)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_SORT_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_SORT_H
#define MOONWATER_SH_SHELL_SORT_H

#if defined(STANDARD_MODERN_C)
typedef p8  sh_sort_u8;
typedef p32 sh_sort_u32;
typedef b32 sh_sort_i32;
typedef p64 sh_sort_u64;
typedef b64 sh_sort_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
      defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  sh_sort_u8;
typedef __UINT32_TYPE__ sh_sort_u32;
typedef __INT32_TYPE__  sh_sort_i32;
typedef __UINT64_TYPE__ sh_sort_u64;
typedef __INT64_TYPE__  sh_sort_i64;
#else
#include <stdint.h>
typedef uint8_t  sh_sort_u8;
typedef uint32_t sh_sort_u32;
typedef int32_t  sh_sort_i32;
typedef uint64_t sh_sort_u64;
typedef int64_t  sh_sort_i64;
#endif

/* Exact Moonwater SORT_* flag bits */
#define SH_SORT_FOLD       1u
#define SH_SORT_DICTIONARY 2u
#define SH_SORT_PRINTABLE  4u

/* 32 bytes — byte-for-byte identical to Moonwater's sort_number */
struct sh_sort_number {
        union {
                const sh_sort_u8 *at;
                sh_sort_u64       packed;
        } integer;                  /* +0  packed word if digits <= 8, else byte ptr */
        const sh_sort_u8 *fraction; /* +8  pointer to fractional digits */
        sh_sort_i64       rank;     /* +16 signed (digits + 1), or 0 for zero */
        sh_sort_u64       places;   /* +24 trimmed fractional digit count */
};

#ifdef offset_of
#define SH_SORT_OFFSET(T, M) offset_of(T, M)
#else
#define SH_SORT_OFFSET(T, M) ((sh_sort_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct sh_sort_number) == 32, "sh_sort_number must be 32 bytes");
_Static_assert(SH_SORT_OFFSET(struct sh_sort_number, integer)  == 0,  "sh_sort_number.integer offset");
_Static_assert(SH_SORT_OFFSET(struct sh_sort_number, fraction) == 8,  "sh_sort_number.fraction offset");
_Static_assert(SH_SORT_OFFSET(struct sh_sort_number, rank)     == 16, "sh_sort_number.rank offset");
_Static_assert(SH_SORT_OFFSET(struct sh_sort_number, places)   == 24, "sh_sort_number.places offset");

sh_sort_u64 sh_sort_number_window(const sh_sort_u8 *text, sh_sort_u64 len, sh_sort_u8 *exact_out);
sh_sort_i64 sh_sort_human_order(const sh_sort_u8 *text, sh_sort_u64 len);
void        sh_sort_number_parse(const sh_sort_u8 *text, sh_sort_u64 len, struct sh_sort_number *out);
sh_sort_i64 sh_sort_compare_parsed(const struct sh_sort_number *a, const struct sh_sort_number *b);
sh_sort_u64 sh_sort_translate(sh_sort_u8 *into, const sh_sort_u8 *from, sh_sort_u64 len, sh_sort_u64 how);
sh_sort_i64 sh_sort_compare_bytes(const sh_sort_u8 *a, sh_sort_u64 la,
                                  const sh_sort_u8 *b, sh_sort_u64 lb, sh_sort_u64 how);

#endif /* MOONWATER_SH_SHELL_SORT_H */

#if defined(SH_SHELL_SORT_ASM_IMPLEMENTATION) && !defined(SH_SHELL_SORT_ASM_EMITTED)
#define SH_SHELL_SORT_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_SORT_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_SORT_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_SORT_RISCV64 1
# else
#  error "src/sh/shell_sort.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_SORT_ARM64 1
# elif defined(X64) && X64
#  define SH_SORT_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_SORT_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_SORT_SECTION   ASM_SECTION
# define SH_SORT_FUNC(n)   ASM_FUNC(n)
# define SH_SORT_END(n)    ASM_END(n)
# define SH_SORT_RET       ASM_RET
#else
# define SH_SORT_SECTION   ".text\n"
# define SH_SORT_RET       "ret\n"
# if defined(__APPLE__)
#  define SH_SORT_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_SORT_END(n)   ""
# elif defined(_WIN32)
#  define SH_SORT_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_SORT_END(n)   ""
# else
#  if defined(SH_SORT_ARM64)
#   define SH_SORT_FTYPE   "%function"
#  else
#   define SH_SORT_FTYPE   "@function"
#  endif
#  define SH_SORT_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_SORT_FTYPE "\n" #n ":\n"
#  define SH_SORT_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SH_SORT_ARM64)

__asm__(
        SH_SORT_SECTION

        /* sh_sort_number_window(x0=text, x1=len, x2=exact_out) -> x0=window */
        SH_SORT_FUNC(sh_sort_number_window)
        "cbz x0, 12f\n   cbz x1, 12f\n   add x1, x0, x1\n"
        "1:  cmp x0, x1\n   b.hs 12f\n   ldrb w3, [x0]\n   cmp w3, #32\n   b.eq 2f\n   sub w4, w3, #9\n   cmp w4, #1\n   b.hi 3f\n"
        "2:  add x0, x0, #1\n   b 1b\n"
        "3:  mov w5, #0\n   cmp w3, #45\n   b.ne 4f\n   mov w5, #1\n   add x0, x0, #1\n"
        "4:  cmp x0, x1\n   b.hs 5f\n   ldrb w3, [x0]\n   cmp w3, #48\n   b.ne 5f\n   add x0, x0, #1\n   b 4b\n"
        "5:  mov x6, x0\n"
        "6:  cmp x0, x1\n   b.hs 7f\n   ldrb w3, [x0]\n   sub w4, w3, #48\n   cmp w4, #9\n   b.hi 7f\n   add x0, x0, #1\n   b 6b\n"
        "7:  sub x7, x0, x6\n   mov x8, x0\n   mov x9, x0\n   cmp x0, x1\n   b.hs 9f\n   ldrb w3, [x0]\n   cmp w3, #46\n   b.ne 9f\n"
        "add x0, x0, #1\n   mov x8, x0\n   mov x9, x0\n"
        "8:  cmp x0, x1\n   b.hs 9f\n   ldrb w3, [x0]\n   sub w4, w3, #48\n   cmp w4, #9\n   b.hi 9f\n   add x0, x0, #1\n   cmp w3, #48\n   csel x9, x0, x9, ne\n   b 8b\n"
        "9:  sub x9, x9, x8\n   orr x10, x7, x9\n   cbz x10, 12f\n   add x10, x7, #1\n   mov x11, #0\n   mov w12, #0\n   cmp x10, #127\n   b.hs 11f\n"
        "mov x13, #0\n   mov x14, #0\n"
        "13: cmp x14, x7\n   b.hs 14f\n   cmp x13, #7\n   b.hs 15f\n   ldrb w3, [x6, x14]\n   orr x11, x3, x11, lsl #8\n   add x14, x14, #1\n   add x13, x13, #1\n   b 13b\n"
        "14: mov x14, #0\n"
        "16: cmp x14, x9\n   b.hs 15f\n   cmp x13, #7\n   b.hs 15f\n   ldrb w3, [x8, x14]\n   orr x11, x3, x11, lsl #8\n   add x14, x14, #1\n   add x13, x13, #1\n   b 16b\n"
        "15: mov x14, #7\n   sub x14, x14, x13\n   lsl x14, x14, #3\n   lsl x11, x11, x14\n   add x14, x7, x9\n   cmp x14, #7\n   cset w12, ls\n   b 17f\n"
        "11: mov x10, #127\n"
        "17: cbz x2, 18f\n   strb w12, [x2]\n"
        "18: mov x14, #0x80\n   cbz w5, 19f\n   sub x14, x14, x10\n   mvn x11, x11\n   and x11, x11, #0x00ffffffffffffff\n   orr x0, x11, x14, lsl #56\n" SH_SORT_RET
        "19: add x14, x14, x10\n   orr x0, x11, x14, lsl #56\n" SH_SORT_RET
        "12: cbz x2, 20f\n   mov w12, #1\n   strb w12, [x2]\n"
        "20: movz x0, #0x8000, lsl #48\n" SH_SORT_RET
        SH_SORT_END(sh_sort_number_window)

        /* sh_sort_human_order(x0=text, x1=len) -> x0=signed suffix rank (-10..10) */
        SH_SORT_FUNC(sh_sort_human_order)
        "cbz x0, 10f\n   cbz x1, 10f\n   add x1, x0, x1\n"
        "1:  cmp x0, x1\n   b.hs 10f\n   ldrb w2, [x0]\n   cmp w2, #32\n   b.eq 2f\n   sub w3, w2, #9\n   cmp w3, #1\n   b.hi 3f\n"
        "2:  add x0, x0, #1\n   b 1b\n"
        "3:  mov w4, #0\n   cmp w2, #45\n   b.ne 4f\n   mov w4, #1\n   add x0, x0, #1\n"
        "4:  mov w5, #0\n"
        "5:  cmp x0, x1\n   b.hs 10f\n   ldrb w2, [x0]\n   sub w3, w2, #48\n   cmp w3, #9\n   b.hi 6f\n   orr w5, w5, w3\n   add x0, x0, #1\n   b 5b\n"
        "6:  cmp w2, #46\n   b.ne 8f\n   add x0, x0, #1\n"
        "7:  cmp x0, x1\n   b.hs 10f\n   ldrb w2, [x0]\n   sub w3, w2, #48\n   cmp w3, #9\n   b.hi 8f\n   orr w5, w5, w3\n   add x0, x0, #1\n   b 7b\n"
        "8:  cbz w5, 10f\n   sub w3, w2, #69\n   cmp w3, #38\n   b.hi 10f\n"
        "mov x0, #1\n   cmp w2, #75\n   b.eq 9f\n   cmp w2, #107\n   b.eq 9f\n"
        "mov x0, #2\n   cmp w2, #77\n   b.eq 9f\n   mov x0, #3\n   cmp w2, #71\n   b.eq 9f\n   mov x0, #4\n   cmp w2, #84\n   b.eq 9f\n"
        "mov x0, #5\n   cmp w2, #80\n   b.eq 9f\n   mov x0, #6\n   cmp w2, #69\n   b.eq 9f\n   mov x0, #7\n   cmp w2, #90\n   b.eq 9f\n"
        "mov x0, #8\n   cmp w2, #89\n   b.eq 9f\n   mov x0, #9\n   cmp w2, #82\n   b.eq 9f\n   mov x0, #10\n   cmp w2, #81\n   b.ne 10f\n"
        "9:  cmp w4, #0\n   cneg x0, x0, ne\n" SH_SORT_RET
        "10: mov x0, #0\n" SH_SORT_RET
        SH_SORT_END(sh_sort_human_order)

        /* sh_sort_number_parse(x0=text, x1=len, x2=out) */
        SH_SORT_FUNC(sh_sort_number_parse)
        "cbz x2, 13f\n   cbz x0, 12f\n   cbz x1, 12f\n   add x1, x0, x1\n"
        "1:  cmp x0, x1\n   b.hs 12f\n   ldrb w3, [x0]\n   cmp w3, #32\n   b.eq 2f\n   sub w4, w3, #9\n   cmp w4, #1\n   b.hi 3f\n"
        "2:  add x0, x0, #1\n   b 1b\n"
        "3:  mov w5, #0\n   cmp w3, #45\n   b.ne 4f\n   mov w5, #1\n   add x0, x0, #1\n"
        "4:  cmp x0, x1\n   b.hs 5f\n   ldrb w3, [x0]\n   cmp w3, #48\n   b.ne 5f\n   add x0, x0, #1\n   b 4b\n"
        "5:  mov x6, x0\n"
        "6:  cmp x0, x1\n   b.hs 7f\n   ldrb w3, [x0]\n   sub w4, w3, #48\n   cmp w4, #9\n   b.hi 7f\n   add x0, x0, #1\n   b 6b\n"
        "7:  sub x7, x0, x6\n   mov x8, x0\n   mov x9, x0\n   cmp x0, x1\n   b.hs 9f\n   ldrb w3, [x0]\n   cmp w3, #46\n   b.ne 9f\n"
        "add x0, x0, #1\n   mov x8, x0\n   mov x9, x0\n"
        "8:  cmp x0, x1\n   b.hs 9f\n   ldrb w3, [x0]\n   sub w4, w3, #48\n   cmp w4, #9\n   b.hi 9f\n   add x0, x0, #1\n   cmp w3, #48\n   csel x9, x0, x9, ne\n   b 8b\n"
        "9:  sub x9, x9, x8\n   orr x10, x7, x9\n   cbz x10, 10f\n   add x10, x7, #1\n   cmp w5, #0\n   cneg x10, x10, ne\n"
        "10: cmp x7, #8\n   b.hi 11f\n   mov x11, #0\n   mov x12, #0\n"
        "14: cmp x12, x7\n   b.hs 15f\n   ldrb w3, [x6, x12]\n   orr x11, x3, x11, lsl #8\n   add x12, x12, #1\n   b 14b\n"
        "15: mov x6, x11\n"
        "11: stp x6, x8, [x2]\n   stp x10, x9, [x2, #16]\n" SH_SORT_RET
        "12: stp xzr, x0, [x2]\n   stp xzr, xzr, [x2, #16]\n"
        "13: " SH_SORT_RET
        SH_SORT_END(sh_sort_number_parse)

        /* sh_sort_compare_parsed(x0=a, x1=b) -> x0=-1/0/1 */
        SH_SORT_FUNC(sh_sort_compare_parsed)
        "cmp x0, x1\n   b.eq 10f\n   cbz x0, 11f\n   cbz x1, 12f\n"
        "ldr x2, [x0, #16]\n   ldr x3, [x1, #16]\n   cmp x2, x3\n   b.ne 13f\n   cbz x2, 10f\n"
        "cmp x2, #0\n   cneg x4, x2, lt\n   sub x4, x4, #1\n   ldr x5, [x0]\n   ldr x6, [x1]\n   cmp x4, #8\n   b.hi 2f\n"
        "cmp x5, x6\n   b.ne 14f\n   b 4f\n"
        "2:  cmp x4, #8\n   b.lo 3f\n   ldr x7, [x5]\n   ldr x8, [x6]\n   cmp x7, x8\n   b.ne 20f\n   add x5, x5, #8\n   add x6, x6, #8\n   sub x4, x4, #8\n   b 2b\n"
        "20: rev x7, x7\n   rev x8, x8\n   cmp x7, x8\n   b 14f\n"
        "3:  cbz x4, 4f\n   ldrb w7, [x5], #1\n   ldrb w8, [x6], #1\n   cmp w7, w8\n   b.ne 14f\n   sub x4, x4, #1\n   b 3b\n"
        "4:  ldr x5, [x0, #24]\n   ldr x6, [x1, #24]\n   cmp x5, x6\n   csel x4, x5, x6, lo\n   cbz x4, 7f\n   ldr x7, [x0, #8]\n   ldr x8, [x1, #8]\n"
        "6:  ldrb w9, [x7], #1\n   ldrb w10, [x8], #1\n   cmp w9, w10\n   b.ne 14f\n   subs x4, x4, #1\n   b.ne 6b\n"
        "7:  cmp x5, x6\n   b.eq 10f\n"
        "14: mov x0, #1\n   cneg x0, x0, lo\n   cmp x2, #0\n   cneg x0, x0, lt\n" SH_SORT_RET
        "13: mov x0, #1\n   cneg x0, x0, lt\n" SH_SORT_RET
        "10: mov x0, #0\n" SH_SORT_RET
        "11: mov x0, #-1\n" SH_SORT_RET
        "12: mov x0, #1\n" SH_SORT_RET
        SH_SORT_END(sh_sort_compare_parsed)

        /* sh_sort_translate(x0=into, x1=from, x2=len, x3=how) -> x0=out_len */
        SH_SORT_FUNC(sh_sort_translate)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x2, 9f\n   mov x4, x0\n   add x2, x1, x2\n"
        "tbnz x3, #1, 10f\n   tbnz x3, #2, 6f\n"
        "1:  cmp x1, x2\n   b.hs 8f\n   ldrb w6, [x1], #1\n   tbz x3, #0, 2f\n   sub w7, w6, #97\n   cmp w7, #25\n   sub w8, w6, #32\n   csel w6, w8, w6, ls\n"
        "2:  strb w6, [x0], #1\n   b 1b\n"
        "6:  cmp x1, x2\n   b.hs 8f\n   ldrb w6, [x1], #1\n   sub w7, w6, #32\n   cmp w7, #94\n   b.hi 6b\n"
        "tbz x3, #0, 7f\n   sub w7, w6, #97\n   cmp w7, #25\n   sub w8, w6, #32\n   csel w6, w8, w6, ls\n"
        "7:  strb w6, [x0], #1\n   b 6b\n"
        "10: cmp x1, x2\n   b.hs 8f\n   ldrb w6, [x1], #1\n   sub w7, w6, #48\n   cmp w7, #9\n   b.ls 12f\n"
        "orr w7, w6, #32\n   sub w7, w7, #97\n   cmp w7, #25\n   b.ls 11f\n"
        "cmp w6, #32\n   b.eq 12f\n   tbnz x3, #2, 10b\n   sub w7, w6, #9\n   cmp w7, #1\n   b.hi 10b\n   b 12f\n"
        "11: tbz x3, #0, 12f\n   and w6, w6, #0xffffffdf\n"
        "12: strb w6, [x0], #1\n   b 10b\n"
        "8:  sub x0, x0, x4\n" SH_SORT_RET
        "9:  mov x0, #0\n" SH_SORT_RET
        SH_SORT_END(sh_sort_translate)

        /* sh_sort_compare_bytes(x0=a, x1=la, x2=b, x3=lb, x4=how) -> x0=-1/0/1 */
        SH_SORT_FUNC(sh_sort_compare_bytes)
        "cbnz x0, 1f\n   mov x1, #0\n"
        "1:  cbnz x2, 2f\n   mov x3, #0\n"
        "2:  and x4, x4, #7\n   cbnz x4, 10f\n   cmp x1, x3\n   csel x5, x1, x3, lo\n"
        "29: cmp x5, #16\n   b.lo 3f\n   ldp x6, x7, [x0]\n   ldp x8, x9, [x2]\n   eor x10, x6, x8\n   eor x11, x7, x9\n   orr x10, x10, x11\n   cbnz x10, 3f\n"
        "add x0, x0, #16\n   add x2, x2, #16\n   sub x5, x5, #16\n   b 29b\n"
        "3:  cmp x5, #8\n   b.lo 4f\n   ldr x6, [x0]\n   ldr x7, [x2]\n   cmp x6, x7\n   b.ne 5f\n   add x0, x0, #8\n   add x2, x2, #8\n   sub x5, x5, #8\n   b 3b\n"
        "5:  rev x6, x6\n   rev x7, x7\n   cmp x6, x7\n   mov x0, #1\n   cneg x0, x0, lo\n" SH_SORT_RET
        "4:  cbz x5, 6f\n   ldrb w6, [x0], #1\n   ldrb w7, [x2], #1\n   cmp w6, w7\n   b.ne 23f\n   sub x5, x5, #1\n   b 4b\n"
        "6:  cmp x1, x3\n   b.eq 22f\n"
        "23: mov x0, #1\n   cneg x0, x0, lo\n" SH_SORT_RET
        "10: cmp x4, #1\n   b.ne 13f\n   cmp x1, x3\n   csel x5, x1, x3, lo\n"
        "11: cbz x5, 6b\n   ldrb w6, [x0], #1\n   ldrb w7, [x2], #1\n   sub x5, x5, #1\n   cmp w6, w7\n   b.eq 11b\n"
        "sub w8, w6, #97\n   cmp w8, #25\n   sub w9, w6, #32\n   csel w6, w9, w6, ls\n"
        "sub w8, w7, #97\n   cmp w8, #25\n   sub w9, w7, #32\n   csel w7, w9, w7, ls\n"
        "cmp w6, w7\n   b.eq 11b\n   b 23b\n"
        "13: add x1, x0, x1\n   add x3, x2, x3\n"
        "14: cmp x0, x1\n   b.hs 16f\n   ldrb w6, [x0]\n   tbz x4, #1, 140f\n"
        "sub w8, w6, #48\n   cmp w8, #9\n   b.ls 15f\n   orr w8, w6, #32\n   sub w8, w8, #97\n   cmp w8, #25\n   b.ls 15f\n"
        "cmp w6, #32\n   b.eq 15f\n   tbnz x4, #2, 142f\n   sub w8, w6, #9\n   cmp w8, #1\n   b.ls 15f\n   b 142f\n"
        "140: sub w8, w6, #32\n   cmp w8, #94\n   b.ls 15f\n"
        "142: add x0, x0, #1\n   b 14b\n"
        "15: cmp x2, x3\n   b.hs 21f\n   ldrb w7, [x2]\n   tbz x4, #1, 150f\n"
        "sub w8, w7, #48\n   cmp w8, #9\n   b.ls 17f\n   orr w8, w7, #32\n   sub w8, w8, #97\n   cmp w8, #25\n   b.ls 17f\n"
        "cmp w7, #32\n   b.eq 17f\n   tbnz x4, #2, 152f\n   sub w8, w7, #9\n   cmp w8, #1\n   b.ls 17f\n   b 152f\n"
        "150: sub w8, w7, #32\n   cmp w8, #94\n   b.ls 17f\n"
        "152: add x2, x2, #1\n   b 15b\n"
        "17: add x0, x0, #1\n   add x2, x2, #1\n   cmp w6, w7\n   b.eq 14b\n   tbz x4, #0, 18f\n"
        "sub w8, w6, #97\n   cmp w8, #25\n   sub w9, w6, #32\n   csel w6, w9, w6, ls\n"
        "sub w8, w7, #97\n   cmp w8, #25\n   sub w9, w7, #32\n   csel w7, w9, w7, ls\n"
        "cmp w6, w7\n   b.eq 14b\n"
        "18: cmp w6, w7\n   b 23b\n"
        "16: cmp x2, x3\n   b.hs 22f\n   ldrb w7, [x2]\n   tbz x4, #1, 160f\n"
        "sub w8, w7, #48\n   cmp w8, #9\n   b.ls 20f\n   orr w8, w7, #32\n   sub w8, w8, #97\n   cmp w8, #25\n   b.ls 20f\n"
        "cmp w7, #32\n   b.eq 20f\n   tbnz x4, #2, 162f\n   sub w8, w7, #9\n   cmp w8, #1\n   b.ls 20f\n   b 162f\n"
        "160: sub w8, w7, #32\n   cmp w8, #94\n   b.ls 20f\n"
        "162: add x2, x2, #1\n   b 16b\n"
        "20: mov x0, #-1\n" SH_SORT_RET
        "21: mov x0, #1\n" SH_SORT_RET
        "22: mov x0, #0\n" SH_SORT_RET
        SH_SORT_END(sh_sort_compare_bytes)
);

#elif defined(SH_SORT_X64)

#if defined(_WIN32)
#define SH_SORT_LOAD2 "mov r8, rcx\n   mov r9, rdx\n"
#define SH_SORT_LOAD3 "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
#define SH_SORT_LOAD4 "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
#define SH_SORT_LOAD5 "mov rbx, qword ptr [rsp + 48]\n   mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
#else
#define SH_SORT_LOAD2 "mov r8, rdi\n   mov r9, rsi\n"
#define SH_SORT_LOAD3 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
#define SH_SORT_LOAD4 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
#define SH_SORT_LOAD5 "mov rbx, r8\n   mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
#endif

__asm__(
        SH_SORT_SECTION
        ".intel_syntax noprefix\n"

        /* sh_sort_number_window(r8=text, r9=len, r10=exact_out) -> rax=window */
        SH_SORT_FUNC(sh_sort_number_window)
        "push rbx\n   push rbp\n   push r12\n" SH_SORT_LOAD3
        "test r8, r8\n   jz 12f\n   test r9, r9\n   jz 12f\n   add r9, r8\n"
        "1:  cmp r8, r9\n   jae 12f\n   movzx eax, byte ptr [r8]\n   cmp eax, 32\n   je 2f\n   lea ecx, [rax - 9]\n   cmp ecx, 1\n   ja 3f\n"
        "2:  inc r8\n   jmp 1b\n"
        "3:  xor ebx, ebx\n   cmp eax, 45\n   jne 4f\n   mov ebx, 1\n   inc r8\n"
        "4:  cmp r8, r9\n   jae 5f\n   cmp byte ptr [r8], 48\n   jne 5f\n   inc r8\n   jmp 4b\n"
        "5:  mov r11, r8\n"
        "6:  cmp r8, r9\n   jae 7f\n   movzx eax, byte ptr [r8]\n   sub eax, 48\n   cmp eax, 9\n   ja 7f\n   inc r8\n   jmp 6b\n"
        "7:  mov rbp, r8\n   sub rbp, r11\n   mov r12, r8\n   mov rdx, r8\n   cmp r8, r9\n   jae 9f\n   cmp byte ptr [r8], 46\n   jne 9f\n"
        "inc r8\n   mov r12, r8\n   mov rdx, r8\n"
        "8:  cmp r8, r9\n   jae 9f\n   movzx eax, byte ptr [r8]\n   lea ecx, [rax - 48]\n   cmp ecx, 9\n   ja 9f\n   inc r8\n   cmp eax, 48\n   cmovne rdx, r8\n   jmp 8b\n"
        "9:  sub rdx, r12\n   mov rax, rbp\n   or rax, rdx\n   jz 12f\n   lea r8, [rbp + 1]\n   xor r9d, r9d\n   xor ecx, ecx\n   cmp r8, 127\n   jae 11f\n"
        "xor eax, eax\n"
        "13: cmp rax, rbp\n   jae 14f\n   cmp rcx, 7\n   jae 15f\n   shl r9, 8\n   movzx ecx, byte ptr [r11 + rax]\n   or r9, rcx\n   inc rax\n   mov rcx, rax\n   jmp 13b\n"
        "14: mov r11, rax\n   xor eax, eax\n"
        "16: cmp rax, rdx\n   jae 151f\n   cmp r11, 7\n   jae 151f\n   shl r9, 8\n   movzx ecx, byte ptr [r12 + rax]\n   or r9, rcx\n   inc rax\n   inc r11\n   jmp 16b\n"
        "15: mov r11, rcx\n"
        "151: mov ecx, 7\n   sub rcx, r11\n   shl ecx, 3\n   shl r9, cl\n   lea rax, [rbp + rdx]\n   cmp rax, 7\n   setbe cl\n   jmp 17f\n"
        "11: mov r8d, 127\n   xor ecx, ecx\n"
        "17: test r10, r10\n   jz 18f\n   mov byte ptr [r10], cl\n"
        "18: mov eax, 0x80\n   test ebx, ebx\n   jz 19f\n   sub rax, r8\n   shl rax, 56\n   not r9\n   mov rcx, 0x00ffffffffffffff\n   and r9, rcx\n   or rax, r9\n   jmp 21f\n"
        "19: add rax, r8\n   shl rax, 56\n   or rax, r9\n   jmp 21f\n"
        "12: test r10, r10\n   jz 20f\n   mov byte ptr [r10], 1\n"
        "20: mov rax, 0x8000000000000000\n"
        "21: pop r12\n   pop rbp\n   pop rbx\n" SH_SORT_RET
        SH_SORT_END(sh_sort_number_window)

        /* sh_sort_human_order(r8=text, r9=len) -> rax=signed suffix rank (-10..10) */
        SH_SORT_FUNC(sh_sort_human_order)
        SH_SORT_LOAD2
        "test r8, r8\n   jz 10f\n   test r9, r9\n   jz 10f\n   add r9, r8\n"
        "1:  cmp r8, r9\n   jae 10f\n   movzx eax, byte ptr [r8]\n   cmp eax, 32\n   je 2f\n   lea ecx, [rax - 9]\n   cmp ecx, 1\n   ja 3f\n"
        "2:  inc r8\n   jmp 1b\n"
        "3:  mov r10, 1\n   cmp eax, 45\n   jne 4f\n   mov r10, -1\n   inc r8\n"
        "4:  xor r11d, r11d\n"
        "5:  cmp r8, r9\n   jae 10f\n   movzx eax, byte ptr [r8]\n   lea ecx, [rax - 48]\n   cmp ecx, 9\n   ja 6f\n   or r11d, ecx\n   inc r8\n   jmp 5b\n"
        "6:  cmp eax, 46\n   jne 8f\n   inc r8\n"
        "7:  cmp r8, r9\n   jae 10f\n   movzx eax, byte ptr [r8]\n   lea ecx, [rax - 48]\n   cmp ecx, 9\n   ja 8f\n   or r11d, ecx\n   inc r8\n   jmp 7b\n"
        "8:  test r11d, r11d\n   jz 10f\n   mov edx, 1\n   cmp eax, 75\n   je 9f\n   cmp eax, 107\n   je 9f\n"
        "mov edx, 2\n   cmp eax, 77\n   je 9f\n   mov edx, 3\n   cmp eax, 71\n   je 9f\n   mov edx, 4\n   cmp eax, 84\n   je 9f\n"
        "mov edx, 5\n   cmp eax, 80\n   je 9f\n   mov edx, 6\n   cmp eax, 69\n   je 9f\n   mov edx, 7\n   cmp eax, 90\n   je 9f\n"
        "mov edx, 8\n   cmp eax, 89\n   je 9f\n   mov edx, 9\n   cmp eax, 82\n   je 9f\n   mov edx, 10\n   cmp eax, 81\n   jne 10f\n"
        "9:  mov rax, rdx\n   imul rax, r10\n" SH_SORT_RET
        "10: xor eax, eax\n" SH_SORT_RET
        SH_SORT_END(sh_sort_human_order)

        /* sh_sort_number_parse(r8=text, r9=len, r10=out) */
        SH_SORT_FUNC(sh_sort_number_parse)
        "push rbx\n   push rbp\n" SH_SORT_LOAD3
        "test r10, r10\n   jz 13f\n   test r8, r8\n   jz 12f\n   test r9, r9\n   jz 12f\n   add r9, r8\n"
        "1:  cmp r8, r9\n   jae 12f\n   movzx eax, byte ptr [r8]\n   cmp eax, 32\n   je 2f\n   lea ecx, [rax - 9]\n   cmp ecx, 1\n   ja 3f\n"
        "2:  inc r8\n   jmp 1b\n"
        "3:  xor ebx, ebx\n   cmp eax, 45\n   jne 4f\n   mov ebx, 1\n   inc r8\n"
        "4:  cmp r8, r9\n   jae 5f\n   cmp byte ptr [r8], 48\n   jne 5f\n   inc r8\n   jmp 4b\n"
        "5:  mov r11, r8\n"
        "6:  cmp r8, r9\n   jae 7f\n   movzx eax, byte ptr [r8]\n   sub eax, 48\n   cmp eax, 9\n   ja 7f\n   inc r8\n   jmp 6b\n"
        "7:  mov rbp, r8\n   sub rbp, r11\n   mov rcx, r8\n   mov rdx, r8\n   cmp r8, r9\n   jae 9f\n   cmp byte ptr [r8], 46\n   jne 9f\n"
        "inc r8\n   mov rcx, r8\n   mov rdx, r8\n"
        "8:  cmp r8, r9\n   jae 9f\n   movzx eax, byte ptr [r8]\n   sub eax, 48\n   cmp eax, 9\n   ja 9f\n   inc r8\n   test eax, eax\n   cmovne rdx, r8\n   jmp 8b\n"
        "9:  sub rdx, rcx\n   mov [r10 + 8], rcx\n   mov [r10 + 24], rdx\n   mov rax, rbp\n   or rax, rdx\n   jz 10f\n"
        "lea rax, [rbp + 1]\n   test ebx, ebx\n   jz 10f\n   neg rax\n"
        "10: mov [r10 + 16], rax\n   cmp rbp, 8\n   ja 11f\n   xor eax, eax\n   xor edx, edx\n"
        "14: cmp rdx, rbp\n   jae 15f\n   shl rax, 8\n   movzx ecx, byte ptr [r11 + rdx]\n   or rax, rcx\n   inc rdx\n   jmp 14b\n"
        "15: mov [r10], rax\n   jmp 13f\n"
        "11: mov [r10], r11\n   jmp 13f\n"
        "12: xor eax, eax\n   mov [r10], rax\n   mov [r10 + 8], r8\n   mov [r10 + 16], rax\n   mov [r10 + 24], rax\n"
        "13: pop rbp\n   pop rbx\n" SH_SORT_RET
        SH_SORT_END(sh_sort_number_parse)

        /* sh_sort_compare_parsed(r8=a, r9=b) -> rax=-1/0/1 */
        SH_SORT_FUNC(sh_sort_compare_parsed)
        SH_SORT_LOAD2
        "cmp r8, r9\n   je 10f\n   test r8, r8\n   jz 11f\n   test r9, r9\n   jz 12f\n"
        "mov r10, [r8 + 16]\n   mov r11, [r9 + 16]\n   cmp r10, r11\n   jne 14f\n   test r10, r10\n   jz 10f\n"
        "mov rcx, r10\n   mov rax, rcx\n   sar rax, 63\n   xor rcx, rax\n   sub rcx, rax\n   dec rcx\n"
        "mov rax, [r8]\n   mov rdx, [r9]\n   cmp rcx, 8\n   ja 2f\n   cmp rax, rdx\n   jb 8f\n   ja 9f\n   jmp 4f\n"
        "2:  cmp rcx, 8\n   jb 3f\n   mov r11, [rax]\n   cmp r11, [rdx]\n   jne 20f\n   add rax, 8\n   add rdx, 8\n   sub rcx, 8\n   jmp 2b\n"
        "20: mov rdx, [rdx]\n   bswap r11\n   bswap rdx\n   cmp r11, rdx\n   jb 8f\n   jmp 9f\n"
        "3:  test rcx, rcx\n   jz 4f\n   movzx r11d, byte ptr [rax]\n   cmp r11b, byte ptr [rdx]\n   jb 8f\n   ja 9f\n   inc rax\n   inc rdx\n   dec rcx\n   jmp 3b\n"
        "4:  mov rcx, [r8 + 24]\n   mov r11, [r9 + 24]\n   cmp rcx, r11\n   cmova rcx, r11\n   test rcx, rcx\n   jz 7f\n"
        "mov rax, [r8 + 8]\n   mov rdx, [r9 + 8]\n"
        "6:  movzx r11d, byte ptr [rax]\n   cmp r11b, byte ptr [rdx]\n   jb 8f\n   ja 9f\n   inc rax\n   inc rdx\n   dec rcx\n   jnz 6b\n"
        "7:  mov rax, [r8 + 24]\n   cmp rax, [r9 + 24]\n   jb 8f\n   ja 9f\n   jmp 10f\n"
        "14: mov rax, -1\n   mov rcx, 1\n   cmovg rax, rcx\n" SH_SORT_RET
        "8:  mov rax, -1\n   test r10, r10\n   jns 13f\n   neg rax\n"
        "13: " SH_SORT_RET
        "9:  mov rax, 1\n   test r10, r10\n   jns 13b\n   neg rax\n" SH_SORT_RET
        "10: xor eax, eax\n" SH_SORT_RET
        "11: mov rax, -1\n" SH_SORT_RET
        "12: mov rax, 1\n" SH_SORT_RET
        SH_SORT_END(sh_sort_compare_parsed)

        /* sh_sort_translate(r8=into, r9=from, r10=len, r11=how) -> rax=out_len */
        SH_SORT_FUNC(sh_sort_translate)
        SH_SORT_LOAD4
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test r10, r10\n   jz 9f\n"
        "mov rax, r8\n   add r10, r9\n"
        "1:  cmp r9, r10\n   jae 8f\n   movzx ecx, byte ptr [r9]\n   inc r9\n   test r11d, 6\n   jz 4f\n"
        "test r11d, 4\n   jz 2f\n   lea edx, [rcx - 32]\n   cmp edx, 94\n   ja 1b\n"
        "2:  test r11d, 2\n   jz 4f\n   cmp ecx, 32\n   je 4f\n   lea edx, [rcx - 9]\n   cmp edx, 1\n   jbe 4f\n"
        "lea edx, [rcx - 48]\n   cmp edx, 9\n   jbe 4f\n   mov edx, ecx\n   or edx, 32\n   sub edx, 97\n   cmp edx, 25\n   ja 1b\n"
        "4:  test r11d, 1\n   jz 5f\n   lea edx, [rcx - 97]\n   cmp edx, 25\n   ja 5f\n   sub ecx, 32\n"
        "5:  mov byte ptr [r8], cl\n   inc r8\n   jmp 1b\n"
        "8:  sub r8, rax\n   mov rax, r8\n" SH_SORT_RET
        "9:  xor eax, eax\n" SH_SORT_RET
        SH_SORT_END(sh_sort_translate)

        /* sh_sort_compare_bytes(r8=a, r9=la, r10=b, r11=lb, rbx=how) -> rax=-1/0/1 */
        SH_SORT_FUNC(sh_sort_compare_bytes)
        "push rbx\n" SH_SORT_LOAD5
        "test r8, r8\n   jnz 1f\n   xor r9d, r9d\n"
        "1:  test r10, r10\n   jnz 2f\n   xor r11d, r11d\n"
        "2:  and ebx, 7\n   jnz 10f\n   mov rcx, r9\n   cmp rcx, r11\n   cmova rcx, r11\n"
        "3:  cmp rcx, 8\n   jb 4f\n   mov rax, [r8]\n   mov rdx, [r10]\n   cmp rax, rdx\n   jne 5f\n   add r8, 8\n   add r10, 8\n   sub rcx, 8\n   jmp 3b\n"
        "5:  bswap rax\n   bswap rdx\n   cmp rax, rdx\n   jb 20f\n   jmp 21f\n"
        "4:  test rcx, rcx\n   jz 6f\n   movzx eax, byte ptr [r8]\n   movzx edx, byte ptr [r10]\n   cmp eax, edx\n   jb 20f\n   ja 21f\n   inc r8\n   inc r10\n   dec rcx\n   jmp 4b\n"
        "6:  cmp r9, r11\n   jb 20f\n   ja 21f\n   jmp 22f\n"
        "10: cmp ebx, 1\n   jne 13f\n   mov rcx, r9\n   cmp rcx, r11\n   cmova rcx, r11\n"
        "11: test rcx, rcx\n   jz 6b\n   movzx eax, byte ptr [r8]\n   movzx edx, byte ptr [r10]\n   inc r8\n   inc r10\n   dec rcx\n   cmp eax, edx\n   je 11b\n"
        "sub eax, 97\n   cmp eax, 25\n   ja 111f\n   sub eax, 32\n"
        "111: add eax, 97\n   sub edx, 97\n   cmp edx, 25\n   ja 112f\n   sub edx, 32\n"
        "112: add edx, 97\n   cmp eax, edx\n   je 11b\n   jb 20f\n   jmp 21f\n"
        "13: add r9, r8\n   add r11, r10\n"
        "14: cmp r8, r9\n   jae 16f\n   movzx eax, byte ptr [r8]\n   test ebx, 4\n   jz 141f\n   lea ecx, [rax - 32]\n   cmp ecx, 94\n   ja 142f\n"
        "141: test ebx, 2\n   jz 15f\n   cmp eax, 32\n   je 15f\n   lea ecx, [rax - 9]\n   cmp ecx, 1\n   jbe 15f\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 15f\n   mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 15f\n"
        "142: inc r8\n   jmp 14b\n"
        "15: cmp r10, r11\n   jae 21f\n   movzx edx, byte ptr [r10]\n   test ebx, 4\n   jz 151f\n   lea ecx, [rdx - 32]\n   cmp ecx, 94\n   ja 152f\n"
        "151: test ebx, 2\n   jz 17f\n   cmp edx, 32\n   je 17f\n   lea ecx, [rdx - 9]\n   cmp ecx, 1\n   jbe 17f\n"
        "lea ecx, [rdx - 48]\n   cmp ecx, 9\n   jbe 17f\n   mov ecx, edx\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 17f\n"
        "152: inc r10\n   jmp 15b\n"
        "17: inc r8\n   inc r10\n   cmp eax, edx\n   je 14b\n   test ebx, 1\n   jz 18f\n"
        "lea ecx, [rax - 97]\n   cmp ecx, 25\n   ja 171f\n   sub eax, 32\n"
        "171: lea ecx, [rdx - 97]\n   cmp ecx, 25\n   ja 172f\n   sub edx, 32\n"
        "172: cmp eax, edx\n   je 14b\n"
        "18: cmp eax, edx\n   jb 20f\n   jmp 21f\n"
        "16: cmp r10, r11\n   jae 22f\n   movzx edx, byte ptr [r10]\n   test ebx, 4\n   jz 161f\n   lea ecx, [rdx - 32]\n   cmp ecx, 94\n   ja 162f\n"
        "161: test ebx, 2\n   jz 20f\n   cmp edx, 32\n   je 20f\n   lea ecx, [rdx - 9]\n   cmp ecx, 1\n   jbe 20f\n"
        "lea ecx, [rdx - 48]\n   cmp ecx, 9\n   jbe 20f\n   mov ecx, edx\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 20f\n"
        "162: inc r10\n   jmp 16b\n"
        "20: mov rax, -1\n   pop rbx\n" SH_SORT_RET
        "21: mov rax, 1\n   pop rbx\n" SH_SORT_RET
        "22: xor eax, eax\n   pop rbx\n" SH_SORT_RET
        SH_SORT_END(sh_sort_compare_bytes)

        ".att_syntax prefix\n"
);

#elif defined(SH_SORT_RISCV64)

__asm__(
        SH_SORT_SECTION

        /* sh_sort_number_window(a0=text, a1=len, a2=exact_out) -> a0=window */
        SH_SORT_FUNC(sh_sort_number_window)
        "beqz a0, 12f\n   beqz a1, 12f\n   add a1, a0, a1\n"
        "1:  bgeu a0, a1, 12f\n   lbu a3, 0(a0)\n   li t0, 32\n   beq a3, t0, 2f\n   addi t0, a3, -9\n   li t1, 1\n   bgtu t0, t1, 3f\n"
        "2:  addi a0, a0, 1\n   j 1b\n"
        "3:  li a4, 0\n   li t0, 45\n   bne a3, t0, 4f\n   li a4, 1\n   addi a0, a0, 1\n"
        "4:  bgeu a0, a1, 5f\n   lbu a3, 0(a0)\n   li t0, 48\n   bne a3, t0, 5f\n   addi a0, a0, 1\n   j 4b\n"
        "5:  mv a5, a0\n   li t1, 9\n"
        "6:  bgeu a0, a1, 7f\n   lbu a3, 0(a0)\n   addi t0, a3, -48\n   bgtu t0, t1, 7f\n   addi a0, a0, 1\n   j 6b\n"
        "7:  sub a6, a0, a5\n   mv a7, a0\n   mv t2, a0\n   bgeu a0, a1, 9f\n   lbu a3, 0(a0)\n   li t0, 46\n   bne a3, t0, 9f\n"
        "addi a0, a0, 1\n   mv a7, a0\n   mv t2, a0\n   li t3, 48\n"
        "8:  bgeu a0, a1, 9f\n   lbu a3, 0(a0)\n   addi t0, a3, -48\n   bgtu t0, t1, 9f\n   addi a0, a0, 1\n   beq a3, t3, 8b\n   mv t2, a0\n   j 8b\n"
        "9:  sub t2, t2, a7\n   or t0, a6, t2\n   beqz t0, 12f\n   addi t3, a6, 1\n   li t4, 0\n   li t5, 0\n   li t0, 127\n   bgeu t3, t0, 11f\n"
        "li t6, 0\n   li t0, 0\n   li t1, 7\n"
        "13: bgeu t0, a6, 14f\n   bgeu t6, t1, 15f\n   add a3, a5, t0\n   lbu a3, 0(a3)\n   slli t4, t4, 8\n   or t4, t4, a3\n   addi t0, t0, 1\n   addi t6, t6, 1\n   j 13b\n"
        "14: li t0, 0\n"
        "16: bgeu t0, t2, 15f\n   bgeu t6, t1, 15f\n   add a3, a7, t0\n   lbu a3, 0(a3)\n   slli t4, t4, 8\n   or t4, t4, a3\n   addi t0, t0, 1\n   addi t6, t6, 1\n   j 16b\n"
        "15: sub t0, t1, t6\n   slli t0, t0, 3\n   sll t4, t4, t0\n   add t0, a6, t2\n   sltiu t5, t0, 8\n   j 17f\n"
        "11: li t3, 127\n"
        "17: beqz a2, 18f\n   sb t5, 0(a2)\n"
        "18: li t0, 0x80\n   beqz a4, 19f\n   sub t0, t0, t3\n   slli t0, t0, 56\n   not t4, t4\n   slli t4, t4, 8\n   srli t4, t4, 8\n   or a0, t0, t4\n" SH_SORT_RET
        "19: add t0, t0, t3\n   slli t0, t0, 56\n   or a0, t0, t4\n" SH_SORT_RET
        "12: beqz a2, 20f\n   li t5, 1\n   sb t5, 0(a2)\n"
        "20: li a0, 0x80\n   slli a0, a0, 56\n" SH_SORT_RET
        SH_SORT_END(sh_sort_number_window)

        /* sh_sort_human_order(a0=text, a1=len) -> a0=signed suffix rank (-10..10) */
        SH_SORT_FUNC(sh_sort_human_order)
        "beqz a0, 10f\n   beqz a1, 10f\n   add a1, a0, a1\n"
        "1:  bgeu a0, a1, 10f\n   lbu a2, 0(a0)\n   li t0, 32\n   beq a2, t0, 2f\n   addi t0, a2, -9\n   li t1, 1\n   bgtu t0, t1, 3f\n"
        "2:  addi a0, a0, 1\n   j 1b\n"
        "3:  li a3, 1\n   li t0, 45\n   bne a2, t0, 4f\n   li a3, -1\n   addi a0, a0, 1\n"
        "4:  li a4, 0\n   li t1, 9\n"
        "5:  bgeu a0, a1, 10f\n   lbu a2, 0(a0)\n   addi t0, a2, -48\n   bgtu t0, t1, 6f\n   or a4, a4, t0\n   addi a0, a0, 1\n   j 5b\n"
        "6:  li t0, 46\n   bne a2, t0, 8f\n   addi a0, a0, 1\n"
        "7:  bgeu a0, a1, 10f\n   lbu a2, 0(a0)\n   addi t0, a2, -48\n   bgtu t0, t1, 8f\n   or a4, a4, t0\n   addi a0, a0, 1\n   j 7b\n"
        "8:  beqz a4, 10f\n   li a0, 1\n   li t0, 75\n   beq a2, t0, 9f\n   li t0, 107\n   beq a2, t0, 9f\n"
        "li a0, 2\n   li t0, 77\n   beq a2, t0, 9f\n   li a0, 3\n   li t0, 71\n   beq a2, t0, 9f\n   li a0, 4\n   li t0, 84\n   beq a2, t0, 9f\n"
        "li a0, 5\n   li t0, 80\n   beq a2, t0, 9f\n   li a0, 6\n   li t0, 69\n   beq a2, t0, 9f\n   li a0, 7\n   li t0, 90\n   beq a2, t0, 9f\n"
        "li a0, 8\n   li t0, 89\n   beq a2, t0, 9f\n   li a0, 9\n   li t0, 82\n   beq a2, t0, 9f\n   li a0, 10\n   li t0, 81\n   bne a2, t0, 10f\n"
        "9:  bgtz a3, 11f\n   neg a0, a0\n"
        "11: " SH_SORT_RET
        "10: li a0, 0\n" SH_SORT_RET
        SH_SORT_END(sh_sort_human_order)

        /* sh_sort_number_parse(a0=text, a1=len, a2=out) */
        SH_SORT_FUNC(sh_sort_number_parse)
        "beqz a2, 13f\n   beqz a0, 12f\n   beqz a1, 12f\n   add a1, a0, a1\n"
        "1:  bgeu a0, a1, 12f\n   lbu a3, 0(a0)\n   li t0, 32\n   beq a3, t0, 2f\n   addi t0, a3, -9\n   li t1, 1\n   bgtu t0, t1, 3f\n"
        "2:  addi a0, a0, 1\n   j 1b\n"
        "3:  li a4, 0\n   li t0, 45\n   bne a3, t0, 4f\n   li a4, 1\n   addi a0, a0, 1\n"
        "4:  bgeu a0, a1, 5f\n   lbu a3, 0(a0)\n   li t0, 48\n   bne a3, t0, 5f\n   addi a0, a0, 1\n   j 4b\n"
        "5:  mv a5, a0\n   li t1, 9\n"
        "6:  bgeu a0, a1, 7f\n   lbu a3, 0(a0)\n   addi t0, a3, -48\n   bgtu t0, t1, 7f\n   addi a0, a0, 1\n   j 6b\n"
        "7:  sub a6, a0, a5\n   mv a7, a0\n   mv t2, a0\n   bgeu a0, a1, 9f\n   lbu a3, 0(a0)\n   li t0, 46\n   bne a3, t0, 9f\n"
        "addi a0, a0, 1\n   mv a7, a0\n   mv t2, a0\n   li t3, 48\n"
        "8:  bgeu a0, a1, 9f\n   lbu a3, 0(a0)\n   addi t0, a3, -48\n   bgtu t0, t1, 9f\n   addi a0, a0, 1\n   beq a3, t3, 8b\n   mv t2, a0\n   j 8b\n"
        "9:  sub t2, t2, a7\n   or t3, a6, t2\n   beqz t3, 10f\n   addi t3, a6, 1\n   beqz a4, 10f\n   neg t3, t3\n"
        "10: li t0, 8\n   bgtu a6, t0, 11f\n   li t4, 0\n   li t0, 0\n"
        "14: bgeu t0, a6, 15f\n   add a3, a5, t0\n   lbu a3, 0(a3)\n   slli t4, t4, 8\n   or t4, t4, a3\n   addi t0, t0, 1\n   j 14b\n"
        "15: mv a5, t4\n"
        "11: sd a5, 0(a2)\n   sd a7, 8(a2)\n   sd t3, 16(a2)\n   sd t2, 24(a2)\n" SH_SORT_RET
        "12: sd zero, 0(a2)\n   sd a0, 8(a2)\n   sd zero, 16(a2)\n   sd zero, 24(a2)\n"
        "13: " SH_SORT_RET
        SH_SORT_END(sh_sort_number_parse)

        /* sh_sort_compare_parsed(a0=a, a1=b) -> a0=-1/0/1 */
        SH_SORT_FUNC(sh_sort_compare_parsed)
        "beq a0, a1, 10f\n   beqz a0, 11f\n   beqz a1, 12f\n"
        "ld a2, 16(a0)\n   ld a3, 16(a1)\n   blt a2, a3, 11f\n   bgt a2, a3, 12f\n   beqz a2, 10f\n"
        "mv a4, a2\n   bgez a4, 1f\n   neg a4, a4\n"
        "1:  addi a4, a4, -1\n   ld a5, 0(a0)\n   ld a6, 0(a1)\n   li t0, 8\n   bgtu a4, t0, 2f\n"
        "bltu a5, a6, 8f\n   bgtu a5, a6, 9f\n   j 4f\n"
        "2:  bltu a4, t0, 3f\n   ld t1, 0(a5)\n   ld t2, 0(a6)\n   bne t1, t2, 3f\n   addi a5, a5, 8\n   addi a6, a6, 8\n   addi a4, a4, -8\n   j 2b\n"
        "3:  beqz a4, 4f\n   lbu t1, 0(a5)\n   lbu t2, 0(a6)\n   bltu t1, t2, 8f\n   bgtu t1, t2, 9f\n   addi a5, a5, 1\n   addi a6, a6, 1\n   addi a4, a4, -1\n   j 3b\n"
        "4:  ld a5, 24(a0)\n   ld a6, 24(a1)\n   mv a4, a5\n   bleu a5, a6, 41f\n   mv a4, a6\n"
        "41: beqz a4, 7f\n   ld t3, 8(a0)\n   ld t4, 8(a1)\n   li t0, 8\n"
        "5:  bltu a4, t0, 6f\n   ld t1, 0(t3)\n   ld t2, 0(t4)\n   bne t1, t2, 6f\n   addi t3, t3, 8\n   addi t4, t4, 8\n   addi a4, a4, -8\n   j 5b\n"
        "6:  beqz a4, 7f\n   lbu t1, 0(t3)\n   lbu t2, 0(t4)\n   bltu t1, t2, 8f\n   bgtu t1, t2, 9f\n   addi t3, t3, 1\n   addi t4, t4, 1\n   addi a4, a4, -1\n   j 6b\n"
        "7:  bltu a5, a6, 8f\n   bgtu a5, a6, 9f\n   j 10f\n"
        "8:  li a0, -1\n   bgez a2, 13f\n   neg a0, a0\n"
        "13: " SH_SORT_RET
        "9:  li a0, 1\n   bgez a2, 13b\n   neg a0, a0\n" SH_SORT_RET
        "10: li a0, 0\n" SH_SORT_RET
        "11: li a0, -1\n" SH_SORT_RET
        "12: li a0, 1\n" SH_SORT_RET
        SH_SORT_END(sh_sort_compare_parsed)

        /* sh_sort_translate(a0=into, a1=from, a2=len, a3=how) -> a0=out_len */
        SH_SORT_FUNC(sh_sort_translate)
        "beqz a0, 9f\n   beqz a1, 9f\n   beqz a2, 9f\n   mv a4, a0\n   add a2, a1, a2\n   andi a5, a3, 6\n"
        "1:  bgeu a1, a2, 8f\n   lbu a6, 0(a1)\n   addi a1, a1, 1\n   beqz a5, 4f\n"
        "andi t0, a3, 4\n   beqz t0, 2f\n   addi t0, a6, -32\n   li t1, 94\n   bgtu t0, t1, 1b\n"
        "2:  andi t0, a3, 2\n   beqz t0, 4f\n   li t1, 32\n   beq a6, t1, 4f\n   addi t0, a6, -9\n   li t1, 1\n   bleu t0, t1, 4f\n"
        "addi t0, a6, -48\n   li t1, 9\n   bleu t0, t1, 4f\n   ori t0, a6, 32\n   addi t0, t0, -97\n   li t1, 25\n   bgtu t0, t1, 1b\n"
        "4:  andi t0, a3, 1\n   beqz t0, 5f\n   addi t0, a6, -97\n   li t1, 25\n   bgtu t0, t1, 5f\n   addi a6, a6, -32\n"
        "5:  sb a6, 0(a0)\n   addi a0, a0, 1\n   j 1b\n"
        "8:  sub a0, a0, a4\n" SH_SORT_RET
        "9:  li a0, 0\n" SH_SORT_RET
        SH_SORT_END(sh_sort_translate)

        /* sh_sort_compare_bytes(a0=a, a1=la, a2=b, a3=lb, a4=how) -> a0=-1/0/1 */
        SH_SORT_FUNC(sh_sort_compare_bytes)
        "bnez a0, 1f\n   li a1, 0\n"
        "1:  bnez a2, 2f\n   li a3, 0\n"
        "2:  andi a4, a4, 7\n   bnez a4, 10f\n   mv a5, a1\n   bleu a1, a3, 21f\n   mv a5, a3\n"
        "21: li t0, 8\n"
        "3:  bltu a5, t0, 4f\n   ld t1, 0(a0)\n   ld t2, 0(a2)\n   bne t1, t2, 4f\n   addi a0, a0, 8\n   addi a2, a2, 8\n   addi a5, a5, -8\n   j 3b\n"
        "4:  beqz a5, 6f\n   lbu t1, 0(a0)\n   lbu t2, 0(a2)\n   bltu t1, t2, 20f\n   bgtu t1, t2, 22f\n   addi a0, a0, 1\n   addi a2, a2, 1\n   addi a5, a5, -1\n   j 4b\n"
        "6:  bltu a1, a3, 20f\n   bgtu a1, a3, 22f\n   j 23f\n"
        "10: li t0, 1\n   bne a4, t0, 13f\n   mv a5, a1\n   bleu a1, a3, 101f\n   mv a5, a3\n"
        "101: li t3, 25\n"
        "11: beqz a5, 6b\n   lbu t1, 0(a0)\n   lbu t2, 0(a2)\n   addi a0, a0, 1\n   addi a2, a2, 1\n   addi a5, a5, -1\n   beq t1, t2, 11b\n"
        "addi t0, t1, -97\n   bgtu t0, t3, 111f\n   addi t1, t1, -32\n"
        "111: addi t0, t2, -97\n   bgtu t0, t3, 112f\n   addi t2, t2, -32\n"
        "112: beq t1, t2, 11b\n   bltu t1, t2, 20f\n   j 22f\n"
        "13: add a1, a0, a1\n   add a3, a2, a3\n"
        "14: bgeu a0, a1, 16f\n   lbu t1, 0(a0)\n   andi t0, a4, 4\n   beqz t0, 141f\n   addi t0, t1, -32\n   li t3, 94\n   bgtu t0, t3, 142f\n"
        "141: andi t0, a4, 2\n   beqz t0, 15f\n   li t3, 32\n   beq t1, t3, 15f\n   addi t0, t1, -9\n   li t3, 1\n   bleu t0, t3, 15f\n"
        "addi t0, t1, -48\n   li t3, 9\n   bleu t0, t3, 15f\n   ori t0, t1, 32\n   addi t0, t0, -97\n   li t3, 25\n   bleu t0, t3, 15f\n"
        "142: addi a0, a0, 1\n   j 14b\n"
        "15: bgeu a2, a3, 22f\n   lbu t2, 0(a2)\n   andi t0, a4, 4\n   beqz t0, 151f\n   addi t0, t2, -32\n   li t3, 94\n   bgtu t0, t3, 152f\n"
        "151: andi t0, a4, 2\n   beqz t0, 17f\n   li t3, 32\n   beq t2, t3, 17f\n   addi t0, t2, -9\n   li t3, 1\n   bleu t0, t3, 17f\n"
        "addi t0, t2, -48\n   li t3, 9\n   bleu t0, t3, 17f\n   ori t0, t2, 32\n   addi t0, t0, -97\n   li t3, 25\n   bleu t0, t3, 17f\n"
        "152: addi a2, a2, 1\n   j 15b\n"
        "17: addi a0, a0, 1\n   addi a2, a2, 1\n   beq t1, t2, 14b\n   andi t0, a4, 1\n   beqz t0, 18f\n   li t3, 25\n"
        "addi t0, t1, -97\n   bgtu t0, t3, 171f\n   addi t1, t1, -32\n"
        "171: addi t0, t2, -97\n   bgtu t0, t3, 172f\n   addi t2, t2, -32\n"
        "172: beq t1, t2, 14b\n"
        "18: bltu t1, t2, 20f\n   j 22f\n"
        "16: bgeu a2, a3, 23f\n   lbu t2, 0(a2)\n   andi t0, a4, 4\n   beqz t0, 161f\n   addi t0, t2, -32\n   li t3, 94\n   bgtu t0, t3, 162f\n"
        "161: andi t0, a4, 2\n   beqz t0, 20f\n   li t3, 32\n   beq t2, t3, 20f\n   addi t0, t2, -9\n   li t3, 1\n   bleu t0, t3, 20f\n"
        "addi t0, t2, -48\n   li t3, 9\n   bleu t0, t3, 20f\n   ori t0, t2, 32\n   addi t0, t0, -97\n   li t3, 25\n   bleu t0, t3, 20f\n"
        "162: addi a2, a2, 1\n   j 16b\n"
        "20: li a0, -1\n" SH_SORT_RET
        "22: li a0, 1\n" SH_SORT_RET
        "23: li a0, 0\n" SH_SORT_RET
        SH_SORT_END(sh_sort_compare_bytes)
);

#endif

#endif /* SH_SHELL_SORT_ASM_IMPLEMENTATION */
