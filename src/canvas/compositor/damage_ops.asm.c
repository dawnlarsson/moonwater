/*
        Compositor damage region algebra & rect-cutting kernels in pure assembly:
          - dmg_rect_empty : empty/inverted rectangle test (NULL-safe)
          - dmg_rect_area  : overflow-safe 64-bit area (0 for empty/NULL, saturating INT64_MAX)
          - dmg_rect_join  : bounding union of two rects (aliasing-safe)
          - dmg_rect_meet  : intersection of two rects (aliasing-safe, zeros out on empty)
          - dmg_rect_cover : containment test (empty/NULL inner always covered)
          - dmg_rect_cut   : 4-piece exterior decomposition of r outside cut (aliasing-safe)
          - dmg_add        : coalescing damage-list insert with exact overflow-safe slack & cap-8 min-growth merge
          - dmg_subtract   : transactional damage-list subtraction (leaves list untouched on >8 overflow)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_DAMAGE_OPS_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_DAMAGE_OPS_ASM_H
#define MOONWATER_COMPOSITOR_DAMAGE_OPS_ASM_H

#ifdef STANDARD_MODERN_C
typedef p32 dmg_u32;
typedef b32 dmg_i32;
typedef p64 dmg_u64;
typedef b64 dmg_i64;
#elif defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT32_TYPE__ dmg_u32;
typedef __INT32_TYPE__  dmg_i32;
typedef __UINT64_TYPE__ dmg_u64;
typedef __INT64_TYPE__  dmg_i64;
#else
#include <stdint.h>
typedef uint32_t dmg_u32;
typedef int32_t  dmg_i32;
typedef uint64_t dmg_u64;
typedef int64_t  dmg_i64;
#endif

#ifndef DMG_LIST_CAP
#define DMG_LIST_CAP 8u
#endif

/* 16 bytes — byte-for-byte identical to struct canvas_rect */
struct dmg_rect {
        dmg_i32 x1; /* +0  */
        dmg_i32 y1; /* +4  */
        dmg_i32 x2; /* +8  */
        dmg_i32 y2; /* +12 */
};

/* 132 bytes — byte-for-byte identical to struct canvas_damage */
struct dmg_list {
        struct dmg_rect r[DMG_LIST_CAP]; /* +0..+127 */
        dmg_u32         count;           /* +128 */
};

#ifdef offset_of
#define DMG_OPS_OFFSET(T, M) offset_of(T, M)
#else
#define DMG_OPS_OFFSET(T, M) ((dmg_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct dmg_rect) == 16,  "dmg_rect must be 16 bytes");
_Static_assert(sizeof(struct dmg_list) == 132, "dmg_list must be 132 bytes");
_Static_assert(DMG_OPS_OFFSET(struct dmg_rect, x1) == 0,   "dmg_rect.x1");
_Static_assert(DMG_OPS_OFFSET(struct dmg_rect, y1) == 4,   "dmg_rect.y1");
_Static_assert(DMG_OPS_OFFSET(struct dmg_rect, x2) == 8,   "dmg_rect.x2");
_Static_assert(DMG_OPS_OFFSET(struct dmg_rect, y2) == 12,  "dmg_rect.y2");
_Static_assert(DMG_OPS_OFFSET(struct dmg_list, r)  == 0,   "dmg_list.r");
_Static_assert(DMG_OPS_OFFSET(struct dmg_list, count) == 128, "dmg_list.count");

dmg_i32 dmg_rect_empty(const struct dmg_rect *r);
dmg_i64 dmg_rect_area(const struct dmg_rect *r);
void    dmg_rect_join(const struct dmg_rect *a, const struct dmg_rect *b, struct dmg_rect *out);
dmg_i32 dmg_rect_meet(const struct dmg_rect *a, const struct dmg_rect *b, struct dmg_rect *out);
dmg_i32 dmg_rect_cover(const struct dmg_rect *outer, const struct dmg_rect *inner);
dmg_i32 dmg_rect_cut(const struct dmg_rect *r, const struct dmg_rect *cut, struct dmg_rect out[4]);
void    dmg_add(struct dmg_list *d, const struct dmg_rect *r, dmg_i64 slack);
dmg_i32 dmg_subtract(struct dmg_list *d, const struct dmg_rect *cut);

#endif /* MOONWATER_COMPOSITOR_DAMAGE_OPS_ASM_H */

#if defined(COMPOSITOR_DAMAGE_OPS_ASM_IMPLEMENTATION) && !defined(COMPOSITOR_DAMAGE_OPS_ASM_EMITTED)
#define COMPOSITOR_DAMAGE_OPS_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define DMG_OPS_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define DMG_OPS_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define DMG_OPS_RISCV64 1
# else
#  error "src/canvas/compositor/damage_ops.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define DMG_OPS_ARM64 1
# elif defined(X64) && X64
#  define DMG_OPS_X64 1
# elif defined(RISCV64) && RISCV64
#  define DMG_OPS_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define DMG_ASM_SECTION   ASM_SECTION
# define DMG_ASM_FUNC(n)   ASM_FUNC(n)
# define DMG_ASM_END(n)    ASM_END(n)
# define DMG_ASM_RET       ASM_RET
#else
# define DMG_ASM_SECTION   ".text\n"
# define DMG_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define DMG_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define DMG_ASM_END(n)   ""
# elif defined(_WIN32)
#  define DMG_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define DMG_ASM_END(n)   ""
# else
#  if defined(DMG_OPS_ARM64)
#   define DMG_ASM_FTYPE   "%function"
#  else
#   define DMG_ASM_FTYPE   "@function"
#  endif
#  define DMG_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " DMG_ASM_FTYPE "\n" #n ":\n"
#  define DMG_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(DMG_OPS_ARM64)

__asm__(
        DMG_ASM_SECTION

        /* dmg_rect_empty(x0=r) -> w0 */
        DMG_ASM_FUNC(dmg_rect_empty)
        "cbz x0, 1f\n   ldp w1, w2, [x0]\n   ldp w3, w4, [x0, #8]\n   cmp w3, w1\n   b.le 1f\n   cmp w4, w2\n   cset w0, le\n" DMG_ASM_RET
        "1:  mov w0, #1\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_empty)

        /* dmg_rect_area(x0=r) -> x0 (0 on NULL/empty, saturates INT64_MAX) */
        DMG_ASM_FUNC(dmg_rect_area)
        "cbz x0, 1f\n   ldpsw x1, x2, [x0]\n   ldpsw x3, x4, [x0, #8]\n   subs x1, x3, x1\n   b.le 1f\n   subs x2, x4, x2\n   b.le 1f\n"
        "mul x0, x1, x2\n   tbz x0, #63, 2f\n   mov x0, #0x7fffffffffffffff\n"
        "2:  " DMG_ASM_RET
        "1:  mov x0, #0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_area)

        /* dmg_rect_join(x0=a, x1=b, x2=out) */
        DMG_ASM_FUNC(dmg_rect_join)
        "cbz x0, 1f\n   cbz x1, 1f\n   cbz x2, 1f\n   ldp w3, w4, [x0]\n   ldp w5, w6, [x0, #8]\n"
        "ldp w7, w8, [x1]\n   ldp w9, w10, [x1, #8]\n   cmp w3, w7\n   csel w3, w3, w7, lt\n   cmp w4, w8\n   csel w4, w4, w8, lt\n"
        "cmp w5, w9\n   csel w5, w5, w9, gt\n   cmp w6, w10\n   csel w6, w6, w10, gt\n   stp w3, w4, [x2]\n   stp w5, w6, [x2, #8]\n"
        "1:  " DMG_ASM_RET
        DMG_ASM_END(dmg_rect_join)

        /* dmg_rect_meet(x0=a, x1=b, x2=out) -> w0 (1 non-empty, 0 empty/NULL) */
        DMG_ASM_FUNC(dmg_rect_meet)
        "cbz x0, 1f\n   cbz x1, 1f\n   ldp w3, w4, [x0]\n   ldp w5, w6, [x0, #8]\n   ldp w7, w8, [x1]\n   ldp w9, w10, [x1, #8]\n"
        "cmp w3, w7\n   csel w3, w3, w7, gt\n   cmp w4, w8\n   csel w4, w4, w8, gt\n   cmp w5, w9\n   csel w5, w5, w9, lt\n"
        "cmp w6, w10\n   csel w6, w6, w10, lt\n   cmp w5, w3\n   b.le 1f\n   cmp w6, w4\n   b.le 1f\n"
        "cbz x2, 2f\n   stp w3, w4, [x2]\n   stp w5, w6, [x2, #8]\n"
        "2:  mov w0, #1\n" DMG_ASM_RET
        "1:  cbz x2, 3f\n   stp wzr, wzr, [x2]\n   stp wzr, wzr, [x2, #8]\n"
        "3:  mov w0, #0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_meet)

        /* dmg_rect_cover(x0=outer, x1=inner) -> w0 */
        DMG_ASM_FUNC(dmg_rect_cover)
        "cbz x1, 2f\n   ldp w2, w3, [x1]\n   ldp w4, w5, [x1, #8]\n   cmp w4, w2\n   b.le 2f\n   cmp w5, w3\n   b.le 2f\n"
        "cbz x0, 1f\n   ldp w6, w7, [x0]\n   ldp w8, w9, [x0, #8]\n   cmp w6, w2\n   b.gt 1f\n   cmp w7, w3\n   b.gt 1f\n"
        "cmp w8, w4\n   b.lt 1f\n   cmp w9, w5\n   cset w0, ge\n" DMG_ASM_RET
        "1:  mov w0, #0\n" DMG_ASM_RET
        "2:  mov w0, #1\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_cover)

        /* dmg_rect_cut(x0=r, x1=cut, x2=out[4]) -> w0 (0..4) */
        DMG_ASM_FUNC(dmg_rect_cut)
        "cbz x0, 9f\n   cbz x2, 9f\n   ldp w3, w4, [x0]\n   ldp w5, w6, [x0, #8]\n   cmp w5, w3\n   b.le 9f\n   cmp w6, w4\n   b.le 9f\n"
        "cbz x1, 8f\n   ldp w7, w8, [x1]\n   ldp w9, w10, [x1, #8]\n   cmp w3, w7\n   csel w7, w3, w7, gt\n   cmp w4, w8\n   csel w8, w4, w8, gt\n"
        "cmp w5, w9\n   csel w9, w5, w9, lt\n   cmp w6, w10\n   csel w10, w6, w10, lt\n   cmp w9, w7\n   b.le 8f\n   cmp w10, w8\n   b.le 8f\n"
        "mov w0, #0\n   cmp w4, w8\n   b.ge 1f\n   stp w3, w4, [x2]\n   stp w5, w8, [x2, #8]\n   add x2, x2, #16\n   add w0, w0, #1\n"
        "1:  cmp w10, w6\n   b.ge 2f\n   stp w3, w10, [x2]\n   stp w5, w6, [x2, #8]\n   add x2, x2, #16\n   add w0, w0, #1\n"
        "2:  cmp w3, w7\n   b.ge 3f\n   stp w3, w8, [x2]\n   stp w7, w10, [x2, #8]\n   add x2, x2, #16\n   add w0, w0, #1\n"
        "3:  cmp w9, w5\n   b.ge 4f\n   stp w9, w8, [x2]\n   stp w5, w10, [x2, #8]\n   add w0, w0, #1\n"
        "4:  " DMG_ASM_RET
        "8:  stp w3, w4, [x2]\n   stp w5, w6, [x2, #8]\n   mov w0, #1\n" DMG_ASM_RET
        "9:  mov w0, #0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_cut)

        /* dmg_add(x0=d, x1=r, x2=slack) */
        DMG_ASM_FUNC(dmg_add)
        "cbz x0, 99f\n   cbz x1, 99f\n   ldpsw x3, x4, [x1]\n   ldpsw x5, x6, [x1, #8]\n"
        "subs x10, x5, x3\n   b.le 99f\n   subs x11, x6, x4\n   b.le 99f\n"
        "stp x19, x20, [sp, #-16]!\n   mul x7, x10, x11\n   ldr w8, [x0, #128]\n   cmp w8, #8\n   mov w10, #8\n   csel w8, w10, w8, hi\n   mov w9, #0\n"
        "1:  cmp w9, w8\n   b.hs 4f\n   add x1, x0, x9, lsl #4\n   ldpsw x10, x11, [x1]\n   ldpsw x12, x13, [x1, #8]\n"
        "cmp x3, x10\n   csel x14, x3, x10, lt\n   cmp x4, x11\n   csel x15, x4, x11, lt\n"
        "cmp x5, x12\n   csel x16, x5, x12, gt\n   cmp x6, x13\n   csel x17, x6, x13, gt\n"
        "sub x19, x16, x14\n   sub x20, x17, x15\n   mul x19, x19, x20\n"
        "subs x10, x12, x10\n   csel x10, xzr, x10, le\n   subs x11, x13, x11\n   csel x11, xzr, x11, le\n   mul x20, x10, x11\n"
        "sub x10, x19, x7\n   tbnz x2, #63, 2f\n   adds x11, x20, x2\n   b.cs 3f\n   cmp x10, x11\n   b.ls 3f\n   add w9, w9, #1\n   b 1b\n"
        "2:  neg x11, x2\n   subs x11, x20, x11\n   b.lo 20f\n   cmp x10, x11\n   b.ls 3f\n"
        "20: add w9, w9, #1\n   b 1b\n"
        "3:  mov x3, x14\n   mov x4, x15\n   mov x5, x16\n   mov x6, x17\n   mov x7, x19\n   sub w8, w8, #1\n"
        "add x10, x0, x8, lsl #4\n   ldp w11, w12, [x10]\n   ldp w13, w14, [x10, #8]\n   stp w11, w12, [x1]\n   stp w13, w14, [x1, #8]\n   mov w9, #0\n   b 1b\n"
        "4:  cmp w8, #8\n   b.hs 5f\n   add x1, x0, x8, lsl #4\n   stp w3, w4, [x1]\n   stp w5, w6, [x1, #8]\n"
        "add w8, w8, #1\n   str w8, [x0, #128]\n   ldp x19, x20, [sp], #16\n" DMG_ASM_RET
        "5:  mov w9, #0\n   mov w19, #0\n   mvn x20, xzr\n"
        "6:  add x1, x0, x9, lsl #4\n   ldpsw x10, x11, [x1]\n   ldpsw x12, x13, [x1, #8]\n"
        "cmp x3, x10\n   csel x14, x3, x10, lt\n   cmp x4, x11\n   csel x15, x4, x11, lt\n"
        "cmp x5, x12\n   csel x16, x5, x12, gt\n   cmp x6, x13\n   csel x17, x6, x13, gt\n"
        "sub x14, x16, x14\n   sub x15, x17, x15\n   mul x14, x14, x15\n"
        "subs x10, x12, x10\n   csel x10, xzr, x10, le\n   subs x11, x13, x11\n   csel x11, xzr, x11, le\n   mul x10, x10, x11\n"
        "sub x14, x14, x10\n   cmp x14, x20\n   b.hs 7f\n   mov x20, x14\n   mov w19, w9\n"
        "7:  add w9, w9, #1\n   cmp w9, #8\n   b.lo 6b\n"
        "add x1, x0, x19, lsl #4\n   ldp w10, w11, [x1]\n   ldp w12, w13, [x1, #8]\n"
        "cmp w3, w10\n   csel w3, w3, w10, lt\n   cmp w4, w11\n   csel w4, w4, w11, lt\n"
        "cmp w5, w12\n   csel w5, w5, w12, gt\n   cmp w6, w13\n   csel w6, w6, w13, gt\n"
        "stp w3, w4, [x1]\n   stp w5, w6, [x1, #8]\n   str w8, [x0, #128]\n   ldp x19, x20, [sp], #16\n"
        "99: " DMG_ASM_RET
        DMG_ASM_END(dmg_add)

        /* dmg_subtract(x0=d, x1=cut) -> w0 (1 ok, 0 overflow/NULL) */
        DMG_ASM_FUNC(dmg_subtract)
        "cbz x0, 99f\n   ldr w2, [x0, #128]\n   cbz w2, 98f\n   cmp w2, #8\n   mov w3, #8\n   csel w2, w3, w2, hi\n"
        "cbz x1, 98f\n   ldp w3, w4, [x1]\n   ldp w5, w6, [x1, #8]\n   cmp w5, w3\n   b.le 98f\n   cmp w6, w4\n   b.le 98f\n"
        "sub sp, sp, #128\n   mov w7, #0\n   mov w8, #0\n   mov x9, sp\n"
        "1:  cmp w7, w2\n   b.hs 8f\n   add x10, x0, x7, lsl #4\n   ldp w11, w12, [x10]\n   ldp w13, w14, [x10, #8]\n   add w7, w7, #1\n"
        "cmp w13, w3\n   b.le 2f\n   cmp w11, w5\n   b.ge 2f\n   cmp w14, w4\n   b.le 2f\n   cmp w12, w6\n   b.ge 2f\n"
        "cmp w13, w11\n   b.le 1b\n   cmp w14, w12\n   b.le 1b\n"
        "cmp w11, w3\n   csel w15, w11, w3, gt\n   cmp w12, w4\n   csel w16, w12, w4, gt\n"
        "cmp w13, w5\n   csel w17, w13, w5, lt\n   cmp w14, w6\n   csel w10, w14, w6, lt\n"
        "cmp w12, w16\n   b.ge 3f\n   cmp w8, #8\n   b.hs 9f\n   stp w11, w12, [x9]\n   stp w13, w16, [x9, #8]\n   add x9, x9, #16\n   add w8, w8, #1\n"
        "3:  cmp w10, w14\n   b.ge 4f\n   cmp w8, #8\n   b.hs 9f\n   stp w11, w10, [x9]\n   stp w13, w14, [x9, #8]\n   add x9, x9, #16\n   add w8, w8, #1\n"
        "4:  cmp w11, w15\n   b.ge 5f\n   cmp w8, #8\n   b.hs 9f\n   stp w11, w16, [x9]\n   stp w15, w10, [x9, #8]\n   add x9, x9, #16\n   add w8, w8, #1\n"
        "5:  cmp w17, w13\n   b.ge 1b\n   cmp w8, #8\n   b.hs 9f\n   stp w17, w16, [x9]\n   stp w13, w10, [x9, #8]\n   add x9, x9, #16\n   add w8, w8, #1\n   b 1b\n"
        "2:  cmp w8, #8\n   b.hs 9f\n   stp w11, w12, [x9]\n   stp w13, w14, [x9, #8]\n   add x9, x9, #16\n   add w8, w8, #1\n   b 1b\n"
        "8:  str w8, [x0, #128]\n   mov x9, sp\n   mov w7, #0\n"
        "80: cmp w7, w8\n   b.hs 81f\n   ldp w11, w12, [x9]\n   ldp w13, w14, [x9, #8]\n   add x9, x9, #16\n   stp w11, w12, [x0]\n   stp w13, w14, [x0, #8]\n   add x0, x0, #16\n   add w7, w7, #1\n   b 80b\n"
        "81: add sp, sp, #128\n"
        "98: mov w0, #1\n" DMG_ASM_RET
        "9:  add sp, sp, #128\n"
        "99: mov w0, #0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_subtract)
);

#elif defined(DMG_OPS_X64)

#if defined(_WIN32)
# define DMG_A0 "rcx"
# define DMG_A1 "rdx"
# define DMG_A2 "r8"
# define DMG_X64_PUSH_FULL "push rbx\n   push rbp\n   push rsi\n   push rdi\n   push r12\n   push r13\n   push r14\n   push r15\n"
# define DMG_X64_POP_FULL  "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbp\n   pop rbx\n"
# define DMG_X64_ARGS3_FULL "mov rbp, rcx\n   mov rbx, rdx\n   mov r12, r8\n"
# define DMG_X64_ARGS2_FULL "mov rbp, rcx\n   mov rbx, rdx\n"
#else
# define DMG_A0 "rdi"
# define DMG_A1 "rsi"
# define DMG_A2 "rdx"
# define DMG_X64_PUSH_FULL "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
# define DMG_X64_POP_FULL  "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
# define DMG_X64_ARGS3_FULL "mov rbp, rdi\n   mov rbx, rsi\n   mov r12, rdx\n"
# define DMG_X64_ARGS2_FULL "mov rbp, rdi\n   mov rbx, rsi\n"
#endif

__asm__(
        DMG_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* dmg_rect_empty(A0=r) -> eax */
        DMG_ASM_FUNC(dmg_rect_empty)
        "test " DMG_A0 ", " DMG_A0 "\n   jz 1f\n   mov r9d, [" DMG_A0 " + 8]\n   cmp r9d, [" DMG_A0 "]\n   jle 1f\n"
        "mov r10d, [" DMG_A0 " + 12]\n   xor eax, eax\n   cmp r10d, [" DMG_A0 " + 4]\n   setle al\n" DMG_ASM_RET
        "1:  mov eax, 1\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_empty)

        /* dmg_rect_area(A0=r) -> rax */
        DMG_ASM_FUNC(dmg_rect_area)
        "test " DMG_A0 ", " DMG_A0 "\n   jz 1f\n   movsxd rax, dword ptr [" DMG_A0 " + 8]\n   movsxd r9, dword ptr [" DMG_A0 "]\n   sub rax, r9\n   jle 1f\n"
        "movsxd r10, dword ptr [" DMG_A0 " + 12]\n   movsxd r11, dword ptr [" DMG_A0 " + 4]\n   sub r10, r11\n   jle 1f\n"
        "imul rax, r10\n   test rax, rax\n   jns 2f\n   mov rax, 0x7fffffffffffffff\n"
        "2:  " DMG_ASM_RET
        "1:  xor eax, eax\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_area)

        /* dmg_rect_join(A0=a, A1=b, A2=out) */
        DMG_ASM_FUNC(dmg_rect_join)
        "test " DMG_A0 ", " DMG_A0 "\n   jz 1f\n   test " DMG_A1 ", " DMG_A1 "\n   jz 1f\n   test " DMG_A2 ", " DMG_A2 "\n   jz 1f\n"
        "mov eax, [" DMG_A0 "]\n   cmp eax, [" DMG_A1 "]\n   cmovg eax, [" DMG_A1 "]\n"
        "mov r9d, [" DMG_A0 " + 4]\n   cmp r9d, [" DMG_A1 " + 4]\n   cmovg r9d, [" DMG_A1 " + 4]\n"
        "mov r10d, [" DMG_A0 " + 8]\n   cmp r10d, [" DMG_A1 " + 8]\n   cmovl r10d, [" DMG_A1 " + 8]\n"
        "mov r11d, [" DMG_A0 " + 12]\n   cmp r11d, [" DMG_A1 " + 12]\n   cmovl r11d, [" DMG_A1 " + 12]\n"
        "mov [" DMG_A2 "], eax\n   mov [" DMG_A2 " + 4], r9d\n   mov [" DMG_A2 " + 8], r10d\n   mov [" DMG_A2 " + 12], r11d\n"
        "1:  " DMG_ASM_RET
        DMG_ASM_END(dmg_rect_join)

        /* dmg_rect_meet(A0=a, A1=b, A2=out) -> eax */
        DMG_ASM_FUNC(dmg_rect_meet)
        "test " DMG_A0 ", " DMG_A0 "\n   jz 1f\n   test " DMG_A1 ", " DMG_A1 "\n   jz 1f\n"
        "mov eax, [" DMG_A0 "]\n   cmp eax, [" DMG_A1 "]\n   cmovl eax, [" DMG_A1 "]\n"
        "mov r9d, [" DMG_A0 " + 4]\n   cmp r9d, [" DMG_A1 " + 4]\n   cmovl r9d, [" DMG_A1 " + 4]\n"
        "mov r10d, [" DMG_A0 " + 8]\n   cmp r10d, [" DMG_A1 " + 8]\n   cmovg r10d, [" DMG_A1 " + 8]\n"
        "mov r11d, [" DMG_A0 " + 12]\n   cmp r11d, [" DMG_A1 " + 12]\n   cmovg r11d, [" DMG_A1 " + 12]\n"
        "cmp r10d, eax\n   jle 1f\n   cmp r11d, r9d\n   jle 1f\n   test " DMG_A2 ", " DMG_A2 "\n   jz 2f\n"
        "mov [" DMG_A2 "], eax\n   mov [" DMG_A2 " + 4], r9d\n   mov [" DMG_A2 " + 8], r10d\n   mov [" DMG_A2 " + 12], r11d\n"
        "2:  mov eax, 1\n" DMG_ASM_RET
        "1:  xor eax, eax\n   test " DMG_A2 ", " DMG_A2 "\n   jz 3f\n   mov [" DMG_A2 "], rax\n   mov [" DMG_A2 " + 8], rax\n"
        "3:  " DMG_ASM_RET
        DMG_ASM_END(dmg_rect_meet)

        /* dmg_rect_cover(A0=outer, A1=inner) -> eax */
        DMG_ASM_FUNC(dmg_rect_cover)
        "test " DMG_A1 ", " DMG_A1 "\n   jz 2f\n   mov r9d, [" DMG_A1 "]\n   mov r10d, [" DMG_A1 " + 8]\n   cmp r10d, r9d\n   jle 2f\n"
        "mov r11d, [" DMG_A1 " + 4]\n   mov eax, [" DMG_A1 " + 12]\n   cmp eax, r11d\n   jle 2f\n   test " DMG_A0 ", " DMG_A0 "\n   jz 1f\n"
        "cmp [" DMG_A0 "], r9d\n   jg 1f\n   cmp [" DMG_A0 " + 4], r11d\n   jg 1f\n   cmp [" DMG_A0 " + 8], r10d\n   jl 1f\n"
        "cmp [" DMG_A0 " + 12], eax\n   jl 1f\n"
        "2:  mov eax, 1\n" DMG_ASM_RET
        "1:  xor eax, eax\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_cover)

        /* dmg_rect_cut(A0=r, A1=cut, A2=out[4]) -> eax */
        DMG_ASM_FUNC(dmg_rect_cut)
        DMG_X64_PUSH_FULL DMG_X64_ARGS3_FULL
        "test rbp, rbp\n   jz 9f\n   test r12, r12\n   jz 9f\n   mov r8d, [rbp]\n   mov r9d, [rbp + 4]\n   mov r10d, [rbp + 8]\n   mov r11d, [rbp + 12]\n"
        "cmp r10d, r8d\n   jle 9f\n   cmp r11d, r9d\n   jle 9f\n   test rbx, rbx\n   jz 8f\n"
        "mov r13d, r8d\n   cmp r13d, [rbx]\n   cmovl r13d, [rbx]\n   mov r14d, r9d\n   cmp r14d, [rbx + 4]\n   cmovl r14d, [rbx + 4]\n"
        "mov r15d, r10d\n   cmp r15d, [rbx + 8]\n   cmovg r15d, [rbx + 8]\n   mov ecx, r11d\n   cmp ecx, [rbx + 12]\n   cmovg ecx, [rbx + 12]\n"
        "cmp r15d, r13d\n   jle 8f\n   cmp ecx, r14d\n   jle 8f\n   xor eax, eax\n"
        "cmp r9d, r14d\n   jge 1f\n   mov [r12], r8d\n   mov [r12 + 4], r9d\n   mov [r12 + 8], r10d\n   mov [r12 + 12], r14d\n   add r12, 16\n   inc eax\n"
        "1:  cmp ecx, r11d\n   jge 2f\n   mov [r12], r8d\n   mov [r12 + 4], ecx\n   mov [r12 + 8], r10d\n   mov [r12 + 12], r11d\n   add r12, 16\n   inc eax\n"
        "2:  cmp r8d, r13d\n   jge 3f\n   mov [r12], r8d\n   mov [r12 + 4], r14d\n   mov [r12 + 8], r13d\n   mov [r12 + 12], ecx\n   add r12, 16\n   inc eax\n"
        "3:  cmp r15d, r10d\n   jge 4f\n   mov [r12], r15d\n   mov [r12 + 4], r14d\n   mov [r12 + 8], r10d\n   mov [r12 + 12], ecx\n   inc eax\n"
        "4:  " DMG_X64_POP_FULL DMG_ASM_RET
        "8:  mov [r12], r8d\n   mov [r12 + 4], r9d\n   mov [r12 + 8], r10d\n   mov [r12 + 12], r11d\n   mov eax, 1\n" DMG_X64_POP_FULL DMG_ASM_RET
        "9:  xor eax, eax\n" DMG_X64_POP_FULL DMG_ASM_RET
        DMG_ASM_END(dmg_rect_cut)

        /* dmg_add(A0=d, A1=r, A2=slack) */
        DMG_ASM_FUNC(dmg_add)
        DMG_X64_PUSH_FULL DMG_X64_ARGS3_FULL
        "sub rsp, 24\n   test rbp, rbp\n   jz 99f\n   test rbx, rbx\n   jz 99f\n"
        "mov r13d, [rbx]\n   mov r14d, [rbx + 4]\n   mov r15d, [rbx + 8]\n   mov ebx, [rbx + 12]\n"
        "movsxd rax, r15d\n   movsxd rcx, r13d\n   sub rax, rcx\n   jle 99f\n"
        "movsxd rdx, ebx\n   movsxd rcx, r14d\n   sub rdx, rcx\n   jle 99f\n"
        "imul rax, rdx\n   mov [rsp], rax\n   mov [rsp + 8], r12\n"
        "mov ecx, [rbp + 128]\n   mov eax, 8\n   cmp ecx, eax\n   cmova ecx, eax\n   xor edx, edx\n"
        "1:  cmp edx, ecx\n   jae 4f\n   mov eax, edx\n   shl eax, 4\n   lea rsi, [rbp + rax]\n"
        "mov r8d, [rsi]\n   cmp r8d, r13d\n   cmovg r8d, r13d\n   mov r9d, [rsi + 4]\n   cmp r9d, r14d\n   cmovg r9d, r14d\n"
        "mov r10d, [rsi + 8]\n   cmp r10d, r15d\n   cmovl r10d, r15d\n   mov r11d, [rsi + 12]\n   cmp r11d, ebx\n   cmovl r11d, ebx\n"
        "movsxd rdi, r10d\n   movsxd rax, r8d\n   sub rdi, rax\n   movsxd r12, r11d\n   movsxd rax, r9d\n   sub r12, rax\n   imul rdi, r12\n"
        "movsxd r12, dword ptr [rsi + 8]\n   movsxd rax, dword ptr [rsi]\n   sub r12, rax\n   jle 10f\n"
        "movsxd rax, dword ptr [rsi + 12]\n   movsxd rsi, dword ptr [rsi + 4]\n   sub rax, rsi\n   jle 10f\n   imul r12, rax\n   jmp 11f\n"
        "10: xor r12d, r12d\n"
        "11: mov rax, rdi\n   sub rax, [rsp]\n   mov rsi, [rsp + 8]\n   test rsi, rsi\n   js 2f\n"
        "add r12, rsi\n   jc 3f\n   cmp rax, r12\n   jbe 3f\n   inc edx\n   jmp 1b\n"
        "2:  neg rsi\n   sub r12, rsi\n   jb 20f\n   cmp rax, r12\n   jbe 3f\n"
        "20: inc edx\n   jmp 1b\n"
        "3:  mov r13d, r8d\n   mov r14d, r9d\n   mov r15d, r10d\n   mov ebx, r11d\n   mov [rsp], rdi\n   dec ecx\n"
        "mov eax, ecx\n   shl eax, 4\n   mov rsi, [rbp + rax]\n   mov rdi, [rbp + rax + 8]\n"
        "mov eax, edx\n   shl eax, 4\n   mov [rbp + rax], rsi\n   mov [rbp + rax + 8], rdi\n   xor edx, edx\n   jmp 1b\n"
        "4:  cmp ecx, 8\n   jae 5f\n   mov eax, ecx\n   shl eax, 4\n   mov [rbp + rax], r13d\n   mov [rbp + rax + 4], r14d\n"
        "mov [rbp + rax + 8], r15d\n   mov [rbp + rax + 12], ebx\n   inc ecx\n   mov [rbp + 128], ecx\n   jmp 99f\n"
        "5:  xor edx, edx\n   xor edi, edi\n   mov rax, -1\n   mov [rsp + 16], rax\n"
        "6:  mov eax, edx\n   shl eax, 4\n   lea rsi, [rbp + rax]\n"
        "mov r8d, [rsi]\n   cmp r8d, r13d\n   cmovg r8d, r13d\n   mov r9d, [rsi + 4]\n   cmp r9d, r14d\n   cmovg r9d, r14d\n"
        "mov r10d, [rsi + 8]\n   cmp r10d, r15d\n   cmovl r10d, r15d\n   mov r11d, [rsi + 12]\n   cmp r11d, ebx\n   cmovl r11d, ebx\n"
        "movsxd r12, r10d\n   movsxd rax, r8d\n   sub r12, rax\n   movsxd rax, r11d\n   movsxd r8, r9d\n   sub rax, r8\n   imul r12, rax\n"
        "movsxd r8, dword ptr [rsi + 8]\n   movsxd rax, dword ptr [rsi]\n   sub r8, rax\n   jle 60f\n"
        "movsxd rax, dword ptr [rsi + 12]\n   movsxd rsi, dword ptr [rsi + 4]\n   sub rax, rsi\n   jle 60f\n   imul r8, rax\n   jmp 61f\n"
        "60: xor r8d, r8d\n"
        "61: sub r12, r8\n   cmp r12, [rsp + 16]\n   jae 7f\n   mov [rsp + 16], r12\n   mov edi, edx\n"
        "7:  inc edx\n   cmp edx, 8\n   jb 6b\n"
        "shl edi, 4\n   lea rsi, [rbp + rdi]\n"
        "cmp r13d, [rsi]\n   cmovg r13d, [rsi]\n   cmp r14d, [rsi + 4]\n   cmovg r14d, [rsi + 4]\n"
        "cmp r15d, [rsi + 8]\n   cmovl r15d, [rsi + 8]\n   cmp ebx, [rsi + 12]\n   cmovl ebx, [rsi + 12]\n"
        "mov [rsi], r13d\n   mov [rsi + 4], r14d\n   mov [rsi + 8], r15d\n   mov [rsi + 12], ebx\n   mov [rbp + 128], ecx\n"
        "99: add rsp, 24\n" DMG_X64_POP_FULL DMG_ASM_RET
        DMG_ASM_END(dmg_add)

        /* dmg_subtract(A0=d, A1=cut) -> eax */
        DMG_ASM_FUNC(dmg_subtract)
        DMG_X64_PUSH_FULL DMG_X64_ARGS2_FULL
        "sub rsp, 136\n   test rbp, rbp\n   jz 99f\n   mov ecx, [rbp + 128]\n   test ecx, ecx\n   jz 98f\n"
        "mov eax, 8\n   cmp ecx, eax\n   cmova ecx, eax\n   test rbx, rbx\n   jz 98f\n"
        "mov r12d, [rbx]\n   mov r13d, [rbx + 4]\n   mov r14d, [rbx + 8]\n   mov r15d, [rbx + 12]\n"
        "cmp r14d, r12d\n   jle 98f\n   cmp r15d, r13d\n   jle 98f\n"
        "mov [rsp + 128], ecx\n   mov dword ptr [rsp + 132], 0\n   xor ebx, ebx\n   mov rdi, rsp\n"
        "1:  mov eax, [rsp + 132]\n   cmp eax, [rsp + 128]\n   jae 8f\n   inc dword ptr [rsp + 132]\n   shl eax, 4\n"
        "mov r8d, [rbp + rax]\n   mov r9d, [rbp + rax + 4]\n   mov r10d, [rbp + rax + 8]\n   mov r11d, [rbp + rax + 12]\n"
        "cmp r10d, r12d\n   jle 2f\n   cmp r8d, r14d\n   jge 2f\n   cmp r11d, r13d\n   jle 2f\n   cmp r9d, r15d\n   jge 2f\n"
        "cmp r10d, r8d\n   jle 1b\n   cmp r11d, r9d\n   jle 1b\n"
        "mov eax, r8d\n   cmp eax, r12d\n   cmovl eax, r12d\n   mov ecx, r9d\n   cmp ecx, r13d\n   cmovl ecx, r13d\n"
        "mov edx, r10d\n   cmp edx, r14d\n   cmovg edx, r14d\n   mov esi, r11d\n   cmp esi, r15d\n   cmovg esi, r15d\n"
        "cmp r9d, ecx\n   jge 3f\n   cmp ebx, 8\n   jae 99f\n   mov [rdi], r8d\n   mov [rdi + 4], r9d\n   mov [rdi + 8], r10d\n   mov [rdi + 12], ecx\n   add rdi, 16\n   inc ebx\n"
        "3:  cmp esi, r11d\n   jge 4f\n   cmp ebx, 8\n   jae 99f\n   mov [rdi], r8d\n   mov [rdi + 4], esi\n   mov [rdi + 8], r10d\n   mov [rdi + 12], r11d\n   add rdi, 16\n   inc ebx\n"
        "4:  cmp r8d, eax\n   jge 5f\n   cmp ebx, 8\n   jae 99f\n   mov [rdi], r8d\n   mov [rdi + 4], ecx\n   mov [rdi + 8], eax\n   mov [rdi + 12], esi\n   add rdi, 16\n   inc ebx\n"
        "5:  cmp edx, r10d\n   jge 1b\n   cmp ebx, 8\n   jae 99f\n   mov [rdi], edx\n   mov [rdi + 4], ecx\n   mov [rdi + 8], r10d\n   mov [rdi + 12], esi\n   add rdi, 16\n   inc ebx\n   jmp 1b\n"
        "2:  cmp ebx, 8\n   jae 99f\n   mov [rdi], r8d\n   mov [rdi + 4], r9d\n   mov [rdi + 8], r10d\n   mov [rdi + 12], r11d\n   add rdi, 16\n   inc ebx\n   jmp 1b\n"
        "8:  mov [rbp + 128], ebx\n   xor ecx, ecx\n   mov rsi, rsp\n"
        "80: cmp ecx, ebx\n   jae 98f\n   mov rax, [rsi]\n   mov rdx, [rsi + 8]\n   mov [rbp], rax\n   mov [rbp + 8], rdx\n   add rsi, 16\n   add rbp, 16\n   inc ecx\n   jmp 80b\n"
        "98: mov eax, 1\n   jmp 95f\n"
        "99: xor eax, eax\n"
        "95: add rsp, 136\n" DMG_X64_POP_FULL DMG_ASM_RET
        DMG_ASM_END(dmg_subtract)

        ".att_syntax prefix\n"
);

#elif defined(DMG_OPS_RISCV64)

__asm__(
        DMG_ASM_SECTION

        /* dmg_rect_empty(a0=r) -> a0 */
        DMG_ASM_FUNC(dmg_rect_empty)
        "beqz a0, 1f\n   lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "ble t2, t0, 1f\n   ble t3, t1, 1f\n   li a0, 0\n" DMG_ASM_RET
        "1:  li a0, 1\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_empty)

        /* dmg_rect_area(a0=r) -> a0 */
        DMG_ASM_FUNC(dmg_rect_area)
        "beqz a0, 1f\n   lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "sub t0, t2, t0\n   blez t0, 1f\n   sub t1, t3, t1\n   blez t1, 1f\n   mul a0, t0, t1\n"
        "bgez a0, 2f\n   li a0, -1\n   srli a0, a0, 1\n"
        "2:  " DMG_ASM_RET
        "1:  li a0, 0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_area)

        /* dmg_rect_join(a0=a, a1=b, a2=out) */
        DMG_ASM_FUNC(dmg_rect_join)
        "beqz a0, 5f\n   beqz a1, 5f\n   beqz a2, 5f\n   lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "lw t4, 0(a1)\n   lw t5, 4(a1)\n   lw t6, 8(a1)\n   lw a3, 12(a1)\n"
        "ble t0, t4, 1f\n   mv t0, t4\n"
        "1:  ble t1, t5, 2f\n   mv t1, t5\n"
        "2:  bge t2, t6, 3f\n   mv t2, t6\n"
        "3:  bge t3, a3, 4f\n   mv t3, a3\n"
        "4:  sw t0, 0(a2)\n   sw t1, 4(a2)\n   sw t2, 8(a2)\n   sw t3, 12(a2)\n"
        "5:  " DMG_ASM_RET
        DMG_ASM_END(dmg_rect_join)

        /* dmg_rect_meet(a0=a, a1=b, a2=out) -> a0 */
        DMG_ASM_FUNC(dmg_rect_meet)
        "beqz a0, 6f\n   beqz a1, 6f\n   lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "lw t4, 0(a1)\n   lw t5, 4(a1)\n   lw t6, 8(a1)\n   lw a3, 12(a1)\n"
        "bge t0, t4, 1f\n   mv t0, t4\n"
        "1:  bge t1, t5, 2f\n   mv t1, t5\n"
        "2:  ble t2, t6, 3f\n   mv t2, t6\n"
        "3:  ble t3, a3, 4f\n   mv t3, a3\n"
        "4:  ble t2, t0, 6f\n   ble t3, t1, 6f\n   beqz a2, 5f\n   sw t0, 0(a2)\n   sw t1, 4(a2)\n   sw t2, 8(a2)\n   sw t3, 12(a2)\n"
        "5:  li a0, 1\n" DMG_ASM_RET
        "6:  beqz a2, 7f\n   sw zero, 0(a2)\n   sw zero, 4(a2)\n   sw zero, 8(a2)\n   sw zero, 12(a2)\n"
        "7:  li a0, 0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_meet)

        /* dmg_rect_cover(a0=outer, a1=inner) -> a0 */
        DMG_ASM_FUNC(dmg_rect_cover)
        "beqz a1, 2f\n   lw t0, 0(a1)\n   lw t1, 4(a1)\n   lw t2, 8(a1)\n   lw t3, 12(a1)\n   ble t2, t0, 2f\n   ble t3, t1, 2f\n"
        "beqz a0, 1f\n   lw t4, 0(a0)\n   lw t5, 4(a0)\n   lw t6, 8(a0)\n   lw a3, 12(a0)\n"
        "bgt t4, t0, 1f\n   bgt t5, t1, 1f\n   blt t6, t2, 1f\n   blt a3, t3, 1f\n"
        "2:  li a0, 1\n" DMG_ASM_RET
        "1:  li a0, 0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_cover)

        /* dmg_rect_cut(a0=r, a1=cut, a2=out[4]) -> a0 */
        DMG_ASM_FUNC(dmg_rect_cut)
        "beqz a0, 9f\n   beqz a2, 9f\n   lw a3, 0(a0)\n   lw a4, 4(a0)\n   lw a5, 8(a0)\n   lw a6, 12(a0)\n"
        "ble a5, a3, 9f\n   ble a6, a4, 9f\n   beqz a1, 8f\n"
        "lw t0, 0(a1)\n   lw t1, 4(a1)\n   lw t2, 8(a1)\n   lw t3, 12(a1)\n"
        "bge t0, a3, 10f\n   mv t0, a3\n"
        "10: bge t1, a4, 11f\n   mv t1, a4\n"
        "11: ble t2, a5, 12f\n   mv t2, a5\n"
        "12: ble t3, a6, 13f\n   mv t3, a6\n"
        "13: ble t2, t0, 8f\n   ble t3, t1, 8f\n   li a7, 0\n"
        "bge a4, t1, 1f\n   sw a3, 0(a2)\n   sw a4, 4(a2)\n   sw a5, 8(a2)\n   sw t1, 12(a2)\n   addi a2, a2, 16\n   addi a7, a7, 1\n"
        "1:  bge t3, a6, 2f\n   sw a3, 0(a2)\n   sw t3, 4(a2)\n   sw a5, 8(a2)\n   sw a6, 12(a2)\n   addi a2, a2, 16\n   addi a7, a7, 1\n"
        "2:  bge a3, t0, 3f\n   sw a3, 0(a2)\n   sw t1, 4(a2)\n   sw t0, 8(a2)\n   sw t3, 12(a2)\n   addi a2, a2, 16\n   addi a7, a7, 1\n"
        "3:  bge t2, a5, 4f\n   sw t2, 0(a2)\n   sw t1, 4(a2)\n   sw a5, 8(a2)\n   sw t3, 12(a2)\n   addi a7, a7, 1\n"
        "4:  mv a0, a7\n" DMG_ASM_RET
        "8:  sw a3, 0(a2)\n   sw a4, 4(a2)\n   sw a5, 8(a2)\n   sw a6, 12(a2)\n   li a0, 1\n" DMG_ASM_RET
        "9:  li a0, 0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_rect_cut)

        /* dmg_add(a0=d, a1=r, a2=slack) */
        DMG_ASM_FUNC(dmg_add)
        "beqz a0, 99f\n   beqz a1, 99f\n   lw t0, 0(a1)\n   lw t1, 4(a1)\n   lw t2, 8(a1)\n   lw t3, 12(a1)\n"
        "sub t4, t2, t0\n   blez t4, 99f\n   sub t5, t3, t1\n   blez t5, 99f\n"
        "addi sp, sp, -64\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n"
        "sd s4, 32(sp)\n   sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n"
        "mv s0, t0\n   mv s1, t1\n   mv s2, t2\n   mv s3, t3\n   mul s4, t4, t5\n"
        "lwu s5, 128(a0)\n   li t0, 8\n   bleu s5, t0, 0f\n   mv s5, t0\n"
        "0:  li s6, 0\n"
        "1:  bgeu s6, s5, 4f\n   slli a1, s6, 4\n   add a1, a0, a1\n"
        "lw t0, 0(a1)\n   lw t1, 4(a1)\n   lw t2, 8(a1)\n   lw t3, 12(a1)\n"
        "mv a3, s0\n   ble a3, t0, 10f\n   mv a3, t0\n"
        "10: mv a4, s1\n   ble a4, t1, 11f\n   mv a4, t1\n"
        "11: mv a5, s2\n   bge a5, t2, 12f\n   mv a5, t2\n"
        "12: mv a6, s3\n   bge a6, t3, 13f\n   mv a6, t3\n"
        "13: sub t4, a5, a3\n   sub t5, a6, a4\n   mul s7, t4, t5\n"
        "sub t4, t2, t0\n   bgtz t4, 14f\n   li t4, 0\n"
        "14: sub t5, t3, t1\n   bgtz t5, 15f\n   li t5, 0\n"
        "15: mul t4, t4, t5\n   sub t6, s7, s4\n   bltz a2, 2f\n"
        "add t5, t4, a2\n   bltu t5, t4, 3f\n   bleu t6, t5, 3f\n   addi s6, s6, 1\n   j 1b\n"
        "2:  neg t5, a2\n   bltu t4, t5, 20f\n   sub t5, t4, t5\n   bleu t6, t5, 3f\n"
        "20: addi s6, s6, 1\n   j 1b\n"
        "3:  mv s0, a3\n   mv s1, a4\n   mv s2, a5\n   mv s3, a6\n   mv s4, s7\n   addi s5, s5, -1\n"
        "slli t0, s5, 4\n   add t0, a0, t0\n   lw t1, 0(t0)\n   lw t2, 4(t0)\n   lw t3, 8(t0)\n   lw t4, 12(t0)\n"
        "sw t1, 0(a1)\n   sw t2, 4(a1)\n   sw t3, 8(a1)\n   sw t4, 12(a1)\n   li s6, 0\n   j 1b\n"
        "4:  li t0, 8\n   bgeu s5, t0, 5f\n   slli a1, s5, 4\n   add a1, a0, a1\n"
        "sw s0, 0(a1)\n   sw s1, 4(a1)\n   sw s2, 8(a1)\n   sw s3, 12(a1)\n   addi s5, s5, 1\n   sw s5, 128(a0)\n   j 98f\n"
        "5:  li s6, 0\n   li s7, 0\n   li a7, -1\n"
        "6:  slli a1, s6, 4\n   add a1, a0, a1\n   lw t0, 0(a1)\n   lw t1, 4(a1)\n   lw t2, 8(a1)\n   lw t3, 12(a1)\n"
        "mv a3, s0\n   ble a3, t0, 60f\n   mv a3, t0\n"
        "60: mv a4, s1\n   ble a4, t1, 61f\n   mv a4, t1\n"
        "61: mv a5, s2\n   bge a5, t2, 62f\n   mv a5, t2\n"
        "62: mv a6, s3\n   bge a6, t3, 63f\n   mv a6, t3\n"
        "63: sub t4, a5, a3\n   sub t5, a6, a4\n   mul t6, t4, t5\n"
        "sub t4, t2, t0\n   bgtz t4, 64f\n   li t4, 0\n"
        "64: sub t5, t3, t1\n   bgtz t5, 65f\n   li t5, 0\n"
        "65: mul t4, t4, t5\n   sub t6, t6, t4\n   bgeu t6, a7, 7f\n   mv a7, t6\n   mv s7, s6\n"
        "7:  addi s6, s6, 1\n   li t0, 8\n   bltu s6, t0, 6b\n"
        "slli a1, s7, 4\n   add a1, a0, a1\n   lw t0, 0(a1)\n   lw t1, 4(a1)\n   lw t2, 8(a1)\n   lw t3, 12(a1)\n"
        "ble s0, t0, 70f\n   mv s0, t0\n"
        "70: ble s1, t1, 71f\n   mv s1, t1\n"
        "71: bge s2, t2, 72f\n   mv s2, t2\n"
        "72: bge s3, t3, 73f\n   mv s3, t3\n"
        "73: sw s0, 0(a1)\n   sw s1, 4(a1)\n   sw s2, 8(a1)\n   sw s3, 12(a1)\n   sw s5, 128(a0)\n"
        "98: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n"
        "ld s4, 32(sp)\n   ld s5, 40(sp)\n   ld s6, 48(sp)\n   ld s7, 56(sp)\n   addi sp, sp, 64\n"
        "99: " DMG_ASM_RET
        DMG_ASM_END(dmg_add)

        /* dmg_subtract(a0=d, a1=cut) -> a0 */
        DMG_ASM_FUNC(dmg_subtract)
        "beqz a0, 99f\n   lwu t0, 128(a0)\n   beqz t0, 98f\n   li t1, 8\n   bleu t0, t1, 0f\n   mv t0, t1\n"
        "0:  beqz a1, 98f\n   lw t1, 0(a1)\n   lw t2, 4(a1)\n   lw t3, 8(a1)\n   lw t4, 12(a1)\n"
        "ble t3, t1, 98f\n   ble t4, t2, 98f\n"
        "addi sp, sp, -176\n   sd s0, 128(sp)\n   sd s1, 136(sp)\n   sd s2, 144(sp)\n   sd s3, 152(sp)\n   sd s4, 160(sp)\n   sd s5, 168(sp)\n"
        "mv s0, t1\n   mv s1, t2\n   mv s2, t3\n   mv s3, t4\n   mv s4, t0\n   li s5, 0\n   li a3, 0\n   mv a4, sp\n   li a6, 8\n"
        "1:  bgeu a3, s4, 8f\n   slli a2, a3, 4\n   add a2, a0, a2\n   lw t0, 0(a2)\n   lw t1, 4(a2)\n   lw t2, 8(a2)\n   lw t3, 12(a2)\n   addi a3, a3, 1\n"
        "ble t2, s0, 2f\n   bge t0, s2, 2f\n   ble t3, s1, 2f\n   bge t1, s3, 2f\n   ble t2, t0, 1b\n   ble t3, t1, 1b\n"
        "mv t4, t0\n   bge t4, s0, 10f\n   mv t4, s0\n"
        "10: mv t5, t1\n   bge t5, s1, 11f\n   mv t5, s1\n"
        "11: mv t6, t2\n   ble t6, s2, 12f\n   mv t6, s2\n"
        "12: mv a5, t3\n   ble a5, s3, 13f\n   mv a5, s3\n"
        "13: bge t1, t5, 3f\n   bgeu s5, a6, 9f\n   sw t0, 0(a4)\n   sw t1, 4(a4)\n   sw t2, 8(a4)\n   sw t5, 12(a4)\n   addi a4, a4, 16\n   addi s5, s5, 1\n"
        "3:  bge a5, t3, 4f\n   bgeu s5, a6, 9f\n   sw t0, 0(a4)\n   sw a5, 4(a4)\n   sw t2, 8(a4)\n   sw t3, 12(a4)\n   addi a4, a4, 16\n   addi s5, s5, 1\n"
        "4:  bge t0, t4, 5f\n   bgeu s5, a6, 9f\n   sw t0, 0(a4)\n   sw t5, 4(a4)\n   sw t4, 8(a4)\n   sw a5, 12(a4)\n   addi a4, a4, 16\n   addi s5, s5, 1\n"
        "5:  bge t6, t2, 1b\n   bgeu s5, a6, 9f\n   sw t6, 0(a4)\n   sw t5, 4(a4)\n   sw t2, 8(a4)\n   sw a5, 12(a4)\n   addi a4, a4, 16\n   addi s5, s5, 1\n   j 1b\n"
        "2:  bgeu s5, a6, 9f\n   sw t0, 0(a4)\n   sw t1, 4(a4)\n   sw t2, 8(a4)\n   sw t3, 12(a4)\n   addi a4, a4, 16\n   addi s5, s5, 1\n   j 1b\n"
        "8:  sw s5, 128(a0)\n   mv a4, sp\n   li a3, 0\n"
        "80: bgeu a3, s5, 81f\n   lw t0, 0(a4)\n   lw t1, 4(a4)\n   lw t2, 8(a4)\n   lw t3, 12(a4)\n   addi a4, a4, 16\n"
        "sw t0, 0(a0)\n   sw t1, 4(a0)\n   sw t2, 8(a0)\n   sw t3, 12(a0)\n   addi a0, a0, 16\n   addi a3, a3, 1\n   j 80b\n"
        "81: li a0, 1\n   j 95f\n"
        "9:  li a0, 0\n"
        "95: ld s0, 128(sp)\n   ld s1, 136(sp)\n   ld s2, 144(sp)\n   ld s3, 152(sp)\n   ld s4, 160(sp)\n   ld s5, 168(sp)\n   addi sp, sp, 176\n" DMG_ASM_RET
        "98: li a0, 1\n" DMG_ASM_RET
        "99: li a0, 0\n" DMG_ASM_RET
        DMG_ASM_END(dmg_subtract)
);

#endif

#endif /* COMPOSITOR_DAMAGE_OPS_ASM_IMPLEMENTATION && !COMPOSITOR_DAMAGE_OPS_ASM_EMITTED */
