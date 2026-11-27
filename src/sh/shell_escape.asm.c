/*
        POSIX / Moonwater-compatible shell escape & ANSI SGR primitives in pure assembly:
          - sh_unescape_c    : parameterized C-escape unescaper (awk, sed, ptx, $'...' modes)
          - sh_unescape_tr   : GNU tr unquoter with per-byte escaped mask & warning bailout
          - sh_ansi_sgr_skip : ANSI CSI SGR (\x1b[...m) escape-sequence length scanner

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_ESCAPE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_ESCAPE_H
#define MOONWATER_SH_SHELL_ESCAPE_H

#if defined(STANDARD_MODERN_C)
typedef p32 sh_esc_u32;
typedef b32 sh_esc_i32;
typedef p64 sh_esc_u64;
typedef b64 sh_esc_i64;
#elif defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT32_TYPE__ sh_esc_u32;
typedef __INT32_TYPE__  sh_esc_i32;
typedef __UINT64_TYPE__ sh_esc_u64;
typedef __INT64_TYPE__  sh_esc_i64;
#else
#include <stdint.h>
typedef uint32_t sh_esc_u32;
typedef int32_t  sh_esc_i32;
typedef uint64_t sh_esc_u64;
typedef int64_t  sh_esc_i64;
#endif

/* Orthogonal escape-dialect flags for sh_unescape_c() */
#define SH_ESC_BS           (1u << 0)   /* \b -> 0x08 */
#define SH_ESC_E_LOWER      (1u << 1)   /* \e -> 0x1b */
#define SH_ESC_E_UPPER      (1u << 2)   /* \E -> 0x1b */
#define SH_ESC_OCTAL        (1u << 3)   /* \0..\7 up to 3 octal digits (& 0xff) */
#define SH_ESC_OCTAL_ZERO   (1u << 4)   /* \0 + 0..3 octal digits (& 0xff) */
#define SH_ESC_OCTAL_O      (1u << 5)   /* \o + 1..3 octal digits (& 0xff) */
#define SH_ESC_DEC_D        (1u << 6)   /* \d + 1..3 decimal digits (& 0xff) */
#define SH_ESC_HEX2         (1u << 7)   /* \x + 1..2 hex digits (& 0xff) */
#define SH_ESC_HEX3         (1u << 8)   /* \x + 1..3 hex digits (& 0xff) */
#define SH_ESC_CTRL_XOR     (1u << 9)   /* \cX -> toupper(X) ^ 0x40 */
#define SH_ESC_CTRL_MASK    (1u << 10)  /* \cX -> X == '?' ? 127 : (X & 31) */
#define SH_ESC_CTRL_DBLBS   (1u << 11)  /* \c\\ consumes 4 bytes */
#define SH_ESC_STOP_C       (1u << 12)  /* \c stops unescaping */
#define SH_ESC_KEEP_UNKNOWN (1u << 13)  /* unknown \z emits "\z" instead of "z" */
#define SH_ESC_KEEP_QUOTES  (1u << 14)  /* with KEEP_UNKNOWN: \\ \' \" drop '\' */
#define SH_ESC_KEEP_QMARK   (1u << 15)  /* with KEEP_UNKNOWN: \? drops '\' */
#define SH_ESC_DROP_TRAIL   (1u << 16)  /* trailing '\' at EOF dropped instead of kept */
#define SH_ESC_STOP_NUL     (1u << 17)  /* produced 0x00 stops output */

/* Preset dialect combinations */
#define SH_ESC_MODE_AWK \
        (SH_ESC_BS | SH_ESC_OCTAL | SH_ESC_HEX2)
#define SH_ESC_MODE_SED \
        (SH_ESC_OCTAL_O | SH_ESC_DEC_D | SH_ESC_HEX2 | SH_ESC_CTRL_XOR | SH_ESC_CTRL_DBLBS)
#define SH_ESC_MODE_PTX \
        (SH_ESC_BS | SH_ESC_OCTAL_ZERO | SH_ESC_HEX3 | SH_ESC_STOP_C | SH_ESC_KEEP_UNKNOWN | SH_ESC_DROP_TRAIL)
#define SH_ESC_MODE_ANSI_BASH \
        (SH_ESC_BS | SH_ESC_E_LOWER | SH_ESC_E_UPPER | SH_ESC_OCTAL | SH_ESC_HEX2 | \
         SH_ESC_CTRL_MASK | SH_ESC_KEEP_UNKNOWN | SH_ESC_KEEP_QUOTES | SH_ESC_KEEP_QMARK | SH_ESC_STOP_NUL)
#define SH_ESC_MODE_ANSI_DASH \
        (SH_ESC_BS | SH_ESC_E_LOWER | SH_ESC_OCTAL | SH_ESC_CTRL_XOR | \
         SH_ESC_KEEP_UNKNOWN | SH_ESC_KEEP_QUOTES | SH_ESC_STOP_NUL)

sh_esc_u64 sh_unescape_c(const char *src, sh_esc_u64 len, char *dst, sh_esc_u64 cap, sh_esc_u32 flags);
sh_esc_i64 sh_unescape_tr(const char *src, sh_esc_u64 len, char *dst, char *esc_out, sh_esc_u64 cap);
sh_esc_u64 sh_ansi_sgr_skip(const char *src, sh_esc_u64 len);

#endif /* MOONWATER_SH_SHELL_ESCAPE_H */

#ifdef SH_SHELL_ESCAPE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_ESC_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_ESC_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_ESC_RISCV64 1
# else
#  error "src/sh/shell_escape.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_ESC_ARM64 1
# elif defined(X64) && X64
#  define SH_ESC_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_ESC_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_ESC_SECTION   ASM_SECTION
# define SH_ESC_FUNC(n)   ASM_FUNC(n)
# define SH_ESC_END(n)    ASM_END(n)
# define SH_ESC_RET       ASM_RET
#else
# define SH_ESC_SECTION   ".text\n"
# define SH_ESC_RET       "ret\n"
# if defined(__APPLE__)
#  define SH_ESC_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_ESC_END(n)   ""
# elif defined(_WIN32)
#  define SH_ESC_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_ESC_END(n)   ""
# else
#  if defined(SH_ESC_ARM64)
#   define SH_ESC_FTYPE   "%function"
#  else
#   define SH_ESC_FTYPE   "@function"
#  endif
#  define SH_ESC_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_ESC_FTYPE "\n" #n ":\n"
#  define SH_ESC_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SH_ESC_ARM64)

__asm__(
        SH_ESC_SECTION

        /* sh_unescape_c(x0=src, x1=len, x2=dst, x3=cap, w4=flags) -> x0=out */
        SH_ESC_FUNC(sh_unescape_c)
        "cbz x0, 29f\n   cbz x1, 29f\n   cbz x2, 29f\n   cbz x3, 29f\n"
        "add x1, x0, x1\n   mov x5, x2\n   add x3, x2, x3\n"
        "orr x14, xzr, #0x0101010101010101\n   orr x15, xzr, #0x8080808080808080\n   mov w16, #92\n   mul x16, x14, x16\n"
        "1:  sub x12, x1, x0\n   sub x13, x3, x2\n   cmp x12, x13\n   csel x12, x12, x13, lo\n   cbz x12, 28f\n"
        "39: cmp x12, #8\n   b.lo 40f\n   ldr x7, [x0]\n   eor x8, x7, x16\n   sub x9, x8, x14\n   bic x9, x9, x8\n"
        "sub x10, x7, x14\n   bic x10, x10, x7\n   orr x9, x9, x10\n   tst x9, x15\n   b.ne 40f\n"
        "str x7, [x2], #8\n   add x0, x0, #8\n   sub x12, x12, #8\n   b 39b\n"
        "40: cbz x12, 28f\n   ldrb w7, [x0], #1\n   sub x12, x12, #1\n   cmp w7, #92\n   b.eq 2f\n"
        "cbnz w7, 30f\n   tbnz w4, #17, 28f\n"
        "30: strb w7, [x2], #1\n   b 40b\n"
        "2:  cmp x0, x1\n   b.lo 3f\n   tbnz w4, #16, 28f\n   mov w8, #92\n   strb w8, [x2], #1\n   b 28f\n"
        "3:  ldrb w7, [x0], #1\n"
        "cmp w7, #110\n   b.ne 4f\n   mov w8, #10\n   b 25f\n"
        "4:  cmp w7, #116\n   b.ne 5f\n   mov w8, #9\n   b 25f\n"
        "5:  cmp w7, #114\n   b.ne 6f\n   mov w8, #13\n   b 25f\n"
        "6:  cmp w7, #97\n   b.ne 7f\n   mov w8, #7\n   b 25f\n"
        "7:  cmp w7, #102\n   b.ne 8f\n   mov w8, #12\n   b 25f\n"
        "8:  cmp w7, #118\n   b.ne 9f\n   mov w8, #11\n   b 25f\n"
        "9:  cmp w7, #98\n   b.ne 10f\n   tbz w4, #0, 22f\n   mov w8, #8\n   b 25f\n"
        "10: cmp w7, #101\n   b.ne 11f\n   tbz w4, #1, 22f\n   mov w8, #27\n   b 25f\n"
        "11: cmp w7, #69\n   b.ne 12f\n   tbz w4, #2, 22f\n   mov w8, #27\n   b 25f\n"
        "12: sub w12, w7, #48\n   cmp w12, #7\n   b.hi 13f\n   tbz w4, #3, 31f\n"
        "mov w9, #8\n   mov w10, #2\n   mov w8, w12\n   mov w11, #1\n   b 19f\n"
        "31: cbnz w12, 22f\n   tbz w4, #4, 22f\n   mov w9, #8\n   mov w10, #3\n   mov w8, #0\n   mov w11, #1\n   b 19f\n"
        "13: cmp w7, #120\n   b.ne 14f\n   tbnz w4, #8, 32f\n   tbz w4, #7, 22f\n"
        "mov w9, #16\n   mov w10, #2\n   mov w8, #0\n   mov w11, #0\n   b 19f\n"
        "32: mov w9, #16\n   mov w10, #3\n   mov w8, #0\n   mov w11, #0\n   b 19f\n"
        "14: cmp w7, #111\n   b.ne 15f\n   tbz w4, #5, 22f\n   mov w9, #8\n   mov w10, #3\n   mov w8, #0\n   mov w11, #0\n   b 19f\n"
        "15: cmp w7, #100\n   b.ne 16f\n   tbz w4, #6, 22f\n   mov w9, #10\n   mov w10, #3\n   mov w8, #0\n   mov w11, #0\n   b 19f\n"
        "16: cmp w7, #99\n   b.ne 22f\n   tbnz w4, #12, 28f\n   tst w4, #0x600\n   b.eq 22f\n   cmp x0, x1\n   b.hs 22f\n"
        "ldrb w8, [x0], #1\n   tbz w4, #11, 17f\n   cmp w8, #92\n   b.ne 17f\n"
        "cmp x0, x1\n   b.hs 17f\n   ldrb w12, [x0]\n   cmp w12, #92\n   b.ne 17f\n   add x0, x0, #1\n"
        "17: tbz w4, #9, 18f\n   sub w12, w8, #97\n   cmp w12, #25\n   b.hi 33f\n   sub w8, w8, #32\n"
        "33: eor w8, w8, #64\n   b 25f\n"
        "18: cmp w8, #63\n   b.ne 34f\n   mov w8, #127\n   b 25f\n"
        "34: and w8, w8, #31\n   b 25f\n"
        "19: cbz w10, 21f\n   cmp x0, x1\n   b.hs 21f\n   ldrb w12, [x0]\n   sub w13, w12, #48\n"
        "cmp w13, #9\n   b.ls 20f\n   cmp w9, #16\n   b.ne 21f\n   orr w13, w12, #32\n   sub w13, w13, #97\n"
        "cmp w13, #5\n   b.hi 21f\n   add w13, w13, #10\n"
        "20: cmp w13, w9\n   b.hs 21f\n   madd w8, w8, w9, w13\n   add x0, x0, #1\n   add w11, w11, #1\n   sub w10, w10, #1\n   b 19b\n"
        "21: cbz w11, 22f\n   and w8, w8, #255\n   b 25f\n"
        "22: tbz w4, #13, 24f\n   tbz w4, #14, 23f\n   cmp w7, #92\n   b.eq 24f\n   cmp w7, #39\n   b.eq 24f\n   cmp w7, #34\n   b.eq 24f\n"
        "23: tbz w4, #15, 35f\n   cmp w7, #63\n   b.eq 24f\n"
        "35: mov w12, #92\n   strb w12, [x2], #1\n   cmp x2, x3\n   b.hs 28f\n"
        "24: mov w8, w7\n"
        "25: cbnz w8, 26f\n   tbnz w4, #17, 28f\n"
        "26: strb w8, [x2], #1\n   b 1b\n"
        "28: sub x0, x2, x5\n" SH_ESC_RET
        "29: mov x0, #0\n" SH_ESC_RET
        SH_ESC_END(sh_unescape_c)

        /* sh_unescape_tr(x0=src, x1=len, x2=dst, x3=esc_out, x4=cap) -> x0=out or -1 on warning */
        SH_ESC_FUNC(sh_unescape_tr)
        "cbz x0, 14f\n   cbz x1, 14f\n   cbz x2, 14f\n   cbz x3, 14f\n   cbz x4, 14f\n"
        "add x1, x0, x1\n   mov x5, x2\n   add x4, x2, x4\n"
        "orr x14, xzr, #0x0101010101010101\n   orr x15, xzr, #0x8080808080808080\n   mov w16, #92\n   mul x16, x14, x16\n"
        "1:  sub x12, x1, x0\n   sub x13, x4, x2\n   cmp x12, x13\n   csel x12, x12, x13, lo\n   cbz x12, 12f\n"
        "16: cmp x12, #8\n   b.lo 17f\n   ldr x7, [x0]\n   eor x8, x7, x16\n   sub x9, x8, x14\n   bic x9, x9, x8\n"
        "tst x9, x15\n   b.ne 17f\n   str x7, [x2], #8\n   str xzr, [x3], #8\n   add x0, x0, #8\n   sub x12, x12, #8\n   b 16b\n"
        "17: cbz x12, 12f\n   ldrb w7, [x0], #1\n   sub x12, x12, #1\n   cmp w7, #92\n   b.eq 2f\n"
        "strb w7, [x2], #1\n   strb wzr, [x3], #1\n   b 17b\n"
        "2:  cmp x0, x1\n   b.hs 13f\n   ldrb w7, [x0], #1\n   sub w8, w7, #48\n   cmp w8, #7\n   b.hi 4f\n"
        "cmp x0, x1\n   b.hs 11f\n   ldrb w9, [x0]\n   sub w10, w9, #48\n   cmp w10, #7\n   b.hi 11f\n"
        "add w8, w10, w8, lsl #3\n   add x0, x0, #1\n   cmp x0, x1\n   b.hs 11f\n   ldrb w9, [x0]\n"
        "sub w10, w9, #48\n   cmp w10, #7\n   b.hi 11f\n   add w10, w10, w8, lsl #3\n   cmp w10, #255\n   b.hi 13f\n"
        "mov w8, w10\n   add x0, x0, #1\n   b 11f\n"
        "4:  mov w8, w7\n   cmp w7, #110\n   mov w9, #10\n   csel w8, w9, w8, eq\n"
        "cmp w7, #116\n   mov w9, #9\n   csel w8, w9, w8, eq\n   cmp w7, #114\n   mov w9, #13\n   csel w8, w9, w8, eq\n"
        "cmp w7, #97\n   mov w9, #7\n   csel w8, w9, w8, eq\n   cmp w7, #98\n   mov w9, #8\n   csel w8, w9, w8, eq\n"
        "cmp w7, #102\n   mov w9, #12\n   csel w8, w9, w8, eq\n   cmp w7, #118\n   mov w9, #11\n   csel w8, w9, w8, eq\n"
        "11: mov w9, #1\n   strb w8, [x2], #1\n   strb w9, [x3], #1\n   b 1b\n"
        "12: sub x0, x2, x5\n" SH_ESC_RET
        "13: mov x0, #-1\n" SH_ESC_RET
        "14: mov x0, #0\n" SH_ESC_RET
        SH_ESC_END(sh_unescape_tr)

        /* sh_ansi_sgr_skip(x0=src, x1=len) -> x0=total SGR length (>= 3) or 0 */
        SH_ESC_FUNC(sh_ansi_sgr_skip)
        "cbz x0, 3f\n   cmp x1, #3\n   b.lo 3f\n   ldrh w2, [x0]\n   mov w3, #0x5b1b\n   cmp w2, w3\n   b.ne 3f\n"
        "add x1, x0, x1\n   add x2, x0, #2\n"
        "1:  cmp x2, x1\n   b.hs 3f\n   ldrb w3, [x2], #1\n   sub w4, w3, #32\n   cmp w4, #31\n   b.ls 1b\n"
        "cmp w3, #109\n   b.ne 3f\n   sub x0, x2, x0\n" SH_ESC_RET
        "3:  mov x0, #0\n" SH_ESC_RET
        SH_ESC_END(sh_ansi_sgr_skip)
);

#elif defined(SH_ESC_X64)

#if defined(_WIN32)
#define SH_ESC_LOAD5  "mov r12, rcx\n   mov r13, rdx\n   mov r10, r8\n   mov r11, r9\n   mov ebx, dword ptr [rsp + 80]\n"
#define SH_ESC_TR5    "mov r12, rcx\n   mov r13, rdx\n   mov r10, r8\n   mov r11, r9\n   mov rbx, qword ptr [rsp + 64]\n"
#define SH_ESC_LOAD2  "mov r8, rcx\n   mov r9, rdx\n"
#else
#define SH_ESC_LOAD5  "mov r12, rdi\n   mov r13, rsi\n   mov r10, rdx\n   mov r11, rcx\n   mov ebx, r8d\n"
#define SH_ESC_TR5    "mov r12, rdi\n   mov r13, rsi\n   mov r10, rdx\n   mov r11, rcx\n   mov rbx, r8\n"
#define SH_ESC_LOAD2  "mov r8, rdi\n   mov r9, rsi\n"
#endif

__asm__(
        SH_ESC_SECTION
        ".intel_syntax noprefix\n"

        /* sh_unescape_c(src, len, dst, cap, flags) -> out */
        SH_ESC_FUNC(sh_unescape_c)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n" SH_ESC_LOAD5
        "test r12, r12\n   jz 29f\n   test r13, r13\n   jz 29f\n   test r10, r10\n   jz 29f\n   test r11, r11\n   jz 29f\n"
        "xor r8d, r8d\n   xor r9d, r9d\n"
        "1:  mov rax, r13\n   sub rax, r8\n   mov rcx, r11\n   sub rcx, r9\n   cmp rax, rcx\n   cmova rax, rcx\n   test rax, rax\n   jz 28f\n"
        "39: cmp rax, 8\n   jb 40f\n   mov rdx, [r12 + r8]\n   mov r14, 0x5c5c5c5c5c5c5c5c\n   xor r14, rdx\n"
        "mov rcx, 0x0101010101010101\n   mov rbp, r14\n   sub rbp, rcx\n   not r14\n   and rbp, r14\n"
        "mov r14, rdx\n   sub r14, rcx\n   mov rcx, rdx\n   not rcx\n   and r14, rcx\n   or rbp, r14\n"
        "mov rcx, 0x8080808080808080\n   test rbp, rcx\n   jnz 40f\n"
        "mov [r10 + r9], rdx\n   add r8, 8\n   add r9, 8\n   sub rax, 8\n   jmp 39b\n"
        "40: test rax, rax\n   jz 28f\n   movzx ebp, byte ptr [r12 + r8]\n   inc r8\n   dec rax\n"
        "cmp ebp, 92\n   je 2f\n   test ebp, ebp\n   jnz 30f\n   bt ebx, 17\n   jc 28f\n"
        "30: mov byte ptr [r10 + r9], bpl\n   inc r9\n   jmp 40b\n"
        "2:  cmp r8, r13\n   jb 3f\n   bt ebx, 16\n   jc 28f\n   mov byte ptr [r10 + r9], 92\n   inc r9\n   jmp 28f\n"
        "3:  movzx ebp, byte ptr [r12 + r8]\n   inc r8\n"
        "cmp ebp, 110\n   jne 4f\n   mov eax, 10\n   jmp 25f\n"
        "4:  cmp ebp, 116\n   jne 5f\n   mov eax, 9\n   jmp 25f\n"
        "5:  cmp ebp, 114\n   jne 6f\n   mov eax, 13\n   jmp 25f\n"
        "6:  cmp ebp, 97\n   jne 7f\n   mov eax, 7\n   jmp 25f\n"
        "7:  cmp ebp, 102\n   jne 8f\n   mov eax, 12\n   jmp 25f\n"
        "8:  cmp ebp, 118\n   jne 9f\n   mov eax, 11\n   jmp 25f\n"
        "9:  cmp ebp, 98\n   jne 10f\n   bt ebx, 0\n   jnc 22f\n   mov eax, 8\n   jmp 25f\n"
        "10: cmp ebp, 101\n   jne 11f\n   bt ebx, 1\n   jnc 22f\n   mov eax, 27\n   jmp 25f\n"
        "11: cmp ebp, 69\n   jne 12f\n   bt ebx, 2\n   jnc 22f\n   mov eax, 27\n   jmp 25f\n"
        "12: lea eax, [rbp - 48]\n   cmp eax, 7\n   ja 13f\n   bt ebx, 3\n   jnc 31f\n"
        "mov ecx, 8\n   mov edx, 2\n   mov r14d, 1\n   jmp 19f\n"
        "31: test eax, eax\n   jnz 22f\n   bt ebx, 4\n   jnc 22f\n   mov ecx, 8\n   mov edx, 3\n   mov r14d, 1\n   jmp 19f\n"
        "13: cmp ebp, 120\n   jne 14f\n   bt ebx, 8\n   jc 32f\n   bt ebx, 7\n   jnc 22f\n"
        "mov ecx, 16\n   mov edx, 2\n   xor eax, eax\n   xor r14d, r14d\n   jmp 19f\n"
        "32: mov ecx, 16\n   mov edx, 3\n   xor eax, eax\n   xor r14d, r14d\n   jmp 19f\n"
        "14: cmp ebp, 111\n   jne 15f\n   bt ebx, 5\n   jnc 22f\n   mov ecx, 8\n   mov edx, 3\n   xor eax, eax\n   xor r14d, r14d\n   jmp 19f\n"
        "15: cmp ebp, 100\n   jne 16f\n   bt ebx, 6\n   jnc 22f\n   mov ecx, 10\n   mov edx, 3\n   xor eax, eax\n   xor r14d, r14d\n   jmp 19f\n"
        "16: cmp ebp, 99\n   jne 22f\n   bt ebx, 12\n   jc 28f\n   test ebx, 0x600\n   jz 22f\n   cmp r8, r13\n   jae 22f\n"
        "movzx eax, byte ptr [r12 + r8]\n   inc r8\n   bt ebx, 11\n   jnc 17f\n   cmp eax, 92\n   jne 17f\n"
        "cmp r8, r13\n   jae 17f\n   cmp byte ptr [r12 + r8], 92\n   jne 17f\n   inc r8\n"
        "17: bt ebx, 9\n   jnc 18f\n   lea ecx, [rax - 97]\n   cmp ecx, 25\n   ja 33f\n   sub eax, 32\n"
        "33: xor eax, 64\n   jmp 25f\n"
        "18: cmp eax, 63\n   jne 34f\n   mov eax, 127\n   jmp 25f\n"
        "34: and eax, 31\n   jmp 25f\n"
        "19: test edx, edx\n   jz 21f\n   cmp r8, r13\n   jae 21f\n   push rdx\n   movzx edx, byte ptr [r12 + r8]\n"
        "lea ebp, [rdx - 48]\n   cmp ebp, 9\n   jbe 20f\n   cmp ecx, 16\n   jne 36f\n   or edx, 32\n   sub edx, 97\n"
        "cmp edx, 5\n   ja 36f\n   lea ebp, [rdx + 10]\n"
        "20: cmp ebp, ecx\n   jae 36f\n   imul eax, ecx\n   add eax, ebp\n   pop rdx\n   inc r8\n   inc r14d\n   dec edx\n   jmp 19b\n"
        "36: pop rdx\n"
        "21: test r14d, r14d\n   jz 37f\n   and eax, 255\n   jmp 25f\n"
        "37: movzx ebp, byte ptr [r12 + r8 - 1]\n"
        "22: bt ebx, 13\n   jnc 24f\n   bt ebx, 14\n   jnc 23f\n   cmp ebp, 92\n   je 24f\n   cmp ebp, 39\n   je 24f\n   cmp ebp, 34\n   je 24f\n"
        "23: bt ebx, 15\n   jnc 35f\n   cmp ebp, 63\n   je 24f\n"
        "35: mov byte ptr [r10 + r9], 92\n   inc r9\n   cmp r9, r11\n   jae 28f\n"
        "24: mov eax, ebp\n"
        "25: test eax, eax\n   jnz 26f\n   bt ebx, 17\n   jc 28f\n"
        "26: mov byte ptr [r10 + r9], al\n   inc r9\n   jmp 1b\n"
        "28: mov rax, r9\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" SH_ESC_RET
        "29: xor eax, eax\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" SH_ESC_RET
        SH_ESC_END(sh_unescape_c)

        /* sh_unescape_tr(src, len, dst, esc_out, cap) -> out or -1 */
        SH_ESC_FUNC(sh_unescape_tr)
        "push rbx\n   push r12\n   push r13\n" SH_ESC_TR5
        "test r12, r12\n   jz 14f\n   test r13, r13\n   jz 14f\n   test r10, r10\n   jz 14f\n   test r11, r11\n   jz 14f\n   test rbx, rbx\n   jz 14f\n"
        "xor r8d, r8d\n   xor r9d, r9d\n"
        "1:  mov rax, r13\n   sub rax, r8\n   mov rcx, rbx\n   sub rcx, r9\n   cmp rax, rcx\n   cmova rax, rcx\n   test rax, rax\n   jz 12f\n"
        "16: cmp rax, 8\n   jb 17f\n   mov rdx, [r12 + r8]\n   mov rcx, 0x5c5c5c5c5c5c5c5c\n   xor rcx, rdx\n"
        "push rax\n   mov rax, 0x0101010101010101\n   sub rax, rcx\n   neg rax\n   not rcx\n   and rax, rcx\n"
        "mov rcx, 0x8080808080808080\n   test rax, rcx\n   pop rax\n   jnz 17f\n"
        "mov [r10 + r9], rdx\n   mov qword ptr [r11 + r9], 0\n   add r8, 8\n   add r9, 8\n   sub rax, 8\n   jmp 16b\n"
        "17: test rax, rax\n   jz 12f\n   movzx ecx, byte ptr [r12 + r8]\n   inc r8\n   dec rax\n"
        "cmp ecx, 92\n   je 2f\n   mov byte ptr [r10 + r9], cl\n   mov byte ptr [r11 + r9], 0\n   inc r9\n   jmp 17b\n"
        "2:  cmp r8, r13\n   jae 13f\n   movzx eax, byte ptr [r12 + r8]\n   inc r8\n   lea ecx, [rax - 48]\n   cmp ecx, 7\n   ja 4f\n"
        "mov eax, ecx\n   cmp r8, r13\n   jae 11f\n   movzx edx, byte ptr [r12 + r8]\n   sub edx, 48\n   cmp edx, 7\n   ja 11f\n"
        "lea eax, [rdx + rax*8]\n   inc r8\n   cmp r8, r13\n   jae 11f\n   movzx edx, byte ptr [r12 + r8]\n"
        "sub edx, 48\n   cmp edx, 7\n   ja 11f\n   lea edx, [rdx + rax*8]\n   cmp edx, 255\n   ja 13f\n"
        "mov eax, edx\n   inc r8\n   jmp 11f\n"
        "4:  cmp eax, 110\n   jne 5f\n   mov eax, 10\n   jmp 11f\n"
        "5:  cmp eax, 116\n   jne 6f\n   mov eax, 9\n   jmp 11f\n"
        "6:  cmp eax, 114\n   jne 7f\n   mov eax, 13\n   jmp 11f\n"
        "7:  cmp eax, 97\n   jne 8f\n   mov eax, 7\n   jmp 11f\n"
        "8:  cmp eax, 98\n   jne 9f\n   mov eax, 8\n   jmp 11f\n"
        "9:  cmp eax, 102\n   jne 10f\n   mov eax, 12\n   jmp 11f\n"
        "10: cmp eax, 118\n   jne 11f\n   mov eax, 11\n"
        "11: mov byte ptr [r10 + r9], al\n   mov byte ptr [r11 + r9], 1\n   inc r9\n   jmp 1b\n"
        "12: mov rax, r9\n   pop r13\n   pop r12\n   pop rbx\n" SH_ESC_RET
        "13: mov rax, -1\n   pop r13\n   pop r12\n   pop rbx\n" SH_ESC_RET
        "14: xor eax, eax\n   pop r13\n   pop r12\n   pop rbx\n" SH_ESC_RET
        SH_ESC_END(sh_unescape_tr)

        /* sh_ansi_sgr_skip(src, len) -> total SGR length (>= 3) or 0 */
        SH_ESC_FUNC(sh_ansi_sgr_skip)
        SH_ESC_LOAD2
        "test r8, r8\n   jz 3f\n   cmp r9, 3\n   jb 3f\n   cmp word ptr [r8], 0x5b1b\n   jne 3f\n"
        "add r9, r8\n   lea rax, [r8 + 2]\n"
        "1:  cmp rax, r9\n   jae 3f\n   movzx ecx, byte ptr [rax]\n   inc rax\n   lea edx, [rcx - 32]\n   cmp edx, 31\n   jbe 1b\n"
        "cmp ecx, 109\n   jne 3f\n   sub rax, r8\n" SH_ESC_RET
        "3:  xor eax, eax\n" SH_ESC_RET
        SH_ESC_END(sh_ansi_sgr_skip)

        ".att_syntax prefix\n"
);

#elif defined(SH_ESC_RISCV64)

__asm__(
        SH_ESC_SECTION

        /* sh_unescape_c(a0=src, a1=len, a2=dst, a3=cap, a4=flags) -> a0=out */
        SH_ESC_FUNC(sh_unescape_c)
        "beqz a0, 29f\n   beqz a1, 29f\n   beqz a2, 29f\n   beqz a3, 29f\n"
        "add a1, a0, a1\n   mv a5, a2\n   add a3, a2, a3\n"
        "1:  bgeu a0, a1, 28f\n   bgeu a2, a3, 28f\n   lbu a7, 0(a0)\n   addi a0, a0, 1\n"
        "li t5, 92\n   beq a7, t5, 2f\n   bnez a7, 30f\n   srli t4, a4, 17\n   andi t4, t4, 1\n   bnez t4, 28f\n"
        "30: sb a7, 0(a2)\n   addi a2, a2, 1\n   j 1b\n"
        "2:  bltu a0, a1, 3f\n   srli t4, a4, 16\n   andi t4, t4, 1\n   bnez t4, 28f\n"
        "sb t5, 0(a2)\n   addi a2, a2, 1\n   j 28f\n"
        "3:  lbu a7, 0(a0)\n   addi a0, a0, 1\n"
        "li t5, 110\n   bne a7, t5, 4f\n   li t0, 10\n   j 25f\n"
        "4:  li t5, 116\n   bne a7, t5, 5f\n   li t0, 9\n   j 25f\n"
        "5:  li t5, 114\n   bne a7, t5, 6f\n   li t0, 13\n   j 25f\n"
        "6:  li t5, 97\n   bne a7, t5, 7f\n   li t0, 7\n   j 25f\n"
        "7:  li t5, 102\n   bne a7, t5, 8f\n   li t0, 12\n   j 25f\n"
        "8:  li t5, 118\n   bne a7, t5, 9f\n   li t0, 11\n   j 25f\n"
        "9:  li t5, 98\n   bne a7, t5, 10f\n   andi t4, a4, 1\n   beqz t4, 22f\n   li t0, 8\n   j 25f\n"
        "10: li t5, 101\n   bne a7, t5, 11f\n   andi t4, a4, 2\n   beqz t4, 22f\n   li t0, 27\n   j 25f\n"
        "11: li t5, 69\n   bne a7, t5, 12f\n   andi t4, a4, 4\n   beqz t4, 22f\n   li t0, 27\n   j 25f\n"
        "12: addi t4, a7, -48\n   li t5, 7\n   bgtu t4, t5, 13f\n   andi t6, a4, 8\n   beqz t6, 31f\n"
        "li t1, 8\n   li t2, 2\n   mv t0, t4\n   li t3, 1\n   j 19f\n"
        "31: bnez t4, 22f\n   andi t6, a4, 16\n   beqz t6, 22f\n   li t1, 8\n   li t2, 3\n   li t0, 0\n   li t3, 1\n   j 19f\n"
        "13: li t5, 120\n   bne a7, t5, 14f\n   andi t6, a4, 256\n   bnez t6, 32f\n   andi t6, a4, 128\n   beqz t6, 22f\n"
        "li t1, 16\n   li t2, 2\n   li t0, 0\n   li t3, 0\n   j 19f\n"
        "32: li t1, 16\n   li t2, 3\n   li t0, 0\n   li t3, 0\n   j 19f\n"
        "14: li t5, 111\n   bne a7, t5, 15f\n   andi t6, a4, 32\n   beqz t6, 22f\n   li t1, 8\n   li t2, 3\n   li t0, 0\n   li t3, 0\n   j 19f\n"
        "15: li t5, 100\n   bne a7, t5, 16f\n   andi t6, a4, 64\n   beqz t6, 22f\n   li t1, 10\n   li t2, 3\n   li t0, 0\n   li t3, 0\n   j 19f\n"
        "16: li t5, 99\n   bne a7, t5, 22f\n   srli t6, a4, 12\n   andi t6, t6, 1\n   bnez t6, 28f\n"
        "andi t6, a4, 0x600\n   beqz t6, 22f\n   bgeu a0, a1, 22f\n   lbu t0, 0(a0)\n   addi a0, a0, 1\n"
        "srli t6, a4, 11\n   andi t6, t6, 1\n   beqz t6, 17f\n   li t5, 92\n   bne t0, t5, 17f\n"
        "bgeu a0, a1, 17f\n   lbu t6, 0(a0)\n   bne t6, t5, 17f\n   addi a0, a0, 1\n"
        "17: andi t6, a4, 0x200\n   beqz t6, 18f\n   addi t4, t0, -97\n   li t5, 25\n   bgtu t4, t5, 33f\n   addi t0, t0, -32\n"
        "33: xori t0, t0, 64\n   j 25f\n"
        "18: li t5, 63\n   bne t0, t5, 34f\n   li t0, 127\n   j 25f\n"
        "34: andi t0, t0, 31\n   j 25f\n"
        "19: beqz t2, 21f\n   bgeu a0, a1, 21f\n   lbu t5, 0(a0)\n   addi t6, t5, -48\n"
        "li t4, 9\n   bleu t6, t4, 20f\n   li t4, 16\n   bne t1, t4, 21f\n   ori t6, t5, 32\n   addi t6, t6, -97\n"
        "li t4, 5\n   bgtu t6, t4, 21f\n   addi t6, t6, 10\n"
        "20: bgeu t6, t1, 21f\n   mul t0, t0, t1\n   add t0, t0, t6\n   addi a0, a0, 1\n   addi t3, t3, 1\n   addi t2, t2, -1\n   j 19b\n"
        "21: beqz t3, 22f\n   andi t0, t0, 255\n   j 25f\n"
        "22: srli t6, a4, 13\n   andi t6, t6, 1\n   beqz t6, 24f\n   srli t6, a4, 14\n   andi t6, t6, 1\n   beqz t6, 23f\n"
        "li t5, 92\n   beq a7, t5, 24f\n   li t5, 39\n   beq a7, t5, 24f\n   li t5, 34\n   beq a7, t5, 24f\n"
        "23: srli t6, a4, 15\n   andi t6, t6, 1\n   beqz t6, 35f\n   li t5, 63\n   beq a7, t5, 24f\n"
        "35: li t5, 92\n   sb t5, 0(a2)\n   addi a2, a2, 1\n   bgeu a2, a3, 28f\n"
        "24: mv t0, a7\n"
        "25: bnez t0, 26f\n   srli t6, a4, 17\n   andi t6, t6, 1\n   bnez t6, 28f\n"
        "26: sb t0, 0(a2)\n   addi a2, a2, 1\n   j 1b\n"
        "28: sub a0, a2, a5\n" SH_ESC_RET
        "29: li a0, 0\n" SH_ESC_RET
        SH_ESC_END(sh_unescape_c)

        /* sh_unescape_tr(a0=src, a1=len, a2=dst, a3=esc_out, a4=cap) -> a0=out or -1 */
        SH_ESC_FUNC(sh_unescape_tr)
        "beqz a0, 14f\n   beqz a1, 14f\n   beqz a2, 14f\n   beqz a3, 14f\n   beqz a4, 14f\n"
        "add a1, a0, a1\n   mv a5, a2\n   add a4, a2, a4\n"
        "1:  bgeu a0, a1, 12f\n   bgeu a2, a4, 12f\n   lbu t0, 0(a0)\n   addi a0, a0, 1\n"
        "li t5, 92\n   beq t0, t5, 2f\n   sb t0, 0(a2)\n   sb zero, 0(a3)\n   addi a2, a2, 1\n   addi a3, a3, 1\n   j 1b\n"
        "2:  bgeu a0, a1, 13f\n   lbu t0, 0(a0)\n   addi a0, a0, 1\n   addi a7, t0, -48\n   li t5, 7\n   bgtu a7, t5, 4f\n"
        "bgeu a0, a1, 11f\n   lbu t1, 0(a0)\n   addi t2, t1, -48\n   bgtu t2, t5, 11f\n"
        "slli a7, a7, 3\n   add a7, a7, t2\n   addi a0, a0, 1\n   bgeu a0, a1, 11f\n"
        "lbu t1, 0(a0)\n   addi t2, t1, -48\n   bgtu t2, t5, 11f\n   slli t3, a7, 3\n   add t3, t3, t2\n"
        "li t6, 255\n   bgtu t3, t6, 13f\n   mv a7, t3\n   addi a0, a0, 1\n   j 11f\n"
        "4:  li t5, 110\n   bne t0, t5, 5f\n   li a7, 10\n   j 11f\n"
        "5:  li t5, 116\n   bne t0, t5, 6f\n   li a7, 9\n   j 11f\n"
        "6:  li t5, 114\n   bne t0, t5, 7f\n   li a7, 13\n   j 11f\n"
        "7:  li t5, 97\n   bne t0, t5, 8f\n   li a7, 7\n   j 11f\n"
        "8:  li t5, 98\n   bne t0, t5, 9f\n   li a7, 8\n   j 11f\n"
        "9:  li t5, 102\n   bne t0, t5, 10f\n   li a7, 12\n   j 11f\n"
        "10: li t5, 118\n   bne t0, t5, 15f\n   li a7, 11\n   j 11f\n"
        "15: mv a7, t0\n"
        "11: li t5, 1\n   sb a7, 0(a2)\n   sb t5, 0(a3)\n   addi a2, a2, 1\n   addi a3, a3, 1\n   j 1b\n"
        "12: sub a0, a2, a5\n" SH_ESC_RET
        "13: li a0, -1\n" SH_ESC_RET
        "14: li a0, 0\n" SH_ESC_RET
        SH_ESC_END(sh_unescape_tr)

        /* sh_ansi_sgr_skip(a0=src, a1=len) -> a0=total SGR length (>= 3) or 0 */
        SH_ESC_FUNC(sh_ansi_sgr_skip)
        "beqz a0, 3f\n   li t1, 3\n   bltu a1, t1, 3f\n   lbu t0, 0(a0)\n   li t1, 27\n   bne t0, t1, 3f\n"
        "lbu t0, 1(a0)\n   li t1, 91\n   bne t0, t1, 3f\n   add a1, a0, a1\n   addi a2, a0, 2\n   li t2, 31\n"
        "1:  bgeu a2, a1, 3f\n   lbu t0, 0(a2)\n   addi a2, a2, 1\n   addi t3, t0, -32\n   bleu t3, t2, 1b\n"
        "li t1, 109\n   bne t0, t1, 3f\n   sub a0, a2, a0\n" SH_ESC_RET
        "3:  li a0, 0\n" SH_ESC_RET
        SH_ESC_END(sh_ansi_sgr_skip)
);

#endif

#endif /* SH_SHELL_ESCAPE_ASM_IMPLEMENTATION */
