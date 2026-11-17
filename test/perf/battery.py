#!/usr/bin/env python3
"""Chromium hammered whole on a live Canvas: its web content and its own interface, through the compositor as a person drives it.

    battery.py [ROUNDS]        run by battery.sh on the machine (python3 of a bowl, root): one JSON line a scenario, then a summary

Input is real: a keyboard and a tablet (an absolute pointer with buttons and a wheel) made with /dev/uinput, so every key, click,
wheel notch and drag goes through the compositor's input, its focus and its Wayland seat, as a person's would: Chromium's tab strip,
omnibox and menus get them as they get a person's. DevTools (127.0.0.1:9222) only watches: the page's frames, and the browser's answer
time, asked every 100 ms on a connection of its own (the longest wait is how long the browser's UI could not answer anything).

The compositor's side is asked through battery.sh, which runs on the machine outside the bowl: the windows (moonwater window), a window
sized as the compositor sizes it (moonwater window move PID size WxH), and moonwater debug before and after each scenario (presents,
late presents, missed vblanks, client frames read in place and copied).

Scenarios: sites loaded, a page scrolled by the wheel, links clicked, tabs opened by Ctrl+T with an address typed, tabs cycled with
Ctrl+Tab, the omnibox typed into, the menu opened and closed, a tab closed and opened again (Ctrl+W, Ctrl+Shift+T), reloads, the window's
corner dragged by the pointer (the compositor's own resize of a held edge), the window sized in steps by the compositor, and idle.
Each row: its time, the browser's worst answer and how many answers took over 250 ms, the page's frame gaps where a page of ours is shown,
the compositor's presents a second and late ones, and the processor time of the compositor and of Chromium over the scenario.
"""
import base64
import fcntl
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.request

sys.path.insert(0, "/tmp/cperf")
from wayland_chromium import Cdp  # noqa: E402

PORT = 9222
CPERF = "/tmp/cperf"
SITES = ["https://en.wikipedia.org/wiki/Linux", "https://www.lingscars.com/", "https://www.bbc.com/news", "https://www.theguardian.com/international",
         "https://github.com/torvalds/linux", "https://news.ycombinator.com/", "https://developer.mozilla.org/en-US/docs/Web/JavaScript",
         "https://www.cnn.com/", "https://unsplash.com/", "https://en.wikipedia.org/wiki/Moon"]
# A video that plays without an account: Big Buck Bunny on YouTube.
VIDEO = "https://www.youtube.com/watch?v=aqz-KE-bpKQ"

# ---- input: a keyboard and a tablet made with uinput ----
EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE = 0x110, 0x111, 0x112
REL_WHEEL = 8
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_RELBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045566, 0x40045567
UI_DEV_SETUP, UI_ABS_SETUP, UI_DEV_CREATE, UI_DEV_DESTROY = 0x405C5503, 0x401C5504, 0x5501, 0x5502
ABS_RANGE = 32767
KEY = {"esc": 1, "backspace": 14, "tab": 15, "enter": 28, "ctrl": 29, "shift": 42, "alt": 56, "space": 57, "f5": 63, "f6": 64,
       "-": 12, "=": 13, "/": 53, ".": 52, ";": 39, "'": 40, ",": 51, "down": 108, "up": 103, "pagedown": 109, "pageup": 104}
for i, c in enumerate("1234567890"):
    KEY[c] = 2 + i
for row, first in (("qwertyuiop", 16), ("asdfghjkl", 30), ("zxcvbnm", 44)):
    for i, c in enumerate(row):
        KEY[c] = first + i


class Uinput:
    """One input device made with uinput. A bowl's /dev/uinput is /dev/null (a bowl injects no input), so battery.sh opens the real one
    outside it and hands the descriptors in as 7 and 8: each device takes one."""

    handed = [7, 8]

    def __init__(self, name, keys, rel=(), absolute=False):
        self.fd = Uinput.handed.pop(0)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        for k in keys:
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, k)
        if rel:
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_REL)
            for r in rel:
                fcntl.ioctl(self.fd, UI_SET_RELBIT, r)
        if absolute:
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
            for axis in (0, 1):
                fcntl.ioctl(self.fd, UI_SET_ABSBIT, axis)
                fcntl.ioctl(self.fd, UI_ABS_SETUP, struct.pack("HxxIiiiii", axis, 0, 0, ABS_RANGE, 0, 0, 0))
        fcntl.ioctl(self.fd, UI_DEV_SETUP, struct.pack("HHHH80sI", 3, 0x1209, 0x6d77 + len(name), 1, name.encode(), 0))
        fcntl.ioctl(self.fd, UI_DEV_CREATE)

    def send(self, *events):
        now = time.time()
        sec, usec = int(now), int((now - int(now)) * 1e6)
        out = b"".join(struct.pack("qqHHi", sec, usec, t, c, v) for t, c, v in events) + struct.pack("qqHHi", sec, usec, EV_SYN, 0, 0)
        os.write(self.fd, out)

    def close(self):
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        finally:
            os.close(self.fd)


class Person:
    """What a person does with a keyboard and a pointer, at the screen's pixels."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.keyboard = Uinput("battery keyboard", sorted(set(KEY.values())))
        self.pointer = Uinput("battery tablet", [BTN_LEFT, BTN_RIGHT, BTN_MIDDLE], rel=[REL_WHEEL], absolute=True)
        self.x = self.y = 0
        time.sleep(1.5)  # the compositor's hotplug watch opens the two devices

    def move(self, x, y):
        self.x, self.y = int(x), int(y)
        self.pointer.send((EV_ABS, 0, self.x * ABS_RANGE // max(1, self.width - 1)), (EV_ABS, 1, self.y * ABS_RANGE // max(1, self.height - 1)))

    def glide(self, x, y, seconds=0.4, steps=24):
        x0, y0 = self.x, self.y
        for i in range(1, steps + 1):
            self.move(x0 + (x - x0) * i / steps, y0 + (y - y0) * i / steps)
            time.sleep(seconds / steps)

    def button(self, code, down):
        self.pointer.send((EV_KEY, code, 1 if down else 0))

    def click(self, x, y, code=BTN_LEFT):
        self.move(x, y)
        time.sleep(0.03)
        self.button(code, True)
        time.sleep(0.05)
        self.button(code, False)

    def wheel(self, notches, pause=0.016):
        for _ in range(abs(notches)):
            self.pointer.send((EV_REL, REL_WHEEL, -1 if notches > 0 else 1))
            time.sleep(pause)

    def chord(self, *names):
        codes = [KEY[n] for n in names]
        for c in codes:
            self.keyboard.send((EV_KEY, c, 1))
        time.sleep(0.02)
        for c in reversed(codes):
            self.keyboard.send((EV_KEY, c, 0))
        time.sleep(0.03)

    def type(self, text, pause=0.02):
        for ch in text:
            if ch == ":":
                self.chord("shift", ";")
            elif ch == " ":
                self.chord("space")
            elif ch in KEY:
                self.chord(ch)
            elif ch.lower() in KEY:
                self.chord("shift", ch.lower())
            time.sleep(pause)

    def close(self):
        self.keyboard.close()
        self.pointer.close()


# ---- the machine's side, through battery.sh ----
def machine(command, timeout=20):
    """A shell command run by battery.sh outside the bowl; its output."""
    name = "%s/cmd.%d.%d" % (CPERF, os.getpid(), int(time.time() * 1e6))
    with open(name + ".tmp", "w") as f:
        f.write(command + "\n")
    os.rename(name + ".tmp", name + ".sh")
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(name + ".done"):
            with open(name + ".done") as f:
                out = f.read()
            os.unlink(name + ".done")
            return out
        time.sleep(0.05)
    return ""


def chromium_pids():
    out = set()
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                with open("/proc/%s/comm" % d) as f:
                    if f.read().strip() == "chromium":
                        out.add(int(d))
            except OSError:
                pass
    return out


def window():
    """Chromium's window as the compositor has it: (pid, x, y, w, h), or None."""
    pids = chromium_pids()
    for line in machine("moonwater window").splitlines():
        try:
            pid = int(line.split("pid ")[1].split(",")[0])
            at = line.split(" at ")[1].split(",")
            x, y = int(at[0]), int(at[1])
            w, h = (int(v) for v in at[2].split(":")[0].strip().split(" by "))
        except (IndexError, ValueError):
            continue
        if pid in pids:
            return pid, x, y, w, h
    return None


def debug_numbers():
    """The compositor's counters this scenario is measured by, from moonwater debug."""
    text = machine("moonwater debug")
    out = {}
    for line in text.splitlines():
        words = line.split()
        if line.startswith("  presents ") and "late a second" in line:
            out["late_per_s"] = int(words[1])
            out["missed_per_s"] = int(words[5])
        elif line.startswith("  metrics ") and "presents a second" in line:
            out["presents_per_s"] = int(words[1])
        elif line.startswith("  client frames "):
            out["read_in_place"] = int(words[2])
            out["copied"] = int(words[6])
        elif line.startswith("  gpu ") and "the GPU composed" in line:
            out["gpu_presents"] = int(words[4])
            out["cpu_presents"] = int(words[8])
    return out


def ticks(pids):
    total = 0
    for p in pids:
        try:
            with open("/proc/%d/stat" % p) as f:
                fields = f.read().rsplit(")", 1)[1].split()
            total += int(fields[11]) + int(fields[12])
        except OSError:
            pass
    return total


def compositor_pid():
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                with open("/proc/%s/comm" % d) as f:
                    if f.read().strip() == "compositor":
                        with open("/proc/%s/stat" % d) as g:
                            if g.read().rsplit(")", 1)[1].split()[0] != "Z":
                                return int(d)
            except OSError:
                pass
    return None


# ---- DevTools: watching only ----
class Target(Cdp):
    """A DevTools connection to one target's websocket."""

    def __init__(self, url):
        host, _, path = url[len("ws://"):].partition("/")
        address, _, number = host.partition(":")
        self.sock = socket.create_connection((address, int(number)), timeout=30)
        self.sock.sendall(("GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                           % (path, host, base64.b64encode(os.urandom(16)).decode())).encode())
        seen = b""
        while b"\r\n\r\n" not in seen:
            seen += self.sock.recv(1)
        self.next = 1
        self.pending = b""


def targets():
    return [t for t in json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % PORT, timeout=5)) if t.get("type") == "page"]


def browser():
    return Target(json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/version" % PORT, timeout=5))["webSocketDebuggerUrl"])


class Pinger(threading.Thread):
    """The browser's answer time, every 100 ms, on a connection of its own."""

    def __init__(self):
        super().__init__(daemon=True)
        self.cdp = browser()
        self.lock = threading.Lock()
        self.worst, self.over, self.count, self.failed = 0.0, 0, 0, 0
        self.running = True

    def run(self):
        while self.running:
            t = time.time()
            try:
                self.cdp.call("Browser.getVersion", timeout=30)
            except Exception:
                with self.lock:
                    self.failed += 1
                try:
                    self.cdp = browser()
                except Exception:
                    time.sleep(1)
            took = (time.time() - t) * 1000
            with self.lock:
                self.count += 1
                self.worst = max(self.worst, took)
                self.over += took > 250
            time.sleep(0.1)

    def take(self):
        with self.lock:
            out = {"answers": self.count, "worst_answer_ms": round(self.worst, 1), "answers_over_250ms": self.over}
            if self.failed:
                out["answers_failed"] = self.failed
            self.worst, self.over, self.count, self.failed = 0.0, 0, 0, 0
        return out


GAPS_ON = "window.__gaps=[];(function g(p){requestAnimationFrame(function(n){if(p)window.__gaps.push(n-p);g(n)})})(0);0"
GAPS_TAKE = "(()=>{const g=window.__gaps||[];window.__gaps=[];return g})()"


def gaps(values):
    if not isinstance(values, list) or not values:
        return {}
    s = sorted(values)
    return {"frames": len(s), "gap_p50": round(s[len(s) // 2], 1), "gap_p99": round(s[min(len(s) - 1, int(len(s) * 0.99))], 1),
            "gap_max": round(s[-1], 1), "gaps_over_50ms": sum(1 for x in s if x > 50)}


class Battery:
    def __init__(self):
        self.results = []
        self.page = Target(targets()[0]["webSocketDebuggerUrl"])
        self.page.call("Page.enable")
        out = machine("moonwater debug")
        size = [l for l in out.splitlines() if l.startswith("  screen 0")]
        self.width, self.height = 1920, 1080
        if size:
            try:
                wh = size[0].split(", ")[1].split(" at ")[0]
                self.width, self.height = (int(v) for v in wh.split("x"))
            except (IndexError, ValueError):
                pass
        self.person = Person(self.width, self.height)
        self.pinger = Pinger()
        self.pinger.start()

    def win(self):
        w = window()
        if not w:
            raise SystemExit("no Chromium window on the screen")
        return w

    def focus(self):
        """A click on the window's title strip gives it the keys (the strip's empty part, right of the tabs)."""
        _, x, y, w, h = self.win()
        self.person.click(x + w * 3 // 4, y + 12)
        time.sleep(0.3)

    def scenario(self, name, body, measure_page=False):
        comp = compositor_pid()
        pids = chromium_pids()
        before = debug_numbers()
        self.pinger.take()
        if measure_page:
            try:
                self.page.eval(GAPS_ON)
            except Exception:
                pass
        c0, k0, t0 = ticks([comp] if comp else []), ticks(pids), time.time()
        extra = body() or {}
        took = time.time() - t0
        c1, k1 = ticks([comp] if comp else []), ticks(pids | chromium_pids())
        row = {"scenario": name, "seconds": round(took, 2)}
        row.update(extra)
        if measure_page:
            try:
                row.update(gaps(self.page.eval(GAPS_TAKE)))
            except Exception as e:
                row["page"] = "no answer: %s" % type(e).__name__
        row.update(self.pinger.take())
        after = debug_numbers()
        hz = os.sysconf("SC_CLK_TCK")
        row["compositor_cpu_pct"] = round(100.0 * (c1 - c0) / hz / max(took, 0.001), 1)
        row["chromium_cpu_pct"] = round(100.0 * (k1 - k0) / hz / max(took, 0.001), 1)
        for key in ("presents_per_s", "late_per_s", "missed_per_s"):
            if key in after:
                row[key] = after[key]
        for key in ("read_in_place", "copied", "gpu_presents", "cpu_presents"):
            if key in after and key in before:
                row[key] = after[key] - before[key]
        self.results.append(row)
        print(json.dumps(row), flush=True)

    # -- the scenarios --
    def load(self, url):
        t = time.time()
        self.page.call("Page.navigate", {"url": url}, timeout=60)
        for _ in range(300):
            try:
                if self.page.eval("document.readyState") == "complete":
                    break
            except Exception:
                pass
            time.sleep(0.1)
        return {"load_ms": round((time.time() - t) * 1000)}

    def wheel_scroll(self):
        _, x, y, w, h = self.win()
        self.person.move(x + w // 2, y + h // 2)
        before = self.page.eval("scrollY")
        self.person.wheel(40)
        time.sleep(0.2)
        self.person.wheel(-20)
        time.sleep(0.3)
        return {"scrolled_px": (self.page.eval("scrollY") or 0) - (before or 0)}

    def click_links(self, count=3):
        done = 0
        for _ in range(count):
            box = self.page.eval("(()=>{const a=[...document.querySelectorAll('a[href^=\"http\"],a[href^=\"/\"]')].filter(e=>{const r=e.getBoundingClientRect();"
                                 "return r.width>20&&r.height>8&&r.top>60&&r.bottom<innerHeight-20&&r.left>10});if(!a.length)return null;"
                                 "const e=a[Math.floor(a.length/3)];const r=e.getBoundingClientRect();"
                                 "return [r.left+r.width/2,r.top+r.height/2,outerWidth-innerWidth,outerHeight-innerHeight]})()")
            if not isinstance(box, list):
                break
            _, x, y, w, h = self.win()
            # The page's place in the window: its top is below Chromium's own strip (the window's height less the page's).
            px, py = x + box[0], y + (h - self.page.eval("innerHeight")) + box[1]
            url = self.page.eval("location.href")
            self.page.eval("window.__press=null;addEventListener('mousedown',e=>{window.__press=[e.clientX,e.clientY]},{once:true,capture:true});0")
            self.person.click(px, py)
            time.sleep(0.1)
            landed = self.page.eval("window.__press")
            if landed is not None and done == 0:
                print(json.dumps({"note": "click aimed at page %d,%d, landed at %s" % (box[0], box[1], landed)}), file=sys.stderr, flush=True)
            for _ in range(100):
                time.sleep(0.1)
                try:
                    if self.page.eval("location.href") != url and self.page.eval("document.readyState") == "complete":
                        done += 1
                        break
                except Exception:
                    pass
        return {"links_followed": done}

    def new_tabs(self, count=6):
        self.focus()
        made = len(targets())
        for i in range(count):
            self.person.chord("ctrl", "t")
            time.sleep(0.4)
            self.person.type(SITES[i % len(SITES)].replace("https://", ""))
            self.person.chord("enter")
            time.sleep(1.2)
        time.sleep(2)
        return {"tabs_opened": len(targets()) - made}

    def cycle_tabs(self, rounds=30):
        self.focus()
        for _ in range(rounds):
            self.person.chord("ctrl", "tab")
            time.sleep(0.08)
        for _ in range(rounds // 2):
            self.person.chord("ctrl", "shift", "tab")
            time.sleep(0.08)
        return {"switches": rounds + rounds // 2}

    def omnibox(self):
        self.focus()
        self.person.chord("ctrl", "l")
        time.sleep(0.2)
        self.person.type("the quick brown fox jumps over the lazy dog moonwater wayland compositor", pause=0.03)
        time.sleep(0.4)
        self.person.chord("esc")
        self.person.chord("esc")
        return {}

    def menus(self, rounds=6):
        self.focus()
        for _ in range(rounds):
            self.person.chord("alt", "f")
            time.sleep(0.5)
            self.person.chord("down")
            self.person.chord("down")
            time.sleep(0.2)
            self.person.chord("esc")
            time.sleep(0.3)
        return {}

    def close_reopen(self, rounds=4):
        self.focus()
        for _ in range(rounds):
            self.person.chord("ctrl", "w")
            time.sleep(0.5)
            self.person.chord("ctrl", "shift", "t")
            time.sleep(1.0)
        return {"tabs": len(targets())}

    def reloads(self, rounds=4):
        self.focus()
        for _ in range(rounds):
            self.person.chord("f5")
            time.sleep(1.5)
        return {}

    def drag_resize(self, sweeps=3):
        """The window's bottom right corner held by the pointer and dragged: the compositor's own resize, as a person's."""
        pid, x, y, w, h = self.win()
        corner_x, corner_y = x + w + 6, y + h + 6  # in Chromium's own shadow, past its window: Chromium asks the compositor to resize
        self.person.move(corner_x, corner_y)
        time.sleep(0.2)
        self.person.button(BTN_LEFT, True)
        time.sleep(0.05)
        for _ in range(sweeps):
            self.person.glide(min(self.width - 5, x + w + 500), min(self.height - 60, y + h + 0), seconds=0.8, steps=60)
            self.person.glide(max(x + 500, x + w - 400), max(y + 350, y + h - 350), seconds=0.8, steps=60)
            self.person.glide(corner_x, corner_y, seconds=0.5, steps=30)
        self.person.button(BTN_LEFT, False)
        time.sleep(0.5)
        after = self.win()
        return {"from": "%dx%d" % (w, h), "to": "%dx%d" % (after[3], after[4])}

    def sized_by_compositor(self):
        pid = self.win()[0]
        for size in ("1200x800", "900x650", "1600x950", "1000x700", "1500x900", "1300x850"):
            machine("moonwater window move %d size %s" % (pid, size))
            time.sleep(0.35)
        return {}

    def video(self, seconds=12):
        """A video played: YouTube's player, its frames through Chromium's video path and the compositor."""
        self.load(VIDEO)
        time.sleep(3)
        self.page.eval("(()=>{const b=[...document.querySelectorAll('button')].find(b=>/accept all|reject all|agree/i.test(b.textContent));if(b)b.click()})();0")
        time.sleep(1)
        self.page.eval("(()=>{const v=document.querySelector('video');if(v){v.muted=true;v.play()}})();0")
        time.sleep(1)
        self.page.eval(GAPS_ON)
        quality = self.page.eval("(()=>{const v=document.querySelector('video');return v?[v.getVideoPlaybackQuality().totalVideoFrames,v.getVideoPlaybackQuality().droppedVideoFrames]:null})()")
        time.sleep(seconds)
        after = self.page.eval("(()=>{const v=document.querySelector('video');return v?[v.getVideoPlaybackQuality().totalVideoFrames,v.getVideoPlaybackQuality().droppedVideoFrames,v.currentTime,v.videoWidth+'x'+v.videoHeight]:null})()")
        out = gaps(self.page.eval(GAPS_TAKE))
        if isinstance(quality, list) and isinstance(after, list):
            out.update({"video_frames": after[0] - quality[0], "video_dropped": after[1] - quality[1], "video_at_s": round(after[2], 1), "video_size": after[3]})
        else:
            out["video"] = "no video element"
        return out

    def video_fullscreen(self, seconds=8):
        """The same video made full screen (f, the player's key) and back: the window's size and a full screen of video frames."""
        _, x, y, w, h = self.win()
        self.person.click(x + w // 2, y + h // 2)  # the player has the keys (and pauses); a second click plays again
        time.sleep(0.3)
        self.person.click(x + w // 2, y + h // 2)
        self.person.chord("f")
        time.sleep(2)
        self.page.eval(GAPS_ON)
        q0 = self.page.eval("(()=>{const v=document.querySelector('video');return v?v.getVideoPlaybackQuality().droppedVideoFrames:null})()")
        time.sleep(seconds)
        q1 = self.page.eval("(()=>{const v=document.querySelector('video');return v?v.getVideoPlaybackQuality().droppedVideoFrames:null})()")
        out = gaps(self.page.eval(GAPS_TAKE))
        out["fullscreen_size"] = self.page.eval("innerWidth+'x'+innerHeight")
        self.person.chord("esc")
        time.sleep(1.5)
        if isinstance(q0, int) and isinstance(q1, int):
            out["video_dropped"] = q1 - q0
        return out

    def idle(self, seconds=3):
        time.sleep(seconds)
        return {}

    def run(self, rounds):
        machine("moonwater window move %d size 1400x900" % self.win()[0])
        time.sleep(1)
        for r in range(rounds):
            tag = " (round %d)" % (r + 1) if rounds > 1 else ""
            for url in SITES:
                self.scenario("load " + url + tag, lambda u=url: self.load(u))
                self.scenario("wheel scroll " + url + tag, self.wheel_scroll, measure_page=True)
            self.scenario("click links" + tag, self.click_links, measure_page=True)
            self.scenario("open tabs with Ctrl+T and typed addresses" + tag, self.new_tabs)
            self.scenario("cycle tabs with Ctrl+Tab" + tag, self.cycle_tabs)
            self.scenario("type into the omnibox" + tag, self.omnibox)
            self.scenario("open and close the menu" + tag, self.menus)
            self.scenario("close and reopen tabs" + tag, self.close_reopen)
            self.scenario("reload" + tag, self.reloads)
            self.scenario("drag the window's corner" + tag, self.drag_resize)
            self.scenario("sized by the compositor" + tag, self.sized_by_compositor)
            self.scenario("play a YouTube video" + tag, self.video)
            self.scenario("YouTube full screen and back" + tag, self.video_fullscreen)
            self.scenario("idle" + tag, self.idle)
        self.pinger.running = False
        worst = max(self.results, key=lambda r: r.get("worst_answer_ms", 0))
        print(json.dumps({"summary": True, "scenarios": len(self.results),
                          "worst_stall_ms": worst.get("worst_answer_ms"), "worst_stall_in": worst["scenario"],
                          "answers_over_250ms": sum(r.get("answers_over_250ms", 0) for r in self.results),
                          "page_gaps_over_50ms": sum(r.get("gaps_over_50ms", 0) for r in self.results),
                          "late_presents_seen": sum(r.get("late_per_s", 0) for r in self.results),
                          "frames_copied": sum(r.get("copied", 0) for r in self.results)}), flush=True)
        self.person.close()


if __name__ == "__main__":
    Battery().run(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
