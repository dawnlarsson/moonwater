#!/usr/bin/env python3
"""A game's frames as the compositor saw them (moonwater canvas frames SECONDS: /run/moonwater/record/frames.txt).

    frames.py FRAMES.txt [ROW]

The file is a line an event: "T ROW NS" a window's new picture taken, "P - NS" a present made, "V - NS" the vblank a flip landed on.
For the window with the most pictures (or ROW): its frame times (picture to picture), fps, the 50th, 99th and 99.9th percentile frame
time and the 1% and 0.1% lows (the fps of the slowest 1% and 0.1% of frames, averaged), the longest gap; the pictures replaced before a
present showed them (dropped); the time from a picture to the vblank that showed it; and the refreshes between shown pictures (pacing:
how many vblanks each shown picture stayed). Standard library only.
"""
import sys


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    takes, presents, vblanks = {}, [], []
    for line in open(sys.argv[1]):
        parts = line.split()
        if len(parts) != 3:
            continue
        kind, row, when = parts[0], parts[1], int(parts[2])
        if kind == "T":
            takes.setdefault(int(row), []).append(when)
        elif kind == "P":
            presents.append(when)
        elif kind == "V":
            vblanks.append(when)
    if not takes:
        raise SystemExit("no window drew a picture in the recording")
    row = int(sys.argv[2]) if len(sys.argv) > 2 else max(takes, key=lambda r: len(takes[r]))
    t = takes.get(row, [])
    if len(t) < 3:
        raise SystemExit("window %d drew %d pictures: too few" % (row, len(t)))
    gaps = sorted((b - a) / 1e6 for a, b in zip(t, t[1:]))
    span = (t[-1] - t[0]) / 1e9

    def pct(p):
        return gaps[min(len(gaps) - 1, int(p * len(gaps)))]

    def low(share):
        worst = gaps[-max(1, int(len(gaps) * share)):]
        return 1000.0 / (sum(worst) / len(worst))

    # A picture is shown by the first present after it if no newer picture of the window came first; the vblank after that present shows it.
    shown, dropped, latency = [], 0, []
    p_at = 0
    for i, when in enumerate(t):
        while p_at < len(presents) and presents[p_at] < when:
            p_at += 1
        if p_at >= len(presents):
            break
        if i + 1 < len(t) and t[i + 1] <= presents[p_at]:
            dropped += 1
            continue
        v = next((v for v in vblanks if v >= presents[p_at]), None)
        if v is not None:
            shown.append(v)
            latency.append((v - when) / 1e6)
    period = None
    if len(vblanks) > 2:
        steps = sorted(b - a for a, b in zip(vblanks, vblanks[1:]))
        period = steps[len(steps) // 4]  # the refresh: the shorter quarter of vblank steps (a skipped one is two)
    held = []
    if period:
        for a, b in zip(shown, shown[1:]):
            held.append(round((b - a) / period))
    latency.sort()
    print("window %d: %d pictures in %.2f s, %.1f fps" % (row, len(t), span, (len(t) - 1) / span if span else 0))
    print("frame time ms: p50 %.2f, p99 %.2f, p99.9 %.2f, max %.2f" % (pct(0.5), pct(0.99), pct(0.999), gaps[-1]))
    print("lows: 1%% %.1f fps, 0.1%% %.1f fps" % (low(0.01), low(0.001)))
    print("dropped (replaced before a present showed them): %d of %d" % (dropped, len(t)))
    if latency:
        print("picture to vblank ms: p50 %.2f, p99 %.2f, max %.2f" % (latency[len(latency) // 2], latency[int(0.99 * (len(latency) - 1))], latency[-1]))
    if held:
        counts = {}
        for h in held:
            counts[h] = counts.get(h, 0) + 1
        print("refreshes each shown picture stayed (refresh %.2f ms): %s" % (period / 1e6, ", ".join("%d:%d" % (k, counts[k]) for k in sorted(counts))))
    others = sorted(((len(v), r) for r, v in takes.items() if r != row), reverse=True)[:4]
    if others:
        print("other windows: " + ", ".join("%d (%d pictures)" % (r, n) for n, r in others))


main()
