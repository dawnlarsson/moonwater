#!/usr/bin/env python3
"""
The stress bench's runner: every scenario against Moonwater's compositor and each reference compositor that starts here,
a side-by-side table, a sorted list of where Moonwater is slower, and the results in a text file and a JSON file beside it.
run.sh builds what it needs and calls this (in a user namespace with a private /run, /root and /tmp/.X11-unix, headless);
run it through run.sh.

    python3 run.py --bin DIR --moonwater COMPOSITOR --work DIR --out FILE [--real] [--seconds S] [--only a,b] [--compositors a,b]
                   [--baseline OLD.json]

Headless, Moonwater is `compositor -H WxH` (its evdev FIFO is the one source of input any compositor here can be given, so
the input rows are Moonwater's alone), weston is --backend=headless --renderer=pixman, kwin_wayland --virtual and mutter
--headless --virtual-monitor, each at the same size with its own runtime directory and session bus. --real is the owner's
machine: a live Moonwater session (/run/moonwater/wayland-0) is measured where it is on its card, and the references are
started on their DRM backends when the card is free (a text console), else nested in the session (said in a NOTE).
"""
import argparse
import json
import os
import platform
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib

W, H = 1920, 1080
TICK = os.sysconf("SC_CLK_TCK")
NOTES = []


def note(text):
    NOTES.append(text)
    print("NOTE " + text, flush=True)


def ns():
    return time.monotonic_ns()


def cpu_ticks(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            fields = f.read().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, IndexError, ValueError):
        return None


def children_named(pid, name):
    out = []
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            with open(f"/proc/{p}/stat") as f:
                text = f.read()
            comm = text[text.index("(") + 1:text.rindex(")")]
            ppid = int(text.rsplit(")", 1)[1].split()[1])
        except (OSError, ValueError):
            continue
        if comm == name and ppid == pid:
            out.append(int(p))
    return out


def parse_result(text):
    res = {}
    for line in text.splitlines():
        if line.startswith("RESULT "):
            for word in line.split()[1:]:
                if "=" in word:
                    k, v = word.split("=", 1)
                    try:
                        res[k] = float(v) if not v.startswith("0x") else int(v, 16)
                    except ValueError:
                        res[k] = v
        elif line.startswith("NOTE "):
            res.setdefault("_notes", []).append(line[5:])
    return res


def wait_path(path, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(path):
            return True
        time.sleep(0.0005)
    return False


def x_sockets():
    try:
        return {n for n in os.listdir("/tmp/.X11-unix") if n.startswith("X")}
    except OSError:
        return set()


# ---- the compositors ----
class Compositor:
    name = "?"

    def __init__(self, args):
        self.args = args
        self.proc = None
        self.pid = None
        self.env = {}
        self.fifo = None
        self.display = None
        self.xproc = None
        self.extra = []
        self.start_ms = None
        self.t_spawn = None
        self.logs = []
        self.work = os.path.join(args.work, self.name)
        os.makedirs(self.work, exist_ok=True)

    def base_env(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith(("WAYLAND_", "DISPLAY", "XDG_RUNTIME_DIR", "DBUS_SESSION"))}
        env["HOME"] = os.path.join(self.work, "home")
        os.makedirs(env["HOME"], exist_ok=True)
        return env

    def runtime(self):
        # Short: a socket's path has 108 bytes. Headless, /run is the namespace's own; on the owner's machine, /tmp.
        rt = os.path.join("/tmp" if self.args.real else "/run", "wls-%s-%d" % (self.name, os.getpid()))
        shutil.rmtree(rt, ignore_errors=True)
        os.makedirs(rt, mode=0o700)
        return rt

    def session_bus(self, rt, env):
        bus = os.path.join(rt, "bus")
        p = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--nopidfile", f"--address=unix:path={bus}"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        self.extra.append(p)
        wait_path(bus, 5)
        env["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={bus}"

    def spawn(self, argv, env, socket_path, log):
        before = x_sockets()
        self.t_spawn = ns()
        self.proc = subprocess.Popen(argv, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT, cwd=self.work)
        self.pid = self.proc.pid
        ok = wait_path(socket_path, 30)
        self.start_ms = (ns() - self.t_spawn) / 1e6
        self.logs.append(log)
        self.x_before = before
        return ok and self.proc.poll() is None

    def find_display(self, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            new = x_sockets() - self.x_before
            if new:
                self.display = ":" + sorted(new)[0][1:]
                return True
            time.sleep(0.01)
        return False

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def x_pids(self):
        pids = [self.xproc.pid] if self.xproc and self.xproc.poll() is None else []
        return pids + (children_named(self.pid, "Xwayland") if self.pid else [])

    def screenshot(self):
        return None  # (width, height, bytes RGBX) or None

    def stop(self):
        for p in ([self.xproc] if self.xproc else []) + ([self.proc] if self.proc else []) + self.extra:
            if p.poll() is None:
                p.terminate()
        for p in ([self.xproc] if self.xproc else []) + ([self.proc] if self.proc else []) + self.extra:
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        self.proc = self.xproc = None
        self.extra = []


class Moonwater(Compositor):
    name = "moonwater"
    SOCK = "/run/moonwater/wayland-0"
    WM = "/run/moonwater/xwm/wm-0"

    def start(self):
        env = self.base_env()
        env.update(XDG_RUNTIME_DIR="/run", WAYLAND_DISPLAY=self.SOCK, MOONWATER_ASKED="1")
        if self.args.real:
            if not os.path.exists(self.SOCK):
                note("moonwater: --real and no live session at /run/moonwater/wayland-0: started on the card")
                return self.start_card(env)
            self.pid = self.live_pid()
            self.env = env
            self.display = os.environ.get("DISPLAY")
            note("moonwater: the live session on its card is measured (pid %s); its input is the person's, so the input rows "
                 "are not injected" % self.pid)
            return True
        bindir = os.path.join(self.work, "bin")
        os.makedirs(bindir, exist_ok=True)
        link = os.path.join(bindir, "compositor")
        if os.path.lexists(link):
            os.unlink(link)
        os.symlink(self.args.moonwater, link)
        os.makedirs("/run/moonwater", exist_ok=True)
        for p in (self.SOCK, self.WM):
            if os.path.exists(p):
                os.unlink(p)
        self.fifo = os.path.join(self.work, "in.fifo")
        if os.path.exists(self.fifo):
            os.unlink(self.fifo)
        os.mkfifo(self.fifo)
        self.fifo_hold = os.open(self.fifo, os.O_RDWR)  # a writer for as long as the compositor reads it
        self.scan = os.path.join(self.work, "scan.img")
        self.env = env
        argv = [link, "-H", f"{W}x{H}", self.scan, "-i", self.fifo, "-l", "us"] + self.args.moonwater_options
        return self.spawn(argv, env, self.SOCK, os.path.join(self.work, "compositor.log"))

    def start_card(self, env):
        link = self.args.moonwater
        self.env = env
        return self.spawn([link], env, self.SOCK, os.path.join(self.work, "compositor.log"))

    def live_pid(self):
        for p in os.listdir("/proc"):
            if p.isdigit():
                try:
                    argv0 = open(f"/proc/{p}/cmdline", "rb").read().split(b"\0")[0]
                except OSError:
                    continue
                if argv0.endswith(b"compositor"):
                    return int(p)
        return None

    def start_x(self):
        """Xwayland as the bowl's wrapper starts it: rootless, its window manager's connection given as -wm."""
        if self.args.real:
            return bool(self.display)
        if not shutil.which("Xwayland") or not os.path.exists(self.WM):
            return False
        wm = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        wm.connect(self.WM)
        fd = wm.detach()
        self.display = ":91"
        env = dict(self.env)
        self.t_xspawn = ns()
        self.xproc = subprocess.Popen(["Xwayland", self.display, "-rootless", "-wm", str(fd), "-nolisten", "tcp", "-noreset"],
                                      pass_fds=[fd], env=env, stdout=open(os.path.join(self.work, "xwayland.log"), "w"),
                                      stderr=subprocess.STDOUT)
        os.close(fd)
        return True

    def screenshot(self):
        if self.args.real:
            return None
        try:
            raw = open(self.scan, "rb").read()
        except OSError:
            return None
        # The scan image is XRGB8888 in memory: B, G, R, X.
        return W, H, raw, (2, 1, 0)

    def stop(self):
        super().stop()
        if getattr(self, "fifo_hold", None) is not None:
            os.close(self.fifo_hold)
            self.fifo_hold = None


class Weston(Compositor):
    name = "weston"

    def start(self):
        exe = shutil.which("weston")
        if not exe:
            note("weston: not installed here: skipped")
            return False
        rt = self.runtime()
        env = self.base_env()
        env["XDG_RUNTIME_DIR"] = rt
        if self.args.real:
            backend = ["--backend=drm"] if not os.path.exists(Moonwater.SOCK) else ["--backend=wayland", "--fullscreen"]
            if backend[0] == "--backend=wayland":
                env["WAYLAND_DISPLAY"] = Moonwater.SOCK
                note("weston: nested in the live Moonwater session (the card is the session's)")
        else:
            backend = ["--backend=headless", "--renderer=pixman", f"--width={W}", f"--height={H}"]
        argv = [exe] + backend + ["--socket=wl-stress", "--xwayland", "--debug", "--idle-time=0"]
        ok = self.spawn(argv, env, os.path.join(rt, "wl-stress"), os.path.join(self.work, "weston.log"))
        self.env = dict(env, WAYLAND_DISPLAY="wl-stress")
        if ok:
            self.find_display(3)
        return ok

    def start_x(self):
        return bool(self.display)

    def screenshot(self):
        tool = shutil.which("weston-screenshooter")
        if not tool:
            return None
        shots = os.path.join(self.work, "shots")
        shutil.rmtree(shots, ignore_errors=True)
        os.makedirs(shots)
        subprocess.run([tool], env=self.env, cwd=shots, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        files = [f for f in os.listdir(shots) if f.endswith(".png")]
        if not files:
            return None
        return png_rgbx(os.path.join(shots, files[0]))


class KWin(Compositor):
    name = "kwin"

    def start(self):
        exe = shutil.which("kwin_wayland")
        if not exe:
            note("kwin: kwin_wayland not installed here: skipped")
            return False
        # kwin_wayland carries file capabilities, which a container's exec refuses: a copy without them runs the same code.
        copy = os.path.join(self.work, "kwin_wayland")
        shutil.copyfile(exe, copy)
        os.chmod(copy, 0o755)
        rt = self.runtime()
        env = self.base_env()
        env["XDG_RUNTIME_DIR"] = rt
        self.session_bus(rt, env)
        if self.args.real:
            if os.path.exists(Moonwater.SOCK):
                env["WAYLAND_DISPLAY"] = Moonwater.SOCK
                backend = []
                note("kwin: nested in the live Moonwater session (the card is the session's)")
            else:
                backend = ["--drm"]
        else:
            backend = ["--virtual", "--width", str(W), "--height", str(H)]
        # Headless, KWin's Xwayland crashes at its start here (glamor with no DRM device) and KWin stops answering its clients,
        # so its X rows are left out with a NOTE; on a card it is asked for.
        xw = ["--xwayland"] if self.args.real else []
        if not self.args.real:
            note("kwin: headless, its Xwayland dies at its start (no DRM device) and KWin then answers no client: its X rows are not run")
        argv = [copy] + backend + ["--socket", "wl-stress"] + xw
        ok = self.spawn(argv, env, os.path.join(rt, "wl-stress"), os.path.join(self.work, "kwin.log"))
        self.env = dict(env, WAYLAND_DISPLAY="wl-stress")
        if ok and xw:
            self.find_display(10)
        return ok

    def start_x(self):
        return bool(self.display)


class Mutter(Compositor):
    name = "mutter"

    def start(self):
        exe = shutil.which("mutter")
        if not exe:
            note("mutter: not installed here: skipped")
            return False
        rt = self.runtime()
        env = self.base_env()
        env["XDG_RUNTIME_DIR"] = rt
        self.session_bus(rt, env)
        if self.args.real:
            if os.path.exists(Moonwater.SOCK):
                env["WAYLAND_DISPLAY"] = Moonwater.SOCK
                backend = ["--nested", "--wayland"]
                note("mutter: nested in the live Moonwater session (the card is the session's)")
            else:
                backend = ["--display-server", "--wayland"]
        else:
            backend = ["--headless", "--wayland", "--virtual-monitor", f"{W}x{H}"]
        argv = [exe] + backend + ["--wayland-display", "wl-stress"]
        ok = self.spawn(argv, env, os.path.join(rt, "wl-stress"), os.path.join(self.work, "mutter.log"))
        self.env = dict(env, WAYLAND_DISPLAY="wl-stress")
        if ok:
            self.find_display(10)
        return ok

    def start_x(self):
        # Mutter's Xwayland asks for the cookie it wrote beside its runtime directory's sockets.
        rt = self.env.get("XDG_RUNTIME_DIR", "")
        for f in sorted(os.listdir(rt)) if rt and os.path.isdir(rt) else []:
            if f.startswith(".mutter-Xwaylandauth"):
                self.env["XAUTHORITY"] = os.path.join(rt, f)
        return bool(self.display)


def png_rgbx(path):
    """A PNG (8-bit RGB or RGBA, as weston-screenshooter writes) as (width, height, bytes of 4 a pixel, channel order)."""
    data = open(path, "rb").read()
    at, idat, width, height, kind = 8, b"", 0, 0, 0
    while at < len(data):
        length, tag = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if tag == b"IHDR":
            width, height, depth, kind = struct.unpack(">IIBB", body[:10])
        elif tag == b"IDAT":
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    bpp = 4 if kind == 6 else 3
    stride = width * bpp
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(width):
            out[(y * width + x) * 4:(y * width + x) * 4 + 3] = line[x * bpp:x * bpp + 3]
    return width, height, bytes(out), (0, 1, 2)


def overlay_verdict(before, after, overlay):
    """What the overlay did to the screen, read before and after it was mapped, wherever the compositor put it: under its
    transparent left half nothing may change, under its translucent right half each pixel must go halfway to white; a pixel
    gone black, or gone to the gray 0x80 whatever was under it, is the alpha dropped."""
    width, height, a, order = before
    _, _, b, _ = after
    ow, oh = overlay[2], overlay[3]
    counts = {"changed": 0, "black": 0, "gray": 0, "blend": 0, "other": 0}
    box = [10 ** 9, 10 ** 9, -1, -1]
    stride = width * 4
    for y in range(height):
        ra, rb = a[y * stride:(y + 1) * stride], b[y * stride:(y + 1) * stride]
        if ra == rb:
            continue
        for x in range(width):
            o = x * 4
            pa = (ra[o + order[0]], ra[o + order[1]], ra[o + order[2]])
            pb = (rb[o + order[0]], rb[o + order[1]], rb[o + order[2]])
            if pa == pb:
                continue
            counts["changed"] += 1
            box[0], box[1], box[2], box[3] = min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)
            if pb == (0, 0, 0):
                counts["black"] += 1
            elif all(abs(c - 0x80) <= 2 for c in pb):
                counts["gray"] += 1
            elif all(abs(pb[i] - (0x80 + (pa[i] * 0x7f + 127) // 255)) <= 4 for i in range(3)):
                counts["blend"] += 1
            else:
                counts["other"] += 1
    half = ow // 2 * oh
    if counts["black"] >= 0.5 * half:
        verdict = "BLACK: the transparent half is drawn black" + (" and the translucent half opaque gray" if counts["gray"] >= 0.5 * half else "")
    elif counts["gray"] >= 0.5 * half:
        verdict = "OPAQUE: the translucent half is drawn without its alpha (gray over whatever was under it)"
    elif counts["blend"] >= 0.8 * half:
        verdict = "alpha honoured: the transparent half left the screen as it was and the translucent half blends"
    elif counts["changed"] < 0.1 * half:
        verdict = "NOT SHOWN: the screen did not change where an overlay was mapped"
    else:
        verdict = "UNCLEAR: the screen changed in a way no rule here reads"
    return {"verdict": verdict, "counts": counts, "box": box if box[2] >= 0 else None, "half_pixels": half}


# ---- the scenarios ----
def run_client(comp, argv, extra_env=None, timeout=None, t0=False):
    env = dict(comp.env)
    if extra_env:
        env.update(extra_env)
    if t0:
        env["STRESS_T0"] = str(ns())
    before = cpu_ticks(comp.pid) if comp.pid else None
    xb = sum(cpu_ticks(p) or 0 for p in comp.x_pids())
    start = ns()
    try:
        p = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=timeout or (comp.args.seconds + 20))
        out, status = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        out, status = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or ""), "timeout"
    wall = (ns() - start) / 1e9
    res = parse_result(out)
    after = cpu_ticks(comp.pid) if comp.pid else None
    if before is not None and after is not None and wall > 0:
        res["cpu_ms_per_s"] = (after - before) * 1000.0 / TICK / wall
    xa = sum(cpu_ticks(p) or 0 for p in comp.x_pids())
    if comp.x_pids() and wall > 0:
        res["xwayland_cpu_ms_per_s"] = (xa - xb) * 1000.0 / TICK / wall
    res["_status"] = status
    if status not in (0, 3) or "mode" not in res:
        res.setdefault("_notes", []).append(f"exit {status}: " + " | ".join(out.strip().splitlines()[-3:])[:300])
    return res


def scenarios(args, comp):
    b, s = args.bin, str(args.seconds)
    wl = os.path.join(b, "wl-stress")
    fifo = ["-F", comp.fifo] if comp.fifo else []
    gl_env = {"LIBGL_ALWAYS_SOFTWARE": "1"} if not args.real else {}
    out = [
        ("anim-shm-2buf", [wl, "-m", "anim", "-t", s, "-B", "2"], {}),
        ("anim-shm-3buf", [wl, "-m", "anim", "-t", s, "-B", "3"], {}),
        ("anim-shm-band", [wl, "-m", "anim", "-t", s, "-R"], {}),
        ("anim-dmabuf", [wl, "-m", "dmabuf", "-t", s], {}),
        ("windows-4", [wl, "-m", "anim", "-t", s, "-N", "4"], {}),
        ("windows-16", [wl, "-m", "anim", "-t", s, "-N", "16"], {}),
        ("resize-storm", [wl, "-m", "resize", "-t", s], {}),
        ("subsurface-churn", [wl, "-m", "sub", "-t", s], {}),
        ("popup-churn", [wl, "-m", "popup", "-t", s], {}),
        ("lock-input", [wl, "-m", "lock", "-t", s] + fifo, {}),
        ("clipboard-dnd", [wl, "-m", "clip", "-n", "50"] + fifo, {}),
        ("sdl2", [os.path.join(b, "sdl-anim"), s], dict(gl_env, SDL_VIDEODRIVER="wayland")),
        ("glfw", [os.path.join(b, "glfw-anim"), s], gl_env),
    ]
    return out


METRICS = {
    # name: (lower is better, minimal difference that counts)
    "fps": (False, 1.0), "frame_p50": (True, 0.5), "frame_p99": (True, 1.0), "dropped": (True, 2), "late": (True, 2),
    "c2p_p50": (True, 0.5), "c2p_p99": (True, 1.0), "cb_p50": (True, 0.5), "cb_p99": (True, 1.0), "cb_after_present_p50": (True, 0.2),
    "stalls": (True, 2), "discarded": (True, 2), "cpu_ms_per_s": (True, 5.0), "xwayland_cpu_ms_per_s": (True, 5.0),
    "input_p50": (True, 0.5), "input_p99": (True, 1.0), "event_p50": (True, 0.1), "event_p99": (True, 0.5),
    "op_p50": (True, 0.05), "op_p99": (True, 0.2), "shown_p50": (True, 0.5), "shown_p99": (True, 1.0), "churn_per_s": (False, 2),
    "copy_p50": (True, 0.1), "copy_p99": (True, 0.3), "dnd_p50": (True, 0.5), "dnd": (False, 1), "copies": (False, 1),
    "start_ms": (True, 2.0), "first_frame_ms": (True, 2.0), "connect_to_present_ms": (True, 1.0), "launch_to_present_ms": (True, 1.0),
    "launch_to_first_ms": (True, 2.0), "open_ms": (True, 2.0), "first_complete_ms": (True, 2.0), "msc_skips": (True, 2),
    "overlay_black": (True, 1),
}
SHOWN = ["fps", "frame_p50", "frame_p99", "dropped", "late", "c2p_p50", "c2p_p99", "cb_p50", "cb_after_present_p50", "stalls",
         "cpu_ms_per_s", "xwayland_cpu_ms_per_s", "input_p50", "input_p99", "event_p50", "op_p50", "op_p99", "shown_p50", "shown_p99",
         "churn_per_s", "copy_p50", "copy_p99", "copies", "dnd", "dnd_p50", "start_ms", "first_frame_ms", "connect_to_present_ms",
         "launch_to_present_ms", "launch_to_first_ms", "open_ms", "first_complete_ms", "msc_skips", "overlay_black"]


def bench(args, comp):
    results = {}
    print(f"== {comp.name}", flush=True)
    if not comp.start():
        if comp.name != "moonwater" and comp.proc is None:
            return None
        log = comp.logs[-1] if comp.logs else ""
        tail = open(log, errors="replace").read()[-300:] if log and os.path.exists(log) else ""
        note(f"{comp.name}: did not start here: skipped ({tail.strip().splitlines()[-1] if tail.strip() else 'no log'})")
        comp.stop()
        return None
    try:
        # Startup: the compositor's start to its socket, and to the first frame of a client started the moment the socket is there.
        first = run_client(comp, [os.path.join(args.bin, "wl-stress"), "-m", "first"], timeout=20)
        results["startup"] = {"start_ms": comp.start_ms}
        if "connect_to_present_ms" in first and comp.t_spawn:
            results["startup"]["first_frame_ms"] = comp.start_ms + first.get("exec_to_present_ms", 0)
            results["startup"]["connect_to_present_ms"] = first["connect_to_present_ms"]
        time.sleep(0.3)
        hung = 0
        for name, argv, env in scenarios(args, comp):
            if args.only and name not in args.only:
                continue
            if not os.path.exists(argv[0]):
                results[name] = {"_notes": [f"{os.path.basename(argv[0])} was not built"]}
                continue
            r = run_client(comp, argv, env)
            results[name] = r
            hung = hung + 1 if r.get("_status") == "timeout" else 0
            if hung >= 2:
                note(f"{comp.name}: two scenarios in a row did not end: the rest of its rows are not run")
                break
            print(f"  {name}: " + " ".join(f"{k}={v:.3f}" if isinstance(v, float) else f"{k}={v}" for k, v in r.items()
                                           if k in SHOWN or k == "_notes"), flush=True)
            if not comp.alive() and not args.real:
                note(f"{comp.name}: died during {name}")
                break
        # An application's launch to its first frame, on a compositor that is warm.
        app = run_client(comp, [os.path.join(args.bin, "wl-stress"), "-m", "first"], t0=True, timeout=20)
        results["startup"]["launch_to_present_ms"] = app.get("launch_to_present_ms")
        if os.path.exists(os.path.join(args.bin, "glfw-anim")):
            g = run_client(comp, [os.path.join(args.bin, "glfw-anim"), "1"], {"LIBGL_ALWAYS_SOFTWARE": "1"} if not args.real else {},
                           t0=True, timeout=30)
            if "launch_to_first_ms" in g:
                results["startup"]["launch_to_first_ms"] = g["launch_to_first_ms"]
        # The X rows: Xwayland's start (the first X client's open and first frame), its animation, and the overlay.
        xs = os.path.join(args.bin, "x-stress")
        if (not args.only or any(n.startswith("x") for n in args.only)) and os.path.exists(xs):
            if comp.start_x():
                xenv = {"DISPLAY": comp.display}
                xa = run_client(comp, [xs, "-m", "anim", "-t", str(args.seconds)], xenv, t0=True, timeout=args.seconds + 30)
                results["x-anim"] = xa
                results["startup"]["open_ms"] = xa.get("open_ms")
                results["startup"]["first_complete_ms"] = xa.get("first_complete_ms")
                print("  x-anim: " + " ".join(f"{k}={v}" for k, v in xa.items() if k in SHOWN or k == "_notes"), flush=True)
                results["x-overlay"] = overlay(args, comp, xs, xenv)
                print("  x-overlay: %s" % results["x-overlay"], flush=True)
            else:
                results["x-anim"] = {"_notes": ["no Xwayland on this compositor here"]}
    finally:
        comp.stop()
    return results


def overlay(args, comp, xs, xenv):
    env = dict(comp.env, **xenv)
    if not args.real:
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    p = subprocess.Popen([xs, "-m", "overlay", "-t", "3", "-w", "640", "-h", "400"], env=env, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    geo, ground, lines = None, None, []
    end = time.time() + 20
    while time.time() < end:
        line = p.stdout.readline()
        if not line:
            break
        lines.append(line.strip())
        if line.startswith("OVERLAY"):
            geo = [int(v) for v in line.split()[1:]]
        elif line.startswith("GROUND"):
            ground = tuple(int(v) for v in line.split()[1:])
            break
    res = {"_notes": [l[5:] for l in lines if l.startswith("NOTE ")]}
    if geo and ground:
        time.sleep(0.5)
        before = comp.screenshot()
        p.stdin.write("go\n")
        p.stdin.flush()
        # The first frames after the map, where a window shown before its picture is would flash black, then the screen settled.
        flash, t_map = 0, time.time()
        while before and time.time() - t_map < 0.4:
            shot = comp.screenshot()
            if shot:
                flash = max(flash, overlay_verdict(before, shot, geo)["counts"]["black"])
            time.sleep(0.02)
        time.sleep(1.0)
        after = comp.screenshot()
        if before and after:
            v = overlay_verdict(before, after, geo)
            res.update(v)
            res["flash_black"] = flash
            res["overlay_black"] = 1.0 if v["verdict"].startswith(("BLACK", "OPAQUE", "NOT SHOWN")) else 0.0
        else:
            res["_notes"].append("the screen cannot be read back on this compositor here: the overlay's pixels are not judged")
    else:
        res["_notes"].append("the overlay did not start: " + " | ".join(lines[-3:]))
    try:
        p.stdin.close()
        p.wait(10)
    except (subprocess.TimeoutExpired, OSError):
        p.kill()
    return res


# ---- the report ----
def fmt(v):
    if v is None:
        return "-"
    if isinstance(v, float):
        return "%.0f" % v if abs(v) >= 1000 else "%.2f" % v
    return str(v)


def table(all_results, names):
    lines = []
    scen = []
    for r in all_results.values():
        if r:
            for k in r:
                if k not in scen:
                    scen.append(k)
    width = 14
    lines.append("%-40s" % "scenario / metric" + "".join("%*s" % (width, n) for n in names))
    for s in scen:
        metrics = []
        for n in names:
            r = (all_results.get(n) or {}).get(s) or {}
            for k in SHOWN:
                if k in r and isinstance(r[k], (int, float)) and r[k] != -1 and k not in metrics:
                    metrics.append(k)
        if not metrics:
            lines.append("%-40s" % s + "".join("%*s" % (width, "-") for n in names))
        for k in metrics:
            row = "%-40s" % (s + " " + k)
            for n in names:
                v = ((all_results.get(n) or {}).get(s) or {}).get(k)
                row += "%*s" % (width, fmt(v if v != -1 else None))
            lines.append(row)
        for n in names:
            for t in ((all_results.get(n) or {}).get(s) or {}).get("_notes", []):
                lines.append(f"    NOTE {n}: {t}")
            v = ((all_results.get(n) or {}).get(s) or {}).get("verdict")
            if v:
                lines.append(f"    {n}: overlay {v}")
    return lines


def gaps(all_results, names):
    mw = all_results.get("moonwater") or {}
    out = []
    for other in names:
        if other == "moonwater" or not all_results.get(other):
            continue
        for s, r in mw.items():
            o = all_results[other].get(s) or {}
            for k, (lower, eps) in METRICS.items():
                a, b = r.get(k), o.get(k)
                if not isinstance(a, (int, float)) or not isinstance(b, (int, float)) or a == -1 or b == -1:
                    continue
                worse = a - b if lower else b - a
                if worse <= eps:
                    continue
                ratio = (a / b if b else float("inf")) if lower else (b / a if a else float("inf"))
                out.append((ratio, worse, other, s, k, a, b))
    out.sort(key=lambda t: (-t[0] if t[0] != float("inf") else -1e18, -t[1]))
    return ["Moonwater slower than %s by %s: %s %s (%s against %s)" %
            (o, ("x%.2f" % r) if r != float("inf") else "+" + fmt(w), s, k, fmt(a), fmt(b)) for r, w, o, s, k, a, b in out]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--moonwater", required=True)
    ap.add_argument("--moonwater-options", default="")
    ap.add_argument("--work", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--real", action="store_true")
    ap.add_argument("--seconds", type=float, default=3)
    ap.add_argument("--only", default="")
    ap.add_argument("--compositors", default="moonwater,weston,kwin,mutter")
    ap.add_argument("--baseline", default="")
    args = ap.parse_args()
    args.only = [s for s in args.only.split(",") if s]
    args.moonwater_options = args.moonwater_options.split()
    os.makedirs(args.work, exist_ok=True)
    kinds = {"moonwater": Moonwater, "weston": Weston, "kwin": KWin, "mutter": Mutter}
    names = [n for n in args.compositors.split(",") if n in kinds]
    all_results = {}
    for n in names:
        all_results[n] = bench(args, kinds[n](args))
    shown = [n for n in names if all_results.get(n)]
    head = ["Wayland stress bench, %s, %s, %s, %d s a scenario, %s" % (platform.node(), time.strftime("%Y-%m-%d %H:%M"),
            "real machine" if args.real else f"headless {W}x{H}", args.seconds, " ".join(shown))]
    lines = head + [""] + table(all_results, shown) + [""] + gaps(all_results, shown)
    if args.baseline and os.path.exists(args.baseline):
        old = json.load(open(args.baseline)).get("results", {}).get("moonwater") or {}
        new = all_results.get("moonwater") or {}
        lines += ["", "Moonwater before (%s) and after:" % args.baseline]
        for s in new:
            for k in SHOWN:
                a, b = (old.get(s) or {}).get(k), (new.get(s) or {}).get(k)
                if isinstance(a, (int, float)) and isinstance(b, (int, float)) and (a != b):
                    lines.append("  %-40s %12s -> %-12s" % (s + " " + k, fmt(a), fmt(b)))
    lines += [""] + ["NOTE " + n for n in NOTES]
    text = "\n".join(lines) + "\n"
    print(text)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    open(args.out, "w").write(text)
    json.dump({"notes": NOTES, "results": all_results}, open(os.path.splitext(args.out)[0] + ".json", "w"), indent=1, default=str)
    print("results: %s (and %s)" % (args.out, os.path.splitext(args.out)[0] + ".json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
