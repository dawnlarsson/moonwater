#!/usr/bin/env python3
"""Which parts of a (sparse) file are resident: mmap a window at a time read-only, mincore it, sum. Touches no page.
    resident.py PATH [WINDOW_MB]"""
import ctypes, ctypes.util, mmap, os, sys

libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
libc.mmap.restype = ctypes.c_void_p
libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
libc.mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
path = sys.argv[1]
win = int(sys.argv[2] if len(sys.argv) > 2 else 1024) << 20
fd = os.open(path, os.O_RDONLY)
size = os.fstat(fd).st_size
page = 4096
vec = (ctypes.c_ubyte * (win // page))()
runs = []  # (start, end, pages)
total = 0
at = 0
while at < size:
    n = min(win, size - at)
    p = libc.mmap(None, n, mmap.PROT_READ, mmap.MAP_SHARED, fd, at)
    if p in (None, ctypes.c_void_p(-1).value):
        print("mmap failed at", at, ctypes.get_errno()); break
    if libc.mincore(p, n, vec) == 0:
        got = sum(1 for i in range((n + page - 1) // page) if vec[i] & 1)
        if got:
            total += got
            if runs and runs[-1][1] == at:
                runs[-1] = (runs[-1][0], at + n, runs[-1][2] + got)
            else:
                runs.append((at, at + n, got))
    libc.munmap(p, n)
    at += n
print("file %d bytes, resident %.1f MiB in %d runs" % (size, total * page / 2**20, len(runs)))
for s, e, g in runs[:40]:
    print("  [%#x, %#x) %.1f MiB resident" % (s, e, g * page / 2**20))
