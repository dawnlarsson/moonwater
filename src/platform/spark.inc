/* Private entry ABI shared by the Spark loader and freestanding _start. */
#ifndef MOONWATER_SPARK_ENTRY_INCLUDED
#define MOONWATER_SPARK_ENTRY_INCLUDED

#define SPARK_START_MAGIC 0x5350524b53544132UL /* "SPRKSTA2" */
/* Version 3 is version 2 and one more register, the entry facts: what the
   kernel knows about a program's start that the program could only find out
   at a cost. r15 on x86_64, x22 on arm64, s5 on riscv64. The two magics
   differ in their last byte alone, so _start takes both in one compare and
   trusts the facts only under version 3: an older loader leaves that
   register holding whatever it held. */
#define SPARK_START_MAGIC_FACTS 0x5350524b53544133UL /* "SPRKSTA3" */
/* No seccomp filter is on the program: its mode was disabled at exec. A
   filter survives exec and only the task itself adds one, so this holds
   until the program installs one of its own, and saves it reading and
   authenticating /proc/self/status to prove the same. */
#define SPARK_ENTRY_UNFILTERED 0x01UL
/* An auxiliary vector follows the environment's null on the stack, as it
   does for an ELF: AT_SYSINFO_EHDR (when a vDSO was mapped), AT_PAGESZ and
   AT_NULL. Older loaders put nothing there but the strings. */
#define SPARK_ENTRY_AUXV 0x02UL
/* The bss is already VM_NOHUGEPAGE: the loader mapped it so, and a program
   that would otherwise madvise its whole bss to say the same (a kernel with
   transparent huge pages at "always" answers each first touch of a sparse
   region with two megabytes of zeroed memory) has nothing to ask. */
#define SPARK_ENTRY_BSS_NOHUGE 0x04UL
/* Version 2 changes the feature word from packed bits to Boolean byte lanes.
   Keeping that change in the magic makes either side of a mixed old/new pair
   reject the private handoff and use the ordinary CPUID fallback instead of
   silently decoding the other version's feature layout. */
/* Append-only Boolean byte lanes. Older loaders zero the complete feature
   register before setting their known lanes; older runtimes ignore the
   extra lanes. Extending this word therefore keeps the version-2 ABI.

   x86_64: the carry-less multiply and AES lanes are SSE-level and need no
   saved state beyond xmm, so a loader publishes them on any processor that
   has them. VPCLMULQDQ and VAES are published only once ymm state is saved;
   a body that wants them in zmm also tests the AVX-512 lane. With these
   four the 64-bit word is full: a ninth lane needs a second register and a
   new magic.

   arm64 (x20) and riscv64 (s3) use the same byte lanes from the bottom: the
   carry-less multiply (PMULL, Zbc) in byte 0 and AES (the AES extension,
   Zvkned with usable V) in byte 1. */
#define SPARK_CPU_AVX2 0x0001UL
#define SPARK_CPU_AVX512 0x0100UL
#define SPARK_CPU_AVX512_VBMI 0x010000UL
#define SPARK_CPU_FMA 0x01000000UL
#define SPARK_CPU_PCLMUL 0x0100000000UL
#define SPARK_CPU_AES 0x010000000000UL
#define SPARK_CPU_VPCLMUL 0x01000000000000UL
#define SPARK_CPU_VAES 0x0100000000000000UL
#define SPARK_CPU_CLMUL_LANE 0x0001UL
#define SPARK_CPU_AES_LANE 0x0100UL

/* Final exec still applies its rlimit-dependent argument ceiling. This is
   only the device-copy ceiling, kept at Linux's ordinary ARG_MAX floor. */
#define SPARK_SPAWN_MAX_BYTES (2UL << 20)
#define SPARK_SPAWN_MAX_STRINGS 65536U

#endif
