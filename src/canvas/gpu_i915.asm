; The Intel GPU's kernels, by hand: Gen9 (graphics version 9), SIMD16. Each kernel is written as its instruction words, four per
; instruction, the words in the order the EU fetches them: a `kernel NAME` line starts it, the words follow, comma separated, with
; comments after a `;`. test/gpu_kernels.py --target intel-gen9 checks these words against Intel's own (IGT's gen9 fill, lib/gpgpu_fill.c)
; and the build (build gpu-kernels) writes them to artifacts/gpu_i915_gen9.inc, which src/canvas/gpu.c includes.
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
