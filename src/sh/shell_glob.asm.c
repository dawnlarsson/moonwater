/*
        POSIX / Moonwater-compatible shell glob & pattern primitives in pure assembly:
          - sh_glob_magic             : glob metacharacter / extglob head scanner
          - sh_glob_set_end           : [...] bracket-class closer (handles !/^, leading ], [:...:], \x)
          - sh_glob_unit_end          : single escaped byte or bracket-class unit end (optional bound)
          - sh_glob_group_end         : balanced extglob (...) group closer skipping \x and [...]
          - sh_glob_extended_anywhere : extglob head + '(' detector (? * + @ ! followed by '(')
          - sh_glob_match             : linear-time iterative glob matcher (*, ?, \x, [...], fold, UTF-8/[:class:] bailout)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_GLOB_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_GLOB_H
#define MOONWATER_SH_SHELL_GLOB_H

#if defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT32_TYPE__ sh_glob_u32;
typedef __INT32_TYPE__  sh_glob_i32;
typedef __UINT64_TYPE__ sh_glob_u64;
#else
#include <stdint.h>
typedef uint32_t sh_glob_u32;
typedef int32_t  sh_glob_i32;
typedef uint64_t sh_glob_u64;
#endif

#define SH_GLOB_FOLD 1u
#define SH_GLOB_UTF8 2u

sh_glob_i32  sh_glob_magic(const char *pattern, sh_glob_u32 extglob);
const char  *sh_glob_set_end(const char *at);
const char  *sh_glob_unit_end(const char *at, const char *bound);
const char  *sh_glob_group_end(const char *at);
sh_glob_i32  sh_glob_extended_anywhere(const char *pattern);
sh_glob_i32  sh_glob_match(const char *pattern, const char *text, sh_glob_u32 flags);

#endif /* MOONWATER_SH_SHELL_GLOB_H */

#ifdef SH_SHELL_GLOB_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_GLOB_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_GLOB_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_GLOB_RISCV64 1
# else
#  error "src/sh/shell_glob.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_GLOB_ARM64 1
# elif defined(X64) && X64
#  define SH_GLOB_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_GLOB_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_GLOB_ASM_SECTION   ASM_SECTION
# define SH_GLOB_ASM_FUNC(n)   ASM_FUNC(n)
# define SH_GLOB_ASM_END(n)    ASM_END(n)
# define SH_GLOB_ASM_RET       ASM_RET
#else
# define SH_GLOB_ASM_SECTION   ".text\n"
# define SH_GLOB_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define SH_GLOB_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_GLOB_ASM_END(n)   ""
# elif defined(_WIN32)
#  define SH_GLOB_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_GLOB_ASM_END(n)   ""
# else
#  if defined(SH_GLOB_ARM64)
#   define SH_GLOB_ASM_FTYPE   "%function"
#  else
#   define SH_GLOB_ASM_FTYPE   "@function"
#  endif
#  define SH_GLOB_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_GLOB_ASM_FTYPE "\n" #n ":\n"
#  define SH_GLOB_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define SH_GLOB_SYM(n) "_" #n
#else
# define SH_GLOB_SYM(n) #n
#endif

#if defined(SH_GLOB_ARM64)

__asm__(
        SH_GLOB_ASM_SECTION

        /* sh_glob_magic(x0=pattern, w1=extglob) -> 1 if glob/extglob magic, else 0 */
        SH_GLOB_ASM_FUNC(sh_glob_magic)
        "cbz x0, 5f\n   movz x5, #0x0200\n   movk x5, #0x4000, lsl #16\n   movk x5, #0x0c00, lsl #48\n"
        "cbz w1, 1f\n   movk x5, #0x0601\n   movk x5, #0xc000, lsl #16\n"
        "1:  ldrb w2, [x0], #1\n   cbz w2, 5f\n   sub w3, w2, #33\n   cmp w3, #59\n   b.hi 1b\n"
        "lsr x4, x5, x3\n   tbz x4, #0, 1b\n   cmp w2, #92\n   b.eq 2f\n"
        "cmp w2, #43\n   b.eq 3f\n   cmp w2, #64\n   b.eq 3f\n   cmp w2, #33\n   b.ne 4f\n"
        "3:  ldrb w3, [x0]\n   cmp w3, #40\n   b.ne 1b\n"
        "4:  mov w0, #1\n" SH_GLOB_ASM_RET
        "2:  ldrb w3, [x0]\n   cbz w3, 5f\n   add x0, x0, #1\n   b 1b\n"
        "5:  mov w0, #0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_magic)

        /* sh_glob_set_end(x0=at) -> ptr to closing ']' or NULL (clobbers ONLY x0-x4) */
        SH_GLOB_ASM_FUNC(sh_glob_set_end)
        "cbz x0, 7f\n   ldrb w1, [x0]\n   cbz w1, 7f\n   add x0, x0, #1\n   ldrb w1, [x0]\n"
        "cmp w1, #33\n   b.eq 1f\n   cmp w1, #94\n   b.ne 2f\n"
        "1:  add x0, x0, #1\n   ldrb w1, [x0]\n"
        "2:  cmp w1, #93\n   b.ne 3f\n   add x0, x0, #1\n"
        "3:  ldrb w1, [x0]\n   cbz w1, 7f\n   cmp w1, #93\n   b.eq 8f\n"
        "cmp w1, #91\n   b.eq 4f\n   cmp w1, #92\n   b.eq 6f\n   add x0, x0, #1\n   b 3b\n"
        "4:  ldrb w2, [x0, #1]\n   cmp w2, #58\n   b.ne 9f\n   add x3, x0, #2\n"
        "5:  ldrb w2, [x3]\n   cbz w2, 9f\n   cmp w2, #58\n   b.ne 10f\n"
        "ldrb w4, [x3, #1]\n   cmp w4, #93\n   b.ne 10f\n   add x0, x3, #2\n   b 3b\n"
        "10: add x3, x3, #1\n   b 5b\n"
        "9:  add x0, x0, #1\n   b 3b\n"
        "6:  ldrb w2, [x0, #1]\n   cbz w2, 7f\n   add x0, x0, #2\n   b 3b\n"
        "7:  mov x0, #0\n"
        "8:  " SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_set_end)

        /* sh_glob_unit_end(x0=at, x1=bound) -> one past \x or [...], else NULL */
        SH_GLOB_ASM_FUNC(sh_glob_unit_end)
        "cbz x0, 4f\n   cbz x1, 1f\n   cmp x0, x1\n   b.hs 4f\n"
        "1:  ldrb w2, [x0]\n   cmp w2, #92\n   b.ne 2f\n   add x3, x0, #1\n"
        "cbz x1, 5f\n   cmp x3, x1\n   b.hs 4f\n"
        "5:  ldrb w2, [x3]\n   cbz w2, 4f\n   add x0, x0, #2\n" SH_GLOB_ASM_RET
        "2:  cmp w2, #91\n   b.ne 4f\n   mov x6, x1\n   str x30, [sp, #-16]!\n"
        "bl " SH_GLOB_SYM(sh_glob_set_end) "\n   ldr x30, [sp], #16\n"
        "cbz x0, 4f\n   cbz x6, 3f\n   cmp x0, x6\n   b.hs 4f\n"
        "3:  add x0, x0, #1\n" SH_GLOB_ASM_RET
        "4:  mov x0, #0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_unit_end)

        /* sh_glob_group_end(x0=at) -> one past matching ')', skipping \x and [...] */
        SH_GLOB_ASM_FUNC(sh_glob_group_end)
        "cbz x0, 7f\n   mov x6, x0\n   mov x7, #0\n"
        "1:  ldrb w1, [x6]\n   cbz w1, 7f\n   cmp w1, #92\n   b.eq 2f\n"
        "cmp w1, #91\n   b.eq 3f\n   cmp w1, #40\n   b.eq 4f\n   cmp w1, #41\n   b.eq 5f\n"
        "6:  add x6, x6, #1\n   b 1b\n"
        "2:  ldrb w2, [x6, #1]\n   cbz w2, 7f\n   add x6, x6, #2\n   b 1b\n"
        "3:  mov x0, x6\n   str x30, [sp, #-16]!\n   bl " SH_GLOB_SYM(sh_glob_set_end) "\n   ldr x30, [sp], #16\n"
        "cbz x0, 6b\n   add x6, x0, #1\n   b 1b\n"
        "4:  add x7, x7, #1\n   add x6, x6, #1\n   b 1b\n"
        "5:  sub x7, x7, #1\n   cbnz x7, 6b\n   add x0, x6, #1\n" SH_GLOB_ASM_RET
        "7:  mov x0, #0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_group_end)

        /* sh_glob_extended_anywhere(x0=pattern) -> 1 if (? * + @ !)'(' exists, else 0 */
        SH_GLOB_ASM_FUNC(sh_glob_extended_anywhere)
        "cbz x0, 3f\n   mov x1, x0\n   movz w4, #0x0601\n   movk w4, #0xc000, lsl #16\n"
        "mov x6, #0x0101010101010101\n   mov x7, #0x8080808080808080\n   lsl x8, x6, #5\n   orr x8, x8, x6, lsl #3\n"
        "0:  tst x0, #7\n   b.eq 2f\n"
        "1:  ldrb w2, [x0], #1\n   cbz w2, 3f\n   cmp w2, #40\n   b.ne 0b\n"
        "sub x5, x0, x1\n   cmp x5, #2\n   b.lo 0b\n   ldrb w3, [x0, #-2]\n"
        "sub w3, w3, #33\n   cmp w3, #31\n   b.hi 0b\n   lsr w5, w4, w3\n   tbz w5, #0, 0b\n"
        "mov w0, #1\n" SH_GLOB_ASM_RET
        "2:  ldr x9, [x0]\n   sub x10, x9, x6\n   bic x10, x10, x9\n   eor x11, x9, x8\n"
        "sub x12, x11, x6\n   bic x12, x12, x11\n   orr x10, x10, x12\n   tst x10, x7\n   b.ne 1b\n"
        "add x0, x0, #8\n   b 2b\n"
        "3:  mov w0, #0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_extended_anywhere)

        /* sh_glob_match(x0=pattern, x1=text, w2=flags) -> 1 match, 0 no match, -1 fallback */
        /* live across sh_glob_set_end: x5=pat, x6=txt, w7=flags, x8=star, x9=back, w10=behind */
        SH_GLOB_ASM_FUNC(sh_glob_match)
        "cbz x0, 30f\n   cbz x1, 30f\n   mov x5, x0\n   mov x6, x1\n   mov w7, w2\n"
        "mov x8, #0\n   mov x9, #0\n   mov w10, #0\n"
        "1:  ldrb w11, [x6]\n   cbz w11, 25f\n   ldrb w12, [x5]\n   cmp w12, w11\n   b.ne 3f\n"
        "cmp w12, #92\n   b.hi 18f\n   cmp w12, #42\n   b.eq 2f\n   cmp w12, #91\n   b.eq 4f\n"
        "cmp w12, #92\n   b.eq 15f\n   b 18f\n"
        "3:  cmp w12, #92\n   b.hi 17f\n   cmp w12, #42\n   b.ne 4f\n"
        "2:  add x5, x5, #1\n   ldrb w12, [x5]\n   cmp w12, #42\n   b.eq 2b\n   cbz w12, 31f\n"
        "mov x8, x5\n   mov x9, x6\n   mov w10, #0\n   tbnz w7, #0, 4f\n"
        "cmp w12, #63\n   b.eq 4f\n   cmp w12, #91\n   b.eq 4f\n   cmp w12, #92\n   b.eq 4f\n   mov w10, w12\n"
        "4:  cmp w12, #91\n   b.ne 15f\n   mov x0, x5\n   str x30, [sp, #-16]!\n"
        "bl " SH_GLOB_SYM(sh_glob_set_end) "\n   ldr x30, [sp], #16\n   cbz x0, 15f\n"
        "tbz w7, #1, 5f\n   cmp w11, #128\n   b.hs 32f\n"
        "5:  add x1, x5, #1\n   mov w2, #0\n   mov w3, #0\n   ldrb w4, [x1]\n"
        "cmp w4, #33\n   b.eq 6f\n   cmp w4, #94\n   b.ne 7f\n"
        "6:  mov w2, #1\n   add x1, x1, #1\n"
        "7:  mov w13, w11\n   tbz w7, #0, 8f\n   sub w4, w13, #65\n   cmp w4, #25\n   b.hi 8f\n   orr w13, w13, #32\n"
        "8:  cmp x1, x0\n   b.hs 14f\n   ldrb w4, [x1]\n   cmp w4, #91\n   b.ne 10f\n"
        "add x14, x1, #1\n   cmp x14, x0\n   b.hs 10f\n   ldrb w15, [x14]\n   cmp w15, #58\n   b.ne 10f\n"
        "add x14, x1, #2\n"
        "9:  add x15, x14, #1\n   cmp x15, x0\n   b.hs 10f\n   ldrb w12, [x14]\n   cmp w12, #58\n   b.ne 33f\n"
        "ldrb w12, [x15]\n   cmp w12, #93\n   b.eq 32f\n"
        "33: add x14, x14, #1\n   b 9b\n"
        "10: cmp w4, #92\n   b.ne 11f\n   add x14, x1, #1\n   cmp x14, x0\n   b.hs 11f\n   mov x1, x14\n   ldrb w4, [x1]\n"
        "11: add x1, x1, #1\n   tbz w7, #0, 12f\n   sub w12, w4, #65\n   cmp w12, #25\n   b.hi 12f\n   orr w4, w4, #32\n"
        "12: add x14, x1, #1\n   cmp x14, x0\n   b.hs 13f\n   ldrb w12, [x1]\n   cmp w12, #45\n   b.ne 13f\n"
        "add x1, x1, #1\n   ldrb w12, [x1]\n   cmp w12, #92\n   b.ne 34f\n   add x14, x1, #1\n   cmp x14, x0\n   b.hs 34f\n"
        "mov x1, x14\n   ldrb w12, [x1]\n"
        "34: add x1, x1, #1\n   tbz w7, #0, 35f\n   sub w15, w12, #65\n   cmp w15, #25\n   b.hi 35f\n   orr w12, w12, #32\n"
        "35: cmp w13, w4\n   b.lo 8b\n   cmp w13, w12\n   b.hi 8b\n   mov w3, #1\n   b 8b\n"
        "13: cmp w13, w4\n   b.ne 8b\n   mov w3, #1\n   b 8b\n"
        "14: eor w3, w3, w2\n   cbz w3, 20f\n   add x5, x0, #1\n   add x6, x6, #1\n   b 1b\n"
        "15: cbz w12, 20f\n   cmp w12, #92\n   b.ne 16f\n   ldrb w13, [x5, #1]\n   cbz w13, 17f\n"
        "add x5, x5, #1\n   mov w12, w13\n   b 17f\n"
        "16: cmp w12, #63\n   b.ne 17f\n   tbz w7, #1, 18f\n   cmp w11, #128\n   b.hs 32f\n   b 18f\n"
        "17: cmp w12, w11\n   b.eq 18f\n   tbz w7, #0, 20f\n   sub w13, w12, #65\n   cmp w13, #25\n   b.hi 36f\n   orr w12, w12, #32\n"
        "36: sub w13, w11, #65\n   cmp w13, #25\n   b.hi 37f\n   orr w11, w11, #32\n"
        "37: cmp w12, w11\n   b.ne 20f\n"
        "18: add x5, x5, #1\n   add x6, x6, #1\n   b 1b\n"
        "20: cbz x8, 30f\n   tbz w7, #1, 21f\n   ldrb w11, [x9]\n   cmp w11, #128\n   b.hs 32f\n"
        "21: mov x5, x8\n   add x9, x9, #1\n   mov x6, x9\n   cbz w10, 1b\n"
        "22: ldrb w11, [x6], #1\n   cmp w11, w10\n   b.eq 23f\n   cbnz w11, 22b\n   b 30f\n"
        "23: sub x6, x6, #1\n   mov x9, x6\n   b 1b\n"
        "25: ldrb w12, [x5]\n   cmp w12, #42\n   b.ne 26f\n   add x5, x5, #1\n   b 25b\n"
        "26: cmp w12, #0\n   cset w0, eq\n" SH_GLOB_ASM_RET
        "30: mov w0, #0\n" SH_GLOB_ASM_RET
        "31: mov w0, #1\n" SH_GLOB_ASM_RET
        "32: mov w0, #-1\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_match)
);

#elif defined(SH_GLOB_X64)

#if defined(_WIN32)
# define SH_G_A1   "rcx"
# define SH_G_LD1  "mov r8, rcx\n"
# define SH_G_LD2  "mov r8, rcx\n   mov r9, rdx\n"
# define SH_G_LD3  "mov r12, rcx\n   mov r13, rdx\n   mov ebx, r8d\n"
# define SH_G_SET1(r) "mov rcx, " #r "\n"
#else
# define SH_G_A1   "rdi"
# define SH_G_LD1  "mov r8, rdi\n"
# define SH_G_LD2  "mov r8, rdi\n   mov r9, rsi\n"
# define SH_G_LD3  "mov r12, rdi\n   mov r13, rsi\n   mov ebx, edx\n"
# define SH_G_SET1(r) "mov rdi, " #r "\n"
#endif

__asm__(
        SH_GLOB_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* sh_glob_magic(pattern, extglob) */
        SH_GLOB_ASM_FUNC(sh_glob_magic)
        SH_G_LD2
        "test r8, r8\n   jz 5f\n   mov r10, 0x0c00000040000200\n   test r9d, r9d\n   jz 1f\n"
        "mov r10, 0x0c000000c0000601\n"
        "1:  movzx eax, byte ptr [r8]\n   test eax, eax\n   jz 5f\n   inc r8\n"
        "lea ecx, [rax - 33]\n   cmp ecx, 59\n   ja 1b\n   bt r10, rcx\n   jnc 1b\n"
        "cmp eax, 92\n   je 2f\n   cmp eax, 43\n   je 3f\n   cmp eax, 64\n   je 3f\n   cmp eax, 33\n   jne 4f\n"
        "3:  cmp byte ptr [r8], 40\n   jne 1b\n"
        "4:  mov eax, 1\n" SH_GLOB_ASM_RET
        "2:  cmp byte ptr [r8], 0\n   je 5f\n   inc r8\n   jmp 1b\n"
        "5:  xor eax, eax\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_magic)

        /* sh_glob_set_end(at) -> ptr to closing ']' or NULL (clobbers ONLY rax, rcx, rdx) */
        SH_GLOB_ASM_FUNC(sh_glob_set_end)
        "mov rax, " SH_G_A1 "\n   test rax, rax\n   jz 7f\n   cmp byte ptr [rax], 0\n   je 7f\n"
        "inc rax\n   movzx ecx, byte ptr [rax]\n   cmp ecx, 33\n   je 1f\n   cmp ecx, 94\n   jne 2f\n"
        "1:  inc rax\n   movzx ecx, byte ptr [rax]\n"
        "2:  cmp ecx, 93\n   jne 3f\n   inc rax\n"
        "3:  movzx ecx, byte ptr [rax]\n   test ecx, ecx\n   jz 7f\n   cmp ecx, 93\n   je 8f\n"
        "cmp ecx, 91\n   je 4f\n   cmp ecx, 92\n   je 6f\n   inc rax\n   jmp 3b\n"
        "4:  cmp byte ptr [rax + 1], 58\n   jne 9f\n   lea rdx, [rax + 2]\n"
        "5:  movzx ecx, byte ptr [rdx]\n   test ecx, ecx\n   jz 9f\n   cmp ecx, 58\n   jne 10f\n"
        "cmp byte ptr [rdx + 1], 93\n   jne 10f\n   lea rax, [rdx + 2]\n   jmp 3b\n"
        "10: inc rdx\n   jmp 5b\n"
        "9:  inc rax\n   jmp 3b\n"
        "6:  cmp byte ptr [rax + 1], 0\n   je 7f\n   add rax, 2\n   jmp 3b\n"
        "7:  xor eax, eax\n"
        "8:  " SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_set_end)

        /* sh_glob_unit_end(at, bound) */
        SH_GLOB_ASM_FUNC(sh_glob_unit_end)
        SH_G_LD2
        "test r8, r8\n   jz 4f\n   test r9, r9\n   jz 1f\n   cmp r8, r9\n   jae 4f\n"
        "1:  movzx eax, byte ptr [r8]\n   cmp eax, 92\n   jne 2f\n   lea rcx, [r8 + 1]\n"
        "test r9, r9\n   jz 5f\n   cmp rcx, r9\n   jae 4f\n"
        "5:  cmp byte ptr [rcx], 0\n   je 4f\n   lea rax, [r8 + 2]\n" SH_GLOB_ASM_RET
        "2:  cmp eax, 91\n   jne 4f\n   " SH_G_SET1(r8) "sub rsp, 8\n   call " SH_GLOB_SYM(sh_glob_set_end) "\n   add rsp, 8\n"
        "test rax, rax\n   jz 4f\n   test r9, r9\n   jz 3f\n   cmp rax, r9\n   jae 4f\n"
        "3:  inc rax\n" SH_GLOB_ASM_RET
        "4:  xor eax, eax\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_unit_end)

        /* sh_glob_group_end(at) */
        SH_GLOB_ASM_FUNC(sh_glob_group_end)
        SH_G_LD1
        "test r8, r8\n   jz 7f\n   xor r9d, r9d\n"
        "1:  movzx eax, byte ptr [r8]\n   test eax, eax\n   jz 7f\n   cmp eax, 92\n   je 2f\n"
        "cmp eax, 91\n   je 3f\n   cmp eax, 40\n   je 4f\n   cmp eax, 41\n   je 5f\n"
        "6:  inc r8\n   jmp 1b\n"
        "2:  cmp byte ptr [r8 + 1], 0\n   je 7f\n   add r8, 2\n   jmp 1b\n"
        "3:  " SH_G_SET1(r8) "sub rsp, 8\n   call " SH_GLOB_SYM(sh_glob_set_end) "\n   add rsp, 8\n"
        "test rax, rax\n   jz 6b\n   lea r8, [rax + 1]\n   jmp 1b\n"
        "4:  inc r9\n   inc r8\n   jmp 1b\n"
        "5:  dec r9\n   jnz 6b\n   lea rax, [r8 + 1]\n" SH_GLOB_ASM_RET
        "7:  xor eax, eax\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_group_end)

        /* sh_glob_extended_anywhere(pattern) */
        SH_GLOB_ASM_FUNC(sh_glob_extended_anywhere)
        SH_G_LD1
        "test r8, r8\n   jz 3f\n   mov r9, r8\n   mov r10d, 0xc0000601\n"
        "1:  movzx eax, byte ptr [r8]\n   test eax, eax\n   jz 3f\n   inc r8\n   cmp eax, 40\n   jne 1b\n"
        "mov rcx, r8\n   sub rcx, r9\n   cmp rcx, 2\n   jb 1b\n   movzx eax, byte ptr [r8 - 2]\n"
        "sub eax, 33\n   cmp eax, 31\n   ja 1b\n   bt r10d, eax\n   jnc 1b\n"
        "mov eax, 1\n" SH_GLOB_ASM_RET
        "3:  xor eax, eax\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_extended_anywhere)

        /* sh_glob_match(pattern, text, flags) -> 1 match, 0 no match, -1 fallback */
        /* live across sh_glob_set_end: r12=pat, r13=txt, ebx=flags, r8=star, r9=back, r10d=behind */
        SH_GLOB_ASM_FUNC(sh_glob_match)
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n" SH_G_LD3
        "test r12, r12\n   jz 30f\n   test r13, r13\n   jz 30f\n"
        "xor r8d, r8d\n   xor r9d, r9d\n   xor r10d, r10d\n"
        "1:  movzx r11d, byte ptr [r13]\n   test r11d, r11d\n   jz 25f\n"
        "movzx eax, byte ptr [r12]\n   cmp eax, 92\n   ja 17f\n   cmp eax, 42\n   jne 4f\n"
        "2:  inc r12\n   movzx eax, byte ptr [r12]\n   cmp eax, 42\n   je 2b\n   test eax, eax\n   jz 31f\n"
        "mov r8, r12\n   mov r9, r13\n   xor r10d, r10d\n   test ebx, 1\n   jnz 4f\n"
        "cmp eax, 63\n   je 4f\n   cmp eax, 91\n   je 4f\n   cmp eax, 92\n   je 4f\n   mov r10d, eax\n"
        "4:  cmp eax, 91\n   jne 15f\n   " SH_G_SET1(r12) "call " SH_GLOB_SYM(sh_glob_set_end) "\n"
        "test rax, rax\n   jnz 5f\n   mov eax, 91\n   jmp 15f\n"
        "5:  test ebx, 2\n   jz 6f\n   cmp r11d, 128\n   jae 32f\n"
        "6:  lea rcx, [r12 + 1]\n   xor edx, edx\n   movzx r14d, byte ptr [rcx]\n"
        "cmp r14d, 33\n   je 7f\n   cmp r14d, 94\n   jne 8f\n"
        "7:  mov edx, 1\n   inc rcx\n"
        "8:  test ebx, 1\n   jz 9f\n   lea r14d, [r11 - 65]\n   cmp r14d, 25\n   ja 9f\n   or r11d, 32\n"
        "9:  cmp rcx, rax\n   jae 14f\n   movzx r14d, byte ptr [rcx]\n   cmp r14d, 91\n   jne 11f\n"
        "lea r15, [rcx + 1]\n   cmp r15, rax\n   jae 11f\n   cmp byte ptr [r15], 58\n   jne 11f\n   lea r15, [rcx + 2]\n"
        "10: lea r14, [r15 + 1]\n   cmp r14, rax\n   jae 38f\n   cmp byte ptr [r15], 58\n   jne 33f\n"
        "cmp byte ptr [r14], 93\n   je 32f\n"
        "33: inc r15\n   jmp 10b\n"
        "38: mov r14d, 91\n"
        "11: cmp r14d, 92\n   jne 12f\n   lea r15, [rcx + 1]\n   cmp r15, rax\n   jae 12f\n   mov rcx, r15\n   movzx r14d, byte ptr [rcx]\n"
        "12: inc rcx\n   test ebx, 1\n   jz 13f\n   lea r15d, [r14 - 65]\n   cmp r15d, 25\n   ja 13f\n   or r14d, 32\n"
        "13: lea r15, [rcx + 1]\n   cmp r15, rax\n   jae 36f\n   cmp byte ptr [rcx], 45\n   jne 36f\n"
        "inc rcx\n   movzx r15d, byte ptr [rcx]\n   cmp r15d, 92\n   jne 34f\n"
        "lea r15, [rcx + 1]\n   cmp r15, rax\n   jae 39f\n   mov rcx, r15\n"
        "39: movzx r15d, byte ptr [rcx]\n"
        "34: inc rcx\n   test ebx, 1\n   jz 35f\n   push rcx\n   lea ecx, [r15 - 65]\n   cmp ecx, 25\n   pop rcx\n   ja 35f\n   or r15d, 32\n"
        "35: cmp r11d, r14d\n   jb 9b\n   cmp r11d, r15d\n   ja 9b\n   or edx, 2\n   jmp 9b\n"
        "36: cmp r11d, r14d\n   jne 9b\n   or edx, 2\n   jmp 9b\n"
        "14: mov ecx, edx\n   shr ecx, 1\n   xor ecx, edx\n   test ecx, 1\n   jz 20f\n"
        "lea r12, [rax + 1]\n   inc r13\n   jmp 1b\n"
        "15: test eax, eax\n   jz 20f\n   cmp eax, 92\n   jne 16f\n   movzx ecx, byte ptr [r12 + 1]\n   test ecx, ecx\n   jz 17f\n"
        "inc r12\n   mov eax, ecx\n   jmp 17f\n"
        "16: cmp eax, 63\n   jne 17f\n   test ebx, 2\n   jz 18f\n   cmp r11d, 128\n   jae 32f\n   jmp 18f\n"
        "17: cmp eax, r11d\n   je 18f\n   test ebx, 1\n   jz 20f\n   lea ecx, [rax - 65]\n   cmp ecx, 25\n   ja 40f\n   or eax, 32\n"
        "40: lea ecx, [r11 - 65]\n   cmp ecx, 25\n   ja 41f\n   or r11d, 32\n"
        "41: cmp eax, r11d\n   jne 20f\n"
        "18: inc r12\n   inc r13\n   jmp 1b\n"
        "20: test r8, r8\n   jz 30f\n   test ebx, 2\n   jz 21f\n   cmp byte ptr [r9], 128\n   jae 32f\n"
        "21: mov r12, r8\n   inc r9\n   mov r13, r9\n   test r10d, r10d\n   jz 1b\n"
        "22: movzx eax, byte ptr [r13]\n   test eax, eax\n   jz 30f\n   cmp eax, r10d\n   je 23f\n   inc r13\n   jmp 22b\n"
        "23: mov r9, r13\n   jmp 1b\n"
        "25: movzx eax, byte ptr [r12]\n   cmp eax, 42\n   jne 26f\n   inc r12\n   jmp 25b\n"
        "26: test eax, eax\n   sete al\n   movzx eax, al\n   jmp 29f\n"
        "30: xor eax, eax\n   jmp 29f\n"
        "31: mov eax, 1\n   jmp 29f\n"
        "32: mov eax, -1\n"
        "29: pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_match)

        ".att_syntax prefix\n"
);

#elif defined(SH_GLOB_RISCV64)

__asm__(
        SH_GLOB_ASM_SECTION

        /* sh_glob_magic(a0=pattern, a1=extglob) */
        SH_GLOB_ASM_FUNC(sh_glob_magic)
        "beqz a0, 5f\n   slli a1, a1, 32\n   srli a1, a1, 32\n"
        "li t4, 0x0c00000040000200\n   beqz a1, 0f\n   li t4, 0x0c000000c0000601\n"
        "0:  li t5, 59\n   li t2, 92\n"
        "1:  lbu t0, 0(a0)\n   beqz t0, 5f\n   addi a0, a0, 1\n   addi t1, t0, -33\n   bgtu t1, t5, 1b\n"
        "srl t3, t4, t1\n   andi t3, t3, 1\n   beqz t3, 1b\n   beq t0, t2, 2f\n"
        "li t1, 43\n   beq t0, t1, 3f\n   li t1, 64\n   beq t0, t1, 3f\n   li t1, 33\n   bne t0, t1, 4f\n"
        "3:  lbu t1, 0(a0)\n   li t6, 40\n   bne t1, t6, 1b\n"
        "4:  li a0, 1\n" SH_GLOB_ASM_RET
        "2:  lbu t1, 0(a0)\n   beqz t1, 5f\n   addi a0, a0, 1\n   j 1b\n"
        "5:  li a0, 0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_magic)

        /* sh_glob_set_end(a0=at) -> ptr to closing ']' or NULL (clobbers ONLY a0, t0-t3) */
        SH_GLOB_ASM_FUNC(sh_glob_set_end)
        "beqz a0, 7f\n   lbu t0, 0(a0)\n   beqz t0, 7f\n   addi a0, a0, 1\n   lbu t0, 0(a0)\n"
        "li t1, 33\n   beq t0, t1, 1f\n   li t1, 94\n   bne t0, t1, 2f\n"
        "1:  addi a0, a0, 1\n   lbu t0, 0(a0)\n"
        "2:  li t1, 93\n   bne t0, t1, 3f\n   addi a0, a0, 1\n"
        "3:  lbu t0, 0(a0)\n   beqz t0, 7f\n   li t1, 93\n   beq t0, t1, 8f\n"
        "li t1, 91\n   beq t0, t1, 4f\n   li t1, 92\n   beq t0, t1, 6f\n   addi a0, a0, 1\n   j 3b\n"
        "4:  lbu t1, 1(a0)\n   li t2, 58\n   bne t1, t2, 9f\n   addi t3, a0, 2\n"
        "5:  lbu t1, 0(t3)\n   beqz t1, 9f\n   bne t1, t2, 10f\n"
        "lbu t1, 1(t3)\n   li t0, 93\n   bne t1, t0, 10f\n   addi a0, t3, 2\n   j 3b\n"
        "10: addi t3, t3, 1\n   j 5b\n"
        "9:  addi a0, a0, 1\n   j 3b\n"
        "6:  lbu t1, 1(a0)\n   beqz t1, 7f\n   addi a0, a0, 2\n   j 3b\n"
        "7:  li a0, 0\n"
        "8:  " SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_set_end)

        /* sh_glob_unit_end(a0=at, a1=bound) */
        SH_GLOB_ASM_FUNC(sh_glob_unit_end)
        "beqz a0, 4f\n   beqz a1, 1f\n   bgeu a0, a1, 4f\n"
        "1:  lbu t0, 0(a0)\n   li t1, 92\n   bne t0, t1, 2f\n   addi t2, a0, 1\n"
        "beqz a1, 5f\n   bgeu t2, a1, 4f\n"
        "5:  lbu t0, 0(t2)\n   beqz t0, 4f\n   addi a0, a0, 2\n" SH_GLOB_ASM_RET
        "2:  li t1, 91\n   bne t0, t1, 4f\n   addi sp, sp, -16\n   sd ra, 8(sp)\n"
        "call " SH_GLOB_SYM(sh_glob_set_end) "\n   ld ra, 8(sp)\n   addi sp, sp, 16\n"
        "beqz a0, 4f\n   beqz a1, 3f\n   bgeu a0, a1, 4f\n"
        "3:  addi a0, a0, 1\n" SH_GLOB_ASM_RET
        "4:  li a0, 0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_unit_end)

        /* sh_glob_group_end(a0=at) */
        SH_GLOB_ASM_FUNC(sh_glob_group_end)
        "beqz a0, 7f\n   mv a1, a0\n   li a2, 0\n"
        "1:  lbu t0, 0(a1)\n   beqz t0, 7f\n   li t1, 92\n   beq t0, t1, 2f\n"
        "li t1, 91\n   beq t0, t1, 3f\n   li t1, 40\n   beq t0, t1, 4f\n   li t1, 41\n   beq t0, t1, 5f\n"
        "6:  addi a1, a1, 1\n   j 1b\n"
        "2:  lbu t0, 1(a1)\n   beqz t0, 7f\n   addi a1, a1, 2\n   j 1b\n"
        "3:  mv a0, a1\n   addi sp, sp, -16\n   sd ra, 8(sp)\n   call " SH_GLOB_SYM(sh_glob_set_end) "\n"
        "ld ra, 8(sp)\n   addi sp, sp, 16\n   beqz a0, 6b\n   addi a1, a0, 1\n   j 1b\n"
        "4:  addi a2, a2, 1\n   addi a1, a1, 1\n   j 1b\n"
        "5:  addi a2, a2, -1\n   bnez a2, 6b\n   addi a0, a1, 1\n" SH_GLOB_ASM_RET
        "7:  li a0, 0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_group_end)

        /* sh_glob_extended_anywhere(a0=pattern) */
        SH_GLOB_ASM_FUNC(sh_glob_extended_anywhere)
        "beqz a0, 3f\n   mv a1, a0\n   li t4, 0xc0000601\n   li t5, 40\n   li t6, 31\n"
        "1:  lbu t0, 0(a0)\n   beqz t0, 3f\n   addi a0, a0, 1\n   bne t0, t5, 1b\n"
        "sub t1, a0, a1\n   li t2, 2\n   bltu t1, t2, 1b\n   lbu t1, -2(a0)\n"
        "addi t1, t1, -33\n   bgtu t1, t6, 1b\n   srl t2, t4, t1\n   andi t2, t2, 1\n   beqz t2, 1b\n"
        "li a0, 1\n" SH_GLOB_ASM_RET
        "3:  li a0, 0\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_extended_anywhere)

        /* sh_glob_match(a0=pattern, a1=text, a2=flags) -> 1 match, 0 no match, -1 fallback */
        /* live across sh_glob_set_end: a3=pat, a4=txt, a5=flags, a6=star, a7=back, t6=behind */
        SH_GLOB_ASM_FUNC(sh_glob_match)
        "beqz a0, 30f\n   beqz a1, 30f\n   mv a3, a0\n   mv a4, a1\n   andi a5, a2, 3\n"
        "li a6, 0\n   li a7, 0\n   li t6, 0\n"
        "1:  lbu t4, 0(a4)\n   beqz t4, 25f\n   lbu t5, 0(a3)\n   li t0, 92\n   bgtu t5, t0, 17f\n"
        "li t0, 42\n   bne t5, t0, 4f\n"
        "2:  addi a3, a3, 1\n   lbu t5, 0(a3)\n   beq t5, t0, 2b\n   beqz t5, 31f\n"
        "mv a6, a3\n   mv a7, a4\n   li t6, 0\n   andi t1, a5, 1\n   bnez t1, 4f\n"
        "li t1, 63\n   beq t5, t1, 4f\n   li t1, 91\n   beq t5, t1, 4f\n   li t1, 92\n   beq t5, t1, 4f\n   mv t6, t5\n"
        "4:  li t0, 91\n   bne t5, t0, 15f\n   mv a0, a3\n   addi sp, sp, -16\n   sd ra, 8(sp)\n"
        "call " SH_GLOB_SYM(sh_glob_set_end) "\n   ld ra, 8(sp)\n   addi sp, sp, 16\n   beqz a0, 15f\n"
        "andi t0, a5, 2\n   beqz t0, 5f\n   li t0, 128\n   bgeu t4, t0, 32f\n"
        "5:  addi a1, a3, 1\n   li a2, 0\n   lbu t0, 0(a1)\n   li t1, 33\n   beq t0, t1, 6f\n   li t1, 94\n   bne t0, t1, 7f\n"
        "6:  li a2, 1\n   addi a1, a1, 1\n"
        "7:  andi t0, a5, 1\n   beqz t0, 8f\n   addi t1, t4, -65\n   li t2, 25\n   bgtu t1, t2, 8f\n   ori t4, t4, 32\n"
        "8:  bgeu a1, a0, 14f\n   lbu t0, 0(a1)\n   li t1, 91\n   bne t0, t1, 10f\n"
        "addi t2, a1, 1\n   bgeu t2, a0, 10f\n   lbu t3, 0(t2)\n   li t1, 58\n   bne t3, t1, 10f\n   addi t2, a1, 2\n"
        "9:  addi t3, t2, 1\n   bgeu t3, a0, 10f\n   lbu t0, 0(t2)\n   bne t0, t1, 33f\n"
        "lbu t0, 0(t3)\n   li t5, 93\n   beq t0, t5, 32f\n"
        "33: addi t2, t2, 1\n   j 9b\n"
        "10: lbu t0, 0(a1)\n   li t1, 92\n   bne t0, t1, 11f\n   addi t2, a1, 1\n   bgeu t2, a0, 11f\n   mv a1, t2\n   lbu t0, 0(a1)\n"
        "11: addi a1, a1, 1\n   andi t1, a5, 1\n   beqz t1, 12f\n   addi t2, t0, -65\n   li t3, 25\n   bgtu t2, t3, 12f\n   ori t0, t0, 32\n"
        "12: addi t2, a1, 1\n   bgeu t2, a0, 13f\n   lbu t1, 0(a1)\n   li t3, 45\n   bne t1, t3, 13f\n"
        "addi a1, a1, 1\n   lbu t1, 0(a1)\n   li t3, 92\n   bne t1, t3, 34f\n   addi t2, a1, 1\n   bgeu t2, a0, 34f\n   mv a1, t2\n   lbu t1, 0(a1)\n"
        "34: addi a1, a1, 1\n   andi t2, a5, 1\n   beqz t2, 35f\n   addi t3, t1, -65\n   li t5, 25\n   bgtu t3, t5, 35f\n   ori t1, t1, 32\n"
        "35: bltu t4, t0, 8b\n   bgtu t4, t1, 8b\n   ori a2, a2, 2\n   j 8b\n"
        "13: bne t4, t0, 8b\n   ori a2, a2, 2\n   j 8b\n"
        "14: srli t0, a2, 1\n   xor t0, t0, a2\n   andi t0, t0, 1\n   beqz t0, 20f\n   addi a3, a0, 1\n   addi a4, a4, 1\n   j 1b\n"
        "15: beqz t5, 20f\n   li t0, 92\n   bne t5, t0, 16f\n   lbu t1, 1(a3)\n   beqz t1, 17f\n   addi a3, a3, 1\n   mv t5, t1\n   j 17f\n"
        "16: li t0, 63\n   bne t5, t0, 17f\n   andi t1, a5, 2\n   beqz t1, 18f\n   li t1, 128\n   bgeu t4, t1, 32f\n   j 18f\n"
        "17: beq t5, t4, 18f\n   andi t0, a5, 1\n   beqz t0, 20f\n   addi t1, t5, -65\n   li t2, 25\n   bgtu t1, t2, 36f\n   ori t5, t5, 32\n"
        "36: addi t1, t4, -65\n   bgtu t1, t2, 37f\n   ori t4, t4, 32\n"
        "37: bne t5, t4, 20f\n"
        "18: addi a3, a3, 1\n   addi a4, a4, 1\n   j 1b\n"
        "20: beqz a6, 30f\n   andi t0, a5, 2\n   beqz t0, 21f\n   lbu t1, 0(a7)\n   li t2, 128\n   bgeu t1, t2, 32f\n"
        "21: mv a3, a6\n   addi a7, a7, 1\n   mv a4, a7\n   beqz t6, 1b\n"
        "22: lbu t0, 0(a4)\n   beqz t0, 30f\n   beq t0, t6, 23f\n   addi a4, a4, 1\n   j 22b\n"
        "23: mv a7, a4\n   j 1b\n"
        "25: lbu t5, 0(a3)\n   li t0, 42\n   bne t5, t0, 26f\n   addi a3, a3, 1\n   j 25b\n"
        "26: seqz a0, t5\n" SH_GLOB_ASM_RET
        "30: li a0, 0\n" SH_GLOB_ASM_RET
        "31: li a0, 1\n" SH_GLOB_ASM_RET
        "32: li a0, -1\n" SH_GLOB_ASM_RET
        SH_GLOB_ASM_END(sh_glob_match)
);

#endif

#endif /* SH_SHELL_GLOB_IMPLEMENTATION */
