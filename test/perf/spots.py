#!/usr/bin/env python3
"""Points of a film (moonwater canvas film: film.raw) through time: the sum of red, green and blue at each X,Y, one line a frame where any
changed. A shadow laid over itself falls a little every frame; a flash is one frame between two equal ones.

    spots.py FILM.raw X,Y [X,Y...]
"""
import struct, sys
path = sys.argv[1]
spots = [tuple(map(int, s.split(","))) for s in sys.argv[2:]]
with open(path, "rb") as f:
    n = 0; last = None; t0 = None
    while True:
        head = f.read(32)
        if len(head) < 32: break
        magic, seq, _, when, w, h = struct.unpack("<8sIIQII", head)
        px = f.read(w * h * 4)
        t0 = t0 or when
        vals = []
        for x, y in spots:
            i = (y * w + x) * 4
            vals.append(px[i] + px[i+1] + px[i+2])
        if vals != last:
            print("%5d %8.1f ms %s" % (n, (when - t0) / 1e6, " ".join("%4d" % v for v in vals)))
            last = vals
        n += 1
