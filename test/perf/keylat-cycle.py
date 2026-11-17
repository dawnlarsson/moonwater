"""keylat-cycle.py [KEYS] [GAP] [URL] (ember: a copy of keylat.py that never types a run of one letter: the keys cycle asdfghjkl;
the brief forbids runs like "eeee" anywhere in Chromium). keylat.py [KEYS] [GAP] [URL]: key to photon in Chromium. A page that is one textarea (40 px monospace, no caret), the window's top
of it filmed (moonwater canvas film), KEYS presses of x GAP seconds apart through uinput, each press's monotonic time kept; then for each,
the first present after it whose picture differs from the one before it. Writes /tmp/cperf/keylat.json."""
import json, os, struct, sys, time
sys.path.insert(0, "/tmp/cperf")
import battery as B
N = int(sys.argv[1]) if len(sys.argv) > 1 else 30
GAP = float(sys.argv[2]) if len(sys.argv) > 2 else 0.2
b = B.Battery()
page = sys.argv[3] if len(sys.argv) > 3 else ("data:text/html,<style>html,body{margin:0;height:100%25}textarea{width:100%25;height:100%25;"
       "font:40px monospace;caret-color:transparent;border:0;outline:0;resize:none;padding:4px}</style><textarea autofocus spellcheck=false></textarea>")
b.load(page)
_, x, y, w, h = b.win()
b.person.click(x + w // 2, y + h // 2)
time.sleep(0.6)
if os.environ.get("OMNI"):
    b.person.chord("ctrl", "l")
    time.sleep(0.4)
fx, fy, fw, fh = x, y + 88, min(w, 840) // 8 * 8, 112
seconds = int(N * GAP + 4)
B.machine("rm -f /run/moonwater/record/film.txt /run/moonwater/record/film.raw /run/moonwater/record/keys.txt; moonwater canvas film %d %d %d %d %d" % (seconds, fx, fy, fw, fh))
B.machine("moonwater canvas keys %d" % seconds)
time.sleep(0.8)
kb = b.person.keyboard
LETTERS = "asdfghjkl"  # ember: no run of one letter; a cycle of distinct letters, typed into a textarea only
times = []
for i in range(N):
    if i and i % 30 == 0 and not os.environ.get("OMNI"):  # never Enter in the omnibox: it would search for what was typed
        kb.send((B.EV_KEY, B.KEY["enter"], 1)); kb.send((B.EV_KEY, B.KEY["enter"], 0)); time.sleep(GAP)
    t = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
    code = B.KEY[LETTERS[i % len(LETTERS)]]
    kb.send((B.EV_KEY, code, 1))
    times.append(t)
    time.sleep(0.012)
    kb.send((B.EV_KEY, code, 0))
    time.sleep(GAP - 0.012)
json.dump({"times": times, "rect": [fx, fy, fw, fh]}, open("/tmp/cperf/keylat.json", "w"))
for _ in range(seconds + 10):
    if os.path.exists("/run/moonwater/record/film.txt"):
        break
    time.sleep(1)
frames = []
with open("/run/moonwater/record/film.raw", "rb") as f:
    while True:
        hd = f.read(32)
        if len(hd) < 32:
            break
        _, seq, flags, when, fw2, fh2 = struct.unpack("<8sIIQII", hd)
        px = f.read(fw2 * fh2 * 4)
        if len(px) < fw2 * fh2 * 4:
            break
        frames.append((when, seq, hash(px), px, fw2, fh2, flags))
def span(a, b, w, h):
    xs = []
    for y in range(0, h, 2):
        ra, rb = a[y * w * 4:(y + 1) * w * 4], b[y * w * 4:(y + 1) * w * 4]
        if ra != rb:
            xs += [x for x in range(0, w, 2) if ra[x * 4:x * 4 + 3] != rb[x * 4:x * 4 + 3]]
    return (min(xs), max(xs)) if xs else None
lat, missing, detail = [], 0, []
t0 = frames[0][0] if frames else 0
for i, fr in enumerate(frames[1:], 1):
    sp = span(frames[i - 1][3], fr[3], fr[4], fr[5])
    detail.append("frame %d at %.1f ms (present %d): x %s  %s front %d picture %d" % (i, (fr[0] - times[0]) / 1e6, fr[1], sp, "lent" if fr[6] >> 31 else "table", (fr[6] >> 30) & 1, fr[6] & 0x3fffffff))
open("/tmp/cperf/keylat.frames", "w").write("\n".join(detail) + "\nkeys at ms: " + " ".join("%.1f" % ((t - times[0]) / 1e6) for t in times) + "\n")
for t in times:
    before = [fr for fr in frames if fr[0] < t]
    base = before[-1][2] if before else None
    hit = next((fr for fr in frames if fr[0] >= t and fr[2] != base), None)
    if hit:
        lat.append((hit[0] - t) / 1e6)
    else:
        missing += 1
lat.sort()
def q(p): return round(lat[min(len(lat) - 1, int(p * len(lat)))], 2) if lat else None
print(json.dumps({"keys": N, "frames": len(frames), "seen": len(lat), "missing": missing, "min_ms": q(0), "median_ms": q(0.5), "p90_ms": q(0.9),
                  "max_ms": q(0.999), "all": [round(v, 2) for v in lat]}), flush=True)
b.pinger.running = False
