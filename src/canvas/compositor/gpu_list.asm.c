/*
        Untrusted GPU display-list op validator & clamper in pure assembly:
          - gpu_list_cut : branchless signed 32-bit clamp [low, high]
          - gpu_list_ops : TOCTOU-safe single-load 32-byte op validator/clamper
                           (kinds 0/1 fill/tone, kind 2 AA box + 8/9 records,
                            kind 3 text run, kind 10 256-tone glyph)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_GPU_LIST_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_GPU_LIST_ASM_H
#define MOONWATER_COMPOSITOR_GPU_LIST_ASM_H

#ifdef STANDARD_MODERN_C
typedef p32 gpu_list_u32;
typedef b32 gpu_list_i32;
typedef p64 gpu_list_u64;
#elif defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT32_TYPE__ gpu_list_u32;
typedef __INT32_TYPE__  gpu_list_i32;
typedef __UINT64_TYPE__ gpu_list_u64;
#else
#include <stdint.h>
typedef uint32_t gpu_list_u32;
typedef int32_t  gpu_list_i32;
typedef uint64_t gpu_list_u64;
#endif

#ifndef COMPOSITOR_GPU_LIST_REACH
#define COMPOSITOR_GPU_LIST_REACH   4096ul
#endif
#ifndef COMPOSITOR_GPU_LIST_REFUSED
#define COMPOSITOR_GPU_LIST_REFUSED (~0u)
#endif
#ifndef COMPOSITOR_GPU_LIST_COORD
#define COMPOSITOR_GPU_LIST_COORD   (1 << 24)
#endif

/* 32 bytes — single 8-word GPU display-list op */
struct gpu_list_op {
        gpu_list_u32 v[8];
};

/* 64 bytes — call specification for gpu_list_ops */
struct gpu_list_spec {
        const gpu_list_u32 *from;       /* +0  untrusted op stream */
        gpu_list_u32       *to;         /* +8  validated op output */
        gpu_list_u64        cover_at;   /* +16 cover bytes offset in list */
        gpu_list_u64        cover_used; /* +24 cover bytes length */
        gpu_list_u64        list_bytes; /* +32 total list shm size */
        gpu_list_u32        count;      /* +40 input op count */
        gpu_list_u32        room;       /* +44 output op capacity */
        gpu_list_i32        width;      /* +48 picture width */
        gpu_list_i32        height;     /* +52 picture height */
        gpu_list_u32        slot;       /* +56 card base table slot (1..255) */
        gpu_list_u32        _pad;       /* +60 zero */
};

#ifdef offset_of
#define GPU_LIST_OFFSET(T, M) offset_of(T, M)
#else
#define GPU_LIST_OFFSET(T, M) ((gpu_list_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct gpu_list_op)   == 32, "gpu_list_op must be 32 bytes");
_Static_assert(sizeof(struct gpu_list_spec) == 64, "gpu_list_spec must be 64 bytes");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, from)       == 0,  "spec.from");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, to)         == 8,  "spec.to");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, cover_at)   == 16, "spec.cover_at");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, cover_used) == 24, "spec.cover_used");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, list_bytes) == 32, "spec.list_bytes");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, count)      == 40, "spec.count");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, room)       == 44, "spec.room");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, width)      == 48, "spec.width");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, height)     == 52, "spec.height");
_Static_assert(GPU_LIST_OFFSET(struct gpu_list_spec, slot)       == 56, "spec.slot");

gpu_list_i32 gpu_list_cut(gpu_list_u32 word, gpu_list_i32 low, gpu_list_i32 high);
gpu_list_u32 gpu_list_ops(const struct gpu_list_spec *spec);

#endif /* MOONWATER_COMPOSITOR_GPU_LIST_ASM_H */

#ifdef COMPOSITOR_GPU_LIST_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define GPU_LIST_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define GPU_LIST_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define GPU_LIST_RISCV64 1
# else
#  error "src/canvas/compositor/gpu_list.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define GPU_LIST_ARM64 1
# elif defined(X64) && X64
#  define GPU_LIST_X64 1
# elif defined(RISCV64) && RISCV64
#  define GPU_LIST_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define GL_ASM_SECTION   ASM_SECTION
# define GL_ASM_FUNC(n)   ASM_FUNC(n)
# define GL_ASM_END(n)    ASM_END(n)
# define GL_ASM_RET       ASM_RET
#else
# define GL_ASM_SECTION   ".text\n"
# define GL_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define GL_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define GL_ASM_END(n)   ""
# elif defined(_WIN32)
#  define GL_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define GL_ASM_END(n)   ""
# else
#  if defined(GPU_LIST_ARM64)
#   define GL_ASM_FTYPE   "%function"
#  else
#   define GL_ASM_FTYPE   "@function"
#  endif
#  define GL_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " GL_ASM_FTYPE "\n" #n ":\n"
#  define GL_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(GPU_LIST_ARM64)

__asm__(
        GL_ASM_SECTION

        /* gpu_list_cut(w0=word, w1=low, w2=high) -> clamped i32 */
        GL_ASM_FUNC(gpu_list_cut)
        "cmp w0, w2\n   csel w3, w2, w0, gt\n   cmp w0, w1\n   csel w0, w1, w3, lt\n" GL_ASM_RET
        GL_ASM_END(gpu_list_cut)

        /* gpu_list_ops(x0=spec) -> emitted count or ~0u */
        GL_ASM_FUNC(gpu_list_ops)
        "cbz x0, 99f\n   stp x19, x20, [sp, #-32]!\n   stp x21, x22, [sp, #16]\n"
        "ldp x1, x2, [x0]\n   ldp x3, x4, [x0, #16]\n   ldr x20, [x0, #32]\n"
        "ldp w5, w7, [x0, #40]\n   ldp w8, w9, [x0, #48]\n   ldr w10, [x0, #56]\n"
        "cmp w8, #0\n   b.le 98f\n   cmp w9, #0\n   b.le 98f\n   sub w21, w10, #1\n   cmp w21, #254\n   b.hi 98f\n"
        "lsl w10, w10, #16\n   cbz w5, 97f\n   cbz x1, 98f\n   cmp x2, #0\n   csel w7, wzr, w7, eq\n"
        "mov w11, #0\n   cmp x3, #4096\n   b.lo 0f\n   cmp x3, x20\n   b.hi 0f\n   sub x21, x20, x3\n"
        "cmp x4, x21\n   b.hi 0f\n   sub x21, x21, x4\n   cmp x21, #4096\n   b.lo 0f\n   add x4, x3, x4\n   mov w11, #1\n"
        "0:  mov w6, #0\n"
        "1:  cbz w5, 96f\n   ldp w0, w12, [x1]\n   ldp w13, w14, [x1, #8]\n   ldp w15, w16, [x1, #16]\n   ldp w17, w19, [x1, #24]\n"
        "add x1, x1, #32\n   and w20, w0, #255\n   cmp w20, #1\n   b.hi 2f\n"
        "cmp w0, #1\n   b.hi 98f\n"
        "bic w12, w12, w12, asr #31\n   cmp w12, w8\n   csel w12, w8, w12, gt\n"
        "bic w13, w13, w13, asr #31\n   cmp w13, w9\n   csel w13, w9, w13, gt\n"
        "bic w14, w14, w14, asr #31\n   cmp w14, w8\n   csel w14, w8, w14, gt\n"
        "bic w15, w15, w15, asr #31\n   cmp w15, w9\n   csel w15, w9, w15, gt\n"
        "cmp w12, w14\n   b.ge 8f\n   cmp w13, w15\n   b.ge 8f\n   cmp w6, w7\n   b.hs 98f\n   mov w17, #0\n   mov w19, #0\n"
        "7:  stp w0, w12, [x2]\n   stp w13, w14, [x2, #8]\n   stp w15, w16, [x2, #16]\n   stp w17, w19, [x2, #24]\n"
        "add x2, x2, #32\n   add w6, w6, #1\n"
        "8:  sub w5, w5, #1\n   b 1b\n"
        "2:  cmp w20, #2\n   b.ne 4f\n   lsr w20, w0, #8\n   cmp w20, #3\n   b.hi 98f\n   cmp w20, w5\n   b.hs 98f\n"
        "sub w21, w7, w6\n   add w22, w20, #1\n   cmp w22, w21\n   b.hi 98f\n   mov w21, #0x1000000\n   neg w22, w21\n"
        "3:  cmp w12, w22\n   csel w12, w22, w12, lt\n   cmp w12, w21\n   csel w12, w21, w12, gt\n"
        "cmp w13, w22\n   csel w13, w22, w13, lt\n   cmp w13, w21\n   csel w13, w21, w13, gt\n"
        "cmp w14, w22\n   csel w14, w22, w14, lt\n   cmp w14, w21\n   csel w14, w21, w14, gt\n"
        "cmp w15, w22\n   csel w15, w22, w15, lt\n   cmp w15, w21\n   csel w15, w21, w15, gt\n"
        "bic w17, w17, w17, asr #31\n   cmp w17, w21\n   csel w17, w21, w17, gt\n"
        "bic w19, w19, w19, asr #31\n   cmp w19, w21\n   csel w19, w21, w19, gt\n"
        "stp w0, w12, [x2]\n   stp w13, w14, [x2, #8]\n   stp w15, w16, [x2, #16]\n   stp w17, w19, [x2, #24]\n"
        "add x2, x2, #32\n   add w6, w6, #1\n   sub w5, w5, #1\n   cbz w20, 1b\n   sub w20, w20, #1\n"
        "ldp w0, w12, [x1]\n   ldp w13, w14, [x1, #8]\n   ldp w15, w16, [x1, #16]\n   ldp w17, w19, [x1, #24]\n"
        "add x1, x1, #32\n   cmp w0, #8\n   b.eq 3b\n   cmp w0, #9\n   b.eq 3b\n   b 98f\n"
        "4:  cmp w20, #3\n   b.ne 5f\n   cbz w11, 98f\n   lsr w20, w0, #8\n   sub w21, w20, #3\n   cmp w21, #3497\n   b.hi 98f\n"
        "orr w21, w12, w13\n   tbnz w21, #31, 98f\n   cmp w14, w8\n   b.gt 98f\n   cmp w15, w9\n   b.gt 98f\n"
        "cmp w12, w14\n   b.ge 8b\n   cmp w13, w15\n   b.ge 8b\n   cmp w6, w7\n   b.hs 98f\n"
        "sub w21, w14, w12\n   add x22, x21, #2\n   cmp x22, x20\n   b.hi 98f\n   add x22, x20, #1\n   cmp x19, x22\n   b.lo 98f\n"
        "sub x22, x19, x22\n   cmp x22, x3\n   b.lo 98f\n   cmp x19, x4\n   b.hi 98f\n   sub w22, w15, w13\n"
        "madd x22, x22, x20, x21\n   add x22, x22, #1\n   sub x21, x4, x19\n   cmp x22, x21\n   b.hi 98f\n"
        "and w17, w17, #0xffff\n   orr w17, w17, w10\n   b 7b\n"
        "5:  cmp w20, #10\n   b.ne 98f\n   cbz w11, 98f\n   orr w21, w12, w13\n   tbnz w21, #31, 98f\n"
        "cmp w14, w8\n   b.gt 98f\n   cmp w15, w9\n   b.gt 98f\n   cmp w12, w14\n   b.ge 8b\n   cmp w13, w15\n   b.ge 8b\n"
        "cmp w6, w7\n   b.hs 98f\n   lsr w20, w0, #8\n   sub w21, w14, w12\n   cmp x20, x21\n   b.lo 98f\n   cmp x20, #3500\n   b.hi 98f\n"
        "tst w16, #3\n   b.ne 98f\n   cmp x16, x3\n   b.lo 98f\n   cmp x16, x4\n   b.hi 98f\n   sub x22, x4, x16\n   cmp x22, #1024\n   b.lo 98f\n"
        "cmp x19, x3\n   b.lo 98f\n   cmp x19, x4\n   b.hi 98f\n   sub w22, w15, w13\n   sub x22, x22, #1\n"
        "madd x22, x22, x20, x21\n   sub x21, x4, x19\n   cmp x22, x21\n   b.hi 98f\n   mov w17, w10\n   b 7b\n"
        "96: mov w0, w6\n   b 95f\n"
        "97: mov w0, #0\n   b 95f\n"
        "98: mov w0, #-1\n"
        "95: ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #32\n" GL_ASM_RET
        "99: mov w0, #-1\n" GL_ASM_RET
        GL_ASM_END(gpu_list_ops)
);

#elif defined(GPU_LIST_X64)

#if defined(_WIN32)
#define GL_X64_PROLOGUE "push rbx\n   push rbp\n   push rsi\n   push rdi\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 32\n   mov rbp, rcx\n"
#define GL_X64_EPILOGUE "add rsp, 32\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbp\n   pop rbx\n"
#define GL_X64_CUT_BODY "mov eax, ecx\n   cmp ecx, r8d\n   cmovg eax, r8d\n   cmp ecx, edx\n   cmovl eax, edx\n"
#else
#define GL_X64_PROLOGUE "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 32\n   mov rbp, rdi\n"
#define GL_X64_EPILOGUE "add rsp, 32\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#define GL_X64_CUT_BODY "mov eax, edi\n   cmp edi, edx\n   cmovg eax, edx\n   cmp edi, esi\n   cmovl eax, esi\n"
#endif

__asm__(
        GL_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* gpu_list_cut(word, low, high) -> clamped i32 */
        GL_ASM_FUNC(gpu_list_cut)
        GL_X64_CUT_BODY GL_ASM_RET
        GL_ASM_END(gpu_list_cut)

        /* gpu_list_ops(spec) -> emitted count or ~0u */
        GL_ASM_FUNC(gpu_list_ops)
        GL_X64_PROLOGUE
        "test rbp, rbp\n   jz 98f\n   cmp dword ptr [rbp + 48], 0\n   jle 98f\n   cmp dword ptr [rbp + 52], 0\n   jle 98f\n"
        "mov eax, [rbp + 56]\n   lea ecx, [rax - 1]\n   cmp ecx, 254\n   ja 98f\n   shl eax, 16\n   mov [rsp + 8], eax\n"
        "mov r13d, [rbp + 40]\n   test r13d, r13d\n   jz 97f\n   mov rbx, [rbp]\n   test rbx, rbx\n   jz 98f\n"
        "mov r12, [rbp + 8]\n   mov r15d, [rbp + 44]\n   xor r14d, r14d\n   test r12, r12\n   cmovz r15d, r14d\n"
        "mov dword ptr [rsp + 12], 0\n   mov rax, [rbp + 16]\n   cmp rax, 4096\n   jb 1f\n   mov rcx, [rbp + 32]\n"
        "cmp rax, rcx\n   ja 1f\n   sub rcx, rax\n   mov rdx, [rbp + 24]\n   cmp rdx, rcx\n   ja 1f\n"
        "sub rcx, rdx\n   cmp rcx, 4096\n   jb 1f\n   add rax, rdx\n   mov [rsp], rax\n   mov dword ptr [rsp + 12], 1\n"
        "1:  test r13d, r13d\n   jz 96f\n   mov eax, [rbx]\n   mov r8d, [rbx + 4]\n   mov r9d, [rbx + 8]\n   mov r10d, [rbx + 12]\n"
        "mov r11d, [rbx + 16]\n   mov esi, [rbx + 20]\n   mov edi, [rbx + 24]\n   mov edx, [rbx + 28]\n   add rbx, 32\n"
        "movzx ecx, al\n   cmp ecx, 1\n   ja 2f\n   cmp eax, 1\n   ja 98f\n   xor ecx, ecx\n"
        "test r8d, r8d\n   cmovs r8d, ecx\n   cmp r8d, [rbp + 48]\n   cmovg r8d, [rbp + 48]\n"
        "test r9d, r9d\n   cmovs r9d, ecx\n   cmp r9d, [rbp + 52]\n   cmovg r9d, [rbp + 52]\n"
        "test r10d, r10d\n   cmovs r10d, ecx\n   cmp r10d, [rbp + 48]\n   cmovg r10d, [rbp + 48]\n"
        "test r11d, r11d\n   cmovs r11d, ecx\n   cmp r11d, [rbp + 52]\n   cmovg r11d, [rbp + 52]\n"
        "cmp r8d, r10d\n   jge 8f\n   cmp r9d, r11d\n   jge 8f\n   cmp r14d, r15d\n   jae 98f\n   xor edi, edi\n   xor edx, edx\n"
        "7:  mov [r12], eax\n   mov [r12 + 4], r8d\n   mov [r12 + 8], r9d\n   mov [r12 + 12], r10d\n"
        "mov [r12 + 16], r11d\n   mov [r12 + 20], esi\n   mov [r12 + 24], edi\n   mov [r12 + 28], edx\n   add r12, 32\n   inc r14d\n"
        "8:  dec r13d\n   jmp 1b\n"
        "2:  cmp ecx, 2\n   jne 4f\n   mov ecx, eax\n   shr ecx, 8\n   cmp ecx, 3\n   ja 98f\n   cmp ecx, r13d\n   jae 98f\n"
        "mov [rsp + 16], ecx\n   inc ecx\n   mov [rsp + 20], eax\n   mov eax, r15d\n   sub eax, r14d\n   cmp ecx, eax\n   ja 98f\n   mov eax, [rsp + 20]\n"
        "3:  mov [r12], eax\n   mov eax, 0x1000000\n   mov ecx, -0x1000000\n"
        "cmp r8d, ecx\n   cmovl r8d, ecx\n   cmp r8d, eax\n   cmovg r8d, eax\n"
        "cmp r9d, ecx\n   cmovl r9d, ecx\n   cmp r9d, eax\n   cmovg r9d, eax\n"
        "cmp r10d, ecx\n   cmovl r10d, ecx\n   cmp r10d, eax\n   cmovg r10d, eax\n"
        "cmp r11d, ecx\n   cmovl r11d, ecx\n   cmp r11d, eax\n   cmovg r11d, eax\n"
        "xor ecx, ecx\n   test edi, edi\n   cmovs edi, ecx\n   cmp edi, eax\n   cmovg edi, eax\n"
        "test edx, edx\n   cmovs edx, ecx\n   cmp edx, eax\n   cmovg edx, eax\n"
        "mov [r12 + 4], r8d\n   mov [r12 + 8], r9d\n   mov [r12 + 12], r10d\n   mov [r12 + 16], r11d\n"
        "mov [r12 + 20], esi\n   mov [r12 + 24], edi\n   mov [r12 + 28], edx\n   add r12, 32\n   inc r14d\n   dec r13d\n"
        "dec dword ptr [rsp + 16]\n   js 1b\n   mov eax, [rbx]\n   mov r8d, [rbx + 4]\n   mov r9d, [rbx + 8]\n   mov r10d, [rbx + 12]\n"
        "mov r11d, [rbx + 16]\n   mov esi, [rbx + 20]\n   mov edi, [rbx + 24]\n   mov edx, [rbx + 28]\n   add rbx, 32\n"
        "cmp eax, 8\n   je 3b\n   cmp eax, 9\n   je 3b\n   jmp 98f\n"
        "4:  cmp ecx, 3\n   jne 5f\n   cmp dword ptr [rsp + 12], 0\n   je 98f\n   mov ecx, eax\n   shr ecx, 8\n"
        "cmp ecx, 3\n   jb 98f\n   cmp ecx, 3500\n   ja 98f\n   test r8d, r8d\n   js 98f\n   test r9d, r9d\n   js 98f\n"
        "cmp r10d, [rbp + 48]\n   jg 98f\n   cmp r11d, [rbp + 52]\n   jg 98f\n   cmp r8d, r10d\n   jge 8b\n   cmp r9d, r11d\n   jge 8b\n"
        "cmp r14d, r15d\n   jae 98f\n   mov [rsp + 16], eax\n   mov eax, r10d\n   sub eax, r8d\n   lea rax, [rax + 2]\n   cmp rax, rcx\n   ja 98f\n"
        "lea rax, [rcx + 1]\n   cmp rdx, rax\n   jb 98f\n   mov rax, rdx\n   sub rax, rcx\n   dec rax\n   cmp rax, [rbp + 16]\n   jb 98f\n"
        "cmp rdx, [rsp]\n   ja 98f\n   mov eax, r11d\n   sub eax, r9d\n   imul rax, rcx\n   mov ecx, r10d\n   sub ecx, r8d\n"
        "lea rax, [rax + rcx + 1]\n   mov rcx, [rsp]\n   sub rcx, rdx\n   cmp rax, rcx\n   ja 98f\n"
        "mov eax, [rsp + 16]\n   and edi, 0xffff\n   or edi, [rsp + 8]\n   jmp 7b\n"
        "5:  cmp ecx, 10\n   jne 98f\n   cmp dword ptr [rsp + 12], 0\n   je 98f\n   test r8d, r8d\n   js 98f\n   test r9d, r9d\n   js 98f\n"
        "cmp r10d, [rbp + 48]\n   jg 98f\n   cmp r11d, [rbp + 52]\n   jg 98f\n   cmp r8d, r10d\n   jge 8b\n   cmp r9d, r11d\n   jge 8b\n"
        "cmp r14d, r15d\n   jae 98f\n   mov [rsp + 16], eax\n   mov ecx, eax\n   shr ecx, 8\n   mov eax, r10d\n   sub eax, r8d\n"
        "cmp rcx, rax\n   jb 98f\n   cmp rcx, 3500\n   ja 98f\n   test esi, 3\n   jnz 98f\n   cmp rsi, [rbp + 16]\n   jb 98f\n"
        "cmp rsi, [rsp]\n   ja 98f\n   mov rax, [rsp]\n   sub rax, rsi\n   cmp rax, 1024\n   jb 98f\n   cmp rdx, [rbp + 16]\n   jb 98f\n"
        "cmp rdx, [rsp]\n   ja 98f\n   mov eax, r11d\n   sub eax, r9d\n   dec rax\n   imul rax, rcx\n   mov ecx, r10d\n   sub ecx, r8d\n"
        "add rax, rcx\n   mov rcx, [rsp]\n   sub rcx, rdx\n   cmp rax, rcx\n   ja 98f\n"
        "mov eax, [rsp + 16]\n   mov edi, [rsp + 8]\n   jmp 7b\n"
        "96: mov eax, r14d\n   jmp 95f\n"
        "97: xor eax, eax\n   jmp 95f\n"
        "98: mov eax, -1\n"
        "95: " GL_X64_EPILOGUE GL_ASM_RET
        GL_ASM_END(gpu_list_ops)

        ".att_syntax prefix\n"
);

#elif defined(GPU_LIST_RISCV64)

__asm__(
        GL_ASM_SECTION

        /* gpu_list_cut(a0=word, a1=low, a2=high) -> clamped i32 */
        GL_ASM_FUNC(gpu_list_cut)
        "sext.w a0, a0\n   sext.w a1, a1\n   sext.w a2, a2\n   blt a0, a1, 1f\n   ble a0, a2, 2f\n   mv a0, a2\n" GL_ASM_RET
        "1:  mv a0, a1\n"
        "2:  " GL_ASM_RET
        GL_ASM_END(gpu_list_cut)

        /* gpu_list_ops(a0=spec) -> emitted count or ~0u */
        GL_ASM_FUNC(gpu_list_ops)
        "beqz a0, 99f\n   addi sp, sp, -96\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n"
        "sd s4, 32(sp)\n   sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n   sd s8, 64(sp)\n   sd s9, 72(sp)\n   sd s10, 80(sp)\n"
        "ld s0, 0(a0)\n   ld s1, 8(a0)\n   ld s2, 16(a0)\n   ld s3, 24(a0)\n   ld t0, 32(a0)\n"
        "lwu s4, 40(a0)\n   lwu s6, 44(a0)\n   lw s7, 48(a0)\n   lw s8, 52(a0)\n   lwu s9, 56(a0)\n"
        "blez s7, 98f\n   blez s8, 98f\n   addi t1, s9, -1\n   li t2, 254\n   bgtu t1, t2, 98f\n"
        "slli s9, s9, 16\n   beqz s4, 97f\n   beqz s0, 98f\n   bnez s1, 0f\n   li s6, 0\n"
        "0:  li s10, 0\n   li t2, 4096\n   bltu s2, t2, 6f\n   bgtu s2, t0, 6f\n   sub t1, t0, s2\n"
        "bgtu s3, t1, 6f\n   sub t1, t1, s3\n   bltu t1, t2, 6f\n   add s3, s2, s3\n   li s10, 1\n"
        "6:  li s5, 0\n"
        "1:  beqz s4, 96f\n   lwu a0, 0(s0)\n   lw a1, 4(s0)\n   lw a2, 8(s0)\n   lw a3, 12(s0)\n"
        "lw a4, 16(s0)\n   lwu a5, 20(s0)\n   lwu a6, 24(s0)\n   lwu a7, 28(s0)\n   addi s0, s0, 32\n"
        "andi t0, a0, 255\n   li t1, 1\n   bgtu t0, t1, 2f\n   bgtu a0, t1, 98f\n"
        "sraiw t1, a1, 31\n   not t1, t1\n   and a1, a1, t1\n   ble a1, s7, 10f\n   mv a1, s7\n"
        "10: sraiw t1, a2, 31\n   not t1, t1\n   and a2, a2, t1\n   ble a2, s8, 11f\n   mv a2, s8\n"
        "11: sraiw t1, a3, 31\n   not t1, t1\n   and a3, a3, t1\n   ble a3, s7, 12f\n   mv a3, s7\n"
        "12: sraiw t1, a4, 31\n   not t1, t1\n   and a4, a4, t1\n   ble a4, s8, 13f\n   mv a4, s8\n"
        "13: bge a1, a3, 8f\n   bge a2, a4, 8f\n   bgeu s5, s6, 98f\n   li a6, 0\n   li a7, 0\n"
        "7:  sw a0, 0(s1)\n   sw a1, 4(s1)\n   sw a2, 8(s1)\n   sw a3, 12(s1)\n   sw a4, 16(s1)\n"
        "sw a5, 20(s1)\n   sw a6, 24(s1)\n   sw a7, 28(s1)\n   addi s1, s1, 32\n   addi s5, s5, 1\n"
        "8:  addi s4, s4, -1\n   j 1b\n"
        "2:  li t1, 2\n   bne t0, t1, 4f\n   srliw t0, a0, 8\n   li t1, 3\n   bgtu t0, t1, 98f\n   bgeu t0, s4, 98f\n"
        "sub t1, s6, s5\n   addi t2, t0, 1\n   bgtu t2, t1, 98f\n   li t1, 0x1000000\n   neg t2, t1\n"
        "3:  bge a1, t2, 20f\n   mv a1, t2\n   j 21f\n   20: ble a1, t1, 21f\n   mv a1, t1\n"
        "21: bge a2, t2, 22f\n   mv a2, t2\n   j 23f\n   22: ble a2, t1, 23f\n   mv a2, t1\n"
        "23: bge a3, t2, 24f\n   mv a3, t2\n   j 25f\n   24: ble a3, t1, 25f\n   mv a3, t1\n"
        "25: bge a4, t2, 26f\n   mv a4, t2\n   j 27f\n   26: ble a4, t1, 27f\n   mv a4, t1\n"
        "27: sext.w a6, a6\n   sraiw t3, a6, 31\n   not t3, t3\n   and a6, a6, t3\n   ble a6, t1, 28f\n   mv a6, t1\n"
        "28: sext.w a7, a7\n   sraiw t3, a7, 31\n   not t3, t3\n   and a7, a7, t3\n   ble a7, t1, 29f\n   mv a7, t1\n"
        "29: sw a0, 0(s1)\n   sw a1, 4(s1)\n   sw a2, 8(s1)\n   sw a3, 12(s1)\n   sw a4, 16(s1)\n"
        "sw a5, 20(s1)\n   sw a6, 24(s1)\n   sw a7, 28(s1)\n   addi s1, s1, 32\n   addi s5, s5, 1\n   addi s4, s4, -1\n"
        "beqz t0, 1b\n   addi t0, t0, -1\n   lwu a0, 0(s0)\n   lw a1, 4(s0)\n   lw a2, 8(s0)\n   lw a3, 12(s0)\n"
        "lw a4, 16(s0)\n   lwu a5, 20(s0)\n   lwu a6, 24(s0)\n   lwu a7, 28(s0)\n   addi s0, s0, 32\n"
        "li t3, 8\n   beq a0, t3, 3b\n   li t3, 9\n   beq a0, t3, 3b\n   j 98f\n"
        "4:  li t1, 3\n   bne t0, t1, 5f\n   beqz s10, 98f\n   srliw t0, a0, 8\n   addi t1, t0, -3\n   li t2, 3497\n   bgtu t1, t2, 98f\n"
        "or t1, a1, a2\n   bltz t1, 98f\n   bgt a3, s7, 98f\n   bgt a4, s8, 98f\n   bge a1, a3, 8b\n   bge a2, a4, 8b\n"
        "bgeu s5, s6, 98f\n   sub t1, a3, a1\n   addi t2, t1, 2\n   bgtu t2, t0, 98f\n   addi t2, t0, 1\n   bltu a7, t2, 98f\n"
        "sub t2, a7, t2\n   bltu t2, s2, 98f\n   bgtu a7, s3, 98f\n   sub t2, a4, a2\n   mul t2, t2, t0\n"
        "add t2, t2, t1\n   addi t2, t2, 1\n   sub t3, s3, a7\n   bgtu t2, t3, 98f\n"
        "slli a6, a6, 48\n   srli a6, a6, 48\n   or a6, a6, s9\n   j 7b\n"
        "5:  li t1, 10\n   bne t0, t1, 98f\n   beqz s10, 98f\n   or t1, a1, a2\n   bltz t1, 98f\n"
        "bgt a3, s7, 98f\n   bgt a4, s8, 98f\n   bge a1, a3, 8b\n   bge a2, a4, 8b\n   bgeu s5, s6, 98f\n"
        "srliw t0, a0, 8\n   sub t1, a3, a1\n   bltu t0, t1, 98f\n   li t2, 3500\n   bgtu t0, t2, 98f\n"
        "andi t2, a5, 3\n   bnez t2, 98f\n   bltu a5, s2, 98f\n   bgtu a5, s3, 98f\n   sub t2, s3, a5\n"
        "li t3, 1024\n   bltu t2, t3, 98f\n   bltu a7, s2, 98f\n   bgtu a7, s3, 98f\n   sub t2, a4, a2\n"
        "addi t2, t2, -1\n   mul t2, t2, t0\n   add t2, t2, t1\n   sub t3, s3, a7\n   bgtu t2, t3, 98f\n"
        "mv a6, s9\n   j 7b\n"
        "96: sext.w a0, s5\n   j 95f\n"
        "97: li a0, 0\n   j 95f\n"
        "98: li a0, -1\n"
        "95: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   ld s4, 32(sp)\n   ld s5, 40(sp)\n"
        "ld s6, 48(sp)\n   ld s7, 56(sp)\n   ld s8, 64(sp)\n   ld s9, 72(sp)\n   ld s10, 80(sp)\n   addi sp, sp, 96\n" GL_ASM_RET
        "99: li a0, -1\n" GL_ASM_RET
        GL_ASM_END(gpu_list_ops)
);

#endif

#endif /* COMPOSITOR_GPU_LIST_ASM_IMPLEMENTATION */
