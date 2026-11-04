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
        except subprocess.TimeoutExpired:
            return name, 'killed (timeout)', ''
        if ran.returncode != 0 or 'FAIL' in ran.stdout:
            return name, 'killed', ' '.join(runner[1:]) + ': ' + (ran.stdout.strip().splitlines() or ['(' + str(ran.returncode) + ')'])[0]
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
