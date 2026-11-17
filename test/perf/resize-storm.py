"""resize-storm.py [STEPS] [PX] [PY]: a framecode window resized by moonwater window move while it animates, judged on both scanouts
(cedar, Round 2: the resize-storm bench stage). Each step: resize the window to the next size (moonwater window move PID at X,Y size WxH,
the client follows its configure), wait a random 0.15-0.6 s so the resize lands mid-animation, then dump both scanouts and read the
window's top and bottom code bands in each. A buffer is OK when its band codes are equal and are a frame the client committed in the
dump's time window (a quarter second before the dump to its end). Stale is a step where both buffers are wrong; one buffer wrong is
counted apart (the other is the one on screen or the one not composed into since). A step where another window covers the window is
skipped (covered). Prints one JSON line. Env: BOWL is the bowl (fc-run.sh), MACHINE and GPU name the row, SEED makes the waits repeatable."""
import json, os, random, re, subprocess, sys, time
sys.path.insert(0, "/tmp/cperf")
import battery as B

STEPS = int(sys.argv[1]) if len(sys.argv) > 1 else 24
PX = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
PY = int(sys.argv[3]) if len(sys.argv) > 3 else 800
OUT = "/tmp/cperf/runs/storm"
os.makedirs(OUT, exist_ok=True)
TITLE = "framecode-storm"
SIZES = [(320, 120), (360, 140), (300, 110), (440, 180), (340, 130), (500, 200), (320, 160), (400, 120)]
rnd = random.Random(int(os.environ.get("SEED", "7")))
MACHINE = os.environ.get("MACHINE", "?")
GPU = os.environ.get("GPU", "?")
PATH = os.environ.get("PATH_LABEL", "?")
open(os.path.join(OUT, "fc-timeline.txt"), "w").close()
client = subprocess.Popen([sys.executable, "/tmp/cperf/framecode-keep.py", TITLE, OUT], stdout=subprocess.DEVNULL,
                          stderr=open(os.path.join(OUT, "client.err"), "a"))


def windows():
    out = []
    for line in B.machine("moonwater window").splitlines():
        m = re.search(r"pid (\d+), at (\d+),(\d+), (\d+) by (\d+): (.*)$", line)
        if m:
            out.append({"pid": int(m.group(1)), "x": int(m.group(2)), "y": int(m.group(3)), "w": int(m.group(4)), "h": int(m.group(5)),
                        "title": m.group(6).strip()})
    return out


def frames_between(t0, t1):
    codes = set()
    with open(os.path.join(OUT, "fc-timeline.txt")) as f:
        for ln in f.read().splitlines()[-400:]:
            p = ln.split()
            if len(p) >= 2 and t0 - 0.25 <= float(p[1]) <= t1 + 0.05:
                codes.add(int(p[0]) & 0xffff)
    return codes


def code_at(raw, pitch, x, y):
    code = 0
    for k in range(16):
        o = y * pitch + (x + 4 + k * 18 + 7) * 4
        if raw[o] + raw[o + 1] + raw[o + 2] > 384:
            code |= 1 << k
    return code


def overlaps(a, b):
    return a["x"] < b["x"] + b["w"] and b["x"] < a["x"] + a["w"] and a["y"] < b["y"] + b["h"] and b["y"] < a["y"] + a["h"]


try:
    time.sleep(2.5)
    w = None
    for _ in range(40):
        for x in windows():
            if x["title"] == TITLE:
                w = x
        if w:
            break
        time.sleep(0.25)
    if w is None:
        raise SystemExit("the framecode window is not on the screen")
    pid = w["pid"]
    B.machine("moonwater window move %d at %d,%d" % (pid, PX, PY))
    time.sleep(1.0)
    W = {"screen": None}
    scr = B.machine("moonwater debug 2>/dev/null | grep '  screen 0'")
    m = re.search(r"(\d+)x(\d+)", scr or "")
    pitch = (int(m.group(1)) if m else 1920) * 4
    judged = covered = both_ok = one_bad = both_bad = size_mismatch = 0
    detail = []
    for step in range(STEPS):
        sw, sh = SIZES[step % len(SIZES)]
        B.machine("moonwater window move %d at %d,%d size %dx%d" % (pid, PX, PY, sw, sh))
        time.sleep(rnd.uniform(0.15, 0.6))
        t0 = time.monotonic()
        dump = B.machine("sh /tmp/cperf/scandump.sh %s/s%03d" % (OUT, step), timeout=180)
        t1 = time.monotonic()
        wins = windows()
        cur = [x for x in wins if x["title"] == TITLE]
        if not cur:
            continue
        cw = cur[0]
        if (cw["w"], cw["h"]) != (sw, sh):
            size_mismatch += 1
        if any(o is not cw and overlaps(cw, o) for o in wins):
            covered += 1
            continue
        judged += 1
        seen = frames_between(t0, t1)
        oks = []
        for n in (1, 2):
            fn = os.path.join(OUT, "s%03d-%d.raw" % (step, n))
            if not os.path.exists(fn):
                oks.append(False)
                continue
            raw = open(fn, "rb").read()
            top = code_at(raw, pitch, cw["x"], cw["y"] + 10)
            bot = code_at(raw, pitch, cw["x"], cw["y"] + 42)
            oks.append(top == bot and top in seen)
            os.remove(fn)
        if all(oks):
            both_ok += 1
        elif any(oks):
            one_bad += 1
        else:
            both_bad += 1
        detail.append([step, sw, sh, oks])
    row = {"machine": MACHINE, "gpu": GPU, "path": PATH, "steps": STEPS, "judged": judged, "covered": covered, "both_ok": both_ok,
           "one_buffer_wrong": one_bad, "both_wrong": both_bad, "size_mismatch": size_mismatch}
    print(json.dumps(row), flush=True)
    open(os.path.join(OUT, "last.json"), "w").write(json.dumps(dict(row, detail=detail)))
finally:
    client.terminate()
    try:
        client.wait(timeout=5)
    except Exception:
        client.kill()
