#!/usr/bin/env python3
"""Mutation proofs for the audio engines, src/audio/mix.c (the mixing core), src/audio/alsa.c (the sound card layer) and src/audio/pulse.c (the protocol).

Each mutation is one edit to a copy of one of them (a coefficient, an exponent, a bound, a flag, a byte order, a constant),
built into CHECK_audio_mix, CHECK_audio_alsa or CHECK_audio_pulse with gcc; the check must turn red.  A mutation that survives is a hole
in the check.  The "bowl" ones are edits to a copy of src/bowl.c (the PULSE_SERVER a bowl's program is offered), run against the
bowl_session harness of test/differential.py, which must exit with an error.

    python3 test/audio_mutations.py [NAME...]          from the tree's root; 8 builds at a time, about 4 minutes

Last run: see the commit that changed this line.
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
engines = {'mix': os.path.join(root, 'src/audio/mix.c'), 'alsa': os.path.join(root, 'src/audio/alsa.c'), 'pulse': os.path.join(root, 'src/audio/pulse.c'),
           'control': os.path.join(root, 'src/audio/control.c'), 'socket': os.path.join(root, 'src/audio/control.c'), 'bowl': os.path.join(root, 'src/bowl.c')}
# 'socket' is control.c's own socket transport, built into CHECK_audio_control (which takes the system's socket, not the engine's).
target_file = {'socket': 'control'}
check_of = {'control': 'pulse', 'socket': 'control'}
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
    ("mix", "seek one frame too far", "> (long long)ring->size)\n                return -1;\n        if ((unsigned long)target > ring->limit)", ">= (long long)ring->size)\n                return -1;\n        if ((unsigned long)target > ring->limit)"),
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
    ("mix", "resampler step truncated", "(unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout);", "(unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout) - 5000;"),
    ("mix", "table taps not scaled for downsampling", "taps = AUDIO_TAPS * m;\n        phases", "taps = AUDIO_TAPS;\n        phases"),
    ("mix", "cutoff not lowered for downsampling", "cutoff = 0.5 * (rin > rout ? (double)rout / (double)rin : 1.0);", "cutoff = 0.5;"),
    ("mix", "silent voice still reads", "if (!voice->flows && voice->now[0] == 0 && voice->now[1] == 0 && voice->target[0] == 0 && voice->target[1] == 0)\n                return 0;", ""),
    ("mix", "voice gain NaN let through", "voice->target[0] = audio_gain_clean(left);\n        voice->target[1] = audio_gain_clean(right);", "voice->target[0] = left;\n        voice->target[1] = right;"),
    ("mix", "master gain NaN let through", "sink->master_target = audio_gain_clean(gain);", "sink->master_target = gain;"),
    ("mix", "gain past 200% let through", "g > AUDIO_GAIN_MAX ? AUDIO_GAIN_MAX : g;", "g;"),
    ("mix", "encoder makes any format", "|| channels < 1 || !(format == AUDIO_FORMAT_S16LE", "|| channels < 1 || (0 && format == AUDIO_FORMAT_S16LE"),
    ("mix", "ring put ignores room", "unsigned long room = audio_ring_room(ring), from, first;\n\n        if (n > room)\n                n = room;\n        n -= n % ring->frame;\n        from = ring->write % ring->size;\n        first = ring->size - from;\n        if (first > n)\n                first = n;\n        __builtin_memcpy", "unsigned long room = audio_ring_room(ring), from, first;\n\n        n -= n % ring->frame;\n        from = ring->write % ring->size;\n        first = ring->size - from;\n        if (first > n)\n                first = n;\n        __builtin_memcpy"),
    ("mix", "an exact step rounded down", "(((unsigned long long)table->num << 32) + table->den - 1) / table->den)", "(((unsigned long long)table->num << 32)) / table->den)"),
    ("mix", "an exact fraction rounded down", "(unsigned long)(((unsigned long long)rs->k << 32) + table->den - 1) / table->den;", "(unsigned long)(((unsigned long long)rs->k << 32)) / table->den;"),
    ("mix", "an exact place not carried", "rs->k = advance % table->den;", "rs->k = 0;"),
    ("mix", "no exact tables", "if (den <= AUDIO_EXACT_MOST && den * (unsigned long)taps <= AUDIO_TABLE_FLOATS)", "if (0)"),
    ("mix", "exact tables of any size", "if (den <= AUDIO_EXACT_MOST && den * (unsigned long)taps <= AUDIO_TABLE_FLOATS)", "if (den <= AUDIO_EXACT_MOST)"),
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
    ("alsa", 'a switch written off, not on', '                        want = 1;\n                }\n                else if (kind == 2 || kind == 4)', '                        want = 0;\n                }\n                else if (kind == 2 || kind == 4)'),
    ("alsa", 'a gain taken at its top, not at 0 dB', '                        want = audio_mixer_zero_db((int)fd, &info);\n                        if (want < 0)', '                        want = -1;\n                        if (want < 0)'),
    ("alsa", '0 dB one step high', '        return min + (0 - low) / step;\n}', '        return min + (0 - low) / step + 1;\n}'),
    ("alsa", '0 dB one step low', '        return min + (0 - low) / step;\n}', '        return min + (0 - low) / step - 1;\n}'),
    ("alsa", 'a range wholly over 0 dB not at its bottom', '        if (low >= 0)\n                return min;', '        if (low >= 0)\n                return -1;'),
    ("alsa", 'a scale not read as a scale', '        if (tlv.tlv[0] == AUDIO_CTL_TLVT_DB_SCALE)\n        {', '        if (0)\n        {'),
    ("alsa", 'a line between two ends not read', '        else if (tlv.tlv[0] == AUDIO_CTL_TLVT_DB_MINMAX || tlv.tlv[0] == AUDIO_CTL_TLVT_DB_MINMAX_MUTE)', '        else if (tlv.tlv[0] == AUDIO_CTL_TLVT_DB_MINMAX)'),
    ("alsa", "a scale's step taken whole", 'step = (long)(tlv.tlv[3] & 0xffff);', 'step = (long)tlv.tlv[3];'),
    ("alsa", 'a control that is read-only written to', '!(info.access & AUDIO_CTL_ACCESS_WRITE) || (info.access & AUDIO_CTL_ACCESS_INACTIVE))', '(info.access & AUDIO_CTL_ACCESS_INACTIVE))'),
    ("alsa", 'a control that is inactive written to', '!(info.access & AUDIO_CTL_ACCESS_WRITE) || (info.access & AUDIO_CTL_ACCESS_INACTIVE))', '!(info.access & AUDIO_CTL_ACCESS_WRITE))'),
    ("alsa", 'a control of another interface touched', '                if (ids[k].iface != AUDIO_CTL_IFACE_MIXER)\n                        continue;\n', ''),
    ("alsa", 'the capture touched for the outputs', 'if (!kind || !(which & (kind == 3 || kind == 4 ? 2 : 1)))', 'if (!kind || !(which & 3))'),
    ("alsa", 'the outputs touched for the capture', 'if (!kind || !(which & (kind == 3 || kind == 4 ? 2 : 1)))', 'if (!kind || !(which & (kind == 3 || kind == 4 ? 2 : 3)))'),
    ("alsa", 'only the first channel written', '        for (unsigned int i = 0; i < count; i++)\n        {\n                if (enumerated)', '        for (unsigned int i = 0; i < 1; i++)\n        {\n                if (enumerated)'),
    ("alsa", 'a control already right written again', '        if (!changed)\n                return 0;\n        if (audio_sys_ioctl(fd, AUDIO_CTL_ELEM_WRITE, &put) < 0)', '        if (audio_sys_ioctl(fd, AUDIO_CTL_ELEM_WRITE, &put) < 0)'),
    ("alsa", 'a write that fails not counted', '                else if (changed < 0)\n                        report->failed++;', '                else if (changed < 0)\n                        report->failed += 0;'),
    ("alsa", 'a switch left off not said', 'if ((kind == 1 || kind == 3) && changed != 0)', 'if (0)'),
    ("alsa", 'a switch left off judged by the wrong value', 'AUDIO_CTL_ELEM_READ, &now) == 0 && now.value.integer[0] == 0)', 'AUDIO_CTL_ELEM_READ, &now) == 0 && now.value.integer[0] == 2)'),
    ("alsa", 'auto-mute turned off', 'audio_mixer_item((int)fd, &info, "Enabled")', 'audio_mixer_item((int)fd, &info, "Disabled")'),
    ("alsa", 'auto-mute not looked for among the items', 'item < info->value.enumerated.items && item < 64; item++', 'item < 1 && item < 64; item++'),
    ("alsa", 'auto-mute of a card that has no such item written', '                        want = audio_mixer_item((int)fd, &info, "Enabled");\n                        if (want < 0)\n                                continue;', '                        want = audio_mixer_item((int)fd, &info, "Enabled");'),
    ("alsa", 'a boolean control taken for an integer one', '                        if (info.type != AUDIO_CTL_TYPE_BOOLEAN)\n                                continue;', ''),
    ("alsa", 'an integer control taken for a boolean one', '                        if (info.type != AUDIO_CTL_TYPE_INTEGER)\n                                continue;', ''),
    ("alsa", 'the Headphone not an output', '"Master",     "Speaker",    "Headphone",    "Headset",', '"Master",     "Speaker",    "Headphones",   "Headset",'),
    ("alsa", 'the Speaker not an output', '"Master",     "Speaker",    "Headphone",    "Headset",', '"Master",     "Speakers",   "Headphone",    "Headset",'),
    ("alsa", 'the S/PDIF not an output', '"Speaker+LO", "IEC958",     "IEC958 Default PCM",', '"Speaker+LO", "IEC95",      "IEC958 Default PCM",'),
    ("alsa", 'the line-in monitor an output', '"Line Out", "Surround",', '"Line", "Surround",'),
    ("alsa", 'a name that ends in the base taken for it', '                if (k != length)\n                        continue;', ''),
    ("alsa", 'a name that only starts like a switch', 'if (audio_mixer_ends(name, " Playback Switch", &before))\n                return audio_mixer_output_base(name, before) ? 1 : 0;', 'if (audio_mixer_ends(name, "Switch", &before))\n                return audio_mixer_output_base(name, before - 9) ? 1 : 0;'),
    ("alsa", 'the controls of the card not counted', '        report->controls = list.count;', '        report->controls = 0;'),
    ("alsa", 'a card with no mixer is an error', '        fd = audio_sys_open(path, AUDIO_O_RDWR | AUDIO_O_CLOEXEC);\n        if (fd < 0)\n                return fd;\n        __builtin_memset(&list, 0, sizeof(list));', '        fd = audio_sys_open(path, AUDIO_O_RDWR | AUDIO_O_CLOEXEC);\n        if (fd < 0)\n                return 0;\n        __builtin_memset(&list, 0, sizeof(list));'),
    ("alsa", 'the mixer file left open', '        audio_sys_close((int)fd);\n        return 0;\n}\n\n#endif // AUDIO_ALSA_INCLUDED', '        return 0;\n}\n\n#endif // AUDIO_ALSA_INCLUDED'),
    ("alsa", "the list's room larger than the array", '        list.space = AUDIO_MIXER_MAX;', '        list.space = AUDIO_MIXER_MAX * 2;'),
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
    ("pulse", "kill open to all", "if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (command == PULSE_COMMAND_KILL_SOURCE_OUTPUT)", "if (command == PULSE_COMMAND_KILL_SOURCE_OUTPUT)"),
    ("pulse", "the default sink open to all", "if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (name)\n                        sink = pulse_sink_named(p, name);", "if (name)\n                        sink = pulse_sink_named(p, name);"),
    ("pulse", "the default source open to all", "if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (name)\n                        source = pulse_source_named(p, name);", "if (name)\n                        source = pulse_source_named(p, name);"),
    ("pulse", "a flush that cuts at once", "s->voice.ring.write = s->voice.ring.limit;\n                                s->flushing = 1;", "s->voice.ring.limit = s->voice.ring.write = s->voice.ring.read;\n                                s->flushing = 1;"),
    ("pulse", "cork not applied", "s->corked = cork;\n                if (cork)\n                        s->underrun = 1;\n                pulse_stream_apply(s);", "s->corked = cork;"),
    ("pulse", 'record peak window one too long', 'if (++r->window_count >= window)', 'if (++r->window_count > window)'),
    ("pulse", 'record peak keeps the lesser of the pair', 'o0[made] = o1[made] = r->window[0] > r->window[1] ? r->window[0] : r->window[1];', 'o0[made] = o1[made] = r->window[0] > r->window[1] ? r->window[1] : r->window[0];'),
    ("pulse", 'record peak right side not kept', 'r->window[1] = b > r->window[1] ? b : r->window[1];', 'r->window[1] = r->window[1];'),
    ("pulse", 'record big-endian not turned over', '        if (swap)\n        {\n                width = audio_format_bytes(base);', '        if (0)\n        {\n                width = audio_format_bytes(base);'),
    ("pulse", 'record swap leaves the middle byte of three', 'for (long k = 0; k < width / 2; k++)', 'for (long k = 0; k < width / 2 - (width == 3); k++)'),
    ("pulse", 'record U8 not rounded', 'v = (v + 128) >> 8;', 'v = v >> 8;'),
    ("pulse", 'record U8 not held', 'dst[i] = (unsigned char)((v > 127 ? 127 : v) + 128);', 'dst[i] = (unsigned char)(v + 128);'),
    ("pulse", 'record fragment size ignored', 'if (r->fragsize >= (unsigned long)frame && r->fragsize < (unsigned long)chunk)\n                chunk = (long)r->fragsize;', 'if (0)\n                chunk = (long)r->fragsize;'),
    ("pulse", 'record queue keeps no reserve', 'if (PULSE_OUT - pulse_out_fill(c) - 20 - n < PULSE_RECORD_RESERVE)', 'if (PULSE_OUT - pulse_out_fill(c) - 20 - n < 0)'),
    ("pulse", 'record dropped bytes not counted', 'r->dropped += (unsigned long long)count;\n                        return;', 'return;'),
    ("pulse", 'record stream gain not applied', 'audio_gain_run(g0, a0, n, &now0, r->target, audio_slope(r->now[0], r->target), r->peak, 0);', 'audio_gain_run(g0, a0, n, &now0, 1.0f, audio_slope(r->now[0], 1.0f), r->peak, 0);'),
    ("pulse", 'record source volume not applied', 'target = pulse_source_target(s);\n        a = b = s->now;', 'target = 1.0f;\n        a = b = s->now;'),
    ("pulse", 'record source mute not applied', 'return s->muted ? 0.0f : pulse_mean_gain(s->volume, 2);', 'return pulse_mean_gain(s->volume, 2);'),
    ("pulse", 'record stream mute not applied', 'r->target = r->muted ? 0.0f : pulse_mean_gain(r->volume, r->map_channels);', 'r->target = pulse_mean_gain(r->volume, r->map_channels);'),
    ("pulse", 'record corked stream still fed', 'if (p->record[i].used && p->record[i].source == slot && !p->record[i].corked)\n                        pulse_record_take(', 'if (p->record[i].used && p->record[i].source == slot)\n                        pulse_record_take('),
    ("pulse", 'record corked stream still wanted', 'n += p->record[i].used && p->record[i].source == slot && !p->record[i].corked;', 'n += p->record[i].used && p->record[i].source == slot;'),
    ("pulse", 'record monitor not fed', '        pulse_source_feed(p, slot, sink->mix.mix[0], sink->mix.mix[1], frames);\n', ''),
    ("pulse", 'record monitor taken before the master volume', 'pulse_source_feed(p, slot, sink->mix.mix[0], sink->mix.mix[1], frames);', 'pulse_source_feed(p, slot, sink->mix.bus[0], sink->mix.bus[1], frames);'),
    ("pulse", 'record another user hears the monitor', 'if (!p->source[source].capture && !c->root)\n                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n        for (int i = 0; i < PULSE_RECORDS; i++)', 'for (int i = 0; i < PULSE_RECORDS; i++)'),
    ("pulse", 'record made with capture off', '        if (p->capture_off)\n                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n        if (direct', '        if (direct'),
    ("pulse", 'record capture off leaves the streams', '        p->capture_off = !allowed;\n        if (!allowed)', '        p->capture_off = !allowed;\n        if (0)'),
    ("pulse", 'record resampler tables the wrong way round', 'table = audio_table_take(p->source[source].rate, rate);', 'table = audio_table_take(rate, p->source[source].rate);'),
    ("pulse", 'record peaks get a filter', 'if (!peak && rate != p->source[source].rate)', 'if (rate != p->source[source].rate)'),
    ("pulse", 'record capture device not the default over a monitor', 'if (p->default_source < 0 || p->default_source < PULSE_SINKS)', 'if (p->default_source < 0)'),
    ("pulse", 'record kill not told', 'if (tell && c)\n        {\n                struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};\n\n                pulse_w_u32(&w, PULSE_COMMAND_RECORD_STREAM_KILLED);', 'if (0 && c)\n        {\n                struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};\n\n                pulse_w_u32(&w, PULSE_COMMAND_RECORD_STREAM_KILLED);'),
    ("pulse", 'record end not announced', '        pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_REMOVE, r->index);\n        pulse_changed(p, PULSE_CHANGED_RECORDS, r->source);\n        pulse_record_free(p, r);', '        pulse_changed(p, PULSE_CHANGED_RECORDS, r->source);\n        pulse_record_free(p, r);'),
    ("pulse", "record another user's output shown", 'if (p->record[i].used && (list || p->record[i].index == index) && pulse_may(c, p->record[i].uid))', 'if (p->record[i].used && (list || p->record[i].index == index))'),
    ("pulse", 'record source volume set by anyone', '                if (!c->root)\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (n != 2)\n                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);\n                p->source[source].volume[0] = volume[0];', '                if (n != 2)\n                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);\n                p->source[source].volume[0] = volume[0];'),
    ("pulse", 'record source mute turned over', 'p->source[source].muted = mute;', 'p->source[source].muted = !mute;'),
    ("pulse", 'record latency indexes swapped', "pulse_w_64(&w, 'r', (long long)rec->produced);\n                pulse_w_64(&w, 'r', (long long)rec->sent);", "pulse_w_64(&w, 'r', (long long)rec->sent);\n                pulse_w_64(&w, 'r', (long long)rec->produced);"),
    ("pulse", 'record rate changed on any stream', 'if (!rec->variable_rate || rate < 1 || rate > AUDIO_RATE_MAX)', 'if (rate < 1 || rate > AUDIO_RATE_MAX)'),
    ("pulse", 'record move leaves the filter', '        if (!r->peak_detect)\n        {\n                audio_table_give(r->table);\n                r->table = table;\n                if (table)\n                        audio_rs_open(&r->rs, table);\n        }\n        pulse_changed(p, PULSE_CHANGED_RECORDS, r->source);', '        pulse_changed(p, PULSE_CHANGED_RECORDS, r->source);'),
    ("pulse", 'record fragment longer than the buffer', '        if (fragsize > maxlength)\n                fragsize = maxlength;\n        fragsize -= fragsize % frame;', '        fragsize -= fragsize % frame;'),
    ("pulse", 'record buffer under one frame', '        if (maxlength < frame)\n                maxlength = frame;\n', ''),
    ("pulse", 'record source not idle', 'pulse_w_u32(w, busy ? 0 : 1); // running, or idle', 'pulse_w_u32(w, 0); // running, or idle'),
    ("pulse", 'record capture device called a monitor', 'pulse_w_u32(w, s->capture ? PULSE_INVALID : (unsigned long)s->sink);', 'pulse_w_u32(w, (unsigned long)(s->sink < 0 ? 0 : s->sink));'),
    ("pulse", 'record name not kept', 'pulse_props_set(&rec->props, "media.name", (const unsigned char *)name, pulse_len(name) + 1);\n                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);\n                return pulse_ack(p, c, tag);\n        }\n        case PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE:', 'pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);\n                return pulse_ack(p, c, tag);\n        }\n        case PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE:'),
    ("pulse", 'record default falls to nothing', 'return p->default_sink >= 0 && p->source[p->default_sink].used ? p->default_sink : -1;', 'return -1;'),
    ("pulse", 'record default sink does not move a monitor default', '                        if (p->default_source >= 0 && p->default_source < PULSE_SINKS)\n                                pulse_source_default(p, sink);', ''),
    ("pulse", 'record default source does not move its followers', '                if (p->record[i].used && !p->record[i].owner_set && !p->record[i].no_move)\n                        pulse_record_move(p, p->record + i, slot);', ''),
    ("pulse", 'record the stream that named its source is moved', 'if (p->record[i].used && !p->record[i].owner_set && !p->record[i].no_move)', 'if (p->record[i].used && !p->record[i].no_move)'),
    ("pulse", 'record volume of a stranger', '                if (!pulse_may(c, rec->uid))\n                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n                if (n != rec->map_channels)', '                if (n != rec->map_channels)'),
    ("pulse", 'record move onto a monitor by anyone', 'if (!pulse_may(c, rec->uid) || (!p->source[source].capture && !c->root))', 'if (!pulse_may(c, rec->uid))'),
    ("pulse", "record a client's streams kept after it goes", '        for (int i = 0; i < PULSE_RECORDS; i++)\n                if (p->record[i].used && p->record[i].client == slot)\n                        pulse_record_end(p, p->record + i, 0);', ''),
    ("pulse", 'record a device that goes leaves its streams', '        for (int i = 0; i < PULSE_RECORDS; i++)\n                if (p->record[i].used && p->record[i].source == slot)\n                        pulse_record_end(p, p->record + i, 1);\n        p->source[slot].used = 0;', '        p->source[slot].used = 0;'),
    ("pulse", "record a device's new rate not met", '        if (changed)\n                pulse_source_retable(p, slot);\n        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)slot);', '        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)slot);'),
    ("pulse", 'record a stream with no table kept after the ratio fails', '                        if (!table)\n                        {\n                                pulse_record_end(p, r, 1);\n                                continue;\n                        }', '                        if (!table)\n                                continue;'),
    ("pulse", 'record cap of a client', 'if (count >= PULSE_RECORDS_PER_CLIENT || !rec)', 'if (!rec)'),
    ("pulse", 'record format that cannot be recorded', '        if (!pulse_record_format_ok(format))\n                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);\n', ''),
    ("pulse", "record meter on another's stream served", '        if (direct != PULSE_INVALID)\n                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);', ''),
    ("pulse", 'sample name of any length', '        if (n < 1 || n >= PULSE_NAME)\n                return 0;\n        for (long i = 0; i < n; i++)', '        if (n < 1)\n                return 0;\n        for (long i = 0; i < n; i++)'),
    ("pulse", 'sample name with control bytes', "if ((unsigned char)name[i] < ' ' || (unsigned char)name[i] == 127)\n                        return 0;\n        return 1;", 'if (0)\n                        return 0;\n        return 1;'),
    ("pulse", 'sample of any size', 'if (length > PULSE_SAMPLE_MAX || mine >= PULSE_UPLOADS_PER_CLIENT', 'if (mine >= PULSE_UPLOADS_PER_CLIENT'),
    ("pulse", 'uploads of a client unbounded', 'if (length > PULSE_SAMPLE_MAX || mine >= PULSE_UPLOADS_PER_CLIENT || !u', 'if (length > PULSE_SAMPLE_MAX || !u'),
    ("pulse", 'cache of any size', 'p->sample_total + p->upload_total + length > PULSE_SAMPLE_TOTAL ||', '0 ||'),
    ("pulse", "a user's share of the cache not held", '(!c->root && pulse_sample_held(p, c->uid) + length > PULSE_SAMPLE_USER))', '0)'),
    ("pulse", 'sample of part of a frame', 'length == 0 || length % (unsigned long)(audio_format_bytes(format) * channels))', 'length == 0)'),
    ("pulse", 'upload data past what was said', 'if ((flags & 0xff) != 0 || offset != 0 || (unsigned long)length > u->length - u->got)\n                                return PULSE_DEAD;', 'if ((flags & 0xff) != 0 || offset != 0)\n                                return PULSE_DEAD;'),
    ("pulse", 'upload data that seeks', 'if ((flags & 0xff) != 0 || offset != 0 || (unsigned long)length > u->length - u->got)', 'if ((unsigned long)length > u->length - u->got)'),
    ("pulse", 'upload finished short', '        if (u->got != u->length)\n        {\n                pulse_upload_free(p, u);\n                return pulse_error(p, c, tag, PULSE_ERR_INVALID);\n        }', '        if (0)\n        {\n                pulse_upload_free(p, u);\n                return pulse_error(p, c, tag, PULSE_ERR_INVALID);\n        }'),
    ("pulse", 'sample replaced by a stranger', 'if (old && !pulse_may(c, old->uid))\n        {', 'if (0)\n        {'),
    ("pulse", 'sample taken out by a stranger', '        if (!pulse_may(c, s->uid))\n                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);\n        s->condemned = 1;', '        s->condemned = 1;'),
    ("pulse", 'replacement has a new index', 's->index = old ? old->index : p->next_sample++;', 's->index = p->next_sample++;'),
    ("pulse", 'replacement announced as new', 'pulse_event(p, PULSE_FACILITY_SAMPLE_CACHE, old ? PULSE_EVENT_CHANGE : PULSE_EVENT_NEW, s->index);', 'pulse_event(p, PULSE_FACILITY_SAMPLE_CACHE, PULSE_EVENT_NEW, s->index);'),
    ("pulse", 'sample freed under the stream that plays it', 'if (!s->used || !s->condemned || s->playing > 0)\n                return;', 'if (!s->used || !s->condemned)\n                return;'),
    ("pulse", 'sample kept after its last stream', '                sample->playing--;\n                pulse_sample_release(p, sample);', '                sample->playing--;'),
    ("pulse", 'playing not counted', '        sample->playing++;\n', ''),
    ("pulse", 'volume of a play ignored', 's->volume[i] = volume == PULSE_INVALID ? AUDIO_VOLUME_NORM : volume > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : volume;', 's->volume[i] = AUDIO_VOLUME_NORM;'),
    ("pulse", 'remembered volume not used for a sound', 'if (volume == PULSE_INVALID && p->host.recall && pulse_props_text(&c->props', 'if (0 && p->host.recall && pulse_props_text(&c->props'),
    ("pulse", 'sample never ends', 'if (played < frames && s->one_shot)', 'if (0)'),
    ("pulse", 'sounds without a cap on what one program plays', 'if (mine >= PULSE_STREAMS_PER_CLIENT || !s)\n                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);\n        __builtin_memset(s, 0, __builtin_offsetof(struct pulse_stream, voice));\n        if (audio_voice_open(&s->voice, sample->format', 'if (!s)\n                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);\n        __builtin_memset(s, 0, __builtin_offsetof(struct pulse_stream, voice));\n        if (audio_voice_open(&s->voice, sample->format'),
    ("pulse", 'sink named and numbered for a sound', '                return pulse_error(p, c, tag, PULSE_ERR_INVALID);\n        if (sink_name && sink_name[0])\n                sink = pulse_sink_named(p, sink_name);\n        else if (sink_index != PULSE_INVALID)\n                sink = pulse_find_sink(p, sink_index, 0);\n        else\n                sink = p->default_sink;\n        sample = pulse_sample_named(p, name);', '                (void)0;\n        if (sink_name && sink_name[0])\n                sink = pulse_sink_named(p, sink_name);\n        else if (sink_index != PULSE_INVALID)\n                sink = pulse_find_sink(p, sink_index, 0);\n        else\n                sink = p->default_sink;\n        sample = pulse_sample_named(p, name);'),
    ("pulse", "sound's stream owned by the program that asked", '        s->client = -1;\n        s->index = p->next_input++;\n        s->sink = sink;\n        s->format = sample->format;', '        s->client = slot;\n        s->index = p->next_input++;\n        s->sink = sink;\n        s->format = sample->format;'),
    ("pulse", "sound's own properties not on its stream", '        pulse_props_update(&s->props, &sample->props, 1);\n', ''),
    ("pulse", "sound's stream not named for its program", '        if (!pulse_props_text(&s->props, "application.name", value, sizeof(value)) && pulse_props_text(&c->props, "application.name", value, sizeof(value)))\n                pulse_props_set(&s->props, "application.name", (const unsigned char *)value, pulse_len(value) + 1);\n        if (!pulse_props_text(&s->props, "media.name"', '        if (!pulse_props_text(&s->props, "media.name"'),
    ("pulse", 'sample out of the cache still shown', 'if (p->sample[i].used && !p->sample[i].condemned && (list ||', 'if (p->sample[i].used && (list ||'),
    ("pulse", 'sample out of the cache still found', 'if (p->sample[i].used && !p->sample[i].condemned && pulse_equal(p->sample[i].name, name))', 'if (p->sample[i].used && pulse_equal(p->sample[i].name, name))'),
    ("pulse", "cache's size not said", 'pulse_w_u32(&w, p->sample_total);', 'pulse_w_u32(&w, 0);'),
    ("pulse", "a client's uploads kept after it goes", '        for (int i = 0; i < PULSE_UPLOADS; i++)\n                if (p->upload[i].used && p->upload[i].client == slot)\n                        pulse_upload_free(p, p->upload + i);\n', ''),
    ("pulse", 'an upload finished by another client', 'if (p->upload[i].used && p->upload[i].client == client && p->upload[i].channel == channel)', 'if (p->upload[i].used && p->upload[i].channel == channel)'),
    ("pulse", 'sample duration wrong', "pulse_w_64(w, 'U', (long long)frames * 1000000 / (long long)s->rate);", "pulse_w_64(w, 'U', (long long)frames * 1000 / (long long)s->rate);"),
    ("pulse", 'sample played from its start only at the fade', '        s->voice.ring.write = s->voice.ring.limit = s->voice.ring.size;\n        s->used = 1;\n        s->client = -1;', '        s->voice.ring.write = s->voice.ring.limit = s->voice.ring.size / 2;\n        s->used = 1;\n        s->client = -1;'),
    ("pulse", "overlong four-byte UTF-8 taken as text","if (good && extra == 3 && c == 0xf0 && v[at + 1] < 0x90)\n                                                good = 0;", ""),
    ("pulse", "four-byte UTF-8 past U+10FFFF taken as text", "if (good && extra == 3 && c == 0xf4 && v[at + 1] >= 0x90)\n                                                good = 0;", ""),
    ("pulse", "string copy cut one byte short", "if (n > room - 1)\n                n = room - 1;", "if (n > room - 1)\n                n = room - 2;"),
    ("pulse", "string copy cut one byte short", "if (n > room - 1)\n                n = room - 1;", "if (n > room - 1)\n                n = room - 2;"),
    ("pulse", "output ring wrap copy dropped", "if (first < n)\n                __builtin_memcpy(c->out, from + first, (unsigned long)(n - first));", "if (0)\n                __builtin_memcpy(c->out, from + first, (unsigned long)(n - first));"),
    ("pulse", 'a list too large drops its client', 'if (w->bad)\n                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);\n        return pulse_reply_end(c, w);', 'return pulse_reply_end(c, w);'),
    ("pulse", 'a feed keeps the frames it has read', 'c->in_have -= at;', 'c->in_have -= 0;'),
    ("pulse", 'a replacement in a full cache needs a free place', 'if (old && old->playing == 0)\n        {\n                old->condemned = 1;\n                pulse_sample_release(p, old);\n        }\n        for (int i = 0; i < PULSE_SAMPLES && !s; i++)', 'for (int i = 0; i < PULSE_SAMPLES && !s; i++)'),
    # ---- the four loops with specifications in checks.c (audio_c_text_clean, audio_c_key_printable, audio_c_reverse_units,
    #      audio_c_s16_to_u8, audio_c_window_peaks): each engine loop edited, and the reference row must turn red ----
    ("pulse", "name cleaner: tilde shown as '?'", "if (c >= 0x20 && c < 0x7f)", "if (c >= 0x20 && c < 0x7e)"),
    ("pulse", "key test: DEL taken as a key byte", "(unsigned char)key[i] >= 127", "(unsigned char)key[i] >= 128"),
    ("pulse", "key test: space taken as a key byte", "(unsigned char)key[i] <= ' ' ||", "(unsigned char)key[i] < ' ' ||"),
    ("pulse", "big-endian: all but the last byte pair reversed", "for (long k = 0; k < width / 2; k++)", "for (long k = 0; k < width / 2 - 1; k++)"),
    ("pulse", "U8: clip at 126", "v > 127 ? 127 : v", "v > 126 ? 126 : v"),
    ("pulse", "U8: rounding at 127 not 128", "v = (v + 128) >> 8;", "v = (v + 127) >> 8;"),
    ("pulse", "peak window one frame long", "++r->window_count >= window", "++r->window_count > window"),
    ("pulse", "mono peaks never joined", "if (r->channels == 1)", "if (r->channels == 3)"),
    # ---- control.c (the mixer's client, checked in CHECK_audio_pulse against the engine) ----
    ("control", "a volume on the first channel only", "for (long i = 0; i < channels; i++)\n                all[i] = volume;", "for (long i = 0; i < 1; i++)\n                all[i] = volume;"),
    ("control", "a mute turned over", "ctl_w_bool(&w, muted != 0);\n        if (w.bad)\n                return CTL_FAILED;\n        got = ctl_exchange(c, w.len);\n        return got < 0 ? got : 0;\n}\n\nstatic long ctl_set_input_mute", "ctl_w_bool(&w, muted == 0);\n        if (w.bad)\n                return CTL_FAILED;\n        got = ctl_exchange(c, w.len);\n        return got < 0 ? got : 0;\n}\n\nstatic long ctl_set_input_mute"),
    ("control", "the channel count not kept", "        c->kept[c->kept_next].index = index;\n        c->kept[c->kept_next].channels = channels;\n        c->kept_next = (c->kept_next + 1) % CTL_CACHE;", "        c->kept_next = (c->kept_next + 1) % CTL_CACHE;"),
    ("control", "a wrong count not mended", "                if (got != CTL_INVALID)\n                        return got < 0 ? got : 0;", "                return got < 0 ? got : 0;"),
    ("control", "an access error as a failure", "return code == CTL_ERR_ACCESS ? CTL_ACCESS :", "return code == CTL_ERR_ACCESS ? CTL_FAILED :"),
    ("control", "a missing stream as a failure", "code == CTL_ERR_NOENTITY ? CTL_NOENTITY", "code == CTL_ERR_NOENTITY ? CTL_FAILED"),
    ("control", "any tag taken for ours", "if (frame[20] != 'L' || frame[25] != 'L' || answered != tag)", "if (frame[20] != 'L' || frame[25] != 'L')"),
    ("control", "a lost answer not a failure", "        c->open = 0;\n        return CTL_FAILED;\n}\n\n// A request begun", "        return CTL_FAILED;\n}\n\n// A request begun"),
    ("control", "the device named by nothing", "ctl_w_u32(&w, CTL_NONE);\n        ctl_w_string(&w, name);\n        ctl_w_cvolume(&w, both, 2);", "ctl_w_u32(&w, CTL_NONE);\n        ctl_w_string(&w, 0);\n        ctl_w_cvolume(&w, both, 2);"),
    ("control", "the timeout of the connection ignored", "long wait = c->timeout_ms ? c->timeout_ms : CTL_TIMEOUT_MS;", "long wait = CTL_TIMEOUT_MS;"),
    ("control", "a stream's mute misread", "into->muted = ctl_r_bool(&r);", "into->muted = 0, (void)ctl_r_bool(&r);"),
    # ---- bowl.c (the sound server a bowl's program is offered, checked by the bowl_session harness) ----
    ("bowl", "the sound server offered with no socket", "return system_access_at(AT_FDCWD, (string_address)BOWL_SOUND_SOCKET, 0) >= 0;", "return true;"),
    ("bowl", "the sound server offered over the program's own", "if (offer_sound && !have_sound)\n                mixed[n++]", "if (offer_sound)\n                mixed[n++]"),
    ("bowl", "a session with its own everything else not offered the sound server", "                missing += offer_sound && !have_sound;\n", ""),
    ("bowl", "the program's own sound server not seen", "if (bowl_env_named(inherited[at], BOWL_SOUND_VARIABLE))\n                        have_sound = true;", "if (0)\n                        have_sound = true;"),
    ("bowl", "the sound server's socket said wrong", "\"=unix:\" BOWL_SOUND_SOCKET", "\"=\" BOWL_SOUND_SOCKET"),
    ("bowl", "no room kept for what is offered", "#define BOWL_ENV_DEFAULTS 13", "#define BOWL_ENV_DEFAULTS 12"),
    ("bowl", "the bus offered with no socket", "return system_access_at(AT_FDCWD, (string_address)BOWL_SESSION_BUS_SOCKET, 0) >= 0;", "return true;"),
    ("control", 'a source muted as a sink', 'return ctl_device_mute(c, CTL_COMMAND_SET_SOURCE_MUTE, source, muted);', 'return ctl_device_mute(c, CTL_COMMAND_SET_SINK_MUTE, source, muted);'),
    ("control", "a source's volume sent to the sink", 'return ctl_device_volume(c, CTL_COMMAND_SET_SOURCE_VOLUME, source, volume);', 'return ctl_device_volume(c, CTL_COMMAND_SET_SINK_VOLUME, source, volume);'),
    ("control", "a record stream's volume sent as a playing stream's", 'got = ctl_volume_once(c, CTL_COMMAND_SET_SOURCE_OUTPUT_VOLUME, index, volume, guess[k]);', 'got = ctl_volume_once(c, CTL_COMMAND_SET_SINK_INPUT_VOLUME, index, volume, guess[k]);'),
    ("control", "a record stream's mute sent as a playing stream's", 'return ctl_stream_mute(c, CTL_COMMAND_SET_SOURCE_OUTPUT_MUTE, index, muted);', 'return ctl_stream_mute(c, CTL_COMMAND_SET_SINK_INPUT_MUTE, index, muted);'),
    ("control", "a record stream's count not kept", '                        ctl_keep_channels(c, index + CTL_RECORD_KEY, guess[k]);', ''),
    ("control", "a record stream's count kept as a playing stream's", 'ctl_keep_channels(c, index + CTL_RECORD_KEY, guess[k]);', 'ctl_keep_channels(c, index, guess[k]);'),
    ("control", "a record stream's kept count looked up as a playing stream's", 'long channels = ctl_kept_channels(c, index + CTL_RECORD_KEY), got = CTL_INVALID;', 'long channels = ctl_kept_channels(c, index), got = CTL_INVALID;'),
    ("control", "a record stream's counts not tried", 'static const signed char guess[] = {2, 1, 4, 6, 8};', 'static const signed char guess[] = {2};'),
    ("control", "a record stream's wrong count not mended", 'if (channels && (got = ctl_volume_once(c, CTL_COMMAND_SET_SOURCE_OUTPUT_VOLUME, index, volume, channels)) != CTL_INVALID)', 'if (channels && (got = ctl_volume_once(c, CTL_COMMAND_SET_SOURCE_OUTPUT_VOLUME, index, volume, channels)) != 12345)'),
    ("control", "a record stream's refusal for its count taken for one for access", '                if (got == CTL_INVALID)\n                        continue;', '                if (got < 0)\n                        continue;'),
    ("control", "the stream killed is another", "ctl_begin(c, &w, CTL_COMMAND_KILL_SINK_INPUT);\n        ctl_w_u32(&w, index);", "ctl_begin(c, &w, CTL_COMMAND_KILL_SINK_INPUT);\n        ctl_w_u32(&w, index + 1);"),
    # ---- control.c's socket and budget (CHECK_audio_control: the system's socket, to forked servers) ----
    ("socket", "one socket shared by every connection", "        ctl_disconnect(c); // a connection made again replaces the one it had, and does not leak its socket\n        c->socket.fd = (int)fd;\n        c->socket.linked = 1;\n        c->transport = (struct ctl_transport){&c->socket,", "        static struct ctl_socket shared;\n        ctl_disconnect(c);\n        shared.fd = (int)fd;\n        shared.linked = 1;\n        c->transport = (struct ctl_transport){&shared,"),
    ("socket", "a reconnect leaks the socket it replaces", "        ctl_disconnect(c); // a connection made again replaces the one it had, and does not leak its socket\n", ""),
    ("socket", "a receive waits per part again", "if (ctl_wait_until(s, 1, until) <= 0)", "if (ctl_wait(s->fd, 1, timeout_ms) <= 0)"),
    ("socket", "a send waits per part again", "if (ctl_wait_until(s, 0, until) <= 0)", "if (ctl_wait(s->fd, 0, timeout_ms) <= 0)"),
    ("socket", "every part of an answer given the whole timeout", "c->transport.receive(c->transport.context, frame, 20, ctl_left(c)) < 0", "c->transport.receive(c->transport.context, frame, 20, c->timeout_ms ? c->timeout_ms : CTL_TIMEOUT_MS) < 0"),
    ("socket", "a call's budget never begun", "        c->deadline = ctl_now(c) + (c->timeout_ms ? c->timeout_ms : CTL_TIMEOUT_MS);", "        (void)0;"),
    ("socket", "a transport with no clock called through null", "return c->transport.now ? c->transport.now(c->transport.context) : 0;", "return c->transport.now(c->transport.context);"),
    # ---- alsa.c: a negative count is refused before the card is asked (CHECK_audio_alsa) ----
    ("alsa", "a negative write count sent to the card", "        if (count < 0)\n                return AUDIO_EINVAL;\n        result = audio_sys_write(", "        if (0)\n                return AUDIO_EINVAL;\n        result = audio_sys_write("),
    ("alsa", "a negative read count sent to the card", "        if (count < 0)\n                return AUDIO_EINVAL;\n        result = audio_sys_read(", "        if (0)\n                return AUDIO_EINVAL;\n        result = audio_sys_read("),
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
    if which == "bowl":
        os.makedirs(d + "/src")
        os.makedirs(d + "/test")
        shutil.copy(os.path.join(root, "test/differential.py"), d + "/test/differential.py")
        open(d + "/src/bowl.c", "w").write(src.replace(old, new, 1))
        try:
            r = subprocess.run([sys.executable, d + "/test/differential.py", "--harness", "bowl_session"], capture_output=True, text=True,
                               errors="replace", timeout=300, env=dict(os.environ, CC="gcc"))
        except subprocess.TimeoutExpired:
            return (name, "RED (hang)", "")
        fails = [l.strip() for l in r.stderr.splitlines() + r.stdout.splitlines() if l.startswith("FAIL")][:1]
        if r.returncode == 0:
            return (name, "SURVIVED", "bowl session")
        if "error:" in r.stdout + r.stderr:
            return (name, "BUILD ERROR", (r.stdout + r.stderr)[:200])
        return (name, "RED", "bowl session" + (" | " + fails[0][:110] if fails else ""))
    os.makedirs(d + "/src/audio")
    os.makedirs(d + "/test")
    os.symlink(checks, d + "/test/checks.c")
    for k, v in engines.items():
        shutil.copy(v, d + "/src/audio/" + k + ".c")
    open(d + "/src/audio/" + target_file.get(which, which) + ".c", "w").write(src.replace(old, new) if name.startswith("peak") else src.replace(old, new, 1))
    exe = d + "/check"
    cc = subprocess.run(["gcc", "-std=gnu11", "-O2", "-w", "-D_GNU_SOURCE", "-DCHECK_audio_" + check_of.get(which, which), d + "/test/checks.c", "-o", exe, "-lm"],
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
