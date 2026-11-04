#!/usr/bin/env python3
"""Mutation proofs for the Wayland shim, src/canvas/wayland.c.

Each mutation is one edit to a copy of the shim, built into CHECK_wayland (test/checks.c) with the compiler in CC (cc by
default); the check must turn red. A mutation that survives is a hole in the check. The mutations are the defects the
security pass of 2026-11-04 fixed, put back (incident_response.md names each by its MW number).

    python3 test/wayland_mutations.py [NAME...]          from the tree's root; a build and a run each, a few seconds
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = tempfile.mkdtemp(prefix='wayland-mutations-')
only = sys.argv[1:]
cc = os.environ.get('CC', 'cc')

M = [
    # MW-2026-112: a copy of a run made the window's rectangle again, a walk of every surface and its chain.
    ("every copy walks the window again",
     "        long ox, oy, wx = window->x, wy = window->y, ww = window->width, wh = window->height;\n",
     "        struct wayland_rect again = wayland_window_of(c, wayland_root_of(c, child));\n"
     "        long ox, oy, wx = again.x, wy = again.y, ww = again.width, wh = again.height;\n"
     "        (void)window;\n"),
]


def one(index, name, old, new):
    place = os.path.join(work, str(index))
    os.makedirs(os.path.join(place, 'src/canvas'))
    os.makedirs(os.path.join(place, 'test'))
    shutil.copy(os.path.join(root, 'test/checks.c'), os.path.join(place, 'test/checks.c'))
    shutil.copy(os.path.join(root, 'src/canvas/wayland_protocol.inc'), os.path.join(place, 'src/canvas/wayland_protocol.inc'))
    text = open(os.path.join(root, 'src/canvas/wayland.c')).read()
    if text.count(old) != 1:
        return name, 'NOT APPLIED (the text is not there once)'
    open(os.path.join(place, 'src/canvas/wayland.c'), 'w').write(text.replace(old, new))
    binary = os.path.join(place, 'check')
    built = subprocess.run([cc, '-std=gnu11', '-O2', '-w', '-D_GNU_SOURCE', '-DCHECK_wayland', '-DCONFIG_MOONWATER_WAYLAND',
                            os.path.join(place, 'test/checks.c'), '-o', binary], capture_output=True, text=True)
    if built.returncode:
        return name, 'red (did not build)'
    ran = subprocess.run([binary, '2000'], capture_output=True, text=True, timeout=600)
    return name, 'red' if ran.returncode else 'SURVIVED'


chosen = [(i, m) for i, m in enumerate(M) if not only or m[0] in only]
with concurrent.futures.ThreadPoolExecutor(8) as pool:
    results = list(pool.map(lambda pair: one(pair[0], *pair[1]), chosen))
survivors = 0
for name, verdict in results:
    print(('ok   ' if verdict.startswith('red') else 'FAIL ') + name + ': ' + verdict)
    survivors += not verdict.startswith('red')
shutil.rmtree(work, ignore_errors=True)
print('%d of %d mutations turn CHECK_wayland red' % (len(results) - survivors, len(results)))
sys.exit(survivors != 0)
