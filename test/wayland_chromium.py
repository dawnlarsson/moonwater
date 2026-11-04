#!/usr/bin/env python3
"""
Real Chromium on the Wayland shim, a setting changed while it runs, and what the browser does about it, asked through its
DevTools port (standard library only: a websocket client of a few lines).

    python3 wayland_chromium.py scale PORT SCAN WIDTH HEIGHT DISPLAYS
    python3 wayland_chromium.py theme PORT SETTINGS_DIRECTORY BROWSER_LOG

scale: the headless compositor (SCAN is its screen file, WIDTH by HEIGHT, DISPLAYS /root/canvas.displays) is set to 100,
150, 200, 125 and 100 percent in turn, and the page says each time what the browser sees: window.devicePixelRatio, the
screen's size in CSS pixels (the logical size), and whether the page fits the screen. The scale of a screen is a live setting
of the machine; a browser that does not follow it is the bug this holds.

theme: with the session bus running (sessionbus, DBUS_SESSION_BUS_ADDRESS in the browser's environment), the theme of the machine
(SETTINGS_DIRECTORY/canvas.theme) is set to dark, light and dark again, and the page asks matchMedia('(prefers-color-scheme:
dark)') each time: the browser reads the org.freedesktop.appearance color-scheme from the Settings portal and follows its
SettingChanged. BROWSER_LOG is what the browser said, which holds no complaint of a bus that is not there.

One line a row: "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures. test/wayland_trial.sh runs it with
the browser in a private root (a distribution's Chromium; Canvas's own is Alpine's, run by `chrome` in a bowl).
"""
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.request


class Cdp:
    """A Chrome DevTools client: the first page's websocket, Runtime.evaluate and Page.navigate."""

    def __init__(self, port, timeout=90):
        deadline = time.time() + timeout
        info = None
        while time.time() < deadline and not info:
            try:
                pages = [t for t in json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % port, timeout=2))
                         if t.get("type") == "page"]
                info = pages[0] if pages else None
            except Exception:
                pass
            if not info:
                time.sleep(0.5)
        if not info:
            raise SystemExit("no devtools page on port %d" % port)
        host, _, path = info["webSocketDebuggerUrl"][len("ws://"):].partition("/")
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

    def _read(self, count):
        while len(self.pending) < count:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise SystemExit("devtools closed")
            self.pending += chunk
        out, self.pending = self.pending[:count], self.pending[count:]
        return out

    def _frame(self):
        head = self._read(2)
        length = head[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", self._read(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self._read(8))[0]
        return head[0] & 0x0F, self._read(length)

    def call(self, method, params=None, timeout=30):
        ident = self.next
        self.next += 1
        data = json.dumps({"id": ident, "method": method, "params": params or {}}).encode()
        mask = os.urandom(4)
        head = bytes([0x81])
        if len(data) < 126:
            head += bytes([0x80 | len(data)])
        elif len(data) < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", len(data))
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", len(data))
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
        self.sock.settimeout(timeout)
        while True:
            opcode, payload = self._frame()
            if opcode != 1:
                continue
            message = json.loads(payload)
            if message.get("id") == ident:
                return message

    def eval(self, expression):
        result = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        value = result.get("result", {}).get("result", {})
        return value.get("value", value)


failures = 0


def row(ok, name, detail=""):
    global failures
    if ok:
        print("ok " + name)
    else:
        print("FAIL %s %s" % (name, detail))
        failures += 1
    sys.stdout.flush()


PAGE = ("data:text/html,<body style='margin:0;background:%23fff'><script>window.log=[];"
        "addEventListener('resize',()=>log.push([innerWidth,innerHeight,devicePixelRatio]))</script>")
STATE = """JSON.stringify({dpr: devicePixelRatio, inner: [innerWidth, innerHeight], outer: [outerWidth, outerHeight],
  screen: [screen.width, screen.height], resizes: (window.log || []).length})"""


def state(cdp):
    return json.loads(cdp.eval(STATE))


def scale(port, width, height, displays):
    cdp = Cdp(port)
    cdp.call("Page.navigate", {"url": PAGE})
    time.sleep(3)
    first = state(cdp)
    row(first["dpr"] == 1 and first["screen"] == [width, height], "at 100 percent the browser sees a device pixel ratio of 1 and a screen of %dx%d" % (width, height),
        json.dumps(first))
    for percent in (150, 200, 125, 100):
        with open(displays, "w") as f:
            f.write("Canvas scale=%d\n" % percent)
        end = time.time() + 12
        now = state(cdp)
        while time.time() < end and now["dpr"] != percent / 100.0:
            time.sleep(0.5)
            now = state(cdp)
        logical = [width * 100 // percent, height * 100 // percent]
        row(now["dpr"] == percent / 100.0, "at %d percent the browser's device pixel ratio becomes %s, with no restart" % (percent, percent / 100.0), json.dumps(now))
        row(abs(now["screen"][0] - logical[0]) <= 1 and abs(now["screen"][1] - logical[1]) <= 1,
            "and its screen is the logical size, %dx%d" % tuple(logical), json.dumps(now))
        # The window keeps its size in CSS pixels, so it is larger in pixels at a larger scale: it is asked to fit the screen (the page
        # of it; outerWidth counts the shadow the browser draws round its window, which the compositor leaves out of the window).
        end = time.time() + 8
        while time.time() < end and (now["inner"][0] * now["dpr"] > width + 1 or now["inner"][1] * now["dpr"] > height + 1):
            time.sleep(0.5)
            now = state(cdp)
        row(now["inner"][0] * now["dpr"] <= width + 1 and now["inner"][1] * now["dpr"] <= height + 1,
            "and the page fits the screen, %dx%d pixels or less" % (width, height), json.dumps(now))


def dark(cdp):
    return cdp.eval("matchMedia('(prefers-color-scheme: dark)').matches")


def theme(port, root, log):
    cdp = Cdp(port)
    cdp.call("Page.navigate", {"url": "about:blank"})
    time.sleep(2)
    for word, want in (("dark", True), ("light", False), ("dark", True), ("light", False)):
        with open(os.path.join(root, "canvas.theme"), "w") as f:
            f.write(word + "\n")
        end = time.time() + 12
        now = dark(cdp)
        while time.time() < end and now != want:
            time.sleep(0.4)
            now = dark(cdp)
        row(now == want, "with the theme %s the browser's prefers-color-scheme: dark is %s, with no restart" % (word, str(want).lower()), str(now))
    text = open(log, errors="replace").read() if os.path.exists(log) else ""
    # The system bus (UPower, logind) is not Moonwater's to give; the session bus is, and it is there.
    errors = [line for line in text.splitlines() if "Failed to connect to the bus" in line and "system_bus_socket" not in line]
    row(not errors, "and the browser's log has no complaint of a session bus that is not there", "%d lines, the first: %s" % (len(errors), errors[:1]))


def chooser(port, trace):
    """A click on a file input: the browser opens its file dialog (the toolkit's, not the portal's) and asks the bus for no FileChooser."""
    cdp = Cdp(port)
    cdp.call("Page.navigate", {"url": "data:text/html,<body style='margin:0'><input id=f type=file style='position:fixed;left:0;top:0;width:400px;height:300px'>"})
    time.sleep(2)
    for kind in ("mousePressed", "mouseReleased"):
        cdp.call("Input.dispatchMouseEvent", {"type": kind, "x": 50, "y": 50, "button": "left", "clickCount": 1})
    time.sleep(4)
    text = open(trace, errors="replace").read() if os.path.exists(trace) else ""
    row("FileChooser.OpenFile" not in text, "a click on a file input calls no FileChooser.OpenFile of the portal, which has none", [l for l in text.splitlines() if "FileChooser" in l][:3])


if __name__ == "__main__":
    what = sys.argv[1]
    if what == "scale":
        scale(int(sys.argv[2]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])
    elif what == "chooser":
        chooser(int(sys.argv[2]), sys.argv[3])
    elif what == "theme":
        theme(int(sys.argv[2]), sys.argv[3], sys.argv[4])
    else:
        raise SystemExit("unknown scenario " + what)
    sys.exit(failures)
