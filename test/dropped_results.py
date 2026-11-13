#!/usr/bin/env python3
"""
Results that are dropped where they are almost always used.

A function whose answer is tested or kept at nearly every call is one whose
answer matters, and the few calls that let it fall are either a decision
nobody wrote down or a failure nobody hears of. This lists them: for every
function used at USED_LEAST call sites or more, the statements that are the
call and nothing else, when they are at most one in DROPPED_SHARE of its calls.
A drop that is meant says so, as the tree does elsewhere: (void)name(...).

    python3 test/dropped_results.py              the sites
    python3 test/dropped_results.py --summary    the count, by function
    python3 test/dropped_results.py --check      against test/dropped_results.seal
    python3 test/dropped_results.py --seal       write the seal (the count may only fall)

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
                yield first + text[start:start + lead].count('\n'), piece
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
        for line, piece in statements(text):
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


def main(argv):
    found = survey()
    count = sum(len(sites) for _, sites in found.values())
    if '--seal' in argv:
        SEAL.write_text('# Results dropped where they are almost always used (python3 test/dropped_results.py):\n'
                        '# the tree may not have more than this, and the aim is none.\n'
                        f'dropped {count}\nfunctions {len(found)}\n')
        print(f'dropped results: sealed at {count} sites of {len(found)} functions')
        return 0
    if '--check' in argv:
        sealed = int(re.search(r'^dropped (\d+)', SEAL.read_text(), re.M).group(1))
        if count > sealed:
            print(f'dropped results: {count} sites, sealed {sealed}: a result that is used nearly everywhere is '
                  f'dropped at a new call; handle it, or say the drop is meant with (void)')
            return 1
        if count < sealed:
            print(f'dropped results: {count} sites, sealed {sealed}: lower the seal (--seal)')
            return 1
        print(f'dropped results: {count} sites of {len(found)} functions, as sealed')
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
