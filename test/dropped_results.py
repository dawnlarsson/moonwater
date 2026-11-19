#!/usr/bin/env python3
"""
Results that are dropped where they are almost always used.

A function whose answer is tested or kept at nearly every call is one whose
answer matters, and the few calls that let it fall are either a decision
nobody wrote down or a failure nobody hears of. This lists them: for every
function used at USED_LEAST call sites or more, the statements that are the
call and nothing else, when they are at most one in DROPPED_SHARE of its calls.
A drop that is meant says so, as the tree does elsewhere: (void)name(...).
And because (void) is also how a failure is hidden, every (void) that drops a
status is listed in the seal with the reason it is right (void_survey below).

    python3 test/dropped_results.py              the sites
    python3 test/dropped_results.py --summary    the count, by function
    python3 test/dropped_results.py --check      against test/dropped_results.seal
    python3 test/dropped_results.py --seal       write the seal (the count may only fall)
    python3 test/dropped_results.py --voids      the (void) drops of a status, with the seal's reason for each

It reads source text, not types: statements are cut at semicolons and braces
outside parentheses, strings and comments, so a call that is one operand of a
longer expression is a use. Macros that are statements read as calls; the
few that are named in MACROS are left out.
"""
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SEAL = ROOT / 'test/dropped_results.seal'
USED_LEAST = 15
DROPPED_SHARE = 4
SKIP = ('src/lib.c', 'src/sh/zstd.c', 'src/sh/xz.c', 'src/sh/gzip.c')
WORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'else', 'do', 'case', 'defined'}
MACROS = {'return_if', 'scope_exit', 'address_to', 'address_of'}
# What a function answers for its caller's convenience and not as news: a refusal's status handed back so it can be
# returned in one line, the place a formatter stopped, a block's own address.
CONVENIENCE = {'host_fail', 'host_refuse', 'bowl_fail', 'bowl_refuse', 'build_die', 'shell_answered', 'text_done',
               'positive_into_padded', 'bipolar_into_string', 'gpu_text_put', 'if_rare', 'compositor_corner_mask',
               'network_order_32', 'compositor_screens', 'exec_node'}
CALL = re.compile(r'\b([a-z][a-z0-9_]{3,})\s*\(')
WHOLE = re.compile(r'^([a-z][a-z0-9_]{3,})\s*\((.*)\)$', re.S)
DECLARES = re.compile(r'^(static|extern|typedef|struct|union|enum|const|unsigned|signed|inline|COLD|HOT|PURE|CONST|'
                      r'READS|WRITES|READS_WRITES|DEAD_END|[A-Za-z_][A-Za-z0-9_]*\s+\**[A-Za-z_])')


def strip(text):
    """Comments and the insides of strings and character constants, as spaces."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append(c + ''.join(ch if ch == '\n' else ' ' for ch in text[i + 1:j]) + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def statements(text):
    """(line, statement) for each run between ; { } outside parentheses."""
    depth, start, line, first = 0, 0, 1, 1
    for i, c in enumerate(text):
        if c == '(':
            depth += 1
        elif c == ')':
            depth = max(0, depth - 1)
        elif c == '\n':
            line += 1
        if depth == 0 and c in ';{}':
            piece = text[start:i].strip()
            if piece:
                lead = len(text[start:i]) - len(text[start:i].lstrip())
                yield first + text[start:start + lead].count('\n'), piece, start + lead
            start, first = i + 1, line
            continue


def survey():
    used = collections.Counter()
    dropped = collections.defaultdict(list)
    for path in sorted(ROOT.glob('src/**/*.c')):
        name = path.relative_to(ROOT).as_posix()
        if name in SKIP:
            continue
        text = strip(path.read_text(errors='replace'))
        text = re.sub(r'^[ \t]*#.*(?:\\\n.*)*', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.M)
        for line, piece, _ in statements(text):
            piece = re.sub(r'^(?:else\s+|do\s+)+', '', piece)
            piece = re.sub(r'^(?:if|while|for|switch)\s*\((?:[^()]|\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\))*\)\s*', '', piece)
            whole = WHOLE.match(piece)
            inner = piece
            if whole and whole.group(1) not in WORDS | MACROS and balanced(whole.group(2)):
                dropped[whole.group(1)].append((name, line))
                inner = whole.group(2)
            elif DECLARES.match(piece) and '=' not in piece:
                continue
            for call in CALL.findall(inner):
                if call not in WORDS | MACROS:
                    used[call] += 1
    found = {}
    for function, sites in dropped.items():
        if (function not in CONVENIENCE and used[function] >= USED_LEAST and
                len(sites) * DROPPED_SHARE <= used[function] + len(sites)):
            found[function] = (used[function], sites)
    return found


def balanced(text):
    depth = 0
    for c in text:
        depth += c == '('
        depth -= c == ')'
        if depth < 0:
            return False
    return depth == 0


# (void) is the tree's word for a drop that is meant, and so the survey above takes it for a use: a reviewed drop
# and a failure nobody hears of read the same there. A status -- the answer of a function declared to return bool,
# bipolar, b32, b64, int or long -- dropped with (void) is listed by its file, the function it is in and the function
# it drops, and each such site has its reason in the seal. One that is not there fails, and so does one that is there
# and no longer found; the count of them, and of those still marked unreviewed, may only fall.
STATUS = {'bool', 'bipolar', 'b32', 'b64', 'int', 'long'}
DEFINES = re.compile(r'^(?:(?:static|inline|extern|COLD|HOT|PURE|CONST|DEAD_END|READS|WRITES|READS_WRITES)\s+)*'
                     r'([a-z_0-9]+)\s+\**\s*([a-z][a-z0-9_]{3,})\s*\(', re.M)
VOIDED = re.compile(r'^\(\s*void\s*\)\s*([a-z][a-z0-9_]{3,})\s*\((.*)\)$', re.S)
HEADER = re.compile(r'([a-z_][a-z0-9_]*)\s*\((?:[^()]|\([^()]*\))*\)\s*(?:[A-Z_]+\s*)*$', re.S)
UNREVIEWED = 'unreviewed'


def returns():
    """Each function name and the types it is defined or declared to return, anywhere in src."""
    types = collections.defaultdict(set)
    for path in sorted(ROOT.glob('src/**/*.[ch]')):
        for m in DEFINES.finditer(path.read_text(errors='replace')):
            types[m.group(2)].add(m.group(1))
    return types


def functions(text):
    """(start, end, name) of each function body at file level."""
    spans, depth, paren, start, name, body = [], 0, 0, 0, None, 0
    for i, c in enumerate(text):
        if c == '(':
            paren += 1
        elif c == ')':
            paren = max(0, paren - 1)
        elif c == '{' and not paren:
            if depth == 0:
                head = text[start:i]
                m = HEADER.search(head)
                name = m.group(1) if m and '=' not in head[m.start():] else None
                body = i
            depth += 1
        elif c == '}' and not paren:
            depth = max(0, depth - 1)
            if depth == 0:
                if name:
                    spans.append((body, i, name))
                name = None
                start = i + 1
        elif c == ';' and depth == 0 and not paren:
            start = i + 1
    return spans


def void_survey():
    """{(file, caller, callee): [lines]} for every (void) drop of a status."""
    types = returns()
    found = collections.defaultdict(list)
    for path in sorted(ROOT.glob('src/**/*.c')):
        name = path.relative_to(ROOT).as_posix()
        if name in SKIP:
            continue
        text = strip(path.read_text(errors='replace'))
        text = re.sub(r'^[ \t]*#.*(?:\\\n.*)*', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.M)
        spans = functions(text)
        for line, piece, at in statements(text):
            piece = re.sub(r'^(?:else\s+|do\s+)+', '', piece)
            piece = re.sub(r'^(?:if|while|for|switch)\s*\((?:[^()]|\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\))*\)\s*', '', piece)
            piece = re.sub(r'^(?:(?:case\s+[^:?]+|default)\s*:(?!:)\s*)+', '', piece)
            m = VOIDED.match(piece)
            if (not m or not balanced(m.group(2)) or m.group(1) in CONVENIENCE or
                    not types.get(m.group(1), set()) & STATUS):
                continue
            caller = next((n for b, e, n in spans if b < at < e), '-')
            found[(name, caller, m.group(1))].append(line)
    return found


def void_seal():
    """The seal's (void) lines, {key: reason}, and the counts it allows."""
    said, most, unreviewed = {}, None, None
    for line in SEAL.read_text().splitlines():
        if line.startswith('voids '):
            most = int(line.split()[1])
        elif line.startswith('unreviewed '):
            unreviewed = int(line.split()[1])
        elif line.startswith('void '):
            key, _, reason = line[5:].partition('\t')
            said[key] = reason.strip()
    return said, most, unreviewed


def void_key(site, lines):
    return '%s %s %s %d' % (site[0], site[1], site[2], len(lines))


def main(argv):
    found = survey()
    count = sum(len(sites) for _, sites in found.values())
    voids = void_survey()
    keys = {void_key(site, lines): (site, lines) for site, lines in voids.items()}
    total = sum(len(lines) for lines in voids.values())
    said, most, unreviewed = void_seal()
    if '--seal' in argv:
        new = [k for k in keys if k not in said]
        if most is not None and new:
            for k in sorted(new):
                site, lines = keys[k]
                print('dropped results: %s:%d: (void)%s in %s is in no line of the seal: handle its status, or write '
                      'the line by hand with its reason (void %s<TAB>why) and raise voids'
                      % (site[0], lines[0], site[2], site[1], k))
            return 1
        reasons = {k: said.get(k, UNREVIEWED) for k in keys}
        waiting = sum(1 for k in keys if reasons[k] == UNREVIEWED)
        if most is not None and (total > most or waiting > (unreviewed or 0)):
            print('dropped results: %d (void) drops (sealed %d), %d unreviewed (sealed %d): the seal may only fall'
                  % (total, most, waiting, unreviewed or 0))
            return 1
        body = ''.join('void %s\t%s\n' % (k, reasons[k]) for k in sorted(keys))
        SEAL.write_text('# Results dropped where they are almost always used (python3 test/dropped_results.py):\n'
                        '# the tree may not have more than this, and the aim is none.\n'
                        f'dropped {count}\nfunctions {len(found)}\n'
                        '# A status (a bool, bipolar, b32, b64, int or long answer) dropped with (void): a line a site, void, the\n'
                        '# file, the function it is in, the function dropped and how many times there, a tab, why the drop is\n'
                        '# right. A drop with no line fails, and a line with no drop. voids and unreviewed may only fall.\n'
                        f'voids {total}\nunreviewed {waiting}\n' + body)
        print(f'dropped results: sealed at {count} sites of {len(found)} functions; {total} (void) drops of a status, '
              f'{waiting} unreviewed')
        return 0
    if '--check' in argv:
        status = 0
        sealed = int(re.search(r'^dropped (\d+)', SEAL.read_text(), re.M).group(1))
        if count > sealed:
            print(f'dropped results: {count} sites, sealed {sealed}: a result that is used nearly everywhere is '
                  f'dropped at a new call; handle it, or say the drop is meant with (void) and why in the seal')
            status = 1
        elif count < sealed:
            print(f'dropped results: {count} sites, sealed {sealed}: lower the seal (--seal)')
            status = 1
        for k in sorted(keys):
            site, lines = keys[k]
            if k not in said:
                print('dropped results: %s:%d: (void)%s in %s drops a status and the seal has no line for it: handle '
                      'the status, or add "void %s<TAB>why" by hand' % (site[0], lines[0], site[2], site[1], k))
                status = 1
            elif not said[k]:
                print("dropped results: the seal's line for %s has no reason" % k)
                status = 1
        for k in sorted(set(said) - set(keys)):
            print('dropped results: the seal lists (void) %s and it is no longer found: take the line out (--seal)' % k)
            status = 1
        waiting = sum(1 for k in said if said[k] == UNREVIEWED)
        if most is None or total > most:
            print(f"dropped results: {total} (void) drops of a status, over the seal's voids {most}: a status is "
                  f"dropped at a new call")
            status = 1
        elif total < most:
            print(f'dropped results: {total} (void) drops of a status, sealed {most}: lower the seal (--seal)')
            status = 1
        if unreviewed is None or waiting > unreviewed:
            print(f"dropped results: {waiting} lines marked {UNREVIEWED}, over the seal's {unreviewed}")
            status = 1
        elif waiting < unreviewed:
            print(f'dropped results: {waiting} lines marked {UNREVIEWED}, sealed {unreviewed}: lower the seal (--seal)')
            status = 1
        if not status:
            print(f'dropped results: {count} sites of {len(found)} functions, and {total} (void) drops of a status '
                  f'({waiting} still {UNREVIEWED}), as sealed')
        return status
    if '--voids' in argv:
        for k in sorted(keys):
            site, lines = keys[k]
            print('%s:%s: (void)%s in %s  [%s]' % (site[0], ','.join(map(str, lines)), site[2], site[1],
                                                   said.get(k, 'NOT IN THE SEAL')))
        print(f'{total} (void) drops of a status at {len(keys)} lines of the seal')
        return 0
    for function, (uses, sites) in sorted(found.items(), key=lambda item: -item[1][0]):
        if '--summary' in argv:
            print(f'{function:36s} used {uses:4d}  dropped {len(sites)}')
        else:
            for name, line in sites:
                print(f'{name}:{line}: {function} (used {uses}, dropped {len(sites)})')
    print(f'{count} sites of {len(found)} functions')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
