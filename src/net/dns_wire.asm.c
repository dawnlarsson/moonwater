/*
        RFC 1035 / mDNS / DNS-SD wire-format primitives in pure assembly:
          - dns_wire_write_name     : dotted ASCII name -> RFC 1035 length-prefixed wire name
          - dns_wire_copy_name      : pointer-safe bounded wire name decompress (loop/hop/budget)
          - dns_wire_skip_name      : zero-buffer pointer-safe wire name validator & skipper
          - dns_wire_reply_identity : DNS header txid/QDCOUNT + exact/case-folded question check
          - dns_wire_records_end    : zero-buffer bounded RR section walker (shared pointer budget)
          - dns_wire_to_host        : uncompressed wire labels -> NUL-terminated dotted ASCII host

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define NET_DNS_WIRE_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_NET_DNS_WIRE_H
#define MOONWATER_NET_DNS_WIRE_H

#ifdef STANDARD_MODERN_C
typedef p8  dw_u8;
typedef p32 dw_u32;
typedef b32 dw_i32;
typedef p64 dw_u64;
typedef b64 dw_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT32_TYPE__) && defined(__INT32_TYPE__) && \
    defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  dw_u8;
typedef __UINT32_TYPE__ dw_u32;
typedef __INT32_TYPE__  dw_i32;
typedef __UINT64_TYPE__ dw_u64;
typedef __INT64_TYPE__  dw_i64;
#else
#include <stdint.h>
typedef uint8_t  dw_u8;
typedef uint32_t dw_u32;
typedef int32_t  dw_i32;
typedef uint64_t dw_u64;
typedef int64_t  dw_i64;
#endif

#define DW_MALFORMED    (-3)
#define DW_HEADER       12u
#define DW_NAME_MAX     255u
#define DW_POINTER_HOPS 127u

#if defined(DNS_MALFORMED) && defined(DNS_NAME_MAX) && defined(DNS_POINTER_HOPS) && defined(DNS_HEADER)
_Static_assert(DW_MALFORMED == DNS_MALFORMED && DW_NAME_MAX == DNS_NAME_MAX &&
               DW_POINTER_HOPS == DNS_POINTER_HOPS && DW_HEADER == DNS_HEADER,
               "DW_* constants mismatch");
#endif

dw_i64 dns_wire_write_name(dw_u8 *into, dw_u64 room, const char *name);
dw_i64 dns_wire_copy_name(const dw_u8 *message, dw_u64 size, dw_u64 at,
                          dw_u8 *into, dw_u64 room,
                          dw_u64 *ended_out, dw_u64 *pointers_io);
dw_i64 dns_wire_skip_name(const dw_u8 *message, dw_u64 size, dw_u64 at);
dw_i32 dns_wire_reply_identity(const dw_u8 *reply, dw_u64 size, dw_u32 id,
                               const dw_u8 *request, dw_u64 question_length,
                               dw_u32 folded);
dw_i64 dns_wire_records_end(const dw_u8 *message, dw_u64 size,
                            dw_u64 at, dw_u64 count, dw_u64 max_pointers);
dw_u64 dns_wire_to_host(const dw_u8 *wire, dw_u64 len, dw_u8 *out, dw_u64 room);

#endif /* MOONWATER_NET_DNS_WIRE_H */

#ifdef NET_DNS_WIRE_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define DW_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define DW_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define DW_RISCV64 1
# else
#  error "src/net/dns_wire.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define DW_ARM64 1
# elif defined(X64) && X64
#  define DW_X64 1
# elif defined(RISCV64) && RISCV64
#  define DW_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define DW_ASM_SECTION   ASM_SECTION
# define DW_ASM_FUNC(n)   ASM_FUNC(n)
# define DW_ASM_END(n)    ASM_END(n)
# define DW_ASM_RET       ASM_RET
#else
# define DW_ASM_SECTION   ".text\n"
# define DW_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define DW_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define DW_ASM_END(n)   ""
# elif defined(_WIN32)
#  define DW_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define DW_ASM_END(n)   ""
# else
#  if defined(DW_ARM64)
#   define DW_ASM_FTYPE   "%function"
#  else
#   define DW_ASM_FTYPE   "@function"
#  endif
#  define DW_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " DW_ASM_FTYPE "\n" #n ":\n"
#  define DW_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(DW_ARM64)

__asm__(
        DW_ASM_SECTION

        /* dns_wire_write_name(x0=into, x1=room, x2=name) -> bytes written or -3 */
        DW_ASM_FUNC(dns_wire_write_name)
        "cbz x0, 5f\n   cbz x2, 5f\n   cbz x1, 5f\n   mov x3, #255\n   cmp x1, x3\n   csel x1, x3, x1, hi\n"
        "sub x1, x1, #1\n   mov x3, #0\n"
        "1:  ldrb w4, [x2]\n   cbz w4, 4f\n   mov x5, x3\n   add x6, x3, #1\n   mov w7, #0\n"
        "2:  cmp w4, #46\n   b.eq 6f\n   add w7, w7, #1\n   cmp w7, #63\n   b.hi 5f\n"
        "cmp x6, x1\n   b.hs 5f\n   strb w4, [x0, x6]\n   add x6, x6, #1\n   add x2, x2, #1\n"
        "ldrb w4, [x2]\n   cbnz w4, 2b\n   b 3f\n"
        "6:  add x2, x2, #1\n"
        "3:  cbz w7, 5f\n   strb w7, [x0, x5]\n   mov x3, x6\n   b 1b\n"
        "4:  strb wzr, [x0, x3]\n   add x0, x3, #1\n" DW_ASM_RET
        "5:  mov x0, #-3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_write_name)

        /* dns_wire_copy_name(x0=msg, x1=size, x2=at, x3=into, x4=room, x5=ended_out, x6=ptrs_io) */
        DW_ASM_FUNC(dns_wire_copy_name)
        "cbz x5, 0f\n   str xzr, [x5]\n"
        "0:  cbz x0, 9f\n   mov x7, #255\n   cmp x4, x7\n   csel x7, x7, x4, hi\n"
        "mov x8, #0\n   mov x9, #0\n   mov x10, #0\n"
        "1:  cmp x2, x1\n   b.hs 9f\n   mov x12, x2\n   ldrb w11, [x0, x2]\n   add x2, x2, #1\n"
        "cbz w11, 5f\n   tst w11, #0xc0\n   b.ne 3f\n   sub x13, x1, x2\n   cmp x13, x11\n   b.lo 9f\n"
        "add x14, x11, #1\n   sub x13, x7, x8\n   cmp x13, x14\n   b.lo 9f\n   cbz x3, 2f\n"
        "strb w11, [x3, x8]\n   add x13, x3, x8\n   add x13, x13, #1\n   add x15, x0, x2\n   mov w12, w11\n"
        "8:  ldrb w4, [x15], #1\n   strb w4, [x13], #1\n   subs w12, w12, #1\n   b.ne 8b\n"
        "2:  add x8, x8, x14\n   add x2, x2, x11\n   b 1b\n"
        "3:  and w13, w11, #0xc0\n   cmp w13, #0xc0\n   b.ne 9f\n   cmp x2, x1\n   b.hs 9f\n"
        "ldrb w13, [x0, x2]\n   add x2, x2, #1\n   and w11, w11, #0x3f\n   orr w13, w13, w11, lsl #8\n"
        "cbnz x10, 4f\n   mov x10, x2\n"
        "4:  add x9, x9, #1\n   cmp x13, x12\n   b.hs 9f\n   cmp x9, #127\n   b.hi 9f\n"
        "mov x1, x12\n   mov x2, x13\n   b 1b\n"
        "5:  cmp x8, x7\n   b.hs 9f\n   cbz x3, 6f\n   strb wzr, [x3, x8]\n"
        "6:  add x8, x8, #1\n   cbz x6, 7f\n   ldr x13, [x6]\n   cmp x13, x9\n   b.lo 9f\n"
        "sub x13, x13, x9\n   str x13, [x6]\n"
        "7:  cbnz x10, 10f\n   mov x10, x2\n"
        "10: cbz x5, 11f\n   str x10, [x5]\n"
        "11: mov x0, x8\n" DW_ASM_RET
        "9:  mov x0, #-3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_copy_name)

        /* dns_wire_skip_name(x0=msg, x1=size, x2=at) -> ended offset or -3 */
        DW_ASM_FUNC(dns_wire_skip_name)
        "cbz x0, 9f\n   mov x3, #0\n   mov x4, #0\n   mov x5, #0\n"
        "1:  cmp x2, x1\n   b.hs 9f\n   mov x6, x2\n   ldrb w7, [x0, x2]\n   add x2, x2, #1\n"
        "cbz w7, 5f\n   tst w7, #0xc0\n   b.ne 3f\n   sub x8, x1, x2\n   cmp x8, x7\n   b.lo 9f\n"
        "add x3, x3, x7\n   add x3, x3, #1\n   cmp x3, #255\n   b.hs 9f\n   add x2, x2, x7\n   b 1b\n"
        "3:  and w8, w7, #0xc0\n   cmp w8, #0xc0\n   b.ne 9f\n   cmp x2, x1\n   b.hs 9f\n"
        "ldrb w8, [x0, x2]\n   add x2, x2, #1\n   and w7, w7, #0x3f\n   orr w8, w8, w7, lsl #8\n"
        "cbnz x5, 4f\n   mov x5, x2\n"
        "4:  add x4, x4, #1\n   cmp x8, x6\n   b.hs 9f\n   cmp x4, #127\n   b.hi 9f\n"
        "mov x1, x6\n   mov x2, x8\n   b 1b\n"
        "5:  cmp x3, #255\n   b.hs 9f\n   cbnz x5, 6f\n   mov x5, x2\n"
        "6:  mov x0, x5\n" DW_ASM_RET
        "9:  mov x0, #-3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_skip_name)

        /* dns_wire_reply_identity(x0=reply, x1=size, w2=id, x3=req, x4=qlen, w5=folded) -> 0/1 */
        DW_ASM_FUNC(dns_wire_reply_identity)
        "cbz x0, 8f\n   cbz x3, 8f\n   cmp x1, #12\n   b.lo 8f\n   sub x1, x1, #12\n   cmp x4, x1\n   b.hi 8f\n"
        "ldrb w6, [x0]\n   ldrb w7, [x0, #1]\n   orr w6, w7, w6, lsl #8\n   and w2, w2, #0xffff\n"
        "cmp w6, w2\n   b.ne 8f\n   ldrb w6, [x0, #4]\n   ldrb w7, [x0, #5]\n"
        "cbnz w6, 8f\n   cmp w7, #1\n   b.ne 8f\n   cbz x4, 7f\n   add x0, x0, #12\n   add x3, x3, #12\n   cbnz w5, 3f\n"
        "1:  ldrb w6, [x0], #1\n   ldrb w7, [x3], #1\n   cmp w6, w7\n   b.ne 8f\n   subs x4, x4, #1\n   b.ne 1b\n   b 7f\n"
        "3:  ldrb w6, [x0], #1\n   ldrb w7, [x3], #1\n   eor w8, w6, w7\n   cbz w8, 4f\n"
        "cmp w8, #32\n   b.ne 8f\n   orr w6, w6, #32\n   sub w6, w6, #97\n   cmp w6, #25\n   b.hi 8f\n"
        "4:  subs x4, x4, #1\n   b.ne 3b\n"
        "7:  mov w0, #1\n" DW_ASM_RET
        "8:  mov w0, #0\n" DW_ASM_RET
        DW_ASM_END(dns_wire_reply_identity)

        /* dns_wire_records_end(x0=msg, x1=size, x2=at, x3=count, x4=max_ptrs) -> end or -3 */
        DW_ASM_FUNC(dns_wire_records_end)
        "cbz x3, 8f\n   cbz x0, 9f\n"
        "1:  mov x5, x1\n   mov x6, x2\n   mov x7, #0\n   mov x8, #0\n   mov x9, #0\n"
        "2:  cmp x6, x5\n   b.hs 9f\n   mov x10, x6\n   ldrb w11, [x0, x6]\n   add x6, x6, #1\n"
        "cbz w11, 5f\n   tst w11, #0xc0\n   b.ne 3f\n   sub x12, x5, x6\n   cmp x12, x11\n   b.lo 9f\n"
        "add x7, x7, x11\n   add x7, x7, #1\n   cmp x7, #255\n   b.hs 9f\n   add x6, x6, x11\n   b 2b\n"
        "3:  and w12, w11, #0xc0\n   cmp w12, #0xc0\n   b.ne 9f\n   cmp x6, x5\n   b.hs 9f\n"
        "ldrb w12, [x0, x6]\n   add x6, x6, #1\n   and w11, w11, #0x3f\n   orr w12, w12, w11, lsl #8\n"
        "cbnz x9, 4f\n   mov x9, x6\n"
        "4:  add x8, x8, #1\n   cmp x12, x10\n   b.hs 9f\n   cmp x8, #127\n   b.hi 9f\n"
        "mov x5, x10\n   mov x6, x12\n   b 2b\n"
        "5:  cmp x7, #255\n   b.hs 9f\n   cmp x4, x8\n   b.lo 9f\n   sub x4, x4, x8\n"
        "cbnz x9, 6f\n   mov x9, x6\n"
        "6:  cmp x9, x1\n   b.hi 9f\n   sub x12, x1, x9\n   cmp x12, #10\n   b.lo 9f\n"
        "add x10, x0, x9\n   ldrb w11, [x10, #8]\n   ldrb w12, [x10, #9]\n   orr w11, w12, w11, lsl #8\n"
        "add x9, x9, #10\n   sub x12, x1, x9\n   cmp x12, x11\n   b.lo 9f\n   add x2, x9, x11\n"
        "subs x3, x3, #1\n   b.ne 1b\n"
        "8:  mov x0, x2\n" DW_ASM_RET
        "9:  mov x0, #-3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_records_end)

        /* dns_wire_to_host(x0=wire, x1=len, x2=out, x3=room) -> chars written or 0 */
        DW_ASM_FUNC(dns_wire_to_host)
        "cbz x2, 5f\n   cbz x3, 5f\n   cbnz x0, 0f\n   cbnz x1, 5f\n   strb wzr, [x2]\n   mov x0, #0\n" DW_ASM_RET
        "0:  mov x4, #0\n   mov x5, #0\n"
        "1:  cmp x4, x1\n   b.hs 4f\n   ldrb w6, [x0, x4]\n   add x4, x4, #1\n   cbz w6, 4f\n"
        "cmp w6, #63\n   b.hi 5f\n   sub x7, x1, x4\n   cmp x7, x6\n   b.lo 5f\n   cbz x5, 2f\n"
        "add x7, x5, #1\n   cmp x7, x3\n   b.hs 5f\n   mov w8, #46\n   strb w8, [x2, x5]\n   mov x5, x7\n"
        "2:  sub x7, x3, x5\n   cmp x7, x6\n   b.ls 5f\n   add x8, x0, x4\n   add x9, x2, x5\n   mov w10, w6\n"
        "3:  ldrb w11, [x8], #1\n   strb w11, [x9], #1\n   subs w10, w10, #1\n   b.ne 3b\n"
        "add x4, x4, x6\n   add x5, x5, x6\n   b 1b\n"
        "4:  strb wzr, [x2, x5]\n   mov x0, x5\n" DW_ASM_RET
        "5:  mov x0, #0\n" DW_ASM_RET
        DW_ASM_END(dns_wire_to_host)
);

#elif defined(DW_X64)

#if defined(_WIN32)
# define DW_LOAD3 "mov r10, r8\n   mov r8, rcx\n   mov r9, rdx\n"
# define DW_LOAD4 "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n"
#else
# define DW_LOAD3 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
# define DW_LOAD4 "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11, rcx\n"
#endif

__asm__(
        DW_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* dns_wire_write_name(into, room, name) -> bytes written or -3 */
        DW_ASM_FUNC(dns_wire_write_name)
        DW_LOAD3
        "test r8, r8\n   jz 5f\n   test r10, r10\n   jz 5f\n   test r9, r9\n   jz 5f\n"
        "mov eax, 255\n   cmp r9, rax\n   cmova r9, rax\n   dec r9\n   xor r11d, r11d\n"
        "1:  movzx eax, byte ptr [r10]\n   test eax, eax\n   jz 4f\n   mov rcx, r11\n   lea rdx, [r11 + 1]\n   xor r11d, r11d\n"
        "2:  cmp eax, 46\n   je 6f\n   inc r11d\n   cmp r11d, 63\n   ja 5f\n"
        "cmp rdx, r9\n   jae 5f\n   mov byte ptr [r8 + rdx], al\n   inc rdx\n   inc r10\n"
        "movzx eax, byte ptr [r10]\n   test eax, eax\n   jnz 2b\n   jmp 3f\n"
        "6:  inc r10\n"
        "3:  test r11d, r11d\n   jz 5f\n   mov byte ptr [r8 + rcx], r11b\n   mov r11, rdx\n   jmp 1b\n"
        "4:  mov byte ptr [r8 + r11], 0\n   lea rax, [r11 + 1]\n" DW_ASM_RET
        "5:  mov rax, -3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_write_name)

        /* dns_wire_copy_name(msg, size, at, into, room, ended_out, ptrs_io) */
        DW_ASM_FUNC(dns_wire_copy_name)
#if defined(_WIN32)
        "push rbx\n   push rsi\n   push rdi\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, rcx\n   mov r13, rdx\n   mov rdx, r8\n   mov r14, r9\n"
        "mov r8, [rsp + 96]\n   mov r15, [rsp + 104]\n   mov rbx, [rsp + 112]\n"
#else
        "push rbx\n   push r12\n   push r13\n   push r14\n   push r15\n"
        "mov r12, rdi\n   mov r13, rsi\n   mov r14, rcx\n   mov r15, r9\n   mov rbx, [rsp + 48]\n"
#endif
        "test r15, r15\n   jz 0f\n   mov qword ptr [r15], 0\n"
        "0:  test r12, r12\n   jz 9f\n   mov eax, 255\n   cmp r8, rax\n   cmova r8, rax\n"
        "xor r9d, r9d\n   xor r10d, r10d\n   xor r11d, r11d\n"
        "1:  cmp rdx, r13\n   jae 9f\n   mov rcx, rdx\n   movzx eax, byte ptr [r12 + rdx]\n   inc rdx\n"
        "test eax, eax\n   jz 5f\n   test eax, 0xc0\n   jnz 3f\n   mov rsi, r13\n   sub rsi, rdx\n   cmp rsi, rax\n   jb 9f\n"
        "lea rdi, [rax + 1]\n   mov rsi, r8\n   sub rsi, r9\n   cmp rsi, rdi\n   jb 9f\n   test r14, r14\n   jz 2f\n"
        "mov byte ptr [r14 + r9], al\n   lea rsi, [r12 + rdx]\n   lea rdi, [r14 + r9 + 1]\n   mov rcx, rax\n"
        "rep movsb\n   lea rdi, [rax + 1]\n"
        "2:  add r9, rdi\n   add rdx, rax\n   jmp 1b\n"
        "3:  mov esi, eax\n   and esi, 0xc0\n   cmp esi, 0xc0\n   jne 9f\n   cmp rdx, r13\n   jae 9f\n"
        "movzx esi, byte ptr [r12 + rdx]\n   inc rdx\n   and eax, 0x3f\n   shl eax, 8\n   or esi, eax\n"
        "test r11, r11\n   jnz 4f\n   mov r11, rdx\n"
        "4:  inc r10\n   cmp rsi, rcx\n   jae 9f\n   cmp r10, 127\n   ja 9f\n"
        "mov r13, rcx\n   mov rdx, rsi\n   jmp 1b\n"
        "5:  cmp r9, r8\n   jae 9f\n   test r14, r14\n   jz 6f\n   mov byte ptr [r14 + r9], 0\n"
        "6:  inc r9\n   test rbx, rbx\n   jz 7f\n   mov rax, [rbx]\n   cmp rax, r10\n   jb 9f\n"
        "sub rax, r10\n   mov [rbx], rax\n"
        "7:  test r11, r11\n   jnz 10f\n   mov r11, rdx\n"
        "10: test r15, r15\n   jz 11f\n   mov [r15], r11\n"
        "11: mov rax, r9\n"
#if defined(_WIN32)
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbx\n" DW_ASM_RET
        "9:  mov rax, -3\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbx\n" DW_ASM_RET
#else
        "pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" DW_ASM_RET
        "9:  mov rax, -3\n   pop r15\n   pop r14\n   pop r13\n   pop r12\n   pop rbx\n" DW_ASM_RET
#endif
        DW_ASM_END(dns_wire_copy_name)

        /* dns_wire_skip_name(msg, size, at) -> ended offset or -3 */
        DW_ASM_FUNC(dns_wire_skip_name)
        DW_LOAD3
        "test r8, r8\n   jz 9f\n   xor r11d, r11d\n   xor ecx, ecx\n   xor edx, edx\n"
        "1:  cmp r10, r9\n   jae 9f\n   mov rax, r10\n   movzx r10d, byte ptr [r8 + rax]\n   xchg rax, r10\n   inc r10\n"
        "test eax, eax\n   jz 5f\n   test eax, 0xc0\n   jnz 3f\n   push rax\n   mov rax, r9\n   sub rax, r10\n"
        "cmp rax, [rsp]\n   pop rax\n   jb 9f\n   lea r11, [r11 + rax + 1]\n   cmp r11, 255\n   jae 9f\n   add r10, rax\n   jmp 1b\n"
        "3:  test al, 0xc0\n   jpo 9f\n   cmp r10, r9\n   jae 9f\n   lea r9, [r10 - 1]\n"
        "and eax, 0x3f\n   shl eax, 8\n   movzx r10d, byte ptr [r8 + r10]\n   or eax, r10d\n"
        "test rdx, rdx\n   jnz 4f\n   lea rdx, [r9 + 2]\n"
        "4:  inc rcx\n   cmp rax, r9\n   jae 9f\n   cmp rcx, 127\n   ja 9f\n   mov r10, rax\n   jmp 1b\n"
        "5:  cmp r11, 255\n   jae 9f\n   test rdx, rdx\n   cmovz rdx, r10\n   mov rax, rdx\n" DW_ASM_RET
        "9:  mov rax, -3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_skip_name)

        /* dns_wire_reply_identity(reply, size, id, req, qlen, folded) -> 0/1 */
        DW_ASM_FUNC(dns_wire_reply_identity)
#if defined(_WIN32)
        "mov r10, [rsp + 40]\n   mov r11d, dword ptr [rsp + 48]\n"
        "test rcx, rcx\n   jz 8f\n   test r9, r9\n   jz 8f\n   cmp rdx, 12\n   jb 8f\n"
        "sub rdx, 12\n   cmp r10, rdx\n   ja 8f\n   movzx eax, word ptr [rcx]\n   rol ax, 8\n"
        "cmp ax, r8w\n   jne 8f\n   cmp word ptr [rcx + 4], 0x0100\n   jne 8f\n"
        "add rcx, 12\n   add r9, 12\n   test r11d, r11d\n   jnz 3f\n"
        "1:  test r10, r10\n   jz 7f\n   movzx eax, byte ptr [rcx]\n   cmp al, byte ptr [r9]\n   jne 8f\n"
        "inc rcx\n   inc r9\n   dec r10\n   jmp 1b\n"
        "3:  test r10, r10\n   jz 7f\n   movzx eax, byte ptr [rcx]\n   movzx edx, byte ptr [r9]\n"
        "cmp eax, edx\n   je 4f\n   xor edx, eax\n   cmp edx, 32\n   jne 8f\n"
        "or eax, 32\n   sub eax, 97\n   cmp eax, 25\n   ja 8f\n"
        "4:  inc rcx\n   inc r9\n   dec r10\n   jmp 3b\n"
        "7:  mov eax, 1\n" DW_ASM_RET
        "8:  xor eax, eax\n" DW_ASM_RET
#else
        "test rdi, rdi\n   jz 8f\n   test rcx, rcx\n   jz 8f\n   cmp rsi, 12\n   jb 8f\n"
        "sub rsi, 12\n   cmp r8, rsi\n   ja 8f\n   movzx eax, word ptr [rdi]\n   rol ax, 8\n"
        "cmp ax, dx\n   jne 8f\n   cmp word ptr [rdi + 4], 0x0100\n   jne 8f\n"
        "add rdi, 12\n   add rcx, 12\n   test r9d, r9d\n   jnz 3f\n"
        "1:  test r8, r8\n   jz 7f\n   movzx eax, byte ptr [rdi]\n   cmp al, byte ptr [rcx]\n   jne 8f\n"
        "inc rdi\n   inc rcx\n   dec r8\n   jmp 1b\n"
        "3:  test r8, r8\n   jz 7f\n   movzx eax, byte ptr [rdi]\n   movzx edx, byte ptr [rcx]\n"
        "cmp eax, edx\n   je 4f\n   xor edx, eax\n   cmp edx, 32\n   jne 8f\n"
        "or eax, 32\n   sub eax, 97\n   cmp eax, 25\n   ja 8f\n"
        "4:  inc rdi\n   inc rcx\n   dec r8\n   jmp 3b\n"
        "7:  mov eax, 1\n" DW_ASM_RET
        "8:  xor eax, eax\n" DW_ASM_RET
#endif
        DW_ASM_END(dns_wire_reply_identity)

        /* dns_wire_records_end(msg, size, at, count, max_ptrs) -> end or -3 */
        DW_ASM_FUNC(dns_wire_records_end)
#if defined(_WIN32)
        "push rbx\n   push rsi\n   push rdi\n   push r12\n   push r13\n"
        "mov r12, rcx\n   mov r13, rdx\n   mov rbx, r8\n   mov rcx, r9\n   mov r8, [rsp + 80]\n"
#else
        "push rbx\n   push r12\n   push r13\n"
        "mov r12, rdi\n   mov r13, rsi\n   mov rbx, rdx\n"
#endif
        "test rcx, rcx\n   jz 8f\n   test r12, r12\n   jz 9f\n"
        "1:  mov r9, r13\n   mov rdx, rbx\n   xor r10d, r10d\n   xor r11d, r11d\n   xor esi, esi\n"
        "2:  cmp rdx, r9\n   jae 9f\n   mov rdi, rdx\n   movzx eax, byte ptr [r12 + rdx]\n   inc rdx\n"
        "test eax, eax\n   jz 5f\n   test eax, 0xc0\n   jnz 3f\n   mov rax, rax\n"
        "push rcx\n   mov rcx, r9\n   sub rcx, rdx\n   cmp rcx, rax\n   pop rcx\n   jb 9f\n"
        "lea r10, [r10 + rax + 1]\n   cmp r10, 255\n   jae 9f\n   add rdx, rax\n   jmp 2b\n"
        "3:  test al, 0xc0\n   jpo 9f\n   cmp rdx, r9\n   jae 9f\n"
        "and eax, 0x3f\n   shl eax, 8\n   movzx edx, byte ptr [r12 + rdx]\n   or eax, edx\n"
        "test rsi, rsi\n   jnz 4f\n   lea rsi, [rdi + 2]\n"
        "4:  inc r11\n   cmp rax, rdi\n   jae 9f\n   cmp r11, 127\n   ja 9f\n   mov r9, rdi\n   mov rdx, rax\n   jmp 2b\n"
        "5:  cmp r10, 255\n   jae 9f\n   cmp r8, r11\n   jb 9f\n   sub r8, r11\n   test rsi, rsi\n   cmovz rsi, rdx\n"
        "cmp rsi, r13\n   ja 9f\n   mov rax, r13\n   sub rax, rsi\n   cmp rax, 10\n   jb 9f\n"
        "movzx eax, word ptr [r12 + rsi + 8]\n   rol ax, 8\n   add rsi, 10\n   mov rdx, r13\n   sub rdx, rsi\n"
        "cmp rdx, rax\n   jb 9f\n   lea rbx, [rsi + rax]\n   dec rcx\n   jnz 1b\n"
        "8:  mov rax, rbx\n"
#if defined(_WIN32)
        "pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbx\n" DW_ASM_RET
        "9:  mov rax, -3\n   pop r13\n   pop r12\n   pop rdi\n   pop rsi\n   pop rbx\n" DW_ASM_RET
#else
        "pop r13\n   pop r12\n   pop rbx\n" DW_ASM_RET
        "9:  mov rax, -3\n   pop r13\n   pop r12\n   pop rbx\n" DW_ASM_RET
#endif
        DW_ASM_END(dns_wire_records_end)

        /* dns_wire_to_host(wire, len, out, room) -> chars written or 0 */
        DW_ASM_FUNC(dns_wire_to_host)
        DW_LOAD4
        "push rbx\n   test r10, r10\n   jz 5f\n   test r11, r11\n   jz 5f\n   test r8, r8\n   jnz 0f\n"
        "test r9, r9\n   jnz 5f\n   mov byte ptr [r10], 0\n   xor eax, eax\n   pop rbx\n" DW_ASM_RET
        "0:  xor ecx, ecx\n   xor edx, edx\n"
        "1:  cmp rcx, r9\n   jae 4f\n   movzx eax, byte ptr [r8 + rcx]\n   inc rcx\n   test eax, eax\n   jz 4f\n"
        "cmp eax, 63\n   ja 5f\n   mov rbx, r9\n   sub rbx, rcx\n   cmp rbx, rax\n   jb 5f\n"
        "test rdx, rdx\n   jz 2f\n   lea rbx, [rdx + 1]\n   cmp rbx, r11\n   jae 5f\n"
        "mov byte ptr [r10 + rdx], 46\n   mov rdx, rbx\n"
        "2:  mov rbx, r11\n   sub rbx, rdx\n   cmp rbx, rax\n   jbe 5f\n"
        "3:  mov bl, byte ptr [r8 + rcx]\n   mov byte ptr [r10 + rdx], bl\n   inc rcx\n   inc rdx\n"
        "dec eax\n   jnz 3b\n   jmp 1b\n"
        "4:  mov byte ptr [r10 + rdx], 0\n   mov rax, rdx\n   pop rbx\n" DW_ASM_RET
        "5:  xor eax, eax\n   pop rbx\n" DW_ASM_RET
        DW_ASM_END(dns_wire_to_host)

        ".att_syntax prefix\n"
);

#elif defined(DW_RISCV64)

__asm__(
        DW_ASM_SECTION

        /* dns_wire_write_name(a0=into, a1=room, a2=name) -> bytes written or -3 */
        DW_ASM_FUNC(dns_wire_write_name)
        "beqz a0, 5f\n   beqz a2, 5f\n   beqz a1, 5f\n   li t0, 255\n   bleu a1, t0, 0f\n   mv a1, t0\n"
        "0:  addi a1, a1, -1\n   li a3, 0\n   li t1, 46\n   li t2, 63\n"
        "1:  lbu t0, 0(a2)\n   beqz t0, 4f\n   mv a4, a3\n   addi a5, a3, 1\n   li a6, 0\n"
        "2:  beq t0, t1, 6f\n   addi a6, a6, 1\n   bgtu a6, t2, 5f\n   bgeu a5, a1, 5f\n"
        "add t3, a0, a5\n   sb t0, 0(t3)\n   addi a5, a5, 1\n   addi a2, a2, 1\n"
        "lbu t0, 0(a2)\n   bnez t0, 2b\n   j 3f\n"
        "6:  addi a2, a2, 1\n"
        "3:  beqz a6, 5f\n   add t3, a0, a4\n   sb a6, 0(t3)\n   mv a3, a5\n   j 1b\n"
        "4:  add t3, a0, a3\n   sb zero, 0(t3)\n   addi a0, a3, 1\n" DW_ASM_RET
        "5:  li a0, -3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_write_name)

        /* dns_wire_copy_name(a0=msg, a1=size, a2=at, a3=into, a4=room, a5=ended_out, a6=ptrs_io) */
        DW_ASM_FUNC(dns_wire_copy_name)
        "beqz a5, 0f\n   sd zero, 0(a5)\n"
        "0:  beqz a0, 9f\n   li t0, 255\n   bleu a4, t0, 10f\n   mv a4, t0\n"
        "10: li a7, 0\n   li t0, 0\n   li t1, 0\n"
        "1:  bgeu a2, a1, 9f\n   mv t2, a2\n   add t3, a0, a2\n   lbu t4, 0(t3)\n   addi a2, a2, 1\n"
        "beqz t4, 5f\n   andi t5, t4, 0xc0\n   bnez t5, 3f\n   sub t6, a1, a2\n   bltu t6, t4, 9f\n"
        "addi t5, t4, 1\n   sub t6, a4, a7\n   bltu t6, t5, 9f\n   beqz a3, 2f\n"
        "add t6, a3, a7\n   sb t4, 0(t6)\n   addi t6, t6, 1\n   add t3, a0, a2\n   mv t2, t4\n"
        "8:  lbu t5, 0(t3)\n   sb t5, 0(t6)\n   addi t3, t3, 1\n   addi t6, t6, 1\n   addi t2, t2, -1\n   bnez t2, 8b\n"
        "addi t5, t4, 1\n"
        "2:  add a7, a7, t5\n   add a2, a2, t4\n   j 1b\n"
        "3:  li t6, 0xc0\n   bne t5, t6, 9f\n   bgeu a2, a1, 9f\n   add t3, a0, a2\n   lbu t5, 0(t3)\n"
        "addi a2, a2, 1\n   andi t4, t4, 0x3f\n   slli t4, t4, 8\n   or t5, t5, t4\n"
        "bnez t1, 4f\n   mv t1, a2\n"
        "4:  addi t0, t0, 1\n   bgeu t5, t2, 9f\n   li t6, 127\n   bgtu t0, t6, 9f\n"
        "mv a1, t2\n   mv a2, t5\n   j 1b\n"
        "5:  bgeu a7, a4, 9f\n   beqz a3, 6f\n   add t6, a3, a7\n   sb zero, 0(t6)\n"
        "6:  addi a7, a7, 1\n   beqz a6, 7f\n   ld t6, 0(a6)\n   bltu t6, t0, 9f\n"
        "sub t6, t6, t0\n   sd t6, 0(a6)\n"
        "7:  bnez t1, 11f\n   mv t1, a2\n"
        "11: beqz a5, 12f\n   sd t1, 0(a5)\n"
        "12: mv a0, a7\n" DW_ASM_RET
        "9:  li a0, -3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_copy_name)

        /* dns_wire_skip_name(a0=msg, a1=size, a2=at) -> ended offset or -3 */
        DW_ASM_FUNC(dns_wire_skip_name)
        "beqz a0, 9f\n   li a3, 0\n   li a4, 0\n   li a5, 0\n   li t6, 255\n"
        "1:  bgeu a2, a1, 9f\n   mv t0, a2\n   add t1, a0, a2\n   lbu t2, 0(t1)\n   addi a2, a2, 1\n"
        "beqz t2, 5f\n   andi t3, t2, 0xc0\n   bnez t3, 3f\n   sub t4, a1, a2\n   bltu t4, t2, 9f\n"
        "add a3, a3, t2\n   addi a3, a3, 1\n   bgeu a3, t6, 9f\n   add a2, a2, t2\n   j 1b\n"
        "3:  li t4, 0xc0\n   bne t3, t4, 9f\n   bgeu a2, a1, 9f\n   add t1, a0, a2\n   lbu t3, 0(t1)\n"
        "addi a2, a2, 1\n   andi t2, t2, 0x3f\n   slli t2, t2, 8\n   or t3, t3, t2\n"
        "bnez a5, 4f\n   mv a5, a2\n"
        "4:  addi a4, a4, 1\n   bgeu t3, t0, 9f\n   li t4, 127\n   bgtu a4, t4, 9f\n"
        "mv a1, t0\n   mv a2, t3\n   j 1b\n"
        "5:  bgeu a3, t6, 9f\n   bnez a5, 6f\n   mv a5, a2\n"
        "6:  mv a0, a5\n" DW_ASM_RET
        "9:  li a0, -3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_skip_name)

        /* dns_wire_reply_identity(a0=reply, a1=size, a2=id, a3=req, a4=qlen, a5=folded) -> 0/1 */
        DW_ASM_FUNC(dns_wire_reply_identity)
        "beqz a0, 8f\n   beqz a3, 8f\n   li t0, 12\n   bltu a1, t0, 8f\n   sub a1, a1, t0\n   bgtu a4, a1, 8f\n"
        "lbu t1, 0(a0)\n   lbu t2, 1(a0)\n   slli t1, t1, 8\n   or t1, t1, t2\n"
        "slli a2, a2, 48\n   srli a2, a2, 48\n   bne t1, a2, 8f\n"
        "lbu t1, 4(a0)\n   bnez t1, 8f\n   lbu t2, 5(a0)\n   li t1, 1\n   bne t2, t1, 8f\n"
        "addi a0, a0, 12\n   addi a3, a3, 12\n   slli a5, a5, 32\n   srli a5, a5, 32\n   bnez a5, 3f\n"
        "1:  beqz a4, 7f\n   lbu t1, 0(a0)\n   lbu t2, 0(a3)\n   bne t1, t2, 8f\n"
        "addi a0, a0, 1\n   addi a3, a3, 1\n   addi a4, a4, -1\n   j 1b\n"
        "3:  li t4, 32\n   li t5, 25\n"
        "2:  beqz a4, 7f\n   lbu t1, 0(a0)\n   lbu t2, 0(a3)\n   beq t1, t2, 4f\n"
        "xor t3, t1, t2\n   bne t3, t4, 8f\n   ori t1, t1, 32\n   addi t1, t1, -97\n   bgtu t1, t5, 8f\n"
        "4:  addi a0, a0, 1\n   addi a3, a3, 1\n   addi a4, a4, -1\n   j 2b\n"
        "7:  li a0, 1\n" DW_ASM_RET
        "8:  li a0, 0\n" DW_ASM_RET
        DW_ASM_END(dns_wire_reply_identity)

        /* dns_wire_records_end(a0=msg, a1=size, a2=at, a3=count, a4=max_ptrs) -> end or -3 */
        DW_ASM_FUNC(dns_wire_records_end)
        "beqz a3, 8f\n   beqz a0, 9f\n   li t6, 255\n"
        "1:  mv a5, a1\n   mv a6, a2\n   li a7, 0\n   li t0, 0\n   li t1, 0\n"
        "2:  bgeu a6, a5, 9f\n   mv t2, a6\n   add t3, a0, a6\n   lbu t4, 0(t3)\n   addi a6, a6, 1\n"
        "beqz t4, 5f\n   andi t5, t4, 0xc0\n   bnez t5, 3f\n   sub t3, a5, a6\n   bltu t3, t4, 9f\n"
        "add a7, a7, t4\n   addi a7, a7, 1\n   bgeu a7, t6, 9f\n   add a6, a6, t4\n   j 2b\n"
        "3:  li t3, 0xc0\n   bne t5, t3, 9f\n   bgeu a6, a5, 9f\n   add t3, a0, a6\n   lbu t5, 0(t3)\n"
        "addi a6, a6, 1\n   andi t4, t4, 0x3f\n   slli t4, t4, 8\n   or t5, t5, t4\n"
        "bnez t1, 4f\n   mv t1, a6\n"
        "4:  addi t0, t0, 1\n   bgeu t5, t2, 9f\n   li t3, 127\n   bgtu t0, t3, 9f\n"
        "mv a5, t2\n   mv a6, t5\n   j 2b\n"
        "5:  bgeu a7, t6, 9f\n   bltu a4, t0, 9f\n   sub a4, a4, t0\n   bnez t1, 6f\n   mv t1, a6\n"
        "6:  bgtu t1, a1, 9f\n   sub t3, a1, t1\n   li t4, 10\n   bltu t3, t4, 9f\n"
        "add t3, a0, t1\n   lbu t4, 8(t3)\n   lbu t5, 9(t3)\n   slli t4, t4, 8\n   or t4, t4, t5\n"
        "addi t1, t1, 10\n   sub t3, a1, t1\n   bltu t3, t4, 9f\n   add a2, t1, t4\n"
        "addi a3, a3, -1\n   bnez a3, 1b\n"
        "8:  mv a0, a2\n" DW_ASM_RET
        "9:  li a0, -3\n" DW_ASM_RET
        DW_ASM_END(dns_wire_records_end)

        /* dns_wire_to_host(a0=wire, a1=len, a2=out, a3=room) -> chars written or 0 */
        DW_ASM_FUNC(dns_wire_to_host)
        "beqz a2, 5f\n   beqz a3, 5f\n   bnez a0, 0f\n   bnez a1, 5f\n   sb zero, 0(a2)\n   li a0, 0\n" DW_ASM_RET
        "0:  li a4, 0\n   li a5, 0\n   li t4, 63\n   li t5, 46\n"
        "1:  bgeu a4, a1, 4f\n   add t0, a0, a4\n   lbu t1, 0(t0)\n   addi a4, a4, 1\n   beqz t1, 4f\n"
        "bgtu t1, t4, 5f\n   sub t2, a1, a4\n   bltu t2, t1, 5f\n   beqz a5, 2f\n"
        "addi t2, a5, 1\n   bgeu t2, a3, 5f\n   add t3, a2, a5\n   sb t5, 0(t3)\n   mv a5, t2\n"
        "2:  sub t2, a3, a5\n   bleu t2, t1, 5f\n   add t0, a0, a4\n   add t3, a2, a5\n   mv t6, t1\n"
        "3:  lbu t2, 0(t0)\n   sb t2, 0(t3)\n   addi t0, t0, 1\n   addi t3, t3, 1\n   addi t6, t6, -1\n   bnez t6, 3b\n"
        "add a4, a4, t1\n   add a5, a5, t1\n   j 1b\n"
        "4:  add t3, a2, a5\n   sb zero, 0(t3)\n   mv a0, a5\n" DW_ASM_RET
        "5:  li a0, 0\n" DW_ASM_RET
        DW_ASM_END(dns_wire_to_host)
);

#endif

#endif /* NET_DNS_WIRE_IMPLEMENTATION */
