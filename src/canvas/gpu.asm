; The GPU's kernels, by hand: AMD RDNA2 (gfx1036), wave64. test/gpu_kernels.py assembles this and writes the arrays gpu.c holds.
; In: s[4:5] the arguments (struct gpu_args), s6 s7 the workgroup's x and y, v0 the lane. The pixels are 32 bits.
.amdgcn_target "amdgcn-amd-amdhsa--gfx1036"
.text

; op 0: a pixel a lane, d[y * dp + x] = s[y * sp + x] for x below w
.globl copy
.p2align 8
.type copy,@function
copy:
	s_load_dwordx4	s[8:11], s[4:5], 0x0
	s_load_dwordx2	s[14:15], s[4:5], 0x18
	s_load_dword	s16, s[4:5], 0x24
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.copy_done
	s_mul_i32	s18, s7, s14
	s_mul_i32	s19, s7, s15
	v_add_nc_u32	v2, s18, v1
	v_add_nc_u32	v3, s19, v1
	v_lshlrev_b32	v2, 2, v2
	v_lshlrev_b32	v3, 2, v3
	global_load_dword v4, v2, s[8:9]
	s_waitcnt	vmcnt(0)
	global_store_dword v3, v4, s[10:11]
.copy_done:
	s_endpgm
.size copy, .-copy

; op 1: d[y * dp + x] = v for x below w
.globl fill
.p2align 8
.type fill,@function
fill:
	s_load_dwordx2	s[10:11], s[4:5], 0x8
	s_load_dword	s15, s[4:5], 0x1c
	s_load_dwordx2	s[16:17], s[4:5], 0x24
	s_load_dword	s20, s[4:5], 0x2c
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.fill_done
	s_mul_i32	s19, s7, s15
	v_add_nc_u32	v3, s19, v1
	v_lshlrev_b32	v3, 2, v3
	v_mov_b32	v4, s20
	global_store_dword v3, v4, s[10:11]
.fill_done:
	s_endpgm
.size fill, .-fill

; op 2: the tone v laid over d[y * dp + x] as ui_over lays it, two channels in each 32-bit register (a lane of 16 bits holds at most 255 * 255 + 127)
.globl tone
.p2align 8
.type tone,@function
tone:
	s_load_dwordx2	s[10:11], s[4:5], 0x8
	s_load_dword	s15, s[4:5], 0x1c
	s_load_dwordx2	s[16:17], s[4:5], 0x24
	s_load_dword	s20, s[4:5], 0x2c
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	s_lshr_b32	s21, s20, 24
	s_cmp_eq_u32	s21, 0
	s_cbranch_scc1	.tone_done
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.tone_done
	s_mul_i32	s19, s7, s15
	v_add_nc_u32	v3, s19, v1
	v_lshlrev_b32	v3, 2, v3
	global_load_dword v4, v3, s[10:11]
	s_sub_u32	s21, 255, s21
	s_mov_b32	s22, 0x00ff00ff
	s_and_b32	s23, s20, s22
	s_lshr_b32	s24, s20, 8
	s_and_b32	s24, s24, s22
	s_waitcnt	vmcnt(0)
	v_and_b32	v5, s22, v4
	v_lshrrev_b32	v6, 8, v4
	v_and_b32	v6, s22, v6
	v_mad_u32_u24	v5, v5, s21, 0x007f007f
	v_mad_u32_u24	v6, v6, s21, 0x007f007f
	v_lshrrev_b32	v7, 8, v5
	v_and_b32	v7, s22, v7
	v_add3_u32	v5, v5, 0x00010001, v7
	v_lshrrev_b32	v5, 8, v5
	v_and_b32	v5, s22, v5
	v_lshrrev_b32	v8, 8, v6
	v_and_b32	v8, s22, v8
	v_add3_u32	v6, v6, 0x00010001, v8
	v_lshrrev_b32	v6, 8, v6
	v_and_b32	v6, s22, v6
	v_add_nc_u32	v5, s23, v5
	v_add_nc_u32	v6, s24, v6
	v_pk_min_u16	v5, v5, s22
	v_pk_min_u16	v6, v6, s22
	v_lshl_or_b32	v4, v6, 8, v5
	global_store_dword v3, v4, s[10:11]
.tone_done:
	s_endpgm
.size tone, .-tone

; op 3: the picture over the ground with its grain, into d: the ground's colours plus (dark, v = 0) or less (light) the grain
; 2 * ((x + x0 ^ y + y0) & 1) + (y + y0 & 1), then the picture's alpha lays the picture over it as tone does
.globl glass
.p2align 8
.type glass,@function
glass:
	s_load_dwordx8	s[8:15], s[4:5], 0x0
	s_load_dwordx8	s[16:23], s[4:5], 0x20
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s17, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.glass_done
	s_mul_i32	s24, s7, s14
	s_mul_i32	s25, s7, s15
	s_mul_i32	s26, s7, s16
	v_add_nc_u32	v2, s24, v1
	v_lshlrev_b32	v2, 2, v2
	global_load_dword v4, v2, s[8:9]
	v_add_nc_u32	v3, s26, v1
	v_lshlrev_b32	v3, 2, v3
	global_load_dword v5, v3, s[12:13]
	s_add_u32	s27, s7, s21
	s_and_b32	s28, s27, 1
	v_add_nc_u32	v6, s20, v1
	v_xor_b32	v6, s27, v6
	v_and_b32	v6, 1, v6
	v_lshl_add_u32	v6, v6, 1, s28
	v_mul_u32_u24	v7, 0x00010001, v6
	s_mov_b32	s29, 0x00ff00ff
	s_waitcnt	vmcnt(0)
	v_and_b32	v8, s29, v5
	v_lshrrev_b32	v9, 8, v5
	v_and_b32	v9, s29, v9
	s_cmp_eq_u32	s19, 0
	s_cbranch_scc0	.glass_light
	v_add_nc_u32	v8, v8, v7
	v_add_nc_u32	v9, v9, v6
	v_pk_min_u16	v8, v8, s29
	v_pk_min_u16	v9, v9, s29
	s_branch	.glass_blend
.glass_light:
	v_pk_sub_u16	v8, v8, v7 clamp
	v_pk_sub_u16	v9, v9, v6 clamp
.glass_blend:
	v_lshrrev_b32	v10, 24, v4
	v_sub_nc_u32	v10, 0xff, v10
	v_mad_u32_u24	v8, v8, v10, 0x007f007f
	v_mad_u32_u24	v9, v9, v10, 0x007f007f
	v_lshrrev_b32	v11, 8, v8
	v_and_b32	v11, s29, v11
	v_add3_u32	v8, v8, 0x00010001, v11
	v_lshrrev_b32	v8, 8, v8
	v_and_b32	v8, s29, v8
	v_lshrrev_b32	v12, 8, v9
	v_and_b32	v12, s29, v12
	v_add3_u32	v9, v9, 0x00010001, v12
	v_lshrrev_b32	v9, 8, v9
	v_and_b32	v9, s29, v9
	v_and_b32	v13, s29, v4
	v_lshrrev_b32	v14, 8, v4
	v_and_b32	v14, s29, v14
	v_add_nc_u32	v8, v8, v13
	v_add_nc_u32	v9, v9, v14
	v_pk_min_u16	v8, v8, s29
	v_pk_min_u16	v9, v9, s29
	v_lshl_or_b32	v4, v9, 8, v8
	v_add_nc_u32	v2, s25, v1
	v_lshlrev_b32	v2, 2, v2
	global_store_dword v2, v4, s[10:11]
.glass_done:
	s_endpgm
.size glass, .-glass

.rodata
.p2align 6
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
.end_amdhsa_kernel
.endm
kernel copy, 8, 24
kernel fill, 8, 24
kernel tone, 10, 28
kernel glass, 16, 32
