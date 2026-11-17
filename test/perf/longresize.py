"""longresize.py SECONDS URL: Alt and the right button held on Chromium's corner, the hand going round for SECONDS; every 0.25 s the
compositor's size of the window and Chromium's own (outerWidth/Height through DevTools), and how long Chromium took to answer. A lockup is
Chromium not answering, or its size not following the window's for over a second."""
import sys, time, json, math
sys.path.insert(0, "/tmp/cperf")
import battery as B
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60
url = sys.argv[2] if len(sys.argv) > 2 else "https://dawning.dev"
b = B.Battery()
b.load(url)
_, x, y, w, h = b.win()
p = b.person
import os
edge = os.environ.get("EDGE") == "1"
cx, cy = (x + w + 4, y + h + 4) if edge else (x + w - 40, y + h - 40)
cw0 = b.page.eval("[outerWidth, outerHeight]"); win0 = B.window()
margin = (cw0[0] - win0[3], cw0[1] - win0[4])
b.page.eval("window.__f=0;(function t(){window.__f++;requestAnimationFrame(t)})()")
p.move(cx, cy); time.sleep(0.2)
btn = B.BTN_LEFT if edge else B.BTN_RIGHT
if not edge: p.keyboard.send((B.EV_KEY, B.KEY["alt"], 1)); time.sleep(0.05)
p.button(btn, True); time.sleep(0.05)
t0 = time.time(); last_sample = 0; behind_since = None; worst_behind = 0; worst_answer = 0; stuck = []; samples = 0
while time.time() - t0 < secs:
    t = time.time() - t0
    r = 250 + 200 * math.sin(t * 0.7)
    p.move(cx - 300 + r * math.cos(t * 2.1), cy - 200 + r * 0.6 * math.sin(t * 2.1))
    time.sleep(float(os.environ.get("STEP", "0.0083")))
    if t - last_sample >= 0.25:
        last_sample = t; samples += 1
        q = time.time()
        try:
            cw = b.page.eval("[outerWidth, outerHeight]")
        except Exception as e:
            cw = None
        answer = (time.time() - q) * 1000
        worst_answer = max(worst_answer, answer)
        win = B.window()
        ww = (win[3], win[4]) if win else None
        off = not cw or not ww or abs(cw[0] - margin[0] - ww[0]) > 4 or abs(cw[1] - margin[1] - ww[1]) > 4
        try:
            f = b.page.eval("window.__f")
        except Exception:
            f = None
        if f is not None:
            fps_last = getattr(b, "_f", None); b._f = f
            if fps_last is not None and f - fps_last < 2:
                stuck.append((round(t, 2), "frames in 0.25 s", f - fps_last, ww, cw))
        if off:
            behind_since = behind_since or t
            worst_behind = max(worst_behind, t - behind_since)
            if t - behind_since > 1.0 and (not stuck or stuck[-1][0] != round(behind_since, 2)):
                stuck.append((round(behind_since, 2), ww, cw, round(answer)))
        else:
            behind_since = None
        if answer > 500:
            stuck.append((round(t, 2), "answer ms", round(answer)))
p.button(btn, False)
if not edge: p.keyboard.send((B.EV_KEY, B.KEY["alt"], 0))
time.sleep(1.5)
try:
    cw = b.page.eval("[outerWidth, outerHeight]")
except Exception:
    cw = None
print(json.dumps({"seconds": secs, "samples": samples, "margin": margin, "frames": getattr(b, "_f", None), "worst_behind_s": round(worst_behind, 2), "worst_answer_ms": round(worst_answer),
                  "stuck": stuck[:20], "stuck_count": len(stuck), "after": {"window": B.window(), "chrome": cw},
                  "debug": B.debug_numbers()}), flush=True)
b.pinger.running = False
