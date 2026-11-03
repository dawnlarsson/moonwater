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
