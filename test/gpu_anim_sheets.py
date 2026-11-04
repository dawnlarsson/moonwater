#!/usr/bin/env python3
# Contact sheets of the animation lane's frames (test/gpu_anim_flicker.sh with ANIM_FRAMES=1), and PNGs of the record's PPM frames.
# Standard library only (no PIL on the box): PNG is written with zlib.
#
#     gpu_anim_sheets.py sheets WORKDIR MODE OUTDIR   one contact sheet per step of MODE (card, cardoff, processor): half-scale frames,
#                                                      the frames sampled in the step's time (and the one before and after), four a row
#     gpu_anim_sheets.py ppm FILE.ppm OUT.png          one record frame as a PNG (full size)
#     gpu_anim_sheets.py crop WORKDIR MODE SEQ X Y W H ZOOM OUT.png   a rectangle of one sampled frame, enlarged (nearest) by ZOOM
#
# WORKDIR/MODE.samples is the sampler's log, WORKDIR/MODE/steps.tsv the steps' times, WORKDIR/MODE.frames/frame-NNNNNN.raw the frames (XRGB,
# little-endian: blue, green, red, unused). The sheets print which frame sequence numbers each sheet holds, so a sheet is read against them.
import os, struct, sys, zlib

W, H = 800, 600


def png(path, width, height, rgb):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def xrgb_to_rgb(raw, width, height):
    rgb = bytearray(width * height * 3)
    rgb[0::3] = raw[2::4]
    rgb[1::3] = raw[1::4]
    rgb[2::3] = raw[0::4]
    return rgb


def half(rgb, width, height):
    out = bytearray()
    for y in range(0, height, 2):
        row = rgb[y * width * 3:(y + 1) * width * 3]
        r, g, b = row[0::3][::2], row[1::3][::2], row[2::3][::2]
        pixels = bytearray(len(r) * 3)
        pixels[0::3], pixels[1::3], pixels[2::3] = r, g, b
        out += pixels
    return out, width // 2, height // 2


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    width, height = map(int, parts[1].split())
    return parts[3][:width * height * 3], width, height


def load_samples(path):
    frames = []
    with open(path, "rb") as f:
        data = f.read()
    at = 0
    while at + 16 <= len(data):
        t, seq, rows = struct.unpack_from("<QII", data, at)
        at += 16 + 8 * rows
        frames.append((t, seq))
    return frames


def sheets(work, mode, out):
    samples = load_samples(os.path.join(work, mode + ".samples"))
    steps = []
    with open(os.path.join(work, mode, "steps.tsv")) as f:
        for line in f:
            label, start, end = line.rstrip("\n").split("\t")
            steps.append((label, int(start), int(end)))
    os.makedirs(out, exist_ok=True)
    for index, (label, start, end) in enumerate(steps):
        picks = [k for k, (t, _) in enumerate(samples) if start <= t <= end]
        if not picks:
            continue
        lo, hi = max(0, picks[0] - 1), min(len(samples) - 1, picks[-1] + 1)
        chosen = list(range(lo, hi + 1))[:16]
        tiles = []
        for k in chosen:
            seq = samples[k][1]
            name = os.path.join(work, mode + ".frames", "frame-%06d.raw" % seq)
            if not os.path.exists(name):
                continue
            with open(name, "rb") as f:
                rgb, w, h = xrgb_to_rgb(f.read(), W, H), W, H
            rgb, w, h = half(rgb, w, h)
            tiles.append((seq, rgb, w, h))
        if not tiles:
            continue
        columns = min(4, len(tiles))
        rows = (len(tiles) + columns - 1) // columns
        tw, th = tiles[0][2], tiles[0][3]
        pad = 4
        sw, sh = columns * (tw + pad) + pad, rows * (th + pad) + pad
        sheet = bytearray(b"\x40\x40\x40" * (sw * sh))
        for i, (seq, rgb, w, h) in enumerate(tiles):
            x0, y0 = pad + (i % columns) * (tw + pad), pad + (i // columns) * (th + pad)
            for y in range(h):
                sheet[((y0 + y) * sw + x0) * 3:((y0 + y) * sw + x0 + w) * 3] = rgb[y * w * 3:(y + 1) * w * 3]
        name = os.path.join(out, "%s-%02d-%s.png" % (mode, index, label.replace(" ", "_")))
        png(name, sw, sh, bytes(sheet))
        print("%s frames %s" % (os.path.basename(name), " ".join(str(t[0]) for t in tiles)))


def ppm(path, out):
    rgb, width, height = read_ppm(path)
    png(out, width, height, bytes(rgb))
    print(out)


def crop(work, mode, seq, x, y, w, h, zoom, out):
    with open(os.path.join(work, mode + ".frames", "frame-%06d.raw" % seq), "rb") as f:
        rgb = xrgb_to_rgb(f.read(), W, H)
    tile = bytearray()
    for yy in range(y, y + h):
        row = rgb[(yy * W + x) * 3:(yy * W + x + w) * 3]
        line = bytearray()
        for xx in range(w):
            line += row[xx * 3:xx * 3 + 3] * zoom
        for _ in range(zoom):
            tile += line
    png(out, w * zoom, h * zoom, bytes(tile))
    print(out)


if __name__ == "__main__":
    if len(sys.argv) == 11 and sys.argv[1] == "crop":
        crop(sys.argv[2], sys.argv[3], *[int(v) for v in sys.argv[4:10]], sys.argv[10])
    elif len(sys.argv) == 5 and sys.argv[1] == "sheets":
        sheets(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 4 and sys.argv[1] == "ppm":
        ppm(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(2)
