#!/usr/bin/env python3
"""
        State that is set and never taken back, listed and held to a seal.

        A lifecycle kept as a flag has two ends, and the bugs this tree has
        had from them were all of one shape: the bit is set where the thing
        begins and nothing clears it where the thing ends (a window asked to
        close once and never again, an X window's row kept after its
        mapping, a flip pending for good). So the scan is for exactly that:

          bit   a NAME in capitals that is ORed into something (`|= NAME`,
                alone or among others) somewhere under src/ and is never
                taken out bit by bit (`&= ~NAME`, `&= ~(... NAME ...)`)
                anywhere under src/. Only what is ORed into a member or an
                element counts: a local word of options is built and gone.
          field a member with a lifecycle's name (started, done, closed,
                sent, held...) that is assigned true through `.` or `->` in
                a file and is never assigned false or 0 in that file.

        Most of what it finds is right as it is: a property of a thing that
        lives as long as the thing, a request bit of a word that is built
        fresh each time, a latch that is meant to hold until the process
        ends. Those are listed in test/one_way_state.seal with the reason,
        one line each. The check fails on anything found that the seal does
        not list (a new flag is either given its other end or said to be
        terminal, with why) and on anything the seal lists that is no longer
        found (the list only says what is true).

            python3 test/one_way_state.py            the findings, with counts
            python3 test/one_way_state.py --check    against the seal
            python3 test/one_way_state.py --seal     write the seal, keeping
                                                     the reasons it has
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SEAL = os.path.join(HERE, "one_way_state.seal")
UNSAID = "NOT LOOKED AT"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')
NAME = re.compile(r"\b[A-Z][A-Z0-9]*_[A-Z0-9_]+\b")
# A name that is ORed in, and not one a condition tests, compares or counts from
# (`|= (flags & NAME) != 0`, `|= kind == NAME`, `|= 1u << (code - NAME)`).
ORED = re.compile(r"(?<![&=!<>+*/-])(?<![&=!<>+*/-] )(?<![&=!<>+*/-]  )\b([A-Z][A-Z0-9]*_[A-Z0-9_]+)\b(?!\s*[=!<>]=)")
SET = re.compile(r"(?:(?:->|\.)\s*[a-z_][a-z0-9_]*|\])\s*\|=([^;{}]*);")
CLEAR = re.compile(r"&=\s*~([^;{}]*);")
LIFE = re.compile(r"(start|ready|done|finish|clos|gone|lost|dead|pending|open|sent|asked|told|ack|linked|held|taken|"
                  r"leaving|ended|exit|answered|dialed|refused|leased|dirty|busy|active|running|waiting|alive|"
                  r"shown|mapped|bound|joined|connected|armed|locked)")
FIELD = re.compile(r"(?:->|\.)\s*([a-z_][a-z0-9_]*)\s*=\s*(true|false|1|0)\s*[;,)]")


def sources():
    for base, _, files in os.walk(os.path.join(ROOT, "src")):
        for name in sorted(files):
            if name.endswith((".c", ".h", ".inc")):
                yield os.path.join(base, name)


def text_of(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    text = COMMENT.sub(lambda m: " " * len(m.group(0)) if "\n" not in m.group(0)
                       else re.sub(r"[^\n]", " ", m.group(0)), text)
    return STRING.sub('""', text)


def scan():
    sets, clears = {}, set()
    fields = {}
    for path in sources():
        text = text_of(path)
        where = os.path.relpath(path, ROOT)
        for match in SET.finditer(text):
            for name in ORED.findall(match.group(1)):
                sets.setdefault(name, set()).add(where)
        for match in CLEAR.finditer(text):
            clears.update(NAME.findall(match.group(1)))
        on, off = set(), set()
        for match in FIELD.finditer(text):
            (on if match.group(2) in ("true", "1") else off).add(match.group(1))
        # `x->flag = 1` beside `x->count = 1`: only a member this file also
        # writes true or false to is taken for a boolean when the value is 1.
        truthy = {m.group(1) for m in FIELD.finditer(text) if m.group(2) in ("true", "false")}
        for name in sorted((on - off) & truthy):
            if LIFE.search(name):
                fields["%s:%s" % (where, name)] = True
    found = {}
    for name in sorted(sets):
        if name not in clears:
            found["bit " + name] = ", ".join(sorted(sets[name])[:2])
    for key in sorted(fields):
        found["field " + key] = ""
    return found, len(sets)


def seal_read():
    said = {}
    if not os.path.exists(SEAL):
        return said
    with open(SEAL, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            key, _, reason = line.partition("\t")
            said[key] = reason
    return said


def main(argv):
    found, flags = scan()
    bits = sum(1 for key in found if key.startswith("bit "))
    if "--seal" in argv:
        said = seal_read()
        with open(SEAL, "w", encoding="utf-8") as handle:
            handle.write("# State that is set and never taken back, and why each is right as it is: test/one_way_state.py.\n"
                         "# One line each, the finding, a tab, the reason. A finding with no reason here fails the check.\n")
            for key in sorted(found):
                handle.write("%s\t%s\n" % (key, said.get(key) or UNSAID))
        print("one-way state: %d bits of %d set and %d fields sealed" % (bits, flags, len(found) - bits))
        return 0
    if "--check" in argv:
        said = seal_read()
        bad = 0
        for key in sorted(found):
            if key not in said:
                print("one-way state: %s is set and never taken back (%s): give it its other end, or list it in "
                      "test/one_way_state.seal with why it has none" % (key, found[key] or "its file"))
                bad += 1
            elif not said[key] or said[key] == UNSAID:
                print("one-way state: %s is listed with no reason" % key)
                bad += 1
        for key in sorted(said):
            if key not in found:
                print("one-way state: %s is listed and is no longer found: take it out of the seal "
                      "(python3 test/one_way_state.py --seal)" % key)
                bad += 1
        if bad:
            return 1
        print("one-way state: %d bits of %d set and %d fields, as sealed" % (bits, flags, len(found) - bits))
        return 0
    for key in sorted(found):
        print("%s\t%s" % (key, found[key]))
    print("one-way state: %d bits of %d set and %d fields" % (bits, flags, len(found) - bits))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
