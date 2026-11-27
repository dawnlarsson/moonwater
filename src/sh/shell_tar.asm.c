/*
        POSIX / GNU / Moonwater-compatible tar header primitives in pure assembly:
          - sh_tar_header_sum       : 512-byte unsigned header checksum (SWAR 64-bit, [148..155] as spaces)
          - sh_tar_header_sum_match : unsigned or GNU signed-char 512-byte header checksum match
          - sh_tar_base256          : GNU unsigned base-256 field decoder with overflow check
          - sh_tar_field_moment     : signed timestamp decoder (octal or signed GNU base-256)
          - sh_tar_octal_eight      : SWAR 8-byte octal digit decoder
          - sh_tar_field_value      : tar numeric field decoder (base-256, 8/12-byte SWAR fast paths, general)
          - sh_tar_field_put_octal  : tar octal field encoder (7/8/12-byte SWAR fast paths, general)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_TAR_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_TAR_ASM_H
#define MOONWATER_SH_SHELL_TAR_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  sh_tar_u8;
typedef p32 sh_tar_u32;
typedef b32 sh_tar_i32;
typedef p64 sh_tar_u64;
typedef b64 sh_tar_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
      defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  sh_tar_u8;
typedef __UINT32_TYPE__ sh_tar_u32;
typedef __INT32_TYPE__  sh_tar_i32;
typedef __UINT64_TYPE__ sh_tar_u64;
typedef __INT64_TYPE__  sh_tar_i64;
#else
#include <stdint.h>
typedef uint8_t  sh_tar_u8;
typedef uint32_t sh_tar_u32;
typedef int32_t  sh_tar_i32;
typedef uint64_t sh_tar_u64;
typedef int64_t  sh_tar_i64;
#endif

sh_tar_u32 sh_tar_header_sum(const sh_tar_u8 *block);
sh_tar_i32 sh_tar_header_sum_match(const sh_tar_u8 *block, sh_tar_u64 stored, sh_tar_u64 expect);
sh_tar_i32 sh_tar_base256(const sh_tar_u8 *field, sh_tar_u64 width, sh_tar_u64 *value);
sh_tar_i32 sh_tar_field_moment(const sh_tar_u8 *field, sh_tar_u64 width, sh_tar_i64 *value);
sh_tar_i32 sh_tar_octal_eight(sh_tar_u64 word, sh_tar_u64 *value);
sh_tar_i32 sh_tar_field_value(const sh_tar_u8 *field, sh_tar_u64 width, sh_tar_u64 *value);
void       sh_tar_field_put_octal(sh_tar_u8 *field, sh_tar_u64 width, sh_tar_u64 value);

#endif /* MOONWATER_SH_SHELL_TAR_ASM_H */

#ifdef SH_SHELL_TAR_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_TAR_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_TAR_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_TAR_RISCV64 1
# else
#  error "src/sh/shell_tar.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_TAR_ARM64 1
# elif defined(X64) && X64
#  define SH_TAR_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_TAR_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_TAR_ASM_SECTION   ASM_SECTION
# define SH_TAR_ASM_FUNC(n)   ASM_FUNC(n)
# define SH_TAR_ASM_END(n)    ASM_END(n)
# define SH_TAR_ASM_RET       ASM_RET
#else
# define SH_TAR_ASM_SECTION   ".text\n"
# define SH_TAR_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define SH_TAR_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_TAR_ASM_END(n)   ""
# elif defined(_WIN32)
#  define SH_TAR_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_TAR_ASM_END(n)   ""
# else
#  if defined(SH_TAR_ARM64)
#   define SH_TAR_ASM_FTYPE   "%function"
#  else
#   define SH_TAR_ASM_FTYPE   "@function"
#  endif
#  define SH_TAR_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_TAR_ASM_FTYPE "\n" #n ":\n"
#  define SH_TAR_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define SH_TAR_SYM(n) "_" #n
#else
# define SH_TAR_SYM(n) #n
#endif

#if defined(SH_TAR_ARM64)

__asm__(
        SH_TAR_ASM_SECTION

        /* sh_tar_header_sum(x0=block) -> w0 unsigned 512B checksum with [148..155] as spaces */
        SH_TAR_ASM_FUNC(sh_tar_header_sum)
        "cbz x0, 2f\n   mov x1, #0x00ff00ff00ff00ff\n   mov x2, #0\n   mov x3, #0\n"
        "mov x4, x0\n   add x5, x0, #512\n"
        "1:  ldp x6, x7, [x4], #16\n   and x8, x6, x1\n   add x2, x2, x8\n   and x8, x1, x6, lsr #8\n   add x3, x3, x8\n"
        "and x8, x7, x1\n   add x2, x2, x8\n   and x8, x1, x7, lsr #8\n   add x3, x3, x8\n   cmp x4, x5\n   b.lo 1b\n"
        "add x2, x2, x3\n   add x2, x2, x2, lsr #32\n   and x3, x2, #0xffff\n   ubfx x2, x2, #16, #16\n   add x2, x2, x3\n"
        "ldr x6, [x0, #148]\n   and x8, x6, x1\n   and x7, x1, x6, lsr #8\n   add x8, x8, x7\n"
        "add x8, x8, x8, lsr #32\n   and x7, x8, #0xffff\n   ubfx x8, x8, #16, #16\n   add x8, x8, x7\n"
        "sub x2, x2, x8\n   add w0, w2, #256\n" SH_TAR_ASM_RET
        "2:  mov w0, #0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_header_sum)

        /* sh_tar_header_sum_match(x0=block, x1=stored, x2=expect) -> w0 (1 match, 0 no) */
        SH_TAR_ASM_FUNC(sh_tar_header_sum_match)
        "cbz x0, 3f\n   cmp x1, x2\n   b.eq 2f\n   mov x3, #0x0101010101010101\n"
        "mov x4, #0\n   mov x5, x0\n   add x6, x0, #512\n"
        "1:  ldp x7, x8, [x5], #16\n   and x9, x3, x7, lsr #7\n   add x4, x4, x9\n"
        "and x9, x3, x8, lsr #7\n   add x4, x4, x9\n   cmp x5, x6\n   b.lo 1b\n"
        "mov x9, #0x00ff00ff00ff00ff\n   and x7, x4, x9\n   and x8, x9, x4, lsr #8\n   add x4, x7, x8\n"
        "add x4, x4, x4, lsr #32\n   and x7, x4, #0xffff\n   ubfx x4, x4, #16, #16\n   add x4, x4, x7\n"
        "ldr x7, [x0, #148]\n   and x7, x3, x7, lsr #7\n   mul x7, x7, x3\n   sub x4, x4, x7, lsr #56\n"
        "sub x2, x2, x4, lsl #8\n   cmp x1, x2\n   cset w0, eq\n" SH_TAR_ASM_RET
        "2:  mov w0, #1\n" SH_TAR_ASM_RET
        "3:  mov w0, #0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_header_sum_match)

        /* sh_tar_base256(x0=field, x1=width, x2=value) -> w0 (1 ok, 0 err) */
        SH_TAR_ASM_FUNC(sh_tar_base256)
        "cbz x0, 3f\n   cbz x1, 3f\n   cbz x2, 3f\n   ldrb w3, [x0]\n   tbnz w3, #6, 3f\n"
        "and x3, x3, #0x7f\n   mov x4, #1\n"
        "1:  cmp x4, x1\n   b.hs 2f\n   tst x3, #0xff00000000000000\n   b.ne 3f\n"
        "ldrb w5, [x0, x4]\n   orr x3, x5, x3, lsl #8\n   add x4, x4, #1\n   b 1b\n"
        "2:  str x3, [x2]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "3:  mov w0, #0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_base256)

        /* sh_tar_octal_eight(x0=word, x1=value) -> w0 (1 ok, 0 err) */
        SH_TAR_ASM_FUNC(sh_tar_octal_eight)
        "cbz x1, 1f\n   eor x0, x0, #0x3030303030303030\n   tst x0, #0xf8f8f8f8f8f8f8f8\n   b.ne 1f\n"
        "lsr x2, x0, #8\n   add x0, x2, x0, lsl #3\n   and x0, x0, #0x00ff00ff00ff00ff\n"
        "lsr x2, x0, #16\n   add x0, x2, x0, lsl #6\n   and x0, x0, #0x0000ffff0000ffff\n"
        "lsr x2, x0, #32\n   add x0, x2, x0, lsl #12\n   and x0, x0, #0xffffff\n"
        "str x0, [x1]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "1:  mov w0, #0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_octal_eight)

        /* sh_tar_field_value(x0=field, x1=width, x2=value) -> w0 (1 ok, 0 err) */
        SH_TAR_ASM_FUNC(sh_tar_field_value)
        "cbz x0, 11f\n   cbz x2, 11f\n   cbz x1, 10f\n   ldrb w3, [x0]\n   tbnz w3, #7, 1f\n"
        "cmp x1, #8\n   b.eq 2f\n   cmp x1, #12\n   b.ne 4f\n"
        "ldrb w4, [x0, #11]\n   cbz w4, 3f\n   cmp w4, #32\n   b.ne 4f\n"
        "3:  sub w4, w3, #48\n   cmp w4, #7\n   b.hi 4f\n   ldrb w5, [x0, #1]\n   sub w5, w5, #48\n   cmp w5, #7\n   b.hi 4f\n"
        "ldrb w6, [x0, #2]\n   sub w6, w6, #48\n   cmp w6, #7\n   b.hi 4f\n"
        "ldr x7, [x0, #3]\n   eor x7, x7, #0x3030303030303030\n   orr x4, x6, x4, lsl #6\n   orr x4, x4, x5, lsl #3\n   b 12f\n"
        "1:  b " SH_TAR_SYM(sh_tar_base256) "\n"
        "2:  ldr x7, [x0]\n   lsr x4, x7, #56\n   cbz x4, 15f\n   cmp x4, #32\n   b.ne 4f\n"
        "15: eor x7, x7, #0x3030303030303030\n   lsl x7, x7, #8\n   mov x4, #0\n"
        "12: tst x7, #0xf8f8f8f8f8f8f8f8\n   b.ne 4f\n"
        "lsr x8, x7, #8\n   add x7, x8, x7, lsl #3\n   and x7, x7, #0x00ff00ff00ff00ff\n"
        "lsr x8, x7, #16\n   add x7, x8, x7, lsl #6\n   and x7, x7, #0x0000ffff0000ffff\n"
        "lsr x8, x7, #32\n   add x7, x8, x7, lsl #12\n   and x7, x7, #0xffffff\n"
        "orr x7, x7, x4, lsl #24\n   str x7, [x2]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "4:  cmp x1, #32\n   b.hs 11f\n   cbz w3, 10f\n   mov x4, #0\n"
        "5:  cmp x4, x1\n   b.hs 10f\n   ldrb w3, [x0, x4]\n   cmp w3, #32\n   b.ne 6f\n   add x4, x4, #1\n   b 5b\n"
        "6:  cbz w3, 10f\n   mov x5, #0\n   mov w6, #0\n"
        "7:  sub w7, w3, #48\n   cmp w7, #7\n   b.hi 8f\n   tst x5, #0xe000000000000000\n   b.ne 11f\n"
        "orr x5, x7, x5, lsl #3\n   mov w6, #1\n   add x4, x4, #1\n   cmp x4, x1\n   b.hs 9f\n   ldrb w3, [x0, x4]\n   b 7b\n"
        "8:  cbz w6, 11f\n"
        "13: cmp x4, x1\n   b.hs 9f\n   ldrb w3, [x0, x4]\n   cbz w3, 14f\n   cmp w3, #32\n   b.ne 11f\n"
        "14: add x4, x4, #1\n   b 13b\n"
        "9:  str x5, [x2]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "10: str xzr, [x2]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "11: mov w0, #0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_value)

        /* sh_tar_field_moment(x0=field, x1=width, x2=value) -> w0 (1 ok, 0 err) */
        SH_TAR_ASM_FUNC(sh_tar_field_moment)
        "cbz x0, 5f\n   cbz x2, 5f\n   cbz x1, 1f\n   ldrb w3, [x0]\n   tbz w3, #7, 1f\n"
        "sbfx x3, x3, #0, #7\n   mov x4, #1\n"
        "2:  cmp x4, x1\n   b.hs 3f\n   lsl x5, x3, #8\n   cmp x3, x5, asr #8\n   b.ne 5f\n"
        "ldrb w6, [x0, x4]\n   orr x3, x5, x6\n   add x4, x4, #1\n   b 2b\n"
        "3:  str x3, [x2]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "1:  stp x29, x30, [sp, #-32]!\n   str x2, [sp, #16]\n   add x2, sp, #24\n"
        "bl " SH_TAR_SYM(sh_tar_field_value) "\n   ldr x2, [sp, #16]\n   ldr x3, [sp, #24]\n   ldp x29, x30, [sp], #32\n"
        "cbz w0, 5f\n   tbnz x3, #63, 5f\n   str x3, [x2]\n   mov w0, #1\n" SH_TAR_ASM_RET
        "5:  mov w0, #0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_moment)

        /* sh_tar_field_put_octal(x0=field, x1=width, x2=value) -> void */
        SH_TAR_ASM_FUNC(sh_tar_field_put_octal)
        "cbz x0, 9f\n   cbz x1, 9f\n   cmp x1, #8\n   b.eq 1f\n   cmp x1, #7\n   b.eq 2f\n   cmp x1, #12\n   b.eq 3f\n   b 5f\n"
        "1:  lsr x3, x2, #21\n   cbnz x3, 5f\n   and x3, x2, #0xffffff\n   orr x3, x3, x3, lsl #20\n   and x3, x3, #0x00000fff00000fff\n"
        "orr x3, x3, x3, lsl #10\n   and x3, x3, #0x003f003f003f003f\n   orr x3, x3, x3, lsl #5\n   and x3, x3, #0x0707070707070707\n"
        "rev x3, x3\n   orr x3, x3, #0x3030303030303030\n   lsr x3, x3, #8\n   str x3, [x0]\n" SH_TAR_ASM_RET
        "2:  lsr x3, x2, #18\n   cbnz x3, 5f\n   and x3, x2, #0xffffff\n   orr x3, x3, x3, lsl #20\n   and x3, x3, #0x00000fff00000fff\n"
        "orr x3, x3, x3, lsl #10\n   and x3, x3, #0x003f003f003f003f\n   orr x3, x3, x3, lsl #5\n   and x3, x3, #0x0707070707070707\n"
        "rev x3, x3\n   orr x3, x3, #0x3030303030303030\n   lsr x3, x3, #16\n"
        "str w3, [x0]\n   lsr x4, x3, #32\n   strh w4, [x0, #4]\n   strb wzr, [x0, #6]\n" SH_TAR_ASM_RET
        "3:  lsr x3, x2, #33\n   cbnz x3, 5f\n   and x3, x2, #0xffffff\n   orr x3, x3, x3, lsl #20\n   and x3, x3, #0x00000fff00000fff\n"
        "orr x3, x3, x3, lsl #10\n   and x3, x3, #0x003f003f003f003f\n   orr x3, x3, x3, lsl #5\n   and x3, x3, #0x0707070707070707\n"
        "rev x3, x3\n   orr x3, x3, #0x3030303030303030\n   lsr x4, x2, #24\n"
        "ubfx x5, x4, #6, #3\n   add w5, w5, #48\n   strb w5, [x0]\n"
        "ubfx x5, x4, #3, #3\n   add w5, w5, #48\n   strb w5, [x0, #1]\n"
        "and w5, w4, #7\n   add w5, w5, #48\n   strb w5, [x0, #2]\n"
        "str x3, [x0, #3]\n   strb wzr, [x0, #11]\n" SH_TAR_ASM_RET
        "5:  sub x1, x1, #1\n   strb wzr, [x0, x1]\n   cbz x1, 9f\n   mov x3, x2\n   mov x4, #1\n"
        "6:  lsr x3, x3, #3\n   cbz x3, 7f\n   add x4, x4, #1\n   b 6b\n"
        "7:  cmp x4, x1\n   b.ls 8f\n   sub x4, x4, x1\n   add x4, x4, x4, lsl #1\n   lsr x2, x2, x4\n"
        "8:  sub x1, x1, #1\n   and w3, w2, #7\n   add w3, w3, #48\n   strb w3, [x0, x1]\n   lsr x2, x2, #3\n   cbnz x1, 8b\n"
        "9:  " SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_put_octal)
);

#elif defined(SH_TAR_X64)

#if defined(_WIN32)
# define SH_T_LD1      "mov r8, rcx\n"
# define SH_T_LD2      "mov r8, rcx\n   mov r9, rdx\n"
# define SH_T_LD3      "mov r10, rcx\n   mov r11, rdx\n   mov r9, r8\n"
# define SH_T_A1       "rcx"
# define SH_T_A2       "rdx"
# define SH_T_A3       "r8"
#else
# define SH_T_LD1      "mov r8, rdi\n"
# define SH_T_LD2      "mov r8, rdi\n   mov r9, rsi\n"
# define SH_T_LD3      "mov r10, rdi\n   mov r11, rsi\n   mov r9, rdx\n"
# define SH_T_A1       "rdi"
# define SH_T_A2       "rsi"
# define SH_T_A3       "rdx"
#endif

__asm__(
        SH_TAR_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* sh_tar_header_sum(block) -> eax */
        SH_TAR_ASM_FUNC(sh_tar_header_sum)
        SH_T_LD1
        "test r8, r8\n   jz 2f\n   mov r9, 0x00ff00ff00ff00ff\n   xor eax, eax\n   xor edx, edx\n   xor ecx, ecx\n"
        "1:  mov r10, [r8 + rcx]\n   mov r11, r10\n   and r10, r9\n   add rax, r10\n"
        "shr r11, 8\n   and r11, r9\n   add rdx, r11\n   add rcx, 8\n   cmp rcx, 512\n   jb 1b\n"
        "add rax, rdx\n   mov rdx, rax\n   shr rdx, 32\n   add rax, rdx\n"
        "movzx edx, ax\n   shr rax, 16\n   movzx eax, ax\n   add eax, edx\n"
        "mov r10, [r8 + 148]\n   mov r11, r10\n   and r10, r9\n   shr r11, 8\n   and r11, r9\n   add r10, r11\n"
        "mov r11, r10\n   shr r11, 32\n   add r10, r11\n   movzx ecx, r10w\n   shr r10, 16\n   movzx r10d, r10w\n"
        "add ecx, r10d\n   sub eax, ecx\n   add eax, 256\n" SH_TAR_ASM_RET
        "2:  xor eax, eax\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_header_sum)

        /* sh_tar_header_sum_match(block, stored, expect) -> eax */
        SH_TAR_ASM_FUNC(sh_tar_header_sum_match)
        SH_T_LD3
        "test r10, r10\n   jz 3f\n   cmp r11, r9\n   je 2f\n"
        "mov r8, 0x0101010101010101\n   xor eax, eax\n   xor ecx, ecx\n"
        "1:  mov rdx, [r10 + rcx]\n   shr rdx, 7\n   and rdx, r8\n   add rax, rdx\n   add rcx, 8\n   cmp rcx, 512\n   jb 1b\n"
        "mov rcx, 0x00ff00ff00ff00ff\n   mov rdx, rax\n   and rax, rcx\n   shr rdx, 8\n   and rdx, rcx\n   add rax, rdx\n"
        "mov rdx, rax\n   shr rdx, 32\n   add rax, rdx\n   movzx edx, ax\n   shr rax, 16\n   movzx eax, ax\n   add rax, rdx\n"
        "mov rdx, [r10 + 148]\n   shr rdx, 7\n   and rdx, r8\n   imul rdx, r8\n   shr rdx, 56\n   sub rax, rdx\n"
        "shl rax, 8\n   sub r9, rax\n   xor eax, eax\n   cmp r11, r9\n   sete al\n" SH_TAR_ASM_RET
        "2:  mov eax, 1\n" SH_TAR_ASM_RET
        "3:  xor eax, eax\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_header_sum_match)

        /* sh_tar_base256(field, width, value) -> eax */
        SH_TAR_ASM_FUNC(sh_tar_base256)
        SH_T_LD3
        "test r10, r10\n   jz 3f\n   test r11, r11\n   jz 3f\n   test r9, r9\n   jz 3f\n"
        "movzx eax, byte ptr [r10]\n   test eax, 0x40\n   jnz 3f\n   and eax, 0x7f\n   mov ecx, 1\n"
        "1:  cmp rcx, r11\n   jae 2f\n   mov rdx, rax\n   shr rdx, 56\n   jnz 3f\n"
        "shl rax, 8\n   movzx edx, byte ptr [r10 + rcx]\n   or rax, rdx\n   inc rcx\n   jmp 1b\n"
        "2:  mov [r9], rax\n   mov eax, 1\n" SH_TAR_ASM_RET
        "3:  xor eax, eax\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_base256)

        /* sh_tar_octal_eight(word, value) -> eax */
        SH_TAR_ASM_FUNC(sh_tar_octal_eight)
        SH_T_LD2
        "test r9, r9\n   jz 1f\n   mov rax, 0x3030303030303030\n   xor rax, r8\n"
        "mov rcx, 0xf8f8f8f8f8f8f8f8\n   test rax, rcx\n   jnz 1f\n"
        "mov rcx, 0x0707070707070707\n   and r8, rcx\n   mov rax, r8\n   shr rax, 8\n   lea r8, [rax + r8*8]\n"
        "mov rcx, 0x00ff00ff00ff00ff\n   and r8, rcx\n   mov rax, r8\n   shr rax, 16\n   shl r8, 6\n   add r8, rax\n"
        "mov rcx, 0x0000ffff0000ffff\n   and r8, rcx\n   mov rax, r8\n   shr rax, 32\n   shl r8, 12\n   add r8, rax\n"
        "and r8d, 0xffffff\n   mov [r9], r8\n   mov eax, 1\n" SH_TAR_ASM_RET
        "1:  xor eax, eax\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_octal_eight)

        /* sh_tar_field_value(field, width, value) -> eax */
        SH_TAR_ASM_FUNC(sh_tar_field_value)
        "test " SH_T_A1 ", " SH_T_A1 "\n   jz 11f\n   test " SH_T_A3 ", " SH_T_A3 "\n   jz 11f\n"
        "test " SH_T_A2 ", " SH_T_A2 "\n   jz 10f\n   movzx eax, byte ptr [" SH_T_A1 "]\n   test eax, 0x80\n   jnz 1f\n"
        SH_T_LD3
        "cmp r11, 8\n   je 2f\n   cmp r11, 12\n   jne 4f\n"
        "movzx ecx, byte ptr [r10 + 11]\n   test ecx, ecx\n   jz 3f\n   cmp ecx, 32\n   jne 4f\n"
        "3:  lea ecx, [rax - 48]\n   cmp ecx, 7\n   ja 4f\n   movzx edx, byte ptr [r10 + 1]\n   sub edx, 48\n   cmp edx, 7\n   ja 4f\n"
        "shl ecx, 3\n   or ecx, edx\n   movzx edx, byte ptr [r10 + 2]\n   sub edx, 48\n   cmp edx, 7\n   ja 4f\n"
        "shl ecx, 3\n   or ecx, edx\n   mov r8, [r10 + 3]\n   mov rdx, 0x3030303030303030\n   xor rdx, r8\n"
        "push rcx\n   mov rcx, 0xf8f8f8f8f8f8f8f8\n   test rdx, rcx\n   pop rcx\n   jnz 4f\n"
        "mov rdx, 0x0707070707070707\n   and r8, rdx\n   mov rdx, r8\n   shr rdx, 8\n   lea r8, [rdx + r8*8]\n"
        "mov rdx, 0x00ff00ff00ff00ff\n   and r8, rdx\n   mov rdx, r8\n   shr rdx, 16\n   shl r8, 6\n   add r8, rdx\n"
        "mov rdx, 0x0000ffff0000ffff\n   and r8, rdx\n   mov rdx, r8\n   shr rdx, 32\n   shl r8, 12\n   add r8, rdx\n"
        "and r8d, 0xffffff\n   shl rcx, 24\n   or r8, rcx\n   mov [r9], r8\n   mov eax, 1\n" SH_TAR_ASM_RET
        "1:  jmp " SH_TAR_SYM(sh_tar_base256) "\n"
        "2:  mov r8, [r10]\n   mov rdx, r8\n   shr rdx, 56\n   test edx, edx\n   jz 12f\n   cmp edx, 32\n   jne 4f\n"
        "12: mov rdx, 0x00ffffffffffffff\n   and r8, rdx\n   mov rdx, 0x3000000000000000\n   or r8, rdx\n"
        "mov rdx, 0x3030303030303030\n   xor rdx, r8\n   mov rcx, 0xf8f8f8f8f8f8f8f8\n   test rdx, rcx\n   jnz 4f\n"
        "mov rdx, 0x0707070707070707\n   and r8, rdx\n   mov rdx, r8\n   shr rdx, 8\n   lea r8, [rdx + r8*8]\n"
        "mov rdx, 0x00ff00ff00ff00ff\n   and r8, rdx\n   mov rdx, r8\n   shr rdx, 16\n   shl r8, 6\n   add r8, rdx\n"
        "mov rdx, 0x0000ffff0000ffff\n   and r8, rdx\n   mov rdx, r8\n   shr rdx, 32\n   shl r8, 12\n   add r8, rdx\n"
        "and r8d, 0xffffff\n   shr r8, 3\n   mov [r9], r8\n   mov eax, 1\n" SH_TAR_ASM_RET
        "4:  cmp r11, 32\n   jae 11f\n   test eax, eax\n   jz 15f\n   xor ecx, ecx\n"
        "5:  cmp rcx, r11\n   jae 15f\n   movzx eax, byte ptr [r10 + rcx]\n   cmp eax, 32\n   jne 6f\n   inc rcx\n   jmp 5b\n"
        "6:  test eax, eax\n   jz 15f\n   xor r8d, r8d\n   xor edx, edx\n"
        "7:  sub eax, 48\n   cmp eax, 7\n   ja 8f\n   mov rdx, r8\n   shr rdx, 61\n   jnz 11f\n"
        "shl r8, 3\n   or r8, rax\n   mov edx, 1\n   inc rcx\n   cmp rcx, r11\n   jae 9f\n   movzx eax, byte ptr [r10 + rcx]\n   jmp 7b\n"
        "8:  test edx, edx\n   jz 11f\n"
        "13: cmp rcx, r11\n   jae 9f\n   movzx eax, byte ptr [r10 + rcx]\n   test eax, eax\n   jz 14f\n   cmp eax, 32\n   jne 11f\n"
        "14: inc rcx\n   jmp 13b\n"
        "9:  mov [r9], r8\n   mov eax, 1\n" SH_TAR_ASM_RET
        "15: mov qword ptr [r9], 0\n   mov eax, 1\n" SH_TAR_ASM_RET
        "10: mov qword ptr [" SH_T_A3 "], 0\n   mov eax, 1\n" SH_TAR_ASM_RET
        "11: xor eax, eax\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_value)

        /* sh_tar_field_moment(field, width, value) -> eax */
        SH_TAR_ASM_FUNC(sh_tar_field_moment)
        "test " SH_T_A1 ", " SH_T_A1 "\n   jz 5f\n   test " SH_T_A3 ", " SH_T_A3 "\n   jz 5f\n"
        "test " SH_T_A2 ", " SH_T_A2 "\n   jz 1f\n   movzx eax, byte ptr [" SH_T_A1 "]\n   test eax, 0x80\n   jz 1f\n"
        SH_T_LD3
        "shl rax, 57\n   sar rax, 57\n   mov ecx, 1\n"
        "2:  cmp rcx, r11\n   jae 3f\n   mov rdx, rax\n   shl rdx, 8\n   mov r8, rdx\n   sar r8, 8\n   cmp r8, rax\n   jne 5f\n"
        "movzx eax, byte ptr [r10 + rcx]\n   or rax, rdx\n   inc rcx\n   jmp 2b\n"
        "3:  mov [r9], rax\n   mov eax, 1\n" SH_TAR_ASM_RET
        "1:  push rbx\n   sub rsp, 48\n   mov rbx, " SH_T_A3 "\n   lea " SH_T_A3 ", [rsp + 32]\n"
        "call " SH_TAR_SYM(sh_tar_field_value) "\n   test eax, eax\n   jz 4f\n"
        "mov rdx, [rsp + 32]\n   test rdx, rdx\n   js 4f\n   mov [rbx], rdx\n   mov eax, 1\n   add rsp, 48\n   pop rbx\n" SH_TAR_ASM_RET
        "4:  xor eax, eax\n   add rsp, 48\n   pop rbx\n" SH_TAR_ASM_RET
        "5:  xor eax, eax\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_moment)

        /* sh_tar_field_put_octal(field, width, value) -> void */
        SH_TAR_ASM_FUNC(sh_tar_field_put_octal)
        SH_T_LD3
        "test r10, r10\n   jz 9f\n   test r11, r11\n   jz 9f\n"
        "cmp r11, 8\n   je 1f\n   cmp r11, 7\n   je 2f\n   cmp r11, 12\n   je 3f\n   jmp 5f\n"
        "1:  mov rax, r9\n   shr rax, 21\n   jnz 5f\n   mov eax, r9d\n   and eax, 0xffffff\n"
        "mov rdx, rax\n   shl rdx, 20\n   or rax, rdx\n   mov rcx, 0x00000fff00000fff\n   and rax, rcx\n"
        "mov rdx, rax\n   shl rdx, 10\n   or rax, rdx\n   mov rcx, 0x003f003f003f003f\n   and rax, rcx\n"
        "mov rdx, rax\n   shl rdx, 5\n   or rax, rdx\n   mov rcx, 0x0707070707070707\n   and rax, rcx\n"
        "bswap rax\n   mov rcx, 0x3030303030303030\n   or rax, rcx\n   shr rax, 8\n   mov [r10], rax\n" SH_TAR_ASM_RET
        "2:  mov rax, r9\n   shr rax, 18\n   jnz 5f\n   mov eax, r9d\n   and eax, 0xffffff\n"
        "mov rdx, rax\n   shl rdx, 20\n   or rax, rdx\n   mov rcx, 0x00000fff00000fff\n   and rax, rcx\n"
        "mov rdx, rax\n   shl rdx, 10\n   or rax, rdx\n   mov rcx, 0x003f003f003f003f\n   and rax, rcx\n"
        "mov rdx, rax\n   shl rdx, 5\n   or rax, rdx\n   mov rcx, 0x0707070707070707\n   and rax, rcx\n"
        "bswap rax\n   mov rcx, 0x3030303030303030\n   or rax, rcx\n   shr rax, 16\n"
        "mov [r10], eax\n   shr rax, 32\n   mov [r10 + 4], ax\n   mov byte ptr [r10 + 6], 0\n" SH_TAR_ASM_RET
        "3:  mov rax, r9\n   shr rax, 33\n   jnz 5f\n   mov eax, r9d\n   and eax, 0xffffff\n"
        "mov rdx, rax\n   shl rdx, 20\n   or rax, rdx\n   mov rcx, 0x00000fff00000fff\n   and rax, rcx\n"
        "mov rdx, rax\n   shl rdx, 10\n   or rax, rdx\n   mov rcx, 0x003f003f003f003f\n   and rax, rcx\n"
        "mov rdx, rax\n   shl rdx, 5\n   or rax, rdx\n   mov rcx, 0x0707070707070707\n   and rax, rcx\n"
        "bswap rax\n   mov rcx, 0x3030303030303030\n   or rax, rcx\n   mov rdx, r9\n   shr rdx, 24\n"
        "mov ecx, edx\n   shr ecx, 6\n   and ecx, 7\n   add ecx, 48\n   mov [r10], cl\n"
        "mov ecx, edx\n   shr ecx, 3\n   and ecx, 7\n   add ecx, 48\n   mov [r10 + 1], cl\n"
        "and edx, 7\n   add edx, 48\n   mov [r10 + 2], dl\n"
        "mov [r10 + 3], rax\n   mov byte ptr [r10 + 11], 0\n" SH_TAR_ASM_RET
        "5:  dec r11\n   mov byte ptr [r10 + r11], 0\n   test r11, r11\n   jz 9f\n   mov rax, r9\n   mov ecx, 1\n"
        "6:  shr rax, 3\n   jz 7f\n   inc rcx\n   jmp 6b\n"
        "7:  cmp rcx, r11\n   jbe 8f\n   sub rcx, r11\n   lea rcx, [rcx + rcx*2]\n   shr r9, cl\n"
        "8:  dec r11\n   mov eax, r9d\n   and eax, 7\n   add eax, 48\n   mov [r10 + r11], al\n   shr r9, 3\n   test r11, r11\n   jnz 8b\n"
        "9:  " SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_put_octal)

        ".att_syntax prefix\n"
);

#elif defined(SH_TAR_RISCV64)

__asm__(
        SH_TAR_ASM_SECTION

        /* sh_tar_header_sum(a0=block) -> a0 */
        SH_TAR_ASM_FUNC(sh_tar_header_sum)
        "beqz a0, 2f\n   li t0, 0x00ff00ff00ff00ff\n   li t1, 0\n   li t2, 0\n   mv t3, a0\n   addi t4, a0, 512\n"
        "1:  ld t5, 0(t3)\n   and t6, t5, t0\n   add t1, t1, t6\n   srli t5, t5, 8\n   and t5, t5, t0\n"
        "add t2, t2, t5\n   addi t3, t3, 8\n   bltu t3, t4, 1b\n"
        "add t1, t1, t2\n   srli t2, t1, 32\n   add t1, t1, t2\n   li t6, 0xffff\n"
        "and t2, t1, t6\n   srli t1, t1, 16\n   and t1, t1, t6\n   add t1, t1, t2\n"
        "ld t5, 148(a0)\n   and t2, t5, t0\n   srli t5, t5, 8\n   and t5, t5, t0\n   add t2, t2, t5\n"
        "srli t5, t2, 32\n   add t2, t2, t5\n   and t5, t2, t6\n   srli t2, t2, 16\n   and t2, t2, t6\n"
        "add t2, t2, t5\n   sub t1, t1, t2\n   addiw a0, t1, 256\n   slli a0, a0, 32\n   srli a0, a0, 32\n" SH_TAR_ASM_RET
        "2:  li a0, 0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_header_sum)

        /* sh_tar_header_sum_match(a0=block, a1=stored, a2=expect) -> a0 */
        SH_TAR_ASM_FUNC(sh_tar_header_sum_match)
        "beqz a0, 3f\n   beq a1, a2, 2f\n   li t0, 0x0101010101010101\n   li t1, 0\n   mv t2, a0\n   addi t3, a0, 512\n"
        "1:  ld t4, 0(t2)\n   srli t4, t4, 7\n   and t4, t4, t0\n   add t1, t1, t4\n   addi t2, t2, 8\n   bltu t2, t3, 1b\n"
        "li t5, 0x00ff00ff00ff00ff\n   and t4, t1, t5\n   srli t1, t1, 8\n   and t1, t1, t5\n   add t1, t1, t4\n"
        "srli t4, t1, 32\n   add t1, t1, t4\n   li t6, 0xffff\n   and t4, t1, t6\n   srli t1, t1, 16\n   and t1, t1, t6\n   add t1, t1, t4\n"
        "ld t4, 148(a0)\n   srli t4, t4, 7\n   and t4, t4, t0\n   mul t4, t4, t0\n   srli t4, t4, 56\n   sub t1, t1, t4\n"
        "slli t1, t1, 8\n   sub a2, a2, t1\n   sub a1, a1, a2\n   seqz a0, a1\n" SH_TAR_ASM_RET
        "2:  li a0, 1\n" SH_TAR_ASM_RET
        "3:  li a0, 0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_header_sum_match)

        /* sh_tar_base256(a0=field, a1=width, a2=value) -> a0 */
        SH_TAR_ASM_FUNC(sh_tar_base256)
        "beqz a0, 3f\n   beqz a1, 3f\n   beqz a2, 3f\n   lbu t0, 0(a0)\n   andi t1, t0, 0x40\n   bnez t1, 3f\n"
        "andi t0, t0, 0x7f\n   li t2, 1\n"
        "1:  bgeu t2, a1, 2f\n   srli t3, t0, 56\n   bnez t3, 3f\n"
        "add t4, a0, t2\n   lbu t4, 0(t4)\n   slli t0, t0, 8\n   or t0, t0, t4\n   addi t2, t2, 1\n   j 1b\n"
        "2:  sd t0, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "3:  li a0, 0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_base256)

        /* sh_tar_octal_eight(a0=word, a1=value) -> a0 */
        SH_TAR_ASM_FUNC(sh_tar_octal_eight)
        "beqz a1, 1f\n   li t0, 0x3030303030303030\n   xor t1, a0, t0\n   li t2, 0xf8f8f8f8f8f8f8f8\n   and t1, t1, t2\n   bnez t1, 1f\n"
        "li t0, 0x0707070707070707\n   and a0, a0, t0\n   srli t1, a0, 8\n   slli a0, a0, 3\n   add a0, a0, t1\n"
        "li t0, 0x00ff00ff00ff00ff\n   and a0, a0, t0\n   srli t1, a0, 16\n   slli a0, a0, 6\n   add a0, a0, t1\n"
        "li t0, 0x0000ffff0000ffff\n   and a0, a0, t0\n   srli t1, a0, 32\n   slli a0, a0, 12\n   add a0, a0, t1\n"
        "li t0, 0xffffff\n   and a0, a0, t0\n   sd a0, 0(a1)\n   li a0, 1\n" SH_TAR_ASM_RET
        "1:  li a0, 0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_octal_eight)

        /* sh_tar_field_value(a0=field, a1=width, a2=value) -> a0 */
        SH_TAR_ASM_FUNC(sh_tar_field_value)
        "beqz a0, 11f\n   beqz a2, 11f\n   beqz a1, 10f\n   lbu t0, 0(a0)\n   andi t1, t0, 0x80\n   bnez t1, 1f\n"
        "li t2, 8\n   beq a1, t2, 2f\n   li t2, 12\n   bne a1, t2, 4f\n"
        "lbu t1, 11(a0)\n   beqz t1, 3f\n   li t2, 32\n   bne t1, t2, 4f\n"
        "3:  addi t3, t0, -48\n   li t2, 7\n   bgtu t3, t2, 4f\n   lbu t4, 1(a0)\n   addi t4, t4, -48\n   bgtu t4, t2, 4f\n"
        "slli t3, t3, 3\n   or t3, t3, t4\n   lbu t4, 2(a0)\n   addi t4, t4, -48\n   bgtu t4, t2, 4f\n"
        "slli t3, t3, 3\n   or t3, t3, t4\n   ld t5, 3(a0)\n"
        "li t1, 0x3030303030303030\n   xor t4, t5, t1\n   li t1, 0xf8f8f8f8f8f8f8f8\n   and t4, t4, t1\n   bnez t4, 4f\n"
        "li t1, 0x0707070707070707\n   and t5, t5, t1\n   srli t4, t5, 8\n   slli t5, t5, 3\n   add t5, t5, t4\n"
        "li t1, 0x00ff00ff00ff00ff\n   and t5, t5, t1\n   srli t4, t5, 16\n   slli t5, t5, 6\n   add t5, t5, t4\n"
        "li t1, 0x0000ffff0000ffff\n   and t5, t5, t1\n   srli t4, t5, 32\n   slli t5, t5, 12\n   add t5, t5, t4\n"
        "li t1, 0xffffff\n   and t5, t5, t1\n   slli t3, t3, 24\n   or t5, t5, t3\n   sd t5, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "1:  tail " SH_TAR_SYM(sh_tar_base256) "\n"
        "2:  ld t5, 0(a0)\n   srli t1, t5, 56\n   beqz t1, 12f\n   li t2, 32\n   bne t1, t2, 4f\n"
        "12: slli t5, t5, 8\n   srli t5, t5, 8\n   li t1, 0x3000000000000000\n   or t5, t5, t1\n"
        "li t1, 0x3030303030303030\n   xor t4, t5, t1\n   li t1, 0xf8f8f8f8f8f8f8f8\n   and t4, t4, t1\n   bnez t4, 4f\n"
        "li t1, 0x0707070707070707\n   and t5, t5, t1\n   srli t4, t5, 8\n   slli t5, t5, 3\n   add t5, t5, t4\n"
        "li t1, 0x00ff00ff00ff00ff\n   and t5, t5, t1\n   srli t4, t5, 16\n   slli t5, t5, 6\n   add t5, t5, t4\n"
        "li t1, 0x0000ffff0000ffff\n   and t5, t5, t1\n   srli t4, t5, 32\n   slli t5, t5, 12\n   add t5, t5, t4\n"
        "li t1, 0xffffff\n   and t5, t5, t1\n   srli t5, t5, 3\n   sd t5, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "4:  li t1, 32\n   bgeu a1, t1, 11f\n   beqz t0, 10f\n   li t2, 0\n"
        "5:  bgeu t2, a1, 10f\n   add t3, a0, t2\n   lbu t0, 0(t3)\n   bne t0, t1, 6f\n   addi t2, t2, 1\n   j 5b\n"
        "6:  beqz t0, 10f\n   li t4, 0\n   li t5, 0\n   li t6, 7\n"
        "7:  addi t3, t0, -48\n   bgtu t3, t6, 8f\n   srli a3, t4, 61\n   bnez a3, 11f\n"
        "slli t4, t4, 3\n   or t4, t4, t3\n   li t5, 1\n   addi t2, t2, 1\n   bgeu t2, a1, 9f\n   add t3, a0, t2\n   lbu t0, 0(t3)\n   j 7b\n"
        "8:  beqz t5, 11f\n"
        "13: bgeu t2, a1, 9f\n   add t3, a0, t2\n   lbu t0, 0(t3)\n   beqz t0, 14f\n   bne t0, t1, 11f\n"
        "14: addi t2, t2, 1\n   j 13b\n"
        "9:  sd t4, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "10: sd zero, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "11: li a0, 0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_value)

        /* sh_tar_field_moment(a0=field, a1=width, a2=value) -> a0 */
        SH_TAR_ASM_FUNC(sh_tar_field_moment)
        "beqz a0, 5f\n   beqz a2, 5f\n   beqz a1, 1f\n   lbu t0, 0(a0)\n   andi t1, t0, 0x80\n   beqz t1, 1f\n"
        "slli t0, t0, 57\n   srai t0, t0, 57\n   li t2, 1\n"
        "2:  bgeu t2, a1, 3f\n   slli t3, t0, 8\n   srai t4, t3, 8\n   bne t4, t0, 5f\n"
        "add t5, a0, t2\n   lbu t5, 0(t5)\n   or t0, t3, t5\n   addi t2, t2, 1\n   j 2b\n"
        "3:  sd t0, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "1:  addi sp, sp, -32\n   sd ra, 24(sp)\n   sd a2, 16(sp)\n   addi a2, sp, 8\n"
        "call " SH_TAR_SYM(sh_tar_field_value) "\n   ld ra, 24(sp)\n   ld a2, 16(sp)\n   ld t0, 8(sp)\n   addi sp, sp, 32\n"
        "beqz a0, 5f\n   bltz t0, 5f\n   sd t0, 0(a2)\n   li a0, 1\n" SH_TAR_ASM_RET
        "5:  li a0, 0\n" SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_moment)

        /* sh_tar_field_put_octal(a0=field, a1=width, a2=value) -> void */
        SH_TAR_ASM_FUNC(sh_tar_field_put_octal)
        "beqz a0, 9f\n   beqz a1, 9f\n   addi a1, a1, -1\n   add t0, a0, a1\n   sb zero, 0(t0)\n   beqz a1, 9f\n"
        "mv t1, a2\n   li t2, 1\n"
        "6:  srli t1, t1, 3\n   beqz t1, 7f\n   addi t2, t2, 1\n   j 6b\n"
        "7:  bleu t2, a1, 8f\n   sub t2, t2, a1\n   slli t3, t2, 1\n   add t2, t2, t3\n   srl a2, a2, t2\n"
        "8:  addi a1, a1, -1\n   andi t1, a2, 7\n   addi t1, t1, 48\n   add t0, a0, a1\n   sb t1, 0(t0)\n"
        "srli a2, a2, 3\n   bnez a1, 8b\n"
        "9:  " SH_TAR_ASM_RET
        SH_TAR_ASM_END(sh_tar_field_put_octal)
);

#endif

#endif /* SH_SHELL_TAR_ASM_IMPLEMENTATION */
