/*
        Moonwater Canvas X11 wire codec core primitives in pure assembly:
          - xwm_asm_request_start        : bounded request header + zero-fill
          - xwm_asm_request_words        : bounded request header + u32 word payload
          - xwm_asm_request_name         : bounded request header + padded name payload
          - xwm_asm_event_words          : 32-byte X11 event builder + u32 word payload
          - xwm_asm_enc_configure_window : ConfigureWindow validator + sparse word packer
          - xwm_asm_enc_change_window_attributes : ChangeWindowAttributes popcount + packer
          - xwm_asm_server_message_size  : overflow-safe X11 reply/GenericEvent framing
          - xwm_asm_setup_size           : overflow-safe X11 setup reply framing
          - xwm_asm_parse_setup          : full X11 ConnectionSetup parser & bounds verifier
          - xwm_asm_parse_error          : X11 Error packet parser
          - xwm_asm_parse_atom_reply     : InternAtom reply parser
          - xwm_asm_parse_atom_name_reply: GetAtomName reply parser
          - xwm_asm_parse_property_reply : GetProperty reply parser (8/16/32-bit overflow-safe)
          - xwm_asm_parse_geometry_reply : GetGeometry reply parser (signed i16 x/y)
          - xwm_asm_parse_attributes_reply: GetWindowAttributes reply parser
          - xwm_asm_parse_extension_reply: QueryExtension reply parser
          - xwm_asm_parse_xres_client_pid: XRes QueryClientIds reply parser
          - xwm_asm_parse_event          : zero-table X11 core event unpacker (all 18 opcodes)
          - xwm_asm_parse_xfixes_selection: XFIXES SelectionNotify unpacker
          - xwm_asm_latin1_utf8          : Latin-1 to UTF-8 transcoder
          - xwm_asm_utf8_latin1          : stateful UTF-8 to Latin-1 transcoder

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define XWM_WIRE_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_CANVAS_XWM_WIRE_ASM_H
#define MOONWATER_CANVAS_XWM_WIRE_ASM_H

#ifdef STANDARD_MODERN_C
typedef p32 xwm_u32;
typedef b32 xwm_i32;
typedef p64 xwm_u64;
typedef b64 xwm_i64;
#elif defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT32_TYPE__ xwm_u32;
typedef __INT32_TYPE__  xwm_i32;
typedef __UINT64_TYPE__ xwm_u64;
typedef __INT64_TYPE__  xwm_i64;
#else
#include <stdint.h>
typedef uint32_t xwm_u32;
typedef int32_t  xwm_i32;
typedef uint64_t xwm_u64;
typedef int64_t  xwm_i64;
#endif

xwm_i64 xwm_asm_request_start(unsigned char *out, xwm_u64 cap, xwm_u64 bytes, xwm_u32 opcode);
xwm_i64 xwm_asm_request_words(unsigned char *out, xwm_u64 cap, xwm_u32 op_data, const xwm_u32 *words, xwm_u64 count);
xwm_i64 xwm_asm_request_name(unsigned char *out, xwm_u64 cap, xwm_u32 op_data, const unsigned char *name, xwm_u64 name_len, xwm_u64 max_len);
xwm_i64 xwm_asm_event_words(unsigned char *out, xwm_u64 cap, xwm_u32 type_detail, const xwm_u32 *words, xwm_u64 count);
xwm_i64 xwm_asm_enc_configure_window(unsigned char *out, xwm_u64 cap, xwm_u32 window, const void *cfg);
xwm_i64 xwm_asm_enc_change_window_attributes(unsigned char *out, xwm_u64 cap, xwm_u32 window, xwm_u32 mask, const xwm_u32 *values, xwm_u64 count);
xwm_i64 xwm_asm_server_message_size(const unsigned char *buf, xwm_u64 len);
xwm_i64 xwm_asm_setup_size(const unsigned char *buf, xwm_u64 len);
xwm_i32 xwm_asm_parse_setup(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_error(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_atom_reply(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_atom_name_reply(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_property_reply(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_geometry_reply(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_attributes_reply(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_extension_reply(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_xres_client_pid(const unsigned char *buf, xwm_u64 len, xwm_u32 *pid);
xwm_i32 xwm_asm_parse_event(const unsigned char *buf, xwm_u64 len, void *out);
xwm_i32 xwm_asm_parse_xfixes_selection(const unsigned char *buf, xwm_u64 len, xwm_u32 first_event, void *out);
xwm_u64 xwm_asm_latin1_utf8(const unsigned char *in, xwm_u64 n, unsigned char *out);
xwm_u64 xwm_asm_utf8_latin1(void *cut, const unsigned char *in, xwm_u64 n, unsigned char *out);

#endif /* MOONWATER_CANVAS_XWM_WIRE_ASM_H */

#ifdef XWM_WIRE_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define XWM_ASM_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define XWM_ASM_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define XWM_ASM_RISCV64 1
# else
#  error "src/canvas/xwm_wire.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define XWM_ASM_ARM64 1
# elif defined(X64) && X64
#  define XWM_ASM_X64 1
# elif defined(RISCV64) && RISCV64
#  define XWM_ASM_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define XWM_ASM_SECTION  ASM_SECTION
# define XWM_ASM_FUNC(n)  ASM_FUNC(n)
# define XWM_ASM_END(n)   ASM_END(n)
# define XWM_ASM_RET      ASM_RET
#else
# define XWM_ASM_SECTION  ".text\n"
# define XWM_ASM_RET      "ret\n"
# if defined(__APPLE__)
#  define XWM_ASM_FUNC(n) ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define XWM_ASM_END(n)  ""
# elif defined(_WIN32)
#  define XWM_ASM_FUNC(n) ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define XWM_ASM_END(n)  ""
# else
#  if defined(XWM_ASM_ARM64)
#   define XWM_ASM_FTYPE  "%function"
#  else
#   define XWM_ASM_FTYPE  "@function"
#  endif
#  define XWM_ASM_FUNC(n) ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " XWM_ASM_FTYPE "\n" #n ":\n"
#  define XWM_ASM_END(n)  ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(XWM_ASM_ARM64)

__asm__(
        XWM_ASM_SECTION

        /* xwm_asm_request_start(x0=out, x1=cap, x2=bytes, w3=opcode) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_start)
        "cbz x0, 4f\n   cmp x2, #4\n   b.lo 4f\n   lsr x4, x2, #2\n   mov x5, #65535\n   cmp x4, x5\n   b.hi 5f\n"
        "cmp x2, x1\n   b.hi 6f\n   mov x5, x0\n   mov x6, x2\n"
        "1:  cmp x6, #8\n   b.lo 2f\n   str xzr, [x5], #8\n   sub x6, x6, #8\n   b 1b\n"
        "2:  cbz x6, 3f\n   strb wzr, [x5], #1\n   sub x6, x6, #1\n   b 2b\n"
        "3:  strb w3, [x0]\n   strh w4, [x0, #2]\n   mov x0, x2\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "5:  mov x0, #-5\n" XWM_ASM_RET
        "6:  mov x0, #-1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_start)

        /* xwm_asm_request_words(x0=out, x1=cap, w2=op|(data<<8), x3=words, x4=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_words)
        "cbz x0, 4f\n   mov x5, #65534\n   cmp x4, x5\n   b.hi 5f\n   lsl x5, x4, #2\n   add x5, x5, #4\n"
        "cmp x5, x1\n   b.hi 6f\n   cbz x4, 2f\n   cbz x3, 4f\n"
        "2:  strh w2, [x0]\n   add w6, w4, #1\n   strh w6, [x0, #2]\n   add x7, x0, #4\n   mov x6, #0\n"
        "1:  cmp x6, x4\n   b.hs 3f\n   ldr w8, [x3, x6, lsl #2]\n   str w8, [x7, x6, lsl #2]\n   add x6, x6, #1\n   b 1b\n"
        "3:  mov x0, x5\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "5:  mov x0, #-5\n" XWM_ASM_RET
        "6:  mov x0, #-1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_words)

        /* xwm_asm_request_name(x0=out, x1=cap, w2=op|(data<<8), x3=name, x4=name_len, x5=max_len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_name)
        "cbz x3, 4f\n   cbz x4, 4f\n   cmp x4, x5\n   b.hi 5f\n   cbz x0, 4f\n"
        "add x6, x4, #3\n   and x6, x6, #-4\n   add x7, x6, #8\n   lsr x8, x7, #2\n   mov x9, #65535\n"
        "cmp x8, x9\n   b.hi 5f\n   cmp x7, x1\n   b.hi 6f\n"
        "strh w2, [x0]\n   strh w8, [x0, #2]\n   str w4, [x0, #4]\n   add x9, x0, #8\n   mov x10, #0\n"
        "1:  cmp x10, x4\n   b.hs 2f\n   ldrb w11, [x3, x10]\n   strb w11, [x9, x10]\n   add x10, x10, #1\n   b 1b\n"
        "2:  cmp x10, x6\n   b.hs 3f\n   strb wzr, [x9, x10]\n   add x10, x10, #1\n   b 2b\n"
        "3:  mov x0, x7\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "5:  mov x0, #-5\n" XWM_ASM_RET
        "6:  mov x0, #-1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_name)

        /* xwm_asm_event_words(x0=out, x1=cap, w2=type|(detail<<8), x3=words, x4=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_event_words)
        "cbz x0, 4f\n   cmp x4, #7\n   b.hi 4f\n   cmp x1, #32\n   b.lo 6f\n"
        "cbz x4, 2f\n   cbz x3, 4f\n"
        "2:  stp xzr, xzr, [x0]\n   stp xzr, xzr, [x0, #16]\n   strh w2, [x0]\n   add x5, x0, #4\n   mov x6, #0\n"
        "1:  cmp x6, x4\n   b.hs 3f\n   ldr w7, [x3, x6, lsl #2]\n   str w7, [x5, x6, lsl #2]\n   add x6, x6, #1\n   b 1b\n"
        "3:  mov x0, #32\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "6:  mov x0, #-1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_event_words)

        /* xwm_asm_enc_configure_window(x0=out, x1=cap, w2=window, x3=cfg) -> i64 */
        XWM_ASM_FUNC(xwm_asm_enc_configure_window)
        "cbz x3, 9f\n   ldr w4, [x3]\n   tst w4, #~0x7f\n   b.ne 9f\n"
        "tbz w4, #0, 1f\n   ldr w5, [x3, #4]\n   add w5, w5, #32768\n   cmp w5, #65536\n   b.hs 9f\n"
        "1:  tbz w4, #1, 2f\n   ldr w5, [x3, #8]\n   add w5, w5, #32768\n   cmp w5, #65536\n   b.hs 9f\n"
        "2:  tbz w4, #2, 3f\n   ldr w5, [x3, #12]\n   cbz w5, 9f\n   cmp w5, #65536\n   b.hs 9f\n"
        "3:  tbz w4, #3, 4f\n   ldr w5, [x3, #16]\n   cbz w5, 9f\n   cmp w5, #65536\n   b.hs 9f\n"
        "4:  tbz w4, #4, 5f\n   ldr w5, [x3, #20]\n   cmp w5, #65536\n   b.hs 9f\n"
        "5:  tbz w4, #6, 6f\n   ldr w5, [x3, #28]\n   cmp w5, #4\n   b.hi 9f\n"
        "6:  mov w5, w4\n   mov x6, #0\n"
        "7:  cbz w5, 8f\n   and w7, w5, #1\n   add x6, x6, x7\n   lsr w5, w5, #1\n   b 7b\n"
        "8:  cbz x0, 9f\n   lsl x7, x6, #2\n   add x7, x7, #12\n   cmp x7, x1\n   b.hi 10f\n"
        "mov w8, #12\n   strh w8, [x0]\n   add w9, w6, #3\n   strh w9, [x0, #2]\n"
        "str w2, [x0, #4]\n   str w4, [x0, #8]\n   add x8, x0, #12\n   mov x9, #0\n"
        "11: cmp x9, #7\n   b.hs 13f\n   lsr w10, w4, w9\n   tbz w10, #0, 12f\n"
        "add x11, x3, #4\n   ldr w10, [x11, x9, lsl #2]\n   str w10, [x8], #4\n"
        "12: add x9, x9, #1\n   b 11b\n"
        "13: mov x0, x7\n" XWM_ASM_RET
        "9:  mov x0, #-2\n" XWM_ASM_RET
        "10: mov x0, #-1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_enc_configure_window)

        /* xwm_asm_enc_change_window_attributes(x0=out, x1=cap, w2=win, w3=mask, x4=vals, x5=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_enc_change_window_attributes)
        "cbz w2, 4f\n   cbz w3, 4f\n   mov w6, #0x7fff\n   bics wzr, w3, w6\n   b.ne 4f\n   cbz x4, 4f\n"
        "mov w6, w3\n   mov x7, #0\n"
        "1:  cbz w6, 2f\n   and w8, w6, #1\n   add x7, x7, x8\n   lsr w6, w6, #1\n   b 1b\n"
        "2:  cmp x7, x5\n   b.ne 4f\n   cbz x0, 4f\n   lsl x8, x5, #2\n   add x8, x8, #12\n   cmp x8, x1\n   b.hi 6f\n"
        "mov w9, #2\n   strh w9, [x0]\n   add w9, w5, #3\n   strh w9, [x0, #2]\n"
        "str w2, [x0, #4]\n   str w3, [x0, #8]\n   add x9, x0, #12\n   mov x10, #0\n"
        "3:  cmp x10, x5\n   b.hs 5f\n   ldr w11, [x4, x10, lsl #2]\n   str w11, [x9, x10, lsl #2]\n   add x10, x10, #1\n   b 3b\n"
        "5:  mov x0, x8\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "6:  mov x0, #-1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_enc_change_window_attributes)

        /* xwm_asm_server_message_size(x0=buf, x1=len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_server_message_size)
        "cbz x0, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w2, [x0]\n   and w2, w2, #0x7f\n"
        "cmp w2, #1\n   b.eq 1f\n   cmp w2, #35\n   b.eq 1f\n   mov x0, #32\n" XWM_ASM_RET
        "1:  ldr w3, [x0, #4]\n   lsl x3, x3, #2\n   add x3, x3, #32\n   movz x4, #0x0020\n   movk x4, #1, lsl #16\n"
        "cmp x3, x4\n   b.hi 5f\n   mov x0, x3\n" XWM_ASM_RET
        "3:  mov x0, #-3\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "5:  mov x0, #-5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_server_message_size)

        /* xwm_asm_setup_size(x0=buf, x1=len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_setup_size)
        "cbz x0, 4f\n   cmp x1, #8\n   b.lo 3f\n   ldrh w2, [x0, #6]\n   lsl x2, x2, #2\n   add x2, x2, #8\n"
        "mov x3, #65536\n   cmp x2, x3\n   b.hi 5f\n   mov x0, x2\n" XWM_ASM_RET
        "3:  mov x0, #-3\n" XWM_ASM_RET
        "4:  mov x0, #-2\n" XWM_ASM_RET
        "5:  mov x0, #-5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_setup_size)

        /* xwm_asm_parse_setup(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_setup)
        "cbz x0, 14f\n   cbz x2, 14f\n   cmp x1, #8\n   b.lo 13f\n"
        "ldrh w3, [x0, #6]\n   lsl x3, x3, #2\n   add x3, x3, #8\n   mov x4, #65536\n   cmp x3, x4\n   b.hi 15f\n"
        "cmp x1, x3\n   b.lo 16f\n   ldrb w4, [x0]\n   str w4, [x2]\n   cbnz w4, 1f\n"
        "ldrb w5, [x0, #1]\n   ldrh w6, [x0, #2]\n   ldrh w7, [x0, #4]\n"
        "str w5, [x2, #4]\n   stp w6, w7, [x2, #8]\n   add x5, x5, #8\n   cmp x5, x3\n   b.hi 16f\n   mov w0, #0\n" XWM_ASM_RET
        "1:  cmp w4, #2\n   b.eq 12f\n   cmp w4, #1\n   b.ne 17f\n   cmp x3, #40\n   b.lo 16f\n"
        "ldrh w5, [x0, #2]\n   ldrh w6, [x0, #4]\n   stp w5, w6, [x2, #8]\n"
        "ldr w7, [x0, #8]\n   ldr w8, [x0, #12]\n   ldr w9, [x0, #16]\n   ldr w10, [x0, #20]\n"
        "stp w7, w8, [x2, #16]\n   stp w9, w10, [x2, #24]\n"
        "ldrh w11, [x0, #24]\n   ldrh w12, [x0, #26]\n   stp w11, w12, [x2, #32]\n"
        "ldrb w13, [x0, #29]\n   ldrb w14, [x0, #28]\n   stp w13, w14, [x2, #40]\n"
        "ldrb w15, [x0, #30]\n   ldrb w16, [x0, #31]\n   stp w15, w16, [x2, #48]\n"
        "ldrb w15, [x0, #32]\n   ldrb w16, [x0, #33]\n   stp w15, w16, [x2, #56]\n"
        "ldrb w15, [x0, #34]\n   ldrb w16, [x0, #35]\n   stp w15, w16, [x2, #64]\n"
        "cmp w5, #11\n   b.ne 17f\n   cbz w9, 14f\n   tst w8, w9\n   b.ne 14f\n   cbz w14, 14f\n"
        "cmp w11, #256\n   b.hi 15f\n   cmp w13, #32\n   b.hi 15f\n   cmp w14, #4\n   b.hi 15f\n"
        "add x4, x11, #3\n   and x4, x4, #-4\n   add x4, x4, #40\n   cmp x4, x3\n   b.hi 16f\n"
        "sub x5, x3, x4\n   lsl x6, x13, #3\n   cmp x5, x6\n   b.lo 16f\n   add x4, x4, x6\n   mov w5, #0\n"
        "2:  cmp w5, w14\n   b.hs 6f\n   sub x6, x3, x4\n   cmp x6, #40\n   b.lo 16f\n   add x7, x0, x4\n"
        "ldrb w8, [x7, #39]\n   cbz w8, 14f\n   cmp w8, #32\n   b.hi 15f\n   cbnz w5, 3f\n"
        "ldr w9, [x7]\n   ldr w10, [x7, #32]\n   stp w9, w10, [x2, #72]\n"
        "ldrb w9, [x7, #38]\n   ldr w10, [x7, #16]\n   stp w9, w10, [x2, #80]\n"
        "ldr w9, [x7, #4]\n   ldr w10, [x7, #8]\n   stp w9, w10, [x2, #88]\n"
        "ldr w9, [x7, #12]\n   ldrh w10, [x7, #20]\n   stp w9, w10, [x2, #96]\n"
        "ldrh w9, [x7, #22]\n   ldrh w10, [x7, #24]\n   stp w9, w10, [x2, #104]\n"
        "ldrh w9, [x7, #26]\n   str w9, [x2, #112]\n"
        "3:  add x4, x4, #40\n   mov x9, #0\n   mov x10, #0\n"
        "4:  cmp x10, x8\n   b.hs 5f\n   sub x6, x3, x4\n   cmp x6, #8\n   b.lo 16f\n"
        "add x7, x0, x4\n   ldrh w11, [x7, #2]\n   add x9, x9, x11\n   add x4, x4, #8\n"
        "cmp x9, #4096\n   b.hi 15f\n   add x12, x11, x11, lsl #1\n   lsl x12, x12, #3\n"
        "sub x6, x3, x4\n   cmp x6, x12\n   b.lo 16f\n   add x4, x4, x12\n   add x10, x10, #1\n   b 4b\n"
        "5:  add w5, w5, #1\n   b 2b\n"
        "6:  cmp x4, x3\n   b.ne 16f\n"
        "12: mov w0, #0\n" XWM_ASM_RET
        "13: mov w0, #-3\n" XWM_ASM_RET
        "14: mov w0, #-2\n" XWM_ASM_RET
        "15: mov w0, #-5\n" XWM_ASM_RET
        "16: mov w0, #-4\n" XWM_ASM_RET
        "17: mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_setup)

        /* xwm_asm_parse_error(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_error)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cbnz w3, 6f\n"
        "ldrb w4, [x0, #1]\n   ldrh w5, [x0, #2]\n   ldr w6, [x0, #4]\n   ldrh w7, [x0, #8]\n   ldrb w8, [x0, #10]\n"
        "stp w4, w5, [x2]\n   stp w6, w7, [x2, #8]\n   str w8, [x2, #16]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_error)

        /* xwm_asm_parse_atom_reply(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_atom_reply)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldr w4, [x0, #4]\n   cbnz w4, 5f\n   ldrh w5, [x0, #2]\n   ldr w6, [x0, #8]\n"
        "stp w5, w6, [x2]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_atom_reply)

        /* xwm_asm_parse_atom_name_reply(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_atom_name_reply)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldrh w4, [x0, #8]\n   cmp w4, #256\n   b.hi 7f\n   add x5, x4, #3\n   lsr x5, x5, #2\n"
        "ldr w6, [x0, #4]\n   cmp x6, x5\n   b.ne 5f\n   sub x7, x1, #32\n   lsr x7, x7, #2\n   cmp x7, x5\n   b.lo 5f\n"
        "ldrh w8, [x0, #2]\n   add x9, x0, #32\n   str w8, [x2]\n   stp x9, x4, [x2, #8]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        "7:  mov w0, #-5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_atom_name_reply)

        /* xwm_asm_parse_property_reply(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_property_reply)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldrb w4, [x0, #1]\n   ldr w5, [x0, #16]\n   cbnz w4, 1f\n   cbnz w5, 4f\n   mov x6, #0\n   b 2f\n"
        "1:  cmp w4, #8\n   b.ne 8f\n   mov x7, #65536\n   cmp x5, x7\n   b.hi 7f\n   mov x6, x5\n   b 2f\n"
        "8:  cmp w4, #16\n   b.ne 9f\n   mov x7, #32768\n   cmp x5, x7\n   b.hi 7f\n   lsl x6, x5, #1\n   b 2f\n"
        "9:  cmp w4, #32\n   b.ne 4f\n   mov x7, #16384\n   cmp x5, x7\n   b.hi 7f\n   lsl x6, x5, #2\n"
        "2:  add x7, x6, #3\n   lsr x7, x7, #2\n   ldr w8, [x0, #4]\n   cmp x8, x7\n   b.ne 5f\n"
        "sub x8, x1, #32\n   lsr x8, x8, #2\n   cmp x8, x7\n   b.lo 5f\n"
        "ldrh w8, [x0, #2]\n   ldr w9, [x0, #8]\n   ldr w10, [x0, #12]\n   add x11, x0, #32\n"
        "stp w8, w4, [x2]\n   stp w9, w10, [x2, #8]\n   str w5, [x2, #16]\n   stp x11, x6, [x2, #24]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        "7:  mov w0, #-5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_property_reply)

        /* xwm_asm_parse_geometry_reply(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_geometry_reply)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldr w4, [x0, #4]\n   cbnz w4, 5f\n   ldrh w5, [x0, #2]\n   ldrb w6, [x0, #1]\n   ldr w7, [x0, #8]\n"
        "ldrh w8, [x0, #16]\n   ldrh w9, [x0, #18]\n   ldrh w10, [x0, #20]\n   ldrsh w11, [x0, #12]\n   ldrsh w12, [x0, #14]\n"
        "stp w5, w6, [x2]\n   stp w7, w8, [x2, #8]\n   stp w9, w10, [x2, #16]\n   stp w11, w12, [x2, #24]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_geometry_reply)

        /* xwm_asm_parse_attributes_reply(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_attributes_reply)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldr w4, [x0, #4]\n   cmp w4, #3\n   b.ne 5f\n   cmp x1, #44\n   b.lo 5f\n"
        "ldrh w5, [x0, #2]\n   ldr w6, [x0, #8]\n   ldrh w7, [x0, #12]\n   ldrb w8, [x0, #14]\n"
        "ldrb w9, [x0, #15]\n   ldrb w10, [x0, #24]\n   ldrb w11, [x0, #25]\n   ldrb w12, [x0, #26]\n"
        "stp w5, w6, [x2]\n   stp w7, w8, [x2, #8]\n   stp w9, w10, [x2, #16]\n   stp w11, w12, [x2, #24]\n"
        "ldrb w5, [x0, #27]\n   ldr w6, [x0, #28]\n   ldr w7, [x0, #32]\n   ldr w8, [x0, #36]\n   ldrh w9, [x0, #40]\n"
        "stp w5, w6, [x2, #32]\n   stp w7, w8, [x2, #40]\n   str w9, [x2, #48]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_attributes_reply)

        /* xwm_asm_parse_extension_reply(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_extension_reply)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldr w4, [x0, #4]\n   cbnz w4, 5f\n   ldrb w5, [x0, #8]\n   cmp w5, #1\n   b.hi 4f\n"
        "ldrh w6, [x0, #2]\n   ldrb w7, [x0, #9]\n   ldrb w8, [x0, #10]\n   ldrb w9, [x0, #11]\n"
        "stp w6, w5, [x2]\n   stp w7, w8, [x2, #8]\n   str w9, [x2, #16]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_extension_reply)

        /* xwm_asm_parse_xres_client_pid(x0=buf, x1=len, x2=pid) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_xres_client_pid)
        "cbz x0, 4f\n   cbz x2, 4f\n   cmp x1, #32\n   b.lo 3f\n   ldrb w3, [x0]\n   cmp w3, #1\n   b.ne 6f\n"
        "ldr w4, [x0, #4]\n   sub x5, x1, #32\n   lsr x5, x5, #2\n   cmp x5, x4\n   b.lo 5f\n"
        "lsl x5, x4, #2\n   add x5, x5, #32\n   str wzr, [x2]\n   mov x6, #32\n   ldr w7, [x0, #8]\n   mov w8, #0\n"
        "1:  cmp w8, w7\n   b.hs 2f\n   sub x9, x5, x6\n   cmp x9, #12\n   b.lo 5f\n   add x10, x0, x6\n"
        "ldr w11, [x10, #8]\n   sub x9, x9, #12\n   cmp x11, x9\n   b.hi 5f\n   add x12, x11, #3\n   and x12, x12, #-4\n"
        "cmp x12, x9\n   b.hi 5f\n   cmp x11, #4\n   b.ne 7f\n   ldr w13, [x10, #4]\n   tbz w13, #1, 7f\n"
        "ldr w13, [x10, #12]\n   str w13, [x2]\n"
        "7:  add x6, x6, #12\n   add x6, x6, x12\n   add w8, w8, #1\n   b 1b\n"
        "2:  mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "5:  mov w0, #-4\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_xres_client_pid)

        /* xwm_asm_parse_event(x0=buf, x1=len, x2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_event)
        "cbz x0, 28f\n   cbz x2, 28f\n   cmp x1, #32\n   b.lo 27f\n   ldrb w3, [x0]\n   and w4, w3, #0x7f\n"
        "cmp w4, #2\n   b.lo 29f\n   mov x5, x2\n   mov w6, #8\n"
        "1:  stp xzr, xzr, [x5], #16\n   subs w6, w6, #1\n   b.ne 1b\n   str wzr, [x5]\n"
        "lsr w5, w3, #7\n   ldrh w6, [x0, #2]\n   ldrb w7, [x0, #1]\n"
        "stp w4, w5, [x2, #4]\n   stp w6, w7, [x2, #12]\n"
        "cmp w4, #22\n   b.eq 10f\n   cmp w4, #23\n   b.eq 11f\n   cmp w4, #28\n   b.eq 12f\n   cmp w4, #33\n   b.eq 13f\n"
        "cmp w4, #16\n   b.eq 14f\n   cmp w4, #17\n   b.eq 15f\n   cmp w4, #18\n   b.eq 16f\n   cmp w4, #19\n   b.eq 17f\n"
        "cmp w4, #20\n   b.eq 18f\n   cmp w4, #9\n   b.eq 19f\n   cmp w4, #10\n   b.eq 19f\n   cmp w4, #7\n   b.eq 20f\n"
        "cmp w4, #8\n   b.eq 20f\n   cmp w4, #29\n   b.eq 21f\n   cmp w4, #30\n   b.eq 22f\n   cmp w4, #31\n   b.eq 23f\n"
        "cmp w4, #12\n   b.eq 24f\n   cmp w4, #21\n   b.eq 24f\n"
        "0:  mov w0, #0\n" XWM_ASM_RET
        "10: mov w5, #6\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   str w5, [x2, #20]\n   ldrb w6, [x0, #26]\n   str w6, [x2, #60]\n   b 25f\n"
        "11: mov w5, #3\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   str w5, [x2, #28]\n   ldrh w6, [x0, #26]\n   str w6, [x2, #56]\n"
        "25: ldr w5, [x0, #8]\n   ldr w6, [x0, #12]\n   str w5, [x2, #24]\n   str w6, [x2, #32]\n"
        "ldrsh w5, [x0, #16]\n   ldrsh w6, [x0, #18]\n   stp w5, w6, [x2, #92]\n"
        "ldrh w5, [x0, #20]\n   ldrh w6, [x0, #22]\n   ldrh w7, [x0, #24]\n   stp w5, w6, [x2, #100]\n   str w7, [x2, #108]\n   b 0b\n"
        "12: mov w5, #7\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   ldr w7, [x0, #12]\n   ldrb w8, [x0, #16]\n"
        "str w5, [x2, #24]\n   stp w6, w7, [x2, #36]\n   str w8, [x2, #44]\n   b 0b\n"
        "13: mov w5, #8\n   str w5, [x2]\n   str w7, [x2, #48]\n   cmp w7, #8\n   b.eq 26f\n   cmp w7, #16\n   b.eq 26f\n"
        "cmp w7, #32\n   b.ne 28f\n   ldr w5, [x0, #12]\n   ldr w6, [x0, #16]\n   ldr w7, [x0, #20]\n   ldr w8, [x0, #24]\n   ldr w9, [x0, #28]\n"
        "stp w5, w6, [x2, #112]\n   stp w7, w8, [x2, #120]\n   str w9, [x2, #128]\n"
        "26: ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   str w5, [x2, #24]\n   str w6, [x2, #36]\n   b 0b\n"
        "14: mov w5, #9\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   str w5, [x2, #28]\n   str w6, [x2, #24]\n"
        "ldrsh w5, [x0, #12]\n   ldrsh w6, [x0, #14]\n   stp w5, w6, [x2, #92]\n"
        "ldrh w5, [x0, #16]\n   ldrh w6, [x0, #18]\n   ldrh w7, [x0, #20]\n   ldrb w8, [x0, #22]\n"
        "stp w5, w6, [x2, #100]\n   str w7, [x2, #108]\n   str w8, [x2, #60]\n   b 0b\n"
        "15: mov w5, #5\n   b 30f\n"
        "16: mov w5, #4\n   ldrb w6, [x0, #12]\n   str w6, [x2, #64]\n   b 30f\n"
        "17: mov w5, #14\n   ldrb w6, [x0, #12]\n   str w6, [x2, #60]\n"
        "30: str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   stp w5, w6, [x2, #20]\n   b 0b\n"
        "18: mov w5, #2\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   str w5, [x2, #28]\n   str w6, [x2, #24]\n   b 0b\n"
        "19: add w5, w4, #1\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldrb w6, [x0, #8]\n   str w5, [x2, #20]\n   str w6, [x2, #52]\n   b 0b\n"
        "20: add w5, w4, #5\n   str w5, [x2]\n   ldr w5, [x0, #12]\n   ldrb w6, [x0, #30]\n   str w5, [x2, #20]\n   str w6, [x2, #52]\n   b 0b\n"
        "21: mov w5, #15\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   ldr w7, [x0, #12]\n"
        "str w5, [x2, #40]\n   str w6, [x2, #68]\n   str w7, [x2, #76]\n   b 0b\n"
        "22: mov w5, #16\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   ldr w7, [x0, #12]\n"
        "ldr w8, [x0, #16]\n   ldr w9, [x0, #20]\n   ldr w10, [x0, #24]\n   str w5, [x2, #40]\n"
        "stp w6, w7, [x2, #68]\n   stp w8, w9, [x2, #76]\n   str w10, [x2, #84]\n   b 0b\n"
        "23: mov w5, #17\n   str w5, [x2]\n   ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   ldr w7, [x0, #12]\n"
        "ldr w8, [x0, #16]\n   ldr w9, [x0, #20]\n   str w5, [x2, #40]\n   stp w6, w7, [x2, #72]\n   stp w8, w9, [x2, #80]\n   b 0b\n"
        "24: mov w5, #1\n   str w5, [x2]\n   b 0b\n"
        "27: mov w0, #-3\n" XWM_ASM_RET
        "28: mov w0, #-2\n" XWM_ASM_RET
        "29: mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_event)

        /* xwm_asm_parse_xfixes_selection(x0=buf, x1=len, w2=first_event, x3=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_xfixes_selection)
        "cbz x0, 4f\n   cbz x3, 4f\n   cmp x1, #32\n   b.lo 3f\n"
        "cmp w2, #36\n   b.lo 6f\n   cmp w2, #127\n   b.hi 6f\n   ldrb w4, [x0]\n   and w5, w4, #0x7f\n   cmp w5, w2\n   b.ne 6f\n"
        "mov x5, x3\n   mov w6, #8\n"
        "1:  stp xzr, xzr, [x5], #16\n   subs w6, w6, #1\n   b.ne 1b\n   str wzr, [x5]\n"
        "mov w5, #18\n   lsr w6, w4, #7\n   ldrh w7, [x0, #2]\n   ldrb w8, [x0, #1]\n"
        "stp w5, w2, [x3]\n   stp w6, w7, [x3, #8]\n   str w8, [x3, #16]\n"
        "ldr w5, [x0, #4]\n   ldr w6, [x0, #8]\n   ldr w7, [x0, #12]\n   ldr w8, [x0, #16]\n   ldr w9, [x0, #20]\n"
        "str w5, [x3, #24]\n   str w6, [x3, #68]\n   str w7, [x3, #76]\n   str w8, [x3, #40]\n   str w9, [x3, #88]\n   mov w0, #0\n" XWM_ASM_RET
        "3:  mov w0, #-3\n" XWM_ASM_RET
        "4:  mov w0, #-2\n" XWM_ASM_RET
        "6:  mov w0, #-6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_xfixes_selection)

        /* xwm_asm_latin1_utf8(x0=in, x1=n, x2=out) -> u64 */
        XWM_ASM_FUNC(xwm_asm_latin1_utf8)
        "cbz x1, 4f\n   cbz x0, 4f\n   cbz x2, 4f\n   mov x3, x2\n   add x1, x0, x1\n"
        "1:  ldrb w4, [x0], #1\n   tbnz w4, #7, 2f\n   strb w4, [x2], #1\n"
        "3:  cmp x0, x1\n   b.lo 1b\n   sub x0, x2, x3\n" XWM_ASM_RET
        "2:  lsr w5, w4, #6\n   orr w5, w5, #0xc0\n   and w4, w4, #0x3f\n   orr w4, w4, #0x80\n"
        "strb w5, [x2]\n   strb w4, [x2, #1]\n   add x2, x2, #2\n   b 3b\n"
        "4:  mov x0, #0\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_latin1_utf8)

        /* xwm_asm_utf8_latin1(x0=cut, x1=in, x2=n, x3=out) -> u64 */
        XWM_ASM_FUNC(xwm_asm_utf8_latin1)
        "cbz x2, 9f\n   cbz x0, 9f\n   cbz x1, 9f\n   cbz x3, 9f\n"
        "ldp w4, w5, [x0]\n   mov x6, x3\n   add x2, x1, x2\n   mov w9, #63\n   cbz w4, 10f\n"
        "1:  ldrb w7, [x1], #1\n   and w8, w7, #0xc0\n   cmp w8, #0x80\n   b.ne 2f\n"
        "and w8, w7, #0x3f\n   orr w5, w8, w5, lsl #6\n   subs w4, w4, #1\n   b.ne 7f\n"
        "13: cmp w5, #255\n   csel w7, w5, w9, ls\n"
        "3:  strb w7, [x3], #1\n"
        "14: cmp x1, x2\n   b.hs 11f\n"
        "10: ldrb w7, [x1], #1\n   tbz w7, #7, 3b\n"
        "4:  and w8, w7, #0xe0\n   cmp w8, #0xc0\n   b.ne 5f\n   and w5, w7, #0x1f\n"
        "cmp x1, x2\n   b.hs 12f\n   ldrb w8, [x1]\n   and w10, w8, #0xc0\n   cmp w10, #0x80\n   b.ne 12f\n"
        "add x1, x1, #1\n   and w8, w8, #0x3f\n   orr w5, w8, w5, lsl #6\n   b 13b\n"
        "12: mov w4, #1\n   b 7f\n"
        "2:  mov w4, #0\n   strb w9, [x3], #1\n   tbz w7, #7, 3b\n   b 4b\n"
        "5:  and w8, w7, #0xf0\n   cmp w8, #0xe0\n   b.ne 6f\n   mov w4, #2\n   and w5, w7, #0x0f\n   b 7f\n"
        "6:  and w8, w7, #0xf8\n   cmp w8, #0xf0\n   b.ne 8f\n   mov w4, #3\n   and w5, w7, #0x07\n   b 7f\n"
        "8:  mov w7, w9\n   b 3b\n"
        "7:  cmp x1, x2\n   b.lo 1b\n"
        "11: stp w4, w5, [x0]\n   sub x0, x3, x6\n" XWM_ASM_RET
        "9:  mov x0, #0\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_utf8_latin1)
);

#elif defined(XWM_ASM_X64)

#if defined(_WIN32)
# define XWM_L2    "mov r8, rcx\n   mov r9, rdx\n"
# define XWM_L3    "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define XWM_L4    "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
# define XWM_L5_P0 "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov rcx, [rsp + 40]\n"
# define XWM_L6_P1 "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov rcx, [rsp + 48]\n   mov rdx, [rsp + 56]\n"
# define XWM_B3_PRO  "push rdi\n   push rsi\n   mov rdi, rcx\n   mov rsi, rdx\n   mov rdx, r8\n"
# define XWM_B3_EPI  "pop rsi\n   pop rdi\n"
#else
# define XWM_L2    "mov r8, rdi\n   mov r9, rsi\n"
# define XWM_L3    "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define XWM_L4    "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
# define XWM_L5_P0 "mov r10, rdx\n   mov r11, rcx\n   mov rcx, r8\n   mov r8, rdi\n   mov r9, rsi\n"
# define XWM_L6_P1 "mov r10, rdx\n   mov r11, rcx\n   mov rcx, r8\n   mov rdx, r9\n   mov r8, rdi\n   mov r9, rsi\n"
# define XWM_B3_PRO  ""
# define XWM_B3_EPI  ""
#endif

__asm__(
        XWM_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* xwm_asm_request_start(r8=out, r9=cap, r10=bytes, r11d=opcode) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_start)
        XWM_L4
        "test r8, r8\n   jz 4f\n   cmp r10, 4\n   jb 4f\n   mov rax, r10\n   shr rax, 2\n   cmp rax, 65535\n   ja 5f\n"
        "cmp r10, r9\n   ja 6f\n   mov rdx, r8\n   mov rcx, r10\n"
        "1:  cmp rcx, 8\n   jb 2f\n   mov qword ptr [rdx], 0\n   add rdx, 8\n   sub rcx, 8\n   jmp 1b\n"
        "2:  test rcx, rcx\n   jz 3f\n   mov byte ptr [rdx], 0\n   inc rdx\n   dec rcx\n   jmp 2b\n"
        "3:  mov byte ptr [r8], r11b\n   mov word ptr [r8 + 2], ax\n   mov rax, r10\n" XWM_ASM_RET
        "4:  mov rax, -2\n" XWM_ASM_RET
        "5:  mov rax, -5\n" XWM_ASM_RET
        "6:  mov rax, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_start)

        /* xwm_asm_request_words(r8=out, r9=cap, r10d=op_data, r11=words, rcx=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_words)
        XWM_L5_P0
        "test r8, r8\n   jz 4f\n   cmp rcx, 65534\n   ja 5f\n   lea rax, [rcx * 4 + 4]\n   cmp rax, r9\n   ja 6f\n"
        "test rcx, rcx\n   jz 2f\n   test r11, r11\n   jz 4f\n"
        "2:  mov word ptr [r8], r10w\n   lea edx, [ecx + 1]\n   mov word ptr [r8 + 2], dx\n   xor rdx, rdx\n"
        "1:  cmp rdx, rcx\n   jae 3f\n   mov r9d, dword ptr [r11 + rdx * 4]\n   mov dword ptr [r8 + rdx * 4 + 4], r9d\n   inc rdx\n   jmp 1b\n"
        "3:  " XWM_ASM_RET
        "4:  mov rax, -2\n" XWM_ASM_RET
        "5:  mov rax, -5\n" XWM_ASM_RET
        "6:  mov rax, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_words)

        /* xwm_asm_request_name(r8=out, r9=cap, r10d=op_data, r11=name, rcx=name_len, rdx=max_len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_name)
        "push rbx\n"
        XWM_L6_P1
        "test r11, r11\n   jz 4f\n   test rcx, rcx\n   jz 4f\n   cmp rcx, rdx\n   ja 5f\n   test r8, r8\n   jz 4f\n"
        "lea rbx, [rcx + 3]\n   and rbx, -4\n   lea rax, [rbx + 8]\n   mov rdx, rax\n   shr rdx, 2\n"
        "cmp rdx, 65535\n   ja 5f\n   cmp rax, r9\n   ja 6f\n"
        "mov word ptr [r8], r10w\n   mov word ptr [r8 + 2], dx\n   mov dword ptr [r8 + 4], ecx\n   xor r9, r9\n"
        "1:  cmp r9, rcx\n   jae 2f\n   movzx edx, byte ptr [r11 + r9]\n   mov byte ptr [r8 + r9 + 8], dl\n   inc r9\n   jmp 1b\n"
        "2:  cmp r9, rbx\n   jae 3f\n   mov byte ptr [r8 + r9 + 8], 0\n   inc r9\n   jmp 2b\n"
        "3:  pop rbx\n" XWM_ASM_RET
        "4:  mov rax, -2\n   pop rbx\n" XWM_ASM_RET
        "5:  mov rax, -5\n   pop rbx\n" XWM_ASM_RET
        "6:  mov rax, -1\n   pop rbx\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_name)

        /* xwm_asm_event_words(r8=out, r9=cap, r10d=type_detail, r11=words, rcx=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_event_words)
        XWM_L5_P0
        "test r8, r8\n   jz 4f\n   cmp rcx, 7\n   ja 4f\n   cmp r9, 32\n   jb 6f\n"
        "test rcx, rcx\n   jz 2f\n   test r11, r11\n   jz 4f\n"
        "2:  xor eax, eax\n   mov [r8], rax\n   mov [r8 + 8], rax\n   mov [r8 + 16], rax\n   mov [r8 + 24], rax\n"
        "mov word ptr [r8], r10w\n   xor rdx, rdx\n"
        "1:  cmp rdx, rcx\n   jae 3f\n   mov eax, dword ptr [r11 + rdx * 4]\n   mov dword ptr [r8 + rdx * 4 + 4], eax\n   inc rdx\n   jmp 1b\n"
        "3:  mov rax, 32\n" XWM_ASM_RET
        "4:  mov rax, -2\n" XWM_ASM_RET
        "6:  mov rax, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_event_words)

        /* xwm_asm_enc_configure_window(r8=out, r9=cap, r10d=window, r11=cfg) -> i64 */
        XWM_ASM_FUNC(xwm_asm_enc_configure_window)
        XWM_L4
        "test r11, r11\n   jz 9f\n   mov edx, dword ptr [r11]\n   test edx, -128\n   jnz 9f\n"
        "test edx, 1\n   jz 1f\n   mov eax, dword ptr [r11 + 4]\n   add eax, 32768\n   cmp eax, 65535\n   ja 9f\n"
        "1:  test edx, 2\n   jz 2f\n   mov eax, dword ptr [r11 + 8]\n   add eax, 32768\n   cmp eax, 65535\n   ja 9f\n"
        "2:  test edx, 4\n   jz 3f\n   mov eax, dword ptr [r11 + 12]\n   dec eax\n   cmp eax, 65534\n   ja 9f\n"
        "3:  test edx, 8\n   jz 4f\n   mov eax, dword ptr [r11 + 16]\n   dec eax\n   cmp eax, 65534\n   ja 9f\n"
        "4:  test edx, 16\n   jz 5f\n   cmp dword ptr [r11 + 20], 65535\n   ja 9f\n"
        "5:  test edx, 64\n   jz 6f\n   cmp dword ptr [r11 + 28], 4\n   ja 9f\n"
        "6:  mov eax, edx\n   xor ecx, ecx\n"
        "7:  test eax, eax\n   jz 8f\n   shr eax, 1\n   adc rcx, 0\n   jmp 7b\n"
        "8:  test r8, r8\n   jz 9f\n   lea rax, [rcx * 4 + 12]\n   cmp rax, r9\n   ja 10f\n"
        "mov word ptr [r8], 12\n   add ecx, 3\n   mov word ptr [r8 + 2], cx\n"
        "mov dword ptr [r8 + 4], r10d\n   mov dword ptr [r8 + 8], edx\n   lea r9, [r8 + 12]\n   xor ecx, ecx\n"
        "11: cmp ecx, 7\n   jae 13f\n   bt edx, ecx\n   jnc 12f\n"
        "mov r10d, dword ptr [r11 + rcx * 4 + 4]\n   mov dword ptr [r9], r10d\n   add r9, 4\n"
        "12: inc ecx\n   jmp 11b\n"
        "13: " XWM_ASM_RET
        "9:  mov rax, -2\n" XWM_ASM_RET
        "10: mov rax, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_enc_configure_window)

        /* xwm_asm_enc_change_window_attributes(r8=out, r9=cap, r10d=win, r11d=mask, rcx=vals, rdx=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_enc_change_window_attributes)
        "push rbx\n"
        XWM_L6_P1
        "test r10d, r10d\n   jz 4f\n   test r11d, r11d\n   jz 4f\n   test r11d, -32768\n   jnz 4f\n   test rcx, rcx\n   jz 4f\n"
        "mov eax, r11d\n   xor rbx, rbx\n"
        "1:  test eax, eax\n   jz 2f\n   shr eax, 1\n   adc rbx, 0\n   jmp 1b\n"
        "2:  cmp rbx, rdx\n   jne 4f\n   test r8, r8\n   jz 4f\n   lea rax, [rdx * 4 + 12]\n   cmp rax, r9\n   ja 6f\n"
        "mov word ptr [r8], 2\n   lea ebx, [edx + 3]\n   mov word ptr [r8 + 2], bx\n"
        "mov dword ptr [r8 + 4], r10d\n   mov dword ptr [r8 + 8], r11d\n   xor r9, r9\n"
        "3:  cmp r9, rdx\n   jae 5f\n   mov ebx, dword ptr [rcx + r9 * 4]\n   mov dword ptr [r8 + r9 * 4 + 12], ebx\n   inc r9\n   jmp 3b\n"
        "5:  pop rbx\n" XWM_ASM_RET
        "4:  mov rax, -2\n   pop rbx\n" XWM_ASM_RET
        "6:  mov rax, -1\n   pop rbx\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_enc_change_window_attributes)

        /* xwm_asm_server_message_size(r8=buf, r9=len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_server_message_size)
        XWM_L2
        "test r8, r8\n   jz 4f\n   cmp r9, 32\n   jb 3f\n   movzx eax, byte ptr [r8]\n   and eax, 0x7f\n"
        "cmp eax, 1\n   je 1f\n   cmp eax, 35\n   je 1f\n   mov rax, 32\n" XWM_ASM_RET
        "1:  mov eax, dword ptr [r8 + 4]\n   lea rax, [rax * 4 + 32]\n   cmp rax, 65568\n   ja 5f\n" XWM_ASM_RET
        "3:  mov rax, -3\n" XWM_ASM_RET
        "4:  mov rax, -2\n" XWM_ASM_RET
        "5:  mov rax, -5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_server_message_size)

        /* xwm_asm_setup_size(r8=buf, r9=len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_setup_size)
        XWM_L2
        "test r8, r8\n   jz 4f\n   cmp r9, 8\n   jb 3f\n   movzx eax, word ptr [r8 + 6]\n   lea rax, [rax * 4 + 8]\n"
        "cmp rax, 65536\n   ja 5f\n" XWM_ASM_RET
        "3:  mov rax, -3\n" XWM_ASM_RET
        "4:  mov rax, -2\n" XWM_ASM_RET
        "5:  mov rax, -5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_setup_size)

        /* xwm_asm_parse_setup(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_setup)
        XWM_B3_PRO
        "push rbx\n   push r12\n   push r13\n"
        "test rdi, rdi\n   jz 14f\n   test rdx, rdx\n   jz 14f\n   cmp rsi, 8\n   jb 13f\n"
        "movzx r11d, word ptr [rdi + 6]\n   lea r11, [r11 * 4 + 8]\n   cmp r11, 65536\n   ja 15f\n"
        "cmp rsi, r11\n   jb 16f\n   movzx eax, byte ptr [rdi]\n   mov [rdx], eax\n   test eax, eax\n   jnz 1f\n"
        "movzx ecx, byte ptr [rdi + 1]\n   movzx esi, word ptr [rdi + 2]\n   movzx eax, word ptr [rdi + 4]\n"
        "mov [rdx + 4], ecx\n   mov [rdx + 8], esi\n   mov [rdx + 12], eax\n"
        "add rcx, 8\n   cmp rcx, r11\n   ja 16f\n   jmp 12f\n"
        "1:  cmp eax, 2\n   je 12f\n   cmp eax, 1\n   jne 17f\n   cmp r11, 40\n   jb 16f\n"
        "movzx eax, word ptr [rdi + 2]\n   mov [rdx + 8], eax\n   movzx ecx, word ptr [rdi + 4]\n   mov [rdx + 12], ecx\n"
        "mov ecx, [rdi + 8]\n   mov [rdx + 16], ecx\n   mov ecx, [rdi + 12]\n   mov [rdx + 20], ecx\n"
        "mov esi, [rdi + 16]\n   mov [rdx + 24], esi\n   mov ebx, [rdi + 20]\n   mov [rdx + 28], ebx\n"
        "movzx ebx, word ptr [rdi + 24]\n   mov [rdx + 32], ebx\n   movzx r12d, word ptr [rdi + 26]\n   mov [rdx + 36], r12d\n"
        "movzx r12d, byte ptr [rdi + 29]\n   mov [rdx + 40], r12d\n   movzx r13d, byte ptr [rdi + 28]\n   mov [rdx + 44], r13d\n"
        "movzx r8d, byte ptr [rdi + 30]\n   mov [rdx + 48], r8d\n   movzx r8d, byte ptr [rdi + 31]\n   mov [rdx + 52], r8d\n"
        "movzx r8d, byte ptr [rdi + 32]\n   mov [rdx + 56], r8d\n   movzx r8d, byte ptr [rdi + 33]\n   mov [rdx + 60], r8d\n"
        "movzx r8d, byte ptr [rdi + 34]\n   mov [rdx + 64], r8d\n   movzx r8d, byte ptr [rdi + 35]\n   mov [rdx + 68], r8d\n"
        "cmp eax, 11\n   jne 17f\n   test esi, esi\n   jz 14f\n   test ecx, esi\n   jnz 14f\n   test r13d, r13d\n   jz 14f\n"
        "cmp ebx, 256\n   ja 15f\n   cmp r12d, 32\n   ja 15f\n   cmp r13d, 4\n   ja 15f\n"
        "lea rcx, [rbx + 3]\n   and rcx, -4\n   add rcx, 40\n   cmp rcx, r11\n   ja 16f\n"
        "mov rax, r11\n   sub rax, rcx\n   shl r12, 3\n   cmp rax, r12\n   jb 16f\n   add rcx, r12\n   xor r12d, r12d\n"
        "2:  cmp r12d, r13d\n   jae 6f\n   mov rax, r11\n   sub rax, rcx\n   cmp rax, 40\n   jb 16f\n   lea rsi, [rdi + rcx]\n"
        "movzx ebx, byte ptr [rsi + 39]\n   test ebx, ebx\n   jz 14f\n   cmp ebx, 32\n   ja 15f\n   test r12d, r12d\n   jnz 3f\n"
        "mov eax, [rsi]\n   mov [rdx + 72], eax\n   mov eax, [rsi + 32]\n   mov [rdx + 76], eax\n"
        "movzx eax, byte ptr [rsi + 38]\n   mov [rdx + 80], eax\n   mov eax, [rsi + 16]\n   mov [rdx + 84], eax\n"
        "mov eax, [rsi + 4]\n   mov [rdx + 88], eax\n   mov eax, [rsi + 8]\n   mov [rdx + 92], eax\n"
        "mov eax, [rsi + 12]\n   mov [rdx + 96], eax\n   movzx eax, word ptr [rsi + 20]\n   mov [rdx + 100], eax\n"
        "movzx eax, word ptr [rsi + 22]\n   mov [rdx + 104], eax\n   movzx eax, word ptr [rsi + 24]\n   mov [rdx + 108], eax\n"
        "movzx eax, word ptr [rsi + 26]\n   mov [rdx + 112], eax\n"
        "3:  add rcx, 40\n   xor r8, r8\n   xor esi, esi\n"
        "4:  cmp rsi, rbx\n   jae 5f\n   mov rax, r11\n   sub rax, rcx\n   cmp rax, 8\n   jb 16f\n"
        "movzx eax, word ptr [rdi + rcx + 2]\n   add r8, rax\n   add rcx, 8\n   cmp r8, 4096\n   ja 15f\n"
        "lea rax, [rax + rax * 2]\n   shl rax, 3\n   mov r9, r11\n   sub r9, rcx\n   cmp r9, rax\n   jb 16f\n"
        "add rcx, rax\n   inc rsi\n   jmp 4b\n"
        "5:  inc r12d\n   jmp 2b\n"
        "6:  cmp rcx, r11\n   jne 16f\n"
        "12: xor eax, eax\n"
        "18: pop r13\n   pop r12\n   pop rbx\n" XWM_B3_EPI XWM_ASM_RET
        "13: mov eax, -3\n   jmp 18b\n"
        "14: mov eax, -2\n   jmp 18b\n"
        "15: mov eax, -5\n   jmp 18b\n"
        "16: mov eax, -4\n   jmp 18b\n"
        "17: mov eax, -6\n   jmp 18b\n"
        XWM_ASM_END(xwm_asm_parse_setup)

        /* xwm_asm_parse_error(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_error)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 0\n   jne 6f\n"
        "movzx eax, byte ptr [rdi + 1]\n   mov [rdx], eax\n   movzx eax, word ptr [rdi + 2]\n   mov [rdx + 4], eax\n"
        "mov eax, [rdi + 4]\n   mov [rdx + 8], eax\n   movzx eax, word ptr [rdi + 8]\n   mov [rdx + 12], eax\n"
        "movzx eax, byte ptr [rdi + 10]\n   mov [rdx + 16], eax\n   xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_error)

        /* xwm_asm_parse_atom_reply(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_atom_reply)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "cmp dword ptr [rdi + 4], 0\n   jne 5f\n   movzx eax, word ptr [rdi + 2]\n   mov [rdx], eax\n"
        "mov eax, [rdi + 8]\n   mov [rdx + 4], eax\n   xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "5:  mov eax, -4\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_atom_reply)

        /* xwm_asm_parse_atom_name_reply(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_atom_name_reply)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "movzx eax, word ptr [rdi + 8]\n   cmp eax, 256\n   ja 7f\n   lea rcx, [rax + 3]\n   shr rcx, 2\n"
        "cmp dword ptr [rdi + 4], ecx\n   jne 5f\n   sub rsi, 32\n   shr rsi, 2\n   cmp rsi, rcx\n   jb 5f\n"
        "movzx ecx, word ptr [rdi + 2]\n   mov [rdx], ecx\n   lea rcx, [rdi + 32]\n   mov [rdx + 8], rcx\n   mov [rdx + 16], rax\n"
        "xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "5:  mov eax, -4\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        "7:  mov eax, -5\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_atom_name_reply)

        /* xwm_asm_parse_property_reply(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_property_reply)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "movzx r8d, byte ptr [rdi + 1]\n   mov ecx, [rdi + 16]\n   test r8d, r8d\n   jnz 1f\n"
        "test ecx, ecx\n   jnz 4f\n   xor eax, eax\n   jmp 2f\n"
        "1:  cmp r8d, 8\n   jne 8f\n   cmp rcx, 65536\n   ja 7f\n   mov rax, rcx\n   jmp 2f\n"
        "8:  cmp r8d, 16\n   jne 9f\n   cmp rcx, 32768\n   ja 7f\n   lea rax, [rcx + rcx]\n   jmp 2f\n"
        "9:  cmp r8d, 32\n   jne 4f\n   cmp rcx, 16384\n   ja 7f\n   lea rax, [rcx * 4]\n"
        "2:  lea r9, [rax + 3]\n   shr r9, 2\n   cmp [rdi + 4], r9d\n   jne 5f\n"
        "sub rsi, 32\n   shr rsi, 2\n   cmp rsi, r9\n   jb 5f\n"
        "movzx esi, word ptr [rdi + 2]\n   mov [rdx], esi\n   mov [rdx + 4], r8d\n"
        "mov esi, [rdi + 8]\n   mov [rdx + 8], esi\n   mov esi, [rdi + 12]\n   mov [rdx + 12], esi\n"
        "mov [rdx + 16], ecx\n   lea rsi, [rdi + 32]\n   mov [rdx + 24], rsi\n   mov [rdx + 32], rax\n   xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "5:  mov eax, -4\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        "7:  mov eax, -5\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_property_reply)

        /* xwm_asm_parse_geometry_reply(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_geometry_reply)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "cmp dword ptr [rdi + 4], 0\n   jne 5f\n"
        "movzx eax, word ptr [rdi + 2]\n   mov [rdx], eax\n   movzx eax, byte ptr [rdi + 1]\n   mov [rdx + 4], eax\n"
        "mov eax, [rdi + 8]\n   mov [rdx + 8], eax\n   movzx eax, word ptr [rdi + 16]\n   mov [rdx + 12], eax\n"
        "movzx eax, word ptr [rdi + 18]\n   mov [rdx + 16], eax\n   movzx eax, word ptr [rdi + 20]\n   mov [rdx + 20], eax\n"
        "movsx eax, word ptr [rdi + 12]\n   mov [rdx + 24], eax\n   movsx eax, word ptr [rdi + 14]\n   mov [rdx + 28], eax\n"
        "xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "5:  mov eax, -4\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_geometry_reply)

        /* xwm_asm_parse_attributes_reply(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_attributes_reply)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "cmp dword ptr [rdi + 4], 3\n   jne 5f\n   cmp rsi, 44\n   jb 5f\n"
        "movzx eax, word ptr [rdi + 2]\n   mov [rdx], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 4], eax\n"
        "movzx eax, word ptr [rdi + 12]\n   mov [rdx + 8], eax\n   movzx eax, byte ptr [rdi + 14]\n   mov [rdx + 12], eax\n"
        "movzx eax, byte ptr [rdi + 15]\n   mov [rdx + 16], eax\n   movzx eax, byte ptr [rdi + 24]\n   mov [rdx + 20], eax\n"
        "movzx eax, byte ptr [rdi + 25]\n   mov [rdx + 24], eax\n   movzx eax, byte ptr [rdi + 26]\n   mov [rdx + 28], eax\n"
        "movzx eax, byte ptr [rdi + 27]\n   mov [rdx + 32], eax\n   mov eax, [rdi + 28]\n   mov [rdx + 36], eax\n"
        "mov eax, [rdi + 32]\n   mov [rdx + 40], eax\n   mov eax, [rdi + 36]\n   mov [rdx + 44], eax\n"
        "movzx eax, word ptr [rdi + 40]\n   mov [rdx + 48], eax\n   xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "5:  mov eax, -4\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_attributes_reply)

        /* xwm_asm_parse_extension_reply(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_extension_reply)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "cmp dword ptr [rdi + 4], 0\n   jne 5f\n   movzx eax, byte ptr [rdi + 8]\n   cmp eax, 1\n   ja 4f\n"
        "mov [rdx + 4], eax\n   movzx eax, word ptr [rdi + 2]\n   mov [rdx], eax\n"
        "movzx eax, byte ptr [rdi + 9]\n   mov [rdx + 8], eax\n   movzx eax, byte ptr [rdi + 10]\n   mov [rdx + 12], eax\n"
        "movzx eax, byte ptr [rdi + 11]\n   mov [rdx + 16], eax\n   xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "5:  mov eax, -4\n" XWM_B3_EPI XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_extension_reply)

        /* xwm_asm_parse_xres_client_pid(rdi=buf, rsi=len, rdx=pid) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_xres_client_pid)
        XWM_B3_PRO
        "push rbx\n"
        "test rdi, rdi\n   jz 4f\n   test rdx, rdx\n   jz 4f\n   cmp rsi, 32\n   jb 3f\n   cmp byte ptr [rdi], 1\n   jne 6f\n"
        "mov eax, [rdi + 4]\n   sub rsi, 32\n   shr rsi, 2\n   cmp rsi, rax\n   jb 5f\n"
        "lea rsi, [rax * 4 + 32]\n   mov dword ptr [rdx], 0\n   mov rcx, 32\n   mov r8d, [rdi + 8]\n   xor ebx, ebx\n"
        "1:  cmp ebx, r8d\n   jae 2f\n   mov rax, rsi\n   sub rax, rcx\n   cmp rax, 12\n   jb 5f\n"
        "mov r9d, [rdi + rcx + 8]\n   sub rax, 12\n   cmp r9, rax\n   ja 5f\n"
        "lea r9, [r9 + 3]\n   and r9, -4\n   cmp r9, rax\n   ja 5f\n"
        "cmp dword ptr [rdi + rcx + 8], 4\n   jne 7f\n   test dword ptr [rdi + rcx + 4], 2\n   jz 7f\n"
        "mov eax, [rdi + rcx + 12]\n   mov [rdx], eax\n"
        "7:  lea rcx, [rcx + r9 + 12]\n   inc ebx\n   jmp 1b\n"
        "2:  xor eax, eax\n"
        "8:  pop rbx\n" XWM_B3_EPI XWM_ASM_RET
        "3:  mov eax, -3\n   jmp 8b\n"
        "4:  mov eax, -2\n   jmp 8b\n"
        "5:  mov eax, -4\n   jmp 8b\n"
        "6:  mov eax, -6\n   jmp 8b\n"
        XWM_ASM_END(xwm_asm_parse_xres_client_pid)

        /* xwm_asm_parse_event(rdi=buf, rsi=len, rdx=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_event)
        XWM_B3_PRO
        "test rdi, rdi\n   jz 28f\n   test rdx, rdx\n   jz 28f\n   cmp rsi, 32\n   jb 27f\n"
        "movzx esi, byte ptr [rdi]\n   mov ecx, esi\n   and ecx, 0x7f\n   cmp ecx, 2\n   jb 29f\n"
        "xor eax, eax\n   mov r8d, 16\n"
        "1:  mov [rdx + r8 * 8 - 8], rax\n   dec r8\n   jnz 1b\n   mov [rdx + 128], eax\n"
        "mov [rdx + 4], ecx\n   shr esi, 7\n   mov [rdx + 8], esi\n"
        "movzx eax, word ptr [rdi + 2]\n   mov [rdx + 12], eax\n   movzx esi, byte ptr [rdi + 1]\n   mov [rdx + 16], esi\n"
        "cmp ecx, 22\n   je 10f\n   cmp ecx, 23\n   je 11f\n   cmp ecx, 28\n   je 12f\n   cmp ecx, 33\n   je 13f\n"
        "cmp ecx, 16\n   je 14f\n   cmp ecx, 17\n   je 15f\n   cmp ecx, 18\n   je 16f\n   cmp ecx, 19\n   je 17f\n"
        "cmp ecx, 20\n   je 18f\n   cmp ecx, 9\n   je 19f\n   cmp ecx, 10\n   je 19f\n   cmp ecx, 7\n   je 20f\n"
        "cmp ecx, 8\n   je 20f\n   cmp ecx, 29\n   je 21f\n   cmp ecx, 30\n   je 22f\n   cmp ecx, 31\n   je 23f\n"
        "cmp ecx, 12\n   je 24f\n   cmp ecx, 21\n   je 24f\n"
        "0:  xor eax, eax\n" XWM_B3_EPI XWM_ASM_RET
        "10: mov byte ptr [rdx], 6\n   mov eax, [rdi + 4]\n   mov [rdx + 20], eax\n"
        "movzx eax, byte ptr [rdi + 26]\n   mov [rdx + 60], eax\n   jmp 25f\n"
        "11: mov byte ptr [rdx], 3\n   mov eax, [rdi + 4]\n   mov [rdx + 28], eax\n"
        "movzx eax, word ptr [rdi + 26]\n   mov [rdx + 56], eax\n"
        "25: mov eax, [rdi + 8]\n   mov [rdx + 24], eax\n   mov eax, [rdi + 12]\n   mov [rdx + 32], eax\n"
        "movsx eax, word ptr [rdi + 16]\n   mov [rdx + 92], eax\n   movsx eax, word ptr [rdi + 18]\n   mov [rdx + 96], eax\n"
        "movzx eax, word ptr [rdi + 20]\n   mov [rdx + 100], eax\n   movzx eax, word ptr [rdi + 22]\n   mov [rdx + 104], eax\n"
        "movzx eax, word ptr [rdi + 24]\n   mov [rdx + 108], eax\n   jmp 0b\n"
        "12: mov byte ptr [rdx], 7\n   mov eax, [rdi + 4]\n   mov [rdx + 24], eax\n"
        "mov eax, [rdi + 8]\n   mov [rdx + 36], eax\n   mov eax, [rdi + 12]\n   mov [rdx + 40], eax\n"
        "movzx eax, byte ptr [rdi + 16]\n   mov [rdx + 44], eax\n   jmp 0b\n"
        "13: mov byte ptr [rdx], 8\n   mov [rdx + 48], esi\n   cmp esi, 8\n   je 26f\n   cmp esi, 16\n   je 26f\n"
        "cmp esi, 32\n   jne 28f\n   mov eax, [rdi + 12]\n   mov [rdx + 112], eax\n"
        "mov eax, [rdi + 16]\n   mov [rdx + 116], eax\n   mov eax, [rdi + 20]\n   mov [rdx + 120], eax\n"
        "mov eax, [rdi + 24]\n   mov [rdx + 124], eax\n   mov eax, [rdi + 28]\n   mov [rdx + 128], eax\n"
        "26: mov eax, [rdi + 4]\n   mov [rdx + 24], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 36], eax\n   jmp 0b\n"
        "14: mov byte ptr [rdx], 9\n   mov eax, [rdi + 4]\n   mov [rdx + 28], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 24], eax\n"
        "movsx eax, word ptr [rdi + 12]\n   mov [rdx + 92], eax\n   movsx eax, word ptr [rdi + 14]\n   mov [rdx + 96], eax\n"
        "movzx eax, word ptr [rdi + 16]\n   mov [rdx + 100], eax\n   movzx eax, word ptr [rdi + 18]\n   mov [rdx + 104], eax\n"
        "movzx eax, word ptr [rdi + 20]\n   mov [rdx + 108], eax\n   movzx eax, byte ptr [rdi + 22]\n   mov [rdx + 60], eax\n   jmp 0b\n"
        "15: mov al, 5\n   jmp 30f\n"
        "16: movzx eax, byte ptr [rdi + 12]\n   mov [rdx + 64], eax\n   mov al, 4\n   jmp 30f\n"
        "17: movzx eax, byte ptr [rdi + 12]\n   mov [rdx + 60], eax\n   mov al, 14\n"
        "30: mov byte ptr [rdx], al\n   mov eax, [rdi + 4]\n   mov [rdx + 20], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 24], eax\n   jmp 0b\n"
        "18: mov byte ptr [rdx], 2\n   mov eax, [rdi + 4]\n   mov [rdx + 28], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 24], eax\n   jmp 0b\n"
        "19: inc ecx\n   mov [rdx], ecx\n   mov eax, [rdi + 4]\n   mov [rdx + 20], eax\n   movzx eax, byte ptr [rdi + 8]\n   mov [rdx + 52], eax\n   jmp 0b\n"
        "20: add ecx, 5\n   mov [rdx], ecx\n   mov eax, [rdi + 12]\n   mov [rdx + 20], eax\n   movzx eax, byte ptr [rdi + 30]\n   mov [rdx + 52], eax\n   jmp 0b\n"
        "21: mov byte ptr [rdx], 15\n   mov eax, [rdi + 4]\n   mov [rdx + 40], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 68], eax\n"
        "mov eax, [rdi + 12]\n   mov [rdx + 76], eax\n   jmp 0b\n"
        "22: mov byte ptr [rdx], 16\n   mov eax, [rdi + 4]\n   mov [rdx + 40], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 68], eax\n"
        "mov eax, [rdi + 12]\n   mov [rdx + 72], eax\n   mov eax, [rdi + 16]\n   mov [rdx + 76], eax\n"
        "mov eax, [rdi + 20]\n   mov [rdx + 80], eax\n   mov eax, [rdi + 24]\n   mov [rdx + 84], eax\n   jmp 0b\n"
        "23: mov byte ptr [rdx], 17\n   mov eax, [rdi + 4]\n   mov [rdx + 40], eax\n   mov eax, [rdi + 8]\n   mov [rdx + 72], eax\n"
        "mov eax, [rdi + 12]\n   mov [rdx + 76], eax\n   mov eax, [rdi + 16]\n   mov [rdx + 80], eax\n"
        "mov eax, [rdi + 20]\n   mov [rdx + 84], eax\n   jmp 0b\n"
        "24: mov byte ptr [rdx], 1\n   jmp 0b\n"
        "27: mov eax, -3\n" XWM_B3_EPI XWM_ASM_RET
        "28: mov eax, -2\n" XWM_B3_EPI XWM_ASM_RET
        "29: mov eax, -6\n" XWM_B3_EPI XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_event)

        /* xwm_asm_parse_xfixes_selection(r8=buf, r9=len, r10d=first_event, r11=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_xfixes_selection)
        XWM_L4
        "test r8, r8\n   jz 4f\n   test r11, r11\n   jz 4f\n   cmp r9, 32\n   jb 3f\n"
        "cmp r10d, 36\n   jb 6f\n   cmp r10d, 127\n   ja 6f\n   movzx edx, byte ptr [r8]\n   mov ecx, edx\n   and ecx, 0x7f\n   cmp ecx, r10d\n   jne 6f\n"
        "xor eax, eax\n   mov rcx, 16\n"
        "1:  mov qword ptr [r11 + rcx * 8 - 8], rax\n   dec rcx\n   jnz 1b\n   mov dword ptr [r11 + 128], eax\n"
        "mov dword ptr [r11], 18\n   mov dword ptr [r11 + 4], r10d\n   shr edx, 7\n   mov dword ptr [r11 + 8], edx\n"
        "movzx eax, word ptr [r8 + 2]\n   mov dword ptr [r11 + 12], eax\n   movzx eax, byte ptr [r8 + 1]\n   mov dword ptr [r11 + 16], eax\n"
        "mov eax, dword ptr [r8 + 4]\n   mov dword ptr [r11 + 24], eax\n   mov eax, dword ptr [r8 + 8]\n   mov dword ptr [r11 + 68], eax\n"
        "mov eax, dword ptr [r8 + 12]\n   mov dword ptr [r11 + 76], eax\n   mov eax, dword ptr [r8 + 16]\n   mov dword ptr [r11 + 40], eax\n"
        "mov eax, dword ptr [r8 + 20]\n   mov dword ptr [r11 + 88], eax\n   xor eax, eax\n" XWM_ASM_RET
        "3:  mov eax, -3\n" XWM_ASM_RET
        "4:  mov eax, -2\n" XWM_ASM_RET
        "6:  mov eax, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_xfixes_selection)

        /* xwm_asm_latin1_utf8(r8=in, r9=n, r10=out) -> u64 */
        XWM_ASM_FUNC(xwm_asm_latin1_utf8)
        XWM_L3
        "test r9, r9\n   jz 4f\n   test r8, r8\n   jz 4f\n   test r10, r10\n   jz 4f\n   mov rax, r10\n   add r9, r8\n"
        "1:  movzx ecx, byte ptr [r8]\n   inc r8\n   test ecx, 0x80\n   jnz 2f\n"
        "mov byte ptr [r10], cl\n   inc r10\n   cmp r8, r9\n   jb 1b\n   sub r10, rax\n   mov rax, r10\n" XWM_ASM_RET
        "2:  mov edx, ecx\n   shr edx, 6\n   or edx, 0xc0\n   and ecx, 0x3f\n   or ecx, 0x80\n"
        "mov byte ptr [r10], dl\n   mov byte ptr [r10 + 1], cl\n   add r10, 2\n   cmp r8, r9\n   jb 1b\n"
        "sub r10, rax\n   mov rax, r10\n" XWM_ASM_RET
        "4:  xor eax, eax\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_latin1_utf8)

        /* xwm_asm_utf8_latin1(r8=cut, r9=in, r10=n, r11=out) -> u64 */
        XWM_ASM_FUNC(xwm_asm_utf8_latin1)
        "push rbx\n   push r12\n"
        XWM_L4
        "test r10, r10\n   jz 9f\n   test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test r11, r11\n   jz 9f\n"
        "mov r12, r8\n   mov ecx, dword ptr [r12]\n   mov edx, dword ptr [r12 + 4]\n   mov rbx, r11\n   add r10, r9\n"
        "1:  movzx eax, byte ptr [r9]\n   inc r9\n   test ecx, ecx\n   jz 3f\n"
        "mov r8d, eax\n   and r8d, 0xc0\n   cmp r8d, 0x80\n   jne 2f\n"
        "and eax, 0x3f\n   shl edx, 6\n   or edx, eax\n   dec ecx\n   jnz 7f\n"
        "mov eax, 63\n   cmp edx, 255\n   cmovbe eax, edx\n   mov byte ptr [r11], al\n   inc r11\n   jmp 7f\n"
        "2:  xor ecx, ecx\n   mov byte ptr [r11], 63\n   inc r11\n"
        "3:  test eax, 0x80\n   jnz 4f\n   mov byte ptr [r11], al\n   inc r11\n   jmp 7f\n"
        "4:  mov r8d, eax\n   and r8d, 0xe0\n   cmp r8d, 0xc0\n   jne 5f\n   mov ecx, 1\n   and eax, 0x1f\n   mov edx, eax\n   jmp 7f\n"
        "5:  mov r8d, eax\n   and r8d, 0xf0\n   cmp r8d, 0xe0\n   jne 6f\n   mov ecx, 2\n   and eax, 0x0f\n   mov edx, eax\n   jmp 7f\n"
        "6:  mov r8d, eax\n   and r8d, 0xf8\n   cmp r8d, 0xf0\n   jne 8f\n   mov ecx, 3\n   and eax, 0x07\n   mov edx, eax\n   jmp 7f\n"
        "8:  mov byte ptr [r11], 63\n   inc r11\n"
        "7:  cmp r9, r10\n   jb 1b\n   mov dword ptr [r12], ecx\n   mov dword ptr [r12 + 4], edx\n"
        "mov rax, r11\n   sub rax, rbx\n   pop r12\n   pop rbx\n" XWM_ASM_RET
        "9:  xor eax, eax\n   pop r12\n   pop rbx\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_utf8_latin1)

        ".att_syntax prefix\n"
);

#elif defined(XWM_ASM_RISCV64)

__asm__(
        XWM_ASM_SECTION

        /* xwm_asm_request_start(a0=out, a1=cap, a2=bytes, a3=opcode) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_start)
        "beqz a0, 4f\n   li t0, 4\n   bltu a2, t0, 4f\n   srli t1, a2, 2\n   li t0, 65535\n   bgtu t1, t0, 5f\n"
        "bgtu a2, a1, 6f\n   mv t2, a0\n   mv t3, a2\n   li t4, 8\n"
        "1:  bltu t3, t4, 2f\n   sd zero, 0(t2)\n   addi t2, t2, 8\n   addi t3, t3, -8\n   j 1b\n"
        "2:  beqz t3, 3f\n   sb zero, 0(t2)\n   addi t2, t2, 1\n   addi t3, t3, -1\n   j 2b\n"
        "3:  sb a3, 0(a0)\n   sh t1, 2(a0)\n   mv a0, a2\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -5\n" XWM_ASM_RET
        "6:  li a0, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_start)

        /* xwm_asm_request_words(a0=out, a1=cap, a2=op_data, a3=words, a4=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_words)
        "beqz a0, 4f\n   li t0, 65534\n   bgtu a4, t0, 5f\n   slli t1, a4, 2\n   addi t1, t1, 4\n"
        "bgtu t1, a1, 6f\n   beqz a4, 2f\n   beqz a3, 4f\n"
        "2:  sh a2, 0(a0)\n   addi t2, a4, 1\n   sh t2, 2(a0)\n   addi t3, a0, 4\n   li t4, 0\n"
        "1:  bgeu t4, a4, 3f\n   slli t5, t4, 2\n   add t6, a3, t5\n   lw t0, 0(t6)\n"
        "add t6, t3, t5\n   sw t0, 0(t6)\n   addi t4, t4, 1\n   j 1b\n"
        "3:  mv a0, t1\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -5\n" XWM_ASM_RET
        "6:  li a0, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_words)

        /* xwm_asm_request_name(a0=out, a1=cap, a2=op_data, a3=name, a4=name_len, a5=max_len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_request_name)
        "beqz a3, 4f\n   beqz a4, 4f\n   bgtu a4, a5, 5f\n   beqz a0, 4f\n"
        "addi t0, a4, 3\n   andi t0, t0, -4\n   addi t1, t0, 8\n   srli t2, t1, 2\n   li t3, 65535\n"
        "bgtu t2, t3, 5f\n   bgtu t1, a1, 6f\n"
        "sh a2, 0(a0)\n   sh t2, 2(a0)\n   sw a4, 4(a0)\n   addi t3, a0, 8\n   li t4, 0\n"
        "1:  bgeu t4, a4, 2f\n   add t5, a3, t4\n   lbu t6, 0(t5)\n   add t5, t3, t4\n   sb t6, 0(t5)\n   addi t4, t4, 1\n   j 1b\n"
        "2:  bgeu t4, t0, 3f\n   add t5, t3, t4\n   sb zero, 0(t5)\n   addi t4, t4, 1\n   j 2b\n"
        "3:  mv a0, t1\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -5\n" XWM_ASM_RET
        "6:  li a0, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_request_name)

        /* xwm_asm_event_words(a0=out, a1=cap, a2=type_detail, a3=words, a4=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_event_words)
        "beqz a0, 4f\n   li t0, 7\n   bgtu a4, t0, 4f\n   li t0, 32\n   bltu a1, t0, 6f\n"
        "beqz a4, 2f\n   beqz a3, 4f\n"
        "2:  sd zero, 0(a0)\n   sd zero, 8(a0)\n   sd zero, 16(a0)\n   sd zero, 24(a0)\n"
        "sh a2, 0(a0)\n   addi t1, a0, 4\n   li t2, 0\n"
        "1:  bgeu t2, a4, 3f\n   slli t3, t2, 2\n   add t4, a3, t3\n   lw t5, 0(t4)\n"
        "add t4, t1, t3\n   sw t5, 0(t4)\n   addi t2, t2, 1\n   j 1b\n"
        "3:  li a0, 32\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "6:  li a0, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_event_words)

        /* xwm_asm_enc_configure_window(a0=out, a1=cap, a2=window, a3=cfg) -> i64 */
        XWM_ASM_FUNC(xwm_asm_enc_configure_window)
        "beqz a3, 9f\n   lwu t0, 0(a3)\n   andi t1, t0, -128\n   bnez t1, 9f\n   li t2, 32768\n   li t3, 65535\n   li t4, 65534\n"
        "andi t1, t0, 1\n   beqz t1, 1f\n   lwu t5, 4(a3)\n   addw t5, t5, t2\n   slli t5, t5, 32\n   srli t5, t5, 32\n   bgtu t5, t3, 9f\n"
        "1:  andi t1, t0, 2\n   beqz t1, 2f\n   lwu t5, 8(a3)\n   addw t5, t5, t2\n   slli t5, t5, 32\n   srli t5, t5, 32\n   bgtu t5, t3, 9f\n"
        "2:  andi t1, t0, 4\n   beqz t1, 3f\n   lwu t5, 12(a3)\n   addiw t5, t5, -1\n   slli t5, t5, 32\n   srli t5, t5, 32\n   bgtu t5, t4, 9f\n"
        "3:  andi t1, t0, 8\n   beqz t1, 4f\n   lwu t5, 16(a3)\n   addiw t5, t5, -1\n   slli t5, t5, 32\n   srli t5, t5, 32\n   bgtu t5, t4, 9f\n"
        "4:  andi t1, t0, 16\n   beqz t1, 5f\n   lwu t5, 20(a3)\n   bgtu t5, t3, 9f\n"
        "5:  andi t1, t0, 64\n   beqz t1, 6f\n   lwu t5, 28(a3)\n   li t6, 4\n   bgtu t5, t6, 9f\n"
        "6:  mv t1, t0\n   li t2, 0\n"
        "7:  beqz t1, 8f\n   andi t3, t1, 1\n   add t2, t2, t3\n   srli t1, t1, 1\n   j 7b\n"
        "8:  beqz a0, 9f\n   slli t3, t2, 2\n   addi t3, t3, 12\n   bgtu t3, a1, 10f\n"
        "li t4, 12\n   sh t4, 0(a0)\n   addi t4, t2, 3\n   sh t4, 2(a0)\n   sw a2, 4(a0)\n   sw t0, 8(a0)\n"
        "addi t4, a0, 12\n   li t5, 0\n   li t6, 7\n"
        "11: bgeu t5, t6, 13f\n   srl t1, t0, t5\n   andi t1, t1, 1\n   beqz t1, 12f\n"
        "slli t1, t5, 2\n   add t1, a3, t1\n   lw t1, 4(t1)\n   sw t1, 0(t4)\n   addi t4, t4, 4\n"
        "12: addi t5, t5, 1\n   j 11b\n"
        "13: mv a0, t3\n" XWM_ASM_RET
        "9:  li a0, -2\n" XWM_ASM_RET
        "10: li a0, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_enc_configure_window)

        /* xwm_asm_enc_change_window_attributes(a0=out, a1=cap, a2=win, a3=mask, a4=vals, a5=count) -> i64 */
        XWM_ASM_FUNC(xwm_asm_enc_change_window_attributes)
        "slli a2, a2, 32\n   srli a2, a2, 32\n   slli a3, a3, 32\n   srli a3, a3, 32\n"
        "beqz a2, 4f\n   beqz a3, 4f\n   li t0, 0x7fff\n   bgtu a3, t0, 4f\n   beqz a4, 4f\n"
        "mv t0, a3\n   li t1, 0\n"
        "1:  beqz t0, 2f\n   andi t2, t0, 1\n   add t1, t1, t2\n   srli t0, t0, 1\n   j 1b\n"
        "2:  bne t1, a5, 4f\n   beqz a0, 4f\n   slli t2, a5, 2\n   addi t2, t2, 12\n   bgtu t2, a1, 6f\n"
        "li t3, 2\n   sh t3, 0(a0)\n   addi t3, a5, 3\n   sh t3, 2(a0)\n   sw a2, 4(a0)\n   sw a3, 8(a0)\n"
        "addi t3, a0, 12\n   li t4, 0\n"
        "3:  bgeu t4, a5, 5f\n   slli t5, t4, 2\n   add t6, a4, t5\n   lw t0, 0(t6)\n"
        "add t6, t3, t5\n   sw t0, 0(t6)\n   addi t4, t4, 1\n   j 3b\n"
        "5:  mv a0, t2\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "6:  li a0, -1\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_enc_change_window_attributes)

        /* xwm_asm_server_message_size(a0=buf, a1=len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_server_message_size)
        "beqz a0, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   andi t1, t1, 0x7f\n"
        "li t2, 1\n   beq t1, t2, 1f\n   li t2, 35\n   beq t1, t2, 1f\n   li a0, 32\n" XWM_ASM_RET
        "1:  lwu t3, 4(a0)\n   slli t3, t3, 2\n   addi t3, t3, 32\n   li t4, 65568\n   bgtu t3, t4, 5f\n   mv a0, t3\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_server_message_size)

        /* xwm_asm_setup_size(a0=buf, a1=len) -> i64 */
        XWM_ASM_FUNC(xwm_asm_setup_size)
        "beqz a0, 4f\n   li t0, 8\n   bltu a1, t0, 3f\n   lhu t1, 6(a0)\n   slli t1, t1, 2\n   addi t1, t1, 8\n"
        "li t2, 65536\n   bgtu t1, t2, 5f\n   mv a0, t1\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_setup_size)

        /* xwm_asm_parse_setup(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_setup)
        "beqz a0, 14f\n   beqz a2, 14f\n   li t0, 8\n   bltu a1, t0, 13f\n"
        "lhu a3, 6(a0)\n   slli a3, a3, 2\n   addi a3, a3, 8\n   li t0, 65536\n   bgtu a3, t0, 15f\n"
        "bltu a1, a3, 16f\n   lbu t0, 0(a0)\n   sw t0, 0(a2)\n   bnez t0, 1f\n"
        "lbu t1, 1(a0)\n   lhu t2, 2(a0)\n   lhu t3, 4(a0)\n   sw t1, 4(a2)\n   sw t2, 8(a2)\n   sw t3, 12(a2)\n"
        "addi t1, t1, 8\n   bgtu t1, a3, 16f\n   li a0, 0\n" XWM_ASM_RET
        "1:  li t1, 2\n   beq t0, t1, 12f\n   li t1, 1\n   bne t0, t1, 17f\n   li t1, 40\n   bltu a3, t1, 16f\n"
        "lhu a4, 2(a0)\n   sw a4, 8(a2)\n   lhu t0, 4(a0)\n   sw t0, 12(a2)\n"
        "lwu t0, 8(a0)\n   sw t0, 16(a2)\n   lwu a5, 12(a0)\n   sw a5, 20(a2)\n"
        "lwu a6, 16(a0)\n   sw a6, 24(a2)\n   lwu t0, 20(a0)\n   sw t0, 28(a2)\n"
        "lhu t1, 24(a0)\n   sw t1, 32(a2)\n   lhu t0, 26(a0)\n   sw t0, 36(a2)\n"
        "lbu t2, 29(a0)\n   sw t2, 40(a2)\n   lbu a7, 28(a0)\n   sw a7, 44(a2)\n"
        "lbu t0, 30(a0)\n   sw t0, 48(a2)\n   lbu t0, 31(a0)\n   sw t0, 52(a2)\n"
        "lbu t0, 32(a0)\n   sw t0, 56(a2)\n   lbu t0, 33(a0)\n   sw t0, 60(a2)\n"
        "lbu t0, 34(a0)\n   sw t0, 64(a2)\n   lbu t0, 35(a0)\n   sw t0, 68(a2)\n"
        "li t0, 11\n   bne a4, t0, 17f\n   beqz a6, 14f\n   and t0, a5, a6\n   bnez t0, 14f\n   beqz a7, 14f\n"
        "li t0, 256\n   bgtu t1, t0, 15f\n   li t0, 32\n   bgtu t2, t0, 15f\n   li t0, 4\n   bgtu a7, t0, 15f\n"
        "addi a4, t1, 3\n   andi a4, a4, -4\n   addi a4, a4, 40\n   bgtu a4, a3, 16f\n"
        "sub t0, a3, a4\n   slli t2, t2, 3\n   bltu t0, t2, 16f\n   add a4, a4, t2\n   li a5, 0\n"
        "2:  bgeu a5, a7, 6f\n   sub t0, a3, a4\n   li t1, 40\n   bltu t0, t1, 16f\n   add t2, a0, a4\n"
        "lbu a6, 39(t2)\n   beqz a6, 14f\n   li t0, 32\n   bgtu a6, t0, 15f\n   bnez a5, 3f\n"
        "lwu t0, 0(t2)\n   sw t0, 72(a2)\n   lwu t0, 32(t2)\n   sw t0, 76(a2)\n   lbu t0, 38(t2)\n   sw t0, 80(a2)\n"
        "lwu t0, 16(t2)\n   sw t0, 84(a2)\n   lwu t0, 4(t2)\n   sw t0, 88(a2)\n   lwu t0, 8(t2)\n   sw t0, 92(a2)\n"
        "lwu t0, 12(t2)\n   sw t0, 96(a2)\n   lhu t0, 20(t2)\n   sw t0, 100(a2)\n   lhu t0, 22(t2)\n   sw t0, 104(a2)\n"
        "lhu t0, 24(t2)\n   sw t0, 108(a2)\n   lhu t0, 26(t2)\n   sw t0, 112(a2)\n"
        "3:  addi a4, a4, 40\n   li t3, 0\n   li t4, 0\n"
        "4:  bgeu t4, a6, 5f\n   sub t0, a3, a4\n   li t1, 8\n   bltu t0, t1, 16f\n"
        "add t2, a0, a4\n   lhu t5, 2(t2)\n   add t3, t3, t5\n   addi a4, a4, 8\n"
        "li t0, 4096\n   bgtu t3, t0, 15f\n   slli t6, t5, 1\n   add t6, t6, t5\n   slli t6, t6, 3\n"
        "sub t0, a3, a4\n   bltu t0, t6, 16f\n   add a4, a4, t6\n   addi t4, t4, 1\n   j 4b\n"
        "5:  addi a5, a5, 1\n   j 2b\n"
        "6:  bne a4, a3, 16f\n"
        "12: li a0, 0\n" XWM_ASM_RET
        "13: li a0, -3\n" XWM_ASM_RET
        "14: li a0, -2\n" XWM_ASM_RET
        "15: li a0, -5\n" XWM_ASM_RET
        "16: li a0, -4\n" XWM_ASM_RET
        "17: li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_setup)

        /* xwm_asm_parse_error(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_error)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   bnez t1, 6f\n"
        "lbu t0, 1(a0)\n   sw t0, 0(a2)\n   lhu t0, 2(a0)\n   sw t0, 4(a2)\n   lwu t0, 4(a0)\n   sw t0, 8(a2)\n"
        "lhu t0, 8(a0)\n   sw t0, 12(a2)\n   lbu t0, 10(a0)\n   sw t0, 16(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_error)

        /* xwm_asm_parse_atom_reply(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_atom_reply)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lwu t1, 4(a0)\n   bnez t1, 5f\n   lhu t0, 2(a0)\n   sw t0, 0(a2)\n   lwu t0, 8(a0)\n   sw t0, 4(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_atom_reply)

        /* xwm_asm_parse_atom_name_reply(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_atom_name_reply)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lhu t1, 8(a0)\n   li t0, 256\n   bgtu t1, t0, 7f\n   addi t2, t1, 3\n   srli t2, t2, 2\n"
        "lwu t3, 4(a0)\n   bne t3, t2, 5f\n   addi t4, a1, -32\n   srli t4, t4, 2\n   bltu t4, t2, 5f\n"
        "lhu t0, 2(a0)\n   sw t0, 0(a2)\n   addi t0, a0, 32\n   sd t0, 8(a2)\n   sd t1, 16(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        "7:  li a0, -5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_atom_name_reply)

        /* xwm_asm_parse_property_reply(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_property_reply)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lbu t1, 1(a0)\n   lwu t2, 16(a0)\n   bnez t1, 1f\n   bnez t2, 4f\n   li t3, 0\n   j 2f\n"
        "1:  li t0, 8\n   bne t1, t0, 8f\n   li t0, 65536\n   bgtu t2, t0, 7f\n   mv t3, t2\n   j 2f\n"
        "8:  li t0, 16\n   bne t1, t0, 9f\n   li t0, 32768\n   bgtu t2, t0, 7f\n   slli t3, t2, 1\n   j 2f\n"
        "9:  li t0, 32\n   bne t1, t0, 4f\n   li t0, 16384\n   bgtu t2, t0, 7f\n   slli t3, t2, 2\n"
        "2:  addi t4, t3, 3\n   srli t4, t4, 2\n   lwu t5, 4(a0)\n   bne t5, t4, 5f\n"
        "addi t5, a1, -32\n   srli t5, t5, 2\n   bltu t5, t4, 5f\n"
        "lhu t0, 2(a0)\n   sw t0, 0(a2)\n   sw t1, 4(a2)\n   lwu t0, 8(a0)\n   sw t0, 8(a2)\n"
        "lwu t0, 12(a0)\n   sw t0, 12(a2)\n   sw t2, 16(a2)\n   addi t0, a0, 32\n   sd t0, 24(a2)\n   sd t3, 32(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        "7:  li a0, -5\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_property_reply)

        /* xwm_asm_parse_geometry_reply(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_geometry_reply)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lwu t1, 4(a0)\n   bnez t1, 5f\n"
        "lhu t0, 2(a0)\n   sw t0, 0(a2)\n   lbu t0, 1(a0)\n   sw t0, 4(a2)\n   lwu t0, 8(a0)\n   sw t0, 8(a2)\n"
        "lhu t0, 16(a0)\n   sw t0, 12(a2)\n   lhu t0, 18(a0)\n   sw t0, 16(a2)\n   lhu t0, 20(a0)\n   sw t0, 20(a2)\n"
        "lh t0, 12(a0)\n   sw t0, 24(a2)\n   lh t0, 14(a0)\n   sw t0, 28(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_geometry_reply)

        /* xwm_asm_parse_attributes_reply(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_attributes_reply)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lwu t1, 4(a0)\n   li t0, 3\n   bne t1, t0, 5f\n   li t0, 44\n   bltu a1, t0, 5f\n"
        "lhu t0, 2(a0)\n   sw t0, 0(a2)\n   lwu t0, 8(a0)\n   sw t0, 4(a2)\n   lhu t0, 12(a0)\n   sw t0, 8(a2)\n"
        "lbu t0, 14(a0)\n   sw t0, 12(a2)\n   lbu t0, 15(a0)\n   sw t0, 16(a2)\n   lbu t0, 24(a0)\n   sw t0, 20(a2)\n"
        "lbu t0, 25(a0)\n   sw t0, 24(a2)\n   lbu t0, 26(a0)\n   sw t0, 28(a2)\n   lbu t0, 27(a0)\n   sw t0, 32(a2)\n"
        "lwu t0, 28(a0)\n   sw t0, 36(a2)\n   lwu t0, 32(a0)\n   sw t0, 40(a2)\n   lwu t0, 36(a0)\n   sw t0, 44(a2)\n"
        "lhu t0, 40(a0)\n   sw t0, 48(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_attributes_reply)

        /* xwm_asm_parse_extension_reply(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_extension_reply)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lwu t1, 4(a0)\n   bnez t1, 5f\n   lbu t1, 8(a0)\n   li t0, 1\n   bgtu t1, t0, 4f\n"
        "sw t1, 4(a2)\n   lhu t0, 2(a0)\n   sw t0, 0(a2)\n   lbu t0, 9(a0)\n   sw t0, 8(a2)\n"
        "lbu t0, 10(a0)\n   sw t0, 12(a2)\n   lbu t0, 11(a0)\n   sw t0, 16(a2)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_extension_reply)

        /* xwm_asm_parse_xres_client_pid(a0=buf, a1=len, a2=pid) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_xres_client_pid)
        "beqz a0, 4f\n   beqz a2, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n   lbu t1, 0(a0)\n   li t0, 1\n   bne t1, t0, 6f\n"
        "lwu t1, 4(a0)\n   addi t2, a1, -32\n   srli t2, t2, 2\n   bltu t2, t1, 5f\n"
        "slli a3, t1, 2\n   addi a3, a3, 32\n   sw zero, 0(a2)\n   li a4, 32\n   lwu a5, 8(a0)\n   li a6, 0\n   li t6, 12\n"
        "1:  bgeu a6, a5, 2f\n   sub t0, a3, a4\n   bltu t0, t6, 5f\n   add t1, a0, a4\n"
        "lwu t2, 8(t1)\n   addi t0, t0, -12\n   bgtu t2, t0, 5f\n   addi t3, t2, 3\n   andi t3, t3, -4\n   bgtu t3, t0, 5f\n"
        "li t4, 4\n   bne t2, t4, 7f\n   lwu t4, 4(t1)\n   andi t4, t4, 2\n   beqz t4, 7f\n   lwu t4, 12(t1)\n   sw t4, 0(a2)\n"
        "7:  addi a4, a4, 12\n   add a4, a4, t3\n   addi a6, a6, 1\n   j 1b\n"
        "2:  li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "5:  li a0, -4\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_xres_client_pid)

        /* xwm_asm_parse_event(a0=buf, a1=len, a2=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_event)
        "beqz a0, 28f\n   beqz a2, 28f\n   li t0, 32\n   bltu a1, t0, 27f\n"
        "lbu t1, 0(a0)\n   andi t2, t1, 0x7f\n   li t0, 2\n   bltu t2, t0, 29f\n"
        "mv t3, a2\n   addi t4, a2, 128\n"
        "1:  sd zero, 0(t3)\n   addi t3, t3, 8\n   bltu t3, t4, 1b\n   sw zero, 128(a2)\n"
        "sw t2, 4(a2)\n   srli t1, t1, 7\n   sw t1, 8(a2)\n   lhu t0, 2(a0)\n   sw t0, 12(a2)\n   lbu t3, 1(a0)\n   sw t3, 16(a2)\n"
        "li t0, 22\n   beq t2, t0, 10f\n   li t0, 23\n   beq t2, t0, 11f\n   li t0, 28\n   beq t2, t0, 12f\n   li t0, 33\n   beq t2, t0, 13f\n"
        "li t0, 16\n   beq t2, t0, 14f\n   li t0, 17\n   beq t2, t0, 15f\n   li t0, 18\n   beq t2, t0, 16f\n   li t0, 19\n   beq t2, t0, 17f\n"
        "li t0, 20\n   beq t2, t0, 18f\n   li t0, 9\n   beq t2, t0, 19f\n   li t0, 10\n   beq t2, t0, 19f\n   li t0, 7\n   beq t2, t0, 20f\n"
        "li t0, 8\n   beq t2, t0, 20f\n   li t0, 29\n   beq t2, t0, 21f\n   li t0, 30\n   beq t2, t0, 22f\n   li t0, 31\n   beq t2, t0, 23f\n"
        "li t0, 12\n   beq t2, t0, 24f\n   li t0, 21\n   beq t2, t0, 24f\n   li a0, 0\n" XWM_ASM_RET
        "10: li t0, 6\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 20(a2)\n   lbu t0, 26(a0)\n   sw t0, 60(a2)\n   j 25f\n"
        "11: li t0, 3\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 28(a2)\n   lhu t0, 26(a0)\n   sw t0, 56(a2)\n"
        "25: lwu t0, 8(a0)\n   sw t0, 24(a2)\n   lwu t0, 12(a0)\n   sw t0, 32(a2)\n   lh t0, 16(a0)\n   sw t0, 92(a2)\n"
        "lh t0, 18(a0)\n   sw t0, 96(a2)\n   lhu t0, 20(a0)\n   sw t0, 100(a2)\n   lhu t0, 22(a0)\n   sw t0, 104(a2)\n"
        "lhu t0, 24(a0)\n   sw t0, 108(a2)\n   li a0, 0\n" XWM_ASM_RET
        "12: li t0, 7\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 24(a2)\n   lwu t0, 8(a0)\n   sw t0, 36(a2)\n"
        "lwu t0, 12(a0)\n   sw t0, 40(a2)\n   lbu t0, 16(a0)\n   sw t0, 44(a2)\n   li a0, 0\n" XWM_ASM_RET
        "13: li t0, 8\n   sw t0, 0(a2)\n   sw t3, 48(a2)\n   li t0, 8\n   beq t3, t0, 26f\n   li t0, 16\n   beq t3, t0, 26f\n"
        "li t0, 32\n   bne t3, t0, 28f\n   lwu t0, 12(a0)\n   sw t0, 112(a2)\n   lwu t0, 16(a0)\n   sw t0, 116(a2)\n"
        "lwu t0, 20(a0)\n   sw t0, 120(a2)\n   lwu t0, 24(a0)\n   sw t0, 124(a2)\n   lwu t0, 28(a0)\n   sw t0, 128(a2)\n"
        "26: lwu t0, 4(a0)\n   sw t0, 24(a2)\n   lwu t0, 8(a0)\n   sw t0, 36(a2)\n   li a0, 0\n" XWM_ASM_RET
        "14: li t0, 9\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 28(a2)\n   lwu t0, 8(a0)\n   sw t0, 24(a2)\n"
        "lh t0, 12(a0)\n   sw t0, 92(a2)\n   lh t0, 14(a0)\n   sw t0, 96(a2)\n   lhu t0, 16(a0)\n   sw t0, 100(a2)\n"
        "lhu t0, 18(a0)\n   sw t0, 104(a2)\n   lhu t0, 20(a0)\n   sw t0, 108(a2)\n   lbu t0, 22(a0)\n   sw t0, 60(a2)\n   li a0, 0\n" XWM_ASM_RET
        "15: li t0, 5\n   j 30f\n"
        "16: lbu t1, 12(a0)\n   sw t1, 64(a2)\n   li t0, 4\n   j 30f\n"
        "17: lbu t1, 12(a0)\n   sw t1, 60(a2)\n   li t0, 14\n"
        "30: sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 20(a2)\n   lwu t0, 8(a0)\n   sw t0, 24(a2)\n   li a0, 0\n" XWM_ASM_RET
        "18: li t0, 2\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 28(a2)\n   lwu t0, 8(a0)\n   sw t0, 24(a2)\n   li a0, 0\n" XWM_ASM_RET
        "19: addi t0, t2, 1\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 20(a2)\n   lbu t0, 8(a0)\n   sw t0, 52(a2)\n   li a0, 0\n" XWM_ASM_RET
        "20: addi t0, t2, 5\n   sw t0, 0(a2)\n   lwu t0, 12(a0)\n   sw t0, 20(a2)\n   lbu t0, 30(a0)\n   sw t0, 52(a2)\n   li a0, 0\n" XWM_ASM_RET
        "21: li t0, 15\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 40(a2)\n   lwu t0, 8(a0)\n   sw t0, 68(a2)\n   lwu t0, 12(a0)\n   sw t0, 76(a2)\n   li a0, 0\n" XWM_ASM_RET
        "22: li t0, 16\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 40(a2)\n   lwu t0, 8(a0)\n   sw t0, 68(a2)\n   lwu t0, 12(a0)\n   sw t0, 72(a2)\n"
        "lwu t0, 16(a0)\n   sw t0, 76(a2)\n   lwu t0, 20(a0)\n   sw t0, 80(a2)\n   lwu t0, 24(a0)\n   sw t0, 84(a2)\n   li a0, 0\n" XWM_ASM_RET
        "23: li t0, 17\n   sw t0, 0(a2)\n   lwu t0, 4(a0)\n   sw t0, 40(a2)\n   lwu t0, 8(a0)\n   sw t0, 72(a2)\n   lwu t0, 12(a0)\n   sw t0, 76(a2)\n"
        "lwu t0, 16(a0)\n   sw t0, 80(a2)\n   lwu t0, 20(a0)\n   sw t0, 84(a2)\n   li a0, 0\n" XWM_ASM_RET
        "24: li t0, 1\n   sw t0, 0(a2)\n   li a0, 0\n" XWM_ASM_RET
        "27: li a0, -3\n" XWM_ASM_RET
        "28: li a0, -2\n" XWM_ASM_RET
        "29: li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_event)

        /* xwm_asm_parse_xfixes_selection(a0=buf, a1=len, a2=first_event, a3=out) -> i32 */
        XWM_ASM_FUNC(xwm_asm_parse_xfixes_selection)
        "slli a2, a2, 32\n   srli a2, a2, 32\n   beqz a0, 4f\n   beqz a3, 4f\n   li t0, 32\n   bltu a1, t0, 3f\n"
        "li t0, 36\n   bltu a2, t0, 6f\n   li t0, 127\n   bgtu a2, t0, 6f\n   lbu t1, 0(a0)\n   andi t2, t1, 0x7f\n   bne t2, a2, 6f\n"
        "mv t3, a3\n   addi t4, a3, 128\n"
        "1:  sd zero, 0(t3)\n   addi t3, t3, 8\n   bltu t3, t4, 1b\n   sw zero, 128(a3)\n"
        "li t0, 18\n   sw t0, 0(a3)\n   sw a2, 4(a3)\n   srli t1, t1, 7\n   sw t1, 8(a3)\n"
        "lhu t0, 2(a0)\n   sw t0, 12(a3)\n   lbu t0, 1(a0)\n   sw t0, 16(a3)\n   lwu t0, 4(a0)\n   sw t0, 24(a3)\n"
        "lwu t0, 8(a0)\n   sw t0, 68(a3)\n   lwu t0, 12(a0)\n   sw t0, 76(a3)\n   lwu t0, 16(a0)\n   sw t0, 40(a3)\n"
        "lwu t0, 20(a0)\n   sw t0, 88(a3)\n   li a0, 0\n" XWM_ASM_RET
        "3:  li a0, -3\n" XWM_ASM_RET
        "4:  li a0, -2\n" XWM_ASM_RET
        "6:  li a0, -6\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_parse_xfixes_selection)

        /* xwm_asm_latin1_utf8(a0=in, a1=n, a2=out) -> u64 */
        XWM_ASM_FUNC(xwm_asm_latin1_utf8)
        "beqz a1, 4f\n   beqz a0, 4f\n   beqz a2, 4f\n   mv a3, a2\n   add a1, a0, a1\n   li t2, 0x80\n"
        "1:  lbu t0, 0(a0)\n   addi a0, a0, 1\n   bgeu t0, t2, 2f\n   sb t0, 0(a2)\n   addi a2, a2, 1\n"
        "bltu a0, a1, 1b\n   sub a0, a2, a3\n" XWM_ASM_RET
        "2:  srli t1, t0, 6\n   ori t1, t1, 0xc0\n   andi t0, t0, 0x3f\n   ori t0, t0, 0x80\n"
        "sb t1, 0(a2)\n   sb t0, 1(a2)\n   addi a2, a2, 2\n   bltu a0, a1, 1b\n   sub a0, a2, a3\n" XWM_ASM_RET
        "4:  li a0, 0\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_latin1_utf8)

        /* xwm_asm_utf8_latin1(a0=cut, a1=in, a2=n, a3=out) -> u64 */
        XWM_ASM_FUNC(xwm_asm_utf8_latin1)
        "beqz a2, 9f\n   beqz a0, 9f\n   beqz a1, 9f\n   beqz a3, 9f\n"
        "lwu a4, 0(a0)\n   lwu a5, 4(a0)\n   mv a6, a3\n   add a2, a1, a2\n   li a7, 63\n   li t5, 0x80\n   li t6, 255\n"
        "1:  lbu t0, 0(a1)\n   addi a1, a1, 1\n   beqz a4, 3f\n   andi t1, t0, 0xc0\n   bne t1, t5, 2f\n"
        "andi t1, t0, 0x3f\n   slliw a5, a5, 6\n   or a5, a5, t1\n   slli a5, a5, 32\n   srli a5, a5, 32\n   addi a4, a4, -1\n   bnez a4, 7f\n"
        "mv t1, a5\n   bleu t1, t6, 10f\n   mv t1, a7\n"
        "10: sb t1, 0(a3)\n   addi a3, a3, 1\n   j 7f\n"
        "2:  li a4, 0\n   sb a7, 0(a3)\n   addi a3, a3, 1\n"
        "3:  bgeu t0, t5, 4f\n   sb t0, 0(a3)\n   addi a3, a3, 1\n   j 7f\n"
        "4:  andi t1, t0, 0xe0\n   li t2, 0xc0\n   bne t1, t2, 5f\n   li a4, 1\n   andi a5, t0, 0x1f\n   j 7f\n"
        "5:  andi t1, t0, 0xf0\n   li t2, 0xe0\n   bne t1, t2, 6f\n   li a4, 2\n   andi a5, t0, 0x0f\n   j 7f\n"
        "6:  andi t1, t0, 0xf8\n   li t2, 0xf0\n   bne t1, t2, 8f\n   li a4, 3\n   andi a5, t0, 0x07\n   j 7f\n"
        "8:  sb a7, 0(a3)\n   addi a3, a3, 1\n"
        "7:  bltu a1, a2, 1b\n   sw a4, 0(a0)\n   sw a5, 4(a0)\n   sub a0, a3, a6\n" XWM_ASM_RET
        "9:  li a0, 0\n" XWM_ASM_RET
        XWM_ASM_END(xwm_asm_utf8_latin1)
);

#endif

#endif /* XWM_WIRE_ASM_IMPLEMENTATION */
