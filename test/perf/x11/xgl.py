"""xgl.py [SECONDS]: an OpenGL ES 2 window on X11 (EGL and libGLESv2 through ctypes, the bowl's Mesa; run under xwayland-moonwater
the way xclient.py is). ember, round 2: the ground-truth GL case. Each frame it clears a 16-cell strip at the top left: cell i is white
when bit i of the frame counter is set, black otherwise (scissored clears, no shader). It prints "frame N MONOTONIC_MS" after each swap
and "key K MONOTONIC_MS" on each key press (keys are never read as text; a key only bumps the key count the frame prints).
xgl_check.py decodes the strip on the screen and reports which frame the screen shows, against the printed ones (wrong, older, stale).
"""
import ctypes, sys, time

W, H = 480, 240
CELLS, CELL_W, CELL_H = 16, 16, 24
KEY_PRESS = 2
EGL_NONE, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT = 0x3038, 0x3033, 0x0004, 0x3040, 0x0004
EGL_RED, EGL_GREEN, EGL_BLUE, EGL_ALPHA = 0x3024, 0x3023, 0x3022, 0x3021
EGL_CONTEXT_CLIENT_VERSION, EGL_OPENGL_ES_API = 0x3098, 0x30A0
GL_COLOR_BUFFER_BIT, GL_SCISSOR_TEST = 0x4000, 0x0C11


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    X = ctypes.CDLL("libX11.so.6")
    E = ctypes.CDLL("libEGL.so.1")
    G = ctypes.CDLL("libGLESv2.so.2")
    X.XOpenDisplay.restype = ctypes.c_void_p
    X.XCreateGC.restype = ctypes.c_void_p
    E.eglGetDisplay.restype = ctypes.c_void_p
    E.eglCreateContext.restype = ctypes.c_void_p
    E.eglCreateWindowSurface.restype = ctypes.c_void_p
    d = X.XOpenDisplay(None)
    if not d:
        sys.exit("xgl: no display")
    root = X.XRootWindow(ctypes.c_void_p(d), X.XDefaultScreen(ctypes.c_void_p(d)))
    win = X.XCreateSimpleWindow(ctypes.c_void_p(d), root, 120, 120, W, H, 0, 0, 0)
    X.XStoreName(ctypes.c_void_p(d), win, b"xgl")
    X.XSelectInput(ctypes.c_void_p(d), win, ctypes.c_long((1 << 0) | (1 << 15)))
    X.XMapWindow(ctypes.c_void_p(d), win)
    X.XFlush(ctypes.c_void_p(d))
    disp = E.eglGetDisplay(ctypes.c_void_p(d))
    E.eglInitialize(ctypes.c_void_p(disp), None, None)
    E.eglBindAPI(EGL_OPENGL_ES_API)
    attribs = (ctypes.c_int * 15)(EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                  EGL_RED, 8, EGL_GREEN, 8, EGL_BLUE, 8, EGL_ALPHA, 8, EGL_NONE, 0, 0)
    cfg = ctypes.c_void_p()
    n = ctypes.c_int()
    if not E.eglChooseConfig(ctypes.c_void_p(disp), attribs, ctypes.byref(cfg), 1, ctypes.byref(n)) or n.value < 1:
        sys.exit("xgl: no EGL config")
    ctx_attr = (ctypes.c_int * 3)(EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE)
    ctx = E.eglCreateContext(ctypes.c_void_p(disp), cfg, None, ctx_attr)
    surf = E.eglCreateWindowSurface(ctypes.c_void_p(disp), cfg, ctypes.c_void_p(win), None)
    if not surf or not E.eglMakeCurrent(ctypes.c_void_p(disp), ctypes.c_void_p(surf), ctypes.c_void_p(surf), ctypes.c_void_p(ctx)):
        sys.exit("xgl: no current context")
    E.eglSwapInterval(ctypes.c_void_p(disp), 1)
    G.glViewport(0, 0, W, H)
    G.glEnable(GL_SCISSOR_TEST)
    G.glClearColor.argtypes = [ctypes.c_float] * 4
    print("ready %d" % win, flush=True)
    stop = time.monotonic() + seconds
    frame, keys, dirty = 0, 0, True
    ev = ctypes.create_string_buffer(256)
    xd = ctypes.c_void_p(d)
    while True:
        while X.XPending(xd):
            X.XNextEvent(xd, ev)
            if ctypes.c_int.from_buffer(ev).value == KEY_PRESS:
                keys += 1
                print("key %d %.3f" % (keys, time.monotonic() * 1000), flush=True)
                dirty = True
        animating = time.monotonic() < stop
        if not (animating or dirty):
            time.sleep(0.002)
            continue
        dirty = False
        frame += 1
        G.glScissor(0, 0, W, H)
        G.glClearColor(0.0, 0.0, 0.0, 1.0)
        G.glClear(GL_COLOR_BUFFER_BIT)
        for i in range(CELLS):
            if (frame >> i) & 1:
                G.glScissor(i * CELL_W, H - CELL_H, CELL_W, CELL_H)
                G.glClearColor(1.0, 1.0, 1.0, 1.0)
                G.glClear(GL_COLOR_BUFFER_BIT)
        E.eglSwapBuffers(ctypes.c_void_p(disp), ctypes.c_void_p(surf))
        print("frame %d %.3f" % (frame, time.monotonic() * 1000), flush=True)


if __name__ == "__main__":
    main()
