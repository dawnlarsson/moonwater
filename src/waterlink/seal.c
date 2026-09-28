/*
        Sealing and opening a waterlink datagram, over lib.c's assembly.

        AES-128-GCM, shaped to the datagram rather than to a general record.
        Because the datagram is whole blocks and the cleartext header is
        exactly one of them (waterlink.c says why), the header, box and tag
        sit at fixed offsets and the whole operation is one assembly body
        around two calls into lib.c: aes128_ctr_blocks once, ghash_blocks
        once. A datagram of
        acknowledgements alone is the same shape cut shorter. The general path in
        net.c pays for an arbitrary record -- a separate call for the tag's
        block, padding for partial blocks, and a framing layer around both --
        and a datagram needs none of it.

        The one trick is the tag's mask. GCM encrypts counter one for the tag
        and counters two onward for the text, which is one counter-mode run
        starting at one if the block in front of the text is zero. The header
        is that block: it is set aside, the slot is zeroed, seventy four
        blocks go through in a single call, and the first block out is the
        mask. Then the header comes back and is hashed as what it always was,
        the associated data.

        This is included where lib.c and net.c's crypto are already in scope;
        the prepared key is net.c's crypto_aesgcm_key.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_SEAL_INCLUDED
#define WATERLINK_SEAL_INCLUDED

#include "waterlink.c"

/*
        The two bodies, one per machine, each around one call to
        aes128_ctr_blocks and one to ghash_blocks.

        Sealing: the box is zeroed from used to its end, and the counter
        runs over header and box from J0 -- the nonce is the header's
        counter, little endian, behind four zero bytes (WireGuard's layout;
        each direction of a session has its own key, so two ends counting up
        from one never use a nonce twice under one key), then GCM's block
        counter at one. The first block's key stream is the tag's mask; the
        header goes back, GCM's lengths block sits where the tag goes (the
        header is 128 bits of associated data, the box is the text), and
        header, box and lengths
        are hashed in one run -- one reduction of the state instead of two,
        which is the part of hashing a datagram nothing runs beside. The tag
        is the hash xored with the mask.

        Opening: the tag that arrived is kept, the lengths block borrows its
        slot, the hash is taken over the ciphertext and the slot given back;
        then the counter run decrypts, and the hash with the mask must equal
        the tag in all sixteen bytes, compared whole so a wrong tag says
        nothing about where it went wrong. A box that does not open is
        wiped: the counter run has already turned it into plaintext-shaped
        bytes, and unauthenticated plaintext is not something to leave in a
        buffer the caller might read.

        They were C around memory_copy, memory_zero and crypto_forget, and
        what that cost was not the calls: the zero state, the counter block,
        the lengths and the header went to memory a byte or eight bytes at a
        time just before the two routines loaded them sixteen or sixty four
        at a time, and a load that no single store can answer waits for every
        store in front of it -- the whole counter run's -- to reach the
        cache. On the 9950X ghash_blocks spent more of a seal waiting on its
        state than hashing. Here each of those blocks is one store, made as
        early as it can be; the header is never zeroed, since the run turns
        it into the header xored with the mask and the mask is that xored
        with the header again; and the hash, the mask and the tag live in a
        few words of stack that are wiped before returning. The key is
        net.c's prepared key: the 176-byte schedule, then the GHASH table at
        192.
*/
_Static_assert(__builtin_offsetof(crypto_aesgcm_key, table) == 192,
               "the seal bodies find the GHASH table at 192");

fn waterlink_seal_box(crypto_aesgcm_key address_to key, p8 address_to datagram,
                      positive used, positive box);
bool waterlink_open_box(crypto_aesgcm_key address_to key,
                        p8 address_to datagram, positive box);

#if X64
/*
        The small bodies' pieces. xmm0 to xmm4 are the key stream from J0,
        xmm11 the header, xmm15 the byte reversal GHASH works in; the hash
        sums its products into xmm8 (low), xmm9 (high) and xmm10 (middle),
        against the power %r8 points at, and folds them once at the end.
*/
#define WATERLINK_SMALL_ROUND(op, at)                                         \
    "movdqu " at "(%rdi), %xmm5\n"                                            \
    op " %xmm5, %xmm0\n   " op " %xmm5, %xmm1\n   " op " %xmm5, %xmm2\n"      \
    op " %xmm5, %xmm3\n   " op " %xmm5, %xmm4\n"
//  The header is read as the two words a caller writes it as -- a block
//  loaded across two stores is not forwarded and waits for both to reach
//  the cache -- and J0 needs only the counter's word.
#define WATERLINK_SMALL_COUNTERS                                              \
    "movq 8(%rsi), %xmm0\n   pslldq $4, %xmm0\n"                              \
    "movdqa .Lwaterlink_small_one(%rip), %xmm5\n   por %xmm5, %xmm0\n"         \
    "movdqa %xmm0, %xmm1\n   paddd %xmm5, %xmm1\n"                            \
    "movdqa %xmm1, %xmm2\n   paddd %xmm5, %xmm2\n"                            \
    "movdqa %xmm2, %xmm3\n   paddd %xmm5, %xmm3\n"                            \
    "movdqa %xmm3, %xmm4\n   paddd %xmm5, %xmm4\n"                            \
    "movq (%rsi), %xmm11\n   movhps 8(%rsi), %xmm11\n"
#define WATERLINK_SMALL_AES                                                   \
    WATERLINK_SMALL_ROUND("pxor", "0")                                        \
    WATERLINK_SMALL_ROUND("aesenc", "16")                                     \
    WATERLINK_SMALL_ROUND("aesenc", "32")                                     \
    WATERLINK_SMALL_ROUND("aesenc", "48")                                     \
    WATERLINK_SMALL_ROUND("aesenc", "64")                                     \
    WATERLINK_SMALL_ROUND("aesenc", "80")                                     \
    WATERLINK_SMALL_ROUND("aesenc", "96")                                     \
    WATERLINK_SMALL_ROUND("aesenc", "112")                                    \
    WATERLINK_SMALL_ROUND("aesenc", "128")                                    \
    WATERLINK_SMALL_ROUND("aesenc", "144")                                    \
    WATERLINK_SMALL_ROUND("aesenclast", "160")
#define WATERLINK_SMALL_PRODUCT(x)                                            \
    "movdqa " x ", %xmm7\n   pclmulqdq $0x00, (%r8), %xmm7\n"                 \
    "pxor %xmm7, %xmm8\n"                                                     \
    "movdqa " x ", %xmm7\n   pclmulqdq $0x11, (%r8), %xmm7\n"                 \
    "pxor %xmm7, %xmm9\n"                                                     \
    "movdqa " x ", %xmm7\n   psrldq $8, %xmm7\n   pxor " x ", %xmm7\n"         \
    "pclmulqdq $0x00, 768(%r8), %xmm7\n   pxor %xmm7, %xmm10\n"                \
    "add $16, %r8\n"
//  The header against H^(n+2), from 768 - 16 (n + 2) in the table at 192.
#define WATERLINK_SMALL_HASH_BEGIN(box)                                       \
    "movdqa .Lwaterlink_bswap(%rip), %xmm15\n"                                \
    "pxor %xmm8, %xmm8\n   pxor %xmm9, %xmm9\n   pxor %xmm10, %xmm10\n"       \
    "lea 928(%rdi), %r8\n   sub " box ", %r8\n"                               \
    "pshufb %xmm15, %xmm11\n"                                                 \
    WATERLINK_SMALL_PRODUCT("%xmm11")
#define WATERLINK_SMALL_HASH_START WATERLINK_SMALL_HASH_BEGIN("%rcx")
#define WATERLINK_SMALL_HASH_START_OPEN WATERLINK_SMALL_HASH_BEGIN("%rdx")
//  A block sealed: the text to used, xored with its key stream, stored,
//  and hashed.
#define WATERLINK_SMALL_SEAL_BLOCK(stream, at, index)                         \
    "movdqu " at "(%rsi), %xmm6\n   movdqa %xmm13, %xmm12\n"                  \
    "pcmpgtb " index "(%r9), %xmm12\n   pand %xmm12, %xmm6\n"                 \
    "pxor %xmm6, " stream "\n   movdqu " stream ", " at "(%rsi)\n"             \
    "movdqa " stream ", %xmm6\n   pshufb %xmm15, %xmm6\n"                     \
    WATERLINK_SMALL_PRODUCT("%xmm6")
//  A block as it came, hashed.
#define WATERLINK_SMALL_OPEN_BLOCK(at)                                        \
    "movdqu " at "(%rsi), %xmm6\n   pshufb %xmm15, %xmm6\n"                   \
    WATERLINK_SMALL_PRODUCT("%xmm6")
//  The lengths block against H^1 -- 128 bits of header, the box's bits of
//  text, reversed -- then the fold, and the hash xored with E(J0) in xmm9.
#define WATERLINK_SMALL_HASH_END(box)                                         \
    "lea (," box ",8), %rax\n   movq %rax, %xmm6\n"                           \
    "por .Lwaterlink_small_lengths(%rip), %xmm6\n"                            \
    WATERLINK_SMALL_PRODUCT("%xmm6")                                          \
    "pxor %xmm8, %xmm10\n   pxor %xmm9, %xmm10\n"                             \
    "movdqa %xmm10, %xmm7\n   pslldq $8, %xmm7\n   psrldq $8, %xmm10\n"        \
    "pxor %xmm7, %xmm8\n   pxor %xmm10, %xmm9\n"                              \
    "movdqa %xmm8, %xmm7\n   pclmulqdq $0x00, .Lwaterlink_poly(%rip), %xmm7\n" \
    "pshufd $0x4e, %xmm8, %xmm8\n   pxor %xmm7, %xmm8\n"                      \
    "movdqa %xmm8, %xmm7\n   pclmulqdq $0x00, .Lwaterlink_poly(%rip), %xmm7\n" \
    "pshufd $0x4e, %xmm8, %xmm8\n   pxor %xmm7, %xmm9\n   pxor %xmm8, %xmm9\n" \
    "pshufb %xmm15, %xmm9\n   pxor %xmm0, %xmm9\n"
//  Key stream, text and hash leave no register behind.
#define WATERLINK_SMALL_WIPE                                                  \
    "pxor %xmm0, %xmm0\n   pxor %xmm1, %xmm1\n   pxor %xmm2, %xmm2\n"         \
    "pxor %xmm3, %xmm3\n   pxor %xmm4, %xmm4\n   pxor %xmm5, %xmm5\n"         \
    "pxor %xmm6, %xmm6\n   pxor %xmm7, %xmm7\n   pxor %xmm8, %xmm8\n"         \
    "pxor %xmm9, %xmm9\n   pxor %xmm10, %xmm10\n   pxor %xmm11, %xmm11\n"     \
    "pxor %xmm12, %xmm12\n   pxor %xmm13, %xmm13\n"

/*
        The whole-datagram bodies' pieces. zmm20 to zmm30 are the round keys,
        zmm31 the byte reversal, zmm16 the next four counter blocks and zmm18
        four; k1 skips the header's block, k2 keeps the box's last two and k3
        picks the lane the running hash is folded into. A product sums into
        zmm13 (low), zmm14 (high) and zmm15 (middle), against the powers at
        lo and the middle terms' at mid in the table %r9 points at.
*/
#define WATERLINK_ZMM_SETUP                                                   \
    "vbroadcasti32x4 .Lwaterlink_bswap(%rip), %zmm31\n   vbroadcasti32x4 0(%rdi), %zmm20\n" \
    "vbroadcasti32x4 16(%rdi), %zmm21\n   vbroadcasti32x4 32(%rdi), %zmm22\n"  \
    "vbroadcasti32x4 48(%rdi), %zmm23\n   vbroadcasti32x4 64(%rdi), %zmm24\n"  \
    "vbroadcasti32x4 80(%rdi), %zmm25\n   vbroadcasti32x4 96(%rdi), %zmm26\n"  \
    "vbroadcasti32x4 112(%rdi), %zmm27\n   vbroadcasti32x4 128(%rdi), %zmm28\n" \
    "vbroadcasti32x4 144(%rdi), %zmm29\n   vbroadcasti32x4 160(%rdi), %zmm30\n" \
    "lea 192(%rdi), %r9\n   mov 8(%rsi), %rax\n   bswap %rax\n   mov %rax, %r8\n" \
    "shl $32, %r8\n   or $1, %r8\n   shr $32, %rax\n   vmovq %r8, %xmm8\n   vpinsrq $1, %rax, %xmm8, %xmm8\n" \
    "vshufi64x2 $0, %zmm8, %zmm8, %zmm16\n   vpaddd .Lwaterlink_steps(%rip), %zmm16, %zmm16\n" \
    "vbroadcasti32x4 .Lwaterlink_four(%rip), %zmm18\n   mov $0xfc, %eax\n"   \
    "kmovw %eax, %k1\n   mov $0x0f, %eax\n   kmovw %eax, %k2\n   mov $0x0c, %eax\n" \
    "kmovw %eax, %k3\n"
//  Four counter blocks to zmm r, whitened; then the rounds, a key at a time
//  across the registers, so each round's latency hides behind the others.
#define WATERLINK_ZMM_KEYS(r)                                                 \
    "vpshufb %zmm31, %zmm16, %zmm" r "\n   vpaddd %zmm18, %zmm16, %zmm16\n"   \
    "vpxorq %zmm20, %zmm" r ", %zmm" r "\n"
#define WATERLINK_ZMM_ROUND3(op, k, a, b, c)                                  \
    op " %zmm" k ", %zmm" a ", %zmm" a "\n   " op " %zmm" k ", %zmm" b ", %zmm" b "\n   " \
    op " %zmm" k ", %zmm" c ", %zmm" c "\n"
#define WATERLINK_ZMM_ROUND4(op, k, a, b, c, d)                               \
    WATERLINK_ZMM_ROUND3(op, k, a, b, c) op " %zmm" k ", %zmm" d ", %zmm" d "\n"
#define WATERLINK_ZMM_ROUNDS(round, ...)                                      \
    round("vaesenc", "21", __VA_ARGS__) round("vaesenc", "22", __VA_ARGS__)   \
    round("vaesenc", "23", __VA_ARGS__) round("vaesenc", "24", __VA_ARGS__)   \
    round("vaesenc", "25", __VA_ARGS__) round("vaesenc", "26", __VA_ARGS__)   \
    round("vaesenc", "27", __VA_ARGS__) round("vaesenc", "28", __VA_ARGS__)   \
    round("vaesenc", "29", __VA_ARGS__) round("vaesenclast", "30", __VA_ARGS__)
#define WATERLINK_ZMM_AES4(a, b, c, d)                                        \
    WATERLINK_ZMM_KEYS(a) WATERLINK_ZMM_KEYS(b) WATERLINK_ZMM_KEYS(c)         \
    WATERLINK_ZMM_KEYS(d) WATERLINK_ZMM_ROUNDS(WATERLINK_ZMM_ROUND4, a, b, c, d)
#define WATERLINK_ZMM_AES3(a, b, c)                                           \
    WATERLINK_ZMM_KEYS(a) WATERLINK_ZMM_KEYS(b) WATERLINK_ZMM_KEYS(c)         \
    WATERLINK_ZMM_ROUNDS(WATERLINK_ZMM_ROUND3, a, b, c)
//  zmm x's four blocks, reversed, into the sums: the first of a run sets
//  them, the rest add to them.
#define WATERLINK_ZMM_PRODUCT(x, lo, mid, l, h, m)                            \
    "vpshufb %zmm31, %zmm" x ", %zmm8\n   vpsrldq $8, %zmm8, %zmm9\n"         \
    "vpxorq %zmm8, %zmm9, %zmm9\n   vpclmulqdq $0x00, " lo "(%r9), %zmm8, %zmm" l "\n" \
    "vpclmulqdq $0x11, " lo "(%r9), %zmm8, %zmm" h "\n"                       \
    "vpclmulqdq $0x00, " mid "(%r9), %zmm9, %zmm" m "\n"
#define WATERLINK_ZMM_HASH_FIRST(x, lo, mid)                                  \
    WATERLINK_ZMM_PRODUCT(x, lo, mid, "13", "14", "15")
#define WATERLINK_ZMM_HASH(x, lo, mid)                                        \
    WATERLINK_ZMM_PRODUCT(x, lo, mid, "10", "11", "12")                       \
    "vpxorq %zmm10, %zmm13, %zmm13\n   vpxorq %zmm11, %zmm14, %zmm14\n"       \
    "vpxorq %zmm12, %zmm15, %zmm15\n"
//  Key stream r over the datagram's bytes at at, in place.
#define WATERLINK_ZMM_XOR_STORE(r, at)                                        \
    "vpxorq " at "(%rsi), %zmm" r ", %zmm" r "\n   vmovdqu64 %zmm" r ", " at "(%rsi)\n"
//  Opening: the four blocks at at to zmm x, hashed as they came, then
//  decrypted with key stream k in place.
#define WATERLINK_ZMM_OPEN(x, k, at, mid)                                     \
    "vmovdqu64 " at "(%rsi), %zmm" x "\n" WATERLINK_ZMM_HASH(x, at, mid)      \
    "vpxorq %zmm" x ", %zmm" k ", %zmm" k "\n   vmovdqu64 %zmm" k ", " at "(%rsi)\n"
//  The sums' four lanes folded to one and reduced, into xmm14.
#define WATERLINK_ZMM_REDUCE                                                  \
    "vpternlogq $0x96, %zmm14, %zmm13, %zmm15\n   vpslldq $8, %zmm15, %zmm12\n" \
    "vpsrldq $8, %zmm15, %zmm15\n   vpxorq %zmm12, %zmm13, %zmm13\n   vpxorq %zmm15, %zmm14, %zmm14\n" \
    "vextracti64x4 $1, %zmm13, %ymm12\n   vpxorq %ymm12, %ymm13, %ymm13\n"    \
    "vextracti32x4 $1, %ymm13, %xmm12\n   vpxorq %xmm12, %xmm13, %xmm13\n"    \
    "vextracti64x4 $1, %zmm14, %ymm12\n   vpxorq %ymm12, %ymm14, %ymm14\n"    \
    "vextracti32x4 $1, %ymm14, %xmm12\n   vpxorq %xmm12, %xmm14, %xmm14\n"    \
    "vpclmulqdq $0x00, .Lwaterlink_poly(%rip), %xmm13, %xmm12\n   vpshufd $0x4e, %xmm13, %xmm13\n" \
    "vpxorq %xmm12, %xmm13, %xmm13\n   vpclmulqdq $0x00, .Lwaterlink_poly(%rip), %xmm13, %xmm12\n" \
    "vpshufd $0x4e, %xmm13, %xmm13\n   vpternlogq $0x96, %xmm12, %xmm13, %xmm14\n"
//  Every vector register and the header mask left zero, and back.
#define WATERLINK_ZMM_WIPE                                                    \
    "vpxorq %xmm0, %xmm0, %xmm0\n   vpxorq %xmm1, %xmm1, %xmm1\n   vpxorq %xmm2, %xmm2, %xmm2\n" \
    "vpxorq %xmm3, %xmm3, %xmm3\n   vpxorq %xmm4, %xmm4, %xmm4\n   vpxorq %xmm5, %xmm5, %xmm5\n" \
    "vpxorq %xmm6, %xmm6, %xmm6\n   vpxorq %xmm7, %xmm7, %xmm7\n   vpxorq %xmm8, %xmm8, %xmm8\n" \
    "vpxorq %xmm9, %xmm9, %xmm9\n   vpxorq %xmm10, %xmm10, %xmm10\n   vpxorq %xmm11, %xmm11, %xmm11\n" \
    "vpxorq %xmm12, %xmm12, %xmm12\n   vpxorq %xmm13, %xmm13, %xmm13\n   vpxorq %xmm14, %xmm14, %xmm14\n" \
    "vpxorq %xmm15, %xmm15, %xmm15\n   vpxorq %xmm16, %xmm16, %xmm16\n   vpxorq %xmm17, %xmm17, %xmm17\n" \
    "vpxorq %xmm18, %xmm18, %xmm18\n   vpxorq %xmm19, %xmm19, %xmm19\n   vpxorq %xmm20, %xmm20, %xmm20\n" \
    "vpxorq %xmm21, %xmm21, %xmm21\n   vpxorq %xmm22, %xmm22, %xmm22\n   vpxorq %xmm23, %xmm23, %xmm23\n" \
    "vpxorq %xmm24, %xmm24, %xmm24\n   vpxorq %xmm25, %xmm25, %xmm25\n   vpxorq %xmm26, %xmm26, %xmm26\n" \
    "vpxorq %xmm27, %xmm27, %xmm27\n   vpxorq %xmm28, %xmm28, %xmm28\n   vpxorq %xmm29, %xmm29, %xmm29\n" \
    "vpxorq %xmm30, %xmm30, %xmm30\n   vpxorq %xmm31, %xmm31, %xmm31\n"        \
    "kxorw %k1, %k1, %k1\n   vzeroupper\n   ret\n"

__asm__(
    ASM_FUNC(waterlink_seal_box)
    "cmp $64, %rcx\n   ja .Lwaterlink_seal_box_general\n"
    "cmpb $0, cpu_has_aes(%rip)\n   je .Lwaterlink_seal_box_general\n"
    "cmpb $0, cpu_has_pclmul(%rip)\n   je .Lwaterlink_seal_box_general\n"
    "jmp waterlink_seal_small\n"
    ".Lwaterlink_seal_box_general:\n"
    "push %rbx\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n"
    "sub $48, %rsp\n"
    "mov %rdi, %rbx\n   mov %rsi, %r12\n   mov %rcx, %r13\n"
    "cmp %rcx, %rdx\n   jae 1f\n"
    "lea 16(%rsi,%rdx), %rdi\n   mov %rcx, %rsi\n   sub %rdx, %rsi\n"
    "call memory_zero\n"
    "1:  cmp $1168, %r13\n   jne 2f\n"
    "cmpb $0, cpu_has_vaes(%rip)\n   je 2f\n"
    "cmpb $0, cpu_has_vpclmul(%rip)\n   je 2f\n"
    "cmpb $0, cpu_has_avx512(%rip)\n   je 2f\n"
    "mov %rbx, %rdi\n   mov %r12, %rsi\n   call .Lwaterlink_seal_zmm\n"
    "jmp 3f\n"
    //  Every block the two calls load is written whole, in one store, and
    //  long enough before: a block written in two halves cannot be forwarded
    //  to a load of all of it, and the load then waits for every store in
    //  front of it to reach the cache -- the counter run's included. The
    //  lengths go in the tag's slot now, the hash's zero state beside J0.
    "2:  lea 16(%r12,%r13), %r14\n"
    "movabs $0x8000000000000000, %rax\n   mov %r13, %rdx\n   shl $3, %rdx\n   bswap %rdx\n"
    "movq %rax, %xmm0\n   movq %rdx, %xmm1\n   punpcklqdq %xmm1, %xmm0\n   movdqu %xmm0, (%r14)\n"
    "pxor %xmm0, %xmm0\n   movdqu %xmm0, 32(%rsp)\n"
    //  J0 at 0(%rsp): four zero bytes, the counter, then 00 00 00 01.
    "mov 8(%r12), %r15\n   mov %r15, %rax\n   shl $32, %rax\n   mov %r15, %rdx\n   shr $32, %rdx\n"
    "mov $0x01000000, %ecx\n   shl $32, %rcx\n   or %rcx, %rdx\n"
    "movq %rax, %xmm0\n   movq %rdx, %xmm1\n   punpcklqdq %xmm1, %xmm0\n   movdqu %xmm0, (%rsp)\n"
    //  The run covers the header too, so the header's own block comes out
    //  as the header xored with the tag's mask; it is put back after.
    "movdqu (%r12), %xmm2\n   movdqu %xmm2, 16(%rsp)\n"
    "mov %rbx, %rdi\n   mov %rsp, %rsi\n   mov %r12, %rdx\n   mov %r12, %rcx\n"
    "mov %r13, %r8\n   shr $4, %r8\n   inc %r8\n"
    "call aes128_ctr_blocks\n"
    "movdqu 16(%rsp), %xmm2\n   movdqu (%r12), %xmm0\n   pxor %xmm2, %xmm0\n"
    "movdqu %xmm0, 16(%rsp)\n   movdqu %xmm2, (%r12)\n"
    "lea 32(%rsp), %rdi\n   lea 192(%rbx), %rsi\n   mov %r12, %rdx\n"
    "mov %r13, %rcx\n   shr $4, %rcx\n   add $2, %rcx\n"
    "call ghash_blocks\n"
    "movdqu 32(%rsp), %xmm0\n   movdqu 16(%rsp), %xmm1\n   pxor %xmm1, %xmm0\n   movdqu %xmm0, (%r14)\n"
    "pxor %xmm0, %xmm0\n   pxor %xmm1, %xmm1\n   pxor %xmm2, %xmm2\n"
    "movdqu %xmm0, (%rsp)\n   movdqu %xmm0, 16(%rsp)\n   movdqu %xmm0, 32(%rsp)\n"
    "3:  add $48, %rsp\n"
    "pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbx\n"
    ASM_RET
    //  A whole datagram on a machine with AVX-512, VAES and VPCLMULQDQ:
    //  one pass, the key stream of each four vectors computed while the
    //  four before are hashed, with no load of anything a store just wrote.
    ".Lwaterlink_seal_zmm:\n"
    WATERLINK_ZMM_SETUP WATERLINK_ZMM_AES4("0", "1", "2", "3")
    "vmovdqa64 %xmm0, %xmm19\n   vpxorq 0(%rsi), %zmm0, %zmm0\n   vpxorq 64(%rsi), %zmm1, %zmm1\n"
    "vpxorq 128(%rsi), %zmm2, %zmm2\n   vpxorq 192(%rsi), %zmm3, %zmm3\n   vmovdqu64 %zmm0, (%rsi){%k1}\n"
    "vmovdqu64 %zmm1, 64(%rsi)\n   vmovdqu64 %zmm2, 128(%rsi)\n   vmovdqu64 %zmm3, 192(%rsi)\n"
    "vpxorq %zmm19, %zmm0, %zmm0\n"
    WATERLINK_ZMM_AES4("4", "5", "6", "7")
    WATERLINK_ZMM_HASH_FIRST("0", "0", "768")
    WATERLINK_ZMM_HASH("1", "64", "832") WATERLINK_ZMM_HASH("2", "128", "896")
    WATERLINK_ZMM_HASH("3", "192", "960") WATERLINK_ZMM_XOR_STORE("4", "256")
    WATERLINK_ZMM_XOR_STORE("5", "320") WATERLINK_ZMM_XOR_STORE("6", "384")
    WATERLINK_ZMM_XOR_STORE("7", "448") WATERLINK_ZMM_AES4("0", "1", "2", "3")
    WATERLINK_ZMM_HASH("4", "256", "1024")
    WATERLINK_ZMM_HASH("5", "320", "1088")
    WATERLINK_ZMM_HASH("6", "384", "1152")
    WATERLINK_ZMM_HASH("7", "448", "1216") WATERLINK_ZMM_XOR_STORE("0", "512")
    WATERLINK_ZMM_XOR_STORE("1", "576") WATERLINK_ZMM_XOR_STORE("2", "640")
    WATERLINK_ZMM_XOR_STORE("3", "704") WATERLINK_ZMM_AES4("4", "5", "6", "7")
    WATERLINK_ZMM_HASH("0", "512", "1280")
    WATERLINK_ZMM_HASH("1", "576", "1344")
    WATERLINK_ZMM_HASH("2", "640", "1408")
    WATERLINK_ZMM_HASH("3", "704", "1472") WATERLINK_ZMM_XOR_STORE("4", "768")
    WATERLINK_ZMM_XOR_STORE("5", "832") WATERLINK_ZMM_XOR_STORE("6", "896")
    WATERLINK_ZMM_XOR_STORE("7", "960") WATERLINK_ZMM_REDUCE
    "vmovdqa64 %xmm14, %xmm17\n"
    WATERLINK_ZMM_AES3("0", "1", "2")
    "valignq $6, %zmm4, %zmm4, %zmm12{%k1}{z}\n   vpshufb %zmm31, %zmm12, %zmm8\n   vshufi64x2 $0, %zmm17, %zmm17, %zmm12\n"
    "vpxorq %zmm12, %zmm8, %zmm8{%k3}\n   vpsrldq $8, %zmm8, %zmm9\n   vpxorq %zmm8, %zmm9, %zmm9\n"
    "vpclmulqdq $0x00, 320(%r9), %zmm8, %zmm13\n   vpclmulqdq $0x11, 320(%r9), %zmm8, %zmm14\n   vpclmulqdq $0x00, 1088(%r9), %zmm9, %zmm15\n"
    "valignq $6, %zmm4, %zmm5, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "384", "1152")
    "valignq $6, %zmm5, %zmm6, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "448", "1216")
    "valignq $6, %zmm6, %zmm7, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "512", "1280") WATERLINK_ZMM_XOR_STORE("0", "1024")
    WATERLINK_ZMM_XOR_STORE("1", "1088")
    "vmovdqu64 1152(%rsi), %zmm8{%k2}{z}\n   vpxorq %zmm8, %zmm2, %zmm2\n   vmovdqu64 %zmm2, 1152(%rsi){%k2}\n"
    "vinserti32x4 $2, .Lwaterlink_lengths(%rip), %zmm2, %zmm2\n   valignq $6, %zmm7, %zmm0, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "576", "1344")
    "valignq $6, %zmm0, %zmm1, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "640", "1408")
    "valignq $6, %zmm1, %zmm2, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "704", "1472") WATERLINK_ZMM_REDUCE
    "vmovdqa64 %xmm14, %xmm17\n   vpshufb %xmm31, %xmm17, %xmm17\n   vpxorq %xmm19, %xmm17, %xmm17\n"
    "vmovdqu64 %xmm17, 1184(%rsi)\n"
    WATERLINK_ZMM_WIPE
    ".Lwaterlink_open_zmm:\n"
    WATERLINK_ZMM_SETUP WATERLINK_ZMM_AES4("0", "1", "2", "3")
    "vmovdqa64 %xmm0, %xmm19\n   vmovdqu64 0(%rsi), %zmm4\n"
    WATERLINK_ZMM_HASH_FIRST("4", "0", "768")
    "vpxorq %zmm4, %zmm0, %zmm0\n   vmovdqu64 %zmm0, (%rsi){%k1}\n"
    WATERLINK_ZMM_OPEN("5", "1", "64", "832")
    WATERLINK_ZMM_OPEN("6", "2", "128", "896")
    WATERLINK_ZMM_OPEN("7", "3", "192", "960")
    WATERLINK_ZMM_AES4("0", "1", "2", "3")
    WATERLINK_ZMM_OPEN("4", "0", "256", "1024")
    WATERLINK_ZMM_OPEN("5", "1", "320", "1088")
    WATERLINK_ZMM_OPEN("6", "2", "384", "1152")
    WATERLINK_ZMM_OPEN("7", "3", "448", "1216")
    WATERLINK_ZMM_AES4("0", "1", "2", "3")
    WATERLINK_ZMM_OPEN("4", "0", "512", "1280")
    WATERLINK_ZMM_OPEN("5", "1", "576", "1344")
    WATERLINK_ZMM_OPEN("6", "2", "640", "1408")
    WATERLINK_ZMM_OPEN("7", "3", "704", "1472") WATERLINK_ZMM_REDUCE
    "vmovdqa64 %xmm14, %xmm17\n"
    WATERLINK_ZMM_AES4("0", "1", "2", "3")
    "vmovdqu64 768(%rsi), %zmm12\n   valignq $6, %zmm12, %zmm12, %zmm12{%k1}{z}\n   vpshufb %zmm31, %zmm12, %zmm8\n"
    "vshufi64x2 $0, %zmm17, %zmm17, %zmm12\n   vpxorq %zmm12, %zmm8, %zmm8{%k3}\n   vpsrldq $8, %zmm8, %zmm9\n"
    "vpxorq %zmm8, %zmm9, %zmm9\n   vpclmulqdq $0x00, 320(%r9), %zmm8, %zmm13\n   vpclmulqdq $0x11, 320(%r9), %zmm8, %zmm14\n"
    "vpclmulqdq $0x00, 1088(%r9), %zmm9, %zmm15\n   vmovdqu64 816(%rsi), %zmm12\n"
    WATERLINK_ZMM_HASH("12", "384", "1152")
    "vmovdqu64 880(%rsi), %zmm12\n"
    WATERLINK_ZMM_HASH("12", "448", "1216")
    "vmovdqu64 944(%rsi), %zmm12\n"
    WATERLINK_ZMM_HASH("12", "512", "1280")
    "vmovdqu64 1008(%rsi), %zmm12\n"
    WATERLINK_ZMM_HASH("12", "576", "1344")
    "vmovdqu64 1072(%rsi), %zmm12\n"
    WATERLINK_ZMM_HASH("12", "640", "1408")
    "vmovdqu64 1136(%rsi), %zmm12\n   vinserti32x4 $3, .Lwaterlink_lengths(%rip), %zmm12, %zmm12\n"
    WATERLINK_ZMM_HASH("12", "704", "1472") WATERLINK_ZMM_XOR_STORE("0", "768")
    WATERLINK_ZMM_XOR_STORE("1", "832") WATERLINK_ZMM_XOR_STORE("2", "896")
    WATERLINK_ZMM_XOR_STORE("3", "960") WATERLINK_ZMM_AES3("0", "1", "2")
    WATERLINK_ZMM_XOR_STORE("0", "1024") WATERLINK_ZMM_XOR_STORE("1", "1088")
    "vmovdqu64 1152(%rsi), %zmm8{%k2}{z}\n   vpxorq %zmm8, %zmm2, %zmm2\n   vmovdqu64 %zmm2, 1152(%rsi){%k2}\n"
    WATERLINK_ZMM_REDUCE
    "vmovdqa64 %xmm14, %xmm17\n   vpshufb %xmm31, %xmm17, %xmm17\n   vpxorq %xmm19, %xmm17, %xmm17\n"
    "vmovdqu64 %xmm17, (%rcx)\n"
    WATERLINK_ZMM_WIPE
    //  A box of one to four blocks, in registers from end to end: the five
    //  counter blocks from J0 go through AES-NI side by side -- one more
    //  than a short box needs costs no time beside the rounds' latency --,
    //  the hash takes header, box and lengths against H^(n+2) ... H^1 with
    //  one reduction, and nothing is written but the box and the tag. The
    //  round keys come with movdqu: the schedule is the caller's, and a
    //  legacy memory operand would have to be aligned.
    ASM_LOCAL_FUNC(waterlink_seal_small)
    WATERLINK_SMALL_COUNTERS
    WATERLINK_SMALL_AES
    WATERLINK_SMALL_HASH_START
    //  used, as a byte in every lane, against each block's byte offsets:
    //  what is past it is sealed as zeros, as the general body zeroes it.
    "movd %edx, %xmm13\n   pxor %xmm12, %xmm12\n   pshufb %xmm12, %xmm13\n"
    "lea .Lwaterlink_small_index(%rip), %r9\n"
    WATERLINK_SMALL_SEAL_BLOCK("%xmm1", "16", "0")
    "cmp $16, %rcx\n   je 1f\n"
    WATERLINK_SMALL_SEAL_BLOCK("%xmm2", "32", "16")
    "cmp $32, %rcx\n   je 1f\n"
    WATERLINK_SMALL_SEAL_BLOCK("%xmm3", "48", "32")
    "cmp $48, %rcx\n   je 1f\n"
    WATERLINK_SMALL_SEAL_BLOCK("%xmm4", "64", "48")
    "1:\n"
    WATERLINK_SMALL_HASH_END("%rcx")
    "movdqu %xmm9, 16(%rsi,%rcx)\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    ASM_LOCAL_END(waterlink_seal_small)

    //  Opening hashes the box as it came, beside the rounds, so the tag is
    //  known as soon as the key stream is; the text goes back only when all
    //  sixteen bytes of hash, mask and tag agree, and a box that does not
    //  open is wiped instead.
    ASM_LOCAL_FUNC(waterlink_open_small)
    WATERLINK_SMALL_COUNTERS
    WATERLINK_SMALL_AES
    WATERLINK_SMALL_HASH_START_OPEN
    WATERLINK_SMALL_OPEN_BLOCK("16")
    "cmp $16, %rdx\n   je 1f\n"
    WATERLINK_SMALL_OPEN_BLOCK("32")
    "cmp $32, %rdx\n   je 1f\n"
    WATERLINK_SMALL_OPEN_BLOCK("48")
    "cmp $48, %rdx\n   je 1f\n"
    WATERLINK_SMALL_OPEN_BLOCK("64")
    "1:\n"
    WATERLINK_SMALL_HASH_END("%rdx")
    "movdqu 16(%rsi,%rdx), %xmm6\n   pxor %xmm6, %xmm9\n"
    "pxor %xmm12, %xmm12\n   pcmpeqb %xmm9, %xmm12\n   pmovmskb %xmm12, %eax\n"
    "cmp $0xffff, %eax\n   jne 3f\n"
    "movdqu 16(%rsi), %xmm6\n   pxor %xmm6, %xmm1\n   movdqu %xmm1, 16(%rsi)\n"
    "cmp $16, %rdx\n   je 2f\n"
    "movdqu 32(%rsi), %xmm6\n   pxor %xmm6, %xmm2\n   movdqu %xmm2, 32(%rsi)\n"
    "cmp $32, %rdx\n   je 2f\n"
    "movdqu 48(%rsi), %xmm6\n   pxor %xmm6, %xmm3\n   movdqu %xmm3, 48(%rsi)\n"
    "cmp $48, %rdx\n   je 2f\n"
    "movdqu 64(%rsi), %xmm6\n   pxor %xmm6, %xmm4\n   movdqu %xmm4, 64(%rsi)\n"
    "2:  mov $1, %eax\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    "3:  pxor %xmm6, %xmm6\n   xor %eax, %eax\n"
    "4:  movdqu %xmm6, 16(%rsi,%rax)\n   add $16, %rax\n   cmp %rdx, %rax\n   jb 4b\n"
    "xor %eax, %eax\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    ASM_LOCAL_END(waterlink_open_small)

    ".pushsection .rodata\n   .balign 64\n"
    ".Lwaterlink_steps:\n   .long 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0\n"
    ".Lwaterlink_bswap:\n   .byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0\n"
    ".Lwaterlink_four:\n   .long 4, 0, 0, 0\n"
    ".Lwaterlink_poly:\n   .quad 0xc200000000000000, 0\n"
    //  GCM's lengths block for a whole datagram: 128 bits of header, 9,344
    //  of box.
    ".Lwaterlink_lengths:\n   .byte 0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0x24, 0x80\n"
    ".balign 16\n"
    ".Lwaterlink_small_one:\n   .long 0, 0, 0, 0x01000000\n"
    ".Lwaterlink_small_lengths:\n   .quad 0, 0x80\n"
    ".Lwaterlink_small_index:\n"
    ".byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15\n"
    ".byte 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31\n"
    ".byte 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47\n"
    ".byte 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63\n"
    ".popsection\n"
    ASM_END(waterlink_seal_box)
    ASM_FUNC(waterlink_open_box)
    "cmp $64, %rdx\n   ja .Lwaterlink_open_box_general\n"
    "cmpb $0, cpu_has_aes(%rip)\n   je .Lwaterlink_open_box_general\n"
    "cmpb $0, cpu_has_pclmul(%rip)\n   je .Lwaterlink_open_box_general\n"
    "jmp waterlink_open_small\n"
    ".Lwaterlink_open_box_general:\n"
    "push %rbx\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n"
    "sub $64, %rsp\n"
    "mov %rdi, %rbx\n   mov %rsi, %r12\n   mov %rdx, %r13\n"
    "lea 16(%rsi,%rdx), %r14\n"
    "cmp $1168, %r13\n   jne 2f\n"
    "cmpb $0, cpu_has_vaes(%rip)\n   je 2f\n"
    "cmpb $0, cpu_has_vpclmul(%rip)\n   je 2f\n"
    "cmpb $0, cpu_has_avx512(%rip)\n   je 2f\n"
    "mov %rbx, %rdi\n   mov %r12, %rsi\n   lea 32(%rsp), %rcx\n"
    "call .Lwaterlink_open_zmm\n"
    "mov 32(%rsp), %rax\n   xor (%r14), %rax\n   mov 40(%rsp), %rdx\n   xor 8(%r14), %rdx\n"
    "or %rdx, %rax\n   jmp 3f\n"
    "2:\n"
    //  The tag that came to 48(%rsp), the lengths in its slot and the zero
    //  state at 32(%rsp), each in one store; J0 at 0(%rsp) while they land.
    "movdqu (%r14), %xmm0\n   movdqu %xmm0, 48(%rsp)\n"
    "movabs $0x8000000000000000, %rax\n   mov %r13, %rdx\n   shl $3, %rdx\n   bswap %rdx\n"
    "movq %rax, %xmm0\n   movq %rdx, %xmm1\n   punpcklqdq %xmm1, %xmm0\n   movdqu %xmm0, (%r14)\n"
    "pxor %xmm0, %xmm0\n   movdqu %xmm0, 32(%rsp)\n"
    "mov 8(%r12), %r15\n   mov %r15, %rax\n   shl $32, %rax\n   mov %r15, %rdx\n   shr $32, %rdx\n"
    "mov $0x01000000, %ecx\n   shl $32, %rcx\n   or %rcx, %rdx\n"
    "movq %rax, %xmm0\n   movq %rdx, %xmm1\n   punpcklqdq %xmm1, %xmm0\n   movdqu %xmm0, (%rsp)\n"
    "lea 32(%rsp), %rdi\n   lea 192(%rbx), %rsi\n   mov %r12, %rdx\n"
    "mov %r13, %rcx\n   shr $4, %rcx\n   add $2, %rcx\n"
    "call ghash_blocks\n"
    "movdqu 48(%rsp), %xmm0\n   movdqu %xmm0, (%r14)\n"
    //  The run over header and box: the header comes out xored with the
    //  mask, and goes back.
    "movdqu (%r12), %xmm2\n   movdqu %xmm2, 16(%rsp)\n"
    "mov %rbx, %rdi\n   mov %rsp, %rsi\n   mov %r12, %rdx\n   mov %r12, %rcx\n"
    "mov %r13, %r8\n   shr $4, %r8\n   inc %r8\n"
    "call aes128_ctr_blocks\n"
    //  Hash, mask and tag xored together are zero in every bit, or not.
    "movdqu 16(%rsp), %xmm2\n   movdqu (%r12), %xmm0\n   pxor %xmm2, %xmm0\n"
    "movdqu %xmm2, (%r12)\n"
    "movdqu 32(%rsp), %xmm1\n   pxor %xmm1, %xmm0\n   movdqu 48(%rsp), %xmm1\n   pxor %xmm1, %xmm0\n"
    "movq %xmm0, %rax\n   psrldq $8, %xmm0\n   movq %xmm0, %rdx\n   or %rdx, %rax\n"
    "3:  mov %rax, %r15\n   test %rax, %rax\n   jz 1f\n"
    "lea 16(%r12), %rdi\n   mov %r13, %rsi\n   call memory_zero\n"
    "1:  pxor %xmm0, %xmm0\n   pxor %xmm1, %xmm1\n   pxor %xmm2, %xmm2\n"
    "movdqu %xmm0, (%rsp)\n   movdqu %xmm0, 16(%rsp)\n   movdqu %xmm0, 32(%rsp)\n   movdqu %xmm0, 48(%rsp)\n"
    "xor %eax, %eax\n   test %r15, %r15\n   sete %al\n"
    "add $64, %rsp\n"
    "pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbx\n"
    ASM_RET
    ASM_END(waterlink_open_box)
);
#elif ARM64
/*
        The small bodies on arm64, the x86 ones' shape: v0 to v4 the key
        stream from J0, v16 to v26 the round keys; then the hash, blocks
        in rev64's order against the table's high-word-first powers, into
        v17 (the pmull2 half), v18 (the pmull half) and v19 (the middle),
        with v30 zero and v31 0xc2 << 56 for the fold.
*/
#define WATERLINK_SMALL_ROUND(key)                                            \
    "aese v0.16b, " key ".16b\n   aesmc v0.16b, v0.16b\n"                     \
    "aese v1.16b, " key ".16b\n   aesmc v1.16b, v1.16b\n"                     \
    "aese v2.16b, " key ".16b\n   aesmc v2.16b, v2.16b\n"                     \
    "aese v3.16b, " key ".16b\n   aesmc v3.16b, v3.16b\n"                     \
    "aese v4.16b, " key ".16b\n   aesmc v4.16b, v4.16b\n"
//  J0 is four zero bytes, the header's counter word and 00 00 00 01; the
//  counter word is all it needs of the header.
#define WATERLINK_SMALL_STREAM                                                \
    "ldr d29, [x1, #8]\n   movi v30.16b, #0\n"                                \
    "ext v29.16b, v30.16b, v29.16b, #12\n"                                    \
    "movz w10, #0x0100, lsl #16\n   mov v0.16b, v29.16b\n   mov v0.s[3], w10\n" \
    "movz w10, #0x0200, lsl #16\n   mov v1.16b, v29.16b\n   mov v1.s[3], w10\n" \
    "movz w10, #0x0300, lsl #16\n   mov v2.16b, v29.16b\n   mov v2.s[3], w10\n" \
    "movz w10, #0x0400, lsl #16\n   mov v3.16b, v29.16b\n   mov v3.s[3], w10\n" \
    "movz w10, #0x0500, lsl #16\n   mov v4.16b, v29.16b\n   mov v4.s[3], w10\n" \
    "ldp q16, q17, [x0]\n   ldp q18, q19, [x0, #32]\n   ldp q20, q21, [x0, #64]\n" \
    "ldp q22, q23, [x0, #96]\n   ldp q24, q25, [x0, #128]\n   ldr q26, [x0, #160]\n" \
    WATERLINK_SMALL_ROUND("v16") WATERLINK_SMALL_ROUND("v17")                 \
    WATERLINK_SMALL_ROUND("v18") WATERLINK_SMALL_ROUND("v19")                 \
    WATERLINK_SMALL_ROUND("v20") WATERLINK_SMALL_ROUND("v21")                 \
    WATERLINK_SMALL_ROUND("v22") WATERLINK_SMALL_ROUND("v23")                 \
    WATERLINK_SMALL_ROUND("v24")                                              \
    "aese v0.16b, v25.16b\n   aese v1.16b, v25.16b\n   aese v2.16b, v25.16b\n" \
    "aese v3.16b, v25.16b\n   aese v4.16b, v25.16b\n"                         \
    "eor v0.16b, v0.16b, v26.16b\n   eor v1.16b, v1.16b, v26.16b\n"           \
    "eor v2.16b, v2.16b, v26.16b\n   eor v3.16b, v3.16b, v26.16b\n"           \
    "eor v4.16b, v4.16b, v26.16b\n"
#define WATERLINK_SMALL_PRODUCT(x)                                            \
    "ldr q23, [x9]\n"                                                         \
    "pmull2 v20.1q, " x ".2d, v23.2d\n   eor v17.16b, v17.16b, v20.16b\n"     \
    "pmull v20.1q, " x ".1d, v23.1d\n   eor v18.16b, v18.16b, v20.16b\n"      \
    "ext v21.16b, " x ".16b, " x ".16b, #8\n   eor v21.16b, v21.16b, " x ".16b\n" \
    "ldr q23, [x9, #768]\n   add x9, x9, #16\n"                               \
    "pmull v20.1q, v21.1d, v23.1d\n   eor v19.16b, v19.16b, v20.16b\n"
//  The header against H^(n+2), from 768 - 16 (n + 2) in the table at 192.
#define WATERLINK_SMALL_HASH_BEGIN(box)                                       \
    "movz x9, #0xc200, lsl #48\n   fmov d31, x9\n"                            \
    "movi v17.16b, #0\n   movi v18.16b, #0\n   movi v19.16b, #0\n"            \
    "add x9, x0, #928\n   sub x9, x9, " box "\n"                              \
    "ldp d27, d28, [x1]\n   mov v27.d[1], v28.d[0]\n   rev64 v27.16b, v27.16b\n" \
    WATERLINK_SMALL_PRODUCT("v27")
//  The lengths block against H^1, the fold, and hash with E(J0) in v22.
#define WATERLINK_SMALL_HASH_END(box)                                         \
    "lsl x10, " box ", #3\n   movz x11, #0x80\n   fmov d24, x11\n"            \
    "mov v24.d[1], x10\n"                                                     \
    WATERLINK_SMALL_PRODUCT("v24")                                            \
    "eor v19.16b, v19.16b, v17.16b\n   eor v19.16b, v19.16b, v18.16b\n"       \
    "ext v20.16b, v30.16b, v19.16b, #8\n   eor v17.16b, v17.16b, v20.16b\n"  \
    "ext v20.16b, v19.16b, v30.16b, #8\n   eor v18.16b, v18.16b, v20.16b\n"  \
    "pmull v20.1q, v17.1d, v31.1d\n   ext v17.16b, v17.16b, v17.16b, #8\n"   \
    "eor v17.16b, v17.16b, v20.16b\n"                                         \
    "pmull v20.1q, v17.1d, v31.1d\n   ext v17.16b, v17.16b, v17.16b, #8\n"   \
    "eor v18.16b, v18.16b, v20.16b\n   eor v18.16b, v18.16b, v17.16b\n"       \
    "ext v22.16b, v18.16b, v18.16b, #8\n   rev64 v22.16b, v22.16b\n"          \
    "eor v22.16b, v22.16b, v0.16b\n"
//  A block sealed: the text to used (v28, in every lane) against the
//  block's byte offsets from x10, xored with its key stream, stored and
//  hashed.
#define WATERLINK_SMALL_SEAL_BLOCK(n, at, index)                              \
    "ldr q24, [x1, #" at "]\n   ldr q25, [x10, #" index "]\n"                 \
    "cmhi v25.16b, v28.16b, v25.16b\n   and v24.16b, v24.16b, v25.16b\n"      \
    "eor v" n ".16b, v" n ".16b, v24.16b\n   str q" n ", [x1, #" at "]\n"     \
    "rev64 v24.16b, v" n ".16b\n"                                            \
    WATERLINK_SMALL_PRODUCT("v24")
//  A block as it came, hashed.
#define WATERLINK_SMALL_OPEN_BLOCK(at)                                        \
    "ldr q24, [x1, #" at "]\n   rev64 v24.16b, v24.16b\n"                    \
    WATERLINK_SMALL_PRODUCT("v24")
#define WATERLINK_SMALL_WIPE                                                  \
    "movi v0.16b, #0\n   movi v1.16b, #0\n   movi v2.16b, #0\n   movi v3.16b, #0\n"  \
    "movi v4.16b, #0\n   movi v16.16b, #0\n   movi v17.16b, #0\n   movi v18.16b, #0\n" \
    "movi v19.16b, #0\n   movi v20.16b, #0\n   movi v21.16b, #0\n   movi v22.16b, #0\n" \
    "movi v23.16b, #0\n   movi v24.16b, #0\n   movi v25.16b, #0\n   movi v26.16b, #0\n" \
    "movi v27.16b, #0\n   movi v28.16b, #0\n   movi v29.16b, #0\n"

__asm__(
    ASM_FUNC(waterlink_seal_box)
    "cmp x3, #64\n   b.hi .Lwaterlink_seal_box_general\n"
    "adrp x9, cpu_has_aes\n   ldrb w9, [x9, :lo12:cpu_has_aes]\n"
    "cbz w9, .Lwaterlink_seal_box_general\n"
    "adrp x9, cpu_has_pclmul\n   ldrb w9, [x9, :lo12:cpu_has_pclmul]\n"
    "cbz w9, .Lwaterlink_seal_box_general\n"
    "b waterlink_seal_small\n"
    ".Lwaterlink_seal_box_general:\n"
    "stp x29, x30, [sp, #-112]!\n   mov x29, sp\n"
    "stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n"
    "mov x19, x0\n   mov x20, x1\n   mov x21, x3\n"
    "cmp x2, x3\n   b.hs 1f\n"
    "add x0, x1, #16\n   add x0, x0, x2\n   sub x1, x3, x2\n   bl memory_zero\n"
    //  The lengths in the tag's slot and the zero state at sp+80 first, as
    //  whole blocks; the header to sp+64 and J0 to sp+48.
    "1:  add x22, x20, #16\n   add x22, x22, x21\n"
    "mov x9, #0x8000000000000000\n   lsl x10, x21, #3\n   rev x10, x10\n"
    "stp x9, x10, [x22]\n   stp xzr, xzr, [sp, #80]\n"
    "ldp x9, x10, [x20]\n   stp x9, x10, [sp, #64]\n"
    "lsl x11, x10, #32\n   lsr x12, x10, #32\n   mov x13, #0x0100000000000000\n"
    "orr x12, x12, x13\n   stp x11, x12, [sp, #48]\n"
    "mov x0, x19\n   add x1, sp, #48\n   mov x2, x20\n   mov x3, x20\n"
    "lsr x4, x21, #4\n   add x4, x4, #1\n   bl aes128_ctr_blocks\n"
    //  The header came out xored with the mask: the mask to sp+96, the
    //  header back.
    "ldp x9, x10, [sp, #64]\n   ldp x11, x12, [x20]\n"
    "eor x11, x11, x9\n   eor x12, x12, x10\n   stp x11, x12, [sp, #96]\n"
    "stp x9, x10, [x20]\n"
    "add x0, sp, #80\n   add x1, x19, #192\n   mov x2, x20\n"
    "lsr x3, x21, #4\n   add x3, x3, #2\n   bl ghash_blocks\n"
    "ldp x9, x10, [sp, #80]\n   ldp x11, x12, [sp, #96]\n"
    "eor x9, x9, x11\n   eor x10, x10, x12\n   stp x9, x10, [x22]\n"
    "stp xzr, xzr, [sp, #48]\n   stp xzr, xzr, [sp, #64]\n"
    "stp xzr, xzr, [sp, #80]\n   stp xzr, xzr, [sp, #96]\n"
    "ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n"
    "ldp x29, x30, [sp], #112\n"
    ASM_RET
    ASM_END(waterlink_seal_box)
    ASM_FUNC(waterlink_open_box)
    "cmp x2, #64\n   b.hi .Lwaterlink_open_box_general\n"
    "adrp x9, cpu_has_aes\n   ldrb w9, [x9, :lo12:cpu_has_aes]\n"
    "cbz w9, .Lwaterlink_open_box_general\n"
    "adrp x9, cpu_has_pclmul\n   ldrb w9, [x9, :lo12:cpu_has_pclmul]\n"
    "cbz w9, .Lwaterlink_open_box_general\n"
    "b waterlink_open_small\n"
    ".Lwaterlink_open_box_general:\n"
    "stp x29, x30, [sp, #-112]!\n   mov x29, sp\n"
    "stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n"
    "mov x19, x0\n   mov x20, x1\n   mov x21, x2\n"
    "add x22, x1, #16\n   add x22, x22, x2\n"
    //  The tag that came to sp+96, the lengths in its slot, the zero state
    //  at sp+80, the header at sp+64 and J0 at sp+48.
    "ldp x9, x10, [x22]\n   stp x9, x10, [sp, #96]\n"
    "mov x9, #0x8000000000000000\n   lsl x10, x21, #3\n   rev x10, x10\n"
    "stp x9, x10, [x22]\n   stp xzr, xzr, [sp, #80]\n"
    "ldp x9, x10, [x20]\n   stp x9, x10, [sp, #64]\n"
    "lsl x11, x10, #32\n   lsr x12, x10, #32\n   mov x13, #0x0100000000000000\n"
    "orr x12, x12, x13\n   stp x11, x12, [sp, #48]\n"
    "add x0, sp, #80\n   add x1, x19, #192\n   mov x2, x20\n"
    "lsr x3, x21, #4\n   add x3, x3, #2\n   bl ghash_blocks\n"
    "ldp x9, x10, [sp, #96]\n   stp x9, x10, [x22]\n"
    "mov x0, x19\n   add x1, sp, #48\n   mov x2, x20\n   mov x3, x20\n"
    "lsr x4, x21, #4\n   add x4, x4, #1\n   bl aes128_ctr_blocks\n"
    //  The header back; header, what came out in its place, hash and tag
    //  xored together are zero in every bit, or not.
    "ldp x9, x10, [sp, #64]\n   ldp x11, x12, [x20]\n   stp x9, x10, [x20]\n"
    "eor x11, x11, x9\n   eor x12, x12, x10\n"
    "ldp x13, x14, [sp, #80]\n   eor x11, x11, x13\n   eor x12, x12, x14\n"
    "ldp x13, x14, [sp, #96]\n   eor x11, x11, x13\n   eor x12, x12, x14\n"
    "orr x22, x11, x12\n"
    "cbz x22, 1f\n"
    "add x0, x20, #16\n   mov x1, x21\n   bl memory_zero\n"
    "1:  stp xzr, xzr, [sp, #48]\n   stp xzr, xzr, [sp, #64]\n"
    "stp xzr, xzr, [sp, #80]\n   stp xzr, xzr, [sp, #96]\n"
    "cmp x22, #0\n   cset w0, eq\n"
    "ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n"
    "ldp x29, x30, [sp], #112\n"
    ASM_RET
    ASM_END(waterlink_open_box)

    //  x0 the key, x1 the datagram, x2 used, x3 the box.
    ASM_LOCAL_FUNC(waterlink_seal_small)
    ".arch_extension crypto\n"
    WATERLINK_SMALL_STREAM
    WATERLINK_SMALL_HASH_BEGIN("x3")
    "dup v28.16b, w2\n"
    "adrp x10, .Lwaterlink_small_index\n"
    "add x10, x10, :lo12:.Lwaterlink_small_index\n"
    WATERLINK_SMALL_SEAL_BLOCK("1", "16", "0")
    "cmp x3, #16\n   b.eq 1f\n"
    WATERLINK_SMALL_SEAL_BLOCK("2", "32", "16")
    "cmp x3, #32\n   b.eq 1f\n"
    WATERLINK_SMALL_SEAL_BLOCK("3", "48", "32")
    "cmp x3, #48\n   b.eq 1f\n"
    WATERLINK_SMALL_SEAL_BLOCK("4", "64", "48")
    "1:\n"
    WATERLINK_SMALL_HASH_END("x3")
    "add x10, x1, #16\n   str q22, [x10, x3]\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    ".arch_extension nocrypto\n"
    ASM_LOCAL_END(waterlink_seal_small)

    //  x0 the key, x1 the datagram, x2 the box.
    ASM_LOCAL_FUNC(waterlink_open_small)
    ".arch_extension crypto\n"
    WATERLINK_SMALL_STREAM
    WATERLINK_SMALL_HASH_BEGIN("x2")
    WATERLINK_SMALL_OPEN_BLOCK("16")
    "cmp x2, #16\n   b.eq 1f\n"
    WATERLINK_SMALL_OPEN_BLOCK("32")
    "cmp x2, #32\n   b.eq 1f\n"
    WATERLINK_SMALL_OPEN_BLOCK("48")
    "cmp x2, #48\n   b.eq 1f\n"
    WATERLINK_SMALL_OPEN_BLOCK("64")
    "1:\n"
    WATERLINK_SMALL_HASH_END("x2")
    "add x10, x1, #16\n   ldr q24, [x10, x2]\n   eor v22.16b, v22.16b, v24.16b\n"
    "mov x10, v22.d[0]\n   mov x11, v22.d[1]\n   orr x10, x10, x11\n"
    "cbnz x10, 3f\n"
    "ldr q24, [x1, #16]\n   eor v1.16b, v1.16b, v24.16b\n   str q1, [x1, #16]\n"
    "cmp x2, #16\n   b.eq 2f\n"
    "ldr q24, [x1, #32]\n   eor v2.16b, v2.16b, v24.16b\n   str q2, [x1, #32]\n"
    "cmp x2, #32\n   b.eq 2f\n"
    "ldr q24, [x1, #48]\n   eor v3.16b, v3.16b, v24.16b\n   str q3, [x1, #48]\n"
    "cmp x2, #48\n   b.eq 2f\n"
    "ldr q24, [x1, #64]\n   eor v4.16b, v4.16b, v24.16b\n   str q4, [x1, #64]\n"
    "2:  mov w0, #1\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    "3:  add x10, x1, #16\n   add x11, x10, x2\n"
    "4:  stp xzr, xzr, [x10], #16\n   cmp x10, x11\n   b.lo 4b\n"
    "mov w0, #0\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    ".arch_extension nocrypto\n"
    ASM_LOCAL_END(waterlink_open_small)

    ".pushsection .rodata\n   .balign 16\n"
    ".Lwaterlink_small_index:\n"
    ".byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15\n"
    ".byte 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31\n"
    ".byte 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47\n"
    ".byte 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63\n"
    ".popsection\n"
);
#elif RISCV64
/*
        The small bodies on riscv64, where AES is Zvkned's and the
        carry-less multiply is Zbc's, so they are asked for together as on
        the other two. The datagram comes in as bytes to v16 -- nothing says
        it is aligned -- and the counter blocks from J0 go through the
        rounds in v8, a block to an element group, the round keys in v0 to
        v7 and v24 to v26. Every vector operand is a group of eight at vl
        and no more, so one shape serves every VLEN: a group holds eight
        blocks at the least, the bodies need six at the most, and nothing
        here assumes a block a register.
        The hash is scalar, where the multiply is: a gather puts each
        block's bytes in reverse in v0, its words come off the front of it
        one at a time, and their Karatsuba products against H^(n+2) ... H^1
        from the table a0 points at sum into t0/t1 (low), t2/t3 (high) and
        t4/t5 (middle), folded once at the end.
*/
//  t5 bytes of the datagram, header first, to v16, and a counter block for
//  each sixteen of them from J0 through the rounds in v8. J0 is four zero
//  bytes, the header's counter word and 00 00 00 01: a gather takes the
//  header's words two and three into lanes one and two of every block --
//  an index no VLMAX reaches reads zero -- and a table adds the counters.
//  The round keys are read sixteen bytes each, as lib.c's Zvkned body
//  reads them, rather than a register's worth past the schedule.
#define WATERLINK_SMALL_STREAM                                                \
    "vsetvli zero, t5, e8, m8, ta, mu\n   vle8.v v16, (a1)\n"                 \
    "srli t6, t5, 2\n   vsetvli zero, t6, e32, m8, ta, mu\n"                  \
    "lla t4, .Lwaterlink_small_j0\n   vle32.v v24, (t4)\n"                    \
    "addi t4, t4, 96\n   vle32.v v0, (t4)\n"                                  \
    "vrgather.vv v8, v16, v24\n   vor.vv v8, v8, v0\n"                        \
    "vsetivli zero, 16, e8, m1, ta, ma\n   vle8.v v0, (a0)\n"                 \
    "addi t4, a0, 16\n   vle8.v v1, (t4)\n   addi t4, a0, 32\n   vle8.v v2, (t4)\n" \
    "addi t4, a0, 48\n   vle8.v v3, (t4)\n   addi t4, a0, 64\n   vle8.v v4, (t4)\n" \
    "addi t4, a0, 80\n   vle8.v v5, (t4)\n   addi t4, a0, 96\n   vle8.v v6, (t4)\n" \
    "addi t4, a0, 112\n   vle8.v v7, (t4)\n   addi t4, a0, 128\n   vle8.v v24, (t4)\n" \
    "addi t4, a0, 144\n   vle8.v v25, (t4)\n   addi t4, a0, 160\n   vle8.v v26, (t4)\n" \
    "vsetvli zero, t6, e32, m8, ta, mu\n"                                     \
    "vaesz.vs v8, v0\n   vaesem.vs v8, v1\n   vaesem.vs v8, v2\n"             \
    "vaesem.vs v8, v3\n   vaesem.vs v8, v4\n   vaesem.vs v8, v5\n"            \
    "vaesem.vs v8, v6\n   vaesem.vs v8, v7\n   vaesem.vs v8, v24\n"           \
    "vaesem.vs v8, v25\n   vaesef.vs v8, v26\n"
//  The next block's two words off the front of v0, low then high: each
//  slide moves every word of the group down one, vl being all of them.
#define WATERLINK_SMALL_WORDS                                                 \
    "vslidedown.vi v0, v0, 1\n   vmv.x.s a4, v0\n"                            \
    "vslidedown.vi v0, v0, 1\n   vmv.x.s a5, v0\n"
//  Its three products against the power at lo(a0), hi(a0) and its words'
//  sum at mid(a0), summed.
#define WATERLINK_SMALL_PRODUCT(lo, hi, mid)                                  \
    "ld a7, " lo "(a0)\n   clmul a6, a4, a7\n   xor t0, t0, a6\n"             \
    "clmulh a6, a4, a7\n   xor t1, t1, a6\n"                                  \
    "ld a7, " hi "(a0)\n   clmul a6, a5, a7\n   xor t2, t2, a6\n"             \
    "clmulh a6, a5, a7\n   xor t3, t3, a6\n"                                  \
    "xor a2, a4, a5\n   ld a7, " mid "(a0)\n   clmul a6, a2, a7\n"            \
    "xor t4, t4, a6\n   clmulh a6, a2, a7\n   xor t5, t5, a6\n"
//  The header against H^(n+2), from 768 - 16 (n + 2) in the table at 192,
//  then the box's blocks against the powers after it.
#define WATERLINK_SMALL_HASH_START                                            \
    "addi a0, a0, 928\n   sub a0, a0, a3\n"                                   \
    "srli t6, t5, 3\n   vsetvli zero, t6, e64, m8, ta, ma\n"                  \
    "vmv.x.s a4, v0\n   vslidedown.vi v0, v0, 1\n   vmv.x.s a5, v0\n"         \
    "ld a7, 0(a0)\n   clmul t0, a4, a7\n   clmulh t1, a4, a7\n"               \
    "ld a7, 8(a0)\n   clmul t2, a5, a7\n   clmulh t3, a5, a7\n"               \
    "xor a2, a4, a5\n   ld a7, 768(a0)\n"                                     \
    "clmul t4, a2, a7\n   clmulh t5, a2, a7\n"                                \
    WATERLINK_SMALL_WORDS WATERLINK_SMALL_PRODUCT("16", "24", "784")          \
    "li t6, 16\n   beq a3, t6, 1f\n"                                          \
    WATERLINK_SMALL_WORDS WATERLINK_SMALL_PRODUCT("32", "40", "800")          \
    "li t6, 32\n   beq a3, t6, 1f\n"                                          \
    WATERLINK_SMALL_WORDS WATERLINK_SMALL_PRODUCT("48", "56", "816")          \
    "li t6, 48\n   beq a3, t6, 1f\n"                                          \
    WATERLINK_SMALL_WORDS WATERLINK_SMALL_PRODUCT("64", "72", "832")          \
    "1:\n"
//  The lengths block against H^1 -- 128 bits of header, the box's bits of
//  text, so its high word's products are shifts -- then the fold, as
//  lib.c's Zbc body folds: the hash's high word in t3, its low in t2.
#define WATERLINK_SMALL_HASH_END                                              \
    "add t6, a0, a3\n   slli a4, a3, 3\n"                                     \
    "ld a7, 16(t6)\n   clmul a6, a4, a7\n   xor t0, t0, a6\n"                 \
    "clmulh a6, a4, a7\n   xor t1, t1, a6\n"                                  \
    "ld a7, 24(t6)\n   slli a6, a7, 7\n   xor t2, t2, a6\n"                   \
    "srli a6, a7, 57\n   xor t3, t3, a6\n"                                    \
    "xori a2, a4, 0x80\n   ld a7, 784(t6)\n   clmul a6, a2, a7\n"             \
    "xor t4, t4, a6\n   clmulh a6, a2, a7\n   xor t5, t5, a6\n"               \
    "xor a4, t4, t0\n   xor a4, a4, t2\n   xor a5, t5, t1\n   xor a5, a5, t3\n" \
    "xor t1, t1, a4\n   xor t2, t2, a5\n"                                     \
    "li a7, 0xc2\n   slli a7, a7, 56\n"                                       \
    "clmul a4, t0, a7\n   clmulh a5, t0, a7\n   xor t1, t1, a4\n"             \
    "xor t2, t2, t0\n   xor t2, t2, a5\n"                                     \
    "clmul a4, t1, a7\n   clmulh a5, t1, a7\n   xor t2, t2, a4\n"             \
    "xor t3, t3, t1\n   xor t3, t3, a5\n"
//  Key stream, text and hash leave no register behind.
#define WATERLINK_SMALL_WIPE                                                  \
    "vsetvli t6, zero, e8, m8, ta, ma\n"                                      \
    "vmv.v.i v0, 0\n   vmv.v.i v8, 0\n   vmv.v.i v16, 0\n   vmv.v.i v24, 0\n" \
    "li t0, 0\n   li t1, 0\n   li t2, 0\n   li t3, 0\n   li t4, 0\n"          \
    "li t5, 0\n   li a4, 0\n   li a5, 0\n   li a6, 0\n"

__asm__(
    ASM_FUNC(waterlink_seal_box)
    "li t0, 64\n   bgtu a3, t0, .Lwaterlink_seal_box_general\n"
    "lla t0, cpu_has_aes\n   lbu t0, 0(t0)\n"
    "lla t1, cpu_has_pclmul\n   lbu t1, 0(t1)\n"
    "and t0, t0, t1\n   beqz t0, .Lwaterlink_seal_box_general\n"
    "tail waterlink_seal_small\n"
    ".Lwaterlink_seal_box_general:\n"
    "addi sp, sp, -128\n   sd ra, 120(sp)\n   sd s0, 112(sp)\n   sd s1, 104(sp)\n"
    "sd s2, 96(sp)\n   sd s3, 88(sp)\n"
    "mv s0, a0\n   mv s1, a1\n   mv s2, a3\n"
    "bgeu a2, a3, 1f\n"
    "addi a0, a1, 16\n   add a0, a0, a2\n   sub a1, a3, a2\n   call memory_zero\n"
    //  The lengths in the tag's slot -- (box * 8) big endian in its last
    //  three bytes, with no byte reverse to hand -- and the zero state at
    //  sp+32; the header to sp+16 and J0 to sp+0.
    "1:  add s3, s1, s2\n   addi s3, s3, 16\n"
    "li t0, 1\n   slli t0, t0, 63\n   sd t0, 0(s3)\n"
    "slli t0, s2, 3\n   srli t1, t0, 16\n   andi t1, t1, 255\n   slli t1, t1, 40\n"
    "srli t2, t0, 8\n   andi t2, t2, 255\n   slli t2, t2, 48\n   or t1, t1, t2\n"
    "andi t2, t0, 255\n   slli t2, t2, 56\n   or t1, t1, t2\n   sd t1, 8(s3)\n"
    "sd zero, 32(sp)\n   sd zero, 40(sp)\n"
    "ld t0, 0(s1)\n   ld t1, 8(s1)\n   sd t0, 16(sp)\n   sd t1, 24(sp)\n"
    "slli t0, t1, 32\n   srli t1, t1, 32\n   li t2, 1\n   slli t2, t2, 56\n"
    "or t1, t1, t2\n   sd t0, 0(sp)\n   sd t1, 8(sp)\n"
    "mv a0, s0\n   mv a1, sp\n   mv a2, s1\n   mv a3, s1\n"
    "srli a4, s2, 4\n   addi a4, a4, 1\n   call aes128_ctr_blocks\n"
    //  The header came out xored with the mask: the mask to sp+48, the
    //  header back.
    "ld t0, 16(sp)\n   ld t1, 24(sp)\n   ld t2, 0(s1)\n   ld t3, 8(s1)\n"
    "xor t2, t2, t0\n   xor t3, t3, t1\n   sd t2, 48(sp)\n   sd t3, 56(sp)\n"
    "sd t0, 0(s1)\n   sd t1, 8(s1)\n"
    "addi a0, sp, 32\n   addi a1, s0, 192\n   mv a2, s1\n"
    "srli a3, s2, 4\n   addi a3, a3, 2\n   call ghash_blocks\n"
    "ld t0, 32(sp)\n   ld t1, 48(sp)\n   xor t0, t0, t1\n   sd t0, 0(s3)\n"
    "ld t0, 40(sp)\n   ld t1, 56(sp)\n   xor t0, t0, t1\n   sd t0, 8(s3)\n"
    "sd zero, 0(sp)\n   sd zero, 8(sp)\n   sd zero, 16(sp)\n   sd zero, 24(sp)\n"
    "sd zero, 32(sp)\n   sd zero, 40(sp)\n   sd zero, 48(sp)\n   sd zero, 56(sp)\n"
    "ld ra, 120(sp)\n   ld s0, 112(sp)\n   ld s1, 104(sp)\n"
    "ld s2, 96(sp)\n   ld s3, 88(sp)\n   addi sp, sp, 128\n"
    ASM_RET
    ASM_END(waterlink_seal_box)
    ASM_FUNC(waterlink_open_box)
    "li t0, 64\n   bgtu a2, t0, .Lwaterlink_open_box_general\n"
    "lla t0, cpu_has_aes\n   lbu t0, 0(t0)\n"
    "lla t1, cpu_has_pclmul\n   lbu t1, 0(t1)\n"
    "and t0, t0, t1\n   beqz t0, .Lwaterlink_open_box_general\n"
    "tail waterlink_open_small\n"
    ".Lwaterlink_open_box_general:\n"
    "addi sp, sp, -128\n   sd ra, 120(sp)\n   sd s0, 112(sp)\n   sd s1, 104(sp)\n"
    "sd s2, 96(sp)\n   sd s3, 88(sp)\n"
    "mv s0, a0\n   mv s1, a1\n   mv s2, a2\n"
    "add s3, a1, a2\n   addi s3, s3, 16\n"
    //  The tag that came to sp+48, the lengths in its slot, the zero state
    //  at sp+32, the header at sp+16 and J0 at sp+0.
    "ld t0, 0(s3)\n   ld t1, 8(s3)\n   sd t0, 48(sp)\n   sd t1, 56(sp)\n"
    "li t0, 1\n   slli t0, t0, 63\n   sd t0, 0(s3)\n"
    "slli t0, s2, 3\n   srli t1, t0, 16\n   andi t1, t1, 255\n   slli t1, t1, 40\n"
    "srli t2, t0, 8\n   andi t2, t2, 255\n   slli t2, t2, 48\n   or t1, t1, t2\n"
    "andi t2, t0, 255\n   slli t2, t2, 56\n   or t1, t1, t2\n   sd t1, 8(s3)\n"
    "sd zero, 32(sp)\n   sd zero, 40(sp)\n"
    "ld t0, 0(s1)\n   ld t1, 8(s1)\n   sd t0, 16(sp)\n   sd t1, 24(sp)\n"
    "slli t0, t1, 32\n   srli t1, t1, 32\n   li t2, 1\n   slli t2, t2, 56\n"
    "or t1, t1, t2\n   sd t0, 0(sp)\n   sd t1, 8(sp)\n"
    "addi a0, sp, 32\n   addi a1, s0, 192\n   mv a2, s1\n"
    "srli a3, s2, 4\n   addi a3, a3, 2\n   call ghash_blocks\n"
    "ld t0, 48(sp)\n   ld t1, 56(sp)\n   sd t0, 0(s3)\n   sd t1, 8(s3)\n"
    "mv a0, s0\n   mv a1, sp\n   mv a2, s1\n   mv a3, s1\n"
    "srli a4, s2, 4\n   addi a4, a4, 1\n   call aes128_ctr_blocks\n"
    //  The header back; header, what came out in its place, hash and tag
    //  xored together are zero in every bit, or not.
    "ld t0, 16(sp)\n   ld t1, 24(sp)\n   ld t2, 0(s1)\n   ld t3, 8(s1)\n"
    "sd t0, 0(s1)\n   sd t1, 8(s1)\n   xor t2, t2, t0\n   xor t3, t3, t1\n"
    "ld t0, 32(sp)\n   ld t1, 40(sp)\n   xor t2, t2, t0\n   xor t3, t3, t1\n"
    "ld t0, 48(sp)\n   ld t1, 56(sp)\n   xor t2, t2, t0\n   xor t3, t3, t1\n"
    "or s3, t2, t3\n"
    "beqz s3, 1f\n"
    "addi a0, s1, 16\n   mv a1, s2\n   call memory_zero\n"
    "1:  sd zero, 0(sp)\n   sd zero, 8(sp)\n   sd zero, 16(sp)\n   sd zero, 24(sp)\n"
    "sd zero, 32(sp)\n   sd zero, 40(sp)\n   sd zero, 48(sp)\n   sd zero, 56(sp)\n"
    "seqz a0, s3\n"
    "ld ra, 120(sp)\n   ld s0, 112(sp)\n   ld s1, 104(sp)\n"
    "ld s2, 96(sp)\n   ld s3, 88(sp)\n   addi sp, sp, 128\n"
    ASM_RET
    ASM_END(waterlink_open_box)

    //  a0 the key, a1 the datagram, a2 used, a3 the box: one to four
    //  blocks, in registers from end to end, and nothing written but the
    //  box and the tag.
    ASM_LOCAL_FUNC(waterlink_seal_small)
    ".option push\n   .option arch, +v, +zvkned, +zbc\n"
    "addi t5, a3, 16\n"
    WATERLINK_SMALL_STREAM
    //  The text to used, xored in; past it the key stream stands as it is,
    //  which is sealing zeros, as the general body zeroes them. Lane i - 16
    //  is asked against used and the box, so the header's lanes, where it
    //  wraps past 239, take neither and block 0 keeps E(J0).
    "vsetvli zero, t5, e8, m8, ta, mu\n"
    "vid.v v24\n   vadd.vi v24, v24, -16\n"
    "vmsltu.vx v0, v24, a2\n   vxor.vv v8, v8, v16, v0.t\n"
    "vmsltu.vx v0, v24, a3\n   vse8.v v8, (a1), v0.t\n"
    "vmerge.vvm v16, v16, v8, v0\n"
    //  Header and box, each block's bytes reversed, for the hash.
    "vid.v v24\n   vxor.vi v24, v24, 15\n   vrgather.vv v0, v16, v24\n"
    WATERLINK_SMALL_HASH_START
    WATERLINK_SMALL_HASH_END
    //  The hash back to a block, its bytes reversed, xored with E(J0), and
    //  that is the tag.
    "vsetivli zero, 2, e64, m8, ta, ma\n"
    "vmv.v.x v16, t2\n   vslide1down.vx v0, v16, t3\n"
    "vsetivli zero, 16, e8, m8, ta, ma\n"
    "vrgather.vv v16, v0, v24\n   vxor.vv v16, v16, v8\n"
    "add t6, a1, a3\n   addi t6, t6, 16\n   vse8.v v16, (t6)\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    ".option pop\n"
    ASM_LOCAL_END(waterlink_seal_small)

    //  a0 the key, a1 the datagram, a2 the box. The rounds make one block
    //  more, for the tag's lanes, and E(J0) goes onto the tag that came:
    //  the hash of header and box as they came, each block reversed, is
    //  then that block's two words in reverse, compared whole. The text
    //  goes back only when they agree, and a box that does not open is
    //  wiped instead, through the same store.
    ASM_LOCAL_FUNC(waterlink_open_small)
    ".option push\n   .option arch, +v, +zvkned, +zbc\n"
    "mv a3, a2\n   addi t5, a2, 32\n"
    WATERLINK_SMALL_STREAM
    "vsetvli zero, t5, e8, m8, ta, mu\n"
    "vmv.v.i v0, 0\n   addi t6, a3, 16\n   vslideup.vx v0, v8, t6\n"
    "vxor.vv v16, v16, v0\n"
    "vid.v v24\n   vxor.vi v24, v24, 15\n   vrgather.vv v0, v16, v24\n"
    "vxor.vv v8, v8, v16\n"
    WATERLINK_SMALL_HASH_START
    WATERLINK_SMALL_HASH_END
    WATERLINK_SMALL_WORDS
    "xor a4, a4, t2\n   xor a5, a5, t3\n   or a4, a4, a5\n"
    //  Lanes 16 to 16 + box, from the reversed index, whose high bits are
    //  the lane's.
    "addi t6, a3, 32\n   vsetvli zero, t6, e8, m8, ta, mu\n"
    "vadd.vi v24, v24, -16\n   vmsltu.vx v0, v24, a3\n"
    "beqz a4, 2f\n"
    "vmv.v.i v8, 0\n"
    "2:  vse8.v v8, (a1), v0.t\n   seqz a0, a4\n"
    WATERLINK_SMALL_WIPE
    ASM_RET
    ".option pop\n"
    ASM_LOCAL_END(waterlink_open_small)

    ".pushsection .rodata\n   .balign 16\n"
    //  J0's gather, six blocks of it: the header's words two and three into
    //  lanes one and two, and an index no VLMAX reaches into the others;
    //  then each block's counter, big endian in lane three.
    ".Lwaterlink_small_j0:\n"
    ".rept 6\n   .long 0xffffffff, 2, 3, 0xffffffff\n   .endr\n"
    ".long 0, 0, 0, 0x01000000, 0, 0, 0, 0x02000000\n"
    ".long 0, 0, 0, 0x03000000, 0, 0, 0, 0x04000000\n"
    ".long 0, 0, 0, 0x05000000, 0, 0, 0, 0x06000000\n"
    ".popsection\n"
);
#endif

/*
        The box a datagram goes out with, for used bytes of frames: cut to
        whole blocks, as WireGuard pads -- except a box with no room left for
        another frame, which is padded to the full size. A keystroke is then
        a 48-byte datagram and not a 1200-byte one, and a run of full frames
        is still all one size, which is what segment offload needs.
*/
positive waterlink_box(positive used)
{
        if (used > WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST)
                return WATERLINK_PAYLOAD;
        return used ? (used + 15) & ~(positive)15 : 16;
}

/*
        Seal a datagram whose header is written and whose box holds used
        bytes of frames, and answer its length. The rest of the box is
        zeroed first -- padding is part of what is authenticated, and a box
        that ends in leftover bytes from the last datagram would be sealing
        whatever they were.
*/
positive waterlink_seal(crypto_aesgcm_key address_to key, p8 address_to datagram,
                        positive used)
{
        positive box;

        /*      `used` is normally the result of waterlink_fill, but sealing
                is also an application-facing boundary.  Refuse an impossible
                span before padding arithmetic or the assembly receives it;
                otherwise a wrapped size can turn the padding clear into a
                write outside the datagram. */
        if (used > WATERLINK_PAYLOAD)
                return 0;
        box = waterlink_box(used);

        waterlink_seal_box(key, datagram, used, box);
        return 16 + box + 16;
}

/*
        Open a datagram of length bytes in place. The tag is checked over the
        ciphertext before anything is trusted, compared whole so a wrong tag
        says nothing about where it went wrong, and on failure the box is
        wiped: the counter run has already turned it into plaintext-shaped
        bytes, and unauthenticated plaintext is not something to leave lying
        in a buffer the caller might read.

        Returns false for a tag that does not verify, or a length that is not
        a datagram's. The counter is only trustworthy after this returns true,
        which is why the replay window is asked afterwards and never before.
*/
bool waterlink_open(crypto_aesgcm_key address_to key, p8 address_to datagram,
                    positive length)
{
        if (length < 48 || length > WATERLINK_DATAGRAM || length % 16)
                return false;
        return waterlink_open_box(key, datagram, length - 32);
}

#endif // WATERLINK_SEAL_INCLUDED
