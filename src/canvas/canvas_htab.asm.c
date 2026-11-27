/*
        Moonwater Canvas open-addressing hash-table core primitives in pure assembly:
          - chtab_key  : bounds-checked 32-bit / 64-bit key load from strided record table
          - chtab_hash : golden-ratio power-of-2 bucket hash (32-bit & 64-bit modes, non-pow2 guarded)
          - chtab_put  : bounded linear-probing insert (never loops infinitely on full table)
          - chtab_find : bounded linear-probing lookup (exact/first-slot mode, OOB-slot guarded)
          - chtab_del  : branchless cyclic-distance backward-shift delete (hole & slot guarded)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define CANVAS_HTAB_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CANVAS_HTAB_ASM_H
#define MOONWATER_CANVAS_HTAB_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  chtab_u8;
typedef p16 chtab_u16;
typedef p32 chtab_u32;
typedef b32 chtab_i32;
typedef p64 chtab_u64;
typedef b64 chtab_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  chtab_u8;
typedef __UINT16_TYPE__ chtab_u16;
typedef __UINT32_TYPE__ chtab_u32;
typedef __INT32_TYPE__  chtab_i32;
typedef __UINT64_TYPE__ chtab_u64;
typedef __INT64_TYPE__  chtab_i64;
#else
#include <stdint.h>
typedef uint8_t  chtab_u8;
typedef uint16_t chtab_u16;
typedef uint32_t chtab_u32;
typedef int32_t  chtab_i32;
typedef uint64_t chtab_u64;
typedef int64_t  chtab_i64;
#endif

/* 80 bytes -- byte-for-byte identical to struct wayland_table */
struct chtab_table {
        chtab_u8  *at;              /* +0  */
        chtab_i64  unit;            /* +8  */
        chtab_i64  count;           /* +16 */
        chtab_i64  usable;          /* +24 */
        chtab_i64  reserved;        /* +32 */
        chtab_i64  bytes;           /* +40 */
        chtab_i64  frees;           /* +48 */
        chtab_i64  committed;       /* +56 */
        chtab_i64  stack_committed; /* +64 */
        chtab_i64  stack;           /* +72 */
};

#ifdef offset_of
#define CHTAB_OFFSET(T, M) offset_of(T, M)
#else
#define CHTAB_OFFSET(T, M) ((chtab_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct chtab_table) == 80, "chtab_table must be 80 bytes");
_Static_assert(CHTAB_OFFSET(struct chtab_table, at) == 0 &&
               CHTAB_OFFSET(struct chtab_table, unit) == 8 &&
               CHTAB_OFFSET(struct chtab_table, count) == 16,
               "chtab_table field offsets");

chtab_u64 chtab_key(const struct chtab_table *records, chtab_i64 slot, chtab_i32 key64);
chtab_u64 chtab_hash(chtab_u64 key, chtab_i64 buckets, chtab_i32 key64);
void      chtab_put(struct chtab_table *index, chtab_u64 key, chtab_i64 slot, chtab_i32 key64);
chtab_i64 chtab_find(const struct chtab_table *index, const struct chtab_table *records,
                     chtab_u64 key, chtab_i32 first, chtab_u64 *at, chtab_i32 key64);
void      chtab_del(struct chtab_table *index, const struct chtab_table *records,
                    chtab_u64 hole, chtab_i32 key64);

#endif /* MOONWATER_CANVAS_HTAB_ASM_H */

#if defined(CANVAS_HTAB_ASM_IMPLEMENTATION) && !defined(CANVAS_HTAB_ASM_EMITTED)
#define CANVAS_HTAB_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define CHTAB_ASM_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define CHTAB_ASM_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define CHTAB_ASM_RISCV64 1
# else
#  error "src/canvas/canvas_htab.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define CHTAB_ASM_ARM64 1
# elif defined(X64) && X64
#  define CHTAB_ASM_X64 1
# elif defined(RISCV64) && RISCV64
#  define CHTAB_ASM_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define CHTAB_ASM_SECTION  ASM_SECTION
# define CHTAB_ASM_FUNC(n)  ASM_FUNC(n)
# define CHTAB_ASM_END(n)   ASM_END(n)
# define CHTAB_ASM_RET      ASM_RET
#else
# define CHTAB_ASM_SECTION  ".text\n"
# define CHTAB_ASM_RET      "ret\n"
# if defined(__APPLE__)
#  define CHTAB_ASM_FUNC(n) ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define CHTAB_ASM_END(n)  ""
# elif defined(_WIN32)
#  define CHTAB_ASM_FUNC(n) ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define CHTAB_ASM_END(n)  ""
# else
#  if defined(CHTAB_ASM_ARM64)
#   define CHTAB_ASM_FTYPE  "%function"
#  else
#   define CHTAB_ASM_FTYPE  "@function"
#  endif
#  define CHTAB_ASM_FUNC(n) ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " CHTAB_ASM_FTYPE "\n" #n ":\n"
#  define CHTAB_ASM_END(n)  ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(CHTAB_ASM_ARM64)

__asm__(
        CHTAB_ASM_SECTION

        /* chtab_key(x0=records, x1=slot, w2=key64) -> u64 */
        CHTAB_ASM_FUNC(chtab_key)
        "cbz x0, 2f\n   tbnz x1, #63, 2f\n   ldp x3, x4, [x0]\n   ldr x5, [x0, #16]\n"
        "cbz x3, 2f\n   cmp x4, #0\n   b.le 2f\n   cmp x1, x5\n   b.hs 2f\n"
        "madd x3, x1, x4, x3\n   cbz w2, 1f\n   ldr x0, [x3]\n" CHTAB_ASM_RET
        "1:  ldr w0, [x3]\n" CHTAB_ASM_RET
        "2:  mov x0, #0\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_key)

        /* chtab_hash(x0=key, x1=buckets, w2=key64) -> u64 */
        CHTAB_ASM_FUNC(chtab_hash)
        "cmp x1, #0\n   b.le 2f\n   sub x3, x1, #1\n   tst x1, x3\n   b.ne 2f\n"
        "movz x4, #0x7c15\n   movk x4, #0x7f4a, lsl #16\n   movk x4, #0x79b9, lsl #32\n   movk x4, #0x9e37, lsl #48\n"
        "cbz w2, 1f\n   lsr x0, x0, #12\n   mul x0, x0, x4\n   lsr x0, x0, #20\n   and x0, x0, x3\n" CHTAB_ASM_RET
        "1:  mov w0, w0\n   mul x0, x0, x4\n   lsr x0, x0, #24\n   and x0, x0, x3\n" CHTAB_ASM_RET
        "2:  mov x0, #0\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_hash)

        /* chtab_put(x0=index, x1=key, x2=slot, w3=key64) */
        CHTAB_ASM_FUNC(chtab_put)
        "cbz x0, 4f\n   tbnz x2, #63, 4f\n   ldr x4, [x0]\n   ldr x5, [x0, #16]\n"
        "cbz x4, 4f\n   cmp x5, #0\n   b.le 4f\n   sub x6, x5, #1\n   tst x5, x6\n   b.ne 4f\n"
        "movz x7, #0x7c15\n   movk x7, #0x7f4a, lsl #16\n   movk x7, #0x79b9, lsl #32\n   movk x7, #0x9e37, lsl #48\n"
        "cbz w3, 1f\n   lsr x8, x1, #12\n   mul x8, x8, x7\n   lsr x8, x8, #20\n   b 2f\n"
        "1:  mov w8, w1\n   mul x8, x8, x7\n   lsr x8, x8, #24\n"
        "2:  and x8, x8, x6\n   add w9, w2, #1\n"
        "3:  ldr w10, [x4, x8, lsl #2]\n   cbz w10, 5f\n   add x8, x8, #1\n   and x8, x8, x6\n   subs x5, x5, #1\n   b.ne 3b\n"
        "4:  " CHTAB_ASM_RET
        "5:  str w9, [x4, x8, lsl #2]\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_put)

        /* chtab_find(x0=index, x1=records, x2=key, w3=first, x4=at, w5=key64) -> i64 */
        CHTAB_ASM_FUNC(chtab_find)
        "cbz x0, 9f\n   cbz x1, 9f\n   ldr x6, [x0]\n   ldr x7, [x0, #16]\n"
        "cbz x6, 9f\n   cmp x7, #0\n   b.le 9f\n   sub x8, x7, #1\n   tst x7, x8\n   b.ne 9f\n"
        "ldp x9, x10, [x1]\n   ldr x11, [x1, #16]\n   cbz x9, 9f\n   cmp x10, #0\n   b.le 9f\n   cmp x11, #0\n   b.le 9f\n"
        "movz x13, #0x7c15\n   movk x13, #0x7f4a, lsl #16\n   movk x13, #0x79b9, lsl #32\n   movk x13, #0x9e37, lsl #48\n"
        "cbz w5, 1f\n   lsr x12, x2, #12\n   mul x12, x12, x13\n   lsr x12, x12, #20\n   b 2f\n"
        "1:  mov w12, w2\n   mul x12, x12, x13\n   lsr x12, x12, #24\n"
        "2:  and x12, x12, x8\n   mov x0, #-1\n"
        "3:  ldr w13, [x6, x12, lsl #2]\n   cbz w13, 8f\n   sub x14, x13, #1\n   cmp x14, x11\n   b.hs 6f\n"
        "madd x15, x14, x10, x9\n   cbz w5, 4f\n   ldr x15, [x15]\n   b 5f\n"
        "4:  ldr w15, [x15]\n"
        "5:  cmp x15, x2\n   b.ne 6f\n   cbnz w3, 7f\n   cbz x4, 10f\n   str x12, [x4]\n"
        "10: mov x0, x14\n" CHTAB_ASM_RET
        "7:  tbnz x0, #63, 11f\n   cmp x14, x0\n   b.ge 6f\n"
        "11: mov x0, x14\n"
        "6:  add x12, x12, #1\n   and x12, x12, x8\n   subs x7, x7, #1\n   b.ne 3b\n"
        "8:  " CHTAB_ASM_RET
        "9:  mov x0, #-1\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_find)

        /* chtab_del(x0=index, x1=records, x2=hole, w3=key64) */
        CHTAB_ASM_FUNC(chtab_del)
        "cbz x0, 8f\n   cbz x1, 8f\n   ldr x4, [x0]\n   ldr x5, [x0, #16]\n"
        "cbz x4, 8f\n   cmp x5, #0\n   b.le 8f\n   sub x6, x5, #1\n   tst x5, x6\n   b.ne 8f\n   cmp x2, x5\n   b.hs 8f\n"
        "ldp x7, x8, [x1]\n   ldr x9, [x1, #16]\n   cbz x7, 8f\n   cmp x8, #0\n   b.le 8f\n   cmp x9, #0\n   b.le 8f\n"
        "str wzr, [x4, x2, lsl #2]\n   subs x5, x5, #1\n   b.eq 8f\n"
        "movz x10, #0x7c15\n   movk x10, #0x7f4a, lsl #16\n   movk x10, #0x79b9, lsl #32\n   movk x10, #0x9e37, lsl #48\n"
        "add x11, x2, #1\n   and x11, x11, x6\n"
        "1:  ldr w12, [x4, x11, lsl #2]\n   cbz w12, 8f\n   sub x13, x12, #1\n   cmp x13, x9\n   b.hs 4f\n"
        "madd x14, x13, x8, x7\n   cbz w3, 2f\n   ldr x14, [x14]\n   lsr x14, x14, #12\n   mul x14, x14, x10\n   lsr x14, x14, #20\n   b 3f\n"
        "2:  ldr w14, [x14]\n   mul x14, x14, x10\n   lsr x14, x14, #24\n"
        "3:  and x14, x14, x6\n   sub x15, x2, x14\n   and x15, x15, x6\n   sub x16, x11, x14\n   and x16, x16, x6\n"
        "cmp x15, x16\n   b.hs 4f\n   str w12, [x4, x2, lsl #2]\n   str wzr, [x4, x11, lsl #2]\n   mov x2, x11\n"
        "4:  add x11, x11, #1\n   and x11, x11, x6\n   subs x5, x5, #1\n   b.ne 1b\n"
        "8:  " CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_del)
);

#elif defined(CHTAB_ASM_X64)

#if defined(_WIN32)
# define CHTAB_L3 "mov r10d, r8d\n   mov r8, rcx\n   mov r9, rdx\n"
# define CHTAB_L4 "mov r10, r8\n   mov r11d, r9d\n   mov r8, rcx\n   mov r9, rdx\n"
# define CHTAB_L6 "mov r12, [rsp + 96]\n   mov r13d, [rsp + 104]\n   mov r10, r8\n   mov r11d, r9d\n   mov r8, rcx\n   mov r9, rdx\n"
#else
# define CHTAB_L3 "mov r8, rdi\n   mov r9, rsi\n   mov r10d, edx\n"
# define CHTAB_L4 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11d, ecx\n"
# define CHTAB_L6 "mov r12, r8\n   mov r13d, r9d\n   mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11d, ecx\n"
#endif

__asm__(
        CHTAB_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* chtab_key(r8=records, r9=slot, r10d=key64) -> u64 */
        CHTAB_ASM_FUNC(chtab_key)
        CHTAB_L3
        "xor eax, eax\n   test r8, r8\n   jz 2f\n   test r9, r9\n   js 2f\n"
        "mov rcx, [r8]\n   test rcx, rcx\n   jz 2f\n   mov rdx, [r8 + 8]\n   test rdx, rdx\n   jle 2f\n"
        "cmp r9, [r8 + 16]\n   jae 2f\n   imul r9, rdx\n   add rcx, r9\n   test r10d, r10d\n   jz 1f\n"
        "mov rax, [rcx]\n" CHTAB_ASM_RET
        "1:  mov eax, [rcx]\n"
        "2:  " CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_key)

        /* chtab_hash(r8=key, r9=buckets, r10d=key64) -> u64 */
        CHTAB_ASM_FUNC(chtab_hash)
        CHTAB_L3
        "xor eax, eax\n   test r9, r9\n   jle 2f\n   lea rcx, [r9 - 1]\n   test r9, rcx\n   jnz 2f\n"
        "mov rdx, 0x9e3779b97f4a7c15\n   test r10d, r10d\n   jz 1f\n"
        "mov rax, r8\n   shr rax, 12\n   imul rax, rdx\n   shr rax, 20\n   and rax, rcx\n" CHTAB_ASM_RET
        "1:  mov eax, r8d\n   imul rax, rdx\n   shr rax, 24\n   and rax, rcx\n"
        "2:  " CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_hash)

        /* chtab_put(r8=index, r9=key, r10=slot, r11d=key64) */
        CHTAB_ASM_FUNC(chtab_put)
        CHTAB_L4
        "test r8, r8\n   jz 4f\n   test r10, r10\n   js 4f\n   mov rdx, [r8]\n   mov rcx, [r8 + 16]\n"
        "test rdx, rdx\n   jz 4f\n   test rcx, rcx\n   jle 4f\n   lea r8, [rcx - 1]\n   test rcx, r8\n   jnz 4f\n"
        "mov rax, 0x9e3779b97f4a7c15\n   test r11d, r11d\n   jz 1f\n"
        "shr r9, 12\n   imul rax, r9\n   shr rax, 20\n   jmp 2f\n"
        "1:  mov r9d, r9d\n   imul rax, r9\n   shr rax, 24\n"
        "2:  and rax, r8\n   inc r10d\n"
        "3:  cmp dword ptr [rdx + rax * 4], 0\n   je 5f\n   inc rax\n   and rax, r8\n   dec rcx\n   jnz 3b\n"
        "4:  " CHTAB_ASM_RET
        "5:  mov [rdx + rax * 4], r10d\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_put)

        /* chtab_find(index, records, key, first, at, key64) -> i64 */
        CHTAB_ASM_FUNC(chtab_find)
        "push rbx\n   push rbp\n   push rsi\n   push r12\n   push r13\n   push r14\n   push r15\n"
        CHTAB_L6
        "mov rbp, -1\n   test r8, r8\n   jz 8f\n   test r9, r9\n   jz 8f\n"
        "mov rbx, [r8]\n   mov rcx, [r8 + 16]\n   test rbx, rbx\n   jz 8f\n   test rcx, rcx\n   jle 8f\n"
        "lea r8, [rcx - 1]\n   test rcx, r8\n   jnz 8f\n"
        "mov r14, [r9]\n   mov r15, [r9 + 8]\n   mov r9, [r9 + 16]\n"
        "test r14, r14\n   jz 8f\n   test r15, r15\n   jle 8f\n   test r9, r9\n   jle 8f\n"
        "mov rdx, 0x9e3779b97f4a7c15\n   test r13d, r13d\n   jz 1f\n"
        "mov rax, r10\n   shr rax, 12\n   imul rdx, rax\n   shr rdx, 20\n   jmp 2f\n"
        "1:  mov eax, r10d\n   imul rdx, rax\n   shr rdx, 24\n"
        "2:  and rdx, r8\n"
        "3:  mov esi, [rbx + rdx * 4]\n   test esi, esi\n   jz 8f\n   dec rsi\n   cmp rsi, r9\n   jae 6f\n"
        "mov rax, rsi\n   imul rax, r15\n   add rax, r14\n   test r13d, r13d\n   jz 4f\n   mov rax, [rax]\n   jmp 5f\n"
        "4:  mov eax, [rax]\n"
        "5:  cmp rax, r10\n   jne 6f\n   test r11d, r11d\n   jnz 7f\n   test r12, r12\n   jz 10f\n   mov [r12], rdx\n"
        "10: mov rbp, rsi\n   jmp 8f\n"
        "7:  test rbp, rbp\n   js 11f\n   cmp rsi, rbp\n   jge 6f\n"
        "11: mov rbp, rsi\n"
        "6:  inc rdx\n   and rdx, r8\n   dec rcx\n   jnz 3b\n"
        "8:  mov rax, rbp\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rsi\n   pop rbp\n   pop rbx\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_find)

        /* chtab_del(r8=index, r9=records, r10=hole, r11d=key64) */
        CHTAB_ASM_FUNC(chtab_del)
        CHTAB_L4
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   mov rax, [r8]\n   mov rcx, [r8 + 16]\n"
        "test rax, rax\n   jz 9f\n   test rcx, rcx\n   jle 9f\n   lea rdx, [rcx - 1]\n   test rcx, rdx\n   jnz 9f\n   cmp r10, rcx\n   jae 9f\n"
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov rbx, rax\n   mov r8, rdx\n   mov r12, [r9]\n   mov r13, [r9 + 8]\n   mov r14, [r9 + 16]\n"
        "test r12, r12\n   jz 8f\n   test r13, r13\n   jle 8f\n   test r14, r14\n   jle 8f\n"
        "mov dword ptr [rbx + r10 * 4], 0\n   dec rcx\n   jz 8f\n"
        "mov r15, 0x9e3779b97f4a7c15\n   lea rbp, [r10 + 1]\n   and rbp, r8\n"
        "1:  mov r9d, [rbx + rbp * 4]\n   test r9d, r9d\n   jz 8f\n   lea rax, [r9 - 1]\n   cmp rax, r14\n   jae 4f\n"
        "imul rax, r13\n   add rax, r12\n   test r11d, r11d\n   jz 2f\n   mov rax, [rax]\n   shr rax, 12\n   imul rax, r15\n   shr rax, 20\n   jmp 3f\n"
        "2:  mov eax, [rax]\n   imul rax, r15\n   shr rax, 24\n"
        "3:  and rax, r8\n   mov rdx, r10\n   sub rdx, rax\n   and rdx, r8\n   sub rax, rbp\n   neg rax\n   and rax, r8\n"
        "cmp rdx, rax\n   jae 4f\n   mov [rbx + r10 * 4], r9d\n   mov dword ptr [rbx + rbp * 4], 0\n   mov r10, rbp\n"
        "4:  inc rbp\n   and rbp, r8\n   dec rcx\n   jnz 1b\n"
        "8:  pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
        "9:  " CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_del)

        ".att_syntax prefix\n"
);

#elif defined(CHTAB_ASM_RISCV64)

__asm__(
        CHTAB_ASM_SECTION

        /* chtab_key(a0=records, a1=slot, a2=key64) -> u64 */
        CHTAB_ASM_FUNC(chtab_key)
        "beqz a0, 2f\n   bltz a1, 2f\n   ld a3, 0(a0)\n   ld a4, 8(a0)\n   ld a5, 16(a0)\n"
        "beqz a3, 2f\n   blez a4, 2f\n   bgeu a1, a5, 2f\n   mul t0, a1, a4\n   add a3, a3, t0\n"
        "beqz a2, 1f\n   ld a0, 0(a3)\n" CHTAB_ASM_RET
        "1:  lwu a0, 0(a3)\n" CHTAB_ASM_RET
        "2:  li a0, 0\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_key)

        /* chtab_hash(a0=key, a1=buckets, a2=key64) -> u64 */
        CHTAB_ASM_FUNC(chtab_hash)
        "blez a1, 2f\n   addi a3, a1, -1\n   and t0, a1, a3\n   bnez t0, 2f\n   li a4, 0x9e3779b97f4a7c15\n"
        "beqz a2, 1f\n   srli a0, a0, 12\n   mul a0, a0, a4\n   srli a0, a0, 20\n   and a0, a0, a3\n" CHTAB_ASM_RET
        "1:  slli a0, a0, 32\n   srli a0, a0, 32\n   mul a0, a0, a4\n   srli a0, a0, 24\n   and a0, a0, a3\n" CHTAB_ASM_RET
        "2:  li a0, 0\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_hash)

        /* chtab_put(a0=index, a1=key, a2=slot, a3=key64) */
        CHTAB_ASM_FUNC(chtab_put)
        "beqz a0, 4f\n   bltz a2, 4f\n   ld a4, 0(a0)\n   ld a5, 16(a0)\n"
        "beqz a4, 4f\n   blez a5, 4f\n   addi a6, a5, -1\n   and t0, a5, a6\n   bnez t0, 4f\n"
        "li a7, 0x9e3779b97f4a7c15\n   beqz a3, 1f\n   srli t1, a1, 12\n   mul t1, t1, a7\n   srli t1, t1, 20\n   j 2f\n"
        "1:  slli t1, a1, 32\n   srli t1, t1, 32\n   mul t1, t1, a7\n   srli t1, t1, 24\n"
        "2:  and t1, t1, a6\n   addiw t2, a2, 1\n"
        "3:  slli t3, t1, 2\n   add t3, a4, t3\n   lwu t4, 0(t3)\n   beqz t4, 5f\n"
        "addi t1, t1, 1\n   and t1, t1, a6\n   addi a5, a5, -1\n   bnez a5, 3b\n"
        "4:  " CHTAB_ASM_RET
        "5:  sw t2, 0(t3)\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_put)

        /* chtab_find(a0=index, a1=records, a2=key, a3=first, a4=at, a5=key64) -> i64 */
        CHTAB_ASM_FUNC(chtab_find)
        "beqz a0, 9f\n   beqz a1, 9f\n   ld a6, 0(a0)\n   ld a7, 16(a0)\n"
        "beqz a6, 9f\n   blez a7, 9f\n   addi t0, a7, -1\n   and t5, a7, t0\n   bnez t5, 9f\n"
        "ld t1, 0(a1)\n   ld t2, 8(a1)\n   ld t3, 16(a1)\n   beqz t1, 9f\n   blez t2, 9f\n   blez t3, 9f\n"
        "li t5, 0x9e3779b97f4a7c15\n   beqz a5, 1f\n   srli t4, a2, 12\n   mul t4, t4, t5\n   srli t4, t4, 20\n   j 2f\n"
        "1:  slli t4, a2, 32\n   srli t4, t4, 32\n   mul t4, t4, t5\n   srli t4, t4, 24\n"
        "2:  and t4, t4, t0\n   li a0, -1\n"
        "3:  slli t6, t4, 2\n   add t6, a6, t6\n   lwu t5, 0(t6)\n   beqz t5, 8f\n   addi t5, t5, -1\n   bgeu t5, t3, 6f\n"
        "mul a1, t5, t2\n   add a1, t1, a1\n   beqz a5, 4f\n   ld a1, 0(a1)\n   j 5f\n"
        "4:  lwu a1, 0(a1)\n"
        "5:  bne a1, a2, 6f\n   bnez a3, 7f\n   beqz a4, 10f\n   sd t4, 0(a4)\n"
        "10: mv a0, t5\n" CHTAB_ASM_RET
        "7:  bltz a0, 11f\n   bge t5, a0, 6f\n"
        "11: mv a0, t5\n"
        "6:  addi t4, t4, 1\n   and t4, t4, t0\n   addi a7, a7, -1\n   bnez a7, 3b\n"
        "8:  " CHTAB_ASM_RET
        "9:  li a0, -1\n" CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_find)

        /* chtab_del(a0=index, a1=records, a2=hole, a3=key64) */
        CHTAB_ASM_FUNC(chtab_del)
        "beqz a0, 8f\n   beqz a1, 8f\n   ld a4, 0(a0)\n   ld a5, 16(a0)\n"
        "beqz a4, 8f\n   blez a5, 8f\n   addi a6, a5, -1\n   and t4, a5, a6\n   bnez t4, 8f\n   bgeu a2, a5, 8f\n"
        "ld a7, 0(a1)\n   ld t0, 8(a1)\n   ld t1, 16(a1)\n   beqz a7, 8f\n   blez t0, 8f\n   blez t1, 8f\n"
        "slli t4, a2, 2\n   add t4, a4, t4\n   sw zero, 0(t4)\n   addi a5, a5, -1\n   beqz a5, 8f\n"
        "li t2, 0x9e3779b97f4a7c15\n   addi t3, a2, 1\n   and t3, t3, a6\n"
        "1:  slli t4, t3, 2\n   add t4, a4, t4\n   lwu t5, 0(t4)\n   beqz t5, 8f\n   addi t6, t5, -1\n   bgeu t6, t1, 4f\n"
        "mul a0, t6, t0\n   add a0, a7, a0\n   beqz a3, 2f\n   ld a0, 0(a0)\n   srli a0, a0, 12\n   mul a0, a0, t2\n   srli a0, a0, 20\n   j 3f\n"
        "2:  lwu a0, 0(a0)\n   mul a0, a0, t2\n   srli a0, a0, 24\n"
        "3:  and a0, a0, a6\n   sub a1, a2, a0\n   and a1, a1, a6\n   sub t6, t3, a0\n   and t6, t6, a6\n"
        "bgeu a1, t6, 4f\n   slli a1, a2, 2\n   add a1, a4, a1\n   sw t5, 0(a1)\n   sw zero, 0(t4)\n   mv a2, t3\n"
        "4:  addi t3, t3, 1\n   and t3, t3, a6\n   addi a5, a5, -1\n   bnez a5, 1b\n"
        "8:  " CHTAB_ASM_RET
        CHTAB_ASM_END(chtab_del)
);

#endif
#endif /* CANVAS_HTAB_ASM_IMPLEMENTATION */
