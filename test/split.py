#!/usr/bin/env python3
"""Cut a file many people edit at once into parts it includes in place, by a cut list of names.

    python3 test/split.py                        the targets, each with its plan; nothing written
    python3 test/split.py FILE                   FILE's plan: each part, where it starts and ends, what it holds, and the
                                                 tools that name FILE and what the split does to each; nothing written
    python3 test/split.py FILE --check           the split made in a copy of the tree and held to the checks below; this
                                                 tree is not touched
    python3 test/split.py FILE --apply           --check, then the same split written into this tree, the tools that name
                                                 FILE moved to the new paths, and the function audit resealed
    python3 test/split.py FILE --carry BRANCH [--split REV]
                                                 BRANCH's commits since the commit the split was made on, each cut the same
                                                 way, as commits on the split: refs/heads/carried/BRANCH
    python3 test/split.py FILE --prove BRANCH    --carry onto a scratch split of where BRANCH left main, and the carried tree
                                                 held to the views of BRANCH unsplit; nothing is kept

FILE stays the one translation unit it is: each part holds one run of it, in its order, and FILE includes the part where
the run was, so every static, macro and section is where it was and nothing is declared again. The cut list of each target
(TARGETS) is names, not line numbers: a part starts at a function (by its name) or at a line (by its text, such as a
section's #ifdef CHECK_<name> or a /* ---- ... ---- */ heading), and the statics, macros and comments between that and the
function or section before it go with it, so the list runs on whatever main is that minute. A part ends where the next
entry starts, or where the conditional branch it started in ends; what is not in a part (and every run the list gives to
FILE itself) stays FILE's own text. A part opens with a header saying what it holds and that it is FILE's; a quote include
in it that FILE's folder resolves is given "../", one found through -I is left as it is.

The split is held to (--check, in a copy):
  - FILE with each part put back where its #include is (its header off and its includes' "../" taken away) is the file it
    was, byte for byte;
  - gcc -E -P of every view of FILE (the shell with the types row's defines, and without the Wayland shim and the card,
    the build tool, and each section of test/checks.c that reaches FILE: for test/checks.c itself, every section) is
    byte-identical before and after, or identical once __LINE__ and __FILE__ are taken as names (a part's lines are its
    own: a row that prints __LINE__ prints its line in the part), or, for a unit so large GCC stops tracking columns
    before its end, identical but for the indentation GCC lays its last lines out with: every token and line the same;
  - the function audit counts the same functions, and dropped_results, one_way_state, performance_coverage and
    specialization_coverage give what they gave;
  - every tool that names FILE by path is moved (see TOOLS below) and still reads what it read: a Python tool's reads give
    FILE's text as it was, test/run's reads give the lines they gave and its seams put the parts where FILE is, and a
    mutation tool's scratch tree links or copies the parts beside FILE, which is built in a tree laid out that way.

The split must be applied when nobody has uncommitted edits in FILE: --apply refuses a tree where FILE or a tool it
rewrites differs from HEAD, or where FILE's folder of parts is already there. A branch made before the split is carried
across it with --carry: rebase it onto the commit before the split, commit everything, then
    python3 test/split.py FILE --carry BRANCH
which writes refs/heads/carried/BRANCH (one commit for each of BRANCH's, its message, author and date kept) and changes no
working tree; then, in BRANCH's worktree, git reset --hard carried/BRANCH and rebase onto main. It refuses, changing
nothing, a BRANCH that does not descend from the commit the split was made on, a commit that loses, renames, doubles or
reorders an entry of the cut list, a function or section added between two parts (its part is not known: move it after
something of the part it belongs to), and a change outside FILE that does not apply.
"""

import argparse
import ast
import collections
import hashlib
import os
import pathlib
import posixpath
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import textwrap

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'test/assembly'))
from inventory import function_header, lex  # noqa: E402


class Refused(Exception):
    pass


Target = collections.namedtuple('Target', 'source cuts views note')

# ---- the targets ----
#
# Each cut list is in the file's order: an entry starts a run at a function (its name) or at a line (its text, stripped; a
# text ending in ' ...' is a line's beginning, and (text, n, count) the nth of count such lines), and the run goes on to
# the next entry or to the end of the branch it started in. A run named for the file itself stays in it, as does the text
# before the first entry. An entry's words are what its part holds, and an entry may add a line to its header (how its
# sections are run); the target's note closes every header. None names a CHECK_, BENCH_ or SHARED_ word, which test/run's
# readers count.

CHECKS = 'test/checks.c'
HOST = 'src/sh/host.c'

FACES = ("The terminal's program, the kit's frames, the draw list and the GPU model's kit run with the four faces: "
         'fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf '
         'fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.')
BLOCK = ("One part of the program that the terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model "
         "kit's sections share (checks.c opens and closes it). " + FACES)

TARGETS = {
    CHECKS: Target(CHECKS, [
        ('standard.c', '#ifdef CHECK_standard', "the standard's own library: the C library's names and what each does"),
        ('verify.c', '#ifdef CHECK_verify', "the library's assembly bodies held to their C, at every size and alignment"),
        ('memory.c', '#ifdef CHECK_exact',
         'the memory routines at literal sizes: exact copies and sets, needles, bounded and exact scans, prefixes, case, '
         'the text and shell assembly, hashes, bases, folds, the allocator, reserve and UTF-8'),
        ('numbers.c', '#ifdef CHECK_hex',
         'hex, codecs, fixed point, tables, errors, strings, stdlib, leaving, numbers, clocks, math, matrices, modulo, '
         'the trainer, the ranged routines and the environment'),
        ('stdio.c', '#ifdef CHECK_format_standalone', 'format and scan, streams and spools, and their glibc twins'),
        ('process.c', '#ifdef CHECK_process', 'processes, signals, locks and sockets'),
        ('net.c', '#ifdef CHECK_net', 'the network stack, and the crypto vectors it is held to'),
        ('waterlink.c', '#ifdef CHECK_waterlink', 'the waterlink and its service'),
        ('bowl.c', '#ifdef CHECK_bowl', 'bowls'),
        ('compress.c', '#ifdef CHECK_tar', 'tar, zstd, gzip and xz'),
        ('shell.c', '#ifdef CHECK_reuse_shell',
         "the shell's reuse, its path stage, the builtins' regressions, slurp, the wait retry and the writer's buffer"),
        ('storage.c', '#ifdef CHECK_storage_io', "storage's I/O and its formats"),
        ('machine.c', '#ifdef CHECK_machine', 'the machine, snapshots, sensors and the browser'),
        ('tensor.c', ('#ifdef CHECK_tensor', 1, 2), 'the tensor routines'),
        ('font.c', '#ifdef CHECK_font', 'the font engine and shaping'),
        (CHECKS.rsplit('/', 1)[1], '#if defined(CHECK_terminal) || ...',
         'the terminal, the kit and the compositor: one program for the sections that share it'),
        ('terminal.c', 'host_terminal_opening',
         "that program's fakes (the kernel's DRM and the GPU model's hook, the card, a popup's row, what the kit hands the "
         "font engine) and the terminal's rows: its frames, the selection and its scroll, the kit's keys, the pointer a "
         'program asked for', BLOCK),
        ('compositor.c', 'check_take',
         "the compositor's rows: a first show, glass, partial presents and runs, scanout, screens and scales, corners and "
         'turns, the snap preview, shared edges, Alt-Tab, the desktop, the taskbar and its launcher, bands',
         "Rows of the compositor's compose.c, glass.c, damage.c, scanout.c, present.c and outputs.c. " + BLOCK),
        ('compositor_hand.c', 'check_compositor_click',
         "the compositor and the hand: a held button, the cursor, close buttons, popups and menus, dropdowns, the tray, a "
         "tab's reach, the launcher button, the pointer's motion, prediction, the wheel, volume, notices, the chrome",
         "Rows of the compositor's pointer.c, input.c, manage.c and compose.c. " + BLOCK),
        ('compositor_windows.c', 'life_colour',
         "the compositor's windows: their lifecycle, the screens they are on, the card and the table, the snap, tiles, the "
         'grid and dividers, the lit edge',
         "Rows of the compositor's manage.c, outputs.c, kms.c and session.c. " + BLOCK),
        ('compositor_touch.c', '#ifdef COMPOSITOR_LINE',
         "touch through the compositor, the snap's frames, the touch path, auto-scroll, the caret, scrolling, flicks, the "
         "wheel and the throw, the trace, the screen's edges",
         "Rows of the compositor's input.c, manage.c and damage.c. " + BLOCK),
        ('compositor_places.c', 'place_same', 'where a window is put and kept, and the glass over a wallpaper',
         "Rows of the compositor's manage.c and glass.c. " + BLOCK),
        ('settings.c', 'settings_parsed', "the settings window's model, its rows and its pages", BLOCK),
        ('kit.c', 'check_killed', "the kit's widgets, their motion and the hand on them", BLOCK),
        ('apps.c', '#ifdef CHECK_kit_frames', "the kit's frames, and the terminal's own program: fields, inputs, the editor, "
         'menus, languages, the identify app and the terminal against a full draw', BLOCK),
        ('probe.c', '#ifdef CHECK_probe', 'the probe and the stack guard'),
        ('checksum.c', '#ifdef CHECK_checksum_crc', 'checksums and CRCs, and their benchmarks'),
        ('spark.c', '#ifdef BENCH_unicode_width', "the Unicode width and link benchmarks, and spark's entry"),
        ('canvas.c', '#ifdef CHECK_text_engine', "the text engine and the canvas's cells"),
        ('wayland.c', '#ifdef CHECK_wayland', "the Wayland shim and the X window manager's wire"),
        ('dbus.c', '#ifdef CHECK_dbus', 'the session bus'),
        ('audiolib.c', '#ifdef CHECK_audiolib', "the audio library's routines"),
        ('audio.c', '#ifdef CHECK_audio_mix', 'the sound server: its mixer, ALSA, PulseAudio, the service, control and state'),
        ('native.c', '#ifdef CHECK_stdio_buffer', 'the stdio buffer and the native routines built against generated headers'),
        ('edit.c', '#ifdef CHECK_edit_regressions', "the editor's regressions, the lexer's spans and regular expressions"),
        ('bench.c', '#ifdef BENCH_floor', 'the benchmarks: the floor, numbers and bases, and pixels and memory'),
        ('bench_text.c', '#ifdef BENCH_ranged', "the benchmarks of the ranged routines, text, numbers and the network's parsers"),
        ('bench_system.c', '#ifdef BENCH_clock',
         'the benchmarks of the clock, randomness and limbs, the allocator and locks, streams, startup and exec'),
        ('floors.c', '#if defined(CHECK_compression_floor) || defined(BENCH_compression_floor)',
         'the floors: compression, the hardware, libc and inlining'),
        ('hosted.c', '#ifdef CHECK_hwsim_radio', 'the static probes a booted image runs: the radio, ring 0 and the firewall'),
        ('gpu.c', '#ifdef CHECK_gpu', 'the card and its kit'),
        ('draw.c', '#ifdef CHECK_issues', 'the issue board, draw lists and the GPU assembly', FACES),
        ('coverage.c', '#ifdef COVERAGE_hook', 'the hook a coverage build calls at each basic block'),
        ('gpu_model.c', '#ifdef CHECK_gpu_model', "the model of the card held to the card and to itself, and the GPU kit's "
         'rows drawn on it', FACES),
        ('routine.c', '#ifdef BENCH_routine', "the routine driver's benchmark"),
        ('libdiff.c', '#ifdef CHECK_libdiff', "the library's memory routines held to C (libdiff)"),
    ], 'sections',
        'A section is built as test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib '
        '-nostartfiles -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its '
        'freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name> '
        'test/checks.c).'),
    HOST: Target(HOST, [
        ('disk.c', 'host_watch_open',
         "the disk: the state directory and boot's verdict, the lock, the census of disks, images and their builds, the "
         "place steps an image is written by (host_place_*), the splash an install lays, a disk taken, boot's settle and "
         "its question, and setup install's work (host_install_locked); the setup verbs are page.c's"),
        ('image.c', 'host_spark_once',
         "the settings an image carries and a disk keeps (host_settings_*): read, written, carried and applied; and "
         "spark's bind"),
        ('boot.c', 'host_event_environment', "boot's service, its events and the shells it starts; the verbs canvas, latency "
         'and bios'),
        ('nl80211.c', '/* ---- radio: wifi and bluetooth, and the nl80211 they ask the kernel through. ---- */',
         "the radio's words to the kernel: nl80211, a station joined and left and the process that keeps it (the keeper), "
         "WPA's and SAE's handshakes; the wifi verb is radio.c's"),
        ('radio.c', 'radio_net_wake',
         'the verbs wifi, bluetooth, priority and wired: the switches, the saved networks, the air scanned, a join, its '
         'password and its state files (wifi.last), the status, the shared link'),
        ('tune.c', '/* ---- tune: the switches the kernel keeps in sysfs. ---- */',
         'the verbs airplane, brightness, charge, battery, power, cpu, sleep and hibernate: the switches the kernel keeps '
         'in sysfs'),
        ('device.c', '/* ---- device: which devices plugged in are let in. ---- */',
         'the verb device: which devices plugged in are let in, the rules, the census, the lock, the watch, the list'),
        ('locale.c', '/* ---- locale: the timezone and the clock, and the SNTP that sets it. ---- */',
         "the timezone and the clock (SNTP, NTS, the zone found and set), the keyboard's layout and the canvas's modes: "
         "the work under the verbs time, timezone, ntp and keyboard, which are debug.c's"),
        ('setting.c', 'host_splash_vendor',
         "the verbs splash, taskbar, mouse, language and audio, and the canvas's settings; the clock kept and restored at "
         'boot, and the name'),
        ('debug.c', 'host_debug_hex', 'the verbs debug, notices, sensors, name, time, timezone, ntp and keyboard'),
        ('desktop.c', 'desktop_choice', 'the verb desktop: the desktop a machine starts, and which one'),
        ('security.c', 'dns_tls_publish', 'the verbs dns, security and bind'),
        ('page.c', 'host_title',
         'the verbs setup (install, update, update usb, rollback, use, live, removable), status, help, wipe, machine, '
         'boot and ask: the page of verbs and its rows, the sticks and an update from one, a rollback, a wipe'),
        (HOST.rsplit('/', 1)[1], 'host_canvas_identify',
         'the verbs settings, window and clipboard, canvas identify, and host_main, which hands each verb to its part'),
    ], 'shell', 'host_main, at the end of src/sh/host.c, names the function each verb runs.'),
}

END_MARKER = re.compile(r'\s*/\* ---- end of .* ---- \*/\s*$')
INCLUDE_LINE = re.compile(r'^(\s*#\s*include\s+")([^"]*)(")', re.M)
UNINCLUDE = re.compile(r'^(\s*#\s*include\s+")\.\./', re.M)
FREESTANDING = ['-O2', '-march=x86-64', '-fno-builtin', '-fno-stack-protector']
SECTION_WORD = re.compile(r'\b((?:CHECK|BENCH|SHARED|COVERAGE)_\w+)')


def parts_dir(source):
    return source[:-2]


def stem(source):
    return source.rsplit('/', 1)[1][:-2]


def own(source):
    return source.rsplit('/', 1)[1]


# ---- reading a file ----

def layout(text):
    """For each line from 1: whether the line starts outside every comment, string, continued directive and brace; the
       conditionals open at its start, each (the line it opened on, its branch); and its directive's word."""
    clean, stacks, words = [None], [None], [None]
    in_block = continued = False
    brace = 0
    stack, braces = [], []      # each branch counts its braces from where its conditional opened; the first one's stand
    for at, line in enumerate(text.split('\n'), 1):
        clean.append(not in_block and not continued and brace == 0)
        stacks.append(tuple(stack))
        if continued:
            continued = line.endswith('\\')
            words.append('')
            continue
        if not in_block and line.lstrip().startswith('#'):
            found = re.match(r'\s*#\s*(\w+)', line)
            word = found.group(1) if found else ''
            if word in ('if', 'ifdef', 'ifndef'):
                stack.append((at, 0))
                braces.append([brace, None])
            elif word in ('elif', 'else') and stack:
                stack[-1] = (stack[-1][0], stack[-1][1] + 1)
                if braces[-1][1] is None:
                    braces[-1][1] = brace
                brace = braces[-1][0]
            elif word == 'endif' and stack:
                stack.pop()
                opened, first = braces.pop()
                brace = brace if first is None else first
            continued = line.endswith('\\')
            in_block = '/*' in line and line.rfind('/*') > line.rfind('*/')
            words.append(word)
            continue
        words.append('')
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
    return clean, stacks, words


def functions(text):
    """Every top-level function body: its name, the line its header starts on and the line of its closing brace. The
       braces of a conditional's branches are each counted from where it opened, and the first branch's count stands
       after it, so a function whose opening differs by machine (#if X64 ... { #else ... { #endif) is one body."""
    tokens, directives = lex(text)
    depth, header, found, current = 0, [], [], None
    marks, at = [], 0
    for token in tokens:
        while at < len(directives) and directives[at].start < token.start:
            word = re.match(r'\s*#\s*(\w*)', directives[at].text).group(1)
            if word in ('if', 'ifdef', 'ifndef'):
                marks.append([depth, None])
            elif word in ('elif', 'else') and marks:
                if marks[-1][1] is None:
                    marks[-1][1] = depth
                depth = marks[-1][0]
            elif word == 'endif' and marks:
                opened, first = marks.pop()
                depth = depth if first is None else first
            at += 1
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


class Shape:
    """A text read once: its lines, their layout, its functions."""

    def __init__(self, text):
        self.text = text
        self.lines = text.split('\n')
        self.clean, self.stacks, self.words = layout(text)
        self.bodies = functions(text)
        self.named = collections.defaultdict(list)
        for name, head, close in self.bodies:
            self.named[name].append(head)
        self.closes = collections.defaultdict(list)
        for _, _, close in self.bodies:
            self.closes[self.stacks[close]].append(close)
        self.ends = collections.defaultdict(list)       # the lines a unit at a level ends on: an #endif back to it
        self.openers = collections.defaultdict(list)    # the lines a branch of a level opens on
        count = len(self.lines)
        for at in range(1, count + 1):
            word = self.words[at]
            after = self.stacks[at + 1] if at + 1 <= count else ()
            if word == 'endif':
                self.ends[after].append(at)
            elif word in ('if', 'ifdef', 'ifndef', 'elif', 'else'):
                self.openers[after].append(at)

    def branch_end(self, start):
        """The line of the directive that ends the branch the line start is in (its #elif, #else or #endif), or the line
           after the last."""
        level = self.stacks[start]
        if not level:
            return len(self.lines) + 1
        for at in range(start, len(self.lines) + 1):
            if self.stacks[at] == level and self.words[at] in ('elif', 'else', 'endif'):
                return at
        return len(self.lines) + 1

    def head(self, part, anchor):
        """The line an entry's anchor is on: a function's header, or the line whose text (stripped) is the anchor, or
           starts with it when it ends in ' ...'. Of the lines it names, those least deep in conditionals count, and must
           be one; an anchor (text, n, count) is the nth of count such lines."""
        nth, count = 1, 1
        if isinstance(anchor, tuple):
            anchor, nth, count = anchor
        if anchor.startswith(('#', '/*', '//')):
            if anchor.endswith(' ...'):
                found = [at for at, line in enumerate(self.lines, 1) if line.strip().startswith(anchor[:-4])]
            else:
                found = [at for at, line in enumerate(self.lines, 1) if line.strip() == anchor]
            found = [at for at in found if self.clean[at]]
            least = min([len(self.stacks[at]) for at in found] or [0])
            found = [at for at in found if len(self.stacks[at]) == least]
            if len(found) != count:
                raise Refused('%s: the line %r is there %d times at its least depth, not %d' % (part, anchor, len(found), count))
            return found[nth - 1]
        heads = self.named.get(anchor, [])
        if len(heads) != 1:
            raise Refused('%s: the function %s is defined %d times, not once' % (part, anchor, len(heads)))
        return heads[0]

    def start(self, part, anchor, stays, lift=None):
        """The line an entry's run starts at: after the function or section before its anchor at the anchor's level, and
           the blank lines and end markers after that, so the statics, macros and comments between go with it. With lift
           (the level of the part before, which must not end inside a conditional it did not open), an anchor deeper than
           that which is the first thing in its conditional's first branch starts the run at the conditional's own line."""
        head = self.head(part, anchor)
        level = self.stacks[head]
        while lift is not None and len(level) > len(lift) and level[:len(lift)] == lift:
            opened = level[-1][0]
            if level[-1][1] != 0 or \
                    [line for line in self.closes[level] + self.ends[level] + self.openers[level] if opened < line < head]:
                raise Refused('%s: %s is inside a conditional (line %d) that has more than it, so the part before would '
                              "end inside it: name the conditional's own line instead" % (part, anchor, opened))
            head, level = opened, self.stacks[opened]
        before = max([line for line in self.closes[level] + self.ends[level] + self.openers[level] if line < head] or [0])
        at = before + 1
        while at < head and (not self.lines[at - 1].strip() or END_MARKER.match(self.lines[at - 1])):
            at += 1
        # A run that stays in the file takes the blank lines before it, so it stands apart from the #include lines above.
        while stays and at - 1 > before and not self.lines[at - 2].strip() and self.clean[at - 1]:
            at -= 1
        if not self.clean[at] or self.stacks[at] != level:
            raise Refused('%s: the cut before %s (line %d) is not at a clean line of its level' % (part, anchor, at))
        return at


Run = collections.namedtuple('Run', 'part first after what level notes')


def runs(source, shape, cuts):
    """The runs of the cut: each entry's (part, its first line, the line after its last, its words), with the text that
       stays between a run's branch end and the next entry as runs of the file itself."""
    name = own(source)
    starts = []
    for index, (part, anchor, *_) in enumerate(cuts):
        lift = None
        if index and cuts[index - 1][0] != name:
            lift = shape.stacks[starts[-1]]
        starts.append(shape.start(part, anchor, part == name, lift))
    for (part, anchor, *_), at, (next_part, next_anchor, *_), next_at in zip(cuts, starts, cuts[1:], starts[1:]):
        anchor, next_anchor = anchor if isinstance(anchor, str) else anchor[0], next_anchor if isinstance(next_anchor, str) else next_anchor[0]
        if next_at <= at:
            raise Refused("%s (%s, line %d) does not come after %s (%s, line %d): the cut list is in the file's order" %
                          (next_part, next_anchor, next_at, part, anchor, at))
    out = []
    seen = set()
    limit = len(shape.lines) + 1
    for (part, anchor, what, *notes), first, after in zip(cuts, starts, starts[1:] + [limit]):
        if part == name:
            out.append(Run(part, first, after, what, shape.stacks[first], notes))
            continue
        if part in seen:
            raise Refused('%s is named twice in the cut list: a part is one run of the file' % part)
        seen.add(part)
        end = min(after, shape.branch_end(first))
        level = shape.stacks[first]
        if (shape.stacks[end] if end <= len(shape.lines) else ()) != level:
            raise Refused('%s (%s): the next entry starts inside a conditional that opened in the run: name the '
                          "conditional's own line instead" % (part, anchor))
        out.append(Run(part, first, end, what, level, notes))
        if end < after:
            out.append(Run(name, end, after, None, shape.stacks[end], []))
    return starts[0], out


# ---- a part's text ----

def header(source, part, what, notes=()):
    def para(text):
        return textwrap.fill(text, 118, initial_indent=' ' * 8, subsequent_indent=' ' * 8, break_on_hyphens=False,
                             break_long_words=False)
    said = [para('%s/%s: %s.' % (parts_dir(source), part, what))] + [para(note) for note in notes]
    said.append(para('A run of %s, which includes this file where the run stood (test/split.py cut it there): it is '
                     'compiled only as part of that file, with its statics and macros, and never on its own. %s'
                     % (source, TARGETS[source].note)))
    return '/*\n%s\n*/\n\n' % '\n\n'.join(said)


def strip_header(body, part_stem, part):
    """A part's text without the header split.py gave it."""
    found = re.match(r'/\*\n {8}\S*%s/%s: .*?\n\*/\n\n' % (re.escape(part_stem), re.escape(part)), body, re.S)
    return body[found.end():] if found else body


def include_line(source, part, what):
    return '#include "%s/%s" // %s\n' % (stem(source), part, what)


def part_line(source):
    return re.compile(r'^#include "%s/([\w.]+)".*\n' % re.escape(stem(source)), re.M)


def moved_includes(source, run, exists):
    """The run's text as a part of source: each quote include that source's folder resolves given "../"."""
    folder = posixpath.dirname(source)

    def one(match):
        path = match.group(2)
        resolved = posixpath.normpath(posixpath.join(folder, path))
        if exists(resolved):
            return match.group(1) + '../' + path + match.group(3)
        if path.startswith('../'):
            raise Refused('%s: #include "%s" in a part does not resolve beside %s, and starts with ../: put back, '
                          'it could not be told from one that does' % (source, path, source))
        if exists(posixpath.join(parts_dir(source), path)):
            raise Refused('%s: #include "%s" is found through -I, and a part of the same name would hide it' % (source, path))
        return match.group(0)
    return INCLUDE_LINE.sub(one, run)


def split(source, text, exists):
    """The new source and {part: text}, and the plan: (part, first line, last line, functions, sections)."""
    target = TARGETS[source]
    shape = Shape(text)
    first, cut = runs(source, shape, target.cuts)
    lines = text.splitlines(keepends=True)
    main, parts, plan = [''.join(lines[:first - 1])], {}, []
    for run in cut:
        body = ''.join(lines[run.first - 1:run.after - 1])
        inside = [name for name, head, _ in shape.bodies if run.first <= head < run.after]
        sections = sorted(set(word for at in range(run.first, run.after)
                              if shape.words[at] in ('if', 'ifdef', 'ifndef', 'elif')
                              for word in SECTION_WORD.findall(shape.lines[at - 1])), key=lambda w: body.find(w))
        plan.append((run.part, run.first, run.after - 1, inside, sections))
        if run.part == own(source):
            main.append(body)
            continue
        for words in [run.what] + list(run.notes):
            if SECTION_WORD.search(words):
                raise Refused('%s: its words name %s, which the readers of test/run would count' %
                              (run.part, SECTION_WORD.search(words).group(1)))
        parts[run.part] = header(source, run.part, run.what, run.notes) + moved_includes(source, body, exists)
        main.append(include_line(source, run.part, run.what))
    return ''.join(main), parts, plan


def flatten(source, main, parts):
    """source with each part in the place of its #include, the inverse of split."""
    return part_line(source).sub(lambda m: UNINCLUDE.sub(r'\1', strip_header(parts[m.group(1)], stem(source), m.group(1))),
                                 main)


# ---- the text the tools are given ----
#
# A reader of a split file reads it whole: the same function, generic over every file split this way (FILE.c and its
# parts in FILE/, included by #include "FILE/part.c" lines), is written into each Python tool that reads one, and test/run
# reads a split file through cat of it and its parts. A part with no header (test/compositor_split.py's) is put back as
# it is.

SOURCE_TEXT = '''def source_text(path, **options):
    """path's text, and for a file test/split.py cut in parts (path.c includes each part, path/part.c, where its text
       stood) the text it had before the cut: each part put back in its place, without its header, its includes as
       path.c had them."""
    path = pathlib.Path(path)
    text = path.read_text(**options)
    folder = path.parent / path.stem
    if path.suffix != ".c" or not folder.is_dir():
        return text

    def part(match):
        body = (folder / match.group(1)).read_text(**options)
        head = re.match(r"/\\*\\n {8}\\S*%s/%s: .*?\\n\\*/\\n\\n" % (re.escape(path.stem), re.escape(match.group(1))), body, re.S)
        return re.sub(r'^(\\s*#\\s*include\\s+")\\.\\./', r"\\1", body[head.end():] if head else body, flags=re.M)
    return re.sub(r'^#include "%s/([\\w.]+)".*\\n' % re.escape(path.stem), part, text, flags=re.M)
'''


def source_text_here(path, **options):
    scope = {'re': re, 'pathlib': pathlib}
    exec(SOURCE_TEXT, scope)
    return scope['source_text'](path, **options)


def tree_reader(tree):
    return lambda name: (tree / name).read_text() if (tree / name).is_file() else None


# ---- the tools that name a file ----

class ToolEdit:
    """A tool rewritten for the split: its new text, what the check holds it to, and what was done, for the plan."""

    def __init__(self, path, text, said, proofs=()):
        self.path, self.text, self.said, self.proofs = path, text, said, list(proofs)


def python_tools(tree_files, read, source):
    """The Python tools under test/ that name source by path (outside comments and docstrings)."""
    found = []
    for name in sorted(tree_files):
        if not name.startswith('test/') or not name.endswith('.py') or name == 'test/split.py':
            continue
        text = read(name)
        if text is not None and (source in text or "'%s'" % own(source) in text or '"%s"' % own(source) in text):
            found.append(name)
    return found


SEGMENT_STARTS = {}


def segment(text, node):
    """node's source text (ast.get_source_segment, without splitting the whole text again for each node)."""
    key = id(text)
    if SEGMENT_STARTS.get(key, (None,))[0] is not text:
        SEGMENT_STARTS.clear()
        SEGMENT_STARTS[key] = (text, offsets(text))
    starts = SEGMENT_STARTS[key][1]
    begin, end = span(starts, node)
    return text[begin:end]


def offsets(text):
    starts, at = [0], 0
    for line in text.split('\n'):
        at += len(line) + 1
        starts.append(at)
    return starts


def span(starts, node):
    return starts[node.lineno - 1] + node.col_offset, starts[node.end_lineno - 1] + node.end_col_offset


def parents_of(tree):
    up = {}
    for node in ast.walk(tree):
        for child in ast.iter_child_nodes(node):
            up[child] = node
    return up


def is_flags_list(node):
    return isinstance(node, (ast.List, ast.Tuple)) and any(
        isinstance(e, ast.Constant) and isinstance(e.value, str) and e.value.startswith('-') for e in node.elts)


def runs_a_program(node, up):
    """Whether node is inside the arguments of a call that runs a program (a compile: the file is still the entry)."""
    while node in up:
        node = up[node]
        if isinstance(node, ast.Call):
            name = node.func.attr if isinstance(node.func, ast.Attribute) else getattr(node.func, 'id', '')
            if name in ('run', 'Popen', 'check_output', 'check_call', 'call', 'build_run', 'compiled'):
                return True
        if is_flags_list(node):
            return True
    return False


class Index:
    """A tool's syntax tree, with what the rewrite asks of it often: each node's parent, the loops and what they iterate,
       the loads of each name and what each name is bound to."""

    def __init__(self, tree):
        self.tree = tree
        self.up = parents_of(tree)
        self.loops, self.loads, self.bound = [], collections.defaultdict(list), collections.defaultdict(list)
        for node in ast.walk(tree):
            if isinstance(node, (ast.For, ast.comprehension)):
                self.loops.append((node, set(id(n) for n in ast.walk(node.iter))))
            elif isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load):
                self.loads[node.id].append(node)
            elif isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                self.bound[node.targets[0].id].append(node.value)


def loop_targets(index, node):
    """The names of For loops (and comprehensions) that iterate over node, directly or through the name it is bound to,
       with the statements the names are read in."""
    up = index.up
    holder = node
    while holder in up and isinstance(up[holder], (ast.List, ast.Tuple, ast.Set, ast.BinOp, ast.Dict)):
        holder = up[holder]
    sources = {id(holder)}
    owner = up.get(holder)
    if isinstance(owner, ast.Assign) and len(owner.targets) == 1 and isinstance(owner.targets[0], ast.Name):
        sources |= set(id(n) for n in index.loads[owner.targets[0].id])
    names = []
    for loop, inside in index.loops:
        if not sources & inside:
            continue
        targets = [loop.target] if isinstance(loop.target, ast.Name) else \
            [e for e in getattr(loop.target, 'elts', []) if isinstance(e, ast.Name)]
        if isinstance(holder, ast.Dict) and isinstance(loop.target, ast.Tuple):
            targets = targets[1:]   # for key, value in d.items(): the value names the file
        body = loop.body if isinstance(loop, ast.For) else [up[loop]]
        names.append(([t.id for t in targets], body))
    return names


def read_calls(nodes, receiver):
    """The .read_text(...) calls in nodes whose receiver is receiver(node) true."""
    found = []
    for statement in nodes:
        for node in ast.walk(statement):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'read_text' \
                    and receiver(node.func.value):
                found.append(node)
    return found


def is_path(node, source):
    return isinstance(node, ast.Constant) and node.value == source


def rewrite_python(path, text, source, ctx):
    """A Python tool that names source: each read of it through source_text, each copy or link of it with its parts,
       each write between markers into the part that holds them; a compile of it left as it is. Anything else that names
       it is refused, so a new use of the file is seen and taught here."""
    tree = ast.parse(text)
    index = Index(tree)
    up = index.up
    starts = offsets(text)
    edits, said, proofs, inserts = [], [], [], []
    reads = {}
    base, folder = own(source), posixpath.dirname(source).rsplit('/', 1)[-1]

    def read_edit(call):
        receiver = segment(text, call.func.value)
        arguments = [segment(text, a) for a in call.args] + ['%s=%s' % (k.arg, segment(text, k.value)) for k in call.keywords]
        reads[span(starts, call)] = 'source_text(%s)' % ', '.join([receiver] + arguments)

    def names_file(node):
        """A constant that is source, or a path or join that ends in it."""
        if is_path(node, source):
            return True
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div) and is_path(node.right, source):
            return True
        if isinstance(node, ast.Call) and getattr(node.func, 'attr', '') == 'join':
            words = [a.value for a in node.args if isinstance(a, ast.Constant) and isinstance(a.value, str)]
            return '/'.join(words).endswith(source) or words[-2:] == [folder, base] or (words and words[-1] == source)
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add) and isinstance(node.right, ast.Constant):
            return isinstance(node.right.value, str) and node.right.value.endswith('/' + source)
        return False

    unknown = []
    handled = set()
    # Copies and links of the file: the parts go beside it.
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if not isinstance(node.func, ast.Attribute) or not isinstance(node.func.value, ast.Name):
            continue
        name = node.func.value.id + '.' + node.func.attr
        if name not in ('os.symlink', 'shutil.copy', 'shutil.copy2', 'shutil.copyfile', 'os.link') or len(node.args) < 2:
            continue
        first, second = node.args[0], node.args[1]
        bound = {}
        if isinstance(first, ast.Name) and index.bound.get(first.id):
            bound[first.id] = index.bound[first.id][-1]
        if not (names_file(first) or names_file(second) or (isinstance(first, ast.Name) and first.id in bound and
                                                             names_file(bound[first.id]))):
            continue
        statement = node
        while not isinstance(statement, ast.stmt):
            statement = up[statement]
        indent = ' ' * statement.col_offset
        a, b = segment(text, first), segment(text, second)
        if name == 'os.symlink' or name == 'os.link':
            lines = ('%sfor split_part in os.listdir(os.path.join(os.path.dirname(%s), "%s")):  # %s\'s parts (test/split.py)\n'
                     '%s    os.makedirs(os.path.join(os.path.dirname(%s), "%s"), exist_ok=True)\n'
                     '%s    %s(os.path.join(os.path.dirname(%s), "%s", split_part),\n'
                     '%s    %s os.path.join(os.path.dirname(%s), "%s", split_part))\n'
                     % (indent, a, stem(source), source, indent, b, stem(source), indent, name, a, stem(source),
                        indent, ' ' * len(name), b, stem(source)))
            said.append('%s: its link of %s (line %d) links each part beside it' % (path, base, node.lineno))
        else:
            lines = ('%sshutil.copytree(os.path.join(os.path.dirname(%s), "%s"),  # %s\'s parts (test/split.py)\n'
                     '%s                os.path.join(os.path.dirname(%s), "%s"))\n' % (indent, a, stem(source), source, indent, b,
                                                                                   stem(source)))
            said.append('%s: its copy of %s (line %d) copies the parts beside it' % (path, base, node.lineno))
        inserts.append((starts[statement.end_lineno], lines))
        proofs.append(('layout', path, node.lineno, name))
        for n in ast.walk(node):
            handled.add(n)
        if isinstance(first, ast.Name) and first.id in bound:
            for n in ast.walk(bound[first.id]):
                handled.add(n)

    for node in ast.walk(tree):
        if not (isinstance(node, ast.Constant) and node.value == source) or node in handled:
            continue
        parent = up.get(node)
        # (root / "FILE").read_text(...)
        if isinstance(parent, ast.BinOp) and isinstance(parent.op, ast.Div) and parent.right is node:
            grand = up.get(parent)
            if isinstance(grand, ast.Attribute) and grand.attr == 'read_text' and isinstance(up.get(grand), ast.Call):
                read_edit(up[grand])
                continue
            if isinstance(grand, ast.Tuple) and len(grand.elts) >= 3 and all(
                    isinstance(e, ast.Constant) for e in grand.elts[1:3]) and isinstance(grand.elts[2].value, str):
                # A place written between markers: the part that holds the end marker.
                marker = grand.elts[2].value
                holders = [p for p, body in ctx['parts'].items() if marker in body]
                if marker in ctx['main'] or len(holders) != 1:
                    unknown.append('line %d: a write between markers whose end %r is not in one part' % (node.lineno, marker))
                    continue
                edits.append((span(starts, node), '"%s/%s"' % (parts_dir(source), holders[0])))
                said.append('%s: its write between markers (line %d) writes %s/%s' % (path, node.lineno, parts_dir(source),
                                                                                        holders[0]))
                function = node
                while function in up and not isinstance(function, ast.FunctionDef):
                    function = up[function]
                markers = sorted(set(n.value for n in ast.walk(function) if isinstance(n, ast.Constant) and
                                     isinstance(n.value, str) and n.value.startswith('//') and n.value.strip() in ctx['original']))
                proofs.append(('marker', path, marker, '%s/%s' % (parts_dir(source), holders[0]), [m.strip() for m in markers]))
                continue
            if runs_a_program(parent, up):
                continue
            loops = loop_targets(index, parent)
            if loops:
                for targets, body in loops:
                    calls = read_calls(body, lambda r: isinstance(r, ast.Name) and r.id in targets)
                    for call in calls:
                        read_edit(call)
                if any(read_calls(body, lambda r: isinstance(r, ast.Name) and r.id in targets) for targets, body in loops):
                    continue
            unknown.append('line %d: %s' % (node.lineno, text.split('\n')[node.lineno - 1].strip()[:120]))
            continue
        if runs_a_program(node, up):
            continue
        # for name in ("FILE", ...): (root / name).read_text(); for key, home in {"x": "FILE"}.items(): ...
        loops = loop_targets(index, node)
        done = False
        for targets, body in loops:
            calls = read_calls(body, lambda r: isinstance(r, ast.BinOp) and isinstance(r.op, ast.Div)
                               and isinstance(r.right, ast.Name) and r.right.id in targets)
            for call in calls:
                read_edit(call)
                done = True
        if done:
            continue
        # A loop that takes the name and reads no file by it (a view to preprocess, a line to print) changes nothing; one
        # that opens it some other way is not known.
        if loops:
            other = [n for targets, body in loops for statement in body for n in ast.walk(statement)
                     if isinstance(n, ast.Call) and (getattr(n.func, 'attr', '') in ('read_bytes', 'open_text') or
                                                     getattr(n.func, 'id', '') == 'open') and
                     any(isinstance(a, ast.Name) and a.id in targets for a in ast.walk(n))]
            if not other:
                continue
        # read("FILE"), read being def read(path): return (root / path).read_text()
        if isinstance(parent, ast.Call) and isinstance(parent.func, ast.Name):
            helpers = local_helper(tree, up, parent, parent.func.id)
            calls = [c for f in helpers for c in read_calls(f.body, lambda r: isinstance(r, ast.BinOp) and
                                                             isinstance(r.right, ast.Name) and r.right.id == f.args.args[0].arg)]
            if calls:
                for call in calls:
                    read_edit(call)
                continue
        # A name or a word the tool compares or says: no read.
        if isinstance(parent, (ast.Compare, ast.Dict, ast.JoinedStr)) or isinstance(parent, ast.Tuple) and \
                isinstance(up.get(parent), ast.List) and not loop_targets(index, parent):
            continue
        unknown.append('line %d: %s' % (node.lineno, text.split('\n')[node.lineno - 1].strip()[:120]))
    if unknown:
        raise Refused('%s names %s in ways the split does not know; teach rewrite_python each one:\n  %s' %
                      (path, source, '\n  '.join(unknown)))
    for (begin, end), new in reads.items():
        edits.append(((begin, end), new))
    if reads:
        said.append('%s: %d reads of %s give it whole (source_text)' % (path, len(reads), source))
        proofs.append(('reads', path, len(reads)))
    if not edits and not inserts:
        return None
    out = text
    pieces = sorted([(b, e, new) for (b, e), new in edits] + [(at, at, new) for at, new in inserts], reverse=True)
    for begin, end, new in pieces:
        out = out[:begin] + new + out[end:]
    if reads and 'def source_text(' not in out:
        out = insert_helper(out, SOURCE_TEXT)
    needed = (['os'] if inserts else []) + (['shutil'] if any('shutil.' in new for _, new in inserts) else []) + \
        (['re', 'pathlib'] if reads else [])
    for module in needed:
        if not imported(out, module):
            out = insert_import(out, module)
    return ToolEdit(path, out, said, proofs)


def imported(text, module):
    """Whether text imports module by its own name (import os, import concurrent.futures, os)."""
    for statement in ast.parse(text).body:
        if isinstance(statement, ast.Import) and any((a.asname or a.name) == module for a in statement.names):
            return True
    return False


def local_helper(tree, up, call, name):
    """The one-argument function name that call reaches: the nearest enclosing function's own, else the module's."""
    scope = call
    while scope in up:
        scope = up[scope]
        if isinstance(scope, (ast.FunctionDef, ast.Module)):
            found = [f for f in scope.body if isinstance(f, ast.FunctionDef) and f.name == name and len(f.args.args) == 1]
            if found:
                return found[-1:]
    return []


def last_import_end(text):
    tree = ast.parse(text)
    last = None
    for statement in tree.body:
        if isinstance(statement, (ast.Import, ast.ImportFrom)):
            last = statement
        elif last is not None and not isinstance(statement, (ast.Import, ast.ImportFrom)):
            break
    if last is None:
        raise Refused('a tool with no imports to put the split\'s helper after')
    return offsets(text)[last.end_lineno]


def insert_helper(text, helper):
    at = last_import_end(text)
    return text[:at] + '\n\n' + helper + '\n' + text[at:]


def insert_import(text, module):
    at = last_import_end(text)
    return text[:at] + 'import %s\n' % module + text[at:]


def rewrite_manifest(text, source, ctx):
    """test/assembly/manifest.py reads every evidence file (test/checks.c#CHECK_<name> among them) whole."""
    read = "text = evidence_path.read_text(encoding='utf-8', errors='replace')"
    if read not in text:
        raise Refused('test/assembly/manifest.py: no %r to read evidence whole with' % read)
    if 'def source_text(' in text:
        return None
    text = text.replace(read, "text = source_text(evidence_path, encoding='utf-8', errors='replace')")
    helper = SOURCE_TEXT.replace('    path = Path(path)\n', '    path = pathlib.Path(path)\n')
    return ToolEdit('test/assembly/manifest.py', insert_helper(text, helper),
                    ['test/assembly/manifest.py: an evidence file is read whole (source_text), so test/checks.c#CHECK_<name> '
                     "names the section wherever its part is; performance_coverage's and specialization_coverage's rows stay "
                     'as they are'], [('evidence', source)])


ONE_WAY_TEXT = '''def text_of(path):
    """path's text, comments and strings blanked; a file test/split.py cut in parts is read whole (its parts put back
       in their places), so a field set in one part and cleared in another is the one file's, as it was."""
    text = source_text(path)
'''


def rewrite_one_way(text, source, ctx):
    """test/one_way_state.py: a split file's fields are its own whole (the rule is per file); its parts are not read again."""
    old_read = ('def text_of(path):\n    with open(path, encoding="utf-8", errors="replace") as handle:\n'
                '        text = handle.read()\n')
    old_walk = '            if name.endswith((".c", ".h", ".inc")):\n                yield os.path.join(base, name)\n'
    if 'def source_text(' in text:
        return None
    if old_read not in text or old_walk not in text:
        raise Refused('test/one_way_state.py: its sources() or text_of() is not the shape the split rewrites')
    text = text.replace(old_read, ONE_WAY_TEXT)
    text = text.replace(old_walk, '            if name.endswith((".c", ".h", ".inc")) and not split_part(base, name):\n'
                                  '                yield os.path.join(base, name)\n')
    helper = SOURCE_TEXT.replace('    path = Path(path)\n', '    path = pathlib.Path(path)\n').replace(
        'path.read_text(**options)', 'path.read_text(encoding="utf-8", errors="replace")').replace(
        '(folder / match.group(1)).read_text(**options)', '(folder / match.group(1)).read_text(encoding="utf-8", errors="replace")')
    helper += '''

def split_part(base, name):
    """Whether base/name is a part of a file test/split.py cut (base.c includes it), which is read with that file."""
    whole = base + ".c"
    if not os.path.isfile(whole):
        return False
    with open(whole, encoding="utf-8", errors="replace") as handle:
        return ('#include "%s/%s"' % (os.path.basename(base), name)) in handle.read()
'''
    text = insert_helper(text, helper)
    if not re.search(r'^import pathlib\b', text, re.M):
        text = insert_import(text, 'pathlib')
    return ToolEdit('test/one_way_state.py', text,
                    ['test/one_way_state.py: a split file is scanned whole, its parts with it, so its fields keep their seal keys'],
                    [('one_way', source)])


# test/run: the seams put the parts where the file is; a grep, awk or sed of the file reads it and its parts.
SHELL_READERS = ('grep', 'awk', 'sed')


def shell_words(text, at):
    """The words of the command at text[at:], to the end of the command: (word, start, end) each, and where it ends."""
    words = []
    i, n = at, len(text)
    while i < n:
        while i < n and text[i] in ' \t':
            i += 1
        if text.startswith('\\\n', i):
            i += 2
            continue
        if i >= n or text[i] in '\n|;&)' or text.startswith('2>', i) or text[i] in '<>':
            break
        start = i
        while i < n and text[i] not in ' \t\n|;&)<>':
            if text[i] == "'":
                close = text.find("'", i + 1)
                i = n if close < 0 else close + 1
            elif text[i] == '"':
                i += 1
                while i < n and text[i] != '"':
                    i += 2 if text[i] == '\\' else 1
                i += 1
            elif text.startswith('\\\n', i):
                break
            elif text[i] == '\\':
                i += 2
            else:
                i += 1
        words.append((text[start:i], start, i))
    return words, i


def rewrite_run(text, source, ctx):
    """test/run names source: a seam's case that names it names its parts too, each grep, awk or sed of it reads it and its
       parts through cat, and every other line that names it (a compile, an #include, a comment, a message) stays."""
    folder = parts_dir(source)
    token = re.compile(r'(?<![\w./-])%s(?![\w./-])' % re.escape(source))
    edits, pairs, said, unknown = [], [], [], []
    lines = text.split('\n')
    starts = offsets(text)
    # The readers first: a grep, awk or sed whose one file is source.
    for match in re.finditer(r'(?<![\w$./-])(grep|awk|sed)\b', text):
        line_start = text.rfind('\n', 0, match.start()) + 1
        if text[line_start:match.start()].lstrip().startswith('#'):
            continue
        words, end = shell_words(text, match.start())
        named = [w for w in words if w[0] == source]
        if not named:
            continue
        files = [w for w in words[1:] if not w[0].startswith(('-', "'", '"')) and '/' in w[0]]
        number = text.count('\n', 0, match.start()) + 1
        if len(named) != 1 or len(files) != 1:
            unknown.append('line %d: %s reads %s beside other files' % (number, match.group(1), source))
            continue
        word, begin, finish = named[0]
        cut = text[match.start():begin].rstrip()
        if cut.endswith('\\'):
            cut = cut[:-1].rstrip()
        new = 'cat %s %s/*.c | %s%s' % (source, folder, cut, text[finish:end])
        edits.append((match.start(), end, new))
        pairs.append(('reader', text[match.start():end], new))
        said.append('test/run: line %d: its %s of %s reads it and its parts' % (number, match.group(1), source))
    # The seams: a pattern of a case that names the file names its parts after it.
    depth = 0
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith('#'):
            continue
        if re.match(r'case\b.*\bin$', stripped):
            depth += 1
        elif re.match(r'esac\b', stripped):
            depth -= 1
        found = token.search(line)
        if not found or depth <= 0:
            continue
        at = starts[number - 1] + found.end()
        if any(begin <= at <= end for begin, end, _ in edits):
            continue
        after = re.match(r'\s*([|)])', line[found.end():])
        if after:
            edits.append((at, at, ' | %s/*' % folder))
            said.append("test/run: line %d (a seam's case) gives %s/* what it gives %s" % (number, folder, source))
            pairs.append(('seam', number))
    # Everything else that names it.
    for number, line in enumerate(lines, 1):
        found = token.search(line)
        if not found or line.lstrip().startswith('#'):
            continue
        at = starts[number - 1] + found.start()
        if any(begin <= at < max(end, begin + 1) or begin == at + len(source) for begin, end, _ in edits):
            continue
        logical, k = line, number
        while k < len(lines) and lines[k - 1].endswith('\\'):
            logical += ' ' + lines[k]
            k += 1
        back = number - 1
        while back >= 1 and lines[back - 1].endswith('\\'):
            logical = lines[back - 1] + ' ' + logical
            back -= 1
        if re.search(r'#include\s+"%s"' % re.escape(source), line):
            continue
        if re.search(r'(?:^|\s)(?:-D|-o\b|-c\b|-I|-f|-W|-static|-O\d|-l)|\bc?cc\b|\$\{?CC|freestanding|\$\w*cc\b|"\$\w*cc"', logical):
            continue
        if re.search(r'build_label=|\becho\s|\bsame\s|\bprintf\s', logical):
            continue
        unknown.append('line %d: %s' % (number, line.strip()[:120]))
    if unknown:
        raise Refused('test/run names %s in ways the split does not know; teach rewrite_run each one:\n  %s' %
                      (source, '\n  '.join(unknown)))
    if not edits:
        return None
    out = text
    for begin, end, new in sorted(edits, reverse=True):
        out = out[:begin] + new + out[end:]
    return ToolEdit('test/run', out, said, [('run', pairs)])


# The coverage manifests name their evidence files in rows, which test/assembly/manifest.py reads (rewrite_manifest).
THROUGH_MANIFEST = ('test/performance_coverage.py', 'test/specialization_coverage.py')


def tool_edits(tree_files, read, source, ctx):
    """Every tool the split rewrites for source, from the texts read gives."""
    out = []
    for path in python_tools(tree_files, read, source):
        if path in ('test/one_way_state.py', 'test/assembly/manifest.py') + THROUGH_MANIFEST:
            continue
        edit = rewrite_python(path, read(path), source, ctx)
        if edit:
            out.append(edit)
    if source == CHECKS:
        edit = rewrite_manifest(read('test/assembly/manifest.py'), source, ctx)
        if edit:
            out.append(edit)
    if TARGETS[source].views == 'shell':
        edit = rewrite_one_way(read('test/one_way_state.py'), source, ctx)
        if edit:
            out.append(edit)
    edit = rewrite_run(read('test/run'), source, ctx)
    if edit:
        out.append(edit)
    return out


REPORTED = [
    ('test/function_audit.seal', 'the digest moves (resealed by --apply); the inventory count does not'),
    ('README.md, incident_response.md, .github/SECURITY.md and the comments under src/ that name the file',
     'name it as the program it is (test/checks.c -DCHECK_<name>, src/sh/host.c); the section or function named is '
     'found in its part by name'),
    ('test/differential.py', "a #line it writes for a body lifted from the file, and a failure's line number, count lines "
     'of the whole text (source_text), which are the lines the file had before the cut'),
]


# ---- the checks ----

def run(command, cwd, **options):
    return subprocess.run(command, cwd=str(cwd), capture_output=True, **options)


def say(text):
    print('  ' + text, flush=True)


def git(*args, env=None, data=None):
    done = subprocess.run(['git'] + list(args), cwd=str(ROOT), capture_output=True, env=env, input=data)
    if done.returncode:
        raise Refused('git %s: %s' % (' '.join(args[:2]), done.stderr.decode(errors='replace').strip()[-1200:]))
    return done.stdout.decode(errors='replace').strip()


def tracked():
    return [name for name in git('ls-files', '-z').split('\0') if name]


def copy_tree(into):
    """The tracked files as they are on disk, and the built GPU kernels gpu.c includes: copies and not links, since the
       tools run in the copy write their seals where they stand."""
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


def outputs(tree, source):
    """What the split writes into tree, from tree's own file and tools: ({path: text}, plan, original, tool edits)."""
    text = (tree / source).read_text()
    files = set(str(p.relative_to(tree)) for p in tree.rglob('*') if p.is_file())
    main, parts, plan = split(source, text, lambda name: name in files)
    if flatten(source, main, parts) != text:
        raise Refused('the parts put back in their places are not %s as it was' % source)
    written = {source: main}
    written.update({'%s/%s' % (parts_dir(source), part): body for part, body in parts.items()})
    edits = tool_edits(files, tree_reader(tree), source, {'main': main, 'parts': parts, 'original': text})
    for edit in edits:
        written[edit.path] = edit.text
    return written, plan, text, edits


def includes_of(tree, name):
    """The quote includes of tree/name that resolve in tree."""
    text = (tree / name).read_text(errors='replace')
    folder = posixpath.dirname(name)
    for match in INCLUDE_LINE.finditer(text):
        path = posixpath.normpath(posixpath.join(folder, match.group(2)))
        if (tree / path).is_file():
            yield path, text.count('\n', 0, match.start()) + 1


def reachers(tree, source):
    """The files of tree whose includes reach source."""
    files = [n for n in tracked() if n.endswith(('.c', '.h', '.inc')) and (tree / n).is_file()]
    included = collections.defaultdict(set)
    for name in files:
        for path, _ in includes_of(tree, name):
            included[path].add(name)
    reach, todo = {source}, [source]
    while todo:
        for name in included.get(todo.pop(), ()):
            if name not in reach:
                reach.add(name)
                todo.append(name)
    return reach


def checks_sections(tree, source, every):
    """The section names of test/checks.c to preprocess: every one, or those around an include of a file that reaches
       source (the CHECK_ names of the innermost conditional naming any)."""
    text = source_text_here(tree / CHECKS)
    shape = Shape(text)
    names = []
    if every:
        for at in range(1, len(shape.lines) + 1):
            if shape.words[at] in ('if', 'ifdef', 'ifndef', 'elif'):
                for word in SECTION_WORD.findall(shape.lines[at - 1]):
                    if word not in names:
                        names.append(word)
        return names
    reach = reachers(tree, source)
    for match in INCLUDE_LINE.finditer(text):
        path = posixpath.normpath(posixpath.join('test', match.group(2)))
        if path not in reach:
            continue
        line = text.count('\n', 0, match.start()) + 1
        for opened, _ in reversed(shape.stacks[line]):
            found = [w for w in SECTION_WORD.findall(shape.lines[opened - 1]) if w.startswith(('CHECK_', 'BENCH_'))]
            if found:
                names += [w for w in found if w not in names]
                break
    return names


def stub_includes(tree, into):
    """Empty headers for the quote includes of test/checks.c that resolve nowhere (the native sections' generated ones,
       which test/run writes), so every section preprocesses; before and after are given the same."""
    into.mkdir(parents=True, exist_ok=True)
    text = source_text_here(tree / CHECKS)
    for match in INCLUDE_LINE.finditer(text):
        path = posixpath.normpath(posixpath.join('test', match.group(2)))
        if not (tree / path).exists() and '/' not in match.group(2):
            (into / match.group(2)).write_text('')
    return into


def views(tree, source, stubs):
    out = []
    reach = reachers(tree, source)
    if 'programs/shell.c' in reach:
        out += [('programs/shell.c', ['-O2', '-DCONFIG_MOONWATER_WAYLAND']),
                ('programs/shell.c', ['-O2', '-DSHELL_NO_WAYLAND', '-DSHELL_NO_GPU'])]
    if 'src/build/build.c' in reach:
        out.append(('src/build/build.c', ['-O2', '-DCONFIG_MOONWATER_WAYLAND']))
    every = source == CHECKS
    for name in checks_sections(tree, source, every):
        flags = FREESTANDING + ['-I' + str(stubs), '-D' + name]
        if name.startswith('SHARED_'):
            flags = ['-O2', '-I' + str(stubs), '-D' + name]
        out.append((CHECKS, flags))
    if every:
        out += [(CHECKS, ['-O2', '-D_GNU_SOURCE', '-DCONFIG_MOONWATER_WAYLAND', '-DCHECK_wayland']),
                (CHECKS, ['-O2', '-D_GNU_SOURCE', '-DCHECK_allocator', '-DALLOCATOR_REFERENCE']),
                (CHECKS, ['-O2', '-D_GNU_SOURCE', '-DCHECK_dbus'])]
    return out


INDENT = re.compile(rb'^[ \t]+', re.M)
NAMES = ['-Wno-builtin-macro-redefined', '-D__LINE__=__SPLIT_LINE__', '-D__FILE__=__SPLIT_FILE__']


def preprocessed(tree, source, flags):
    done = run(['gcc', '-E', '-P'] + flags + [source], tree)
    if done.returncode:
        return None, done.stderr.decode(errors='replace')[-600:]
    return done.stdout, None


def first_difference(a, b):
    a, b = a.split(b'\n'), b.split(b'\n')
    for at, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return 'line %d: before %r, after %r' % (at + 1, x[:160], y[:160])
    return 'one is a prefix of the other (%d and %d lines)' % (len(a), len(b))


def compare_view(before_tree, after_tree, source, flags):
    """One view before and after: what was the same, or why not."""
    before, failed = preprocessed(before_tree, source, flags)
    after, failed_after = preprocessed(after_tree, source, flags)
    label = 'gcc -E -P %s %s' % (' '.join(f for f in flags if not f.startswith('-I/')), source)
    if before is None or after is None:
        if before is None and after is None and failed.split('error:')[-1] == failed_after.split('error:')[-1]:
            return label, 'does not preprocess, before or after, with the same error', 0
        raise Refused('%s: preprocesses %s before and %s after:\n%s' % (label, 'not' if before is None else 'well',
                                                                         'not' if after is None else 'well',
                                                                         failed or failed_after))
    if before == after:
        return label, 'byte-identical (%d bytes, sha256 %s)' % (len(after), hashlib.sha256(after).hexdigest()[:16]), 1
    lined = sum(x != y for x, y in zip(before.split(b'\n'), after.split(b'\n')))
    named_before, _ = preprocessed(before_tree, source, flags + NAMES)
    named_after, _ = preprocessed(after_tree, source, flags + NAMES)
    if named_before == named_after:
        return label, ('identical once __LINE__ and __FILE__ are names (%d lines carry their values, which are the part\'s '
                       'line and name)' % lined), 2
    if INDENT.sub(b'', named_before) == INDENT.sub(b'', named_after):
        return label, ('identical but for the indentation of lines GCC lays out once it stops tracking columns '
                       '(LINE_MAP_MAX_LOCATION_WITH_COLS), and %d lines that carry __LINE__ or __FILE__' % lined), 3
    raise Refused('%s differs after the split: %s' % (label, first_difference(INDENT.sub(b'', named_before),
                                                                             INDENT.sub(b'', named_after))))


def compare_views(before_tree, after_tree, source, stubs, jobs=2):
    import concurrent.futures
    every = views(before_tree, source, stubs)
    tally = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(compare_view, before_tree, after_tree, s, f) for s, f in every]
        for future in futures:
            label, what, kind = future.result()
            tally[kind] += 1
            if kind != 1 or len(every) < 12:
                say('%s: %s' % (label, what))
    say('%d views: %d byte-identical, %d identical once __LINE__ and __FILE__ are names, %d identical but for '
        'indentation, %d preprocess neither before nor after' % (len(every), tally[1], tally[2], tally[3], tally[0]))


def audit(tree, *flags):
    done = run([sys.executable, 'test/function_audit.py'] + list(flags), tree)
    out = done.stdout.decode(errors='replace') + done.stderr.decode(errors='replace')
    if done.returncode:
        raise Refused('function_audit.py %s failed in %s:\n%s' % (' '.join(flags), tree, out[-2000:]))
    return out


def audit_count(tree, seal):
    if seal:
        audit(tree, '--seal')
        audit(tree, '--check')
    count = re.search(r'^\s*(\d+)\s+TOTAL C FUNCTIONS', audit(tree, '--summary'), re.M)
    if not count:
        raise Refused('function_audit.py --summary in %s gave no total' % tree)
    return count.group(1)


MANIFEST_TOOLS = ('dropped_results', 'one_way_state', 'performance_coverage', 'specialization_coverage')


def manifest_runs(tree):
    out = {}
    for tool in MANIFEST_TOOLS:
        flags = ['--check'] if tool in ('dropped_results', 'one_way_state') else []
        done = run([sys.executable, 'test/%s.py' % tool] + flags, tree)
        out[tool] = (done.returncode, (done.stdout + done.stderr).decode(errors='replace'))
    return out


def seam_of(tree, paths):
    text = (tree / 'test/run').read_text()
    body = re.search(r'^seam_of_path\(\)\n\{\n.*?^\}\n', text, re.M | re.S)
    if not body:
        raise Refused('test/run has no seam_of_path to ask')
    script = body.group(0) + ''.join('seam_of_path %s\n' % shlex.quote(p) for p in paths)
    done = run(['sh', '-c', script], tree)
    return done.stdout.decode().split('\n')


def hold_tools(before_tree, after_tree, source, original, edits, written):
    """Each tool rewritten does what it did."""
    whole = source_text_here(after_tree / source)
    if whole != original:
        raise Refused('source_text of %s after the split is not the file as it was' % source)
    say('source_text (the readers\' helper) gives %s as it was, byte for byte' % source)
    for edit in edits:
        for proof in edit.proofs:
            kind = proof[0]
            if kind in ('reads', 'evidence', 'one_way'):
                helper = next(n for n in ast.parse(edit.text).body if isinstance(n, ast.FunctionDef) and n.name == 'source_text')
                scope = dict(re=re, pathlib=pathlib)
                exec(compile(ast.Module(body=[helper], type_ignores=[]), edit.path, 'exec'), scope)
                if scope['source_text'](after_tree / source) != original:
                    raise Refused('%s: its source_text does not give %s as it was' % (edit.path, source))
                say('%s: its source_text gives %s as it was%s' % (edit.path, source, ', for %d reads' % proof[2]
                                                                    if kind == 'reads' else ''))
            elif kind == 'marker':
                body = (after_tree / proof[3]).read_text()
                for marker in proof[4]:
                    if body.count(marker) != original.count(marker):
                        raise Refused('%s: the marker %r is not in %s as often as it was in %s' % (edit.path, marker, proof[3],
                                                                                               source))
                say('%s: its markers (%s) are in %s, which it now writes' % (edit.path, ', '.join(proof[4]), proof[3]))
            elif kind == 'layout':
                pass
            elif kind == 'run':
                hold_run(before_tree, after_tree, source, written, proof[1])
    layouts = [e for e in edits if any(p[0] == 'layout' for p in e.proofs)]
    if layouts:
        hold_layout(after_tree, source)
        say('the mutation tools\' scratch trees: %s' % ', '.join(
            '%s (%d)' % (e.path, sum(p[0] == 'layout' for p in e.proofs)) for e in layouts))


def hold_run(before_tree, after_tree, source, written, pairs):
    paths = [source] + sorted(p for p in written if p.startswith(parts_dir(source) + '/'))
    before = seam_of(before_tree, [source] * len(paths))
    after = seam_of(after_tree, paths)
    if before != after:
        raise Refused('test/run: seam_of_path gives the parts %s, where %s was %s' % (after, source, before[0]))
    say('test/run: seam_of_path puts %s and each of its %d parts in %s' % (source, len(paths) - 1, before[0] or '-'))
    for kind, *rest in pairs:
        if kind != 'reader':
            continue
        old, new = rest
        a = run(['sh', '-c', old.replace('\\\n', ' ')], before_tree)
        b = run(['sh', '-c', new.replace('\\\n', ' ')], after_tree)
        if a.stdout != b.stdout or a.returncode != b.returncode:
            raise Refused('test/run: %r gives %r before and %r after' % (new, a.stdout[:200], b.stdout[:200]))
        say('test/run: %s gives what %s gave (%d bytes)' % (new.split('|', 1)[1].strip()[:60], old.split()[0], len(a.stdout)))


def hold_layout(tree, source):
    """A scratch tree laid out as the mutation tools lay theirs (src as links, the file linked and each part linked
       beside it) preprocesses as the tree does: a part's ../ goes through the scratch tree, not the links' targets."""
    if source != CHECKS:
        return
    with tempfile.TemporaryDirectory(prefix='split-layout-') as scratch:
        d = pathlib.Path(scratch) / 'm'
        (d / 'test' / stem(source)).mkdir(parents=True)
        subprocess.run(['cp', '-as', str(tree / 'src'), str(d / 'src')], check=True)
        if (tree / 'artifacts').exists():
            os.symlink(str(tree / 'artifacts'), str(d / 'artifacts'))
        os.symlink(str(tree / source), str(d / source))
        for part in os.listdir(str(tree / parts_dir(source))):
            os.symlink(str(tree / parts_dir(source) / part), str(d / parts_dir(source) / part))
        flags = FREESTANDING + ['-DCHECK_terminal']
        a, _ = preprocessed(tree, source, flags)
        b, failed = preprocessed(d, str(d / source), flags)
        if b is None or a != b:
            raise Refused('a scratch tree of links does not preprocess CHECK_terminal as the tree does: %s' % (failed or ''))
        say('a scratch tree of links (the file and each part linked, src copied as links) preprocesses CHECK_terminal '
            'as the tree does')


def check(tree, source, jobs):
    """The split made in tree (a copy) and held to every check; answers what was written."""
    before_count = audit_count(ROOT, False)
    before_manifests = manifest_runs(ROOT)
    written, plan, original, edits = outputs(tree, source)
    for name, text in written.items():
        write(tree, name, text)
    say('the parts put back are %s byte for byte' % source)
    for edit in edits:
        for line in edit.said:
            say(line)
    hold_tools(ROOT, tree, source, original, edits, written)
    with tempfile.TemporaryDirectory(prefix='split-stubs-') as stubs:
        compare_views(ROOT, tree, source, stub_includes(ROOT, pathlib.Path(stubs)), jobs)
    after_count = audit_count(tree, True)
    if after_count != before_count:
        raise Refused('the function audit counts %s functions before and %s after' % (before_count, after_count))
    say('function audit: %s C functions before and after, the seal written in the copy' % after_count)
    after_manifests = manifest_runs(tree)
    for tool in MANIFEST_TOOLS:
        (rc, out), (rc_after, out_after) = before_manifests[tool], after_manifests[tool]
        if rc != rc_after or out.strip().split('\n')[-1] != out_after.strip().split('\n')[-1]:
            raise Refused('test/%s.py gave %d (%s) before and %d (%s) after' % (tool, rc, out.strip()[-300:], rc_after,
                                                                                out_after.strip()[-300:]))
        say('test/%s.py: %s, as before' % (tool, 'passes' if rc == 0 else 'FAILS (as it did before the split)')
            + ': ' + out_after.strip().split('\n')[-1][:100])
    return written


def print_plan(source, plan, text, edits=None):
    print('%s, %d lines, cut in %d runs:' % (source, text.count('\n'), len(plan)))
    print('  %-22s %6s %6s %6s  %4s  %s' % ('file', 'from', 'to', 'lines', 'fns', 'sections, or first and last function'))
    for part, first, last, inside, sections in plan:
        what = (sections[0] + (' .. ' + sections[-1] if len(sections) > 1 else '')) if sections else \
            ('%s .. %s' % (inside[0], inside[-1]) if inside else '-')
        print('  %-22s %6d %6d %6d  %4d  %s' % (part, first, last, last - first + 1, len(inside), what))
    if edits is not None:
        print('the tools that name %s, and what the split does to each:' % source)
        for edit in edits:
            for line in edit.said:
                print('  ' + line)
        print('the ones it does not, and why:')
        for path, why in REPORTED:
            print('  %s: %s' % (path, why))


# ---- carrying a branch across the split ----

def shown(rev, path):
    done = subprocess.run(['git', 'show', '%s:%s' % (rev, path)], cwd=str(ROOT), capture_output=True)
    return done.stdout.decode() if done.returncode == 0 else None


def rev_files(rev):
    return set(name for name in git('ls-tree', '-r', '--name-only', '-z', rev).split('\0') if name)


def cut_at(rev, source):
    """rev's file cut, with its tools rewritten for it: {path: text} for the split's files, and the file's text."""
    text = shown(rev, source)
    if text is None:
        raise Refused('%s has no %s' % (rev, source))
    files_there = rev_files(rev)
    try:
        main, parts, _ = split(source, text, lambda name: name in files_there)
    except Refused as refusal:
        raise Refused('%s: %s' % (rev[:12], refusal))
    if flatten(source, main, parts) != text:
        raise Refused('%s: the parts put back are not its %s' % (rev[:12], source))
    files = {source: main}
    files.update({'%s/%s' % (parts_dir(source), part): body for part, body in parts.items()})
    cache = {}

    def read(name):
        if name not in cache:
            cache[name] = shown(rev, name) if name in files_there else None
        return cache[name]
    try:
        for edit in tool_edits(files_there, read, source, {'main': main, 'parts': parts, 'original': text}):
            files[edit.path] = edit.text
    except Refused as refusal:
        raise Refused('%s: %s' % (rev[:12], refusal))
    return files, text


def tree_with(index, base, files, patch=None):
    env = dict(os.environ, GIT_INDEX_FILE=str(index))
    git('read-tree', base, env=env)
    if patch:
        git('apply', '--cached', '--3way', '--whitespace=nowarn', env=env, data=patch)
    for path, text in files.items():
        blob = git('hash-object', '-w', '--stdin', data=text.encode())
        mode = (git('ls-files', '-s', '--', path, env=env).split() or ['100644'])[0]
        git('update-index', '--add', '--cacheinfo', '%s,%s,%s' % (mode, blob, path), env=env)
    return git('write-tree', env=env)


def units(text):
    """The functions and the conditionals of text in its order: (line, name, the conditionals it is inside)."""
    shape = Shape(text)
    out = [(head, name, shape.stacks[head]) for name, head, _ in shape.bodies]
    for at in range(1, len(shape.lines) + 1):
        if shape.words[at] in ('if', 'ifdef', 'ifndef') and shape.clean[at]:
            out.append((at, shape.lines[at - 1].strip(), shape.stacks[at]))
    return sorted(out)


def ambiguous(source, known, new, files_there):
    """The functions and sections new has and the file before the split did not (known: their names) that sit between
       two that it had, at their own level, which are in different runs: which part each was meant for is not known."""
    _, _, plan = split(source, new, lambda name: name in files_there)
    order = units(new)

    def run_of(line):
        return next((k for k, (_, first, last, _, _) in enumerate(plan) if first <= line <= last), -1)
    found = []
    for at, (line, name, level) in enumerate(order):
        if name in known:
            continue
        before = next((order[k] for k in range(at - 1, -1, -1) if order[k][1] in known and order[k][2] == level), None)
        after = next((order[k] for k in range(at + 1, len(order)) if order[k][1] in known and order[k][2] == level), None)
        if before and after and run_of(before[0]) != run_of(after[0]):
            where = lambda unit: plan[run_of(unit[0])][0] if run_of(unit[0]) >= 0 else own(source)
            found.append('%s (line %d, between %s of %s and %s of %s)' % (name, line, before[1], where(before), after[1],
                                                                         where(after)))
    return found


def carry(branch, split_rev, index, source):
    parent = git('rev-parse', split_rev + '^')
    tip = git('rev-parse', branch + '^{commit}')
    if subprocess.run(['git', 'merge-base', '--is-ancestor', parent, tip], cwd=str(ROOT)).returncode:
        raise Refused('%s does not descend from %s, the commit the split was made on: rebase it onto %s (main from just '
                      'before the split) and carry it again' % (branch, parent[:12], parent[:12]))
    if tip == git('rev-parse', 'HEAD') and git('status', '--porcelain', '--untracked-files=no'):
        raise Refused('%s is checked out here with edits not committed: commit them first, then carry' % branch)
    commits = git('rev-list', '--reverse', '--topo-order', '%s..%s' % (parent, tip)).split()
    if any(len(git('rev-list', '--parents', '-n', '1', commit).split()) != 2 for commit in commits):
        raise Refused('%s has a merge since %s: rebase it into a line of commits and carry it again' % (branch, parent[:12]))
    known = set(name for _, name, _ in units(shown(parent, source)))
    carried, previous = git('rev-parse', split_rev), parent
    for commit in commits:
        files, text = cut_at(commit, source)
        odd = ambiguous(source, known, text, rev_files(commit))
        if odd:
            raise Refused('%s adds functions or sections at a cut, where the part they belong to is not known: %s. Move each '
                          'after something of the part it belongs to and carry again' % (commit[:12], '; '.join(odd)))
        excluded = [':(exclude)' + path for path in list(files) + ['test/function_audit.seal']]
        patch = subprocess.run(['git', 'diff', '--binary', '--full-index', previous, commit, '--', '.'] + excluded,
                               cwd=str(ROOT), capture_output=True).stdout
        tree = tree_with(index, carried + '^{tree}', files, patch)
        author = git('log', '-1', '--format=%an%x00%ae%x00%aI%x00%B', commit).split('\0', 3)
        env = dict(os.environ, GIT_AUTHOR_NAME=author[0], GIT_AUTHOR_EMAIL=author[1], GIT_AUTHOR_DATE=author[2])
        carried = git('commit-tree', tree, '-p', carried, env=env, data=author[3].encode())
        say('%s -> %s %s' % (commit[:12], carried[:12], author[3].split('\n')[0][:90]))
        previous = commit
    return carried, len(commits)


def split_commit(rev, index, source):
    files, _ = cut_at(rev, source)
    tree = tree_with(index, rev + '^{tree}', files)
    return git('commit-tree', tree, '-p', rev, '-m', '%s cut in parts (a scratch split, test/split.py)' % source)


def tree_dir(rev, into):
    into.mkdir(parents=True, exist_ok=True)
    archive = subprocess.run(['git', 'archive', rev], cwd=str(ROOT), capture_output=True)
    if archive.returncode:
        raise Refused('git archive %s failed' % rev)
    subprocess.run(['tar', '-x', '-C', str(into)], input=archive.stdout, check=True)
    if (ROOT / 'artifacts/gpu_kernels.inc').is_file():
        (into / 'artifacts').mkdir(exist_ok=True)
        shutil.copy2(ROOT / 'artifacts/gpu_kernels.inc', into / 'artifacts/gpu_kernels.inc')
    return into


def carry_main(arguments, source):
    branch = arguments.carry or arguments.prove
    with tempfile.TemporaryDirectory(prefix='split-carry-') as scratch:
        scratch = pathlib.Path(scratch)
        index = scratch / 'index'
        if arguments.prove:
            base = git('merge-base', branch, 'origin/main')
            split_rev = split_commit(base, index, source)
            print('a scratch split of %s, where %s left main: %s' % (base[:12], branch, split_rev[:12]))
        else:
            first_part = '%s/%s' % (parts_dir(source), next(entry[0] for entry in TARGETS[source].cuts if entry[0] != own(source)))
            split_rev = arguments.split or git('log', '--diff-filter=A', '--format=%H', 'HEAD', '--', first_part).split('\n')[-1]
            if not split_rev:
                raise Refused('no commit on HEAD made %s: the split is not on this branch yet (name it with --split REV)' %
                              first_part)
        print('carrying %s onto %s:' % (branch, split_rev[:12]))
        carried, count = carry(branch, split_rev, index, source)
        if arguments.prove:
            print('%s unsplit against %s carried, preprocessed:' % (branch, carried[:12]))
            before, after = tree_dir(branch, scratch / 'unsplit'), tree_dir(carried, scratch / 'carried')
            compare_views(before, after, source, stub_includes(before, scratch / 'stubs'), arguments.jobs)
            print('the carry holds for %s (%d commits)' % (branch, count))
            return 0
    name = branch if not re.fullmatch(r'[0-9a-f]{7,40}', branch) else branch[:12]
    git('update-ref', 'refs/heads/carried/' + name, carried)
    print("carried/%s is %s, %d commits on %s; in the branch's worktree: git reset --hard carried/%s, then rebase onto main"
          % (name, carried[:12], count, split_rev[:12], name))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('file', nargs='?', choices=sorted(TARGETS), help='the file to cut')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='make the split in a copy and hold it to its checks')
    mode.add_argument('--apply', action='store_true', help='--check, then write the split into this tree')
    mode.add_argument('--carry', metavar='BRANCH', help="carry BRANCH's commits across the split: refs/heads/carried/BRANCH")
    mode.add_argument('--prove', metavar='BRANCH', help='--carry onto a scratch split of where BRANCH left main, and hold '
                      'the carried tree to the views of BRANCH unsplit; nothing is kept')
    parser.add_argument('--split', metavar='REV', help='the split commit to carry onto (default: the one on HEAD that made the parts)')
    parser.add_argument('--keep', metavar='DIR', help="make --check's copy in DIR (new or empty) and leave it there")
    parser.add_argument('--jobs', type=int, default=2, help='preprocessor runs at once (default 2)')
    arguments = parser.parse_args()
    try:
        if not arguments.file:
            if arguments.check or arguments.apply or arguments.carry or arguments.prove:
                raise Refused('name the file to cut: %s' % ', '.join(sorted(TARGETS)))
            for source in TARGETS:
                text = (ROOT / source).read_text()
                if (ROOT / parts_dir(source)).is_dir():
                    print('%s: cut (%s/ holds its parts)' % (source, parts_dir(source)))
                    continue
                _, _, plan = split(source, text, lambda name: (ROOT / name).exists())
                print_plan(source, plan, text)
            return 0
        source = arguments.file
        if arguments.carry or arguments.prove:
            return carry_main(arguments, source)
        if (ROOT / parts_dir(source)).is_dir() and not (arguments.check or arguments.apply):
            print('%s is cut: %s/ holds its parts' % (source, parts_dir(source)))
            return 0
        text = (ROOT / source).read_text()
        files = set(tracked())
        main_text, parts, plan = split(source, text, lambda name: name in files or (ROOT / name).exists())
        edits = tool_edits(files, tree_reader(ROOT), source, {'main': main_text, 'parts': parts, 'original': text})
        print_plan(source, plan, text, edits)
        if not (arguments.check or arguments.apply):
            return 0
        if arguments.apply:
            paths = [source, parts_dir(source), 'test/function_audit.seal'] + [e.path for e in edits]
            changed = run(['git', 'status', '--porcelain', '--'] + paths, ROOT).stdout.decode()
            if changed.strip() or (ROOT / parts_dir(source)).exists():
                raise Refused('the tree has changes the split would write over, or %s is already there:\n%s' %
                              (parts_dir(source), changed))
        if not (ROOT / 'artifacts/gpu_kernels.inc').is_file():
            raise Refused('artifacts/gpu_kernels.inc is not built (sh build.sh gpu-kernels): gcc -E reaches gpu.c, which includes it')
        print('checks, in a copy of the tree:')
        if arguments.keep:
            tree = pathlib.Path(arguments.keep).resolve()
            if tree.exists() and any(tree.iterdir()):
                raise Refused('%s is not empty' % tree)
            copy_tree(tree)
            written = check(tree, source, arguments.jobs)
        else:
            with tempfile.TemporaryDirectory(prefix='split-') as scratch:
                tree = pathlib.Path(scratch)
                copy_tree(tree)
                written = check(tree, source, arguments.jobs)
        print('the split holds')
        if arguments.apply:
            for name, body in written.items():
                mode = (ROOT / name).stat().st_mode if (ROOT / name).exists() else None
                write(ROOT, name, body)
                if mode is not None:
                    os.chmod(str(ROOT / name), mode)
            audit_count(ROOT, True)
            print('written: %s, and test/function_audit.seal' % ', '.join(sorted(written)))
            print('next: commit these files as one commit; workers carry their branches with --carry')
        return 0
    except Refused as refusal:
        print('split: %s' % refusal, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
