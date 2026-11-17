#!/usr/bin/env python3
"""bench-table.py TABLE.tsv: the benchmark's long form (machine gpu path scenario metric value, from bench.sh) as markdown.

    one table per scenario: keys (key to vblank stages, bench-keys.sh), consent (the stale rounds folded: rounds with stale pixels
    out of the rounds run, the most stale pixels in a round, the compositor's and Chromium's CPU ticks per round), debug (moonwater
    debug's counters at the start and the end of the stage), skip (machines not reachable).
"""
import collections
import statistics
import sys

rows = [line.rstrip("\n").split("\t") for line in open(sys.argv[1]) if line.count("\t") == 5]
scen_order = []
table = collections.OrderedDict()  # scenario -> OrderedDict(row key -> OrderedDict(metric -> value))


def put(scen, key, metric, value):
    if scen not in table:
        table[scen] = collections.OrderedDict()
        scen_order.append(scen)
    table[scen].setdefault(key, collections.OrderedDict())[metric] = value


rounds = collections.defaultdict(lambda: collections.defaultdict(list))
for m, g, p, s, metric, v in rows:
    key = (m, g, p)
    if s.startswith("consent-round"):
        rounds[key][metric].append(v)
    elif s.startswith("debug-"):
        put("debug", key, metric + "@" + s[len("debug-"):], v)
    elif s == "skip":
        put("skip", key, metric, v)
    else:
        put(s, key, metric, v)

for key, metrics in rounds.items():
    n = len(metrics.get("stale_px", []))
    px = [int(x) for x in metrics.get("stale_px", []) if x not in ("", None)]
    dirty = sum(1 for x in px if x > 0)
    out = table.setdefault("consent", collections.OrderedDict())
    if "consent" not in scen_order:
        scen_order.append("consent")
    cell = out.setdefault(key, collections.OrderedDict())
    cell["stale_rounds"] = "%d/%d" % (dirty, n)
    cell["stale_px_max"] = str(max(px)) if px else ""
    cell["stale_px_median"] = str(int(statistics.median(px))) if px else ""
    for name in ("compositor_ticks", "chromium_ticks"):
        vals = [int(x) for x in metrics.get(name, []) if x not in ("", None)]
        if vals:
            cell[name + "_mean"] = "%.0f" % statistics.mean(vals)

print("# Benchmark table\n")
print("Rows: machine, gpu (card on/off or the processor), path (card or processor compose), then the scenario's metrics.\n")
for scen in scen_order:
    data = table.get(scen, {})
    if not data:
        continue
    cols = []
    for metrics in data.values():
        for c in metrics:
            if c not in cols:
                cols.append(c)
    print("## %s\n" % scen)
    print("| machine | gpu | path | " + " | ".join(cols) + " |")
    print("|" + "---|" * (3 + len(cols)))
    for (m, g, p), metrics in data.items():
        print("| %s | %s | %s | %s |" % (m, g, p, " | ".join(metrics.get(c, "") for c in cols)))
    print()
