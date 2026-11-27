/*
        Constant-time AES-128 & SIV/authenticator primitives in pure assembly:
          - aes_core_sbox_byte     : constant-time GF(2^8) normal-basis AES S-box
          - aes_core_inv_sbox_byte : constant-time GF(2^8) normal-basis AES InvS-box
          - aes_core_128_expand    : constant-time AES-128 key schedule (16 -> 176 bytes)
          - aes_core_128_decrypt   : constant-time AES-128 decrypt (SWAR InvMixColumns)
          - aes_core_siv_double    : constant-time GF(2^128) doubling (0x87 reduction)
          - aes_core_ct_equal      : 64-bit word-wide constant-time byte comparison

        Pure baseline arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and
        riscv64 (RV64GC) assembly. Zero C function bodies, zero .data/.rodata
        lookup tables, zero secret-dependent branches, zero allocations, zero syscalls.

        STB-style: include for declarations; define NET_AES_CORE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_NET_AES_CORE_ASM_H
#define MOONWATER_NET_AES_CORE_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  ac_u8;
typedef p16 ac_u16;
typedef p32 ac_u32;
typedef b32 ac_i32;
typedef p64 ac_u64;
typedef b64 ac_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
    defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  ac_u8;
typedef __UINT16_TYPE__ ac_u16;
typedef __UINT32_TYPE__ ac_u32;
typedef __INT32_TYPE__  ac_i32;
typedef __UINT64_TYPE__ ac_u64;
typedef __INT64_TYPE__  ac_i64;
#else
#include <stdint.h>
typedef uint8_t  ac_u8;
typedef uint16_t ac_u16;
typedef uint32_t ac_u32;
typedef int32_t  ac_i32;
typedef uint64_t ac_u64;
typedef int64_t  ac_i64;
#endif

ac_u8  aes_core_sbox_byte(ac_u8 x);
ac_u8  aes_core_inv_sbox_byte(ac_u8 x);
void   aes_core_128_expand(const ac_u8 *key, ac_u8 *round_keys);
void   aes_core_128_decrypt(const ac_u8 *round_keys, const ac_u8 *in, ac_u8 *out);
void   aes_core_siv_double(ac_u8 *block);
ac_i32 aes_core_ct_equal(const ac_u8 *a, const ac_u8 *b, ac_u64 len);

#endif /* MOONWATER_NET_AES_CORE_ASM_H */

#ifdef NET_AES_CORE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define AC_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define AC_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define AC_RISCV64 1
# else
#  error "src/net/aes_core.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define AC_ARM64 1
# elif defined(X64) && X64
#  define AC_X64 1
# elif defined(RISCV64) && RISCV64
#  define AC_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define AC_ASM_SECTION   ASM_SECTION
# define AC_ASM_FUNC(n)   ASM_FUNC(n)
# define AC_ASM_END(n)    ASM_END(n)
# define AC_ASM_RET       ASM_RET
#else
# define AC_ASM_SECTION   ".text\n"
# define AC_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define AC_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define AC_ASM_END(n)   ""
# elif defined(_WIN32)
#  define AC_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define AC_ASM_END(n)   ""
# else
#  if defined(AC_ARM64)
#   define AC_ASM_FTYPE   "%function"
#  else
#   define AC_ASM_FTYPE   "@function"
#  endif
#  define AC_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " AC_ASM_FTYPE "\n" #n ":\n"
#  define AC_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define AC_LOCAL(n)   "L_" #n
# define AC_LOC_DEF(n) ".text\n.balign 16\nL_" #n ":\n"
# define AC_SYM(n)     "_" #n
#else
# define AC_LOCAL(n)   ".L_" #n
# define AC_LOC_DEF(n) ".text\n.balign 16\n.L_" #n ":\n"
# define AC_SYM(n)     #n
#endif

#if defined(AC_ARM64)

__asm__(
        AC_ASM_SECTION

        /* _ac_mat8: w0 = 8x8 GF(2) matrix-vector product (w0=x, x1=M_u64); clobbers x1..x4 */
        AC_LOC_DEF(ac_mat8)
        "mov w2, #0\n   mov w3, #8\n"
        "1:  and w4, w0, #1\n   neg w4, w4\n   and w4, w4, w1\n   eor w2, w2, w4\n"
        "lsr w0, w0, #1\n   lsr x1, x1, #8\n   subs w3, w3, #1\n   b.ne 1b\n"
        "and w0, w2, #0xff\n" AC_ASM_RET

        /* _ac_gf4_mul: w0 = gf4_mul(w0=a, w1=b) using register constants w8=GF2_MUL, w9=GF2_MULN */
        AC_LOC_DEF(ac_gf4_mul)
        "eor w2, w0, w0, lsr #2\n   and w2, w2, #3\n   eor w3, w1, w1, lsr #2\n   and w3, w3, #3\n"
        "orr w2, w2, w3, lsl #2\n   lsl w2, w2, #1\n   lsr w2, w9, w2\n   and w2, w2, #3\n"
        "and w3, w0, #3\n   and w4, w1, #3\n   orr w3, w3, w4, lsl #2\n   lsl w3, w3, #1\n   lsr w3, w8, w3\n   and w3, w3, #3\n"
        "lsr w4, w0, #2\n   lsr w5, w1, #2\n   orr w4, w4, w5, lsl #2\n   lsl w4, w4, #1\n   lsr w4, w8, w4\n   and w4, w4, #3\n"
        "eor w3, w3, w2\n   eor w4, w4, w2\n   orr w0, w3, w4, lsl #2\n" AC_ASM_RET

        /* _ac_gf_inv_byte: w0 = Canright register-SWAR GF(2^8) inverse of byte w0; zero stack spills */
        AC_LOC_DEF(ac_gf_inv_byte)
        "stp x29, x30, [sp, #-16]!\n"
        "movz x1, #0xcfff\n   movk x1, #0x4206, lsl #16\n   movk x1, #0xa4a6, lsl #32\n   movk x1, #0x13d1, lsl #48\n"
        "bl " AC_LOCAL(ac_mat8) "\n"
        "and w10, w0, #0xf\n   lsr w11, w0, #4\n"
        "movz w8, #0x7800\n   movk w8, #0xe49c, lsl #16\n   movz w9, #0x9c00\n   movk w9, #0x78e4, lsl #16\n"
        "mov w0, w10\n   mov w1, w11\n   bl " AC_LOCAL(ac_gf4_mul) "\n"
        "movz x1, #0x9e70\n   movk x1, #0xda34, lsl #16\n   movk x1, #0x52bc, lsl #32\n   movk x1, #0x16f8, lsl #48\n"
        "eor w2, w10, w11\n   lsl w2, w2, #2\n   lsr x1, x1, x2\n   eor w0, w0, w1\n   and w0, w0, #0xf\n"
        "movz x1, #0x8c40\n   movk x1, #0xdea1, lsl #16\n   movk x1, #0x95b3, lsl #32\n   movk x1, #0xf672, lsl #48\n"
        "lsl w0, w0, #2\n   lsr x12, x1, x0\n   and w12, w12, #0xf\n"
        "mov w0, w11\n   mov w1, w12\n   bl " AC_LOCAL(ac_gf4_mul) "\n   mov w13, w0\n"
        "mov w0, w10\n   mov w1, w12\n   bl " AC_LOCAL(ac_gf4_mul) "\n   orr w0, w13, w0, lsl #4\n"
        "movz x1, #0x306f\n   movk x1, #0x9934, lsl #16\n   movk x1, #0xdcdf, lsl #32\n   movk x1, #0xc838, lsl #48\n"
        "bl " AC_LOCAL(ac_mat8) "\n   ldp x29, x30, [sp], #16\n" AC_ASM_RET

        /* _ac_invmix_col: SWAR InvMixColumns on 32-bit column w0; clobbers w1..w3 */
        AC_LOC_DEF(ac_invmix_col)
        "mov w3, #0x1b\n   eor w1, w0, w0, ror #16\n"
        "and w2, w1, #0x7f7f7f7f\n   and w1, w1, #0x80808080\n   lsr w1, w1, #7\n   mul w1, w1, w3\n   eor w1, w1, w2, lsl #1\n"
        "and w2, w1, #0x7f7f7f7f\n   and w1, w1, #0x80808080\n   lsr w1, w1, #7\n   mul w1, w1, w3\n   eor w1, w1, w2, lsl #1\n"
        "eor w0, w0, w1\n   eor w1, w0, w0, ror #8\n"
        "and w2, w1, #0x7f7f7f7f\n   and w1, w1, #0x80808080\n   lsr w1, w1, #7\n   mul w1, w1, w3\n   eor w1, w1, w2, lsl #1\n"
        "eor w1, w1, w0, ror #8\n   eor w1, w1, w0, ror #16\n   eor w0, w1, w0, ror #24\n" AC_ASM_RET

        /* aes_core_sbox_byte(w0=x) -> w0 */
        AC_ASM_FUNC(aes_core_sbox_byte)
        "stp x29, x30, [sp, #-16]!\n   bl " AC_LOCAL(ac_gf_inv_byte) "\n"
        "orr w1, w0, w0, lsl #8\n   eor w0, w0, w1, lsr #7\n   eor w0, w0, w1, lsr #6\n"
        "eor w0, w0, w1, lsr #5\n   eor w0, w0, w1, lsr #4\n   mov w2, #0x63\n   eor w0, w0, w2\n   and w0, w0, #0xff\n"
        "ldp x29, x30, [sp], #16\n" AC_ASM_RET
        AC_ASM_END(aes_core_sbox_byte)

        /* aes_core_inv_sbox_byte(w0=x) -> w0 */
        AC_ASM_FUNC(aes_core_inv_sbox_byte)
        "and w0, w0, #0xff\n   orr w1, w0, w0, lsl #8\n"
        "lsr w0, w1, #7\n   eor w0, w0, w1, lsr #5\n   eor w0, w0, w1, lsr #2\n   mov w2, #0x05\n   eor w0, w0, w2\n"
        "b " AC_LOCAL(ac_gf_inv_byte) "\n"
        AC_ASM_END(aes_core_inv_sbox_byte)

        /* aes_core_128_expand(x0=key, x1=round_keys) */
        AC_ASM_FUNC(aes_core_128_expand)
        "cbz x0, 9f\n   cbz x1, 9f\n"
        "stp x19, x20, [sp, #-96]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
        "stp x25, x26, [sp, #48]\n   stp x27, x28, [sp, #64]\n   stp x29, x30, [sp, #80]\n"
        "ldp w19, w20, [x0]\n   ldp w21, w22, [x0, #8]\n   stp w19, w20, [x1]\n   stp w21, w22, [x1, #8]\n"
        "add x23, x1, #16\n   mov w24, #1\n"
        "1:  ror w25, w22, #8\n   mov w26, #0\n   mov w27, #0\n"
        "2:  lsr w0, w25, w27\n   bl " AC_SYM(aes_core_sbox_byte) "\n"
        "lsl w0, w0, w27\n   orr w26, w26, w0\n   add w27, w27, #8\n   cmp w27, #32\n   b.lo 2b\n"
        "eor w26, w26, w24\n   eor w19, w19, w26\n   eor w20, w20, w19\n   eor w21, w21, w20\n   eor w22, w22, w21\n"
        "stp w19, w20, [x23]\n   stp w21, w22, [x23, #8]\n   add x23, x23, #16\n"
        "lsr w1, w24, #7\n   neg w1, w1\n   mov w2, #0x1b\n   and w1, w1, w2\n   eor w24, w1, w24, lsl #1\n   and w24, w24, #0xff\n"
        "cmp w24, #0x6c\n   b.ne 1b\n"
        "ldp x29, x30, [sp, #80]\n   ldp x27, x28, [sp, #64]\n   ldp x25, x26, [sp, #48]\n"
        "ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #96\n"
        "9:  " AC_ASM_RET
        AC_ASM_END(aes_core_128_expand)

        /* aes_core_128_decrypt(x0=round_keys, x1=in, x2=out) */
        AC_ASM_FUNC(aes_core_128_decrypt)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x2, 9f\n"
        "stp x19, x20, [sp, #-80]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n   stp x29, x30, [sp, #48]\n"
        "mov x19, x0\n   mov x20, x2\n   mov w21, #9\n"
        "ldp x8, x9, [x1]\n   ldp x10, x11, [x19, #160]\n   eor x8, x8, x10\n   eor x9, x9, x11\n   stp x8, x9, [sp, #64]\n"
        "1:  ldrb w8, [sp, #77]\n   ldrb w9, [sp, #73]\n   ldrb w10, [sp, #69]\n   ldrb w11, [sp, #65]\n"
        "strb w8, [sp, #65]\n   strb w11, [sp, #69]\n   strb w10, [sp, #73]\n   strb w9, [sp, #77]\n"
        "ldrb w8, [sp, #66]\n   ldrb w9, [sp, #74]\n   strb w9, [sp, #66]\n   strb w8, [sp, #74]\n"
        "ldrb w8, [sp, #70]\n   ldrb w9, [sp, #78]\n   strb w9, [sp, #70]\n   strb w8, [sp, #78]\n"
        "ldrb w8, [sp, #67]\n   ldrb w9, [sp, #71]\n   ldrb w10, [sp, #75]\n   ldrb w11, [sp, #79]\n"
        "strb w9, [sp, #67]\n   strb w10, [sp, #71]\n   strb w11, [sp, #75]\n   strb w8, [sp, #79]\n"
        "add x22, x19, x21, lsl #4\n   mov x23, #0\n"
        "2:  add x24, sp, #64\n   ldrb w0, [x24, x23]\n   bl " AC_SYM(aes_core_inv_sbox_byte) "\n"
        "ldrb w1, [x22, x23]\n   eor w0, w0, w1\n   strb w0, [x24, x23]\n   add x23, x23, #1\n   cmp x23, #16\n   b.lo 2b\n"
        "cbz w21, 4f\n   mov x23, #0\n"
        "3:  add x24, sp, #64\n   ldr w0, [x24, x23]\n   bl " AC_LOCAL(ac_invmix_col) "\n"
        "str w0, [x24, x23]\n   add x23, x23, #4\n   cmp x23, #16\n   b.lo 3b\n"
        "sub w21, w21, #1\n   b 1b\n"
        "4:  ldp x8, x9, [sp, #64]\n   stp x8, x9, [x20]\n   stp xzr, xzr, [sp, #64]\n"
        "ldp x29, x30, [sp, #48]\n   ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #80\n"
        "9:  " AC_ASM_RET
        AC_ASM_END(aes_core_128_decrypt)

        /* aes_core_siv_double(x0=block) */
        AC_ASM_FUNC(aes_core_siv_double)
        "cbz x0, 1f\n   ldp x1, x2, [x0]\n   rev x1, x1\n   rev x2, x2\n"
        "asr x3, x1, #63\n   mov w4, #0x87\n   and x3, x3, x4\n"
        "lsl x1, x1, #1\n   orr x1, x1, x2, lsr #63\n   eor x2, x3, x2, lsl #1\n"
        "rev x1, x1\n   rev x2, x2\n   stp x1, x2, [x0]\n"
        "1:  " AC_ASM_RET
        AC_ASM_END(aes_core_siv_double)

        /* aes_core_ct_equal(x0=a, x1=b, x2=len) -> w0 (1 if equal, 0 else) */
        AC_ASM_FUNC(aes_core_ct_equal)
        "cbz x2, 4f\n   cbz x0, 5f\n   cbz x1, 5f\n   mov x3, #0\n"
        "1:  cmp x2, #8\n   b.lo 2f\n   ldr x4, [x0], #8\n   ldr x5, [x1], #8\n"
        "eor x4, x4, x5\n   orr x3, x3, x4\n   sub x2, x2, #8\n   b 1b\n"
        "2:  cbz x2, 3f\n   ldrb w4, [x0], #1\n   ldrb w5, [x1], #1\n"
        "eor w4, w4, w5\n   orr x3, x3, x4\n   sub x2, x2, #1\n   b 2b\n"
        "3:  cmp x3, #0\n   cset w0, eq\n" AC_ASM_RET
        "4:  mov w0, #1\n" AC_ASM_RET
        "5:  mov w0, #0\n" AC_ASM_RET
        AC_ASM_END(aes_core_ct_equal)
);

#elif defined(AC_X64)

__asm__(
        AC_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* _ac_mat8: eax = 8x8 GF(2) matrix-vector product (eax=x, rdx=M_u64); clobbers ecx, edx, r8d */
        AC_LOC_DEF(ac_mat8)
        "xor r8d, r8d\n   mov ecx, 8\n"
        "1:  mov r9d, eax\n   and r9d, 1\n   neg r9d\n   and r9d, edx\n   xor r8d, r9d\n"
        "shr eax, 1\n   shr rdx, 8\n   dec ecx\n   jnz 1b\n   movzx eax, r8b\n" AC_ASM_RET

        /* _ac_gf4_mul: eax = gf4_mul(eax=a, edx=b); r10d=GF2_MUL, r11d=GF2_MULN; clobbers ecx, r8d, r9d */
        AC_LOC_DEF(ac_gf4_mul)
        "mov ecx, eax\n   shr ecx, 2\n   xor ecx, eax\n   and ecx, 3\n"
        "mov r8d, edx\n   shr r8d, 2\n   xor r8d, edx\n   and r8d, 3\n   shl r8d, 2\n   or ecx, r8d\n   add ecx, ecx\n"
        "mov r8d, r11d\n   shr r8d, cl\n   and r8d, 3\n"
        "mov ecx, edx\n   and ecx, 3\n   shl ecx, 2\n   mov r9d, eax\n   and r9d, 3\n   or ecx, r9d\n   add ecx, ecx\n"
        "mov r9d, r10d\n   shr r9d, cl\n   and r9d, 3\n   xor r9d, r8d\n"
        "mov ecx, edx\n   shr ecx, 2\n   shl ecx, 2\n   shr eax, 2\n   or ecx, eax\n   add ecx, ecx\n"
        "mov eax, r10d\n   shr eax, cl\n   and eax, 3\n   xor eax, r8d\n   shl eax, 2\n   or eax, r9d\n" AC_ASM_RET

        /* _ac_gf_inv_byte: eax = Canright register-SWAR GF(2^8) inverse of byte eax; preserves rbx,rbp,r12..r15,rdi,rsi */
        AC_LOC_DEF(ac_gf_inv_byte)
        "push rbx\n   push rbp\n"
        "mov rdx, 0x13d1a4a64206cfff\n   call " AC_LOCAL(ac_mat8) "\n"
        "mov ebx, eax\n   and ebx, 0xf\n   mov ebp, eax\n   shr ebp, 4\n"
        "mov r10d, 0xe49c7800\n   mov r11d, 0x78e49c00\n"
        "mov eax, ebx\n   mov edx, ebp\n   call " AC_LOCAL(ac_gf4_mul) "\n"
        "mov ecx, ebx\n   xor ecx, ebp\n   shl ecx, 2\n   mov rdx, 0x16f852bcda349e70\n   shr rdx, cl\n"
        "xor eax, edx\n   and eax, 0xf\n   lea ecx, [rax * 4]\n   mov rdx, 0xf67295b3dea18c40\n   shr rdx, cl\n   and edx, 0xf\n"
        "push rdx\n   mov eax, ebp\n   call " AC_LOCAL(ac_gf4_mul) "\n   mov ebp, eax\n"
        "pop rdx\n   mov eax, ebx\n   call " AC_LOCAL(ac_gf4_mul) "\n   shl eax, 4\n   or eax, ebp\n"
        "mov rdx, 0xc838dcdf9934306f\n   call " AC_LOCAL(ac_mat8) "\n   pop rbp\n   pop rbx\n" AC_ASM_RET

        /* _ac_invmix_col: SWAR InvMixColumns on 32-bit column eax; clobbers ecx, edx */
        AC_LOC_DEF(ac_invmix_col)
        "mov ecx, eax\n   ror ecx, 16\n   xor ecx, eax\n"
        "mov edx, ecx\n   and edx, 0x7f7f7f7f\n   add edx, edx\n   and ecx, 0x80808080\n   shr ecx, 7\n   imul ecx, ecx, 0x1b\n   xor ecx, edx\n"
        "mov edx, ecx\n   and edx, 0x7f7f7f7f\n   add edx, edx\n   and ecx, 0x80808080\n   shr ecx, 7\n   imul ecx, ecx, 0x1b\n   xor ecx, edx\n"
        "xor eax, ecx\n   mov ecx, eax\n   ror ecx, 8\n   xor ecx, eax\n"
        "mov edx, ecx\n   and edx, 0x7f7f7f7f\n   add edx, edx\n   and ecx, 0x80808080\n   shr ecx, 7\n   imul ecx, ecx, 0x1b\n   xor ecx, edx\n"
        "ror eax, 8\n   xor ecx, eax\n   ror eax, 8\n   xor ecx, eax\n   ror eax, 8\n   xor eax, ecx\n" AC_ASM_RET

        /* aes_core_sbox_byte(x) -> al */
        AC_ASM_FUNC(aes_core_sbox_byte)
#if defined(_WIN32)
        "movzx eax, cl\n"
#else
        "movzx eax, dil\n"
#endif
        "call " AC_LOCAL(ac_gf_inv_byte) "\n"
        "mov ecx, eax\n   rol cl, 1\n   xor eax, ecx\n   rol cl, 1\n   xor eax, ecx\n"
        "rol cl, 1\n   xor eax, ecx\n   rol cl, 1\n   xor eax, ecx\n   xor eax, 0x63\n   movzx eax, al\n" AC_ASM_RET
        AC_ASM_END(aes_core_sbox_byte)

        /* aes_core_inv_sbox_byte(x) -> al */
        AC_ASM_FUNC(aes_core_inv_sbox_byte)
#if defined(_WIN32)
        "movzx eax, cl\n"
#else
        "movzx eax, dil\n"
#endif
        "mov ecx, eax\n   rol al, 1\n   rol cl, 3\n   xor al, cl\n   rol cl, 3\n   xor al, cl\n   xor al, 0x05\n   movzx eax, al\n"
        "jmp " AC_LOCAL(ac_gf_inv_byte) "\n"
        AC_ASM_END(aes_core_inv_sbox_byte)

        /* aes_core_128_expand(key, round_keys) */
        AC_ASM_FUNC(aes_core_128_expand)
#if defined(_WIN32)
        "test rcx, rcx\n   jz 9f\n   test rdx, rdx\n   jz 9f\n"
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, rcx\n   mov r13, rdx\n"
#else
        "test rdi, rdi\n   jz 9f\n   test rsi, rsi\n   jz 9f\n"
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, rdi\n   mov r13, rsi\n"
#endif
        "mov r14d, [r12]\n   mov r15d, [r12 + 4]\n   mov ebx, [r12 + 8]\n   mov ebp, [r12 + 12]\n"
        "mov [r13], r14d\n   mov [r13 + 4], r15d\n   mov [r13 + 8], ebx\n   mov [r13 + 12], ebp\n"
        "add r13, 16\n   mov r12d, 1\n"
        "1:  mov eax, ebp\n   ror eax, 8\n   push rax\n   xor eax, eax\n   push rax\n   xor edx, edx\n"
        "2:  mov eax, [rsp + 8]\n   mov ecx, edx\n   shr eax, cl\n   push rdx\n"
#if defined(_WIN32)
        "movzx ecx, al\n"
#else
        "movzx edi, al\n"
#endif
        "call " AC_SYM(aes_core_sbox_byte) "\n   pop rdx\n   mov ecx, edx\n   shl eax, cl\n   or [rsp], eax\n"
        "add edx, 8\n   cmp edx, 32\n   jb 2b\n"
        "pop rax\n   add rsp, 8\n   xor eax, r12d\n"
        "xor r14d, eax\n   xor r15d, r14d\n   xor ebx, r15d\n   xor ebp, ebx\n"
        "mov [r13], r14d\n   mov [r13 + 4], r15d\n   mov [r13 + 8], ebx\n   mov [r13 + 12], ebp\n   add r13, 16\n"
        "mov eax, r12d\n   shr eax, 7\n   neg eax\n   and eax, 0x1b\n   shl r12d, 1\n   xor r12d, eax\n   and r12d, 0xff\n"
        "cmp r12d, 0x6c\n   jne 1b\n"
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
        "9:  " AC_ASM_RET
        AC_ASM_END(aes_core_128_expand)

        /* aes_core_128_decrypt(round_keys, in, out) */
        AC_ASM_FUNC(aes_core_128_decrypt)
#if defined(_WIN32)
        "test rcx, rcx\n   jz 9f\n   test rdx, rdx\n   jz 9f\n   test r8, r8\n   jz 9f\n"
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 16\n"
        "mov r12, rcx\n   mov r13, r8\n   mov rax, [rdx]\n   mov r9, [rdx + 8]\n"
#else
        "test rdi, rdi\n   jz 9f\n   test rsi, rsi\n   jz 9f\n   test rdx, rdx\n   jz 9f\n"
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 16\n"
        "mov r12, rdi\n   mov r13, rdx\n   mov rax, [rsi]\n   mov r9, [rsi + 8]\n"
#endif
        "xor rax, [r12 + 160]\n   xor r9, [r12 + 168]\n   mov [rsp], rax\n   mov [rsp + 8], r9\n   mov r14d, 9\n"
        "1:  mov al, [rsp + 13]\n   xchg al, [rsp + 1]\n   xchg al, [rsp + 5]\n   xchg al, [rsp + 9]\n   mov [rsp + 13], al\n"
        "mov al, [rsp + 2]\n   xchg al, [rsp + 10]\n   mov [rsp + 2], al\n   mov al, [rsp + 6]\n   xchg al, [rsp + 14]\n   mov [rsp + 6], al\n"
        "mov al, [rsp + 3]\n   xchg al, [rsp + 15]\n   xchg al, [rsp + 11]\n   xchg al, [rsp + 7]\n   mov [rsp + 3], al\n"
        "mov r15, r14\n   shl r15, 4\n   add r15, r12\n   xor ebx, ebx\n"
        "2:  movzx eax, byte ptr [rsp + rbx]\n"
#if defined(_WIN32)
        "mov ecx, eax\n"
#else
        "mov edi, eax\n"
#endif
        "call " AC_SYM(aes_core_inv_sbox_byte) "\n   xor al, [r15 + rbx]\n   mov [rsp + rbx], al\n"
        "inc ebx\n   cmp ebx, 16\n   jb 2b\n   test r14d, r14d\n   jz 4f\n   xor ebx, ebx\n"
        "3:  mov eax, [rsp + rbx]\n   call " AC_LOCAL(ac_invmix_col) "\n   mov [rsp + rbx], eax\n"
        "add ebx, 4\n   cmp ebx, 16\n   jb 3b\n   dec r14d\n   jmp 1b\n"
        "4:  mov rax, [rsp]\n   mov r9, [rsp + 8]\n   mov [r13], rax\n   mov [r13 + 8], r9\n"
        "xor eax, eax\n   mov [rsp], rax\n   mov [rsp + 8], rax\n   add rsp, 16\n"
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n"
        "9:  " AC_ASM_RET
        AC_ASM_END(aes_core_128_decrypt)

        /* aes_core_siv_double(block) */
        AC_ASM_FUNC(aes_core_siv_double)
#if defined(_WIN32)
        "test rcx, rcx\n   jz 1f\n   mov rax, [rcx]\n   mov rdx, [rcx + 8]\n"
        "bswap rax\n   bswap rdx\n   mov r8, rax\n   sar r8, 63\n   and r8d, 0x87\n"
        "shld rax, rdx, 1\n   shl rdx, 1\n   xor rdx, r8\n"
        "bswap rax\n   bswap rdx\n   mov [rcx], rax\n   mov [rcx + 8], rdx\n"
#else
        "test rdi, rdi\n   jz 1f\n   mov rax, [rdi]\n   mov rdx, [rdi + 8]\n"
        "bswap rax\n   bswap rdx\n   mov rcx, rax\n   sar rcx, 63\n   and ecx, 0x87\n"
        "shld rax, rdx, 1\n   shl rdx, 1\n   xor rdx, rcx\n"
        "bswap rax\n   bswap rdx\n   mov [rdi], rax\n   mov [rdi + 8], rdx\n"
#endif
        "1:  " AC_ASM_RET
        AC_ASM_END(aes_core_siv_double)

        /* aes_core_ct_equal(a, b, len) -> eax (1 if equal, 0 else) */
        AC_ASM_FUNC(aes_core_ct_equal)
#if defined(_WIN32)
        "test r8, r8\n   jz 4f\n   test rcx, rcx\n   jz 5f\n   test rdx, rdx\n   jz 5f\n   xor eax, eax\n"
        "1:  cmp r8, 8\n   jb 2f\n   mov r9, [rcx]\n   xor r9, [rdx]\n   or rax, r9\n"
        "add rcx, 8\n   add rdx, 8\n   sub r8, 8\n   jmp 1b\n"
        "2:  test r8, r8\n   jz 3f\n   movzx r9d, byte ptr [rcx]\n   movzx r10d, byte ptr [rdx]\n"
        "xor r9d, r10d\n   or rax, r9\n   inc rcx\n   inc rdx\n   dec r8\n   jmp 2b\n"
#else
        "test rdx, rdx\n   jz 4f\n   test rdi, rdi\n   jz 5f\n   test rsi, rsi\n   jz 5f\n   xor eax, eax\n"
        "1:  cmp rdx, 8\n   jb 2f\n   mov rcx, [rdi]\n   xor rcx, [rsi]\n   or rax, rcx\n"
        "add rdi, 8\n   add rsi, 8\n   sub rdx, 8\n   jmp 1b\n"
        "2:  test rdx, rdx\n   jz 3f\n   movzx ecx, byte ptr [rdi]\n   movzx r8d, byte ptr [rsi]\n"
        "xor ecx, r8d\n   or rax, rcx\n   inc rdi\n   inc rsi\n   dec rdx\n   jmp 2b\n"
#endif
        "3:  test rax, rax\n   sete al\n   movzx eax, al\n" AC_ASM_RET
        "4:  mov eax, 1\n" AC_ASM_RET
        "5:  xor eax, eax\n" AC_ASM_RET
        AC_ASM_END(aes_core_ct_equal)

        ".att_syntax prefix\n"
);

#elif defined(AC_RISCV64)

__asm__(
        AC_ASM_SECTION

        /* _ac_mat8: a0 = 8x8 GF(2) matrix-vector product (a0=x, a1=M_u64); clobbers a1, t0..t2 */
        AC_LOC_DEF(ac_mat8)
        "li t0, 0\n   li t1, 8\n"
        "1:  andi t2, a0, 1\n   neg t2, t2\n   and t2, t2, a1\n   xor t0, t0, t2\n"
        "srli a0, a0, 1\n   srli a1, a1, 8\n   addi t1, t1, -1\n   bnez t1, 1b\n   andi a0, t0, 0xff\n" AC_ASM_RET

        /* _ac_gf4_mul: a0 = gf4_mul(a0=a, a1=b); a5=GF2_MUL, a6=GF2_MULN; clobbers t0..t3 */
        AC_LOC_DEF(ac_gf4_mul)
        "srli t0, a0, 2\n   xor t0, t0, a0\n   andi t0, t0, 3\n   srli t1, a1, 2\n   xor t1, t1, a1\n   andi t1, t1, 3\n"
        "slli t1, t1, 2\n   or t0, t0, t1\n   slli t0, t0, 1\n   srl t0, a6, t0\n   andi t0, t0, 3\n"
        "andi t1, a0, 3\n   andi t2, a1, 3\n   slli t2, t2, 2\n   or t1, t1, t2\n   slli t1, t1, 1\n"
        "srl t1, a5, t1\n   andi t1, t1, 3\n   xor t1, t1, t0\n"
        "srli t2, a0, 2\n   srli t3, a1, 2\n   slli t3, t3, 2\n   or t2, t2, t3\n   slli t2, t2, 1\n"
        "srl t2, a5, t2\n   andi t2, t2, 3\n   xor t2, t2, t0\n   slli t2, t2, 2\n   or a0, t1, t2\n" AC_ASM_RET

        /* _ac_gf_inv_byte: a0 = Canright register-SWAR GF(2^8) inverse of byte a0; preserves s0..s11 */
        AC_LOC_DEF(ac_gf_inv_byte)
        "addi sp, sp, -16\n   sd ra, 0(sp)\n"
        "li a1, 0x13d1a4a64206cfff\n   call " AC_LOCAL(ac_mat8) "\n"
        "andi a2, a0, 0xf\n   srli a3, a0, 4\n   li a5, 0xe49c7800\n   li a6, 0x78e49c00\n"
        "mv a0, a2\n   mv a1, a3\n   call " AC_LOCAL(ac_gf4_mul) "\n"
        "xor t0, a2, a3\n   slli t0, t0, 2\n   li t1, 0x16f852bcda349e70\n   srl t1, t1, t0\n"
        "xor a0, a0, t1\n   andi a0, a0, 0xf\n   slli a0, a0, 2\n   li t1, 0xf67295b3dea18c40\n   srl a4, t1, a0\n   andi a4, a4, 0xf\n"
        "mv a0, a3\n   mv a1, a4\n   call " AC_LOCAL(ac_gf4_mul) "\n   mv a7, a0\n"
        "mv a0, a2\n   mv a1, a4\n   call " AC_LOCAL(ac_gf4_mul) "\n   slli a0, a0, 4\n   or a0, a0, a7\n"
        "li a1, 0xc838dcdf9934306f\n   call " AC_LOCAL(ac_mat8) "\n   ld ra, 0(sp)\n   addi sp, sp, 16\n" AC_ASM_RET

        /* _ac_invmix_col: SWAR InvMixColumns on 32-bit column a0; clobbers t0..t6 */
        AC_LOC_DEF(ac_invmix_col)
        "li t4, 0x7f7f7f7f\n   li t5, 0x80808080\n   li t6, 0x1b\n"
        "srliw t0, a0, 16\n   slliw t1, a0, 16\n   or t0, t0, t1\n   xor t0, t0, a0\n"
        "and t1, t0, t4\n   slliw t1, t1, 1\n   and t0, t0, t5\n   srliw t0, t0, 7\n   mulw t0, t0, t6\n   xor t0, t0, t1\n"
        "and t1, t0, t4\n   slliw t1, t1, 1\n   and t0, t0, t5\n   srliw t0, t0, 7\n   mulw t0, t0, t6\n   xor t0, t0, t1\n"
        "xor a0, a0, t0\n   srliw t2, a0, 8\n   slliw t3, a0, 24\n   or t2, t2, t3\n   xor t0, a0, t2\n"
        "and t1, t0, t4\n   slliw t1, t1, 1\n   and t0, t0, t5\n   srliw t0, t0, 7\n   mulw t0, t0, t6\n   xor t0, t0, t1\n"
        "xor t0, t0, t2\n   srliw t2, a0, 16\n   slliw t3, a0, 16\n   or t2, t2, t3\n   xor t0, t0, t2\n"
        "srliw t2, a0, 24\n   slliw t3, a0, 8\n   or t2, t2, t3\n   xor a0, t0, t2\n" AC_ASM_RET

        /* aes_core_sbox_byte(a0=x) -> a0 */
        AC_ASM_FUNC(aes_core_sbox_byte)
        "addi sp, sp, -16\n   sd ra, 0(sp)\n   call " AC_LOCAL(ac_gf_inv_byte) "\n"
        "slli t0, a0, 8\n   or t0, t0, a0\n   srli t1, t0, 7\n   xor a0, a0, t1\n"
        "srli t1, t0, 6\n   xor a0, a0, t1\n   srli t1, t0, 5\n   xor a0, a0, t1\n"
        "srli t1, t0, 4\n   xor a0, a0, t1\n   xori a0, a0, 0x63\n   andi a0, a0, 0xff\n"
        "ld ra, 0(sp)\n   addi sp, sp, 16\n" AC_ASM_RET
        AC_ASM_END(aes_core_sbox_byte)

        /* aes_core_inv_sbox_byte(a0=x) -> a0 */
        AC_ASM_FUNC(aes_core_inv_sbox_byte)
        "andi a0, a0, 0xff\n   slli t0, a0, 8\n   or t0, t0, a0\n"
        "srli a0, t0, 7\n   srli t1, t0, 5\n   xor a0, a0, t1\n   srli t1, t0, 2\n   xor a0, a0, t1\n   xori a0, a0, 0x05\n"
        "tail " AC_LOCAL(ac_gf_inv_byte) "\n"
        AC_ASM_END(aes_core_inv_sbox_byte)

        /* aes_core_128_expand(a0=key, a1=round_keys) */
        AC_ASM_FUNC(aes_core_128_expand)
        "beqz a0, 9f\n   beqz a1, 9f\n"
        "addi sp, sp, -48\n   sd ra, 0(sp)\n   sd s0, 8(sp)\n   sd s1, 16(sp)\n   sd s2, 24(sp)\n   sd s3, 32(sp)\n   sd s4, 40(sp)\n"
        "mv s0, a1\n   li t0, 16\n"
        "0:  lbu t1, 0(a0)\n   sb t1, 0(a1)\n   addi a0, a0, 1\n   addi a1, a1, 1\n   addi t0, t0, -1\n   bnez t0, 0b\n"
        "addi s0, s0, 16\n   li s1, 1\n"
        "1:  li s2, 0\n"
        "2:  addi t0, s2, 1\n   andi t0, t0, 3\n   add t0, s0, t0\n   lbu a0, -4(t0)\n"
        "call " AC_SYM(aes_core_sbox_byte) "\n   bnez s2, 3f\n   xor a0, a0, s1\n"
        "3:  add t0, s0, s2\n   lbu t1, -16(t0)\n   xor a0, a0, t1\n   sb a0, 0(t0)\n"
        "addi s2, s2, 1\n   li t1, 4\n   bltu s2, t1, 2b\n"
        "li s2, 4\n"
        "4:  add t0, s0, s2\n   lbu t1, -16(t0)\n   lbu t2, -4(t0)\n   xor t1, t1, t2\n   sb t1, 0(t0)\n"
        "addi s2, s2, 1\n   li t1, 16\n   bltu s2, t1, 4b\n"
        "addi s0, s0, 16\n   srli t0, s1, 7\n   neg t0, t0\n   andi t0, t0, 0x1b\n   slli s1, s1, 1\n   xor s1, s1, t0\n   andi s1, s1, 0xff\n"
        "li t1, 0x6c\n   bne s1, t1, 1b\n"
        "ld ra, 0(sp)\n   ld s0, 8(sp)\n   ld s1, 16(sp)\n   ld s2, 24(sp)\n   ld s3, 32(sp)\n   ld s4, 40(sp)\n   addi sp, sp, 48\n"
        "9:  " AC_ASM_RET
        AC_ASM_END(aes_core_128_expand)

        /* aes_core_128_decrypt(a0=round_keys, a1=in, a2=out) */
        AC_ASM_FUNC(aes_core_128_decrypt)
        "beqz a0, 9f\n   beqz a1, 9f\n   beqz a2, 9f\n"
        "addi sp, sp, -64\n   sd ra, 0(sp)\n   sd s0, 8(sp)\n   sd s1, 16(sp)\n   sd s2, 24(sp)\n   sd s3, 32(sp)\n   sd s4, 40(sp)\n"
        "mv s0, a0\n   mv s1, a2\n   li s2, 9\n   li t0, 0\n   li t3, 16\n"
        "0:  add t1, a1, t0\n   lbu t1, 0(t1)\n   add t2, s0, t0\n   lbu t2, 160(t2)\n   xor t1, t1, t2\n"
        "add t2, sp, t0\n   sb t1, 48(t2)\n   addi t0, t0, 1\n   bltu t0, t3, 0b\n"
        "1:  lbu t0, 61(sp)\n   lbu t1, 57(sp)\n   lbu t2, 53(sp)\n   lbu t3, 49(sp)\n"
        "sb t0, 49(sp)\n   sb t3, 53(sp)\n   sb t2, 57(sp)\n   sb t1, 61(sp)\n"
        "lbu t0, 50(sp)\n   lbu t1, 58(sp)\n   sb t1, 50(sp)\n   sb t0, 58(sp)\n"
        "lbu t0, 54(sp)\n   lbu t1, 62(sp)\n   sb t1, 54(sp)\n   sb t0, 62(sp)\n"
        "lbu t0, 51(sp)\n   lbu t1, 55(sp)\n   lbu t2, 59(sp)\n   lbu t3, 63(sp)\n"
        "sb t1, 51(sp)\n   sb t2, 55(sp)\n   sb t3, 59(sp)\n   sb t0, 63(sp)\n"
        "slli s3, s2, 4\n   add s3, s0, s3\n   li s4, 0\n"
        "2:  add t0, sp, s4\n   lbu a0, 48(t0)\n   call " AC_SYM(aes_core_inv_sbox_byte) "\n"
        "add t0, s3, s4\n   lbu t1, 0(t0)\n   xor a0, a0, t1\n   add t0, sp, s4\n   sb a0, 48(t0)\n"
        "addi s4, s4, 1\n   li t1, 16\n   bltu s4, t1, 2b\n   beqz s2, 4f\n   li s4, 0\n"
        "3:  add t0, sp, s4\n   lwu a0, 48(t0)\n   call " AC_LOCAL(ac_invmix_col) "\n"
        "add t0, sp, s4\n   sw a0, 48(t0)\n   addi s4, s4, 4\n   li t1, 16\n   bltu s4, t1, 3b\n"
        "addi s2, s2, -1\n   j 1b\n"
        "4:  li t0, 0\n   li t3, 16\n"
        "5:  add t1, sp, t0\n   lbu t2, 48(t1)\n   add t1, s1, t0\n   sb t2, 0(t1)\n   addi t0, t0, 1\n   bltu t0, t3, 5b\n"
        "sd zero, 48(sp)\n   sd zero, 56(sp)\n"
        "ld ra, 0(sp)\n   ld s0, 8(sp)\n   ld s1, 16(sp)\n   ld s2, 24(sp)\n   ld s3, 32(sp)\n   ld s4, 40(sp)\n   addi sp, sp, 64\n"
        "9:  " AC_ASM_RET
        AC_ASM_END(aes_core_128_decrypt)

        /* aes_core_siv_double(a0=block) */
        AC_ASM_FUNC(aes_core_siv_double)
        "beqz a0, 2f\n   lbu t0, 0(a0)\n   srli t0, t0, 7\n   neg t0, t0\n   andi t0, t0, 0x87\n"
        "addi t1, a0, 15\n   mv t2, zero\n"
        "1:  lbu t3, 0(t1)\n   srli t4, t3, 7\n   slli t3, t3, 1\n   or t3, t3, t2\n   xor t3, t3, t0\n"
        "sb t3, 0(t1)\n   mv t2, t4\n   mv t0, zero\n   addi t1, t1, -1\n   bgeu t1, a0, 1b\n"
        "2:  " AC_ASM_RET
        AC_ASM_END(aes_core_siv_double)

        /* aes_core_ct_equal(a0=a, a1=b, a2=len) -> a0 (1 if equal, 0 else) */
        AC_ASM_FUNC(aes_core_ct_equal)
        "beqz a2, 4f\n   beqz a0, 5f\n   beqz a1, 5f\n   li t0, 0\n"
        "or t1, a0, a1\n   andi t1, t1, 7\n   bnez t1, 2f\n   li t2, 8\n"
        "1:  bltu a2, t2, 2f\n   ld t3, 0(a0)\n   ld t4, 0(a1)\n   xor t3, t3, t4\n   or t0, t0, t3\n"
        "addi a0, a0, 8\n   addi a1, a1, 8\n   addi a2, a2, -8\n   j 1b\n"
        "2:  beqz a2, 3f\n   lbu t3, 0(a0)\n   lbu t4, 0(a1)\n   xor t3, t3, t4\n   or t0, t0, t3\n"
        "addi a0, a0, 1\n   addi a1, a1, 1\n   addi a2, a2, -1\n   j 2b\n"
        "3:  seqz a0, t0\n" AC_ASM_RET
        "4:  li a0, 1\n" AC_ASM_RET
        "5:  li a0, 0\n" AC_ASM_RET
        AC_ASM_END(aes_core_ct_equal)
);

#endif

#endif /* NET_AES_CORE_ASM_IMPLEMENTATION */
