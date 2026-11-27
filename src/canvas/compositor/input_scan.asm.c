/*
        Compositor RT input-thread evdev batch scanner, OLS frame-ahead pointer
        predictor, and wheel-stream acceleration primitives in pure assembly:
          - inscan_evdev_batch  : single-pass evdev batch classifier (MSC/SYN noise,
                                  direct key, direct pointer/btn/wheel, fallback)
          - inscan_scale_rem    : remainder-preserving 64-bit integer scaler
          - inscan_predict_lead : OLS linear-regression pointer lead with straightness,
                                  stillness, reversal-kill, and circular isqrt cap
          - inscan_wheel_stream : wheel stream gap/memory state machine
          - inscan_stream_gain  : wheel stream rate -> 1.5-power integer isqrt gain
          - inscan_stream_rate  : 6-slot circular timestamp history -> rate
          - inscan_stream_turn  : clamped 120th-notch wheel turn with remainder

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_INPUT_SCAN_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_INPUT_SCAN_ASM_H
#define MOONWATER_COMPOSITOR_INPUT_SCAN_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  inscan_u8;
typedef p16 inscan_u16;
typedef p32 inscan_u32;
typedef b32 inscan_i32;
typedef p64 inscan_u64;
typedef b64 inscan_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  inscan_u8;
typedef __UINT16_TYPE__ inscan_u16;
typedef __UINT32_TYPE__ inscan_u32;
typedef __INT32_TYPE__  inscan_i32;
typedef __UINT64_TYPE__ inscan_u64;
typedef __INT64_TYPE__  inscan_i64;
#else
#include <stdint.h>
typedef uint8_t  inscan_u8;
typedef uint16_t inscan_u16;
typedef uint32_t inscan_u32;
typedef int32_t  inscan_i32;
typedef uint64_t inscan_u64;
typedef int64_t  inscan_i64;
#endif

/* inscan_evdev_batch return codes */
#define INSCAN_BATCH_FALLBACK 0
#define INSCAN_BATCH_NOISE    1
#define INSCAN_BATCH_KEY      2
#define INSCAN_BATCH_POINTER  3

/* inscan_evdev_batch flags (bits 3..4 match compositor_reader.shape hires bits) */
#define INSCAN_F_FLIP         1u
#define INSCAN_F_NO_DIRECT    2u
#define INSCAN_F_HIRES_V      8u
#define INSCAN_F_HIRES_H      16u

/* Exact compositor constants mirrored by assembly */
#define INSCAN_PREDICT_HISTORY       16u
#define INSCAN_PREDICT_FX            1024
#define INSCAN_PREDICT_CAP           4
#define INSCAN_PREDICT_WINDOW_NS     50000000ull
#define INSCAN_PREDICT_SPAN_NS       4000000ull
#define INSCAN_PREDICT_STRAIGHT_LO   600
#define INSCAN_PREDICT_STRAIGHT_SPAN 300
#define INSCAN_PREDICT_STILL_LO      40
#define INSCAN_PREDICT_STILL_HI      80
#define INSCAN_HAND_GAP_NS           60000000ull
#define INSCAN_WHEEL_MEMORY_NS       3000000000ull
#define INSCAN_WHEEL_STREAMS         3u
#define INSCAN_GAIN_BASE             2048
#define INSCAN_STREAM_HIST           6u
#define INSCAN_GAIN_TURN             1024000

/* 24 bytes — byte-for-byte identical to struct compositor_input_event */
struct inscan_event {
        inscan_i64 seconds;      /* +0  */
        inscan_i64 microseconds; /* +8  */
        inscan_u16 type;         /* +16 */
        inscan_u16 code;         /* +18 */
        inscan_i32 value;        /* +20 */
};

/* 48 bytes — classified evdev batch output */
struct inscan_batch {
        inscan_i64 dx;           /* +0  */
        inscan_i64 dy;           /* +8  */
        inscan_u64 micro;        /* +16 */
        inscan_i32 key;          /* +24 */
        inscan_i32 key_val;      /* +28 */
        inscan_u32 btn;          /* +32 */
        inscan_i32 btn_val;      /* +36 */
        inscan_i32 wheel_v120;   /* +40 */
        inscan_i32 wheel_horiz;  /* +44 */
};

/* 544 bytes — byte-for-byte identical to struct compositor_predict */
struct inscan_predict {
        inscan_i64 x[16];        /* +0   */
        inscan_i64 y[16];        /* +128 */
        inscan_u64 when[16];     /* +256 */
        inscan_u64 arrived[16];  /* +384 */
        inscan_u32 count;        /* +512 */
        inscan_u32 _pad;         /* +516 */
        inscan_u64 last;         /* +520 */
        inscan_i64 lead_x;       /* +528 */
        inscan_i64 lead_y;       /* +536 */
};

/* 32 bytes — byte-for-byte identical to prefix of struct compositor_device */
struct inscan_wheel_dev {
        inscan_i32 number;          /* +0  */
        inscan_u8  wakeonly;        /* +4  */
        inscan_u8  wheel_streamed;  /* +5  */
        inscan_u8  pad_slot;        /* +6  */
        inscan_u8  touch_slot;      /* +7  */
        inscan_u32 wheel_run;       /* +8  */
        inscan_u32 _pad;            /* +12 */
        inscan_u64 wheel_at;        /* +16 */
        inscan_u64 wheel_stream_at; /* +24 */
};

#ifdef offset_of
#define INSCAN_OFFSET(T, M) offset_of(T, M)
#else
#define INSCAN_OFFSET(T, M) ((inscan_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct inscan_event)     == 24,  "inscan_event must be 24 bytes");
_Static_assert(INSCAN_OFFSET(struct inscan_event, seconds)      == 0,  "inscan_event.seconds");
_Static_assert(INSCAN_OFFSET(struct inscan_event, microseconds) == 8,  "inscan_event.microseconds");
_Static_assert(INSCAN_OFFSET(struct inscan_event, type)         == 16, "inscan_event.type");
_Static_assert(INSCAN_OFFSET(struct inscan_event, code)         == 18, "inscan_event.code");
_Static_assert(INSCAN_OFFSET(struct inscan_event, value)        == 20, "inscan_event.value");

_Static_assert(sizeof(struct inscan_batch)     == 48,  "inscan_batch must be 48 bytes");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, dx)           == 0,  "inscan_batch.dx");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, dy)           == 8,  "inscan_batch.dy");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, micro)        == 16, "inscan_batch.micro");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, key)          == 24, "inscan_batch.key");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, key_val)      == 28, "inscan_batch.key_val");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, btn)          == 32, "inscan_batch.btn");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, btn_val)      == 36, "inscan_batch.btn_val");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, wheel_v120)   == 40, "inscan_batch.wheel_v120");
_Static_assert(INSCAN_OFFSET(struct inscan_batch, wheel_horiz)  == 44, "inscan_batch.wheel_horiz");

_Static_assert(sizeof(struct inscan_predict)   == 544, "inscan_predict must be 544 bytes");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, x)          == 0,   "inscan_predict.x");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, y)          == 128, "inscan_predict.y");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, when)       == 256, "inscan_predict.when");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, arrived)    == 384, "inscan_predict.arrived");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, count)      == 512, "inscan_predict.count");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, last)       == 520, "inscan_predict.last");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, lead_x)     == 528, "inscan_predict.lead_x");
_Static_assert(INSCAN_OFFSET(struct inscan_predict, lead_y)     == 536, "inscan_predict.lead_y");

_Static_assert(sizeof(struct inscan_wheel_dev) == 32,  "inscan_wheel_dev must be 32 bytes");
_Static_assert(INSCAN_OFFSET(struct inscan_wheel_dev, wheel_streamed)  == 5,  "inscan_wheel_dev.wheel_streamed");
_Static_assert(INSCAN_OFFSET(struct inscan_wheel_dev, wheel_run)       == 8,  "inscan_wheel_dev.wheel_run");
_Static_assert(INSCAN_OFFSET(struct inscan_wheel_dev, wheel_at)        == 16, "inscan_wheel_dev.wheel_at");
_Static_assert(INSCAN_OFFSET(struct inscan_wheel_dev, wheel_stream_at) == 24, "inscan_wheel_dev.wheel_stream_at");

inscan_i32 inscan_evdev_batch(const struct inscan_event *raw, inscan_u32 count, inscan_u32 flags, struct inscan_batch *out);
inscan_i64 inscan_scale_rem(inscan_i64 delta, inscan_i64 num, inscan_i64 den, inscan_i64 *rem);
void       inscan_predict_lead(struct inscan_predict *track, inscan_u64 period_ns, inscan_i32 scale);
inscan_i32 inscan_wheel_stream(struct inscan_wheel_dev *dev, inscan_u32 *hist_n, inscan_u64 now);
inscan_i64 inscan_stream_gain(inscan_u64 rate);
inscan_u64 inscan_stream_rate(const inscan_u64 *hist, inscan_u32 n);
inscan_i64 inscan_stream_turn(inscan_i64 value120, inscan_i64 gain, inscan_i32 speed, inscan_i64 *rest);

#endif /* MOONWATER_COMPOSITOR_INPUT_SCAN_ASM_H */

#if defined(COMPOSITOR_INPUT_SCAN_ASM_IMPLEMENTATION) && !defined(COMPOSITOR_INPUT_SCAN_ASM_EMITTED)
#define COMPOSITOR_INPUT_SCAN_ASM_EMITTED 1

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define INSCAN_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define INSCAN_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define INSCAN_RISCV64 1
# else
#  error "src/canvas/compositor/input_scan.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define INSCAN_ARM64 1
# elif defined(X64) && X64
#  define INSCAN_X64 1
# elif defined(RISCV64) && RISCV64
#  define INSCAN_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define INSCAN_ASM_SECTION   ASM_SECTION
# define INSCAN_ASM_FUNC(n)   ASM_FUNC(n)
# define INSCAN_ASM_END(n)    ASM_END(n)
# define INSCAN_ASM_RET       ASM_RET
#else
# define INSCAN_ASM_SECTION   ".text\n"
# define INSCAN_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define INSCAN_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define INSCAN_ASM_END(n)   ""
# elif defined(_WIN32)
#  define INSCAN_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define INSCAN_ASM_END(n)   ""
# else
#  if defined(INSCAN_ARM64)
#   define INSCAN_ASM_FTYPE   "%function"
#  else
#   define INSCAN_ASM_FTYPE   "@function"
#  endif
#  define INSCAN_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " INSCAN_ASM_FTYPE "\n" #n ":\n"
#  define INSCAN_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(INSCAN_ARM64)

__asm__(
        INSCAN_ASM_SECTION

        /* inscan_evdev_batch(x0=raw, w1=count, w2=flags, x3=out) -> 0:fallback 1:noise 2:key 3:pointer */
        INSCAN_ASM_FUNC(inscan_evdev_batch)
        "cbz x0, 20f\n   cbz w1, 20f\n   mov w1, w1\n   add x17, x1, x1, lsl #1\n   add x1, x0, x17, lsl #3\n"
        "tbnz w2, #1, 15f\n   cbz x3, 20f\n   mov x13, x0\n   mov x4, #0\n   mov x5, #0\n"
        "mov w6, #0\n   mov w7, #0\n   mov w8, #-1\n   mov w9, #0\n   mov w10, #0\n   mov w11, #0\n   mov w12, #1\n"
        "1:  ldp w14, w16, [x13, #16]\n   add x13, x13, #24\n   cbz w14, 10f\n   lsr w15, w14, #16\n   and w14, w14, #0xffff\n"
        "cmp w14, #4\n   b.eq 10f\n   mov w12, #0\n   cmp w14, #2\n   b.ne 6f\n"
        "cbnz w15, 3f\n   add x4, x4, w16, sxtw\n   b 10f\n"
        "3:  cmp w15, #1\n   b.ne 4f\n   add x5, x5, w16, sxtw\n   b 10f\n"
        "4:  cbnz w10, 20f\n   cmp w15, #8\n   b.eq 41f\n   cmp w15, #6\n   b.eq 42f\n"
        "cmp w15, #11\n   b.eq 43f\n   cmp w15, #12\n   b.ne 20f\n   mov w11, #1\n   b 44f\n"
        "41: tbnz w2, #3, 10f\n   mov w11, #0\n   b 45f\n"
        "42: tbnz w2, #4, 10f\n   mov w11, #1\n"
        "45: mov w17, #120\n   mul w16, w16, w17\n   b 44f\n"
        "43: mov w11, #0\n"
        "44: neg w17, w16\n   tst w2, #1\n   csel w10, w17, w16, ne\n   b 10f\n"
        "6:  cmp w14, #1\n   b.ne 20f\n   sub w17, w15, #0x110\n   cmp w17, #7\n   b.hi 7f\n"
        "cbnz w6, 20f\n   mov w6, w15\n   mov w7, w16\n   b 10f\n"
        "7:  cmp w15, #255\n   b.hi 20f\n   tbnz w8, #31, 8f\n   b 20f\n"
        "8:  mov w8, w15\n   mov w9, w16\n"
        "10: cmp x13, x1\n   b.lo 1b\n   cbnz w12, 19f\n"
        "ldp x14, x15, [x1, #-24]\n   movz x16, #0x4240\n   movk x16, #0x000f, lsl #16\n   madd x14, x14, x16, x15\n"
        "orr x17, x4, x5\n   orr w15, w6, w10\n   orr x17, x17, x15\n"
        "tbnz w8, #31, 12f\n   cbnz x17, 20f\n   stp xzr, xzr, [x3]\n   str x14, [x3, #16]\n"
        "stp w8, w9, [x3, #24]\n   stp xzr, xzr, [x3, #32]\n   mov w0, #2\n" INSCAN_ASM_RET
        "12: cbz x17, 20f\n   stp x4, x5, [x3]\n   str x14, [x3, #16]\n   stp w8, wzr, [x3, #24]\n"
        "stp w6, w7, [x3, #32]\n   stp w10, w11, [x3, #40]\n   mov w0, #3\n" INSCAN_ASM_RET
        "15: mov x13, x0\n"
        "16: ldr w14, [x13, #16]\n   add x13, x13, #24\n   cbz w14, 17f\n   and w14, w14, #0xffff\n   cmp w14, #4\n   b.ne 20f\n"
        "17: cmp x13, x1\n   b.lo 16b\n"
        "19: mov w0, #1\n" INSCAN_ASM_RET
        "20: mov w0, #0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_evdev_batch)

        /* inscan_scale_rem(x0=delta, x1=num, x2=den, x3=rem) -> out */
        INSCAN_ASM_FUNC(inscan_scale_rem)
        "cbz x2, 1f\n   cbz x3, 1f\n   ldr x4, [x3]\n   madd x4, x0, x1, x4\n"
        "sdiv x0, x4, x2\n   msub x5, x0, x2, x4\n   str x5, [x3]\n" INSCAN_ASM_RET
        "1:  mov x0, #0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_scale_rem)

        /* inscan_predict_lead(x0=track, x1=period_ns, w2=scale) */
        INSCAN_ASM_FUNC(inscan_predict_lead)
        "cbz x0, 99f\n   str xzr, [x0, #528]\n   str xzr, [x0, #536]\n   ldr w3, [x0, #512]\n"
        "cmp w3, #2\n   b.lo 99f\n   cmp w3, #16\n   b.hi 99f\n"
        "sxtw x4, w2\n   lsl x4, x4, #2\n   mov x5, #100\n   sdiv x4, x4, x5\n   cmp x4, #0\n   b.le 99f\n"
        "sub w5, w3, #1\n   add x6, x0, #256\n   ldr x7, [x6, x5, lsl #3]\n"
        "movz x8, #0xf080\n   movk x8, #0x02fa, lsl #16\n   sub x8, x7, x8\n   mov w9, w5\n"
        "1:  cbz w9, 2f\n   sub w10, w9, #1\n   ldr x11, [x6, x10, lsl #3]\n   cmp x11, x8\n   b.lt 2f\n   mov w9, w10\n   b 1b\n"
        "2:  sub w10, w3, w9\n   cmp w10, #2\n   b.lt 99f\n   ldr x11, [x6, x9, lsl #3]\n   sub x11, x7, x11\n"
        "movz x12, #0x0900\n   movk x12, #0x003d, lsl #16\n   cmp x11, x12\n   b.lt 99f\n"
        "stp x19, x20, [sp, #-64]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n   stp x25, x26, [sp, #48]\n"
        "mov x19, #0\n   mov x20, #0\n   mov x21, #0\n   mov x22, #0\n   mov x23, #0\n   mov x24, #0\n   mov x25, #0\n"
        "mov w12, w9\n   movz x13, #0xf7cf\n   movk x13, #0xe353, lsl #16\n   movk x13, #0x9ba5, lsl #32\n   movk x13, #0x20c4, lsl #48\n   add x14, x0, #128\n"
        "3:  ldr x15, [x6, x12, lsl #3]\n   sub x15, x15, x7\n   smulh x8, x15, x13\n   asr x8, x8, #7\n   add x15, x8, x15, lsr #63\n"
        "ldr x16, [x0, x12, lsl #3]\n   ldr x17, [x14, x12, lsl #3]\n"
        "add x19, x19, x15\n   madd x20, x15, x15, x20\n   add x21, x21, x16\n   madd x22, x15, x16, x22\n"
        "add x23, x23, x17\n   madd x24, x15, x17, x24\n   cmp w12, w9\n   b.eq 4f\n"
        "sub x8, x16, x26\n   cmp x8, #0\n   cneg x8, x8, lt\n   add x25, x25, x8\n"
        "sub x8, x17, x2\n   cmp x8, #0\n   cneg x8, x8, lt\n   add x25, x25, x8\n"
        "4:  mov x26, x16\n   mov x2, x17\n   add w12, w12, #1\n   cmp w12, w3\n   b.lo 3b\n"
        "sxtw x10, w10\n   mul x8, x10, x20\n   msub x8, x19, x19, x8\n   cbz x8, 98f\n"
        "smulh x12, x1, x13\n   asr x12, x12, #7\n   add x1, x12, x1, lsr #63\n"
        "mul x12, x10, x22\n   msub x12, x19, x21, x12\n   mul x12, x12, x1\n   sdiv x12, x12, x8\n"
        "mul x15, x10, x24\n   msub x15, x19, x23, x15\n   mul x15, x15, x1\n   sdiv x15, x15, x8\n"
        "ldr x16, [x0, x9, lsl #3]\n   ldr x17, [x14, x9, lsl #3]\n"
        "sub x16, x26, x16\n   cmp x16, #0\n   cneg x16, x16, lt\n"
        "sub x17, x2, x17\n   cmp x17, #0\n   cneg x17, x17, lt\n   add x16, x16, x17\n"
        "mov x1, #1000\n   mov x17, #0\n   cmp x25, #0\n   b.le 5f\n   mul x17, x16, x1\n   sdiv x17, x17, x25\n"
        "5:  sub x17, x17, #600\n   mul x17, x17, x1\n   mov x8, #300\n   sdiv x17, x17, x8\n"
        "cmp x17, #0\n   csel x17, xzr, x17, lt\n   cmp x17, x1\n   csel x17, x1, x17, gt\n"
        "movz x8, #0x4240\n   movk x8, #0x000f, lsl #16\n   mul x8, x16, x8\n   sdiv x8, x8, x11\n"
        "sub x8, x8, #40\n   mov x11, #25\n   mul x8, x8, x11\n"
        "cmp x8, #0\n   csel x8, xzr, x8, lt\n   cmp x8, x1\n   csel x8, x1, x8, gt\n"
        "mul x12, x12, x17\n   smulh x16, x12, x13\n   asr x16, x16, #7\n   add x12, x16, x12, lsr #63\n"
        "mul x12, x12, x8\n   smulh x16, x12, x13\n   asr x16, x16, #7\n   add x12, x16, x12, lsr #63\n"
        "mul x15, x15, x17\n   smulh x16, x15, x13\n   asr x16, x16, #7\n   add x15, x16, x15, lsr #63\n"
        "mul x15, x15, x8\n   smulh x16, x15, x13\n   asr x16, x16, #7\n   add x15, x16, x15, lsr #63\n"
        "sub w5, w3, #2\n   ldr x16, [x0, x5, lsl #3]\n   sub x16, x26, x16\n"
        "eor x17, x16, x12\n   cbz x16, 6f\n   tbnz x17, #63, 61f\n   b 6f\n"
        "61: mov x12, #0\n"
        "6:  ldr x16, [x14, x5, lsl #3]\n   sub x16, x2, x16\n"
        "eor x17, x16, x15\n   cbz x16, 7f\n   tbnz x17, #63, 71f\n   b 7f\n"
        "71: mov x15, #0\n"
        "7:  cmp x12, #0\n   cneg x16, x12, lt\n   add x16, x16, #512\n   lsr x16, x16, #10\n   cmp x12, #0\n   cneg x12, x16, lt\n"
        "cmp x15, #0\n   cneg x16, x15, lt\n   add x16, x16, #512\n   lsr x16, x16, #10\n   cmp x15, #0\n   cneg x15, x16, lt\n"
        "mul x16, x12, x12\n   madd x16, x15, x15, x16\n   mul x17, x4, x4\n   cmp x16, x17\n   b.le 9f\n"
        "clz x8, x16\n   mov x9, #63\n   sub x8, x9, x8\n   bic x8, x8, #1\n   mov x9, #1\n   lsl x9, x9, x8\n   mov x10, #0\n   mov x11, x16\n"
        "8:  add x13, x10, x9\n   lsr x10, x10, #1\n   cmp x11, x13\n   b.lo 81f\n   sub x11, x11, x13\n   add x10, x10, x9\n"
        "81: lsr x9, x9, #2\n   cbnz x9, 8b\n   mul x13, x10, x10\n   cmp x13, x16\n   cinc x10, x10, lo\n"
        "mul x12, x12, x4\n   sdiv x12, x12, x10\n   mul x15, x15, x4\n   sdiv x15, x15, x10\n"
        "9:  lsl x12, x12, #10\n   lsl x15, x15, #10\n   str x12, [x0, #528]\n   str x15, [x0, #536]\n"
        "98: ldp x25, x26, [sp, #48]\n   ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #64\n"
        "99: " INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_predict_lead)

        /* inscan_wheel_stream(x0=dev, x1=hist_n, x2=now) -> 0/1 */
        INSCAN_ASM_FUNC(inscan_wheel_stream)
        "cbz x0, 5f\n   ldp x3, x4, [x0, #16]\n   mov w5, #0\n"
        "cbz x3, 1f\n   cmp x2, x3\n   b.lo 1f\n   sub x6, x2, x3\n"
        "movz x7, #0x8700\n   movk x7, #0x0393, lsl #16\n   cmp x6, x7\n   b.hi 1f\n   mov w5, #1\n   b 2f\n"
        "1:  ldrb w6, [x0, #5]\n   cbz w6, 3f\n   cbz x4, 3f\n   cmp x2, x4\n   b.lo 3f\n"
        "sub x6, x2, x4\n   movz x7, #0x5e00\n   movk x7, #0xb2d0, lsl #16\n   cmp x6, x7\n   b.hi 3f\n   mov w5, #1\n"
        "2:  ldr w6, [x0, #8]\n   add w6, w6, #1\n   str w6, [x0, #8]\n"
        "cmp w6, #3\n   cset w7, hs\n   ldrb w8, [x0, #5]\n   orr w8, w8, w7\n   strb w8, [x0, #5]\n   str x2, [x0, #24]\n   b 4f\n"
        "3:  str wzr, [x0, #8]\n   cbz x1, 4f\n   str wzr, [x1]\n"
        "4:  str x2, [x0, #16]\n   mov w0, w5\n" INSCAN_ASM_RET
        "5:  mov w0, #0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_wheel_stream)

        /* inscan_stream_gain(x0=rate) -> gain */
        INSCAN_ASM_FUNC(inscan_stream_gain)
        "cmp x0, #1000\n   b.ls 3f\n   movz x1, #0x86a0\n   movk x1, #0x0001, lsl #16\n"
        "sub x2, x0, #1000\n   cmp x0, x1\n   csel x2, x1, x2, hi\n"
        "lsl x2, x2, #16\n   mov x3, #3500\n   udiv x2, x2, x3\n   mov x3, #65536\n   cmp x2, x3\n   csel x2, x3, x2, hi\n"
        "lsl x4, x2, #16\n   clz x5, x4\n   mov x6, #63\n   sub x5, x6, x5\n   bic x5, x5, #1\n   mov x6, #1\n   lsl x5, x6, x5\n   mov x6, #0\n"
        "1:  add x7, x6, x5\n   lsr x6, x6, #1\n   subs x8, x4, x7\n   csel x4, x8, x4, hs\n   add x7, x6, x5\n   csel x6, x7, x6, hs\n   lsr x5, x5, #2\n   cbnz x5, 1b\n"
        "mul x2, x2, x6\n   lsr x2, x2, #16\n   mov x3, #6144\n   mul x2, x2, x3\n   lsr x2, x2, #16\n   add x0, x2, #2048\n" INSCAN_ASM_RET
        "3:  mov x0, #2048\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_gain)

        /* inscan_stream_rate(x0=hist, w1=n) -> rate */
        INSCAN_ASM_FUNC(inscan_stream_rate)
        "cbz x0, 2f\n   cmp w1, #2\n   b.lo 2f\n   mov w2, #6\n   cmp w1, w2\n   csel w3, w1, w2, lo\n"
        "sub w4, w1, #1\n   udiv w5, w4, w2\n   msub w4, w5, w2, w4\n   ldr x6, [x0, x4, lsl #3]\n"
        "sub w4, w1, w3\n   udiv w5, w4, w2\n   msub w4, w5, w2, w4\n   ldr x7, [x0, x4, lsl #3]\n"
        "cmp x6, x7\n   b.lo 2f\n   movz x4, #0xe800\n   movk x4, #0x4876, lsl #16\n   movk x4, #0x0017, lsl #32\n"
        "b.eq 1f\n   sub w3, w3, #1\n   sub x6, x6, x7\n   mul x4, x3, x4\n   udiv x0, x4, x6\n" INSCAN_ASM_RET
        "1:  mov x0, x4\n" INSCAN_ASM_RET
        "2:  mov x0, #0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_rate)

        /* inscan_stream_turn(x0=value120, x1=gain, w2=speed, x3=rest) -> out */
        INSCAN_ASM_FUNC(inscan_stream_turn)
        "cbz x3, 1f\n   movz x4, #0xca00\n   movk x4, #0x3b9a, lsl #16\n   neg x5, x4\n"
        "cmp x0, x5\n   csel x0, x5, x0, lt\n   cmp x0, x4\n   csel x0, x4, x0, gt\n"
        "sxtw x2, w2\n   mul x1, x1, x2\n   ldr x4, [x3]\n   madd x4, x0, x1, x4\n"
        "movz x5, #0xa000\n   movk x5, #0x000f, lsl #16\n   sdiv x0, x4, x5\n   msub x6, x0, x5, x4\n   str x6, [x3]\n" INSCAN_ASM_RET
        "1:  mov x0, #0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_turn)
);

#elif defined(INSCAN_X64)

#if defined(_WIN32)
#define INSCAN_X64_SAVE_DI_SI   "push rdi\n   push rsi\n"
#define INSCAN_X64_REST_DI_SI   "pop rsi\n   pop rdi\n"
#define INSCAN_X64_ARG1         rcx
#define INSCAN_X64_ARG2         rdx
#define INSCAN_X64_ARG3         r8
#define INSCAN_X64_ARG4         r9
#define INSCAN_X64_LOAD4_EV     "mov r12, rcx\n   mov r13d, edx\n   mov r14d, r8d\n   mov r15, r9\n"
#define INSCAN_X64_LOAD4_SR     "mov r8, rcx\n   mov r9, rdx\n   mov r10, r8\n" /* explicit per-fn below */
#define INSCAN_X64_LOAD3_PL     "mov rbx, rcx\n   mov r12, rdx\n   movsxd r13, r8d\n"
#define INSCAN_X64_LOAD3_WS     "mov r10, rcx\n   mov r11, rdx\n   mov r9, r8\n"
#define INSCAN_X64_LOAD2_RATE   "mov r8, rcx\n   mov r9d, edx\n"
#else
#define INSCAN_X64_SAVE_DI_SI   ""
#define INSCAN_X64_REST_DI_SI   ""
#define INSCAN_X64_ARG1         rdi
#define INSCAN_X64_ARG2         rsi
#define INSCAN_X64_ARG3         rdx
#define INSCAN_X64_ARG4         rcx
#define INSCAN_X64_LOAD4_EV     "mov r12, rdi\n   mov r13d, esi\n   mov r14d, edx\n   mov r15, rcx\n"
#define INSCAN_X64_LOAD3_PL     "mov rbx, rdi\n   mov r12, rsi\n   movsxd r13, edx\n"
#define INSCAN_X64_LOAD3_WS     "mov r10, rdi\n   mov r11, rsi\n   mov r9, rdx\n"
#define INSCAN_X64_LOAD2_RATE   "mov r8, rdi\n   mov r9d, esi\n"
#endif

__asm__(
        INSCAN_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* inscan_evdev_batch(raw, count, flags, out) */
        INSCAN_ASM_FUNC(inscan_evdev_batch)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        INSCAN_X64_SAVE_DI_SI
        INSCAN_X64_LOAD4_EV
        "test r12, r12\n   jz 20f\n   test r13d, r13d\n   jz 20f\n"
        "lea rax, [r13 + r13*2]\n   lea r13, [r12 + rax*8]\n"
        "test r14d, 2\n   jnz 15f\n   test r15, r15\n   jz 20f\n"
        "xor r8d, r8d\n   xor r9d, r9d\n   xor ebx, ebx\n   xor ebp, ebp\n"
        "mov edi, -1\n   xor esi, esi\n   xor r10d, r10d\n   xor r11d, r11d\n   mov ecx, 1\n"
        "1:  movzx eax, word ptr [r12 + 16]\n   movzx edx, word ptr [r12 + 18]\n   push rcx\n   mov ecx, dword ptr [r12 + 20]\n   add r12, 24\n"
        "cmp eax, 4\n   je 10f\n   test eax, eax\n   jnz 2f\n   test edx, edx\n   jz 10f\n   pop rcx\n   jmp 20f\n"
        "2:  mov dword ptr [rsp], 0\n   cmp eax, 2\n   jne 6f\n   test edx, edx\n   jnz 3f\n   movsxd rax, ecx\n   add r8, rax\n   jmp 10f\n"
        "3:  cmp edx, 1\n   jne 4f\n   movsxd rax, ecx\n   add r9, rax\n   jmp 10f\n"
        "4:  test r10d, r10d\n   jnz 21f\n   cmp edx, 8\n   je 41f\n   cmp edx, 6\n   je 42f\n"
        "cmp edx, 11\n   je 43f\n   cmp edx, 12\n   jne 21f\n   mov r11d, 1\n   jmp 44f\n"
        "41: test r14d, 8\n   jnz 10f\n   xor r11d, r11d\n   jmp 45f\n"
        "42: test r14d, 16\n   jnz 10f\n   mov r11d, 1\n"
        "45: imul ecx, ecx, 120\n   jmp 44f\n"
        "43: xor r11d, r11d\n"
        "44: mov eax, ecx\n   neg eax\n   test r14d, 1\n   cmovnz ecx, eax\n   mov r10d, ecx\n   jmp 10f\n"
        "6:  cmp eax, 1\n   jne 21f\n   lea eax, [rdx - 0x110]\n   cmp eax, 7\n   ja 7f\n"
        "test ebx, ebx\n   jnz 21f\n   mov ebx, edx\n   mov ebp, ecx\n   jmp 10f\n"
        "7:  cmp edx, 255\n   ja 21f\n   test edi, edi\n   jns 21f\n   mov edi, edx\n   mov esi, ecx\n"
        "10: pop rcx\n   cmp r12, r13\n   jb 1b\n   test ecx, ecx\n   jnz 19f\n"
        "mov rax, [r13 - 24]\n   imul rax, rax, 1000000\n   add rax, [r13 - 16]\n"
        "mov rdx, r8\n   or rdx, r9\n   mov ecx, ebx\n   or ecx, r10d\n   or rdx, rcx\n"
        "test edi, edi\n   js 12f\n   test rdx, rdx\n   jnz 20f\n"
        "mov qword ptr [r15], 0\n   mov qword ptr [r15 + 8], 0\n   mov [r15 + 16], rax\n"
        "mov [r15 + 24], edi\n   mov [r15 + 28], esi\n   mov qword ptr [r15 + 32], 0\n   mov qword ptr [r15 + 40], 0\n"
        "mov eax, 2\n   jmp 22f\n"
        "12: test rdx, rdx\n   jz 20f\n   mov [r15], r8\n   mov [r15 + 8], r9\n   mov [r15 + 16], rax\n"
        "mov [r15 + 24], edi\n   mov dword ptr [r15 + 28], 0\n   mov [r15 + 32], ebx\n   mov [r15 + 36], ebp\n"
        "mov [r15 + 40], r10d\n   mov [r15 + 44], r11d\n   mov eax, 3\n   jmp 22f\n"
        "15: movzx eax, word ptr [r12 + 16]\n   movzx edx, word ptr [r12 + 18]\n   add r12, 24\n"
        "cmp eax, 4\n   je 17f\n   test eax, eax\n   jnz 20f\n   test edx, edx\n   jnz 20f\n"
        "17: cmp r12, r13\n   jb 15b\n"
        "19: mov eax, 1\n   jmp 22f\n"
        "21: pop rcx\n"
        "20: xor eax, eax\n"
        "22: " INSCAN_X64_REST_DI_SI "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_evdev_batch)

        /* inscan_scale_rem(delta, num, den, rem) -> out */
        INSCAN_ASM_FUNC(inscan_scale_rem)
#if defined(_WIN32)
        "mov rax, rcx\n   mov r10, rdx\n   mov r11, r8\n   mov rcx, r9\n"
#else
        "mov rax, rdi\n   mov r10, rsi\n   mov r11, rdx\n"
#endif
        "test r11, r11\n   jz 2f\n   test rcx, rcx\n   jz 2f\n"
        "imul rax, r10\n   add rax, [rcx]\n   cmp r11, -1\n   je 1f\n"
        "cqo\n   idiv r11\n   mov [rcx], rdx\n" INSCAN_ASM_RET
        "1:  neg rax\n   mov qword ptr [rcx], 0\n" INSCAN_ASM_RET
        "2:  xor eax, eax\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_scale_rem)

        /* inscan_predict_lead(track, period_ns, scale) */
        INSCAN_ASM_FUNC(inscan_predict_lead)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
        INSCAN_X64_SAVE_DI_SI
        INSCAN_X64_LOAD3_PL
        "sub rsp, 88\n"
        /* stack: [0]=St [8]=Stt [16]=Sx [24]=Stx [32]=Sy [40]=Sty [48]=path [56]=span [64]=lim [72]=period [80]=n_i0 */
        "test rbx, rbx\n   jz 99f\n   mov qword ptr [rbx + 528], 0\n   mov qword ptr [rbx + 536], 0\n"
        "mov r14d, dword ptr [rbx + 512]\n   cmp r14d, 2\n   jb 99f\n   cmp r14d, 16\n   ja 99f\n"
        "lea rax, [r13*4]\n   cqo\n   mov rcx, 100\n   idiv rcx\n   test rax, rax\n   jle 99f\n   mov [rsp + 64], rax\n"
        "lea ebp, [r14d - 1]\n   mov r15, [rbx + rbp*8 + 256]\n   mov r8, r15\n   sub r8, 50000000\n   mov ecx, ebp\n"
        "1:  test ecx, ecx\n   jz 2f\n   lea edx, [ecx - 1]\n   cmp [rbx + rdx*8 + 256], r8\n   jl 2f\n   mov ecx, edx\n   jmp 1b\n"
        "2:  mov eax, r14d\n   sub eax, ecx\n   cmp eax, 2\n   jl 99f\n"
        "mov rdx, r15\n   sub rdx, [rbx + rcx*8 + 256]\n   cmp rdx, 4000000\n   jl 99f\n   mov [rsp + 56], rdx\n"
        "mov dword ptr [rsp + 80], r14d\n   mov dword ptr [rsp + 84], ecx\n"
        "mov rax, r12\n   cqo\n   mov r8, 1000\n   idiv r8\n   mov [rsp + 72], rax\n"
        "xor eax, eax\n   mov [rsp], rax\n   mov [rsp + 8], rax\n   mov [rsp + 16], rax\n   mov [rsp + 24], rax\n"
        "mov [rsp + 32], rax\n   mov [rsp + 40], rax\n   mov [rsp + 48], rax\n"
        "mov r9d, ecx\n   mov r10d, ecx\n   mov r11, 1000\n"
        "3:  mov rax, [rbx + r9*8 + 256]\n   sub rax, r15\n   cqo\n   idiv r11\n"
        "mov rdi, [rbx + r9*8]\n   mov rsi, [rbx + r9*8 + 128]\n"
        "add [rsp], rax\n   mov rdx, rax\n   imul rdx, rax\n   add [rsp + 8], rdx\n"
        "add [rsp + 16], rdi\n   mov rdx, rax\n   imul rdx, rdi\n   add [rsp + 24], rdx\n"
        "add [rsp + 32], rsi\n   imul rax, rsi\n   add [rsp + 40], rax\n"
        "cmp r9d, r10d\n   je 4f\n"
        "mov rax, rdi\n   sub rax, r12\n   mov rdx, rax\n   neg rdx\n   cmovs rdx, rax\n   add [rsp + 48], rdx\n"
        "mov rax, rsi\n   sub rax, r13\n   mov rdx, rax\n   neg rdx\n   cmovs rdx, rax\n   add [rsp + 48], rdx\n"
        "4:  mov r12, rdi\n   mov r13, rsi\n   inc r9d\n   cmp r9d, r14d\n   jb 3b\n"
        "mov eax, r14d\n   sub eax, r10d\n   movsxd rcx, eax\n"
        "mov r8, [rsp + 8]\n   imul r8, rcx\n   mov rax, [rsp]\n   imul rax, rax\n   sub r8, rax\n   jz 99f\n"
        "mov rax, [rsp + 24]\n   imul rax, rcx\n   mov rdx, [rsp]\n   imul rdx, [rsp + 16]\n   sub rax, rdx\n"
        "imul rax, [rsp + 72]\n   cqo\n   idiv r8\n   mov rdi, rax\n"
        "mov rax, [rsp + 40]\n   imul rax, rcx\n   mov rdx, [rsp]\n   imul rdx, [rsp + 32]\n   sub rax, rdx\n"
        "imul rax, [rsp + 72]\n   cqo\n   idiv r8\n   mov rsi, rax\n"
        "mov rax, r12\n   sub rax, [rbx + r10*8]\n   mov rdx, rax\n   neg rdx\n   cmovs rdx, rax\n   mov r8, rdx\n"
        "mov rax, r13\n   sub rax, [rbx + r10*8 + 128]\n   mov rdx, rax\n   neg rdx\n   cmovs rdx, rax\n   add r8, rdx\n"
        "xor eax, eax\n   mov rcx, [rsp + 48]\n   test rcx, rcx\n   jle 5f\n   mov rax, r8\n   imul rax, rax, 1000\n   cqo\n   idiv rcx\n"
        "5:  sub rax, 600\n   imul rax, rax, 1000\n   cqo\n   mov rcx, 300\n   idiv rcx\n"
        "xor edx, edx\n   test rax, rax\n   cmovs rax, rdx\n   cmp rax, r11\n   cmovg rax, r11\n   mov r9, rax\n"
        "mov rax, r8\n   imul rax, rax, 1000000\n   cqo\n   idiv qword ptr [rsp + 56]\n   sub rax, 40\n   imul rax, rax, 25\n"
        "xor edx, edx\n   test rax, rax\n   cmovs rax, rdx\n   cmp rax, r11\n   cmovg rax, r11\n   mov r10, rax\n"
        "mov rax, rdi\n   imul rax, r9\n   cqo\n   idiv r11\n   imul rax, r10\n   cqo\n   idiv r11\n   mov rdi, rax\n"
        "mov rax, rsi\n   imul rax, r9\n   cqo\n   idiv r11\n   imul rax, r10\n   cqo\n   idiv r11\n   mov rsi, rax\n"
        "lea eax, [r14d - 2]\n   mov r8, r12\n   sub r8, [rbx + rax*8]\n   test r8, r8\n   jz 6f\n"
        "xor r8, rdi\n   jns 6f\n   xor edi, edi\n"
        "6:  mov r8, r13\n   sub r8, [rbx + rax*8 + 128]\n   test r8, r8\n   jz 7f\n"
        "xor r8, rsi\n   jns 7f\n   xor esi, esi\n"
        "7:  mov rax, rdi\n   neg rax\n   test rdi, rdi\n   cmovns rax, rdi\n   add rax, 512\n   shr rax, 10\n"
        "mov rdx, rax\n   neg rdx\n   test rdi, rdi\n   cmovs rax, rdx\n   mov rdi, rax\n"
        "mov rax, rsi\n   neg rax\n   test rsi, rsi\n   cmovns rax, rsi\n   add rax, 512\n   shr rax, 10\n"
        "mov rdx, rax\n   neg rdx\n   test rsi, rsi\n   cmovs rax, rdx\n   mov rsi, rax\n"
        "mov rax, rdi\n   imul rax, rdi\n   mov rdx, rsi\n   imul rdx, rsi\n   add rax, rdx\n"
        "mov r8, [rsp + 64]\n   mov rcx, r8\n   imul rcx, r8\n   cmp rax, rcx\n   jle 9f\n"
        "bsr rcx, rax\n   and rcx, -2\n   mov r9d, 1\n   shl r9, cl\n   xor r10d, r10d\n   mov r11, rax\n"
        "8:  lea rdx, [r10 + r9]\n   shr r10, 1\n   cmp r11, rdx\n   jb 81f\n   sub r11, rdx\n   add r10, r9\n"
        "81: shr r9, 2\n   jnz 8b\n   mov rdx, r10\n   imul rdx, r10\n   cmp rdx, rax\n   adc r10, 0\n"
        "mov rax, rdi\n   imul rax, r8\n   cqo\n   idiv r10\n   mov rdi, rax\n"
        "mov rax, rsi\n   imul rax, r8\n   cqo\n   idiv r10\n   mov rsi, rax\n"
        "9:  shl rdi, 10\n   shl rsi, 10\n   mov [rbx + 528], rdi\n   mov [rbx + 536], rsi\n"
        "99: add rsp, 88\n   " INSCAN_X64_REST_DI_SI "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_predict_lead)

        /* inscan_wheel_stream(dev, hist_n, now) -> 0/1 */
        INSCAN_ASM_FUNC(inscan_wheel_stream)
        INSCAN_X64_LOAD3_WS
        "test r10, r10\n   jz 5f\n   mov rcx, [r10 + 16]\n   xor eax, eax\n"
        "test rcx, rcx\n   jz 1f\n   cmp r9, rcx\n   jb 1f\n   mov rdx, r9\n   sub rdx, rcx\n"
        "cmp rdx, 60000000\n   ja 1f\n   mov eax, 1\n   jmp 2f\n"
        "1:  cmp byte ptr [r10 + 5], 0\n   je 3f\n   mov rcx, [r10 + 24]\n   test rcx, rcx\n   jz 3f\n"
        "cmp r9, rcx\n   jb 3f\n   mov rdx, r9\n   sub rdx, rcx\n   mov r8, 3000000000\n   cmp rdx, r8\n   ja 3f\n   mov eax, 1\n"
        "2:  mov edx, [r10 + 8]\n   inc edx\n   mov [r10 + 8], edx\n"
        "cmp edx, 3\n   setae cl\n   or [r10 + 5], cl\n   mov [r10 + 24], r9\n   jmp 4f\n"
        "3:  mov dword ptr [r10 + 8], 0\n   test r11, r11\n   jz 4f\n   mov dword ptr [r11], 0\n"
        "4:  mov [r10 + 16], r9\n" INSCAN_ASM_RET
        "5:  xor eax, eax\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_wheel_stream)

        /* inscan_stream_gain(rate) -> gain */
        INSCAN_ASM_FUNC(inscan_stream_gain)
#if defined(_WIN32)
        "mov rax, rcx\n"
#else
        "mov rax, rdi\n"
#endif
        "cmp rax, 1000\n   jbe 3f\n   lea rcx, [rax - 1000]\n   mov rdx, 100000\n   cmp rax, rdx\n   cmova rcx, rdx\n"
        "shl rcx, 16\n   mov rax, rcx\n   xor edx, edx\n   mov r8, 3500\n   div r8\n"
        "mov r8, 65536\n   cmp rax, r8\n   cmova rax, r8\n   mov r8, rax\n"
        "shl rax, 16\n   bsr rcx, rax\n   and rcx, -2\n   mov r9d, 1\n   shl r9, cl\n   xor r10d, r10d\n"
        "1:  lea r11, [r10 + r9]\n   shr r10, 1\n   mov rdx, rax\n   sub rdx, r11\n   cmovae rax, rdx\n   lea rcx, [r10 + r9]\n   cmovae r10, rcx\n"
        "shr r9, 2\n   jnz 1b\n"
        "imul r8, r10\n   shr r8, 16\n   imul rax, r8, 6144\n   shr rax, 16\n   add rax, 2048\n" INSCAN_ASM_RET
        "3:  mov eax, 2048\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_gain)

        /* inscan_stream_rate(hist, n) -> rate */
        INSCAN_ASM_FUNC(inscan_stream_rate)
        INSCAN_X64_LOAD2_RATE
        "test r8, r8\n   jz 2f\n   cmp r9d, 2\n   jb 2f\n   mov r10d, 6\n   cmp r9d, 6\n   cmovb r10d, r9d\n"
        "lea eax, [r9d - 1]\n   xor edx, edx\n   mov ecx, 6\n   div ecx\n   mov r11, [r8 + rdx*8]\n"
        "mov eax, r9d\n   sub eax, r10d\n   xor edx, edx\n   div ecx\n   mov rcx, [r8 + rdx*8]\n"
        "cmp r11, rcx\n   jb 2f\n   mov rax, 100000000000\n   je 1f\n"
        "dec r10d\n   sub r11, rcx\n   imul rax, r10\n   xor edx, edx\n   div r11\n"
        "1:  " INSCAN_ASM_RET
        "2:  xor eax, eax\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_rate)

        /* inscan_stream_turn(value120, gain, speed, rest) -> out */
        INSCAN_ASM_FUNC(inscan_stream_turn)
#if defined(_WIN32)
        "mov rax, rcx\n   mov r10, rdx\n   movsxd r11, r8d\n   mov rcx, r9\n"
#else
        "mov rax, rdi\n   mov r10, rsi\n   movsxd r11, edx\n"
#endif
        "test rcx, rcx\n   jz 1f\n   mov r8, -1000000000\n   mov r9, 1000000000\n"
        "cmp rax, r8\n   cmovl rax, r8\n   cmp rax, r9\n   cmovg rax, r9\n"
        "imul r10, r11\n   imul rax, r10\n   add rax, [rcx]\n"
        "mov r8, 1024000\n   cqo\n   idiv r8\n   mov [rcx], rdx\n" INSCAN_ASM_RET
        "1:  xor eax, eax\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_turn)

        ".att_syntax prefix\n"
);

#elif defined(INSCAN_RISCV64)

__asm__(
        INSCAN_ASM_SECTION

        /* inscan_evdev_batch(a0=raw, a1=count, a2=flags, a3=out) -> 0/1/2/3 */
        INSCAN_ASM_FUNC(inscan_evdev_batch)
        "slli a1, a1, 32\n   srli a1, a1, 32\n   beqz a0, 20f\n   beqz a1, 20f\n"
        "slli t0, a1, 1\n   add t0, t0, a1\n   slli t0, t0, 3\n   add a1, a0, t0\n"
        "andi t0, a2, 2\n   bnez t0, 15f\n   beqz a3, 20f\n"
        "addi sp, sp, -48\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n   sd s5, 40(sp)\n"
        "mv t0, a0\n   li a4, 0\n   li a5, 0\n   li a6, 0\n   li a7, 0\n"
        "li s0, -1\n   li s1, 0\n   li s2, 0\n   li s3, 0\n   li s4, 1\n"
        "1:  lhu t1, 16(t0)\n   lhu t2, 18(t0)\n   lw t3, 20(t0)\n   addi t0, t0, 24\n"
        "li t4, 4\n   beq t1, t4, 10f\n   bnez t1, 2f\n   beqz t2, 10f\n   j 21f\n"
        "2:  li s4, 0\n   li t4, 2\n   bne t1, t4, 6f\n   bnez t2, 3f\n   add a4, a4, t3\n   j 10f\n"
        "3:  li t4, 1\n   bne t2, t4, 4f\n   add a5, a5, t3\n   j 10f\n"
        "4:  bnez s2, 21f\n   li t4, 8\n   beq t2, t4, 41f\n   li t4, 6\n   beq t2, t4, 42f\n"
        "li t4, 11\n   beq t2, t4, 43f\n   li t4, 12\n   bne t2, t4, 21f\n   li s3, 1\n   j 44f\n"
        "41: andi t4, a2, 8\n   bnez t4, 10f\n   li s3, 0\n   j 45f\n"
        "42: andi t4, a2, 16\n   bnez t4, 10f\n   li s3, 1\n"
        "45: li t4, 120\n   mulw t3, t3, t4\n   j 44f\n"
        "43: li s3, 0\n"
        "44: andi t4, a2, 1\n   beqz t4, 46f\n   negw t3, t3\n"
        "46: mv s2, t3\n   j 10f\n"
        "6:  li t4, 1\n   bne t1, t4, 21f\n   addi t4, t2, -0x110\n   li t5, 7\n   bgtu t4, t5, 7f\n"
        "bnez a6, 21f\n   mv a6, t2\n   mv a7, t3\n   j 10f\n"
        "7:  li t5, 255\n   bgtu t2, t5, 21f\n   bgez s0, 21f\n   mv s0, t2\n   mv s1, t3\n"
        "10: bltu t0, a1, 1b\n   bnez s4, 18f\n"
        "ld t1, -24(a1)\n   ld t2, -16(a1)\n   li t3, 1000000\n   mul t1, t1, t3\n   add t1, t1, t2\n"
        "or t4, a4, a5\n   or t5, a6, s2\n   or t4, t4, t5\n"
        "bltz s0, 12f\n   bnez t4, 21f\n"
        "sd zero, 0(a3)\n   sd zero, 8(a3)\n   sd t1, 16(a3)\n   sw s0, 24(a3)\n   sw s1, 28(a3)\n"
        "sd zero, 32(a3)\n   sd zero, 40(a3)\n   li a0, 2\n   j 22f\n"
        "12: beqz t4, 21f\n   sd a4, 0(a3)\n   sd a5, 8(a3)\n   sd t1, 16(a3)\n   sw s0, 24(a3)\n   sw zero, 28(a3)\n"
        "sw a6, 32(a3)\n   sw a7, 36(a3)\n   sw s2, 40(a3)\n   sw s3, 44(a3)\n   li a0, 3\n   j 22f\n"
        "18: li a0, 1\n   j 22f\n"
        "21: li a0, 0\n"
        "22: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   ld s4, 32(sp)\n   ld s5, 40(sp)\n   addi sp, sp, 48\n" INSCAN_ASM_RET
        "15: mv t0, a0\n   li t4, 4\n"
        "16: lhu t1, 16(t0)\n   lhu t2, 18(t0)\n   addi t0, t0, 24\n"
        "beq t1, t4, 17f\n   bnez t1, 20f\n   bnez t2, 20f\n"
        "17: bltu t0, a1, 16b\n   li a0, 1\n" INSCAN_ASM_RET
        "20: li a0, 0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_evdev_batch)

        /* inscan_scale_rem(a0=delta, a1=num, a2=den, a3=rem) -> out */
        INSCAN_ASM_FUNC(inscan_scale_rem)
        "beqz a2, 1f\n   beqz a3, 1f\n   ld t0, 0(a3)\n   mul t1, a0, a1\n   add t0, t0, t1\n"
        "div a0, t0, a2\n   mul t1, a0, a2\n   sub t0, t0, t1\n   sd t0, 0(a3)\n" INSCAN_ASM_RET
        "1:  li a0, 0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_scale_rem)

        /* inscan_predict_lead(a0=track, a1=period_ns, a2=scale) */
        INSCAN_ASM_FUNC(inscan_predict_lead)
        "beqz a0, 99f\n   sd zero, 528(a0)\n   sd zero, 536(a0)\n   lwu a3, 512(a0)\n"
        "li t0, 2\n   bltu a3, t0, 99f\n   li t0, 16\n   bgtu a3, t0, 99f\n"
        "sext.w a2, a2\n   slli a2, a2, 2\n   li t0, 100\n   div a2, a2, t0\n   blez a2, 99f\n"
        "addi t1, a3, -1\n   addi a4, a0, 256\n   slli t2, t1, 3\n   add t2, a4, t2\n   ld a5, 0(t2)\n"
        "li t3, 50000000\n   sub t3, a5, t3\n   mv a6, t1\n"
        "1:  beqz a6, 2f\n   addi t4, a6, -1\n   slli t5, t4, 3\n   add t5, a4, t5\n   ld t6, 0(t5)\n"
        "blt t6, t3, 2f\n   mv a6, t4\n   j 1b\n"
        "2:  sub a7, a3, a6\n   li t0, 2\n   blt a7, t0, 99f\n"
        "slli t5, a6, 3\n   add t5, a4, t5\n   ld t6, 0(t5)\n   sub t6, a5, t6\n   li t0, 4000000\n   blt t6, t0, 99f\n"
        "addi sp, sp, -96\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n"
        "sd s4, 32(sp)\n   sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n"
        "sd s8, 64(sp)\n   sd s9, 72(sp)\n   sd s10, 80(sp)\n   sd s11, 88(sp)\n"
        "li s0, 0\n   li s1, 0\n   li s2, 0\n   li s3, 0\n   li s4, 0\n   li s5, 0\n   li s6, 0\n"
        "mv s7, t6\n   mv s8, a2\n   mv t0, a6\n   li s9, 1000\n   addi s10, a0, 128\n"
        "3:  slli t1, t0, 3\n   add t2, a4, t1\n   ld t2, 0(t2)\n   sub t2, t2, a5\n   div t2, t2, s9\n"
        "add t3, a0, t1\n   ld t3, 0(t3)\n   add t4, s10, t1\n   ld t4, 0(t4)\n"
        "add s0, s0, t2\n   mul t5, t2, t2\n   add s1, s1, t5\n"
        "add s2, s2, t3\n   mul t5, t2, t3\n   add s3, s3, t5\n"
        "add s4, s4, t4\n   mul t5, t2, t4\n   add s5, s5, t5\n"
        "beq t0, a6, 4f\n"
        "sub t5, t3, s11\n   bgez t5, 31f\n   neg t5, t5\n"
        "31: add s6, s6, t5\n   sub t5, t4, a2\n   bgez t5, 32f\n   neg t5, t5\n"
        "32: add s6, s6, t5\n"
        "4:  mv s11, t3\n   mv a2, t4\n   addi t0, t0, 1\n   bltu t0, a3, 3b\n"
        "mul t0, a7, s1\n   mul t1, s0, s0\n   sub t0, t0, t1\n   beqz t0, 98f\n"
        "div a1, a1, s9\n   mul t1, a7, s3\n   mul t2, s0, s2\n   sub t1, t1, t2\n   mul t1, t1, a1\n   div t1, t1, t0\n"
        "mul t2, a7, s5\n   mul t3, s0, s4\n   sub t2, t2, t3\n   mul t2, t2, a1\n   div t2, t2, t0\n"
        "slli t3, a6, 3\n   add t4, a0, t3\n   ld t4, 0(t4)\n   sub t4, s11, t4\n   bgez t4, 41f\n   neg t4, t4\n"
        "41: add t5, s10, t3\n   ld t5, 0(t5)\n   sub t5, a2, t5\n   bgez t5, 42f\n   neg t5, t5\n"
        "42: add t4, t4, t5\n   li t5, 0\n   blez s6, 5f\n   mul t5, t4, s9\n   div t5, t5, s6\n"
        "5:  addi t5, t5, -600\n   mul t5, t5, s9\n   li t6, 300\n   div t5, t5, t6\n"
        "bgez t5, 51f\n   li t5, 0\n"
        "51: ble t5, s9, 52f\n   mv t5, s9\n"
        "52: li t6, 1000000\n   mul t6, t4, t6\n   div t6, t6, s7\n   addi t6, t6, -40\n   li t3, 25\n   mul t6, t6, t3\n"
        "bgez t6, 53f\n   li t6, 0\n"
        "53: ble t6, s9, 54f\n   mv t6, s9\n"
        "54: mul t1, t1, t5\n   div t1, t1, s9\n   mul t1, t1, t6\n   div t1, t1, s9\n"
        "mul t2, t2, t5\n   div t2, t2, s9\n   mul t2, t2, t6\n   div t2, t2, s9\n"
        "addi t3, a3, -2\n   slli t3, t3, 3\n   add t4, a0, t3\n   ld t4, 0(t4)\n   sub t4, s11, t4\n"
        "beqz t4, 6f\n   xor t5, t4, t1\n   bgez t5, 6f\n   li t1, 0\n"
        "6:  add t4, s10, t3\n   ld t4, 0(t4)\n   sub t4, a2, t4\n"
        "beqz t4, 7f\n   xor t5, t4, t2\n   bgez t5, 7f\n   li t2, 0\n"
        "7:  bgez t1, 71f\n   neg t1, t1\n   addi t1, t1, 512\n   srli t1, t1, 10\n   neg t1, t1\n   j 72f\n"
        "71: addi t1, t1, 512\n   srli t1, t1, 10\n"
        "72: bgez t2, 73f\n   neg t2, t2\n   addi t2, t2, 512\n   srli t2, t2, 10\n   neg t2, t2\n   j 74f\n"
        "73: addi t2, t2, 512\n   srli t2, t2, 10\n"
        "74: mul t3, t1, t1\n   mul t4, t2, t2\n   add t3, t3, t4\n   mul t4, s8, s8\n   ble t3, t4, 9f\n"
        "li t5, 1\n   slli t5, t5, 62\n"
        "80: bleu t5, t3, 81f\n   srli t5, t5, 2\n   j 80b\n"
        "81: li t6, 0\n   mv a4, t3\n"
        "82: add a5, t6, t5\n   srli t6, t6, 1\n   bltu a4, a5, 83f\n   sub a4, a4, a5\n   add t6, t6, t5\n"
        "83: srli t5, t5, 2\n   bnez t5, 82b\n   mul a5, t6, t6\n   bgeu a5, t3, 84f\n   addi t6, t6, 1\n"
        "84: mul t1, t1, s8\n   div t1, t1, t6\n   mul t2, t2, s8\n   div t2, t2, t6\n"
        "9:  slli t1, t1, 10\n   slli t2, t2, 10\n   sd t1, 528(a0)\n   sd t2, 536(a0)\n"
        "98: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n"
        "ld s4, 32(sp)\n   ld s5, 40(sp)\n   ld s6, 48(sp)\n   ld s7, 56(sp)\n"
        "ld s8, 64(sp)\n   ld s9, 72(sp)\n   ld s10, 80(sp)\n   ld s11, 88(sp)\n   addi sp, sp, 96\n"
        "99: " INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_predict_lead)

        /* inscan_wheel_stream(a0=dev, a1=hist_n, a2=now) -> 0/1 */
        INSCAN_ASM_FUNC(inscan_wheel_stream)
        "beqz a0, 5f\n   ld a3, 16(a0)\n   ld a4, 24(a0)\n   li a5, 0\n"
        "beqz a3, 1f\n   bltu a2, a3, 1f\n   sub t0, a2, a3\n   li t1, 60000000\n   bgtu t0, t1, 1f\n   li a5, 1\n   j 2f\n"
        "1:  lbu t0, 5(a0)\n   beqz t0, 3f\n   beqz a4, 3f\n   bltu a2, a4, 3f\n"
        "sub t0, a2, a4\n   li t1, 3000000000\n   bgtu t0, t1, 3f\n   li a5, 1\n"
        "2:  lwu t0, 8(a0)\n   addi t0, t0, 1\n   sw t0, 8(a0)\n"
        "li t1, 3\n   sltu t2, t0, t1\n   xori t2, t2, 1\n   lbu t3, 5(a0)\n   or t3, t3, t2\n   sb t3, 5(a0)\n   sd a2, 24(a0)\n   j 4f\n"
        "3:  sw zero, 8(a0)\n   beqz a1, 4f\n   sw zero, 0(a1)\n"
        "4:  sd a2, 16(a0)\n   mv a0, a5\n" INSCAN_ASM_RET
        "5:  li a0, 0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_wheel_stream)

        /* inscan_stream_gain(a0=rate) -> gain */
        INSCAN_ASM_FUNC(inscan_stream_gain)
        "li t0, 1000\n   bleu a0, t0, 3f\n   li t1, 100000\n   sub t2, a0, t0\n   bleu a0, t1, 4f\n   mv t2, t1\n"
        "4:  slli t2, t2, 16\n   li t3, 3500\n   divu t2, t2, t3\n   li t3, 65536\n   bleu t2, t3, 5f\n   mv t2, t3\n"
        "5:  slli t4, t2, 16\n   li t5, 1\n   slli t5, t5, 32\n   li t6, 0\n"
        "1:  add a1, t6, t5\n   srli t6, t6, 1\n   bltu t4, a1, 2f\n   sub t4, t4, a1\n   add t6, t6, t5\n"
        "2:  srli t5, t5, 2\n   bnez t5, 1b\n"
        "mul t2, t2, t6\n   srli t2, t2, 16\n   li t3, 6144\n   mul t2, t2, t3\n   srli t2, t2, 16\n   li t3, 2048\n   add a0, t2, t3\n" INSCAN_ASM_RET
        "3:  li a0, 2048\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_gain)

        /* inscan_stream_rate(a0=hist, a1=n) -> rate */
        INSCAN_ASM_FUNC(inscan_stream_rate)
        "slli a1, a1, 32\n   srli a1, a1, 32\n   beqz a0, 2f\n   li t0, 2\n   bltu a1, t0, 2f\n"
        "li t1, 6\n   mv t2, a1\n   bltu a1, t1, 0f\n   mv t2, t1\n"
        "0:  addi t3, a1, -1\n   remu t3, t3, t1\n   slli t3, t3, 3\n   add t3, a0, t3\n   ld t4, 0(t3)\n"
        "sub t3, a1, t2\n   remu t3, t3, t1\n   slli t3, t3, 3\n   add t3, a0, t3\n   ld t5, 0(t3)\n"
        "bltu t4, t5, 2f\n   li t6, 100000000000\n   beq t4, t5, 1f\n"
        "addi t2, t2, -1\n   sub t4, t4, t5\n   mul t6, t2, t6\n   divu a0, t6, t4\n" INSCAN_ASM_RET
        "1:  mv a0, t6\n" INSCAN_ASM_RET
        "2:  li a0, 0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_rate)

        /* inscan_stream_turn(a0=value120, a1=gain, a2=speed, a3=rest) -> out */
        INSCAN_ASM_FUNC(inscan_stream_turn)
        "beqz a3, 1f\n   li t0, 1000000000\n   neg t1, t0\n"
        "bge a0, t1, 2f\n   mv a0, t1\n"
        "2:  ble a0, t0, 3f\n   mv a0, t0\n"
        "3:  sext.w a2, a2\n   mul a1, a1, a2\n   ld t0, 0(a3)\n   mul t1, a0, a1\n   add t0, t0, t1\n"
        "li t2, 1024000\n   div a0, t0, t2\n   mul t1, a0, t2\n   sub t0, t0, t1\n   sd t0, 0(a3)\n" INSCAN_ASM_RET
        "1:  li a0, 0\n" INSCAN_ASM_RET
        INSCAN_ASM_END(inscan_stream_turn)
);

#endif /* INSCAN_ARM64 / INSCAN_X64 / INSCAN_RISCV64 */

#endif /* COMPOSITOR_INPUT_SCAN_ASM_IMPLEMENTATION && !COMPOSITOR_INPUT_SCAN_ASM_EMITTED */
