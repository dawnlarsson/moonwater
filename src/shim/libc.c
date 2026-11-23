/*
        libmoonwater-c.so: lib.c's memory and string routines under the C library's own names, for programs of a bowl
        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        A program of a bowl is linked to its distribution's C library, glibc (Arch, Debian, Fedora, Steam's runtime) or musl
        (Alpine), and every memcpy it makes is that library's. Preloaded, this library's names come first: the dynamic loader
        binds the program's memcpy, and every other library's, to the first object in the search order that defines it, and
        a preloaded object is ahead of the C library. Nothing else changes: the C library's own calls inside itself are bound
        when it was linked and stay its own, and every name this library does not define is the C library's as before.

        Assembly only, as lib.c is: this file adds no C body, and what it adds to lib.c's routines is the few instructions
        that make a C library's contract out of one of them (a length test before a copy, a count of wide characters turned
        into bytes). test/libc_shim.c builds the library unstripped and fails if a symbol in its text is not one of lib.c's
        routines or one named here.

        The names (src/shim/libc.map) are the ones whose meaning is the C standard's, POSIX's or glibc's and nothing more, and
        which the C standard keeps for the implementation (str, mem and wmem followed by a lower case letter), so that no
        program can have one meaning something else. Left out, each for its reason:
          - the ctype, case folding and collating names: glibc's and musl's follow setlocale, and lib.c's are the C locale's;
          - strdup, strndup and anything else that allocates: the C library's free would be handed lib.c's pointers;
          - setjmp and longjmp: glibc mangles the pointers in its jmp_buf and keeps the signal mask in it;
          - atoi, strtol and the other number readers: errno, the locale's white space and its digits;
          - the math names: glibc's are already one SSE4.1 instruction where one exists, and its wrappers set errno;
          - zlib's crc32 and adler32: not names the C standard keeps, so a library that is not zlib may export its own.
        strtok's hidden pointer is lib.c's: every call a program makes reaches this one, and the C libraries' own callers use
        their strtok_r.

        memcpy is memmove here (LIBRARY_MEMCPY_OVERLAPS): glibc's x86_64 memcpy has been its memmove since 2.14, and
        memcpy@GLIBC_2.2.5 promises overlap to every binary linked before that, so a program that copies onto itself works
        under glibc and must go on working. The two are one body to 255 bytes; above, memmove's overlap test is the cost.

        The _chk names are glibc's checked copies, which a program built with _FORTIFY_SOURCE calls when it knows how large
        the destination is: the same work after one comparison, and the same end when the destination is too small (the
        message glibc's __chk_fail writes, SIGABRT with its default action restored and unblocked, then exit_group). musl has
        none of them and no program of musl calls one.

        The processor is asked what it has when the library is loaded: moonwater_cpu_detect is an .init_array entry of its
        own, run by the dynamic loader before the program's constructors. A call made before that, from the constructor of a
        library loaded earlier, takes the bodies every x86_64 runs, which are correct and only slower.

        Built into the byte arrays in src/bowl.c with:
          clang -target x86_64-linux-gnu -std=gnu11 -O2 -fPIC -shared -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
                -fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -ffunction-sections -fdata-sections \
                -fuse-ld=lld -Wl,-soname,libmoonwater-c.so,--version-script=src/shim/libc.map,--gc-sections,-z,now,-Bsymbolic \
                -Wl,--hash-style=both,--build-id=none,-s -o libmoonwater-c.so src/shim/libc.c
*/
#if defined(__i386__)
/*
        The i386 build is a library with nothing in it. glibc's preload names /usr/local/$LIB/libmoonwater-c.so, and $LIB is
        lib32 (Arch) or lib/i386-linux-gnu (Debian) in a 32-bit process, which would otherwise say "cannot be preloaded"
        at every start of every 32-bit program (Steam's client, a 32-bit game, Wine's 32-bit side). lib.c has no i386
        bodies, so a 32-bit program keeps its C library's routines whole. Built with:
          clang -target i386-linux-gnu -shared -nostdlib -fuse-ld=lld -Wl,-soname,libmoonwater-c.so,--build-id=none,-s \
                -o libmoonwater-c.so src/shim/libc.c
*/
__asm__("");
#else
#define STANDARD_NO_ENTRY 1             // no _start: the program has its own
#define LIBRARY_MEMCPY_OVERLAPS 1       // memcpy is memmove's body (above)
#include "../lib.c"

#if !X64
#error "libmoonwater-c.so is x86_64 only for now: the other architectures' bowls keep their C library's routines"
#endif

__asm__(
    //
    //       The detector, from the loader's list of what to run at load. glibc passes argc, argv and envp, musl nothing;
    //       moonwater_cpu_detect reads no argument and keeps rbx, the one callee-saved register cpuid writes.
    //
    ".pushsection .init_array, \"aw\"\n   .balign 8\n   .quad moonwater_cpu_detect\n   .popsection\n"

    //
    //       Second names of lib.c's routines, a label on the same address and no jump.
    //
    ASM_ALIAS(index,          string_first_of)
    ASM_ALIAS(rindex,         string_last_of_or_end)
    ASM_ALIAS(__memcmpeq,     memory_compare)       // GCC's call for a memcmp only compared with zero
    ASM_ALIAS(explicit_bzero, memory_zero)          // a call cannot be optimised away, and this is a call
    ASM_ALIAS(__bzero,        memory_zero)
    ASM_ALIAS(memset_explicit, memory_fill)
    ASM_ALIAS(__stpcpy,       string_copy_end)
    ASM_ALIAS(__stpncpy,      string_copy_max_endptr)
    ASM_ALIAS(__strnlen,      string_length_max)
    ASM_ALIAS(__strtok_r,     string_token_next)
    ASM_ALIAS(ffsl,           bits_first_set_wide)
    ASM_ALIAS(ffsll,          bits_first_set_wide)
    ASM_ALIAS(labs,           absolute_wide)
    ASM_ALIAS(llabs,          absolute_wide)
    ASM_ALIAS(imaxabs,        absolute_wide)
    ASM_ALIAS(wmemset,        memory_fill_32)       // a count of four byte units, the destination returned: wmemset's contract

    //
    //       rawmemchr: memchr with no end. The byte is promised to be there, so every byte up to it is the caller's and
    //       none after it need be. The first window is read where it is only when it does not reach the next page;
    //       every load after it is aligned to its own width, so it is inside one page, and is made only after
    //       everything in front of it held no match, so the byte is at or after it and its page is the caller's. (A
    //       count of the rest of the address space handed to memory_first_of was a read past the answer's page by
    //       its unrolled loop, which test/libc_shim.c's unmapped page after the answer caught; a page at a time
    //       through memory_first_of was safe and cost eleven to thirteen cycles a call when the byte was in the first
    //       few, against seven for these.) Three bodies, one a machine: zmm16 and up with AVX-512, so no way out needs
    //       a vzeroupper; 256-bit with AVX2; 128-bit everywhere, which is also what runs before moonwater_cpu_detect.
    //       Each is the first window, four vectors aligned down one at a time, then blocks of four aligned to their
    //       size.
    //
    ASM_FUNC(rawmemchr)
    "cmpb $0, cpu_has_avx512(%rip)\n   jne " ASM_LABEL(libc_shim_raw_z) "\n   cmpb $0, cpu_has_avx2(%rip)\n   jne " ASM_LABEL(libc_shim_raw_y) "\n"
    "movzbl %sil, %eax\n   imul $0x01010101, %eax, %eax\n   movd %eax, %xmm0\n   pshufd $0, %xmm0, %xmm0\n"
    "mov %edi, %eax\n   and $4095, %eax\n   cmp $4080, %eax\n   ja " ASM_LABEL(libc_shim_raw_x_edge) "\n"
    "movdqu (%rdi), %xmm1\n   pcmpeqb %xmm0, %xmm1\n   pmovmskb %xmm1, %eax\n   test %eax, %eax\n   jz " ASM_LABEL(libc_shim_raw_x_more) "\n"
    "bsf %eax, %eax\n   add %rdi, %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_x_more) ":\n   and $-16, %rdi\n"
    "" ASM_LABEL(libc_shim_raw_x_rungs) ":\n"
    "movdqa 16(%rdi), %xmm1\n   pcmpeqb %xmm0, %xmm1\n   pmovmskb %xmm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x1) "\n"
    "movdqa 32(%rdi), %xmm1\n   pcmpeqb %xmm0, %xmm1\n   pmovmskb %xmm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x2) "\n"
    "movdqa 48(%rdi), %xmm1\n   pcmpeqb %xmm0, %xmm1\n   pmovmskb %xmm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x3) "\n"
    "movdqa 64(%rdi), %xmm1\n   pcmpeqb %xmm0, %xmm1\n   pmovmskb %xmm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x4) "\n"
    "add $16, %rdi\n   and $-64, %rdi\n"
    "" ASM_LABEL(libc_shim_raw_x_loop) ":\n"
    "movdqa 64(%rdi), %xmm1\n   movdqa 80(%rdi), %xmm2\n   movdqa 96(%rdi), %xmm3\n   movdqa 112(%rdi), %xmm4\n"
    "pcmpeqb %xmm0, %xmm1\n   pcmpeqb %xmm0, %xmm2\n   pcmpeqb %xmm0, %xmm3\n   pcmpeqb %xmm0, %xmm4\n"
    "movdqa %xmm1, %xmm5\n   por %xmm2, %xmm5\n   movdqa %xmm3, %xmm6\n   por %xmm4, %xmm6\n   por %xmm6, %xmm5\n   pmovmskb %xmm5, %eax\n"
    "add $64, %rdi\n   test %eax, %eax\n   jz " ASM_LABEL(libc_shim_raw_x_loop) "\n"
    "pmovmskb %xmm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x0) "\n"
    "pmovmskb %xmm2, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x1) "\n"
    "pmovmskb %xmm3, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_x2) "\n"
    "pmovmskb %xmm4, %eax\n"
    "" ASM_LABEL(libc_shim_raw_x3) ":\n   bsf %eax, %eax\n   lea 48(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_x0) ":\n   bsf %eax, %eax\n   add %rdi, %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_x1) ":\n   bsf %eax, %eax\n   lea 16(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_x2) ":\n   bsf %eax, %eax\n   lea 32(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_x4) ":\n   bsf %eax, %eax\n   lea 64(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_x_edge) ":\n   mov %rdi, %rcx\n   and $-16, %rdi\n   movdqa (%rdi), %xmm1\n   pcmpeqb %xmm0, %xmm1\n   pmovmskb %xmm1, %eax\n"
    "and $15, %ecx\n   shr %cl, %eax\n   jz " ASM_LABEL(libc_shim_raw_x_rungs) "\n   bsf %eax, %eax\n   add %rdi, %rax\n   add %rcx, %rax\n" ASM_RET
    //       AVX2.
    ".balign 64\n"
    "" ASM_LABEL(libc_shim_raw_y) ":\n   vmovd %esi, %xmm0\n   vpbroadcastb %xmm0, %ymm0\n"
    "mov %edi, %eax\n   and $4095, %eax\n   cmp $4064, %eax\n   ja " ASM_LABEL(libc_shim_raw_y_edge) "\n"
    "vpcmpeqb (%rdi), %ymm0, %ymm1\n   vpmovmskb %ymm1, %eax\n   test %eax, %eax\n   jz " ASM_LABEL(libc_shim_raw_y_more) "\n"
    "tzcnt %eax, %eax\n   add %rdi, %rax\n   vzeroupper\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_y_more) ":\n   and $-32, %rdi\n"
    "" ASM_LABEL(libc_shim_raw_y_rungs) ":\n"
    "vpcmpeqb 32(%rdi), %ymm0, %ymm1\n   vpmovmskb %ymm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y1) "\n"
    "vpcmpeqb 64(%rdi), %ymm0, %ymm1\n   vpmovmskb %ymm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y2) "\n"
    "vpcmpeqb 96(%rdi), %ymm0, %ymm1\n   vpmovmskb %ymm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y3) "\n"
    "vpcmpeqb 128(%rdi), %ymm0, %ymm1\n   vpmovmskb %ymm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y4) "\n"
    "add $32, %rdi\n   and $-128, %rdi\n"
    "" ASM_LABEL(libc_shim_raw_y_loop) ":\n"
    "vpcmpeqb 128(%rdi), %ymm0, %ymm1\n   vpcmpeqb 160(%rdi), %ymm0, %ymm2\n   vpcmpeqb 192(%rdi), %ymm0, %ymm3\n   vpcmpeqb 224(%rdi), %ymm0, %ymm4\n"
    "vpor %ymm1, %ymm2, %ymm5\n   vpor %ymm3, %ymm4, %ymm6\n   vpor %ymm5, %ymm6, %ymm5\n   vpmovmskb %ymm5, %eax\n"
    "sub $-128, %rdi\n   test %eax, %eax\n   jz " ASM_LABEL(libc_shim_raw_y_loop) "\n"
    "vpmovmskb %ymm1, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y0) "\n"
    "vpmovmskb %ymm2, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y1) "\n"
    "vpmovmskb %ymm3, %eax\n   test %eax, %eax\n   jnz " ASM_LABEL(libc_shim_raw_y2) "\n"
    "vpmovmskb %ymm4, %eax\n"
    "" ASM_LABEL(libc_shim_raw_y3) ":\n   tzcnt %eax, %eax\n   lea 96(%rdi,%rax), %rax\n   vzeroupper\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_y0) ":\n   tzcnt %eax, %eax\n   add %rdi, %rax\n   vzeroupper\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_y1) ":\n   tzcnt %eax, %eax\n   lea 32(%rdi,%rax), %rax\n   vzeroupper\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_y2) ":\n   tzcnt %eax, %eax\n   lea 64(%rdi,%rax), %rax\n   vzeroupper\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_y4) ":\n   tzcnt %eax, %eax\n   lea 128(%rdi,%rax), %rax\n   vzeroupper\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_y_edge) ":\n   mov %rdi, %rcx\n   and $-32, %rdi\n   vpcmpeqb (%rdi), %ymm0, %ymm1\n   vpmovmskb %ymm1, %eax\n"
    "shr %cl, %eax  # by the start's place in its vector: a 32-bit shift counts five bits\n   jz " ASM_LABEL(libc_shim_raw_y_rungs) "\n   tzcnt %eax, %eax\n   add %rcx, %rax\n   vzeroupper\n" ASM_RET
    //       AVX-512: the block is four exclusive-ors with the loads inside them, three minima and one test.
    //
    ".balign 64\n"
    "" ASM_LABEL(libc_shim_raw_z) ":\n   vpbroadcastb %esi, %zmm16\n"
    "mov %edi, %eax\n   and $4095, %eax\n   cmp $4032, %eax\n   ja " ASM_LABEL(libc_shim_raw_edge) "\n"
    "vpcmpeqb (%rdi), %zmm16, %k0\n   kmovq %k0, %rax\n   test %rax, %rax\n   jz " ASM_LABEL(libc_shim_raw_more) "\n"
    "tzcnt %rax, %rax\n   add %rdi, %rax\n" ASM_RET
    ".balign 16\n"
    "" ASM_LABEL(libc_shim_raw_more) ":\n   and $-64, %rdi\n"
    "" ASM_LABEL(libc_shim_raw_rungs) ":\n"
    "vpcmpeqb 64(%rdi), %zmm16, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_1) "\n"
    "vpcmpeqb 128(%rdi), %zmm16, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_2) "\n"
    "vpcmpeqb 192(%rdi), %zmm16, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_3) "\n"
    "vpcmpeqb 256(%rdi), %zmm16, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_4) "\n"
    "add $64, %rdi\n   xor %dil, %dil\n"
    ".balign 16\n"
    "" ASM_LABEL(libc_shim_raw_loop) ":\n"
    "vpxorq 256(%rdi), %zmm16, %zmm17\n   vpxorq 320(%rdi), %zmm16, %zmm18\n   vpxorq 384(%rdi), %zmm16, %zmm19\n   vpxorq 448(%rdi), %zmm16, %zmm20\n"
    "vpminub %zmm18, %zmm17, %zmm21\n   vpminub %zmm20, %zmm19, %zmm22\n   vpminub %zmm21, %zmm22, %zmm21\n   vptestnmb %zmm21, %zmm21, %k2\n"
    "kmovq %k2, %rdx\n   add $256, %rdi\n   test %rdx, %rdx\n   jz " ASM_LABEL(libc_shim_raw_loop) "\n"
    "vptestnmb %zmm17, %zmm17, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_0) "\n"
    "vptestnmb %zmm18, %zmm18, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_1) "\n"
    "vptestnmb %zmm19, %zmm19, %k0\n   kortestq %k0, %k0\n   jnz " ASM_LABEL(libc_shim_raw_2) "\n"
    "tzcnt %rdx, %rax\n   lea 192(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_0) ":\n   kmovq %k0, %rax\n   tzcnt %rax, %rax\n   add %rdi, %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_1) ":\n   kmovq %k0, %rax\n   tzcnt %rax, %rax\n   lea 64(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_2) ":\n   kmovq %k0, %rax\n   tzcnt %rax, %rax\n   lea 128(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_3) ":\n   kmovq %k0, %rax\n   tzcnt %rax, %rax\n   lea 192(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_4) ":\n   kmovq %k0, %rax\n   tzcnt %rax, %rax\n   lea 256(%rdi,%rax), %rax\n" ASM_RET
    "" ASM_LABEL(libc_shim_raw_edge) ":\n   mov %rdi, %rcx\n   and $-64, %rdi\n   vpcmpeqb (%rdi), %zmm16, %k0\n   kmovq %k0, %rax\n   shr %cl, %rax\n"
    "jz " ASM_LABEL(libc_shim_raw_rungs) "\n   tzcnt %rax, %rax\n   add %rcx, %rax\n" ASM_RET
    ASM_END(rawmemchr)
    ASM_ALIAS(__rawmemchr,    rawmemchr)

    //
    //       mempcpy: the copy, and the end of the destination handed back (kept on the stack across the call, which also
    //       leaves the stack aligned for it).
    //
    ASM_FUNC(mempcpy)
    "lea (%rdi,%rdx), %rax\n   push %rax\n   call memory_copy\n   pop %rax\n" ASM_RET
    ASM_END(mempcpy)
    ASM_ALIAS(__mempcpy,      mempcpy)

    //
    //       The wide copies: a count of wchar_t, four bytes on Linux, made a count of bytes.
    //
    ASM_FUNC(wmemcpy)
    "shl $2, %rdx\n   jmp memory_copy\n"
    ASM_END(wmemcpy)
    ASM_ALIAS(wmemmove,       wmemcpy)
    ASM_FUNC(wmempcpy)
    "shl $2, %rdx\n   jmp mempcpy\n"
    ASM_END(wmempcpy)

    //
    //       The checked copies: the room is the last argument, and too little of it is the end of the program.
    //       rcx is the room for the four argument forms, rdx for the three argument ones.
    //
    ASM_FUNC(__memcpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp memory_copy\n"
    ASM_END(__memcpy_chk)
    ASM_ALIAS(__memmove_chk,  __memcpy_chk)
    ASM_FUNC(__mempcpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp mempcpy\n"
    ASM_END(__mempcpy_chk)
    ASM_FUNC(__memset_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp memory_fill\n"
    ASM_END(__memset_chk)
    ASM_FUNC(__explicit_bzero_chk)
    "cmp %rsi, %rdx\n   jb libc_shim_overflow\n   jmp memory_zero\n"
    ASM_END(__explicit_bzero_chk)
    ASM_FUNC(__strncpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp strncpy\n"
    ASM_END(__strncpy_chk)
    ASM_FUNC(__stpncpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp string_copy_max_endptr\n"
    ASM_END(__stpncpy_chk)
    ASM_FUNC(__strlcpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp string_copy_bounded\n"
    ASM_END(__strlcpy_chk)
    ASM_FUNC(__strlcat_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp string_append_bounded\n"
    ASM_END(__strlcat_chk)
    //       The wide ones count their room in wchar_t, as glibc's do.
    ASM_FUNC(__wmemset_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   jmp memory_fill_32\n"
    ASM_END(__wmemset_chk)
    ASM_FUNC(__wmemcpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   shl $2, %rdx\n   jmp memory_copy\n"
    ASM_END(__wmemcpy_chk)
    ASM_ALIAS(__wmemmove_chk, __wmemcpy_chk)
    ASM_FUNC(__wmempcpy_chk)
    "cmp %rdx, %rcx\n   jb libc_shim_overflow\n   shl $2, %rdx\n   jmp mempcpy\n"
    ASM_END(__wmempcpy_chk)

    //
    //       __strcpy_chk and __stpcpy_chk: the source's length first, then the copy of it and its terminator, which needs
    //       one byte more than the length. rbx the destination, r12 the source, r13 the room and then the length; three
    //       pushes over the return address leave the stack aligned for the calls.
    //
    ASM_FUNC(__strcpy_chk)
    "push %rbx\n   push %r12\n   push %r13\n   mov %rdi, %rbx\n   mov %rsi, %r12\n   mov %rdx, %r13\n   mov %rsi, %rdi\n"
    "call string_length\n   cmp %rax, %r13\n   jbe libc_shim_overflow\n"
    "mov %rbx, %rdi\n   mov %r12, %rsi\n   lea 1(%rax), %rdx\n   call memory_copy_apart\n   mov %rbx, %rax\n"
    "pop %r13\n   pop %r12\n   pop %rbx\n" ASM_RET
    ASM_END(__strcpy_chk)
    ASM_FUNC(__stpcpy_chk)
    "push %rbx\n   push %r12\n   push %r13\n   mov %rdi, %rbx\n   mov %rsi, %r12\n   mov %rdx, %r13\n   mov %rsi, %rdi\n"
    "call string_length\n   cmp %rax, %r13\n   jbe libc_shim_overflow\n   mov %rax, %r13\n"
    "mov %rbx, %rdi\n   mov %r12, %rsi\n   lea 1(%rax), %rdx\n   call memory_copy_apart\n   lea (%rbx,%r13), %rax\n"
    "pop %r13\n   pop %r12\n   pop %rbx\n" ASM_RET
    ASM_END(__stpcpy_chk)

    //
    //       __strcat_chk and __strncat_chk: the destination's length must leave room, then what is appended (all of the
    //       source, or at most n of it) and its terminator must fit in what is left. rbx the destination, r12 the source,
    //       r13 the room, r14 the destination's length, r15 n; five pushes over the return address and eight bytes more
    //       keep the calls aligned. The append is memory_copy_apart_end, the copy and one terminator after it.
    //
    ASM_FUNC(__strcat_chk)
    "mov %rsi, %rcx\n   not %rcx  # no n: the bound is the rest of the address space, which never wraps\n"
    "" ASM_LABEL(libc_shim_strcat_chk) ":\n"
    "push %rbx\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n   sub $8, %rsp\n"
    "mov %rdi, %rbx\n   mov %rsi, %r12\n   mov %rdx, %r13\n   mov %rcx, %r15\n"
    "call string_length\n   cmp %rax, %r13\n   jbe libc_shim_overflow\n   mov %rax, %r14\n"
    "mov %r12, %rdi\n   mov %r15, %rsi\n   call string_length_max\n"
    "mov %r13, %rcx\n   sub %r14, %rcx\n   cmp %rax, %rcx\n   jbe libc_shim_overflow\n"
    "lea (%rbx,%r14), %rdi\n   mov %r12, %rsi\n   mov %rax, %rdx\n   call memory_copy_apart_end\n   mov %rbx, %rax\n"
    "add $8, %rsp\n   pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbx\n" ASM_RET
    ASM_END(__strcat_chk)
    ASM_FUNC(__strncat_chk)
    "xchg %rdx, %rcx\n   jmp " ASM_LABEL(libc_shim_strcat_chk) "\n"
    ASM_END(__strncat_chk)

    //
    //       glibc's __chk_fail, said the way glibc says it, and the end it makes. Reached by a jump from any of the
    //       above with the stack as it was, which matters to nothing here: it never returns.
    //
    ASM_FUNC(libc_shim_overflow)
    "mov $1, %eax\n   mov $2, %edi\n   lea " ASM_LABEL(libc_shim_said) "(%rip), %rsi\n   mov $45, %edx\n   syscall  # write\n"
    "sub $32, %rsp\n   and $-16, %rsp\n   xor %eax, %eax\n   mov %rax, (%rsp)\n   mov %rax, 8(%rsp)\n   mov %rax, 16(%rsp)\n   mov %rax, 24(%rsp)\n"
    "mov $13, %eax\n   mov $6, %edi\n   mov %rsp, %rsi\n   xor %edx, %edx\n   mov $8, %r10d\n   syscall  # rt_sigaction(SIGABRT, SIG_DFL)\n"
    "movq $32, (%rsp)  # the mask with SIGABRT alone in it\n"
    "mov $14, %eax\n   mov $1, %edi\n   mov %rsp, %rsi\n   xor %edx, %edx\n   mov $8, %r10d\n   syscall  # rt_sigprocmask(SIG_UNBLOCK)\n"
    "mov $39, %eax\n   syscall  # getpid\n   mov %eax, %r12d\n   mov $186, %eax\n   syscall  # gettid\n"
    "" ASM_LABEL(libc_shim_abort) ":\n"
    "mov %eax, %esi\n   mov %r12d, %edi\n   mov $6, %edx\n   mov $234, %eax\n   syscall  # tgkill(SIGABRT)\n"
    "mov $231, %eax\n   mov $127, %edi\n   syscall  # exit_group, if that was survived\n   ud2\n"
    ".pushsection .rodata.libc_shim_said, \"a\"\n"
    "" ASM_LABEL(libc_shim_said) ":  .ascii \"*** buffer overflow detected ***: terminated\\n\"\n"
    ".popsection\n"
    ASM_END(libc_shim_overflow)
);
#endif
