/*
        PulseAudio native protocol tag-struct codec primitives in pure assembly:
          - aw_be32, aw_put_be32, aw_strlen
          - Readers: aw_r_tag, aw_r_u8, aw_r_u32, aw_r_bool, aw_r_string,
                     aw_r_sample_spec, aw_r_channel_map, aw_r_cvolume,
                     aw_r_timeval, aw_r_arbitrary
          - Writers: aw_w_raw, aw_w_byte, aw_w_be32, aw_w_tagged32, aw_w_bool,
                     aw_w_u8, aw_w_64, aw_w_sample_spec, aw_w_channel_map,
                     aw_w_string, aw_w_cvolume

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define AUDIO_WIRE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_AUDIO_WIRE_ASM_H
#define MOONWATER_AUDIO_WIRE_ASM_H

#if defined(STANDARD_MODERN_C)
typedef p8  aw_u8;
typedef b8  aw_i8;
typedef p32 aw_u32;
typedef b32 aw_i32;
typedef p64 aw_u64;
typedef b64 aw_i64;
#elif defined(__UINT8_TYPE__) && defined(__INT8_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  aw_u8;
typedef __INT8_TYPE__   aw_i8;
typedef __UINT32_TYPE__ aw_u32;
typedef __INT32_TYPE__  aw_i32;
typedef __UINT64_TYPE__ aw_u64;
typedef __INT64_TYPE__  aw_i64;
#else
#include <stdint.h>
typedef uint8_t  aw_u8;
typedef int8_t   aw_i8;
typedef uint32_t aw_u32;
typedef int32_t  aw_i32;
typedef uint64_t aw_u64;
typedef int64_t  aw_i64;
#endif

/* 32 bytes — byte-for-byte identical to struct wire_w / struct wire_r on LP64 */
struct aw_w {
        aw_u8 *p;       /* +0  */
        aw_i64 cap;     /* +8  */
        aw_i64 len;     /* +16 */
        aw_i32 bad;     /* +24 */
        aw_i32 _pad;    /* +28 */
};

struct aw_r {
        const aw_u8 *p; /* +0  */
        aw_i64 length;  /* +8  */
        aw_i64 at;      /* +16 */
        aw_i32 bad;     /* +24 */
        aw_i32 _pad;    /* +28 */
};

#ifdef offset_of
#define AW_OFFSET(T, M) offset_of(T, M)
#else
#define AW_OFFSET(T, M) ((aw_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct aw_w) == 32, "aw_w must be 32 bytes");
_Static_assert(AW_OFFSET(struct aw_w, p)   == 0  && AW_OFFSET(struct aw_w, cap) == 8 &&
               AW_OFFSET(struct aw_w, len) == 16 && AW_OFFSET(struct aw_w, bad) == 24,
               "aw_w layout mismatch");
_Static_assert(sizeof(struct aw_r) == 32, "aw_r must be 32 bytes");
_Static_assert(AW_OFFSET(struct aw_r, p)      == 0  && AW_OFFSET(struct aw_r, length) == 8 &&
               AW_OFFSET(struct aw_r, at)     == 16 && AW_OFFSET(struct aw_r, bad)    == 24,
               "aw_r layout mismatch");

aw_u32       aw_be32(const aw_u8 *p);
void         aw_put_be32(aw_u8 *to, aw_u32 v);
aw_i64       aw_strlen(const char *s);

aw_i32       aw_r_tag(struct aw_r *r, aw_u32 tag);
aw_u32       aw_r_u8(struct aw_r *r);
aw_u32       aw_r_u32(struct aw_r *r);
aw_i32       aw_r_bool(struct aw_r *r);
const char  *aw_r_string(struct aw_r *r, aw_i64 limit, aw_i32 *isnull);
void         aw_r_sample_spec(struct aw_r *r, aw_i64 *format, aw_i64 *channels, aw_i64 *rate);
aw_i64       aw_r_channel_map(struct aw_r *r, aw_i8 *map, aw_i64 out_cap, aw_i64 max_ch);
aw_i64       aw_r_cvolume(struct aw_r *r, aw_u64 *volume, aw_i64 max_ch, aw_u64 vol_max);
void         aw_r_timeval(struct aw_r *r, aw_u64 *seconds, aw_u64 *microseconds);
const aw_u8 *aw_r_arbitrary(struct aw_r *r, aw_u64 n);

void         aw_w_raw(struct aw_w *w, const void *data, aw_i64 n);
void         aw_w_byte(struct aw_w *w, aw_u32 c);
void         aw_w_be32(struct aw_w *w, aw_u32 v);
void         aw_w_tagged32(struct aw_w *w, aw_u32 tag, aw_u32 v);
void         aw_w_bool(struct aw_w *w, aw_i32 v);
void         aw_w_u8(struct aw_w *w, aw_u32 v);
void         aw_w_64(struct aw_w *w, aw_u32 tag, aw_u64 v);
void         aw_w_sample_spec(struct aw_w *w, aw_i64 format, aw_i64 channels, aw_i64 rate);
void         aw_w_channel_map(struct aw_w *w, const aw_i8 *map, aw_i64 channels, aw_i64 map_cap, aw_u32 aux0);
void         aw_w_string(struct aw_w *w, const char *s);
void         aw_w_cvolume(struct aw_w *w, const aw_u64 *volume, aw_i64 channels);

#endif /* MOONWATER_AUDIO_WIRE_ASM_H */

#ifdef AUDIO_WIRE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define AW_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define AW_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define AW_RISCV64 1
# else
#  error "src/audio/wire.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define AW_ARM64 1
# elif defined(X64) && X64
#  define AW_X64 1
# elif defined(RISCV64) && RISCV64
#  define AW_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define AW_ASM_SECTION   ASM_SECTION
# define AW_ASM_FUNC(n)   ASM_FUNC(n)
# define AW_ASM_END(n)    ASM_END(n)
# define AW_ASM_RET       ASM_RET
#else
# define AW_ASM_SECTION   ".text\n"
# define AW_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define AW_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define AW_ASM_END(n)   ""
# elif defined(_WIN32)
#  define AW_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define AW_ASM_END(n)   ""
# else
#  if defined(AW_ARM64)
#   define AW_ASM_FTYPE   "%function"
#  else
#   define AW_ASM_FTYPE   "@function"
#  endif
#  define AW_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " AW_ASM_FTYPE "\n" #n ":\n"
#  define AW_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(AW_ARM64)

__asm__(
        AW_ASM_SECTION

        /* aw_be32(x0=p) -> w0 */
        AW_ASM_FUNC(aw_be32)
        "cbz x0, 1f\n   ldr w0, [x0]\n   rev w0, w0\n" AW_ASM_RET
        "1:  mov w0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_be32)

        /* aw_put_be32(x0=to, w1=v) */
        AW_ASM_FUNC(aw_put_be32)
        "cbz x0, 1f\n   rev w1, w1\n   str w1, [x0]\n"
        "1:  " AW_ASM_RET
        AW_ASM_END(aw_put_be32)

        /* aw_strlen(x0=s) -> x0 */
        AW_ASM_FUNC(aw_strlen)
        "cbz x0, 2f\n   mov x1, x0\n"
        "1:  ldrb w2, [x1], #1\n   cbnz w2, 1b\n   sub x0, x1, x0\n   sub x0, x0, #1\n" AW_ASM_RET
        "2:  mov x0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_strlen)

        /* aw_r_tag(x0=r, w1=tag) -> w0 */
        AW_ASM_FUNC(aw_r_tag)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x3, x4, [x0]\n   cbz x3, 1f\n   tbnz x4, #63, 1f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 1f\n   ldrb w6, [x3, x5]\n   and w1, w1, #0xff\n   cmp w6, w1\n   b.ne 1f\n"
        "add x5, x5, #1\n   str x5, [x0, #16]\n   mov w0, #1\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  mov w0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_tag)

        /* aw_r_u8(x0=r) -> w0 */
        AW_ASM_FUNC(aw_r_u8)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x3, x4, [x0]\n   cbz x3, 1f\n   tbnz x4, #63, 1f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 1f\n   sub x6, x4, x5\n   cmp x6, #2\n   b.lo 1f\n"
        "add x7, x3, x5\n   ldrb w6, [x7]\n   cmp w6, #66\n   b.ne 1f\n   ldrb w6, [x7, #1]\n"
        "add x5, x5, #2\n   str x5, [x0, #16]\n   mov w0, w6\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  mov w0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_u8)

        /* aw_r_u32(x0=r) -> w0 */
        AW_ASM_FUNC(aw_r_u32)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x3, x4, [x0]\n   cbz x3, 1f\n   tbnz x4, #63, 1f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 1f\n   sub x6, x4, x5\n   cmp x6, #5\n   b.lo 1f\n"
        "add x7, x3, x5\n   ldrb w6, [x7]\n   cmp w6, #76\n   b.ne 1f\n   ldr w6, [x7, #1]\n   rev w6, w6\n"
        "add x5, x5, #5\n   str x5, [x0, #16]\n   mov w0, w6\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  mov w0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_u32)

        /* aw_r_bool(x0=r) -> w0 */
        AW_ASM_FUNC(aw_r_bool)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x3, x4, [x0]\n   cbz x3, 1f\n   tbnz x4, #63, 1f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 1f\n   ldrb w6, [x3, x5]\n   sub w7, w6, #48\n   cmp w7, #1\n   b.hi 1f\n"
        "add x5, x5, #1\n   str x5, [x0, #16]\n   mov w0, w7\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  mov w0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_bool)

        /* aw_r_string(x0=r, x1=limit, x2=isnull) -> x0 */
        AW_ASM_FUNC(aw_r_string)
        "cbz x2, 1f\n   str wzr, [x2]\n"
        "1:  cbz x0, 9f\n   ldr w3, [x0, #24]\n   cbnz w3, 9f\n   ldp x3, x4, [x0]\n   cbz x3, 8f\n   tbnz x4, #63, 8f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 8f\n   ldrb w6, [x3, x5]\n   cmp w6, #78\n   b.ne 2f\n"
        "add x5, x5, #1\n   str x5, [x0, #16]\n   cbz x2, 9f\n   mov w6, #1\n   str w6, [x2]\n   b 9f\n"
        "2:  cmp w6, #116\n   b.ne 8f\n   add x5, x5, #1\n   sub x6, x4, x5\n   cbz x6, 8f\n"
        "cmp x1, #0\n   b.le 3f\n   sub x7, x6, #1\n   cmp x7, x1\n   b.le 3f\n   add x6, x1, #1\n"
        "3:  add x7, x3, x5\n   mov x8, #0\n   cmp x6, #8\n   b.lo 6f\n"
        "mov x10, #0x0101010101010101\n   mov x11, #0x8080808080808080\n"
        "4:  ldr x9, [x7, x8]\n   sub x12, x9, x10\n   bic x12, x12, x9\n   ands x12, x12, x11\n   b.ne 7f\n"
        "add x8, x8, #8\n   sub x12, x6, x8\n   cmp x12, #8\n   b.hs 4b\n"
        "6:  cmp x8, x6\n   b.hs 8f\n   ldrb w9, [x7, x8]\n   cbz w9, 5f\n   add x8, x8, #1\n   b 6b\n"
        "7:  rbit x12, x12\n   clz x12, x12\n   add x8, x8, x12, lsr #3\n"
        "5:  add x5, x5, x8\n   add x5, x5, #1\n   str x5, [x0, #16]\n   mov x0, x7\n" AW_ASM_RET
        "8:  mov w3, #1\n   str w3, [x0, #24]\n"
        "9:  mov x0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_string)

        /* aw_r_sample_spec(x0=r, x1=format, x2=channels, x3=rate) */
        AW_ASM_FUNC(aw_r_sample_spec)
        "cbz x0, 5f\n   ldr w4, [x0, #24]\n   cbnz w4, 5f\n   ldp x4, x5, [x0]\n   cbz x4, 4f\n   tbnz x5, #63, 4f\n"
        "ldr x6, [x0, #16]\n   cmp x6, x5\n   b.hs 4f\n   sub x7, x5, x6\n   cmp x7, #7\n   b.lo 4f\n"
        "add x7, x4, x6\n   ldrb w8, [x7]\n   cmp w8, #97\n   b.ne 4f\n"
        "ldrb w8, [x7, #1]\n   ldrb w9, [x7, #2]\n   ldr w10, [x7, #3]\n   rev w10, w10\n"
        "add x6, x6, #7\n   str x6, [x0, #16]\n   cbz x1, 1f\n   str x8, [x1]\n"
        "1:  cbz x2, 2f\n   str x9, [x2]\n"
        "2:  cbz x3, 5f\n   str x10, [x3]\n" AW_ASM_RET
        "4:  mov w4, #1\n   str w4, [x0, #24]\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_r_sample_spec)

        /* aw_r_channel_map(x0=r, x1=map, x2=out_cap, x3=max_ch) -> x0 */
        AW_ASM_FUNC(aw_r_channel_map)
        "cbz x0, 9f\n   ldr w4, [x0, #24]\n   cbnz w4, 9f\n   ldp x4, x5, [x0]\n   cbz x4, 8f\n   tbnz x5, #63, 8f\n"
        "tbnz x3, #63, 8f\n   ldr x6, [x0, #16]\n   cmp x6, x5\n   b.hs 8f\n   sub x9, x5, x6\n   cmp x9, #2\n   b.lo 8f\n"
        "add x7, x4, x6\n   ldrb w8, [x7]\n   cmp w8, #109\n   b.ne 8f\n   ldrb w8, [x7, #1]\n"
        "cmp x8, x3\n   b.hi 8f\n   sub x9, x9, #2\n   cmp x9, x8\n   b.lo 8f\n"
        "add x6, x6, #2\n   add x6, x6, x8\n   str x6, [x0, #16]\n   cbz x1, 3f\n   cmp x2, #0\n   b.le 3f\n"
        "cmp x8, x2\n   csel x9, x8, x2, lo\n   add x7, x7, #2\n   mov x10, #0\n   mov w12, #63\n"
        "2:  cmp x10, x9\n   b.hs 3f\n   ldrb w11, [x7, x10]\n   cmp w11, #64\n   csel w11, w11, w12, lo\n"
        "strb w11, [x1, x10]\n   add x10, x10, #1\n   b 2b\n"
        "3:  mov x0, x8\n" AW_ASM_RET
        "8:  mov w4, #1\n   str w4, [x0, #24]\n"
        "9:  mov x0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_channel_map)

        /* aw_r_cvolume(x0=r, x1=volume, x2=max_ch, x3=vol_max) -> x0 */
        AW_ASM_FUNC(aw_r_cvolume)
        "cbz x0, 9f\n   ldr w4, [x0, #24]\n   cbnz w4, 9f\n   ldp x4, x5, [x0]\n   cbz x4, 8f\n   tbnz x5, #63, 8f\n"
        "tbnz x2, #63, 8f\n   ldr x6, [x0, #16]\n   cmp x6, x5\n   b.hs 8f\n   sub x9, x5, x6\n   cmp x9, #2\n   b.lo 8f\n"
        "add x7, x4, x6\n   ldrb w8, [x7]\n   cmp w8, #118\n   b.ne 8f\n   ldrb w8, [x7, #1]\n"
        "cmp x8, x2\n   b.hi 8f\n   sub x9, x9, #2\n   cmp x9, x8, lsl #2\n   b.lo 8f\n"
        "add x6, x6, #2\n   add x6, x6, x8, lsl #2\n   str x6, [x0, #16]\n   cbz x1, 3f\n   add x7, x7, #2\n   mov x9, #0\n"
        "2:  cmp x9, x8\n   b.hs 3f\n   ldr w10, [x7, x9, lsl #2]\n   rev w10, w10\n   cmp x10, x3\n   csel x10, x3, x10, hi\n"
        "str x10, [x1, x9, lsl #3]\n   add x9, x9, #1\n   b 2b\n"
        "3:  mov x0, x8\n" AW_ASM_RET
        "8:  mov w4, #1\n   str w4, [x0, #24]\n"
        "9:  mov x0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_cvolume)

        /* aw_r_timeval(x0=r, x1=seconds, x2=microseconds) */
        AW_ASM_FUNC(aw_r_timeval)
        "cbz x0, 5f\n   ldr w3, [x0, #24]\n   cbnz w3, 5f\n   ldp x3, x4, [x0]\n   cbz x3, 4f\n   tbnz x4, #63, 4f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 4f\n   sub x6, x4, x5\n   cmp x6, #9\n   b.lo 4f\n"
        "add x6, x3, x5\n   ldrb w7, [x6]\n   cmp w7, #84\n   b.ne 4f\n"
        "ldr w7, [x6, #1]\n   rev w7, w7\n   ldr w8, [x6, #5]\n   rev w8, w8\n"
        "add x5, x5, #9\n   str x5, [x0, #16]\n   cbz x1, 1f\n   str x7, [x1]\n"
        "1:  cbz x2, 5f\n   str x8, [x2]\n" AW_ASM_RET
        "4:  mov w3, #1\n   str w3, [x0, #24]\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_r_timeval)

        /* aw_r_arbitrary(x0=r, x1=n) -> x0 */
        AW_ASM_FUNC(aw_r_arbitrary)
        "cbz x0, 9f\n   ldr w2, [x0, #24]\n   cbnz w2, 9f\n   ldp x3, x4, [x0]\n   cbz x3, 8f\n   tbnz x4, #63, 8f\n"
        "ldr x5, [x0, #16]\n   cmp x5, x4\n   b.hs 8f\n   sub x6, x4, x5\n   cmp x6, #5\n   b.lo 8f\n"
        "add x7, x3, x5\n   ldrb w8, [x7]\n   cmp w8, #120\n   b.ne 8f\n   ldr w8, [x7, #1]\n   rev w8, w8\n"
        "cmp x8, x1\n   b.ne 8f\n   sub x6, x6, #5\n   cmp x6, x1\n   b.lo 8f\n"
        "add x5, x5, #5\n   add x5, x5, x1\n   str x5, [x0, #16]\n   add x0, x7, #5\n" AW_ASM_RET
        "8:  mov w2, #1\n   str w2, [x0, #24]\n"
        "9:  mov x0, #0\n" AW_ASM_RET
        AW_ASM_END(aw_r_arbitrary)

        /* aw_w_raw(x0=w, x1=data, x2=n) */
        AW_ASM_FUNC(aw_w_raw)
        "cbz x0, 5f\n   ldr w3, [x0, #24]\n   cbnz w3, 5f\n   tbnz x2, #63, 4f\n   ldp x3, x4, [x0]\n   ldr x5, [x0, #16]\n"
        "tbnz x5, #63, 4f\n   cmp x4, x5\n   b.lt 4f\n   cbz x2, 5f\n   cbz x3, 4f\n   cbz x1, 4f\n"
        "sub x6, x4, x5\n   cmp x6, x2\n   b.lo 4f\n   add x6, x3, x5\n   add x5, x5, x2\n   str x5, [x0, #16]\n"
        "1:  cmp x2, #8\n   b.lo 2f\n   ldr x7, [x1], #8\n   str x7, [x6], #8\n   sub x2, x2, #8\n   b 1b\n"
        "2:  cbz x2, 5f\n   ldrb w7, [x1], #1\n   strb w7, [x6], #1\n   sub x2, x2, #1\n   b 2b\n"
        "4:  mov w3, #1\n   str w3, [x0, #24]\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_raw)

        /* aw_w_byte(x0=w, w1=c) */
        AW_ASM_FUNC(aw_w_byte)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x2, x3, [x0]\n   cbz x2, 1f\n   ldr x4, [x0, #16]\n"
        "tbnz x4, #63, 1f\n   cmp x3, x4\n   b.le 1f\n   strb w1, [x2, x4]\n   add x4, x4, #1\n   str x4, [x0, #16]\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_byte)

        /* aw_w_be32(x0=w, w1=v) */
        AW_ASM_FUNC(aw_w_be32)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x2, x3, [x0]\n   cbz x2, 1f\n   ldr x4, [x0, #16]\n"
        "tbnz x4, #63, 1f\n   cmp x3, x4\n   b.lt 1f\n   sub x5, x3, x4\n   cmp x5, #4\n   b.lo 1f\n"
        "rev w1, w1\n   str w1, [x2, x4]\n   add x4, x4, #4\n   str x4, [x0, #16]\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_be32)

        /* aw_w_tagged32(x0=w, w1=tag, w2=v) */
        AW_ASM_FUNC(aw_w_tagged32)
        "cbz x0, 2f\n   ldr w3, [x0, #24]\n   cbnz w3, 2f\n   ldp x3, x4, [x0]\n   cbz x3, 1f\n   ldr x5, [x0, #16]\n"
        "tbnz x5, #63, 1f\n   cmp x4, x5\n   b.lt 1f\n   sub x6, x4, x5\n   cmp x6, #5\n   b.lo 1f\n"
        "add x6, x3, x5\n   rev w2, w2\n   strb w1, [x6]\n   str w2, [x6, #1]\n   add x5, x5, #5\n   str x5, [x0, #16]\n" AW_ASM_RET
        "1:  mov w3, #1\n   str w3, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_tagged32)

        /* aw_w_bool(x0=w, w1=v) */
        AW_ASM_FUNC(aw_w_bool)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x2, x3, [x0]\n   cbz x2, 1f\n   ldr x4, [x0, #16]\n"
        "tbnz x4, #63, 1f\n   cmp x3, x4\n   b.le 1f\n   cmp w1, #0\n   cset w5, ne\n   add w5, w5, #48\n"
        "strb w5, [x2, x4]\n   add x4, x4, #1\n   str x4, [x0, #16]\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_bool)

        /* aw_w_u8(x0=w, w1=v) */
        AW_ASM_FUNC(aw_w_u8)
        "cbz x0, 2f\n   ldr w2, [x0, #24]\n   cbnz w2, 2f\n   ldp x2, x3, [x0]\n   cbz x2, 1f\n   ldr x4, [x0, #16]\n"
        "tbnz x4, #63, 1f\n   cmp x3, x4\n   b.lt 1f\n   sub x5, x3, x4\n   cmp x5, #2\n   b.lo 1f\n"
        "add x5, x2, x4\n   mov w6, #66\n   strb w6, [x5]\n   strb w1, [x5, #1]\n   add x4, x4, #2\n   str x4, [x0, #16]\n" AW_ASM_RET
        "1:  mov w2, #1\n   str w2, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_u8)

        /* aw_w_64(x0=w, w1=tag, x2=v) */
        AW_ASM_FUNC(aw_w_64)
        "cbz x0, 2f\n   ldr w3, [x0, #24]\n   cbnz w3, 2f\n   ldp x3, x4, [x0]\n   cbz x3, 1f\n   ldr x5, [x0, #16]\n"
        "tbnz x5, #63, 1f\n   cmp x4, x5\n   b.lt 1f\n   sub x6, x4, x5\n   cmp x6, #9\n   b.lo 1f\n"
        "add x6, x3, x5\n   rev x2, x2\n   strb w1, [x6]\n   str x2, [x6, #1]\n   add x5, x5, #9\n   str x5, [x0, #16]\n" AW_ASM_RET
        "1:  mov w3, #1\n   str w3, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_64)

        /* aw_w_sample_spec(x0=w, x1=format, x2=channels, x3=rate) */
        AW_ASM_FUNC(aw_w_sample_spec)
        "cbz x0, 2f\n   ldr w4, [x0, #24]\n   cbnz w4, 2f\n   ldp x4, x5, [x0]\n   cbz x4, 1f\n   ldr x6, [x0, #16]\n"
        "tbnz x6, #63, 1f\n   cmp x5, x6\n   b.lt 1f\n   sub x7, x5, x6\n   cmp x7, #7\n   b.lo 1f\n"
        "add x7, x4, x6\n   mov w8, #97\n   rev w3, w3\n   strb w8, [x7]\n   strb w1, [x7, #1]\n"
        "strb w2, [x7, #2]\n   str w3, [x7, #3]\n   add x6, x6, #7\n   str x6, [x0, #16]\n" AW_ASM_RET
        "1:  mov w4, #1\n   str w4, [x0, #24]\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_sample_spec)

        /* aw_w_channel_map(x0=w, x1=map, x2=channels, x3=map_cap, w4=aux0) */
        AW_ASM_FUNC(aw_w_channel_map)
        "cbz x0, 5f\n   ldr w5, [x0, #24]\n   cbnz w5, 5f\n   cmp x2, #255\n   b.hi 4f\n"
        "cbz x2, 1f\n   cmp x3, #0\n   b.le 1f\n   cbz x1, 4f\n"
        "1:  ldp x5, x6, [x0]\n   cbz x5, 4f\n   ldr x7, [x0, #16]\n   tbnz x7, #63, 4f\n   cmp x6, x7\n   b.lt 4f\n"
        "sub x8, x6, x7\n   add x9, x2, #2\n   cmp x8, x9\n   b.lo 4f\n   add x8, x5, x7\n   add x7, x7, x9\n   str x7, [x0, #16]\n"
        "mov w10, #109\n   strb w10, [x8]\n   strb w2, [x8, #1]\n   add x8, x8, #2\n   mov x9, #0\n"
        "2:  cmp x9, x2\n   b.hs 5f\n   cmp x9, x3\n   b.ge 3f\n   ldrb w10, [x1, x9]\n   strb w10, [x8, x9]\n   add x9, x9, #1\n   b 2b\n"
        "3:  add w10, w4, w9\n   strb w10, [x8, x9]\n   add x9, x9, #1\n   b 2b\n"
        "4:  mov w5, #1\n   str w5, [x0, #24]\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_channel_map)

        /* aw_w_string(x0=w, x1=s) */
        AW_ASM_FUNC(aw_w_string)
        "cbz x0, 5f\n   ldr w2, [x0, #24]\n   cbnz w2, 5f\n   ldp x2, x3, [x0]\n   cbz x2, 4f\n   ldr x4, [x0, #16]\n"
        "tbnz x4, #63, 4f\n   cmp x3, x4\n   b.lt 4f\n   sub x5, x3, x4\n   cbnz x1, 1f\n"
        "cbz x5, 4f\n   mov w6, #78\n   strb w6, [x2, x4]\n   add x4, x4, #1\n   str x4, [x0, #16]\n" AW_ASM_RET
        "1:  mov x6, x1\n"
        "2:  ldrb w7, [x6], #1\n   cbnz w7, 2b\n   sub x6, x6, x1\n   add x7, x6, #1\n   cmp x5, x7\n   b.lo 4f\n"
        "add x5, x2, x4\n   add x4, x4, x7\n   str x4, [x0, #16]\n   mov w7, #116\n   strb w7, [x5], #1\n"
        "3:  cmp x6, #8\n   b.lo 6f\n   ldr x7, [x1], #8\n   str x7, [x5], #8\n   sub x6, x6, #8\n   b 3b\n"
        "6:  cbz x6, 5f\n   ldrb w7, [x1], #1\n   strb w7, [x5], #1\n   sub x6, x6, #1\n   b 6b\n"
        "4:  mov w2, #1\n   str w2, [x0, #24]\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_string)

        /* aw_w_cvolume(x0=w, x1=volume, x2=channels) */
        AW_ASM_FUNC(aw_w_cvolume)
        "cbz x0, 5f\n   ldr w3, [x0, #24]\n   cbnz w3, 5f\n   cmp x2, #255\n   b.hi 4f\n   cbz x2, 1f\n   cbz x1, 4f\n"
        "1:  ldp x3, x4, [x0]\n   cbz x3, 4f\n   ldr x5, [x0, #16]\n   tbnz x5, #63, 4f\n   cmp x4, x5\n   b.lt 4f\n"
        "sub x6, x4, x5\n   lsl x7, x2, #2\n   add x7, x7, #2\n   cmp x6, x7\n   b.lo 4f\n"
        "add x6, x3, x5\n   add x5, x5, x7\n   str x5, [x0, #16]\n   mov w8, #118\n   strb w8, [x6]\n   strb w2, [x6, #1]\n"
        "add x6, x6, #2\n   mov x8, #0\n"
        "2:  cmp x8, x2\n   b.hs 5f\n   ldr x9, [x1, x8, lsl #3]\n   rev w9, w9\n   str w9, [x6, x8, lsl #2]\n   add x8, x8, #1\n   b 2b\n"
        "4:  mov w3, #1\n   str w3, [x0, #24]\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_cvolume)
);

#elif defined(AW_X64)

#if defined(_WIN32)
# define AW_X1  "mov r10, rcx\n"
# define AW_X2  "mov r10, rcx\n   mov r11, rdx\n"
# define AW_X3  "mov r10, rcx\n   mov r11, rdx\n   mov rdx, r8\n"
# define AW_X4  "mov r10, rcx\n   mov r11, rdx\n   mov rdx, r8\n   mov rcx, r9\n"
# define AW_X5  "mov r10, rcx\n   mov r11, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8d, dword ptr [rsp + 40]\n"
#else
# define AW_X1  "mov r10, rdi\n"
# define AW_X2  "mov r10, rdi\n   mov r11, rsi\n"
# define AW_X3  "mov r10, rdi\n   mov r11, rsi\n"
# define AW_X4  "mov r10, rdi\n   mov r11, rsi\n"
# define AW_X5  "mov r10, rdi\n   mov r11, rsi\n"
#endif

__asm__(
        AW_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* aw_be32(p) -> eax */
        AW_ASM_FUNC(aw_be32)
        AW_X1
        "xor eax, eax\n   test r10, r10\n   jz 1f\n   mov eax, dword ptr [r10]\n   bswap eax\n"
        "1:  " AW_ASM_RET
        AW_ASM_END(aw_be32)

        /* aw_put_be32(to, v) */
        AW_ASM_FUNC(aw_put_be32)
        AW_X2
        "test r10, r10\n   jz 1f\n   bswap r11d\n   mov dword ptr [r10], r11d\n"
        "1:  " AW_ASM_RET
        AW_ASM_END(aw_put_be32)

        /* aw_strlen(s) -> rax */
        AW_ASM_FUNC(aw_strlen)
        AW_X1
        "xor eax, eax\n   test r10, r10\n   jz 2f\n"
        "1:  cmp byte ptr [r10 + rax], 0\n   je 2f\n   inc rax\n   jmp 1b\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_strlen)

        /* aw_r_tag(r, tag) -> eax */
        AW_ASM_FUNC(aw_r_tag)
        AW_X2
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 1f\n   mov rax, [r10 + 16]\n   cmp rax, r9\n   jae 1f\n"
        "cmp byte ptr [r8 + rax], r11b\n   jne 1f\n   inc rax\n   mov [r10 + 16], rax\n   mov eax, 1\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_tag)

        /* aw_r_u8(r) -> eax */
        AW_ASM_FUNC(aw_r_u8)
        AW_X1
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 1f\n   mov rcx, [r10 + 16]\n   cmp rcx, r9\n   jae 1f\n"
        "sub r9, rcx\n   cmp r9, 2\n   jb 1f\n   cmp byte ptr [r8 + rcx], 66\n   jne 1f\n"
        "movzx eax, byte ptr [r8 + rcx + 1]\n   add rcx, 2\n   mov [r10 + 16], rcx\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_u8)

        /* aw_r_u32(r) -> eax */
        AW_ASM_FUNC(aw_r_u32)
        AW_X1
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 1f\n   mov rcx, [r10 + 16]\n   cmp rcx, r9\n   jae 1f\n"
        "sub r9, rcx\n   cmp r9, 5\n   jb 1f\n   cmp byte ptr [r8 + rcx], 76\n   jne 1f\n"
        "mov eax, dword ptr [r8 + rcx + 1]\n   bswap eax\n   add rcx, 5\n   mov [r10 + 16], rcx\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_u32)

        /* aw_r_bool(r) -> eax */
        AW_ASM_FUNC(aw_r_bool)
        AW_X1
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 1f\n   mov rcx, [r10 + 16]\n   cmp rcx, r9\n   jae 1f\n"
        "movzx eax, byte ptr [r8 + rcx]\n   sub eax, 48\n   cmp eax, 1\n   ja 1f\n   inc rcx\n   mov [r10 + 16], rcx\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_bool)

        /* aw_r_string(r, limit, isnull) -> rax */
        AW_ASM_FUNC(aw_r_string)
        AW_X3
        "test rdx, rdx\n   jz 1f\n   mov dword ptr [rdx], 0\n"
        "1:  test r10, r10\n   jz 9f\n   cmp dword ptr [r10 + 24], 0\n   jne 9f\n   mov r8, [r10]\n   test r8, r8\n   jz 8f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 8f\n   mov rcx, [r10 + 16]\n   cmp rcx, r9\n   jae 8f\n"
        "movzx eax, byte ptr [r8 + rcx]\n   cmp eax, 78\n   jne 2f\n   inc rcx\n   mov [r10 + 16], rcx\n"
        "test rdx, rdx\n   jz 9f\n   mov dword ptr [rdx], 1\n   jmp 9f\n"
        "2:  cmp eax, 116\n   jne 8f\n   inc rcx\n   sub r9, rcx\n   jz 8f\n"
        "test r11, r11\n   jle 3f\n   lea rax, [r9 - 1]\n   cmp rax, r11\n   jle 3f\n   lea r9, [r11 + 1]\n"
        "3:  add r8, rcx\n   xor eax, eax\n"
        "4:  cmp rax, r9\n   jae 8f\n   cmp byte ptr [r8 + rax], 0\n   je 5f\n   inc rax\n   jmp 4b\n"
        "5:  lea rcx, [rcx + rax + 1]\n   mov [r10 + 16], rcx\n   mov rax, r8\n" AW_ASM_RET
        "8:  mov dword ptr [r10 + 24], 1\n"
        "9:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_string)

        /* aw_r_sample_spec(r, format, channels, rate) */
        AW_ASM_FUNC(aw_r_sample_spec)
        AW_X4
        "test r10, r10\n   jz 5f\n   cmp dword ptr [r10 + 24], 0\n   jne 5f\n   mov r8, [r10]\n   test r8, r8\n   jz 4f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 4f\n   mov rax, [r10 + 16]\n   cmp rax, r9\n   jae 4f\n"
        "sub r9, rax\n   cmp r9, 7\n   jb 4f\n   add r8, rax\n   cmp byte ptr [r8], 97\n   jne 4f\n"
        "add rax, 7\n   mov [r10 + 16], rax\n   test r11, r11\n   jz 1f\n   movzx eax, byte ptr [r8 + 1]\n   mov [r11], rax\n"
        "1:  test rdx, rdx\n   jz 2f\n   movzx eax, byte ptr [r8 + 2]\n   mov [rdx], rax\n"
        "2:  test rcx, rcx\n   jz 5f\n   mov eax, dword ptr [r8 + 3]\n   bswap eax\n   mov [rcx], rax\n" AW_ASM_RET
        "4:  mov dword ptr [r10 + 24], 1\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_r_sample_spec)

        /* aw_r_channel_map(r, map, out_cap, max_ch) -> rax */
        AW_ASM_FUNC(aw_r_channel_map)
        AW_X4
        "test r10, r10\n   jz 9f\n   cmp dword ptr [r10 + 24], 0\n   jne 9f\n   mov r8, [r10]\n   test r8, r8\n   jz 8f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 8f\n   test rcx, rcx\n   js 8f\n   mov rax, [r10 + 16]\n   cmp rax, r9\n   jae 8f\n"
        "sub r9, rax\n   cmp r9, 2\n   jb 8f\n   add r8, rax\n   cmp byte ptr [r8], 109\n   jne 8f\n"
        "movzx eax, byte ptr [r8 + 1]\n   cmp rax, rcx\n   ja 8f\n   sub r9, 2\n   cmp r9, rax\n   jb 8f\n"
        "lea r9, [rax + 2]\n   add [r10 + 16], r9\n   test r11, r11\n   jz 3f\n   test rdx, rdx\n   jle 3f\n"
        "cmp rax, rdx\n   cmovb rdx, rax\n   add r8, 2\n   xor ecx, ecx\n"
        "2:  cmp rcx, rdx\n   jae 3f\n   movzx r9d, byte ptr [r8 + rcx]\n   mov r10d, 63\n   cmp r9d, 64\n   cmovae r9d, r10d\n"
        "mov [r11 + rcx], r9b\n   inc rcx\n   jmp 2b\n"
        "3:  " AW_ASM_RET
        "8:  mov dword ptr [r10 + 24], 1\n"
        "9:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_channel_map)

        /* aw_r_cvolume(r, volume, max_ch, vol_max) -> rax */
        AW_ASM_FUNC(aw_r_cvolume)
        AW_X4
        "test r10, r10\n   jz 9f\n   cmp dword ptr [r10 + 24], 0\n   jne 9f\n   mov r8, [r10]\n   test r8, r8\n   jz 8f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 8f\n   test rdx, rdx\n   js 8f\n   mov rax, [r10 + 16]\n   cmp rax, r9\n   jae 8f\n"
        "sub r9, rax\n   cmp r9, 2\n   jb 8f\n   add r8, rax\n   cmp byte ptr [r8], 118\n   jne 8f\n"
        "movzx eax, byte ptr [r8 + 1]\n   cmp rax, rdx\n   ja 8f\n   lea rdx, [rax * 4]\n   sub r9, 2\n   cmp r9, rdx\n   jb 8f\n"
        "add rdx, 2\n   add [r10 + 16], rdx\n   test r11, r11\n   jz 3f\n   add r8, 2\n   xor r9d, r9d\n"
        "2:  cmp r9, rax\n   jae 3f\n   mov edx, dword ptr [r8 + r9 * 4]\n   bswap edx\n   cmp rdx, rcx\n   cmova rdx, rcx\n"
        "mov [r11 + r9 * 8], rdx\n   inc r9\n   jmp 2b\n"
        "3:  " AW_ASM_RET
        "8:  mov dword ptr [r10 + 24], 1\n"
        "9:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_cvolume)

        /* aw_r_timeval(r, seconds, microseconds) */
        AW_ASM_FUNC(aw_r_timeval)
        AW_X3
        "test r10, r10\n   jz 5f\n   cmp dword ptr [r10 + 24], 0\n   jne 5f\n   mov r8, [r10]\n   test r8, r8\n   jz 4f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 4f\n   mov rax, [r10 + 16]\n   cmp rax, r9\n   jae 4f\n"
        "sub r9, rax\n   cmp r9, 9\n   jb 4f\n   add r8, rax\n   cmp byte ptr [r8], 84\n   jne 4f\n"
        "add rax, 9\n   mov [r10 + 16], rax\n   test r11, r11\n   jz 1f\n   mov eax, dword ptr [r8 + 1]\n   bswap eax\n   mov [r11], rax\n"
        "1:  test rdx, rdx\n   jz 5f\n   mov eax, dword ptr [r8 + 5]\n   bswap eax\n   mov [rdx], rax\n" AW_ASM_RET
        "4:  mov dword ptr [r10 + 24], 1\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_r_timeval)

        /* aw_r_arbitrary(r, n) -> rax */
        AW_ASM_FUNC(aw_r_arbitrary)
        AW_X2
        "test r10, r10\n   jz 9f\n   cmp dword ptr [r10 + 24], 0\n   jne 9f\n   mov r8, [r10]\n   test r8, r8\n   jz 8f\n"
        "mov r9, [r10 + 8]\n   test r9, r9\n   jle 8f\n   mov rcx, [r10 + 16]\n   cmp rcx, r9\n   jae 8f\n"
        "sub r9, rcx\n   cmp r9, 5\n   jb 8f\n   add r8, rcx\n   cmp byte ptr [r8], 120\n   jne 8f\n"
        "mov eax, dword ptr [r8 + 1]\n   bswap eax\n   cmp rax, r11\n   jne 8f\n   sub r9, 5\n   cmp r9, r11\n   jb 8f\n"
        "lea rcx, [rcx + r11 + 5]\n   mov [r10 + 16], rcx\n   lea rax, [r8 + 5]\n" AW_ASM_RET
        "8:  mov dword ptr [r10 + 24], 1\n"
        "9:  xor eax, eax\n" AW_ASM_RET
        AW_ASM_END(aw_r_arbitrary)

        /* aw_w_raw(w, data, n) */
        AW_ASM_FUNC(aw_w_raw)
        AW_X3
        "test r10, r10\n   jz 5f\n   cmp dword ptr [r10 + 24], 0\n   jne 5f\n   test rdx, rdx\n   js 4f\n"
        "mov r8, [r10]\n   mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 4f\n   cmp r9, rax\n   jl 4f\n"
        "test rdx, rdx\n   jz 5f\n   test r8, r8\n   jz 4f\n   test r11, r11\n   jz 4f\n"
        "sub r9, rax\n   cmp r9, rdx\n   jb 4f\n   add r8, rax\n   add rax, rdx\n   mov [r10 + 16], rax\n   xor ecx, ecx\n"
        "1:  cmp rdx, 8\n   jb 2f\n   mov rax, [r11 + rcx]\n   mov [r8 + rcx], rax\n   add rcx, 8\n   sub rdx, 8\n   jmp 1b\n"
        "2:  test rdx, rdx\n   jz 5f\n   mov al, byte ptr [r11 + rcx]\n   mov [r8 + rcx], al\n   inc rcx\n   dec rdx\n   jmp 2b\n"
        "4:  mov dword ptr [r10 + 24], 1\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_raw)

        /* aw_w_byte(w, c) */
        AW_ASM_FUNC(aw_w_byte)
        AW_X2
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jle 1f\n"
        "mov [r8 + rax], r11b\n   inc rax\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_byte)

        /* aw_w_be32(w, v) */
        AW_ASM_FUNC(aw_w_be32)
        AW_X2
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jl 1f\n"
        "sub r9, rax\n   cmp r9, 4\n   jb 1f\n   bswap r11d\n   mov dword ptr [r8 + rax], r11d\n"
        "add rax, 4\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_be32)

        /* aw_w_tagged32(w, tag, v) */
        AW_ASM_FUNC(aw_w_tagged32)
        AW_X3
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jl 1f\n"
        "sub r9, rax\n   cmp r9, 5\n   jb 1f\n   add r8, rax\n   bswap edx\n   mov [r8], r11b\n"
        "mov dword ptr [r8 + 1], edx\n   add rax, 5\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_tagged32)

        /* aw_w_bool(w, v) */
        AW_ASM_FUNC(aw_w_bool)
        AW_X2
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jle 1f\n"
        "xor ecx, ecx\n   test r11d, r11d\n   setne cl\n   add ecx, 48\n   mov [r8 + rax], cl\n"
        "inc rax\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_bool)

        /* aw_w_u8(w, v) */
        AW_ASM_FUNC(aw_w_u8)
        AW_X2
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jl 1f\n"
        "sub r9, rax\n   cmp r9, 2\n   jb 1f\n   add r8, rax\n   mov byte ptr [r8], 66\n   mov [r8 + 1], r11b\n"
        "add rax, 2\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_u8)

        /* aw_w_64(w, tag, v) */
        AW_ASM_FUNC(aw_w_64)
        AW_X3
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jl 1f\n"
        "sub r9, rax\n   cmp r9, 9\n   jb 1f\n   add r8, rax\n   bswap rdx\n   mov [r8], r11b\n"
        "mov [r8 + 1], rdx\n   add rax, 9\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_64)

        /* aw_w_sample_spec(w, format, channels, rate) */
        AW_ASM_FUNC(aw_w_sample_spec)
        AW_X4
        "test r10, r10\n   jz 2f\n   cmp dword ptr [r10 + 24], 0\n   jne 2f\n   mov r8, [r10]\n   test r8, r8\n   jz 1f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 1f\n   cmp r9, rax\n   jl 1f\n"
        "sub r9, rax\n   cmp r9, 7\n   jb 1f\n   add r8, rax\n   bswap ecx\n   mov byte ptr [r8], 97\n"
        "mov [r8 + 1], r11b\n   mov [r8 + 2], dl\n   mov dword ptr [r8 + 3], ecx\n   add rax, 7\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  mov dword ptr [r10 + 24], 1\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_sample_spec)

        /* aw_w_channel_map(w, map, channels, map_cap, aux0) */
        AW_ASM_FUNC(aw_w_channel_map)
        AW_X5
        "test r10, r10\n   jz 5f\n   cmp dword ptr [r10 + 24], 0\n   jne 5f\n   cmp rdx, 255\n   ja 4f\n"
        "test rdx, rdx\n   jz 1f\n   test rcx, rcx\n   jle 1f\n   test r11, r11\n   jz 4f\n"
        "1:  mov r9, [r10]\n   test r9, r9\n   jz 4f\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 4f\n"
        "mov r9, [r10 + 8]\n   cmp r9, rax\n   jl 4f\n   sub r9, rax\n   lea rax, [rdx + 2]\n   cmp r9, rax\n   jb 4f\n"
        "mov r9, [r10]\n   add r9, [r10 + 16]\n   add [r10 + 16], rax\n   mov byte ptr [r9], 109\n   mov [r9 + 1], dl\n"
        "add r9, 2\n   xor eax, eax\n"
        "2:  cmp rax, rdx\n   jae 5f\n   cmp rax, rcx\n   jge 3f\n   mov r10b, byte ptr [r11 + rax]\n   mov [r9 + rax], r10b\n   inc rax\n   jmp 2b\n"
        "3:  lea r10d, [r8 + rax]\n   mov [r9 + rax], r10b\n   inc rax\n   jmp 2b\n"
        "4:  mov dword ptr [r10 + 24], 1\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_channel_map)

        /* aw_w_string(w, s) */
        AW_ASM_FUNC(aw_w_string)
        AW_X2
        "test r10, r10\n   jz 5f\n   cmp dword ptr [r10 + 24], 0\n   jne 5f\n   mov r8, [r10]\n   test r8, r8\n   jz 4f\n"
        "mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n   test rax, rax\n   js 4f\n   cmp r9, rax\n   jl 4f\n"
        "sub r9, rax\n   test r11, r11\n   jnz 1f\n   test r9, r9\n   jz 4f\n   mov byte ptr [r8 + rax], 78\n"
        "inc rax\n   mov [r10 + 16], rax\n" AW_ASM_RET
        "1:  xor ecx, ecx\n"
        "2:  cmp byte ptr [r11 + rcx], 0\n   je 3f\n   inc rcx\n   jmp 2b\n"
        "3:  inc rcx\n   lea rdx, [rcx + 1]\n   cmp r9, rdx\n   jb 4f\n   add r8, rax\n   add rax, rdx\n   mov [r10 + 16], rax\n"
        "mov byte ptr [r8], 116\n   inc r8\n   xor edx, edx\n"
        "6:  cmp rcx, 8\n   jb 7f\n   mov rax, [r11 + rdx]\n   mov [r8 + rdx], rax\n   add rdx, 8\n   sub rcx, 8\n   jmp 6b\n"
        "7:  test rcx, rcx\n   jz 5f\n   mov al, byte ptr [r11 + rdx]\n   mov [r8 + rdx], al\n   inc rdx\n   dec rcx\n   jmp 7b\n"
        "4:  mov dword ptr [r10 + 24], 1\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_string)

        /* aw_w_cvolume(w, volume, channels) */
        AW_ASM_FUNC(aw_w_cvolume)
        AW_X3
        "test r10, r10\n   jz 5f\n   cmp dword ptr [r10 + 24], 0\n   jne 5f\n   cmp rdx, 255\n   ja 4f\n"
        "test rdx, rdx\n   jz 1f\n   test r11, r11\n   jz 4f\n"
        "1:  mov r8, [r10]\n   test r8, r8\n   jz 4f\n   mov r9, [r10 + 8]\n   mov rax, [r10 + 16]\n"
        "test rax, rax\n   js 4f\n   cmp r9, rax\n   jl 4f\n   sub r9, rax\n   lea rcx, [rdx * 4 + 2]\n   cmp r9, rcx\n   jb 4f\n"
        "add r8, rax\n   add rax, rcx\n   mov [r10 + 16], rax\n   mov byte ptr [r8], 118\n   mov [r8 + 1], dl\n"
        "add r8, 2\n   xor ecx, ecx\n"
        "2:  cmp rcx, rdx\n   jae 5f\n   mov eax, dword ptr [r11 + rcx * 8]\n   bswap eax\n   mov dword ptr [r8 + rcx * 4], eax\n"
        "inc rcx\n   jmp 2b\n"
        "4:  mov dword ptr [r10 + 24], 1\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_cvolume)

        ".att_syntax prefix\n"
);

#elif defined(AW_RISCV64)

__asm__(
        AW_ASM_SECTION

        /* aw_be32(a0=p) -> a0 */
        AW_ASM_FUNC(aw_be32)
        "beqz a0, 1f\n   lbu a1, 0(a0)\n   lbu a2, 1(a0)\n   lbu a3, 2(a0)\n   lbu a4, 3(a0)\n"
        "slli a1, a1, 24\n   slli a2, a2, 16\n   slli a3, a3, 8\n   or a1, a1, a2\n   or a3, a3, a4\n   or a0, a1, a3\n   sext.w a0, a0\n" AW_ASM_RET
        "1:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_be32)

        /* aw_put_be32(a0=to, a1=v) */
        AW_ASM_FUNC(aw_put_be32)
        "beqz a0, 1f\n   srli a2, a1, 24\n   srli a3, a1, 16\n   srli a4, a1, 8\n"
        "sb a2, 0(a0)\n   sb a3, 1(a0)\n   sb a4, 2(a0)\n   sb a1, 3(a0)\n"
        "1:  " AW_ASM_RET
        AW_ASM_END(aw_put_be32)

        /* aw_strlen(a0=s) -> a0 */
        AW_ASM_FUNC(aw_strlen)
        "beqz a0, 2f\n   mv a1, a0\n"
        "1:  lbu a2, 0(a1)\n   beqz a2, 3f\n   addi a1, a1, 1\n   j 1b\n"
        "3:  sub a0, a1, a0\n" AW_ASM_RET
        "2:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_strlen)

        /* aw_r_tag(a0=r, a1=tag) -> a0 */
        AW_ASM_FUNC(aw_r_tag)
        "beqz a0, 2f\n   lw a2, 24(a0)\n   bnez a2, 2f\n   ld a2, 0(a0)\n   beqz a2, 1f\n"
        "ld a3, 8(a0)\n   blez a3, 1f\n   ld a4, 16(a0)\n   bgeu a4, a3, 1f\n"
        "add a2, a2, a4\n   lbu a5, 0(a2)\n   andi a1, a1, 0xff\n   bne a5, a1, 1f\n"
        "addi a4, a4, 1\n   sd a4, 16(a0)\n   li a0, 1\n" AW_ASM_RET
        "1:  li a2, 1\n   sw a2, 24(a0)\n"
        "2:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_tag)

        /* aw_r_u8(a0=r) -> a0 */
        AW_ASM_FUNC(aw_r_u8)
        "beqz a0, 2f\n   lw a1, 24(a0)\n   bnez a1, 2f\n   ld a1, 0(a0)\n   beqz a1, 1f\n"
        "ld a2, 8(a0)\n   blez a2, 1f\n   ld a3, 16(a0)\n   bgeu a3, a2, 1f\n"
        "sub a4, a2, a3\n   li a5, 2\n   bltu a4, a5, 1f\n   add a4, a1, a3\n"
        "lbu a5, 0(a4)\n   li t0, 66\n   bne a5, t0, 1f\n   lbu a5, 1(a4)\n"
        "addi a3, a3, 2\n   sd a3, 16(a0)\n   mv a0, a5\n" AW_ASM_RET
        "1:  li a1, 1\n   sw a1, 24(a0)\n"
        "2:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_u8)

        /* aw_r_u32(a0=r) -> a0 */
        AW_ASM_FUNC(aw_r_u32)
        "beqz a0, 2f\n   lw a1, 24(a0)\n   bnez a1, 2f\n   ld a1, 0(a0)\n   beqz a1, 1f\n"
        "ld a2, 8(a0)\n   blez a2, 1f\n   ld a3, 16(a0)\n   bgeu a3, a2, 1f\n"
        "sub a4, a2, a3\n   li a5, 5\n   bltu a4, a5, 1f\n   add a4, a1, a3\n"
        "lbu a5, 0(a4)\n   li t0, 76\n   bne a5, t0, 1f\n"
        "lbu a1, 1(a4)\n   lbu a2, 2(a4)\n   lbu a5, 3(a4)\n   lbu t0, 4(a4)\n"
        "slli a1, a1, 24\n   slli a2, a2, 16\n   slli a5, a5, 8\n   or a1, a1, a2\n   or a5, a5, t0\n   or a1, a1, a5\n"
        "addi a3, a3, 5\n   sd a3, 16(a0)\n   sext.w a0, a1\n" AW_ASM_RET
        "1:  li a1, 1\n   sw a1, 24(a0)\n"
        "2:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_u32)

        /* aw_r_bool(a0=r) -> a0 */
        AW_ASM_FUNC(aw_r_bool)
        "beqz a0, 2f\n   lw a1, 24(a0)\n   bnez a1, 2f\n   ld a1, 0(a0)\n   beqz a1, 1f\n"
        "ld a2, 8(a0)\n   blez a2, 1f\n   ld a3, 16(a0)\n   bgeu a3, a2, 1f\n"
        "add a4, a1, a3\n   lbu a5, 0(a4)\n   addi a5, a5, -48\n   li a2, 1\n   bgtu a5, a2, 1f\n"
        "addi a3, a3, 1\n   sd a3, 16(a0)\n   mv a0, a5\n" AW_ASM_RET
        "1:  li a1, 1\n   sw a1, 24(a0)\n"
        "2:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_bool)

        /* aw_r_string(a0=r, a1=limit, a2=isnull) -> a0 */
        AW_ASM_FUNC(aw_r_string)
        "beqz a2, 1f\n   sw zero, 0(a2)\n"
        "1:  beqz a0, 9f\n   lw a3, 24(a0)\n   bnez a3, 9f\n   ld a3, 0(a0)\n   beqz a3, 8f\n"
        "ld a4, 8(a0)\n   blez a4, 8f\n   ld a5, 16(a0)\n   bgeu a5, a4, 8f\n"
        "add t0, a3, a5\n   lbu t1, 0(t0)\n   li t2, 78\n   bne t1, t2, 2f\n"
        "addi a5, a5, 1\n   sd a5, 16(a0)\n   beqz a2, 9f\n   li a4, 1\n   sw a4, 0(a2)\n   j 9f\n"
        "2:  li t2, 116\n   bne t1, t2, 8f\n   addi a5, a5, 1\n   sub a4, a4, a5\n   beqz a4, 8f\n"
        "blez a1, 3f\n   addi t0, a4, -1\n   ble t0, a1, 3f\n   addi a4, a1, 1\n"
        "3:  add a3, a3, a5\n   li a2, 0\n"
        "4:  bgeu a2, a4, 8f\n   add t0, a3, a2\n   lbu t1, 0(t0)\n   beqz t1, 5f\n   addi a2, a2, 1\n   j 4b\n"
        "5:  add a5, a5, a2\n   addi a5, a5, 1\n   sd a5, 16(a0)\n   mv a0, a3\n" AW_ASM_RET
        "8:  li a3, 1\n   sw a3, 24(a0)\n"
        "9:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_string)

        /* aw_r_sample_spec(a0=r, a1=format, a2=channels, a3=rate) */
        AW_ASM_FUNC(aw_r_sample_spec)
        "beqz a0, 5f\n   lw a4, 24(a0)\n   bnez a4, 5f\n   ld a4, 0(a0)\n   beqz a4, 4f\n"
        "ld a5, 8(a0)\n   blez a5, 4f\n   ld t0, 16(a0)\n   bgeu t0, a5, 4f\n"
        "sub t1, a5, t0\n   li t2, 7\n   bltu t1, t2, 4f\n   add a4, a4, t0\n"
        "lbu a5, 0(a4)\n   li t2, 97\n   bne a5, t2, 4f\n   addi t0, t0, 7\n   sd t0, 16(a0)\n"
        "beqz a1, 1f\n   lbu a5, 1(a4)\n   sd a5, 0(a1)\n"
        "1:  beqz a2, 2f\n   lbu a5, 2(a4)\n   sd a5, 0(a2)\n"
        "2:  beqz a3, 5f\n   lbu a1, 3(a4)\n   lbu a2, 4(a4)\n   lbu a5, 5(a4)\n   lbu t0, 6(a4)\n"
        "slli a1, a1, 24\n   slli a2, a2, 16\n   slli a5, a5, 8\n   or a1, a1, a2\n   or a5, a5, t0\n   or a1, a1, a5\n"
        "sd a1, 0(a3)\n" AW_ASM_RET
        "4:  li a4, 1\n   sw a4, 24(a0)\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_r_sample_spec)

        /* aw_r_channel_map(a0=r, a1=map, a2=out_cap, a3=max_ch) -> a0 */
        AW_ASM_FUNC(aw_r_channel_map)
        "beqz a0, 9f\n   lw a4, 24(a0)\n   bnez a4, 9f\n   ld a4, 0(a0)\n   beqz a4, 8f\n"
        "ld a5, 8(a0)\n   blez a5, 8f\n   bltz a3, 8f\n   ld t0, 16(a0)\n   bgeu t0, a5, 8f\n"
        "sub a5, a5, t0\n   li t1, 2\n   bltu a5, t1, 8f\n   add a4, a4, t0\n"
        "lbu t1, 0(a4)\n   li t2, 109\n   bne t1, t2, 8f\n   lbu t1, 1(a4)\n"
        "bgtu t1, a3, 8f\n   addi a5, a5, -2\n   bltu a5, t1, 8f\n"
        "addi t0, t0, 2\n   add t0, t0, t1\n   sd t0, 16(a0)\n   beqz a1, 3f\n   blez a2, 3f\n"
        "mv a5, t1\n   bltu a5, a2, 1f\n   mv a5, a2\n"
        "1:  addi a4, a4, 2\n   li a2, 0\n   li a3, 63\n"
        "2:  bgeu a2, a5, 3f\n   add t0, a4, a2\n   lbu t2, 0(t0)\n   bleu t2, a3, 4f\n   mv t2, a3\n"
        "4:  add t0, a1, a2\n   sb t2, 0(t0)\n   addi a2, a2, 1\n   j 2b\n"
        "3:  mv a0, t1\n" AW_ASM_RET
        "8:  li a4, 1\n   sw a4, 24(a0)\n"
        "9:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_channel_map)

        /* aw_r_cvolume(a0=r, a1=volume, a2=max_ch, a3=vol_max) -> a0 */
        AW_ASM_FUNC(aw_r_cvolume)
        "beqz a0, 9f\n   lw a4, 24(a0)\n   bnez a4, 9f\n   ld a4, 0(a0)\n   beqz a4, 8f\n"
        "ld a5, 8(a0)\n   blez a5, 8f\n   bltz a2, 8f\n   ld t0, 16(a0)\n   bgeu t0, a5, 8f\n"
        "sub a5, a5, t0\n   li t1, 2\n   bltu a5, t1, 8f\n   add a4, a4, t0\n"
        "lbu t1, 0(a4)\n   li t2, 118\n   bne t1, t2, 8f\n   lbu t1, 1(a4)\n"
        "bgtu t1, a2, 8f\n   slli t2, t1, 2\n   addi a5, a5, -2\n   bltu a5, t2, 8f\n"
        "addi t0, t0, 2\n   add t0, t0, t2\n   sd t0, 16(a0)\n   beqz a1, 3f\n   addi a4, a4, 2\n   li a5, 0\n"
        "2:  bgeu a5, t1, 3f\n   lbu a2, 0(a4)\n   lbu t0, 1(a4)\n   lbu t2, 2(a4)\n   lbu t3, 3(a4)\n"
        "slli a2, a2, 24\n   slli t0, t0, 16\n   slli t2, t2, 8\n   or a2, a2, t0\n   or t2, t2, t3\n   or a2, a2, t2\n"
        "bleu a2, a3, 4f\n   mv a2, a3\n"
        "4:  sd a2, 0(a1)\n   addi a4, a4, 4\n   addi a1, a1, 8\n   addi a5, a5, 1\n   j 2b\n"
        "3:  mv a0, t1\n" AW_ASM_RET
        "8:  li a4, 1\n   sw a4, 24(a0)\n"
        "9:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_cvolume)

        /* aw_r_timeval(a0=r, a1=seconds, a2=microseconds) */
        AW_ASM_FUNC(aw_r_timeval)
        "beqz a0, 5f\n   lw a3, 24(a0)\n   bnez a3, 5f\n   ld a3, 0(a0)\n   beqz a3, 4f\n"
        "ld a4, 8(a0)\n   blez a4, 4f\n   ld a5, 16(a0)\n   bgeu a5, a4, 4f\n"
        "sub a4, a4, a5\n   li t0, 9\n   bltu a4, t0, 4f\n   add a3, a3, a5\n"
        "lbu a4, 0(a3)\n   li t0, 84\n   bne a4, t0, 4f\n   addi a5, a5, 9\n   sd a5, 16(a0)\n"
        "beqz a1, 1f\n   lbu a4, 1(a3)\n   lbu a5, 2(a3)\n   lbu t0, 3(a3)\n   lbu t1, 4(a3)\n"
        "slli a4, a4, 24\n   slli a5, a5, 16\n   slli t0, t0, 8\n   or a4, a4, a5\n   or t0, t0, t1\n   or a4, a4, t0\n   sd a4, 0(a1)\n"
        "1:  beqz a2, 5f\n   lbu a4, 5(a3)\n   lbu a5, 6(a3)\n   lbu t0, 7(a3)\n   lbu t1, 8(a3)\n"
        "slli a4, a4, 24\n   slli a5, a5, 16\n   slli t0, t0, 8\n   or a4, a4, a5\n   or t0, t0, t1\n   or a4, a4, t0\n   sd a4, 0(a2)\n" AW_ASM_RET
        "4:  li a3, 1\n   sw a3, 24(a0)\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_r_timeval)

        /* aw_r_arbitrary(a0=r, a1=n) -> a0 */
        AW_ASM_FUNC(aw_r_arbitrary)
        "beqz a0, 9f\n   lw a2, 24(a0)\n   bnez a2, 9f\n   ld a2, 0(a0)\n   beqz a2, 8f\n"
        "ld a3, 8(a0)\n   blez a3, 8f\n   ld a4, 16(a0)\n   bgeu a4, a3, 8f\n"
        "sub a3, a3, a4\n   li a5, 5\n   bltu a3, a5, 8f\n   add a2, a2, a4\n"
        "lbu a5, 0(a2)\n   li t0, 120\n   bne a5, t0, 8f\n"
        "lbu a5, 1(a2)\n   lbu t0, 2(a2)\n   lbu t1, 3(a2)\n   lbu t2, 4(a2)\n"
        "slli a5, a5, 24\n   slli t0, t0, 16\n   slli t1, t1, 8\n   or a5, a5, t0\n   or t1, t1, t2\n   or a5, a5, t1\n"
        "bne a5, a1, 8f\n   addi a3, a3, -5\n   bltu a3, a1, 8f\n"
        "addi a4, a4, 5\n   add a4, a4, a1\n   sd a4, 16(a0)\n   addi a0, a2, 5\n" AW_ASM_RET
        "8:  li a2, 1\n   sw a2, 24(a0)\n"
        "9:  li a0, 0\n" AW_ASM_RET
        AW_ASM_END(aw_r_arbitrary)

        /* aw_w_raw(a0=w, a1=data, a2=n) */
        AW_ASM_FUNC(aw_w_raw)
        "beqz a0, 5f\n   lw a3, 24(a0)\n   bnez a3, 5f\n   bltz a2, 4f\n   ld a3, 0(a0)\n   ld a4, 8(a0)\n"
        "ld a5, 16(a0)\n   bltz a5, 4f\n   blt a4, a5, 4f\n   beqz a2, 5f\n   beqz a3, 4f\n   beqz a1, 4f\n"
        "sub a4, a4, a5\n   bltu a4, a2, 4f\n   add a3, a3, a5\n   add a5, a5, a2\n   sd a5, 16(a0)\n"
        "1:  beqz a2, 5f\n   lbu a4, 0(a1)\n   sb a4, 0(a3)\n   addi a1, a1, 1\n   addi a3, a3, 1\n   addi a2, a2, -1\n   j 1b\n"
        "4:  li a3, 1\n   sw a3, 24(a0)\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_raw)

        /* aw_w_byte(a0=w, a1=c) */
        AW_ASM_FUNC(aw_w_byte)
        "beqz a0, 2f\n   lw a2, 24(a0)\n   bnez a2, 2f\n   ld a2, 0(a0)\n   beqz a2, 1f\n"
        "ld a3, 8(a0)\n   ld a4, 16(a0)\n   bltz a4, 1f\n   ble a3, a4, 1f\n"
        "add a2, a2, a4\n   sb a1, 0(a2)\n   addi a4, a4, 1\n   sd a4, 16(a0)\n" AW_ASM_RET
        "1:  li a2, 1\n   sw a2, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_byte)

        /* aw_w_be32(a0=w, a1=v) */
        AW_ASM_FUNC(aw_w_be32)
        "beqz a0, 2f\n   lw a2, 24(a0)\n   bnez a2, 2f\n   ld a2, 0(a0)\n   beqz a2, 1f\n"
        "ld a3, 8(a0)\n   ld a4, 16(a0)\n   bltz a4, 1f\n   blt a3, a4, 1f\n"
        "sub a3, a3, a4\n   li a5, 4\n   bltu a3, a5, 1f\n   add a2, a2, a4\n"
        "srli a3, a1, 24\n   srli a5, a1, 16\n   srli t0, a1, 8\n"
        "sb a3, 0(a2)\n   sb a5, 1(a2)\n   sb t0, 2(a2)\n   sb a1, 3(a2)\n"
        "addi a4, a4, 4\n   sd a4, 16(a0)\n" AW_ASM_RET
        "1:  li a2, 1\n   sw a2, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_be32)

        /* aw_w_tagged32(a0=w, a1=tag, a2=v) */
        AW_ASM_FUNC(aw_w_tagged32)
        "beqz a0, 2f\n   lw a3, 24(a0)\n   bnez a3, 2f\n   ld a3, 0(a0)\n   beqz a3, 1f\n"
        "ld a4, 8(a0)\n   ld a5, 16(a0)\n   bltz a5, 1f\n   blt a4, a5, 1f\n"
        "sub a4, a4, a5\n   li t0, 5\n   bltu a4, t0, 1f\n   add a3, a3, a5\n"
        "sb a1, 0(a3)\n   srli a4, a2, 24\n   srli t0, a2, 16\n   srli t1, a2, 8\n"
        "sb a4, 1(a3)\n   sb t0, 2(a3)\n   sb t1, 3(a3)\n   sb a2, 4(a3)\n"
        "addi a5, a5, 5\n   sd a5, 16(a0)\n" AW_ASM_RET
        "1:  li a3, 1\n   sw a3, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_tagged32)

        /* aw_w_bool(a0=w, a1=v) */
        AW_ASM_FUNC(aw_w_bool)
        "beqz a0, 2f\n   lw a2, 24(a0)\n   bnez a2, 2f\n   ld a2, 0(a0)\n   beqz a2, 1f\n"
        "ld a3, 8(a0)\n   ld a4, 16(a0)\n   bltz a4, 1f\n   ble a3, a4, 1f\n"
        "snez a5, a1\n   addi a5, a5, 48\n   add a2, a2, a4\n   sb a5, 0(a2)\n   addi a4, a4, 1\n   sd a4, 16(a0)\n" AW_ASM_RET
        "1:  li a2, 1\n   sw a2, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_bool)

        /* aw_w_u8(a0=w, a1=v) */
        AW_ASM_FUNC(aw_w_u8)
        "beqz a0, 2f\n   lw a2, 24(a0)\n   bnez a2, 2f\n   ld a2, 0(a0)\n   beqz a2, 1f\n"
        "ld a3, 8(a0)\n   ld a4, 16(a0)\n   bltz a4, 1f\n   blt a3, a4, 1f\n"
        "sub a3, a3, a4\n   li a5, 2\n   bltu a3, a5, 1f\n   add a2, a2, a4\n"
        "li a5, 66\n   sb a5, 0(a2)\n   sb a1, 1(a2)\n   addi a4, a4, 2\n   sd a4, 16(a0)\n" AW_ASM_RET
        "1:  li a2, 1\n   sw a2, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_u8)

        /* aw_w_64(a0=w, a1=tag, a2=v) */
        AW_ASM_FUNC(aw_w_64)
        "beqz a0, 2f\n   lw a3, 24(a0)\n   bnez a3, 2f\n   ld a3, 0(a0)\n   beqz a3, 1f\n"
        "ld a4, 8(a0)\n   ld a5, 16(a0)\n   bltz a5, 1f\n   blt a4, a5, 1f\n"
        "sub a4, a4, a5\n   li t0, 9\n   bltu a4, t0, 1f\n   add a3, a3, a5\n   addi a5, a5, 9\n   sd a5, 16(a0)\n"
        "sb a1, 0(a3)\n   srli a4, a2, 56\n   srli a5, a2, 48\n   srli t0, a2, 40\n   srli t1, a2, 32\n"
        "sb a4, 1(a3)\n   sb a5, 2(a3)\n   sb t0, 3(a3)\n   sb t1, 4(a3)\n"
        "srli a4, a2, 24\n   srli a5, a2, 16\n   srli t0, a2, 8\n"
        "sb a4, 5(a3)\n   sb a5, 6(a3)\n   sb t0, 7(a3)\n   sb a2, 8(a3)\n" AW_ASM_RET
        "1:  li a3, 1\n   sw a3, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_64)

        /* aw_w_sample_spec(a0=w, a1=format, a2=channels, a3=rate) */
        AW_ASM_FUNC(aw_w_sample_spec)
        "beqz a0, 2f\n   lw a4, 24(a0)\n   bnez a4, 2f\n   ld a4, 0(a0)\n   beqz a4, 1f\n"
        "ld a5, 8(a0)\n   ld t0, 16(a0)\n   bltz t0, 1f\n   blt a5, t0, 1f\n"
        "sub a5, a5, t0\n   li t1, 7\n   bltu a5, t1, 1f\n   add a4, a4, t0\n"
        "li a5, 97\n   sb a5, 0(a4)\n   sb a1, 1(a4)\n   sb a2, 2(a4)\n"
        "srli a1, a3, 24\n   srli a2, a3, 16\n   srli a5, a3, 8\n"
        "sb a1, 3(a4)\n   sb a2, 4(a4)\n   sb a5, 5(a4)\n   sb a3, 6(a4)\n"
        "addi t0, t0, 7\n   sd t0, 16(a0)\n" AW_ASM_RET
        "1:  li a4, 1\n   sw a4, 24(a0)\n"
        "2:  " AW_ASM_RET
        AW_ASM_END(aw_w_sample_spec)

        /* aw_w_channel_map(a0=w, a1=map, a2=channels, a3=map_cap, a4=aux0) */
        AW_ASM_FUNC(aw_w_channel_map)
        "beqz a0, 5f\n   lw a5, 24(a0)\n   bnez a5, 5f\n   li t0, 255\n   bgtu a2, t0, 4f\n"
        "beqz a2, 1f\n   blez a3, 1f\n   beqz a1, 4f\n"
        "1:  ld a5, 0(a0)\n   beqz a5, 4f\n   ld t0, 8(a0)\n   ld t1, 16(a0)\n   bltz t1, 4f\n   blt t0, t1, 4f\n"
        "sub t0, t0, t1\n   addi t2, a2, 2\n   bltu t0, t2, 4f\n   add a5, a5, t1\n   add t1, t1, t2\n   sd t1, 16(a0)\n"
        "li t2, 109\n   sb t2, 0(a5)\n   sb a2, 1(a5)\n   addi a5, a5, 2\n   li t0, 0\n"
        "2:  bgeu t0, a2, 5f\n   bge t0, a3, 3f\n   add t1, a1, t0\n   lbu t2, 0(t1)\n   sb t2, 0(a5)\n"
        "addi a5, a5, 1\n   addi t0, t0, 1\n   j 2b\n"
        "3:  add t2, a4, t0\n   sb t2, 0(a5)\n   addi a5, a5, 1\n   addi t0, t0, 1\n   j 2b\n"
        "4:  li a5, 1\n   sw a5, 24(a0)\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_channel_map)

        /* aw_w_string(a0=w, a1=s) */
        AW_ASM_FUNC(aw_w_string)
        "beqz a0, 5f\n   lw a2, 24(a0)\n   bnez a2, 5f\n   ld a2, 0(a0)\n   beqz a2, 4f\n"
        "ld a3, 8(a0)\n   ld a4, 16(a0)\n   bltz a4, 4f\n   blt a3, a4, 4f\n   sub a3, a3, a4\n   bnez a1, 1f\n"
        "beqz a3, 4f\n   add a2, a2, a4\n   li a5, 78\n   sb a5, 0(a2)\n   addi a4, a4, 1\n   sd a4, 16(a0)\n" AW_ASM_RET
        "1:  mv a5, a1\n"
        "2:  lbu t0, 0(a5)\n   addi a5, a5, 1\n   bnez t0, 2b\n   sub a5, a5, a1\n   addi t0, a5, 1\n   bltu a3, t0, 4f\n"
        "add a2, a2, a4\n   add a4, a4, t0\n   sd a4, 16(a0)\n   li t0, 116\n   sb t0, 0(a2)\n   addi a2, a2, 1\n"
        "3:  beqz a5, 5f\n   lbu t0, 0(a1)\n   sb t0, 0(a2)\n   addi a1, a1, 1\n   addi a2, a2, 1\n   addi a5, a5, -1\n   j 3b\n"
        "4:  li a2, 1\n   sw a2, 24(a0)\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_string)

        /* aw_w_cvolume(a0=w, a1=volume, a2=channels) */
        AW_ASM_FUNC(aw_w_cvolume)
        "beqz a0, 5f\n   lw a3, 24(a0)\n   bnez a3, 5f\n   li a4, 255\n   bgtu a2, a4, 4f\n   beqz a2, 1f\n   beqz a1, 4f\n"
        "1:  ld a3, 0(a0)\n   beqz a3, 4f\n   ld a4, 8(a0)\n   ld a5, 16(a0)\n   bltz a5, 4f\n   blt a4, a5, 4f\n"
        "sub a4, a4, a5\n   slli t0, a2, 2\n   addi t0, t0, 2\n   bltu a4, t0, 4f\n"
        "add a3, a3, a5\n   add a5, a5, t0\n   sd a5, 16(a0)\n   li t0, 118\n   sb t0, 0(a3)\n   sb a2, 1(a3)\n"
        "addi a3, a3, 2\n   li a4, 0\n"
        "2:  bgeu a4, a2, 5f\n   lw a5, 0(a1)\n   srli t0, a5, 24\n   srli t1, a5, 16\n   srli t2, a5, 8\n"
        "sb t0, 0(a3)\n   sb t1, 1(a3)\n   sb t2, 2(a3)\n   sb a5, 3(a3)\n"
        "addi a1, a1, 8\n   addi a3, a3, 4\n   addi a4, a4, 1\n   j 2b\n"
        "4:  li a3, 1\n   sw a3, 24(a0)\n"
        "5:  " AW_ASM_RET
        AW_ASM_END(aw_w_cvolume)
);

#endif
#endif /* AUDIO_WIRE_ASM_IMPLEMENTATION */
