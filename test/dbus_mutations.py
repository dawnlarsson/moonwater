#!/usr/bin/env python3
"""Mutation proofs for the session bus's engine, src/dbus/dbus.c, against CHECK_dbus.

Each mutation is one edit to a copy of dbus.c (a check taken out, a bound moved, a count changed), built into CHECK_dbus with
gcc and run; the check must turn red.  A mutation that survives is a hole in the check.

    python3 test/dbus_mutations.py [NAME...]          from the tree's root; 8 builds at a time

Last run: see the commit that changed this line.
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
engine = os.path.join(root, 'src/dbus/dbus.c')
checks = os.path.join(root, 'test/checks.c')
work = tempfile.mkdtemp(prefix='dbus-mutations-')
only = sys.argv[1:]

# (name, old text, new text); the old text must stand in dbus.c, and every place it stands is replaced.
M = [
    # ---- what no client library takes is not passed on (2026-11-04) ----
    ("the Local interface passed on", 'if (dbus_equal(m->interface, "org.freedesktop.DBus.Local") || dbus_equal(m->path, "/org/freedesktop/DBus/Local"))',
     'if (dbus_equal(m->path, "/org/freedesktop/DBus/Local"))'),
    ("the Local path passed on", 'if (dbus_equal(m->interface, "org.freedesktop.DBus.Local") || dbus_equal(m->path, "/org/freedesktop/DBus/Local"))',
     'if (dbus_equal(m->interface, "org.freedesktop.DBus.Local"))'),
    ("a reply serial of 0 taken", "else if (code == 5 && (m->reply = dbus_get32(b + at - 4)) != 0)", "else if (code == 5 && ((m->reply = dbus_get32(b + at - 4)) != 0 || 1))"),
    ("a header field of code 0 taken as one not known", "if (code > 9)\n                        continue;", "if (code > 9 || !code)\n                        continue;"),
    ("a dict entry not a level of its own", "&each, depth + 2)", "&each, depth + 1)"),
    ("values nested to 32 only", "#define DBUS_DEPTH 64 ", "#define DBUS_DEPTH 32 "),
    ("values nested past 64", "#define DBUS_DEPTH 64 ", "#define DBUS_DEPTH 66 "),
    ("dict entries counted as structs", "dbus_sig_one(s, n, at, arrays + 1, structs, dicts + 1)", "dbus_sig_one(s, n, at, arrays + 1, structs + 1, dicts + 1)"),
    ("a field the bus does not know refused", "if (code > 9)\n                        continue;", "if (code > 9)\n                        return -1;"),
    ("a message of a type the bus does not know ends its sender", "c->in[at + 1] > DBUS_SIGNAL)", "c->in[at + 1] > 200)"),
    ("an array GDBus reads short passed on", "if (*at != stop || ((first == '(' || first == '{') && counted + 4 == stop - length && elements > 1 && previous >= stop - 4))",
     "if (*at != stop)"),
    ("an array GDBus reads short: one byte too strict", "elements > 1 && previous >= stop - 4))", "elements > 1 && previous >= stop - 5))"),
    # ---- no client can have the bus drop another (2026-11-04) ----
    ("a queue that overflows drops its client", "if (!c->full_since)\n                        c->full_since = dbus.host->now(dbus.host->context) | 1;\n                return 0;",
     "c->dead = 2;\n                return 0;"),
    ("what was not asked for fills the whole queue", "(asked ? DBUS_OUT : DBUS_UNASKED)", "DBUS_OUT"),
    ("the stall never ends a client", "else if (c->full_since && now - c->full_since > DBUS_STALL_MS)", "else if (0)"),
    ("reading does not stop the stall clock", "c->out_head = c->out_length = 0;\n        c->full_since = 0;", "c->out_head = c->out_length = 0;"),
    ("a refused call is not told its caller", "if (n <= 0 || !dbus_deliver(target, dbus.stamped, n, DBUS_UNASKED_FOR))",
     "if (n <= 0 || (dbus_deliver(target, dbus.stamped, n, DBUS_UNASKED_FOR) && 0))"),
    ("the queue's move to the front cut short", "dbus_copy(c->out, c->out + c->out_head, c->out_length - c->out_head);",
     "dbus_copy(c->out, c->out + c->out_head, c->out_length - c->out_head - 1);"),
    ("the queue's head kept where it was", "c->out_head += count;", "c->out_head += count / 2;"),
    # ---- the bus's methods ----
    ("RequestName answers 0 past the cap", "if (!flags)\n                        dbus_error_reply(", "if (0)\n                        dbus_error_reply("),
    ("an owner's second RequestName keeps its flags", "n->replaceable = (flags & 1) != 0;\n                        return 4;", "return 4;"),
    ("ReleaseName of a unique name", "(length = dbus_call_string(m, text, sizeof text)) < 0 || text[0] == ':' ||\n                    !dbus_name_ok(text)",
     "(length = dbus_call_string(m, text, sizeof text)) < 0 ||\n                    !dbus_name_ok(text)"),
    ("ListActivatableNames lists the portal twice", "if (!all)\n                dbus_o_string(&w, DBUS_NAME_PORTAL);",
     "if (!all)\n                dbus_o_string(&w, DBUS_NAME_PORTAL), dbus_o_string(&w, DBUS_NAME_PORTAL);"),
    ("a match rule's text cut at 288 bytes", "char rule_text[DBUS_RULE_TEXT];", "char rule_text[288];"),
    ("argN matches an object path", "(r->arg_mode[k] != 1 && m->signature[k] != 's')", "(0)"),
    ("a unique name with a leading zero", "name[3] < '1' || name[3] > '9'", "name[3] < '0' || name[3] > '9'"),
    ("the trace keeps a client's control bytes", "line[k] = '?';", "(void)k;"),
    # ---- the portal ----
    ("ReadAll's * wants a dot before it", "if (pattern[n - 1] != '*')", "if (n < 2 || pattern[n - 1] != '*' || pattern[n - 2] != '.')"),
    ("ReadAll's prefix one short", "if (k == n - 1)\n                        return 1;", "if (k >= n - 2)\n                        return 1;"),
    ("ReadAll reads eight patterns", "while (at < limit)\n        {\n                long n, k = 0;", "for (int seen = 0; at < limit && seen < 8; seen++)\n        {\n                long n, k = 0;"),
]

source = open(engine).read()


def run(index):
    name, old, new = M[index]
    if only and name not in only:
        return None
    if source.count(old) < 1:
        return (name, "NOT APPLIED", "")
    d = os.path.join(work, "m%d" % index)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d + "/src/dbus")
    os.makedirs(d + "/test")
    os.symlink(checks, d + "/test/checks.c")
    open(d + "/src/dbus/dbus.c", "w").write(source.replace(old, new))
    exe = d + "/check"
    cc = subprocess.run(["gcc", "-std=gnu11", "-O2", "-w", "-D_GNU_SOURCE", "-DCHECK_dbus", d + "/test/checks.c", "-o", exe],
                        capture_output=True, text=True, errors="replace")
    if cc.returncode:
        return (name, "BUILD ERROR", cc.stderr[:200])
    try:
        r = subprocess.run([exe, "20000", "20000"], capture_output=True, text=True, errors="replace", timeout=300)
    except subprocess.TimeoutExpired:
        return (name, "RED (hang)", "")
    last = [l for l in r.stdout.splitlines() if l.startswith("CHECK_dbus:")]
    fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l][:1]
    if r.returncode == 0:
        return (name, "SURVIVED", last[0] if last else "")
    return (name, "RED", (last[0] if last else "crashed (%d)" % r.returncode) + (" | " + fails[0][:110] if fails else ""))


with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    results = [r for r in pool.map(run, range(len(M))) if r]
survived = 0
for name, status, detail in results:
    print("%-48s %-12s %s" % (name, status, detail))
    survived += status in ("SURVIVED", "NOT APPLIED", "BUILD ERROR")
print("mutations: %d, red: %d, survived or unusable: %d" % (len(results), len(results) - survived, survived))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if survived else 0)
