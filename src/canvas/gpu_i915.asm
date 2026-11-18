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
