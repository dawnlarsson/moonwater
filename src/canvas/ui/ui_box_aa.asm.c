/*
        Moonwater Canvas UI anti-aliased rounded-rect & ring span primitives in pure assembly:
          - ui_box_aa_radii       : CSS border-radius proportional clamping (overflow-free 64-bit)
          - ui_box_aa_arc         : unit-circle quarter-arc sqrt(2^32 - u^2) for u in [0, 65536]
          - ui_box_aa_insets      : 16-subline corner inset batch (NEON/SSE2 FP64, RV64IM exact isqrt)
          - ui_box_aa_spans_radii : 16-subline row span generator with straight-middle fast path
          - ui_box_aa_spans_cover : pixel coverage sum across 16 sublines (NEON/SSE2/RV64I)
          - ui_box_aa_span_ends   : clipped straight-row span endpoints & edge coverage

        Pure arm64 (NEON), x86-64 (SSE2; SysV ELF / Mach-O / Windows x64 COFF), and
        riscv64 (RV64I/M) assembly. Zero C function bodies, zero allocations, zero
        syscalls, zero .data/.rodata lookup tables, zero external calls.

        STB-style: include for declarations; define CANVAS_UI_BOX_AA_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CANVAS_UI_BOX_AA_ASM_H
#define MOONWATER_CANVAS_UI_BOX_AA_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  ui_aa_u8;
typedef p16 ui_aa_u16;
typedef p32 ui_aa_u32;
typedef b32 ui_aa_i32;
typedef p64 ui_aa_u64;
typedef b64 ui_aa_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  ui_aa_u8;
typedef __UINT16_TYPE__ ui_aa_u16;
typedef __UINT32_TYPE__ ui_aa_u32;
typedef __INT32_TYPE__  ui_aa_i32;
typedef __UINT64_TYPE__ ui_aa_u64;
typedef __INT64_TYPE__  ui_aa_i64;
#else
#include <stdint.h>
typedef uint8_t  ui_aa_u8;
typedef uint16_t ui_aa_u16;
typedef uint32_t ui_aa_u32;
typedef int32_t  ui_aa_i32;
typedef uint64_t ui_aa_u64;
typedef int64_t  ui_aa_i64;
#endif

#define UI_BOX_AA_SUB  16
#define UI_BOX_AA_FULL (256 * UI_BOX_AA_SUB)

/* 24 bytes — byte-for-byte identical to ui.c's struct ui_box */
struct ui_aa_box {
        ui_aa_i32 x0, y0, x1, y1; /* +0, +4, +8, +12 */
        ui_aa_i32 rx, ry;         /* +16, +20        */
};

/* 160 bytes — byte-for-byte identical to ui.c's struct ui_spans */
struct ui_aa_spans {
        ui_aa_i32 lo, hi, a, b;                           /* +0, +4, +8, +12 */
        ui_aa_u32 inside;                                 /* +16             */
        ui_aa_i32 count;                                  /* +20             */
        ui_aa_i32 left[16]  __attribute__((aligned(16))); /* +32 .. +95      */
        ui_aa_i32 right[16] __attribute__((aligned(16))); /* +96 .. +159     */
};

#ifdef offset_of
#define UIAA_OFF(T, M) offset_of(T, M)
#else
#define UIAA_OFF(T, M) ((ui_aa_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct ui_aa_box) == 24, "ui_aa_box size");
_Static_assert(UIAA_OFF(struct ui_aa_box, x0) == 0  && UIAA_OFF(struct ui_aa_box, y0) == 4 &&
               UIAA_OFF(struct ui_aa_box, x1) == 8  && UIAA_OFF(struct ui_aa_box, y1) == 12 &&
               UIAA_OFF(struct ui_aa_box, rx) == 16 && UIAA_OFF(struct ui_aa_box, ry) == 20,
               "ui_aa_box layout");
_Static_assert(sizeof(struct ui_aa_spans) == 160, "ui_aa_spans size");
_Static_assert(UIAA_OFF(struct ui_aa_spans, lo)     == 0  && UIAA_OFF(struct ui_aa_spans, hi)    == 4 &&
               UIAA_OFF(struct ui_aa_spans, a)      == 8  && UIAA_OFF(struct ui_aa_spans, b)     == 12 &&
               UIAA_OFF(struct ui_aa_spans, inside) == 16 && UIAA_OFF(struct ui_aa_spans, count) == 20 &&
               UIAA_OFF(struct ui_aa_spans, left)   == 32 && UIAA_OFF(struct ui_aa_spans, right) == 96,
               "ui_aa_spans layout");

void      ui_box_aa_radii(const struct ui_aa_box *box, ui_aa_i32 *rx, ui_aa_i32 *ry);
ui_aa_u64 ui_box_aa_arc(ui_aa_u64 u);
void      ui_box_aa_insets(const ui_aa_i32 *dy, ui_aa_i32 rx, ui_aa_i32 ry, ui_aa_i32 *inset);
ui_aa_i32 ui_box_aa_spans_radii(const struct ui_aa_box *box, ui_aa_i32 rx, ui_aa_i32 ry,
                                ui_aa_i32 y, ui_aa_i32 first, ui_aa_i32 width,
                                struct ui_aa_spans *s);
ui_aa_u32 ui_box_aa_spans_cover(const struct ui_aa_spans *s, ui_aa_i32 x);
ui_aa_i32 ui_box_aa_span_ends(ui_aa_i32 first, ui_aa_i32 width, ui_aa_i32 left, ui_aa_i32 right,
                              ui_aa_i32 *p0, ui_aa_i32 *p1, ui_aa_u32 *c0, ui_aa_u32 *c1);

#endif /* MOONWATER_CANVAS_UI_BOX_AA_ASM_H */

#ifdef CANVAS_UI_BOX_AA_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define UIAA_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define UIAA_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define UIAA_RISCV64 1
# else
#  error "src/canvas/ui/ui_box_aa.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define UIAA_ARM64 1
# elif defined(X64) && X64
#  define UIAA_X64 1
# elif defined(RISCV64) && RISCV64
#  define UIAA_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define UIAA_ASM_SECTION   ASM_SECTION
# define UIAA_ASM_FUNC(n)   ASM_FUNC(n)
# define UIAA_ASM_END(n)    ASM_END(n)
# define UIAA_ASM_RET       ASM_RET
#else
# define UIAA_ASM_SECTION   ".text\n"
# define UIAA_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define UIAA_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define UIAA_ASM_END(n)   ""
# elif defined(_WIN32)
#  define UIAA_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define UIAA_ASM_END(n)   ""
# else
#  if defined(UIAA_ARM64)
#   define UIAA_ASM_FTYPE   "%function"
#  else
#   define UIAA_ASM_FTYPE   "@function"
#  endif
#  define UIAA_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " UIAA_ASM_FTYPE "\n" #n ":\n"
#  define UIAA_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(UIAA_ARM64)

__asm__(
        UIAA_ASM_SECTION

        /* ui_box_aa_radii(x0=box, x1=rx, x2=ry) */
        UIAA_ASM_FUNC(ui_box_aa_radii)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x2, 9f\n"
        "ldrsw x3, [x0, #0]\n   ldrsw x4, [x0, #4]\n   ldrsw x5, [x0, #8]\n   ldrsw x6, [x0, #12]\n"
        "subs x5, x5, x3\n   b.le 8f\n   subs x6, x6, x4\n   b.le 8f\n"
        "ldrsw x3, [x1]\n   ldrsw x4, [x2]\n"
        "cmp x3, #0\n   b.ge 1f\n   mov x3, #0\n   str wzr, [x1]\n"
        "1:  cmp x4, #0\n   b.ge 2f\n   mov x4, #0\n   str wzr, [x2]\n"
        "2:  mov x7, #65536\n   cbz x3, 3f\n   lsl x8, x3, #1\n   cmp x8, x5\n   b.ls 3f\n"
        "lsl x9, x5, #16\n   udiv x7, x9, x8\n"
        "3:  cbz x4, 4f\n   lsl x8, x4, #1\n   cmp x8, x6\n   b.ls 4f\n"
        "lsl x9, x6, #16\n   udiv x9, x9, x8\n   cmp x9, x7\n   csel x7, x9, x7, lo\n"
        "4:  cmp x7, #65536\n   b.hs 9f\n"
        "mul x3, x3, x7\n   lsr x3, x3, #16\n   str w3, [x1]\n"
        "mul x4, x4, x7\n   lsr x4, x4, #16\n   str w4, [x2]\n"
        "9:  " UIAA_ASM_RET
        "8:  str wzr, [x1]\n   str wzr, [x2]\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_radii)

        /* ui_box_aa_arc(x0=u) -> x0 */
        UIAA_ASM_FUNC(ui_box_aa_arc)
        "cmp x0, #65536\n   b.hs 1f\n   mul x1, x0, x0\n   mov x2, #1\n"
        "sub x1, xzr, x1\n   add x1, x1, x2, lsl #32\n   ucvtf d0, x1\n   fsqrt d0, d0\n   fcvtzu x0, d0\n" UIAA_ASM_RET
        "1:  mov x0, #0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_arc)

        /* ui_box_aa_insets(x0=dy, w1=rx, w2=ry, x3=inset) — preserves x6..x15, x17 for internal call */
        UIAA_ASM_FUNC(ui_box_aa_insets)
        "Lui_box_aa_insets_in:\n"
        "cbz x0, 9f\n   cbz x3, 9f\n   cmp w1, #0\n   b.le 8f\n   cmp w2, #0\n   b.le 8f\n"
        "sxtw x4, w1\n   scvtf d3, x4\n   movz x16, #0x3ef0, lsl #48\n   fmov d7, x16\n"
        "fmul d3, d3, d7\n   dup v3.2d, v3.d[0]\n"
        "sxtw x5, w2\n   scvtf d4, x5\n   dup v4.2d, v4.d[0]\n"
        "movz x16, #0x40f0, lsl #48\n   dup v5.2d, x16\n"
        "movz x16, #0x41f0, lsl #48\n   dup v6.2d, x16\n"
        "dup v17.2s, w2\n   movi v18.2s, #0\n   mov x4, #0\n"
        "1:  ldr d0, [x0, x4]\n   smax v0.2s, v0.2s, v18.2s\n   smin v0.2s, v0.2s, v17.2s\n"
        "sxtl v0.2d, v0.2s\n   scvtf v0.2d, v0.2d\n   fmul v0.2d, v0.2d, v5.2d\n   fdiv v0.2d, v0.2d, v4.2d\n"
        "frintz v1.2d, v0.2d\n   fmul v1.2d, v1.2d, v1.2d\n   fsub v2.2d, v6.2d, v1.2d\n"
        "fsqrt v2.2d, v2.2d\n   frintz v2.2d, v2.2d\n   fmul v2.2d, v2.2d, v3.2d\n"
        "fcvtzs v2.2d, v2.2d\n   xtn v2.2s, v2.2d\n   str d2, [x3, x4]\n"
        "add x4, x4, #8\n   cmp x4, #64\n   b.lo 1b\n"
        "9:  " UIAA_ASM_RET
        "8:  movi v0.16b, #0\n   stp q0, q0, [x3]\n   stp q0, q0, [x3, #32]\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_insets)

        /* ui_box_aa_spans_radii(x0=box, w1=rx, w2=ry, w3=y, w4=first, w5=width, x6=s) -> w0 */
        UIAA_ASM_FUNC(ui_box_aa_spans_radii)
        "cbz x6, 99f\n   str wzr, [x6, #20]\n   cbz x0, 99f\n   cmp w5, #0\n   b.le 99f\n"
        "ldrsw x7, [x0, #0]\n   ldrsw x8, [x0, #4]\n   ldrsw x9, [x0, #8]\n   ldrsw x10, [x0, #12]\n"
        "cmp x9, x7\n   b.le 99f\n   sxtw x11, w3\n   lsl x11, x11, #8\n   add x12, x11, #256\n"
        "cmp x12, x8\n   b.le 99f\n   cmp x11, x10\n   b.ge 99f\n"
        "sxtw x13, w4\n   lsl x13, x13, #8\n   sxtw x14, w5\n   lsl x14, x14, #8\n   sxtw x15, w2\n"
        "cmp x11, x8\n   b.lt 2f\n   cmp x12, x10\n   b.gt 2f\n"
        "add x16, x8, x15\n   cmp x11, x16\n   b.lt 2f\n   sub x16, x10, x15\n   cmp x12, x16\n   b.gt 2f\n"
        "sub x7, x7, x13\n   cmp x7, #0\n   csel x7, xzr, x7, lt\n"
        "sub x9, x9, x13\n   cmp x9, x14\n   csel x9, x14, x9, gt\n   cmp x9, x7\n   b.le 99f\n"
        "dup v0.4s, w7\n   dup v1.4s, w9\n   stp q0, q0, [x6, #32]\n   stp q0, q0, [x6, #64]\n"
        "stp q1, q1, [x6, #96]\n   stp q1, q1, [x6, #128]\n"
        "asr x10, x7, #8\n   add x11, x9, #255\n   asr x11, x11, #8\n"
        "add x12, x7, #255\n   asr x12, x12, #8\n   asr x13, x9, #8\n"
        "cmp x13, x12\n   b.gt 1f\n   mov x12, x11\n   mov x13, x11\n"
        "1:  stp w10, w11, [x6, #0]\n   stp w12, w13, [x6, #8]\n"
        "mov w14, #4096\n   mov w15, #16\n   stp w14, w15, [x6, #16]\n   mov w0, #1\n" UIAA_ASM_RET
        "2:  sub sp, sp, #144\n   str x30, [sp, #0]\n   mov w17, #0\n"
        "add x16, x8, x15\n   sub x12, x10, x15\n   add x4, x11, #8\n   mov x5, #0\n   add x11, sp, #16\n"
        "3:  mov w0, #0\n   cmp x4, x8\n   b.lt 4f\n   cmp x4, x10\n   b.ge 4f\n"
        "cmp x4, x16\n   b.ge 5f\n   sub x0, x16, x4\n   b 6f\n"
        "5:  cmp x4, x12\n   b.lt 6f\n   sub x0, x4, x12\n   add x0, x0, #1\n   b 6f\n"
        "4:  mov w3, #1\n   lsl w3, w3, w5\n   orr w17, w17, w3\n"
        "6:  str w0, [x11, x5, lsl #2]\n   add x4, x4, #16\n   add x5, x5, #1\n   cmp x5, #16\n   b.lo 3b\n"
        "cmp w1, #0\n   b.le 7f\n   cmp w2, #0\n   b.le 7f\n"
        "sxtw x4, w1\n   add x7, x7, x4\n   sub x9, x9, x4\n"
        "add x0, sp, #16\n   add x3, sp, #80\n   bl Lui_box_aa_insets_in\n   b 8f\n"
        "7:  movi v0.16b, #0\n   stp q0, q0, [sp, #80]\n   stp q0, q0, [sp, #112]\n"
        "8:  mov w0, #0\n   mov x1, #0\n   add x2, sp, #80\n"
        "10: lsr w3, w17, w1\n   tbnz w3, #0, 12f\n   ldrsw x3, [x2, x1, lsl #2]\n"
        "sub x4, x7, x3\n   add x5, x9, x3\n   cmp x5, x4\n   b.le 12f\n"
        "sub x4, x4, x13\n   cmp x4, #0\n   csel x4, xzr, x4, lt\n"
        "sub x5, x5, x13\n   cmp x5, x14\n   csel x5, x14, x5, gt\n   cmp x5, x4\n   b.le 12f\n"
        "cbnz w0, 11f\n   mov x8, x4\n   mov x11, x4\n   mov x10, x5\n   mov x12, x5\n   b 13f\n"
        "11: cmp x4, x8\n   csel x8, x4, x8, lt\n   cmp x5, x10\n   csel x10, x5, x10, gt\n"
        "cmp x4, x11\n   csel x11, x4, x11, gt\n   cmp x5, x12\n   csel x12, x5, x12, lt\n"
        "13: add x15, x6, #32\n   str w4, [x15, x0, lsl #2]\n"
        "add x15, x6, #96\n   str w5, [x15, x0, lsl #2]\n   add w0, w0, #1\n"
        "12: add x1, x1, #1\n   cmp x1, #16\n   b.lo 10b\n"
        "mov w1, w0\n"
        "14: cmp w1, #16\n   b.hs 15f\n   add x15, x6, #32\n   str wzr, [x15, x1, lsl #2]\n"
        "add x15, x6, #96\n   str wzr, [x15, x1, lsl #2]\n   add w1, w1, #1\n   b 14b\n"
        "15: ldr x30, [sp, #0]\n   add sp, sp, #144\n   str w0, [x6, #20]\n   cbz w0, 99f\n"
        "asr x8, x8, #8\n   add x10, x10, #255\n   asr x10, x10, #8\n"
        "add x11, x11, #255\n   asr x11, x11, #8\n   asr x12, x12, #8\n"
        "cmp x12, x11\n   b.gt 16f\n   mov x11, x10\n   mov x12, x10\n"
        "16: stp w8, w10, [x6, #0]\n   stp w11, w12, [x6, #8]\n"
        "lsl w0, w0, #8\n   str w0, [x6, #16]\n   mov w0, #1\n" UIAA_ASM_RET
        "99: mov w0, #0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_spans_radii)

        /* ui_box_aa_spans_cover(x0=s, w1=x) -> w0 */
        UIAA_ASM_FUNC(ui_box_aa_spans_cover)
        "cbz x0, 9f\n   ldr w2, [x0, #20]\n   cmp w2, #0\n   b.le 9f\n"
        "ldp w2, w3, [x0, #0]\n   cmp w1, w2\n   b.lt 9f\n   cmp w1, w3\n   b.ge 9f\n"
        "ldp w2, w3, [x0, #8]\n   cmp w1, w2\n   b.lt 1f\n   cmp w1, w3\n   b.ge 1f\n"
        "ldr w0, [x0, #16]\n" UIAA_ASM_RET
        "1:  lsl w1, w1, #8\n   add w2, w1, #256\n   dup v0.4s, w1\n   dup v1.4s, w2\n"
        "movi v2.4s, #0\n   movi v3.4s, #0\n   add x2, x0, #32\n   add x3, x0, #96\n   mov w4, #4\n"
        "2:  ldr q4, [x2], #16\n   ldr q5, [x3], #16\n   smax v4.4s, v4.4s, v0.4s\n   smin v5.4s, v5.4s, v1.4s\n"
        "sub v5.4s, v5.4s, v4.4s\n   smax v5.4s, v5.4s, v2.4s\n   add v3.4s, v3.4s, v5.4s\n"
        "subs w4, w4, #1\n   b.ne 2b\n   addv s3, v3.4s\n   fmov w0, s3\n" UIAA_ASM_RET
        "9:  mov w0, #0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_spans_cover)

        /* ui_box_aa_span_ends(w0=first, w1=width, w2=left, w3=right, x4=p0, x5=p1, x6=c0, x7=c1) -> w0 */
        UIAA_ASM_FUNC(ui_box_aa_span_ends)
        "cbz x4, 9f\n   cbz x5, 9f\n   cbz x6, 9f\n   cbz x7, 9f\n   cmp w1, #0\n   b.le 9f\n"
        "sxtw x8, w0\n   lsl x8, x8, #8\n   sxtw x9, w1\n   lsl x9, x9, #8\n"
        "sxtw x10, w2\n   sub x10, x10, x8\n   cmp x10, #0\n   csel x10, xzr, x10, lt\n"
        "sxtw x11, w3\n   sub x11, x11, x8\n   cmp x11, x9\n   csel x11, x9, x11, gt\n   cmp x11, x10\n   b.le 9f\n"
        "asr x12, x10, #8\n   sub x13, x11, #1\n   asr x13, x13, #8\n   str w12, [x4]\n   str w13, [x5]\n"
        "cmp x12, x13\n   b.ne 1f\n   sub x14, x11, x10\n   lsl w14, w14, #4\n"
        "str w14, [x6]\n   str w14, [x7]\n   mov w0, #1\n" UIAA_ASM_RET
        "1:  and x14, x10, #255\n   mov x15, #256\n   sub x14, x15, x14\n   lsl w14, w14, #4\n   str w14, [x6]\n"
        "sub x15, x11, x13, lsl #8\n   lsl w15, w15, #4\n   str w15, [x7]\n   mov w0, #1\n" UIAA_ASM_RET
        "9:  mov w0, #0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_span_ends)
);

#elif defined(UIAA_X64)

#if defined(_WIN32)
# define UIAA_LD1      "mov rax, rcx\n"
# define UIAA_LD2      "mov r8, rcx\n   mov r9d, edx\n"
# define UIAA_LD3      "mov r8, rcx\n   mov r9, rdx\n   mov r10, r8\n"
# define UIAA_LD3_RAD  "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define UIAA_LD4_INS  "mov r10d, r8d\n   mov r11, r9\n   mov r8, rcx\n   mov r9d, edx\n"
# define UIAA_PUSH_SP  "push rbx\n   push rbp\n   push rdi\n   push rsi\n   push r12\n   push r13\n   push r14\n   push r15\n" \
                       "mov rbx, rcx\n   mov r12d, edx\n   mov r13d, r8d\n   movsxd r14, r9d\n" \
                       "movsxd r15, dword ptr [rsp + 104]\n   movsxd rbp, dword ptr [rsp + 112]\n   mov rdi, [rsp + 120]\n"
# define UIAA_POP_SP   "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rsi\n   pop rdi\n   pop rbp\n   pop rbx\n"
# define UIAA_PUSH_SE  "push rdi\n   push rsi\n" \
                       "movsxd rax, ecx\n   movsxd rdx, edx\n   movsxd r10, r8d\n   movsxd r11, r9d\n" \
                       "mov r8, [rsp + 56]\n   mov r9, [rsp + 64]\n   mov rdi, [rsp + 72]\n   mov rsi, [rsp + 80]\n"
# define UIAA_POP_SE   "pop rsi\n   pop rdi\n"
#else
# define UIAA_LD1      "mov rax, rdi\n"
# define UIAA_LD2      "mov r8, rdi\n   mov r9d, esi\n"
# define UIAA_LD3_RAD  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define UIAA_LD4_INS  "mov r8, rdi\n   mov r9d, esi\n   mov r10d, edx\n   mov r11, rcx\n"
# define UIAA_PUSH_SP  "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n" \
                       "mov rbx, rdi\n   mov r12d, esi\n   mov r13d, edx\n   movsxd r14, ecx\n" \
                       "movsxd r15, r8d\n   movsxd rbp, r9d\n   mov rdi, [rsp + 56]\n"
# define UIAA_POP_SP   "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
# define UIAA_PUSH_SE  "movsxd rax, edi\n   movsxd r10, edx\n   movsxd r11, ecx\n   movsxd rdx, esi\n" \
                       "mov rdi, [rsp + 8]\n   mov rsi, [rsp + 16]\n"
# define UIAA_POP_SE   ""
#endif

__asm__(
        ".intel_syntax noprefix\n"
        UIAA_ASM_SECTION

        /* ui_box_aa_radii(box, rx, ry) */
        UIAA_ASM_FUNC(ui_box_aa_radii)
        UIAA_LD3_RAD
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test r10, r10\n   jz 9f\n"
        "movsxd rcx, dword ptr [r8 + 8]\n   movsxd rax, dword ptr [r8 + 0]\n   sub rcx, rax\n   jle 8f\n"
        "movsxd r11, dword ptr [r8 + 12]\n   movsxd rax, dword ptr [r8 + 4]\n   sub r11, rax\n   jle 8f\n"
        "movsxd rax, dword ptr [r9]\n   test rax, rax\n   jge 1f\n   xor eax, eax\n   mov dword ptr [r9], 0\n"
        "1:  movsxd rdx, dword ptr [r10]\n   test rdx, rdx\n   jge 2f\n   xor edx, edx\n   mov dword ptr [r10], 0\n"
        "2:  mov r8, 65536\n   push rdx\n   push rax\n   test rax, rax\n   jz 3f\n"
        "lea rdx, [rax + rax]\n   cmp rdx, rcx\n   jbe 3f\n   mov rax, rcx\n   shl rax, 16\n   mov rcx, rdx\n"
        "xor edx, edx\n   div rcx\n   mov r8, rax\n"
        "3:  mov rax, [rsp + 8]\n   test rax, rax\n   jz 4f\n   lea rcx, [rax + rax]\n   cmp rcx, r11\n   jbe 4f\n"
        "mov rax, r11\n   shl rax, 16\n   xor edx, edx\n   div rcx\n   cmp rax, r8\n   cmovb r8, rax\n"
        "4:  pop rax\n   pop rdx\n   cmp r8, 65536\n   jae 9f\n"
        "imul rax, r8\n   shr rax, 16\n   mov [r9], eax\n"
        "imul rdx, r8\n   shr rdx, 16\n   mov [r10], edx\n"
        "9:  " UIAA_ASM_RET
        "8:  mov dword ptr [r9], 0\n   mov dword ptr [r10], 0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_radii)

        /* ui_box_aa_arc(u) -> rax */
        UIAA_ASM_FUNC(ui_box_aa_arc)
        UIAA_LD1
        "cmp rax, 65536\n   jae 1f\n   imul rax, rax\n   mov ecx, 1\n   shl rcx, 32\n   sub rcx, rax\n"
        "cvtsi2sd xmm0, rcx\n   sqrtsd xmm0, xmm0\n   cvttsd2si rax, xmm0\n" UIAA_ASM_RET
        "1:  xor eax, eax\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_arc)

        /* ui_box_aa_insets(dy, rx, ry, inset) — xmm0..xmm5 only (Win64-safe), preserves rbx,rbp,rdi,rsi,r12..r15 */
        UIAA_ASM_FUNC(ui_box_aa_insets)
        UIAA_LD4_INS
        "Lui_box_aa_insets_in:\n"
        "test r8, r8\n   jz 9f\n   test r11, r11\n   jz 9f\n   test r9d, r9d\n   jle 8f\n   test r10d, r10d\n   jle 8f\n"
        "cvtsi2sd xmm3, r9d\n   mov eax, 0x3ef00000\n   shl rax, 32\n   movq xmm0, rax\n"
        "mulsd xmm3, xmm0\n   unpcklpd xmm3, xmm3\n"
        "cvtsi2sd xmm4, r10d\n   unpcklpd xmm4, xmm4\n"
        "mov eax, 0x40f00000\n   shl rax, 32\n   movq xmm5, rax\n   unpcklpd xmm5, xmm5\n"
        "xor ecx, ecx\n"
        "1:  mov eax, [r8 + rcx*4]\n   xor edx, edx\n   test eax, eax\n   cmovl eax, edx\n   cmp eax, r10d\n   cmovg eax, r10d\n"
        "mov edx, [r8 + rcx*4 + 4]\n   test edx, edx\n   jge 2f\n   xor edx, edx\n"
        "2:  cmp edx, r10d\n   cmovg edx, r10d\n   shl rdx, 32\n   or rax, rdx\n   movq xmm0, rax\n"
        "cvtdq2pd xmm0, xmm0\n   mulpd xmm0, xmm5\n   divpd xmm0, xmm4\n"
        "cvttpd2dq xmm1, xmm0\n   cvtdq2pd xmm1, xmm1\n   mulpd xmm1, xmm1\n"
        "movapd xmm2, xmm5\n   mulpd xmm2, xmm5\n   subpd xmm2, xmm1\n   sqrtpd xmm2, xmm2\n"
        "cvttpd2dq xmm2, xmm2\n   cvtdq2pd xmm2, xmm2\n   mulpd xmm2, xmm3\n"
        "cvttpd2dq xmm2, xmm2\n   movq [r11 + rcx*4], xmm2\n"
        "add rcx, 2\n   cmp rcx, 16\n   jb 1b\n"
        "9:  " UIAA_ASM_RET
        "8:  pxor xmm0, xmm0\n   movdqu [r11], xmm0\n   movdqu [r11 + 16], xmm0\n"
        "movdqu [r11 + 32], xmm0\n   movdqu [r11 + 48], xmm0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_insets)

        /* ui_box_aa_spans_radii(box, rx, ry, y, first, width, s) -> eax */
        UIAA_ASM_FUNC(ui_box_aa_spans_radii)
        UIAA_PUSH_SP
        "test rdi, rdi\n   jz 99f\n   mov dword ptr [rdi + 20], 0\n   test rbx, rbx\n   jz 99f\n   test rbp, rbp\n   jle 99f\n"
        "movsxd r8, dword ptr [rbx + 0]\n   movsxd r9, dword ptr [rbx + 4]\n"
        "movsxd r10, dword ptr [rbx + 8]\n   movsxd r11, dword ptr [rbx + 12]\n"
        "cmp r10, r8\n   jle 99f\n   shl r14, 8\n   lea rcx, [r14 + 256]\n"
        "cmp rcx, r9\n   jle 99f\n   cmp r14, r11\n   jge 99f\n   shl r15, 8\n   shl rbp, 8\n"
        "movsxd rax, r13d\n   cmp r14, r9\n   jl 2f\n   cmp rcx, r11\n   jg 2f\n"
        "lea rdx, [r9 + rax]\n   cmp r14, rdx\n   jl 2f\n   mov rdx, r11\n   sub rdx, rax\n   cmp rcx, rdx\n   jg 2f\n"
        "sub r8, r15\n   xor eax, eax\n   cmp r8, 0\n   cmovl r8, rax\n"
        "sub r10, r15\n   cmp r10, rbp\n   cmovg r10, rbp\n   cmp r10, r8\n   jle 99f\n"
        "movd xmm0, r8d\n   pshufd xmm0, xmm0, 0\n   movd xmm1, r10d\n   pshufd xmm1, xmm1, 0\n"
        "movdqu [rdi + 32], xmm0\n   movdqu [rdi + 48], xmm0\n   movdqu [rdi + 64], xmm0\n   movdqu [rdi + 80], xmm0\n"
        "movdqu [rdi + 96], xmm1\n   movdqu [rdi + 112], xmm1\n   movdqu [rdi + 128], xmm1\n   movdqu [rdi + 144], xmm1\n"
        "mov rax, r8\n   sar rax, 8\n   mov [rdi + 0], eax\n"
        "lea rcx, [r10 + 255]\n   sar rcx, 8\n   mov [rdi + 4], ecx\n"
        "lea rax, [r8 + 255]\n   sar rax, 8\n   mov rdx, r10\n   sar rdx, 8\n"
        "cmp rdx, rax\n   jg 1f\n   mov rax, rcx\n   mov rdx, rcx\n"
        "1:  mov [rdi + 8], eax\n   mov [rdi + 12], edx\n"
        "mov dword ptr [rdi + 16], 4096\n   mov dword ptr [rdi + 20], 16\n   mov eax, 1\n"
        UIAA_POP_SP UIAA_ASM_RET
        "2:  sub rsp, 152\n   mov [rsp + 128], r8\n   mov [rsp + 136], r10\n"
        "movsxd rdx, r13d\n   lea r8, [r9 + rdx]\n   mov r10, r11\n   sub r10, rdx\n"
        "lea rcx, [r14 + 8]\n   xor esi, esi\n   xor edx, edx\n"
        "3:  xor eax, eax\n   cmp rcx, r9\n   jl 4f\n   cmp rcx, r11\n   jge 4f\n"
        "cmp rcx, r8\n   jge 5f\n   mov rax, r8\n   sub rax, rcx\n   jmp 6f\n"
        "5:  cmp rcx, r10\n   jl 6f\n   mov rax, rcx\n   sub rax, r10\n   inc rax\n   jmp 6f\n"
        "4:  bts esi, edx\n"
        "6:  mov [rsp + rdx*4], eax\n   add rcx, 16\n   inc rdx\n   cmp rdx, 16\n   jb 3b\n"
        "mov r8, [rsp + 128]\n   mov r10, [rsp + 136]\n   test r12d, r12d\n   jle 7f\n   test r13d, r13d\n   jle 7f\n"
        "movsxd rax, r12d\n   add r8, rax\n   sub r10, rax\n   mov [rsp + 128], r8\n   mov [rsp + 136], r10\n"
        "mov [rsp + 144], esi\n   mov r8, rsp\n   mov r9d, r12d\n   mov r10d, r13d\n   lea r11, [rsp + 64]\n"
        "call Lui_box_aa_insets_in\n   mov esi, [rsp + 144]\n   jmp 8f\n"
        "7:  pxor xmm0, xmm0\n   movdqu [rsp + 64], xmm0\n   movdqu [rsp + 80], xmm0\n"
        "movdqu [rsp + 96], xmm0\n   movdqu [rsp + 112], xmm0\n"
        "8:  mov rbx, [rsp + 128]\n   mov r14, [rsp + 136]\n   xor ecx, ecx\n   xor r12d, r12d\n"
        "10: bt esi, r12d\n   jc 12f\n   movsxd rax, dword ptr [rsp + 64 + r12*4]\n"
        "mov rdx, rbx\n   sub rdx, rax\n   lea rax, [r14 + rax]\n   cmp rax, rdx\n   jle 12f\n"
        "sub rdx, r15\n   xor r13d, r13d\n   cmp rdx, 0\n   cmovl rdx, r13\n"
        "sub rax, r15\n   cmp rax, rbp\n   cmovg rax, rbp\n   cmp rax, rdx\n   jle 12f\n"
        "test ecx, ecx\n   jnz 11f\n   mov r8, rdx\n   mov r10, rdx\n   mov r9, rax\n   mov r11, rax\n   jmp 13f\n"
        "11: cmp rdx, r8\n   cmovl r8, rdx\n   cmp rax, r9\n   cmovg r9, rax\n"
        "cmp rdx, r10\n   cmovg r10, rdx\n   cmp rax, r11\n   cmovl r11, rax\n"
        "13: mov [rdi + 32 + rcx*4], edx\n   mov [rdi + 96 + rcx*4], eax\n   inc ecx\n"
        "12: inc r12\n   cmp r12, 16\n   jb 10b\n   mov eax, ecx\n"
        "14: cmp eax, 16\n   jae 15f\n   mov dword ptr [rdi + 32 + rax*4], 0\n"
        "mov dword ptr [rdi + 96 + rax*4], 0\n   inc eax\n   jmp 14b\n"
        "15: add rsp, 152\n   mov [rdi + 20], ecx\n   test ecx, ecx\n   jz 99f\n"
        "mov rax, r8\n   sar rax, 8\n   mov [rdi + 0], eax\n"
        "lea rdx, [r9 + 255]\n   sar rdx, 8\n   mov [rdi + 4], edx\n"
        "lea rax, [r10 + 255]\n   sar rax, 8\n   mov rbx, r11\n   sar rbx, 8\n"
        "cmp rbx, rax\n   jg 16f\n   mov rax, rdx\n   mov rbx, rdx\n"
        "16: mov [rdi + 8], eax\n   mov [rdi + 12], ebx\n   shl ecx, 8\n   mov [rdi + 16], ecx\n   mov eax, 1\n"
        UIAA_POP_SP UIAA_ASM_RET
        "99: xor eax, eax\n"
        UIAA_POP_SP UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_spans_radii)

        /* ui_box_aa_spans_cover(s, x) -> eax — xmm0..xmm5 only (Win64-safe) */
        UIAA_ASM_FUNC(ui_box_aa_spans_cover)
        UIAA_LD2
        "test r8, r8\n   jz 9f\n   cmp dword ptr [r8 + 20], 0\n   jle 9f\n"
        "cmp r9d, [r8 + 0]\n   jl 9f\n   cmp r9d, [r8 + 4]\n   jge 9f\n"
        "cmp r9d, [r8 + 8]\n   jl 1f\n   cmp r9d, [r8 + 12]\n   jge 1f\n"
        "mov eax, [r8 + 16]\n" UIAA_ASM_RET
        "1:  shl r9d, 8\n   movd xmm0, r9d\n   pshufd xmm0, xmm0, 0\n"
        "add r9d, 256\n   movd xmm1, r9d\n   pshufd xmm1, xmm1, 0\n"
        "pxor xmm2, xmm2\n   xor ecx, ecx\n"
        "2:  movdqu xmm3, [r8 + 32 + rcx]\n   movdqa xmm4, xmm3\n   pcmpgtd xmm4, xmm0\n"
        "pand xmm3, xmm4\n   pandn xmm4, xmm0\n   por xmm3, xmm4\n"
        "movdqu xmm4, [r8 + 96 + rcx]\n   movdqa xmm5, xmm1\n   pcmpgtd xmm5, xmm4\n"
        "pand xmm4, xmm5\n   pandn xmm5, xmm1\n   por xmm4, xmm5\n"
        "psubd xmm4, xmm3\n   movdqa xmm5, xmm4\n   psrad xmm5, 31\n   pandn xmm5, xmm4\n   paddd xmm2, xmm5\n"
        "add rcx, 16\n   cmp rcx, 64\n   jb 2b\n"
        "pshufd xmm3, xmm2, 0x4e\n   paddd xmm2, xmm3\n   pshufd xmm3, xmm2, 0xb1\n   paddd xmm2, xmm3\n"
        "movd eax, xmm2\n" UIAA_ASM_RET
        "9:  xor eax, eax\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_spans_cover)

        /* ui_box_aa_span_ends(first, width, left, right, p0, p1, c0, c1) -> eax */
        UIAA_ASM_FUNC(ui_box_aa_span_ends)
        UIAA_PUSH_SE
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test rdi, rdi\n   jz 9f\n   test rsi, rsi\n   jz 9f\n"
        "test rdx, rdx\n   jle 9f\n   shl rax, 8\n   shl rdx, 8\n"
        "sub r10, rax\n   xor ecx, ecx\n   cmp r10, 0\n   cmovl r10, rcx\n"
        "sub r11, rax\n   cmp r11, rdx\n   cmovg r11, rdx\n   cmp r11, r10\n   jle 9f\n"
        "mov rax, r10\n   sar rax, 8\n   mov [r8], eax\n"
        "lea rcx, [r11 - 1]\n   sar rcx, 8\n   mov [r9], ecx\n"
        "cmp rax, rcx\n   jne 1f\n   sub r11, r10\n   shl r11d, 4\n   mov [rdi], r11d\n   mov [rsi], r11d\n"
        "mov eax, 1\n" UIAA_POP_SE UIAA_ASM_RET
        "1:  and r10d, 255\n   mov eax, 256\n   sub eax, r10d\n   shl eax, 4\n   mov [rdi], eax\n"
        "shl rcx, 8\n   sub r11, rcx\n   shl r11d, 4\n   mov [rsi], r11d\n   mov eax, 1\n"
        UIAA_POP_SE UIAA_ASM_RET
        "9:  xor eax, eax\n" UIAA_POP_SE UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_span_ends)

        ".att_syntax prefix\n"
);

#elif defined(UIAA_RISCV64)

__asm__(
        UIAA_ASM_SECTION

        /* ui_box_aa_radii(a0=box, a1=rx, a2=ry) */
        UIAA_ASM_FUNC(ui_box_aa_radii)
        "beqz a0, 9f\n   beqz a1, 9f\n   beqz a2, 9f\n"
        "lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "sub t2, t2, t0\n   blez t2, 8f\n   sub t3, t3, t1\n   blez t3, 8f\n"
        "lw t0, 0(a1)\n   lw t1, 0(a2)\n"
        "bgez t0, 1f\n   li t0, 0\n   sw zero, 0(a1)\n"
        "1:  bgez t1, 2f\n   li t1, 0\n   sw zero, 0(a2)\n"
        "2:  li t4, 65536\n   beqz t0, 3f\n   slli t5, t0, 1\n   bgeu t2, t5, 3f\n"
        "slli t6, t2, 16\n   divu t4, t6, t5\n"
        "3:  beqz t1, 4f\n   slli t5, t1, 1\n   bgeu t3, t5, 4f\n"
        "slli t6, t3, 16\n   divu t6, t6, t5\n   bgeu t6, t4, 4f\n   mv t4, t6\n"
        "4:  li t5, 65536\n   bgeu t4, t5, 9f\n"
        "mul t0, t0, t4\n   srli t0, t0, 16\n   sw t0, 0(a1)\n"
        "mul t1, t1, t4\n   srli t1, t1, 16\n   sw t1, 0(a2)\n"
        "9:  " UIAA_ASM_RET
        "8:  sw zero, 0(a1)\n   sw zero, 0(a2)\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_radii)

        /* ui_box_aa_arc(a0=u) -> a0 — pure RV64IM exact integer sqrt */
        UIAA_ASM_FUNC(ui_box_aa_arc)
        "beqz a0, 2f\n   li t0, 65536\n   bgeu a0, t0, 3f\n"
        "mul t1, a0, a0\n   li t2, 1\n   slli t2, t2, 32\n   sub t1, t2, t1\n"
        "li a0, 0\n   li t0, 1\n   slli t0, t0, 30\n"
        "1:  add t2, a0, t0\n   srli a0, a0, 1\n   bltu t1, t2, 4f\n   sub t1, t1, t2\n   add a0, a0, t0\n"
        "4:  srli t0, t0, 2\n   bnez t0, 1b\n" UIAA_ASM_RET
        "2:  li a0, 65536\n" UIAA_ASM_RET
        "3:  li a0, 0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_arc)

        /* ui_box_aa_insets(a0=dy, a1=rx, a2=ry, a3=inset) — pure RV64IM */
        UIAA_ASM_FUNC(ui_box_aa_insets)
        "Lui_box_aa_insets_in:\n"
        "beqz a0, 9f\n   beqz a3, 9f\n   sext.w a1, a1\n   sext.w a2, a2\n"
        "blez a1, 8f\n   blez a2, 8f\n   li t3, 16\n   li t4, 65536\n"
        "1:  lw t0, 0(a0)\n   blez t0, 2f\n   bge t0, a2, 3f\n"
        "slli t0, t0, 16\n   divu t0, t0, a2\n   beqz t0, 2f\n   bgeu t0, t4, 3f\n"
        "mul t1, t0, t0\n   li t2, 1\n   slli t2, t2, 32\n   sub t1, t2, t1\n"
        "li t0, 0\n   li t2, 1\n   slli t2, t2, 30\n"
        "4:  add t5, t0, t2\n   srli t0, t0, 1\n   bltu t1, t5, 5f\n   sub t1, t1, t5\n   add t0, t0, t2\n"
        "5:  srli t2, t2, 2\n   bnez t2, 4b\n   mul t0, t0, a1\n   srli t0, t0, 16\n   j 6f\n"
        "2:  mv t0, a1\n   j 6f\n"
        "3:  li t0, 0\n"
        "6:  sw t0, 0(a3)\n   addi a0, a0, 4\n   addi a3, a3, 4\n   addi t3, t3, -1\n   bnez t3, 1b\n"
        "9:  " UIAA_ASM_RET
        "8:  li t3, 8\n"
        "7:  sd zero, 0(a3)\n   addi a3, a3, 8\n   addi t3, t3, -1\n   bnez t3, 7b\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_insets)

        /* ui_box_aa_spans_radii(a0=box, a1=rx, a2=ry, a3=y, a4=first, a5=width, a6=s) -> a0 */
        UIAA_ASM_FUNC(ui_box_aa_spans_radii)
        "beqz a6, 99f\n   sw zero, 20(a6)\n   beqz a0, 99f\n   sext.w a5, a5\n   blez a5, 99f\n"
        "lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "ble t2, t0, 99f\n   sext.w a3, a3\n   slli t4, a3, 8\n   addi t5, t4, 256\n"
        "ble t5, t1, 99f\n   bge t4, t3, 99f\n   sext.w a4, a4\n   slli a4, a4, 8\n   slli a5, a5, 8\n"
        "sext.w a1, a1\n   sext.w a2, a2\n   blt t4, t1, 2f\n   bgt t5, t3, 2f\n"
        "add t6, t1, a2\n   blt t4, t6, 2f\n   sub t6, t3, a2\n   bgt t5, t6, 2f\n"
        "sub t0, t0, a4\n   bgez t0, 0f\n   li t0, 0\n"
        "0:  sub t2, t2, a4\n   ble t2, a5, 1f\n   mv t2, a5\n"
        "1:  ble t2, t0, 99f\n   addi t3, a6, 32\n   addi t4, a6, 96\n   li t5, 16\n"
        "19: sw t0, 0(t3)\n   sw t2, 0(t4)\n   addi t3, t3, 4\n   addi t4, t4, 4\n   addi t5, t5, -1\n   bnez t5, 19b\n"
        "srai t3, t0, 8\n   sw t3, 0(a6)\n   addi t4, t2, 255\n   srai t4, t4, 8\n   sw t4, 4(a6)\n"
        "addi t5, t0, 255\n   srai t5, t5, 8\n   srai t6, t2, 8\n   bgt t6, t5, 18f\n   mv t5, t4\n   mv t6, t4\n"
        "18: sw t5, 8(a6)\n   sw t6, 12(a6)\n   li t3, 4096\n   sw t3, 16(a6)\n   li t3, 16\n   sw t3, 20(a6)\n"
        "li a0, 1\n" UIAA_ASM_RET
        "2:  addi sp, sp, -208\n   sd ra, 128(sp)\n   sd s0, 136(sp)\n   sd s1, 144(sp)\n   sd s2, 152(sp)\n"
        "sd s3, 160(sp)\n   sd s4, 168(sp)\n   sd s5, 176(sp)\n   sd s6, 184(sp)\n   sd s7, 192(sp)\n"
        "mv s0, a6\n   mv s1, t0\n   mv s2, t2\n   mv s3, a4\n   mv s4, a5\n   li s5, 0\n"
        "add s6, t1, a2\n   sub s7, t3, a2\n   addi t4, t4, 8\n   li t5, 0\n   mv a7, sp\n"
        "3:  li t0, 0\n   blt t4, t1, 4f\n   bge t4, t3, 4f\n"
        "bge t4, s6, 5f\n   sub t0, s6, t4\n   j 6f\n"
        "5:  blt t4, s7, 6f\n   sub t0, t4, s7\n   addi t0, t0, 1\n   j 6f\n"
        "4:  li t6, 1\n   sll t6, t6, t5\n   or s5, s5, t6\n"
        "6:  sw t0, 0(a7)\n   addi a7, a7, 4\n   addi t4, t4, 16\n   addi t5, t5, 1\n   li t6, 16\n   blt t5, t6, 3b\n"
        "blez a1, 7f\n   blez a2, 7f\n   add s1, s1, a1\n   sub s2, s2, a1\n"
        "mv a0, sp\n   addi a3, sp, 64\n   jal ra, Lui_box_aa_insets_in\n   j 8f\n"
        "7:  addi t0, sp, 64\n   li t1, 8\n"
        "77: sd zero, 0(t0)\n   addi t0, t0, 8\n   addi t1, t1, -1\n   bnez t1, 77b\n"
        "8:  li a0, 0\n   li t0, 0\n   addi a1, sp, 64\n"
        "10: srl t1, s5, t0\n   andi t1, t1, 1\n   bnez t1, 12f\n   lw t1, 0(a1)\n"
        "sub t2, s1, t1\n   add t3, s2, t1\n   ble t3, t2, 12f\n"
        "sub t2, t2, s3\n   bgez t2, 20f\n   li t2, 0\n"
        "20: sub t3, t3, s3\n   ble t3, s4, 21f\n   mv t3, s4\n"
        "21: ble t3, t2, 12f\n   bnez a0, 11f\n   mv a2, t2\n   mv a4, t2\n   mv a3, t3\n   mv a5, t3\n   j 13f\n"
        "11: bge t2, a2, 22f\n   mv a2, t2\n"
        "22: ble t3, a3, 23f\n   mv a3, t3\n"
        "23: ble t2, a4, 24f\n   mv a4, t2\n"
        "24: bge t3, a5, 13f\n   mv a5, t3\n"
        "13: slli t4, a0, 2\n   add t5, s0, t4\n   sw t2, 32(t5)\n   sw t3, 96(t5)\n   addi a0, a0, 1\n"
        "12: addi a1, a1, 4\n   addi t0, t0, 1\n   li t6, 16\n   blt t0, t6, 10b\n"
        "mv t0, a0\n"
        "14: li t6, 16\n   bge t0, t6, 15f\n   slli t4, t0, 2\n   add t5, s0, t4\n"
        "sw zero, 32(t5)\n   sw zero, 96(t5)\n   addi t0, t0, 1\n   j 14b\n"
        "15: sw a0, 20(s0)\n   beqz a0, 98f\n   srai t0, a2, 8\n   sw t0, 0(s0)\n"
        "addi t1, a3, 255\n   srai t1, t1, 8\n   sw t1, 4(s0)\n"
        "addi t2, a4, 255\n   srai t2, t2, 8\n   srai t3, a5, 8\n   bgt t3, t2, 16f\n   mv t2, t1\n   mv t3, t1\n"
        "16: sw t2, 8(s0)\n   sw t3, 12(s0)\n   slli a0, a0, 8\n   sw a0, 16(s0)\n   li a0, 1\n   j 97f\n"
        "98: li a0, 0\n"
        "97: ld ra, 128(sp)\n   ld s0, 136(sp)\n   ld s1, 144(sp)\n   ld s2, 152(sp)\n"
        "ld s3, 160(sp)\n   ld s4, 168(sp)\n   ld s5, 176(sp)\n   ld s6, 184(sp)\n   ld s7, 192(sp)\n"
        "addi sp, sp, 208\n" UIAA_ASM_RET
        "99: li a0, 0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_spans_radii)

        /* ui_box_aa_spans_cover(a0=s, a1=x) -> a0 */
        UIAA_ASM_FUNC(ui_box_aa_spans_cover)
        "beqz a0, 9f\n   sext.w a1, a1\n   lw t0, 20(a0)\n   blez t0, 9f\n"
        "lw t1, 0(a0)\n   blt a1, t1, 9f\n   lw t2, 4(a0)\n   bge a1, t2, 9f\n"
        "lw t1, 8(a0)\n   blt a1, t1, 1f\n   lw t2, 12(a0)\n   bge a1, t2, 1f\n"
        "lwu a0, 16(a0)\n" UIAA_ASM_RET
        "1:  li t1, 16\n   ble t0, t1, 2f\n   mv t0, t1\n"
        "2:  slli t1, a1, 8\n   addi t2, t1, 256\n   addi t3, a0, 32\n   addi t4, a0, 96\n   li a0, 0\n"
        "3:  lw t5, 0(t3)\n   lw t6, 0(t4)\n   bge t5, t1, 4f\n   mv t5, t1\n"
        "4:  ble t6, t2, 5f\n   mv t6, t2\n"
        "5:  sub t6, t6, t5\n   blez t6, 6f\n   add a0, a0, t6\n"
        "6:  addi t3, t3, 4\n   addi t4, t4, 4\n   addi t0, t0, -1\n   bnez t0, 3b\n" UIAA_ASM_RET
        "9:  li a0, 0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_spans_cover)

        /* ui_box_aa_span_ends(a0=first, a1=width, a2=left, a3=right, a4=p0, a5=p1, a6=c0, a7=c1) -> a0 */
        UIAA_ASM_FUNC(ui_box_aa_span_ends)
        "beqz a4, 9f\n   beqz a5, 9f\n   beqz a6, 9f\n   beqz a7, 9f\n   sext.w a1, a1\n   blez a1, 9f\n"
        "sext.w a0, a0\n   slli t0, a0, 8\n   slli t1, a1, 8\n"
        "sext.w t2, a2\n   sub t2, t2, t0\n   bgez t2, 1f\n   li t2, 0\n"
        "1:  sext.w t3, a3\n   sub t3, t3, t0\n   ble t3, t1, 2f\n   mv t3, t1\n"
        "2:  ble t3, t2, 9f\n   srai t4, t2, 8\n   sw t4, 0(a4)\n   addi t5, t3, -1\n   srai t5, t5, 8\n   sw t5, 0(a5)\n"
        "bne t4, t5, 3f\n   sub t6, t3, t2\n   slli t6, t6, 4\n   sw t6, 0(a6)\n   sw t6, 0(a7)\n   li a0, 1\n" UIAA_ASM_RET
        "3:  andi t6, t2, 255\n   li t0, 256\n   sub t6, t0, t6\n   slli t6, t6, 4\n   sw t6, 0(a6)\n"
        "slli t5, t5, 8\n   sub t6, t3, t5\n   slli t6, t6, 4\n   sw t6, 0(a7)\n   li a0, 1\n" UIAA_ASM_RET
        "9:  li a0, 0\n" UIAA_ASM_RET
        UIAA_ASM_END(ui_box_aa_span_ends)
);

#endif /* arch */
#endif /* CANVAS_UI_BOX_AA_ASM_IMPLEMENTATION */
