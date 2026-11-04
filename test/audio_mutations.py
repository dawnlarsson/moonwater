#!/usr/bin/env python3
"""Mutation proofs for the audio mixing core, src/audio/mix.c.

Each mutation is one edit to a copy of the engine (a coefficient, an exponent, a bound, a byte order, a constant),
built into CHECK_audio_mix with gcc; the check must turn red.  A mutation that survives is a hole in the check.

    python3 test/audio_mutations.py [NAME...]          from the tree's root; 8 builds at a time, about 4 minutes

Last run: 37 mutations, 37 red.
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
engine = os.path.join(root, 'src/audio/mix.c')
checks = os.path.join(root, 'test/checks.c')
work = tempfile.mkdtemp(prefix='audio-mutations-')
only = sys.argv[1:]

M = [
    ("volume curve squared", "return x * x * x;", "return x * x;"),
    ("ramp 48 frames", "#define AUDIO_RAMP 96", "#define AUDIO_RAMP 48"),
    ("ring read off by one", "unsigned long from = voice->ring.read % voice->ring.size;", "unsigned long from = (voice->ring.read + voice->frame) % voice->ring.size;"),
    ("output channels swapped", "c == 0 ? left[i] : c == 1 ? right[i] : 0.0f", "c == 0 ? right[i] : c == 1 ? left[i] : 0.0f"),
    ("LFE mixed in", "case AUDIO_POSITION_LFE:\n                        break;", "case AUDIO_POSITION_LFE:\n                        side = 2;\n                        break;"),
    ("table rows not normalised", "row[k] = (float)((double)row[k] / sum);", "row[k] = row[k];"),
    ("clips not counted", "sink->clips++;", ""),
    ("24-bit scale wrong", "d[i] = (float)x * (1.0f / 8388608.0f);\n                        }\n                        break;\n                case AUDIO_FORMAT_S24BE:", "d[i] = (float)x * (1.0f / 4194304.0f);\n                        }\n                        break;\n                case AUDIO_FORMAT_S24BE:"),
    ("no fade when data runs out", "held = played < AUDIO_RAMP ? played : AUDIO_RAMP;", "held = 0;"),
    ("master gain ignored", "&now, sink->master_target, audio_slope(master, sink->master_target)", "&now, 1.0f, audio_slope(master, 1.0f)"),
    ("peak without abs", "a = y < 0 ? -y : y;", "a = y;"),
    ("u8 silence wrong", "return format == AUDIO_FORMAT_U8 ? 0x80", "return format == AUDIO_FORMAT_U8 ? 0x7f"),
    ("mono only left", "matrix[0][0] = matrix[1][0] = 1.0f;\n                return 0;", "matrix[0][0] = 1.0f;\n                return 0;"),
    ("A-law bias", "t += 0x108;", "t += 0x100;"),
    ("rate change always keeps the table", "(rate > was ? rate - was : was - rate) * 20 <= was", "(rate > was ? rate - was : was - rate) * 20 <= was * 100"),
    ("seek one frame too far", "target - (long long)ring->read > (long long)ring->size)\n                return -1;\n        if ((unsigned long)target > ring->end)", "target - (long long)ring->read >= (long long)ring->size)\n                return -1;\n        if ((unsigned long)target > ring->end)"),
    ("ramp twice as fast", "(most > 1.0f ? most : 1.0f) * (1.0f / (float)AUDIO_RAMP)", "(most > 1.0f ? most : 1.0f) * (2.0f / (float)AUDIO_RAMP)"),
    ("16-bit clip one too high", "v = v > 32767.0f ? 32767.0f : v;", "v = v > 32768.0f ? 32768.0f : v;"),
    ("table mirror not turned", "table->h[p * taps + k] = table->h[(phases - p) * taps + taps - 1 - k];", "table->h[p * taps + k] = table->h[(phases - p) * taps + k];"),
    ("drain tail too short", "long zeros = rs->table->taps / 2;", "long zeros = rs->table->taps / 4;"),
    ("u8 offset", "(float)((int)p[i * stride] - 128)", "(float)((int)p[i * stride] - 127)"),
    ("remix rows not scaled", "matrix[o][c] /= sum;", "matrix[o][c] *= 1.0f;"),
    ("kaiser beta low", "double cutoff, beta = 9.84, base;", "double cutoff, beta = 6.0, base;"),
    ("phases cut to an eighth", "phases = AUDIO_PHASES / m;", "phases = AUDIO_PHASES / m / 8;"),
    ("no phase interpolation", "coefficient[k] = a[k] + f * (b[k] - a[k]);", "coefficient[k] = a[k];"),
    ("filter centre one off", "long lead = rs->table->taps / 2 - 1;", "long lead = rs->table->taps / 2;"),
    ("volume not clamped", "if (volume > AUDIO_VOLUME_MAX)\n                volume = AUDIO_VOLUME_MAX;", ""),
    ("program's NaN let through", "return x == x ? x : 0.0f;", "return x;"),
    ("S16BE byte order", "(short)((p[i * stride] << 8) | p[i * stride + 1])", "(short)((p[i * stride + 1] << 8) | p[i * stride])"),
    ("A-law sign", "audio_law[0][byte] = (float)((a & 0x80) ? t : -t)", "audio_law[0][byte] = (float)((a & 0x80) ? -t : t)"),
    ("mu-law bias", "t = ((u & 0x0f) << 3) + 0x84;", "t = ((u & 0x0f) << 3) + 0x80;"),
    ("sink rounds down", "r = v + 12582912.0f;", "r = v + 12582912.0f - 0.5f;"),
    ("resampler step truncated", "rs->step = (unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout);", "rs->step = (unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout) - 5000;"),
    ("table taps not scaled for downsampling", "taps = AUDIO_TAPS * m;\n        phases", "taps = AUDIO_TAPS;\n        phases"),
    ("cutoff not lowered for downsampling", "cutoff = 0.5 * (rin > rout ? (double)rout / (double)rin : 1.0);", "cutoff = 0.5;"),
    ("silent voice still reads", "if (voice->now[0] == 0 && voice->now[1] == 0 && voice->target[0] == 0 && voice->target[1] == 0)\n                return 0;", ""),
    ("ring put ignores room", "unsigned long room = audio_ring_room(ring), from, first;\n\n        if (n > room)\n                n = room;\n        n -= n % ring->frame;\n        from = ring->write % ring->size;\n        first = ring->size - from;\n        if (first > n)\n                first = n;\n        __builtin_memcpy", "unsigned long room = audio_ring_room(ring), from, first;\n\n        n -= n % ring->frame;\n        from = ring->write % ring->size;\n        first = ring->size - from;\n        if (first > n)\n                first = n;\n        __builtin_memcpy"),
]

src = open(engine).read()


def run(index):
    name, old, new = M[index]
    if only and name not in only:
        return None
    if src.count(old) < 1:
        return (name, "NOT APPLIED", "")
    d = os.path.join(work, "m%d" % index)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d + "/src/audio")
    os.makedirs(d + "/test")
    os.symlink(checks, d + "/test/checks.c")
    open(d + "/src/audio/mix.c", "w").write(src.replace(old, new) if name.startswith("peak") else src.replace(old, new, 1))
    exe = d + "/check"
    cc = subprocess.run(["gcc", "-std=gnu11", "-O2", "-w", "-D_GNU_SOURCE", "-DCHECK_audio_mix", d + "/test/checks.c", "-o", exe, "-lm"],
                        capture_output=True, text=True)
    if cc.returncode:
        return (name, "BUILD ERROR", cc.stderr[:200])
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return (name, "RED (hang)", "")
    last = [l for l in r.stdout.splitlines() if l.startswith("CHECK_audio_mix")]
    fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l][:1]
    if r.returncode == 0:
        return (name, "SURVIVED", last[0] if last else "")
    return (name, "RED", (last[0] if last else "crashed (%d)" % r.returncode) + (" | " + fails[0][:110] if fails else ""))


with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    results = [r for r in pool.map(run, range(len(M))) if r]
survived = 0
for name, status, detail in results:
    print("%-42s %-12s %s" % (name, status, detail))
    survived += status in ("SURVIVED", "NOT APPLIED", "BUILD ERROR")
print("mutations: %d, red: %d, survived or unusable: %d" % (len(results), len(results) - survived, survived))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if survived else 0)
