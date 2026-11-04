#!/usr/bin/env python3
# test/audio_counter.py: a counter stream for the sound server's file sink, and the check that it arrives whole.
#
#   audio_counter.py gen RATE SECONDS FMT IN   FMT is s16 or f32: stereo, frame n (from 1) carries the counter n (s16: left
#                                              is n & 0xffff, right is n >> 16; f32: the sample v / 32768 with v the 16-bit
#                                              counter ((n - 1) % 65535) - 32767, exact through the server's round(x * 32768))
#   audio_counter.py check OUT IN FMT          OUT is the file sink's S16 output. Every frame of the stream's sound is compared
#                                              with the counter: any frame dropped, repeated or silenced inside the stream is a
#                                              discontinuity. The first and last 200 frames of the sound are not counted (the
#                                              fade-in and fade-out scale the counter). Exit 0 when the stream is whole, else 1.
#   audio_counter.py selftest                  a planted drop is found, and a whole stream is not reported (the check's own test)
import array
import sys


def make(rate, seconds, fmt, path):
    frames = int(float(seconds) * int(rate))
    if fmt == 'f32':
        out = array.array('f', [0.0]) * (frames * 2)
        for n in range(1, frames + 1):
            v = ((n - 1) % 65535) - 32767
            out[2 * (n - 1)] = v / 32768.0
            out[2 * (n - 1) + 1] = v / 32768.0
    else:
        out = array.array('h', [0]) * (frames * 2)
        for n in range(1, frames + 1):
            left = n & 0xffff
            right = (n >> 16) & 0xffff
            out[2 * (n - 1)] = left - 65536 if left >= 32768 else left
            out[2 * (n - 1) + 1] = right - 65536 if right >= 32768 else right
    with open(path, 'wb') as f:
        f.write(out.tobytes())
    return frames


def counters(data, fmt):
    """The counter of each output frame (-1 for silence): S16 frames as a 32-bit counter; float frames as the 16-bit one."""
    frames = len(data) // 2
    values = []
    if fmt == 'f32':
        raw = [data[2 * i] for i in range(frames)]
        for i in range(frames):
            silent = raw[i] == 0 and ((i > 0 and raw[i - 1] == 0) or (i + 1 < frames and raw[i + 1] == 0))
            values.append(-1 if silent else raw[i] + 32767)
    else:
        for i in range(frames):
            v = (data[2 * i] & 0xffff) | ((data[2 * i + 1] & 0xffff) << 16)
            values.append(-1 if v == 0 else v)
    return values


def discontinuities(values, fmt):
    """(first, last, gaps): the analysed range of output frames and every place the counter does not go up by one."""
    sound = [i for i, v in enumerate(values) if v >= 0]
    if not sound:
        return None, None, [('no sound', 0, 0, 0, 0)]
    first = sound[0] + 200
    last = sound[-1] - 200
    period = 65535 if fmt == 'f32' else 1 << 32
    gaps = []
    prev = values[first]
    silent = 0
    for i in range(first + 1, last + 1):
        v = values[i]
        if v < 0:
            silent += 1
            continue
        skip = (v - prev - 1) % period
        if fmt == 'f32' and skip > period // 2:
            skip -= period
        if skip != 0 or silent:
            gaps.append((i, prev, v, skip, silent))
        prev = v
        silent = 0
    return first, last, gaps


def check(out_path, in_path, fmt, report=True):
    data = array.array('h')
    data.frombytes(open(out_path, 'rb').read())
    in_frames = len(open(in_path, 'rb').read()) // (4 if fmt == 's16' else 8)
    values = counters(data, fmt)
    first, last, gaps = discontinuities(values, fmt)
    if first is None:
        print("  no sound reached the file")
        return False
    # the stream occupies the frames from its first sound to its last; a whole stream spans its own length (the fades are inside it)
    sound = [i for i, v in enumerate(values) if v >= 0]
    span = sound[-1] - sound[0] + 1
    ok = not gaps and span >= in_frames - 200
    if report:
        print("  %s: the sound spans %d frames of a stream of %d; analysed %d frames; %d discontinuities" % (
            fmt, span, in_frames, last - first + 1, len(gaps)))
        for (i, prev_v, v, skip, silent_before) in gaps[:12]:
            print("    output frame %d: counter %d -> %d, skip %d, silent before %d" % (i, prev_v, v, skip, silent_before))
    return ok


def selftest():
    import os
    import tempfile
    tmp = tempfile.mkdtemp()
    whole = os.path.join(tmp, 'whole.raw')
    frames = make(48000, 2, 'f32', os.path.join(tmp, 'in.raw'))
    # the output a whole stream would make: the same counter as S16 (round(x * 32768))
    data = array.array('h', [0]) * (frames * 2)
    raw = array.array('f')
    raw.frombytes(open(os.path.join(tmp, 'in.raw'), 'rb').read())
    for i in range(frames * 2):
        data[i] = int(round(raw[i] * 32768.0))
    with open(whole, 'wb') as f:
        f.write(data.tobytes())
    ok_whole = check(whole, os.path.join(tmp, 'in.raw'), 'f32', report=False)
    # the same with one frame of 4096 taken out (a drop) and one repeated (a repeat)
    planted = os.path.join(tmp, 'planted.raw')
    cut = array.array('h', list(data[:2 * 20000]) + list(data[2 * 20001:]))
    with open(planted, 'wb') as f:
        f.write(cut.tobytes())
    ok_planted = check(planted, os.path.join(tmp, 'in.raw'), 'f32', report=False)
    print("selftest: a whole stream passes: %s; a stream with a dropped frame fails: %s" % (ok_whole, not ok_planted))
    return ok_whole and not ok_planted


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        make(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
        sys.exit(0)
    if sys.argv[1] == 'check':
        sys.exit(0 if check(sys.argv[2], sys.argv[3], sys.argv[4]) else 1)
    if sys.argv[1] == 'selftest':
        sys.exit(0 if selftest() else 1)
    sys.exit(2)
