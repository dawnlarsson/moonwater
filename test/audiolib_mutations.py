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
    # ---- resample_polyphase2_f32_rows ----
    ("x86_64", "rows avx2: the row after the one named", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n   add %r9, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n"),
    ("x86_64", "rows avx2: the product taken at a bit lower", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $31, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n"),
    ("x86_64", "rows avx2: the index taken at a bit lower", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $31, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n"),
    ("x86_64", "rows avx2: the fraction taken whole (pos for its low half)", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11, %r15\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n"),
    ("x86_64", "rows avx2: the second plane from the first", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_y_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rdx,%rax,4), %rbx\\n"),
    ("x86_64", "rows avx2: the second vector summed into the first accumulator", "vfmadd231ps (%r12,%rbp), %ymm0, %ymm10\\n   vfmadd231ps 32(%r12,%rbp), %ymm1, %ymm11\\n\"", "vfmadd231ps (%r12,%rbp), %ymm0, %ymm10\\n   vfmadd231ps 32(%r12,%rbp), %ymm1, %ymm10\\n\""),
    ("x86_64", "rows avx2: the last vector of the second plane not summed", "vfmadd231ps 192(%rbx,%rbp), %ymm6, %ymm12\\n   vfmadd231ps 224(%rbx,%rbp), %ymm7, %ymm13\\n\"", "vfmadd231ps 192(%rbx,%rbp), %ymm6, %ymm12\\n\""),
    ("x86_64", "rows avx2: a turn of 32 taps for 64", "add $256, %rbp\\n   cmp %r9, %rbp\\n   jb .Laudio_rows_y_tap", "add $128, %rbp\\n   cmp %r9, %rbp\\n   jb .Laudio_rows_y_tap"),
    ("x86_64", "rows avx2: the walk steps by phases", "add $4, %rdi\\n   add $4, %rsi\\n   add %r14, %r11\\n   dec %r10\\n   jnz .Laudio_rows_y_frame", "add $4, %rdi\\n   add $4, %rsi\\n   add %r13, %r11\\n   dec %r10\\n   jnz .Laudio_rows_y_frame"),
    ("x86_64", "rows avx2: the answer is the step", "\"vzeroupper\\n   mov %r11, %rax", "\"vzeroupper\\n   mov %r14, %rax"),
    ("x86_64", "rows: a count of nothing answers nothing", "jnz .Laudio_rows_go\\n   mov 16(%rsp), %rax\\n", "jnz .Laudio_rows_go\\n   xor %eax, %eax\\n"),
    ("x86_64", "abi: the rows routine returns with rbx cleared", "pop %rbp\\n   pop %rbx\\n\" ASM_RET\n    \".Laudio_rows_sse2:", "pop %rbp\\n   pop %rbx\\n   xor %ebx, %ebx\\n\" ASM_RET\n    \".Laudio_rows_sse2:"),
    ("x86_64", "rows sse2: the row after the one named", ".Laudio_rows_s_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_s_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n   add %r9, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n"),
    ("x86_64", "rows sse2: the second plane from the first", ".Laudio_rows_s_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rcx,%rax,4), %rbx\\n", ".Laudio_rows_s_frame:\\n\"\n    \"mov %r11, %rax\\n   shr $32, %rax\\n   mov %r11d, %r15d\\n   imul %r13, %r15\\n   shr $32, %r15\\n   imul %r9, %r15\\n   add %r8, %r15\\n\"\n    \"lea (%rdx,%rax,4), %r12\\n   lea (%rdx,%rax,4), %rbx\\n"),
    ("x86_64", "rows sse2: the answer is the step", "\"mov %r11, %rax\\n   pop %r15", "\"mov %r14, %rax\\n   pop %r15"),
    ("x86_64", "rows sse2: the second half of a group the first", "\"movups \" lo \"(%r15,%rbp), %xmm2\\n   movups \" hi \"(%r15,%rbp), %xmm3\\n\"", "\"movups \" lo \"(%r15,%rbp), %xmm2\\n   movups \" lo \"(%r15,%rbp), %xmm3\\n\""),
    ("x86_64", "rows sse2: the walk steps by phases", "add $4, %rdi\\n   add $4, %rsi\\n   add %r14, %r11\\n   dec %r10\\n   jnz .Laudio_rows_s_frame", "add $4, %rdi\\n   add $4, %rsi\\n   add %r13, %r11\\n   dec %r10\\n   jnz .Laudio_rows_s_frame"),
    ("arm64", "rows neon: the row after the one named", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x12, x12, x5\\n   add x14, x2, x10, lsl #2"),
    ("arm64", "rows neon: the fraction taken whole", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov x11, x7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2"),
    ("arm64", "rows neon: the product taken at a bit lower", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #31\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2"),
    ("arm64", "rows neon: a count of nothing walks a frame", "ldp x8, x9, [sp]\\n   cbz x9, 9f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2", "ldp x8, x9, [sp]\\n   cbz x9, 1f\\n   lsl x5, x5, #2\\n\"\n    \".balign 16\\n1:  lsr x10, x7, #32\\n   mov w11, w7\\n   mul x11, x11, x6\\n   lsr x12, x11, #32\\n\"\n    \"madd x12, x12, x5, x4\\n   add x14, x2, x10, lsl #2"),
    ("arm64", "rows neon: the second pair of the first plane summed into the first accumulator", "2:  ldp q0, q1, [x12], #32\\n   ldp q2, q3, [x12], #32\\n   ldp q16, q17, [x14], #32\\n   ldp q18, q19, [x14], #32\\n\"\n    \"fmla v20.4s, v0.4s, v16.4s\\n   fmla v21.4s, v1.4s, v17.4s", "2:  ldp q0, q1, [x12], #32\\n   ldp q2, q3, [x12], #32\\n   ldp q16, q17, [x14], #32\\n   ldp q18, q19, [x14], #32\\n\"\n    \"fmla v20.4s, v0.4s, v16.4s\\n   fmla v20.4s, v1.4s, v17.4s"),
    ("arm64", "rows neon: the first plane read at the second's place", "2:  ldp q0, q1, [x12], #32\\n   ldp q2, q3, [x12], #32\\n   ldp q16, q17, [x14], #32\\n   ldp q18, q19, [x14], #32\\n\"\n    \"fmla v20.4s, v0.4s, v16.4s\\n   fmla v21.4s, v1.4s, v17.4s", "2:  ldp q0, q1, [x12], #32\\n   ldp q2, q3, [x12], #32\\n   ldp q16, q17, [x15], #32\\n   ldp q18, q19, [x14], #32\\n\"\n    \"fmla v20.4s, v0.4s, v16.4s\\n   fmla v21.4s, v1.4s, v17.4s"),
    ("arm64", "abi: the rows routine in the callee-saved v14", "2:  ldp q0, q1, [x12], #32\\n   ldp q2, q3, [x12], #32\\n   ldp q16, q17, [x14], #32\\n   ldp q18, q19, [x14], #32\\n\"\n    \"fmla v20.4s, v0.4s, v16.4s\\n   fmla v21.4s, v1.4s, v17.4s", "2:  movi v14.4s, #0\\n   ldp q0, q1, [x12], #32\\n   ldp q2, q3, [x12], #32\\n   ldp q16, q17, [x14], #32\\n   ldp q18, q19, [x14], #32\\n\"\n    \"fmla v20.4s, v0.4s, v16.4s\\n   fmla v21.4s, v1.4s, v17.4s"),
    ("arm64", "rows neon: the answer is the step", "9:  mov x0, x7\\n   ", "9:  mov x0, x8\\n   "),
    ("riscv64", "rows rv scalar: the row after the one named", "srli t3, t0, 32\\n   mul t3, t3, a5\\n   add t3, t3, a4\\n\"\n    \"fmv.w.x fa0, zero", "srli t3, t0, 32\\n   mul t3, t3, a5\\n   add t3, t3, a4\\n   add t3, t3, a5\\n\"\n    \"fmv.w.x fa0, zero"),
    ("riscv64", "rows rv scalar: the product taken at a bit lower", "srli t3, t0, 32\\n   mul t3, t3, a5\\n   add t3, t3, a4\\n\"\n    \"fmv.w.x fa0, zero", "srli t3, t0, 31\\n   mul t3, t3, a5\\n   add t3, t3, a4\\n\"\n    \"fmv.w.x fa0, zero"),
    ("riscv64", "rows rv vector: the row after the one named", "srli t3, t0, 32\\n   slli t4, a5, 2\\n   mul t3, t3, t4\\n   add t3, t3, a4\\n\"\n    \"vsetvli t0, zero, e32, m4, ta, ma", "srli t3, t0, 32\\n   slli t4, a5, 2\\n   mul t3, t3, t4\\n   add t3, t3, a4\\n   add t3, t3, t4\\n\"\n    \"vsetvli t0, zero, e32, m4, ta, ma"),
    ("riscv64", "rows rv vector: the second plane from the first", "vle32.v v0, (t3)\\n   vle32.v v8, (t1)\\n   vfmacc.vv v16, v0, v8\\n   vle32.v v12, (t2)", "vle32.v v0, (t3)\\n   vle32.v v8, (t1)\\n   vfmacc.vv v16, v0, v8\\n   vle32.v v12, (t1)"),
    ("riscv64", "rows rv scalar: the answer is the step", "9:  mv a0, a7\\n\" ASM_RET", "9:  mv a0, a6\\n\" ASM_RET"),
    ("riscv64", "rows rv vector: the answer is the step", "bnez t6, 21b\\n   mv a0, a7", "bnez t6, 21b\\n   mv a0, a6"),
    ("riscv64", "abi: the rows routine in the saved s1", "ASM_FUNC(resample_polyphase2_f32_rows)\n    \"ld t5, 8(sp)\\n   beqz t5, 9f", "ASM_FUNC(resample_polyphase2_f32_rows)\n    \"li s1, 7\\n   ld t5, 8(sp)\\n   beqz t5, 9f"),
    # ---- multi: f32_planes_from_s16, _s32, _f32 for 3 to 8 channels ----
    ("x86_64", "multi s16 avx2: the second four frames from two frames on", ".Laudio_decode_s16_m_a:\\n   lea (%rsi,%r8,4), %r9", ".Laudio_decode_s16_m_a:\\n   lea (%rsi,%r8,2), %r9"),
    ("x86_64", "multi s16 avx2: the second eight-channel half of the frames from two frames on", ".Laudio_decode_s16_m_b:\\n   lea (%rsi,%r8,4), %r9", ".Laudio_decode_s16_m_b:\\n   lea (%rsi,%r8,2), %r9"),
    ("x86_64", "multi s16 avx2: a plane taken from the wrong column (3 and 4 channels)", "vunpckhpd %ymm6, %ymm4, %ymm9", "vunpcklpd %ymm6, %ymm4, %ymm9"),
    ("x86_64", "multi s16 avx2: a fourth plane stored for three channels", "cmp $3, %ecx\\n   je .Laudio_decode_s16_m_a_next", "cmp $4, %ecx\\n   je .Laudio_decode_s16_m_a_next"),
    ("x86_64", "multi s16 avx2: the outputs advance by half a block (3 and 4 channels)", ".Laudio_decode_s16_m_a_next:\\n   add $32, %r11", ".Laudio_decode_s16_m_a_next:\\n   add $16, %r11"),
    ("x86_64", "multi s16 avx2: the transpose takes the high lanes twice (5 to 8 channels)", "vperm2f128 $0x31, %ymm4, %ymm0, %ymm12", "vperm2f128 $0x20, %ymm4, %ymm0, %ymm12"),
    ("x86_64", "multi s16 avx2: the first shuffle takes the wrong pair", "vshufps $0x44, %ymm10, %ymm8, %ymm0", "vshufps $0xee, %ymm10, %ymm8, %ymm0"),
    ("x86_64", "multi s16 avx2: seven channels stored as six", "cmp $7, %ecx\\n   je .Laudio_decode_s16_m_b_next", "cmp $8, %ecx\\n   je .Laudio_decode_s16_m_b_next"),
    ("x86_64", "multi s16 avx2: the threshold is a frame short", ".Laudio_decode_s16_m:\\n   mov $9, %eax", ".Laudio_decode_s16_m:\\n   mov $8, %eax"),
    ("x86_64", "multi s16 avx2: the threshold of eight channels two frames short", "cmp $8, %ecx\\n   jne .Laudio_decode_s16_m_t8\\n   dec %eax", "cmp $8, %ecx\\n   jne .Laudio_decode_s16_m_t8\\n   sub $2, %eax"),
    ("x86_64", "multi s16 avx2: the tail reads channels from the wrong place", "lea (%rsi,%r9,2), %rax\\n   mov %rdx, %r12", "lea (%rsi,%r9,4), %rax\\n   mov %rdx, %r12"),
    ("x86_64", "multi s16 avx2: the tail writes at the start of the planes", "mov (%rdi,%r9,8), %r10\\n   add %r11, %r10", "mov (%rdi,%r9,8), %r10\\n   add $0, %r10"),
    ("x86_64", "multi s32 avx2: the tail leaves the scale out", "vmulss .Laudio_decode_s32_scale(%rip), %xmm0, %xmm0\\n   vmovss %xmm0, (%r10)", "vmovss %xmm0, (%r10)"),
    ("x86_64", "multi s32 avx2: the blocks take the scale of s16", "vbroadcastss .Laudio_decode_s32_scale(%rip), %ymm0", "vbroadcastss .Laudio_decode_s16_scale(%rip), %ymm0"),
    ("x86_64", "multi f32 avx2: the lower bound taken as the upper (5 to 8 channels)", "vmaxps %ymm8, %ymm1, %ymm8", "vmaxps %ymm8, %ymm0, %ymm8"),
    ("x86_64", "multi f32 avx2: a NaN not made zero in the first plane", "vandps %ymm2, %ymm8, %ymm8", "vmovaps %ymm8, %ymm8"),
    ("x86_64", "multi f32 avx2: the tail leaves a NaN", "vandps %xmm1, %xmm0, %xmm0\\n   vmovss %xmm0, (%r10)", "vmovss %xmm0, (%r10)"),
    ("arm64", "multi neon s16: three channels count the blocks by sixteen frames", "110: ldp x4, x6, [x0]\\n   ldr x7, [x0, #16]\\n   lsr x5, x2, #3", "110: ldp x4, x6, [x0]\\n   ldr x7, [x0, #16]\\n   lsr x5, x2, #4"),
    ("arm64", "multi neon s16: three channels, the third plane to the second", "stp q20, q21, [x7], #32\\n   subs x5, x5, #1\\n   b.ne 111b", "stp q20, q21, [x6], #32\\n   subs x5, x5, #1\\n   b.ne 111b"),
    ("arm64", "multi neon s16: three channels, the frames done not counted", "119: ldr x9, [x0]\\n   sub x15, x4, x9\\n   and x2, x2, #7", "119: ldr x9, [x0]\\n   mov x15, #0\\n   and x2, x2, #7"),
    ("arm64", "multi neon s16: six channels, the even sample shifted by eight", "131: ld3 {v0.4s, v1.4s, v2.4s}, [x1], #48\\nshl v16.4s, v0.4s, #16", "131: ld3 {v0.4s, v1.4s, v2.4s}, [x1], #48\\nshl v16.4s, v0.4s, #8"),
    ("arm64", "multi neon s16: six channels, the odd sample taken unsigned", "sshr v17.4s, v0.4s, #16", "ushr v17.4s, v0.4s, #16"),
    ("arm64", "multi neon s16: eight channels, a plane stored to the wrong pointer", "str q23, [x13], #16", "str q23, [x12], #16"),
    ("arm64", "multi neon s32: six channels, the odd channel from the even", "uzp2 v17.4s, v0.4s, v3.4s", "uzp1 v17.4s, v0.4s, v3.4s"),
    ("arm64", "multi neon s32: eight channels, the second load of frames over the registers of the first", "141: ld4 {v0.2d, v1.2d, v2.2d, v3.2d}, [x1], #64\\n   ld4 {v4.2d, v5.2d, v6.2d, v7.2d}, [x1], #64", "141: ld4 {v0.2d, v1.2d, v2.2d, v3.2d}, [x1], #64\\n   ld4 {v0.2d, v1.2d, v2.2d, v3.2d}, [x1], #64"),
    ("arm64", "multi neon s32: three channels leave the scale out", "scvtf v1.4s, v1.4s, #31\\nscvtf v2.4s, v2.4s, #31\\nstr q0, [x4], #16", "scvtf v1.4s, v1.4s, #31\\nstr q0, [x4], #16"),
    ("arm64", "multi neon f32: three channels leave a NaN", "fmax v2.4s, v2.4s, v17.4s\\nand v2.16b, v2.16b, v18.16b\\nstr q0, [x4], #16", "fmax v2.4s, v2.4s, v17.4s\\nstr q0, [x4], #16"),
    ("arm64", "multi neon f32: eight channels clamp the first plane against the lower bound twice", "fmin v20.4s, v20.4s, v16.4s", "fmin v20.4s, v20.4s, v17.4s"),
    ("arm64", "multi neon: five channels sent to the six channel body", "100: mov x15, #0\\n   cmp x3, #3\\n   b.eq 110f\\n   cmp x3, #4\\n   b.eq 120f\\n   cmp x3, #6", "100: mov x15, #0\\n   cmp x3, #3\\n   b.eq 110f\\n   cmp x3, #4\\n   b.eq 120f\\n   cmp x3, #5"),
    ("arm64", "multi neon: the tail writes at the start of the planes", "151: ldr x4, [x0, x10, lsl #3]\\n   add x4, x4, x15", "151: ldr x4, [x0, x10, lsl #3]\\n   add x4, x4, #0"),
    ("arm64", "multi neon: the fours left over lost (six and eight channels)", "139: ldr x4, [x0]\\n   sub x15, x6, x4\\n   and x2, x2, #3", "139: ldr x4, [x0]\\n   sub x15, x6, x4\\n   and x2, x2, #1"),
    # ---- multi, SSE2 ----
    ("x86_64", "multi sse2: the block reads a frame past the end", ".Laudio_decode_s32_ms:\\n   mov $5, %eax", ".Laudio_decode_s32_ms:\\n   mov $4, %eax"),
    ("x86_64", "multi sse2: the first plane takes the wrong halves", "movlhps %xmm5, %xmm4", "movhlps %xmm5, %xmm4"),
    ("x86_64", "multi sse2: the second plane takes the wrong halves", "movhlps %xmm6, %xmm5", "movlhps %xmm6, %xmm5"),
    ("x86_64", "multi sse2: the second group of channels read from the first", "movups 16(%rsi), %xmm0", "movups 0(%rsi), %xmm0"),
    ("x86_64", "multi sse2: a fourth plane stored for three channels", "je .Laudio_decode_s32_ms_a_next", "jmp .Laudio_decode_s32_ms_a_next"),
    ("x86_64", "multi sse2: a channel past the fifth always stored", "je .Laudio_decode_s32_ms_b_next", "jmp .Laudio_decode_s32_ms_b_next"),
    ("x86_64", "multi sse2: the blocks leave the scale out", "mulps %xmm8, %xmm4", "nop"),
    ("x86_64", "multi sse2: a NaN left in the first plane", "andps %xmm10, %xmm4", "movaps %xmm10, %xmm10"),
    ("x86_64", "multi sse2: sixteen-bit frames not sign-extended", "punpcklwd %xmm0, %xmm0\\n   psrad $16, %xmm0", "punpcklwd %xmm0, %xmm0\\n   psrld $16, %xmm0"),
    # ---- pcm_widen ----
    ("x86_64", "widen avx2: u8 bias left out", "vpslld $24, %ymm0, %ymm0\\n   vpxor %ymm7, %ymm0, %ymm0", "vpslld $24, %ymm0, %ymm0"),
    ("x86_64", "widen scalar: u8 bias left out", "movzbl (%rsi), %eax\\n   xor $0x80, %eax\\n   shl $24, %eax", "movzbl (%rsi), %eax\\n   shl $24, %eax"),
    ("x86_64", "widen avx2: s16 big-endian bytes the wrong way", ".Lpw_m16be:\\n   .byte 0x80, 0x80, 0x01, 0x00", ".Lpw_m16be:\\n   .byte 0x80, 0x80, 0x00, 0x01"),
    ("x86_64", "widen scalar: s16 big-endian not swapped", "movzwl (%rsi), %eax\\n   rol $8, %ax\\n   shl $16, %eax", "movzwl (%rsi), %eax\\n   shl $16, %eax"),
    ("x86_64", "widen avx2: s32 big-endian bytes the wrong way", ".Lpw_m32be:\\n   .byte 0x03, 0x02, 0x01, 0x00", ".Lpw_m32be:\\n   .byte 0x03, 0x02, 0x00, 0x01"),
    ("x86_64", "widen scalar: s32 big-endian not swapped", "mov (%rsi), %eax\\n   bswap %eax\\n   mov %eax, (%rdi)", "mov (%rsi), %eax\\n   mov %eax, (%rdi)"),
    ("x86_64", "widen avx2: s24 little-endian bytes wrong", ".Lpw_m24le:\\n   .byte 0x80, 0x00, 0x01, 0x02", ".Lpw_m24le:\\n   .byte 0x80, 0x00, 0x01, 0x01"),
    ("x86_64", "widen avx2: s24 big-endian bytes wrong", ".Lpw_m24be:\\n   .byte 0x80, 0x02, 0x01, 0x00", ".Lpw_m24be:\\n   .byte 0x80, 0x02, 0x00, 0x01"),
    ("x86_64", "widen avx2: s24 second half read from the wrong place", "vinserti128 $1, 12(%rsi), %ymm0, %ymm0", "vinserti128 $1, 8(%rsi), %ymm0, %ymm0"),
    ("x86_64", "widen avx2: s24 reads a vector past the end", "   cmp $10, %rdx\\n   jb .Lpw_k5_s", "   cmp $8, %rdx\\n   jb .Lpw_k5_s"),
    ("x86_64", "widen scalar: s24 little-endian top byte wrong", "movzbl 2(%rsi), %r8d\\n   shl $16, %r8d\\n   or %r8d, %eax\\n   shl $8, %eax", "movzbl 2(%rsi), %r8d\\n   shl $8, %r8d\\n   or %r8d, %eax\\n   shl $8, %eax"),
    ("x86_64", "widen scalar: s24 big-endian middle byte wrong", "movzbl 1(%rsi), %r8d\\n   shl $8, %r8d\\n   or %r8d, %eax\\n   movzbl 2(%rsi), %r8d", "movzbl 1(%rsi), %r8d\\n   shl $16, %r8d\\n   or %r8d, %eax\\n   movzbl 2(%rsi), %r8d"),
    ("x86_64", "widen avx2: s24_32 shifted by sixteen", "vpslld $8, %ymm0, %ymm0\\n   vmovdqu %ymm0, (%rdi)\\n   add $32, %rsi\\n   add $32, %rdi\\n   sub $8, %rdx\\n   cmp $8, %rdx\\n   jae .Lpw_k7_v", "vpslld $16, %ymm0, %ymm0\\n   vmovdqu %ymm0, (%rdi)\\n   add $32, %rsi\\n   add $32, %rdi\\n   sub $8, %rdx\\n   cmp $8, %rdx\\n   jae .Lpw_k7_v"),
    ("x86_64", "widen avx2: s24_32 big-endian bytes wrong", ".Lpw_m2432be:\\n   .byte 0x80, 0x03, 0x02, 0x01", ".Lpw_m2432be:\\n   .byte 0x80, 0x03, 0x02, 0x02"),
    ("x86_64", "widen scalar: s24_32 big-endian not shifted", "mov (%rsi), %eax\\n   bswap %eax\\n   shl $8, %eax", "mov (%rsi), %eax\\n   bswap %eax"),
    ("x86_64", "widen a count of nothing writes", "test %rdx, %rdx\\n   jz .Lpw_done\\n   cmp $6, %rcx", "test %rdx, %rdx\\n   cmp $6, %rcx"),
    ("x86_64", "widen avx2: the vector loop leaves its remainder out", "   vzeroupper\\n   test %rdx, %rdx\\n   jnz .Lpw_k4_s", "   vzeroupper\\n   test %rdx, %rdx\\n   jnz .Lpw_done"),
    ("arm64", "widen neon: u8 bias left out", "101: ld1 {v0.16b}, [x1], #16\\n   eor v0.16b, v0.16b, v31.16b", "101: ld1 {v0.16b}, [x1], #16"),
    ("arm64", "widen scalar: u8 bias left out", "108: ldrb w4, [x1], #1\\n   eor w4, w4, #0x80", "108: ldrb w4, [x1], #1"),
    ("arm64", "widen neon: s16 big-endian not swapped", "131: ld1 {v0.16b, v1.16b}, [x1], #32\\n   rev16 v0.16b, v0.16b", "131: ld1 {v0.16b, v1.16b}, [x1], #32\\n   mov v0.16b, v0.16b"),
    ("arm64", "widen scalar: s16 big-endian not swapped", "138: ldrh w4, [x1], #2\\n   rev16 w4, w4", "138: ldrh w4, [x1], #2\\n   mov w4, w4"),
    ("arm64", "widen neon: s32 big-endian not swapped", "141: ld1 {v4.16b, v5.16b, v6.16b, v7.16b}, [x1], #64\\n   rev32 v4.16b, v4.16b", "141: ld1 {v4.16b, v5.16b, v6.16b, v7.16b}, [x1], #64\\n   mov v4.16b, v4.16b"),
    ("arm64", "widen neon: s24 little-endian halves exchanged", "zip1 v16.8h, v3.8h, v5.8h\\n   zip2 v17.8h, v3.8h, v5.8h\\n   zip1 v18.8h, v4.8h, v6.8h\\n   zip2 v19.8h, v4.8h, v6.8h\\n   st1 {v16.4s, v17.4s, v18.4s, v19.4s}, [x0], #64\\n   sub x2, x2, #16\\n   cmp x2, #16\\n   b.hs 151b", "zip1 v16.8h, v5.8h, v3.8h\\n   zip2 v17.8h, v3.8h, v5.8h\\n   zip1 v18.8h, v4.8h, v6.8h\\n   zip2 v19.8h, v4.8h, v6.8h\\n   st1 {v16.4s, v17.4s, v18.4s, v19.4s}, [x0], #64\\n   sub x2, x2, #16\\n   cmp x2, #16\\n   b.hs 151b"),
    ("arm64", "widen neon: s24 big-endian high bytes exchanged", "zip1 v5.16b, v1.16b, v0.16b", "zip1 v5.16b, v0.16b, v1.16b"),
    ("arm64", "widen scalar: s24 little-endian top byte wrong", "158: ldrb w4, [x1]\\n   ldrb w5, [x1, #1]\\n   ldrb w6, [x1, #2]\\n   add x1, x1, #3\\n   lsl w4, w4, #8\\n   orr w4, w4, w5, lsl #16\\n   orr w4, w4, w6, lsl #24", "158: ldrb w4, [x1]\\n   ldrb w5, [x1, #1]\\n   ldrb w6, [x1, #2]\\n   add x1, x1, #3\\n   lsl w4, w4, #8\\n   orr w4, w4, w5, lsl #16\\n   orr w4, w4, w6, lsl #16"),
    ("arm64", "widen neon: s24_32 shifted by sixteen", "171: ld1 {v4.4s, v5.4s, v6.4s, v7.4s}, [x1], #64\\n   shl v4.4s, v4.4s, #8", "171: ld1 {v4.4s, v5.4s, v6.4s, v7.4s}, [x1], #64\\n   shl v4.4s, v4.4s, #16"),
    ("arm64", "widen neon: s24_32 big-endian not swapped", "181: ld1 {v4.16b, v5.16b, v6.16b, v7.16b}, [x1], #64\\n   rev32 v4.16b, v4.16b\\n   shl v4.4s, v4.4s, #8", "181: ld1 {v4.16b, v5.16b, v6.16b, v7.16b}, [x1], #64\\n   shl v4.4s, v4.4s, #8"),
    ("arm64", "widen neon: the vector loop reads eight past the end", "140: cmp x2, #16\\n   b.lo 149f", "140: cmp x2, #8\\n   b.lo 149f"),
    ("arm64", "widen neon: the scalar loop entered with nothing left", "109: cbz x2, 9f\\n\"\n    \"108: ldrb w4", "109: nop\\n\"\n    \"108: ldrb w4"),
    ("riscv64", "widen scalar: u8 bias left out", "lbu t4, 0(a1)\\n   xori t4, t4, 0x80", "lbu t4, 0(a1)\\n   xori t4, t4, 0x00"),
    ("riscv64", "widen vector: u8 bias left out", "vxor.vx v12, v12, t2\\n   vsll.vi v12, v12, 24", "vsll.vi v12, v12, 24"),
    ("riscv64", "widen vector: s16 big-endian low byte wrong", "vsll.vi v16, v16, 24\\n   vsrl.vi v20, v12, 8", "vsll.vi v16, v16, 16\\n   vsrl.vi v20, v12, 8"),
    ("riscv64", "widen scalar: s16 big-endian bytes the wrong way", "slli t0, t0, 8\\n   or t4, t0, t1\\n   slliw t4, t4, 16", "slli t1, t1, 8\\n   or t4, t0, t1\\n   slliw t4, t4, 16"),
    ("riscv64", "widen vector: s32 big-endian top byte wrong", "341: vsetvli t0, a2, e32, m4, ta, ma\\n   vle32.v v12, (a1)\\n   vsll.vi v16, v12, 24", "341: vsetvli t0, a2, e32, m4, ta, ma\\n   vle32.v v12, (a1)\\n   vsll.vi v16, v12, 16"),
    ("riscv64", "widen scalar: s32 big-endian top byte wrong", "srliw t0, t4, 24", "srliw t0, t4, 16"),
    ("riscv64", "widen vector: s24 little-endian top byte wrong", "vsll.vi v12, v12, 8\\n   vsll.vi v16, v16, 16\\n   vsll.vi v20, v20, 24", "vsll.vi v12, v12, 8\\n   vsll.vi v16, v16, 16\\n   vsll.vi v20, v20, 16"),
    ("riscv64", "widen vector: s24 advances by two a sample", "slli t1, t0, 1\\n   add t1, t1, t0\\n   add a1, a1, t1", "slli t1, t0, 1\\n   add a1, a1, t1"),
    ("riscv64", "widen scalar: s24 big-endian bytes the wrong way", "slli t2, t2, 8\\n   slli t1, t1, 16\\n   slli t0, t0, 24", "slli t0, t0, 8\\n   slli t1, t1, 16\\n   slli t2, t2, 24"),
    ("riscv64", "widen vector: s24_32 shifted by sixteen", "371: vsetvli t0, a2, e32, m4, ta, ma\\n   vle32.v v12, (a1)\\n   vsll.vi v12, v12, 8", "371: vsetvli t0, a2, e32, m4, ta, ma\\n   vle32.v v12, (a1)\\n   vsll.vi v12, v12, 16"),
    ("riscv64", "widen scalar: s24_32 big-endian not shifted", "slliw t4, t4, 8\\n   sw t4, 0(a0)\\n   addi a1, a1, 4\\n   addi a0, a0, 4\\n   addi a2, a2, -1\\n   bnez a2, 181b", "sw t4, 0(a0)\\n   addi a1, a1, 4\\n   addi a0, a0, 4\\n   addi a2, a2, -1\\n   bnez a2, 181b"),
    ("riscv64", "widen a count of nothing writes", "ASM_FUNC(pcm_widen)\n    \"beqz a2, 9f\\n   li t0, 6", "ASM_FUNC(pcm_widen)\n    \"li t0, 6"),
    ("x86_64", "pack24 sse2: the high half stored at the wrong place", "movhps %xmm0, 6(%rdi)", "movhps %xmm0, 8(%rdi)"),
    ("x86_64", "pack24 sse2: the low mask a byte short", ".Lpk_m1:\\n   .quad 0x0000000000ffffff", ".Lpk_m1:\\n   .quad 0x000000000000ffff"),
    ("x86_64", "pack24 sse2: the high mask the wrong bytes", ".Lpk_m2:\\n   .quad 0x0000ffffff000000", ".Lpk_m2:\\n   .quad 0x0000ffff00000000"),
    ("x86_64", "pack24 sse2: the shift of the high word wrong", "psrlq $8, %xmm1", "psrlq $16, %xmm1"),
    ("x86_64", "pack24 sse2: the vector loop runs to the last lane", "   sub $4, %rdx\\n   cmp $5, %rdx\\n   jae .Lpk_v", "   sub $4, %rdx\\n   cmp $4, %rdx\\n   jae .Lpk_v"),
    ("x86_64", "pack24 scalar: the last sample's top byte lost", "shr $16, %eax\\n   mov %al, 2(%rdi)", "shr $8, %eax\\n   mov %al, 2(%rdi)"),
    ("x86_64", "pack24 scalar: the samples before the last advance by four", "mov %eax, (%rdi)\\n   add $4, %rsi\\n   add $3, %rdi", "mov %eax, (%rdi)\\n   add $4, %rsi\\n   add $4, %rdi"),
    ("arm64", "pack24 neon: the third byte left out", "st3 {v0.16b, v1.16b, v2.16b}, [x0], #48", "st3 {v1.16b, v2.16b, v3.16b}, [x0], #48"),
    ("arm64", "pack24 neon: the vector loop reads a block past the end", "1:  ld4 {v0.16b, v1.16b, v2.16b, v3.16b}, [x1], #64\\n   st3 {v0.16b, v1.16b, v2.16b}, [x0], #48\\n   sub x2, x2, #16\\n   cmp x2, #16", "1:  ld4 {v0.16b, v1.16b, v2.16b, v3.16b}, [x1], #64\\n   st3 {v0.16b, v1.16b, v2.16b}, [x0], #48\\n   sub x2, x2, #16\\n   cmp x2, #0"),
    ("arm64", "pack24 scalar: the top byte from the wrong shift", "lsr w4, w4, #16\\n   strb w4, [x0, #2]", "lsr w4, w4, #8\\n   strb w4, [x0, #2]"),
    ("riscv64", "pack24 vector: the segment stored is the wrong one", "vsseg3e8.v v8, (a0)", "vsseg3e8.v v9, (a0)"),
    ("riscv64", "pack24 vector: the source advances by three a lane", "slli t1, t0, 2\\n   add a1, a1, t1\\n   slli t1, t0, 1", "slli t1, t0, 1\\n   add a1, a1, t1\\n   slli t1, t0, 1"),
    ("riscv64", "pack24 scalar: the middle byte from the low one", "lbu t1, 1(a1)\\n   lbu t2, 2(a1)\\n   sb t0, 0(a0)", "lbu t1, 0(a1)\\n   lbu t2, 2(a1)\\n   sb t0, 0(a0)"),
    ("x86_64", "law the other law's table taken", "test %r8, %r8\\n   setne %al", "test %r8, %r8\\n   sete %al"),
    ("x86_64", "law every plane reads the first channel", "lea (%rsi,%r9), %rax", "lea (%rsi), %rax"),
    ("x86_64", "law the third lookup from the second sample", "movzbl (%r13), %r14d", "movzbl (%rax,%rcx), %r14d"),
    ("x86_64", "law the source advances by two frames a turn", "lea (%rax,%rcx,4), %rax", "lea (%rax,%rcx,2), %rax"),
    ("x86_64", "law the third float stored over the fourth", "mov %r14d, 8(%r10)", "mov %r14d, 12(%r10)"),
    ("x86_64", "law the last samples not stored", "mov (%r11,%rbx,4), %ebx\\n   mov %ebx, (%r10)", "mov (%r11,%rbx,4), %ebx\\n   mov %ebx, 4(%r10)"),
    ("x86_64", "law a word of the A-law table wrong", ".Lfl_table:\\n   .long 0xbe2c0000", ".Lfl_table:\\n   .long 0xbe2c0001"),
    ("x86_64", "law the four lookups advance by three", "sub $4, %r8\\n   jmp .Lfl_4", "sub $3, %r8\\n   jmp .Lfl_4"),
    ("arm64", "law the second law not selected", "csel x10, x10, xzr, ne", "csel x10, xzr, x10, ne"),
    ("arm64", "law the second sample from the first", "ldrb w11, [x6]\\n   add x6, x6, x3\\n   ldrb w12, [x6]", "ldrb w11, [x6]\\n   add x6, x6, #0\\n   ldrb w12, [x6]"),
    ("arm64", "law the third lookup from the second sample", "ldr s2, [x9, x13, lsl #2]", "ldr s2, [x9, x12, lsl #2]"),
    ("arm64", "law the last samples advance by a byte", "5:  ldrb w11, [x6]\\n   add x6, x6, x3", "5:  ldrb w11, [x6]\\n   add x6, x6, #1"),
    ("arm64", "law the four stored a float apart wrong", "stp s2, s3, [x5, #8]", "stp s2, s3, [x5, #4]"),
    ("riscv64", "law the law taken the other way (scalar)", "lla t6, .Lfl_table\\n   beqz a4, 2f", "lla t6, .Lfl_table\\n   bnez a4, 2f"),
    ("riscv64", "law the law taken the other way (vector)", "beqz a4, 21f", "bnez a4, 21f"),
    ("riscv64", "law the index a byte (vector)", "vsll.vi v12, v12, 2\\n   vluxei32.v", "vsll.vi v12, v12, 3\\n   vluxei32.v"),
    ("riscv64", "law the source advances by a byte a sample (vector)", "mul t4, t0, a3", "mv t4, t0"),
    ("riscv64", "law the index a byte (scalar)", "lbu t0, 0(t2)\\n   slli t0, t0, 2", "lbu t0, 0(t2)\\n   slli t0, t0, 3"),
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
