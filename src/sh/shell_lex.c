/*
        POSIX / Moonwater-compatible shell core primitives in pure assembly:
          - sh_lex_line    : bounded zero-table tokenizer (all 22 OP_* operators, quotes, escapes, comments)
          - sh_unquote     : POSIX single/double/backslash quote removal
          - sh_valid_name  : POSIX identifier validator [A-Za-z_][A-Za-z0-9_]*
          - sh_param_name  : POSIX/Bash scalar parameter validator (@*#?$-!, digits, or identifier)
          - sh_escaped_end : escaped quote-run scanner (NUL-terminated, skips \x pairs)
          - sh_assign_split: NAME=... (1) / NAME+=... (2) assignment detector

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_SHELL_LEX_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_SHELL_LEX_H
#define MOONWATER_SH_SHELL_LEX_H

#if defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT32_TYPE__ sh_u32;
typedef __INT32_TYPE__  sh_i32;
typedef __UINT64_TYPE__ sh_u64;
#else
#include <stdint.h>
typedef uint32_t sh_u32;
typedef int32_t  sh_i32;
typedef uint64_t sh_u64;
#endif

#define SH_LEX_END       0u
#define SH_LEX_WORD      1u
#define SH_LEX_OPERATOR  2u

/* Exact Moonwater OP_* numbering (longest-match first) */
#define SH_OP_AND_IF     1u  /* &&  */
#define SH_OP_OR_IF      2u  /* ||  */
#define SH_OP_DSEMI      3u  /* ;;  */
#define SH_OP_DLESS      4u  /* <<  */
#define SH_OP_DGREAT     5u  /* >>  */
#define SH_OP_LESSAND    6u  /* <&  */
#define SH_OP_GREATAND   7u  /* >&  */
#define SH_OP_LESSGREAT  8u  /* <>  */
#define SH_OP_CLOBBER    9u  /* >|  */
#define SH_OP_SEMI       10u /* ;   */
#define SH_OP_PIPE       11u /* |   */
#define SH_OP_AMP        12u /* &   */
#define SH_OP_LESS       13u /* <   */
#define SH_OP_GREAT      14u /* >   */
#define SH_OP_LPAREN     15u /* (   */
#define SH_OP_RPAREN     16u /* )   */
#define SH_OP_ANDGREAT   17u /* &>  */
#define SH_OP_ANDDGREAT  18u /* &>> */
#define SH_OP_HERESTRING 19u /* <<< */
#define SH_OP_PIPEAND    20u /* |&  */
#define SH_OP_SEMIAND    21u /* ;&  */
#define SH_OP_DSEMIAND   22u /* ;;& */

/* 32 bytes — byte-for-byte identical to Moonwater's lex_token */
struct sh_token {
        sh_u32      kind;   /* +0  SH_LEX_WORD / SH_LEX_OPERATOR */
        sh_u32      op;     /* +4  SH_OP_* (0 for words) */
        const char *text;   /* +8  borrowed pointer into src */
        sh_u64      length; /* +16 byte length */
        sh_u64      at;     /* +24 byte offset from src start */
};

#ifdef offset_of
#define SH_LEX_OFFSET(T, M) offset_of(T, M)
#else
#define SH_LEX_OFFSET(T, M) ((sh_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct sh_token) == 32, "sh_token must be 32 bytes");
_Static_assert(SH_LEX_OFFSET(struct sh_token, kind)   == 0,  "sh_token.kind offset");
_Static_assert(SH_LEX_OFFSET(struct sh_token, op)     == 4,  "sh_token.op offset");
_Static_assert(SH_LEX_OFFSET(struct sh_token, text)   == 8,  "sh_token.text offset");
_Static_assert(SH_LEX_OFFSET(struct sh_token, length) == 16, "sh_token.length offset");
_Static_assert(SH_LEX_OFFSET(struct sh_token, at)     == 24, "sh_token.at offset");

/* When included alongside src/sh/lex.c, prove 1:1 ABI parity with lex_token & OP_* */
#if defined(LEX_WORD) && defined(OP_DSEMIAND)
_Static_assert(sizeof(struct sh_token) == sizeof(lex_token), "sh_token != lex_token size");
_Static_assert(SH_LEX_END == LEX_END && SH_LEX_WORD == LEX_WORD && SH_LEX_OPERATOR == LEX_OPERATOR,
               "SH_LEX_* mismatch");
_Static_assert(SH_OP_AND_IF == OP_AND_IF && SH_OP_DSEMIAND == OP_DSEMIAND &&
               SH_OP_HERESTRING == OP_HERESTRING && SH_OP_PIPEAND == OP_PIPEAND,
               "SH_OP_* mismatch");
#endif

sh_i32      sh_lex_line(const char *src, sh_u64 len, struct sh_token *out, sh_u32 cap);
sh_u64      sh_unquote(const char *src, sh_u64 len, char *dst, sh_u64 dst_cap);
sh_i32      sh_valid_name(const char *name, sh_u64 len);
sh_i32      sh_param_name(const char *name, sh_u64 len);
const char *sh_escaped_end(const char *at, sh_u32 quote);
sh_i32      sh_assign_split(const char *word, sh_u64 len, sh_u64 *name_len_out);

#endif /* MOONWATER_SH_SHELL_LEX_H */

#ifdef SH_SHELL_LEX_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_LEX_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_LEX_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_LEX_RISCV64 1
# else
#  error "src/sh/shell_lex.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_LEX_ARM64 1
# elif defined(X64) && X64
#  define SH_LEX_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_LEX_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_ASM_SECTION   ASM_SECTION
# define SH_ASM_FUNC(n)   ASM_FUNC(n)
# define SH_ASM_END(n)    ASM_END(n)
# define SH_ASM_RET       ASM_RET
#else
# define SH_ASM_SECTION   ".text\n"
# define SH_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define SH_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_ASM_END(n)   ""
# elif defined(_WIN32)
#  define SH_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_ASM_END(n)   ""
# else
#  if defined(SH_LEX_ARM64)
#   define SH_ASM_FTYPE   "%function"
#  else
#   define SH_ASM_FTYPE   "@function"
#  endif
#  define SH_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_ASM_FTYPE "\n" #n ":\n"
#  define SH_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SH_LEX_ARM64)

__asm__(
        SH_ASM_SECTION

        /* sh_lex_line(x0=src, x1=len, x2=out, w3=cap) */
        SH_ASM_FUNC(sh_lex_line)
        "cbz x0, 30f\n   cbz x2, 30f\n   cbz w3, 30f\n   mov x4, #0\n   mov w5, #0\n"
        "movz x8, #0x2600\n   movk x8, #0x03c5, lsl #32\n   movk x8, #0x5800, lsl #48\n"
        "movz x9, #0x2600\n   movk x9, #0x0001, lsl #32\n"
        "1:  cmp x4, x1\n   b.hs 28f\n   ldrb w7, [x0, x4]\n   cmp w7, #32\n   b.hi 2f\n"
        "lsr x13, x9, x7\n   tbz x13, #0, 2f\n   add x4, x4, #1\n   b 1b\n"
        "2:  cmp w7, #35\n   b.eq 28f\n   mov x6, x4\n   cmp w7, #64\n   b.lo 32f\n"
        "cmp w7, #124\n   b.eq 15f\n   cmp w7, #92\n   b.eq 6f\n   b 5f\n"
        "32: lsr x13, x8, x7\n   tbz x13, #0, 5f\n   cmp w7, #39\n   b.eq 7f\n"
        "cmp w7, #34\n   b.eq 9f\n   cmp w7, #38\n   b.eq 13f\n   cmp w7, #59\n   b.eq 17f\n"
        "cmp w7, #60\n   b.eq 19f\n   cmp w7, #62\n   b.eq 22f\n   cmp w7, #40\n   b.eq 25f\n   b 26f\n"
        "5:  add x4, x4, #1\n"
        "3:  cmp x4, x1\n   b.hs 12f\n   ldrb w7, [x0, x4]\n   cmp w7, #64\n   b.lo 4f\n"
        "cmp w7, #92\n   b.eq 6f\n   cmp w7, #124\n   b.ne 5b\n   b 12f\n"
        "4:  lsr x13, x8, x7\n   tbz x13, #0, 5b\n   cmp w7, #39\n   b.eq 7f\n"
        "cmp w7, #34\n   b.eq 9f\n   b 12f\n"
        "6:  add x4, x4, #1\n   cmp x4, x1\n   b.hs 12f\n   add x4, x4, #1\n   b 3b\n"
        "7:  add x4, x4, #1\n"
        "8:  cmp x4, x1\n   b.hs 29f\n   ldrb w7, [x0, x4]\n   add x4, x4, #1\n   cmp w7, #39\n   b.ne 8b\n   b 3b\n"
        "9:  add x4, x4, #1\n"
        "10: cmp x4, x1\n   b.hs 29f\n   ldrb w7, [x0, x4]\n   cmp w7, #34\n   b.eq 11f\n"
        "add x4, x4, #1\n   cmp w7, #92\n   b.ne 10b\n   cmp x4, x1\n   b.hs 29f\n   add x4, x4, #1\n   b 10b\n"
        "11: add x4, x4, #1\n   b 3b\n"
        "12: mov w11, #1\n   mov w12, #0\n   add x13, x0, x6\n   sub x10, x4, x6\n"
        "stp w11, w12, [x2]\n   stp x13, x10, [x2, #8]\n   str x6, [x2, #24]\n"
        "add x2, x2, #32\n   add w5, w5, #1\n   cmp w5, w3\n   b.lo 1b\n   b 28f\n"
        "13: sub x10, x1, x4\n   mov w12, #12\n   mov x13, #1\n   cmp x10, #2\n   b.lo 27f\n"
        "add x11, x4, #1\n   ldrb w11, [x0, x11]\n   cmp w11, #38\n   b.ne 14f\n   mov w12, #1\n   mov x13, #2\n   b 27f\n"
        "14: cmp w11, #62\n   b.ne 27f\n   mov w12, #17\n   mov x13, #2\n   cmp x10, #3\n   b.lo 27f\n"
        "add x11, x4, #2\n   ldrb w11, [x0, x11]\n   cmp w11, #62\n   b.ne 27f\n   mov w12, #18\n   mov x13, #3\n   b 27f\n"
        "15: sub x10, x1, x4\n   mov w12, #11\n   mov x13, #1\n   cmp x10, #2\n   b.lo 27f\n"
        "add x11, x4, #1\n   ldrb w11, [x0, x11]\n   cmp w11, #124\n   b.ne 16f\n   mov w12, #2\n   mov x13, #2\n   b 27f\n"
        "16: cmp w11, #38\n   b.ne 27f\n   mov w12, #20\n   mov x13, #2\n   b 27f\n"
        "17: sub x10, x1, x4\n   mov w12, #10\n   mov x13, #1\n   cmp x10, #2\n   b.lo 27f\n"
        "add x11, x4, #1\n   ldrb w11, [x0, x11]\n   cmp w11, #59\n   b.ne 18f\n   mov w12, #3\n   mov x13, #2\n"
        "cmp x10, #3\n   b.lo 27f\n   add x11, x4, #2\n   ldrb w11, [x0, x11]\n   cmp w11, #38\n   b.ne 27f\n"
        "mov w12, #22\n   mov x13, #3\n   b 27f\n"
        "18: cmp w11, #38\n   b.ne 27f\n   mov w12, #21\n   mov x13, #2\n   b 27f\n"
        "19: sub x10, x1, x4\n   mov w12, #13\n   mov x13, #1\n   cmp x10, #2\n   b.lo 27f\n"
        "add x11, x4, #1\n   ldrb w11, [x0, x11]\n   cmp w11, #60\n   b.ne 20f\n   mov w12, #4\n   mov x13, #2\n"
        "cmp x10, #3\n   b.lo 27f\n   add x11, x4, #2\n   ldrb w11, [x0, x11]\n   cmp w11, #60\n   b.ne 27f\n"
        "mov w12, #19\n   mov x13, #3\n   b 27f\n"
        "20: cmp w11, #38\n   b.ne 21f\n   mov w12, #6\n   mov x13, #2\n   b 27f\n"
        "21: cmp w11, #62\n   b.ne 27f\n   mov w12, #8\n   mov x13, #2\n   b 27f\n"
        "22: sub x10, x1, x4\n   mov w12, #14\n   mov x13, #1\n   cmp x10, #2\n   b.lo 27f\n"
        "add x11, x4, #1\n   ldrb w11, [x0, x11]\n   cmp w11, #62\n   b.ne 23f\n   mov w12, #5\n   mov x13, #2\n   b 27f\n"
        "23: cmp w11, #38\n   b.ne 24f\n   mov w12, #7\n   mov x13, #2\n   b 27f\n"
        "24: cmp w11, #124\n   b.ne 27f\n   mov w12, #9\n   mov x13, #2\n   b 27f\n"
        "25: mov w12, #15\n   mov x13, #1\n   b 27f\n"
        "26: mov w12, #16\n   mov x13, #1\n"
        "27: mov w11, #2\n   add x10, x0, x6\n   stp w11, w12, [x2]\n   stp x10, x13, [x2, #8]\n"
        "str x6, [x2, #24]\n   add x2, x2, #32\n   add x4, x4, x13\n   add w5, w5, #1\n   cmp w5, w3\n   b.lo 1b\n"
        "28: mov w0, w5\n" SH_ASM_RET
        "29: mov w0, #-1\n" SH_ASM_RET
        "30: mov w0, #0\n" SH_ASM_RET
        SH_ASM_END(sh_lex_line)

        /* sh_unquote(x0=src, x1=len, x2=dst, x3=dst_cap) */
        SH_ASM_FUNC(sh_unquote)
        "cbz x2, 12f\n   cbz x3, 12f\n   cbz x0, 11f\n   cbz x1, 11f\n"
        "mov x5, x2\n   add x1, x0, x1\n   add x3, x2, x3\n   sub x3, x3, #1\n"
        "1:  cmp x0, x1\n   b.hs 10f\n   cmp x2, x3\n   b.hs 10f\n   ldrb w7, [x0], #1\n"
        "cmp w7, #92\n   b.eq 2f\n   cmp w7, #39\n   b.eq 3f\n   cmp w7, #34\n   b.eq 4f\n   strb w7, [x2], #1\n   b 1b\n"
        "2:  cmp x0, x1\n   b.hs 10f\n   ldrb w7, [x0], #1\n   strb w7, [x2], #1\n   b 1b\n"
        "3:  cmp x0, x1\n   b.hs 10f\n   cmp x2, x3\n   b.hs 10f\n   ldrb w7, [x0], #1\n"
        "cmp w7, #39\n   b.eq 1b\n   strb w7, [x2], #1\n   b 3b\n"
        "4:  cmp x0, x1\n   b.hs 10f\n   cmp x2, x3\n   b.hs 10f\n   ldrb w7, [x0], #1\n"
        "cmp w7, #34\n   b.eq 1b\n   cmp w7, #92\n   b.eq 5f\n"
        "6:  strb w7, [x2], #1\n   b 4b\n"
        "5:  cmp x0, x1\n   b.hs 6b\n   ldrb w8, [x0]\n   cmp w8, #34\n   b.eq 7f\n   cmp w8, #92\n   b.eq 7f\n"
        "cmp w8, #36\n   b.eq 7f\n   cmp w8, #96\n   b.eq 7f\n   cmp w8, #10\n   b.ne 6b\n"
        "7:  add x0, x0, #1\n   strb w8, [x2], #1\n   b 4b\n"
        "10: strb wzr, [x2]\n   sub x0, x2, x5\n" SH_ASM_RET
        "11: strb wzr, [x2]\n"
        "12: mov x0, #0\n" SH_ASM_RET
        SH_ASM_END(sh_unquote)

        /* sh_valid_name(x0=name, x1=len) -> [A-Za-z_][A-Za-z0-9_]* */
        SH_ASM_FUNC(sh_valid_name)
        "cbz x0, 3f\n   cbz x1, 3f\n   ldrb w2, [x0]\n   orr w3, w2, #32\n   sub w3, w3, #97\n"
        "cmp w3, #25\n   b.ls 1f\n   cmp w2, #95\n   b.ne 3f\n"
        "1:  mov x4, #1\n"
        "2:  cmp x4, x1\n   b.hs 4f\n   ldrb w2, [x0, x4]\n   add x4, x4, #1\n"
        "orr w3, w2, #32\n   sub w3, w3, #97\n   cmp w3, #25\n   b.ls 2b\n"
        "sub w3, w2, #48\n   cmp w3, #9\n   b.ls 2b\n   cmp w2, #95\n   b.eq 2b\n"
        "3:  mov w0, #0\n" SH_ASM_RET
        "4:  mov w0, #1\n" SH_ASM_RET
        SH_ASM_END(sh_valid_name)

        /* sh_param_name(x0=name, x1=len) -> @*#?$-! | [0-9]+ | [A-Za-z_][A-Za-z0-9_]* */
        SH_ASM_FUNC(sh_param_name)
        "cbz x0, 5f\n   cbz x1, 5f\n   ldrb w2, [x0]\n   cmp x1, #1\n   b.ne 1f\n"
        "sub w3, w2, #33\n   cmp w3, #31\n   b.hi 1f\n   movz w4, #0x120d\n   movk w4, #0xc000, lsl #16\n"
        "lsr w4, w4, w3\n   tbnz w4, #0, 6f\n"
        "1:  sub w3, w2, #48\n   cmp w3, #9\n   b.hi 3f\n   mov x4, #1\n"
        "2:  cmp x4, x1\n   b.hs 6f\n   ldrb w2, [x0, x4]\n   add x4, x4, #1\n   sub w3, w2, #48\n   cmp w3, #9\n   b.ls 2b\n   b 5f\n"
        "3:  orr w3, w2, #32\n   sub w3, w3, #97\n   cmp w3, #25\n   b.ls 4f\n   cmp w2, #95\n   b.ne 5f\n"
        "4:  mov x4, #1\n"
        "7:  cmp x4, x1\n   b.hs 6f\n   ldrb w2, [x0, x4]\n   add x4, x4, #1\n"
        "orr w3, w2, #32\n   sub w3, w3, #97\n   cmp w3, #25\n   b.ls 7b\n"
        "sub w3, w2, #48\n   cmp w3, #9\n   b.ls 7b\n   cmp w2, #95\n   b.eq 7b\n"
        "5:  mov w0, #0\n" SH_ASM_RET
        "6:  mov w0, #1\n" SH_ASM_RET
        SH_ASM_END(sh_param_name)

        /* sh_escaped_end(x0=at, w1=quote) -> ptr to quote or NUL, skipping \x */
        SH_ASM_FUNC(sh_escaped_end)
        "cbz x0, 3f\n   and w1, w1, #0xff\n"
        "1:  ldrb w2, [x0]\n   cbz w2, 3f\n   cmp w2, w1\n   b.eq 3f\n   add x0, x0, #1\n"
        "cmp w2, #92\n   b.ne 1b\n   ldrb w2, [x0]\n   cbz w2, 3f\n   add x0, x0, #1\n   b 1b\n"
        "3:  " SH_ASM_RET
        SH_ASM_END(sh_escaped_end)

        /* sh_assign_split(x0=word, x1=len, x2=name_len_out) -> 1 ('='), 2 ('+='), 0 */
        SH_ASM_FUNC(sh_assign_split)
        "cbz x0, 6f\n   cbz x1, 6f\n   ldrb w3, [x0]\n   orr w4, w3, #32\n   sub w4, w4, #97\n"
        "cmp w4, #25\n   b.ls 1f\n   cmp w3, #95\n   b.ne 6f\n"
        "1:  mov x5, #1\n"
        "2:  cmp x5, x1\n   b.hs 6f\n   ldrb w3, [x0, x5]\n"
        "orr w4, w3, #32\n   sub w4, w4, #97\n   cmp w4, #25\n   b.ls 3f\n"
        "sub w4, w3, #48\n   cmp w4, #9\n   b.ls 3f\n   cmp w3, #95\n   b.ne 4f\n"
        "3:  add x5, x5, #1\n   b 2b\n"
        "4:  cmp w3, #61\n   b.eq 5f\n   cmp w3, #43\n   b.ne 6f\n   add x4, x5, #1\n   cmp x4, x1\n   b.hs 6f\n"
        "ldrb w3, [x0, x4]\n   cmp w3, #61\n   b.ne 6f\n   cbz x2, 7f\n   str x5, [x2]\n"
        "7:  mov w0, #2\n" SH_ASM_RET
        "5:  cbz x2, 8f\n   str x5, [x2]\n"
        "8:  mov w0, #1\n" SH_ASM_RET
        "6:  mov w0, #0\n" SH_ASM_RET
        SH_ASM_END(sh_assign_split)
);

#elif defined(SH_LEX_X64)

#if defined(_WIN32)
#define SH_LEX_LOAD4 "mov r12, rcx\n   mov r13, rdx\n   mov r10, r8\n   mov r11d, r9d\n"
#define SH_UQ_LOAD4  "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
#define SH_LOAD2     "mov r8, rcx\n   mov r9, rdx\n"
#define SH_LOAD3     "mov r8, rcx\n   mov r9, rdx\n   mov r10, r8\n" /* unused: explicit below */
#define SH_A_R1 rcx
#define SH_A_R2 rdx
#define SH_A_R3 r8
#else
#define SH_LEX_LOAD4 "mov r12, rdi\n   mov r13, rsi\n   mov r10, rdx\n   mov r11d, ecx\n"
#define SH_UQ_LOAD4  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
#define SH_LOAD2     "mov r8, rdi\n   mov r9, rsi\n"
#define SH_A_R1 rdi
#define SH_A_R2 rsi
#define SH_A_R3 rdx
#endif

__asm__(
        SH_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* sh_lex_line(src, len, out, cap) */
        SH_ASM_FUNC(sh_lex_line)
        "push rbx\n   push r12\n   push r13\n" SH_LEX_LOAD4
        "test r12, r12\n   jz 40f\n   test r10, r10\n   jz 40f\n   test r11d, r11d\n   jz 40f\n"
        "xor ecx, ecx\n   xor r8d, r8d\n   mov rbx, 0x580003c500002600\n"
        "1:  mov rdx, 0x0000000100002600\n"
        "2:  cmp rcx, r13\n   jae 38f\n   movzx eax, byte ptr [r12 + rcx]\n   cmp eax, 32\n   ja 3f\n"
        "bt rdx, rax\n   jnc 3f\n   inc rcx\n   jmp 2b\n"
        "3:  cmp eax, 35\n   je 38f\n   mov r9, rcx\n   cmp eax, 64\n   jb 42f\n"
        "cmp eax, 124\n   je 18f\n   cmp eax, 92\n   je 7f\n   jmp 6f\n"
        "42: bt rbx, rax\n   jnc 6f\n   cmp eax, 39\n   je 8f\n   cmp eax, 34\n   je 10f\n"
        "cmp eax, 38\n   je 14f\n   cmp eax, 59\n   je 21f\n   cmp eax, 60\n   je 25f\n"
        "cmp eax, 62\n   je 30f\n   cmp eax, 40\n   je 35f\n   jmp 36f\n"
        "4:  cmp rcx, r13\n   jae 13f\n   movzx eax, byte ptr [r12 + rcx]\n   cmp eax, 64\n   jb 5f\n"
        "cmp eax, 92\n   je 7f\n   cmp eax, 124\n   je 13f\n"
        "6:  inc rcx\n   jmp 4b\n"
        "5:  bt rbx, rax\n   jnc 6b\n   cmp eax, 39\n   je 8f\n   cmp eax, 34\n   je 10f\n   jmp 13f\n"
        "7:  inc rcx\n   cmp rcx, r13\n   jae 13f\n   inc rcx\n   jmp 4b\n"
        "8:  inc rcx\n"
        "9:  cmp rcx, r13\n   jae 39f\n   movzx eax, byte ptr [r12 + rcx]\n   inc rcx\n   cmp eax, 39\n   jne 9b\n   jmp 4b\n"
        "10: inc rcx\n"
        "11: cmp rcx, r13\n   jae 39f\n   movzx eax, byte ptr [r12 + rcx]\n   cmp eax, 34\n   je 12f\n"
        "inc rcx\n   cmp eax, 92\n   jne 11b\n   cmp rcx, r13\n   jae 39f\n   inc rcx\n   jmp 11b\n"
        "12: inc rcx\n   jmp 4b\n"
        "13: mov dword ptr [r10], 1\n   mov dword ptr [r10 + 4], 0\n   lea rax, [r12 + r9]\n   mov [r10 + 8], rax\n"
        "mov rax, rcx\n   sub rax, r9\n   mov [r10 + 16], rax\n   mov [r10 + 24], r9\n"
        "add r10, 32\n   inc r8d\n   cmp r8d, r11d\n   jb 1b\n   jmp 38f\n"
        "14: mov rax, r13\n   sub rax, rcx\n   mov edx, 1\n   cmp rax, 2\n   jb 17f\n"
        "cmp byte ptr [r12 + rcx + 1], 38\n   je 16f\n   cmp byte ptr [r12 + rcx + 1], 62\n   jne 17f\n"
        "cmp rax, 3\n   jb 15f\n   cmp byte ptr [r12 + rcx + 2], 62\n   jne 15f\n   mov eax, 18\n   mov edx, 3\n   jmp 37f\n"
        "15: mov eax, 17\n   mov edx, 2\n   jmp 37f\n"
        "16: mov eax, 1\n   mov edx, 2\n   jmp 37f\n"
        "17: mov eax, 12\n   jmp 37f\n"
        "18: mov rax, r13\n   sub rax, rcx\n   mov edx, 1\n   cmp rax, 2\n   jb 20f\n"
        "cmp byte ptr [r12 + rcx + 1], 124\n   je 19f\n   cmp byte ptr [r12 + rcx + 1], 38\n   jne 20f\n"
        "mov eax, 20\n   mov edx, 2\n   jmp 37f\n"
        "19: mov eax, 2\n   mov edx, 2\n   jmp 37f\n"
        "20: mov eax, 11\n   jmp 37f\n"
        "21: mov rax, r13\n   sub rax, rcx\n   mov edx, 1\n   cmp rax, 2\n   jb 24f\n"
        "cmp byte ptr [r12 + rcx + 1], 59\n   je 22f\n   cmp byte ptr [r12 + rcx + 1], 38\n   jne 24f\n"
        "mov eax, 21\n   mov edx, 2\n   jmp 37f\n"
        "22: cmp rax, 3\n   jb 23f\n   cmp byte ptr [r12 + rcx + 2], 38\n   jne 23f\n   mov eax, 22\n   mov edx, 3\n   jmp 37f\n"
        "23: mov eax, 3\n   mov edx, 2\n   jmp 37f\n"
        "24: mov eax, 10\n   jmp 37f\n"
        "25: mov rax, r13\n   sub rax, rcx\n   mov edx, 1\n   cmp rax, 2\n   jb 29f\n"
        "cmp byte ptr [r12 + rcx + 1], 60\n   je 27f\n   cmp byte ptr [r12 + rcx + 1], 38\n   je 26f\n"
        "cmp byte ptr [r12 + rcx + 1], 62\n   jne 29f\n   mov eax, 8\n   mov edx, 2\n   jmp 37f\n"
        "26: mov eax, 6\n   mov edx, 2\n   jmp 37f\n"
        "27: cmp rax, 3\n   jb 28f\n   cmp byte ptr [r12 + rcx + 2], 60\n   jne 28f\n   mov eax, 19\n   mov edx, 3\n   jmp 37f\n"
        "28: mov eax, 4\n   mov edx, 2\n   jmp 37f\n"
        "29: mov eax, 13\n   jmp 37f\n"
        "30: mov rax, r13\n   sub rax, rcx\n   mov edx, 1\n   cmp rax, 2\n   jb 34f\n"
        "cmp byte ptr [r12 + rcx + 1], 62\n   je 33f\n   cmp byte ptr [r12 + rcx + 1], 38\n   je 32f\n"
        "cmp byte ptr [r12 + rcx + 1], 124\n   jne 34f\n   mov eax, 9\n   mov edx, 2\n   jmp 37f\n"
        "32: mov eax, 7\n   mov edx, 2\n   jmp 37f\n"
        "33: mov eax, 5\n   mov edx, 2\n   jmp 37f\n"
        "34: mov eax, 14\n   jmp 37f\n"
        "35: mov eax, 15\n   mov edx, 1\n   jmp 37f\n"
        "36: mov eax, 16\n   mov edx, 1\n"
        "37: mov dword ptr [r10], 2\n   mov dword ptr [r10 + 4], eax\n   lea rax, [r12 + r9]\n   mov [r10 + 8], rax\n"
        "mov [r10 + 16], rdx\n   mov [r10 + 24], r9\n   add r10, 32\n   add rcx, rdx\n   inc r8d\n   cmp r8d, r11d\n   jb 1b\n"
        "38: mov eax, r8d\n   pop r13\n   pop r12\n   pop rbx\n" SH_ASM_RET
        "39: mov eax, -1\n   pop r13\n   pop r12\n   pop rbx\n" SH_ASM_RET
        "40: xor eax, eax\n   pop r13\n   pop r12\n   pop rbx\n" SH_ASM_RET
        SH_ASM_END(sh_lex_line)

        /* sh_unquote(src, len, dst, dst_cap) */
        SH_ASM_FUNC(sh_unquote)
        SH_UQ_LOAD4
        "test r10, r10\n   jz 12f\n   test r11, r11\n   jz 12f\n   test r8, r8\n   jz 11f\n   test r9, r9\n   jz 11f\n"
        "mov rax, r10\n   add r9, r8\n   lea r11, [r10 + r11 - 1]\n"
        "1:  cmp r8, r9\n   jae 10f\n   cmp r10, r11\n   jae 10f\n   movzx ecx, byte ptr [r8]\n   inc r8\n"
        "cmp ecx, 92\n   je 2f\n   cmp ecx, 39\n   je 3f\n   cmp ecx, 34\n   je 4f\n   mov byte ptr [r10], cl\n   inc r10\n   jmp 1b\n"
        "2:  cmp r8, r9\n   jae 10f\n   movzx ecx, byte ptr [r8]\n   inc r8\n   mov byte ptr [r10], cl\n   inc r10\n   jmp 1b\n"
        "3:  cmp r8, r9\n   jae 10f\n   cmp r10, r11\n   jae 10f\n   movzx ecx, byte ptr [r8]\n   inc r8\n"
        "cmp ecx, 39\n   je 1b\n   mov byte ptr [r10], cl\n   inc r10\n   jmp 3b\n"
        "4:  cmp r8, r9\n   jae 10f\n   cmp r10, r11\n   jae 10f\n   movzx ecx, byte ptr [r8]\n   inc r8\n"
        "cmp ecx, 34\n   je 1b\n   cmp ecx, 92\n   je 5f\n"
        "6:  mov byte ptr [r10], cl\n   inc r10\n   jmp 4b\n"
        "5:  cmp r8, r9\n   jae 6b\n   movzx edx, byte ptr [r8]\n   cmp edx, 34\n   je 7f\n   cmp edx, 92\n   je 7f\n"
        "cmp edx, 36\n   je 7f\n   cmp edx, 96\n   je 7f\n   cmp edx, 10\n   jne 6b\n"
        "7:  inc r8\n   mov byte ptr [r10], dl\n   inc r10\n   jmp 4b\n"
        "10: mov byte ptr [r10], 0\n   sub r10, rax\n   mov rax, r10\n" SH_ASM_RET
        "11: mov byte ptr [r10], 0\n"
        "12: xor eax, eax\n" SH_ASM_RET
        SH_ASM_END(sh_unquote)

        /* sh_valid_name(name, len) -> [A-Za-z_][A-Za-z0-9_]* */
        SH_ASM_FUNC(sh_valid_name)
        SH_LOAD2
        "test r8, r8\n   jz 3f\n   test r9, r9\n   jz 3f\n   movzx eax, byte ptr [r8]\n"
        "mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 1f\n   cmp eax, 95\n   jne 3f\n"
        "1:  mov r10, 1\n"
        "2:  cmp r10, r9\n   jae 4f\n   movzx eax, byte ptr [r8 + r10]\n   inc r10\n"
        "mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 2b\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 2b\n   cmp eax, 95\n   je 2b\n"
        "3:  xor eax, eax\n" SH_ASM_RET
        "4:  mov eax, 1\n" SH_ASM_RET
        SH_ASM_END(sh_valid_name)

        /* sh_param_name(name, len) -> @*#?$-! | [0-9]+ | [A-Za-z_][A-Za-z0-9_]* */
        SH_ASM_FUNC(sh_param_name)
        SH_LOAD2
        "test r8, r8\n   jz 5f\n   test r9, r9\n   jz 5f\n   movzx eax, byte ptr [r8]\n   cmp r9, 1\n   jne 1f\n"
        "lea ecx, [rax - 33]\n   cmp ecx, 31\n   ja 1f\n   mov r10d, 0xc000120d\n   bt r10d, ecx\n   jc 6f\n"
        "1:  lea ecx, [rax - 48]\n   cmp ecx, 9\n   ja 3f\n   mov r10, 1\n"
        "2:  cmp r10, r9\n   jae 6f\n   movzx eax, byte ptr [r8 + r10]\n   inc r10\n   sub eax, 48\n   cmp eax, 9\n   jbe 2b\n   jmp 5f\n"
        "3:  mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 4f\n   cmp eax, 95\n   jne 5f\n"
        "4:  mov r10, 1\n"
        "7:  cmp r10, r9\n   jae 6f\n   movzx eax, byte ptr [r8 + r10]\n   inc r10\n"
        "mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 7b\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 7b\n   cmp eax, 95\n   je 7b\n"
        "5:  xor eax, eax\n" SH_ASM_RET
        "6:  mov eax, 1\n" SH_ASM_RET
        SH_ASM_END(sh_param_name)

        /* sh_escaped_end(at, quote) -> ptr to quote or NUL, skipping \x */
        SH_ASM_FUNC(sh_escaped_end)
        SH_LOAD2
        "mov rax, r8\n   test rax, rax\n   jz 3f\n   movzx r9d, r9b\n"
        "1:  movzx ecx, byte ptr [rax]\n   test ecx, ecx\n   jz 3f\n   cmp ecx, r9d\n   je 3f\n   inc rax\n"
        "cmp ecx, 92\n   jne 1b\n   cmp byte ptr [rax], 0\n   je 3f\n   inc rax\n   jmp 1b\n"
        "3:  " SH_ASM_RET
        SH_ASM_END(sh_escaped_end)

        /* sh_assign_split(word, len, name_len_out) -> 1 ('='), 2 ('+='), 0 */
        SH_ASM_FUNC(sh_assign_split)
#if defined(_WIN32)
        "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
#else
        "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
#endif
        "test r8, r8\n   jz 6f\n   test r9, r9\n   jz 6f\n   movzx eax, byte ptr [r8]\n"
        "mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 1f\n   cmp eax, 95\n   jne 6f\n"
        "1:  mov r11, 1\n"
        "2:  cmp r11, r9\n   jae 6f\n   movzx eax, byte ptr [r8 + r11]\n"
        "mov ecx, eax\n   or ecx, 32\n   sub ecx, 97\n   cmp ecx, 25\n   jbe 3f\n"
        "lea ecx, [rax - 48]\n   cmp ecx, 9\n   jbe 3f\n   cmp eax, 95\n   jne 4f\n"
        "3:  inc r11\n   jmp 2b\n"
        "4:  cmp eax, 61\n   je 5f\n   cmp eax, 43\n   jne 6f\n   lea rcx, [r11 + 1]\n   cmp rcx, r9\n   jae 6f\n"
        "cmp byte ptr [r8 + rcx], 61\n   jne 6f\n   test r10, r10\n   jz 7f\n   mov [r10], r11\n"
        "7:  mov eax, 2\n" SH_ASM_RET
        "5:  test r10, r10\n   jz 8f\n   mov [r10], r11\n"
        "8:  mov eax, 1\n" SH_ASM_RET
        "6:  xor eax, eax\n" SH_ASM_RET
        SH_ASM_END(sh_assign_split)

        ".att_syntax prefix\n"
);

#elif defined(SH_LEX_RISCV64)

__asm__(
        SH_ASM_SECTION

        /* sh_lex_line(a0=src, a1=len, a2=out, a3=cap) */
        SH_ASM_FUNC(sh_lex_line)
        "slli a3, a3, 32\n   srli a3, a3, 32\n   beqz a0, 30f\n   beqz a2, 30f\n   beqz a3, 30f\n"
        "li a4, 0\n   li a5, 0\n   li t0, 0x580003c500002600\n   li t1, 0x0000000100002600\n"
        "1:  bgeu a4, a1, 28f\n   add t6, a0, a4\n   lbu a7, 0(t6)\n   li t2, 32\n   bgtu a7, t2, 2f\n"
        "srl t6, t1, a7\n   andi t6, t6, 1\n   beqz t6, 2f\n   addi a4, a4, 1\n   j 1b\n"
        "2:  li t2, 35\n   beq a7, t2, 28f\n   mv a6, a4\n   li t2, 64\n   bltu a7, t2, 32f\n"
        "li t2, 124\n   beq a7, t2, 15f\n   li t2, 92\n   beq a7, t2, 6f\n   j 5f\n"
        "32: srl t6, t0, a7\n   andi t6, t6, 1\n   beqz t6, 5f\n   li t2, 39\n   beq a7, t2, 7f\n"
        "li t2, 34\n   beq a7, t2, 9f\n   li t2, 38\n   beq a7, t2, 13f\n   li t2, 59\n   beq a7, t2, 17f\n"
        "li t2, 60\n   beq a7, t2, 19f\n   li t2, 62\n   beq a7, t2, 22f\n   li t2, 40\n   beq a7, t2, 25f\n   j 26f\n"
        "5:  addi a4, a4, 1\n"
        "3:  bgeu a4, a1, 12f\n   add t6, a0, a4\n   lbu a7, 0(t6)\n   li t2, 64\n   bltu a7, t2, 4f\n"
        "li t2, 92\n   beq a7, t2, 6f\n   li t2, 124\n   bne a7, t2, 5b\n   j 12f\n"
        "4:  srl t6, t0, a7\n   andi t6, t6, 1\n   beqz t6, 5b\n   li t2, 39\n   beq a7, t2, 7f\n"
        "li t2, 34\n   beq a7, t2, 9f\n   j 12f\n"
        "6:  addi a4, a4, 1\n   bgeu a4, a1, 12f\n   addi a4, a4, 1\n   j 3b\n"
        "7:  addi a4, a4, 1\n   li t2, 39\n"
        "8:  bgeu a4, a1, 29f\n   add t6, a0, a4\n   lbu a7, 0(t6)\n   addi a4, a4, 1\n   bne a7, t2, 8b\n   j 3b\n"
        "9:  addi a4, a4, 1\n   li t2, 34\n   li t3, 92\n"
        "10: bgeu a4, a1, 29f\n   add t6, a0, a4\n   lbu a7, 0(t6)\n   beq a7, t2, 11f\n"
        "addi a4, a4, 1\n   bne a7, t3, 10b\n   bgeu a4, a1, 29f\n   addi a4, a4, 1\n   j 10b\n"
        "11: addi a4, a4, 1\n   j 3b\n"
        "12: li t4, 1\n   sw t4, 0(a2)\n   sw zero, 4(a2)\n   add t6, a0, a6\n   sd t6, 8(a2)\n"
        "sub t5, a4, a6\n   sd t5, 16(a2)\n   sd a6, 24(a2)\n   addi a2, a2, 32\n   addi a5, a5, 1\n   bltu a5, a3, 1b\n   j 28f\n"
        "13: sub t2, a1, a4\n   li t4, 12\n   li t5, 1\n   li t6, 2\n   bltu t2, t6, 27f\n"
        "add t6, a0, a4\n   lbu t3, 1(t6)\n   li t6, 38\n   bne t3, t6, 14f\n   li t4, 1\n   li t5, 2\n   j 27f\n"
        "14: li t6, 62\n   bne t3, t6, 27f\n   li t4, 17\n   li t5, 2\n   li t6, 3\n   bltu t2, t6, 27f\n"
        "add t6, a0, a4\n   lbu t3, 2(t6)\n   li t6, 62\n   bne t3, t6, 27f\n   li t4, 18\n   li t5, 3\n   j 27f\n"
        "15: sub t2, a1, a4\n   li t4, 11\n   li t5, 1\n   li t6, 2\n   bltu t2, t6, 27f\n"
        "add t6, a0, a4\n   lbu t3, 1(t6)\n   li t6, 124\n   bne t3, t6, 16f\n   li t4, 2\n   li t5, 2\n   j 27f\n"
        "16: li t6, 38\n   bne t3, t6, 27f\n   li t4, 20\n   li t5, 2\n   j 27f\n"
        "17: sub t2, a1, a4\n   li t4, 10\n   li t5, 1\n   li t6, 2\n   bltu t2, t6, 27f\n"
        "add t6, a0, a4\n   lbu t3, 1(t6)\n   li t6, 59\n   bne t3, t6, 18f\n   li t4, 3\n   li t5, 2\n"
        "li t6, 3\n   bltu t2, t6, 27f\n   add t6, a0, a4\n   lbu t3, 2(t6)\n   li t6, 38\n   bne t3, t6, 27f\n"
        "li t4, 22\n   li t5, 3\n   j 27f\n"
        "18: li t6, 38\n   bne t3, t6, 27f\n   li t4, 21\n   li t5, 2\n   j 27f\n"
        "19: sub t2, a1, a4\n   li t4, 13\n   li t5, 1\n   li t6, 2\n   bltu t2, t6, 27f\n"
        "add t6, a0, a4\n   lbu t3, 1(t6)\n   li t6, 60\n   bne t3, t6, 20f\n   li t4, 4\n   li t5, 2\n"
        "li t6, 3\n   bltu t2, t6, 27f\n   add t6, a0, a4\n   lbu t3, 2(t6)\n   li t6, 60\n   bne t3, t6, 27f\n"
        "li t4, 19\n   li t5, 3\n   j 27f\n"
        "20: li t6, 38\n   bne t3, t6, 21f\n   li t4, 6\n   li t5, 2\n   j 27f\n"
        "21: li t6, 62\n   bne t3, t6, 27f\n   li t4, 8\n   li t5, 2\n   j 27f\n"
        "22: sub t2, a1, a4\n   li t4, 14\n   li t5, 1\n   li t6, 2\n   bltu t2, t6, 27f\n"
        "add t6, a0, a4\n   lbu t3, 1(t6)\n   li t6, 62\n   bne t3, t6, 23f\n   li t4, 5\n   li t5, 2\n   j 27f\n"
        "23: li t6, 38\n   bne t3, t6, 24f\n   li t4, 7\n   li t5, 2\n   j 27f\n"
        "24: li t6, 124\n   bne t3, t6, 27f\n   li t4, 9\n   li t5, 2\n   j 27f\n"
        "25: li t4, 15\n   li t5, 1\n   j 27f\n"
        "26: li t4, 16\n   li t5, 1\n"
        "27: li t6, 2\n   sw t6, 0(a2)\n   sw t4, 4(a2)\n   add t6, a0, a6\n   sd t6, 8(a2)\n"
        "sd t5, 16(a2)\n   sd a6, 24(a2)\n   addi a2, a2, 32\n   add a4, a4, t5\n   addi a5, a5, 1\n   bltu a5, a3, 1b\n"
        "28: sext.w a0, a5\n" SH_ASM_RET
        "29: li a0, -1\n" SH_ASM_RET
        "30: li a0, 0\n" SH_ASM_RET
        SH_ASM_END(sh_lex_line)

        /* sh_unquote(a0=src, a1=len, a2=dst, a3=dst_cap) */
        SH_ASM_FUNC(sh_unquote)
        "beqz a2, 12f\n   beqz a3, 12f\n   beqz a0, 11f\n   beqz a1, 11f\n"
        "mv a4, a2\n   add a1, a0, a1\n   add a3, a2, a3\n   addi a3, a3, -1\n"
        "1:  bgeu a0, a1, 10f\n   bgeu a2, a3, 10f\n   lbu a5, 0(a0)\n   addi a0, a0, 1\n"
        "li t0, 92\n   beq a5, t0, 2f\n   li t0, 39\n   beq a5, t0, 3f\n   li t0, 34\n   beq a5, t0, 4f\n"
        "sb a5, 0(a2)\n   addi a2, a2, 1\n   j 1b\n"
        "2:  bgeu a0, a1, 10f\n   lbu a5, 0(a0)\n   addi a0, a0, 1\n   sb a5, 0(a2)\n   addi a2, a2, 1\n   j 1b\n"
        "3:  bgeu a0, a1, 10f\n   bgeu a2, a3, 10f\n   lbu a5, 0(a0)\n   addi a0, a0, 1\n"
        "li t0, 39\n   beq a5, t0, 1b\n   sb a5, 0(a2)\n   addi a2, a2, 1\n   j 3b\n"
        "4:  bgeu a0, a1, 10f\n   bgeu a2, a3, 10f\n   lbu a5, 0(a0)\n   addi a0, a0, 1\n"
        "li t0, 34\n   beq a5, t0, 1b\n   li t0, 92\n   beq a5, t0, 5f\n"
        "6:  sb a5, 0(a2)\n   addi a2, a2, 1\n   j 4b\n"
        "5:  bgeu a0, a1, 6b\n   lbu a6, 0(a0)\n   li t0, 34\n   beq a6, t0, 7f\n   li t0, 92\n   beq a6, t0, 7f\n"
        "li t0, 36\n   beq a6, t0, 7f\n   li t0, 96\n   beq a6, t0, 7f\n   li t0, 10\n   bne a6, t0, 6b\n"
        "7:  addi a0, a0, 1\n   sb a6, 0(a2)\n   addi a2, a2, 1\n   j 4b\n"
        "10: sb zero, 0(a2)\n   sub a0, a2, a4\n" SH_ASM_RET
        "11: sb zero, 0(a2)\n"
        "12: li a0, 0\n" SH_ASM_RET
        SH_ASM_END(sh_unquote)

        /* sh_valid_name(a0=name, a1=len) */
        SH_ASM_FUNC(sh_valid_name)
        "beqz a0, 3f\n   beqz a1, 3f\n   lbu t0, 0(a0)\n   ori t1, t0, 32\n   addi t1, t1, -97\n"
        "li t2, 25\n   bleu t1, t2, 1f\n   li t3, 95\n   bne t0, t3, 3f\n"
        "1:  li a2, 1\n   li t2, 25\n   li t3, 95\n   li t4, 9\n"
        "2:  bgeu a2, a1, 4f\n   add t5, a0, a2\n   lbu t0, 0(t5)\n   addi a2, a2, 1\n"
        "ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t2, 2b\n"
        "addi t1, t0, -48\n   bleu t1, t4, 2b\n   beq t0, t3, 2b\n"
        "3:  li a0, 0\n" SH_ASM_RET
        "4:  li a0, 1\n" SH_ASM_RET
        SH_ASM_END(sh_valid_name)

        /* sh_param_name(a0=name, a1=len) */
        SH_ASM_FUNC(sh_param_name)
        "beqz a0, 5f\n   beqz a1, 5f\n   lbu t0, 0(a0)\n   li t2, 1\n   bne a1, t2, 1f\n"
        "addi t1, t0, -33\n   li t2, 31\n   bgtu t1, t2, 1f\n   li t3, 0xc000120d\n   srl t3, t3, t1\n   andi t3, t3, 1\n   bnez t3, 6f\n"
        "1:  addi t1, t0, -48\n   li t4, 9\n   bgtu t1, t4, 3f\n   li a2, 1\n"
        "2:  bgeu a2, a1, 6f\n   add t5, a0, a2\n   lbu t0, 0(t5)\n   addi a2, a2, 1\n   addi t1, t0, -48\n   bleu t1, t4, 2b\n   j 5f\n"
        "3:  ori t1, t0, 32\n   addi t1, t1, -97\n   li t2, 25\n   bleu t1, t2, 4f\n   li t3, 95\n   bne t0, t3, 5f\n"
        "4:  li a2, 1\n   li t2, 25\n   li t3, 95\n"
        "7:  bgeu a2, a1, 6f\n   add t5, a0, a2\n   lbu t0, 0(t5)\n   addi a2, a2, 1\n"
        "ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t2, 7b\n"
        "addi t1, t0, -48\n   bleu t1, t4, 7b\n   beq t0, t3, 7b\n"
        "5:  li a0, 0\n" SH_ASM_RET
        "6:  li a0, 1\n" SH_ASM_RET
        SH_ASM_END(sh_param_name)

        /* sh_escaped_end(a0=at, a1=quote) */
        SH_ASM_FUNC(sh_escaped_end)
        "beqz a0, 3f\n   andi a1, a1, 0xff\n   li t1, 92\n"
        "1:  lbu t0, 0(a0)\n   beqz t0, 3f\n   beq t0, a1, 3f\n   addi a0, a0, 1\n"
        "bne t0, t1, 1b\n   lbu t0, 0(a0)\n   beqz t0, 3f\n   addi a0, a0, 1\n   j 1b\n"
        "3:  " SH_ASM_RET
        SH_ASM_END(sh_escaped_end)

        /* sh_assign_split(a0=word, a1=len, a2=name_len_out) */
        SH_ASM_FUNC(sh_assign_split)
        "beqz a0, 6f\n   beqz a1, 6f\n   lbu t0, 0(a0)\n   ori t1, t0, 32\n   addi t1, t1, -97\n"
        "li t2, 25\n   bleu t1, t2, 1f\n   li t3, 95\n   bne t0, t3, 6f\n"
        "1:  li a3, 1\n   li t2, 25\n   li t3, 95\n   li t4, 9\n"
        "2:  bgeu a3, a1, 6f\n   add t5, a0, a3\n   lbu t0, 0(t5)\n"
        "ori t1, t0, 32\n   addi t1, t1, -97\n   bleu t1, t2, 3f\n"
        "addi t1, t0, -48\n   bleu t1, t4, 3f\n   bne t0, t3, 4f\n"
        "3:  addi a3, a3, 1\n   j 2b\n"
        "4:  li t1, 61\n   beq t0, t1, 5f\n   li t2, 43\n   bne t0, t2, 6f\n   addi t5, a3, 1\n   bgeu t5, a1, 6f\n"
        "add t6, a0, t5\n   lbu t0, 0(t6)\n   bne t0, t1, 6f\n   beqz a2, 7f\n   sd a3, 0(a2)\n"
        "7:  li a0, 2\n" SH_ASM_RET
        "5:  beqz a2, 8f\n   sd a3, 0(a2)\n"
        "8:  li a0, 1\n" SH_ASM_RET
        "6:  li a0, 0\n" SH_ASM_RET
        SH_ASM_END(sh_assign_split)
);

#endif

#endif /* SH_SHELL_LEX_IMPLEMENTATION */
