#!/usr/bin/env python3
"""
The window manager rows (test/wayland_xwm.sh): Xwayland, started with its window manager's connection given as -wm, is a client
of the compositor. The compositor accepts that connection and redirects the root's subwindows (Composite, Manual); a mapped Xlib
client window then gets its surface (xwayland_surface_v1 and a committed buffer) and an Expose.

    python3 test/wayland_xwm.py WORK XROOT CLIENT

WORK is the work directory (the compositor's log is WORK/compositor.log), XROOT the root Xwayland runs in ("" for the host's),
CLIENT the Xlib client's path as the root sees it. One line a row: "ok NAME" or "FAIL NAME ...", and the exit status is the
number of failures.
"""
import os
import socket
import subprocess
import sys
import time

work, xroot, client = sys.argv[1], sys.argv[2], sys.argv[3]
failures = 0
ENV = ["HOME=/tmp", "XDG_RUNTIME_DIR=/run", "PATH=/usr/local/bin:/usr/bin:/bin", "LANG=C.UTF-8"]


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


def inside(args):
    return (["chroot", xroot] + args) if xroot else args


def read(path):
    try:
        with open(path, errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def wait_for(path, text, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        if text in read(path):
            return True
        time.sleep(0.05)
    return False


# The window manager's connection: a socket to the compositor's listening one, its far end given to Xwayland as -wm.
wm = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
wm.connect("/run/moonwater/xwm/wm-0")
fd = wm.detach()
xwayland = subprocess.Popen(
    inside(["/usr/bin/env", "-i"] + ENV + ["WAYLAND_DISPLAY=/run/moonwater/wayland-0", "WAYLAND_DEBUG=1",
                                           "/usr/bin/Xwayland", ":1", "-rootless", "-wm", str(fd), "-nolisten", "local",
                                           "-noreset", "-verbose", "3"]),
    pass_fds=[fd], stdout=open(os.path.join(work, "xwayland.log"), "w"), stderr=subprocess.STDOUT)
os.close(fd)

row(wait_for(os.path.join(work, "compositor.log"), "is connected"), "the window manager of Xwayland is connected",
    read(os.path.join(work, "compositor.log"))[-200:])
row(wait_for(os.path.join(work, "compositor.log"), "redirected"),
    "the root's subwindows are redirected (Composite, Manual)", read(os.path.join(work, "compositor.log"))[-200:])

client_log = os.path.join(work, "client.log")
xclient = subprocess.Popen(
    inside(["/usr/bin/env", "-i"] + ENV + ["DISPLAY=:1", client, "-w", "300", "-h", "200", "-t", "20"]),
    stdin=subprocess.PIPE, stdout=open(client_log, "w"), stderr=subprocess.STDOUT)

row(wait_for(client_log, "expose"), "a mapped X window is drawn: it gets its Expose", read(client_log)[-200:])
xwayland_log = os.path.join(work, "xwayland.log")
row(wait_for(xwayland_log, "get_xwayland_surface"), "the shim makes the window its surface (get_xwayland_surface)",
    read(xwayland_log)[-300:])
row(wait_for(xwayland_log, ".attach(wl_buffer"), "the window's surface takes a buffer", read(xwayland_log)[-300:])

xclient.kill()
xwayland.terminate()
xwayland.wait()
print(f"{failures} failed", flush=True)
sys.exit(failures)
