#!/usr/bin/env python3
"""Pictures and a reading of a film of the compositor's presents (moonwater canvas film SECONDS [X Y W H]: film.raw, every present's
rectangle of the screen as it was composed; src/canvas/record.h says its form).

    film.py FILM.raw OUTDIR [--all] [--scale N] [--sheet COLUMNS]

For every frame against the one before it: how many pixels changed, the box of the change, how far the most changed pixel moved, and
how dark the changed pixels went on average (a shadow laid over itself darkens a little every frame; a frame of black where a picture
should be is many pixels gone to black at once). The frames that changed are written as PNGs (every frame with --all), at 1/N size with
--scale, and contact sheets of them in order, COLUMNS a row (6). One line a frame in OUTDIR/frames.txt, and the frames that look wrong
(black rectangles, a darkening that keeps on, a frame that is between two equal ones: a flash) listed at the end. Standard library only.
"""
import os
import struct
import sys
import zlib

HEAD = 32


def frames(path):
    with open(path, "rb") as f:
        while True:
            head = f.read(HEAD)
            if len(head) < HEAD:
                return
            magic, seq, _, when, w, h = struct.unpack("<8sIIQII", head)
            if magic != b"MWFILM01":
                raise SystemExit("not a film at frame head: %r" % magic)
            pixels = f.read(w * h * 4)
            if len(pixels) < w * h * 4:
                return
            yield seq, when, w, h, pixels


def png(path, w, h, xrgb, scale=1):
    rows = bytearray()
    for y in range(0, h, scale):
        line = xrgb[y * w * 4:(y + 1) * w * 4]
        rgb = bytearray(len(line) // 4 * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = line[2::4], line[1::4], line[0::4]
        if scale > 1:
            rgb = b"".join(rgb[i * 3:i * 3 + 3] for i in range(0, w, scale))
        rows += b"\0" + rgb
    ow, oh = (w + scale - 1) // scale, (h + scale - 1) // scale

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", ow, oh, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


def compare(w, h, a, b):
    """Pixels changed from a to b, their box, the largest change, and the mean change of brightness of the changed ones (negative: darker)."""
    if a is None:
        return w * h, (0, 0, w, h), 255, 0.0
    if a == b:
        return 0, None, 0, 0.0
    changed, most, darker = 0, 0, 0
    x1, y1, x2, y2 = w, h, -1, -1
    row = w * 4
    for y in range(h):
        ra, rb = a[y * row:(y + 1) * row], b[y * row:(y + 1) * row]
        if ra == rb:
            continue
        for x in range(w):
            i = x * 4
            if ra[i:i + 3] != rb[i:i + 3]:
                da = ra[i] + ra[i + 1] + ra[i + 2]
                db = rb[i] + rb[i + 1] + rb[i + 2]
                changed += 1
                most = max(most, abs(db - da) // 3)
                darker += db - da
                x1, y1, x2, y2 = min(x1, x), min(y1, y), max(x2, x), max(y2, y)
    return changed, (x1, y1, x2 - x1 + 1, y2 - y1 + 1), most, darker / 3.0 / max(changed, 1)


def black_box(w, h, pixels, box):
    """How much of the changed box is pure black."""
    x, y, bw, bh = box
    black = total = 0
    for yy in range(y, y + bh, 2):
        for xx in range(x, x + bw, 2):
            i = (yy * w + xx) * 4
            total += 1
            black += pixels[i] < 8 and pixels[i + 1] < 8 and pixels[i + 2] < 8
    return black / max(total, 1)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        raise SystemExit(__doc__)
    path, out = args[0], args[1]
    every = "--all" in args
    scale = int(args[args.index("--scale") + 1]) if "--scale" in args else 1
    columns = int(args[args.index("--sheet") + 1]) if "--sheet" in args else 6
    os.makedirs(out, exist_ok=True)
    report, odd, kept = [], [], []
    before, before2, t0, darkening = None, None, None, 0
    for n, (seq, when, w, h, pixels) in enumerate(frames(path)):
        t0 = when if t0 is None else t0
        changed, box, most, mean = compare(w, h, before, pixels)
        line = "%5d present %d at %8.1f ms: %d px changed" % (n, seq, (when - t0) / 1e6, changed)
        if box:
            line += " in %dx%d at %d,%d, most %d, mean %+.1f" % (box[2], box[3], box[0], box[1], most, mean)
            blacks = black_box(w, h, pixels, box)
            if blacks > 0.3 and changed > 200:
                odd.append("%d: black %.0f%% of a %dx%d change at %d,%d" % (n, blacks * 100, box[2], box[3], box[0], box[1]))
            darkening = darkening + 1 if mean < -0.5 and changed > 50 else 0
            if darkening == 4:
                odd.append("%d: darker four frames running (%+.1f a pixel), in %dx%d at %d,%d: something laid over itself" % (
                    n, mean, box[2], box[3], box[0], box[1]))
        if before2 is not None and before is not None and before2 == pixels and before != pixels:
            odd.append("%d: a flash: frame %d is between two equal ones" % (n, n - 1))
        report.append(line)
        if every or changed:
            name = "%s/frame-%05d.png" % (out, n)
            png(name, w, h, pixels, scale)
            kept.append(name)
        before2, before = before, pixels
    with open(out + "/frames.txt", "w") as f:
        f.write("\n".join(report) + "\n\nlooks wrong:\n" + ("\n".join(odd) if odd else "nothing") + "\n")
    print("%d frames, %d pictures in %s; looks wrong: %d" % (len(report), len(kept), out, len(odd)))
    for line in odd[:30]:
        print("  " + line)


main()
