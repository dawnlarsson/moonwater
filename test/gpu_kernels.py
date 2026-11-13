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

Where LLVM's versions differ. An instruction in the three-source encoding that takes two sources
has a field for the third that the chip does not read: the build's assembler writes the constant 0
there (0x80), as the clang that made the kernels in src/canvas/gpu.c did, and LLVM 18 to 21 write
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


def build(asm):
    work = pathlib.Path(tempfile.mkdtemp())
    subprocess.run([tool('llvm-mc')] + MC + ['-filetype=obj', str(asm), '-o', str(work / 'k.o')], check=True)
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


def main():
    if len(sys.argv) == 2 and sys.argv[1] == '--probe':
        probe()
        return
    if len(sys.argv) == 3 and sys.argv[1] == '--print':
        sys.stdout.write(text(kernels(build(sys.argv[2]))))
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    asm, inc = sys.argv[1], sys.argv[2]
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
