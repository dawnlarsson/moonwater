#!/usr/bin/env python3
"""Mutation proofs for the Wayland shim, src/canvas/wayland.c.

Each mutation is one edit to a copy of the shim, built into CHECK_wayland (test/checks.c) with the compiler in CC (cc by
default) as lane_wayland builds it (linked with src/lib.c's object, built with -DSTANDARD_NO_ENTRY); the check must turn red.
The unmutated tree is built and run first, and must be green, or nothing is counted (exit 2): a mutant is only a proof
against a check that passes without it. A mutant that does not build, and one whose text is not in the shim once, is a
survivor: it proved nothing. A mutation that survives is a hole in the check. The mutations are the defects the
security pass of 2026-11-04 fixed, put back (each named by the commit that fixed it).

    python3 test/wayland_mutations.py [--most N] [NAME...]   from the tree's root; a build and a run each, a few seconds

--most N is the pin (sh test/run mutation_wayland): more than N survivors fails, and fewer fails too, saying to lower it,
so the pin may only fall.
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = tempfile.mkdtemp(prefix='wayland-mutations-')
argv = sys.argv[1:]
most = 0
if argv[:1] == ['--most']:
    most, argv = int(argv[1]), argv[2:]
only = argv
cc = os.environ.get('CC', 'cc')
assembler = ['-fno-integrated-as'] if 'clang' in subprocess.run([cc, '--version'], capture_output=True, text=True).stdout else []

M = [
    # ab981848: a copy of a run made the window's rectangle again, a walk of every surface and its chain.
    ("every copy walks the window again",
     "        long ox, oy, wx = window->x, wy = window->y, ww = window->width, wh = window->height;\n",
     "        struct wayland_rect again = wayland_window_of(c, wayland_root_of(c, child));\n"
     "        long ox, oy, wx = again.x, wy = again.y, ww = again.width, wh = again.height;\n"
     "        (void)window;\n"),
    # 0976821d: a copy's pipe end in the output queue is the shim's, closed once sent or when its client goes.
    ("the queue's own descriptors are never closed",
     "        for (long at = 0; at < sent; at++)\n                if (f[at].owned)\n",
     "        for (long at = 0; at < sent; at++)\n                if (f[at].owned && 0)\n"),
    # The budget: a client's copies per window of time, past which its commits wait as for a GPU.
    ("no budget",
     "        if (!wayland_budget_left(wayland_root_of(c, s)))\n        {\n                c->stalled_fd = -1;",
     "        if (0)\n        {\n                c->stalled_fd = -1;"),
    ("a commit past the budget is never resumed",
     "? c->budget_surface >= 0 && c->surfaces[c->budget_surface].live && !wayland_budget_left(&c->surfaces[c->budget_surface])",
     "? 1"),
    ("a picture's copy is not counted",
     "                s->budget_used += picture.full ? wayland_min(tw, ww) * wayland_min(th, wh) : 0;\n",
     "                ;\n"),
    ("the compositor's own take-away of fullscreen is not kept for the next show",
     "        if (!(facts.states & WAYLAND_STATE_FULLSCREEN))\n                        s->want_fullscreen = 0;\n",
     "        if (0)\n                        s->want_fullscreen = 0;\n"),
    ("a change owed to a client is not sent on its acknowledgement",
     "                                wayland_configure_owed(c, s);\n",
     "\n"),
    ("a fullscreen request is never kept for the next show",
     "                else if (what == WAYLAND_DO_FULLSCREEN)\n                        s->want_fullscreen = 1;\n",
     "                else if (what == WAYLAND_DO_FULLSCREEN && 0)\n                        s->want_fullscreen = 1;\n"),
]


shim = os.path.join(root, 'src/canvas/wayland.c')
text = open(shim).read()
library = os.path.join(work, 'lib.o')


def build_and_run(place, source):
    """The tree as links with the shim's text as given, CHECK_wayland built as lane_wayland builds it, and run."""
    os.makedirs(place)
    subprocess.run(['cp', '-as', os.path.join(root, 'src'), os.path.join(place, 'src')], check=True)
    subprocess.run(['cp', '-as', os.path.join(root, 'test'), os.path.join(place, 'test')], check=True)
    os.unlink(os.path.join(place, 'src/canvas/wayland.c'))
    open(os.path.join(place, 'src/canvas/wayland.c'), 'w').write(source)
    binary = os.path.join(place, 'check')
    built = subprocess.run([cc, '-std=gnu11', '-O2', '-w', '-D_GNU_SOURCE', '-DCHECK_wayland', '-DCONFIG_MOONWATER_WAYLAND',
                            os.path.join(place, 'test/checks.c'), library, '-o', binary], capture_output=True, text=True)
    if built.returncode:
        return 'build', built.stderr.strip()[-300:]
    try:
        ran = subprocess.run([binary], capture_output=True, text=True, errors='replace', timeout=600, cwd=root)
    except subprocess.TimeoutExpired:
        return 'red', 'hang'
    fails = [l.strip() for l in ran.stdout.splitlines() if 'FAIL' in l]
    return ('red', fails[0][:110] if fails else 'exit %d' % ran.returncode) if ran.returncode or fails else ('green', '')


def one(index, name, old, new):
    if text.count(old) != 1:
        return name, 'NOT APPLIED', 'the text is there %d times' % text.count(old)
    state, detail = build_and_run(os.path.join(work, str(index)), text.replace(old, new))
    shutil.rmtree(os.path.join(work, str(index)), ignore_errors=True)
    return name, {'build': 'BUILD ERROR', 'red': 'red', 'green': 'SURVIVED'}[state], detail


made = subprocess.run([cc, '-O2', '-w'] + assembler + ['-DSTANDARD_NO_ENTRY', '-c', os.path.join(root, 'src/lib.c'), '-o', library],
                      capture_output=True, text=True)
state, detail = ('build', made.stderr.strip()[-300:]) if made.returncode else build_and_run(os.path.join(work, 'base'), text)
if state != 'green':
    print('baseline red: the unmutated tree %s: %s' % ('did not build' if state == 'build' else 'fails CHECK_wayland', detail))
    shutil.rmtree(work, ignore_errors=True)
    sys.exit(2)
chosen = [(i, m) for i, m in enumerate(M) if not only or m[0] in only]
with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('MUTATION_JOBS', '8'))) as pool:
    results = list(pool.map(lambda pair: one(pair[0], *pair[1]), chosen))
shutil.rmtree(work, ignore_errors=True)
survivors = [name for name, verdict, _ in results if verdict != 'red']
for name, verdict, detail in results:
    print(('ok   ' if verdict == 'red' else 'FAIL ') + name + ': ' + verdict + (' (' + detail + ')' if detail else ''))
print('mutations wayland: %d, killed %d, survived %d, pinned at most %d' % (len(results), len(results) - len(survivors), len(survivors), most))
if only:
    sys.exit(0 if not survivors else 1)
if len(survivors) < most:
    print('mutations wayland: %d survive and the pin is %d: lower the pin to %d' % (len(survivors), most, len(survivors)))
sys.exit(len(survivors) != most)
