#!/usr/bin/env python3
"""Hold the build's assembler for the GPU's kernels to LLVM's.

    python3 test/gpu_kernels.py ASM INC     assemble ASM with llvm-mc and fail unless INC (what
                                            `build gpu-kernels ASM INC` wrote) holds exactly the
                                            same words, kernel by kernel
    python3 test/gpu_kernels.py --print-intel SRC
                                            the array gpu_i915_gen9.inc holds, made from SRC
                                            (src/canvas/gpu_i915.asm) the way `build gpu-i915`
                                            writes it: a hand build is
                                            python3 test/gpu_kernels.py --print-intel src/canvas/gpu_i915.asm > artifacts/gpu_i915_gen9.inc
    python3 test/gpu_kernels.py --probe     whether the llvm-mc at hand writes the words the build's
                                            assembler is held to, where LLVM's versions differ: 0
                                            when it does, 2 and why when it does not

    python3 test/gpu_kernels.py --chips ASM CHIP...
                                            fail unless llvm-mc makes of ASM, for each CHIP (gfx1010,
                                            gfx1012, gfx1030), the words it makes for gfx1036: the
                                            kernels are then code for those chips as they stand. LLVM
                                            is compared with itself here, so any LLVM will do

Where LLVM's versions differ. An instruction in the three-source encoding that takes two sources
has a field for the third that the chip does not read: the build's assembler writes the constant 0
there (0x80), as the clang that made the kernels in src/canvas/gpu_compiled.inc did, and LLVM 18 to 21 write
the register 0. An LLVM that writes anything but 0x80 cannot be compared word for word, and the
probe says so; nothing is compared with it and nothing passes. (Forward progress, the first
resource word's top bit, is clear by default before LLVM 21 and set from it: the kernels' blocks
ask for it, so that is the same on all of them.)

The kernels are src/canvas/gpu.asm (what ships) and test/gpu_stress.asm (every instruction in every
operand form the build's assembler takes). Each kernel is its two resource words and then its code,
read out of the object llvm-mc and ld.lld make: the kernel descriptor's rsrc1 and rsrc2, and the
bytes between the kernel's symbol and its size. A kernel that needs scratch is refused, as the
dispatch gives none. Needs llvm-mc and ld.lld with the AMDGPU target.
"""
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

MC = ['-triple=amdgcn-amd-amdhsa', '-mcpu=gfx1036', '-mattr=+wavefrontsize64,-wavefrontsize32']


def tool(name):
    found = shutil.which(name)
    if not found:
        sys.exit('%s: not found' % name)
    return found


def build(asm, chip=None):
    work = pathlib.Path(tempfile.mkdtemp())
    mc = MC
    if chip:
        #   the same text for another chip: the target line and the processor name it, nothing else does
        (work / 'k.asm').write_text(pathlib.Path(asm).read_text().replace('--gfx1036"', '--%s"' % chip))
        asm, mc = work / 'k.asm', [w.replace('gfx1036', chip) for w in MC]
    subprocess.run([tool('llvm-mc')] + mc + ['-filetype=obj', str(asm), '-o', str(work / 'k.o')], check=True)
    subprocess.run([tool('ld.lld'), '-shared', str(work / 'k.o'), '-o', str(work / 'k.so')], check=True)
    data = (work / 'k.so').read_bytes()
    shutil.rmtree(work)
    return data


PROBE = '''.amdgcn_target "amdgcn-amd-amdhsa--gfx1036"
.text
.globl probe
.p2align 8
.type probe,@function
probe:
	v_pk_min_u16 v1, v2, v3
	s_endpgm
.size probe, .-probe
.rodata
.p2align 6
.amdhsa_kernel probe
	.amdhsa_next_free_vgpr 8
	.amdhsa_next_free_sgpr 24
	.amdhsa_wavefront_size32 0
	.amdhsa_forward_progress 1
.end_amdhsa_kernel
'''


def probe():
    work = pathlib.Path(tempfile.mkdtemp())
    (work / 'probe.asm').write_text(PROBE)
    try:
        words = kernels(build(work / 'probe.asm'))[0][1]
    finally:
        shutil.rmtree(work, ignore_errors=True)
    version = subprocess.run([tool('llvm-mc'), '--version'], capture_output=True, text=True).stdout
    version = (re.search(r'version (\S+)', version) or [None, 'of no version it says'])[1]
    third = words[3] >> 18 & 0x1ff
    if third != 0x80:
        print('llvm-mc %s writes 0x%02x in the third source a two-source instruction does not have (v_pk_min_u16 v1, v2, v3 is %08x %08x), '
              'the build\'s assembler the constant 0, 0x80: the kernels are not compared with this LLVM\'s' % (version, third, words[2], words[3]))
        sys.exit(2)
    print('llvm-mc %s writes the words the build\'s assembler is held to' % version)


def chips(asm, names):
    ours = kernels(build(asm))
    for chip in names:
        theirs = kernels(build(asm, chip))
        if [n for n, _ in theirs] != [n for n, _ in ours]:
            sys.exit('%s: for %s the kernels are %s' % (asm, chip, [n for n, _ in theirs]))
        for (name, a), (_, b) in zip(ours, theirs):
            bad = [i for i in range(max(len(a), len(b))) if i >= len(a) or i >= len(b) or a[i] != b[i]]
            if bad:
                sys.exit('%s: kernel %s is not the same words for %s as for gfx1036: %d differ, the first at word %d'
                         % (asm, name, chip, len(bad), bad[0]))
    print('gpu kernels: %s is the same %d words for gfx1036 and for %s'
          % (asm, sum(len(w) for _, w in ours), ', '.join(names)))


def sections(d):
    shoff = struct.unpack_from('<Q', d, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3A)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * shentsize)
        secs.append((name, typ, addr, off, size, link))
    return secs


def kernels(d):
    secs = sections(d)
    symtab = next(s for s in secs if s[1] == 2)
    strtab = secs[symtab[5]]
    syms = {}
    for i in range(symtab[4] // 24):
        name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', d, symtab[3] + i * 24)
        syms[d[strtab[3] + name:d.index(b'\0', strtab[3] + name)].decode()] = (value, size, shndx)
    out = []
    #   the kernels, in the order their descriptors lie in the object
    for name in sorted((n[:-3] for n in syms if n.endswith('.kd')), key=lambda n: (syms[n + '.kd'][2], syms[n + '.kd'][0])):
        kd_addr, _, kd_sec = syms[name + '.kd']
        fn_addr, fn_size, fn_sec = syms[name]
        ro = secs[kd_sec]
        at = ro[3] + kd_addr - ro[2]
        kd = d[at:at + 64]
        privately = struct.unpack_from('<I', kd, 4)[0]
        if privately:
            sys.exit('%s needs scratch (%d bytes): the dispatch gives none' % (name, privately))
        rsrc1, rsrc2 = struct.unpack_from('<II', kd, 48)
        tx = secs[fn_sec]
        code = d[tx[3] + fn_addr - tx[2]:tx[3] + fn_addr - tx[2] + fn_size]
        out.append((name, [rsrc1, rsrc2] + list(struct.unpack('<%dI' % (len(code) // 4), code))))
    return out


def arrays(text):
    return [(name, [int(w, 16) for w in re.findall(r'0x[0-9a-f]+', body)])
            for name, body in re.findall(r'static const unsigned int gpu_k_(\w+)\[\] = \{(.*?)\};', text, re.S)]


def check_regs(asm):
    src = pathlib.Path(asm).read_text()
    macros, cur_m, decls = {}, None, {}
    for line in src.splitlines():
        s = line.split(';', 1)[0].strip()
        if not s:
            continue
        if s.startswith('.macro '):
            cur_m = s.split()[1].rstrip(',')
            macros[cur_m] = []
        elif s == '.endm':
            cur_m = None
        elif cur_m is not None:
            macros[cur_m].append(s)
        m = re.match(r'kernel\s+(\w+)\s*,\s*(\d+)\s*,\s*(\d+)', s)
        if m:
            decls[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    cur_k, k_lines = None, {}
    for line in src.splitlines():
        s = line.split(';', 1)[0].strip()
        if cur_k is None:
            m = re.match(r'^(\w+):$', s)
            if m and m.group(1) in decls:
                cur_k = m.group(1)
                k_lines[cur_k] = []
        elif re.match(r'^\.size\s+' + re.escape(cur_k) + r'\s*,', s):
            cur_k = None
        elif s:
            k_lines[cur_k].append(s)

    def scan(lines, seen):
        vg, sg = -1, -1
        for s in lines:
            op = s.split()[0]
            if op in macros and op not in seen:
                mv, ms = scan(macros[op], seen | {op})
                vg, sg = max(vg, mv), max(sg, ms)
            for a, b in re.findall(r'\bv\[(\d+):(\d+)\]', s):
                vg = max(vg, int(a), int(b))
            for a in re.findall(r'\bv(\d+)\b', s):
                vg = max(vg, int(a))
            for a, b in re.findall(r'\bs\[(\d+):(\d+)\]', s):
                for r in (int(a), int(b)):
                    if r <= 105:
                        sg = max(sg, r)
            for a in re.findall(r'\bs(\d+)\b', s):
                r = int(a)
                if r <= 105:
                    sg = max(sg, r)
        return vg, sg

    for name, (nv, ns) in decls.items():
        if name not in k_lines:
            continue
        vg, sg = scan(k_lines[name], set())
        if vg >= nv:
            sys.exit('%s: kernel %s uses v%d, exceeding .amdhsa_next_free_vgpr %d' % (asm, name, vg, nv))
        if sg >= ns:
            sys.exit('%s: kernel %s uses s%d, exceeding .amdhsa_next_free_sgpr %d' % (asm, name, sg, ns))


def intel_kernels(src_text):
    """The Intel kernels of src/canvas/gpu_i915.asm: kernel NAME, then the words, comma separated, comments after ';'."""
    kernels, name = [], None
    for raw in src_text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if not line:
            continue
        if line.startswith('kernel '):
            name = line.split()[1]
            kernels.append((name, []))
            continue
        if name is None or not re.fullmatch(r'0x[0-9a-fA-F]+(\s*,\s*0x[0-9a-fA-F]+)*\s*,?', line):
            sys.exit('gpu_i915.asm: not a kernel word line: %r' % raw)
        kernels[-1][1].extend(int(w, 16) for w in re.findall(r'0x[0-9a-fA-F]+', line))
    return kernels


# IGT's gen9 gpgpu_fill kernel (lib/gpgpu_fill.c, gen9_gpgpu_kernel, 10 rows of 4 words), rows 1 to 9 of it: the
# fill without the replicate of row 0 (the colour is a dword, see gpu_i915.asm). Read from IGT, not from our own build.
IGT_GEN9_FILL_ROWS_1_TO_9 = [
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x00800001, 0x20a00208, 0x00000020, 0x00000000,
    0x0c800031, 0x24000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


def intel_text(kernels):
    """The include build gpu-i915 writes, byte for byte: one array per kernel, ten words a line. A full line ends with a comma and
    a newline; a short line (the array's last) ends with ', ' and no newline; the close is a newline, '};' and a newline, so an
    array whose last line is full has a blank line before its '};', as the build's output does."""
    out = []
    for name, words in kernels:
        out.append('static const unsigned int gpu_i915_%s[] = {\n' % name)
        for at in range(0, len(words), 10):
            line = words[at:at + 10]
            if len(line) == 10:
                out.append('    ' + ', '.join('0x%08x' % w for w in line) + ',\n')
            else:
                out.append('    ' + ', '.join('0x%08x' % w for w in line) + ', ')
        out.append('\n};\n')
    return ''.join(out)


# The copy (op 0): src/canvas/gpu_i915_copy.iga assembled by iga64 -a -p=9 (the words the Gen9 copy was verified with on subtle-cottage).
COPY_GEN9_WORDS = [
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x20a00208, 0x06000080, 0x02190001,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


# The tone (op 2): src/canvas/gpu_i915_tone.iga assembled by iga64 -a -p=9, 43 instructions.
TONE_GEN9_WORDS = [
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x20a00208, 0x06000080, 0x02190000,
    0x00000001, 0x22800208, 0x00000020, 0x00000000,
    0x00000008, 0x22a00208, 0x06000280, 0x00000018,
    0x00000040, 0x22c00208, 0x060042a0, 0x000000ff,
    0x00000005, 0x22e00208, 0x06000280, 0x00ff00ff,
    0x00000008, 0x23200208, 0x06000280, 0x00000008,
    0x00000005, 0x23200208, 0x06000320, 0x00ff00ff,
    0x00400001, 0x21c00208, 0x002000a0, 0x00000000,
    0x00400005, 0x21400208, 0x062000a0, 0x00ff00ff,
    0x00400008, 0x21600208, 0x062000a0, 0x00000008,
    0x00400005, 0x21600208, 0x06200160, 0x00ff00ff,
    0x00400041, 0x21400208, 0x12200140, 0x000002c0,
    0x00400040, 0x21400208, 0x06200140, 0x007f007f,
    0x00400041, 0x21600208, 0x12200160, 0x000002c0,
    0x00400040, 0x21600208, 0x06200160, 0x007f007f,
    0x00400008, 0x21800208, 0x06200140, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x00200140,
    0x00400040, 0x21800208, 0x06200180, 0x00010001,
    0x00400008, 0x21800208, 0x06200180, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400008, 0x21a00208, 0x06200160, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21a00208, 0x022001a0, 0x00200160,
    0x00400040, 0x21a00208, 0x062001a0, 0x00010001,
    0x00400008, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x000002e0,
    0x00400040, 0x21a00208, 0x022001a0, 0x00000320,
    0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff,
    0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff,
    0x00400009, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400006, 0x20a00208, 0x02200180, 0x002001a0,
    0x01400010, 0x20000200, 0x060002a0, 0x00000000,
    0x00410001, 0x20a00208, 0x002001c0, 0x00000000,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


# The glass (op 3): src/canvas/gpu_i915_glass.iga assembled by iga64 -a -p=9, 71 instructions.
GLASS_GEN9_WORDS = [
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x20a00208, 0x06000080, 0x02190001,
    0x0c800031, 0x20c00208, 0x06000080, 0x02190002,
    0x00000005, 0x22800208, 0x06000024, 0x00000001,
    0x00000040, 0x22840208, 0x02000018, 0x00000028,
    0x00000005, 0x22840208, 0x06000284, 0x00000001,
    0x00000007, 0x22880208, 0x02000280, 0x00000284,
    0x00000009, 0x22a00208, 0x06000288, 0x00000001,
    0x00000040, 0x22a00208, 0x020002a0, 0x00000284,
    0x00000009, 0x22a40208, 0x06000288, 0x00000001,
    0x00000040, 0x22a40208, 0x060042a4, 0x00000002,
    0x00000040, 0x22a40208, 0x020002a4, 0x00000284,
    0x00000001, 0x22c00608, 0x00000000, 0x00000000,
    0x02000010, 0x20000200, 0x06000020, 0x00000000,
    0x00010001, 0x22c00608, 0x00000000, 0x00000001,
    0x00000040, 0x22c40208, 0x060042c0, 0x00000001,
    0x00000001, 0x22e00208, 0x000002a0, 0x00000000,
    0x00000001, 0x22e40208, 0x000002a4, 0x00000000,
    0x00000001, 0x22e80208, 0x000002a0, 0x00000000,
    0x00000001, 0x22ec0208, 0x000002a4, 0x00000000,
    0x00400041, 0x23000208, 0x122002e0, 0x000002c4,
    0x00400041, 0x23200208, 0x122002e0, 0x000002c0,
    0x00400009, 0x23400208, 0x06200300, 0x00000010,
    0x00400006, 0x23400208, 0x02200340, 0x00200300,
    0x00400009, 0x23600208, 0x06200320, 0x00000010,
    0x00400006, 0x23600208, 0x02200360, 0x00200320,
    0x00400005, 0x20e00208, 0x062000c0, 0x00ff00ff,
    0x00400008, 0x21000208, 0x062000c0, 0x00000008,
    0x00400005, 0x21000208, 0x06200100, 0x00ff00ff,
    0x00600040, 0x20e01a68, 0x1a8d00e0, 0x008d0340,
    0x00600040, 0x20e01a68, 0x1a8d00e0, 0x008d4360,
    0x00600040, 0x21001a68, 0x1a8d0100, 0x008d0300,
    0x00600040, 0x21001a68, 0x1a8d0100, 0x008d4320,
    0x04600002, 0x20e01a68, 0x1e8d00e0, 0x00000000,
    0x05600002, 0x20e01a68, 0x1e8d00e0, 0x00ff00ff,
    0x04600002, 0x21001a68, 0x1e8d0100, 0x00000000,
    0x05600002, 0x21001a68, 0x1e8d0100, 0x00ff00ff,
    0x00400008, 0x21200208, 0x062000a0, 0x00000018,
    0x00400040, 0x21200208, 0x06204120, 0x000000ff,
    0x00400005, 0x21400208, 0x062000a0, 0x00ff00ff,
    0x00400008, 0x21600208, 0x062000a0, 0x00000008,
    0x00400005, 0x21600208, 0x06200160, 0x00ff00ff,
    0x00400041, 0x21800208, 0x122000e0, 0x008a0120,
    0x00400040, 0x21800208, 0x06200180, 0x007f007f,
    0x00400008, 0x21a00208, 0x06200180, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21a00208, 0x022001a0, 0x00200180,
    0x00400040, 0x21a00208, 0x062001a0, 0x00010001,
    0x00400008, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21a00208, 0x022001a0, 0x00200140,
    0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff,
    0x00400041, 0x21c00208, 0x12200100, 0x008a0120,
    0x00400040, 0x21c00208, 0x062001c0, 0x007f007f,
    0x00400008, 0x21e00208, 0x062001c0, 0x00000008,
    0x00400005, 0x21e00208, 0x062001e0, 0x00ff00ff,
    0x00400040, 0x21e00208, 0x022001e0, 0x002001c0,
    0x00400040, 0x21e00208, 0x062001e0, 0x00010001,
    0x00400008, 0x21e00208, 0x062001e0, 0x00000008,
    0x00400005, 0x21e00208, 0x062001e0, 0x00ff00ff,
    0x00400040, 0x21e00208, 0x022001e0, 0x00200160,
    0x05600002, 0x21e01248, 0x168d01e0, 0x00ff00ff,
    0x00400009, 0x21e00208, 0x062001e0, 0x00000008,
    0x00400006, 0x20a00208, 0x022001a0, 0x002001e0,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


# The scene (op 6): src/canvas/gpu_i915_scene.iga assembled by iga64 -a -p=9, 87 instructions.
SCENE_GEN9_WORDS = [
    0x00000001, 0x24200608, 0x00000000, 0x00000000,
    0x00000001, 0x24240608, 0x00000000, 0x00000001,
    0x00000001, 0x24280608, 0x00000000, 0x00000002,
    0x00000001, 0x242c0608, 0x00000000, 0x00000003,
    0x00000001, 0x23800208, 0x00000020, 0x00000000,
    0x00000001, 0x23840608, 0x00000000, 0x00000000,
    0x00000009, 0x23000208, 0x06000004, 0x00000002,
    0x00000040, 0x23000208, 0x02000300, 0x00000024,
    0x00000040, 0x23400208, 0x02000018, 0x00000028,
    0x00000009, 0x20400208, 0x06000004, 0x00000004,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x20a00208, 0x06000080, 0x02190000,
    0x00400001, 0x21c00208, 0x002000a0, 0x00000000,
    0x00400040, 0x24400208, 0x02200420, 0x00000300,
    0x00600001, 0x24000208, 0x008d0000, 0x00000000,
    0x00000005, 0x25800208, 0x06000384, 0x00000001,
    0x00000009, 0x25800208, 0x06000580, 0x00000005,
    0x00000001, 0x24000208, 0x00000580, 0x00000000,
    0x00000008, 0x25a00208, 0x06000384, 0x00000001,
    0x00000001, 0x24040208, 0x000005a0, 0x00000000,
    0x00000001, 0x24080608, 0x00000000, 0x0000001f,
    0x0c800031, 0x23c00208, 0x06000400, 0x02190001,
    0x00000001, 0x22800208, 0x000003d4, 0x00000000,
    0x00000008, 0x22a00208, 0x06000280, 0x00000018,
    0x00000040, 0x22c00208, 0x060042a0, 0x000000ff,
    0x00000005, 0x22e00208, 0x06000280, 0x00ff00ff,
    0x00000008, 0x23200208, 0x06000280, 0x00000008,
    0x00000005, 0x23200208, 0x06000320, 0x00ff00ff,
    0x00400005, 0x21400208, 0x062001c0, 0x00ff00ff,
    0x00400008, 0x21600208, 0x062001c0, 0x00000008,
    0x00400005, 0x21600208, 0x06200160, 0x00ff00ff,
    0x00400041, 0x21400208, 0x12200140, 0x000002c0,
    0x00400040, 0x21400208, 0x06200140, 0x007f007f,
    0x00400041, 0x21600208, 0x12200160, 0x000002c0,
    0x00400040, 0x21600208, 0x06200160, 0x007f007f,
    0x00400008, 0x21800208, 0x06200140, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x00200140,
    0x00400040, 0x21800208, 0x06200180, 0x00010001,
    0x00400008, 0x21800208, 0x06200180, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400008, 0x21a00208, 0x06200160, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21a00208, 0x022001a0, 0x00200160,
    0x00400040, 0x21a00208, 0x062001a0, 0x00010001,
    0x00400008, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x000002e0,
    0x00400040, 0x21a00208, 0x022001a0, 0x00000320,
    0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff,
    0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff,
    0x00400009, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400006, 0x20a00208, 0x02200180, 0x002001a0,
    0x00000040, 0x25600208, 0x060043c0, 0x00000000,
    0x00000006, 0x25000208, 0x020003c0, 0x00000560,
    0x00000007, 0x25200208, 0x060003c0, 0x00000001,
    0x00000040, 0x25600208, 0x06004520, 0x00000000,
    0x00000006, 0x25200208, 0x02000520, 0x00000560,
    0x00000040, 0x25400208, 0x060002a0, 0xffffffff,
    0x00000006, 0x25040208, 0x02000520, 0x00000540,
    0x00000040, 0x25400208, 0x02000340, 0x000043c8,
    0x00000040, 0x25200208, 0x020003d0, 0x00004340,
    0x00000040, 0x25200208, 0x06000520, 0xffffffff,
    0x00000006, 0x25080208, 0x02000540, 0x00000520,
    0x00400040, 0x24600208, 0x02200440, 0x000043c4,
    0x00400040, 0x24800208, 0x020003cc, 0x00204440,
    0x00400040, 0x24800208, 0x06200480, 0xffffffff,
    0x00400006, 0x24a00208, 0x02200460, 0x00200480,
    0x00400006, 0x24a00208, 0x022004a0, 0x00000508,
    0x00400006, 0x24a00208, 0x022004a0, 0x00000500,
    0x00400006, 0x24c00208, 0x02200460, 0x00200480,
    0x00400006, 0x24c00208, 0x022004c0, 0x00000508,
    0x00400006, 0x24c00208, 0x022004c0, 0x00000504,
    0x04400010, 0x20000a20, 0x0e2004a0, 0x00000000,
    0x04400010, 0x20000a22, 0x0e2004c0, 0x00000000,
    0x00410001, 0x21c0020a, 0x002000a0, 0x00000000,
    0x00410001, 0x21c00208, 0x00000280, 0x00000000,
    0x00000040, 0x23840208, 0x06000384, 0x00000001,
    0x05800010, 0x20000201, 0x02000384, 0x00000380,
    0x00010020, 0x34000005, 0x0e001400, 0xfffffbe0,
    0x00400001, 0x20a00208, 0x002001c0, 0x00000000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


# The cp (op 4): src/canvas/gpu_i915_cp.iga assembled by iga64 -a -p=9, 127 instructions.
CP_GEN9_WORDS = [
    0x00000001, 0x27800208, 0x00000020, 0x00000000,
    0x00000001, 0x27840208, 0x00000024, 0x00000000,
    0x00000001, 0x27880208, 0x00000028, 0x00000000,
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x20a00208, 0x06000080, 0x02190000,
    0x00400001, 0x20c00208, 0x002000a0, 0x00000000,
    0x00000009, 0x27a00208, 0x06000004, 0x00000002,
    0x00000008, 0x27a40208, 0x06000004, 0x00000004,
    0x00000008, 0x27a80208, 0x06000018, 0x00000006,
    0x00000041, 0x27ac0208, 0x120007a8, 0x00000784,
    0x00000040, 0x27ac0208, 0x020007ac, 0x000007a4,
    0x00000009, 0x27b00208, 0x060007ac, 0x00000003,
    0x00000005, 0x27c00208, 0x060007b0, 0x00003fff,
    0x00000008, 0x27c40208, 0x060007b0, 0x0000000e,
    0x00600001, 0x25000208, 0x008d0000, 0x00000000,
    0x00200001, 0x25000208, 0x004507c0, 0x00000000,
    0x00000001, 0x25080608, 0x00000000, 0x00000007,
    0x0c800031, 0x25200208, 0x06000500, 0x02190001,
    0x00000001, 0x27e00208, 0x00000520, 0x00000000,
    0x00000001, 0x27e40208, 0x00000524, 0x00000000,
    0x00000009, 0x27e80208, 0x060007e0, 0x00000005,
    0x00000040, 0x27e80208, 0x020007e8, 0x00000788,
    0x00000001, 0x2b800608, 0x00000000, 0x00000000,
    0x00000001, 0x2b840608, 0x00000000, 0x00000001,
    0x00000001, 0x2b880608, 0x00000000, 0x00000002,
    0x00000001, 0x2b8c0608, 0x00000000, 0x00000003,
    0x00400040, 0x2b600208, 0x02690b80, 0x000007a0,
    0x01000010, 0x20000202, 0x060007e4, 0x00000000,
    0x00010020, 0x34000006, 0x0e001400, 0x000005b0,
    0x00000040, 0x28000208, 0x060007e8, 0x00000000,
    0x00000005, 0x28040208, 0x06000800, 0x00003fff,
    0x00000008, 0x28080208, 0x06000800, 0x0000000e,
    0x00600001, 0x25800208, 0x008d0000, 0x00000000,
    0x00200001, 0x25800208, 0x00450804, 0x00000000,
    0x00000001, 0x25880608, 0x00000000, 0x00000007,
    0x0c800031, 0x28c00208, 0x06000580, 0x02190001,
    0x00000040, 0x28000208, 0x060007e8, 0x00000008,
    0x00000005, 0x28040208, 0x06000800, 0x00003fff,
    0x00000008, 0x28080208, 0x06000800, 0x0000000e,
    0x00600001, 0x25a00208, 0x008d0000, 0x00000000,
    0x00200001, 0x25a00208, 0x00450804, 0x00000000,
    0x00000001, 0x25a80608, 0x00000000, 0x00000007,
    0x0c800031, 0x28e00208, 0x060005a0, 0x02190001,
    0x00000040, 0x28000208, 0x060007e8, 0x00000010,
    0x00000005, 0x28040208, 0x06000800, 0x00003fff,
    0x00000008, 0x28080208, 0x06000800, 0x0000000e,
    0x00600001, 0x25c00208, 0x008d0000, 0x00000000,
    0x00200001, 0x25c00208, 0x00450804, 0x00000000,
    0x00000001, 0x25c80608, 0x00000000, 0x00000007,
    0x0c800031, 0x29000208, 0x060005c0, 0x02190001,
    0x00000040, 0x28000208, 0x060007e8, 0x00000018,
    0x00000005, 0x28040208, 0x06000800, 0x00003fff,
    0x00000008, 0x28080208, 0x06000800, 0x0000000e,
    0x00600001, 0x25e00208, 0x008d0000, 0x00000000,
    0x00200001, 0x25e00208, 0x00450804, 0x00000000,
    0x00000001, 0x25e80608, 0x00000000, 0x00000007,
    0x0c800031, 0x29200208, 0x060005e0, 0x02190001,
    0x00000001, 0x2a000208, 0x000008c0, 0x00000000,
    0x00000001, 0x2a040208, 0x000008c4, 0x00000000,
    0x00000001, 0x2a080208, 0x000008e0, 0x00000000,
    0x00000001, 0x2a0c0208, 0x000008e4, 0x00000000,
    0x00000001, 0x2a100208, 0x00000900, 0x00000000,
    0x00000001, 0x2a140208, 0x00000904, 0x00000000,
    0x00000008, 0x2a200208, 0x06000a14, 0x00000018,
    0x00000040, 0x2a800208, 0x06004a20, 0x000000ff,
    0x00000005, 0x2aa00208, 0x06000a14, 0x00ff00ff,
    0x00000008, 0x2ac00208, 0x06000a14, 0x00000008,
    0x00000005, 0x2ac00208, 0x06000ac0, 0x00ff00ff,
    0x05000002, 0x2a240a28, 0x0a000a0c, 0x00000780,
    0x04000002, 0x2a240a28, 0x0a000a24, 0x00000a04,
    0x00000040, 0x2a280208, 0x02000a24, 0x00004a04,
    0x00000040, 0x2a400208, 0x02000018, 0x00004a08,
    0x04000002, 0x2a440a28, 0x0a000a10, 0x00000a08,
    0x00000040, 0x2a440208, 0x02000a44, 0x00004a08,
    0x00000001, 0x2a480608, 0x00000000, 0x00000000,
    0x05000010, 0x20000200, 0x02000a40, 0x00000a44,
    0x00010001, 0x2a480208, 0x00000a28, 0x00000000,
    0x01000010, 0x20000200, 0x06000a20, 0x00000000,
    0x00000001, 0x2a600608, 0x00000000, 0x00000000,
    0x00010001, 0x2a600608, 0x00000000, 0x00000001,
    0x00000005, 0x2a600208, 0x02000a60, 0x00000a00,
    0x02000010, 0x20000200, 0x06000a60, 0x00000000,
    0x00010001, 0x2a480608, 0x00000000, 0x00000000,
    0x00400040, 0x2b400208, 0x02690b60, 0x00004a04,
    0x05400010, 0x20000200, 0x02690b40, 0x00000a48,
    0x01000010, 0x20000202, 0x06000a00, 0x00000000,
    0x00010020, 0x34000006, 0x0e001400, 0x000001c0,
    0x00400005, 0x21400208, 0x062000a0, 0x00ff00ff,
    0x00400008, 0x21600208, 0x062000a0, 0x00000008,
    0x00400005, 0x21600208, 0x06200160, 0x00ff00ff,
    0x00400041, 0x21400208, 0x12200140, 0x00000a80,
    0x00400040, 0x21400208, 0x06200140, 0x007f007f,
    0x00400041, 0x21600208, 0x12200160, 0x00000a80,
    0x00400040, 0x21600208, 0x06200160, 0x007f007f,
    0x00400008, 0x21800208, 0x06200140, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x00200140,
    0x00400040, 0x21800208, 0x06200180, 0x00010001,
    0x00400008, 0x21800208, 0x06200180, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400008, 0x21a00208, 0x06200160, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21a00208, 0x022001a0, 0x00200160,
    0x00400040, 0x21a00208, 0x062001a0, 0x00010001,
    0x00400008, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x00000aa0,
    0x00400040, 0x21a00208, 0x022001a0, 0x00000ac0,
    0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff,
    0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff,
    0x00400009, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400006, 0x21e00208, 0x02200180, 0x002001a0,
    0x05400010, 0x20000200, 0x02690b40, 0x00000a48,
    0x00410001, 0x20a00208, 0x002001e0, 0x00000000,
    0x00000020, 0x34000004, 0x0e001400, 0x00000010,
    0x00410001, 0x20a00208, 0x00000a14, 0x00000000,
    0x00000040, 0x27e40208, 0x060007e4, 0xffffffff,
    0x00000040, 0x27e80208, 0x060007e8, 0x00000020,
    0x02000010, 0x20000202, 0x060007e4, 0x00000000,
    0x00010020, 0x34000006, 0x0e001400, 0xfffffa50,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


def intel_arrays(text):
    return [(name, [int(w, 16) for w in re.findall(r'0x[0-9a-f]+', body)])
            for name, body in re.findall(r'static const unsigned int gpu_i915_(\w+)\[\] = \{(.*?)\};', text, re.S)]


# The box (op 5, the rectangle part): src/canvas/gpu_i915_bx.iga assembled by iga64 -a -p=9, 81 instructions.
BX_GEN9_WORDS = [
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00200001, 0x20800208, 0x00450040, 0x00000000,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x20a00208, 0x06000080, 0x02190000,
    0x00600001, 0x22800208, 0x008d0020, 0x00000000,
    0x00000008, 0x22c00208, 0x06000280, 0x00000018,
    0x00000005, 0x23200208, 0x06000280, 0x00ff00ff,
    0x00000008, 0x23400208, 0x06000280, 0x00000008,
    0x00000005, 0x23400208, 0x06000340, 0x000000ff,
    0x00000009, 0x25200208, 0x06000018, 0x00000008,
    0x00000040, 0x25200208, 0x06000520, 0x00000008,
    0x00000040, 0x25400a28, 0x0a00028c, 0x00004520,
    0x00000040, 0x25400a28, 0x0e000540, 0x0000000f,
    0x0000000c, 0x25400a28, 0x0e000540, 0x00000004,
    0x00000040, 0x25600a28, 0x0a000290, 0x00004520,
    0x00000040, 0x25600a28, 0x0e000560, 0x0000000f,
    0x0000000c, 0x25600a28, 0x0e000560, 0x00000004,
    0x05000002, 0x25400a28, 0x0e000540, 0x00000010,
    0x04000002, 0x25400a28, 0x0e000540, 0x00000000,
    0x05000002, 0x25600a28, 0x0e000560, 0x00000010,
    0x04000002, 0x25600a28, 0x0e000560, 0x00000000,
    0x00000040, 0x25800a28, 0x0a000560, 0x00004540,
    0x04000002, 0x25800a28, 0x0e000580, 0x00000000,
    0x00000041, 0x23000208, 0x120002c0, 0x00000580,
    0x00000001, 0x23c00608, 0x00000000, 0x00000000,
    0x00000001, 0x23c40608, 0x00000000, 0x00000001,
    0x00000001, 0x23c80608, 0x00000000, 0x00000002,
    0x00000001, 0x23cc0608, 0x00000000, 0x00000003,
    0x00000009, 0x23e00208, 0x06000004, 0x00000002,
    0x00400040, 0x23e00208, 0x022003c0, 0x000003e0,
    0x00400009, 0x23e00208, 0x062003e0, 0x00000008,
    0x04400002, 0x24000a28, 0x0a000284, 0x002003e0,
    0x00400040, 0x24200a28, 0x0e2003e0, 0x00000100,
    0x05400002, 0x24200a28, 0x0a000288, 0x00200420,
    0x00400040, 0x24400a28, 0x0a200420, 0x00204400,
    0x04400002, 0x24400a28, 0x0e200440, 0x00000000,
    0x00400041, 0x24600208, 0x12200440, 0x00000300,
    0x00400008, 0x24600208, 0x06200460, 0x0000000c,
    0x00400008, 0x24800208, 0x06200460, 0x00000007,
    0x00400040, 0x24800208, 0x02200480, 0x00200460,
    0x00400040, 0x24a00a28, 0x0e204460, 0x000000ff,
    0x00400041, 0x24c00208, 0x12000320, 0x00400480,
    0x00400040, 0x24c00208, 0x062004c0, 0x00800080,
    0x00400008, 0x24c00208, 0x062004c0, 0x00000008,
    0x00400005, 0x24c00208, 0x062004c0, 0x00ff00ff,
    0x00400041, 0x24e00208, 0x12000340, 0x00400480,
    0x00400040, 0x24e00208, 0x062004e0, 0x00000080,
    0x00400008, 0x24e00208, 0x062004e0, 0x00000008,
    0x00400009, 0x25000208, 0x06200460, 0x00000010,
    0x00400006, 0x24e00208, 0x022004e0, 0x00200500,
    0x00400001, 0x21c00208, 0x002000a0, 0x00000000,
    0x00400005, 0x21400208, 0x062001c0, 0x00ff00ff,
    0x00400008, 0x21600208, 0x062001c0, 0x00000008,
    0x00400005, 0x21600208, 0x06200160, 0x00ff00ff,
    0x00400041, 0x21400208, 0x12200140, 0x004004a0,
    0x00400040, 0x21400208, 0x06200140, 0x007f007f,
    0x00400041, 0x21600208, 0x12200160, 0x004004a0,
    0x00400040, 0x21600208, 0x06200160, 0x007f007f,
    0x00400008, 0x21800208, 0x06200140, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x00200140,
    0x00400040, 0x21800208, 0x06200180, 0x00010001,
    0x00400008, 0x21800208, 0x06200180, 0x00000008,
    0x00400005, 0x21800208, 0x06200180, 0x00ff00ff,
    0x00400008, 0x21a00208, 0x06200160, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21a00208, 0x022001a0, 0x00200160,
    0x00400040, 0x21a00208, 0x062001a0, 0x00010001,
    0x00400008, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400005, 0x21a00208, 0x062001a0, 0x00ff00ff,
    0x00400040, 0x21800208, 0x02200180, 0x002004c0,
    0x00400040, 0x21a00208, 0x022001a0, 0x002004e0,
    0x05600002, 0x21801248, 0x168d0180, 0x00ff00ff,
    0x05600002, 0x21a01248, 0x168d01a0, 0x00ff00ff,
    0x00400009, 0x21a00208, 0x062001a0, 0x00000008,
    0x00400006, 0x20a00208, 0x02200180, 0x002001a0,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]

# The scale (op 7): src/canvas/gpu_i915_scale.iga assembled by iga64 -a -p=9, 75 instructions.
SCALE_GEN9_WORDS = [
    0x00000041, 0x20400208, 0x06000004, 0x00000010,
    0x00000001, 0x20440208, 0x00000018, 0x00000000,
    0x00600001, 0x20c00208, 0x008d0000, 0x00000000,
    0x00000001, 0x20c80608, 0x00000000, 0x0000000f,
    0x00000005, 0x20c00208, 0x06000040, 0x00003fff,
    0x00000008, 0x20c40208, 0x06000040, 0x0000000e,
    0x0c800031, 0x20a00208, 0x060000c0, 0x02190002,
    0x00000040, 0x20600208, 0x02000024, 0x00000044,
    0x00000009, 0x20600208, 0x06000060, 0x00000002,
    0x00600001, 0x21000208, 0x008d0000, 0x00000000,
    0x00000001, 0x21080608, 0x00000000, 0x0000000f,
    0x00000005, 0x21000208, 0x06000060, 0x00003fff,
    0x00000008, 0x21040208, 0x06000060, 0x0000000e,
    0x0c800031, 0x20e00208, 0x06000100, 0x02190002,
    0x00000041, 0x21200208, 0x120000e0, 0x00000020,
    0x00000008, 0x20640208, 0x06000040, 0x00000002,
    0x00000040, 0x20600a28, 0x0a000024, 0x00004064,
    0x05000002, 0x20680a28, 0x0e000060, 0x00000004,
    0x04000002, 0x20680a28, 0x0e000068, 0x00000000,
    0x00000040, 0x27800208, 0x02000120, 0x000000a0,
    0x00000009, 0x27800208, 0x06000780, 0x00000002,
    0x06000010, 0x20000a20, 0x0e000068, 0x00000000,
    0x00010001, 0x27800608, 0x00000000, 0x00000000,
    0x00600001, 0x23c00208, 0x008d0000, 0x00000000,
    0x00000005, 0x23c00208, 0x06000780, 0x00003fff,
    0x00000008, 0x23c40208, 0x06000780, 0x0000000e,
    0x00000001, 0x23c80608, 0x00000000, 0x0000000f,
    0x0c800031, 0x25000208, 0x060003c0, 0x02190001,
    0x00000040, 0x27a00208, 0x02000120, 0x000000a4,
    0x00000009, 0x27a00208, 0x060007a0, 0x00000002,
    0x06000010, 0x20000a20, 0x0e000068, 0x00000001,
    0x00010001, 0x27a00608, 0x00000000, 0x00000000,
    0x00600001, 0x23e00208, 0x008d0000, 0x00000000,
    0x00000005, 0x23e00208, 0x060007a0, 0x00003fff,
    0x00000008, 0x23e40208, 0x060007a0, 0x0000000e,
    0x00000001, 0x23e80608, 0x00000000, 0x0000000f,
    0x0c800031, 0x25200208, 0x060003e0, 0x02190001,
    0x00000040, 0x27c00208, 0x02000120, 0x000000a8,
    0x00000009, 0x27c00208, 0x060007c0, 0x00000002,
    0x06000010, 0x20000a20, 0x0e000068, 0x00000002,
    0x00010001, 0x27c00608, 0x00000000, 0x00000000,
    0x00600001, 0x24000208, 0x008d0000, 0x00000000,
    0x00000005, 0x24000208, 0x060007c0, 0x00003fff,
    0x00000008, 0x24040208, 0x060007c0, 0x0000000e,
    0x00000001, 0x24080608, 0x00000000, 0x0000000f,
    0x0c800031, 0x25400208, 0x06000400, 0x02190001,
    0x00000040, 0x27e00208, 0x02000120, 0x000000ac,
    0x00000009, 0x27e00208, 0x060007e0, 0x00000002,
    0x06000010, 0x20000a20, 0x0e000068, 0x00000003,
    0x00010001, 0x27e00608, 0x00000000, 0x00000000,
    0x00600001, 0x24200208, 0x008d0000, 0x00000000,
    0x00000005, 0x24200208, 0x060007e0, 0x00003fff,
    0x00000008, 0x24240208, 0x060007e0, 0x0000000e,
    0x00000001, 0x24280608, 0x00000000, 0x0000000f,
    0x0c800031, 0x25600208, 0x06000420, 0x02190001,
    0x00000041, 0x21280208, 0x12000044, 0x00000028,
    0x00000009, 0x21280208, 0x06000128, 0x00000002,
    0x00000040, 0x21280208, 0x02000128, 0x00000040,
    0x00600001, 0x20800208, 0x008d0000, 0x00000000,
    0x00000005, 0x20800208, 0x06000128, 0x00003fff,
    0x00000008, 0x20840208, 0x06000128, 0x0000000e,
    0x00000001, 0x20880608, 0x00000000, 0x0000000f,
    0x0c800031, 0x26400208, 0x06000080, 0x02190000,
    0x00400001, 0x20a00208, 0x00200640, 0x00000000,
    0x03000010, 0x20000a20, 0x0e000068, 0x00000000,
    0x00010001, 0x20a00208, 0x00000500, 0x00000000,
    0x03000010, 0x20000a20, 0x0e000068, 0x00000001,
    0x00010001, 0x20a40208, 0x00000520, 0x00000000,
    0x03000010, 0x20000a20, 0x0e000068, 0x00000002,
    0x00010001, 0x20a80208, 0x00000540, 0x00000000,
    0x03000010, 0x20000a20, 0x0e000068, 0x00000003,
    0x00010001, 0x20ac0208, 0x00000560, 0x00000000,
    0x0c800031, 0x20000a40, 0x06000080, 0x060a8000,
    0x00600001, 0x2e000208, 0x008d0000, 0x00000000,
    0x07800031, 0x20000a40, 0x06000e00, 0x82000010,
]


def intel_check(src, inc):
    kernels = intel_kernels(pathlib.Path(src).read_text())
    if [n for n, _ in kernels] != ['copy', 'fill', 'tone', 'glass', 'bx', 'scene', 'cp', 'scale']:
        sys.exit('%s: the kernels are %s, the Gen9 set has copy, fill, tone, glass, bx, scene, cp and scale' % (src, [n for n, _ in kernels]))
    copy = kernels[0][1]
    if copy != COPY_GEN9_WORDS:
        sys.exit('%s: copy differs from the iga64 assembly of gpu_i915_copy.iga (%d words, the reviewed kernel has %d)' % (src, len(copy), len(COPY_GEN9_WORDS)))
    fill = kernels[1][1]
    tone = kernels[2][1]
    glass = kernels[3][1]
    if glass != GLASS_GEN9_WORDS:
        sys.exit('%s: glass differs from the iga64 assembly of gpu_i915_glass.iga (%d words, the reviewed kernel has %d)' % (src, len(glass), len(GLASS_GEN9_WORDS)))
    if tone != TONE_GEN9_WORDS:
        sys.exit('%s: tone differs from the iga64 assembly of gpu_i915_tone.iga (%d words, the reviewed kernel has %d)' % (src, len(tone), len(TONE_GEN9_WORDS)))
    if kernels[5][1] != SCENE_GEN9_WORDS:
        sys.exit('%s: scene differs from the iga64 assembly of gpu_i915_scene.iga (%d words, the reviewed kernel has %d)' % (src, len(kernels[5][1]), len(SCENE_GEN9_WORDS)))
    if kernels[6][1] != CP_GEN9_WORDS:
        sys.exit('%s: cp differs from the iga64 assembly of gpu_i915_cp.iga (%d words, the reviewed kernel has %d)' % (src, len(kernels[6][1]), len(CP_GEN9_WORDS)))
    if kernels[7][1] != SCALE_GEN9_WORDS:
        sys.exit('%s: scale differs from the iga64 assembly of gpu_i915_scale.iga (%d words, the reviewed kernel has %d)' % (src, len(kernels[7][1]), len(SCALE_GEN9_WORDS)))
    if kernels[4][1] != BX_GEN9_WORDS:
        sys.exit('%s: bx differs from the iga64 assembly of gpu_i915_bx.iga (%d words, the reviewed kernel has %d)' % (src, len(kernels[4][1]), len(BX_GEN9_WORDS)))
    if fill != IGT_GEN9_FILL_ROWS_1_TO_9:
        bad = [i for i in range(max(len(fill), 36)) if i >= len(fill) or i >= 36 or fill[i] != IGT_GEN9_FILL_ROWS_1_TO_9[i]]
        sys.exit('%s: fill differs from IGT at word %d (%s)' % (src, bad[0], 'missing' if bad[0] >= len(fill) else '%08x vs IGT' % fill[bad[0]]))
    made = intel_arrays(pathlib.Path(inc).read_text())
    if [n for n, _ in made] != ['copy', 'fill', 'tone', 'glass', 'bx', 'scene', 'cp', 'scale']:
        sys.exit('%s: the arrays are %s, the source has copy, fill, tone, glass, bx, scene, cp and scale' % (inc, [n for n, _ in made]))
    for (name, words), want in zip(made, (copy, fill, tone, glass, kernels[4][1], kernels[5][1], kernels[6][1], kernels[7][1])):
        if words != want:
            sys.exit('%s: the array %s is not the source\'s words' % (inc, name))
    print('gpu i915 kernels: %s, copy the iga64 assembly of gpu_i915_copy.iga, fill the same as IGT rows 1 to 9, tone, glass, bx and cp the iga64 assembly, all as %s' % (src, inc))


def main():
    if len(sys.argv) == 5 and sys.argv[1] == '--target' and sys.argv[2] == 'intel-gen9':
        intel_check(sys.argv[3], sys.argv[4])
        return
    if len(sys.argv) == 3 and sys.argv[1] == '--print-intel':
        sys.stdout.write(intel_text(intel_kernels(pathlib.Path(sys.argv[2]).read_text())))
        return
    if len(sys.argv) > 3 and sys.argv[1] == '--chips':
        chips(sys.argv[2], sys.argv[3:])
        return
    if len(sys.argv) == 2 and sys.argv[1] == '--probe':
        probe()
        return
    if len(sys.argv) != 3 or sys.argv[1].startswith('-'):
        sys.exit(__doc__)
    asm, inc = sys.argv[1], sys.argv[2]
    if pathlib.Path(asm).name != 'gpu_stress.asm':
        check_regs(asm)
    llvm = kernels(build(asm))
    ours = arrays(pathlib.Path(inc).read_text())
    if [n for n, _ in llvm] != [n for n, _ in ours]:
        sys.exit('%s: the kernels are %s, LLVM has %s' % (inc, [n for n, _ in ours], [n for n, _ in llvm]))
    words = 0
    for (name, theirs), (_, mine) in zip(llvm, ours):
        if len(theirs) != len(mine):
            sys.exit('%s: kernel %s is %d words, LLVM makes %d' % (inc, name, len(mine), len(theirs)))
        bad = [i for i in range(len(mine)) if mine[i] != theirs[i]]
        if bad:
            i = bad[0]
            sys.exit('%s: kernel %s differs from LLVM in %d of %d words, the first at word %d (code word %d): %08x, LLVM %08x'
                     % (inc, name, len(bad), len(mine), i, i - 2, mine[i], theirs[i]))
        words += len(mine)
    print('gpu kernels: %s, %d kernels and %d words, the same as LLVM makes' % (asm, len(llvm), words))


main()
