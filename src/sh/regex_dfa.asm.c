/*
        Moonwater regex DFA hot inner-loop kernels in pure assembly:
          - rx_dfa_step4       : 4-lane parallel DFA transition stepper (11 live regs, zero spills)
          - rx_dfa_step1       : single-lane DFA transition stepper
          - rx_dfa_first_fast  : earliest-match DFA fast-path scanner
          - rx_dfa_longest_fast: leftmost-longest DFA fast-path scanner with branchless accept
          - rx_single          : single-byte regex node matcher (RX_ANY, RX_SET, RX_BYTE + icase)

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define SH_REGEX_DFA_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_SH_REGEX_DFA_ASM_H
#define MOONWATER_SH_REGEX_DFA_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  sh_rx_u8;
typedef p16 sh_rx_u16;
typedef p32 sh_rx_u32;
typedef b32 sh_rx_i32;
typedef p64 sh_rx_u64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
    defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT8_TYPE__  sh_rx_u8;
typedef __UINT16_TYPE__ sh_rx_u16;
typedef __UINT32_TYPE__ sh_rx_u32;
typedef __INT32_TYPE__  sh_rx_i32;
typedef __UINT64_TYPE__ sh_rx_u64;
#else
#include <stdint.h>
typedef uint8_t  sh_rx_u8;
typedef uint16_t sh_rx_u16;
typedef uint32_t sh_rx_u32;
typedef int32_t  sh_rx_i32;
typedef uint64_t sh_rx_u64;
#endif

/* 32 bytes — byte-for-byte identical to rx_lane in src/sh/regex_graph.c */
struct sh_rx_lane {
        const sh_rx_u8 *at;    /* +0  */
        const sh_rx_u8 *past;  /* +8  */
        const sh_rx_u8 *begin; /* +16 */
        sh_rx_i32       row;   /* +24 */
        sh_rx_i32       _pad;  /* +28 */
};

/* 24 bytes — byte-for-byte identical to rx_node in src/sh/regex_graph.c */
struct sh_rx_node {
        sh_rx_u8  kind;                        /* +0  RX_BYTE=1, RX_ANY=2, RX_SET=3 */
        sh_rx_u16 argument;                    /* +2  */
        sh_rx_u32 next, previous, left, right; /* +4..+16 */
        sh_rx_i32 min_max_packed;              /* +20 */
};

/* 32 bytes — byte-for-byte identical to regex_program in src/sh/regex_graph.c */
struct sh_rx_program {
        const struct sh_rx_node *nodes; /* +0  */
        const sh_rx_u8 (*sets)[256];    /* +8  */
        const void *hints;              /* +16 */
        sh_rx_u32   first;              /* +24 */
        sh_rx_u8    groups;             /* +28 */
        sh_rx_u8    policy;             /* +29 bit 0 = REGEX_DOT_NEWLINE */
        sh_rx_u8    boundary;           /* +30 */
        sh_rx_u8    flags;              /* +31 bit 7 = RX_IGNORE_CASE */
};

/* 56 bytes — immutable parameters for rx_dfa_longest_fast */
struct sh_rx_longest_ctx {
        const sh_rx_u8  *classes;   /* +0  */
        const sh_rx_u8  *name;      /* +8  */
        const sh_rx_u8  *accept;    /* +16 */
        const sh_rx_i32 *trans;     /* +24 */
        sh_rx_u64        class_rcp; /* +32 */
        const sh_rx_u8  *text;      /* +40 */
        sh_rx_u64        length;    /* +48 */
};

/* 24 bytes — mutable cursor state for rx_dfa_longest_fast */
struct sh_rx_longest_state {
        sh_rx_u64 p;    /* +0  */
        sh_rx_u64 last; /* +8  */
        sh_rx_i32 row;  /* +16 */
        sh_rx_i32 _pad; /* +20 */
};

#ifdef offset_of
#define SH_RX_OFFSET(T, M) offset_of(T, M)
#else
#define SH_RX_OFFSET(T, M) ((sh_rx_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct sh_rx_lane) == 32, "sh_rx_lane size");
_Static_assert(SH_RX_OFFSET(struct sh_rx_lane, at)  == 0,  "sh_rx_lane.at offset");
_Static_assert(SH_RX_OFFSET(struct sh_rx_lane, row) == 24, "sh_rx_lane.row offset");

_Static_assert(sizeof(struct sh_rx_node) == 24, "sh_rx_node size");
_Static_assert(SH_RX_OFFSET(struct sh_rx_node, kind)     == 0, "sh_rx_node.kind offset");
_Static_assert(SH_RX_OFFSET(struct sh_rx_node, argument) == 2, "sh_rx_node.argument offset");

_Static_assert(sizeof(struct sh_rx_program) == 32, "sh_rx_program size");
_Static_assert(SH_RX_OFFSET(struct sh_rx_program, sets)   == 8,  "sh_rx_program.sets offset");
_Static_assert(SH_RX_OFFSET(struct sh_rx_program, policy) == 29, "sh_rx_program.policy offset");
_Static_assert(SH_RX_OFFSET(struct sh_rx_program, flags)  == 31, "sh_rx_program.flags offset");

_Static_assert(sizeof(struct sh_rx_longest_ctx) == 56, "sh_rx_longest_ctx size");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, classes)   == 0,  "ctx.classes");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, name)      == 8,  "ctx.name");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, accept)    == 16, "ctx.accept");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, trans)     == 24, "ctx.trans");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, class_rcp) == 32, "ctx.class_rcp");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, text)      == 40, "ctx.text");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_ctx, length)    == 48, "ctx.length");

_Static_assert(sizeof(struct sh_rx_longest_state) == 24, "sh_rx_longest_state size");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_state, p)    == 0,  "st.p");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_state, last) == 8,  "st.last");
_Static_assert(SH_RX_OFFSET(struct sh_rx_longest_state, row)  == 16, "st.row");

sh_rx_i32 rx_dfa_step4(const sh_rx_u8 *classes, const sh_rx_i32 *trans,
                       void *lanes, sh_rx_u64 m, sh_rx_i32 *out_n);
sh_rx_i32 rx_dfa_step1(const sh_rx_u8 *classes, const sh_rx_i32 *trans,
                       const sh_rx_u8 **at_io, const sh_rx_u8 *past,
                       sh_rx_i32 *row_io);
sh_rx_i32 rx_dfa_first_fast(const sh_rx_u8 *classes, const sh_rx_i32 *trans,
                            const sh_rx_u8 *text, sh_rx_u64 *p_io,
                            sh_rx_u64 length, sh_rx_i32 *row_io);
sh_rx_i32 rx_dfa_longest_fast(const struct sh_rx_longest_ctx *ctx,
                              struct sh_rx_longest_state *st);
sh_rx_i32 rx_single(const void *program, const void *node, sh_rx_u8 byte);

#endif /* MOONWATER_SH_REGEX_DFA_ASM_H */

#ifdef SH_REGEX_DFA_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define SH_RX_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define SH_RX_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define SH_RX_RISCV64 1
# else
#  error "src/sh/regex_dfa.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define SH_RX_ARM64 1
# elif defined(X64) && X64
#  define SH_RX_X64 1
# elif defined(RISCV64) && RISCV64
#  define SH_RX_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define SH_RX_ASM_SECTION  ASM_SECTION
# define SH_RX_ASM_FUNC(n)  ASM_FUNC(n)
# define SH_RX_ASM_END(n)   ASM_END(n)
# define SH_RX_ASM_RET      ASM_RET
#else
# define SH_RX_ASM_SECTION  ".text\n"
# define SH_RX_ASM_RET      "ret\n"
# if defined(__APPLE__)
#  define SH_RX_ASM_FUNC(n) ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define SH_RX_ASM_END(n)  ""
# elif defined(_WIN32)
#  define SH_RX_ASM_FUNC(n) ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define SH_RX_ASM_END(n)  ""
# else
#  if defined(SH_RX_ARM64)
#   define SH_RX_ASM_FTYPE  "%function"
#  else
#   define SH_RX_ASM_FTYPE  "@function"
#  endif
#  define SH_RX_ASM_FUNC(n) ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " SH_RX_ASM_FTYPE "\n" #n ":\n"
#  define SH_RX_ASM_END(n)  ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(SH_RX_ARM64)

__asm__(
        SH_RX_ASM_SECTION

        /* rx_dfa_step4(x0=classes, x1=trans, x2=lanes, x3=m, x4=out_n) -> 1 if stopped, 0 if done */
        SH_RX_ASM_FUNC(rx_dfa_step4)
        "cbz x0, 4f\n   cbz x1, 4f\n   cbz x2, 4f\n   cbz x3, 4f\n"
        "ldr x5, [x2, #0]\n   ldr x6, [x2, #32]\n   ldr x7, [x2, #64]\n   ldr x8, [x2, #96]\n"
        "ldrsw x9, [x2, #24]\n   ldrsw x10, [x2, #56]\n   ldrsw x11, [x2, #88]\n   ldrsw x12, [x2, #120]\n"
        "1:  ldrb w13, [x5], #1\n   ldrb w14, [x6], #1\n   ldrb w15, [x7], #1\n   ldrb w16, [x8], #1\n"
        "ldrb w13, [x0, x13]\n   ldrb w14, [x0, x14]\n   ldrb w15, [x0, x15]\n   ldrb w16, [x0, x16]\n"
        "add x13, x9, x13\n   add x14, x10, x14\n   add x15, x11, x15\n   add x16, x12, x16\n"
        "ldr w13, [x1, x13, lsl #2]\n   ldr w14, [x1, x14, lsl #2]\n   ldr w15, [x1, x15, lsl #2]\n   ldr w16, [x1, x16, lsl #2]\n"
        "orr w17, w13, w14\n   orr w17, w17, w15\n   orr w17, w17, w16\n   tbnz w17, #31, 2f\n"
        "mov x9, x13\n   mov x10, x14\n   mov x11, x15\n   mov x12, x16\n   subs x3, x3, #1\n   b.ne 1b\n"
        "str x5, [x2, #0]\n   str x6, [x2, #32]\n   str x7, [x2, #64]\n   str x8, [x2, #96]\n"
        "str w9, [x2, #24]\n   str w10, [x2, #56]\n   str w11, [x2, #88]\n   str w12, [x2, #120]\n"
        "4:  mov w0, #0\n" SH_RX_ASM_RET
        "2:  sub x5, x5, #1\n   sub x6, x6, #1\n   sub x7, x7, #1\n   sub x8, x8, #1\n"
        "str x5, [x2, #0]\n   str x6, [x2, #32]\n   str x7, [x2, #64]\n   str x8, [x2, #96]\n"
        "str w9, [x2, #24]\n   str w10, [x2, #56]\n   str w11, [x2, #88]\n   str w12, [x2, #120]\n"
        "cbz x4, 3f\n   stp w13, w14, [x4]\n   stp w15, w16, [x4, #8]\n"
        "3:  mov w0, #1\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_step4)

        /* rx_dfa_step1(x0=classes, x1=trans, x2=at_io, x3=past, x4=row_io) -> last/stop next */
        SH_RX_ASM_FUNC(rx_dfa_step1)
        "cbz x0, 4f\n   cbz x1, 4f\n   cbz x2, 4f\n   cbz x4, 4f\n"
        "ldr x5, [x2]\n   ldrsw x6, [x4]\n   mov w7, #0\n   cmp x5, x3\n   b.hs 3f\n"
        "1:  ldrb w8, [x5], #1\n   ldrb w8, [x0, x8]\n   add x8, x6, x8\n   ldr w7, [x1, x8, lsl #2]\n"
        "tbnz w7, #31, 2f\n   mov x6, x7\n   cmp x5, x3\n   b.lo 1b\n   b 3f\n"
        "2:  sub x5, x5, #1\n"
        "3:  str x5, [x2]\n   str w6, [x4]\n   mov x0, x7\n" SH_RX_ASM_RET
        "4:  mov w0, #0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_step1)

        /* rx_dfa_first_fast(x0=classes, x1=trans, x2=text, x3=p_io, x4=length, x5=row_io) -> <0 stop or 0 at end */
        SH_RX_ASM_FUNC(rx_dfa_first_fast)
        "cbz x0, 3f\n   cbz x1, 3f\n   cbz x3, 3f\n   cbz x5, 3f\n"
        "ldr x6, [x3]\n   ldrsw x7, [x5]\n   mov w8, #0\n   cmp x6, x4\n   b.hs 2f\n   cbz x2, 3f\n"
        "1:  ldrb w9, [x2, x6]\n   ldrb w9, [x0, x9]\n   add x9, x7, x9\n   ldr w8, [x1, x9, lsl #2]\n"
        "tbnz w8, #31, 2f\n   mov x7, x8\n   add x6, x6, #1\n   cmp x6, x4\n   b.lo 1b\n   mov w8, #0\n"
        "2:  str x6, [x3]\n   str w7, [x5]\n   mov x0, x8\n" SH_RX_ASM_RET
        "3:  mov w0, #0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_first_fast)

        /* rx_dfa_longest_fast(x0=ctx, x1=st) -> <0 stop or 0 at end */
        SH_RX_ASM_FUNC(rx_dfa_longest_fast)
        "cbz x0, 3f\n   cbz x1, 3f\n"
        "ldp x2, x3, [x0, #0]\n   ldp x4, x5, [x0, #16]\n   ldp x6, x7, [x0, #32]\n   ldr x8, [x0, #48]\n"
        "ldp x9, x10, [x1, #0]\n   ldrsw x11, [x1, #16]\n   mov w12, #0\n   cmp x9, x8\n   b.hs 2f\n   cbz x7, 3f\n"
        "mov w15, #1\n"
        "1:  mul x13, x11, x6\n   ldrb w14, [x7, x9]\n   lsr x13, x13, #32\n   ldrb w14, [x2, x14]\n"
        "ldrb w13, [x4, x13]\n   ldrb w17, [x3, x14]\n   cmp w17, #0\n   cinc w17, w15, eq\n"
        "tst w13, w17\n   csel x10, x9, x10, ne\n   add x14, x11, x14\n   ldr w12, [x5, x14, lsl #2]\n"
        "tbnz w12, #31, 2f\n   mov x11, x12\n   add x9, x9, #1\n   cmp x9, x8\n   b.lo 1b\n   mov w12, #0\n"
        "2:  stp x9, x10, [x1, #0]\n   str w11, [x1, #16]\n   mov x0, x12\n" SH_RX_ASM_RET
        "3:  mov w0, #0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_longest_fast)

        /* rx_single(x0=program, x1=node, w2=byte) -> 0/1 */
        SH_RX_ASM_FUNC(rx_single)
        "cbz x0, 4f\n   cbz x1, 4f\n   and w2, w2, #0xff\n   ldrb w3, [x1, #0]\n"
        "cmp w3, #2\n   b.eq 1f\n   cmp w3, #3\n   b.eq 2f\n"
        "ldrsb w4, [x0, #31]\n   sub w5, w2, #65\n   cmp w4, #0\n   ccmp w5, #25, #2, lt\n"
        "orr w4, w2, #32\n   csel w2, w4, w2, ls\n   ldrh w4, [x1, #2]\n   cmp w2, w4\n   cset w0, eq\n" SH_RX_ASM_RET
        "1:  ldrb w4, [x0, #29]\n   cmp w2, #10\n   cset w5, ne\n   orr w0, w4, w5\n   and w0, w0, #1\n" SH_RX_ASM_RET
        "2:  ldr x4, [x0, #8]\n   ldrh w5, [x1, #2]\n   add x4, x4, x5, lsl #8\n"
        "ldrb w0, [x4, x2]\n   cmp w0, #0\n   cset w0, ne\n" SH_RX_ASM_RET
        "4:  mov w0, #0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_single)
);

#elif defined(SH_RX_X64)

#if defined(_WIN32)
#define SH_RX_STEP4_PROLOG \
        "push rbx\n   push rbp\n   push rdi\n   push rsi\n   push r12\n   push r13\n   push r14\n   push r15\n" \
        "mov rax, [rsp + 104]\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   push rdx\n   push rax\n"
#define SH_RX_STEP4_EPILOG \
        "add rsp, 16\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rsi\n   pop rdi\n   pop rbp\n   pop rbx\n"
#define SH_RX_STEP1_PROLOG \
        "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 56]\n"
#define SH_RX_STEP1_EPILOG "pop rsi\n   pop rdi\n"
#define SH_RX_FIRST_PROLOG \
        "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n   mov rcx, r9\n   mov r8, [rsp + 56]\n   mov r9, [rsp + 64]\n"
#define SH_RX_FIRST_EPILOG "pop rsi\n   pop rdi\n"
#define SH_RX_LONG_PROLOG \
        "push rbx\n   push rbp\n   push rdi\n   push rsi\n   push r12\n   push r13\n   push r14\n   push r15\n" \
        "mov rdi, rcx\n   mov rsi, rdx\n"
#define SH_RX_LONG_EPILOG \
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rsi\n   pop rdi\n   pop rbp\n   pop rbx\n"
#define SH_RX_SINGLE_LOAD "mov r9, rcx\n   mov r10, rdx\n   movzx eax, r8b\n"
#define SH_RX_S_P "r9"
#define SH_RX_S_N "r10"
#else
#define SH_RX_STEP4_PROLOG \
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n   push rdx\n   push r8\n"
#define SH_RX_STEP4_EPILOG \
        "add rsp, 16\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#define SH_RX_STEP1_PROLOG ""
#define SH_RX_STEP1_EPILOG ""
#define SH_RX_FIRST_PROLOG ""
#define SH_RX_FIRST_EPILOG ""
#define SH_RX_LONG_PROLOG \
        "push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n   push r15\n"
#define SH_RX_LONG_EPILOG \
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n"
#define SH_RX_SINGLE_LOAD "movzx eax, dl\n"
#define SH_RX_S_P "rdi"
#define SH_RX_S_N "rsi"
#endif

__asm__(
        SH_RX_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* rx_dfa_step4(classes, trans, lanes, m, out_n) -> 1 if stopped, 0 if done */
        SH_RX_ASM_FUNC(rx_dfa_step4)
        SH_RX_STEP4_PROLOG
        "test rdi, rdi\n   jz 4f\n   test rsi, rsi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   test rcx, rcx\n   jz 4f\n"
        "mov r8, [rdx]\n   mov r9, [rdx + 32]\n   mov r10, [rdx + 64]\n   mov r11, [rdx + 96]\n"
        "movsxd r12, dword ptr [rdx + 24]\n   movsxd r13, dword ptr [rdx + 56]\n"
        "movsxd r14, dword ptr [rdx + 88]\n   movsxd r15, dword ptr [rdx + 120]\n"
        "1:  movzx eax, byte ptr [r8]\n   movzx ebx, byte ptr [r9]\n   movzx edx, byte ptr [r10]\n   movzx ebp, byte ptr [r11]\n"
        "movzx eax, byte ptr [rdi + rax]\n   movzx ebx, byte ptr [rdi + rbx]\n   movzx edx, byte ptr [rdi + rdx]\n   movzx ebp, byte ptr [rdi + rbp]\n"
        "add rax, r12\n   add rbx, r13\n   add rdx, r14\n   add rbp, r15\n"
        "mov eax, dword ptr [rsi + rax*4]\n   mov ebx, dword ptr [rsi + rbx*4]\n   mov edx, dword ptr [rsi + rdx*4]\n   mov ebp, dword ptr [rsi + rbp*4]\n"
        "test eax, eax\n   js 2f\n   test ebx, ebx\n   js 2f\n   test edx, edx\n   js 2f\n   test ebp, ebp\n   js 2f\n"
        "inc r8\n   inc r9\n   inc r10\n   inc r11\n"
        "mov r12d, eax\n   mov r13d, ebx\n   mov r14d, edx\n   mov r15d, ebp\n   dec rcx\n   jnz 1b\n"
        "mov rdx, [rsp + 8]\n   mov [rdx], r8\n   mov [rdx + 32], r9\n   mov [rdx + 64], r10\n   mov [rdx + 96], r11\n"
        "mov [rdx + 24], r12d\n   mov [rdx + 56], r13d\n   mov [rdx + 88], r14d\n   mov [rdx + 120], r15d\n"
        "4:  xor eax, eax\n   jmp 5f\n"
        "2:  mov rcx, [rsp + 8]\n   mov [rcx], r8\n   mov [rcx + 32], r9\n   mov [rcx + 64], r10\n   mov [rcx + 96], r11\n"
        "mov [rcx + 24], r12d\n   mov [rcx + 56], r13d\n   mov [rcx + 88], r14d\n   mov [rcx + 120], r15d\n"
        "mov rcx, [rsp]\n   test rcx, rcx\n   jz 3f\n"
        "mov [rcx], eax\n   mov [rcx + 4], ebx\n   mov [rcx + 8], edx\n   mov [rcx + 12], ebp\n"
        "3:  mov eax, 1\n"
        "5:  " SH_RX_STEP4_EPILOG SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_step4)

        /* rx_dfa_step1(classes, trans, at_io, past, row_io) -> last/stop next */
        SH_RX_ASM_FUNC(rx_dfa_step1)
        SH_RX_STEP1_PROLOG
        "xor eax, eax\n   test rdi, rdi\n   jz 3f\n   test rsi, rsi\n   jz 3f\n   test rdx, rdx\n   jz 3f\n   test r8, r8\n   jz 3f\n"
        "mov r9, [rdx]\n   movsxd r10, dword ptr [r8]\n"
        "1:  cmp r9, rcx\n   jae 2f\n   movzx eax, byte ptr [r9]\n   movzx eax, byte ptr [rdi + rax]\n"
        "add rax, r10\n   mov eax, dword ptr [rsi + rax*4]\n   test eax, eax\n   js 2f\n"
        "mov r10d, eax\n   inc r9\n   jmp 1b\n"
        "2:  mov [rdx], r9\n   mov [r8], r10d\n"
        "3:  " SH_RX_STEP1_EPILOG SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_step1)

        /* rx_dfa_first_fast(classes, trans, text, p_io, length, row_io) -> <0 stop or 0 at end */
        SH_RX_ASM_FUNC(rx_dfa_first_fast)
        SH_RX_FIRST_PROLOG
        "xor eax, eax\n   test rdi, rdi\n   jz 3f\n   test rsi, rsi\n   jz 3f\n   test rcx, rcx\n   jz 3f\n   test r9, r9\n   jz 3f\n"
        "mov r10, [rcx]\n   movsxd r11, dword ptr [r9]\n   cmp r10, r8\n   jae 2f\n   test rdx, rdx\n   jz 3f\n"
        "1:  movzx eax, byte ptr [rdx + r10]\n   movzx eax, byte ptr [rdi + rax]\n   add rax, r11\n"
        "mov eax, dword ptr [rsi + rax*4]\n   test eax, eax\n   js 2f\n"
        "mov r11d, eax\n   inc r10\n   cmp r10, r8\n   jb 1b\n   xor eax, eax\n"
        "2:  mov [rcx], r10\n   mov [r9], r11d\n"
        "3:  " SH_RX_FIRST_EPILOG SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_first_fast)

        /* rx_dfa_longest_fast(ctx, st) -> <0 stop or 0 at end */
        SH_RX_ASM_FUNC(rx_dfa_longest_fast)
        SH_RX_LONG_PROLOG
        "xor eax, eax\n   test rdi, rdi\n   jz 3f\n   test rsi, rsi\n   jz 3f\n"
        "mov r8, [rdi]\n   mov r9, [rdi + 8]\n   mov r10, [rdi + 16]\n   mov r11, [rdi + 24]\n"
        "mov r12, [rdi + 32]\n   mov r13, [rdi + 40]\n   mov r14, [rdi + 48]\n"
        "mov rcx, [rsi]\n   mov r15, [rsi + 8]\n   movsxd rbx, dword ptr [rsi + 16]\n"
        "cmp rcx, r14\n   jae 2f\n   test r13, r13\n   jz 3f\n   mov edi, 1\n"
        "1:  mov rax, rbx\n   imul rax, r12\n   shr rax, 32\n   movzx ebp, byte ptr [r10 + rax]\n"
        "movzx eax, byte ptr [r13 + rcx]\n   movzx eax, byte ptr [r8 + rax]\n"
        "cmp byte ptr [r9 + rax], 0\n   mov edx, 2\n   cmovne edx, edi\n"
        "test ebp, edx\n   cmovne r15, rcx\n   add rax, rbx\n"
        "mov eax, dword ptr [r11 + rax*4]\n   test eax, eax\n   js 2f\n"
        "mov ebx, eax\n   inc rcx\n   cmp rcx, r14\n   jb 1b\n   xor eax, eax\n"
        "2:  mov [rsi], rcx\n   mov [rsi + 8], r15\n   mov [rsi + 16], ebx\n"
        "3:  " SH_RX_LONG_EPILOG SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_longest_fast)

        /* rx_single(program, node, byte) -> 0/1 */
        SH_RX_ASM_FUNC(rx_single)
        SH_RX_SINGLE_LOAD
        "test " SH_RX_S_P ", " SH_RX_S_P "\n   jz 4f\n   test " SH_RX_S_N ", " SH_RX_S_N "\n   jz 4f\n   movzx ecx, byte ptr [" SH_RX_S_N "]\n"
        "cmp ecx, 2\n   je 1f\n   cmp ecx, 3\n   je 2f\n"
        "test byte ptr [" SH_RX_S_P " + 31], 128\n   jz 3f\n   lea edx, [rax - 65]\n   cmp edx, 25\n   ja 3f\n   or eax, 32\n"
        "3:  cmp ax, word ptr [" SH_RX_S_N " + 2]\n   sete al\n   movzx eax, al\n" SH_RX_ASM_RET
        "1:  cmp eax, 10\n   setne cl\n   or cl, byte ptr [" SH_RX_S_P " + 29]\n   movzx eax, cl\n   and eax, 1\n" SH_RX_ASM_RET
        "2:  mov rdx, [" SH_RX_S_P " + 8]\n   movzx ecx, word ptr [" SH_RX_S_N " + 2]\n   shl rcx, 8\n   add rdx, rcx\n"
        "cmp byte ptr [rdx + rax], 0\n   setne al\n   movzx eax, al\n" SH_RX_ASM_RET
        "4:  xor eax, eax\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_single)

        ".att_syntax prefix\n"
);

#elif defined(SH_RX_RISCV64)

__asm__(
        SH_RX_ASM_SECTION

        /* rx_dfa_step4(a0=classes, a1=trans, a2=lanes, a3=m, a4=out_n) -> 1 if stopped, 0 if done */
        SH_RX_ASM_FUNC(rx_dfa_step4)
        "beqz a0, 4f\n   beqz a1, 4f\n   beqz a2, 4f\n   beqz a3, 4f\n"
        "addi sp, sp, -48\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n   sd s4, 32(sp)\n"
        "ld a5, 0(a2)\n   ld a6, 32(a2)\n   ld a7, 64(a2)\n   ld s0, 96(a2)\n"
        "lw s1, 24(a2)\n   lw s2, 56(a2)\n   lw s3, 88(a2)\n   lw s4, 120(a2)\n"
        "1:  lbu t0, 0(a5)\n   lbu t1, 0(a6)\n   lbu t2, 0(a7)\n   lbu t3, 0(s0)\n"
        "add t0, a0, t0\n   add t1, a0, t1\n   add t2, a0, t2\n   add t3, a0, t3\n"
        "lbu t0, 0(t0)\n   lbu t1, 0(t1)\n   lbu t2, 0(t2)\n   lbu t3, 0(t3)\n"
        "add t0, s1, t0\n   add t1, s2, t1\n   add t2, s3, t2\n   add t3, s4, t3\n"
        "slli t0, t0, 2\n   slli t1, t1, 2\n   slli t2, t2, 2\n   slli t3, t3, 2\n"
        "add t0, a1, t0\n   add t1, a1, t1\n   add t2, a1, t2\n   add t3, a1, t3\n"
        "lw t0, 0(t0)\n   lw t1, 0(t1)\n   lw t2, 0(t2)\n   lw t3, 0(t3)\n"
        "or t4, t0, t1\n   or t4, t4, t2\n   or t4, t4, t3\n   bltz t4, 2f\n"
        "addi a5, a5, 1\n   addi a6, a6, 1\n   addi a7, a7, 1\n   addi s0, s0, 1\n"
        "mv s1, t0\n   mv s2, t1\n   mv s3, t2\n   mv s4, t3\n   addi a3, a3, -1\n   bnez a3, 1b\n"
        "sd a5, 0(a2)\n   sd a6, 32(a2)\n   sd a7, 64(a2)\n   sd s0, 96(a2)\n"
        "sw s1, 24(a2)\n   sw s2, 56(a2)\n   sw s3, 88(a2)\n   sw s4, 120(a2)\n   li a0, 0\n   j 5f\n"
        "2:  sd a5, 0(a2)\n   sd a6, 32(a2)\n   sd a7, 64(a2)\n   sd s0, 96(a2)\n"
        "sw s1, 24(a2)\n   sw s2, 56(a2)\n   sw s3, 88(a2)\n   sw s4, 120(a2)\n"
        "beqz a4, 3f\n   sw t0, 0(a4)\n   sw t1, 4(a4)\n   sw t2, 8(a4)\n   sw t3, 12(a4)\n"
        "3:  li a0, 1\n"
        "5:  ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   ld s4, 32(sp)\n   addi sp, sp, 48\n" SH_RX_ASM_RET
        "4:  li a0, 0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_step4)

        /* rx_dfa_step1(a0=classes, a1=trans, a2=at_io, a3=past, a4=row_io) -> last/stop next */
        SH_RX_ASM_FUNC(rx_dfa_step1)
        "beqz a0, 3f\n   beqz a1, 3f\n   beqz a2, 3f\n   beqz a4, 3f\n"
        "ld a5, 0(a2)\n   lw a6, 0(a4)\n   li t0, 0\n"
        "1:  bgeu a5, a3, 2f\n   lbu t1, 0(a5)\n   add t1, a0, t1\n   lbu t1, 0(t1)\n"
        "add t1, a6, t1\n   slli t1, t1, 2\n   add t1, a1, t1\n   lw t0, 0(t1)\n"
        "bltz t0, 2f\n   mv a6, t0\n   addi a5, a5, 1\n   j 1b\n"
        "2:  sd a5, 0(a2)\n   sw a6, 0(a4)\n   mv a0, t0\n" SH_RX_ASM_RET
        "3:  li a0, 0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_step1)

        /* rx_dfa_first_fast(a0=classes, a1=trans, a2=text, a3=p_io, a4=length, a5=row_io) -> <0 stop or 0 at end */
        SH_RX_ASM_FUNC(rx_dfa_first_fast)
        "beqz a0, 3f\n   beqz a1, 3f\n   beqz a3, 3f\n   beqz a5, 3f\n"
        "ld a6, 0(a3)\n   lw a7, 0(a5)\n   li t0, 0\n   bgeu a6, a4, 2f\n   beqz a2, 3f\n"
        "1:  add t1, a2, a6\n   lbu t1, 0(t1)\n   add t1, a0, t1\n   lbu t1, 0(t1)\n"
        "add t1, a7, t1\n   slli t1, t1, 2\n   add t1, a1, t1\n   lw t0, 0(t1)\n"
        "bltz t0, 2f\n   mv a7, t0\n   addi a6, a6, 1\n   bltu a6, a4, 1b\n   li t0, 0\n"
        "2:  sd a6, 0(a3)\n   sw a7, 0(a5)\n   mv a0, t0\n" SH_RX_ASM_RET
        "3:  li a0, 0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_first_fast)

        /* rx_dfa_longest_fast(a0=ctx, a1=st) -> <0 stop or 0 at end */
        SH_RX_ASM_FUNC(rx_dfa_longest_fast)
        "beqz a0, 3f\n   beqz a1, 3f\n"
        "addi sp, sp, -32\n   sd s0, 0(sp)\n   sd s1, 8(sp)\n   sd s2, 16(sp)\n   sd s3, 24(sp)\n"
        "ld a2, 0(a0)\n   ld a3, 8(a0)\n   ld a4, 16(a0)\n   ld a5, 24(a0)\n"
        "ld a6, 32(a0)\n   ld a7, 40(a0)\n   ld s0, 48(a0)\n"
        "ld s1, 0(a1)\n   ld s2, 8(a1)\n   lw s3, 16(a1)\n   li t0, 0\n   bgeu s1, s0, 2f\n   beqz a7, 4f\n"
        "1:  mul t1, s3, a6\n   srli t1, t1, 32\n   add t1, a4, t1\n   lbu t1, 0(t1)\n"
        "add t2, a7, s1\n   lbu t2, 0(t2)\n   add t2, a2, t2\n   lbu t2, 0(t2)\n"
        "add t3, a3, t2\n   lbu t3, 0(t3)\n   snez t3, t3\n   li t4, 2\n   sub t3, t4, t3\n"
        "and t1, t1, t3\n   beqz t1, 6f\n   mv s2, s1\n"
        "6:  add t2, s3, t2\n   slli t2, t2, 2\n   add t2, a5, t2\n   lw t0, 0(t2)\n"
        "bltz t0, 2f\n   mv s3, t0\n   addi s1, s1, 1\n   bltu s1, s0, 1b\n   li t0, 0\n"
        "2:  sd s1, 0(a1)\n   sd s2, 8(a1)\n   sw s3, 16(a1)\n   mv a0, t0\n   j 5f\n"
        "4:  li a0, 0\n"
        "5:  ld s0, 0(sp)\n   ld s1, 8(sp)\n   ld s2, 16(sp)\n   ld s3, 24(sp)\n   addi sp, sp, 32\n" SH_RX_ASM_RET
        "3:  li a0, 0\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_dfa_longest_fast)

        /* rx_single(a0=program, a1=node, a2=byte) -> 0/1 */
        SH_RX_ASM_FUNC(rx_single)
        "beqz a0, 4f\n   beqz a1, 4f\n   andi a2, a2, 0xff\n   lbu t0, 0(a1)\n"
        "li t1, 2\n   beq t0, t1, 1f\n   li t1, 3\n   beq t0, t1, 2f\n"
        "lbu t0, 31(a0)\n   andi t0, t0, 128\n   beqz t0, 3f\n"
        "addi t1, a2, -65\n   li t2, 25\n   bgtu t1, t2, 3f\n   ori a2, a2, 32\n"
        "3:  lhu t0, 2(a1)\n   sub t0, a2, t0\n   seqz a0, t0\n" SH_RX_ASM_RET
        "1:  lbu t0, 29(a0)\n   andi t0, t0, 1\n   bnez t0, 5f\n   addi t0, a2, -10\n   snez a0, t0\n" SH_RX_ASM_RET
        "2:  ld t0, 8(a0)\n   lhu t1, 2(a1)\n   slli t1, t1, 8\n   add t0, t0, t1\n"
        "add t0, t0, a2\n   lbu t0, 0(t0)\n   snez a0, t0\n" SH_RX_ASM_RET
        "4:  li a0, 0\n" SH_RX_ASM_RET
        "5:  li a0, 1\n" SH_RX_ASM_RET
        SH_RX_ASM_END(rx_single)
);

#endif

#endif /* SH_REGEX_DFA_ASM_IMPLEMENTATION */
