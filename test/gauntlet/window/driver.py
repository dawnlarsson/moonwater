#!/usr/bin/env python3
"""
The window gauntlet's cases, run in the namespace inside.sh made: Xwayland started under the compositor's window manager, then
each windowing library's client on Wayland and on X11, with the card's GL and with llvmpipe, each held to a deadline.

    python3 driver.py WORK XROOT FILTER       XROOT "" when the driver runs in the root itself

One line a case: PASS, FAIL (with the client's last words) or HANG; then the totals. The exit status is the number not passed.
"""
import os
import socket
import subprocess
import sys
import time

work, xroot, filt = sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ""
ENV = ["HOME=/tmp", "XDG_RUNTIME_DIR=/run", "PATH=/usr/local/bin:/usr/bin:/bin", "LANG=C.UTF-8"]
DISPLAY = ":7"
counts = {"PASS": 0, "FAIL": 0, "HANG": 0}


def inside(args):
    return (["chroot", xroot] + args) if xroot else args


def start_xwayland():
    wm = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    wm.connect("/run/moonwater/xwm/wm-0")
    fd = wm.detach()
    proc = subprocess.Popen(inside(["/usr/bin/env", "-i"] + ENV + ["WAYLAND_DISPLAY=/run/moonwater/wayland-0", "/usr/bin/Xwayland", DISPLAY,
                                                                     "-rootless", "-wm", str(fd), "-nolisten", "local", "-noreset"]),
                            pass_fds=[fd], stdout=open(os.path.join(work, "xwayland.log"), "w"), stderr=subprocess.STDOUT)
    os.close(fd)
    end = time.time() + 10
    while time.time() < end and not os.path.exists(os.path.join(xroot or "/", "tmp/.X11-unix/X" + DISPLAY[1:])):
        time.sleep(0.1)
    return proc


def case(name, args, env, limit=60):
    if filt and filt not in name:
        return
    log = os.path.join(work, "case.log")
    try:
        p = subprocess.run(inside(["/usr/bin/env", "-i"] + ENV + env + args), stdout=open(log, "w"), stderr=subprocess.STDOUT, timeout=limit)
        status = "PASS" if p.returncode == 0 else "FAIL"
    except subprocess.TimeoutExpired:
        status = "HANG"
    counts[status] += 1
    print(f"{status} {name}", flush=True)
    if status != "PASS":
        with open(log, errors="replace") as f:
            for line in f.read().strip().splitlines()[-5:]:
                print("     " + line, flush=True)


xw = start_xwayland()
if xw.poll() is not None:
    print("FAIL Xwayland did not start")
WAYLAND = ["WAYLAND_DISPLAY=/run/moonwater/wayland-0"]
X11 = ["DISPLAY=" + DISPLAY]
for gl, glenv in (("card", []), ("llvmpipe", ["LIBGL_ALWAYS_SOFTWARE=1", "GALLIUM_DRIVER=llvmpipe"])):
    case(f"GLFW wayland {gl}", ["/trial/glfw", "wayland"], WAYLAND + glenv)
    case(f"GLFW x11 {gl}", ["/trial/glfw", "x11"], X11 + WAYLAND + glenv)
    case(f"SDL2-compat wayland {gl}", ["/trial/sdl"], WAYLAND + glenv + ["SDL_VIDEODRIVER=wayland"])
    case(f"SDL2-compat x11 {gl}", ["/trial/sdl"], X11 + glenv + ["SDL_VIDEODRIVER=x11"])
    if os.path.isdir(os.path.join(work, "sdl2real")):
        case(f"SDL2 (Steam's) wayland {gl}", ["/trial/sdl"], WAYLAND + glenv + ["SDL_VIDEODRIVER=wayland", "LD_LIBRARY_PATH=/trial/sdl2real"])
        case(f"SDL2 (Steam's) x11 {gl}", ["/trial/sdl"], X11 + glenv + ["SDL_VIDEODRIVER=x11", "LD_LIBRARY_PATH=/trial/sdl2real"])
xw.kill()
print(f"pass {counts['PASS']}, fail {counts['FAIL']}, hang {counts['HANG']}")
sys.exit(counts["FAIL"] + counts["HANG"])
