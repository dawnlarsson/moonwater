/*
        Compositor keymap lookup & dead-key compose state machine in pure assembly:
          - km_held_bit     : evdev modifier keycode -> held bitmask
          - km_held_flags   : held bitmask -> WINDOW_KEY_* modifier flags (branchless)
          - km_layout_index : 2-char layout name lookup (guard-page safe)
          - km_letter_flip  : ASCII + Latin-1 Caps Lock case flip
          - km_compose      : dead accent + base character -> composed codepoint
          - km_character    : US + layout patch + 4-level fallback + Ctrl/Caps evaluation
          - km_event        : full key press/repeat/release + dead-key state machine

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define
        COMPOSITOR_KEYMAP_EVAL_ASM_IMPLEMENTATION in one translation unit to emit
        the assembly routines.
*/
#ifndef MOONWATER_COMPOSITOR_KEYMAP_EVAL_ASM_H
#define MOONWATER_COMPOSITOR_KEYMAP_EVAL_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  km_u8;
typedef p16 km_u16;
typedef p32 km_u32;
typedef b32 km_i32;
typedef p64 km_u64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__)
typedef __UINT8_TYPE__  km_u8;
typedef __UINT16_TYPE__ km_u16;
typedef __UINT32_TYPE__ km_u32;
typedef __INT32_TYPE__  km_i32;
typedef __UINT64_TYPE__ km_u64;
#else
#include <stdint.h>
typedef uint8_t  km_u8;
typedef uint16_t km_u16;
typedef uint32_t km_u32;
typedef int32_t  km_i32;
typedef uint64_t km_u64;
#endif

#define KM_DEAD        0x10000u
#define KM_TABLE       99u
#define KM_LEVELS      4u
#define KM_CAPSLOCK    58u
#define KM_MAX_CODE    128u

#define KM_MOD_SHIFT   2u
#define KM_MOD_CONTROL 4u
#define KM_MOD_ALT     8u
#define KM_MOD_ALTGR   64u
#define KM_MOD_SUPER   256u

/* 20 bytes — byte-for-byte identical to struct compositor_patch */
struct km_patch {
        km_u8  code;       /* +0 */
        km_u32 level[4];   /* +4 */
};

/* 24 bytes — byte-for-byte identical to compositor_layouts[] entry */
struct km_layout {
        char                   name[3]; /* +0 */
        const struct km_patch *patch;   /* +8 */
        km_u8                  count;   /* +16 */
};

/* 28 bytes — byte-for-byte identical to compositor_composed[] entry */
struct km_composed {
        km_u16 accent;    /* +0 */
        char   bases[13]; /* +2 */
        km_u8  made[12];  /* +15 */
};

/* 24 bytes — byte-for-byte identical to struct compositor_keymap */
struct km_state {
        km_u8  layout;    /* +0 */
        km_u8  caps;      /* +1 */
        km_u32 dead;      /* +4 */
        km_u32 hidden[4]; /* +8 */
};

/* 32 bytes — table descriptor passed to km_character / km_event */
struct km_tables {
        const km_u8              *us;             /* +0  compositor_us[99][2] */
        const struct km_layout   *layouts;        /* +8  compositor_layouts[] */
        const struct km_composed *composed;       /* +16 compositor_composed[] */
        km_u32                    layout_count;   /* +24 count of layouts */
        km_u32                    composed_count; /* +28 count of composed rows */
};

#ifdef offset_of
#define KM_EVAL_OFFSET(T, M) offset_of(T, M)
#else
#define KM_EVAL_OFFSET(T, M) ((km_u64)&(((T *)0)->M))
#endif

_Static_assert(sizeof(struct km_patch)    == 20, "km_patch must be 20 bytes");
_Static_assert(KM_EVAL_OFFSET(struct km_patch, code)     == 0,  "km_patch.code");
_Static_assert(KM_EVAL_OFFSET(struct km_patch, level)    == 4,  "km_patch.level");
_Static_assert(sizeof(struct km_layout)   == 24, "km_layout must be 24 bytes");
_Static_assert(KM_EVAL_OFFSET(struct km_layout, name)    == 0,  "km_layout.name");
_Static_assert(KM_EVAL_OFFSET(struct km_layout, patch)   == 8,  "km_layout.patch");
_Static_assert(KM_EVAL_OFFSET(struct km_layout, count)   == 16, "km_layout.count");
_Static_assert(sizeof(struct km_composed) == 28, "km_composed must be 28 bytes");
_Static_assert(KM_EVAL_OFFSET(struct km_composed, accent) == 0,  "km_composed.accent");
_Static_assert(KM_EVAL_OFFSET(struct km_composed, bases)  == 2,  "km_composed.bases");
_Static_assert(KM_EVAL_OFFSET(struct km_composed, made)   == 15, "km_composed.made");
_Static_assert(sizeof(struct km_state)    == 24, "km_state must be 24 bytes");
_Static_assert(KM_EVAL_OFFSET(struct km_state, layout)   == 0,  "km_state.layout");
_Static_assert(KM_EVAL_OFFSET(struct km_state, caps)     == 1,  "km_state.caps");
_Static_assert(KM_EVAL_OFFSET(struct km_state, dead)     == 4,  "km_state.dead");
_Static_assert(KM_EVAL_OFFSET(struct km_state, hidden)   == 8,  "km_state.hidden");
_Static_assert(sizeof(struct km_tables)   == 32, "km_tables must be 32 bytes");
_Static_assert(KM_EVAL_OFFSET(struct km_tables, us)             == 0,  "km_tables.us");
_Static_assert(KM_EVAL_OFFSET(struct km_tables, layouts)        == 8,  "km_tables.layouts");
_Static_assert(KM_EVAL_OFFSET(struct km_tables, composed)       == 16, "km_tables.composed");
_Static_assert(KM_EVAL_OFFSET(struct km_tables, layout_count)   == 24, "km_tables.layout_count");
_Static_assert(KM_EVAL_OFFSET(struct km_tables, composed_count) == 28, "km_tables.composed_count");

km_u32 km_held_bit(km_u32 code);
km_u32 km_held_flags(km_u32 held);
km_i32 km_layout_index(const char *name, const struct km_layout *layouts, km_u32 count);
km_u32 km_letter_flip(km_u32 c);
km_u32 km_compose(km_u32 accent, km_u32 c, const struct km_composed *table, km_u32 count);
km_u32 km_character(const struct km_tables *tables, const struct km_state *map, km_u32 code, km_u32 modifiers);
km_i32 km_event(const struct km_tables *tables, struct km_state *map, km_u32 code, km_i32 value, km_u32 modifiers, km_u32 out[2]);

#endif /* MOONWATER_COMPOSITOR_KEYMAP_EVAL_ASM_H */

#ifdef COMPOSITOR_KEYMAP_EVAL_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define KM_EVAL_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define KM_EVAL_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define KM_EVAL_RISCV64 1
# else
#  error "src/canvas/compositor/keymap_eval.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define KM_EVAL_ARM64 1
# elif defined(X64) && X64
#  define KM_EVAL_X64 1
# elif defined(RISCV64) && RISCV64
#  define KM_EVAL_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define KM_ASM_SECTION   ASM_SECTION
# define KM_ASM_FUNC(n)   ASM_FUNC(n)
# define KM_ASM_END(n)    ASM_END(n)
# define KM_ASM_RET       ASM_RET
#else
# define KM_ASM_SECTION   ".text\n"
# define KM_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define KM_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define KM_ASM_END(n)   ""
# elif defined(_WIN32)
#  define KM_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define KM_ASM_END(n)   ""
# else
#  if defined(KM_EVAL_ARM64)
#   define KM_ASM_FTYPE   "%function"
#  else
#   define KM_ASM_FTYPE   "@function"
#  endif
#  define KM_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " KM_ASM_FTYPE "\n" #n ":\n"
#  define KM_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(__APPLE__)
# define KM_ASM_SYM(n) "_" #n
#else
# define KM_ASM_SYM(n) #n
#endif

#if defined(KM_EVAL_ARM64)

__asm__(
        KM_ASM_SECTION

        /* km_held_bit(w0=code) -> held bit or 0 */
        KM_ASM_FUNC(km_held_bit)
        "cmp w0, #56\n   b.eq 5f\n   b.hi 3f\n   mov w1, #1\n   cmp w0, #42\n   b.eq 1f\n"
        "mov w1, #2\n   cmp w0, #54\n   b.eq 1f\n   mov w1, #4\n   cmp w0, #29\n   b.eq 1f\n   b 2f\n"
        "3:  mov w1, #8\n   cmp w0, #97\n   b.eq 1f\n   mov w1, #32\n   cmp w0, #100\n   b.eq 1f\n"
        "mov w1, #64\n   cmp w0, #125\n   b.eq 1f\n   mov w1, #128\n   cmp w0, #126\n   b.eq 1f\n"
        "2:  mov w1, #0\n"
        "1:  mov w0, w1\n" KM_ASM_RET
        "5:  mov w0, #16\n" KM_ASM_RET
        KM_ASM_END(km_held_bit)

        /* km_held_flags(w0=held) -> WINDOW_KEY_* flags (branchless) */
        KM_ASM_FUNC(km_held_flags)
        "orr w1, w0, w0, lsr #1\n   and w2, w1, #1\n   and w3, w1, #4\n   orr w2, w3, w2, lsl #1\n"
        "and w1, w1, #64\n   orr w2, w2, w1, lsl #2\n   lsr w3, w0, #1\n   and w3, w3, #8\n"
        "orr w2, w2, w3\n   lsl w0, w0, #1\n   and w0, w0, #64\n   orr w0, w2, w0\n" KM_ASM_RET
        KM_ASM_END(km_held_flags)

        /* km_layout_index(x0=name, x1=layouts, w2=count) -> index or -1 */
        KM_ASM_FUNC(km_layout_index)
        "cbz x0, 2f\n   cbz x1, 2f\n   cbz w2, 2f\n   ldrb w3, [x0]\n   cbz w3, 2f\n"
        "ldrb w4, [x0, #1]\n   cbz w4, 2f\n   ldrb w5, [x0, #2]\n   cbnz w5, 2f\n"
        "orr w3, w3, w4, lsl #8\n   mov w4, #0\n"
        "1:  ldrh w5, [x1]\n   cmp w5, w3\n   b.eq 3f\n   add x1, x1, #24\n"
        "add w4, w4, #1\n   cmp w4, w2\n   b.lo 1b\n"
        "2:  mov w0, #-1\n" KM_ASM_RET
        "3:  mov w0, w4\n" KM_ASM_RET
        KM_ASM_END(km_layout_index)

        /* km_letter_flip(w0=c) -> case-flipped ASCII/Latin-1 letter */
        KM_ASM_FUNC(km_letter_flip)
        "orr w1, w0, #0x20\n   sub w2, w1, #97\n   cmp w2, #26\n   b.lo 1f\n"
        "sub w2, w1, #0xe0\n   cmp w2, #31\n   b.hs 2f\n   cmp w1, #0xf7\n   b.eq 2f\n"
        "1:  eor w0, w0, #0x20\n"
        "2:  " KM_ASM_RET
        KM_ASM_END(km_letter_flip)

        /* km_compose(w0=accent, w1=c, x2=table, w3=count) -> composed or 0 */
        KM_ASM_FUNC(km_compose)
        "cmp w1, #32\n   b.eq 5f\n   cmp w1, w0\n   b.eq 5f\n   cbz x2, 4f\n   cbz w3, 4f\n"
        "cbz w1, 4f\n   cmp w1, #128\n   b.hs 4f\n"
        "1:  ldrh w4, [x2]\n   cmp w4, w0\n   b.ne 3f\n   add x5, x2, #2\n   add x7, x2, #14\n"
        "2:  ldrb w6, [x5], #1\n   cbz w6, 3f\n   cmp w6, w1\n   b.eq 6f\n   cmp x5, x7\n   b.lo 2b\n"
        "3:  add x2, x2, #28\n   subs w3, w3, #1\n   b.ne 1b\n"
        "4:  mov w0, #0\n" KM_ASM_RET
        "5:  " KM_ASM_RET
        "6:  ldrb w0, [x5, #12]\n" KM_ASM_RET
        KM_ASM_END(km_compose)

        /* km_character(x0=tables, x1=map, w2=code, w3=modifiers) -> char|KM_DEAD or 0 */
        KM_ASM_FUNC(km_character)
        "cbz x0, 9f\n   cbz x1, 9f\n   cmp w2, #99\n   b.hs 9f\n   ldr x4, [x0]\n   cbz x4, 9f\n"
        "add x5, x4, w2, uxtw #1\n   ldrb w6, [x5]\n   ldrb w7, [x5, #1]\n   mov w8, #0\n   mov w9, #0\n"
        "ldrb w10, [x1]\n   ldr w11, [x0, #24]\n   ldr x12, [x0, #8]\n   cbz x12, 2f\n   cmp w10, w11\n   b.hs 2f\n"
        "add x10, x10, x10, lsl #1\n   add x12, x12, x10, lsl #3\n   ldr x13, [x12, #8]\n   ldrb w14, [x12, #16]\n"
        "cbz x13, 2f\n   cbz w14, 2f\n"
        "1:  ldrb w15, [x13], #20\n   cmp w15, w2\n   b.eq 10f\n"
        "13: subs w14, w14, #1\n   b.ne 1b\n   b 2f\n"
        "10: ldp w6, w7, [x13, #-16]\n   ldp w8, w9, [x13, #-8]\n   b 13b\n"
        "2:  mov w0, w6\n   tbz w3, #1, 3f\n   cbz w7, 3f\n   mov w0, w7\n"
        "3:  tbz w3, #6, 4f\n   cbz w8, 11f\n   mov w0, w8\n"
        "11: tbz w3, #1, 4f\n   cbz w9, 4f\n   mov w0, w9\n"
        "4:  cbz w0, 9f\n   tbz w3, #2, 5f\n   cmp w0, #127\n   b.ls 5f\n   tbnz w0, #16, 5f\n"
        "ubfx w10, w3, #1, #1\n   ldrb w0, [x5, x10]\n"
        "5:  ldrb w10, [x1, #1]\n   cbz w10, 6f\n   tbnz w0, #16, 6f\n"
        "orr w11, w0, #0x20\n   sub w12, w11, #97\n   cmp w12, #26\n   b.lo 12f\n"
        "sub w12, w11, #0xe0\n   cmp w12, #31\n   b.hs 6f\n   cmp w11, #0xf7\n   b.eq 6f\n"
        "12: eor w0, w0, #0x20\n"
        "6:  tbz w3, #2, 7f\n   tbnz w0, #16, 7f\n   orr w11, w0, #0x20\n   sub w12, w11, #97\n"
        "cmp w12, #26\n   b.hs 7f\n   and w0, w0, #0x1f\n"
        "7:  " KM_ASM_RET
        "9:  mov w0, #0\n" KM_ASM_RET
        KM_ASM_END(km_character)

        /* km_event(x0=tables, x1=map, w2=code, w3=value, w4=modifiers, x5=out) -> -1, 0, 1, 2 */
        KM_ASM_FUNC(km_event)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x5, 9f\n   cmp w2, #128\n   b.hs 9f\n"
        "lsr w6, w2, #5\n   mov w7, #1\n   lsl w7, w7, w2\n   add x8, x1, #8\n"
        "ldr w9, [x8, x6, lsl #2]\n   tst w9, w7\n   cbnz w3, 1f\n"
        "bic w9, w9, w7\n   str w9, [x8, x6, lsl #2]\n   csinv w0, wzr, wzr, eq\n" KM_ASM_RET
        "1:  b.ne 8f\n   cmp w2, #58\n   b.ne 2f\n   cmp w3, #1\n   b.ne 9f\n"
        "ldrb w9, [x1, #1]\n   eor w9, w9, #1\n   strb w9, [x1, #1]\n   b 9f\n"
        "2:  stp x29, x30, [sp, #-48]!\n   mov x29, sp\n   stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n"
        "mov x19, x0\n   mov x20, x1\n   mov x21, x5\n   mov w22, w2\n   mov w3, w4\n   bl " KM_ASM_SYM(km_character) "\n"
        "cbz w0, 6f\n   tbz w0, #16, 3f\n   bic w1, w0, #0x10000\n   ldr w2, [x20, #4]\n   cbnz w2, 10f\n"
        "lsr w3, w22, #5\n   mov w4, #1\n   lsl w4, w4, w22\n   add x5, x20, #8\n"
        "ldr w6, [x5, x3, lsl #2]\n   orr w6, w6, w4\n   str w6, [x5, x3, lsl #2]\n"
        "str w1, [x20, #4]\n   mov w0, #-1\n   b 7f\n"
        "10: cmp w2, w1\n   csel w1, wzr, w1, eq\n   str w1, [x20, #4]\n   str w2, [x21]\n   mov w0, #1\n   b 7f\n"
        "3:  ldr w1, [x20, #4]\n   cbz w1, 4f\n   cmp w0, #32\n   b.lo 5f\n   cmp w0, #127\n   b.eq 5f\n"
        "mov w22, w0\n   mov w0, w1\n   mov w1, w22\n   ldr x2, [x19, #16]\n   ldr w3, [x19, #28]\n"
        "mov w19, w0\n   bl " KM_ASM_SYM(km_compose) "\n   str wzr, [x20, #4]\n   cbz w0, 11f\n"
        "str w0, [x21]\n   mov w0, #1\n   b 7f\n"
        "11: stp w19, w22, [x21]\n   mov w0, #2\n   b 7f\n"
        "5:  str wzr, [x20, #4]\n"
        "4:  str w0, [x21]\n   mov w0, #1\n   b 7f\n"
        "6:  mov w0, #0\n"
        "7:  ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #48\n" KM_ASM_RET
        "8:  mov w0, #-1\n" KM_ASM_RET
        "9:  mov w0, #0\n" KM_ASM_RET
        KM_ASM_END(km_event)
);

#elif defined(KM_EVAL_X64)

#if defined(_WIN32)
#define KM_X64_A1D ecx
#define KM_X64_A1Q rcx
#define KM_X64_A2D edx
#define KM_X64_A2Q rdx
#define KM_X64_A3D r8d
#define KM_X64_A3Q r8
#define KM_X64_A4D r9d
#define KM_X64_A4Q r9
#else
#define KM_X64_A1D edi
#define KM_X64_A1Q rdi
#define KM_X64_A2D esi
#define KM_X64_A2Q rsi
#define KM_X64_A3D edx
#define KM_X64_A3Q rdx
#define KM_X64_A4D ecx
#define KM_X64_A4Q rcx
#endif
#define KM_STR1(x) #x
#define KM_STR(x)  KM_STR1(x)

__asm__(
        KM_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* km_held_bit(code) -> held bit or 0 */
        KM_ASM_FUNC(km_held_bit)
        "lea edx, [" KM_STR(KM_X64_A1D) " - 29]\n   cmp edx, 97\n   ja 2f\n"
        "mov eax, 1\n   cmp " KM_STR(KM_X64_A1D) ", 42\n   je 1f\n   mov eax, 2\n   cmp " KM_STR(KM_X64_A1D) ", 54\n   je 1f\n"
        "mov eax, 4\n   cmp " KM_STR(KM_X64_A1D) ", 29\n   je 1f\n   mov eax, 8\n   cmp " KM_STR(KM_X64_A1D) ", 97\n   je 1f\n"
        "mov eax, 16\n   cmp " KM_STR(KM_X64_A1D) ", 56\n   je 1f\n   mov eax, 32\n   cmp " KM_STR(KM_X64_A1D) ", 100\n   je 1f\n"
        "mov eax, 64\n   cmp " KM_STR(KM_X64_A1D) ", 125\n   je 1f\n   mov eax, 128\n   cmp " KM_STR(KM_X64_A1D) ", 126\n   je 1f\n"
        "2:  xor eax, eax\n"
        "1:  " KM_ASM_RET
        KM_ASM_END(km_held_bit)

        /* km_held_flags(held) -> WINDOW_KEY_* flags (branchless) */
        KM_ASM_FUNC(km_held_flags)
        "mov r8d, " KM_STR(KM_X64_A1D) "\n   mov eax, r8d\n   shr eax, 1\n   or eax, r8d\n"
        "mov r9d, eax\n   and r9d, 1\n   add r9d, r9d\n   mov r10d, eax\n   and r10d, 4\n   or r9d, r10d\n"
        "and eax, 64\n   shl eax, 2\n   or eax, r9d\n   mov r9d, r8d\n   shr r9d, 1\n   and r9d, 8\n"
        "or eax, r9d\n   add r8d, r8d\n   and r8d, 64\n   or eax, r8d\n" KM_ASM_RET
        KM_ASM_END(km_held_flags)

        /* km_layout_index(name, layouts, count) -> index or -1 */
        KM_ASM_FUNC(km_layout_index)
        "mov r10, " KM_STR(KM_X64_A1Q) "\n   mov r11, " KM_STR(KM_X64_A2Q) "\n   mov r9d, " KM_STR(KM_X64_A3D) "\n"
        "test r10, r10\n   jz 2f\n   test r11, r11\n   jz 2f\n   test r9d, r9d\n   jz 2f\n"
        "movzx r8d, byte ptr [r10]\n   test r8d, r8d\n   jz 2f\n   movzx eax, byte ptr [r10 + 1]\n   test eax, eax\n   jz 2f\n"
        "cmp byte ptr [r10 + 2], 0\n   jne 2f\n   shl eax, 8\n   or r8d, eax\n   xor eax, eax\n"
        "1:  cmp word ptr [r11], r8w\n   je 3f\n   add r11, 24\n   inc eax\n   cmp eax, r9d\n   jb 1b\n"
        "2:  mov eax, -1\n"
        "3:  " KM_ASM_RET
        KM_ASM_END(km_layout_index)

        /* km_letter_flip(c) -> case-flipped ASCII/Latin-1 letter */
        KM_ASM_FUNC(km_letter_flip)
        "mov eax, " KM_STR(KM_X64_A1D) "\n   mov r8d, eax\n   or r8d, 32\n   lea r9d, [r8d - 97]\n   cmp r9d, 26\n   jb 1f\n"
        "lea r9d, [r8d - 224]\n   cmp r9d, 31\n   jae 2f\n   cmp r8d, 247\n   je 2f\n"
        "1:  xor eax, 32\n"
        "2:  " KM_ASM_RET
        KM_ASM_END(km_letter_flip)

        /* km_compose(accent, c, table, count) -> composed or 0 */
        KM_ASM_FUNC(km_compose)
        "mov eax, " KM_STR(KM_X64_A1D) "\n   mov r10d, " KM_STR(KM_X64_A2D) "\n   mov r11, " KM_STR(KM_X64_A3Q) "\n   mov r9d, " KM_STR(KM_X64_A4D) "\n"
        "cmp r10d, 32\n   je 5f\n   cmp r10d, eax\n   je 5f\n   test r11, r11\n   jz 4f\n   test r9d, r9d\n   jz 4f\n"
        "test r10d, r10d\n   jz 4f\n   cmp r10d, 128\n   jae 4f\n"
        "1:  movzx r8d, word ptr [r11]\n   cmp r8d, eax\n   jne 3f\n   xor r8d, r8d\n"
        "2:  movzx ecx, byte ptr [r11 + r8 + 2]\n   test ecx, ecx\n   jz 3f\n   cmp ecx, r10d\n   je 6f\n"
        "inc r8\n   cmp r8, 12\n   jb 2b\n"
        "3:  add r11, 28\n   dec r9d\n   jnz 1b\n"
        "4:  xor eax, eax\n"
        "5:  " KM_ASM_RET
        "6:  movzx eax, byte ptr [r11 + r8 + 15]\n" KM_ASM_RET
        KM_ASM_END(km_compose)

        /* km_character(tables, map, code, modifiers) -> char|KM_DEAD or 0 */
        KM_ASM_FUNC(km_character)
#if defined(_WIN32)
        "push rbx\n   push rbp\n   push rsi\n   push rdi\n   push r12\n   mov r10, rcx\n   mov r11, rdx\n   mov r12d, r8d\n   mov ebp, r9d\n"
#else
        "push rbx\n   push rbp\n   push r12\n   mov r10, rdi\n   mov r11, rsi\n   mov r12d, edx\n   mov ebp, ecx\n"
#endif
        "test r10, r10\n   jz 9f\n   test r11, r11\n   jz 9f\n   cmp r12d, 99\n   jae 9f\n"
        "mov rax, [r10]\n   test rax, rax\n   jz 9f\n   lea rbx, [rax + r12 * 2]\n"
        "movzx eax, byte ptr [rbx]\n   movzx ecx, byte ptr [rbx + 1]\n   xor edx, edx\n   xor r8d, r8d\n"
        "movzx r9d, byte ptr [r11]\n   cmp r9d, dword ptr [r10 + 24]\n   jae 2f\n   mov r10, [r10 + 8]\n   test r10, r10\n   jz 2f\n"
        "lea r9, [r9 + r9 * 2]\n   lea r10, [r10 + r9 * 8]\n   movzx r9d, byte ptr [r10 + 16]\n   mov r10, [r10 + 8]\n"
        "test r10, r10\n   jz 2f\n   test r9d, r9d\n   jz 2f\n"
        "1:  movzx edi, byte ptr [r10]\n   cmp edi, r12d\n   jne 10f\n"
        "mov eax, dword ptr [r10 + 4]\n   mov ecx, dword ptr [r10 + 8]\n   mov edx, dword ptr [r10 + 12]\n   mov r8d, dword ptr [r10 + 16]\n"
        "10: add r10, 20\n   dec r9d\n   jnz 1b\n"
        "2:  test ebp, 2\n   jz 3f\n   test ecx, ecx\n   cmovnz eax, ecx\n"
        "3:  test ebp, 64\n   jz 4f\n   test edx, edx\n   cmovnz eax, edx\n   test ebp, 2\n   jz 4f\n   test r8d, r8d\n   cmovnz eax, r8d\n"
        "4:  test eax, eax\n   jz 9f\n   test ebp, 4\n   jz 5f\n   cmp eax, 127\n   jbe 5f\n   test eax, 0x10000\n   jnz 5f\n"
        "mov ecx, ebp\n   shr ecx, 1\n   and ecx, 1\n   movzx eax, byte ptr [rbx + rcx]\n"
        "5:  cmp byte ptr [r11 + 1], 0\n   je 6f\n   test eax, 0x10000\n   jnz 6f\n"
        "mov ecx, eax\n   or ecx, 32\n   lea edx, [ecx - 97]\n   cmp edx, 26\n   jb 12f\n"
        "lea edx, [ecx - 224]\n   cmp edx, 31\n   jae 6f\n   cmp ecx, 247\n   je 6f\n"
        "12: xor eax, 32\n"
        "6:  test ebp, 4\n   jz 7f\n   test eax, 0x10000\n   jnz 7f\n   mov ecx, eax\n   or ecx, 32\n"
        "lea edx, [ecx - 97]\n   cmp edx, 26\n   jae 7f\n   and eax, 31\n   jmp 7f\n"
        "9:  xor eax, eax\n"
#if defined(_WIN32)
        "7:  pop r12\n   pop rdi\n   pop rsi\n   pop rbp\n   pop rbx\n" KM_ASM_RET
#else
        "7:  pop r12\n   pop rbp\n   pop rbx\n" KM_ASM_RET
#endif
        KM_ASM_END(km_character)

        /* km_event(tables, map, code, value, modifiers, out) -> -1, 0, 1, 2 */
        KM_ASM_FUNC(km_event)
#if defined(_WIN32)
        "mov r10d, dword ptr [rsp + 40]\n   mov r11, qword ptr [rsp + 48]\n"
        "test rcx, rcx\n   jz 9f\n   test rdx, rdx\n   jz 9f\n   test r11, r11\n   jz 9f\n   cmp r8d, 128\n   jae 9f\n"
        "test r9d, r9d\n   jnz 1f\n   btr dword ptr [rdx + 8], r8d\n   sbb eax, eax\n" KM_ASM_RET
        "1:  bt dword ptr [rdx + 8], r8d\n   jc 8f\n   cmp r8d, 58\n   jne 2f\n"
        "cmp r9d, 1\n   jne 9f\n   xor byte ptr [rdx + 1], 1\n   jmp 9f\n"
        "2:  push rbx\n   push rbp\n   push r12\n   push r13\n   sub rsp, 40\n"
        "mov rbx, rcx\n   mov r12, rdx\n   mov r13, r11\n   mov ebp, r8d\n   mov r9d, r10d\n   call " KM_ASM_SYM(km_character) "\n"
        "test eax, eax\n   jz 6f\n   test eax, 0x10000\n   jz 4f\n"
        "and eax, 0xfffeffff\n   mov edx, dword ptr [r12 + 4]\n   test edx, edx\n   jnz 10f\n"
        "bts dword ptr [r12 + 8], ebp\n   mov dword ptr [r12 + 4], eax\n   mov eax, -1\n   jmp 7f\n"
        "10: xor ecx, ecx\n   cmp edx, eax\n   cmove eax, ecx\n   mov dword ptr [r12 + 4], eax\n"
        "mov dword ptr [r13], edx\n   mov eax, 1\n   jmp 7f\n"
        "4:  mov ebp, dword ptr [r12 + 4]\n   test ebp, ebp\n   jz 5f\n   cmp eax, 32\n   jb 11f\n   cmp eax, 127\n   je 11f\n"
        "mov ecx, ebp\n   mov edx, eax\n   mov r8, [rbx + 16]\n   mov r9d, dword ptr [rbx + 28]\n   mov ebx, eax\n"
        "call " KM_ASM_SYM(km_compose) "\n   mov dword ptr [r12 + 4], 0\n   test eax, eax\n   jz 12f\n"
        "mov dword ptr [r13], eax\n   mov eax, 1\n   jmp 7f\n"
        "12: mov dword ptr [r13], ebp\n   mov dword ptr [r13 + 4], ebx\n   mov eax, 2\n   jmp 7f\n"
        "11: mov dword ptr [r12 + 4], 0\n"
        "5:  mov dword ptr [r13], eax\n   mov eax, 1\n   jmp 7f\n"
        "6:  xor eax, eax\n"
        "7:  add rsp, 40\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" KM_ASM_RET
        "8:  mov eax, -1\n" KM_ASM_RET
        "9:  xor eax, eax\n" KM_ASM_RET
#else
        "test rdi, rdi\n   jz 9f\n   test rsi, rsi\n   jz 9f\n   test r9, r9\n   jz 9f\n   cmp edx, 128\n   jae 9f\n"
        "test ecx, ecx\n   jnz 1f\n   btr dword ptr [rsi + 8], edx\n   sbb eax, eax\n" KM_ASM_RET
        "1:  bt dword ptr [rsi + 8], edx\n   jc 8f\n   cmp edx, 58\n   jne 2f\n"
        "cmp ecx, 1\n   jne 9f\n   xor byte ptr [rsi + 1], 1\n   jmp 9f\n"
        "2:  push rbx\n   push rbp\n   push r12\n   push r13\n   push r14\n"
        "mov rbx, rdi\n   mov r12, rsi\n   mov r13, r9\n   mov r14d, edx\n   mov ecx, r8d\n   call " KM_ASM_SYM(km_character) "\n"
        "test eax, eax\n   jz 6f\n   test eax, 0x10000\n   jz 4f\n"
        "and eax, 0xfffeffff\n   mov edx, dword ptr [r12 + 4]\n   test edx, edx\n   jnz 10f\n"
        "bts dword ptr [r12 + 8], r14d\n   mov dword ptr [r12 + 4], eax\n   mov eax, -1\n   jmp 7f\n"
        "10: xor ecx, ecx\n   cmp edx, eax\n   cmove eax, ecx\n   mov dword ptr [r12 + 4], eax\n"
        "mov dword ptr [r13], edx\n   mov eax, 1\n   jmp 7f\n"
        "4:  mov ebp, dword ptr [r12 + 4]\n   test ebp, ebp\n   jz 5f\n   cmp eax, 32\n   jb 11f\n   cmp eax, 127\n   je 11f\n"
        "mov r14d, eax\n   mov edi, ebp\n   mov esi, eax\n   mov rdx, [rbx + 16]\n   mov ecx, dword ptr [rbx + 28]\n"
        "call " KM_ASM_SYM(km_compose) "\n   mov dword ptr [r12 + 4], 0\n   test eax, eax\n   jz 12f\n"
        "mov dword ptr [r13], eax\n   mov eax, 1\n   jmp 7f\n"
        "12: mov dword ptr [r13], ebp\n   mov dword ptr [r13 + 4], r14d\n   mov eax, 2\n   jmp 7f\n"
        "11: mov dword ptr [r12 + 4], 0\n"
        "5:  mov dword ptr [r13], eax\n   mov eax, 1\n   jmp 7f\n"
        "6:  xor eax, eax\n"
        "7:  pop r14\n   pop r13\n   pop r12\n   pop rbp\n   pop rbx\n" KM_ASM_RET
        "8:  mov eax, -1\n" KM_ASM_RET
        "9:  xor eax, eax\n" KM_ASM_RET
#endif
        KM_ASM_END(km_event)

        ".att_syntax prefix\n"
);

#elif defined(KM_EVAL_RISCV64)

__asm__(
        KM_ASM_SECTION

        /* km_held_bit(a0=code) -> held bit or 0 */
        KM_ASM_FUNC(km_held_bit)
        "slli t0, a0, 32\n   srli t0, t0, 32\n   addi t1, t0, -29\n   li t2, 97\n   bgtu t1, t2, 2f\n"
        "li a0, 1\n   li t2, 42\n   beq t0, t2, 1f\n   li a0, 2\n   li t2, 54\n   beq t0, t2, 1f\n"
        "li a0, 4\n   li t2, 29\n   beq t0, t2, 1f\n   li a0, 8\n   li t2, 97\n   beq t0, t2, 1f\n"
        "li a0, 16\n   li t2, 56\n   beq t0, t2, 1f\n   li a0, 32\n   li t2, 100\n   beq t0, t2, 1f\n"
        "li a0, 64\n   li t2, 125\n   beq t0, t2, 1f\n   li a0, 128\n   li t2, 126\n   beq t0, t2, 1f\n"
        "2:  li a0, 0\n"
        "1:  " KM_ASM_RET
        KM_ASM_END(km_held_bit)

        /* km_held_flags(a0=held) -> WINDOW_KEY_* flags (branchless) */
        KM_ASM_FUNC(km_held_flags)
        "srliw t0, a0, 1\n   or t1, a0, t0\n   andi t2, t1, 1\n   slli t2, t2, 1\n   andi t3, t1, 4\n   or t2, t2, t3\n"
        "andi t1, t1, 64\n   slli t1, t1, 2\n   or t2, t2, t1\n   andi t0, t0, 8\n   or t2, t2, t0\n"
        "slliw a0, a0, 1\n   andi a0, a0, 64\n   or a0, a0, t2\n" KM_ASM_RET
        KM_ASM_END(km_held_flags)

        /* km_layout_index(a0=name, a1=layouts, a2=count) -> index or -1 */
        KM_ASM_FUNC(km_layout_index)
        "slli a2, a2, 32\n   srli a2, a2, 32\n   beqz a0, 2f\n   beqz a1, 2f\n   beqz a2, 2f\n"
        "lbu t0, 0(a0)\n   beqz t0, 2f\n   lbu t1, 1(a0)\n   beqz t1, 2f\n   lbu t2, 2(a0)\n   bnez t2, 2f\n"
        "slli t1, t1, 8\n   or t0, t0, t1\n   li t3, 0\n"
        "1:  lhu t4, 0(a1)\n   beq t4, t0, 3f\n   addi a1, a1, 24\n   addi t3, t3, 1\n   bltu t3, a2, 1b\n"
        "2:  li a0, -1\n" KM_ASM_RET
        "3:  sext.w a0, t3\n" KM_ASM_RET
        KM_ASM_END(km_layout_index)

        /* km_letter_flip(a0=c) -> case-flipped ASCII/Latin-1 letter */
        KM_ASM_FUNC(km_letter_flip)
        "slli t0, a0, 32\n   srli t0, t0, 32\n   ori t1, t0, 32\n   addi t2, t1, -97\n   li t3, 26\n   bltu t2, t3, 1f\n"
        "addi t2, t1, -224\n   li t3, 31\n   bgeu t2, t3, 2f\n   li t3, 247\n   beq t1, t3, 2f\n"
        "1:  xori t0, t0, 32\n"
        "2:  sext.w a0, t0\n" KM_ASM_RET
        KM_ASM_END(km_letter_flip)

        /* km_compose(a0=accent, a1=c, a2=table, a3=count) -> composed or 0 */
        KM_ASM_FUNC(km_compose)
        "slli a0, a0, 32\n   srli a0, a0, 32\n   slli a1, a1, 32\n   srli a1, a1, 32\n   slli a3, a3, 32\n   srli a3, a3, 32\n"
        "li t0, 32\n   beq a1, t0, 5f\n   beq a1, a0, 5f\n   beqz a2, 4f\n   beqz a3, 4f\n   beqz a1, 4f\n"
        "li t0, 128\n   bgeu a1, t0, 4f\n   li t5, 12\n"
        "1:  lhu t1, 0(a2)\n   bne t1, a0, 3f\n   li t2, 0\n"
        "2:  add t3, a2, t2\n   lbu t4, 2(t3)\n   beqz t4, 3f\n   beq t4, a1, 6f\n"
        "addi t2, t2, 1\n   bltu t2, t5, 2b\n"
        "3:  addi a2, a2, 28\n   addi a3, a3, -1\n   bnez a3, 1b\n"
        "4:  li a0, 0\n" KM_ASM_RET
        "5:  sext.w a0, a0\n" KM_ASM_RET
        "6:  add t3, a2, t2\n   lbu a0, 15(t3)\n" KM_ASM_RET
        KM_ASM_END(km_compose)

        /* km_character(a0=tables, a1=map, a2=code, a3=modifiers) -> char|KM_DEAD or 0 */
        KM_ASM_FUNC(km_character)
        "slli a2, a2, 32\n   srli a2, a2, 32\n   beqz a0, 9f\n   beqz a1, 9f\n   li t0, 99\n   bgeu a2, t0, 9f\n"
        "ld t1, 0(a0)\n   beqz t1, 9f\n   slli t0, a2, 1\n   add t1, t1, t0\n"
        "lbu a4, 0(t1)\n   lbu a5, 1(t1)\n   li a6, 0\n   li a7, 0\n"
        "lbu t2, 0(a1)\n   lwu t3, 24(a0)\n   ld t4, 8(a0)\n   beqz t4, 2f\n   bgeu t2, t3, 2f\n"
        "slli t3, t2, 1\n   add t2, t2, t3\n   slli t2, t2, 3\n   add t4, t4, t2\n"
        "ld t5, 8(t4)\n   lbu t6, 16(t4)\n   beqz t5, 2f\n   beqz t6, 2f\n"
        "1:  lbu t2, 0(t5)\n   bne t2, a2, 10f\n   lwu a4, 4(t5)\n   lwu a5, 8(t5)\n   lwu a6, 12(t5)\n   lwu a7, 16(t5)\n"
        "10: addi t5, t5, 20\n   addi t6, t6, -1\n   bnez t6, 1b\n"
        "2:  mv t0, a4\n   andi t2, a3, 2\n   beqz t2, 3f\n   beqz a5, 3f\n   mv t0, a5\n"
        "3:  andi t3, a3, 64\n   beqz t3, 4f\n   beqz a6, 11f\n   mv t0, a6\n"
        "11: beqz t2, 4f\n   beqz a7, 4f\n   mv t0, a7\n"
        "4:  beqz t0, 9f\n   andi t3, a3, 4\n   beqz t3, 5f\n   li t4, 128\n   bltu t0, t4, 5f\n"
        "srli t4, t0, 16\n   andi t4, t4, 1\n   bnez t4, 5f\n   srli t2, t2, 1\n   add t4, t1, t2\n   lbu t0, 0(t4)\n"
        "5:  lbu t4, 1(a1)\n   beqz t4, 6f\n   srli t4, t0, 16\n   andi t4, t4, 1\n   bnez t4, 6f\n"
        "ori t4, t0, 32\n   addi t5, t4, -97\n   li t6, 26\n   bltu t5, t6, 12f\n"
        "addi t5, t4, -224\n   li t6, 31\n   bgeu t5, t6, 6f\n   li t6, 247\n   beq t4, t6, 6f\n"
        "12: xori t0, t0, 32\n"
        "6:  beqz t3, 7f\n   srli t4, t0, 16\n   andi t4, t4, 1\n   bnez t4, 7f\n"
        "ori t4, t0, 32\n   addi t5, t4, -97\n   li t6, 26\n   bgeu t5, t6, 7f\n   andi t0, t0, 31\n"
        "7:  sext.w a0, t0\n" KM_ASM_RET
        "9:  li a0, 0\n" KM_ASM_RET
        KM_ASM_END(km_character)

        /* km_event(a0=tables, a1=map, a2=code, a3=value, a4=modifiers, a5=out) -> -1, 0, 1, 2 */
        KM_ASM_FUNC(km_event)
        "slli a2, a2, 32\n   srli a2, a2, 32\n   beqz a0, 9f\n   beqz a1, 9f\n   beqz a5, 9f\n   li t0, 128\n   bgeu a2, t0, 9f\n"
        "srli t0, a2, 5\n   slli t0, t0, 2\n   add t0, a1, t0\n   li t1, 1\n   sllw t1, t1, a2\n"
        "lwu t2, 8(t0)\n   and t3, t2, t1\n   bnez a3, 1f\n"
        "not t1, t1\n   and t2, t2, t1\n   sw t2, 8(t0)\n   snez a0, t3\n   negw a0, a0\n" KM_ASM_RET
        "1:  bnez t3, 8f\n   li t0, 58\n   bne a2, t0, 2f\n   li t0, 1\n   bne a3, t0, 9f\n"
        "lbu t1, 1(a1)\n   xori t1, t1, 1\n   sb t1, 1(a1)\n   j 9f\n"
        "2:  addi sp, sp, -48\n   sd ra, 40(sp)\n   sd s0, 32(sp)\n   sd s1, 24(sp)\n   sd s2, 16(sp)\n   sd s3, 8(sp)\n"
        "mv s0, a0\n   mv s1, a1\n   mv s2, a5\n   mv s3, a2\n   mv a3, a4\n   call " KM_ASM_SYM(km_character) "\n"
        "slli a0, a0, 32\n   srli a0, a0, 32\n   beqz a0, 6f\n   srli t0, a0, 16\n   andi t0, t0, 1\n   beqz t0, 3f\n"
        "li t0, 0x10000\n   xor t1, a0, t0\n   lwu t2, 4(s1)\n   bnez t2, 10f\n"
        "srli t3, s3, 5\n   slli t3, t3, 2\n   add t3, s1, t3\n   li t4, 1\n   sllw t4, t4, s3\n"
        "lwu t5, 8(t3)\n   or t5, t5, t4\n   sw t5, 8(t3)\n   sw t1, 4(s1)\n   li a0, -1\n   j 7f\n"
        "10: bne t2, t1, 13f\n   li t1, 0\n"
        "13: sw t1, 4(s1)\n   sw t2, 0(s2)\n   li a0, 1\n   j 7f\n"
        "3:  lwu t1, 4(s1)\n   beqz t1, 4f\n   li t2, 32\n   bltu a0, t2, 5f\n   li t2, 127\n   beq a0, t2, 5f\n"
        "mv s3, a0\n   mv a1, a0\n   mv a0, t1\n   ld a2, 16(s0)\n   lwu a3, 28(s0)\n   mv s0, t1\n"
        "call " KM_ASM_SYM(km_compose) "\n   sw zero, 4(s1)\n   beqz a0, 11f\n"
        "sw a0, 0(s2)\n   li a0, 1\n   j 7f\n"
        "11: sw s0, 0(s2)\n   sw s3, 4(s2)\n   li a0, 2\n   j 7f\n"
        "5:  sw zero, 4(s1)\n"
        "4:  sw a0, 0(s2)\n   li a0, 1\n   j 7f\n"
        "6:  li a0, 0\n"
        "7:  ld s3, 8(sp)\n   ld s2, 16(sp)\n   ld s1, 24(sp)\n   ld s0, 32(sp)\n   ld ra, 40(sp)\n   addi sp, sp, 48\n" KM_ASM_RET
        "8:  li a0, -1\n" KM_ASM_RET
        "9:  li a0, 0\n" KM_ASM_RET
        KM_ASM_END(km_event)
);

#endif /* arch */
#endif /* COMPOSITOR_KEYMAP_EVAL_ASM_IMPLEMENTATION */
