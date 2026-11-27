/*
        Compositor pointer motion, shake detector, software cursor SDF/supersample
        rasterizer, dwell-ring overlay, and resize-edge hit-test in pure assembly:
          - prast_counts       : evdev delta sum clamped to [-WINDOW_COORD_MAX, WINDOW_COORD_MAX]
          - prast_gain_wanted  : flat/curved pointer gain scaled by sensitivity percent
          - prast_tablet       : tablet coordinate clamp & range-to-size projection
          - prast_shake        : direction-reversal cursor-shake detector (3 sensitivity tiers)
          - prast_edges_at     : 9-zone window resize edge/corner hit-test (overflow-safe)
          - prast_dyn_cover    : dynamic cursor SDF (rounded-rect, stretch, lens) fill/edge cover
          - prast_pointer_ring : clockwise dwell-click ring overlay (diamond angle + isqrt)
          - prast_pointer_rows : dynamic SDF & 2x2/4x4 bitmap supersample rasterizer + ring

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_POINTER_RAST_ASM_IMPLEMENTATION in one translation unit to
        emit the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_POINTER_RAST_ASM_H
#define MOONWATER_COMPOSITOR_POINTER_RAST_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  prast_u8;
typedef p16 prast_u16;
typedef p32 prast_u32;
typedef b32 prast_i32;
typedef p64 prast_u64;
typedef b64 prast_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  prast_u8;
typedef __UINT16_TYPE__ prast_u16;
typedef __UINT32_TYPE__ prast_u32;
typedef __INT32_TYPE__  prast_i32;
typedef __UINT64_TYPE__ prast_u64;
typedef __INT64_TYPE__  prast_i64;
#else
#include <stdint.h>
typedef uint8_t  prast_u8;
typedef uint16_t prast_u16;
typedef uint32_t prast_u32;
typedef int32_t  prast_i32;
typedef uint64_t prast_u64;
typedef int64_t  prast_i64;
#endif

#define PRAST_COORD_MAX    16777216
#define PRAST_ACCEL_ONE    1024
#define PRAST_SHAKE_WINDOW 700000000ull
#define PRAST_CELL         24
#define PRAST_CELL_STRIDE  25
#define PRAST_SHAPES       11
#define PRAST_SPAN_MOST    384
#define PRAST_RING_STEPS   32
#define PRAST_EDGE_LEFT    1u
#define PRAST_EDGE_RIGHT   2u
#define PRAST_EDGE_TOP     4u
#define PRAST_EDGE_BOTTOM  8u

/* 24 bytes — byte-for-byte identical to struct compositor_shake */
struct prast_shake {
        prast_u64 window;    /* +0 */
        prast_i32 direction; /* +8 */
        prast_i32 count;     /* +12 */
        prast_i32 feel;      /* +16 */
        prast_i32 _pad;      /* +20 */
};

/* 72 bytes — byte-for-byte identical to struct compositor_pointer (LP64) */
struct prast_pointer {
        prast_i32 dyn;       /* +0 */
        prast_u32 shape;     /* +4 */
        prast_i32 scale;     /* +8 */
        prast_i32 hw;        /* +12 */
        prast_i32 hh;        /* +16 */
        prast_i32 c;         /* +20 */
        prast_i32 ux;        /* +24 */
        prast_i32 uy;        /* +28 */
        prast_i32 grow;      /* +32 */
        prast_i32 lens;      /* +36 */
        prast_i32 span;      /* +40 */
        prast_i32 hot_x;     /* +44 */
        prast_i32 hot_y;     /* +48 */
        prast_i32 _pad0;     /* +52 */
        prast_u64 picture;   /* +56 */
        prast_i32 ring;      /* +64 */
        prast_i32 _pad1;     /* +68 */
};

/* 64 bytes — byte-for-byte identical to struct compositor_dyn_pre */
struct prast_dyn_pre {
        prast_i64 a;     /* +0 */
        prast_i64 half;  /* +8 */
        prast_i64 reach; /* +16 */
        prast_i64 line;  /* +24 */
        prast_i64 lined; /* +32 */
        prast_i64 bound; /* +40 */
        prast_i64 tip;   /* +48 */
        prast_i64 leg;   /* +56 */
};

/* 64 bytes — call descriptor for prast_pointer_rows & prast_pointer_ring */
struct prast_rows_arg {
        const struct prast_pointer *p;     /* +0 */
        const char                 *art;   /* +8  compositor_cursor_art[0][0] (11*24*25) */
        const signed char          *hot;   /* +16 compositor_cursor_hot[0] (11*2) */
        prast_u8                   *fill;  /* +24 */
        prast_u8                   *edge;  /* +32 */
        prast_i32                   span;  /* +40 */
        prast_i32                   hot_x; /* +44 */
        prast_i32                   hot_y; /* +48 */
        prast_i32                   first; /* +52 */
        prast_i32                   rows;  /* +56 */
        prast_i32                   pitch; /* +60 */
};

/* 40 bytes — call descriptor for prast_edges_at */
struct prast_edges_arg {
        prast_i32 px, py;         /* +0, +4 */
        prast_i32 x1, y1, x2, y2; /* +8, +12, +16, +20 */
        prast_i32 grip, in;       /* +24, +28 */
        prast_i32 top_in, corner; /* +32, +36 */
};

#ifdef offset_of
#define PRAST_OFFSET(T, M) offset_of(T, M)
#else
#define PRAST_OFFSET(T, M) ((prast_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct prast_shake)    == 24, "prast_shake must be 24 bytes");
_Static_assert(PRAST_OFFSET(struct prast_shake, window)    == 0,  "prast_shake.window");
_Static_assert(PRAST_OFFSET(struct prast_shake, direction) == 8,  "prast_shake.direction");
_Static_assert(PRAST_OFFSET(struct prast_shake, count)     == 12, "prast_shake.count");
_Static_assert(PRAST_OFFSET(struct prast_shake, feel)      == 16, "prast_shake.feel");
_Static_assert(sizeof(struct prast_pointer)  == 72, "prast_pointer must be 72 bytes");
_Static_assert(PRAST_OFFSET(struct prast_pointer, dyn)     == 0,  "prast_pointer.dyn");
_Static_assert(PRAST_OFFSET(struct prast_pointer, shape)   == 4,  "prast_pointer.shape");
_Static_assert(PRAST_OFFSET(struct prast_pointer, scale)   == 8,  "prast_pointer.scale");
_Static_assert(PRAST_OFFSET(struct prast_pointer, hw)      == 12, "prast_pointer.hw");
_Static_assert(PRAST_OFFSET(struct prast_pointer, hh)      == 16, "prast_pointer.hh");
_Static_assert(PRAST_OFFSET(struct prast_pointer, c)       == 20, "prast_pointer.c");
_Static_assert(PRAST_OFFSET(struct prast_pointer, ux)      == 24, "prast_pointer.ux");
_Static_assert(PRAST_OFFSET(struct prast_pointer, uy)      == 28, "prast_pointer.uy");
_Static_assert(PRAST_OFFSET(struct prast_pointer, grow)    == 32, "prast_pointer.grow");
_Static_assert(PRAST_OFFSET(struct prast_pointer, lens)    == 36, "prast_pointer.lens");
_Static_assert(PRAST_OFFSET(struct prast_pointer, picture) == 56, "prast_pointer.picture");
_Static_assert(PRAST_OFFSET(struct prast_pointer, ring)    == 64, "prast_pointer.ring");
_Static_assert(sizeof(struct prast_dyn_pre)  == 64, "prast_dyn_pre must be 64 bytes");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, a)       == 0,  "prast_dyn_pre.a");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, half)    == 8,  "prast_dyn_pre.half");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, reach)   == 16, "prast_dyn_pre.reach");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, line)    == 24, "prast_dyn_pre.line");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, lined)   == 32, "prast_dyn_pre.lined");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, bound)   == 40, "prast_dyn_pre.bound");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, tip)     == 48, "prast_dyn_pre.tip");
_Static_assert(PRAST_OFFSET(struct prast_dyn_pre, leg)     == 56, "prast_dyn_pre.leg");
_Static_assert(sizeof(struct prast_rows_arg) == 64, "prast_rows_arg must be 64 bytes");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, p)      == 0,  "prast_rows_arg.p");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, art)    == 8,  "prast_rows_arg.art");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, hot)    == 16, "prast_rows_arg.hot");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, fill)   == 24, "prast_rows_arg.fill");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, edge)   == 32, "prast_rows_arg.edge");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, span)   == 40, "prast_rows_arg.span");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, hot_x)  == 44, "prast_rows_arg.hot_x");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, hot_y)  == 48, "prast_rows_arg.hot_y");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, first)  == 52, "prast_rows_arg.first");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, rows)   == 56, "prast_rows_arg.rows");
_Static_assert(PRAST_OFFSET(struct prast_rows_arg, pitch)  == 60, "prast_rows_arg.pitch");
_Static_assert(sizeof(struct prast_edges_arg) == 40, "prast_edges_arg must be 40 bytes");

prast_i32 prast_counts(prast_i32 sum, prast_i64 more);
prast_i32 prast_gain_wanted(prast_i32 gain, prast_i32 flat, prast_i32 percent);
prast_i32 prast_tablet(prast_i32 value, prast_i32 minimum, prast_i32 maximum, prast_i32 size);
prast_i32 prast_shake(struct prast_shake *shake, prast_i32 delta, prast_u64 now);
prast_u32 prast_edges_at(const struct prast_edges_arg *arg);
void      prast_dyn_cover(const struct prast_pointer *p, const struct prast_dyn_pre *pre,
                          prast_i64 u, prast_i64 v, prast_u8 *fill, prast_u8 *edge);
void      prast_pointer_ring(const struct prast_rows_arg *arg);
void      prast_pointer_rows(const struct prast_rows_arg *arg);

#endif /* MOONWATER_COMPOSITOR_POINTER_RAST_ASM_H */

#if defined(COMPOSITOR_POINTER_RAST_ASM_IMPLEMENTATION) && !defined(COMPOSITOR_POINTER_RAST_ASM_EMITTED)
#define COMPOSITOR_POINTER_RAST_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define PRAST_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define PRAST_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define PRAST_RISCV64 1
# else
#  error "src/canvas/compositor/pointer_rast.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define PRAST_ARM64 1
# elif defined(X64) && X64
#  define PRAST_X64 1
# elif defined(RISCV64) && RISCV64
#  define PRAST_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define PRAST_ASM_SECTION   ASM_SECTION
# define PRAST_ASM_FUNC(n)   ASM_FUNC(n)
# define PRAST_ASM_END(n)    ASM_END(n)
# define PRAST_ASM_RET       ASM_RET
#else
# define PRAST_ASM_SECTION   ".text\n"
# define PRAST_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define PRAST_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define PRAST_ASM_END(n)   ""
# elif defined(_WIN32)
#  define PRAST_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define PRAST_ASM_END(n)   ""
# else
#  if defined(PRAST_ARM64)
#   define PRAST_ASM_FTYPE   "%function"
#  else
#   define PRAST_ASM_FTYPE   "@function"
#  endif
#  define PRAST_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " PRAST_ASM_FTYPE "\n" #n ":\n"
#  define PRAST_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define PRAST_SYM(n)        "_" #n
# define PRAST_HELPER(n)     ".text\n.balign 16\nL_" #n ":\n"
# define PRAST_HELPER_END(n) ""
# define PRAST_HREF(n)       "L_" #n
#elif defined(_WIN32)
# define PRAST_SYM(n)        #n
# define PRAST_HELPER(n)     ".section .text,\"xr\"\n.balign 16\n.L_" #n ":\n"
# define PRAST_HELPER_END(n) ""
# define PRAST_HREF(n)       ".L_" #n
#else
# define PRAST_SYM(n)        #n
# define PRAST_HELPER(n)     ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.L_" #n ":\n"
# define PRAST_HELPER_END(n) ".size .L_" #n ", .-.L_" #n "\n.popsection\n"
# define PRAST_HREF(n)       ".L_" #n
#endif

/* =========================================================================
 * ARM64 (AArch64 AAPCS64 — Linux ELF / macOS Mach-O / Windows ARM64)
 * ========================================================================= */
#if defined(PRAST_ARM64)

__asm__(
        PRAST_ASM_SECTION

        /* internal isqrt(x0=n) -> x0=floor(sqrt(n)); clobbers only x0, x1, x2 */
        PRAST_HELPER(prast_isqrt)
        "cbz x0, 2f\n   clz x1, x0\n   mov x2, #63\n   sub x1, x2, x1\n   lsr x1, x1, #1\n"
        "add x1, x1, #1\n   mov x2, #1\n   lsl x1, x2, x1\n"
        "1:  udiv x2, x0, x1\n   add x2, x2, x1\n   lsr x2, x2, #1\n   cmp x2, x1\n   b.hs 3f\n   mov x1, x2\n   b 1b\n"
        "3:  mov x0, x1\n"
        "2:  " PRAST_ASM_RET
        PRAST_HELPER_END(prast_isqrt)

        /* prast_counts(w0=sum, x1=more) -> clamped i32 */
        PRAST_ASM_FUNC(prast_counts)
        "sxtw x0, w0\n   add x0, x0, x1\n   mov x2, #0x1000000\n   neg x3, x2\n"
        "cmp x0, x2\n   csel x0, x2, x0, gt\n   cmp x0, x3\n   csel x0, x3, x0, lt\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_counts)

        /* prast_gain_wanted(w0=gain, w1=flat, w2=percent) -> i32 */
        PRAST_ASM_FUNC(prast_gain_wanted)
        "mov w3, #1024\n   cmp w1, #0\n   csel w0, w3, w0, ne\n   sxtw x0, w0\n"
        "cbz w2, 1f\n   cmp w2, #100\n   b.eq 1f\n   sxtw x2, w2\n   mul x0, x0, x2\n   mov x3, #100\n   sdiv x0, x0, x3\n"
        "1:  " PRAST_ASM_RET
        PRAST_ASM_END(prast_gain_wanted)

        /* prast_tablet(w0=value, w1=min, w2=max, w3=size) -> i32 */
        PRAST_ASM_FUNC(prast_tablet)
        "sxtw x1, w1\n   sxtw x2, w2\n   sub x4, x2, x1\n   cmp x4, #0\n   b.le 1f\n"
        "sxtw x0, w0\n   cmp x0, x1\n   csel x0, x1, x0, lt\n   cmp x0, x2\n   csel x0, x2, x0, gt\n"
        "sub x0, x0, x1\n   sxtw x3, w3\n   mul x0, x0, x3\n   sdiv x0, x0, x4\n" PRAST_ASM_RET
        "1:  mov w0, #0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_tablet)

        /* prast_shake(x0=shake, w1=delta, x2=now) -> 0/1 */
        PRAST_ASM_FUNC(prast_shake)
        "cbz x0, 9f\n   ldr w3, [x0, #16]\n   mov w4, #6\n   mov w5, #5\n"
        "cmp w3, #1\n   b.ne 1f\n   mov w4, #9\n   mov w5, #7\n   b 2f\n"
        "1:  cmp w3, #2\n   b.ne 2f\n   mov w4, #4\n   mov w5, #4\n"
        "2:  neg w6, w4\n   cmp w1, w6\n   b.le 3f\n   cmp w1, w4\n   b.lt 9f\n"
        "3:  mov w6, #1\n   mov w7, #-1\n   cmp w1, #0\n   csel w6, w6, w7, gt\n"
        "ldr x7, [x0]\n   sub x8, x2, x7\n   movz x9, #0x2700\n   movk x9, #0x29b9, lsl #16\n"
        "cmp x8, x9\n   b.ls 4f\n   str x2, [x0]\n   str wzr, [x0, #12]\n"
        "4:  ldr w7, [x0, #8]\n   cmp w6, w7\n   b.eq 9f\n   str w6, [x0, #8]\n"
        "ldr w7, [x0, #12]\n   add w7, w7, #1\n   cmp w7, w5\n   b.lt 5f\n   str wzr, [x0, #12]\n   mov w0, #1\n" PRAST_ASM_RET
        "5:  str w7, [x0, #12]\n"
        "9:  mov w0, #0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_shake)

        /* prast_edges_at(x0=arg) -> edge bitmask */
        PRAST_ASM_FUNC(prast_edges_at)
        "cbz x0, 9f\n   ldrsw x1, [x0]\n   ldrsw x2, [x0, #4]\n   ldrsw x3, [x0, #8]\n   ldrsw x4, [x0, #12]\n"
        "ldrsw x5, [x0, #16]\n   ldrsw x6, [x0, #20]\n   ldrsw x7, [x0, #24]\n   ldrsw x8, [x0, #28]\n"
        "ldrsw x9, [x0, #32]\n   ldrsw x10, [x0, #36]\n"
        "sub x11, x3, x7\n   cmp x1, x11\n   b.lt 9f\n   add x11, x5, x7\n   cmp x1, x11\n   b.ge 9f\n"
        "sub x11, x4, x7\n   cmp x2, x11\n   b.lt 9f\n   add x11, x6, x7\n   cmp x2, x11\n   b.ge 9f\n"
        "mov w0, #0\n   add x11, x3, x8\n   cmp x1, x11\n   b.ge 1f\n   orr w0, w0, #1\n   b 2f\n"
        "1:  sub x11, x5, x8\n   cmp x1, x11\n   b.lt 2f\n   orr w0, w0, #2\n"
        "2:  add x11, x4, x9\n   cmp x2, x11\n   b.ge 3f\n   orr w0, w0, #4\n   b 4f\n"
        "3:  sub x11, x6, x8\n   cmp x2, x11\n   b.lt 4f\n   orr w0, w0, #8\n"
        "4:  cbz x10, 8f\n   tst w0, #3\n   b.eq 5f\n   tst w0, #12\n   b.ne 8f\n"
        "add x11, x4, x10\n   cmp x2, x11\n   b.ge 6f\n   orr w0, w0, #4\n" PRAST_ASM_RET
        "6:  sub x11, x6, x10\n   cmp x2, x11\n   b.lt 8f\n   orr w0, w0, #8\n" PRAST_ASM_RET
        "5:  tst w0, #12\n   b.eq 8f\n   add x11, x3, x10\n   cmp x1, x11\n   b.ge 7f\n   orr w0, w0, #1\n" PRAST_ASM_RET
        "7:  sub x11, x5, x10\n   cmp x1, x11\n   b.lt 8f\n   orr w0, w0, #2\n"
        "8:  " PRAST_ASM_RET
        "9:  mov w0, #0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_edges_at)

        /* prast_dyn_cover(x0=p, x1=pre, x2=u, x3=v, x4=fill, x5=edge) */
        PRAST_ASM_FUNC(prast_dyn_cover)
        "cbz x4, 99f\n   cbz x5, 99f\n   strb wzr, [x4]\n   strb wzr, [x5]\n   cbz x0, 99f\n   cbz x1, 99f\n"
        "ldr x12, [x1, #40]\n   cmp x2, x12\n   b.gt 99f\n   neg x13, x12\n   cmp x2, x13\n   b.lt 99f\n"
        "cmp x3, x12\n   b.gt 99f\n   cmp x3, x13\n   b.lt 99f\n"
        "ldrsw x14, [x0, #8]\n   cmp x14, #0\n   b.le 99f\n"
        "str x30, [sp, #-16]!\n   mov x6, x0\n   mov x7, x1\n   mov x8, x2\n   mov x9, x3\n   mov x10, x4\n   mov x11, x5\n"
        "ldr w12, [x6, #36]\n   cbz w12, 12f\n"
        /* lens path */
        "ldrsw x12, [x6, #24]\n   ldrsw x13, [x6, #28]\n   mul x14, x8, x12\n   madd x14, x9, x13, x14\n"
        "asr x15, x14, #63\n   add x14, x14, x15, lsr #58\n   asr x14, x14, #6\n"
        "mul x15, x9, x12\n   msub x15, x8, x13, x15\n   asr x16, x15, #63\n   add x15, x15, x16, lsr #58\n   asr x15, x15, #6\n"
        "cmp x14, #0\n   cneg x14, x14, lt\n   cmp x15, #0\n   cneg x15, x15, lt\n"
        "ldrsw x12, [x6, #16]\n   ldr x13, [x7, #48]\n   cmp x13, x12\n   b.eq 10f\n   cbz x13, 98f\n"
        "ldr x16, [x7, #56]\n   mul x0, x14, x16\n   mul x1, x15, x12\n   cmp x0, x1\n   b.le 10f\n"
        "sub x17, x14, x13\n   mul x0, x17, x16\n   cmp x0, x1\n   b.ge 11f\n"
        "mul x0, x14, x12\n   madd x0, x15, x16, x0\n   sdiv x0, x0, x13\n   sub x14, x0, x12\n   b 30f\n"
        "10: mul x0, x14, x14\n   madd x0, x15, x15, x0\n   bl " PRAST_HREF(prast_isqrt) "\n   sub x14, x0, x12\n   b 30f\n"
        "11: mul x0, x17, x17\n   madd x0, x15, x15, x0\n   bl " PRAST_HREF(prast_isqrt) "\n   mov x14, x0\n   b 30f\n"
        /* rounded-rect / stretched path */
        "12: ldr w12, [x6, #32]\n   cbz w12, 13f\n   ldr x3, [x7]\n   cbz x3, 98f\n"
        "ldrsw x12, [x6, #24]\n   ldrsw x13, [x6, #28]\n   mul x14, x8, x12\n   madd x14, x9, x13, x14\n"
        "asr x15, x14, #63\n   add x14, x14, x15, lsr #58\n   asr x14, x14, #6\n   lsl x14, x14, #8\n   sdiv x14, x14, x3\n"
        "mul x15, x9, x12\n   msub x15, x8, x13, x15\n   asr x16, x15, #63\n   add x15, x15, x16, lsr #58\n   asr x15, x15, #6\n"
        "mul x15, x15, x3\n   asr x16, x15, #63\n   add x15, x15, x16, lsr #56\n   asr x15, x15, #8\n"
        "mul x8, x14, x12\n   msub x8, x15, x13, x8\n   asr x16, x8, #63\n   add x8, x8, x16, lsr #58\n   asr x8, x8, #6\n"
        "mul x9, x14, x13\n   madd x9, x15, x12, x9\n   asr x16, x9, #63\n   add x9, x9, x16, lsr #58\n   asr x9, x9, #6\n"
        "13: ldrsw x12, [x6, #12]\n   ldrsw x13, [x6, #16]\n   ldrsw x4, [x6, #20]\n"
        "cmp x8, #0\n   cneg x14, x8, lt\n   sub x12, x12, x4\n   sub x14, x14, x12\n"
        "cmp x9, #0\n   cneg x15, x9, lt\n   sub x13, x13, x4\n   sub x15, x15, x13\n"
        "mov x16, #64\n   mov x17, #0\n   cmp x14, #0\n   b.le 20f\n   cmp x15, #0\n   b.le 20f\n"
        "mul x0, x14, x14\n   madd x0, x15, x15, x0\n   ldr x12, [x7, #16]\n   add x13, x4, x12\n   mul x13, x13, x13\n"
        "cmp x0, x13\n   b.ge 98f\n   ldr x3, [x7]\n   ldr x5, [x7, #32]\n   mul x5, x5, x3\n"
        "asr x13, x5, #63\n   add x5, x5, x13, lsr #56\n   asr x5, x5, #8\n   sub x5, x4, x5\n   sub x5, x5, x12\n"
        "cmp x5, #0\n   b.le 19f\n   mul x13, x5, x5\n   cmp x0, x13\n   b.gt 19f\n   mov w12, #255\n   strb w12, [x10]\n   b 98f\n"
        "19: bl " PRAST_HREF(prast_isqrt) "\n   lsl x16, x14, #6\n   sdiv x16, x16, x0\n   lsl x17, x15, #6\n   sdiv x17, x17, x0\n"
        "sub x14, x0, x4\n   b 22f\n"
        "20: cmp x14, x15\n   b.le 21f\n   sub x14, x14, x4\n   b 22f\n"
        "21: sub x14, x15, x4\n   mov x16, #0\n   mov x17, #64\n"
        "22: ldr w12, [x6, #32]\n   cbz w12, 30f\n   ldr x3, [x7]\n   cbz x3, 98f\n"
        "cmp x8, #0\n   cneg x16, x16, lt\n   cmp x9, #0\n   cneg x17, x17, lt\n"
        "ldrsw x12, [x6, #24]\n   ldrsw x13, [x6, #28]\n   mul x0, x16, x12\n   madd x0, x17, x13, x0\n"
        "asr x1, x0, #63\n   add x0, x0, x1, lsr #58\n   asr x0, x0, #6\n   lsl x0, x0, #8\n   sdiv x4, x0, x3\n"
        "mul x1, x17, x12\n   msub x1, x16, x13, x1\n   asr x2, x1, #63\n   add x1, x1, x2, lsr #58\n   asr x1, x1, #6\n"
        "mul x1, x1, x3\n   asr x2, x1, #63\n   add x1, x1, x2, lsr #56\n   asr x5, x1, #8\n"
        "mul x0, x4, x4\n   madd x0, x5, x5, x0\n   bl " PRAST_HREF(prast_isqrt) "\n"
        "cmp x0, #0\n   b.ne 23f\n   mov x0, #1\n"
        "23: lsl x14, x14, #6\n   sdiv x14, x14, x0\n"
        /* lined: away in x14 */
        "30: ldrsw x12, [x6, #8]\n   mul x14, x14, x12\n   asr x15, x14, #63\n   add x14, x14, x15, lsr #56\n   asr x14, x14, #8\n"
        "mov x12, #128\n   sub x15, x12, x14\n   mov x13, #256\n   cmp x15, #0\n   csel x15, xzr, x15, lt\n"
        "cmp x15, x13\n   csel x15, x13, x15, gt\n   ldr x0, [x7, #24]\n   sub x16, x12, x14\n   sub x16, x16, x0\n"
        "cmp x16, #0\n   csel x16, xzr, x16, lt\n   cmp x16, x13\n   csel x16, x13, x16, gt\n"
        "sub x17, x15, x16\n   mov x12, #255\n   mul x0, x17, x12\n   lsr x0, x0, #8\n   strb w0, [x11]\n"
        "cbz x16, 98f\n   sub x13, x13, x17\n   mul x16, x16, x12\n   udiv x16, x16, x13\n   strb w16, [x10]\n"
        "98: ldr x30, [sp], #16\n"
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_dyn_cover)

        /* prast_pointer_ring(x0=arg) */
        PRAST_ASM_FUNC(prast_pointer_ring)
        "cbz x0, 99f\n   ldr x1, [x0]\n   cbz x1, 99f\n   ldr x2, [x0, #24]\n   cbz x2, 99f\n   ldr x3, [x0, #32]\n   cbz x3, 99f\n"
        "ldr w4, [x0, #40]\n   cmp w4, #0\n   b.le 99f\n   ldr w5, [x0, #56]\n   cmp w5, #0\n   b.le 99f\n"
        "ldrsw x6, [x1, #8]\n   cmp x6, #0\n   b.le 99f\n"
        "stp x19, x20, [sp, #-96]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
        "stp x25, x26, [sp, #48]\n   stp x27, x28, [sp, #64]\n   str x30, [sp, #80]\n"
        /* x19=fill, x20=edge, w21=span, w22=row, w23=end_row, x24=pitch */
        "mov x19, x2\n   mov x20, x3\n   mov w21, w4\n   ldr w22, [x0, #52]\n   add w23, w22, w5\n   ldrsw x24, [x0, #60]\n"
        /* core = (max(scale,256)*3)>>2 in x25; turn = ring<<7 in x26 */
        "mov x7, #256\n   cmp x6, x7\n   csel x7, x6, x7, gt\n   add x7, x7, x7, lsl #1\n   lsr x25, x7, #2\n"
        "ldrsw x26, [x1, #64]\n   lsl x26, x26, #7\n"
        "ldrsw x8, [x0, #44]\n   lsl x8, x8, #8\n   ldrsw x9, [x0, #48]\n   lsl x9, x9, #8\n"
        "ldr w7, [x1]\n   cbz w7, 1f\n"
        /* dyn: cx=hot_x<<8, cy=hot_y<<8, r=12*scale */
        "mov x10, #12\n   mul x27, x6, x10\n   b 2f\n"
        /* bitmap: verify hot & shape < 11 */
        "1:  ldr x10, [x0, #16]\n   cbz x10, 90f\n   ldr w11, [x1, #4]\n   cmp w11, #11\n   b.hs 90f\n"
        "lsl x11, x11, #1\n   ldrsb x12, [x10, x11]\n   add x11, x11, #1\n   ldrsb x13, [x10, x11]\n"
        "mov x14, #12\n   sub x12, x14, x12\n   madd x8, x12, x6, x8\n   sub x13, x14, x13\n   madd x9, x13, x6, x9\n"
        "mov x14, #19\n   mul x27, x6, x14\n   lsr x27, x27, #1\n"
        /* x8=cx, x9=cy, x27=r, x25=core, x26=turn */
        "2:  add x10, x25, #512\n   sub x11, x27, x10\n   cmp x27, x10\n   csel x11, x11, xzr, gt\n   mul x11, x11, x11\n"
        "add x12, x27, x10\n   mul x12, x12, x12\n   mov x28, x8\n   mov x16, x9\n   mov x17, x11\n   mov x15, x12\n"
        /* x28=cx, x16=cy, x17=inner, x15=outer (preserved across L_prast_isqrt!) */
        "3:  cmp w22, w23\n   b.ge 90f\n   sxtw x4, w22\n   lsl x4, x4, #8\n   add x4, x4, #128\n   sub x4, x4, x16\n"
        "mul x5, x4, x4\n   cmp x5, x15\n   b.gt 8f\n"
        "mov x6, #128\n   sub x6, x6, x28\n   mov x7, #0\n"
        "4:  cmp w7, w21\n   b.ge 8f\n   mul x0, x6, x6\n   add x0, x0, x5\n   cmp x0, x15\n   b.gt 7f\n   cmp x0, x17\n   b.lt 7f\n"
        "neg x8, x4\n   cmp x6, #0\n   b.lt 5f\n   cmp x8, #0\n   b.le 5f\n   mov x9, #0\n   mov x10, x6\n   mov x11, x8\n   b 6f\n"
        "5:  cmp x6, #0\n   b.le 10f\n   mov x9, #1024\n   neg x10, x8\n   mov x11, x6\n   b 6f\n"
        "10: cmp x8, #0\n   b.ge 11f\n   mov x9, #2048\n   neg x10, x6\n   neg x11, x8\n   b 6f\n"
        "11: mov x9, #3072\n   mov x10, x8\n   neg x11, x6\n"
        "6:  add x12, x10, x11\n   cmp x12, #0\n   b.ne 12f\n   mov x12, #1\n"
        "12: lsl x10, x10, #10\n   sdiv x10, x10, x12\n   add x9, x9, x10\n   cmp x9, x26\n   b.ge 7f\n"
        "bl " PRAST_HREF(prast_isqrt) "\n   sub x8, x0, x27\n   cmp x8, #0\n   cneg x8, x8, lt\n"
        "add x9, x25, #128\n   sub x9, x9, x8\n   mov x10, #256\n   cmp x9, #0\n   csel x9, xzr, x9, lt\n"
        "cmp x9, x10\n   csel x9, x10, x9, gt\n   add x11, x25, #384\n   sub x11, x11, x8\n"
        "cmp x11, #0\n   csel x11, xzr, x11, lt\n   cmp x11, x10\n   csel x11, x10, x11, gt\n   sub x11, x11, x9\n"
        "mov x12, #255\n   mul x9, x9, x12\n   lsr x9, x9, #8\n   mul x11, x11, x12\n   lsr x11, x11, #8\n"
        "ldrb w13, [x19, x7]\n   cmp w13, w9\n   csel w13, w13, w9, hi\n   strb w13, [x19, x7]\n"
        "ldrb w13, [x20, x7]\n   cmp w13, w11\n   csel w13, w13, w11, hi\n   strb w13, [x20, x7]\n"
        "7:  add x6, x6, #256\n   add x7, x7, #1\n   b 4b\n"
        "8:  add w22, w22, #1\n   add x19, x19, x24\n   add x20, x20, x24\n   b 3b\n"
        "90: ldr x30, [sp, #80]\n   ldp x27, x28, [sp, #64]\n   ldp x25, x26, [sp, #48]\n"
        "ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #96\n"
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_pointer_ring)

        /* prast_pointer_rows(x0=arg)
           Stack layout (832 bytes):
             [sp+0..63]    : prast_dyn_pre (dyn) OR col_u[16] (bitmap, +0..15) + scratch (+16..63)
             [sp+64..191]  : down_ptr[16] (128 bytes)
             [sp+192..769] : share[289][2] (578 bytes)
             [sp+784..831] : saved locals / alignment (frame = 928 = 832 + 96 callee-saved)
        */
        PRAST_ASM_FUNC(prast_pointer_rows)
        "cbz x0, 99f\n   ldr x1, [x0]\n   cbz x1, 99f\n   ldr x2, [x0, #24]\n   cbz x2, 99f\n   ldr x3, [x0, #32]\n   cbz x3, 99f\n"
        "ldr w4, [x0, #40]\n   cmp w4, #0\n   b.le 99f\n   ldr w5, [x0, #56]\n   cmp w5, #0\n   b.le 99f\n"
        "ldrsw x6, [x1, #8]\n   cmp x6, #0\n   b.le 99f\n"
        "stp x19, x20, [sp, #-96]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
        "stp x25, x26, [sp, #48]\n   stp x27, x28, [sp, #64]\n   str x30, [sp, #80]\n"
        "sub sp, sp, #832\n   mov x19, x0\n   mov x20, x1\n   mov x21, x2\n   mov x22, x3\n"
        /* x19=arg, x20=p, x21=fill, x22=edge, x6=scale */
        "ldr w7, [x20]\n   cbz w7, 30f\n"
        /* ---- DYNAMIC CURSOR PATH ---- */
        "ldrsw x7, [x20, #32]\n   add x7, x7, #256\n   str x7, [sp]\n" /* pre.a */
        "mov x8, #32768\n   sdiv x8, x8, x6\n   add x8, x8, #1\n   str x8, [sp, #8]\n" /* pre.half */
        "mul x9, x8, x7\n   asr x10, x9, #63\n   add x9, x9, x10, lsr #56\n   asr x9, x9, #8\n   add x9, x9, #1\n   str x9, [sp, #16]\n" /* pre.reach */
        "mov x9, #256\n   cmp x6, x9\n   csel x10, x9, x6, lt\n   str x10, [sp, #24]\n" /* pre.line */
        "lsl x10, x10, #8\n   sdiv x10, x10, x6\n   str x10, [sp, #32]\n" /* pre.lined */
        "ldrsw x10, [x20, #12]\n   ldrsw x11, [x20, #16]\n   add x12, x10, x11\n   mul x12, x12, x7\n"
        "asr x13, x12, #63\n   add x12, x12, x13, lsr #56\n   asr x12, x12, #8\n   add x12, x12, x8\n   str x12, [sp, #40]\n" /* pre.bound */
        "cmp x10, x11\n   csel x12, x10, x11, gt\n   str x12, [sp, #48]\n" /* pre.tip */
        "mov x13, #0\n   ldr w14, [x20, #36]\n   cbz w14, 1f\n   cmp x12, x11\n   b.le 1f\n"
        "mul x0, x12, x12\n   msub x0, x11, x11, x0\n   bl " PRAST_HREF(prast_isqrt) "\n   mov x13, x0\n"
        "1:  str x13, [sp, #56]\n" /* pre.leg */
        /* step = (32768<<16)/scale -> x23; ox -> x24; oy -> x25 */
        "mov x8, #0x80000000\n   sdiv x23, x8, x6\n"
        "ldrsw x8, [x19, #44]\n   lsl x8, x8, #16\n   sdiv x8, x8, x6\n   add x24, x8, #128\n"
        "ldrsw x8, [x19, #48]\n   lsl x8, x8, #16\n   sdiv x8, x8, x6\n   add x25, x8, #128\n"
        "ldr w26, [x19, #52]\n   ldr w8, [x19, #56]\n   add w27, w26, w8\n   ldr x28, [sp, #40]\n" /* row, end_row, bound */
        "2:  cmp w26, w27\n   b.ge 80f\n   sxtw x8, w26\n   lsl x8, x8, #1\n   add x8, x8, #1\n"
        "mul x8, x8, x23\n   asr x8, x8, #16\n   sub x8, x8, x25\n" /* v */
        "cmp x8, x28\n   b.gt 3f\n   neg x9, x28\n   cmp x8, x9\n   b.ge 5f\n"
        /* fast zero row */
        "3:  ldr w9, [x19, #40]\n   mov x10, #0\n"
        "4:  strb wzr, [x21, x10]\n   strb wzr, [x22, x10]\n   add x10, x10, #1\n   cmp w10, w9\n   b.lt 4b\n   b 9f\n"
        "5:  str x8, [sp, #64]\n   mov x9, x23\n   lsl x10, x23, #1\n   str x10, [sp, #72]\n   mov x11, #0\n   ldr w12, [x19, #40]\n"
        "6:  cmp w11, w12\n   b.ge 9f\n   asr x2, x9, #16\n   sub x2, x2, x24\n   ldr x10, [sp, #72]\n   add x9, x9, x10\n"
        "cmp x2, x28\n   b.gt 7f\n   neg x13, x28\n   cmp x2, x13\n   b.lt 7f\n"
        "str x9, [sp, #80]\n   str x11, [sp, #88]\n   mov x0, x20\n   mov x1, sp\n   ldr x3, [sp, #64]\n"
        "add x4, x21, x11\n   add x5, x22, x11\n   bl " PRAST_SYM(prast_dyn_cover) "\n"
        "ldr x9, [sp, #80]\n   ldr x11, [sp, #88]\n   ldr w12, [x19, #40]\n   add x11, x11, #1\n   b 6b\n"
        "7:  strb wzr, [x21, x11]\n   strb wzr, [x22, x11]\n   add x11, x11, #1\n   b 6b\n"
        "9:  ldrsw x8, [x19, #60]\n   add x21, x21, x8\n   add x22, x22, x8\n   add w26, w26, #1\n   b 2b\n"

        /* ---- BITMAP SUPERSAMPLE PATH ---- */
        "30: ldr x7, [x19, #8]\n   cbz x7, 31f\n   ldr x8, [x19, #16]\n   cbz x8, 31f\n"
        "ldr w9, [x20, #4]\n   cmp w9, #11\n   b.lo 33f\n"
        /* invalid art/hot/shape: zero output safely */
        "31: ldr w4, [x19, #40]\n   ldr w5, [x19, #56]\n   ldrsw x6, [x19, #60]\n"
        "32: mov x10, #0\n"
        "321: strb wzr, [x21, x10]\n   strb wzr, [x22, x10]\n   add x10, x10, #1\n   cmp w10, w4\n   b.lt 321b\n"
        "add x21, x21, x6\n   add x22, x22, x6\n   subs w5, w5, #1\n   b.gt 32b\n   b 80f\n"
        /* valid bitmap: n in w23, all in w24, scale in x6 */
        "33: mov w23, #4\n   mov w10, #2\n   cmp x6, #1024\n   csel w23, w23, w10, lt\n   mul w24, w23, w23\n"
        /* build share[e*(all+1)+f][2] at [sp+192] */
        "add x10, sp, #192\n   add w11, w24, #1\n   mul x12, x24, x6\n   mov w13, #0\n" /* e=0 */
        "34: cmp w13, w24\n   b.gt 39f\n   mov w14, #0\n" /* f=0 */
        "35: add w15, w13, w14\n   cmp w15, w24\n   b.gt 38f\n"
        "cmp x6, #256\n   b.lt 36f\n"
        "mov w15, #255\n   mul w16, w13, w15\n   udiv w16, w16, w24\n" /* lined */
        "mov w17, #0\n   cbz w14, 37f\n   mul w17, w14, w15\n   sub w15, w24, w13\n   udiv w17, w17, w15\n   b 37f\n"
        "36: mov x15, #65280\n   mul x16, x13, x15\n   udiv x16, x16, x12\n   cmp x16, #255\n   mov x0, #255\n   csel x16, x0, x16, gt\n"
        "mul x17, x14, x15\n   udiv x17, x17, x12\n   sub x15, x0, x16\n   cmp x17, x15\n   csel x17, x15, x17, gt\n"
        "cbz x17, 37f\n   mul x17, x17, x0\n   udiv x17, x17, x15\n"
        "37: mul w15, w13, w11\n   add w15, w15, w14\n   lsl w15, w15, #1\n"
        "strb w17, [x10, x15]\n   add w15, w15, #1\n   strb w16, [x10, x15]\n   add w14, w14, #1\n   b 35b\n"
        "38: add w13, w13, #1\n   b 34b\n"
        /* step = (32768<<16)/(all*scale) -> [sp+24]; ox -> x25; oy -> x26; shape_art -> x27 */
        "39: mov x10, #0x80000000\n   sdiv x10, x10, x12\n   str x10, [sp, #24]\n   lsl x11, x10, #1\n   str x11, [sp, #32]\n"
        "ldr w9, [x20, #4]\n   mov x12, #600\n   mul x12, x9, x12\n   add x27, x7, x12\n" /* shape_art */
        "lsl x9, x9, #1\n   ldrsb x12, [x8, x9]\n   add x9, x9, #1\n   ldrsb x13, [x8, x9]\n"
        "ldrsw x14, [x19, #44]\n   lsl x14, x14, #16\n   sdiv x14, x14, x6\n   sub x25, x14, x12, lsl #8\n" /* ox */
        "ldrsw x14, [x19, #48]\n   lsl x14, x14, #16\n   sdiv x14, x14, x6\n   sub x26, x14, x13, lsl #8\n" /* oy */
        "ldr w28, [x19, #52]\n   ldr w14, [x19, #56]\n   add w14, w28, w14\n   str w14, [sp, #40]\n" /* end_row */
        "40: ldr w14, [sp, #40]\n   cmp w28, w14\n   b.ge 80f\n"
        /* build down_ptr[0..all-1] at [sp+64] */
        "sxtw x0, w28\n   mul x0, x0, x24\n   lsl x0, x0, #1\n   add x0, x0, #1\n"
        "ldr x1, [sp, #24]\n   ldr x2, [sp, #32]\n   mul x0, x0, x1\n" /* vq */
        "add x3, sp, #64\n   mov w4, #0\n   mov w5, #0\n   mov x6, #6144\n   mov x7, #25\n"
        "41: cmp w4, w24\n   b.ge 43f\n   asr x8, x0, #16\n   sub x8, x8, x26\n   add x0, x0, x2\n"
        "cmp x8, x6\n   b.hs 42f\n   lsr x8, x8, #8\n   mul x8, x8, x7\n   add x8, x27, x8\n   str x8, [x3, x4, lsl #3]\n   mov w5, #1\n   add w4, w4, #1\n   b 41b\n"
        "42: str xzr, [x3, x4, lsl #3]\n   add w4, w4, #1\n   b 41b\n"
        "43: cbnz w5, 45f\n"
        /* empty row: fast zero */
        "ldr w4, [x19, #40]\n   mov x5, #0\n"
        "44: strb wzr, [x21, x5]\n   strb wzr, [x22, x5]\n   add x5, x5, #1\n   cmp w5, w4\n   b.lt 44b\n   b 55f\n"
        /* non-empty row: loop col = 0..span-1 */
        "45: ldr x0, [sp, #24]\n   ldr x2, [sp, #32]\n   sub w8, w24, #1\n   mul x8, x8, x2\n   mul x9, x24, x2\n" /* uq=step, span_step=(all-1)*step2, col_step=all*step2 */
        "ldr w10, [x19, #40]\n   mov x11, #0\n" /* col=0 */
        "46: cmp w11, w10\n   b.ge 55f\n"
        "asr x12, x0, #16\n   sub x12, x12, x25\n   add x13, x0, x8\n   asr x13, x13, #16\n   sub x13, x13, x25\n"
        "cmp x13, #0\n   b.lt 47f\n   cmp x12, x6\n   b.lt 48f\n"
        "47: strb wzr, [x21, x11]\n   strb wzr, [x22, x11]\n   add x0, x0, x9\n   add x11, x11, #1\n   b 46b\n"
        /* fill col_u[0..all-1] at [sp] */
        "48: mov w4, #0\n"
        "49: cmp w4, w24\n   b.ge 50f\n   asr x12, x0, #16\n   sub x12, x12, x25\n   add x0, x0, x2\n"
        "cmp x12, x6\n   lsr x13, x12, #8\n   mov w14, #-1\n   csel w13, w13, w14, lo\n   strb w13, [sp, x4]\n   add w4, w4, #1\n   b 49b\n"
        /* count e_cnt(w14), f_cnt(w15) across j=0..n-1, i=0..n-1 */
        "50: mov w14, #0\n   mov w15, #0\n   mov w4, #0\n   add x16, sp, #64\n" /* j=0, dp=&down_ptr[0] */
        "51: cmp w4, w23\n   b.ge 54f\n   mov w5, #0\n   add x17, sp, x4\n" /* i=0, up=&col_u[j] */
        "52: cmp w5, w23\n   b.ge 53f\n   ldr x12, [x16], #8\n   ldrsb w13, [x17]\n   add x17, x17, x23\n"
        "cbz x12, 521f\n   tbnz w13, #31, 521f\n   ldrb w12, [x12, x13]\n"
        "cmp w12, #35\n   cinc w14, w14, eq\n   cmp w12, #111\n   cinc w15, w15, eq\n"
        "521: add w5, w5, #1\n   b 52b\n"
        "53: add w4, w4, #1\n   b 51b\n"
        "54: add w12, w24, #1\n   mul w12, w14, w12\n   add w12, w12, w15\n   lsl w12, w12, #1\n"
        "add x13, sp, #192\n   ldrb w14, [x13, x12]\n   add w12, w12, #1\n   ldrb w15, [x13, x12]\n"
        "strb w14, [x21, x11]\n   strb w15, [x22, x11]\n   add x11, x11, #1\n   b 46b\n"
        "55: ldrsw x12, [x19, #60]\n   add x21, x21, x12\n   add x22, x22, x12\n   add w28, w28, #1\n   b 40b\n"

        /* epilogue + optional tail-call to prast_pointer_ring */
        "80: ldr w1, [x20, #64]\n   mov x0, x19\n   add sp, sp, #832\n"
        "ldr x30, [sp, #80]\n   ldp x27, x28, [sp, #64]\n   ldp x25, x26, [sp, #48]\n"
        "ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #96\n"
        "cbz w1, 99f\n   b " PRAST_SYM(prast_pointer_ring) "\n"
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_pointer_rows)
);

/* =========================================================================
 * x86-64 (SysV ELF / Mach-O / Windows x64 COFF) — Intel syntax
 * ========================================================================= */
#elif defined(PRAST_X64)

#if defined(_WIN32)
# define PRAST_X64_A1_R  "rcx"
# define PRAST_X64_A1_D  "ecx"
# define PRAST_X64_A2_R  "rdx"
# define PRAST_X64_A2_D  "edx"
# define PRAST_X64_A3_R  "r8"
# define PRAST_X64_A3_D  "r8d"
# define PRAST_X64_A4_D  "r9d"
# define PRAST_X64_SAVE  "push rbx\n   push rbp\n   push rsi\n   push rdi\n   push r12\n   push r13\n   push r14\n   push r15\n"
# define PRAST_X64_REST  "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbp\n   pop rbx\n"
#else
# define PRAST_X64_A1_R  "rdi"
# define PRAST_X64_A1_D  "edi"
# define PRAST_X64_A2_R  "rsi"
# define PRAST_X64_A2_D  "esi"
# define PRAST_X64_A3_R  "rdx"
# define PRAST_X64_A3_D  "edx"
# define PRAST_X64_A4_D  "ecx"
# define PRAST_X64_SAVE  "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
# define PRAST_X64_REST  "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#endif

__asm__(
        PRAST_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* internal isqrt(rax=n) -> rax=floor(sqrt(n)); clobbers rax, rcx, rdx, r11 */
        PRAST_HELPER(prast_isqrt)
        "test rax, rax\n   jz 2f\n   mov r11, rax\n   bsr rcx, rax\n   shr rcx, 1\n   inc rcx\n   mov eax, 1\n   shl rax, cl\n   mov rcx, rax\n"
        "1:  mov rax, r11\n   xor edx, edx\n   div rcx\n   add rax, rcx\n   shr rax, 1\n   cmp rax, rcx\n   jae 3f\n   mov rcx, rax\n   jmp 1b\n"
        "3:  mov rax, rcx\n"
        "2:  " PRAST_ASM_RET
        PRAST_HELPER_END(prast_isqrt)

        /* prast_counts(sum, more) -> clamped i32 */
        PRAST_ASM_FUNC(prast_counts)
        "movsxd rax, " PRAST_X64_A1_D "\n   add rax, " PRAST_X64_A2_R "\n   mov rcx, 16777216\n   mov rdx, -16777216\n"
        "cmp rax, rcx\n   cmovg rax, rcx\n   cmp rax, rdx\n   cmovl rax, rdx\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_counts)

        /* prast_gain_wanted(gain, flat, percent) -> i32 */
        PRAST_ASM_FUNC(prast_gain_wanted)
        "mov eax, " PRAST_X64_A1_D "\n   mov r9d, 1024\n   test " PRAST_X64_A2_D ", " PRAST_X64_A2_D "\n   cmovnz eax, r9d\n"
        "movsxd rax, eax\n   mov r9d, " PRAST_X64_A3_D "\n   test r9d, r9d\n   jz 1f\n   cmp r9d, 100\n   je 1f\n"
        "movsxd r9, r9d\n   imul rax, r9\n   cqo\n   mov r10d, 100\n   idiv r10\n"
        "1:  " PRAST_ASM_RET
        PRAST_ASM_END(prast_gain_wanted)

        /* prast_tablet(value, min, max, size) -> i32 */
        PRAST_ASM_FUNC(prast_tablet)
        "movsxd rax, " PRAST_X64_A1_D "\n   movsxd r10, " PRAST_X64_A2_D "\n   movsxd r11, " PRAST_X64_A3_D "\n   movsxd r9, " PRAST_X64_A4_D "\n"
        "mov r8, r11\n   sub r8, r10\n   jle 1f\n   cmp rax, r10\n   cmovl rax, r10\n   cmp rax, r11\n   cmovg rax, r11\n"
        "sub rax, r10\n   imul rax, r9\n   cqo\n   idiv r8\n" PRAST_ASM_RET
        "1:  xor eax, eax\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_tablet)

        /* prast_shake(shake, delta, now) -> 0/1 */
        PRAST_ASM_FUNC(prast_shake)
        "mov r10, " PRAST_X64_A1_R "\n   mov r11d, " PRAST_X64_A2_D "\n   mov r9, " PRAST_X64_A3_R "\n"
        "test r10, r10\n   jz 9f\n   mov eax, [r10 + 16]\n   mov ecx, 6\n   mov edx, 5\n"
        "cmp eax, 1\n   jne 1f\n   mov ecx, 9\n   mov edx, 7\n   jmp 2f\n"
        "1:  cmp eax, 2\n   jne 2f\n   mov ecx, 4\n   mov edx, 4\n"
        "2:  mov eax, ecx\n   neg eax\n   cmp r11d, eax\n   jle 3f\n   cmp r11d, ecx\n   jl 9f\n"
        "3:  mov ecx, 1\n   mov eax, -1\n   test r11d, r11d\n   cmovle ecx, eax\n"
        "mov rax, r9\n   sub rax, [r10]\n   cmp rax, 700000000\n   jbe 4f\n   mov [r10], r9\n   mov dword ptr [r10 + 12], 0\n"
        "4:  cmp ecx, [r10 + 8]\n   je 9f\n   mov [r10 + 8], ecx\n   mov eax, [r10 + 12]\n   inc eax\n"
        "cmp eax, edx\n   jl 5f\n   mov dword ptr [r10 + 12], 0\n   mov eax, 1\n" PRAST_ASM_RET
        "5:  mov [r10 + 12], eax\n"
        "9:  xor eax, eax\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_shake)

        /* prast_edges_at(arg) -> edge bitmask */
        PRAST_ASM_FUNC(prast_edges_at)
        "mov r10, " PRAST_X64_A1_R "\n   test r10, r10\n   jz 9f\n"
        "movsxd rcx, dword ptr [r10]\n   movsxd rdx, dword ptr [r10 + 4]\n"
        "movsxd r8, dword ptr [r10 + 8]\n   movsxd r9, dword ptr [r10 + 12]\n"
        "movsxd r11, dword ptr [r10 + 24]\n" /* grip */
        "mov rax, r8\n   sub rax, r11\n   cmp rcx, rax\n   jl 9f\n"
        "movsxd rax, dword ptr [r10 + 16]\n   add rax, r11\n   cmp rcx, rax\n   jge 9f\n"
        "mov rax, r9\n   sub rax, r11\n   cmp rdx, rax\n   jl 9f\n"
        "movsxd rax, dword ptr [r10 + 20]\n   add rax, r11\n   cmp rdx, rax\n   jge 9f\n"
        "xor eax, eax\n   movsxd r11, dword ptr [r10 + 28]\n" /* in */
        "push r12\n   lea r12, [r8 + r11]\n   cmp rcx, r12\n   jge 1f\n   or eax, 1\n   jmp 2f\n"
        "1:  movsxd r12, dword ptr [r10 + 16]\n   sub r12, r11\n   cmp rcx, r12\n   jl 2f\n   or eax, 2\n"
        "2:  movsxd r12, dword ptr [r10 + 32]\n   add r12, r9\n   cmp rdx, r12\n   jge 3f\n   or eax, 4\n   jmp 4f\n"
        "3:  movsxd r12, dword ptr [r10 + 20]\n   sub r12, r11\n   cmp rdx, r12\n   jl 4f\n   or eax, 8\n"
        "4:  movsxd r11, dword ptr [r10 + 36]\n   test r11, r11\n   jz 8f\n"
        "test eax, 3\n   jz 5f\n   test eax, 12\n   jnz 8f\n"
        "lea r12, [r9 + r11]\n   cmp rdx, r12\n   jge 6f\n   or eax, 4\n   jmp 8f\n"
        "6:  movsxd r12, dword ptr [r10 + 20]\n   sub r12, r11\n   cmp rdx, r12\n   jl 8f\n   or eax, 8\n   jmp 8f\n"
        "5:  test eax, 12\n   jz 8f\n   lea r12, [r8 + r11]\n   cmp rcx, r12\n   jge 7f\n   or eax, 1\n   jmp 8f\n"
        "7:  movsxd r12, dword ptr [r10 + 16]\n   sub r12, r11\n   cmp rcx, r12\n   jl 8f\n   or eax, 2\n"
        "8:  pop r12\n" PRAST_ASM_RET
        "9:  xor eax, eax\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_edges_at)

        /* prast_dyn_cover(p, pre, u, v, fill, edge) */
        PRAST_ASM_FUNC(prast_dyn_cover)
        PRAST_X64_SAVE
#if defined(_WIN32)
        /* After 8 pushes (64 bytes) + retaddr (8 bytes): arg5=[rsp+104], arg6=[rsp+112] */
        "mov r12, rcx\n   mov r13, rdx\n   mov r14, r8\n   mov r15, r9\n"
        "mov rbx, [rsp + 104]\n   mov rbp, [rsp + 112]\n"
#else
        "mov r12, rdi\n   mov r13, rsi\n   mov r14, rdx\n   mov r15, rcx\n   mov rbx, r8\n   mov rbp, r9\n"
#endif
        "test rbx, rbx\n   jz 98f\n   test rbp, rbp\n   jz 98f\n"
        "mov byte ptr [rbx], 0\n   mov byte ptr [rbp], 0\n"
        "test r12, r12\n   jz 98f\n   test r13, r13\n   jz 98f\n"
        "mov rax, [r13 + 40]\n   cmp r14, rax\n   jg 98f\n   cmp r15, rax\n   jg 98f\n"
        "neg rax\n   cmp r14, rax\n   jl 98f\n   cmp r15, rax\n   jl 98f\n"
        "cmp dword ptr [r12 + 8], 0\n   jle 98f\n"
        "cmp dword ptr [r12 + 36], 0\n   je 12f\n"
        /* lens path */
        "movsxd r8, dword ptr [r12 + 24]\n   movsxd r9, dword ptr [r12 + 28]\n"
        "mov rax, r14\n   imul rax, r8\n   mov rcx, r15\n   imul rcx, r9\n   add rax, rcx\n"
        "lea rcx, [rax + 63]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 6\n   mov r10, rax\n"
        "mov rax, r15\n   imul rax, r8\n   mov rcx, r14\n   imul rcx, r9\n   sub rax, rcx\n"
        "lea rcx, [rax + 63]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 6\n   mov r9, rax\n"
        "mov rax, r10\n   neg rax\n   cmovl rax, r10\n   mov r10, rax\n" /* la = |la| */
        "mov rax, r9\n   neg rax\n   cmovl rax, r9\n   mov r9, rax\n"   /* b = |b| */
        "movsxd r8, dword ptr [r12 + 16]\n   mov r14, [r13 + 48]\n" /* r, tip */
        "cmp r14, r8\n   je 10f\n   test r14, r14\n   jz 98f\n   mov r15, [r13 + 56]\n" /* leg */
        "mov rax, r10\n   imul rax, r15\n   mov rcx, r9\n   imul rcx, r8\n   cmp rax, rcx\n   jle 10f\n"
        "mov rdx, r10\n   sub rdx, r14\n   mov rax, rdx\n   imul rax, r15\n   cmp rax, rcx\n   jge 11f\n"
        "mov rax, r10\n   imul rax, r8\n   mov rcx, r9\n   imul rcx, r15\n   add rax, rcx\n"
        "cqo\n   idiv r14\n   sub rax, r8\n   jmp 30f\n"
        "10: mov rax, r10\n   imul rax, rax\n   imul r9, r9\n   add rax, r9\n   call " PRAST_HREF(prast_isqrt) "\n   sub rax, r8\n   jmp 30f\n"
        "11: mov rax, rdx\n   imul rax, rax\n   imul r9, r9\n   add rax, r9\n   call " PRAST_HREF(prast_isqrt) "\n   jmp 30f\n"
        /* rounded-rect / stretch path */
        "12: cmp dword ptr [r12 + 32], 0\n   je 13f\n   mov r10, [r13]\n   test r10, r10\n   jz 98f\n"
        "movsxd r8, dword ptr [r12 + 24]\n   movsxd r9, dword ptr [r12 + 28]\n"
        "mov rax, r14\n   imul rax, r8\n   mov rcx, r15\n   imul rcx, r9\n   add rax, rcx\n"
        "lea rcx, [rax + 63]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 6\n   shl rax, 8\n   cqo\n   idiv r10\n   mov r11, rax\n"
        "mov rax, r15\n   imul rax, r8\n   mov rcx, r14\n   imul rcx, r9\n   sub rax, rcx\n"
        "lea rcx, [rax + 63]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 6\n   imul rax, r10\n"
        "lea rcx, [rax + 255]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 8\n   mov r10, rax\n"
        "mov rax, r11\n   imul rax, r8\n   mov rcx, r10\n   imul rcx, r9\n   sub rax, rcx\n"
        "lea rcx, [rax + 63]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 6\n   mov r14, rax\n"
        "mov rax, r11\n   imul rax, r9\n   mov rcx, r10\n   imul rcx, r8\n   add rax, rcx\n"
        "lea rcx, [rax + 63]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 6\n   mov r15, rax\n"
        "13: movsxd r8, dword ptr [r12 + 20]\n" /* c */
        "mov rax, r14\n   neg rax\n   cmovl rax, r14\n   movsxd rcx, dword ptr [r12 + 12]\n   sub rcx, r8\n   sub rax, rcx\n   mov r9, rax\n" /* x */
        "mov rax, r15\n   neg rax\n   cmovl rax, r15\n   movsxd rcx, dword ptr [r12 + 16]\n   sub rcx, r8\n   sub rax, rcx\n   mov r10, rax\n" /* y */
        "mov eax, 64\n   xor edx, edx\n   test r9, r9\n   jle 20f\n   test r10, r10\n   jle 20f\n"
        "mov rax, r9\n   imul rax, rax\n   mov rcx, r10\n   imul rcx, rcx\n   add rax, rcx\n" /* square */
        "mov rdx, [r13 + 16]\n   lea rcx, [r8 + rdx]\n   imul rcx, rcx\n   cmp rax, rcx\n   jge 98f\n"
        "mov rcx, [r13 + 32]\n   imul rcx, [r13]\n   lea r11, [rcx + 255]\n   test rcx, rcx\n   cmovs rcx, r11\n   sar rcx, 8\n"
        "mov r11, r8\n   sub r11, rcx\n   sub r11, rdx\n   test r11, r11\n   jle 19f\n"
        "imul r11, r11\n   cmp rax, r11\n   jg 19f\n   mov byte ptr [rbx], 255\n   jmp 98f\n"
        "19: call " PRAST_HREF(prast_isqrt) "\n   mov r11, rax\n"
        "mov rax, r9\n   shl rax, 6\n   cqo\n   idiv r11\n   mov r9, rax\n" /* nx */
        "mov rax, r10\n   shl rax, 6\n   cqo\n   idiv r11\n   mov r10, rax\n" /* ny */
        "mov rax, r11\n   sub rax, r8\n   jmp 22f\n"
        "20: cmp r9, r10\n   jle 21f\n   mov rax, r9\n   sub rax, r8\n   mov r9d, 64\n   xor r10d, r10d\n   jmp 22f\n"
        "21: mov rax, r10\n   sub rax, r8\n   xor r9d, r9d\n   mov r10d, 64\n"
        "22: cmp dword ptr [r12 + 32], 0\n   je 30f\n   mov r8, rax\n" /* save away in r8 */
        "mov rax, r9\n   neg rax\n   test r14, r14\n   cmovs r9, rax\n"
        "mov rax, r10\n   neg rax\n   test r15, r15\n   cmovs r10, rax\n"
        "mov r14, [r13]\n   test r14, r14\n   jz 98f\n"
        "movsxd rcx, dword ptr [r12 + 24]\n   movsxd r15, dword ptr [r12 + 28]\n"
        "mov rax, r9\n   imul rax, rcx\n   mov rdx, r10\n   imul rdx, r15\n   add rax, rdx\n"
        "lea rdx, [rax + 63]\n   test rax, rax\n   cmovs rax, rdx\n   sar rax, 6\n   shl rax, 8\n   cqo\n   idiv r14\n   mov r11, rax\n"
        "mov rax, r10\n   imul rax, rcx\n   mov rdx, r9\n   imul rdx, r15\n   sub rax, rdx\n"
        "lea rdx, [rax + 63]\n   test rax, rax\n   cmovs rax, rdx\n   sar rax, 6\n   imul rax, r14\n"
        "lea rdx, [rax + 255]\n   test rax, rax\n   cmovs rax, rdx\n   sar rax, 8\n"
        "imul rax, rax\n   imul r11, r11\n   add rax, r11\n   call " PRAST_HREF(prast_isqrt) "\n"
        "mov ecx, 1\n   test rax, rax\n   cmovz rax, rcx\n   mov rcx, rax\n   mov rax, r8\n   shl rax, 6\n   cqo\n   idiv rcx\n"
        /* lined: away in rax */
        "30: movsxd rcx, dword ptr [r12 + 8]\n   imul rax, rcx\n   lea rcx, [rax + 255]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 8\n"
        "mov r8d, 128\n   sub r8, rax\n   xor ecx, ecx\n   mov edx, 256\n   test r8, r8\n   cmovs r8, rcx\n   cmp r8, rdx\n   cmovg r8, rdx\n"
        "mov r9d, 128\n   sub r9, rax\n   sub r9, [r13 + 24]\n   test r9, r9\n   cmovs r9, rcx\n   cmp r9, rdx\n   cmovg r9, rdx\n"
        "mov r10, r8\n   sub r10, r9\n   mov rax, r10\n   imul rax, 255\n   shr rax, 8\n   mov [rbp], al\n"
        "test r9, r9\n   jz 98f\n   mov rcx, 256\n   sub rcx, r10\n   mov rax, r9\n   imul rax, 255\n   xor edx, edx\n   div rcx\n   mov [rbx], al\n"
        "98: " PRAST_X64_REST PRAST_ASM_RET
        PRAST_ASM_END(prast_dyn_cover)

        /* prast_pointer_ring(arg) */
        PRAST_ASM_FUNC(prast_pointer_ring)
        "mov r10, " PRAST_X64_A1_R "\n   test r10, r10\n   jz 99f\n   mov r11, [r10]\n   test r11, r11\n   jz 99f\n"
        "cmp qword ptr [r10 + 24], 0\n   je 99f\n   cmp qword ptr [r10 + 32], 0\n   je 99f\n"
        "cmp dword ptr [r10 + 40], 0\n   jle 99f\n   cmp dword ptr [r10 + 56], 0\n   jle 99f\n"
        "cmp dword ptr [r11 + 8], 0\n   jle 99f\n"
        PRAST_X64_SAVE
        /* stack scratch (96 bytes): [rsp+0]=cx, [8]=cy, [16]=r, [24]=core, [32]=turn, [40]=inner, [48]=outer, [56]=end_row, [64]=pitch, [72]=dy, [80]=dy2 */
        "sub rsp, 96\n   mov rbp, r10\n   mov rbx, r11\n"
        "movsxd r8, dword ptr [rbx + 8]\n   mov eax, 256\n   cmp r8, rax\n   cmovg rax, r8\n"
        "lea rax, [rax + rax * 2]\n   shr rax, 2\n   mov [rsp + 24], rax\n" /* core */
        "movsxd rcx, dword ptr [rbx + 64]\n   shl rcx, 7\n   mov [rsp + 32], rcx\n" /* turn */
        "movsxd r9, dword ptr [rbp + 44]\n   shl r9, 8\n   movsxd r10, dword ptr [rbp + 48]\n   shl r10, 8\n"
        "cmp dword ptr [rbx], 0\n   je 1f\n   imul r11, r8, 12\n   jmp 2f\n"
        "1:  mov rcx, [rbp + 16]\n   test rcx, rcx\n   jz 90f\n   mov edx, [rbx + 4]\n   cmp edx, 11\n   jae 90f\n"
        "movsx r12, byte ptr [rcx + rdx * 2]\n   movsx r13, byte ptr [rcx + rdx * 2 + 1]\n"
        "mov eax, 12\n   sub rax, r12\n   imul rax, r8\n   add r9, rax\n"
        "mov eax, 12\n   sub rax, r13\n   imul rax, r8\n   add r10, rax\n"
        "imul r11, r8, 19\n   shr r11, 1\n"
        "2:  mov [rsp], r9\n   mov [rsp + 8], r10\n   mov [rsp + 16], r11\n"
        "mov rax, [rsp + 24]\n   add rax, 512\n   mov rcx, r11\n   sub rcx, rax\n   xor edx, edx\n   cmp r11, rax\n   cmovle rcx, rdx\n"
        "imul rcx, rcx\n   mov [rsp + 40], rcx\n   lea rcx, [r11 + rax]\n   imul rcx, rcx\n   mov [rsp + 48], rcx\n"
        "mov r12, [rbp + 24]\n   mov r13, [rbp + 32]\n   mov r14d, [rbp + 52]\n"
        "mov eax, r14d\n   add eax, [rbp + 56]\n   mov [rsp + 56], eax\n   movsxd rax, dword ptr [rbp + 60]\n   mov [rsp + 64], rax\n"
        "3:  cmp r14d, [rsp + 56]\n   jge 90f\n   movsxd rax, r14d\n   shl rax, 8\n   add rax, 128\n   sub rax, [rsp + 8]\n"
        "mov [rsp + 72], rax\n   imul rax, rax\n   mov [rsp + 80], rax\n   cmp rax, [rsp + 48]\n   jg 8f\n"
        "mov r15, 128\n   sub r15, [rsp]\n   xor r8d, r8d\n" /* dx, col */
        "4:  cmp r8d, [rbp + 40]\n   jge 8f\n   mov rax, r15\n   imul rax, rax\n   add rax, [rsp + 80]\n"
        "cmp rax, [rsp + 48]\n   jg 7f\n   cmp rax, [rsp + 40]\n   jl 7f\n   mov r9, [rsp + 72]\n   neg r9\n" /* y = -dy */
        "test r15, r15\n   js 5f\n   test r9, r9\n   jle 5f\n   xor r10d, r10d\n   mov rcx, r15\n   mov rdx, r9\n   jmp 6f\n"
        "5:  test r15, r15\n   jle 10f\n   mov r10d, 1024\n   mov rcx, r9\n   neg rcx\n   mov rdx, r15\n   jmp 6f\n"
        "10: test r9, r9\n   jge 11f\n   mov r10d, 2048\n   mov rcx, r15\n   neg rcx\n   mov rdx, r9\n   neg rdx\n   jmp 6f\n"
        "11: mov r10d, 3072\n   mov rcx, r9\n   mov rdx, r15\n   neg rdx\n"
        "6:  lea r11, [rcx + rdx]\n   mov edx, 1\n   test r11, r11\n   cmovz r11, rdx\n"
        "mov r9, rax\n   mov rax, rcx\n   shl rax, 10\n   cqo\n   idiv r11\n   add rax, r10\n   cmp rax, [rsp + 32]\n   jge 7f\n"
        "mov rax, r9\n   call " PRAST_HREF(prast_isqrt) "\n   sub rax, [rsp + 16]\n   mov rcx, rax\n   neg rcx\n   cmovl rcx, rax\n"
        "mov r9, [rsp + 24]\n   lea rax, [r9 + 128]\n   sub rax, rcx\n   xor edx, edx\n   mov r10d, 256\n"
        "test rax, rax\n   cmovs rax, rdx\n   cmp rax, r10\n   cmovg rax, r10\n"
        "lea r11, [r9 + 384]\n   sub r11, rcx\n   test r11, r11\n   cmovs r11, rdx\n   cmp r11, r10\n   cmovg r11, r10\n   sub r11, rax\n"
        "imul rax, 255\n   shr rax, 8\n   imul r11, 255\n   shr r11, 8\n"
        "movzx edx, byte ptr [r12 + r8]\n   cmp edx, eax\n   cmovb edx, eax\n   mov [r12 + r8], dl\n"
        "movzx edx, byte ptr [r13 + r8]\n   cmp edx, r11d\n   cmovb edx, r11d\n   mov [r13 + r8], dl\n"
        "7:  add r15, 256\n   inc r8\n   jmp 4b\n"
        "8:  inc r14d\n   add r12, [rsp + 64]\n   add r13, [rsp + 64]\n   jmp 3b\n"
        "90: add rsp, 96\n" PRAST_X64_REST
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_pointer_ring)

        /* prast_pointer_rows(arg)
           Stack frame (944 bytes; aligned so calls have 16-byte aligned rsp):
             [rsp+0..47]    : Win64 shadow/arg5/arg6 (48 bytes)
             [rsp+48..111]  : prast_dyn_pre (64 bytes) OR col_u[16] (+48..63) + locals (+64..111)
             [rsp+112..239] : down_ptr[16] (128 bytes)
             [rsp+240..817] : share[289][2] (578 bytes)
             [rsp+824..935] : locals (step, step2, ox, oy, v, uq, span_step, col_step, shape_art, n, all, end_row, col)
        */
        PRAST_ASM_FUNC(prast_pointer_rows)
        "mov r10, " PRAST_X64_A1_R "\n   test r10, r10\n   jz 99f\n   mov r11, [r10]\n   test r11, r11\n   jz 99f\n"
        "cmp qword ptr [r10 + 24], 0\n   je 99f\n   cmp qword ptr [r10 + 32], 0\n   je 99f\n"
        "cmp dword ptr [r10 + 40], 0\n   jle 99f\n   cmp dword ptr [r10 + 56], 0\n   jle 99f\n"
        "cmp dword ptr [r11 + 8], 0\n   jle 99f\n"
        PRAST_X64_SAVE
        "sub rsp, 936\n   mov rbp, r10\n   mov rbx, r11\n   mov r12, [rbp + 24]\n   mov r13, [rbp + 32]\n"
        "movsxd r14, dword ptr [rbx + 8]\n" /* scale */
        "cmp dword ptr [rbx], 0\n   je 30f\n"
        /* ---- DYNAMIC CURSOR PATH ---- */
        "movsxd r8, dword ptr [rbx + 32]\n   add r8, 256\n   mov [rsp + 48], r8\n" /* pre.a */
        "mov eax, 32768\n   cqo\n   idiv r14\n   inc rax\n   mov [rsp + 56], rax\n   mov r9, rax\n" /* pre.half */
        "imul rax, r8\n   lea rcx, [rax + 255]\n   test rax, rax\n   cmovs rax, rcx\n   sar rax, 8\n   inc rax\n   mov [rsp + 64], rax\n"
        "mov eax, 256\n   cmp r14, rax\n   cmovg rax, r14\n   mov [rsp + 72], rax\n" /* pre.line */
        "shl rax, 8\n   cqo\n   idiv r14\n   mov [rsp + 80], rax\n" /* pre.lined */
        "movsxd rax, dword ptr [rbx + 12]\n   movsxd rcx, dword ptr [rbx + 16]\n   lea r10, [rax + rcx]\n   imul r10, r8\n"
        "lea rdx, [r10 + 255]\n   test r10, r10\n   cmovs r10, rdx\n   sar r10, 8\n   add r10, r9\n   mov [rsp + 88], r10\n" /* pre.bound */
        "mov r8, rax\n   cmp r8, rcx\n   cmovl r8, rcx\n   mov [rsp + 96], r8\n" /* pre.tip */
        "xor eax, eax\n   cmp dword ptr [rbx + 36], 0\n   je 1f\n   cmp r8, rcx\n   jle 1f\n"
        "mov rax, r8\n   imul rax, rax\n   imul rcx, rcx\n   sub rax, rcx\n   call " PRAST_HREF(prast_isqrt) "\n"
        "1:  mov [rsp + 104], rax\n" /* pre.leg */
        "mov rax, 0x80000000\n   cqo\n   idiv r14\n   mov [rsp + 824], rax\n   shl rax, 1\n   mov [rsp + 832], rax\n" /* step, step2 */
        "movsxd rax, dword ptr [rbp + 44]\n   shl rax, 16\n   cqo\n   idiv r14\n   add rax, 128\n   mov [rsp + 840], rax\n" /* ox */
        "movsxd rax, dword ptr [rbp + 48]\n   shl rax, 16\n   cqo\n   idiv r14\n   add rax, 128\n   mov [rsp + 848], rax\n" /* oy */
        "mov r15d, [rbp + 52]\n   mov eax, r15d\n   add eax, [rbp + 56]\n   mov [rsp + 912], eax\n" /* row, end_row */
        "2:  cmp r15d, [rsp + 912]\n   jge 80f\n   movsxd rax, r15d\n   lea rax, [rax + rax + 1]\n   imul rax, [rsp + 824]\n"
        "sar rax, 16\n   sub rax, [rsp + 848]\n   mov rcx, [rsp + 88]\n   cmp rax, rcx\n   jg 3f\n   neg rcx\n   cmp rax, rcx\n   jge 5f\n"
        "3:  mov ecx, [rbp + 40]\n   xor edx, edx\n"
        "4:  mov byte ptr [r12 + rdx], 0\n   mov byte ptr [r13 + rdx], 0\n   inc rdx\n   cmp edx, ecx\n   jl 4b\n   jmp 9f\n"
        "5:  mov [rsp + 856], rax\n   mov rax, [rsp + 824]\n   mov [rsp + 864], rax\n   xor r14d, r14d\n" /* v, uq, col */
        "6:  cmp r14d, [rbp + 40]\n   jge 9f\n   mov rax, [rsp + 864]\n   mov rcx, rax\n   add rcx, [rsp + 832]\n   mov [rsp + 864], rcx\n"
        "sar rax, 16\n   sub rax, [rsp + 840]\n   mov rcx, [rsp + 88]\n   cmp rax, rcx\n   jg 7f\n   neg rcx\n   cmp rax, rcx\n   jl 7f\n"
#if defined(_WIN32)
        "mov rcx, rbx\n   lea rdx, [rsp + 48]\n   mov r8, rax\n   mov r9, [rsp + 856]\n"
        "lea rax, [r12 + r14]\n   mov [rsp + 32], rax\n   lea rax, [r13 + r14]\n   mov [rsp + 40], rax\n"
#else
        "mov rdi, rbx\n   lea rsi, [rsp + 48]\n   mov rdx, rax\n   mov rcx, [rsp + 856]\n"
        "lea r8, [r12 + r14]\n   lea r9, [r13 + r14]\n"
#endif
        "call " PRAST_SYM(prast_dyn_cover) "\n   inc r14\n   jmp 6b\n"
        "7:  mov byte ptr [r12 + r14], 0\n   mov byte ptr [r13 + r14], 0\n   inc r14\n   jmp 6b\n"
        "9:  movsxd rax, dword ptr [rbp + 60]\n   add r12, rax\n   add r13, rax\n   inc r15d\n   jmp 2b\n"

        /* ---- BITMAP SUPERSAMPLE PATH ---- */
        "30: cmp qword ptr [rbp + 8], 0\n   je 31f\n   cmp qword ptr [rbp + 16], 0\n   je 31f\n"
        "cmp dword ptr [rbx + 4], 11\n   jb 33f\n"
        "31: mov ecx, [rbp + 40]\n   mov r8d, [rbp + 56]\n   movsxd r9, dword ptr [rbp + 60]\n"
        "32: xor edx, edx\n"
        "321: mov byte ptr [r12 + rdx], 0\n   mov byte ptr [r13 + rdx], 0\n   inc rdx\n   cmp edx, ecx\n   jl 321b\n"
        "add r12, r9\n   add r13, r9\n   dec r8d\n   jg 32b\n   jmp 80f\n"
        "33: mov eax, 4\n   mov ecx, 2\n   cmp r14, 1024\n   cmovge eax, ecx\n   mov [rsp + 904], eax\n" /* n */
        "imul eax, eax\n   mov [rsp + 908], eax\n   mov r15d, eax\n" /* all */
        /* build share[e*(all+1)+f][2] at [rsp+240] */
        "mov r8, r15\n   imul r8, r14\n   lea r9d, [r15d + 1]\n   xor r10d, r10d\n" /* denom=all*scale, stride=all+1, e=0 */
        "34: cmp r10d, r15d\n   jg 39f\n   xor r11d, r11d\n" /* f=0 */
        "35: lea eax, [r10d + r11d]\n   cmp eax, r15d\n   jg 38f\n   cmp r14, 256\n   jl 36f\n"
        "mov eax, r10d\n   imul eax, 255\n   xor edx, edx\n   div r15d\n   mov ecx, eax\n" /* lined */
        "xor eax, eax\n   test r11d, r11d\n   jz 37f\n   mov eax, r11d\n   imul eax, 255\n   mov esi, r15d\n   sub esi, r10d\n   xor edx, edx\n   div esi\n   jmp 37f\n"
        "36: mov rax, r10\n   imul rax, 65280\n   xor edx, edx\n   div r8\n   mov esi, 255\n   cmp rax, rsi\n   cmovg rax, rsi\n   mov rcx, rax\n"
        "mov rax, r11\n   imul rax, 65280\n   xor edx, edx\n   div r8\n   sub rsi, rcx\n   cmp rax, rsi\n   cmovg rax, rsi\n"
        "test rax, rax\n   jz 37f\n   imul rax, 255\n   xor edx, edx\n   div rsi\n"
        "37: mov edx, r10d\n   imul edx, r9d\n   add edx, r11d\n   mov [rsp + 240 + rdx * 2], al\n   mov [rsp + 241 + rdx * 2], cl\n"
        "inc r11d\n   jmp 35b\n"
        "38: inc r10d\n   jmp 34b\n"
        /* step, step2, ox, oy, shape_art */
        "39: mov rax, 0x80000000\n   cqo\n   idiv r8\n   mov [rsp + 824], rax\n   shl rax, 1\n   mov [rsp + 832], rax\n"
        "lea rcx, [r15 - 1]\n   imul rcx, rax\n   mov [rsp + 872], rcx\n   imul rax, r15\n   mov [rsp + 880], rax\n" /* span_step, col_step */
        "mov eax, [rbx + 4]\n   imul rcx, rax, 600\n   add rcx, [rbp + 8]\n   mov [rsp + 888], rcx\n" /* shape_art */
        "mov rdx, [rbp + 16]\n   movsx r8, byte ptr [rdx + rax * 2]\n   movsx r9, byte ptr [rdx + rax * 2 + 1]\n"
        "movsxd rax, dword ptr [rbp + 44]\n   shl rax, 16\n   cqo\n   idiv r14\n   shl r8, 8\n   sub rax, r8\n   mov [rsp + 840], rax\n"
        "movsxd rax, dword ptr [rbp + 48]\n   shl rax, 16\n   cqo\n   idiv r14\n   shl r9, 8\n   sub rax, r9\n   mov [rsp + 848], rax\n"
        "mov r14d, [rbp + 52]\n   mov eax, r14d\n   add eax, [rbp + 56]\n   mov [rsp + 912], eax\n" /* row, end_row */
        "40: cmp r14d, [rsp + 912]\n   jge 80f\n"
        /* build down_ptr[0..all-1] at [rsp+112] */
        "movsxd rax, r14d\n   imul rax, r15\n   lea rax, [rax + rax + 1]\n   imul rax, [rsp + 824]\n"
        "mov rcx, [rsp + 832]\n   mov rdx, [rsp + 848]\n   mov r8, [rsp + 888]\n   xor r9d, r9d\n   xor r10d, r10d\n"
        "41: cmp r9d, r15d\n   jge 43f\n   mov r11, rax\n   sar r11, 16\n   sub r11, rdx\n   add rax, rcx\n"
        "cmp r11, 6144\n   jae 42f\n   shr r11, 8\n   imul r11, 25\n   add r11, r8\n   mov [rsp + 112 + r9 * 8], r11\n   mov r10d, 1\n   inc r9\n   jmp 41b\n"
        "42: mov qword ptr [rsp + 112 + r9 * 8], 0\n   inc r9\n   jmp 41b\n"
        "43: test r10d, r10d\n   jnz 45f\n   mov ecx, [rbp + 40]\n   xor edx, edx\n"
        "44: mov byte ptr [r12 + rdx], 0\n   mov byte ptr [r13 + rdx], 0\n   inc rdx\n   cmp edx, ecx\n   jl 44b\n   jmp 55f\n"
        /* non-empty row: col=0..span-1 */
        "45: mov rax, [rsp + 824]\n   xor r8d, r8d\n" /* uq, col */
        "46: cmp r8d, [rbp + 40]\n   jge 55f\n   mov rcx, rax\n   sar rcx, 16\n   sub rcx, [rsp + 840]\n"
        "mov rdx, rax\n   add rdx, [rsp + 872]\n   sar rdx, 16\n   sub rdx, [rsp + 840]\n"
        "test rdx, rdx\n   js 47f\n   cmp rcx, 6144\n   jl 48f\n"
        "47: mov byte ptr [r12 + r8], 0\n   mov byte ptr [r13 + r8], 0\n   add rax, [rsp + 880]\n   inc r8\n   jmp 46b\n"
        "48: mov rdx, [rsp + 832]\n   mov r9, [rsp + 840]\n   xor r10d, r10d\n"
        "49: cmp r10d, r15d\n   jge 50f\n   mov rcx, rax\n   sar rcx, 16\n   sub rcx, r9\n   add rax, rdx\n"
        "mov r11d, -1\n   cmp rcx, 6144\n   jae 491f\n   shr rcx, 8\n   mov r11d, ecx\n"
        "491: mov [rsp + 48 + r10], r11b\n   inc r10\n   jmp 49b\n"
        "50: mov [rsp + 864], rax\n   xor r9d, r9d\n   xor r10d, r10d\n   xor ecx, ecx\n   lea rdx, [rsp + 112]\n   mov eax, [rsp + 904]\n" /* e, f, j, dp, n */
        "51: cmp ecx, eax\n   jge 54f\n   xor r11d, r11d\n   lea rdi, [rsp + 48 + rcx]\n"
        "52: cmp r11d, eax\n   jge 53f\n   mov rsi, [rdx]\n   add rdx, 8\n   movsx rbx, byte ptr [rdi]\n   add rdi, rax\n"
        "test rsi, rsi\n   jz 521f\n   test rbx, rbx\n   js 521f\n   movzx esi, byte ptr [rsi + rbx]\n"
        "cmp esi, 35\n   sete bl\n   movzx ebx, bl\n   add r9d, ebx\n   cmp esi, 111\n   sete bl\n   movzx ebx, bl\n   add r10d, ebx\n"
        "521: inc r11d\n   jmp 52b\n"
        "53: inc ecx\n   jmp 51b\n"
        "54: lea ecx, [r15d + 1]\n   imul r9d, ecx\n   add r9d, r10d\n"
        "mov al, [rsp + 240 + r9 * 2]\n   mov [r12 + r8], al\n   mov al, [rsp + 241 + r9 * 2]\n   mov [r13 + r8], al\n"
        "mov rax, [rsp + 864]\n   inc r8\n   jmp 46b\n"
        "55: movsxd rax, dword ptr [rbp + 60]\n   add r12, rax\n   add r13, rax\n   inc r14d\n   jmp 40b\n"

        "80: mov rbx, [rbp]\n   mov eax, [rbx + 64]\n   mov " PRAST_X64_A1_R ", rbp\n   add rsp, 936\n"
        PRAST_X64_REST
        "test eax, eax\n   jz 99f\n   jmp " PRAST_SYM(prast_pointer_ring) "\n"
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_pointer_rows)
        ".att_syntax prefix\n"
);

/* =========================================================================
 * RISC-V 64 (RV64IM — Linux ELF)
 * ========================================================================= */
#elif defined(PRAST_RISCV64)

__asm__(
        PRAST_ASM_SECTION

        /* internal isqrt(a0=n) -> a0=floor(sqrt(n)); clobbers a0, t0, t1, t2 */
        PRAST_HELPER(prast_isqrt)
        "beqz a0, 2f\n   li t0, 1\n   slli t0, t0, 62\n"
        "1:  bleu t0, a0, 3f\n   srli t0, t0, 2\n   bnez t0, 1b\n"
        "3:  li t1, 0\n"
        "4:  beqz t0, 5f\n   add t2, t1, t0\n   srli t1, t1, 1\n   bltu a0, t2, 6f\n   sub a0, a0, t2\n   add t1, t1, t0\n"
        "6:  srli t0, t0, 2\n   j 4b\n"
        "5:  mv a0, t1\n"
        "2:  " PRAST_ASM_RET
        PRAST_HELPER_END(prast_isqrt)

        /* prast_counts(a0=sum, a1=more) -> clamped i32 */
        PRAST_ASM_FUNC(prast_counts)
        "sext.w a0, a0\n   add a0, a0, a1\n   li t0, 16777216\n   neg t1, t0\n"
        "ble a0, t0, 1f\n   mv a0, t0\n"
        "1:  bge a0, t1, 2f\n   mv a0, t1\n"
        "2:  sext.w a0, a0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_counts)

        /* prast_gain_wanted(a0=gain, a1=flat, a2=percent) -> i32 */
        PRAST_ASM_FUNC(prast_gain_wanted)
        "sext.w a0, a0\n   beqz a1, 1f\n   li a0, 1024\n"
        "1:  sext.w a2, a2\n   beqz a2, 2f\n   li t0, 100\n   beq a2, t0, 2f\n   mul a0, a0, a2\n   div a0, a0, t0\n"
        "2:  sext.w a0, a0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_gain_wanted)

        /* prast_tablet(a0=value, a1=min, a2=max, a3=size) -> i32 */
        PRAST_ASM_FUNC(prast_tablet)
        "sext.w a1, a1\n   sext.w a2, a2\n   sub t0, a2, a1\n   blez t0, 3f\n   sext.w a0, a0\n"
        "bge a0, a1, 1f\n   mv a0, a1\n"
        "1:  ble a0, a2, 2f\n   mv a0, a2\n"
        "2:  sub a0, a0, a1\n   sext.w a3, a3\n   mul a0, a0, a3\n   div a0, a0, t0\n   sext.w a0, a0\n" PRAST_ASM_RET
        "3:  li a0, 0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_tablet)

        /* prast_shake(a0=shake, a1=delta, a2=now) -> 0/1 */
        PRAST_ASM_FUNC(prast_shake)
        "beqz a0, 9f\n   lw t0, 16(a0)\n   li t1, 6\n   li t2, 5\n"
        "li t3, 1\n   bne t0, t3, 1f\n   li t1, 9\n   li t2, 7\n   j 2f\n"
        "1:  li t3, 2\n   bne t0, t3, 2f\n   li t1, 4\n   li t2, 4\n"
        "2:  sext.w a1, a1\n   neg t3, t1\n   ble a1, t3, 3f\n   blt a1, t1, 9f\n"
        "3:  li t3, 1\n   bgtz a1, 4f\n   li t3, -1\n"
        "4:  ld t4, 0(a0)\n   sub t4, a2, t4\n   li t5, 700000000\n   bleu t4, t5, 5f\n   sd a2, 0(a0)\n   sw zero, 12(a0)\n"
        "5:  lw t4, 8(a0)\n   beq t3, t4, 9f\n   sw t3, 8(a0)\n   lw t4, 12(a0)\n   addi t4, t4, 1\n"
        "blt t4, t2, 6f\n   sw zero, 12(a0)\n   li a0, 1\n" PRAST_ASM_RET
        "6:  sw t4, 12(a0)\n"
        "9:  li a0, 0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_shake)

        /* prast_edges_at(a0=arg) -> edge bitmask */
        PRAST_ASM_FUNC(prast_edges_at)
        "beqz a0, 9f\n   lw a1, 0(a0)\n   lw a2, 4(a0)\n   lw a3, 8(a0)\n   lw a4, 12(a0)\n"
        "lw a5, 16(a0)\n   lw a6, 20(a0)\n   lw a7, 24(a0)\n   lw t0, 28(a0)\n   lw t1, 32(a0)\n   lw t2, 36(a0)\n"
        "sub t3, a3, a7\n   blt a1, t3, 9f\n   add t3, a5, a7\n   bge a1, t3, 9f\n"
        "sub t3, a4, a7\n   blt a2, t3, 9f\n   add t3, a6, a7\n   bge a2, t3, 9f\n"
        "li a0, 0\n   add t3, a3, t0\n   bge a1, t3, 1f\n   ori a0, a0, 1\n   j 2f\n"
        "1:  sub t3, a5, t0\n   blt a1, t3, 2f\n   ori a0, a0, 2\n"
        "2:  add t3, a4, t1\n   bge a2, t3, 3f\n   ori a0, a0, 4\n   j 4f\n"
        "3:  sub t3, a6, t0\n   blt a2, t3, 4f\n   ori a0, a0, 8\n"
        "4:  beqz t2, 8f\n   andi t3, a0, 3\n   beqz t3, 5f\n   andi t3, a0, 12\n   bnez t3, 8f\n"
        "add t3, a4, t2\n   bge a2, t3, 6f\n   ori a0, a0, 4\n" PRAST_ASM_RET
        "6:  sub t3, a6, t2\n   blt a2, t3, 8f\n   ori a0, a0, 8\n" PRAST_ASM_RET
        "5:  andi t3, a0, 12\n   beqz t3, 8f\n   add t3, a3, t2\n   bge a1, t3, 7f\n   ori a0, a0, 1\n" PRAST_ASM_RET
        "7:  sub t3, a5, t2\n   blt a1, t3, 8f\n   ori a0, a0, 2\n"
        "8:  " PRAST_ASM_RET
        "9:  li a0, 0\n" PRAST_ASM_RET
        PRAST_ASM_END(prast_edges_at)

        /* prast_dyn_cover(a0=p, a1=pre, a2=u, a3=v, a4=fill, a5=edge) */
        PRAST_ASM_FUNC(prast_dyn_cover)
        "beqz a4, 99f\n   beqz a5, 99f\n   sb zero, 0(a4)\n   sb zero, 0(a5)\n   beqz a0, 99f\n   beqz a1, 99f\n"
        "ld t0, 40(a1)\n   bgt a2, t0, 99f\n   bgt a3, t0, 99f\n   neg t1, t0\n   blt a2, t1, 99f\n   blt a3, t1, 99f\n"
        "lw t0, 8(a0)\n   blez t0, 99f\n"
        "addi sp, sp, -32\n   sd ra, 24(sp)\n   sd s0, 16(sp)\n   sd s1, 8(sp)\n   sd s2, 0(sp)\n"
        "mv s0, a0\n   mv s1, a1\n"
        /* a2=u, a3=v, a4=fill, a5=edge; L_prast_isqrt only clobbers a0, t0, t1, t2 */
        "lw t0, 36(s0)\n   beqz t0, 12f\n"
        /* lens path */
        "lw t3, 24(s0)\n   lw t4, 28(s0)\n   mul t5, a2, t3\n   mul t6, a3, t4\n   add t5, t5, t6\n"
        "srai t0, t5, 63\n   srli t0, t0, 58\n   add t5, t5, t0\n   srai t5, t5, 6\n" /* la */
        "mul t6, a3, t3\n   mul t0, a2, t4\n   sub t6, t6, t0\n   srai t0, t6, 63\n   srli t0, t0, 58\n   add t6, t6, t0\n   srai t6, t6, 6\n" /* b */
        "bgez t5, 100f\n   neg t5, t5\n"
        "100: bgez t6, 101f\n   neg t6, t6\n"
        "101: lw t3, 16(s0)\n   ld t4, 48(s1)\n   beq t4, t3, 10f\n   beqz t4, 98f\n   ld a6, 56(s1)\n"
        "mul t0, t5, a6\n   mul t1, t6, t3\n   ble t0, t1, 10f\n   sub a7, t5, t4\n   mul t0, a7, a6\n   bge t0, t1, 11f\n"
        "mul t0, t5, t3\n   mul t1, t6, a6\n   add t0, t0, t1\n   div t0, t0, t4\n   sub s2, t0, t3\n   j 30f\n"
        "10: mul t0, t5, t5\n   mul t1, t6, t6\n   add a0, t0, t1\n   call " PRAST_HREF(prast_isqrt) "\n   sub s2, a0, t3\n   j 30f\n"
        "11: mul t0, a7, a7\n   mul t1, t6, t6\n   add a0, t0, t1\n   call " PRAST_HREF(prast_isqrt) "\n   mv s2, a0\n   j 30f\n"
        /* rounded-rect / stretch path */
        "12: lw t0, 32(s0)\n   beqz t0, 13f\n   ld a6, 0(s1)\n   beqz a6, 98f\n"
        "lw t3, 24(s0)\n   lw t4, 28(s0)\n   mul t5, a2, t3\n   mul t6, a3, t4\n   add t5, t5, t6\n"
        "srai t0, t5, 63\n   srli t0, t0, 58\n   add t5, t5, t0\n   srai t5, t5, 6\n   slli t5, t5, 8\n   div t5, t5, a6\n"
        "mul t6, a3, t3\n   mul t0, a2, t4\n   sub t6, t6, t0\n   srai t0, t6, 63\n   srli t0, t0, 58\n   add t6, t6, t0\n   srai t6, t6, 6\n"
        "mul t6, t6, a6\n   srai t0, t6, 63\n   srli t0, t0, 56\n   add t6, t6, t0\n   srai t6, t6, 8\n"
        "mul a2, t5, t3\n   mul t0, t6, t4\n   sub a2, a2, t0\n   srai t0, a2, 63\n   srli t0, t0, 58\n   add a2, a2, t0\n   srai a2, a2, 6\n"
        "mul a3, t5, t4\n   mul t0, t6, t3\n   add a3, a3, t0\n   srai t0, a3, 63\n   srli t0, t0, 58\n   add a3, a3, t0\n   srai a3, a3, 6\n"
        "13: lw a6, 20(s0)\n   lw t3, 12(s0)\n   lw t4, 16(s0)\n   sub t3, t3, a6\n   sub t4, t4, a6\n"
        "mv t5, a2\n   bgez t5, 131f\n   neg t5, t5\n"
        "131: sub t5, t5, t3\n   mv t6, a3\n   bgez t6, 132f\n   neg t6, t6\n"
        "132: sub t6, t6, t4\n   li t3, 64\n   li t4, 0\n   blez t5, 20f\n   blez t6, 20f\n"
        "mul t0, t5, t5\n   mul t1, t6, t6\n   add a0, t0, t1\n   ld a7, 16(s1)\n   add t0, a6, a7\n   mul t0, t0, t0\n"
        "bge a0, t0, 98f\n   ld t0, 32(s1)\n   ld t1, 0(s1)\n   mul t0, t0, t1\n   srai t1, t0, 63\n   srli t1, t1, 56\n   add t0, t0, t1\n   srai t0, t0, 8\n"
        "sub t0, a6, t0\n   sub t0, t0, a7\n   blez t0, 19f\n   mul t0, t0, t0\n   bgt a0, t0, 19f\n   li t0, 255\n   sb t0, 0(a4)\n   j 98f\n"
        "19: call " PRAST_HREF(prast_isqrt) "\n   slli t3, t5, 6\n   div t3, t3, a0\n   slli t4, t6, 6\n   div t4, t4, a0\n   sub s2, a0, a6\n   j 22f\n"
        "20: ble t5, t6, 21f\n   sub s2, t5, a6\n   j 22f\n"
        "21: sub s2, t6, a6\n   li t3, 0\n   li t4, 64\n"
        "22: lw t0, 32(s0)\n   beqz t0, 30f\n   ld a6, 0(s1)\n   beqz a6, 98f\n"
        "bgez a2, 221f\n   neg t3, t3\n"
        "221: bgez a3, 222f\n   neg t4, t4\n"
        "222: lw t5, 24(s0)\n   lw t6, 28(s0)\n   mul t0, t3, t5\n   mul t1, t4, t6\n   add t0, t0, t1\n"
        "srai t1, t0, 63\n   srli t1, t1, 58\n   add t0, t0, t1\n   srai t0, t0, 6\n   slli t0, t0, 8\n   div a7, t0, a6\n"
        "mul t0, t4, t5\n   mul t1, t3, t6\n   sub t0, t0, t1\n   srai t1, t0, 63\n   srli t1, t1, 58\n   add t0, t0, t1\n   srai t0, t0, 6\n"
        "mul t0, t0, a6\n   srai t1, t0, 63\n   srli t1, t1, 56\n   add t0, t0, t1\n   srai t0, t0, 8\n"
        "mul a0, a7, a7\n   mul t0, t0, t0\n   add a0, a0, t0\n   call " PRAST_HREF(prast_isqrt) "\n"
        "bnez a0, 23f\n   li a0, 1\n"
        "23: slli s2, s2, 6\n   div s2, s2, a0\n"
        /* lined: away in s2 */
        "30: lw t0, 8(s0)\n   mul s2, s2, t0\n   srai t0, s2, 63\n   srli t0, t0, 56\n   add s2, s2, t0\n   srai s2, s2, 8\n"
        "li t0, 128\n   sub t1, t0, s2\n   li t2, 256\n   bgez t1, 31f\n   li t1, 0\n"
        "31: ble t1, t2, 32f\n   mv t1, t2\n"
        "32: ld t3, 24(s1)\n   sub t4, t0, s2\n   sub t4, t4, t3\n   bgez t4, 33f\n   li t4, 0\n"
        "33: ble t4, t2, 34f\n   mv t4, t2\n"
        "34: sub t5, t1, t4\n   li t6, 255\n   mul t0, t5, t6\n   srli t0, t0, 8\n   sb t0, 0(a5)\n"
        "beqz t4, 98f\n   sub t2, t2, t5\n   mul t4, t4, t6\n   divu t4, t4, t2\n   sb t4, 0(a4)\n"
        "98: ld s2, 0(sp)\n   ld s1, 8(sp)\n   ld s0, 16(sp)\n   ld ra, 24(sp)\n   addi sp, sp, 32\n"
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_dyn_cover)

        /* prast_pointer_ring(a0=arg) */
        PRAST_ASM_FUNC(prast_pointer_ring)
        "beqz a0, 99f\n   ld a1, 0(a0)\n   beqz a1, 99f\n   ld a2, 24(a0)\n   beqz a2, 99f\n   ld a3, 32(a0)\n   beqz a3, 99f\n"
        "lw a4, 40(a0)\n   blez a4, 99f\n   lw a5, 56(a0)\n   blez a5, 99f\n   lw a6, 8(a1)\n   blez a6, 99f\n"
        "addi sp, sp, -128\n   sd ra, 120(sp)\n   sd s0, 112(sp)\n   sd s1, 104(sp)\n   sd s2, 96(sp)\n"
        "sd s3, 88(sp)\n   sd s4, 80(sp)\n   sd s5, 72(sp)\n   sd s6, 64(sp)\n   sd s7, 56(sp)\n"
        "sd s8, 48(sp)\n   sd s9, 40(sp)\n   sd s10, 32(sp)\n   sd s11, 24(sp)\n"
        "mv s0, a0\n   mv s1, a2\n   mv s2, a3\n   mv s3, a4\n   lw s4, 52(s0)\n   add s5, s4, a5\n"
        "li t0, 256\n   bgt a6, t0, 100f\n   mv t1, t0\n   j 101f\n"
        "100: mv t1, a6\n"
        "101: slli t2, t1, 1\n   add t1, t1, t2\n   srli s6, t1, 2\n" /* core */
        "lw s7, 64(a1)\n   slli s7, s7, 7\n" /* turn */
        "lw s8, 44(s0)\n   slli s8, s8, 8\n   lw s9, 48(s0)\n   slli s9, s9, 8\n" /* cx, cy */
        "lw t0, 0(a1)\n   beqz t0, 1f\n   li t1, 12\n   mul s10, a6, t1\n   j 2f\n"
        "1:  ld t0, 16(s0)\n   beqz t0, 90f\n   lwu t1, 4(a1)\n   li t2, 11\n   bgeu t1, t2, 90f\n"
        "slli t1, t1, 1\n   add t0, t0, t1\n   lb t2, 0(t0)\n   lb t3, 1(t0)\n"
        "li t4, 12\n   sub t2, t4, t2\n   mul t2, t2, a6\n   add s8, s8, t2\n"
        "sub t3, t4, t3\n   mul t3, t3, a6\n   add s9, s9, t3\n   li t4, 19\n   mul s10, a6, t4\n   srli s10, s10, 1\n"
        "2:  addi t0, s6, 512\n   li t1, 0\n   ble s10, t0, 20f\n   sub t1, s10, t0\n   mul t1, t1, t1\n"
        "20: add t2, s10, t0\n   mul s11, t2, t2\n   sd t1, 0(sp)\n   lw t3, 60(s0)\n   sd t3, 8(sp)\n" /* inner, pitch */
        /* s1=fill, s2=edge, s3=span, s4=row, s5=end_row, s6=core, s7=turn, s8=cx, s9=cy, s10=r, s11=outer */
        "3:  bge s4, s5, 90f\n   slli a1, s4, 8\n   addi a1, a1, 128\n   sub a1, a1, s9\n   mul a2, a1, a1\n"
        "bgt a2, s11, 8f\n   li a3, 128\n   sub a3, a3, s8\n   li a4, 0\n   ld a5, 0(sp)\n" /* dx, col, inner */
        "4:  bge a4, s3, 8f\n   mul a0, a3, a3\n   add a0, a0, a2\n   bgt a0, s11, 7f\n   blt a0, a5, 7f\n"
        "neg a6, a1\n   bltz a3, 5f\n   blez a6, 5f\n   li t3, 0\n   mv t4, a3\n   mv t5, a6\n   j 6f\n"
        "5:  blez a3, 10f\n   li t3, 1024\n   neg t4, a6\n   mv t5, a3\n   j 6f\n"
        "10: bgez a6, 11f\n   li t3, 2048\n   neg t4, a3\n   neg t5, a6\n   j 6f\n"
        "11: li t3, 3072\n   mv t4, a6\n   neg t5, a3\n"
        "6:  add t6, t4, t5\n   bnez t6, 12f\n   li t6, 1\n"
        "12: slli t4, t4, 10\n   div t4, t4, t6\n   add t3, t3, t4\n   bge t3, s7, 7f\n"
        "call " PRAST_HREF(prast_isqrt) "\n   sub t3, a0, s10\n   bgez t3, 13f\n   neg t3, t3\n"
        "13: addi t4, s6, 128\n   sub t4, t4, t3\n   li t5, 256\n   bgez t4, 14f\n   li t4, 0\n"
        "14: ble t4, t5, 15f\n   mv t4, t5\n"
        "15: addi t6, s6, 384\n   sub t6, t6, t3\n   bgez t6, 16f\n   li t6, 0\n"
        "16: ble t6, t5, 17f\n   mv t6, t5\n"
        "17: sub t6, t6, t4\n   li t0, 255\n   mul t4, t4, t0\n   srli t4, t4, 8\n   mul t6, t6, t0\n   srli t6, t6, 8\n"
        "add t1, s1, a4\n   lbu t2, 0(t1)\n   bgeu t2, t4, 18f\n   sb t4, 0(t1)\n"
        "18: add t1, s2, a4\n   lbu t2, 0(t1)\n   bgeu t2, t6, 7f\n   sb t6, 0(t1)\n"
        "7:  addi a3, a3, 256\n   addi a4, a4, 1\n   j 4b\n"
        "8:  ld t0, 8(sp)\n   add s1, s1, t0\n   add s2, s2, t0\n   addi s4, s4, 1\n   j 3b\n"
        "90: ld s11, 24(sp)\n   ld s10, 32(sp)\n   ld s9, 40(sp)\n   ld s8, 48(sp)\n   ld s7, 56(sp)\n"
        "ld s6, 64(sp)\n   ld s5, 72(sp)\n   ld s4, 80(sp)\n   ld s3, 88(sp)\n   ld s2, 96(sp)\n"
        "ld s1, 104(sp)\n   ld s0, 112(sp)\n   ld ra, 120(sp)\n   addi sp, sp, 128\n"
        "99: " PRAST_ASM_RET
        PRAST_ASM_END(prast_pointer_ring)

        /* prast_pointer_rows(a0=arg)
           Stack frame (960 bytes):
             [sp+0..63]    : prast_dyn_pre (dyn) OR col_u[16] (+0..15) + locals (+16..63)
             [sp+64..191]  : down_ptr[16] (128 bytes)
             [sp+192..769] : share[289][2] (578 bytes)
             [sp+784..847] : locals (step, step2, ox, oy, v, uq, span_step, col_step)
             [sp+848..959] : saved ra, s0..s11 (104 bytes)
        */
        PRAST_ASM_FUNC(prast_pointer_rows)
        "beqz a0, 99f\n   ld a1, 0(a0)\n   beqz a1, 99f\n   ld a2, 24(a0)\n   beqz a2, 99f\n   ld a3, 32(a0)\n   beqz a3, 99f\n"
        "lw a4, 40(a0)\n   blez a4, 99f\n   lw a5, 56(a0)\n   blez a5, 99f\n   lw a6, 8(a1)\n   blez a6, 99f\n"
        "addi sp, sp, -960\n   sd ra, 952(sp)\n   sd s0, 944(sp)\n   sd s1, 936(sp)\n   sd s2, 928(sp)\n"
        "sd s3, 920(sp)\n   sd s4, 912(sp)\n   sd s5, 904(sp)\n   sd s6, 896(sp)\n   sd s7, 888(sp)\n"
        "sd s8, 880(sp)\n   sd s9, 872(sp)\n   sd s10, 864(sp)\n   sd s11, 856(sp)\n"
        "mv s0, a0\n   mv s1, a1\n   mv s2, a2\n   mv s3, a3\n   mv s4, a6\n"
        "lw t0, 0(s1)\n   beqz t0, 30f\n"
        /* ---- DYNAMIC CURSOR PATH ---- */
        "lw t0, 32(s1)\n   addi t0, t0, 256\n   sd t0, 0(sp)\n"
        "li t1, 32768\n   div t1, t1, s4\n   addi t1, t1, 1\n   sd t1, 8(sp)\n"
        "mul t2, t1, t0\n   srai t3, t2, 63\n   srli t3, t3, 56\n   add t2, t2, t3\n   srai t2, t2, 8\n   addi t2, t2, 1\n   sd t2, 16(sp)\n"
        "li t2, 256\n   bge s4, t2, 100f\n   j 101f\n"
        "100: mv t2, s4\n"
        "101: sd t2, 24(sp)\n   slli t2, t2, 8\n   div t2, t2, s4\n   sd t2, 32(sp)\n"
        "lw t2, 12(s1)\n   lw t3, 16(s1)\n   add t4, t2, t3\n   mul t4, t4, t0\n"
        "srai t5, t4, 63\n   srli t5, t5, 56\n   add t4, t4, t5\n   srai t4, t4, 8\n   add s5, t4, t1\n   sd s5, 40(sp)\n" /* bound in s5 */
        "bge t2, t3, 102f\n   mv t2, t3\n"
        "102: sd t2, 48(sp)\n   li t4, 0\n   lw t5, 36(s1)\n   beqz t5, 1f\n   ble t2, t3, 1f\n"
        "mul a0, t2, t2\n   mul t3, t3, t3\n   sub a0, a0, t3\n   call " PRAST_HREF(prast_isqrt) "\n   mv t4, a0\n"
        "1:  sd t4, 56(sp)\n   li t0, 1\n   slli t0, t0, 31\n   div s6, t0, s4\n   slli t1, s6, 1\n   sd t1, 792(sp)\n" /* step in s6, step2 */
        "lw t0, 44(s0)\n   slli t0, t0, 16\n   div t0, t0, s4\n   addi s7, t0, 128\n" /* ox */
        "lw t0, 48(s0)\n   slli t0, t0, 16\n   div t0, t0, s4\n   addi s8, t0, 128\n" /* oy */
        "lw s9, 52(s0)\n   lw t0, 56(s0)\n   add s10, s9, t0\n" /* row, end_row */
        "2:  bge s9, s10, 80f\n   slli t0, s9, 1\n   addi t0, t0, 1\n   mul t0, t0, s6\n   srai t0, t0, 16\n   sub s11, t0, s8\n" /* v */
        "bgt s11, s5, 3f\n   neg t0, s5\n   bge s11, t0, 5f\n"
        "3:  lw t0, 40(s0)\n   li t1, 0\n"
        "4:  add t2, s2, t1\n   sb zero, 0(t2)\n   add t2, s3, t1\n   sb zero, 0(t2)\n   addi t1, t1, 1\n   blt t1, t0, 4b\n   j 9f\n"
        "5:  sd s6, 800(sp)\n   li t1, 0\n   sd t1, 808(sp)\n" /* uq, col */
        "6:  ld t1, 808(sp)\n   lw t0, 40(s0)\n   bge t1, t0, 9f\n"
        "ld t2, 800(sp)\n   ld t3, 792(sp)\n   add t3, t2, t3\n   sd t3, 800(sp)\n"
        "srai a2, t2, 16\n   sub a2, a2, s7\n   bgt a2, s5, 7f\n   neg t3, s5\n   blt a2, t3, 7f\n"
        "mv a0, s1\n   mv a1, sp\n   mv a3, s11\n   add a4, s2, t1\n   add a5, s3, t1\n   call " PRAST_SYM(prast_dyn_cover) "\n"
        "ld t1, 808(sp)\n   addi t1, t1, 1\n   sd t1, 808(sp)\n   j 6b\n"
        "7:  add t2, s2, t1\n   sb zero, 0(t2)\n   add t2, s3, t1\n   sb zero, 0(t2)\n   addi t1, t1, 1\n   sd t1, 808(sp)\n   j 6b\n"
        "9:  lw t0, 60(s0)\n   add s2, s2, t0\n   add s3, s3, t0\n   addi s9, s9, 1\n   j 2b\n"

        /* ---- BITMAP SUPERSAMPLE PATH ---- */
        "30: ld t0, 8(s0)\n   beqz t0, 31f\n   ld t1, 16(s0)\n   beqz t1, 31f\n   lwu t2, 4(s1)\n   li t3, 11\n   bltu t2, t3, 33f\n"
        "31: lw t0, 40(s0)\n   lw t1, 56(s0)\n   lw t2, 60(s0)\n"
        "32: li t3, 0\n"
        "321: add t4, s2, t3\n   sb zero, 0(t4)\n   add t4, s3, t3\n   sb zero, 0(t4)\n   addi t3, t3, 1\n   blt t3, t0, 321b\n"
        "add s2, s2, t2\n   add s3, s3, t2\n   addi t1, t1, -1\n   bgtz t1, 32b\n   j 80f\n"
        "33: li s5, 4\n   li t3, 1024\n   blt s4, t3, 331f\n   li s5, 2\n"
        "331: mul s6, s5, s5\n   addi t3, s6, 1\n   mul t4, s6, s4\n   addi a0, sp, 192\n   li t5, 0\n" /* s5=n, s6=all, t3=all+1, t4=all*scale */
        "34: bgt t5, s6, 39f\n   li t6, 0\n"
        "35: add a1, t5, t6\n   bgt a1, s6, 38f\n   li a2, 256\n   blt s4, a2, 36f\n"
        "li a2, 255\n   mul a3, t5, a2\n   divu a3, a3, s6\n   li a4, 0\n   beqz t6, 37f\n"
        "mul a4, t6, a2\n   sub a5, s6, t5\n   divu a4, a4, a5\n   j 37f\n"
        "36: li a2, 65280\n   li a5, 255\n   mul a3, t5, a2\n   divu a3, a3, t4\n   ble a3, a5, 361f\n   mv a3, a5\n"
        "361: mul a4, t6, a2\n   divu a4, a4, t4\n   sub a6, a5, a3\n   ble a4, a6, 362f\n   mv a4, a6\n"
        "362: beqz a4, 37f\n   mul a4, a4, a5\n   divu a4, a4, a6\n"
        "37: mul a1, t5, t3\n   add a1, a1, t6\n   slli a1, a1, 1\n   add a1, a0, a1\n   sb a4, 0(a1)\n   sb a3, 1(a1)\n   addi t6, t6, 1\n   j 35b\n"
        "38: addi t5, t5, 1\n   j 34b\n"
        "39: li a0, 1\n   slli a0, a0, 31\n   div a0, a0, t4\n   sd a0, 784(sp)\n   slli a1, a0, 1\n   sd a1, 792(sp)\n"
        "addi a2, s6, -1\n   mul a2, a2, a1\n   sd a2, 816(sp)\n   mul a2, s6, a1\n   sd a2, 824(sp)\n"
        "li a2, 600\n   mul a2, t2, a2\n   add s7, t0, a2\n" /* shape_art */
        "slli t2, t2, 1\n   add t1, t1, t2\n   lb a2, 0(t1)\n   lb a3, 1(t1)\n"
        "lw a4, 44(s0)\n   slli a4, a4, 16\n   div a4, a4, s4\n   slli a2, a2, 8\n   sub s8, a4, a2\n" /* ox */
        "lw a4, 48(s0)\n   slli a4, a4, 16\n   div a4, a4, s4\n   slli a3, a3, 8\n   sub s9, a4, a3\n" /* oy */
        "lw s10, 52(s0)\n   lw a4, 56(s0)\n   add s11, s10, a4\n" /* row, end_row */
        "40: bge s10, s11, 80f\n   mul a0, s10, s6\n   slli a0, a0, 1\n   addi a0, a0, 1\n"
        "ld a1, 784(sp)\n   ld a2, 792(sp)\n   mul a0, a0, a1\n   addi a3, sp, 64\n   li a4, 0\n   li a5, 0\n   li a6, 6144\n   li a7, 25\n"
        "41: bge a4, s6, 43f\n   srai t0, a0, 16\n   sub t0, t0, s9\n   add a0, a0, a2\n"
        "bgeu t0, a6, 42f\n   srli t0, t0, 8\n   mul t0, t0, a7\n   add t0, s7, t0\n   slli t1, a4, 3\n   add t1, a3, t1\n   sd t0, 0(t1)\n   li a5, 1\n   addi a4, a4, 1\n   j 41b\n"
        "42: slli t1, a4, 3\n   add t1, a3, t1\n   sd zero, 0(t1)\n   addi a4, a4, 1\n   j 41b\n"
        "43: bnez a5, 45f\n   lw t0, 40(s0)\n   li t1, 0\n"
        "44: add t2, s2, t1\n   sb zero, 0(t2)\n   add t2, s3, t1\n   sb zero, 0(t2)\n   addi t1, t1, 1\n   blt t1, t0, 44b\n   j 55f\n"
        "45: ld a0, 784(sp)\n   ld a1, 816(sp)\n   ld a2, 824(sp)\n   lw a3, 40(s0)\n   li a4, 0\n" /* uq, span_step, col_step, span, col */
        "46: bge a4, a3, 55f\n   srai t0, a0, 16\n   sub t0, t0, s8\n   add t1, a0, a1\n   srai t1, t1, 16\n   sub t1, t1, s8\n"
        "bltz t1, 47f\n   blt t0, a6, 48f\n"
        "47: add t2, s2, a4\n   sb zero, 0(t2)\n   add t2, s3, a4\n   sb zero, 0(t2)\n   add a0, a0, a2\n   addi a4, a4, 1\n   j 46b\n"
        "48: ld a7, 792(sp)\n   li t0, 0\n"
        "49: bge t0, s6, 50f\n   srai t1, a0, 16\n   sub t1, t1, s8\n   add a0, a0, a7\n"
        "li t2, -1\n   bgeu t1, a6, 491f\n   srli t2, t1, 8\n"
        "491: add t3, sp, t0\n   sb t2, 0(t3)\n   addi t0, t0, 1\n   j 49b\n"
        "50: li t0, 0\n   li t1, 0\n   li t2, 0\n   addi t3, sp, 64\n" /* e, f, j, dp */
        "51: bge t2, s5, 54f\n   li t4, 0\n   add t5, sp, t2\n" /* i, up */
        "52: bge t4, s5, 53f\n   ld t6, 0(t3)\n   addi t3, t3, 8\n   lb a5, 0(t5)\n   add t5, t5, s5\n"
        "beqz t6, 521f\n   bltz a5, 521f\n   add t6, t6, a5\n   lbu t6, 0(t6)\n"
        "li a5, 35\n   bne t6, a5, 520f\n   addi t0, t0, 1\n   j 521f\n"
        "520: li a5, 111\n   bne t6, a5, 521f\n   addi t1, t1, 1\n"
        "521: addi t4, t4, 1\n   j 52b\n"
        "53: addi t2, t2, 1\n   j 51b\n"
        "54: addi t2, s6, 1\n   mul t0, t0, t2\n   add t0, t0, t1\n   slli t0, t0, 1\n   addi t2, sp, 192\n   add t2, t2, t0\n"
        "lbu t0, 0(t2)\n   lbu t1, 1(t2)\n   add t2, s2, a4\n   sb t0, 0(t2)\n   add t2, s3, a4\n   sb t1, 0(t2)\n   addi a4, a4, 1\n   j 46b\n"
        "55: lw t0, 60(s0)\n   add s2, s2, t0\n   add s3, s3, t0\n   addi s10, s10, 1\n   j 40b\n"

        "80: lw t0, 64(s1)\n   mv a0, s0\n"
        "ld s11, 856(sp)\n   ld s10, 864(sp)\n   ld s9, 872(sp)\n   ld s8, 880(sp)\n   ld s7, 888(sp)\n"
        "ld s6, 896(sp)\n   ld s5, 904(sp)\n   ld s4, 912(sp)\n   ld s3, 920(sp)\n   ld s2, 928(sp)\n"
        "ld s1, 936(sp)\n   ld s0, 944(sp)\n   ld ra, 952(sp)\n   addi sp, sp, 960\n"
        "bnez t0, 81f\n"
        "99: " PRAST_ASM_RET
        "81: tail " PRAST_SYM(prast_pointer_ring) "\n"
        PRAST_ASM_END(prast_pointer_rows)
);

#endif /* arch */
#endif /* COMPOSITOR_POINTER_RAST_ASM_IMPLEMENTATION && !COMPOSITOR_POINTER_RAST_ASM_EMITTED */
