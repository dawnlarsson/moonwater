#!/usr/bin/env python3
"""List every production function entry, including generated bodies/aliases.

Debug information and object symbol tables are deliberately not the inventory:
an optimizer may erase an unused or inlined function.  This reads definitions
and alias declarations from source with the same comment/string-safe lexer as
the assembly inventory.  Function-generating macros are listed below because
their definitions, rather than their expansions, are what raw source contains.

The seal verifies inventory and source integrity only. It does not establish
manual review coverage, branch coverage, or freedom from defects.

    python3 test/function_audit.py
    python3 test/function_audit.py --summary
"""

import argparse
import hashlib
import pathlib
import re
import sys
from collections import Counter, namedtuple

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'assembly'))
from inventory import c_bodies, graph_assembly_inventory, included_audit, lex


ROOT = pathlib.Path(__file__).resolve().parents[1]
SEAL = ROOT / 'test/function_audit.seal'
Definition = namedtuple('Definition', 'path line name kind')

# One invocation can emit more than one body.  The format strings receive the
# first macro argument.  Keeping this table tiny and explicit also makes a new
# body-generating macro a visible inventory event rather than a parser guess.
GENERATORS = {
    ('src/canvas/canvas.c', 'REPORT_CANVAS'): ('{}',),
    ('src/lib.util.c', 'SEARCH_KNOWN'): ('{}',),
    ('src/sh/awk.c', 'AWK_BINARY_LEVEL'): ('{}',),
    ('src/sh/awk.c', 'AWK_EVALUATOR'): ('{}',),
    ('src/sh/awk.c', 'AWK_TRIG_KERNEL'): ('{}',),
    ('src/sh/awk.c', 'AWK_TRIG'): ('{}',),
    ('src/sh/expand.c', 'ARITH_LEVEL'): ('{}',),
    ('src/sh/expand.c', 'ARITH_LOGICAL_LEVEL'): ('{}',),
    ('src/sh/exec.c', 'CONDITIONAL_LOGICAL_LEVEL'): ('{}',),
    ('src/sh/file.c', 'FILE_TREE_NODE_NEW'): ('{}',),
    ('src/sh/snapshot.c', 'SYSTEM_FIELD'): ('{}',),
    ('src/sh/builtin.c', 'STORAGE_ADAPTER'):
        ('shell_{}', 'storage_program_{}'),
    ('src/sh/builtin.c', 'TEST_LOGICAL_LEVEL'): ('{}',),
    ('src/sh/storage.c', 'STORAGE_TABLE_RELEASE'): ('{}',),
    ('src/sh/tools.c', 'PS_TAKE'): ('{}',),
    ('src/lib.util.c', 'PROCESS_EXECL'): ('{}',),
    ('src/lib.util.c', 'ERROR_OPEN'): ('{}',),
    ('src/lib.util.c', 'ERROR_ENTRY'): ('{}',),
    ('src/lib.util.c', 'ERROR_ID'): ('{}',),
    ('src/lib.util.c', 'var_list_entry'): ('{}',),
    ('src/lib.util.c', 'var_list_entry'): ('{}',),
    ('src/lib.util.c', 'var_list_entry'): ('{}',),
    ('src/lib.util.c', 'MATH_CLASSIFIER'):
        ('{}_magnitude_bits', '{}_is_nan', '{}_is_infinite', '{}_is_finite',
         '{}_is_normal', '{}_sign_bit', '{}_class'),
    ('src/lib.util.c', 'NUMBERS_EXACT'): ('{}',),
    ('src/lib.util.c', 'NUMBERS_TO'): ('{}',),
    ('src/lib.util.c', 'SIGNAL_SET_CHANGE'): ('{}',),
    ('src/lib.util.c', 'SPOOL_PIPELINE_SLOT'): ('{}',),
    ('src/lib.util.c', 'STDLIB_DIVIDE'): ('{}',),
}


def production_sources():
    paths = []
    for root in (ROOT / 'programs', ROOT / 'src', ROOT / 'kernel/patch'):
        paths += [path for path in root.rglob('*')
                  if path.suffix in ('.c', '.h', '.inc')]
    #   kernel/kernel.c is assembly and its tags and offsets, with no C body,
    #   but it is production source and a change to it has to break the seal.
    paths.append(ROOT / 'kernel/kernel.c')
    return sorted(path for path in paths if 'test' not in path.parts)


def audited_sources():
    paths = []
    for root in (ROOT / 'programs', ROOT / 'src', ROOT / 'kernel/patch',
                 ROOT / 'kernel/replace'):
        paths += [path for path in root.rglob('*')
                  if path.suffix in ('.c', '.h', '.inc', '.asm') and
                  'test' not in path.parts]
    paths.append(ROOT / 'kernel/kernel.c')
    paths.append(ROOT / 'kernel/patch/functions')
    # A parser change must invalidate the inventory sealed by the old parser.
    paths += [ROOT / 'test/function_audit.py',
              ROOT / 'test/assembly/inventory.py']
    return sorted(paths)


def source_digest():
    digest = hashlib.sha256()
    for path in audited_sources():
        relative = path.relative_to(ROOT).as_posix().encode()
        digest.update(len(relative).to_bytes(4, 'little'))
        digest.update(relative)
        data = path.read_bytes()
        digest.update(len(data).to_bytes(8, 'little'))
        digest.update(data)
    return digest.hexdigest()


def ordinary_definitions(path):
    """Use the compact inventory's comment/string-safe C declarator parser."""
    relative = path.relative_to(ROOT).as_posix()
    return [Definition(relative, item.line, item.name, 'body')
            for item in c_bodies(str(path))]


def alias_definitions(path):
    """Find function symbols implemented as aliases instead of wrappers."""
    relative = path.relative_to(ROOT).as_posix()
    tokens, _ = lex(path.read_text(encoding='utf-8', errors='replace'))
    definitions = []
    brace_depth = 0
    boundary = 0

    for index, token in enumerate(tokens):
        if token.value == '{':
            brace_depth += 1
        elif token.value == '}':
            brace_depth -= 1
            if brace_depth == 0:
                boundary = index + 1
        elif token.value == ';' and brace_depth == 0:
            declaration = tokens[boundary:index]
            boundary = index + 1
            if not any(item.value == 'alias' for item in declaration):
                continue

            depth = 0
            groups = []
            for at, item in enumerate(declaration):
                if item.value == '(':
                    if (depth == 0 and at and
                            declaration[at - 1].kind == 'identifier'):
                        groups.append((declaration[at - 1].value, at))
                    depth += 1
                elif item.value == ')' and depth:
                    depth -= 1
            if groups:
                name, at = groups[0]
                definitions.append(Definition(
                    relative, declaration[at - 1].line, name, 'alias'))

    return definitions


def macro_arguments(tokens, open_at):
    """Return the tokens of a macro call's top-level arguments."""
    arguments, start, depth = [], open_at + 1, 1
    at = start
    while at < len(tokens):
        value = tokens[at].value
        if value == '(':
            depth += 1
        elif value == ')':
            depth -= 1
            if depth == 0:
                arguments.append(tokens[start:at])
                return arguments, at
        elif value == ',' and depth == 1:
            arguments.append(tokens[start:at])
            start = at + 1
        at += 1
    return [], open_at


def generated_definitions(path):
    relative = path.relative_to(ROOT).as_posix()
    wanted = {macro: templates for (source, macro), templates in
              GENERATORS.items() if source == relative}
    if not wanted:
        return []

    tokens, _ = lex(path.read_text(encoding='utf-8', errors='replace'))
    definitions = []
    brace_depth = 0
    at = 0
    while at + 1 < len(tokens):
        token = tokens[at]
        if token.value == '{':
            brace_depth += 1
        elif token.value == '}':
            brace_depth -= 1
        elif (brace_depth == 0 and token.value in wanted and
              tokens[at + 1].value == '('):
            arguments, end = macro_arguments(tokens, at + 1)
            if not arguments or len(arguments[0]) != 1 or \
                    arguments[0][0].kind != 'identifier':
                raise RuntimeError('%s:%d: cannot read generated function name'
                                   % (relative, token.line))
            argument = arguments[0][0].value
            for template in wanted[token.value]:
                definitions.append(Definition(
                    relative, token.line, template.format(argument),
                    'generated'))
            at = end
        at += 1
    return definitions


def inventory():
    definitions = []
    for path in production_sources():
        definitions += ordinary_definitions(path)
        definitions += alias_definitions(path)
        definitions += generated_definitions(path)
    return sorted(definitions)


def library_routines():
    """Return semantic routines, architecture bodies, and zero-body aliases."""
    _, _, _, sources = included_audit(str(ROOT / 'src/lib.c'), 'linux')
    have, order, _, _, _, _, _ = graph_assembly_inventory(sources)

    # Architecture-local aliases are already semantic routines in ``order``.
    # What remains is the libc/API spelling with no additional body.
    alias_names = set()
    for source in sources.values():
        tokens, _ = lex(source)
        for index in range(len(tokens) - 3):
            if (tokens[index].value == 'ASM_ALIAS' and
                    tokens[index + 1].value == '(' and
                    tokens[index + 2].kind == 'identifier' and
                    tokens[index + 3].value == ','):
                alias_names.add(tokens[index + 2].value)

    return (order, sum(len(arches) for arches in have.values()),
            sorted(alias_names - set(order)))


def marked_assembly(root):
    """Return architecture bodies declared with #> arch/SYM_FUNC_START.

    A directory that is not there is an error and not an empty answer: the
    Canvas count read src/canvas for a week after the rename moved Canvas to
    src/moonwater, and a glob of a missing path is empty, so the seal's
    canvas_asm 0 was a directory nobody had rather than a count of nothing."""
    if not root.is_dir():
        raise SystemExit('function audit: %s is not a directory; the assembly '
                         'counted there has moved' % root.relative_to(ROOT).as_posix())
    bodies = []
    for path in sorted(root.glob('*.asm')):
        architecture = None
        for line, text in enumerate(path.read_text(
                encoding='utf-8', errors='replace').splitlines(), 1):
            match = re.match(r'\s*#>\s*arch\s+(\S+)', text)
            if match:
                architecture = match.group(1)
                continue
            match = re.match(r'\s*SYM_FUNC_START\(([^)]+)\)', text)
            if match and architecture not in (None, 'other', 'shared'):
                bodies.append((path.relative_to(ROOT).as_posix(), line,
                               match.group(1), architecture))
    return bodies


def kernel_c_assembly():
    """The routines of kernel/kernel.c, one body for each ASM_FUNC, on the
    architecture of the #ifdef it sits in: the kernel functions rewritten in
    assembly, which kernel/patch/functions puts in the place of their C
    originals."""
    path = ROOT / 'kernel/kernel.c'
    if not path.is_file():
        raise SystemExit('function audit: kernel/kernel.c is gone; the kernel '
                         'assembly counted there has moved')
    bodies, architecture = [], None
    names = {'CONFIG_X86_64': 'x86_64', 'CONFIG_ARM64': 'arm64', 'CONFIG_RISCV': 'riscv64'}
    for line, text in enumerate(path.read_text(
            encoding='utf-8', errors='replace').splitlines(), 1):
        match = re.match(r'#ifdef\s+(CONFIG_X86_64|CONFIG_ARM64|CONFIG_RISCV)\b', text)
        if match:
            architecture = names[match.group(1)]
            continue
        match = re.match(r'\s*ASM_FUNC\(([^)]+)\)', text)
        if match and architecture:
            bodies.append((path.relative_to(ROOT).as_posix(), line,
                           match.group(1), architecture))
    return bodies


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--summary', action='store_true')
    parser.add_argument('--check', action='store_true',
                        help='require the production source to match the last '
                             'complete audit')
    parser.add_argument('--seal', action='store_true',
                        help='write the seal from the tree as it stands, for '
                             'after a change has been accounted for')
    arguments = parser.parse_args()
    definitions = inventory()

    keys = Counter((item.path, item.line, item.name) for item in definitions)
    duplicates = [key for key, count in keys.items() if count != 1]
    if duplicates:
        for path, line, name in duplicates:
            print('function audit: duplicate definition %s:%d:%s' %
                  (path, line, name), file=sys.stderr)
        return 1

    library, library_bodies, library_aliases = library_routines()
    canvas = marked_assembly(ROOT / 'src/canvas')
    canvas_names = {item[2] for item in canvas}
    kernel = marked_assembly(ROOT / 'kernel/replace') + kernel_c_assembly()

    def sealed_counts():
        """The eight numbers the seal carries, asked in one place so that the
           branch which writes them and the branch which checks them cannot
           come to disagree about what is being counted."""
        return {
            'sha256': source_digest(),
            'c_functions': str(len(definitions)),
            'library_asm': str(len(library)),
            'library_arch_bodies': str(library_bodies),
            'library_aliases': str(len(library_aliases)),
            'canvas_asm': str(len(canvas_names)),
            'canvas_arch_bodies': str(len(canvas)),
            'kernel_asm_bodies': str(len(kernel)),
        }

    if arguments.seal:
        """
                The seal is a hash of the production sources as well as a
                count, so any edit moves it and every one of them has to be
                rewritten by hand. Doing that by hand is how a wrong number
                gets written down, so the writing is mechanical and the
                deciding is not: this says what moved and leaves the reader
                to say whether it should have.
        """
        counts = sealed_counts()
        was = {}
        if SEAL.is_file():
            was = dict(line.split(None, 1)
                       for line in SEAL.read_text().splitlines()
                       if line and not line.startswith('#'))
        text = SEAL.read_text() if SEAL.is_file() else (
            '# Production-function inventory seal; not a proof of review '
            'coverage or correctness.\n' +
            ''.join(f'{key} \n' for key in counts))
        for key, value in counts.items():
            text = re.sub(rf'^{key} .*$', f'{key} {value}', text, flags=re.M)
        SEAL.write_text(text)
        for key, value in counts.items():
            if was.get(key) != value:
                before = was.get(key, 'nothing')
                if key == 'sha256':
                    before, value = before[:12], value[:12]
                print(f'function audit: {key} {before} -> {value}')
        return 0

    if arguments.check:
        if not SEAL.is_file():
            print('function audit: missing %s' % SEAL.relative_to(ROOT),
                  file=sys.stderr)
            return 1
        sealed = dict(line.split(None, 1) for line in SEAL.read_text().splitlines()
                      if line and not line.startswith('#'))
        current = sealed_counts()
        wrong = [key for key, value in current.items()
                 if sealed.get(key) != value]
        if wrong:
            for key in wrong:
                print('function audit: %s is %s, sealed %s' %
                      (key, current[key], sealed.get(key, 'missing')),
                      file=sys.stderr)
            return 1
        print('function audit: %d C functions, %d library asm routines/%d '
              'architecture bodies/%d aliases, %d Canvas asm routines/%d '
              'architecture bodies, %d kernel asm bodies; inventory matches seal' %
              (len(definitions), len(library), library_bodies,
               len(library_aliases), len(canvas_names), len(canvas),
               len(kernel)))
    elif arguments.summary:
        counts = Counter(item.path for item in definitions)
        for path in sorted(counts):
            print('%4d  %s' % (counts[path], path))
        print('%4d  TOTAL C FUNCTIONS' % len(definitions))
        print('%4d  MACRO-GENERATED' %
              sum(item.kind == 'generated' for item in definitions))
        print('%4d  C ALIASES' %
              sum(item.kind == 'alias' for item in definitions))
        variants = Counter((item.path, item.name) for item in definitions)
        print('%4d  CONFIGURATION VARIANTS' %
              sum(count - 1 for count in variants.values() if count > 1))
        print('%4d  LIBRARY ASM ROUTINES' % len(library))
        print('%4d  LIBRARY ARCH BODIES' % library_bodies)
        print('%4d  LIBRARY ASM ALIASES' % len(library_aliases))
        print('%4d  CANVAS ASM ROUTINES' % len(canvas_names))
        print('%4d  CANVAS ARCH BODIES' % len(canvas))
        print('%4d  KERNEL ASM BODIES' % len(kernel))
        print('%4d  TOTAL AUDIT SYMBOLS' %
              (len(definitions) + len(library) + len(library_aliases) +
               len(canvas_names) + len(kernel)))
        print('%4d  TOTAL AUDIT IMPLEMENTATIONS' %
              (len(definitions) + library_bodies + len(canvas) + len(kernel)))
        print('      SOURCE SHA256 %s' % source_digest())
    else:
        for item in definitions:
            print('%s\t%d\t%s\t%s' %
                  (item.path, item.line, item.name,
                   item.kind))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
