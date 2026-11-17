"""stale2.py [URL] [ROUNDS]: a page left idle, the screen the compositor composed against what Chromium draws (cedar, 2026-11-17).

    The oracle is Chromium itself: Page.captureScreenshot draws the page's current frame fresh, so it is what the page should show.
    The screen is `moonwater canvas shot` (what the compositor last composed). The page's viewport is compared with the window's
    rectangle at OFFSET below the window's top (the toolbar), minus the places nothing should be compared: the pointer's square and
    the translate bubble's strip. Pixels that differ are stale parts. Env: IDLE seconds (4), OFFSET (87), OUT dir.
    One JSON line per round.
"""
import base64, glob, json, os, struct, sys, time, zlib
sys.path.insert(0, "/tmp/cperf")
import battery as B

URL = sys.argv[1] if len(sys.argv) > 1 else "https://www.google.com/"
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 1
IDLE = float(os.environ.get("IDLE", "4"))
OFFSET = int(os.environ.get("OFFSET", "87"))
OUT = os.environ.get("OUT", "/tmp/cperf/runs/stale2")
TOL = int(os.environ.get("TOL", "24"))  # per channel: anti-aliasing and the compositor's own rounding are under this
os.makedirs(OUT, exist_ok=True)


def png_rgb(data):
    """Decode an 8-bit RGB or RGBA non-interlaced PNG to (w, h, bytes RGB)."""
    pos, idat, w, h, ct = 8, b"", 0, 0, 2
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ct = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and body[12] == 0, "unsupported png"
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b_ = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b_) & 255
            elif f == 3: line[i] = (line[i] + ((a + b_) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b_ - c), abs(a - c), abs(a + b_ - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b_ if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if bpp == 4:
        out = bytes(out[i] for i in range(len(out)) if i % 4 != 3)
    return w, h, bytes(out)


def screen_ppm(data):
    parts, pos = [], 0
    while len(parts) < 4:
        e = data.index(b"\n", pos); parts += data[pos:e].split(); pos = e + 1
    return int(parts[1]), int(parts[2]), data[pos:]


def shot_screen(tag):
    before = set(glob.glob("/run/moonwater/record/*shot*.ppm"))
    B.machine("moonwater canvas shot")
    for _ in range(100):
        new = sorted(set(glob.glob("/run/moonwater/record/*shot*.ppm")) - before)
        if new:
            time.sleep(0.2)
            return screen_ppm(open(new[-1], "rb").read())
        time.sleep(0.05)
    return None


b = B.Battery()
for r in range(ROUNDS):
    b.load(URL)
    pid, x, y, w, h = b.win()
    px, py = x + w // 2, y + h // 2
    b.person.move(px, py)
    time.sleep(1.0)
    b.person.wheel(15, pause=0.02)
    time.sleep(0.8)
    b.person.wheel(-6, pause=0.02)
    time.sleep(IDLE)
    scr = shot_screen("r%d" % r)
    # what Chromium draws now: a fresh frame of the viewport
    metrics = b.page.call("Page.getLayoutMetrics").get("result", {})
    vv = metrics.get("cssVisualViewport", {})
    cap = b.page.call("Page.captureScreenshot", {"format": "png"}, timeout=30).get("result", {})
    if "data" not in cap:
        print(json.dumps({"round": r, "error": str(cap)[:300]}), flush=True)
        continue
    cw, ch, pix = png_rgb(base64.b64decode(cap["data"]))
    sw, sh, spix = scr
    vw, vh = min(cw, w), min(ch, h - OFFSET)
    bad, rows = 0, set()
    mx0, mx1, my0, my1 = px - 12 - x, px + 12 - x, py - 12 - y, py + 12 - y  # pointer square (window-relative)
    for yy in range(vh):
        sy = y + OFFSET + yy
        wy = OFFSET + yy
        if 85 <= wy <= 135:  # translate bubble strip (its own widget)
            continue
        for xx in range(vw):
            if my0 <= wy <= my1 and mx0 <= xx <= mx1:
                continue
            i = (yy * cw + xx) * 3
            j = (sy * sw + x + xx) * 3
            if max(abs(pix[i + k] - spix[j + k]) for k in range(3)) > TOL:
                bad += 1
                rows.add(yy)
    print(json.dumps({"round": r, "url": URL, "window": [x, y, w, h], "offset": OFFSET, "viewport": [cw, ch], "viewport_css": [vv.get("clientWidth"), vv.get("clientHeight")],
                      "stale_px": bad, "rows": [min(rows), max(rows)] if rows else None, "tol": TOL}), flush=True)
    open("%s/cap-%d.png" % (OUT, r), "wb").write(base64.b64decode(cap["data"]))
    with open("%s/scr-%d.ppm" % (OUT, r), "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (sw, sh) + spix)
b.pinger.running = False
