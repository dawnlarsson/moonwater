#!/usr/bin/env python3
"""
What can a test suite not see?

A suite that passes tells you nothing on its own: it passes on the code it
was written against. Change the code by one, and if it still passes, that
change is invisible to it -- and so is the bug that would have looked the
same. This makes those changes one at a time and reports the ones nothing
noticed.

Three outcomes, not two. A mutation is caught when its lane fails, survived
when the lane still passes, and invalid when the result does not build or
does not stop. Folding invalid into either of the others is how this stops
being useful: into survived and the list drowns, into caught and the gaps
hide.

Every mutation is named by the routine it is in and the text it changed,
never by a line number, because these files move under it. A survivor that
has been looked at and dismissed goes in the ledger with the reason, and the
next run separates ones already judged from ones nobody has seen.

    python3 test/mutate.py --list            what would be tried
    python3 test/mutate.py                   the whole run
    python3 test/mutate.py --target text     one target only
    python3 test/mutate.py --operator relation  one operator only

Run it where the suite runs.
"""
import argparse, bisect, concurrent.futures, hashlib, json, os, re, shutil
import subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, 'test', 'mutate.ledger.json')

#
#       What is mutated, and the lane that would have to notice.
#
#       Mutating one file and running a lane that never touches it proves
#       nothing, so the pairing is written down rather than assumed.
#
TARGETS = {
    'library': ('src/lib.c',  'verify'),
    'text':    ('src/sh/text.c',  'text'),
    'shell':   ('src/sh/lex.c',   'shell'),
    'parse':   ('src/sh/parse.c', 'shell'),
    'expand':  ('src/sh/expand.c','shell'),
    'builtin': ('src/sh/builtin.c','shell'),
    'file':    ('src/sh/file.c',  'files'),
    #   The network stack: what a peer reaches. Its lanes are long (net
    #   alone is minutes), so each of these is judged by STAGES below,
    #   cheapest first, and the whole lane is the confirmation a survivor
    #   gets with --confirm, not the price every mutation pays.
    'net':       ('src/net/net.c',            'net'),
    'wait':      ('src/net/wait.c',           'net'),
    'discover':  ('src/waterlink/discover.c', 'waterlink'),
    'nearby':    ('src/waterlink/nearby.c',   'waterlink'),
    'link':      ('src/waterlink/link.c',     'waterlink'),
    'handshake': ('src/waterlink/handshake.c','waterlink'),
    'seal':      ('src/waterlink/seal.c',     'waterlink'),
    'service':   ('src/waterlink/service.c',  'waterlink'),
    'command':   ('src/waterlink/command.c',  'waterlink'),
    'shnet':     ('src/sh/net.c',             'netem'),
    'host':      ('src/sh/host.c',            'machine'),
    'bowl':      ('src/bowl.c',               'bowl'),
}

#
#       C files whose string literals are text, not code. The asm targets
#       keep their strings mutable on purpose: lib.c's bodies live in them.
#       A number in a diagnostic or a '<' in a header name is not a bound,
#       and a mutation of one lands in survived and drowns the list.
#
PROSE_STRINGS = {'src/net/net.c', 'src/net/wait.c', 'src/sh/net.c',
                 'src/sh/host.c', 'src/bowl.c'} | {
    'src/waterlink/' + n + '.c' for n in
    ('discover', 'nearby', 'link', 'handshake', 'seal', 'service', 'command')}

#
#       How a target's mutation is judged when its lane is too long to pay
#       for every one. Each stage is a command run in the worker tree; a
#       stage that exits other than it did on the unmutated tree caught the
#       mutation, and the first one that does ends the judging. A check
#       stage builds one section of test/checks.c natively and runs it, the
#       way freestanding_checks_one does on the host architecture; a build
#       that fails says invalid, not caught. The harness stages are the
#       hosted lifts that slice these files themselves.
#
CHECK_BUILD = ('gcc -O2 -static -nostdlib -nostartfiles -fno-stack-protector '
               '-fno-builtin -march=x86-64 -DCHECK_{section} -fwhole-program -w '
               '-T src/build/spark.ld -Wl,-e,_start -Wl,--build-id=none '
               '-Wl,--no-warn-rwx-segments -o "{out}" test/checks.c')

def check_stage(section, timeout=None):
    """
    One section of test/checks.c, built natively and run.

    When the mutated tree builds the very binary the unmutated one did,
    the run is skipped: it would say what the baseline said, and saying
    MUTATE-SAME lets the verdict record that the compiler found the two
    programs the same (or never compiled the line into this section).
    """
    script = ('o="$TMPDIR/check.{section}"; '
              + CHECK_BUILD + ' 2> "$o.err" || exit 125; '
              'if cmp -s "$o" "$MUTATE_BASE/check.{section}"; then '
              'echo MUTATE-SAME; exit "$(cat "$MUTATE_BASE/check.{section}.status")"; fi; '
              'cd "$TMPDIR" && "$o" > "$o.out" 2>&1; s=$?; '
              'tail -1 "$o.out" | grep -q " checks, 0 failures$" || '
              '{{ grep -m1 FAIL "$o.out"; exit 1; }}; '
              'exit $s').format(section=section, out='$o')
    return ('check:' + section, ['sh', '-c', script], timeout)

def harness_stage(name, *extra, timeout=None):
    return ('harness:' + name,
            ['python3', 'test/differential.py', '--harness', name] + list(extra),
            timeout)

NET_HARNESSES = ('http_urls', 'http_response_framing', 'tls_hostnames',
                 'tls_dates', 'dns_fuzz', 'dhcp_fuzz', 'netlink_fuzz',
                 'http_fuzz', 'tls_der_fuzz', 'tls_hs_fuzz')

STAGES = {
    'net':       [harness_stage('wire_constants'), check_stage('net')] +
                 [harness_stage(h) for h in NET_HARNESSES],
    'wait':      [check_stage('net'), harness_stage('net_clock_fault'),
                  harness_stage('dns_fuzz'), harness_stage('netlink_fuzz')],
    'discover':  [check_stage('waterlink'), harness_stage('waterlink_sanitized')],
    'nearby':    [check_stage('waterlink'), check_stage('waterlink_service'),
                  harness_stage('waterlink_sanitized')],
    'link':      [check_stage('waterlink'), harness_stage('waterlink_sanitized')],
    'handshake': [check_stage('waterlink'), harness_stage('waterlink_sanitized')],
    'seal':      [check_stage('waterlink'), harness_stage('waterlink_sanitized')],
    'service':   [check_stage('waterlink_service'), check_stage('waterlink'),
                  harness_stage('waterlink_sanitized')],
    'command':   [check_stage('waterlink_service'),
                  harness_stage('waterlink_sanitized')],
    'shnet':     [harness_stage('wire_constants'), check_stage('machine'), harness_stage('dhcp_fuzz'),
                  harness_stage('netlink_fuzz'), harness_stage('host_writes')],
    'host':      [harness_stage('wire_constants'), check_stage('machine'), harness_stage('sntp_fuzz'),
                  harness_stage('sntp_era'), harness_stage('wifi_eapol_fuzz'),
                  harness_stage('wifi_scan_fuzz')],
    'bowl':      [check_stage('bowl'), harness_stage('bowl_sig_fuzz')],
}

#
#       The SWAR constants are structural: every byte of them carries a bit
#       pattern the algorithm is built on, and a plus one turns them into a
#       number that means nothing rather than into a different bound. They
#       are excluded here and mutated by their own operator or not at all.
#
STRUCTURAL = {'0x0101010101010101', '0x8080808080808080', '0x7f7f7f7f7f7f7f7f',
              '0x0101', '0x7f7f', '0x8080', '0xff', '0xfff', '0xff8'}

def routines_of(text, path):
    """Where each routine starts, so a mutation can be named by the one it is in."""
    marks = []
    if path.endswith('lib.c'):
        for m in re.finditer(r'ASM_FUNC\(([A-Za-z0-9_]+)\)', text):
            marks.append((m.start(), m.group(1)))
    for m in re.finditer(r'^[A-Za-z_][A-Za-z0-9_ *]*?\b([a-z_][a-z0-9_]*)\s*\([^;]*$',
                         text, re.M):
        marks.append((m.start(), m.group(1)))
    marks.sort()
    return marks

def routine_at(marks, pos):
    name = '(file)'
    for start, n in marks:
        if start > pos:
            break
        name = n
    return name

def in_data(line):
    return '.ascii' in line or '.asciz' in line or '.string' in line

def commented(text, path=''):
    """Whether an offset is inside a comment, asked once per file.

    Each operator below had its own guess at this and each guess was a
    startswith on the line, which cannot see a /* */ that opened on an
    earlier one. src/lib.c begins with pages of them, so every number in
    the prose -- the licence, the AVX-512, the count of routines -- became
    a mutation nothing can ever catch. The docstring at the top says what
    that does to a run: they land in survived and the list drowns.

    In the C files named in PROSE_STRINGS a string or character literal
    and an #include line count as prose too (a #define is not: its number
    is a ceiling, the first thing worth changing). The one pattern walks
    comments and literals together, left to right, so a quote inside a
    comment and a slash inside a string are each read as what they are.
    """
    if path in PROSE_STRINGS:
        pattern = (r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"'
                   r"|'(?:\\.|[^'\\\n])+'|^[ \t]*#[ \t]*include[^\n]*")
        spans = [(m.start(), m.end())
                 for m in re.finditer(pattern, text, re.S | re.M)]
    else:
        spans = [(m.start(), m.end())
                 for m in re.finditer(r'/\*.*?\*/|//[^\n]*', text, re.S)]
    starts = [a for a, _ in spans]
    ends = [b for _, b in spans]

    def inside(at):
        which = bisect.bisect_right(starts, at) - 1
        return which >= 0 and at < ends[which]

    return inside

def c_numbers(text, path):
    """
    The C reading of a literal: a minus is the operator before it, never
    part of it (`n - 1` minus one is `n - 2`, not the expression `n 0`
    that does not build), and an integer suffix belongs to the number, so
    `64u` and `1ULL` are bounds like any other.
    """
    out = []
    marks = routines_of(text, path)
    inside = commented(text, path)
    for m in re.finditer(r'(?<![\w.])(0x[0-9a-fA-F]+|\d+)([uUlL]*)(?![\w.])', text):
        raw = m.group(1)
        if raw in STRUCTURAL:
            continue
        value = int(raw, 16) if raw.lower().startswith('0x') else int(raw)
        if value > 0xffff or inside(m.start()):
            continue
        out.append(((m.start(1), m.end(1)), raw, value, routine_at(marks, m.start())))
    return out

def numbers(text, path):
    """Every numeric literal worth changing, with where it is."""
    if path in PROSE_STRINGS:
        return c_numbers(text, path)
    out = []
    marks = routines_of(text, path)
    inside = commented(text, path)
    lines = text.split('\n')
    offset = 0
    for line in lines:
        base, offset = offset, offset + len(line) + 1
        s = line.strip()
        if in_data(line) or s.startswith('#include'):
            continue
        # a number after $ or # is an immediate; a bare one is an offset or a
        # count. A digit inside a register name is neither, and the lookbehind
        # is what keeps x0, %r10, t4 and w9 out of this.
        for m in re.finditer(r'([$#])(-?0x[0-9a-fA-F]+|-?\d+)'
                             r'|(?<![\w#$.])(-?0x[0-9a-fA-F]+|-?\d+)(?![\w])',
                             line):
            raw = m.group(2) or m.group(3)
            if raw in STRUCTURAL or raw.lstrip('-') in STRUCTURAL:
                continue
            try:
                value = int(raw, 16) if raw.lower().startswith(('0x', '-0x')) else int(raw)
            except ValueError:
                continue
            if abs(value) > 0xffff:          # a mask or a magic number, not a bound
                continue
            span = (base + m.start(2 if m.group(2) else 3),
                    base + m.end(2 if m.group(2) else 3))
            if inside(span[0]):
                continue
            out.append((span, raw, value, routine_at(marks, base)))
    return out

def plus_minus(text, path):
    """The off by one operator, which is the one the real bugs were."""
    for (a, b), raw, value, routine in numbers(text, path):
        for delta in (1, -1):
            new = value + delta
            if raw.lower().startswith(('0x', '-0x')):
                shown = ('-0x%x' % -new) if new < 0 else ('0x%x' % new)
            else:
                shown = str(new)
            yield {'operator': 'plusminus', 'routine': routine,
                   'was': raw, 'now': shown, 'at': a, 'end': b}

FLIPS = [('jne', 'je'), ('je', 'jne'), ('jae', 'jb'), ('jb', 'jae'),
         ('jbe', 'ja'), ('ja', 'jbe'), ('jz', 'jnz'), ('jnz', 'jz'),
         ('b.eq', 'b.ne'), ('b.ne', 'b.eq'), ('b.lo', 'b.hs'), ('b.hs', 'b.lo'),
         ('cbz', 'cbnz'), ('cbnz', 'cbz'),
         ('beqz', 'bnez'), ('bnez', 'beqz'), ('beq', 'bne'), ('bne', 'beq'),
         ('bltu', 'bgeu'), ('bgeu', 'bltu')]

def branches(text, path):
    """Flip the sense of a branch: the other half of an off by one."""
    marks = routines_of(text, path)
    inside = commented(text, path)
    for was, now in FLIPS:
        for m in re.finditer(r'(?<![\w.])' + re.escape(was) + r'(?=\s)', text):
            line_start = text.rfind('\n', 0, m.start()) + 1
            line = text[line_start:text.find('\n', m.start())]
            if in_data(line) or inside(m.start()):
                continue
            yield {'operator': 'branch', 'routine': routine_at(marks, m.start()),
                   'was': was, 'now': now, 'at': m.start(), 'end': m.end()}

RELATIONS = [('<=', '<'), ('<', '<='), ('>=', '>'), ('>', '>='),
             ('==', '!='), ('!=', '==')]

def relations(text, path):
    """The same idea in C: a bound that is off by one is a comparison."""
    if path.endswith('lib.c'):
        return
    marks = routines_of(text, path)
    inside = commented(text, path)
    for was, now in RELATIONS:
        # the lookbehind's minus keeps a->b out of '>'
        for m in re.finditer(r'(?<![<>=!-])' + re.escape(was) + r'(?![<>=])', text):
            if inside(m.start()):
                continue
            yield {'operator': 'relation', 'routine': routine_at(marks, m.start()),
                   'was': was, 'now': now, 'at': m.start(), 'end': m.end()}


def logic(text, path):
    """Both halves of a refusal or only one: && for || and back."""
    if path.endswith('lib.c'):
        return
    marks = routines_of(text, path)
    inside = commented(text, path)
    for was, now in (('&&', '||'), ('||', '&&')):
        for m in re.finditer(re.escape(was), text):
            if inside(m.start()):
                continue
            yield {'operator': 'logic', 'routine': routine_at(marks, m.start()),
                   'was': was, 'now': now, 'at': m.start(), 'end': m.end()}


def closing_paren(text, at):
    """Where the parenthesis opened at `at` closes, or -1."""
    depth = 0
    for i in range(at, min(len(text), at + 4000)):
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
    return -1


def negations(text, path):
    """
    The test of an if, taken the other way.

    A refusal that is never asked for is one whose condition can be
    turned around without a lane noticing: the early return then fires on
    the honest input, or never on the hostile one.
    """
    if path.endswith('lib.c'):
        return
    marks = routines_of(text, path)
    inside = commented(text, path)
    for m in re.finditer(r'\b(if|while)\s*\(', text):
        if inside(m.start()):
            continue
        open_at = m.end() - 1
        close = closing_paren(text, open_at)
        if close < 0:
            continue
        cond = text[open_at + 1:close]
        yield {'operator': 'negate', 'routine': routine_at(marks, m.start()),
               'was': '(' + cond + ')', 'now': '(!(' + cond + '))',
               'at': open_at, 'end': close + 1}


#
#       Calls whose whole job is a property a peer cannot see in an answer:
#       a secret wiped, a descriptor given back, a buffer cleared before
#       reuse. Taking one away changes no output a lane compares, so a
#       survivor here says a property is unpinned, not that it is broken.
#
DROPPED = ('crypto_forget', 'memory_zero', 'system_close', 'socket_close',
           'tls_forget', 'http_forget', 'netlink_forget', 'digest_close',
           'crypto_hmac_close', 'link_groups_scrub', 'host_wipe',
           'link_session_close', 'link_stream_close', 'http_link_close',
           'net_kmsg_close', 'dhcp_prefix_clear')

def drops(text, path):
    """A statement that is only one of those calls, taken out."""
    if path.endswith('lib.c'):
        return
    marks = routines_of(text, path)
    inside = commented(text, path)
    names = '|'.join(DROPPED)
    for m in re.finditer(r'(?<=[;{}\s])(' + names + r')\s*\(', text):
        if inside(m.start()):
            continue
        close = closing_paren(text, m.end() - 1)
        if close < 0 or text[close + 1:close + 2] != ';':
            continue
        # a statement of its own: nothing but space before it on its line
        line_start = text.rfind('\n', 0, m.start()) + 1
        if text[line_start:m.start()].strip():
            continue
        was = text[m.start():close + 2]
        yield {'operator': 'drop', 'routine': routine_at(marks, m.start()),
               'was': was, 'now': ';', 'at': m.start(), 'end': close + 2}


def targets_of_branches(text, path):
    """
    Where a branch goes, not whether it is taken.

    This is the operator that would have found the string_find bug: the
    branch was right, its destination was one label out, and the scan
    resumed where the candidate stopped instead of where it began. A
    condition flip could never produce that.
    """
    marks = routines_of(text, path)
    for m in re.finditer(r'(?<![\w.])(\d)([fb])(?![\w])', text):
        line_start = text.rfind('\n', 0, m.start()) + 1
        end = text.find('\n', m.start())
        line = text[line_start:end if end > 0 else len(text)]
        if in_data(line) or line.strip().startswith('//'):
            continue
        # backwards instead of forwards, and the label either side of it
        for now in (m.group(1) + ('b' if m.group(2) == 'f' else 'f'),
                    str(int(m.group(1)) + 1) + m.group(2)):
            if now == m.group(0):
                continue
            yield {'operator': 'target', 'routine': routine_at(marks, m.start()),
                   'was': m.group(0), 'now': now, 'at': m.start(), 'end': m.end()}


SWAR = ['0x0101010101010101', '0x8080808080808080', '0x7f7f7f7f7f7f7f7f']

def swar_constants(text, path):
    """
    One SWAR constant for another.

    The strrchr bug was exactly this shape: the three instruction zero test
    where the five instruction one was needed, which reads as the wrong
    constant beside the right instructions. Plus one on these means nothing,
    so they are swapped for each other instead.
    """
    marks = routines_of(text, path)
    for was in SWAR:
        for m in re.finditer(re.escape(was), text):
            line_start = text.rfind('\n', 0, m.start()) + 1
            end = text.find('\n', m.start())
            line = text[line_start:end if end > 0 else len(text)]
            if in_data(line) or line.strip().startswith('//'):
                continue
            for now in SWAR:
                if now == was:
                    continue
                yield {'operator': 'swar', 'routine': routine_at(marks, m.start()),
                       'was': was, 'now': now, 'at': m.start(), 'end': m.end()}


OPERATORS = {'plusminus': plus_minus, 'branch': branches,
             'relation': relations, 'target': targets_of_branches,
             'swar': swar_constants, 'logic': logic, 'negate': negations,
             'drop': drops}

def identify(target, m, text):
    """A name that survives the file moving: the routine and the text around it."""
    context = text[max(0, m['at'] - 40):m['at'] + 40]
    was, now = m['was'], m['now']
    if len(was) > 24 or len(now) > 24:      # a whole condition or call
        was = 'sha1:' + hashlib.sha1(was.encode()).hexdigest()[:8]
        now = 'sha1:' + hashlib.sha1(m['now'].encode()).hexdigest()[:8]
    key = f"{target}|{m['routine']}|{m['operator']}|{was}->{now}|" \
          f"{hashlib.sha1(context.encode()).hexdigest()[:8]}"
    return key

def build(target, operators, only_routine=None):
    path, lane = TARGETS[target]
    text = open(os.path.join(ROOT, path)).read()
    seen, out = set(), []
    for name in operators:
        for m in OPERATORS[name](text, path):
            if only_routine and m['routine'] != only_routine:
                continue
            m['target'], m['lane'], m['path'] = target, lane, path
            m['id'] = identify(target, m, text)
            start = text.rfind('\n', 0, m['at']) + 1
            stop = text.find('\n', m['at'])
            m['line'] = text[start:stop if stop > 0 else len(text)].strip()
            m['lineno'] = text.count('\n', 0, m['at']) + 1
            if m['id'] in seen:
                continue
            seen.add(m['id'])
            out.append(m)
    return out, text

class Workers:
    """
    One tree per worker, made once and reused, on a real disk.

    Copying the repository for every mutation is most of the wall clock when
    the lane itself takes a second and a half. Each worker keeps its own
    tree, and a mutation is one file written into it.

    Not under /tmp. That is a tmpfs on the machine this runs on, it is shared
    with whatever else is building there, and a mutation can produce a
    program that writes without stopping -- one run filled thirty gigabytes
    of somebody else's RAM before anything noticed. Each worker gets its own
    directory beside the repository instead, and the lane's own scratch is
    pointed inside it so that cleaning the worker cleans everything it made.
    """
    def __init__(self, count, where):
        self.free = []
        self.made = []
        os.makedirs(where, exist_ok=True)
        for _ in range(count):
            work = tempfile.mkdtemp(prefix='w.', dir=where)
            subprocess.run(['rsync', '-a', '--exclude', '.git', '--exclude', 'dist',
                            '--exclude', 'linux', '--exclude', 'fs',
                            '--exclude', '/scratch', '--exclude', '/mutate-work',
                            ROOT + '/', work + '/'], check=True, capture_output=True)
            self.free.append(work)
            self.made.append(work)
        self.lock = __import__('threading').Lock()

    def take(self):
        with self.lock:
            return self.free.pop()

    def give(self, work):
        with self.lock:
            self.free.append(work)

    def clean(self):
        for work in self.made:
            shutil.rmtree(work, ignore_errors=True)


BUILDS = {
    'src/lib.c': [
        ['sh', '-c',
         'printf \'#include "lib.c"\\n\' > $0/tu.c && '
         'for t in x86_64 aarch64 riscv64; do '
         '  clang --target=$t-unknown-linux-gnu -c $0/tu.c -o /dev/null '
         '    -I$0/src -nostdlib -ffreestanding -fno-builtin -O2 '
         '    -fno-stack-protector -fno-PIE || exit 1; '
         'done'],
    ],
}

def builds(work, path, env=None):
    """
    Does the mutated file still assemble?

    Asked separately, because a mutation that does not build is not a gap in
    the tests and should not be counted as one either way. It is also far
    cheaper to find out here than by running a whole lane.
    """
    recipe = BUILDS.get(path)
    if recipe is None:
        r = subprocess.run(['gcc', '-fsyntax-only', '-I' + os.path.join(work, 'src'),
                            os.path.join(work, path)],
                           capture_output=True, cwd=work, env=env)
        return r.returncode == 0
    for cmd in recipe:
        r = subprocess.run(cmd + [work], capture_output=True, cwd=work, env=env)
        if r.returncode != 0:
            return False
    return True



def run_group(argv, cwd, env, timeout):
    """
    Run it in its own process group, and kill the group.

    A mutation can produce a program that never stops, and killing the shell
    that started it leaves the program behind: orphaned, still spinning, still
    writing. Twenty six of them survived one interrupted run here and two were
    filling a disk through file handles nothing could see, because the files
    were already unlinked. Only the parent had been killed.

    So every lane gets its own session, and the timeout and every other way
    out kill the whole group by its negative pid rather than the one process
    that happens to be at the top of it.
    """
    child = subprocess.Popen(argv, cwd=cwd, env=env, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             start_new_session=True)
    try:
        out, err = child.communicate(timeout=timeout)
        return subprocess.CompletedProcess(argv, child.returncode, out, err)
    except subprocess.TimeoutExpired:
        _end(child)
        raise
    except BaseException:
        _end(child)
        raise


def _end(child):
    import signal, time
    for how in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(os.getpgid(child.pid), how)
        except (ProcessLookupError, PermissionError):
            return
        try:
            child.wait(timeout=5)
            return
        except subprocess.TimeoutExpired:
            time.sleep(0.2)


def stage_env(work):
    # Every compiler and every lane writes its scratch inside the worker.
    # A compiler that cannot write its assembly file fails exactly the way
    # a broken mutation does, so a full /tmp elsewhere on the machine would
    # quietly turn a whole run into "invalid" without ever saying why.
    scratch = os.path.join(work, 'scratch')
    os.makedirs(scratch, exist_ok=True)
    #   A libFuzzer smoke stops at -max_total_time when the box is busy and
    #   at -runs when it is not, so its verdict would be the box's load.
    #   The run count governs here and the clock is only a backstop.
    return dict(os.environ, TMPDIR=scratch, TEMP=scratch, TMP=scratch,
                MUTATE_BASE=os.path.join(work, '.mutate-base'),
                MOONWATER_FUZZ_RUNS='20000', MOONWATER_FUZZ_SECONDS='600')


def run_stage(stage, work, timeout):
    """One stage's exit, or None when it did not stop."""
    name, argv, own = stage
    env = stage_env(work)
    try:
        r = run_group(argv, work, env, own or timeout)
    except subprocess.TimeoutExpired:
        return None, ''
    finally:
        shutil.rmtree(os.path.join(work, 'scratch'), ignore_errors=True)
    return r.returncode, r.stdout + r.stderr


def first_failure(out):
    for line in out.splitlines():
        if 'FAIL' in line or 'Sanitizer' in line or 'runtime error' in line:
            return ' '.join(line.split())[:160]
    return ''


#   A harness that cannot lift its slice of the mutated file has not
#   judged the mutation; it has said the text no longer has the shape its
#   anchors look for. That is a mutation that did not build, not a catch.
LIFT_BROKEN = re.compile(r'does not build|did not build|substring not found|marker not found')


def baseline(stages, workers, timeout):
    """
    What each stage says of the tree as it is, asked in every worker.

    A stage is judged against this, never against zero: a harness that
    says NOT RUN here (2) says it of every mutation too, and one that
    disagrees with itself between two workers is not a judge at all.
    """
    expect = {}
    for work in list(workers.free):
        os.makedirs(os.path.join(work, '.mutate-base'), exist_ok=True)
        for stage in stages:
            status, _ = run_stage(stage, work, timeout)
            if stage[0].startswith('check:'):
                # the binary the unmutated tree builds, for the
                # compiled-the-same test in check_stage
                section = stage[0].split(':', 1)[1]
                base = os.path.join(work, '.mutate-base', 'check.' + section)
                subprocess.run(['sh', '-c', CHECK_BUILD.format(section=section, out=base)],
                               cwd=work, capture_output=True)
                if not os.path.exists(base):
                    raise SystemExit(f'{stage[0]}: the unmutated tree does not build')
                open(base + '.status', 'w').write(f'{status}\n')
            if stage[0] in expect and expect[stage[0]] != status:
                raise SystemExit(f'{stage[0]} answered {expect[stage[0]]} and '
                                 f'{status} on the same tree: not a judge')
            if status is None:
                raise SystemExit(f'{stage[0]} did not stop on the unmutated tree')
            expect[stage[0]] = status
    #   A stage that says NOT RUN of the unmutated tree says it of every
    #   mutation too, and would pass them all. It is no judge here.
    for name in sorted(n for n, s in expect.items() if s != 0):
        print(f'  {name} answered {expect[name]} unmutated: not a judge, left out',
              flush=True)
    return {n: s for n, s in expect.items() if s == 0}


def run_one(m, text, timeout, workers, stages, expect):
    """
    The verdict, and the stage that gave it.

    Without stages the lane is the judge, as it always was. With them,
    the first stage whose exit differs from the unmutated tree's caught
    the mutation; 125 from a check stage is a build that failed.
    A check stage that built the same binary as the unmutated tree says
    so, and a survivor every check stage compiled the same is marked: the
    compiler has said it is the same program there, or the section never
    reaches it.
    """
    work = workers.take()
    try:
        mutated = text[:m['at']] + m['now'] + text[m['end']:]
        target = os.path.join(work, m['path'])
        open(target, 'w').write(mutated)
        try:
            if not stages:
                env = stage_env(work)
                if not builds(work, m['path'], env):
                    return 'invalid', 'did not build'
                try:
                    r = run_group(['sh', 'test/run', m['lane']], work, env, timeout)
                except subprocess.TimeoutExpired:
                    return 'invalid', 'did not stop'
                finally:
                    # A mutant can leave a running program's output behind,
                    # and a mutant that never stops leaves a lot of it.
                    shutil.rmtree(os.path.join(work, 'scratch'), ignore_errors=True)
                if r.returncode == 0 and 'everything agrees' in r.stdout:
                    return 'survived', ''
                if r.returncode == 0:
                    return 'invalid', 'lane did not report'
                return 'caught', ''
            same = []
            for stage in stages:
                if stage[0] not in expect:
                    continue
                status, out = run_stage(stage, work, timeout)
                if status is None:
                    return 'invalid', 'did not stop: ' + stage[0]
                if status == 125 and stage[0].startswith('check:'):
                    return 'invalid', 'did not build'
                if status != expect[stage[0]]:
                    fail = first_failure(out)
                    if stage[0].startswith('harness:') and LIFT_BROKEN.search(out):
                        return 'invalid', 'lift did not build: ' + stage[0]
                    return 'caught', stage[0] + (': ' + fail if fail else '')
                if 'MUTATE-SAME' in out:
                    same.append(stage[0])
            checks = [s[0] for s in stages if s[0].startswith('check:') and s[0] in expect]
            if checks and same == checks:
                return 'survived', 'same binary'
            return 'survived', ''
        finally:
            shutil.copyfile(os.path.join(ROOT, m['path']), target)
    finally:
        workers.give(work)


SHELL_BUILD = ('gcc -O2 -static -nostdlib -nostartfiles -fno-stack-protector '
               '-fno-builtin -march=x86-64 -w -T src/build/spark.ld -Wl,-e,_start '
               '-Wl,--build-id=none -Wl,--no-warn-rwx-segments -o "{out}" programs/shell.c')

def shell_stage(*harnesses, timeout=None):
    """
    The shell built from the mutated tree, and harnesses that drive it.

    These are the ones that see wget, the DHCP and SNTP clients and link
    as a person runs them (tls_peer, wget_hostile, https_downgrade,
    net_netem, waterlink_link); a build of the shell is half a minute, so
    they judge only what the cheap stages let through. A harness spelled
    with arguments is a tuple.
    """
    runs = []
    for h in harnesses:
        name, *extra = (h,) if isinstance(h, str) else h
        runs.append('python3 test/differential.py --harness %s --shell "$s" %s'
                    ' || exit 1' % (name, ' '.join(extra)))
    script = ('s="$TMPDIR/shell"; ' + SHELL_BUILD.format(out='$s')
              + ' 2> "$s.err" || exit 125; ' + '; '.join(runs))
    label = '+'.join(h if isinstance(h, str) else '%s %s' % (h[0], ' '.join(h[1:]))
                     for h in harnesses)
    return ('shell:' + label, ['sh', '-c', script], timeout)

#
#       What a stage survivor is taken to before anybody calls it a gap:
#       the harnesses that build the shell and drive it, and the checks the
#       cheap stages leave to the lanes, which no stage can afford for
#       every mutation.
#
WATERLINK_CONFIRM = [harness_stage('waterlink_fuzz'), harness_stage('waterlink_pre_fuzz'),
                     shell_stage('waterlink_link')]
CONFIRM = {
    'net': [harness_stage('tls_chains'), harness_stage('crypto_fuzz'),
            harness_stage('tls_verify_fuzz'),
            shell_stage('tls_peer', ('tls_peer', '--mutate', '100'), 'https_downgrade',
                        'wget_mutation', 'wget_hostile')],
    'wait': [shell_stage('net_netem', 'tls_peer', 'wget_hostile')],
    'discover': WATERLINK_CONFIRM, 'nearby': WATERLINK_CONFIRM,
    'link': WATERLINK_CONFIRM, 'handshake': WATERLINK_CONFIRM,
    'seal': WATERLINK_CONFIRM,
    'service': WATERLINK_CONFIRM + [shell_stage('moonwater_cli')],
    'command': WATERLINK_CONFIRM + [shell_stage('moonwater_cli')],
    'shnet': [check_stage('net'), shell_stage('net_netem')],
    'host': [shell_stage('net_netem', 'moonwater_cli')],
    'bowl': [harness_stage('bowl_session'), harness_stage('bowl_roots')],
}


def load_ledger():
    try:
        return json.load(open(LEDGER))
    except (OSError, ValueError):
        return {}

def shown(text, width=40):
    text = ' '.join(text.split())
    return text if len(text) <= width else text[:width - 3] + '...'

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--target', action='append')
    p.add_argument('--operator', action='append',
                   choices=sorted(OPERATORS), default=None)
    p.add_argument('--routine', action='append',
                   help='only mutations inside this routine (a regular expression, '
                        'matched whole; may be given more than once)')
    p.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    p.add_argument('--timeout', type=int, default=45)
    p.add_argument('--work', default=os.path.join(ROOT, '..', 'mutate-work'),
                   help='where the worker trees go; not /tmp on purpose')
    p.add_argument('--limit', type=int)
    p.add_argument('--sample', type=int,
                   help='this many mutations, drawn by --seed from the plan')
    p.add_argument('--seed', type=int, default=1,
                   help='the draw of --sample; the same seed is the same mutations')
    p.add_argument('--lane', action='store_true',
                   help='judge by the whole lane even where the target has stages')
    p.add_argument('--results', help='write every verdict here as JSON, sorted by id')
    p.add_argument('--rerun', help='only the mutations a --results file says survived')
    p.add_argument('--confirm', action='store_true',
                   help='take every stage survivor to the stages in CONFIRM')
    p.add_argument('--confirm-timeout', type=int, default=3600)
    p.add_argument('--list', action='store_true')
    args = p.parse_args()

    targets = args.target or list(TARGETS)
    operators = args.operator or ['plusminus']

    plan = []
    texts = {}
    for t in targets:
        got, text = build(t, operators)
        if args.routine:
            got = [m for m in got
                   if any(re.fullmatch(r, m['routine']) for r in args.routine)]
        texts[t] = text
        plan += got

    #   The plan is in file order; a sample is drawn from it by the seed
    #   alone and put back in that order, so a run is the same run on any
    #   machine that has the same tree.
    if args.sample and args.sample < len(plan):
        import random
        keep = set(random.Random(args.seed).sample(range(len(plan)), args.sample))
        plan = [m for i, m in enumerate(plan) if i in keep]

    if args.rerun:
        before = {r['id'] for r in json.load(open(args.rerun)) if r['verdict'] == 'survived'}
        plan = [m for m in plan if m['id'] in before]

    if args.limit:
        plan = plan[:args.limit]

    if args.list:
        for m in plan:
            print(f"{m['target']:8} {m['routine']:26} {m['operator']:10} "
                  f"{shown(m['was'], 30):>10} -> {shown(m['now'], 30)}")
        print(f"\n{len(plan)} mutations over {len(targets)} targets")
        return 0

    ledger = load_ledger()
    print(f"{len(plan)} mutations, {args.jobs} at a time")

    workers = Workers(args.jobs, os.path.abspath(args.work))

    #   Stages for the targets in this plan, and what each says unmutated.
    staged = {} if args.lane else {t: STAGES[t] for t in targets if t in STAGES}
    expect = {}
    if staged:
        every = {}
        for t in targets:
            for s in staged.get(t, ()):
                every[s[0]] = s
        expect = baseline(list(every.values()), workers, args.timeout)
        print('unmutated: ' + ', '.join(f'{k} {v}' for k, v in sorted(expect.items())),
              flush=True)

    verdicts = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_one, m, texts[m['target']], args.timeout, workers,
                               staged.get(m['target']), expect): m
                   for m in plan}
        for done in concurrent.futures.as_completed(futures):
            m = futures[done]
            verdict, why = done.result()
            verdicts.append((m, verdict, why))
            if len(verdicts) % 25 == 0:
                print(f"  {len(verdicts)}/{len(plan)}", flush=True)

    survivors = [(i, v) for i, v in enumerate(verdicts)
                 if v[1] == 'survived' and v[0]['target'] in CONFIRM]
    if args.confirm and survivors:
        print(f"confirming {len(survivors)} survivors", flush=True)
        every = {}
        for _, v in survivors:
            for s in CONFIRM[v[0]['target']]:
                every[s[0]] = s
        more = baseline(list(every.values()), workers, args.confirm_timeout)
        print('unmutated: ' + ', '.join(f'{k} {v}' for k, v in sorted(more.items())),
              flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(run_one, v[0], texts[v[0]['target']],
                                   args.confirm_timeout, workers,
                                   CONFIRM[v[0]['target']], more): i
                       for i, v in survivors}
            for done in concurrent.futures.as_completed(futures):
                i = futures[done]
                verdict, why = done.result()
                if verdict == 'survived':
                    why = '; '.join(w for w in (verdicts[i][2], 'confirmed') if w)
                verdicts[i] = (verdicts[i][0], verdict, why)

    workers.clean()
    verdicts.sort(key=lambda v: (v[0]['target'], v[0]['lineno'], v[0]['id']))

    caught = sum(1 for _, v, _ in verdicts if v == 'caught')
    invalid = sum(1 for _, v, _ in verdicts if v == 'invalid')
    survived = [(m, why) for m, v, why in verdicts if v == 'survived']
    known = [(m, why) for m, why in survived if m['id'] in ledger]
    new = [(m, why) for m, why in survived if m['id'] not in ledger]

    print(f"\n  caught   {caught}")
    print(f"  invalid  {invalid}   (did not build, or did not stop)")
    print(f"  survived {len(survived)}   ({len(new)} nobody has judged)")

    #   The score of a file is caught over caught and survived; a mutation
    #   the ledger calls equivalent is not a program the tests could tell
    #   apart, so it is counted in neither.
    print(f"\n  {'target':10} {'caught':>7} {'survived':>9} {'equiv':>6} "
          f"{'invalid':>8} {'score':>6}")
    for t in targets:
        mine = [(m, v) for m, v, _ in verdicts if m['target'] == t]
        if not mine:
            continue
        c = sum(1 for _, v in mine if v == 'caught')
        e = sum(1 for m, v in mine if v == 'survived' and m['id'] in ledger)
        s = sum(1 for _, v in mine if v == 'survived') - e
        i = sum(1 for _, v in mine if v == 'invalid')
        score = f"{100 * c / (c + s):.1f}" if c + s else '-'
        print(f"  {t:10} {c:7} {s:9} {e:6} {i:8} {score:>6}")

    if args.results:
        with open(args.results, 'w') as f:
            json.dump([{'id': m['id'], 'target': m['target'], 'path': m['path'],
                        'lineno': m['lineno'], 'routine': m['routine'],
                        'operator': m['operator'], 'was': m['was'], 'now': m['now'],
                        'line': m['line'], 'verdict': v, 'why': why}
                       for m, v, why in verdicts], f, indent=1, sort_keys=True)

    if known:
        print(f"\nsurvivors already in the ledger:")
        for m, _ in known:
            print(f"  {m['path']}:{m['lineno']}  {m['routine']}  "
                  f"{shown(m['was'])} -> {shown(m['now'])}")
            print(f"      {ledger[m['id']].get('why', 'no reason written down')}")

    if new:
        print(f"\nnobody has judged these -- each is a change no test could see:")
        for m, why in new:
            print(f"  {m['path']}:{m['lineno']}  {m['routine']}  "
                  f"{shown(m['was'])} -> {shown(m['now'])}"
                  + (f"  ({why})" if why else ''))
            print(f"      {m['line'][:104]}")
            print(f"      {m['id']}")

    return 1 if new else 0

sys.exit(main())
