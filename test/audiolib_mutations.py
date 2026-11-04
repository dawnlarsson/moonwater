#!/usr/bin/env python3
"""Mutation proofs for lib.c's audio routines (the sound server's mixing loops), CHECK_audiolib.

Each mutation is one edit to a copy of src/lib.c (a constant, a lane, a register, an instruction's sense), built into
CHECK_audiolib for the machine the edited body belongs to and run under the cpu that reaches it (x86_64 natively, which walks
AVX2 with FMA and the SSE2 floor itself; arm64 and riscv64 under qemu-user, riscv64 with V at two vector lengths and without);
the check must turn red.  A mutation that survives is a hole in the check.

    python3 test/audiolib_mutations.py [NAME...]        from the tree's root; 8 builds at a time

Last run: see the commit that changed this line.
"""
import concurrent.futures, os, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = tempfile.mkdtemp(prefix='audiolib-mutations-')
only = sys.argv[1:]

COMPILER = {'x86_64': ['gcc', '-march=x86-64'],
            'arm64': ['aarch64-linux-gnu-gcc', '-mno-outline-atomics'],
            'riscv64': ['riscv64-linux-gnu-gcc', '-march=rv64imafd_zicsr_zicntr', '-mabi=lp64d']}
RUN = {'x86_64': [[]],
       'arm64': [['qemu-aarch64']],
       'riscv64': [['qemu-riscv64', '-cpu', 'rv64'], ['qemu-riscv64', '-cpu', 'rv64,v=true,vlen=128'],
                   ['qemu-riscv64', '-cpu', 'rv64,v=true,vlen=512'],
                   ['qemu-riscv64', '-cpu', 'rv64,v=true,vlen=1024,rvv_ta_all_1s=true,rvv_ma_all_1s=true']]}

# (machine, name, old text, new text); the old text is replaced wherever it stands in the machine's block.
M = [
    # ---- resample_polyphase2_f32, x86_64 ----
    ("x86_64", "polyphase: weight scale 2^-31", ".long 0x2f800000", ".long 0x2f000000"),
    ("x86_64", "polyphase avx2: row mixed the wrong way (fmadd for fnmadd)", "vfnmadd213ps %ymm0, %ymm8, %ymm4", "vfmadd213ps %ymm0, %ymm8, %ymm4"),
    ("x86_64", "polyphase avx2: next row two rows on", "lea (%r15,%r9), %r13\\n\"\n    \"vxorps %xmm9, %xmm9, %xmm9\\n   vcvtsi2ssq %r14, %xmm9, %xmm9\\n   vmulss .Laudio_poly_scale(%rip), %xmm9, %xmm9\\n   vbroadcastss %xmm9, %ymm8",
     "lea (%r15,%r9,2), %r13\\n\"\n    \"vxorps %xmm9, %xmm9, %xmm9\\n   vcvtsi2ssq %r14, %xmm9, %xmm9\\n   vmulss .Laudio_poly_scale(%rip), %xmm9, %xmm9\\n   vbroadcastss %xmm9, %ymm8"),
    ("x86_64", "polyphase avx2: last vector of a turn not summed", "\"vfmadd231ps 64(%r12,%rbp), %ymm6, %ymm10\\n   vfmadd231ps 96(%r12,%rbp), %ymm7, %ymm11\\n\"",
     "\"vfmadd231ps 64(%r12,%rbp), %ymm6, %ymm10\\n\""),
    ("x86_64", "polyphase avx2: planes swapped on the way out", "\"vmovss %xmm2, (%rdi)\\n   vextractps $1, %xmm2, (%rsi)\\n\"\n    \"add $4, %rdi\\n   add $4, %rsi\\n   add 72(%rsp), %r11\\n   dec %r10\\n   jnz .Laudio_poly_y_frame\\n\"",
     "\"vmovss %xmm2, (%rsi)\\n   vextractps $1, %xmm2, (%rdi)\\n\"\n    \"add $4, %rdi\\n   add $4, %rsi\\n   add 72(%rsp), %r11\\n   dec %r10\\n   jnz .Laudio_poly_y_frame\\n\""),
    ("x86_64", "polyphase avx2: fraction taken as a signed 32-bit integer", "vcvtsi2ssq %r14, %xmm9, %xmm9\\n   vmulss .Laudio_poly_scale(%rip), %xmm9, %xmm9\\n   vbroadcastss %xmm9, %ymm8",
     "vcvtsi2ssl %r14d, %xmm9, %xmm9\\n   vmulss .Laudio_poly_scale(%rip), %xmm9, %xmm9\\n   vbroadcastss %xmm9, %ymm8"),
    ("x86_64", "polyphase avx2: input one tap late", "lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n\"\n    \"vxorps %xmm10, %xmm10, %xmm10\\n   vxorps %xmm11, %xmm11, %xmm11\\n   vxorps %xmm12, %xmm12, %xmm12\\n   vxorps %xmm13, %xmm13, %xmm13\\n\"\n    \"xor %ebp, %ebp\\n\"\n    \".balign 64\\n.Laudio_poly_y_tap",
     "lea 4(%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n\"\n    \"vxorps %xmm10, %xmm10, %xmm10\\n   vxorps %xmm11, %xmm11, %xmm11\\n   vxorps %xmm12, %xmm12, %xmm12\\n   vxorps %xmm13, %xmm13, %xmm13\\n\"\n    \"xor %ebp, %ebp\\n\"\n    \".balign 64\\n.Laudio_poly_y_tap"),
    ("x86_64", "polyphase avx2: only the first 32 taps", "\"add $128, %rbp\\n   cmp %r9, %rbp\\n   jb .Laudio_poly_y_tap\\n\"", "\"add $128, %rbp\\n   cmp $128, %rbp\\n   jb .Laudio_poly_y_tap\\n\""),
    ("x86_64", "polyphase avx2: position stepped by its own integer", "add 72(%rsp), %r11\\n   dec %r10\\n   jnz .Laudio_poly_y_frame", "add 64(%rsp), %r11\\n   dec %r10\\n   jnz .Laudio_poly_y_frame"),
    ("x86_64", "polyphase avx2: row from half the fraction", "mov %r11d, %r14d\\n   imul 56(%rsp), %r14\\n   mov %r14, %r15\\n   shr $32, %r15\\n\"\n    \"mov %r14d, %r14d\\n   imul %r9, %r15\\n   add %r8, %r15\\n   lea (%r15,%r9), %r13\\n\"\n    \"vxorps",
     "mov %r11d, %r14d\\n   imul 56(%rsp), %r14\\n   mov %r14, %r15\\n   shr $33, %r15\\n\"\n    \"mov %r14d, %r14d\\n   imul %r9, %r15\\n   add %r8, %r15\\n   lea (%r15,%r9), %r13\\n\"\n    \"vxorps"),
    ("x86_64", "polyphase avx2: lanes of the two planes folded together", "\"vperm2f128 $0x20, %ymm12, %ymm10, %ymm0\\n   vperm2f128 $0x31, %ymm12, %ymm10, %ymm1\\n   vaddps %ymm1, %ymm0, %ymm0\\n\"",
     "\"vperm2f128 $0x20, %ymm12, %ymm10, %ymm0\\n   vperm2f128 $0x20, %ymm12, %ymm10, %ymm1\\n   vaddps %ymm1, %ymm0, %ymm0\\n\""),
    ("x86_64", "polyphase sse2: product not added in (second lane group lost)", "\"movups \" hi \"(%r12,%rbp), %xmm5\\n   mulps %xmm3, %xmm5\\n   addps %xmm5, %xmm11\\n\"",
     "\"movups \" hi \"(%r12,%rbp), %xmm5\\n   mulps %xmm3, %xmm5\\n   addps %xmm5, %xmm5\\n\""),
    ("x86_64", "polyphase sse2: the row not mixed by the weight", "\"subps %xmm0, %xmm2\\n   subps %xmm1, %xmm3\\n   mulps %xmm8, %xmm2\\n   mulps %xmm8, %xmm3\\n\"",
     "\"subps %xmm0, %xmm2\\n   subps %xmm1, %xmm3\\n   mulps %xmm8, %xmm2\\n\""),
    ("x86_64", "polyphase sse2: lanes folded in another order (not the C's bits)", "movaps %xmm10, %xmm0\\n   shufps $0xb1, %xmm0, %xmm0", "movaps %xmm10, %xmm0\\n   shufps $0x4e, %xmm0, %xmm0"),
    ("x86_64", "polyphase sse2: lanes of a plane summed in another grouping (not the C's bits)", "AUDIO_POLY_SSE2_GROUP(\"0\", \"16\")\n    AUDIO_POLY_SSE2_GROUP(\"32\", \"48\")",
     "AUDIO_POLY_SSE2_GROUP(\"0\", \"32\")\n    AUDIO_POLY_SSE2_GROUP(\"16\", \"48\")"),
    # ---- resample_polyphase2_f32, arm64 ----
    ("arm64", "polyphase neon: row mixed the wrong way", "fmla v0.4s, v4.4s, v30.s[0]\\n   fmla v1.4s", "fmls v0.4s, v4.4s, v30.s[0]\\n   fmla v1.4s"),
    ("arm64", "polyphase neon: a turn of eight taps", "\"subs x16, x16, #64\\n   b.ne 2b\\n\"", "\"subs x16, x16, #32\\n   b.ne 2b\\n\""),
    ("arm64", "polyphase neon: planes swapped in the fold", "faddp v20.4s, v20.4s, v22.4s", "faddp v20.4s, v22.4s, v20.4s"),
    ("arm64", "polyphase neon: fraction keeps the row's bits", "mul x11, x11, x6\\n   lsr x12, x11, #32\\n   mov w11, w11\\n   ucvtf s30, x11", "mul x11, x11, x6\\n   lsr x12, x11, #32\\n   ucvtf s30, x11"),
    ("arm64", "polyphase neon: row from half the fraction", "lsr x12, x11, #32\\n   mov w11, w11", "lsr x12, x11, #33\\n   mov w11, w11"),
    ("arm64", "polyphase neon: the weight not scaled", "fmul s30, s30, s31", "fmul s30, s30, s30"),
    ("arm64", "polyphase neon: second plane's product on the first's inputs", "ldp q16, q17, [x15], #32\\n   ldp q18, q19, [x15], #32", "ldp q16, q17, [x14], #32\\n   ldp q18, q19, [x14], #32"),
    # ---- resample_polyphase2_f32, riscv64 ----
    ("riscv64", "polyphase rv scalar: row mixed from the wrong side", "fsub.s ft4, ft4, ft0\\n   fsub.s ft5, ft5, ft1", "fsub.s ft4, ft0, ft4\\n   fsub.s ft5, ft5, ft1"),
    ("riscv64", "polyphase rv scalar: fraction taken as a signed 32-bit integer", "fcvt.s.lu ft11, t0\\n   fmul.s ft11, ft11, ft10\\n   mul t3, t3, a5", "fcvt.s.w ft11, t0\\n   fmul.s ft11, ft11, ft10\\n   mul t3, t3, a5"),
    ("riscv64", "polyphase rv scalar: planes' sums swapped", "fsw fa0, 0(a0)\\n   fsw fa4, 0(a1)", "fsw fa0, 0(a1)\\n   fsw fa4, 0(a0)"),
    ("riscv64", "polyphase rv scalar: a lane lost", "fadd.s fa0, fa0, fa1\\n   fadd.s fa2, fa2, fa3\\n   fadd.s fa0, fa0, fa2", "fadd.s fa0, fa0, fa1\\n   fadd.s fa2, fa2, fa3"),
    ("riscv64", "polyphase rv vector: row mixed from the wrong side", "vfsub.vv v4, v4, v0\\n   vfmacc.vf v0, ft11, v4", "vfsub.vv v4, v0, v4\\n   vfmacc.vf v0, ft11, v4"),
    ("riscv64", "polyphase rv vector: tail agnostic (the lanes a short strip leaves are lost)", "vsetvli t0, t5, e32, m4, tu, ma\\n   vle32.v v0, (t3)", "vsetvli t0, t5, e32, m4, ta, ma\\n   vle32.v v0, (t3)"),
    ("riscv64", "polyphase rv vector: one sum for both planes", "vfredusum.vs v24, v20, v24\\n   vfmv.f.s ft1, v24", "vfredusum.vs v24, v16, v24\\n   vfmv.f.s ft1, v24"),
    ("riscv64", "polyphase rv vector: accumulators not cleared", "vmv.v.i v16, 0\\n   vmv.v.i v20, 0\\n   mv t5, a5", "mv t5, a5"),
    # ---- f32_scale_peak, f32_scale_add_peak ----
    ("x86_64", "scale avx512: masked window one lane wrong", "mov $16, %ecx\\n   sub %edx, %ecx\\n   mov $-1, %eax", "mov $15, %ecx\\n   sub %edx, %ecx\\n   mov $-1, %eax"),
    ("x86_64", "scale avx512: fourth peak vector not kept", "vpandd %zmm5, %zmm9, %zmm13\\n   vmaxps %zmm4, %zmm13, %zmm4", "vpandd %zmm5, %zmm9, %zmm13\\n   vmaxps %zmm4, %zmm13, %zmm13"),
    ("x86_64", "scale avx512: the sign kept in the peak", "vpandd %zmm5, %zmm6, %zmm10", "vpandd %zmm6, %zmm6, %zmm10"),
    ("x86_64", "scale avx512: sixteens not advanced", "sub $16, %edx\\n   jmp .Laudio_scale_add_peak_z16_more", "sub $15, %edx\\n   jmp .Laudio_scale_add_peak_z16_more"),
    ("x86_64", "scale avx512: add lost in the masked window", "vaddps (%rdi), %zmm6, %zmm6{%k1}{z}", "vaddps (%rdi), %zmm6, %zmm7{%k1}{z}"),
    ("x86_64", "scale avx2: last vector stored one float off", "vmovups %ymm14, -32(%r10)", "vmovups %ymm14, -28(%r10)"),
    ("x86_64", "scale avx2: the sign kept in the peak", "vandps %ymm5, %ymm6, %ymm10", "vandps %ymm6, %ymm6, %ymm10"),
    ("x86_64", "scale avx2: last vector's sum with dst lost", "vaddps -32(%rdi,%rdx,4), %ymm14, %ymm14", "vaddps -32(%rsi,%rdx,4), %ymm14, %ymm14"),
    ("x86_64", "scale avx2: last vector's peak lost", "vmaxps %ymm4, %ymm15, %ymm4", "vmaxps %ymm4, %ymm15, %ymm15"),
    ("x86_64", "scale avx2: eight-float turns stop at seven", "sub $8, %edx\\n   jmp .Laudio_scale_peak_y8_more", "sub $7, %edx\\n   jmp .Laudio_scale_peak_y8_more"),
    ("x86_64", "scale sse2: a NaN poisons the peak", "andps %xmm5, %xmm10\\n   maxps %xmm1, %xmm10\\n   movaps %xmm10, %xmm1\\n\"\n    \"movups %xmm6, (%rdi)", "andps %xmm5, %xmm10\\n   maxps %xmm10, %xmm1\\n   movaps %xmm1, %xmm1\\n\"\n    \"movups %xmm6, (%rdi)"),
    ("x86_64", "scale sse2: last vector stored one float off", "movups %xmm14, -16(%r10)", "movups %xmm14, -12(%r10)"),
    ("x86_64", "scale sse2: scalar tail without the sum", "mulss %xmm0, %xmm6\\n   movaps %xmm6, %xmm10\\n   andps %xmm5, %xmm10\\n   maxss %xmm1, %xmm10\\n   movaps %xmm10, %xmm1\\n\"\n    \"addss (%rdi), %xmm6", "mulss %xmm0, %xmm6\\n   movaps %xmm6, %xmm10\\n   andps %xmm5, %xmm10\\n   maxss %xmm1, %xmm10\\n   movaps %xmm10, %xmm1\\n\"\n    \"addss (%rsi), %xmm6"),
    ("arm64", "scale neon: a NaN poisons the peak (fmax for fmaxnm)", "fmaxnm v24.4s, v24.4s, v16.4s", "fmax v24.4s, v24.4s, v16.4s"),
    ("arm64", "scale neon: the single floats left over lost", "4:  ands x2, x2, #3\\n   b.eq 6f", "4:  ands x2, x2, #1\\n   b.eq 6f"),
    ("arm64", "scale neon: second half of a turn not stored", "st1 {v4.4s, v5.4s, v6.4s, v7.4s}, [x0], #64\\n   subs x3, x3, #1\\n   b.ne 1b", "add x0, x0, #64\\n   subs x3, x3, #1\\n   b.ne 1b"),
    ("arm64", "scale neon: the sum with dst lost in the fours", "ldr q20, [x0]\\n   fadd v0.4s, v0.4s, v20.4s\\n\"\n    \"str q0, [x0], #16", "ldr q20, [x1]\\n   fadd v0.4s, v0.4s, v20.4s\\n\"\n    \"str q0, [x0], #16"),
    ("riscv64", "scale rv scalar: the least for the greatest", "fmax.s ft8, ft8, ft4\\n   fmax.s ft8, ft8, ft5", "fmin.s ft8, ft8, ft4\\n   fmax.s ft8, ft8, ft5"),
    ("riscv64", "scale rv scalar: the sign kept in the peak", "fsgnjx.s ft4, ft0, ft0\\n   fsgnjx.s ft5, ft1, ft1", "fmv.s ft4, ft0\\n   fsgnjx.s ft5, ft1, ft1"),
    ("riscv64", "scale rv vector: the least for the greatest", "vfmax.vv v24, v24, v8", "vfmin.vv v24, v24, v8"),
    ("riscv64", "scale rv vector: tail agnostic (the peaks a short last strip leaves are lost)", "vsetvli t0, a2, e32, m8, tu, ma\\n   vle32.v v0, (a1)\\n   vfmul.vf", "vsetvli t0, a2, e32, m8, ta, ma\\n   vle32.v v0, (a1)\\n   vfmul.vf"),
    ("riscv64", "scale rv vector: peak not folded from the group", "vfredmax.vs v8, v24, v8", "vfredmax.vs v8, v8, v8"),
    # ---- f32_planes_from_s16, f32_planes_from_s32, f32_planes_from_f32 ----
    ("x86_64", "decode avx2: stereo planes swapped", "vmovups %ymm1, (%rdi)\\n   vmovups %ymm0, (%r8)\\n   add $32, %rsi", "vmovups %ymm1, (%r8)\\n   vmovups %ymm0, (%rdi)\\n   add $32, %rsi"),
    ("x86_64", "decode avx2: 16-bit samples zero-extended", "vpsrad $16, %ymm1, %ymm1", "vpsrld $16, %ymm1, %ymm1"),
    ("x86_64", "decode: 32-bit scale 2^-30", ".long 0x30000000", ".long 0x30800000"),
    ("x86_64", "decode: 16-bit scale 2^-14", ".long 0x38000000", ".long 0x38800000"),
    ("x86_64", "decode: float clamp at 17", "mov $0x41800000, %eax", "mov $0x41880000, %eax"),
    ("x86_64", "decode: float clamp low end", "mov $0xc1800000, %eax", "mov $0xc1000000, %eax"),
    ("x86_64", "decode avx2: mono end window one sample off", "vpmovsxwd -16(%rsi), %ymm0", "vpmovsxwd -14(%rsi), %ymm0"),
    ("x86_64", "decode avx2: stereo end window one frame off", "vmovdqu -32(%rsi), %ymm0", "vmovdqu -28(%rsi), %ymm0"),
    ("x86_64", "decode sse2: right channel of floats taken from the left's lanes", "shufps $0xdd, %xmm1, %xmm0", "shufps $0x88, %xmm1, %xmm0"),
    ("x86_64", "decode: other channel counts with the wrong frame stride", "imul $2, %rcx, %r11", "imul $4, %rcx, %r11"),
    ("x86_64", "decode avx2: floats' lanes left crossed", "vpermpd $0xd8, %ymm2, %ymm2", "vpermpd $0xe4, %ymm2, %ymm2"),
    ("x86_64", "decode: NaN of a float not zeroed (scalar)", "andps %xmm1, %xmm0\\n   movss %xmm0, (%rdi)", "movss %xmm0, (%rdi)"),
    ("arm64", "decode neon: 16-bit scale 2^-14", "scvtf v2.4s, v2.4s, #15", "scvtf v2.4s, v2.4s, #14"),
    ("arm64", "decode neon: float high clamp lost", "fmin v2.4s, v2.4s, v16.4s", "fmax v2.4s, v2.4s, v16.4s"),
    ("arm64", "decode neon: stereo not split", "ld2 {v0.8h, v1.8h}, [x1], #32", "ld1 {v0.8h, v1.8h}, [x1], #32"),
    ("arm64", "decode neon: stereo frames left over lost", "6:  and x2, x2, #7\\n   cbz x2, 9f", "6:  and x2, x2, #3\\n   cbz x2, 9f"),
    ("arm64", "decode neon: 32-bit samples read unsigned", "ldr w6, [x1]\\n   scvtf s0, w6, #31", "ldr w6, [x1]\\n   ucvtf s0, w6, #31"),
    ("riscv64", "decode rv scalar: 16-bit samples zero-extended", "3:  lh t0, 0(t2)", "3:  lhu t0, 0(t2)"),
    ("riscv64", "decode rv vector: 16-bit samples zero-extended", "vsext.vf2 v8, v0", "vzext.vf2 v8, v0"),
    ("riscv64", "decode rv vector: float low clamp lost", "vfmax.vf v8, v8, ft11", "vfmin.vf v8, v8, ft11"),
    ("riscv64", "decode rv vector: strip advance by a float a frame", "mul t3, t0, t5\\n   add t2, t2, t3", "slli t3, t0, 2\\n   add t2, t2, t3"),
    ("riscv64", "decode rv scalar: float NaN not zeroed", "fmv.s ft0, ft1\\n   bnez t0, 4f\\n   fmv.w.x ft0, zero", "fmv.s ft0, ft1\\n   bnez t0, 4f"),
    # ---- f32_pair_to_s16, _s32, _s24_32, _f32, f32_pair_clip ----
    ("x86_64", "encode: 16-bit scale 2^14", "mov $0x47000000, %eax", "mov $0x46800000, %eax"),
    ("x86_64", "encode: 16-bit high clamp 32768", "mov $0x46fffe00, %eax", "mov $0x47000000, %eax"),
    ("x86_64", "encode: 24-bit high clamp 8388608", "mov $0x4afffffe, %eax", "mov $0x4b000000, %eax"),
    ("x86_64", "encode avx2: 16-bit samples not interleaved right (right shifted into the left)", "vpslld $16, %ymm1, %ymm1\\n   vpblendw $0xaa, %ymm1, %ymm0, %ymm0", "vpslld $16, %ymm0, %ymm0\\n   vpblendw $0xaa, %ymm0, %ymm1, %ymm0"),
    ("x86_64", "encode avx2: a NaN left to the conversion", "vcmpordps %ymm0, %ymm0, %ymm2\\n   vmulps %ymm13, %ymm0, %ymm0\\n   vandps %ymm2, %ymm0, %ymm0", "vcmpordps %ymm0, %ymm0, %ymm2\\n   vmulps %ymm13, %ymm0, %ymm0"),
    ("x86_64", "encode avx2: the top of 32 bits not turned back", "vcvtps2dq %ymm0, %ymm0\\n   vpxor %ymm2, %ymm0, %ymm0", "vcvtps2dq %ymm0, %ymm0\\n   vpxor %ymm0, %ymm0, %ymm0"),
    ("x86_64", "encode avx2: window at the end one frame off", "lea -8(%rcx), %r9", "lea -7(%rcx), %r9"),
    ("x86_64", "encode avx2: 32-bit frames' halves swapped", "vperm2f128 $0x20, %ymm3, %ymm2, %ymm0\\n   vperm2f128 $0x31, %ymm3, %ymm2, %ymm1", "vperm2f128 $0x31, %ymm3, %ymm2, %ymm0\\n   vperm2f128 $0x20, %ymm3, %ymm2, %ymm1"),
    ("x86_64", "encode sse2: 16-bit words unpacked the wrong way", "punpcklwd %xmm1, %xmm0\\n   movdqu %xmm0", "punpcklwd %xmm0, %xmm0\\n   movdqu %xmm0"),
    ("x86_64", "encode sse2: the top of 32 bits not turned back", "cvtps2dq %xmm0, %xmm0\\n   pxor %xmm2, %xmm0", "cvtps2dq %xmm0, %xmm0\\n   pxor %xmm0, %xmm0"),
    ("x86_64", "encode sse2: scalar right sample at the left's place", "mov %ax, (%rdi)\\n   mov %r8w, 2(%rdi)", "mov %ax, (%rdi)\\n   mov %ax, 2(%rdi)"),
    ("x86_64", "clip avx2: a frame counted for the left plane only", "vcmpgtps %ymm13, %ymm3, %ymm3\\n   vorps %ymm3, %ymm2, %ymm2", "vcmpgtps %ymm13, %ymm3, %ymm3\\n   vorps %ymm2, %ymm2, %ymm2"),
    ("x86_64", "clip avx2: not clamped past full scale on the negative side", "vmaxps %ymm0, %ymm14, %ymm0\\n   vminps %ymm1, %ymm13, %ymm1", "vminps %ymm0, %ymm13, %ymm0\\n   vminps %ymm1, %ymm13, %ymm1"),
    ("x86_64", "clip avx2: the last window not clamped", "jnz .Laudio_clip_y8_over_last", "jmp .Laudio_clip_ydone"),
    ("x86_64", "clip sse2: the count of a mask's bits wrong", "shr $1, %ecx\\n   and $0x5, %ecx\\n   and $0x5, %eax\\n   add %ecx, %eax", "shr $1, %ecx\\n   and $0x5, %ecx\\n   and $0x4, %eax\\n   add %ecx, %eax"),
    ("x86_64", "clip sse2: a NaN clamped to one", "movaps %xmm13, %xmm4\\n   minps %xmm0, %xmm4\\n   movaps %xmm14, %xmm0\\n   maxps %xmm4, %xmm0", "movaps %xmm0, %xmm4\\n   minps %xmm13, %xmm4\\n   movaps %xmm14, %xmm0\\n   maxps %xmm4, %xmm0"),
    ("arm64", "encode neon: 16-bit not saturating (xtn for sqxtn)", "sqxtn v4.4h, v0.4s\\n   sqxtn2 v4.8h, v2.4s", "xtn v4.4h, v0.4s\\n   xtn2 v4.8h, v2.4s"),
    ("arm64", "encode neon: ties away from zero", "fcvtns v0.4s, v0.4s", "fcvtas v0.4s, v0.4s"),
    ("arm64", "encode neon: 24-bit not held to its top", "smin v0.4s, v0.4s, v29.4s", "smin v0.4s, v0.4s, v30.4s"),
    ("arm64", "encode neon: the fours' right plane taken from the left", "ld1 {v0.4s}, [x1], #16\\n   ld1 {v1.4s}, [x2], #16", "ld1 {v0.4s}, [x1], #16\\n   ld1 {v1.4s}, [x1], #16"),
    ("arm64", "encode neon: float clamp lost", "fmin v0.4s, v0.4s, v29.4s", "fmax v0.4s, v0.4s, v29.4s"),
    ("arm64", "clip neon: the count lost", "sub v27.4s, v27.4s, v2.4s", "sub v27.4s, v27.4s, v27.4s"),
    ("arm64", "clip neon: right plane not stored", "str q1, [x1], #16\\n   subs x3, x3, #1", "add x1, x1, #16\\n   subs x3, x3, #1"),
    ("riscv64", "encode rv scalar: a NaN is not zero", "fcvt.w.s t1, ft7, rne\\n   bnez t0, 7f\\n   li t1, 0\\n7:", "fcvt.w.s t1, ft7, rne\\n7:"),
    ("riscv64", "encode rv scalar: 16 bits not held to their top", "fmul.s ft7, ft0, ft8\\n   fmin.s ft7, ft7, ft10\\n", "fmul.s ft7, ft0, ft8\\n"),
    ("riscv64", "encode rv vector: 16 bits' narrowing from the wrong register", "vncvt.x.x.w v18, v12", "vncvt.x.x.w v18, v8"),
    ("riscv64", "encode rv vector: a NaN not zeroed", "vfcvt.x.f.v v8, v8\\n   vmerge.vxm v8, v8, zero, v0\\n   ", "vfcvt.x.f.v v8, v8\\n   "),
    ("riscv64", "encode rv vector: float NaN not put back", "vmerge.vvm v8, v8, v16, v0", "vmerge.vvm v8, v8, v8, v0"),
    ("riscv64", "clip rv vector: left plane stored under the right's mask", "vmv1r.v v0, v1\\n   vse32.v v16, (a3), v0.t", "vmv1r.v v0, v2\\n   vse32.v v16, (a3), v0.t"),
    ("riscv64", "clip rv scalar: the low side not clamped", "flt.s t0, ft0, ft9\\n   beqz t0, 6f\\n   fmv.s ft0, ft9", "flt.s t0, ft0, ft9\\n   beqz t0, 6f"),
    # ---- f32_rows_mix2 ----
    ("x86_64", "mix2 avx2: second plane's weight from the first's", "vbroadcastss 32(%r8), %ymm15\\n   xor %r11d, %r11d\\n   test %rax, %rax\\n   jz .Laudio_mix2_y_tail1", "vbroadcastss (%r8), %ymm15\\n   xor %r11d, %r11d\\n   test %rax, %rax\\n   jz .Laudio_mix2_y_tail1"),
    ("x86_64", "mix2 avx2: later channels not added to the outputs", "vaddps (%rdi,%r11), %ymm3, %ymm3", "vaddps %ymm5, %ymm3, %ymm3"),
    ("x86_64", "mix2 avx2: the first term not added to zero", "vmulps %ymm2, %ymm14, %ymm3\\n   vaddps %ymm5, %ymm3, %ymm3", "vmulps %ymm2, %ymm14, %ymm3"),
    ("x86_64", "mix2: stride in floats taken for bytes", "shl $2, %rcx\\n   shl $2, %r9", "shl $2, %r9"),
    ("x86_64", "mix2: the next channel's row is the same row", "lea (%rdx,%rcx), %rdx\\n   add $4, %r8\\n   vbroadcastss", "lea (%rdx), %rdx\\n   add $4, %r8\\n   vbroadcastss"),
    ("x86_64", "mix2 avx2: the frames left over past eight lost", "cmp %r9, %r11\\n   jae .Laudio_mix2_y_next\\n   vmovss (%rdx,%r11), %xmm2\\n   vmovss 0(%r8)", "jmp .Laudio_mix2_y_next\\n   vmovss (%rdx,%r11), %xmm2\\n   vmovss 0(%r8)"),
    ("x86_64", "mix2 sse2: second plane's weight from the first's", "movss 32(%r8), %xmm15\\n   shufps $0, %xmm15, %xmm15\\n   xor %r11d, %r11d\\n   test %rax, %rax\\n   jz .Laudio_mix2_x_tail1", "movss (%r8), %xmm15\\n   shufps $0, %xmm15, %xmm15\\n   xor %r11d, %r11d\\n   test %rax, %rax\\n   jz .Laudio_mix2_x_tail1"),
    ("x86_64", "mix2 sse2: a channel's pass adds into the first plane twice", "movups (%rsi,%r11), %xmm7\\n   addps %xmm7, %xmm2", "movups (%rdi,%r11), %xmm7\\n   addps %xmm7, %xmm2"),
    ("arm64", "mix2 neon: second plane's weight from the first's (first channel of sixteen)", "\"10: mov x7, x13\\n   mov x8, x4\\n   sub x10, x6, #1\\n   ldp q2, q3, [x7]\\n   ldp q4, q5, [x7, #32]\\n   ldr s0, [x8]\\n   ldr s1, [x8, #32]", "\"10: mov x7, x13\\n   mov x8, x4\\n   sub x10, x6, #1\\n   ldp q2, q3, [x7]\\n   ldp q4, q5, [x7, #32]\\n   ldr s0, [x8]\\n   ldr s1, [x8]"),
    ("arm64", "mix2 neon: second plane's weight from the first's (later channels of sixteen)", "\"11: add x7, x7, x3\\n   add x8, x8, #4\\n   ldp q2, q3, [x7]\\n   ldp q4, q5, [x7, #32]\\n   ldr s0, [x8]\\n   ldr s1, [x8, #32]", "\"11: add x7, x7, x3\\n   add x8, x8, #4\\n   ldp q2, q3, [x7]\\n   ldp q4, q5, [x7, #32]\\n   ldr s0, [x8]\\n   ldr s1, [x8]"),
    ("arm64", "mix2 neon: second plane's weight from the first's (four frames)", "\"22: add x7, x7, x3\\n   add x8, x8, #4\\n   ldr q2, [x7]\\n   ldr s0, [x8]\\n   ldr s1, [x8, #32]", "\"22: add x7, x7, x3\\n   add x8, x8, #4\\n   ldr q2, [x7]\\n   ldr s0, [x8]\\n   ldr s1, [x8]"),
    ("arm64", "mix2 neon: second plane's weight from the first's (one frame)", "\"32: add x7, x7, x3\\n   add x8, x8, #4\\n   ldr s2, [x7]\\n   ldr s0, [x8]\\n   ldr s1, [x8, #32]", "\"32: add x7, x7, x3\\n   add x8, x8, #4\\n   ldr s2, [x7]\\n   ldr s0, [x8]\\n   ldr s1, [x8]"),
    ("arm64", "mix2 neon: weights two floats on a channel (sixteen)", "\"11: add x7, x7, x3\\n   add x8, x8, #4", "\"11: add x7, x7, x3\\n   add x8, x8, #8"),
    ("arm64", "mix2 neon: the next channel's row is the same row (four frames)", "\"22: add x7, x7, x3", "\"22: add x7, x7, #0"),
    ("arm64", "mix2 neon: later channels not added to the sums (sixteen)", "fadd v16.4s, v24.4s, v16.4s\\n   fadd v17.4s", "fadd v16.4s, v24.4s, v24.4s\\n   fadd v17.4s"),
    ("arm64", "mix2 neon: later channels not added to the sums (four frames)", "fadd v20.4s, v28.4s, v20.4s\\n   subs x10, x10, #1\\n   b.ne 22b", "fadd v20.4s, v28.4s, v28.4s\\n   subs x10, x10, #1\\n   b.ne 22b"),
    ("arm64", "mix2 neon: later channels not added to the sums (one frame)", "fadd s20, s28, s20\\n   subs x10", "fadd s20, s28, s28\\n   subs x10"),
    ("arm64", "mix2 neon: the first term not added to zero (sixteen)", "fadd v16.4s, v16.4s, v6.4s\\n   fadd v17.4s, v17.4s, v6.4s", "fadd v17.4s, v17.4s, v6.4s"),
    ("arm64", "mix2 neon: the first term not added to zero (four frames)", "fadd v16.4s, v16.4s, v6.4s\\n   fadd v20.4s, v20.4s, v6.4s\\n   cbz x10, 23f", "fadd v20.4s, v20.4s, v6.4s\\n   cbz x10, 23f"),
    ("arm64", "mix2 neon: the first term not added to zero (one frame)", "fadd s16, s16, s6\\n   fadd s20, s20, s6", "fadd s20, s20, s6"),
    ("arm64", "mix2 neon: the sixteen-frame turns counted by thirty-two", "lsr x12, x5, #4\\n   cbz x12, 20f", "lsr x12, x5, #5\\n   cbz x12, 20f"),
    ("arm64", "mix2 neon: the fours left over lost", "\"20: and x12, x5, #15", "\"20: b 30f\\n   and x12, x5, #15"),
    ("arm64", "mix2 neon: the ones left over lost", "\"30: and x12, x5, #3", "\"30: b 9f\\n   and x12, x5, #3"),
    ("arm64", "mix2 neon: stride taken in floats for bytes", "\"1:  lsl x3, x3, #2", "\"1:  lsl x3, x3, #0"),
    ("arm64", "mix2 neon: the next sixteen frames start a vector early", "add x13, x13, #64", "add x13, x13, #48"),
    ("arm64", "abi: the remix in the callee-saved v14", "\"11: add x7, x7, x3", "\"11: movi v14.4s, #0\\n   add x7, x7, x3"),
    ("riscv64", "mix2 rv scalar: second plane's weight from the first's", "2:  flw ft0, 0(a4)\\n   flw ft1, 32(a4)", "2:  flw ft0, 0(a4)\\n   flw ft1, 0(a4)"),
    ("riscv64", "mix2 rv scalar: the first term not added to zero", "4:  fadd.s ft3, ft3, ft5\\n   fadd.s ft4, ft4, ft5", "4:  fmv.s ft3, ft3\\n   fadd.s ft4, ft4, ft5"),
    ("riscv64", "mix2 rv vector: the second output loaded from the first", "vle32.v v24, (t2)", "vle32.v v24, (t1)"),
    ("riscv64", "mix2 rv vector: the first term not added to zero", "33: vfadd.vf v12, v12, ft5\\n   vfadd.vf v16, v16, ft5", "33: vfadd.vf v16, v16, ft5"),
    # ---- the registers a routine keeps ----
    ("x86_64", "abi: rbx used by a routine that does not save it", "ASM_FUNC(f32_scale_peak)\n    \"test %rdx, %rdx\\n   jz .Laudio_scale_peak_none\\n\"", "ASM_FUNC(f32_scale_peak)\n    \"xor %ebx, %ebx\\n   test %rdx, %rdx\\n   jz .Laudio_scale_peak_none\\n\""),
    ("arm64", "abi: d14 used by a routine that does not save it", "5:  and x3, x3, #3\\n   cbz x3, 9f\\n\"\n    \"6:  ldr s0, [x1], #4\\n   ldr s1, [x2], #4", "5:  and x3, x3, #3\\n   cbz x3, 9f\\n   movi v14.4s, #0\\n\"\n    \"6:  ldr s0, [x1], #4\\n   ldr s1, [x2], #4"),
    ("riscv64", "abi: s1 used by a routine that does not save it", "ASM_FUNC(f32_pair_clip)\n    \"mv a3, a0\\n   li a0, 0\\n   beqz a2, 9f\\n\"", "ASM_FUNC(f32_pair_clip)\n    \"li s1, 7\\n   mv a3, a0\\n   li a0, 0\\n   beqz a2, 9f\\n\""),
]


def build_and_run(index):
    machine, name, old, new = M[index]
    tree = os.path.join(work, str(index))
    os.makedirs(os.path.join(tree, 'src'))
    os.makedirs(os.path.join(tree, 'test'))
    for entry in os.listdir(os.path.join(root, 'src')):
        if entry != 'lib.c':
            os.symlink(os.path.join(root, 'src', entry), os.path.join(tree, 'src', entry))
    os.symlink(os.path.join(root, 'test', 'checks.c'), os.path.join(tree, 'test', 'checks.c'))
    text = open(os.path.join(root, 'src', 'lib.c')).read()
    if text.count(old) == 0:
        return name, 'NOT FOUND', ''
    open(os.path.join(tree, 'src', 'lib.c'), 'w').write(text.replace(old, new))
    binary = os.path.join(tree, 'check')
    command = COMPILER[machine][:1] + ['-O2', '-static', '-nostdlib', '-nostartfiles', '-fno-stack-protector', '-fno-builtin'] + COMPILER[machine][1:] + [
        '-w', '-T', os.path.join(root, 'src/build/spark.ld'), '-Wl,-e,_start', '-Wl,--build-id=none', '-Wl,--no-warn-rwx-segments',
        '-DCHECK_audiolib', '-o', binary, os.path.join(tree, 'test', 'checks.c')]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode:
        return name, 'DID NOT BUILD', built.stderr[-400:]
    for runner in RUN[machine]:
        try:
            ran = subprocess.run(runner + [binary], capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired as late:
            said = [line for line in (late.stdout or b'').decode(errors='replace').splitlines() if 'FAIL' in line]
            return name, 'killed (timeout)', (' '.join(runner[1:]) + ': ' + said[0].strip()) if said else ''
        if ran.returncode != 0 or 'FAIL' in ran.stdout:
            said = [line for line in ran.stdout.splitlines() if 'FAIL' in line] or ran.stdout.strip().splitlines() or ['(' + str(ran.returncode) + ')']
            return name, 'killed', ' '.join(runner[1:]) + ': ' + said[0].strip()
    return name, 'SURVIVED', ''


def main():
    chosen = [i for i, m in enumerate(M) if not only or any(o in m[1] for o in only)]
    survived = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for name, state, detail in pool.map(build_and_run, chosen):
            print('%-12s %s %s' % (state, name, detail))
            survived += state in ('SURVIVED', 'NOT FOUND', 'DID NOT BUILD')
    print('%d mutations, %d not killed' % (len(chosen), survived))
    return 1 if survived else 0


sys.exit(main())
