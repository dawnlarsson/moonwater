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

; the one lane's tone over a pixel (ui_over), the tone's parts in s72 (the rest, 255 - alpha), s73 (red and blue), s74 (green and alpha), s75 (0x00ff00ff)
.macro over out, pix
	v_and_b32	v20, s75, \pix
	v_lshrrev_b32	v21, 8, \pix
	v_and_b32	v21, s75, v21
	v_mad_u32_u24	v20, v20, s72, 0x007f007f
	v_mad_u32_u24	v21, v21, s72, 0x007f007f
	v_lshrrev_b32	v22, 8, v20
	v_and_b32	v22, s75, v22
	v_add3_u32	v20, v20, 0x00010001, v22
	v_lshrrev_b32	v20, 8, v20
	v_and_b32	v20, s75, v20
	v_lshrrev_b32	v22, 8, v21
	v_and_b32	v22, s75, v22
	v_add3_u32	v21, v21, 0x00010001, v22
	v_lshrrev_b32	v21, 8, v21
	v_and_b32	v21, s75, v21
	v_add_nc_u32	v20, s73, v20
	v_add_nc_u32	v21, s74, v21
	v_pk_min_u16	v20, v20, s75
	v_pk_min_u16	v21, v21, s75
	v_lshl_or_b32	\out, v21, 8, v20
.endm

; whether the lane's pixel at x (\xv) is in the op's rectangle, in the mask \mk (the rows' in s[76:77]; the width's end in s53)
.macro inside mk, xv
	v_cmp_le_u32_e64 \mk, s57, \xv
	v_cmp_gt_u32	vcc, s53, \xv
	s_and_b64	\mk, \mk, vcc
	s_and_b64	\mk, \mk, s[76:77]
.endm

; a tone from a VGPR (per lane): the same as over, its parts taken from \t
.macro overv out, pix, t
	v_lshrrev_b32	v39, 24, \t
	v_sub_nc_u32	v39, 0xff, v39
	v_and_b32	v40, s75, \t
	v_lshrrev_b32	v41, 8, \t
	v_and_b32	v41, s75, v41
	v_and_b32	v20, s75, \pix
	v_lshrrev_b32	v21, 8, \pix
	v_and_b32	v21, s75, v21
	v_mad_u32_u24	v20, v20, v39, 0x007f007f
	v_mad_u32_u24	v21, v21, v39, 0x007f007f
	v_lshrrev_b32	v22, 8, v20
	v_and_b32	v22, s75, v22
	v_add3_u32	v20, v20, 0x00010001, v22
	v_lshrrev_b32	v20, 8, v20
	v_and_b32	v20, s75, v20
	v_lshrrev_b32	v22, 8, v21
	v_and_b32	v22, s75, v22
	v_add3_u32	v21, v21, 0x00010001, v22
	v_lshrrev_b32	v21, 8, v21
	v_and_b32	v21, s75, v21
	v_add_nc_u32	v20, v40, v20
	v_add_nc_u32	v21, v41, v21
	v_pk_min_u16	v20, v20, s75
	v_pk_min_u16	v21, v21, s75
	v_lshl_or_b32	\out, v21, 8, v20
.endm

; \out = ui_premultiplied(the op's colour, \cover): a = alpha * cover / 4096, each colour (c * (a + (a >> 7)) + 128) >> 8
; (the colour's alpha in s52, blue in s64, green in s65, red in s66)
.macro premulv out, cover
	v_mul_u32_u24	v35, s52, \cover
	v_lshrrev_b32	v35, 12, v35
	v_lshrrev_b32	v36, 7, v35
	v_add_nc_u32	v36, v35, v36
	v_mad_u32_u24	v37, s64, v36, 128
	v_lshrrev_b32	v37, 8, v37
	v_mad_u32_u24	v38, s65, v36, 128
	v_lshrrev_b32	v38, 8, v38
	v_mad_u32_u24	v36, s66, v36, 128
	v_lshrrev_b32	v36, 8, v36
	v_lshl_or_b32	v37, v38, 8, v37
	v_lshl_or_b32	v37, v36, 16, v37
	v_lshl_or_b32	\out, v35, 24, v37
.endm

; the cover of a row in the straight middle over the pixel at \p0 (256ths): the overlap of [x0, x1) with [p0, p0 + 256), times 16
.macro cover_straight ck, p0
	v_max_i32	v9, s57, \p0
	v_add_nc_u32	v11, 0x100, \p0
	v_min_i32	v11, s59, v11
	v_sub_nc_u32	v11, v11, v9
	v_max_i32	v11, 0, v11
	v_lshlrev_b32	\ck, 4, v11
.endm

; one line's span (v45 left, v46 right, from the lane that has it) added to the four covers
.macro accumulate ck, p0, p1
	v_max_i32	v47, v45, \p0
	v_min_i32	v48, v46, \p1
	v_sub_nc_u32	v48, v48, v47
	v_max_i32	v48, 0, v48
	v_add_nc_u32	\ck, \ck, v48
.endm

; the pixel \o of the lane with the box's cover \ck: the colour at that cover laid over it where it has any alpha
.macro boxpixel o, ck
	premulv		v34, \ck
	overv		v23, \o, v34
	v_cmp_lt_u32_e64 s[88:89], 0x00ffffff, v34
	v_cndmask_b32_e64 \o, \o, v23, s[88:89]
.endm

; \out = ui_tone(the op's colour, \a): the colour premultiplied at the alpha \a (0 to 255) (the colour's blue, green, red in s94, s95, s96)
.macro tonev out, a
	v_lshrrev_b32	v53, 7, \a
	v_add_nc_u32	v53, \a, v53
	v_mad_u32_u24	v54, s94, v53, 128
	v_lshrrev_b32	v54, 8, v54
	v_mad_u32_u24	v55, s95, v53, 128
	v_lshrrev_b32	v55, 8, v55
	v_mad_u32_u24	v56, s96, v53, 128
	v_lshrrev_b32	v56, 8, v56
	v_lshl_or_b32	v54, v55, 8, v54
	v_lshl_or_b32	v54, v56, 16, v54
	v_lshl_or_b32	\out, \a, 24, v54
.endm

; one pixel of a text run (ui_run_in's walk): its cover \cov and its neighbours' (\lf \rt \up \dn), the stroke's share of the strongest neighbour
; (stroke s80), the cover at the alpha (s52) as a tone, laid over \o where the pixel is in the run (\mk)
.macro textpixel o, cov, lf, rt, up, dn, mk
	v_max_u32	v50, \lf, \rt
	v_max_u32	v50, v50, \up
	v_max_u32	v50, v50, \dn
	v_mul_u32_u24	v50, v50, s80
	v_lshrrev_b32	v50, 7, v50
	v_add_nc_u32	v51, \cov, v50
	v_min_u32	v51, 255, v51
	v_mul_u32_u24	v52, v51, s52
	v_lshrrev_b32	v57, 8, v52
	v_add3_u32	v52, v52, v57, 1
	v_lshrrev_b32	v52, 8, v52
	tonev		v58, v52
	overv		v59, \o, v58
	v_cndmask_b32_e64 \o, \o, v59, \mk
.endm

; op 6: a scene, from the patch (x0, y0 in the arguments, in patches of 64 pixels by 4 rows) on for the grid's size, so that a part of the picture is drawn alone (records of 8 words in the lists: 0 fill, 1 tone over, 2 box as ui_kit_fill takes it with its radii already scaled as CSS draws them),
; a wave a patch of 64 pixels by 4 rows, a lane four pixels in a row (v5 to v8), each written once; the width and the pitch a multiple of 4.
; The wave leaves if its 64-pixel square has no op, does not read the picture when the first op holds the patch, and skips an op that misses it.
; The lanes of a row in a box's corner take a line each of its sixteen (the 64-bit quotient and the root by a float and a correction, exact)
; and read each other's with ds_bpermute, so the lanes past the picture's edge stay until the store.
.globl scene
.p2align 8
.type scene,@function
scene:
	s_load_dwordx4	s[8:11], s[4:5], 0x8
	s_load_dwordx4	s[12:15], s[4:5], 0x18
	s_load_dwordx2	s[2:3], s[4:5], 0x30
	s_load_dwordx2	s[102:103], s[4:5], 0x0
	v_and_b32	v1, 15, v0
	v_lshrrev_b32	v2, 4, v0
	s_waitcnt	lgkmcnt(0)
	s_add_u32	s6, s6, s2
	s_add_u32	s7, s7, s3
	s_lshl_b32	s28, s6, 6
	s_lshl_b32	s29, s7, 2
	s_add_u32	s16, s12, 63
	s_lshr_b32	s16, s16, 6
	s_add_u32	s17, s14, 63
	s_lshr_b32	s17, s17, 6
	s_lshr_b32	s18, s7, 4
	s_mul_i32	s18, s18, s16
	s_add_u32	s18, s18, s6
	s_lshl_b32	s19, s18, 3
	s_add_u32	s20, s10, s19
	s_addc_u32	s21, s11, 0
	s_load_dwordx2	s[22:23], s[20:21], 0x0
	s_mul_i32	s24, s16, s17
	s_lshl_b32	s24, s24, 3
	s_waitcnt	lgkmcnt(0)
	s_cmp_eq_u32	s23, 0
	s_cbranch_scc1	.scene_done
	s_lshl_b32	s25, s22, 5
	s_add_u32	s24, s24, s25
	s_add_u32	s26, s10, s24
	s_addc_u32	s27, s11, 0
	s_load_dwordx8	s[32:39], s[26:27], 0x0
	v_lshl_add_u32	v3, v1, 2, s28
	v_add_nc_u32	v4, s29, v2
	v_cmp_gt_u32	vcc, s14, v4
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.scene_done
	v_cmp_gt_u32_e64 s[30:31], s12, v3
	v_mad_u32_u24	v10, v4, s13, v3
	v_lshlrev_b32	v10, 2, v10
	v_add_nc_u32	v17, 1, v3
	v_add_nc_u32	v18, 2, v3
	v_add_nc_u32	v19, 3, v3
	s_add_u32	s40, s28, 64
	s_min_u32	s40, s40, s12
	s_add_u32	s41, s29, 4
	s_min_u32	s41, s41, s14
	s_waitcnt	lgkmcnt(0)
	s_cmp_eq_u32	s32, 2
	s_cbranch_scc1	.scene_first_box
	s_cmp_lg_u32	s32, 0
	s_cbranch_scc1	.scene_read
	s_cmp_gt_u32	s33, s28
	s_cbranch_scc1	.scene_read
	s_cmp_lt_u32	s35, s40
	s_cbranch_scc1	.scene_read
	s_cmp_gt_u32	s34, s29
	s_cbranch_scc1	.scene_read
	s_cmp_lt_u32	s36, s41
	s_cbranch_scc1	.scene_read
	s_branch	.scene_ops
.scene_first_box:
	s_lshr_b32	s43, s37, 24
	s_cmp_lg_u32	s43, 255
	s_cbranch_scc1	.scene_read
	s_lshl_b32	s78, s28, 8
	s_lshl_b32	s79, s29, 8
	s_lshl_b32	s80, s40, 8
	s_lshl_b32	s81, s41, 8
	s_cmp_gt_i32	s33, s78
	s_cbranch_scc1	.scene_read
	s_cmp_lt_i32	s35, s80
	s_cbranch_scc1	.scene_read
	s_cmp_gt_i32	s34, s79
	s_cbranch_scc1	.scene_read
	s_cmp_lt_i32	s36, s81
	s_cbranch_scc1	.scene_read
	s_add_i32	s43, s34, s39
	s_cmp_gt_i32	s43, s79
	s_cbranch_scc1	.scene_read
	s_sub_i32	s43, s36, s39
	s_cmp_lt_i32	s43, s81
	s_cbranch_scc1	.scene_read
	s_branch	.scene_ops
.scene_read:
	s_mov_b64	s[2:3], exec
	s_and_b64	exec, exec, s[30:31]
	global_load_dwordx4 v[5:8], v10, s[8:9]
	s_waitcnt	vmcnt(0)
	s_mov_b64	exec, s[2:3]
.scene_ops:
	s_mov_b32	s75, 0x00ff00ff
	s_mov_b32	s44, 0
	s_mov_b32	s45, 0
.scene_loop:
	s_cmp_ge_u32	s44, s23
	s_cbranch_scc1	.scene_store
	s_lshl_b32	s46, s44, 5
	s_add_u32	s48, s26, s46
	s_addc_u32	s49, s27, 0
	s_load_dwordx8	s[56:63], s[48:49], 0x0
	s_add_u32	s44, s44, 1
	s_waitcnt	lgkmcnt(0)
	s_and_b32	s47, s56, 0xff
	s_cmp_eq_u32	s47, 2
	s_cbranch_scc1	.scene_box
	s_cmp_eq_u32	s47, 3
	s_cbranch_scc1	.scene_text
	s_cmp_le_u32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_u32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_u32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_u32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_lshr_b32	s52, s61, 24
	s_cmp_eq_u32	s56, 0
	s_cbranch_scc1	.scene_mask
	s_cmp_eq_u32	s52, 0
	s_cbranch_scc1	.scene_loop
.scene_mask:
	s_mov_b32	s45, 1
	s_min_u32	s53, s59, s12
	v_cmp_le_u32_e64 s[76:77], s58, v4
	v_cmp_gt_u32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[64:65], v3
	inside		s[66:67], v17
	inside		s[68:69], v18
	inside		s[70:71], v19
	s_cmp_eq_u32	s56, 0
	s_cbranch_scc0	.scene_tone
	v_mov_b32	v15, s61
	v_cndmask_b32_e64 v5, v5, v15, s[64:65]
	v_cndmask_b32_e64 v6, v6, v15, s[66:67]
	v_cndmask_b32_e64 v7, v7, v15, s[68:69]
	v_cndmask_b32_e64 v8, v8, v15, s[70:71]
	s_branch	.scene_loop
.scene_tone:
	s_sub_u32	s72, 255, s52
	s_and_b32	s73, s61, s75
	s_lshr_b32	s74, s61, 8
	s_and_b32	s74, s74, s75
	over		v23, v5
	v_cndmask_b32_e64 v5, v5, v23, s[64:65]
	over		v23, v6
	v_cndmask_b32_e64 v6, v6, v23, s[66:67]
	over		v23, v7
	v_cndmask_b32_e64 v7, v7, v23, s[68:69]
	over		v23, v8
	v_cndmask_b32_e64 v8, v8, v23, s[70:71]
	s_branch	.scene_loop

; a text run (3 | the pitch << 8, x0, y0, x1, y1, the ink, alpha | stroke << 8, the bitmap's place from the buffer's start): the cover of each pixel is
; a byte of the bitmap, with a border of nothing round it; each lane reads its four bytes and the ones beside and above and below by unaligned dwords
.scene_text:
	s_cmp_le_u32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_u32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_u32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_u32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_and_b32	s52, s62, 0xff
	s_cmp_eq_u32	s52, 0
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_bfe_u32	s80, s62, 0x80008
	s_lshr_b32	s81, s56, 8
	s_and_b32	s94, s61, 255
	s_bfe_u32	s95, s61, 0x80008
	s_bfe_u32	s96, s61, 0x80010
	s_min_u32	s53, s59, s12
	v_cmp_le_u32_e64 s[76:77], s58, v4
	v_cmp_gt_u32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	s_and_saveexec_b64 s[92:93], s[76:77]
	s_cbranch_execz	.text_done
	v_subrev_nc_u32	v30, s58, v4
	v_mad_u32_u24	v30, v30, s81, s63
	v_subrev_nc_u32	v31, s57, v3
	v_add_nc_u32	v30, v30, v31
	v_subrev_nc_u32	v32, s81, v30
	v_add_nc_u32	v33, s81, v30
	global_load_dword v34, v30, s[102:103] offset:-1
	global_load_dword v35, v30, s[102:103] offset:3
	global_load_dword v36, v32, s[102:103]
	global_load_dword v37, v33, s[102:103]
	s_waitcnt	vmcnt(0)
	v_bfe_u32	v74, v34, 0, 8
	v_bfe_u32	v70, v34, 8, 8
	v_bfe_u32	v71, v34, 16, 8
	v_bfe_u32	v72, v34, 24, 8
	v_bfe_u32	v73, v35, 0, 8
	v_bfe_u32	v75, v35, 8, 8
	v_bfe_u32	v76, v36, 0, 8
	v_bfe_u32	v77, v36, 8, 8
	v_bfe_u32	v78, v36, 16, 8
	v_bfe_u32	v79, v36, 24, 8
	v_bfe_u32	v80, v37, 0, 8
	v_bfe_u32	v81, v37, 8, 8
	v_bfe_u32	v82, v37, 16, 8
	v_bfe_u32	v83, v37, 24, 8
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	textpixel	v5, v70, v74, v71, v76, v80, s[82:83]
	textpixel	v6, v71, v70, v72, v77, v81, s[84:85]
	textpixel	v7, v72, v71, v73, v78, v82, s[86:87]
	textpixel	v8, v73, v72, v75, v79, v83, s[88:89]
.text_done:
	s_mov_b64	exec, s[92:93]
	s_branch	.scene_loop

; a box (256ths: x0 s57, y0 s58, x1 s59, y1 s60, the colour s61, rx s62, ry s63)
.scene_box:
	s_lshl_b32	s78, s28, 8
	s_lshl_b32	s79, s29, 8
	s_lshl_b32	s80, s40, 8
	s_lshl_b32	s81, s41, 8
	s_cmp_le_i32	s59, s57
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s58
	s_cbranch_scc1	.scene_loop
	s_cmp_ge_i32	s78, s59
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s80, s57
	s_cbranch_scc1	.scene_loop
	s_cmp_ge_i32	s79, s60
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s81, s58
	s_cbranch_scc1	.scene_loop
	s_lshr_b32	s52, s61, 24
	s_cmp_eq_u32	s52, 0
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_and_b32	s64, s61, 255
	s_bfe_u32	s65, s61, 0x80008
	s_bfe_u32	s66, s61, 0x80010
	s_add_i32	s86, s58, s63
	s_sub_i32	s87, s60, s63
	s_cmp_lt_i32	s78, s57
	s_cbranch_scc1	.box_edge
	s_cmp_gt_i32	s80, s59
	s_cbranch_scc1	.box_edge
	s_cmp_lt_i32	s79, s58
	s_cbranch_scc1	.box_edge
	s_cmp_gt_i32	s81, s60
	s_cbranch_scc1	.box_edge
	s_cmp_lt_i32	s79, s86
	s_cbranch_scc1	.box_edge
	s_cmp_gt_i32	s81, s87
	s_cbranch_scc1	.box_edge
; the patch is whole inside the box's straight middle: the colour at a whole cover, one tone over all the lanes
	s_lshr_b32	s67, s52, 7
	s_add_u32	s67, s52, s67
	s_mul_i32	s68, s64, s67
	s_add_u32	s68, s68, 128
	s_lshr_b32	s68, s68, 8
	s_mul_i32	s69, s65, s67
	s_add_u32	s69, s69, 128
	s_lshr_b32	s69, s69, 8
	s_mul_i32	s70, s66, s67
	s_add_u32	s70, s70, 128
	s_lshr_b32	s70, s70, 8
	s_lshl_b32	s70, s70, 16
	s_lshl_b32	s69, s69, 8
	s_or_b32	s70, s70, s69
	s_or_b32	s70, s70, s68
	s_lshl_b32	s71, s52, 24
	s_or_b32	s71, s71, s70
	s_cmp_eq_u32	s52, 255
	s_cbranch_scc0	.box_whole_over
	v_mov_b32	v5, s71
	v_mov_b32	v6, s71
	v_mov_b32	v7, s71
	v_mov_b32	v8, s71
	s_branch	.scene_loop
.box_whole_over:
	s_sub_u32	s72, 255, s52
	s_and_b32	s73, s71, s75
	s_lshr_b32	s74, s71, 8
	s_and_b32	s74, s74, s75
	over		v5, v5
	over		v6, v6
	over		v7, v7
	over		v8, v8
	s_branch	.scene_loop

; the patch holds an edge: the cover of each of the lane's four pixels, then the colour at it
.box_edge:
	v_lshlrev_b32	v32, 8, v4
	v_add_nc_u32	v33, 0x100, v32
	v_lshlrev_b32	v28, 8, v3
	v_lshlrev_b32	v29, 8, v17
	v_lshlrev_b32	v30, 8, v18
	v_lshlrev_b32	v31, 8, v19
	v_add_nc_u32	v65, 0x100, v28
	v_add_nc_u32	v66, 0x100, v29
	v_add_nc_u32	v67, 0x100, v30
	v_add_nc_u32	v68, 0x100, v31
	v_cmp_lt_i32_e64 s[82:83], s58, v33
	v_cmp_gt_i32_e64 s[88:89], s60, v32
	s_and_b64	s[82:83], s[82:83], s[88:89]
	v_cmp_ge_i32_e64 s[84:85], v32, s86
	v_cmp_le_i32_e64 s[88:89], v33, s87
	s_and_b64	s[84:85], s[84:85], s[88:89]
	v_cmp_ge_i32_e64 s[88:89], v32, s58
	s_and_b64	s[84:85], s[84:85], s[88:89]
	v_cmp_le_i32_e64 s[88:89], v33, s60
	s_and_b64	s[84:85], s[84:85], s[88:89]
	cover_straight	v24, v28
	cover_straight	v25, v29
	cover_straight	v26, v30
	cover_straight	v27, v31
	s_andn2_b64	s[90:91], s[82:83], s[84:85]
	s_and_saveexec_b64 s[92:93], s[90:91]
	s_cbranch_execz	.box_corner_done
; this lane's line of sixteen: yy = top + (lane & 15) * 16 + 8, and its span
	v_mov_b32	v24, 0
	v_mov_b32	v25, 0
	v_mov_b32	v26, 0
	v_mov_b32	v27, 0
	v_and_b32	v43, 15, v0
	v_lshlrev_b32	v43, 4, v43
	v_add3_u32	v44, v32, v43, 8
	v_cmp_ge_i32_e64 s[94:95], v44, s58
	v_cmp_lt_i32_e64 s[96:97], v44, s60
	s_and_b64	s[94:95], s[94:95], s[96:97]
	s_cmp_le_i32	s62, 0
	s_cbranch_scc1	.box_flat
	s_cmp_le_i32	s63, 0
	s_cbranch_scc1	.box_flat
	v_sub_nc_u32	v45, s86, v44
	v_sub_nc_u32	v46, v44, s87
	v_add_nc_u32	v46, 1, v46
	v_cmp_le_i32_e64 s[96:97], s87, v44
	v_mov_b32	v47, 0
	v_cndmask_b32_e64 v47, v47, v46, s[96:97]
	v_cmp_gt_i32_e64 s[96:97], s86, v44
	v_cndmask_b32_e64 v47, v47, v45, s[96:97]
	v_cmp_lt_i32_e64 s[96:97], 0, v47
	v_cvt_f32_u32	v48, v47
	v_cvt_f32_u32	v49, s63
	v_rcp_f32	v49, v49
	v_mul_f32	v48, v48, v49
	v_mul_f32	v48, 0x47800000, v48
	v_cvt_u32_f32	v48, v48
	v_lshlrev_b32	v50, 16, v47
	v_lshrrev_b32	v51, 16, v47
	v_mad_u64_u32	v[52:53], null, v48, s63, 0
	v_cmp_gt_u64_e64 s[98:99], v[52:53], v[50:51]
	v_subrev_nc_u32	v54, 1, v48
	v_cndmask_b32_e64 v48, v48, v54, s[98:99]
	v_add_nc_u32	v54, 1, v48
	v_mad_u64_u32	v[52:53], null, v54, s63, 0
	v_cmp_le_u64_e64 s[98:99], v[52:53], v[50:51]
	v_cndmask_b32_e64 v48, v48, v54, s[98:99]
	v_mul_lo_u32	v49, v48, v48
	v_sub_nc_u32	v49, 0, v49
	v_cvt_f32_u32	v54, v49
	v_sqrt_f32	v54, v54
	v_cvt_u32_f32	v54, v54
	v_mov_b32	v56, v49
	v_mov_b32	v57, 0
	v_mad_u64_u32	v[58:59], null, v54, v54, 0
	v_cmp_gt_u64_e64 s[98:99], v[58:59], v[56:57]
	v_subrev_nc_u32	v60, 1, v54
	v_cndmask_b32_e64 v54, v54, v60, s[98:99]
	v_add_nc_u32	v60, 1, v54
	v_mad_u64_u32	v[58:59], null, v60, v60, 0
	v_cmp_le_u64_e64 s[98:99], v[58:59], v[56:57]
	v_cndmask_b32_e64 v54, v54, v60, s[98:99]
	v_mad_u64_u32	v[58:59], null, v54, s62, 0
	v_alignbit_b32	v60, v59, v58, 16
	v_cmp_gt_u32_e64 s[98:99], 0x10000, v48
	v_cndmask_b32_e64 v60, 0, v60, s[98:99]
	s_add_i32	s100, s57, s62
	s_sub_i32	s101, s59, s62
	v_sub_nc_u32	v61, s100, v60
	v_add_nc_u32	v62, s101, v60
	v_cndmask_b32_e64 v61, s57, v61, s[96:97]
	v_cndmask_b32_e64 v62, s59, v62, s[96:97]
	v_cmp_gt_i32_e64 s[98:99], v62, v61
	s_and_b64	s[94:95], s[94:95], s[98:99]
	s_branch	.box_lines
.box_flat:
	v_mov_b32	v61, s57
	v_mov_b32	v62, s59
.box_lines:
	v_cndmask_b32_e64 v61, 0, v61, s[94:95]
	v_cndmask_b32_e64 v62, 0, v62, s[94:95]
	v_and_b32	v43, 48, v0
	v_lshlrev_b32	v43, 2, v43
	.irp		j, 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60
	v_add_nc_u32	v44, \j, v43
	ds_bpermute_b32	v45, v44, v61
	ds_bpermute_b32	v46, v44, v62
	s_waitcnt	lgkmcnt(0)
	accumulate	v24, v28, v65
	accumulate	v25, v29, v66
	accumulate	v26, v30, v67
	accumulate	v27, v31, v68
	.endr
.box_corner_done:
	s_mov_b64	exec, s[92:93]
	v_cndmask_b32_e64 v24, 0, v24, s[82:83]
	v_cndmask_b32_e64 v25, 0, v25, s[82:83]
	v_cndmask_b32_e64 v26, 0, v26, s[82:83]
	v_cndmask_b32_e64 v27, 0, v27, s[82:83]
	boxpixel	v5, v24
	boxpixel	v6, v25
	boxpixel	v7, v26
	boxpixel	v8, v27
	s_branch	.scene_loop

.scene_store:
	s_cmp_eq_u32	s45, 0
	s_cbranch_scc1	.scene_done
	s_and_b64	exec, exec, s[30:31]
	global_store_dwordx4 v10, v[5:8], s[8:9]
.scene_done:
	s_endpgm
.size scene, .-scene

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
kernel scene, 88, 106
