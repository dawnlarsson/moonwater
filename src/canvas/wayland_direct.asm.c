/*
        Moonwater Canvas Wayland direct input & scale core primitives in pure assembly:
          - wl_direct_div120       : exact signed bias-rounded division by 120 (magic umulh, zero sdiv)
          - wl_direct_pixels       : u * scale120 rounded to nearest pixel (half away from 0)
          - wl_direct_pixels_floor : floor(u * scale120 / 120)
          - wl_direct_pixels_ceil  : ceil(u * scale120 / 120)
          - wl_direct_units        : pixel-to-surface-unit conversion (p * 120 / scale120)
          - wl_direct_units_fixed  : pixel-to-wl_fixed (24.8) conversion (p * 256 * 120 / scale120)
          - wl_direct_cover        : scale a 4-field wayland_rect via floor/ceil (in==out safe)
          - wl_direct_text         : bounded UTF-8 & control-char sanitizer (ASCII fast path)
          - wl_direct_pack_key     : wl_keyboard.key wire packet builder (1-8 kHz reader path)
          - wl_direct_pack_packet  : locked-pointer relative/button/wheel/frame wire packet builder

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define CANVAS_WAYLAND_DIRECT_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CANVAS_WAYLAND_DIRECT_ASM_H
#define MOONWATER_CANVAS_WAYLAND_DIRECT_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  wl_u8;
typedef p16 wl_u16;
typedef p32 wl_u32;
typedef b32 wl_i32;
typedef p64 wl_u64;
typedef b64 wl_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  wl_u8;
typedef __UINT16_TYPE__ wl_u16;
typedef __UINT32_TYPE__ wl_u32;
typedef __INT32_TYPE__  wl_i32;
typedef __UINT64_TYPE__ wl_u64;
typedef __INT64_TYPE__  wl_i64;
#else
#include <stdint.h>
typedef uint8_t  wl_u8;
typedef uint16_t wl_u16;
typedef uint32_t wl_u32;
typedef int32_t  wl_i32;
typedef uint64_t wl_u64;
typedef int64_t  wl_i64;
#endif

/* 32 bytes -- byte-for-byte identical to struct wayland_rect */
struct wl_direct_rect {
        wl_i64 x;       /* +0  */
        wl_i64 y;       /* +8  */
        wl_i64 width;   /* +16 */
        wl_i64 height;  /* +24 */
};

/* 40 bytes -- input descriptor for wl_direct_pack_key */
struct wl_direct_key {
        const wl_u32 *ids;     /* +0  */
        wl_i64        count;   /* +8  */
        wl_u64        micro;   /* +16 */
        wl_u32        serial;  /* +24 */
        wl_u32        code;    /* +28 */
        wl_i32        pressed; /* +32 */
        wl_u32        _pad;    /* +36 */
};

/* 112 bytes -- input descriptor for wl_direct_pack_packet */
struct wl_direct_packet {
        wl_i64        dx;          /* +0   */
        wl_i64        dy;          /* +8   */
        wl_u64        micro;       /* +16  */
        wl_i64        scale120;    /* +24  */
        const wl_u32 *rel_ids;     /* +32  */
        wl_i64        rel_count;   /* +40  */
        const wl_u32 *ptr_ids;     /* +48  */
        const wl_u8  *ptr_ver;     /* +56  */
        wl_i64        ptr_count;   /* +64  */
        const wl_u32 *frame_ids;   /* +72  */
        wl_i64        frame_count; /* +80  */
        wl_u32        btn;         /* +88  */
        wl_i32        btn_val;     /* +92  */
        wl_u32        btn_serial;  /* +96  */
        wl_i32        wheel_v120;  /* +100 */
        wl_i32        wheel_horiz; /* +104 */
        wl_u32        _pad;        /* +108 */
};

#ifdef offset_of
#define WL_DIR_OFFSET(T, M) offset_of(T, M)
#else
#define WL_DIR_OFFSET(T, M) ((wl_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct wl_direct_rect) == 32, "wl_direct_rect must be 32 bytes");
_Static_assert(WL_DIR_OFFSET(struct wl_direct_rect, x) == 0 && WL_DIR_OFFSET(struct wl_direct_rect, y) == 8 &&
               WL_DIR_OFFSET(struct wl_direct_rect, width) == 16 && WL_DIR_OFFSET(struct wl_direct_rect, height) == 24,
               "wl_direct_rect field offsets");

_Static_assert(sizeof(struct wl_direct_key) == 40, "wl_direct_key must be 40 bytes");
_Static_assert(WL_DIR_OFFSET(struct wl_direct_key, ids) == 0 && WL_DIR_OFFSET(struct wl_direct_key, count) == 8 &&
               WL_DIR_OFFSET(struct wl_direct_key, micro) == 16 && WL_DIR_OFFSET(struct wl_direct_key, serial) == 24 &&
               WL_DIR_OFFSET(struct wl_direct_key, code) == 28 && WL_DIR_OFFSET(struct wl_direct_key, pressed) == 32,
               "wl_direct_key field offsets");

_Static_assert(sizeof(struct wl_direct_packet) == 112, "wl_direct_packet must be 112 bytes");
_Static_assert(WL_DIR_OFFSET(struct wl_direct_packet, dx) == 0 && WL_DIR_OFFSET(struct wl_direct_packet, dy) == 8 &&
               WL_DIR_OFFSET(struct wl_direct_packet, micro) == 16 && WL_DIR_OFFSET(struct wl_direct_packet, scale120) == 24 &&
               WL_DIR_OFFSET(struct wl_direct_packet, rel_ids) == 32 && WL_DIR_OFFSET(struct wl_direct_packet, rel_count) == 40 &&
               WL_DIR_OFFSET(struct wl_direct_packet, ptr_ids) == 48 && WL_DIR_OFFSET(struct wl_direct_packet, ptr_ver) == 56 &&
               WL_DIR_OFFSET(struct wl_direct_packet, ptr_count) == 64 && WL_DIR_OFFSET(struct wl_direct_packet, frame_ids) == 72 &&
               WL_DIR_OFFSET(struct wl_direct_packet, frame_count) == 80 && WL_DIR_OFFSET(struct wl_direct_packet, btn) == 88 &&
               WL_DIR_OFFSET(struct wl_direct_packet, btn_val) == 92 && WL_DIR_OFFSET(struct wl_direct_packet, btn_serial) == 96 &&
               WL_DIR_OFFSET(struct wl_direct_packet, wheel_v120) == 100 && WL_DIR_OFFSET(struct wl_direct_packet, wheel_horiz) == 104,
               "wl_direct_packet field offsets");

wl_i64 wl_direct_div120(wl_i64 v, wl_i64 pb, wl_i64 nb);
wl_i64 wl_direct_pixels(wl_i64 u, wl_i64 s);
wl_i64 wl_direct_pixels_floor(wl_i64 u, wl_i64 s);
wl_i64 wl_direct_pixels_ceil(wl_i64 u, wl_i64 s);
wl_i64 wl_direct_units(wl_i64 p, wl_i64 s);
wl_u64 wl_direct_units_fixed(wl_i64 p, wl_i64 s);
void   wl_direct_cover(const struct wl_direct_rect *in, wl_i64 s, struct wl_direct_rect *out);
wl_i64 wl_direct_text(char *into, wl_u64 cap, const wl_u8 *text, wl_i64 length);
wl_u32 wl_direct_pack_key(wl_u8 *out, wl_u64 cap, const struct wl_direct_key *req);
wl_u32 wl_direct_pack_packet(wl_u8 *out, wl_u64 cap, const struct wl_direct_packet *pkt);

#endif /* MOONWATER_CANVAS_WAYLAND_DIRECT_ASM_H */

#ifdef CANVAS_WAYLAND_DIRECT_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define WLD_ASM_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define WLD_ASM_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define WLD_ASM_RISCV64 1
# else
#  error "src/canvas/wayland_direct.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define WLD_ASM_ARM64 1
# elif defined(X64) && X64
#  define WLD_ASM_X64 1
# elif defined(RISCV64) && RISCV64
#  define WLD_ASM_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define WLD_ASM_SECTION  ASM_SECTION
# define WLD_ASM_FUNC(n)  ASM_FUNC(n)
# define WLD_ASM_END(n)   ASM_END(n)
# define WLD_ASM_RET      ASM_RET
#else
# define WLD_ASM_SECTION  ".text\n"
# define WLD_ASM_RET      "ret\n"
# if defined(__APPLE__)
#  define WLD_ASM_FUNC(n) ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define WLD_ASM_END(n)  ""
# elif defined(_WIN32)
#  define WLD_ASM_FUNC(n) ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define WLD_ASM_END(n)  ""
# else
#  if defined(WLD_ASM_ARM64)
#   define WLD_ASM_FTYPE  "%function"
#  else
#   define WLD_ASM_FTYPE  "@function"
#  endif
#  define WLD_ASM_FUNC(n) ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " WLD_ASM_FTYPE "\n" #n ":\n"
#  define WLD_ASM_END(n)  ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(WLD_ASM_ARM64)

__asm__(
        WLD_ASM_SECTION

        /* wl_direct_div120(x0=v, x1=pb, x2=nb) -> i64 */
        WLD_ASM_FUNC(wl_direct_div120)
        "cmp x0, #0\n   cneg x3, x0, lt\n   csel x4, x1, x2, ge\n   cset w5, lt\n   adds x3, x3, x4\n"
        "b.pl 1f\n   neg x3, x3\n   eor w5, w5, #1\n"
        "1:  lsr x3, x3, #3\n   orr x4, xzr, #0x1111111111111111\n   add x4, x4, #1\n   umulh x0, x3, x4\n"
        "cbz w5, 2f\n   neg x0, x0\n"
        "2:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_div120)

        /* wl_direct_pixels(x0=u, x1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels)
        "mul x0, x0, x1\n   cmp x0, #0\n   cneg x1, x0, lt\n   add x1, x1, #60\n   lsr x1, x1, #3\n"
        "orr x2, xzr, #0x1111111111111111\n   add x2, x2, #1\n   umulh x1, x1, x2\n   cneg x0, x1, lt\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels)

        /* wl_direct_pixels_floor(x0=u, x1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels_floor)
        "mul x0, x0, x1\n   cmp x0, #0\n   cneg x1, x0, lt\n   mov x2, #119\n   csel x2, xzr, x2, ge\n"
        "add x1, x1, x2\n   lsr x1, x1, #3\n   orr x2, xzr, #0x1111111111111111\n   add x2, x2, #1\n"
        "umulh x1, x1, x2\n   cneg x0, x1, lt\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels_floor)

        /* wl_direct_pixels_ceil(x0=u, x1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels_ceil)
        "mul x0, x0, x1\n   cmp x0, #0\n   cneg x1, x0, lt\n   mov x2, #119\n   csel x2, x2, xzr, ge\n"
        "add x1, x1, x2\n   lsr x1, x1, #3\n   orr x2, xzr, #0x1111111111111111\n   add x2, x2, #1\n"
        "umulh x1, x1, x2\n   cneg x0, x1, lt\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels_ceil)

        /* wl_direct_units(x0=p, x1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_units)
        "cmp x1, #0\n   b.le 1f\n   cmp x1, #120\n   b.eq 1f\n   mov x2, #120\n   mul x0, x0, x2\n   sdiv x0, x0, x1\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_units)

        /* wl_direct_units_fixed(x0=p, x1=s) -> u64 */
        WLD_ASM_FUNC(wl_direct_units_fixed)
        "lsl x0, x0, #8\n   cmp x1, #0\n   b.le 1f\n   cmp x1, #120\n   b.eq 1f\n   mov x2, #120\n   mul x0, x0, x2\n   sdiv x0, x0, x1\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_units_fixed)

        /* wl_direct_cover(x0=in, x1=s, x2=out) */
        WLD_ASM_FUNC(wl_direct_cover)
        "cbz x0, 1f\n   cbz x2, 1f\n   ldp x3, x4, [x0]\n   ldp x5, x6, [x0, #16]\n"
        "add x5, x3, x5\n   add x6, x4, x6\n   orr x7, xzr, #0x1111111111111111\n   add x7, x7, #1\n   mov x8, #119\n"
        "mul x9, x3, x1\n   cmp x9, #0\n   cneg x10, x9, lt\n   csel x11, xzr, x8, ge\n   add x10, x10, x11\n"
        "lsr x10, x10, #3\n   umulh x10, x10, x7\n   cneg x3, x10, lt\n"
        "mul x9, x4, x1\n   cmp x9, #0\n   cneg x10, x9, lt\n   csel x11, xzr, x8, ge\n   add x10, x10, x11\n"
        "lsr x10, x10, #3\n   umulh x10, x10, x7\n   cneg x4, x10, lt\n"
        "mul x9, x5, x1\n   cmp x9, #0\n   cneg x10, x9, lt\n   csel x11, x8, xzr, ge\n   add x10, x10, x11\n"
        "lsr x10, x10, #3\n   umulh x10, x10, x7\n   cneg x5, x10, lt\n"
        "mul x9, x6, x1\n   cmp x9, #0\n   cneg x10, x9, lt\n   csel x11, x8, xzr, ge\n   add x10, x10, x11\n"
        "lsr x10, x10, #3\n   umulh x10, x10, x7\n   cneg x6, x10, lt\n"
        "sub x5, x5, x3\n   sub x6, x6, x4\n   stp x3, x4, [x2]\n   stp x5, x6, [x2, #16]\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_cover)

        /* wl_direct_text(x0=into, x1=cap, x2=text, x3=length) -> i64 */
        WLD_ASM_FUNC(wl_direct_text)
        "cbz x0, 9f\n   cbz x1, 9f\n   sub x1, x1, #1\n   mov x4, #0\n   cbz x2, 8f\n   cmp x3, #0\n   b.le 8f\n   mov x5, #0\n"
        "1:  cmp x5, x3\n   b.ge 8f\n   cmp x4, x1\n   b.hs 8f\n   ldrb w6, [x2, x5]\n"
        "sub w7, w6, #0x20\n   cmp w7, #0x5e\n   b.hi 2f\n   strb w6, [x0, x4]\n   add x4, x4, #1\n   add x5, x5, #1\n   b 1b\n"
        "2:  cmp w6, #0xc2\n   b.lo 7f\n   sub x8, x3, x5\n   sub x9, x1, x4\n   cmp w6, #0xe0\n   b.hs 3f\n"
        "cmp x8, #2\n   b.lo 7f\n   cmp x9, #2\n   b.lo 7f\n   add x10, x5, #1\n   ldrb w10, [x2, x10]\n"
        "and w11, w10, #0xc0\n   cmp w11, #0x80\n   b.ne 7f\n"
        "strb w6, [x0, x4]\n   add x11, x4, #1\n   strb w10, [x0, x11]\n   add x4, x4, #2\n   add x5, x5, #2\n   b 1b\n"
        "3:  cmp w6, #0xf0\n   b.hs 5f\n   cmp x8, #3\n   b.lo 7f\n   cmp x9, #3\n   b.lo 7f\n"
        "add x10, x2, x5\n   ldrb w11, [x10, #1]\n   ldrb w12, [x10, #2]\n"
        "and w13, w11, #0xc0\n   cmp w13, #0x80\n   b.ne 7f\n   and w13, w12, #0xc0\n   cmp w13, #0x80\n   b.ne 7f\n"
        "cmp w6, #0xe0\n   b.ne 4f\n   cmp w11, #0xa0\n   b.lo 7f\n   b 10f\n"
        "4:  cmp w6, #0xed\n   b.ne 10f\n   cmp w11, #0xa0\n   b.hs 7f\n"
        "10: add x10, x0, x4\n   strb w6, [x10]\n   strb w11, [x10, #1]\n   strb w12, [x10, #2]\n   add x4, x4, #3\n   add x5, x5, #3\n   b 1b\n"
        "5:  cmp w6, #0xf4\n   b.hi 7f\n   cmp x8, #4\n   b.lo 7f\n   cmp x9, #4\n   b.lo 7f\n"
        "add x10, x2, x5\n   ldrb w11, [x10, #1]\n   ldrb w12, [x10, #2]\n   ldrb w13, [x10, #3]\n"
        "and w14, w11, #0xc0\n   cmp w14, #0x80\n   b.ne 7f\n   and w14, w12, #0xc0\n   cmp w14, #0x80\n   b.ne 7f\n"
        "and w14, w13, #0xc0\n   cmp w14, #0x80\n   b.ne 7f\n"
        "cmp w6, #0xf0\n   b.ne 6f\n   cmp w11, #0x90\n   b.lo 7f\n   b 11f\n"
        "6:  cmp w6, #0xf4\n   b.ne 11f\n   cmp w11, #0x90\n   b.hs 7f\n"
        "11: add x10, x0, x4\n   strb w6, [x10]\n   strb w11, [x10, #1]\n   strb w12, [x10, #2]\n   strb w13, [x10, #3]\n"
        "add x4, x4, #4\n   add x5, x5, #4\n   b 1b\n"
        "7:  mov w7, #63\n   strb w7, [x0, x4]\n   add x4, x4, #1\n   add x5, x5, #1\n   b 1b\n"
        "8:  strb wzr, [x0, x4]\n   mov x0, x4\n" WLD_ASM_RET
        "9:  mov x0, #0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_text)

        /* wl_direct_pack_key(x0=out, x1=cap, x2=req) -> u32 */
        WLD_ASM_FUNC(wl_direct_pack_key)
        "cbz x0, 3f\n   cbz x2, 3f\n   ldp x3, x4, [x2]\n   cbz x3, 3f\n   cmp x4, #0\n   b.le 3f\n"
        "ldr x5, [x2, #16]\n   lsr x5, x5, #3\n   movz x6, #0xf7cf\n   movk x6, #0xe353, lsl #16\n"
        "movk x6, #0x9ba5, lsl #32\n   movk x6, #0x20c4, lsl #48\n   umulh x7, x5, x6\n   lsr x7, x7, #4\n"
        "ldp w8, w9, [x2, #24]\n   ldr w10, [x2, #32]\n   cmp w10, #0\n   cset w10, ne\n"
        "movz w11, #3\n   movk w11, #24, lsl #16\n   mov x12, #0\n   mov x13, #0\n"
        "1:  cmp x13, x4\n   b.ge 2f\n   sub x14, x1, x12\n   cmp x14, #24\n   b.lo 2f\n"
        "ldr w15, [x3, x13, lsl #2]\n   add x14, x0, x12\n   stp w15, w11, [x14]\n   stp w8, w7, [x14, #8]\n"
        "stp w9, w10, [x14, #16]\n   add x12, x12, #24\n   add x13, x13, #1\n   b 1b\n"
        "2:  mov w0, w12\n" WLD_ASM_RET
        "3:  mov w0, #0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pack_key)

        /* wl_direct_pack_packet(x0=out, x1=cap, x2=pkt) -> u32 */
        WLD_ASM_FUNC(wl_direct_pack_packet)
        "cbz x0, 15f\n   cbz x2, 15f\n   mov x3, #0\n   ldp x4, x5, [x2]\n   orr x6, x4, x5\n   cbz x6, 4f\n"
        "ldp x6, x7, [x2, #32]\n   cbz x6, 4f\n   cmp x7, #0\n   b.le 4f\n"
        "lsl x8, x4, #8\n   lsl x9, x5, #8\n   ldr x10, [x2, #24]\n   mov x11, x8\n   mov x12, x9\n"
        "cmp x10, #0\n   b.le 1f\n   cmp x10, #120\n   b.eq 1f\n   mov x13, #120\n"
        "mul x11, x8, x13\n   sdiv x11, x11, x10\n   mul x12, x9, x13\n   sdiv x12, x12, x10\n"
        "1:  ldr x13, [x2, #16]\n   lsr x14, x13, #32\n   movz w15, #0\n   movk w15, #32, lsl #16\n   mov x10, #0\n"
        "2:  cmp x10, x7\n   b.ge 4f\n   sub x16, x1, x3\n   cmp x16, #32\n   b.lo 14f\n"
        "ldr w17, [x6, x10, lsl #2]\n   add x16, x0, x3\n   stp w17, w15, [x16]\n   stp w14, w13, [x16, #8]\n"
        "stp w11, w12, [x16, #16]\n   stp w8, w9, [x16, #24]\n   add x3, x3, #32\n   add x10, x10, #1\n   b 2b\n"
        "4:  ldr w4, [x2, #88]\n   ldr w5, [x2, #100]\n   orr w6, w4, w5\n   cbz w6, 12f\n"
        "ldr x6, [x2, #16]\n   lsr x6, x6, #3\n   movz x7, #0xf7cf\n   movk x7, #0xe353, lsl #16\n"
        "movk x7, #0x9ba5, lsl #32\n   movk x7, #0x20c4, lsl #48\n   umulh x6, x6, x7\n   lsr x6, x6, #4\n"
        "cbz w4, 7f\n   ldr x7, [x2, #48]\n   ldr x8, [x2, #64]\n   cbz x7, 7f\n   cmp x8, #0\n   b.le 7f\n"
        "ldr w9, [x2, #96]\n   ldr w10, [x2, #92]\n   cmp w10, #0\n   cset w10, ne\n"
        "movz w11, #3\n   movk w11, #24, lsl #16\n   mov x12, #0\n"
        "6:  cmp x12, x8\n   b.ge 7f\n   sub x13, x1, x3\n   cmp x13, #24\n   b.lo 14f\n"
        "ldr w14, [x7, x12, lsl #2]\n   add x13, x0, x3\n   stp w14, w11, [x13]\n   stp w9, w6, [x13, #8]\n"
        "stp w4, w10, [x13, #16]\n   add x3, x3, #24\n   add x12, x12, #1\n   b 6b\n"
        "7:  cbz w5, 12f\n   ldp x7, x8, [x2, #48]\n   ldr x9, [x2, #64]\n   cbz x7, 12f\n   cbz x8, 12f\n   cmp x9, #0\n   b.le 12f\n"
        "ldr w10, [x2, #104]\n   sxtw x11, w5\n   cmp w10, #0\n   cset w10, ne\n   cneg x11, x11, eq\n"
        "lsl w12, w11, #5\n   cmp x11, #0\n   cneg x13, x11, lt\n   lsr x13, x13, #3\n"
        "orr x14, xzr, #0x1111111111111111\n   add x14, x14, #1\n   umulh x13, x13, x14\n   cneg x13, x13, lt\n   mov x14, #0\n"
        "8:  cmp x14, x9\n   b.ge 12f\n   ldr w15, [x7, x14, lsl #2]\n   ldrb w16, [x8, x14]\n"
        "cmp w16, #5\n   b.lo 11f\n   sub x17, x1, x3\n   cmp x17, #12\n   b.lo 14f\n"
        "movz w4, #6\n   movk w4, #12, lsl #16\n   add x17, x0, x3\n   stp w15, w4, [x17]\n   str wzr, [x17, #8]\n   add x3, x3, #12\n"
        "cmp w16, #8\n   b.lo 9f\n   sub x17, x1, x3\n   cmp x17, #16\n   b.lo 14f\n"
        "movz w4, #9\n   movk w4, #16, lsl #16\n   add x17, x0, x3\n   stp w15, w4, [x17]\n   stp w10, w11, [x17, #8]\n   add x3, x3, #16\n   b 11f\n"
        "9:  cbz w13, 11f\n   sub x17, x1, x3\n   cmp x17, #16\n   b.lo 14f\n"
        "movz w4, #8\n   movk w4, #16, lsl #16\n   add x17, x0, x3\n   stp w15, w4, [x17]\n   stp w10, w13, [x17, #8]\n   add x3, x3, #16\n"
        "11: sub x17, x1, x3\n   cmp x17, #20\n   b.lo 14f\n   movz w4, #4\n   movk w4, #20, lsl #16\n"
        "add x17, x0, x3\n   stp w15, w4, [x17]\n   stp w6, w10, [x17, #8]\n   str w12, [x17, #16]\n   add x3, x3, #20\n   add x14, x14, #1\n   b 8b\n"
        "12: ldp x4, x5, [x2, #72]\n   cbz x4, 14f\n   cmp x5, #0\n   b.le 14f\n"
        "movz w6, #5\n   movk w6, #8, lsl #16\n   mov x7, #0\n"
        "13: cmp x7, x5\n   b.ge 14f\n   sub x8, x1, x3\n   cmp x8, #8\n   b.lo 14f\n"
        "ldr w9, [x4, x7, lsl #2]\n   add x8, x0, x3\n   stp w9, w6, [x8]\n   add x3, x3, #8\n   add x7, x7, #1\n   b 13b\n"
        "14: mov w0, w3\n" WLD_ASM_RET
        "15: mov w0, #0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pack_packet)
);

#elif defined(WLD_ASM_X64)

#if defined(_WIN32)
# define WLD_L2 "mov r8, rcx\n   mov r9, rdx\n"
# define WLD_L3 "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define WLD_L4 "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
#else
# define WLD_L2 "mov r8, rdi\n   mov r9, rsi\n"
# define WLD_L3 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define WLD_L4 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
#endif

__asm__(
        WLD_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* wl_direct_div120(r8=v, r9=pb, r10=nb) -> i64 */
        WLD_ASM_FUNC(wl_direct_div120)
        WLD_L3
        "mov rax, r8\n   xor ecx, ecx\n   test r8, r8\n   jns 1f\n   neg rax\n   mov r9, r10\n   mov ecx, 1\n"
        "1:  add rax, r9\n   jns 2f\n   neg rax\n   xor ecx, 1\n"
        "2:  shr rax, 3\n   mov rdx, 0x1111111111111112\n   mul rdx\n   mov rax, rdx\n   test ecx, ecx\n   jz 3f\n   neg rax\n"
        "3:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_div120)

        /* wl_direct_pixels(r8=u, r9=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels)
        WLD_L2
        "imul r8, r9\n   mov rax, r8\n   neg rax\n   cmovs rax, r8\n   add rax, 60\n   shr rax, 3\n"
        "mov rdx, 0x1111111111111112\n   mul rdx\n   mov rax, rdx\n   neg rdx\n   test r8, r8\n   cmovs rax, rdx\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels)

        /* wl_direct_pixels_floor(r8=u, r9=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels_floor)
        WLD_L2
        "imul r8, r9\n   mov rax, r8\n   neg rax\n   cmovs rax, r8\n   mov ecx, 119\n   xor r9d, r9d\n"
        "test r8, r8\n   cmovns rcx, r9\n   add rax, rcx\n   shr rax, 3\n"
        "mov rdx, 0x1111111111111112\n   mul rdx\n   mov rax, rdx\n   neg rdx\n   test r8, r8\n   cmovs rax, rdx\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels_floor)

        /* wl_direct_pixels_ceil(r8=u, r9=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels_ceil)
        WLD_L2
        "imul r8, r9\n   mov rax, r8\n   neg rax\n   cmovs rax, r8\n   mov ecx, 119\n   xor r9d, r9d\n"
        "test r8, r8\n   cmovs rcx, r9\n   add rax, rcx\n   shr rax, 3\n"
        "mov rdx, 0x1111111111111112\n   mul rdx\n   mov rax, rdx\n   neg rdx\n   test r8, r8\n   cmovs rax, rdx\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels_ceil)

        /* wl_direct_units(r8=p, r9=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_units)
        WLD_L2
        "mov rax, r8\n   test r9, r9\n   jle 1f\n   cmp r9, 120\n   je 1f\n   imul rax, r8, 120\n   cqo\n   idiv r9\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_units)

        /* wl_direct_units_fixed(r8=p, r9=s) -> u64 */
        WLD_ASM_FUNC(wl_direct_units_fixed)
        WLD_L2
        "mov rax, r8\n   shl rax, 8\n   test r9, r9\n   jle 1f\n   cmp r9, 120\n   je 1f\n   imul rax, rax, 120\n   cqo\n   idiv r9\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_units_fixed)

        /* wl_direct_cover(r8=in, r9=s, r10=out) */
        WLD_ASM_FUNC(wl_direct_cover)
        WLD_L3
        "test r8, r8\n   jz 1f\n   test r10, r10\n   jz 1f\n   push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, [r8]\n   mov r13, [r8 + 8]\n   mov r14, [r8 + 16]\n   mov r15, [r8 + 24]\n"
        "add r14, r12\n   add r15, r13\n   mov rbx, 0x1111111111111112\n   mov r11d, 119\n"
        "imul r12, r9\n   mov rax, r12\n   neg rax\n   cmovs rax, r12\n   xor ecx, ecx\n   test r12, r12\n   cmovs rcx, r11\n"
        "add rax, rcx\n   shr rax, 3\n   mul rbx\n   mov rcx, rdx\n   neg rcx\n   test r12, r12\n   cmovs rdx, rcx\n   mov r12, rdx\n"
        "imul r13, r9\n   mov rax, r13\n   neg rax\n   cmovs rax, r13\n   xor ecx, ecx\n   test r13, r13\n   cmovs rcx, r11\n"
        "add rax, rcx\n   shr rax, 3\n   mul rbx\n   mov rcx, rdx\n   neg rcx\n   test r13, r13\n   cmovs rdx, rcx\n   mov r13, rdx\n"
        "imul r14, r9\n   mov rax, r14\n   neg rax\n   cmovs rax, r14\n   xor ecx, ecx\n   test r14, r14\n   cmovns rcx, r11\n"
        "add rax, rcx\n   shr rax, 3\n   mul rbx\n   mov rcx, rdx\n   neg rcx\n   test r14, r14\n   cmovs rdx, rcx\n   sub rdx, r12\n   mov r14, rdx\n"
        "imul r15, r9\n   mov rax, r15\n   neg rax\n   cmovs rax, r15\n   xor ecx, ecx\n   test r15, r15\n   cmovns rcx, r11\n"
        "add rax, rcx\n   shr rax, 3\n   mul rbx\n   mov rcx, rdx\n   neg rcx\n   test r15, r15\n   cmovs rdx, rcx\n   sub rdx, r13\n"
        "mov [r10], r12\n   mov [r10 + 8], r13\n   mov [r10 + 16], r14\n   mov [r10 + 24], rdx\n"
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_cover)

        /* wl_direct_text(r8=into, r9=cap, r10=text, r11=length) -> i64 */
        WLD_ASM_FUNC(wl_direct_text)
        WLD_L4
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   dec r9\n   xor eax, eax\n"
        "test r10, r10\n   jz 8f\n   test r11, r11\n   jle 8f\n   push rbx\n   push r12\n   push r13\n   xor ecx, ecx\n"
        "1:  cmp rcx, r11\n   jge 12f\n   cmp rax, r9\n   jae 12f\n   movzx edx, byte ptr [r10 + rcx]\n"
        "lea ebx, [rdx - 0x20]\n   cmp ebx, 0x5e\n   ja 2f\n   mov byte ptr [r8 + rax], dl\n   inc rax\n   inc rcx\n   jmp 1b\n"
        "2:  cmp edx, 0xc2\n   jb 7f\n   mov r12, r11\n   sub r12, rcx\n   mov r13, r9\n   sub r13, rax\n   cmp edx, 0xe0\n   jae 3f\n"
        "cmp r12, 2\n   jb 7f\n   cmp r13, 2\n   jb 7f\n   movzx ebx, byte ptr [r10 + rcx + 1]\n"
        "mov r12d, ebx\n   and r12d, 0xc0\n   cmp r12d, 0x80\n   jne 7f\n"
        "mov byte ptr [r8 + rax], dl\n   mov byte ptr [r8 + rax + 1], bl\n   add rax, 2\n   add rcx, 2\n   jmp 1b\n"
        "3:  cmp edx, 0xf0\n   jae 5f\n   cmp r12, 3\n   jb 7f\n   cmp r13, 3\n   jb 7f\n"
        "movzx ebx, byte ptr [r10 + rcx + 1]\n   movzx r12d, byte ptr [r10 + rcx + 2]\n"
        "mov r13d, ebx\n   and r13d, 0xc0\n   cmp r13d, 0x80\n   jne 7f\n"
        "mov r13d, r12d\n   and r13d, 0xc0\n   cmp r13d, 0x80\n   jne 7f\n"
        "cmp edx, 0xe0\n   jne 4f\n   cmp ebx, 0xa0\n   jb 7f\n   jmp 10f\n"
        "4:  cmp edx, 0xed\n   jne 10f\n   cmp ebx, 0xa0\n   jae 7f\n"
        "10: mov byte ptr [r8 + rax], dl\n   mov byte ptr [r8 + rax + 1], bl\n   mov byte ptr [r8 + rax + 2], r12b\n"
        "add rax, 3\n   add rcx, 3\n   jmp 1b\n"
        "5:  cmp edx, 0xf4\n   ja 7f\n   cmp r12, 4\n   jb 7f\n   cmp r13, 4\n   jb 7f\n"
        "movzx ebx, byte ptr [r10 + rcx + 1]\n   movzx r12d, byte ptr [r10 + rcx + 2]\n   movzx r13d, byte ptr [r10 + rcx + 3]\n"
        "push rax\n   mov eax, ebx\n   and eax, 0xc0\n   cmp eax, 0x80\n   jne 13f\n"
        "mov eax, r12d\n   and eax, 0xc0\n   cmp eax, 0x80\n   jne 13f\n"
        "mov eax, r13d\n   and eax, 0xc0\n   cmp eax, 0x80\n   jne 13f\n"
        "cmp edx, 0xf0\n   jne 6f\n   cmp ebx, 0x90\n   jb 13f\n   jmp 11f\n"
        "6:  cmp edx, 0xf4\n   jne 11f\n   cmp ebx, 0x90\n   jae 13f\n"
        "11: pop rax\n   mov byte ptr [r8 + rax], dl\n   mov byte ptr [r8 + rax + 1], bl\n"
        "mov byte ptr [r8 + rax + 2], r12b\n   mov byte ptr [r8 + rax + 3], r13b\n   add rax, 4\n   add rcx, 4\n   jmp 1b\n"
        "13: pop rax\n"
        "7:  mov byte ptr [r8 + rax], 63\n   inc rax\n   inc rcx\n   jmp 1b\n"
        "12: pop r13\n   pop r12\n   pop rbx\n"
        "8:  mov byte ptr [r8 + rax], 0\n" WLD_ASM_RET
        "9:  xor eax, eax\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_text)

        /* wl_direct_pack_key(r8=out, r9=cap, r10=req) -> u32 */
        WLD_ASM_FUNC(wl_direct_pack_key)
        WLD_L3
        "test r8, r8\n   jz 3f\n   test r10, r10\n   jz 3f\n   mov r11, [r10]\n   mov rcx, [r10 + 8]\n"
        "test r11, r11\n   jz 3f\n   test rcx, rcx\n   jle 3f\n   push rbx\n   push r12\n   push r13\n   push r14\n"
        "mov rax, [r10 + 16]\n   shr rax, 3\n   mov rdx, 0x20c49ba5e353f7cf\n   mul rdx\n   shr rdx, 4\n   mov r12d, edx\n"
        "mov ebx, [r10 + 24]\n   mov r13d, [r10 + 28]\n   xor r14d, r14d\n   cmp dword ptr [r10 + 32], 0\n   setne r14b\n"
        "xor eax, eax\n   xor r10d, r10d\n"
        "1:  cmp r10, rcx\n   jge 2f\n   mov rdx, r9\n   sub rdx, rax\n   cmp rdx, 24\n   jb 2f\n"
        "mov edx, [r11 + r10 * 4]\n   mov [r8 + rax], edx\n   mov dword ptr [r8 + rax + 4], 0x00180003\n"
        "mov [r8 + rax + 8], ebx\n   mov [r8 + rax + 12], r12d\n   mov [r8 + rax + 16], r13d\n"
        "mov [r8 + rax + 20], r14d\n   add rax, 24\n   inc r10\n   jmp 1b\n"
        "2:  pop r14\n   pop r13\n   pop r12\n   pop rbx\n" WLD_ASM_RET
        "3:  xor eax, eax\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pack_key)

        /* wl_direct_pack_packet(r8=out, r9=cap, r10=pkt) -> u32 */
        WLD_ASM_FUNC(wl_direct_pack_packet)
        WLD_L3
        "test r8, r8\n   jz 15f\n   test r10, r10\n   jz 15f\n"
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n   push rbp\n   sub rsp, 24\n"
        "xor r11d, r11d\n   mov r12, [r10]\n   mov r13, [r10 + 8]\n   mov rax, r12\n   or rax, r13\n   jz 4f\n"
        "mov r14, [r10 + 32]\n   mov r15, [r10 + 40]\n   test r14, r14\n   jz 4f\n   test r15, r15\n   jle 4f\n"
        "shl r12, 8\n   shl r13, 8\n   mov rbx, r12\n   mov rbp, r13\n   mov rcx, [r10 + 24]\n"
        "test rcx, rcx\n   jle 1f\n   cmp rcx, 120\n   je 1f\n"
        "imul rax, r12, 120\n   cqo\n   idiv rcx\n   mov rbx, rax\n   imul rax, r13, 120\n   cqo\n   idiv rcx\n   mov rbp, rax\n"
        "1:  mov rax, [r10 + 16]\n   mov rdx, rax\n   shr rdx, 32\n   xor ecx, ecx\n"
        "2:  cmp rcx, r15\n   jge 4f\n   mov [rsp], rax\n   mov rax, r9\n   sub rax, r11\n   cmp rax, 32\n   jb 14f\n   mov rax, [rsp]\n"
        "mov [rsp + 8], rdx\n   mov edx, [r14 + rcx * 4]\n   mov [r8 + r11], edx\n   mov dword ptr [r8 + r11 + 4], 0x00200000\n"
        "mov rdx, [rsp + 8]\n   mov [r8 + r11 + 8], edx\n   mov [r8 + r11 + 12], eax\n"
        "mov [r8 + r11 + 16], ebx\n   mov [r8 + r11 + 20], ebp\n   mov [r8 + r11 + 24], r12d\n   mov [r8 + r11 + 28], r13d\n"
        "add r11, 32\n   inc rcx\n   jmp 2b\n"
        "4:  mov r12d, [r10 + 88]\n   mov r13d, [r10 + 100]\n   mov eax, r12d\n   or eax, r13d\n   jz 12f\n"
        "mov rax, [r10 + 16]\n   shr rax, 3\n   mov rdx, 0x20c49ba5e353f7cf\n   mul rdx\n   shr rdx, 4\n   mov [rsp], rdx\n"
        "test r12d, r12d\n   jz 7f\n   mov r14, [r10 + 48]\n   mov r15, [r10 + 64]\n   test r14, r14\n   jz 7f\n   test r15, r15\n   jle 7f\n"
        "mov ebx, [r10 + 96]\n   xor ebp, ebp\n   cmp dword ptr [r10 + 92], 0\n   setne bpl\n   mov eax, [rsp]\n   xor ecx, ecx\n"
        "6:  cmp rcx, r15\n   jge 7f\n   mov rdx, r9\n   sub rdx, r11\n   cmp rdx, 24\n   jb 14f\n"
        "mov edx, [r14 + rcx * 4]\n   mov [r8 + r11], edx\n   mov dword ptr [r8 + r11 + 4], 0x00180003\n"
        "mov [r8 + r11 + 8], ebx\n   mov [r8 + r11 + 12], eax\n   mov [r8 + r11 + 16], r12d\n   mov [r8 + r11 + 20], ebp\n"
        "add r11, 24\n   inc rcx\n   jmp 6b\n"
        "7:  test r13d, r13d\n   jz 12f\n   mov r14, [r10 + 48]\n   mov rbp, [r10 + 56]\n   mov r15, [r10 + 64]\n"
        "test r14, r14\n   jz 12f\n   test rbp, rbp\n   jz 12f\n   test r15, r15\n   jle 12f\n"
        "movsxd r12, r13d\n   xor ebx, ebx\n   cmp dword ptr [r10 + 104], 0\n   setne bl\n   jne 16f\n   neg r12\n"
        "16: mov eax, r12d\n   shl eax, 5\n   mov [rsp + 8], eax\n"
        "mov rax, r12\n   neg rax\n   cmovs rax, r12\n   shr rax, 3\n   mov rdx, 0x1111111111111112\n   mul rdx\n"
        "mov r13, rdx\n   neg rdx\n   test r12, r12\n   cmovs r13, rdx\n   xor ecx, ecx\n"
        "8:  cmp rcx, r15\n   jge 12f\n   mov eax, [r14 + rcx * 4]\n   movzx edx, byte ptr [rbp + rcx]\n"
        "cmp edx, 5\n   jb 11f\n   mov [rsp + 16], rdx\n   mov rdx, r9\n   sub rdx, r11\n   cmp rdx, 12\n   jb 14f\n"
        "mov [r8 + r11], eax\n   mov dword ptr [r8 + r11 + 4], 0x000c0006\n   mov dword ptr [r8 + r11 + 8], 0\n   add r11, 12\n"
        "mov rdx, [rsp + 16]\n   cmp edx, 8\n   jb 9f\n   mov rdx, r9\n   sub rdx, r11\n   cmp rdx, 16\n   jb 14f\n"
        "mov [r8 + r11], eax\n   mov dword ptr [r8 + r11 + 4], 0x00100009\n   mov [r8 + r11 + 8], ebx\n   mov [r8 + r11 + 12], r12d\n"
        "add r11, 16\n   jmp 11f\n"
        "9:  test r13d, r13d\n   jz 11f\n   mov rdx, r9\n   sub rdx, r11\n   cmp rdx, 16\n   jb 14f\n"
        "mov [r8 + r11], eax\n   mov dword ptr [r8 + r11 + 4], 0x00100008\n   mov [r8 + r11 + 8], ebx\n   mov [r8 + r11 + 12], r13d\n   add r11, 16\n"
        "11: mov rdx, r9\n   sub rdx, r11\n   cmp rdx, 20\n   jb 14f\n   mov [r8 + r11], eax\n   mov dword ptr [r8 + r11 + 4], 0x00140004\n"
        "mov edx, [rsp]\n   mov [r8 + r11 + 8], edx\n   mov [r8 + r11 + 12], ebx\n   mov edx, [rsp + 8]\n   mov [r8 + r11 + 16], edx\n"
        "add r11, 20\n   inc rcx\n   jmp 8b\n"
        "12: mov r14, [r10 + 72]\n   mov r15, [r10 + 80]\n   test r14, r14\n   jz 14f\n   test r15, r15\n   jle 14f\n   xor ecx, ecx\n"
        "13: cmp rcx, r15\n   jge 14f\n   mov rdx, r9\n   sub rdx, r11\n   cmp rdx, 8\n   jb 14f\n"
        "mov eax, [r14 + rcx * 4]\n   mov [r8 + r11], eax\n   mov dword ptr [r8 + r11 + 4], 0x00080005\n   add r11, 8\n   inc rcx\n   jmp 13b\n"
        "14: mov eax, r11d\n   add rsp, 24\n   pop rbp\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" WLD_ASM_RET
        "15: xor eax, eax\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pack_packet)

        ".att_syntax prefix\n"
);

#elif defined(WLD_ASM_RISCV64)

__asm__(
        WLD_ASM_SECTION

        /* wl_direct_div120(a0=v, a1=pb, a2=nb) -> i64 */
        WLD_ASM_FUNC(wl_direct_div120)
        "li t0, 0\n   bge a0, zero, 1f\n   neg a0, a0\n   mv a1, a2\n   li t0, 1\n"
        "1:  add a0, a0, a1\n   bge a0, zero, 2f\n   neg a0, a0\n   xori t0, t0, 1\n"
        "2:  srli a0, a0, 3\n   li t1, 0x1111111111111112\n   mulhu a0, a0, t1\n   beqz t0, 3f\n   neg a0, a0\n"
        "3:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_div120)

        /* wl_direct_pixels(a0=u, a1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels)
        "mul a0, a0, a1\n   srai t0, a0, 63\n   xor a1, a0, t0\n   sub a1, a1, t0\n   addi a1, a1, 60\n"
        "srli a1, a1, 3\n   li t1, 0x1111111111111112\n   mulhu a1, a1, t1\n   xor a0, a1, t0\n   sub a0, a0, t0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels)

        /* wl_direct_pixels_floor(a0=u, a1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels_floor)
        "mul a0, a0, a1\n   srai t0, a0, 63\n   xor a1, a0, t0\n   sub a1, a1, t0\n   andi t2, t0, 119\n"
        "add a1, a1, t2\n   srli a1, a1, 3\n   li t1, 0x1111111111111112\n   mulhu a1, a1, t1\n   xor a0, a1, t0\n   sub a0, a0, t0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels_floor)

        /* wl_direct_pixels_ceil(a0=u, a1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_pixels_ceil)
        "mul a0, a0, a1\n   srai t0, a0, 63\n   xor a1, a0, t0\n   sub a1, a1, t0\n   not t2, t0\n   andi t2, t2, 119\n"
        "add a1, a1, t2\n   srli a1, a1, 3\n   li t1, 0x1111111111111112\n   mulhu a1, a1, t1\n   xor a0, a1, t0\n   sub a0, a0, t0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pixels_ceil)

        /* wl_direct_units(a0=p, a1=s) -> i64 */
        WLD_ASM_FUNC(wl_direct_units)
        "blez a1, 1f\n   li t0, 120\n   beq a1, t0, 1f\n   mul a0, a0, t0\n   div a0, a0, a1\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_units)

        /* wl_direct_units_fixed(a0=p, a1=s) -> u64 */
        WLD_ASM_FUNC(wl_direct_units_fixed)
        "slli a0, a0, 8\n   blez a1, 1f\n   li t0, 120\n   beq a1, t0, 1f\n   mul a0, a0, t0\n   div a0, a0, a1\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_units_fixed)

        /* wl_direct_cover(a0=in, a1=s, a2=out) */
        WLD_ASM_FUNC(wl_direct_cover)
        "beqz a0, 1f\n   beqz a2, 1f\n   ld a3, 0(a0)\n   ld a4, 8(a0)\n   ld a5, 16(a0)\n   ld a6, 24(a0)\n"
        "add a5, a3, a5\n   add a6, a4, a6\n   li t1, 0x1111111111111112\n"
        "mul t2, a3, a1\n   srai t0, t2, 63\n   xor t2, t2, t0\n   sub t2, t2, t0\n   andi t3, t0, 119\n"
        "add t2, t2, t3\n   srli t2, t2, 3\n   mulhu t2, t2, t1\n   xor t2, t2, t0\n   sub a3, t2, t0\n"
        "mul t2, a4, a1\n   srai t0, t2, 63\n   xor t2, t2, t0\n   sub t2, t2, t0\n   andi t3, t0, 119\n"
        "add t2, t2, t3\n   srli t2, t2, 3\n   mulhu t2, t2, t1\n   xor t2, t2, t0\n   sub a4, t2, t0\n"
        "mul t2, a5, a1\n   srai t0, t2, 63\n   xor t2, t2, t0\n   sub t2, t2, t0\n   not t3, t0\n   andi t3, t3, 119\n"
        "add t2, t2, t3\n   srli t2, t2, 3\n   mulhu t2, t2, t1\n   xor t2, t2, t0\n   sub a5, t2, t0\n"
        "mul t2, a6, a1\n   srai t0, t2, 63\n   xor t2, t2, t0\n   sub t2, t2, t0\n   not t3, t0\n   andi t3, t3, 119\n"
        "add t2, t2, t3\n   srli t2, t2, 3\n   mulhu t2, t2, t1\n   xor t2, t2, t0\n   sub a6, t2, t0\n"
        "sub a5, a5, a3\n   sub a6, a6, a4\n   sd a3, 0(a2)\n   sd a4, 8(a2)\n   sd a5, 16(a2)\n   sd a6, 24(a2)\n"
        "1:  " WLD_ASM_RET
        WLD_ASM_END(wl_direct_cover)

        /* wl_direct_text(a0=into, a1=cap, a2=text, a3=length) -> i64 */
        WLD_ASM_FUNC(wl_direct_text)
        "beqz a0, 9f\n   beqz a1, 9f\n   addi a1, a1, -1\n   li a4, 0\n   beqz a2, 8f\n   blez a3, 8f\n   li a5, 0\n"
        "1:  bge a5, a3, 8f\n   bgeu a4, a1, 8f\n   add t0, a2, a5\n   lbu a6, 0(t0)\n"
        "addi t1, a6, -0x20\n   li t2, 0x5e\n   bgtu t1, t2, 2f\n   add t1, a0, a4\n   sb a6, 0(t1)\n   addi a4, a4, 1\n   addi a5, a5, 1\n   j 1b\n"
        "2:  li t2, 0xc2\n   bltu a6, t2, 7f\n   sub t3, a3, a5\n   sub t4, a1, a4\n   li t2, 0xe0\n   bgeu a6, t2, 3f\n"
        "li t2, 2\n   bltu t3, t2, 7f\n   bltu t4, t2, 7f\n   lbu a7, 1(t0)\n"
        "andi t5, a7, 0xc0\n   li t6, 0x80\n   bne t5, t6, 7f\n"
        "add t1, a0, a4\n   sb a6, 0(t1)\n   sb a7, 1(t1)\n   addi a4, a4, 2\n   addi a5, a5, 2\n   j 1b\n"
        "3:  li t2, 0xf0\n   bgeu a6, t2, 5f\n   li t2, 3\n   bltu t3, t2, 7f\n   bltu t4, t2, 7f\n"
        "lbu a7, 1(t0)\n   lbu t3, 2(t0)\n   li t6, 0x80\n   andi t5, a7, 0xc0\n   bne t5, t6, 7f\n"
        "andi t5, t3, 0xc0\n   bne t5, t6, 7f\n   li t2, 0xe0\n   bne a6, t2, 4f\n   li t2, 0xa0\n   bltu a7, t2, 7f\n   j 10f\n"
        "4:  li t2, 0xed\n   bne a6, t2, 10f\n   li t2, 0xa0\n   bgeu a7, t2, 7f\n"
        "10: add t1, a0, a4\n   sb a6, 0(t1)\n   sb a7, 1(t1)\n   sb t3, 2(t1)\n   addi a4, a4, 3\n   addi a5, a5, 3\n   j 1b\n"
        "5:  li t2, 0xf4\n   bgtu a6, t2, 7f\n   li t2, 4\n   bltu t3, t2, 7f\n   bltu t4, t2, 7f\n"
        "lbu a7, 1(t0)\n   lbu t3, 2(t0)\n   lbu t4, 3(t0)\n   li t6, 0x80\n"
        "andi t5, a7, 0xc0\n   bne t5, t6, 7f\n   andi t5, t3, 0xc0\n   bne t5, t6, 7f\n   andi t5, t4, 0xc0\n   bne t5, t6, 7f\n"
        "li t2, 0xf0\n   bne a6, t2, 6f\n   li t2, 0x90\n   bltu a7, t2, 7f\n   j 11f\n"
        "6:  li t2, 0xf4\n   bne a6, t2, 11f\n   li t2, 0x90\n   bgeu a7, t2, 7f\n"
        "11: add t1, a0, a4\n   sb a6, 0(t1)\n   sb a7, 1(t1)\n   sb t3, 2(t1)\n   sb t4, 3(t1)\n   addi a4, a4, 4\n   addi a5, a5, 4\n   j 1b\n"
        "7:  li t2, 63\n   add t1, a0, a4\n   sb t2, 0(t1)\n   addi a4, a4, 1\n   addi a5, a5, 1\n   j 1b\n"
        "8:  add t1, a0, a4\n   sb zero, 0(t1)\n   mv a0, a4\n" WLD_ASM_RET
        "9:  li a0, 0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_text)

        /* wl_direct_pack_key(a0=out, a1=cap, a2=req) -> u32 */
        WLD_ASM_FUNC(wl_direct_pack_key)
        "beqz a0, 3f\n   beqz a2, 3f\n   ld a3, 0(a2)\n   ld a4, 8(a2)\n   beqz a3, 3f\n   blez a4, 3f\n"
        "ld t0, 16(a2)\n   srli t0, t0, 3\n   li t1, 0x20c49ba5e353f7cf\n   mulhu a5, t0, t1\n   srli a5, a5, 4\n"
        "lwu a6, 24(a2)\n   lwu a7, 28(a2)\n   lw t2, 32(a2)\n   snez t2, t2\n   li t3, 0x00180003\n   li t4, 0\n   li t5, 0\n"
        "1:  bge t5, a4, 2f\n   sub t6, a1, t4\n   li t0, 24\n   bltu t6, t0, 2f\n"
        "slli t0, t5, 2\n   add t0, a3, t0\n   lwu t1, 0(t0)\n   add t6, a0, t4\n"
        "sw t1, 0(t6)\n   sw t3, 4(t6)\n   sw a6, 8(t6)\n   sw a5, 12(t6)\n   sw a7, 16(t6)\n   sw t2, 20(t6)\n"
        "addi t4, t4, 24\n   addi t5, t5, 1\n   j 1b\n"
        "2:  slli a0, t4, 32\n   srli a0, a0, 32\n" WLD_ASM_RET
        "3:  li a0, 0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pack_key)

        /* wl_direct_pack_packet(a0=out, a1=cap, a2=pkt) -> u32 */
        WLD_ASM_FUNC(wl_direct_pack_packet)
        "beqz a0, 15f\n   beqz a2, 15f\n   addi sp, sp, -48\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n"
        "sd s3, 24(sp)\n   sd s4, 32(sp)\n   sd s5, 40(sp)\n   li a3, 0\n"
        "ld a4, 0(a2)\n   ld a5, 8(a2)\n   or t0, a4, a5\n   beqz t0, 4f\n"
        "ld a6, 32(a2)\n   ld a7, 40(a2)\n   beqz a6, 4f\n   blez a7, 4f\n"
        "slli s0, a4, 8\n   slli s1, a5, 8\n   mv s2, s0\n   mv s3, s1\n   ld t0, 24(a2)\n"
        "blez t0, 1f\n   li t1, 120\n   beq t0, t1, 1f\n   mul s2, s0, t1\n   div s2, s2, t0\n   mul s3, s1, t1\n   div s3, s3, t0\n"
        "1:  ld s4, 16(a2)\n   srli s5, s4, 32\n   li t2, 0x00200000\n   li t3, 0\n"
        "2:  bge t3, a7, 4f\n   sub t4, a1, a3\n   li t5, 32\n   bltu t4, t5, 14f\n"
        "slli t5, t3, 2\n   add t5, a6, t5\n   lwu t6, 0(t5)\n   add t4, a0, a3\n"
        "sw t6, 0(t4)\n   sw t2, 4(t4)\n   sw s5, 8(t4)\n   sw s4, 12(t4)\n   sw s2, 16(t4)\n   sw s3, 20(t4)\n"
        "sw s0, 24(t4)\n   sw s1, 28(t4)\n   addi a3, a3, 32\n   addi t3, t3, 1\n   j 2b\n"
        "4:  lwu a4, 88(a2)\n   lw a5, 100(a2)\n   or t0, a4, a5\n   beqz t0, 12f\n"
        "ld t0, 16(a2)\n   srli t0, t0, 3\n   li t1, 0x20c49ba5e353f7cf\n   mulhu s0, t0, t1\n   srli s0, s0, 4\n"
        "beqz a4, 7f\n   ld a6, 48(a2)\n   ld a7, 64(a2)\n   beqz a6, 7f\n   blez a7, 7f\n"
        "lwu s1, 96(a2)\n   lw s2, 92(a2)\n   snez s2, s2\n   li t2, 0x00180003\n   li t3, 0\n"
        "6:  bge t3, a7, 7f\n   sub t4, a1, a3\n   li t5, 24\n   bltu t4, t5, 14f\n"
        "slli t5, t3, 2\n   add t5, a6, t5\n   lwu t6, 0(t5)\n   add t4, a0, a3\n"
        "sw t6, 0(t4)\n   sw t2, 4(t4)\n   sw s1, 8(t4)\n   sw s0, 12(t4)\n   sw a4, 16(t4)\n   sw s2, 20(t4)\n"
        "addi a3, a3, 24\n   addi t3, t3, 1\n   j 6b\n"
        "7:  beqz a5, 12f\n   ld a6, 48(a2)\n   ld s1, 56(a2)\n   ld a7, 64(a2)\n   beqz a6, 12f\n   beqz s1, 12f\n   blez a7, 12f\n"
        "lw s2, 104(a2)\n   snez s2, s2\n   mv s3, a5\n   bnez s2, 16f\n   neg s3, s3\n"
        "16: slli s4, s3, 5\n   srai t0, s3, 63\n   xor t1, s3, t0\n   sub t1, t1, t0\n   srli t1, t1, 3\n"
        "li t2, 0x1111111111111112\n   mulhu t1, t1, t2\n   xor t1, t1, t0\n   sub s5, t1, t0\n   li t3, 0\n"
        "8:  bge t3, a7, 12f\n   slli t4, t3, 2\n   add t4, a6, t4\n   lwu t6, 0(t4)\n   add t4, s1, t3\n   lbu t5, 0(t4)\n"
        "li t0, 5\n   bltu t5, t0, 11f\n   sub t4, a1, a3\n   li t0, 12\n   bltu t4, t0, 14f\n"
        "add t4, a0, a3\n   li t0, 0x000c0006\n   sw t6, 0(t4)\n   sw t0, 4(t4)\n   sw zero, 8(t4)\n   addi a3, a3, 12\n"
        "li t0, 8\n   bltu t5, t0, 9f\n   sub t4, a1, a3\n   li t0, 16\n   bltu t4, t0, 14f\n"
        "add t4, a0, a3\n   li t0, 0x00100009\n   sw t6, 0(t4)\n   sw t0, 4(t4)\n   sw s2, 8(t4)\n   sw s3, 12(t4)\n   addi a3, a3, 16\n   j 11f\n"
        "9:  beqz s5, 11f\n   sub t4, a1, a3\n   li t0, 16\n   bltu t4, t0, 14f\n"
        "add t4, a0, a3\n   li t0, 0x00100008\n   sw t6, 0(t4)\n   sw t0, 4(t4)\n   sw s2, 8(t4)\n   sw s5, 12(t4)\n   addi a3, a3, 16\n"
        "11: sub t4, a1, a3\n   li t0, 20\n   bltu t4, t0, 14f\n   add t4, a0, a3\n   li t0, 0x00140004\n"
        "sw t6, 0(t4)\n   sw t0, 4(t4)\n   sw s0, 8(t4)\n   sw s2, 12(t4)\n   sw s4, 16(t4)\n   addi a3, a3, 20\n   addi t3, t3, 1\n   j 8b\n"
        "12: ld a6, 72(a2)\n   ld a7, 80(a2)\n   beqz a6, 14f\n   blez a7, 14f\n   li t2, 0x00080005\n   li t3, 0\n"
        "13: bge t3, a7, 14f\n   sub t4, a1, a3\n   li t5, 8\n   bltu t4, t5, 14f\n"
        "slli t5, t3, 2\n   add t5, a6, t5\n   lwu t6, 0(t5)\n   add t4, a0, a3\n   sw t6, 0(t4)\n   sw t2, 4(t4)\n   addi a3, a3, 8\n   addi t3, t3, 1\n   j 13b\n"
        "14: slli a0, a3, 32\n   srli a0, a0, 32\n   ld s5, 40(sp)\n   ld s4, 32(sp)\n   ld s3, 24(sp)\n"
        "ld s2, 16(sp)\n   ld s1, 8(sp)\n   ld s0, 0(sp)\n   addi sp, sp, 48\n" WLD_ASM_RET
        "15: li a0, 0\n" WLD_ASM_RET
        WLD_ASM_END(wl_direct_pack_packet)
);

#endif
#endif /* CANVAS_WAYLAND_DIRECT_ASM_IMPLEMENTATION */
