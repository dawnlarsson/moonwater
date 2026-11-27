/*
        POSIX / Moonwater-compatible shell arithmetic & test loop primitives in pure assembly:
          - sh_test_digits_compare    : single-pass unsigned decimal compare (<=18 sig digits)
          - sh_arith_operator         : binary arithmetic operator precedence & byte-width classifier
          - sh_arith_power_of         : branchless wrapping 64-bit binary exponentiation
          - sh_expand_bracket_end_fast: single-mask zero-table scanner for $((...)) / ${...} / [...]

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_ARITH_TEST_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_ARITH_TEST_H
#define MOONWATER_SH_SHELL_ARITH_TEST_H

#if defined(STANDARD_MODERN_C)
typedef p8  sh_at_u8;
typedef p32 sh_at_u32;
typedef b32 sh_at_i32;
typedef p64 sh_at_u64;
typedef b64 sh_at_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
      defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  sh_at_u8;
typedef __UINT32_TYPE__ sh_at_u32;
typedef __INT32_TYPE__  sh_at_i32;
typedef __UINT64_TYPE__ sh_at_u64;
typedef __INT64_TYPE__  sh_at_i64;
#else
#include <stdint.h>
typedef uint8_t  sh_at_u8;
typedef uint32_t sh_at_u32;
typedef int32_t  sh_at_i32;
typedef uint64_t sh_at_u64;
typedef int64_t  sh_at_i64;
#endif

_Static_assert(sizeof(sh_at_u8)  == 1, "sh_at_u8 must be 1 byte");
_Static_assert(sizeof(sh_at_u32) == 4, "sh_at_u32 must be 4 bytes");
_Static_assert(sizeof(sh_at_i32) == 4, "sh_at_i32 must be 4 bytes");
_Static_assert(sizeof(sh_at_u64) == 8, "sh_at_u64 must be 8 bytes");
_Static_assert(sizeof(sh_at_i64) == 8, "sh_at_i64 must be 8 bytes");

/* Exact Moonwater arith_operator precedence levels */
#define SH_ARITH_PREC_NONE     0u
#define SH_ARITH_PREC_OR_OR    1u  /* ||        */
#define SH_ARITH_PREC_AND_AND  2u  /* &&        */
#define SH_ARITH_PREC_BIT_OR   3u  /* |         */
#define SH_ARITH_PREC_BIT_XOR  4u  /* ^         */
#define SH_ARITH_PREC_BIT_AND  5u  /* &         */
#define SH_ARITH_PREC_EQUAL    6u  /* == !=     */
#define SH_ARITH_PREC_REL      7u  /* < <= > >= */
#define SH_ARITH_PREC_SHIFT    8u  /* << >>     */
#define SH_ARITH_PREC_ADD      9u  /* + -       */
#define SH_ARITH_PREC_MUL     10u  /* * / %     */

/* Status codes for sh_expand_bracket_end_fast */
#define SH_BRACKET_EOF         0   /* NUL reached before matching close (*out_ptr = NULL) */
#define SH_BRACKET_FOUND       1   /* matching close found (*out_ptr = close ptr) */
#define SH_BRACKET_QUOTED      2   /* quote/escape/dollar/etc. hit: fall back to quoted walker */

sh_at_i32 sh_test_digits_compare(const char *left, const char *right, sh_at_i64 *cmp_out);
sh_at_u64 sh_arith_operator(const char *at, sh_at_u64 *width_out);
sh_at_i64 sh_arith_power_of(sh_at_i64 base, sh_at_i64 exponent);
sh_at_i32 sh_expand_bracket_end_fast(const char *at, sh_at_u32 open, sh_at_u32 close,
                                     sh_at_u32 dollar_brace, const char **out_ptr);

#endif /* MOONWATER_SH_SHELL_ARITH_TEST_H */

#ifdef SH_SHELL_ARITH_TEST_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_AT_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_AT_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_AT_RISCV64 1
# else
#  error "src/sh/shell_arith_test.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_AT_ARM64 1
# elif defined(X64) && X64
#  define SH_AT_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_AT_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_AT_SECTION   ASM_SECTION
# define SH_AT_FUNC(n)   ASM_FUNC(n)
# define SH_AT_END(n)    ASM_END(n)
# define SH_AT_RET       ASM_RET
#else
# define SH_AT_SECTION   ".text\n"
# define SH_AT_RET       "ret\n"
# if defined(__APPLE__)
#  define SH_AT_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_AT_END(n)   ""
# elif defined(_WIN32)
#  define SH_AT_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_AT_END(n)   ""
# else
#  if defined(SH_AT_ARM64)
#   define SH_AT_FTYPE   "%function"
#  else
#   define SH_AT_FTYPE   "@function"
#  endif
#  define SH_AT_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_AT_FTYPE "\n" #n ":\n"
#  define SH_AT_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SH_AT_ARM64)

__asm__(
        SH_AT_SECTION

        /* sh_test_digits_compare(x0=left, x1=right, x2=cmp_out) -> w0 */
        SH_AT_FUNC(sh_test_digits_compare)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x2, 9f\n   ldrb w3, [x0]\n   ldrb w4, [x1]\n"
        "sub w5, w3, #48\n   cmp w5, #9\n   b.hi 9f\n   sub w6, w4, #48\n   cmp w6, #9\n   b.hi 9f\n"
        "1:  cmp w3, #48\n   b.ne 2f\n   ldrb w5, [x0, #1]\n   sub w6, w5, #48\n   cmp w6, #9\n   b.hi 2f\n"
        "add x0, x0, #1\n   mov w3, w5\n   b 1b\n"
        "2:  cmp w4, #48\n   b.ne 3f\n   ldrb w5, [x1, #1]\n   sub w6, w5, #48\n   cmp w6, #9\n   b.hi 3f\n"
        "add x1, x1, #1\n   mov w4, w5\n   b 2b\n"
        "3:  sub w7, w3, w4\n   mov x5, #1\n"
        "4:  ldrb w3, [x0, x5]\n   ldrb w4, [x1, x5]\n   sub w6, w3, #48\n   sub w8, w4, #48\n"
        "cmp w6, #9\n   b.hi 5f\n   cmp w8, #9\n   b.hi 6f\n"
        "sub w9, w3, w4\n   cmp w7, #0\n   csel w7, w9, w7, eq\n   add x5, x5, #1\n   cmp x5, #18\n   b.ls 4b\n   b 9f\n"
        "5:  cbnz w3, 9f\n   cmp w8, #9\n   b.ls 7f\n   cbnz w4, 9f\n"
        "cmp w7, #0\n   cset x8, gt\n   csinv x8, x8, xzr, ge\n   str x8, [x2]\n   mov w0, #1\n" SH_AT_RET
        "6:  cbnz w4, 9f\n   mov x8, #1\n   b 8f\n"
        "7:  mov x0, x1\n   mov x8, #-1\n"
        "8:  add x5, x5, #1\n   cmp x5, #18\n   b.hi 9f\n   ldrb w3, [x0, x5]\n   sub w6, w3, #48\n   cmp w6, #9\n   b.ls 8b\n"
        "cbnz w3, 9f\n   str x8, [x2]\n   mov w0, #1\n" SH_AT_RET
        "9:  mov w0, #0\n" SH_AT_RET
        SH_AT_END(sh_test_digits_compare)

        /* sh_arith_operator(x0=at, x1=width_out) -> x0 */
        SH_AT_FUNC(sh_arith_operator)
        "cbz x1, 9f\n   mov x2, #1\n   str x2, [x1]\n   cbz x0, 9f\n   ldrb w3, [x0]\n   cbz w3, 9f\n   ldrb w4, [x0, #1]\n"
        "mov x5, #9\n   cmp w3, #43\n   b.eq 1f\n   cmp w3, #45\n   b.eq 1f\n"
        "mov x5, #10\n   cmp w3, #42\n   b.eq 1f\n   cmp w3, #47\n   b.eq 1f\n   cmp w3, #37\n   b.eq 1f\n"
        "cmp w3, #60\n   b.eq 3f\n   cmp w3, #62\n   b.eq 3f\n"
        "cmp w3, #61\n   b.eq 6f\n   cmp w3, #33\n   b.eq 6f\n"
        "cmp w3, #38\n   b.eq 7f\n   mov x5, #4\n   cmp w3, #94\n   b.eq 1f\n   cmp w3, #124\n   b.ne 9f\n"
        "mov x5, #3\n   cmp w4, #124\n   b.ne 1f\n   mov x0, #1\n   b 2f\n"
        "3:  cmp w4, w3\n   b.ne 4f\n   mov x2, #2\n   str x2, [x1]\n   ldrb w4, [x0, #2]\n   mov x5, #8\n   b 1f\n"
        "4:  cmp w4, #61\n   b.ne 5f\n   mov x2, #2\n   str x2, [x1]\n"
        "5:  mov x0, #7\n" SH_AT_RET
        "6:  mov x2, #2\n   str x2, [x1]\n   cmp w4, #61\n   mov x5, #6\n   csel x0, x5, xzr, eq\n" SH_AT_RET
        "7:  mov x5, #5\n   cmp w4, #38\n   b.ne 1f\n   mov x0, #2\n"
        "2:  mov x2, #2\n   str x2, [x1]\n" SH_AT_RET
        "1:  cmp w4, #61\n   csel x0, x5, xzr, ne\n" SH_AT_RET
        "9:  mov x0, #0\n" SH_AT_RET
        SH_AT_END(sh_arith_operator)

        /* sh_arith_power_of(x0=base, x1=exponent) -> x0 */
        SH_AT_FUNC(sh_arith_power_of)
        "tbnz x1, #63, 3f\n   mov x2, #1\n   cbz x1, 2f\n"
        "1:  mul x3, x2, x0\n   tst x1, #1\n   csel x2, x3, x2, ne\n   lsr x1, x1, #1\n   mul x0, x0, x0\n   cbnz x1, 1b\n"
        "2:  mov x0, x2\n" SH_AT_RET
        "3:  mov x0, #0\n" SH_AT_RET
        SH_AT_END(sh_arith_power_of)

        /* sh_expand_bracket_end_fast(x0=at, w1=open, w2=close, w3=dollar_brace, x4=out_ptr) -> w0 */
        SH_AT_FUNC(sh_expand_bracket_end_fast)
        "cbz x0, 8f\n   and w1, w1, #255\n   and w2, w2, #255\n"
        "movz x5, #0x0025\n   movk x5, #0x0400, lsl #48\n"
        "cbz w3, 9f\n   movk x5, #0x1400, lsl #16\n   movk x5, #0x4400, lsl #48\n   b 10f\n"
        "9:  sub w7, w1, #34\n   cmp w7, #62\n   b.hi 10f\n   mov x8, #1\n   lsl x8, x8, x7\n   orr x5, x5, x8\n"
        "10: mov x6, #1\n"
        "1:  ldrb w7, [x0], #1\n   cbz w7, 8f\n   cmp w7, w2\n   b.eq 4f\n"
        "sub w8, w7, #34\n   cmp w8, #62\n   b.hi 2f\n   lsr x9, x5, x8\n   tbz x9, #0, 1b\n   b 5f\n"
        "2:  cbnz w3, 1b\n   cmp w7, w1\n   b.ne 1b\n   add x6, x6, #1\n   b 1b\n"
        "4:  subs x6, x6, #1\n   b.ne 1b\n   sub x8, x0, #1\n   mov w0, #1\n   b 11f\n"
        "5:  cbz w3, 6f\n   and w8, w7, #0xfffffffd\n   cmp w8, #60\n   b.ne 7f\n   ldrb w8, [x0]\n   cmp w8, #40\n   b.ne 1b\n   b 7f\n"
        "6:  cmp w7, w1\n   b.ne 7f\n   cmp w7, #39\n   b.eq 7f\n   cmp w7, #34\n   b.eq 7f\n"
        "cmp w7, #92\n   b.eq 7f\n   cmp w7, #36\n   b.eq 7f\n   add x6, x6, #1\n   b 1b\n"
        "7:  mov x8, #0\n   mov w0, #2\n   b 11f\n"
        "8:  mov x8, #0\n   mov w0, #0\n"
        "11: cbz x4, 12f\n   str x8, [x4]\n"
        "12:\n" SH_AT_RET
        SH_AT_END(sh_expand_bracket_end_fast)
);

#elif defined(SH_AT_X64)

#if defined(_WIN32)
# define SH_AT_LOAD3  "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define SH_AT_LOAD2  "mov r8, rcx\n   mov r9, rdx\n"
# define SH_AT_LOAD2P "mov r8, rcx\n   mov rcx, rdx\n"
# define SH_AT_LOAD5  "mov r11, qword ptr [rsp + 48]\n   mov r10d, r9d\n   mov r9d, r8d\n   mov r8d, edx\n   mov rdx, rcx\n"
#else
# define SH_AT_LOAD3  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define SH_AT_LOAD2  "mov r8, rdi\n   mov r9, rsi\n"
# define SH_AT_LOAD2P "mov r8, rdi\n   mov rcx, rsi\n"
# define SH_AT_LOAD5  "mov r11, r8\n   mov r10d, ecx\n   mov r9d, edx\n   mov r8d, esi\n   mov rdx, rdi\n"
#endif

__asm__(
        SH_AT_SECTION
        ".intel_syntax noprefix\n"

        /* sh_test_digits_compare(left, right, cmp_out) -> eax */
        SH_AT_FUNC(sh_test_digits_compare)
        SH_AT_LOAD3
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test r10, r10\n   jz 9f\n"
        "movzx eax, byte ptr [r8]\n   movzx edx, byte ptr [r9]\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   ja 9f\n   lea r11d, [rdx - 48]\n   cmp r11d, 9\n   ja 9f\n"
        "1:  cmp eax, 48\n   jne 2f\n   movzx ecx, byte ptr [r8 + 1]\n   lea r11d, [rcx - 48]\n   cmp r11d, 9\n   ja 2f\n"
        "inc r8\n   mov eax, ecx\n   jmp 1b\n"
        "2:  cmp edx, 48\n   jne 3f\n   movzx ecx, byte ptr [r9 + 1]\n   lea r11d, [rcx - 48]\n   cmp r11d, 9\n   ja 3f\n"
        "inc r9\n   mov edx, ecx\n   jmp 2b\n"
        "3:  sub eax, edx\n   mov r11d, eax\n   mov ecx, 1\n"
        "4:  movzx eax, byte ptr [r8 + rcx]\n   movzx edx, byte ptr [r9 + rcx]\n"
        "sub eax, 48\n   cmp eax, 9\n   ja 5f\n   sub edx, 48\n   cmp edx, 9\n   ja 6f\n"
        "sub eax, edx\n   test r11d, r11d\n   cmove r11d, eax\n   inc rcx\n   cmp rcx, 18\n   jbe 4b\n   jmp 9f\n"
        "5:  cmp eax, -48\n   jne 9f\n   sub edx, 48\n   cmp edx, 9\n   jbe 7f\n   cmp edx, -48\n   jne 9f\n"
        "xor eax, eax\n   test r11d, r11d\n   jz 10f\n   mov rax, -1\n   mov edx, 1\n   cmovg rax, rdx\n"
        "10: mov [r10], rax\n   mov eax, 1\n" SH_AT_RET
        "6:  cmp edx, -48\n   jne 9f\n   mov edx, 1\n   jmp 8f\n"
        "7:  mov r8, r9\n   mov rdx, -1\n"
        "8:  inc rcx\n   cmp rcx, 18\n   ja 9f\n   movzx eax, byte ptr [r8 + rcx]\n   sub eax, 48\n   cmp eax, 9\n   jbe 8b\n"
        "cmp eax, -48\n   jne 9f\n   mov [r10], rdx\n   mov eax, 1\n" SH_AT_RET
        "9:  xor eax, eax\n" SH_AT_RET
        SH_AT_END(sh_test_digits_compare)

        /* sh_arith_operator(at, width_out) -> rax */
        SH_AT_FUNC(sh_arith_operator)
        SH_AT_LOAD2
        "test r9, r9\n   jz 9f\n   mov qword ptr [r9], 1\n   test r8, r8\n   jz 9f\n"
        "movzx ecx, byte ptr [r8]\n   test ecx, ecx\n   jz 9f\n   movzx edx, byte ptr [r8 + 1]\n"
        "mov r10d, 9\n   cmp ecx, 43\n   je 1f\n   cmp ecx, 45\n   je 1f\n"
        "mov r10d, 10\n   cmp ecx, 42\n   je 1f\n   cmp ecx, 47\n   je 1f\n   cmp ecx, 37\n   je 1f\n"
        "cmp ecx, 60\n   je 3f\n   cmp ecx, 62\n   je 3f\n"
        "cmp ecx, 61\n   je 6f\n   cmp ecx, 33\n   je 6f\n"
        "cmp ecx, 38\n   je 7f\n   mov r10d, 4\n   cmp ecx, 94\n   je 1f\n   cmp ecx, 124\n   jne 9f\n"
        "mov r10d, 3\n   cmp edx, 124\n   jne 1f\n   mov eax, 1\n   jmp 2f\n"
        "3:  cmp edx, ecx\n   jne 4f\n   mov qword ptr [r9], 2\n   movzx edx, byte ptr [r8 + 2]\n   mov r10d, 8\n   jmp 1f\n"
        "4:  cmp edx, 61\n   jne 5f\n   mov qword ptr [r9], 2\n"
        "5:  mov eax, 7\n" SH_AT_RET
        "6:  mov qword ptr [r9], 2\n   xor eax, eax\n   cmp edx, 61\n   mov edx, 6\n   cmove eax, edx\n" SH_AT_RET
        "7:  mov r10d, 5\n   cmp edx, 38\n   jne 1f\n   mov eax, 2\n"
        "2:  mov qword ptr [r9], 2\n" SH_AT_RET
        "1:  xor eax, eax\n   cmp edx, 61\n   cmovne eax, r10d\n" SH_AT_RET
        "9:  xor eax, eax\n" SH_AT_RET
        SH_AT_END(sh_arith_operator)

        /* sh_arith_power_of(base, exponent) -> rax */
        SH_AT_FUNC(sh_arith_power_of)
        SH_AT_LOAD2P
        "test rcx, rcx\n   js 3f\n   mov eax, 1\n   jz 2f\n"
        "1:  mov rdx, rax\n   imul rdx, r8\n   test cl, 1\n   cmovne rax, rdx\n   imul r8, r8\n   shr rcx, 1\n   jnz 1b\n"
        "2:\n" SH_AT_RET
        "3:  xor eax, eax\n" SH_AT_RET
        SH_AT_END(sh_arith_power_of)

        /* sh_expand_bracket_end_fast(at, open, close, dollar_brace, out_ptr) -> eax */
        SH_AT_FUNC(sh_expand_bracket_end_fast)
        "push rbx\n" SH_AT_LOAD5
        "test rdx, rdx\n   jz 8f\n   and r8d, 255\n   and r9d, 255\n"
        "mov rbx, 0x0400000000000025\n   test r10d, r10d\n   jz 9f\n   mov rbx, 0x4400000014000025\n   jmp 10f\n"
        "9:  lea ecx, [r8 - 34]\n   cmp ecx, 62\n   ja 10f\n   bts rbx, rcx\n"
        "10: mov ecx, 1\n"
        "1:  movzx eax, byte ptr [rdx]\n   inc rdx\n   test eax, eax\n   jz 8f\n   cmp eax, r9d\n   je 4f\n"
        "lea eax, [rax - 34]\n   cmp eax, 62\n   ja 2f\n   bt rbx, rax\n   jnc 1b\n   add eax, 34\n   jmp 5f\n"
        "2:  test r10d, r10d\n   jnz 1b\n   add eax, 34\n   cmp eax, r8d\n   jne 1b\n   inc rcx\n   jmp 1b\n"
        "4:  dec rcx\n   jnz 1b\n   dec rdx\n   mov eax, 1\n   jmp 11f\n"
        "5:  test r10d, r10d\n   jz 6f\n   and eax, 0xfd\n   cmp eax, 60\n   jne 7f\n   cmp byte ptr [rdx], 40\n   jne 1b\n   jmp 7f\n"
        "6:  cmp eax, r8d\n   jne 7f\n   cmp eax, 39\n   je 7f\n   cmp eax, 34\n   je 7f\n"
        "cmp eax, 92\n   je 7f\n   cmp eax, 36\n   je 7f\n   inc rcx\n   jmp 1b\n"
        "7:  xor edx, edx\n   mov eax, 2\n   jmp 11f\n"
        "8:  xor edx, edx\n   xor eax, eax\n"
        "11: test r11, r11\n   jz 12f\n   mov [r11], rdx\n"
        "12: pop rbx\n" SH_AT_RET
        SH_AT_END(sh_expand_bracket_end_fast)

        ".att_syntax prefix\n"
);

#elif defined(SH_AT_RISCV64)

__asm__(
        SH_AT_SECTION

        /* sh_test_digits_compare(a0=left, a1=right, a2=cmp_out) -> a0 */
        SH_AT_FUNC(sh_test_digits_compare)
        "beqz a0, 9f\n   beqz a1, 9f\n   beqz a2, 9f\n   lbu t0, 0(a0)\n   lbu t1, 0(a1)\n"
        "addi t2, t0, -48\n   sltiu t3, t2, 10\n   beqz t3, 9f\n"
        "addi t2, t1, -48\n   sltiu t3, t2, 10\n   beqz t3, 9f\n   li t6, 48\n"
        "1:  bne t0, t6, 2f\n   lbu t2, 1(a0)\n   addi t3, t2, -48\n   sltiu t4, t3, 10\n   beqz t4, 2f\n"
        "addi a0, a0, 1\n   mv t0, t2\n   j 1b\n"
        "2:  bne t1, t6, 3f\n   lbu t2, 1(a1)\n   addi t3, t2, -48\n   sltiu t4, t3, 10\n   beqz t4, 3f\n"
        "addi a1, a1, 1\n   mv t1, t2\n   j 2b\n"
        "3:  sub a5, t0, t1\n   li t5, 1\n   li a4, 18\n"
        "4:  add t2, a0, t5\n   lbu t0, 0(t2)\n   add t3, a1, t5\n   lbu t1, 0(t3)\n"
        "addi t2, t0, -48\n   sltiu t3, t2, 10\n   beqz t3, 5f\n"
        "addi t2, t1, -48\n   sltiu t3, t2, 10\n   beqz t3, 6f\n"
        "bnez a5, 10f\n   sub a5, t0, t1\n"
        "10: addi t5, t5, 1\n   bleu t5, a4, 4b\n   j 9f\n"
        "5:  bnez t0, 9f\n   addi t2, t1, -48\n   sltiu t3, t2, 10\n   bnez t3, 7f\n   bnez t1, 9f\n"
        "slt t3, zero, a5\n   slt t4, a5, zero\n   sub t3, t3, t4\n   sd t3, 0(a2)\n   li a0, 1\n" SH_AT_RET
        "6:  bnez t1, 9f\n   li a5, 1\n   j 8f\n"
        "7:  mv a0, a1\n   li a5, -1\n"
        "8:  addi t5, t5, 1\n   bgtu t5, a4, 9f\n   add t2, a0, t5\n   lbu t0, 0(t2)\n"
        "addi t2, t0, -48\n   sltiu t3, t2, 10\n   bnez t3, 8b\n"
        "bnez t0, 9f\n   sd a5, 0(a2)\n   li a0, 1\n" SH_AT_RET
        "9:  li a0, 0\n" SH_AT_RET
        SH_AT_END(sh_test_digits_compare)

        /* sh_arith_operator(a0=at, a1=width_out) -> a0 */
        SH_AT_FUNC(sh_arith_operator)
        "beqz a1, 9f\n   li t0, 1\n   sd t0, 0(a1)\n   beqz a0, 9f\n   lbu t1, 0(a0)\n   beqz t1, 9f\n   lbu t2, 1(a0)\n"
        "li t3, 61\n   li t5, 9\n   li t4, 43\n   beq t1, t4, 1f\n   li t4, 45\n   beq t1, t4, 1f\n"
        "li t5, 10\n   li t4, 42\n   beq t1, t4, 1f\n   li t4, 47\n   beq t1, t4, 1f\n   li t4, 37\n   beq t1, t4, 1f\n"
        "li t4, 60\n   beq t1, t4, 3f\n   li t4, 62\n   beq t1, t4, 3f\n"
        "beq t1, t3, 6f\n   li t4, 33\n   beq t1, t4, 6f\n"
        "li t4, 38\n   beq t1, t4, 7f\n   li t5, 4\n   li t4, 94\n   beq t1, t4, 1f\n   li t4, 124\n   bne t1, t4, 9f\n"
        "li t5, 3\n   bne t2, t4, 1f\n   li a0, 1\n   j 2f\n"
        "3:  bne t2, t1, 4f\n   li t0, 2\n   sd t0, 0(a1)\n   lbu t2, 2(a0)\n   li t5, 8\n   j 1f\n"
        "4:  bne t2, t3, 5f\n   li t0, 2\n   sd t0, 0(a1)\n"
        "5:  li a0, 7\n" SH_AT_RET
        "6:  li t0, 2\n   sd t0, 0(a1)\n   bne t2, t3, 9f\n   li a0, 6\n" SH_AT_RET
        "7:  li t5, 5\n   bne t2, t4, 1f\n   li a0, 2\n"
        "2:  li t0, 2\n   sd t0, 0(a1)\n" SH_AT_RET
        "1:  beq t2, t3, 9f\n   mv a0, t5\n" SH_AT_RET
        "9:  li a0, 0\n" SH_AT_RET
        SH_AT_END(sh_arith_operator)

        /* sh_arith_power_of(a0=base, a1=exponent) -> a0 */
        SH_AT_FUNC(sh_arith_power_of)
        "bltz a1, 3f\n   li t0, 1\n   beqz a1, 2f\n"
        "1:  andi t1, a1, 1\n   beqz t1, 4f\n   mul t0, t0, a0\n"
        "4:  srli a1, a1, 1\n   mul a0, a0, a0\n   bnez a1, 1b\n"
        "2:  mv a0, t0\n" SH_AT_RET
        "3:  li a0, 0\n" SH_AT_RET
        SH_AT_END(sh_arith_power_of)

        /* sh_expand_bracket_end_fast(a0=at, a1=open, a2=close, a3=dollar_brace, a4=out_ptr) -> a0 */
        SH_AT_FUNC(sh_expand_bracket_end_fast)
        "beqz a0, 8f\n   andi a1, a1, 255\n   andi a2, a2, 255\n"
        "li t0, 1\n   slli t0, t0, 58\n   ori t0, t0, 0x25\n"
        "beqz a3, 9f\n   li t4, 5\n   slli t4, t4, 26\n   or t0, t0, t4\n   li t4, 1\n   slli t4, t4, 62\n   or t0, t0, t4\n   j 10f\n"
        "9:  addi t4, a1, -34\n   li t5, 62\n   bgtu t4, t5, 10f\n   li t5, 1\n   sll t4, t5, t4\n   or t0, t0, t4\n"
        "10: li t2, 1\n   li t6, 62\n"
        "1:  lbu t3, 0(a0)\n   addi a0, a0, 1\n   beqz t3, 8f\n   beq t3, a2, 4f\n"
        "addi t4, t3, -34\n   bgtu t4, t6, 2f\n   srl t5, t0, t4\n   andi t5, t5, 1\n   beqz t5, 1b\n   j 5f\n"
        "2:  bnez a3, 1b\n   bne t3, a1, 1b\n   addi t2, t2, 1\n   j 1b\n"
        "4:  addi t2, t2, -1\n   bnez t2, 1b\n   addi t4, a0, -1\n   li a0, 1\n   j 11f\n"
        "5:  beqz a3, 6f\n   andi t4, t3, 0xfd\n   li t5, 60\n   bne t4, t5, 7f\n   lbu t4, 0(a0)\n   li t5, 40\n   bne t4, t5, 1b\n   j 7f\n"
        "6:  bne t3, a1, 7f\n   li t4, 39\n   beq t3, t4, 7f\n   li t4, 34\n   beq t3, t4, 7f\n"
        "li t4, 92\n   beq t3, t4, 7f\n   li t4, 36\n   beq t3, t4, 7f\n   addi t2, t2, 1\n   j 1b\n"
        "7:  li t4, 0\n   li a0, 2\n   j 11f\n"
        "8:  li t4, 0\n   li a0, 0\n"
        "11: beqz a4, 12f\n   sd t4, 0(a4)\n"
        "12:\n" SH_AT_RET
        SH_AT_END(sh_expand_bracket_end_fast)
);

#endif /* arch */

#endif /* SH_SHELL_ARITH_TEST_ASM_IMPLEMENTATION */
