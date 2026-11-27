/*
        POSIX / Moonwater-compatible shell lexer and POSIX word unquoter.
        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include this file for declarations; define
        SH_SHELL_LEX_IMPLEMENTATION in one translation unit to emit the
        assembly routines. Integrates with Moonwater's ASM_FUNC / ASM_RET /
        ASM_END macros when included after src/lib.c, and builds standalone
        when compiled on its own.
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

_Static_assert(sizeof(struct sh_token) == 32, "sh_token must be 32 bytes");
_Static_assert(__builtin_offsetof(struct sh_token, kind)   == 0,  "sh_token.kind offset");
_Static_assert(__builtin_offsetof(struct sh_token, op)     == 4,  "sh_token.op offset");
_Static_assert(__builtin_offsetof(struct sh_token, text)   == 8,  "sh_token.text offset");
_Static_assert(__builtin_offsetof(struct sh_token, length) == 16, "sh_token.length offset");
_Static_assert(__builtin_offsetof(struct sh_token, at)     == 24, "sh_token.at offset");

/* When included alongside src/sh/lex.c, prove 1:1 ABI parity with lex_token & OP_* */
#if defined(LEX_WORD) && defined(OP_DSEMIAND)
_Static_assert(sizeof(struct sh_token) == sizeof(lex_token), "sh_token != lex_token size");
_Static_assert(SH_LEX_END == LEX_END && SH_LEX_WORD == LEX_WORD && SH_LEX_OPERATOR == LEX_OPERATOR,
               "SH_LEX_* mismatch");
_Static_assert(SH_OP_AND_IF == OP_AND_IF && SH_OP_DSEMIAND == OP_DSEMIAND &&
               SH_OP_HERESTRING == OP_HERESTRING && SH_OP_PIPEAND == OP_PIPEAND,
               "SH_OP_* mismatch");
#endif

sh_i32 sh_lex_line(const char *src, sh_u64 len, struct sh_token *out, sh_u32 cap);
sh_u64 sh_unquote(const char *src, sh_u64 len, char *dst, sh_u64 dst_cap);

#endif /* MOONWATER_SH_SHELL_LEX_H */

#ifdef SH_SHELL_LEX_IMPLEMENTATION

/* Architecture & ASM macro bridge: reuse src/lib.c's macros when present,
   otherwise define minimal standalone equivalents (Mach-O, ELF, COFF). */
#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_LEX_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_LEX_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_LEX_RISCV64 1
# else
#  error "src/sh/shell_lex.c: unsupported architecture (requires arm64, x86-64, or riscv64)"
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

        /*
                sh_i32 sh_lex_line(const char *src, sh_u64 len, struct sh_token *out, sh_u32 cap)
                x0 = src, x1 = len, x2 = out_ptr, w3 = cap
                x4 = i, w5 = count, x6 = start, w7 = c
                x8 = WORD_SPECIAL_MASK (0x580003C500002600: \t,\n,\r,' ',"',&,'(',),;,<,>)
                x9 = BLANK_MASK        (0x0000000100002600: \t,\n,\r,' ')
                x10 = rem / oplen / tok_len, w11 = kind / c1, w12 = op, x13 = scratch / ptr
        */
        SH_ASM_FUNC(sh_lex_line)
        "cbz x0, 30f\n"
        "cbz x2, 30f\n"
        "cbz w3, 30f\n"
        "mov x4, #0\n"
        "mov w5, #0\n"
        "movz x8, #0x2600\n"
        "movk x8, #0x03c5, lsl #32\n"
        "movk x8, #0x5800, lsl #48\n"
        "movz x9, #0x2600\n"
        "movk x9, #0x0001, lsl #32\n"

        /* 1: Main token loop (skip blanks) */
        "1: cmp x4, x1\n"
        "b.hs 28f\n"
        "ldrb w7, [x0, x4]\n"
        "cmp w7, #32\n"
        "b.hi 2f\n"
        "lsr x13, x9, x7\n"
        "tbz x13, #0, 2f\n"
        "add x4, x4, #1\n"
        "b 1b\n"

        /* 2: Token start — fast-reject non-operators before 7-way dispatch */
        "2: cmp w7, #35\n" /* '#' comment */
        "b.eq 28f\n"
        "mov x6, x4\n"
        "cmp w7, #64\n"
        "b.lo 32f\n"
        "cmp w7, #124\n" /* '|' */
        "b.eq 15f\n"
        "cmp w7, #92\n"  /* '\\' */
        "b.eq 6f\n"
        "b 5f\n"         /* ordinary >= 64 word start */

        /* 32: Token start < 64: only bits in x8 can be ops or quotes */
        "32: lsr x13, x8, x7\n"
        "tbz x13, #0, 5f\n"
        "cmp w7, #39\n"  /* '\'' */
        "b.eq 7f\n"
        "cmp w7, #34\n"  /* '"' */
        "b.eq 9f\n"
        "cmp w7, #38\n"  /* '&' */
        "b.eq 13f\n"
        "cmp w7, #59\n"  /* ';' */
        "b.eq 17f\n"
        "cmp w7, #60\n"  /* '<' */
        "b.eq 19f\n"
        "cmp w7, #62\n"  /* '>' */
        "b.eq 22f\n"
        "cmp w7, #40\n"  /* '(' */
        "b.eq 25f\n"
        "b 26f\n"        /* must be ')' (41) — blanks already skipped */

        /* 3/5: Word scan loop (hot path: ordinary ASCII >= 64) */
        "5: add x4, x4, #1\n"
        "3: cmp x4, x1\n"
        "b.hs 12f\n"
        "ldrb w7, [x0, x4]\n"
        "cmp w7, #64\n"
        "b.lo 4f\n"
        "cmp w7, #92\n"  /* '\\' */
        "b.eq 6f\n"
        "cmp w7, #124\n" /* '|' */
        "b.ne 5b\n"
        "b 12f\n"

        /* 4: Word char < 64 */
        "4: lsr x13, x8, x7\n"
        "tbz x13, #0, 5b\n"
        "cmp w7, #39\n"  /* '\'' */
        "b.eq 7f\n"
        "cmp w7, #34\n"  /* '"' */
        "b.eq 9f\n"
        "b 12f\n"

        /* 6: Backslash escape in word */
        "6: add x4, x4, #1\n"
        "cmp x4, x1\n"
        "b.hs 12f\n"
        "add x4, x4, #1\n"
        "b 3b\n"

        /* 7/8: Single-quoted span in word */
        "7: add x4, x4, #1\n"
        "8: cmp x4, x1\n"
        "b.hs 29f\n"
        "ldrb w7, [x0, x4]\n"
        "add x4, x4, #1\n"
        "cmp w7, #39\n"
        "b.ne 8b\n"
        "b 3b\n"

        /* 9/10/11: Double-quoted span in word */
        "9: add x4, x4, #1\n"
        "10: cmp x4, x1\n"
        "b.hs 29f\n"
        "ldrb w7, [x0, x4]\n"
        "cmp w7, #34\n"
        "b.eq 11f\n"
        "add x4, x4, #1\n"
        "cmp w7, #92\n"
        "b.ne 10b\n"
        "cmp x4, x1\n"
        "b.hs 29f\n"
        "add x4, x4, #1\n"
        "b 10b\n"
        "11: add x4, x4, #1\n"
        "b 3b\n"

        /* 12: Emit word token */
        "12: mov w11, #1\n"
        "mov w12, #0\n"
        "add x13, x0, x6\n"
        "sub x10, x4, x6\n"
        "stp w11, w12, [x2]\n"
        "stp x13, x10, [x2, #8]\n"
        "str x6, [x2, #24]\n"
        "add x2, x2, #32\n"
        "add w5, w5, #1\n"
        "cmp w5, w3\n"
        "b.lo 1b\n"
        "b 28f\n"

        /* 13/14: Operator '&' -> &>> (18,3), && (1,2), &> (17,2), & (12,1) */
        "13: sub x10, x1, x4\n"
        "mov w12, #12\n"
        "mov x13, #1\n"
        "cmp x10, #2\n"
        "b.lo 27f\n"
        "add x11, x4, #1\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #38\n"
        "b.ne 14f\n"
        "mov w12, #1\n"
        "mov x13, #2\n"
        "b 27f\n"
        "14: cmp w11, #62\n"
        "b.ne 27f\n"
        "mov w12, #17\n"
        "mov x13, #2\n"
        "cmp x10, #3\n"
        "b.lo 27f\n"
        "add x11, x4, #2\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #62\n"
        "b.ne 27f\n"
        "mov w12, #18\n"
        "mov x13, #3\n"
        "b 27f\n"

        /* 15/16: Operator '|' -> || (2,2), |& (20,2), | (11,1) */
        "15: sub x10, x1, x4\n"
        "mov w12, #11\n"
        "mov x13, #1\n"
        "cmp x10, #2\n"
        "b.lo 27f\n"
        "add x11, x4, #1\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #124\n"
        "b.ne 16f\n"
        "mov w12, #2\n"
        "mov x13, #2\n"
        "b 27f\n"
        "16: cmp w11, #38\n"
        "b.ne 27f\n"
        "mov w12, #20\n"
        "mov x13, #2\n"
        "b 27f\n"

        /* 17/18: Operator ';' -> ;;& (22,3), ;; (3,2), ;& (21,2), ; (10,1) */
        "17: sub x10, x1, x4\n"
        "mov w12, #10\n"
        "mov x13, #1\n"
        "cmp x10, #2\n"
        "b.lo 27f\n"
        "add x11, x4, #1\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #59\n"
        "b.ne 18f\n"
        "mov w12, #3\n"
        "mov x13, #2\n"
        "cmp x10, #3\n"
        "b.lo 27f\n"
        "add x11, x4, #2\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #38\n"
        "b.ne 27f\n"
        "mov w12, #22\n"
        "mov x13, #3\n"
        "b 27f\n"
        "18: cmp w11, #38\n"
        "b.ne 27f\n"
        "mov w12, #21\n"
        "mov x13, #2\n"
        "b 27f\n"

        /* 19/20/21: Operator '<' -> <<< (19,3), << (4,2), <& (6,2), <> (8,2), < (13,1) */
        "19: sub x10, x1, x4\n"
        "mov w12, #13\n"
        "mov x13, #1\n"
        "cmp x10, #2\n"
        "b.lo 27f\n"
        "add x11, x4, #1\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #60\n"
        "b.ne 20f\n"
        "mov w12, #4\n"
        "mov x13, #2\n"
        "cmp x10, #3\n"
        "b.lo 27f\n"
        "add x11, x4, #2\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #60\n"
        "b.ne 27f\n"
        "mov w12, #19\n"
        "mov x13, #3\n"
        "b 27f\n"
        "20: cmp w11, #38\n"
        "b.ne 21f\n"
        "mov w12, #6\n"
        "mov x13, #2\n"
        "b 27f\n"
        "21: cmp w11, #62\n"
        "b.ne 27f\n"
        "mov w12, #8\n"
        "mov x13, #2\n"
        "b 27f\n"

        /* 22/23/24: Operator '>' -> >> (5,2), >& (7,2), >| (9,2), > (14,1) */
        "22: sub x10, x1, x4\n"
        "mov w12, #14\n"
        "mov x13, #1\n"
        "cmp x10, #2\n"
        "b.lo 27f\n"
        "add x11, x4, #1\n"
        "ldrb w11, [x0, x11]\n"
        "cmp w11, #62\n"
        "b.ne 23f\n"
        "mov w12, #5\n"
        "mov x13, #2\n"
        "b 27f\n"
        "23: cmp w11, #38\n"
        "b.ne 24f\n"
        "mov w12, #7\n"
        "mov x13, #2\n"
        "b 27f\n"
        "24: cmp w11, #124\n"
        "b.ne 27f\n"
        "mov w12, #9\n"
        "mov x13, #2\n"
        "b 27f\n"

        /* 25/26: '(' -> 15, ')' -> 16 */
        "25: mov w12, #15\n"
        "mov x13, #1\n"
        "b 27f\n"
        "26: mov w12, #16\n"
        "mov x13, #1\n"

        /* 27: Emit operator token (w12 = op, x13 = oplen) */
        "27: mov w11, #2\n"
        "add x10, x0, x6\n"
        "stp w11, w12, [x2]\n"
        "stp x10, x13, [x2, #8]\n"
        "str x6, [x2, #24]\n"
        "add x2, x2, #32\n"
        "add x4, x4, x13\n"
        "add w5, w5, #1\n"
        "cmp w5, w3\n"
        "b.lo 1b\n"

        /* 28: Done */
        "28: mov w0, w5\n"
        SH_ASM_RET

        /* 29: Unclosed quote error */
        "29: mov w0, #-1\n"
        SH_ASM_RET

        /* 30: Null / cap == 0 */
        "30: mov w0, #0\n"
        SH_ASM_RET
        SH_ASM_END(sh_lex_line)

        /*
                sh_u64 sh_unquote(const char *src, sh_u64 len, char *dst, sh_u64 dst_cap)
                PC-as-state-machine with post-increment pointers (zero mode register):
                  x0 = p (src cursor), x1 = src_end
                  x2 = out_p (dst cursor), x3 = dst_end (dst + dst_cap - 1)
                  x5 = dst_start, w7 = c, w8 = n
        */
        SH_ASM_FUNC(sh_unquote)
        "cbz x2, 12f\n"
        "cbz x3, 12f\n"
        "cbz x0, 11f\n"
        "cbz x1, 11f\n"
        "mov x5, x2\n"
        "add x1, x0, x1\n"
        "add x3, x2, x3\n"
        "sub x3, x3, #1\n"

        /* 1: Mode 0 (unquoted) */
        "1: cmp x0, x1\n"
        "b.hs 10f\n"
        "cmp x2, x3\n"
        "b.hs 10f\n"
        "ldrb w7, [x0], #1\n"
        "cmp w7, #92\n" /* '\\' */
        "b.eq 2f\n"
        "cmp w7, #39\n" /* '\'' */
        "b.eq 3f\n"
        "cmp w7, #34\n" /* '"' */
        "b.eq 4f\n"
        "strb w7, [x2], #1\n"
        "b 1b\n"

        /* 2: Mode 0 backslash */
        "2: cmp x0, x1\n"
        "b.hs 10f\n"
        "ldrb w7, [x0], #1\n"
        "strb w7, [x2], #1\n"
        "b 1b\n"

        /* 3: Mode 1 (single-quoted) */
        "3: cmp x0, x1\n"
        "b.hs 10f\n"
        "cmp x2, x3\n"
        "b.hs 10f\n"
        "ldrb w7, [x0], #1\n"
        "cmp w7, #39\n"
        "b.eq 1b\n"
        "strb w7, [x2], #1\n"
        "b 3b\n"

        /* 4: Mode 2 (double-quoted) */
        "4: cmp x0, x1\n"
        "b.hs 10f\n"
        "cmp x2, x3\n"
        "b.hs 10f\n"
        "ldrb w7, [x0], #1\n"
        "cmp w7, #34\n"
        "b.eq 1b\n"
        "cmp w7, #92\n"
        "b.eq 5f\n"
        "6: strb w7, [x2], #1\n"
        "b 4b\n"

        /* 5/7: Mode 2 backslash */
        "5: cmp x0, x1\n"
        "b.hs 6b\n"
        "ldrb w8, [x0]\n"
        "cmp w8, #34\n"
        "b.eq 7f\n"
        "cmp w8, #92\n"
        "b.eq 7f\n"
        "cmp w8, #36\n"
        "b.eq 7f\n"
        "cmp w8, #96\n"
        "b.eq 7f\n"
        "cmp w8, #10\n"
        "b.ne 6b\n"
        "7: add x0, x0, #1\n"
        "strb w8, [x2], #1\n"
        "b 4b\n"

        /* 10: Done */
        "10: strb wzr, [x2]\n"
        "sub x0, x2, x5\n"
        SH_ASM_RET

        /* 11: Empty src -> NUL terminate */
        "11: strb wzr, [x2]\n"
        /* 12: Return 0 */
        "12: mov x0, #0\n"
        SH_ASM_RET
        SH_ASM_END(sh_unquote)
);

#elif defined(SH_LEX_X64)

#if defined(_WIN32)
#define SH_LEX_LOAD_ARGS \
        "mov r12, rcx\n" \
        "mov r13, rdx\n" \
        "mov r10, r8\n"  \
        "mov r11d, r9d\n"
#define SH_UQ_LOAD_ARGS \
        "mov r10, r8\n"  \
        "mov r11, r9\n"  \
        "mov r8, rcx\n"  \
        "mov r9, rdx\n"
#else
#define SH_LEX_LOAD_ARGS \
        "mov r12, rdi\n" \
        "mov r13, rsi\n" \
        "mov r10, rdx\n" \
        "mov r11d, ecx\n"
#define SH_UQ_LOAD_ARGS \
        "mov r8, rdi\n"  \
        "mov r9, rsi\n"  \
        "mov r10, rdx\n" \
        "mov r11, rcx\n"
#endif

__asm__(
        SH_ASM_SECTION
        ".intel_syntax noprefix\n"

        /*
                sh_i32 sh_lex_line(const char *src, sh_u64 len, struct sh_token *out, sh_u32 cap)
                Normalized registers:
                  r12 = src, r13 = len, r10 = out_ptr, r11d = cap
                  rcx = i, r8d = count, r9 = start
                  rbx = WORD_SPECIAL_MASK (0x580003C500002600)
                  rax, rdx = scratch
        */
        SH_ASM_FUNC(sh_lex_line)
        "push rbx\n"
        "push r12\n"
        "push r13\n"
        SH_LEX_LOAD_ARGS
        "test r12, r12\n"
        "jz 40f\n"
        "test r10, r10\n"
        "jz 40f\n"
        "test r11d, r11d\n"
        "jz 40f\n"
        "xor ecx, ecx\n"
        "xor r8d, r8d\n"
        "mov rbx, 0x580003c500002600\n"

        /* 1/2: Main token loop (skip blanks) */
        "1: mov rdx, 0x0000000100002600\n"
        "2: cmp rcx, r13\n"
        "jae 38f\n"
        "movzx eax, byte ptr [r12 + rcx]\n"
        "cmp eax, 32\n"
        "ja 3f\n"
        "bt rdx, rax\n"
        "jnc 3f\n"
        "inc rcx\n"
        "jmp 2b\n"

        /* 3: Token start — fast-reject non-operators before dispatch */
        "3: cmp eax, 35\n" /* '#' comment */
        "je 38f\n"
        "mov r9, rcx\n"
        "cmp eax, 64\n"
        "jb 42f\n"
        "cmp eax, 124\n" /* '|' */
        "je 18f\n"
        "cmp eax, 92\n"  /* '\\' */
        "je 7f\n"
        "jmp 6f\n"       /* ordinary >= 64 word start */

        /* 42: Token start < 64: only bits in rbx can be ops or quotes */
        "42: bt rbx, rax\n"
        "jnc 6f\n"
        "cmp eax, 39\n"  /* '\'' */
        "je 8f\n"
        "cmp eax, 34\n"  /* '"' */
        "je 10f\n"
        "cmp eax, 38\n"  /* '&' */
        "je 14f\n"
        "cmp eax, 59\n"  /* ';' */
        "je 21f\n"
        "cmp eax, 60\n"  /* '<' */
        "je 25f\n"
        "cmp eax, 62\n"  /* '>' */
        "je 30f\n"
        "cmp eax, 40\n"  /* '(' */
        "je 35f\n"
        "jmp 36f\n"      /* must be ')' (41) — blanks already skipped */

        /* 4: Word scan loop (hot path: ordinary ASCII >= 64) */
        "4: cmp rcx, r13\n"
        "jae 13f\n"
        "movzx eax, byte ptr [r12 + rcx]\n"
        "cmp eax, 64\n"
        "jb 5f\n"
        "cmp eax, 92\n"  /* '\\' */
        "je 7f\n"
        "cmp eax, 124\n" /* '|' */
        "je 13f\n"
        "6: inc rcx\n"
        "jmp 4b\n"

        /* 5: Word char < 64 */
        "5: bt rbx, rax\n"
        "jnc 6b\n"
        "cmp eax, 39\n"  /* '\'' */
        "je 8f\n"
        "cmp eax, 34\n"  /* '"' */
        "je 10f\n"
        "jmp 13f\n"

        /* 7: Backslash escape in word */
        "7: inc rcx\n"
        "cmp rcx, r13\n"
        "jae 13f\n"
        "inc rcx\n"
        "jmp 4b\n"

        /* 8/9: Single-quoted span in word */
        "8: inc rcx\n"
        "9: cmp rcx, r13\n"
        "jae 39f\n"
        "movzx eax, byte ptr [r12 + rcx]\n"
        "inc rcx\n"
        "cmp eax, 39\n"
        "jne 9b\n"
        "jmp 4b\n"

        /* 10/11/12: Double-quoted span in word */
        "10: inc rcx\n"
        "11: cmp rcx, r13\n"
        "jae 39f\n"
        "movzx eax, byte ptr [r12 + rcx]\n"
        "cmp eax, 34\n"
        "je 12f\n"
        "inc rcx\n"
        "cmp eax, 92\n"
        "jne 11b\n"
        "cmp rcx, r13\n"
        "jae 39f\n"
        "inc rcx\n"
        "jmp 11b\n"
        "12: inc rcx\n"
        "jmp 4b\n"

        /* 13: Emit word token */
        "13: mov dword ptr [r10], 1\n"
        "mov dword ptr [r10 + 4], 0\n"
        "lea rax, [r12 + r9]\n"
        "mov [r10 + 8], rax\n"
        "mov rax, rcx\n"
        "sub rax, r9\n"
        "mov [r10 + 16], rax\n"
        "mov [r10 + 24], r9\n"
        "add r10, 32\n"
        "inc r8d\n"
        "cmp r8d, r11d\n"
        "jb 1b\n"
        "jmp 38f\n"

        /* 14..17: Operator '&' -> &>> (18,3), && (1,2), &> (17,2), & (12,1) */
        "14: mov rax, r13\n"
        "sub rax, rcx\n"
        "mov edx, 1\n"
        "cmp rax, 2\n"
        "jb 17f\n"
        "cmp byte ptr [r12 + rcx + 1], 38\n"
        "je 16f\n"
        "cmp byte ptr [r12 + rcx + 1], 62\n"
        "jne 17f\n"
        "cmp rax, 3\n"
        "jb 15f\n"
        "cmp byte ptr [r12 + rcx + 2], 62\n"
        "jne 15f\n"
        "mov eax, 18\n"
        "mov edx, 3\n"
        "jmp 37f\n"
        "15: mov eax, 17\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "16: mov eax, 1\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "17: mov eax, 12\n"
        "jmp 37f\n"

        /* 18..20: Operator '|' -> || (2,2), |& (20,2), | (11,1) */
        "18: mov rax, r13\n"
        "sub rax, rcx\n"
        "mov edx, 1\n"
        "cmp rax, 2\n"
        "jb 20f\n"
        "cmp byte ptr [r12 + rcx + 1], 124\n"
        "je 19f\n"
        "cmp byte ptr [r12 + rcx + 1], 38\n"
        "jne 20f\n"
        "mov eax, 20\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "19: mov eax, 2\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "20: mov eax, 11\n"
        "jmp 37f\n"

        /* 21..24: Operator ';' -> ;;& (22,3), ;; (3,2), ;& (21,2), ; (10,1) */
        "21: mov rax, r13\n"
        "sub rax, rcx\n"
        "mov edx, 1\n"
        "cmp rax, 2\n"
        "jb 24f\n"
        "cmp byte ptr [r12 + rcx + 1], 59\n"
        "je 22f\n"
        "cmp byte ptr [r12 + rcx + 1], 38\n"
        "jne 24f\n"
        "mov eax, 21\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "22: cmp rax, 3\n"
        "jb 23f\n"
        "cmp byte ptr [r12 + rcx + 2], 38\n"
        "jne 23f\n"
        "mov eax, 22\n"
        "mov edx, 3\n"
        "jmp 37f\n"
        "23: mov eax, 3\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "24: mov eax, 10\n"
        "jmp 37f\n"

        /* 25..29: Operator '<' -> <<< (19,3), << (4,2), <& (6,2), <> (8,2), < (13,1) */
        "25: mov rax, r13\n"
        "sub rax, rcx\n"
        "mov edx, 1\n"
        "cmp rax, 2\n"
        "jb 29f\n"
        "cmp byte ptr [r12 + rcx + 1], 60\n"
        "je 27f\n"
        "cmp byte ptr [r12 + rcx + 1], 38\n"
        "je 26f\n"
        "cmp byte ptr [r12 + rcx + 1], 62\n"
        "jne 29f\n"
        "mov eax, 8\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "26: mov eax, 6\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "27: cmp rax, 3\n"
        "jb 28f\n"
        "cmp byte ptr [r12 + rcx + 2], 60\n"
        "jne 28f\n"
        "mov eax, 19\n"
        "mov edx, 3\n"
        "jmp 37f\n"
        "28: mov eax, 4\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "29: mov eax, 13\n"
        "jmp 37f\n"

        /* 30..34: Operator '>' -> >> (5,2), >& (7,2), >| (9,2), > (14,1) */
        "30: mov rax, r13\n"
        "sub rax, rcx\n"
        "mov edx, 1\n"
        "cmp rax, 2\n"
        "jb 34f\n"
        "cmp byte ptr [r12 + rcx + 1], 62\n"
        "je 33f\n"
        "cmp byte ptr [r12 + rcx + 1], 38\n"
        "je 32f\n"
        "cmp byte ptr [r12 + rcx + 1], 124\n"
        "jne 34f\n"
        "mov eax, 9\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "32: mov eax, 7\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "33: mov eax, 5\n"
        "mov edx, 2\n"
        "jmp 37f\n"
        "34: mov eax, 14\n"
        "jmp 37f\n"

        /* 35/36: '(' -> 15, ')' -> 16 */
        "35: mov eax, 15\n"
        "mov edx, 1\n"
        "jmp 37f\n"
        "36: mov eax, 16\n"
        "mov edx, 1\n"

        /* 37: Emit operator token (eax = op, rdx = oplen) */
        "37: mov dword ptr [r10], 2\n"
        "mov dword ptr [r10 + 4], eax\n"
        "lea rax, [r12 + r9]\n"
        "mov [r10 + 8], rax\n"
        "mov [r10 + 16], rdx\n"
        "mov [r10 + 24], r9\n"
        "add r10, 32\n"
        "add rcx, rdx\n"
        "inc r8d\n"
        "cmp r8d, r11d\n"
        "jb 1b\n"

        /* 38: Done */
        "38: mov eax, r8d\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        SH_ASM_RET

        /* 39: Unclosed quote error */
        "39: mov eax, -1\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        SH_ASM_RET

        /* 40: Null / cap == 0 */
        "40: xor eax, eax\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        SH_ASM_RET
        SH_ASM_END(sh_lex_line)

        /*
                sh_u64 sh_unquote(const char *src, sh_u64 len, char *dst, sh_u64 dst_cap)
                Zero-push leaf function! PC-as-state-machine using only caller-saved regs:
                  r8 = p (src cursor), r9 = src_end
                  r10 = out_p (dst cursor), r11 = dst_end (dst + dst_cap - 1)
                  rax = dst_start, ecx = c, edx = n
        */
        SH_ASM_FUNC(sh_unquote)
        SH_UQ_LOAD_ARGS
        "test r10, r10\n"
        "jz 12f\n"
        "test r11, r11\n"
        "jz 12f\n"
        "test r8, r8\n"
        "jz 11f\n"
        "test r9, r9\n"
        "jz 11f\n"
        "mov rax, r10\n"
        "add r9, r8\n"
        "lea r11, [r10 + r11 - 1]\n"

        /* 1: Mode 0 (unquoted) */
        "1: cmp r8, r9\n"
        "jae 10f\n"
        "cmp r10, r11\n"
        "jae 10f\n"
        "movzx ecx, byte ptr [r8]\n"
        "inc r8\n"
        "cmp ecx, 92\n" /* '\\' */
        "je 2f\n"
        "cmp ecx, 39\n" /* '\'' */
        "je 3f\n"
        "cmp ecx, 34\n" /* '"' */
        "je 4f\n"
        "mov byte ptr [r10], cl\n"
        "inc r10\n"
        "jmp 1b\n"

        /* 2: Mode 0 backslash */
        "2: cmp r8, r9\n"
        "jae 10f\n"
        "movzx ecx, byte ptr [r8]\n"
        "inc r8\n"
        "mov byte ptr [r10], cl\n"
        "inc r10\n"
        "jmp 1b\n"

        /* 3: Mode 1 (single-quoted) */
        "3: cmp r8, r9\n"
        "jae 10f\n"
        "cmp r10, r11\n"
        "jae 10f\n"
        "movzx ecx, byte ptr [r8]\n"
        "inc r8\n"
        "cmp ecx, 39\n"
        "je 1b\n"
        "mov byte ptr [r10], cl\n"
        "inc r10\n"
        "jmp 3b\n"

        /* 4: Mode 2 (double-quoted) */
        "4: cmp r8, r9\n"
        "jae 10f\n"
        "cmp r10, r11\n"
        "jae 10f\n"
        "movzx ecx, byte ptr [r8]\n"
        "inc r8\n"
        "cmp ecx, 34\n"
        "je 1b\n"
        "cmp ecx, 92\n"
        "je 5f\n"
        "6: mov byte ptr [r10], cl\n"
        "inc r10\n"
        "jmp 4b\n"

        /* 5/7: Mode 2 backslash */
        "5: cmp r8, r9\n"
        "jae 6b\n"
        "movzx edx, byte ptr [r8]\n"
        "cmp edx, 34\n"
        "je 7f\n"
        "cmp edx, 92\n"
        "je 7f\n"
        "cmp edx, 36\n"
        "je 7f\n"
        "cmp edx, 96\n"
        "je 7f\n"
        "cmp edx, 10\n"
        "jne 6b\n"
        "7: inc r8\n"
        "mov byte ptr [r10], dl\n"
        "inc r10\n"
        "jmp 4b\n"

        /* 10: Done */
        "10: mov byte ptr [r10], 0\n"
        "sub r10, rax\n"
        "mov rax, r10\n"
        SH_ASM_RET

        /* 11: Empty src -> NUL terminate */
        "11: mov byte ptr [r10], 0\n"
        /* 12: Return 0 */
        "12: xor eax, eax\n"
        SH_ASM_RET
        SH_ASM_END(sh_unquote)

        ".att_syntax prefix\n"
);

#elif defined(SH_LEX_RISCV64)

__asm__(
        SH_ASM_SECTION

        /*
                sh_i32 sh_lex_line(const char *src, sh_u64 len, struct sh_token *out, sh_u32 cap)
                Zero-spill RV64I leaf function:
                  a0 = src, a1 = len, a2 = out_ptr, a3 = cap (zero-extended)
                  a4 = i, a5 = count, a6 = start, a7 = c
                  t0 = WORD_SPECIAL_MASK (0x580003C500002600)
                  t1 = BLANK_MASK        (0x0000000100002600)
                  t2 = rem / scratch, t3 = c1 / scratch, t4 = op, t5 = oplen, t6 = scratch
        */
        SH_ASM_FUNC(sh_lex_line)
        "slli a3, a3, 32\n"
        "srli a3, a3, 32\n"
        "beqz a0, 30f\n"
        "beqz a2, 30f\n"
        "beqz a3, 30f\n"
        "li a4, 0\n"
        "li a5, 0\n"
        "li t0, 0x580003c500002600\n"
        "li t1, 0x0000000100002600\n"

        /* 1: Main token loop (skip blanks) */
        "1: bgeu a4, a1, 28f\n"
        "add t6, a0, a4\n"
        "lbu a7, 0(t6)\n"
        "li t2, 32\n"
        "bgtu a7, t2, 2f\n"
        "srl t6, t1, a7\n"
        "andi t6, t6, 1\n"
        "beqz t6, 2f\n"
        "addi a4, a4, 1\n"
        "j 1b\n"

        /* 2: Token start */
        "2: li t2, 35\n" /* '#' comment */
        "beq a7, t2, 28f\n"
        "mv a6, a4\n"
        "li t2, 64\n"
        "bltu a7, t2, 32f\n"
        "li t2, 124\n" /* '|' */
        "beq a7, t2, 15f\n"
        "li t2, 92\n"  /* '\\' */
        "beq a7, t2, 6f\n"
        "j 5f\n"

        /* 32: Token start < 64 */
        "32: srl t6, t0, a7\n"
        "andi t6, t6, 1\n"
        "beqz t6, 5f\n"
        "li t2, 39\n"  /* '\'' */
        "beq a7, t2, 7f\n"
        "li t2, 34\n"  /* '"' */
        "beq a7, t2, 9f\n"
        "li t2, 38\n"  /* '&' */
        "beq a7, t2, 13f\n"
        "li t2, 59\n"  /* ';' */
        "beq a7, t2, 17f\n"
        "li t2, 60\n"  /* '<' */
        "beq a7, t2, 19f\n"
        "li t2, 62\n"  /* '>' */
        "beq a7, t2, 22f\n"
        "li t2, 40\n"  /* '(' */
        "beq a7, t2, 25f\n"
        "j 26f\n"      /* ')' */

        /* 3/5: Word scan loop */
        "5: addi a4, a4, 1\n"
        "3: bgeu a4, a1, 12f\n"
        "add t6, a0, a4\n"
        "lbu a7, 0(t6)\n"
        "li t2, 64\n"
        "bltu a7, t2, 4f\n"
        "li t2, 92\n"  /* '\\' */
        "beq a7, t2, 6f\n"
        "li t2, 124\n" /* '|' */
        "bne a7, t2, 5b\n"
        "j 12f\n"

        /* 4: Word char < 64 */
        "4: srl t6, t0, a7\n"
        "andi t6, t6, 1\n"
        "beqz t6, 5b\n"
        "li t2, 39\n"
        "beq a7, t2, 7f\n"
        "li t2, 34\n"
        "beq a7, t2, 9f\n"
        "j 12f\n"

        /* 6: Backslash escape in word */
        "6: addi a4, a4, 1\n"
        "bgeu a4, a1, 12f\n"
        "addi a4, a4, 1\n"
        "j 3b\n"

        /* 7/8: Single-quoted span in word */
        "7: addi a4, a4, 1\n"
        "li t2, 39\n"
        "8: bgeu a4, a1, 29f\n"
        "add t6, a0, a4\n"
        "lbu a7, 0(t6)\n"
        "addi a4, a4, 1\n"
        "bne a7, t2, 8b\n"
        "j 3b\n"

        /* 9/10/11: Double-quoted span in word */
        "9: addi a4, a4, 1\n"
        "li t2, 34\n"
        "li t3, 92\n"
        "10: bgeu a4, a1, 29f\n"
        "add t6, a0, a4\n"
        "lbu a7, 0(t6)\n"
        "beq a7, t2, 11f\n"
        "addi a4, a4, 1\n"
        "bne a7, t3, 10b\n"
        "bgeu a4, a1, 29f\n"
        "addi a4, a4, 1\n"
        "j 10b\n"
        "11: addi a4, a4, 1\n"
        "j 3b\n"

        /* 12: Emit word token */
        "12: li t4, 1\n"
        "sw t4, 0(a2)\n"
        "sw zero, 4(a2)\n"
        "add t6, a0, a6\n"
        "sd t6, 8(a2)\n"
        "sub t5, a4, a6\n"
        "sd t5, 16(a2)\n"
        "sd a6, 24(a2)\n"
        "addi a2, a2, 32\n"
        "addi a5, a5, 1\n"
        "bltu a5, a3, 1b\n"
        "j 28f\n"

        /* 13/14: '&' */
        "13: sub t2, a1, a4\n"
        "li t4, 12\n"
        "li t5, 1\n"
        "li t6, 2\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 1(t6)\n"
        "li t6, 38\n"
        "bne t3, t6, 14f\n"
        "li t4, 1\n"
        "li t5, 2\n"
        "j 27f\n"
        "14: li t6, 62\n"
        "bne t3, t6, 27f\n"
        "li t4, 17\n"
        "li t5, 2\n"
        "li t6, 3\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 2(t6)\n"
        "li t6, 62\n"
        "bne t3, t6, 27f\n"
        "li t4, 18\n"
        "li t5, 3\n"
        "j 27f\n"

        /* 15/16: '|' */
        "15: sub t2, a1, a4\n"
        "li t4, 11\n"
        "li t5, 1\n"
        "li t6, 2\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 1(t6)\n"
        "li t6, 124\n"
        "bne t3, t6, 16f\n"
        "li t4, 2\n"
        "li t5, 2\n"
        "j 27f\n"
        "16: li t6, 38\n"
        "bne t3, t6, 27f\n"
        "li t4, 20\n"
        "li t5, 2\n"
        "j 27f\n"

        /* 17/18: ';' */
        "17: sub t2, a1, a4\n"
        "li t4, 10\n"
        "li t5, 1\n"
        "li t6, 2\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 1(t6)\n"
        "li t6, 59\n"
        "bne t3, t6, 18f\n"
        "li t4, 3\n"
        "li t5, 2\n"
        "li t6, 3\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 2(t6)\n"
        "li t6, 38\n"
        "bne t3, t6, 27f\n"
        "li t4, 22\n"
        "li t5, 3\n"
        "j 27f\n"
        "18: li t6, 38\n"
        "bne t3, t6, 27f\n"
        "li t4, 21\n"
        "li t5, 2\n"
        "j 27f\n"

        /* 19/20/21: '<' */
        "19: sub t2, a1, a4\n"
        "li t4, 13\n"
        "li t5, 1\n"
        "li t6, 2\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 1(t6)\n"
        "li t6, 60\n"
        "bne t3, t6, 20f\n"
        "li t4, 4\n"
        "li t5, 2\n"
        "li t6, 3\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 2(t6)\n"
        "li t6, 60\n"
        "bne t3, t6, 27f\n"
        "li t4, 19\n"
        "li t5, 3\n"
        "j 27f\n"
        "20: li t6, 38\n"
        "bne t3, t6, 21f\n"
        "li t4, 6\n"
        "li t5, 2\n"
        "j 27f\n"
        "21: li t6, 62\n"
        "bne t3, t6, 27f\n"
        "li t4, 8\n"
        "li t5, 2\n"
        "j 27f\n"

        /* 22/23/24: '>' */
        "22: sub t2, a1, a4\n"
        "li t4, 14\n"
        "li t5, 1\n"
        "li t6, 2\n"
        "bltu t2, t6, 27f\n"
        "add t6, a0, a4\n"
        "lbu t3, 1(t6)\n"
        "li t6, 62\n"
        "bne t3, t6, 23f\n"
        "li t4, 5\n"
        "li t5, 2\n"
        "j 27f\n"
        "23: li t6, 38\n"
        "bne t3, t6, 24f\n"
        "li t4, 7\n"
        "li t5, 2\n"
        "j 27f\n"
        "24: li t6, 124\n"
        "bne t3, t6, 27f\n"
        "li t4, 9\n"
        "li t5, 2\n"
        "j 27f\n"

        /* 25/26: '(' -> 15, ')' -> 16 */
        "25: li t4, 15\n"
        "li t5, 1\n"
        "j 27f\n"
        "26: li t4, 16\n"
        "li t5, 1\n"

        /* 27: Emit operator token (t4 = op, t5 = oplen) */
        "27: li t6, 2\n"
        "sw t6, 0(a2)\n"
        "sw t4, 4(a2)\n"
        "add t6, a0, a6\n"
        "sd t6, 8(a2)\n"
        "sd t5, 16(a2)\n"
        "sd a6, 24(a2)\n"
        "addi a2, a2, 32\n"
        "add a4, a4, t5\n"
        "addi a5, a5, 1\n"
        "bltu a5, a3, 1b\n"

        /* 28: Done */
        "28: sext.w a0, a5\n"
        SH_ASM_RET

        /* 29: Unclosed quote error */
        "29: li a0, -1\n"
        SH_ASM_RET

        /* 30: Null / cap == 0 */
        "30: li a0, 0\n"
        SH_ASM_RET
        SH_ASM_END(sh_lex_line)

        /*
                sh_u64 sh_unquote(const char *src, sh_u64 len, char *dst, sh_u64 dst_cap)
                Zero-spill RV64I leaf function:
                  a0 = p, a1 = src_end, a2 = out_p, a3 = dst_end
                  a4 = dst_start, a5 = c, a6 = n, t0 = imm
        */
        SH_ASM_FUNC(sh_unquote)
        "beqz a2, 12f\n"
        "beqz a3, 12f\n"
        "beqz a0, 11f\n"
        "beqz a1, 11f\n"
        "mv a4, a2\n"
        "add a1, a0, a1\n"
        "add a3, a2, a3\n"
        "addi a3, a3, -1\n"

        /* 1: Mode 0 (unquoted) */
        "1: bgeu a0, a1, 10f\n"
        "bgeu a2, a3, 10f\n"
        "lbu a5, 0(a0)\n"
        "addi a0, a0, 1\n"
        "li t0, 92\n"
        "beq a5, t0, 2f\n"
        "li t0, 39\n"
        "beq a5, t0, 3f\n"
        "li t0, 34\n"
        "beq a5, t0, 4f\n"
        "sb a5, 0(a2)\n"
        "addi a2, a2, 1\n"
        "j 1b\n"

        /* 2: Mode 0 backslash */
        "2: bgeu a0, a1, 10f\n"
        "lbu a5, 0(a0)\n"
        "addi a0, a0, 1\n"
        "sb a5, 0(a2)\n"
        "addi a2, a2, 1\n"
        "j 1b\n"

        /* 3: Mode 1 (single-quoted) */
        "3: bgeu a0, a1, 10f\n"
        "bgeu a2, a3, 10f\n"
        "lbu a5, 0(a0)\n"
        "addi a0, a0, 1\n"
        "li t0, 39\n"
        "beq a5, t0, 1b\n"
        "sb a5, 0(a2)\n"
        "addi a2, a2, 1\n"
        "j 3b\n"

        /* 4: Mode 2 (double-quoted) */
        "4: bgeu a0, a1, 10f\n"
        "bgeu a2, a3, 10f\n"
        "lbu a5, 0(a0)\n"
        "addi a0, a0, 1\n"
        "li t0, 34\n"
        "beq a5, t0, 1b\n"
        "li t0, 92\n"
        "beq a5, t0, 5f\n"
        "6: sb a5, 0(a2)\n"
        "addi a2, a2, 1\n"
        "j 4b\n"

        /* 5/7: Mode 2 backslash */
        "5: bgeu a0, a1, 6b\n"
        "lbu a6, 0(a0)\n"
        "li t0, 34\n"
        "beq a6, t0, 7f\n"
        "li t0, 92\n"
        "beq a6, t0, 7f\n"
        "li t0, 36\n"
        "beq a6, t0, 7f\n"
        "li t0, 96\n"
        "beq a6, t0, 7f\n"
        "li t0, 10\n"
        "bne a6, t0, 6b\n"
        "7: addi a0, a0, 1\n"
        "sb a6, 0(a2)\n"
        "addi a2, a2, 1\n"
        "j 4b\n"

        /* 10: Done */
        "10: sb zero, 0(a2)\n"
        "sub a0, a2, a4\n"
        SH_ASM_RET

        /* 11: Empty src -> NUL terminate */
        "11: sb zero, 0(a2)\n"
        /* 12: Return 0 */
        "12: li a0, 0\n"
        SH_ASM_RET
        SH_ASM_END(sh_unquote)
);

#endif

#endif /* SH_SHELL_LEX_IMPLEMENTATION */
