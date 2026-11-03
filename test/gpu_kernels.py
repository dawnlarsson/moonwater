#!/usr/bin/env python3
"""Assemble src/canvas/gpu.asm for the GPU and say what src/canvas/gpu.c must hold.

    python3 test/gpu_kernels.py            print the C arrays (one per kernel: rsrc1, rsrc2, then the code)
    python3 test/gpu_kernels.py --check    fail unless src/canvas/gpu.c holds exactly those, between its gpu.asm markers
    python3 test/gpu_kernels.py --write    put them there

Needs llvm-mc and ld.lld with the AMDGPU target. A kernel that needs scratch is refused: the dispatch gives it none.
"""
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASM = ROOT / 'src/canvas/gpu.asm'
SOURCE = ROOT / 'src/canvas/gpu.c'
BEGIN = '// gpu.asm begin: test/gpu_kernels.py --write\n'
END = '// gpu.asm end\n'


def build():
    work = pathlib.Path(tempfile.mkdtemp())
    subprocess.run(['llvm-mc', '-triple=amdgcn-amd-amdhsa', '-mcpu=gfx1036', '-mattr=+wavefrontsize64,-wavefrontsize32',
                    '-filetype=obj', str(ASM), '-o', str(work / 'k.o')], check=True)
    subprocess.run(['ld.lld', '-shared', str(work / 'k.o'), '-o', str(work / 'k.so')], check=True)
    return (work / 'k.so').read_bytes()


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
    for name in re.findall(r'^\.amdhsa_kernel\s+(\w+)|^kernel\s+(\w+),', ASM.read_text(), re.M):
        name = name[0] or name[1]
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


def text():
    lines = []
    for name, words in kernels(build()):
        lines.append('static const unsigned int gpu_k_%s[] = {\n' % name)
        for i in range(0, len(words), 10):
            lines.append('    ' + ', '.join('0x%08x' % w for w in words[i:i + 10]) + (',' if i + 10 < len(words) else '};') + '\n')
    return ''.join(lines)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else '--print'
    block = BEGIN + text() + END
    if mode == '--print':
        sys.stdout.write(block)
        return
    source = SOURCE.read_text()
    a, b = source.find(BEGIN), source.find(END)
    if a < 0 or b < 0:
        sys.exit('src/canvas/gpu.c has no gpu.asm markers')
    held = source[a:b + len(END)]
    if mode == '--check':
        if held != block:
            sys.exit('src/canvas/gpu.c does not hold what src/canvas/gpu.asm assembles to: python3 test/gpu_kernels.py --write')
        print('gpu kernels: %d, gpu.c holds what gpu.asm assembles to' % block.count('static const'))
    elif mode == '--write':
        SOURCE.write_text(source[:a] + block + source[b + len(END):])
    else:
        sys.exit(__doc__)


main()
