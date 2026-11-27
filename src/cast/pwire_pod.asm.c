/*
        PipeWire SPA pod binary parser & wire helpers in pure assembly:
          - pwire_pod_get32   : little-endian u32 wire reader (NULL-safe)
          - pwire_pod_put32   : little-endian u32 wire writer (NULL-safe)
          - pwire_pod_next    : bounded 8-byte-aligned SPA pod framing parser (overflow-proof)
          - pwire_pod_struct  : SPA_TYPE_Struct cursor initializer
          - pwire_pod_member  : bounded next member extractor with sticky bad flag
          - pwire_pod_int     : SPA Int/Id/Bool 32-bit member extractor
          - pwire_pod_string  : SPA String/None NUL-terminated member extractor
          - pwire_pod_values  : SPA Choice / plain property value view extractor
          - pwire_pod_value32 : overflow-proof indexed 32-bit word reader from values
          - pwire_pod_allows  : SPA Choice (None/Range/Step/Enum/Flags) membership test
          - pwire_pod_object  : SPA_TYPE_Object header + property cursor initializer
          - pwire_pod_prop    : bounded SPA object property (key + pod value) iterator

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only, 100% leaf functions.

        STB-style: include for declarations; define CAST_PWIRE_POD_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CAST_PWIRE_POD_ASM_H
#define MOONWATER_CAST_PWIRE_POD_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  pw_u8;
typedef p32 pw_u32;
typedef b32 pw_i32;
typedef p64 pw_u64;
typedef b64 pw_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  pw_u8;
typedef __UINT32_TYPE__ pw_u32;
typedef __INT32_TYPE__  pw_i32;
typedef __UINT64_TYPE__ pw_u64;
typedef __INT64_TYPE__  pw_i64;
#else
#include <stdint.h>
typedef uint8_t  pw_u8;
typedef uint32_t pw_u32;
typedef int32_t  pw_i32;
typedef uint64_t pw_u64;
typedef int64_t  pw_i64;
#endif

/* Exact PipeWire SPA pod type constants (matches src/cast/pipewire.c) */
#define PW_POD_NONE      1u
#define PW_POD_BOOL      2u
#define PW_POD_ID        3u
#define PW_POD_INT       4u
#define PW_POD_STRING    8u
#define PW_POD_STRUCT    14u
#define PW_POD_OBJECT    15u
#define PW_POD_CHOICE    19u

/* Exact PipeWire SPA choice type constants */
#define PW_CHOICE_NONE   0u
#define PW_CHOICE_RANGE  1u
#define PW_CHOICE_STEP   2u
#define PW_CHOICE_ENUM   3u
#define PW_CHOICE_FLAGS  4u

/* 16 bytes — byte-for-byte identical to struct pwire_pod */
struct pwire_pod_view {
        pw_u32       type;  /* +0 */
        pw_u32       size;  /* +4 */
        const pw_u8 *body;  /* +8 */
};

/* 32 bytes — byte-for-byte identical to struct pwire_members */
struct pwire_pod_members {
        const pw_u8 *p;     /* +0  */
        pw_i64       n;     /* +8  */
        pw_i64       at;    /* +16 */
        pw_i32       bad;   /* +24 */
};

/* 24 bytes — byte-for-byte identical to struct pwire_values */
struct pwire_pod_values {
        pw_u32       type;   /* +0  */
        pw_u32       choice; /* +4  */
        pw_u32       size;   /* +8  */
        pw_u32       count;  /* +12 */
        const pw_u8 *at;     /* +16 */
};

/* 32 bytes — byte-for-byte identical to struct pwire_props */
struct pwire_pod_props {
        pw_u32       type;  /* +0  */
        pw_u32       id;    /* +4  */
        const pw_u8 *p;     /* +8  */
        pw_i64       n;     /* +16 */
        pw_i64       at;    /* +24 */
};

#ifdef offset_of
#define PW_POD_OFFSET(T, M) offset_of(T, M)
#else
#define PW_POD_OFFSET(T, M) ((pw_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct pwire_pod_view) == 16, "pwire_pod_view must be 16 bytes");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_view, type) == 0, "pwire_pod_view.type");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_view, size) == 4, "pwire_pod_view.size");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_view, body) == 8, "pwire_pod_view.body");

_Static_assert(sizeof(struct pwire_pod_members) == 32, "pwire_pod_members must be 32 bytes");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_members, p)   == 0,  "pwire_pod_members.p");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_members, n)   == 8,  "pwire_pod_members.n");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_members, at)  == 16, "pwire_pod_members.at");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_members, bad) == 24, "pwire_pod_members.bad");

_Static_assert(sizeof(struct pwire_pod_values) == 24, "pwire_pod_values must be 24 bytes");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_values, type)   == 0,  "pwire_pod_values.type");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_values, choice) == 4,  "pwire_pod_values.choice");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_values, size)   == 8,  "pwire_pod_values.size");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_values, count)  == 12, "pwire_pod_values.count");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_values, at)     == 16, "pwire_pod_values.at");

_Static_assert(sizeof(struct pwire_pod_props) == 32, "pwire_pod_props must be 32 bytes");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_props, type) == 0,  "pwire_pod_props.type");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_props, id)   == 4,  "pwire_pod_props.id");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_props, p)    == 8,  "pwire_pod_props.p");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_props, n)    == 16, "pwire_pod_props.n");
_Static_assert(PW_POD_OFFSET(struct pwire_pod_props, at)   == 24, "pwire_pod_props.at");

pw_u32 pwire_pod_get32(const pw_u8 *p);
void   pwire_pod_put32(pw_u8 *p, pw_u32 v);
pw_i32 pwire_pod_next(const pw_u8 *p, pw_i64 n, pw_i64 *at, struct pwire_pod_view *pod);
pw_i32 pwire_pod_struct(const struct pwire_pod_view *pod, struct pwire_pod_members *m);
pw_i32 pwire_pod_member(struct pwire_pod_members *m, struct pwire_pod_view *out);
pw_u32 pwire_pod_int(struct pwire_pod_members *m);
pw_i32 pwire_pod_string(struct pwire_pod_members *m, const char **out, pw_i64 *length);
pw_i32 pwire_pod_values(const struct pwire_pod_view *pod, struct pwire_pod_values *v);
pw_u32 pwire_pod_value32(const struct pwire_pod_values *v, pw_u32 k, pw_u32 word);
pw_i32 pwire_pod_allows(const struct pwire_pod_values *v, pw_u32 x);
pw_i32 pwire_pod_object(const struct pwire_pod_view *pod, struct pwire_pod_props *o);
pw_i32 pwire_pod_prop(struct pwire_pod_props *o, pw_u32 *key, struct pwire_pod_view *value);

#endif /* MOONWATER_CAST_PWIRE_POD_ASM_H */

#ifdef CAST_PWIRE_POD_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define PW_POD_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define PW_POD_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define PW_POD_RISCV64 1
# else
#  error "src/cast/pwire_pod.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define PW_POD_ARM64 1
# elif defined(X64) && X64
#  define PW_POD_X64 1
# elif defined(RISCV64) && RISCV64
#  define PW_POD_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define PW_ASM_SECTION   ASM_SECTION
# define PW_ASM_FUNC(n)   ASM_FUNC(n)
# define PW_ASM_END(n)    ASM_END(n)
# define PW_ASM_RET       ASM_RET
#else
# define PW_ASM_SECTION   ".text\n"
# define PW_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define PW_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define PW_ASM_END(n)   ""
# elif defined(_WIN32)
#  define PW_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define PW_ASM_END(n)   ""
# else
#  if defined(PW_POD_ARM64)
#   define PW_ASM_FTYPE   "%function"
#  else
#   define PW_ASM_FTYPE   "@function"
#  endif
#  define PW_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " PW_ASM_FTYPE "\n" #n ":\n"
#  define PW_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(PW_POD_ARM64)

__asm__(
        PW_ASM_SECTION

        /* pwire_pod_get32(x0=p) -> w0 */
        PW_ASM_FUNC(pwire_pod_get32)
        "cbz x0, 1f\n   ldr w0, [x0]\n" PW_ASM_RET
        "1:  mov w0, #0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_get32)

        /* pwire_pod_put32(x0=p, w1=v) */
        PW_ASM_FUNC(pwire_pod_put32)
        "cbz x0, 1f\n   str w1, [x0]\n"
        "1:  " PW_ASM_RET
        PW_ASM_END(pwire_pod_put32)

        /* pwire_pod_next(x0=p, x1=n, x2=at, x3=pod) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_next)
        "cbz x0, 9f\n   cbz x2, 9f\n   cbz x3, 9f\n   ldr x4, [x2]\n   tbnz x4, #63, 9f\n"
        "tbnz x1, #63, 9f\n   cmp x4, x1\n   b.gt 9f\n   sub x5, x1, x4\n   cmp x5, #8\n   b.lt 9f\n"
        "add x6, x0, x4\n   ldp w7, w8, [x6]\n   sub x9, x5, #8\n   cmp x7, x9\n   b.hi 9f\n"
        "stp w8, w7, [x3]\n   add x6, x6, #8\n   str x6, [x3, #8]\n"
        "add x7, x7, #7\n   and x7, x7, #-8\n   add x7, x7, #8\n"
        "cmp x7, x5\n   add x4, x4, x7\n   csel x4, x1, x4, hs\n   str x4, [x2]\n   mov w0, #0\n" PW_ASM_RET
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_next)

        /* pwire_pod_struct(x0=pod, x1=m) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_struct)
        "cbz x0, 9f\n   cbz x1, 9f\n   ldp w2, w3, [x0]\n   ldr x4, [x0, #8]\n"
        "stp x4, x3, [x1]\n   str xzr, [x1, #16]\n   cmp w2, #14\n   cset w5, ne\n   str w5, [x1, #24]\n"
        "neg w0, w5\n" PW_ASM_RET
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_struct)

        /* pwire_pod_member(x0=m, x1=out) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_member)
        "cbz x1, 8f\n   stp xzr, xzr, [x1]\n   cbz x0, 9f\n   ldr w2, [x0, #24]\n   cbnz w2, 9f\n"
        "ldp x3, x4, [x0]\n   ldr x5, [x0, #16]\n   cbz x3, 8f\n   tbnz x5, #63, 8f\n"
        "tbnz x4, #63, 8f\n   cmp x5, x4\n   b.gt 8f\n   sub x6, x4, x5\n   cmp x6, #8\n   b.lt 8f\n"
        "add x7, x3, x5\n   ldp w8, w9, [x7]\n   sub x10, x6, #8\n   cmp x8, x10\n   b.hi 8f\n"
        "stp w9, w8, [x1]\n   add x7, x7, #8\n   str x7, [x1, #8]\n"
        "add x8, x8, #7\n   and x8, x8, #-8\n   add x8, x8, #8\n"
        "cmp x8, x6\n   add x5, x5, x8\n   csel x5, x4, x5, hs\n   str x5, [x0, #16]\n   mov w0, #0\n" PW_ASM_RET
        "8:  cbz x0, 9f\n   mov w2, #1\n   str w2, [x0, #24]\n"
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_member)

        /* pwire_pod_int(x0=m) -> u32 (0 and m->bad=1 on error) */
        PW_ASM_FUNC(pwire_pod_int)
        "cbz x0, 9f\n   ldr w1, [x0, #24]\n   cbnz w1, 9f\n   ldp x2, x3, [x0]\n   ldr x4, [x0, #16]\n"
        "cbz x2, 8f\n   tbnz x4, #63, 8f\n   tbnz x3, #63, 8f\n   cmp x4, x3\n   b.gt 8f\n"
        "sub x5, x3, x4\n   cmp x5, #8\n   b.lt 8f\n   add x6, x2, x4\n   ldp w7, w8, [x6]\n"
        "sub x9, x5, #8\n   cmp x7, x9\n   b.hi 8f\n   add x10, x7, #7\n   and x10, x10, #-8\n"
        "add x10, x10, #8\n   cmp x10, x5\n   add x4, x4, x10\n   csel x4, x3, x4, hs\n   str x4, [x0, #16]\n"
        "sub w8, w8, #2\n   cmp w8, #2\n   b.hi 8f\n   cmp w7, #4\n   b.lo 8f\n"
        "ldr w0, [x6, #8]\n" PW_ASM_RET
        "8:  mov w1, #1\n   str w1, [x0, #24]\n"
        "9:  mov w0, #0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_int)

        /* pwire_pod_string(x0=m, x1=out, x2=length) -> 1 string / 0 none / -1 err */
        PW_ASM_FUNC(pwire_pod_string)
        "cbz x1, 1f\n   str xzr, [x1]\n"
        "1:  cbz x2, 2f\n   str xzr, [x2]\n"
        "2:  cbz x0, 9f\n   ldr w3, [x0, #24]\n   cbnz w3, 9f\n   ldp x4, x5, [x0]\n   ldr x6, [x0, #16]\n"
        "cbz x4, 8f\n   tbnz x6, #63, 8f\n   tbnz x5, #63, 8f\n   cmp x6, x5\n   b.gt 8f\n"
        "sub x7, x5, x6\n   cmp x7, #8\n   b.lt 8f\n   add x8, x4, x6\n   ldp w9, w10, [x8]\n"
        "sub x11, x7, #8\n   cmp x9, x11\n   b.hi 8f\n   add x11, x9, #7\n   and x11, x11, #-8\n"
        "add x11, x11, #8\n   cmp x11, x7\n   add x6, x6, x11\n   csel x6, x5, x6, hs\n   str x6, [x0, #16]\n"
        "cmp w10, #1\n   b.eq 7f\n   cmp w10, #8\n   b.ne 8f\n   cbz w9, 8f\n"
        "add x8, x8, #8\n   sub x11, x9, #1\n   ldrb w12, [x8, x11]\n   cbnz w12, 8f\n   mov x11, #0\n"
        "3:  ldrb w12, [x8, x11]\n   cbz w12, 4f\n   add x11, x11, #1\n   b 3b\n"
        "4:  cbz x1, 5f\n   str x8, [x1]\n"
        "5:  cbz x2, 6f\n   str x11, [x2]\n"
        "6:  mov w0, #1\n" PW_ASM_RET
        "7:  mov w0, #0\n" PW_ASM_RET
        "8:  mov w3, #1\n   str w3, [x0, #24]\n"
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_string)

        /* pwire_pod_values(x0=pod, x1=v) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_values)
        "cbz x0, 9f\n   cbz x1, 9f\n   ldp w2, w3, [x0]\n   ldr x4, [x0, #8]\n"
        "cmp w2, #19\n   b.eq 1f\n   mov w5, #1\n   stp w2, wzr, [x1]\n   stp w3, w5, [x1, #8]\n"
        "str x4, [x1, #16]\n   mov w0, #0\n" PW_ASM_RET
        "1:  cbz x4, 9f\n   cmp w3, #16\n   b.lo 9f\n   ldr w5, [x4]\n   ldp w6, w7, [x4, #8]\n"
        "sub w3, w3, #16\n   cbz w6, 9f\n   cmp w6, w3\n   b.hi 9f\n   udiv w8, w3, w6\n"
        "add x4, x4, #16\n   stp w7, w5, [x1]\n   stp w6, w8, [x1, #8]\n   str x4, [x1, #16]\n   mov w0, #0\n" PW_ASM_RET
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_values)

        /* pwire_pod_value32(x0=v, w1=k, w2=word) -> u32 */
        PW_ASM_FUNC(pwire_pod_value32)
        "cbz x0, 9f\n   ldr x5, [x0, #16]\n   cbz x5, 9f\n   ldp w3, w4, [x0, #8]\n"
        "cmp w1, w4\n   b.hs 9f\n   mov w1, w1\n   mov w2, w2\n   add x6, x2, #1\n   lsl x6, x6, #2\n"
        "cmp x3, x6\n   b.lo 9f\n   lsl x6, x2, #2\n   madd x6, x1, x3, x6\n   ldr w0, [x5, x6]\n" PW_ASM_RET
        "9:  mov w0, #0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_value32)

        /* pwire_pod_allows(x0=v, w1=x) -> 1 / 0 */
        PW_ASM_FUNC(pwire_pod_allows)
        "cbz x0, 9f\n   ldr w2, [x0, #4]\n   ldp w3, w4, [x0, #8]\n   ldr x5, [x0, #16]\n"
        "cbz w2, 1f\n   cmp w2, #3\n   b.eq 2f\n   cmp w2, #1\n   b.eq 5f\n   cmp w2, #2\n   b.eq 5f\n"
        "cmp w2, #4\n   b.ne 9f\n   mov w6, #0\n   cbz w4, 6f\n   cmp w3, #4\n   b.lo 6f\n   cbz x5, 6f\n   ldr w6, [x5]\n"
        "6:  and w6, w6, w1\n   cmp w6, w1\n   cset w0, eq\n" PW_ASM_RET
        "1:  mov w6, #0\n   cbz w4, 7f\n   cmp w3, #4\n   b.lo 7f\n   cbz x5, 7f\n   ldr w6, [x5]\n"
        "7:  cmp w6, w1\n   cset w0, eq\n" PW_ASM_RET
        "2:  cbz w4, 9f\n   cmp w3, #4\n   b.lo 8f\n   cbz x5, 8f\n   cmp w4, #1\n   cset w6, hi\n"
        "madd x7, x6, x3, x5\n"
        "3:  cmp w6, w4\n   b.hs 9f\n   ldr w8, [x7]\n   cmp w8, w1\n   b.eq 4f\n   add x7, x7, x3\n   add w6, w6, #1\n   b 3b\n"
        "4:  mov w0, #1\n" PW_ASM_RET
        "5:  cmp w4, #3\n   b.lo 9f\n   cmp w3, #4\n   b.lo 8f\n   cbz x5, 8f\n"
        "add x5, x5, x3\n   ldr w6, [x5]\n   cmp w6, w1\n   b.hi 9f\n   ldr w7, [x5, x3]\n   cmp w1, w7\n   cset w0, ls\n" PW_ASM_RET
        "8:  cmp w1, #0\n   cset w0, eq\n" PW_ASM_RET
        "9:  mov w0, #0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_allows)

        /* pwire_pod_object(x0=pod, x1=o) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_object)
        "cbz x0, 9f\n   cbz x1, 9f\n   ldp w2, w3, [x0]\n   ldr x4, [x0, #8]\n"
        "cbz x4, 9f\n   cmp w2, #15\n   b.ne 9f\n   cmp w3, #8\n   b.lo 9f\n"
        "ldp w5, w6, [x4]\n   add x4, x4, #8\n   sub w3, w3, #8\n"
        "stp w5, w6, [x1]\n   stp x4, x3, [x1, #8]\n   str xzr, [x1, #24]\n   mov w0, #0\n" PW_ASM_RET
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_object)

        /* pwire_pod_prop(x0=o, x1=key, x2=value) -> 1 prop / 0 end / -1 err */
        PW_ASM_FUNC(pwire_pod_prop)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x2, 9f\n   ldp x3, x4, [x0, #16]\n"
        "tbnz x4, #63, 9f\n   cmp x4, x3\n   b.ge 8f\n   ldr x5, [x0, #8]\n   cbz x5, 9f\n"
        "sub x6, x3, x4\n   cmp x6, #8\n   b.lt 9f\n   add x7, x5, x4\n   ldr w8, [x7]\n   str w8, [x1]\n"
        "add x4, x4, #8\n   str x4, [x0, #24]\n   add x7, x7, #8\n   sub x6, x6, #8\n   cmp x6, #8\n   b.lt 9f\n"
        "ldp w8, w9, [x7]\n   sub x10, x6, #8\n   cmp x8, x10\n   b.hi 9f\n"
        "stp w9, w8, [x2]\n   add x7, x7, #8\n   str x7, [x2, #8]\n"
        "add x8, x8, #7\n   and x8, x8, #-8\n   add x8, x8, #8\n"
        "cmp x8, x6\n   add x4, x4, x8\n   csel x4, x3, x4, hs\n   str x4, [x0, #24]\n   mov w0, #1\n" PW_ASM_RET
        "8:  mov w0, #0\n" PW_ASM_RET
        "9:  mov w0, #-1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_prop)
);

#elif defined(PW_POD_X64)

#if defined(_WIN32)
# define PW_R1   "rcx"
# define PW_R1D  "ecx"
# define PW_R2   "rdx"
# define PW_R2D  "edx"
# define PW_R3   "r8"
# define PW_R3D  "r8d"
# define PW_R4   "r9"
# define PW_R4D  "r9d"
# define PW_T1   "r10"
# define PW_T1D  "r10d"
# define PW_T2   "r11"
# define PW_T2D  "r11d"
#else
# define PW_R1   "rdi"
# define PW_R1D  "edi"
# define PW_R2   "rsi"
# define PW_R2D  "esi"
# define PW_R3   "rdx"
# define PW_R3D  "edx"
# define PW_R4   "rcx"
# define PW_R4D  "ecx"
# define PW_T1   "r8"
# define PW_T1D  "r8d"
# define PW_T2   "r9"
# define PW_T2D  "r9d"
#endif

__asm__(
        PW_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* pwire_pod_get32(R1=p) -> eax */
        PW_ASM_FUNC(pwire_pod_get32)
        "xor eax, eax\n   test " PW_R1 ", " PW_R1 "\n   jz 1f\n   mov eax, dword ptr [" PW_R1 "]\n"
        "1:  " PW_ASM_RET
        PW_ASM_END(pwire_pod_get32)

        /* pwire_pod_put32(R1=p, R2D=v) */
        PW_ASM_FUNC(pwire_pod_put32)
        "test " PW_R1 ", " PW_R1 "\n   jz 1f\n   mov dword ptr [" PW_R1 "], " PW_R2D "\n"
        "1:  " PW_ASM_RET
        PW_ASM_END(pwire_pod_put32)

        /* pwire_pod_next(R1=p, R2=n, R3=at, R4=pod) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_next)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   test " PW_R3 ", " PW_R3 "\n   jz 9f\n   test " PW_R4 ", " PW_R4 "\n   jz 9f\n"
        "mov rax, [" PW_R3 "]\n   test rax, rax\n   js 9f\n   test " PW_R2 ", " PW_R2 "\n   js 9f\n   cmp rax, " PW_R2 "\n   jg 9f\n"
        "mov " PW_T1 ", " PW_R2 "\n   sub " PW_T1 ", rax\n   cmp " PW_T1 ", 8\n   jl 9f\n   add " PW_R1 ", rax\n"
        "mov " PW_T2D ", dword ptr [" PW_R1 "]\n   sub " PW_T1 ", 8\n   cmp " PW_T2 ", " PW_T1 "\n   ja 9f\n"
        "mov dword ptr [" PW_R4 " + 4], " PW_T2D "\n   mov " PW_T1D ", dword ptr [" PW_R1 " + 4]\n   mov dword ptr [" PW_R4 "], " PW_T1D "\n"
        "lea " PW_T1 ", [" PW_R1 " + 8]\n   mov [" PW_R4 " + 8], " PW_T1 "\n"
        "add " PW_T2 ", 7\n   and " PW_T2 ", -8\n   add " PW_T2 ", 8\n   mov " PW_T1 ", " PW_R2 "\n   sub " PW_T1 ", rax\n"
        "cmp " PW_T2 ", " PW_T1 "\n   jae 1f\n   add rax, " PW_T2 "\n   mov [" PW_R3 "], rax\n   xor eax, eax\n" PW_ASM_RET
        "1:  mov [" PW_R3 "], " PW_R2 "\n   xor eax, eax\n" PW_ASM_RET
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_next)

        /* pwire_pod_struct(R1=pod, R2=m) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_struct)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   test " PW_R2 ", " PW_R2 "\n   jz 9f\n   mov rax, [" PW_R1 " + 8]\n   mov [" PW_R2 "], rax\n"
        "mov " PW_R3D ", dword ptr [" PW_R1 " + 4]\n   mov [" PW_R2 " + 8], " PW_R3 "\n   mov qword ptr [" PW_R2 " + 16], 0\n"
        "xor eax, eax\n   cmp dword ptr [" PW_R1 "], 14\n   setne al\n   mov dword ptr [" PW_R2 " + 24], eax\n   neg eax\n" PW_ASM_RET
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_struct)

        /* pwire_pod_member(R1=m, R2=out) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_member)
        "test " PW_R2 ", " PW_R2 "\n   jz 8f\n   mov qword ptr [" PW_R2 "], 0\n   mov qword ptr [" PW_R2 " + 8], 0\n"
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   cmp dword ptr [" PW_R1 " + 24], 0\n   jne 9f\n"
        "mov " PW_R3 ", [" PW_R1 "]\n   test " PW_R3 ", " PW_R3 "\n   jz 8f\n   mov " PW_R4 ", [" PW_R1 " + 8]\n   mov rax, [" PW_R1 " + 16]\n"
        "test rax, rax\n   js 8f\n   test " PW_R4 ", " PW_R4 "\n   js 8f\n   cmp rax, " PW_R4 "\n   jg 8f\n"
        "mov " PW_T1 ", " PW_R4 "\n   sub " PW_T1 ", rax\n   cmp " PW_T1 ", 8\n   jl 8f\n   add " PW_R3 ", rax\n"
        "mov " PW_T2D ", dword ptr [" PW_R3 "]\n   sub " PW_T1 ", 8\n   cmp " PW_T2 ", " PW_T1 "\n   ja 8f\n"
        "mov dword ptr [" PW_R2 " + 4], " PW_T2D "\n   mov " PW_T1D ", dword ptr [" PW_R3 " + 4]\n   mov dword ptr [" PW_R2 "], " PW_T1D "\n"
        "lea " PW_T1 ", [" PW_R3 " + 8]\n   mov [" PW_R2 " + 8], " PW_T1 "\n"
        "add " PW_T2 ", 7\n   and " PW_T2 ", -8\n   add " PW_T2 ", 8\n   mov " PW_T1 ", " PW_R4 "\n   sub " PW_T1 ", rax\n"
        "cmp " PW_T2 ", " PW_T1 "\n   jae 1f\n   add rax, " PW_T2 "\n   mov [" PW_R1 " + 16], rax\n   xor eax, eax\n" PW_ASM_RET
        "1:  mov [" PW_R1 " + 16], " PW_R4 "\n   xor eax, eax\n" PW_ASM_RET
        "8:  test " PW_R1 ", " PW_R1 "\n   jz 9f\n   mov dword ptr [" PW_R1 " + 24], 1\n"
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_member)

        /* pwire_pod_int(R1=m) -> u32 */
        PW_ASM_FUNC(pwire_pod_int)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   cmp dword ptr [" PW_R1 " + 24], 0\n   jne 9f\n"
        "mov " PW_R2 ", [" PW_R1 "]\n   test " PW_R2 ", " PW_R2 "\n   jz 8f\n   mov " PW_R3 ", [" PW_R1 " + 8]\n   mov rax, [" PW_R1 " + 16]\n"
        "test rax, rax\n   js 8f\n   test " PW_R3 ", " PW_R3 "\n   js 8f\n   cmp rax, " PW_R3 "\n   jg 8f\n"
        "mov " PW_R4 ", " PW_R3 "\n   sub " PW_R4 ", rax\n   cmp " PW_R4 ", 8\n   jl 8f\n   add " PW_R2 ", rax\n"
        "mov " PW_T1D ", dword ptr [" PW_R2 "]\n   sub " PW_R4 ", 8\n   cmp " PW_T1 ", " PW_R4 "\n   ja 8f\n"
        "lea " PW_T2 ", [" PW_T1 " + 7]\n   and " PW_T2 ", -8\n   add " PW_T2 ", 8\n   add " PW_R4 ", 8\n"
        "cmp " PW_T2 ", " PW_R4 "\n   jae 1f\n   add rax, " PW_T2 "\n   mov [" PW_R1 " + 16], rax\n   jmp 2f\n"
        "1:  mov [" PW_R1 " + 16], " PW_R3 "\n"
        "2:  mov " PW_R4D ", dword ptr [" PW_R2 " + 4]\n   sub " PW_R4D ", 2\n   cmp " PW_R4D ", 2\n   ja 8f\n   cmp " PW_T1D ", 4\n   jb 8f\n"
        "mov eax, dword ptr [" PW_R2 " + 8]\n" PW_ASM_RET
        "8:  mov dword ptr [" PW_R1 " + 24], 1\n"
        "9:  xor eax, eax\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_int)

        /* pwire_pod_string(R1=m, R2=out, R3=length) -> 1 string / 0 none / -1 err */
        PW_ASM_FUNC(pwire_pod_string)
        "test " PW_R2 ", " PW_R2 "\n   jz 1f\n   mov qword ptr [" PW_R2 "], 0\n"
        "1:  test " PW_R3 ", " PW_R3 "\n   jz 2f\n   mov qword ptr [" PW_R3 "], 0\n"
        "2:  test " PW_R1 ", " PW_R1 "\n   jz 9f\n   cmp dword ptr [" PW_R1 " + 24], 0\n   jne 9f\n"
        "mov " PW_R4 ", [" PW_R1 "]\n   test " PW_R4 ", " PW_R4 "\n   jz 8f\n   mov rax, [" PW_R1 " + 16]\n   mov " PW_T1 ", [" PW_R1 " + 8]\n"
        "test rax, rax\n   js 8f\n   test " PW_T1 ", " PW_T1 "\n   js 8f\n   cmp rax, " PW_T1 "\n   jg 8f\n"
        "sub " PW_T1 ", rax\n   cmp " PW_T1 ", 8\n   jl 8f\n   add " PW_R4 ", rax\n   mov " PW_T2D ", dword ptr [" PW_R4 "]\n"
        "sub " PW_T1 ", 8\n   cmp " PW_T2 ", " PW_T1 "\n   ja 8f\n   add " PW_T1 ", 8\n"
        "lea rax, [" PW_T2 " + 7]\n   and rax, -8\n   add rax, 8\n   cmp rax, " PW_T1 "\n   jae 3f\n"
        "add [" PW_R1 " + 16], rax\n   jmp 4f\n"
        "3:  mov rax, [" PW_R1 " + 8]\n   mov [" PW_R1 " + 16], rax\n"
        "4:  mov eax, dword ptr [" PW_R4 " + 4]\n   cmp eax, 1\n   je 7f\n   cmp eax, 8\n   jne 8f\n   test " PW_T2D ", " PW_T2D "\n   jz 8f\n"
        "add " PW_R4 ", 8\n   cmp byte ptr [" PW_R4 " + " PW_T2 " - 1], 0\n   jne 8f\n   xor " PW_T1D ", " PW_T1D "\n"
        "5:  cmp byte ptr [" PW_R4 " + " PW_T1 "], 0\n   je 6f\n   inc " PW_T1 "\n   jmp 5b\n"
        "6:  test " PW_R2 ", " PW_R2 "\n   jz 10f\n   mov [" PW_R2 "], " PW_R4 "\n"
        "10: test " PW_R3 ", " PW_R3 "\n   jz 11f\n   mov [" PW_R3 "], " PW_T1 "\n"
        "11: mov eax, 1\n" PW_ASM_RET
        "7:  xor eax, eax\n" PW_ASM_RET
        "8:  mov dword ptr [" PW_R1 " + 24], 1\n"
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_string)

        /* pwire_pod_values(R1=pod, R2=v) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_values)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   test " PW_R2 ", " PW_R2 "\n   jz 9f\n   mov eax, dword ptr [" PW_R1 "]\n"
        "mov " PW_R4D ", dword ptr [" PW_R1 " + 4]\n   mov " PW_T1 ", [" PW_R1 " + 8]\n   cmp eax, 19\n   je 1f\n"
        "mov dword ptr [" PW_R2 "], eax\n   mov dword ptr [" PW_R2 " + 4], 0\n   mov dword ptr [" PW_R2 " + 8], " PW_R4D "\n"
        "mov dword ptr [" PW_R2 " + 12], 1\n   mov [" PW_R2 " + 16], " PW_T1 "\n   xor eax, eax\n" PW_ASM_RET
        "1:  test " PW_T1 ", " PW_T1 "\n   jz 9f\n   cmp " PW_R4D ", 16\n   jb 9f\n   mov " PW_T2 ", " PW_R2 "\n"
        "mov " PW_R2D ", dword ptr [" PW_T1 " + 8]\n   sub " PW_R4D ", 16\n   test " PW_R2D ", " PW_R2D "\n   jz 9f\n"
        "cmp " PW_R2D ", " PW_R4D "\n   ja 9f\n   mov eax, " PW_R4D "\n   mov " PW_R4D ", " PW_R2D "\n   xor edx, edx\n   div " PW_R4D "\n"
        "mov edx, dword ptr [" PW_T1 " + 12]\n   mov dword ptr [" PW_T2 "], edx\n   mov edx, dword ptr [" PW_T1 "]\n"
        "mov dword ptr [" PW_T2 " + 4], edx\n   mov dword ptr [" PW_T2 " + 8], " PW_R4D "\n   mov dword ptr [" PW_T2 " + 12], eax\n"
        "add " PW_T1 ", 16\n   mov [" PW_T2 " + 16], " PW_T1 "\n   xor eax, eax\n" PW_ASM_RET
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_values)

        /* pwire_pod_value32(R1=v, R2D=k, R3D=word) -> u32 */
        PW_ASM_FUNC(pwire_pod_value32)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   mov " PW_T1 ", [" PW_R1 " + 16]\n   test " PW_T1 ", " PW_T1 "\n   jz 9f\n"
        "cmp " PW_R2D ", dword ptr [" PW_R1 " + 12]\n   jae 9f\n   mov " PW_R4D ", dword ptr [" PW_R1 " + 8]\n   mov eax, " PW_R3D "\n"
        "lea " PW_T2 ", [rax + 1]\n   shl " PW_T2 ", 2\n   cmp " PW_R4 ", " PW_T2 "\n   jb 9f\n   mov " PW_T2D ", " PW_R2D "\n"
        "imul " PW_T2 ", " PW_R4 "\n   shl rax, 2\n   add " PW_T2 ", rax\n   mov eax, dword ptr [" PW_T1 " + " PW_T2 "]\n" PW_ASM_RET
        "9:  xor eax, eax\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_value32)

        /* pwire_pod_allows(R1=v, R2D=x) -> 1 / 0 */
        PW_ASM_FUNC(pwire_pod_allows)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   mov eax, dword ptr [" PW_R1 " + 4]\n   mov " PW_R3D ", dword ptr [" PW_R1 " + 8]\n"
        "mov " PW_R4D ", dword ptr [" PW_R1 " + 12]\n   mov " PW_T1 ", [" PW_R1 " + 16]\n   test eax, eax\n   jz 1f\n"
        "cmp eax, 3\n   je 2f\n   cmp eax, 1\n   je 5f\n   cmp eax, 2\n   je 5f\n   cmp eax, 4\n   jne 9f\n"
        "xor " PW_T2D ", " PW_T2D "\n   test " PW_R4D ", " PW_R4D "\n   jz 6f\n   cmp " PW_R3D ", 4\n   jb 6f\n"
        "test " PW_T1 ", " PW_T1 "\n   jz 6f\n   mov " PW_T2D ", dword ptr [" PW_T1 "]\n"
        "6:  and " PW_T2D ", " PW_R2D "\n   xor eax, eax\n   cmp " PW_T2D ", " PW_R2D "\n   sete al\n" PW_ASM_RET
        "1:  xor " PW_T2D ", " PW_T2D "\n   test " PW_R4D ", " PW_R4D "\n   jz 7f\n   cmp " PW_R3D ", 4\n   jb 7f\n"
        "test " PW_T1 ", " PW_T1 "\n   jz 7f\n   mov " PW_T2D ", dword ptr [" PW_T1 "]\n"
        "7:  xor eax, eax\n   cmp " PW_T2D ", " PW_R2D "\n   sete al\n" PW_ASM_RET
        "2:  test " PW_R4D ", " PW_R4D "\n   jz 9f\n   cmp " PW_R3D ", 4\n   jb 8f\n   test " PW_T1 ", " PW_T1 "\n   jz 8f\n"
        "xor eax, eax\n   cmp " PW_R4D ", 1\n   seta al\n   mov " PW_T2D ", eax\n   imul rax, " PW_R3 "\n   add " PW_T1 ", rax\n"
        "3:  cmp " PW_T2D ", " PW_R4D "\n   jae 9f\n   cmp dword ptr [" PW_T1 "], " PW_R2D "\n   je 4f\n   add " PW_T1 ", " PW_R3 "\n   inc " PW_T2D "\n   jmp 3b\n"
        "4:  mov eax, 1\n" PW_ASM_RET
        "5:  cmp " PW_R4D ", 3\n   jb 9f\n   cmp " PW_R3D ", 4\n   jb 8f\n   test " PW_T1 ", " PW_T1 "\n   jz 8f\n"
        "cmp dword ptr [" PW_T1 " + " PW_R3 "], " PW_R2D "\n   ja 9f\n   xor eax, eax\n   cmp " PW_R2D ", dword ptr [" PW_T1 " + " PW_R3 "*2]\n   setbe al\n" PW_ASM_RET
        "8:  xor eax, eax\n   test " PW_R2D ", " PW_R2D "\n   sete al\n" PW_ASM_RET
        "9:  xor eax, eax\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_allows)

        /* pwire_pod_object(R1=pod, R2=o) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_object)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   test " PW_R2 ", " PW_R2 "\n   jz 9f\n   mov " PW_R3 ", [" PW_R1 " + 8]\n   test " PW_R3 ", " PW_R3 "\n   jz 9f\n"
        "cmp dword ptr [" PW_R1 "], 15\n   jne 9f\n   mov " PW_R4D ", dword ptr [" PW_R1 " + 4]\n   cmp " PW_R4D ", 8\n   jb 9f\n"
        "mov rax, [" PW_R3 "]\n   mov [" PW_R2 "], rax\n   add " PW_R3 ", 8\n   mov [" PW_R2 " + 8], " PW_R3 "\n"
        "sub " PW_R4D ", 8\n   mov [" PW_R2 " + 16], " PW_R4 "\n   mov qword ptr [" PW_R2 " + 24], 0\n   xor eax, eax\n" PW_ASM_RET
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_object)

        /* pwire_pod_prop(R1=o, R2=key, R3=value) -> 1 prop / 0 end / -1 err */
        PW_ASM_FUNC(pwire_pod_prop)
        "test " PW_R1 ", " PW_R1 "\n   jz 9f\n   test " PW_R2 ", " PW_R2 "\n   jz 9f\n   test " PW_R3 ", " PW_R3 "\n   jz 9f\n"
        "mov rax, [" PW_R1 " + 24]\n   mov " PW_R4 ", [" PW_R1 " + 16]\n   test rax, rax\n   js 9f\n   cmp rax, " PW_R4 "\n   jge 8f\n"
        "mov " PW_T1 ", [" PW_R1 " + 8]\n   test " PW_T1 ", " PW_T1 "\n   jz 9f\n   sub " PW_R4 ", rax\n   cmp " PW_R4 ", 8\n   jl 9f\n"
        "add " PW_T1 ", rax\n   mov " PW_T2D ", dword ptr [" PW_T1 "]\n   mov dword ptr [" PW_R2 "], " PW_T2D "\n"
        "add rax, 8\n   mov [" PW_R1 " + 24], rax\n   add " PW_T1 ", 8\n   sub " PW_R4 ", 8\n   cmp " PW_R4 ", 8\n   jl 9f\n"
        "mov " PW_T2D ", dword ptr [" PW_T1 "]\n   sub " PW_R4 ", 8\n   cmp " PW_T2 ", " PW_R4 "\n   ja 9f\n"
        "mov dword ptr [" PW_R3 " + 4], " PW_T2D "\n   mov " PW_R4D ", dword ptr [" PW_T1 " + 4]\n   mov dword ptr [" PW_R3 "], " PW_R4D "\n"
        "lea " PW_R4 ", [" PW_T1 " + 8]\n   mov [" PW_R3 " + 8], " PW_R4 "\n   mov " PW_R4 ", [" PW_R1 " + 16]\n   sub " PW_R4 ", rax\n"
        "add " PW_T2 ", 7\n   and " PW_T2 ", -8\n   add " PW_T2 ", 8\n   cmp " PW_T2 ", " PW_R4 "\n   jae 1f\n   add rax, " PW_T2 "\n"
        "mov [" PW_R1 " + 24], rax\n   mov eax, 1\n" PW_ASM_RET
        "1:  mov rax, [" PW_R1 " + 16]\n   mov [" PW_R1 " + 24], rax\n   mov eax, 1\n" PW_ASM_RET
        "8:  xor eax, eax\n" PW_ASM_RET
        "9:  mov eax, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_prop)

        ".att_syntax prefix\n"
);

#elif defined(PW_POD_RISCV64)

__asm__(
        PW_ASM_SECTION

        /* pwire_pod_get32(a0=p) -> a0 */
        PW_ASM_FUNC(pwire_pod_get32)
        "beqz a0, 1f\n   lw a0, 0(a0)\n" PW_ASM_RET
        "1:  li a0, 0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_get32)

        /* pwire_pod_put32(a0=p, a1=v) */
        PW_ASM_FUNC(pwire_pod_put32)
        "beqz a0, 1f\n   sw a1, 0(a0)\n"
        "1:  " PW_ASM_RET
        PW_ASM_END(pwire_pod_put32)

        /* pwire_pod_next(a0=p, a1=n, a2=at, a3=pod) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_next)
        "beqz a0, 9f\n   beqz a2, 9f\n   beqz a3, 9f\n   ld t0, 0(a2)\n   bltz t0, 9f\n"
        "bltz a1, 9f\n   bgt t0, a1, 9f\n   sub t1, a1, t0\n   li t2, 8\n   blt t1, t2, 9f\n"
        "add t3, a0, t0\n   lwu t4, 0(t3)\n   lwu t5, 4(t3)\n   addi t6, t1, -8\n   bgtu t4, t6, 9f\n"
        "sw t5, 0(a3)\n   sw t4, 4(a3)\n   addi t3, t3, 8\n   sd t3, 8(a3)\n"
        "addi t4, t4, 7\n   andi t4, t4, -8\n   addi t4, t4, 8\n"
        "bgeu t4, t1, 1f\n   add t0, t0, t4\n   sd t0, 0(a2)\n   li a0, 0\n" PW_ASM_RET
        "1:  sd a1, 0(a2)\n   li a0, 0\n" PW_ASM_RET
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_next)

        /* pwire_pod_struct(a0=pod, a1=m) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_struct)
        "beqz a0, 9f\n   beqz a1, 9f\n   lwu t0, 0(a0)\n   lwu t1, 4(a0)\n   ld t2, 8(a0)\n"
        "sd t2, 0(a1)\n   sd t1, 8(a1)\n   sd zero, 16(a1)\n   addi t0, t0, -14\n   snez t0, t0\n"
        "sw t0, 24(a1)\n   negw a0, t0\n" PW_ASM_RET
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_struct)

        /* pwire_pod_member(a0=m, a1=out) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_member)
        "beqz a1, 8f\n   sd zero, 0(a1)\n   sd zero, 8(a1)\n   beqz a0, 9f\n   lw t0, 24(a0)\n   bnez t0, 9f\n"
        "ld t1, 0(a0)\n   beqz t1, 8f\n   ld t2, 8(a0)\n   ld t3, 16(a0)\n   bltz t3, 8f\n"
        "bltz t2, 8f\n   bgt t3, t2, 8f\n   sub t4, t2, t3\n   li t5, 8\n   blt t4, t5, 8f\n"
        "add t5, t1, t3\n   lwu t6, 0(t5)\n   lwu a2, 4(t5)\n   addi a3, t4, -8\n   bgtu t6, a3, 8f\n"
        "sw a2, 0(a1)\n   sw t6, 4(a1)\n   addi t5, t5, 8\n   sd t5, 8(a1)\n"
        "addi t6, t6, 7\n   andi t6, t6, -8\n   addi t6, t6, 8\n"
        "bgeu t6, t4, 1f\n   add t3, t3, t6\n   sd t3, 16(a0)\n   li a0, 0\n" PW_ASM_RET
        "1:  sd t2, 16(a0)\n   li a0, 0\n" PW_ASM_RET
        "8:  beqz a0, 9f\n   li t0, 1\n   sw t0, 24(a0)\n"
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_member)

        /* pwire_pod_int(a0=m) -> u32 */
        PW_ASM_FUNC(pwire_pod_int)
        "beqz a0, 9f\n   lw t0, 24(a0)\n   bnez t0, 9f\n   ld t1, 0(a0)\n   beqz t1, 8f\n"
        "ld t2, 8(a0)\n   ld t3, 16(a0)\n   bltz t3, 8f\n   bltz t2, 8f\n   bgt t3, t2, 8f\n"
        "sub t4, t2, t3\n   li t5, 8\n   blt t4, t5, 8f\n   add t5, t1, t3\n   lwu t6, 0(t5)\n"
        "lwu a1, 4(t5)\n   addi a2, t4, -8\n   bgtu t6, a2, 8f\n   addi a2, t6, 7\n   andi a2, a2, -8\n"
        "addi a2, a2, 8\n   bgeu a2, t4, 1f\n   add t3, t3, a2\n   sd t3, 16(a0)\n   j 2f\n"
        "1:  sd t2, 16(a0)\n"
        "2:  addi a1, a1, -2\n   li a2, 2\n   bgtu a1, a2, 8f\n   li a2, 4\n   bltu t6, a2, 8f\n"
        "lw a0, 8(t5)\n" PW_ASM_RET
        "8:  li t0, 1\n   sw t0, 24(a0)\n"
        "9:  li a0, 0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_int)

        /* pwire_pod_string(a0=m, a1=out, a2=length) -> 1 string / 0 none / -1 err */
        PW_ASM_FUNC(pwire_pod_string)
        "beqz a1, 1f\n   sd zero, 0(a1)\n"
        "1:  beqz a2, 2f\n   sd zero, 0(a2)\n"
        "2:  beqz a0, 9f\n   lw t0, 24(a0)\n   bnez t0, 9f\n   ld t1, 0(a0)\n   beqz t1, 8f\n"
        "ld t2, 8(a0)\n   ld t3, 16(a0)\n   bltz t3, 8f\n   bltz t2, 8f\n   bgt t3, t2, 8f\n"
        "sub t4, t2, t3\n   li t5, 8\n   blt t4, t5, 8f\n   add t5, t1, t3\n   lwu t6, 0(t5)\n"
        "lwu a3, 4(t5)\n   addi a4, t4, -8\n   bgtu t6, a4, 8f\n   addi a4, t6, 7\n   andi a4, a4, -8\n"
        "addi a4, a4, 8\n   bgeu a4, t4, 3f\n   add t3, t3, a4\n   sd t3, 16(a0)\n   j 4f\n"
        "3:  sd t2, 16(a0)\n"
        "4:  li a4, 1\n   beq a3, a4, 7f\n   li a4, 8\n   bne a3, a4, 8f\n   beqz t6, 8f\n"
        "addi t5, t5, 8\n   add a4, t5, t6\n   lbu a5, -1(a4)\n   bnez a5, 8f\n   mv a4, t5\n"
        "5:  lbu a5, 0(a4)\n   beqz a5, 6f\n   addi a4, a4, 1\n   j 5b\n"
        "6:  sub a4, a4, t5\n   beqz a1, 10f\n   sd t5, 0(a1)\n"
        "10: beqz a2, 11f\n   sd a4, 0(a2)\n"
        "11: li a0, 1\n" PW_ASM_RET
        "7:  li a0, 0\n" PW_ASM_RET
        "8:  li t0, 1\n   sw t0, 24(a0)\n"
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_string)

        /* pwire_pod_values(a0=pod, a1=v) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_values)
        "beqz a0, 9f\n   beqz a1, 9f\n   lwu t0, 0(a0)\n   lwu t1, 4(a0)\n   ld t2, 8(a0)\n"
        "li t3, 19\n   beq t0, t3, 1f\n   li t4, 1\n   sw t0, 0(a1)\n   sw zero, 4(a1)\n"
        "sw t1, 8(a1)\n   sw t4, 12(a1)\n   sd t2, 16(a1)\n   li a0, 0\n" PW_ASM_RET
        "1:  beqz t2, 9f\n   li t3, 16\n   bltu t1, t3, 9f\n   lwu t4, 0(t2)\n   lwu t5, 8(t2)\n"
        "lwu t6, 12(t2)\n   addi t1, t1, -16\n   beqz t5, 9f\n   bgtu t5, t1, 9f\n   divuw a2, t1, t5\n"
        "addi t2, t2, 16\n   sw t6, 0(a1)\n   sw t4, 4(a1)\n   sw t5, 8(a1)\n   sw a2, 12(a1)\n   sd t2, 16(a1)\n   li a0, 0\n" PW_ASM_RET
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_values)

        /* pwire_pod_value32(a0=v, a1=k, a2=word) -> u32 */
        PW_ASM_FUNC(pwire_pod_value32)
        "beqz a0, 9f\n   ld t0, 16(a0)\n   beqz t0, 9f\n   lwu t1, 8(a0)\n   lwu t2, 12(a0)\n"
        "slli a1, a1, 32\n   srli a1, a1, 32\n   bgeu a1, t2, 9f\n   slli a2, a2, 32\n   srli a2, a2, 32\n"
        "addi t3, a2, 1\n   slli t3, t3, 2\n   bltu t1, t3, 9f\n   mul t4, a1, t1\n   slli t5, a2, 2\n"
        "add t4, t4, t5\n   add t0, t0, t4\n   lw a0, 0(t0)\n" PW_ASM_RET
        "9:  li a0, 0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_value32)

        /* pwire_pod_allows(a0=v, a1=x) -> 1 / 0 */
        PW_ASM_FUNC(pwire_pod_allows)
        "beqz a0, 9f\n   slli a1, a1, 32\n   srli a1, a1, 32\n   lwu t0, 4(a0)\n   lwu t1, 8(a0)\n"
        "lwu t2, 12(a0)\n   ld t3, 16(a0)\n   beqz t0, 1f\n   li t4, 3\n   beq t0, t4, 2f\n"
        "li t4, 1\n   beq t0, t4, 5f\n   li t4, 2\n   beq t0, t4, 5f\n   li t4, 4\n   bne t0, t4, 9f\n"
        "li t5, 0\n   beqz t2, 6f\n   li t4, 4\n   bltu t1, t4, 6f\n   beqz t3, 6f\n   lwu t5, 0(t3)\n"
        "6:  and t5, t5, a1\n   xor t5, t5, a1\n   seqz a0, t5\n" PW_ASM_RET
        "1:  li t5, 0\n   beqz t2, 7f\n   li t4, 4\n   bltu t1, t4, 7f\n   beqz t3, 7f\n   lwu t5, 0(t3)\n"
        "7:  xor t5, t5, a1\n   seqz a0, t5\n" PW_ASM_RET
        "2:  beqz t2, 9f\n   li t4, 4\n   bltu t1, t4, 8f\n   beqz t3, 8f\n   li t4, 1\n   sltu t5, t4, t2\n"
        "mul t6, t5, t1\n   add t3, t3, t6\n"
        "3:  bgeu t5, t2, 9f\n   lwu t6, 0(t3)\n   beq t6, a1, 4f\n   add t3, t3, t1\n   addi t5, t5, 1\n   j 3b\n"
        "4:  li a0, 1\n" PW_ASM_RET
        "5:  li t4, 3\n   bltu t2, t4, 9f\n   li t4, 4\n   bltu t1, t4, 8f\n   beqz t3, 8f\n"
        "add t5, t3, t1\n   lwu t6, 0(t5)\n   bgtu t6, a1, 9f\n   add t5, t5, t1\n   lwu t6, 0(t5)\n"
        "sltu a0, t6, a1\n   xori a0, a0, 1\n" PW_ASM_RET
        "8:  seqz a0, a1\n" PW_ASM_RET
        "9:  li a0, 0\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_allows)

        /* pwire_pod_object(a0=pod, a1=o) -> 0 / -1 */
        PW_ASM_FUNC(pwire_pod_object)
        "beqz a0, 9f\n   beqz a1, 9f\n   lwu t0, 0(a0)\n   lwu t1, 4(a0)\n   ld t2, 8(a0)\n"
        "beqz t2, 9f\n   li t3, 15\n   bne t0, t3, 9f\n   li t3, 8\n   bltu t1, t3, 9f\n"
        "lwu t4, 0(t2)\n   lwu t5, 4(t2)\n   addi t2, t2, 8\n   addi t1, t1, -8\n"
        "sw t4, 0(a1)\n   sw t5, 4(a1)\n   sd t2, 8(a1)\n   sd t1, 16(a1)\n   sd zero, 24(a1)\n   li a0, 0\n" PW_ASM_RET
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_object)

        /* pwire_pod_prop(a0=o, a1=key, a2=value) -> 1 prop / 0 end / -1 err */
        PW_ASM_FUNC(pwire_pod_prop)
        "beqz a0, 9f\n   beqz a1, 9f\n   beqz a2, 9f\n   ld t0, 16(a0)\n   ld t1, 24(a0)\n"
        "bltz t1, 9f\n   bge t1, t0, 8f\n   ld t2, 8(a0)\n   beqz t2, 9f\n"
        "sub t3, t0, t1\n   li t4, 8\n   blt t3, t4, 9f\n   add t4, t2, t1\n   lwu t5, 0(t4)\n   sw t5, 0(a1)\n"
        "addi t1, t1, 8\n   sd t1, 24(a0)\n   addi t4, t4, 8\n   addi t3, t3, -8\n   li t5, 8\n   blt t3, t5, 9f\n"
        "lwu t5, 0(t4)\n   lwu t6, 4(t4)\n   addi a3, t3, -8\n   bgtu t5, a3, 9f\n"
        "sw t6, 0(a2)\n   sw t5, 4(a2)\n   addi t4, t4, 8\n   sd t4, 8(a2)\n"
        "addi t5, t5, 7\n   andi t5, t5, -8\n   addi t5, t5, 8\n"
        "bgeu t5, t3, 1f\n   add t1, t1, t5\n   sd t1, 24(a0)\n   li a0, 1\n" PW_ASM_RET
        "1:  sd t0, 24(a0)\n   li a0, 1\n" PW_ASM_RET
        "8:  li a0, 0\n" PW_ASM_RET
        "9:  li a0, -1\n" PW_ASM_RET
        PW_ASM_END(pwire_pod_prop)
);

#endif

#endif /* CAST_PWIRE_POD_ASM_IMPLEMENTATION */
