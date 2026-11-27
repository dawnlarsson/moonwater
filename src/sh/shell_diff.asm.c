/*
        POSIX / Moonwater-compatible diff Myers O(ND) core primitives in pure assembly:
          - sh_diff_common_prefix_u32 : bounded 64-bit-paired leading equal i32 span
          - sh_diff_common_suffix_u32 : bounded 64-bit-paired trailing equal i32 span
          - sh_diff_meet              : Myers O(ND) bidirectional frontier & snake kernel
                                        with optional half-step budget limit

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_DIFF_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_DIFF_H
#define MOONWATER_SH_SHELL_DIFF_H

#if defined(STANDARD_MODERN_C)
typedef p8  sh_diff_u8;
typedef p16 sh_diff_u16;
typedef p32 sh_diff_u32;
typedef b32 sh_diff_i32;
typedef p64 sh_diff_u64;
typedef b64 sh_diff_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && \
      defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
      defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  sh_diff_u8;
typedef __UINT16_TYPE__ sh_diff_u16;
typedef __UINT32_TYPE__ sh_diff_u32;
typedef __INT32_TYPE__  sh_diff_i32;
typedef __UINT64_TYPE__ sh_diff_u64;
typedef __INT64_TYPE__  sh_diff_i64;
#else
#include <stdint.h>
typedef uint8_t  sh_diff_u8;
typedef uint16_t sh_diff_u16;
typedef uint32_t sh_diff_u32;
typedef int32_t  sh_diff_i32;
typedef uint64_t sh_diff_u64;
typedef int64_t  sh_diff_i64;
#endif

_Static_assert(sizeof(sh_diff_u8)  == 1, "sh_diff_u8 must be 1 byte");
_Static_assert(sizeof(sh_diff_u16) == 2, "sh_diff_u16 must be 2 bytes");
_Static_assert(sizeof(sh_diff_u32) == 4, "sh_diff_u32 must be 4 bytes");
_Static_assert(sizeof(sh_diff_i32) == 4, "sh_diff_i32 must be 4 bytes");
_Static_assert(sizeof(sh_diff_u64) == 8, "sh_diff_u64 must be 8 bytes");
_Static_assert(sizeof(sh_diff_i64) == 8, "sh_diff_i64 must be 8 bytes");

/* 96 bytes — ABI-locked parameter/result block for sh_diff_meet */
struct sh_diff_req {
        const sh_diff_i32 *xv;    /* +0  left kept sequence */
        const sh_diff_i32 *yv;    /* +8  right kept sequence */
        sh_diff_i32       *fwd;   /* +16 forward frontier base */
        sh_diff_i32       *bwd;   /* +24 backward frontier base */
        sh_diff_u64        shift; /* +32 diagonal zero offset (diff_middle) */
        sh_diff_u64        xoff;  /* +40 left range start */
        sh_diff_u64        xlim;  /* +48 left range end */
        sh_diff_u64        yoff;  /* +56 right range start */
        sh_diff_u64        ylim;  /* +64 right range end */
        sh_diff_u64        limit; /* +72 max half-steps (0 = unlimited) */
        sh_diff_u64        xmid;  /* +80 out: midpoint x */
        sh_diff_u64        ymid;  /* +88 out: midpoint y */
};

#ifdef offset_of
#define SH_DIFF_OFFSET(T, M) offset_of(T, M)
#else
#define SH_DIFF_OFFSET(T, M) ((sh_diff_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct sh_diff_req) == 96, "sh_diff_req must be 96 bytes");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, xv)    == 0,  "sh_diff_req.xv offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, yv)    == 8,  "sh_diff_req.yv offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, fwd)   == 16, "sh_diff_req.fwd offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, bwd)   == 24, "sh_diff_req.bwd offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, shift) == 32, "sh_diff_req.shift offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, xoff)  == 40, "sh_diff_req.xoff offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, xlim)  == 48, "sh_diff_req.xlim offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, yoff)  == 56, "sh_diff_req.yoff offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, ylim)  == 64, "sh_diff_req.ylim offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, limit) == 72, "sh_diff_req.limit offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, xmid)  == 80, "sh_diff_req.xmid offset");
_Static_assert(SH_DIFF_OFFSET(struct sh_diff_req, ymid)  == 88, "sh_diff_req.ymid offset");

sh_diff_u64 sh_diff_common_prefix_u32(const sh_diff_i32 *xv, const sh_diff_i32 *yv,
                                      sh_diff_u64 max_len);
sh_diff_u64 sh_diff_common_suffix_u32(const sh_diff_i32 *xv_end, const sh_diff_i32 *yv_end,
                                      sh_diff_u64 max_len);
sh_diff_i32 sh_diff_meet(struct sh_diff_req *req);

#endif /* MOONWATER_SH_SHELL_DIFF_H */

#ifdef SH_SHELL_DIFF_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_DIFF_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_DIFF_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_DIFF_RISCV64 1
# else
#  error "src/sh/shell_diff.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_DIFF_ARM64 1
# elif defined(X64) && X64
#  define SH_DIFF_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_DIFF_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_DIFF_SECTION  ASM_SECTION
# define SH_DIFF_FUNC(n)  ASM_FUNC(n)
# define SH_DIFF_END(n)   ASM_END(n)
# define SH_DIFF_RET      ASM_RET
#else
# define SH_DIFF_SECTION  ".text\n"
# define SH_DIFF_RET      "ret\n"
# if defined(__APPLE__)
#  define SH_DIFF_FUNC(n) ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_DIFF_END(n)  ""
# elif defined(_WIN32)
#  define SH_DIFF_FUNC(n) ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_DIFF_END(n)  ""
# else
#  if defined(SH_DIFF_ARM64)
#   define SH_DIFF_FTYPE  "%function"
#  else
#   define SH_DIFF_FTYPE  "@function"
#  endif
#  define SH_DIFF_FUNC(n) ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_DIFF_FTYPE "\n" #n ":\n"
#  define SH_DIFF_END(n)  ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SH_DIFF_ARM64)

__asm__(
        SH_DIFF_SECTION

        /* sh_diff_common_prefix_u32(x0=xv, x1=yv, x2=max_len) -> x0 */
        SH_DIFF_FUNC(sh_diff_common_prefix_u32)
        "cbz x0, 5f\n   cbz x1, 5f\n   mov x3, #0\n   b 2f\n"
        "1:  ldr x4, [x0], #8\n   ldr x5, [x1], #8\n   cmp x4, x5\n   b.ne 4f\n   add x3, x3, #2\n   sub x2, x2, #2\n"
        "2:  cmp x2, #2\n   b.hs 1b\n   cbz x2, 3f\n   ldr w4, [x0]\n   ldr w5, [x1]\n"
        "4:  cmp w4, w5\n   cinc x3, x3, eq\n"
        "3:  mov x0, x3\n" SH_DIFF_RET
        "5:  mov x0, #0\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_common_prefix_u32)

        /* sh_diff_common_suffix_u32(x0=xv_end, x1=yv_end, x2=max_len) -> x0 */
        SH_DIFF_FUNC(sh_diff_common_suffix_u32)
        "cbz x0, 5f\n   cbz x1, 5f\n   mov x3, #0\n   b 2f\n"
        "1:  ldr x4, [x0, #-8]!\n   ldr x5, [x1, #-8]!\n   cmp x4, x5\n   b.ne 4f\n   add x3, x3, #2\n   sub x2, x2, #2\n"
        "2:  cmp x2, #2\n   b.hs 1b\n   cbz x2, 3f\n   ldr w4, [x0, #-4]\n   ldr w5, [x1, #-4]\n   b 6f\n"
        "4:  lsr x4, x4, #32\n   lsr x5, x5, #32\n"
        "6:  cmp w4, w5\n   cinc x3, x3, eq\n"
        "3:  mov x0, x3\n" SH_DIFF_RET
        "5:  mov x0, #0\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_common_suffix_u32)

        /* sh_diff_meet(x0=req) -> w0 (1=found, 0=limit/invalid) */
        SH_DIFF_FUNC(sh_diff_meet)
        "cbz x0, 90f\n   stp x19, x20, [sp, #-80]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
        "stp x25, x26, [sp, #48]\n   stp x27, x28, [sp, #64]\n   mov x19, x0\n"
        "ldp x20, x21, [x19]\n   ldp x22, x23, [x19, #16]\n   ldp x1, x24, [x19, #32]\n"
        "ldp x25, x26, [x19, #48]\n   ldp x27, x28, [x19, #64]\n"
        "cbz x22, 80f\n   cbz x23, 80f\n   cmp x24, x25\n   b.hi 80f\n   cmp x26, x27\n   b.hi 80f\n"
        "cmp x24, x25\n   b.ne 1f\n   cmp x26, x27\n   b.ne 1f\n   stp x24, x26, [x19, #80]\n   b 81f\n"
        "1:  cmp x24, x25\n   b.eq 2f\n   cbz x20, 80f\n"
        "2:  cmp x26, x27\n   b.eq 3f\n   cbz x21, 80f\n"
        "3:  add x22, x22, x1, lsl #2\n   add x23, x23, x1, lsl #2\n"
        "sub x8, x24, x27\n   sub x9, x25, x26\n   sub x10, x24, x26\n   sub x12, x25, x27\n"
        "mov x11, x10\n   mov x13, x12\n   sub x14, x10, x12\n   and x14, x14, #1\n"
        "str w24, [x22, x10, lsl #2]\n   str w25, [x23, x12, lsl #2]\n"
        "cmp x28, #0\n   csinv x28, x28, xzr, ne\n   mov w16, #-1\n   mov w17, #0x7fffffff\n"
        /* forward pass */
        "10: cbz x28, 80f\n   sub x28, x28, #1\n   cmp x10, x8\n   b.le 11f\n"
        "sub x10, x10, #1\n   sub x0, x10, #1\n   str w16, [x22, x0, lsl #2]\n   b 12f\n"
        "11: add x10, x10, #1\n"
        "12: cmp x11, x9\n   b.ge 13f\n   add x11, x11, #1\n   add x0, x11, #1\n   str w16, [x22, x0, lsl #2]\n   b 14f\n"
        "13: sub x11, x11, #1\n"
        "14: mov x1, x11\n"
        "15: cmp x1, x10\n   b.lt 20f\n   add x0, x22, x1, lsl #2\n   ldrsw x2, [x0, #-4]\n   ldrsw x3, [x0, #4]\n"
        "add x4, x2, #1\n   cmp x2, x3\n   csel x2, x3, x4, lt\n   sub x3, x2, x1\n"
        "sub x4, x25, x2\n   sub x5, x27, x3\n   cmp x4, x5\n   csel x4, x4, x5, lt\n   cmp x4, #0\n   b.le 18f\n"
        "add x5, x20, x2, lsl #2\n   add x6, x21, x3, lsl #2\n   cmp x4, #2\n   b.lt 17f\n"
        "16: ldr x7, [x5], #8\n   ldr x15, [x6], #8\n   cmp x7, x15\n   b.ne 19f\n"
        "add x2, x2, #2\n   sub x4, x4, #2\n   cmp x4, #2\n   b.ge 16b\n"
        "17: cbz x4, 18f\n   ldr w7, [x5]\n   ldr w15, [x6]\n"
        "19: cmp w7, w15\n   cinc x2, x2, eq\n"
        "18: str w2, [x0]\n   cbz x14, 190f\n   cmp x1, x12\n   b.lt 190f\n   cmp x1, x13\n   b.gt 190f\n"
        "ldrsw x4, [x23, x1, lsl #2]\n   cmp x4, x2\n   b.le 70f\n"
        "190: sub x1, x1, #2\n   b 15b\n"
        /* backward pass */
        "20: cbz x28, 80f\n   sub x28, x28, #1\n   cmp x12, x8\n   b.le 21f\n"
        "sub x12, x12, #1\n   sub x0, x12, #1\n   str w17, [x23, x0, lsl #2]\n   b 22f\n"
        "21: add x12, x12, #1\n"
        "22: cmp x13, x9\n   b.ge 23f\n   add x13, x13, #1\n   add x0, x13, #1\n   str w17, [x23, x0, lsl #2]\n   b 24f\n"
        "23: sub x13, x13, #1\n"
        "24: mov x1, x13\n"
        "25: cmp x1, x12\n   b.lt 10b\n   add x0, x23, x1, lsl #2\n   ldrsw x2, [x0, #-4]\n   ldrsw x3, [x0, #4]\n"
        "sub x4, x3, #1\n   cmp x2, x3\n   csel x2, x2, x4, lt\n   sub x3, x2, x1\n"
        "sub x4, x2, x24\n   sub x5, x3, x26\n   cmp x4, x5\n   csel x4, x4, x5, lt\n   cmp x4, #0\n   b.le 28f\n"
        "add x5, x20, x2, lsl #2\n   add x6, x21, x3, lsl #2\n   cmp x4, #2\n   b.lt 27f\n"
        "26: ldr x7, [x5, #-8]!\n   ldr x15, [x6, #-8]!\n   cmp x7, x15\n   b.ne 29f\n"
        "sub x2, x2, #2\n   sub x4, x4, #2\n   cmp x4, #2\n   b.ge 26b\n"
        "27: cbz x4, 28f\n   ldr w7, [x5, #-4]\n   ldr w15, [x6, #-4]\n   cmp w7, w15\n   cset x7, eq\n   sub x2, x2, x7\n   b 28f\n"
        "29: lsr x7, x7, #32\n   lsr x15, x15, #32\n   cmp w7, w15\n   cset x7, eq\n   sub x2, x2, x7\n"
        "28: str w2, [x0]\n   cbnz x14, 290f\n   cmp x1, x10\n   b.lt 290f\n   cmp x1, x11\n   b.gt 290f\n"
        "ldrsw x4, [x22, x1, lsl #2]\n   cmp x2, x4\n   b.le 70f\n"
        "290: sub x1, x1, #2\n   b 25b\n"
        "70: sub x3, x2, x1\n   stp x2, x3, [x19, #80]\n"
        "81: mov w0, #1\n   b 82f\n"
        "80: mov w0, #0\n"
        "82: ldp x27, x28, [sp, #64]\n   ldp x25, x26, [sp, #48]\n   ldp x23, x24, [sp, #32]\n"
        "ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #80\n" SH_DIFF_RET
        "90: mov w0, #0\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_meet)
);

#elif defined(SH_DIFF_X64)

#if defined(_WIN32)
# define SH_DIFF_LOAD3     "mov r9, rcx\n   mov r10, rdx\n   mov r11, r8\n"
# define SH_DIFF_PROLOGUE  "push rbx\n   push rbp\n   push rdi\n   push rsi\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 40\n   mov rbx, rcx\n"
# define SH_DIFF_EPILOGUE  "add rsp, 40\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rsi\n   pop rdi\n   pop rbp\n   pop rbx\n"
#else
# define SH_DIFF_LOAD3     "mov r9, rdi\n   mov r10, rsi\n   mov r11, rdx\n"
# define SH_DIFF_PROLOGUE  "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 40\n   mov rbx, rdi\n"
# define SH_DIFF_EPILOGUE  "add rsp, 40\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#endif

__asm__(
        SH_DIFF_SECTION
        ".intel_syntax noprefix\n"

        /* sh_diff_common_prefix_u32(xv, yv, max_len) -> rax */
        SH_DIFF_FUNC(sh_diff_common_prefix_u32)
        SH_DIFF_LOAD3
        "xor eax, eax\n   test r9, r9\n   jz 4f\n   test r10, r10\n   jz 4f\n   test r11, r11\n   jz 4f\n"
        "cmp r11, 2\n   jb 3f\n"
        "1:  mov rcx, [r9 + rax*4]\n   cmp rcx, [r10 + rax*4]\n   jne 2f\n"
        "add rax, 2\n   sub r11, 2\n   cmp r11, 2\n   jae 1b\n"
        "3:  test r11, r11\n   jz 4f\n   mov ecx, [r9 + rax*4]\n   cmp ecx, [r10 + rax*4]\n   jne 4f\n   inc rax\n"
        "4:\n" SH_DIFF_RET
        "2:  cmp ecx, [r10 + rax*4]\n   jne 4b\n   inc rax\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_common_prefix_u32)

        /* sh_diff_common_suffix_u32(xv_end, yv_end, max_len) -> rax */
        SH_DIFF_FUNC(sh_diff_common_suffix_u32)
        SH_DIFF_LOAD3
        "xor eax, eax\n   test r9, r9\n   jz 4f\n   test r10, r10\n   jz 4f\n   test r11, r11\n   jz 4f\n"
        "cmp r11, 2\n   jb 3f\n"
        "1:  mov rcx, [r9 - 8]\n   cmp rcx, [r10 - 8]\n   jne 2f\n"
        "sub r9, 8\n   sub r10, 8\n   add rax, 2\n   sub r11, 2\n   cmp r11, 2\n   jae 1b\n"
        "3:  test r11, r11\n   jz 4f\n   mov ecx, [r9 - 4]\n   cmp ecx, [r10 - 4]\n   jne 4f\n   inc rax\n"
        "4:\n" SH_DIFF_RET
        "2:  shr rcx, 32\n   cmp ecx, [r10 - 4]\n   jne 4b\n   inc rax\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_common_suffix_u32)

        /* sh_diff_meet(req) -> eax */
        SH_DIFF_FUNC(sh_diff_meet)
        SH_DIFF_PROLOGUE
        "test rbx, rbx\n   jz 80f\n   mov r8, [rbx]\n   mov r9, [rbx + 8]\n   mov r10, [rbx + 16]\n   mov r11, [rbx + 24]\n"
        "test r10, r10\n   jz 80f\n   test r11, r11\n   jz 80f\n"
        "mov rax, [rbx + 40]\n   mov rcx, [rbx + 48]\n   cmp rax, rcx\n   ja 80f\n"
        "mov rdx, [rbx + 56]\n   mov rsi, [rbx + 64]\n   cmp rdx, rsi\n   ja 80f\n"
        "cmp rax, rcx\n   jne 1f\n   cmp rdx, rsi\n   jne 1f\n   mov [rbx + 80], rax\n   mov [rbx + 88], rdx\n   jmp 81f\n"
        "1:  cmp rax, rcx\n   je 2f\n   test r8, r8\n   jz 80f\n"
        "2:  cmp rdx, rsi\n   je 3f\n   test r9, r9\n   jz 80f\n"
        "3:  mov rdi, [rbx + 32]\n   lea r10, [r10 + rdi*4]\n   lea r11, [r11 + rdi*4]\n"
        "mov rdi, rax\n   sub rdi, rsi\n   mov [rsp], rdi\n   mov rdi, rcx\n   sub rdi, rdx\n   mov [rsp + 8], rdi\n"
        "mov r12, rax\n   sub r12, rdx\n   mov r13, r12\n   mov r14, rcx\n   sub r14, rsi\n   mov r15, r14\n"
        "mov rdi, r12\n   sub rdi, r14\n   and edi, 1\n   mov [rsp + 16], rdi\n"
        "mov [r10 + r12*4], eax\n   mov [r11 + r14*4], ecx\n"
        "mov rax, [rbx + 72]\n   test rax, rax\n   jnz 4f\n   dec rax\n"
        "4:  mov [rsp + 24], rax\n"
        /* forward pass */
        "10: mov rax, [rsp + 24]\n   test rax, rax\n   jz 80f\n   dec rax\n   mov [rsp + 24], rax\n"
        "cmp r12, [rsp]\n   jle 11f\n   dec r12\n   mov dword ptr [r10 + r12*4 - 4], -1\n   jmp 12f\n"
        "11: inc r12\n"
        "12: cmp r13, [rsp + 8]\n   jge 13f\n   inc r13\n   mov dword ptr [r10 + r13*4 + 4], -1\n   jmp 14f\n"
        "13: dec r13\n"
        "14: mov rdi, r13\n"
        "15: cmp rdi, r12\n   jl 20f\n   movsxd rax, dword ptr [r10 + rdi*4 - 4]\n   movsxd rcx, dword ptr [r10 + rdi*4 + 4]\n"
        "lea rdx, [rax + 1]\n   cmp rax, rcx\n   cmovl rdx, rcx\n   mov rax, rdx\n"
        "mov rcx, [rbx + 48]\n   sub rcx, rax\n   sub rdx, rdi\n   mov rsi, [rbx + 64]\n   sub rsi, rdx\n"
        "cmp rcx, rsi\n   cmovg rcx, rsi\n   test rcx, rcx\n   jle 18f\n"
        "lea rsi, [r8 + rax*4]\n   lea rdx, [r9 + rdx*4]\n   cmp rcx, 2\n   jl 17f\n"
        "16: mov rbp, [rsi]\n   cmp rbp, [rdx]\n   jne 19f\n"
        "add rsi, 8\n   add rdx, 8\n   add rax, 2\n   sub rcx, 2\n   cmp rcx, 2\n   jge 16b\n"
        "17: test rcx, rcx\n   jz 18f\n   mov ebp, [rsi]\n   cmp ebp, [rdx]\n   jne 18f\n   inc rax\n   jmp 18f\n"
        "19: cmp ebp, [rdx]\n   jne 18f\n   inc rax\n"
        "18: mov [r10 + rdi*4], eax\n   cmp qword ptr [rsp + 16], 0\n   je 190f\n"
        "cmp rdi, r14\n   jl 190f\n   cmp rdi, r15\n   jg 190f\n"
        "movsxd rcx, dword ptr [r11 + rdi*4]\n   cmp rcx, rax\n   jle 70f\n"
        "190: sub rdi, 2\n   jmp 15b\n"
        /* backward pass */
        "20: mov rax, [rsp + 24]\n   test rax, rax\n   jz 80f\n   dec rax\n   mov [rsp + 24], rax\n"
        "cmp r14, [rsp]\n   jle 21f\n   dec r14\n   mov dword ptr [r11 + r14*4 - 4], 0x7fffffff\n   jmp 22f\n"
        "21: inc r14\n"
        "22: cmp r15, [rsp + 8]\n   jge 23f\n   inc r15\n   mov dword ptr [r11 + r15*4 + 4], 0x7fffffff\n   jmp 24f\n"
        "23: dec r15\n"
        "24: mov rdi, r15\n"
        "25: cmp rdi, r14\n   jl 10b\n   movsxd rax, dword ptr [r11 + rdi*4 - 4]\n   movsxd rcx, dword ptr [r11 + rdi*4 + 4]\n"
        "lea rdx, [rcx - 1]\n   cmp rax, rcx\n   cmovl rdx, rax\n   mov rax, rdx\n"
        "mov rcx, rax\n   sub rcx, [rbx + 40]\n   sub rdx, rdi\n   mov rsi, rdx\n   sub rsi, [rbx + 56]\n"
        "cmp rcx, rsi\n   cmovg rcx, rsi\n   test rcx, rcx\n   jle 28f\n"
        "lea rsi, [r8 + rax*4]\n   lea rdx, [r9 + rdx*4]\n   cmp rcx, 2\n   jl 27f\n"
        "26: mov rbp, [rsi - 8]\n   cmp rbp, [rdx - 8]\n   jne 29f\n"
        "sub rsi, 8\n   sub rdx, 8\n   sub rax, 2\n   sub rcx, 2\n   cmp rcx, 2\n   jge 26b\n"
        "27: test rcx, rcx\n   jz 28f\n   mov ebp, [rsi - 4]\n   cmp ebp, [rdx - 4]\n   jne 28f\n   dec rax\n   jmp 28f\n"
        "29: shr rbp, 32\n   cmp ebp, [rdx - 4]\n   jne 28f\n   dec rax\n"
        "28: mov [r11 + rdi*4], eax\n   cmp qword ptr [rsp + 16], 0\n   jne 290f\n"
        "cmp rdi, r12\n   jl 290f\n   cmp rdi, r13\n   jg 290f\n"
        "movsxd rcx, dword ptr [r10 + rdi*4]\n   cmp rax, rcx\n   jle 70f\n"
        "290: sub rdi, 2\n   jmp 25b\n"
        "70: mov [rbx + 80], rax\n   sub rax, rdi\n   mov [rbx + 88], rax\n"
        "81: mov eax, 1\n   jmp 82f\n"
        "80: xor eax, eax\n"
        "82:\n" SH_DIFF_EPILOGUE SH_DIFF_RET
        SH_DIFF_END(sh_diff_meet)

        ".att_syntax prefix\n"
);

#elif defined(SH_DIFF_RISCV64)

__asm__(
        SH_DIFF_SECTION

        /* sh_diff_common_prefix_u32(a0=xv, a1=yv, a2=max_len) -> a0 */
        SH_DIFF_FUNC(sh_diff_common_prefix_u32)
        "beqz a0, 3f\n   beqz a1, 3f\n   beqz a2, 3f\n   li t0, 0\n"
        "1:  lw t1, 0(a0)\n   lw t2, 0(a1)\n   bne t1, t2, 2f\n"
        "addi a0, a0, 4\n   addi a1, a1, 4\n   addi t0, t0, 1\n   bltu t0, a2, 1b\n"
        "2:  mv a0, t0\n" SH_DIFF_RET
        "3:  li a0, 0\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_common_prefix_u32)

        /* sh_diff_common_suffix_u32(a0=xv_end, a1=yv_end, a2=max_len) -> a0 */
        SH_DIFF_FUNC(sh_diff_common_suffix_u32)
        "beqz a0, 3f\n   beqz a1, 3f\n   beqz a2, 3f\n   li t0, 0\n"
        "1:  lw t1, -4(a0)\n   lw t2, -4(a1)\n   bne t1, t2, 2f\n"
        "addi a0, a0, -4\n   addi a1, a1, -4\n   addi t0, t0, 1\n   bltu t0, a2, 1b\n"
        "2:  mv a0, t0\n" SH_DIFF_RET
        "3:  li a0, 0\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_common_suffix_u32)

        /* sh_diff_meet(a0=req) -> a0 */
        SH_DIFF_FUNC(sh_diff_meet)
        "beqz a0, 90f\n   addi sp, sp, -96\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n"
        "sd s4, 32(sp)\n   sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n"
        "sd s8, 64(sp)\n   sd s9, 72(sp)\n   sd s10, 80(sp)\n   sd s11, 88(sp)\n   mv s0, a0\n"
        "ld s1, 0(s0)\n   ld s2, 8(s0)\n   ld s3, 16(s0)\n   ld s4, 24(s0)\n   ld t0, 32(s0)\n"
        "ld s5, 40(s0)\n   ld s6, 48(s0)\n   ld s7, 56(s0)\n   ld s8, 64(s0)\n   ld s11, 72(s0)\n"
        "beqz s3, 80f\n   beqz s4, 80f\n   bgtu s5, s6, 80f\n   bgtu s7, s8, 80f\n"
        "bne s5, s6, 1f\n   bne s7, s8, 1f\n   sd s5, 80(s0)\n   sd s7, 88(s0)\n   j 81f\n"
        "1:  beq s5, s6, 2f\n   beqz s1, 80f\n"
        "2:  beq s7, s8, 3f\n   beqz s2, 80f\n"
        "3:  slli t0, t0, 2\n   add s3, s3, t0\n   add s4, s4, t0\n"
        "sub s9, s5, s8\n   sub s10, s6, s7\n   sub a1, s5, s7\n   mv a2, a1\n"
        "sub a3, s6, s8\n   mv a4, a3\n   sub a5, a1, a3\n   andi a5, a5, 1\n"
        "slli t0, a1, 2\n   add t0, s3, t0\n   sw s5, 0(t0)\n"
        "slli t0, a3, 2\n   add t0, s4, t0\n   sw s6, 0(t0)\n"
        "bnez s11, 10f\n   li s11, -1\n"
        /* forward pass */
        "10: beqz s11, 80f\n   addi s11, s11, -1\n   ble a1, s9, 11f\n"
        "addi a1, a1, -1\n   slli t0, a1, 2\n   add t0, s3, t0\n   li t1, -1\n   sw t1, -4(t0)\n   j 12f\n"
        "11: addi a1, a1, 1\n"
        "12: bge a2, s10, 13f\n   addi a2, a2, 1\n   slli t0, a2, 2\n   add t0, s3, t0\n   li t1, -1\n   sw t1, 4(t0)\n   j 14f\n"
        "13: addi a2, a2, -1\n"
        "14: mv a6, a2\n"
        "15: blt a6, a1, 20f\n   slli t0, a6, 2\n   add t0, s3, t0\n   lw t1, -4(t0)\n   lw t2, 4(t0)\n"
        "blt t1, t2, 16f\n   addi a7, t1, 1\n   j 17f\n"
        "16: mv a7, t2\n"
        "17: sub t3, a7, a6\n   sub t4, s6, a7\n   sub t5, s8, t3\n   blt t4, t5, 18f\n   mv t4, t5\n"
        "18: blez t4, 191f\n   slli t1, a7, 2\n   add t1, s1, t1\n   slli t2, t3, 2\n   add t2, s2, t2\n"
        "19: lw t5, 0(t1)\n   lw t6, 0(t2)\n   bne t5, t6, 191f\n"
        "addi t1, t1, 4\n   addi t2, t2, 4\n   addi a7, a7, 1\n   addi t4, t4, -1\n   bgtz t4, 19b\n"
        "191: sw a7, 0(t0)\n   beqz a5, 192f\n   blt a6, a3, 192f\n   bgt a6, a4, 192f\n"
        "slli t1, a6, 2\n   add t1, s4, t1\n   lw t1, 0(t1)\n   ble t1, a7, 70f\n"
        "192: addi a6, a6, -2\n   j 15b\n"
        /* backward pass */
        "20: beqz s11, 80f\n   addi s11, s11, -1\n   ble a3, s9, 21f\n"
        "addi a3, a3, -1\n   slli t0, a3, 2\n   add t0, s4, t0\n   li t1, 0x7fffffff\n   sw t1, -4(t0)\n   j 22f\n"
        "21: addi a3, a3, 1\n"
        "22: bge a4, s10, 23f\n   addi a4, a4, 1\n   slli t0, a4, 2\n   add t0, s4, t0\n   li t1, 0x7fffffff\n   sw t1, 4(t0)\n   j 24f\n"
        "23: addi a4, a4, -1\n"
        "24: mv a6, a4\n"
        "25: blt a6, a3, 10b\n   slli t0, a6, 2\n   add t0, s4, t0\n   lw t1, -4(t0)\n   lw t2, 4(t0)\n"
        "blt t1, t2, 26f\n   addi a7, t2, -1\n   j 27f\n"
        "26: mv a7, t1\n"
        "27: sub t3, a7, a6\n   sub t4, a7, s5\n   sub t5, t3, s7\n   blt t4, t5, 28f\n   mv t4, t5\n"
        "28: blez t4, 291f\n   slli t1, a7, 2\n   add t1, s1, t1\n   slli t2, t3, 2\n   add t2, s2, t2\n"
        "29: lw t5, -4(t1)\n   lw t6, -4(t2)\n   bne t5, t6, 291f\n"
        "addi t1, t1, -4\n   addi t2, t2, -4\n   addi a7, a7, -1\n   addi t4, t4, -1\n   bgtz t4, 29b\n"
        "291: sw a7, 0(t0)\n   bnez a5, 292f\n   blt a6, a1, 292f\n   bgt a6, a2, 292f\n"
        "slli t1, a6, 2\n   add t1, s3, t1\n   lw t1, 0(t1)\n   ble a7, t1, 70f\n"
        "292: addi a6, a6, -2\n   j 25b\n"
        "70: sd a7, 80(s0)\n   sub t3, a7, a6\n   sd t3, 88(s0)\n"
        "81: li a0, 1\n   j 82f\n"
        "80: li a0, 0\n"
        "82: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n"
        "ld s4, 32(sp)\n   ld s5, 40(sp)\n   ld s6, 48(sp)\n   ld s7, 56(sp)\n"
        "ld s8, 64(sp)\n   ld s9, 72(sp)\n   ld s10, 80(sp)\n   ld s11, 88(sp)\n   addi sp, sp, 96\n" SH_DIFF_RET
        "90: li a0, 0\n" SH_DIFF_RET
        SH_DIFF_END(sh_diff_meet)
);

#endif

#endif /* SH_SHELL_DIFF_ASM_IMPLEMENTATION */
