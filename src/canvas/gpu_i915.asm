; The Intel GPU's kernels, by hand: Gen9 (graphics version 9), SIMD16. Each kernel is written as its instruction words, four per
; instruction, the words in the order the EU fetches them: a `kernel NAME` line starts it, the words follow, comma separated, with
; comments after a `;`. The fill's words are IGT's gen9 fill (lib/gpgpu_fill.c). The other kernels are written in Gen9 assembly as
; src/canvas/gpu_i915_<op>.iga (assembled with iga64 -a -p=9, which gives these words). test/gpu_kernels.py --target intel-gen9 holds every
; kernel to its literal copy of these words; `build gpu-i915` writes them to artifacts/gpu_i915_gen9.inc, which src/canvas/gpu.c includes,
; and `test/gpu_kernels.py --print-intel` writes the same bytes. Gen12's kernels (NAME12, its own encoding) are after Gen9's: see there.
;
; fill (op 1): d[y * dp + x] = v for x below w, in bytes, 16 bytes a block. The compositor's fill of a 32-bit pixel is the same
; fill in bytes: a surface of 4 * w bytes at 4 * dp bytes a row, with the colour's dword as the payload (a dword is a pixel,
; little endian). IGT's gen9 fill starts with an instruction that replicates the colour's low byte, which is wrong for a
; 32-bit colour, so this kernel starts at the next instruction: the entry is 64-byte aligned (the descriptor's kernel start
; is bits 31:6), and the words here are IGT's from instruction 1 on, unchanged.
;
; Verified against IGT: the row-1 word 0x06000004 is the shift count 4 (16 elements a group); the rest is IGT's, unchanged. The
; colour dword is read from r1.0 (the CURBE's first dword), the group ids from r0.1 (x) and r0.6 (y).
; The block write is on binding table entry 0; test/gpu_kernels.py checks each word against IGT's table.
;
; copy (op 0): d[y * dp + x] = s[y * sp + x] for x below w, in bytes, 16 bytes a block, as the fill. The source is read
; a block at a time on binding table entry 1 into r5 (a media block read with its header, one register of response), and
; the same write as the fill puts it on entry 0. Written in Gen9 assembly (src/canvas/gpu_i915_copy.iga, assembled and
; checked with iga64 -a -p=9: the read's descriptor 0x02190001 is the header-present one; without the header bit the message
; never completes and the card hangs). Verified on subtle-cottage (Gen9 UHD 630): 256x16, 512x32, 16x1, 128x16, 2048x64,
; 1024x256 and 4096x16 copies, mismatched source and destination pitches, five repeats, all bit-exact, no kernel log lines.

kernel copy
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; copy, row 1
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; copy, row 2
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; copy, row 3
0x00200001, 0x20800208, 0x00450040, 0x00000000, ; copy, row 4
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; copy, row 5
0x0c800031, 0x20a00208, 0x06000080, 0x02190001, ; copy, row 6 (the media block read, header and 1 response register)
0x0c800031, 0x20000a40, 0x06000080, 0x060a8000, ; copy, row 7 (the media block write)
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; copy, row 8
0x07800031, 0x20000a40, 0x06000e00, 0x82000010, ; copy, row 9 (the last)

kernel fill
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; IGT gen9 fill, row 1
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; IGT gen9 fill, row 2
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; IGT gen9 fill, row 3
0x00200001, 0x20800208, 0x00450040, 0x00000000, ; IGT gen9 fill, row 4
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; IGT gen9 fill, row 5
0x00800001, 0x20a00208, 0x00000020, 0x00000000, ; IGT gen9 fill, row 6
0x0c800031, 0x24000a40, 0x06000080, 0x060a8000, ; IGT gen9 fill, row 7
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; IGT gen9 fill, row 8
0x07800031, 0x20000a40, 0x06000e00, 0x82000010  ; IGT gen9 fill, row 9 (the last)


; tone (op 2): the premultiplied tone v (CURBE dword 0) laid over each pixel of the block as ui_over lays it: a pixel under a
; tone of alpha 0 is left as it was (a predicated move), the rest is the lane formula (x = u * (255 - alpha) + 127, q = (x + (x >> 8) + 1)
; >> 8 per 16-bit lane) plus the tone's own channel, clamped at 255 per lane (sel.lt). Written in Gen9 assembly, src/canvas/gpu_i915_tone.iga
; (iga64 -a -p=9), and checked on the card against the processor's ui_over (bit-exact, eight tones). Binding table entry 0.
kernel tone
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; tone, word 0
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; tone, word 4
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; tone, word 8
0x00200001, 0x20800208, 0x00450040, 0x00000000, ; tone, word 12
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; tone, word 16
0x0c800031, 0x20a00208, 0x06000080, 0x02190000, ; tone, word 20
0x00000001, 0x22800208, 0x00000020, 0x00000000, ; tone, word 24
0x00000008, 0x22a00208, 0x06000280, 0x00000018, ; tone, word 28
0x00000040, 0x22c00208, 0x060042a0, 0x000000ff, ; tone, word 32
0x00000005, 0x22e00208, 0x06000280, 0x00ff00ff, ; tone, word 36
0x00000008, 0x23200208, 0x06000280, 0x00000008, ; tone, word 40
0x00000005, 0x23200208, 0x06000320, 0x00ff00ff, ; tone, word 44
0x00400001, 0x21c00208, 0x002000a0, 0x00000000, ; tone, word 48
0x00400005, 0x21400208, 0x062000a0, 0x00ff00ff, ; tone, word 52
0x00400008, 0x21600208, 0x062000a0, 0x00000008, ; tone, word 56
0x00400005, 0x21600208, 0x06200160, 0x00ff00ff, ; tone, word 60
0x00400041, 0x21400208, 0x12200140, 0x000002c0, ; tone, word 64
0x00400040, 0x21400208, 0x06200140, 0x007f007f, ; tone, word 68
0x00400041, 0x21600208, 0x12200160, 0x000002c0, ; tone, word 72
0x00400040, 0x21600208, 0x06200160, 0x007f007f, ; tone, word 76
0x00400008, 0x21800208, 0x06200140, 0x00000008, ; tone, word 80
0x00400005, 0x21800208, 0x06200180, 0x00ff00ff, ; tone, word 84
0x00400040, 0x21800208, 0x02200180, 0x00200140, ; tone, word 88
0x00400040, 0x21800208, 0x06200180, 0x00010001, ; tone, word 92
0x00400008, 0x21800208, 0x06200180, 0x00000008, ; tone, word 96
0x00400005, 0x21800208, 0x06200180, 0x00ff00ff, ; tone, word 100
0x00400008, 0x21a00208, 0x06200160, 0x00000008, ; tone, word 104
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; tone, word 108
0x00400040, 0x21a00208, 0x022001a0, 0x00200160, ; tone, word 112
0x00400040, 0x21a00208, 0x062001a0, 0x00010001, ; tone, word 116
0x00400008, 0x21a00208, 0x062001a0, 0x00000008, ; tone, word 120
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; tone, word 124
0x00400040, 0x21800208, 0x02200180, 0x000002e0, ; tone, word 128
0x00400040, 0x21a00208, 0x022001a0, 0x00000320, ; tone, word 132
0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff, ; tone, word 136
0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff, ; tone, word 140
0x00400009, 0x21a00208, 0x062001a0, 0x00000008, ; tone, word 144
0x00400006, 0x20a00208, 0x02200180, 0x002001a0, ; tone, word 148
0x01400010, 0x20000200, 0x060002a0, 0x00000000, ; tone, word 152
0x00410001, 0x20a00208, 0x002001c0, 0x00000000, ; tone, word 156
0x0c800031, 0x20000a40, 0x06000080, 0x060a8000, ; tone, word 160
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; tone, word 164
0x07800031, 0x20000a40, 0x06000e00, 0x82000010, ; tone, word 168
; glass (op 3): the picture over the ground with its grain, into d, as the processor's memory_glass_over_u32 (the grain of
; compositor/glass.c, the blend of lib.c): each of R, G, B of the ground plus the grain level (dark, v = 0) or minus it (light), clamped;
; the ground's times (255 - the picture's alpha) divided by 255 rounded, plus the picture's byte, clamped. The grain level of x, y is
; 2 * ((x + x0 ^ y + y0) & 1) + ((y + y0) & 1), from the CURBE's x0 and y0. Written in Gen9 assembly, src/canvas/gpu_i915_glass.iga
; (iga64 -a -p=9). Binding table entry 0 the destination, 1 the picture, 2 the ground.
kernel glass
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; glass, word 0
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; glass, word 4
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; glass, word 8
0x00200001, 0x20800208, 0x00450040, 0x00000000, ; glass, word 12
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; glass, word 16
0x0c800031, 0x20a00208, 0x06000080, 0x02190001, ; glass, word 20
0x0c800031, 0x20c00208, 0x06000080, 0x02190002, ; glass, word 24
0x00000005, 0x22800208, 0x06000024, 0x00000001, ; glass, word 28
0x00000040, 0x22840208, 0x02000018, 0x00000028, ; glass, word 32
0x00000005, 0x22840208, 0x06000284, 0x00000001, ; glass, word 36
0x00000007, 0x22880208, 0x02000280, 0x00000284, ; glass, word 40
0x00000009, 0x22a00208, 0x06000288, 0x00000001, ; glass, word 44
0x00000040, 0x22a00208, 0x020002a0, 0x00000284, ; glass, word 48
0x00000009, 0x22a40208, 0x06000288, 0x00000001, ; glass, word 52
0x00000040, 0x22a40208, 0x060042a4, 0x00000002, ; glass, word 56
0x00000040, 0x22a40208, 0x020002a4, 0x00000284, ; glass, word 60
0x00000001, 0x22c00608, 0x00000000, 0x00000000, ; glass, word 64
0x02000010, 0x20000200, 0x06000020, 0x00000000, ; glass, word 68
0x00010001, 0x22c00608, 0x00000000, 0x00000001, ; glass, word 72
0x00000040, 0x22c40208, 0x060042c0, 0x00000001, ; glass, word 76
0x00000001, 0x22e00208, 0x000002a0, 0x00000000, ; glass, word 80
0x00000001, 0x22e40208, 0x000002a4, 0x00000000, ; glass, word 84
0x00000001, 0x22e80208, 0x000002a0, 0x00000000, ; glass, word 88
0x00000001, 0x22ec0208, 0x000002a4, 0x00000000, ; glass, word 92
0x00400041, 0x23000208, 0x122002e0, 0x000002c4, ; glass, word 96
0x00400041, 0x23200208, 0x122002e0, 0x000002c0, ; glass, word 100
0x00400009, 0x23400208, 0x06200300, 0x00000010, ; glass, word 104
0x00400006, 0x23400208, 0x02200340, 0x00200300, ; glass, word 108
0x00400009, 0x23600208, 0x06200320, 0x00000010, ; glass, word 112
0x00400006, 0x23600208, 0x02200360, 0x00200320, ; glass, word 116
0x00400005, 0x20e00208, 0x062000c0, 0x00ff00ff, ; glass, word 120
0x00400008, 0x21000208, 0x062000c0, 0x00000008, ; glass, word 124
0x00400005, 0x21000208, 0x06200100, 0x00ff00ff, ; glass, word 128
0x00600040, 0x20e01a68, 0x1a8d00e0, 0x008d0340, ; glass, word 132
0x00600040, 0x20e01a68, 0x1a8d00e0, 0x008d4360, ; glass, word 136
0x00600040, 0x21001a68, 0x1a8d0100, 0x008d0300, ; glass, word 140
0x00600040, 0x21001a68, 0x1a8d0100, 0x008d4320, ; glass, word 144
0x04600002, 0x20e01a68, 0x1e8d00e0, 0x00000000, ; glass, word 148
0x05600002, 0x20e01a68, 0x1e8d00e0, 0x00ff00ff, ; glass, word 152
0x04600002, 0x21001a68, 0x1e8d0100, 0x00000000, ; glass, word 156
0x05600002, 0x21001a68, 0x1e8d0100, 0x00ff00ff, ; glass, word 160
0x00400008, 0x21200208, 0x062000a0, 0x00000018, ; glass, word 164
0x00400040, 0x21200208, 0x06204120, 0x000000ff, ; glass, word 168
0x00400005, 0x21400208, 0x062000a0, 0x00ff00ff, ; glass, word 172
0x00400008, 0x21600208, 0x062000a0, 0x00000008, ; glass, word 176
0x00400005, 0x21600208, 0x06200160, 0x00ff00ff, ; glass, word 180
0x00400041, 0x21800208, 0x122000e0, 0x008a0120, ; glass, word 184
0x00400040, 0x21800208, 0x06200180, 0x007f007f, ; glass, word 188
0x00400008, 0x21a00208, 0x06200180, 0x00000008, ; glass, word 192
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; glass, word 196
0x00400040, 0x21a00208, 0x022001a0, 0x00200180, ; glass, word 200
0x00400040, 0x21a00208, 0x062001a0, 0x00010001, ; glass, word 204
0x00400008, 0x21a00208, 0x062001a0, 0x00000008, ; glass, word 208
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; glass, word 212
0x00400040, 0x21a00208, 0x022001a0, 0x00200140, ; glass, word 216
0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff, ; glass, word 220
0x00400041, 0x21c00208, 0x12200100, 0x008a0120, ; glass, word 224
0x00400040, 0x21c00208, 0x062001c0, 0x007f007f, ; glass, word 228
0x00400008, 0x21e00208, 0x062001c0, 0x00000008, ; glass, word 232
0x00400005, 0x21e00208, 0x062001e0, 0x00ff00ff, ; glass, word 236
0x00400040, 0x21e00208, 0x022001e0, 0x002001c0, ; glass, word 240
0x00400040, 0x21e00208, 0x062001e0, 0x00010001, ; glass, word 244
0x00400008, 0x21e00208, 0x062001e0, 0x00000008, ; glass, word 248
0x00400005, 0x21e00208, 0x062001e0, 0x00ff00ff, ; glass, word 252
0x00400040, 0x21e00208, 0x022001e0, 0x00200160, ; glass, word 256
0x05600002, 0x21e01248, 0x168d01e0, 0x00ff00ff, ; glass, word 260
0x00400009, 0x21e00208, 0x062001e0, 0x00000008, ; glass, word 264
0x00400006, 0x20a00208, 0x022001a0, 0x002001e0, ; glass, word 268
0x0c800031, 0x20000a40, 0x06000080, 0x060a8000, ; glass, word 272
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; glass, word 276
0x07800031, 0x20000a40, 0x06000e00, 0x82000010, ; glass, word 280

; box (op 5), the rectangle part: one plain colour box with no corner radii, src/canvas/gpu_i915_bx.iga (iga64 -a -p=9, 81 instructions).
; Checked on subtle-cottage (Gen9 UHD 630) against a host port of the kit box layer (ui_kit_fill, rx = ry = 0, no clip): 64 boxes, bit-exact.
kernel bx
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; box, instruction 1
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; box, instruction 2
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; box, instruction 3
0x00200001, 0x20800208, 0x00450040, 0x00000000, ; box, instruction 4
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; box, instruction 5
0x0c800031, 0x20a00208, 0x06000080, 0x02190000, ; box, instruction 6
0x00600001, 0x22800208, 0x008d0020, 0x00000000, ; box, instruction 7
0x00000008, 0x22c00208, 0x06000280, 0x00000018, ; box, instruction 8
0x00000005, 0x23200208, 0x06000280, 0x00ff00ff, ; box, instruction 9
0x00000008, 0x23400208, 0x06000280, 0x00000008, ; box, instruction 10
0x00000005, 0x23400208, 0x06000340, 0x000000ff, ; box, instruction 11
0x00000009, 0x25200208, 0x06000018, 0x00000008, ; box, instruction 12
0x00000040, 0x25200208, 0x06000520, 0x00000008, ; box, instruction 13
0x00000040, 0x25400a28, 0x0a00028c, 0x00004520, ; box, instruction 14
0x00000040, 0x25400a28, 0x0e000540, 0x0000000f, ; box, instruction 15
0x0000000c, 0x25400a28, 0x0e000540, 0x00000004, ; box, instruction 16
0x00000040, 0x25600a28, 0x0a000290, 0x00004520, ; box, instruction 17
0x00000040, 0x25600a28, 0x0e000560, 0x0000000f, ; box, instruction 18
0x0000000c, 0x25600a28, 0x0e000560, 0x00000004, ; box, instruction 19
0x05000002, 0x25400a28, 0x0e000540, 0x00000010, ; box, instruction 20
0x04000002, 0x25400a28, 0x0e000540, 0x00000000, ; box, instruction 21
0x05000002, 0x25600a28, 0x0e000560, 0x00000010, ; box, instruction 22
0x04000002, 0x25600a28, 0x0e000560, 0x00000000, ; box, instruction 23
0x00000040, 0x25800a28, 0x0a000560, 0x00004540, ; box, instruction 24
0x04000002, 0x25800a28, 0x0e000580, 0x00000000, ; box, instruction 25
0x00000041, 0x23000208, 0x120002c0, 0x00000580, ; box, instruction 26
0x00000001, 0x23c00608, 0x00000000, 0x00000000, ; box, instruction 27
0x00000001, 0x23c40608, 0x00000000, 0x00000001, ; box, instruction 28
0x00000001, 0x23c80608, 0x00000000, 0x00000002, ; box, instruction 29
0x00000001, 0x23cc0608, 0x00000000, 0x00000003, ; box, instruction 30
0x00000009, 0x23e00208, 0x06000004, 0x00000002, ; box, instruction 31
0x00400040, 0x23e00208, 0x022003c0, 0x000003e0, ; box, instruction 32
0x00400009, 0x23e00208, 0x062003e0, 0x00000008, ; box, instruction 33
0x04400002, 0x24000a28, 0x0a000284, 0x002003e0, ; box, instruction 34
0x00400040, 0x24200a28, 0x0e2003e0, 0x00000100, ; box, instruction 35
0x05400002, 0x24200a28, 0x0a000288, 0x00200420, ; box, instruction 36
0x00400040, 0x24400a28, 0x0a200420, 0x00204400, ; box, instruction 37
0x04400002, 0x24400a28, 0x0e200440, 0x00000000, ; box, instruction 38
0x00400041, 0x24600208, 0x12200440, 0x00000300, ; box, instruction 39
0x00400008, 0x24600208, 0x06200460, 0x0000000c, ; box, instruction 40
0x00400008, 0x24800208, 0x06200460, 0x00000007, ; box, instruction 41
0x00400040, 0x24800208, 0x02200480, 0x00200460, ; box, instruction 42
0x00400040, 0x24a00a28, 0x0e204460, 0x000000ff, ; box, instruction 43
0x00400041, 0x24c00208, 0x12000320, 0x00400480, ; box, instruction 44
0x00400040, 0x24c00208, 0x062004c0, 0x00800080, ; box, instruction 45
0x00400008, 0x24c00208, 0x062004c0, 0x00000008, ; box, instruction 46
0x00400005, 0x24c00208, 0x062004c0, 0x00ff00ff, ; box, instruction 47
0x00400041, 0x24e00208, 0x12000340, 0x00400480, ; box, instruction 48
0x00400040, 0x24e00208, 0x062004e0, 0x00000080, ; box, instruction 49
0x00400008, 0x24e00208, 0x062004e0, 0x00000008, ; box, instruction 50
0x00400009, 0x25000208, 0x06200460, 0x00000010, ; box, instruction 51
0x00400006, 0x24e00208, 0x022004e0, 0x00200500, ; box, instruction 52
0x00400001, 0x21c00208, 0x002000a0, 0x00000000, ; box, instruction 53
0x00400005, 0x21400208, 0x062001c0, 0x00ff00ff, ; box, instruction 54
0x00400008, 0x21600208, 0x062001c0, 0x00000008, ; box, instruction 55
0x00400005, 0x21600208, 0x06200160, 0x00ff00ff, ; box, instruction 56
0x00400041, 0x21400208, 0x12200140, 0x004004a0, ; box, instruction 57
0x00400040, 0x21400208, 0x06200140, 0x007f007f, ; box, instruction 58
0x00400041, 0x21600208, 0x12200160, 0x004004a0, ; box, instruction 59
0x00400040, 0x21600208, 0x06200160, 0x007f007f, ; box, instruction 60
0x00400008, 0x21800208, 0x06200140, 0x00000008, ; box, instruction 61
0x00400005, 0x21800208, 0x06200180, 0x00ff00ff, ; box, instruction 62
0x00400040, 0x21800208, 0x02200180, 0x00200140, ; box, instruction 63
0x00400040, 0x21800208, 0x06200180, 0x00010001, ; box, instruction 64
0x00400008, 0x21800208, 0x06200180, 0x00000008, ; box, instruction 65
0x00400005, 0x21800208, 0x06200180, 0x00ff00ff, ; box, instruction 66
0x00400008, 0x21a00208, 0x06200160, 0x00000008, ; box, instruction 67
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; box, instruction 68
0x00400040, 0x21a00208, 0x022001a0, 0x00200160, ; box, instruction 69
0x00400040, 0x21a00208, 0x062001a0, 0x00010001, ; box, instruction 70
0x00400008, 0x21a00208, 0x062001a0, 0x00000008, ; box, instruction 71
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; box, instruction 72
0x00400040, 0x21800208, 0x02200180, 0x002004c0, ; box, instruction 73
0x00400040, 0x21a00208, 0x022001a0, 0x002004e0, ; box, instruction 74
0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff, ; box, instruction 75
0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff, ; box, instruction 76
0x00400009, 0x21a00208, 0x062001a0, 0x00000008, ; box, instruction 77
0x00400006, 0x20a00208, 0x02200180, 0x002001a0, ; box, instruction 78
0x0c800031, 0x20000a40, 0x06000080, 0x060a8000, ; box, instruction 79
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; box, instruction 80
0x07800031, 0x20000a40, 0x06000e00, 0x82000010, ; box, instruction 81

; scene (op 6): fill (0), tone over (1), image copy (4) and premultiplied image over (11), the images with an optional R/B swap (0x80),
; drawn in order over a strip of eight 8 by 8 tiles of the damage a thread (walker group X the strip's 64 pixels, Y its row of tiles).
; The strip reads its cell's list once from entry 1 (a cell 64 by 64 pixels, eight 64-byte rows, a record a row; a list of more than
; eight is in entry 2, the cell's first row saying where), the records clipped to the damage and binned by the encoder, a list
; started at the last record that covers all of its cell. A tile is read and written on entry 0 with one 32-byte by 8-row media block
; message each: read only when no record covers the tile whole and opaque, at the first record that draws in it, and written only
; when one did. An image's tile is read through the record's own entry (word 7, the send's descriptor made in a0.0). The record drawn
; is moved out of the batch of eight by its number (r[a0.0]). Every ALU instruction is SIMD8. Written in Gen9 assembly,
; src/canvas/gpu_i915_scene.iga (iga64 -a -p=9 -Xforce-no-compact); gpu.c's Gen7 patch of these words is iga64's own Gen7.5 assembly
; of the same source but for the sends' port and source type (540 instructions compared).
kernel scene
0x00000001, 0x20e00608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r7.0<1>:ud 0x0:ud
0x00000001, 0x20e40608, 0x00000000, 0x00000001, ; scene, mov (1|M0) r7.1<1>:ud 0x1:ud
0x00000001, 0x20e80608, 0x00000000, 0x00000002, ; scene, mov (1|M0) r7.2<1>:ud 0x2:ud
0x00000001, 0x20ec0608, 0x00000000, 0x00000003, ; scene, mov (1|M0) r7.3<1>:ud 0x3:ud
0x00000001, 0x20f00608, 0x00000000, 0x00000004, ; scene, mov (1|M0) r7.4<1>:ud 0x4:ud
0x00000001, 0x20f40608, 0x00000000, 0x00000005, ; scene, mov (1|M0) r7.5<1>:ud 0x5:ud
0x00000001, 0x20f80608, 0x00000000, 0x00000006, ; scene, mov (1|M0) r7.6<1>:ud 0x6:ud
0x00000001, 0x20fc0608, 0x00000000, 0x00000007, ; scene, mov (1|M0) r7.7<1>:ud 0x7:ud
0x00000009, 0x20400208, 0x06000004, 0x00000006, ; scene, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x6:ud
0x00000040, 0x20400208, 0x02000040, 0x00000024, ; scene, add (1|M0) r2.0<1>:ud r2.0<0;1,0>:ud r1.1<0;1,0>:ud
0x00000009, 0x20440208, 0x06000018, 0x00000003, ; scene, shl (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000040, 0x20440208, 0x02000044, 0x00000028, ; scene, add (1|M0) r2.1<1>:ud r2.1<0;1,0>:ud r1.2<0;1,0>:ud
0x00000040, 0x20800208, 0x06000040, 0x00000040, ; scene, add (1|M0) r4.0<1>:ud r2.0<0;1,0>:ud 0x40:ud
0x05000002, 0x2080020b, 0x02000080, 0x0000002c, ; scene, sel (1|M0) (lt)f1.1 r4.0<1>:ud r4.0<0;1,0>:ud r1.3<0;1,0>:ud
0x00000040, 0x204c0208, 0x06000044, 0x00000008, ; scene, add (1|M0) r2.3<1>:ud r2.1<0;1,0>:ud 0x8:ud
0x05000002, 0x204c020b, 0x0200004c, 0x00000030, ; scene, sel (1|M0) (lt)f1.1 r2.3<1>:ud r2.3<0;1,0>:ud r1.4<0;1,0>:ud
0x00600001, 0x21a00208, 0x008d0000, 0x00000000, ; scene, mov (8|M0) r13.0<1>:ud r0.0<8;8,1>:ud
0x00000009, 0x21a00208, 0x06000004, 0x00000008, ; scene, shl (1|M0) r13.0<1>:ud r0.1<0;1,0>:ud 0x8:ud
0x00000009, 0x21a40208, 0x06000018, 0x00000003, ; scene, shl (1|M0) r13.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000001, 0x21a80608, 0x00000000, 0x0007001f, ; scene, mov (1|M0) r13.2<1>:ud 0x7001F:ud
0x00600001, 0x21800208, 0x008d0000, 0x00000000, ; scene, mov (8|M0) r12.0<1>:ud r0.0<8;8,1>:ud
0x00000001, 0x21880608, 0x00000000, 0x0007001f, ; scene, mov (1|M0) r12.2<1>:ud 0x7001F:ud
0x00600001, 0x21600208, 0x008d0000, 0x00000000, ; scene, mov (8|M0) r11.0<1>:ud r0.0<8;8,1>:ud
0x00000009, 0x21600208, 0x06000004, 0x00000006, ; scene, shl (1|M0) r11.0<1>:ud r0.1<0;1,0>:ud 0x6:ud
0x00000005, 0x21640208, 0x06000018, 0xfffffff8, ; scene, and (1|M0) r11.1<1>:ud r0.6<0;1,0>:ud 0xFFFFFFF8:ud
0x00000001, 0x21680608, 0x00000000, 0x0007001f, ; scene, mov (1|M0) r11.2<1>:ud 0x7001F:ud
0x0c800031, 0x25000208, 0x06000160, 0x02890001, ; scene, send (16|M0) r40 r11 0xC 0x02890001
0x00000008, 0x20880208, 0x06000500, 0x00000010, ; scene, shr (1|M0) r4.2<1>:ud r40.0<0;1,0>:ud 0x10:ud
0x00000005, 0x208c0208, 0x06000500, 0x0000ff00, ; scene, and (1|M0) r4.3<1>:ud r40.0<0;1,0>:ud 0xFF00:ud
0x00000001, 0x20580608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r2.6<1>:ud 0x0:ud
0x00000001, 0x209c0608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r4.7<1>:ud 0x0:ud
0x00000001, 0x20840608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r4.1<1>:ud 0x0:ud
0x01800010, 0x20000201, 0x06000088, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r4.2<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00002fc0, ; scene, (W&f0.1) jmpi L12768
0x00000005, 0x21000208, 0x0600008c, 0x00000100, ; scene, and (1|M0) r8.0<1>:ud r4.3<0;1,0>:ud 0x100:ud
0x01800010, 0x20000201, 0x06000100, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.0<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000040, ; scene, (W&f0.1) jmpi L656
0x00000001, 0x20840208, 0x00000504, 0x00000000, ; scene, mov (1|M0) r4.1<1>:ud r40.1<0;1,0>:ud
0x00000001, 0x21600608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000001, 0x21640208, 0x00000084, 0x00000000, ; scene, mov (1|M0) r11.1<1>:ud r4.1<0;1,0>:ud
0x0c800031, 0x25000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r40 r11 0xC 0x02890002
0x00000040, 0x20480208, 0x06000040, 0x00000008, ; scene, add (1|M0) r2.2<1>:ud r2.0<0;1,0>:ud 0x8:ud
0x00000009, 0x205c0208, 0x06000040, 0x00000002, ; scene, shl (1|M0) r2.7<1>:ud r2.0<0;1,0>:ud 0x2:ud
0x00600040, 0x20600208, 0x028d00e0, 0x00000040, ; scene, add (8|M0) r3.0<1>:ud r7.0<8;8,1>:ud r2.0<0;1,0>:ud
0x00000001, 0x20500608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r2.4<1>:ud 0x0:ud
0x00000001, 0x20540608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r2.5<1>:ud 0x0:ud
0x00000001, 0x20900608, 0x00000000, 0x00000001, ; scene, mov (1|M0) r4.4<1>:ud 0x1:ud
0x00000001, 0x20940608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r4.5<1>:ud 0x0:ud
0x00000005, 0x21000208, 0x0600008c, 0x00000200, ; scene, and (1|M0) r8.0<1>:ud r4.3<0;1,0>:ud 0x200:ud
0x02800010, 0x20000201, 0x06000100, 0x00000000, ; scene, cmp (16|M0) (ne)f0.1 null<1>:ud r8.0<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000230, ; scene, (W&f0.1) jmpi L1376
0x00000001, 0x20540608, 0x00000000, 0xffffffff, ; scene, mov (1|M0) r2.5<1>:ud 0xFFFFFFFF:ud
0x04800010, 0x20000201, 0x02000050, 0x00000088, ; scene, cmp (16|M0) (ge)f0.1 null<1>:ud r2.4<0;1,0>:ud r4.2<0;1,0>:ud
0x00010020, 0x34000005, 0x0e001400, 0x000001b0, ; scene, (W&f0.1) jmpi L1296
0x00000008, 0x21140208, 0x06000050, 0x00000003, ; scene, shr (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x3:ud
0x01800010, 0x20000201, 0x02000114, 0x00000058, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.5<0;1,0>:ud r2.6<0;1,0>:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000050, ; scene, (W&f0.1) jmpi L992
0x00000001, 0x20580208, 0x00000114, 0x00000000, ; scene, mov (1|M0) r2.6<1>:ud r8.5<0;1,0>:ud
0x00000009, 0x21180208, 0x06000114, 0x00000003, ; scene, shl (1|M0) r8.6<1>:ud r8.5<0;1,0>:ud 0x3:ud
0x00000001, 0x21600608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000040, 0x21640208, 0x02000084, 0x00000118, ; scene, add (1|M0) r11.1<1>:ud r4.1<0;1,0>:ud r8.6<0;1,0>:ud
0x0c800031, 0x25000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r40 r11 0xC 0x02890002
0x00000005, 0x21140208, 0x06000050, 0x00000007, ; scene, and (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x7:ud
0x00000009, 0x21140208, 0x06000114, 0x00000005, ; scene, shl (1|M0) r8.5<1>:ud r8.5<0;1,0>:ud 0x5:ud
0x00000040, 0x22001240, 0x16000114, 0x05000500, ; scene, add (1|M0) a0.0<1>:uw r8.10<0;1,0>:uw 0x500:uw
0x00600001, 0x24e00208, 0x008d8000, 0x00000000, ; scene, mov (8|M0) r39.0<1>:ud r[a0.0]<8;8,1>:ud
0x00000040, 0x210c0208, 0x02000040, 0x000044e4, ; scene, add (1|M0) r8.3<1>:ud r2.0<0;1,0>:ud -r39.1<0;1,0>:ud
0x00000040, 0x21100208, 0x020004ec, 0x00004048, ; scene, add (1|M0) r8.4<1>:ud r39.3<0;1,0>:ud -r2.2<0;1,0>:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x00000040, 0x21100208, 0x02000044, 0x000044e8, ; scene, add (1|M0) r8.4<1>:ud r2.1<0;1,0>:ud -r39.2<0;1,0>:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x00000040, 0x21100208, 0x020004f0, 0x0000404c, ; scene, add (1|M0) r8.4<1>:ud r39.4<0;1,0>:ud -r2.3<0;1,0>:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x00000005, 0x21100208, 0x060004e0, 0x0000007b, ; scene, and (1|M0) r8.4<1>:ud r39.0<0;1,0>:ud 0x7B:ud
0x00000040, 0x21100208, 0x06004110, 0x00000000, ; scene, add (1|M0) r8.4<1>:ud -r8.4<0;1,0>:ud 0x0:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x05800010, 0x20000a21, 0x0e00010c, 0x00000000, ; scene, cmp (16|M0) (lt)f0.1 null<1>:d r8.3<0;1,0>:d 0:d
0x00010020, 0x34000005, 0x0e001400, 0x00000010, ; scene, (W&f0.1) jmpi L1264
0x00000001, 0x20540208, 0x00000050, 0x00000000, ; scene, mov (1|M0) r2.5<1>:ud r2.4<0;1,0>:ud
0x00000040, 0x20500208, 0x06000050, 0x00000001, ; scene, add (1|M0) r2.4<1>:ud r2.4<0;1,0>:ud 0x1:ud
0x00000020, 0x34000004, 0x0e001400, 0xfffffe30, ; scene, (W) jmpi L832
0x00000001, 0x20500208, 0x00000054, 0x00000000, ; scene, mov (1|M0) r2.4<1>:ud r2.5<0;1,0>:ud
0x02800010, 0x20000201, 0x06000054, 0xffffffff, ; scene, cmp (16|M0) (ne)f0.1 null<1>:ud r2.5<0;1,0>:ud 0xFFFFFFFF:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000020, ; scene, (W&f0.1) jmpi L1376
0x00000001, 0x20500608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r2.4<1>:ud 0x0:ud
0x00000001, 0x20900608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r4.4<1>:ud 0x0:ud
0x04800010, 0x20000201, 0x02000050, 0x00000088, ; scene, cmp (16|M0) (ge)f0.1 null<1>:ud r2.4<0;1,0>:ud r4.2<0;1,0>:ud
0x00010020, 0x34000005, 0x0e001400, 0x00002bf0, ; scene, (W&f0.1) jmpi L12656
0x00000008, 0x21140208, 0x06000050, 0x00000003, ; scene, shr (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x3:ud
0x01800010, 0x20000201, 0x02000114, 0x00000058, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.5<0;1,0>:ud r2.6<0;1,0>:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000050, ; scene, (W&f0.1) jmpi L1536
0x00000001, 0x20580208, 0x00000114, 0x00000000, ; scene, mov (1|M0) r2.6<1>:ud r8.5<0;1,0>:ud
0x00000009, 0x21180208, 0x06000114, 0x00000003, ; scene, shl (1|M0) r8.6<1>:ud r8.5<0;1,0>:ud 0x3:ud
0x00000001, 0x21600608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000040, 0x21640208, 0x02000084, 0x00000118, ; scene, add (1|M0) r11.1<1>:ud r4.1<0;1,0>:ud r8.6<0;1,0>:ud
0x0c800031, 0x25000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r40 r11 0xC 0x02890002
0x00000005, 0x21140208, 0x06000050, 0x00000007, ; scene, and (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x7:ud
0x00000009, 0x21140208, 0x06000114, 0x00000005, ; scene, shl (1|M0) r8.5<1>:ud r8.5<0;1,0>:ud 0x5:ud
0x00000040, 0x22001240, 0x16000114, 0x05000500, ; scene, add (1|M0) a0.0<1>:uw r8.10<0;1,0>:uw 0x500:uw
0x00600001, 0x24e00208, 0x008d8000, 0x00000000, ; scene, mov (8|M0) r39.0<1>:ud r[a0.0]<8;8,1>:ud
0x00000040, 0x210c0208, 0x020004ec, 0x00004040, ; scene, add (1|M0) r8.3<1>:ud r39.3<0;1,0>:ud -r2.0<0;1,0>:ud
0x00000040, 0x210c0208, 0x0600010c, 0xffffffff, ; scene, add (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud 0xFFFFFFFF:ud
0x00000040, 0x21100208, 0x02000048, 0x000044e4, ; scene, add (1|M0) r8.4<1>:ud r2.2<0;1,0>:ud -r39.1<0;1,0>:ud
0x00000040, 0x21100208, 0x06000110, 0xffffffff, ; scene, add (1|M0) r8.4<1>:ud r8.4<0;1,0>:ud 0xFFFFFFFF:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x00000040, 0x21100208, 0x020004f0, 0x00004044, ; scene, add (1|M0) r8.4<1>:ud r39.4<0;1,0>:ud -r2.1<0;1,0>:ud
0x00000040, 0x21100208, 0x06000110, 0xffffffff, ; scene, add (1|M0) r8.4<1>:ud r8.4<0;1,0>:ud 0xFFFFFFFF:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x00000040, 0x21100208, 0x0200004c, 0x000044e8, ; scene, add (1|M0) r8.4<1>:ud r2.3<0;1,0>:ud -r39.2<0;1,0>:ud
0x00000040, 0x21100208, 0x06000110, 0xffffffff, ; scene, add (1|M0) r8.4<1>:ud r8.4<0;1,0>:ud 0xFFFFFFFF:ud
0x00000006, 0x210c0208, 0x0200010c, 0x00000110, ; scene, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud
0x05800010, 0x20000a21, 0x0e00010c, 0x00000000, ; scene, cmp (16|M0) (lt)f0.1 null<1>:d r8.3<0;1,0>:d 0:d
0x00010020, 0x34000005, 0x0e001400, 0x00002a40, ; scene, (W&f0.1) jmpi L12624
0x02800010, 0x20000201, 0x06000090, 0x00000000, ; scene, cmp (16|M0) (ne)f0.1 null<1>:ud r4.4<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000090, ; scene, (W&f0.1) jmpi L1984
0x0c800031, 0x21c00208, 0x060001a0, 0x02890000, ; scene, send (16|M0) r14 r13 0xC 0x02890000
0x00600006, 0x24c00208, 0x028d01c0, 0x008d01e0, ; scene, or (8|M0) r38.0<1>:ud r14.0<8;8,1>:ud r15.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d04c0, 0x008d0200, ; scene, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r16.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d04c0, 0x008d0220, ; scene, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r17.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d04c0, 0x008d0240, ; scene, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r18.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d04c0, 0x008d0260, ; scene, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r19.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d04c0, 0x008d0280, ; scene, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r20.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d04c0, 0x008d02a0, ; scene, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r21.0<8;8,1>:ud
0x00000001, 0x20900608, 0x00000000, 0x00000001, ; scene, mov (1|M0) r4.4<1>:ud 0x1:ud
0x00600040, 0x21200208, 0x028d0060, 0x000044e4, ; scene, add (8|M0) r9.0<1>:ud r3.0<8;8,1>:ud -r39.1<0;1,0>:ud
0x00600040, 0x24c00208, 0x028d4060, 0x000004ec, ; scene, add (8|M0) r38.0<1>:ud -r3.0<8;8,1>:ud r39.3<0;1,0>:ud
0x00600040, 0x24c00208, 0x068d04c0, 0xffffffff, ; scene, add (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud 0xFFFFFFFF:ud
0x00600006, 0x21200208, 0x028d0120, 0x008d04c0, ; scene, or (8|M0) r9.0<1>:ud r9.0<8;8,1>:ud r38.0<8;8,1>:ud
0x00000040, 0x21000208, 0x02000044, 0x000044e8, ; scene, add (1|M0) r8.0<1>:ud r2.1<0;1,0>:ud -r39.2<0;1,0>:ud
0x00000040, 0x21040208, 0x020004f0, 0x00004044, ; scene, add (1|M0) r8.1<1>:ud r39.4<0;1,0>:ud -r2.1<0;1,0>:ud
0x00000040, 0x21040208, 0x06000104, 0xffffffff, ; scene, add (1|M0) r8.1<1>:ud r8.1<0;1,0>:ud 0xFFFFFFFF:ud
0x00600040, 0x21400208, 0x028d00e0, 0x00000100, ; scene, add (8|M0) r10.0<1>:ud r7.0<8;8,1>:ud r8.0<0;1,0>:ud
0x00600040, 0x24c00208, 0x028d40e0, 0x00000104, ; scene, add (8|M0) r38.0<1>:ud -r7.0<8;8,1>:ud r8.1<0;1,0>:ud
0x00600006, 0x21400208, 0x028d0140, 0x008d04c0, ; scene, or (8|M0) r10.0<1>:ud r10.0<8;8,1>:ud r38.0<8;8,1>:ud
0x00000005, 0x21080208, 0x060004e0, 0x0000007f, ; scene, and (1|M0) r8.2<1>:ud r39.0<0;1,0>:ud 0x7F:ud
0x01800010, 0x20000201, 0x06000108, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00001460, ; scene, (W&f0.1) jmpi L7408
0x01800010, 0x20000201, 0x06000108, 0x00000001, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x1:ud
0x00010020, 0x34000005, 0x0e001400, 0x000014d0, ; scene, (W&f0.1) jmpi L7552
0x00000005, 0x210c0208, 0x06000108, 0x0000007e, ; scene, and (1|M0) r8.3<1>:ud r8.2<0;1,0>:ud 0x7E:ud
0x01800010, 0x20000201, 0x0600010c, 0x00000020, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.3<0;1,0>:ud 0x20:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000e20, ; scene, (W&f0.1) jmpi L5888
0x01800010, 0x20000201, 0x06000108, 0x00000030, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x30:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000e90, ; scene, (W&f0.1) jmpi L6032
0x00000040, 0x21800208, 0x020004f4, 0x0000005c, ; scene, add (1|M0) r12.0<1>:ud r39.5<0;1,0>:ud r2.7<0;1,0>:ud
0x00000040, 0x21840208, 0x020004f8, 0x00000044, ; scene, add (1|M0) r12.1<1>:ud r39.6<0;1,0>:ud r2.1<0;1,0>:ud
0x00000006, 0x22000200, 0x060004fc, 0x02890000, ; scene, or (1|M0) a0.0<1>:ud r39.7<0;1,0>:ud 0x2890000:ud
0x0c800031, 0x22c00208, 0x00000180, 0x00000200, ; scene, send (16|M0) r22 r12 0xC a0.0
0x00000005, 0x210c0208, 0x060004e0, 0x00000080, ; scene, and (1|M0) r8.3<1>:ud r39.0<0;1,0>:ud 0x80:ud
0x01800010, 0x20000201, 0x0600010c, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.3<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000380, ; scene, (W&f0.1) jmpi L3312
0x00600005, 0x23c00208, 0x068d02c0, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r22.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d02c0, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r22.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d02c0, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r22.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x22c00208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r22.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x22c00208, 0x028d02c0, 0x008d0400, ; scene, or (8|M0) r22.0<1>:ud r22.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d02e0, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r23.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d02e0, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r23.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d02e0, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r23.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x22e00208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r23.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x22e00208, 0x028d02e0, 0x008d0400, ; scene, or (8|M0) r23.0<1>:ud r23.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d0300, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r24.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d0300, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r24.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d0300, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r24.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x23000208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r24.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x23000208, 0x028d0300, 0x008d0400, ; scene, or (8|M0) r24.0<1>:ud r24.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d0320, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r25.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d0320, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r25.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d0320, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r25.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x23200208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r25.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x23200208, 0x028d0320, 0x008d0400, ; scene, or (8|M0) r25.0<1>:ud r25.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d0340, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r26.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d0340, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r26.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d0340, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r26.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x23400208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r26.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x23400208, 0x028d0340, 0x008d0400, ; scene, or (8|M0) r26.0<1>:ud r26.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d0360, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r27.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d0360, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r27.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d0360, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r27.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x23600208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r27.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x23600208, 0x028d0360, 0x008d0400, ; scene, or (8|M0) r27.0<1>:ud r27.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d0380, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r28.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d0380, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r28.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d0380, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r28.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x23800208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r28.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x23800208, 0x028d0380, 0x008d0400, ; scene, or (8|M0) r28.0<1>:ud r28.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600005, 0x23c00208, 0x068d03a0, 0xff00ff00, ; scene, and (8|M0) r30.0<1>:ud r29.0<8;8,1>:ud 0xFF00FF00:ud
0x00600008, 0x23e00208, 0x068d03a0, 0x00000010, ; scene, shr (8|M0) r31.0<1>:ud r29.0<8;8,1>:ud 0x10:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x000000ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x24000208, 0x068d03a0, 0x000000ff, ; scene, and (8|M0) r32.0<1>:ud r29.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x24000208, 0x068d0400, 0x00000010, ; scene, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud
0x00600006, 0x23a00208, 0x028d03c0, 0x008d03e0, ; scene, or (8|M0) r29.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600006, 0x23a00208, 0x028d03a0, 0x008d0400, ; scene, or (8|M0) r29.0<1>:ud r29.0<8;8,1>:ud r32.0<8;8,1>:ud
0x01800010, 0x20000201, 0x06000108, 0x00000031, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x31:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000b00, ; scene, (W&f0.1) jmpi L6160
0x01800010, 0x20000201, 0x06000094, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r4.5<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x000009a0, ; scene, (W&f0.1) jmpi L5840
0x00600008, 0x23c00208, 0x068d0600, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r48.0<8;8,1>:ud 0x18:ud
0x00600006, 0x22c00208, 0x028d02c0, 0x00000098, ; scene, or (8|M0) r22.0<1>:ud r22.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d02c0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r22.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d02c0, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r22.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x22c00208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r22.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0620, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r49.0<8;8,1>:ud 0x18:ud
0x00600006, 0x22e00208, 0x028d02e0, 0x00000098, ; scene, or (8|M0) r23.0<1>:ud r23.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d02e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r23.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d02e0, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r23.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x22e00208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r23.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0640, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r50.0<8;8,1>:ud 0x18:ud
0x00600006, 0x23000208, 0x028d0300, 0x00000098, ; scene, or (8|M0) r24.0<1>:ud r24.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d0300, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r24.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0300, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r24.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x23000208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r24.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0660, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r51.0<8;8,1>:ud 0x18:ud
0x00600006, 0x23200208, 0x028d0320, 0x00000098, ; scene, or (8|M0) r25.0<1>:ud r25.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d0320, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r25.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0320, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r25.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x23200208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r25.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0680, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r52.0<8;8,1>:ud 0x18:ud
0x00600006, 0x23400208, 0x028d0340, 0x00000098, ; scene, or (8|M0) r26.0<1>:ud r26.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d0340, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r26.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0340, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r26.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x23400208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r26.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d06a0, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r53.0<8;8,1>:ud 0x18:ud
0x00600006, 0x23600208, 0x028d0360, 0x00000098, ; scene, or (8|M0) r27.0<1>:ud r27.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d0360, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r27.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0360, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r27.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x23600208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r27.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d06c0, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r54.0<8;8,1>:ud 0x18:ud
0x00600006, 0x23800208, 0x028d0380, 0x00000098, ; scene, or (8|M0) r28.0<1>:ud r28.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d0380, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r28.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0380, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r28.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x23800208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r28.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d06e0, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r55.0<8;8,1>:ud 0x18:ud
0x00600006, 0x23a00208, 0x028d03a0, 0x00000098, ; scene, or (8|M0) r29.0<1>:ud r29.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00600005, 0x23e00208, 0x068d03a0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r29.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d03a0, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r29.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x23e00208, 0x128d03e0, 0x004003c0, ; scene, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x23e00208, 0x068d03e0, 0x00800080, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud
0x00600041, 0x24000208, 0x128d0400, 0x004003c0, ; scene, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24000208, 0x068d0400, 0x00800080, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud
0x00600008, 0x24200208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x23e00208, 0x028d03e0, 0x008d0420, ; scene, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600008, 0x23e00208, 0x068d03e0, 0x00000008, ; scene, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d0400, 0x00000008, ; scene, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24000208, 0x028d0400, 0x008d0420, ; scene, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600005, 0x24000208, 0x068d0400, 0xff00ff00, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud
0x00600006, 0x23a00208, 0x028d03e0, 0x008d0400, ; scene, or (8|M0) r29.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud
0x00000001, 0x20940608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r4.5<1>:ud 0x0:ud
0x00000020, 0x34000004, 0x0e001400, 0x00000900, ; scene, (W) jmpi L8144
0x01800010, 0x20000201, 0x06000108, 0x00000004, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x4:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000750, ; scene, (W&f0.1) jmpi L7744
0x00000020, 0x34000004, 0x0e001400, 0x000008d0, ; scene, (W) jmpi L8144
0x00000040, 0x21800208, 0x020004f4, 0x0000005c, ; scene, add (1|M0) r12.0<1>:ud r39.5<0;1,0>:ud r2.7<0;1,0>:ud
0x00000040, 0x21840208, 0x020004f8, 0x00000044, ; scene, add (1|M0) r12.1<1>:ud r39.6<0;1,0>:ud r2.1<0;1,0>:ud
0x00000006, 0x22000200, 0x060004fc, 0x02890000, ; scene, or (1|M0) a0.0<1>:ud r39.7<0;1,0>:ud 0x2890000:ud
0x0c800031, 0x26000208, 0x00000180, 0x00000200, ; scene, send (16|M0) r48 r12 0xC a0.0
0x00000005, 0x210c0208, 0x060004e0, 0x00000001, ; scene, and (1|M0) r8.3<1>:ud r39.0<0;1,0>:ud 0x1:ud
0x00000040, 0x210c0208, 0x0600410c, 0x00000000, ; scene, add (1|M0) r8.3<1>:ud -r8.3<0;1,0>:ud 0x0:ud
0x00000005, 0x20980208, 0x0600010c, 0xff000000, ; scene, and (1|M0) r4.6<1>:ud r8.3<0;1,0>:ud 0xFF000000:ud
0x00000001, 0x20940608, 0x00000000, 0x00000001, ; scene, mov (1|M0) r4.5<1>:ud 0x1:ud
0x00000020, 0x34000004, 0x0e001400, 0x000019c0, ; scene, (W) jmpi L12624
0x00600001, 0x22c00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r22.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x22e00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r23.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23000208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r24.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23200208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r25.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23400208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r26.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23600208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r27.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23800208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r28.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23a00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r29.0<1>:ud r39.5<0;1,0>:ud
0x02800010, 0x20000201, 0x0600009c, 0x00000000, ; scene, cmp (16|M0) (ne)f0.1 null<1>:ud r4.7<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x000000a0, ; scene, (W&f0.1) jmpi L6352
0x00000001, 0x21600608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000001, 0x21640608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r11.1<1>:ud 0x0:ud
0x0c800031, 0x28000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r64 r11 0xC 0x02890002
0x00000001, 0x21600608, 0x00000000, 0x00000020, ; scene, mov (1|M0) r11.0<1>:ud 0x20:ud
0x0c800031, 0x29000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r72 r11 0xC 0x02890002
0x00000001, 0x21640608, 0x00000000, 0x00000008, ; scene, mov (1|M0) r11.1<1>:ud 0x8:ud
0x0c800031, 0x2b000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r88 r11 0xC 0x02890002
0x00000001, 0x21600608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r11.0<1>:ud 0x0:ud
0x0c800031, 0x2a000208, 0x06000160, 0x02890002, ; scene, send (16|M0) r80 r11 0xC 0x02890002
0x00000001, 0x209c0608, 0x00000000, 0x00000001, ; scene, mov (1|M0) r4.7<1>:ud 0x1:ud
0x00000001, 0x27000608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r56.0<1>:ud 0x0:ud
0x00000040, 0x22001240, 0x16000700, 0x02c002c0, ; scene, add (1|M0) a0.0<1>:uw r56.0<0;1,0>:uw 0x2C0:uw
0x00600001, 0x27400208, 0x008d8000, 0x00000000, ; scene, mov (8|M0) r58.0<1>:ud r[a0.0]<8;8,1>:ud
0x00000040, 0x22001240, 0x16000700, 0x06000600, ; scene, add (1|M0) a0.0<1>:uw r56.0<0;1,0>:uw 0x600:uw
0x00600001, 0x27600208, 0x008d8000, 0x00000000, ; scene, mov (8|M0) r59.0<1>:ud r[a0.0]<8;8,1>:ud
0x00000008, 0x27100208, 0x06000700, 0x00000003, ; scene, shr (1|M0) r56.4<1>:ud r56.0<0;1,0>:ud 0x3:ud
0x00000040, 0x22001240, 0x16000710, 0x01400140, ; scene, add (1|M0) a0.0<1>:uw r56.8<0;1,0>:uw 0x140:uw
0x00000001, 0x270c0208, 0x00008000, 0x00000000, ; scene, mov (1|M0) r56.3<1>:ud r[a0.0]<0;1,0>:ud
0x00600008, 0x27800208, 0x068d0760, 0x00000018, ; scene, shr (8|M0) r60.0<1>:ud r59.0<8;8,1>:ud 0x18:ud
0x00600008, 0x27a00208, 0x068d0780, 0x00000007, ; scene, shr (8|M0) r61.0<1>:ud r60.0<8;8,1>:ud 0x7:ud
0x00600040, 0x27a00208, 0x028d07a0, 0x008d0780, ; scene, add (8|M0) r61.0<1>:ud r61.0<8;8,1>:ud r60.0<8;8,1>:ud
0x00600040, 0x27c00208, 0x068d47a0, 0x00000100, ; scene, add (8|M0) r62.0<1>:ud -r61.0<8;8,1>:ud 0x100:ud
0x00600001, 0x27e00608, 0x00000000, 0x00000000, ; scene, mov (8|M0) r63.0<1>:ud 0x0:ud
0x00000001, 0x27040608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r56.1<1>:ud 0x0:ud
0x00600008, 0x2c000208, 0x028d01c0, 0x00000704, ; scene, shr (8|M0) r96.0<1>:ud r14.0<8;8,1>:ud r56.1<0;1,0>:ud
0x00600005, 0x2c000208, 0x068d0c00, 0x000000ff, ; scene, and (8|M0) r96.0<1>:ud r96.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x2c000208, 0x068d0c00, 0x00000001, ; scene, shl (8|M0) r96.0<1>:ud r96.0<8;8,1>:ud 0x1:ud
0x00600040, 0x22001240, 0x16ae0c00, 0x08000800, ; scene, add (8|M0) a0.0<1>:uw r96.0<16;8,2>:uw 0x800:uw
0x00600001, 0x2c601208, 0x01e08000, 0x00000000, ; scene, mov (8|M0) r99.0<1>:ud r[a0.0]<1,0>:uw
0x00600008, 0x2c200208, 0x028d0740, 0x00000704, ; scene, shr (8|M0) r97.0<1>:ud r58.0<8;8,1>:ud r56.1<0;1,0>:ud
0x00600005, 0x2c200208, 0x068d0c20, 0x000000ff, ; scene, and (8|M0) r97.0<1>:ud r97.0<8;8,1>:ud 0xFF:ud
0x00600009, 0x2c200208, 0x068d0c20, 0x00000001, ; scene, shl (8|M0) r97.0<1>:ud r97.0<8;8,1>:ud 0x1:ud
0x00600040, 0x22001240, 0x16ae0c20, 0x08000800, ; scene, add (8|M0) a0.0<1>:uw r97.0<16;8,2>:uw 0x800:uw
0x00600001, 0x2c801208, 0x01e08000, 0x00000000, ; scene, mov (8|M0) r100.0<1>:ud r[a0.0]<1,0>:uw
0x00600041, 0x2c600208, 0x128d0c60, 0x00ae07c0, ; scene, mul (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud r62.0<16;8,2>:uw
0x00600041, 0x2c800208, 0x128d0c80, 0x00ae07a0, ; scene, mul (8|M0) r100.0<1>:ud r100.0<8;8,1>:ud r61.0<16;8,2>:uw
0x00600040, 0x2c600208, 0x028d0c60, 0x008d0c80, ; scene, add (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud r100.0<8;8,1>:ud
0x00600040, 0x2c600208, 0x068d0c60, 0x00000080, ; scene, add (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud 0x80:ud
0x00600008, 0x2c600208, 0x068d0c60, 0x00000008, ; scene, shr (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud 0x8:ud
0x00600001, 0x2ce00608, 0x00000000, 0x00000000, ; scene, mov (8|M0) r103.0<1>:ud 0x0:ud
0x00000001, 0x27080608, 0x00000000, 0x00000080, ; scene, mov (1|M0) r56.2<1>:ud 0x80:ud
0x00600040, 0x2d000208, 0x028d0ce0, 0x00000708, ; scene, add (8|M0) r104.0<1>:ud r103.0<8;8,1>:ud r56.2<0;1,0>:ud
0x00600009, 0x2d200208, 0x068d0d00, 0x00000001, ; scene, shl (8|M0) r105.0<1>:ud r104.0<8;8,1>:ud 0x1:ud
0x00600040, 0x22001240, 0x16ae0d20, 0x0a000a00, ; scene, add (8|M0) a0.0<1>:uw r105.0<16;8,2>:uw 0xA00:uw
0x00600001, 0x2d401208, 0x01e08000, 0x00000000, ; scene, mov (8|M0) r106.0<1>:ud r[a0.0]<1,0>:uw
0x06600010, 0x20000200, 0x028d0d40, 0x008d0c60, ; scene, cmp (8|M0) (le)f0.0 null<1>:ud r106.0<8;8,1>:ud r99.0<8;8,1>:ud
0x00610001, 0x2ce00208, 0x008d0d00, 0x00000000, ; scene, (f0.0) mov (8|M0) r103.0<1>:ud r104.0<8;8,1>:ud
0x00000008, 0x27080208, 0x06000708, 0x00000001, ; scene, shr (1|M0) r56.2<1>:ud r56.2<0;1,0>:ud 0x1:ud
0x02800010, 0x20000201, 0x06000708, 0x00000000, ; scene, cmp (16|M0) (ne)f0.1 null<1>:ud r56.2<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0xffffff70, ; scene, (W&f0.1) jmpi L6848
0x00600009, 0x2ce00208, 0x028d0ce0, 0x00000704, ; scene, shl (8|M0) r103.0<1>:ud r103.0<8;8,1>:ud r56.1<0;1,0>:ud
0x00600006, 0x27e00208, 0x028d07e0, 0x008d0ce0, ; scene, or (8|M0) r63.0<1>:ud r63.0<8;8,1>:ud r103.0<8;8,1>:ud
0x00000040, 0x27040208, 0x06000704, 0x00000008, ; scene, add (1|M0) r56.1<1>:ud r56.1<0;1,0>:ud 0x8:ud
0x05800010, 0x20000201, 0x06000704, 0x00000018, ; scene, cmp (16|M0) (lt)f0.1 null<1>:ud r56.1<0;1,0>:ud 0x18:ud
0x00010020, 0x34000005, 0x0e001400, 0xfffffe10, ; scene, (W&f0.1) jmpi L6576
0x01600010, 0x20000200, 0x068d0780, 0x00000000, ; scene, cmp (8|M0) (eq)f0.0 null<1>:ud r60.0<8;8,1>:ud 0x0:ud
0x00610001, 0x27e00208, 0x008d01c0, 0x00000000, ; scene, (f0.0) mov (8|M0) r63.0<1>:ud r14.0<8;8,1>:ud
0x04600010, 0x20000200, 0x068d0780, 0x000000ff, ; scene, cmp (8|M0) (ge)f0.0 null<1>:ud r60.0<8;8,1>:ud 0xFF:ud
0x00610001, 0x27e00208, 0x008d0740, 0x00000000, ; scene, (f0.0) mov (8|M0) r63.0<1>:ud r58.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x0000070c, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r56.3<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x21c00208, 0x008d07e0, 0x00000000, ; scene, (f0.0) mov (8|M0) r14.0<1>:ud r63.0<8;8,1>:ud
0x00600001, 0x27200208, 0x008d01c0, 0x00000000, ; scene, mov (8|M0) r57.0<1>:ud r14.0<8;8,1>:ud
0x00600001, 0x21c00208, 0x008d01e0, 0x00000000, ; scene, mov (8|M0) r14.0<1>:ud r15.0<8;8,1>:ud
0x00600001, 0x21e00208, 0x008d0200, 0x00000000, ; scene, mov (8|M0) r15.0<1>:ud r16.0<8;8,1>:ud
0x00600001, 0x22000208, 0x008d0220, 0x00000000, ; scene, mov (8|M0) r16.0<1>:ud r17.0<8;8,1>:ud
0x00600001, 0x22200208, 0x008d0240, 0x00000000, ; scene, mov (8|M0) r17.0<1>:ud r18.0<8;8,1>:ud
0x00600001, 0x22400208, 0x008d0260, 0x00000000, ; scene, mov (8|M0) r18.0<1>:ud r19.0<8;8,1>:ud
0x00600001, 0x22600208, 0x008d0280, 0x00000000, ; scene, mov (8|M0) r19.0<1>:ud r20.0<8;8,1>:ud
0x00600001, 0x22800208, 0x008d02a0, 0x00000000, ; scene, mov (8|M0) r20.0<1>:ud r21.0<8;8,1>:ud
0x00600001, 0x22a00208, 0x008d0720, 0x00000000, ; scene, mov (8|M0) r21.0<1>:ud r57.0<8;8,1>:ud
0x00000040, 0x27000208, 0x06000700, 0x00000020, ; scene, add (1|M0) r56.0<1>:ud r56.0<0;1,0>:ud 0x20:ud
0x05800010, 0x20000201, 0x06000700, 0x00000100, ; scene, cmp (16|M0) (lt)f0.1 null<1>:ud r56.0<0;1,0>:ud 0x100:ud
0x00010020, 0x34000005, 0x0e001400, 0xfffffc10, ; scene, (W&f0.1) jmpi L6368
0x00000001, 0x20940608, 0x00000000, 0x00000000, ; scene, mov (1|M0) r4.5<1>:ud 0x0:ud
0x00000020, 0x34000004, 0x0e001400, 0x00001460, ; scene, (W) jmpi L12624
0x00600001, 0x22c00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r22.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x22e00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r23.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23000208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r24.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23200208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r25.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23400208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r26.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23600208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r27.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23800208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r28.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23a00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r29.0<1>:ud r39.5<0;1,0>:ud
0x00000020, 0x34000004, 0x0e001400, 0x000000c0, ; scene, (W) jmpi L7744
0x00000008, 0x210c0208, 0x060004f4, 0x00000018, ; scene, shr (1|M0) r8.3<1>:ud r39.5<0;1,0>:ud 0x18:ud
0x01800010, 0x20000201, 0x0600010c, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r8.3<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x000013a0, ; scene, (W&f0.1) jmpi L12624
0x00600001, 0x22c00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r22.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x22e00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r23.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23000208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r24.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23200208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r25.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23400208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r26.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23600208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r27.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23800208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r28.0<1>:ud r39.5<0;1,0>:ud
0x00600001, 0x23a00208, 0x000004f4, 0x00000000, ; scene, mov (8|M0) r29.0<1>:ud r39.5<0;1,0>:ud
0x00000020, 0x34000004, 0x0e001400, 0x00000190, ; scene, (W) jmpi L8144
0x00600006, 0x24c00208, 0x028d0120, 0x00000140, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.0<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x21c00208, 0x008d02c0, 0x00000000, ; scene, (f0.0) mov (8|M0) r14.0<1>:ud r22.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000144, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.1<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x21e00208, 0x008d02e0, 0x00000000, ; scene, (f0.0) mov (8|M0) r15.0<1>:ud r23.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000148, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.2<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22000208, 0x008d0300, 0x00000000, ; scene, (f0.0) mov (8|M0) r16.0<1>:ud r24.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x0000014c, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.3<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22200208, 0x008d0320, 0x00000000, ; scene, (f0.0) mov (8|M0) r17.0<1>:ud r25.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000150, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.4<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22400208, 0x008d0340, 0x00000000, ; scene, (f0.0) mov (8|M0) r18.0<1>:ud r26.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000154, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.5<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22600208, 0x008d0360, 0x00000000, ; scene, (f0.0) mov (8|M0) r19.0<1>:ud r27.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000158, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.6<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22800208, 0x008d0380, 0x00000000, ; scene, (f0.0) mov (8|M0) r20.0<1>:ud r28.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x0000015c, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.7<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22a00208, 0x008d03a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r21.0<1>:ud r29.0<8;8,1>:ud
0x00000020, 0x34000004, 0x0e001400, 0x00001180, ; scene, (W) jmpi L12624
0x00600008, 0x23c00208, 0x068d02c0, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r22.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d02c0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r22.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d02c0, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r22.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d01c0, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r14.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d01c0, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r14.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000140, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.0<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x21c00208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r14.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d02e0, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r23.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d02e0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r23.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d02e0, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r23.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d01e0, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r15.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d01e0, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r15.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000144, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.1<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x21e00208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r15.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0300, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r24.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d0300, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r24.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0300, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r24.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d0200, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r16.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d0200, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r16.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000148, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.2<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22000208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r16.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0320, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r25.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d0320, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r25.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0320, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r25.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d0220, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r17.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d0220, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r17.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x0000014c, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.3<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22200208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r17.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0340, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r26.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d0340, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r26.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0340, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r26.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d0240, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r18.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d0240, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r18.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000150, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.4<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22400208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r18.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0360, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r27.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d0360, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r27.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0360, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r27.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d0260, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r19.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d0260, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r19.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000154, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.5<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22600208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r19.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d0380, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r28.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d0380, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r28.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0380, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r28.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d0280, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r20.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d0280, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r20.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x00000158, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.6<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22800208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r20.0<1>:ud r37.0<8;8,1>:ud
0x00600008, 0x23c00208, 0x068d03a0, 0x00000018, ; scene, shr (8|M0) r30.0<1>:ud r29.0<8;8,1>:ud 0x18:ud
0x00600040, 0x23c00208, 0x068d43c0, 0x000000ff, ; scene, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud
0x00600005, 0x23e00208, 0x068d03a0, 0x00ff00ff, ; scene, and (8|M0) r31.0<1>:ud r29.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d03a0, 0x00000008, ; scene, shr (8|M0) r32.0<1>:ud r29.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; scene, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x24200208, 0x068d02a0, 0x00ff00ff, ; scene, and (8|M0) r33.0<1>:ud r21.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d02a0, 0x00000008, ; scene, shr (8|M0) r34.0<1>:ud r21.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; scene, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600041, 0x24200208, 0x128d0420, 0x004003c0, ; scene, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24200208, 0x068d0420, 0x007f007f, ; scene, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud
0x00600041, 0x24400208, 0x128d0440, 0x004003c0, ; scene, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw
0x00600040, 0x24400208, 0x068d0440, 0x007f007f, ; scene, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud
0x00600008, 0x24600208, 0x068d0420, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d0420, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud
0x00600040, 0x24600208, 0x068d0460, 0x00010001, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24600208, 0x068d0460, 0x00000008, ; scene, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; scene, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0440, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0440, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud
0x00600040, 0x24800208, 0x068d0480, 0x00010001, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud
0x00600008, 0x24800208, 0x068d0480, 0x00000008, ; scene, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; scene, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600040, 0x24600208, 0x028d0460, 0x008d03e0, ; scene, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00600040, 0x24800208, 0x028d0480, 0x008d0400, ; scene, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud
0x05600002, 0x2460124b, 0x168d0460, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2470124b, 0x168d0470, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x05600002, 0x2480124b, 0x168d0480, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw
0x05600002, 0x2490124b, 0x168d0490, 0x00ff00ff, ; scene, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; scene, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x24a00208, 0x028d0460, 0x008d0480, ; scene, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00600006, 0x24c00208, 0x028d0120, 0x0000015c, ; scene, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.7<0;1,0>:ud
0x04600010, 0x20000a20, 0x0e8d04c0, 0x00000000, ; scene, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d
0x00610001, 0x22a00208, 0x008d04a0, 0x00000000, ; scene, (f0.0) mov (8|M0) r21.0<1>:ud r37.0<8;8,1>:ud
0x00000040, 0x20500208, 0x06000050, 0x00000001, ; scene, add (1|M0) r2.4<1>:ud r2.4<0;1,0>:ud 0x1:ud
0x00000020, 0x34000004, 0x0e001400, 0xffffd3f0, ; scene, (W) jmpi L1376
0x01800010, 0x20000201, 0x06000090, 0x00000000, ; scene, cmp (16|M0) (eq)f0.1 null<1>:ud r4.4<0;1,0>:ud 0x0:ud
0x00010020, 0x34000005, 0x0e001400, 0x00000010, ; scene, (W&f0.1) jmpi L12704
0x0c800031, 0x20000240, 0x060001a0, 0x120a8000, ; scene, send (16|M0) null:uw r13 0xC 0x120A8000
0x00000040, 0x20400208, 0x06000040, 0x00000008, ; scene, add (1|M0) r2.0<1>:ud r2.0<0;1,0>:ud 0x8:ud
0x00000040, 0x21a00208, 0x060001a0, 0x00000020, ; scene, add (1|M0) r13.0<1>:ud r13.0<0;1,0>:ud 0x20:ud
0x05800010, 0x20000201, 0x02000040, 0x00000080, ; scene, cmp (16|M0) (lt)f0.1 null<1>:ud r2.0<0;1,0>:ud r4.0<0;1,0>:ud
0x00010020, 0x34000005, 0x0e001400, 0xffffd0b0, ; scene, (W&f0.1) jmpi L656
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; scene, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x07800031, 0x20000a40, 0x06000e00, 0x82000010 ; scene, send (16|M0) null:uw r112:d 0x27 0x02000010 {EOT}

; cp (op 4): the scene of fills (kind 0) and tones (kind 1) over the whole picture, of any width, at a pitch that is a multiple of 16
; pixels (64 bytes: rows sharing a 64-byte line raced on the card). One thread a 16-byte block of a row: its 64 by 64 cell's records (the
; bins table on entry 1, 8-byte media block reads, the records in scene order) laid over the block as the processor's scene does (a fill
; writes its colour, a tone lays it by ui_over, a tone of alpha 0 leaves the pixel as it was), pixels past w left as they were. Written in
; Gen9 assembly, src/canvas/gpu_i915_cp.iga (iga64 -a -p=9, 127 instructions).

; cp (op 4): the scene of fills (kind 0) and tones (kind 1) over the whole picture, of any width, at a pitch that is a multiple of 16
; pixels (64 bytes: rows sharing a 64-byte line raced on the card). One thread a 16-byte block of a row: its 64 by 64 cell's records (the
; bins table on entry 1, 8-byte media block reads, the records in scene order) laid over the block as the processor's scene does (a fill
; writes its colour, a tone lays it by ui_over, a tone of alpha 0 leaves the pixel as it was), pixels past w left as they were. Written in
; Gen9 assembly, src/canvas/gpu_i915_cp.iga (iga64 -a -p=9, 127 instructions).
kernel cp
0x00000001, 0x27800208, 0x00000020, 0x00000000, ; cp, word 0
0x00000001, 0x27840208, 0x00000024, 0x00000000, ; cp, word 16
0x00000001, 0x27880208, 0x00000028, 0x00000000, ; cp, word 32
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; cp, word 48
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; cp, word 64
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; cp, word 80
0x00200001, 0x20800208, 0x00450040, 0x00000000, ; cp, word 96
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; cp, word 112
0x0c800031, 0x20a00208, 0x06000080, 0x02190000, ; cp, word 128
0x00400001, 0x20c00208, 0x002000a0, 0x00000000, ; cp, word 144
0x00000009, 0x27a00208, 0x06000004, 0x00000002, ; cp, word 160
0x00000008, 0x27a40208, 0x06000004, 0x00000004, ; cp, word 176
0x00000008, 0x27a80208, 0x06000018, 0x00000006, ; cp, word 192
0x00000041, 0x27ac0208, 0x120007a8, 0x00000784, ; cp, word 208
0x00000040, 0x27ac0208, 0x020007ac, 0x000007a4, ; cp, word 224
0x00000009, 0x27b00208, 0x060007ac, 0x00000003, ; cp, word 240
0x00000005, 0x27c00208, 0x060007b0, 0x00003fff, ; cp, word 256
0x00000008, 0x27c40208, 0x060007b0, 0x0000000e, ; cp, word 272
0x00600001, 0x25000208, 0x008d0000, 0x00000000, ; cp, word 288
0x00200001, 0x25000208, 0x004507c0, 0x00000000, ; cp, word 304
0x00000001, 0x25080608, 0x00000000, 0x00000007, ; cp, word 320
0x0c800031, 0x25200208, 0x06000500, 0x02190001, ; cp, word 336
0x00000001, 0x27e00208, 0x00000520, 0x00000000, ; cp, word 352
0x00000001, 0x27e40208, 0x00000524, 0x00000000, ; cp, word 368
0x00000009, 0x27e80208, 0x060007e0, 0x00000005, ; cp, word 384
0x00000040, 0x27e80208, 0x020007e8, 0x00000788, ; cp, word 400
0x00000001, 0x2b800608, 0x00000000, 0x00000000, ; cp, word 416
0x00000001, 0x2b840608, 0x00000000, 0x00000001, ; cp, word 432
0x00000001, 0x2b880608, 0x00000000, 0x00000002, ; cp, word 448
0x00000001, 0x2b8c0608, 0x00000000, 0x00000003, ; cp, word 464
0x00400040, 0x2b600208, 0x02690b80, 0x000007a0, ; cp, word 480
0x01000010, 0x20000202, 0x060007e4, 0x00000000, ; cp, word 496
0x00010020, 0x34000006, 0x0e001400, 0x000005b0, ; cp, word 512
0x00000040, 0x28000208, 0x060007e8, 0x00000000, ; cp, word 528
0x00000005, 0x28040208, 0x06000800, 0x00003fff, ; cp, word 544
0x00000008, 0x28080208, 0x06000800, 0x0000000e, ; cp, word 560
0x00600001, 0x25800208, 0x008d0000, 0x00000000, ; cp, word 576
0x00200001, 0x25800208, 0x00450804, 0x00000000, ; cp, word 592
0x00000001, 0x25880608, 0x00000000, 0x00000007, ; cp, word 608
0x0c800031, 0x28c00208, 0x06000580, 0x02190001, ; cp, word 624
0x00000040, 0x28000208, 0x060007e8, 0x00000008, ; cp, word 640
0x00000005, 0x28040208, 0x06000800, 0x00003fff, ; cp, word 656
0x00000008, 0x28080208, 0x06000800, 0x0000000e, ; cp, word 672
0x00600001, 0x25a00208, 0x008d0000, 0x00000000, ; cp, word 688
0x00200001, 0x25a00208, 0x00450804, 0x00000000, ; cp, word 704
0x00000001, 0x25a80608, 0x00000000, 0x00000007, ; cp, word 720
0x0c800031, 0x28e00208, 0x060005a0, 0x02190001, ; cp, word 736
0x00000040, 0x28000208, 0x060007e8, 0x00000010, ; cp, word 752
0x00000005, 0x28040208, 0x06000800, 0x00003fff, ; cp, word 768
0x00000008, 0x28080208, 0x06000800, 0x0000000e, ; cp, word 784
0x00600001, 0x25c00208, 0x008d0000, 0x00000000, ; cp, word 800
0x00200001, 0x25c00208, 0x00450804, 0x00000000, ; cp, word 816
0x00000001, 0x25c80608, 0x00000000, 0x00000007, ; cp, word 832
0x0c800031, 0x29000208, 0x060005c0, 0x02190001, ; cp, word 848
0x00000040, 0x28000208, 0x060007e8, 0x00000018, ; cp, word 864
0x00000005, 0x28040208, 0x06000800, 0x00003fff, ; cp, word 880
0x00000008, 0x28080208, 0x06000800, 0x0000000e, ; cp, word 896
0x00600001, 0x25e00208, 0x008d0000, 0x00000000, ; cp, word 912
0x00200001, 0x25e00208, 0x00450804, 0x00000000, ; cp, word 928
0x00000001, 0x25e80608, 0x00000000, 0x00000007, ; cp, word 944
0x0c800031, 0x29200208, 0x060005e0, 0x02190001, ; cp, word 960
0x00000001, 0x2a000208, 0x000008c0, 0x00000000, ; cp, word 976
0x00000001, 0x2a040208, 0x000008c4, 0x00000000, ; cp, word 992
0x00000001, 0x2a080208, 0x000008e0, 0x00000000, ; cp, word 1008
0x00000001, 0x2a0c0208, 0x000008e4, 0x00000000, ; cp, word 1024
0x00000001, 0x2a100208, 0x00000900, 0x00000000, ; cp, word 1040
0x00000001, 0x2a140208, 0x00000904, 0x00000000, ; cp, word 1056
0x00000008, 0x2a200208, 0x06000a14, 0x00000018, ; cp, word 1072
0x00000040, 0x2a800208, 0x06004a20, 0x000000ff, ; cp, word 1088
0x00000005, 0x2aa00208, 0x06000a14, 0x00ff00ff, ; cp, word 1104
0x00000008, 0x2ac00208, 0x06000a14, 0x00000008, ; cp, word 1120
0x00000005, 0x2ac00208, 0x06000ac0, 0x00ff00ff, ; cp, word 1136
0x05000002, 0x2a240a28, 0x0a000a0c, 0x00000780, ; cp, word 1152
0x04000002, 0x2a240a28, 0x0a000a24, 0x00000a04, ; cp, word 1168
0x00000040, 0x2a280208, 0x02000a24, 0x00004a04, ; cp, word 1184
0x00000040, 0x2a400208, 0x02000018, 0x00004a08, ; cp, word 1200
0x04000002, 0x2a440a28, 0x0a000a10, 0x00000a08, ; cp, word 1216
0x00000040, 0x2a440208, 0x02000a44, 0x00004a08, ; cp, word 1232
0x00000001, 0x2a480608, 0x00000000, 0x00000000, ; cp, word 1248
0x05000010, 0x20000200, 0x02000a40, 0x00000a44, ; cp, word 1264
0x00010001, 0x2a480208, 0x00000a28, 0x00000000, ; cp, word 1280
0x01000010, 0x20000200, 0x06000a20, 0x00000000, ; cp, word 1296
0x00000001, 0x2a600608, 0x00000000, 0x00000000, ; cp, word 1312
0x00010001, 0x2a600608, 0x00000000, 0x00000001, ; cp, word 1328
0x00000005, 0x2a600208, 0x02000a60, 0x00000a00, ; cp, word 1344
0x02000010, 0x20000200, 0x06000a60, 0x00000000, ; cp, word 1360
0x00010001, 0x2a480608, 0x00000000, 0x00000000, ; cp, word 1376
0x00400040, 0x2b400208, 0x02690b60, 0x00004a04, ; cp, word 1392
0x05400010, 0x20000200, 0x02690b40, 0x00000a48, ; cp, word 1408
0x01000010, 0x20000202, 0x06000a00, 0x00000000, ; cp, word 1424
0x00010020, 0x34000006, 0x0e001400, 0x000001c0, ; cp, word 1440
0x00400005, 0x21400208, 0x062000a0, 0x00ff00ff, ; cp, word 1456
0x00400008, 0x21600208, 0x062000a0, 0x00000008, ; cp, word 1472
0x00400005, 0x21600208, 0x06200160, 0x00ff00ff, ; cp, word 1488
0x00400041, 0x21400208, 0x12200140, 0x00000a80, ; cp, word 1504
0x00400040, 0x21400208, 0x06200140, 0x007f007f, ; cp, word 1520
0x00400041, 0x21600208, 0x12200160, 0x00000a80, ; cp, word 1536
0x00400040, 0x21600208, 0x06200160, 0x007f007f, ; cp, word 1552
0x00400008, 0x21800208, 0x06200140, 0x00000008, ; cp, word 1568
0x00400005, 0x21800208, 0x06200180, 0x00ff00ff, ; cp, word 1584
0x00400040, 0x21800208, 0x02200180, 0x00200140, ; cp, word 1600
0x00400040, 0x21800208, 0x06200180, 0x00010001, ; cp, word 1616
0x00400008, 0x21800208, 0x06200180, 0x00000008, ; cp, word 1632
0x00400005, 0x21800208, 0x06200180, 0x00ff00ff, ; cp, word 1648
0x00400008, 0x21a00208, 0x06200160, 0x00000008, ; cp, word 1664
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; cp, word 1680
0x00400040, 0x21a00208, 0x022001a0, 0x00200160, ; cp, word 1696
0x00400040, 0x21a00208, 0x062001a0, 0x00010001, ; cp, word 1712
0x00400008, 0x21a00208, 0x062001a0, 0x00000008, ; cp, word 1728
0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff, ; cp, word 1744
0x00400040, 0x21800208, 0x02200180, 0x00000aa0, ; cp, word 1760
0x00400040, 0x21a00208, 0x022001a0, 0x00000ac0, ; cp, word 1776
0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff, ; cp, word 1792
0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff, ; cp, word 1808
0x00400009, 0x21a00208, 0x062001a0, 0x00000008, ; cp, word 1824
0x00400006, 0x21e00208, 0x02200180, 0x002001a0, ; cp, word 1840
0x05400010, 0x20000200, 0x02690b40, 0x00000a48, ; cp, word 1856
0x00410001, 0x20a00208, 0x002001e0, 0x00000000, ; cp, word 1872
0x00000020, 0x34000004, 0x0e001400, 0x00000010, ; cp, word 1888
0x00410001, 0x20a00208, 0x00000a14, 0x00000000, ; cp, word 1904
0x00000040, 0x27e40208, 0x060007e4, 0xffffffff, ; cp, word 1920
0x00000040, 0x27e80208, 0x060007e8, 0x00000020, ; cp, word 1936
0x02000010, 0x20000202, 0x060007e4, 0x00000000, ; cp, word 1952
0x00010020, 0x34000006, 0x0e001400, 0xfffffa50, ; cp, word 1968
0x0c800031, 0x20000a40, 0x06000080, 0x060a8000, ; cp, word 1984
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; cp, word 2000
0x07800031, 0x20000a40, 0x06000e00, 0x82000010, ; cp, word 2016
; scale (op 7): d[y * dp + x] = s[ytab[y] * sp + xtab[x]] for x below w and y below the rows, 4 bytes a pixel, nearest, the tables made by the
; processor (xtab's w entries at the CURBE's table base, ytab's rows after them, each the source's index). Four pixels a thread, one 16-byte block
; of the row. Every surface is read and written linear, as a 2D surface 16384 bytes a row (byte B at x = B & 16383, y = B >> 14), so the pitch
; of the source is any and the destination's is whole 16-byte groups. The tables are read on binding table entry 2 (block reads), the source on
; entry 1 one pixel a block (its first dword is the pixel), the destination on entry 0 (read and merged, then written whole, so the last block of a
; row keeps the bytes past the row). Written in Gen9 assembly, src/canvas/gpu_i915_scale.iga (iga64 -a -p=9); the CURBE is sp (a word), w, dp (a word).
kernel scale
0x00000041, 0x20400208, 0x06000004, 0x00000010, ; scale, mul (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x10:ud
0x00000001, 0x20440208, 0x00000018, 0x00000000, ; scale, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00600001, 0x20c00208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r6.0<1>:ud r0.0<8;8,1>:ud
0x00000001, 0x20c80608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r6.2<1>:ud 0xF:ud
0x00000005, 0x20c00208, 0x06000040, 0x00003fff, ; scale, and (1|M0) r6.0<1>:ud r2.0<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x20c40208, 0x06000040, 0x0000000e, ; scale, shr (1|M0) r6.1<1>:ud r2.0<0;1,0>:ud 0xE:ud
0x0c800031, 0x20a00208, 0x060000c0, 0x02190002, ; scale, send (16|M0) r5 r6 0xC 0x02190002
0x00000040, 0x20600208, 0x02000024, 0x00000044, ; scale, add (1|M0) r3.0<1>:ud r1.1<0;1,0>:ud r2.1<0;1,0>:ud
0x00000009, 0x20600208, 0x06000060, 0x00000002, ; scale, shl (1|M0) r3.0<1>:ud r3.0<0;1,0>:ud 0x2:ud
0x00600001, 0x21000208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r8.0<1>:ud r0.0<8;8,1>:ud
0x00000001, 0x21080608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r8.2<1>:ud 0xF:ud
0x00000005, 0x21000208, 0x06000060, 0x00003fff, ; scale, and (1|M0) r8.0<1>:ud r3.0<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x21040208, 0x06000060, 0x0000000e, ; scale, shr (1|M0) r8.1<1>:ud r3.0<0;1,0>:ud 0xE:ud
0x0c800031, 0x20e00208, 0x06000100, 0x02190002, ; scale, send (16|M0) r7 r8 0xC 0x02190002
0x00000041, 0x21200208, 0x120000e0, 0x00000020, ; scale, mul (1|M0) r9.0<1>:ud r7.0<0;1,0>:ud r1.0<0;1,0>:uw
0x00000008, 0x20640208, 0x06000040, 0x00000002, ; scale, shr (1|M0) r3.1<1>:ud r2.0<0;1,0>:ud 0x2:ud
0x00000040, 0x20600a28, 0x0a000024, 0x00004064, ; scale, add (1|M0) r3.0<1>:d r1.1<0;1,0>:d -r3.1<0;1,0>:d
0x05000002, 0x20680a28, 0x0e000060, 0x00000004, ; scale, sel (1|M0) (lt)f0.0 r3.2<1>:d r3.0<0;1,0>:d 4:d
0x04000002, 0x20680a28, 0x0e000068, 0x00000000, ; scale, sel (1|M0) (ge)f0.0 r3.2<1>:d r3.2<0;1,0>:d 0:d
0x00000040, 0x27800208, 0x02000120, 0x000000a0, ; scale, add (1|M0) r60.0<1>:ud r9.0<0;1,0>:ud r5.0<0;1,0>:ud
0x00000009, 0x27800208, 0x06000780, 0x00000002, ; scale, shl (1|M0) r60.0<1>:ud r60.0<0;1,0>:ud 0x2:ud
0x06000010, 0x20000a20, 0x0e000068, 0x00000000, ; scale, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 0:d
0x00010001, 0x27800608, 0x00000000, 0x00000000, ; scale, (f0.0) mov (1|M0) r60.0<1>:ud 0x0:ud
0x00600001, 0x23c00208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r30.0<1>:ud r0.0<8;8,1>:ud
0x00000005, 0x23c00208, 0x06000780, 0x00003fff, ; scale, and (1|M0) r30.0<1>:ud r60.0<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x23c40208, 0x06000780, 0x0000000e, ; scale, shr (1|M0) r30.1<1>:ud r60.0<0;1,0>:ud 0xE:ud
0x00000001, 0x23c80608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r30.2<1>:ud 0xF:ud
0x0c800031, 0x25000208, 0x060003c0, 0x02190001, ; scale, send (16|M0) r40 r30 0xC 0x02190001
0x00000040, 0x27a00208, 0x02000120, 0x000000a4, ; scale, add (1|M0) r61.0<1>:ud r9.0<0;1,0>:ud r5.1<0;1,0>:ud
0x00000009, 0x27a00208, 0x060007a0, 0x00000002, ; scale, shl (1|M0) r61.0<1>:ud r61.0<0;1,0>:ud 0x2:ud
0x06000010, 0x20000a20, 0x0e000068, 0x00000001, ; scale, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 1:d
0x00010001, 0x27a00608, 0x00000000, 0x00000000, ; scale, (f0.0) mov (1|M0) r61.0<1>:ud 0x0:ud
0x00600001, 0x23e00208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r31.0<1>:ud r0.0<8;8,1>:ud
0x00000005, 0x23e00208, 0x060007a0, 0x00003fff, ; scale, and (1|M0) r31.0<1>:ud r61.0<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x23e40208, 0x060007a0, 0x0000000e, ; scale, shr (1|M0) r31.1<1>:ud r61.0<0;1,0>:ud 0xE:ud
0x00000001, 0x23e80608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r31.2<1>:ud 0xF:ud
0x0c800031, 0x25200208, 0x060003e0, 0x02190001, ; scale, send (16|M0) r41 r31 0xC 0x02190001
0x00000040, 0x27c00208, 0x02000120, 0x000000a8, ; scale, add (1|M0) r62.0<1>:ud r9.0<0;1,0>:ud r5.2<0;1,0>:ud
0x00000009, 0x27c00208, 0x060007c0, 0x00000002, ; scale, shl (1|M0) r62.0<1>:ud r62.0<0;1,0>:ud 0x2:ud
0x06000010, 0x20000a20, 0x0e000068, 0x00000002, ; scale, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 2:d
0x00010001, 0x27c00608, 0x00000000, 0x00000000, ; scale, (f0.0) mov (1|M0) r62.0<1>:ud 0x0:ud
0x00600001, 0x24000208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r32.0<1>:ud r0.0<8;8,1>:ud
0x00000005, 0x24000208, 0x060007c0, 0x00003fff, ; scale, and (1|M0) r32.0<1>:ud r62.0<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x24040208, 0x060007c0, 0x0000000e, ; scale, shr (1|M0) r32.1<1>:ud r62.0<0;1,0>:ud 0xE:ud
0x00000001, 0x24080608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r32.2<1>:ud 0xF:ud
0x0c800031, 0x25400208, 0x06000400, 0x02190001, ; scale, send (16|M0) r42 r32 0xC 0x02190001
0x00000040, 0x27e00208, 0x02000120, 0x000000ac, ; scale, add (1|M0) r63.0<1>:ud r9.0<0;1,0>:ud r5.3<0;1,0>:ud
0x00000009, 0x27e00208, 0x060007e0, 0x00000002, ; scale, shl (1|M0) r63.0<1>:ud r63.0<0;1,0>:ud 0x2:ud
0x06000010, 0x20000a20, 0x0e000068, 0x00000003, ; scale, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 3:d
0x00010001, 0x27e00608, 0x00000000, 0x00000000, ; scale, (f0.0) mov (1|M0) r63.0<1>:ud 0x0:ud
0x00600001, 0x24200208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r33.0<1>:ud r0.0<8;8,1>:ud
0x00000005, 0x24200208, 0x060007e0, 0x00003fff, ; scale, and (1|M0) r33.0<1>:ud r63.0<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x24240208, 0x060007e0, 0x0000000e, ; scale, shr (1|M0) r33.1<1>:ud r63.0<0;1,0>:ud 0xE:ud
0x00000001, 0x24280608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r33.2<1>:ud 0xF:ud
0x0c800031, 0x25600208, 0x06000420, 0x02190001, ; scale, send (16|M0) r43 r33 0xC 0x02190001
0x00000041, 0x21280208, 0x12000044, 0x00000028, ; scale, mul (1|M0) r9.2<1>:ud r2.1<0;1,0>:ud r1.4<0;1,0>:uw
0x00000009, 0x21280208, 0x06000128, 0x00000002, ; scale, shl (1|M0) r9.2<1>:ud r9.2<0;1,0>:ud 0x2:ud
0x00000040, 0x21280208, 0x02000128, 0x00000040, ; scale, add (1|M0) r9.2<1>:ud r9.2<0;1,0>:ud r2.0<0;1,0>:ud
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00000005, 0x20800208, 0x06000128, 0x00003fff, ; scale, and (1|M0) r4.0<1>:ud r9.2<0;1,0>:ud 0x3FFF:ud
0x00000008, 0x20840208, 0x06000128, 0x0000000e, ; scale, shr (1|M0) r4.1<1>:ud r9.2<0;1,0>:ud 0xE:ud
0x00000001, 0x20880608, 0x00000000, 0x0000000f, ; scale, mov (1|M0) r4.2<1>:ud 0xF:ud
0x0c800031, 0x26400208, 0x06000080, 0x02190000, ; scale, send (16|M0) r50 r4 0xC 0x02190000
0x00400001, 0x20a00208, 0x00200640, 0x00000000, ; scale, mov (4|M0) r5.0<1>:ud r50.0<1;1,0>:ud
0x03000010, 0x20000a20, 0x0e000068, 0x00000000, ; scale, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 0:d
0x00010001, 0x20a00208, 0x00000500, 0x00000000, ; scale, (f0.0) mov (1|M0) r5.0<1>:ud r40.0<0;1,0>:ud
0x03000010, 0x20000a20, 0x0e000068, 0x00000001, ; scale, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 1:d
0x00010001, 0x20a40208, 0x00000520, 0x00000000, ; scale, (f0.0) mov (1|M0) r5.1<1>:ud r41.0<0;1,0>:ud
0x03000010, 0x20000a20, 0x0e000068, 0x00000002, ; scale, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 2:d
0x00010001, 0x20a80208, 0x00000540, 0x00000000, ; scale, (f0.0) mov (1|M0) r5.2<1>:ud r42.0<0;1,0>:ud
0x03000010, 0x20000a20, 0x0e000068, 0x00000003, ; scale, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 3:d
0x00010001, 0x20ac0208, 0x00000560, 0x00000000, ; scale, (f0.0) mov (1|M0) r5.3<1>:ud r43.0<0;1,0>:ud
0x0c800031, 0x20000a40, 0x06000080, 0x060a8000, ; scale, send (16|M0) null:uw r4:d 0xC 0x060A8000
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; scale, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x07800031, 0x20000a40, 0x06000e00, 0x82000010, ; scale, send (16|M0) null:uw r112:d 0x27 0x02000010 {EOT}

; halve (op 10): the scene under a glass window made its small picture, a small pixel the mean of its block of 8 by 8 as memory_halve_u32 three
; times makes it (the rows of a pair rounded up, the columns down). Four small pixels a thread: four blocks read on entry 1, 16 bytes written on
; entry 0. src/canvas/gpu_i915_halve.iga, assembled with iga64 -a -p=9.

kernel halve
0x00600001, 0x20600208, 0x008d0000, 0x00000000, ; halve, mov (8|M0) r3.0<1>:ud r0.0<8;8,1>:ud
0x00000009, 0x20600208, 0x06000004, 0x00000008, ; halve, shl (1|M0) r3.0<1>:ud r0.1<0;1,0>:ud 0x8:ud
0x00000009, 0x20640208, 0x06000018, 0x00000003, ; halve, shl (1|M0) r3.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000001, 0x20680608, 0x00000000, 0x0007001f, ; halve, mov (1|M0) r3.2<1>:ud 0x7001F:ud
0x00000001, 0x20480608, 0x00000000, 0x00000000, ; halve, mov (1|M0) r2.2<1>:ud 0x0:ud
0x0c800031, 0x21400208, 0x06000060, 0x02890001, ; halve, send (16|M0) r10 r3 0xC 0x02890001
0x00600005, 0x22800208, 0x068d0140, 0x00ff00ff, ; halve, and (8|M0) r20.0<1>:ud r10.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x23c00208, 0x068d0140, 0x00000008, ; halve, shr (8|M0) r30.0<1>:ud r10.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23c00208, 0x068d03c0, 0x00ff00ff, ; halve, and (8|M0) r30.0<1>:ud r30.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x22a00208, 0x068d0160, 0x00ff00ff, ; halve, and (8|M0) r21.0<1>:ud r11.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x23e00208, 0x068d0160, 0x00000008, ; halve, shr (8|M0) r31.0<1>:ud r11.0<8;8,1>:ud 0x8:ud
0x00600005, 0x23e00208, 0x068d03e0, 0x00ff00ff, ; halve, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x22c00208, 0x068d0180, 0x00ff00ff, ; halve, and (8|M0) r22.0<1>:ud r12.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24000208, 0x068d0180, 0x00000008, ; halve, shr (8|M0) r32.0<1>:ud r12.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24000208, 0x068d0400, 0x00ff00ff, ; halve, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x22e00208, 0x068d01a0, 0x00ff00ff, ; halve, and (8|M0) r23.0<1>:ud r13.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24200208, 0x068d01a0, 0x00000008, ; halve, shr (8|M0) r33.0<1>:ud r13.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24200208, 0x068d0420, 0x00ff00ff, ; halve, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x23000208, 0x068d01c0, 0x00ff00ff, ; halve, and (8|M0) r24.0<1>:ud r14.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24400208, 0x068d01c0, 0x00000008, ; halve, shr (8|M0) r34.0<1>:ud r14.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24400208, 0x068d0440, 0x00ff00ff, ; halve, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x23200208, 0x068d01e0, 0x00ff00ff, ; halve, and (8|M0) r25.0<1>:ud r15.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24600208, 0x068d01e0, 0x00000008, ; halve, shr (8|M0) r35.0<1>:ud r15.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24600208, 0x068d0460, 0x00ff00ff, ; halve, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x23400208, 0x068d0200, 0x00ff00ff, ; halve, and (8|M0) r26.0<1>:ud r16.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24800208, 0x068d0200, 0x00000008, ; halve, shr (8|M0) r36.0<1>:ud r16.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24800208, 0x068d0480, 0x00ff00ff, ; halve, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud
0x00600005, 0x23600208, 0x068d0220, 0x00ff00ff, ; halve, and (8|M0) r27.0<1>:ud r17.0<8;8,1>:ud 0xFF00FF:ud
0x00600008, 0x24a00208, 0x068d0220, 0x00000008, ; halve, shr (8|M0) r37.0<1>:ud r17.0<8;8,1>:ud 0x8:ud
0x00600005, 0x24a00208, 0x068d04a0, 0x00ff00ff, ; halve, and (8|M0) r37.0<1>:ud r37.0<8;8,1>:ud 0xFF00FF:ud
0x00800042, 0x25001248, 0x12b10280, 0x00b102a0, ; halve, avg (16|M0) r40.0<1>:uw r20.0<16;16,1>:uw r21.0<16;16,1>:uw
0x00400040, 0x26000208, 0x02400500, 0x00400504, ; halve, add (4|M0) r48.0<1>:ud r40.0<2;1,0>:ud r40.1<2;1,0>:ud
0x00600008, 0x26001248, 0x168d0600, 0x00010001, ; halve, shr (8|M0) r48.0<1>:uw r48.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25201248, 0x12b102c0, 0x00b102e0, ; halve, avg (16|M0) r41.0<1>:uw r22.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00400040, 0x26200208, 0x02400520, 0x00400524, ; halve, add (4|M0) r49.0<1>:ud r41.0<2;1,0>:ud r41.1<2;1,0>:ud
0x00600008, 0x26201248, 0x168d0620, 0x00010001, ; halve, shr (8|M0) r49.0<1>:uw r49.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25401248, 0x12b10300, 0x00b10320, ; halve, avg (16|M0) r42.0<1>:uw r24.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00400040, 0x26400208, 0x02400540, 0x00400544, ; halve, add (4|M0) r50.0<1>:ud r42.0<2;1,0>:ud r42.1<2;1,0>:ud
0x00600008, 0x26401248, 0x168d0640, 0x00010001, ; halve, shr (8|M0) r50.0<1>:uw r50.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25601248, 0x12b10340, 0x00b10360, ; halve, avg (16|M0) r43.0<1>:uw r26.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00400040, 0x26600208, 0x02400560, 0x00400564, ; halve, add (4|M0) r51.0<1>:ud r43.0<2;1,0>:ud r43.1<2;1,0>:ud
0x00600008, 0x26601248, 0x168d0660, 0x00010001, ; halve, shr (8|M0) r51.0<1>:uw r51.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25801248, 0x12b103c0, 0x00b103e0, ; halve, avg (16|M0) r44.0<1>:uw r30.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00400040, 0x26800208, 0x02400580, 0x00400584, ; halve, add (4|M0) r52.0<1>:ud r44.0<2;1,0>:ud r44.1<2;1,0>:ud
0x00600008, 0x26801248, 0x168d0680, 0x00010001, ; halve, shr (8|M0) r52.0<1>:uw r52.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25a01248, 0x12b10400, 0x00b10420, ; halve, avg (16|M0) r45.0<1>:uw r32.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00400040, 0x26a00208, 0x024005a0, 0x004005a4, ; halve, add (4|M0) r53.0<1>:ud r45.0<2;1,0>:ud r45.1<2;1,0>:ud
0x00600008, 0x26a01248, 0x168d06a0, 0x00010001, ; halve, shr (8|M0) r53.0<1>:uw r53.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25c01248, 0x12b10440, 0x00b10460, ; halve, avg (16|M0) r46.0<1>:uw r34.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00400040, 0x26c00208, 0x024005c0, 0x004005c4, ; halve, add (4|M0) r54.0<1>:ud r46.0<2;1,0>:ud r46.1<2;1,0>:ud
0x00600008, 0x26c01248, 0x168d06c0, 0x00010001, ; halve, shr (8|M0) r54.0<1>:uw r54.0<8;8,1>:uw 0x1:uw
0x00800042, 0x25e01248, 0x12b10480, 0x00b104a0, ; halve, avg (16|M0) r47.0<1>:uw r36.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00400040, 0x26e00208, 0x024005e0, 0x004005e4, ; halve, add (4|M0) r55.0<1>:ud r47.0<2;1,0>:ud r47.1<2;1,0>:ud
0x00600008, 0x26e01248, 0x168d06e0, 0x00010001, ; halve, shr (8|M0) r55.0<1>:uw r55.0<8;8,1>:uw 0x1:uw
0x00600042, 0x27001248, 0x128d0600, 0x008d0620, ; halve, avg (8|M0) r56.0<1>:uw r48.0<8;8,1>:uw r49.0<8;8,1>:uw
0x00200040, 0x27800208, 0x02400700, 0x00400704, ; halve, add (2|M0) r60.0<1>:ud r56.0<2;1,0>:ud r56.1<2;1,0>:ud
0x00400008, 0x27801248, 0x16690780, 0x00010001, ; halve, shr (4|M0) r60.0<1>:uw r60.0<4;4,1>:uw 0x1:uw
0x00600042, 0x27201248, 0x128d0640, 0x008d0660, ; halve, avg (8|M0) r57.0<1>:uw r50.0<8;8,1>:uw r51.0<8;8,1>:uw
0x00200040, 0x27a00208, 0x02400720, 0x00400724, ; halve, add (2|M0) r61.0<1>:ud r57.0<2;1,0>:ud r57.1<2;1,0>:ud
0x00400008, 0x27a01248, 0x166907a0, 0x00010001, ; halve, shr (4|M0) r61.0<1>:uw r61.0<4;4,1>:uw 0x1:uw
0x00600042, 0x27401248, 0x128d0680, 0x008d06a0, ; halve, avg (8|M0) r58.0<1>:uw r52.0<8;8,1>:uw r53.0<8;8,1>:uw
0x00200040, 0x27c00208, 0x02400740, 0x00400744, ; halve, add (2|M0) r62.0<1>:ud r58.0<2;1,0>:ud r58.1<2;1,0>:ud
0x00400008, 0x27c01248, 0x166907c0, 0x00010001, ; halve, shr (4|M0) r62.0<1>:uw r62.0<4;4,1>:uw 0x1:uw
0x00600042, 0x27601248, 0x128d06c0, 0x008d06e0, ; halve, avg (8|M0) r59.0<1>:uw r54.0<8;8,1>:uw r55.0<8;8,1>:uw
0x00200040, 0x27e00208, 0x02400760, 0x00400764, ; halve, add (2|M0) r63.0<1>:ud r59.0<2;1,0>:ud r59.1<2;1,0>:ud
0x00400008, 0x27e01248, 0x166907e0, 0x00010001, ; halve, shr (4|M0) r63.0<1>:uw r63.0<4;4,1>:uw 0x1:uw
0x00400042, 0x28001248, 0x12690780, 0x006907a0, ; halve, avg (4|M0) r64.0<1>:uw r60.0<4;4,1>:uw r61.0<4;4,1>:uw
0x00000040, 0x28400208, 0x02000800, 0x00000804, ; halve, add (1|M0) r66.0<1>:ud r64.0<0;1,0>:ud r64.1<0;1,0>:ud
0x00200008, 0x28401248, 0x16450840, 0x00010001, ; halve, shr (2|M0) r66.0<1>:uw r66.0<2;2,1>:uw 0x1:uw
0x00400042, 0x28201248, 0x126907c0, 0x006907e0, ; halve, avg (4|M0) r65.0<1>:uw r62.0<4;4,1>:uw r63.0<4;4,1>:uw
0x00000040, 0x28600208, 0x02000820, 0x00000824, ; halve, add (1|M0) r67.0<1>:ud r65.0<0;1,0>:ud r65.1<0;1,0>:ud
0x00200008, 0x28601248, 0x16450860, 0x00010001, ; halve, shr (2|M0) r67.0<1>:uw r67.0<2;2,1>:uw 0x1:uw
0x00000009, 0x28600208, 0x06000860, 0x00000008, ; halve, shl (1|M0) r67.0<1>:ud r67.0<0;1,0>:ud 0x8:ud
0x00400001, 0x20c00208, 0x006900a4, 0x00000000, ; halve, mov (4|M0) r6.0<1>:ud r5.1<4;4,1>:ud
0x00200001, 0x20d00208, 0x004500b4, 0x00000000, ; halve, mov (2|M0) r6.4<1>:ud r5.5<2;2,1>:ud
0x00000001, 0x20d80208, 0x000000bc, 0x00000000, ; halve, mov (1|M0) r6.6<1>:ud r5.7<0;1,0>:ud
0x00000006, 0x20dc0208, 0x02000840, 0x00000860, ; halve, or (1|M0) r6.7<1>:ud r66.0<0;1,0>:ud r67.0<0;1,0>:ud
0x00600001, 0x20a00208, 0x008d00c0, 0x00000000, ; halve, mov (8|M0) r5.0<1>:ud r6.0<8;8,1>:ud
0x00000040, 0x20600208, 0x06000060, 0x00000020, ; halve, add (1|M0) r3.0<1>:ud r3.0<0;1,0>:ud 0x20:ud
0x00000040, 0x20480208, 0x06000048, 0x00000001, ; halve, add (1|M0) r2.2<1>:ud r2.2<0;1,0>:ud 0x1:ud
0x05800010, 0x20000201, 0x06000048, 0x00000008, ; halve, cmp (16|M0) (lt)f0.1 null<1>:ud r2.2<0;1,0>:ud 0x8:ud
0x00010020, 0x34000005, 0x0e001400, 0xfffffb30, ; halve, (W&f0.1) jmpi L80
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; halve, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00000009, 0x20800208, 0x06000004, 0x00000005, ; halve, shl (1|M0) r4.0<1>:ud r0.1<0;1,0>:ud 0x5:ud
0x00000001, 0x20840208, 0x00000018, 0x00000000, ; halve, mov (1|M0) r4.1<1>:ud r0.6<0;1,0>:ud
0x00000001, 0x20880608, 0x00000000, 0x0000001f, ; halve, mov (1|M0) r4.2<1>:ud 0x1F:ud
0x0c800031, 0x20000240, 0x06000080, 0x040a8000, ; halve, send (16|M0) null:uw r4 0xC 0x040A8000
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; halve, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x07800031, 0x20000a40, 0x06000e00, 0x82000010 ; halve, send (16|M0) null:uw r112:d 0x27 0x02000010 {EOT}

; rise (op 11): a glass window's ground made from its small tinted picture, the three doublings of compositor_rise (memory_double_u32) and the
; ordered grain of memory_glass_over_u32, a block of 8 by 8 pixels a thread: three rows of the small picture read on entry 1, the block's eight
; rows written on entry 0. src/canvas/gpu_i915_rise.iga, assembled with iga64 -a -p=9.

kernel rise
0x00600001, 0x20600208, 0x008d0000, 0x00000000, ; rise, mov (8|M0) r3.0<1>:ud r0.0<8;8,1>:ud
0x00000041, 0x20600208, 0x16000004, 0x00180018, ; rise, mul (1|M0) r3.0<1>:ud r0.1<0;1,0>:ud 0x18:uw
0x00000001, 0x20640208, 0x00000018, 0x00000000, ; rise, mov (1|M0) r3.1<1>:ud r0.6<0;1,0>:ud
0x00000001, 0x20680608, 0x00000000, 0x0002001f, ; rise, mov (1|M0) r3.2<1>:ud 0x2001F:ud
0x0c800031, 0x21c00208, 0x06000060, 0x02390001, ; rise, send (16|M0) r14 r3 0xC 0x02390001
0x00000041, 0x204c0208, 0x16000004, 0x00060006, ; rise, mul (1|M0) r2.3<1>:ud r0.1<0;1,0>:ud 0x6:uw
0x00000040, 0x204c0a28, 0x0a000024, 0x0000404c, ; rise, add (1|M0) r2.3<1>:d r1.1<0;1,0>:d -r2.3<0;1,0>:d
0x05000002, 0x204c020b, 0x0600004c, 0x00000006, ; rise, sel (1|M0) (lt)f1.1 r2.3<1>:ud r2.3<0;1,0>:ud 0x6:ud
0x00000001, 0x20480608, 0x00000000, 0x00000000, ; rise, mov (1|M0) r2.2<1>:ud 0x0:ud
0x02000010, 0x20000a20, 0x0e000020, 0x00000000, ; rise, cmp (1|M0) (ne)f0.0 null<1>:d r1.0<0;1,0>:d 0:d
0x00000001, 0x20400e28, 0x00000000, 0x00000001, ; rise, mov (1|M0) r2.0<1>:d 1:d
0x00010001, 0x20400e28, 0x00000000, 0xffffffff, ; rise, (f0.0) mov (1|M0) r2.0<1>:d -1:d
0x00400001, 0x4f000608, 0x00000000, 0x00000000, ; rise, mov (4|M0) r120.0<2>:ud 0x0:ud
0x00400001, 0x4f040608, 0x00000000, 0x00020002, ; rise, mov (4|M0) r120.1<2>:ud 0x20002:ud
0x00800041, 0x2f001a68, 0x1ab10f00, 0x00000040, ; rise, mul (16|M0) r120.0<1>:w r120.0<16;16,1>:w r2.0<0;1,0>:w
0x00400001, 0x4f200608, 0x00000000, 0x00000000, ; rise, mov (4|M0) r121.0<2>:ud 0x0:ud
0x00400001, 0x4f240608, 0x00000000, 0x00000002, ; rise, mov (4|M0) r121.1<2>:ud 0x2:ud
0x00800041, 0x2f201a68, 0x1ab10f20, 0x00000040, ; rise, mul (16|M0) r121.0<1>:w r121.0<16;16,1>:w r2.0<0;1,0>:w
0x00400001, 0x4f400608, 0x00000000, 0x00030003, ; rise, mov (4|M0) r122.0<2>:ud 0x30003:ud
0x00400001, 0x4f440608, 0x00000000, 0x00010001, ; rise, mov (4|M0) r122.1<2>:ud 0x10001:ud
0x00800041, 0x2f401a68, 0x1ab10f40, 0x00000040, ; rise, mul (16|M0) r122.0<1>:w r122.0<16;16,1>:w r2.0<0;1,0>:w
0x00400001, 0x4f600608, 0x00000000, 0x00000003, ; rise, mov (4|M0) r123.0<2>:ud 0x3:ud
0x00400001, 0x4f640608, 0x00000000, 0x00000001, ; rise, mov (4|M0) r123.1<2>:ud 0x1:ud
0x00800041, 0x2f601a68, 0x1ab10f60, 0x00000040, ; rise, mul (16|M0) r123.0<1>:w r123.0<16;16,1>:w r2.0<0;1,0>:w
0x00600001, 0x20800208, 0x008d0000, 0x00000000, ; rise, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00000041, 0x20800208, 0x16000004, 0x00c000c0, ; rise, mul (1|M0) r4.0<1>:ud r0.1<0;1,0>:ud 0xC0:uw
0x00000009, 0x20840208, 0x06000018, 0x00000003, ; rise, shl (1|M0) r4.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000001, 0x20880608, 0x00000000, 0x0007001f, ; rise, mov (1|M0) r4.2<1>:ud 0x7001F:ud
0x00400005, 0x22800208, 0x066901c0, 0x00ff00ff, ; rise, and (4|M0) r20.0<1>:ud r14.0<4;4,1>:ud 0xFF00FF:ud
0x00400008, 0x22a00208, 0x066901c0, 0x00000008, ; rise, shr (4|M0) r21.0<1>:ud r14.0<4;4,1>:ud 0x8:ud
0x00400005, 0x22a00208, 0x066902a0, 0x00ff00ff, ; rise, and (4|M0) r21.0<1>:ud r21.0<4;4,1>:ud 0xFF00FF:ud
0x00400005, 0x22c00208, 0x066901e0, 0x00ff00ff, ; rise, and (4|M0) r22.0<1>:ud r15.0<4;4,1>:ud 0xFF00FF:ud
0x00400008, 0x22e00208, 0x066901e0, 0x00000008, ; rise, shr (4|M0) r23.0<1>:ud r15.0<4;4,1>:ud 0x8:ud
0x00400005, 0x22e00208, 0x066902e0, 0x00ff00ff, ; rise, and (4|M0) r23.0<1>:ud r23.0<4;4,1>:ud 0xFF00FF:ud
0x00400005, 0x23000208, 0x06690200, 0x00ff00ff, ; rise, and (4|M0) r24.0<1>:ud r16.0<4;4,1>:ud 0xFF00FF:ud
0x00400008, 0x23200208, 0x06690200, 0x00000008, ; rise, shr (4|M0) r25.0<1>:ud r16.0<4;4,1>:ud 0x8:ud
0x00400005, 0x23200208, 0x06690320, 0x00ff00ff, ; rise, and (4|M0) r25.0<1>:ud r25.0<4;4,1>:ud 0xFF00FF:ud
0x00800042, 0x23401248, 0x12b10280, 0x00b102c0, ; rise, avg (16|M0) r26.0<1>:uw r20.0<16;16,1>:uw r22.0<16;16,1>:uw
0x00800040, 0x23601248, 0x12b10280, 0x00b10340, ; rise, add (16|M0) r27.0<1>:uw r20.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x23601248, 0x16b10360, 0x00010001, ; rise, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23801248, 0x12b102c0, 0x00b10340, ; rise, add (16|M0) r28.0<1>:uw r22.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00800042, 0x23401248, 0x12b102a0, 0x00b102e0, ; rise, avg (16|M0) r26.0<1>:uw r21.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00800040, 0x23a01248, 0x12b102a0, 0x00b10340, ; rise, add (16|M0) r29.0<1>:uw r21.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x23a01248, 0x16b103a0, 0x00010001, ; rise, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23c01248, 0x12b102e0, 0x00b10340, ; rise, add (16|M0) r30.0<1>:uw r23.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00800042, 0x23401248, 0x12b102c0, 0x00b10300, ; rise, avg (16|M0) r26.0<1>:uw r22.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800040, 0x23e01248, 0x12b102c0, 0x00b10340, ; rise, add (16|M0) r31.0<1>:uw r22.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x23e01248, 0x16b103e0, 0x00010001, ; rise, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24001248, 0x12b10300, 0x00b10340, ; rise, add (16|M0) r32.0<1>:uw r24.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x24001248, 0x16b10400, 0x00010001, ; rise, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw
0x00800042, 0x23401248, 0x12b102e0, 0x00b10320, ; rise, avg (16|M0) r26.0<1>:uw r23.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800040, 0x24201248, 0x12b102e0, 0x00b10340, ; rise, add (16|M0) r33.0<1>:uw r23.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x24201248, 0x16b10420, 0x00010001, ; rise, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24401248, 0x12b10320, 0x00b10340, ; rise, add (16|M0) r34.0<1>:uw r25.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x24401248, 0x16b10440, 0x00010001, ; rise, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw
0x00400001, 0x23200208, 0x00690364, 0x00000000, ; rise, mov (4|M0) r25.0<1>:ud r27.1<4;4,1>:ud
0x00800042, 0x23001248, 0x12b10360, 0x00b10320, ; rise, avg (16|M0) r24.0<1>:uw r27.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800040, 0x22e01248, 0x12b10360, 0x00b10300, ; rise, add (16|M0) r23.0<1>:uw r27.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800008, 0x22e01248, 0x16b102e0, 0x00010001, ; rise, shr (16|M0) r23.0<1>:uw r23.0<16;16,1>:uw 0x1:uw
0x00800040, 0x22c01248, 0x12b10320, 0x00b10300, ; rise, add (16|M0) r22.0<1>:uw r25.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800008, 0x22c01248, 0x16b102c0, 0x00010001, ; rise, shr (16|M0) r22.0<1>:uw r22.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42a00208, 0x006902e0, 0x00000000, ; rise, mov (4|M0) r21.0<2>:ud r23.0<4;4,1>:ud
0x00400001, 0x42a40208, 0x006902c0, 0x00000000, ; rise, mov (4|M0) r21.1<2>:ud r22.0<4;4,1>:ud
0x00400001, 0x22c00208, 0x006903a4, 0x00000000, ; rise, mov (4|M0) r22.0<1>:ud r29.1<4;4,1>:ud
0x00800042, 0x22e01248, 0x12b103a0, 0x00b102c0, ; rise, avg (16|M0) r23.0<1>:uw r29.0<16;16,1>:uw r22.0<16;16,1>:uw
0x00800040, 0x23001248, 0x12b103a0, 0x00b102e0, ; rise, add (16|M0) r24.0<1>:uw r29.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00800008, 0x23001248, 0x16b10300, 0x00010001, ; rise, shr (16|M0) r24.0<1>:uw r24.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23201248, 0x12b102c0, 0x00b102e0, ; rise, add (16|M0) r25.0<1>:uw r22.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00800008, 0x23201248, 0x16b10320, 0x00010001, ; rise, shr (16|M0) r25.0<1>:uw r25.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42800208, 0x00690300, 0x00000000, ; rise, mov (4|M0) r20.0<2>:ud r24.0<4;4,1>:ud
0x00400001, 0x42840208, 0x00690320, 0x00000000, ; rise, mov (4|M0) r20.1<2>:ud r25.0<4;4,1>:ud
0x00400001, 0x23a00208, 0x00690384, 0x00000000, ; rise, mov (4|M0) r29.0<1>:ud r28.1<4;4,1>:ud
0x00800042, 0x23601248, 0x12b10380, 0x00b103a0, ; rise, avg (16|M0) r27.0<1>:uw r28.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800040, 0x23201248, 0x12b10380, 0x00b10360, ; rise, add (16|M0) r25.0<1>:uw r28.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800008, 0x23201248, 0x16b10320, 0x00010001, ; rise, shr (16|M0) r25.0<1>:uw r25.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23001248, 0x12b103a0, 0x00b10360, ; rise, add (16|M0) r24.0<1>:uw r29.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800008, 0x23001248, 0x16b10300, 0x00010001, ; rise, shr (16|M0) r24.0<1>:uw r24.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42e00208, 0x00690320, 0x00000000, ; rise, mov (4|M0) r23.0<2>:ud r25.0<4;4,1>:ud
0x00400001, 0x42e40208, 0x00690300, 0x00000000, ; rise, mov (4|M0) r23.1<2>:ud r24.0<4;4,1>:ud
0x00400001, 0x23000208, 0x006903c4, 0x00000000, ; rise, mov (4|M0) r24.0<1>:ud r30.1<4;4,1>:ud
0x00800042, 0x23201248, 0x12b103c0, 0x00b10300, ; rise, avg (16|M0) r25.0<1>:uw r30.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800040, 0x23601248, 0x12b103c0, 0x00b10320, ; rise, add (16|M0) r27.0<1>:uw r30.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800008, 0x23601248, 0x16b10360, 0x00010001, ; rise, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23a01248, 0x12b10300, 0x00b10320, ; rise, add (16|M0) r29.0<1>:uw r24.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800008, 0x23a01248, 0x16b103a0, 0x00010001, ; rise, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42c00208, 0x00690360, 0x00000000, ; rise, mov (4|M0) r22.0<2>:ud r27.0<4;4,1>:ud
0x00400001, 0x42c40208, 0x006903a0, 0x00000000, ; rise, mov (4|M0) r22.1<2>:ud r29.0<4;4,1>:ud
0x00400001, 0x23c00208, 0x006903e4, 0x00000000, ; rise, mov (4|M0) r30.0<1>:ud r31.1<4;4,1>:ud
0x00800042, 0x23801248, 0x12b103e0, 0x00b103c0, ; rise, avg (16|M0) r28.0<1>:uw r31.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800040, 0x23a01248, 0x12b103e0, 0x00b10380, ; rise, add (16|M0) r29.0<1>:uw r31.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800008, 0x23a01248, 0x16b103a0, 0x00010001, ; rise, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23601248, 0x12b103c0, 0x00b10380, ; rise, add (16|M0) r27.0<1>:uw r30.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800008, 0x23601248, 0x16b10360, 0x00010001, ; rise, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43200208, 0x006903a0, 0x00000000, ; rise, mov (4|M0) r25.0<2>:ud r29.0<4;4,1>:ud
0x00400001, 0x43240208, 0x00690360, 0x00000000, ; rise, mov (4|M0) r25.1<2>:ud r27.0<4;4,1>:ud
0x00400001, 0x23600208, 0x00690424, 0x00000000, ; rise, mov (4|M0) r27.0<1>:ud r33.1<4;4,1>:ud
0x00800042, 0x23a01248, 0x12b10420, 0x00b10360, ; rise, avg (16|M0) r29.0<1>:uw r33.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800040, 0x23801248, 0x12b10420, 0x00b103a0, ; rise, add (16|M0) r28.0<1>:uw r33.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23c01248, 0x12b10360, 0x00b103a0, ; rise, add (16|M0) r30.0<1>:uw r27.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43000208, 0x00690380, 0x00000000, ; rise, mov (4|M0) r24.0<2>:ud r28.0<4;4,1>:ud
0x00400001, 0x43040208, 0x006903c0, 0x00000000, ; rise, mov (4|M0) r24.1<2>:ud r30.0<4;4,1>:ud
0x00400001, 0x24200208, 0x00690404, 0x00000000, ; rise, mov (4|M0) r33.0<1>:ud r32.1<4;4,1>:ud
0x00800042, 0x23e01248, 0x12b10400, 0x00b10420, ; rise, avg (16|M0) r31.0<1>:uw r32.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800040, 0x23c01248, 0x12b10400, 0x00b103e0, ; rise, add (16|M0) r30.0<1>:uw r32.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23801248, 0x12b10420, 0x00b103e0, ; rise, add (16|M0) r28.0<1>:uw r33.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43a00208, 0x006903c0, 0x00000000, ; rise, mov (4|M0) r29.0<2>:ud r30.0<4;4,1>:ud
0x00400001, 0x43a40208, 0x00690380, 0x00000000, ; rise, mov (4|M0) r29.1<2>:ud r28.0<4;4,1>:ud
0x00400001, 0x23800208, 0x00690444, 0x00000000, ; rise, mov (4|M0) r28.0<1>:ud r34.1<4;4,1>:ud
0x00800042, 0x23c01248, 0x12b10440, 0x00b10380, ; rise, avg (16|M0) r30.0<1>:uw r34.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800040, 0x23e01248, 0x12b10440, 0x00b103c0, ; rise, add (16|M0) r31.0<1>:uw r34.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800008, 0x23e01248, 0x16b103e0, 0x00010001, ; rise, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24201248, 0x12b10380, 0x00b103c0, ; rise, add (16|M0) r33.0<1>:uw r28.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800008, 0x24201248, 0x16b10420, 0x00010001, ; rise, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43600208, 0x006903e0, 0x00000000, ; rise, mov (4|M0) r27.0<2>:ud r31.0<4;4,1>:ud
0x00400001, 0x43640208, 0x00690420, 0x00000000, ; rise, mov (4|M0) r27.1<2>:ud r33.0<4;4,1>:ud
0x00800042, 0x24401248, 0x12b102a0, 0x00b102e0, ; rise, avg (16|M0) r34.0<1>:uw r21.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00800040, 0x24001248, 0x12b102a0, 0x00b10440, ; rise, add (16|M0) r32.0<1>:uw r21.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24001248, 0x16b10400, 0x00010001, ; rise, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24201248, 0x12b102e0, 0x00b10440, ; rise, add (16|M0) r33.0<1>:uw r23.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24201248, 0x16b10420, 0x00010001, ; rise, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw
0x00800042, 0x24401248, 0x12b10280, 0x00b102c0, ; rise, avg (16|M0) r34.0<1>:uw r20.0<16;16,1>:uw r22.0<16;16,1>:uw
0x00800040, 0x23e01248, 0x12b10280, 0x00b10440, ; rise, add (16|M0) r31.0<1>:uw r20.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x23e01248, 0x16b103e0, 0x00010001, ; rise, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23c01248, 0x12b102c0, 0x00b10440, ; rise, add (16|M0) r30.0<1>:uw r22.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00800042, 0x24401248, 0x12b102e0, 0x00b10320, ; rise, avg (16|M0) r34.0<1>:uw r23.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800040, 0x23801248, 0x12b102e0, 0x00b10440, ; rise, add (16|M0) r28.0<1>:uw r23.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23401248, 0x12b10320, 0x00b10440, ; rise, add (16|M0) r26.0<1>:uw r25.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x23401248, 0x16b10340, 0x00010001, ; rise, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw
0x00800042, 0x24401248, 0x12b102c0, 0x00b10300, ; rise, avg (16|M0) r34.0<1>:uw r22.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800040, 0x24601248, 0x12b102c0, 0x00b10440, ; rise, add (16|M0) r35.0<1>:uw r22.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24601248, 0x16b10460, 0x00010001, ; rise, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24801248, 0x12b10300, 0x00b10440, ; rise, add (16|M0) r36.0<1>:uw r24.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24801248, 0x16b10480, 0x00010001, ; rise, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw
0x00800042, 0x24401248, 0x12b10320, 0x00b103a0, ; rise, avg (16|M0) r34.0<1>:uw r25.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800040, 0x24a01248, 0x12b10320, 0x00b10440, ; rise, add (16|M0) r37.0<1>:uw r25.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24a01248, 0x16b104a0, 0x00010001, ; rise, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24c01248, 0x12b103a0, 0x00b10440, ; rise, add (16|M0) r38.0<1>:uw r29.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24c01248, 0x16b104c0, 0x00010001, ; rise, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw
0x00800042, 0x24401248, 0x12b10300, 0x00b10360, ; rise, avg (16|M0) r34.0<1>:uw r24.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800040, 0x24e01248, 0x12b10300, 0x00b10440, ; rise, add (16|M0) r39.0<1>:uw r24.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x24e01248, 0x16b104e0, 0x00010001, ; rise, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25001248, 0x12b10360, 0x00b10440, ; rise, add (16|M0) r40.0<1>:uw r27.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x25001248, 0x16b10500, 0x00010001, ; rise, shr (16|M0) r40.0<1>:uw r40.0<16;16,1>:uw 0x1:uw
0x00400001, 0x23600208, 0x00690404, 0x00000000, ; rise, mov (4|M0) r27.0<1>:ud r32.1<4;4,1>:ud
0x00800042, 0x23a01248, 0x12b10400, 0x00b10360, ; rise, avg (16|M0) r29.0<1>:uw r32.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800040, 0x23001248, 0x12b10400, 0x00b103a0, ; rise, add (16|M0) r24.0<1>:uw r32.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800008, 0x23001248, 0x16b10300, 0x00010001, ; rise, shr (16|M0) r24.0<1>:uw r24.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23201248, 0x12b10360, 0x00b103a0, ; rise, add (16|M0) r25.0<1>:uw r27.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800008, 0x23201248, 0x16b10320, 0x00010001, ; rise, shr (16|M0) r25.0<1>:uw r25.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42c00208, 0x00690300, 0x00000000, ; rise, mov (4|M0) r22.0<2>:ud r24.0<4;4,1>:ud
0x00400001, 0x42c40208, 0x00690320, 0x00000000, ; rise, mov (4|M0) r22.1<2>:ud r25.0<4;4,1>:ud
0x00400001, 0x23200208, 0x006903e4, 0x00000000, ; rise, mov (4|M0) r25.0<1>:ud r31.1<4;4,1>:ud
0x00800042, 0x23001248, 0x12b103e0, 0x00b10320, ; rise, avg (16|M0) r24.0<1>:uw r31.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800040, 0x23a01248, 0x12b103e0, 0x00b10300, ; rise, add (16|M0) r29.0<1>:uw r31.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800008, 0x23a01248, 0x16b103a0, 0x00010001, ; rise, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23601248, 0x12b10320, 0x00b10300, ; rise, add (16|M0) r27.0<1>:uw r25.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800008, 0x23601248, 0x16b10360, 0x00010001, ; rise, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42e00208, 0x006903a0, 0x00000000, ; rise, mov (4|M0) r23.0<2>:ud r29.0<4;4,1>:ud
0x00400001, 0x42e40208, 0x00690360, 0x00000000, ; rise, mov (4|M0) r23.1<2>:ud r27.0<4;4,1>:ud
0x00400001, 0x23e00208, 0x00690424, 0x00000000, ; rise, mov (4|M0) r31.0<1>:ud r33.1<4;4,1>:ud
0x00800042, 0x24001248, 0x12b10420, 0x00b103e0, ; rise, avg (16|M0) r32.0<1>:uw r33.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00800040, 0x23601248, 0x12b10420, 0x00b10400, ; rise, add (16|M0) r27.0<1>:uw r33.0<16;16,1>:uw r32.0<16;16,1>:uw
0x00800008, 0x23601248, 0x16b10360, 0x00010001, ; rise, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23a01248, 0x12b103e0, 0x00b10400, ; rise, add (16|M0) r29.0<1>:uw r31.0<16;16,1>:uw r32.0<16;16,1>:uw
0x00800008, 0x23a01248, 0x16b103a0, 0x00010001, ; rise, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43000208, 0x00690360, 0x00000000, ; rise, mov (4|M0) r24.0<2>:ud r27.0<4;4,1>:ud
0x00400001, 0x43040208, 0x006903a0, 0x00000000, ; rise, mov (4|M0) r24.1<2>:ud r29.0<4;4,1>:ud
0x00400001, 0x23a00208, 0x006903c4, 0x00000000, ; rise, mov (4|M0) r29.0<1>:ud r30.1<4;4,1>:ud
0x00800042, 0x23601248, 0x12b103c0, 0x00b103a0, ; rise, avg (16|M0) r27.0<1>:uw r30.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800040, 0x24001248, 0x12b103c0, 0x00b10360, ; rise, add (16|M0) r32.0<1>:uw r30.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800008, 0x24001248, 0x16b10400, 0x00010001, ; rise, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23e01248, 0x12b103a0, 0x00b10360, ; rise, add (16|M0) r31.0<1>:uw r29.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800008, 0x23e01248, 0x16b103e0, 0x00010001, ; rise, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43200208, 0x00690400, 0x00000000, ; rise, mov (4|M0) r25.0<2>:ud r32.0<4;4,1>:ud
0x00400001, 0x43240208, 0x006903e0, 0x00000000, ; rise, mov (4|M0) r25.1<2>:ud r31.0<4;4,1>:ud
0x00400001, 0x23c00208, 0x00690384, 0x00000000, ; rise, mov (4|M0) r30.0<1>:ud r28.1<4;4,1>:ud
0x00800042, 0x24201248, 0x12b10380, 0x00b103c0, ; rise, avg (16|M0) r33.0<1>:uw r28.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800040, 0x23e01248, 0x12b10380, 0x00b10420, ; rise, add (16|M0) r31.0<1>:uw r28.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800008, 0x23e01248, 0x16b103e0, 0x00010001, ; rise, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24001248, 0x12b103c0, 0x00b10420, ; rise, add (16|M0) r32.0<1>:uw r30.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800008, 0x24001248, 0x16b10400, 0x00010001, ; rise, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43600208, 0x006903e0, 0x00000000, ; rise, mov (4|M0) r27.0<2>:ud r31.0<4;4,1>:ud
0x00400001, 0x43640208, 0x00690400, 0x00000000, ; rise, mov (4|M0) r27.1<2>:ud r32.0<4;4,1>:ud
0x00400001, 0x24000208, 0x00690464, 0x00000000, ; rise, mov (4|M0) r32.0<1>:ud r35.1<4;4,1>:ud
0x00800042, 0x23e01248, 0x12b10460, 0x00b10400, ; rise, avg (16|M0) r31.0<1>:uw r35.0<16;16,1>:uw r32.0<16;16,1>:uw
0x00800040, 0x24201248, 0x12b10460, 0x00b103e0, ; rise, add (16|M0) r33.0<1>:uw r35.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00800008, 0x24201248, 0x16b10420, 0x00010001, ; rise, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23c01248, 0x12b10400, 0x00b103e0, ; rise, add (16|M0) r30.0<1>:uw r32.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43a00208, 0x00690420, 0x00000000, ; rise, mov (4|M0) r29.0<2>:ud r33.0<4;4,1>:ud
0x00400001, 0x43a40208, 0x006903c0, 0x00000000, ; rise, mov (4|M0) r29.1<2>:ud r30.0<4;4,1>:ud
0x00400001, 0x24600208, 0x00690344, 0x00000000, ; rise, mov (4|M0) r35.0<1>:ud r26.1<4;4,1>:ud
0x00800042, 0x23801248, 0x12b10340, 0x00b10460, ; rise, avg (16|M0) r28.0<1>:uw r26.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800040, 0x23c01248, 0x12b10340, 0x00b10380, ; rise, add (16|M0) r30.0<1>:uw r26.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24201248, 0x12b10460, 0x00b10380, ; rise, add (16|M0) r33.0<1>:uw r35.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800008, 0x24201248, 0x16b10420, 0x00010001, ; rise, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43e00208, 0x006903c0, 0x00000000, ; rise, mov (4|M0) r31.0<2>:ud r30.0<4;4,1>:ud
0x00400001, 0x43e40208, 0x00690420, 0x00000000, ; rise, mov (4|M0) r31.1<2>:ud r33.0<4;4,1>:ud
0x00400001, 0x24200208, 0x00690484, 0x00000000, ; rise, mov (4|M0) r33.0<1>:ud r36.1<4;4,1>:ud
0x00800042, 0x23c01248, 0x12b10480, 0x00b10420, ; rise, avg (16|M0) r30.0<1>:uw r36.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800040, 0x23801248, 0x12b10480, 0x00b103c0, ; rise, add (16|M0) r28.0<1>:uw r36.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24601248, 0x12b10420, 0x00b103c0, ; rise, add (16|M0) r35.0<1>:uw r33.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800008, 0x24601248, 0x16b10460, 0x00010001, ; rise, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44000208, 0x00690380, 0x00000000, ; rise, mov (4|M0) r32.0<2>:ud r28.0<4;4,1>:ud
0x00400001, 0x44040208, 0x00690460, 0x00000000, ; rise, mov (4|M0) r32.1<2>:ud r35.0<4;4,1>:ud
0x00400001, 0x24800208, 0x006904a4, 0x00000000, ; rise, mov (4|M0) r36.0<1>:ud r37.1<4;4,1>:ud
0x00800042, 0x23401248, 0x12b104a0, 0x00b10480, ; rise, avg (16|M0) r26.0<1>:uw r37.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00800040, 0x24601248, 0x12b104a0, 0x00b10340, ; rise, add (16|M0) r35.0<1>:uw r37.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x24601248, 0x16b10460, 0x00010001, ; rise, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23801248, 0x12b10480, 0x00b10340, ; rise, add (16|M0) r28.0<1>:uw r36.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43c00208, 0x00690460, 0x00000000, ; rise, mov (4|M0) r30.0<2>:ud r35.0<4;4,1>:ud
0x00400001, 0x43c40208, 0x00690380, 0x00000000, ; rise, mov (4|M0) r30.1<2>:ud r28.0<4;4,1>:ud
0x00400001, 0x23800208, 0x006904e4, 0x00000000, ; rise, mov (4|M0) r28.0<1>:ud r39.1<4;4,1>:ud
0x00800042, 0x24601248, 0x12b104e0, 0x00b10380, ; rise, avg (16|M0) r35.0<1>:uw r39.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800040, 0x23401248, 0x12b104e0, 0x00b10460, ; rise, add (16|M0) r26.0<1>:uw r39.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800008, 0x23401248, 0x16b10340, 0x00010001, ; rise, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24801248, 0x12b10380, 0x00b10460, ; rise, add (16|M0) r36.0<1>:uw r28.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800008, 0x24801248, 0x16b10480, 0x00010001, ; rise, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44200208, 0x00690340, 0x00000000, ; rise, mov (4|M0) r33.0<2>:ud r26.0<4;4,1>:ud
0x00400001, 0x44240208, 0x00690480, 0x00000000, ; rise, mov (4|M0) r33.1<2>:ud r36.0<4;4,1>:ud
0x00400001, 0x24e00208, 0x006904c4, 0x00000000, ; rise, mov (4|M0) r39.0<1>:ud r38.1<4;4,1>:ud
0x00800042, 0x24a01248, 0x12b104c0, 0x00b104e0, ; rise, avg (16|M0) r37.0<1>:uw r38.0<16;16,1>:uw r39.0<16;16,1>:uw
0x00800040, 0x24801248, 0x12b104c0, 0x00b104a0, ; rise, add (16|M0) r36.0<1>:uw r38.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00800008, 0x24801248, 0x16b10480, 0x00010001, ; rise, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23401248, 0x12b104e0, 0x00b104a0, ; rise, add (16|M0) r26.0<1>:uw r39.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00800008, 0x23401248, 0x16b10340, 0x00010001, ; rise, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44600208, 0x00690480, 0x00000000, ; rise, mov (4|M0) r35.0<2>:ud r36.0<4;4,1>:ud
0x00400001, 0x44640208, 0x00690340, 0x00000000, ; rise, mov (4|M0) r35.1<2>:ud r26.0<4;4,1>:ud
0x00400001, 0x23400208, 0x00690504, 0x00000000, ; rise, mov (4|M0) r26.0<1>:ud r40.1<4;4,1>:ud
0x00800042, 0x24801248, 0x12b10500, 0x00b10340, ; rise, avg (16|M0) r36.0<1>:uw r40.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800040, 0x24a01248, 0x12b10500, 0x00b10480, ; rise, add (16|M0) r37.0<1>:uw r40.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00800008, 0x24a01248, 0x16b104a0, 0x00010001, ; rise, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24e01248, 0x12b10340, 0x00b10480, ; rise, add (16|M0) r39.0<1>:uw r26.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00800008, 0x24e01248, 0x16b104e0, 0x00010001, ; rise, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43800208, 0x006904a0, 0x00000000, ; rise, mov (4|M0) r28.0<2>:ud r37.0<4;4,1>:ud
0x00400001, 0x43840208, 0x006904e0, 0x00000000, ; rise, mov (4|M0) r28.1<2>:ud r39.0<4;4,1>:ud
0x00800042, 0x25001248, 0x12b102c0, 0x00b10300, ; rise, avg (16|M0) r40.0<1>:uw r22.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00800040, 0x24c01248, 0x12b10300, 0x00b10500, ; rise, add (16|M0) r38.0<1>:uw r24.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x24c01248, 0x16b104c0, 0x00010001, ; rise, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b102e0, 0x00b10320, ; rise, avg (16|M0) r40.0<1>:uw r23.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00800040, 0x24e01248, 0x12b10320, 0x00b10500, ; rise, add (16|M0) r39.0<1>:uw r25.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x24e01248, 0x16b104e0, 0x00010001, ; rise, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b10300, 0x00b10360, ; rise, avg (16|M0) r40.0<1>:uw r24.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00800040, 0x24a01248, 0x12b10300, 0x00b10500, ; rise, add (16|M0) r37.0<1>:uw r24.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x24a01248, 0x16b104a0, 0x00010001, ; rise, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24801248, 0x12b10360, 0x00b10500, ; rise, add (16|M0) r36.0<1>:uw r27.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x24801248, 0x16b10480, 0x00010001, ; rise, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b10320, 0x00b103a0, ; rise, avg (16|M0) r40.0<1>:uw r25.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00800040, 0x23401248, 0x12b10320, 0x00b10500, ; rise, add (16|M0) r26.0<1>:uw r25.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x23401248, 0x16b10340, 0x00010001, ; rise, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw
0x00800040, 0x22801248, 0x12b103a0, 0x00b10500, ; rise, add (16|M0) r20.0<1>:uw r29.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x22801248, 0x16b10280, 0x00010001, ; rise, shr (16|M0) r20.0<1>:uw r20.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b10360, 0x00b103e0, ; rise, avg (16|M0) r40.0<1>:uw r27.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00800040, 0x22a01248, 0x12b10360, 0x00b10500, ; rise, add (16|M0) r21.0<1>:uw r27.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x22a01248, 0x16b102a0, 0x00010001, ; rise, shr (16|M0) r21.0<1>:uw r21.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24401248, 0x12b103e0, 0x00b10500, ; rise, add (16|M0) r34.0<1>:uw r31.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x24401248, 0x16b10440, 0x00010001, ; rise, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b103a0, 0x00b10400, ; rise, avg (16|M0) r40.0<1>:uw r29.0<16;16,1>:uw r32.0<16;16,1>:uw
0x00800040, 0x25201248, 0x12b103a0, 0x00b10500, ; rise, add (16|M0) r41.0<1>:uw r29.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25201248, 0x16b10520, 0x00010001, ; rise, shr (16|M0) r41.0<1>:uw r41.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25401248, 0x12b10400, 0x00b10500, ; rise, add (16|M0) r42.0<1>:uw r32.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25401248, 0x16b10540, 0x00010001, ; rise, shr (16|M0) r42.0<1>:uw r42.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b103e0, 0x00b103c0, ; rise, avg (16|M0) r40.0<1>:uw r31.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800040, 0x25601248, 0x12b103e0, 0x00b10500, ; rise, add (16|M0) r43.0<1>:uw r31.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25601248, 0x16b10560, 0x00010001, ; rise, shr (16|M0) r43.0<1>:uw r43.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25801248, 0x12b103c0, 0x00b10500, ; rise, add (16|M0) r44.0<1>:uw r30.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25801248, 0x16b10580, 0x00010001, ; rise, shr (16|M0) r44.0<1>:uw r44.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b10400, 0x00b10420, ; rise, avg (16|M0) r40.0<1>:uw r32.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800040, 0x25a01248, 0x12b10400, 0x00b10500, ; rise, add (16|M0) r45.0<1>:uw r32.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25a01248, 0x16b105a0, 0x00010001, ; rise, shr (16|M0) r45.0<1>:uw r45.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25c01248, 0x12b10420, 0x00b10500, ; rise, add (16|M0) r46.0<1>:uw r33.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25c01248, 0x16b105c0, 0x00010001, ; rise, shr (16|M0) r46.0<1>:uw r46.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b103c0, 0x00b10460, ; rise, avg (16|M0) r40.0<1>:uw r30.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800040, 0x25e01248, 0x12b103c0, 0x00b10500, ; rise, add (16|M0) r47.0<1>:uw r30.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x25e01248, 0x16b105e0, 0x00010001, ; rise, shr (16|M0) r47.0<1>:uw r47.0<16;16,1>:uw 0x1:uw
0x00800042, 0x25001248, 0x12b10420, 0x00b10380, ; rise, avg (16|M0) r40.0<1>:uw r33.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800040, 0x26001248, 0x12b10420, 0x00b10500, ; rise, add (16|M0) r48.0<1>:uw r33.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00800008, 0x26001248, 0x16b10600, 0x00010001, ; rise, shr (16|M0) r48.0<1>:uw r48.0<16;16,1>:uw 0x1:uw
0x00400001, 0x23800208, 0x006904c4, 0x00000000, ; rise, mov (4|M0) r28.0<1>:ud r38.1<4;4,1>:ud
0x00000001, 0x23900208, 0x000004d4, 0x00000000, ; rise, mov (1|M0) r28.4<1>:ud r38.5<0;1,0>:ud
0x00800042, 0x24601248, 0x12b104c0, 0x00b10380, ; rise, avg (16|M0) r35.0<1>:uw r38.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800040, 0x24201248, 0x12b104c0, 0x00b10460, ; rise, add (16|M0) r33.0<1>:uw r38.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800008, 0x24201248, 0x16b10420, 0x00010001, ; rise, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23c01248, 0x12b10380, 0x00b10460, ; rise, add (16|M0) r30.0<1>:uw r28.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800008, 0x23c01248, 0x16b103c0, 0x00010001, ; rise, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44000208, 0x006903c0, 0x00000000, ; rise, mov (4|M0) r32.0<2>:ud r30.0<4;4,1>:ud
0x00400001, 0x44040208, 0x00690424, 0x00000000, ; rise, mov (4|M0) r32.1<2>:ud r33.1<4;4,1>:ud
0x00400001, 0x23c00208, 0x006904e4, 0x00000000, ; rise, mov (4|M0) r30.0<1>:ud r39.1<4;4,1>:ud
0x00000001, 0x23d00208, 0x000004f4, 0x00000000, ; rise, mov (1|M0) r30.4<1>:ud r39.5<0;1,0>:ud
0x00800042, 0x24201248, 0x12b104e0, 0x00b103c0, ; rise, avg (16|M0) r33.0<1>:uw r39.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00800040, 0x24601248, 0x12b104e0, 0x00b10420, ; rise, add (16|M0) r35.0<1>:uw r39.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800008, 0x24601248, 0x16b10460, 0x00010001, ; rise, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23801248, 0x12b103c0, 0x00b10420, ; rise, add (16|M0) r28.0<1>:uw r30.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43e00208, 0x00690380, 0x00000000, ; rise, mov (4|M0) r31.0<2>:ud r28.0<4;4,1>:ud
0x00400001, 0x43e40208, 0x00690464, 0x00000000, ; rise, mov (4|M0) r31.1<2>:ud r35.1<4;4,1>:ud
0x00400001, 0x24e00208, 0x006904a4, 0x00000000, ; rise, mov (4|M0) r39.0<1>:ud r37.1<4;4,1>:ud
0x00000001, 0x24f00208, 0x000004b4, 0x00000000, ; rise, mov (1|M0) r39.4<1>:ud r37.5<0;1,0>:ud
0x00800042, 0x24c01248, 0x12b104a0, 0x00b104e0, ; rise, avg (16|M0) r38.0<1>:uw r37.0<16;16,1>:uw r39.0<16;16,1>:uw
0x00800040, 0x23801248, 0x12b104a0, 0x00b104c0, ; rise, add (16|M0) r28.0<1>:uw r37.0<16;16,1>:uw r38.0<16;16,1>:uw
0x00800008, 0x23801248, 0x16b10380, 0x00010001, ; rise, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24601248, 0x12b104e0, 0x00b104c0, ; rise, add (16|M0) r35.0<1>:uw r39.0<16;16,1>:uw r38.0<16;16,1>:uw
0x00800008, 0x24601248, 0x16b10460, 0x00010001, ; rise, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44200208, 0x00690460, 0x00000000, ; rise, mov (4|M0) r33.0<2>:ud r35.0<4;4,1>:ud
0x00400001, 0x44240208, 0x00690384, 0x00000000, ; rise, mov (4|M0) r33.1<2>:ud r28.1<4;4,1>:ud
0x00400001, 0x24600208, 0x00690344, 0x00000000, ; rise, mov (4|M0) r35.0<1>:ud r26.1<4;4,1>:ud
0x00000001, 0x24700208, 0x00000354, 0x00000000, ; rise, mov (1|M0) r35.4<1>:ud r26.5<0;1,0>:ud
0x00800042, 0x23801248, 0x12b10340, 0x00b10460, ; rise, avg (16|M0) r28.0<1>:uw r26.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00800040, 0x24c01248, 0x12b10340, 0x00b10380, ; rise, add (16|M0) r38.0<1>:uw r26.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800008, 0x24c01248, 0x16b104c0, 0x00010001, ; rise, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24e01248, 0x12b10460, 0x00b10380, ; rise, add (16|M0) r39.0<1>:uw r35.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00800008, 0x24e01248, 0x16b104e0, 0x00010001, ; rise, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43c00208, 0x006904e0, 0x00000000, ; rise, mov (4|M0) r30.0<2>:ud r39.0<4;4,1>:ud
0x00400001, 0x43c40208, 0x006904c4, 0x00000000, ; rise, mov (4|M0) r30.1<2>:ud r38.1<4;4,1>:ud
0x00400001, 0x23400208, 0x00690484, 0x00000000, ; rise, mov (4|M0) r26.0<1>:ud r36.1<4;4,1>:ud
0x00000001, 0x23500208, 0x00000494, 0x00000000, ; rise, mov (1|M0) r26.4<1>:ud r36.5<0;1,0>:ud
0x00800042, 0x24a01248, 0x12b10480, 0x00b10340, ; rise, avg (16|M0) r37.0<1>:uw r36.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800040, 0x24e01248, 0x12b10480, 0x00b104a0, ; rise, add (16|M0) r39.0<1>:uw r36.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00800008, 0x24e01248, 0x16b104e0, 0x00010001, ; rise, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24c01248, 0x12b10340, 0x00b104a0, ; rise, add (16|M0) r38.0<1>:uw r26.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00800008, 0x24c01248, 0x16b104c0, 0x00010001, ; rise, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43800208, 0x006904c0, 0x00000000, ; rise, mov (4|M0) r28.0<2>:ud r38.0<4;4,1>:ud
0x00400001, 0x43840208, 0x006904e4, 0x00000000, ; rise, mov (4|M0) r28.1<2>:ud r39.1<4;4,1>:ud
0x00400001, 0x24c00208, 0x00690284, 0x00000000, ; rise, mov (4|M0) r38.0<1>:ud r20.1<4;4,1>:ud
0x00000001, 0x24d00208, 0x00000294, 0x00000000, ; rise, mov (1|M0) r38.4<1>:ud r20.5<0;1,0>:ud
0x00800042, 0x24e01248, 0x12b10280, 0x00b104c0, ; rise, avg (16|M0) r39.0<1>:uw r20.0<16;16,1>:uw r38.0<16;16,1>:uw
0x00800040, 0x24a01248, 0x12b10280, 0x00b104e0, ; rise, add (16|M0) r37.0<1>:uw r20.0<16;16,1>:uw r39.0<16;16,1>:uw
0x00800008, 0x24a01248, 0x16b104a0, 0x00010001, ; rise, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw
0x00800040, 0x23401248, 0x12b104c0, 0x00b104e0, ; rise, add (16|M0) r26.0<1>:uw r38.0<16;16,1>:uw r39.0<16;16,1>:uw
0x00800008, 0x23401248, 0x16b10340, 0x00010001, ; rise, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44600208, 0x00690340, 0x00000000, ; rise, mov (4|M0) r35.0<2>:ud r26.0<4;4,1>:ud
0x00400001, 0x44640208, 0x006904a4, 0x00000000, ; rise, mov (4|M0) r35.1<2>:ud r37.1<4;4,1>:ud
0x00400001, 0x22800208, 0x006902a4, 0x00000000, ; rise, mov (4|M0) r20.0<1>:ud r21.1<4;4,1>:ud
0x00000001, 0x22900208, 0x000002b4, 0x00000000, ; rise, mov (1|M0) r20.4<1>:ud r21.5<0;1,0>:ud
0x00800042, 0x24801248, 0x12b102a0, 0x00b10280, ; rise, avg (16|M0) r36.0<1>:uw r21.0<16;16,1>:uw r20.0<16;16,1>:uw
0x00800040, 0x23401248, 0x12b102a0, 0x00b10480, ; rise, add (16|M0) r26.0<1>:uw r21.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00800008, 0x23401248, 0x16b10340, 0x00010001, ; rise, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24a01248, 0x12b10280, 0x00b10480, ; rise, add (16|M0) r37.0<1>:uw r20.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00800008, 0x24a01248, 0x16b104a0, 0x00010001, ; rise, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44e00208, 0x006904a0, 0x00000000, ; rise, mov (4|M0) r39.0<2>:ud r37.0<4;4,1>:ud
0x00400001, 0x44e40208, 0x00690344, 0x00000000, ; rise, mov (4|M0) r39.1<2>:ud r26.1<4;4,1>:ud
0x00400001, 0x24a00208, 0x00690524, 0x00000000, ; rise, mov (4|M0) r37.0<1>:ud r41.1<4;4,1>:ud
0x00000001, 0x24b00208, 0x00000534, 0x00000000, ; rise, mov (1|M0) r37.4<1>:ud r41.5<0;1,0>:ud
0x00800042, 0x23401248, 0x12b10520, 0x00b104a0, ; rise, avg (16|M0) r26.0<1>:uw r41.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00800040, 0x24801248, 0x12b10520, 0x00b10340, ; rise, add (16|M0) r36.0<1>:uw r41.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x24801248, 0x16b10480, 0x00010001, ; rise, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw
0x00800040, 0x22801248, 0x12b104a0, 0x00b10340, ; rise, add (16|M0) r20.0<1>:uw r37.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00800008, 0x22801248, 0x16b10280, 0x00010001, ; rise, shr (16|M0) r20.0<1>:uw r20.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44c00208, 0x00690280, 0x00000000, ; rise, mov (4|M0) r38.0<2>:ud r20.0<4;4,1>:ud
0x00400001, 0x44c40208, 0x00690484, 0x00000000, ; rise, mov (4|M0) r38.1<2>:ud r36.1<4;4,1>:ud
0x00400001, 0x25200208, 0x00690444, 0x00000000, ; rise, mov (4|M0) r41.0<1>:ud r34.1<4;4,1>:ud
0x00000001, 0x25300208, 0x00000454, 0x00000000, ; rise, mov (1|M0) r41.4<1>:ud r34.5<0;1,0>:ud
0x00800042, 0x22a01248, 0x12b10440, 0x00b10520, ; rise, avg (16|M0) r21.0<1>:uw r34.0<16;16,1>:uw r41.0<16;16,1>:uw
0x00800040, 0x22801248, 0x12b10440, 0x00b102a0, ; rise, add (16|M0) r20.0<1>:uw r34.0<16;16,1>:uw r21.0<16;16,1>:uw
0x00800008, 0x22801248, 0x16b10280, 0x00010001, ; rise, shr (16|M0) r20.0<1>:uw r20.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24801248, 0x12b10520, 0x00b102a0, ; rise, add (16|M0) r36.0<1>:uw r41.0<16;16,1>:uw r21.0<16;16,1>:uw
0x00800008, 0x24801248, 0x16b10480, 0x00010001, ; rise, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw
0x00400001, 0x43400208, 0x00690480, 0x00000000, ; rise, mov (4|M0) r26.0<2>:ud r36.0<4;4,1>:ud
0x00400001, 0x43440208, 0x00690284, 0x00000000, ; rise, mov (4|M0) r26.1<2>:ud r20.1<4;4,1>:ud
0x00400001, 0x24800208, 0x00690544, 0x00000000, ; rise, mov (4|M0) r36.0<1>:ud r42.1<4;4,1>:ud
0x00000001, 0x24900208, 0x00000554, 0x00000000, ; rise, mov (1|M0) r36.4<1>:ud r42.5<0;1,0>:ud
0x00800042, 0x22801248, 0x12b10540, 0x00b10480, ; rise, avg (16|M0) r20.0<1>:uw r42.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00800040, 0x22a01248, 0x12b10540, 0x00b10280, ; rise, add (16|M0) r21.0<1>:uw r42.0<16;16,1>:uw r20.0<16;16,1>:uw
0x00800008, 0x22a01248, 0x16b102a0, 0x00010001, ; rise, shr (16|M0) r21.0<1>:uw r21.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25201248, 0x12b10480, 0x00b10280, ; rise, add (16|M0) r41.0<1>:uw r36.0<16;16,1>:uw r20.0<16;16,1>:uw
0x00800008, 0x25201248, 0x16b10520, 0x00010001, ; rise, shr (16|M0) r41.0<1>:uw r41.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44a00208, 0x00690520, 0x00000000, ; rise, mov (4|M0) r37.0<2>:ud r41.0<4;4,1>:ud
0x00400001, 0x44a40208, 0x006902a4, 0x00000000, ; rise, mov (4|M0) r37.1<2>:ud r21.1<4;4,1>:ud
0x00400001, 0x25400208, 0x00690564, 0x00000000, ; rise, mov (4|M0) r42.0<1>:ud r43.1<4;4,1>:ud
0x00000001, 0x25500208, 0x00000574, 0x00000000, ; rise, mov (1|M0) r42.4<1>:ud r43.5<0;1,0>:ud
0x00800042, 0x24401248, 0x12b10560, 0x00b10540, ; rise, avg (16|M0) r34.0<1>:uw r43.0<16;16,1>:uw r42.0<16;16,1>:uw
0x00800040, 0x25201248, 0x12b10560, 0x00b10440, ; rise, add (16|M0) r41.0<1>:uw r43.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x25201248, 0x16b10520, 0x00010001, ; rise, shr (16|M0) r41.0<1>:uw r41.0<16;16,1>:uw 0x1:uw
0x00800040, 0x22a01248, 0x12b10540, 0x00b10440, ; rise, add (16|M0) r21.0<1>:uw r42.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800008, 0x22a01248, 0x16b102a0, 0x00010001, ; rise, shr (16|M0) r21.0<1>:uw r21.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42800208, 0x006902a0, 0x00000000, ; rise, mov (4|M0) r20.0<2>:ud r21.0<4;4,1>:ud
0x00400001, 0x42840208, 0x00690524, 0x00000000, ; rise, mov (4|M0) r20.1<2>:ud r41.1<4;4,1>:ud
0x00400001, 0x22a00208, 0x006905a4, 0x00000000, ; rise, mov (4|M0) r21.0<1>:ud r45.1<4;4,1>:ud
0x00000001, 0x22b00208, 0x000005b4, 0x00000000, ; rise, mov (1|M0) r21.4<1>:ud r45.5<0;1,0>:ud
0x00800042, 0x25201248, 0x12b105a0, 0x00b102a0, ; rise, avg (16|M0) r41.0<1>:uw r45.0<16;16,1>:uw r21.0<16;16,1>:uw
0x00800040, 0x24401248, 0x12b105a0, 0x00b10520, ; rise, add (16|M0) r34.0<1>:uw r45.0<16;16,1>:uw r41.0<16;16,1>:uw
0x00800008, 0x24401248, 0x16b10440, 0x00010001, ; rise, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25401248, 0x12b102a0, 0x00b10520, ; rise, add (16|M0) r42.0<1>:uw r21.0<16;16,1>:uw r41.0<16;16,1>:uw
0x00800008, 0x25401248, 0x16b10540, 0x00010001, ; rise, shr (16|M0) r42.0<1>:uw r42.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44800208, 0x00690540, 0x00000000, ; rise, mov (4|M0) r36.0<2>:ud r42.0<4;4,1>:ud
0x00400001, 0x44840208, 0x00690444, 0x00000000, ; rise, mov (4|M0) r36.1<2>:ud r34.1<4;4,1>:ud
0x00400001, 0x25a00208, 0x00690584, 0x00000000, ; rise, mov (4|M0) r45.0<1>:ud r44.1<4;4,1>:ud
0x00000001, 0x25b00208, 0x00000594, 0x00000000, ; rise, mov (1|M0) r45.4<1>:ud r44.5<0;1,0>:ud
0x00800042, 0x25601248, 0x12b10580, 0x00b105a0, ; rise, avg (16|M0) r43.0<1>:uw r44.0<16;16,1>:uw r45.0<16;16,1>:uw
0x00800040, 0x25401248, 0x12b10580, 0x00b10560, ; rise, add (16|M0) r42.0<1>:uw r44.0<16;16,1>:uw r43.0<16;16,1>:uw
0x00800008, 0x25401248, 0x16b10540, 0x00010001, ; rise, shr (16|M0) r42.0<1>:uw r42.0<16;16,1>:uw 0x1:uw
0x00800040, 0x24401248, 0x12b105a0, 0x00b10560, ; rise, add (16|M0) r34.0<1>:uw r45.0<16;16,1>:uw r43.0<16;16,1>:uw
0x00800008, 0x24401248, 0x16b10440, 0x00010001, ; rise, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw
0x00400001, 0x45200208, 0x00690440, 0x00000000, ; rise, mov (4|M0) r41.0<2>:ud r34.0<4;4,1>:ud
0x00400001, 0x45240208, 0x00690544, 0x00000000, ; rise, mov (4|M0) r41.1<2>:ud r42.1<4;4,1>:ud
0x00400001, 0x24400208, 0x006905c4, 0x00000000, ; rise, mov (4|M0) r34.0<1>:ud r46.1<4;4,1>:ud
0x00000001, 0x24500208, 0x000005d4, 0x00000000, ; rise, mov (1|M0) r34.4<1>:ud r46.5<0;1,0>:ud
0x00800042, 0x25401248, 0x12b105c0, 0x00b10440, ; rise, avg (16|M0) r42.0<1>:uw r46.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00800040, 0x25601248, 0x12b105c0, 0x00b10540, ; rise, add (16|M0) r43.0<1>:uw r46.0<16;16,1>:uw r42.0<16;16,1>:uw
0x00800008, 0x25601248, 0x16b10560, 0x00010001, ; rise, shr (16|M0) r43.0<1>:uw r43.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25a01248, 0x12b10440, 0x00b10540, ; rise, add (16|M0) r45.0<1>:uw r34.0<16;16,1>:uw r42.0<16;16,1>:uw
0x00800008, 0x25a01248, 0x16b105a0, 0x00010001, ; rise, shr (16|M0) r45.0<1>:uw r45.0<16;16,1>:uw 0x1:uw
0x00400001, 0x42a00208, 0x006905a0, 0x00000000, ; rise, mov (4|M0) r21.0<2>:ud r45.0<4;4,1>:ud
0x00400001, 0x42a40208, 0x00690564, 0x00000000, ; rise, mov (4|M0) r21.1<2>:ud r43.1<4;4,1>:ud
0x00400001, 0x25c00208, 0x006905e4, 0x00000000, ; rise, mov (4|M0) r46.0<1>:ud r47.1<4;4,1>:ud
0x00000001, 0x25d00208, 0x000005f4, 0x00000000, ; rise, mov (1|M0) r46.4<1>:ud r47.5<0;1,0>:ud
0x00800042, 0x25801248, 0x12b105e0, 0x00b105c0, ; rise, avg (16|M0) r44.0<1>:uw r47.0<16;16,1>:uw r46.0<16;16,1>:uw
0x00800040, 0x25a01248, 0x12b105e0, 0x00b10580, ; rise, add (16|M0) r45.0<1>:uw r47.0<16;16,1>:uw r44.0<16;16,1>:uw
0x00800008, 0x25a01248, 0x16b105a0, 0x00010001, ; rise, shr (16|M0) r45.0<1>:uw r45.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25601248, 0x12b105c0, 0x00b10580, ; rise, add (16|M0) r43.0<1>:uw r46.0<16;16,1>:uw r44.0<16;16,1>:uw
0x00800008, 0x25601248, 0x16b10560, 0x00010001, ; rise, shr (16|M0) r43.0<1>:uw r43.0<16;16,1>:uw 0x1:uw
0x00400001, 0x45400208, 0x00690560, 0x00000000, ; rise, mov (4|M0) r42.0<2>:ud r43.0<4;4,1>:ud
0x00400001, 0x45440208, 0x006905a4, 0x00000000, ; rise, mov (4|M0) r42.1<2>:ud r45.1<4;4,1>:ud
0x00400001, 0x25600208, 0x00690604, 0x00000000, ; rise, mov (4|M0) r43.0<1>:ud r48.1<4;4,1>:ud
0x00000001, 0x25700208, 0x00000614, 0x00000000, ; rise, mov (1|M0) r43.4<1>:ud r48.5<0;1,0>:ud
0x00800042, 0x25a01248, 0x12b10600, 0x00b10560, ; rise, avg (16|M0) r45.0<1>:uw r48.0<16;16,1>:uw r43.0<16;16,1>:uw
0x00800040, 0x25801248, 0x12b10600, 0x00b105a0, ; rise, add (16|M0) r44.0<1>:uw r48.0<16;16,1>:uw r45.0<16;16,1>:uw
0x00800008, 0x25801248, 0x16b10580, 0x00010001, ; rise, shr (16|M0) r44.0<1>:uw r44.0<16;16,1>:uw 0x1:uw
0x00800040, 0x25c01248, 0x12b10560, 0x00b105a0, ; rise, add (16|M0) r46.0<1>:uw r43.0<16;16,1>:uw r45.0<16;16,1>:uw
0x00800008, 0x25c01248, 0x16b105c0, 0x00010001, ; rise, shr (16|M0) r46.0<1>:uw r46.0<16;16,1>:uw 0x1:uw
0x00400001, 0x44400208, 0x006905c0, 0x00000000, ; rise, mov (4|M0) r34.0<2>:ud r46.0<4;4,1>:ud
0x00400001, 0x44440208, 0x00690584, 0x00000000, ; rise, mov (4|M0) r34.1<2>:ud r44.1<4;4,1>:ud
0x00800040, 0x24001a68, 0x1ab10400, 0x00b10f00, ; rise, add (16|M0) r32.0<1>:w r32.0<16;16,1>:w r120.0<16;16,1>:w
0x04800002, 0x24001a68, 0x1eb10400, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r32.0<1>:w r32.0<16;16,1>:w 0:w
0x05800002, 0x24001a68, 0x1eb10400, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r32.0<1>:w r32.0<16;16,1>:w 255:w
0x00800040, 0x23e01a68, 0x1ab103e0, 0x00b10f20, ; rise, add (16|M0) r31.0<1>:w r31.0<16;16,1>:w r121.0<16;16,1>:w
0x04800002, 0x23e01a68, 0x1eb103e0, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r31.0<1>:w r31.0<16;16,1>:w 0:w
0x05800002, 0x23e01a68, 0x1eb103e0, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r31.0<1>:w r31.0<16;16,1>:w 255:w
0x00600009, 0x23e00208, 0x068d03e0, 0x00000008, ; rise, shl (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud
0x00600006, 0x20a00208, 0x028d0400, 0x008d03e0, ; rise, or (8|M0) r5.0<1>:ud r32.0<8;8,1>:ud r31.0<8;8,1>:ud
0x00800040, 0x24201a68, 0x1ab10420, 0x00b10f40, ; rise, add (16|M0) r33.0<1>:w r33.0<16;16,1>:w r122.0<16;16,1>:w
0x04800002, 0x24201a68, 0x1eb10420, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r33.0<1>:w r33.0<16;16,1>:w 0:w
0x05800002, 0x24201a68, 0x1eb10420, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r33.0<1>:w r33.0<16;16,1>:w 255:w
0x00800040, 0x23c01a68, 0x1ab103c0, 0x00b10f60, ; rise, add (16|M0) r30.0<1>:w r30.0<16;16,1>:w r123.0<16;16,1>:w
0x04800002, 0x23c01a68, 0x1eb103c0, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r30.0<1>:w r30.0<16;16,1>:w 0:w
0x05800002, 0x23c01a68, 0x1eb103c0, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r30.0<1>:w r30.0<16;16,1>:w 255:w
0x00600009, 0x23c00208, 0x068d03c0, 0x00000008, ; rise, shl (8|M0) r30.0<1>:ud r30.0<8;8,1>:ud 0x8:ud
0x00600006, 0x20c00208, 0x028d0420, 0x008d03c0, ; rise, or (8|M0) r6.0<1>:ud r33.0<8;8,1>:ud r30.0<8;8,1>:ud
0x00800040, 0x23801a68, 0x1ab10380, 0x00b10f00, ; rise, add (16|M0) r28.0<1>:w r28.0<16;16,1>:w r120.0<16;16,1>:w
0x04800002, 0x23801a68, 0x1eb10380, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r28.0<1>:w r28.0<16;16,1>:w 0:w
0x05800002, 0x23801a68, 0x1eb10380, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r28.0<1>:w r28.0<16;16,1>:w 255:w
0x00800040, 0x24601a68, 0x1ab10460, 0x00b10f20, ; rise, add (16|M0) r35.0<1>:w r35.0<16;16,1>:w r121.0<16;16,1>:w
0x04800002, 0x24601a68, 0x1eb10460, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r35.0<1>:w r35.0<16;16,1>:w 0:w
0x05800002, 0x24601a68, 0x1eb10460, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r35.0<1>:w r35.0<16;16,1>:w 255:w
0x00600009, 0x24600208, 0x068d0460, 0x00000008, ; rise, shl (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud
0x00600006, 0x20e00208, 0x028d0380, 0x008d0460, ; rise, or (8|M0) r7.0<1>:ud r28.0<8;8,1>:ud r35.0<8;8,1>:ud
0x00800040, 0x24e01a68, 0x1ab104e0, 0x00b10f40, ; rise, add (16|M0) r39.0<1>:w r39.0<16;16,1>:w r122.0<16;16,1>:w
0x04800002, 0x24e01a68, 0x1eb104e0, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r39.0<1>:w r39.0<16;16,1>:w 0:w
0x05800002, 0x24e01a68, 0x1eb104e0, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r39.0<1>:w r39.0<16;16,1>:w 255:w
0x00800040, 0x24c01a68, 0x1ab104c0, 0x00b10f60, ; rise, add (16|M0) r38.0<1>:w r38.0<16;16,1>:w r123.0<16;16,1>:w
0x04800002, 0x24c01a68, 0x1eb104c0, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r38.0<1>:w r38.0<16;16,1>:w 0:w
0x05800002, 0x24c01a68, 0x1eb104c0, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r38.0<1>:w r38.0<16;16,1>:w 255:w
0x00600009, 0x24c00208, 0x068d04c0, 0x00000008, ; rise, shl (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud 0x8:ud
0x00600006, 0x21000208, 0x028d04e0, 0x008d04c0, ; rise, or (8|M0) r8.0<1>:ud r39.0<8;8,1>:ud r38.0<8;8,1>:ud
0x00800040, 0x23401a68, 0x1ab10340, 0x00b10f00, ; rise, add (16|M0) r26.0<1>:w r26.0<16;16,1>:w r120.0<16;16,1>:w
0x04800002, 0x23401a68, 0x1eb10340, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r26.0<1>:w r26.0<16;16,1>:w 0:w
0x05800002, 0x23401a68, 0x1eb10340, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r26.0<1>:w r26.0<16;16,1>:w 255:w
0x00800040, 0x24a01a68, 0x1ab104a0, 0x00b10f20, ; rise, add (16|M0) r37.0<1>:w r37.0<16;16,1>:w r121.0<16;16,1>:w
0x04800002, 0x24a01a68, 0x1eb104a0, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r37.0<1>:w r37.0<16;16,1>:w 0:w
0x05800002, 0x24a01a68, 0x1eb104a0, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r37.0<1>:w r37.0<16;16,1>:w 255:w
0x00600009, 0x24a00208, 0x068d04a0, 0x00000008, ; rise, shl (8|M0) r37.0<1>:ud r37.0<8;8,1>:ud 0x8:ud
0x00600006, 0x21200208, 0x028d0340, 0x008d04a0, ; rise, or (8|M0) r9.0<1>:ud r26.0<8;8,1>:ud r37.0<8;8,1>:ud
0x00800040, 0x22801a68, 0x1ab10280, 0x00b10f40, ; rise, add (16|M0) r20.0<1>:w r20.0<16;16,1>:w r122.0<16;16,1>:w
0x04800002, 0x22801a68, 0x1eb10280, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r20.0<1>:w r20.0<16;16,1>:w 0:w
0x05800002, 0x22801a68, 0x1eb10280, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r20.0<1>:w r20.0<16;16,1>:w 255:w
0x00800040, 0x24801a68, 0x1ab10480, 0x00b10f60, ; rise, add (16|M0) r36.0<1>:w r36.0<16;16,1>:w r123.0<16;16,1>:w
0x04800002, 0x24801a68, 0x1eb10480, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r36.0<1>:w r36.0<16;16,1>:w 0:w
0x05800002, 0x24801a68, 0x1eb10480, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r36.0<1>:w r36.0<16;16,1>:w 255:w
0x00600009, 0x24800208, 0x068d0480, 0x00000008, ; rise, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud
0x00600006, 0x21400208, 0x028d0280, 0x008d0480, ; rise, or (8|M0) r10.0<1>:ud r20.0<8;8,1>:ud r36.0<8;8,1>:ud
0x00800040, 0x25201a68, 0x1ab10520, 0x00b10f00, ; rise, add (16|M0) r41.0<1>:w r41.0<16;16,1>:w r120.0<16;16,1>:w
0x04800002, 0x25201a68, 0x1eb10520, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r41.0<1>:w r41.0<16;16,1>:w 0:w
0x05800002, 0x25201a68, 0x1eb10520, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r41.0<1>:w r41.0<16;16,1>:w 255:w
0x00800040, 0x22a01a68, 0x1ab102a0, 0x00b10f20, ; rise, add (16|M0) r21.0<1>:w r21.0<16;16,1>:w r121.0<16;16,1>:w
0x04800002, 0x22a01a68, 0x1eb102a0, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r21.0<1>:w r21.0<16;16,1>:w 0:w
0x05800002, 0x22a01a68, 0x1eb102a0, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r21.0<1>:w r21.0<16;16,1>:w 255:w
0x00600009, 0x22a00208, 0x068d02a0, 0x00000008, ; rise, shl (8|M0) r21.0<1>:ud r21.0<8;8,1>:ud 0x8:ud
0x00600006, 0x21600208, 0x028d0520, 0x008d02a0, ; rise, or (8|M0) r11.0<1>:ud r41.0<8;8,1>:ud r21.0<8;8,1>:ud
0x00800040, 0x25401a68, 0x1ab10540, 0x00b10f40, ; rise, add (16|M0) r42.0<1>:w r42.0<16;16,1>:w r122.0<16;16,1>:w
0x04800002, 0x25401a68, 0x1eb10540, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r42.0<1>:w r42.0<16;16,1>:w 0:w
0x05800002, 0x25401a68, 0x1eb10540, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r42.0<1>:w r42.0<16;16,1>:w 255:w
0x00800040, 0x24401a68, 0x1ab10440, 0x00b10f60, ; rise, add (16|M0) r34.0<1>:w r34.0<16;16,1>:w r123.0<16;16,1>:w
0x04800002, 0x24401a68, 0x1eb10440, 0x00000000, ; rise, sel (16|M0) (ge)f0.0 r34.0<1>:w r34.0<16;16,1>:w 0:w
0x05800002, 0x24401a68, 0x1eb10440, 0x00ff00ff, ; rise, sel (16|M0) (lt)f0.0 r34.0<1>:w r34.0<16;16,1>:w 255:w
0x00600009, 0x24400208, 0x068d0440, 0x00000008, ; rise, shl (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x8:ud
0x00600006, 0x21800208, 0x028d0540, 0x008d0440, ; rise, or (8|M0) r12.0<1>:ud r42.0<8;8,1>:ud r34.0<8;8,1>:ud
0x0c800031, 0x20000240, 0x06000080, 0x120a8000, ; rise, send (16|M0) null:uw r4 0xC 0x120A8000
0x00400001, 0x22200208, 0x006901c4, 0x00000000, ; rise, mov (4|M0) r17.0<1>:ud r14.1<4;4,1>:ud
0x00200001, 0x22300208, 0x004501d4, 0x00000000, ; rise, mov (2|M0) r17.4<1>:ud r14.5<2;2,1>:ud
0x00000001, 0x22380208, 0x000001dc, 0x00000000, ; rise, mov (1|M0) r17.6<1>:ud r14.7<0;1,0>:ud
0x00600001, 0x21c00208, 0x008d0220, 0x00000000, ; rise, mov (8|M0) r14.0<1>:ud r17.0<8;8,1>:ud
0x00400001, 0x22200208, 0x006901e4, 0x00000000, ; rise, mov (4|M0) r17.0<1>:ud r15.1<4;4,1>:ud
0x00200001, 0x22300208, 0x004501f4, 0x00000000, ; rise, mov (2|M0) r17.4<1>:ud r15.5<2;2,1>:ud
0x00000001, 0x22380208, 0x000001fc, 0x00000000, ; rise, mov (1|M0) r17.6<1>:ud r15.7<0;1,0>:ud
0x00600001, 0x21e00208, 0x008d0220, 0x00000000, ; rise, mov (8|M0) r15.0<1>:ud r17.0<8;8,1>:ud
0x00400001, 0x22200208, 0x00690204, 0x00000000, ; rise, mov (4|M0) r17.0<1>:ud r16.1<4;4,1>:ud
0x00200001, 0x22300208, 0x00450214, 0x00000000, ; rise, mov (2|M0) r17.4<1>:ud r16.5<2;2,1>:ud
0x00000001, 0x22380208, 0x0000021c, 0x00000000, ; rise, mov (1|M0) r17.6<1>:ud r16.7<0;1,0>:ud
0x00600001, 0x22000208, 0x008d0220, 0x00000000, ; rise, mov (8|M0) r16.0<1>:ud r17.0<8;8,1>:ud
0x00000040, 0x20800208, 0x06000080, 0x00000020, ; rise, add (1|M0) r4.0<1>:ud r4.0<0;1,0>:ud 0x20:ud
0x00000040, 0x20480208, 0x06000048, 0x00000001, ; rise, add (1|M0) r2.2<1>:ud r2.2<0;1,0>:ud 0x1:ud
0x05800010, 0x20000201, 0x02000048, 0x0000004c, ; rise, cmp (16|M0) (lt)f0.1 null<1>:ud r2.2<0;1,0>:ud r2.3<0;1,0>:ud
0x00010020, 0x34000005, 0x0e001400, 0xffffe1a0, ; rise, (W&f0.1) jmpi L448
0x00600001, 0x2e000208, 0x008d0000, 0x00000000, ; rise, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x07800031, 0x20000a40, 0x06000e00, 0x82000010 ; rise, send (16|M0) null:uw r112:d 0x27 0x02000010 {EOT}

; Gen12 (Tiger Lake to Raptor Lake), a kernel NAME12 for each NAME above. Gen12's instructions are another encoding, with each instruction's
; dependencies on the ones before it said in it (the software scoreboard: a distance to an instruction in order, a token for a message), so
; its words are not a patch of Gen9's. Each kernel is its Gen9 source (src/canvas/gpu_i915_NAME.iga; the fill's is gpu_i915_fill.iga, IGT's
; rows as iga64 reads them) put into Gen12's syntax, where the sends alone differ (test/gpu_kernels.py's intel_gen12 says how), and assembled
; with the dependencies iga64 works out:
;     python3 -I test/gpu_kernels.py --intel-gen12 src/canvas/gpu_i915_NAME.iga > NAME12.asm
;     iga64 -a -p=12p1 -Xauto-deps -Xforce-no-compact NAME12.asm -o NAME12.krn
;     od -A n -t x4 -v NAME12.krn                  (the words)
;     iga64 -d -p=12p1 NAME12.krn                  (the comments: the dependencies it set, and the syncs it put after a message that ends a block)
; The same words are test/gpu_kernels.py's NAME_GEN12_WORDS, which --target intel-gen9 holds these to. The kernels jump each thread for itself
; (jmpi), and Gen12's EUs are fused in pairs whose two threads share an instruction pointer: their threads are dispatched one a pair
; (MEDIA_VFE_STATE's Fused EU Dispatch disabled, gpu.c's gpu_i915_frame), or the second of a pair takes the first's jumps.

kernel copy12
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; copy12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; copy12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; copy12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x04050220, 0x00220205, 0x00000000, ; copy12, mov (2|M0) r4.0<1>:ud r2.0<2;2,1>:ud {@2}
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; copy12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00049031, 0x050c0000, 0xc002040c, 0x02400000, ; copy12, send.dc1 (16|M0) r5 r4 null 0x0 0x02190001 {@1,$0}
0x00044031, 0x00000000, 0xc0000414, 0x02a00000, ; copy12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; copy12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; copy12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel fill12
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; fill12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; fill12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; fill12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x04050220, 0x00220205, 0x00000000, ; fill12, mov (2|M0) r4.0<1>:ud r2.0<2;2,1>:ud {@2}
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; fill12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00040061, 0x05050220, 0x00000104, 0x00000000, ; fill12, mov (16|M0) r5.0<1>:ud r1.0<0;1,0>:ud
0x00049031, 0x00000000, 0xc0000414, 0x02a00000, ; fill12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@1,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; fill12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; fill12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel tone12
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; tone12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; tone12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; tone12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x04050220, 0x00220205, 0x00000000, ; tone12, mov (2|M0) r4.0<1>:ud r2.0<2;2,1>:ud {@2}
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; tone12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00049031, 0x050c0000, 0xc000040c, 0x02400000, ; tone12, send.dc1 (16|M0) r5 r4 null 0x0 0x02190000 {@1,$0}
0x00000061, 0x14050220, 0x00000104, 0x00000000, ; tone12, mov (1|M0) r20.0<1>:ud r1.0<0;1,0>:ud
0x00000168, 0x15058220, 0x02001404, 0x00000018, ; tone12, shr (1|M0) r21.0<1>:ud r20.0<0;1,0>:ud 0x18:ud {@1}
0x00000140, 0x1605a220, 0x02001504, 0x000000ff, ; tone12, add (1|M0) r22.0<1>:ud -r21.0<0;1,0>:ud 0xFF:ud {@1}
0x00000065, 0x17058220, 0x02001404, 0x00ff00ff, ; tone12, and (1|M0) r23.0<1>:ud r20.0<0;1,0>:ud 0xFF00FF:ud
0x00000068, 0x19058220, 0x02001404, 0x00000008, ; tone12, shr (1|M0) r25.0<1>:ud r20.0<0;1,0>:ud 0x8:ud
0x00000165, 0x19058220, 0x02001904, 0x00ff00ff, ; tone12, and (1|M0) r25.0<1>:ud r25.0<0;1,0>:ud 0xFF00FF:ud {@1}
0x00022061, 0x0e050220, 0x00100504, 0x00000000, ; tone12, mov (4|M0) r14.0<1>:ud r5.0<1;1,0>:ud {$0.dst}
0x00020065, 0x0a058220, 0x02100504, 0x00ff00ff, ; tone12, and (4|M0) r10.0<1>:ud r5.0<1;1,0>:ud 0xFF00FF:ud
0x00020068, 0x0b058220, 0x02100504, 0x00000008, ; tone12, shr (4|M0) r11.0<1>:ud r5.0<1;1,0>:ud 0x8:ud
0x00020165, 0x0b058220, 0x02100b04, 0x00ff00ff, ; tone12, and (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020341, 0x0a050220, 0x01100a04, 0x00001604, ; tone12, mul (4|M0) r10.0<1>:ud r10.0<1;1,0>:ud r22.0<0;1,0>:uw {@3}
0x00020140, 0x0a058220, 0x02100a04, 0x007f007f, ; tone12, add (4|M0) r10.0<1>:ud r10.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020341, 0x0b050220, 0x01100b04, 0x00001604, ; tone12, mul (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud r22.0<0;1,0>:uw {@3}
0x00020140, 0x0b058220, 0x02100b04, 0x007f007f, ; tone12, add (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020368, 0x0c058220, 0x02100a04, 0x00000008, ; tone12, shr (4|M0) r12.0<1>:ud r10.0<1;1,0>:ud 0x8:ud {@3}
0x00020165, 0x0c058220, 0x02100c04, 0x00ff00ff, ; tone12, and (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0c050220, 0x02100c04, 0x00100a04, ; tone12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud r10.0<1;1,0>:ud {@1}
0x00020140, 0x0c058220, 0x02100c04, 0x00010001, ; tone12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0c058220, 0x02100c04, 0x00000008, ; tone12, shr (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0c058220, 0x02100c04, 0x00ff00ff, ; tone12, and (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020768, 0x0d058220, 0x02100b04, 0x00000008, ; tone12, shr (4|M0) r13.0<1>:ud r11.0<1;1,0>:ud 0x8:ud {@7}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; tone12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0d050220, 0x02100d04, 0x00100b04, ; tone12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r11.0<1;1,0>:ud {@1}
0x00020140, 0x0d058220, 0x02100d04, 0x00010001, ; tone12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0d058220, 0x02100d04, 0x00000008, ; tone12, shr (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; tone12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020740, 0x0c050220, 0x02100c04, 0x00001704, ; tone12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud r23.0<0;1,0>:ud {@7}
0x00020240, 0x0d050220, 0x02100d04, 0x00001904, ; tone12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r25.0<0;1,0>:ud {@2}
0x00030262, 0x0c058110, 0x51460c05, 0x00ff00ff, ; tone12, sel (8|M0) (lt)f0.0 r12.0<1>:uw r12.0<8;8,1>:uw 0xFF:uw {@2}
0x00030262, 0x0d058110, 0x51460d05, 0x00ff00ff, ; tone12, sel (8|M0) (lt)f0.0 r13.0<1>:uw r13.0<8;8,1>:uw 0xFF:uw {@2}
0x00020169, 0x0d058220, 0x02100d04, 0x00000008, ; tone12, shl (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020166, 0x05050220, 0x02100c04, 0x00100d04, ; tone12, or (4|M0) r5.0<1>:ud r12.0<1;1,0>:ud r13.0<1;1,0>:ud {@1}
0x00020070, 0x00018220, 0x12001504, 0x00000000, ; tone12, cmp (4|M0) (eq)f0.0 null<1>:ud r21.0<0;1,0>:ud 0x0:ud
0x01020061, 0x05050220, 0x00100e04, 0x00000000, ; tone12, (f0.0) mov (4|M0) r5.0<1>:ud r14.0<1;1,0>:ud
0x00049031, 0x00000000, 0xc0000414, 0x02a00000, ; tone12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@1,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; tone12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; tone12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel glass12
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; glass12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; glass12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; glass12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x04050220, 0x00220205, 0x00000000, ; glass12, mov (2|M0) r4.0<1>:ud r2.0<2;2,1>:ud {@2}
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; glass12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00049031, 0x050c0000, 0xc002040c, 0x02400000, ; glass12, send.dc1 (16|M0) r5 r4 null 0x0 0x02190001 {@1,$0}
0x00044131, 0x060c0000, 0xc004040c, 0x02400000, ; glass12, send.dc1 (16|M0) r6 r4 null 0x0 0x02190002 {$1}
0x00000065, 0x14058220, 0x02000124, 0x00000001, ; glass12, and (1|M0) r20.0<1>:ud r1.1<0;1,0>:ud 0x1:ud
0x00000040, 0x14250220, 0x020000c4, 0x00000144, ; glass12, add (1|M0) r20.1<1>:ud r0.6<0;1,0>:ud r1.2<0;1,0>:ud
0x00000165, 0x14258220, 0x02001424, 0x00000001, ; glass12, and (1|M0) r20.1<1>:ud r20.1<0;1,0>:ud 0x1:ud {@1}
0x00000167, 0x14450220, 0x02001404, 0x00001424, ; glass12, xor (1|M0) r20.2<1>:ud r20.0<0;1,0>:ud r20.1<0;1,0>:ud {@1}
0x00000169, 0x15058220, 0x02001444, 0x00000001, ; glass12, shl (1|M0) r21.0<1>:ud r20.2<0;1,0>:ud 0x1:ud {@1}
0x00000140, 0x15050220, 0x02001504, 0x00001424, ; glass12, add (1|M0) r21.0<1>:ud r21.0<0;1,0>:ud r20.1<0;1,0>:ud {@1}
0x00000069, 0x15258220, 0x02001444, 0x00000001, ; glass12, shl (1|M0) r21.1<1>:ud r20.2<0;1,0>:ud 0x1:ud
0x00000140, 0x1525a220, 0x02001524, 0x00000002, ; glass12, add (1|M0) r21.1<1>:ud -r21.1<0;1,0>:ud 0x2:ud {@1}
0x00000140, 0x15250220, 0x02001524, 0x00001424, ; glass12, add (1|M0) r21.1<1>:ud r21.1<0;1,0>:ud r20.1<0;1,0>:ud {@1}
0x00000061, 0x16054220, 0x00000000, 0x00000000, ; glass12, mov (1|M0) r22.0<1>:ud 0x0:ud
0x00000070, 0x00018220, 0x22000104, 0x00000000, ; glass12, cmp (1|M0) (ne)f0.0 null<1>:ud r1.0<0;1,0>:ud 0x0:ud
0x01000061, 0x16054220, 0x00000000, 0x00000001, ; glass12, (f0.0) mov (1|M0) r22.0<1>:ud 0x1:ud
0x00000140, 0x1625a220, 0x02001604, 0x00000001, ; glass12, add (1|M0) r22.1<1>:ud -r22.0<0;1,0>:ud 0x1:ud {@1}
0x00000761, 0x17050220, 0x00001504, 0x00000000, ; glass12, mov (1|M0) r23.0<1>:ud r21.0<0;1,0>:ud {@7}
0x00000661, 0x17250220, 0x00001524, 0x00000000, ; glass12, mov (1|M0) r23.1<1>:ud r21.1<0;1,0>:ud {@6}
0x00000061, 0x17450220, 0x00001504, 0x00000000, ; glass12, mov (1|M0) r23.2<1>:ud r21.0<0;1,0>:ud
0x00000061, 0x17650220, 0x00001524, 0x00000000, ; glass12, mov (1|M0) r23.3<1>:ud r21.1<0;1,0>:ud
0x00020141, 0x18050220, 0x01101704, 0x00001624, ; glass12, mul (4|M0) r24.0<1>:ud r23.0<1;1,0>:ud r22.2<0;1,0>:uw {@1}
0x00020041, 0x19050220, 0x01101704, 0x00001604, ; glass12, mul (4|M0) r25.0<1>:ud r23.0<1;1,0>:ud r22.0<0;1,0>:uw
0x00020269, 0x1a058220, 0x02101804, 0x00000010, ; glass12, shl (4|M0) r26.0<1>:ud r24.0<1;1,0>:ud 0x10:ud {@2}
0x00020166, 0x1a050220, 0x02101a04, 0x00101804, ; glass12, or (4|M0) r26.0<1>:ud r26.0<1;1,0>:ud r24.0<1;1,0>:ud {@1}
0x00020369, 0x1b058220, 0x02101904, 0x00000010, ; glass12, shl (4|M0) r27.0<1>:ud r25.0<1;1,0>:ud 0x10:ud {@3}
0x00020166, 0x1b050220, 0x02101b04, 0x00101904, ; glass12, or (4|M0) r27.0<1>:ud r27.0<1;1,0>:ud r25.0<1;1,0>:ud {@1}
0x00022165, 0x07058220, 0x02100604, 0x00ff00ff, ; glass12, and (4|M0) r7.0<1>:ud r6.0<1;1,0>:ud 0xFF00FF:ud {$1.dst}
0x00020068, 0x08058220, 0x02100604, 0x00000008, ; glass12, shr (4|M0) r8.0<1>:ud r6.0<1;1,0>:ud 0x8:ud
0x00020165, 0x08058220, 0x02100804, 0x00ff00ff, ; glass12, and (4|M0) r8.0<1>:ud r8.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00030340, 0x07050550, 0x05460705, 0x00461a05, ; glass12, add (8|M0) r7.0<1>:w r7.0<8;8,1>:w r26.0<8;8,1>:w {@3}
0x00030140, 0x07050550, 0x05460705, 0x02461b05, ; glass12, add (8|M0) r7.0<1>:w r7.0<8;8,1>:w -r27.0<8;8,1>:w {@1}
0x00030340, 0x08050550, 0x05460805, 0x00461805, ; glass12, add (8|M0) r8.0<1>:w r8.0<8;8,1>:w r24.0<8;8,1>:w {@3}
0x00030140, 0x08050550, 0x05460805, 0x02461905, ; glass12, add (8|M0) r8.0<1>:w r8.0<8;8,1>:w -r25.0<8;8,1>:w {@1}
0x00030362, 0x07058550, 0x45460705, 0x00000000, ; glass12, sel (8|M0) (ge)f0.0 r7.0<1>:w r7.0<8;8,1>:w 0:w {@3}
0x00030162, 0x07058550, 0x55460705, 0x00ff00ff, ; glass12, sel (8|M0) (lt)f0.0 r7.0<1>:w r7.0<8;8,1>:w 255:w {@1}
0x00030362, 0x08058550, 0x45460805, 0x00000000, ; glass12, sel (8|M0) (ge)f0.0 r8.0<1>:w r8.0<8;8,1>:w 0:w {@3}
0x00030162, 0x08058550, 0x55460805, 0x00ff00ff, ; glass12, sel (8|M0) (lt)f0.0 r8.0<1>:w r8.0<8;8,1>:w 255:w {@1}
0x00022068, 0x09058220, 0x02100504, 0x00000018, ; glass12, shr (4|M0) r9.0<1>:ud r5.0<1;1,0>:ud 0x18:ud {$0.dst}
0x00020140, 0x0905a220, 0x02100904, 0x000000ff, ; glass12, add (4|M0) r9.0<1>:ud -r9.0<1;1,0>:ud 0xFF:ud {@1}
0x00020065, 0x0a058220, 0x02100504, 0x00ff00ff, ; glass12, and (4|M0) r10.0<1>:ud r5.0<1;1,0>:ud 0xFF00FF:ud
0x00020068, 0x0b058220, 0x02100504, 0x00000008, ; glass12, shr (4|M0) r11.0<1>:ud r5.0<1;1,0>:ud 0x8:ud
0x00020165, 0x0b058220, 0x02100b04, 0x00ff00ff, ; glass12, and (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020441, 0x0c050220, 0x01100704, 0x00440906, ; glass12, mul (4|M0) r12.0<1>:ud r7.0<1;1,0>:ud r9.0<8;4,2>:uw {@4}
0x00020140, 0x0c058220, 0x02100c04, 0x007f007f, ; glass12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020168, 0x0d058220, 0x02100c04, 0x00000008, ; glass12, shr (4|M0) r13.0<1>:ud r12.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; glass12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0d050220, 0x02100d04, 0x00100c04, ; glass12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r12.0<1;1,0>:ud {@1}
0x00020140, 0x0d058220, 0x02100d04, 0x00010001, ; glass12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0d058220, 0x02100d04, 0x00000008, ; glass12, shr (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; glass12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0d050220, 0x02100d04, 0x00100a04, ; glass12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r10.0<1;1,0>:ud {@1}
0x00030162, 0x0d058110, 0x51460d05, 0x00ff00ff, ; glass12, sel (8|M0) (lt)f0.0 r13.0<1>:uw r13.0<8;8,1>:uw 0xFF:uw {@1}
0x00020041, 0x0e050220, 0x01100804, 0x00440906, ; glass12, mul (4|M0) r14.0<1>:ud r8.0<1;1,0>:ud r9.0<8;4,2>:uw
0x00020140, 0x0e058220, 0x02100e04, 0x007f007f, ; glass12, add (4|M0) r14.0<1>:ud r14.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020168, 0x0f058220, 0x02100e04, 0x00000008, ; glass12, shr (4|M0) r15.0<1>:ud r14.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0f058220, 0x02100f04, 0x00ff00ff, ; glass12, and (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0f050220, 0x02100f04, 0x00100e04, ; glass12, add (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud r14.0<1;1,0>:ud {@1}
0x00020140, 0x0f058220, 0x02100f04, 0x00010001, ; glass12, add (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0f058220, 0x02100f04, 0x00000008, ; glass12, shr (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0f058220, 0x02100f04, 0x00ff00ff, ; glass12, and (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0f050220, 0x02100f04, 0x00100b04, ; glass12, add (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud r11.0<1;1,0>:ud {@1}
0x00030162, 0x0f058110, 0x51460f05, 0x00ff00ff, ; glass12, sel (8|M0) (lt)f0.0 r15.0<1>:uw r15.0<8;8,1>:uw 0xFF:uw {@1}
0x00020169, 0x0f058220, 0x02100f04, 0x00000008, ; glass12, shl (4|M0) r15.0<1>:ud r15.0<1;1,0>:ud 0x8:ud {@1}
0x00020166, 0x05050220, 0x02100d04, 0x00100f04, ; glass12, or (4|M0) r5.0<1>:ud r13.0<1;1,0>:ud r15.0<1;1,0>:ud {@1}
0x00049031, 0x00000000, 0xc0000414, 0x02a00000, ; glass12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@1,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; glass12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; glass12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel bx12
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; bx12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; bx12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; bx12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x04050220, 0x00220205, 0x00000000, ; bx12, mov (2|M0) r4.0<1>:ud r2.0<2;2,1>:ud {@2}
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; bx12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00049031, 0x050c0000, 0xc000040c, 0x02400000, ; bx12, send.dc1 (16|M0) r5 r4 null 0x0 0x02190000 {@1,$0}
0x00030061, 0x14050220, 0x00460105, 0x00000000, ; bx12, mov (8|M0) r20.0<1>:ud r1.0<8;8,1>:ud
0x00000168, 0x16058220, 0x02001404, 0x00000018, ; bx12, shr (1|M0) r22.0<1>:ud r20.0<0;1,0>:ud 0x18:ud {@1}
0x00000065, 0x19058220, 0x02001404, 0x00ff00ff, ; bx12, and (1|M0) r25.0<1>:ud r20.0<0;1,0>:ud 0xFF00FF:ud
0x00000068, 0x1a058220, 0x02001404, 0x00000008, ; bx12, shr (1|M0) r26.0<1>:ud r20.0<0;1,0>:ud 0x8:ud
0x00000165, 0x1a058220, 0x02001a04, 0x000000ff, ; bx12, and (1|M0) r26.0<1>:ud r26.0<0;1,0>:ud 0xFF:ud {@1}
0x00000069, 0x29058220, 0x020000c4, 0x00000008, ; bx12, shl (1|M0) r41.0<1>:ud r0.6<0;1,0>:ud 0x8:ud
0x00000140, 0x29058220, 0x02002904, 0x00000008, ; bx12, add (1|M0) r41.0<1>:ud r41.0<0;1,0>:ud 0x8:ud {@1}
0x00000140, 0x2a050660, 0x06001464, 0x02002904, ; bx12, add (1|M0) r42.0<1>:d r20.3<0;1,0>:d -r41.0<0;1,0>:d {@1}
0x00000140, 0x2a058660, 0x06002a04, 0x0000000f, ; bx12, add (1|M0) r42.0<1>:d r42.0<0;1,0>:d 15:d {@1}
0x0000016c, 0x2a058660, 0x06002a04, 0x00000004, ; bx12, asr (1|M0) r42.0<1>:d r42.0<0;1,0>:d 4:d {@1}
0x00000040, 0x2b050660, 0x06001484, 0x02002904, ; bx12, add (1|M0) r43.0<1>:d r20.4<0;1,0>:d -r41.0<0;1,0>:d
0x00000140, 0x2b058660, 0x06002b04, 0x0000000f, ; bx12, add (1|M0) r43.0<1>:d r43.0<0;1,0>:d 15:d {@1}
0x0000016c, 0x2b058660, 0x06002b04, 0x00000004, ; bx12, asr (1|M0) r43.0<1>:d r43.0<0;1,0>:d 4:d {@1}
0x00000462, 0x2a058660, 0x56002a04, 0x00000010, ; bx12, sel (1|M0) (lt)f0.0 r42.0<1>:d r42.0<0;1,0>:d 16:d {@4}
0x00000162, 0x2a058660, 0x46002a04, 0x00000000, ; bx12, sel (1|M0) (ge)f0.0 r42.0<1>:d r42.0<0;1,0>:d 0:d {@1}
0x00000362, 0x2b058660, 0x56002b04, 0x00000010, ; bx12, sel (1|M0) (lt)f0.0 r43.0<1>:d r43.0<0;1,0>:d 16:d {@3}
0x00000162, 0x2b058660, 0x46002b04, 0x00000000, ; bx12, sel (1|M0) (ge)f0.0 r43.0<1>:d r43.0<0;1,0>:d 0:d {@1}
0x00000140, 0x2c050660, 0x06002b04, 0x02002a04, ; bx12, add (1|M0) r44.0<1>:d r43.0<0;1,0>:d -r42.0<0;1,0>:d {@1}
0x00000162, 0x2c058660, 0x46002c04, 0x00000000, ; bx12, sel (1|M0) (ge)f0.0 r44.0<1>:d r44.0<0;1,0>:d 0:d {@1}
0x00000141, 0x18050220, 0x01001604, 0x00002c04, ; bx12, mul (1|M0) r24.0<1>:ud r22.0<0;1,0>:ud r44.0<0;1,0>:uw {@1}
0x00000061, 0x1e054220, 0x00000000, 0x00000000, ; bx12, mov (1|M0) r30.0<1>:ud 0x0:ud
0x00000061, 0x1e254220, 0x00000000, 0x00000001, ; bx12, mov (1|M0) r30.1<1>:ud 0x1:ud
0x00000061, 0x1e454220, 0x00000000, 0x00000002, ; bx12, mov (1|M0) r30.2<1>:ud 0x2:ud
0x00000061, 0x1e654220, 0x00000000, 0x00000003, ; bx12, mov (1|M0) r30.3<1>:ud 0x3:ud
0x00000069, 0x1f058220, 0x02000024, 0x00000002, ; bx12, shl (1|M0) r31.0<1>:ud r0.1<0;1,0>:ud 0x2:ud
0x00020140, 0x1f050220, 0x02101e04, 0x00001f04, ; bx12, add (4|M0) r31.0<1>:ud r30.0<1;1,0>:ud r31.0<0;1,0>:ud {@1}
0x00020169, 0x1f058220, 0x02101f04, 0x00000008, ; bx12, shl (4|M0) r31.0<1>:ud r31.0<1;1,0>:ud 0x8:ud {@1}
0x00020162, 0x20050660, 0x46001424, 0x00101f04, ; bx12, sel (4|M0) (ge)f0.0 r32.0<1>:d r20.1<0;1,0>:d r31.0<1;1,0>:d {@1}
0x00020040, 0x21058660, 0x06101f04, 0x00000100, ; bx12, add (4|M0) r33.0<1>:d r31.0<1;1,0>:d 256:d
0x00020162, 0x21050660, 0x56001444, 0x00102104, ; bx12, sel (4|M0) (lt)f0.0 r33.0<1>:d r20.2<0;1,0>:d r33.0<1;1,0>:d {@1}
0x00020140, 0x22050660, 0x06102104, 0x02102004, ; bx12, add (4|M0) r34.0<1>:d r33.0<1;1,0>:d -r32.0<1;1,0>:d {@1}
0x00020162, 0x22058660, 0x46102204, 0x00000000, ; bx12, sel (4|M0) (ge)f0.0 r34.0<1>:d r34.0<1;1,0>:d 0:d {@1}
0x00020141, 0x23050220, 0x01102204, 0x00001804, ; bx12, mul (4|M0) r35.0<1>:ud r34.0<1;1,0>:ud r24.0<0;1,0>:uw {@1}
0x00020168, 0x23058220, 0x02102304, 0x0000000c, ; bx12, shr (4|M0) r35.0<1>:ud r35.0<1;1,0>:ud 0xC:ud {@1}
0x00020168, 0x24058220, 0x02102304, 0x00000007, ; bx12, shr (4|M0) r36.0<1>:ud r35.0<1;1,0>:ud 0x7:ud {@1}
0x00020140, 0x24050220, 0x02102404, 0x00102304, ; bx12, add (4|M0) r36.0<1>:ud r36.0<1;1,0>:ud r35.0<1;1,0>:ud {@1}
0x00020040, 0x2505a660, 0x06102304, 0x000000ff, ; bx12, add (4|M0) r37.0<1>:d -r35.0<1;1,0>:d 255:d
0x00020241, 0x26050220, 0x01001904, 0x00202404, ; bx12, mul (4|M0) r38.0<1>:ud r25.0<0;1,0>:ud r36.0<2;1,0>:uw {@2}
0x00020140, 0x26058220, 0x02102604, 0x00800080, ; bx12, add (4|M0) r38.0<1>:ud r38.0<1;1,0>:ud 0x800080:ud {@1}
0x00020168, 0x26058220, 0x02102604, 0x00000008, ; bx12, shr (4|M0) r38.0<1>:ud r38.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x26058220, 0x02102604, 0x00ff00ff, ; bx12, and (4|M0) r38.0<1>:ud r38.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020041, 0x27050220, 0x01001a04, 0x00202404, ; bx12, mul (4|M0) r39.0<1>:ud r26.0<0;1,0>:ud r36.0<2;1,0>:uw
0x00020140, 0x27058220, 0x02102704, 0x00000080, ; bx12, add (4|M0) r39.0<1>:ud r39.0<1;1,0>:ud 0x80:ud {@1}
0x00020168, 0x27058220, 0x02102704, 0x00000008, ; bx12, shr (4|M0) r39.0<1>:ud r39.0<1;1,0>:ud 0x8:ud {@1}
0x00020069, 0x28058220, 0x02102304, 0x00000010, ; bx12, shl (4|M0) r40.0<1>:ud r35.0<1;1,0>:ud 0x10:ud
0x00020166, 0x27050220, 0x02102704, 0x00102804, ; bx12, or (4|M0) r39.0<1>:ud r39.0<1;1,0>:ud r40.0<1;1,0>:ud {@1}
0x00022061, 0x0e050220, 0x00100504, 0x00000000, ; bx12, mov (4|M0) r14.0<1>:ud r5.0<1;1,0>:ud {$0.dst}
0x00020165, 0x0a058220, 0x02100e04, 0x00ff00ff, ; bx12, and (4|M0) r10.0<1>:ud r14.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020068, 0x0b058220, 0x02100e04, 0x00000008, ; bx12, shr (4|M0) r11.0<1>:ud r14.0<1;1,0>:ud 0x8:ud
0x00020165, 0x0b058220, 0x02100b04, 0x00ff00ff, ; bx12, and (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020341, 0x0a050220, 0x01100a04, 0x00202504, ; bx12, mul (4|M0) r10.0<1>:ud r10.0<1;1,0>:ud r37.0<2;1,0>:uw {@3}
0x00020140, 0x0a058220, 0x02100a04, 0x007f007f, ; bx12, add (4|M0) r10.0<1>:ud r10.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020341, 0x0b050220, 0x01100b04, 0x00202504, ; bx12, mul (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud r37.0<2;1,0>:uw {@3}
0x00020140, 0x0b058220, 0x02100b04, 0x007f007f, ; bx12, add (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020368, 0x0c058220, 0x02100a04, 0x00000008, ; bx12, shr (4|M0) r12.0<1>:ud r10.0<1;1,0>:ud 0x8:ud {@3}
0x00020165, 0x0c058220, 0x02100c04, 0x00ff00ff, ; bx12, and (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0c050220, 0x02100c04, 0x00100a04, ; bx12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud r10.0<1;1,0>:ud {@1}
0x00020140, 0x0c058220, 0x02100c04, 0x00010001, ; bx12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0c058220, 0x02100c04, 0x00000008, ; bx12, shr (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0c058220, 0x02100c04, 0x00ff00ff, ; bx12, and (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020768, 0x0d058220, 0x02100b04, 0x00000008, ; bx12, shr (4|M0) r13.0<1>:ud r11.0<1;1,0>:ud 0x8:ud {@7}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; bx12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0d050220, 0x02100d04, 0x00100b04, ; bx12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r11.0<1;1,0>:ud {@1}
0x00020140, 0x0d058220, 0x02100d04, 0x00010001, ; bx12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0d058220, 0x02100d04, 0x00000008, ; bx12, shr (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; bx12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020740, 0x0c050220, 0x02100c04, 0x00102604, ; bx12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud r38.0<1;1,0>:ud {@7}
0x00020240, 0x0d050220, 0x02100d04, 0x00102704, ; bx12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r39.0<1;1,0>:ud {@2}
0x00030262, 0x0c058110, 0x51460c05, 0x00ff00ff, ; bx12, sel (8|M0) (lt)f0.0 r12.0<1>:uw r12.0<8;8,1>:uw 0xFF:uw {@2}
0x00030262, 0x0d058110, 0x51460d05, 0x00ff00ff, ; bx12, sel (8|M0) (lt)f0.0 r13.0<1>:uw r13.0<8;8,1>:uw 0xFF:uw {@2}
0x00020169, 0x0d058220, 0x02100d04, 0x00000008, ; bx12, shl (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020166, 0x05050220, 0x02100c04, 0x00100d04, ; bx12, or (4|M0) r5.0<1>:ud r12.0<1;1,0>:ud r13.0<1;1,0>:ud {@1}
0x00049031, 0x00000000, 0xc0000414, 0x02a00000, ; bx12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@1,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; bx12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; bx12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel scene12
0x00000061, 0x07054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r7.0<1>:ud 0x0:ud
0x00000061, 0x07254220, 0x00000000, 0x00000001, ; scene12, mov (1|M0) r7.1<1>:ud 0x1:ud
0x00000061, 0x07454220, 0x00000000, 0x00000002, ; scene12, mov (1|M0) r7.2<1>:ud 0x2:ud
0x00000061, 0x07654220, 0x00000000, 0x00000003, ; scene12, mov (1|M0) r7.3<1>:ud 0x3:ud
0x00000061, 0x07854220, 0x00000000, 0x00000004, ; scene12, mov (1|M0) r7.4<1>:ud 0x4:ud
0x00000061, 0x07a54220, 0x00000000, 0x00000005, ; scene12, mov (1|M0) r7.5<1>:ud 0x5:ud
0x00000061, 0x07c54220, 0x00000000, 0x00000006, ; scene12, mov (1|M0) r7.6<1>:ud 0x6:ud
0x00000061, 0x07e54220, 0x00000000, 0x00000007, ; scene12, mov (1|M0) r7.7<1>:ud 0x7:ud
0x00000069, 0x02058220, 0x02000024, 0x00000006, ; scene12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x6:ud
0x00000140, 0x02050220, 0x02000204, 0x00000124, ; scene12, add (1|M0) r2.0<1>:ud r2.0<0;1,0>:ud r1.1<0;1,0>:ud {@1}
0x00000069, 0x02258220, 0x020000c4, 0x00000003, ; scene12, shl (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000140, 0x02250220, 0x02000224, 0x00000144, ; scene12, add (1|M0) r2.1<1>:ud r2.1<0;1,0>:ud r1.2<0;1,0>:ud {@1}
0x00000340, 0x04058220, 0x02000204, 0x00000040, ; scene12, add (1|M0) r4.0<1>:ud r2.0<0;1,0>:ud 0x40:ud {@3}
0x00c00162, 0x04050220, 0x52000404, 0x00000164, ; scene12, sel (1|M0) (lt)f1.1 r4.0<1>:ud r4.0<0;1,0>:ud r1.3<0;1,0>:ud {@1}
0x00000340, 0x02658220, 0x02000224, 0x00000008, ; scene12, add (1|M0) r2.3<1>:ud r2.1<0;1,0>:ud 0x8:ud {@3}
0x00c00162, 0x02650220, 0x52000264, 0x00000184, ; scene12, sel (1|M0) (lt)f1.1 r2.3<1>:ud r2.3<0;1,0>:ud r1.4<0;1,0>:ud {@1}
0x00030061, 0x0d050220, 0x00460005, 0x00000000, ; scene12, mov (8|M0) r13.0<1>:ud r0.0<8;8,1>:ud
0x00000069, 0x0d058220, 0x02000024, 0x00000008, ; scene12, shl (1|M0) r13.0<1>:ud r0.1<0;1,0>:ud 0x8:ud
0x00000069, 0x0d258220, 0x020000c4, 0x00000003, ; scene12, shl (1|M0) r13.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000061, 0x0d454220, 0x00000000, 0x0007001f, ; scene12, mov (1|M0) r13.2<1>:ud 0x7001F:ud
0x00030061, 0x0c050220, 0x00460005, 0x00000000, ; scene12, mov (8|M0) r12.0<1>:ud r0.0<8;8,1>:ud
0x00000061, 0x0c454220, 0x00000000, 0x0007001f, ; scene12, mov (1|M0) r12.2<1>:ud 0x7001F:ud
0x00030061, 0x0b050220, 0x00460005, 0x00000000, ; scene12, mov (8|M0) r11.0<1>:ud r0.0<8;8,1>:ud
0x00000069, 0x0b058220, 0x02000024, 0x00000006, ; scene12, shl (1|M0) r11.0<1>:ud r0.1<0;1,0>:ud 0x6:ud
0x00000065, 0x0b258220, 0x020000c4, 0xfffffff8, ; scene12, and (1|M0) r11.1<1>:ud r0.6<0;1,0>:ud 0xFFFFFFF8:ud
0x00000061, 0x0b454220, 0x00000000, 0x0007001f, ; scene12, mov (1|M0) r11.2<1>:ud 0x7001F:ud
0x00049031, 0x28440000, 0xc0020b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r40 r11 null 0x0 0x02890001 {@1,$0}
0x00002068, 0x04458220, 0x02002804, 0x00000010, ; scene12, shr (1|M0) r4.2<1>:ud r40.0<0;1,0>:ud 0x10:ud {$0.dst}
0x00000065, 0x04658220, 0x02002804, 0x0000ff00, ; scene12, and (1|M0) r4.3<1>:ud r40.0<0;1,0>:ud 0xFF00:ud
0x00000061, 0x02c54220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r2.6<1>:ud 0x0:ud
0x00000061, 0x04e54220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r4.7<1>:ud 0x0:ud
0x00000061, 0x04254220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r4.1<1>:ud 0x0:ud
0x00440570, 0x00018220, 0x12000444, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r4.2<0;1,0>:ud 0x0:ud {@5}
0x81400120, 0x00004000, 0x00000000, 0x00003090, ; scene12, (W&f0.1) jmpi L12960 {@1}
0x00000665, 0x08058220, 0x02000464, 0x00000100, ; scene12, and (1|M0) r8.0<1>:ud r4.3<0;1,0>:ud 0x100:ud {@6}
0x00440170, 0x00018220, 0x12000804, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.0<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000070, ; scene12, (W&f0.1) jmpi L688 {@1}
0x00000061, 0x04250220, 0x00002824, 0x00000000, ; scene12, mov (1|M0) r4.1<1>:ud r40.1<0;1,0>:ud
0x00000061, 0x0b054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000261, 0x0b250220, 0x00000424, 0x00000000, ; scene12, mov (1|M0) r11.1<1>:ud r4.1<0;1,0>:ud {@2}
0x00049031, 0x28440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r40 r11 null 0x0 0x02890002 {@1,$0}
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; scene12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; scene12, (W) sync.allwr null
0x00000040, 0x02458220, 0x02000204, 0x00000008, ; scene12, add (1|M0) r2.2<1>:ud r2.0<0;1,0>:ud 0x8:ud
0x00000069, 0x02e58220, 0x02000204, 0x00000002, ; scene12, shl (1|M0) r2.7<1>:ud r2.0<0;1,0>:ud 0x2:ud
0x00030040, 0x03050220, 0x02460705, 0x00000204, ; scene12, add (8|M0) r3.0<1>:ud r7.0<8;8,1>:ud r2.0<0;1,0>:ud
0x00000061, 0x02854220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r2.4<1>:ud 0x0:ud
0x00000061, 0x02a54220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r2.5<1>:ud 0x0:ud
0x00000061, 0x04854220, 0x00000000, 0x00000001, ; scene12, mov (1|M0) r4.4<1>:ud 0x1:ud
0x00000061, 0x04a54220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r4.5<1>:ud 0x0:ud
0x00000065, 0x08058220, 0x02000464, 0x00000200, ; scene12, and (1|M0) r8.0<1>:ud r4.3<0;1,0>:ud 0x200:ud
0x00440170, 0x00018220, 0x22000804, 0x00000000, ; scene12, cmp (16|M0) (ne)f0.1 null<1>:ud r8.0<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000260, ; scene12, (W&f0.1) jmpi L1440 {@1}
0x00000061, 0x02a54220, 0x00000000, 0xffffffff, ; scene12, mov (1|M0) r2.5<1>:ud 0xFFFFFFFF:ud
0x00440270, 0x00010220, 0x42000284, 0x00000444, ; scene12, cmp (16|M0) (ge)f0.1 null<1>:ud r2.4<0;1,0>:ud r4.2<0;1,0>:ud {@2}
0x81400120, 0x00004000, 0x00000000, 0x000001e0, ; scene12, (W&f0.1) jmpi L1360 {@1}
0x00000068, 0x08a58220, 0x02000284, 0x00000003, ; scene12, shr (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x3:ud
0x00440170, 0x00010220, 0x120008a4, 0x000002c4, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.5<0;1,0>:ud r2.6<0;1,0>:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000080, ; scene12, (W&f0.1) jmpi L1056 {@1}
0x00000061, 0x02c50220, 0x000008a4, 0x00000000, ; scene12, mov (1|M0) r2.6<1>:ud r8.5<0;1,0>:ud
0x00000069, 0x08c58220, 0x020008a4, 0x00000003, ; scene12, shl (1|M0) r8.6<1>:ud r8.5<0;1,0>:ud 0x3:ud
0x00000061, 0x0b054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000240, 0x0b250220, 0x02000424, 0x000008c4, ; scene12, add (1|M0) r11.1<1>:ud r4.1<0;1,0>:ud r8.6<0;1,0>:ud {@2}
0x00049031, 0x28440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r40 r11 null 0x0 0x02890002 {@1,$0}
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; scene12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; scene12, (W) sync.allwr null
0x00000065, 0x08a58220, 0x02000284, 0x00000007, ; scene12, and (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x7:ud
0x00000169, 0x08a58220, 0x020008a4, 0x00000005, ; scene12, shl (1|M0) r8.5<1>:ud r8.5<0;1,0>:ud 0x5:ud {@1}
0x00000140, 0x10018110, 0x010008a4, 0x05000500, ; scene12, add (1|M0) a0.0<1>:uw r8.10<0;1,0>:uw 0x500:uw {@1}
0x00030161, 0x27050220, 0x00470001, 0x00000000, ; scene12, mov (8|M0) r39.0<1>:ud r[a0.0]<8;8,1>:ud {@1}
0x00000140, 0x08650220, 0x02000204, 0x02002724, ; scene12, add (1|M0) r8.3<1>:ud r2.0<0;1,0>:ud -r39.1<0;1,0>:ud {@1}
0x00000040, 0x08850220, 0x02002764, 0x02000244, ; scene12, add (1|M0) r8.4<1>:ud r39.3<0;1,0>:ud -r2.2<0;1,0>:ud
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00000040, 0x08850220, 0x02000224, 0x02002744, ; scene12, add (1|M0) r8.4<1>:ud r2.1<0;1,0>:ud -r39.2<0;1,0>:ud
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00000040, 0x08850220, 0x02002784, 0x02000264, ; scene12, add (1|M0) r8.4<1>:ud r39.4<0;1,0>:ud -r2.3<0;1,0>:ud
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00000065, 0x08858220, 0x02002704, 0x0000007b, ; scene12, and (1|M0) r8.4<1>:ud r39.0<0;1,0>:ud 0x7B:ud
0x00000140, 0x0885a220, 0x02000884, 0x00000000, ; scene12, add (1|M0) r8.4<1>:ud -r8.4<0;1,0>:ud 0x0:ud {@1}
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00440170, 0x00018660, 0x56000864, 0x00000000, ; scene12, cmp (16|M0) (lt)f0.1 null<1>:d r8.3<0;1,0>:d 0:d {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000020, ; scene12, (W&f0.1) jmpi L1328 {@1}
0x00000061, 0x02a50220, 0x00000284, 0x00000000, ; scene12, mov (1|M0) r2.5<1>:ud r2.4<0;1,0>:ud
0x00000040, 0x02858220, 0x02000284, 0x00000001, ; scene12, add (1|M0) r2.4<1>:ud r2.4<0;1,0>:ud 0x1:ud
0x80000020, 0x00004000, 0x00000000, 0xfffffe20, ; scene12, (W) jmpi L864
0x00000361, 0x02850220, 0x000002a4, 0x00000000, ; scene12, mov (1|M0) r2.4<1>:ud r2.5<0;1,0>:ud {@3}
0x00440570, 0x00018220, 0x220002a4, 0xffffffff, ; scene12, cmp (16|M0) (ne)f0.1 null<1>:ud r2.5<0;1,0>:ud 0xFFFFFFFF:ud {@5}
0x81400120, 0x00004000, 0x00000000, 0x00000030, ; scene12, (W&f0.1) jmpi L1440 {@1}
0x00000061, 0x02854220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r2.4<1>:ud 0x0:ud
0x00000061, 0x04854220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r4.4<1>:ud 0x0:ud
0x00440270, 0x00010220, 0x42000284, 0x00000444, ; scene12, cmp (16|M0) (ge)f0.1 null<1>:ud r2.4<0;1,0>:ud r4.2<0;1,0>:ud {@2}
0x81400120, 0x00004000, 0x00000000, 0x00002c60, ; scene12, (W&f0.1) jmpi L12816 {@1}
0x00000068, 0x08a58220, 0x02000284, 0x00000003, ; scene12, shr (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x3:ud
0x00440170, 0x00010220, 0x120008a4, 0x000002c4, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.5<0;1,0>:ud r2.6<0;1,0>:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000080, ; scene12, (W&f0.1) jmpi L1632 {@1}
0x00000061, 0x02c50220, 0x000008a4, 0x00000000, ; scene12, mov (1|M0) r2.6<1>:ud r8.5<0;1,0>:ud
0x00000069, 0x08c58220, 0x020008a4, 0x00000003, ; scene12, shl (1|M0) r8.6<1>:ud r8.5<0;1,0>:ud 0x3:ud
0x00000061, 0x0b054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000240, 0x0b250220, 0x02000424, 0x000008c4, ; scene12, add (1|M0) r11.1<1>:ud r4.1<0;1,0>:ud r8.6<0;1,0>:ud {@2}
0x00049031, 0x28440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r40 r11 null 0x0 0x02890002 {@1,$0}
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; scene12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; scene12, (W) sync.allwr null
0x00000065, 0x08a58220, 0x02000284, 0x00000007, ; scene12, and (1|M0) r8.5<1>:ud r2.4<0;1,0>:ud 0x7:ud
0x00000169, 0x08a58220, 0x020008a4, 0x00000005, ; scene12, shl (1|M0) r8.5<1>:ud r8.5<0;1,0>:ud 0x5:ud {@1}
0x00000140, 0x10018110, 0x010008a4, 0x05000500, ; scene12, add (1|M0) a0.0<1>:uw r8.10<0;1,0>:uw 0x500:uw {@1}
0x00030161, 0x27050220, 0x00470001, 0x00000000, ; scene12, mov (8|M0) r39.0<1>:ud r[a0.0]<8;8,1>:ud {@1}
0x00000140, 0x08650220, 0x02002764, 0x02000204, ; scene12, add (1|M0) r8.3<1>:ud r39.3<0;1,0>:ud -r2.0<0;1,0>:ud {@1}
0x00000140, 0x08658220, 0x02000864, 0xffffffff, ; scene12, add (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud 0xFFFFFFFF:ud {@1}
0x00000040, 0x08850220, 0x02000244, 0x02002724, ; scene12, add (1|M0) r8.4<1>:ud r2.2<0;1,0>:ud -r39.1<0;1,0>:ud
0x00000140, 0x08858220, 0x02000884, 0xffffffff, ; scene12, add (1|M0) r8.4<1>:ud r8.4<0;1,0>:ud 0xFFFFFFFF:ud {@1}
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00000040, 0x08850220, 0x02002784, 0x02000224, ; scene12, add (1|M0) r8.4<1>:ud r39.4<0;1,0>:ud -r2.1<0;1,0>:ud
0x00000140, 0x08858220, 0x02000884, 0xffffffff, ; scene12, add (1|M0) r8.4<1>:ud r8.4<0;1,0>:ud 0xFFFFFFFF:ud {@1}
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00000040, 0x08850220, 0x02000264, 0x02002744, ; scene12, add (1|M0) r8.4<1>:ud r2.3<0;1,0>:ud -r39.2<0;1,0>:ud
0x00000140, 0x08858220, 0x02000884, 0xffffffff, ; scene12, add (1|M0) r8.4<1>:ud r8.4<0;1,0>:ud 0xFFFFFFFF:ud {@1}
0x00000166, 0x08650220, 0x02000864, 0x00000884, ; scene12, or (1|M0) r8.3<1>:ud r8.3<0;1,0>:ud r8.4<0;1,0>:ud {@1}
0x00440170, 0x00018660, 0x56000864, 0x00000000, ; scene12, cmp (16|M0) (lt)f0.1 null<1>:d r8.3<0;1,0>:d 0:d {@1}
0x81400120, 0x00004000, 0x00000000, 0x00002a90, ; scene12, (W&f0.1) jmpi L12784 {@1}
0x00440170, 0x00018220, 0x22000484, 0x00000000, ; scene12, cmp (16|M0) (ne)f0.1 null<1>:ud r4.4<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x000000a0, ; scene12, (W&f0.1) jmpi L2080 {@1}
0x00044031, 0x0e440000, 0xc0000d0c, 0x02400000, ; scene12, send.dc1 (16|M0) r14 r13 null 0x0 0x02890000 {$0}
0x00032066, 0x26050220, 0x02460e05, 0x00460f05, ; scene12, or (8|M0) r38.0<1>:ud r14.0<8;8,1>:ud r15.0<8;8,1>:ud {$0.dst}
0x00030166, 0x26050220, 0x02462605, 0x00461005, ; scene12, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r16.0<8;8,1>:ud {@1}
0x00030166, 0x26050220, 0x02462605, 0x00461105, ; scene12, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r17.0<8;8,1>:ud {@1}
0x00030166, 0x26050220, 0x02462605, 0x00461205, ; scene12, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r18.0<8;8,1>:ud {@1}
0x00030166, 0x26050220, 0x02462605, 0x00461305, ; scene12, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r19.0<8;8,1>:ud {@1}
0x00030166, 0x26050220, 0x02462605, 0x00461405, ; scene12, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r20.0<8;8,1>:ud {@1}
0x00030166, 0x26050220, 0x02462605, 0x00461505, ; scene12, or (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud r21.0<8;8,1>:ud {@1}
0x00000061, 0x04854220, 0x00000000, 0x00000001, ; scene12, mov (1|M0) r4.4<1>:ud 0x1:ud
0x00030040, 0x09050220, 0x02460305, 0x02002724, ; scene12, add (8|M0) r9.0<1>:ud r3.0<8;8,1>:ud -r39.1<0;1,0>:ud
0x00030040, 0x26052220, 0x02460305, 0x00002764, ; scene12, add (8|M0) r38.0<1>:ud -r3.0<8;8,1>:ud r39.3<0;1,0>:ud
0x00030140, 0x26058220, 0x02462605, 0xffffffff, ; scene12, add (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud 0xFFFFFFFF:ud {@1}
0x00030166, 0x09050220, 0x02460905, 0x00462605, ; scene12, or (8|M0) r9.0<1>:ud r9.0<8;8,1>:ud r38.0<8;8,1>:ud {@1}
0x00000040, 0x08050220, 0x02000224, 0x02002744, ; scene12, add (1|M0) r8.0<1>:ud r2.1<0;1,0>:ud -r39.2<0;1,0>:ud
0x00000040, 0x08250220, 0x02002784, 0x02000224, ; scene12, add (1|M0) r8.1<1>:ud r39.4<0;1,0>:ud -r2.1<0;1,0>:ud
0x00000140, 0x08258220, 0x02000824, 0xffffffff, ; scene12, add (1|M0) r8.1<1>:ud r8.1<0;1,0>:ud 0xFFFFFFFF:ud {@1}
0x00030340, 0x0a050220, 0x02460705, 0x00000804, ; scene12, add (8|M0) r10.0<1>:ud r7.0<8;8,1>:ud r8.0<0;1,0>:ud {@3}
0x00030240, 0x26052220, 0x02460705, 0x00000824, ; scene12, add (8|M0) r38.0<1>:ud -r7.0<8;8,1>:ud r8.1<0;1,0>:ud {@2}
0x00030166, 0x0a050220, 0x02460a05, 0x00462605, ; scene12, or (8|M0) r10.0<1>:ud r10.0<8;8,1>:ud r38.0<8;8,1>:ud {@1}
0x00000065, 0x08458220, 0x02002704, 0x0000007f, ; scene12, and (1|M0) r8.2<1>:ud r39.0<0;1,0>:ud 0x7F:ud
0x00440170, 0x00018220, 0x12000844, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x000014b0, ; scene12, (W&f0.1) jmpi L7568 {@1}
0x00440170, 0x00018220, 0x12000844, 0x00000001, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x1:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00001520, ; scene12, (W&f0.1) jmpi L7712 {@1}
0x00000065, 0x08658220, 0x02000844, 0x0000007e, ; scene12, and (1|M0) r8.3<1>:ud r8.2<0;1,0>:ud 0x7E:ud
0x00440170, 0x00018220, 0x12000864, 0x00000020, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.3<0;1,0>:ud 0x20:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000e30, ; scene12, (W&f0.1) jmpi L5984 {@1}
0x00440170, 0x00018220, 0x12000844, 0x00000030, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x30:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000ec0, ; scene12, (W&f0.1) jmpi L6160 {@1}
0x00000040, 0x0c050220, 0x020027a4, 0x000002e4, ; scene12, add (1|M0) r12.0<1>:ud r39.5<0;1,0>:ud r2.7<0;1,0>:ud
0x00000040, 0x0c250220, 0x020027c4, 0x00000224, ; scene12, add (1|M0) r12.1<1>:ud r39.6<0;1,0>:ud r2.1<0;1,0>:ud
0x80000066, 0x10018220, 0x020027e4, 0x02890000, ; scene12, (W) or (1|M0) a0.0<1>:ud r39.7<0;1,0>:ud 0x2890000:ud
0x00049031, 0x16050000, 0xc0000c04, 0x00000000, ; scene12, send.dc1 (16|M0) r22 r12 null 0x0 a0.0 {@1,$0}
0x00002065, 0x08658220, 0x02002704, 0x00000080, ; scene12, and (1|M0) r8.3<1>:ud r39.0<0;1,0>:ud 0x80:ud {$0.dst}
0x00440170, 0x00018220, 0x12000864, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.3<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x00000390, ; scene12, (W&f0.1) jmpi L3408 {@1}
0x00030065, 0x1e058220, 0x02461605, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r22.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461605, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r22.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461605, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r22.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x16050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r22.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x16050220, 0x02461605, 0x00462005, ; scene12, or (8|M0) r22.0<1>:ud r22.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461705, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r23.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461705, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r23.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461705, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r23.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x17050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r23.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x17050220, 0x02461705, 0x00462005, ; scene12, or (8|M0) r23.0<1>:ud r23.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461805, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r24.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461805, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r24.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461805, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r24.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x18050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r24.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x18050220, 0x02461805, 0x00462005, ; scene12, or (8|M0) r24.0<1>:ud r24.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461905, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r25.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461905, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r25.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461905, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r25.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x19050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r25.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x19050220, 0x02461905, 0x00462005, ; scene12, or (8|M0) r25.0<1>:ud r25.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461a05, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r26.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461a05, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r26.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461a05, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r26.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x1a050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r26.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x1a050220, 0x02461a05, 0x00462005, ; scene12, or (8|M0) r26.0<1>:ud r26.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461b05, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r27.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461b05, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r27.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461b05, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r27.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x1b050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r27.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x1b050220, 0x02461b05, 0x00462005, ; scene12, or (8|M0) r27.0<1>:ud r27.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461c05, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r28.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461c05, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r28.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461c05, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r28.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x1c050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r28.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x1c050220, 0x02461c05, 0x00462005, ; scene12, or (8|M0) r28.0<1>:ud r28.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030065, 0x1e058220, 0x02461d05, 0xff00ff00, ; scene12, and (8|M0) r30.0<1>:ud r29.0<8;8,1>:ud 0xFF00FF00:ud
0x00030068, 0x1f058220, 0x02461d05, 0x00000010, ; scene12, shr (8|M0) r31.0<1>:ud r29.0<8;8,1>:ud 0x10:ud
0x00030165, 0x1f058220, 0x02461f05, 0x000000ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x20058220, 0x02461d05, 0x000000ff, ; scene12, and (8|M0) r32.0<1>:ud r29.0<8;8,1>:ud 0xFF:ud
0x00030169, 0x20058220, 0x02462005, 0x00000010, ; scene12, shl (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x10:ud {@1}
0x00030366, 0x1d050220, 0x02461e05, 0x00461f05, ; scene12, or (8|M0) r29.0<1>:ud r30.0<8;8,1>:ud r31.0<8;8,1>:ud {@3}
0x00030166, 0x1d050220, 0x02461d05, 0x00462005, ; scene12, or (8|M0) r29.0<1>:ud r29.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00440770, 0x00018220, 0x12000844, 0x00000031, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x31:ud {@7}
0x81400120, 0x00004000, 0x00000000, 0x00000b30, ; scene12, (W&f0.1) jmpi L6288 {@1}
0x00440170, 0x00018220, 0x120004a4, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r4.5<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x000009b0, ; scene12, (W&f0.1) jmpi L5936 {@1}
0x00030068, 0x1e058220, 0x02463005, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r48.0<8;8,1>:ud 0x18:ud
0x00030066, 0x16050220, 0x02461605, 0x000004c4, ; scene12, or (8|M0) r22.0<1>:ud r22.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461605, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r22.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461605, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r22.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x16050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r22.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463105, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r49.0<8;8,1>:ud 0x18:ud
0x00030066, 0x17050220, 0x02461705, 0x000004c4, ; scene12, or (8|M0) r23.0<1>:ud r23.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461705, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r23.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461705, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r23.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x17050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r23.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463205, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r50.0<8;8,1>:ud 0x18:ud
0x00030066, 0x18050220, 0x02461805, 0x000004c4, ; scene12, or (8|M0) r24.0<1>:ud r24.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461805, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r24.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461805, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r24.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x18050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r24.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463305, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r51.0<8;8,1>:ud 0x18:ud
0x00030066, 0x19050220, 0x02461905, 0x000004c4, ; scene12, or (8|M0) r25.0<1>:ud r25.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461905, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r25.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461905, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r25.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x19050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r25.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463405, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r52.0<8;8,1>:ud 0x18:ud
0x00030066, 0x1a050220, 0x02461a05, 0x000004c4, ; scene12, or (8|M0) r26.0<1>:ud r26.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461a05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r26.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461a05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r26.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x1a050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r26.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463505, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r53.0<8;8,1>:ud 0x18:ud
0x00030066, 0x1b050220, 0x02461b05, 0x000004c4, ; scene12, or (8|M0) r27.0<1>:ud r27.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461b05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r27.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461b05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r27.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x1b050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r27.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463605, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r54.0<8;8,1>:ud 0x18:ud
0x00030066, 0x1c050220, 0x02461c05, 0x000004c4, ; scene12, or (8|M0) r28.0<1>:ud r28.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461c05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r28.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461c05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r28.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x1c050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r28.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00030068, 0x1e058220, 0x02463705, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r55.0<8;8,1>:ud 0x18:ud
0x00030066, 0x1d050220, 0x02461d05, 0x000004c4, ; scene12, or (8|M0) r29.0<1>:ud r29.0<8;8,1>:ud r4.6<0;1,0>:ud
0x00030165, 0x1f058220, 0x02461d05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r29.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030068, 0x20058220, 0x02461d05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r29.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x1f050220, 0x01461f05, 0x00201e04, ; scene12, mul (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x1f058220, 0x02461f05, 0x00800080, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x800080:ud {@1}
0x00030341, 0x20050220, 0x01462005, 0x00201e04, ; scene12, mul (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x20058220, 0x02462005, 0x00800080, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0x800080:ud {@1}
0x00030368, 0x21058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x1f050220, 0x02461f05, 0x00462105, ; scene12, add (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030168, 0x1f058220, 0x02461f05, 0x00000008, ; scene12, shr (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030668, 0x21058220, 0x02462005, 0x00000008, ; scene12, shr (8|M0) r33.0<1>:ud r32.0<8;8,1>:ud 0x8:ud {@6}
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x20050220, 0x02462005, 0x00462105, ; scene12, add (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030165, 0x20058220, 0x02462005, 0xff00ff00, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF00:ud {@1}
0x00030166, 0x1d050220, 0x02461f05, 0x00462005, ; scene12, or (8|M0) r29.0<1>:ud r31.0<8;8,1>:ud r32.0<8;8,1>:ud {@1}
0x00000061, 0x04a54220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r4.5<1>:ud 0x0:ud
0x80000020, 0x00004000, 0x00000000, 0x00000950, ; scene12, (W) jmpi L8304
0x00440070, 0x00018220, 0x12000844, 0x00000004, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.2<0;1,0>:ud 0x4:ud
0x81400120, 0x00004000, 0x00000000, 0x000007a0, ; scene12, (W&f0.1) jmpi L7904 {@1}
0x80000020, 0x00004000, 0x00000000, 0x00000920, ; scene12, (W) jmpi L8304
0x00000040, 0x0c050220, 0x020027a4, 0x000002e4, ; scene12, add (1|M0) r12.0<1>:ud r39.5<0;1,0>:ud r2.7<0;1,0>:ud
0x00000040, 0x0c250220, 0x020027c4, 0x00000224, ; scene12, add (1|M0) r12.1<1>:ud r39.6<0;1,0>:ud r2.1<0;1,0>:ud
0x80000066, 0x10018220, 0x020027e4, 0x02890000, ; scene12, (W) or (1|M0) a0.0<1>:ud r39.7<0;1,0>:ud 0x2890000:ud
0x00049031, 0x30050000, 0xc0000c04, 0x00000000, ; scene12, send.dc1 (16|M0) r48 r12 null 0x0 a0.0 {@1,$0}
0x00000065, 0x08658220, 0x02002704, 0x00000001, ; scene12, and (1|M0) r8.3<1>:ud r39.0<0;1,0>:ud 0x1:ud
0x00000140, 0x0865a220, 0x02000864, 0x00000000, ; scene12, add (1|M0) r8.3<1>:ud -r8.3<0;1,0>:ud 0x0:ud {@1}
0x00000165, 0x04c58220, 0x02000864, 0xff000000, ; scene12, and (1|M0) r4.6<1>:ud r8.3<0;1,0>:ud 0xFF000000:ud {@1}
0x00000061, 0x04a54220, 0x00000000, 0x00000001, ; scene12, mov (1|M0) r4.5<1>:ud 0x1:ud
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; scene12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; scene12, (W) sync.allwr null
0x80000020, 0x00004000, 0x00000000, 0x000019f0, ; scene12, (W) jmpi L12784
0x00030061, 0x16050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r22.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x17050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r23.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x18050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r24.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x19050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r25.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1a050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r26.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1b050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r27.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1c050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r28.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1d050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r29.0<1>:ud r39.5<0;1,0>:ud
0x00440770, 0x00018220, 0x220004e4, 0x00000000, ; scene12, cmp (16|M0) (ne)f0.1 null<1>:ud r4.7<0;1,0>:ud 0x0:ud {@7}
0x81400120, 0x00004000, 0x00000000, 0x000000d0, ; scene12, (W&f0.1) jmpi L6512 {@1}
0x00000061, 0x0b054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r11.0<1>:ud 0x0:ud
0x00000061, 0x0b254220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r11.1<1>:ud 0x0:ud
0x00049031, 0x40440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r64 r11 null 0x0 0x02890002 {@1,$0}
0x00003061, 0x0b054220, 0x00000000, 0x00000020, ; scene12, mov (1|M0) r11.0<1>:ud 0x20:ud {$0.src}
0x00049131, 0x48440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r72 r11 null 0x0 0x02890002 {@1,$1}
0x00003161, 0x0b254220, 0x00000000, 0x00000008, ; scene12, mov (1|M0) r11.1<1>:ud 0x8:ud {$1.src}
0x00049231, 0x58440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r88 r11 null 0x0 0x02890002 {@1,$2}
0x00003261, 0x0b054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r11.0<1>:ud 0x0:ud {$2.src}
0x00049331, 0x50440000, 0xc0040b0c, 0x02400000, ; scene12, send.dc1 (16|M0) r80 r11 null 0x0 0x02890002 {@1,$3}
0x00000061, 0x04e54220, 0x00000000, 0x00000001, ; scene12, mov (1|M0) r4.7<1>:ud 0x1:ud
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; scene12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; scene12, (W) sync.allwr null
0x00000061, 0x38054220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r56.0<1>:ud 0x0:ud
0x00000140, 0x10018110, 0x01003804, 0x02c002c0, ; scene12, add (1|M0) a0.0<1>:uw r56.0<0;1,0>:uw 0x2C0:uw {@1}
0x00030161, 0x3a050220, 0x00470001, 0x00000000, ; scene12, mov (8|M0) r58.0<1>:ud r[a0.0]<8;8,1>:ud {@1}
0x00000140, 0x10018110, 0x01003804, 0x06000600, ; scene12, add (1|M0) a0.0<1>:uw r56.0<0;1,0>:uw 0x600:uw {@1}
0x00030161, 0x3b050220, 0x00470001, 0x00000000, ; scene12, mov (8|M0) r59.0<1>:ud r[a0.0]<8;8,1>:ud {@1}
0x00000168, 0x38858220, 0x02003804, 0x00000003, ; scene12, shr (1|M0) r56.4<1>:ud r56.0<0;1,0>:ud 0x3:ud {@1}
0x00000140, 0x10018110, 0x01003884, 0x01400140, ; scene12, add (1|M0) a0.0<1>:uw r56.8<0;1,0>:uw 0x140:uw {@1}
0x00000161, 0x38650220, 0x00010000, 0x00000000, ; scene12, mov (1|M0) r56.3<1>:ud r[a0.0]<0;1,0>:ud {@1}
0x00030168, 0x3c058220, 0x02463b05, 0x00000018, ; scene12, shr (8|M0) r60.0<1>:ud r59.0<8;8,1>:ud 0x18:ud {@1}
0x00030168, 0x3d058220, 0x02463c05, 0x00000007, ; scene12, shr (8|M0) r61.0<1>:ud r60.0<8;8,1>:ud 0x7:ud {@1}
0x00030140, 0x3d050220, 0x02463d05, 0x00463c05, ; scene12, add (8|M0) r61.0<1>:ud r61.0<8;8,1>:ud r60.0<8;8,1>:ud {@1}
0x00030140, 0x3e05a220, 0x02463d05, 0x00000100, ; scene12, add (8|M0) r62.0<1>:ud -r61.0<8;8,1>:ud 0x100:ud {@1}
0x00030061, 0x3f054220, 0x00000000, 0x00000000, ; scene12, mov (8|M0) r63.0<1>:ud 0x0:ud
0x00000061, 0x38254220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r56.1<1>:ud 0x0:ud
0x00030168, 0x60050220, 0x02460e05, 0x00003824, ; scene12, shr (8|M0) r96.0<1>:ud r14.0<8;8,1>:ud r56.1<0;1,0>:ud {@1}
0x00030165, 0x60058220, 0x02466005, 0x000000ff, ; scene12, and (8|M0) r96.0<1>:ud r96.0<8;8,1>:ud 0xFF:ud {@1}
0x00030169, 0x60058220, 0x02466005, 0x00000001, ; scene12, shl (8|M0) r96.0<1>:ud r96.0<8;8,1>:ud 0x1:ud {@1}
0x00030140, 0x10018110, 0x01566006, 0x08000800, ; scene12, add (8|M0) a0.0<1>:uw r96.0<16;8,2>:uw 0x800:uw {@1}
0x00030161, 0x63050120, 0x00f10000, 0x00000000, ; scene12, mov (8|M0) r99.0<1>:ud r[a0.0]<1,0>:uw {@1}
0x00030168, 0x61050220, 0x02463a05, 0x00003824, ; scene12, shr (8|M0) r97.0<1>:ud r58.0<8;8,1>:ud r56.1<0;1,0>:ud {@1}
0x00030165, 0x61058220, 0x02466105, 0x000000ff, ; scene12, and (8|M0) r97.0<1>:ud r97.0<8;8,1>:ud 0xFF:ud {@1}
0x00030169, 0x61058220, 0x02466105, 0x00000001, ; scene12, shl (8|M0) r97.0<1>:ud r97.0<8;8,1>:ud 0x1:ud {@1}
0x00030140, 0x10018110, 0x01566106, 0x08000800, ; scene12, add (8|M0) a0.0<1>:uw r97.0<16;8,2>:uw 0x800:uw {@1}
0x00030161, 0x64050120, 0x00f10000, 0x00000000, ; scene12, mov (8|M0) r100.0<1>:ud r[a0.0]<1,0>:uw {@1}
0x00030141, 0x63050220, 0x01466305, 0x00563e06, ; scene12, mul (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud r62.0<16;8,2>:uw {@1}
0x00030241, 0x64050220, 0x01466405, 0x00563d06, ; scene12, mul (8|M0) r100.0<1>:ud r100.0<8;8,1>:ud r61.0<16;8,2>:uw {@2}
0x00030140, 0x63050220, 0x02466305, 0x00466405, ; scene12, add (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud r100.0<8;8,1>:ud {@1}
0x00030140, 0x63058220, 0x02466305, 0x00000080, ; scene12, add (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud 0x80:ud {@1}
0x00030168, 0x63058220, 0x02466305, 0x00000008, ; scene12, shr (8|M0) r99.0<1>:ud r99.0<8;8,1>:ud 0x8:ud {@1}
0x00030061, 0x67054220, 0x00000000, 0x00000000, ; scene12, mov (8|M0) r103.0<1>:ud 0x0:ud
0x00000061, 0x38454220, 0x00000000, 0x00000080, ; scene12, mov (1|M0) r56.2<1>:ud 0x80:ud
0x00030140, 0x68050220, 0x02466705, 0x00003844, ; scene12, add (8|M0) r104.0<1>:ud r103.0<8;8,1>:ud r56.2<0;1,0>:ud {@1}
0x00030169, 0x69058220, 0x02466805, 0x00000001, ; scene12, shl (8|M0) r105.0<1>:ud r104.0<8;8,1>:ud 0x1:ud {@1}
0x00030140, 0x10018110, 0x01566906, 0x0a000a00, ; scene12, add (8|M0) a0.0<1>:uw r105.0<16;8,2>:uw 0xA00:uw {@1}
0x00030161, 0x6a050120, 0x00f10000, 0x00000000, ; scene12, mov (8|M0) r106.0<1>:ud r[a0.0]<1,0>:uw {@1}
0x00030170, 0x00010220, 0x62466a05, 0x00466305, ; scene12, cmp (8|M0) (le)f0.0 null<1>:ud r106.0<8;8,1>:ud r99.0<8;8,1>:ud {@1}
0x01030061, 0x67050220, 0x00466805, 0x00000000, ; scene12, (f0.0) mov (8|M0) r103.0<1>:ud r104.0<8;8,1>:ud
0x00000068, 0x38458220, 0x02003844, 0x00000001, ; scene12, shr (1|M0) r56.2<1>:ud r56.2<0;1,0>:ud 0x1:ud
0x00440170, 0x00018220, 0x22003844, 0x00000000, ; scene12, cmp (16|M0) (ne)f0.1 null<1>:ud r56.2<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0xffffff80, ; scene12, (W&f0.1) jmpi L7008 {@1}
0x00030469, 0x67050220, 0x02466705, 0x00003824, ; scene12, shl (8|M0) r103.0<1>:ud r103.0<8;8,1>:ud r56.1<0;1,0>:ud {@4}
0x00030166, 0x3f050220, 0x02463f05, 0x00466705, ; scene12, or (8|M0) r63.0<1>:ud r63.0<8;8,1>:ud r103.0<8;8,1>:ud {@1}
0x00000040, 0x38258220, 0x02003824, 0x00000008, ; scene12, add (1|M0) r56.1<1>:ud r56.1<0;1,0>:ud 0x8:ud
0x00440170, 0x00018220, 0x52003824, 0x00000018, ; scene12, cmp (16|M0) (lt)f0.1 null<1>:ud r56.1<0;1,0>:ud 0x18:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0xfffffe20, ; scene12, (W&f0.1) jmpi L6736 {@1}
0x00030170, 0x00018220, 0x12463c05, 0x00000000, ; scene12, cmp (8|M0) (eq)f0.0 null<1>:ud r60.0<8;8,1>:ud 0x0:ud {@1}
0x01030061, 0x3f050220, 0x00460e05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r63.0<1>:ud r14.0<8;8,1>:ud
0x00030070, 0x00018220, 0x42463c05, 0x000000ff, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:ud r60.0<8;8,1>:ud 0xFF:ud
0x01030061, 0x3f050220, 0x00463a05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r63.0<1>:ud r58.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00003864, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r56.3<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x0e050220, 0x00463f05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r14.0<1>:ud r63.0<8;8,1>:ud {@3}
0x00030161, 0x39050220, 0x00460e05, 0x00000000, ; scene12, mov (8|M0) r57.0<1>:ud r14.0<8;8,1>:ud {@1}
0x00030061, 0x0e050220, 0x00460f05, 0x00000000, ; scene12, mov (8|M0) r14.0<1>:ud r15.0<8;8,1>:ud
0x00030061, 0x0f050220, 0x00461005, 0x00000000, ; scene12, mov (8|M0) r15.0<1>:ud r16.0<8;8,1>:ud
0x00030061, 0x10050220, 0x00461105, 0x00000000, ; scene12, mov (8|M0) r16.0<1>:ud r17.0<8;8,1>:ud
0x00030061, 0x11050220, 0x00461205, 0x00000000, ; scene12, mov (8|M0) r17.0<1>:ud r18.0<8;8,1>:ud
0x00030061, 0x12050220, 0x00461305, 0x00000000, ; scene12, mov (8|M0) r18.0<1>:ud r19.0<8;8,1>:ud
0x00030061, 0x13050220, 0x00461405, 0x00000000, ; scene12, mov (8|M0) r19.0<1>:ud r20.0<8;8,1>:ud
0x00030061, 0x14050220, 0x00461505, 0x00000000, ; scene12, mov (8|M0) r20.0<1>:ud r21.0<8;8,1>:ud
0x00030761, 0x15050220, 0x00463905, 0x00000000, ; scene12, mov (8|M0) r21.0<1>:ud r57.0<8;8,1>:ud {@7}
0x00000040, 0x38058220, 0x02003804, 0x00000020, ; scene12, add (1|M0) r56.0<1>:ud r56.0<0;1,0>:ud 0x20:ud
0x00440170, 0x00018220, 0x52003804, 0x00000100, ; scene12, cmp (16|M0) (lt)f0.1 null<1>:ud r56.0<0;1,0>:ud 0x100:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0xfffffc20, ; scene12, (W&f0.1) jmpi L6528 {@1}
0x00000061, 0x04a54220, 0x00000000, 0x00000000, ; scene12, mov (1|M0) r4.5<1>:ud 0x0:ud
0x80000020, 0x00004000, 0x00000000, 0x00001470, ; scene12, (W) jmpi L12784
0x00030061, 0x16050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r22.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x17050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r23.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x18050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r24.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x19050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r25.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1a050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r26.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1b050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r27.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1c050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r28.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1d050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r29.0<1>:ud r39.5<0;1,0>:ud
0x80000020, 0x00004000, 0x00000000, 0x000000d0, ; scene12, (W) jmpi L7904
0x00000068, 0x08658220, 0x020027a4, 0x00000018, ; scene12, shr (1|M0) r8.3<1>:ud r39.5<0;1,0>:ud 0x18:ud
0x00440170, 0x00018220, 0x12000864, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r8.3<0;1,0>:ud 0x0:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0x000013b0, ; scene12, (W&f0.1) jmpi L12784 {@1}
0x00030061, 0x16050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r22.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x17050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r23.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x18050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r24.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x19050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r25.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1a050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r26.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1b050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r27.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1c050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r28.0<1>:ud r39.5<0;1,0>:ud
0x00030061, 0x1d050220, 0x000027a4, 0x00000000, ; scene12, mov (8|M0) r29.0<1>:ud r39.5<0;1,0>:ud
0x80000020, 0x00004000, 0x00000000, 0x000001a0, ; scene12, (W) jmpi L8304
0x00030066, 0x26050220, 0x02460905, 0x00000a04, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.0<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x0e050220, 0x00461605, 0x00000000, ; scene12, (f0.0) mov (8|M0) r14.0<1>:ud r22.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000a24, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.1<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x0f050220, 0x00461705, 0x00000000, ; scene12, (f0.0) mov (8|M0) r15.0<1>:ud r23.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000a44, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.2<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x10050220, 0x00461805, 0x00000000, ; scene12, (f0.0) mov (8|M0) r16.0<1>:ud r24.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000a64, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.3<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x11050220, 0x00461905, 0x00000000, ; scene12, (f0.0) mov (8|M0) r17.0<1>:ud r25.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000a84, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.4<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x12050220, 0x00461a05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r18.0<1>:ud r26.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000aa4, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.5<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x13050220, 0x00461b05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r19.0<1>:ud r27.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000ac4, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.6<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x14050220, 0x00461c05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r20.0<1>:ud r28.0<8;8,1>:ud
0x00030066, 0x26050220, 0x02460905, 0x00000ae4, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.7<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030061, 0x15050220, 0x00461d05, 0x00000000, ; scene12, (f0.0) mov (8|M0) r21.0<1>:ud r29.0<8;8,1>:ud
0x80000020, 0x00004000, 0x00000000, 0x00001190, ; scene12, (W) jmpi L12784
0x00030068, 0x1e058220, 0x02461605, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r22.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461605, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r22.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461605, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r22.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02460e05, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r14.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02460e05, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r14.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000a04, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.0<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x0e050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r14.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461705, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r23.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461705, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r23.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461705, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r23.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02460f05, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r15.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02460f05, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r15.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000a24, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.1<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x0f050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r15.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461805, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r24.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461805, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r24.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461805, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r24.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02461005, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r16.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02461005, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r16.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000a44, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.2<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x10050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r16.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461905, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r25.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461905, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r25.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461905, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r25.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02461105, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r17.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02461105, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r17.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000a64, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.3<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x11050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r17.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461a05, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r26.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461a05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r26.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461a05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r26.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02461205, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r18.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02461205, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r18.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000a84, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.4<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x12050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r18.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461b05, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r27.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461b05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r27.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461b05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r27.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02461305, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r19.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02461305, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r19.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000aa4, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.5<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x13050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r19.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461c05, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r28.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461c05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r28.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461c05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r28.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02461405, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r20.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02461405, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r20.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000ac4, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.6<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x14050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r20.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00030068, 0x1e058220, 0x02461d05, 0x00000018, ; scene12, shr (8|M0) r30.0<1>:ud r29.0<8;8,1>:ud 0x18:ud
0x00030140, 0x1e05a220, 0x02461e05, 0x000000ff, ; scene12, add (8|M0) r30.0<1>:ud -r30.0<8;8,1>:ud 0xFF:ud {@1}
0x00030065, 0x1f058220, 0x02461d05, 0x00ff00ff, ; scene12, and (8|M0) r31.0<1>:ud r29.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02461d05, 0x00000008, ; scene12, shr (8|M0) r32.0<1>:ud r29.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; scene12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x21058220, 0x02461505, 0x00ff00ff, ; scene12, and (8|M0) r33.0<1>:ud r21.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02461505, 0x00000008, ; scene12, shr (8|M0) r34.0<1>:ud r21.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; scene12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030341, 0x21050220, 0x01462105, 0x00201e04, ; scene12, mul (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x21058220, 0x02462105, 0x007f007f, ; scene12, add (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030341, 0x22050220, 0x01462205, 0x00201e04, ; scene12, mul (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud r30.0<2;1,0>:uw {@3}
0x00030140, 0x22058220, 0x02462205, 0x007f007f, ; scene12, add (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x7F007F:ud {@1}
0x00030368, 0x23058220, 0x02462105, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r33.0<8;8,1>:ud 0x8:ud {@3}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x23050220, 0x02462305, 0x00462105, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r33.0<8;8,1>:ud {@1}
0x00030140, 0x23058220, 0x02462305, 0x00010001, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x23058220, 0x02462305, 0x00000008, ; scene12, shr (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; scene12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030768, 0x24058220, 0x02462205, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@7}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030140, 0x24050220, 0x02462405, 0x00462205, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00030140, 0x24058220, 0x02462405, 0x00010001, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x10001:ud {@1}
0x00030168, 0x24058220, 0x02462405, 0x00000008, ; scene12, shr (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; scene12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030740, 0x23050220, 0x02462305, 0x00461f05, ; scene12, add (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud r31.0<8;8,1>:ud {@7}
0x00030240, 0x24050220, 0x02462405, 0x00462005, ; scene12, add (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud r32.0<8;8,1>:ud {@2}
0x00c30262, 0x23058110, 0x51462305, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.0<1>:uw r35.0<8;8,1>:uw 0xFF:uw {@2}
0x00c30062, 0x23858110, 0x51462385, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r35.8<1>:uw r35.8<8;8,1>:uw 0xFF:uw
0x00c30362, 0x24058110, 0x51462405, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.0<1>:uw r36.0<8;8,1>:uw 0xFF:uw {@3}
0x00c30062, 0x24858110, 0x51462485, 0x00ff00ff, ; scene12, sel (8|M0) (lt)f1.1 r36.8<1>:uw r36.8<8;8,1>:uw 0xFF:uw
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; scene12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x25050220, 0x02462305, 0x00462405, ; scene12, or (8|M0) r37.0<1>:ud r35.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00030066, 0x26050220, 0x02460905, 0x00000ae4, ; scene12, or (8|M0) r38.0<1>:ud r9.0<8;8,1>:ud r10.7<0;1,0>:ud
0x00030170, 0x00018660, 0x46462605, 0x00000000, ; scene12, cmp (8|M0) (ge)f0.0 null<1>:d r38.0<8;8,1>:d 0:d {@1}
0x01030361, 0x15050220, 0x00462505, 0x00000000, ; scene12, (f0.0) mov (8|M0) r21.0<1>:ud r37.0<8;8,1>:ud {@3}
0x00000040, 0x02858220, 0x02000284, 0x00000001, ; scene12, add (1|M0) r2.4<1>:ud r2.4<0;1,0>:ud 0x1:ud
0x80000020, 0x00004000, 0x00000000, 0xffffd3a0, ; scene12, (W) jmpi L1440
0x00440070, 0x00018220, 0x12000484, 0x00000000, ; scene12, cmp (16|M0) (eq)f0.1 null<1>:ud r4.4<0;1,0>:ud 0x0:ud
0x81400120, 0x00004000, 0x00000000, 0x00000040, ; scene12, (W&f0.1) jmpi L12896 {@1}
0x0004d031, 0x00000000, 0xc0000d4c, 0x02a00000, ; scene12, send.dc1 (16|M0) null r13 null 0x0 0x120A8000 {@5,$0}
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; scene12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; scene12, (W) sync.allwr null
0x00000040, 0x02058220, 0x02000204, 0x00000008, ; scene12, add (1|M0) r2.0<1>:ud r2.0<0;1,0>:ud 0x8:ud
0x00000040, 0x0d058220, 0x02000d04, 0x00000020, ; scene12, add (1|M0) r13.0<1>:ud r13.0<0;1,0>:ud 0x20:ud
0x00440270, 0x00010220, 0x52000204, 0x00000404, ; scene12, cmp (16|M0) (lt)f0.1 null<1>:ud r2.0<0;1,0>:ud r4.0<0;1,0>:ud {@2}
0x81400120, 0x00004000, 0x00000000, 0xffffd020, ; scene12, (W&f0.1) jmpi L688 {@1}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; scene12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049031, 0x00000004, 0x7020700c, 0x00000000 ; scene12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$0}

kernel cp12
0x00000061, 0x3c050220, 0x00000104, 0x00000000, ; cp12, mov (1|M0) r60.0<1>:ud r1.0<0;1,0>:ud
0x00000061, 0x3c250220, 0x00000124, 0x00000000, ; cp12, mov (1|M0) r60.1<1>:ud r1.1<0;1,0>:ud
0x00000061, 0x3c450220, 0x00000144, 0x00000000, ; cp12, mov (1|M0) r60.2<1>:ud r1.2<0;1,0>:ud
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; cp12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; cp12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x04050220, 0x00220205, 0x00000000, ; cp12, mov (2|M0) r4.0<1>:ud r2.0<2;2,1>:ud {@2}
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; cp12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00049031, 0x050c0000, 0xc000040c, 0x02400000, ; cp12, send.dc1 (16|M0) r5 r4 null 0x0 0x02190000 {@1,$0}
0x00022061, 0x06050220, 0x00100504, 0x00000000, ; cp12, mov (4|M0) r6.0<1>:ud r5.0<1;1,0>:ud {$0.dst}
0x00000069, 0x3d058220, 0x02000024, 0x00000002, ; cp12, shl (1|M0) r61.0<1>:ud r0.1<0;1,0>:ud 0x2:ud
0x00000068, 0x3d258220, 0x02000024, 0x00000004, ; cp12, shr (1|M0) r61.1<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000068, 0x3d458220, 0x020000c4, 0x00000006, ; cp12, shr (1|M0) r61.2<1>:ud r0.6<0;1,0>:ud 0x6:ud
0x00000141, 0x3d650220, 0x01003d44, 0x00003c24, ; cp12, mul (1|M0) r61.3<1>:ud r61.2<0;1,0>:ud r60.2<0;1,0>:uw {@1}
0x00000140, 0x3d650220, 0x02003d64, 0x00003d24, ; cp12, add (1|M0) r61.3<1>:ud r61.3<0;1,0>:ud r61.1<0;1,0>:ud {@1}
0x00000169, 0x3d858220, 0x02003d64, 0x00000003, ; cp12, shl (1|M0) r61.4<1>:ud r61.3<0;1,0>:ud 0x3:ud {@1}
0x00000165, 0x3e058220, 0x02003d84, 0x00003fff, ; cp12, and (1|M0) r62.0<1>:ud r61.4<0;1,0>:ud 0x3FFF:ud {@1}
0x00000068, 0x3e258220, 0x02003d84, 0x0000000e, ; cp12, shr (1|M0) r62.1<1>:ud r61.4<0;1,0>:ud 0xE:ud
0x00030061, 0x28050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r40.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x28050220, 0x00223e05, 0x00000000, ; cp12, mov (2|M0) r40.0<1>:ud r62.0<2;2,1>:ud {@2}
0x00000061, 0x28454220, 0x00000000, 0x00000007, ; cp12, mov (1|M0) r40.2<1>:ud 0x7:ud
0x00049031, 0x290c0000, 0xc002280c, 0x02400000, ; cp12, send.dc1 (16|M0) r41 r40 null 0x0 0x02190001 {@1,$0}
0x00002061, 0x3f050220, 0x00002904, 0x00000000, ; cp12, mov (1|M0) r63.0<1>:ud r41.0<0;1,0>:ud {$0.dst}
0x00000061, 0x3f250220, 0x00002924, 0x00000000, ; cp12, mov (1|M0) r63.1<1>:ud r41.1<0;1,0>:ud
0x00000269, 0x3f458220, 0x02003f04, 0x00000005, ; cp12, shl (1|M0) r63.2<1>:ud r63.0<0;1,0>:ud 0x5:ud {@2}
0x00000140, 0x3f450220, 0x02003f44, 0x00003c44, ; cp12, add (1|M0) r63.2<1>:ud r63.2<0;1,0>:ud r60.2<0;1,0>:ud {@1}
0x00000061, 0x5c054220, 0x00000000, 0x00000000, ; cp12, mov (1|M0) r92.0<1>:ud 0x0:ud
0x00000061, 0x5c254220, 0x00000000, 0x00000001, ; cp12, mov (1|M0) r92.1<1>:ud 0x1:ud
0x00000061, 0x5c454220, 0x00000000, 0x00000002, ; cp12, mov (1|M0) r92.2<1>:ud 0x2:ud
0x00000061, 0x5c654220, 0x00000000, 0x00000003, ; cp12, mov (1|M0) r92.3<1>:ud 0x3:ud
0x00020140, 0x5b050220, 0x02345c05, 0x00003d04, ; cp12, add (4|M0) r91.0<1>:ud r92.0<4;4,1>:ud r61.0<0;1,0>:ud {@1}
0x00800770, 0x00018220, 0x12003f24, 0x00000000, ; cp12, cmp (1|M0) (eq)f1.0 null<1>:ud r63.1<0;1,0>:ud 0x0:ud {@7}
0x81800120, 0x00004000, 0x00000000, 0x000005e0, ; cp12, (W&f1.0) jmpi L2016 {@1}
0x00000740, 0x40058220, 0x02003f44, 0x00000000, ; cp12, add (1|M0) r64.0<1>:ud r63.2<0;1,0>:ud 0x0:ud {@7}
0x00000165, 0x40258220, 0x02004004, 0x00003fff, ; cp12, and (1|M0) r64.1<1>:ud r64.0<0;1,0>:ud 0x3FFF:ud {@1}
0x00000068, 0x40458220, 0x02004004, 0x0000000e, ; cp12, shr (1|M0) r64.2<1>:ud r64.0<0;1,0>:ud 0xE:ud
0x00030061, 0x2c050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r44.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x2c050220, 0x00224025, 0x00000000, ; cp12, mov (2|M0) r44.0<1>:ud r64.1<2;2,1>:ud {@2}
0x00000061, 0x2c454220, 0x00000000, 0x00000007, ; cp12, mov (1|M0) r44.2<1>:ud 0x7:ud
0x00049031, 0x460c0000, 0xc0022c0c, 0x02400000, ; cp12, send.dc1 (16|M0) r70 r44 null 0x0 0x02190001 {@1,$0}
0x00000040, 0x40058220, 0x02003f44, 0x00000008, ; cp12, add (1|M0) r64.0<1>:ud r63.2<0;1,0>:ud 0x8:ud
0x00000165, 0x40258220, 0x02004004, 0x00003fff, ; cp12, and (1|M0) r64.1<1>:ud r64.0<0;1,0>:ud 0x3FFF:ud {@1}
0x00000068, 0x40458220, 0x02004004, 0x0000000e, ; cp12, shr (1|M0) r64.2<1>:ud r64.0<0;1,0>:ud 0xE:ud
0x00030061, 0x2d050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r45.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x2d050220, 0x00224025, 0x00000000, ; cp12, mov (2|M0) r45.0<1>:ud r64.1<2;2,1>:ud {@2}
0x00000061, 0x2d454220, 0x00000000, 0x00000007, ; cp12, mov (1|M0) r45.2<1>:ud 0x7:ud
0x00049131, 0x470c0000, 0xc0022d0c, 0x02400000, ; cp12, send.dc1 (16|M0) r71 r45 null 0x0 0x02190001 {@1,$1}
0x00000040, 0x40058220, 0x02003f44, 0x00000010, ; cp12, add (1|M0) r64.0<1>:ud r63.2<0;1,0>:ud 0x10:ud
0x00000165, 0x40258220, 0x02004004, 0x00003fff, ; cp12, and (1|M0) r64.1<1>:ud r64.0<0;1,0>:ud 0x3FFF:ud {@1}
0x00000068, 0x40458220, 0x02004004, 0x0000000e, ; cp12, shr (1|M0) r64.2<1>:ud r64.0<0;1,0>:ud 0xE:ud
0x00030061, 0x2e050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r46.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x2e050220, 0x00224025, 0x00000000, ; cp12, mov (2|M0) r46.0<1>:ud r64.1<2;2,1>:ud {@2}
0x00000061, 0x2e454220, 0x00000000, 0x00000007, ; cp12, mov (1|M0) r46.2<1>:ud 0x7:ud
0x00049231, 0x480c0000, 0xc0022e0c, 0x02400000, ; cp12, send.dc1 (16|M0) r72 r46 null 0x0 0x02190001 {@1,$2}
0x00000040, 0x40058220, 0x02003f44, 0x00000018, ; cp12, add (1|M0) r64.0<1>:ud r63.2<0;1,0>:ud 0x18:ud
0x00000165, 0x40258220, 0x02004004, 0x00003fff, ; cp12, and (1|M0) r64.1<1>:ud r64.0<0;1,0>:ud 0x3FFF:ud {@1}
0x00000068, 0x40458220, 0x02004004, 0x0000000e, ; cp12, shr (1|M0) r64.2<1>:ud r64.0<0;1,0>:ud 0xE:ud
0x00030061, 0x2f050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r47.0<1>:ud r0.0<8;8,1>:ud
0x00010261, 0x2f050220, 0x00224025, 0x00000000, ; cp12, mov (2|M0) r47.0<1>:ud r64.1<2;2,1>:ud {@2}
0x00000061, 0x2f454220, 0x00000000, 0x00000007, ; cp12, mov (1|M0) r47.2<1>:ud 0x7:ud
0x00049331, 0x490c0000, 0xc0022f0c, 0x02400000, ; cp12, send.dc1 (16|M0) r73 r47 null 0x0 0x02190001 {@1,$3}
0x00002061, 0x50050220, 0x00004604, 0x00000000, ; cp12, mov (1|M0) r80.0<1>:ud r70.0<0;1,0>:ud {$0.dst}
0x00000061, 0x50250220, 0x00004624, 0x00000000, ; cp12, mov (1|M0) r80.1<1>:ud r70.1<0;1,0>:ud
0x00002161, 0x50450220, 0x00004704, 0x00000000, ; cp12, mov (1|M0) r80.2<1>:ud r71.0<0;1,0>:ud {$1.dst}
0x00000061, 0x50650220, 0x00004724, 0x00000000, ; cp12, mov (1|M0) r80.3<1>:ud r71.1<0;1,0>:ud
0x00002261, 0x50850220, 0x00004804, 0x00000000, ; cp12, mov (1|M0) r80.4<1>:ud r72.0<0;1,0>:ud {$2.dst}
0x00000061, 0x50a50220, 0x00004824, 0x00000000, ; cp12, mov (1|M0) r80.5<1>:ud r72.1<0;1,0>:ud
0x00000168, 0x51058220, 0x020050a4, 0x00000018, ; cp12, shr (1|M0) r81.0<1>:ud r80.5<0;1,0>:ud 0x18:ud {@1}
0x00000140, 0x5405a220, 0x02005104, 0x000000ff, ; cp12, add (1|M0) r84.0<1>:ud -r81.0<0;1,0>:ud 0xFF:ud {@1}
0x00000065, 0x55058220, 0x020050a4, 0x00ff00ff, ; cp12, and (1|M0) r85.0<1>:ud r80.5<0;1,0>:ud 0xFF00FF:ud
0x00000068, 0x56058220, 0x020050a4, 0x00000008, ; cp12, shr (1|M0) r86.0<1>:ud r80.5<0;1,0>:ud 0x8:ud
0x00000165, 0x56058220, 0x02005604, 0x00ff00ff, ; cp12, and (1|M0) r86.0<1>:ud r86.0<0;1,0>:ud 0xFF00FF:ud {@1}
0x00000762, 0x51250660, 0x56005064, 0x00003c04, ; cp12, sel (1|M0) (lt)f0.0 r81.1<1>:d r80.3<0;1,0>:d r60.0<0;1,0>:d {@7}
0x00000162, 0x51250660, 0x46005124, 0x00005024, ; cp12, sel (1|M0) (ge)f0.0 r81.1<1>:d r81.1<0;1,0>:d r80.1<0;1,0>:d {@1}
0x00000140, 0x51450220, 0x02005124, 0x02005024, ; cp12, add (1|M0) r81.2<1>:ud r81.1<0;1,0>:ud -r80.1<0;1,0>:ud {@1}
0x00000040, 0x52050220, 0x020000c4, 0x02005044, ; cp12, add (1|M0) r82.0<1>:ud r0.6<0;1,0>:ud -r80.2<0;1,0>:ud
0x00000062, 0x52250660, 0x46005084, 0x00005044, ; cp12, sel (1|M0) (ge)f0.0 r82.1<1>:d r80.4<0;1,0>:d r80.2<0;1,0>:d
0x00000140, 0x52250220, 0x02005224, 0x02005044, ; cp12, add (1|M0) r82.1<1>:ud r82.1<0;1,0>:ud -r80.2<0;1,0>:ud {@1}
0x00000061, 0x52454220, 0x00000000, 0x00000000, ; cp12, mov (1|M0) r82.2<1>:ud 0x0:ud
0x00000170, 0x00010220, 0x52005204, 0x00005224, ; cp12, cmp (1|M0) (lt)f0.0 null<1>:ud r82.0<0;1,0>:ud r82.1<0;1,0>:ud {@1}
0x01000661, 0x52450220, 0x00005144, 0x00000000, ; cp12, (f0.0) mov (1|M0) r82.2<1>:ud r81.2<0;1,0>:ud {@6}
0x00000070, 0x00018220, 0x12005104, 0x00000000, ; cp12, cmp (1|M0) (eq)f0.0 null<1>:ud r81.0<0;1,0>:ud 0x0:ud
0x00000061, 0x53054220, 0x00000000, 0x00000000, ; cp12, mov (1|M0) r83.0<1>:ud 0x0:ud
0x01000061, 0x53054220, 0x00000000, 0x00000001, ; cp12, (f0.0) mov (1|M0) r83.0<1>:ud 0x1:ud
0x00000165, 0x53050220, 0x02005304, 0x00005004, ; cp12, and (1|M0) r83.0<1>:ud r83.0<0;1,0>:ud r80.0<0;1,0>:ud {@1}
0x00000170, 0x00018220, 0x22005304, 0x00000000, ; cp12, cmp (1|M0) (ne)f0.0 null<1>:ud r83.0<0;1,0>:ud 0x0:ud {@1}
0x01000061, 0x52454220, 0x00000000, 0x00000000, ; cp12, (f0.0) mov (1|M0) r82.2<1>:ud 0x0:ud
0x00020040, 0x5a050220, 0x02345b05, 0x02005024, ; cp12, add (4|M0) r90.0<1>:ud r91.0<4;4,1>:ud -r80.1<0;1,0>:ud
0x00020170, 0x00010220, 0x52345a05, 0x00005244, ; cp12, cmp (4|M0) (lt)f0.0 null<1>:ud r90.0<4;4,1>:ud r82.2<0;1,0>:ud {@1}
0x00800770, 0x00018220, 0x12005004, 0x00000000, ; cp12, cmp (1|M0) (eq)f1.0 null<1>:ud r80.0<0;1,0>:ud 0x0:ud {@7}
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; cp12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; cp12, (W) sync.allwr null
0x81800120, 0x00004000, 0x00000000, 0x000001d0, ; cp12, (W&f1.0) jmpi L1936 {@1}
0x00020065, 0x0a058220, 0x02100504, 0x00ff00ff, ; cp12, and (4|M0) r10.0<1>:ud r5.0<1;1,0>:ud 0xFF00FF:ud
0x00020068, 0x0b058220, 0x02100504, 0x00000008, ; cp12, shr (4|M0) r11.0<1>:ud r5.0<1;1,0>:ud 0x8:ud
0x00020165, 0x0b058220, 0x02100b04, 0x00ff00ff, ; cp12, and (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020341, 0x0a050220, 0x01100a04, 0x00005404, ; cp12, mul (4|M0) r10.0<1>:ud r10.0<1;1,0>:ud r84.0<0;1,0>:uw {@3}
0x00020140, 0x0a058220, 0x02100a04, 0x007f007f, ; cp12, add (4|M0) r10.0<1>:ud r10.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020341, 0x0b050220, 0x01100b04, 0x00005404, ; cp12, mul (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud r84.0<0;1,0>:uw {@3}
0x00020140, 0x0b058220, 0x02100b04, 0x007f007f, ; cp12, add (4|M0) r11.0<1>:ud r11.0<1;1,0>:ud 0x7F007F:ud {@1}
0x00020368, 0x0c058220, 0x02100a04, 0x00000008, ; cp12, shr (4|M0) r12.0<1>:ud r10.0<1;1,0>:ud 0x8:ud {@3}
0x00020165, 0x0c058220, 0x02100c04, 0x00ff00ff, ; cp12, and (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0c050220, 0x02100c04, 0x00100a04, ; cp12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud r10.0<1;1,0>:ud {@1}
0x00020140, 0x0c058220, 0x02100c04, 0x00010001, ; cp12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0c058220, 0x02100c04, 0x00000008, ; cp12, shr (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0c058220, 0x02100c04, 0x00ff00ff, ; cp12, and (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020768, 0x0d058220, 0x02100b04, 0x00000008, ; cp12, shr (4|M0) r13.0<1>:ud r11.0<1;1,0>:ud 0x8:ud {@7}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; cp12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020140, 0x0d050220, 0x02100d04, 0x00100b04, ; cp12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r11.0<1;1,0>:ud {@1}
0x00020140, 0x0d058220, 0x02100d04, 0x00010001, ; cp12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x10001:ud {@1}
0x00020168, 0x0d058220, 0x02100d04, 0x00000008, ; cp12, shr (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020165, 0x0d058220, 0x02100d04, 0x00ff00ff, ; cp12, and (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0xFF00FF:ud {@1}
0x00020740, 0x0c050220, 0x02100c04, 0x00005504, ; cp12, add (4|M0) r12.0<1>:ud r12.0<1;1,0>:ud r85.0<0;1,0>:ud {@7}
0x00020240, 0x0d050220, 0x02100d04, 0x00005604, ; cp12, add (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud r86.0<0;1,0>:ud {@2}
0x00030262, 0x0c058110, 0x51460c05, 0x00ff00ff, ; cp12, sel (8|M0) (lt)f0.0 r12.0<1>:uw r12.0<8;8,1>:uw 0xFF:uw {@2}
0x00030262, 0x0d058110, 0x51460d05, 0x00ff00ff, ; cp12, sel (8|M0) (lt)f0.0 r13.0<1>:uw r13.0<8;8,1>:uw 0xFF:uw {@2}
0x00020169, 0x0d058220, 0x02100d04, 0x00000008, ; cp12, shl (4|M0) r13.0<1>:ud r13.0<1;1,0>:ud 0x8:ud {@1}
0x00020166, 0x0f050220, 0x02100c04, 0x00100d04, ; cp12, or (4|M0) r15.0<1>:ud r12.0<1;1,0>:ud r13.0<1;1,0>:ud {@1}
0x00020070, 0x00010220, 0x52345a05, 0x00005244, ; cp12, cmp (4|M0) (lt)f0.0 null<1>:ud r90.0<4;4,1>:ud r82.2<0;1,0>:ud
0x01020261, 0x05050220, 0x00100f04, 0x00000000, ; cp12, (f0.0) mov (4|M0) r5.0<1>:ud r15.0<1;1,0>:ud {@2}
0x80000020, 0x00004000, 0x00000000, 0x00000020, ; cp12, (W) jmpi L1952
0x01020061, 0x05050220, 0x000050a4, 0x00000000, ; cp12, (f0.0) mov (4|M0) r5.0<1>:ud r80.5<0;1,0>:ud
0x00000040, 0x3f258220, 0x02003f24, 0xffffffff, ; cp12, add (1|M0) r63.1<1>:ud r63.1<0;1,0>:ud 0xFFFFFFFF:ud
0x00000040, 0x3f458220, 0x02003f44, 0x00000020, ; cp12, add (1|M0) r63.2<1>:ud r63.2<0;1,0>:ud 0x20:ud
0x00800270, 0x00018220, 0x22003f24, 0x00000000, ; cp12, cmp (1|M0) (ne)f1.0 null<1>:ud r63.1<0;1,0>:ud 0x0:ud {@2}
0x81800120, 0x00004000, 0x00000000, 0xfffffa40, ; cp12, (W&f1.0) jmpi L528 {@1}
0x0004d031, 0x00000000, 0xc0000414, 0x02a00000, ; cp12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@5,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; cp12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; cp12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel scale12
0x00000069, 0x02058220, 0x02000024, 0x00000004, ; scale12, shl (1|M0) r2.0<1>:ud r0.1<0;1,0>:ud 0x4:ud
0x00000061, 0x02250220, 0x000000c4, 0x00000000, ; scale12, mov (1|M0) r2.1<1>:ud r0.6<0;1,0>:ud
0x00030061, 0x06050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r6.0<1>:ud r0.0<8;8,1>:ud
0x00000061, 0x06454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r6.2<1>:ud 0xF:ud
0x00000465, 0x06058220, 0x02000204, 0x00003fff, ; scale12, and (1|M0) r6.0<1>:ud r2.0<0;1,0>:ud 0x3FFF:ud {@4}
0x00000068, 0x06258220, 0x02000204, 0x0000000e, ; scale12, shr (1|M0) r6.1<1>:ud r2.0<0;1,0>:ud 0xE:ud
0x00049031, 0x050c0000, 0xc004060c, 0x02400000, ; scale12, send.dc1 (16|M0) r5 r6 null 0x0 0x02190002 {@1,$0}
0x00000540, 0x03050220, 0x02000124, 0x00000224, ; scale12, add (1|M0) r3.0<1>:ud r1.1<0;1,0>:ud r2.1<0;1,0>:ud {@5}
0x00000169, 0x03058220, 0x02000304, 0x00000002, ; scale12, shl (1|M0) r3.0<1>:ud r3.0<0;1,0>:ud 0x2:ud {@1}
0x00030061, 0x08050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r8.0<1>:ud r0.0<8;8,1>:ud
0x00000061, 0x08454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r8.2<1>:ud 0xF:ud
0x00000365, 0x08058220, 0x02000304, 0x00003fff, ; scale12, and (1|M0) r8.0<1>:ud r3.0<0;1,0>:ud 0x3FFF:ud {@3}
0x00000068, 0x08258220, 0x02000304, 0x0000000e, ; scale12, shr (1|M0) r8.1<1>:ud r3.0<0;1,0>:ud 0xE:ud
0x00049131, 0x070c0000, 0xc004080c, 0x02400000, ; scale12, send.dc1 (16|M0) r7 r8 null 0x0 0x02190002 {@1,$1}
0x00002141, 0x09050220, 0x01000704, 0x00000104, ; scale12, mul (1|M0) r9.0<1>:ud r7.0<0;1,0>:ud r1.0<0;1,0>:uw {$1.dst}
0x00000068, 0x03258220, 0x02000204, 0x00000002, ; scale12, shr (1|M0) r3.1<1>:ud r2.0<0;1,0>:ud 0x2:ud
0x00000140, 0x03050660, 0x06000124, 0x02000324, ; scale12, add (1|M0) r3.0<1>:d r1.1<0;1,0>:d -r3.1<0;1,0>:d {@1}
0x00000162, 0x03458660, 0x56000304, 0x00000004, ; scale12, sel (1|M0) (lt)f0.0 r3.2<1>:d r3.0<0;1,0>:d 4:d {@1}
0x00000162, 0x03458660, 0x46000344, 0x00000000, ; scale12, sel (1|M0) (ge)f0.0 r3.2<1>:d r3.2<0;1,0>:d 0:d {@1}
0x0000d040, 0x3c050220, 0x02000904, 0x00000504, ; scale12, add (1|M0) r60.0<1>:ud r9.0<0;1,0>:ud r5.0<0;1,0>:ud {@5,$0.dst}
0x00000169, 0x3c058220, 0x02003c04, 0x00000002, ; scale12, shl (1|M0) r60.0<1>:ud r60.0<0;1,0>:ud 0x2:ud {@1}
0x00000370, 0x00018660, 0x66000344, 0x00000000, ; scale12, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 0:d {@3}
0x01000061, 0x3c054220, 0x00000000, 0x00000000, ; scale12, (f0.0) mov (1|M0) r60.0<1>:ud 0x0:ud
0x00030061, 0x1e050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r30.0<1>:ud r0.0<8;8,1>:ud
0x00000265, 0x1e058220, 0x02003c04, 0x00003fff, ; scale12, and (1|M0) r30.0<1>:ud r60.0<0;1,0>:ud 0x3FFF:ud {@2}
0x00000068, 0x1e258220, 0x02003c04, 0x0000000e, ; scale12, shr (1|M0) r30.1<1>:ud r60.0<0;1,0>:ud 0xE:ud
0x00000061, 0x1e454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r30.2<1>:ud 0xF:ud
0x00049031, 0x280c0000, 0xc0021e0c, 0x02400000, ; scale12, send.dc1 (16|M0) r40 r30 null 0x0 0x02190001 {@1,$0}
0x00000040, 0x3d050220, 0x02000904, 0x00000524, ; scale12, add (1|M0) r61.0<1>:ud r9.0<0;1,0>:ud r5.1<0;1,0>:ud
0x00000169, 0x3d058220, 0x02003d04, 0x00000002, ; scale12, shl (1|M0) r61.0<1>:ud r61.0<0;1,0>:ud 0x2:ud {@1}
0x00000070, 0x00018660, 0x66000344, 0x00000001, ; scale12, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 1:d
0x01000061, 0x3d054220, 0x00000000, 0x00000000, ; scale12, (f0.0) mov (1|M0) r61.0<1>:ud 0x0:ud
0x00030061, 0x1f050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r31.0<1>:ud r0.0<8;8,1>:ud
0x00000265, 0x1f058220, 0x02003d04, 0x00003fff, ; scale12, and (1|M0) r31.0<1>:ud r61.0<0;1,0>:ud 0x3FFF:ud {@2}
0x00000068, 0x1f258220, 0x02003d04, 0x0000000e, ; scale12, shr (1|M0) r31.1<1>:ud r61.0<0;1,0>:ud 0xE:ud
0x00000061, 0x1f454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r31.2<1>:ud 0xF:ud
0x00049131, 0x290c0000, 0xc0021f0c, 0x02400000, ; scale12, send.dc1 (16|M0) r41 r31 null 0x0 0x02190001 {@1,$1}
0x00000040, 0x3e050220, 0x02000904, 0x00000544, ; scale12, add (1|M0) r62.0<1>:ud r9.0<0;1,0>:ud r5.2<0;1,0>:ud
0x00000169, 0x3e058220, 0x02003e04, 0x00000002, ; scale12, shl (1|M0) r62.0<1>:ud r62.0<0;1,0>:ud 0x2:ud {@1}
0x00000070, 0x00018660, 0x66000344, 0x00000002, ; scale12, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 2:d
0x01000061, 0x3e054220, 0x00000000, 0x00000000, ; scale12, (f0.0) mov (1|M0) r62.0<1>:ud 0x0:ud
0x00030061, 0x20050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r32.0<1>:ud r0.0<8;8,1>:ud
0x00000265, 0x20058220, 0x02003e04, 0x00003fff, ; scale12, and (1|M0) r32.0<1>:ud r62.0<0;1,0>:ud 0x3FFF:ud {@2}
0x00000068, 0x20258220, 0x02003e04, 0x0000000e, ; scale12, shr (1|M0) r32.1<1>:ud r62.0<0;1,0>:ud 0xE:ud
0x00000061, 0x20454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r32.2<1>:ud 0xF:ud
0x00049231, 0x2a0c0000, 0xc002200c, 0x02400000, ; scale12, send.dc1 (16|M0) r42 r32 null 0x0 0x02190001 {@1,$2}
0x00000040, 0x3f050220, 0x02000904, 0x00000564, ; scale12, add (1|M0) r63.0<1>:ud r9.0<0;1,0>:ud r5.3<0;1,0>:ud
0x00000169, 0x3f058220, 0x02003f04, 0x00000002, ; scale12, shl (1|M0) r63.0<1>:ud r63.0<0;1,0>:ud 0x2:ud {@1}
0x00000070, 0x00018660, 0x66000344, 0x00000003, ; scale12, cmp (1|M0) (le)f0.0 null<1>:d r3.2<0;1,0>:d 3:d
0x01000061, 0x3f054220, 0x00000000, 0x00000000, ; scale12, (f0.0) mov (1|M0) r63.0<1>:ud 0x0:ud
0x00030061, 0x21050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r33.0<1>:ud r0.0<8;8,1>:ud
0x00000265, 0x21058220, 0x02003f04, 0x00003fff, ; scale12, and (1|M0) r33.0<1>:ud r63.0<0;1,0>:ud 0x3FFF:ud {@2}
0x00000068, 0x21258220, 0x02003f04, 0x0000000e, ; scale12, shr (1|M0) r33.1<1>:ud r63.0<0;1,0>:ud 0xE:ud
0x00000061, 0x21454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r33.2<1>:ud 0xF:ud
0x00049331, 0x2b0c0000, 0xc002210c, 0x02400000, ; scale12, send.dc1 (16|M0) r43 r33 null 0x0 0x02190001 {@1,$3}
0x00000041, 0x09450220, 0x01000224, 0x00000144, ; scale12, mul (1|M0) r9.2<1>:ud r2.1<0;1,0>:ud r1.4<0;1,0>:uw
0x00000169, 0x09458220, 0x02000944, 0x00000002, ; scale12, shl (1|M0) r9.2<1>:ud r9.2<0;1,0>:ud 0x2:ud {@1}
0x00000140, 0x09450220, 0x02000944, 0x00000204, ; scale12, add (1|M0) r9.2<1>:ud r9.2<0;1,0>:ud r2.0<0;1,0>:ud {@1}
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00000265, 0x04058220, 0x02000944, 0x00003fff, ; scale12, and (1|M0) r4.0<1>:ud r9.2<0;1,0>:ud 0x3FFF:ud {@2}
0x00000068, 0x04258220, 0x02000944, 0x0000000e, ; scale12, shr (1|M0) r4.1<1>:ud r9.2<0;1,0>:ud 0xE:ud
0x00000061, 0x04454220, 0x00000000, 0x0000000f, ; scale12, mov (1|M0) r4.2<1>:ud 0xF:ud
0x00049431, 0x320c0000, 0xc000040c, 0x02400000, ; scale12, send.dc1 (16|M0) r50 r4 null 0x0 0x02190000 {@1,$4}
0x00022461, 0x05050220, 0x00103204, 0x00000000, ; scale12, mov (4|M0) r5.0<1>:ud r50.0<1;1,0>:ud {$4.dst}
0x00000070, 0x00018660, 0x36000344, 0x00000000, ; scale12, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 0:d
0x01002061, 0x05050220, 0x00002804, 0x00000000, ; scale12, (f0.0) mov (1|M0) r5.0<1>:ud r40.0<0;1,0>:ud {$0.dst}
0x00000070, 0x00018660, 0x36000344, 0x00000001, ; scale12, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 1:d
0x01002161, 0x05250220, 0x00002904, 0x00000000, ; scale12, (f0.0) mov (1|M0) r5.1<1>:ud r41.0<0;1,0>:ud {$1.dst}
0x00000070, 0x00018660, 0x36000344, 0x00000002, ; scale12, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 2:d
0x01002261, 0x05450220, 0x00002a04, 0x00000000, ; scale12, (f0.0) mov (1|M0) r5.2<1>:ud r42.0<0;1,0>:ud {$2.dst}
0x00000070, 0x00018660, 0x36000344, 0x00000003, ; scale12, cmp (1|M0) (gt)f0.0 null<1>:d r3.2<0;1,0>:d 3:d
0x01002361, 0x05650220, 0x00002b04, 0x00000000, ; scale12, (f0.0) mov (1|M0) r5.3<1>:ud r43.0<0;1,0>:ud {$3.dst}
0x00049031, 0x00000000, 0xc0000414, 0x02a00000, ; scale12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@1,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; scale12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; scale12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel halve12
0x00030061, 0x03050220, 0x00460005, 0x00000000, ; halve12, mov (8|M0) r3.0<1>:ud r0.0<8;8,1>:ud
0x00000069, 0x03058220, 0x02000024, 0x00000008, ; halve12, shl (1|M0) r3.0<1>:ud r0.1<0;1,0>:ud 0x8:ud
0x00000069, 0x03258220, 0x020000c4, 0x00000003, ; halve12, shl (1|M0) r3.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000061, 0x03454220, 0x00000000, 0x0007001f, ; halve12, mov (1|M0) r3.2<1>:ud 0x7001F:ud
0x00000061, 0x02454220, 0x00000000, 0x00000000, ; halve12, mov (1|M0) r2.2<1>:ud 0x0:ud
0x0004a031, 0x0a440000, 0xc002030c, 0x02400000, ; halve12, send.dc1 (16|M0) r10 r3 null 0x0 0x02890001 {@2,$0}
0x00032065, 0x14058220, 0x02460a05, 0x00ff00ff, ; halve12, and (8|M0) r20.0<1>:ud r10.0<8;8,1>:ud 0xFF00FF:ud {$0.dst}
0x00030068, 0x1e058220, 0x02460a05, 0x00000008, ; halve12, shr (8|M0) r30.0<1>:ud r10.0<8;8,1>:ud 0x8:ud
0x00030165, 0x1e058220, 0x02461e05, 0x00ff00ff, ; halve12, and (8|M0) r30.0<1>:ud r30.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x15058220, 0x02460b05, 0x00ff00ff, ; halve12, and (8|M0) r21.0<1>:ud r11.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x1f058220, 0x02460b05, 0x00000008, ; halve12, shr (8|M0) r31.0<1>:ud r11.0<8;8,1>:ud 0x8:ud
0x00030165, 0x1f058220, 0x02461f05, 0x00ff00ff, ; halve12, and (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x16058220, 0x02460c05, 0x00ff00ff, ; halve12, and (8|M0) r22.0<1>:ud r12.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x20058220, 0x02460c05, 0x00000008, ; halve12, shr (8|M0) r32.0<1>:ud r12.0<8;8,1>:ud 0x8:ud
0x00030165, 0x20058220, 0x02462005, 0x00ff00ff, ; halve12, and (8|M0) r32.0<1>:ud r32.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x17058220, 0x02460d05, 0x00ff00ff, ; halve12, and (8|M0) r23.0<1>:ud r13.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x21058220, 0x02460d05, 0x00000008, ; halve12, shr (8|M0) r33.0<1>:ud r13.0<8;8,1>:ud 0x8:ud
0x00030165, 0x21058220, 0x02462105, 0x00ff00ff, ; halve12, and (8|M0) r33.0<1>:ud r33.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x18058220, 0x02460e05, 0x00ff00ff, ; halve12, and (8|M0) r24.0<1>:ud r14.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x22058220, 0x02460e05, 0x00000008, ; halve12, shr (8|M0) r34.0<1>:ud r14.0<8;8,1>:ud 0x8:ud
0x00030165, 0x22058220, 0x02462205, 0x00ff00ff, ; halve12, and (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x19058220, 0x02460f05, 0x00ff00ff, ; halve12, and (8|M0) r25.0<1>:ud r15.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x23058220, 0x02460f05, 0x00000008, ; halve12, shr (8|M0) r35.0<1>:ud r15.0<8;8,1>:ud 0x8:ud
0x00030165, 0x23058220, 0x02462305, 0x00ff00ff, ; halve12, and (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x1a058220, 0x02461005, 0x00ff00ff, ; halve12, and (8|M0) r26.0<1>:ud r16.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x24058220, 0x02461005, 0x00000008, ; halve12, shr (8|M0) r36.0<1>:ud r16.0<8;8,1>:ud 0x8:ud
0x00030165, 0x24058220, 0x02462405, 0x00ff00ff, ; halve12, and (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00030065, 0x1b058220, 0x02461105, 0x00ff00ff, ; halve12, and (8|M0) r27.0<1>:ud r17.0<8;8,1>:ud 0xFF00FF:ud
0x00030068, 0x25058220, 0x02461105, 0x00000008, ; halve12, shr (8|M0) r37.0<1>:ud r17.0<8;8,1>:ud 0x8:ud
0x00030165, 0x25058220, 0x02462505, 0x00ff00ff, ; halve12, and (8|M0) r37.0<1>:ud r37.0<8;8,1>:ud 0xFF00FF:ud {@1}
0x00040042, 0x28050110, 0x01581405, 0x00581505, ; halve12, avg (16|M0) r40.0<1>:uw r20.0<16;16,1>:uw r21.0<16;16,1>:uw
0x00020140, 0x30050220, 0x02202804, 0x00202824, ; halve12, add (4|M0) r48.0<1>:ud r40.0<2;1,0>:ud r40.1<2;1,0>:ud {@1}
0x00030168, 0x30058110, 0x01463005, 0x00010001, ; halve12, shr (8|M0) r48.0<1>:uw r48.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x29050110, 0x01581605, 0x00581705, ; halve12, avg (16|M0) r41.0<1>:uw r22.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00020140, 0x31050220, 0x02202904, 0x00202924, ; halve12, add (4|M0) r49.0<1>:ud r41.0<2;1,0>:ud r41.1<2;1,0>:ud {@1}
0x00030168, 0x31058110, 0x01463105, 0x00010001, ; halve12, shr (8|M0) r49.0<1>:uw r49.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x2a050110, 0x01581805, 0x00581905, ; halve12, avg (16|M0) r42.0<1>:uw r24.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00020140, 0x32050220, 0x02202a04, 0x00202a24, ; halve12, add (4|M0) r50.0<1>:ud r42.0<2;1,0>:ud r42.1<2;1,0>:ud {@1}
0x00030168, 0x32058110, 0x01463205, 0x00010001, ; halve12, shr (8|M0) r50.0<1>:uw r50.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x2b050110, 0x01581a05, 0x00581b05, ; halve12, avg (16|M0) r43.0<1>:uw r26.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00020140, 0x33050220, 0x02202b04, 0x00202b24, ; halve12, add (4|M0) r51.0<1>:ud r43.0<2;1,0>:ud r43.1<2;1,0>:ud {@1}
0x00030168, 0x33058110, 0x01463305, 0x00010001, ; halve12, shr (8|M0) r51.0<1>:uw r51.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x2c050110, 0x01581e05, 0x00581f05, ; halve12, avg (16|M0) r44.0<1>:uw r30.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00020140, 0x34050220, 0x02202c04, 0x00202c24, ; halve12, add (4|M0) r52.0<1>:ud r44.0<2;1,0>:ud r44.1<2;1,0>:ud {@1}
0x00030168, 0x34058110, 0x01463405, 0x00010001, ; halve12, shr (8|M0) r52.0<1>:uw r52.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x2d050110, 0x01582005, 0x00582105, ; halve12, avg (16|M0) r45.0<1>:uw r32.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00020140, 0x35050220, 0x02202d04, 0x00202d24, ; halve12, add (4|M0) r53.0<1>:ud r45.0<2;1,0>:ud r45.1<2;1,0>:ud {@1}
0x00030168, 0x35058110, 0x01463505, 0x00010001, ; halve12, shr (8|M0) r53.0<1>:uw r53.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x2e050110, 0x01582205, 0x00582305, ; halve12, avg (16|M0) r46.0<1>:uw r34.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00020140, 0x36050220, 0x02202e04, 0x00202e24, ; halve12, add (4|M0) r54.0<1>:ud r46.0<2;1,0>:ud r46.1<2;1,0>:ud {@1}
0x00030168, 0x36058110, 0x01463605, 0x00010001, ; halve12, shr (8|M0) r54.0<1>:uw r54.0<8;8,1>:uw 0x1:uw {@1}
0x00040042, 0x2f050110, 0x01582405, 0x00582505, ; halve12, avg (16|M0) r47.0<1>:uw r36.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00020140, 0x37050220, 0x02202f04, 0x00202f24, ; halve12, add (4|M0) r55.0<1>:ud r47.0<2;1,0>:ud r47.1<2;1,0>:ud {@1}
0x00030168, 0x37058110, 0x01463705, 0x00010001, ; halve12, shr (8|M0) r55.0<1>:uw r55.0<8;8,1>:uw 0x1:uw {@1}
0x00030042, 0x38050110, 0x01463005, 0x00463105, ; halve12, avg (8|M0) r56.0<1>:uw r48.0<8;8,1>:uw r49.0<8;8,1>:uw
0x00010140, 0x3c050220, 0x02203804, 0x00203824, ; halve12, add (2|M0) r60.0<1>:ud r56.0<2;1,0>:ud r56.1<2;1,0>:ud {@1}
0x00020168, 0x3c058110, 0x01343c05, 0x00010001, ; halve12, shr (4|M0) r60.0<1>:uw r60.0<4;4,1>:uw 0x1:uw {@1}
0x00030042, 0x39050110, 0x01463205, 0x00463305, ; halve12, avg (8|M0) r57.0<1>:uw r50.0<8;8,1>:uw r51.0<8;8,1>:uw
0x00010140, 0x3d050220, 0x02203904, 0x00203924, ; halve12, add (2|M0) r61.0<1>:ud r57.0<2;1,0>:ud r57.1<2;1,0>:ud {@1}
0x00020168, 0x3d058110, 0x01343d05, 0x00010001, ; halve12, shr (4|M0) r61.0<1>:uw r61.0<4;4,1>:uw 0x1:uw {@1}
0x00030042, 0x3a050110, 0x01463405, 0x00463505, ; halve12, avg (8|M0) r58.0<1>:uw r52.0<8;8,1>:uw r53.0<8;8,1>:uw
0x00010140, 0x3e050220, 0x02203a04, 0x00203a24, ; halve12, add (2|M0) r62.0<1>:ud r58.0<2;1,0>:ud r58.1<2;1,0>:ud {@1}
0x00020168, 0x3e058110, 0x01343e05, 0x00010001, ; halve12, shr (4|M0) r62.0<1>:uw r62.0<4;4,1>:uw 0x1:uw {@1}
0x00030742, 0x3b050110, 0x01463605, 0x00463705, ; halve12, avg (8|M0) r59.0<1>:uw r54.0<8;8,1>:uw r55.0<8;8,1>:uw {@7}
0x00010140, 0x3f050220, 0x02203b04, 0x00203b24, ; halve12, add (2|M0) r63.0<1>:ud r59.0<2;1,0>:ud r59.1<2;1,0>:ud {@1}
0x00020168, 0x3f058110, 0x01343f05, 0x00010001, ; halve12, shr (4|M0) r63.0<1>:uw r63.0<4;4,1>:uw 0x1:uw {@1}
0x00020742, 0x40050110, 0x01343c05, 0x00343d05, ; halve12, avg (4|M0) r64.0<1>:uw r60.0<4;4,1>:uw r61.0<4;4,1>:uw {@7}
0x00000140, 0x42050220, 0x02004004, 0x00004024, ; halve12, add (1|M0) r66.0<1>:ud r64.0<0;1,0>:ud r64.1<0;1,0>:ud {@1}
0x00010168, 0x42058110, 0x01224205, 0x00010001, ; halve12, shr (2|M0) r66.0<1>:uw r66.0<2;2,1>:uw 0x1:uw {@1}
0x00020442, 0x41050110, 0x01343e05, 0x00343f05, ; halve12, avg (4|M0) r65.0<1>:uw r62.0<4;4,1>:uw r63.0<4;4,1>:uw {@4}
0x00000140, 0x43050220, 0x02004104, 0x00004124, ; halve12, add (1|M0) r67.0<1>:ud r65.0<0;1,0>:ud r65.1<0;1,0>:ud {@1}
0x00010168, 0x43058110, 0x01224305, 0x00010001, ; halve12, shr (2|M0) r67.0<1>:uw r67.0<2;2,1>:uw 0x1:uw {@1}
0x00000169, 0x43058220, 0x02004304, 0x00000008, ; halve12, shl (1|M0) r67.0<1>:ud r67.0<0;1,0>:ud 0x8:ud {@1}
0x00020061, 0x06050220, 0x00340525, 0x00000000, ; halve12, mov (4|M0) r6.0<1>:ud r5.1<4;4,1>:ud
0x00010061, 0x06850220, 0x002205a5, 0x00000000, ; halve12, mov (2|M0) r6.4<1>:ud r5.5<2;2,1>:ud
0x00000061, 0x06c50220, 0x000005e4, 0x00000000, ; halve12, mov (1|M0) r6.6<1>:ud r5.7<0;1,0>:ud
0x00000466, 0x06e50220, 0x02004204, 0x00004304, ; halve12, or (1|M0) r6.7<1>:ud r66.0<0;1,0>:ud r67.0<0;1,0>:ud {@4}
0x00030161, 0x05050220, 0x00460605, 0x00000000, ; halve12, mov (8|M0) r5.0<1>:ud r6.0<8;8,1>:ud {@1}
0x00000040, 0x03058220, 0x02000304, 0x00000020, ; halve12, add (1|M0) r3.0<1>:ud r3.0<0;1,0>:ud 0x20:ud
0x00000040, 0x02458220, 0x02000244, 0x00000001, ; halve12, add (1|M0) r2.2<1>:ud r2.2<0;1,0>:ud 0x1:ud
0x00440170, 0x00018220, 0x52000244, 0x00000008, ; halve12, cmp (16|M0) (lt)f0.1 null<1>:ud r2.2<0;1,0>:ud 0x8:ud {@1}
0x81400120, 0x00004000, 0x00000000, 0xfffffb40, ; halve12, (W&f0.1) jmpi L80 {@1}
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; halve12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00000069, 0x04058220, 0x02000024, 0x00000005, ; halve12, shl (1|M0) r4.0<1>:ud r0.1<0;1,0>:ud 0x5:ud
0x00000061, 0x04250220, 0x000000c4, 0x00000000, ; halve12, mov (1|M0) r4.1<1>:ud r0.6<0;1,0>:ud
0x00000061, 0x04454220, 0x00000000, 0x0000001f, ; halve12, mov (1|M0) r4.2<1>:ud 0x1F:ud
0x00049031, 0x00000000, 0xc0000414, 0x02a00000, ; halve12, send.dc1 (16|M0) null r4 null 0x0 0x040A8000 {@1,$0}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; halve12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049131, 0x00000004, 0x7020700c, 0x00000000 ; halve12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$1}

kernel rise12
0x00030061, 0x03050220, 0x00460005, 0x00000000, ; rise12, mov (8|M0) r3.0<1>:ud r0.0<8;8,1>:ud
0x00000041, 0x03058220, 0x01000024, 0x00180018, ; rise12, mul (1|M0) r3.0<1>:ud r0.1<0;1,0>:ud 0x18:uw
0x00000061, 0x03250220, 0x000000c4, 0x00000000, ; rise12, mov (1|M0) r3.1<1>:ud r0.6<0;1,0>:ud
0x00000061, 0x03454220, 0x00000000, 0x0002001f, ; rise12, mov (1|M0) r3.2<1>:ud 0x2001F:ud
0x00049031, 0x0e1c0000, 0xc002030c, 0x02400000, ; rise12, send.dc1 (16|M0) r14 r3 null 0x0 0x02390001 {@1,$0}
0x00000041, 0x02658220, 0x01000024, 0x00060006, ; rise12, mul (1|M0) r2.3<1>:ud r0.1<0;1,0>:ud 0x6:uw
0x00000140, 0x02650660, 0x06000124, 0x02000264, ; rise12, add (1|M0) r2.3<1>:d r1.1<0;1,0>:d -r2.3<0;1,0>:d {@1}
0x00c00162, 0x02658220, 0x52000264, 0x00000006, ; rise12, sel (1|M0) (lt)f1.1 r2.3<1>:ud r2.3<0;1,0>:ud 0x6:ud {@1}
0x00000061, 0x02454220, 0x00000000, 0x00000000, ; rise12, mov (1|M0) r2.2<1>:ud 0x0:ud
0x00000070, 0x00018660, 0x26000104, 0x00000000, ; rise12, cmp (1|M0) (ne)f0.0 null<1>:d r1.0<0;1,0>:d 0:d
0x00000061, 0x02054660, 0x00000000, 0x00000001, ; rise12, mov (1|M0) r2.0<1>:d 1:d
0x01000061, 0x02054660, 0x00000000, 0xffffffff, ; rise12, (f0.0) mov (1|M0) r2.0<1>:d -1:d
0x00020061, 0x78064220, 0x00000000, 0x00000000, ; rise12, mov (4|M0) r120.0<2>:ud 0x0:ud
0x00020061, 0x78264220, 0x00000000, 0x00020002, ; rise12, mov (4|M0) r120.1<2>:ud 0x20002:ud
0x00040141, 0x78050550, 0x05587805, 0x00000204, ; rise12, mul (16|M0) r120.0<1>:w r120.0<16;16,1>:w r2.0<0;1,0>:w {@1}
0x00020061, 0x79064220, 0x00000000, 0x00000000, ; rise12, mov (4|M0) r121.0<2>:ud 0x0:ud
0x00020061, 0x79264220, 0x00000000, 0x00000002, ; rise12, mov (4|M0) r121.1<2>:ud 0x2:ud
0x00040141, 0x79050550, 0x05587905, 0x00000204, ; rise12, mul (16|M0) r121.0<1>:w r121.0<16;16,1>:w r2.0<0;1,0>:w {@1}
0x00020061, 0x7a064220, 0x00000000, 0x00030003, ; rise12, mov (4|M0) r122.0<2>:ud 0x30003:ud
0x00020061, 0x7a264220, 0x00000000, 0x00010001, ; rise12, mov (4|M0) r122.1<2>:ud 0x10001:ud
0x00040141, 0x7a050550, 0x05587a05, 0x00000204, ; rise12, mul (16|M0) r122.0<1>:w r122.0<16;16,1>:w r2.0<0;1,0>:w {@1}
0x00020061, 0x7b064220, 0x00000000, 0x00000003, ; rise12, mov (4|M0) r123.0<2>:ud 0x3:ud
0x00020061, 0x7b264220, 0x00000000, 0x00000001, ; rise12, mov (4|M0) r123.1<2>:ud 0x1:ud
0x00040141, 0x7b050550, 0x05587b05, 0x00000204, ; rise12, mul (16|M0) r123.0<1>:w r123.0<16;16,1>:w r2.0<0;1,0>:w {@1}
0x00030061, 0x04050220, 0x00460005, 0x00000000, ; rise12, mov (8|M0) r4.0<1>:ud r0.0<8;8,1>:ud
0x00000041, 0x04058220, 0x01000024, 0x00c000c0, ; rise12, mul (1|M0) r4.0<1>:ud r0.1<0;1,0>:ud 0xC0:uw
0x00000069, 0x04258220, 0x020000c4, 0x00000003, ; rise12, shl (1|M0) r4.1<1>:ud r0.6<0;1,0>:ud 0x3:ud
0x00000061, 0x04454220, 0x00000000, 0x0007001f, ; rise12, mov (1|M0) r4.2<1>:ud 0x7001F:ud
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; rise12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; rise12, (W) sync.allwr null
0x00020065, 0x14058220, 0x02340e05, 0x00ff00ff, ; rise12, and (4|M0) r20.0<1>:ud r14.0<4;4,1>:ud 0xFF00FF:ud
0x00020068, 0x15058220, 0x02340e05, 0x00000008, ; rise12, shr (4|M0) r21.0<1>:ud r14.0<4;4,1>:ud 0x8:ud
0x00020165, 0x15058220, 0x02341505, 0x00ff00ff, ; rise12, and (4|M0) r21.0<1>:ud r21.0<4;4,1>:ud 0xFF00FF:ud {@1}
0x00020065, 0x16058220, 0x02340f05, 0x00ff00ff, ; rise12, and (4|M0) r22.0<1>:ud r15.0<4;4,1>:ud 0xFF00FF:ud
0x00020068, 0x17058220, 0x02340f05, 0x00000008, ; rise12, shr (4|M0) r23.0<1>:ud r15.0<4;4,1>:ud 0x8:ud
0x00020165, 0x17058220, 0x02341705, 0x00ff00ff, ; rise12, and (4|M0) r23.0<1>:ud r23.0<4;4,1>:ud 0xFF00FF:ud {@1}
0x00020065, 0x18058220, 0x02341005, 0x00ff00ff, ; rise12, and (4|M0) r24.0<1>:ud r16.0<4;4,1>:ud 0xFF00FF:ud
0x00020068, 0x19058220, 0x02341005, 0x00000008, ; rise12, shr (4|M0) r25.0<1>:ud r16.0<4;4,1>:ud 0x8:ud
0x00020165, 0x19058220, 0x02341905, 0x00ff00ff, ; rise12, and (4|M0) r25.0<1>:ud r25.0<4;4,1>:ud 0xFF00FF:ud {@1}
0x00040642, 0x1a050110, 0x01581405, 0x00581605, ; rise12, avg (16|M0) r26.0<1>:uw r20.0<16;16,1>:uw r22.0<16;16,1>:uw {@6}
0x00040140, 0x1b050110, 0x01581405, 0x00581a05, ; rise12, add (16|M0) r27.0<1>:uw r20.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040168, 0x1b058110, 0x01581b05, 0x00010001, ; rise12, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1c050110, 0x01581605, 0x00581a05, ; rise12, add (16|M0) r28.0<1>:uw r22.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00040742, 0x1a050110, 0x01581505, 0x00581705, ; rise12, avg (16|M0) r26.0<1>:uw r21.0<16;16,1>:uw r23.0<16;16,1>:uw {@7}
0x00040140, 0x1d050110, 0x01581505, 0x00581a05, ; rise12, add (16|M0) r29.0<1>:uw r21.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040168, 0x1d058110, 0x01581d05, 0x00010001, ; rise12, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1e050110, 0x01581705, 0x00581a05, ; rise12, add (16|M0) r30.0<1>:uw r23.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x1a050110, 0x01581605, 0x00581805, ; rise12, avg (16|M0) r26.0<1>:uw r22.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00040140, 0x1f050110, 0x01581605, 0x00581a05, ; rise12, add (16|M0) r31.0<1>:uw r22.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040168, 0x1f058110, 0x01581f05, 0x00010001, ; rise12, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x20050110, 0x01581805, 0x00581a05, ; rise12, add (16|M0) r32.0<1>:uw r24.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00040168, 0x20058110, 0x01582005, 0x00010001, ; rise12, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x1a050110, 0x01581705, 0x00581905, ; rise12, avg (16|M0) r26.0<1>:uw r23.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00040140, 0x21050110, 0x01581705, 0x00581a05, ; rise12, add (16|M0) r33.0<1>:uw r23.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040168, 0x21058110, 0x01582105, 0x00010001, ; rise12, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x22050110, 0x01581905, 0x00581a05, ; rise12, add (16|M0) r34.0<1>:uw r25.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00040168, 0x22058110, 0x01582205, 0x00010001, ; rise12, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw {@1}
0x00020061, 0x19050220, 0x00341b25, 0x00000000, ; rise12, mov (4|M0) r25.0<1>:ud r27.1<4;4,1>:ud
0x00040142, 0x18050110, 0x01581b05, 0x00581905, ; rise12, avg (16|M0) r24.0<1>:uw r27.0<16;16,1>:uw r25.0<16;16,1>:uw {@1}
0x00040140, 0x17050110, 0x01581b05, 0x00581805, ; rise12, add (16|M0) r23.0<1>:uw r27.0<16;16,1>:uw r24.0<16;16,1>:uw {@1}
0x00040168, 0x17058110, 0x01581705, 0x00010001, ; rise12, shr (16|M0) r23.0<1>:uw r23.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x16050110, 0x01581905, 0x00581805, ; rise12, add (16|M0) r22.0<1>:uw r25.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00040168, 0x16058110, 0x01581605, 0x00010001, ; rise12, shr (16|M0) r22.0<1>:uw r22.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x15060220, 0x00341705, 0x00000000, ; rise12, mov (4|M0) r21.0<2>:ud r23.0<4;4,1>:ud {@3}
0x00020261, 0x15260220, 0x00341605, 0x00000000, ; rise12, mov (4|M0) r21.1<2>:ud r22.0<4;4,1>:ud {@2}
0x00020061, 0x16050220, 0x00341d25, 0x00000000, ; rise12, mov (4|M0) r22.0<1>:ud r29.1<4;4,1>:ud
0x00040142, 0x17050110, 0x01581d05, 0x00581605, ; rise12, avg (16|M0) r23.0<1>:uw r29.0<16;16,1>:uw r22.0<16;16,1>:uw {@1}
0x00040140, 0x18050110, 0x01581d05, 0x00581705, ; rise12, add (16|M0) r24.0<1>:uw r29.0<16;16,1>:uw r23.0<16;16,1>:uw {@1}
0x00040168, 0x18058110, 0x01581805, 0x00010001, ; rise12, shr (16|M0) r24.0<1>:uw r24.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x19050110, 0x01581605, 0x00581705, ; rise12, add (16|M0) r25.0<1>:uw r22.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00040168, 0x19058110, 0x01581905, 0x00010001, ; rise12, shr (16|M0) r25.0<1>:uw r25.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x14060220, 0x00341805, 0x00000000, ; rise12, mov (4|M0) r20.0<2>:ud r24.0<4;4,1>:ud {@3}
0x00020261, 0x14260220, 0x00341905, 0x00000000, ; rise12, mov (4|M0) r20.1<2>:ud r25.0<4;4,1>:ud {@2}
0x00020061, 0x1d050220, 0x00341c25, 0x00000000, ; rise12, mov (4|M0) r29.0<1>:ud r28.1<4;4,1>:ud
0x00040142, 0x1b050110, 0x01581c05, 0x00581d05, ; rise12, avg (16|M0) r27.0<1>:uw r28.0<16;16,1>:uw r29.0<16;16,1>:uw {@1}
0x00040140, 0x19050110, 0x01581c05, 0x00581b05, ; rise12, add (16|M0) r25.0<1>:uw r28.0<16;16,1>:uw r27.0<16;16,1>:uw {@1}
0x00040168, 0x19058110, 0x01581905, 0x00010001, ; rise12, shr (16|M0) r25.0<1>:uw r25.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x18050110, 0x01581d05, 0x00581b05, ; rise12, add (16|M0) r24.0<1>:uw r29.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00040168, 0x18058110, 0x01581805, 0x00010001, ; rise12, shr (16|M0) r24.0<1>:uw r24.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x17060220, 0x00341905, 0x00000000, ; rise12, mov (4|M0) r23.0<2>:ud r25.0<4;4,1>:ud {@3}
0x00020261, 0x17260220, 0x00341805, 0x00000000, ; rise12, mov (4|M0) r23.1<2>:ud r24.0<4;4,1>:ud {@2}
0x00020061, 0x18050220, 0x00341e25, 0x00000000, ; rise12, mov (4|M0) r24.0<1>:ud r30.1<4;4,1>:ud
0x00040142, 0x19050110, 0x01581e05, 0x00581805, ; rise12, avg (16|M0) r25.0<1>:uw r30.0<16;16,1>:uw r24.0<16;16,1>:uw {@1}
0x00040140, 0x1b050110, 0x01581e05, 0x00581905, ; rise12, add (16|M0) r27.0<1>:uw r30.0<16;16,1>:uw r25.0<16;16,1>:uw {@1}
0x00040168, 0x1b058110, 0x01581b05, 0x00010001, ; rise12, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1d050110, 0x01581805, 0x00581905, ; rise12, add (16|M0) r29.0<1>:uw r24.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00040168, 0x1d058110, 0x01581d05, 0x00010001, ; rise12, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x16060220, 0x00341b05, 0x00000000, ; rise12, mov (4|M0) r22.0<2>:ud r27.0<4;4,1>:ud {@3}
0x00020261, 0x16260220, 0x00341d05, 0x00000000, ; rise12, mov (4|M0) r22.1<2>:ud r29.0<4;4,1>:ud {@2}
0x00020061, 0x1e050220, 0x00341f25, 0x00000000, ; rise12, mov (4|M0) r30.0<1>:ud r31.1<4;4,1>:ud
0x00040142, 0x1c050110, 0x01581f05, 0x00581e05, ; rise12, avg (16|M0) r28.0<1>:uw r31.0<16;16,1>:uw r30.0<16;16,1>:uw {@1}
0x00040140, 0x1d050110, 0x01581f05, 0x00581c05, ; rise12, add (16|M0) r29.0<1>:uw r31.0<16;16,1>:uw r28.0<16;16,1>:uw {@1}
0x00040168, 0x1d058110, 0x01581d05, 0x00010001, ; rise12, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1b050110, 0x01581e05, 0x00581c05, ; rise12, add (16|M0) r27.0<1>:uw r30.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00040168, 0x1b058110, 0x01581b05, 0x00010001, ; rise12, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x19060220, 0x00341d05, 0x00000000, ; rise12, mov (4|M0) r25.0<2>:ud r29.0<4;4,1>:ud {@3}
0x00020261, 0x19260220, 0x00341b05, 0x00000000, ; rise12, mov (4|M0) r25.1<2>:ud r27.0<4;4,1>:ud {@2}
0x00020061, 0x1b050220, 0x00342125, 0x00000000, ; rise12, mov (4|M0) r27.0<1>:ud r33.1<4;4,1>:ud
0x00040142, 0x1d050110, 0x01582105, 0x00581b05, ; rise12, avg (16|M0) r29.0<1>:uw r33.0<16;16,1>:uw r27.0<16;16,1>:uw {@1}
0x00040140, 0x1c050110, 0x01582105, 0x00581d05, ; rise12, add (16|M0) r28.0<1>:uw r33.0<16;16,1>:uw r29.0<16;16,1>:uw {@1}
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1e050110, 0x01581b05, 0x00581d05, ; rise12, add (16|M0) r30.0<1>:uw r27.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x18060220, 0x00341c05, 0x00000000, ; rise12, mov (4|M0) r24.0<2>:ud r28.0<4;4,1>:ud {@3}
0x00020261, 0x18260220, 0x00341e05, 0x00000000, ; rise12, mov (4|M0) r24.1<2>:ud r30.0<4;4,1>:ud {@2}
0x00020061, 0x21050220, 0x00342025, 0x00000000, ; rise12, mov (4|M0) r33.0<1>:ud r32.1<4;4,1>:ud
0x00040142, 0x1f050110, 0x01582005, 0x00582105, ; rise12, avg (16|M0) r31.0<1>:uw r32.0<16;16,1>:uw r33.0<16;16,1>:uw {@1}
0x00040140, 0x1e050110, 0x01582005, 0x00581f05, ; rise12, add (16|M0) r30.0<1>:uw r32.0<16;16,1>:uw r31.0<16;16,1>:uw {@1}
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1c050110, 0x01582105, 0x00581f05, ; rise12, add (16|M0) r28.0<1>:uw r33.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1d060220, 0x00341e05, 0x00000000, ; rise12, mov (4|M0) r29.0<2>:ud r30.0<4;4,1>:ud {@3}
0x00020261, 0x1d260220, 0x00341c05, 0x00000000, ; rise12, mov (4|M0) r29.1<2>:ud r28.0<4;4,1>:ud {@2}
0x00020061, 0x1c050220, 0x00342225, 0x00000000, ; rise12, mov (4|M0) r28.0<1>:ud r34.1<4;4,1>:ud
0x00040142, 0x1e050110, 0x01582205, 0x00581c05, ; rise12, avg (16|M0) r30.0<1>:uw r34.0<16;16,1>:uw r28.0<16;16,1>:uw {@1}
0x00040140, 0x1f050110, 0x01582205, 0x00581e05, ; rise12, add (16|M0) r31.0<1>:uw r34.0<16;16,1>:uw r30.0<16;16,1>:uw {@1}
0x00040168, 0x1f058110, 0x01581f05, 0x00010001, ; rise12, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x21050110, 0x01581c05, 0x00581e05, ; rise12, add (16|M0) r33.0<1>:uw r28.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00040168, 0x21058110, 0x01582105, 0x00010001, ; rise12, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1b060220, 0x00341f05, 0x00000000, ; rise12, mov (4|M0) r27.0<2>:ud r31.0<4;4,1>:ud {@3}
0x00020261, 0x1b260220, 0x00342105, 0x00000000, ; rise12, mov (4|M0) r27.1<2>:ud r33.0<4;4,1>:ud {@2}
0x00040042, 0x22050110, 0x01581505, 0x00581705, ; rise12, avg (16|M0) r34.0<1>:uw r21.0<16;16,1>:uw r23.0<16;16,1>:uw
0x00040140, 0x20050110, 0x01581505, 0x00582205, ; rise12, add (16|M0) r32.0<1>:uw r21.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x20058110, 0x01582005, 0x00010001, ; rise12, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x21050110, 0x01581705, 0x00582205, ; rise12, add (16|M0) r33.0<1>:uw r23.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x21058110, 0x01582105, 0x00010001, ; rise12, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x22050110, 0x01581405, 0x00581605, ; rise12, avg (16|M0) r34.0<1>:uw r20.0<16;16,1>:uw r22.0<16;16,1>:uw
0x00040140, 0x1f050110, 0x01581405, 0x00582205, ; rise12, add (16|M0) r31.0<1>:uw r20.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x1f058110, 0x01581f05, 0x00010001, ; rise12, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1e050110, 0x01581605, 0x00582205, ; rise12, add (16|M0) r30.0<1>:uw r22.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x22050110, 0x01581705, 0x00581905, ; rise12, avg (16|M0) r34.0<1>:uw r23.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00040140, 0x1c050110, 0x01581705, 0x00582205, ; rise12, add (16|M0) r28.0<1>:uw r23.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1a050110, 0x01581905, 0x00582205, ; rise12, add (16|M0) r26.0<1>:uw r25.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x1a058110, 0x01581a05, 0x00010001, ; rise12, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x22050110, 0x01581605, 0x00581805, ; rise12, avg (16|M0) r34.0<1>:uw r22.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00040140, 0x23050110, 0x01581605, 0x00582205, ; rise12, add (16|M0) r35.0<1>:uw r22.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x23058110, 0x01582305, 0x00010001, ; rise12, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x24050110, 0x01581805, 0x00582205, ; rise12, add (16|M0) r36.0<1>:uw r24.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x24058110, 0x01582405, 0x00010001, ; rise12, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x22050110, 0x01581905, 0x00581d05, ; rise12, avg (16|M0) r34.0<1>:uw r25.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00040140, 0x25050110, 0x01581905, 0x00582205, ; rise12, add (16|M0) r37.0<1>:uw r25.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x25058110, 0x01582505, 0x00010001, ; rise12, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x26050110, 0x01581d05, 0x00582205, ; rise12, add (16|M0) r38.0<1>:uw r29.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x26058110, 0x01582605, 0x00010001, ; rise12, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x22050110, 0x01581805, 0x00581b05, ; rise12, avg (16|M0) r34.0<1>:uw r24.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00040140, 0x27050110, 0x01581805, 0x00582205, ; rise12, add (16|M0) r39.0<1>:uw r24.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x27058110, 0x01582705, 0x00010001, ; rise12, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x28050110, 0x01581b05, 0x00582205, ; rise12, add (16|M0) r40.0<1>:uw r27.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x28058110, 0x01582805, 0x00010001, ; rise12, shr (16|M0) r40.0<1>:uw r40.0<16;16,1>:uw 0x1:uw {@1}
0x00020061, 0x1b050220, 0x00342025, 0x00000000, ; rise12, mov (4|M0) r27.0<1>:ud r32.1<4;4,1>:ud
0x00040142, 0x1d050110, 0x01582005, 0x00581b05, ; rise12, avg (16|M0) r29.0<1>:uw r32.0<16;16,1>:uw r27.0<16;16,1>:uw {@1}
0x00040140, 0x18050110, 0x01582005, 0x00581d05, ; rise12, add (16|M0) r24.0<1>:uw r32.0<16;16,1>:uw r29.0<16;16,1>:uw {@1}
0x00040168, 0x18058110, 0x01581805, 0x00010001, ; rise12, shr (16|M0) r24.0<1>:uw r24.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x19050110, 0x01581b05, 0x00581d05, ; rise12, add (16|M0) r25.0<1>:uw r27.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00040168, 0x19058110, 0x01581905, 0x00010001, ; rise12, shr (16|M0) r25.0<1>:uw r25.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x16060220, 0x00341805, 0x00000000, ; rise12, mov (4|M0) r22.0<2>:ud r24.0<4;4,1>:ud {@3}
0x00020261, 0x16260220, 0x00341905, 0x00000000, ; rise12, mov (4|M0) r22.1<2>:ud r25.0<4;4,1>:ud {@2}
0x00020061, 0x19050220, 0x00341f25, 0x00000000, ; rise12, mov (4|M0) r25.0<1>:ud r31.1<4;4,1>:ud
0x00040142, 0x18050110, 0x01581f05, 0x00581905, ; rise12, avg (16|M0) r24.0<1>:uw r31.0<16;16,1>:uw r25.0<16;16,1>:uw {@1}
0x00040140, 0x1d050110, 0x01581f05, 0x00581805, ; rise12, add (16|M0) r29.0<1>:uw r31.0<16;16,1>:uw r24.0<16;16,1>:uw {@1}
0x00040168, 0x1d058110, 0x01581d05, 0x00010001, ; rise12, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1b050110, 0x01581905, 0x00581805, ; rise12, add (16|M0) r27.0<1>:uw r25.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00040168, 0x1b058110, 0x01581b05, 0x00010001, ; rise12, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x17060220, 0x00341d05, 0x00000000, ; rise12, mov (4|M0) r23.0<2>:ud r29.0<4;4,1>:ud {@3}
0x00020261, 0x17260220, 0x00341b05, 0x00000000, ; rise12, mov (4|M0) r23.1<2>:ud r27.0<4;4,1>:ud {@2}
0x00020061, 0x1f050220, 0x00342125, 0x00000000, ; rise12, mov (4|M0) r31.0<1>:ud r33.1<4;4,1>:ud
0x00040142, 0x20050110, 0x01582105, 0x00581f05, ; rise12, avg (16|M0) r32.0<1>:uw r33.0<16;16,1>:uw r31.0<16;16,1>:uw {@1}
0x00040140, 0x1b050110, 0x01582105, 0x00582005, ; rise12, add (16|M0) r27.0<1>:uw r33.0<16;16,1>:uw r32.0<16;16,1>:uw {@1}
0x00040168, 0x1b058110, 0x01581b05, 0x00010001, ; rise12, shr (16|M0) r27.0<1>:uw r27.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1d050110, 0x01581f05, 0x00582005, ; rise12, add (16|M0) r29.0<1>:uw r31.0<16;16,1>:uw r32.0<16;16,1>:uw
0x00040168, 0x1d058110, 0x01581d05, 0x00010001, ; rise12, shr (16|M0) r29.0<1>:uw r29.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x18060220, 0x00341b05, 0x00000000, ; rise12, mov (4|M0) r24.0<2>:ud r27.0<4;4,1>:ud {@3}
0x00020261, 0x18260220, 0x00341d05, 0x00000000, ; rise12, mov (4|M0) r24.1<2>:ud r29.0<4;4,1>:ud {@2}
0x00020061, 0x1d050220, 0x00341e25, 0x00000000, ; rise12, mov (4|M0) r29.0<1>:ud r30.1<4;4,1>:ud
0x00040142, 0x1b050110, 0x01581e05, 0x00581d05, ; rise12, avg (16|M0) r27.0<1>:uw r30.0<16;16,1>:uw r29.0<16;16,1>:uw {@1}
0x00040140, 0x20050110, 0x01581e05, 0x00581b05, ; rise12, add (16|M0) r32.0<1>:uw r30.0<16;16,1>:uw r27.0<16;16,1>:uw {@1}
0x00040168, 0x20058110, 0x01582005, 0x00010001, ; rise12, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1f050110, 0x01581d05, 0x00581b05, ; rise12, add (16|M0) r31.0<1>:uw r29.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00040168, 0x1f058110, 0x01581f05, 0x00010001, ; rise12, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x19060220, 0x00342005, 0x00000000, ; rise12, mov (4|M0) r25.0<2>:ud r32.0<4;4,1>:ud {@3}
0x00020261, 0x19260220, 0x00341f05, 0x00000000, ; rise12, mov (4|M0) r25.1<2>:ud r31.0<4;4,1>:ud {@2}
0x00020061, 0x1e050220, 0x00341c25, 0x00000000, ; rise12, mov (4|M0) r30.0<1>:ud r28.1<4;4,1>:ud
0x00040142, 0x21050110, 0x01581c05, 0x00581e05, ; rise12, avg (16|M0) r33.0<1>:uw r28.0<16;16,1>:uw r30.0<16;16,1>:uw {@1}
0x00040140, 0x1f050110, 0x01581c05, 0x00582105, ; rise12, add (16|M0) r31.0<1>:uw r28.0<16;16,1>:uw r33.0<16;16,1>:uw {@1}
0x00040168, 0x1f058110, 0x01581f05, 0x00010001, ; rise12, shr (16|M0) r31.0<1>:uw r31.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x20050110, 0x01581e05, 0x00582105, ; rise12, add (16|M0) r32.0<1>:uw r30.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00040168, 0x20058110, 0x01582005, 0x00010001, ; rise12, shr (16|M0) r32.0<1>:uw r32.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1b060220, 0x00341f05, 0x00000000, ; rise12, mov (4|M0) r27.0<2>:ud r31.0<4;4,1>:ud {@3}
0x00020261, 0x1b260220, 0x00342005, 0x00000000, ; rise12, mov (4|M0) r27.1<2>:ud r32.0<4;4,1>:ud {@2}
0x00020061, 0x20050220, 0x00342325, 0x00000000, ; rise12, mov (4|M0) r32.0<1>:ud r35.1<4;4,1>:ud
0x00040142, 0x1f050110, 0x01582305, 0x00582005, ; rise12, avg (16|M0) r31.0<1>:uw r35.0<16;16,1>:uw r32.0<16;16,1>:uw {@1}
0x00040140, 0x21050110, 0x01582305, 0x00581f05, ; rise12, add (16|M0) r33.0<1>:uw r35.0<16;16,1>:uw r31.0<16;16,1>:uw {@1}
0x00040168, 0x21058110, 0x01582105, 0x00010001, ; rise12, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1e050110, 0x01582005, 0x00581f05, ; rise12, add (16|M0) r30.0<1>:uw r32.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1d060220, 0x00342105, 0x00000000, ; rise12, mov (4|M0) r29.0<2>:ud r33.0<4;4,1>:ud {@3}
0x00020261, 0x1d260220, 0x00341e05, 0x00000000, ; rise12, mov (4|M0) r29.1<2>:ud r30.0<4;4,1>:ud {@2}
0x00020061, 0x23050220, 0x00341a25, 0x00000000, ; rise12, mov (4|M0) r35.0<1>:ud r26.1<4;4,1>:ud
0x00040142, 0x1c050110, 0x01581a05, 0x00582305, ; rise12, avg (16|M0) r28.0<1>:uw r26.0<16;16,1>:uw r35.0<16;16,1>:uw {@1}
0x00040140, 0x1e050110, 0x01581a05, 0x00581c05, ; rise12, add (16|M0) r30.0<1>:uw r26.0<16;16,1>:uw r28.0<16;16,1>:uw {@1}
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x21050110, 0x01582305, 0x00581c05, ; rise12, add (16|M0) r33.0<1>:uw r35.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00040168, 0x21058110, 0x01582105, 0x00010001, ; rise12, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1f060220, 0x00341e05, 0x00000000, ; rise12, mov (4|M0) r31.0<2>:ud r30.0<4;4,1>:ud {@3}
0x00020261, 0x1f260220, 0x00342105, 0x00000000, ; rise12, mov (4|M0) r31.1<2>:ud r33.0<4;4,1>:ud {@2}
0x00020061, 0x21050220, 0x00342425, 0x00000000, ; rise12, mov (4|M0) r33.0<1>:ud r36.1<4;4,1>:ud
0x00040142, 0x1e050110, 0x01582405, 0x00582105, ; rise12, avg (16|M0) r30.0<1>:uw r36.0<16;16,1>:uw r33.0<16;16,1>:uw {@1}
0x00040140, 0x1c050110, 0x01582405, 0x00581e05, ; rise12, add (16|M0) r28.0<1>:uw r36.0<16;16,1>:uw r30.0<16;16,1>:uw {@1}
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x23050110, 0x01582105, 0x00581e05, ; rise12, add (16|M0) r35.0<1>:uw r33.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00040168, 0x23058110, 0x01582305, 0x00010001, ; rise12, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x20060220, 0x00341c05, 0x00000000, ; rise12, mov (4|M0) r32.0<2>:ud r28.0<4;4,1>:ud {@3}
0x00020261, 0x20260220, 0x00342305, 0x00000000, ; rise12, mov (4|M0) r32.1<2>:ud r35.0<4;4,1>:ud {@2}
0x00020061, 0x24050220, 0x00342525, 0x00000000, ; rise12, mov (4|M0) r36.0<1>:ud r37.1<4;4,1>:ud
0x00040142, 0x1a050110, 0x01582505, 0x00582405, ; rise12, avg (16|M0) r26.0<1>:uw r37.0<16;16,1>:uw r36.0<16;16,1>:uw {@1}
0x00040140, 0x23050110, 0x01582505, 0x00581a05, ; rise12, add (16|M0) r35.0<1>:uw r37.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040168, 0x23058110, 0x01582305, 0x00010001, ; rise12, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1c050110, 0x01582405, 0x00581a05, ; rise12, add (16|M0) r28.0<1>:uw r36.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1e060220, 0x00342305, 0x00000000, ; rise12, mov (4|M0) r30.0<2>:ud r35.0<4;4,1>:ud {@3}
0x00020261, 0x1e260220, 0x00341c05, 0x00000000, ; rise12, mov (4|M0) r30.1<2>:ud r28.0<4;4,1>:ud {@2}
0x00020061, 0x1c050220, 0x00342725, 0x00000000, ; rise12, mov (4|M0) r28.0<1>:ud r39.1<4;4,1>:ud
0x00040142, 0x23050110, 0x01582705, 0x00581c05, ; rise12, avg (16|M0) r35.0<1>:uw r39.0<16;16,1>:uw r28.0<16;16,1>:uw {@1}
0x00040140, 0x1a050110, 0x01582705, 0x00582305, ; rise12, add (16|M0) r26.0<1>:uw r39.0<16;16,1>:uw r35.0<16;16,1>:uw {@1}
0x00040168, 0x1a058110, 0x01581a05, 0x00010001, ; rise12, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x24050110, 0x01581c05, 0x00582305, ; rise12, add (16|M0) r36.0<1>:uw r28.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00040168, 0x24058110, 0x01582405, 0x00010001, ; rise12, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x21060220, 0x00341a05, 0x00000000, ; rise12, mov (4|M0) r33.0<2>:ud r26.0<4;4,1>:ud {@3}
0x00020261, 0x21260220, 0x00342405, 0x00000000, ; rise12, mov (4|M0) r33.1<2>:ud r36.0<4;4,1>:ud {@2}
0x00020061, 0x27050220, 0x00342625, 0x00000000, ; rise12, mov (4|M0) r39.0<1>:ud r38.1<4;4,1>:ud
0x00040142, 0x25050110, 0x01582605, 0x00582705, ; rise12, avg (16|M0) r37.0<1>:uw r38.0<16;16,1>:uw r39.0<16;16,1>:uw {@1}
0x00040140, 0x24050110, 0x01582605, 0x00582505, ; rise12, add (16|M0) r36.0<1>:uw r38.0<16;16,1>:uw r37.0<16;16,1>:uw {@1}
0x00040168, 0x24058110, 0x01582405, 0x00010001, ; rise12, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1a050110, 0x01582705, 0x00582505, ; rise12, add (16|M0) r26.0<1>:uw r39.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00040168, 0x1a058110, 0x01581a05, 0x00010001, ; rise12, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x23060220, 0x00342405, 0x00000000, ; rise12, mov (4|M0) r35.0<2>:ud r36.0<4;4,1>:ud {@3}
0x00020261, 0x23260220, 0x00341a05, 0x00000000, ; rise12, mov (4|M0) r35.1<2>:ud r26.0<4;4,1>:ud {@2}
0x00020061, 0x1a050220, 0x00342825, 0x00000000, ; rise12, mov (4|M0) r26.0<1>:ud r40.1<4;4,1>:ud
0x00040142, 0x24050110, 0x01582805, 0x00581a05, ; rise12, avg (16|M0) r36.0<1>:uw r40.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040140, 0x25050110, 0x01582805, 0x00582405, ; rise12, add (16|M0) r37.0<1>:uw r40.0<16;16,1>:uw r36.0<16;16,1>:uw {@1}
0x00040168, 0x25058110, 0x01582505, 0x00010001, ; rise12, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x27050110, 0x01581a05, 0x00582405, ; rise12, add (16|M0) r39.0<1>:uw r26.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00040168, 0x27058110, 0x01582705, 0x00010001, ; rise12, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw {@1}
0x00020361, 0x1c060220, 0x00342505, 0x00000000, ; rise12, mov (4|M0) r28.0<2>:ud r37.0<4;4,1>:ud {@3}
0x00020261, 0x1c260220, 0x00342705, 0x00000000, ; rise12, mov (4|M0) r28.1<2>:ud r39.0<4;4,1>:ud {@2}
0x00040042, 0x28050110, 0x01581605, 0x00581805, ; rise12, avg (16|M0) r40.0<1>:uw r22.0<16;16,1>:uw r24.0<16;16,1>:uw
0x00040140, 0x26050110, 0x01581805, 0x00582805, ; rise12, add (16|M0) r38.0<1>:uw r24.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x26058110, 0x01582605, 0x00010001, ; rise12, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581705, 0x00581905, ; rise12, avg (16|M0) r40.0<1>:uw r23.0<16;16,1>:uw r25.0<16;16,1>:uw
0x00040140, 0x27050110, 0x01581905, 0x00582805, ; rise12, add (16|M0) r39.0<1>:uw r25.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x27058110, 0x01582705, 0x00010001, ; rise12, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581805, 0x00581b05, ; rise12, avg (16|M0) r40.0<1>:uw r24.0<16;16,1>:uw r27.0<16;16,1>:uw
0x00040140, 0x25050110, 0x01581805, 0x00582805, ; rise12, add (16|M0) r37.0<1>:uw r24.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x25058110, 0x01582505, 0x00010001, ; rise12, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x24050110, 0x01581b05, 0x00582805, ; rise12, add (16|M0) r36.0<1>:uw r27.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00040168, 0x24058110, 0x01582405, 0x00010001, ; rise12, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581905, 0x00581d05, ; rise12, avg (16|M0) r40.0<1>:uw r25.0<16;16,1>:uw r29.0<16;16,1>:uw
0x00040140, 0x1a050110, 0x01581905, 0x00582805, ; rise12, add (16|M0) r26.0<1>:uw r25.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x1a058110, 0x01581a05, 0x00010001, ; rise12, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x14050110, 0x01581d05, 0x00582805, ; rise12, add (16|M0) r20.0<1>:uw r29.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00040168, 0x14058110, 0x01581405, 0x00010001, ; rise12, shr (16|M0) r20.0<1>:uw r20.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581b05, 0x00581f05, ; rise12, avg (16|M0) r40.0<1>:uw r27.0<16;16,1>:uw r31.0<16;16,1>:uw
0x00040140, 0x15050110, 0x01581b05, 0x00582805, ; rise12, add (16|M0) r21.0<1>:uw r27.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x15058110, 0x01581505, 0x00010001, ; rise12, shr (16|M0) r21.0<1>:uw r21.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x22050110, 0x01581f05, 0x00582805, ; rise12, add (16|M0) r34.0<1>:uw r31.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00040168, 0x22058110, 0x01582205, 0x00010001, ; rise12, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581d05, 0x00582005, ; rise12, avg (16|M0) r40.0<1>:uw r29.0<16;16,1>:uw r32.0<16;16,1>:uw
0x00040140, 0x29050110, 0x01581d05, 0x00582805, ; rise12, add (16|M0) r41.0<1>:uw r29.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x29058110, 0x01582905, 0x00010001, ; rise12, shr (16|M0) r41.0<1>:uw r41.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2a050110, 0x01582005, 0x00582805, ; rise12, add (16|M0) r42.0<1>:uw r32.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00040168, 0x2a058110, 0x01582a05, 0x00010001, ; rise12, shr (16|M0) r42.0<1>:uw r42.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581f05, 0x00581e05, ; rise12, avg (16|M0) r40.0<1>:uw r31.0<16;16,1>:uw r30.0<16;16,1>:uw
0x00040140, 0x2b050110, 0x01581f05, 0x00582805, ; rise12, add (16|M0) r43.0<1>:uw r31.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x2b058110, 0x01582b05, 0x00010001, ; rise12, shr (16|M0) r43.0<1>:uw r43.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2c050110, 0x01581e05, 0x00582805, ; rise12, add (16|M0) r44.0<1>:uw r30.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00040168, 0x2c058110, 0x01582c05, 0x00010001, ; rise12, shr (16|M0) r44.0<1>:uw r44.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01582005, 0x00582105, ; rise12, avg (16|M0) r40.0<1>:uw r32.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00040140, 0x2d050110, 0x01582005, 0x00582805, ; rise12, add (16|M0) r45.0<1>:uw r32.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x2d058110, 0x01582d05, 0x00010001, ; rise12, shr (16|M0) r45.0<1>:uw r45.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2e050110, 0x01582105, 0x00582805, ; rise12, add (16|M0) r46.0<1>:uw r33.0<16;16,1>:uw r40.0<16;16,1>:uw
0x00040168, 0x2e058110, 0x01582e05, 0x00010001, ; rise12, shr (16|M0) r46.0<1>:uw r46.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01581e05, 0x00582305, ; rise12, avg (16|M0) r40.0<1>:uw r30.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00040140, 0x2f050110, 0x01581e05, 0x00582805, ; rise12, add (16|M0) r47.0<1>:uw r30.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x2f058110, 0x01582f05, 0x00010001, ; rise12, shr (16|M0) r47.0<1>:uw r47.0<16;16,1>:uw 0x1:uw {@1}
0x00040042, 0x28050110, 0x01582105, 0x00581c05, ; rise12, avg (16|M0) r40.0<1>:uw r33.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00040140, 0x30050110, 0x01582105, 0x00582805, ; rise12, add (16|M0) r48.0<1>:uw r33.0<16;16,1>:uw r40.0<16;16,1>:uw {@1}
0x00040168, 0x30058110, 0x01583005, 0x00010001, ; rise12, shr (16|M0) r48.0<1>:uw r48.0<16;16,1>:uw 0x1:uw {@1}
0x00020061, 0x1c050220, 0x00342625, 0x00000000, ; rise12, mov (4|M0) r28.0<1>:ud r38.1<4;4,1>:ud
0x00000061, 0x1c850220, 0x000026a4, 0x00000000, ; rise12, mov (1|M0) r28.4<1>:ud r38.5<0;1,0>:ud
0x00040142, 0x23050110, 0x01582605, 0x00581c05, ; rise12, avg (16|M0) r35.0<1>:uw r38.0<16;16,1>:uw r28.0<16;16,1>:uw {@1}
0x00040140, 0x21050110, 0x01582605, 0x00582305, ; rise12, add (16|M0) r33.0<1>:uw r38.0<16;16,1>:uw r35.0<16;16,1>:uw {@1}
0x00040168, 0x21058110, 0x01582105, 0x00010001, ; rise12, shr (16|M0) r33.0<1>:uw r33.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1e050110, 0x01581c05, 0x00582305, ; rise12, add (16|M0) r30.0<1>:uw r28.0<16;16,1>:uw r35.0<16;16,1>:uw
0x00040168, 0x1e058110, 0x01581e05, 0x00010001, ; rise12, shr (16|M0) r30.0<1>:uw r30.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x20060220, 0x00341e05, 0x00000000, ; rise12, mov (4|M0) r32.0<2>:ud r30.0<4;4,1>:ud {@1}
0x00020461, 0x20260220, 0x00342125, 0x00000000, ; rise12, mov (4|M0) r32.1<2>:ud r33.1<4;4,1>:ud {@4}
0x00020061, 0x1e050220, 0x00342725, 0x00000000, ; rise12, mov (4|M0) r30.0<1>:ud r39.1<4;4,1>:ud
0x00000061, 0x1e850220, 0x000027a4, 0x00000000, ; rise12, mov (1|M0) r30.4<1>:ud r39.5<0;1,0>:ud
0x00040142, 0x21050110, 0x01582705, 0x00581e05, ; rise12, avg (16|M0) r33.0<1>:uw r39.0<16;16,1>:uw r30.0<16;16,1>:uw {@1}
0x00040140, 0x23050110, 0x01582705, 0x00582105, ; rise12, add (16|M0) r35.0<1>:uw r39.0<16;16,1>:uw r33.0<16;16,1>:uw {@1}
0x00040168, 0x23058110, 0x01582305, 0x00010001, ; rise12, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1c050110, 0x01581e05, 0x00582105, ; rise12, add (16|M0) r28.0<1>:uw r30.0<16;16,1>:uw r33.0<16;16,1>:uw
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x1f060220, 0x00341c05, 0x00000000, ; rise12, mov (4|M0) r31.0<2>:ud r28.0<4;4,1>:ud {@1}
0x00020461, 0x1f260220, 0x00342325, 0x00000000, ; rise12, mov (4|M0) r31.1<2>:ud r35.1<4;4,1>:ud {@4}
0x00020061, 0x27050220, 0x00342525, 0x00000000, ; rise12, mov (4|M0) r39.0<1>:ud r37.1<4;4,1>:ud
0x00000061, 0x27850220, 0x000025a4, 0x00000000, ; rise12, mov (1|M0) r39.4<1>:ud r37.5<0;1,0>:ud
0x00040142, 0x26050110, 0x01582505, 0x00582705, ; rise12, avg (16|M0) r38.0<1>:uw r37.0<16;16,1>:uw r39.0<16;16,1>:uw {@1}
0x00040140, 0x1c050110, 0x01582505, 0x00582605, ; rise12, add (16|M0) r28.0<1>:uw r37.0<16;16,1>:uw r38.0<16;16,1>:uw {@1}
0x00040168, 0x1c058110, 0x01581c05, 0x00010001, ; rise12, shr (16|M0) r28.0<1>:uw r28.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x23050110, 0x01582705, 0x00582605, ; rise12, add (16|M0) r35.0<1>:uw r39.0<16;16,1>:uw r38.0<16;16,1>:uw
0x00040168, 0x23058110, 0x01582305, 0x00010001, ; rise12, shr (16|M0) r35.0<1>:uw r35.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x21060220, 0x00342305, 0x00000000, ; rise12, mov (4|M0) r33.0<2>:ud r35.0<4;4,1>:ud {@1}
0x00020461, 0x21260220, 0x00341c25, 0x00000000, ; rise12, mov (4|M0) r33.1<2>:ud r28.1<4;4,1>:ud {@4}
0x00020061, 0x23050220, 0x00341a25, 0x00000000, ; rise12, mov (4|M0) r35.0<1>:ud r26.1<4;4,1>:ud
0x00000061, 0x23850220, 0x00001aa4, 0x00000000, ; rise12, mov (1|M0) r35.4<1>:ud r26.5<0;1,0>:ud
0x00040142, 0x1c050110, 0x01581a05, 0x00582305, ; rise12, avg (16|M0) r28.0<1>:uw r26.0<16;16,1>:uw r35.0<16;16,1>:uw {@1}
0x00040140, 0x26050110, 0x01581a05, 0x00581c05, ; rise12, add (16|M0) r38.0<1>:uw r26.0<16;16,1>:uw r28.0<16;16,1>:uw {@1}
0x00040168, 0x26058110, 0x01582605, 0x00010001, ; rise12, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x27050110, 0x01582305, 0x00581c05, ; rise12, add (16|M0) r39.0<1>:uw r35.0<16;16,1>:uw r28.0<16;16,1>:uw
0x00040168, 0x27058110, 0x01582705, 0x00010001, ; rise12, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x1e060220, 0x00342705, 0x00000000, ; rise12, mov (4|M0) r30.0<2>:ud r39.0<4;4,1>:ud {@1}
0x00020461, 0x1e260220, 0x00342625, 0x00000000, ; rise12, mov (4|M0) r30.1<2>:ud r38.1<4;4,1>:ud {@4}
0x00020061, 0x1a050220, 0x00342425, 0x00000000, ; rise12, mov (4|M0) r26.0<1>:ud r36.1<4;4,1>:ud
0x00000061, 0x1a850220, 0x000024a4, 0x00000000, ; rise12, mov (1|M0) r26.4<1>:ud r36.5<0;1,0>:ud
0x00040142, 0x25050110, 0x01582405, 0x00581a05, ; rise12, avg (16|M0) r37.0<1>:uw r36.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040140, 0x27050110, 0x01582405, 0x00582505, ; rise12, add (16|M0) r39.0<1>:uw r36.0<16;16,1>:uw r37.0<16;16,1>:uw {@1}
0x00040168, 0x27058110, 0x01582705, 0x00010001, ; rise12, shr (16|M0) r39.0<1>:uw r39.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x26050110, 0x01581a05, 0x00582505, ; rise12, add (16|M0) r38.0<1>:uw r26.0<16;16,1>:uw r37.0<16;16,1>:uw
0x00040168, 0x26058110, 0x01582605, 0x00010001, ; rise12, shr (16|M0) r38.0<1>:uw r38.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x1c060220, 0x00342605, 0x00000000, ; rise12, mov (4|M0) r28.0<2>:ud r38.0<4;4,1>:ud {@1}
0x00020461, 0x1c260220, 0x00342725, 0x00000000, ; rise12, mov (4|M0) r28.1<2>:ud r39.1<4;4,1>:ud {@4}
0x00020061, 0x26050220, 0x00341425, 0x00000000, ; rise12, mov (4|M0) r38.0<1>:ud r20.1<4;4,1>:ud
0x00000061, 0x26850220, 0x000014a4, 0x00000000, ; rise12, mov (1|M0) r38.4<1>:ud r20.5<0;1,0>:ud
0x00040142, 0x27050110, 0x01581405, 0x00582605, ; rise12, avg (16|M0) r39.0<1>:uw r20.0<16;16,1>:uw r38.0<16;16,1>:uw {@1}
0x00040140, 0x25050110, 0x01581405, 0x00582705, ; rise12, add (16|M0) r37.0<1>:uw r20.0<16;16,1>:uw r39.0<16;16,1>:uw {@1}
0x00040168, 0x25058110, 0x01582505, 0x00010001, ; rise12, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x1a050110, 0x01582605, 0x00582705, ; rise12, add (16|M0) r26.0<1>:uw r38.0<16;16,1>:uw r39.0<16;16,1>:uw
0x00040168, 0x1a058110, 0x01581a05, 0x00010001, ; rise12, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x23060220, 0x00341a05, 0x00000000, ; rise12, mov (4|M0) r35.0<2>:ud r26.0<4;4,1>:ud {@1}
0x00020461, 0x23260220, 0x00342525, 0x00000000, ; rise12, mov (4|M0) r35.1<2>:ud r37.1<4;4,1>:ud {@4}
0x00020061, 0x14050220, 0x00341525, 0x00000000, ; rise12, mov (4|M0) r20.0<1>:ud r21.1<4;4,1>:ud
0x00000061, 0x14850220, 0x000015a4, 0x00000000, ; rise12, mov (1|M0) r20.4<1>:ud r21.5<0;1,0>:ud
0x00040142, 0x24050110, 0x01581505, 0x00581405, ; rise12, avg (16|M0) r36.0<1>:uw r21.0<16;16,1>:uw r20.0<16;16,1>:uw {@1}
0x00040140, 0x1a050110, 0x01581505, 0x00582405, ; rise12, add (16|M0) r26.0<1>:uw r21.0<16;16,1>:uw r36.0<16;16,1>:uw {@1}
0x00040168, 0x1a058110, 0x01581a05, 0x00010001, ; rise12, shr (16|M0) r26.0<1>:uw r26.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x25050110, 0x01581405, 0x00582405, ; rise12, add (16|M0) r37.0<1>:uw r20.0<16;16,1>:uw r36.0<16;16,1>:uw
0x00040168, 0x25058110, 0x01582505, 0x00010001, ; rise12, shr (16|M0) r37.0<1>:uw r37.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x27060220, 0x00342505, 0x00000000, ; rise12, mov (4|M0) r39.0<2>:ud r37.0<4;4,1>:ud {@1}
0x00020461, 0x27260220, 0x00341a25, 0x00000000, ; rise12, mov (4|M0) r39.1<2>:ud r26.1<4;4,1>:ud {@4}
0x00020061, 0x25050220, 0x00342925, 0x00000000, ; rise12, mov (4|M0) r37.0<1>:ud r41.1<4;4,1>:ud
0x00000061, 0x25850220, 0x000029a4, 0x00000000, ; rise12, mov (1|M0) r37.4<1>:ud r41.5<0;1,0>:ud
0x00040142, 0x1a050110, 0x01582905, 0x00582505, ; rise12, avg (16|M0) r26.0<1>:uw r41.0<16;16,1>:uw r37.0<16;16,1>:uw {@1}
0x00040140, 0x24050110, 0x01582905, 0x00581a05, ; rise12, add (16|M0) r36.0<1>:uw r41.0<16;16,1>:uw r26.0<16;16,1>:uw {@1}
0x00040168, 0x24058110, 0x01582405, 0x00010001, ; rise12, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x14050110, 0x01582505, 0x00581a05, ; rise12, add (16|M0) r20.0<1>:uw r37.0<16;16,1>:uw r26.0<16;16,1>:uw
0x00040168, 0x14058110, 0x01581405, 0x00010001, ; rise12, shr (16|M0) r20.0<1>:uw r20.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x26060220, 0x00341405, 0x00000000, ; rise12, mov (4|M0) r38.0<2>:ud r20.0<4;4,1>:ud {@1}
0x00020461, 0x26260220, 0x00342425, 0x00000000, ; rise12, mov (4|M0) r38.1<2>:ud r36.1<4;4,1>:ud {@4}
0x00020061, 0x29050220, 0x00342225, 0x00000000, ; rise12, mov (4|M0) r41.0<1>:ud r34.1<4;4,1>:ud
0x00000061, 0x29850220, 0x000022a4, 0x00000000, ; rise12, mov (1|M0) r41.4<1>:ud r34.5<0;1,0>:ud
0x00040142, 0x15050110, 0x01582205, 0x00582905, ; rise12, avg (16|M0) r21.0<1>:uw r34.0<16;16,1>:uw r41.0<16;16,1>:uw {@1}
0x00040140, 0x14050110, 0x01582205, 0x00581505, ; rise12, add (16|M0) r20.0<1>:uw r34.0<16;16,1>:uw r21.0<16;16,1>:uw {@1}
0x00040168, 0x14058110, 0x01581405, 0x00010001, ; rise12, shr (16|M0) r20.0<1>:uw r20.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x24050110, 0x01582905, 0x00581505, ; rise12, add (16|M0) r36.0<1>:uw r41.0<16;16,1>:uw r21.0<16;16,1>:uw
0x00040168, 0x24058110, 0x01582405, 0x00010001, ; rise12, shr (16|M0) r36.0<1>:uw r36.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x1a060220, 0x00342405, 0x00000000, ; rise12, mov (4|M0) r26.0<2>:ud r36.0<4;4,1>:ud {@1}
0x00020461, 0x1a260220, 0x00341425, 0x00000000, ; rise12, mov (4|M0) r26.1<2>:ud r20.1<4;4,1>:ud {@4}
0x00020061, 0x24050220, 0x00342a25, 0x00000000, ; rise12, mov (4|M0) r36.0<1>:ud r42.1<4;4,1>:ud
0x00000061, 0x24850220, 0x00002aa4, 0x00000000, ; rise12, mov (1|M0) r36.4<1>:ud r42.5<0;1,0>:ud
0x00040142, 0x14050110, 0x01582a05, 0x00582405, ; rise12, avg (16|M0) r20.0<1>:uw r42.0<16;16,1>:uw r36.0<16;16,1>:uw {@1}
0x00040140, 0x15050110, 0x01582a05, 0x00581405, ; rise12, add (16|M0) r21.0<1>:uw r42.0<16;16,1>:uw r20.0<16;16,1>:uw {@1}
0x00040168, 0x15058110, 0x01581505, 0x00010001, ; rise12, shr (16|M0) r21.0<1>:uw r21.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x29050110, 0x01582405, 0x00581405, ; rise12, add (16|M0) r41.0<1>:uw r36.0<16;16,1>:uw r20.0<16;16,1>:uw
0x00040168, 0x29058110, 0x01582905, 0x00010001, ; rise12, shr (16|M0) r41.0<1>:uw r41.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x25060220, 0x00342905, 0x00000000, ; rise12, mov (4|M0) r37.0<2>:ud r41.0<4;4,1>:ud {@1}
0x00020461, 0x25260220, 0x00341525, 0x00000000, ; rise12, mov (4|M0) r37.1<2>:ud r21.1<4;4,1>:ud {@4}
0x00020061, 0x2a050220, 0x00342b25, 0x00000000, ; rise12, mov (4|M0) r42.0<1>:ud r43.1<4;4,1>:ud
0x00000061, 0x2a850220, 0x00002ba4, 0x00000000, ; rise12, mov (1|M0) r42.4<1>:ud r43.5<0;1,0>:ud
0x00040142, 0x22050110, 0x01582b05, 0x00582a05, ; rise12, avg (16|M0) r34.0<1>:uw r43.0<16;16,1>:uw r42.0<16;16,1>:uw {@1}
0x00040140, 0x29050110, 0x01582b05, 0x00582205, ; rise12, add (16|M0) r41.0<1>:uw r43.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040168, 0x29058110, 0x01582905, 0x00010001, ; rise12, shr (16|M0) r41.0<1>:uw r41.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x15050110, 0x01582a05, 0x00582205, ; rise12, add (16|M0) r21.0<1>:uw r42.0<16;16,1>:uw r34.0<16;16,1>:uw
0x00040168, 0x15058110, 0x01581505, 0x00010001, ; rise12, shr (16|M0) r21.0<1>:uw r21.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x14060220, 0x00341505, 0x00000000, ; rise12, mov (4|M0) r20.0<2>:ud r21.0<4;4,1>:ud {@1}
0x00020461, 0x14260220, 0x00342925, 0x00000000, ; rise12, mov (4|M0) r20.1<2>:ud r41.1<4;4,1>:ud {@4}
0x00020061, 0x15050220, 0x00342d25, 0x00000000, ; rise12, mov (4|M0) r21.0<1>:ud r45.1<4;4,1>:ud
0x00000061, 0x15850220, 0x00002da4, 0x00000000, ; rise12, mov (1|M0) r21.4<1>:ud r45.5<0;1,0>:ud
0x00040142, 0x29050110, 0x01582d05, 0x00581505, ; rise12, avg (16|M0) r41.0<1>:uw r45.0<16;16,1>:uw r21.0<16;16,1>:uw {@1}
0x00040140, 0x22050110, 0x01582d05, 0x00582905, ; rise12, add (16|M0) r34.0<1>:uw r45.0<16;16,1>:uw r41.0<16;16,1>:uw {@1}
0x00040168, 0x22058110, 0x01582205, 0x00010001, ; rise12, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2a050110, 0x01581505, 0x00582905, ; rise12, add (16|M0) r42.0<1>:uw r21.0<16;16,1>:uw r41.0<16;16,1>:uw
0x00040168, 0x2a058110, 0x01582a05, 0x00010001, ; rise12, shr (16|M0) r42.0<1>:uw r42.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x24060220, 0x00342a05, 0x00000000, ; rise12, mov (4|M0) r36.0<2>:ud r42.0<4;4,1>:ud {@1}
0x00020461, 0x24260220, 0x00342225, 0x00000000, ; rise12, mov (4|M0) r36.1<2>:ud r34.1<4;4,1>:ud {@4}
0x00020061, 0x2d050220, 0x00342c25, 0x00000000, ; rise12, mov (4|M0) r45.0<1>:ud r44.1<4;4,1>:ud
0x00000061, 0x2d850220, 0x00002ca4, 0x00000000, ; rise12, mov (1|M0) r45.4<1>:ud r44.5<0;1,0>:ud
0x00040142, 0x2b050110, 0x01582c05, 0x00582d05, ; rise12, avg (16|M0) r43.0<1>:uw r44.0<16;16,1>:uw r45.0<16;16,1>:uw {@1}
0x00040140, 0x2a050110, 0x01582c05, 0x00582b05, ; rise12, add (16|M0) r42.0<1>:uw r44.0<16;16,1>:uw r43.0<16;16,1>:uw {@1}
0x00040168, 0x2a058110, 0x01582a05, 0x00010001, ; rise12, shr (16|M0) r42.0<1>:uw r42.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x22050110, 0x01582d05, 0x00582b05, ; rise12, add (16|M0) r34.0<1>:uw r45.0<16;16,1>:uw r43.0<16;16,1>:uw
0x00040168, 0x22058110, 0x01582205, 0x00010001, ; rise12, shr (16|M0) r34.0<1>:uw r34.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x29060220, 0x00342205, 0x00000000, ; rise12, mov (4|M0) r41.0<2>:ud r34.0<4;4,1>:ud {@1}
0x00020461, 0x29260220, 0x00342a25, 0x00000000, ; rise12, mov (4|M0) r41.1<2>:ud r42.1<4;4,1>:ud {@4}
0x00020061, 0x22050220, 0x00342e25, 0x00000000, ; rise12, mov (4|M0) r34.0<1>:ud r46.1<4;4,1>:ud
0x00000061, 0x22850220, 0x00002ea4, 0x00000000, ; rise12, mov (1|M0) r34.4<1>:ud r46.5<0;1,0>:ud
0x00040142, 0x2a050110, 0x01582e05, 0x00582205, ; rise12, avg (16|M0) r42.0<1>:uw r46.0<16;16,1>:uw r34.0<16;16,1>:uw {@1}
0x00040140, 0x2b050110, 0x01582e05, 0x00582a05, ; rise12, add (16|M0) r43.0<1>:uw r46.0<16;16,1>:uw r42.0<16;16,1>:uw {@1}
0x00040168, 0x2b058110, 0x01582b05, 0x00010001, ; rise12, shr (16|M0) r43.0<1>:uw r43.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2d050110, 0x01582205, 0x00582a05, ; rise12, add (16|M0) r45.0<1>:uw r34.0<16;16,1>:uw r42.0<16;16,1>:uw
0x00040168, 0x2d058110, 0x01582d05, 0x00010001, ; rise12, shr (16|M0) r45.0<1>:uw r45.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x15060220, 0x00342d05, 0x00000000, ; rise12, mov (4|M0) r21.0<2>:ud r45.0<4;4,1>:ud {@1}
0x00020461, 0x15260220, 0x00342b25, 0x00000000, ; rise12, mov (4|M0) r21.1<2>:ud r43.1<4;4,1>:ud {@4}
0x00020061, 0x2e050220, 0x00342f25, 0x00000000, ; rise12, mov (4|M0) r46.0<1>:ud r47.1<4;4,1>:ud
0x00000061, 0x2e850220, 0x00002fa4, 0x00000000, ; rise12, mov (1|M0) r46.4<1>:ud r47.5<0;1,0>:ud
0x00040142, 0x2c050110, 0x01582f05, 0x00582e05, ; rise12, avg (16|M0) r44.0<1>:uw r47.0<16;16,1>:uw r46.0<16;16,1>:uw {@1}
0x00040140, 0x2d050110, 0x01582f05, 0x00582c05, ; rise12, add (16|M0) r45.0<1>:uw r47.0<16;16,1>:uw r44.0<16;16,1>:uw {@1}
0x00040168, 0x2d058110, 0x01582d05, 0x00010001, ; rise12, shr (16|M0) r45.0<1>:uw r45.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2b050110, 0x01582e05, 0x00582c05, ; rise12, add (16|M0) r43.0<1>:uw r46.0<16;16,1>:uw r44.0<16;16,1>:uw
0x00040168, 0x2b058110, 0x01582b05, 0x00010001, ; rise12, shr (16|M0) r43.0<1>:uw r43.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x2a060220, 0x00342b05, 0x00000000, ; rise12, mov (4|M0) r42.0<2>:ud r43.0<4;4,1>:ud {@1}
0x00020461, 0x2a260220, 0x00342d25, 0x00000000, ; rise12, mov (4|M0) r42.1<2>:ud r45.1<4;4,1>:ud {@4}
0x00020061, 0x2b050220, 0x00343025, 0x00000000, ; rise12, mov (4|M0) r43.0<1>:ud r48.1<4;4,1>:ud
0x00000061, 0x2b850220, 0x000030a4, 0x00000000, ; rise12, mov (1|M0) r43.4<1>:ud r48.5<0;1,0>:ud
0x00040142, 0x2d050110, 0x01583005, 0x00582b05, ; rise12, avg (16|M0) r45.0<1>:uw r48.0<16;16,1>:uw r43.0<16;16,1>:uw {@1}
0x00040140, 0x2c050110, 0x01583005, 0x00582d05, ; rise12, add (16|M0) r44.0<1>:uw r48.0<16;16,1>:uw r45.0<16;16,1>:uw {@1}
0x00040168, 0x2c058110, 0x01582c05, 0x00010001, ; rise12, shr (16|M0) r44.0<1>:uw r44.0<16;16,1>:uw 0x1:uw {@1}
0x00040040, 0x2e050110, 0x01582b05, 0x00582d05, ; rise12, add (16|M0) r46.0<1>:uw r43.0<16;16,1>:uw r45.0<16;16,1>:uw
0x00040168, 0x2e058110, 0x01582e05, 0x00010001, ; rise12, shr (16|M0) r46.0<1>:uw r46.0<16;16,1>:uw 0x1:uw {@1}
0x00020161, 0x22060220, 0x00342e05, 0x00000000, ; rise12, mov (4|M0) r34.0<2>:ud r46.0<4;4,1>:ud {@1}
0x00020461, 0x22260220, 0x00342c25, 0x00000000, ; rise12, mov (4|M0) r34.1<2>:ud r44.1<4;4,1>:ud {@4}
0x00040040, 0x20050550, 0x05582005, 0x00587805, ; rise12, add (16|M0) r32.0<1>:w r32.0<16;16,1>:w r120.0<16;16,1>:w
0x00040162, 0x20058550, 0x45582005, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r32.0<1>:w r32.0<16;16,1>:w 0:w {@1}
0x00040162, 0x20058550, 0x55582005, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r32.0<1>:w r32.0<16;16,1>:w 255:w {@1}
0x00040040, 0x1f050550, 0x05581f05, 0x00587905, ; rise12, add (16|M0) r31.0<1>:w r31.0<16;16,1>:w r121.0<16;16,1>:w
0x00040162, 0x1f058550, 0x45581f05, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r31.0<1>:w r31.0<16;16,1>:w 0:w {@1}
0x00040162, 0x1f058550, 0x55581f05, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r31.0<1>:w r31.0<16;16,1>:w 255:w {@1}
0x00030169, 0x1f058220, 0x02461f05, 0x00000008, ; rise12, shl (8|M0) r31.0<1>:ud r31.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x05050220, 0x02462005, 0x00461f05, ; rise12, or (8|M0) r5.0<1>:ud r32.0<8;8,1>:ud r31.0<8;8,1>:ud {@1}
0x00040040, 0x21050550, 0x05582105, 0x00587a05, ; rise12, add (16|M0) r33.0<1>:w r33.0<16;16,1>:w r122.0<16;16,1>:w
0x00040162, 0x21058550, 0x45582105, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r33.0<1>:w r33.0<16;16,1>:w 0:w {@1}
0x00040162, 0x21058550, 0x55582105, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r33.0<1>:w r33.0<16;16,1>:w 255:w {@1}
0x00040040, 0x1e050550, 0x05581e05, 0x00587b05, ; rise12, add (16|M0) r30.0<1>:w r30.0<16;16,1>:w r123.0<16;16,1>:w
0x00040162, 0x1e058550, 0x45581e05, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r30.0<1>:w r30.0<16;16,1>:w 0:w {@1}
0x00040162, 0x1e058550, 0x55581e05, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r30.0<1>:w r30.0<16;16,1>:w 255:w {@1}
0x00030169, 0x1e058220, 0x02461e05, 0x00000008, ; rise12, shl (8|M0) r30.0<1>:ud r30.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x06050220, 0x02462105, 0x00461e05, ; rise12, or (8|M0) r6.0<1>:ud r33.0<8;8,1>:ud r30.0<8;8,1>:ud {@1}
0x00040040, 0x1c050550, 0x05581c05, 0x00587805, ; rise12, add (16|M0) r28.0<1>:w r28.0<16;16,1>:w r120.0<16;16,1>:w
0x00040162, 0x1c058550, 0x45581c05, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r28.0<1>:w r28.0<16;16,1>:w 0:w {@1}
0x00040162, 0x1c058550, 0x55581c05, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r28.0<1>:w r28.0<16;16,1>:w 255:w {@1}
0x00040040, 0x23050550, 0x05582305, 0x00587905, ; rise12, add (16|M0) r35.0<1>:w r35.0<16;16,1>:w r121.0<16;16,1>:w
0x00040162, 0x23058550, 0x45582305, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r35.0<1>:w r35.0<16;16,1>:w 0:w {@1}
0x00040162, 0x23058550, 0x55582305, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r35.0<1>:w r35.0<16;16,1>:w 255:w {@1}
0x00030169, 0x23058220, 0x02462305, 0x00000008, ; rise12, shl (8|M0) r35.0<1>:ud r35.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x07050220, 0x02461c05, 0x00462305, ; rise12, or (8|M0) r7.0<1>:ud r28.0<8;8,1>:ud r35.0<8;8,1>:ud {@1}
0x00040040, 0x27050550, 0x05582705, 0x00587a05, ; rise12, add (16|M0) r39.0<1>:w r39.0<16;16,1>:w r122.0<16;16,1>:w
0x00040162, 0x27058550, 0x45582705, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r39.0<1>:w r39.0<16;16,1>:w 0:w {@1}
0x00040162, 0x27058550, 0x55582705, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r39.0<1>:w r39.0<16;16,1>:w 255:w {@1}
0x00040040, 0x26050550, 0x05582605, 0x00587b05, ; rise12, add (16|M0) r38.0<1>:w r38.0<16;16,1>:w r123.0<16;16,1>:w
0x00040162, 0x26058550, 0x45582605, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r38.0<1>:w r38.0<16;16,1>:w 0:w {@1}
0x00040162, 0x26058550, 0x55582605, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r38.0<1>:w r38.0<16;16,1>:w 255:w {@1}
0x00030169, 0x26058220, 0x02462605, 0x00000008, ; rise12, shl (8|M0) r38.0<1>:ud r38.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x08050220, 0x02462705, 0x00462605, ; rise12, or (8|M0) r8.0<1>:ud r39.0<8;8,1>:ud r38.0<8;8,1>:ud {@1}
0x00040040, 0x1a050550, 0x05581a05, 0x00587805, ; rise12, add (16|M0) r26.0<1>:w r26.0<16;16,1>:w r120.0<16;16,1>:w
0x00040162, 0x1a058550, 0x45581a05, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r26.0<1>:w r26.0<16;16,1>:w 0:w {@1}
0x00040162, 0x1a058550, 0x55581a05, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r26.0<1>:w r26.0<16;16,1>:w 255:w {@1}
0x00040040, 0x25050550, 0x05582505, 0x00587905, ; rise12, add (16|M0) r37.0<1>:w r37.0<16;16,1>:w r121.0<16;16,1>:w
0x00040162, 0x25058550, 0x45582505, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r37.0<1>:w r37.0<16;16,1>:w 0:w {@1}
0x00040162, 0x25058550, 0x55582505, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r37.0<1>:w r37.0<16;16,1>:w 255:w {@1}
0x00030169, 0x25058220, 0x02462505, 0x00000008, ; rise12, shl (8|M0) r37.0<1>:ud r37.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x09050220, 0x02461a05, 0x00462505, ; rise12, or (8|M0) r9.0<1>:ud r26.0<8;8,1>:ud r37.0<8;8,1>:ud {@1}
0x00040040, 0x14050550, 0x05581405, 0x00587a05, ; rise12, add (16|M0) r20.0<1>:w r20.0<16;16,1>:w r122.0<16;16,1>:w
0x00040162, 0x14058550, 0x45581405, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r20.0<1>:w r20.0<16;16,1>:w 0:w {@1}
0x00040162, 0x14058550, 0x55581405, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r20.0<1>:w r20.0<16;16,1>:w 255:w {@1}
0x00040040, 0x24050550, 0x05582405, 0x00587b05, ; rise12, add (16|M0) r36.0<1>:w r36.0<16;16,1>:w r123.0<16;16,1>:w
0x00040162, 0x24058550, 0x45582405, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r36.0<1>:w r36.0<16;16,1>:w 0:w {@1}
0x00040162, 0x24058550, 0x55582405, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r36.0<1>:w r36.0<16;16,1>:w 255:w {@1}
0x00030169, 0x24058220, 0x02462405, 0x00000008, ; rise12, shl (8|M0) r36.0<1>:ud r36.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x0a050220, 0x02461405, 0x00462405, ; rise12, or (8|M0) r10.0<1>:ud r20.0<8;8,1>:ud r36.0<8;8,1>:ud {@1}
0x00040040, 0x29050550, 0x05582905, 0x00587805, ; rise12, add (16|M0) r41.0<1>:w r41.0<16;16,1>:w r120.0<16;16,1>:w
0x00040162, 0x29058550, 0x45582905, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r41.0<1>:w r41.0<16;16,1>:w 0:w {@1}
0x00040162, 0x29058550, 0x55582905, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r41.0<1>:w r41.0<16;16,1>:w 255:w {@1}
0x00040040, 0x15050550, 0x05581505, 0x00587905, ; rise12, add (16|M0) r21.0<1>:w r21.0<16;16,1>:w r121.0<16;16,1>:w
0x00040162, 0x15058550, 0x45581505, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r21.0<1>:w r21.0<16;16,1>:w 0:w {@1}
0x00040162, 0x15058550, 0x55581505, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r21.0<1>:w r21.0<16;16,1>:w 255:w {@1}
0x00030169, 0x15058220, 0x02461505, 0x00000008, ; rise12, shl (8|M0) r21.0<1>:ud r21.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x0b050220, 0x02462905, 0x00461505, ; rise12, or (8|M0) r11.0<1>:ud r41.0<8;8,1>:ud r21.0<8;8,1>:ud {@1}
0x00040040, 0x2a050550, 0x05582a05, 0x00587a05, ; rise12, add (16|M0) r42.0<1>:w r42.0<16;16,1>:w r122.0<16;16,1>:w
0x00040162, 0x2a058550, 0x45582a05, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r42.0<1>:w r42.0<16;16,1>:w 0:w {@1}
0x00040162, 0x2a058550, 0x55582a05, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r42.0<1>:w r42.0<16;16,1>:w 255:w {@1}
0x00040040, 0x22050550, 0x05582205, 0x00587b05, ; rise12, add (16|M0) r34.0<1>:w r34.0<16;16,1>:w r123.0<16;16,1>:w
0x00040162, 0x22058550, 0x45582205, 0x00000000, ; rise12, sel (16|M0) (ge)f0.0 r34.0<1>:w r34.0<16;16,1>:w 0:w {@1}
0x00040162, 0x22058550, 0x55582205, 0x00ff00ff, ; rise12, sel (16|M0) (lt)f0.0 r34.0<1>:w r34.0<16;16,1>:w 255:w {@1}
0x00030169, 0x22058220, 0x02462205, 0x00000008, ; rise12, shl (8|M0) r34.0<1>:ud r34.0<8;8,1>:ud 0x8:ud {@1}
0x00030166, 0x0c050220, 0x02462a05, 0x00462205, ; rise12, or (8|M0) r12.0<1>:ud r42.0<8;8,1>:ud r34.0<8;8,1>:ud {@1}
0x00049031, 0x00000000, 0xc000044c, 0x02a00000, ; rise12, send.dc1 (16|M0) null r4 null 0x0 0x120A8000 {@1,$0}
0x00020061, 0x11050220, 0x00340e25, 0x00000000, ; rise12, mov (4|M0) r17.0<1>:ud r14.1<4;4,1>:ud
0x00010061, 0x11850220, 0x00220ea5, 0x00000000, ; rise12, mov (2|M0) r17.4<1>:ud r14.5<2;2,1>:ud
0x00000061, 0x11c50220, 0x00000ee4, 0x00000000, ; rise12, mov (1|M0) r17.6<1>:ud r14.7<0;1,0>:ud
0x00030161, 0x0e050220, 0x00461105, 0x00000000, ; rise12, mov (8|M0) r14.0<1>:ud r17.0<8;8,1>:ud {@1}
0x00020061, 0x11050220, 0x00340f25, 0x00000000, ; rise12, mov (4|M0) r17.0<1>:ud r15.1<4;4,1>:ud
0x00010061, 0x11850220, 0x00220fa5, 0x00000000, ; rise12, mov (2|M0) r17.4<1>:ud r15.5<2;2,1>:ud
0x00000061, 0x11c50220, 0x00000fe4, 0x00000000, ; rise12, mov (1|M0) r17.6<1>:ud r15.7<0;1,0>:ud
0x00030161, 0x0f050220, 0x00461105, 0x00000000, ; rise12, mov (8|M0) r15.0<1>:ud r17.0<8;8,1>:ud {@1}
0x00020061, 0x11050220, 0x00341025, 0x00000000, ; rise12, mov (4|M0) r17.0<1>:ud r16.1<4;4,1>:ud
0x00010061, 0x11850220, 0x002210a5, 0x00000000, ; rise12, mov (2|M0) r17.4<1>:ud r16.5<2;2,1>:ud
0x00000061, 0x11c50220, 0x000010e4, 0x00000000, ; rise12, mov (1|M0) r17.6<1>:ud r16.7<0;1,0>:ud
0x00030161, 0x10050220, 0x00461105, 0x00000000, ; rise12, mov (8|M0) r16.0<1>:ud r17.0<8;8,1>:ud {@1}
0x00003040, 0x04058220, 0x02000404, 0x00000020, ; rise12, add (1|M0) r4.0<1>:ud r4.0<0;1,0>:ud 0x20:ud {$0.src}
0x00000040, 0x02458220, 0x02000244, 0x00000001, ; rise12, add (1|M0) r2.2<1>:ud r2.2<0;1,0>:ud 0x1:ud
0x00440170, 0x00010220, 0x52000244, 0x00000264, ; rise12, cmp (16|M0) (lt)f0.1 null<1>:ud r2.2<0;1,0>:ud r2.3<0;1,0>:ud {@1}
0x80000001, 0x00010000, 0x20000000, 0x00000000, ; rise12, (W) sync.allrd null
0x80000001, 0x00010000, 0x30000000, 0x00000000, ; rise12, (W) sync.allwr null
0x81400120, 0x00004000, 0x00000000, 0xffffe190, ; rise12, (W&f0.1) jmpi L480 {@1}
0x00030061, 0x70050220, 0x00460005, 0x00000000, ; rise12, mov (8|M0) r112.0<1>:ud r0.0<8;8,1>:ud
0x00049031, 0x00000004, 0x7020700c, 0x00000000 ; rise12, send.ts (16|M0) null r112 null 0x0 0x02000010 {EOT,@1,$0}
