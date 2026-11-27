/*
        Acoustic modem RX framing & sync primitives in pure assembly:
          - modem_sync_unpack     : stride-packed FSK symbol stream -> LSB-first bit bytes
          - modem_sync_find       : 32-bit sliding sync-word scanner (8-bit SWAR pack + ccmp/unrolled)
          - modem_sync_read_bytes : bounds-checked LSB-first bit-byte -> packed byte reader
          - modem_sync_memo_has   : strictly constant-time authenticated frame memo lookup

        Pure arm64, x86-64 (SysV ELF / Mach-O / Windows x64 COFF), and riscv64
        assembly. Zero C function bodies, zero allocations, zero syscalls,
        zero .data/.rodata lookup tables, integer-only.

        STB-style: include for declarations; define WATERLINK_MODEM_SYNC_ASM_IMPLEMENTATION
        in one translation unit to emit the assembly routines.
*/
#ifndef MOONWATER_WATERLINK_MODEM_SYNC_ASM_H
#define MOONWATER_WATERLINK_MODEM_SYNC_ASM_H

#ifdef STANDARD_MODERN_C
typedef p8  ms_u8;
typedef p16 ms_u16;
typedef p32 ms_u32;
typedef b32 ms_i32;
typedef p64 ms_u64;
typedef b64 ms_i64;
#elif defined(__UINT8_TYPE__) && defined(__UINT16_TYPE__) && defined(__UINT32_TYPE__) && \
      defined(__INT32_TYPE__) && defined(__UINT64_TYPE__) && defined(__INT64_TYPE__)
typedef __UINT8_TYPE__  ms_u8;
typedef __UINT16_TYPE__ ms_u16;
typedef __UINT32_TYPE__ ms_u32;
typedef __INT32_TYPE__  ms_i32;
typedef __UINT64_TYPE__ ms_u64;
typedef __INT64_TYPE__  ms_i64;
#else
#include <stdint.h>
typedef uint8_t  ms_u8;
typedef uint16_t ms_u16;
typedef uint32_t ms_u32;
typedef int32_t  ms_i32;
typedef uint64_t ms_u64;
typedef int64_t  ms_i64;
#endif

ms_u64 modem_sync_unpack(const ms_u8 *best, ms_u64 n, ms_u64 off, ms_u64 sym, ms_u32 sym_bits, ms_u8 *bits);
ms_i64 modem_sync_find(const ms_u8 *bits, ms_u64 nb, ms_u64 start_bit, ms_u32 sync_word);
ms_i32 modem_sync_read_bytes(const ms_u8 *bits, ms_u64 nb, ms_u64 p, ms_u8 *dst, ms_u64 count);
ms_i32 modem_sync_memo_has(const ms_u8 *frames, const ms_u64 *totals, ms_u32 count, ms_u64 stride,
                           const ms_u8 *bytes, ms_u64 total);

#endif /* MOONWATER_WATERLINK_MODEM_SYNC_ASM_H */

#ifdef WATERLINK_MODEM_SYNC_ASM_IMPLEMENTATION

#if !defined(ARM64) && !defined(X64) && !defined(RISCV64)
# if defined(__aarch64__) || defined(_M_ARM64)
#  define MS_ARM64 1
# elif defined(__x86_64__) || defined(_M_X64)
#  define MS_X64 1
# elif defined(__riscv) && (__riscv_xlen == 64)
#  define MS_RISCV64 1
# else
#  error "src/waterlink/modem_sync.asm.c: requires arm64, x86-64, or riscv64"
# endif
#else
# if defined(ARM64) && ARM64
#  define MS_ARM64 1
# elif defined(X64) && X64
#  define MS_X64 1
# elif defined(RISCV64) && RISCV64
#  define MS_RISCV64 1
# endif
#endif

#if defined(ASM_FUNC) && defined(ASM_END) && defined(ASM_SECTION) && defined(ASM_RET)
# define MS_ASM_SECTION   ASM_SECTION
# define MS_ASM_FUNC(n)   ASM_FUNC(n)
# define MS_ASM_END(n)    ASM_END(n)
# define MS_ASM_RET       ASM_RET
#else
# define MS_ASM_SECTION   ".text\n"
# define MS_ASM_RET       "ret\n"
# if defined(__APPLE__)
#  define MS_ASM_FUNC(n)  ".text\n.balign 16\n.globl _" #n "\n_" #n ":\n"
#  define MS_ASM_END(n)   ""
# elif defined(_WIN32)
#  define MS_ASM_FUNC(n)  ".section .text,\"xr\",discard," #n "\n.globl " #n "\n.def " #n "\n.scl 2\n.type 32\n.endef\n.balign 16\n" #n ":\n"
#  define MS_ASM_END(n)   ""
# else
#  if defined(MS_ARM64)
#   define MS_ASM_FTYPE   "%function"
#  else
#   define MS_ASM_FTYPE   "@function"
#  endif
#  define MS_ASM_FUNC(n)  ".pushsection .text." #n ", \"ax\", %progbits\n.balign 16\n.globl " #n "\n.type " #n ", " MS_ASM_FTYPE "\n" #n ":\n"
#  define MS_ASM_END(n)   ".size " #n ", .-" #n "\n.popsection\n"
# endif
#endif

#if defined(MS_ARM64)

__asm__(
        MS_ASM_SECTION

        /* modem_sync_read_bytes(x0=bits, x1=nb, x2=p, x3=dst, x4=count) -> w0 (0/1) */
        MS_ASM_FUNC(modem_sync_read_bytes)
        "cbz x0, 3f\n   cbz x3, 3f\n   cmp x2, x1\n   b.hi 3f\n"
        "sub x5, x1, x2\n   lsr x5, x5, #3\n   cmp x4, x5\n   b.hi 3f\n   cbz x4, 2f\n   add x5, x0, x2\n"
        "movz x6, #0x4080\n   movk x6, #0x1020, lsl #16\n   movk x6, #0x0408, lsl #32\n   movk x6, #0x0102, lsl #48\n"
        "1:  ldr x7, [x5], #8\n   and x7, x7, #0x0101010101010101\n   mul x7, x7, x6\n   lsr x7, x7, #56\n"
        "strb w7, [x3], #1\n   subs x4, x4, #1\n   b.ne 1b\n"
        "2:  mov w0, #1\n" MS_ASM_RET
        "3:  mov w0, #0\n" MS_ASM_RET
        MS_ASM_END(modem_sync_read_bytes)

        /* modem_sync_unpack(x0=best, x1=n, x2=off, x3=sym, w4=sym_bits, x5=bits) -> x0 (nb) */
        MS_ASM_FUNC(modem_sync_unpack)
        "cbz x0, 9f\n   cbz x5, 9f\n   cbz x3, 9f\n   cbz w4, 9f\n   cmp w4, #8\n   b.hi 9f\n"
        "cmp x2, x1\n   b.hi 9f\n   sub x1, x1, x2\n   cmp x1, x3\n   b.lo 9f\n   add x6, x0, x2\n   mov x7, x5\n"
        "cmp w4, #4\n   b.eq 1f\n"
        "5:  ldrb w8, [x6]\n   add x6, x6, x3\n   sub x1, x1, x3\n   mov w9, w4\n"
        "6:  and w10, w8, #1\n   strb w10, [x5], #1\n   lsr w8, w8, #1\n   subs w9, w9, #1\n   b.ne 6b\n"
        "cmp x1, x3\n   b.hs 5b\n   b 8f\n"
        "1:  movz w10, #0x4081\n   movk w10, #0x0020, lsl #16\n"
        "2:  ldrb w8, [x6]\n   add x6, x6, x3\n   sub x1, x1, x3\n   and w8, w8, #15\n"
        "mul w8, w8, w10\n   and w8, w8, #0x01010101\n   str w8, [x5], #4\n   cmp x1, x3\n   b.hs 2b\n"
        "8:  sub x0, x5, x7\n" MS_ASM_RET
        "9:  mov x0, #0\n" MS_ASM_RET
        MS_ASM_END(modem_sync_unpack)

        /* modem_sync_find(x0=bits, x1=nb, x2=start_bit, w3=sync_word) -> x0 (bit idx or -1) */
        MS_ASM_FUNC(modem_sync_find)
        "cbz x0, 9f\n   cmp x1, #32\n   b.lo 9f\n   sub x1, x1, #32\n   cmp x2, x1\n   b.hi 9f\n"
        "sub x4, x1, x2\n   add x5, x0, x2\n"
        "movz x6, #0x4080\n   movk x6, #0x1020, lsl #16\n   movk x6, #0x0408, lsl #32\n   movk x6, #0x0102, lsl #48\n"
        "mov x8, #0\n   mov w9, #0\n"
        "1:  ldr x10, [x5], #8\n   and x10, x10, #0x0101010101010101\n   mul x10, x10, x6\n   lsr x10, x10, #56\n"
        "lsl x10, x10, x9\n   orr x8, x8, x10\n   add w9, w9, #8\n   cmp w9, #32\n   b.lo 1b\n"
        "cmp w8, w3\n   b.eq 8f\n"
        "2:  cmp x4, #8\n   b.lo 6f\n   ldr x10, [x5], #8\n   and x10, x10, #0x0101010101010101\n"
        "mul x10, x10, x6\n   lsr x10, x10, #56\n   orr x8, x8, x10, lsl #32\n   sub x4, x4, #8\n"
        "lsr x11, x8, #1\n   cmp w11, w3\n   lsr x12, x8, #2\n   ccmp w12, w3, #4, ne\n"
        "lsr x13, x8, #3\n   ccmp w13, w3, #4, ne\n   lsr x14, x8, #4\n   ccmp w14, w3, #4, ne\n"
        "lsr x15, x8, #5\n   ccmp w15, w3, #4, ne\n   lsr x16, x8, #6\n   ccmp w16, w3, #4, ne\n"
        "lsr x17, x8, #7\n   ccmp w17, w3, #4, ne\n   lsr x10, x8, #8\n   ccmp w10, w3, #4, ne\n"
        "b.eq 5f\n   mov x8, x10\n   add x2, x2, #8\n   b 2b\n"
        "5:  lsr x8, x8, #1\n   add x2, x2, #1\n   cmp w8, w3\n   b.ne 5b\n   b 8f\n"
        "6:  cbz x4, 9f\n"
        "7:  ldrb w10, [x5], #1\n   and w10, w10, #1\n   lsr w8, w8, #1\n   orr w8, w8, w10, lsl #31\n"
        "add x2, x2, #1\n   cmp w8, w3\n   b.eq 8f\n   subs x4, x4, #1\n   b.ne 7b\n   b 9f\n"
        "8:  mov x0, x2\n" MS_ASM_RET
        "9:  mov x0, #-1\n" MS_ASM_RET
        MS_ASM_END(modem_sync_find)

        /* modem_sync_memo_has(x0=frames, x1=totals, w2=count, x3=stride, x4=bytes, x5=total) -> w0 (0/1, CT) */
        MS_ASM_FUNC(modem_sync_memo_has)
        "cbz x0, 9f\n   cbz x1, 9f\n   cbz x4, 9f\n   cbz w2, 9f\n   cmp x5, x3\n   b.hi 9f\n   mov w6, #0\n"
        "1:  ldr x7, [x1], #8\n   eor x8, x7, x5\n   mov x9, x0\n   add x0, x0, x3\n   mov x10, x4\n   mov x11, x5\n"
        "2:  subs x11, x11, #8\n   b.lo 3f\n   ldr x12, [x9], #8\n   ldr x13, [x10], #8\n"
        "eor x12, x12, x13\n   orr x8, x8, x12\n   b 2b\n"
        "3:  adds x11, x11, #8\n   b.eq 4f\n"
        "5:  ldrb w12, [x9], #1\n   ldrb w13, [x10], #1\n   eor w12, w12, w13\n   orr x8, x8, x12\n   subs x11, x11, #1\n   b.ne 5b\n"
        "4:  cmp x8, #0\n   cset w12, eq\n   orr w6, w6, w12\n   subs w2, w2, #1\n   b.ne 1b\n"
        "mov w0, w6\n" MS_ASM_RET
        "9:  mov w0, #0\n" MS_ASM_RET
        MS_ASM_END(modem_sync_memo_has)
);

#elif defined(MS_X64)

#if defined(_WIN32)
#define MS_RB_LOAD  "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov rcx, [rsp + 48]\n"
#define MS_UP_LOAD  "mov r10, r8\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov ecx, dword ptr [rsp + 56]\n   mov r12, [rsp + 64]\n"
#define MS_FD_LOAD  "mov r10, r8\n   mov r11d, r9d\n   mov r8, rcx\n   mov r9, rdx\n"
#define MS_MM_PRO   "push rbx\n   push rdi\n   push rsi\n   push r12\n   mov r10d, r8d\n   mov r11, r9\n   mov r8, rcx\n   mov r9, rdx\n   mov r12, [rsp + 72]\n   mov rsi, [rsp + 80]\n"
#define MS_MM_EPI   "pop r12\n   pop rsi\n   pop rdi\n   pop rbx\n"
#else
#define MS_RB_LOAD  "mov r11, rcx\n   mov rcx, r8\n   mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
#define MS_UP_LOAD  "mov r11, rcx\n   mov ecx, r8d\n   mov r12, r9\n   mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n"
#define MS_FD_LOAD  "mov r8, rdi\n   mov r9, rsi\n   mov r10, rdx\n   mov r11d, ecx\n"
#define MS_MM_PRO   "push rbx\n   push r12\n   mov r12, r8\n   mov r8, rdi\n   mov r10d, edx\n   mov r11, rcx\n   mov rdx, rsi\n   mov rsi, r9\n   mov r9, rdx\n"
#define MS_MM_EPI   "pop r12\n   pop rbx\n"
#endif

__asm__(
        MS_ASM_SECTION
        ".intel_syntax noprefix\n"

        /* modem_sync_read_bytes(bits, nb, p, dst, count) -> eax (0/1) */
        MS_ASM_FUNC(modem_sync_read_bytes)
        "push rbx\n" MS_RB_LOAD
        "test r8, r8\n   jz 3f\n   test r11, r11\n   jz 3f\n   cmp r10, r9\n   ja 3f\n"
        "sub r9, r10\n   shr r9, 3\n   cmp rcx, r9\n   ja 3f\n   test rcx, rcx\n   jz 2f\n   add r8, r10\n"
        "mov rdx, 0x0101010101010101\n   mov rbx, 0x0102040810204080\n"
        "1:  mov rax, [r8]\n   add r8, 8\n   and rax, rdx\n   imul rax, rbx\n   shr rax, 56\n"
        "mov byte ptr [r11], al\n   inc r11\n   dec rcx\n   jnz 1b\n"
        "2:  mov eax, 1\n   pop rbx\n" MS_ASM_RET
        "3:  xor eax, eax\n   pop rbx\n" MS_ASM_RET
        MS_ASM_END(modem_sync_read_bytes)

        /* modem_sync_unpack(best, n, off, sym, sym_bits, bits) -> rax (nb) */
        MS_ASM_FUNC(modem_sync_unpack)
        "push rbx\n   push r12\n" MS_UP_LOAD
        "test r8, r8\n   jz 9f\n   test r12, r12\n   jz 9f\n   test r11, r11\n   jz 9f\n"
        "test ecx, ecx\n   jz 9f\n   cmp ecx, 8\n   ja 9f\n   cmp r10, r9\n   ja 9f\n"
        "sub r9, r10\n   cmp r9, r11\n   jb 9f\n   add r8, r10\n   mov rbx, r12\n"
        "cmp ecx, 4\n   je 1f\n"
        "5:  movzx eax, byte ptr [r8]\n   add r8, r11\n   sub r9, r11\n   mov edx, ecx\n"
        "6:  mov r10d, eax\n   and r10d, 1\n   mov byte ptr [r12], r10b\n   inc r12\n   shr eax, 1\n   dec edx\n   jnz 6b\n"
        "cmp r9, r11\n   jae 5b\n   jmp 8f\n"
        "1:  mov edx, 0x00204081\n"
        "2:  movzx eax, byte ptr [r8]\n   add r8, r11\n   sub r9, r11\n   and eax, 15\n"
        "imul eax, edx\n   and eax, 0x01010101\n   mov dword ptr [r12], eax\n   add r12, 4\n   cmp r9, r11\n   jae 2b\n"
        "8:  mov rax, r12\n   sub rax, rbx\n   pop r12\n   pop rbx\n" MS_ASM_RET
        "9:  xor eax, eax\n   pop r12\n   pop rbx\n" MS_ASM_RET
        MS_ASM_END(modem_sync_unpack)

        /* modem_sync_find(bits, nb, start_bit, sync_word) -> rax (bit idx or -1) */
        MS_ASM_FUNC(modem_sync_find)
        "push rbx\n   push r12\n" MS_FD_LOAD
        "test r8, r8\n   jz 9f\n   cmp r9, 32\n   jb 9f\n   sub r9, 32\n   cmp r10, r9\n   ja 9f\n"
        "sub r9, r10\n   add r8, r10\n   mov rbx, 0x0101010101010101\n   mov r12, 0x0102040810204080\n"
        "xor edx, edx\n   xor ecx, ecx\n"
        "1:  mov rax, [r8]\n   add r8, 8\n   and rax, rbx\n   imul rax, r12\n   shr rax, 56\n"
        "shl rax, cl\n   or rdx, rax\n   add ecx, 8\n   cmp ecx, 32\n   jb 1b\n"
        "cmp edx, r11d\n   je 8f\n"
        "2:  cmp r9, 8\n   jb 6f\n   mov rax, [r8]\n   add r8, 8\n   and rax, rbx\n   imul rax, r12\n"
        "shr rax, 56\n   shl rax, 32\n   or rdx, rax\n   sub r9, 8\n   mov ecx, 8\n"
        "3:  shr rdx, 1\n   inc r10\n   cmp edx, r11d\n   je 8f\n   dec ecx\n   jnz 3b\n   jmp 2b\n"
        "6:  test r9, r9\n   jz 9f\n"
        "7:  movzx eax, byte ptr [r8]\n   inc r8\n   and eax, 1\n   shl eax, 31\n   shr edx, 1\n   or edx, eax\n"
        "inc r10\n   cmp edx, r11d\n   je 8f\n   dec r9\n   jnz 7b\n   jmp 9f\n"
        "8:  mov rax, r10\n   pop r12\n   pop rbx\n" MS_ASM_RET
        "9:  mov rax, -1\n   pop r12\n   pop rbx\n" MS_ASM_RET
        MS_ASM_END(modem_sync_find)

        /* modem_sync_memo_has(frames, totals, count, stride, bytes, total) -> eax (0/1, CT) */
        /* regs: r8=frames, r9=totals, r10d=count, r11=stride, r12=bytes, rsi=total, edi=found */
        MS_ASM_FUNC(modem_sync_memo_has)
        MS_MM_PRO
        "test r8, r8\n   jz 9f\n   test r9, r9\n   jz 9f\n   test r12, r12\n   jz 9f\n"
        "test r10d, r10d\n   jz 9f\n   cmp rsi, r11\n   ja 9f\n   xor edi, edi\n"
        "1:  mov rbx, [r9]\n   add r9, 8\n   xor rbx, rsi\n   mov rcx, r8\n   add r8, r11\n   mov rdx, r12\n   push rsi\n"
        "2:  cmp rsi, 8\n   jb 3f\n   mov rax, [rcx]\n   xor rax, [rdx]\n   or rbx, rax\n"
        "add rcx, 8\n   add rdx, 8\n   sub rsi, 8\n   jmp 2b\n"
        "3:  test rsi, rsi\n   jz 4f\n   movzx eax, byte ptr [rcx]\n   xor al, byte ptr [rdx]\n"
        "or rbx, rax\n   inc rcx\n   inc rdx\n   dec rsi\n   jnz 3b\n"
        "4:  pop rsi\n   xor eax, eax\n   test rbx, rbx\n   sete al\n   or edi, eax\n   dec r10d\n   jnz 1b\n"
        "mov eax, edi\n" MS_MM_EPI MS_ASM_RET
        "9:  xor eax, eax\n" MS_MM_EPI MS_ASM_RET
        MS_ASM_END(modem_sync_memo_has)

        ".att_syntax prefix\n"
);

#elif defined(MS_RISCV64)

__asm__(
        MS_ASM_SECTION

        /* modem_sync_read_bytes(a0=bits, a1=nb, a2=p, a3=dst, a4=count) -> a0 (0/1) */
        MS_ASM_FUNC(modem_sync_read_bytes)
        "beqz a0, 3f\n   beqz a3, 3f\n   bgtu a2, a1, 3f\n"
        "sub t0, a1, a2\n   srli t0, t0, 3\n   bgtu a4, t0, 3f\n   beqz a4, 2f\n   add a0, a0, a2\n"
        "li t1, 0x0101010101010101\n   li t2, 0x0102040810204080\n"
        "1:  ld t3, 0(a0)\n   addi a0, a0, 8\n   and t3, t3, t1\n   mul t3, t3, t2\n   srli t3, t3, 56\n"
        "sb t3, 0(a3)\n   addi a3, a3, 1\n   addi a4, a4, -1\n   bnez a4, 1b\n"
        "2:  li a0, 1\n" MS_ASM_RET
        "3:  li a0, 0\n" MS_ASM_RET
        MS_ASM_END(modem_sync_read_bytes)

        /* modem_sync_unpack(a0=best, a1=n, a2=off, a3=sym, a4=sym_bits, a5=bits) -> a0 (nb) */
        MS_ASM_FUNC(modem_sync_unpack)
        "slli a4, a4, 32\n   srli a4, a4, 32\n   beqz a0, 9f\n   beqz a5, 9f\n   beqz a3, 9f\n   beqz a4, 9f\n"
        "li t0, 8\n   bgtu a4, t0, 9f\n   bgtu a2, a1, 9f\n   sub a1, a1, a2\n   bltu a1, a3, 9f\n"
        "add a0, a0, a2\n   mv a6, a5\n   li t0, 4\n   beq a4, t0, 1f\n"
        "5:  lbu t1, 0(a0)\n   add a0, a0, a3\n   sub a1, a1, a3\n   mv t2, a4\n"
        "6:  andi t3, t1, 1\n   sb t3, 0(a5)\n   addi a5, a5, 1\n   srli t1, t1, 1\n   addi t2, t2, -1\n   bnez t2, 6b\n"
        "bgeu a1, a3, 5b\n   j 8f\n"
        "1:  li t1, 0x00204081\n   li t2, 0x01010101\n"
        "2:  lbu t3, 0(a0)\n   add a0, a0, a3\n   sub a1, a1, a3\n   andi t3, t3, 15\n"
        "mul t3, t3, t1\n   and t3, t3, t2\n   sw t3, 0(a5)\n   addi a5, a5, 4\n   bgeu a1, a3, 2b\n"
        "8:  sub a0, a5, a6\n" MS_ASM_RET
        "9:  li a0, 0\n" MS_ASM_RET
        MS_ASM_END(modem_sync_unpack)

        /* modem_sync_find(a0=bits, a1=nb, a2=start_bit, a3=sync_word) -> a0 (bit idx or -1) */
        MS_ASM_FUNC(modem_sync_find)
        "sext.w a3, a3\n   beqz a0, 9f\n   li t0, 32\n   bltu a1, t0, 9f\n   addi a1, a1, -32\n   bgtu a2, a1, 9f\n"
        "sub a1, a1, a2\n   add a0, a0, a2\n   li t1, 0x0101010101010101\n   li t2, 0x0102040810204080\n"
        "li a4, 0\n   li t3, 0\n   li t4, 32\n"
        "1:  ld t5, 0(a0)\n   addi a0, a0, 8\n   and t5, t5, t1\n   mul t5, t5, t2\n   srli t5, t5, 56\n"
        "sll t5, t5, t3\n   or a4, a4, t5\n   addi t3, t3, 8\n   bltu t3, t4, 1b\n"
        "sext.w t5, a4\n   beq t5, a3, 8f\n   li t6, 8\n"
        "2:  bltu a1, t6, 6f\n   ld t5, 0(a0)\n   addi a0, a0, 8\n   and t5, t5, t1\n   mul t5, t5, t2\n"
        "srli t5, t5, 56\n   slli t5, t5, 32\n   or a4, a4, t5\n   addi a1, a1, -8\n   li t3, 8\n"
        "3:  srli a4, a4, 1\n   addi a2, a2, 1\n   sext.w t5, a4\n   beq t5, a3, 8f\n   addi t3, t3, -1\n   bnez t3, 3b\n   j 2b\n"
        "6:  beqz a1, 9f\n"
        "7:  lbu t5, 0(a0)\n   addi a0, a0, 1\n   andi t5, t5, 1\n   slli t5, t5, 31\n   srli a4, a4, 1\n"
        "or a4, a4, t5\n   addi a2, a2, 1\n   sext.w t4, a4\n   beq t4, a3, 8f\n   addi a1, a1, -1\n   bnez a1, 7b\n   j 9f\n"
        "8:  mv a0, a2\n" MS_ASM_RET
        "9:  li a0, -1\n" MS_ASM_RET
        MS_ASM_END(modem_sync_find)

        /* modem_sync_memo_has(a0=frames, a1=totals, a2=count, a3=stride, a4=bytes, a5=total) -> a0 (0/1, CT) */
        MS_ASM_FUNC(modem_sync_memo_has)
        "slli a2, a2, 32\n   srli a2, a2, 32\n   beqz a0, 9f\n   beqz a1, 9f\n   beqz a4, 9f\n   beqz a2, 9f\n"
        "bgtu a5, a3, 9f\n   li a6, 0\n   li t6, 8\n"
        "1:  ld t0, 0(a1)\n   addi a1, a1, 8\n   xor t0, t0, a5\n   mv t1, a0\n   add a0, a0, a3\n   mv t2, a4\n   mv t3, a5\n"
        "2:  bltu t3, t6, 3f\n   ld t4, 0(t1)\n   ld t5, 0(t2)\n   xor t4, t4, t5\n   or t0, t0, t4\n"
        "addi t1, t1, 8\n   addi t2, t2, 8\n   addi t3, t3, -8\n   j 2b\n"
        "3:  beqz t3, 4f\n   lbu t4, 0(t1)\n   lbu t5, 0(t2)\n   xor t4, t4, t5\n   or t0, t0, t4\n"
        "addi t1, t1, 1\n   addi t2, t2, 1\n   addi t3, t3, -1\n   bnez t3, 3b\n"
        "4:  seqz t4, t0\n   or a6, a6, t4\n   addi a2, a2, -1\n   bnez a2, 1b\n"
        "mv a0, a6\n" MS_ASM_RET
        "9:  li a0, 0\n" MS_ASM_RET
        MS_ASM_END(modem_sync_memo_has)
);

#endif

#endif /* WATERLINK_MODEM_SYNC_ASM_IMPLEMENTATION */
