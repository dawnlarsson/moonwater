#!/usr/bin/env python3
"""Cut src/canvas/compositor.c into the files of src/canvas/compositor/, at function boundaries.

    python3 test/compositor_split.py             the plan: each file, where it starts and ends, its functions; nothing written
    python3 test/compositor_split.py --check     the split made in a copy of the tree and held to the checks below; this tree
                                                 is not touched
    python3 test/compositor_split.py --apply     --check, and then the same split written into this tree, the tools that name
                                                 compositor.c by path moved to the new paths, and the function audit resealed

compositor.c stays the one translation unit it is: each file holds one run of it, in its order, and compositor.c includes
them where the runs were, so every static is where it was and nothing is declared again. The cut list (CUTS) is names, not
line numbers: a file starts at a function (the statics, macros and comments between that function and the one before it
go with it) or at a section's own opening line, so the list can be run on whatever main is that minute. What is not cut out
(the prelude and its includes, the kernel's interfaces and the compositor's state, the table, the startup and the loop) is
compositor.c's own text between the includes.

The split is held to (--check, in a copy):
  - compositor.c with each part's text put back where its #include is (and its includes' "../" taken off) is the file it
    was, byte for byte;
  - gcc -E -P of programs/shell.c (the types row's defines, and again without the Wayland shim and the card), of
    src/build/build.c and of test/checks.c for every section that includes the compositor is byte-identical before and
    after, or, for a unit so large that GCC stops tracking columns before its end (the shell with the shim, the build
    tool), identical but for the indentation GCC lays its last lines out with, which moves with how the text is cut in
    files: every token and every line break the same;
  - the function audit's inventory has the same count of functions, and dropped_results, one_way_state,
    performance_coverage and specialization_coverage pass;
  - every mutation of test/ui_mutations.py that edits the compositor finds its text exactly once in the file it now
    names, and test/differential.py's reading of the compositor gives the text it gave.

The split must be applied when nobody has uncommitted edits in compositor.c: --apply refuses a tree where compositor.c or
a tool it rewrites differs from HEAD, or where src/canvas/compositor/ is already there.
"""

import argparse
import ast
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'test/assembly'))
from inventory import function_header, lex  # noqa: E402

SOURCE = 'src/canvas/compositor.c'
PARTS = 'src/canvas/compositor'

# The cut list, in the file's order: each entry starts a run at a function (its name) or at a section's opening line (its
# text), and the run goes on to the next entry. 'compositor.c' is a run that stays in compositor.c itself. The text before
# the first entry (the header, the switches, ui/ui.c, the sound server's page and gpu.c with its stand-ins) stays too.
CUTS = [
    ('compose.c', 'compositor_px1',
     "the look on the processor and the card's frames: windows, lines, corners, the panel's tabs, the scene and its plan"),
    ('keymap.c', '/* ---- keymap: what a key means ---- */',
     'what a key means: the layouts, dead keys and the held modifiers'),
    ('pointer.c', '/* ---- pointer: what a hand means ---- */',
     "what a hand means: the gain, the repeat, the shake, the pointer's shapes and its picture"),
    ('outputs.c', '/* ---- outputs: the screens one desktop spans ---- */',
     "the screens one desktop spans: a point and a window kept on them, the snap's rules, a screen's cells"),
    ('compositor.c', 'compositor_predict_px',
     "the kernel's DRM and evdev interface and the compositor's own state"),
    ('damage.c', 'compositor_area',
     "what changed: rectangles and damage, landings and glides, the snap preview's state, a row's pixels and runs, the frames and the bands"),
    ('glass.c', 'compositor_glass_reach',
     "the glass under the panel and the tabs: the grain, the frost, the blur and its scene"),
    ('scanout.c', '/* ---- a window shown straight from its buffer ---- */',
     'what the card scans out: a window straight from its buffer, the record, the flip and its time, the planes, a turned screen'),
    ('gpu_host.c', 'compositor_gpu_publish',
     "the card's part of a present: the render node's state, the scene and the window lists drawn on it, its hangs and its wake"),
    ('present.c', 'compositor_metric_publish',
     "a present: the metrics, the loop's tick and the frame composed and shown"),
    ('kms.c', 'compositor_said',
     'the card: the displays and their modes, the scanouts and plates, the space taken, the card found and opened, the cursor plane'),
    ('session.c', 'compositor_virtual',
     'the session: a machine that slept, the settings read and published, the input devices found, the doors and their threads'),
    ('manage.c', 'compositor_stack_remove',
     'the windows: the stack and the keys, grips and dividers, tiles and the grid, places, a row taken and let go, the keyboard on the screen, the clipboard, popups'),
    ('input.c', 'compositor_latency',
     "what a person does: the keys and the chords, the volume, the hand's motion and buttons, the prediction, the wheel and the pad, the marks and touch"),
    ('compositor.c', 'compositor_table_memfd',
     'the table and its doors, the signals and uevents, a headless screen, the options, the startup and the loop'),
]

END_MARKER = re.compile(r'\s*/\* ---- end of .* ---- \*/\s*$')
INCLUDE = re.compile(r'^(\s*#\s*include\s+")(?!/)', re.M)
UNINCLUDE = re.compile(r'^(\s*#\s*include\s+")\.\./', re.M)
PART_LINE = re.compile(r'^#include "compositor/([\w.]+)".*\n', re.M)

# The preprocessor's view the split is held to: the types row's (test/run gate_types), the build without the shim and the
# card, the build tool, and test/checks.c the way freestanding_check builds every section that includes the compositor
# (check_sections: the CHECK_ names of the conditional each #include of compositor.c in test/checks.c sits in).
FREESTANDING = ['-O2', '-march=x86-64', '-fno-builtin', '-fno-stack-protector']
VIEWS = [
    ('programs/shell.c', ['-O2', '-DCONFIG_MOONWATER_WAYLAND']),
    ('programs/shell.c', ['-O2', '-DSHELL_NO_WAYLAND', '-DSHELL_NO_GPU']),
    ('src/build/build.c', ['-O2', '-DCONFIG_MOONWATER_WAYLAND']),
]


def check_sections(text):
    """The sections of test/checks.c that include the compositor: for each #include of it, the CHECK_ names of the innermost
       conditional around it that names any."""
    open_at, names = [], []
    for line in text.split('\n'):
        word = re.match(r'\s*#\s*(\w+)', line)
        word = word.group(1) if word else ''
        if word in ('if', 'ifdef', 'ifndef'):
            open_at.append(line)
        elif word in ('elif', 'else') and open_at:
            open_at[-1] = line
        elif word == 'endif' and open_at:
            open_at.pop()
        elif word == 'include' and re.search(r'"[^"]*src/canvas/compositor\.c"', line):
            found = next((re.findall(r'\bCHECK_\w+', at) for at in reversed(open_at) if re.search(r'\bCHECK_\w+', at)), [])
            if not found:
                raise Refused('test/checks.c includes the compositor in no CHECK_ section: %s' % line.strip())
            names += [name for name in found if name not in names]
    if not names:
        raise Refused('test/checks.c includes the compositor nowhere: the sections to hold the split to are not known')
    return names

# The tools that name compositor.c by path, and what the split does to each.
DIFFERENTIAL = 'test/differential.py'
PERFORMANCE = 'test/performance_coverage.py'
MUTATIONS = 'test/ui_mutations.py'
REPORTED = [
    ('test/checks.c', 'splash_check_source("src/canvas/compositor.c", ...) still passes, but looks at compositor.c alone: '
     'the "no splash in the compositor" row should read the parts too'),
    ('.github/SECURITY.md', 'names src/canvas/compositor.c for compositor_clip_*, which is manage.c after the split'),
    ('test/function_audit.seal', 'the digest moves (resealed by --apply); the inventory count does not'),
    ('test/run', 'src/canvas/* already covers src/canvas/compositor/*.c in the seams; gpu_kernels_wanted greps the includers '
     'of gpu.c, which stay compositor.c'),
    ('test/gpu_model/card.c', 'includes src/canvas/gpu.c, which the split does not move'),
]


class Refused(Exception):
    pass


# The mutations of ui_mutations.py that edit compositor.c and do not apply before the split either.
STALE = []


def line_states(text):
    """For each line, from 1: whether a cut may go before it, which is outside every comment, string, directive and brace, and
       inside no conditional but the file's own guard. Returns the list (index 0 unused) and the guard's depth."""
    states = [False]
    in_block = continued = False
    brace = cond = guard = 0
    lines = text.split('\n')
    for at, line in enumerate(lines):
        states.append((not in_block and not continued, brace, cond))
        if continued:
            continued = line.endswith('\\')
            continue
        if not in_block and line.lstrip().startswith('#'):
            word = re.match(r'\s*#\s*(\w+)', line)
            word = word.group(1) if word else ''
            if word in ('if', 'ifdef', 'ifndef'):
                cond += 1
            elif word == 'endif':
                cond -= 1
            elif word == 'define' and not guard and re.match(r'\s*#\s*define\s+MOONWATER_CANVAS_COMPOSITOR\b', line):
                guard = cond
            continued = line.endswith('\\')
            in_block = '/*' in line and line.rfind('/*') > line.rfind('*/')
            continue
        i, n = 0, len(line)
        while i < n:
            if in_block:
                close = line.find('*/', i)
                if close < 0:
                    break
                in_block, i = False, close + 2
                continue
            if line.startswith('/*', i):
                in_block, i = True, i + 2
                continue
            if line.startswith('//', i):
                break
            c = line[i]
            if c in '"\'':
                i += 1
                while i < n and line[i] != c:
                    i += 2 if line[i] == '\\' else 1
                i += 1
                continue
            brace += c == '{'
            brace -= c == '}'
            i += 1
    if not guard:
        raise Refused('%s: no #define MOONWATER_CANVAS_COMPOSITOR guard found' % SOURCE)
    return [False] + [s[0] and s[1] == 0 and s[2] == guard for s in states[1:]], guard


def functions(text):
    """Every top-level function body: its name, the line its header starts on and the line of its closing brace."""
    tokens, _ = lex(text)
    depth, header, found, current = 0, [], [], None
    for token in tokens:
        if token.value == '{':
            if depth == 0:
                name = function_header(header)
                current = (name.value, header[0].line if header else token.line) if name else None
            depth += 1
        elif token.value == '}':
            depth -= 1 if depth else 0
            if depth == 0:
                if current:
                    found.append((current[0], current[1], token.line))
                current, header = None, []
        elif depth == 0:
            if token.value == ';':
                header = []
            else:
                header.append(token)
    return found


def cut_lines(text):
    """The line each entry of CUTS starts at."""
    lines = text.split('\n')
    clean, _ = line_states(text)
    bodies = functions(text)
    named = {}
    for name, head, close in bodies:
        named.setdefault(name, []).append((head, close))
    starts = []
    for part, start, _ in CUTS:
        if start.startswith('/*'):
            found = [at + 1 for at, line in enumerate(lines) if line.strip() == start]
            if len(found) != 1:
                raise Refused('%s: the section line %r is there %d times, not once' % (part, start, len(found)))
            at = found[0]
            if not clean[at]:
                raise Refused('%s: the section line %r (line %d) is inside a comment, a brace or a conditional' % (part, start, at))
            starts.append(at)
            continue
        if len(named.get(start, [])) != 1:
            raise Refused('%s: the function %s is defined %d times in %s, not once' % (part, start, len(named.get(start, [])), SOURCE))
        head = named[start][0][0]
        before = max([close for _, h, close in bodies if close < head] or [0])
        at = next((line for line in range(before + 1, head + 1) if clean[line]), None)
        if at is None:
            raise Refused('%s: no line between the end of the function before %s (line %d) and its header (line %d) is outside '
                          'every conditional: name the function the conditional starts with instead' % (part, start, before, head))
        # The blank lines and a section's end line after the function before stay with it; a run that stays in compositor.c
        # takes the blank lines before it, so it stands apart from the #include lines above it.
        while at < head and (not lines[at - 1].strip() or END_MARKER.match(lines[at - 1])):
            at += 1
        while part == 'compositor.c' and at - 1 > before and not lines[at - 2].strip() and clean[at - 1]:
            at -= 1
        if not clean[at]:
            raise Refused('%s: the cut before %s (line %d) is not at a clean line' % (part, start, at))
        starts.append(at)
    for (part, start, _), at, (next_part, next_start, _), next_at in zip(CUTS, starts, CUTS[1:], starts[1:]):
        if next_at <= at:
            raise Refused('%s (%s, line %d) does not come after %s (%s, line %d): CUTS is in the file\'s order' %
                          (next_part, next_start, next_at, part, start, at))
    return starts


def split(text):
    """The new compositor.c and {part: text}, and the plan: (part, first line, last line, functions)."""
    lines = text.splitlines(keepends=True)
    starts = cut_lines(text)
    bounds = list(zip(starts, starts[1:] + [len(lines) + 1]))
    bodies = functions(text)
    head = ''.join(lines[:starts[0] - 1])
    main, parts, plan = [head], {}, []
    seen = set()
    for (part, start, what), (first, after) in zip(CUTS, bounds):
        run = ''.join(lines[first - 1:after - 1])
        inside = [name for name, h, _ in bodies if first <= h < after]
        plan.append((part, first, after - 1, inside))
        if part == 'compositor.c':
            main.append(run)
            continue
        if part in seen:
            raise Refused('%s is named twice in CUTS: a part is one run of the file' % part)
        seen.add(part)
        parts[part] = INCLUDE.sub(r'\1../', run)
        main.append('#include "compositor/%s" // %s\n' % (part, what))
    return ''.join(main), parts, plan


def flatten(main, parts):
    """compositor.c with each part in the place of its #include, the inverse of split."""
    return PART_LINE.sub(lambda m: UNINCLUDE.sub(r'\1', parts[m.group(1)]), main)


# ---- the tools that name compositor.c ----

def compositor_text_source():
    return '''def compositor_text(root):
    """src/canvas/compositor.c as one text, each of its parts (src/canvas/compositor/*.c, which it includes in its order) put
       back in the place of its #include: the text the sections below are read from, as it was before the file was cut in
       parts (test/compositor_split.py)."""
    def part(match):
        text = (root / "src/canvas/compositor" / match.group(1)).read_text()
        return re.sub(r'^(\\s*#\\s*include\\s+")\\.\\./', r'\\1', text, flags=re.M)
    return re.sub(r'^#include "compositor/([\\w.]+)".*\\n', part, (root / "src/canvas/compositor.c").read_text(), flags=re.M)


'''


def rewrite_differential(text):
    read = '(root / "src/canvas/compositor.c").read_text()'
    if read not in text:
        return text
    first = text.index(read)
    start = text.rfind('\ndef ', 0, first) + 1
    return text[:start] + compositor_text_source() + text[start:].replace(read, 'compositor_text(root)')


def rewrite_performance(text, main, parts):
    """A row whose evidence is compositor.c names the file of the split that holds all its routines (compositor.c first)."""
    files = [(SOURCE, main)] + [('%s/%s' % (PARTS, part), body) for part, body in parts.items()]

    def one(match):
        names = match.group(3).split()
        held = [path for path, body in files if all(re.search(r'\b%s\b' % re.escape(name), body) for name in names)]
        if not held:
            raise Refused('%s: no file of the split compositor names all of %s' % (PERFORMANCE, ' '.join(names)))
        return match.group(1) + held[0] + match.group(2) + match.group(3) + match.group(4)
    return re.sub(r"(cover\('\w+', ')src/canvas/compositor\.c(', ')([^']*)(')", one, text)


def p_text(path, main, parts):
    return main if path == SOURCE else parts[path.rsplit('/', 1)[1]]


def evaluate(node, names):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return names[node.id]
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return evaluate(node.left, names) + evaluate(node.right, names)
    raise Refused('%s: line %d: an argument the split cannot read' % (MUTATIONS, node.lineno))


def mutations(text):
    """The mutations of test/ui_mutations.py: (path variable node, path, old)."""
    tree = ast.parse(text)
    names, rows = {}, []
    for statement in tree.body:
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1 and isinstance(statement.targets[0], ast.Name):
            target = statement.targets[0].id
            if isinstance(statement.value, ast.Constant) and isinstance(statement.value.value, str):
                names[target] = statement.value.value
            elif target == 'M' and isinstance(statement.value, ast.List):
                for row in statement.value.elts:
                    if isinstance(row, ast.Tuple) and len(row.elts) == 4 and isinstance(row.elts[0], ast.Name):
                        rows.append((row.elts[0], names[row.elts[0].id], evaluate(row.elts[2], names), evaluate(row.elts[1], names)))
    return names, rows


def rewrite_mutations(text, main, parts, original):
    """Each mutation of compositor.c names the file of the split its text is in. One whose text is not exactly once in
       compositor.c before the split does not apply now (ui_mutations.py says NOT APPLIED), and is left as it is."""
    names, rows = mutations(text)
    lines = text.splitlines(keepends=True)
    edits, wanted = [], {}
    for node, path, old, name in rows:
        if path != SOURCE:
            continue
        if original.count(old) != 1:
            STALE.append(name)
            continue
        found = [p for p in [SOURCE] + ['%s/%s' % (PARTS, part) for part in parts] if p_text(p, main, parts).count(old) == 1]
        if len(found) != 1:
            raise Refused('%s: the mutation %r finds its text in %d files of the split compositor, not one: a cut falls inside '
                          'it, or it is there more than once' % (MUTATIONS, name, len(found)))
        if found[0] == SOURCE:
            continue
        variable = 'C_' + found[0].rsplit('/', 1)[1][:-2].upper()
        wanted[variable] = found[0]
        edits.append((node.lineno, node.col_offset, len(node.id), variable))
    for line, column, length, variable in sorted(edits, reverse=True):
        lines[line - 1] = lines[line - 1][:column] + variable + lines[line - 1][column + length:]
    text = ''.join(lines)
    where = re.search(r'^C = "src/canvas/compositor\.c"\n', text, re.M)
    if wanted and not where:
        raise Refused('%s: no line C = "src/canvas/compositor.c" to put the parts\' names beside' % MUTATIONS)
    if wanted:
        text = text[:where.end()] + ''.join('%s = "%s"\n' % item for item in sorted(wanted.items())) + text[where.end():]
    return text


# ---- the checks ----

def run(command, cwd, **options):
    return subprocess.run(command, cwd=str(cwd), capture_output=True, **options)


def preprocessed(tree, source, flags):
    done = run(['gcc', '-E', '-P'] + flags + [source], tree)
    if done.returncode:
        raise Refused('gcc -E %s %s in %s failed:\n%s' % (' '.join(flags), source, tree,
                                                         done.stderr.decode(errors='replace')[-2000:]))
    return done.stdout


# The indentation of a line of the preprocessor's output, which carries nothing: gcc -E prints a line at its column while it
# tracks columns and with one space after.
INDENT = re.compile(rb'^[ \t]+', re.M)


def indented(a, b):
    return sum(x != y for x, y in zip(a.split(b'\n'), b.split(b'\n')))


def first_line(a, b):
    return next(at + 1 for at, (x, y) in enumerate(zip(a.split(b'\n'), b.split(b'\n'))) if x != y)


def first_difference(a, b):
    a, b = a.split(b'\n'), b.split(b'\n')
    for at, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return 'line %d: before %r, after %r' % (at + 1, x[:160], y[:160])
    return 'one is a prefix of the other (%d and %d lines)' % (len(a), len(b))


def tracked():
    done = run(['git', 'ls-files', '-z'], ROOT)
    if done.returncode:
        raise Refused('git ls-files failed: %s' % done.stderr.decode(errors='replace'))
    return [name for name in done.stdout.decode().split('\0') if name]


def copy_tree(into):
    """The tracked files as they are on disk, and the built GPU kernels gpu.c includes: copies and not links, since the tools
       run in the copy write their seals where they stand."""
    for name in tracked() + ['artifacts/gpu_kernels.inc']:
        source, target = ROOT / name, into / name
        if not source.is_file():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def write(tree, name, text):
    path = tree / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    path.write_text(text)


def outputs(tree):
    """What the split writes into tree, from tree's own compositor.c and tools."""
    text = (tree / SOURCE).read_text()
    main, parts, plan = split(text)
    if flatten(main, parts) != text:
        raise Refused('the parts put back in their places are not compositor.c as it was')
    written = {SOURCE: main}
    written.update({'%s/%s' % (PARTS, part): body for part, body in parts.items()})
    differential = (tree / DIFFERENTIAL).read_text()
    written[DIFFERENTIAL] = rewrite_differential(differential)
    written[PERFORMANCE] = rewrite_performance((tree / PERFORMANCE).read_text(), main, parts)
    written[MUTATIONS] = rewrite_mutations((tree / MUTATIONS).read_text(), main, parts, text)
    return written, plan, text


def audit(tree, *flags):
    done = run([sys.executable, 'test/function_audit.py'] + list(flags), tree)
    out = done.stdout.decode(errors='replace') + done.stderr.decode(errors='replace')
    if done.returncode:
        raise Refused('function_audit.py %s failed in %s:\n%s' % (' '.join(flags), tree, out[-2000:]))
    return out


def audit_count(tree, seal):
    """The inventory's count of C functions in tree; with seal, the seal written there first and held to it."""
    if seal:
        audit(tree, '--seal')
        audit(tree, '--check')
    count = re.search(r'^\s*(\d+)\s+TOTAL C FUNCTIONS', audit(tree, '--summary'), re.M)
    if not count:
        raise Refused('function_audit.py --summary in %s gave no total' % tree)
    return count.group(1)


def check(tree):
    """The split made in tree (a copy) and held to every check; answers what was written."""
    before_count = audit_count(ROOT, False)
    written, plan, original = outputs(tree)
    for name, text in written.items():
        write(tree, name, text)
    say('the parts put back are compositor.c byte for byte')

    sections = check_sections((ROOT / 'test/checks.c').read_text())
    for source, flags in VIEWS + [('test/checks.c', FREESTANDING + ['-D' + name]) for name in sections]:
        before, after = preprocessed(ROOT, source, flags), preprocessed(tree, source, flags)
        label = 'gcc -E -P %s %s' % (' '.join(flags), source)
        if before == after:
            say('%s: byte-identical (%d bytes, sha256 %s)' % (label, len(after), hashlib.sha256(after).hexdigest()[:16]))
            continue
        if INDENT.sub(b'', before) != INDENT.sub(b'', after):
            raise Refused('%s differs after the split: %s' % (label, first_difference(INDENT.sub(b'', before), INDENT.sub(b'', after))))
        say('%s: identical but for the indentation of %d lines from line %d (%d bytes): GCC lays a line out at its '
            'column only while it still tracks columns, which it stops doing once a translation unit this large has used '
            'their room (LINE_MAP_MAX_LOCATION_WITH_COLS), and how soon depends on how the text is cut in files; every '
            'token and every line is the same' % (label, indented(before, after), first_line(before, after), len(after)))

    after_count = audit_count(tree, True)
    if after_count != before_count:
        raise Refused('the function audit counts %s functions before and %s after' % (before_count, after_count))
    say('function audit: %s C functions before and after, the seal written in the copy' % after_count)
    for tool in ('dropped_results', 'one_way_state', 'performance_coverage', 'specialization_coverage'):
        done = run([sys.executable, 'test/%s.py' % tool], tree)
        if done.returncode:
            raise Refused('test/%s.py fails after the split:\n%s' % (tool, (done.stdout + done.stderr).decode(errors='replace')[-2000:]))
        say('test/%s.py passes' % tool)

    _, rows = mutations((tree / MUTATIONS).read_text())
    held = 0
    for _, path, old, name in rows:
        if (path == SOURCE or path.startswith(PARTS + '/')) and name not in STALE:
            if (tree / path).read_text().count(old) != 1:
                raise Refused('%s: the mutation %r is not exactly once in %s' % (MUTATIONS, name, path))
            held += 1
    say('test/ui_mutations.py: each of its %d compositor mutations finds its text once in the file it names' % held)
    for name in STALE:
        say('test/ui_mutations.py: %r does not apply before the split either (its text is not once in compositor.c): '
            'left naming compositor.c' % name)

    scope = {'re': re}
    exec(compile(compositor_text_source(), DIFFERENTIAL, 'exec'), scope)
    if scope['compositor_text'](tree) != original:
        raise Refused('%s: compositor_text does not give the compositor as it was' % DIFFERENTIAL)
    if 'compositor_text(root)' not in written[DIFFERENTIAL] and '"src/canvas/compositor.c"' in written[DIFFERENTIAL]:
        raise Refused('%s still reads compositor.c alone' % DIFFERENTIAL)
    say('test/differential.py reads the compositor through compositor_text, which gives it as it was')
    return written


def say(text):
    print('  ' + text)


def print_plan(plan, text):
    lines = text.count('\n')
    print('%s, %d lines, cut in %d runs:' % (SOURCE, lines, len(plan)))
    print('  %-14s %6s %6s %6s  %4s  %s' % ('file', 'from', 'to', 'lines', 'fns', 'first and last function'))
    for part, first, last, inside in plan:
        print('  %-14s %6d %6d %6d  %4d  %s .. %s' % (part, first, last, last - first + 1, len(inside),
                                                     inside[0] if inside else '-', inside[-1] if inside else '-'))
    tools = [tool for tool in (DIFFERENTIAL, PERFORMANCE, MUTATIONS) if SOURCE in (ROOT / tool).read_text()]
    print('the tools that name %s now, which the split moves to the new paths: %s' % (SOURCE, ', '.join(tools) or 'none'))
    print('the ones it does not, and why:')
    for path, why in REPORTED:
        print('  %s: %s' % (path, why))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='make the split in a copy and hold it to its checks')
    mode.add_argument('--apply', action='store_true', help='--check, then write the split into this tree')
    parser.add_argument('--keep', metavar='DIR', help="make --check's copy in DIR (new or empty) and leave it there")
    arguments = parser.parse_args()
    try:
        text = (ROOT / SOURCE).read_text()
        _, _, plan = split(text)
        print_plan(plan, text)
        if not (arguments.check or arguments.apply):
            return 0
        if arguments.apply:
            changed = run(['git', 'status', '--porcelain', '--', SOURCE, PARTS, DIFFERENTIAL, PERFORMANCE, MUTATIONS,
                           'test/function_audit.seal'], ROOT).stdout.decode()
            if changed.strip() or (ROOT / PARTS).exists():
                raise Refused('the tree has changes the split would write over, or %s is already there:\n%s' % (PARTS, changed))
        if not (ROOT / 'artifacts/gpu_kernels.inc').is_file():
            raise Refused('artifacts/gpu_kernels.inc is not built (sh build.sh gpu-kernels): gcc -E reaches gpu.c, which includes it')
        print('checks, in a copy of the tree:')
        if arguments.keep:
            tree = pathlib.Path(arguments.keep).resolve()
            if tree.exists() and any(tree.iterdir()):
                raise Refused('%s is not empty' % tree)
            copy_tree(tree)
            written = check(tree)
        else:
            with tempfile.TemporaryDirectory(prefix='compositor-split-') as scratch:
                tree = pathlib.Path(scratch)
                copy_tree(tree)
                written = check(tree)
        print('the split holds')
        if arguments.apply:
            for name, body in written.items():
                write(ROOT, name, body)
            audit_count(ROOT, True)
            print('written: %s, and test/function_audit.seal' % ', '.join(sorted(written)))
            print('next: build and run CHECK_terminal (and the gate), then commit these files as one commit')
            print('to do, by hand, which the split does not rewrite:')
            for path, why in REPORTED[:2]:
                print('  %s: %s' % (path, why))
        return 0
    except Refused as refusal:
        print('compositor_split: %s' % refusal, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
