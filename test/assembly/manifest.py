#!/usr/bin/env python3
"""What the two coverage manifests share.

test/performance_coverage.py and test/specialization_coverage.py are both a
classification of the same generated assembly inventory: every routine gets
exactly one row, a row's evidence is a file that has to exist, and the claim
is anchored to a token that has to be present in that file. That machinery
was written twice, once per manifest, by authors who could not see each
other's copy. It lives here once; each manifest keeps only its own
categories, rows and per-category rules.
"""

import argparse
import importlib.util
import pathlib
import re
import sys
from collections import Counter


ROOT = pathlib.Path(__file__).resolve().parents[2]
LIBRARY = ROOT / 'src/lib.c'
INVENTORY_TOOL = pathlib.Path(__file__).resolve().parent / 'inventory.py'


def load_inventory():
    """The generated assembly inventory, as the set of routine names."""
    spec = importlib.util.spec_from_file_location(
        'moonwater_inventory_for_coverage', INVENTORY_TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError('could not load %s' % INVENTORY_TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    _, _, _, sources = module.included_audit(str(LIBRARY), 'linux')
    _, order, _, _, _, _, _ = module.graph_assembly_inventory(sources)
    return set(order)


def token_present(text, name):
    return re.search(r'(?<![A-Za-z0-9_])' + re.escape(name) +
                     r'(?![A-Za-z0-9_])', text) is not None


def reconcile(rows, errors):
    """One row per routine, and the manifest equal to the inventory.

    A new, removed or renamed routine fails here until somebody classifies
    it, which is the coupling both manifests promise in their own words.
    Returns the inventory for the checks only one of them adds on top.
    """
    inventory = load_inventory()
    counts = Counter(row.routine for row in rows)
    duplicates = sorted(name for name, count in counts.items() if count != 1)
    manifested = set(counts)

    if duplicates:
        errors.append('duplicate manifest rows: ' + ', '.join(duplicates))
    if inventory - manifested:
        errors.append('unclassified inventory routines: ' +
                      ', '.join(sorted(inventory - manifested)))
    if manifested - inventory:
        errors.append('stale manifest routines: ' +
                      ', '.join(sorted(manifested - inventory)))
    return inventory


def anchor(row, cache, errors):
    """The row's evidence exists and carries the named token.

    Evidence is a file, or one section of test/checks.c spelled
    test/checks.c#CHECK_<name>: the text between #ifdef CHECK_<name> and its
    #endif is what has to carry the token, so a token another section
    happens to use does not vouch for this one.

    Says whether the evidence was readable at all, so a caller can skip its
    own follow-up checks against a file that is not there.
    """
    file, _, section = row.evidence.partition('#')
    evidence_path = ROOT / file
    if not evidence_path.is_file():
        errors.append('%s: missing evidence file %s' %
                      (row.routine, file))
        return False
    if row.evidence not in cache:
        text = evidence_path.read_text(encoding='utf-8', errors='replace')
        if section:
            # A section may share its body with its twins: #if defined(A) ||
            # defined(B) ... #endif /* A || B */. It still opens and closes
            # on its own name.
            begin = text.find('#ifdef %s\n' % section)
            end = text.find('#endif /* %s */\n' % section)
            if begin < 0:
                opened = re.search(r'^#if[^\n]*defined\(%s\)' % re.escape(section), text, re.M)
                begin = opened.start() if opened else -1
            if end < 0:
                closed = re.search(r'^#endif /\* (?:\w+ \|\| )*%s(?: \|\| \w+)* \*/\n' % re.escape(section), text, re.M)
                end = closed.start() if closed else -1
            if begin < 0 or end < begin:
                errors.append('%s: no section %s in %s' %
                              (row.routine, section, file))
                return False
            text = text[begin:end]
        cache[row.evidence] = text
    if not token_present(cache[row.evidence], row.anchor):
        errors.append('%s: anchor %s absent from %s' %
                      (row.routine, row.anchor, row.evidence))
    return True


def run(label, description, gaps_help, validate, print_report):
    """The whole command line rail: arguments, the error rail, the modes."""
    parser = argparse.ArgumentParser(description=description)
    output = parser.add_mutually_exclusive_group()
    output.add_argument('--gaps', action='store_true', help=gaps_help)
    output.add_argument('--all', action='store_true',
                        help='list every manifest row')
    arguments = parser.parse_args()

    try:
        errors = validate()
    except Exception as error:  # keep CI diagnostics short and actionable
        sys.stderr.write('%s: %s\n' % (label, error))
        return 1
    if errors:
        for error in errors:
            sys.stderr.write('%s: %s\n' % (label, error))
        return 1

    print_report('all' if arguments.all
                 else ('gaps' if arguments.gaps else 'summary'))
    return 0
