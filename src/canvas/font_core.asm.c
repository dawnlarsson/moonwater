/*
        Moonwater Canvas TrueType / OpenType & Rasterizer core primitives in pure assembly:
          - font_asm_search      : bounds-checked big-endian OpenType binary search (u16/u32 keys)
          - font_asm_glyph       : cmap format 12 & format 4 (+ symbol fallback) glyph lookup
          - font_asm_coverage    : OpenType Coverage format 1 & 2 lookup
          - font_asm_class       : OpenType ClassDef format 1 & 2 lookup
          - font_asm_pair_at     : OpenType GPOS PairPos format 1 & 2 lookup
          - font_asm_range_find  : packed (key << shift | value) Unicode range binary search
          - font_asm_lower_bound : strided u16/u32 sorted-array lower_bound
          - font_asm_span        : analytic area coverage span stepper (single & multi-column + row masks)
          - font_asm_edge_rows   : exact floor-divided Bresenham edge row walker
          - font_asm_trace       : 1-row / 2-row / multi-row outline edge tracer

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define CANVAS_FONT_CORE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CANVAS_FONT_CORE_ASM_H
#define MOONWATER_CANVAS_FONT_CORE_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  font_asm_u8;
typedef p16 font_asm_u16;
typedef p32 font_asm_u32;
typedef b32 font_asm_i32;
typedef p64 font_asm_u64;
typedef b64 font_asm_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  font_asm_u8;
typedef __UINT16_TYPE__ font_asm_u16;
typedef __UINT32_TYPE__ font_asm_u32;
typedef __INT32_TYPE__  font_asm_i32;
typedef __UINT64_TYPE__ font_asm_u64;
typedef __INT64_TYPE__  font_asm_i64;
#else
#include <stdint.h>
typedef uint8_t  font_asm_u8;
typedef uint16_t font_asm_u16;
typedef uint32_t font_asm_u32;
typedef int32_t  font_asm_i32;
typedef uint64_t font_asm_u64;
typedef int64_t  font_asm_i64;
#endif

/* 48 bytes — self-contained rasterizer band descriptor */
struct font_raster_band {
        font_asm_u32 *cells;    /* +0  */
        font_asm_u64 *rows;     /* +8  &e->row[0].touched (48-byte stride: +0 touched, +8 crossed) */
        font_asm_i32  band;     /* +16 */
        font_asm_i32  num_rows; /* +20 */
        font_asm_i32  stride;   /* +24 */
        font_asm_i32  width;    /* +28 */
        font_asm_i32  grain;    /* +32 */
        font_asm_i32  cross;    /* +36 */
        font_asm_u8   mark;     /* +40 */
        font_asm_u8   track;    /* +41 */
        font_asm_u8   pad[6];   /* +42 */
};

#ifdef offset_of
#define FONT_CORE_OFF(T, M) offset_of(T, M)
#else
#define FONT_CORE_OFF(T, M) ((font_asm_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct font_raster_band) == 48, "font_raster_band size");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, cells)    == 0,  "cells offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, rows)     == 8,  "rows offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, band)     == 16, "band offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, num_rows) == 20, "num_rows offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, stride)   == 24, "stride offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, width)    == 28, "width offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, grain)    == 32, "grain offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, cross)    == 36, "cross offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, mark)     == 40, "mark offset");
_Static_assert(FONT_CORE_OFF(struct font_raster_band, track)    == 41, "track offset");

font_asm_u32 font_asm_search(const void *face, font_asm_u64 table, font_asm_u32 count,
                             font_asm_u64 stride, font_asm_u32 off_bytes, font_asm_u32 value);
font_asm_u32 font_asm_glyph(const void *face, font_asm_u32 code);
font_asm_i32 font_asm_coverage(const void *face, font_asm_u64 table, font_asm_u32 glyph);
font_asm_u32 font_asm_class(const void *face, font_asm_u64 table, font_asm_u32 glyph);
font_asm_u32 font_asm_pair_at(const void *face, font_asm_u64 table, font_asm_u32 left, font_asm_u32 right,
                              font_asm_u64 *first_at, font_asm_u64 *second_at);
font_asm_u32 font_asm_range_find(const font_asm_u32 *table, font_asm_u32 count, font_asm_u32 shift, font_asm_u32 code);
font_asm_u32 font_asm_lower_bound(const void *values, font_asm_u32 count, font_asm_u32 stride,
                                  font_asm_u32 wide, font_asm_u32 code);
void         font_asm_span(const struct font_raster_band *b, font_asm_i32 row,
                           font_asm_i32 xa, font_asm_i32 xb, font_asm_i32 cover);
void         font_asm_edge_rows(const struct font_raster_band *b, const font_asm_i32 *edge);
void         font_asm_trace(const struct font_raster_band *b, font_asm_i32 x0, font_asm_i32 y0,
                            font_asm_i32 x1, font_asm_i32 y1);

#endif /* MOONWATER_CANVAS_FONT_CORE_ASM_H */

#ifdef CANVAS_FONT_CORE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define FONT_CORE_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define FONT_CORE_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define FONT_CORE_RISCV64 1
# else
#  error "src/canvas/font_core.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define FONT_CORE_ARM64 1
# elif defined(X64) && X64
#  define FONT_CORE_X64 1
# elif defined(RISCV64) && RISCV64
#  define FONT_CORE_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define FC_ASM_SECTION   ASM_SECTION
# define FC_ASM_FUNC(n)   ASM_FUNC(n)
# define FC_ASM_END(n)    ASM_END(n)
# define FC_ASM_RET       ASM_RET
#else
# define FC_ASM_SECTION   ".text\n"
# define FC_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define FC_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define FC_ASM_END(n)   ""
# elif defined(_WIN32)
#  define FC_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define FC_ASM_END(n)   ""
# else
#  if defined(FONT_CORE_ARM64)
#   define FC_ASM_FTYPE   "%function"
#  else
#   define FC_ASM_FTYPE   "@function"
#  endif
#  define FC_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " FC_ASM_FTYPE "\n" #n ":\n"
#  define FC_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define FC_SYM(n) "_" #n
#else
# define FC_SYM(n) #n
#endif

/* ========================================================================== */
/*                                   ARM64                                    */
/* ========================================================================== */
#if defined(FONT_CORE_ARM64)

__asm__(
        FC_ASM_SECTION

        /* font_asm_search(x0=face, x1=table, w2=count, x3=stride, w4=off_bytes, w5=value) -> w0 */
        FC_ASM_FUNC(font_asm_search)
        "cbz x0, 9f\n   cbz w2, 9f\n   ldp x6, x7, [x0]\n   lsr w8, w4, #1\n   add x1, x1, x8\n"
        "mov w9, #0\n   mov w10, w2\n   tbnz w4, #0, 4f\n"
        "sub x11, x7, #2\n"
        "1:  cmp w9, w10\n   b.hs 8f\n   sub w12, w10, w9\n   add w12, w9, w12, lsr #1\n"
        "madd x13, x12, x3, x1\n   mov w14, #0\n   cbz x6, 2f\n   cmp x7, #2\n   b.lo 2f\n"
        "cmp x13, x11\n   b.hi 2f\n   ldrh w14, [x6, x13]\n   rev16 w14, w14\n"
        "2:  add w15, w12, #1\n   cmp w14, w5\n   csel w9, w15, w9, lo\n   csel w10, w10, w12, lo\n   b 1b\n"
        "4:  sub x11, x7, #4\n"
        "5:  cmp w9, w10\n   b.hs 8f\n   sub w12, w10, w9\n   add w12, w9, w12, lsr #1\n"
        "madd x13, x12, x3, x1\n   mov w14, #0\n   cbz x6, 6f\n   cmp x7, #4\n   b.lo 6f\n"
        "cmp x13, x11\n   b.hi 6f\n   ldr w14, [x6, x13]\n   rev w14, w14\n"
        "6:  add w15, w12, #1\n   cmp w14, w5\n   csel w9, w15, w9, lo\n   csel w10, w10, w12, lo\n   b 5b\n"
        "8:  mov w0, w9\n" FC_ASM_RET
        "9:  mov w0, #0\n" FC_ASM_RET
        FC_ASM_END(font_asm_search)

        /* font_asm_glyph(x0=face, w1=code) -> w0 */
        FC_ASM_FUNC(font_asm_glyph)
        "cbz x0, 19f\n   ldp x2, x3, [x0]\n   cbz x2, 19f\n   ldr x4, [x0, #48]\n"
        "ldr w5, [x0, #96]\n   ldrb w6, [x0, #146]\n   ldrb w7, [x0, #147]\n"
        "mov w8, #0\n   cbz w7, 1f\n   cmp w1, #0x100\n   cset w8, lo\n"
        "1:  mov x9, #0\n   cmp w6, #12\n   b.ne 7f\n   cmp x3, #16\n   b.lo 18f\n"
        "sub x10, x3, #16\n   cmp x4, x10\n   b.hi 18f\n   add x11, x4, #12\n"
        "ldr w12, [x2, x11]\n   rev w12, w12\n   sub x10, x10, x4\n   mov x11, #12\n"
        "udiv x10, x10, x11\n   cmp x12, x10\n   csel x12, x10, x12, hi\n"
        "add x13, x4, #16\n   mov w14, #0\n   mov w15, w12\n   sub x16, x3, #4\n"
        "2:  cmp w14, w15\n   b.hs 5f\n   sub w17, w15, w14\n   add w17, w14, w17, lsr #1\n"
        "add x10, x17, x17, lsl #1\n   add x10, x13, x10, lsl #2\n   add x10, x10, #4\n"
        "mov w11, #0\n   cmp x3, #4\n   b.lo 3f\n   cmp x10, x16\n   b.hi 3f\n   ldr w11, [x2, x10]\n   rev w11, w11\n"
        "3:  cmp w11, w1\n   b.hs 4f\n   add w14, w17, #1\n   b 2b\n"
        "4:  mov w15, w17\n   b 2b\n"
        "5:  cmp w14, w12\n   b.hs 18f\n   add x10, x14, x14, lsl #1\n   add x10, x13, x10, lsl #2\n"
        "mov w11, #0\n   cmp x3, #4\n   b.lo 6f\n   cmp x10, x16\n   b.hi 6f\n   ldr w11, [x2, x10]\n   rev w11, w11\n"
        "6:  cmp w11, w1\n   b.hi 18f\n   add x17, x10, #8\n   mov w15, #0\n"
        "cmp x3, #4\n   b.lo 61f\n   cmp x17, x16\n   b.hi 61f\n   ldr w15, [x2, x17]\n   rev w15, w15\n"
        "61: add x9, x15, x1\n   sub x9, x9, x11\n   b 18f\n"
        "7:  cmp w6, #4\n   b.ne 18f\n   tst w1, #0xffff0000\n   b.ne 18f\n   sub x16, x3, #2\n"
        "add x10, x4, #6\n   mov w12, #0\n   cmp x3, #2\n   b.lo 8f\n   cmp x10, x16\n   b.hi 8f\n"
        "ldrh w12, [x2, x10]\n   rev16 w12, w12\n"
        "8:  lsr w12, w12, #1\n   add x13, x4, #14\n   mov w14, #0\n   mov w15, w12\n"
        "9:  cmp w14, w15\n   b.hs 12f\n   sub w17, w15, w14\n   add w17, w14, w17, lsr #1\n"
        "add x10, x13, x17, lsl #1\n   mov w11, #0\n   cmp x3, #2\n   b.lo 10f\n   cmp x10, x16\n   b.hi 10f\n"
        "ldrh w11, [x2, x10]\n   rev16 w11, w11\n"
        "10: cmp w11, w1\n   b.hs 11f\n   add w14, w17, #1\n   b 9b\n"
        "11: mov w15, w17\n   b 9b\n"
        "12: cmp w14, w12\n   b.hs 18f\n   add x10, x13, x12, lsl #1\n   add x10, x10, #2\n"
        "add x11, x10, x14, lsl #1\n   mov w15, #0\n   cmp x3, #2\n   b.lo 13f\n   cmp x11, x16\n   b.hi 13f\n"
        "ldrh w15, [x2, x11]\n   rev16 w15, w15\n"
        "13: cmp w1, w15\n   b.lo 18f\n   add x10, x10, x12, lsl #1\n   add x11, x10, x14, lsl #1\n"
        "mov w17, #0\n   cmp x3, #2\n   b.lo 14f\n   cmp x11, x16\n   b.hi 14f\n   ldrh w17, [x2, x11]\n   rev16 w17, w17\n"
        "14: add x10, x10, x12, lsl #1\n   add x10, x10, x14, lsl #1\n   mov w11, #0\n"
        "cmp x3, #2\n   b.lo 15f\n   cmp x10, x16\n   b.hi 15f\n   ldrh w11, [x2, x10]\n   rev16 w11, w11\n"
        "15: cbz w11, 16f\n   sub w15, w1, w15\n   add x10, x10, x11\n   add x10, x10, x15, lsl #1\n"
        "mov w9, #0\n   cmp x3, #2\n   b.lo 17f\n   cmp x10, x16\n   b.hi 17f\n   ldrh w9, [x2, x10]\n   rev16 w9, w9\n   b 17f\n"
        "16: mov w9, w1\n"
        "17: cbz x9, 18f\n   add x9, x9, x17\n   and x9, x9, #0xffff\n"
        "18: cmp x9, x5\n   csel w0, w9, wzr, lo\n   cbnz w0, 20f\n   cbz w8, 20f\n"
        "mov w8, #0\n   orr w1, w1, #0xf000\n   b 1b\n"
        "19: mov w0, #0\n"
        "20: " FC_ASM_RET
        FC_ASM_END(font_asm_glyph)

        /* font_asm_coverage(x0=face, x1=table, w2=glyph) -> w0 */
        FC_ASM_FUNC(font_asm_coverage)
        "cbz x0, 9f\n   ldp x3, x4, [x0]\n   cbz x3, 9f\n   cmp x4, #2\n   b.lo 9f\n   sub x5, x4, #2\n"
        "mov w6, #0\n   cmp x1, x5\n   b.hi 1f\n   ldrh w6, [x3, x1]\n   rev16 w6, w6\n"
        "1:  add x7, x1, #2\n   mov w8, #0\n   cmp x7, x5\n   b.hi 2f\n   ldrh w8, [x3, x7]\n   rev16 w8, w8\n"
        "2:  add x1, x1, #4\n   mov w9, #0\n   mov w10, w8\n   cmp w6, #1\n   b.eq 3f\n   cmp w6, #2\n   b.ne 9f\n"
        "6:  cmp w9, w10\n   b.hs 8f\n   sub w11, w10, w9\n   add w11, w9, w11, lsr #1\n"
        "add x12, x11, x11, lsl #1\n   add x12, x1, x12, lsl #1\n   add x12, x12, #2\n"
        "mov w13, #0\n   cmp x12, x5\n   b.hi 7f\n   ldrh w13, [x3, x12]\n   rev16 w13, w13\n"
        "7:  cmp w13, w2\n   b.hs 71f\n   add w9, w11, #1\n   b 6b\n"
        "71: mov w10, w11\n   b 6b\n"
        "8:  cmp w9, w8\n   b.hs 9f\n   add x12, x9, x9, lsl #1\n   add x12, x1, x12, lsl #1\n"
        "mov w13, #0\n   cmp x12, x5\n   b.hi 81f\n   ldrh w13, [x3, x12]\n   rev16 w13, w13\n"
        "81: cmp w13, w2\n   b.hi 9f\n   add x12, x12, #4\n   mov w14, #0\n"
        "cmp x12, x5\n   b.hi 82f\n   ldrh w14, [x3, x12]\n   rev16 w14, w14\n"
        "82: add w0, w14, w2\n   sub w0, w0, w13\n" FC_ASM_RET
        "3:  cmp w9, w10\n   b.hs 5f\n   sub w11, w10, w9\n   add w11, w9, w11, lsr #1\n"
        "add x12, x1, x11, lsl #1\n   mov w13, #0\n   cmp x12, x5\n   b.hi 4f\n   ldrh w13, [x3, x12]\n   rev16 w13, w13\n"
        "4:  cmp w13, w2\n   b.hs 41f\n   add w9, w11, #1\n   b 3b\n"
        "41: mov w10, w11\n   b 3b\n"
        "5:  cmp w9, w8\n   b.hs 9f\n   add x12, x1, x9, lsl #1\n   mov w13, #0\n"
        "cmp x12, x5\n   b.hi 51f\n   ldrh w13, [x3, x12]\n   rev16 w13, w13\n"
        "51: cmp w13, w2\n   b.ne 9f\n   mov w0, w9\n" FC_ASM_RET
        "9:  mov w0, #-1\n" FC_ASM_RET
        FC_ASM_END(font_asm_coverage)

        /* font_asm_class(x0=face, x1=table, w2=glyph) -> w0 */
        FC_ASM_FUNC(font_asm_class)
        "cbz x0, 9f\n   ldp x3, x4, [x0]\n   cbz x3, 9f\n   cmp x4, #2\n   b.lo 9f\n   sub x5, x4, #2\n"
        "mov w6, #0\n   cmp x1, x5\n   b.hi 1f\n   ldrh w6, [x3, x1]\n   rev16 w6, w6\n"
        "1:  cmp w6, #1\n   b.ne 3f\n   add x7, x1, #2\n   mov w8, #0\n   cmp x7, x5\n   b.hi 11f\n"
        "ldrh w8, [x3, x7]\n   rev16 w8, w8\n"
        "11: cmp w2, w8\n   b.lo 9f\n   add x7, x1, #4\n   mov w9, #0\n   cmp x7, x5\n   b.hi 12f\n"
        "ldrh w9, [x3, x7]\n   rev16 w9, w9\n"
        "12: sub w10, w2, w8\n   cmp w10, w9\n   b.hs 9f\n   add x7, x1, #6\n   add x7, x7, x10, lsl #1\n"
        "mov w0, #0\n   cmp x7, x5\n   b.hi 2f\n   ldrh w0, [x3, x7]\n   rev16 w0, w0\n"
        "2:  " FC_ASM_RET
        "3:  cmp w6, #2\n   b.ne 9f\n   add x7, x1, #2\n   mov w8, #0\n   cmp x7, x5\n   b.hi 4f\n"
        "ldrh w8, [x3, x7]\n   rev16 w8, w8\n"
        "4:  add x1, x1, #4\n   mov w9, #0\n   mov w10, w8\n"
        "5:  cmp w9, w10\n   b.hs 7f\n   sub w11, w10, w9\n   add w11, w9, w11, lsr #1\n"
        "add x12, x11, x11, lsl #1\n   add x12, x1, x12, lsl #1\n   add x12, x12, #2\n"
        "mov w13, #0\n   cmp x12, x5\n   b.hi 6f\n   ldrh w13, [x3, x12]\n   rev16 w13, w13\n"
        "6:  cmp w13, w2\n   b.hs 61f\n   add w9, w11, #1\n   b 5b\n"
        "61: mov w10, w11\n   b 5b\n"
        "7:  cmp w9, w8\n   b.hs 9f\n   add x12, x9, x9, lsl #1\n   add x12, x1, x12, lsl #1\n"
        "mov w13, #0\n   cmp x12, x5\n   b.hi 71f\n   ldrh w13, [x3, x12]\n   rev16 w13, w13\n"
        "71: cmp w13, w2\n   b.hi 9f\n   add x12, x12, #4\n   mov w0, #0\n"
        "cmp x12, x5\n   b.hi 8f\n   ldrh w0, [x3, x12]\n   rev16 w0, w0\n"
        "8:  " FC_ASM_RET
        "9:  mov w0, #0\n" FC_ASM_RET
        FC_ASM_END(font_asm_class)

        /* font_asm_pair_at(x0=face, x1=table, w2=left, w3=right, x4=first_at, x5=second_at) -> w0 */
        FC_ASM_FUNC(font_asm_pair_at)
        "cbz x0, 19f\n   cbz x4, 19f\n   cbz x5, 19f\n   ldp x6, x7, [x0]\n   cbz x6, 19f\n   cmp x7, #2\n   b.lo 19f\n"
        "stp x29, x30, [sp, #-80]!\n   stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n"
        "stp x23, x24, [sp, #48]\n   stp x25, x26, [sp, #64]\n"
        "mov x19, x0\n   mov x20, x1\n   mov w21, w2\n   mov w22, w3\n   mov x23, x4\n   mov x24, x5\n"
        "sub x8, x7, #2\n   mov w9, #0\n   cmp x1, x8\n   b.hi 1f\n   ldrh w9, [x6, x1]\n   rev16 w9, w9\n"
        "1:  mov w25, w9\n   add x10, x1, #4\n   mov w9, #0\n   cmp x10, x8\n   b.hi 2f\n   ldrh w9, [x6, x10]\n   rev16 w9, w9\n"
        "2:  and w9, w9, #0xff\n   lsr w11, w9, #1\n   and w11, w11, #0x55555555\n   sub w9, w9, w11\n"
        "and w11, w9, #0x33333333\n   lsr w9, w9, #2\n   and w9, w9, #0x33333333\n   add w9, w9, w11\n"
        "add w9, w9, w9, lsr #4\n   and w26, w9, #0x0f\n"
        "add x10, x1, #6\n   mov w9, #0\n   cmp x10, x8\n   b.hi 3f\n   ldrh w9, [x6, x10]\n   rev16 w9, w9\n"
        "3:  and w9, w9, #0xff\n   lsr w11, w9, #1\n   and w11, w11, #0x55555555\n   sub w9, w9, w11\n"
        "and w11, w9, #0x33333333\n   lsr w9, w9, #2\n   and w9, w9, #0x33333333\n   add w9, w9, w11\n"
        "add w9, w9, w9, lsr #4\n   and w9, w9, #0x0f\n   add w9, w9, w26\n   lsl x9, x9, #1\n"
        "orr x25, x25, x9, lsl #16\n" /* x25: lo16=format, hi48=record */
        "add x10, x1, #2\n   mov w9, #0\n   cmp x10, x8\n   b.hi 4f\n   ldrh w9, [x6, x10]\n   rev16 w9, w9\n"
        "4:  add x1, x20, x9\n   mov x0, x19\n   mov w2, w21\n   bl " FC_SYM(font_asm_coverage) "\n"
        "tbnz w0, #31, 18f\n   ldp x6, x7, [x19]\n   sub x8, x7, #2\n   and w9, w25, #0xffff\n"
        "cmp w9, #1\n   b.ne 8f\n   add x10, x20, #8\n   mov w11, #0\n   cmp x10, x8\n   b.hi 5f\n"
        "ldrh w11, [x6, x10]\n   rev16 w11, w11\n"
        "5:  cmp w0, w11\n   b.hs 18f\n   add x10, x20, #10\n   add x10, x10, x0, lsl #1\n   mov w11, #0\n"
        "cmp x10, x8\n   b.hi 6f\n   ldrh w11, [x6, x10]\n   rev16 w11, w11\n"
        "6:  add x21, x20, x11\n   mov w12, #0\n   cmp x21, x8\n   b.hi 7f\n   ldrh w12, [x6, x21]\n   rev16 w12, w12\n"
        "7:  mov x0, x19\n   add x1, x21, #2\n   mov w2, w12\n   lsr x3, x25, #16\n   add x3, x3, #2\n"
        "mov w4, #0\n   mov w5, w22\n   mov w25, w12\n   mov x20, x3\n   bl " FC_SYM(font_asm_search) "\n"
        "cmp w0, w25\n   b.hs 18f\n   add x10, x21, #2\n   madd x10, x0, x20, x10\n"
        "ldp x6, x7, [x19]\n   sub x8, x7, #2\n   mov w11, #0\n   cmp x10, x8\n   b.hi 71f\n"
        "ldrh w11, [x6, x10]\n   rev16 w11, w11\n"
        "71: cmp w11, w22\n   b.ne 18f\n   add x10, x10, #2\n   b 15f\n"
        "8:  cmp w9, #2\n   b.ne 18f\n   add x10, x20, #8\n   mov w11, #0\n   cmp x10, x8\n   b.hi 9f\n"
        "ldrh w11, [x6, x10]\n   rev16 w11, w11\n"
        "9:  mov x0, x19\n   add x1, x20, x11\n   mov w2, w21\n   bl " FC_SYM(font_asm_class) "\n"
        "mov w21, w0\n   ldp x6, x7, [x19]\n   sub x8, x7, #2\n   add x10, x20, #10\n   mov w11, #0\n"
        "cmp x10, x8\n   b.hi 10f\n   ldrh w11, [x6, x10]\n   rev16 w11, w11\n"
        "10: mov x0, x19\n   add x1, x20, x11\n   mov w2, w22\n   bl " FC_SYM(font_asm_class) "\n"
        "mov w22, w0\n   ldp x6, x7, [x19]\n   sub x8, x7, #2\n   add x10, x20, #12\n   mov w11, #0\n"
        "cmp x10, x8\n   b.hi 11f\n   ldrh w11, [x6, x10]\n   rev16 w11, w11\n"
        "11: cmp w21, w11\n   b.hs 18f\n   add x10, x20, #14\n   mov w12, #0\n   cmp x10, x8\n   b.hi 12f\n"
        "ldrh w12, [x6, x10]\n   rev16 w12, w12\n"
        "12: cmp w22, w12\n   b.hs 18f\n   madd x10, x21, x12, x22\n   lsr x11, x25, #16\n"
        "add x12, x20, #16\n   madd x10, x10, x11, x12\n"
        "15: str x10, [x23]\n   add x10, x10, x26, lsl #1\n   str x10, [x24]\n   mov w0, #1\n   b 17f\n"
        "18: mov w0, #0\n"
        "17: ldp x25, x26, [sp, #64]\n   ldp x23, x24, [sp, #48]\n   ldp x21, x22, [sp, #32]\n"
        "ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #80\n" FC_ASM_RET
        "19: mov w0, #0\n" FC_ASM_RET
        FC_ASM_END(font_asm_pair_at)

        /* font_asm_range_find(x0=table, w1=count, w2=shift, w3=code) -> w0 */
        FC_ASM_FUNC(font_asm_range_find)
        "cbz x0, 4f\n   cbz w1, 4f\n   mov w4, #0\n   mov w5, w1\n"
        "1:  sub w6, w5, w4\n   cmp w6, #1\n   b.ls 3f\n   add w7, w4, w6, lsr #1\n"
        "ldr w8, [x0, x7, lsl #2]\n   lsr w8, w8, w2\n   cmp w8, w3\n"
        "csel w4, w7, w4, ls\n   csel w5, w5, w7, ls\n   b 1b\n"
        "3:  ldr w0, [x0, x4, lsl #2]\n   mov w6, #1\n   lsl w6, w6, w2\n   sub w6, w6, #1\n   and w0, w0, w6\n" FC_ASM_RET
        "4:  mov w0, #0\n" FC_ASM_RET
        FC_ASM_END(font_asm_range_find)

        /* font_asm_lower_bound(x0=values, w1=count, w2=stride, w3=wide, w4=code) -> w0 */
        FC_ASM_FUNC(font_asm_lower_bound)
        "cbz x0, 9f\n   cbz w1, 9f\n   mov w5, #0\n   mov w6, w1\n   cbnz w3, 4f\n"
        "lsl x2, x2, #1\n"
        "1:  cmp w5, w6\n   b.hs 8f\n   sub w7, w6, w5\n   add w7, w5, w7, lsr #1\n"
        "mul x8, x7, x2\n   ldrh w9, [x0, x8]\n   add w10, w7, #1\n   cmp w9, w4\n"
        "csel w5, w10, w5, lo\n   csel w6, w6, w7, lo\n   b 1b\n"
        "4:  lsl x2, x2, #2\n"
        "5:  cmp w5, w6\n   b.hs 8f\n   sub w7, w6, w5\n   add w7, w5, w7, lsr #1\n"
        "mul x8, x7, x2\n   ldr w9, [x0, x8]\n   add w10, w7, #1\n   cmp w9, w4\n"
        "csel w5, w10, w5, lo\n   csel w6, w6, w7, lo\n   b 5b\n"
        "8:  mov w0, w5\n" FC_ASM_RET
        "9:  mov w0, #0\n" FC_ASM_RET
        FC_ASM_END(font_asm_lower_bound)

        /* font_asm_span(x0=band, w1=row, w2=xa, w3=xb, w4=cover) */
        FC_ASM_FUNC(font_asm_span)
        "cbz x0, 19f\n   ldr x5, [x0]\n   cbz x5, 19f\n   tbnz w1, #31, 19f\n"
        "ldr w6, [x0, #20]\n   cmp w1, w6\n   b.ge 19f\n   ldr w7, [x0, #28]\n   lsl w7, w7, #10\n"
        "cmp w2, #0\n   csel w2, wzr, w2, lt\n   cmp w2, w7\n   csel w2, w7, w2, gt\n"
        "cmp w3, #0\n   csel w3, wzr, w3, lt\n   cmp w3, w7\n   csel w3, w7, w3, gt\n"
        "ldr w6, [x0, #24]\n   umull x6, w1, w6\n   add x5, x5, x6, lsl #2\n"
        "eor w6, w2, w3\n   asr w6, w6, #10\n   cbnz w6, 4f\n"
        /* span_one */
        "asr w6, w2, #10\n   and w7, w2, #1023\n   and w8, w3, #1023\n   add w7, w7, w8\n   mul w7, w4, w7\n"
        "lsl w8, w4, #11\n   sub w8, w8, w7\n   add x9, x5, w6, sxtw #2\n"
        "ldp w10, w11, [x9]\n   add w10, w10, w8\n   add w11, w11, w7\n   stp w10, w11, [x9]\n"
        "ldr x9, [x0, #8]\n   cbz x9, 19f\n   add w10, w1, w1, lsl #1\n   add x9, x9, w10, sxtw #4\n"
        "ldrb w10, [x0, #40]\n   cbz w10, 2f\n   ldr w11, [x0, #32]\n   add w12, w6, #1\n"
        "asr w12, w12, w11\n   asr w13, w6, w11\n   mov x14, #2\n   lsl x14, x14, x12\n"
        "mov x15, #1\n   lsl x15, x15, x13\n   sub x14, x14, x15\n   ldr x15, [x9]\n   orr x15, x15, x14\n   str x15, [x9]\n"
        "2:  ldrb w10, [x0, #41]\n   cbz w10, 19f\n   ldr w11, [x0, #36]\n   asr w12, w6, w11\n"
        "mov x14, #1\n   lsl x14, x14, x12\n   ldr x15, [x9, #8]\n   orr x15, x15, x14\n   str x15, [x9, #8]\n" FC_ASM_RET
        /* span_many */
        "4:  asr w6, w2, #10\n   asr w7, w3, #10\n   cmp w3, w2\n   cset w8, gt\n" /* w8=right */
        "ldr x9, [x0, #8]\n   cbz x9, 6f\n   add w10, w1, w1, lsl #1\n   add x9, x9, w10, sxtw #4\n"
        "cmp w8, #0\n   csel w12, w6, w7, ne\n   csel w13, w7, w6, ne\n" /* w12=lo_col, w13=hi_col */
        "ldrb w10, [x0, #40]\n   cbz w10, 5f\n   ldr w11, [x0, #32]\n   add w14, w13, #1\n"
        "asr w14, w14, w11\n   asr w15, w12, w11\n   mov x16, #2\n   lsl x16, x16, x14\n"
        "mov x17, #1\n   lsl x17, x17, x15\n   sub x16, x16, x17\n   ldr x17, [x9]\n   orr x17, x17, x16\n   str x17, [x9]\n"
        "5:  ldrb w10, [x0, #41]\n   cbz w10, 6f\n   ldr w11, [x0, #36]\n"
        "asr w14, w13, w11\n   asr w15, w12, w11\n   mov x16, #2\n   lsl x16, x16, x14\n"
        "mov x17, #1\n   lsl x17, x17, x15\n   sub x16, x16, x17\n   ldr x17, [x9, #8]\n   orr x17, x17, x16\n   str x17, [x9, #8]\n"
        "6:  sxtw x9, w2\n   sxtw x10, w3\n   sub x10, x10, x9\n   cmp w8, #0\n   cneg x10, x10, eq\n" /* x10=travel */
        "sxtw x11, w4\n   cmp w4, #0\n   cneg x11, x11, lt\n" /* x11=size */
        "mov w9, #1\n   cneg w9, w9, lt\n" /* w9=sign */
        "and w12, w2, #1023\n   mov w13, #1024\n   sub w14, w13, w12\n   cmp w8, #0\n   csel w14, w14, w12, ne\n" /* x14=first */
        "lsr x15, x10, #1\n   madd x15, x11, x14, x15\n   udiv x12, x15, x10\n   msub x13, x12, x10, x15\n" /* x12=done, x13=carry */
        "lsl x15, x11, #10\n   udiv x14, x15, x10\n   msub x15, x14, x10, x15\n" /* x14=lift, x15=spare */
        "mov w11, #1024\n   cmp w8, #0\n   csel w16, w11, wzr, ne\n   csel w1, wzr, w11, ne\n" /* w16=leave, w1=enter */
        "mov w11, #1\n   cneg w8, w11, eq\n" /* w8=step */
        "mul w11, w12, w9\n   and w2, w2, #1023\n   add w2, w2, w16\n   mul w2, w11, w2\n"
        "lsl w11, w11, #11\n   sub w11, w11, w2\n   add x16, x5, w6, sxtw #2\n"
        "ldp w17, w0, [x16]\n   add w17, w17, w11\n   add w0, w0, w2\n   stp w17, w0, [x16]\n"
        "add w6, w6, w8\n"
        "7:  cmp w6, w7\n   b.eq 9f\n   mov x16, x14\n   add x13, x13, x15\n   cmp x13, x10\n   b.lo 8f\n"
        "sub x13, x13, x10\n   add x16, x16, #1\n"
        "8:  add x12, x12, x16\n   mul w17, w16, w9\n   lsl w17, w17, #10\n   add x16, x5, w6, sxtw #2\n"
        "ldp w0, w11, [x16]\n   add w0, w0, w17\n   add w11, w11, w17\n   stp w0, w11, [x16]\n"
        "add w6, w6, w8\n   b 7b\n"
        "9:  msub w11, w12, w9, w4\n   and w3, w3, #1023\n   add w3, w3, w1\n   mul w3, w11, w3\n"
        "lsl w11, w11, #11\n   sub w11, w11, w3\n   add x16, x5, w7, sxtw #2\n"
        "ldp w17, w0, [x16]\n   add w17, w17, w11\n   add w0, w0, w3\n   stp w17, w0, [x16]\n"
        "19: " FC_ASM_RET
        FC_ASM_END(font_asm_span)

        /* font_asm_edge_rows(x0=band, x1=edge[7]{x0,y0,x1,y1,ya,top,sign}) */
        FC_ASM_FUNC(font_asm_edge_rows)
        "cbz x0, 19f\n   cbz x1, 19f\n   ldr x2, [x0]\n   cbz x2, 19f\n"
        "ldp w2, w3, [x1]\n   ldp w4, w5, [x1, #8]\n   ldp w6, w7, [x1, #16]\n   ldr w8, [x1, #24]\n"
        "sxtw x9, w5\n   sxtw x10, w3\n   subs x9, x9, x10\n   b.le 19f\n   cmp w6, w7\n   b.ge 19f\n"
        "stp x29, x30, [sp, #-128]!\n   stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n"
        "stp x23, x24, [sp, #48]\n   stp x25, x26, [sp, #64]\n   stp x27, x28, [sp, #80]\n"
        /* stack: [sp+96]=at, [sp+104]=carry, [sp+112]=lift, [sp+120]=spare */
        "mov x19, x0\n   mov w20, w2\n   mov w21, w3\n   mov w22, w5\n"
        "mov w23, w6\n   mov w24, w7\n   mov w25, w8\n   sxtw x27, w4\n   sxtw x10, w2\n"
        "sub x27, x27, x10\n   mov x28, x9\n   mov x10, #0\n   asr x11, x28, #1\n"
        "mov x12, #-1\n   stp x10, x12, [sp, #112]\n" /* lift=0, spare=-1 */
        "cmp w23, w21\n   b.eq 1f\n   sxtw x12, w23\n   sxtw x13, w21\n   sub x12, x12, x13\n"
        "madd x12, x27, x12, x11\n   sdiv x10, x12, x28\n   msub x11, x10, x28, x12\n"
        "cmp x11, #0\n   b.ge 1f\n   sub x10, x10, #1\n   add x11, x11, x28\n"
        "1:  stp x10, x11, [sp, #96]\n   add w26, w20, w10\n"
        "2:  cmp w23, w24\n   b.ge 9f\n   and w12, w23, #-1024\n   add w12, w12, #1024\n"
        "tst w23, #1023\n   b.ne 3f\n   cmp w12, w24\n   b.gt 3f\n"
        "ldp x13, x14, [sp, #112]\n   cmp x14, #0\n   b.ge 5f\n"
        "lsl x15, x27, #10\n   sdiv x13, x15, x28\n   msub x14, x13, x28, x15\n"
        "cmp x14, #0\n   b.ge 4f\n   sub x13, x13, #1\n   add x14, x14, x28\n"
        "4:  stp x13, x14, [sp, #112]\n"
        "5:  ldp x10, x11, [sp, #96]\n   add x10, x10, x13\n   add x11, x11, x14\n"
        "cmp x11, x28\n   b.lt 6f\n   sub x11, x11, x28\n   add x10, x10, #1\n"
        "6:  stp x10, x11, [sp, #96]\n   b 8f\n"
        "3:  cmp w12, w24\n   csel w12, w24, w12, gt\n   cmp w12, w22\n   b.ne 7f\n"
        "mov x10, x27\n   str x10, [sp, #96]\n   b 8f\n"
        "7:  sxtw x13, w12\n   sxtw x14, w21\n   sub x13, x13, x14\n   asr x14, x28, #1\n"
        "madd x13, x27, x13, x14\n   sdiv x10, x13, x28\n   msub x11, x10, x28, x13\n"
        "cmp x11, #0\n   b.ge 71f\n   sub x10, x10, #1\n   add x11, x11, x28\n"
        "71: stp x10, x11, [sp, #96]\n"
        "8:  add w3, w20, w10\n   mov w2, w26\n   mov w26, w3\n   sub w4, w12, w23\n"
        "mul w4, w4, w25\n   ldr w1, [x19, #16]\n   asr w13, w23, #10\n   sub w1, w13, w1\n"
        "mov w23, w12\n   mov x0, x19\n   bl " FC_SYM(font_asm_span) "\n   b 2b\n"
        "9:  ldp x27, x28, [sp, #80]\n   ldp x25, x26, [sp, #64]\n   ldp x23, x24, [sp, #48]\n"
        "ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #128\n"
        "19: " FC_ASM_RET
        FC_ASM_END(font_asm_edge_rows)

        /* font_asm_trace(x0=band, w1=x0, w2=y0, w3=x1, w4=y1) */
        FC_ASM_FUNC(font_asm_trace)
        "cbz x0, 9f\n   ldr x5, [x0]\n   cbz x5, 9f\n   cmp w2, w4\n   b.eq 9f\n"
        "mov w5, #1\n   cmp w2, w4\n   b.le 1f\n   mov w6, w1\n   mov w1, w3\n   mov w3, w6\n"
        "mov w6, w2\n   mov w2, w4\n   mov w4, w6\n   mov w5, #-1\n"
        "1:  asr w6, w2, #10\n   sub w7, w4, #1\n   asr w8, w7, #10\n   cmp w6, w8\n   b.ne 2f\n"
        "ldr w9, [x0, #16]\n   sub w9, w6, w9\n   sub w10, w4, w2\n   mul w4, w10, w5\n"
        "mov w2, w1\n   mov w1, w9\n   b " FC_SYM(font_asm_span) "\n"
        "2:  sub w9, w8, w6\n   cmp w9, #1\n   b.ne 5f\n"
        "stp x29, x30, [sp, #-64]!\n   stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n"
        "mov x19, x0\n   and w20, w7, #-1024\n   mov w21, w3\n   sub w10, w4, w20\n   mul w22, w10, w5\n"
        "sxtw x11, w3\n   sxtw x12, w1\n   sub x11, x11, x12\n   sxtw x13, w4\n   sxtw x14, w2\n   sub x13, x13, x14\n"
        "sxtw x15, w20\n   sub x15, x15, x14\n   asr x16, x13, #1\n   madd x15, x11, x15, x16\n"
        "sdiv x11, x15, x13\n   msub x12, x11, x13, x15\n   cmp x12, #0\n   b.ge 3f\n   sub x11, x11, #1\n"
        "3:  add w3, w1, w11\n   mov w2, w1\n   ldr w10, [x19, #16]\n   sub w1, w6, w10\n"
        "sub w4, w20, w14\n   mul w4, w4, w5\n   mov w14, w3\n   mov x0, x19\n   str w14, [sp, #48]\n"
        "bl " FC_SYM(font_asm_span) "\n   ldr w2, [sp, #48]\n   mov x0, x19\n   ldr w10, [x19, #16]\n"
        "asr w1, w20, #10\n   sub w1, w1, w10\n   mov w3, w21\n   mov w4, w22\n"
        "ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #64\n"
        "b " FC_SYM(font_asm_span) "\n"
        "5:  stp x29, x30, [sp, #-48]!\n   stp w1, w2, [sp, #16]\n   stp w3, w4, [sp, #24]\n"
        "stp w2, w4, [sp, #32]\n   str w5, [sp, #40]\n   add x1, sp, #16\n"
        "bl " FC_SYM(font_asm_edge_rows) "\n   ldp x29, x30, [sp], #48\n"
        "9:  " FC_ASM_RET
        FC_ASM_END(font_asm_trace)
);

/* ========================================================================== */
/*                                   X86-64                                   */
/* ========================================================================== */
#elif defined(FONT_CORE_X64)

#if defined(_WIN32)
/* Win64 adapters: move (rcx,rdx,r8,r9,[rsp+40],[rsp+48]) -> SysV (rdi,rsi,rdx,rcx,r8,r9) */
# define FC_X64_PRO2 "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n"
# define FC_X64_PRO3 "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n"
# define FC_X64_PRO4 "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n"
# define FC_X64_PRO5 "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 56]\n"
# define FC_X64_PRO6 "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 56]\n   mov r9, [rsp + 64]\n"
# define FC_X64_EPI  "pop rsi\n   pop rdi\n"
#else
# define FC_X64_PRO2 ""
# define FC_X64_PRO3 ""
# define FC_X64_PRO4 ""
# define FC_X64_PRO5 ""
# define FC_X64_PRO6 ""
# define FC_X64_EPI  ""
#endif

__asm__(
        ".intel_syntax noprefix\n"
        FC_ASM_SECTION

        /* font_asm_search(rdi=face, rsi=table, edx=count, rcx=stride, r8d=off_bytes, r9d=value) -> eax */
        FC_ASM_FUNC(font_asm_search)
        FC_X64_PRO6
        "test rdi, rdi\n   jz 9f\n   test edx, edx\n   jz 9f\n   push rbx\n"
        "mov r10, [rdi]\n   mov r11, [rdi + 8]\n   mov eax, r8d\n   shr eax, 1\n   add rsi, rax\n"
        "xor ebx, ebx\n   test r8d, 1\n   jnz 4f\n   lea rdi, [r11 - 2]\n"
        "1:  cmp ebx, edx\n   jae 8f\n   mov eax, edx\n   sub eax, ebx\n   shr eax, 1\n   add eax, ebx\n"
        "mov r8d, eax\n   imul r8, rcx\n   add r8, rsi\n   xor eax, eax\n"
        "test r10, r10\n   jz 2f\n   cmp r11, 2\n   jb 2f\n   cmp r8, rdi\n   ja 2f\n"
        "movzx eax, word ptr [r10 + r8]\n   rol ax, 8\n"
        "2:  mov r8d, edx\n   sub r8d, ebx\n   shr r8d, 1\n   add r8d, ebx\n"
        "cmp eax, r9d\n   jae 3f\n   lea ebx, [r8d + 1]\n   jmp 1b\n"
        "3:  mov edx, r8d\n   jmp 1b\n"
        "4:  lea rdi, [r11 - 4]\n"
        "5:  cmp ebx, edx\n   jae 8f\n   mov eax, edx\n   sub eax, ebx\n   shr eax, 1\n   add eax, ebx\n"
        "mov r8d, eax\n   imul r8, rcx\n   add r8, rsi\n   xor eax, eax\n"
        "test r10, r10\n   jz 6f\n   cmp r11, 4\n   jb 6f\n   cmp r8, rdi\n   ja 6f\n"
        "mov eax, dword ptr [r10 + r8]\n   bswap eax\n"
        "6:  mov r8d, edx\n   sub r8d, ebx\n   shr r8d, 1\n   add r8d, ebx\n"
        "cmp eax, r9d\n   jae 7f\n   lea ebx, [r8d + 1]\n   jmp 5b\n"
        "7:  mov edx, r8d\n   jmp 5b\n"
        "8:  mov eax, ebx\n   pop rbx\n" FC_X64_EPI FC_ASM_RET
        "9:  xor eax, eax\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_search)

        /* font_asm_glyph(rdi=face, esi=code) -> eax */
        FC_ASM_FUNC(font_asm_glyph)
        FC_X64_PRO2
        "test rdi, rdi\n   jz 19f\n   mov rax, [rdi]\n   test rax, rax\n   jz 19f\n"
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, rax\n   mov r13, [rdi + 8]\n   mov r14, [rdi + 48]\n"
        "mov r15d, dword ptr [rdi + 96]\n   movzx ebx, byte ptr [rdi + 146]\n"
        "movzx edx, byte ptr [rdi + 147]\n   xor ecx, ecx\n   test edx, edx\n   jz 1f\n"
        "cmp esi, 0x100\n   setb cl\n"
        "1:  shl ecx, 8\n   or ebx, ecx\n" /* bl=fmt, bh=try_sym */
        "100: xor r9d, r9d\n   cmp bl, 12\n   jne 7f\n   cmp r13, 16\n   jb 18f\n"
        "lea rax, [r13 - 16]\n   cmp r14, rax\n   ja 18f\n"
        "mov ecx, dword ptr [r12 + r14 + 12]\n   bswap ecx\n   sub rax, r14\n"
        "xor edx, edx\n   mov r8d, 12\n   div r8\n   cmp rcx, rax\n   cmova rcx, rax\n"
        "mov r10d, ecx\n   xor r8d, r8d\n   mov edx, ecx\n   lea rdi, [r13 - 4]\n"
        "2:  cmp r8d, edx\n   jae 5f\n   mov eax, edx\n   sub eax, r8d\n   shr eax, 1\n   add eax, r8d\n"
        "lea rcx, [rax + rax * 2]\n   lea rcx, [r14 + rcx * 4 + 20]\n   xor r11d, r11d\n"
        "cmp r13, 4\n   jb 3f\n   cmp rcx, rdi\n   ja 3f\n   mov r11d, dword ptr [r12 + rcx]\n   bswap r11d\n"
        "3:  cmp r11d, esi\n   jae 4f\n   lea r8d, [eax + 1]\n   jmp 2b\n"
        "4:  mov edx, eax\n   jmp 2b\n"
        "5:  cmp r8d, r10d\n   jae 18f\n   lea rcx, [r8 + r8 * 2]\n   lea rcx, [r14 + rcx * 4 + 16]\n"
        "xor r11d, r11d\n   cmp r13, 4\n   jb 6f\n   cmp rcx, rdi\n   ja 6f\n   mov r11d, dword ptr [r12 + rcx]\n   bswap r11d\n"
        "6:  cmp r11d, esi\n   ja 18f\n   add rcx, 8\n   xor eax, eax\n"
        "cmp r13, 4\n   jb 61f\n   cmp rcx, rdi\n   ja 61f\n   mov eax, dword ptr [r12 + rcx]\n   bswap eax\n"
        "61: mov r9d, esi\n   add r9, rax\n   sub r9, r11\n   jmp 18f\n"
        "7:  cmp bl, 4\n   jne 18f\n   cmp esi, 0xffff\n   ja 18f\n   lea rdi, [r13 - 2]\n"
        "lea rcx, [r14 + 6]\n   xor r10d, r10d\n   cmp r13, 2\n   jb 8f\n   cmp rcx, rdi\n   ja 8f\n"
        "movzx r10d, word ptr [r12 + rcx]\n   rol r10w, 8\n"
        "8:  shr r10d, 1\n   xor r8d, r8d\n   mov edx, r10d\n"
        "9:  cmp r8d, edx\n   jae 12f\n   mov eax, edx\n   sub eax, r8d\n   shr eax, 1\n   add eax, r8d\n"
        "lea rcx, [r14 + rax * 2 + 14]\n   xor r11d, r11d\n   cmp r13, 2\n   jb 10f\n   cmp rcx, rdi\n   ja 10f\n"
        "movzx r11d, word ptr [r12 + rcx]\n   rol r11w, 8\n"
        "10: cmp r11d, esi\n   jae 11f\n   lea r8d, [eax + 1]\n   jmp 9b\n"
        "11: mov edx, eax\n   jmp 9b\n"
        "12: cmp r8d, r10d\n   jae 18f\n   lea rdx, [r14 + r10 * 2 + 16]\n   lea rcx, [rdx + r8 * 2]\n"
        "xor r11d, r11d\n   cmp r13, 2\n   jb 13f\n   cmp rcx, rdi\n   ja 13f\n   movzx r11d, word ptr [r12 + rcx]\n   rol r11w, 8\n"
        "13: cmp esi, r11d\n   jb 18f\n   lea rdx, [rdx + r10 * 2]\n   lea rcx, [rdx + r8 * 2]\n"
        "xor eax, eax\n   cmp r13, 2\n   jb 14f\n   cmp rcx, rdi\n   ja 14f\n   movzx eax, word ptr [r12 + rcx]\n   rol ax, 8\n"
        "14: lea rdx, [rdx + r10 * 2]\n   lea rcx, [rdx + r8 * 2]\n   xor edx, edx\n"
        "cmp r13, 2\n   jb 15f\n   cmp rcx, rdi\n   ja 15f\n   movzx edx, word ptr [r12 + rcx]\n   rol dx, 8\n"
        "15: test edx, edx\n   jz 16f\n   mov r8d, esi\n   sub r8d, r11d\n   add rcx, rdx\n   lea rcx, [rcx + r8 * 2]\n"
        "xor r9d, r9d\n   cmp r13, 2\n   jb 17f\n   cmp rcx, rdi\n   ja 17f\n   movzx r9d, word ptr [r12 + rcx]\n   rol r9w, 8\n   jmp 17f\n"
        "16: mov r9d, esi\n"
        "17: test r9, r9\n   jz 18f\n   add r9, rax\n   and r9d, 0xffff\n"
        "18: xor eax, eax\n   cmp r9, r15\n   cmovb eax, r9d\n   test eax, eax\n   jnz 20f\n"
        "test bh, bh\n   jz 20f\n   xor bh, bh\n   or esi, 0xf000\n   jmp 100b\n"
        "20: pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" FC_X64_EPI FC_ASM_RET
        "19: xor eax, eax\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_glyph)

        /* font_asm_coverage(rdi=face, rsi=table, edx=glyph) -> eax */
        FC_ASM_FUNC(font_asm_coverage)
        FC_X64_PRO3
        "test rdi, rdi\n   jz 9f\n   mov r8, [rdi]\n   mov r9, [rdi + 8]\n   test r8, r8\n   jz 9f\n   cmp r9, 2\n   jb 9f\n"
        "lea r10, [r9 - 2]\n   xor ecx, ecx\n   cmp rsi, r10\n   ja 1f\n   movzx ecx, word ptr [r8 + rsi]\n   rol cx, 8\n"
        "1:  lea rax, [rsi + 2]\n   xor r11d, r11d\n   cmp rax, r10\n   ja 2f\n   movzx r11d, word ptr [r8 + rax]\n   rol r11w, 8\n"
        "2:  add rsi, 4\n   xor r9d, r9d\n   mov edi, r11d\n   cmp ecx, 1\n   je 3f\n   cmp ecx, 2\n   jne 9f\n"
        "6:  cmp r9d, edi\n   jae 8f\n   mov eax, edi\n   sub eax, r9d\n   shr eax, 1\n   add eax, r9d\n"
        "lea rcx, [rax + rax * 2]\n   lea rcx, [rsi + rcx * 2 + 2]\n   xor eax, eax\n"
        "cmp rcx, r10\n   ja 7f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "7:  mov ecx, edi\n   sub ecx, r9d\n   shr ecx, 1\n   add ecx, r9d\n"
        "cmp eax, edx\n   jae 71f\n   lea r9d, [ecx + 1]\n   jmp 6b\n"
        "71: mov edi, ecx\n   jmp 6b\n"
        "8:  cmp r9d, r11d\n   jae 9f\n   lea rcx, [r9 + r9 * 2]\n   lea rcx, [rsi + rcx * 2]\n"
        "xor eax, eax\n   cmp rcx, r10\n   ja 81f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "81: cmp eax, edx\n   ja 9f\n   add rcx, 4\n   xor edi, edi\n"
        "cmp rcx, r10\n   ja 82f\n   movzx edi, word ptr [r8 + rcx]\n   rol di, 8\n"
        "82: sub edx, eax\n   lea eax, [edi + edx]\n" FC_X64_EPI FC_ASM_RET
        "3:  cmp r9d, edi\n   jae 5f\n   mov eax, edi\n   sub eax, r9d\n   shr eax, 1\n   add eax, r9d\n"
        "lea rcx, [rsi + rax * 2]\n   xor eax, eax\n   cmp rcx, r10\n   ja 4f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "4:  mov ecx, edi\n   sub ecx, r9d\n   shr ecx, 1\n   add ecx, r9d\n"
        "cmp eax, edx\n   jae 41f\n   lea r9d, [ecx + 1]\n   jmp 3b\n"
        "41: mov edi, ecx\n   jmp 3b\n"
        "5:  cmp r9d, r11d\n   jae 9f\n   lea rcx, [rsi + r9 * 2]\n   xor eax, eax\n"
        "cmp rcx, r10\n   ja 51f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "51: cmp eax, edx\n   jne 9f\n   mov eax, r9d\n" FC_X64_EPI FC_ASM_RET
        "9:  mov eax, -1\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_coverage)

        /* font_asm_class(rdi=face, rsi=table, edx=glyph) -> eax */
        FC_ASM_FUNC(font_asm_class)
        FC_X64_PRO3
        "test rdi, rdi\n   jz 9f\n   mov r8, [rdi]\n   mov r9, [rdi + 8]\n   test r8, r8\n   jz 9f\n   cmp r9, 2\n   jb 9f\n"
        "lea r10, [r9 - 2]\n   xor ecx, ecx\n   cmp rsi, r10\n   ja 1f\n   movzx ecx, word ptr [r8 + rsi]\n   rol cx, 8\n"
        "1:  cmp ecx, 1\n   jne 3f\n   lea rax, [rsi + 2]\n   xor r11d, r11d\n   cmp rax, r10\n   ja 11f\n"
        "movzx r11d, word ptr [r8 + rax]\n   rol r11w, 8\n"
        "11: cmp edx, r11d\n   jb 9f\n   lea rax, [rsi + 4]\n   xor edi, edi\n   cmp rax, r10\n   ja 12f\n"
        "movzx edi, word ptr [r8 + rax]\n   rol di, 8\n"
        "12: sub edx, r11d\n   cmp edx, edi\n   jae 9f\n   lea rax, [rsi + rdx * 2 + 6]\n"
        "xor ecx, ecx\n   cmp rax, r10\n   ja 2f\n   movzx ecx, word ptr [r8 + rax]\n   rol cx, 8\n"
        "2:  mov eax, ecx\n" FC_X64_EPI FC_ASM_RET
        "3:  cmp ecx, 2\n   jne 9f\n   lea rax, [rsi + 2]\n   xor r11d, r11d\n   cmp rax, r10\n   ja 4f\n"
        "movzx r11d, word ptr [r8 + rax]\n   rol r11w, 8\n"
        "4:  add rsi, 4\n   xor r9d, r9d\n   mov edi, r11d\n"
        "5:  cmp r9d, edi\n   jae 7f\n   mov eax, edi\n   sub eax, r9d\n   shr eax, 1\n   add eax, r9d\n"
        "lea rcx, [rax + rax * 2]\n   lea rcx, [rsi + rcx * 2 + 2]\n   xor eax, eax\n"
        "cmp rcx, r10\n   ja 6f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "6:  mov ecx, edi\n   sub ecx, r9d\n   shr ecx, 1\n   add ecx, r9d\n"
        "cmp eax, edx\n   jae 61f\n   lea r9d, [ecx + 1]\n   jmp 5b\n"
        "61: mov edi, ecx\n   jmp 5b\n"
        "7:  cmp r9d, r11d\n   jae 9f\n   lea rcx, [r9 + r9 * 2]\n   lea rcx, [rsi + rcx * 2]\n"
        "xor eax, eax\n   cmp rcx, r10\n   ja 71f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "71: cmp eax, edx\n   ja 9f\n   add rcx, 4\n   xor eax, eax\n"
        "cmp rcx, r10\n   ja 8f\n   movzx eax, word ptr [r8 + rcx]\n   rol ax, 8\n"
        "8:  " FC_X64_EPI FC_ASM_RET
        "9:  xor eax, eax\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_class)

        /* font_asm_pair_at(rdi=face, rsi=table, edx=left, ecx=right, r8=first_at, r9=second_at) -> eax */
        FC_ASM_FUNC(font_asm_pair_at)
        FC_X64_PRO6
        "test rdi, rdi\n   jz 19f\n   test r8, r8\n   jz 19f\n   test r9, r9\n   jz 19f\n"
        "mov rax, [rdi]\n   mov r10, [rdi + 8]\n   test rax, rax\n   jz 19f\n   cmp r10, 2\n   jb 19f\n"
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 24\n"
        "mov [rsp], r8\n   mov [rsp + 8], r9\n   mov r12, rdi\n   mov r13, rsi\n   mov r14d, edx\n   mov r15d, ecx\n"
        "lea r11, [r10 - 2]\n   xor ebx, ebx\n   cmp rsi, r11\n   ja 1f\n   movzx ebx, word ptr [rax + rsi]\n   rol bx, 8\n"
        "1:  lea rdx, [rsi + 4]\n   xor ecx, ecx\n   cmp rdx, r11\n   ja 2f\n   movzx ecx, word ptr [rax + rdx]\n   rol cx, 8\n"
        "2:  and ecx, 0xff\n   mov edx, ecx\n   shr edx, 1\n   and edx, 0x55\n   sub ecx, edx\n"
        "mov edx, ecx\n   and edx, 0x33\n   shr ecx, 2\n   and ecx, 0x33\n   add ecx, edx\n"
        "mov edx, ecx\n   shr edx, 4\n   add ecx, edx\n   and ecx, 0x0f\n   mov [rsp + 16], ecx\n"
        "lea rdx, [rsi + 6]\n   xor ebp, ebp\n   cmp rdx, r11\n   ja 3f\n   movzx ebp, word ptr [rax + rdx]\n   rol bp, 8\n"
        "3:  and ebp, 0xff\n   mov edx, ebp\n   shr edx, 1\n   and edx, 0x55\n   sub ebp, edx\n"
        "mov edx, ebp\n   and edx, 0x33\n   shr ebp, 2\n   and ebp, 0x33\n   add ebp, edx\n"
        "mov edx, ebp\n   shr edx, 4\n   add ebp, edx\n   and ebp, 0x0f\n   add ebp, ecx\n   add ebp, ebp\n"
        "lea rdx, [rsi + 2]\n   xor ecx, ecx\n   cmp rdx, r11\n   ja 4f\n   movzx ecx, word ptr [rax + rdx]\n   rol cx, 8\n"
        "4:  mov rdi, r12\n   lea rsi, [r13 + rcx]\n   mov edx, r14d\n"
#if defined(_WIN32)
        "mov rcx, rdi\n   mov rdx, rsi\n   mov r8d, r14d\n   sub rsp, 32\n   call " FC_SYM(font_asm_coverage) "\n   add rsp, 32\n"
#else
        "call " FC_SYM(font_asm_coverage) "\n"
#endif
        "test eax, eax\n   js 18f\n   mov r8, [r12]\n   mov r9, [r12 + 8]\n   lea r11, [r9 - 2]\n"
        "cmp ebx, 1\n   jne 8f\n   lea rdx, [r13 + 8]\n   xor ecx, ecx\n   cmp rdx, r11\n   ja 5f\n"
        "movzx ecx, word ptr [r8 + rdx]\n   rol cx, 8\n"
        "5:  cmp eax, ecx\n   jae 18f\n   lea rdx, [r13 + rax * 2 + 10]\n   xor ecx, ecx\n"
        "cmp rdx, r11\n   ja 6f\n   movzx ecx, word ptr [r8 + rdx]\n   rol cx, 8\n"
        "6:  lea r14, [r13 + rcx]\n   xor ebx, ebx\n   cmp r14, r11\n   ja 7f\n   movzx ebx, word ptr [r8 + r14]\n   rol bx, 8\n"
        "7:  lea rcx, [rbp + 2]\n   mov rdi, r12\n   lea rsi, [r14 + 2]\n   mov edx, ebx\n   xor r8d, r8d\n   mov r9d, r15d\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov [rsp + 40], r9\n   mov [rsp + 32], r8\n   mov r9, rcx\n   mov r8d, ebx\n   mov rdx, rsi\n   mov rcx, r12\n"
        "call " FC_SYM(font_asm_search) "\n   add rsp, 48\n"
#else
        "call " FC_SYM(font_asm_search) "\n"
#endif
        "cmp eax, ebx\n   jae 18f\n   lea rcx, [rbp + 2]\n   imul rcx, rax\n   lea r10, [r14 + rcx + 2]\n"
        "mov r8, [r12]\n   mov r9, [r12 + 8]\n   lea r11, [r9 - 2]\n   xor ecx, ecx\n"
        "cmp r10, r11\n   ja 71f\n   movzx ecx, word ptr [r8 + r10]\n   rol cx, 8\n"
        "71: cmp ecx, r15d\n   jne 18f\n   add r10, 2\n   jmp 15f\n"
        "8:  cmp ebx, 2\n   jne 18f\n   lea rdx, [r13 + 8]\n   xor ecx, ecx\n   cmp rdx, r11\n   ja 9f\n"
        "movzx ecx, word ptr [r8 + rdx]\n   rol cx, 8\n"
        "9:  mov rdi, r12\n   lea rsi, [r13 + rcx]\n   mov edx, r14d\n"
#if defined(_WIN32)
        "mov rcx, r12\n   mov rdx, rsi\n   mov r8d, r14d\n   sub rsp, 32\n   call " FC_SYM(font_asm_class) "\n   add rsp, 32\n"
#else
        "call " FC_SYM(font_asm_class) "\n"
#endif
        "mov r14d, eax\n   mov r8, [r12]\n   mov r9, [r12 + 8]\n   lea r11, [r9 - 2]\n"
        "lea rdx, [r13 + 10]\n   xor ecx, ecx\n   cmp rdx, r11\n   ja 10f\n   movzx ecx, word ptr [r8 + rdx]\n   rol cx, 8\n"
        "10: mov rdi, r12\n   lea rsi, [r13 + rcx]\n   mov edx, r15d\n"
#if defined(_WIN32)
        "mov rcx, r12\n   mov rdx, rsi\n   mov r8d, r15d\n   sub rsp, 32\n   call " FC_SYM(font_asm_class) "\n   add rsp, 32\n"
#else
        "call " FC_SYM(font_asm_class) "\n"
#endif
        "mov r15d, eax\n   mov r8, [r12]\n   mov r9, [r12 + 8]\n   lea r11, [r9 - 2]\n"
        "lea rdx, [r13 + 12]\n   xor ecx, ecx\n   cmp rdx, r11\n   ja 11f\n   movzx ecx, word ptr [r8 + rdx]\n   rol cx, 8\n"
        "11: cmp r14d, ecx\n   jae 18f\n   lea rdx, [r13 + 14]\n   xor ecx, ecx\n"
        "cmp rdx, r11\n   ja 12f\n   movzx ecx, word ptr [r8 + rdx]\n   rol cx, 8\n"
        "12: cmp r15d, ecx\n   jae 18f\n   imul r14, rcx\n   add r14, r15\n   imul r14, rbp\n   lea r10, [r13 + r14 + 16]\n"
        "15: mov r8, [rsp]\n   mov r9, [rsp + 8]\n   mov [r8], r10\n"
        "mov ecx, [rsp + 16]\n   lea r10, [r10 + rcx * 2]\n   mov [r9], r10\n   mov eax, 1\n   jmp 17f\n"
        "18: xor eax, eax\n"
        "17: add rsp, 24\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" FC_X64_EPI FC_ASM_RET
        "19: xor eax, eax\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_pair_at)

        /* font_asm_range_find(rdi=table, esi=count, edx=shift, ecx=code) -> eax */
        FC_ASM_FUNC(font_asm_range_find)
        FC_X64_PRO4
        "test rdi, rdi\n   jz 4f\n   test esi, esi\n   jz 4f\n   mov r8d, ecx\n   mov ecx, edx\n   xor r9d, r9d\n"
        "1:  mov eax, esi\n   sub eax, r9d\n   cmp eax, 1\n   jbe 3f\n   shr eax, 1\n   add eax, r9d\n"
        "mov r10d, dword ptr [rdi + rax * 4]\n   shr r10d, cl\n   cmp r10d, r8d\n"
        "cmovbe r9d, eax\n   cmova esi, eax\n   jmp 1b\n"
        "3:  mov eax, dword ptr [rdi + r9 * 4]\n   mov edx, 1\n   shl edx, cl\n   dec edx\n   and eax, edx\n" FC_X64_EPI FC_ASM_RET
        "4:  xor eax, eax\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_range_find)

        /* font_asm_lower_bound(rdi=values, esi=count, edx=stride, ecx=wide, r8d=code) -> eax */
        FC_ASM_FUNC(font_asm_lower_bound)
        FC_X64_PRO5
        "test rdi, rdi\n   jz 9f\n   test esi, esi\n   jz 9f\n   xor r9d, r9d\n   mov r10d, edx\n   test ecx, ecx\n   jnz 4f\n"
        "shl r10, 1\n"
        "1:  cmp r9d, esi\n   jae 8f\n   mov eax, esi\n   sub eax, r9d\n   shr eax, 1\n   add eax, r9d\n"
        "mov r11d, eax\n   imul r11, r10\n   movzx ecx, word ptr [rdi + r11]\n   lea edx, [eax + 1]\n"
        "cmp ecx, r8d\n   cmovb r9d, edx\n   cmovae esi, eax\n   jmp 1b\n"
        "4:  shl r10, 2\n"
        "5:  cmp r9d, esi\n   jae 8f\n   mov eax, esi\n   sub eax, r9d\n   shr eax, 1\n   add eax, r9d\n"
        "mov r11d, eax\n   imul r11, r10\n   mov ecx, dword ptr [rdi + r11]\n   lea edx, [eax + 1]\n"
        "cmp ecx, r8d\n   cmovb r9d, edx\n   cmovae esi, eax\n   jmp 5b\n"
        "8:  mov eax, r9d\n" FC_X64_EPI FC_ASM_RET
        "9:  xor eax, eax\n" FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_lower_bound)

        /* font_asm_span(rdi=band, esi=row, edx=xa, ecx=xb, r8d=cover) */
        FC_ASM_FUNC(font_asm_span)
        FC_X64_PRO5
        "test rdi, rdi\n   jz 19f\n   mov r9, [rdi]\n   test r9, r9\n   jz 19f\n"
        "test esi, esi\n   js 19f\n   cmp esi, dword ptr [rdi + 20]\n   jge 19f\n"
        "mov eax, dword ptr [rdi + 28]\n   shl eax, 10\n   xor r10d, r10d\n"
        "test edx, edx\n   cmovs edx, r10d\n   cmp edx, eax\n   cmovg edx, eax\n"
        "test ecx, ecx\n   cmovs ecx, r10d\n   cmp ecx, eax\n   cmovg ecx, eax\n"
        "mov eax, dword ptr [rdi + 24]\n   imul rax, rsi\n   lea r9, [r9 + rax * 4]\n"
        "mov eax, edx\n   xor eax, ecx\n   sar eax, 10\n   jnz 4f\n"
        /* span_one */
        "mov r10d, edx\n   sar r10d, 10\n   and edx, 1023\n   and ecx, 1023\n   add edx, ecx\n"
        "imul edx, r8d\n   shl r8d, 11\n   sub r8d, edx\n   movsxd r11, r10d\n"
        "add dword ptr [r9 + r11 * 4], r8d\n   add dword ptr [r9 + r11 * 4 + 4], edx\n"
        "mov r9, [rdi + 8]\n   test r9, r9\n   jz 19f\n   lea rax, [rsi + rsi * 2]\n   shl rax, 4\n   add r9, rax\n"
        "cmp byte ptr [rdi + 40], 0\n   je 2f\n   mov ecx, dword ptr [rdi + 32]\n"
        "lea eax, [r10d + 1]\n   sar eax, cl\n   mov r8d, r10d\n   sar r8d, cl\n"
        "mov ecx, eax\n   mov r11d, 2\n   shl r11, cl\n   mov ecx, r8d\n   mov eax, 1\n   shl rax, cl\n"
        "sub r11, rax\n   or [r9], r11\n"
        "2:  cmp byte ptr [rdi + 41], 0\n   je 19f\n   mov ecx, dword ptr [rdi + 36]\n"
        "sar r10d, cl\n   mov ecx, r10d\n   mov eax, 1\n   shl rax, cl\n   or [r9 + 8], rax\n" FC_X64_EPI FC_ASM_RET
        /* span_many */
        "4:  push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 40\n"
        "mov [rsp], r9\n   mov [rsp + 8], r8d\n   mov [rsp + 12], edx\n   mov [rsp + 16], ecx\n"
        "mov r10d, edx\n   sar r10d, 10\n   mov r11d, ecx\n   sar r11d, 10\n"
        "cmp ecx, edx\n   setg bl\n   movzx ebx, bl\n" /* ebx=right */
        "mov r9, [rdi + 8]\n   test r9, r9\n   jz 6f\n   lea rax, [rsi + rsi * 2]\n   shl rax, 4\n   add r9, rax\n"
        "test ebx, ebx\n   mov r12d, r10d\n   cmovz r12d, r11d\n   mov r13d, r11d\n   cmovz r13d, r10d\n"
        "cmp byte ptr [rdi + 40], 0\n   je 5f\n   mov ecx, dword ptr [rdi + 32]\n"
        "lea eax, [r13d + 1]\n   sar eax, cl\n   mov edx, r12d\n   sar edx, cl\n"
        "mov ecx, eax\n   mov r14d, 2\n   shl r14, cl\n   mov ecx, edx\n   mov eax, 1\n   shl rax, cl\n"
        "sub r14, rax\n   or [r9], r14\n"
        "5:  cmp byte ptr [rdi + 41], 0\n   je 6f\n   mov ecx, dword ptr [rdi + 36]\n"
        "mov eax, r13d\n   sar eax, cl\n   mov edx, r12d\n   sar edx, cl\n"
        "mov ecx, eax\n   mov r14d, 2\n   shl r14, cl\n   mov ecx, edx\n   mov eax, 1\n   shl rax, cl\n"
        "sub r14, rax\n   or [r9 + 8], r14\n"
        "6:  movsxd r12, dword ptr [rsp + 16]\n   movsxd rax, dword ptr [rsp + 12]\n   sub r12, rax\n"
        "test ebx, ebx\n   jnz 61f\n   neg r12\n"
        "61: movsxd r13, dword ptr [rsp + 8]\n   mov ebp, 1\n   test r13, r13\n   jns 62f\n   neg r13\n   mov ebp, -1\n"
        "62: mov eax, dword ptr [rsp + 12]\n   and eax, 1023\n   mov ecx, 1024\n   sub ecx, eax\n"
        "test ebx, ebx\n   cmovnz eax, ecx\n" /* rax=first */
        "imul rax, r13\n   mov rdx, r12\n   shr rdx, 1\n   add rax, rdx\n   xor edx, edx\n   div r12\n"
        "mov r14, rax\n   mov r15, rdx\n" /* r14=done, r15=carry */
        "mov rax, r13\n   shl rax, 10\n   xor edx, edx\n   div r12\n"
        "mov [rsp + 24], rax\n   mov [rsp + 32], rdx\n" /* lift, spare */
        "mov edi, 1024\n   xor esi, esi\n   test ebx, ebx\n   cmovz edi, esi\n" /* edi=leave */
        "mov esi, 1024\n   sub esi, edi\n   mov [rsp + 20], esi\n" /* enter */
        "mov eax, 1\n   mov ecx, -1\n   test ebx, ebx\n   cmovz eax, ecx\n   mov ebx, eax\n" /* ebx=step */
        "mov eax, r14d\n   imul eax, ebp\n   mov edx, dword ptr [rsp + 12]\n   and edx, 1023\n   add edx, edi\n"
        "imul edx, eax\n   shl eax, 11\n   sub eax, edx\n   mov r9, [rsp]\n   movsxd rcx, r10d\n"
        "add dword ptr [r9 + rcx * 4], eax\n   add dword ptr [r9 + rcx * 4 + 4], edx\n"
        "add r10d, ebx\n   mov rdi, [rsp + 24]\n   mov rsi, [rsp + 32]\n"
        "7:  cmp r10d, r11d\n   je 9f\n   mov rax, rdi\n   add r15, rsi\n   cmp r15, r12\n   jb 8f\n"
        "sub r15, r12\n   inc rax\n"
        "8:  add r14, rax\n   imul eax, ebp\n   shl eax, 10\n   movsxd rcx, r10d\n"
        "add dword ptr [r9 + rcx * 4], eax\n   add dword ptr [r9 + rcx * 4 + 4], eax\n"
        "add r10d, ebx\n   jmp 7b\n"
        "9:  imul r14d, ebp\n   mov eax, dword ptr [rsp + 8]\n   sub eax, r14d\n"
        "mov edx, dword ptr [rsp + 16]\n   and edx, 1023\n   add edx, dword ptr [rsp + 20]\n"
        "imul edx, eax\n   shl eax, 11\n   sub eax, edx\n   movsxd rcx, r11d\n"
        "add dword ptr [r9 + rcx * 4], eax\n   add dword ptr [r9 + rcx * 4 + 4], edx\n"
        "add rsp, 40\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
        "19: " FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_span)

        /* font_asm_edge_rows(rdi=band, rsi=edge[7]) */
        FC_ASM_FUNC(font_asm_edge_rows)
        FC_X64_PRO2
        "test rdi, rdi\n   jz 19f\n   test rsi, rsi\n   jz 19f\n   cmp qword ptr [rdi], 0\n   je 19f\n"
        "movsxd r8, dword ptr [rsi + 12]\n   movsxd r9, dword ptr [rsi + 4]\n   sub r8, r9\n   jle 19f\n"
        "mov eax, dword ptr [rsi + 16]\n   cmp eax, dword ptr [rsi + 20]\n   jge 19f\n"
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   sub rsp, 56\n"
        /* [rsp+0]=at, [rsp+8]=carry, [rsp+16]=lift, [rsp+24]=spare, [rsp+32]=x0, [rsp+36]=y0, [rsp+40]=y1, [rsp+44]=top, [rsp+48]=sign */
        "mov r12, rdi\n   mov r14, r8\n" /* r12=band, r14=dy */
        "movsxd r13, dword ptr [rsi + 8]\n   movsxd rax, dword ptr [rsi]\n   sub r13, rax\n" /* r13=dx */
        "mov eax, dword ptr [rsi]\n   mov [rsp + 32], eax\n   mov eax, dword ptr [rsi + 4]\n   mov [rsp + 36], eax\n"
        "mov eax, dword ptr [rsi + 12]\n   mov [rsp + 40], eax\n   mov eax, dword ptr [rsi + 20]\n   mov [rsp + 44], eax\n"
        "mov eax, dword ptr [rsi + 24]\n   mov [rsp + 48], eax\n   mov r15d, dword ptr [rsi + 16]\n" /* r15d=ya */
        "mov qword ptr [rsp + 16], 0\n   mov qword ptr [rsp + 24], -1\n"
        "xor eax, eax\n   mov rdx, r14\n   sar rdx, 1\n"
        "cmp r15d, dword ptr [rsp + 36]\n   je 1f\n"
        "movsxd rax, r15d\n   movsxd rcx, dword ptr [rsp + 36]\n   sub rax, rcx\n   imul rax, r13\n"
        "add rax, rdx\n   cqo\n   idiv r14\n   test rdx, rdx\n   jns 1f\n   dec rax\n   add rdx, r14\n"
        "1:  mov [rsp], rax\n   mov [rsp + 8], rdx\n   mov ebx, dword ptr [rsp + 32]\n   add ebx, eax\n" /* ebx=xa */
        "2:  cmp r15d, dword ptr [rsp + 44]\n   jge 9f\n   mov ebp, r15d\n   and ebp, -1024\n   add ebp, 1024\n"
        "test r15d, 1023\n   jnz 3f\n   cmp ebp, dword ptr [rsp + 44]\n   jg 3f\n"
        "cmp qword ptr [rsp + 24], 0\n   jge 5f\n   mov rax, r13\n   shl rax, 10\n   cqo\n   idiv r14\n"
        "test rdx, rdx\n   jns 4f\n   dec rax\n   add rdx, r14\n"
        "4:  mov [rsp + 16], rax\n   mov [rsp + 24], rdx\n"
        "5:  mov rax, [rsp]\n   add rax, [rsp + 16]\n   mov rdx, [rsp + 8]\n   add rdx, [rsp + 24]\n"
        "cmp rdx, r14\n   jl 6f\n   sub rdx, r14\n   inc rax\n"
        "6:  mov [rsp], rax\n   mov [rsp + 8], rdx\n   jmp 8f\n"
        "3:  mov eax, dword ptr [rsp + 44]\n   cmp ebp, eax\n   cmovg ebp, eax\n"
        "cmp ebp, dword ptr [rsp + 40]\n   jne 7f\n   mov rax, r13\n   mov [rsp], rax\n   jmp 8f\n"
        "7:  movsxd rax, ebp\n   movsxd rcx, dword ptr [rsp + 36]\n   sub rax, rcx\n   imul rax, r13\n"
        "mov rdx, r14\n   sar rdx, 1\n   add rax, rdx\n   cqo\n   idiv r14\n"
        "test rdx, rdx\n   jns 71f\n   dec rax\n   add rdx, r14\n"
        "71: mov [rsp], rax\n   mov [rsp + 8], rdx\n"
        "8:  mov ecx, dword ptr [rsp + 32]\n   add ecx, eax\n   mov edx, ebx\n   mov ebx, ecx\n"
        "mov r8d, ebp\n   sub r8d, r15d\n   imul r8d, dword ptr [rsp + 48]\n"
        "mov esi, r15d\n   sar esi, 10\n   sub esi, dword ptr [r12 + 16]\n   mov r15d, ebp\n   mov rdi, r12\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov [rsp + 32], r8\n   mov r9d, ecx\n   mov r8d, edx\n   mov edx, esi\n   mov rcx, r12\n"
        "call " FC_SYM(font_asm_span) "\n   add rsp, 48\n"
#else
        "call " FC_SYM(font_asm_span) "\n"
#endif
        "jmp 2b\n"
        "9:  add rsp, 56\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
        "19: " FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_edge_rows)

        /* font_asm_trace(rdi=band, esi=x0, edx=y0, ecx=x1, r8d=y1) */
        FC_ASM_FUNC(font_asm_trace)
        FC_X64_PRO5
        "test rdi, rdi\n   jz 9f\n   cmp qword ptr [rdi], 0\n   je 9f\n   cmp edx, r8d\n   je 9f\n"
        "mov r9d, 1\n   cmp edx, r8d\n   jle 1f\n   xchg esi, ecx\n   xchg edx, r8d\n   mov r9d, -1\n"
        "1:  mov r10d, edx\n   sar r10d, 10\n   lea r11d, [r8d - 1]\n   mov eax, r11d\n   sar eax, 10\n"
        "cmp r10d, eax\n   jne 2f\n   sub r8d, edx\n   imul r8d, r9d\n"
        "mov edx, esi\n   mov esi, r10d\n   sub esi, dword ptr [rdi + 16]\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov [rsp + 32], r8\n   mov r9d, ecx\n   mov r8d, edx\n   mov edx, esi\n   mov rcx, rdi\n"
        "call " FC_SYM(font_asm_span) "\n   add rsp, 48\n" FC_X64_EPI FC_ASM_RET
#else
        "jmp " FC_SYM(font_asm_span) "\n"
#endif
        "2:  sub eax, r10d\n   cmp eax, 1\n   jne 5f\n"
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n"
        "mov r12, rdi\n   mov r13d, r11d\n   and r13d, -1024\n   mov r14d, ecx\n"
        "mov ebx, r8d\n   sub ebx, r13d\n   imul ebx, r9d\n"
        "movsxd rax, ecx\n   movsxd rdi, esi\n   sub rax, rdi\n"
        "movsxd rdi, r8d\n   movsxd r11, edx\n   sub rdi, r11\n"
        "movsxd rcx, r13d\n   sub rcx, r11\n   imul rax, rcx\n   mov rcx, rdi\n   sar rcx, 1\n"
        "add rax, rcx\n   mov ebp, edx\n   cqo\n   idiv rdi\n   test rdx, rdx\n   jns 3f\n   dec rax\n"
        "3:  add eax, esi\n   mov r8d, r13d\n   sub r8d, ebp\n   imul r8d, r9d\n"
        "mov edx, esi\n   mov ebp, eax\n   mov ecx, eax\n   mov esi, r10d\n   sub esi, dword ptr [r12 + 16]\n   mov rdi, r12\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov [rsp + 32], r8\n   mov r9d, ecx\n   mov r8d, edx\n   mov edx, esi\n   mov rcx, r12\n"
        "call " FC_SYM(font_asm_span) "\n   add rsp, 48\n"
#else
        "call " FC_SYM(font_asm_span) "\n"
#endif
        "mov rdi, r12\n   mov esi, r13d\n   sar esi, 10\n   sub esi, dword ptr [r12 + 16]\n"
        "mov edx, ebp\n   mov ecx, r14d\n   mov r8d, ebx\n"
        "pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#if defined(_WIN32)
        "sub rsp, 48\n   mov [rsp + 32], r8\n   mov r9d, ecx\n   mov r8d, edx\n   mov edx, esi\n   mov rcx, rdi\n"
        "call " FC_SYM(font_asm_span) "\n   add rsp, 48\n" FC_X64_EPI FC_ASM_RET
#else
        "jmp " FC_SYM(font_asm_span) "\n"
#endif
        "5:  sub rsp, 40\n   mov [rsp], esi\n   mov [rsp + 4], edx\n   mov [rsp + 8], ecx\n   mov [rsp + 12], r8d\n"
        "mov [rsp + 16], edx\n   mov [rsp + 20], r8d\n   mov [rsp + 24], r9d\n   mov rsi, rsp\n"
#if defined(_WIN32)
        "mov rcx, rdi\n   mov rdx, rsi\n   sub rsp, 32\n   call " FC_SYM(font_asm_edge_rows) "\n   add rsp, 32\n"
#else
        "call " FC_SYM(font_asm_edge_rows) "\n"
#endif
        "add rsp, 40\n"
        "9:  " FC_X64_EPI FC_ASM_RET
        FC_ASM_END(font_asm_trace)

        ".att_syntax prefix\n"
);

/* ========================================================================== */
/*                                  RISCV64                                   */
/* ========================================================================== */
#elif defined(FONT_CORE_RISCV64)

__asm__(
        FC_ASM_SECTION

        /* font_asm_search(a0=face, a1=table, a2=count, a3=stride, a4=off_bytes, a5=value) -> a0 */
        FC_ASM_FUNC(font_asm_search)
        "beqz a0, 9f\n   slli a2, a2, 32\n   srli a2, a2, 32\n   beqz a2, 9f\n"
        "slli a4, a4, 32\n   srli a4, a4, 32\n   slli a5, a5, 32\n   srli a5, a5, 32\n"
        "ld a6, 0(a0)\n   ld a7, 8(a0)\n   srli t0, a4, 1\n   add a1, a1, t0\n"
        "li t1, 0\n   mv t2, a2\n   andi t0, a4, 1\n   bnez t0, 4f\n   addi t3, a7, -2\n"
        "1:  bgeu t1, t2, 8f\n   sub t4, t2, t1\n   srli t4, t4, 1\n   add t4, t1, t4\n"
        "mul t5, t4, a3\n   add t5, t5, a1\n   li t6, 0\n   beqz a6, 2f\n   li t0, 2\n   bltu a7, t0, 2f\n"
        "bgtu t5, t3, 2f\n   add t5, a6, t5\n   lbu t6, 0(t5)\n   lbu t0, 1(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "2:  bgeu t6, a5, 3f\n   addi t1, t4, 1\n   j 1b\n"
        "3:  mv t2, t4\n   j 1b\n"
        "4:  addi t3, a7, -4\n"
        "5:  bgeu t1, t2, 8f\n   sub t4, t2, t1\n   srli t4, t4, 1\n   add t4, t1, t4\n"
        "mul t5, t4, a3\n   add t5, t5, a1\n   li t6, 0\n   beqz a6, 6f\n   li t0, 4\n   bltu a7, t0, 6f\n"
        "bgtu t5, t3, 6f\n   add t5, a6, t5\n   lbu t6, 0(t5)\n   lbu t0, 1(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "lbu t0, 2(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n   lbu t0, 3(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "6:  bgeu t6, a5, 7f\n   addi t1, t4, 1\n   j 5b\n"
        "7:  mv t2, t4\n   j 5b\n"
        "8:  sext.w a0, t1\n" FC_ASM_RET
        "9:  li a0, 0\n" FC_ASM_RET
        FC_ASM_END(font_asm_search)

        /* font_asm_glyph(a0=face, a1=code) -> a0 */
        FC_ASM_FUNC(font_asm_glyph)
        "beqz a0, 19f\n   ld a2, 0(a0)\n   beqz a2, 19f\n   ld a3, 8(a0)\n   ld a4, 48(a0)\n"
        "lwu a5, 96(a0)\n   lbu a6, 146(a0)\n   lbu a7, 147(a0)\n"
        "slli a1, a1, 32\n   srli a1, a1, 32\n   li t0, 0\n   beqz a7, 1f\n   li t1, 0x100\n   sltu t0, a1, t1\n"
        "1:  addi sp, sp, -48\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n"
        "mv s0, t0\n" /* s0=try_sym */
        "100: li s1, 0\n   li t1, 12\n   bne a6, t1, 7f\n   li t1, 16\n   bltu a3, t1, 18f\n"
        "addi t2, a3, -16\n   bgtu a4, t2, 18f\n   add t3, a2, a4\n"
        "lbu t4, 12(t3)\n   lbu t5, 13(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "lbu t5, 14(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n   lbu t5, 15(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "sub t2, t2, a4\n   li t5, 12\n   divu t2, t2, t5\n   bleu t4, t2, 11f\n   mv t4, t2\n"
        "11: addi s2, a4, 16\n   li t1, 0\n   mv t2, t4\n   addi s3, a3, -4\n   mv s4, t4\n"
        "2:  bgeu t1, t2, 5f\n   sub t3, t2, t1\n   srli t3, t3, 1\n   add t3, t1, t3\n"
        "li t5, 12\n   mul t5, t3, t5\n   add t5, s2, t5\n   addi t5, t5, 4\n   li t6, 0\n"
        "li t0, 4\n   bltu a3, t0, 3f\n   bgtu t5, s3, 3f\n   add t5, a2, t5\n"
        "lbu t6, 0(t5)\n   lbu t0, 1(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "lbu t0, 2(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n   lbu t0, 3(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "3:  bgeu t6, a1, 4f\n   addi t1, t3, 1\n   j 2b\n"
        "4:  mv t2, t3\n   j 2b\n"
        "5:  bgeu t1, s4, 18f\n   li t5, 12\n   mul t5, t1, t5\n   add t5, s2, t5\n   li t6, 0\n"
        "li t0, 4\n   bltu a3, t0, 6f\n   bgtu t5, s3, 6f\n   add t2, a2, t5\n"
        "lbu t6, 0(t2)\n   lbu t0, 1(t2)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "lbu t0, 2(t2)\n   slli t6, t6, 8\n   or t6, t6, t0\n   lbu t0, 3(t2)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "6:  bgtu t6, a1, 18f\n   addi t5, t5, 8\n   li t3, 0\n   li t0, 4\n   bltu a3, t0, 61f\n   bgtu t5, s3, 61f\n"
        "add t2, a2, t5\n   lbu t3, 0(t2)\n   lbu t0, 1(t2)\n   slli t3, t3, 8\n   or t3, t3, t0\n"
        "lbu t0, 2(t2)\n   slli t3, t3, 8\n   or t3, t3, t0\n   lbu t0, 3(t2)\n   slli t3, t3, 8\n   or t3, t3, t0\n"
        "61: add s1, t3, a1\n   sub s1, s1, t6\n   j 18f\n"
        "7:  li t1, 4\n   bne a6, t1, 18f\n   li t1, 0xffff\n   bgtu a1, t1, 18f\n   addi s3, a3, -2\n"
        "addi t5, a4, 6\n   li s4, 0\n   li t0, 2\n   bltu a3, t0, 8f\n   bgtu t5, s3, 8f\n"
        "add t2, a2, t5\n   lbu s4, 0(t2)\n   lbu t0, 1(t2)\n   slli s4, s4, 8\n   or s4, s4, t0\n"
        "8:  srli s4, s4, 1\n   addi s2, a4, 14\n   li t1, 0\n   mv t2, s4\n"
        "9:  bgeu t1, t2, 12f\n   sub t3, t2, t1\n   srli t3, t3, 1\n   add t3, t1, t3\n"
        "slli t5, t3, 1\n   add t5, s2, t5\n   li t6, 0\n   li t0, 2\n   bltu a3, t0, 10f\n   bgtu t5, s3, 10f\n"
        "add t5, a2, t5\n   lbu t6, 0(t5)\n   lbu t0, 1(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "10: bgeu t6, a1, 11f\n   addi t1, t3, 1\n   j 9b\n"
        "11: mv t2, t3\n   j 9b\n"
        "12: bgeu t1, s4, 18f\n   slli t2, s4, 1\n   add t2, s2, t2\n   addi t2, t2, 2\n"
        "slli t3, t1, 1\n   add t5, t2, t3\n   li t4, 0\n   li t0, 2\n   bltu a3, t0, 13f\n   bgtu t5, s3, 13f\n"
        "add t5, a2, t5\n   lbu t4, 0(t5)\n   lbu t0, 1(t5)\n   slli t4, t4, 8\n   or t4, t4, t0\n"
        "13: bltu a1, t4, 18f\n   slli t0, s4, 1\n   add t2, t2, t0\n   add t5, t2, t3\n"
        "li t6, 0\n   li t0, 2\n   bltu a3, t0, 14f\n   bgtu t5, s3, 14f\n"
        "add t5, a2, t5\n   lbu t6, 0(t5)\n   lbu t0, 1(t5)\n   slli t6, t6, 8\n   or t6, t6, t0\n"
        "14: slli t0, s4, 1\n   add t2, t2, t0\n   add t2, t2, t3\n   li t5, 0\n"
        "li t0, 2\n   bltu a3, t0, 15f\n   bgtu t2, s3, 15f\n"
        "add t3, a2, t2\n   lbu t5, 0(t3)\n   lbu t0, 1(t3)\n   slli t5, t5, 8\n   or t5, t5, t0\n"
        "15: beqz t5, 16f\n   sub t4, a1, t4\n   add t2, t2, t5\n   slli t4, t4, 1\n   add t2, t2, t4\n"
        "li s1, 0\n   li t0, 2\n   bltu a3, t0, 17f\n   bgtu t2, s3, 17f\n"
        "add t2, a2, t2\n   lbu s1, 0(t2)\n   lbu t0, 1(t2)\n   slli s1, s1, 8\n   or s1, s1, t0\n   j 17f\n"
        "16: mv s1, a1\n"
        "17: beqz s1, 18f\n   add s1, s1, t6\n   li t0, 0xffff\n   and s1, s1, t0\n"
        "18: li t0, 0\n   bgeu s1, a5, 181f\n   mv t0, s1\n"
        "181: bnez t0, 20f\n   beqz s0, 20f\n   li s0, 0\n   li t1, 0xf000\n   or a1, a1, t1\n   j 100b\n"
        "20: sext.w a0, t0\n   ld s4, 32(sp)\n   ld s3, 24(sp)\n   ld s2, 16(sp)\n   ld s1, 8(sp)\n   ld s0, 0(sp)\n   addi sp, sp, 48\n" FC_ASM_RET
        "19: li a0, 0\n" FC_ASM_RET
        FC_ASM_END(font_asm_glyph)

        /* font_asm_coverage(a0=face, a1=table, a2=glyph) -> a0 */
        FC_ASM_FUNC(font_asm_coverage)
        "beqz a0, 9f\n   ld a3, 0(a0)\n   ld a4, 8(a0)\n   beqz a3, 9f\n   li t0, 2\n   bltu a4, t0, 9f\n"
        "slli a2, a2, 32\n   srli a2, a2, 32\n   addi a5, a4, -2\n"
        "li a6, 0\n   bgtu a1, a5, 1f\n   add t0, a3, a1\n   lbu a6, 0(t0)\n   lbu t1, 1(t0)\n   slli a6, a6, 8\n   or a6, a6, t1\n"
        "1:  addi t2, a1, 2\n   li a7, 0\n   bgtu t2, a5, 2f\n   add t0, a3, t2\n   lbu a7, 0(t0)\n   lbu t1, 1(t0)\n   slli a7, a7, 8\n   or a7, a7, t1\n"
        "2:  addi a1, a1, 4\n   li t1, 0\n   mv t2, a7\n   li t0, 1\n   beq a6, t0, 3f\n   li t0, 2\n   bne a6, t0, 9f\n"
        "6:  bgeu t1, t2, 8f\n   sub t3, t2, t1\n   srli t3, t3, 1\n   add t3, t1, t3\n"
        "slli t4, t3, 1\n   add t4, t4, t3\n   slli t4, t4, 1\n   add t4, a1, t4\n   addi t4, t4, 2\n"
        "li t5, 0\n   bgtu t4, a5, 7f\n   add t4, a3, t4\n   lbu t5, 0(t4)\n   lbu t6, 1(t4)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "7:  bgeu t5, a2, 71f\n   addi t1, t3, 1\n   j 6b\n"
        "71: mv t2, t3\n   j 6b\n"
        "8:  bgeu t1, a7, 9f\n   slli t4, t1, 1\n   add t4, t4, t1\n   slli t4, t4, 1\n   add t4, a1, t4\n"
        "li t5, 0\n   bgtu t4, a5, 81f\n   add t0, a3, t4\n   lbu t5, 0(t0)\n   lbu t6, 1(t0)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "81: bgtu t5, a2, 9f\n   addi t4, t4, 4\n   li t6, 0\n   bgtu t4, a5, 82f\n"
        "add t0, a3, t4\n   lbu t6, 0(t0)\n   lbu t3, 1(t0)\n   slli t6, t6, 8\n   or t6, t6, t3\n"
        "82: add a0, t6, a2\n   sub a0, a0, t5\n   sext.w a0, a0\n" FC_ASM_RET
        "3:  bgeu t1, t2, 5f\n   sub t3, t2, t1\n   srli t3, t3, 1\n   add t3, t1, t3\n"
        "slli t4, t3, 1\n   add t4, a1, t4\n   li t5, 0\n   bgtu t4, a5, 4f\n"
        "add t4, a3, t4\n   lbu t5, 0(t4)\n   lbu t6, 1(t4)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "4:  bgeu t5, a2, 41f\n   addi t1, t3, 1\n   j 3b\n"
        "41: mv t2, t3\n   j 3b\n"
        "5:  bgeu t1, a7, 9f\n   slli t4, t1, 1\n   add t4, a1, t4\n   li t5, 0\n   bgtu t4, a5, 51f\n"
        "add t4, a3, t4\n   lbu t5, 0(t4)\n   lbu t6, 1(t4)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "51: bne t5, a2, 9f\n   sext.w a0, t1\n" FC_ASM_RET
        "9:  li a0, -1\n" FC_ASM_RET
        FC_ASM_END(font_asm_coverage)

        /* font_asm_class(a0=face, a1=table, a2=glyph) -> a0 */
        FC_ASM_FUNC(font_asm_class)
        "beqz a0, 9f\n   ld a3, 0(a0)\n   ld a4, 8(a0)\n   beqz a3, 9f\n   li t0, 2\n   bltu a4, t0, 9f\n"
        "slli a2, a2, 32\n   srli a2, a2, 32\n   addi a5, a4, -2\n"
        "li a6, 0\n   bgtu a1, a5, 1f\n   add t0, a3, a1\n   lbu a6, 0(t0)\n   lbu t1, 1(t0)\n   slli a6, a6, 8\n   or a6, a6, t1\n"
        "1:  li t0, 1\n   bne a6, t0, 3f\n   addi t2, a1, 2\n   li t3, 0\n   bgtu t2, a5, 11f\n"
        "add t0, a3, t2\n   lbu t3, 0(t0)\n   lbu t1, 1(t0)\n   slli t3, t3, 8\n   or t3, t3, t1\n"
        "11: bltu a2, t3, 9f\n   addi t2, a1, 4\n   li t4, 0\n   bgtu t2, a5, 12f\n"
        "add t0, a3, t2\n   lbu t4, 0(t0)\n   lbu t1, 1(t0)\n   slli t4, t4, 8\n   or t4, t4, t1\n"
        "12: sub t5, a2, t3\n   bgeu t5, t4, 9f\n   addi t2, a1, 6\n   slli t5, t5, 1\n   add t2, t2, t5\n"
        "li a0, 0\n   bgtu t2, a5, 2f\n   add t0, a3, t2\n   lbu a0, 0(t0)\n   lbu t1, 1(t0)\n   slli a0, a0, 8\n   or a0, a0, t1\n"
        "2:  sext.w a0, a0\n" FC_ASM_RET
        "3:  li t0, 2\n   bne a6, t0, 9f\n   addi t2, a1, 2\n   li a7, 0\n   bgtu t2, a5, 4f\n"
        "add t0, a3, t2\n   lbu a7, 0(t0)\n   lbu t1, 1(t0)\n   slli a7, a7, 8\n   or a7, a7, t1\n"
        "4:  addi a1, a1, 4\n   li t1, 0\n   mv t2, a7\n"
        "5:  bgeu t1, t2, 7f\n   sub t3, t2, t1\n   srli t3, t3, 1\n   add t3, t1, t3\n"
        "slli t4, t3, 1\n   add t4, t4, t3\n   slli t4, t4, 1\n   add t4, a1, t4\n   addi t4, t4, 2\n"
        "li t5, 0\n   bgtu t4, a5, 6f\n   add t4, a3, t4\n   lbu t5, 0(t4)\n   lbu t6, 1(t4)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "6:  bgeu t5, a2, 61f\n   addi t1, t3, 1\n   j 5b\n"
        "61: mv t2, t3\n   j 5b\n"
        "7:  bgeu t1, a7, 9f\n   slli t4, t1, 1\n   add t4, t4, t1\n   slli t4, t4, 1\n   add t4, a1, t4\n"
        "li t5, 0\n   bgtu t4, a5, 71f\n   add t0, a3, t4\n   lbu t5, 0(t0)\n   lbu t6, 1(t0)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "71: bgtu t5, a2, 9f\n   addi t4, t4, 4\n   li a0, 0\n   bgtu t4, a5, 8f\n"
        "add t0, a3, t4\n   lbu a0, 0(t0)\n   lbu t1, 1(t0)\n   slli a0, a0, 8\n   or a0, a0, t1\n"
        "8:  sext.w a0, a0\n" FC_ASM_RET
        "9:  li a0, 0\n" FC_ASM_RET
        FC_ASM_END(font_asm_class)

        /* font_asm_pair_at(a0=face, a1=table, a2=left, a3=right, a4=first_at, a5=second_at) -> a0 */
        FC_ASM_FUNC(font_asm_pair_at)
        "beqz a0, 19f\n   beqz a4, 19f\n   beqz a5, 19f\n   ld t0, 0(a0)\n   ld t1, 8(a0)\n"
        "beqz t0, 19f\n   li t2, 2\n   bltu t1, t2, 19f\n"
        "addi sp, sp, -80\n   sd ra, 0(sp)\n   sd s0, 8(sp)\n   sd s1, 16(sp)\n   sd s2, 24(sp)\n"
        "sd s3, 32(sp)\n   sd s4, 40(sp)\n   sd s5, 48(sp)\n   sd s6, 56(sp)\n   sd s7, 64(sp)\n   sd s8, 72(sp)\n"
        "mv s0, a0\n   mv s1, a1\n   slli s2, a2, 32\n   srli s2, s2, 32\n   slli s3, a3, 32\n   srli s3, s3, 32\n"
        "mv s4, a4\n   mv s5, a5\n   addi t2, t1, -2\n"
        "li s6, 0\n   bgtu s1, t2, 1f\n   add t3, t0, s1\n   lbu s6, 0(t3)\n   lbu t4, 1(t3)\n   slli s6, s6, 8\n   or s6, s6, t4\n"
        "1:  addi t3, s1, 4\n   li t4, 0\n   bgtu t3, t2, 2f\n   add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "2:  andi t4, t4, 0xff\n   srli t5, t4, 1\n   andi t5, t5, 0x55\n   sub t4, t4, t5\n"
        "andi t5, t4, 0x33\n   srli t4, t4, 2\n   andi t4, t4, 0x33\n   add t4, t4, t5\n"
        "srli t5, t4, 4\n   add t4, t4, t5\n   andi s7, t4, 0x0f\n" /* s7=first_bits */
        "addi t3, s1, 6\n   li t4, 0\n   bgtu t3, t2, 3f\n   add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "3:  andi t4, t4, 0xff\n   srli t5, t4, 1\n   andi t5, t5, 0x55\n   sub t4, t4, t5\n"
        "andi t5, t4, 0x33\n   srli t4, t4, 2\n   andi t4, t4, 0x33\n   add t4, t4, t5\n"
        "srli t5, t4, 4\n   add t4, t4, t5\n   andi t4, t4, 0x0f\n   add t4, t4, s7\n   slli s8, t4, 1\n" /* s8=record */
        "addi t3, s1, 2\n   li t4, 0\n   bgtu t3, t2, 4f\n   add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "4:  mv a0, s0\n   add a1, s1, t4\n   mv a2, s2\n   call " FC_SYM(font_asm_coverage) "\n"
        "bltz a0, 18f\n   ld t0, 0(s0)\n   ld t1, 8(s0)\n   addi t2, t1, -2\n"
        "li t3, 1\n   bne s6, t3, 8f\n   addi t3, s1, 8\n   li t4, 0\n   bgtu t3, t2, 5f\n"
        "add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "5:  bgeu a0, t4, 18f\n   slli t3, a0, 1\n   add t3, s1, t3\n   addi t3, t3, 10\n   li t4, 0\n"
        "bgtu t3, t2, 6f\n   add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "6:  add s2, s1, t4\n   li s6, 0\n   bgtu s2, t2, 7f\n   add t3, t0, s2\n   lbu s6, 0(t3)\n   lbu t4, 1(t3)\n   slli s6, s6, 8\n   or s6, s6, t4\n"
        "7:  mv a0, s0\n   addi a1, s2, 2\n   mv a2, s6\n   addi a3, s8, 2\n   li a4, 0\n   mv a5, s3\n"
        "call " FC_SYM(font_asm_search) "\n   slli a0, a0, 32\n   srli a0, a0, 32\n   bgeu a0, s6, 18f\n"
        "addi t3, s8, 2\n   mul t3, a0, t3\n   add t3, s2, t3\n   addi t3, t3, 2\n"
        "ld t0, 0(s0)\n   ld t1, 8(s0)\n   addi t2, t1, -2\n   li t4, 0\n   bgtu t3, t2, 71f\n"
        "add t5, t0, t3\n   lbu t4, 0(t5)\n   lbu t6, 1(t5)\n   slli t4, t4, 8\n   or t4, t4, t6\n"
        "71: bne t4, s3, 18f\n   addi t3, t3, 2\n   j 15f\n"
        "8:  li t3, 2\n   bne s6, t3, 18f\n   addi t3, s1, 8\n   li t4, 0\n   bgtu t3, t2, 9f\n"
        "add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "9:  mv a0, s0\n   add a1, s1, t4\n   mv a2, s2\n   call " FC_SYM(font_asm_class) "\n"
        "slli s2, a0, 32\n   srli s2, s2, 32\n   ld t0, 0(s0)\n   ld t1, 8(s0)\n   addi t2, t1, -2\n"
        "addi t3, s1, 10\n   li t4, 0\n   bgtu t3, t2, 10f\n   add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "10: mv a0, s0\n   add a1, s1, t4\n   mv a2, s3\n   call " FC_SYM(font_asm_class) "\n"
        "slli s3, a0, 32\n   srli s3, s3, 32\n   ld t0, 0(s0)\n   ld t1, 8(s0)\n   addi t2, t1, -2\n"
        "addi t3, s1, 12\n   li t4, 0\n   bgtu t3, t2, 11f\n   add t3, t0, t3\n   lbu t4, 0(t3)\n   lbu t5, 1(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "11: bgeu s2, t4, 18f\n   addi t3, s1, 14\n   li t5, 0\n   bgtu t3, t2, 12f\n"
        "add t3, t0, t3\n   lbu t5, 0(t3)\n   lbu t6, 1(t3)\n   slli t5, t5, 8\n   or t5, t5, t6\n"
        "12: bgeu s3, t5, 18f\n   mul t3, s2, t5\n   add t3, t3, s3\n   mul t3, t3, s8\n   add t3, s1, t3\n   addi t3, t3, 16\n"
        "15: sd t3, 0(s4)\n   slli t4, s7, 1\n   add t3, t3, t4\n   sd t3, 0(s5)\n   li a0, 1\n   j 17f\n"
        "18: li a0, 0\n"
        "17: ld s8, 72(sp)\n   ld s7, 64(sp)\n   ld s6, 56(sp)\n   ld s5, 48(sp)\n   ld s4, 40(sp)\n"
        "ld s3, 32(sp)\n   ld s2, 24(sp)\n   ld s1, 16(sp)\n   ld s0, 8(sp)\n   ld ra, 0(sp)\n   addi sp, sp, 80\n" FC_ASM_RET
        "19: li a0, 0\n" FC_ASM_RET
        FC_ASM_END(font_asm_pair_at)

        /* font_asm_range_find(a0=table, a1=count, a2=shift, a3=code) -> a0 */
        FC_ASM_FUNC(font_asm_range_find)
        "beqz a0, 4f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   beqz a1, 4f\n"
        "slli a3, a3, 32\n   srli a3, a3, 32\n   li t0, 0\n   mv t1, a1\n   li t6, 1\n"
        "1:  sub t2, t1, t0\n   bleu t2, t6, 3f\n   srli t2, t2, 1\n   add t2, t0, t2\n"
        "slli t3, t2, 2\n   add t3, a0, t3\n   lwu t4, 0(t3)\n   srlw t4, t4, a2\n   slli t4, t4, 32\n   srli t4, t4, 32\n"
        "bgtu t4, a3, 2f\n   mv t0, t2\n   j 1b\n"
        "2:  mv t1, t2\n   j 1b\n"
        "3:  slli t3, t0, 2\n   add t3, a0, t3\n   lwu a0, 0(t3)\n   sllw t4, t6, a2\n   addiw t4, t4, -1\n"
        "and a0, a0, t4\n   sext.w a0, a0\n" FC_ASM_RET
        "4:  li a0, 0\n" FC_ASM_RET
        FC_ASM_END(font_asm_range_find)

        /* font_asm_lower_bound(a0=values, a1=count, a2=stride, a3=wide, a4=code) -> a0 */
        FC_ASM_FUNC(font_asm_lower_bound)
        "beqz a0, 9f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   beqz a1, 9f\n"
        "slli a2, a2, 32\n   srli a2, a2, 32\n   slli a4, a4, 32\n   srli a4, a4, 32\n"
        "li t0, 0\n   mv t1, a1\n   slli a3, a3, 32\n   bnez a3, 4f\n   slli a2, a2, 1\n"
        "1:  bgeu t0, t1, 8f\n   sub t2, t1, t0\n   srli t2, t2, 1\n   add t2, t0, t2\n"
        "mul t3, t2, a2\n   add t3, a0, t3\n   lhu t4, 0(t3)\n   bgeu t4, a4, 2f\n   addi t0, t2, 1\n   j 1b\n"
        "2:  mv t1, t2\n   j 1b\n"
        "4:  slli a2, a2, 2\n"
        "5:  bgeu t0, t1, 8f\n   sub t2, t1, t0\n   srli t2, t2, 1\n   add t2, t0, t2\n"
        "mul t3, t2, a2\n   add t3, a0, t3\n   lwu t4, 0(t3)\n   bgeu t4, a4, 6f\n   addi t0, t2, 1\n   j 5b\n"
        "6:  mv t1, t2\n   j 5b\n"
        "8:  sext.w a0, t0\n" FC_ASM_RET
        "9:  li a0, 0\n" FC_ASM_RET
        FC_ASM_END(font_asm_lower_bound)

        /* font_asm_span(a0=band, a1=row, a2=xa, a3=xb, a4=cover) */
        FC_ASM_FUNC(font_asm_span)
        "beqz a0, 19f\n   ld a5, 0(a0)\n   beqz a5, 19f\n   sext.w a1, a1\n   bltz a1, 19f\n"
        "lw t0, 20(a0)\n   bge a1, t0, 19f\n   lw t1, 28(a0)\n   slliw t1, t1, 10\n"
        "sext.w a2, a2\n   sext.w a3, a3\n   sext.w a4, a4\n"
        "bgez a2, 101f\n   li a2, 0\n"
        "101: ble a2, t1, 102f\n   mv a2, t1\n"
        "102: bgez a3, 103f\n   li a3, 0\n"
        "103: ble a3, t1, 104f\n   mv a3, t1\n"
        "104: lw t0, 24(a0)\n   mul t0, a1, t0\n   slli t0, t0, 2\n   add a5, a5, t0\n"
        "xor t0, a2, a3\n   sraiw t0, t0, 10\n   bnez t0, 4f\n"
        /* span_one */
        "sraiw t0, a2, 10\n   andi t1, a2, 1023\n   andi t2, a3, 1023\n   add t1, t1, t2\n"
        "mulw t1, a4, t1\n   slliw t2, a4, 11\n   subw t2, t2, t1\n   slli t3, t0, 2\n   add t3, a5, t3\n"
        "lw t4, 0(t3)\n   lw t5, 4(t3)\n   addw t4, t4, t2\n   addw t5, t5, t1\n   sw t4, 0(t3)\n   sw t5, 4(t3)\n"
        "ld t3, 8(a0)\n   beqz t3, 19f\n   slli t4, a1, 1\n   add t4, t4, a1\n   slli t4, t4, 4\n   add t3, t3, t4\n"
        "lbu t4, 40(a0)\n   beqz t4, 2f\n   lw t5, 32(a0)\n   addi t6, t0, 1\n   sraw t6, t6, t5\n"
        "sraw t5, t0, t5\n   li t1, 2\n   sll t1, t1, t6\n   li t2, 1\n   sll t2, t2, t5\n"
        "sub t1, t1, t2\n   ld t2, 0(t3)\n   or t2, t2, t1\n   sd t2, 0(t3)\n"
        "2:  lbu t4, 41(a0)\n   beqz t4, 19f\n   lw t5, 36(a0)\n   sraw t5, t0, t5\n"
        "li t1, 1\n   sll t1, t1, t5\n   ld t2, 8(t3)\n   or t2, t2, t1\n   sd t2, 8(t3)\n" FC_ASM_RET
        /* span_many */
        "4:  addi sp, sp, -80\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n"
        "sd s4, 32(sp)\n   sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n   sd s8, 64(sp)\n   sd s9, 72(sp)\n"
        "sraiw s0, a2, 10\n   sraiw s1, a3, 10\n   slt s2, a2, a3\n" /* s0=col, s1=last, s2=right */
        "ld t3, 8(a0)\n   beqz t3, 6f\n   slli t4, a1, 1\n   add t4, t4, a1\n   slli t4, t4, 4\n   add t3, t3, t4\n"
        "mv t0, s0\n   mv t1, s1\n   bnez s2, 41f\n   mv t0, s1\n   mv t1, s0\n"
        "41: lbu t4, 40(a0)\n   beqz t4, 5f\n   lw t5, 32(a0)\n   addi t6, t1, 1\n   sraw t6, t6, t5\n"
        "sraw t5, t0, t5\n   li a6, 2\n   sll a6, a6, t6\n   li a7, 1\n   sll a7, a7, t5\n"
        "sub a6, a6, a7\n   ld a7, 0(t3)\n   or a7, a7, a6\n   sd a7, 0(t3)\n"
        "5:  lbu t4, 41(a0)\n   beqz t4, 6f\n   lw t5, 36(a0)\n   sraw t6, t1, t5\n   sraw t5, t0, t5\n"
        "li a6, 2\n   sll a6, a6, t6\n   li a7, 1\n   sll a7, a7, t5\n"
        "sub a6, a6, a7\n   ld a7, 8(t3)\n   or a7, a7, a6\n   sd a7, 8(t3)\n"
        "6:  sub s3, a3, a2\n   bnez s2, 61f\n   neg s3, s3\n" /* s3=travel */
        "61: mv s4, a4\n   li s5, 1\n   bgez s4, 62f\n   neg s4, s4\n   li s5, -1\n" /* s4=size, s5=sign */
        "62: andi t0, a2, 1023\n   beqz s2, 63f\n   li t1, 1024\n   sub t0, t1, t0\n"
        "63: mul t1, s4, t0\n   srli t2, s3, 1\n   add t1, t1, t2\n   divu s6, t1, s3\n   remu s7, t1, s3\n" /* s6=done, s7=carry */
        "slli t1, s4, 10\n   divu s8, t1, s3\n   remu s9, t1, s3\n" /* s8=lift, s9=spare */
        "li t0, 1024\n   li t1, 0\n   li a6, 1\n   bnez s2, 64f\n   li t0, 0\n   li t1, 1024\n   li a6, -1\n" /* t0=leave, t1=enter, a6=step */
        "64: mulw t2, s6, s5\n   andi t3, a2, 1023\n   add t3, t3, t0\n   mulw t3, t2, t3\n"
        "slliw t2, t2, 11\n   subw t2, t2, t3\n   slli t4, s0, 2\n   add t4, a5, t4\n"
        "lw t5, 0(t4)\n   lw t6, 4(t4)\n   addw t5, t5, t2\n   addw t6, t6, t3\n   sw t5, 0(t4)\n   sw t6, 4(t4)\n"
        "addw s0, s0, a6\n"
        "7:  beq s0, s1, 9f\n   mv t2, s8\n   add s7, s7, s9\n   bltu s7, s3, 8f\n   sub s7, s7, s3\n   addi t2, t2, 1\n"
        "8:  add s6, s6, t2\n   mulw t2, t2, s5\n   slliw t2, t2, 10\n   slli t4, s0, 2\n   add t4, a5, t4\n"
        "lw t5, 0(t4)\n   lw t6, 4(t4)\n   addw t5, t5, t2\n   addw t6, t6, t2\n   sw t5, 0(t4)\n   sw t6, 4(t4)\n"
        "addw s0, s0, a6\n   j 7b\n"
        "9:  mulw t2, s6, s5\n   subw t2, a4, t2\n   andi t3, a3, 1023\n   add t3, t3, t1\n   mulw t3, t2, t3\n"
        "slliw t2, t2, 11\n   subw t2, t2, t3\n   slli t4, s1, 2\n   add t4, a5, t4\n"
        "lw t5, 0(t4)\n   lw t6, 4(t4)\n   addw t5, t5, t2\n   addw t6, t6, t3\n   sw t5, 0(t4)\n   sw t6, 4(t4)\n"
        "ld s9, 72(sp)\n   ld s8, 64(sp)\n   ld s7, 56(sp)\n   ld s6, 48(sp)\n   ld s5, 40(sp)\n"
        "ld s4, 32(sp)\n   ld s3, 24(sp)\n   ld s2, 16(sp)\n   ld s1, 8(sp)\n   ld s0, 0(sp)\n   addi sp, sp, 80\n"
        "19: " FC_ASM_RET
        FC_ASM_END(font_asm_span)

        /* font_asm_edge_rows(a0=band, a1=edge[7]) */
        FC_ASM_FUNC(font_asm_edge_rows)
        "beqz a0, 19f\n   beqz a1, 19f\n   ld t0, 0(a0)\n   beqz t0, 19f\n"
        "lw t1, 12(a1)\n   lw t2, 4(a1)\n   sub t3, t1, t2\n   blez t3, 19f\n"
        "lw t4, 16(a1)\n   lw t5, 20(a1)\n   bge t4, t5, 19f\n"
        "addi sp, sp, -144\n   sd ra, 0(sp)\n   sd s0, 8(sp)\n   sd s1, 16(sp)\n   sd s2, 24(sp)\n"
        "sd s3, 32(sp)\n   sd s4, 40(sp)\n   sd s5, 48(sp)\n   sd s6, 56(sp)\n   sd s7, 64(sp)\n"
        "sd s8, 72(sp)\n   sd s9, 80(sp)\n   sd s10, 88(sp)\n   sd s11, 96(sp)\n"
        /* [sp+104]=at, [sp+112]=carry, [sp+120]=lift, [sp+128]=spare, [sp+136]=yb_tmp */
        "mv s0, a0\n   lw s1, 0(a1)\n   mv s2, t2\n   mv s3, t1\n   mv s4, t4\n   mv s5, t5\n"
        "lw s6, 24(a1)\n   lw t0, 8(a1)\n   sub s7, t0, s1\n   mv s8, t3\n" /* s7=dx, s8=dy */
        "sd zero, 120(sp)\n   li t0, -1\n   sd t0, 128(sp)\n"
        "li t0, 0\n   srai t1, s8, 1\n   beq s4, s2, 1f\n"
        "sub t2, s4, s2\n   mul t2, s7, t2\n   add t2, t2, t1\n   div t0, t2, s8\n   rem t1, t2, s8\n"
        "bgez t1, 1f\n   addi t0, t0, -1\n   add t1, t1, s8\n"
        "1:  sd t0, 104(sp)\n   sd t1, 112(sp)\n   addw s9, s1, t0\n" /* s9=xa */
        "2:  bge s4, s5, 9f\n   andi s10, s4, -1024\n   addi s10, s10, 1024\n" /* s10=yb */
        "andi t0, s4, 1023\n   bnez t0, 3f\n   bgt s10, s5, 3f\n"
        "ld t2, 120(sp)\n   ld t3, 128(sp)\n   bgez t3, 5f\n"
        "slli t4, s7, 10\n   div t2, t4, s8\n   rem t3, t4, s8\n   bgez t3, 4f\n   addi t2, t2, -1\n   add t3, t3, s8\n"
        "4:  sd t2, 120(sp)\n   sd t3, 128(sp)\n"
        "5:  ld t0, 104(sp)\n   ld t1, 112(sp)\n   add t0, t0, t2\n   add t1, t1, t3\n"
        "blt t1, s8, 6f\n   sub t1, t1, s8\n   addi t0, t0, 1\n"
        "6:  sd t0, 104(sp)\n   sd t1, 112(sp)\n   j 8f\n"
        "3:  ble s10, s5, 31f\n   mv s10, s5\n"
        "31: bne s10, s3, 7f\n   mv t0, s7\n   sd t0, 104(sp)\n   j 8f\n"
        "7:  sub t2, s10, s2\n   mul t2, s7, t2\n   srai t3, s8, 1\n   add t2, t2, t3\n"
        "div t0, t2, s8\n   rem t1, t2, s8\n   bgez t1, 71f\n   addi t0, t0, -1\n   add t1, t1, s8\n"
        "71: sd t0, 104(sp)\n   sd t1, 112(sp)\n"
        "8:  addw s11, s1, t0\n   mv a0, s0\n   lw t1, 16(s0)\n   sraiw t2, s4, 10\n   subw a1, t2, t1\n"
        "mv a2, s9\n   mv a3, s11\n   subw a4, s10, s4\n   mulw a4, a4, s6\n"
        "mv s4, s10\n   mv s9, s11\n   call " FC_SYM(font_asm_span) "\n   j 2b\n"
        "9:  ld s11, 96(sp)\n   ld s10, 88(sp)\n   ld s9, 80(sp)\n   ld s8, 72(sp)\n   ld s7, 64(sp)\n"
        "ld s6, 56(sp)\n   ld s5, 48(sp)\n   ld s4, 40(sp)\n   ld s3, 32(sp)\n   ld s2, 24(sp)\n"
        "ld s1, 16(sp)\n   ld s0, 8(sp)\n   ld ra, 0(sp)\n   addi sp, sp, 144\n"
        "19: " FC_ASM_RET
        FC_ASM_END(font_asm_edge_rows)

        /* font_asm_trace(a0=band, a1=x0, a2=y0, a3=x1, a4=y1) */
        FC_ASM_FUNC(font_asm_trace)
        "beqz a0, 9f\n   ld t0, 0(a0)\n   beqz t0, 9f\n   sext.w a1, a1\n   sext.w a2, a2\n"
        "sext.w a3, a3\n   sext.w a4, a4\n   beq a2, a4, 9f\n   li t1, 1\n   ble a2, a4, 1f\n"
        "mv t0, a1\n   mv a1, a3\n   mv a3, t0\n   mv t0, a2\n   mv a2, a4\n   mv a4, t0\n   li t1, -1\n"
        "1:  sraiw t2, a2, 10\n   addiw t3, a4, -1\n   sraiw t4, t3, 10\n   bne t2, t4, 2f\n"
        "lw t5, 16(a0)\n   subw t5, t2, t5\n   subw t6, a4, a2\n   mulw a4, t6, t1\n"
        "mv a2, a1\n   mv a1, t5\n   tail " FC_SYM(font_asm_span) "\n"
        "2:  subw t5, t4, t2\n   li t6, 1\n   bne t5, t6, 5f\n"
        "addi sp, sp, -48\n   sd ra, 0(sp)\n   sd s0, 8(sp)\n   sd s1, 16(sp)\n"
        "sd s2, 24(sp)\n   sd s3, 32(sp)\n   sd s4, 40(sp)\n"
        "mv s0, a0\n   andi s1, t3, -1024\n   mv s2, a3\n   subw t5, a4, s1\n   mulw s3, t5, t1\n"
        "sub t5, a3, a1\n   sub t6, a4, a2\n   sub a5, s1, a2\n   mul a5, t5, a5\n"
        "srai a6, t6, 1\n   add a5, a5, a6\n   div t5, a5, t6\n   rem a6, a5, t6\n"
        "bgez a6, 3f\n   addi t5, t5, -1\n"
        "3:  addw s4, a1, t5\n   mv a0, s0\n   lw t5, 16(s0)\n   subw t5, t2, t5\n"
        "subw t6, s1, a2\n   mulw a4, t6, t1\n   mv a2, a1\n   mv a1, t5\n   mv a3, s4\n"
        "call " FC_SYM(font_asm_span) "\n   mv a0, s0\n   lw t5, 16(s0)\n   sraiw t2, s1, 10\n"
        "subw a1, t2, t5\n   mv a2, s4\n   mv a3, s2\n   mv a4, s3\n"
        "ld s4, 40(sp)\n   ld s3, 32(sp)\n   ld s2, 24(sp)\n   ld s1, 16(sp)\n"
        "ld s0, 8(sp)\n   ld ra, 0(sp)\n   addi sp, sp, 48\n   tail " FC_SYM(font_asm_span) "\n"
        "5:  addi sp, sp, -48\n   sd ra, 0(sp)\n   sw a1, 16(sp)\n   sw a2, 20(sp)\n"
        "sw a3, 24(sp)\n   sw a4, 28(sp)\n   sw a2, 32(sp)\n   sw a4, 36(sp)\n   sw t1, 40(sp)\n"
        "addi a1, sp, 16\n   call " FC_SYM(font_asm_edge_rows) "\n   ld ra, 0(sp)\n   addi sp, sp, 48\n"
        "9:  " FC_ASM_RET
        FC_ASM_END(font_asm_trace)
);

#endif

#endif /* CANVAS_FONT_CORE_ASM_IMPLEMENTATION */
