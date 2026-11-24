; The GPU's kernels, by hand: AMD RDNA2 (gfx1036), wave64. test/gpu_kernels.py assembles this and writes the arrays gpu.c holds.
; In: s[4:5] the arguments (struct gpu_args), s6 s7 the workgroup's x and y, v0 the lane. The pixels are 32 bits.
; The scalar registers are s0 to s101 and no instruction names null (a carry nobody reads goes to s[36:37]): a gfx9 chip has neither s102 on nor null.
; The build makes gfx11's, gfx12's and gfx9's words of this text too (src/build/gpuasm.c). gfx9's long encodings take no literal: a kernel's literals
; there are vector registers of its own, the ones after those its block declares, loaded at its label, so those are left free of it.
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
	v_cmp_le_i32_e64 \mk, s57, \xv
	v_cmp_gt_i32	vcc, s53, \xv
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

; the pixel \o of the lane with the box's cover \ck: on an opaque destination (alpha 255), exact IEC 61966-2-1 linear-light AA
; between \o and the full-cover over colour (s72/s73/s74); on a transparent destination, standard premultiplied over.
; s[98:99] holds GPU_VA + GPU_SRGB_S2L_AT (59392), s[100:101] holds GPU_VA + GPU_SRGB_L2S_AT (60416).
.macro boxpixel o, ck
	premulv		v34, \ck
	overv		v23, \o, v34
	v_cmp_lt_u32_e64 s[88:89], 0x00ffffff, v34
	over		v53, \o
	v_min_u32	v39, 4096, \ck
	v_sub_nc_u32	v40, 4096, v39
	v_bfe_u32	v41, \o, 0, 8
	v_bfe_u32	v42, \o, 8, 8
	v_bfe_u32	v43, \o, 16, 8
	v_bfe_u32	v44, v53, 0, 8
	v_bfe_u32	v45, v53, 8, 8
	v_bfe_u32	v46, v53, 16, 8
	v_lshlrev_b32	v41, 2, v41
	v_lshlrev_b32	v42, 2, v42
	v_lshlrev_b32	v43, 2, v43
	v_lshlrev_b32	v44, 2, v44
	v_lshlrev_b32	v45, 2, v45
	v_lshlrev_b32	v46, 2, v46
	global_load_dword v47, v41, s[98:99]
	global_load_dword v48, v42, s[98:99]
	global_load_dword v49, v43, s[98:99]
	global_load_dword v50, v44, s[98:99]
	global_load_dword v51, v45, s[98:99]
	global_load_dword v52, v46, s[98:99]
	s_waitcnt	vmcnt(0)
	v_mad_u32_u24	v47, v47, v40, 2048
	v_mad_u32_u24	v47, v50, v39, v47
	v_lshrrev_b32	v47, 12, v47
	v_mad_u32_u24	v48, v48, v40, 2048
	v_mad_u32_u24	v48, v51, v39, v48
	v_lshrrev_b32	v48, 12, v48
	v_mad_u32_u24	v49, v49, v40, 2048
	v_mad_u32_u24	v49, v52, v39, v49
	v_lshrrev_b32	v49, 12, v49
	global_load_ubyte v41, v47, s[100:101]
	global_load_ubyte v42, v48, s[100:101]
	global_load_ubyte v43, v49, s[100:101]
	s_waitcnt	vmcnt(0)
	v_lshl_or_b32	v41, v42, 8, v41
	v_lshl_or_b32	v41, v43, 16, v41
	v_lshl_or_b32	v41, 255, 24, v41
	v_cmp_lt_u32_e64 s[84:85], 0xfeffffff, \o
	v_cmp_ne_u32_e64 s[86:87], 0, \ck
	s_and_b64	s[86:87], s[84:85], s[86:87]
	s_andn2_b64	s[88:89], s[88:89], s[84:85]
	v_cndmask_b32_e64 \o, \o, v23, s[88:89]
	v_cndmask_b32_e64 \o, \o, v41, s[86:87]
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

; four UNORM float channels (\r \g \b \a) from image_load dmask:0xf packed into a 32-bit pixel \out (same rounding as kernel image)
.macro packunorm out, r, g, b, a
	v_mul_f32	\r, 0x437f8000, \r
	v_mul_f32	\g, 0x437f8000, \g
	v_mul_f32	\b, 0x437f8000, \b
	v_mul_f32	\a, 0x437f8000, \a
	v_cvt_u32_f32	\r, \r
	v_cvt_u32_f32	\g, \g
	v_cvt_u32_f32	\b, \b
	v_cvt_u32_f32	\a, \a
	v_lshl_or_b32	\out, \g, 8, \r
	v_lshl_or_b32	\out, \b, 16, \out
	v_lshl_or_b32	\out, \a, 24, \out
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
	s_load_dwordx2	s[32:33], s[4:5], 0x0
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
	s_load_dwordx8	s[56:63], s[26:27], 0x0
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
	s_cmp_eq_u32	s56, 2
	s_cbranch_scc1	.scene_first_box
	s_and_b32	s43, s56, 0x1f
	s_cmp_eq_u32	s43, 4
	s_cbranch_scc1	.scene_first_rect
	s_cmp_lg_u32	s56, 0
	s_cbranch_scc1	.scene_read
.scene_first_rect:
	s_cmp_gt_i32	s57, s28
	s_cbranch_scc1	.scene_read
	s_cmp_lt_i32	s59, s40
	s_cbranch_scc1	.scene_read
	s_cmp_gt_i32	s58, s29
	s_cbranch_scc1	.scene_read
	s_cmp_lt_i32	s60, s41
	s_cbranch_scc1	.scene_read
	s_branch	.scene_ops
.scene_first_box:
	s_lshr_b32	s43, s61, 24
	s_cmp_lg_u32	s43, 255
	s_cbranch_scc1	.scene_read
	s_lshl_b32	s78, s28, 8
	s_lshl_b32	s79, s29, 8
	s_lshl_b32	s80, s40, 8
	s_lshl_b32	s81, s41, 8
	s_cmp_gt_i32	s57, s78
	s_cbranch_scc1	.scene_read
	s_cmp_lt_i32	s59, s80
	s_cbranch_scc1	.scene_read
	s_cmp_gt_i32	s58, s79
	s_cbranch_scc1	.scene_read
	s_cmp_lt_i32	s60, s81
	s_cbranch_scc1	.scene_read
	s_add_i32	s43, s58, s63
	s_cmp_gt_i32	s43, s79
	s_cbranch_scc1	.scene_read
	s_sub_i32	s43, s60, s63
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
	s_mov_b32	s44, 1
	s_mov_b32	s45, 0
	s_mov_b64	s[48:49], s[26:27]
	s_branch	.scene_dispatch
.scene_loop:
	s_cmp_ge_u32	s44, s23
	s_cbranch_scc1	.scene_store
	s_lshl_b32	s46, s44, 5
	s_add_u32	s48, s26, s46
	s_addc_u32	s49, s27, 0
	s_load_dwordx8	s[56:63], s[48:49], 0x0
	s_add_u32	s44, s44, 1
	s_waitcnt	lgkmcnt(0)
.scene_dispatch:
	s_and_b32	s47, s56, 0x1f
	s_cmp_gt_u32	s47, 1
	s_cbranch_scc1	.scene_other
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_lshr_b32	s52, s61, 24
	s_cmp_eq_u32	s56, 0
	s_cbranch_scc1	.scene_mask
	s_cmp_eq_u32	s52, 0
	s_cbranch_scc1	.scene_loop
.scene_mask:
	s_mov_b32	s45, 1
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
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
.scene_other:
	s_cmp_eq_u32	s47, 4
	s_cbranch_scc1	.scene_image
	s_cmp_eq_u32	s47, 2
	s_cbranch_scc1	.scene_box
	s_cmp_eq_u32	s47, 10
	s_cbranch_scc1	.scene_translate
	s_cmp_eq_u32	s47, 3
	s_cbranch_scc1	.scene_text
	s_cmp_eq_u32	s47, 11
	s_cbranch_scc1	.scene_image
	s_cmp_eq_u32	s47, 5
	s_cbranch_scc1	.scene_mix
	s_cmp_eq_u32	s47, 12
	s_cbranch_scc1	.scene_mix
	s_cmp_eq_u32	s47, 13
	s_cbranch_scc1	.scene_mix
	s_cmp_eq_u32	s47, 6
	s_cbranch_scc1	.scene_close
	s_cmp_eq_u32	s47, 7
	s_cbranch_scc1	.scene_turn
	s_branch	.scene_loop

; one pixel of a mix (memory_mix_u32_row): the lane's pixel \o and the ink \ink at the cover \cv (0 to 255), where the mask \mk has it: none leaves the pixel, all takes the ink
; whole, between the colours (alpha cleared) are g (255 - c) + i c + 127 in sixteen-bit lanes, then (x + (x >> 8) + 1) >> 8
.macro mixpixel o, cv, ink, mk
	v_sub_nc_u32	v39, 255, \cv
	v_and_b32	v40, 0x00ffffff, \o
	v_and_b32	v41, 0x00ffffff, \ink
	v_and_b32	v42, s75, v40
	v_lshrrev_b32	v43, 8, v40
	v_and_b32	v43, s75, v43
	v_and_b32	v44, s75, v41
	v_lshrrev_b32	v45, 8, v41
	v_and_b32	v45, s75, v45
	v_mad_u32_u24	v42, v42, v39, 0x007f007f
	v_mad_u32_u24	v42, v44, \cv, v42
	v_mad_u32_u24	v43, v43, v39, 0x007f007f
	v_mad_u32_u24	v43, v45, \cv, v43
	v_lshrrev_b32	v46, 8, v42
	v_and_b32	v46, s75, v46
	v_add3_u32	v42, v42, 0x00010001, v46
	v_lshrrev_b32	v42, 8, v42
	v_and_b32	v42, s75, v42
	v_lshrrev_b32	v47, 8, v43
	v_and_b32	v47, s75, v47
	v_add3_u32	v43, v43, 0x00010001, v47
	v_lshrrev_b32	v43, 8, v43
	v_and_b32	v43, s75, v43
	v_lshl_or_b32	v42, v43, 8, v42
	v_cmp_lt_u32_e64 s[96:97], 254, \cv
	v_cndmask_b32_e64 v42, v42, \ink, s[96:97]
	v_cmp_ne_u32_e64 s[96:97], 0, \cv
	v_cndmask_b32_e64 v42, \o, v42, s[96:97]
	v_cndmask_b32_e64 \o, \o, v42, \mk
.endm

; one pixel of an exact IEC 61966-2-1 linear-light mix (canvas_mix_linear, op 13): s[98:99] holds GPU_VA + GPU_SRGB_S2L_AT,
; s[100:101] holds GPU_VA + GPU_SRGB_L2S_AT; cv == 0 keeps \o, cv >= 255 takes \ink, else w = cv + (cv >> 7) in 0..256.
.macro mixlinear o, cv, ink, mk
	v_lshrrev_b32	v39, 7, \cv
	v_add_nc_u32	v39, \cv, v39
	v_sub_nc_u32	v40, 256, v39
	v_bfe_u32	v41, \o, 0, 8
	v_bfe_u32	v42, \o, 8, 8
	v_bfe_u32	v43, \o, 16, 8
	v_bfe_u32	v44, \ink, 0, 8
	v_bfe_u32	v45, \ink, 8, 8
	v_bfe_u32	v46, \ink, 16, 8
	v_lshlrev_b32	v41, 2, v41
	v_lshlrev_b32	v42, 2, v42
	v_lshlrev_b32	v43, 2, v43
	v_lshlrev_b32	v44, 2, v44
	v_lshlrev_b32	v45, 2, v45
	v_lshlrev_b32	v46, 2, v46
	global_load_dword v47, v41, s[98:99]
	global_load_dword v48, v42, s[98:99]
	global_load_dword v49, v43, s[98:99]
	global_load_dword v50, v44, s[98:99]
	global_load_dword v51, v45, s[98:99]
	global_load_dword v52, v46, s[98:99]
	s_waitcnt	vmcnt(0)
	v_mad_u32_u24	v47, v47, v40, 128
	v_mad_u32_u24	v47, v50, v39, v47
	v_lshrrev_b32	v47, 8, v47
	v_mad_u32_u24	v48, v48, v40, 128
	v_mad_u32_u24	v48, v51, v39, v48
	v_lshrrev_b32	v48, 8, v48
	v_mad_u32_u24	v49, v49, v40, 128
	v_mad_u32_u24	v49, v52, v39, v49
	v_lshrrev_b32	v49, 8, v49
	global_load_ubyte v41, v47, s[100:101]
	global_load_ubyte v42, v48, s[100:101]
	global_load_ubyte v43, v49, s[100:101]
	s_waitcnt	vmcnt(0)
	v_lshl_or_b32	v41, v42, 8, v41
	v_lshl_or_b32	v41, v43, 16, v41
	v_cmp_lt_u32_e64 s[96:97], 254, \cv
	v_cndmask_b32_e64 v41, v41, \ink, s[96:97]
	v_cmp_ne_u32_e64 s[96:97], 0, \cv
	v_cndmask_b32_e64 v41, \o, v41, s[96:97]
	v_cndmask_b32_e64 \o, \o, v41, \mk
.endm

; \o = compositor_over_weighted(\o, \ink, \cv) where \mk has it: the premultiplied ink's four channels each times the cover, (c * cv + 127 + 1 + that >> 8) >> 8,
; the ink as it is for a cover of 255 or more, then laid over \o as overv lays it; \o as it was for a cover of 0 or an ink of alpha 0
.macro wover o, cv, ink, mk
	v_and_b32	v44, s75, \ink
	v_mad_u32_u24	v44, v44, \cv, 0x007f007f
	v_lshrrev_b32	v46, 8, v44
	v_and_b32	v46, s75, v46
	v_add3_u32	v44, v44, 0x00010001, v46
	v_lshrrev_b32	v44, 8, v44
	v_and_b32	v44, s75, v44
	v_lshrrev_b32	v45, 8, \ink
	v_and_b32	v45, s75, v45
	v_mad_u32_u24	v45, v45, \cv, 0x007f007f
	v_lshrrev_b32	v46, 8, v45
	v_and_b32	v46, s75, v46
	v_add3_u32	v45, v45, 0x00010001, v46
	v_and_b32	v45, 0xff00ff00, v45
	v_lshl_or_b32	v44, v45, 0, v44
	v_cmp_lt_u32_e64 s[96:97], 254, \cv
	v_cndmask_b32_e64 v44, v44, \ink, s[96:97]
	overv		v42, \o, v44
	v_cmp_ne_u32_e64 s[96:97], 0, \cv
	v_cmp_lt_u32_e64 s[98:99], 0x00ffffff, \ink
	s_and_b64	s[96:97], s[96:97], s[98:99]
	s_and_b64	s[96:97], s[96:97], \mk
	v_cndmask_b32_e64 \o, \o, v42, s[96:97]
.endm

; one load of a turned picture: the pixel \k places along the row from the lane's first (its index in v30 and the step in s80), into \out
.macro turnload out, k
	s_mul_i32	s95, s80, \k
	v_add_nc_u32	v38, s95, v30
	v_lshlrev_b32	v38, 2, v38
	v_ashrrev_i32	v40, 31, v38
	v_add_co_u32	v38, vcc, s61, v38
	v_mov_b32	v39, s62
	v_add_co_ci_u32	v39, vcc, v39, v40, vcc
	global_load_dword \out, v[38:39], off
.endm

; one pixel of a close button's halo (compositor_close_draw): the window's own pixel \under (not the picture's under it) mixed towards window_mix (under, tone, 140) by the halo \cv
; (0 to 255), where the mask \mk has it and the halo is not none, the result without alpha (the tone's parts in s98 and s99: the tone's red and blue, its green, times 140 and 128
; in both sixteen-bit lanes)
.macro closepixel o, cv, under, mk
	v_and_b32	v48, 0x00ffffff, \under
	v_and_b32	v49, s75, v48
	v_lshrrev_b32	v50, 8, v48
	v_and_b32	v50, s75, v50
	v_mad_u32_u24	v49, v49, 116, s98
	v_lshrrev_b32	v49, 8, v49
	v_and_b32	v49, s75, v49
	v_mad_u32_u24	v50, v50, 116, s99
	v_lshrrev_b32	v50, 8, v50
	v_and_b32	v50, s75, v50
	v_lshl_or_b32	v51, v50, 8, v49
	mixpixel	v48, \cv, v51, \mk
	v_cmp_ne_u32_e64 s[90:91], 0, \cv
	s_and_b64	s[90:91], s[90:91], \mk
	v_cndmask_b32_e64 \o, \o, v48, s[90:91]
.endm

; a turned picture (7 | the step along a row of the rectangle plus 0x800000 << 8 (in pixels), x0, y0, x1, y1 in pixels, the address of the source's pixel for x0, y0 in two words, the step down
; a row (signed, in pixels)): the pixel at x, y is the source's at base + (x - x0) * the first step + (y - y0) * the second, which is a copy of a rectangle turned a quarter,
; a half or three quarters (or not turned: 1 and the pitch); each of the lane's four pixels has an address of its own
.scene_turn:
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	s_lshr_b32	s80, s56, 8
	s_sub_i32	s80, s80, 0x800000
	v_subrev_nc_u32	v30, s58, v4
	v_mul_lo_u32	v30, v30, s63
	v_subrev_nc_u32	v31, s57, v3
	v_mul_lo_u32	v31, v31, s80
	v_add_nc_u32	v30, v30, v31
	s_mov_b64	s[92:93], exec
	s_and_b64	exec, s[92:93], s[82:83]
	turnload	v34, 0
	s_and_b64	exec, s[92:93], s[84:85]
	turnload	v35, 1
	s_and_b64	exec, s[92:93], s[86:87]
	turnload	v36, 2
	s_and_b64	exec, s[92:93], s[88:89]
	turnload	v37, 3
	s_mov_b64	exec, s[92:93]
	s_waitcnt	vmcnt(0)
	v_cndmask_b32_e64 v5, v5, v34, s[82:83]
	v_cndmask_b32_e64 v6, v6, v35, s[84:85]
	v_cndmask_b32_e64 v7, v7, v36, s[86:87]
	v_cndmask_b32_e64 v8, v8, v37, s[88:89]
	s_branch	.scene_loop

; a close button's halo (6 | the button's side << 8 | the picture's pitch << 16, x0, y0, x1, y1 the button in pixels, the place from the buffer's start of the side by side
; bytes of the halo's covers, the picture's address at x0, y0 in two words: the first and the high byte of the second, and the tone in the rest of it)
.scene_close:
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	s_bfe_u32	s80, s56, 0x80008
	s_lshr_b32	s81, s56, 16
	s_lshr_b32	s100, s63, 8
	s_and_b32	s101, s63, 0xff
	s_and_b32	s98, s100, s75
	s_mul_i32	s98, s98, 140
	s_add_u32	s98, s98, 0x00800080
	s_lshr_b32	s99, s100, 8
	s_and_b32	s99, s99, s75
	s_mul_i32	s99, s99, 140
	s_add_u32	s99, s99, 0x00800080
	v_subrev_nc_u32	v30, s58, v4
	v_mad_u32_u24	v31, v30, s80, s61
	v_subrev_nc_u32	v32, s57, v3
	v_add_nc_u32	v31, v31, v32
	s_mov_b64	s[92:93], exec
	s_or_b64	s[90:91], s[82:83], s[84:85]
	s_or_b64	s[90:91], s[90:91], s[86:87]
	s_or_b64	s[90:91], s[90:91], s[88:89]
	s_and_b64	exec, s[92:93], s[90:91]
	global_load_dword v34, v31, s[32:33]
	s_mov_b64	exec, s[92:93]
	v_mul_lo_u32	v35, v30, s81
	v_add_nc_u32	v35, v35, v32
	v_lshlrev_b32	v35, 2, v35
	v_ashrrev_i32	v36, 31, v35
	v_add_co_u32	v32, vcc, s62, v35
	v_mov_b32	v33, s101
	v_add_co_ci_u32	v33, vcc, v33, v36, vcc
	s_and_b64	exec, s[92:93], s[82:83]
	global_load_dword v24, v[32:33], off
	s_and_b64	exec, s[92:93], s[84:85]
	global_load_dword v25, v[32:33], off offset:4
	s_and_b64	exec, s[92:93], s[86:87]
	global_load_dword v26, v[32:33], off offset:8
	s_and_b64	exec, s[92:93], s[88:89]
	global_load_dword v27, v[32:33], off offset:12
	s_mov_b64	exec, s[92:93]
	s_waitcnt	vmcnt(0)
	v_bfe_u32	v20, v34, 0, 8
	v_bfe_u32	v21, v34, 8, 8
	v_bfe_u32	v22, v34, 16, 8
	v_bfe_u32	v23, v34, 24, 8
	closepixel	v5, v20, v24, s[82:83]
	closepixel	v6, v21, v25, s[84:85]
	closepixel	v7, v22, v26, s[86:87]
	closepixel	v8, v23, v27, s[88:89]
	s_branch	.scene_loop

; a mix (5 | the cover's pitch << 8 | the ink's pitch << 16, x0, y0, x1, y1 in pixels, the place of the cover byte at x0, y0 from the buffer's start, the ink's address in
; two words, or the ink's colour in the first when the ink's pitch is 0): memory_mix_u32_row over the rectangle, the covers a table of bytes in the buffer;
; 12 with the same words is the ink laid over by its cover instead (compositor_over_weighted: a premultiplied picture in a round corner). 0x80 on the kind is
; an ink picture with red and blue the other way round, swapped as it is loaded (as the image's is)
.scene_mix:
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	s_bfe_u32	s80, s56, 0x80008
	s_lshr_b32	s81, s56, 16
	v_subrev_nc_u32	v30, s58, v4
	v_mad_u32_u24	v30, v30, s80, s61
	v_subrev_nc_u32	v31, s57, v3
	v_add_nc_u32	v30, v30, v31
	s_mov_b64	s[92:93], exec
	s_or_b64	s[90:91], s[82:83], s[84:85]
	s_or_b64	s[90:91], s[90:91], s[86:87]
	s_or_b64	s[90:91], s[90:91], s[88:89]
	s_and_b64	exec, s[92:93], s[90:91]
	global_load_dword v34, v30, s[32:33]
	s_mov_b64	exec, s[92:93]
	s_bitcmp1_b32	s56, 6
	s_cbranch_scc1	.mix_tiled
	s_cmp_eq_u32	s81, 0
	s_cbranch_scc1	.mix_constant
	v_subrev_nc_u32	v35, s58, v4
	v_mul_lo_u32	v35, v35, s81
	v_add_nc_u32	v35, v35, v31
	v_lshlrev_b32	v35, 2, v35
	v_ashrrev_i32	v36, 31, v35
	v_add_co_u32	v32, vcc, s62, v35
	v_mov_b32	v33, s63
	v_add_co_ci_u32	v33, vcc, v33, v36, vcc
	s_and_b64	s[94:95], s[82:83], s[88:89]
	s_and_b64	exec, s[92:93], s[94:95]
	s_cbranch_execz	.mix_edge
	global_load_dwordx4 v[24:27], v[32:33], off
.mix_edge:
	s_andn2_b64	s[94:95], s[90:91], s[94:95]
	s_and_b64	exec, s[92:93], s[94:95]
	s_cbranch_execz	.mix_linear_done
	s_waitcnt	vmcnt(0)
	s_and_b64	s[96:97], s[92:93], s[94:95]
	s_and_b64	exec, s[96:97], s[82:83]
	global_load_dword v24, v[32:33], off
	s_and_b64	exec, s[96:97], s[84:85]
	global_load_dword v25, v[32:33], off offset:4
	s_and_b64	exec, s[96:97], s[86:87]
	global_load_dword v26, v[32:33], off offset:8
	s_and_b64	exec, s[96:97], s[88:89]
	global_load_dword v27, v[32:33], off offset:12
.mix_linear_done:
	s_mov_b64	exec, s[92:93]
	s_branch	.mix_blend
.mix_tiled:
	s_add_u32	s50, s32, s62
	s_addc_u32	s51, s33, 0
	s_load_dwordx8	s[64:71], s[50:51], 0x0
	s_and_b32	s50, s63, 0xffff
	s_lshr_b32	s51, s63, 16
	v_add_nc_u32	v30, s50, v31
	v_subrev_nc_u32	v31, s58, v4
	v_add_nc_u32	v31, s51, v31
	v_add_nc_u32	v32, 1, v30
	v_mov_b32	v33, v31
	v_add_nc_u32	v38, 2, v30
	v_mov_b32	v39, v31
	v_add_nc_u32	v40, 3, v30
	v_mov_b32	v41, v31
	s_waitcnt	lgkmcnt(0)
	s_bitcmp1_b32	s56, 5
	s_cbranch_scc1	.mix_tiled_unorm
	s_and_b64	exec, s[92:93], s[82:83]
	image_load	v24, v[30:31], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[84:85]
	image_load	v25, v[32:33], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[86:87]
	image_load	v26, v[38:39], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[88:89]
	image_load	v27, v[40:41], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_mov_b64	exec, s[92:93]
	s_branch	.mix_blend
.mix_tiled_unorm:
	s_and_b64	exec, s[92:93], s[82:83]
	image_load	v[44:47], v[30:31], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[84:85]
	image_load	v[48:51], v[32:33], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[86:87]
	image_load	v[52:55], v[38:39], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[88:89]
	image_load	v[56:59], v[40:41], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_mov_b64	exec, s[92:93]
	s_waitcnt	vmcnt(0)
	packunorm	v24, v44, v45, v46, v47
	packunorm	v25, v48, v49, v50, v51
	packunorm	v26, v52, v53, v54, v55
	packunorm	v27, v56, v57, v58, v59
	s_branch	.mix_ordered
.mix_constant:
	v_mov_b32	v24, s62
	v_mov_b32	v25, s62
	v_mov_b32	v26, s62
	v_mov_b32	v27, s62
.mix_blend:
	s_waitcnt	vmcnt(0)
	s_and_b32	s50, s56, 0x80
	s_cmp_eq_u32	s50, 0
	s_cbranch_scc1	.mix_ordered
	s_mov_b32	s50, 0x3000102
	v_perm_b32	v24, v24, v24, s50
	v_perm_b32	v25, v25, v25, s50
	v_perm_b32	v26, v26, v26, s50
	v_perm_b32	v27, v27, v27, s50
.mix_ordered:
	v_bfe_u32	v28, v34, 0, 8
	v_bfe_u32	v29, v34, 8, 8
	v_bfe_u32	v30, v34, 16, 8
	v_bfe_u32	v31, v34, 24, 8
	s_cmp_eq_u32	s47, 12
	s_cbranch_scc1	.mix_over
	s_cmp_eq_u32	s47, 13
	s_cbranch_scc1	.mix_linear
	mixpixel	v5, v28, v24, s[82:83]
	mixpixel	v6, v29, v25, s[84:85]
	mixpixel	v7, v30, v26, s[86:87]
	mixpixel	v8, v31, v27, s[88:89]
	s_branch	.scene_loop
.mix_linear:
	s_add_u32	s98, s32, 59392
	s_addc_u32	s99, s33, 0
	s_add_u32	s100, s32, 60416
	s_addc_u32	s101, s33, 0
	mixlinear	v5, v28, v24, s[82:83]
	mixlinear	v6, v29, v25, s[84:85]
	mixlinear	v7, v30, v26, s[86:87]
	mixlinear	v8, v31, v27, s[88:89]
	s_branch	.scene_loop
.mix_over:
	wover		v5, v28, v24, s[82:83]
	wover		v6, v29, v25, s[84:85]
	wover		v7, v30, v26, s[86:87]
	wover		v8, v31, v27, s[88:89]
	s_branch	.scene_loop

; an image (4 copied, 11 premultiplied alpha-over blended, x0, y0, x1, y1 in pixels, the address of the picture's pixel at x0, y0 in two words, its pitch in pixels):
; each lane's four loaded by the address and no further than the rectangle and the rows (a lane outside them loads nothing). 0x80 on the kind is a picture
; whose red and blue are the other way round (a client's ABGR or XBGR buffer read in place): its bytes 0 and 2 are swapped as they are loaded, before any blend
.scene_image:
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	s_bitcmp1_b32	s56, 6
	s_cbranch_scc1	.image_tiled
	v_subrev_nc_u32	v30, s58, v4
	v_mul_lo_u32	v30, v30, s63
	v_subrev_nc_u32	v31, s57, v3
	v_add_nc_u32	v30, v30, v31
	v_lshlrev_b32	v30, 2, v30
	v_ashrrev_i32	v31, 31, v30
	v_add_co_u32	v32, vcc, s61, v30
	v_mov_b32	v33, s62
	v_add_co_ci_u32	v33, vcc, v33, v31, vcc
	s_mov_b64	s[92:93], exec
	s_and_b64	s[90:91], s[82:83], s[88:89]
	s_and_b64	exec, s[92:93], s[90:91]
	s_cbranch_execz	.image_edge
	global_load_dwordx4 v[34:37], v[32:33], off
.image_edge:
	s_or_b64	s[94:95], s[82:83], s[84:85]
	s_or_b64	s[94:95], s[94:95], s[86:87]
	s_or_b64	s[94:95], s[94:95], s[88:89]
	s_andn2_b64	s[94:95], s[94:95], s[90:91]
	s_and_b64	exec, s[92:93], s[94:95]
	s_cbranch_execz	.image_linear_wait
	s_waitcnt	vmcnt(0)
	s_and_b64	s[96:97], s[92:93], s[94:95]
	s_and_b64	exec, s[96:97], s[82:83]
	global_load_dword v34, v[32:33], off
	s_and_b64	exec, s[96:97], s[84:85]
	global_load_dword v35, v[32:33], off offset:4
	s_and_b64	exec, s[96:97], s[86:87]
	global_load_dword v36, v[32:33], off offset:8
	s_and_b64	exec, s[96:97], s[88:89]
	global_load_dword v37, v[32:33], off offset:12
.image_linear_wait:
	s_mov_b64	exec, s[92:93]
	s_waitcnt	vmcnt(0)
	s_branch	.image_loaded
.image_tiled:
	s_add_u32	s50, s32, s61
	s_addc_u32	s51, s33, 0
	s_load_dwordx8	s[64:71], s[50:51], 0x0
	s_and_b32	s50, s62, 0xffff
	s_lshr_b32	s51, s62, 16
	v_subrev_nc_u32	v30, s57, v3
	v_add_nc_u32	v30, s50, v30
	v_subrev_nc_u32	v31, s58, v4
	v_add_nc_u32	v31, s51, v31
	v_add_nc_u32	v32, 1, v30
	v_mov_b32	v33, v31
	v_add_nc_u32	v38, 2, v30
	v_mov_b32	v39, v31
	v_add_nc_u32	v40, 3, v30
	v_mov_b32	v41, v31
	s_mov_b64	s[92:93], exec
	s_waitcnt	lgkmcnt(0)
	s_bitcmp1_b32	s56, 5
	s_cbranch_scc1	.image_tiled_unorm
	s_and_b64	exec, s[92:93], s[82:83]
	image_load	v34, v[30:31], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[84:85]
	image_load	v35, v[32:33], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[86:87]
	image_load	v36, v[38:39], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[88:89]
	image_load	v37, v[40:41], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_mov_b64	exec, s[92:93]
	s_waitcnt	vmcnt(0)
	s_branch	.image_loaded
.image_tiled_unorm:
	s_and_b64	exec, s[92:93], s[82:83]
	image_load	v[44:47], v[30:31], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[84:85]
	image_load	v[48:51], v[32:33], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[86:87]
	image_load	v[52:55], v[38:39], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_and_b64	exec, s[92:93], s[88:89]
	image_load	v[56:59], v[40:41], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_mov_b64	exec, s[92:93]
	s_waitcnt	vmcnt(0)
	packunorm	v34, v44, v45, v46, v47
	packunorm	v35, v48, v49, v50, v51
	packunorm	v36, v52, v53, v54, v55
	packunorm	v37, v56, v57, v58, v59
	s_branch	.image_ordered
.image_loaded:
	s_and_b32	s50, s56, 0x80
	s_cmp_eq_u32	s50, 0
	s_cbranch_scc1	.image_ordered
	s_mov_b32	s50, 0x3000102
	v_perm_b32	v34, v34, v34, s50
	v_perm_b32	v35, v35, v35, s50
	v_perm_b32	v36, v36, v36, s50
	v_perm_b32	v37, v37, v37, s50
.image_ordered:
	s_cmp_eq_u32	s47, 4
	s_cbranch_scc1	.image_store
	overv		v34, v5, v34
	overv		v35, v6, v35
	overv		v36, v7, v36
	overv		v37, v8, v37
.image_store:
	v_cndmask_b32_e64 v5, v5, v34, s[82:83]
	v_cndmask_b32_e64 v6, v6, v35, s[84:85]
	v_cndmask_b32_e64 v7, v7, v36, s[86:87]
	v_cndmask_b32_e64 v8, v8, v37, s[88:89]
	s_branch	.scene_loop

; a text run (3 | the pitch << 8, x0, y0, x1, y1, the ink, alpha | stroke << 8 | a base's number << 16, the bitmap's place from that base: the buffer's start for none, else
; the 64-bit address at that number in the table the arguments name at 0x40): the cover of each pixel is
; a byte of the bitmap, with a border of nothing round it; each lane reads its four bytes and the ones beside and above and below by unaligned dwords
.scene_text:
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
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
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	s_or_b64	s[90:91], s[82:83], s[84:85]
	s_or_b64	s[90:91], s[90:91], s[86:87]
	s_or_b64	s[90:91], s[90:91], s[88:89]
	s_and_saveexec_b64 s[92:93], s[90:91]
	s_cbranch_execz	.text_done
	s_mov_b64	s[98:99], s[32:33]
	s_bfe_u32	s100, s62, 0x80010
	s_cmp_eq_u32	s100, 0
	s_cbranch_scc1	.text_based
	s_load_dwordx2	s[98:99], s[4:5], 0x40
	s_lshl_b32	s100, s100, 3
	s_waitcnt	lgkmcnt(0)
	s_add_u32	s98, s98, s100
	s_addc_u32	s99, s99, 0
	s_load_dwordx2	s[98:99], s[98:99], 0x0
	s_waitcnt	lgkmcnt(0)
.text_based:
	s_add_u32	s100, s98, s63
	s_addc_u32	s101, s99, 0
	v_subrev_nc_u32	v30, s58, v4
	v_mul_lo_u32	v30, v30, s81
	v_subrev_nc_u32	v31, s57, v3
	v_add_nc_u32	v30, v30, v31
	v_ashrrev_i32	v31, 31, v30
	v_add_co_u32	v38, vcc, s100, v30
	v_mov_b32	v39, s101
	v_add_co_ci_u32	v39, vcc, v39, v31, vcc
	v_subrev_nc_u32	v32, s81, v30
	v_ashrrev_i32	v33, 31, v32
	v_add_co_u32	v40, vcc, s100, v32
	v_mov_b32	v41, s101
	v_add_co_ci_u32	v41, vcc, v41, v33, vcc
	v_add_nc_u32	v32, s81, v30
	v_ashrrev_i32	v33, 31, v32
	v_add_co_u32	v42, vcc, s100, v32
	v_mov_b32	v43, s101
	v_add_co_ci_u32	v43, vcc, v43, v33, vcc
	global_load_dword v34, v[38:39], off offset:-1
	global_load_dword v35, v[38:39], off offset:3
	global_load_dword v36, v[40:41], off
	global_load_dword v37, v[42:43], off
	s_waitcnt	vmcnt(0)
	v_bfe_u32	v28, v34, 0, 8
	v_bfe_u32	v24, v34, 8, 8
	v_bfe_u32	v25, v34, 16, 8
	v_bfe_u32	v26, v34, 24, 8
	v_bfe_u32	v27, v35, 0, 8
	v_bfe_u32	v29, v35, 8, 8
	v_bfe_u32	v30, v36, 0, 8
	v_bfe_u32	v31, v36, 8, 8
	v_bfe_u32	v32, v36, 16, 8
	v_bfe_u32	v33, v36, 24, 8
	v_bfe_u32	v42, v37, 0, 8
	v_bfe_u32	v43, v37, 8, 8
	v_bfe_u32	v44, v37, 16, 8
	v_bfe_u32	v45, v37, 24, 8
	textpixel	v5, v24, v28, v25, v30, v42, s[82:83]
	textpixel	v6, v25, v24, v26, v31, v43, s[84:85]
	textpixel	v7, v26, v25, v27, v32, v44, s[86:87]
	textpixel	v8, v27, v26, v29, v33, v45, s[88:89]
.text_done:
	s_mov_b64	exec, s[92:93]
	s_branch	.scene_loop

;
; a glyph translated (10 | the coverage's pitch << 8, x0, y0, x1, y1, the tones' place, the base's number << 16, the coverage's place): font_cell's cell on its ground:
; the pixel at x, y is the tone at index the coverage byte c at (y - y0) * pitch + (x - x0) from the coverage's place, the tones being 256 words at their place, both from the
; base the number names (the buffer's start for none), which is a program's list when the number is the compositor's for it. No blend: memory_translate_u32_rect's translate,
; the pixels of the cell's ground having been laid by a fill before it. Each lane reads four coverage bytes by a dword, as the text run does, and four tones.
.scene_translate:
	s_cmp_le_i32	s59, s28
	s_cbranch_scc1	.scene_loop
	s_add_u32	s50, s28, 64
	s_cmp_ge_i32	s57, s50
	s_cbranch_scc1	.scene_loop
	s_cmp_le_i32	s60, s29
	s_cbranch_scc1	.scene_loop
	s_add_u32	s51, s29, 4
	s_cmp_ge_i32	s58, s51
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_lshr_b32	s81, s56, 8
	s_min_i32	s53, s59, s12
	v_cmp_le_i32_e64 s[76:77], s58, v4
	v_cmp_gt_i32	vcc, s60, v4
	s_and_b64	s[76:77], s[76:77], vcc
	inside		s[82:83], v3
	inside		s[84:85], v17
	inside		s[86:87], v18
	inside		s[88:89], v19
	s_or_b64	s[90:91], s[82:83], s[84:85]
	s_or_b64	s[90:91], s[90:91], s[86:87]
	s_or_b64	s[90:91], s[90:91], s[88:89]
	s_and_saveexec_b64 s[92:93], s[90:91]
	s_cbranch_execz	.translate_done
	s_mov_b64	s[98:99], s[32:33]
	s_bfe_u32	s100, s62, 0x80010
	s_cmp_eq_u32	s100, 0
	s_cbranch_scc1	.translate_based
	s_load_dwordx2	s[98:99], s[4:5], 0x40
	s_lshl_b32	s100, s100, 3
	s_waitcnt	lgkmcnt(0)
	s_add_u32	s98, s98, s100
	s_addc_u32	s99, s99, 0
	s_load_dwordx2	s[98:99], s[98:99], 0x0
	s_waitcnt	lgkmcnt(0)
.translate_based:
	s_add_u32	s100, s98, s63
	s_addc_u32	s101, s99, 0
	v_subrev_nc_u32	v30, s58, v4
	v_mul_lo_u32	v30, v30, s81
	v_subrev_nc_u32	v31, s57, v3
	v_add_nc_u32	v30, v30, v31
	v_ashrrev_i32	v31, 31, v30
	v_add_co_u32	v38, vcc, s100, v30
	v_mov_b32	v39, s101
	v_add_co_ci_u32	v39, vcc, v39, v31, vcc
	s_mov_b64	s[90:91], exec
	s_and_b64	exec, s[90:91], s[82:83]
	global_load_ubyte v24, v[38:39], off
	s_and_b64	exec, s[90:91], s[84:85]
	global_load_ubyte v25, v[38:39], off offset:1
	s_and_b64	exec, s[90:91], s[86:87]
	global_load_ubyte v26, v[38:39], off offset:2
	s_and_b64	exec, s[90:91], s[88:89]
	global_load_ubyte v27, v[38:39], off offset:3
	s_mov_b64	exec, s[90:91]
	s_waitcnt	vmcnt(0)
	s_add_u32	s100, s98, s61
	s_addc_u32	s101, s99, 0
	v_lshlrev_b32	v24, 2, v24
	v_lshlrev_b32	v25, 2, v25
	v_lshlrev_b32	v26, 2, v26
	v_lshlrev_b32	v27, 2, v27
	s_and_b64	exec, s[90:91], s[82:83]
	global_load_dword v28, v24, s[100:101]
	s_and_b64	exec, s[90:91], s[84:85]
	global_load_dword v29, v25, s[100:101]
	s_and_b64	exec, s[90:91], s[86:87]
	global_load_dword v30, v26, s[100:101]
	s_and_b64	exec, s[90:91], s[88:89]
	global_load_dword v31, v27, s[100:101]
	s_mov_b64	exec, s[90:91]
	s_waitcnt	vmcnt(0)
	v_cndmask_b32_e64 v5, v5, v28, s[82:83]
	v_cndmask_b32_e64 v6, v6, v29, s[84:85]
	v_cndmask_b32_e64 v7, v7, v30, s[86:87]
	v_cndmask_b32_e64 v8, v8, v31, s[88:89]
.translate_done:
	s_mov_b64	exec, s[92:93]
	s_branch	.scene_loop

; a box (256ths: x0 s57, y0 s58, x1 s59, y1 s60, the colour s61, rx s62, ry s63)
.scene_box:
	s_bfe_u32	s35, s56, 0x40008
	s_add_u32	s44, s44, s35
	s_mov_b32	s34, 0
	s_lshl_b32	s78, s28, 8
	s_lshl_b32	s79, s29, 8
	s_lshl_b32	s80, s40, 8
	s_lshl_b32	s81, s41, 8
; the record in s[56:63] (the box, then each of its changes of cover: s34 is the one in hand, s35 how many) against the patch
.box_enter:
	s_cmp_le_i32	s59, s57
	s_cbranch_scc1	.box_miss
	s_cmp_le_i32	s60, s58
	s_cbranch_scc1	.box_miss
	s_cmp_ge_i32	s78, s59
	s_cbranch_scc1	.box_miss
	s_cmp_le_i32	s80, s57
	s_cbranch_scc1	.box_miss
	s_cmp_ge_i32	s79, s60
	s_cbranch_scc1	.box_miss
	s_cmp_le_i32	s81, s58
	s_cbranch_scc1	.box_miss
	s_add_i32	s86, s58, s63
	s_sub_i32	s87, s60, s63
	s_cmp_lg_u32	s34, 0
	s_cbranch_scc1	.box_edge
	s_lshr_b32	s52, s61, 24
	s_cmp_eq_u32	s52, 0
	s_cbranch_scc1	.scene_loop
	s_mov_b32	s45, 1
	s_and_b32	s64, s61, 255
	s_bfe_u32	s65, s61, 0x80008
	s_bfe_u32	s66, s61, 0x80010
	s_cmp_lg_u32	s35, 0
	s_cbranch_scc1	.box_edge
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
	v_add_nc_u32	v34, 0x100, v28
	v_add_nc_u32	v35, 0x100, v29
	v_add_nc_u32	v36, 0x100, v30
	v_add_nc_u32	v37, 0x100, v31
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
	v_mad_u64_u32	v[52:53], s[36:37], v48, s63, 0
	v_cmp_gt_u64_e64 s[98:99], v[52:53], v[50:51]
	v_subrev_nc_u32	v54, 1, v48
	v_cndmask_b32_e64 v48, v48, v54, s[98:99]
	v_add_nc_u32	v54, 1, v48
	v_mad_u64_u32	v[52:53], s[36:37], v54, s63, 0
	v_cmp_le_u64_e64 s[98:99], v[52:53], v[50:51]
	v_cndmask_b32_e64 v48, v48, v54, s[98:99]
	v_mul_lo_u32	v49, v48, v48
	v_sub_nc_u32	v49, 0, v49
	v_cvt_f32_u32	v54, v49
	v_sqrt_f32	v54, v54
	v_cvt_u32_f32	v54, v54
	v_mov_b32	v56, v49
	v_mov_b32	v57, 0
	v_mad_u64_u32	v[58:59], s[36:37], v54, v54, 0
	v_cmp_gt_u64_e64 s[98:99], v[58:59], v[56:57]
	v_subrev_nc_u32	v60, 1, v54
	v_cndmask_b32_e64 v54, v54, v60, s[98:99]
	v_add_nc_u32	v60, 1, v54
	v_mad_u64_u32	v[58:59], s[36:37], v60, v60, 0
	v_cmp_le_u64_e64 s[98:99], v[58:59], v[56:57]
	v_cndmask_b32_e64 v54, v54, v60, s[98:99]
	v_mad_u64_u32	v[58:59], s[36:37], v54, s62, 0
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
	accumulate	v24, v28, v34
	accumulate	v25, v29, v35
	accumulate	v26, v30, v36
	accumulate	v27, v31, v37
	.endr
.box_corner_done:
	s_mov_b64	exec, s[92:93]
	v_cndmask_b32_e64 v24, 0, v24, s[82:83]
	v_cndmask_b32_e64 v25, 0, v25, s[82:83]
	v_cndmask_b32_e64 v26, 0, v26, s[82:83]
	v_cndmask_b32_e64 v27, 0, v27, s[82:83]
	s_cmp_lg_u32	s34, 0
	s_cbranch_scc1	.box_combine
	v_mov_b32	v12, v24
	v_mov_b32	v13, v25
	v_mov_b32	v14, v26
	v_mov_b32	v15, v27
; the next change of the box's cover, if it has one: its record, then its cover as the box's was
.box_next:
	s_cmp_ge_u32	s34, s35
	s_cbranch_scc1	.box_blend
	s_add_u32	s34, s34, 1
	s_lshl_b32	s46, s34, 5
	s_add_u32	s2, s48, s46
	s_addc_u32	s3, s49, 0
	s_load_dwordx8	s[56:63], s[2:3], 0x0
	s_waitcnt	lgkmcnt(0)
	s_branch	.box_enter
; a change that misses the patch: a hole takes nothing from the cover, a clip leaves none
.box_miss:
	s_cmp_eq_u32	s34, 0
	s_cbranch_scc1	.scene_loop
	s_bitcmp1_b32	s56, 0
	s_cbranch_scc1	.scene_loop
	s_branch	.box_next
; kind 8 cuts a hole (the cover less its own, none below none), kind 9 a clip (the product of the two, over a whole pixel's 4096)
.box_combine:
	s_bitcmp1_b32	s56, 0
	s_cbranch_scc1	.box_clip
	v_sub_nc_u32	v12, v12, v24
	v_sub_nc_u32	v13, v13, v25
	v_sub_nc_u32	v14, v14, v26
	v_sub_nc_u32	v15, v15, v27
	v_max_i32	v12, 0, v12
	v_max_i32	v13, 0, v13
	v_max_i32	v14, 0, v14
	v_max_i32	v15, 0, v15
	s_branch	.box_next
.box_clip:
	v_mul_u32_u24	v12, v12, v24
	v_mul_u32_u24	v13, v13, v25
	v_mul_u32_u24	v14, v14, v26
	v_mul_u32_u24	v15, v15, v27
	v_lshrrev_b32	v12, 12, v12
	v_lshrrev_b32	v13, 12, v13
	v_lshrrev_b32	v14, 12, v14
	v_lshrrev_b32	v15, 12, v15
	s_branch	.box_next
.box_blend:
	s_add_u32	s98, s32, 59392
	s_addc_u32	s99, s33, 0
	s_add_u32	s100, s32, 60416
	s_addc_u32	s101, s33, 0
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
	s_sub_u32	s72, 255, s52
	s_and_b32	s73, s71, s75
	s_lshr_b32	s74, s71, 8
	s_and_b32	s74, s74, s75
	boxpixel	v5, v12
	boxpixel	v6, v13
	boxpixel	v7, v14
	boxpixel	v8, v15
	s_branch	.scene_loop

.scene_store:
	s_cmp_eq_u32	s45, 0
	s_cbranch_scc1	.scene_done
	s_and_b64	exec, exec, s[30:31]
	global_store_dwordx4 v10, v[5:8], s[8:9]
.scene_done:
	s_endpgm
.size scene, .-scene

; op 7: a picture scaled, nearest, by tables: d[y * dp + x] = s[ytab[y] * sp + xtab[x]] for x below w; xtab (w entries) is at g and ytab (the rows) follows it
; (the tables are made by the processor from the sizes, so that no division is done here: the source cell whose centre is the point's, (2 * x + 1) * source / (2 * width))
.globl scale
.p2align 8
.type scale,@function
scale:
	s_load_dwordx4	s[8:11], s[4:5], 0x0
	s_load_dwordx2	s[12:13], s[4:5], 0x10
	s_load_dwordx2	s[14:15], s[4:5], 0x18
	s_load_dword	s16, s[4:5], 0x24
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.scale_done
	s_lshl_b32	s17, s16, 2
	s_add_u32	s18, s12, s17
	s_addc_u32	s19, s13, 0
	s_lshl_b32	s20, s7, 2
	s_add_u32	s18, s18, s20
	s_addc_u32	s19, s19, 0
	s_load_dword	s21, s[18:19], 0x0
	v_lshlrev_b32	v2, 2, v1
	global_load_dword v3, v2, s[12:13]
	s_waitcnt	lgkmcnt(0)
	s_waitcnt	vmcnt(0)
	s_mul_i32	s22, s21, s14
	v_add_nc_u32	v3, s22, v3
	v_lshlrev_b32	v3, 2, v3
	global_load_dword v4, v3, s[8:9]
	s_mul_i32	s23, s7, s15
	v_add_nc_u32	v5, s23, v1
	v_lshlrev_b32	v5, 2, v5
	s_waitcnt	vmcnt(0)
	global_store_dword v5, v4, s[10:11]
.scale_done:
	s_endpgm
.size scale, .-scale

; op 8: a picture read through the texture unit, d[y * dp + x] = image(x0 + x, y0 + y) for x below w: the image's resource (its eight words at 0x80 of the
; arguments, gpu_amd_image) says its layout, so a client's tiled or compressed (DCC) buffer is read as the card's own sampler reads it, and written out linear.
; v (0x2c) is the kind: bit 1 a resource of the format 32_UINT, the pixel's word read as it is in one channel (a picture without DCC), and bit 2 then its
; red and blue the other way (bytes 0 and 2 swapped); without bit 1 the resource is 8_8_8_8 UNORM (DCC, whose fast clears are encoded by the format's
; channels): the four channels come back as floats of the bytes over 255, each made a byte again by 255.5 and a truncation (b + b / 510 is in
; [b, b + 1) for every byte). Bit 0 lays the picture over d as a premultiplied picture is laid (overv, the scene's image over), instead of copying it.
.globl image
.p2align 8
.type image,@function
image:
	s_load_dwordx2	s[10:11], s[4:5], 0x8
	s_load_dword	s15, s[4:5], 0x1c
	s_load_dword	s16, s[4:5], 0x24
	s_load_dwordx2	s[12:13], s[4:5], 0x30
	s_load_dword	s20, s[4:5], 0x2c
	s_load_dwordx8	s[24:31], s[4:5], 0x80
	s_load_dwordx2	s[32:33], s[4:5], 0x10
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.unit_done
	v_add_nc_u32	v4, s12, v1
	s_add_u32	s17, s13, s7
	v_mov_b32	v5, s17
	s_mul_i32	s19, s7, s15
	v_add_nc_u32	v3, s19, v1
	v_lshlrev_b32	v3, 2, v3
	; bit 3 of the kind: the picture is scaled. The coordinates are the tables at g, as op 7's (w entries of the source's columns, then the rows' source rows),
	; relative to the resource's (x0, y0): image(x0 + xtab[x], y0 + ytab[row]). Lanes past w are off in exec, as above.
	s_bitcmp1_b32	s20, 3
	s_cbranch_scc0	.unit_coords
	v_lshlrev_b32	v11, 2, v1
	global_load_dword v4, v11, s[32:33]
	s_add_u32	s34, s16, s7
	s_lshl_b32	s34, s34, 2
	v_mov_b32	v12, s34
	global_load_dword v5, v12, s[32:33]
	s_waitcnt	vmcnt(0)
	v_add_nc_u32	v4, s12, v4
	v_add_nc_u32	v5, s13, v5
.unit_coords:
	s_bitcmp1_b32	s20, 0
	s_cbranch_scc0	.unit_src
	global_load_dword v10, v3, s[10:11]
.unit_src:
	s_bitcmp1_b32	s20, 1
	s_cbranch_scc0	.unit_unorm
	image_load	v2, v[4:5], s[24:31] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm
	s_waitcnt	vmcnt(0)
	s_bitcmp1_b32	s20, 2
	s_cbranch_scc0	.unit_blend
	v_perm_b32	v2, v2, v2, 0x3000102
	s_branch	.unit_blend
.unit_unorm:
	image_load	v[6:9], v[4:5], s[24:31] dmask:0xf dim:SQ_RSRC_IMG_2D unorm
	s_waitcnt	vmcnt(0)
	packunorm	v2, v6, v7, v8, v9
.unit_blend:
	s_bitcmp1_b32	s20, 0
	s_cbranch_scc0	.unit_store
	s_mov_b32	s21, 0x00ff00ff
	v_lshrrev_b32	v4, 24, v2
	v_sub_nc_u32	v4, 0xff, v4
	v_and_b32	v5, s21, v2
	v_lshrrev_b32	v6, 8, v2
	v_and_b32	v6, s21, v6
	v_and_b32	v7, s21, v10
	v_lshrrev_b32	v8, 8, v10
	v_and_b32	v8, s21, v8
	v_mad_u32_u24	v7, v7, v4, 0x007f007f
	v_mad_u32_u24	v8, v8, v4, 0x007f007f
	v_lshrrev_b32	v9, 8, v7
	v_and_b32	v9, s21, v9
	v_add3_u32	v7, v7, 0x00010001, v9
	v_lshrrev_b32	v7, 8, v7
	v_and_b32	v7, s21, v7
	v_lshrrev_b32	v9, 8, v8
	v_and_b32	v9, s21, v9
	v_add3_u32	v8, v8, 0x00010001, v9
	v_lshrrev_b32	v8, 8, v8
	v_and_b32	v8, s21, v8
	v_add_nc_u32	v7, v5, v7
	v_add_nc_u32	v8, v6, v8
	v_pk_min_u16	v7, v7, s21
	v_pk_min_u16	v8, v8, s21
	v_lshl_or_b32	v2, v8, 8, v7
.unit_store:
	global_store_dword v3, v2, s[10:11]
.unit_done:
	s_endpgm
.size image, .-image

; op 9: a picture of NV12 (a byte of luma a pixel at s, sp bytes a row; then the chroma, a byte of U and one of V for each 2 by 2 pixels, at ops
; (0x48), gp bytes a row) scaled by tables as op 7 is, into d as 32-bit XRGB: d[y * dp + x] = rgb(Y[ytab[y] * sp + xtab[x]], UV[ytab[y] / 2 * gp +
; (xtab[x] & ~1)]), xtab (w entries, the source's column) at g and ytab (the rows', the source's row) after it. The colour is compositor_wl_decode_pixel's,
; bit for bit (BT.601, limited range, 8 bits of fraction): c = max(Y - 16, 0), d = U - 128, e = V - 128, R = (298c + 409e + 128) >> 8,
; G = (298c - 100d - 208e + 128) >> 8, B = (298c + 516d + 128) >> 8, each held to 0..255, alpha 255.
.globl nv12
.p2align 8
.type nv12,@function
nv12:
	s_load_dwordx4	s[8:11], s[4:5], 0x0
	s_load_dwordx2	s[12:13], s[4:5], 0x10
	s_load_dwordx4	s[16:19], s[4:5], 0x18
	s_load_dwordx2	s[20:21], s[4:5], 0x48
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s19, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.nv12_done
	s_lshl_b32	s22, s19, 2
	s_add_u32	s24, s12, s22
	s_addc_u32	s25, s13, 0
	s_lshl_b32	s22, s7, 2
	s_add_u32	s24, s24, s22
	s_addc_u32	s25, s25, 0
	s_load_dword	s26, s[24:25], 0x0
	v_lshlrev_b32	v2, 2, v1
	global_load_dword v3, v2, s[12:13]
	s_mov_b32	s29, 0x12a
	s_mov_b32	s30, 0x199
	s_mov_b32	s31, 0x204
	s_mov_b32	s32, -100
	s_mov_b32	s33, -208
	s_mov_b32	s35, 0xff
	s_waitcnt	lgkmcnt(0)
	s_mul_i32	s27, s26, s16
	s_lshr_b32	s28, s26, 1
	s_mul_i32	s28, s28, s18
	s_waitcnt	vmcnt(0)
	v_add_nc_u32	v4, s27, v3
	v_and_b32	v5, -2, v3
	v_add_nc_u32	v5, s28, v5
	global_load_ubyte v6, v4, s[8:9]
	global_load_ushort v7, v5, s[20:21]
	s_mul_i32	s34, s7, s17
	v_add_nc_u32	v13, s34, v1
	v_lshlrev_b32	v13, 2, v13
	s_waitcnt	vmcnt(0)
	v_lshrrev_b32	v8, 8, v7
	v_and_b32	v7, 0xff, v7
	v_sub_nc_u32	v6, v6, 16 clamp
	v_sub_nc_u32	v7, v7, 0x80
	v_sub_nc_u32	v8, v8, 0x80
	v_mad_u32_u24	v9, v6, s29, 0x80
	v_mad_i32_i24	v10, v8, s30, v9
	v_ashrrev_i32	v10, 8, v10
	v_med3_i32	v10, v10, 0, s35
	v_mad_i32_i24	v11, v7, s32, v9
	v_mad_i32_i24	v11, v8, s33, v11
	v_ashrrev_i32	v11, 8, v11
	v_med3_i32	v11, v11, 0, s35
	v_mad_i32_i24	v12, v7, s31, v9
	v_ashrrev_i32	v12, 8, v12
	v_med3_i32	v12, v12, 0, s35
	v_lshl_or_b32	v12, v11, 8, v12
	v_lshl_or_b32	v12, v10, 16, v12
	v_add_nc_u32	v12, 0xff000000, v12
	global_store_dword v13, v12, s[10:11]
.nv12_done:
	s_endpgm
.size nv12, .-nv12

; op 10: the scene under a glass window made its small picture: d[y * dp + x] the mean of the 8 by 8 pixels of s at 8 x, 8 y (sp a row), a level
; at a time as memory_halve_u32 makes it, the two rows of a pair rounded up a byte and then the two columns rounded down. A small pixel a lane: its
; block is loaded two rows at a time (v10 to v25), halved to a row of four (v26 to v41), and those sixteen to four and to the one.
.macro avgup d, a, b
	v_xor_b32	v8, \a, \b
	v_lshrrev_b32	v8, 1, v8
	v_and_b32	v8, v9, v8
	v_lshl_or_b32	\d, \a, 0, \b
	v_sub_nc_u32	\d, \d, v8
.endm
.macro avgdn d, a, b
	v_xor_b32	v8, \a, \b
	v_lshrrev_b32	v8, 1, v8
	v_and_b32	v8, v9, v8
	v_and_b32	\d, \a, \b
	v_add_nc_u32	\d, \d, v8
.endm
.globl halve
.p2align 8
.type halve,@function
halve:
	s_load_dwordx4	s[8:11], s[4:5], 0x0
	s_load_dwordx2	s[14:15], s[4:5], 0x18
	s_load_dword	s16, s[4:5], 0x24
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.halve_done
	s_lshl_b32	s18, s7, 3
	s_mul_i32	s18, s18, s14
	v_lshlrev_b32	v2, 3, v1
	v_add_nc_u32	v2, s18, v2
	v_lshlrev_b32	v2, 2, v2
	s_lshl_b32	s19, s14, 2
	v_mov_b32	v9, 0x7f7f7f7f
	global_load_dword v10, v2, s[8:9]
	v_add_nc_u32	v3, 4, v2
	global_load_dword v11, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v12, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v13, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v14, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v15, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v16, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v17, v3, s[8:9]
	v_add_nc_u32	v3, s19, v2
	global_load_dword v18, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v19, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v20, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v21, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v22, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v23, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v24, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v25, v3, s[8:9]
	s_waitcnt	vmcnt(0)
	avgup	v10, v10, v18
	avgup	v11, v11, v19
	avgup	v12, v12, v20
	avgup	v13, v13, v21
	avgup	v14, v14, v22
	avgup	v15, v15, v23
	avgup	v16, v16, v24
	avgup	v17, v17, v25
	avgdn	v26, v10, v11
	avgdn	v27, v12, v13
	avgdn	v28, v14, v15
	avgdn	v29, v16, v17
	v_add_nc_u32	v2, s19, v2
	v_add_nc_u32	v2, s19, v2
	global_load_dword v10, v2, s[8:9]
	v_add_nc_u32	v3, 4, v2
	global_load_dword v11, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v12, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v13, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v14, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v15, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v16, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v17, v3, s[8:9]
	v_add_nc_u32	v3, s19, v2
	global_load_dword v18, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v19, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v20, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v21, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v22, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v23, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v24, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v25, v3, s[8:9]
	s_waitcnt	vmcnt(0)
	avgup	v10, v10, v18
	avgup	v11, v11, v19
	avgup	v12, v12, v20
	avgup	v13, v13, v21
	avgup	v14, v14, v22
	avgup	v15, v15, v23
	avgup	v16, v16, v24
	avgup	v17, v17, v25
	avgdn	v30, v10, v11
	avgdn	v31, v12, v13
	avgdn	v32, v14, v15
	avgdn	v33, v16, v17
	v_add_nc_u32	v2, s19, v2
	v_add_nc_u32	v2, s19, v2
	global_load_dword v10, v2, s[8:9]
	v_add_nc_u32	v3, 4, v2
	global_load_dword v11, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v12, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v13, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v14, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v15, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v16, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v17, v3, s[8:9]
	v_add_nc_u32	v3, s19, v2
	global_load_dword v18, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v19, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v20, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v21, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v22, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v23, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v24, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v25, v3, s[8:9]
	s_waitcnt	vmcnt(0)
	avgup	v10, v10, v18
	avgup	v11, v11, v19
	avgup	v12, v12, v20
	avgup	v13, v13, v21
	avgup	v14, v14, v22
	avgup	v15, v15, v23
	avgup	v16, v16, v24
	avgup	v17, v17, v25
	avgdn	v34, v10, v11
	avgdn	v35, v12, v13
	avgdn	v36, v14, v15
	avgdn	v37, v16, v17
	v_add_nc_u32	v2, s19, v2
	v_add_nc_u32	v2, s19, v2
	global_load_dword v10, v2, s[8:9]
	v_add_nc_u32	v3, 4, v2
	global_load_dword v11, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v12, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v13, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v14, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v15, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v16, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v17, v3, s[8:9]
	v_add_nc_u32	v3, s19, v2
	global_load_dword v18, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v19, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v20, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v21, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v22, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v23, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v24, v3, s[8:9]
	v_add_nc_u32	v3, 4, v3
	global_load_dword v25, v3, s[8:9]
	s_waitcnt	vmcnt(0)
	avgup	v10, v10, v18
	avgup	v11, v11, v19
	avgup	v12, v12, v20
	avgup	v13, v13, v21
	avgup	v14, v14, v22
	avgup	v15, v15, v23
	avgup	v16, v16, v24
	avgup	v17, v17, v25
	avgdn	v38, v10, v11
	avgdn	v39, v12, v13
	avgdn	v40, v14, v15
	avgdn	v41, v16, v17
	avgup	v26, v26, v30
	avgup	v27, v27, v31
	avgup	v28, v28, v32
	avgup	v29, v29, v33
	avgup	v34, v34, v38
	avgup	v35, v35, v39
	avgup	v36, v36, v40
	avgup	v37, v37, v41
	avgdn	v26, v26, v27
	avgdn	v27, v28, v29
	avgdn	v34, v34, v35
	avgdn	v35, v36, v37
	avgup	v26, v26, v34
	avgup	v27, v27, v35
	avgdn	v26, v26, v27
	s_mul_i32	s20, s7, s15
	v_add_nc_u32	v4, s20, v1
	v_lshlrev_b32	v4, 2, v4
	global_store_dword v4, v26, s[10:11]
.halve_done:
	s_endpgm
.size halve, .-halve

; op 11: a glass window's ground from its small tinted picture, a pixel a lane: d[y * dp + x] for x below w, the small picture s (sp a row) doubled
; three times as memory_double_u32 doubles it (a pixel of a level (c + ((c + n + 1) >> 1)) >> 1 a byte of the one over it and its neighbour, down the
; rows and then across) and the ordered grain 2 * ((x ^ y) & 1) + (y & 1) added (v = 0, dark) or taken away (light), clamped, the alpha left. The
; pixel's block is x >> 3, y >> 3, and s is the small pixel of the block before the first across and up, so the nine small pixels round the block's
; own are rows y >> 3 to (y >> 3) + 2 at columns x >> 3 to (x >> 3) + 2 (v10 to v18). Of each level only what the pixel is made of is made: the
; three by three of level 2 round its own (v30 to v38), the two by two of level 1 (v46 to v49: its own and the neighbour's, across and down), by
; the bits of x and y: bit 2 says which side of the small pixel, bit 1 of the level 2 one, bit 0 of the level 1 one (the masks s[20:31]).
.globl rise
.p2align 8
.type rise,@function
rise:
	s_load_dwordx4	s[8:11], s[4:5], 0x0
	s_load_dwordx2	s[14:15], s[4:5], 0x18
	s_load_dword	s16, s[4:5], 0x24
	s_load_dword	s17, s[4:5], 0x2c
	v_lshl_or_b32	v1, s6, 6, v0
	s_waitcnt	lgkmcnt(0)
	v_cmp_gt_u32	vcc, s16, v1
	s_and_saveexec_b64 s[0:1], vcc
	s_cbranch_execz	.rise_done
	s_lshr_b32	s18, s7, 3
	s_mul_i32	s18, s18, s14
	v_lshrrev_b32	v2, 3, v1
	v_add_nc_u32	v2, s18, v2
	v_lshlrev_b32	v2, 2, v2
	s_lshl_b32	s19, s14, 2
	global_load_dword v10, v2, s[8:9]
	v_add_nc_u32	v4, 4, v2
	global_load_dword v11, v4, s[8:9]
	v_add_nc_u32	v4, 4, v4
	global_load_dword v12, v4, s[8:9]
	v_add_nc_u32	v3, s19, v2
	global_load_dword v13, v3, s[8:9]
	v_add_nc_u32	v4, 4, v3
	global_load_dword v14, v4, s[8:9]
	v_add_nc_u32	v4, 4, v4
	global_load_dword v15, v4, s[8:9]
	v_add_nc_u32	v3, s19, v3
	global_load_dword v16, v3, s[8:9]
	v_add_nc_u32	v4, 4, v3
	global_load_dword v17, v4, s[8:9]
	v_add_nc_u32	v4, 4, v4
	global_load_dword v18, v4, s[8:9]
	v_mov_b32	v9, 0x7f7f7f7f
	v_mov_b32	v4, s7
	v_and_b32	v5, 4, v1
	v_cmp_ne_u32_e64 s[20:21], 0, v5
	v_and_b32	v5, 2, v1
	v_cmp_ne_u32_e64 s[22:23], 0, v5
	v_lshrrev_b32	v5, 1, v1
	v_xor_b32	v5, v1, v5
	v_and_b32	v5, 1, v5
	v_cmp_eq_u32_e64 s[24:25], 0, v5
	v_and_b32	v5, 4, v4
	v_cmp_ne_u32_e64 s[26:27], 0, v5
	v_and_b32	v5, 2, v4
	v_cmp_ne_u32_e64 s[28:29], 0, v5
	v_lshrrev_b32	v5, 1, v4
	v_xor_b32	v5, v4, v5
	v_and_b32	v5, 1, v5
	v_cmp_eq_u32_e64 s[30:31], 0, v5
	s_waitcnt	vmcnt(0)
	v_cndmask_b32_e64 v5, v10, v13, s[26:27]
	v_cndmask_b32_e64 v6, v13, v10, s[26:27]
	avgup	v7, v5, v6
	avgdn	v20, v5, v7
	v_cndmask_b32_e64 v6, v10, v16, s[26:27]
	avgup	v7, v13, v6
	avgdn	v23, v13, v7
	v_cndmask_b32_e64 v5, v13, v16, s[26:27]
	v_cndmask_b32_e64 v6, v16, v13, s[26:27]
	avgup	v7, v5, v6
	avgdn	v26, v5, v7
	v_cndmask_b32_e64 v5, v11, v14, s[26:27]
	v_cndmask_b32_e64 v6, v14, v11, s[26:27]
	avgup	v7, v5, v6
	avgdn	v21, v5, v7
	v_cndmask_b32_e64 v6, v11, v17, s[26:27]
	avgup	v7, v14, v6
	avgdn	v24, v14, v7
	v_cndmask_b32_e64 v5, v14, v17, s[26:27]
	v_cndmask_b32_e64 v6, v17, v14, s[26:27]
	avgup	v7, v5, v6
	avgdn	v27, v5, v7
	v_cndmask_b32_e64 v5, v12, v15, s[26:27]
	v_cndmask_b32_e64 v6, v15, v12, s[26:27]
	avgup	v7, v5, v6
	avgdn	v22, v5, v7
	v_cndmask_b32_e64 v6, v12, v18, s[26:27]
	avgup	v7, v15, v6
	avgdn	v25, v15, v7
	v_cndmask_b32_e64 v5, v15, v18, s[26:27]
	v_cndmask_b32_e64 v6, v18, v15, s[26:27]
	avgup	v7, v5, v6
	avgdn	v28, v5, v7
	v_cndmask_b32_e64 v5, v20, v21, s[20:21]
	v_cndmask_b32_e64 v6, v21, v20, s[20:21]
	avgup	v7, v5, v6
	avgdn	v30, v5, v7
	v_cndmask_b32_e64 v6, v20, v22, s[20:21]
	avgup	v7, v21, v6
	avgdn	v31, v21, v7
	v_cndmask_b32_e64 v5, v21, v22, s[20:21]
	v_cndmask_b32_e64 v6, v22, v21, s[20:21]
	avgup	v7, v5, v6
	avgdn	v32, v5, v7
	v_cndmask_b32_e64 v5, v23, v24, s[20:21]
	v_cndmask_b32_e64 v6, v24, v23, s[20:21]
	avgup	v7, v5, v6
	avgdn	v33, v5, v7
	v_cndmask_b32_e64 v6, v23, v25, s[20:21]
	avgup	v7, v24, v6
	avgdn	v34, v24, v7
	v_cndmask_b32_e64 v5, v24, v25, s[20:21]
	v_cndmask_b32_e64 v6, v25, v24, s[20:21]
	avgup	v7, v5, v6
	avgdn	v35, v5, v7
	v_cndmask_b32_e64 v5, v26, v27, s[20:21]
	v_cndmask_b32_e64 v6, v27, v26, s[20:21]
	avgup	v7, v5, v6
	avgdn	v36, v5, v7
	v_cndmask_b32_e64 v6, v26, v28, s[20:21]
	avgup	v7, v27, v6
	avgdn	v37, v27, v7
	v_cndmask_b32_e64 v5, v27, v28, s[20:21]
	v_cndmask_b32_e64 v6, v28, v27, s[20:21]
	avgup	v7, v5, v6
	avgdn	v38, v5, v7
	v_cndmask_b32_e64 v5, v30, v36, s[28:29]
	v_cndmask_b32_e64 v6, v36, v30, s[28:29]
	avgup	v7, v33, v5
	avgdn	v40, v33, v7
	v_cndmask_b32_e64 v6, v6, v33, s[30:31]
	v_cndmask_b32_e64 v5, v33, v5, s[30:31]
	avgup	v7, v5, v6
	avgdn	v43, v5, v7
	v_cndmask_b32_e64 v5, v31, v37, s[28:29]
	v_cndmask_b32_e64 v6, v37, v31, s[28:29]
	avgup	v7, v34, v5
	avgdn	v41, v34, v7
	v_cndmask_b32_e64 v6, v6, v34, s[30:31]
	v_cndmask_b32_e64 v5, v34, v5, s[30:31]
	avgup	v7, v5, v6
	avgdn	v44, v5, v7
	v_cndmask_b32_e64 v5, v32, v38, s[28:29]
	v_cndmask_b32_e64 v6, v38, v32, s[28:29]
	avgup	v7, v35, v5
	avgdn	v42, v35, v7
	v_cndmask_b32_e64 v6, v6, v35, s[30:31]
	v_cndmask_b32_e64 v5, v35, v5, s[30:31]
	avgup	v7, v5, v6
	avgdn	v45, v5, v7
	v_cndmask_b32_e64 v5, v40, v42, s[22:23]
	v_cndmask_b32_e64 v6, v42, v40, s[22:23]
	avgup	v7, v41, v5
	avgdn	v46, v41, v7
	v_cndmask_b32_e64 v6, v6, v41, s[24:25]
	v_cndmask_b32_e64 v5, v41, v5, s[24:25]
	avgup	v7, v5, v6
	avgdn	v47, v5, v7
	v_cndmask_b32_e64 v5, v43, v45, s[22:23]
	v_cndmask_b32_e64 v6, v45, v43, s[22:23]
	avgup	v7, v44, v5
	avgdn	v48, v44, v7
	v_cndmask_b32_e64 v6, v6, v44, s[24:25]
	v_cndmask_b32_e64 v5, v44, v5, s[24:25]
	avgup	v7, v5, v6
	avgdn	v49, v5, v7
	avgup	v7, v46, v48
	avgdn	v50, v46, v7
	avgup	v7, v47, v49
	avgdn	v51, v47, v7
	avgup	v7, v50, v51
	avgdn	v52, v50, v7
; the grain: its level in v5, each of the blue, green and red bytes plus it and no more than 255 (dark), or less it and no less than 0 (light)
	v_xor_b32	v5, v1, v4
	v_and_b32	v5, 1, v5
	v_and_b32	v6, 1, v4
	v_lshl_or_b32	v5, v5, 1, v6
	v_bfe_u32	v53, v52, 0, 8
	v_bfe_u32	v54, v52, 8, 8
	v_bfe_u32	v55, v52, 16, 8
	v_and_b32	v56, 0xff000000, v52
	s_cmp_eq_u32	s17, 0
	s_cbranch_scc1	.rise_dark
	v_sub_nc_u32	v53, v53, v5
	v_max_i32	v53, 0, v53
	v_sub_nc_u32	v54, v54, v5
	v_max_i32	v54, 0, v54
	v_sub_nc_u32	v55, v55, v5
	v_max_i32	v55, 0, v55
	s_branch	.rise_pack
.rise_dark:
	v_add_nc_u32	v53, v53, v5
	v_min_u32	v53, 255, v53
	v_add_nc_u32	v54, v54, v5
	v_min_u32	v54, 255, v54
	v_add_nc_u32	v55, v55, v5
	v_min_u32	v55, 255, v55
.rise_pack:
	v_lshl_or_b32	v53, v54, 8, v53
	v_lshl_or_b32	v53, v55, 16, v53
	v_lshl_or_b32	v53, v53, 0, v56
	s_mul_i32	s20, s7, s15
	v_add_nc_u32	v6, s20, v1
	v_lshlrev_b32	v6, 2, v6
	global_store_dword v6, v53, s[10:11]
.rise_done:
	s_endpgm
.size rise, .-rise


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
	.amdhsa_forward_progress 1
.end_amdhsa_kernel
.endm
kernel copy, 8, 24
kernel fill, 8, 24
kernel tone, 10, 28
kernel glass, 16, 32
kernel scene, 64, 102
kernel scale, 8, 24
kernel image, 16, 40
kernel nv12, 16, 40
kernel halve, 44, 24
kernel rise, 60, 32
