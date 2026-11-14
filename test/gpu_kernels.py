#!/usr/bin/env python3
"""Hold the build's assembler for the GPU's kernels to LLVM's.

    python3 test/gpu_kernels.py ASM INC     assemble ASM with llvm-mc and fail unless INC (what
                                            `build gpu-kernels ASM INC` wrote) holds exactly the
                                            same words, kernel by kernel
    python3 test/gpu_kernels.py --print ASM the arrays llvm-mc makes of ASM, in the form the
                                            build writes them
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


def text(made):
    lines = []
    for name, words in made:
        lines.append('static const unsigned int gpu_k_%s[] = {\n' % name)
        for i in range(0, len(words), 10):
            lines.append('    ' + ', '.join('0x%08x' % w for w in words[i:i + 10]) + (',' if i + 10 < len(words) else '};') + '\n')
    return ''.join(lines)


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


def main():
    if len(sys.argv) > 3 and sys.argv[1] == '--chips':
        chips(sys.argv[2], sys.argv[3:])
        return
    if len(sys.argv) == 2 and sys.argv[1] == '--probe':
        probe()
        return
    if len(sys.argv) == 3 and sys.argv[1] == '--check-regs':
        check_regs(sys.argv[2])
        return
    if len(sys.argv) == 3 and sys.argv[1] == '--print':
        sys.stdout.write(text(kernels(build(sys.argv[2]))))
        return
    if len(sys.argv) != 3:
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
