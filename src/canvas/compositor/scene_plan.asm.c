/*
        Compositor scene occlusion planning, Y-edge sort, X-span subtraction,
        and tint/clip geometry kernels in pure assembly:
          - splan_plan_add       : opaque box decomposition (1 square / 3 round) +
                                   bounded unique sorted Y-edge insertion
          - splan_band_edges     : bounded band Y-edge filter
          - splan_span_subtract  : front-to-back X-span subtraction/splitting into
                                   visible draw segments + remaining ground spans
          - splan_corner_occluded: rounded-corner square occlusion test
          - splan_shape_covers   : rounded tint corner edge/fill coverage (div-free /255)
          - splan_tint_clamp     : branchless tint radius & edge-line clamp
          - splan_tint_decompose : O(1)-band pure-geometry tint decomposition
          - splan_cpu_tint_cut   : owner-window scanline exclusion span

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_SCENE_PLAN_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_SCENE_PLAN_ASM_H
#define MOONWATER_COMPOSITOR_SCENE_PLAN_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  splan_u8;
typedef p16 splan_u16;
typedef p32 splan_u32;
typedef b32 splan_i32;
typedef p64 splan_u64;
typedef b64 splan_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  splan_u8;
typedef __UINT16_TYPE__ splan_u16;
typedef __UINT32_TYPE__ splan_u32;
typedef __INT32_TYPE__  splan_i32;
typedef __UINT64_TYPE__ splan_u64;
typedef __INT64_TYPE__  splan_i64;
#else
#include <stdint.h>
typedef uint8_t  splan_u8;
typedef uint16_t splan_u16;
typedef uint32_t splan_u32;
typedef int32_t  splan_i32;
typedef uint64_t splan_u64;
typedef int64_t  splan_i64;
#endif

#ifndef SPLAN_RADIUS_MOST
#define SPLAN_RADIUS_MOST 64
#endif

/* 40 bytes — byte-for-byte identical to struct compositor_plan */
struct splan_plan {
        splan_i32 (*box)[4];    /* +0  */
        splan_u32  *owner;      /* +8  */
        splan_u32   pieces;     /* +16 */
        splan_i32  *edge;       /* +24 */
        splan_u32   edges;      /* +32 */
};

/* 48 bytes — frame outline & clip specification for splan_plan_add */
struct splan_add {
        splan_i32 fx, fy, x2, y2;       /* +0..+12  frame outline */
        splan_i32 tx1, ty1, tx2, ty2;   /* +16..+28 target clip */
        splan_i32 radius;               /* +32 corner radius (0 = square) */
        splan_u32 at;                   /* +36 frame index (owner) */
        splan_u32 piece_cap;            /* +40 max pieces in plan->box/owner */
        splan_u32 edge_cap;             /* +44 max edges in plan->edge */
};

/* 40 bytes — resumable X-span subtraction request for splan_span_subtract */
struct splan_sub_req {
        splan_i32 (*span)[2];   /* +0  active unclaimed X-spans [low, high] */
        splan_u32   spans;      /* +8  in/out: current span count */
        splan_u32   span_cap;   /* +12 max capacity of span[] */
        splan_i32   bx0;        /* +16 piece left X */
        splan_i32   bx2;        /* +20 piece right X */
        splan_i32 (*vis)[2];    /* +24 out: visible draw segments [x1, x2] */
        splan_u32   vis_cap;    /* +32 max capacity of vis[] */
        splan_u32   cursor;     /* +36 in/out: resume index in span[] */
};

/* 64 bytes — byte-for-byte identical to struct compositor_shape */
struct splan_shape {
        splan_i32 x, y, w, h, r, line;  /* +0..+20 */
        splan_u32 fill, edge;           /* +24, +28 */
        const splan_u8 *outer;          /* +32 */
        const splan_u8 *outer_right;    /* +40 */
        const splan_u8 *inner;          /* +48 */
        const splan_u8 *inner_right;    /* +56 */
};

/* 24 bytes — single emitted tint rectangle */
struct splan_tint_rect {
        splan_i32 x0, y0, x1, y1;       /* +0..+12 */
        splan_u32 argb;                 /* +16 */
        splan_i32 is_edge;              /* +20 */
};

/* 336 bytes — full tint decomposition (<=4 corners, <=9 rects) */
struct splan_tint_plan {
        splan_i32 r;                    /* +0  clamped radius */
        splan_i32 line;                 /* +4  clamped edge line */
        splan_u32 corner_count;         /* +8  0 or 4 */
        splan_u32 rect_count;           /* +12 0..9 */
        splan_i32 corner_xy[4][2];      /* +16..+44 (cx, cy) for corners 0..3 */
        struct splan_tint_rect rect[12];/* +48..+335 */
};

#ifdef offset_of
#define SPLAN_OFFSET(T, M) offset_of(T, M)
#else
#define SPLAN_OFFSET(T, M) ((splan_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct splan_plan)      == 40,  "splan_plan must be 40 bytes");
_Static_assert(SPLAN_OFFSET(struct splan_plan, box)    == 0,  "splan_plan.box");
_Static_assert(SPLAN_OFFSET(struct splan_plan, owner)  == 8,  "splan_plan.owner");
_Static_assert(SPLAN_OFFSET(struct splan_plan, pieces) == 16, "splan_plan.pieces");
_Static_assert(SPLAN_OFFSET(struct splan_plan, edge)   == 24, "splan_plan.edge");
_Static_assert(SPLAN_OFFSET(struct splan_plan, edges)  == 32, "splan_plan.edges");
_Static_assert(sizeof(struct splan_add)       == 48,  "splan_add must be 48 bytes");
_Static_assert(SPLAN_OFFSET(struct splan_add, radius)    == 32, "splan_add.radius");
_Static_assert(SPLAN_OFFSET(struct splan_add, piece_cap) == 40, "splan_add.piece_cap");
_Static_assert(sizeof(struct splan_sub_req)   == 40,  "splan_sub_req must be 40 bytes");
_Static_assert(SPLAN_OFFSET(struct splan_sub_req, vis)    == 24, "splan_sub_req.vis");
_Static_assert(SPLAN_OFFSET(struct splan_sub_req, cursor) == 36, "splan_sub_req.cursor");
_Static_assert(sizeof(struct splan_shape)     == 64,  "splan_shape must be 64 bytes");
_Static_assert(SPLAN_OFFSET(struct splan_shape, outer)       == 32, "splan_shape.outer");
_Static_assert(SPLAN_OFFSET(struct splan_shape, inner_right) == 56, "splan_shape.inner_right");
_Static_assert(sizeof(struct splan_tint_rect) == 24,  "splan_tint_rect must be 24 bytes");
_Static_assert(sizeof(struct splan_tint_plan) == 336, "splan_tint_plan must be 336 bytes");
_Static_assert(SPLAN_OFFSET(struct splan_tint_plan, corner_xy) == 16, "splan_tint_plan.corner_xy");
_Static_assert(SPLAN_OFFSET(struct splan_tint_plan, rect)      == 48, "splan_tint_plan.rect");

splan_u32 splan_plan_add(struct splan_plan *plan, const struct splan_add *req);
splan_u32 splan_band_edges(const splan_i32 *src, splan_u32 src_count, splan_i32 y1, splan_i32 y2,
                           splan_i32 *dst, splan_u32 dst_cap);
splan_u32 splan_span_subtract(struct splan_sub_req *req);
splan_i32 splan_corner_occluded(const struct splan_plan *plan, splan_u32 at,
                                splan_i32 x1, splan_i32 y1, splan_i32 x2, splan_i32 y2);
void      splan_shape_covers(const struct splan_shape *s, splan_i32 px, splan_i32 my,
                             splan_u32 *ce, splan_u32 *cf);
void      splan_tint_clamp(splan_i32 w, splan_i32 h, splan_i32 radius,
                           splan_i32 edge_line, splan_u32 edge, splan_i32 out[2]);
splan_i32 splan_tint_decompose(const splan_i32 rect[4], splan_u32 ink,
                               splan_i32 radius, splan_i32 edge_line, splan_u32 edge,
                               struct splan_tint_plan *out);
void      splan_cpu_tint_cut(const splan_i32 owner_rect[4], splan_i32 ri,
                             splan_i32 py, splan_i32 x2, splan_i32 out[2]);

#endif /* MOONWATER_COMPOSITOR_SCENE_PLAN_ASM_H */

#if defined(COMPOSITOR_SCENE_PLAN_ASM_IMPLEMENTATION) && !defined(COMPOSITOR_SCENE_PLAN_ASM_EMITTED)
#define COMPOSITOR_SCENE_PLAN_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SPLAN_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SPLAN_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SPLAN_RISCV64 1
# else
#  error "src/canvas/compositor/scene_plan.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SPLAN_ARM64 1
# elif defined(X64) && X64
#  define SPLAN_X64 1
# elif defined(RISCV64) && RISCV64
#  define SPLAN_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SP_ASM_SECTION   ASM_SECTION
# define SP_ASM_FUNC(n)   ASM_FUNC(n)
# define SP_ASM_END(n)    ASM_END(n)
# define SP_ASM_RET       ASM_RET
#else
# define SP_ASM_SECTION   ".text\n"
# define SP_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define SP_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SP_ASM_END(n)   ""
# elif defined(_WIN32)
#  define SP_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SP_ASM_END(n)   ""
# else
#  if defined(SPLAN_ARM64)
#   define SP_ASM_FTYPE   "%function"
#  else
#   define SP_ASM_FTYPE   "@function"
#  endif
#  define SP_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SP_ASM_FTYPE "\n" #n ":\n"
#  define SP_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SPLAN_ARM64)

__asm__(
        SP_ASM_SECTION

        /* splan_plan_add(x0=plan, x1=req) -> added pieces (0, 1, or 3) */
        SP_ASM_FUNC(splan_plan_add)
        "cbz x0, 9f\n   cbz x1, 9f\n   ldp x2, x3, [x0]\n   ldr x4, [x0, #24]\n"
        "cbz x2, 9f\n   cbz x3, 9f\n   cbz x4, 9f\n"
        "ldp w5, w6, [x1]\n   ldp w7, w8, [x1, #8]\n   ldp w9, w10, [x1, #16]\n   ldp w11, w12, [x1, #24]\n"
        "cmp w7, w9\n   b.le 9f\n   cmp w5, w11\n   b.ge 9f\n   cmp w8, w10\n   b.le 9f\n   cmp w6, w12\n   b.ge 9f\n"
        "ldp w13, w14, [x1, #32]\n   ldp w15, w16, [x1, #40]\n   ldr w17, [x0, #16]\n"
        "cmp w13, #0\n   mov w9, #1\n   mov w11, #3\n   csel w9, w11, w9, ne\n"
        "cmp w17, w15\n   b.hi 9f\n   sub w15, w15, w17\n   cmp w15, w9\n   b.lo 9f\n"
        "add x2, x2, x17, lsl #4\n   add x3, x3, x17, lsl #2\n   add w17, w17, w9\n   str w17, [x0, #16]\n"
        "cbnz w13, 1f\n   stp w5, w6, [x2]\n   stp w7, w8, [x2, #8]\n   str w14, [x3]\n   b 2f\n"
        "1:  add w11, w5, w13\n   sub w15, w7, w13\n   stp w11, w6, [x2]\n   stp w15, w8, [x2, #8]\n"
        "add w17, w6, w13\n   sub w13, w8, w13\n   stp w5, w17, [x2, #16]\n   stp w11, w13, [x2, #24]\n"
        "stp w15, w17, [x2, #32]\n   stp w7, w13, [x2, #40]\n   stp w14, w14, [x3]\n   str w14, [x3, #8]\n"
        "2:  ldr w14, [x0, #32]\n   mov w5, #0\n   mov w11, #2\n   cmp w9, #1\n   mov w15, #4\n   csel w11, w11, w15, eq\n"
        "3:  cmp w5, w11\n   b.hs 8f\n   cbnz w5, 4f\n   mov w7, w6\n   b 7f\n"
        "4:  cmp w5, #1\n   b.ne 5f\n   mov w7, w8\n   b 7f\n"
        "5:  cmp w5, #2\n   b.ne 6f\n   mov w7, w17\n   b 7f\n"
        "6:  mov w7, w13\n"
        "7:  add w5, w5, #1\n   cmp w7, w10\n   b.le 3b\n   cmp w7, w12\n   b.ge 3b\n"
        "mov w15, w14\n"
        "10: cbz w15, 11f\n   sub w1, w15, #1\n   ldr w2, [x4, x1, lsl #2]\n   cmp w2, w7\n   b.le 11f\n   mov w15, w1\n   b 10b\n"
        "11: cbz w15, 12f\n   sub w1, w15, #1\n   ldr w2, [x4, x1, lsl #2]\n   cmp w2, w7\n   b.eq 3b\n"
        "12: cmp w14, w16\n   b.hs 3b\n   mov w1, w14\n"
        "13: cmp w1, w15\n   b.ls 14f\n   sub w2, w1, #1\n   ldr w3, [x4, x2, lsl #2]\n   str w3, [x4, x1, lsl #2]\n   mov w1, w2\n   b 13b\n"
        "14: str w7, [x4, x15, lsl #2]\n   add w14, w14, #1\n   b 3b\n"
        "8:  str w14, [x0, #32]\n   mov w0, w9\n" SP_ASM_RET
        "9:  mov w0, #0\n" SP_ASM_RET
        SP_ASM_END(splan_plan_add)

        /* splan_band_edges(x0=src, w1=src_count, w2=y1, w3=y2, x4=dst, w5=dst_cap) -> count */
        SP_ASM_FUNC(splan_band_edges)
        "cbz x4, 9f\n   cmp w5, #2\n   b.lo 9f\n   str w2, [x4]\n   mov w6, #1\n   sub w5, w5, #1\n"
        "cbz x0, 3f\n   cbz w1, 3f\n   mov w7, #0\n"
        "1:  cmp w7, w1\n   b.hs 3f\n   ldr w8, [x0, x7, lsl #2]\n   add w7, w7, #1\n"
        "cmp w8, w2\n   b.le 1b\n   cmp w8, w3\n   b.ge 1b\n   cmp w6, w5\n   b.hs 1b\n"
        "str w8, [x4, x6, lsl #2]\n   add w6, w6, #1\n   b 1b\n"
        "3:  str w3, [x4, x6, lsl #2]\n   add w0, w6, #1\n" SP_ASM_RET
        "9:  mov w0, #0\n" SP_ASM_RET
        SP_ASM_END(splan_band_edges)

        /* splan_span_subtract(x0=req) -> visible draw segments emitted */
        SP_ASM_FUNC(splan_span_subtract)
        "cbz x0, 9f\n   ldr x1, [x0]\n   ldr x2, [x0, #24]\n   ldp w3, w4, [x0, #8]\n"
        "ldp w5, w6, [x0, #16]\n   ldp w7, w8, [x0, #32]\n"
        "cbz x1, 9f\n   cbz x2, 9f\n   cbz w7, 9f\n   cmp w3, w4\n   csel w3, w4, w3, hi\n"
        "mov w9, #0\n   cmp w6, w5\n   b.gt 0f\n   str w3, [x0, #8]\n   str w3, [x0, #36]\n   mov w0, #0\n" SP_ASM_RET
        "0:  add x10, x1, x8, lsl #3\n   add x16, x1, x3, lsl #3\n"
        "1:  cmp x10, x16\n   b.hs 8f\n   ldp w11, w12, [x10]\n"
        "cmp w11, w5\n   csel w13, w11, w5, gt\n   cmp w12, w6\n   csel w14, w12, w6, lt\n"
        "cmp w14, w13\n   b.gt 2f\n   add x10, x10, #8\n   b 1b\n"
        "2:  cmp w9, w7\n   b.hs 8f\n   stp w13, w14, [x2], #8\n   add w9, w9, #1\n"
        "cmp w11, w13\n   b.ge 4f\n   cmp w14, w12\n   b.ge 3f\n"
        "str w13, [x10, #4]\n   cmp w3, w4\n   b.hs 7f\n   stp w14, w12, [x16], #8\n   add w3, w3, #1\n   add x10, x10, #8\n   b 1b\n"
        "3:  str w13, [x10, #4]\n   add x10, x10, #8\n   b 1b\n"
        "4:  cmp w14, w12\n   b.ge 5f\n   str w14, [x10]\n   add x10, x10, #8\n   b 1b\n"
        "5:  sub w3, w3, #1\n   ldr x15, [x16, #-8]!\n   str x15, [x10]\n   b 1b\n"
        "7:  add x10, x10, #8\n   b 1b\n"
        "8:  sub x10, x10, x1\n   lsr x10, x10, #3\n   str w3, [x0, #8]\n   str w10, [x0, #36]\n   mov w0, w9\n" SP_ASM_RET
        "9:  mov w0, #0\n" SP_ASM_RET
        SP_ASM_END(splan_span_subtract)

        /* splan_corner_occluded(x0=plan, w1=at, w2=x1, w3=y1, w4=x2, w5=y2) -> 0|1 */
        SP_ASM_FUNC(splan_corner_occluded)
        "cbz x0, 9f\n   ldp x6, x7, [x0]\n   ldr w8, [x0, #16]\n   cbz x6, 9f\n   cbz x7, 9f\n   cbz w8, 9f\n"
        "1:  ldr w9, [x7], #4\n   cmp w9, w1\n   b.ls 2f\n   ldp w10, w11, [x6]\n   cmp w10, w2\n   b.gt 2f\n"
        "cmp w11, w3\n   b.gt 2f\n   ldp w10, w11, [x6, #8]\n   cmp w10, w4\n   b.lt 2f\n   cmp w11, w5\n   b.ge 8f\n"
        "2:  add x6, x6, #16\n   subs w8, w8, #1\n   b.ne 1b\n"
        "9:  mov w0, #0\n" SP_ASM_RET
        "8:  mov w0, #1\n" SP_ASM_RET
        SP_ASM_END(splan_corner_occluded)

        /* splan_shape_covers(x0=s, w1=px, w2=my, x3=ce, x4=cf) */
        SP_ASM_FUNC(splan_shape_covers)
        "cbz x0, 9f\n   cbz x3, 9f\n   cbz x4, 9f\n   ldp w5, w6, [x0, #16]\n"
        "cmp w5, #0\n   b.le 8f\n   cmp w2, #0\n   b.lt 8f\n   cmp w2, w5\n   b.ge 8f\n"
        "ldr w7, [x0]\n   ldr w8, [x0, #8]\n   add w8, w7, w8\n   sub w8, w8, w5\n"
        "cmp w1, w8\n   cset w9, ge\n   csel w7, w8, w7, ge\n   sub w10, w1, w7\n"
        "cmp w10, #0\n   b.lt 8f\n   cmp w10, w5\n   b.ge 8f\n"
        "ldp x11, x12, [x0, #32]\n   cmp w9, #0\n   csel x11, x12, x11, ne\n   cbz x11, 8f\n"
        "madd w12, w2, w5, w10\n   ldrb w11, [x11, x12]\n   mov w12, #0\n"
        "sub w13, w5, w6\n   cmp w13, #0\n   b.le 2f\n   sub w14, w2, w6\n   cmp w14, #0\n   b.lt 2f\n   cmp w14, w13\n   b.ge 2f\n"
        "sub w15, w10, w6\n   cmp w9, #0\n   csel w15, w10, w15, ne\n   cmp w15, #0\n   b.lt 2f\n   cmp w15, w13\n   b.ge 2f\n"
        "ldp x7, x8, [x0, #48]\n   cmp w9, #0\n   csel x7, x8, x7, ne\n   cbz x7, 2f\n"
        "madd w14, w14, w13, w15\n   ldrb w12, [x7, x14]\n"
        "2:  cmp w12, w11\n   csel w12, w11, w12, hi\n   ldp w5, w6, [x0, #24]\n   lsr w5, w5, #24\n   lsr w6, w6, #24\n"
        "sub w7, w11, w12\n   mov w8, #128\n   madd w7, w7, w6, w8\n   add w9, w7, w7, lsr #8\n   lsr w9, w9, #8\n"
        "madd w10, w12, w5, w8\n   add w11, w10, w10, lsr #8\n   lsr w11, w11, #8\n"
        "str w9, [x3]\n   str w11, [x4]\n" SP_ASM_RET
        "8:  str wzr, [x3]\n   str wzr, [x4]\n"
        "9:  " SP_ASM_RET
        SP_ASM_END(splan_shape_covers)

        /* splan_tint_clamp(w0=w, w1=h, w2=radius, w3=edge_line, w4=edge, x5=out) */
        SP_ASM_FUNC(splan_tint_clamp)
        "cbz x5, 9f\n   add w6, w0, w0, lsr #31\n   asr w6, w6, #1\n   add w7, w1, w1, lsr #31\n   asr w7, w7, #1\n"
        "cmp w2, w6\n   csel w2, w6, w2, gt\n   cmp w2, w7\n   csel w2, w7, w2, gt\n"
        "mov w8, #64\n   cmp w2, w8\n   csel w2, w8, w2, gt\n   cmp w2, #0\n   csel w2, wzr, w2, lt\n"
        "tst w4, #0xff000000\n   b.eq 1f\n   cmp w3, #0\n   b.lt 1f\n   cbz w2, 2f\n   cmp w3, w2\n   csel w3, w2, w3, gt\n   b 2f\n"
        "1:  mov w3, #0\n"
        "2:  stp w2, w3, [x5]\n"
        "9:  " SP_ASM_RET
        SP_ASM_END(splan_tint_clamp)

        /* splan_tint_decompose(x0=rect, w1=ink, w2=radius, w3=edge_line, w4=edge, x5=out) -> 0|1 */
        SP_ASM_FUNC(splan_tint_decompose)
        "cbz x0, 9f\n   cbz x5, 9f\n   ldp w6, w7, [x0]\n   ldp w8, w9, [x0, #8]\n"
        "cmp w8, #0\n   b.le 8f\n   cmp w9, #0\n   b.le 8f\n"
        "asr w10, w8, #1\n   asr w11, w9, #1\n   cmp w2, w10\n   csel w2, w10, w2, gt\n   cmp w2, w11\n   csel w2, w11, w2, gt\n"
        "mov w10, #64\n   cmp w2, w10\n   csel w2, w10, w2, gt\n   cmp w2, #0\n   csel w2, wzr, w2, lt\n"
        "lsr w10, w4, #24\n   cbz w10, 1f\n   cmp w3, #0\n   b.lt 1f\n   cbz w2, 2f\n   cmp w3, w2\n   csel w3, w2, w3, gt\n   b 2f\n"
        "1:  mov w3, #0\n"
        "2:  stp w2, w3, [x5]\n   add w10, w6, w8\n   add w11, w7, w9\n   cbz w2, 3f\n"
        "mov w12, #4\n   str w12, [x5, #8]\n   sub w12, w10, w2\n   sub w13, w11, w2\n"
        "stp w6, w7, [x5, #16]\n   stp w12, w7, [x5, #24]\n   stp w6, w13, [x5, #32]\n   stp w12, w13, [x5, #40]\n   b 4f\n"
        "3:  str wzr, [x5, #8]\n"
        "4:  add x0, x5, #48\n   mov w12, #0\n   mov w13, w7\n   add w9, w9, #1\n   asr w9, w9, #1\n"
        "5:  cmp w13, w11\n   b.ge 7f\n   sub w14, w13, w7\n   sub w15, w11, #1\n   sub w15, w15, w13\n"
        "cmp w14, w15\n   csel w14, w14, w15, lt\n   cmp w14, w2\n   cset w15, lt\n   cmp w14, w3\n   cset w16, lt\n"
        "mov w14, w11\n   cbz w2, 51f\n   cmp w2, w9\n   b.hs 51f\n   add w17, w7, w2\n   cmp w13, w17\n   b.lt 50f\n"
        "sub w17, w11, w2\n   cmp w13, w17\n   b.ge 51f\n"
        "50: cmp w17, w14\n   csel w14, w17, w14, lt\n"
        "51: cbz w3, 53f\n   cmp w3, w9\n   b.hs 53f\n   add w17, w7, w3\n   cmp w13, w17\n   b.lt 52f\n"
        "sub w17, w11, w3\n   cmp w13, w17\n   b.ge 53f\n"
        "52: cmp w17, w14\n   csel w14, w17, w14, lt\n"
        "53: cmp w3, w8\n   csel w17, w8, w3, gt\n   cmp w15, #0\n   csel w17, w2, w17, ne\n"
        "sub sp, sp, #16\n   stp w2, w3, [sp]\n   add w2, w6, w17\n   sub w3, w10, w17\n"
        "cmp w3, w2\n   b.le 54f\n   cmp w16, #0\n   csel w17, w4, w1, ne\n"
        "stp w2, w13, [x0]\n   stp w3, w14, [x0, #8]\n   stp w17, w16, [x0, #16]\n   add x0, x0, #24\n   add w12, w12, #1\n"
        "54: ldp w2, w3, [sp], #16\n   cbnz w15, 56f\n   cmp w3, #0\n   b.le 56f\n"
        "cmp w3, w8\n   csel w17, w8, w3, gt\n   add w15, w6, w17\n   sub w16, w10, w17\n   cmp w16, w15\n   csel w16, w15, w16, lt\n"
        "mov w17, #1\n   cmp w15, w6\n   b.le 55f\n"
        "stp w6, w13, [x0]\n   stp w15, w14, [x0, #8]\n   stp w4, w17, [x0, #16]\n   add x0, x0, #24\n   add w12, w12, #1\n"
        "55: cmp w10, w16\n   b.le 56f\n"
        "stp w16, w13, [x0]\n   stp w10, w14, [x0, #8]\n   stp w4, w17, [x0, #16]\n   add x0, x0, #24\n   add w12, w12, #1\n"
        "56: mov w13, w14\n   b 5b\n"
        "7:  str w12, [x5, #12]\n   mov w0, #1\n" SP_ASM_RET
        "8:  stp wzr, wzr, [x5]\n   stp wzr, wzr, [x5, #8]\n   mov w0, #1\n" SP_ASM_RET
        "9:  mov w0, #0\n" SP_ASM_RET
        SP_ASM_END(splan_tint_decompose)

        /* splan_cpu_tint_cut(x0=owner_rect, w1=ri, w2=py, w3=x2, x4=out) */
        SP_ASM_FUNC(splan_cpu_tint_cut)
        "cbz x4, 9f\n   stp w3, w3, [x4]\n   cbz x0, 9f\n   ldp w5, w6, [x0]\n   ldp w7, w8, [x0, #8]\n"
        "add w8, w6, w8\n   cmp w2, w6\n   b.lt 9f\n   cmp w2, w8\n   b.ge 9f\n"
        "add w6, w6, w1\n   sub w8, w8, w1\n   cmp w2, w6\n   b.lt 1f\n   cmp w2, w8\n   b.ge 1f\n"
        "add w7, w5, w7\n   b 2f\n"
        "1:  add w7, w5, w7\n   add w5, w5, w1\n   sub w7, w7, w1\n"
        "2:  cmp w7, w5\n   b.le 9f\n   stp w5, w7, [x4]\n"
        "9:  " SP_ASM_RET
        SP_ASM_END(splan_cpu_tint_cut)
);

#elif defined(SPLAN_X64)

#if defined(_WIN32)
# define SP_X64_A2   "push rsi\n   push rdi\n   mov rdi, rcx\n   mov rsi, rdx\n"
# define SP_X64_R2   "pop rdi\n   pop rsi\n"
# define SP_X64_A1   "push rdi\n   mov rdi, rcx\n"
# define SP_X64_R1   "pop rdi\n"
# define SP_X64_A5   "push rsi\n   push rdi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 56]\n"
# define SP_X64_R5   "pop rdi\n   pop rsi\n"
# define SP_X64_A6   "push rsi\n   push rdi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 56]\n   mov r9, [rsp + 64]\n"
# define SP_X64_R6   "pop rdi\n   pop rsi\n"
# define SP_X64_A6S  "push rbx\n   push rbp\n   push rsi\n   push rdi\n   push r12\n   push r13\n   push r14\n   push r15\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 104]\n   mov r9, [rsp + 112]\n"
# define SP_X64_R6S  "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbp\n   pop rbx\n"
#else
# define SP_X64_A2   ""
# define SP_X64_R2   ""
# define SP_X64_A1   ""
# define SP_X64_R1   ""
# define SP_X64_A5   ""
# define SP_X64_R5   ""
# define SP_X64_A6   ""
# define SP_X64_R6   ""
# define SP_X64_A6S  "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
# define SP_X64_R6S  "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#endif

__asm__(
        SP_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* splan_plan_add(rdi=plan, rsi=req) -> added pieces (0, 1, or 3) */
        SP_ASM_FUNC(splan_plan_add)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        SP_X64_A2
        "sub rsp, 16\n   test rdi, rdi\n   jz 9f\n   test rsi, rsi\n   jz 9f\n"
        "mov r8, [rdi]\n   mov r9, [rdi + 8]\n   mov r10, [rdi + 24]\n"
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test r10, r10\n   jz 9f\n"
        "mov eax, [rsi]\n   mov edx, [rsi + 4]\n   mov ecx, [rsi + 8]\n   mov r11d, [rsi + 12]\n"
        "cmp ecx, [rsi + 16]\n   jle 9f\n   cmp eax, [rsi + 24]\n   jge 9f\n"
        "cmp r11d, [rsi + 20]\n   jle 9f\n   cmp edx, [rsi + 28]\n   jge 9f\n"
        "mov r12d, [rsi + 32]\n   mov r13d, [rsi + 36]\n   mov ebx, [rdi + 16]\n"
        "mov ebp, 1\n   mov r14d, 3\n   test r12d, r12d\n   cmovnz ebp, r14d\n"
        "mov r14d, [rsi + 40]\n   cmp ebx, r14d\n   ja 9f\n   sub r14d, ebx\n   cmp r14d, ebp\n   jb 9f\n"
        "mov [rsp], rdi\n   mov r14d, [rsi + 44]\n   mov [rsp + 8], r14d\n   mov [rsp + 12], ebp\n"
        "mov r14d, ebx\n   shl r14, 4\n   add r8, r14\n   mov r14d, ebx\n   shl r14, 2\n   add r9, r14\n"
        "add ebx, ebp\n   mov [rdi + 16], ebx\n   test r12d, r12d\n   jnz 1f\n"
        "mov [r8], eax\n   mov [r8 + 4], edx\n   mov [r8 + 8], ecx\n   mov [r8 + 12], r11d\n   mov [r9], r13d\n   jmp 2f\n"
        "1:  lea r14d, [rax + r12]\n   mov r15d, ecx\n   sub r15d, r12d\n"
        "mov [r8], r14d\n   mov [r8 + 4], edx\n   mov [r8 + 8], r15d\n   mov [r8 + 12], r11d\n"
        "lea ebx, [rdx + r12]\n   mov [r8 + 16], eax\n   mov [r8 + 20], ebx\n   mov [r8 + 24], r14d\n"
        "mov r14d, r11d\n   sub r14d, r12d\n   mov [r8 + 28], r14d\n"
        "mov [r8 + 32], r15d\n   mov [r8 + 36], ebx\n   mov [r8 + 40], ecx\n   mov [r8 + 44], r14d\n"
        "mov [r9], r13d\n   mov [r9 + 4], r13d\n   mov [r9 + 8], r13d\n"
        "2:  mov r12d, [rdi + 32]\n   mov r8d, [rsi + 20]\n   mov r9d, [rsi + 28]\n"
        "xor ecx, ecx\n   mov r15d, 2\n   cmp ebp, 1\n   mov eax, 4\n   cmovne r15d, eax\n"
        "3:  cmp ecx, r15d\n   jae 8f\n   test ecx, ecx\n   jnz 4f\n   mov eax, edx\n   jmp 7f\n"
        "4:  cmp ecx, 1\n   jne 5f\n   mov eax, r11d\n   jmp 7f\n"
        "5:  cmp ecx, 2\n   jne 6f\n   mov eax, ebx\n   jmp 7f\n"
        "6:  mov eax, r14d\n"
        "7:  inc ecx\n   cmp eax, r8d\n   jle 3b\n   cmp eax, r9d\n   jge 3b\n"
        "mov esi, r12d\n"
        "10: test esi, esi\n   jz 11f\n   lea rdi, [rsi - 1]\n   cmp dword ptr [r10 + rdi*4], eax\n   jle 11f\n   mov esi, edi\n   jmp 10b\n"
        "11: test esi, esi\n   jz 12f\n   lea rdi, [rsi - 1]\n   cmp dword ptr [r10 + rdi*4], eax\n   je 3b\n"
        "12: cmp r12d, [rsp + 8]\n   jae 3b\n   mov edi, r12d\n"
        "13: cmp edi, esi\n   jbe 14f\n   lea rdi, [rdi - 1]\n   mov ebp, [r10 + rdi*4]\n   mov [r10 + rdi*4 + 4], ebp\n   jmp 13b\n"
        "14: mov [r10 + rsi*4], eax\n   inc r12d\n   jmp 3b\n"
        "8:  mov rdi, [rsp]\n   mov [rdi + 32], r12d\n   mov eax, [rsp + 12]\n   jmp 99f\n"
        "9:  xor eax, eax\n"
        "99: add rsp, 16\n" SP_X64_R2 "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" SP_ASM_RET
        SP_ASM_END(splan_plan_add)

        /* splan_band_edges(rdi=src, esi=src_count, edx=y1, ecx=y2, r8=dst, r9d=dst_cap) -> count */
        SP_ASM_FUNC(splan_band_edges)
        SP_X64_A6
        "test r8, r8\n   jz 9f\n   cmp r9d, 2\n   jb 9f\n   mov [r8], edx\n   mov eax, 1\n   dec r9d\n"
        "test rdi, rdi\n   jz 3f\n   test esi, esi\n   jz 3f\n   xor r10d, r10d\n"
        "1:  cmp r10d, esi\n   jae 3f\n   mov r11d, [rdi + r10*4]\n   inc r10d\n"
        "cmp r11d, edx\n   jle 1b\n   cmp r11d, ecx\n   jge 1b\n   cmp eax, r9d\n   jae 1b\n"
        "mov [r8 + rax*4], r11d\n   inc eax\n   jmp 1b\n"
        "3:  mov [r8 + rax*4], ecx\n   inc eax\n" SP_X64_R6 SP_ASM_RET
        "9:  xor eax, eax\n" SP_X64_R6 SP_ASM_RET
        SP_ASM_END(splan_band_edges)

        /* splan_span_subtract(rdi=req) -> visible draw segments emitted */
        SP_ASM_FUNC(splan_span_subtract)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        SP_X64_A1
        "test rdi, rdi\n   jz 9f\n   mov r8, [rdi]\n   mov r9, [rdi + 24]\n"
        "mov r10d, [rdi + 8]\n   mov r11d, [rdi + 12]\n   mov esi, [rdi + 16]\n   mov edx, [rdi + 20]\n"
        "mov ecx, [rdi + 32]\n   mov ebx, [rdi + 36]\n"
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test ecx, ecx\n   jz 9f\n"
        "cmp r10d, r11d\n   cmova r10d, r11d\n   xor ebp, ebp\n   cmp edx, esi\n   jg 1f\n"
        "mov [rdi + 8], r10d\n   mov [rdi + 36], r10d\n   xor eax, eax\n   jmp 99f\n"
        "1:  cmp ebx, r10d\n   jae 8f\n   mov eax, ebx\n   mov r12d, [r8 + rax*8]\n   mov r13d, [r8 + rax*8 + 4]\n"
        "mov r14d, r12d\n   cmp r14d, esi\n   cmovl r14d, esi\n"
        "mov r15d, r13d\n   cmp r15d, edx\n   cmovg r15d, edx\n"
        "cmp r15d, r14d\n   jg 2f\n   inc ebx\n   jmp 1b\n"
        "2:  cmp ebp, ecx\n   jae 8f\n   mov [r9 + rbp*8], r14d\n   mov [r9 + rbp*8 + 4], r15d\n   inc ebp\n"
        "cmp r12d, r14d\n   jge 4f\n   cmp r15d, r13d\n   jge 3f\n"
        "mov [r8 + rax*8 + 4], r14d\n   cmp r10d, r11d\n   jae 7f\n"
        "mov [r8 + r10*8], r15d\n   mov [r8 + r10*8 + 4], r13d\n   inc r10d\n   jmp 7f\n"
        "3:  mov [r8 + rax*8 + 4], r14d\n   jmp 7f\n"
        "4:  cmp r15d, r13d\n   jge 5f\n   mov [r8 + rax*8], r15d\n   jmp 7f\n"
        "5:  dec r10d\n   mov r12, [r8 + r10*8]\n   mov [r8 + rax*8], r12\n   jmp 1b\n"
        "7:  inc ebx\n   jmp 1b\n"
        "8:  mov [rdi + 8], r10d\n   mov [rdi + 36], ebx\n   mov eax, ebp\n   jmp 99f\n"
        "9:  xor eax, eax\n"
        "99: " SP_X64_R1 "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" SP_ASM_RET
        SP_ASM_END(splan_span_subtract)

        /* splan_corner_occluded(rdi=plan, esi=at, edx=x1, ecx=y1, r8d=x2, r9d=y2) -> 0|1 */
        SP_ASM_FUNC(splan_corner_occluded)
        SP_X64_A6
        "test rdi, rdi\n   jz 9f\n   mov r10, [rdi]\n   mov r11, [rdi + 8]\n   mov eax, [rdi + 16]\n"
        "test r10, r10\n   jz 9f\n   test r11, r11\n   jz 9f\n   test eax, eax\n   jz 9f\n"
        "1:  cmp dword ptr [r11], esi\n   jbe 2f\n   cmp dword ptr [r10], edx\n   jg 2f\n"
        "cmp dword ptr [r10 + 4], ecx\n   jg 2f\n   cmp dword ptr [r10 + 8], r8d\n   jl 2f\n"
        "cmp dword ptr [r10 + 12], r9d\n   jge 8f\n"
        "2:  add r11, 4\n   add r10, 16\n   dec eax\n   jnz 1b\n"
        "9:  xor eax, eax\n" SP_X64_R6 SP_ASM_RET
        "8:  mov eax, 1\n" SP_X64_R6 SP_ASM_RET
        SP_ASM_END(splan_corner_occluded)

        /* splan_shape_covers(rdi=s, esi=px, edx=my, rcx=ce, r8=cf) */
        SP_ASM_FUNC(splan_shape_covers)
        SP_X64_A5
        "push rbx\n   push rbp\n   push r12\n"
        "test rdi, rdi\n   jz 9f\n   test rcx, rcx\n   jz 9f\n   test r8, r8\n   jz 9f\n"
        "mov r9d, [rdi + 16]\n   mov r10d, [rdi + 20]\n"
        "test r9d, r9d\n   jle 8f\n   test edx, edx\n   js 8f\n   cmp edx, r9d\n   jge 8f\n"
        "mov eax, [rdi]\n   mov r11d, [rdi + 8]\n   add r11d, eax\n   sub r11d, r9d\n"
        "xor ebx, ebx\n   cmp esi, r11d\n   setge bl\n   cmovge eax, r11d\n   sub esi, eax\n"
        "test esi, esi\n   js 8f\n   cmp esi, r9d\n   jge 8f\n"
        "mov r11, [rdi + 32]\n   test ebx, ebx\n   cmovnz r11, [rdi + 40]\n   test r11, r11\n   jz 8f\n"
        "mov eax, edx\n   imul eax, r9d\n   add eax, esi\n   movzx r11d, byte ptr [r11 + rax]\n   xor r12d, r12d\n"
        "sub r9d, r10d\n   test r9d, r9d\n   jle 2f\n   sub edx, r10d\n   test edx, edx\n   js 2f\n   cmp edx, r9d\n   jge 2f\n"
        "mov eax, esi\n   sub eax, r10d\n   test ebx, ebx\n   cmovnz eax, esi\n   test eax, eax\n   js 2f\n   cmp eax, r9d\n   jge 2f\n"
        "mov rbp, [rdi + 48]\n   test ebx, ebx\n   cmovnz rbp, [rdi + 56]\n   test rbp, rbp\n   jz 2f\n"
        "imul edx, r9d\n   add edx, eax\n   movzx r12d, byte ptr [rbp + rdx]\n"
        "2:  cmp r12d, r11d\n   cmova r12d, r11d\n   mov eax, [rdi + 28]\n   shr eax, 24\n"
        "sub r11d, r12d\n   imul r11d, eax\n   add r11d, 127\n   mov eax, r11d\n   shr eax, 8\n   lea eax, [r11 + rax + 1]\n   shr eax, 8\n"
        "mov [rcx], eax\n   mov eax, [rdi + 24]\n   shr eax, 24\n   imul r12d, eax\n   add r12d, 127\n"
        "mov eax, r12d\n   shr eax, 8\n   lea eax, [r12 + rax + 1]\n   shr eax, 8\n   mov [r8], eax\n   jmp 9f\n"
        "8:  mov dword ptr [rcx], 0\n   mov dword ptr [r8], 0\n"
        "9:  pop r12\n   pop rbp\n   pop rbx\n" SP_X64_R5 SP_ASM_RET
        SP_ASM_END(splan_shape_covers)

        /* splan_tint_clamp(edi=w, esi=h, edx=radius, ecx=edge_line, r8d=edge, r9=out) */
        SP_ASM_FUNC(splan_tint_clamp)
        SP_X64_A6
        "test r9, r9\n   jz 9f\n   mov eax, edi\n   shr eax, 31\n   add eax, edi\n   sar eax, 1\n"
        "mov r10d, esi\n   shr r10d, 31\n   add r10d, esi\n   sar r10d, 1\n"
        "cmp edx, eax\n   cmovg edx, eax\n   cmp edx, r10d\n   cmovg edx, r10d\n"
        "mov eax, 64\n   cmp edx, eax\n   cmovg edx, eax\n   xor eax, eax\n   test edx, edx\n   cmovs edx, eax\n"
        "shr r8d, 24\n   jz 1f\n   test ecx, ecx\n   js 1f\n   test edx, edx\n   jz 2f\n   cmp ecx, edx\n   cmovg ecx, edx\n   jmp 2f\n"
        "1:  xor ecx, ecx\n"
        "2:  mov [r9], edx\n   mov [r9 + 4], ecx\n"
        "9:  " SP_X64_R6 SP_ASM_RET
        SP_ASM_END(splan_tint_clamp)

        /* splan_tint_decompose(rdi=rect, esi=ink, edx=radius, ecx=edge_line, r8d=edge, r9=out) -> 0|1 */
        SP_ASM_FUNC(splan_tint_decompose)
        SP_X64_A6S
        "sub rsp, 32\n   test rdi, rdi\n   jz 9f\n   test r9, r9\n   jz 9f\n"
        "mov r10d, [rdi]\n   mov r11d, [rdi + 4]\n   mov r12d, [rdi + 8]\n   mov r13d, [rdi + 12]\n"
        "test r12d, r12d\n   jle 8f\n   test r13d, r13d\n   jle 8f\n"
        "mov eax, r12d\n   sar eax, 1\n   cmp edx, eax\n   cmovg edx, eax\n"
        "mov eax, r13d\n   sar eax, 1\n   cmp edx, eax\n   cmovg edx, eax\n"
        "mov eax, 64\n   cmp edx, eax\n   cmovg edx, eax\n   xor eax, eax\n   test edx, edx\n   cmovs edx, eax\n"
        "mov eax, r8d\n   shr eax, 24\n   jz 1f\n   test ecx, ecx\n   js 1f\n   test edx, edx\n   jz 2f\n   cmp ecx, edx\n   cmovg ecx, edx\n   jmp 2f\n"
        "1:  xor ecx, ecx\n"
        "2:  mov [r9], edx\n   mov [r9 + 4], ecx\n   mov [rsp], esi\n   mov [rsp + 4], edx\n   mov [rsp + 8], ecx\n"
        "mov [rsp + 12], r8d\n   mov [rsp + 16], r12d\n   lea eax, [r13 + 1]\n   sar eax, 1\n   mov [rsp + 20], eax\n"
        "mov [rsp + 24], r9\n   lea r14d, [r10 + r12]\n   lea r15d, [r11 + r13]\n   test edx, edx\n   jz 3f\n"
        "mov dword ptr [r9 + 8], 4\n   mov eax, r14d\n   sub eax, edx\n   mov ebx, r15d\n   sub ebx, edx\n"
        "mov [r9 + 16], r10d\n   mov [r9 + 20], r11d\n   mov [r9 + 24], eax\n   mov [r9 + 28], r11d\n"
        "mov [r9 + 32], r10d\n   mov [r9 + 36], ebx\n   mov [r9 + 40], eax\n   mov [r9 + 44], ebx\n   jmp 4f\n"
        "3:  mov dword ptr [r9 + 8], 0\n"
        "4:  lea rdi, [r9 + 48]\n   xor ebp, ebp\n   mov ebx, r11d\n"
        "5:  cmp ebx, r15d\n   jge 7f\n   mov eax, ebx\n   sub eax, r11d\n   mov esi, r15d\n   dec esi\n   sub esi, ebx\n"
        "cmp esi, eax\n   cmovl eax, esi\n   xor r12d, r12d\n   cmp eax, [rsp + 4]\n   setl r12b\n"
        "xor r13d, r13d\n   cmp eax, [rsp + 8]\n   setl r13b\n   mov esi, r15d\n"
        "mov eax, [rsp + 4]\n   test eax, eax\n   jz 51f\n   cmp eax, [rsp + 20]\n   jae 51f\n"
        "lea edx, [r11 + rax]\n   cmp ebx, edx\n   jl 50f\n   mov edx, r15d\n   sub edx, eax\n   cmp ebx, edx\n   jge 51f\n"
        "50: cmp edx, esi\n   cmovl esi, edx\n"
        "51: mov eax, [rsp + 8]\n   test eax, eax\n   jz 53f\n   cmp eax, [rsp + 20]\n   jae 53f\n"
        "lea edx, [r11 + rax]\n   cmp ebx, edx\n   jl 52f\n   mov edx, r15d\n   sub edx, eax\n   cmp ebx, edx\n   jge 53f\n"
        "52: cmp edx, esi\n   cmovl esi, edx\n"
        "53: mov eax, [rsp + 8]\n   cmp eax, [rsp + 16]\n   cmovg eax, [rsp + 16]\n"
        "test r12d, r12d\n   cmovnz eax, [rsp + 4]\n   lea edx, [r10 + rax]\n   mov ecx, r14d\n   sub ecx, eax\n"
        "cmp ecx, edx\n   jle 54f\n   mov eax, [rsp]\n   test r13d, r13d\n   cmovnz eax, [rsp + 12]\n"
        "mov [rdi], edx\n   mov [rdi + 4], ebx\n   mov [rdi + 8], ecx\n   mov [rdi + 12], esi\n"
        "mov [rdi + 16], eax\n   mov [rdi + 20], r13d\n   add rdi, 24\n   inc ebp\n"
        "54: test r12d, r12d\n   jnz 56f\n   mov eax, [rsp + 8]\n   test eax, eax\n   jle 56f\n"
        "cmp eax, [rsp + 16]\n   cmovg eax, [rsp + 16]\n   lea edx, [r10 + rax]\n   mov ecx, r14d\n   sub ecx, eax\n"
        "cmp ecx, edx\n   cmovl ecx, edx\n   mov r8d, [rsp + 12]\n   cmp edx, r10d\n   jle 55f\n"
        "mov [rdi], r10d\n   mov [rdi + 4], ebx\n   mov [rdi + 8], edx\n   mov [rdi + 12], esi\n"
        "mov [rdi + 16], r8d\n   mov dword ptr [rdi + 20], 1\n   add rdi, 24\n   inc ebp\n"
        "55: cmp r14d, ecx\n   jle 56f\n"
        "mov [rdi], ecx\n   mov [rdi + 4], ebx\n   mov [rdi + 8], r14d\n   mov [rdi + 12], esi\n"
        "mov [rdi + 16], r8d\n   mov dword ptr [rdi + 20], 1\n   add rdi, 24\n   inc ebp\n"
        "56: mov ebx, esi\n   jmp 5b\n"
        "7:  mov r9, [rsp + 24]\n   mov [r9 + 12], ebp\n   mov eax, 1\n   jmp 99f\n"
        "8:  mov dword ptr [r9], 0\n   mov dword ptr [r9 + 4], 0\n   mov dword ptr [r9 + 8], 0\n   mov dword ptr [r9 + 12], 0\n"
        "mov eax, 1\n   jmp 99f\n"
        "9:  xor eax, eax\n"
        "99: add rsp, 32\n" SP_X64_R6S SP_ASM_RET
        SP_ASM_END(splan_tint_decompose)

        /* splan_cpu_tint_cut(rdi=owner_rect, esi=ri, edx=py, ecx=x2, r8=out) */
        SP_ASM_FUNC(splan_cpu_tint_cut)
        SP_X64_A5
        "test r8, r8\n   jz 9f\n   mov [r8], ecx\n   mov [r8 + 4], ecx\n   test rdi, rdi\n   jz 9f\n"
        "mov r9d, [rdi]\n   mov r10d, [rdi + 4]\n   mov eax, [rdi + 8]\n   mov r11d, [rdi + 12]\n"
        "add r11d, r10d\n   cmp edx, r10d\n   jl 9f\n   cmp edx, r11d\n   jge 9f\n"
        "add r10d, esi\n   sub r11d, esi\n   add eax, r9d\n   cmp edx, r10d\n   jl 1f\n   cmp edx, r11d\n   jge 1f\n   jmp 2f\n"
        "1:  add r9d, esi\n   sub eax, esi\n"
        "2:  cmp eax, r9d\n   jle 9f\n   mov [r8], r9d\n   mov [r8 + 4], eax\n"
        "9:  " SP_X64_R5 SP_ASM_RET
        SP_ASM_END(splan_cpu_tint_cut)

        ".att_syntax prefix\n"
);

#elif defined(SPLAN_RISCV64)

__asm__(
        SP_ASM_SECTION

        /* splan_plan_add(a0=plan, a1=req) -> added pieces (0, 1, or 3) */
        SP_ASM_FUNC(splan_plan_add)
        "beqz a0, 9f\n   beqz a1, 9f\n   ld t0, 0(a0)\n   ld t1, 8(a0)\n   ld t2, 24(a0)\n"
        "beqz t0, 9f\n   beqz t1, 9f\n   beqz t2, 9f\n"
        "lw t3, 0(a1)\n   lw t4, 4(a1)\n   lw t5, 8(a1)\n   lw t6, 12(a1)\n"
        "lw a2, 16(a1)\n   lw a3, 20(a1)\n   lw a4, 24(a1)\n   lw a5, 28(a1)\n"
        "ble t5, a2, 9f\n   bge t3, a4, 9f\n   ble t6, a3, 9f\n   bge t4, a5, 9f\n"
        "lw a6, 32(a1)\n   lwu a7, 40(a1)\n   lwu a2, 16(a0)\n"
        "li a4, 1\n   beqz a6, 0f\n   li a4, 3\n"
        "0:  bgtu a2, a7, 9f\n   sub a7, a7, a2\n   bltu a7, a4, 9f\n"
        "addi sp, sp, -48\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n"
        "slli s0, a2, 4\n   add t0, t0, s0\n   slli s0, a2, 2\n   add t1, t1, s0\n"
        "addw a2, a2, a4\n   sw a2, 16(a0)\n   lwu s0, 36(a1)\n   bnez a6, 1f\n"
        "sw t3, 0(t0)\n   sw t4, 4(t0)\n   sw t5, 8(t0)\n   sw t6, 12(t0)\n   sw s0, 0(t1)\n   j 2f\n"
        "1:  addw s1, t3, a6\n   subw s2, t5, a6\n   sw s1, 0(t0)\n   sw t4, 4(t0)\n   sw s2, 8(t0)\n   sw t6, 12(t0)\n"
        "addw s3, t4, a6\n   subw s4, t6, a6\n   sw t3, 16(t0)\n   sw s3, 20(t0)\n   sw s1, 24(t0)\n   sw s4, 28(t0)\n"
        "sw s2, 32(t0)\n   sw s3, 36(t0)\n   sw t5, 40(t0)\n   sw s4, 44(t0)\n"
        "sw s0, 0(t1)\n   sw s0, 4(t1)\n   sw s0, 8(t1)\n"
        "2:  lwu s0, 32(a0)\n   lwu s1, 44(a1)\n   li s2, 0\n   li a7, 2\n   beqz a6, 3f\n   li a7, 4\n"
        "3:  bgeu s2, a7, 8f\n   bnez s2, 4f\n   mv t0, t4\n   j 7f\n"
        "4:  li t1, 1\n   bne s2, t1, 5f\n   mv t0, t6\n   j 7f\n"
        "5:  li t1, 2\n   bne s2, t1, 6f\n   mv t0, s3\n   j 7f\n"
        "6:  mv t0, s4\n"
        "7:  addi s2, s2, 1\n   ble t0, a3, 3b\n   bge t0, a5, 3b\n   mv t1, s0\n"
        "10: beqz t1, 11f\n   addi t3, t1, -1\n   slli t5, t3, 2\n   add t5, t2, t5\n   lw a2, 0(t5)\n   ble a2, t0, 11f\n   mv t1, t3\n   j 10b\n"
        "11: beqz t1, 12f\n   addi t3, t1, -1\n   slli t5, t3, 2\n   add t5, t2, t5\n   lw a2, 0(t5)\n   beq a2, t0, 3b\n"
        "12: bgeu s0, s1, 3b\n   mv t3, s0\n"
        "13: bleu t3, t1, 14f\n   addi t5, t3, -1\n   slli a2, t5, 2\n   add a2, t2, a2\n   lw a6, 0(a2)\n   sw a6, 4(a2)\n   mv t3, t5\n   j 13b\n"
        "14: slli a2, t1, 2\n   add a2, t2, a2\n   sw t0, 0(a2)\n   addi s0, s0, 1\n   j 3b\n"
        "8:  sw s0, 32(a0)\n   mv a0, a4\n"
        "ld s4, 32(sp)\n   ld s3, 24(sp)\n   ld s2, 16(sp)\n   ld s1, 8(sp)\n   ld s0, 0(sp)\n   addi sp, sp, 48\n" SP_ASM_RET
        "9:  li a0, 0\n" SP_ASM_RET
        SP_ASM_END(splan_plan_add)

        /* splan_band_edges(a0=src, a1=src_count, a2=y1, a3=y2, a4=dst, a5=dst_cap) -> count */
        SP_ASM_FUNC(splan_band_edges)
        "beqz a4, 9f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   slli a5, a5, 32\n   srli a5, a5, 32\n"
        "sext.w a2, a2\n   sext.w a3, a3\n   li t0, 2\n   bltu a5, t0, 9f\n   sw a2, 0(a4)\n   li t1, 1\n   addi a5, a5, -1\n"
        "beqz a0, 3f\n   beqz a1, 3f\n   li t2, 0\n"
        "1:  bgeu t2, a1, 3f\n   slli t3, t2, 2\n   add t3, a0, t3\n   lw t4, 0(t3)\n   addi t2, t2, 1\n"
        "ble t4, a2, 1b\n   bge t4, a3, 1b\n   bgeu t1, a5, 1b\n"
        "slli t3, t1, 2\n   add t3, a4, t3\n   sw t4, 0(t3)\n   addi t1, t1, 1\n   j 1b\n"
        "3:  slli t3, t1, 2\n   add t3, a4, t3\n   sw a3, 0(t3)\n   addi a0, t1, 1\n" SP_ASM_RET
        "9:  li a0, 0\n" SP_ASM_RET
        SP_ASM_END(splan_band_edges)

        /* splan_span_subtract(a0=req) -> visible draw segments emitted */
        SP_ASM_FUNC(splan_span_subtract)
        "beqz a0, 9f\n   ld t0, 0(a0)\n   ld t1, 24(a0)\n   lwu t2, 8(a0)\n   lwu t3, 12(a0)\n"
        "lw t4, 16(a0)\n   lw t5, 20(a0)\n   lwu t6, 32(a0)\n   lwu a1, 36(a0)\n"
        "beqz t0, 9f\n   beqz t1, 9f\n   beqz t6, 9f\n   bleu t2, t3, 0f\n   mv t2, t3\n"
        "0:  li a2, 0\n   bgt t5, t4, 1f\n   sw t2, 8(a0)\n   sw t2, 36(a0)\n   li a0, 0\n" SP_ASM_RET
        "1:  bgeu a1, t2, 8f\n   slli a3, a1, 3\n   add a3, t0, a3\n   lw a4, 0(a3)\n   lw a5, 4(a3)\n"
        "mv a6, a4\n   bge a6, t4, 10f\n   mv a6, t4\n"
        "10: mv a7, a5\n   ble a7, t5, 11f\n   mv a7, t5\n"
        "11: bgt a7, a6, 2f\n   addi a1, a1, 1\n   j 1b\n"
        "2:  bgeu a2, t6, 8f\n   slli a4, a2, 3\n   add a4, t1, a4\n   sw a6, 0(a4)\n   sw a7, 4(a4)\n   addi a2, a2, 1\n"
        "lw a4, 0(a3)\n   bge a4, a6, 4f\n   bge a7, a5, 3f\n"
        "sw a6, 4(a3)\n   bgeu t2, t3, 7f\n   slli a4, t2, 3\n   add a4, t0, a4\n   sw a7, 0(a4)\n   sw a5, 4(a4)\n   addi t2, t2, 1\n   j 7f\n"
        "3:  sw a6, 4(a3)\n   j 7f\n"
        "4:  bge a7, a5, 5f\n   sw a7, 0(a3)\n   j 7f\n"
        "5:  addi t2, t2, -1\n   slli a4, t2, 3\n   add a4, t0, a4\n   ld a5, 0(a4)\n   sd a5, 0(a3)\n   j 1b\n"
        "7:  addi a1, a1, 1\n   j 1b\n"
        "8:  sw t2, 8(a0)\n   sw a1, 36(a0)\n   mv a0, a2\n" SP_ASM_RET
        "9:  li a0, 0\n" SP_ASM_RET
        SP_ASM_END(splan_span_subtract)

        /* splan_corner_occluded(a0=plan, a1=at, a2=x1, a3=y1, a4=x2, a5=y2) -> 0|1 */
        SP_ASM_FUNC(splan_corner_occluded)
        "beqz a0, 9f\n   ld t0, 0(a0)\n   ld t1, 8(a0)\n   lwu t2, 16(a0)\n"
        "beqz t0, 9f\n   beqz t1, 9f\n   beqz t2, 9f\n   slli a1, a1, 32\n   srli a1, a1, 32\n"
        "sext.w a2, a2\n   sext.w a3, a3\n   sext.w a4, a4\n   sext.w a5, a5\n"
        "1:  lwu t3, 0(t1)\n   bleu t3, a1, 2f\n   lw t4, 0(t0)\n   bgt t4, a2, 2f\n"
        "lw t4, 4(t0)\n   bgt t4, a3, 2f\n   lw t4, 8(t0)\n   blt t4, a4, 2f\n   lw t4, 12(t0)\n   bge t4, a5, 8f\n"
        "2:  addi t1, t1, 4\n   addi t0, t0, 16\n   addi t2, t2, -1\n   bnez t2, 1b\n"
        "9:  li a0, 0\n" SP_ASM_RET
        "8:  li a0, 1\n" SP_ASM_RET
        SP_ASM_END(splan_corner_occluded)

        /* splan_shape_covers(a0=s, a1=px, a2=my, a3=ce, a4=cf) */
        SP_ASM_FUNC(splan_shape_covers)
        "beqz a0, 9f\n   beqz a3, 9f\n   beqz a4, 9f\n   sext.w a1, a1\n   sext.w a2, a2\n"
        "lw t0, 16(a0)\n   lw t1, 20(a0)\n   blez t0, 8f\n   bltz a2, 8f\n   bge a2, t0, 8f\n"
        "lw t2, 0(a0)\n   lw t3, 8(a0)\n   addw t3, t2, t3\n   subw t3, t3, t0\n"
        "slt t4, a1, t3\n   xori t4, t4, 1\n   beqz t4, 1f\n   subw t5, a1, t3\n   ld t6, 40(a0)\n   j 2f\n"
        "1:  subw t5, a1, t2\n   ld t6, 32(a0)\n"
        "2:  bltz t5, 8f\n   bge t5, t0, 8f\n   beqz t6, 8f\n   mulw a5, a2, t0\n   addw a5, a5, t5\n   add t6, t6, a5\n   lbu a5, 0(t6)\n"
        "li a6, 0\n   subw t2, t0, t1\n   blez t2, 4f\n   subw t3, a2, t1\n   bltz t3, 4f\n   bge t3, t2, 4f\n"
        "mv a7, t5\n   bnez t4, 3f\n   subw a7, t5, t1\n"
        "3:  bltz a7, 4f\n   bge a7, t2, 4f\n   ld t6, 48(a0)\n   beqz t4, 31f\n   ld t6, 56(a0)\n"
        "31: beqz t6, 4f\n   mulw t3, t3, t2\n   addw t3, t3, a7\n   add t6, t6, t3\n   lbu a6, 0(t6)\n"
        "4:  bleu a6, a5, 5f\n   mv a6, a5\n"
        "5:  lwu t0, 28(a0)\n   srliw t0, t0, 24\n   subw t1, a5, a6\n   mulw t1, t1, t0\n   addiw t1, t1, 128\n"
        "srliw t2, t1, 8\n   addw t1, t1, t2\n   srliw t1, t1, 8\n   sw t1, 0(a3)\n"
        "lwu t0, 24(a0)\n   srliw t0, t0, 24\n   mulw t1, a6, t0\n   addiw t1, t1, 128\n"
        "srliw t2, t1, 8\n   addw t1, t1, t2\n   srliw t1, t1, 8\n   sw t1, 0(a4)\n" SP_ASM_RET
        "8:  sw zero, 0(a3)\n   sw zero, 0(a4)\n"
        "9:  " SP_ASM_RET
        SP_ASM_END(splan_shape_covers)

        /* splan_tint_clamp(a0=w, a1=h, a2=radius, a3=edge_line, a4=edge, a5=out) */
        SP_ASM_FUNC(splan_tint_clamp)
        "beqz a5, 9f\n   sext.w a0, a0\n   sext.w a1, a1\n   sext.w a2, a2\n   sext.w a3, a3\n"
        "srliw t0, a0, 31\n   addw t0, a0, t0\n   sraiw t0, t0, 1\n"
        "srliw t1, a1, 31\n   addw t1, a1, t1\n   sraiw t1, t1, 1\n"
        "ble a2, t0, 1f\n   mv a2, t0\n"
        "1:  ble a2, t1, 2f\n   mv a2, t1\n"
        "2:  li t2, 64\n   ble a2, t2, 3f\n   mv a2, t2\n"
        "3:  bgez a2, 4f\n   li a2, 0\n"
        "4:  srliw a4, a4, 24\n   beqz a4, 5f\n   bltz a3, 5f\n   beqz a2, 6f\n   ble a3, a2, 6f\n   mv a3, a2\n   j 6f\n"
        "5:  li a3, 0\n"
        "6:  sw a2, 0(a5)\n   sw a3, 4(a5)\n"
        "9:  " SP_ASM_RET
        SP_ASM_END(splan_tint_clamp)

        /* splan_tint_decompose(a0=rect, a1=ink, a2=radius, a3=edge_line, a4=edge, a5=out) -> 0|1 */
        SP_ASM_FUNC(splan_tint_decompose)
        "beqz a0, 9f\n   beqz a5, 9f\n   lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "blez t2, 8f\n   blez t3, 8f\n   sext.w a2, a2\n   sext.w a3, a3\n"
        "sraiw t4, t2, 1\n   ble a2, t4, 10f\n   mv a2, t4\n"
        "10: sraiw t4, t3, 1\n   ble a2, t4, 11f\n   mv a2, t4\n"
        "11: li t4, 64\n   ble a2, t4, 12f\n   mv a2, t4\n"
        "12: bgez a2, 13f\n   li a2, 0\n"
        "13: srliw t4, a4, 24\n   beqz t4, 1f\n   bltz a3, 1f\n   beqz a2, 2f\n   ble a3, a2, 2f\n   mv a3, a2\n   j 2f\n"
        "1:  li a3, 0\n"
        "2:  addi sp, sp, -48\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n   sd s5, 40(sp)\n"
        "sw a2, 0(a5)\n   sw a3, 4(a5)\n   addw s0, t0, t2\n   addw s1, t1, t3\n   beqz a2, 3f\n"
        "li t4, 4\n   sw t4, 8(a5)\n   subw t4, s0, a2\n   subw t5, s1, a2\n"
        "sw t0, 16(a5)\n   sw t1, 20(a5)\n   sw t4, 24(a5)\n   sw t1, 28(a5)\n"
        "sw t0, 32(a5)\n   sw t5, 36(a5)\n   sw t4, 40(a5)\n   sw t5, 44(a5)\n   j 4f\n"
        "3:  sw zero, 8(a5)\n"
        "4:  addi a0, a5, 48\n   li s2, 0\n   mv s3, t1\n   addiw s4, t3, 1\n   sraiw s4, s4, 1\n"
        "5:  bge s3, s1, 7f\n   subw t4, s3, t1\n   addiw t5, s1, -1\n   subw t5, t5, s3\n"
        "ble t4, t5, 50f\n   mv t4, t5\n"
        "50: slt t5, t4, a2\n   slt t6, t4, a3\n   mv s5, s1\n"
        "beqz a2, 52f\n   bgeu a2, s4, 52f\n   addw a6, t1, a2\n   blt s3, a6, 51f\n"
        "subw a6, s1, a2\n   bge s3, a6, 52f\n"
        "51: bge a6, s5, 52f\n   mv s5, a6\n"
        "52: beqz a3, 54f\n   bgeu a3, s4, 54f\n   addw a6, t1, a3\n   blt s3, a6, 53f\n"
        "subw a6, s1, a3\n   bge s3, a6, 54f\n"
        "53: bge a6, s5, 54f\n   mv s5, a6\n"
        "54: mv a6, a3\n   ble a6, t2, 55f\n   mv a6, t2\n"
        "55: beqz t5, 56f\n   mv a6, a2\n"
        "56: addw a7, t0, a6\n   subw t4, s0, a6\n   ble t4, a7, 57f\n"
        "mv a6, a1\n   beqz t6, 561f\n   mv a6, a4\n"
        "561: sw a7, 0(a0)\n   sw s3, 4(a0)\n   sw t4, 8(a0)\n   sw s5, 12(a0)\n   sw a6, 16(a0)\n   sw t6, 20(a0)\n"
        "addi a0, a0, 24\n   addi s2, s2, 1\n"
        "57: bnez t5, 59f\n   blez a3, 59f\n   mv a6, a3\n   ble a6, t2, 571f\n   mv a6, t2\n"
        "571: addw a7, t0, a6\n   subw t4, s0, a6\n   bge t4, a7, 572f\n   mv t4, a7\n"
        "572: li t6, 1\n   ble a7, t0, 58f\n"
        "sw t0, 0(a0)\n   sw s3, 4(a0)\n   sw a7, 8(a0)\n   sw s5, 12(a0)\n   sw a4, 16(a0)\n   sw t6, 20(a0)\n"
        "addi a0, a0, 24\n   addi s2, s2, 1\n"
        "58: ble s0, t4, 59f\n"
        "sw t4, 0(a0)\n   sw s3, 4(a0)\n   sw s0, 8(a0)\n   sw s5, 12(a0)\n   sw a4, 16(a0)\n   sw t6, 20(a0)\n"
        "addi a0, a0, 24\n   addi s2, s2, 1\n"
        "59: mv s3, s5\n   j 5b\n"
        "7:  sw s2, 12(a5)\n   li a0, 1\n"
        "ld s5, 40(sp)\n   ld s4, 32(sp)\n   ld s3, 24(sp)\n   ld s2, 16(sp)\n   ld s1, 8(sp)\n   ld s0, 0(sp)\n   addi sp, sp, 48\n" SP_ASM_RET
        "8:  sw zero, 0(a5)\n   sw zero, 4(a5)\n   sw zero, 8(a5)\n   sw zero, 12(a5)\n   li a0, 1\n" SP_ASM_RET
        "9:  li a0, 0\n" SP_ASM_RET
        SP_ASM_END(splan_tint_decompose)

        /* splan_cpu_tint_cut(a0=owner_rect, a1=ri, a2=py, a3=x2, a4=out) */
        SP_ASM_FUNC(splan_cpu_tint_cut)
        "beqz a4, 9f\n   sw a3, 0(a4)\n   sw a3, 4(a4)\n   beqz a0, 9f\n"
        "lw t0, 0(a0)\n   lw t1, 4(a0)\n   lw t2, 8(a0)\n   lw t3, 12(a0)\n"
        "addw t3, t1, t3\n   blt a2, t1, 9f\n   bge a2, t3, 9f\n"
        "addw t1, t1, a1\n   subw t3, t3, a1\n   addw t2, t0, t2\n   blt a2, t1, 1f\n   blt a2, t3, 2f\n"
        "1:  addw t0, t0, a1\n   subw t2, t2, a1\n"
        "2:  ble t2, t0, 9f\n   sw t0, 0(a4)\n   sw t2, 4(a4)\n"
        "9:  " SP_ASM_RET
        SP_ASM_END(splan_cpu_tint_cut)
);

#endif /* arch */

#endif /* COMPOSITOR_SCENE_PLAN_ASM_IMPLEMENTATION && !COMPOSITOR_SCENE_PLAN_ASM_EMITTED */
