#!/usr/bin/env python3
"""Mutation proofs for the UI kit's pieces checked in CHECK_terminal: the mixer's pieces of the kit (src/canvas/ui/ui.c: the level meter, the vertical slider, the strips) and the mixer program
(src/canvas/ui/mixer.c), and the terminal's kit faces (src/canvas/ui/terminal.c).

Each mutation is one edit to a copy of one of those files, built into CHECK_terminal with gcc and run from the tree's root; the check must turn red.
A mutation that survives is a hole in the check. The mutant's tree is the real one with the one file changed (symbolic links for every other),
so a build costs the check's compile and nothing else.

    python3 test/ui_mutations.py [NAME...]       from the tree's root; 6 builds at a time, about 5 minutes

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
TM = "src/canvas/ui/terminal.c"
L = "src/canvas/ui/launcher.c"
R = "src/sh/registry.c"

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
    (U, "slider value upside down", "return ui_dev(ui, c->y) + ui_dev(ui, c->h) - y;", "return ui_dev(ui, c->y) + y;"),
    (U, "slider travel without the thumb", "track = ui_slider_span(ui, c) - thumb;", "track = ui_slider_span(ui, c);"),
    (U, "slider value not snapped", "        return ui_slider_snap(c, c->low + ui_qmul(ratio, c->high - c->low));\n}\n\n// A slider's value said", "        return c->low + ui_qmul(ratio, c->high - c->low);\n}\n\n// A slider's value said"),
    (U, "slider fill past its thumb", "fill_end = ratio >= UI_ONE ? span : stop - gap", "fill_end = ratio >= UI_ONE ? span : stop + gap"),
    (U, "slider muted fill not grey", "fill_ink = c->on ? ui_role(UI_OUTLINE) : accent", "fill_ink = accent"),
    (U, "stood slider drag has a dead zone of the flat one", "if (!c->moved && along < 4 * ui->scale / 100 && along > -4 * ui->scale / 100)\n                                        return null;", "if (!c->moved && along < 400 * ui->scale / 100 && along > -400 * ui->scale / 100)\n                                        return null;"),
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
    (U, "strips not mirrored", "t->node[at].x = ui_mirror(t->node[at].x, t->node[at].w, 0, s->w) + s->x, t->node[at].y += s->y;", "t->node[at].x += s->x, t->node[at].y += s->y;"),
    (U, "no gap after the device", "n->margin[0] = k == 1 ? ui_px(ui, UI_STRIP_GAP) : 0;", "n->margin[0] = 0;"),
    (U, "focus lost with the order", "                if (s->item[k].id == s->focus_id)\n                {", "                if (0)\n                {"),
    (U, "keys do not scroll to the strip", "        ui_strips_reveal(s, k);\n        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;", "        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;"),
    (U, "a drag of the ground moves the wrong way", "ui_scroll_drag(&s->scroll, (s->grab_x - x) * ui_dir());", "ui_scroll_drag(&s->scroll, (x - s->grab_x) * ui_dir());"),
    (U, "the keys' strip not said", "unsigned int said = keys && s->focus == k && s->keyed ? ui_role(UI_PRIMARY) : ink;", "unsigned int said = ink;"),
    (U, "a mouse press says the strip as the keys do", "unsigned int said = keys && s->focus == k && s->keyed ? ui_role(UI_PRIMARY) : ink;", "unsigned int said = keys && s->focus == k ? ui_role(UI_PRIMARY) : ink;"),
    (U, "a long name not cut to the slider", "ui_fit(ui, (string_address)strip->name, UI_LABEL_MEDIUM, name.h, fitted, sizeof(fitted))", "(string_address)strip->name"),
    (U, "a slider does not take the strip's height", "n->gap = gap, n->size[1] = (struct ui_size){UI_GROW, 1, ui_px(ui, UI_STRIP_LEAST), UI_TREE_MOST, 0};", "n->gap = gap, n->size[1] = (struct ui_size){UI_FIXED, ui_px(ui, UI_STRIP_LEAST), 0, UI_TREE_MOST, 0};"),
    (U, "the name not turned for a window read from the right", "b32 light = window_light(argb & 0xffffffu), down = ui_rtl();", "b32 light = window_light(argb & 0xffffffu), down = 0;"),
    (U, "a slide's target not taken", "b64 target = changed->sliding ? changed->slide.to : changed->value;", "b64 target = changed->value;"),
    (U, "the speaker never struck", "ui_speaker(ui, at.x + (at.w - size) / 2, at.y + (at.h - size) / 2, size, waves, strip->muted, ink);", "ui_speaker(ui, at.x + (at.w - size) / 2, at.y + (at.h - size) / 2, size, waves, 0, ink);"),
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
    (T, "the tray not longer for the volume cell", "items += (shown) != 0;", "items += (shown) != 0 && slot != TASKBAR_VOLUME;"),
    (T, "the clock where the volume cell is: the cell takes no room in the tray", "n = taskbar_open(t, slot, m, 0, cell);", "n = taskbar_open(t, slot, m, 0, slot == TASKBAR_VOLUME ? 0 : cell);"),
    (T, "the battery before the volume cell", "        CELL(TASKBAR_VOLUME, taskbar.volume_item, glyph, taskbar_volume_cell(view, t, n, ink, ground))                  \\\n        CELL(-4, taskbar.battery_item, 0, taskbar_say(view, t, n, text, ink, ground))                                   \\\n", "        CELL(-4, taskbar.battery_item, 0, taskbar_say(view, t, n, text, ink, ground))                                   \\\n        CELL(TASKBAR_VOLUME, taskbar.volume_item, glyph, taskbar_volume_cell(view, t, n, ink, ground))                  \\\n"),
    (T, "the volume cell never hit", "return which == -4 || which == -3 ? TASKBAR_TRAY : which;", "return which == -4 || which == -3 || which == TASKBAR_VOLUME ? TASKBAR_TRAY : which;"),
    (T, "a tray of the person's own format has a cell", "if ((shown) && !taskbar.tray_custom)", "if ((shown))"),
    (T, "the muted speaker not struck", "ui_speaker(&ui, x, y, mark->w, waves, taskbar.volume_muted, ink | 0xff000000u);", "ui_speaker(&ui, x, y, mark->w, waves, 0, ink | 0xff000000u);"),
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
    (X, "Allow does not have the keys", "big->view.group.keys = 1;", "big->view.group.keys = 0;"),
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
    (T, "the microphone not struck when muted", "mark->w, taskbar.mic_muted, compositor_ink_role(UI_ERROR)", "mark->w, 0, compositor_ink_role(UI_ERROR)"),
    (T, "the notices dot in the kit's roles, which the compositor never makes", "unsigned int colour = compositor_ink_role(taskbar.notices_severity == WINDOWS_ISSUE_ERROR ? UI_ERROR : UI_WARNING);", "unsigned int colour = ui_role(taskbar.notices_severity == WINDOWS_ISSUE_ERROR ? UI_ERROR : UI_WARNING);"),
    (T, "the tabs centred in what the tray leaves of the band", "const struct ui_size gives = {UI_PERCENT, 100, 0, UI_TREE_MOST, 1};", "const struct ui_size gives = {UI_GROW, 1, 0, UI_TREE_MOST, 0};"),
    # ---- the band's tree: kept, made again when what it was made from changes, and what is read from it ----
    (T, "the band laid out again at every question", "if (!band->tree.count || memory_compare(&band->key, &key, sizeof(key)))", "if (1)"),
    (T, "a title that grew leaves its tab as long as it was", "                memory_copy(key.lengths, view->lengths, view->count < WINDOWS ? view->count : WINDOWS);", "                (void)0;"),
    (T, "a screen put elsewhere leaves its band where it was", "struct taskbar_key key = {taskbar.shift, view->width, view->height, view->scale, view->x, view->y,", "struct taskbar_key key = {taskbar.shift, view->width, view->height, view->scale, 0, 0,"),
    (T, "one tree for the bands of every screen", "(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0", "0"),
    (T, "a band read from the right is not turned over", "        if (!m && language_rtl())\n                ui_tree_mirror(t);", "        if (0)\n                ui_tree_mirror(t);"),
    (T, "a finger reaches no further than the tab", "        grown[!t->node[1].dir] = reach;", "        grown[!t->node[1].dir] = 0;"),
    (T, "a tab is as long as its title however many there are", "taskbar_open(t, at, m, gap, wanted < most ? wanted : most);", "taskbar_open(t, at, m, gap, wanted);"),
    (U, "the layout's hit answers an element with no name", "if (n->id && depth[i] > (best < 0 ? 0 : depth[best]))", "if (depth[i] > (best < 0 ? 0 : depth[best]))"),
    (U, "the layout's hit grows the root by the reach", "int rx = i && reach ? reach[0] : 0, ry = i && reach ? reach[1] : 0, above = i ? depth[n->parent] : 0;", "int rx = reach ? reach[0] : 0, ry = reach ? reach[1] : 0, above = i ? depth[n->parent] : 0;"),
    (U, "the layout's root put back at the origin", "*ui_tree_at(&t->node[0], a) = ui_tree_place(*ui_tree_at(&t->node[0], a));", "*ui_tree_at(&t->node[0], a) = 0;"),
    (U, "the layout's mirror about the origin, not the root", "t->node[i].x = 2 * t->node[0].x + t->node[0].w - t->node[i].x - t->node[i].w;", "t->node[i].x = t->node[0].w - t->node[i].x - t->node[i].w;"),
    # ---- the fullscreen slide (`taskbar fullscreen on`) ----
    (T, "fullscreen on, no slide: the band jumps off at once", "taskbar.slide_at = animate ? now : 0;", "taskbar.slide_at = 0;"),
    (T, "fullscreen on, the band never leaves its edge", "int off = (int)(((long long)thick * (long long)shift) / (long long)UI_ONE);", "int off = 0;"),
    (T, "fullscreen hides the band with the setting off", "taskbar.hide_fullscreen && fullscreen ? UI_ONE : 0", "fullscreen ? UI_ONE : 0"),
    (C, "a fullscreen window does not hide the band", "taskbar_slide_now(compositor_fullscreen_up(), now, compositor_glides(), &before)", "taskbar_slide_now(0, now, compositor_glides(), &before)"),
    (C, "a fullscreen window put away hides the band", "if (compositor.fullscreen[row] && compositor_visible((int)row))", "if (compositor.fullscreen[row])"),
    (C, "a slide damages only where the band goes", "taskbar_slide_rect(&view, before, &x, &y, &width, &height);", "taskbar_slide_rect(&view, taskbar_shift(), &x, &y, &width, &height);"),
    (C, "a slide still going is not damaged after a present", "if (taskbar_sliding(now))", "if (0)"),
    (U, "a popup ended before it had the keys", "if (app->dismiss && app->had_keys && !focused)", "if (app->dismiss && !focused)"),
    (U, "a window that is not a popup ended by losing the keys", "if (app->dismiss && app->had_keys && !focused)", "if (app->had_keys && !focused)"),
    (U, "a page asked is not taken", "__atomic_exchange_n(&app->own->table->page[app->own->row], 0u, __ATOMIC_ACQ_REL)", "__atomic_load_n(&app->own->table->page[app->own->row], __ATOMIC_ACQUIRE)"),
    (X, "a page asked is ignored", "                mixer_page(page == 1);", "                (void)page;"),
    (X, "a page asked is the other page", "                mixer_page(page == 1);", "                mixer_page(page == 0);"),
    (X, "an output with nothing plugged in is not dim", "ui_strips_dim(&mixer.big->strips, unplugged);", "ui_strips_dim(&mixer.big->strips, 0);"),
    (X, "the unplugged note missing", "mixer.note = unplugged ? WORDS(mixer_unplugged) : null;", "mixer.note = null;"),
    (X, "the unplugged note not drawn", "                        ui_text(mixer.note, .class = \"warning\");", "                        ui_text(\"\", .class = \"warning\");"),
    (U, "a dim strip drawn in the ink", "strip->muted || strip->dim ? UI_ON_SURFACE_VARIANT : UI_ON_SURFACE", "strip->muted ? UI_ON_SURFACE_VARIANT : UI_ON_SURFACE"),
    (X, "the off message missing", "ui_strips_message(ui, strips, WORDS(mixer_off), false);", "(void)strips;"),
    # ---- the terminal's kit: its faces are the grid's, so the actions bar's labels measure and draw ----
    (TM, "terminal overlay's kit is the whole window (a knob hung below the last line writes past the picture)", "        kit.width = (int)terminal.width - (int)terminal.pad;\n        kit.height = kit.bottom = (int)terminal.height - (int)terminal.pad_y;\n", ""),
    (TM, "terminal's touch lift is never the controller's (the tap on the selection, the tap elsewhere)", "                if (!(event->flags & WINDOW_KEY_DOWN) && !s->finger && !(event->flags & WINDOW_KEY_HOLD))\n                        return false;\n", "                if (!(event->flags & WINDOW_KEY_DOWN))\n                        return false;\n"),
    (C, "a second device's stale place moves the pointer while a finger is down (the hold is at the tablet's place)", "                if (compositor.touch.gesture.mode == UI_TOUCH_NONE)\n", "                if (1)\n"),
    (C, "a long press is placed at the pointer, not the finger's place (the hold's place line removed)", "                                compositor_predict_place(COMPOSITOR_PREDICT_TOUCH, compositor.touch.x, compositor.touch.y, now);\n", ""),
    (TM, "the terminal's kit has no text scratch (the bar's labels are blank)", "        ui_scratch_fit(&terminal.ui, terminal.width);\n", ""),
    (TM, "the terminal's kit text is in the grid's mono face (the menu and the bar in mono)", "        terminal.ui.regular = ui_face_load(&terminal.ui.faces[0], terminal_ui_regular_path) ? &terminal.ui.faces[0] : terminal.plain[0];\n", "        terminal.ui.regular = terminal.plain[0];\n"),
    (TM, "terminal kit has no faces (the bar's labels fault)", "        terminal.ui.regular = terminal.plain[0];\n        terminal.ui.bold = terminal.bold_count ? terminal.bold[0] : terminal.plain[0];\n", ""),
    # ---- the keys' focus: a ring only for the keys, and only where no fill says where they are ----
    (U, "a click leaves the ring on a button", "        if (c->focused && !c->disabled && ui_kit.keyed)", "        if (c->focused && !c->disabled)"),
    (U, "a press does not take the hand off the keys", "        if ((event->flags & WINDOW_KEY_TOUCH) || (event->type == WINDOW_EVENT_BUTTON && down))\n                ui_kit.keyed = 0;", "        if (event->flags & WINDOW_KEY_TOUCH)\n                ui_kit.keyed = 0;"),
    (U, "a finger does not take the hand off the keys", "        if ((event->flags & WINDOW_KEY_TOUCH) || (event->type == WINDOW_EVENT_BUTTON && down))\n                ui_kit.keyed = 0;", "        if (event->type == WINDOW_EVENT_BUTTON && down)\n                ui_kit.keyed = 0;"),
    (U, "any key puts the hand on the keys", "        else if (event->type == WINDOW_EVENT_KEY && down &&\n                 (event->a == 15 ||", "        else if (event->type == WINDOW_EVENT_KEY && down &&\n                 (event->a == 30 || event->a == 15 ||"),
    (U, "an arrow does not put the hand on the keys", "event->a == 106 || event->a == 107 ||\n                  event->a == 108 || event->a == 109))", "event->a == 106 || event->a == 107 ||\n                  event->a == 109))"),
    (U, "a lit row is ringed", "                // The fill is all that says the keys are on the row: a lit row has no ring (ui_kit_focus_ring).\n", "                if (l->keys && !ui->glass)\n                {\n                        struct ui_box ring = box;\n\n                        ring.x0 += 512, ring.y0 += 512, ring.x1 -= 512, ring.y1 -= 512;\n                        ui_kit_focus_ring(ui, &ring);\n                }\n"),
    (U, "the column's chosen group is ringed", "                // The keys in the column have no ring: the pip is the chosen group and the group the keys are on, and it says so.\n", "                if (c->focused && !c->disabled)\n                {\n                        struct ui_box ring = pip;\n\n                        ring.x0 += 512, ring.y0 += 512, ring.x1 -= 512, ring.y1 -= 512;\n                        ui_kit_focus_ring(ui, &ring);\n                }\n"),
    # ---- a virtual list: any number of rows, the ones near the view laid out ----
    (U, "End of a virtual list is the end of its places", "        case UI_KEY_END: by = l->virtual ? l->total : l->count; break;", "        case UI_KEY_END: by = l->count; break;"),
    (U, "a virtual list is as long as its places", "        l->content = l->total * row;\n", "        l->content = l->count * row;\n"),
    (U, "a row far from the places is not revealed", "        if (l->virtual && l->focus >= 0 && l->focus < l->total)\n        {\n                // The row is where its number puts it", "        if (0)\n        {\n                // The row is where its number puts it"),
    (U, "a virtual list's places do not follow the scroll", "        first = at_rest / row - UI_LIST_MARGIN;\n", "        first = 0;\n"),
    (U, "a point on a virtual list is a place, not a row", "                return inside >= 0 && inside / row < l->total ? inside / row : -1;", "                return inside >= 0 && inside / row < l->count ? inside / row : -1;"),
    (U, "a click leaves the light row's edge", "                edged = l->keys && ui_kit.keyed && !ui->glass && ui_kit_light(ui);", "                edged = l->keys && !ui->glass && ui_kit_light(ui);"),
    (U, "a dark row has the edge too", "                edged = l->keys && ui_kit.keyed && !ui->glass && ui_kit_light(ui);", "                edged = l->keys && ui_kit.keyed && !ui->glass;"),
    (U, "the edge is not turned for a window read from the right", "x = ui_rtl() ? across + across_w - wide : across;", "x = across;"),
    (L, "a name of PATH is drawn as its bytes are", "                ui_label_foreign(into, room, (const p8 address_to)name, string_length((string_address)name));", "                string_copy_bounded((string_address)into, (string_address)name, room);"),
    # ---- the launcher's rows, edge to edge ----
    (L, "the launcher's rows are in from the bar's margin", "        launcher.list.row_x = 0, launcher.list.row_w = launcher.width;\n", ""),
    # ---- a mouse drags a list unless a person turned it off ----
    (R, "the mouse's drag of a list is off unless set", '.path = SETTING_SCROLL_DRAG_PATH, .standard = "on",', '.path = SETTING_SCROLL_DRAG_PATH, .standard = "off",'),
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
