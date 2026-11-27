/*
        Compositor row-bitset, atomic door-pop, and vblank/pacing primitives in pure assembly:
          - prows_next          : next set bit >= from in multi-word bitset (bounded by limit & nwords)
          - prows_any           : non-empty test across active words + misc slot (early exit)
          - prows_none          : clear active words + misc slot
          - prows_or            : bitwise OR across active words + misc slot
          - prows_and_not       : bitwise AND-NOT across active words + misc slot
          - prows_take          : atomic ACQ_REL exchange-to-zero drain (skips empty words)
          - prows_door_next     : lock-free atomic lowest-bit pop across door queue bitset
          - prows_metric_worst  : ring-buffer worst-of-last-N scan (branchless wrap, zero mod in loop)
          - prows_popcount_mask : SWAR popcount of low min(count, 32) bits
          - prows_frame_dated   : drift-free frame timestamp dating
          - prows_present_due   : vblank-locked late-present deadline calculation
          - prows_lead_of       : composed present lead computation with spare & widen
          - prows_cadence_slot  : cadence-k direct-scanout slot clamp
          - prows_late_landed   : vblank miss/calm adaptive lead state machine

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_PACING_ROWS_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_PACING_ROWS_ASM_H
#define MOONWATER_COMPOSITOR_PACING_ROWS_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  prows_u8;
typedef p16 prows_u16;
typedef p32 prows_u32;
typedef b32 prows_i32;
typedef p64 prows_u64;
typedef b64 prows_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  prows_u8;
typedef __UINT16_TYPE__ prows_u16;
typedef __UINT32_TYPE__ prows_u32;
typedef __INT32_TYPE__  prows_i32;
typedef __UINT64_TYPE__ prows_u64;
typedef __INT64_TYPE__  prows_i64;
#else
#include <stdint.h>
typedef uint8_t  prows_u8;
typedef uint16_t prows_u16;
typedef uint32_t prows_u32;
typedef int32_t  prows_i32;
typedef uint64_t prows_u64;
typedef int64_t  prows_i64;
#endif

#define PROWS_LEAD_SPARE     1000000ull
#define PROWS_LEAD_CALM      120u
#define PROWS_LEAD_CALM_MOST (64u * PROWS_LEAD_CALM)
#define PROWS_LEAD_IDLE_NS   250000000ull
#define PROWS_LEAD_WARM      8u
#define PROWS_NO_LAST        (~0u)

/* 16 bytes — byte-for-byte identical to struct compositor_lead_step */
struct prows_lead_step {
        prows_u32 calm;      /* +0  */
        prows_u32 calm_need; /* +4  */
        prows_u64 widen;     /* +8  */
};

/* 80 bytes — byte-for-byte identical to compositor_late */
struct prows_late_state {
        prows_i32              early;        /* +0  */
        prows_i32              aimed_direct; /* +4  */
        struct prows_lead_step composed;     /* +8  */
        struct prows_lead_step direct;       /* +24 */
        prows_u32              warm;         /* +40 */
        prows_u32              _pad;         /* +44 */
        prows_u64              landed;       /* +48 */
        prows_u64              aim;          /* +56 */
        prows_u64              made;         /* +64 */
        prows_u64              missed;       /* +72 */
};

#ifdef offset_of
#define PROWS_OFFSET(T, M) offset_of(T, M)
#else
#define PROWS_OFFSET(T, M) ((prows_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct prows_lead_step)  == 16, "prows_lead_step must be 16 bytes");
_Static_assert(sizeof(struct prows_late_state) == 80, "prows_late_state must be 80 bytes");
_Static_assert(PROWS_OFFSET(struct prows_lead_step, calm)          == 0,  "step.calm");
_Static_assert(PROWS_OFFSET(struct prows_lead_step, calm_need)     == 4,  "step.calm_need");
_Static_assert(PROWS_OFFSET(struct prows_lead_step, widen)         == 8,  "step.widen");
_Static_assert(PROWS_OFFSET(struct prows_late_state, early)        == 0,  "late.early");
_Static_assert(PROWS_OFFSET(struct prows_late_state, aimed_direct) == 4,  "late.aimed_direct");
_Static_assert(PROWS_OFFSET(struct prows_late_state, composed)     == 8,  "late.composed");
_Static_assert(PROWS_OFFSET(struct prows_late_state, direct)       == 24, "late.direct");
_Static_assert(PROWS_OFFSET(struct prows_late_state, warm)         == 40, "late.warm");
_Static_assert(PROWS_OFFSET(struct prows_late_state, landed)       == 48, "late.landed");
_Static_assert(PROWS_OFFSET(struct prows_late_state, aim)          == 56, "late.aim");
_Static_assert(PROWS_OFFSET(struct prows_late_state, made)         == 64, "late.made");
_Static_assert(PROWS_OFFSET(struct prows_late_state, missed)       == 72, "late.missed");

prows_i32 prows_next(const prows_u64 *words, prows_u32 nwords, prows_u32 limit, prows_i32 from);
prows_i32 prows_any(const prows_u64 *words, prows_u32 nwords, prows_u32 last_word);
void      prows_none(prows_u64 *words, prows_u32 nwords, prows_u32 last_word);
void      prows_or(prows_u64 *into, const prows_u64 *with, prows_u32 nwords, prows_u32 last_word);
void      prows_and_not(prows_u64 *into, const prows_u64 *without, prows_u32 nwords, prows_u32 last_word);
void      prows_take(prows_u64 *into, prows_u64 *from, prows_u32 nwords, prows_u32 last_word);
prows_i64 prows_door_next(prows_u64 *words, prows_u32 nwords, prows_u32 last_word);
prows_u64 prows_metric_worst(const prows_u64 *ring, prows_u32 ring_size, prows_u32 at, prows_u32 count, prows_u32 last);
prows_u32 prows_popcount_mask(prows_u32 bits, prows_u32 count);
prows_u64 prows_frame_dated(prows_u64 last, prows_u64 period, prows_u64 stamp);
prows_u64 prows_present_due(prows_u64 presented, prows_u64 period, prows_u64 vblank, prows_u64 lead, prows_u64 now);
prows_u64 prows_lead_of(prows_u64 worst, prows_u32 timed, prows_u64 widen, prows_u64 period);
prows_u64 prows_cadence_slot(prows_u64 due, prows_u64 flip_when, prows_u64 period, prows_u64 lead, prows_u32 cadence_k);
void      prows_late_landed(struct prows_late_state *state, prows_u64 when, prows_u64 period);

#endif /* MOONWATER_COMPOSITOR_PACING_ROWS_ASM_H */

#if defined(COMPOSITOR_PACING_ROWS_ASM_IMPLEMENTATION) && !defined(COMPOSITOR_PACING_ROWS_ASM_EMITTED)
#define COMPOSITOR_PACING_ROWS_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define PROWS_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define PROWS_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define PROWS_RISCV64 1
# else
#  error "src/canvas/compositor/pacing_rows.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define PROWS_ARM64 1
# elif defined(X64) && X64
#  define PROWS_X64 1
# elif defined(RISCV64) && RISCV64
#  define PROWS_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define PROWS_ASM_SECTION   ASM_SECTION
# define PROWS_ASM_FUNC(n)   ASM_FUNC(n)
# define PROWS_ASM_END(n)    ASM_END(n)
# define PROWS_ASM_RET       ASM_RET
#else
# define PROWS_ASM_SECTION   ".text\n"
# define PROWS_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define PROWS_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define PROWS_ASM_END(n)   ""
# elif defined(_WIN32)
#  define PROWS_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define PROWS_ASM_END(n)   ""
# else
#  if defined(PROWS_ARM64)
#   define PROWS_ASM_FTYPE   "%function"
#  else
#   define PROWS_ASM_FTYPE   "@function"
#  endif
#  define PROWS_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " PROWS_ASM_FTYPE "\n" #n ":\n"
#  define PROWS_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(PROWS_ARM64)

__asm__(
        PROWS_ASM_SECTION

        /* prows_next(x0=words, w1=nwords, w2=limit, w3=from) -> w0 */
        PROWS_ASM_FUNC(prows_next)
        "cbz x0, 3f\n   cmp w3, #0\n   csel w3, wzr, w3, lt\n   cmp w3, w2\n   b.hs 3f\n"
        "lsr w4, w3, #6\n   cmp w4, w1\n   b.hs 3f\n   mov x6, #-1\n   lsl x6, x6, x3\n"
        "ldr x5, [x0, w4, uxtw #3]\n   ands x5, x5, x6\n   b.ne 2f\n"
        "1:  add w4, w4, #1\n   cmp w4, w1\n   b.hs 3f\n   ldr x5, [x0, w4, uxtw #3]\n   cbz x5, 1b\n"
        "2:  rbit x5, x5\n   clz x5, x5\n   add w0, w5, w4, lsl #6\n   cmp w0, w2\n   b.lo 4f\n"
        "3:  mov w0, #-1\n"
        "4:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_next)

        /* prows_any(x0=words, w1=nwords, w2=last_word) -> w0 */
        PROWS_ASM_FUNC(prows_any)
        "cbz x0, 3f\n   mov w3, #0\n"
        "1:  cmp w3, w1\n   b.hs 2f\n   ldr x4, [x0, w3, uxtw #3]\n   cbnz x4, 4f\n   add w3, w3, #1\n   b 1b\n"
        "2:  cmn w2, #1\n   b.eq 3f\n   cmp w2, w1\n   b.lo 3f\n   ldr x4, [x0, w2, uxtw #3]\n   cbnz x4, 4f\n"
        "3:  mov w0, #0\n" PROWS_ASM_RET
        "4:  mov w0, #1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_any)

        /* prows_none(x0=words, w1=nwords, w2=last_word) */
        PROWS_ASM_FUNC(prows_none)
        "cbz x0, 3f\n   mov w3, #0\n"
        "1:  cmp w3, w1\n   b.hs 2f\n   str xzr, [x0, w3, uxtw #3]\n   add w3, w3, #1\n   b 1b\n"
        "2:  cmn w2, #1\n   b.eq 3f\n   cmp w2, w1\n   b.lo 3f\n   str xzr, [x0, w2, uxtw #3]\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_none)

        /* prows_or(x0=into, x1=with, w2=nwords, w3=last_word) */
        PROWS_ASM_FUNC(prows_or)
        "cbz x0, 3f\n   cbz x1, 3f\n   mov w4, #0\n"
        "1:  cmp w4, w2\n   b.hs 2f\n   ldr x5, [x0, w4, uxtw #3]\n   ldr x6, [x1, w4, uxtw #3]\n"
        "orr x5, x5, x6\n   str x5, [x0, w4, uxtw #3]\n   add w4, w4, #1\n   b 1b\n"
        "2:  cmn w3, #1\n   b.eq 3f\n   cmp w3, w2\n   b.lo 3f\n   ldr x5, [x0, w3, uxtw #3]\n"
        "ldr x6, [x1, w3, uxtw #3]\n   orr x5, x5, x6\n   str x5, [x0, w3, uxtw #3]\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_or)

        /* prows_and_not(x0=into, x1=without, w2=nwords, w3=last_word) */
        PROWS_ASM_FUNC(prows_and_not)
        "cbz x0, 3f\n   cbz x1, 3f\n   mov w4, #0\n"
        "1:  cmp w4, w2\n   b.hs 2f\n   ldr x5, [x0, w4, uxtw #3]\n   ldr x6, [x1, w4, uxtw #3]\n"
        "bic x5, x5, x6\n   str x5, [x0, w4, uxtw #3]\n   add w4, w4, #1\n   b 1b\n"
        "2:  cmn w3, #1\n   b.eq 3f\n   cmp w3, w2\n   b.lo 3f\n   ldr x5, [x0, w3, uxtw #3]\n"
        "ldr x6, [x1, w3, uxtw #3]\n   bic x5, x5, x6\n   str x5, [x0, w3, uxtw #3]\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_and_not)

        /* prows_take(x0=into, x1=from, w2=nwords, w3=last_word) */
        PROWS_ASM_FUNC(prows_take)
        "cbz x0, 7f\n   cbz x1, 7f\n   mov w4, #0\n"
        "1:  cmp w4, w2\n   b.hs 4f\n   add x5, x1, w4, uxtw #3\n   ldr x6, [x5]\n   cbz x6, 3f\n"
        "2:  ldaxr x6, [x5]\n   stlxr w7, xzr, [x5]\n   cbnz w7, 2b\n"
        "3:  str x6, [x0, w4, uxtw #3]\n   add w4, w4, #1\n   b 1b\n"
        "4:  cmn w3, #1\n   b.eq 7f\n   cmp w3, w2\n   b.lo 7f\n   add x5, x1, w3, uxtw #3\n"
        "ldr x6, [x5]\n   cbz x6, 6f\n"
        "5:  ldaxr x6, [x5]\n   stlxr w7, xzr, [x5]\n   cbnz w7, 5b\n"
        "6:  str x6, [x0, w3, uxtw #3]\n"
        "7:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_take)

        /* prows_door_next(x0=words, w1=nwords, w2=last_word) -> x0 */
        PROWS_ASM_FUNC(prows_door_next)
        "cbz x0, 7f\n   mov w3, #0\n"
        "1:  cmp w3, w1\n   b.hs 4f\n   add x4, x0, w3, uxtw #3\n   ldar x5, [x4]\n   cbz x5, 3f\n"
        "2:  ldaxr x5, [x4]\n   cbz x5, 8f\n   sub x6, x5, #1\n   and x6, x5, x6\n"
        "stlxr w7, x6, [x4]\n   cbnz w7, 2b\n   rbit x5, x5\n   clz x5, x5\n   add x0, x5, x3, lsl #6\n" PROWS_ASM_RET
        "8:  clrex\n"
        "3:  add w3, w3, #1\n   b 1b\n"
        "4:  cmn w2, #1\n   b.eq 7f\n   cmp w2, w1\n   b.lo 7f\n   add x4, x0, w2, uxtw #3\n"
        "ldar x5, [x4]\n   cbz x5, 7f\n"
        "5:  ldaxr x5, [x4]\n   cbz x5, 6f\n   sub x6, x5, #1\n   and x6, x5, x6\n"
        "stlxr w7, x6, [x4]\n   cbnz w7, 5b\n   rbit x5, x5\n   clz x5, x5\n   add x0, x5, x2, lsl #6\n" PROWS_ASM_RET
        "6:  clrex\n"
        "7:  mov x0, #-1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_door_next)

        /* prows_metric_worst(x0=ring, w1=ring_size, w2=at, w3=count, w4=last) -> x0 */
        PROWS_ASM_FUNC(prows_metric_worst)
        "cbz x0, 6f\n   cbz w1, 6f\n   cmp w3, w4\n   csel w4, w3, w4, lo\n   cmp w1, w4\n"
        "csel w4, w1, w4, lo\n   cbz w4, 6f\n   cmp w2, w1\n   b.lo 0f\n"
        "udiv w5, w2, w1\n   msub w2, w5, w1, w2\n"
        "0:  mov x5, #0\n   cmp w4, w2\n   csel w6, w4, w2, lo\n   sub w4, w4, w6\n"
        "add x7, x0, w2, uxtw #3\n   cbz w6, 3f\n"
        "1:  cmp w6, #2\n   b.lo 2f\n   ldp x8, x9, [x7, #-16]!\n   cmp x8, x5\n   csel x5, x8, x5, hi\n"
        "cmp x9, x5\n   csel x5, x9, x5, hi\n   subs w6, w6, #2\n   b.ne 1b\n   b 3f\n"
        "2:  ldr x8, [x7, #-8]!\n   cmp x8, x5\n   csel x5, x8, x5, hi\n"
        "3:  cbz w4, 5f\n   mov w6, w4\n   mov w4, #0\n   add x7, x0, w1, uxtw #3\n   b 1b\n"
        "5:  mov x0, x5\n" PROWS_ASM_RET
        "6:  mov x0, #0\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_metric_worst)

        /* prows_popcount_mask(w0=bits, w1=count) -> w0 */
        PROWS_ASM_FUNC(prows_popcount_mask)
        "cbz w1, 2f\n   cmp w1, #32\n   b.hs 1f\n   mov w2, #1\n   lsl w2, w2, w1\n   sub w2, w2, #1\n   and w0, w0, w2\n"
        "1:  lsr w2, w0, #1\n   and w2, w2, #0x55555555\n   sub w0, w0, w2\n   lsr w2, w0, #2\n"
        "and w0, w0, #0x33333333\n   and w2, w2, #0x33333333\n   add w0, w0, w2\n   add w0, w0, w0, lsr #4\n"
        "and w0, w0, #0x0f0f0f0f\n   movz w3, #0x0101\n   movk w3, #0x0101, lsl #16\n   mul w0, w0, w3\n   lsr w0, w0, #24\n" PROWS_ASM_RET
        "2:  mov w0, #0\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_popcount_mask)

        /* prows_frame_dated(x0=last, x1=period, x2=stamp) -> x0 */
        PROWS_ASM_FUNC(prows_frame_dated)
        "cbz x0, 1f\n   cbz x1, 1f\n   add x3, x0, x1\n   cmp x2, x3\n   b.lo 1f\n"
        "sub x4, x2, x3\n   cmp x4, x1\n   csel x0, x3, x2, lo\n" PROWS_ASM_RET
        "1:  mov x0, x2\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_frame_dated)

        /* prows_present_due(x0=presented, x1=period, x2=vblank, x3=lead, x4=now) -> x0 */
        PROWS_ASM_FUNC(prows_present_due)
        "cbz x3, 1f\n   cbz x1, 1f\n   cmp x3, x1\n   b.hs 1f\n   cbz x2, 1f\n"
        "cmp x4, x2\n   b.lo 1f\n   sub x5, x4, x2\n   cmp x5, x1\n   b.hs 1f\n"
        "add x5, x2, x1\n   sub x5, x5, x3\n   cmp x4, x5\n   csel x5, x4, x5, hi\n"
        "add x6, x0, x1, lsr #1\n   cmp x5, x6\n   csel x0, x5, x6, hi\n" PROWS_ASM_RET
        "1:  sub x5, x4, x0\n   add x6, x0, x1\n   cmp x5, x1\n   csel x0, x6, x4, lo\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_present_due)

        /* prows_lead_of(x0=worst, w1=timed, x2=widen, x3=period) -> x0 */
        PROWS_ASM_FUNC(prows_lead_of)
        "cmp w1, #4\n   b.lo 1f\n   cbz x3, 1f\n   cmp x2, x3\n   b.hs 1f\n"
        "add x0, x0, x0, lsr #1\n   movz x4, #0x4240\n   movk x4, #0x000f, lsl #16\n"
        "add x0, x0, x4\n   add x0, x0, x2\n   cmp x0, x3\n   b.lo 2f\n"
        "1:  mov x0, #0\n"
        "2:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_lead_of)

        /* prows_cadence_slot(x0=due, x1=flip_when, x2=period, x3=lead, w4=cadence_k) -> x0 */
        PROWS_ASM_FUNC(prows_cadence_slot)
        "cmp w4, #2\n   b.lo 2f\n   cbz x1, 2f\n   cbz x2, 2f\n   lsr x5, x2, #1\n"
        "cbz x3, 1f\n   cmp x3, x2\n   csel x5, x3, x5, lo\n"
        "1:  uxtw x4, w4\n   madd x6, x4, x2, x1\n   sub x6, x6, x5\n   cmp x6, x0\n   csel x0, x6, x0, hi\n"
        "2:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_cadence_slot)

        /* prows_late_landed(x0=state, x1=when, x2=period) */
        PROWS_ASM_FUNC(prows_late_landed)
        "cbz x0, 6f\n   ldr w3, [x0, #4]\n   add x4, x0, #8\n   add x5, x0, #24\n   cmp w3, #0\n   csel x4, x5, x4, ne\n"
        "ldp x6, x9, [x0, #48]\n   cbz x6, 1f\n   cmp x1, x6\n   b.ls 1f\n   sub x7, x1, x6\n"
        "movz x8, #0x7840\n   movk x8, #0x0ee6, lsl #16\n   cmp x7, x8\n   b.lo 1f\n   str wzr, [x0, #40]\n"
        "1:  stp x1, xzr, [x0, #48]\n   cbz x9, 3f\n   cmp x1, x9\n   b.ls 3f\n"
        "sub x7, x1, x9\n   cmp x7, x2, lsr #1\n   b.lo 3f\n   ldr x8, [x0, #72]\n   add x8, x8, #1\n   str x8, [x0, #72]\n"
        "ldr w8, [x0, #40]\n   cmp w8, #8\n   b.lo 5f\n   str wzr, [x4]\n   ldr x8, [x4, #8]\n   cbnz x8, 2f\n"
        "lsr x8, x2, #2\n   str x8, [x4, #8]\n   b 5f\n"
        "2:  str x2, [x4, #8]\n   ldr w5, [x4, #4]\n   mov w6, #120\n   cmp w5, #0\n   csel w5, w5, w6, ne\n"
        "lsl w5, w5, #1\n   mov w8, #7680\n   cmp w5, w8\n   csel w5, w5, w8, lo\n   str w5, [x4, #4]\n   b 5f\n"
        "3:  ldr x8, [x4, #8]\n   cbz x8, 5f\n   ldp w9, w5, [x4]\n   mov w6, #120\n   cmp w5, #0\n   csel w5, w5, w6, ne\n"
        "add w9, w9, #1\n   str w9, [x4]\n   cmp w9, w5\n   b.lo 5f\n   str wzr, [x4]\n"
        "cmp x8, x2\n   b.lo 4f\n   lsr x8, x2, #2\n   str x8, [x4, #8]\n   b 5f\n"
        "4:  str xzr, [x4, #8]\n   str wzr, [x4, #4]\n"
        "5:  ldr w8, [x0, #40]\n   cmp w8, #8\n   b.hs 6f\n   add w8, w8, #1\n   str w8, [x0, #40]\n"
        "6:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_late_landed)
);

#elif defined(PROWS_X64)

#if defined(_WIN32)
# define PROWS_X64_A3      "mov r8, rcx\n   mov r9, rdx\n   mov r10, r8\n"
# define PROWS_X64_LD3     "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define PROWS_X64_LD4     "mov r11, r9\n   mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define PROWS_X64_LD4C    "mov r10, r8\n   mov r8, rcx\n   mov rcx, r9\n   mov r9, rdx\n"
# define PROWS_X64_LD5_WORST "mov r10d, r8d\n   mov r11d, r9d\n   mov r8, rcx\n   mov r9d, edx\n   mov ecx, dword ptr [rsp + 40]\n"
# define PROWS_X64_LD5_DUE   "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov rcx, qword ptr [rsp + 40]\n"
# define PROWS_X64_LD5_CAD   "mov rax, rcx\n   mov r10, r9\n   mov r9, r8\n   mov r8, rdx\n   mov ecx, dword ptr [rsp + 40]\n"
# define PROWS_X64_LD2_POP   "mov eax, ecx\n   mov ecx, edx\n"
#else
# define PROWS_X64_LD3     "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define PROWS_X64_LD4     "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
# define PROWS_X64_LD4C    "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define PROWS_X64_LD5_WORST "mov r10d, edx\n   mov r11d, ecx\n   mov ecx, r8d\n   mov r8, rdi\n   mov r9d, esi\n"
# define PROWS_X64_LD5_DUE   "mov r10, rdx\n   mov r11, rcx\n   mov rcx, r8\n   mov r8, rdi\n   mov r9, rsi\n"
# define PROWS_X64_LD5_CAD   "mov rax, rdi\n   mov r10, rcx\n   mov ecx, r8d\n   mov r8, rsi\n   mov r9, rdx\n"
# define PROWS_X64_LD2_POP   "mov eax, edi\n   mov ecx, esi\n"
#endif

__asm__(
        PROWS_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* prows_next(words, nwords, limit, from) -> eax */
        PROWS_ASM_FUNC(prows_next)
        PROWS_X64_LD4C
        "test r8, r8\n   jz 3f\n   xor eax, eax\n   test ecx, ecx\n   cmovs ecx, eax\n"
        "cmp ecx, r10d\n   jae 3f\n   mov edx, ecx\n   shr edx, 6\n   cmp edx, r9d\n   jae 3f\n"
        "mov r11, -1\n   shl r11, cl\n   mov rax, qword ptr [r8 + rdx*8]\n   and rax, r11\n   jnz 2f\n"
        "1:  inc edx\n   cmp edx, r9d\n   jae 3f\n   mov rax, qword ptr [r8 + rdx*8]\n   test rax, rax\n   jz 1b\n"
        "2:  bsf rax, rax\n   shl edx, 6\n   add eax, edx\n   cmp eax, r10d\n   jb 4f\n"
        "3:  mov eax, -1\n"
        "4:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_next)

        /* prows_any(words, nwords, last_word) -> eax */
        PROWS_ASM_FUNC(prows_any)
        PROWS_X64_LD3
        "test r8, r8\n   jz 3f\n   xor ecx, ecx\n   mov r9d, r9d\n"
        "1:  cmp ecx, r9d\n   jae 2f\n   cmp qword ptr [r8 + rcx*8], 0\n   jne 4f\n   inc ecx\n   jmp 1b\n"
        "2:  mov r10d, r10d\n   cmp r10d, -1\n   je 3f\n   cmp r10d, r9d\n   jb 3f\n"
        "cmp qword ptr [r8 + r10*8], 0\n   jne 4f\n"
        "3:  xor eax, eax\n" PROWS_ASM_RET
        "4:  mov eax, 1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_any)

        /* prows_none(words, nwords, last_word) */
        PROWS_ASM_FUNC(prows_none)
        PROWS_X64_LD3
        "test r8, r8\n   jz 3f\n   xor ecx, ecx\n   mov r9d, r9d\n"
        "1:  cmp ecx, r9d\n   jae 2f\n   mov qword ptr [r8 + rcx*8], 0\n   inc ecx\n   jmp 1b\n"
        "2:  mov r10d, r10d\n   cmp r10d, -1\n   je 3f\n   cmp r10d, r9d\n   jb 3f\n"
        "mov qword ptr [r8 + r10*8], 0\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_none)

        /* prows_or(into, with, nwords, last_word) */
        PROWS_ASM_FUNC(prows_or)
        PROWS_X64_LD4
        "test r8, r8\n   jz 3f\n   test r9, r9\n   jz 3f\n   xor ecx, ecx\n   mov r10d, r10d\n"
        "1:  cmp ecx, r10d\n   jae 2f\n   mov rax, qword ptr [r9 + rcx*8]\n   or qword ptr [r8 + rcx*8], rax\n   inc ecx\n   jmp 1b\n"
        "2:  mov r11d, r11d\n   cmp r11d, -1\n   je 3f\n   cmp r11d, r10d\n   jb 3f\n"
        "mov rax, qword ptr [r9 + r11*8]\n   or qword ptr [r8 + r11*8], rax\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_or)

        /* prows_and_not(into, without, nwords, last_word) */
        PROWS_ASM_FUNC(prows_and_not)
        PROWS_X64_LD4
        "test r8, r8\n   jz 3f\n   test r9, r9\n   jz 3f\n   xor ecx, ecx\n   mov r10d, r10d\n"
        "1:  cmp ecx, r10d\n   jae 2f\n   mov rax, qword ptr [r9 + rcx*8]\n   not rax\n   and qword ptr [r8 + rcx*8], rax\n   inc ecx\n   jmp 1b\n"
        "2:  mov r11d, r11d\n   cmp r11d, -1\n   je 3f\n   cmp r11d, r10d\n   jb 3f\n"
        "mov rax, qword ptr [r9 + r11*8]\n   not rax\n   and qword ptr [r8 + r11*8], rax\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_and_not)

        /* prows_take(into, from, nwords, last_word) */
        PROWS_ASM_FUNC(prows_take)
        PROWS_X64_LD4
        "test r8, r8\n   jz 5f\n   test r9, r9\n   jz 5f\n   xor ecx, ecx\n   mov r10d, r10d\n"
        "1:  cmp ecx, r10d\n   jae 3f\n   mov rax, qword ptr [r9 + rcx*8]\n   test rax, rax\n   jz 2f\n"
        "xor eax, eax\n   xchg qword ptr [r9 + rcx*8], rax\n"
        "2:  mov qword ptr [r8 + rcx*8], rax\n   inc ecx\n   jmp 1b\n"
        "3:  mov r11d, r11d\n   cmp r11d, -1\n   je 5f\n   cmp r11d, r10d\n   jb 5f\n"
        "mov rax, qword ptr [r9 + r11*8]\n   test rax, rax\n   jz 4f\n   xor eax, eax\n   xchg qword ptr [r9 + r11*8], rax\n"
        "4:  mov qword ptr [r8 + r11*8], rax\n"
        "5:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_take)

        /* prows_door_next(words, nwords, last_word) -> rax */
        PROWS_ASM_FUNC(prows_door_next)
        PROWS_X64_LD3
        "test r8, r8\n   jz 6f\n   xor edx, edx\n   mov r9d, r9d\n"
        "1:  cmp edx, r9d\n   jae 3f\n   mov rax, qword ptr [r8 + rdx*8]\n   test rax, rax\n   jz 2f\n"
        "7:  bsf rcx, rax\n   lock btr qword ptr [r8 + rdx*8], rcx\n   jc 5f\n"
        "mov rax, qword ptr [r8 + rdx*8]\n   test rax, rax\n   jnz 7b\n"
        "2:  inc edx\n   jmp 1b\n"
        "3:  mov r10d, r10d\n   cmp r10d, -1\n   je 6f\n   cmp r10d, r9d\n   jb 6f\n   mov edx, r10d\n"
        "mov rax, qword ptr [r8 + rdx*8]\n   test rax, rax\n   jz 6f\n"
        "4:  bsf rcx, rax\n   lock btr qword ptr [r8 + rdx*8], rcx\n   jc 5f\n"
        "mov rax, qword ptr [r8 + rdx*8]\n   test rax, rax\n   jnz 4b\n   jmp 6f\n"
        "5:  shl rdx, 6\n   lea rax, [rdx + rcx]\n" PROWS_ASM_RET
        "6:  mov rax, -1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_door_next)

        /* prows_metric_worst(ring, ring_size, at, count, last) -> rax */
        PROWS_ASM_FUNC(prows_metric_worst)
        PROWS_X64_LD5_WORST
        "test r8, r8\n   jz 4f\n   test r9d, r9d\n   jz 4f\n   cmp r11d, ecx\n   cmovb ecx, r11d\n"
        "cmp r9d, ecx\n   cmovb ecx, r9d\n   test ecx, ecx\n   jz 4f\n   cmp r10d, r9d\n   jb 1f\n"
        "mov eax, r10d\n   xor edx, edx\n   div r9d\n   mov r10d, edx\n"
        "1:  xor eax, eax\n"
        "2:  test r10d, r10d\n   cmovz r10d, r9d\n   dec r10d\n   mov rdx, qword ptr [r8 + r10*8]\n"
        "cmp rdx, rax\n   cmova rax, rdx\n   dec ecx\n   jnz 2b\n" PROWS_ASM_RET
        "4:  xor eax, eax\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_metric_worst)

        /* prows_popcount_mask(bits, count) -> eax */
        PROWS_ASM_FUNC(prows_popcount_mask)
        PROWS_X64_LD2_POP
        "test ecx, ecx\n   jz 2f\n   cmp ecx, 32\n   jae 1f\n   mov edx, 1\n   shl edx, cl\n   dec edx\n   and eax, edx\n"
        "1:  mov edx, eax\n   shr edx, 1\n   and edx, 0x55555555\n   sub eax, edx\n"
        "mov edx, eax\n   shr edx, 2\n   and eax, 0x33333333\n   and edx, 0x33333333\n   add eax, edx\n"
        "mov edx, eax\n   shr edx, 4\n   add eax, edx\n   and eax, 0x0f0f0f0f\n   imul eax, eax, 0x01010101\n   shr eax, 24\n" PROWS_ASM_RET
        "2:  xor eax, eax\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_popcount_mask)

        /* prows_frame_dated(last, period, stamp) -> rax */
        PROWS_ASM_FUNC(prows_frame_dated)
        PROWS_X64_LD3
        "mov rax, r10\n   test r8, r8\n   jz 1f\n   test r9, r9\n   jz 1f\n"
        "lea rcx, [r8 + r9]\n   mov rdx, r10\n   sub rdx, rcx\n   jb 1f\n   cmp rdx, r9\n   cmovb rax, rcx\n"
        "1:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_frame_dated)

        /* prows_present_due(presented, period, vblank, lead, now) -> rax */
        PROWS_ASM_FUNC(prows_present_due)
        PROWS_X64_LD5_DUE
        "mov rdx, rcx\n   sub rdx, r8\n   lea rax, [r8 + r9]\n   cmp rdx, r9\n   cmovae rax, rcx\n"
        "test r11, r11\n   jz 1f\n   test r9, r9\n   jz 1f\n   cmp r11, r9\n   jae 1f\n"
        "test r10, r10\n   jz 1f\n   mov rdx, rcx\n   sub rdx, r10\n   jb 1f\n   cmp rdx, r9\n   jae 1f\n"
        "lea rax, [r10 + r9]\n   sub rax, r11\n   cmp rax, rcx\n   cmovb rax, rcx\n"
        "mov rdx, r9\n   shr rdx, 1\n   add rdx, r8\n   cmp rax, rdx\n   cmovb rax, rdx\n"
        "1:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_present_due)

        /* prows_lead_of(worst, timed, widen, period) -> rax */
        PROWS_ASM_FUNC(prows_lead_of)
        PROWS_X64_LD4
        "xor eax, eax\n   cmp r9d, 4\n   jb 1f\n   test r11, r11\n   jz 1f\n   cmp r10, r11\n   jae 1f\n"
        "mov rcx, r8\n   shr rcx, 1\n   lea rcx, [r8 + rcx + 1000000]\n   add rcx, r10\n"
        "cmp rcx, r11\n   cmovb rax, rcx\n"
        "1:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_lead_of)

        /* prows_cadence_slot(due, flip_when, period, lead, cadence_k) -> rax */
        PROWS_ASM_FUNC(prows_cadence_slot)
        PROWS_X64_LD5_CAD
        "cmp ecx, 2\n   jb 2f\n   test r8, r8\n   jz 2f\n   test r9, r9\n   jz 2f\n"
        "mov rdx, r9\n   shr rdx, 1\n   test r10, r10\n   jz 1f\n   cmp r10, r9\n   cmovb rdx, r10\n"
        "1:  mov ecx, ecx\n   imul rcx, r9\n   add rcx, r8\n   sub rcx, rdx\n   cmp rax, rcx\n   cmovb rax, rcx\n"
        "2:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_cadence_slot)

        /* prows_late_landed(state, when, period) */
        PROWS_ASM_FUNC(prows_late_landed)
        PROWS_X64_LD3
        "test r8, r8\n   jz 6f\n   lea r11, [r8 + 8]\n   lea rax, [r8 + 24]\n"
        "cmp dword ptr [r8 + 4], 0\n   cmovne r11, rax\n"
        "mov ecx, dword ptr [r11 + 4]\n   mov eax, 120\n   test ecx, ecx\n   cmovz ecx, eax\n"
        "mov rax, qword ptr [r8 + 48]\n   test rax, rax\n   jz 1f\n   cmp r9, rax\n   jbe 1f\n"
        "mov rdx, r9\n   sub rdx, rax\n   cmp rdx, 250000000\n   jb 1f\n   mov dword ptr [r8 + 40], 0\n"
        "1:  mov qword ptr [r8 + 48], r9\n   mov rax, qword ptr [r8 + 56]\n   test rax, rax\n   jz 3f\n"
        "cmp r9, rax\n   jbe 3f\n   mov rdx, r9\n   sub rdx, rax\n   mov rax, r10\n   shr rax, 1\n"
        "cmp rdx, rax\n   jb 3f\n   inc qword ptr [r8 + 72]\n   cmp dword ptr [r8 + 40], 8\n   jb 5f\n"
        "mov dword ptr [r11], 0\n   cmp qword ptr [r11 + 8], 0\n   jne 2f\n"
        "mov rax, r10\n   shr rax, 2\n   mov qword ptr [r11 + 8], rax\n   jmp 5f\n"
        "2:  mov qword ptr [r11 + 8], r10\n   shl ecx, 1\n   mov eax, 7680\n   cmp ecx, eax\n   cmova ecx, eax\n"
        "mov dword ptr [r11 + 4], ecx\n   jmp 5f\n"
        "3:  mov rax, qword ptr [r11 + 8]\n   test rax, rax\n   jz 5f\n   mov edx, dword ptr [r11]\n"
        "inc edx\n   mov dword ptr [r11], edx\n   cmp edx, ecx\n   jb 5f\n   mov dword ptr [r11], 0\n"
        "cmp rax, r10\n   jb 4f\n   mov rax, r10\n   shr rax, 2\n   mov qword ptr [r11 + 8], rax\n   jmp 5f\n"
        "4:  mov qword ptr [r11 + 8], 0\n   mov dword ptr [r11 + 4], 0\n"
        "5:  cmp dword ptr [r8 + 40], 8\n   jae 7f\n   inc dword ptr [r8 + 40]\n"
        "7:  mov qword ptr [r8 + 56], 0\n"
        "6:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_late_landed)

        ".att_syntax prefix\n"
);

#elif defined(PROWS_RISCV64)

__asm__(
        PROWS_ASM_SECTION

        /* prows_next(a0=words, a1=nwords, a2=limit, a3=from) -> a0 */
        PROWS_ASM_FUNC(prows_next)
        "beqz a0, 3f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   slli a2, a2, 32\n   srli a2, a2, 32\n"
        "sext.w a3, a3\n   bgez a3, 1f\n   li a3, 0\n"
        "1:  bgeu a3, a2, 3f\n   srli t0, a3, 6\n   bgeu t0, a1, 3f\n   li t1, -1\n   sll t1, t1, a3\n"
        "slli t2, t0, 3\n   add t2, a0, t2\n   ld t3, 0(t2)\n   and t3, t3, t1\n   bnez t3, 2f\n"
        "4:  addi t0, t0, 1\n   bgeu t0, a1, 3f\n   addi t2, t2, 8\n   ld t3, 0(t2)\n   beqz t3, 4b\n"
        "2:  li t1, 0\n   slli t4, t3, 32\n   bnez t4, 5f\n   srli t3, t3, 32\n   addi t1, t1, 32\n"
        "5:  slli t4, t3, 48\n   bnez t4, 6f\n   srli t3, t3, 16\n   addi t1, t1, 16\n"
        "6:  slli t4, t3, 56\n   bnez t4, 7f\n   srli t3, t3, 8\n   addi t1, t1, 8\n"
        "7:  slli t4, t3, 60\n   bnez t4, 8f\n   srli t3, t3, 4\n   addi t1, t1, 4\n"
        "8:  slli t4, t3, 62\n   bnez t4, 9f\n   srli t3, t3, 2\n   addi t1, t1, 2\n"
        "9:  andi t4, t3, 1\n   bnez t4, 10f\n   addi t1, t1, 1\n"
        "10: slli t0, t0, 6\n   add t0, t0, t1\n   bgeu t0, a2, 3f\n   sext.w a0, t0\n" PROWS_ASM_RET
        "3:  li a0, -1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_next)

        /* prows_any(a0=words, a1=nwords, a2=last_word) -> a0 */
        PROWS_ASM_FUNC(prows_any)
        "beqz a0, 3f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   li t0, 0\n"
        "1:  bgeu t0, a1, 2f\n   slli t1, t0, 3\n   add t1, a0, t1\n   ld t2, 0(t1)\n   bnez t2, 4f\n   addi t0, t0, 1\n   j 1b\n"
        "2:  sext.w t3, a2\n   addi t3, t3, 1\n   beqz t3, 3f\n   slli a2, a2, 32\n   srli a2, a2, 32\n"
        "bltu a2, a1, 3f\n   slli t1, a2, 3\n   add t1, a0, t1\n   ld t2, 0(t1)\n   bnez t2, 4f\n"
        "3:  li a0, 0\n" PROWS_ASM_RET
        "4:  li a0, 1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_any)

        /* prows_none(a0=words, a1=nwords, a2=last_word) */
        PROWS_ASM_FUNC(prows_none)
        "beqz a0, 3f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   li t0, 0\n"
        "1:  bgeu t0, a1, 2f\n   slli t1, t0, 3\n   add t1, a0, t1\n   sd zero, 0(t1)\n   addi t0, t0, 1\n   j 1b\n"
        "2:  sext.w t3, a2\n   addi t3, t3, 1\n   beqz t3, 3f\n   slli a2, a2, 32\n   srli a2, a2, 32\n"
        "bltu a2, a1, 3f\n   slli t1, a2, 3\n   add t1, a0, t1\n   sd zero, 0(t1)\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_none)

        /* prows_or(a0=into, a1=with, a2=nwords, a3=last_word) */
        PROWS_ASM_FUNC(prows_or)
        "beqz a0, 3f\n   beqz a1, 3f\n   slli a2, a2, 32\n   srli a2, a2, 32\n   li t0, 0\n"
        "1:  bgeu t0, a2, 2f\n   slli t1, t0, 3\n   add t2, a0, t1\n   add t3, a1, t1\n"
        "ld t4, 0(t2)\n   ld t5, 0(t3)\n   or t4, t4, t5\n   sd t4, 0(t2)\n   addi t0, t0, 1\n   j 1b\n"
        "2:  sext.w t6, a3\n   addi t6, t6, 1\n   beqz t6, 3f\n   slli a3, a3, 32\n   srli a3, a3, 32\n"
        "bltu a3, a2, 3f\n   slli t1, a3, 3\n   add t2, a0, t1\n   add t3, a1, t1\n"
        "ld t4, 0(t2)\n   ld t5, 0(t3)\n   or t4, t4, t5\n   sd t4, 0(t2)\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_or)

        /* prows_and_not(a0=into, a1=without, a2=nwords, a3=last_word) */
        PROWS_ASM_FUNC(prows_and_not)
        "beqz a0, 3f\n   beqz a1, 3f\n   slli a2, a2, 32\n   srli a2, a2, 32\n   li t0, 0\n"
        "1:  bgeu t0, a2, 2f\n   slli t1, t0, 3\n   add t2, a0, t1\n   add t3, a1, t1\n"
        "ld t4, 0(t2)\n   ld t5, 0(t3)\n   not t5, t5\n   and t4, t4, t5\n   sd t4, 0(t2)\n   addi t0, t0, 1\n   j 1b\n"
        "2:  sext.w t6, a3\n   addi t6, t6, 1\n   beqz t6, 3f\n   slli a3, a3, 32\n   srli a3, a3, 32\n"
        "bltu a3, a2, 3f\n   slli t1, a3, 3\n   add t2, a0, t1\n   add t3, a1, t1\n"
        "ld t4, 0(t2)\n   ld t5, 0(t3)\n   not t5, t5\n   and t4, t4, t5\n   sd t4, 0(t2)\n"
        "3:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_and_not)

        /* prows_take(a0=into, a1=from, a2=nwords, a3=last_word) */
        PROWS_ASM_FUNC(prows_take)
        "beqz a0, 5f\n   beqz a1, 5f\n   slli a2, a2, 32\n   srli a2, a2, 32\n   li t0, 0\n"
        "1:  bgeu t0, a2, 3f\n   slli t1, t0, 3\n   add t2, a1, t1\n   ld t3, 0(t2)\n   beqz t3, 2f\n"
        "amoswap.d.aqrl t3, zero, (t2)\n"
        "2:  add t4, a0, t1\n   sd t3, 0(t4)\n   addi t0, t0, 1\n   j 1b\n"
        "3:  sext.w t6, a3\n   addi t6, t6, 1\n   beqz t6, 5f\n   slli a3, a3, 32\n   srli a3, a3, 32\n"
        "bltu a3, a2, 5f\n   slli t1, a3, 3\n   add t2, a1, t1\n   ld t3, 0(t2)\n   beqz t3, 4f\n"
        "amoswap.d.aqrl t3, zero, (t2)\n"
        "4:  add t4, a0, t1\n   sd t3, 0(t4)\n"
        "5:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_take)

        /* prows_door_next(a0=words, a1=nwords, a2=last_word) -> a0 */
        PROWS_ASM_FUNC(prows_door_next)
        "beqz a0, 11f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   li t0, 0\n"
        "1:  bgeu t0, a1, 3f\n   slli t1, t0, 3\n   add t1, a0, t1\n   ld t2, 0(t1)\n   fence r, rw\n   beqz t2, 2f\n"
        "12: neg t3, t2\n   and t3, t2, t3\n   not t4, t3\n   amoand.d.aqrl t5, t4, (t1)\n"
        "and t5, t5, t3\n   bnez t5, 5f\n   ld t2, 0(t1)\n   fence r, rw\n   bnez t2, 12b\n"
        "2:  addi t0, t0, 1\n   j 1b\n"
        "3:  sext.w t6, a2\n   addi t6, t6, 1\n   beqz t6, 11f\n   slli a2, a2, 32\n   srli a2, a2, 32\n"
        "bltu a2, a1, 11f\n   mv t0, a2\n   slli t1, t0, 3\n   add t1, a0, t1\n   ld t2, 0(t1)\n   fence r, rw\n   beqz t2, 11f\n"
        "4:  neg t3, t2\n   and t3, t2, t3\n   not t4, t3\n   amoand.d.aqrl t5, t4, (t1)\n"
        "and t5, t5, t3\n   bnez t5, 5f\n   ld t2, 0(t1)\n   fence r, rw\n   bnez t2, 4b\n   j 11f\n"
        "5:  li t1, 0\n   slli t4, t3, 32\n   bnez t4, 6f\n   srli t3, t3, 32\n   addi t1, t1, 32\n"
        "6:  slli t4, t3, 48\n   bnez t4, 7f\n   srli t3, t3, 16\n   addi t1, t1, 16\n"
        "7:  slli t4, t3, 56\n   bnez t4, 8f\n   srli t3, t3, 8\n   addi t1, t1, 8\n"
        "8:  slli t4, t3, 60\n   bnez t4, 9f\n   srli t3, t3, 4\n   addi t1, t1, 4\n"
        "9:  slli t4, t3, 62\n   bnez t4, 10f\n   srli t3, t3, 2\n   addi t1, t1, 2\n"
        "10: andi t4, t3, 1\n   bnez t4, 13f\n   addi t1, t1, 1\n"
        "13: slli t0, t0, 6\n   add a0, t0, t1\n" PROWS_ASM_RET
        "11: li a0, -1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_door_next)

        /* prows_metric_worst(a0=ring, a1=ring_size, a2=at, a3=count, a4=last) -> a0 */
        PROWS_ASM_FUNC(prows_metric_worst)
        "beqz a0, 5f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   beqz a1, 5f\n"
        "slli a2, a2, 32\n   srli a2, a2, 32\n   slli a3, a3, 32\n   srli a3, a3, 32\n   slli a4, a4, 32\n   srli a4, a4, 32\n"
        "bgeu a3, a4, 1f\n   mv a4, a3\n"
        "1:  bgeu a1, a4, 2f\n   mv a4, a1\n"
        "2:  beqz a4, 5f\n   bltu a2, a1, 3f\n   remu a2, a2, a1\n"
        "3:  li t0, 0\n"
        "4:  bnez a2, 6f\n   mv a2, a1\n"
        "6:  addi a2, a2, -1\n   slli t1, a2, 3\n   add t1, a0, t1\n   ld t2, 0(t1)\n"
        "bleu t2, t0, 7f\n   mv t0, t2\n"
        "7:  addi a4, a4, -1\n   bnez a4, 4b\n   mv a0, t0\n" PROWS_ASM_RET
        "5:  li a0, 0\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_metric_worst)

        /* prows_popcount_mask(a0=bits, a1=count) -> a0 */
        PROWS_ASM_FUNC(prows_popcount_mask)
        "slli a0, a0, 32\n   srli a0, a0, 32\n   slli a1, a1, 32\n   srli a1, a1, 32\n   beqz a1, 2f\n"
        "li t0, 32\n   bgeu a1, t0, 1f\n   li t0, 1\n   sll t0, t0, a1\n   addi t0, t0, -1\n   and a0, a0, t0\n"
        "1:  srli t0, a0, 1\n   li t1, 0x55555555\n   and t0, t0, t1\n   sub a0, a0, t0\n"
        "srli t0, a0, 2\n   li t1, 0x33333333\n   and a0, a0, t1\n   and t0, t0, t1\n   add a0, a0, t0\n"
        "srli t0, a0, 4\n   add a0, a0, t0\n   li t1, 0x0f0f0f0f\n   and a0, a0, t1\n"
        "li t1, 0x01010101\n   mulw a0, a0, t1\n   srliw a0, a0, 24\n" PROWS_ASM_RET
        "2:  li a0, 0\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_popcount_mask)

        /* prows_frame_dated(a0=last, a1=period, a2=stamp) -> a0 */
        PROWS_ASM_FUNC(prows_frame_dated)
        "beqz a0, 1f\n   beqz a1, 1f\n   add t0, a0, a1\n   bltu a2, t0, 1f\n"
        "sub t1, a2, t0\n   bgeu t1, a1, 1f\n   mv a0, t0\n" PROWS_ASM_RET
        "1:  mv a0, a2\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_frame_dated)

        /* prows_present_due(a0=presented, a1=period, a2=vblank, a3=lead, a4=now) -> a0 */
        PROWS_ASM_FUNC(prows_present_due)
        "sub t0, a4, a0\n   add t1, a0, a1\n   bltu t0, a1, 1f\n   mv t1, a4\n"
        "1:  beqz a3, 4f\n   beqz a1, 4f\n   bgeu a3, a1, 4f\n   beqz a2, 4f\n"
        "bltu a4, a2, 4f\n   sub t0, a4, a2\n   bgeu t0, a1, 4f\n"
        "add t2, a2, a1\n   sub t2, t2, a3\n   bleu a4, t2, 2f\n   mv t2, a4\n"
        "2:  srli t3, a1, 1\n   add t3, a0, t3\n   bleu t2, t3, 3f\n   mv a0, t2\n" PROWS_ASM_RET
        "3:  mv a0, t3\n" PROWS_ASM_RET
        "4:  mv a0, t1\n" PROWS_ASM_RET
        PROWS_ASM_END(prows_present_due)

        /* prows_lead_of(a0=worst, a1=timed, a2=widen, a3=period) -> a0 */
        PROWS_ASM_FUNC(prows_lead_of)
        "slli a1, a1, 32\n   srli a1, a1, 32\n   li t0, 4\n   bltu a1, t0, 1f\n   beqz a3, 1f\n   bgeu a2, a3, 1f\n"
        "srli t0, a0, 1\n   add a0, a0, t0\n   li t1, 1000000\n   add a0, a0, t1\n   add a0, a0, a2\n"
        "bltu a0, a3, 2f\n"
        "1:  li a0, 0\n"
        "2:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_lead_of)

        /* prows_cadence_slot(a0=due, a1=flip_when, a2=period, a3=lead, a4=cadence_k) -> a0 */
        PROWS_ASM_FUNC(prows_cadence_slot)
        "slli a4, a4, 32\n   srli a4, a4, 32\n   li t0, 2\n   bltu a4, t0, 2f\n   beqz a1, 2f\n   beqz a2, 2f\n"
        "srli t1, a2, 1\n   beqz a3, 1f\n   bgeu a3, a2, 1f\n   mv t1, a3\n"
        "1:  mul t2, a4, a2\n   add t2, t2, a1\n   sub t2, t2, t1\n   bleu t2, a0, 2f\n   mv a0, t2\n"
        "2:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_cadence_slot)

        /* prows_late_landed(a0=state, a1=when, a2=period) */
        PROWS_ASM_FUNC(prows_late_landed)
        "beqz a0, 8f\n   lw t0, 4(a0)\n   addi t1, a0, 8\n   beqz t0, 1f\n   addi t1, a0, 24\n"
        "1:  lwu t2, 4(t1)\n   bnez t2, 2f\n   li t2, 120\n"
        "2:  ld t3, 48(a0)\n   beqz t3, 3f\n   bleu a1, t3, 3f\n   sub t4, a1, t3\n   li t5, 250000000\n"
        "bltu t4, t5, 3f\n   sw zero, 40(a0)\n"
        "3:  sd a1, 48(a0)\n   ld t3, 56(a0)\n   beqz t3, 5f\n   bleu a1, t3, 5f\n   sub t4, a1, t3\n"
        "srli t5, a2, 1\n   bltu t4, t5, 5f\n   ld t6, 72(a0)\n   addi t6, t6, 1\n   sd t6, 72(a0)\n"
        "lwu t6, 40(a0)\n   li t5, 8\n   bltu t6, t5, 7f\n   sw zero, 0(t1)\n   ld t6, 8(t1)\n   bnez t6, 4f\n"
        "srli t6, a2, 2\n   sd t6, 8(t1)\n   j 7f\n"
        "4:  sd a2, 8(t1)\n   slliw t2, t2, 1\n   slli t2, t2, 32\n   srli t2, t2, 32\n   li t5, 7680\n"
        "bltu t2, t5, 10f\n   mv t2, t5\n"
        "10: sw t2, 4(t1)\n   j 7f\n"
        "5:  ld t6, 8(t1)\n   beqz t6, 7f\n   lwu t4, 0(t1)\n   addi t4, t4, 1\n   slli t4, t4, 32\n   srli t4, t4, 32\n"
        "sw t4, 0(t1)\n   bltu t4, t2, 7f\n   sw zero, 0(t1)\n   bltu t6, a2, 6f\n   srli t6, a2, 2\n   sd t6, 8(t1)\n   j 7f\n"
        "6:  sd zero, 8(t1)\n   sw zero, 4(t1)\n"
        "7:  lwu t6, 40(a0)\n   li t5, 8\n   bgeu t6, t5, 9f\n   addi t6, t6, 1\n   sw t6, 40(a0)\n"
        "9:  sd zero, 56(a0)\n"
        "8:  " PROWS_ASM_RET
        PROWS_ASM_END(prows_late_landed)
);

#endif

#endif /* COMPOSITOR_PACING_ROWS_ASM_IMPLEMENTATION && !COMPOSITOR_PACING_ROWS_ASM_EMITTED */
