#!/usr/bin/env python3
"""A game's frames as the compositor saw them (moonwater canvas frames SECONDS: /run/moonwater/record/frames.txt).

    frames.py FRAMES.txt [ROW]

The file is a line an event: "T ROW NS" a window's new picture taken, "B - NS" a present begun, "P - NS" a present made, "F - NS" the
card's last job of a present done, "C ROW NS" a window's frame callbacks sent, "V - NS" the vblank a flip landed on, "W - NS" a wake or
the screens taken again (src/canvas/record.h). For the window with the most pictures (or ROW): its frame times (picture to picture), fps,
the 50th, 99th and 99.9th percentile frame time and the 1% and 0.1% lows (the fps of the slowest 1% and 0.1% of frames, averaged), the
longest gap; the pictures replaced before a present showed them (dropped); the time from a picture to the vblank that showed it; the
refreshes between shown pictures (pacing: how many vblanks each shown picture stayed); the time from its frame callback to its next
picture. For every present: its time from begun to made (B to P), and for the card's, from begun to its job done (B to F). For every
wake: the window's longest frame time in the two seconds after it. Standard library only.
"""
import bisect
import sys


def spread(values):
    """p50, p99 and the most of a list of milliseconds, as text (empty for none)."""
    if not values:
        return ""
    v = sorted(values)
    return "p50 %.2f, p99 %.2f, max %.2f (%d)" % (v[len(v) // 2], v[int(0.99 * (len(v) - 1))], v[-1], len(v))


def after(begins, ends):
    """Each end paired with the latest begin at or before it (and after the end before it): the spans in milliseconds."""
    spans, used = [], -1
    for end in ends:
        at = bisect.bisect_right(begins, end) - 1
        if at >= 0 and at > used:
            spans.append((end - begins[at]) / 1e6)
            used = at
    return spans


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    takes, presents, vblanks, begins, fences, callbacks, wakes = {}, [], [], [], [], {}, []
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
        elif kind == "B":
            begins.append(when)
        elif kind == "F":
            fences.append(when)
        elif kind == "C":
            callbacks.setdefault(int(row), []).append(when)
        elif kind == "W":
            wakes.append(when)
    # The ring is in the order the events were noted: a present's B is noted at its end, an F before its P.
    for marks in [presents, vblanks, begins, fences, wakes] + list(takes.values()) + list(callbacks.values()):
        marks.sort()
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
    if row in callbacks:
        print("frame callback to its next picture ms: " + spread(after(callbacks[row], t)))
    if begins:
        print("present begun to made ms (the loop's, or to the card's jobs seen done): " + spread(after(begins, presents)))
    if fences:
        print("present begun to the card's job done ms: " + spread(after(begins, fences)))
    for wake in wakes:
        near = [(b - a) / 1e6 for a, b in zip(t, t[1:]) if wake <= b <= wake + 2000000000]
        print("wake at +%.3f s: the window's longest frame time in the 2 s after it %.2f ms" % ((wake - t[0]) / 1e9, max(near) if near else 0.0))
    others = sorted(((len(v), r) for r, v in takes.items() if r != row), reverse=True)[:4]
    if others:
        print("other windows: " + ", ".join("%d (%d pictures)" % (r, n) for n, r in others))


main()
