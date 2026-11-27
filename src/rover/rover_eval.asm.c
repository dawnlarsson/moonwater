/*
        Moonwater Rover hot IRQ-off rule evaluation primitives in pure assembly:
          - rover_eval_glob          : bounded O(N*M) wildcard matcher (* and ?), single-pass head/tail
          - rover_eval_compare_num   : signed 64-bit numeric ops (TRUE, EQ..GE, wrap-around BETWEEN)
          - rover_eval_may           : once & signed-ns rate-limit gate with drop counter
          - rover_eval_key_next      : kernel-safe scalar key-or-ANY scanner
          - rover_eval_holds_numbers : number-form rule evaluator (AND/OR/LINE short-circuit + trigger)
          - rover_walk_numbers       : leaf event-rule walker for number-form spans
          - rover_eval_edges_numbers : leaf when-rule edge evaluator for number-form spans

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, zero FP/SIMD registers, no red-zone
        reliance — 100% Linux kernel IRQ-off safe.

        STB-style: include for declarations; define ROVER_EVAL_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_ROVER_EVAL_ASM_H
#define MOONWATER_ROVER_EVAL_ASM_H

#if defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
    defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  rover_u8;
typedef __UINT16_TYPE__ rover_u16;
typedef __UINT32_TYPE__ rover_u32;
typedef __INT32_TYPE__  rover_i32;
typedef __UINT64_TYPE__ rover_u64;
typedef __INT64_TYPE__  rover_i64;
#else
#include <stdint.h>
typedef uint8_t  rover_u8;
typedef uint16_t rover_u16;
typedef uint32_t rover_u32;
typedef int32_t  rover_i32;
typedef uint64_t rover_u64;
typedef int64_t  rover_i64;
#endif

#define ROVER_EVAL_EVENTS          128u
#define ROVER_EVAL_FIELDS          4u
#define ROVER_EVAL_KEY_ANY         0xffffffffu
#define ROVER_EVAL_SPAN_COUNT      0x7fffu
#define ROVER_EVAL_SPAN_NUMBERS    0x8000u
#define ROVER_EVAL_WALK_STOPPED    (1ull << 63)

#define ROVER_EVAL_RULE_WHEN       1u
#define ROVER_EVAL_RULE_ONCE       2u
#define ROVER_EVAL_RULE_OFF        4u
#define ROVER_EVAL_RULE_NUMBERS    16u

#define ROVER_EVAL_TRUE            0u
#define ROVER_EVAL_EQ              1u
#define ROVER_EVAL_NE              2u
#define ROVER_EVAL_LT              3u
#define ROVER_EVAL_LE              4u
#define ROVER_EVAL_GT              5u
#define ROVER_EVAL_GE              6u
#define ROVER_EVAL_MATCH           7u
#define ROVER_EVAL_NOMATCH         8u
#define ROVER_EVAL_BETWEEN         9u

#define ROVER_EVAL_JOIN_OR         1u
#define ROVER_EVAL_JOIN_LINE       2u
#define ROVER_EVAL_FORM_NUMBER     1u

#define ROVER_EVAL_CONST_NUMBER    1u
#define ROVER_EVAL_STATE_NUMBER    3u
#define ROVER_EVAL_FIELD           5u

struct rover_eval_operand {
        rover_u8  source;
        rover_u8  page;
        rover_u16 index;
};

struct rover_eval_span {
        rover_u16 first;
        rover_u16 count;
};

struct rover_eval_comparison {
        rover_u8  op;
        rover_u8  type;
        rover_u8  join;
        rover_u8  form;
        struct rover_eval_operand a, b, c;
};

struct rover_eval_rule {
        rover_u16 event;
        rover_u8  place;
        rover_u8  flags;
        rover_u16 comparison;
        rover_u16 comparisons;
        rover_u8  trigger;
        rover_u8  then;
        rover_u8  otherwise;
        rover_u8  pad;
        rover_u16 action;
        rover_u16 pad2;
        rover_u32 limit_ms;
        rover_u32 name;
        rover_u32 about;
        rover_u32 pad3;
        rover_u64 reads;
};

struct rover_eval_value {
        rover_i64       number;
        const rover_u8 *text;
        rover_u32       length;
        rover_u32       pad;
};

struct rover_eval_event {
        rover_u16 id;
        rover_u8  count;
        rover_u8  depth;
        rover_u32 pad;
        rover_u64 when;
        rover_i64 realtime;
        struct rover_eval_value field[ROVER_EVAL_FIELDS];
};

struct rover_eval_run {
        rover_u64 last;
        rover_u32 fires;
        rover_u32 dropped;
        rover_u8  edge;
        rover_u8  done;
        rover_u8  pad[6];
};

struct rover_eval_hit {
        rover_u16 rule;
        rover_u16 action;
        rover_u8  count;
        rover_u8  otherwise;
        rover_u16 pad;
};

struct rover_eval_table {
        const void                   *head;       /* +0  */
        const struct rover_eval_rule *rule;       /* +8  */
        const rover_u16              *order;      /* +16 */
        const rover_u32              *key;        /* +24 */
        const struct rover_eval_span *index;      /* +32 */
        const struct rover_eval_comparison *comparison; /* +40 */
        const void                   *call;       /* +48 */
        const void                   *action;     /* +56 */
        const void                   *argument;   /* +64 */
        const void                   *note;       /* +72 */
        const rover_u8               *pool;       /* +80 */
        const void                   *service;    /* +88 */
};

#ifdef offset_of
#define ROVER_EVAL_OFF(T, M) offset_of(T, M)
#else
#define ROVER_EVAL_OFF(T, M) ((rover_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct rover_eval_operand) == 4, "rover_eval_operand size");
_Static_assert(sizeof(struct rover_eval_span) == 4, "rover_eval_span size");
_Static_assert(sizeof(struct rover_eval_comparison) == 16 &&
               ROVER_EVAL_OFF(struct rover_eval_comparison, join) == 2 &&
               ROVER_EVAL_OFF(struct rover_eval_comparison, a) == 4 &&
               ROVER_EVAL_OFF(struct rover_eval_comparison, b) == 8, "rover_eval_comparison layout");
_Static_assert(sizeof(struct rover_eval_rule) == 40 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, place) == 2 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, flags) == 3 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, comparison) == 4 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, comparisons) == 6 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, trigger) == 8 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, then) == 9 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, otherwise) == 10 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, action) == 12 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, limit_ms) == 16 &&
               ROVER_EVAL_OFF(struct rover_eval_rule, reads) == 32, "rover_eval_rule layout");
_Static_assert(sizeof(struct rover_eval_value) == 24 &&
               ROVER_EVAL_OFF(struct rover_eval_event, count) == 2 &&
               ROVER_EVAL_OFF(struct rover_eval_event, when) == 8 &&
               ROVER_EVAL_OFF(struct rover_eval_event, field) == 24, "rover_eval_event layout");
_Static_assert(sizeof(struct rover_eval_run) == 24 &&
               ROVER_EVAL_OFF(struct rover_eval_run, fires) == 8 &&
               ROVER_EVAL_OFF(struct rover_eval_run, dropped) == 12 &&
               ROVER_EVAL_OFF(struct rover_eval_run, edge) == 16 &&
               ROVER_EVAL_OFF(struct rover_eval_run, done) == 17, "rover_eval_run layout");
_Static_assert(sizeof(struct rover_eval_hit) == 8 &&
               ROVER_EVAL_OFF(struct rover_eval_hit, action) == 2 &&
               ROVER_EVAL_OFF(struct rover_eval_hit, count) == 4 &&
               ROVER_EVAL_OFF(struct rover_eval_hit, otherwise) == 5, "rover_eval_hit layout");
_Static_assert(ROVER_EVAL_OFF(struct rover_eval_table, rule) == 8 &&
               ROVER_EVAL_OFF(struct rover_eval_table, order) == 16 &&
               ROVER_EVAL_OFF(struct rover_eval_table, key) == 24 &&
               ROVER_EVAL_OFF(struct rover_eval_table, index) == 32 &&
               ROVER_EVAL_OFF(struct rover_eval_table, comparison) == 40 &&
               ROVER_EVAL_OFF(struct rover_eval_table, pool) == 80, "rover_eval_table layout");

#if defined(ROVER_INCLUDED)
_Static_assert(sizeof(struct rover_eval_table) == sizeof(struct rover_table) &&
               sizeof(struct rover_eval_rule) == sizeof(struct rover_rule) &&
               sizeof(struct rover_eval_comparison) == sizeof(struct rover_comparison) &&
               sizeof(struct rover_eval_event) == sizeof(struct rover_event) &&
               sizeof(struct rover_eval_run) == sizeof(struct rover_run) &&
               sizeof(struct rover_eval_hit) == sizeof(struct rover_hit) &&
               sizeof(struct rover_eval_span) == sizeof(struct rover_span), "rover_eval struct size parity");
_Static_assert(ROVER_EVAL_OFF(struct rover_rule, reads) == 32 &&
               ROVER_EVAL_OFF(struct rover_run, edge) == 16 &&
               ROVER_EVAL_OFF(struct rover_comparison, a) == 4 &&
               ROVER_EVAL_OFF(struct rover_comparison, b) == 8, "rover_eval field offset parity");
_Static_assert(ROVER_EVAL_TRUE == ROVER_TRUE && ROVER_EVAL_EQ == ROVER_EQ &&
               ROVER_EVAL_GE == ROVER_GE && ROVER_EVAL_BETWEEN == ROVER_BETWEEN &&
               ROVER_EVAL_RULE_WHEN == ROVER_RULE_WHEN && ROVER_EVAL_RULE_ONCE == ROVER_RULE_ONCE &&
               ROVER_EVAL_RULE_OFF == ROVER_RULE_OFF && ROVER_EVAL_RULE_NUMBERS == ROVER_RULE_NUMBERS &&
               ROVER_EVAL_CONST_NUMBER == ROVER_CONST_NUMBER && ROVER_EVAL_STATE_NUMBER == ROVER_STATE_NUMBER &&
               ROVER_EVAL_FIELD == ROVER_FIELD && ROVER_EVAL_JOIN_OR == ROVER_JOIN_OR &&
               ROVER_EVAL_JOIN_LINE == ROVER_JOIN_LINE && ROVER_EVAL_KEY_ANY == ROVER_KEY_ANY,
               "rover_eval constant parity");
#endif

rover_u32 rover_eval_glob(const rover_u8 *text, rover_u32 length, const rover_u8 *pattern, rover_u32 size);
rover_u32 rover_eval_compare_num(rover_i64 a, rover_i64 b, rover_i64 c, rover_u32 op);
rover_u32 rover_eval_may(const struct rover_eval_rule *rule, struct rover_eval_run *run, rover_u64 when);
rover_u64 rover_eval_key_next(const rover_u32 *keys, rover_u64 at, rover_u64 count, rover_u32 want);
rover_i32 rover_eval_holds_numbers(const struct rover_eval_table *table, const struct rover_eval_rule *rule,
                                   const struct rover_eval_event *event, const rover_u8 *const *pages);
rover_u64 rover_walk_numbers(const struct rover_eval_table *table, struct rover_eval_run *runs,
                             const rover_u8 *const *pages, const struct rover_eval_event *event,
                             rover_u64 place_room, struct rover_eval_hit *hits);
rover_u64 rover_eval_edges_numbers(const struct rover_eval_table *table, struct rover_eval_run *runs,
                                   const rover_u8 *const *pages, rover_u64 changed,
                                   const struct rover_eval_event *event, rover_u64 place_room,
                                   struct rover_eval_hit *hits);

#endif /* MOONWATER_ROVER_EVAL_ASM_H */

#ifdef ROVER_EVAL_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define ROVER_ASM_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define ROVER_ASM_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define ROVER_ASM_RISCV64 1
# else
#  error "src/rover/rover_eval.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define ROVER_ASM_ARM64 1
# elif defined(X64) && X64
#  define ROVER_ASM_X64 1
# elif defined(RISCV64) && RISCV64
#  define ROVER_ASM_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define RV_ASM_SECTION   ASM_SECTION
# define RV_ASM_FUNC(n)   ASM_FUNC(n)
# define RV_ASM_END(n)    ASM_END(n)
# define RV_ASM_RET       ASM_RET
#else
# define RV_ASM_SECTION   ".text\n"
# define RV_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define RV_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define RV_ASM_END(n)   ""
# elif defined(_WIN32)
#  define RV_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define RV_ASM_END(n)   ""
# else
#  if defined(ROVER_ASM_ARM64)
#   define RV_ASM_FTYPE   "%function"
#  else
#   define RV_ASM_FTYPE   "@function"
#  endif
#  define RV_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " RV_ASM_FTYPE "\n" #n ":\n"
#  define RV_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(ROVER_ASM_ARM64)

#define RV_A64_SIDE(reg_cmp, at, into, p0, p1, pool, ev, next)                                                    \
        "ldrb w0, [" reg_cmp ", #" #at "]\n   ldrh w2, [" reg_cmp ", #" #at "+2]\n   cmp w0, #3\n   b.ne 61f\n"   \
        "ldrb w0, [" reg_cmp ", #" #at "+1]\n   cmp w0, #0\n   csel x0, " p0 ", " p1 ", eq\n"                     \
        "mov " into ", #0\n   cbz x0, " next "\n   ldrsw " into ", [x0, x2]\n   b " next "\n"                     \
        "61: cmp w0, #1\n   b.ne 62f\n   ldr " into ", [" pool ", x2]\n   b " next "\n"                           \
        "62: mov " into ", #0\n   ldrb w0, [" ev ", #2]\n   cmp w2, w0\n   b.hs " next "\n"                       \
        "mov x0, #24\n   mul x2, x2, x0\n   add x2, x2, " ev "\n   ldr " into ", [x2, #24]\n"

__asm__(
        RV_ASM_SECTION

        /* rover_eval_glob(x0=text, w1=length, x2=pattern, w3=size) -> w0 (0/1) */
        RV_ASM_FUNC(rover_eval_glob)
        "mov w1, w1\n   mov w3, w3\n   cbz w3, 10f\n   cbz x2, 11f\n   cbz w1, 1f\n   cbz x0, 11f\n"
        "1:  mov x4, #0\n   mov w13, #63\n"
        "2:  cmp x4, x3\n   b.hs 12f\n   ldrb w5, [x2, x4]\n   cmp w5, #42\n   ccmp w5, w13, #4, ne\n   b.eq 3f\n"
        "cmp x4, x1\n   b.hs 11f\n   ldrb w6, [x0, x4]\n   cmp w5, w6\n   b.ne 11f\n   add x4, x4, #1\n   b 2b\n"
        "3:  mov x7, x1\n   mov x8, x3\n"
        "4:  cmp x8, x4\n   b.ls 5f\n   sub x9, x8, #1\n   ldrb w5, [x2, x9]\n   cmp w5, #42\n   ccmp w5, w13, #4, ne\n"
        "b.eq 5f\n   cmp x7, x4\n   b.ls 11f\n   sub x7, x7, #1\n   ldrb w6, [x0, x7]\n"
        "cmp w5, w6\n   b.ne 11f\n   mov x8, x9\n   b 4b\n"
        "5:  add x9, x4, #1\n   cmp x8, x9\n   b.ne 6f\n   ldrb w5, [x2, x4]\n   cmp w5, #42\n   b.eq 13f\n"
        "6:  mov x9, x4\n   mov x10, x4\n   mov x11, #0\n   mov x12, #0\n"
        "7:  cmp x9, x7\n   b.hs 9f\n   cmp x10, x8\n   b.hs 8f\n   ldrb w5, [x2, x10]\n   cmp w5, #42\n   b.ne 14f\n"
        "add x10, x10, #1\n   mov x11, x10\n   mov x12, x9\n   b 7b\n"
        "14: cmp w5, #63\n   b.eq 15f\n   ldrb w6, [x0, x9]\n   cmp w5, w6\n   b.ne 8f\n"
        "15: add x9, x9, #1\n   add x10, x10, #1\n   b 7b\n"
        "8:  cbz x11, 11f\n   mov x10, x11\n   add x12, x12, #1\n   mov x9, x12\n   b 7b\n"
        "9:  cmp x10, x8\n   b.hs 13f\n   ldrb w5, [x2, x10]\n   cmp w5, #42\n   b.ne 11f\n   add x10, x10, #1\n   b 9b\n"
        "10: cmp w1, #0\n   cset w0, eq\n" RV_ASM_RET
        "12: cmp x4, x1\n   cset w0, eq\n" RV_ASM_RET
        "11: mov w0, #0\n" RV_ASM_RET
        "13: mov w0, #1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_glob)

        /* rover_eval_compare_num(x0=a, x1=b, x2=c, w3=op) -> w0 (0/1) */
        RV_ASM_FUNC(rover_eval_compare_num)
        "cbz w3, 1f\n   cmp w3, #6\n   b.hi 2f\n   cmp x0, x1\n   cset w4, gt\n   csinv w4, w4, wzr, ge\n"
        "add w4, w4, #1\n   add w4, w4, w3, lsl #2\n   mov w5, #0x1520\n   movk w5, #0x0643, lsl #16\n"
        "lsr w5, w5, w4\n   and w0, w5, #1\n" RV_ASM_RET
        "1:  cmp x0, #0\n   cset w0, ne\n" RV_ASM_RET
        "2:  cmp w3, #9\n   b.ne 4f\n   cmp x1, x2\n   b.gt 3f\n   cmp x0, x1\n   b.lt 4f\n   cmp x0, x2\n   cset w0, le\n" RV_ASM_RET
        "3:  cmp x0, x1\n   b.ge 5f\n   cmp x0, x2\n   cset w0, le\n" RV_ASM_RET
        "4:  mov w0, #0\n" RV_ASM_RET
        "5:  mov w0, #1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_compare_num)

        /* rover_eval_may(x0=rule, x1=run, x2=when) -> w0 (0/1) */
        RV_ASM_FUNC(rover_eval_may)
        "ldrb w3, [x0, #3]\n   tbz w3, #1, 1f\n   ldrb w3, [x1, #17]\n   cbnz w3, 3f\n"
        "1:  ldr w3, [x0, #16]\n   cbz w3, 2f\n   ldr w4, [x1, #8]\n   cbz w4, 2f\n"
        "mov x4, #16960\n   movk x4, #15, lsl #16\n   mul x3, x3, x4\n   ldr x4, [x1]\n   sub x2, x2, x4\n"
        "cmp x2, x3\n   b.ge 2f\n   ldr w4, [x1, #12]\n   add w4, w4, #1\n   str w4, [x1, #12]\n"
        "3:  mov w0, #0\n" RV_ASM_RET
        "2:  mov w0, #1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_may)

        /* rover_eval_key_next(x0=keys, x1=at, x2=count, w3=want) -> x0 */
        RV_ASM_FUNC(rover_eval_key_next)
        "cmp x1, x2\n   b.hs 2f\n"
        "1:  ldr w4, [x0, x1, lsl #2]\n   cmp w4, w3\n   ccmn w4, #1, #4, ne\n   b.eq 2f\n"
        "add x1, x1, #1\n   cmp x1, x2\n   b.lo 1b\n"
        "2:  mov x0, x1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_key_next)

        /* rover_eval_holds_numbers(x0=table, x1=rule, x2=event, x3=pages) -> w0 (-1/0/1) */
        RV_ASM_FUNC(rover_eval_holds_numbers)
        "mov x4, x2\n   ldp x6, x7, [x3]\n   ldr x8, [x0, #80]\n   ldr x9, [x0, #40]\n"
        "ldrh w10, [x1, #4]\n   add x10, x9, x10, lsl #4\n   mov x15, x10\n"
        "ldrh w11, [x1, #6]\n   add x11, x10, x11, lsl #4\n   mov w12, #1\n"
        "1:  cmp x10, x11\n   b.hs 6f\n   cmp w12, #1\n   b.ne 2f\n"
        RV_A64_SIDE("x10", 4, "x13", "x6", "x7", "x8", "x4", "63f")
        "63:\n"
        RV_A64_SIDE("x10", 8, "x14", "x6", "x7", "x8", "x4", "64f")
        "64: cmp x13, x14\n   cset w0, gt\n   csinv w0, w0, wzr, ge\n   add w0, w0, #1\n   ldrb w2, [x10]\n"
        "add w0, w0, w2, lsl #2\n   mov w2, #0x1520\n   movk w2, #0x0643, lsl #16\n   lsr w2, w2, w0\n"
        "and w2, w2, #1\n   and w12, w12, #2\n   orr w12, w12, w2\n"
        "2:  ldrb w0, [x10, #2]\n   tst w0, #3\n   b.eq 3f\n   orr w12, w12, w12, lsl #1\n   and w12, w12, #2\n"
        "orr w12, w12, #1\n   tbz w0, #1, 3f\n   tbz w12, #1, 4f\n   mov w12, #1\n"
        "3:  add x10, x10, #16\n   b 1b\n"
        "4:  ldrb w0, [x1, #3]\n   tbnz w0, #0, 5f\n   sub x0, x10, x15\n   lsr x0, x0, #4\n"
        "ldrb w2, [x1, #8]\n   cmp x0, x2\n   b.hs 5f\n   mov w0, #-1\n" RV_ASM_RET
        "5:  mov w0, #0\n" RV_ASM_RET
        "6:  mov w0, #1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_holds_numbers)

        /* rover_walk_numbers(x0=table, x1=runs, x2=pages, x3=event, x4=place_room, x5=hits) -> x0 */
        RV_ASM_FUNC(rover_walk_numbers)
        "lsr x17, x4, #32\n   cbz x17, 98f\n   ldrh w8, [x3]\n   cmp w8, #128\n   b.hs 98f\n"
        "stp x19, x20, [sp, #-48]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
        "ldp x6, x7, [x2]\n   mov w2, w8\n   ldr x8, [x0, #80]\n   ldr x9, [x0, #8]\n   ldr x10, [x0, #40]\n"
        "ldr x24, [x0, #32]\n   add x24, x24, x2, lsl #2\n   ldrh w2, [x24]\n   ldrh w13, [x24, #2]\n"
        "and w13, w13, #0x7fff\n   ldr x11, [x0, #16]\n   add x11, x11, x2, lsl #1\n   ldr x12, [x0, #24]\n   add x12, x12, x2, lsl #2\n"
        "mov w16, #-1\n   ldrb w2, [x3, #2]\n   cbz w2, 1f\n   ldr x2, [x3, #24]\n   tbnz x2, #63, 1f\n"
        "mov x24, #0xffffffff\n   cmp x2, x24\n   b.hs 1f\n   mov w16, w2\n"
        "1:  and w4, w4, #0xff\n   mov x14, #0\n   mov x15, #0\n"
        "10: cmp x14, x13\n   b.hs 90f\n   ldr w2, [x12, x14, lsl #2]\n   cmp w2, w16\n   b.eq 20f\n   cmn w2, #1\n   b.eq 20f\n"
        "add x14, x14, #1\n   b 10b\n"
        "20: ldrh w0, [x11, x14, lsl #1]\n   mov x24, #40\n   madd x19, x0, x24, x9\n"
        "ldrb w2, [x19, #2]\n   cmp w2, w4\n   b.ne 80f\n   ldrb w2, [x19, #3]\n   tbnz w2, #2, 80f\n   tbz w2, #4, 95f\n"
        "ldrh w20, [x19, #4]\n   add x20, x10, x20, lsl #4\n   ldrh w21, [x19, #6]\n   add x21, x20, x21, lsl #4\n   mov w22, #1\n"
        "30: cmp x20, x21\n   b.hs 50f\n   cmp w22, #1\n   b.ne 38f\n"
        RV_A64_SIDE("x20", 4, "x23", "x6", "x7", "x8", "x3", "63f")
        "63:\n"
        RV_A64_SIDE("x20", 8, "x24", "x6", "x7", "x8", "x3", "64f")
        "64: cmp x23, x24\n   cset w0, gt\n   csinv w0, w0, wzr, ge\n   add w0, w0, #1\n   ldrb w2, [x20]\n   add w0, w0, w2, lsl #2\n"
        "mov w2, #0x1520\n   movk w2, #0x0643, lsl #16\n   lsr w2, w2, w0\n   and w2, w2, #1\n   and w22, w22, #2\n   orr w22, w22, w2\n"
        "38: ldrb w0, [x20, #2]\n   tst w0, #3\n   b.eq 39f\n   orr w22, w22, w22, lsl #1\n   and w22, w22, #2\n   orr w22, w22, #1\n"
        "tbz w0, #1, 39f\n   tbz w22, #1, 45f\n   mov w22, #1\n"
        "39: add x20, x20, #16\n   b 30b\n"
        "45: ldrb w0, [x19, #3]\n   tbnz w0, #0, 47f\n   ldrh w0, [x19, #4]\n   add x0, x10, x0, lsl #4\n   sub x0, x20, x0\n"
        "lsr x0, x0, #4\n   ldrb w2, [x19, #8]\n   cmp x0, x2\n   b.lo 80f\n"
        "47: ldrb w21, [x19, #10]\n   cbz w21, 80f\n   ldrh w22, [x19, #12]\n   ldrb w0, [x19, #9]\n   add w22, w22, w0\n"
        "mov w23, #1\n   b 55f\n"
        "50: ldrb w21, [x19, #9]\n   ldrh w22, [x19, #12]\n   mov w23, #0\n"
        "55: ldrh w0, [x11, x14, lsl #1]\n   mov x24, #24\n   madd x24, x0, x24, x1\n"
        "ldrb w2, [x19, #3]\n   tbz w2, #1, 56f\n   ldrb w2, [x24, #17]\n   cbnz w2, 80f\n"
        "56: ldr w2, [x19, #16]\n   cbz w2, 57f\n   ldr w0, [x24, #8]\n   cbz w0, 57f\n   mov x0, #16960\n   movk x0, #15, lsl #16\n"
        "mul x2, x2, x0\n   ldr x0, [x3, #8]\n   ldr x20, [x24]\n   sub x0, x0, x20\n   cmp x0, x2\n   b.ge 57f\n"
        "ldr w0, [x24, #12]\n   add w0, w0, #1\n   str w0, [x24, #12]\n   b 80f\n"
        "57: ldrh w0, [x11, x14, lsl #1]\n   orr x0, x0, x22, lsl #16\n   orr x0, x0, x21, lsl #32\n   orr x0, x0, x23, lsl #40\n"
        "str x0, [x5, x15, lsl #3]\n   ldr w0, [x24, #8]\n   add w0, w0, #1\n   str w0, [x24, #8]\n   ldr x0, [x3, #8]\n   str x0, [x24]\n"
        "ldrb w0, [x19, #3]\n   tbz w0, #1, 58f\n   mov w0, #1\n   strb w0, [x24, #17]\n"
        "58: add x15, x15, #1\n   cmp x15, x17\n   b.hs 90f\n"
        "80: add x14, x14, #1\n   b 10b\n"
        "90: mov x0, x15\n   b 99f\n"
        "95: lsl x0, x14, #32\n   orr x0, x0, x15\n   orr x0, x0, #0x8000000000000000\n"
        "99: ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #48\n" RV_ASM_RET
        "98: mov x0, #0\n" RV_ASM_RET
        RV_ASM_END(rover_walk_numbers)

        /* rover_eval_edges_numbers(x0=table, x1=runs, x2=pages, x3=changed, x4=event, x5=place_room, x6=hits) -> x0 */
        RV_ASM_FUNC(rover_eval_edges_numbers)
        "stp x19, x20, [sp, #-48]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
        "ldp x7, x8, [x2]\n   ldr x9, [x0, #80]\n   ldr x10, [x0, #8]\n   ldr x11, [x0, #40]\n"
        "ldr x24, [x0, #32]\n   ldrh w2, [x24]\n   ldrh w13, [x24, #2]\n   and w13, w13, #0x7fff\n"
        "ldr x12, [x0, #16]\n   add x12, x12, x2, lsl #1\n   lsr x16, x5, #32\n   and w17, w5, #0xff\n"
        "mov x14, #0\n   mov x15, #0\n"
        "10: cmp x14, x13\n   b.hs 90f\n   ldrh w0, [x12, x14, lsl #1]\n   mov x24, #40\n   madd x19, x0, x24, x10\n"
        "ldrb w2, [x19, #2]\n   cmp w2, w17\n   b.ne 80f\n   ldrb w2, [x19, #3]\n   tbnz w2, #2, 80f\n"
        "ldr x5, [x19, #32]\n   tst x5, x3\n   b.eq 80f\n   tbz w2, #4, 95f\n"
        "ldrh w20, [x19, #4]\n   add x20, x11, x20, lsl #4\n   ldrh w21, [x19, #6]\n   add x21, x20, x21, lsl #4\n   mov w22, #1\n"
        "30: cmp x20, x21\n   b.hs 46f\n   cmp w22, #1\n   b.ne 38f\n"
        RV_A64_SIDE("x20", 4, "x23", "x7", "x8", "x9", "x4", "63f")
        "63:\n"
        RV_A64_SIDE("x20", 8, "x24", "x7", "x8", "x9", "x4", "64f")
        "64: cmp x23, x24\n   cset w0, gt\n   csinv w0, w0, wzr, ge\n   add w0, w0, #1\n   ldrb w2, [x20]\n   add w0, w0, w2, lsl #2\n"
        "mov w2, #0x1520\n   movk w2, #0x0643, lsl #16\n   lsr w2, w2, w0\n   and w2, w2, #1\n   and w22, w22, #2\n   orr w22, w22, w2\n"
        "38: ldrb w0, [x20, #2]\n   tst w0, #3\n   b.eq 39f\n   orr w22, w22, w22, lsl #1\n   and w22, w22, #2\n   orr w22, w22, #1\n"
        "tbz w0, #1, 39f\n   tbz w22, #1, 45f\n   mov w22, #1\n"
        "39: add x20, x20, #16\n   b 30b\n"
        "45: mov w23, #0\n   b 48f\n"
        "46: mov w23, #1\n"
        "48: ldrh w0, [x12, x14, lsl #1]\n   mov x24, #24\n   madd x24, x0, x24, x1\n"
        "ldrb w2, [x24, #16]\n   cmp w2, w23\n   b.eq 80f\n   ldrb w21, [x19, #10]\n"
        "cmp x15, x16\n   b.ne 49f\n   orr w2, w23, w21\n   cbnz w2, 80f\n"
        "49: strb w23, [x24, #16]\n   cbnz w23, 50f\n   cbz w21, 80f\n   ldrh w22, [x19, #12]\n"
        "ldrb w2, [x19, #9]\n   add w22, w22, w2\n   mov w23, #1\n   b 55f\n"
        "50: ldrb w21, [x19, #9]\n   ldrh w22, [x19, #12]\n   mov w23, #0\n"
        "55: ldrb w2, [x19, #3]\n   tbz w2, #1, 56f\n   ldrb w2, [x24, #17]\n   cbnz w2, 80f\n"
        "56: ldr w2, [x19, #16]\n   cbz w2, 57f\n   ldr w5, [x24, #8]\n   cbz w5, 57f\n   mov x5, #16960\n   movk x5, #15, lsl #16\n"
        "mul x2, x2, x5\n   ldr x5, [x4, #8]\n   ldr x20, [x24]\n   sub x5, x5, x20\n   cmp x5, x2\n   b.ge 57f\n"
        "ldr w5, [x24, #12]\n   add w5, w5, #1\n   str w5, [x24, #12]\n   b 80f\n"
        "57: orr x0, x0, x22, lsl #16\n   orr x0, x0, x21, lsl #32\n   orr x0, x0, x23, lsl #40\n"
        "str x0, [x6, x15, lsl #3]\n   ldr w5, [x24, #8]\n   add w5, w5, #1\n   str w5, [x24, #8]\n   ldr x5, [x4, #8]\n   str x5, [x24]\n"
        "ldrb w5, [x19, #3]\n   tbz w5, #1, 58f\n   mov w5, #1\n   strb w5, [x24, #17]\n"
        "58: add x15, x15, #1\n"
        "80: add x14, x14, #1\n   b 10b\n"
        "90: mov x0, x15\n   b 99f\n"
        "95: lsl x0, x14, #32\n   orr x0, x0, x15\n   orr x0, x0, #0x8000000000000000\n"
        "99: ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #48\n" RV_ASM_RET
        RV_ASM_END(rover_eval_edges_numbers)
);

#undef RV_A64_SIDE

#elif defined(ROVER_ASM_X64)

#if defined(_WIN32)
# define RV_X64_A1 rcx
# define RV_X64_A2 rdx
# define RV_X64_A3 r8
# define RV_X64_A4 r9
# define RV_X64_PUSH_W "push rdi\n   push rsi\n"
# define RV_X64_POP_W  "pop rsi\n   pop rdi\n"
# define RV_X64_WALK_LOAD                                                                                         \
        "mov [rsp + 40], rcx\n   mov [rsp + 48], rdx\n   mov r10, r8\n   mov r15, r9\n"                           \
        "mov rax, [rsp + 168]\n   mov [rsp + 32], rax\n   mov rax, [rsp + 176]\n   mov [rsp + 56], rax\n"
# define RV_X64_EDGE_LOAD                                                                                         \
        "mov [rsp + 40], rcx\n   mov [rsp + 48], rdx\n   mov r10, r8\n   mov r14, r9\n"                           \
        "mov r15, [rsp + 168]\n   mov rax, [rsp + 176]\n   mov [rsp + 32], rax\n"                                 \
        "mov rax, [rsp + 184]\n   mov [rsp + 56], rax\n"
#else
# define RV_X64_A1 rdi
# define RV_X64_A2 rsi
# define RV_X64_A3 rdx
# define RV_X64_A4 rcx
# define RV_X64_PUSH_W ""
# define RV_X64_POP_W  ""
# define RV_X64_WALK_LOAD                                                                                         \
        "mov [rsp + 40], rdi\n   mov [rsp + 48], rsi\n   mov r10, rdx\n   mov r15, rcx\n"                         \
        "mov [rsp + 32], r8\n   mov [rsp + 56], r9\n"
# define RV_X64_EDGE_LOAD                                                                                         \
        "mov [rsp + 40], rdi\n   mov [rsp + 48], rsi\n   mov r10, rdx\n   mov r14, rcx\n"                         \
        "mov r15, r8\n   mov [rsp + 32], r9\n   mov rax, [rsp + 120]\n   mov [rsp + 56], rax\n"
#endif

#define RV_X64_SIDE(at, next)                                                                                     \
        "movzx ecx, byte ptr [r8 + " #at "]\n   movzx edx, word ptr [r8 + " #at " + 2]\n   cmp ecx, 3\n   jne 61f\n" \
        "mov rax, [rsp]\n   cmp byte ptr [r8 + " #at " + 1], 0\n   cmovne rax, [rsp + 8]\n"                       \
        "test rax, rax\n   jz 65f\n   movsxd rax, dword ptr [rax + rdx]\n   jmp " next "\n"                       \
        "65: xor eax, eax\n   jmp " next "\n"                                                                     \
        "61: cmp ecx, 1\n   jne 62f\n   mov rax, [rsp + 16]\n   mov rax, [rax + rdx]\n   jmp " next "\n"          \
        "62: xor eax, eax\n   movzx ecx, byte ptr [r15 + 2]\n   cmp edx, ecx\n   jae " next "\n"                  \
        "lea rdx, [rdx + rdx * 2]\n   mov rax, [r15 + rdx * 8 + 24]\n"

__asm__(
        RV_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* rover_eval_glob(text, length, pattern, size) -> eax (0/1) */
        RV_ASM_FUNC(rover_eval_glob)
        "push rbx\n   push r12\n   push r13\n"
#if defined(_WIN32)
        "mov r10, rcx\n   mov r11d, edx\n   mov r12, r8\n   mov r13d, r9d\n"
#else
        "mov r10, rdi\n   mov r11d, esi\n   mov r12, rdx\n   mov r13d, ecx\n"
#endif
        "test r13d, r13d\n   jz 10f\n   test r12, r12\n   jz 11f\n   test r11d, r11d\n   jz 1f\n   test r10, r10\n   jz 11f\n"
        "1:  xor ecx, ecx\n"
        "2:  cmp rcx, r13\n   jae 12f\n   movzx eax, byte ptr [r12 + rcx]\n   cmp eax, 42\n   je 3f\n   cmp eax, 63\n   je 3f\n"
        "cmp rcx, r11\n   jae 11f\n   cmp al, byte ptr [r10 + rcx]\n   jne 11f\n   inc rcx\n   jmp 2b\n"
        "3:  mov rdx, r11\n   mov r8, r13\n"
        "4:  cmp r8, rcx\n   jbe 5f\n   movzx eax, byte ptr [r12 + r8 - 1]\n   cmp eax, 42\n   je 5f\n"
        "cmp eax, 63\n   je 5f\n   cmp rdx, rcx\n   jbe 11f\n   dec rdx\n   cmp al, byte ptr [r10 + rdx]\n   jne 11f\n"
        "dec r8\n   jmp 4b\n"
        "5:  lea rax, [rcx + 1]\n   cmp r8, rax\n   jne 6f\n   cmp byte ptr [r12 + rcx], 42\n   je 13f\n"
        "6:  mov r9, rcx\n   xor ebx, ebx\n   xor r11d, r11d\n"
        "7:  cmp r9, rdx\n   jae 9f\n   cmp rcx, r8\n   jae 8f\n   movzx eax, byte ptr [r12 + rcx]\n   cmp eax, 42\n   jne 14f\n"
        "inc rcx\n   mov rbx, rcx\n   mov r11, r9\n   jmp 7b\n"
        "14: cmp eax, 63\n   je 15f\n   cmp al, byte ptr [r10 + r9]\n   jne 8f\n"
        "15: inc r9\n   inc rcx\n   jmp 7b\n"
        "8:  test rbx, rbx\n   jz 11f\n   mov rcx, rbx\n   inc r11\n   mov r9, r11\n   jmp 7b\n"
        "9:  cmp rcx, r8\n   jae 13f\n   cmp byte ptr [r12 + rcx], 42\n   jne 11f\n   inc rcx\n   jmp 9b\n"
        "10: xor eax, eax\n   test r11d, r11d\n   sete al\n   jmp 16f\n"
        "12: xor eax, eax\n   cmp rcx, r11\n   sete al\n   jmp 16f\n"
        "11: xor eax, eax\n   jmp 16f\n"
        "13: mov eax, 1\n"
        "16: pop r13\n   pop r12\n   pop rbx\n" RV_ASM_RET
        RV_ASM_END(rover_eval_glob)

        /* rover_eval_compare_num(a, b, c, op) -> eax (0/1) */
        RV_ASM_FUNC(rover_eval_compare_num)
#if defined(_WIN32)
        "mov r10, rcx\n   mov r11, rdx\n   mov rax, r8\n   mov edx, r9d\n"
#else
        "mov r10, rdi\n   mov r11, rsi\n   mov rax, rdx\n   mov edx, ecx\n"
#endif
        "test edx, edx\n   jz 1f\n   cmp edx, 6\n   ja 2f\n   xor ecx, ecx\n   xor r8d, r8d\n"
        "cmp r10, r11\n   setg cl\n   setl r8b\n   sub ecx, r8d\n   lea ecx, [rcx + rdx * 4 + 1]\n"
        "mov eax, 0x06431520\n   shr eax, cl\n   and eax, 1\n" RV_ASM_RET
        "1:  xor eax, eax\n   test r10, r10\n   setne al\n" RV_ASM_RET
        "2:  cmp edx, 9\n   jne 4f\n   cmp r11, rax\n   jg 3f\n   cmp r10, r11\n   jl 4f\n"
        "cmp r10, rax\n   setle al\n   movzx eax, al\n" RV_ASM_RET
        "3:  cmp r10, r11\n   jge 5f\n   cmp r10, rax\n   setle al\n   movzx eax, al\n" RV_ASM_RET
        "4:  xor eax, eax\n" RV_ASM_RET
        "5:  mov eax, 1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_compare_num)

        /* rover_eval_may(rule, run, when) -> eax (0/1) */
        RV_ASM_FUNC(rover_eval_may)
#if defined(_WIN32)
        "mov r10, rcx\n   mov r11, rdx\n   mov rcx, r8\n"
#else
        "mov r10, rdi\n   mov r11, rsi\n   mov rcx, rdx\n"
#endif
        "test byte ptr [r10 + 3], 2\n   jz 1f\n   cmp byte ptr [r11 + 17], 0\n   jne 3f\n"
        "1:  mov eax, dword ptr [r10 + 16]\n   test eax, eax\n   jz 2f\n   cmp dword ptr [r11 + 8], 0\n   je 2f\n"
        "imul rax, rax, 1000000\n   sub rcx, [r11]\n   cmp rcx, rax\n   jge 2f\n   inc dword ptr [r11 + 12]\n"
        "3:  xor eax, eax\n" RV_ASM_RET
        "2:  mov eax, 1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_may)

        /* rover_eval_key_next(keys, at, count, want) -> rax */
        RV_ASM_FUNC(rover_eval_key_next)
#if defined(_WIN32)
        "mov r10, rcx\n   mov rax, rdx\n   mov r11, r8\n   mov ecx, r9d\n"
#else
        "mov r10, rdi\n   mov rax, rsi\n   mov r11, rdx\n"
#endif
        "1:  cmp rax, r11\n   jae 2f\n   mov edx, dword ptr [r10 + rax * 4]\n   cmp edx, ecx\n   je 2f\n"
        "cmp edx, -1\n   je 2f\n   inc rax\n   jmp 1b\n"
        "2:  " RV_ASM_RET
        RV_ASM_END(rover_eval_key_next)

        /* rover_eval_holds_numbers(table, rule, event, pages) -> eax (-1/0/1) */
        RV_ASM_FUNC(rover_eval_holds_numbers)
        "push rbx\n   push r12\n   push r13\n   push r15\n   sub rsp, 24\n"
#if defined(_WIN32)
        "mov r12, rcx\n   mov r13, rdx\n   mov r15, r8\n   mov r10, r9\n"
#else
        "mov r12, rdi\n   mov r13, rsi\n   mov r15, rdx\n   mov r10, rcx\n"
#endif
        "mov rax, [r10]\n   mov [rsp], rax\n   mov rax, [r10 + 8]\n   mov [rsp + 8], rax\n"
        "mov rax, [r12 + 80]\n   mov [rsp + 16], rax\n"
        "movzx r8d, word ptr [r13 + 4]\n   shl r8, 4\n   add r8, [r12 + 40]\n   mov rbx, r8\n"
        "movzx r11d, word ptr [r13 + 6]\n   shl r11, 4\n   add r11, r8\n   mov r10d, 1\n"
        "1:  cmp r8, r11\n   jae 6f\n   cmp r10d, 1\n   jne 2f\n"
        RV_X64_SIDE(4, "63f")
        "63: mov r9, rax\n"
        RV_X64_SIDE(8, "64f")
        "64: xor ecx, ecx\n   xor edx, edx\n   cmp r9, rax\n   setg cl\n   setl dl\n   sub ecx, edx\n"
        "movzx edx, byte ptr [r8]\n   lea ecx, [rcx + rdx * 4 + 1]\n   mov eax, 0x06431520\n   shr eax, cl\n"
        "and eax, 1\n   and r10d, 2\n   or r10d, eax\n"
        "2:  movzx ecx, byte ptr [r8 + 2]\n   test cl, 3\n   jz 3f\n   lea eax, [r10 + r10]\n   or r10d, eax\n"
        "and r10d, 2\n   or r10d, 1\n   test cl, 2\n   jz 3f\n   test r10d, 2\n   jz 4f\n   mov r10d, 1\n"
        "3:  add r8, 16\n   jmp 1b\n"
        "4:  test byte ptr [r13 + 3], 1\n   jnz 5f\n   sub r8, rbx\n   shr r8, 4\n   movzx eax, byte ptr [r13 + 8]\n"
        "cmp r8, rax\n   jae 5f\n   mov eax, -1\n   jmp 7f\n"
        "5:  xor eax, eax\n   jmp 7f\n"
        "6:  mov eax, 1\n"
        "7:  add rsp, 24\n   pop r15\n   pop r13\n   pop r12\n   pop rbx\n" RV_ASM_RET
        RV_ASM_END(rover_eval_holds_numbers)

        /* rover_walk_numbers(table, runs, pages, event, place_room, hits) -> rax */
        RV_ASM_FUNC(rover_walk_numbers)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n" RV_X64_PUSH_W "sub rsp, 64\n"
        RV_X64_WALK_LOAD
        "mov rax, [rsp + 32]\n   shr rax, 32\n   test rax, rax\n   jz 98f\n"
        "movzx eax, word ptr [r15]\n   cmp eax, 128\n   jae 98f\n"
        "mov rcx, [r10]\n   mov [rsp], rcx\n   mov rcx, [r10 + 8]\n   mov [rsp + 8], rcx\n"
        "mov rdi, [rsp + 40]\n   mov rcx, [rdi + 80]\n   mov [rsp + 16], rcx\n"
        "mov rdx, [rdi + 32]\n   movzx ecx, word ptr [rdx + rax * 4]\n   movzx r12d, word ptr [rdx + rax * 4 + 2]\n"
        "and r12d, 0x7fff\n   mov r13, [rdi + 16]\n   lea r13, [r13 + rcx * 2]\n   mov r14, [rdi + 24]\n   lea r14, [r14 + rcx * 4]\n"
        "mov eax, -1\n   cmp byte ptr [r15 + 2], 0\n   je 1f\n   mov rdx, [r15 + 24]\n   test rdx, rdx\n   js 1f\n"
        "cmp rdx, rax\n   jae 1f\n   mov eax, edx\n"
        "1:  mov dword ptr [rsp + 24], eax\n   xor ebx, ebx\n   xor ebp, ebp\n"
        "10: cmp rbx, r12\n   jae 90f\n   mov eax, dword ptr [r14 + rbx * 4]\n   cmp eax, dword ptr [rsp + 24]\n   je 20f\n"
        "cmp eax, -1\n   je 20f\n   inc rbx\n   jmp 10b\n"
        "20: movzx eax, word ptr [r13 + rbx * 2]\n   lea rdi, [rax + rax * 4]\n   mov rdx, [rsp + 40]\n   shl rdi, 3\n   add rdi, [rdx + 8]\n"
        "movzx ecx, byte ptr [rdi + 2]\n   cmp cl, byte ptr [rsp + 32]\n   jne 80f\n"
        "movzx ecx, byte ptr [rdi + 3]\n   test cl, 4\n   jnz 80f\n   test cl, 16\n   jz 95f\n"
        "movzx r8d, word ptr [rdi + 4]\n   shl r8, 4\n   add r8, [rdx + 40]\n   movzx r11d, word ptr [rdi + 6]\n   shl r11, 4\n   add r11, r8\n"
        "mov r10d, 1\n"
        "30: cmp r8, r11\n   jae 50f\n   cmp r10d, 1\n   jne 38f\n"
        RV_X64_SIDE(4, "63f")
        "63: mov rsi, rax\n"
        RV_X64_SIDE(8, "64f")
        "64: xor ecx, ecx\n   xor edx, edx\n   cmp rsi, rax\n   setg cl\n   setl dl\n   sub ecx, edx\n"
        "movzx edx, byte ptr [r8]\n   lea ecx, [rcx + rdx * 4 + 1]\n   mov eax, 0x06431520\n   shr eax, cl\n   and eax, 1\n"
        "and r10d, 2\n   or r10d, eax\n"
        "38: movzx ecx, byte ptr [r8 + 2]\n   test cl, 3\n   jz 39f\n   lea eax, [r10 + r10]\n   or r10d, eax\n   and r10d, 2\n"
        "or r10d, 1\n   test cl, 2\n   jz 39f\n   test r10d, 2\n   jz 45f\n   mov r10d, 1\n"
        "39: add r8, 16\n   jmp 30b\n"
        "45: test byte ptr [rdi + 3], 1\n   jnz 47f\n   mov rdx, [rsp + 40]\n   movzx eax, word ptr [rdi + 4]\n   shl rax, 4\n   add rax, [rdx + 40]\n"
        "sub r8, rax\n   shr r8, 4\n   movzx eax, byte ptr [rdi + 8]\n   cmp r8, rax\n   jb 80f\n"
        "47: movzx ecx, byte ptr [rdi + 10]\n   test ecx, ecx\n   jz 80f\n   movzx edx, word ptr [rdi + 12]\n   movzx eax, byte ptr [rdi + 9]\n"
        "add edx, eax\n   mov r8d, 1\n   jmp 55f\n"
        "50: movzx ecx, byte ptr [rdi + 9]\n   movzx edx, word ptr [rdi + 12]\n   xor r8d, r8d\n"
        "55: movzx eax, word ptr [r13 + rbx * 2]\n   lea r10, [rax + rax * 2]\n   shl r10, 3\n   add r10, [rsp + 48]\n"
        "test byte ptr [rdi + 3], 2\n   jz 56f\n   cmp byte ptr [r10 + 17], 0\n   jne 80f\n"
        "56: mov esi, dword ptr [rdi + 16]\n   test esi, esi\n   jz 57f\n   cmp dword ptr [r10 + 8], 0\n   je 57f\n"
        "imul rsi, rsi, 1000000\n   mov r11, [r15 + 8]\n   sub r11, [r10]\n   cmp r11, rsi\n   jge 57f\n"
        "inc dword ptr [r10 + 12]\n   jmp 80f\n"
        "57: shl rdx, 16\n   or rax, rdx\n   shl rcx, 32\n   or rax, rcx\n   shl r8, 40\n   or rax, r8\n"
        "mov r9, [rsp + 56]\n   mov [r9 + rbp * 8], rax\n   inc dword ptr [r10 + 8]\n   mov rax, [r15 + 8]\n   mov [r10], rax\n"
        "test byte ptr [rdi + 3], 2\n   jz 58f\n   mov byte ptr [r10 + 17], 1\n"
        "58: inc rbp\n   mov rax, [rsp + 32]\n   shr rax, 32\n   cmp rbp, rax\n   jae 90f\n"
        "80: inc rbx\n   jmp 10b\n"
        "90: mov rax, rbp\n   jmp 99f\n"
        "95: mov rax, rbx\n   shl rax, 32\n   or rax, rbp\n   bts rax, 63\n   jmp 99f\n"
        "98: xor eax, eax\n"
        "99: add rsp, 64\n" RV_X64_POP_W "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" RV_ASM_RET
        RV_ASM_END(rover_walk_numbers)

        /* rover_eval_edges_numbers(table, runs, pages, changed, event, place_room, hits) -> rax */
        RV_ASM_FUNC(rover_eval_edges_numbers)
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n" RV_X64_PUSH_W "sub rsp, 64\n"
        RV_X64_EDGE_LOAD
        "mov rcx, [r10]\n   mov [rsp], rcx\n   mov rcx, [r10 + 8]\n   mov [rsp + 8], rcx\n"
        "mov rdi, [rsp + 40]\n   mov rcx, [rdi + 80]\n   mov [rsp + 16], rcx\n"
        "mov rdx, [rdi + 32]\n   movzx ecx, word ptr [rdx]\n   movzx r12d, word ptr [rdx + 2]\n   and r12d, 0x7fff\n"
        "mov r13, [rdi + 16]\n   lea r13, [r13 + rcx * 2]\n   xor ebx, ebx\n   xor ebp, ebp\n"
        "10: cmp rbx, r12\n   jae 90f\n   movzx eax, word ptr [r13 + rbx * 2]\n   lea rdi, [rax + rax * 4]\n"
        "mov rdx, [rsp + 40]\n   shl rdi, 3\n   add rdi, [rdx + 8]\n   movzx ecx, byte ptr [rdi + 2]\n"
        "cmp cl, byte ptr [rsp + 32]\n   jne 80f\n   movzx ecx, byte ptr [rdi + 3]\n   test cl, 4\n   jnz 80f\n"
        "test [rdi + 32], r14\n   jz 80f\n   test cl, 16\n   jz 95f\n"
        "movzx r8d, word ptr [rdi + 4]\n   shl r8, 4\n   add r8, [rdx + 40]\n   movzx r11d, word ptr [rdi + 6]\n   shl r11, 4\n   add r11, r8\n"
        "mov r10d, 1\n"
        "30: cmp r8, r11\n   jae 46f\n   cmp r10d, 1\n   jne 38f\n"
        RV_X64_SIDE(4, "63f")
        "63: mov rsi, rax\n"
        RV_X64_SIDE(8, "64f")
        "64: xor ecx, ecx\n   xor edx, edx\n   cmp rsi, rax\n   setg cl\n   setl dl\n   sub ecx, edx\n"
        "movzx edx, byte ptr [r8]\n   lea ecx, [rcx + rdx * 4 + 1]\n   mov eax, 0x06431520\n   shr eax, cl\n   and eax, 1\n"
        "and r10d, 2\n   or r10d, eax\n"
        "38: movzx ecx, byte ptr [r8 + 2]\n   test cl, 3\n   jz 39f\n   lea eax, [r10 + r10]\n   or r10d, eax\n   and r10d, 2\n"
        "or r10d, 1\n   test cl, 2\n   jz 39f\n   test r10d, 2\n   jz 45f\n   mov r10d, 1\n"
        "39: add r8, 16\n   jmp 30b\n"
        "45: xor r8d, r8d\n   jmp 48f\n"
        "46: mov r8d, 1\n"
        "48: movzx eax, word ptr [r13 + rbx * 2]\n   lea r10, [rax + rax * 2]\n   shl r10, 3\n   add r10, [rsp + 48]\n"
        "movzx ecx, byte ptr [r10 + 16]\n   cmp ecx, r8d\n   je 80f\n   movzx ecx, byte ptr [rdi + 10]\n"
        "mov rdx, [rsp + 32]\n   shr rdx, 32\n   cmp rbp, rdx\n   jne 49f\n   mov edx, r8d\n   or edx, ecx\n   jnz 80f\n"
        "49: mov byte ptr [r10 + 16], r8b\n   test r8d, r8d\n   jnz 50f\n   test ecx, ecx\n   jz 80f\n"
        "movzx edx, word ptr [rdi + 12]\n   movzx esi, byte ptr [rdi + 9]\n   add edx, esi\n   mov r8d, 1\n   jmp 55f\n"
        "50: movzx ecx, byte ptr [rdi + 9]\n   movzx edx, word ptr [rdi + 12]\n   xor r8d, r8d\n"
        "55: test byte ptr [rdi + 3], 2\n   jz 56f\n   cmp byte ptr [r10 + 17], 0\n   jne 80f\n"
        "56: mov esi, dword ptr [rdi + 16]\n   test esi, esi\n   jz 57f\n   cmp dword ptr [r10 + 8], 0\n   je 57f\n"
        "imul rsi, rsi, 1000000\n   mov r11, [r15 + 8]\n   sub r11, [r10]\n   cmp r11, rsi\n   jge 57f\n"
        "inc dword ptr [r10 + 12]\n   jmp 80f\n"
        "57: shl rdx, 16\n   or rax, rdx\n   shl rcx, 32\n   or rax, rcx\n   shl r8, 40\n   or rax, r8\n"
        "mov r9, [rsp + 56]\n   mov [r9 + rbp * 8], rax\n   inc dword ptr [r10 + 8]\n   mov rax, [r15 + 8]\n   mov [r10], rax\n"
        "test byte ptr [rdi + 3], 2\n   jz 58f\n   mov byte ptr [r10 + 17], 1\n"
        "58: inc rbp\n"
        "80: inc rbx\n   jmp 10b\n"
        "90: mov rax, rbp\n   jmp 99f\n"
        "95: mov rax, rbx\n   shl rax, 32\n   or rax, rbp\n   bts rax, 63\n"
        "99: add rsp, 64\n" RV_X64_POP_W "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" RV_ASM_RET
        RV_ASM_END(rover_eval_edges_numbers)

        ".att_syntax prefix\n"
);

#undef RV_X64_SIDE
#undef RV_X64_WALK_LOAD
#undef RV_X64_EDGE_LOAD
#undef RV_X64_PUSH_W
#undef RV_X64_POP_W

#elif defined(ROVER_ASM_RISCV64)

#define RV_R64_SIDE(reg_cmp, at, into, p0, p1, pool, ev, next)                                                    \
        "lbu t0, " #at "(" reg_cmp ")\n   lhu t1, " #at "+2(" reg_cmp ")\n   li t2, 3\n   bne t0, t2, 61f\n"     \
        "lbu t0, " #at "+1(" reg_cmp ")\n   mv t2, " p0 "\n   beqz t0, 65f\n   mv t2, " p1 "\n"                   \
        "65: li " into ", 0\n   beqz t2, " next "\n   add t2, t2, t1\n   lw " into ", 0(t2)\n   j " next "\n"     \
        "61: li t2, 1\n   bne t0, t2, 62f\n   add t2, " pool ", t1\n   ld " into ", 0(t2)\n   j " next "\n"       \
        "62: li " into ", 0\n   lbu t0, 2(" ev ")\n   bgeu t1, t0, " next "\n"                                    \
        "slli t2, t1, 1\n   add t1, t1, t2\n   slli t1, t1, 3\n   add t1, " ev ", t1\n   ld " into ", 24(t1)\n"

__asm__(
        RV_ASM_SECTION

        /* rover_eval_glob(a0=text, a1=length, a2=pattern, a3=size) -> a0 (0/1) */
        RV_ASM_FUNC(rover_eval_glob)
        "slli a1, a1, 32\n   srli a1, a1, 32\n   slli a3, a3, 32\n   srli a3, a3, 32\n"
        "beqz a3, 10f\n   beqz a2, 11f\n   beqz a1, 1f\n   beqz a0, 11f\n"
        "1:  li a4, 0\n   li t3, 42\n   li t4, 63\n"
        "2:  bgeu a4, a3, 12f\n   add t5, a2, a4\n   lbu t0, 0(t5)\n   beq t0, t3, 3f\n   beq t0, t4, 3f\n"
        "bgeu a4, a1, 11f\n   add t5, a0, a4\n   lbu t1, 0(t5)\n   bne t0, t1, 11f\n   addi a4, a4, 1\n   j 2b\n"
        "3:  mv a5, a1\n   mv a6, a3\n"
        "4:  bleu a6, a4, 5f\n   addi t2, a6, -1\n   add t5, a2, t2\n   lbu t0, 0(t5)\n   beq t0, t3, 5f\n"
        "beq t0, t4, 5f\n   bleu a5, a4, 11f\n   addi a5, a5, -1\n   add t5, a0, a5\n   lbu t1, 0(t5)\n"
        "bne t0, t1, 11f\n   mv a6, t2\n   j 4b\n"
        "5:  addi t2, a4, 1\n   bne a6, t2, 6f\n   add t5, a2, a4\n   lbu t0, 0(t5)\n   beq t0, t3, 13f\n"
        "6:  mv a7, a4\n   li t6, 0\n   li t2, 0\n"
        "7:  bgeu a7, a5, 9f\n   bgeu a4, a6, 8f\n   add t5, a2, a4\n   lbu t0, 0(t5)\n   bne t0, t3, 14f\n"
        "addi a4, a4, 1\n   mv t6, a4\n   mv t2, a7\n   j 7b\n"
        "14: beq t0, t4, 15f\n   add t5, a0, a7\n   lbu t1, 0(t5)\n   bne t0, t1, 8f\n"
        "15: addi a7, a7, 1\n   addi a4, a4, 1\n   j 7b\n"
        "8:  beqz t6, 11f\n   mv a4, t6\n   addi t2, t2, 1\n   mv a7, t2\n   j 7b\n"
        "9:  bgeu a4, a6, 13f\n   add t5, a2, a4\n   lbu t0, 0(t5)\n   bne t0, t3, 11f\n   addi a4, a4, 1\n   j 9b\n"
        "10: seqz a0, a1\n" RV_ASM_RET
        "12: sub t0, a4, a1\n   seqz a0, t0\n" RV_ASM_RET
        "11: li a0, 0\n" RV_ASM_RET
        "13: li a0, 1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_glob)

        /* rover_eval_compare_num(a0=a, a1=b, a2=c, a3=op) -> a0 (0/1) */
        RV_ASM_FUNC(rover_eval_compare_num)
        "slli a3, a3, 32\n   srli a3, a3, 32\n   beqz a3, 1f\n   li t0, 6\n   bgtu a3, t0, 2f\n"
        "slt t1, a1, a0\n   slt t2, a0, a1\n   sub t1, t1, t2\n   addi t1, t1, 1\n   slli t3, a3, 2\n"
        "add t1, t1, t3\n   li t0, 0x06431520\n   srl t0, t0, t1\n   andi a0, t0, 1\n" RV_ASM_RET
        "1:  snez a0, a0\n" RV_ASM_RET
        "2:  li t0, 9\n   bne a3, t0, 4f\n   bgt a1, a2, 3f\n   blt a0, a1, 4f\n   slt t0, a2, a0\n   xori a0, t0, 1\n" RV_ASM_RET
        "3:  bge a0, a1, 5f\n   slt t0, a2, a0\n   xori a0, t0, 1\n" RV_ASM_RET
        "4:  li a0, 0\n" RV_ASM_RET
        "5:  li a0, 1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_compare_num)

        /* rover_eval_may(a0=rule, a1=run, a2=when) -> a0 (0/1) */
        RV_ASM_FUNC(rover_eval_may)
        "lbu t0, 3(a0)\n   andi t0, t0, 2\n   beqz t0, 1f\n   lbu t0, 17(a1)\n   bnez t0, 3f\n"
        "1:  lwu t0, 16(a0)\n   beqz t0, 2f\n   lwu t1, 8(a1)\n   beqz t1, 2f\n   li t1, 1000000\n"
        "mul t0, t0, t1\n   ld t1, 0(a1)\n   sub t2, a2, t1\n   bge t2, t0, 2f\n"
        "lwu t1, 12(a1)\n   addi t1, t1, 1\n   sw t1, 12(a1)\n"
        "3:  li a0, 0\n" RV_ASM_RET
        "2:  li a0, 1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_may)

        /* rover_eval_key_next(a0=keys, a1=at, a2=count, a3=want) -> a0 */
        RV_ASM_FUNC(rover_eval_key_next)
        "slli a3, a3, 32\n   srli a3, a3, 32\n   li t2, 0xffffffff\n"
        "1:  bgeu a1, a2, 2f\n   slli t0, a1, 2\n   add t0, a0, t0\n   lwu t1, 0(t0)\n"
        "beq t1, a3, 2f\n   beq t1, t2, 2f\n   addi a1, a1, 1\n   j 1b\n"
        "2:  mv a0, a1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_key_next)

        /* rover_eval_holds_numbers(a0=table, a1=rule, a2=event, a3=pages) -> a0 (-1/0/1) */
        RV_ASM_FUNC(rover_eval_holds_numbers)
        "ld a4, 0(a3)\n   ld a5, 8(a3)\n   ld a6, 80(a0)\n   ld t5, 40(a0)\n"
        "lhu a7, 4(a1)\n   slli a7, a7, 4\n   add a7, t5, a7\n   mv t6, a7\n"
        "lhu t5, 6(a1)\n   slli t5, t5, 4\n   add t5, a7, t5\n   li a3, 1\n"
        "1:  bgeu a7, t5, 6f\n   li t0, 1\n   bne a3, t0, 2f\n"
        RV_R64_SIDE("a7", 4, "t3", "a4", "a5", "a6", "a2", "63f")
        "63:\n"
        RV_R64_SIDE("a7", 8, "t4", "a4", "a5", "a6", "a2", "64f")
        "64: slt t0, t4, t3\n   slt t1, t3, t4\n   sub t0, t0, t1\n   addi t0, t0, 1\n"
        "lbu t1, 0(a7)\n   slli t1, t1, 2\n   add t0, t0, t1\n   li t1, 0x06431520\n"
        "srl t1, t1, t0\n   andi t1, t1, 1\n   andi a3, a3, 2\n   or a3, a3, t1\n"
        "2:  lbu t0, 2(a7)\n   andi t1, t0, 3\n   beqz t1, 3f\n   slli t1, a3, 1\n   or a3, a3, t1\n"
        "andi a3, a3, 2\n   ori a3, a3, 1\n   andi t1, t0, 2\n   beqz t1, 3f\n   andi t1, a3, 2\n   beqz t1, 4f\n   li a3, 1\n"
        "3:  addi a7, a7, 16\n   j 1b\n"
        "4:  lbu t0, 3(a1)\n   andi t0, t0, 1\n   bnez t0, 5f\n   sub t0, a7, t6\n   srli t0, t0, 4\n"
        "lbu t1, 8(a1)\n   bgeu t0, t1, 5f\n   li a0, -1\n" RV_ASM_RET
        "5:  li a0, 0\n" RV_ASM_RET
        "6:  li a0, 1\n" RV_ASM_RET
        RV_ASM_END(rover_eval_holds_numbers)

        /* rover_walk_numbers(a0=table, a1=runs, a2=pages, a3=event, a4=place_room, a5=hits) -> a0 */
        RV_ASM_FUNC(rover_walk_numbers)
        "srli a7, a4, 32\n   beqz a7, 98f\n   lhu t0, 0(a3)\n   li t1, 128\n   bgeu t0, t1, 98f\n"
        "addi sp, sp, -96\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n"
        "sd s5, 40(sp)\n   sd s6, 48(sp)\n   sd s7, 56(sp)\n   sd s8, 64(sp)\n   sd s9, 72(sp)\n   sd s10, 80(sp)\n"
        "ld s0, 0(a2)\n   ld s1, 8(a2)\n   ld s2, 80(a0)\n   ld s3, 8(a0)\n   ld s4, 40(a0)\n"
        "ld t1, 32(a0)\n   slli t0, t0, 2\n   add t1, t1, t0\n   lhu t2, 0(t1)\n   lhu s7, 2(t1)\n"
        "li t3, 0x7fff\n   and s7, s7, t3\n   ld s5, 16(a0)\n   slli t0, t2, 1\n   add s5, s5, t0\n"
        "ld s6, 24(a0)\n   slli t0, t2, 2\n   add s6, s6, t0\n"
        "li a6, 0xffffffff\n   lbu t0, 2(a3)\n   beqz t0, 1f\n   ld t0, 24(a3)\n   bltz t0, 1f\n"
        "bgeu t0, a6, 1f\n   mv a6, t0\n"
        "1:  andi a4, a4, 0xff\n   li s8, 0\n   li s9, 0\n"
        "10: bgeu s8, s7, 90f\n   slli t0, s8, 2\n   add t0, s6, t0\n   lwu t1, 0(t0)\n"
        "beq t1, a6, 20f\n   li t2, 0xffffffff\n   beq t1, t2, 20f\n   addi s8, s8, 1\n   j 10b\n"
        "20: slli t0, s8, 1\n   add t0, s5, t0\n   lhu a0, 0(t0)\n   slli t1, a0, 2\n   add t1, t1, a0\n"
        "slli t1, t1, 3\n   add a2, s3, t1\n   lbu t0, 2(a2)\n   bne t0, a4, 80f\n"
        "lbu t0, 3(a2)\n   andi t1, t0, 4\n   bnez t1, 80f\n   andi t1, t0, 16\n   beqz t1, 95f\n"
        "lhu t5, 4(a2)\n   slli t5, t5, 4\n   add t5, s4, t5\n   lhu t6, 6(a2)\n   slli t6, t6, 4\n   add t6, t5, t6\n   li t4, 1\n"
        "30: bgeu t5, t6, 50f\n   li t0, 1\n   bne t4, t0, 38f\n"
        RV_R64_SIDE("t5", 4, "s10", "s0", "s1", "s2", "a3", "63f")
        "63:\n"
        RV_R64_SIDE("t5", 8, "t3", "s0", "s1", "s2", "a3", "64f")
        "64: slt t1, t3, s10\n   slt t2, s10, t3\n   sub t1, t1, t2\n"
        "addi t1, t1, 1\n   lbu t2, 0(t5)\n   slli t2, t2, 2\n   add t1, t1, t2\n   li t0, 0x06431520\n"
        "srl t0, t0, t1\n   andi t0, t0, 1\n   andi t4, t4, 2\n   or t4, t4, t0\n"
        "38: lbu t0, 2(t5)\n   andi t1, t0, 3\n   beqz t1, 39f\n   slli t1, t4, 1\n   or t4, t4, t1\n"
        "andi t4, t4, 2\n   ori t4, t4, 1\n   andi t1, t0, 2\n   beqz t1, 39f\n   andi t1, t4, 2\n   beqz t1, 45f\n   li t4, 1\n"
        "39: addi t5, t5, 16\n   j 30b\n"
        "45: lbu t0, 3(a2)\n   andi t0, t0, 1\n   bnez t0, 47f\n   lhu t0, 4(a2)\n   slli t0, t0, 4\n   add t0, s4, t0\n"
        "sub t0, t5, t0\n   srli t0, t0, 4\n   lbu t1, 8(a2)\n   bltu t0, t1, 80f\n"
        "47: lbu t3, 10(a2)\n   beqz t3, 80f\n   lhu t4, 12(a2)\n   lbu t0, 9(a2)\n   add t4, t4, t0\n   li t5, 1\n   j 55f\n"
        "50: lbu t3, 9(a2)\n   lhu t4, 12(a2)\n   li t5, 0\n"
        "55: slli t0, a0, 1\n   add t0, t0, a0\n   slli t0, t0, 3\n   add t6, a1, t0\n"
        "lbu t0, 3(a2)\n   andi t0, t0, 2\n   beqz t0, 56f\n   lbu t0, 17(t6)\n   bnez t0, 80f\n"
        "56: lwu t0, 16(a2)\n   beqz t0, 57f\n   lwu t1, 8(t6)\n   beqz t1, 57f\n   li t1, 1000000\n   mul t0, t0, t1\n"
        "ld t1, 8(a3)\n   ld t2, 0(t6)\n   sub t1, t1, t2\n   bge t1, t0, 57f\n   lwu t1, 12(t6)\n   addi t1, t1, 1\n   sw t1, 12(t6)\n   j 80f\n"
        "57: slli t4, t4, 16\n   or t0, a0, t4\n   slli t3, t3, 32\n   or t0, t0, t3\n   slli t5, t5, 40\n   or t0, t0, t5\n"
        "slli t1, s9, 3\n   add t1, a5, t1\n   sd t0, 0(t1)\n   lwu t1, 8(t6)\n   addi t1, t1, 1\n   sw t1, 8(t6)\n"
        "ld t1, 8(a3)\n   sd t1, 0(t6)\n   lbu t1, 3(a2)\n   andi t1, t1, 2\n   beqz t1, 58f\n   li t1, 1\n   sb t1, 17(t6)\n"
        "58: addi s9, s9, 1\n   bgeu s9, a7, 90f\n"
        "80: addi s8, s8, 1\n   j 10b\n"
        "90: mv a0, s9\n   j 99f\n"
        "95: slli a0, s8, 32\n   or a0, a0, s9\n   li t0, 1\n   slli t0, t0, 63\n   or a0, a0, t0\n"
        "99: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   ld s4, 32(sp)\n"
        "ld s5, 40(sp)\n   ld s6, 48(sp)\n   ld s7, 56(sp)\n   ld s8, 64(sp)\n   ld s9, 72(sp)\n   ld s10, 80(sp)\n   addi sp, sp, 96\n" RV_ASM_RET
        "98: li a0, 0\n" RV_ASM_RET
        RV_ASM_END(rover_walk_numbers)

        /* rover_eval_edges_numbers(a0=table, a1=runs, a2=pages, a3=changed, a4=event, a5=place_room, a6=hits) -> a0 */
        RV_ASM_FUNC(rover_eval_edges_numbers)
        "addi sp, sp, -96\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n   sd s5, 40(sp)\n"
        "sd s6, 48(sp)\n   sd s7, 56(sp)\n   sd s8, 64(sp)\n   sd s9, 72(sp)\n   sd s10, 80(sp)\n   sd s11, 88(sp)\n"
        "ld s0, 0(a2)\n   ld s1, 8(a2)\n   ld s2, 80(a0)\n   ld s3, 8(a0)\n   ld s4, 40(a0)\n"
        "ld t1, 32(a0)\n   lhu t2, 0(t1)\n   lhu s6, 2(t1)\n   li t3, 0x7fff\n   and s6, s6, t3\n"
        "ld s5, 16(a0)\n   slli t0, t2, 1\n   add s5, s5, t0\n   srli s9, a5, 32\n   andi s10, a5, 0xff\n"
        "mv s11, a3\n   li s7, 0\n   li s8, 0\n"
        "10: bgeu s7, s6, 90f\n   slli t0, s7, 1\n   add t0, s5, t0\n   lhu a0, 0(t0)\n"
        "slli t1, a0, 2\n   add t1, t1, a0\n   slli t1, t1, 3\n   add a2, s3, t1\n"
        "lbu t0, 2(a2)\n   bne t0, s10, 80f\n   lbu t0, 3(a2)\n   andi t1, t0, 4\n   bnez t1, 80f\n"
        "ld t1, 32(a2)\n   and t1, t1, s11\n   beqz t1, 80f\n   andi t1, t0, 16\n   beqz t1, 95f\n"
        "lhu t5, 4(a2)\n   slli t5, t5, 4\n   add t5, s4, t5\n   lhu t6, 6(a2)\n   slli t6, t6, 4\n   add t6, t5, t6\n   li t4, 1\n"
        "30: bgeu t5, t6, 46f\n   li t0, 1\n   bne t4, t0, 38f\n"
        RV_R64_SIDE("t5", 4, "a7", "s0", "s1", "s2", "a4", "63f")
        "63:\n"
        RV_R64_SIDE("t5", 8, "t3", "s0", "s1", "s2", "a4", "64f")
        "64: slt t1, t3, a7\n   slt t2, a7, t3\n   sub t1, t1, t2\n   addi t1, t1, 1\n"
        "lbu t2, 0(t5)\n   slli t2, t2, 2\n   add t1, t1, t2\n   li t0, 0x06431520\n"
        "srl t0, t0, t1\n   andi t0, t0, 1\n   andi t4, t4, 2\n   or t4, t4, t0\n"
        "38: lbu t0, 2(t5)\n   andi t1, t0, 3\n   beqz t1, 39f\n   slli t1, t4, 1\n   or t4, t4, t1\n"
        "andi t4, t4, 2\n   ori t4, t4, 1\n   andi t1, t0, 2\n   beqz t1, 39f\n   andi t1, t4, 2\n   beqz t1, 45f\n   li t4, 1\n"
        "39: addi t5, t5, 16\n   j 30b\n"
        "45: li t5, 0\n   j 48f\n"
        "46: li t5, 1\n"
        "48: slli t0, a0, 1\n   add t0, t0, a0\n   slli t0, t0, 3\n   add t6, a1, t0\n"
        "lbu t0, 16(t6)\n   beq t0, t5, 80f\n   lbu t3, 10(a2)\n   bne s8, s9, 49f\n   or t0, t5, t3\n   bnez t0, 80f\n"
        "49: sb t5, 16(t6)\n   bnez t5, 50f\n   beqz t3, 80f\n   lhu t4, 12(a2)\n   lbu t0, 9(a2)\n   add t4, t4, t0\n   li t5, 1\n   j 55f\n"
        "50: lbu t3, 9(a2)\n   lhu t4, 12(a2)\n   li t5, 0\n"
        "55: lbu t0, 3(a2)\n   andi t0, t0, 2\n   beqz t0, 56f\n   lbu t0, 17(t6)\n   bnez t0, 80f\n"
        "56: lwu t0, 16(a2)\n   beqz t0, 57f\n   lwu t1, 8(t6)\n   beqz t1, 57f\n   li t1, 1000000\n   mul t0, t0, t1\n"
        "ld t1, 8(a4)\n   ld t2, 0(t6)\n   sub t1, t1, t2\n   bge t1, t0, 57f\n   lwu t1, 12(t6)\n   addi t1, t1, 1\n   sw t1, 12(t6)\n   j 80f\n"
        "57: slli t4, t4, 16\n   or t0, a0, t4\n   slli t3, t3, 32\n   or t0, t0, t3\n   slli t5, t5, 40\n   or t0, t0, t5\n"
        "slli t1, s8, 3\n   add t1, a6, t1\n   sd t0, 0(t1)\n   lwu t1, 8(t6)\n   addi t1, t1, 1\n   sw t1, 8(t6)\n"
        "ld t1, 8(a4)\n   sd t1, 0(t6)\n   lbu t1, 3(a2)\n   andi t1, t1, 2\n   beqz t1, 58f\n   li t1, 1\n   sb t1, 17(t6)\n"
        "58: addi s8, s8, 1\n"
        "80: addi s7, s7, 1\n   j 10b\n"
        "90: mv a0, s8\n   j 99f\n"
        "95: slli a0, s7, 32\n   or a0, a0, s8\n   li t0, 1\n   slli t0, t0, 63\n   or a0, a0, t0\n"
        "99: ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   ld s4, 32(sp)\n   ld s5, 40(sp)\n"
        "ld s6, 48(sp)\n   ld s7, 56(sp)\n   ld s8, 64(sp)\n   ld s9, 72(sp)\n   ld s10, 80(sp)\n   ld s11, 88(sp)\n   addi sp, sp, 96\n" RV_ASM_RET
        RV_ASM_END(rover_eval_edges_numbers)
);

#undef RV_R64_SIDE

#endif

#endif /* ROVER_EVAL_ASM_IMPLEMENTATION */
