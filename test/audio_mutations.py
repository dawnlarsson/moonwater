#!/usr/bin/env python3
"""Mutation proofs for the audio engines, src/audio/mix.c (the mixing core), src/audio/alsa.c (the sound card layer) and src/audio/pulse.c (the protocol).

Each mutation is one edit to a copy of one of them (a coefficient, an exponent, a bound, a flag, a byte order, a constant),
built into CHECK_audio_mix, CHECK_audio_alsa or CHECK_audio_pulse with gcc; the check must turn red.  A mutation that survives is a hole
in the check.

    python3 test/audio_mutations.py [NAME...]          from the tree's root; 8 builds at a time, about 4 minutes

Last run: see the commit that changed this line.
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
engines = {'mix': os.path.join(root, 'src/audio/mix.c'), 'alsa': os.path.join(root, 'src/audio/alsa.c'), 'pulse': os.path.join(root, 'src/audio/pulse.c'),
           'control': os.path.join(root, 'src/audio/control.c')}
checks = os.path.join(root, 'test/checks.c')
work = tempfile.mkdtemp(prefix='audio-mutations-')
only = sys.argv[1:]

M = [
    # ---- mix.c ----
    ("mix", "volume curve squared", "return x * x * x;", "return x * x;"),
    ("mix", "ramp 48 frames", "#define AUDIO_RAMP 96", "#define AUDIO_RAMP 48"),
    ("mix", "ring read off by one", "unsigned long from = voice->ring.read % voice->ring.size;", "unsigned long from = (voice->ring.read + voice->frame) % voice->ring.size;"),
    ("mix", "output channels swapped", "c == 0 ? left[i] : c == 1 ? right[i] : 0.0f", "c == 0 ? right[i] : c == 1 ? left[i] : 0.0f"),
    ("mix", "LFE mixed in", "case AUDIO_POSITION_LFE:\n                        break;", "case AUDIO_POSITION_LFE:\n                        side = 2;\n                        break;"),
    ("mix", "table rows not normalised", "row[k] = (float)((double)row[k] / sum);", "row[k] = row[k];"),
    ("mix", "clips not counted", "sink->clips += f32_pair_clip(sink->mix[0], sink->mix[1], (unsigned long)frames);", "(void)f32_pair_clip(sink->mix[0], sink->mix[1], (unsigned long)frames);"),
    ("mix", "24-bit scale wrong", "d[i] = (float)x * (1.0f / 8388608.0f);\n                        }\n                        break;\n                case AUDIO_FORMAT_S24BE:", "d[i] = (float)x * (1.0f / 4194304.0f);\n                        }\n                        break;\n                case AUDIO_FORMAT_S24BE:"),
    ("mix", "no fade when data runs out", "held = played < AUDIO_RAMP ? played : AUDIO_RAMP;", "held = 0;"),
    ("mix", "master gain ignored", "&now, sink->master_target, audio_slope(master, sink->master_target)", "&now, 1.0f, audio_slope(master, 1.0f)"),
    ("mix", "peak without abs", "a = y < 0 ? -y : y;", "a = y;"),
    ("mix", "u8 silence wrong", "return format == AUDIO_FORMAT_U8 ? 0x80", "return format == AUDIO_FORMAT_U8 ? 0x7f"),
    ("mix", "mono only left", "matrix[0][0] = matrix[1][0] = 1.0f;\n                return 0;", "matrix[0][0] = 1.0f;\n                return 0;"),
    ("mix", "A-law bias", "t += 0x108;", "t += 0x100;"),
    ("mix", "rate change always keeps the table", "(rate > was ? rate - was : was - rate) * 20 <= was", "(rate > was ? rate - was : was - rate) * 20 <= was * 100"),
    ("mix", "seek one frame too far", "target - (long long)ring->read > (long long)ring->size)\n                return -1;\n        if ((unsigned long)target > ring->end)", "target - (long long)ring->read >= (long long)ring->size)\n                return -1;\n        if ((unsigned long)target > ring->end)"),
    ("mix", "ramp twice as fast", "(most > 1.0f ? most : 1.0f) * (1.0f / (float)AUDIO_RAMP)", "(most > 1.0f ? most : 1.0f) * (2.0f / (float)AUDIO_RAMP)"),
    ("mix", "16-bit clip one too high", "v = v > 32767.0f ? 32767.0f : v;", "v = v > 32768.0f ? 32768.0f : v;"),
    ("mix", "table mirror not turned", "table->h[p * taps + k] = table->h[(phases - p) * taps + taps - 1 - k];", "table->h[p * taps + k] = table->h[(phases - p) * taps + k];"),
    ("mix", "drain tail too short", "long zeros = rs->table->taps / 2;", "long zeros = rs->table->taps / 4;"),
    ("mix", "u8 offset", "(float)((int)p[i * stride] - 128)", "(float)((int)p[i * stride] - 127)"),
    ("mix", "remix rows not scaled", "matrix[o][c] /= sum;", "matrix[o][c] *= 1.0f;"),
    ("mix", "kaiser beta low", "double cutoff, beta = 9.84, base;", "double cutoff, beta = 6.0, base;"),
    ("mix", "phases cut to an eighth", "phases = AUDIO_PHASES / m;", "phases = AUDIO_PHASES / m / 8;"),
    # (the resampler's row between two phases is lib.c's now: its mutations are test/audiolib_mutations.py's)
    ("mix", "filter centre one off", "long lead = rs->table->taps / 2 - 1;", "long lead = rs->table->taps / 2;"),
    ("mix", "volume not clamped", "if (volume > AUDIO_VOLUME_MAX)\n                volume = AUDIO_VOLUME_MAX;", ""),
    ("mix", "program's NaN let through", "return x == x ? x : 0.0f;", "return x;"),
    ("mix", "S16BE byte order", "(short)((p[i * stride] << 8) | p[i * stride + 1])", "(short)((p[i * stride + 1] << 8) | p[i * stride])"),
    ("mix", "A-law sign", "audio_law[0][byte] = (float)((a & 0x80) ? t : -t)", "audio_law[0][byte] = (float)((a & 0x80) ? -t : t)"),
    ("mix", "mu-law bias", "t = ((u & 0x0f) << 3) + 0x84;", "t = ((u & 0x0f) << 3) + 0x80;"),
    ("mix", "sink rounds down", "r = v + 12582912.0f;", "r = v + 12582912.0f - 0.5f;"),
    ("mix", "resampler step truncated", "rs->step = (unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout);", "rs->step = (unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout) - 5000;"),
    ("mix", "table taps not scaled for downsampling", "taps = AUDIO_TAPS * m;\n        phases", "taps = AUDIO_TAPS;\n        phases"),
    ("mix", "cutoff not lowered for downsampling", "cutoff = 0.5 * (rin > rout ? (double)rout / (double)rin : 1.0);", "cutoff = 0.5;"),
    ("mix", "silent voice still reads", "if (voice->now[0] == 0 && voice->now[1] == 0 && voice->target[0] == 0 && voice->target[1] == 0)\n                return 0;", ""),
    ("mix", "ring put ignores room", "unsigned long room = audio_ring_room(ring), from, first;\n\n        if (n > room)\n                n = room;\n        n -= n % ring->frame;\n        from = ring->write % ring->size;\n        first = ring->size - from;\n        if (first > n)\n                first = n;\n        __builtin_memcpy", "unsigned long room = audio_ring_room(ring), from, first;\n\n        n -= n % ring->frame;\n        from = ring->write % ring->size;\n        first = ring->size - from;\n        if (first > n)\n                first = n;\n        __builtin_memcpy"),
    # ---- alsa.c ----
    ("alsa", "sync_ptr without the read flags", "sync.flags = AUDIO_ALSA_SYNC_HWSYNC | AUDIO_ALSA_SYNC_APPL | AUDIO_ALSA_SYNC_AVAIL_MIN;", "sync.flags = AUDIO_ALSA_SYNC_HWSYNC;"),
    ("alsa", "room counted from the wrong side", "(long)sync.s.status.hw_ptr + pcm->buffer - (long)sync.c.control.appl_ptr", "(long)sync.s.status.hw_ptr - pcm->buffer - (long)sync.c.control.appl_ptr"),
    ("alsa", "no wrap of the room", "if (avail < 0)\n                avail += pcm->boundary;", "if (avail < 0)\n                avail += 0;"),
    ("alsa", "16 bits before 32", "audio_alsa_formats[] = {{AUDIO_ALSA_FORMAT_S32_LE, 4, AUDIO_FORMAT_S32LE},", "audio_alsa_formats[] = {{AUDIO_ALSA_FORMAT_S16_LE, 2, AUDIO_FORMAT_S16LE}, {AUDIO_ALSA_FORMAT_S32_LE, 4, AUDIO_FORMAT_S32LE},"),
    ("alsa", "the most channels, not a pair", "channels = low <= 2 && high >= 2 ? 2 : (long)low;", "channels = (long)high;"),
    ("alsa", "no rounding of the period to the card's", "nearest = (long)audio_alsa_interval(&wide, AUDIO_ALSA_PERIOD_SIZE)->min;", "nearest = period;"),
    ("alsa", "target past the buffer", "pcm->target = pcm->buffer < 3 * pcm->period ? pcm->buffer : 3 * pcm->period;", "pcm->target = 3 * pcm->period;"),
    ("alsa", "wake at every period played, not the target's", "sw.avail_min = (unsigned long)(pcm->buffer - pcm->target + pcm->period);", "sw.avail_min = (unsigned long)pcm->period;"),
    ("alsa", "underruns not counted", "else if (error == AUDIO_EPIPE)\n                pcm->xruns++;", "else if (error == AUDIO_EPIPE)\n                pcm->xruns += 0;"),
    ("alsa", "a card that is gone not marked", "if (result == AUDIO_ENODEV)\n                        pcm->broken = 1;\n                return result;", "return result;"),
    ("alsa", "a driver's name shown as it is", "to[at] = from[at] >= 32 && from[at] < 127 ? (char)from[at] : '?';", "to[at] = (char)from[at];"),
    ("alsa", "a name read past its array", "for (; at < room - 1 && from[at]; at++)", "for (; from[at] && at < room + 40; at++)"),
    ("alsa", "control file left open", "                audio_sys_close((int)fd);\n                found++;", "                found++;"),
    ("alsa", "44100 tried before 48000", "{48000, 44100, 96000, 192000,", "{44100, 48000, 96000, 192000,"),
    ("alsa", "period not scaled to the rate", "period = audio_alsa_clamp(period_at_48000 * got_rate / 48000, 16, 1 << 16);", "period = audio_alsa_clamp(period_at_48000, 16, 1 << 16);"),
    ("alsa", "an underrun not prepared again", "        result = audio_sys_ioctl(pcm->fd, AUDIO_ALSA_PREPARE, 0);\n        if (result == AUDIO_ENODEV)", "        result = 0;\n        if (result == AUDIO_ENODEV)"),
    ("alsa", "bytes returned as frames", "return result < 0 ? result : result / pcm->frame_bytes;\n}\n\nAUDIO_API long audio_pcm_read", "return result;\n}\n\nAUDIO_API long audio_pcm_read"),
    ("alsa", "PCM_INFO number", "AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'U', 0x31,", "AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'U', 0x32,"),
    ("alsa", "access other than read/write", "audio_alsa_only(&hw, AUDIO_ALSA_ACCESS, AUDIO_ALSA_ACCESS_RW_INTERLEAVED);", "audio_alsa_only(&hw, AUDIO_ALSA_ACCESS, 0);"),
    # ---- pulse.c ----
    ("pulse", "shared memory offered", "pulse_w_u32(&w, PULSE_VERSION); // no shared memory, no memfd: neither flag", "pulse_w_u32(&w, PULSE_VERSION | 0x80000000u);"),
    ("pulse", "commands served before the handshake", "if (!c->authorized && command != PULSE_COMMAND_AUTH && command != PULSE_COMMAND_SET_CLIENT_NAME)", "if (0 && command != PULSE_COMMAND_AUTH)"),
    ("pulse", "any program sets the sink volume", "if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (n != 2)", "if (n != 2)"),
    ("pulse", "any program sets any stream's volume", "if (!pulse_may(c, s->uid))\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (n != s->map_channels)", "if (n != s->map_channels)"),
    ("pulse", "no cap on a user's clients", "if (uid != 0 && used_by_user >= PULSE_PER_USER)", "if (0)"),
    ("pulse", "no cap on a client's streams", "if (count >= PULSE_STREAMS_PER_CLIENT || !s)", "if (!s)"),
    ("pulse", "no cap on the rings", "if (p->ring_total + ring > PULSE_RING_TOTAL)", "if (0)"),
    ("pulse", "shared memory flags accepted", "if (flags & 0xff000000u)", "if (0)"),
    ("pulse", "data off the frame accepted", "if (length % (long)s->voice.frame)\n                return PULSE_DEAD;", ""),
    ("pulse", "frame length not capped", "if (channel == PULSE_INVALID ? length > PULSE_FRAME_COMMAND : length > PULSE_FRAME_DATA)", "if (0)"),
    ("pulse", "a full queue written past", "if (pulse_out_fill(c) + 20 + length > PULSE_OUT)\n                return PULSE_DEAD;", ""),
    ("pulse", "base volume with the wrong tag", "pulse_w_volume(w, AUDIO_VOLUME_NORM);              // base volume", "pulse_w_u32(w, AUDIO_VOLUME_NORM);"),
    ("pulse", "sink volume not a mean", "return audio_volume_gain(n ? sum / (unsigned long)n : 0);", "return audio_volume_gain(sum);"),
    ("pulse", "mute does not mute", "if (s->muted || s->corked || s->dying)\n                left = right = 0.0f;", "if (s->corked || s->dying)\n                left = right = 0.0f;"),
    ("pulse", "a muted stream stands still", "s->voice.flows = !s->corked;", "s->voice.flows = 0;"),
    ("pulse", "events to everyone", "if (!c || !c->authorized || !(c->subscribe & (1ul << facility)))", "if (!c || !c->authorized)"),
    ("pulse", "the program's own process id", "pulse_props_set(&c->props, \"application.process.id\", (const unsigned char *)pid, n + 1);", ""),
    ("pulse", "names shown as they are", "if (c >= 0x20 && c < 0x7f)\n                                        to[n++] = (char)c;", "if (c >= 0x20)\n                                        to[n++] = (char)c;"),
    ("pulse", "requests of any size", "if (!missing || (missing < s->minreq && (queued + s->outstanding) != 0))", "if (!missing)"),
    ("pulse", "start target ignored", "if (fill > 0 && (fill >= s->prebuf || s->trigger))", "if (fill > 0)"),
    ("pulse", "no underflow said", "else if (!s->flushing)\n                        pulse_stream_signal(p, s, PULSE_COMMAND_UNDERFLOW, 1);", ""),
    ("pulse", "drain answered at once", "#define PULSE_DRAIN_USEC 20000l", "#define PULSE_DRAIN_USEC 0l"),
    ("pulse", "a stream cut when its client goes", "if (s->voice.now[0] == 0 && s->voice.now[1] == 0)\n                pulse_stream_free(p, s);\n}", "pulse_stream_free(p, s);\n}"),
    ("pulse", "strings of any length", "#define PULSE_STRING 1024 ", "#define PULSE_STRING 100000 "),
    ("pulse", "no cap on a property list", "if (props->count >= PULSE_PROPS)\n                return 0;", ""),
    ("pulse", "no time to say who", "now - c->born > PULSE_AUTH_USEC", "now - c->born > (1l << 40)"),
    ("pulse", "the remembered volume not used", "if (p->host.recall && app[0] && p->host.recall(p->host.context, app, &remembered, &mute_remembered))", "if (0)"),
    ("pulse", "a change not remembered", "if (owner && pulse_props_text(&owner->props, \"application.name\", app, sizeof(app)))\n                                p->host.remember(p->host.context, app, volume[0], s->muted);", ""),
    ("pulse", "every program sees every stream", "if (p->stream[i].used && !p->stream[i].dying && (list || p->stream[i].index == index) && pulse_may(c, p->stream[i].uid))", "if (p->stream[i].used && !p->stream[i].dying && (list || p->stream[i].index == index))"),
    ("pulse", "every program sees every client", "&& (c->root || p->client[i]->uid == c->uid))", ")"),
    ("pulse", "a request not counted", "if (pulse_enqueue(c, PULSE_INVALID, w.p, w.len) == PULSE_DONE)\n                s->outstanding += missing;", "pulse_enqueue(c, PULSE_INVALID, w.p, w.len);"),
    ("pulse", "kill open to all", "if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (command == PULSE_COMMAND_KILL_SINK_INPUT)", "if (command == PULSE_COMMAND_KILL_SINK_INPUT)"),
    ("pulse", "the default sink open to all", "if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (name)\n                {", "if (name)\n                {"),
    ("pulse", "a flush that cuts at once", "s->voice.ring.write = s->voice.ring.limit;\n                                s->flushing = 1;", "s->voice.ring.limit = s->voice.ring.write = s->voice.ring.read;\n                                s->flushing = 1;"),
    ("pulse", "cork not applied", "s->corked = cork;\n                if (cork)\n                        s->underrun = 1;\n                pulse_stream_apply(s);", "s->corked = cork;"),
    # ---- control.c (the mixer's client, checked in CHECK_audio_pulse against the engine) ----
    ("control", "a volume on the first channel only", "for (long i = 0; i < channels; i++)\n                all[i] = volume;", "for (long i = 0; i < 1; i++)\n                all[i] = volume;"),
    ("control", "a mute turned over", "pulse_w_bool(&w, muted != 0);\n        if (w.bad)\n                return CTL_FAILED;\n        got = ctl_exchange(c, w.len);\n        return got < 0 ? got : 0;\n}\n\n// A stream ended", "pulse_w_bool(&w, muted == 0);\n        if (w.bad)\n                return CTL_FAILED;\n        got = ctl_exchange(c, w.len);\n        return got < 0 ? got : 0;\n}\n\n// A stream ended"),
    ("control", "the channel count not kept", "        c->kept[c->kept_next].index = index;\n        c->kept[c->kept_next].channels = channels;\n        c->kept_next = (c->kept_next + 1) % CTL_CACHE;", "        c->kept_next = (c->kept_next + 1) % CTL_CACHE;"),
    ("control", "a wrong count not mended", "                if (got != CTL_INVALID)\n                        return got < 0 ? got : 0;", "                return got < 0 ? got : 0;"),
    ("control", "an access error as a failure", "return code == PULSE_ERR_ACCESS ? CTL_ACCESS :", "return code == PULSE_ERR_ACCESS ? CTL_FAILED :"),
    ("control", "a missing stream as a failure", "code == PULSE_ERR_NOENTITY ? CTL_NOENTITY", "code == PULSE_ERR_NOENTITY ? CTL_FAILED"),
    ("control", "any tag taken for ours", "if (frame[20] != 'L' || frame[25] != 'L' || answered != tag)", "if (frame[20] != 'L' || frame[25] != 'L')"),
    ("control", "a lost answer not a failure", "        c->open = 0;\n        return CTL_FAILED;\n}\n\n// A request begun", "        return CTL_FAILED;\n}\n\n// A request begun"),
    ("control", "the sink named by nothing", "pulse_w_u32(&w, PULSE_INVALID);\n        pulse_w_string(&w, sink);\n        pulse_w_cvolume(&w, both, 2);", "pulse_w_u32(&w, PULSE_INVALID);\n        pulse_w_string(&w, 0);\n        pulse_w_cvolume(&w, both, 2);"),
    ("control", "a stream's mute misread", "into->muted = pulse_r_bool(&r);", "into->muted = 0, (void)pulse_r_bool(&r);"),
    ("control", "the stream killed is another", "ctl_begin(c, &w, PULSE_COMMAND_KILL_SINK_INPUT);\n        pulse_w_u32(&w, index);", "ctl_begin(c, &w, PULSE_COMMAND_KILL_SINK_INPUT);\n        pulse_w_u32(&w, index + 1);"),
]

sources = {k: open(v).read() for k, v in engines.items()}


def run(index):
    which, name, old, new = M[index]
    if only and name not in only and which not in only:
        return None
    src = sources[which]
    if src.count(old) < 1:
        return (name, "NOT APPLIED", "")
    d = os.path.join(work, "m%d" % index)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d + "/src/audio")
    os.makedirs(d + "/test")
    os.symlink(checks, d + "/test/checks.c")
    for k, v in engines.items():
        shutil.copy(v, d + "/src/audio/" + k + ".c")
    open(d + "/src/audio/" + which + ".c", "w").write(src.replace(old, new) if name.startswith("peak") else src.replace(old, new, 1))
    exe = d + "/check"
    cc = subprocess.run(["gcc", "-std=gnu11", "-O2", "-w", "-D_GNU_SOURCE", "-DCHECK_audio_" + ("pulse" if which == "control" else which), d + "/test/checks.c", "-o", exe, "-lm"],
                        capture_output=True, text=True, errors="replace")
    if cc.returncode:
        return (name, "BUILD ERROR", cc.stderr[:200])
    try:
        r = subprocess.run([exe], capture_output=True, text=True, errors="replace", timeout=300)
    except subprocess.TimeoutExpired:
        return (name, "RED (hang)", "")
    last = [l for l in r.stdout.splitlines() if l.startswith("CHECK_audio_")]
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
