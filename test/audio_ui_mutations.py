#!/usr/bin/env python3
"""Mutation proofs for the mixer's pieces of the kit (src/canvas/ui/ui.c: the level meter, the vertical slider, the strips) and the mixer program
(src/canvas/ui/mixer.c).

Each mutation is one edit to a copy of one of those files, built into CHECK_terminal with gcc and run from the tree's root; the check must turn red.
A mutation that survives is a hole in the check. The mutant's tree is the real one with the one file changed (symbolic links for every other),
so a build costs the check's compile and nothing else.

    python3 test/audio_ui_mutations.py [NAME...]       from the tree's root; 6 builds at a time, about 5 minutes

The control client's mutations (src/audio/control.c) are test/audio_mutations.py's, checked in CHECK_audio_pulse.
"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
only = sys.argv[1:]
work = tempfile.mkdtemp(prefix='audio-ui-mutations-')
fonts = ["fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf", "fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf", "fonts/inter/Inter-Regular.ttf", "fonts/inter/Inter-Bold.ttf"]

U = "src/canvas/ui/ui.c"
X = "src/canvas/ui/mixer.c"
T = "src/canvas/ui/taskbar.c"
C = "src/canvas/compositor.c"

M = [
    # ---- the level meter ----
    (U, "meter scale halved", "100343 / 1000000", "100343 / 2000000"),
    (U, "meter falls in three seconds", "#define UI_LEVEL_FALL_NS 1500000000ll", "#define UI_LEVEL_FALL_NS 3000000000ll"),
    (U, "meter line holds half a second", "#define UI_LEVEL_HOLD_NS 1000000000ll", "#define UI_LEVEL_HOLD_NS 500000000ll"),
    (U, "meter bar does not fall", "b64 bar = l->shown - ui_progress(now - l->at, UI_LEVEL_FALL_NS), line;", "b64 bar = l->shown, line;"),
    (U, "meter does not rise at once", "        if (target > bar)\n                bar = target;\n        if (target >= line)", "        if (target >= line)"),
    (U, "meter line not raised by a louder peak", "        if (target >= line)\n                line = target, l->held = now;", "        if (target > line + UI_ONE)\n                line = target, l->held = now;"),
    (U, "meter amber too low", "#define UI_METER_AMBER UI_Q2(75)", "#define UI_METER_AMBER UI_Q2(60)"),
    (U, "meter red missing", "return ui_role(share > UI_METER_RED ? UI_ERROR : share > UI_METER_AMBER ? UI_WARNING : UI_SUCCESS);", "return ui_role(share > UI_METER_AMBER ? UI_WARNING : UI_SUCCESS);"),
    # ---- the vertical slider ----
    (U, "slider value upside down", "- ui_dev(ui, UI_VSLIDER_KNOB) / 2 - y, ratio;", "- ui_dev(ui, UI_VSLIDER_KNOB) / 2 + y, ratio;"),
    (U, "slider travel without the knob", "return (b64)ui_dev(ui, c->h) - ui_dev(ui, UI_VSLIDER_KNOB);", "return (b64)ui_dev(ui, c->h);"),
    (U, "slider value not snapped", "        return ui_slider_snap(c, c->low + ui_qmul(ratio, c->high - c->low));\n}\n\n// The knob's centre", "        return c->low + ui_qmul(ratio, c->high - c->low);\n}\n\n// The knob's centre"),
    (U, "slider fill from the top", "        fill.y0 = ky;\n", "        fill.y1 = ky;\n"),
    (U, "slider muted fill not grey", "fill_ink = c->on ? ui_role(UI_OUTLINE) : accent", "fill_ink = accent"),
    (U, "slider drag has a dead zone of the horizontal one", "if (!c->moved && dy < 4 * ui->scale / 100 && dy > -4 * ui->scale / 100)\n                                                return null;", "if (!c->moved && dy < 400 * ui->scale / 100 && dy > -400 * ui->scale / 100)\n                                                return null;"),
    # ---- the strips ----
    (U, "Up is two", "key == UI_KEY_UP ? percent + 1", "key == UI_KEY_UP ? percent + 2"),
    (U, "Page Down is five", "key == UI_KEY_PAGE_DOWN ? percent - 10", "key == UI_KEY_PAGE_DOWN ? percent - 5"),
    (U, "Home is 0", "key == UI_KEY_HOME ? 100 : 0;", "key == UI_KEY_HOME ? 0 : 0;"),
    (U, "M does not mute", "        case UI_KEY_M:\n        case UI_KEY_SPACE:", "        case 0:\n        case UI_KEY_SPACE:"),
    (U, "a held M mutes again", "                if (event->flags & WINDOW_KEY_REPEAT)\n                        return UI_STRIPS_TAKEN;\n                s->item[k].muted", "                s->item[k].muted"),
    (U, "arrows not turned for a window read from the right", "                k += ui_arrow(key);\n                break;", "                k += key == UI_KEY_RIGHT ? 1 : -1;\n                break;"),
    (U, "Tab does not wrap", "k = k < 0 ? s->count - 1 : k >= s->count ? 0 : k;", "k = k < 0 ? 0 : k >= s->count ? s->count - 1 : k;"),
    (U, "wheel notch is three", "#define UI_STRIPS_WHEEL 2 ", "#define UI_STRIPS_WHEEL 3 "),
    (U, "wheel up lowers", "        steps = (int)(total / 120);\n        s->wheel = (int)(total - (long)steps * 120);\n        if (!steps)\n                return UI_STRIPS_TAKEN;", "        steps = -(int)(total / 120);\n        s->wheel = (int)(total + (long)steps * 120);\n        if (!steps)\n                return UI_STRIPS_TAKEN;"),
    (U, "a person's volume replaced at once", "if (!old || now - strip->changed_at > UI_STRIPS_HOLD_NS)", "if (1)"),
    (U, "a person's volume held for ever", "if (!old || now - strip->changed_at > UI_STRIPS_HOLD_NS)", "if (!old)"),
    (U, "strips not mirrored", "return ui_mirror(s->x + ui_strip_left(ui, k) - (int)shift, ui_dev(ui, UI_STRIP_W), s->x, s->w);", "return s->x + ui_strip_left(ui, k) - (int)shift;"),
    (U, "no gap after the device", "UI_STRIP_PAD + k * UI_STRIP_W + (k > 0 ? UI_STRIP_GAP : 0)", "UI_STRIP_PAD + k * UI_STRIP_W"),
    (U, "focus lost with the order", "                if (s->item[k].id == s->focus_id)\n                {", "                if (0)\n                {"),
    (U, "keys do not scroll to the strip", "        ui_strips_reveal(ui, s, k);\n        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;", "        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;"),
    (U, "a drag of the ground moves the wrong way", "b64 want = s->grab_shift - (b64)(x - s->grab_x) * ui_dir();", "b64 want = s->grab_shift + (b64)(x - s->grab_x) * ui_dir();"),
    (U, "the mute button label not changed", "mute->label = s->mute_label[k][strip->muted];", "mute->label = s->mute_label[k][0];"),
    (U, "a slide's target not taken", "b64 target = changed->sliding ? changed->slide.to : changed->value;", "b64 target = changed->value;"),
    (U, "the speaker never struck", "ui_speaker(ui, centre - size / 2, s->y + ui_dev(ui, 16) + (face - size) / 2, size, waves, strip->muted, ink);", "ui_speaker(ui, centre - size / 2, s->y + ui_dev(ui, 16) + (face - size) / 2, size, waves, 0, ink);"),
    # ---- the program ----
    (X, "same name of another user is one program", "return a->uid == b->uid && string_equals((string_address)a->app, (string_address)b->app);", "return string_equals((string_address)a->app, (string_address)b->app);"),
    (X, "percent truncated", "((unsigned long)volume * 100 + MIXER_VOLUME / 2) / MIXER_VOLUME", "((unsigned long)volume * 100) / MIXER_VOLUME"),
    (X, "a program at its quietest stream", "agg[at].percent = mixer_percent(stream->volume) > agg[at].percent ? mixer_percent(stream->volume) : agg[at].percent;", "agg[at].percent = at < 0 || agg[at].percent == 0 ? mixer_percent(stream->volume) : agg[at].percent;"),
    (X, "muted when any stream is", "agg[at].muted = agg[at].muted && (stream->flags & AUDIO_STATE_MUTED) != 0;", "agg[at].muted = agg[at].muted || (stream->flags & AUDIO_STATE_MUTED) != 0;"),
    (X, "a corked stream is playing", "agg[at].playing = agg[at].playing || ((stream->flags & AUDIO_STATE_PLAYING) && !(stream->flags & AUDIO_STATE_CORKED));", "agg[at].playing = agg[at].playing || (stream->flags & AUDIO_STATE_PLAYING);"),
    (X, "only the first stream of a program is sent", "member < big->group[k - big->device].count && answer >= 0;", "member < 1 && answer >= 0;"),
    (X, "a volume sent as a percent", "unsigned long volume = (unsigned long)strip->percent * MIXER_VOLUME / 100;", "unsigned long volume = (unsigned long)strip->percent;"),
    (X, "a failed send forgotten", "                if (answer == CTL_FAILED)\n                {\n                        mixer_disconnect();\n                        failed = true;\n                        break;\n                }", "                if (answer == CTL_FAILED)\n                {\n                        mixer_disconnect();\n                        failed = true;\n                }"),
    (X, "the device's volume sent to nobody", "answer = ctl_set_sink_volume(&big->ctl, big->sink, volume);", "answer = ctl_set_sink_volume(&big->ctl, \"\", volume);"),
    (X, "Escape does not close", "if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && event->a == UI_KEY_ESCAPE)\n                return false;", ""),
    (X, "a page that has not moved is listed again", "if (changed)\n                mixer_list(now);", "mixer_list(now);"),
    # ---- the tray's volume cell and the volume keys ----
    (C, "the volume step is ten", "#define COMPOSITOR_VOLUME_STEP 5", "#define COMPOSITOR_VOLUME_STEP 10"),
    (C, "a volume key does not unmute", "                percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;\n                muted = 0;", "                percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;"),
    (C, "a held Mute mutes again", "                if (value == 1)\n                        compositor_volume_step(0, 1);", "                compositor_volume_step(0, 1);"),
    (C, "our own change undone by the page at once", "        if (now >= compositor_volume.hold_until)\n        {", "        if (1)\n        {"),
    (C, "a server that is gone leaves the cell", "                        compositor_volume.present = 0, compositor_volume.recording = 0;\n                else", "                        compositor_volume.present = compositor_volume.present;\n                else"),
    (C, "the wheel up lowers", "compositor_volume_step(COMPOSITOR_VOLUME_STEP * (notch / 120), 0);", "compositor_volume_step(-COMPOSITOR_VOLUME_STEP * (notch / 120), 0);"),
    (C, "a press at 100 writes the file", "        if (percent != compositor_volume.percent)\n        {\n                int at = 0;", "        if (1)\n        {\n                int at = 0;"),
    (C, "the volume keys with the keys of a window", "(code == CK_KEY_MUTE || code == CK_KEY_VOLUME_DOWN || code == CK_KEY_VOLUME_UP || code == CK_KEY_MICMUTE))\n        {\n                if (value)\n                        compositor_volume_key(code, value);\n                return;\n        }", "(code == 0))\n        {\n                if (value)\n                        compositor_volume_key(code, value);\n                return;\n        }"),
    (C, "the mute file written for a volume change", "        if (muted != compositor_volume.muted)\n                compositor_volume_write", "        if (1)\n                compositor_volume_write"),
    (T, "the tray not longer for the volume cell", "(1 + (taskbar.battery_item ? 1 : 0) + (taskbar.volume_item ? 1 : 0) + (taskbar.mic_item ? 1 : 0))", "(1 + (taskbar.battery_item ? 1 : 0) + (taskbar.mic_item ? 1 : 0))"),
    (T, "the clock under the volume cell", "(taskbar.battery_item ? text : 0) + (taskbar.volume_item ? text : 0) + (taskbar.mic_item ? text : 0), *length = text;", "(taskbar.battery_item ? text : 0) + (taskbar.mic_item ? text : 0), *length = text;"),
    (T, "the battery under the volume cell", "*along = lay->tray_at + (taskbar.volume_item ? text : 0) + (taskbar.mic_item ? text : 0), *length = text;\n        }\n        else if (which == TASKBAR_VOLUME)", "*along = lay->tray_at + (taskbar.mic_item ? text : 0), *length = text;\n        }\n        else if (which == TASKBAR_VOLUME)"),
    (T, "the volume cell never hit", "for (int which = TASKBAR_MIC; which < (int)view->count; which++)", "for (int which = TASKBAR_LAUNCHER; which < (int)view->count; which++)"),
    (T, "a tray of the person's own format has a cell", "if (!taskbar.volume_item || taskbar.tray_custom)", "if (!taskbar.volume_item)"),
    (T, "the muted speaker not struck", "ui_speaker(&ui, x - lay->bx + (width - size) / 2, y - lay->by + (height - size) / 2, size, waves, taskbar.volume_muted, ink | 0xff000000u);", "ui_speaker(&ui, x - lay->bx + (width - size) / 2, y - lay->by + (height - size) / 2, size, waves, 0, ink | 0xff000000u);"),
    (C, "the master told to nobody", "answer = ctl_set_sink_volume(&compositor_ctl, sink, (unsigned long)volume * 65536 / 100);", "answer = ctl_set_sink_volume(&compositor_ctl, \"\", (unsigned long)volume * 65536 / 100);"),
    (C, "the told percent not scaled", "(unsigned long)volume * 65536 / 100", "(unsigned long)volume"),
    (C, "the mute not told", "if (answer >= 0 && mute >= 0)", "if (answer >= 0 && mute > 5)"),
    (C, "the mute told the wrong way", "answer = ctl_set_sink_mute(&compositor_ctl, sink, mute);", "answer = ctl_set_sink_mute(&compositor_ctl, sink, !mute);"),
    (C, "a failed socket kept", "        if (answer == CTL_FAILED)\n        {\n                ctl_disconnect(&compositor_ctl);", "        if (answer == 12345)\n        {\n                ctl_disconnect(&compositor_ctl);"),
    (C, "a failed socket tried again at once", "#define COMPOSITOR_VOLUME_AGAIN_NS 2000000000ull", "#define COMPOSITOR_VOLUME_AGAIN_NS 0ull"),
    (C, "the server waited for as long as the mixer does", "compositor_ctl.timeout_ms = COMPOSITOR_VOLUME_WAIT_MS;", "compositor_ctl.timeout_ms = 0;"),
    (C, "a page with no sink name told", "if (!compositor_volume.present || !sink[0])", "if (!compositor_volume.present)"),
    (C, "everything told at every change", "compositor_volume_tell(percent != compositor_volume.percent ? percent : -1, muted != compositor_volume.muted ? muted : -1);", "compositor_volume_tell(percent, muted);"),
    (X, "the popup lists the programs", "programs = mixer.have && (!mixer.quick || mixer.recording) && !off;", "programs = mixer.have && !off;"),
    (X, "a program that records what plays is shown", "if (stream->index == MIXER_NONE || (mixer.recording && (stream->flags & AUDIO_STATE_MONITOR)))", "if (stream->index == MIXER_NONE)"),
    (X, "recording turned off listed anyway", "programs = mixer.have && (!mixer.quick || mixer.recording) && !off;", "programs = mixer.have && (!mixer.quick || mixer.recording);"),
    (X, "the microphone not the Recording page's first strip", "else if (programs && page->input[0])", "else if (0)"),
    (X, "the microphone's volume sent to the sink", "answer = ctl_set_source_volume(&big->ctl, big->source, volume);", "answer = ctl_set_sink_volume(&big->ctl, big->sink, volume);"),
    (X, "the microphone's mute sent to the sink", "answer = ctl_set_source_mute(&big->ctl, big->source, strip->muted);", "answer = ctl_set_sink_mute(&big->ctl, big->sink, strip->muted);"),
    (X, "a record stream's volume sent as a playing stream's", "mixer.recording ? ctl_set_output_volume(&big->ctl, number, volume) : ctl_set_input_volume(&big->ctl, number, volume)", "ctl_set_input_volume(&big->ctl, number, volume)"),
    (X, "a record stream's mute sent as a playing stream's", "mixer.recording ? ctl_set_output_mute(&big->ctl, number, strip->muted) : ctl_set_input_mute(&big->ctl, number, strip->muted)", "ctl_set_input_mute(&big->ctl, number, strip->muted)"),
    (X, "a page change that does not send first", "        mixer_flush();\n        mixer.recording = recording;", "        mixer.recording = recording;"),
    (X, "Allow does not have the keys", "mixer.allow.keys = 1;", "mixer.allow.keys = 0;"),
    (X, "Allow turns recording off", "compositor_volume_write(mixer.capture_path, \"on\\n\");", "compositor_volume_write(mixer.capture_path, \"off\\n\");"),
    (X, "the Mixer row starts the output page from Recording", "        if (mixer.recording)\n                words[1] = (string_address) \"-i\", words[2] = (string_address) \"-f\", words[3] = face;", "        if (0)\n                words[1] = (string_address) \"-i\", words[2] = (string_address) \"-f\", words[3] = face;"),
    (X, "the tabs not drawn", "ui_control_draw(ui, &mixer.tabs);", "(void)0;"),
    (X, "the recording popup as narrow as its strips", "return count < 3 ? 3 : count > 4 ? 4 : count;", "return count;"),
    (X, "Page Down shows Playback", "mixer_page(event->a == UI_KEY_TAB ? !mixer.recording : event->a == UI_KEY_PAGE_DOWN);", "mixer_page(event->a == UI_KEY_TAB ? !mixer.recording : event->a == UI_KEY_PAGE_UP);"),
    (X, "no microphone not said", "ui_strips_message(ui, strips, WORDS(mixer_no_mic), false);", "(void)strips;"),
    (U, "the microphone drawn as a speaker", "if (strip->master == 2)", "if (0)"),
    (U, "the microphone not struck", "if (struck && y >= -10.0 && y <= 10.0)", "if (0)"),
    (U, "a press on a tab not the tabs'", "bits |= event->type == WINDOW_EVENT_BUTTON && (c->down || had) ? UI_TABS_TAKEN : 0;", "bits |= 0;"),
    (U, "the tabs take another control's release", "(ui_kit_held && ui_kit_held != c) ||\n            (event->type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT))", "(event->type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT))"),
    (C, "the microphone key does nothing", "if (value == 1)\n                        compositor_mic_step();", "if (value == 1)\n                        (void)0;"),
    (C, "a held microphone key mutes again", "if (value == 1)\n                        compositor_mic_step();", "compositor_mic_step();"),
    (C, "a program that records what plays is a microphone's", "!(copy->record[k].flags & (AUDIO_STATE_MONITOR | AUDIO_STATE_CORKED))", "!(copy->record[k].flags & AUDIO_STATE_CORKED)"),
    (C, "a paused recording is a microphone's", "!(copy->record[k].flags & (AUDIO_STATE_MONITOR | AUDIO_STATE_CORKED))", "!(copy->record[k].flags & AUDIO_STATE_MONITOR)"),
    (C, "recording turned off is a microphone's", "copy->capture && copy->record[k].index != 0xffffffffu", "copy->record[k].index != 0xffffffffu"),
    (C, "the microphone's mute not kept", "        compositor_volume_write(compositor_volume_path[4], muted ? \"on\\n\" : \"off\\n\");", ""),
    (C, "the microphone's mute told to the sink", "ctl_set_source_mute(&compositor_ctl, source, muted)", "ctl_set_sink_mute(&compositor_ctl, source, muted)"),
    (C, "the microphone's mute thrown not shown", "compositor_volume.recording || compositor_volume.mic_flash_until", "compositor_volume.recording"),
    (C, "the microphone's cell asks for the volume's popup", "compositor_script_say(\"mic_button\");", "compositor_script_say(\"volume_button\");"),
    (T, "the microphone not struck when muted", "size, taskbar.mic_muted, ui_role(UI_ERROR)", "size, 0, ui_role(UI_ERROR)"),
    (U, "a popup ended before it had the keys", "if (app->dismiss && app->had_keys && !focused)", "if (app->dismiss && !focused)"),
    (U, "a window that is not a popup ended by losing the keys", "if (app->dismiss && app->had_keys && !focused)", "if (app->had_keys && !focused)"),
    (X, "the off message missing", "ui_strips_message(ui, strips, WORDS(mixer_off), false);", "(void)strips;"),
]

check = os.path.join(root, "test/checks.c")


def run(index):
    path, name, old, new = M[index]
    if only and name not in only:
        return None
    text = open(os.path.join(root, path)).read()
    if text.count(old) != 1:
        return (name, "NOT APPLIED", "%d times" % text.count(old))
    d = os.path.join(work, "m%d" % index)
    os.makedirs(d + "/test")
    # the tree as links, the one file a copy with the edit
    subprocess.run(["cp", "-as", os.path.join(root, "src"), d + "/src"], check=True)
    os.unlink(os.path.join(d, path))
    open(os.path.join(d, path), "w").write(text.replace(old, new, 1))
    os.symlink(os.path.join(root, "artifacts"), d + "/artifacts") if os.path.exists(os.path.join(root, "artifacts")) else None
    os.symlink(check, d + "/test/checks.c")
    exe = d + "/check"
    cc = subprocess.run(["gcc", "-O2", "-static", "-nostdlib", "-nostartfiles", "-fno-stack-protector", "-fno-builtin", "-march=x86-64", "-w", "-T",
                         os.path.join(root, "src/build/spark.ld"), "-Wl,-e,_start", "-Wl,--build-id=none", "-Wl,--no-warn-rwx-segments", "-DCHECK_terminal",
                         "-fwhole-program", "-o", exe, d + "/test/checks.c"], capture_output=True, text=True, errors="replace", cwd=root)
    if cc.returncode:
        return (name, "BUILD ERROR", cc.stderr[:300])
    try:
        r = subprocess.run([exe] + fonts, capture_output=True, text=True, errors="replace", timeout=300, cwd=root)
    except subprocess.TimeoutExpired:
        return (name, "RED (hang)", "")
    # The launcher's checks read a file in /tmp that mutants running side by side all append to: their failures are the race's and not the mutant's.
    fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l and not l.strip().startswith("FAIL launcher")]
    shutil.rmtree(d, ignore_errors=True)
    if r.returncode == 0 and not fails:
        return (name, "SURVIVED", "")
    return (name, "RED", ("%d failing" % len(fails) if fails else "crashed (%d)" % r.returncode) + (" | " + fails[0][:110] if fails else ""))


with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    results = [r for r in pool.map(run, range(len(M))) if r]
survived = 0
for name, status, detail in results:
    print("%-52s %-12s %s" % (name, status, detail))
    survived += status in ("SURVIVED", "NOT APPLIED", "BUILD ERROR")
print("mutations: %d, red: %d, survived or unusable: %d" % (len(results), len(results) - survived, survived))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if survived else 0)
