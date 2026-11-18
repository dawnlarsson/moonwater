#!/usr/bin/env python3
"""Rover's schema, generated from the code that says it.

    python3 -I test/rover_schema.py            write src/rover/schema.inc
    python3 -I test/rover_schema.py --check    say whether it is current; 1 when it is stale
    python3 -I test/rover_schema.py --print    the schema's records, as `moonwater rover schema` prints them

The inputs (src/rover/README.md says what each part of the schema is):
  - src/rover/schema.list: the states, the userspace events and the primitives, by hand;
  - src/moonwater/spark.c: the kernel's events, spark_bind_event_name in id order, so a kernel event's rover id is
    spark's id (canvas on and canvas off are canvas.c's two names);
  - src/lib.c: the routines its inventory lists on all three machines whose prototype is PURE or CONST and takes and
    answers values only. kernel=yes when the prototype and, on every machine, a body stand outside #ifndef KERNEL_MODE;
  - src/sh/tools.inc: the shell's built-in tools.

The output is C: the type, state, event, primitive, function and tool rows, one typed call for each function, the
schema's records as one string, and their FNV-1a hash, which a table carries and the loader compares.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIST = ROOT / 'src/rover/schema.list'
SPARK = ROOT / 'src/moonwater/spark.c'
CANVAS = ROOT / 'src/canvas/canvas.c'
LIB = ROOT / 'src/lib.c'
TOOLS = ROOT / 'src/sh/tools.inc'
OUT = ROOT / 'src/rover/schema.inc'
DEBUG = False   # --why: say why each routine left out is left out

PAGE_HEAD = 16      # a page's first bytes: its version, its sequence, the zone's offset
PAGE_BYTES = 4096
WORD_SLOT = 64      # a word state: its length, 62 bytes and a NUL
KINDS = ['int', 'percent', 'duration', 'clock', 'weekday', 'enum', 'onoff', 'chord', 'word', 'text']
PAGES = ['kernel', 'desktop']
SOURCES = ['kernel', 'runner', 'desktop']
CLASSES = ['any', 'lower-only', 'deliberate']
KEY_RENAMES = {'poweroff': 'key power', 'sleep': 'key sleep', 'reset': 'key reset',
               'ctrl_alt_delete': 'key ctrl-alt-delete'}

# The C spelling of an integer argument, as the routine's own prototype names it, and the cast that hands it over.
INTEGERS = {'positive', 'bipolar', 'p8', 'b8', 'p16', 'b16', 'p32', 'b32', 'p64', 'b64', 'bool', 'int',
            'unsigned int', 'unsigned', 'long', 'unsigned long', 'char', 'unsigned char'}
INTEGER_RANGE = {'p8': (0, 255), 'b8': (-128, 127), 'p16': (0, 65535), 'b16': (-32768, 32767),
                 'p32': (0, 0xffffffff), 'b32': (-(1 << 31), (1 << 31) - 1), 'bool': (0, 1), 'int': (-(1 << 31), (1 << 31) - 1),
                 'unsigned int': (0, 0xffffffff), 'unsigned': (0, 0xffffffff), 'char': (-128, 127),
                 'unsigned char': (0, 255), 'positive': (0, (1 << 63) - 1), 'p64': (0, (1 << 63) - 1)}
WORDS = {'const_string', 'string_address', 'const p8 address_to', 'const char address_to', 'const char *'}
ANSWERS = INTEGERS
ATTRIBUTES = re.compile(r'\b(PURE|CONST|HOT|COLD|INLINE|static|inline|extern|'
                        r'READS\s*\([^)]*\)|WRITES\s*\([^)]*\)|__attribute__\s*\(\([^)]*\)\))\s*')


def fail(message):
    sys.exit('rover_schema: ' + message)


def split_words(line):
    words, at = [], 0
    while at < len(line):
        if line[at] in ' \t':
            at += 1
        elif line[at] == '"':
            end = line.index('"', at + 1)
            words.append(line[at + 1:end])
            at = end + 1
        else:
            end = at
            while end < len(line) and line[end] not in ' \t':
                end += 1
            words.append(line[at:end])
            at = end
    return words


class Types:
    """Every distinct type once; states, fields and arguments name one by its number."""

    def __init__(self):
        self.rows, self.index = [], {}
        #   The base types, first and in this order: the compiler names them by row (ROVER_TYPE_INT .. _TEXT).
        for base in ('int', 'percent', 'duration', 'clock', 'weekday', 'onoff', 'chord', 'word', 'text'):
            self.take(base)

    def take(self, spelling):
        parts = spelling.split(':')
        kind = parts[0]
        if kind not in KINDS:
            fail('unknown type ' + spelling)
        low, high, choices = 0, 0, ''
        if kind == 'int':
            low, high = (int(parts[1]), int(parts[2])) if len(parts) == 3 else (-(1 << 62), 1 << 62)
        elif kind == 'enum':
            choices = parts[1]
        elif kind == 'onoff':
            choices = 'off,on'
        elif kind == 'weekday':
            choices = 'mon,tue,wed,thu,fri,sat,sun'
        elif len(parts) != 1:
            fail('a ' + kind + ' takes no bounds: ' + spelling)
        key = (kind, low, high, choices)
        if key not in self.index:
            self.index[key] = len(self.rows)
            self.rows.append(key)
        return self.index[key]

    def spell(self, number):
        kind, low, high, choices = self.rows[number]
        if kind == 'enum':
            return 'enum:' + choices
        if kind == 'int' and (low, high) != (-(1 << 62), 1 << 62):
            return 'int:%d:%d' % (low, high)
        return kind


def read_list(types):
    states, events, primitives, ranges = [], [], [], {}
    offsets = {page: PAGE_HEAD for page in PAGES}
    for number, line in enumerate(LIST.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        words = split_words(line)
        if words[0] == 'state':
            name, page, spelling = words[1:4]
            if page not in PAGES:
                fail('line %d: no page %s' % (number, page))
            kind = types.take(spelling)
            size = WORD_SLOT if types.rows[kind][0] == 'word' else 4
            states.append((name, PAGES.index(page), kind, offsets[page]))
            offsets[page] += size
            if offsets[page] > PAGE_BYTES:
                fail('page %s is full' % page)
        elif words[0] == 'event':
            fields = [(f.split(':', 1)[0], types.take(f.split(':', 1)[1])) for f in words[4:]]
            events.append((int(words[1]), words[3], SOURCES.index(words[2]), fields))
        elif words[0] == 'primitive':
            args = [(a.split(':', 1)[0], types.take(a.split(':', 1)[1])) for a in words[5:]]
            primitives.append((int(words[1]), words[4], SOURCES.index(words[2]), CLASSES.index(words[3]), args))
        elif words[0] == 'function':
            stated = {}
            for word in words[2:]:
                if word == 'total':
                    continue
                param, low, high = word.split(':')
                stated[param] = (int(low, 0), int(high, 0))
            ranges[words[1]] = stated
        else:
            fail('line %d: no record %s' % (number, words[0]))
    return states, events, primitives, ranges


def kernel_events():
    text = SPARK.read_text()
    body = re.search(r'spark_bind_event_name\[SPARK_BIND_EVENTS\]\[SPARK_BIND_NAME_MAX\]\s*=\s*\{(.*?)\};', text, re.S)
    if not body:
        fail('spark_bind_event_name is not where it was')
    canvas = dict(re.findall(r'#define (SPARK_BIND_CANVAS_O\w+_NAME) "([^"]*)"', CANVAS.read_text()))
    names = []
    for item in re.findall(r'"[^"]*"|SPARK_BIND_\w+', body.group(1)):
        names.append(item.strip('"') if item.startswith('"') else canvas[item])
    count = int(re.search(r'#define SPARK_BIND_EVENTS (\d+)u', text).group(1))
    if len(names) != count:
        fail('spark has %d event names for %d events' % (len(names), count))
    return [(at + 1, KEY_RENAMES.get(name, name.replace('_', ' ')), 0, []) for at, name in enumerate(names)]


def strip_comments(text):
    out, at, n = [], 0, len(text)
    while at < n:
        if text.startswith('//', at):
            end = text.find('\n', at)
            at = n if end < 0 else end
        elif text.startswith('/*', at):
            end = text.find('*/', at + 2)
            out.append('\n' * text.count('\n', at, end))
            at = end + 2
        elif text[at] == '"':
            end = at + 1
            while text[end] != '"':
                end += 2 if text[end] == '\\' else 1
            out.append(text[at:end + 1])
            at = end + 1
        else:
            out.append(text[at])
            at += 1
    return ''.join(out)


def condition_kind(expression):
    """What a conditional's branch says of the kernel and the machine: (kernel excluded, user excluded, arch)."""
    e = expression.strip()
    arch = {'X64': 'x86_64', 'ARM64': 'arm64', 'RISCV64': 'riscv64'}.get(e)
    if arch:
        return False, False, arch, False
    if 'KERNEL_MODE' in e or 'STANDARD_MODERN_C_KERNEL' in e:
        negated = re.search(r'!\s*defined\s*\(?\s*(KERNEL_MODE|STANDARD_MODERN_C_KERNEL)', e)
        return bool(negated), not negated, None, False
    #   Builds the shell and the module never are: the library with no platform layer, and Windows.
    if re.fullmatch(r'!\s*defined\s*\(\s*(STANDARD_NO_PLATFORM|WINDOWS)\s*\)', e):
        return False, False, None, False
    return False, False, None, True


def lib_functions(ranges):
    """lib.c's routines on all three machines, with their prototypes and where a kernel build has them."""
    raw = LIB.read_text()
    inventory = re.search(r'routine\s+scope\s+x86_64\s+arm64\s+riscv64\n\s*-[- ]+\n(.*?)\n\s*\n', raw, re.S)
    if not inventory:
        fail("lib.c's inventory is not where it was")
    everywhere = set()
    for row in inventory.group(1).splitlines():
        parts = row.split()
        if len(parts) == 5 and parts[1] == 'public' and parts[2:] == ['yes', 'yes', 'yes']:
            everywhere.add(parts[0])

    text = strip_comments(raw)
    stack = []          # one frame a conditional: [kernel excluded, user excluded, arch, other]
    bodies = {}         # name -> set of machines with a body a kernel build compiles
    prototypes = {}     # name -> (text, kernel, plain)
    pending = None
    lines = text.splitlines()
    for number, line in enumerate(lines):
        stripped = line.strip()
        directive = re.match(r'#\s*(ifndef|ifdef|if|elif|else|endif)\b(.*)', stripped)
        if directive:
            word, rest = directive.group(1), directive.group(2)
            following = next((l.strip() for l in lines[number + 1:] if l.strip()), '')
            if word == 'ifndef' and re.match(r'#\s*define\s+' + re.escape(rest.strip()) + r'\b', following):
                stack.append([False, False, None, False])   # an include guard
            elif word == 'ifndef':
                stack.append(list(condition_kind('!defined(' + rest.strip() + ')')))
            elif word == 'ifdef':
                stack.append(list(condition_kind('defined(' + rest.strip() + ')')))
            elif word == 'if':
                stack.append(list(condition_kind(rest)))
            elif word == 'elif':
                stack[-1] = list(condition_kind(rest))
            elif word == 'else':
                kernel_out, user_out, arch, other = stack[-1]
                stack[-1] = [user_out, kernel_out, None, other or arch is not None]
            else:
                stack.pop()
            continue
        kernel_out = any(frame[0] for frame in stack)
        user_out = any(frame[1] for frame in stack)
        arch = next((frame[2] for frame in reversed(stack) if frame[2]), None)
        other = any(frame[3] or frame[2] for frame in stack)
        for name in re.findall(r'ASM_FUNC\((\w+)\)', line):
            if name in everywhere and arch and not kernel_out:
                bodies.setdefault(name, set()).add(arch)
        if pending is not None:
            pending[0] += ' ' + stripped
        elif re.match(r'(PURE|CONST)\b', stripped):
            pending = [stripped, not kernel_out, not user_out and not other]
        if pending is not None and ';' in pending[0]:
            match = re.match(r'(.*?)\b(\w+)\s*\(([^()]*)\)\s*;', ATTRIBUTES.sub('', pending[0]))
            if match and match.group(2) in everywhere and match.group(2) not in prototypes:
                prototypes[match.group(2)] = (match.group(1).strip(), match.group(3).strip(), pending[1], pending[2])
            pending = None

    functions = []
    for name in sorted(prototypes):
        answer, params, kernel, plain = prototypes[name]
        signature, casts = value_signature(params)
        why = ('inside a conditional' if not plain else 'answers ' + answer if answer not in ANSWERS else
               'takes ' + params if signature is None else 'takes nothing' if not signature else None)
        #   An integer's domain is what the code does not say: a selector past its cases, a byte past 255. A routine
        #   with one is bound once schema.list says its range, or that every value is one it takes.
        if not why and 'i' in signature:
            if name not in ranges:
                why = 'integer arguments with no function row in schema.list'
            else:
                for kind, ctype, param in casts:
                    if kind == 'i':
                        low, high = ranges[name].get(param, (None, None))
                        if low is None:
                            low, high = INTEGER_RANGE.get(ctype, (-(1 << 63), (1 << 63) - 1))
                        casts[casts.index((kind, ctype, param))] = (kind, ctype, param, low, high)
        if why:
            if DEBUG:
                print('rover_schema: not %s: %s' % (name, why), file=sys.stderr)
            continue
        kernel = kernel and bodies.get(name, set()) == {'x86_64', 'arm64', 'riscv64'}
        functions.append((name, answer, signature, casts, kernel))
    for name in ranges:
        if name not in [f[0] for f in functions]:
            fail('schema.list has a function row for %s, which is no routine rover binds' % name)
    return functions


def value_signature(params):
    """The routine's arguments as rover's: w a word, b a byte buffer (its bytes and length), i an integer."""
    if params in ('', 'void'):
        return '', []
    parts = [re.sub(r'\s+', ' ', p.strip()) for p in params.split(',')]
    signature, casts, at = '', [], 0
    while at < len(parts):
        part = parts[at]
        ctype = re.sub(r'\s*\b\w+$', '', part) if ' ' in part or 'address_to' in part else part
        param = part[len(ctype):].strip()
        #   A pointer is a buffer only with its length beside it, named as one: string_table_find's table and stride
        #   are an address and a number, and reading them as a word and its length would hand the routine a count of
        #   entries past the word's end.
        #   A word with a length after it is a buffer too (byte_class_index's name is a run inside a pattern): read as
        #   a word and a number, a length past the word's end would be read past it.
        if (ctype in ('address_any', 'const address_any') or ctype in WORDS) and at + 1 < len(parts) and \
                re.match(r'positive (size|length)$', parts[at + 1]):
            signature += 'b'
            casts.append(('b', ctype, param))
            at += 2
            continue
        if ctype in WORDS:
            signature += 'w'
            casts.append(('w', ctype, param))
        elif ctype in INTEGERS:
            signature += 'i'
            casts.append(('i', ctype, param))
        else:
            return None, None
        at += 1
    if len(signature) > 4:
        return None, None
    return signature, casts


def tools():
    names = []
    for line in TOOLS.read_text().splitlines():
        match = re.match(r'SHELL_TOOL\((\w+),\s*([^,]+),', line.strip())
        if match and match.group(1) != 'SYSTEM':
            names.append(match.group(2).strip())
    return names


def quote(name):
    return '"' + name + '"' if ' ' in name else name


def records(types, states, events, primitives, functions, tool_names):
    lines = []
    for ident, name, source, fields in events:
        line = 'event id=%d name=%s source=%s' % (ident, quote(name), SOURCES[source])
        if fields:
            line += ' fields=' + ','.join('%s:%s' % (f, types.spell(t)) for f, t in fields)
        lines.append(line)
    for name, page, kind, offset in states:
        lines.append('state name=%s page=%s offset=%d type=%s' % (name, PAGES[page], offset, types.spell(kind)))
    lines.append('state name=clock page=clock offset=0 type=clock')
    lines.append('state name=weekday page=clock offset=0 type=weekday')
    for ident, name, runs, klass, args in primitives:
        line = 'primitive id=%d name=%s runs=%s class=%s bounded=yes' % (ident, quote(name), SOURCES[runs],
                                                                        CLASSES[klass])
        if args:
            line += ' args=' + ','.join('%s:%s' % (a, types.spell(t)) for a, t in args)
        lines.append(line)
    for number, (name, answer, signature, casts, kernel) in enumerate(functions):
        stated = ''.join(' %s=%d:%d' % (c[2], c[3], c[4]) for c in casts if c[0] == 'i')
        lines.append('function id=%d name=%s args=%s returns=int kernel=%s bounded=yes%s' %
                     (number, name, signature, 'yes' if kernel else 'no', stated))
    for number, name in enumerate(tool_names):
        lines.append('tool id=%d name=%s runs=runner bounded=no' % (number, name))
    return ''.join(line + '\n' for line in lines)


def fnv(text):
    value = 0x811c9dc5
    for byte in text.encode():
        value = ((value ^ byte) * 0x01000193) & 0xffffffff
    return value


def c_number(value):
    #   The one 64-bit number C cannot write as a literal is the lowest; every other is a decimal with its suffix.
    return '(-0x7fffffffffffffffll - 1)' if value == -(1 << 63) else '%dll' % value


def c_string(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def generate():
    types = Types()
    states, user_events, primitives, ranges = read_list(types)
    events = sorted(kernel_events() + user_events)
    seen = set()
    for ident, name, _, _ in events:
        if ident in seen or not 0 < ident < 128:
            fail('event id %d is taken or out of range' % ident)
        seen.add(ident)
    functions = lib_functions(ranges)
    tool_names = tools()
    text = records(types, states, events, primitives, functions, tool_names)

    out = ['/*',
           '        Generated by test/rover_schema.py from src/rover/schema.list, spark\'s event table, lib.c\'s',
           '        inventory and the shell\'s tools -- do not edit by hand; edit those and run it again.',
           '',
           '        %d types, %d states, %d events, %d primitives, %d functions (%d of them in the kernel), %d tools.' %
           (len(types.rows), len(states), len(events), len(primitives), len(functions),
            sum(1 for f in functions if f[4]), len(tool_names)),
           '*/',
           '',
           '#define ROVER_SCHEMA_HASH 0x%08xu' % fnv(text),
           '#define ROVER_TYPES %d' % len(types.rows),
           '#define ROVER_STATES %d' % len(states),
           '#define ROVER_EVENT_ROWS %d' % len(events),
           '#define ROVER_PRIMITIVES %d' % len(primitives),
           '#define ROVER_FUNCTIONS %d' % len(functions),
           '#define ROVER_TOOLS %d' % len(tool_names),
           '']
    out.append('static const struct rover_type_row rover_types[ROVER_TYPES] = {')
    for kind, low, high, choices in types.rows:
        count = len(choices.split(',')) if choices else 0
        out.append('        {ROVER_T_%s, %d, %dll, %dll, %s},' % (kind.upper(), count, low, high, c_string(choices)))
    out.append('};')
    out.append('')
    out.append('static const struct rover_state_row rover_states[ROVER_STATES] = {')
    for name, page, kind, offset in states:
        out.append('        {%s, %d, %d, %d},' % (c_string(name), page, kind, offset))
    out.append('};')
    out.append('')
    out.append('static const struct rover_event_row rover_event_rows[ROVER_EVENT_ROWS] = {')
    for ident, name, source, fields in events:
        names = ', '.join(c_string(f) for f, _ in fields) or '0'
        kinds = ', '.join(str(t) for _, t in fields) or '0'
        out.append('        {%s, %d, %d, %d, {%s}, {%s}},' % (c_string(name), ident, source, len(fields), kinds, names))
    out.append('};')
    out.append('')
    out.append('static const struct rover_primitive_row rover_primitives[ROVER_PRIMITIVES] = {')
    for ident, name, runs, klass, args in primitives:
        names = ', '.join(c_string(a) for a, _ in args) or '0'
        kinds = ', '.join(str(t) for _, t in args) or '0'
        out.append('        {%s, %d, %d, %d, %d, {%s}, {%s}},' % (c_string(name), ident, runs, klass, len(args), kinds,
                                                                names))
    out.append('};')
    out.append('')
    out.append('//      One call a function: the values of a comparison handed to the routine as its prototype says.')
    for name, answer, signature, casts, kernel in functions:
        args, slot = [], 0
        for kind, ctype, *_ in casts:
            if kind == 'b':
                args.append('(%s)value[%d].text' % ('address_any' if 'address_any' in ctype else
                                                     ctype.replace('address_to', '*'), slot))
                args.append('(positive)value[%d].length' % slot)
            elif kind == 'w':
                args.append('(%s)value[%d].text' % (ctype.replace('address_to', '*'), slot))
            else:
                args.append('(%s)value[%d].number' % (ctype, slot))
            slot += 1
        if not kernel:
            out.append('#ifndef KERNEL_MODE')
        out.append('static b64 rover_call_%s(const struct rover_value address_to value)' % name)
        out.append('{')
        out.append('        return (b64)%s(%s);' % (name, ', '.join(args)))
        out.append('}')
        if not kernel:
            out.append('#endif')
    out.append('')
    out.append('static const struct rover_function_row rover_functions[ROVER_FUNCTIONS] = {')
    for name, answer, signature, casts, kernel in functions:
        call = 'rover_call_' + name if kernel else 'ROVER_USER_CALL(rover_call_%s)' % name
        lows = ', '.join(c_number(c[3]) if c[0] == 'i' else '0' for c in casts)
        highs = ', '.join(c_number(c[4]) if c[0] == 'i' else '0' for c in casts)
        out.append('        {%s, %s, %d, %d, %s, {%s}, {%s}},' % (c_string(name), c_string(signature), len(signature),
                                                              1 if kernel else 0, call, lows, highs))
    out.append('};')
    out.append('')
    out.append('static const char address_to const rover_tools[ROVER_TOOLS] = {')
    for at in range(0, len(tool_names), 8):
        out.append('        ' + ' '.join(c_string(t) + ',' for t in tool_names[at:at + 8]))
    out.append('};')
    out.append('')
    out.append('//      The records `moonwater rover schema` prints, whose FNV-1a is ROVER_SCHEMA_HASH.')
    out.append('static const char rover_schema_text[] =')
    for line in text.splitlines():
        out.append('        ' + c_string(line + '\\n').replace('\\\\n"', '\\n"'))
    out.append('        ;')
    return '\n'.join(out) + '\n', text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument("--print", action="store_true")
    parser.add_argument("--why", action="store_true")
    options = parser.parse_args()
    global DEBUG
    DEBUG = options.why
    made, text = generate()
    if options.print:
        sys.stdout.write(text)
        return 0
    if options.check:
        current = OUT.read_text() if OUT.exists() else ''
        if current != made:
            print('rover_schema: src/rover/schema.inc is stale; run python3 -I test/rover_schema.py')
            return 1
        print('rover_schema: src/rover/schema.inc is current')
        return 0
    OUT.write_text(made)
    print('rover_schema: wrote src/rover/schema.inc')
    return 0


if __name__ == '__main__':
    sys.exit(main())
