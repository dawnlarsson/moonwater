/*
        Moonwater Canvas UI text blit & premultiplied blend primitives in pure assembly:
          - ui_text_over      : premultiplied ARGB pixel over blend (exact round-to-nearest /255 + saturate)
          - ui_text_cover_row : extract green-channel coverage bytes from ARGB scratch row with XOR mask
          - ui_text_run_walk  : clipped text run compositor with paper-skip, 4-neighbor stroke, and lazy ramp cache

        Pure arm64 (NEON), x86-64 (SSE2; SysV ELF / Mach-O / Windows x64 COFF), and
        riscv64 (RV64I/M) assembly. Zero C function bodies, zero allocations, zero
        syscalls, zero .data/.rodata lookup tables, zero external calls.

        STB-style: include for declarations; define CANVAS_UI_TEXT_BLIT_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CANVAS_UI_TEXT_BLIT_ASM_H
#define MOONWATER_CANVAS_UI_TEXT_BLIT_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  ui_txt_u8;
typedef p16 ui_txt_u16;
typedef p32 ui_txt_u32;
typedef b32 ui_txt_i32;
typedef p64 ui_txt_u64;
typedef b64 ui_txt_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  ui_txt_u8;
typedef __UINT16_TYPE__ ui_txt_u16;
typedef __UINT32_TYPE__ ui_txt_u32;
typedef __INT32_TYPE__  ui_txt_i32;
typedef __UINT64_TYPE__ ui_txt_u64;
typedef __INT64_TYPE__  ui_txt_i64;
#else
#include <stdint.h>
typedef uint8_t  ui_txt_u8;
typedef uint16_t ui_txt_u16;
typedef uint32_t ui_txt_u32;
typedef int32_t  ui_txt_i32;
typedef uint64_t ui_txt_u64;
typedef int64_t  ui_txt_i64;
#endif

/* 2128 bytes — byte-for-byte identical to ui.c's struct ui_ramp */
struct ui_text_ramp {
        ui_txt_u32 colour;     /* +0    */
        ui_txt_u32 alpha;      /* +4    */
        ui_txt_u32 ground;     /* +8    */
        ui_txt_i32 set;        /* +12   */
        ui_txt_u32 tone[256];  /* +16   */
        ui_txt_u32 over[256];  /* +1040 */
        ui_txt_u64 known[4];   /* +2064 */
        ui_txt_u64 made[4];    /* +2096 */
};

/* 152 bytes — byte-for-byte identical to ui.c's struct ui_walk */
struct ui_text_blit_walk {
        const ui_txt_u32    *scratch;     /* +0   */
        ui_txt_i64           wide;        /* +8   */
        ui_txt_i64           rows;        /* +16  */
        ui_txt_i64           top;         /* +24  */
        ui_txt_i64           first;       /* +32  */
        ui_txt_i64           last;        /* +40  */
        ui_txt_u32          *pixels;      /* +48  */
        ui_txt_i64           pitch;       /* +56  */
        ui_txt_i64           pen_x;       /* +64  */
        ui_txt_i64           ui_top;      /* +72  */
        ui_txt_i64           ui_bottom;   /* +80  */
        ui_txt_i64           ui_height;   /* +88  */
        ui_txt_i64           band_top;    /* +96  */
        ui_txt_i64           band_bottom; /* +104 */
        ui_txt_u64           paper;       /* +112 */
        ui_txt_u64           ground;      /* +120 */
        ui_txt_u64           flip;        /* +128 */
        ui_txt_u64           stroke;      /* +136 */
        struct ui_text_ramp *ramp;        /* +144 */
};

#ifdef offset_of
#define UITXT_OFF(T, M) offset_of(T, M)
#else
#define UITXT_OFF(T, M) ((ui_txt_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct ui_text_ramp) == 2128, "ui_text_ramp size");
_Static_assert(UITXT_OFF(struct ui_text_ramp, colour) == 0    && UITXT_OFF(struct ui_text_ramp, alpha) == 4 &&
               UITXT_OFF(struct ui_text_ramp, ground) == 8    && UITXT_OFF(struct ui_text_ramp, set)   == 12 &&
               UITXT_OFF(struct ui_text_ramp, tone)   == 16   && UITXT_OFF(struct ui_text_ramp, over)  == 1040 &&
               UITXT_OFF(struct ui_text_ramp, known)  == 2064 && UITXT_OFF(struct ui_text_ramp, made)  == 2096,
               "ui_text_ramp layout");
_Static_assert(sizeof(struct ui_text_blit_walk) == 152, "ui_text_blit_walk size");
_Static_assert(UITXT_OFF(struct ui_text_blit_walk, scratch)     == 0   && UITXT_OFF(struct ui_text_blit_walk, wide)        == 8 &&
               UITXT_OFF(struct ui_text_blit_walk, rows)        == 16  && UITXT_OFF(struct ui_text_blit_walk, top)         == 24 &&
               UITXT_OFF(struct ui_text_blit_walk, first)       == 32  && UITXT_OFF(struct ui_text_blit_walk, last)        == 40 &&
               UITXT_OFF(struct ui_text_blit_walk, pixels)      == 48  && UITXT_OFF(struct ui_text_blit_walk, pitch)       == 56 &&
               UITXT_OFF(struct ui_text_blit_walk, pen_x)       == 64  && UITXT_OFF(struct ui_text_blit_walk, ui_top)      == 72 &&
               UITXT_OFF(struct ui_text_blit_walk, ui_bottom)   == 80  && UITXT_OFF(struct ui_text_blit_walk, ui_height)   == 88 &&
               UITXT_OFF(struct ui_text_blit_walk, band_top)    == 96  && UITXT_OFF(struct ui_text_blit_walk, band_bottom) == 104 &&
               UITXT_OFF(struct ui_text_blit_walk, paper)       == 112 && UITXT_OFF(struct ui_text_blit_walk, ground)      == 120 &&
               UITXT_OFF(struct ui_text_blit_walk, flip)        == 128 && UITXT_OFF(struct ui_text_blit_walk, stroke)      == 136 &&
               UITXT_OFF(struct ui_text_blit_walk, ramp)        == 144,
               "ui_text_blit_walk layout");

ui_txt_u32 ui_text_over(ui_txt_u32 pixel, ui_txt_u32 under);
void       ui_text_cover_row(const ui_txt_u32 *src, ui_txt_u8 *dst, ui_txt_u64 width, ui_txt_u32 xor_mask);
void       ui_text_run_walk(const struct ui_text_blit_walk *walk);

#endif /* MOONWATER_CANVAS_UI_TEXT_BLIT_ASM_H */

#ifdef CANVAS_UI_TEXT_BLIT_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define UITXT_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define UITXT_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define UITXT_RISCV64 1
# else
#  error "src/canvas/ui/ui_text_blit.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define UITXT_ARM64 1
# elif defined(X64) && X64
#  define UITXT_X64 1
# elif defined(RISCV64) && RISCV64
#  define UITXT_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define UITXT_ASM_SECTION   ASM_SECTION
# define UITXT_ASM_FUNC(n)   ASM_FUNC(n)
# define UITXT_ASM_END(n)    ASM_END(n)
# define UITXT_ASM_RET       ASM_RET
#else
# define UITXT_ASM_SECTION   ".text\n"
# define UITXT_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define UITXT_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define UITXT_ASM_END(n)   ""
# elif defined(_WIN32)
#  define UITXT_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define UITXT_ASM_END(n)   ""
# else
#  if defined(UITXT_ARM64)
#   define UITXT_ASM_FTYPE   "%function"
#  else
#   define UITXT_ASM_FTYPE   "@function"
#  endif
#  define UITXT_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " UITXT_ASM_FTYPE "\n" #n ":\n"
#  define UITXT_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(UITXT_ARM64)

__asm__(
        UITXT_ASM_SECTION

        /* ui_text_over(w0=pixel, w1=under) -> w0 */
        UITXT_ASM_FUNC(ui_text_over)
        "lsr w2, w0, #24\n   cbz w2, 2f\n   mov w3, #255\n   subs w3, w3, w2\n   b.eq 3f\n   cbz w1, 3f\n"
        "fmov s0, w1\n   uxtl v0.8h, v0.8b\n   dup v1.8h, w3\n   mul v0.8h, v0.8h, v1.8h\n"
        "movi v2.8h, #128\n   add v0.8h, v0.8h, v2.8h\n   movz w4, #257\n   dup v3.4h, w4\n"
        "umull v4.4s, v0.4h, v3.4h\n   shrn v4.4h, v4.4s, #16\n   fmov s5, w0\n   uxtl v5.8h, v5.8b\n"
        "add v4.4h, v4.4h, v5.4h\n   sqxtun v4.8b, v4.8h\n   fmov w0, s4\n"
        "3:  " UITXT_ASM_RET
        "2:  mov w0, w1\n" UITXT_ASM_RET
        UITXT_ASM_END(ui_text_over)

        /* ui_text_cover_row(x0=src, x1=dst, x2=width, w3=xor_mask) */
        UITXT_ASM_FUNC(ui_text_cover_row)
        "cbz x0, 3f\n   cbz x1, 3f\n   cbz x2, 3f\n   cmp x2, #16\n   b.lo 2f\n"
        "dup v4.16b, w3\n   lsr x4, x2, #4\n"
        "1:  ld4 {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64\n   eor v1.16b, v1.16b, v4.16b\n"
        "st1 {v1.16b}, [x1], #16\n   subs x4, x4, #1\n   b.ne 1b\n   and x2, x2, #15\n   cbz x2, 3f\n"
        "2:  ldrb w5, [x0, #1]\n   add x0, x0, #4\n   eor w5, w5, w3\n   strb w5, [x1], #1\n"
        "subs x2, x2, #1\n   b.ne 2b\n"
        "3:  " UITXT_ASM_RET
        UITXT_ASM_END(ui_text_cover_row)

        /* ui_text_run_walk(x0=walk) — zero stack frame, zero external calls, shared tone+over */
        UITXT_ASM_FUNC(ui_text_run_walk)
        "cbz x0, 99f\n   ldr x13, [x0, #0]\n   cbz x13, 99f\n   ldr x14, [x0, #48]\n   cbz x14, 99f\n"
        "ldr x8, [x0, #144]\n   cbz x8, 99f\n   ldr x15, [x0, #8]\n   cmp x15, #0\n   b.le 99f\n"
        "ldr x5, [x0, #16]\n   cmp x5, #0\n   b.le 99f\n   ldr x6, [x0, #32]\n   cmp x6, #0\n   csel x6, xzr, x6, lt\n"
        "ldr x7, [x0, #40]\n   cmp x7, x15\n   csel x7, x15, x7, gt\n   cmp x6, x7\n   b.ge 99f\n"
        "ldr x13, [x0, #72]\n   cmp x13, #0\n   csel x13, xzr, x13, lt\n"
        "ldr x14, [x0, #80]\n   ldr x15, [x0, #88]\n   cmp x14, x15\n   csel x14, x14, x15, lt\n"
        "ldr x15, [x0, #96]\n   ldr x16, [x0, #104]\n   cmp x16, x15\n   b.le 1f\n"
        "cmp x15, x13\n   csel x13, x15, x13, gt\n   cmp x16, x14\n   csel x14, x16, x14, lt\n"
        "1:  ldr x15, [x0, #24]\n   sub x4, x13, x15\n   cmp x4, #0\n   csel x4, xzr, x4, lt\n"
        "sub x14, x14, x15\n   cmp x14, x5\n   csel x5, x14, x5, lt\n   cmp x4, x5\n   b.ge 99f\n"
        "ldr w9, [x0, #112]\n   orr x9, x9, x9, lsl #32\n   ldr w10, [x0, #120]\n"
        "ldr w11, [x0, #128]\n   and w11, w11, #255\n   ldr w12, [x0, #136]\n"
        "movi v6.8h, #128\n   movz w15, #257\n   dup v7.4h, w15\n"
        "2:  ldr x13, [x0, #24]\n   add x13, x13, x4\n   ldr x14, [x0, #56]\n   mul x13, x13, x14\n"
        "ldr x14, [x0, #64]\n   add x13, x13, x14\n   ldr x2, [x0, #48]\n   add x2, x2, x13, lsl #2\n"
        "ldr x14, [x0, #8]\n   mul x14, x4, x14\n   ldr x1, [x0, #0]\n   add x1, x1, x14, lsl #2\n   mov x3, x6\n"
        "3:  cmp x3, x7\n   b.ge 20f\n   cbnz w12, 6f\n"
        "ldr w13, [x1, x3, lsl #2]\n   cmp w13, w9\n   b.ne 7f\n   add x3, x3, #1\n   add x14, x1, x3, lsl #2\n"
        "4:  add x15, x3, #2\n   cmp x15, x7\n   b.gt 5f\n   ldr x15, [x14]\n   cmp x15, x9\n   b.ne 5f\n"
        "add x14, x14, #8\n   add x3, x3, #2\n   b 4b\n"
        "5:  cmp x3, x7\n   b.ge 20f\n   ldr w13, [x14], #4\n   cmp w13, w9\n   b.ne 7f\n   add x3, x3, #1\n   b 5b\n"
        "6:  ldr w13, [x1, x3, lsl #2]\n"
        "7:  ubfx w13, w13, #8, #8\n   eor w13, w13, w11\n   cbz w12, 12f\n   cmp w13, #255\n   b.hs 12f\n"
        "mov w14, #0\n   cbz x3, 8f\n   sub x15, x3, #1\n   ldr w15, [x1, x15, lsl #2]\n"
        "ubfx w15, w15, #8, #8\n   eor w15, w15, w11\n   cmp w15, w14\n   csel w14, w15, w14, hi\n"
        "8:  add x15, x3, #1\n   ldr x16, [x0, #8]\n   cmp x15, x16\n   b.ge 9f\n"
        "ldr w15, [x1, x15, lsl #2]\n   ubfx w15, w15, #8, #8\n   eor w15, w15, w11\n   cmp w15, w14\n   csel w14, w15, w14, hi\n"
        "9:  cbz x4, 10f\n   ldr x16, [x0, #8]\n   sub x15, x3, x16\n   ldr w15, [x1, x15, lsl #2]\n"
        "ubfx w15, w15, #8, #8\n   eor w15, w15, w11\n   cmp w15, w14\n   csel w14, w15, w14, hi\n"
        "10: add x15, x4, #1\n   ldr x16, [x0, #16]\n   cmp x15, x16\n   b.ge 11f\n"
        "ldr x16, [x0, #8]\n   add x15, x3, x16\n   ldr w15, [x1, x15, lsl #2]\n"
        "ubfx w15, w15, #8, #8\n   eor w15, w15, w11\n   cmp w15, w14\n   csel w14, w15, w14, hi\n"
        "11: mul w14, w14, w12\n   add w13, w13, w14, lsr #7\n   mov w15, #255\n   cmp w13, #255\n   csel w13, w15, w13, hi\n"
        "12: cbz w13, 19f\n   ldr w14, [x2, x3, lsl #2]\n   lsr w15, w13, #6\n   cmp w14, w10\n   b.ne 13f\n"
        "add x16, x8, #2096\n   ldr x17, [x16, x15, lsl #3]\n   lsr x16, x17, x13\n   tbz x16, #0, 14f\n"
        "add x16, x8, #1040\n   ldr w14, [x16, x13, lsl #2]\n   str w14, [x2, x3, lsl #2]\n   b 19f\n"
        "13: add x16, x8, #2064\n   ldr x17, [x16, x15, lsl #3]\n   lsr x16, x17, x13\n   mov w15, #0\n   tbz x16, #0, 15f\n"
        "add x16, x8, #16\n   ldr w16, [x16, x13, lsl #2]\n   b 16f\n"
        "14: mov w15, #1\n"
        "15: ldr w16, [x8, #4]\n   mul w16, w13, w16\n   mov w17, #255\n   udiv w16, w16, w17\n"
        "cmp w16, #255\n   csel w16, w17, w16, hi\n   add w17, w16, w16, lsr #7\n   fmov s1, w16\n"
        "ldr w16, [x8, #0]\n   fmov s0, w16\n   uxtl v0.8h, v0.8b\n   dup v2.8h, w17\n   mul v0.8h, v0.8h, v2.8h\n"
        "add v0.8h, v0.8h, v6.8h\n   shrn v0.8b, v0.8h, #8\n   fmov w16, s0\n   fmov w17, s1\n   bfi w16, w17, #24, #8\n"
        "cbnz w15, 21f\n   add x17, x8, #16\n   str w16, [x17, x13, lsl #2]\n   lsr w17, w13, #6\n"
        "add x17, x8, x17, lsl #3\n   ldr x17, [x17, #2064]\n   mov x15, #1\n   lsl x15, x15, x13\n"
        "orr x17, x17, x15\n   lsr w15, w13, #6\n   add x15, x8, x15, lsl #3\n   str x17, [x15, #2064]\n   mov w15, #0\n   b 16f\n"
        "21: ldr w14, [x8, #8]\n"
        "16: lsr w17, w16, #24\n   cbz w17, 18f\n   sub w17, wzr, w17\n   add w17, w17, #255\n"
        "cbz w17, 17f\n   cbz w14, 17f\n   fmov s0, w14\n   uxtl v0.8h, v0.8b\n   dup v1.8h, w17\n"
        "mul v0.8h, v0.8h, v1.8h\n   add v0.8h, v0.8h, v6.8h\n   umull v4.4s, v0.4h, v7.4h\n   shrn v4.4h, v4.4s, #16\n"
        "fmov s5, w16\n   uxtl v5.8h, v5.8b\n   add v4.4h, v4.4h, v5.4h\n   sqxtun v4.8b, v4.8h\n   fmov w16, s4\n"
        "17: mov w14, w16\n"
        "18: str w14, [x2, x3, lsl #2]\n   cbz w15, 19f\n   add x16, x8, #1040\n   str w14, [x16, x13, lsl #2]\n"
        "lsr w15, w13, #6\n   add x16, x8, #2096\n   ldr x17, [x16, x15, lsl #3]\n   mov x14, #1\n"
        "lsl x14, x14, x13\n   orr x17, x17, x14\n   str x17, [x16, x15, lsl #3]\n"
        "19: add x3, x3, #1\n   b 3b\n"
        "20: add x4, x4, #1\n   cmp x4, x5\n   b.lt 2b\n"
        "99: " UITXT_ASM_RET
        UITXT_ASM_END(ui_text_run_walk)
);

#elif defined(UITXT_X64)

#if defined(_WIN32)
# define UITXT_LOAD2   "mov r8d, ecx\n   mov r9d, edx\n"
# define UITXT_LOAD4   "mov r10, r8\n   mov r11d, r9d\n   mov r8, rcx\n   mov r9, rdx\n"
# define UITXT_PUSHW   "push rbx\n   push rbp\n   push rdi\n   push rsi\n   push r12\n   push r13\n   push r14\n   push r15\n   mov rbx, rcx\n"
# define UITXT_POPW    "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rsi\n   pop rdi\n   pop rbp\n   pop rbx\n"
#else
# define UITXT_LOAD2   "mov r8d, edi\n   mov r9d, esi\n"
# define UITXT_LOAD4   "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11d, ecx\n"
# define UITXT_PUSHW   "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   mov rbx, rdi\n"
# define UITXT_POPW    "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#endif

__asm__(
        ".intel_syntax noprefix\n"
        UITXT_ASM_SECTION

        /* ui_text_over(pixel, under) -> eax */
        UITXT_ASM_FUNC(ui_text_over)
        UITXT_LOAD2
        "mov ecx, r8d\n   shr ecx, 24\n   jz 2f\n   mov eax, 255\n   sub eax, ecx\n   jz 3f\n   test r9d, r9d\n   jz 3f\n"
        "movd xmm3, eax\n   pshuflw xmm3, xmm3, 0\n   movd xmm0, r9d\n   movd xmm1, r8d\n   pxor xmm2, xmm2\n"
        "punpcklbw xmm0, xmm2\n   punpcklbw xmm1, xmm2\n   pmullw xmm0, xmm3\n"
        "mov eax, 0x00800080\n   movd xmm2, eax\n   pshufd xmm2, xmm2, 0\n   paddw xmm0, xmm2\n"
        "mov eax, 0x01010101\n   movd xmm3, eax\n   pshufd xmm3, xmm3, 0\n   pmulhuw xmm0, xmm3\n"
        "paddw xmm0, xmm1\n   packuswb xmm0, xmm0\n   movd eax, xmm0\n" UITXT_ASM_RET
        "2:  mov eax, r9d\n" UITXT_ASM_RET
        "3:  mov eax, r8d\n" UITXT_ASM_RET
        UITXT_ASM_END(ui_text_over)

        /* ui_text_cover_row(src, dst, width, xor_mask) */
        UITXT_ASM_FUNC(ui_text_cover_row)
        UITXT_LOAD4
        "test r8, r8\n   jz 3f\n   test r9, r9\n   jz 3f\n   test r10, r10\n   jz 3f\n"
        "movzx r11d, r11b\n   cmp r10, 16\n   jb 2f\n"
        "mov eax, 0x000000ff\n   movd xmm4, eax\n   pshufd xmm4, xmm4, 0\n"
        "imul eax, r11d, 0x01010101\n   movd xmm5, eax\n   pshufd xmm5, xmm5, 0\n   mov rcx, r10\n   shr rcx, 4\n"
        "1:  movdqu xmm0, [r8]\n   movdqu xmm1, [r8 + 16]\n   movdqu xmm2, [r8 + 32]\n   movdqu xmm3, [r8 + 48]\n"
        "psrld xmm0, 8\n   psrld xmm1, 8\n   psrld xmm2, 8\n   psrld xmm3, 8\n"
        "pand xmm0, xmm4\n   pand xmm1, xmm4\n   pand xmm2, xmm4\n   pand xmm3, xmm4\n"
        "packssdw xmm0, xmm1\n   packssdw xmm2, xmm3\n   packuswb xmm0, xmm2\n   pxor xmm0, xmm5\n"
        "movdqu [r9], xmm0\n   add r8, 64\n   add r9, 16\n   dec rcx\n   jnz 1b\n   and r10, 15\n   jz 3f\n"
        "2:  movzx eax, byte ptr [r8 + 1]\n   xor eax, r11d\n   mov [r9], al\n   add r8, 4\n   inc r9\n   dec r10\n   jnz 2b\n"
        "3:  " UITXT_ASM_RET
        UITXT_ASM_END(ui_text_cover_row)

        /* ui_text_run_walk(walk) — zero external calls, xmm0..xmm5 only, shared tone+over */
        UITXT_ASM_FUNC(ui_text_run_walk)
        UITXT_PUSHW
        "test rbx, rbx\n   jz 99f\n   cmp qword ptr [rbx], 0\n   je 99f\n   cmp qword ptr [rbx + 48], 0\n   je 99f\n"
        "mov r11, [rbx + 144]\n   test r11, r11\n   jz 99f\n   mov rax, [rbx + 8]\n   test rax, rax\n   jle 99f\n"
        "mov rbp, [rbx + 16]\n   test rbp, rbp\n   jle 99f\n"
        "mov rdi, [rbx + 32]\n   xor ecx, ecx\n   cmp rdi, 0\n   cmovl rdi, rcx\n"
        "mov r10, [rbx + 40]\n   cmp r10, rax\n   cmovg r10, rax\n   cmp rdi, r10\n   jge 99f\n"
        "mov rax, [rbx + 72]\n   cmp rax, 0\n   cmovl rax, rcx\n"
        "mov rdx, [rbx + 80]\n   cmp rdx, [rbx + 88]\n   cmovg rdx, [rbx + 88]\n"
        "mov r8, [rbx + 96]\n   mov r9, [rbx + 104]\n   cmp r9, r8\n   jle 1f\n"
        "cmp r8, rax\n   cmovg rax, r8\n   cmp r9, rdx\n   cmovl rdx, r9\n"
        "1:  mov r8, [rbx + 24]\n   sub rax, r8\n   cmp rax, 0\n   cmovl rax, rcx\n   mov r12, rax\n"
        "sub rdx, r8\n   cmp rdx, rbp\n   cmovl rbp, rdx\n   cmp r12, rbp\n   jge 99f\n"
        "mov esi, [rbx + 112]\n   mov rax, rsi\n   shl rax, 32\n   or rsi, rax\n"
        "mov eax, 0x00800080\n   movd xmm4, eax\n   pshufd xmm4, xmm4, 0\n"
        "mov eax, 0x01010101\n   movd xmm5, eax\n   pshufd xmm5, xmm5, 0\n"
        "2:  mov rax, [rbx + 24]\n   add rax, r12\n   imul rax, [rbx + 56]\n   add rax, [rbx + 64]\n"
        "mov r14, [rbx + 48]\n   lea r14, [r14 + rax*4]\n"
        "mov rax, [rbx + 8]\n   imul rax, r12\n   mov r15, [rbx]\n   lea r15, [r15 + rax*4]\n   mov r13, rdi\n"
        "3:  cmp r13, r10\n   jge 20f\n   cmp qword ptr [rbx + 136], 0\n   jne 6f\n"
        "mov eax, [r15 + r13*4]\n   cmp eax, esi\n   jne 7f\n   inc r13\n"
        "4:  lea rcx, [r13 + 2]\n   cmp rcx, r10\n   jg 5f\n   cmp [r15 + r13*4], rsi\n   jne 5f\n   add r13, 2\n   jmp 4b\n"
        "5:  cmp r13, r10\n   jge 20f\n   mov eax, [r15 + r13*4]\n   cmp eax, esi\n   jne 7f\n   inc r13\n   jmp 5b\n"
        "6:  mov eax, [r15 + r13*4]\n"
        "7:  shr eax, 8\n   and eax, 255\n   mov ecx, [rbx + 128]\n   and ecx, 255\n   xor eax, ecx\n"
        "cmp qword ptr [rbx + 136], 0\n   je 12f\n   cmp eax, 255\n   jae 12f\n   xor r9d, r9d\n"
        "test r13, r13\n   jz 8f\n   mov edx, [r15 + r13*4 - 4]\n   shr edx, 8\n   and edx, 255\n   xor edx, ecx\n"
        "cmp edx, r9d\n   cmova r9d, edx\n"
        "8:  lea rdx, [r13 + 1]\n   cmp rdx, [rbx + 8]\n   jge 9f\n   mov edx, [r15 + r13*4 + 4]\n"
        "shr edx, 8\n   and edx, 255\n   xor edx, ecx\n   cmp edx, r9d\n   cmova r9d, edx\n"
        "9:  test r12, r12\n   jz 10f\n   mov rdx, r13\n   sub rdx, [rbx + 8]\n   mov edx, [r15 + rdx*4]\n"
        "shr edx, 8\n   and edx, 255\n   xor edx, ecx\n   cmp edx, r9d\n   cmova r9d, edx\n"
        "10: lea rdx, [r12 + 1]\n   cmp rdx, [rbx + 16]\n   jge 11f\n   mov rdx, r13\n   add rdx, [rbx + 8]\n"
        "mov edx, [r15 + rdx*4]\n   shr edx, 8\n   and edx, 255\n   xor edx, ecx\n   cmp edx, r9d\n   cmova r9d, edx\n"
        "11: imul r9d, dword ptr [rbx + 136]\n   shr r9d, 7\n   add eax, r9d\n   mov edx, 255\n   cmp eax, 255\n   cmova eax, edx\n"
        "12: test eax, eax\n   jz 19f\n   mov r8d, eax\n   mov r9d, [r14 + r13*4]\n"
        "cmp r9d, dword ptr [rbx + 120]\n   jne 13f\n   bt qword ptr [r11 + 2096], r8\n   jnc 14f\n"
        "mov eax, [r11 + r8*4 + 1040]\n   mov [r14 + r13*4], eax\n   jmp 19f\n"
        "13: bt qword ptr [r11 + 2064], r8\n   jnc 15f\n   mov eax, [r11 + r8*4 + 16]\n   jmp 16f\n"
        "14: or r8, 256\n"
        "15: movzx eax, r8b\n   imul eax, dword ptr [r11 + 4]\n   xor edx, edx\n   mov ecx, 255\n   div ecx\n"
        "cmp eax, 255\n   cmova eax, ecx\n   mov ecx, eax\n   shr ecx, 7\n   add ecx, eax\n"
        "mov edx, [r11]\n   mov r9d, edx\n   and r9d, 0x00ff00ff\n   imul r9d, ecx\n   add r9d, 0x00800080\n"
        "shr r9d, 8\n   and r9d, 0x00ff00ff\n   shr edx, 8\n   and edx, 255\n   imul edx, ecx\n   add edx, 128\n"
        "and edx, 0xff00\n   shl eax, 24\n   or eax, r9d\n   or eax, edx\n"
        "test r8d, 256\n   jnz 21f\n   mov [r11 + r8*4 + 16], eax\n   bts qword ptr [r11 + 2064], r8\n"
        "mov r9d, [r14 + r13*4]\n   jmp 16f\n"
        "21: mov r9d, [r11 + 8]\n"
        "16: mov ecx, eax\n   shr ecx, 24\n   jz 18f\n   mov edx, 255\n   sub edx, ecx\n   jz 17f\n   test r9d, r9d\n   jz 17f\n"
        "movd xmm3, edx\n   pshuflw xmm3, xmm3, 0\n   movd xmm0, r9d\n   movd xmm1, eax\n   pxor xmm2, xmm2\n"
        "punpcklbw xmm0, xmm2\n   punpcklbw xmm1, xmm2\n   pmullw xmm0, xmm3\n   paddw xmm0, xmm4\n"
        "pmulhuw xmm0, xmm5\n   paddw xmm0, xmm1\n   packuswb xmm0, xmm0\n   movd eax, xmm0\n"
        "17: mov r9d, eax\n"
        "18: mov [r14 + r13*4], r9d\n   test r8d, 256\n   jz 19f\n   and r8d, 255\n"
        "mov [r11 + r8*4 + 1040], r9d\n   bts qword ptr [r11 + 2096], r8\n"
        "19: inc r13\n   jmp 3b\n"
        "20: inc r12\n   cmp r12, rbp\n   jl 2b\n"
        "99: " UITXT_POPW UITXT_ASM_RET
        UITXT_ASM_END(ui_text_run_walk)
        ".att_syntax prefix\n"
);

#elif defined(UITXT_RISCV64)

__asm__(
        UITXT_ASM_SECTION

        /* ui_text_over(a0=pixel, a1=under) -> a0 (RV64I/M SWAR + branchless sat) */
        UITXT_ASM_FUNC(ui_text_over)
        "srliw t0, a0, 24\n   beqz t0, 2f\n   li t1, 255\n   subw t1, t1, t0\n   beqz t1, 3f\n"
        "slli t2, a1, 32\n   beqz t2, 3f\n   li t2, 0x00ff00ff\n   li t3, 0x00800080\n   li t4, 0x01000100\n"
        "and t5, a1, t2\n   mul t5, t5, t1\n   add t5, t5, t3\n   srli t6, t5, 8\n   and t6, t6, t2\n"
        "add t5, t5, t6\n   srli t5, t5, 8\n   and t5, t5, t2\n   and t6, a0, t2\n   add t5, t5, t6\n"
        "and t6, t5, t4\n   srli a2, t6, 8\n   sub t6, t6, a2\n   or t5, t5, t6\n   and t5, t5, t2\n"
        "srliw a3, a1, 8\n   and a3, a3, t2\n   mul a3, a3, t1\n   add a3, a3, t3\n   srli t6, a3, 8\n"
        "and t6, t6, t2\n   add a3, a3, t6\n   srli a3, a3, 8\n   and a3, a3, t2\n   srliw t6, a0, 8\n"
        "and t6, t6, t2\n   add a3, a3, t6\n   and t6, a3, t4\n   srli a2, t6, 8\n   sub t6, t6, a2\n"
        "or a3, a3, t6\n   and a3, a3, t2\n   slli a3, a3, 8\n   or a0, t5, a3\n"
        "3:  addiw a0, a0, 0\n" UITXT_ASM_RET
        "2:  addiw a0, a1, 0\n" UITXT_ASM_RET
        UITXT_ASM_END(ui_text_over)

        /* ui_text_cover_row(a0=src, a1=dst, a2=width, a3=xor_mask) */
        UITXT_ASM_FUNC(ui_text_cover_row)
        "beqz a0, 3f\n   beqz a1, 3f\n   beqz a2, 3f\n   andi a3, a3, 255\n   li t4, 2\n   bltu a2, t4, 2f\n"
        "1:  lbu t0, 1(a0)\n   lbu t1, 5(a0)\n   xor t0, t0, a3\n   xor t1, t1, a3\n"
        "sb t0, 0(a1)\n   sb t1, 1(a1)\n   addi a0, a0, 8\n   addi a1, a1, 2\n   addi a2, a2, -2\n"
        "bgeu a2, t4, 1b\n   beqz a2, 3f\n"
        "2:  lbu t0, 1(a0)\n   xor t0, t0, a3\n   sb t0, 0(a1)\n"
        "3:  " UITXT_ASM_RET
        UITXT_ASM_END(ui_text_cover_row)

        /* ui_text_run_walk(a0=walk) — RV64I/M, zero external calls, shared tone+over */
        UITXT_ASM_FUNC(ui_text_run_walk)
        "beqz a0, 98f\n   addi sp, sp, -96\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n"
        "sd s3, 24(sp)\n   sd s4, 32(sp)\n   sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n"
        "sd s8, 64(sp)\n   sd s9, 72(sp)\n   sd s10, 80(sp)\n   sd s11, 88(sp)\n   mv s0, a0\n"
        "ld t0, 0(s0)\n   beqz t0, 99f\n   ld t1, 48(s0)\n   beqz t1, 99f\n   ld s8, 144(s0)\n   beqz s8, 99f\n"
        "ld t2, 8(s0)\n   blez t2, 99f\n   ld s5, 16(s0)\n   blez s5, 99f\n"
        "ld s6, 32(s0)\n   bgez s6, 1f\n   li s6, 0\n"
        "1:  ld s7, 40(s0)\n   ble s7, t2, 2f\n   mv s7, t2\n"
        "2:  bge s6, s7, 99f\n   ld t0, 72(s0)\n   bgez t0, 3f\n   li t0, 0\n"
        "3:  ld t1, 80(s0)\n   ld t2, 88(s0)\n   ble t1, t2, 4f\n   mv t1, t2\n"
        "4:  ld t2, 96(s0)\n   ld t3, 104(s0)\n   ble t3, t2, 6f\n   ble t2, t0, 5f\n   mv t0, t2\n"
        "5:  bge t3, t1, 6f\n   mv t1, t3\n"
        "6:  ld t2, 24(s0)\n   sub s4, t0, t2\n   bgez s4, 7f\n   li s4, 0\n"
        "7:  sub t1, t1, t2\n   bge t1, s5, 8f\n   mv s5, t1\n"
        "8:  bge s4, s5, 99f\n   lwu s9, 112(s0)\n   lwu s10, 120(s0)\n   lwu a4, 128(s0)\n   andi a4, a4, 255\n"
        "lwu a5, 136(s0)\n   li a6, 0x00ff00ff\n   li a7, 0x00800080\n   li s11, 0x01000100\n"
        "9:  ld t0, 24(s0)\n   add t0, t0, s4\n   ld t1, 56(s0)\n   mul t0, t0, t1\n   ld t1, 64(s0)\n   add t0, t0, t1\n"
        "slli t0, t0, 2\n   ld s2, 48(s0)\n   add s2, s2, t0\n   ld t0, 8(s0)\n   mul t0, s4, t0\n   slli t0, t0, 2\n"
        "ld s1, 0(s0)\n   add s1, s1, t0\n   mv s3, s6\n"
        "10: bge s3, s7, 29f\n   bnez a5, 13f\n   slli t0, s3, 2\n   add t1, s1, t0\n   lwu t2, 0(t1)\n"
        "bne t2, s9, 14f\n   addi s3, s3, 1\n   addi t1, t1, 4\n"
        "11: addi t0, s3, 2\n   bgt t0, s7, 12f\n   lwu t2, 0(t1)\n   bne t2, s9, 12f\n   lwu t2, 4(t1)\n"
        "bne t2, s9, 12f\n   addi s3, s3, 2\n   addi t1, t1, 8\n   j 11b\n"
        "12: bge s3, s7, 29f\n   lwu t2, 0(t1)\n   bne t2, s9, 14f\n   addi s3, s3, 1\n   addi t1, t1, 4\n   j 12b\n"
        "13: slli t0, s3, 2\n   add t1, s1, t0\n   lwu t2, 0(t1)\n"
        "14: srliw t0, t2, 8\n   andi t0, t0, 255\n   xor t0, t0, a4\n   beqz a5, 20f\n   li t3, 255\n   bgeu t0, t3, 20f\n"
        "li t4, 0\n   beqz s3, 15f\n   lwu t5, -4(t1)\n   srliw t5, t5, 8\n   andi t5, t5, 255\n   xor t5, t5, a4\n"
        "bleu t5, t4, 15f\n   mv t4, t5\n"
        "15: addi t5, s3, 1\n   ld t6, 8(s0)\n   bge t5, t6, 16f\n   lwu t5, 4(t1)\n   srliw t5, t5, 8\n"
        "andi t5, t5, 255\n   xor t5, t5, a4\n   bleu t5, t4, 16f\n   mv t4, t5\n"
        "16: beqz s4, 17f\n   ld t6, 8(s0)\n   slli t6, t6, 2\n   sub t5, t1, t6\n   lwu t5, 0(t5)\n"
        "srliw t5, t5, 8\n   andi t5, t5, 255\n   xor t5, t5, a4\n   bleu t5, t4, 17f\n   mv t4, t5\n"
        "17: addi t5, s4, 1\n   ld t6, 16(s0)\n   bge t5, t6, 18f\n   ld t6, 8(s0)\n   slli t6, t6, 2\n"
        "add t5, t1, t6\n   lwu t5, 0(t5)\n   srliw t5, t5, 8\n   andi t5, t5, 255\n   xor t5, t5, a4\n"
        "bleu t5, t4, 18f\n   mv t4, t5\n"
        "18: mulw t4, t4, a5\n   srliw t4, t4, 7\n   addw t0, t0, t4\n   bleu t0, t3, 20f\n   li t0, 255\n"
        "20: beqz t0, 28f\n   slli t1, s3, 2\n   add a0, s2, t1\n   lwu a1, 0(a0)\n   srli t2, t0, 6\n   slli t2, t2, 3\n"
        "bne a1, s10, 21f\n   addi t3, s8, 2047\n   addi t3, t3, 49\n   add t3, t3, t2\n   ld t4, 0(t3)\n"
        "srl t5, t4, t0\n   andi t5, t5, 1\n   beqz t5, 22f\n"
        "slli t1, t0, 2\n   addi t3, s8, 1040\n   add t3, t3, t1\n   lw t4, 0(t3)\n   sw t4, 0(a0)\n   j 28f\n"
        "21: addi t3, s8, 2047\n   addi t3, t3, 17\n   add t3, t3, t2\n   ld t4, 0(t3)\n   srl t5, t4, t0\n"
        "andi t5, t5, 1\n   beqz t5, 23f\n   slli t1, t0, 2\n   addi t4, s8, 16\n   add t4, t4, t1\n"
        "lwu a2, 0(t4)\n   j 26f\n"
        "22: ori t0, t0, 256\n"
        "23: andi t1, t0, 255\n   lwu t4, 4(s8)\n   mulw t1, t1, t4\n   li t4, 255\n   divuw t1, t1, t4\n"
        "bleu t1, t4, 24f\n   li t1, 255\n"
        "24: srli t4, t1, 7\n   add t4, t4, t1\n   lwu t5, 0(s8)\n   and t6, t5, a6\n   mul t6, t6, t4\n"
        "add t6, t6, a7\n   srli t6, t6, 8\n   and t6, t6, a6\n   srli t5, t5, 8\n   andi t5, t5, 255\n"
        "mul t5, t5, t4\n   addi t5, t5, 128\n   li a2, 0xff00\n   and t5, t5, a2\n   slli a2, t1, 24\n"
        "or a2, a2, t6\n   or a2, a2, t5\n   andi t4, t0, 256\n   bnez t4, 25f\n"
        "slli t1, t0, 2\n   addi t4, s8, 16\n   add t4, t4, t1\n   sw a2, 0(t4)\n   ld t4, 0(t3)\n"
        "li t5, 1\n   sll t5, t5, t0\n   or t4, t4, t5\n   sd t4, 0(t3)\n   j 26f\n"
        "25: lwu a1, 8(s8)\n"
        "26: srli t1, a2, 24\n   beqz t1, 27f\n   li t4, 255\n   sub t4, t4, t1\n   beqz t4, 30f\n   beqz a1, 30f\n"
        "and t5, a1, a6\n   mul t5, t5, t4\n   add t5, t5, a7\n   srli t6, t5, 8\n   and t6, t6, a6\n"
        "add t5, t5, t6\n   srli t5, t5, 8\n   and t5, t5, a6\n   and t6, a2, a6\n   add t5, t5, t6\n"
        "and t6, t5, s11\n   srli t1, t6, 8\n   sub t6, t6, t1\n   or t5, t5, t6\n   and t5, t5, a6\n"
        "srli a3, a1, 8\n   and a3, a3, a6\n   mul a3, a3, t4\n   add a3, a3, a7\n   srli t6, a3, 8\n"
        "and t6, t6, a6\n   add a3, a3, t6\n   srli a3, a3, 8\n   and a3, a3, a6\n   srli t6, a2, 8\n"
        "and t6, t6, a6\n   add a3, a3, t6\n   and t6, a3, s11\n   srli t1, t6, 8\n   sub t6, t6, t1\n"
        "or a3, a3, t6\n   and a3, a3, a6\n   slli a3, a3, 8\n   or a2, t5, a3\n"
        "30: mv a1, a2\n"
        "27: sw a1, 0(a0)\n   andi t4, t0, 256\n   beqz t4, 28f\n   andi t0, t0, 255\n"
        "slli t1, t0, 2\n   addi t4, s8, 1040\n   add t4, t4, t1\n   sw a1, 0(t4)\n   ld t4, 0(t3)\n"
        "li t5, 1\n   sll t5, t5, t0\n   or t4, t4, t5\n   sd t4, 0(t3)\n"
        "28: addi s3, s3, 1\n   j 10b\n"
        "29: addi s4, s4, 1\n   blt s4, s5, 9b\n"
        "99: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   ld s4, 32(sp)\n"
        "ld s5, 40(sp)\n   ld s6, 48(sp)\n   ld s7, 56(sp)\n   ld s8, 64(sp)\n   ld s9, 72(sp)\n"
        "ld s10, 80(sp)\n   ld s11, 88(sp)\n   addi sp, sp, 96\n"
        "98: " UITXT_ASM_RET
        UITXT_ASM_END(ui_text_run_walk)
);

#endif

#endif /* CANVAS_UI_TEXT_BLIT_ASM_IMPLEMENTATION */
