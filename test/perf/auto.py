#!/usr/bin/env python3
"""Chromium on Canvas driven through DevTools (127.0.0.1:9222), with no person at the machine.

    auto.py   every scenario in turn; one JSON line each, and a summary

Each scenario runs while two things are measured:
  frames  the page's own requestAnimationFrame intervals (p50, p99, the longest gap, gaps over 50 ms)
  stalls  a second connection to the browser's own target asks Browser.getVersion every 100 ms; the longest answer is how long
          the browser's UI thread could not answer anything (the "it locks up for seconds" a person feels)
"""
import json
import sys
import threading
import time
import urllib.parse

sys.path.insert(0, "/tmp/cperf")
from cperf import BrowserCdp, Cdp, MEASURE  # noqa: E402

SITES = [
    "https://en.wikipedia.org/wiki/Main_Page",
    "https://en.wikipedia.org/wiki/Linux",
    "https://news.ycombinator.com/",
    "https://github.com/torvalds/linux",
    "https://developer.mozilla.org/en-US/docs/Web/JavaScript",
]


class Pinger(threading.Thread):
    """The browser's answer time, every 100 ms, on a connection of its own."""

    def __init__(self):
        super().__init__(daemon=True)
        self.cdp = BrowserCdp(9222)
        self.lock = threading.Lock()
        self.reset()
        self.running = True

    def reset(self):
        with getattr(self, "lock", threading.Lock()):
            self.worst = 0.0
            self.over = 0
            self.count = 0

    def run(self):
        while self.running:
            t = time.time()
            try:
                self.cdp.call("Browser.getVersion", timeout=30)
            except Exception:
                pass
            took = (time.time() - t) * 1000
            with self.lock:
                self.count += 1
                self.worst = max(self.worst, took)
                self.over += took > 250
            time.sleep(0.1)

    def take(self):
        with self.lock:
            out = {"answers": self.count, "worst_answer_ms": round(self.worst, 1), "answers_over_250ms": self.over}
        self.reset()
        return out


def frames_begin(cdp, seconds):
    """Starts the page's rAF measure (it resolves after seconds); returns at once."""
    cdp.eval("window.__m = (%s); 0" % (MEASURE % (seconds * 1000)).strip())


def frames_end(cdp):
    r = cdp.eval("window.__m")
    if isinstance(r, dict):
        r = {k: (round(v, 2) if isinstance(v, float) else v) for k, v in r.items() if k not in ("dpr",)}
    return r


def gaps_probe(cdp):
    cdp.eval("window.__gaps=[];(function g(p){requestAnimationFrame(function(n){if(p)window.__gaps.push(n-p);g(n)})})(0);0")


def gaps_take(cdp):
    g = cdp.eval("(()=>{const g=window.__gaps||[];window.__gaps=[];return g})()") or []
    if not isinstance(g, list) or not g:
        return {}
    s = sorted(g)
    return {"frames": len(g), "p50": round(s[len(s) // 2], 1), "p99": round(s[min(len(s) - 1, int(len(s) * 0.99))], 1),
            "longest": round(s[-1], 1), "gaps_over_50ms": sum(1 for x in g if x > 50)}


def load(cdp, url):
    t = time.time()
    cdp.call("Page.navigate", {"url": url}, timeout=60)
    for _ in range(300):
        state = cdp.eval("document.readyState")
        if state == "complete":
            break
        time.sleep(0.1)
    return round((time.time() - t) * 1000)


def wheel(cdp, seconds, delta):
    end = time.time() + seconds
    while time.time() < end:
        cdp.call("Input.dispatchMouseEvent", {"type": "mouseWheel", "x": 600, "y": 400, "deltaX": 0, "deltaY": delta})
        time.sleep(0.016)


def click_link(cdp):
    box = cdp.eval("(()=>{const a=[...document.querySelectorAll('a[href^=\"http\"],a[href^=\"/\"]')].find(e=>{const r=e.getBoundingClientRect();"
                   "return r.width>20&&r.height>8&&r.top>80&&r.bottom<innerHeight-20});if(!a)return null;const r=a.getBoundingClientRect();"
                   "return [r.left+r.width/2,r.top+r.height/2]})()")
    if not isinstance(box, list):
        return None
    t = time.time()
    for kind in ("mouseMoved", "mousePressed", "mouseReleased"):
        cdp.call("Input.dispatchMouseEvent", {"type": kind, "x": box[0], "y": box[1], "button": "left", "clickCount": 1})
    for _ in range(300):
        time.sleep(0.1)
        try:
            if cdp.eval("document.readyState") == "complete" and cdp.eval("performance.now()") < (time.time() - t) * 1000 + 500:
                break
        except Exception:
            pass
    return round((time.time() - t) * 1000)


def main():
    page = Cdp(9222)
    page.call("Page.enable")
    browser = BrowserCdp(9222)
    pinger = Pinger()
    pinger.start()
    results = []

    def done(name, **extra):
        row = {"scenario": name}
        row.update(extra)
        row.update(pinger.take())
        results.append(row)
        print(json.dumps(row), flush=True)

    target = next((t["id"] for t in json.load(__import__("urllib.request").request.urlopen("http://127.0.0.1:9222/json/list", timeout=5))
                   if t.get("type") == "page"), None)
    window = browser.call("Browser.getWindowForTarget", {"targetId": target} if target else {}).get("result", {}).get("windowId")
    if window:
        browser.call("Browser.setWindowBounds", {"windowId": window, "bounds": {"windowState": "normal"}})
        browser.call("Browser.setWindowBounds", {"windowId": window, "bounds": {"left": 40, "top": 40, "width": 1400, "height": 900}})
    pinger.take()

    for url in SITES:
        took = load(page, url)
        done("load " + url, load_ms=took)
        gaps_probe(page)
        wheel(page, 4, 120)
        wheel(page, 2, -240)
        done("scroll " + url, **gaps_take(page))

    load(page, SITES[0])
    gaps_probe(page)
    took = click_link(page)
    done("click a link", load_ms=took, **gaps_take(page))

    if window:
        gaps_probe(page)
        for width in list(range(800, 1801, 100)) + list(range(1700, 799, -100)):
            browser.call("Browser.setWindowBounds", {"windowId": window, "bounds": {"width": width, "height": 600 + width // 3}})
            time.sleep(0.12)
        done("resize 800 to 1800 and back", **gaps_take(page))

    frames_begin(page, 3)
    time.sleep(3.5)
    done("idle 3 s", **(frames_end(page) or {}))

    t = time.time()
    made = browser.call("Target.createTarget", {"url": SITES[2]})
    done("new tab", made=bool(made.get("result")), ms=round((time.time() - t) * 1000))
    time.sleep(2)
    if made.get("result"):
        browser.call("Target.closeTarget", {"targetId": made["result"]["targetId"]})

    pinger.running = False
    worst = max(results, key=lambda r: r.get("worst_answer_ms", 0))
    print(json.dumps({"summary": True, "scenarios": len(results), "worst_stall_ms": worst.get("worst_answer_ms"),
                      "worst_stall_in": worst["scenario"],
                      "frame_gaps_over_50ms": sum(r.get("gaps_over_50ms", 0) for r in results)}), flush=True)


main()
