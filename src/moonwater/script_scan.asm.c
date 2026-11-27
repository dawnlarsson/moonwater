/*
        Moonwater machine-script scanner core primitives in pure assembly:
          - mw_script_char_kind     : zero-table byte classifier (replaces script_kind[256])
          - mw_script_eq            : NULL-safe NUL-terminated string equality
          - mw_script_event_is      : event pattern matcher (' ' matches ' ' or '_')
          - mw_script_heredoc_start : <<[-][ \t]*delim parser (quotes, \, \\\n continuation)
          - mw_script_skip_heredoc  : bounded heredoc body skipper (rescan budget, <<- tabs)
          - mw_script_next          : full script tokenizer (fast clear, heredocs, quotes, words)
          - mw_script_peek          : non-destructive 1-token lookahead
          - mw_script_command_start : command-start predicate (DSEMI, punct, 9 keywords)
          - mw_script_keyword       : zero-table keyword bitmask classifier (MW_KW_*)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only, kernel-safe (no FP/SIMD,
        no red-zone reliance, standard ABI callee-saved preservation).

        STB-style: include for declarations; define
        MOONWATER_SCRIPT_SCAN_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_SCRIPT_SCAN_ASM_H
#define MOONWATER_SCRIPT_SCAN_ASM_H

#if defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
    defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT8_TYPE__  mw_scan_u8;
typedef __UINT16_TYPE__ mw_scan_u16;
typedef __UINT32_TYPE__ mw_scan_u32;
typedef __INT32_TYPE__  mw_scan_i32;
typedef __UINT64_TYPE__ mw_scan_u64;
#else
#include <stdint.h>
typedef uint8_t  mw_scan_u8;
typedef uint16_t mw_scan_u16;
typedef uint32_t mw_scan_u32;
typedef int32_t  mw_scan_i32;
typedef uint64_t mw_scan_u64;
#endif

#define MW_SCRIPT_WORD      64u
#define MW_SCRIPT_TOK_END   0u
#define MW_SCRIPT_TOK_WORD  1u
#define MW_SCRIPT_TOK_PUNCT 2u
#define MW_SCRIPT_TOK_DSEMI 3u

#define MW_SK_NONE          0u
#define MW_SK_SPACE         1u
#define MW_SK_NL            2u
#define MW_SK_HASH          3u
#define MW_SK_ESC           4u
#define MW_SK_PUNCT         5u
#define MW_SK_SEMI          6u
#define MW_SK_QUOTE         7u
#define MW_SK_WORD          8u
#define MW_SK_WORD2         9u

#define MW_KW_OPENER        1u  /* if, while, until, for, select */
#define MW_KW_CLOSER        2u  /* fi, done */
#define MW_KW_CASE          4u  /* case */
#define MW_KW_ESAC          8u  /* esac */
#define MW_KW_IN            16u /* in */
#define MW_KW_TEST          32u /* [[ */
#define MW_KW_FUNCTION      64u /* function */

/* 112 bytes — byte-for-byte identical to Moonwater's struct script_read */
struct mw_script_read {
        const unsigned char *text;      /* +0  */
        mw_scan_u64          length;    /* +8  */
        mw_scan_u64          at;        /* +16 */
        mw_scan_u32          line;      /* +24 */
        mw_scan_u8           heredoc_tabs;   /* +28 */
        mw_scan_u8           heredoc_active; /* +29 */
        mw_scan_u8           heredoc_length; /* +30 */
        mw_scan_u8           _pad;           /* +31 */
        mw_scan_u64          heredoc_total;  /* +32 */
        mw_scan_u64          rescan;         /* +40 */
        char                 heredoc[MW_SCRIPT_WORD]; /* +48..+111 */
};

/* 70 bytes — byte-for-byte identical to Moonwater's struct script_token */
struct mw_script_token {
        mw_scan_u8  kind;                    /* +0 */
        mw_scan_u8  punct;                   /* +1 */
        mw_scan_u8  fresh;                   /* +2 */
        mw_scan_u8  _pad;                    /* +3 */
        mw_scan_u16 line;                    /* +4 */
        char        word[MW_SCRIPT_WORD];    /* +6..+69 */
};

#ifdef offset_of
#define MW_SCAN_OFFSET(T, M) offset_of(T, M)
#else
#define MW_SCAN_OFFSET(T, M) ((mw_scan_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct mw_script_read) == 112, "mw_script_read must be 112 bytes");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, text)           == 0,  "read.text");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, length)         == 8,  "read.length");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, at)             == 16, "read.at");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, line)           == 24, "read.line");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, heredoc_tabs)   == 28, "read.heredoc_tabs");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, heredoc_active) == 29, "read.heredoc_active");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, heredoc_length) == 30, "read.heredoc_length");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, heredoc_total)  == 32, "read.heredoc_total");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, rescan)         == 40, "read.rescan");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_read, heredoc)        == 48, "read.heredoc");

_Static_assert(sizeof(struct mw_script_token) == 70, "mw_script_token must be 70 bytes");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_token, kind)  == 0, "token.kind");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_token, punct) == 1, "token.punct");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_token, fresh) == 2, "token.fresh");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_token, line)  == 4, "token.line");
_Static_assert(MW_SCAN_OFFSET(struct mw_script_token, word)  == 6, "token.word");

mw_scan_u32 mw_script_char_kind(mw_scan_u32 c);
mw_scan_i32 mw_script_eq(const char *a, const char *b);
mw_scan_i32 mw_script_event_is(const char *pattern, const char *event);
void        mw_script_heredoc_start(struct mw_script_read *scan);
void        mw_script_skip_heredoc(struct mw_script_read *scan);
void        mw_script_next(struct mw_script_read *scan, struct mw_script_token *token);
void        mw_script_peek(struct mw_script_read *scan, struct mw_script_token *token);
mw_scan_i32 mw_script_command_start(const struct mw_script_token *token);
mw_scan_u32 mw_script_keyword(const struct mw_script_token *token);

#endif /* MOONWATER_SCRIPT_SCAN_ASM_H */

#ifdef MOONWATER_SCRIPT_SCAN_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define MW_SCAN_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define MW_SCAN_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define MW_SCAN_RISCV64 1
# else
#  error "src/moonwater/script_scan.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define MW_SCAN_ARM64 1
# elif defined(X64) && X64
#  define MW_SCAN_X64 1
# elif defined(RISCV64) && RISCV64
#  define MW_SCAN_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define MW_ASM_SECTION   ASM_SECTION
# define MW_ASM_FUNC(n)   ASM_FUNC(n)
# define MW_ASM_END(n)    ASM_END(n)
# define MW_ASM_RET       ASM_RET
#else
# define MW_ASM_SECTION   ".text\n"
# define MW_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define MW_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define MW_ASM_END(n)   ""
# elif defined(_WIN32)
#  define MW_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define MW_ASM_END(n)   ""
# else
#  if defined(MW_SCAN_ARM64)
#   define MW_ASM_FTYPE   "%function"
#  else
#   define MW_ASM_FTYPE   "@function"
#  endif
#  define MW_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " MW_ASM_FTYPE "\n" #n ":\n"
#  define MW_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define MW_ASM_SYM(n) "_" #n
#else
# define MW_ASM_SYM(n) #n
#endif

#if defined(MW_SCAN_ARM64)

__asm__(
        MW_ASM_SECTION

        /* mw_script_char_kind(w0=c) -> SK_* (0..9) */
        MW_ASM_FUNC(mw_script_char_kind)
        "cmp w0, #125\n   b.hi 9f\n   cmp w0, #97\n   b.lo 1f\n   cmp w0, #122\n   b.ls 8f\n"
        "mov w0, #5\n" MW_ASM_RET
        "1:  cmp w0, #63\n   b.lo 2f\n   cmp w0, #91\n   b.ls 8f\n   cmp w0, #95\n   b.eq 8f\n"
        "cmp w0, #92\n   b.eq 4f\n   cmp w0, #93\n   b.eq 7f\n   cmp w0, #96\n   b.eq 6f\n   b 9f\n"
        "2:  cmp w0, #45\n   b.lo 3f\n   cmp w0, #57\n   b.ls 7f\n   cmp w0, #59\n   b.ne 9f\n"
        "mov w0, #6\n" MW_ASM_RET
        "3:  cmp w0, #32\n   b.eq 10f\n   cmp w0, #9\n   b.eq 10f\n   cmp w0, #13\n   b.eq 10f\n"
        "cmp w0, #10\n   b.eq 11f\n   cmp w0, #35\n   b.eq 12f\n   cmp w0, #34\n   b.eq 6f\n"
        "cmp w0, #39\n   b.eq 6f\n   cmp w0, #36\n   b.eq 8f\n   cmp w0, #42\n   b.eq 8f\n"
        "sub w1, w0, #40\n   cmp w1, #1\n   b.hi 9f\n   mov w0, #5\n" MW_ASM_RET
        "4:  mov w0, #4\n" MW_ASM_RET
        "6:  mov w0, #7\n" MW_ASM_RET
        "7:  mov w0, #9\n" MW_ASM_RET
        "8:  mov w0, #8\n" MW_ASM_RET
        "9:  mov w0, #0\n" MW_ASM_RET
        "10: mov w0, #1\n" MW_ASM_RET
        "11: mov w0, #2\n" MW_ASM_RET
        "12: mov w0, #3\n" MW_ASM_RET
        MW_ASM_END(mw_script_char_kind)

        /* mw_script_eq(x0=a, x1=b) -> 1 if equal else 0 */
        MW_ASM_FUNC(mw_script_eq)
        "cbz x0, 2f\n   cbz x1, 2f\n"
        "1:  ldrb w2, [x0], #1\n   ldrb w3, [x1], #1\n   cmp w2, w3\n   b.ne 2f\n   cbnz w2, 1b\n"
        "mov w0, #1\n" MW_ASM_RET
        "2:  mov w0, #0\n" MW_ASM_RET
        MW_ASM_END(mw_script_eq)

        /* mw_script_event_is(x0=pattern, x1=event) -> 1 if match else 0 */
        MW_ASM_FUNC(mw_script_event_is)
        "cbz x0, 3f\n   cbz x1, 3f\n"
        "1:  ldrb w2, [x0], #1\n   ldrb w3, [x1], #1\n   cbz w2, 2f\n   cbz w3, 3f\n"
        "cmp w2, w3\n   b.eq 1b\n   cmp w3, #32\n   b.ne 3f\n   cmp w2, #95\n   b.eq 1b\n"
        "3:  mov w0, #0\n" MW_ASM_RET
        "2:  cmp w3, #0\n   cset w0, eq\n" MW_ASM_RET
        MW_ASM_END(mw_script_event_is)

        /* mw_script_heredoc_start(x0=scan) */
        MW_ASM_FUNC(mw_script_heredoc_start)
        "cbz x0, 9f\n   ldr x1, [x0]\n   cbz x1, 9f\n   ldr x2, [x0, #8]\n   ldr x3, [x0, #16]\n"
        "ldr w4, [x0, #24]\n   add x3, x3, #2\n   mov w5, #0\n   cmp x3, x2\n   b.hs 1f\n"
        "ldrb w6, [x1, x3]\n   cmp w6, #45\n   b.ne 1f\n   mov w5, #1\n   add x3, x3, #1\n"
        "1:  strb w5, [x0, #28]\n"
        "2:  cmp x3, x2\n   b.hs 3f\n   ldrb w6, [x1, x3]\n   cmp w6, #32\n   b.eq 10f\n"
        "cmp w6, #9\n   b.ne 3f\n"
        "10: add x3, x3, #1\n   b 2b\n"
        "3:  mov w5, #0\n   mov w7, #0\n   mov x8, #0\n   add x9, x0, #48\n"
        "movz x10, #0x0600\n   movk x10, #0x0241, lsl #32\n   movk x10, #0x5800, lsl #48\n"
        "4:  cmp x3, x2\n   b.hs 8f\n   ldrb w6, [x1, x3]\n   cbnz w5, 5f\n"
        "cmp w6, #39\n   b.eq 11f\n   cmp w6, #34\n   b.ne 6f\n"
        "11: mov w5, w6\n   add x3, x3, #1\n   b 4b\n"
        "5:  cmp w6, w5\n   b.ne 6f\n   mov w5, #0\n   add x3, x3, #1\n   b 4b\n"
        "6:  cmp w6, #10\n   b.eq 8f\n   cbnz w5, 7f\n   cmp w6, #124\n   b.eq 8f\n"
        "cmp w6, #64\n   b.hs 7f\n   lsr x11, x10, x6\n   tbnz x11, #0, 8f\n"
        "7:  cmp w6, #92\n   b.ne 12f\n   cmp w5, #39\n   b.eq 12f\n   sub x11, x2, x3\n"
        "cmp x11, #2\n   b.lo 12f\n   add x3, x3, #1\n   ldrb w6, [x1, x3]\n   cmp w6, #10\n   b.ne 12f\n"
        "add x3, x3, #1\n   add w4, w4, #1\n   b 4b\n"
        "12: add x8, x8, #1\n   cmp w7, #63\n   b.hs 13f\n   strb w6, [x9, x7]\n   add w7, w7, #1\n"
        "13: add x3, x3, #1\n   b 4b\n"
        "8:  strb wzr, [x9, x7]\n   mov w11, #1\n   strb w11, [x0, #29]\n   strb w7, [x0, #30]\n"
        "str x8, [x0, #32]\n   str x3, [x0, #16]\n   str w4, [x0, #24]\n"
        "9:  " MW_ASM_RET
        MW_ASM_END(mw_script_heredoc_start)

        /* mw_script_skip_heredoc(x0=scan) */
        MW_ASM_FUNC(mw_script_skip_heredoc)
        "cbz x0, 9f\n   ldr x1, [x0]\n   cbz x1, 9f\n   ldr x2, [x0, #8]\n   ldr x3, [x0, #16]\n"
        "mov x4, x3\n   mov w5, #0\n   ldrb w6, [x0, #30]\n   ldrb w7, [x0, #28]\n"
        "ldr x8, [x0, #32]\n   ldr x9, [x0, #40]\n   add x14, x0, #48\n"
        "1:  cbz x9, 6f\n   cmp x3, x2\n   b.hs 6f\n   add x3, x3, #1\n   mov x10, x3\n   add w5, w5, #1\n"
        "2:  cmp x10, x2\n   b.hs 3f\n   ldrb w11, [x1, x10]\n   cmp w11, #10\n   b.eq 3f\n"
        "add x10, x10, #1\n   b 2b\n"
        "3:  mov x12, x3\n   cbz w7, 5f\n"
        "4:  cmp x12, x10\n   b.hs 5f\n   ldrb w11, [x1, x12]\n   cmp w11, #9\n   b.ne 5f\n"
        "add x12, x12, #1\n   b 4b\n"
        "5:  mov x3, x10\n   sub x11, x10, x12\n   cmp x11, x8\n   b.ne 1b\n   mov x13, #0\n"
        "10: cmp x13, x6\n   b.hs 11f\n   add x15, x12, x13\n   ldrb w11, [x1, x15]\n"
        "ldrb w15, [x14, x13]\n   cmp w11, w15\n   b.ne 1b\n   add x13, x13, #1\n   b 10b\n"
        "11: str x3, [x0, #16]\n   ldr w11, [x0, #24]\n   add w11, w11, w5\n   str w11, [x0, #24]\n   b 8f\n"
        "6:  sub x11, x3, x4\n   cmp x3, x4\n   csel x11, x11, xzr, hi\n"
        "subs x12, x9, x11\n   csel x12, x12, xzr, hi\n   str x12, [x0, #40]\n"
        "add x4, x4, #1\n   str x4, [x0, #16]\n   ldr w11, [x0, #24]\n   add w11, w11, #1\n   str w11, [x0, #24]\n"
        "8:  strb wzr, [x0, #29]\n   strb wzr, [x0, #30]\n"
        "9:  " MW_ASM_RET
        MW_ASM_END(mw_script_skip_heredoc)

        /* mw_script_next(x0=scan, x1=token) */
        MW_ASM_FUNC(mw_script_next)
        "cbz x1, 30f\n   stp xzr, xzr, [x1]\n   stp xzr, xzr, [x1, #16]\n"
        "stp xzr, xzr, [x1, #32]\n   stp xzr, xzr, [x1, #48]\n   str wzr, [x1, #64]\n   strh wzr, [x1, #68]\n"
        "cbz x0, 30f\n   ldr x2, [x0]\n   cbz x2, 30f\n   ldr x3, [x0, #8]\n"
        "ldr x4, [x0, #16]\n   ldr w5, [x0, #24]\n   mov w6, #0\n"
        "1:  cmp x4, x3\n   b.hs 7f\n   ldrb w7, [x2, x4]\n   cmp w7, #10\n   b.eq 2f\n"
        "cmp w7, #32\n   b.eq 3f\n   cmp w7, #9\n   b.eq 3f\n   cmp w7, #13\n   b.eq 3f\n"
        "cmp w7, #92\n   b.eq 4f\n   cmp w7, #35\n   b.ne 7f\n"
        "5:  cmp x4, x3\n   b.hs 7f\n   ldrb w7, [x2, x4]\n   cmp w7, #10\n   b.eq 1b\n"
        "add x4, x4, #1\n   b 5b\n"
        "2:  ldrb w8, [x0, #29]\n   cbnz w8, 6f\n   mov w6, #1\n   add w5, w5, #1\n"
        "3:  add x4, x4, #1\n   b 1b\n"
        "4:  sub x8, x3, x4\n   cmp x8, #2\n   b.lo 7f\n   add x8, x4, #1\n   ldrb w8, [x2, x8]\n"
        "cmp w8, #10\n   b.ne 7f\n   add x4, x4, #2\n   add w5, w5, #1\n   b 1b\n"
        "6:  str x4, [x0, #16]\n   str w5, [x0, #24]\n   stp x0, x1, [sp, #-32]!\n   str x30, [sp, #16]\n"
        "bl " MW_ASM_SYM(mw_script_skip_heredoc) "\n"
        "ldr x30, [sp, #16]\n   ldp x0, x1, [sp], #32\n   ldr x2, [x0]\n   ldr x3, [x0, #8]\n"
        "ldr x4, [x0, #16]\n   ldr w5, [x0, #24]\n   mov w6, #1\n   b 1b\n"
        "7:  mov w8, #65535\n   cmp w5, w8\n   csel w8, w8, w5, hi\n   strh w8, [x1, #4]\n   strb w6, [x1, #2]\n"
        "cmp x4, x3\n   b.hs 29f\n   ldrb w7, [x2, x4]\n   cmp w7, #59\n   b.ne 8f\n"
        "sub x8, x3, x4\n   cmp x8, #2\n   b.lo 12f\n   add x8, x4, #1\n   ldrb w8, [x2, x8]\n"
        "cmp w8, #59\n   b.ne 12f\n   add x4, x4, #2\n   mov w8, #3\n   strb w8, [x1]\n   b 29f\n"
        "8:  cmp w7, #60\n   b.ne 9f\n   sub x8, x3, x4\n   cmp x8, #3\n   b.lo 12f\n"
        "add x8, x4, #1\n   ldrb w9, [x2, x8]\n   cmp w9, #60\n   b.ne 12f\n"
        "add x8, x4, #2\n   ldrb w9, [x2, x8]\n   cmp w9, #60\n   b.eq 12f\n"
        "str x4, [x0, #16]\n   str w5, [x0, #24]\n   stp x1, x30, [sp, #-16]!\n"
        "bl " MW_ASM_SYM(mw_script_heredoc_start) "\n"
        "ldp x1, x30, [sp], #16\n   mov w8, #2\n   mov w7, #60\n   strb w8, [x1]\n"
        "strb w7, [x1, #1]\n   strb w7, [x1, #6]\n" MW_ASM_RET
        "9:  sub w8, w7, #40\n   cmp w8, #1\n   b.ls 12f\n   sub w8, w7, #123\n   cmp w8, #2\n   b.ls 12f\n"
        "cmp w7, #34\n   b.eq 13f\n   cmp w7, #39\n   b.eq 13f\n   cmp w7, #96\n   b.eq 13f\n"
        "sub w8, w7, #63\n   cmp w8, #28\n   b.ls 17f\n   sub w8, w7, #97\n   cmp w8, #25\n   b.ls 17f\n"
        "cmp w7, #95\n   b.eq 17f\n   cmp w7, #36\n   b.eq 17f\n   cmp w7, #42\n   b.eq 17f\n"
        "12: add x4, x4, #1\n   mov w8, #2\n   strb w8, [x1]\n   strb w7, [x1, #1]\n   strb w7, [x1, #6]\n   b 29f\n"
        "13: mov w8, #1\n   strb w8, [x1]\n   mov w9, w7\n   add x4, x4, #1\n   mov w10, #0\n   add x11, x1, #6\n"
        "14: cmp x4, x3\n   b.hs 29f\n   ldrb w7, [x2, x4]\n   cmp w7, w9\n   b.eq 16f\n"
        "cmp w9, #39\n   b.eq 15f\n   cmp w7, #92\n   b.ne 15f\n   sub x8, x3, x4\n   cmp x8, #2\n   b.lo 15f\n"
        "add x4, x4, #1\n   ldrb w7, [x2, x4]\n"
        "15: cmp w7, #10\n   cinc w5, w5, eq\n   cmp w10, #63\n   b.hs 20f\n"
        "strb w7, [x11, x10]\n   add w10, w10, #1\n"
        "20: add x4, x4, #1\n   b 14b\n"
        "16: add x4, x4, #1\n   b 29f\n"
        "17: mov w8, #1\n   strb w8, [x1]\n   mov w10, #0\n   add x13, x1, #6\n"
        "movz x11, #0xe418, lsl #32\n   movk x11, #0x83ff, lsl #48\n"
        "movz x12, #0xffff\n   movk x12, #0xafff, lsl #16\n   movk x12, #0xfffe, lsl #32\n   movk x12, #0x07ff, lsl #48\n"
        "18: cmp x4, x3\n   b.hs 29f\n   ldrb w7, [x2, x4]\n   cmp w7, #128\n   b.hs 29f\n"
        "cmp w7, #64\n   b.lo 19f\n   lsr x8, x12, x7\n   tbz x8, #0, 29f\n   b 21f\n"
        "19: lsr x8, x11, x7\n   tbz x8, #0, 29f\n"
        "21: cmp w10, #63\n   b.hs 22f\n   strb w7, [x13, x10]\n   add w10, w10, #1\n"
        "22: add x4, x4, #1\n   b 18b\n"
        "29: str x4, [x0, #16]\n   str w5, [x0, #24]\n"
        "30: " MW_ASM_RET
        MW_ASM_END(mw_script_next)

        /* mw_script_peek(x0=scan, x1=token) */
        MW_ASM_FUNC(mw_script_peek)
        "cbz x1, 2f\n   cbnz x0, 1f\n   b " MW_ASM_SYM(mw_script_next) "\n"
        "1:  stp x29, x30, [sp, #-144]!\n   str x19, [sp, #16]\n   mov x19, x0\n"
        "ldp x2, x3, [x0]\n   ldp x4, x5, [x0, #16]\n   ldp x6, x7, [x0, #32]\n   ldp x8, x9, [x0, #48]\n"
        "stp x2, x3, [sp, #32]\n   stp x4, x5, [sp, #48]\n   stp x6, x7, [sp, #64]\n   stp x8, x9, [sp, #80]\n"
        "ldp x2, x3, [x0, #64]\n   ldp x4, x5, [x0, #80]\n   ldp x6, x7, [x0, #96]\n"
        "stp x2, x3, [sp, #96]\n   stp x4, x5, [sp, #112]\n   stp x6, x7, [sp, #128]\n"
        "bl " MW_ASM_SYM(mw_script_next) "\n"
        "ldp x2, x3, [sp, #32]\n   ldp x4, x5, [sp, #48]\n   ldp x6, x7, [sp, #64]\n   ldp x8, x9, [sp, #80]\n"
        "stp x2, x3, [x19]\n   stp x4, x5, [x19, #16]\n   stp x6, x7, [x19, #32]\n   stp x8, x9, [x19, #48]\n"
        "ldp x2, x3, [sp, #96]\n   ldp x4, x5, [sp, #112]\n   ldp x6, x7, [sp, #128]\n"
        "stp x2, x3, [x19, #64]\n   stp x4, x5, [x19, #80]\n   stp x6, x7, [x19, #96]\n"
        "ldr x19, [sp, #16]\n   ldp x29, x30, [sp], #144\n"
        "2:  " MW_ASM_RET
        MW_ASM_END(mw_script_peek)

        /* mw_script_command_start(x0=token) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_command_start)
        "cbz x0, 9f\n   ldrb w1, [x0]\n   cmp w1, #3\n   b.eq 8f\n   cmp w1, #2\n   b.ne 1f\n"
        "ldrb w2, [x0, #1]\n   cmp w2, #64\n   b.lo 10f\n   sub w2, w2, #123\n   cmp w2, #2\n   cset w0, ls\n" MW_ASM_RET
        "10: movz x3, #0x0340, lsl #32\n   movk x3, #0x0800, lsl #48\n   lsr x3, x3, x2\n   and w0, w3, #1\n" MW_ASM_RET
        "1:  cmp w1, #1\n   b.ne 9f\n   add x4, x0, #6\n   mov x1, #0\n   mov x2, #0\n"
        "2:  ldrb w3, [x4], #1\n   cbz w3, 3f\n   cmp x2, #40\n   b.hs 9f\n"
        "lsl x3, x3, x2\n   orr x1, x1, x3\n   add x2, x2, #8\n   b 2b\n"
        "3:  cmp x1, #0x21\n   b.eq 8f\n   movz x2, #0x6669\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6f64\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6874\n   movk x2, #0x6e65, lsl #16\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6974\n   movk x2, #0x656d, lsl #16\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6c65\n   movk x2, #0x6573, lsl #16\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6c65\n   movk x2, #0x6669, lsl #16\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6877\n   movk x2, #0x6c69, lsl #16\n   movk x2, #0x0065, lsl #32\n   cmp x1, x2\n   b.eq 8f\n"
        "movz x2, #0x6e75\n   movk x2, #0x6974, lsl #16\n   movk x2, #0x006c, lsl #32\n   cmp x1, x2\n   b.eq 8f\n"
        "9:  mov w0, #0\n" MW_ASM_RET
        "8:  mov w0, #1\n" MW_ASM_RET
        MW_ASM_END(mw_script_command_start)

        /* mw_script_keyword(x0=token) -> MW_KW_* bitmask */
        MW_ASM_FUNC(mw_script_keyword)
        "cbz x0, 9f\n   ldrb w1, [x0]\n   cmp w1, #1\n   b.ne 9f\n   add x4, x0, #6\n   mov x1, #0\n   mov x2, #0\n"
        "2:  ldrb w3, [x4], #1\n   cbz w3, 3f\n   cmp x2, #64\n   b.hs 9f\n"
        "lsl x3, x3, x2\n   orr x1, x1, x3\n   add x2, x2, #8\n   b 2b\n"
        "3:  movz x2, #0x6669\n   cmp x1, x2\n   b.eq 20f\n   movz x2, #0x6f66\n   movk x2, #0x0072, lsl #16\n   cmp x1, x2\n   b.eq 20f\n"
        "movz x2, #0x6877\n   movk x2, #0x6c69, lsl #16\n   movk x2, #0x0065, lsl #32\n   cmp x1, x2\n   b.eq 20f\n"
        "movz x2, #0x6e75\n   movk x2, #0x6974, lsl #16\n   movk x2, #0x006c, lsl #32\n   cmp x1, x2\n   b.eq 20f\n"
        "movz x2, #0x6573\n   movk x2, #0x656c, lsl #16\n   movk x2, #0x7463, lsl #32\n   cmp x1, x2\n   b.eq 20f\n"
        "movz x2, #0x6966\n   cmp x1, x2\n   b.eq 21f\n   movz x2, #0x6f64\n   movk x2, #0x656e, lsl #16\n   cmp x1, x2\n   b.eq 21f\n"
        "movz x2, #0x6163\n   movk x2, #0x6573, lsl #16\n   cmp x1, x2\n   b.eq 22f\n"
        "movz x2, #0x7365\n   movk x2, #0x6361, lsl #16\n   cmp x1, x2\n   b.eq 23f\n"
        "movz x2, #0x6e69\n   cmp x1, x2\n   b.eq 24f\n   movz x2, #0x5b5b\n   cmp x1, x2\n   b.eq 25f\n"
        "movz x2, #0x7566\n   movk x2, #0x636e, lsl #16\n   movk x2, #0x6974, lsl #32\n   movk x2, #0x6e6f, lsl #48\n"
        "cmp x1, x2\n   b.eq 26f\n"
        "9:  mov w0, #0\n" MW_ASM_RET
        "20: mov w0, #1\n" MW_ASM_RET
        "21: mov w0, #2\n" MW_ASM_RET
        "22: mov w0, #4\n" MW_ASM_RET
        "23: mov w0, #8\n" MW_ASM_RET
        "24: mov w0, #16\n" MW_ASM_RET
        "25: mov w0, #32\n" MW_ASM_RET
        "26: mov w0, #64\n" MW_ASM_RET
        MW_ASM_END(mw_script_keyword)
);

#elif defined(MW_SCAN_X64)

#if defined(_WIN32)
# define MW_X64_A0          "rcx"
# define MW_X64_A0D         "ecx"
# define MW_X64_A1          "rdx"
# define MW_X64_LOAD1       "mov r8, rcx\n"
# define MW_X64_LOAD2       "mov r8, rcx\n   mov r9, rdx\n"
# define MW_X64_SET_A0_R8   "mov rcx, r8\n"
# define MW_X64_CALL2_SUB   "sub rsp, 40\n"
# define MW_X64_CALL2_ADD   "add rsp, 40\n"
# define MW_X64_CALL1_SUB   "sub rsp, 32\n"
# define MW_X64_CALL1_ADD   "add rsp, 32\n"
# define MW_X64_PEEK_SUB    "sub rsp, 152\n"
# define MW_X64_PEEK_ADD    "add rsp, 152\n"
# define MW_X64_PEEK_OFF    "32"
# define MW_X64_PEEK_ARGS   "mov rcx, rbx\n   mov rdx, r12\n"
#else
# define MW_X64_A0          "rdi"
# define MW_X64_A0D         "edi"
# define MW_X64_A1          "rsi"
# define MW_X64_LOAD1       "mov r8, rdi\n"
# define MW_X64_LOAD2       "mov r8, rdi\n   mov r9, rsi\n"
# define MW_X64_SET_A0_R8   "mov rdi, r8\n"
# define MW_X64_CALL2_SUB   "sub rsp, 8\n"
# define MW_X64_CALL2_ADD   "add rsp, 8\n"
# define MW_X64_CALL1_SUB   ""
# define MW_X64_CALL1_ADD   ""
# define MW_X64_PEEK_SUB    "sub rsp, 120\n"
# define MW_X64_PEEK_ADD    "add rsp, 120\n"
# define MW_X64_PEEK_OFF    "0"
# define MW_X64_PEEK_ARGS   "mov rdi, rbx\n   mov rsi, r12\n"
#endif

__asm__(
        MW_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* mw_script_char_kind(c) -> SK_* (0..9) */
        MW_ASM_FUNC(mw_script_char_kind)
        "mov eax, " MW_X64_A0D "\n   cmp eax, 125\n   ja 9f\n   cmp eax, 97\n   jb 1f\n"
        "cmp eax, 122\n   jbe 8f\n   mov eax, 5\n" MW_ASM_RET
        "1:  cmp eax, 63\n   jb 2f\n   cmp eax, 91\n   jbe 8f\n   cmp eax, 95\n   je 8f\n"
        "cmp eax, 92\n   je 4f\n   cmp eax, 93\n   je 7f\n   cmp eax, 96\n   je 6f\n   jmp 9f\n"
        "2:  cmp eax, 45\n   jb 3f\n   cmp eax, 57\n   jbe 7f\n   cmp eax, 59\n   jne 9f\n"
        "mov eax, 6\n" MW_ASM_RET
        "3:  cmp eax, 32\n   je 10f\n   cmp eax, 9\n   je 10f\n   cmp eax, 13\n   je 10f\n"
        "cmp eax, 10\n   je 11f\n   cmp eax, 35\n   je 12f\n   cmp eax, 34\n   je 6f\n"
        "cmp eax, 39\n   je 6f\n   cmp eax, 36\n   je 8f\n   cmp eax, 42\n   je 8f\n"
        "sub eax, 40\n   cmp eax, 1\n   ja 9f\n   mov eax, 5\n" MW_ASM_RET
        "4:  mov eax, 4\n" MW_ASM_RET
        "6:  mov eax, 7\n" MW_ASM_RET
        "7:  mov eax, 9\n" MW_ASM_RET
        "8:  mov eax, 8\n" MW_ASM_RET
        "9:  xor eax, eax\n" MW_ASM_RET
        "10: mov eax, 1\n" MW_ASM_RET
        "11: mov eax, 2\n" MW_ASM_RET
        "12: mov eax, 3\n" MW_ASM_RET
        MW_ASM_END(mw_script_char_kind)

        /* mw_script_eq(a, b) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_eq)
        MW_X64_LOAD2
        "test r8, r8\n   jz 2f\n   test r9, r9\n   jz 2f\n"
        "1:  movzx eax, byte ptr [r8]\n   movzx ecx, byte ptr [r9]\n   cmp eax, ecx\n   jne 2f\n"
        "inc r8\n   inc r9\n   test eax, eax\n   jnz 1b\n   mov eax, 1\n" MW_ASM_RET
        "2:  xor eax, eax\n" MW_ASM_RET
        MW_ASM_END(mw_script_eq)

        /* mw_script_event_is(pattern, event) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_event_is)
        MW_X64_LOAD2
        "test r8, r8\n   jz 3f\n   test r9, r9\n   jz 3f\n"
        "1:  movzx eax, byte ptr [r8]\n   movzx ecx, byte ptr [r9]\n   test eax, eax\n   jz 2f\n"
        "test ecx, ecx\n   jz 3f\n   inc r8\n   inc r9\n   cmp eax, ecx\n   je 1b\n"
        "cmp ecx, 32\n   jne 3f\n   cmp eax, 95\n   je 1b\n"
        "3:  xor eax, eax\n" MW_ASM_RET
        "2:  test ecx, ecx\n   sete al\n   movzx eax, al\n" MW_ASM_RET
        MW_ASM_END(mw_script_event_is)

        /* mw_script_heredoc_start(scan) */
        MW_ASM_FUNC(mw_script_heredoc_start)
        MW_X64_LOAD1
        "test r8, r8\n   jz 9f\n   mov r9, [r8]\n   test r9, r9\n   jz 9f\n"
        "mov r11, [r8 + 8]\n   mov r10, [r8 + 16]\n   add r10, 2\n   xor eax, eax\n"
        "cmp r10, r11\n   jae 1f\n   cmp byte ptr [r9 + r10], 45\n   jne 1f\n   mov al, 1\n   inc r10\n"
        "1:  mov byte ptr [r8 + 28], al\n"
        "2:  cmp r10, r11\n   jae 3f\n   movzx edx, byte ptr [r9 + r10]\n   cmp edx, 32\n   je 10f\n"
        "cmp edx, 9\n   jne 3f\n"
        "10: inc r10\n   jmp 2b\n"
        "3:  xor eax, eax\n   xor ecx, ecx\n"
        "4:  cmp r10, r11\n   jae 8f\n   movzx edx, byte ptr [r9 + r10]\n   test ch, ch\n   jnz 5f\n"
        "cmp edx, 39\n   je 11f\n   cmp edx, 34\n   jne 6f\n"
        "11: mov ch, dl\n   inc r10\n   jmp 4b\n"
        "5:  cmp dl, ch\n   jne 6f\n   xor ch, ch\n   inc r10\n   jmp 4b\n"
        "6:  cmp edx, 10\n   je 8f\n   test ch, ch\n   jnz 7f\n   cmp edx, 124\n   je 8f\n"
        "cmp edx, 64\n   jae 7f\n   push rax\n   mov rax, 0x5800024100000600\n   bt rax, rdx\n   pop rax\n   jc 8f\n"
        "7:  cmp edx, 92\n   jne 12f\n   cmp ch, 39\n   je 12f\n   push rax\n   mov rax, r11\n"
        "sub rax, r10\n   cmp rax, 2\n   pop rax\n   jb 12f\n   inc r10\n   movzx edx, byte ptr [r9 + r10]\n"
        "cmp edx, 10\n   jne 12f\n   inc r10\n   inc dword ptr [r8 + 24]\n   jmp 4b\n"
        "12: inc rax\n   cmp cl, 63\n   jae 13f\n   push rdx\n   movzx edx, cl\n   mov cl, byte ptr [rsp]\n"
        "mov byte ptr [r8 + rdx + 48], cl\n   mov cl, dl\n   inc cl\n   pop rdx\n"
        "13: inc r10\n   jmp 4b\n"
        "8:  movzx edx, cl\n   mov byte ptr [r8 + rdx + 48], 0\n   mov byte ptr [r8 + 29], 1\n"
        "mov byte ptr [r8 + 30], cl\n   mov [r8 + 32], rax\n   mov [r8 + 16], r10\n"
        "9:  " MW_ASM_RET
        MW_ASM_END(mw_script_heredoc_start)

        /* mw_script_skip_heredoc(scan) */
        MW_ASM_FUNC(mw_script_skip_heredoc)
        MW_X64_LOAD1
        "test r8, r8\n   jz 9f\n   mov r9, [r8]\n   test r9, r9\n   jz 9f\n"
        "push rbx\n   push r12\n   push r13\n"
        "mov r10, [r8 + 16]\n   mov r11, r10\n   mov r12, [r8 + 8]\n   xor ebx, ebx\n"
        "1:  cmp qword ptr [r8 + 40], 0\n   je 6f\n   cmp r10, r12\n   jae 6f\n"
        "inc r10\n   mov rcx, r10\n   inc ebx\n"
        "2:  cmp rcx, r12\n   jae 3f\n   cmp byte ptr [r9 + rcx], 10\n   je 3f\n   inc rcx\n   jmp 2b\n"
        "3:  mov rdx, r10\n   cmp byte ptr [r8 + 28], 0\n   je 5f\n"
        "4:  cmp rdx, rcx\n   jae 5f\n   cmp byte ptr [r9 + rdx], 9\n   jne 5f\n   inc rdx\n   jmp 4b\n"
        "5:  mov r10, rcx\n   sub rcx, rdx\n   cmp rcx, [r8 + 32]\n   jne 1b\n"
        "movzx r13d, byte ptr [r8 + 30]\n   xor eax, eax\n"
        "10: cmp rax, r13\n   jae 11f\n   mov cl, byte ptr [r9 + rdx]\n   cmp cl, byte ptr [r8 + rax + 48]\n"
        "jne 1b\n   inc rdx\n   inc rax\n   jmp 10b\n"
        "11: mov [r8 + 16], r10\n   add dword ptr [r8 + 24], ebx\n   jmp 8f\n"
        "6:  xor eax, eax\n   mov rcx, r10\n   sub rcx, r11\n   cmp r10, r11\n   cmova rax, rcx\n"
        "mov rcx, [r8 + 40]\n   sub rcx, rax\n   xor edx, edx\n   cmp [r8 + 40], rax\n   cmovbe rcx, rdx\n"
        "mov [r8 + 40], rcx\n   inc r11\n   mov [r8 + 16], r11\n   inc dword ptr [r8 + 24]\n"
        "8:  mov byte ptr [r8 + 29], 0\n   mov byte ptr [r8 + 30], 0\n"
        "pop r13\n   pop r12\n   pop rbx\n"
        "9:  " MW_ASM_RET
        MW_ASM_END(mw_script_skip_heredoc)

        /* mw_script_next(scan, token) */
        MW_ASM_FUNC(mw_script_next)
        MW_X64_LOAD2
        "test r9, r9\n   jz 30f\n   xor eax, eax\n"
        "mov [r9], rax\n   mov [r9 + 8], rax\n   mov [r9 + 16], rax\n   mov [r9 + 24], rax\n"
        "mov [r9 + 32], rax\n   mov [r9 + 40], rax\n   mov [r9 + 48], rax\n   mov [r9 + 56], rax\n"
        "mov dword ptr [r9 + 64], eax\n   mov word ptr [r9 + 68], ax\n"
        "test r8, r8\n   jz 30f\n   mov rdx, [r8]\n   test rdx, rdx\n   jz 30f\n"
        "mov r11, [r8 + 8]\n   mov r10, [r8 + 16]\n"
        "1:  cmp r10, r11\n   jae 7f\n   movzx eax, byte ptr [rdx + r10]\n   cmp eax, 10\n   je 2f\n"
        "cmp eax, 32\n   je 3f\n   cmp eax, 9\n   je 3f\n   cmp eax, 13\n   je 3f\n"
        "cmp eax, 92\n   je 4f\n   cmp eax, 35\n   jne 7f\n"
        "5:  cmp r10, r11\n   jae 7f\n   cmp byte ptr [rdx + r10], 10\n   je 1b\n   inc r10\n   jmp 5b\n"
        "2:  cmp byte ptr [r8 + 29], 0\n   jne 6f\n   mov byte ptr [r9 + 2], 1\n   inc dword ptr [r8 + 24]\n"
        "3:  inc r10\n   jmp 1b\n"
        "4:  mov rcx, r11\n   sub rcx, r10\n   cmp rcx, 2\n   jb 7f\n"
        "cmp byte ptr [rdx + r10 + 1], 10\n   jne 7f\n   add r10, 2\n   inc dword ptr [r8 + 24]\n   jmp 1b\n"
        "6:  mov [r8 + 16], r10\n   push r8\n   push r9\n" MW_X64_CALL2_SUB MW_X64_SET_A0_R8
        "call " MW_ASM_SYM(mw_script_skip_heredoc) "\n"
        MW_X64_CALL2_ADD "pop r9\n   pop r8\n   mov rdx, [r8]\n   mov r11, [r8 + 8]\n   mov r10, [r8 + 16]\n"
        "mov byte ptr [r9 + 2], 1\n   jmp 1b\n"
        "7:  mov eax, dword ptr [r8 + 24]\n   mov ecx, 65535\n   cmp eax, ecx\n   cmova eax, ecx\n"
        "mov word ptr [r9 + 4], ax\n   cmp r10, r11\n   jae 29f\n   movzx eax, byte ptr [rdx + r10]\n"
        "cmp eax, 59\n   jne 8f\n   mov rcx, r11\n   sub rcx, r10\n   cmp rcx, 2\n   jb 12f\n"
        "cmp byte ptr [rdx + r10 + 1], 59\n   jne 12f\n   add r10, 2\n   mov byte ptr [r9], 3\n   jmp 29f\n"
        "8:  cmp eax, 60\n   jne 9f\n   mov rcx, r11\n   sub rcx, r10\n   cmp rcx, 3\n   jb 12f\n"
        "cmp byte ptr [rdx + r10 + 1], 60\n   jne 12f\n   cmp byte ptr [rdx + r10 + 2], 60\n   je 12f\n"
        "mov [r8 + 16], r10\n   push r9\n" MW_X64_CALL1_SUB MW_X64_SET_A0_R8
        "call " MW_ASM_SYM(mw_script_heredoc_start) "\n"
        MW_X64_CALL1_ADD "pop r9\n   mov byte ptr [r9], 2\n   mov byte ptr [r9 + 1], 60\n"
        "mov byte ptr [r9 + 6], 60\n" MW_ASM_RET
        "9:  lea ecx, [rax - 40]\n   cmp ecx, 1\n   jbe 12f\n   lea ecx, [rax - 123]\n   cmp ecx, 2\n   jbe 12f\n"
        "cmp eax, 34\n   je 13f\n   cmp eax, 39\n   je 13f\n   cmp eax, 96\n   je 13f\n"
        "lea ecx, [rax - 63]\n   cmp ecx, 28\n   jbe 17f\n   lea ecx, [rax - 97]\n   cmp ecx, 25\n   jbe 17f\n"
        "cmp eax, 95\n   je 17f\n   cmp eax, 36\n   je 17f\n   cmp eax, 42\n   je 17f\n"
        "12: inc r10\n   mov byte ptr [r9], 2\n   mov byte ptr [r9 + 1], al\n   mov byte ptr [r9 + 6], al\n   jmp 29f\n"
        "13: mov byte ptr [r9], 1\n   mov ch, al\n   xor cl, cl\n   inc r10\n"
        "14: cmp r10, r11\n   jae 29f\n   movzx eax, byte ptr [rdx + r10]\n   cmp al, ch\n   je 16f\n"
        "cmp ch, 39\n   je 15f\n   cmp eax, 92\n   jne 15f\n   push rax\n   mov rax, r11\n   sub rax, r10\n"
        "cmp rax, 2\n   pop rax\n   jb 15f\n   inc r10\n   movzx eax, byte ptr [rdx + r10]\n"
        "15: cmp eax, 10\n   jne 20f\n   inc dword ptr [r8 + 24]\n"
        "20: cmp cl, 63\n   jae 21f\n   push rdx\n   movzx edx, cl\n   mov byte ptr [r9 + rdx + 6], al\n"
        "inc cl\n   pop rdx\n"
        "21: inc r10\n   jmp 14b\n"
        "16: inc r10\n   jmp 29f\n"
        "17: mov byte ptr [r9], 1\n   xor ecx, ecx\n   push rbx\n   mov rbx, 0x07fffffeafffffff\n"
        "18: cmp r10, r11\n   jae 28f\n   movzx eax, byte ptr [rdx + r10]\n   cmp eax, 128\n   jae 28f\n"
        "cmp eax, 64\n   jb 19f\n   bt rbx, rax\n   jnc 28f\n   jmp 22f\n"
        "19: push rbx\n   mov rbx, 0x83ffe41800000000\n   bt rbx, rax\n   pop rbx\n   jnc 28f\n"
        "22: cmp ecx, 63\n   jae 23f\n   mov byte ptr [r9 + rcx + 6], al\n   inc ecx\n"
        "23: inc r10\n   jmp 18b\n"
        "28: pop rbx\n"
        "29: mov [r8 + 16], r10\n"
        "30: " MW_ASM_RET
        MW_ASM_END(mw_script_next)

        /* mw_script_peek(scan, token) */
        MW_ASM_FUNC(mw_script_peek)
        MW_X64_LOAD2
        "test r9, r9\n   jz 3f\n   test r8, r8\n   jnz 1f\n   jmp " MW_ASM_SYM(mw_script_next) "\n"
        "1:  push rbx\n   push r12\n" MW_X64_PEEK_SUB
        "mov rbx, r8\n   mov r12, r9\n   xor ecx, ecx\n"
        "2:  mov rax, [rbx + rcx]\n   mov [rsp + rcx + " MW_X64_PEEK_OFF "], rax\n   add rcx, 8\n"
        "cmp rcx, 112\n   jb 2b\n" MW_X64_PEEK_ARGS
        "call " MW_ASM_SYM(mw_script_next) "\n"
        "xor ecx, ecx\n"
        "4:  mov rax, [rsp + rcx + " MW_X64_PEEK_OFF "]\n   mov [rbx + rcx], rax\n   add rcx, 8\n"
        "cmp rcx, 112\n   jb 4b\n" MW_X64_PEEK_ADD "pop r12\n   pop rbx\n"
        "3:  " MW_ASM_RET
        MW_ASM_END(mw_script_peek)

        /* mw_script_command_start(token) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_command_start)
        MW_X64_LOAD1
        "test r8, r8\n   jz 9f\n   movzx eax, byte ptr [r8]\n   cmp eax, 3\n   je 8f\n"
        "cmp eax, 2\n   jne 1f\n   movzx eax, byte ptr [r8 + 1]\n   cmp eax, 64\n   jb 10f\n"
        "sub eax, 123\n   cmp eax, 2\n   setbe al\n   movzx eax, al\n" MW_ASM_RET
        "10: mov rcx, 0x0800034000000000\n   bt rcx, rax\n   setc al\n   movzx eax, al\n" MW_ASM_RET
        "1:  cmp eax, 1\n   jne 9f\n   lea r9, [r8 + 6]\n   xor edx, edx\n   xor ecx, ecx\n"
        "2:  movzx eax, byte ptr [r9]\n   inc r9\n   test eax, eax\n   jz 3f\n   cmp cl, 40\n   jae 9f\n"
        "shl rax, cl\n   or rdx, rax\n   add cl, 8\n   jmp 2b\n"
        "3:  cmp rdx, 0x21\n   je 8f\n   cmp rdx, 0x6669\n   je 8f\n   cmp rdx, 0x6f64\n   je 8f\n"
        "cmp rdx, 0x6e656874\n   je 8f\n   cmp rdx, 0x656d6974\n   je 8f\n   cmp rdx, 0x65736c65\n   je 8f\n"
        "cmp rdx, 0x66696c65\n   je 8f\n   mov rax, 0x00656c696877\n   cmp rdx, rax\n   je 8f\n"
        "mov rax, 0x006c69746e75\n   cmp rdx, rax\n   je 8f\n"
        "9:  xor eax, eax\n" MW_ASM_RET
        "8:  mov eax, 1\n" MW_ASM_RET
        MW_ASM_END(mw_script_command_start)

        /* mw_script_keyword(token) -> MW_KW_* bitmask */
        MW_ASM_FUNC(mw_script_keyword)
        MW_X64_LOAD1
        "test r8, r8\n   jz 9f\n   cmp byte ptr [r8], 1\n   jne 9f\n   lea r9, [r8 + 6]\n   xor edx, edx\n   xor ecx, ecx\n"
        "2:  movzx eax, byte ptr [r9]\n   inc r9\n   test eax, eax\n   jz 3f\n   cmp cl, 64\n   jae 9f\n"
        "shl rax, cl\n   or rdx, rax\n   add cl, 8\n   jmp 2b\n"
        "3:  cmp rdx, 0x6669\n   je 20f\n   cmp rdx, 0x726f66\n   je 20f\n"
        "mov rax, 0x00656c696877\n   cmp rdx, rax\n   je 20f\n   mov rax, 0x006c69746e75\n   cmp rdx, rax\n   je 20f\n"
        "mov rax, 0x7463656c6573\n   cmp rdx, rax\n   je 20f\n   cmp rdx, 0x6966\n   je 21f\n"
        "cmp rdx, 0x656e6f64\n   je 21f\n   cmp rdx, 0x65736163\n   je 22f\n   cmp rdx, 0x63617365\n   je 23f\n"
        "cmp rdx, 0x6e69\n   je 24f\n   cmp rdx, 0x5b5b\n   je 25f\n"
        "mov rax, 0x6e6f6974636e7566\n   cmp rdx, rax\n   je 26f\n"
        "9:  xor eax, eax\n" MW_ASM_RET
        "20: mov eax, 1\n" MW_ASM_RET
        "21: mov eax, 2\n" MW_ASM_RET
        "22: mov eax, 4\n" MW_ASM_RET
        "23: mov eax, 8\n" MW_ASM_RET
        "24: mov eax, 16\n" MW_ASM_RET
        "25: mov eax, 32\n" MW_ASM_RET
        "26: mov eax, 64\n" MW_ASM_RET
        MW_ASM_END(mw_script_keyword)

        ".att_syntax prefix\n"
);

#elif defined(MW_SCAN_RISCV64)

__asm__(
        MW_ASM_SECTION

        /* mw_script_char_kind(a0=c) -> SK_* (0..9) */
        MW_ASM_FUNC(mw_script_char_kind)
        "slli a0, a0, 32\n   srli a0, a0, 32\n   li t0, 125\n   bgtu a0, t0, 9f\n"
        "li t0, 97\n   bltu a0, t0, 1f\n   li t0, 122\n   bleu a0, t0, 8f\n   li a0, 5\n" MW_ASM_RET
        "1:  li t0, 63\n   bltu a0, t0, 2f\n   li t0, 91\n   bleu a0, t0, 8f\n   li t0, 95\n   beq a0, t0, 8f\n"
        "li t0, 92\n   beq a0, t0, 4f\n   li t0, 93\n   beq a0, t0, 7f\n   li t0, 96\n   beq a0, t0, 6f\n   j 9f\n"
        "2:  li t0, 45\n   bltu a0, t0, 3f\n   li t0, 57\n   bleu a0, t0, 7f\n   li t0, 59\n   bne a0, t0, 9f\n"
        "li a0, 6\n" MW_ASM_RET
        "3:  li t0, 32\n   beq a0, t0, 10f\n   li t0, 9\n   beq a0, t0, 10f\n   li t0, 13\n   beq a0, t0, 10f\n"
        "li t0, 10\n   beq a0, t0, 11f\n   li t0, 35\n   beq a0, t0, 12f\n   li t0, 34\n   beq a0, t0, 6f\n"
        "li t0, 39\n   beq a0, t0, 6f\n   li t0, 36\n   beq a0, t0, 8f\n   li t0, 42\n   beq a0, t0, 8f\n"
        "addi t0, a0, -40\n   li t1, 1\n   bgtu t0, t1, 9f\n   li a0, 5\n" MW_ASM_RET
        "4:  li a0, 4\n" MW_ASM_RET
        "6:  li a0, 7\n" MW_ASM_RET
        "7:  li a0, 9\n" MW_ASM_RET
        "8:  li a0, 8\n" MW_ASM_RET
        "9:  li a0, 0\n" MW_ASM_RET
        "10: li a0, 1\n" MW_ASM_RET
        "11: li a0, 2\n" MW_ASM_RET
        "12: li a0, 3\n" MW_ASM_RET
        MW_ASM_END(mw_script_char_kind)

        /* mw_script_eq(a0=a, a1=b) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_eq)
        "beqz a0, 2f\n   beqz a1, 2f\n"
        "1:  lbu t0, 0(a0)\n   lbu t1, 0(a1)\n   bne t0, t1, 2f\n   addi a0, a0, 1\n   addi a1, a1, 1\n"
        "bnez t0, 1b\n   li a0, 1\n" MW_ASM_RET
        "2:  li a0, 0\n" MW_ASM_RET
        MW_ASM_END(mw_script_eq)

        /* mw_script_event_is(a0=pattern, a1=event) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_event_is)
        "beqz a0, 3f\n   beqz a1, 3f\n   li t2, 32\n   li t3, 95\n"
        "1:  lbu t0, 0(a0)\n   lbu t1, 0(a1)\n   beqz t0, 2f\n   beqz t1, 3f\n"
        "addi a0, a0, 1\n   addi a1, a1, 1\n   beq t0, t1, 1b\n   bne t1, t2, 3f\n   beq t0, t3, 1b\n"
        "3:  li a0, 0\n" MW_ASM_RET
        "2:  seqz a0, t1\n" MW_ASM_RET
        MW_ASM_END(mw_script_event_is)

        /* mw_script_heredoc_start(a0=scan) */
        MW_ASM_FUNC(mw_script_heredoc_start)
        "beqz a0, 9f\n   ld a1, 0(a0)\n   beqz a1, 9f\n   ld a2, 8(a0)\n   ld a3, 16(a0)\n"
        "lwu a4, 24(a0)\n   addi a3, a3, 2\n   li a5, 0\n   bgeu a3, a2, 1f\n"
        "add t0, a1, a3\n   lbu t1, 0(t0)\n   li t2, 45\n   bne t1, t2, 1f\n   li a5, 1\n   addi a3, a3, 1\n"
        "1:  sb a5, 28(a0)\n   li t2, 32\n   li t3, 9\n"
        "2:  bgeu a3, a2, 3f\n   add t0, a1, a3\n   lbu t1, 0(t0)\n   beq t1, t2, 10f\n   bne t1, t3, 3f\n"
        "10: addi a3, a3, 1\n   j 2b\n"
        "3:  li a5, 0\n   li a6, 0\n   li a7, 0\n   addi t4, a0, 48\n   li t5, 0x5800024100000600\n"
        "4:  bgeu a3, a2, 8f\n   add t0, a1, a3\n   lbu t1, 0(t0)\n   bnez a5, 5f\n"
        "li t2, 39\n   beq t1, t2, 11f\n   li t2, 34\n   bne t1, t2, 6f\n"
        "11: mv a5, t1\n   addi a3, a3, 1\n   j 4b\n"
        "5:  bne t1, a5, 6f\n   li a5, 0\n   addi a3, a3, 1\n   j 4b\n"
        "6:  li t2, 10\n   beq t1, t2, 8f\n   bnez a5, 7f\n   li t2, 124\n   beq t1, t2, 8f\n"
        "li t2, 64\n   bgeu t1, t2, 7f\n   srl t6, t5, t1\n   andi t6, t6, 1\n   bnez t6, 8f\n"
        "7:  li t2, 92\n   bne t1, t2, 12f\n   li t2, 39\n   beq a5, t2, 12f\n   sub t6, a2, a3\n"
        "li t2, 2\n   bltu t6, t2, 12f\n   addi a3, a3, 1\n   add t0, a1, a3\n   lbu t1, 0(t0)\n"
        "li t2, 10\n   bne t1, t2, 12f\n   addi a3, a3, 1\n   addi a4, a4, 1\n   j 4b\n"
        "12: addi a7, a7, 1\n   li t2, 63\n   bgeu a6, t2, 13f\n   add t0, t4, a6\n   sb t1, 0(t0)\n   addi a6, a6, 1\n"
        "13: addi a3, a3, 1\n   j 4b\n"
        "8:  add t0, t4, a6\n   sb zero, 0(t0)\n   li t2, 1\n   sb t2, 29(a0)\n   sb a6, 30(a0)\n"
        "sd a7, 32(a0)\n   sd a3, 16(a0)\n   sw a4, 24(a0)\n"
        "9:  " MW_ASM_RET
        MW_ASM_END(mw_script_heredoc_start)

        /* mw_script_skip_heredoc(a0=scan) */
        MW_ASM_FUNC(mw_script_skip_heredoc)
        "beqz a0, 9f\n   ld a1, 0(a0)\n   beqz a1, 9f\n   ld a2, 8(a0)\n   ld a3, 16(a0)\n"
        "mv a4, a3\n   li a5, 0\n   lbu a6, 30(a0)\n   lbu a7, 28(a0)\n"
        "ld t0, 32(a0)\n   ld t1, 40(a0)\n"
        "1:  beqz t1, 6f\n   bgeu a3, a2, 6f\n   addi a3, a3, 1\n   mv t2, a3\n   addi a5, a5, 1\n   li t6, 10\n"
        "2:  bgeu t2, a2, 3f\n   add t3, a1, t2\n   lbu t4, 0(t3)\n   beq t4, t6, 3f\n   addi t2, t2, 1\n   j 2b\n"
        "3:  mv t3, a3\n   beqz a7, 5f\n   li t6, 9\n"
        "4:  bgeu t3, t2, 5f\n   add t4, a1, t3\n   lbu t5, 0(t4)\n   bne t5, t6, 5f\n   addi t3, t3, 1\n   j 4b\n"
        "5:  mv a3, t2\n   sub t4, t2, t3\n   bne t4, t0, 1b\n   li t5, 0\n"
        "10: bgeu t5, a6, 11f\n   add t4, t3, t5\n   add t4, a1, t4\n   lbu t4, 0(t4)\n"
        "add t6, a0, t5\n   lbu t6, 48(t6)\n   bne t4, t6, 1b\n   addi t5, t5, 1\n   j 10b\n"
        "11: sd a3, 16(a0)\n   lwu t4, 24(a0)\n   add t4, t4, a5\n   sw t4, 24(a0)\n   j 8f\n"
        "6:  li t4, 0\n   bleu a3, a4, 7f\n   sub t4, a3, a4\n"
        "7:  li t5, 0\n   bgeu t4, t1, 12f\n   sub t5, t1, t4\n"
        "12: sd t5, 40(a0)\n   addi a4, a4, 1\n   sd a4, 16(a0)\n   lwu t4, 24(a0)\n   addi t4, t4, 1\n   sw t4, 24(a0)\n"
        "8:  sb zero, 29(a0)\n   sb zero, 30(a0)\n"
        "9:  " MW_ASM_RET
        MW_ASM_END(mw_script_skip_heredoc)

        /* mw_script_next(a0=scan, a1=token) */
        MW_ASM_FUNC(mw_script_next)
        "beqz a1, 30f\n   sd zero, 0(a1)\n   sd zero, 8(a1)\n   sd zero, 16(a1)\n   sd zero, 24(a1)\n"
        "sd zero, 32(a1)\n   sd zero, 40(a1)\n   sd zero, 48(a1)\n   sd zero, 56(a1)\n"
        "sw zero, 64(a1)\n   sh zero, 68(a1)\n"
        "beqz a0, 30f\n   ld a2, 0(a0)\n   beqz a2, 30f\n   ld a3, 8(a0)\n"
        "ld a4, 16(a0)\n   lwu a5, 24(a0)\n   li a6, 0\n"
        "1:  bgeu a4, a3, 7f\n   add t0, a2, a4\n   lbu a7, 0(t0)\n   li t1, 10\n   beq a7, t1, 2f\n"
        "li t1, 32\n   beq a7, t1, 3f\n   li t1, 9\n   beq a7, t1, 3f\n   li t1, 13\n   beq a7, t1, 3f\n"
        "li t1, 92\n   beq a7, t1, 4f\n   li t1, 35\n   bne a7, t1, 7f\n   li t1, 10\n"
        "5:  bgeu a4, a3, 7f\n   add t0, a2, a4\n   lbu a7, 0(t0)\n   beq a7, t1, 1b\n   addi a4, a4, 1\n   j 5b\n"
        "2:  lbu t1, 29(a0)\n   bnez t1, 6f\n   li a6, 1\n   addi a5, a5, 1\n"
        "3:  addi a4, a4, 1\n   j 1b\n"
        "4:  sub t1, a3, a4\n   li t2, 2\n   bltu t1, t2, 7f\n   add t0, a2, a4\n   lbu t1, 1(t0)\n"
        "li t2, 10\n   bne t1, t2, 7f\n   addi a4, a4, 2\n   addi a5, a5, 1\n   j 1b\n"
        "6:  sd a4, 16(a0)\n   sw a5, 24(a0)\n   addi sp, sp, -32\n   sd ra, 0(sp)\n   sd a0, 8(sp)\n   sd a1, 16(sp)\n"
        "call " MW_ASM_SYM(mw_script_skip_heredoc) "\n"
        "ld ra, 0(sp)\n   ld a0, 8(sp)\n   ld a1, 16(sp)\n   addi sp, sp, 32\n"
        "ld a2, 0(a0)\n   ld a3, 8(a0)\n   ld a4, 16(a0)\n   lwu a5, 24(a0)\n   li a6, 1\n   j 1b\n"
        "7:  li t1, 65535\n   mv t2, a5\n   bleu t2, t1, 25f\n   mv t2, t1\n"
        "25: sh t2, 4(a1)\n   sb a6, 2(a1)\n   bgeu a4, a3, 29f\n   add t0, a2, a4\n   lbu a7, 0(t0)\n"
        "li t1, 59\n   bne a7, t1, 8f\n   sub t2, a3, a4\n   li t3, 2\n   bltu t2, t3, 12f\n"
        "lbu t2, 1(t0)\n   bne t2, t1, 12f\n   addi a4, a4, 2\n   li t1, 3\n   sb t1, 0(a1)\n   j 29f\n"
        "8:  li t1, 60\n   bne a7, t1, 9f\n   sub t2, a3, a4\n   li t3, 3\n   bltu t2, t3, 12f\n"
        "lbu t2, 1(t0)\n   bne t2, t1, 12f\n   lbu t2, 2(t0)\n   beq t2, t1, 12f\n"
        "sd a4, 16(a0)\n   sw a5, 24(a0)\n   addi sp, sp, -16\n   sd ra, 0(sp)\n   sd a1, 8(sp)\n"
        "call " MW_ASM_SYM(mw_script_heredoc_start) "\n"
        "ld ra, 0(sp)\n   ld a1, 8(sp)\n   addi sp, sp, 16\n   li t1, 2\n   li t2, 60\n"
        "sb t1, 0(a1)\n   sb t2, 1(a1)\n   sb t2, 6(a1)\n" MW_ASM_RET
        "9:  addi t1, a7, -40\n   li t2, 1\n   bleu t1, t2, 12f\n   addi t1, a7, -123\n   li t2, 2\n   bleu t1, t2, 12f\n"
        "li t1, 34\n   beq a7, t1, 13f\n   li t1, 39\n   beq a7, t1, 13f\n   li t1, 96\n   beq a7, t1, 13f\n"
        "addi t1, a7, -63\n   li t2, 28\n   bleu t1, t2, 17f\n   addi t1, a7, -97\n   li t2, 25\n   bleu t1, t2, 17f\n"
        "li t1, 95\n   beq a7, t1, 17f\n   li t1, 36\n   beq a7, t1, 17f\n   li t1, 42\n   beq a7, t1, 17f\n"
        "12: addi a4, a4, 1\n   li t1, 2\n   sb t1, 0(a1)\n   sb a7, 1(a1)\n   sb a7, 6(a1)\n   j 29f\n"
        "13: li t1, 1\n   sb t1, 0(a1)\n   mv t3, a7\n   addi a4, a4, 1\n   li t4, 0\n   addi t5, a1, 6\n"
        "14: bgeu a4, a3, 29f\n   add t0, a2, a4\n   lbu a7, 0(t0)\n   beq a7, t3, 16f\n"
        "li t1, 39\n   beq t3, t1, 15f\n   li t1, 92\n   bne a7, t1, 15f\n   sub t1, a3, a4\n   li t2, 2\n   bltu t1, t2, 15f\n"
        "addi a4, a4, 1\n   lbu a7, 1(t0)\n"
        "15: li t1, 10\n   bne a7, t1, 20f\n   addi a5, a5, 1\n"
        "20: li t1, 63\n   bgeu t4, t1, 21f\n   add t6, t5, t4\n   sb a7, 0(t6)\n   addi t4, t4, 1\n"
        "21: addi a4, a4, 1\n   j 14b\n"
        "16: addi a4, a4, 1\n   j 29f\n"
        "17: li t1, 1\n   sb t1, 0(a1)\n   li t4, 0\n   addi t5, a1, 6\n"
        "li t1, 0x83ffe41800000000\n   li t2, 0x07fffffeafffffff\n   li t3, 128\n"
        "18: bgeu a4, a3, 29f\n   add t0, a2, a4\n   lbu a7, 0(t0)\n   bgeu a7, t3, 29f\n"
        "li t6, 64\n   bltu a7, t6, 19f\n   srl t6, t2, a7\n   andi t6, t6, 1\n   beqz t6, 29f\n   j 22f\n"
        "19: srl t6, t1, a7\n   andi t6, t6, 1\n   beqz t6, 29f\n"
        "22: li t6, 63\n   bgeu t4, t6, 23f\n   add t6, t5, t4\n   sb a7, 0(t6)\n   addi t4, t4, 1\n"
        "23: addi a4, a4, 1\n   j 18b\n"
        "29: sd a4, 16(a0)\n   sw a5, 24(a0)\n"
        "30: " MW_ASM_RET
        MW_ASM_END(mw_script_next)

        /* mw_script_peek(a0=scan, a1=token) */
        MW_ASM_FUNC(mw_script_peek)
        "beqz a1, 3f\n   bnez a0, 1f\n   tail " MW_ASM_SYM(mw_script_next) "\n"
        "1:  addi sp, sp, -144\n   sd ra, 0(sp)\n   sd s0, 8(sp)\n   mv s0, a0\n   li t0, 0\n   li t1, 112\n"
        "2:  add t2, a0, t0\n   ld t3, 0(t2)\n   add t2, sp, t0\n   sd t3, 16(t2)\n   addi t0, t0, 8\n   bltu t0, t1, 2b\n"
        "call " MW_ASM_SYM(mw_script_next) "\n"
        "li t0, 0\n   li t1, 112\n"
        "4:  add t2, sp, t0\n   ld t3, 16(t2)\n   add t2, s0, t0\n   sd t3, 0(t2)\n   addi t0, t0, 8\n   bltu t0, t1, 4b\n"
        "ld ra, 0(sp)\n   ld s0, 8(sp)\n   addi sp, sp, 144\n"
        "3:  " MW_ASM_RET
        MW_ASM_END(mw_script_peek)

        /* mw_script_command_start(a0=token) -> 1 or 0 */
        MW_ASM_FUNC(mw_script_command_start)
        "beqz a0, 9f\n   lbu t0, 0(a0)\n   li t1, 3\n   beq t0, t1, 8f\n   li t1, 2\n   bne t0, t1, 1f\n"
        "lbu t2, 1(a0)\n   li t1, 64\n   bltu t2, t1, 10f\n   addi t2, t2, -123\n   li t1, 2\n   sltu a0, t1, t2\n   xori a0, a0, 1\n" MW_ASM_RET
        "10: li t3, 0x0800034000000000\n   srl t3, t3, t2\n   andi a0, t3, 1\n" MW_ASM_RET
        "1:  li t1, 1\n   bne t0, t1, 9f\n   addi t4, a0, 6\n   li t0, 0\n   li t1, 0\n   li t5, 40\n"
        "2:  lbu t2, 0(t4)\n   addi t4, t4, 1\n   beqz t2, 3f\n   bgeu t1, t5, 9f\n"
        "sll t2, t2, t1\n   or t0, t0, t2\n   addi t1, t1, 8\n   j 2b\n"
        "3:  li t1, 0x21\n   beq t0, t1, 8f\n   li t1, 0x6669\n   beq t0, t1, 8f\n   li t1, 0x6f64\n   beq t0, t1, 8f\n"
        "li t1, 0x6e656874\n   beq t0, t1, 8f\n   li t1, 0x656d6974\n   beq t0, t1, 8f\n   li t1, 0x65736c65\n   beq t0, t1, 8f\n"
        "li t1, 0x66696c65\n   beq t0, t1, 8f\n   li t1, 0x00656c696877\n   beq t0, t1, 8f\n   li t1, 0x006c69746e75\n   beq t0, t1, 8f\n"
        "9:  li a0, 0\n" MW_ASM_RET
        "8:  li a0, 1\n" MW_ASM_RET
        MW_ASM_END(mw_script_command_start)

        /* mw_script_keyword(a0=token) -> MW_KW_* bitmask */
        MW_ASM_FUNC(mw_script_keyword)
        "beqz a0, 9f\n   lbu t0, 0(a0)\n   li t1, 1\n   bne t0, t1, 9f\n   addi t4, a0, 6\n   li t0, 0\n   li t1, 0\n   li t5, 64\n"
        "2:  lbu t2, 0(t4)\n   addi t4, t4, 1\n   beqz t2, 3f\n   bgeu t1, t5, 9f\n"
        "sll t2, t2, t1\n   or t0, t0, t2\n   addi t1, t1, 8\n   j 2b\n"
        "3:  li t1, 0x6669\n   beq t0, t1, 20f\n   li t1, 0x726f66\n   beq t0, t1, 20f\n"
        "li t1, 0x00656c696877\n   beq t0, t1, 20f\n   li t1, 0x006c69746e75\n   beq t0, t1, 20f\n"
        "li t1, 0x7463656c6573\n   beq t0, t1, 20f\n   li t1, 0x6966\n   beq t0, t1, 21f\n"
        "li t1, 0x656e6f64\n   beq t0, t1, 21f\n   li t1, 0x65736163\n   beq t0, t1, 22f\n   li t1, 0x63617365\n   beq t0, t1, 23f\n"
        "li t1, 0x6e69\n   beq t0, t1, 24f\n   li t1, 0x5b5b\n   beq t0, t1, 25f\n"
        "li t1, 0x6e6f6974636e7566\n   beq t0, t1, 26f\n"
        "9:  li a0, 0\n" MW_ASM_RET
        "20: li a0, 1\n" MW_ASM_RET
        "21: li a0, 2\n" MW_ASM_RET
        "22: li a0, 4\n" MW_ASM_RET
        "23: li a0, 8\n" MW_ASM_RET
        "24: li a0, 16\n" MW_ASM_RET
        "25: li a0, 32\n" MW_ASM_RET
        "26: li a0, 64\n" MW_ASM_RET
        MW_ASM_END(mw_script_keyword)
);

#endif

#endif /* MOONWATER_SCRIPT_SCAN_ASM_IMPLEMENTATION */
