; The assembler in the build against LLVM: every instruction of its table in every operand form it takes, in the
; statements the file gpu.asm is written with. test/gpu_kernels.py assembles this with llvm-mc and with `build gpu-kernels` and
; requires every word the same. It is never run on a GPU; it is only assembled.
.amdgcn_target "amdgcn-amd-amdhsa--gfx1036"
.text
.macro one
	s_mov_b32	s0, s1
.endm
.macro pair a, b
	s_mov_b32	\a, \b
.endm
.macro quad a, b, c, d
	pair	\a, \b
	pair	\c, \d
.endm
; arguments that are registers and ranges, constants, a literal; a comment inside a body
.macro mixed dst, lo, hi, imm
	s_mov_b64	\dst, \lo	; a comment in a body is not part of it
	s_and_b64	\dst, \dst, \hi
	s_add_u32	s4, s5, \imm
	v_mad_u32_u24	v1, v2, s3, \imm
.endm
.macro ladder base
	.irp	n, 1, 2, 3
	v_add_nc_u32	v\n, \base, v\n
	.endr
.endm
.macro sixteen
	.irp	n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	s_mov_b32	s0, s1
	.endr
.endm
.macro two56
	.irp	n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	sixteen
	.endr
.endm
.macro nothing
.endm
.macro mask out, a
	v_cmp_le_u32_e64	\out, s57, \a
	v_cmp_gt_u32	vcc, s53, \a
	s_and_b64	\out, \out, vcc
.endm
.macro cond out, in, t
	mask	s[64:65], \in
	v_cndmask_b32_e64	\out, \in, \t, s[64:65]
.endm

; SOP1, SOP2, SOPC: scalar and 64-bit registers, inline constants, literals, a literal that is an inline constant
.globl salu
.p2align 8
.type salu,@function
salu:
	s_mov_b32	s0, s0
	s_mov_b32	s0, s1
	s_mov_b32	s0, s7
	s_mov_b32	s0, s64
	s_mov_b32	s0, s104
	s_mov_b32	s0, s105
	s_mov_b32	s0, 0
	s_mov_b32	s0, 1
	s_mov_b32	s0, 2
	s_mov_b32	s0, 63
	s_mov_b32	s0, 64
	s_mov_b32	s0, -1
	s_mov_b32	s0, -2
	s_mov_b32	s0, -15
	s_mov_b32	s0, -16
	s_mov_b32	s0, 65
	s_mov_b32	s0, -17
	s_mov_b32	s0, 255
	s_mov_b32	s0, 256
	s_mov_b32	s0, 0x1234
	s_mov_b32	s0, 0x00ff00ff
	s_mov_b32	s0, 0x12345678
	s_mov_b32	s0, 0x7fffffff
	s_mov_b32	s0, 0x80000000
	s_mov_b32	s0, 0xfffffff0
	s_mov_b32	s0, 0xffffffef
	s_mov_b32	s0, 0xffffffff
	s_mov_b32	s0, 0xdeadbeef
	s_mov_b32	s9, s0
	s_mov_b32	s9, s1
	s_mov_b32	s9, s7
	s_mov_b32	s9, s64
	s_mov_b32	s9, s104
	s_mov_b32	s9, s105
	s_mov_b32	s9, 0
	s_mov_b32	s9, 1
	s_mov_b32	s9, 2
	s_mov_b32	s9, 63
	s_mov_b32	s9, 64
	s_mov_b32	s9, -1
	s_mov_b32	s9, -2
	s_mov_b32	s9, -15
	s_mov_b32	s9, -16
	s_mov_b32	s9, 65
	s_mov_b32	s9, -17
	s_mov_b32	s9, 255
	s_mov_b32	s9, 256
	s_mov_b32	s9, 0x1234
	s_mov_b32	s9, 0x00ff00ff
	s_mov_b32	s9, 0x12345678
	s_mov_b32	s9, 0x7fffffff
	s_mov_b32	s9, 0x80000000
	s_mov_b32	s9, 0xfffffff0
	s_mov_b32	s9, 0xffffffef
	s_mov_b32	s9, 0xffffffff
	s_mov_b32	s9, 0xdeadbeef
	s_mov_b32	s105, s0
	s_mov_b32	s105, s1
	s_mov_b32	s105, s7
	s_mov_b32	s105, s64
	s_mov_b32	s105, s104
	s_mov_b32	s105, s105
	s_mov_b32	s105, 0
	s_mov_b32	s105, 1
	s_mov_b32	s105, 2
	s_mov_b32	s105, 63
	s_mov_b32	s105, 64
	s_mov_b32	s105, -1
	s_mov_b32	s105, -2
	s_mov_b32	s105, -15
	s_mov_b32	s105, -16
	s_mov_b32	s105, 65
	s_mov_b32	s105, -17
	s_mov_b32	s105, 255
	s_mov_b32	s105, 256
	s_mov_b32	s105, 0x1234
	s_mov_b32	s105, 0x00ff00ff
	s_mov_b32	s105, 0x12345678
	s_mov_b32	s105, 0x7fffffff
	s_mov_b32	s105, 0x80000000
	s_mov_b32	s105, 0xfffffff0
	s_mov_b32	s105, 0xffffffef
	s_mov_b32	s105, 0xffffffff
	s_mov_b32	s105, 0xdeadbeef
	s_mov_b64	s[0:1], s[0:1]
	s_mov_b64	s[0:1], s[2:3]
	s_mov_b64	s[0:1], s[104:105]
	s_mov_b64	s[0:1], vcc
	s_mov_b64	s[0:1], exec
	s_mov_b64	s[0:1], 0
	s_mov_b64	s[0:1], 1
	s_mov_b64	s[0:1], 2
	s_mov_b64	s[0:1], 63
	s_mov_b64	s[0:1], 64
	s_mov_b64	s[0:1], -1
	s_mov_b64	s[0:1], -2
	s_mov_b64	s[0:1], -15
	s_mov_b64	s[0:1], -16
	s_mov_b64	s[6:7], s[0:1]
	s_mov_b64	s[6:7], s[2:3]
	s_mov_b64	s[6:7], s[104:105]
	s_mov_b64	s[6:7], vcc
	s_mov_b64	s[6:7], exec
	s_mov_b64	s[6:7], 0
	s_mov_b64	s[6:7], 1
	s_mov_b64	s[6:7], 2
	s_mov_b64	s[6:7], 63
	s_mov_b64	s[6:7], 64
	s_mov_b64	s[6:7], -1
	s_mov_b64	s[6:7], -2
	s_mov_b64	s[6:7], -15
	s_mov_b64	s[6:7], -16
	s_mov_b64	s[104:105], s[0:1]
	s_mov_b64	s[104:105], s[2:3]
	s_mov_b64	s[104:105], s[104:105]
	s_mov_b64	s[104:105], vcc
	s_mov_b64	s[104:105], exec
	s_mov_b64	s[104:105], 0
	s_mov_b64	s[104:105], 1
	s_mov_b64	s[104:105], 2
	s_mov_b64	s[104:105], 63
	s_mov_b64	s[104:105], 64
	s_mov_b64	s[104:105], -1
	s_mov_b64	s[104:105], -2
	s_mov_b64	s[104:105], -15
	s_mov_b64	s[104:105], -16
	s_mov_b64	vcc, s[0:1]
	s_mov_b64	vcc, s[2:3]
	s_mov_b64	vcc, s[104:105]
	s_mov_b64	vcc, vcc
	s_mov_b64	vcc, exec
	s_mov_b64	vcc, 0
	s_mov_b64	vcc, 1
	s_mov_b64	vcc, 2
	s_mov_b64	vcc, 63
	s_mov_b64	vcc, 64
	s_mov_b64	vcc, -1
	s_mov_b64	vcc, -2
	s_mov_b64	vcc, -15
	s_mov_b64	vcc, -16
	s_mov_b64	exec, s[0:1]
	s_mov_b64	exec, s[2:3]
	s_mov_b64	exec, s[104:105]
	s_mov_b64	exec, vcc
	s_mov_b64	exec, exec
	s_mov_b64	exec, 0
	s_mov_b64	exec, 1
	s_mov_b64	exec, 2
	s_mov_b64	exec, 63
	s_mov_b64	exec, 64
	s_mov_b64	exec, -1
	s_mov_b64	exec, -2
	s_mov_b64	exec, -15
	s_mov_b64	exec, -16
	s_and_saveexec_b64	s[0:1], s[2:3]
	s_and_saveexec_b64	s[104:105], s[2:3]
	s_and_saveexec_b64	s[0:1], vcc
	s_and_saveexec_b64	s[104:105], vcc
	s_and_saveexec_b64	s[0:1], exec
	s_and_saveexec_b64	s[104:105], exec
	s_and_saveexec_b64	s[0:1], 0
	s_and_saveexec_b64	s[104:105], 0
	s_and_saveexec_b64	s[0:1], -1
	s_and_saveexec_b64	s[104:105], -1
	s_and_saveexec_b64	vcc, s[2:3]
	s_and_saveexec_b64	exec, s[2:3]
	s_add_u32	s0, s1, s2
	s_add_u32	s105, s104, s103
	s_add_u32	s0, 5, s2
	s_add_u32	s0, s2, 5
	s_add_u32	s0, -1, -16
	s_add_u32	s0, 64, 64
	s_add_u32	s0, 0x1234, s5
	s_add_u32	s0, s5, 0x1234
	s_add_u32	s0, 0x12345678, 0x12345678
	s_add_u32	s9, 0xffffffff, s5
	s_add_u32	s9, s5, 0xfffffff0
	s_add_u32	s9, 0x80008, s61
	s_add_u32	s65, s61, 0x80010
	s_sub_u32	s0, s1, s2
	s_sub_u32	s105, s104, s103
	s_sub_u32	s0, 5, s2
	s_sub_u32	s0, s2, 5
	s_sub_u32	s0, -1, -16
	s_sub_u32	s0, 64, 64
	s_sub_u32	s0, 0x1234, s5
	s_sub_u32	s0, s5, 0x1234
	s_sub_u32	s0, 0x12345678, 0x12345678
	s_sub_u32	s9, 0xffffffff, s5
	s_sub_u32	s9, s5, 0xfffffff0
	s_sub_u32	s9, 0x80008, s61
	s_sub_u32	s65, s61, 0x80010
	s_add_i32	s0, s1, s2
	s_add_i32	s105, s104, s103
	s_add_i32	s0, 5, s2
	s_add_i32	s0, s2, 5
	s_add_i32	s0, -1, -16
	s_add_i32	s0, 64, 64
	s_add_i32	s0, 0x1234, s5
	s_add_i32	s0, s5, 0x1234
	s_add_i32	s0, 0x12345678, 0x12345678
	s_add_i32	s9, 0xffffffff, s5
	s_add_i32	s9, s5, 0xfffffff0
	s_add_i32	s9, 0x80008, s61
	s_add_i32	s65, s61, 0x80010
	s_sub_i32	s0, s1, s2
	s_sub_i32	s105, s104, s103
	s_sub_i32	s0, 5, s2
	s_sub_i32	s0, s2, 5
	s_sub_i32	s0, -1, -16
	s_sub_i32	s0, 64, 64
	s_sub_i32	s0, 0x1234, s5
	s_sub_i32	s0, s5, 0x1234
	s_sub_i32	s0, 0x12345678, 0x12345678
	s_sub_i32	s9, 0xffffffff, s5
	s_sub_i32	s9, s5, 0xfffffff0
	s_sub_i32	s9, 0x80008, s61
	s_sub_i32	s65, s61, 0x80010
	s_addc_u32	s0, s1, s2
	s_addc_u32	s105, s104, s103
	s_addc_u32	s0, 5, s2
	s_addc_u32	s0, s2, 5
	s_addc_u32	s0, -1, -16
	s_addc_u32	s0, 64, 64
	s_addc_u32	s0, 0x1234, s5
	s_addc_u32	s0, s5, 0x1234
	s_addc_u32	s0, 0x12345678, 0x12345678
	s_addc_u32	s9, 0xffffffff, s5
	s_addc_u32	s9, s5, 0xfffffff0
	s_addc_u32	s9, 0x80008, s61
	s_addc_u32	s65, s61, 0x80010
	s_min_u32	s0, s1, s2
	s_min_u32	s105, s104, s103
	s_min_u32	s0, 5, s2
	s_min_u32	s0, s2, 5
	s_min_u32	s0, -1, -16
	s_min_u32	s0, 64, 64
	s_min_u32	s0, 0x1234, s5
	s_min_u32	s0, s5, 0x1234
	s_min_u32	s0, 0x12345678, 0x12345678
	s_min_u32	s9, 0xffffffff, s5
	s_min_u32	s9, s5, 0xfffffff0
	s_min_u32	s9, 0x80008, s61
	s_min_u32	s65, s61, 0x80010
	s_and_b32	s0, s1, s2
	s_and_b32	s105, s104, s103
	s_and_b32	s0, 5, s2
	s_and_b32	s0, s2, 5
	s_and_b32	s0, -1, -16
	s_and_b32	s0, 64, 64
	s_and_b32	s0, 0x1234, s5
	s_and_b32	s0, s5, 0x1234
	s_and_b32	s0, 0x12345678, 0x12345678
	s_and_b32	s9, 0xffffffff, s5
	s_and_b32	s9, s5, 0xfffffff0
	s_and_b32	s9, 0x80008, s61
	s_and_b32	s65, s61, 0x80010
	s_or_b32	s0, s1, s2
	s_or_b32	s105, s104, s103
	s_or_b32	s0, 5, s2
	s_or_b32	s0, s2, 5
	s_or_b32	s0, -1, -16
	s_or_b32	s0, 64, 64
	s_or_b32	s0, 0x1234, s5
	s_or_b32	s0, s5, 0x1234
	s_or_b32	s0, 0x12345678, 0x12345678
	s_or_b32	s9, 0xffffffff, s5
	s_or_b32	s9, s5, 0xfffffff0
	s_or_b32	s9, 0x80008, s61
	s_or_b32	s65, s61, 0x80010
	s_lshl_b32	s0, s1, s2
	s_lshl_b32	s105, s104, s103
	s_lshl_b32	s0, 5, s2
	s_lshl_b32	s0, s2, 5
	s_lshl_b32	s0, -1, -16
	s_lshl_b32	s0, 64, 64
	s_lshl_b32	s0, 0x1234, s5
	s_lshl_b32	s0, s5, 0x1234
	s_lshl_b32	s0, 0x12345678, 0x12345678
	s_lshl_b32	s9, 0xffffffff, s5
	s_lshl_b32	s9, s5, 0xfffffff0
	s_lshl_b32	s9, 0x80008, s61
	s_lshl_b32	s65, s61, 0x80010
	s_lshr_b32	s0, s1, s2
	s_lshr_b32	s105, s104, s103
	s_lshr_b32	s0, 5, s2
	s_lshr_b32	s0, s2, 5
	s_lshr_b32	s0, -1, -16
	s_lshr_b32	s0, 64, 64
	s_lshr_b32	s0, 0x1234, s5
	s_lshr_b32	s0, s5, 0x1234
	s_lshr_b32	s0, 0x12345678, 0x12345678
	s_lshr_b32	s9, 0xffffffff, s5
	s_lshr_b32	s9, s5, 0xfffffff0
	s_lshr_b32	s9, 0x80008, s61
	s_lshr_b32	s65, s61, 0x80010
	s_mul_i32	s0, s1, s2
	s_mul_i32	s105, s104, s103
	s_mul_i32	s0, 5, s2
	s_mul_i32	s0, s2, 5
	s_mul_i32	s0, -1, -16
	s_mul_i32	s0, 64, 64
	s_mul_i32	s0, 0x1234, s5
	s_mul_i32	s0, s5, 0x1234
	s_mul_i32	s0, 0x12345678, 0x12345678
	s_mul_i32	s9, 0xffffffff, s5
	s_mul_i32	s9, s5, 0xfffffff0
	s_mul_i32	s9, 0x80008, s61
	s_mul_i32	s65, s61, 0x80010
	s_bfe_u32	s0, s1, s2
	s_bfe_u32	s105, s104, s103
	s_bfe_u32	s0, 5, s2
	s_bfe_u32	s0, s2, 5
	s_bfe_u32	s0, -1, -16
	s_bfe_u32	s0, 64, 64
	s_bfe_u32	s0, 0x1234, s5
	s_bfe_u32	s0, s5, 0x1234
	s_bfe_u32	s0, 0x12345678, 0x12345678
	s_bfe_u32	s9, 0xffffffff, s5
	s_bfe_u32	s9, s5, 0xfffffff0
	s_bfe_u32	s9, 0x80008, s61
	s_bfe_u32	s65, s61, 0x80010
	s_and_b64	s[0:1], s[2:3], s[4:5]
	s_and_b64	vcc, vcc, s[2:3]
	s_and_b64	exec, exec, s[30:31]
	s_and_b64	s[2:3], s[2:3], vcc
	s_and_b64	s[0:1], 0, -1
	s_and_b64	s[0:1], 64, s[2:3]
	s_and_b64	s[104:105], exec, vcc
	s_and_b64	s[0:1], s[2:3], -16
	s_or_b64	s[0:1], s[2:3], s[4:5]
	s_or_b64	vcc, vcc, s[2:3]
	s_or_b64	exec, exec, s[30:31]
	s_or_b64	s[2:3], s[2:3], vcc
	s_or_b64	s[0:1], 0, -1
	s_or_b64	s[0:1], 64, s[2:3]
	s_or_b64	s[104:105], exec, vcc
	s_or_b64	s[0:1], s[2:3], -16
	s_andn2_b64	s[0:1], s[2:3], s[4:5]
	s_andn2_b64	vcc, vcc, s[2:3]
	s_andn2_b64	exec, exec, s[30:31]
	s_andn2_b64	s[2:3], s[2:3], vcc
	s_andn2_b64	s[0:1], 0, -1
	s_andn2_b64	s[0:1], 64, s[2:3]
	s_andn2_b64	s[104:105], exec, vcc
	s_andn2_b64	s[0:1], s[2:3], -16
	s_cmp_eq_u32	s0, s1
	s_cmp_eq_u32	s105, 64
	s_cmp_eq_u32	-16, s3
	s_cmp_eq_u32	s0, 0x1234
	s_cmp_eq_u32	0x1234, s21
	s_cmp_eq_u32	7, 0x1234
	s_cmp_eq_u32	0x1234, 0x1234
	s_cmp_eq_u32	s21, 0
	s_cmp_eq_u32	s21, 65
	s_cmp_eq_u32	0xffffffff, -1
	s_cmp_lg_u32	s0, s1
	s_cmp_lg_u32	s105, 64
	s_cmp_lg_u32	-16, s3
	s_cmp_lg_u32	s0, 0x1234
	s_cmp_lg_u32	0x1234, s21
	s_cmp_lg_u32	7, 0x1234
	s_cmp_lg_u32	0x1234, 0x1234
	s_cmp_lg_u32	s21, 0
	s_cmp_lg_u32	s21, 65
	s_cmp_lg_u32	0xffffffff, -1
	s_cmp_gt_u32	s0, s1
	s_cmp_gt_u32	s105, 64
	s_cmp_gt_u32	-16, s3
	s_cmp_gt_u32	s0, 0x1234
	s_cmp_gt_u32	0x1234, s21
	s_cmp_gt_u32	7, 0x1234
	s_cmp_gt_u32	0x1234, 0x1234
	s_cmp_gt_u32	s21, 0
	s_cmp_gt_u32	s21, 65
	s_cmp_gt_u32	0xffffffff, -1
	s_cmp_ge_u32	s0, s1
	s_cmp_ge_u32	s105, 64
	s_cmp_ge_u32	-16, s3
	s_cmp_ge_u32	s0, 0x1234
	s_cmp_ge_u32	0x1234, s21
	s_cmp_ge_u32	7, 0x1234
	s_cmp_ge_u32	0x1234, 0x1234
	s_cmp_ge_u32	s21, 0
	s_cmp_ge_u32	s21, 65
	s_cmp_ge_u32	0xffffffff, -1
	s_cmp_lt_u32	s0, s1
	s_cmp_lt_u32	s105, 64
	s_cmp_lt_u32	-16, s3
	s_cmp_lt_u32	s0, 0x1234
	s_cmp_lt_u32	0x1234, s21
	s_cmp_lt_u32	7, 0x1234
	s_cmp_lt_u32	0x1234, 0x1234
	s_cmp_lt_u32	s21, 0
	s_cmp_lt_u32	s21, 65
	s_cmp_lt_u32	0xffffffff, -1
	s_cmp_le_u32	s0, s1
	s_cmp_le_u32	s105, 64
	s_cmp_le_u32	-16, s3
	s_cmp_le_u32	s0, 0x1234
	s_cmp_le_u32	0x1234, s21
	s_cmp_le_u32	7, 0x1234
	s_cmp_le_u32	0x1234, 0x1234
	s_cmp_le_u32	s21, 0
	s_cmp_le_u32	s21, 65
	s_cmp_le_u32	0xffffffff, -1
	s_cmp_eq_i32	s0, s1
	s_cmp_eq_i32	s105, 64
	s_cmp_eq_i32	-16, s3
	s_cmp_eq_i32	s0, 0x1234
	s_cmp_eq_i32	0x1234, s21
	s_cmp_eq_i32	7, 0x1234
	s_cmp_eq_i32	0x1234, 0x1234
	s_cmp_eq_i32	s21, 0
	s_cmp_eq_i32	s21, 65
	s_cmp_eq_i32	0xffffffff, -1
	s_cmp_gt_i32	s0, s1
	s_cmp_gt_i32	s105, 64
	s_cmp_gt_i32	-16, s3
	s_cmp_gt_i32	s0, 0x1234
	s_cmp_gt_i32	0x1234, s21
	s_cmp_gt_i32	7, 0x1234
	s_cmp_gt_i32	0x1234, 0x1234
	s_cmp_gt_i32	s21, 0
	s_cmp_gt_i32	s21, 65
	s_cmp_gt_i32	0xffffffff, -1
	s_cmp_ge_i32	s0, s1
	s_cmp_ge_i32	s105, 64
	s_cmp_ge_i32	-16, s3
	s_cmp_ge_i32	s0, 0x1234
	s_cmp_ge_i32	0x1234, s21
	s_cmp_ge_i32	7, 0x1234
	s_cmp_ge_i32	0x1234, 0x1234
	s_cmp_ge_i32	s21, 0
	s_cmp_ge_i32	s21, 65
	s_cmp_ge_i32	0xffffffff, -1
	s_cmp_lt_i32	s0, s1
	s_cmp_lt_i32	s105, 64
	s_cmp_lt_i32	-16, s3
	s_cmp_lt_i32	s0, 0x1234
	s_cmp_lt_i32	0x1234, s21
	s_cmp_lt_i32	7, 0x1234
	s_cmp_lt_i32	0x1234, 0x1234
	s_cmp_lt_i32	s21, 0
	s_cmp_lt_i32	s21, 65
	s_cmp_lt_i32	0xffffffff, -1
	s_cmp_le_i32	s0, s1
	s_cmp_le_i32	s105, 64
	s_cmp_le_i32	-16, s3
	s_cmp_le_i32	s0, 0x1234
	s_cmp_le_i32	0x1234, s21
	s_cmp_le_i32	7, 0x1234
	s_cmp_le_i32	0x1234, 0x1234
	s_cmp_le_i32	s21, 0
	s_cmp_le_i32	s21, 65
	s_cmp_le_i32	0xffffffff, -1
	s_bitcmp0_b32	s0, s1
	s_bitcmp0_b32	s105, 64
	s_bitcmp0_b32	-16, s3
	s_bitcmp0_b32	s0, 0x1234
	s_bitcmp0_b32	0x1234, s21
	s_bitcmp0_b32	7, 0x1234
	s_bitcmp0_b32	0x1234, 0x1234
	s_bitcmp0_b32	s21, 0
	s_bitcmp0_b32	s21, 65
	s_bitcmp0_b32	0xffffffff, -1
	s_bitcmp1_b32	s0, s1
	s_bitcmp1_b32	s105, 64
	s_bitcmp1_b32	-16, s3
	s_bitcmp1_b32	s0, 0x1234
	s_bitcmp1_b32	0x1234, s21
	s_bitcmp1_b32	7, 0x1234
	s_bitcmp1_b32	0x1234, 0x1234
	s_bitcmp1_b32	s21, 0
	s_bitcmp1_b32	s21, 65
	s_bitcmp1_b32	0xffffffff, -1
	s_endpgm
.size salu, .-salu

; SMEM: each width, a base pair, offsets
.globl smem
.p2align 4
.type smem,@function
smem:
	s_load_dword	s0, s[0:1], 0
	s_load_dword	s105, s[0:1], 0
	s_load_dwordx2	s[2:3], s[0:1], 0
	s_load_dwordx2	s[104:105], s[0:1], 0
	s_load_dwordx4	s[4:7], s[0:1], 0
	s_load_dwordx4	s[100:103], s[0:1], 0
	s_load_dwordx8	s[8:15], s[0:1], 0
	s_load_dwordx8	s[96:103], s[0:1], 0
	s_load_dword	s0, s[0:1], 4
	s_load_dword	s105, s[0:1], 4
	s_load_dwordx2	s[2:3], s[0:1], 4
	s_load_dwordx2	s[104:105], s[0:1], 4
	s_load_dwordx4	s[4:7], s[0:1], 4
	s_load_dwordx4	s[100:103], s[0:1], 4
	s_load_dwordx8	s[8:15], s[0:1], 4
	s_load_dwordx8	s[96:103], s[0:1], 4
	s_load_dword	s0, s[0:1], 0x24
	s_load_dword	s105, s[0:1], 0x24
	s_load_dwordx2	s[2:3], s[0:1], 0x24
	s_load_dwordx2	s[104:105], s[0:1], 0x24
	s_load_dwordx4	s[4:7], s[0:1], 0x24
	s_load_dwordx4	s[100:103], s[0:1], 0x24
	s_load_dwordx8	s[8:15], s[0:1], 0x24
	s_load_dwordx8	s[96:103], s[0:1], 0x24
	s_load_dword	s0, s[0:1], 0x1ff
	s_load_dword	s105, s[0:1], 0x1ff
	s_load_dwordx2	s[2:3], s[0:1], 0x1ff
	s_load_dwordx2	s[104:105], s[0:1], 0x1ff
	s_load_dwordx4	s[4:7], s[0:1], 0x1ff
	s_load_dwordx4	s[100:103], s[0:1], 0x1ff
	s_load_dwordx8	s[8:15], s[0:1], 0x1ff
	s_load_dwordx8	s[96:103], s[0:1], 0x1ff
	s_load_dword	s0, s[0:1], 0xfffff
	s_load_dword	s105, s[0:1], 0xfffff
	s_load_dwordx2	s[2:3], s[0:1], 0xfffff
	s_load_dwordx2	s[104:105], s[0:1], 0xfffff
	s_load_dwordx4	s[4:7], s[0:1], 0xfffff
	s_load_dwordx4	s[100:103], s[0:1], 0xfffff
	s_load_dwordx8	s[8:15], s[0:1], 0xfffff
	s_load_dwordx8	s[96:103], s[0:1], 0xfffff
	s_load_dword	s0, s[0:1], 1048575
	s_load_dword	s105, s[0:1], 1048575
	s_load_dwordx2	s[2:3], s[0:1], 1048575
	s_load_dwordx2	s[104:105], s[0:1], 1048575
	s_load_dwordx4	s[4:7], s[0:1], 1048575
	s_load_dwordx4	s[100:103], s[0:1], 1048575
	s_load_dwordx8	s[8:15], s[0:1], 1048575
	s_load_dwordx8	s[96:103], s[0:1], 1048575
	s_load_dword	s0, s[0:1], -4
	s_load_dword	s105, s[0:1], -4
	s_load_dwordx2	s[2:3], s[0:1], -4
	s_load_dwordx2	s[104:105], s[0:1], -4
	s_load_dwordx4	s[4:7], s[0:1], -4
	s_load_dwordx4	s[100:103], s[0:1], -4
	s_load_dwordx8	s[8:15], s[0:1], -4
	s_load_dwordx8	s[96:103], s[0:1], -4
	s_load_dword	s0, s[0:1], -1
	s_load_dword	s105, s[0:1], -1
	s_load_dwordx2	s[2:3], s[0:1], -1
	s_load_dwordx2	s[104:105], s[0:1], -1
	s_load_dwordx4	s[4:7], s[0:1], -1
	s_load_dwordx4	s[100:103], s[0:1], -1
	s_load_dwordx8	s[8:15], s[0:1], -1
	s_load_dwordx8	s[96:103], s[0:1], -1
	s_load_dword	s0, s[0:1], -0x100000
	s_load_dword	s105, s[0:1], -0x100000
	s_load_dwordx2	s[2:3], s[0:1], -0x100000
	s_load_dwordx2	s[104:105], s[0:1], -0x100000
	s_load_dwordx4	s[4:7], s[0:1], -0x100000
	s_load_dwordx4	s[100:103], s[0:1], -0x100000
	s_load_dwordx8	s[8:15], s[0:1], -0x100000
	s_load_dwordx8	s[96:103], s[0:1], -0x100000
	s_load_dword	s0, s[0:1], -1048576
	s_load_dword	s105, s[0:1], -1048576
	s_load_dwordx2	s[2:3], s[0:1], -1048576
	s_load_dwordx2	s[104:105], s[0:1], -1048576
	s_load_dwordx4	s[4:7], s[0:1], -1048576
	s_load_dwordx4	s[100:103], s[0:1], -1048576
	s_load_dwordx8	s[8:15], s[0:1], -1048576
	s_load_dwordx8	s[96:103], s[0:1], -1048576
	s_load_dword	s0, s[2:3], 0
	s_load_dword	s105, s[2:3], 0
	s_load_dwordx2	s[2:3], s[2:3], 0
	s_load_dwordx2	s[104:105], s[2:3], 0
	s_load_dwordx4	s[4:7], s[2:3], 0
	s_load_dwordx4	s[100:103], s[2:3], 0
	s_load_dwordx8	s[8:15], s[2:3], 0
	s_load_dwordx8	s[96:103], s[2:3], 0
	s_load_dword	s0, s[2:3], 4
	s_load_dword	s105, s[2:3], 4
	s_load_dwordx2	s[2:3], s[2:3], 4
	s_load_dwordx2	s[104:105], s[2:3], 4
	s_load_dwordx4	s[4:7], s[2:3], 4
	s_load_dwordx4	s[100:103], s[2:3], 4
	s_load_dwordx8	s[8:15], s[2:3], 4
	s_load_dwordx8	s[96:103], s[2:3], 4
	s_load_dword	s0, s[2:3], 0x24
	s_load_dword	s105, s[2:3], 0x24
	s_load_dwordx2	s[2:3], s[2:3], 0x24
	s_load_dwordx2	s[104:105], s[2:3], 0x24
	s_load_dwordx4	s[4:7], s[2:3], 0x24
	s_load_dwordx4	s[100:103], s[2:3], 0x24
	s_load_dwordx8	s[8:15], s[2:3], 0x24
	s_load_dwordx8	s[96:103], s[2:3], 0x24
	s_load_dword	s0, s[2:3], 0x1ff
	s_load_dword	s105, s[2:3], 0x1ff
	s_load_dwordx2	s[2:3], s[2:3], 0x1ff
	s_load_dwordx2	s[104:105], s[2:3], 0x1ff
	s_load_dwordx4	s[4:7], s[2:3], 0x1ff
	s_load_dwordx4	s[100:103], s[2:3], 0x1ff
	s_load_dwordx8	s[8:15], s[2:3], 0x1ff
	s_load_dwordx8	s[96:103], s[2:3], 0x1ff
	s_load_dword	s0, s[2:3], 0xfffff
	s_load_dword	s105, s[2:3], 0xfffff
	s_load_dwordx2	s[2:3], s[2:3], 0xfffff
	s_load_dwordx2	s[104:105], s[2:3], 0xfffff
	s_load_dwordx4	s[4:7], s[2:3], 0xfffff
	s_load_dwordx4	s[100:103], s[2:3], 0xfffff
	s_load_dwordx8	s[8:15], s[2:3], 0xfffff
	s_load_dwordx8	s[96:103], s[2:3], 0xfffff
	s_load_dword	s0, s[2:3], 1048575
	s_load_dword	s105, s[2:3], 1048575
	s_load_dwordx2	s[2:3], s[2:3], 1048575
	s_load_dwordx2	s[104:105], s[2:3], 1048575
	s_load_dwordx4	s[4:7], s[2:3], 1048575
	s_load_dwordx4	s[100:103], s[2:3], 1048575
	s_load_dwordx8	s[8:15], s[2:3], 1048575
	s_load_dwordx8	s[96:103], s[2:3], 1048575
	s_load_dword	s0, s[2:3], -4
	s_load_dword	s105, s[2:3], -4
	s_load_dwordx2	s[2:3], s[2:3], -4
	s_load_dwordx2	s[104:105], s[2:3], -4
	s_load_dwordx4	s[4:7], s[2:3], -4
	s_load_dwordx4	s[100:103], s[2:3], -4
	s_load_dwordx8	s[8:15], s[2:3], -4
	s_load_dwordx8	s[96:103], s[2:3], -4
	s_load_dword	s0, s[2:3], -1
	s_load_dword	s105, s[2:3], -1
	s_load_dwordx2	s[2:3], s[2:3], -1
	s_load_dwordx2	s[104:105], s[2:3], -1
	s_load_dwordx4	s[4:7], s[2:3], -1
	s_load_dwordx4	s[100:103], s[2:3], -1
	s_load_dwordx8	s[8:15], s[2:3], -1
	s_load_dwordx8	s[96:103], s[2:3], -1
	s_load_dword	s0, s[2:3], -0x100000
	s_load_dword	s105, s[2:3], -0x100000
	s_load_dwordx2	s[2:3], s[2:3], -0x100000
	s_load_dwordx2	s[104:105], s[2:3], -0x100000
	s_load_dwordx4	s[4:7], s[2:3], -0x100000
	s_load_dwordx4	s[100:103], s[2:3], -0x100000
	s_load_dwordx8	s[8:15], s[2:3], -0x100000
	s_load_dwordx8	s[96:103], s[2:3], -0x100000
	s_load_dword	s0, s[2:3], -1048576
	s_load_dword	s105, s[2:3], -1048576
	s_load_dwordx2	s[2:3], s[2:3], -1048576
	s_load_dwordx2	s[104:105], s[2:3], -1048576
	s_load_dwordx4	s[4:7], s[2:3], -1048576
	s_load_dwordx4	s[100:103], s[2:3], -1048576
	s_load_dwordx8	s[8:15], s[2:3], -1048576
	s_load_dwordx8	s[96:103], s[2:3], -1048576
	s_load_dword	s0, s[4:5], 0
	s_load_dword	s105, s[4:5], 0
	s_load_dwordx2	s[2:3], s[4:5], 0
	s_load_dwordx2	s[104:105], s[4:5], 0
	s_load_dwordx4	s[4:7], s[4:5], 0
	s_load_dwordx4	s[100:103], s[4:5], 0
	s_load_dwordx8	s[8:15], s[4:5], 0
	s_load_dwordx8	s[96:103], s[4:5], 0
	s_load_dword	s0, s[4:5], 4
	s_load_dword	s105, s[4:5], 4
	s_load_dwordx2	s[2:3], s[4:5], 4
	s_load_dwordx2	s[104:105], s[4:5], 4
	s_load_dwordx4	s[4:7], s[4:5], 4
	s_load_dwordx4	s[100:103], s[4:5], 4
	s_load_dwordx8	s[8:15], s[4:5], 4
	s_load_dwordx8	s[96:103], s[4:5], 4
	s_load_dword	s0, s[4:5], 0x24
	s_load_dword	s105, s[4:5], 0x24
	s_load_dwordx2	s[2:3], s[4:5], 0x24
	s_load_dwordx2	s[104:105], s[4:5], 0x24
	s_load_dwordx4	s[4:7], s[4:5], 0x24
	s_load_dwordx4	s[100:103], s[4:5], 0x24
	s_load_dwordx8	s[8:15], s[4:5], 0x24
	s_load_dwordx8	s[96:103], s[4:5], 0x24
	s_load_dword	s0, s[4:5], 0x1ff
	s_load_dword	s105, s[4:5], 0x1ff
	s_load_dwordx2	s[2:3], s[4:5], 0x1ff
	s_load_dwordx2	s[104:105], s[4:5], 0x1ff
	s_load_dwordx4	s[4:7], s[4:5], 0x1ff
	s_load_dwordx4	s[100:103], s[4:5], 0x1ff
	s_load_dwordx8	s[8:15], s[4:5], 0x1ff
	s_load_dwordx8	s[96:103], s[4:5], 0x1ff
	s_load_dword	s0, s[4:5], 0xfffff
	s_load_dword	s105, s[4:5], 0xfffff
	s_load_dwordx2	s[2:3], s[4:5], 0xfffff
	s_load_dwordx2	s[104:105], s[4:5], 0xfffff
	s_load_dwordx4	s[4:7], s[4:5], 0xfffff
	s_load_dwordx4	s[100:103], s[4:5], 0xfffff
	s_load_dwordx8	s[8:15], s[4:5], 0xfffff
	s_load_dwordx8	s[96:103], s[4:5], 0xfffff
	s_load_dword	s0, s[4:5], 1048575
	s_load_dword	s105, s[4:5], 1048575
	s_load_dwordx2	s[2:3], s[4:5], 1048575
	s_load_dwordx2	s[104:105], s[4:5], 1048575
	s_load_dwordx4	s[4:7], s[4:5], 1048575
	s_load_dwordx4	s[100:103], s[4:5], 1048575
	s_load_dwordx8	s[8:15], s[4:5], 1048575
	s_load_dwordx8	s[96:103], s[4:5], 1048575
	s_load_dword	s0, s[4:5], -4
	s_load_dword	s105, s[4:5], -4
	s_load_dwordx2	s[2:3], s[4:5], -4
	s_load_dwordx2	s[104:105], s[4:5], -4
	s_load_dwordx4	s[4:7], s[4:5], -4
	s_load_dwordx4	s[100:103], s[4:5], -4
	s_load_dwordx8	s[8:15], s[4:5], -4
	s_load_dwordx8	s[96:103], s[4:5], -4
	s_load_dword	s0, s[4:5], -1
	s_load_dword	s105, s[4:5], -1
	s_load_dwordx2	s[2:3], s[4:5], -1
	s_load_dwordx2	s[104:105], s[4:5], -1
	s_load_dwordx4	s[4:7], s[4:5], -1
	s_load_dwordx4	s[100:103], s[4:5], -1
	s_load_dwordx8	s[8:15], s[4:5], -1
	s_load_dwordx8	s[96:103], s[4:5], -1
	s_load_dword	s0, s[4:5], -0x100000
	s_load_dword	s105, s[4:5], -0x100000
	s_load_dwordx2	s[2:3], s[4:5], -0x100000
	s_load_dwordx2	s[104:105], s[4:5], -0x100000
	s_load_dwordx4	s[4:7], s[4:5], -0x100000
	s_load_dwordx4	s[100:103], s[4:5], -0x100000
	s_load_dwordx8	s[8:15], s[4:5], -0x100000
	s_load_dwordx8	s[96:103], s[4:5], -0x100000
	s_load_dword	s0, s[4:5], -1048576
	s_load_dword	s105, s[4:5], -1048576
	s_load_dwordx2	s[2:3], s[4:5], -1048576
	s_load_dwordx2	s[104:105], s[4:5], -1048576
	s_load_dwordx4	s[4:7], s[4:5], -1048576
	s_load_dwordx4	s[100:103], s[4:5], -1048576
	s_load_dwordx8	s[8:15], s[4:5], -1048576
	s_load_dwordx8	s[96:103], s[4:5], -1048576
	s_load_dword	s0, s[104:105], 0
	s_load_dword	s105, s[104:105], 0
	s_load_dwordx2	s[2:3], s[104:105], 0
	s_load_dwordx2	s[104:105], s[104:105], 0
	s_load_dwordx4	s[4:7], s[104:105], 0
	s_load_dwordx4	s[100:103], s[104:105], 0
	s_load_dwordx8	s[8:15], s[104:105], 0
	s_load_dwordx8	s[96:103], s[104:105], 0
	s_load_dword	s0, s[104:105], 4
	s_load_dword	s105, s[104:105], 4
	s_load_dwordx2	s[2:3], s[104:105], 4
	s_load_dwordx2	s[104:105], s[104:105], 4
	s_load_dwordx4	s[4:7], s[104:105], 4
	s_load_dwordx4	s[100:103], s[104:105], 4
	s_load_dwordx8	s[8:15], s[104:105], 4
	s_load_dwordx8	s[96:103], s[104:105], 4
	s_load_dword	s0, s[104:105], 0x24
	s_load_dword	s105, s[104:105], 0x24
	s_load_dwordx2	s[2:3], s[104:105], 0x24
	s_load_dwordx2	s[104:105], s[104:105], 0x24
	s_load_dwordx4	s[4:7], s[104:105], 0x24
	s_load_dwordx4	s[100:103], s[104:105], 0x24
	s_load_dwordx8	s[8:15], s[104:105], 0x24
	s_load_dwordx8	s[96:103], s[104:105], 0x24
	s_load_dword	s0, s[104:105], 0x1ff
	s_load_dword	s105, s[104:105], 0x1ff
	s_load_dwordx2	s[2:3], s[104:105], 0x1ff
	s_load_dwordx2	s[104:105], s[104:105], 0x1ff
	s_load_dwordx4	s[4:7], s[104:105], 0x1ff
	s_load_dwordx4	s[100:103], s[104:105], 0x1ff
	s_load_dwordx8	s[8:15], s[104:105], 0x1ff
	s_load_dwordx8	s[96:103], s[104:105], 0x1ff
	s_load_dword	s0, s[104:105], 0xfffff
	s_load_dword	s105, s[104:105], 0xfffff
	s_load_dwordx2	s[2:3], s[104:105], 0xfffff
	s_load_dwordx2	s[104:105], s[104:105], 0xfffff
	s_load_dwordx4	s[4:7], s[104:105], 0xfffff
	s_load_dwordx4	s[100:103], s[104:105], 0xfffff
	s_load_dwordx8	s[8:15], s[104:105], 0xfffff
	s_load_dwordx8	s[96:103], s[104:105], 0xfffff
	s_load_dword	s0, s[104:105], 1048575
	s_load_dword	s105, s[104:105], 1048575
	s_load_dwordx2	s[2:3], s[104:105], 1048575
	s_load_dwordx2	s[104:105], s[104:105], 1048575
	s_load_dwordx4	s[4:7], s[104:105], 1048575
	s_load_dwordx4	s[100:103], s[104:105], 1048575
	s_load_dwordx8	s[8:15], s[104:105], 1048575
	s_load_dwordx8	s[96:103], s[104:105], 1048575
	s_load_dword	s0, s[104:105], -4
	s_load_dword	s105, s[104:105], -4
	s_load_dwordx2	s[2:3], s[104:105], -4
	s_load_dwordx2	s[104:105], s[104:105], -4
	s_load_dwordx4	s[4:7], s[104:105], -4
	s_load_dwordx4	s[100:103], s[104:105], -4
	s_load_dwordx8	s[8:15], s[104:105], -4
	s_load_dwordx8	s[96:103], s[104:105], -4
	s_load_dword	s0, s[104:105], -1
	s_load_dword	s105, s[104:105], -1
	s_load_dwordx2	s[2:3], s[104:105], -1
	s_load_dwordx2	s[104:105], s[104:105], -1
	s_load_dwordx4	s[4:7], s[104:105], -1
	s_load_dwordx4	s[100:103], s[104:105], -1
	s_load_dwordx8	s[8:15], s[104:105], -1
	s_load_dwordx8	s[96:103], s[104:105], -1
	s_load_dword	s0, s[104:105], -0x100000
	s_load_dword	s105, s[104:105], -0x100000
	s_load_dwordx2	s[2:3], s[104:105], -0x100000
	s_load_dwordx2	s[104:105], s[104:105], -0x100000
	s_load_dwordx4	s[4:7], s[104:105], -0x100000
	s_load_dwordx4	s[100:103], s[104:105], -0x100000
	s_load_dwordx8	s[8:15], s[104:105], -0x100000
	s_load_dwordx8	s[96:103], s[104:105], -0x100000
	s_load_dword	s0, s[104:105], -1048576
	s_load_dword	s105, s[104:105], -1048576
	s_load_dwordx2	s[2:3], s[104:105], -1048576
	s_load_dwordx2	s[104:105], s[104:105], -1048576
	s_load_dwordx4	s[4:7], s[104:105], -1048576
	s_load_dwordx4	s[100:103], s[104:105], -1048576
	s_load_dwordx8	s[8:15], s[104:105], -1048576
	s_load_dwordx8	s[96:103], s[104:105], -1048576
	s_endpgm
.size smem, .-smem

; SOPP: the counters, and branches forward, backward and to themselves
.globl sopp
.p2align 8
.type sopp,@function
sopp:
.top:
	s_waitcnt	lgkmcnt(0)
	s_waitcnt	vmcnt(0)
	s_waitcnt	expcnt(0)
	s_waitcnt	vmcnt(63)
	s_waitcnt	vmcnt(15)
	s_waitcnt	vmcnt(16)
	s_waitcnt	vmcnt(48)
	s_waitcnt	expcnt(7)
	s_waitcnt	expcnt(5)
	s_waitcnt	lgkmcnt(63)
	s_waitcnt	lgkmcnt(17)
	s_waitcnt	lgkmcnt(0) & vmcnt(0)
	s_waitcnt	vmcnt(0) & lgkmcnt(0)
	s_waitcnt	vmcnt(1) lgkmcnt(2) expcnt(3)
	s_waitcnt	vmcnt(33), expcnt(2), lgkmcnt(40)
	s_waitcnt	expcnt(1) & lgkmcnt(0)
	s_waitcnt	lgkmcnt(1) & expcnt(0) & vmcnt(2)
	s_branch	.top
	s_branch	.next_s_branch
.next_s_branch:
	s_branch	.far_s_branch
	s_cbranch_scc0	.top
	s_cbranch_scc0	.next_s_cbranch_scc0
.next_s_cbranch_scc0:
	s_cbranch_scc0	.far_s_cbranch_scc0
	s_cbranch_scc1	.top
	s_cbranch_scc1	.next_s_cbranch_scc1
.next_s_cbranch_scc1:
	s_cbranch_scc1	.far_s_cbranch_scc1
	s_cbranch_execz	.top
	s_cbranch_execz	.next_s_cbranch_execz
.next_s_cbranch_execz:
	s_cbranch_execz	.far_s_cbranch_execz
.self: s_branch .self
.same: s_mov_b32 s0, s1
	s_cbranch_scc1	.same
	two56
	two56
.far_s_branch:
	s_mov_b32	s0, s1
.far_s_cbranch_scc0:
	s_mov_b32	s0, s1
.far_s_cbranch_scc1:
	s_mov_b32	s0, s1
.far_s_cbranch_execz:
	s_mov_b32	s0, s1
	s_branch	tail
	s_mov_b32	s0, s1
tail:
	s_endpgm
.size sopp, .-sopp

; VOP1: the short and the long encoding, float and integer sources
.globl vop1
.p2align 8
.type vop1,@function
vop1:
	v_mov_b32	v0, v0
	v_mov_b32	v0, v255
	v_mov_b32	v0, s0
	v_mov_b32	v0, s105
	v_mov_b32	v0, v7
	v_mov_b32	v1, v0
	v_mov_b32	v1, v255
	v_mov_b32	v1, s0
	v_mov_b32	v1, s105
	v_mov_b32	v1, v7
	v_mov_b32	v255, v0
	v_mov_b32	v255, v255
	v_mov_b32	v255, s0
	v_mov_b32	v255, s105
	v_mov_b32	v255, v7
	v_mov_b32	v3, 0
	v_mov_b32	v3, 1
	v_mov_b32	v3, 2
	v_mov_b32	v3, 63
	v_mov_b32	v3, 64
	v_mov_b32	v3, -1
	v_mov_b32	v3, -2
	v_mov_b32	v3, -15
	v_mov_b32	v3, -16
	v_mov_b32	v3, 65
	v_mov_b32	v3, -17
	v_mov_b32	v3, 255
	v_mov_b32	v3, 256
	v_mov_b32	v3, 0x1234
	v_mov_b32	v3, 0x00ff00ff
	v_mov_b32	v3, 0x12345678
	v_mov_b32	v3, 0x7fffffff
	v_mov_b32	v3, 0x80000000
	v_mov_b32	v3, 0xfffffff0
	v_mov_b32	v3, 0xffffffef
	v_mov_b32	v3, 0xffffffff
	v_mov_b32	v3, 0xdeadbeef
	v_mov_b32_e32	v1, v2
	v_mov_b32_e32	v1, s2
	v_mov_b32_e32	v1, 0x12345
	v_mov_b32_e64	v1, v2
	v_mov_b32_e64	v1, s2
	v_mov_b32_e64	v1, 5
	v_mov_b32_e64	v1, 0x12345
	v_cvt_f32_u32	v0, v0
	v_cvt_f32_u32	v0, v255
	v_cvt_f32_u32	v0, s0
	v_cvt_f32_u32	v0, s105
	v_cvt_f32_u32	v0, v7
	v_cvt_f32_u32	v1, v0
	v_cvt_f32_u32	v1, v255
	v_cvt_f32_u32	v1, s0
	v_cvt_f32_u32	v1, s105
	v_cvt_f32_u32	v1, v7
	v_cvt_f32_u32	v255, v0
	v_cvt_f32_u32	v255, v255
	v_cvt_f32_u32	v255, s0
	v_cvt_f32_u32	v255, s105
	v_cvt_f32_u32	v255, v7
	v_cvt_f32_u32	v3, 0
	v_cvt_f32_u32	v3, 1
	v_cvt_f32_u32	v3, 2
	v_cvt_f32_u32	v3, 63
	v_cvt_f32_u32	v3, 64
	v_cvt_f32_u32	v3, -1
	v_cvt_f32_u32	v3, -2
	v_cvt_f32_u32	v3, -15
	v_cvt_f32_u32	v3, -16
	v_cvt_f32_u32	v3, 65
	v_cvt_f32_u32	v3, -17
	v_cvt_f32_u32	v3, 255
	v_cvt_f32_u32	v3, 256
	v_cvt_f32_u32	v3, 0x1234
	v_cvt_f32_u32	v3, 0x00ff00ff
	v_cvt_f32_u32	v3, 0x12345678
	v_cvt_f32_u32	v3, 0x7fffffff
	v_cvt_f32_u32	v3, 0x80000000
	v_cvt_f32_u32	v3, 0xfffffff0
	v_cvt_f32_u32	v3, 0xffffffef
	v_cvt_f32_u32	v3, 0xffffffff
	v_cvt_f32_u32	v3, 0xdeadbeef
	v_cvt_f32_u32_e32	v1, v2
	v_cvt_f32_u32_e32	v1, s2
	v_cvt_f32_u32_e32	v1, 0x12345
	v_cvt_f32_u32_e64	v1, v2
	v_cvt_f32_u32_e64	v1, s2
	v_cvt_f32_u32_e64	v1, 5
	v_cvt_f32_u32_e64	v1, 0x12345
	v_cvt_f32_u32	v1, v2 clamp
	v_cvt_f32_u32	v1, s2 clamp
	v_cvt_f32_u32_e64	v255, v255 clamp
	v_cvt_u32_f32	v0, v0
	v_cvt_u32_f32	v0, v255
	v_cvt_u32_f32	v0, s0
	v_cvt_u32_f32	v0, s105
	v_cvt_u32_f32	v0, v7
	v_cvt_u32_f32	v1, v0
	v_cvt_u32_f32	v1, v255
	v_cvt_u32_f32	v1, s0
	v_cvt_u32_f32	v1, s105
	v_cvt_u32_f32	v1, v7
	v_cvt_u32_f32	v255, v0
	v_cvt_u32_f32	v255, v255
	v_cvt_u32_f32	v255, s0
	v_cvt_u32_f32	v255, s105
	v_cvt_u32_f32	v255, v7
	v_cvt_u32_f32	v3, 0
	v_cvt_u32_f32	v3, 1
	v_cvt_u32_f32	v3, 2
	v_cvt_u32_f32	v3, 63
	v_cvt_u32_f32	v3, 64
	v_cvt_u32_f32	v3, -1
	v_cvt_u32_f32	v3, -2
	v_cvt_u32_f32	v3, -15
	v_cvt_u32_f32	v3, -16
	v_cvt_u32_f32	v3, 65
	v_cvt_u32_f32	v3, -17
	v_cvt_u32_f32	v3, 255
	v_cvt_u32_f32	v3, 256
	v_cvt_u32_f32	v3, 0x1234
	v_cvt_u32_f32	v3, 0x00ff00ff
	v_cvt_u32_f32	v3, 0x12345678
	v_cvt_u32_f32	v3, 0x7fffffff
	v_cvt_u32_f32	v3, 0x80000000
	v_cvt_u32_f32	v3, 0xfffffff0
	v_cvt_u32_f32	v3, 0xffffffef
	v_cvt_u32_f32	v3, 0xffffffff
	v_cvt_u32_f32	v3, 0xdeadbeef
	v_cvt_u32_f32_e32	v1, v2
	v_cvt_u32_f32_e32	v1, s2
	v_cvt_u32_f32_e32	v1, 0x12345
	v_cvt_u32_f32_e64	v1, v2
	v_cvt_u32_f32_e64	v1, s2
	v_cvt_u32_f32_e64	v1, 5
	v_cvt_u32_f32_e64	v1, 0x12345
	v_cvt_u32_f32	v1, v2 clamp
	v_cvt_u32_f32	v1, s2 clamp
	v_cvt_u32_f32_e64	v255, v255 clamp
	v_rcp_f32	v0, v0
	v_rcp_f32	v0, v255
	v_rcp_f32	v0, s0
	v_rcp_f32	v0, s105
	v_rcp_f32	v0, v7
	v_rcp_f32	v1, v0
	v_rcp_f32	v1, v255
	v_rcp_f32	v1, s0
	v_rcp_f32	v1, s105
	v_rcp_f32	v1, v7
	v_rcp_f32	v255, v0
	v_rcp_f32	v255, v255
	v_rcp_f32	v255, s0
	v_rcp_f32	v255, s105
	v_rcp_f32	v255, v7
	v_rcp_f32	v3, 0
	v_rcp_f32	v3, 1
	v_rcp_f32	v3, 2
	v_rcp_f32	v3, 63
	v_rcp_f32	v3, 64
	v_rcp_f32	v3, -1
	v_rcp_f32	v3, -2
	v_rcp_f32	v3, -15
	v_rcp_f32	v3, -16
	v_rcp_f32	v3, 65
	v_rcp_f32	v3, -17
	v_rcp_f32	v3, 255
	v_rcp_f32	v3, 256
	v_rcp_f32	v3, 0x1234
	v_rcp_f32	v3, 0x00ff00ff
	v_rcp_f32	v3, 0x12345678
	v_rcp_f32	v3, 0x7fffffff
	v_rcp_f32	v3, 0x80000000
	v_rcp_f32	v3, 0xfffffff0
	v_rcp_f32	v3, 0xffffffef
	v_rcp_f32	v3, 0xffffffff
	v_rcp_f32	v3, 0xdeadbeef
	v_rcp_f32_e32	v1, v2
	v_rcp_f32_e32	v1, s2
	v_rcp_f32_e32	v1, 0x12345
	v_rcp_f32_e64	v1, v2
	v_rcp_f32_e64	v1, s2
	v_rcp_f32_e64	v1, 5
	v_rcp_f32_e64	v1, 0x12345
	v_rcp_f32	v1, v2 clamp
	v_rcp_f32	v1, s2 clamp
	v_rcp_f32_e64	v255, v255 clamp
	v_sqrt_f32	v0, v0
	v_sqrt_f32	v0, v255
	v_sqrt_f32	v0, s0
	v_sqrt_f32	v0, s105
	v_sqrt_f32	v0, v7
	v_sqrt_f32	v1, v0
	v_sqrt_f32	v1, v255
	v_sqrt_f32	v1, s0
	v_sqrt_f32	v1, s105
	v_sqrt_f32	v1, v7
	v_sqrt_f32	v255, v0
	v_sqrt_f32	v255, v255
	v_sqrt_f32	v255, s0
	v_sqrt_f32	v255, s105
	v_sqrt_f32	v255, v7
	v_sqrt_f32	v3, 0
	v_sqrt_f32	v3, 1
	v_sqrt_f32	v3, 2
	v_sqrt_f32	v3, 63
	v_sqrt_f32	v3, 64
	v_sqrt_f32	v3, -1
	v_sqrt_f32	v3, -2
	v_sqrt_f32	v3, -15
	v_sqrt_f32	v3, -16
	v_sqrt_f32	v3, 65
	v_sqrt_f32	v3, -17
	v_sqrt_f32	v3, 255
	v_sqrt_f32	v3, 256
	v_sqrt_f32	v3, 0x1234
	v_sqrt_f32	v3, 0x00ff00ff
	v_sqrt_f32	v3, 0x12345678
	v_sqrt_f32	v3, 0x7fffffff
	v_sqrt_f32	v3, 0x80000000
	v_sqrt_f32	v3, 0xfffffff0
	v_sqrt_f32	v3, 0xffffffef
	v_sqrt_f32	v3, 0xffffffff
	v_sqrt_f32	v3, 0xdeadbeef
	v_sqrt_f32_e32	v1, v2
	v_sqrt_f32_e32	v1, s2
	v_sqrt_f32_e32	v1, 0x12345
	v_sqrt_f32_e64	v1, v2
	v_sqrt_f32_e64	v1, s2
	v_sqrt_f32_e64	v1, 5
	v_sqrt_f32_e64	v1, 0x12345
	v_sqrt_f32	v1, v2 clamp
	v_sqrt_f32	v1, s2 clamp
	v_sqrt_f32_e64	v255, v255 clamp
	v_cvt_u32_f32	v1, 0x3f000000
	v_cvt_u32_f32_e64	v1, 0x3f000000
	v_cvt_u32_f32	v1, 0xbf000000
	v_cvt_u32_f32_e64	v1, 0xbf000000
	v_cvt_u32_f32	v1, 0x3f800000
	v_cvt_u32_f32_e64	v1, 0x3f800000
	v_cvt_u32_f32	v1, 0xbf800000
	v_cvt_u32_f32_e64	v1, 0xbf800000
	v_cvt_u32_f32	v1, 0x40000000
	v_cvt_u32_f32_e64	v1, 0x40000000
	v_cvt_u32_f32	v1, 0xc0000000
	v_cvt_u32_f32_e64	v1, 0xc0000000
	v_cvt_u32_f32	v1, 0x40800000
	v_cvt_u32_f32_e64	v1, 0x40800000
	v_cvt_u32_f32	v1, 0xc0800000
	v_cvt_u32_f32_e64	v1, 0xc0800000
	v_cvt_u32_f32	v1, 0x3e22f983
	v_cvt_u32_f32_e64	v1, 0x3e22f983
	v_cvt_u32_f32	v1, 0x47800000
	v_cvt_u32_f32_e64	v1, 0x47800000
	v_cvt_u32_f32	v1, 0x3f800001
	v_cvt_u32_f32_e64	v1, 0x3f800001
	v_cvt_u32_f32	v1, 0x3e22f984
	v_cvt_u32_f32_e64	v1, 0x3e22f984
	v_rcp_f32	v1, 0x3f000000
	v_rcp_f32_e64	v1, 0x3f000000
	v_rcp_f32	v1, 0xbf000000
	v_rcp_f32_e64	v1, 0xbf000000
	v_rcp_f32	v1, 0x3f800000
	v_rcp_f32_e64	v1, 0x3f800000
	v_rcp_f32	v1, 0xbf800000
	v_rcp_f32_e64	v1, 0xbf800000
	v_rcp_f32	v1, 0x40000000
	v_rcp_f32_e64	v1, 0x40000000
	v_rcp_f32	v1, 0xc0000000
	v_rcp_f32_e64	v1, 0xc0000000
	v_rcp_f32	v1, 0x40800000
	v_rcp_f32_e64	v1, 0x40800000
	v_rcp_f32	v1, 0xc0800000
	v_rcp_f32_e64	v1, 0xc0800000
	v_rcp_f32	v1, 0x3e22f983
	v_rcp_f32_e64	v1, 0x3e22f983
	v_rcp_f32	v1, 0x47800000
	v_rcp_f32_e64	v1, 0x47800000
	v_rcp_f32	v1, 0x3f800001
	v_rcp_f32_e64	v1, 0x3f800001
	v_rcp_f32	v1, 0x3e22f984
	v_rcp_f32_e64	v1, 0x3e22f984
	v_sqrt_f32	v1, 0x3f000000
	v_sqrt_f32_e64	v1, 0x3f000000
	v_sqrt_f32	v1, 0xbf000000
	v_sqrt_f32_e64	v1, 0xbf000000
	v_sqrt_f32	v1, 0x3f800000
	v_sqrt_f32_e64	v1, 0x3f800000
	v_sqrt_f32	v1, 0xbf800000
	v_sqrt_f32_e64	v1, 0xbf800000
	v_sqrt_f32	v1, 0x40000000
	v_sqrt_f32_e64	v1, 0x40000000
	v_sqrt_f32	v1, 0xc0000000
	v_sqrt_f32_e64	v1, 0xc0000000
	v_sqrt_f32	v1, 0x40800000
	v_sqrt_f32_e64	v1, 0x40800000
	v_sqrt_f32	v1, 0xc0800000
	v_sqrt_f32_e64	v1, 0xc0800000
	v_sqrt_f32	v1, 0x3e22f983
	v_sqrt_f32_e64	v1, 0x3e22f983
	v_sqrt_f32	v1, 0x47800000
	v_sqrt_f32_e64	v1, 0x47800000
	v_sqrt_f32	v1, 0x3f800001
	v_sqrt_f32_e64	v1, 0x3f800001
	v_sqrt_f32	v1, 0x3e22f984
	v_sqrt_f32_e64	v1, 0x3e22f984
	v_mov_b32	v1, 0x3f000000
	v_mov_b32_e64	v1, 0x3f000000
	v_mov_b32	v1, 0xbf000000
	v_mov_b32_e64	v1, 0xbf000000
	v_mov_b32	v1, 0x3f800000
	v_mov_b32_e64	v1, 0x3f800000
	v_mov_b32	v1, 0xbf800000
	v_mov_b32_e64	v1, 0xbf800000
	v_mov_b32	v1, 0x40000000
	v_mov_b32_e64	v1, 0x40000000
	v_mov_b32	v1, 0xc0000000
	v_mov_b32_e64	v1, 0xc0000000
	v_mov_b32	v1, 0x40800000
	v_mov_b32_e64	v1, 0x40800000
	v_mov_b32	v1, 0xc0800000
	v_mov_b32_e64	v1, 0xc0800000
	v_mov_b32	v1, 0x3e22f983
	v_mov_b32_e64	v1, 0x3e22f983
	v_mov_b32	v1, 0x47800000
	v_mov_b32_e64	v1, 0x47800000
	v_mov_b32	v1, 0x3f800001
	v_mov_b32_e64	v1, 0x3f800001
	v_mov_b32	v1, 0x3e22f984
	v_mov_b32_e64	v1, 0x3e22f984
	s_endpgm
.size vop1, .-vop1

; VOP2: which encoding the operands choose, and what _e32, _e64 and clamp force
.globl vop2
.p2align 8
.type vop2,@function
vop2:
	v_add_nc_u32	v1, v2, v3
	v_add_nc_u32	v255, v254, v253
	v_add_nc_u32	v0, v0, v0
	v_add_nc_u32	v1, s2, v3
	v_add_nc_u32	v1, s105, v3
	v_add_nc_u32	v1, 5, v3
	v_add_nc_u32	v1, -16, v3
	v_add_nc_u32	v1, 64, v3
	v_add_nc_u32	v1, 0x100, v3
	v_add_nc_u32	v1, 65, v3
	v_add_nc_u32	v1, 0xffffffff, v3
	v_add_nc_u32	v1, v2, s3
	v_add_nc_u32	v1, v2, s105
	v_add_nc_u32	v1, v2, 5
	v_add_nc_u32	v1, v2, -1
	v_add_nc_u32	v1, v2, 0x100
	v_add_nc_u32	v1, s2, s3
	v_add_nc_u32	v1, s2, 5
	v_add_nc_u32	v1, 5, 6
	v_add_nc_u32	v1, 0x100, s3
	v_add_nc_u32	v1, s2, 0x100
	v_add_nc_u32	v1, 0x100, 0x100
	v_add_nc_u32	v1, 0x100, 5
	v_add_nc_u32_e32	v1, v2, v3
	v_add_nc_u32_e32	v1, s2, v3
	v_add_nc_u32_e32	v1, 5, v3
	v_add_nc_u32_e32	v1, 0x100, v3
	v_add_nc_u32_e64	v1, v2, v3
	v_add_nc_u32_e64	v1, s2, v3
	v_add_nc_u32_e64	v1, 5, v3
	v_add_nc_u32_e64	v1, 0x100, v3
	v_add_nc_u32_e64	v1, v2, s3
	v_add_nc_u32_e64	v1, v2, 5
	v_add_nc_u32_e64	v1, v2, 0x100
	v_add_nc_u32_e64	v1, 0x100, s3
	v_add_nc_u32	v1, v2, v3 clamp
	v_add_nc_u32_e64	v1, v2, v3 clamp
	v_add_nc_u32	v1, v2, s3 clamp
	v_sub_nc_u32	v1, v2, v3
	v_sub_nc_u32	v255, v254, v253
	v_sub_nc_u32	v0, v0, v0
	v_sub_nc_u32	v1, s2, v3
	v_sub_nc_u32	v1, s105, v3
	v_sub_nc_u32	v1, 5, v3
	v_sub_nc_u32	v1, -16, v3
	v_sub_nc_u32	v1, 64, v3
	v_sub_nc_u32	v1, 0x100, v3
	v_sub_nc_u32	v1, 65, v3
	v_sub_nc_u32	v1, 0xffffffff, v3
	v_sub_nc_u32	v1, v2, s3
	v_sub_nc_u32	v1, v2, s105
	v_sub_nc_u32	v1, v2, 5
	v_sub_nc_u32	v1, v2, -1
	v_sub_nc_u32	v1, v2, 0x100
	v_sub_nc_u32	v1, s2, s3
	v_sub_nc_u32	v1, s2, 5
	v_sub_nc_u32	v1, 5, 6
	v_sub_nc_u32	v1, 0x100, s3
	v_sub_nc_u32	v1, s2, 0x100
	v_sub_nc_u32	v1, 0x100, 0x100
	v_sub_nc_u32	v1, 0x100, 5
	v_sub_nc_u32_e32	v1, v2, v3
	v_sub_nc_u32_e32	v1, s2, v3
	v_sub_nc_u32_e32	v1, 5, v3
	v_sub_nc_u32_e32	v1, 0x100, v3
	v_sub_nc_u32_e64	v1, v2, v3
	v_sub_nc_u32_e64	v1, s2, v3
	v_sub_nc_u32_e64	v1, 5, v3
	v_sub_nc_u32_e64	v1, 0x100, v3
	v_sub_nc_u32_e64	v1, v2, s3
	v_sub_nc_u32_e64	v1, v2, 5
	v_sub_nc_u32_e64	v1, v2, 0x100
	v_sub_nc_u32_e64	v1, 0x100, s3
	v_sub_nc_u32	v1, v2, v3 clamp
	v_sub_nc_u32_e64	v1, v2, v3 clamp
	v_sub_nc_u32	v1, v2, s3 clamp
	v_subrev_nc_u32	v1, v2, v3
	v_subrev_nc_u32	v255, v254, v253
	v_subrev_nc_u32	v0, v0, v0
	v_subrev_nc_u32	v1, s2, v3
	v_subrev_nc_u32	v1, s105, v3
	v_subrev_nc_u32	v1, 5, v3
	v_subrev_nc_u32	v1, -16, v3
	v_subrev_nc_u32	v1, 64, v3
	v_subrev_nc_u32	v1, 0x100, v3
	v_subrev_nc_u32	v1, 65, v3
	v_subrev_nc_u32	v1, 0xffffffff, v3
	v_subrev_nc_u32	v1, v2, s3
	v_subrev_nc_u32	v1, v2, s105
	v_subrev_nc_u32	v1, v2, 5
	v_subrev_nc_u32	v1, v2, -1
	v_subrev_nc_u32	v1, v2, 0x100
	v_subrev_nc_u32	v1, s2, s3
	v_subrev_nc_u32	v1, s2, 5
	v_subrev_nc_u32	v1, 5, 6
	v_subrev_nc_u32	v1, 0x100, s3
	v_subrev_nc_u32	v1, s2, 0x100
	v_subrev_nc_u32	v1, 0x100, 0x100
	v_subrev_nc_u32	v1, 0x100, 5
	v_subrev_nc_u32_e32	v1, v2, v3
	v_subrev_nc_u32_e32	v1, s2, v3
	v_subrev_nc_u32_e32	v1, 5, v3
	v_subrev_nc_u32_e32	v1, 0x100, v3
	v_subrev_nc_u32_e64	v1, v2, v3
	v_subrev_nc_u32_e64	v1, s2, v3
	v_subrev_nc_u32_e64	v1, 5, v3
	v_subrev_nc_u32_e64	v1, 0x100, v3
	v_subrev_nc_u32_e64	v1, v2, s3
	v_subrev_nc_u32_e64	v1, v2, 5
	v_subrev_nc_u32_e64	v1, v2, 0x100
	v_subrev_nc_u32_e64	v1, 0x100, s3
	v_subrev_nc_u32	v1, v2, v3 clamp
	v_subrev_nc_u32_e64	v1, v2, v3 clamp
	v_subrev_nc_u32	v1, v2, s3 clamp
	v_and_b32	v1, v2, v3
	v_and_b32	v255, v254, v253
	v_and_b32	v0, v0, v0
	v_and_b32	v1, s2, v3
	v_and_b32	v1, s105, v3
	v_and_b32	v1, 5, v3
	v_and_b32	v1, -16, v3
	v_and_b32	v1, 64, v3
	v_and_b32	v1, 0x100, v3
	v_and_b32	v1, 65, v3
	v_and_b32	v1, 0xffffffff, v3
	v_and_b32	v1, v2, s3
	v_and_b32	v1, v2, s105
	v_and_b32	v1, v2, 5
	v_and_b32	v1, v2, -1
	v_and_b32	v1, v2, 0x100
	v_and_b32	v1, s2, s3
	v_and_b32	v1, s2, 5
	v_and_b32	v1, 5, 6
	v_and_b32	v1, 0x100, s3
	v_and_b32	v1, s2, 0x100
	v_and_b32	v1, 0x100, 0x100
	v_and_b32	v1, 0x100, 5
	v_and_b32_e32	v1, v2, v3
	v_and_b32_e32	v1, s2, v3
	v_and_b32_e32	v1, 5, v3
	v_and_b32_e32	v1, 0x100, v3
	v_and_b32_e64	v1, v2, v3
	v_and_b32_e64	v1, s2, v3
	v_and_b32_e64	v1, 5, v3
	v_and_b32_e64	v1, 0x100, v3
	v_and_b32_e64	v1, v2, s3
	v_and_b32_e64	v1, v2, 5
	v_and_b32_e64	v1, v2, 0x100
	v_and_b32_e64	v1, 0x100, s3
	v_xor_b32	v1, v2, v3
	v_xor_b32	v255, v254, v253
	v_xor_b32	v0, v0, v0
	v_xor_b32	v1, s2, v3
	v_xor_b32	v1, s105, v3
	v_xor_b32	v1, 5, v3
	v_xor_b32	v1, -16, v3
	v_xor_b32	v1, 64, v3
	v_xor_b32	v1, 0x100, v3
	v_xor_b32	v1, 65, v3
	v_xor_b32	v1, 0xffffffff, v3
	v_xor_b32	v1, v2, s3
	v_xor_b32	v1, v2, s105
	v_xor_b32	v1, v2, 5
	v_xor_b32	v1, v2, -1
	v_xor_b32	v1, v2, 0x100
	v_xor_b32	v1, s2, s3
	v_xor_b32	v1, s2, 5
	v_xor_b32	v1, 5, 6
	v_xor_b32	v1, 0x100, s3
	v_xor_b32	v1, s2, 0x100
	v_xor_b32	v1, 0x100, 0x100
	v_xor_b32	v1, 0x100, 5
	v_xor_b32_e32	v1, v2, v3
	v_xor_b32_e32	v1, s2, v3
	v_xor_b32_e32	v1, 5, v3
	v_xor_b32_e32	v1, 0x100, v3
	v_xor_b32_e64	v1, v2, v3
	v_xor_b32_e64	v1, s2, v3
	v_xor_b32_e64	v1, 5, v3
	v_xor_b32_e64	v1, 0x100, v3
	v_xor_b32_e64	v1, v2, s3
	v_xor_b32_e64	v1, v2, 5
	v_xor_b32_e64	v1, v2, 0x100
	v_xor_b32_e64	v1, 0x100, s3
	v_lshrrev_b32	v1, v2, v3
	v_lshrrev_b32	v255, v254, v253
	v_lshrrev_b32	v0, v0, v0
	v_lshrrev_b32	v1, s2, v3
	v_lshrrev_b32	v1, s105, v3
	v_lshrrev_b32	v1, 5, v3
	v_lshrrev_b32	v1, -16, v3
	v_lshrrev_b32	v1, 64, v3
	v_lshrrev_b32	v1, 0x100, v3
	v_lshrrev_b32	v1, 65, v3
	v_lshrrev_b32	v1, 0xffffffff, v3
	v_lshrrev_b32	v1, v2, s3
	v_lshrrev_b32	v1, v2, s105
	v_lshrrev_b32	v1, v2, 5
	v_lshrrev_b32	v1, v2, -1
	v_lshrrev_b32	v1, v2, 0x100
	v_lshrrev_b32	v1, s2, s3
	v_lshrrev_b32	v1, s2, 5
	v_lshrrev_b32	v1, 5, 6
	v_lshrrev_b32	v1, 0x100, s3
	v_lshrrev_b32	v1, s2, 0x100
	v_lshrrev_b32	v1, 0x100, 0x100
	v_lshrrev_b32	v1, 0x100, 5
	v_lshrrev_b32_e32	v1, v2, v3
	v_lshrrev_b32_e32	v1, s2, v3
	v_lshrrev_b32_e32	v1, 5, v3
	v_lshrrev_b32_e32	v1, 0x100, v3
	v_lshrrev_b32_e64	v1, v2, v3
	v_lshrrev_b32_e64	v1, s2, v3
	v_lshrrev_b32_e64	v1, 5, v3
	v_lshrrev_b32_e64	v1, 0x100, v3
	v_lshrrev_b32_e64	v1, v2, s3
	v_lshrrev_b32_e64	v1, v2, 5
	v_lshrrev_b32_e64	v1, v2, 0x100
	v_lshrrev_b32_e64	v1, 0x100, s3
	v_lshlrev_b32	v1, v2, v3
	v_lshlrev_b32	v255, v254, v253
	v_lshlrev_b32	v0, v0, v0
	v_lshlrev_b32	v1, s2, v3
	v_lshlrev_b32	v1, s105, v3
	v_lshlrev_b32	v1, 5, v3
	v_lshlrev_b32	v1, -16, v3
	v_lshlrev_b32	v1, 64, v3
	v_lshlrev_b32	v1, 0x100, v3
	v_lshlrev_b32	v1, 65, v3
	v_lshlrev_b32	v1, 0xffffffff, v3
	v_lshlrev_b32	v1, v2, s3
	v_lshlrev_b32	v1, v2, s105
	v_lshlrev_b32	v1, v2, 5
	v_lshlrev_b32	v1, v2, -1
	v_lshlrev_b32	v1, v2, 0x100
	v_lshlrev_b32	v1, s2, s3
	v_lshlrev_b32	v1, s2, 5
	v_lshlrev_b32	v1, 5, 6
	v_lshlrev_b32	v1, 0x100, s3
	v_lshlrev_b32	v1, s2, 0x100
	v_lshlrev_b32	v1, 0x100, 0x100
	v_lshlrev_b32	v1, 0x100, 5
	v_lshlrev_b32_e32	v1, v2, v3
	v_lshlrev_b32_e32	v1, s2, v3
	v_lshlrev_b32_e32	v1, 5, v3
	v_lshlrev_b32_e32	v1, 0x100, v3
	v_lshlrev_b32_e64	v1, v2, v3
	v_lshlrev_b32_e64	v1, s2, v3
	v_lshlrev_b32_e64	v1, 5, v3
	v_lshlrev_b32_e64	v1, 0x100, v3
	v_lshlrev_b32_e64	v1, v2, s3
	v_lshlrev_b32_e64	v1, v2, 5
	v_lshlrev_b32_e64	v1, v2, 0x100
	v_lshlrev_b32_e64	v1, 0x100, s3
	v_max_i32	v1, v2, v3
	v_max_i32	v255, v254, v253
	v_max_i32	v0, v0, v0
	v_max_i32	v1, s2, v3
	v_max_i32	v1, s105, v3
	v_max_i32	v1, 5, v3
	v_max_i32	v1, -16, v3
	v_max_i32	v1, 64, v3
	v_max_i32	v1, 0x100, v3
	v_max_i32	v1, 65, v3
	v_max_i32	v1, 0xffffffff, v3
	v_max_i32	v1, v2, s3
	v_max_i32	v1, v2, s105
	v_max_i32	v1, v2, 5
	v_max_i32	v1, v2, -1
	v_max_i32	v1, v2, 0x100
	v_max_i32	v1, s2, s3
	v_max_i32	v1, s2, 5
	v_max_i32	v1, 5, 6
	v_max_i32	v1, 0x100, s3
	v_max_i32	v1, s2, 0x100
	v_max_i32	v1, 0x100, 0x100
	v_max_i32	v1, 0x100, 5
	v_max_i32_e32	v1, v2, v3
	v_max_i32_e32	v1, s2, v3
	v_max_i32_e32	v1, 5, v3
	v_max_i32_e32	v1, 0x100, v3
	v_max_i32_e64	v1, v2, v3
	v_max_i32_e64	v1, s2, v3
	v_max_i32_e64	v1, 5, v3
	v_max_i32_e64	v1, 0x100, v3
	v_max_i32_e64	v1, v2, s3
	v_max_i32_e64	v1, v2, 5
	v_max_i32_e64	v1, v2, 0x100
	v_max_i32_e64	v1, 0x100, s3
	v_min_i32	v1, v2, v3
	v_min_i32	v255, v254, v253
	v_min_i32	v0, v0, v0
	v_min_i32	v1, s2, v3
	v_min_i32	v1, s105, v3
	v_min_i32	v1, 5, v3
	v_min_i32	v1, -16, v3
	v_min_i32	v1, 64, v3
	v_min_i32	v1, 0x100, v3
	v_min_i32	v1, 65, v3
	v_min_i32	v1, 0xffffffff, v3
	v_min_i32	v1, v2, s3
	v_min_i32	v1, v2, s105
	v_min_i32	v1, v2, 5
	v_min_i32	v1, v2, -1
	v_min_i32	v1, v2, 0x100
	v_min_i32	v1, s2, s3
	v_min_i32	v1, s2, 5
	v_min_i32	v1, 5, 6
	v_min_i32	v1, 0x100, s3
	v_min_i32	v1, s2, 0x100
	v_min_i32	v1, 0x100, 0x100
	v_min_i32	v1, 0x100, 5
	v_min_i32_e32	v1, v2, v3
	v_min_i32_e32	v1, s2, v3
	v_min_i32_e32	v1, 5, v3
	v_min_i32_e32	v1, 0x100, v3
	v_min_i32_e64	v1, v2, v3
	v_min_i32_e64	v1, s2, v3
	v_min_i32_e64	v1, 5, v3
	v_min_i32_e64	v1, 0x100, v3
	v_min_i32_e64	v1, v2, s3
	v_min_i32_e64	v1, v2, 5
	v_min_i32_e64	v1, v2, 0x100
	v_min_i32_e64	v1, 0x100, s3
	v_mul_u32_u24	v1, v2, v3
	v_mul_u32_u24	v255, v254, v253
	v_mul_u32_u24	v0, v0, v0
	v_mul_u32_u24	v1, s2, v3
	v_mul_u32_u24	v1, s105, v3
	v_mul_u32_u24	v1, 5, v3
	v_mul_u32_u24	v1, -16, v3
	v_mul_u32_u24	v1, 64, v3
	v_mul_u32_u24	v1, 0x100, v3
	v_mul_u32_u24	v1, 65, v3
	v_mul_u32_u24	v1, 0xffffffff, v3
	v_mul_u32_u24	v1, v2, s3
	v_mul_u32_u24	v1, v2, s105
	v_mul_u32_u24	v1, v2, 5
	v_mul_u32_u24	v1, v2, -1
	v_mul_u32_u24	v1, v2, 0x100
	v_mul_u32_u24	v1, s2, s3
	v_mul_u32_u24	v1, s2, 5
	v_mul_u32_u24	v1, 5, 6
	v_mul_u32_u24	v1, 0x100, s3
	v_mul_u32_u24	v1, s2, 0x100
	v_mul_u32_u24	v1, 0x100, 0x100
	v_mul_u32_u24	v1, 0x100, 5
	v_mul_u32_u24_e32	v1, v2, v3
	v_mul_u32_u24_e32	v1, s2, v3
	v_mul_u32_u24_e32	v1, 5, v3
	v_mul_u32_u24_e32	v1, 0x100, v3
	v_mul_u32_u24_e64	v1, v2, v3
	v_mul_u32_u24_e64	v1, s2, v3
	v_mul_u32_u24_e64	v1, 5, v3
	v_mul_u32_u24_e64	v1, 0x100, v3
	v_mul_u32_u24_e64	v1, v2, s3
	v_mul_u32_u24_e64	v1, v2, 5
	v_mul_u32_u24_e64	v1, v2, 0x100
	v_mul_u32_u24_e64	v1, 0x100, s3
	v_mul_u32_u24	v1, v2, v3 clamp
	v_mul_u32_u24_e64	v1, v2, v3 clamp
	v_mul_u32_u24	v1, v2, s3 clamp
	v_max_u32	v1, v2, v3
	v_max_u32	v255, v254, v253
	v_max_u32	v0, v0, v0
	v_max_u32	v1, s2, v3
	v_max_u32	v1, s105, v3
	v_max_u32	v1, 5, v3
	v_max_u32	v1, -16, v3
	v_max_u32	v1, 64, v3
	v_max_u32	v1, 0x100, v3
	v_max_u32	v1, 65, v3
	v_max_u32	v1, 0xffffffff, v3
	v_max_u32	v1, v2, s3
	v_max_u32	v1, v2, s105
	v_max_u32	v1, v2, 5
	v_max_u32	v1, v2, -1
	v_max_u32	v1, v2, 0x100
	v_max_u32	v1, s2, s3
	v_max_u32	v1, s2, 5
	v_max_u32	v1, 5, 6
	v_max_u32	v1, 0x100, s3
	v_max_u32	v1, s2, 0x100
	v_max_u32	v1, 0x100, 0x100
	v_max_u32	v1, 0x100, 5
	v_max_u32_e32	v1, v2, v3
	v_max_u32_e32	v1, s2, v3
	v_max_u32_e32	v1, 5, v3
	v_max_u32_e32	v1, 0x100, v3
	v_max_u32_e64	v1, v2, v3
	v_max_u32_e64	v1, s2, v3
	v_max_u32_e64	v1, 5, v3
	v_max_u32_e64	v1, 0x100, v3
	v_max_u32_e64	v1, v2, s3
	v_max_u32_e64	v1, v2, 5
	v_max_u32_e64	v1, v2, 0x100
	v_max_u32_e64	v1, 0x100, s3
	v_min_u32	v1, v2, v3
	v_min_u32	v255, v254, v253
	v_min_u32	v0, v0, v0
	v_min_u32	v1, s2, v3
	v_min_u32	v1, s105, v3
	v_min_u32	v1, 5, v3
	v_min_u32	v1, -16, v3
	v_min_u32	v1, 64, v3
	v_min_u32	v1, 0x100, v3
	v_min_u32	v1, 65, v3
	v_min_u32	v1, 0xffffffff, v3
	v_min_u32	v1, v2, s3
	v_min_u32	v1, v2, s105
	v_min_u32	v1, v2, 5
	v_min_u32	v1, v2, -1
	v_min_u32	v1, v2, 0x100
	v_min_u32	v1, s2, s3
	v_min_u32	v1, s2, 5
	v_min_u32	v1, 5, 6
	v_min_u32	v1, 0x100, s3
	v_min_u32	v1, s2, 0x100
	v_min_u32	v1, 0x100, 0x100
	v_min_u32	v1, 0x100, 5
	v_min_u32_e32	v1, v2, v3
	v_min_u32_e32	v1, s2, v3
	v_min_u32_e32	v1, 5, v3
	v_min_u32_e32	v1, 0x100, v3
	v_min_u32_e64	v1, v2, v3
	v_min_u32_e64	v1, s2, v3
	v_min_u32_e64	v1, 5, v3
	v_min_u32_e64	v1, 0x100, v3
	v_min_u32_e64	v1, v2, s3
	v_min_u32_e64	v1, v2, 5
	v_min_u32_e64	v1, v2, 0x100
	v_min_u32_e64	v1, 0x100, s3
	v_ashrrev_i32	v1, v2, v3
	v_ashrrev_i32	v255, v254, v253
	v_ashrrev_i32	v0, v0, v0
	v_ashrrev_i32	v1, s2, v3
	v_ashrrev_i32	v1, s105, v3
	v_ashrrev_i32	v1, 5, v3
	v_ashrrev_i32	v1, -16, v3
	v_ashrrev_i32	v1, 64, v3
	v_ashrrev_i32	v1, 0x100, v3
	v_ashrrev_i32	v1, 65, v3
	v_ashrrev_i32	v1, 0xffffffff, v3
	v_ashrrev_i32	v1, v2, s3
	v_ashrrev_i32	v1, v2, s105
	v_ashrrev_i32	v1, v2, 5
	v_ashrrev_i32	v1, v2, -1
	v_ashrrev_i32	v1, v2, 0x100
	v_ashrrev_i32	v1, s2, s3
	v_ashrrev_i32	v1, s2, 5
	v_ashrrev_i32	v1, 5, 6
	v_ashrrev_i32	v1, 0x100, s3
	v_ashrrev_i32	v1, s2, 0x100
	v_ashrrev_i32	v1, 0x100, 0x100
	v_ashrrev_i32	v1, 0x100, 5
	v_ashrrev_i32_e32	v1, v2, v3
	v_ashrrev_i32_e32	v1, s2, v3
	v_ashrrev_i32_e32	v1, 5, v3
	v_ashrrev_i32_e32	v1, 0x100, v3
	v_ashrrev_i32_e64	v1, v2, v3
	v_ashrrev_i32_e64	v1, s2, v3
	v_ashrrev_i32_e64	v1, 5, v3
	v_ashrrev_i32_e64	v1, 0x100, v3
	v_ashrrev_i32_e64	v1, v2, s3
	v_ashrrev_i32_e64	v1, v2, 5
	v_ashrrev_i32_e64	v1, v2, 0x100
	v_ashrrev_i32_e64	v1, 0x100, s3
	v_mul_f32	v1, v2, v3
	v_mul_f32	v255, v254, v253
	v_mul_f32	v0, v0, v0
	v_mul_f32	v1, s2, v3
	v_mul_f32	v1, s105, v3
	v_mul_f32	v1, 5, v3
	v_mul_f32	v1, -16, v3
	v_mul_f32	v1, 64, v3
	v_mul_f32	v1, 0x100, v3
	v_mul_f32	v1, 65, v3
	v_mul_f32	v1, 0xffffffff, v3
	v_mul_f32	v1, v2, s3
	v_mul_f32	v1, v2, s105
	v_mul_f32	v1, v2, 5
	v_mul_f32	v1, v2, -1
	v_mul_f32	v1, v2, 0x100
	v_mul_f32	v1, s2, s3
	v_mul_f32	v1, s2, 5
	v_mul_f32	v1, 5, 6
	v_mul_f32	v1, 0x100, s3
	v_mul_f32	v1, s2, 0x100
	v_mul_f32	v1, 0x100, 0x100
	v_mul_f32	v1, 0x100, 5
	v_mul_f32_e32	v1, v2, v3
	v_mul_f32_e32	v1, s2, v3
	v_mul_f32_e32	v1, 5, v3
	v_mul_f32_e32	v1, 0x100, v3
	v_mul_f32_e64	v1, v2, v3
	v_mul_f32_e64	v1, s2, v3
	v_mul_f32_e64	v1, 5, v3
	v_mul_f32_e64	v1, 0x100, v3
	v_mul_f32_e64	v1, v2, s3
	v_mul_f32_e64	v1, v2, 5
	v_mul_f32_e64	v1, v2, 0x100
	v_mul_f32_e64	v1, 0x100, s3
	v_mul_f32	v1, v2, v3 clamp
	v_mul_f32_e64	v1, v2, v3 clamp
	v_mul_f32	v1, v2, s3 clamp
	v_mul_f32	v48, 0x3f000000, v48
	v_mul_f32	v48, v2, 0x3f000000
	v_mul_f32_e64	v48, 0x3f000000, v3
	v_mul_f32_e64	v48, v2, 0x3f000000
	v_mul_f32_e32	v48, 0x3f000000, v3
	v_mul_f32	v48, 0xbf000000, v48
	v_mul_f32	v48, v2, 0xbf000000
	v_mul_f32_e64	v48, 0xbf000000, v3
	v_mul_f32_e64	v48, v2, 0xbf000000
	v_mul_f32_e32	v48, 0xbf000000, v3
	v_mul_f32	v48, 0x3f800000, v48
	v_mul_f32	v48, v2, 0x3f800000
	v_mul_f32_e64	v48, 0x3f800000, v3
	v_mul_f32_e64	v48, v2, 0x3f800000
	v_mul_f32_e32	v48, 0x3f800000, v3
	v_mul_f32	v48, 0xbf800000, v48
	v_mul_f32	v48, v2, 0xbf800000
	v_mul_f32_e64	v48, 0xbf800000, v3
	v_mul_f32_e64	v48, v2, 0xbf800000
	v_mul_f32_e32	v48, 0xbf800000, v3
	v_mul_f32	v48, 0x40000000, v48
	v_mul_f32	v48, v2, 0x40000000
	v_mul_f32_e64	v48, 0x40000000, v3
	v_mul_f32_e64	v48, v2, 0x40000000
	v_mul_f32_e32	v48, 0x40000000, v3
	v_mul_f32	v48, 0xc0000000, v48
	v_mul_f32	v48, v2, 0xc0000000
	v_mul_f32_e64	v48, 0xc0000000, v3
	v_mul_f32_e64	v48, v2, 0xc0000000
	v_mul_f32_e32	v48, 0xc0000000, v3
	v_mul_f32	v48, 0x40800000, v48
	v_mul_f32	v48, v2, 0x40800000
	v_mul_f32_e64	v48, 0x40800000, v3
	v_mul_f32_e64	v48, v2, 0x40800000
	v_mul_f32_e32	v48, 0x40800000, v3
	v_mul_f32	v48, 0xc0800000, v48
	v_mul_f32	v48, v2, 0xc0800000
	v_mul_f32_e64	v48, 0xc0800000, v3
	v_mul_f32_e64	v48, v2, 0xc0800000
	v_mul_f32_e32	v48, 0xc0800000, v3
	v_mul_f32	v48, 0x3e22f983, v48
	v_mul_f32	v48, v2, 0x3e22f983
	v_mul_f32_e64	v48, 0x3e22f983, v3
	v_mul_f32_e64	v48, v2, 0x3e22f983
	v_mul_f32_e32	v48, 0x3e22f983, v3
	v_mul_f32	v48, 0x47800000, v48
	v_mul_f32	v48, v2, 0x47800000
	v_mul_f32_e64	v48, 0x47800000, v3
	v_mul_f32_e64	v48, v2, 0x47800000
	v_mul_f32_e32	v48, 0x47800000, v3
	v_mul_f32	v48, 0x3f800001, v48
	v_mul_f32	v48, v2, 0x3f800001
	v_mul_f32_e64	v48, 0x3f800001, v3
	v_mul_f32_e64	v48, v2, 0x3f800001
	v_mul_f32_e32	v48, 0x3f800001, v3
	s_endpgm
.size vop2, .-vop2

; VOPC: vcc or a pair for the result, 32 and 64 bits
.globl vopc
.p2align 8
.type vopc,@function
vopc:
	v_cmp_eq_u32	vcc, v2, v3
	v_cmp_eq_u32	vcc, v255, v254
	v_cmp_eq_u32	vcc, s2, v3
	v_cmp_eq_u32	vcc, s105, v3
	v_cmp_eq_u32	vcc, 5, v3
	v_cmp_eq_u32	vcc, -16, v3
	v_cmp_eq_u32	vcc, 0x100, v3
	v_cmp_eq_u32	vcc, 0xffffffef, v3
	v_cmp_eq_u32	vcc, v2, s3
	v_cmp_eq_u32	vcc, v2, 5
	v_cmp_eq_u32	vcc, v2, 0x100
	v_cmp_eq_u32	vcc, s2, s3
	v_cmp_eq_u32	vcc, 0x100, s3
	v_cmp_eq_u32	s[0:1], v2, v3
	v_cmp_eq_u32	s[104:105], v2, v3
	v_cmp_eq_u32	s[2:3], s4, v3
	v_cmp_eq_u32	s[2:3], 0x100, v3
	v_cmp_eq_u32	s[2:3], v2, 0x100
	v_cmp_eq_u32	exec, v2, v3
	v_cmp_eq_u32	s[2:3], 5, 6
	v_cmp_eq_u32_e32	vcc, v2, v3
	v_cmp_eq_u32_e32	vcc, s2, v3
	v_cmp_eq_u32_e32	vcc, 5, v3
	v_cmp_eq_u32_e32	vcc, 0x100, v3
	v_cmp_eq_u32_e64	vcc, v2, v3
	v_cmp_eq_u32_e64	vcc, s2, v3
	v_cmp_eq_u32_e64	vcc, 5, v3
	v_cmp_eq_u32_e64	vcc, 0x100, v3
	v_cmp_eq_u32_e64	s[4:5], v2, v3
	v_cmp_eq_u32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_eq_u32_e64	exec, v2, v3
	v_cmp_eq_u32_e64	vcc, v2, s3
	v_cmp_eq_u32_e64	s[0:1], s2, s3
	v_cmp_ne_u32	vcc, v2, v3
	v_cmp_ne_u32	vcc, v255, v254
	v_cmp_ne_u32	vcc, s2, v3
	v_cmp_ne_u32	vcc, s105, v3
	v_cmp_ne_u32	vcc, 5, v3
	v_cmp_ne_u32	vcc, -16, v3
	v_cmp_ne_u32	vcc, 0x100, v3
	v_cmp_ne_u32	vcc, 0xffffffef, v3
	v_cmp_ne_u32	vcc, v2, s3
	v_cmp_ne_u32	vcc, v2, 5
	v_cmp_ne_u32	vcc, v2, 0x100
	v_cmp_ne_u32	vcc, s2, s3
	v_cmp_ne_u32	vcc, 0x100, s3
	v_cmp_ne_u32	s[0:1], v2, v3
	v_cmp_ne_u32	s[104:105], v2, v3
	v_cmp_ne_u32	s[2:3], s4, v3
	v_cmp_ne_u32	s[2:3], 0x100, v3
	v_cmp_ne_u32	s[2:3], v2, 0x100
	v_cmp_ne_u32	exec, v2, v3
	v_cmp_ne_u32	s[2:3], 5, 6
	v_cmp_ne_u32_e32	vcc, v2, v3
	v_cmp_ne_u32_e32	vcc, s2, v3
	v_cmp_ne_u32_e32	vcc, 5, v3
	v_cmp_ne_u32_e32	vcc, 0x100, v3
	v_cmp_ne_u32_e64	vcc, v2, v3
	v_cmp_ne_u32_e64	vcc, s2, v3
	v_cmp_ne_u32_e64	vcc, 5, v3
	v_cmp_ne_u32_e64	vcc, 0x100, v3
	v_cmp_ne_u32_e64	s[4:5], v2, v3
	v_cmp_ne_u32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_ne_u32_e64	exec, v2, v3
	v_cmp_ne_u32_e64	vcc, v2, s3
	v_cmp_ne_u32_e64	s[0:1], s2, s3
	v_cmp_gt_u32	vcc, v2, v3
	v_cmp_gt_u32	vcc, v255, v254
	v_cmp_gt_u32	vcc, s2, v3
	v_cmp_gt_u32	vcc, s105, v3
	v_cmp_gt_u32	vcc, 5, v3
	v_cmp_gt_u32	vcc, -16, v3
	v_cmp_gt_u32	vcc, 0x100, v3
	v_cmp_gt_u32	vcc, 0xffffffef, v3
	v_cmp_gt_u32	vcc, v2, s3
	v_cmp_gt_u32	vcc, v2, 5
	v_cmp_gt_u32	vcc, v2, 0x100
	v_cmp_gt_u32	vcc, s2, s3
	v_cmp_gt_u32	vcc, 0x100, s3
	v_cmp_gt_u32	s[0:1], v2, v3
	v_cmp_gt_u32	s[104:105], v2, v3
	v_cmp_gt_u32	s[2:3], s4, v3
	v_cmp_gt_u32	s[2:3], 0x100, v3
	v_cmp_gt_u32	s[2:3], v2, 0x100
	v_cmp_gt_u32	exec, v2, v3
	v_cmp_gt_u32	s[2:3], 5, 6
	v_cmp_gt_u32_e32	vcc, v2, v3
	v_cmp_gt_u32_e32	vcc, s2, v3
	v_cmp_gt_u32_e32	vcc, 5, v3
	v_cmp_gt_u32_e32	vcc, 0x100, v3
	v_cmp_gt_u32_e64	vcc, v2, v3
	v_cmp_gt_u32_e64	vcc, s2, v3
	v_cmp_gt_u32_e64	vcc, 5, v3
	v_cmp_gt_u32_e64	vcc, 0x100, v3
	v_cmp_gt_u32_e64	s[4:5], v2, v3
	v_cmp_gt_u32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_gt_u32_e64	exec, v2, v3
	v_cmp_gt_u32_e64	vcc, v2, s3
	v_cmp_gt_u32_e64	s[0:1], s2, s3
	v_cmp_le_u32	vcc, v2, v3
	v_cmp_le_u32	vcc, v255, v254
	v_cmp_le_u32	vcc, s2, v3
	v_cmp_le_u32	vcc, s105, v3
	v_cmp_le_u32	vcc, 5, v3
	v_cmp_le_u32	vcc, -16, v3
	v_cmp_le_u32	vcc, 0x100, v3
	v_cmp_le_u32	vcc, 0xffffffef, v3
	v_cmp_le_u32	vcc, v2, s3
	v_cmp_le_u32	vcc, v2, 5
	v_cmp_le_u32	vcc, v2, 0x100
	v_cmp_le_u32	vcc, s2, s3
	v_cmp_le_u32	vcc, 0x100, s3
	v_cmp_le_u32	s[0:1], v2, v3
	v_cmp_le_u32	s[104:105], v2, v3
	v_cmp_le_u32	s[2:3], s4, v3
	v_cmp_le_u32	s[2:3], 0x100, v3
	v_cmp_le_u32	s[2:3], v2, 0x100
	v_cmp_le_u32	exec, v2, v3
	v_cmp_le_u32	s[2:3], 5, 6
	v_cmp_le_u32_e32	vcc, v2, v3
	v_cmp_le_u32_e32	vcc, s2, v3
	v_cmp_le_u32_e32	vcc, 5, v3
	v_cmp_le_u32_e32	vcc, 0x100, v3
	v_cmp_le_u32_e64	vcc, v2, v3
	v_cmp_le_u32_e64	vcc, s2, v3
	v_cmp_le_u32_e64	vcc, 5, v3
	v_cmp_le_u32_e64	vcc, 0x100, v3
	v_cmp_le_u32_e64	s[4:5], v2, v3
	v_cmp_le_u32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_le_u32_e64	exec, v2, v3
	v_cmp_le_u32_e64	vcc, v2, s3
	v_cmp_le_u32_e64	s[0:1], s2, s3
	v_cmp_lt_u32	vcc, v2, v3
	v_cmp_lt_u32	vcc, v255, v254
	v_cmp_lt_u32	vcc, s2, v3
	v_cmp_lt_u32	vcc, s105, v3
	v_cmp_lt_u32	vcc, 5, v3
	v_cmp_lt_u32	vcc, -16, v3
	v_cmp_lt_u32	vcc, 0x100, v3
	v_cmp_lt_u32	vcc, 0xffffffef, v3
	v_cmp_lt_u32	vcc, v2, s3
	v_cmp_lt_u32	vcc, v2, 5
	v_cmp_lt_u32	vcc, v2, 0x100
	v_cmp_lt_u32	vcc, s2, s3
	v_cmp_lt_u32	vcc, 0x100, s3
	v_cmp_lt_u32	s[0:1], v2, v3
	v_cmp_lt_u32	s[104:105], v2, v3
	v_cmp_lt_u32	s[2:3], s4, v3
	v_cmp_lt_u32	s[2:3], 0x100, v3
	v_cmp_lt_u32	s[2:3], v2, 0x100
	v_cmp_lt_u32	exec, v2, v3
	v_cmp_lt_u32	s[2:3], 5, 6
	v_cmp_lt_u32_e32	vcc, v2, v3
	v_cmp_lt_u32_e32	vcc, s2, v3
	v_cmp_lt_u32_e32	vcc, 5, v3
	v_cmp_lt_u32_e32	vcc, 0x100, v3
	v_cmp_lt_u32_e64	vcc, v2, v3
	v_cmp_lt_u32_e64	vcc, s2, v3
	v_cmp_lt_u32_e64	vcc, 5, v3
	v_cmp_lt_u32_e64	vcc, 0x100, v3
	v_cmp_lt_u32_e64	s[4:5], v2, v3
	v_cmp_lt_u32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_lt_u32_e64	exec, v2, v3
	v_cmp_lt_u32_e64	vcc, v2, s3
	v_cmp_lt_u32_e64	s[0:1], s2, s3
	v_cmp_ge_u32	vcc, v2, v3
	v_cmp_ge_u32	vcc, v255, v254
	v_cmp_ge_u32	vcc, s2, v3
	v_cmp_ge_u32	vcc, s105, v3
	v_cmp_ge_u32	vcc, 5, v3
	v_cmp_ge_u32	vcc, -16, v3
	v_cmp_ge_u32	vcc, 0x100, v3
	v_cmp_ge_u32	vcc, 0xffffffef, v3
	v_cmp_ge_u32	vcc, v2, s3
	v_cmp_ge_u32	vcc, v2, 5
	v_cmp_ge_u32	vcc, v2, 0x100
	v_cmp_ge_u32	vcc, s2, s3
	v_cmp_ge_u32	vcc, 0x100, s3
	v_cmp_ge_u32	s[0:1], v2, v3
	v_cmp_ge_u32	s[104:105], v2, v3
	v_cmp_ge_u32	s[2:3], s4, v3
	v_cmp_ge_u32	s[2:3], 0x100, v3
	v_cmp_ge_u32	s[2:3], v2, 0x100
	v_cmp_ge_u32	exec, v2, v3
	v_cmp_ge_u32	s[2:3], 5, 6
	v_cmp_ge_u32_e32	vcc, v2, v3
	v_cmp_ge_u32_e32	vcc, s2, v3
	v_cmp_ge_u32_e32	vcc, 5, v3
	v_cmp_ge_u32_e32	vcc, 0x100, v3
	v_cmp_ge_u32_e64	vcc, v2, v3
	v_cmp_ge_u32_e64	vcc, s2, v3
	v_cmp_ge_u32_e64	vcc, 5, v3
	v_cmp_ge_u32_e64	vcc, 0x100, v3
	v_cmp_ge_u32_e64	s[4:5], v2, v3
	v_cmp_ge_u32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_ge_u32_e64	exec, v2, v3
	v_cmp_ge_u32_e64	vcc, v2, s3
	v_cmp_ge_u32_e64	s[0:1], s2, s3
	v_cmp_gt_i32	vcc, v2, v3
	v_cmp_gt_i32	vcc, v255, v254
	v_cmp_gt_i32	vcc, s2, v3
	v_cmp_gt_i32	vcc, s105, v3
	v_cmp_gt_i32	vcc, 5, v3
	v_cmp_gt_i32	vcc, -16, v3
	v_cmp_gt_i32	vcc, 0x100, v3
	v_cmp_gt_i32	vcc, 0xffffffef, v3
	v_cmp_gt_i32	vcc, v2, s3
	v_cmp_gt_i32	vcc, v2, 5
	v_cmp_gt_i32	vcc, v2, 0x100
	v_cmp_gt_i32	vcc, s2, s3
	v_cmp_gt_i32	vcc, 0x100, s3
	v_cmp_gt_i32	s[0:1], v2, v3
	v_cmp_gt_i32	s[104:105], v2, v3
	v_cmp_gt_i32	s[2:3], s4, v3
	v_cmp_gt_i32	s[2:3], 0x100, v3
	v_cmp_gt_i32	s[2:3], v2, 0x100
	v_cmp_gt_i32	exec, v2, v3
	v_cmp_gt_i32	s[2:3], 5, 6
	v_cmp_gt_i32_e32	vcc, v2, v3
	v_cmp_gt_i32_e32	vcc, s2, v3
	v_cmp_gt_i32_e32	vcc, 5, v3
	v_cmp_gt_i32_e32	vcc, 0x100, v3
	v_cmp_gt_i32_e64	vcc, v2, v3
	v_cmp_gt_i32_e64	vcc, s2, v3
	v_cmp_gt_i32_e64	vcc, 5, v3
	v_cmp_gt_i32_e64	vcc, 0x100, v3
	v_cmp_gt_i32_e64	s[4:5], v2, v3
	v_cmp_gt_i32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_gt_i32_e64	exec, v2, v3
	v_cmp_gt_i32_e64	vcc, v2, s3
	v_cmp_gt_i32_e64	s[0:1], s2, s3
	v_cmp_le_i32	vcc, v2, v3
	v_cmp_le_i32	vcc, v255, v254
	v_cmp_le_i32	vcc, s2, v3
	v_cmp_le_i32	vcc, s105, v3
	v_cmp_le_i32	vcc, 5, v3
	v_cmp_le_i32	vcc, -16, v3
	v_cmp_le_i32	vcc, 0x100, v3
	v_cmp_le_i32	vcc, 0xffffffef, v3
	v_cmp_le_i32	vcc, v2, s3
	v_cmp_le_i32	vcc, v2, 5
	v_cmp_le_i32	vcc, v2, 0x100
	v_cmp_le_i32	vcc, s2, s3
	v_cmp_le_i32	vcc, 0x100, s3
	v_cmp_le_i32	s[0:1], v2, v3
	v_cmp_le_i32	s[104:105], v2, v3
	v_cmp_le_i32	s[2:3], s4, v3
	v_cmp_le_i32	s[2:3], 0x100, v3
	v_cmp_le_i32	s[2:3], v2, 0x100
	v_cmp_le_i32	exec, v2, v3
	v_cmp_le_i32	s[2:3], 5, 6
	v_cmp_le_i32_e32	vcc, v2, v3
	v_cmp_le_i32_e32	vcc, s2, v3
	v_cmp_le_i32_e32	vcc, 5, v3
	v_cmp_le_i32_e32	vcc, 0x100, v3
	v_cmp_le_i32_e64	vcc, v2, v3
	v_cmp_le_i32_e64	vcc, s2, v3
	v_cmp_le_i32_e64	vcc, 5, v3
	v_cmp_le_i32_e64	vcc, 0x100, v3
	v_cmp_le_i32_e64	s[4:5], v2, v3
	v_cmp_le_i32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_le_i32_e64	exec, v2, v3
	v_cmp_le_i32_e64	vcc, v2, s3
	v_cmp_le_i32_e64	s[0:1], s2, s3
	v_cmp_lt_i32	vcc, v2, v3
	v_cmp_lt_i32	vcc, v255, v254
	v_cmp_lt_i32	vcc, s2, v3
	v_cmp_lt_i32	vcc, s105, v3
	v_cmp_lt_i32	vcc, 5, v3
	v_cmp_lt_i32	vcc, -16, v3
	v_cmp_lt_i32	vcc, 0x100, v3
	v_cmp_lt_i32	vcc, 0xffffffef, v3
	v_cmp_lt_i32	vcc, v2, s3
	v_cmp_lt_i32	vcc, v2, 5
	v_cmp_lt_i32	vcc, v2, 0x100
	v_cmp_lt_i32	vcc, s2, s3
	v_cmp_lt_i32	vcc, 0x100, s3
	v_cmp_lt_i32	s[0:1], v2, v3
	v_cmp_lt_i32	s[104:105], v2, v3
	v_cmp_lt_i32	s[2:3], s4, v3
	v_cmp_lt_i32	s[2:3], 0x100, v3
	v_cmp_lt_i32	s[2:3], v2, 0x100
	v_cmp_lt_i32	exec, v2, v3
	v_cmp_lt_i32	s[2:3], 5, 6
	v_cmp_lt_i32_e32	vcc, v2, v3
	v_cmp_lt_i32_e32	vcc, s2, v3
	v_cmp_lt_i32_e32	vcc, 5, v3
	v_cmp_lt_i32_e32	vcc, 0x100, v3
	v_cmp_lt_i32_e64	vcc, v2, v3
	v_cmp_lt_i32_e64	vcc, s2, v3
	v_cmp_lt_i32_e64	vcc, 5, v3
	v_cmp_lt_i32_e64	vcc, 0x100, v3
	v_cmp_lt_i32_e64	s[4:5], v2, v3
	v_cmp_lt_i32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_lt_i32_e64	exec, v2, v3
	v_cmp_lt_i32_e64	vcc, v2, s3
	v_cmp_lt_i32_e64	s[0:1], s2, s3
	v_cmp_ge_i32	vcc, v2, v3
	v_cmp_ge_i32	vcc, v255, v254
	v_cmp_ge_i32	vcc, s2, v3
	v_cmp_ge_i32	vcc, s105, v3
	v_cmp_ge_i32	vcc, 5, v3
	v_cmp_ge_i32	vcc, -16, v3
	v_cmp_ge_i32	vcc, 0x100, v3
	v_cmp_ge_i32	vcc, 0xffffffef, v3
	v_cmp_ge_i32	vcc, v2, s3
	v_cmp_ge_i32	vcc, v2, 5
	v_cmp_ge_i32	vcc, v2, 0x100
	v_cmp_ge_i32	vcc, s2, s3
	v_cmp_ge_i32	vcc, 0x100, s3
	v_cmp_ge_i32	s[0:1], v2, v3
	v_cmp_ge_i32	s[104:105], v2, v3
	v_cmp_ge_i32	s[2:3], s4, v3
	v_cmp_ge_i32	s[2:3], 0x100, v3
	v_cmp_ge_i32	s[2:3], v2, 0x100
	v_cmp_ge_i32	exec, v2, v3
	v_cmp_ge_i32	s[2:3], 5, 6
	v_cmp_ge_i32_e32	vcc, v2, v3
	v_cmp_ge_i32_e32	vcc, s2, v3
	v_cmp_ge_i32_e32	vcc, 5, v3
	v_cmp_ge_i32_e32	vcc, 0x100, v3
	v_cmp_ge_i32_e64	vcc, v2, v3
	v_cmp_ge_i32_e64	vcc, s2, v3
	v_cmp_ge_i32_e64	vcc, 5, v3
	v_cmp_ge_i32_e64	vcc, 0x100, v3
	v_cmp_ge_i32_e64	s[4:5], v2, v3
	v_cmp_ge_i32_e64	s[88:89], 0x00ffffff, v34
	v_cmp_ge_i32_e64	exec, v2, v3
	v_cmp_ge_i32_e64	vcc, v2, s3
	v_cmp_ge_i32_e64	s[0:1], s2, s3
	v_cmp_gt_u64	vcc, v[2:3], v[4:5]
	v_cmp_gt_u64	vcc, v[254:255], v[252:253]
	v_cmp_gt_u64	vcc, s[2:3], v[4:5]
	v_cmp_gt_u64	vcc, vcc, v[4:5]
	v_cmp_gt_u64	vcc, exec, v[4:5]
	v_cmp_gt_u64	vcc, 5, v[4:5]
	v_cmp_gt_u64	vcc, -1, v[4:5]
	v_cmp_gt_u64	vcc, 0, v[4:5]
	v_cmp_gt_u64	vcc, v[2:3], s[4:5]
	v_cmp_gt_u64	vcc, v[2:3], vcc
	v_cmp_gt_u64	vcc, v[2:3], 5
	v_cmp_gt_u64	vcc, v[2:3], 64
	v_cmp_gt_u64	s[0:1], v[2:3], v[4:5]
	v_cmp_gt_u64	s[98:99], v[52:53], v[50:51]
	v_cmp_gt_u64	exec, v[2:3], v[4:5]
	v_cmp_gt_u64	s[0:1], s[2:3], s[4:5]
	v_cmp_gt_u64	s[0:1], s[2:3], 1
	v_cmp_gt_u64_e32	vcc, v[2:3], v[4:5]
	v_cmp_gt_u64_e32	vcc, s[2:3], v[4:5]
	v_cmp_gt_u64_e32	vcc, 5, v[4:5]
	v_cmp_gt_u64_e64	vcc, v[2:3], v[4:5]
	v_cmp_gt_u64_e64	s[4:5], v[2:3], v[4:5]
	v_cmp_gt_u64_e64	s[4:5], v[2:3], 5
	v_cmp_le_u64	vcc, v[2:3], v[4:5]
	v_cmp_le_u64	vcc, v[254:255], v[252:253]
	v_cmp_le_u64	vcc, s[2:3], v[4:5]
	v_cmp_le_u64	vcc, vcc, v[4:5]
	v_cmp_le_u64	vcc, exec, v[4:5]
	v_cmp_le_u64	vcc, 5, v[4:5]
	v_cmp_le_u64	vcc, -1, v[4:5]
	v_cmp_le_u64	vcc, 0, v[4:5]
	v_cmp_le_u64	vcc, v[2:3], s[4:5]
	v_cmp_le_u64	vcc, v[2:3], vcc
	v_cmp_le_u64	vcc, v[2:3], 5
	v_cmp_le_u64	vcc, v[2:3], 64
	v_cmp_le_u64	s[0:1], v[2:3], v[4:5]
	v_cmp_le_u64	s[98:99], v[52:53], v[50:51]
	v_cmp_le_u64	exec, v[2:3], v[4:5]
	v_cmp_le_u64	s[0:1], s[2:3], s[4:5]
	v_cmp_le_u64	s[0:1], s[2:3], 1
	v_cmp_le_u64_e32	vcc, v[2:3], v[4:5]
	v_cmp_le_u64_e32	vcc, s[2:3], v[4:5]
	v_cmp_le_u64_e32	vcc, 5, v[4:5]
	v_cmp_le_u64_e64	vcc, v[2:3], v[4:5]
	v_cmp_le_u64_e64	s[4:5], v[2:3], v[4:5]
	v_cmp_le_u64_e64	s[4:5], v[2:3], 5
	s_endpgm
.size vopc, .-vopc

; VOP3 forms with three sources and two
.globl vop3
.p2align 8
.type vop3,@function
vop3:
	v_cndmask_b32_e64	v1, v2, v3, s[4:5]
	v_cndmask_b32_e64	v1, 0, v3, s[4:5]
	v_cndmask_b32_e64	v1, v2, v3, vcc
	v_cndmask_b32_e64	v1, s2, v3, exec
	v_cndmask_b32_e64	v1, 0x100, v3, s[4:5]
	v_cndmask_b32_e64	v1, v2, 5, s[4:5]
	v_cndmask_b32_e64	v1, v2, s3, s[4:5]
	v_cndmask_b32_e64	v1, s2, v3, vcc
	v_cndmask_b32_e64	v255, v254, v253, s[104:105]
	v_cndmask_b32_e64	v1, -1, -16, s[4:5]
	v_cndmask_b32_e64	v1, v2, 0xffffffef, vcc
	v_mad_u32_u24	v1, v2, v3, v4
	v_mad_u32_u24	v255, v254, v253, v252
	v_mad_u32_u24	v1, s2, v3, v4
	v_mad_u32_u24	v1, v2, s3, v4
	v_mad_u32_u24	v1, v2, v3, s4
	v_mad_u32_u24	v1, v2, 6, v4
	v_mad_u32_u24	v1, 5, 6, 7
	v_mad_u32_u24	v1, -1, -16, 64
	v_mad_u32_u24	v1, v2, v3, 65
	v_mad_u32_u24	v1, 0x007f007f, v3, v4
	v_mad_u32_u24	v1, v2, 0x007f007f, v4
	v_mad_u32_u24	v1, v2, v3, 0x007f007f
	v_mad_u32_u24	v1, 0x007f007f, v3, 0x007f007f
	v_mad_u32_u24	v1, 0x007f007f, 0x007f007f, 0x007f007f
	v_mad_u32_u24	v1, s2, s2, v4
	v_mad_u32_u24	v1, s2, s3, v4
	v_mad_u32_u24	v1, s2, 0x100, 0x100
	v_mad_u32_u24	v1, s2, v3, 0x100
	v_mad_u32_u24	v1, s6, 6, v0
	v_mad_u32_u24	v1, v2, v3, 0xffffffff
	v_mad_u32_u24	v1, 0x10000, v2, v3
	v_mad_u32_u24	v1, v59, v58, 16
	v_mad_u32_u24	v1, v2, 0x80000000, v3
	v_mad_u32_u24_e64	v1, v2, v3, v4
	v_mad_u32_u24_e64	v1, s2, 0x100, v4
	v_mad_u32_u24	v1, v2, v3, v4 clamp
	v_mad_u32_u24_e64	v1, 0x100, v3, v4 clamp
	v_add3_u32	v1, v2, v3, v4
	v_add3_u32	v255, v254, v253, v252
	v_add3_u32	v1, s2, v3, v4
	v_add3_u32	v1, v2, s3, v4
	v_add3_u32	v1, v2, v3, s4
	v_add3_u32	v1, v2, 6, v4
	v_add3_u32	v1, 5, 6, 7
	v_add3_u32	v1, -1, -16, 64
	v_add3_u32	v1, v2, v3, 65
	v_add3_u32	v1, 0x007f007f, v3, v4
	v_add3_u32	v1, v2, 0x007f007f, v4
	v_add3_u32	v1, v2, v3, 0x007f007f
	v_add3_u32	v1, 0x007f007f, v3, 0x007f007f
	v_add3_u32	v1, 0x007f007f, 0x007f007f, 0x007f007f
	v_add3_u32	v1, s2, s2, v4
	v_add3_u32	v1, s2, s3, v4
	v_add3_u32	v1, s2, 0x100, 0x100
	v_add3_u32	v1, s2, v3, 0x100
	v_add3_u32	v1, s6, 6, v0
	v_add3_u32	v1, v2, v3, 0xffffffff
	v_add3_u32	v1, 0x10000, v2, v3
	v_add3_u32	v1, v59, v58, 16
	v_add3_u32	v1, v2, 0x80000000, v3
	v_add3_u32_e64	v1, v2, v3, v4
	v_add3_u32_e64	v1, s2, 0x100, v4
	v_lshl_or_b32	v1, v2, v3, v4
	v_lshl_or_b32	v255, v254, v253, v252
	v_lshl_or_b32	v1, s2, v3, v4
	v_lshl_or_b32	v1, v2, s3, v4
	v_lshl_or_b32	v1, v2, v3, s4
	v_lshl_or_b32	v1, v2, 6, v4
	v_lshl_or_b32	v1, 5, 6, 7
	v_lshl_or_b32	v1, -1, -16, 64
	v_lshl_or_b32	v1, v2, v3, 65
	v_lshl_or_b32	v1, 0x007f007f, v3, v4
	v_lshl_or_b32	v1, v2, 0x007f007f, v4
	v_lshl_or_b32	v1, v2, v3, 0x007f007f
	v_lshl_or_b32	v1, 0x007f007f, v3, 0x007f007f
	v_lshl_or_b32	v1, 0x007f007f, 0x007f007f, 0x007f007f
	v_lshl_or_b32	v1, s2, s2, v4
	v_lshl_or_b32	v1, s2, s3, v4
	v_lshl_or_b32	v1, s2, 0x100, 0x100
	v_lshl_or_b32	v1, s2, v3, 0x100
	v_lshl_or_b32	v1, s6, 6, v0
	v_lshl_or_b32	v1, v2, v3, 0xffffffff
	v_lshl_or_b32	v1, 0x10000, v2, v3
	v_lshl_or_b32	v1, v59, v58, 16
	v_lshl_or_b32	v1, v2, 0x80000000, v3
	v_lshl_or_b32_e64	v1, v2, v3, v4
	v_lshl_or_b32_e64	v1, s2, 0x100, v4
	v_lshl_add_u32	v1, v2, v3, v4
	v_lshl_add_u32	v255, v254, v253, v252
	v_lshl_add_u32	v1, s2, v3, v4
	v_lshl_add_u32	v1, v2, s3, v4
	v_lshl_add_u32	v1, v2, v3, s4
	v_lshl_add_u32	v1, v2, 6, v4
	v_lshl_add_u32	v1, 5, 6, 7
	v_lshl_add_u32	v1, -1, -16, 64
	v_lshl_add_u32	v1, v2, v3, 65
	v_lshl_add_u32	v1, 0x007f007f, v3, v4
	v_lshl_add_u32	v1, v2, 0x007f007f, v4
	v_lshl_add_u32	v1, v2, v3, 0x007f007f
	v_lshl_add_u32	v1, 0x007f007f, v3, 0x007f007f
	v_lshl_add_u32	v1, 0x007f007f, 0x007f007f, 0x007f007f
	v_lshl_add_u32	v1, s2, s2, v4
	v_lshl_add_u32	v1, s2, s3, v4
	v_lshl_add_u32	v1, s2, 0x100, 0x100
	v_lshl_add_u32	v1, s2, v3, 0x100
	v_lshl_add_u32	v1, s6, 6, v0
	v_lshl_add_u32	v1, v2, v3, 0xffffffff
	v_lshl_add_u32	v1, 0x10000, v2, v3
	v_lshl_add_u32	v1, v59, v58, 16
	v_lshl_add_u32	v1, v2, 0x80000000, v3
	v_lshl_add_u32_e64	v1, v2, v3, v4
	v_lshl_add_u32_e64	v1, s2, 0x100, v4
	v_alignbit_b32	v1, v2, v3, v4
	v_alignbit_b32	v255, v254, v253, v252
	v_alignbit_b32	v1, s2, v3, v4
	v_alignbit_b32	v1, v2, s3, v4
	v_alignbit_b32	v1, v2, v3, s4
	v_alignbit_b32	v1, v2, 6, v4
	v_alignbit_b32	v1, 5, 6, 7
	v_alignbit_b32	v1, -1, -16, 64
	v_alignbit_b32	v1, v2, v3, 65
	v_alignbit_b32	v1, 0x007f007f, v3, v4
	v_alignbit_b32	v1, v2, 0x007f007f, v4
	v_alignbit_b32	v1, v2, v3, 0x007f007f
	v_alignbit_b32	v1, 0x007f007f, v3, 0x007f007f
	v_alignbit_b32	v1, 0x007f007f, 0x007f007f, 0x007f007f
	v_alignbit_b32	v1, s2, s2, v4
	v_alignbit_b32	v1, s2, s3, v4
	v_alignbit_b32	v1, s2, 0x100, 0x100
	v_alignbit_b32	v1, s2, v3, 0x100
	v_alignbit_b32	v1, s6, 6, v0
	v_alignbit_b32	v1, v2, v3, 0xffffffff
	v_alignbit_b32	v1, 0x10000, v2, v3
	v_alignbit_b32	v1, v59, v58, 16
	v_alignbit_b32	v1, v2, 0x80000000, v3
	v_alignbit_b32_e64	v1, v2, v3, v4
	v_alignbit_b32_e64	v1, s2, 0x100, v4
	v_alignbit_b32	v1, v2, v3, v4 clamp
	v_alignbit_b32_e64	v1, 0x100, v3, v4 clamp
	v_bfe_u32	v1, v2, v3, v4
	v_bfe_u32	v255, v254, v253, v252
	v_bfe_u32	v1, s2, v3, v4
	v_bfe_u32	v1, v2, s3, v4
	v_bfe_u32	v1, v2, v3, s4
	v_bfe_u32	v1, v2, 6, v4
	v_bfe_u32	v1, 5, 6, 7
	v_bfe_u32	v1, -1, -16, 64
	v_bfe_u32	v1, v2, v3, 65
	v_bfe_u32	v1, 0x007f007f, v3, v4
	v_bfe_u32	v1, v2, 0x007f007f, v4
	v_bfe_u32	v1, v2, v3, 0x007f007f
	v_bfe_u32	v1, 0x007f007f, v3, 0x007f007f
	v_bfe_u32	v1, 0x007f007f, 0x007f007f, 0x007f007f
	v_bfe_u32	v1, s2, s2, v4
	v_bfe_u32	v1, s2, s3, v4
	v_bfe_u32	v1, s2, 0x100, 0x100
	v_bfe_u32	v1, s2, v3, 0x100
	v_bfe_u32	v1, s6, 6, v0
	v_bfe_u32	v1, v2, v3, 0xffffffff
	v_bfe_u32	v1, 0x10000, v2, v3
	v_bfe_u32	v1, v59, v58, 16
	v_bfe_u32	v1, v2, 0x80000000, v3
	v_bfe_u32_e64	v1, v2, v3, v4
	v_bfe_u32_e64	v1, s2, 0x100, v4
	v_mul_lo_u32	v1, v2, v3
	v_mul_lo_u32	v255, v254, v253
	v_mul_lo_u32	v1, s2, v3
	v_mul_lo_u32	v1, v2, s3
	v_mul_lo_u32	v1, 5, v3
	v_mul_lo_u32	v1, v2, -16
	v_mul_lo_u32	v1, 0x100, v3
	v_mul_lo_u32	v1, v2, 0x100
	v_mul_lo_u32	v1, s2, s3
	v_mul_lo_u32	v1, s2, 0x100
	v_mul_lo_u32	v1, 0x100, 0x100
	v_mul_lo_u32	v48, v48, v48
	v_mul_lo_u32_e64	v1, v2, v3
	s_endpgm
.size vop3, .-vop3

; VOP3B: the carry to null, vcc, exec or a pair
.globl vop3b
.p2align 8
.type vop3b,@function
vop3b:
	v_mad_u64_u32	v[2:3], null, v4, v5, 0
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, 0
	v_mad_u64_u32	v[2:3], null, v4, v5, v[6:7]
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, v[6:7]
	v_mad_u64_u32	v[2:3], null, v4, s5, 0
	v_mad_u64_u32_e64	v[2:3], null, v4, s5, 0
	v_mad_u64_u32	v[2:3], null, s4, v5, s[6:7]
	v_mad_u64_u32_e64	v[2:3], null, s4, v5, s[6:7]
	v_mad_u64_u32	v[252:253], null, v254, v255, v[250:251]
	v_mad_u64_u32_e64	v[252:253], null, v254, v255, v[250:251]
	v_mad_u64_u32	v[2:3], null, v4, v5, 5
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, 5
	v_mad_u64_u32	v[2:3], null, v4, v5, -1
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, -1
	v_mad_u64_u32	v[2:3], null, v4, v5, 64
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, 64
	v_mad_u64_u32	v[2:3], null, 0x100, v5, 0
	v_mad_u64_u32_e64	v[2:3], null, 0x100, v5, 0
	v_mad_u64_u32	v[2:3], null, v4, 0x100, 0
	v_mad_u64_u32_e64	v[2:3], null, v4, 0x100, 0
	v_mad_u64_u32	v[2:3], null, v4, v5, vcc
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, vcc
	v_mad_u64_u32	v[2:3], null, v4, v5, exec
	v_mad_u64_u32_e64	v[2:3], null, v4, v5, exec
	v_mad_u64_u32	v[58:59], null, v54, v54, 0
	v_mad_u64_u32_e64	v[58:59], null, v54, v54, 0
	v_mad_u64_u32	v[2:3], null, 5, 6, 7
	v_mad_u64_u32_e64	v[2:3], null, 5, 6, 7
	v_mad_u64_u32	v[2:3], null, v4, v5, v[6:7] clamp
	v_mad_u64_u32	v[2:3], vcc, v4, v5, 0
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, 0
	v_mad_u64_u32	v[2:3], vcc, v4, v5, v[6:7]
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, v[6:7]
	v_mad_u64_u32	v[2:3], vcc, v4, s5, 0
	v_mad_u64_u32_e64	v[2:3], vcc, v4, s5, 0
	v_mad_u64_u32	v[2:3], vcc, s4, v5, s[6:7]
	v_mad_u64_u32_e64	v[2:3], vcc, s4, v5, s[6:7]
	v_mad_u64_u32	v[252:253], vcc, v254, v255, v[250:251]
	v_mad_u64_u32_e64	v[252:253], vcc, v254, v255, v[250:251]
	v_mad_u64_u32	v[2:3], vcc, v4, v5, 5
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, 5
	v_mad_u64_u32	v[2:3], vcc, v4, v5, -1
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, -1
	v_mad_u64_u32	v[2:3], vcc, v4, v5, 64
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, 64
	v_mad_u64_u32	v[2:3], vcc, 0x100, v5, 0
	v_mad_u64_u32_e64	v[2:3], vcc, 0x100, v5, 0
	v_mad_u64_u32	v[2:3], vcc, v4, 0x100, 0
	v_mad_u64_u32_e64	v[2:3], vcc, v4, 0x100, 0
	v_mad_u64_u32	v[2:3], vcc, v4, v5, vcc
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, vcc
	v_mad_u64_u32	v[2:3], vcc, v4, v5, exec
	v_mad_u64_u32_e64	v[2:3], vcc, v4, v5, exec
	v_mad_u64_u32	v[58:59], vcc, v54, v54, 0
	v_mad_u64_u32_e64	v[58:59], vcc, v54, v54, 0
	v_mad_u64_u32	v[2:3], vcc, 5, 6, 7
	v_mad_u64_u32_e64	v[2:3], vcc, 5, 6, 7
	v_mad_u64_u32	v[2:3], vcc, v4, v5, v[6:7] clamp
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, 0
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, 0
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, v[6:7]
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, v[6:7]
	v_mad_u64_u32	v[2:3], s[4:5], v4, s5, 0
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, s5, 0
	v_mad_u64_u32	v[2:3], s[4:5], s4, v5, s[6:7]
	v_mad_u64_u32_e64	v[2:3], s[4:5], s4, v5, s[6:7]
	v_mad_u64_u32	v[252:253], s[4:5], v254, v255, v[250:251]
	v_mad_u64_u32_e64	v[252:253], s[4:5], v254, v255, v[250:251]
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, 5
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, 5
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, -1
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, -1
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, 64
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, 64
	v_mad_u64_u32	v[2:3], s[4:5], 0x100, v5, 0
	v_mad_u64_u32_e64	v[2:3], s[4:5], 0x100, v5, 0
	v_mad_u64_u32	v[2:3], s[4:5], v4, 0x100, 0
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, 0x100, 0
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, vcc
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, vcc
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, exec
	v_mad_u64_u32_e64	v[2:3], s[4:5], v4, v5, exec
	v_mad_u64_u32	v[58:59], s[4:5], v54, v54, 0
	v_mad_u64_u32_e64	v[58:59], s[4:5], v54, v54, 0
	v_mad_u64_u32	v[2:3], s[4:5], 5, 6, 7
	v_mad_u64_u32_e64	v[2:3], s[4:5], 5, 6, 7
	v_mad_u64_u32	v[2:3], s[4:5], v4, v5, v[6:7] clamp
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, 0
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, 0
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, v[6:7]
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, v[6:7]
	v_mad_u64_u32	v[2:3], s[104:105], v4, s5, 0
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, s5, 0
	v_mad_u64_u32	v[2:3], s[104:105], s4, v5, s[6:7]
	v_mad_u64_u32_e64	v[2:3], s[104:105], s4, v5, s[6:7]
	v_mad_u64_u32	v[252:253], s[104:105], v254, v255, v[250:251]
	v_mad_u64_u32_e64	v[252:253], s[104:105], v254, v255, v[250:251]
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, 5
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, 5
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, -1
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, -1
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, 64
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, 64
	v_mad_u64_u32	v[2:3], s[104:105], 0x100, v5, 0
	v_mad_u64_u32_e64	v[2:3], s[104:105], 0x100, v5, 0
	v_mad_u64_u32	v[2:3], s[104:105], v4, 0x100, 0
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, 0x100, 0
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, vcc
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, vcc
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, exec
	v_mad_u64_u32_e64	v[2:3], s[104:105], v4, v5, exec
	v_mad_u64_u32	v[58:59], s[104:105], v54, v54, 0
	v_mad_u64_u32_e64	v[58:59], s[104:105], v54, v54, 0
	v_mad_u64_u32	v[2:3], s[104:105], 5, 6, 7
	v_mad_u64_u32_e64	v[2:3], s[104:105], 5, 6, 7
	v_mad_u64_u32	v[2:3], s[104:105], v4, v5, v[6:7] clamp
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, 0
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, 0
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, v[6:7]
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, v[6:7]
	v_mad_u64_u32	v[2:3], s[0:1], v4, s5, 0
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, s5, 0
	v_mad_u64_u32	v[2:3], s[0:1], s4, v5, s[6:7]
	v_mad_u64_u32_e64	v[2:3], s[0:1], s4, v5, s[6:7]
	v_mad_u64_u32	v[252:253], s[0:1], v254, v255, v[250:251]
	v_mad_u64_u32_e64	v[252:253], s[0:1], v254, v255, v[250:251]
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, 5
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, 5
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, -1
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, -1
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, 64
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, 64
	v_mad_u64_u32	v[2:3], s[0:1], 0x100, v5, 0
	v_mad_u64_u32_e64	v[2:3], s[0:1], 0x100, v5, 0
	v_mad_u64_u32	v[2:3], s[0:1], v4, 0x100, 0
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, 0x100, 0
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, vcc
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, vcc
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, exec
	v_mad_u64_u32_e64	v[2:3], s[0:1], v4, v5, exec
	v_mad_u64_u32	v[58:59], s[0:1], v54, v54, 0
	v_mad_u64_u32_e64	v[58:59], s[0:1], v54, v54, 0
	v_mad_u64_u32	v[2:3], s[0:1], 5, 6, 7
	v_mad_u64_u32_e64	v[2:3], s[0:1], 5, 6, 7
	v_mad_u64_u32	v[2:3], s[0:1], v4, v5, v[6:7] clamp
	v_mad_u64_u32	v[2:3], exec, v4, v5, 0
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, 0
	v_mad_u64_u32	v[2:3], exec, v4, v5, v[6:7]
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, v[6:7]
	v_mad_u64_u32	v[2:3], exec, v4, s5, 0
	v_mad_u64_u32_e64	v[2:3], exec, v4, s5, 0
	v_mad_u64_u32	v[2:3], exec, s4, v5, s[6:7]
	v_mad_u64_u32_e64	v[2:3], exec, s4, v5, s[6:7]
	v_mad_u64_u32	v[252:253], exec, v254, v255, v[250:251]
	v_mad_u64_u32_e64	v[252:253], exec, v254, v255, v[250:251]
	v_mad_u64_u32	v[2:3], exec, v4, v5, 5
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, 5
	v_mad_u64_u32	v[2:3], exec, v4, v5, -1
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, -1
	v_mad_u64_u32	v[2:3], exec, v4, v5, 64
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, 64
	v_mad_u64_u32	v[2:3], exec, 0x100, v5, 0
	v_mad_u64_u32_e64	v[2:3], exec, 0x100, v5, 0
	v_mad_u64_u32	v[2:3], exec, v4, 0x100, 0
	v_mad_u64_u32_e64	v[2:3], exec, v4, 0x100, 0
	v_mad_u64_u32	v[2:3], exec, v4, v5, vcc
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, vcc
	v_mad_u64_u32	v[2:3], exec, v4, v5, exec
	v_mad_u64_u32_e64	v[2:3], exec, v4, v5, exec
	v_mad_u64_u32	v[58:59], exec, v54, v54, 0
	v_mad_u64_u32_e64	v[58:59], exec, v54, v54, 0
	v_mad_u64_u32	v[2:3], exec, 5, 6, 7
	v_mad_u64_u32_e64	v[2:3], exec, 5, 6, 7
	v_mad_u64_u32	v[2:3], exec, v4, v5, v[6:7] clamp
	s_endpgm
.size vop3b, .-vop3b

; VOP3B and VOP2 with a carry: vcc or a pair, the short encoding and the long
.globl carry
.p2align 8
.type carry,@function
carry:
	v_add_co_u32	v1, vcc, v2, v3
	v_add_co_u32_e64	v1, vcc, v2, v3
	v_add_co_u32	v1, vcc, v2, v3 clamp
	v_add_co_u32	v255, vcc, v254, v253
	v_add_co_u32_e64	v255, vcc, v254, v253
	v_add_co_u32	v255, vcc, v254, v253 clamp
	v_add_co_u32	v1, vcc, s2, v3
	v_add_co_u32_e64	v1, vcc, s2, v3
	v_add_co_u32	v1, vcc, s2, v3 clamp
	v_add_co_u32	v1, vcc, v2, s3
	v_add_co_u32_e64	v1, vcc, v2, s3
	v_add_co_u32	v1, vcc, v2, s3 clamp
	v_add_co_u32	v1, vcc, 5, v3
	v_add_co_u32_e64	v1, vcc, 5, v3
	v_add_co_u32	v1, vcc, 5, v3 clamp
	v_add_co_u32	v1, vcc, v2, -16
	v_add_co_u32_e64	v1, vcc, v2, -16
	v_add_co_u32	v1, vcc, v2, -16 clamp
	v_add_co_u32	v1, vcc, 0x1234, v3
	v_add_co_u32_e64	v1, vcc, 0x1234, v3
	v_add_co_u32	v1, vcc, 0x1234, v3 clamp
	v_add_co_u32	v1, vcc, v2, 0x1234
	v_add_co_u32_e64	v1, vcc, v2, 0x1234
	v_add_co_u32	v1, vcc, v2, 0x1234 clamp
	v_add_co_u32	v1, vcc, s2, s3
	v_add_co_u32_e64	v1, vcc, s2, s3
	v_add_co_u32	v1, vcc, s2, s3 clamp
	v_add_co_u32	v1, vcc, s2, 0x1234
	v_add_co_u32_e64	v1, vcc, s2, 0x1234
	v_add_co_u32	v1, vcc, s2, 0x1234 clamp
	v_add_co_u32	v1, vcc, 0x3f800000, v3
	v_add_co_u32_e64	v1, vcc, 0x3f800000, v3
	v_add_co_u32	v1, vcc, 0x3f800000, v3 clamp
	v_add_co_u32	v32, vcc, s61, v30
	v_add_co_u32_e64	v32, vcc, s61, v30
	v_add_co_u32	v32, vcc, s61, v30 clamp
	v_add_co_u32	v1, s[2:3], v2, v3
	v_add_co_u32_e64	v1, s[2:3], v2, v3
	v_add_co_u32	v1, s[2:3], v2, v3 clamp
	v_add_co_u32	v255, s[2:3], v254, v253
	v_add_co_u32_e64	v255, s[2:3], v254, v253
	v_add_co_u32	v255, s[2:3], v254, v253 clamp
	v_add_co_u32	v1, s[2:3], s2, v3
	v_add_co_u32_e64	v1, s[2:3], s2, v3
	v_add_co_u32	v1, s[2:3], s2, v3 clamp
	v_add_co_u32	v1, s[2:3], v2, s3
	v_add_co_u32_e64	v1, s[2:3], v2, s3
	v_add_co_u32	v1, s[2:3], v2, s3 clamp
	v_add_co_u32	v1, s[2:3], 5, v3
	v_add_co_u32_e64	v1, s[2:3], 5, v3
	v_add_co_u32	v1, s[2:3], 5, v3 clamp
	v_add_co_u32	v1, s[2:3], v2, -16
	v_add_co_u32_e64	v1, s[2:3], v2, -16
	v_add_co_u32	v1, s[2:3], v2, -16 clamp
	v_add_co_u32	v1, s[2:3], 0x1234, v3
	v_add_co_u32_e64	v1, s[2:3], 0x1234, v3
	v_add_co_u32	v1, s[2:3], 0x1234, v3 clamp
	v_add_co_u32	v1, s[2:3], v2, 0x1234
	v_add_co_u32_e64	v1, s[2:3], v2, 0x1234
	v_add_co_u32	v1, s[2:3], v2, 0x1234 clamp
	v_add_co_u32	v1, s[2:3], s2, s3
	v_add_co_u32_e64	v1, s[2:3], s2, s3
	v_add_co_u32	v1, s[2:3], s2, s3 clamp
	v_add_co_u32	v1, s[2:3], s2, 0x1234
	v_add_co_u32_e64	v1, s[2:3], s2, 0x1234
	v_add_co_u32	v1, s[2:3], s2, 0x1234 clamp
	v_add_co_u32	v1, s[2:3], 0x3f800000, v3
	v_add_co_u32_e64	v1, s[2:3], 0x3f800000, v3
	v_add_co_u32	v1, s[2:3], 0x3f800000, v3 clamp
	v_add_co_u32	v32, s[2:3], s61, v30
	v_add_co_u32_e64	v32, s[2:3], s61, v30
	v_add_co_u32	v32, s[2:3], s61, v30 clamp
	v_add_co_u32	v1, s[104:105], v2, v3
	v_add_co_u32_e64	v1, s[104:105], v2, v3
	v_add_co_u32	v1, s[104:105], v2, v3 clamp
	v_add_co_u32	v255, s[104:105], v254, v253
	v_add_co_u32_e64	v255, s[104:105], v254, v253
	v_add_co_u32	v255, s[104:105], v254, v253 clamp
	v_add_co_u32	v1, s[104:105], s2, v3
	v_add_co_u32_e64	v1, s[104:105], s2, v3
	v_add_co_u32	v1, s[104:105], s2, v3 clamp
	v_add_co_u32	v1, s[104:105], v2, s3
	v_add_co_u32_e64	v1, s[104:105], v2, s3
	v_add_co_u32	v1, s[104:105], v2, s3 clamp
	v_add_co_u32	v1, s[104:105], 5, v3
	v_add_co_u32_e64	v1, s[104:105], 5, v3
	v_add_co_u32	v1, s[104:105], 5, v3 clamp
	v_add_co_u32	v1, s[104:105], v2, -16
	v_add_co_u32_e64	v1, s[104:105], v2, -16
	v_add_co_u32	v1, s[104:105], v2, -16 clamp
	v_add_co_u32	v1, s[104:105], 0x1234, v3
	v_add_co_u32_e64	v1, s[104:105], 0x1234, v3
	v_add_co_u32	v1, s[104:105], 0x1234, v3 clamp
	v_add_co_u32	v1, s[104:105], v2, 0x1234
	v_add_co_u32_e64	v1, s[104:105], v2, 0x1234
	v_add_co_u32	v1, s[104:105], v2, 0x1234 clamp
	v_add_co_u32	v1, s[104:105], s2, s3
	v_add_co_u32_e64	v1, s[104:105], s2, s3
	v_add_co_u32	v1, s[104:105], s2, s3 clamp
	v_add_co_u32	v1, s[104:105], s2, 0x1234
	v_add_co_u32_e64	v1, s[104:105], s2, 0x1234
	v_add_co_u32	v1, s[104:105], s2, 0x1234 clamp
	v_add_co_u32	v1, s[104:105], 0x3f800000, v3
	v_add_co_u32_e64	v1, s[104:105], 0x3f800000, v3
	v_add_co_u32	v1, s[104:105], 0x3f800000, v3 clamp
	v_add_co_u32	v32, s[104:105], s61, v30
	v_add_co_u32_e64	v32, s[104:105], s61, v30
	v_add_co_u32	v32, s[104:105], s61, v30 clamp
	v_add_co_u32	v1, exec, v2, v3
	v_add_co_u32_e64	v1, exec, v2, v3
	v_add_co_u32	v1, exec, v2, v3 clamp
	v_add_co_u32	v255, exec, v254, v253
	v_add_co_u32_e64	v255, exec, v254, v253
	v_add_co_u32	v255, exec, v254, v253 clamp
	v_add_co_u32	v1, exec, s2, v3
	v_add_co_u32_e64	v1, exec, s2, v3
	v_add_co_u32	v1, exec, s2, v3 clamp
	v_add_co_u32	v1, exec, v2, s3
	v_add_co_u32_e64	v1, exec, v2, s3
	v_add_co_u32	v1, exec, v2, s3 clamp
	v_add_co_u32	v1, exec, 5, v3
	v_add_co_u32_e64	v1, exec, 5, v3
	v_add_co_u32	v1, exec, 5, v3 clamp
	v_add_co_u32	v1, exec, v2, -16
	v_add_co_u32_e64	v1, exec, v2, -16
	v_add_co_u32	v1, exec, v2, -16 clamp
	v_add_co_u32	v1, exec, 0x1234, v3
	v_add_co_u32_e64	v1, exec, 0x1234, v3
	v_add_co_u32	v1, exec, 0x1234, v3 clamp
	v_add_co_u32	v1, exec, v2, 0x1234
	v_add_co_u32_e64	v1, exec, v2, 0x1234
	v_add_co_u32	v1, exec, v2, 0x1234 clamp
	v_add_co_u32	v1, exec, s2, s3
	v_add_co_u32_e64	v1, exec, s2, s3
	v_add_co_u32	v1, exec, s2, s3 clamp
	v_add_co_u32	v1, exec, s2, 0x1234
	v_add_co_u32_e64	v1, exec, s2, 0x1234
	v_add_co_u32	v1, exec, s2, 0x1234 clamp
	v_add_co_u32	v1, exec, 0x3f800000, v3
	v_add_co_u32_e64	v1, exec, 0x3f800000, v3
	v_add_co_u32	v1, exec, 0x3f800000, v3 clamp
	v_add_co_u32	v32, exec, s61, v30
	v_add_co_u32_e64	v32, exec, s61, v30
	v_add_co_u32	v32, exec, s61, v30 clamp
	v_add_co_u32	v1, null, v2, v3
	v_add_co_u32_e64	v1, null, v2, v3
	v_add_co_u32	v1, null, v2, v3 clamp
	v_add_co_u32	v255, null, v254, v253
	v_add_co_u32_e64	v255, null, v254, v253
	v_add_co_u32	v255, null, v254, v253 clamp
	v_add_co_u32	v1, null, s2, v3
	v_add_co_u32_e64	v1, null, s2, v3
	v_add_co_u32	v1, null, s2, v3 clamp
	v_add_co_u32	v1, null, v2, s3
	v_add_co_u32_e64	v1, null, v2, s3
	v_add_co_u32	v1, null, v2, s3 clamp
	v_add_co_u32	v1, null, 5, v3
	v_add_co_u32_e64	v1, null, 5, v3
	v_add_co_u32	v1, null, 5, v3 clamp
	v_add_co_u32	v1, null, v2, -16
	v_add_co_u32_e64	v1, null, v2, -16
	v_add_co_u32	v1, null, v2, -16 clamp
	v_add_co_u32	v1, null, 0x1234, v3
	v_add_co_u32_e64	v1, null, 0x1234, v3
	v_add_co_u32	v1, null, 0x1234, v3 clamp
	v_add_co_u32	v1, null, v2, 0x1234
	v_add_co_u32_e64	v1, null, v2, 0x1234
	v_add_co_u32	v1, null, v2, 0x1234 clamp
	v_add_co_u32	v1, null, s2, s3
	v_add_co_u32_e64	v1, null, s2, s3
	v_add_co_u32	v1, null, s2, s3 clamp
	v_add_co_u32	v1, null, s2, 0x1234
	v_add_co_u32_e64	v1, null, s2, 0x1234
	v_add_co_u32	v1, null, s2, 0x1234 clamp
	v_add_co_u32	v1, null, 0x3f800000, v3
	v_add_co_u32_e64	v1, null, 0x3f800000, v3
	v_add_co_u32	v1, null, 0x3f800000, v3 clamp
	v_add_co_u32	v32, null, s61, v30
	v_add_co_u32_e64	v32, null, s61, v30
	v_add_co_u32	v32, null, s61, v30 clamp
	v_add_co_ci_u32	v1, vcc, v2, v3, vcc
	v_add_co_ci_u32_e64	v1, vcc, v2, v3, vcc
	v_add_co_ci_u32	v255, vcc, v254, v253, vcc
	v_add_co_ci_u32_e64	v255, vcc, v254, v253, vcc
	v_add_co_ci_u32	v1, vcc, s2, v3, vcc
	v_add_co_ci_u32_e64	v1, vcc, s2, v3, vcc
	v_add_co_ci_u32	v1, vcc, v2, s3, vcc
	v_add_co_ci_u32_e64	v1, vcc, v2, s3, vcc
	v_add_co_ci_u32	v1, vcc, 5, v3, vcc
	v_add_co_ci_u32_e64	v1, vcc, 5, v3, vcc
	v_add_co_ci_u32	v1, vcc, -1, v3, vcc
	v_add_co_ci_u32_e64	v1, vcc, -1, v3, vcc
	v_add_co_ci_u32	v1, vcc, 0x1234, v3, vcc
	v_add_co_ci_u32_e64	v1, vcc, 0x1234, v3, vcc
	v_add_co_ci_u32	v1, vcc, v2, 0x1234, vcc
	v_add_co_ci_u32_e64	v1, vcc, v2, 0x1234, vcc
	v_add_co_ci_u32	v1, vcc, 0, 1, vcc
	v_add_co_ci_u32_e64	v1, vcc, 0, 1, vcc
	v_add_co_ci_u32	v1, vcc, v2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, v2, v3, s[4:5]
	v_add_co_ci_u32	v255, vcc, v254, v253, s[4:5]
	v_add_co_ci_u32_e64	v255, vcc, v254, v253, s[4:5]
	v_add_co_ci_u32	v1, vcc, s2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, s2, v3, s[4:5]
	v_add_co_ci_u32	v1, vcc, v2, s3, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, v2, s3, s[4:5]
	v_add_co_ci_u32	v1, vcc, 5, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, 5, v3, s[4:5]
	v_add_co_ci_u32	v1, vcc, -1, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, -1, v3, s[4:5]
	v_add_co_ci_u32	v1, vcc, 0x1234, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, 0x1234, v3, s[4:5]
	v_add_co_ci_u32	v1, vcc, v2, 0x1234, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, v2, 0x1234, s[4:5]
	v_add_co_ci_u32	v1, vcc, 0, 1, s[4:5]
	v_add_co_ci_u32_e64	v1, vcc, 0, 1, s[4:5]
	v_add_co_ci_u32	v1, vcc, v2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, v2, v3, s[104:105]
	v_add_co_ci_u32	v255, vcc, v254, v253, s[104:105]
	v_add_co_ci_u32_e64	v255, vcc, v254, v253, s[104:105]
	v_add_co_ci_u32	v1, vcc, s2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, s2, v3, s[104:105]
	v_add_co_ci_u32	v1, vcc, v2, s3, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, v2, s3, s[104:105]
	v_add_co_ci_u32	v1, vcc, 5, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, 5, v3, s[104:105]
	v_add_co_ci_u32	v1, vcc, -1, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, -1, v3, s[104:105]
	v_add_co_ci_u32	v1, vcc, 0x1234, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, 0x1234, v3, s[104:105]
	v_add_co_ci_u32	v1, vcc, v2, 0x1234, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, v2, 0x1234, s[104:105]
	v_add_co_ci_u32	v1, vcc, 0, 1, s[104:105]
	v_add_co_ci_u32_e64	v1, vcc, 0, 1, s[104:105]
	v_add_co_ci_u32	v1, vcc, v2, v3, exec
	v_add_co_ci_u32_e64	v1, vcc, v2, v3, exec
	v_add_co_ci_u32	v255, vcc, v254, v253, exec
	v_add_co_ci_u32_e64	v255, vcc, v254, v253, exec
	v_add_co_ci_u32	v1, vcc, s2, v3, exec
	v_add_co_ci_u32_e64	v1, vcc, s2, v3, exec
	v_add_co_ci_u32	v1, vcc, v2, s3, exec
	v_add_co_ci_u32_e64	v1, vcc, v2, s3, exec
	v_add_co_ci_u32	v1, vcc, 5, v3, exec
	v_add_co_ci_u32_e64	v1, vcc, 5, v3, exec
	v_add_co_ci_u32	v1, vcc, -1, v3, exec
	v_add_co_ci_u32_e64	v1, vcc, -1, v3, exec
	v_add_co_ci_u32	v1, vcc, 0x1234, v3, exec
	v_add_co_ci_u32_e64	v1, vcc, 0x1234, v3, exec
	v_add_co_ci_u32	v1, vcc, v2, 0x1234, exec
	v_add_co_ci_u32_e64	v1, vcc, v2, 0x1234, exec
	v_add_co_ci_u32	v1, vcc, 0, 1, exec
	v_add_co_ci_u32_e64	v1, vcc, 0, 1, exec
	v_add_co_ci_u32	v1, s[2:3], v2, v3, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], v2, v3, vcc
	v_add_co_ci_u32	v255, s[2:3], v254, v253, vcc
	v_add_co_ci_u32_e64	v255, s[2:3], v254, v253, vcc
	v_add_co_ci_u32	v1, s[2:3], s2, v3, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], s2, v3, vcc
	v_add_co_ci_u32	v1, s[2:3], v2, s3, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], v2, s3, vcc
	v_add_co_ci_u32	v1, s[2:3], 5, v3, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], 5, v3, vcc
	v_add_co_ci_u32	v1, s[2:3], -1, v3, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], -1, v3, vcc
	v_add_co_ci_u32	v1, s[2:3], 0x1234, v3, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], 0x1234, v3, vcc
	v_add_co_ci_u32	v1, s[2:3], v2, 0x1234, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], v2, 0x1234, vcc
	v_add_co_ci_u32	v1, s[2:3], 0, 1, vcc
	v_add_co_ci_u32_e64	v1, s[2:3], 0, 1, vcc
	v_add_co_ci_u32	v1, s[2:3], v2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], v2, v3, s[4:5]
	v_add_co_ci_u32	v255, s[2:3], v254, v253, s[4:5]
	v_add_co_ci_u32_e64	v255, s[2:3], v254, v253, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], s2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], s2, v3, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], v2, s3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], v2, s3, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], 5, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], 5, v3, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], -1, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], -1, v3, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], 0x1234, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], 0x1234, v3, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], v2, 0x1234, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], v2, 0x1234, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], 0, 1, s[4:5]
	v_add_co_ci_u32_e64	v1, s[2:3], 0, 1, s[4:5]
	v_add_co_ci_u32	v1, s[2:3], v2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], v2, v3, s[104:105]
	v_add_co_ci_u32	v255, s[2:3], v254, v253, s[104:105]
	v_add_co_ci_u32_e64	v255, s[2:3], v254, v253, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], s2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], s2, v3, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], v2, s3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], v2, s3, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], 5, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], 5, v3, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], -1, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], -1, v3, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], 0x1234, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], 0x1234, v3, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], v2, 0x1234, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], v2, 0x1234, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], 0, 1, s[104:105]
	v_add_co_ci_u32_e64	v1, s[2:3], 0, 1, s[104:105]
	v_add_co_ci_u32	v1, s[2:3], v2, v3, exec
	v_add_co_ci_u32_e64	v1, s[2:3], v2, v3, exec
	v_add_co_ci_u32	v255, s[2:3], v254, v253, exec
	v_add_co_ci_u32_e64	v255, s[2:3], v254, v253, exec
	v_add_co_ci_u32	v1, s[2:3], s2, v3, exec
	v_add_co_ci_u32_e64	v1, s[2:3], s2, v3, exec
	v_add_co_ci_u32	v1, s[2:3], v2, s3, exec
	v_add_co_ci_u32_e64	v1, s[2:3], v2, s3, exec
	v_add_co_ci_u32	v1, s[2:3], 5, v3, exec
	v_add_co_ci_u32_e64	v1, s[2:3], 5, v3, exec
	v_add_co_ci_u32	v1, s[2:3], -1, v3, exec
	v_add_co_ci_u32_e64	v1, s[2:3], -1, v3, exec
	v_add_co_ci_u32	v1, s[2:3], 0x1234, v3, exec
	v_add_co_ci_u32_e64	v1, s[2:3], 0x1234, v3, exec
	v_add_co_ci_u32	v1, s[2:3], v2, 0x1234, exec
	v_add_co_ci_u32_e64	v1, s[2:3], v2, 0x1234, exec
	v_add_co_ci_u32	v1, s[2:3], 0, 1, exec
	v_add_co_ci_u32_e64	v1, s[2:3], 0, 1, exec
	v_add_co_ci_u32	v1, s[104:105], v2, v3, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], v2, v3, vcc
	v_add_co_ci_u32	v255, s[104:105], v254, v253, vcc
	v_add_co_ci_u32_e64	v255, s[104:105], v254, v253, vcc
	v_add_co_ci_u32	v1, s[104:105], s2, v3, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], s2, v3, vcc
	v_add_co_ci_u32	v1, s[104:105], v2, s3, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], v2, s3, vcc
	v_add_co_ci_u32	v1, s[104:105], 5, v3, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], 5, v3, vcc
	v_add_co_ci_u32	v1, s[104:105], -1, v3, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], -1, v3, vcc
	v_add_co_ci_u32	v1, s[104:105], 0x1234, v3, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], 0x1234, v3, vcc
	v_add_co_ci_u32	v1, s[104:105], v2, 0x1234, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], v2, 0x1234, vcc
	v_add_co_ci_u32	v1, s[104:105], 0, 1, vcc
	v_add_co_ci_u32_e64	v1, s[104:105], 0, 1, vcc
	v_add_co_ci_u32	v1, s[104:105], v2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], v2, v3, s[4:5]
	v_add_co_ci_u32	v255, s[104:105], v254, v253, s[4:5]
	v_add_co_ci_u32_e64	v255, s[104:105], v254, v253, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], s2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], s2, v3, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], v2, s3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], v2, s3, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], 5, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], 5, v3, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], -1, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], -1, v3, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], 0x1234, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], 0x1234, v3, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], v2, 0x1234, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], v2, 0x1234, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], 0, 1, s[4:5]
	v_add_co_ci_u32_e64	v1, s[104:105], 0, 1, s[4:5]
	v_add_co_ci_u32	v1, s[104:105], v2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], v2, v3, s[104:105]
	v_add_co_ci_u32	v255, s[104:105], v254, v253, s[104:105]
	v_add_co_ci_u32_e64	v255, s[104:105], v254, v253, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], s2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], s2, v3, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], v2, s3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], v2, s3, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], 5, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], 5, v3, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], -1, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], -1, v3, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], 0x1234, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], 0x1234, v3, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], v2, 0x1234, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], v2, 0x1234, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], 0, 1, s[104:105]
	v_add_co_ci_u32_e64	v1, s[104:105], 0, 1, s[104:105]
	v_add_co_ci_u32	v1, s[104:105], v2, v3, exec
	v_add_co_ci_u32_e64	v1, s[104:105], v2, v3, exec
	v_add_co_ci_u32	v255, s[104:105], v254, v253, exec
	v_add_co_ci_u32_e64	v255, s[104:105], v254, v253, exec
	v_add_co_ci_u32	v1, s[104:105], s2, v3, exec
	v_add_co_ci_u32_e64	v1, s[104:105], s2, v3, exec
	v_add_co_ci_u32	v1, s[104:105], v2, s3, exec
	v_add_co_ci_u32_e64	v1, s[104:105], v2, s3, exec
	v_add_co_ci_u32	v1, s[104:105], 5, v3, exec
	v_add_co_ci_u32_e64	v1, s[104:105], 5, v3, exec
	v_add_co_ci_u32	v1, s[104:105], -1, v3, exec
	v_add_co_ci_u32_e64	v1, s[104:105], -1, v3, exec
	v_add_co_ci_u32	v1, s[104:105], 0x1234, v3, exec
	v_add_co_ci_u32_e64	v1, s[104:105], 0x1234, v3, exec
	v_add_co_ci_u32	v1, s[104:105], v2, 0x1234, exec
	v_add_co_ci_u32_e64	v1, s[104:105], v2, 0x1234, exec
	v_add_co_ci_u32	v1, s[104:105], 0, 1, exec
	v_add_co_ci_u32_e64	v1, s[104:105], 0, 1, exec
	v_add_co_ci_u32	v1, exec, v2, v3, vcc
	v_add_co_ci_u32_e64	v1, exec, v2, v3, vcc
	v_add_co_ci_u32	v255, exec, v254, v253, vcc
	v_add_co_ci_u32_e64	v255, exec, v254, v253, vcc
	v_add_co_ci_u32	v1, exec, s2, v3, vcc
	v_add_co_ci_u32_e64	v1, exec, s2, v3, vcc
	v_add_co_ci_u32	v1, exec, v2, s3, vcc
	v_add_co_ci_u32_e64	v1, exec, v2, s3, vcc
	v_add_co_ci_u32	v1, exec, 5, v3, vcc
	v_add_co_ci_u32_e64	v1, exec, 5, v3, vcc
	v_add_co_ci_u32	v1, exec, -1, v3, vcc
	v_add_co_ci_u32_e64	v1, exec, -1, v3, vcc
	v_add_co_ci_u32	v1, exec, 0x1234, v3, vcc
	v_add_co_ci_u32_e64	v1, exec, 0x1234, v3, vcc
	v_add_co_ci_u32	v1, exec, v2, 0x1234, vcc
	v_add_co_ci_u32_e64	v1, exec, v2, 0x1234, vcc
	v_add_co_ci_u32	v1, exec, 0, 1, vcc
	v_add_co_ci_u32_e64	v1, exec, 0, 1, vcc
	v_add_co_ci_u32	v1, exec, v2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, v2, v3, s[4:5]
	v_add_co_ci_u32	v255, exec, v254, v253, s[4:5]
	v_add_co_ci_u32_e64	v255, exec, v254, v253, s[4:5]
	v_add_co_ci_u32	v1, exec, s2, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, s2, v3, s[4:5]
	v_add_co_ci_u32	v1, exec, v2, s3, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, v2, s3, s[4:5]
	v_add_co_ci_u32	v1, exec, 5, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, 5, v3, s[4:5]
	v_add_co_ci_u32	v1, exec, -1, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, -1, v3, s[4:5]
	v_add_co_ci_u32	v1, exec, 0x1234, v3, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, 0x1234, v3, s[4:5]
	v_add_co_ci_u32	v1, exec, v2, 0x1234, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, v2, 0x1234, s[4:5]
	v_add_co_ci_u32	v1, exec, 0, 1, s[4:5]
	v_add_co_ci_u32_e64	v1, exec, 0, 1, s[4:5]
	v_add_co_ci_u32	v1, exec, v2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, v2, v3, s[104:105]
	v_add_co_ci_u32	v255, exec, v254, v253, s[104:105]
	v_add_co_ci_u32_e64	v255, exec, v254, v253, s[104:105]
	v_add_co_ci_u32	v1, exec, s2, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, s2, v3, s[104:105]
	v_add_co_ci_u32	v1, exec, v2, s3, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, v2, s3, s[104:105]
	v_add_co_ci_u32	v1, exec, 5, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, 5, v3, s[104:105]
	v_add_co_ci_u32	v1, exec, -1, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, -1, v3, s[104:105]
	v_add_co_ci_u32	v1, exec, 0x1234, v3, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, 0x1234, v3, s[104:105]
	v_add_co_ci_u32	v1, exec, v2, 0x1234, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, v2, 0x1234, s[104:105]
	v_add_co_ci_u32	v1, exec, 0, 1, s[104:105]
	v_add_co_ci_u32_e64	v1, exec, 0, 1, s[104:105]
	v_add_co_ci_u32	v1, exec, v2, v3, exec
	v_add_co_ci_u32_e64	v1, exec, v2, v3, exec
	v_add_co_ci_u32	v255, exec, v254, v253, exec
	v_add_co_ci_u32_e64	v255, exec, v254, v253, exec
	v_add_co_ci_u32	v1, exec, s2, v3, exec
	v_add_co_ci_u32_e64	v1, exec, s2, v3, exec
	v_add_co_ci_u32	v1, exec, v2, s3, exec
	v_add_co_ci_u32_e64	v1, exec, v2, s3, exec
	v_add_co_ci_u32	v1, exec, 5, v3, exec
	v_add_co_ci_u32_e64	v1, exec, 5, v3, exec
	v_add_co_ci_u32	v1, exec, -1, v3, exec
	v_add_co_ci_u32_e64	v1, exec, -1, v3, exec
	v_add_co_ci_u32	v1, exec, 0x1234, v3, exec
	v_add_co_ci_u32_e64	v1, exec, 0x1234, v3, exec
	v_add_co_ci_u32	v1, exec, v2, 0x1234, exec
	v_add_co_ci_u32_e64	v1, exec, v2, 0x1234, exec
	v_add_co_ci_u32	v1, exec, 0, 1, exec
	v_add_co_ci_u32_e64	v1, exec, 0, 1, exec
	v_add_co_ci_u32_e32	v33, vcc, v33, v31, vcc
	v_add_co_ci_u32_e32	v1, vcc, v2, v3, vcc
	v_add_co_ci_u32_e32	v1, vcc, s2, v3, vcc
	v_add_co_ci_u32_e32	v1, vcc, 5, v3, vcc
	v_add_co_ci_u32_e32	v1, vcc, 0x1234, v3, vcc
	v_add_co_ci_u32_e32	v255, vcc, v255, v255, vcc
	s_endpgm
.size carry, .-carry

; VOP3P: packed 16-bit forms, clamp
.globl vop3p
.p2align 8
.type vop3p,@function
vop3p:
	v_pk_min_u16	v1, v2, v3
	v_pk_min_u16	v1, v2, v3 clamp
	v_pk_min_u16	v255, v254, v253
	v_pk_min_u16	v255, v254, v253 clamp
	v_pk_min_u16	v1, s2, v3
	v_pk_min_u16	v1, s2, v3 clamp
	v_pk_min_u16	v1, v2, s3
	v_pk_min_u16	v1, v2, s3 clamp
	v_pk_min_u16	v1, s2, s2
	v_pk_min_u16	v1, s2, s2 clamp
	v_pk_min_u16	v1, s105, s105
	v_pk_min_u16	v1, s105, s105 clamp
	v_pk_min_u16	v1, 5, v3
	v_pk_min_u16	v1, 5, v3 clamp
	v_pk_min_u16	v1, v2, -16
	v_pk_min_u16	v1, v2, -16 clamp
	v_pk_min_u16	v1, 64, 0
	v_pk_min_u16	v1, 64, 0 clamp
	v_pk_min_u16	v8, v8, s22
	v_pk_min_u16	v8, v8, v7 clamp
	v_pk_sub_u16	v1, v2, v3
	v_pk_sub_u16	v1, v2, v3 clamp
	v_pk_sub_u16	v255, v254, v253
	v_pk_sub_u16	v255, v254, v253 clamp
	v_pk_sub_u16	v1, s2, v3
	v_pk_sub_u16	v1, s2, v3 clamp
	v_pk_sub_u16	v1, v2, s3
	v_pk_sub_u16	v1, v2, s3 clamp
	v_pk_sub_u16	v1, s2, s2
	v_pk_sub_u16	v1, s2, s2 clamp
	v_pk_sub_u16	v1, s105, s105
	v_pk_sub_u16	v1, s105, s105 clamp
	v_pk_sub_u16	v1, 5, v3
	v_pk_sub_u16	v1, 5, v3 clamp
	v_pk_sub_u16	v1, v2, -16
	v_pk_sub_u16	v1, v2, -16 clamp
	v_pk_sub_u16	v1, 64, 0
	v_pk_sub_u16	v1, 64, 0 clamp
	v_pk_sub_u16	v8, v8, s22
	v_pk_sub_u16	v8, v8, v7 clamp
	s_endpgm
.size vop3p, .-vop3p

; DS and global memory
.globl memory
.p2align 8
.type memory,@function
memory:
	ds_bpermute_b32	v1, v2, v3
	ds_bpermute_b32	v255, v254, v253
	ds_bpermute_b32	v0, v0, v0
	ds_bpermute_b32	v45, v44, v61
	global_load_dword	v1, v2, s[0:1]
	global_store_dword	v2, v1, s[0:1]
	global_load_dword	v255, v255, s[0:1]
	global_store_dword	v255, v255, s[0:1]
	global_load_dword	v0, v0, s[0:1]
	global_store_dword	v0, v0, s[0:1]
	global_load_dword	v4, v10, s[0:1]
	global_store_dword	v10, v4, s[0:1]
	global_load_dwordx4	v[4:7], v2, s[0:1]
	global_store_dwordx4	v2, v[4:7], s[0:1]
	global_load_dwordx4	v[252:255], v255, s[0:1]
	global_store_dwordx4	v255, v[252:255], s[0:1]
	global_load_dwordx4	v[0:3], v0, s[0:1]
	global_store_dwordx4	v0, v[0:3], s[0:1]
	global_load_dwordx4	v[5:8], v10, s[0:1]
	global_store_dwordx4	v10, v[5:8], s[0:1]
	global_load_dword	v1, v2, s[2:3]
	global_store_dword	v2, v1, s[2:3]
	global_load_dword	v255, v255, s[2:3]
	global_store_dword	v255, v255, s[2:3]
	global_load_dword	v0, v0, s[2:3]
	global_store_dword	v0, v0, s[2:3]
	global_load_dword	v4, v10, s[2:3]
	global_store_dword	v10, v4, s[2:3]
	global_load_dwordx4	v[4:7], v2, s[2:3]
	global_store_dwordx4	v2, v[4:7], s[2:3]
	global_load_dwordx4	v[252:255], v255, s[2:3]
	global_store_dwordx4	v255, v[252:255], s[2:3]
	global_load_dwordx4	v[0:3], v0, s[2:3]
	global_store_dwordx4	v0, v[0:3], s[2:3]
	global_load_dwordx4	v[5:8], v10, s[2:3]
	global_store_dwordx4	v10, v[5:8], s[2:3]
	global_load_dword	v1, v2, s[8:9]
	global_store_dword	v2, v1, s[8:9]
	global_load_dword	v255, v255, s[8:9]
	global_store_dword	v255, v255, s[8:9]
	global_load_dword	v0, v0, s[8:9]
	global_store_dword	v0, v0, s[8:9]
	global_load_dword	v4, v10, s[8:9]
	global_store_dword	v10, v4, s[8:9]
	global_load_dwordx4	v[4:7], v2, s[8:9]
	global_store_dwordx4	v2, v[4:7], s[8:9]
	global_load_dwordx4	v[252:255], v255, s[8:9]
	global_store_dwordx4	v255, v[252:255], s[8:9]
	global_load_dwordx4	v[0:3], v0, s[8:9]
	global_store_dwordx4	v0, v[0:3], s[8:9]
	global_load_dwordx4	v[5:8], v10, s[8:9]
	global_store_dwordx4	v10, v[5:8], s[8:9]
	global_load_dword	v1, v2, s[104:105]
	global_store_dword	v2, v1, s[104:105]
	global_load_dword	v255, v255, s[104:105]
	global_store_dword	v255, v255, s[104:105]
	global_load_dword	v0, v0, s[104:105]
	global_store_dword	v0, v0, s[104:105]
	global_load_dword	v4, v10, s[104:105]
	global_store_dword	v10, v4, s[104:105]
	global_load_dwordx4	v[4:7], v2, s[104:105]
	global_store_dwordx4	v2, v[4:7], s[104:105]
	global_load_dwordx4	v[252:255], v255, s[104:105]
	global_store_dwordx4	v255, v[252:255], s[104:105]
	global_load_dwordx4	v[0:3], v0, s[104:105]
	global_store_dwordx4	v0, v[0:3], s[104:105]
	global_load_dwordx4	v[5:8], v10, s[104:105]
	global_store_dwordx4	v10, v[5:8], s[104:105]
	global_load_dword	v1, v[2:3], off
	global_store_dword	v[2:3], v1, off
	global_load_dword	v1, v[2:3], off offset:12
	global_store_dword	v[2:3], v1, off offset:-12
	global_load_dword	v255, v[254:255], off
	global_store_dword	v[254:255], v255, off
	global_load_dword	v255, v[254:255], off offset:12
	global_store_dword	v[254:255], v255, off offset:-12
	global_load_dword	v0, v[0:1], off
	global_store_dword	v[0:1], v0, off
	global_load_dword	v0, v[0:1], off offset:12
	global_store_dword	v[0:1], v0, off offset:-12
	global_load_dword	v34, v[32:33], off
	global_store_dword	v[32:33], v34, off
	global_load_dword	v34, v[32:33], off offset:12
	global_store_dword	v[32:33], v34, off offset:-12
	global_load_dwordx4	v[4:7], v[2:3], off
	global_store_dwordx4	v[2:3], v[4:7], off
	global_load_dwordx4	v[4:7], v[2:3], off offset:2047
	global_store_dwordx4	v[2:3], v[4:7], off offset:-2048
	global_load_dwordx4	v[252:255], v[254:255], off
	global_store_dwordx4	v[254:255], v[252:255], off
	global_load_dwordx4	v[252:255], v[254:255], off offset:2047
	global_store_dwordx4	v[254:255], v[252:255], off offset:-2048
	global_load_dwordx4	v[0:3], v[0:1], off
	global_store_dwordx4	v[0:1], v[0:3], off
	global_load_dwordx4	v[0:3], v[0:1], off offset:2047
	global_store_dwordx4	v[0:1], v[0:3], off offset:-2048
	global_load_dword	v35, v30, s[0:1] offset:0
	global_store_dword	v30, v35, s[0:1] offset:0
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:0
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:0
	global_load_dword	v35, v30, s[0:1] offset:3
	global_store_dword	v30, v35, s[0:1] offset:3
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:3
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:3
	global_load_dword	v35, v30, s[0:1] offset:-1
	global_store_dword	v30, v35, s[0:1] offset:-1
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:-1
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:-1
	global_load_dword	v35, v30, s[0:1] offset:12
	global_store_dword	v30, v35, s[0:1] offset:12
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:12
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:12
	global_load_dword	v35, v30, s[0:1] offset:0x10
	global_store_dword	v30, v35, s[0:1] offset:0x10
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:0x10
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:0x10
	global_load_dword	v35, v30, s[0:1] offset:2047
	global_store_dword	v30, v35, s[0:1] offset:2047
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:2047
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:2047
	global_load_dword	v35, v30, s[0:1] offset:-2048
	global_store_dword	v30, v35, s[0:1] offset:-2048
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:-2048
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:-2048
	global_load_dword	v35, v30, s[0:1] offset:-8
	global_store_dword	v30, v35, s[0:1] offset:-8
	global_load_dwordx4	v[4:7], v30, s[0:1] offset:-8
	global_store_dwordx4	v30, v[4:7], s[0:1] offset:-8
	global_load_dword	v35, v30, s[2:3] offset:0
	global_store_dword	v30, v35, s[2:3] offset:0
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:0
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:0
	global_load_dword	v35, v30, s[2:3] offset:3
	global_store_dword	v30, v35, s[2:3] offset:3
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:3
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:3
	global_load_dword	v35, v30, s[2:3] offset:-1
	global_store_dword	v30, v35, s[2:3] offset:-1
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:-1
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:-1
	global_load_dword	v35, v30, s[2:3] offset:12
	global_store_dword	v30, v35, s[2:3] offset:12
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:12
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:12
	global_load_dword	v35, v30, s[2:3] offset:0x10
	global_store_dword	v30, v35, s[2:3] offset:0x10
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:0x10
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:0x10
	global_load_dword	v35, v30, s[2:3] offset:2047
	global_store_dword	v30, v35, s[2:3] offset:2047
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:2047
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:2047
	global_load_dword	v35, v30, s[2:3] offset:-2048
	global_store_dword	v30, v35, s[2:3] offset:-2048
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:-2048
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:-2048
	global_load_dword	v35, v30, s[2:3] offset:-8
	global_store_dword	v30, v35, s[2:3] offset:-8
	global_load_dwordx4	v[4:7], v30, s[2:3] offset:-8
	global_store_dwordx4	v30, v[4:7], s[2:3] offset:-8
	global_load_dword	v35, v30, s[8:9] offset:0
	global_store_dword	v30, v35, s[8:9] offset:0
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:0
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:0
	global_load_dword	v35, v30, s[8:9] offset:3
	global_store_dword	v30, v35, s[8:9] offset:3
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:3
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:3
	global_load_dword	v35, v30, s[8:9] offset:-1
	global_store_dword	v30, v35, s[8:9] offset:-1
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:-1
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:-1
	global_load_dword	v35, v30, s[8:9] offset:12
	global_store_dword	v30, v35, s[8:9] offset:12
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:12
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:12
	global_load_dword	v35, v30, s[8:9] offset:0x10
	global_store_dword	v30, v35, s[8:9] offset:0x10
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:0x10
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:0x10
	global_load_dword	v35, v30, s[8:9] offset:2047
	global_store_dword	v30, v35, s[8:9] offset:2047
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:2047
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:2047
	global_load_dword	v35, v30, s[8:9] offset:-2048
	global_store_dword	v30, v35, s[8:9] offset:-2048
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:-2048
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:-2048
	global_load_dword	v35, v30, s[8:9] offset:-8
	global_store_dword	v30, v35, s[8:9] offset:-8
	global_load_dwordx4	v[4:7], v30, s[8:9] offset:-8
	global_store_dwordx4	v30, v[4:7], s[8:9] offset:-8
	global_load_dword	v35, v30, s[104:105] offset:0
	global_store_dword	v30, v35, s[104:105] offset:0
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:0
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:0
	global_load_dword	v35, v30, s[104:105] offset:3
	global_store_dword	v30, v35, s[104:105] offset:3
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:3
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:3
	global_load_dword	v35, v30, s[104:105] offset:-1
	global_store_dword	v30, v35, s[104:105] offset:-1
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:-1
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:-1
	global_load_dword	v35, v30, s[104:105] offset:12
	global_store_dword	v30, v35, s[104:105] offset:12
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:12
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:12
	global_load_dword	v35, v30, s[104:105] offset:0x10
	global_store_dword	v30, v35, s[104:105] offset:0x10
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:0x10
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:0x10
	global_load_dword	v35, v30, s[104:105] offset:2047
	global_store_dword	v30, v35, s[104:105] offset:2047
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:2047
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:2047
	global_load_dword	v35, v30, s[104:105] offset:-2048
	global_store_dword	v30, v35, s[104:105] offset:-2048
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:-2048
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:-2048
	global_load_dword	v35, v30, s[104:105] offset:-8
	global_store_dword	v30, v35, s[104:105] offset:-8
	global_load_dwordx4	v[4:7], v30, s[104:105] offset:-8
	global_store_dwordx4	v30, v[4:7], s[104:105] offset:-8
	s_endpgm
.size memory, .-memory

; macros, nested macros, .irp, nested .irp, comments and blanks
.globl macros
.p2align 8
.type macros,@function
macros:
	one
	nothing
	pair	s2, s3
	quad	s2, s3, s4, 0x12345
	mixed	s[2:3], exec, vcc, 0x100
	mixed	s[4:5], s[6:7], s[8:9], 7
	ladder	v9
	ladder	s9
	mask	s[68:69], v17
	cond	v5, v6, v7
	two56
	sixteen
	one
	.irp	r, 0, 8, 16, 255
	v_mov_b32	v\r, v\r
	.endr
	.irp	x, s0, s7
	.irp	y, v1, v2
	v_mov_b32	\y, \x
	.endr
	.endr
	pair	s1,	s2
	pair s1 , s2
  	pair		s1,s2   ; trailing blanks and a comment
	   s_mov_b32     s1   ,   s2
	s_endpgm
.size macros, .-macros

.rodata
.p2align 6
; the block the file gives each kernel: the macro the real file uses, and the properties spelt out
.macro kernel name, vgprs, sgprs
.amdhsa_kernel \name
	.amdhsa_group_segment_fixed_size 0
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_kernarg_size 64
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_next_free_vgpr \vgprs
	.amdhsa_next_free_sgpr \sgprs
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
.end_amdhsa_kernel
.endm
kernel salu, 1, 24
kernel smem, 2, 0
kernel sopp, 4, 1
kernel vop1, 5, 104
kernel vop2, 8, 106
kernel vopc, 9, 8
kernel vop3, 16, 12
kernel vop3b, 17, 100
kernel carry, 20, 105
kernel vop3p, 33, 50
kernel memory, 64, 17
kernel macros, 65, 24

; code and blocks again after the data, and a block written out in another order
.text

.globl size2
.p2align 2
.type size2,@function
size2:
	s_endpgm
.size size2, .-size2

.globl size3
.p2align 3
.type size3,@function
size3:
	s_endpgm
.size size3, .-size3

.globl size4
.p2align 4
.type size4,@function
size4:
	s_endpgm
.size size4, .-size4

.globl size5
.p2align 5
.type size5,@function
size5:
	s_endpgm
.size size5, .-size5

.globl size6
.p2align 6
.type size6,@function
size6:
	s_endpgm
.size size6, .-size6

.globl size7
.p2align 7
.type size7,@function
size7:
	s_endpgm
.size size7, .-size7

.globl size8
.p2align 8
.type size8,@function
size8:
	s_endpgm
.size size8, .-size8

.globl size9
.p2align 2
.type size9,@function
size9:
	s_endpgm
.size size9, .-size9

.globl size100
.p2align 3
.type size100,@function
size100:
	s_endpgm
.size size100, .-size100

.globl size127
.p2align 4
.type size127,@function
size127:
	s_endpgm
.size size127, .-size127

.globl size128
.p2align 5
.type size128,@function
size128:
	s_endpgm
.size size128, .-size128

.globl size129
.p2align 6
.type size129,@function
size129:
	s_endpgm
.size size129, .-size129

.globl size192
.p2align 7
.type size192,@function
size192:
	s_endpgm
.size size192, .-size192

.globl size255
.p2align 8
.type size255,@function
size255:
	s_endpgm
.size size255, .-size255

.globl size256
.p2align 2
.type size256,@function
size256:
	s_endpgm
.size size256, .-size256

.rodata
kernel size2, 2, 8
kernel size3, 3, 8
kernel size4, 4, 8
kernel size5, 5, 8
kernel size6, 6, 8
kernel size7, 7, 8
kernel size8, 8, 8
kernel size9, 9, 8
kernel size100, 100, 8
.amdhsa_kernel size127
	.amdhsa_next_free_sgpr 24
	.amdhsa_next_free_vgpr 127
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_kernarg_size 64
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_group_segment_fixed_size 0
.end_amdhsa_kernel
.amdhsa_kernel size128
	.amdhsa_next_free_sgpr 24
	.amdhsa_next_free_vgpr 128
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_kernarg_size 64
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_group_segment_fixed_size 0
.end_amdhsa_kernel
.amdhsa_kernel size129
	.amdhsa_next_free_sgpr 24
	.amdhsa_next_free_vgpr 129
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_kernarg_size 64
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_group_segment_fixed_size 0
.end_amdhsa_kernel
.amdhsa_kernel size192
	.amdhsa_next_free_sgpr 24
	.amdhsa_next_free_vgpr 192
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_kernarg_size 64
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_group_segment_fixed_size 0
.end_amdhsa_kernel
.amdhsa_kernel size255
	.amdhsa_next_free_sgpr 24
	.amdhsa_next_free_vgpr 255
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_kernarg_size 64
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_group_segment_fixed_size 0
.end_amdhsa_kernel
.amdhsa_kernel size256
	.amdhsa_next_free_sgpr 24
	.amdhsa_next_free_vgpr 256
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
	.amdhsa_system_sgpr_workgroup_id_y 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_user_sgpr_private_segment_buffer 1
	.amdhsa_kernarg_size 64
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_group_segment_fixed_size 0
.end_amdhsa_kernel
